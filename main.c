/******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for the PSoC 4: CY8CPROTO-041TP Demo
 *              code example for ModusToolbox.
 * 
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * Copyright 2020-2024, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cycfg_capsense.h"
#include <stdio.h>
#include <stdlib.h>
/*******************************************************************************
 * User configurable Macros
 ********************************************************************************/
/*Enables the Runtime measurement functionality used to for processing time measurement */
#define ENABLE_RUN_TIME_MEASUREMENT     (0u)

/* Enable this, if Tuner needs to be enabled */
#define ENABLE_TUNER                    (1u)

/*Enable PWM controlled LEDs*/
#define ENABLE_PWM_LED                  (1u)

/* 128Hz Refresh rate in Active mode */
#define ACTIVE_MODE_REFRESH_RATE        (128u)

/* 32Hz Refresh rate in Active-Low Refresh rate(ALR) mode */
#define ALR_MODE_REFRESH_RATE           (32u)

/* Timeout to move from ACTIVE mode to ALR mode if there is no user activity */
#define ACTIVE_MODE_TIMEOUT_SEC         (10u)

/* Timeout to move from ALR mode to WOT mode if there is no user activity */
#define ALR_MODE_TIMEOUT_SEC            (5u)

/* Active mode Scan time calculated in us ~= 208us */
#define ACTIVE_MODE_FRAME_SCAN_TIME     (208u)

/* Active mode Processing time in us ~= 111us with LEDs and Tuner disabled*/
#define ACTIVE_MODE_PROCESS_TIME        (111u)

/* ALR mode Scan time calculated in us ~= 208us */
#define ALR_MODE_FRAME_SCAN_TIME        (208u)

/* ALR mode Processing time in us ~= 111us with LEDs and Tuner disabled*/
#define ALR_MODE_PROCESS_TIME           (111u)

#define MAXIMUM_BRIGHTNESS_LED          (255u)

/*******************************************************************************
 * Macros
 ********************************************************************************/
#define CAPSENSE_MSC0_INTR_PRIORITY     (3u)
#define CY_ASSERT_FAILED                (0u)

/* EZI2C interrupt priority must be higher than CAPSENSE&trade; interrupt. */
#define EZI2C_INTR_PRIORITY             (2u)

#define ILO_FREQ                        (40000u)
#define TIME_IN_US                      (1000000u)

#define MINIMUM_TIMER                   (TIME_IN_US / ILO_FREQ)
#if ((TIME_IN_US / ACTIVE_MODE_REFRESH_RATE) > (ACTIVE_MODE_FRAME_SCAN_TIME + ACTIVE_MODE_PROCESS_TIME))
#define ACTIVE_MODE_TIMER           (TIME_IN_US / ACTIVE_MODE_REFRESH_RATE - \
        (ACTIVE_MODE_FRAME_SCAN_TIME + ACTIVE_MODE_PROCESS_TIME))
#elif
#define ACTIVE_MODE_TIMER           (MINIMUM_TIMER)
#endif

#if ((TIME_IN_US / ALR_MODE_REFRESH_RATE) > (ALR_MODE_FRAME_SCAN_TIME + ALR_MODE_PROCESS_TIME))
#define ALR_MODE_TIMER              (TIME_IN_US / ALR_MODE_REFRESH_RATE - \
        (ALR_MODE_FRAME_SCAN_TIME + ALR_MODE_PROCESS_TIME))
#elif
#define ALR_MODE_TIMER              (MINIMUM_TIMER)
#endif

#define ACTIVE_MODE_TIMEOUT             (ACTIVE_MODE_REFRESH_RATE * ACTIVE_MODE_TIMEOUT_SEC)

#define ALR_MODE_TIMEOUT                (ALR_MODE_REFRESH_RATE * ALR_MODE_TIMEOUT_SEC)

#define TIMEOUT_RESET                   (0u)

#if ENABLE_RUN_TIME_MEASUREMENT
#define SYS_TICK_INTERVAL           (0x00FFFFFF)
#define TIME_PER_TICK_IN_US         ((float)1/CY_CAPSENSE_CPU_CLK)*TIME_IN_US
#endif

/* Touch status of the proximity sensor */
#define TOUCH_STATE                     (3u)

/* Proximity status of the proximity sensor */
#define PROX_STATE                      (1u)


#if ENABLE_PWM_LED
#define CYBSP_LED_OFF                   (0u)
#define CYBSP_LED_ON                    (1u)
#endif

/* Current Sensor slot number for proximity widget - can be different in other CE */
#define PROXIMITY_SENSOR_SLOT_INDEX     (11u)

/*****************************************************************************
 * Finite state machine states for device operating states
 *****************************************************************************/
typedef enum
{
    ACTIVE_MODE = 0x01u,    /* Active mode - All the sensors are scanned in this state
     * with highest refresh rate */
    ALR_MODE = 0x02u,       /* Active-Low Refresh Rate (ALR) mode - All the sensors are
     * scanned in this state with low refresh rate */
    WOT_MODE = 0x03u        /* Wake on Touch (WoT) mode - Low Power sensors are scanned
     * in this state with lowest refresh rate */
} APPLICATION_STATE;

/*******************************************************************************
 * Function Prototypes
 ********************************************************************************/
static void initialize_capsense(void);
static void capsense_msc0_isr(void);

static void ezi2c_isr(void);
static void initialize_capsense_tuner(void);

#if ENABLE_RUN_TIME_MEASUREMENT
static void init_sys_tick();
static void start_runtime_measurement();
static uint32_t stop_runtime_measurement();
#endif

void led_control();
void PWM_initialisation(void);
uint16_t ALS_reading();


/* Deep Sleep Callback function */
void register_callback(void);
cy_en_syspm_status_t deep_sleep_callback(cy_stc_syspm_callback_params_t *callbackParams,
        cy_en_syspm_callback_mode_t mode);

/*******************************************************************************
 * Global Definitions
 *******************************************************************************/

/* Variables holds the current low power state [ACTIVE, ALR or WOT] */
APPLICATION_STATE capsense_state;

cy_stc_scb_uart_context_t CYBSP_UART_context;

cy_stc_scb_ezi2c_context_t ezi2c_context;

/* Callback parameters for custom, EzI2C */

/* Callback parameters for EzI2C */
cy_stc_syspm_callback_params_t ezi2cCallbackParams =
{
        .base       = SCB1,
        .context    = &ezi2c_context
};

/* Callback parameters for custom callback */
cy_stc_syspm_callback_params_t deepSleepCallBackParams = {
        .base       =  NULL,
        .context    =  NULL
};

/* Callback declaration for EzI2C Deep Sleep callback */
cy_stc_syspm_callback_t ezi2cCallback =
{
        .callback       = (Cy_SysPmCallback)&Cy_SCB_EZI2C_DeepSleepCallback,
        .type           = CY_SYSPM_DEEPSLEEP,
        .skipMode       = 0UL,
        .callbackParams = &ezi2cCallbackParams,
        .prevItm        = NULL,
        .nextItm        = NULL,
        .order          = 0
};

/* Callback declaration for Custom Deep Sleep callback */
cy_stc_syspm_callback_t deepSleepCb =
{
        .callback       = &deep_sleep_callback,
        .type           = CY_SYSPM_DEEPSLEEP,
        .skipMode       = 0UL,
        .callbackParams = &deepSleepCallBackParams,
        .prevItm        = NULL,
        .nextItm        = NULL,
        .order          = 2
};

/* Variable to get the object distance from proximity range. */
#if ENABLE_PWM_LED

/* Initial value of the led brightness for proximity. */
uint32_t proxLedBrightness = 0;

/* To get run time raw count value for proximity Widget */
uint16_t proxMaxRawCount = 0;

/* To get run time diff count value for proximity Widget */
uint32_t maxDiffCount = 0;
#endif



/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  System entrance point. This function performs
 *  - initial setup of device
 *  - initialize CAPSENSE&trade;
 *  - initialize tuner communication
 *  - scan touch input continuously at 3 different power modes
 *  - LED for touch indication
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    uint32_t capsense_state_timeout;
    uint32_t interruptStatus;

    #if ENABLE_RUN_TIME_MEASUREMENT
    static uint32_t active_processing_time;
    static uint32_t alr_processing_time;
    #endif

    result = cybsp_init() ;

    #if ENABLE_RUN_TIME_MEASUREMENT
    init_sys_tick();
    #endif

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Initialize the SAR ADC with the device configurator generated structure*/
    result = Cy_SAR_Init(SAR0, &pass_0_sar_0_config);
    if (result != CY_SAR_SUCCESS)
    {
        CY_ASSERT(0);
    }
    /* Enable the SAR ADC */
    Cy_SAR_Enable(SAR0);

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize EZI2C */
    initialize_capsense_tuner();  

    #if ENABLE_PWM_LED
    PWM_initialisation();
    #endif

    /* Register callbacks */
    register_callback();

    /* Define initial state of the device and the corresponding refresh rate*/
    capsense_state = ACTIVE_MODE;
    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;

    /* Initialize MSC CAPSENSE&trade; */
    initialize_capsense();

    /* Measures the actual ILO frequency and compensate MSCLP wake up timers */
    Cy_CapSense_IloCompensate(&cy_capsense_context);

    /* Configure the MSCLP wake up timer as per the ACTIVE mode refresh rate */
    Cy_CapSense_ConfigureMsclpTimer(ACTIVE_MODE_TIMER, &cy_capsense_context);

    for (;;)
    {

        switch(capsense_state)
        {
            case ACTIVE_MODE:

                Cy_CapSense_ScanAllSlots(&cy_capsense_context);

                interruptStatus = Cy_SysLib_EnterCriticalSection();

                while (Cy_CapSense_IsBusy(&cy_capsense_context))
                {
                #if ENABLE_PWM_LED
                    Cy_SysPm_CpuEnterSleep();
                #else
                    Cy_SysPm_CpuEnterDeepSleep();
                #endif

                    Cy_SysLib_ExitCriticalSection(interruptStatus);

                    /* This is a place where all interrupt handlers will be executed */
                    interruptStatus = Cy_SysLib_EnterCriticalSection();
                }

                #if ENABLE_RUN_TIME_MEASUREMENT
                active_processing_time=0;
                start_runtime_measurement();
                #endif

                Cy_SysLib_ExitCriticalSection(interruptStatus);

                Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

                /* Scan, process and check the status of the all Active mode sensors */
                if(Cy_CapSense_IsAnyWidgetActive(&cy_capsense_context))
                {
                    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;
                }
                else
                {
                    capsense_state_timeout--;

                    if(TIMEOUT_RESET == capsense_state_timeout)
                    {

                        capsense_state = ALR_MODE;
                        capsense_state_timeout = ALR_MODE_TIMEOUT;

                        /* Configure the MSCLP wake up timer as per the ALR mode refresh rate */
                        Cy_CapSense_ConfigureMsclpTimer(ALR_MODE_TIMER, &cy_capsense_context);
                    }
                }


                #if ENABLE_RUN_TIME_MEASUREMENT
                active_processing_time=stop_runtime_measurement();
                #endif

                break;
                /* End of ACTIVE_MODE */

                /* Active Low Refresh-rate Mode */
            case ALR_MODE :

                Cy_CapSense_ScanAllSlots(&cy_capsense_context);
                interruptStatus = Cy_SysLib_EnterCriticalSection();

                while (Cy_CapSense_IsBusy(&cy_capsense_context))
                {
                #if ENABLE_PWM_LED
                    Cy_SysPm_CpuEnterSleep();
                #else
                    Cy_SysPm_CpuEnterDeepSleep();
                #endif

                    Cy_SysLib_ExitCriticalSection(interruptStatus);

                    /* This is a place where all interrupt handlers will be executed */
                    interruptStatus = Cy_SysLib_EnterCriticalSection();
                }

                Cy_SysLib_ExitCriticalSection(interruptStatus);

                #if ENABLE_RUN_TIME_MEASUREMENT
                alr_processing_time=0;
                start_runtime_measurement();
                #endif

                Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

                /* Scan, process and check the status of the all Active mode sensors */
                if(Cy_CapSense_IsAnyWidgetActive(&cy_capsense_context))
                {
                    capsense_state = ACTIVE_MODE;
                    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;
                #if ENABLE_PWM_LED
                PWM_initialisation();
                #endif

                    /* Configure the MSCLP wake up timer as per the ACTIVE mode refresh rate */
                    Cy_CapSense_ConfigureMsclpTimer(ACTIVE_MODE_TIMER, &cy_capsense_context);
                }
                else
                {
                    capsense_state_timeout--;

                    if(TIMEOUT_RESET == capsense_state_timeout)
                    {
                        capsense_state = WOT_MODE;
                    }
                }

                #if ENABLE_RUN_TIME_MEASUREMENT
                alr_processing_time=stop_runtime_measurement();
                #endif

                break;
                /* End of Active-Low Refresh Rate(ALR) mode */

                /* Wake On Touch Mode */
            case WOT_MODE :

                /* Scanning only those low-power slots needed for the specific configuration*/
                Cy_CapSense_ScanAllLpSlots(&cy_capsense_context);

                interruptStatus = Cy_SysLib_EnterCriticalSection();

                while (Cy_CapSense_IsBusy(&cy_capsense_context))
                {
                    Cy_SysPm_CpuEnterDeepSleep();

                    Cy_SysLib_ExitCriticalSection(interruptStatus);

                    /* This is a place where all interrupt handlers will be executed */
                    interruptStatus = Cy_SysLib_EnterCriticalSection();
                }

                Cy_SysLib_ExitCriticalSection(interruptStatus);

                if (Cy_CapSense_IsAnyLpWidgetActive(&cy_capsense_context))
                {
                    capsense_state = ACTIVE_MODE;
                    capsense_state_timeout = ACTIVE_MODE_TIMEOUT;

                    #if ENABLE_PWM_LED
                    PWM_initialisation();
                    #endif

                    /* Configure the MSCLP wake up timer as per the ACTIVE mode refresh rate */
                    Cy_CapSense_ConfigureMsclpTimer(ACTIVE_MODE_TIMER, &cy_capsense_context);
                }
                else
                {
                    capsense_state = ALR_MODE;
                    capsense_state_timeout = ALR_MODE_TIMEOUT;

                    /* Configure the MSCLP wake up timer as per the ALR mode refresh rate */
                    Cy_CapSense_ConfigureMsclpTimer(ALR_MODE_TIMER, &cy_capsense_context);
                }

                break;
                /* End of "WAKE_ON_TOUCH_MODE" */

            default:
                /**  Unknown power mode state. Unexpected situation.  **/
                CY_ASSERT(CY_ASSERT_FAILED);
                break;
        }
        #if ENABLE_PWM_LED
        led_control();
        #endif

        #if ENABLE_TUNER
        /* Establishes synchronized communication with the CAPSENSE&trade; Tuner tool */
        Cy_CapSense_RunTuner(&cy_capsense_context);
        #endif
    }
}

/*******************************************************************************
 * Function Name: initialize_capsense
 ********************************************************************************
 * Summary:
 *  This function initializes the CAPSENSE&trade; and configures the CAPSENSE&trade;
 *  interrupt.
 *
 *******************************************************************************/
static void initialize_capsense(void)
{
    cy_capsense_status_t status = CY_CAPSENSE_STATUS_SUCCESS;

    /* CAPSENSE&trade; interrupt configuration MSCLP 0 */
    const cy_stc_sysint_t capsense_msc0_interrupt_config =
    {
            .intrSrc = CY_MSCLP0_LP_IRQ,
            .intrPriority = CAPSENSE_MSC0_INTR_PRIORITY,
    };

    /* Capture the MSC HW block and initialize it to the default state. */
    status = Cy_CapSense_Init(&cy_capsense_context);

    if (CY_CAPSENSE_STATUS_SUCCESS == status)
    {
        /* Initialize CAPSENSE&trade; interrupt for MSC 0 */
        Cy_SysInt_Init(&capsense_msc0_interrupt_config, capsense_msc0_isr);
        NVIC_ClearPendingIRQ(capsense_msc0_interrupt_config.intrSrc);
        NVIC_EnableIRQ(capsense_msc0_interrupt_config.intrSrc);

        status = Cy_CapSense_Enable(&cy_capsense_context);
    }

    if(status != CY_CAPSENSE_STATUS_SUCCESS)
    {
        /* This status could fail before tuning the sensors correctly.
         * Ensure that this function passes after the CAPSENSE&trade; sensors are tuned
         * as per procedure give in the Readme.md file */
    }
}

/*******************************************************************************
 * Function Name: capsense_msc0_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from CAPSENSE&trade; MSC0 block.
 *
 *******************************************************************************/
static void capsense_msc0_isr(void)
{
    Cy_CapSense_InterruptHandler(CY_MSCLP0_HW, &cy_capsense_context);
}

/*******************************************************************************
 * Function Name: initialize_capsense_tuner
 ********************************************************************************
 * Summary:
 *  EZI2C module to communicate with the CAPSENSE&trade; Tuner tool.
 *
 *******************************************************************************/
static void initialize_capsense_tuner(void)
{
    cy_en_scb_ezi2c_status_t status = CY_SCB_EZI2C_SUCCESS;

    /* EZI2C interrupt configuration structure */
    const cy_stc_sysint_t ezi2c_intr_config =
    {
            .intrSrc = CYBSP_EZI2C_IRQ,
            .intrPriority = EZI2C_INTR_PRIORITY,
    };

    /* Initialize the EzI2C firmware module */
    status = Cy_SCB_EZI2C_Init(CYBSP_EZI2C_HW, &CYBSP_EZI2C_config, &ezi2c_context);

    if(status != CY_SCB_EZI2C_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    Cy_SysInt_Init(&ezi2c_intr_config, ezi2c_isr);
    NVIC_EnableIRQ(ezi2c_intr_config.intrSrc);

    /* Set the CAPSENSE&trade; data structure as the I2C buffer to be exposed to the
     * master on primary slave address interface. Any I2C host tools such as
     * the Tuner or the Bridge Control Panel can read this buffer but you can
     * connect only one tool at a time.
     */
    #if ENABLE_TUNER
    Cy_SCB_EZI2C_SetBuffer1(CYBSP_EZI2C_HW, (uint8_t *)&cy_capsense_tuner,
            sizeof(cy_capsense_tuner), sizeof(cy_capsense_tuner),
            &ezi2c_context);
    #endif
    Cy_SCB_EZI2C_Enable(CYBSP_EZI2C_HW);

}

/*******************************************************************************
 * Function Name: ezi2c_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from EZI2C block.
 *
 *******************************************************************************/
static void ezi2c_isr(void)
{
    Cy_SCB_EZI2C_Interrupt(CYBSP_EZI2C_HW, &ezi2c_context);
}

#if ENABLE_RUN_TIME_MEASUREMENT
/*******************************************************************************
 * Function Name: init_sys_tick
 ********************************************************************************
 * Summary:
 *  initializes the system tick with highest possible value to start counting down.
 *
 *******************************************************************************/
static void init_sys_tick()
{
    Cy_SysTick_Init (CY_SYSTICK_CLOCK_SOURCE_CLK_CPU ,0x00FFFFFF);
}
#endif

#if ENABLE_RUN_TIME_MEASUREMENT
/*******************************************************************************
 * Function Name: start_runtime_measurement
 ********************************************************************************
 * Summary:
 *  Initializes the system tick counter by calling Cy_SysTick_Clear() API.
 *******************************************************************************/
static void start_runtime_measurement()
{
    Cy_SysTick_Clear();
}
#endif

#if ENABLE_RUN_TIME_MEASUREMENT
/*******************************************************************************
 * Function Name: stop_runtime_measurement
 ********************************************************************************
 * Summary:
 *  Reads the system tick and converts to time in microseconds(us).
 *
 *  Returns:
 *  runtime - in microseconds(us)
 *******************************************************************************/

static uint32_t stop_runtime_measurement()
{
    uint32_t ticks;
    uint32_t runtime;
    ticks=Cy_SysTick_GetValue();
    ticks= SYS_TICK_INTERVAL - Cy_SysTick_GetValue();
    runtime=ticks*TIME_PER_TICK_IN_US;
    return runtime;
}
#endif

#if ENABLE_PWM_LED
/*******************************************************************************
 * Function Name: ALS_reading
 ********************************************************************************
 * Summary:
 *  Run adc single shot function and captured ambient light sensor (ALS) reading
 *
 *******************************************************************************/
uint16_t ALS_reading()
{
    uint16_t adcResult = 0;
    Cy_SAR_StartConvert(SAR0, CY_SAR_START_CONVERT_SINGLE_SHOT);
        if(Cy_SAR_IsEndConversion(SAR0, CY_SAR_WAIT_FOR_RESULT) == CY_SAR_SUCCESS)
        {
           adcResult = Cy_SAR_GetResult16(SAR0, 0);
        }
    return adcResult;
}
#endif

#if ENABLE_PWM_LED
/*******************************************************************************
 * Function Name: led_control
 ********************************************************************************
 * Summary:
 *  Control LED2, LED3, LED5 and LED6 in the kit to show the CSD button, CSX button,
 *  Touchpad , Proximity and Ambient Light Sensor  status:
 *  -----------------------------------------------------------------------------------------------------------------------
 *  |    Widget     |         LED 2          |      LED 3             |         LED 5          |      LED 6              |
 *  | CSD Button    | OFF                    | ON                     | OFF                    | OFF                     |
 *  | CSX Button    | ON                     | OFF                    | OFF                    | OFF                     |
 *  | Touchpad      | ON with brightness     | ON with brightness     | OFF                    | OFF                     |
 *  |               | corresponding to touch | corresponding to touch |                        |                         | 
 *  |               | position on touchpad   | position on touchpad   |                        |                         |
 *  | Proximity     | OFF                    | OFF                    | ON with brightness     | OFF                     |
 *  |               |                        |                        | corresponding to       |                         |
 *  |               |                        |                        | proximity distance     |                         |
    | ALS           | OFF                    | OFF                    | OFF                    | ON with brightness      |
    |               |                        |                        |                        | corresponding to ambient|
    |               |                        |                        |                        | light present           |
 *  -----------------------------------------------------------------------------------------------------------------------
 *
 *******************************************************************************/
void led_control()
{
    cy_stc_capsense_touch_t *touchpad_touch_info;
    uint16_t touchpad_pos_x;
    uint16_t touchpad_pos_y;

    if(Cy_CapSense_IsAnyWidgetActive(&cy_capsense_context))
    {

        if(Cy_CapSense_IsWidgetActive(CY_CAPSENSE_TOUCHPAD0_WDGT_ID, &cy_capsense_context) )
        {
            /* Get Touchpad status */
            touchpad_touch_info = Cy_CapSense_GetTouchInfo(CY_CAPSENSE_TOUCHPAD0_WDGT_ID, &cy_capsense_context);
            touchpad_pos_x = (MAXIMUM_BRIGHTNESS_LED-touchpad_touch_info->ptrPosition->x);
            touchpad_pos_y = touchpad_touch_info->ptrPosition->y;

            /* LED2 & LED3 Turns ON and brightness changes when there is a touch detected on the Touchpad */
            Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM0_HW, CYBSP_PWM0_NUM, touchpad_pos_x);
            Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM1_HW, CYBSP_PWM1_NUM, touchpad_pos_y);
        }
        else
        {
            if(Cy_CapSense_IsWidgetActive(CY_CAPSENSE_BUTTON0_WDGT_ID, &cy_capsense_context) )
            {
                /* LED3  Turns ON when there is a touch detected on CSD button */
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM0_HW, CYBSP_PWM0_NUM, CYBSP_PWM0_config.period0);
            }
            else
            {
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM0_HW, CYBSP_PWM0_NUM, CYBSP_LED_OFF);

            }

            if(Cy_CapSense_IsWidgetActive(CY_CAPSENSE_BUTTON1_WDGT_ID, &cy_capsense_context) )
            {
                /* LED2  Turns ON when there is a touch detected on CSX button */
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM1_HW, CYBSP_PWM1_NUM, CYBSP_PWM1_config.period0);

            }
            else
            {
                Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM1_HW, CYBSP_PWM1_NUM, CYBSP_LED_OFF);
            }
        }

        if(Cy_CapSense_IsWidgetActive(CY_CAPSENSE_PROXIMITY0_WDGT_ID, &cy_capsense_context) )
        {
            proxMaxRawCount = cy_capsense_tuner.widgetContext[CY_CAPSENSE_PROXIMITY0_WDGT_ID].maxRawCount;
            maxDiffCount = proxMaxRawCount - cy_capsense_tuner.sensorContext[PROXIMITY_SENSOR_SLOT_INDEX].bsln;
            proxLedBrightness = ((uint32_t)(cy_capsense_tuner.sensorContext[PROXIMITY_SENSOR_SLOT_INDEX].diff*CYBSP_PWM1_config.period0)/maxDiffCount);

            /* LED5 Turns ON and brightness changes as per hand distance from proximity sensor */
            Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM2_HW, CYBSP_PWM2_NUM, proxLedBrightness);
        }
        else{
            Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM2_HW, CYBSP_PWM2_NUM, CYBSP_LED_OFF);
        }
    }

    else
        {
             /* Turn OFF LED1 */
             Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM0_HW, CYBSP_PWM0_NUM, CYBSP_LED_OFF);
             /* Turn OFF LED2  */
             Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM1_HW, CYBSP_PWM1_NUM, CYBSP_LED_OFF);
             /* Turn OFF LED5  */
            Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM2_HW, CYBSP_PWM2_NUM, CYBSP_LED_OFF);
        }

       /* LED6 Turns ON and brightness changes as per ambient light present*/
        Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM3_HW, CYBSP_PWM3_NUM, ALS_reading()); 

}
#endif

/*******************************************************************************
 * Function Name: register_callback
 ********************************************************************************
 *
 * Summary:
 *  Register Deep Sleep callbacks for EzI2C, SPI components
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void register_callback(void)
{
    /* Register EzI2C Deep Sleep callback */
    Cy_SysPm_RegisterCallback(&ezi2cCallback);

    /* Register Deep Sleep callback */
    Cy_SysPm_RegisterCallback(&deepSleepCb);
}

/*******************************************************************************
 * Function Name: deep_sleep_callback
 ********************************************************************************
 *
 * Summary:
 * Deep Sleep callback implementation. Waits for the completion of SPI transaction.
 * And change the SPI GPIOs to highZ while transition to deep-sleep and vice-versa
 *
 * Parameters:
 *  callbackParams: The pointer to the callback parameters structure cy_stc_syspm_callback_params_t.
 *  mode: Callback mode, see cy_en_syspm_callback_mode_t
 *
 * Return:
 *  Entered status, see cy_en_syspm_status_t.
 *
 *******************************************************************************/
cy_en_syspm_status_t deep_sleep_callback(
        cy_stc_syspm_callback_params_t *callbackParams, cy_en_syspm_callback_mode_t mode)
{
    cy_en_syspm_status_t ret_val = CY_SYSPM_FAIL;

    switch (mode)
    {
        case CY_SYSPM_CHECK_READY:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_CHECK_FAIL:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_BEFORE_TRANSITION:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_AFTER_TRANSITION:

            ret_val = CY_SYSPM_SUCCESS;
            break;

        default:
            /* Don't do anything in the other modes */
            ret_val = CY_SYSPM_SUCCESS;
            break;
    }
    return ret_val;
}

#if ENABLE_PWM_LED
void PWM_initialisation(void)
{

    /* Initialize PWM block */
    (void)Cy_TCPWM_PWM_Init(CYBSP_PWM0_HW, CYBSP_PWM0_NUM, &CYBSP_PWM0_config);
    (void)Cy_TCPWM_PWM_Init(CYBSP_PWM1_HW, CYBSP_PWM1_NUM, &CYBSP_PWM1_config);
    (void)Cy_TCPWM_PWM_Init(CYBSP_PWM2_HW, CYBSP_PWM2_NUM, &CYBSP_PWM2_config);
    (void)Cy_TCPWM_PWM_Init(CYBSP_PWM3_HW, CYBSP_PWM3_NUM, &CYBSP_PWM3_config);

    /* Enable the initialized PWM */
    Cy_TCPWM_Enable_Multiple(CYBSP_PWM0_HW, CYBSP_PWM0_MASK);
    Cy_TCPWM_Enable_Multiple(CYBSP_PWM1_HW, CYBSP_PWM1_MASK);
    Cy_TCPWM_Enable_Multiple(CYBSP_PWM2_HW, CYBSP_PWM2_MASK);
    Cy_TCPWM_Enable_Multiple(CYBSP_PWM3_HW, CYBSP_PWM3_MASK);

    /* Then start the PWM */
    Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM0_HW, CYBSP_PWM0_MASK);
    Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM1_HW, CYBSP_PWM1_MASK);
    Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM2_HW, CYBSP_PWM2_MASK);
    Cy_TCPWM_TriggerReloadOrIndex(CYBSP_PWM3_HW, CYBSP_PWM3_MASK);
}
#endif

/* [] END OF FILE */
