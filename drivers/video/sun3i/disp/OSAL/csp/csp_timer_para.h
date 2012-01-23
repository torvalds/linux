/*
*******************************************************************************
*           				eBase
*                 the Abstract of Hardware
*
*
*              (c) Copyright 2006-2010, ALL WINNER TECH.
*           								All Rights Reserved
*
* File     :  d:\winners\eBase\eBSP\CSP\sun_20\SW_TIMER\CSP_TMRC.h
* Date     :  2010/11/23 16:36
* By       :  Sam.Wu
* Version  :  V1.00
* Description :  CSP timer controller
* Update   :  date      author      version     notes
*
* Notes: After start the timer, the timer will count down from Interval you set.
         If it counts to Zero it will send IRQ if its IRQ enabled and reset the chip
         if it is the watch-dog and reset control is valid.
*******************************************************************************
*/
#ifndef _CSP_TIMER_PARA_H_
#define _CSP_TIMER_PARA_H_

#define TMRC_TRUE       1
#define TMRC_FALSE      0
#define TMRC_ENABLE     1
#define TMRC_DISABLE    0
#define TMRC_FREE       0
#define TMRC_USED       1

/************************************************************************/
/* There are 2 sources for a timer  in this timer Controller: LOSC and HOSC.
 * the HOSC is the 24MHz oscillate in the chip, and the LOSC has 3 kinds of
 * sources--internal 32K low speed oscillate, but it's not exact sometimes;
 * the external 32768Hz low speed oscillate; and the HOSC(High spped Oscillate).
*/
/************************************************************************/

/*********************************************************************
* TypeName	 :    		CSP_TMRC_LoscSrc_t
* Description: sources for the LOSC
* Members    :

* Note       : If the source is CSP_TMRC_LOSC_SRC_EX_32768, the rate of
            clock source LOSC is 32768Hz, otherwise,(internal 32K or HOSC)
            the rate of LOSC is 32KHz.
*********************************************************************/
typedef enum _CSP_TMRC_LOSC_SRC{
    CSP_TMRC_LOSC_SRC_INTER_32K = 0,
    CSP_TMRC_LOSC_SRC_EX_32768,
    CSP_TMRC_LOSC_SRC_HOSC
}CSP_TMRC_LoscSrc_t;

typedef enum _CSP_TMRC_TMR_SRC{
    CSP_TMRC_TMR_SRC_LOSC,
    CSP_TMRC_TMR_SRC_HOSC
}CSP_TMRC_TmrSrc_t;


/************************************************************************/
/* RTC                      */
/************************************************************************/
typedef enum _RTC_WEEK_NO{
    CSP_TMRC_RTC_MONDAY   = 0,
    CSP_TMRC_RTC_TUSDAY,
    CSP_TMRC_RTC_WEDNESDAY,
    CSP_TMRC_RTC_THURSDAY,
    CSP_TMRC_RTC_FRIDAY,
    CSP_TMRC_RTC_SATURDAY,
    CSP_TMRC_RTC_SUNDAY
}CSP_TMRC_RTC_WeekNo_t;

/************************************************************************/
/* Alarm                       */
/************************************************************************/

/*********************************************************************
* TypeName	 :    		CSP_TMRC_AlarmMode_t
* Description:
* Members    :
    @timerMode: set TMRC_TRUE if you choose timer mode. TMRC_FALSE if normal mode.
    @.mode.timer: meaningful only when timerMode is TMRC_TRUE. If meaningful, this field
      means the interval to the next alarm time you want to set or get.
    @.mode.normal: meaningful only when timerMode is TMRC_FALSE. If meaningful, this field
      means the alarm time referenced to RTC.
* Note       :The alarm can work in one of the 2 work mode---timer mode and normal mode.
            1) If work in timer mode, the alarm is used the same as a timer, it will
              count from 0 to alarm_value + 1, the unit is in second and alarm_value =
              (day*24*60*60 + hour*60*60 + minute*60 + second).
            2) If work in normal mode, the alarm is the everyday alarm clock. If the time
              you preset is equal the RTC, the alarm will make irq if irq enabled.
*********************************************************************/
typedef struct _CSP_TMRC_AlarmMode{
    __bool timerMode;//TMRC_TRUE or TMRC_FALSE

    union{
        struct{
            u8 day;
            u8 hour;
            u8 minute;
            u8 second;
        }timer;//timer is meaningful only when timerMode is TMRC_TRUE

        struct{
            u8     hour;
            u8     minute;
            u8     second;
            __bool alarmInMonday;
            __bool alarmInTusday;
            __bool alarmInWesday;
            __bool alarmInTursday;
            __bool alarmInFriday;
            __bool alarmInSaturday;
            __bool alarmInSunday;
        }normal;//alarmTime is meaningful only when timerMode is TMRC_FALSE
    }mode;
}CSP_TMRC_AlarmMode_t;




/************************************************************************/
/* Watch-dog                      */
/************************************************************************/

typedef struct _CSP_WD_PARA{
    CSP_TMRC_TmrSrc_t clkSrc;
    __bool            irqEnable;
    __bool            resetValid;
    u32               interVal;
}CSP_TMRC_WatchDogPara_t;


/************************************************************************/
/* timer                      */
/************************************************************************/

typedef enum _CSP_TMRC_TMR_MODE{
    CSP_TMRC_TMR_MODE_CONTINU,
    CSP_TMRC_TMR_MODE_ONE_SHOOT,
}CSP_TMRC_TmrMode_t;

typedef enum _CSP_TMRC_TMR_PRECISION{
    CSP_TMRC_TMR_PRECISION_NANO_SECOND,
    CSP_TMRC_TMR_PRECISION_MICRO_SECOND,
    CSP_TMRC_TMR_PRECISION_MILLI_SECOND,
    CSP_TMRC_TMR_PRECISION_SECOND
}CSP_TMRC_TmrPrecision_t;

typedef struct _CSP_TMRC_tmr_type{
    CSP_TMRC_TmrPrecision_t precision;//This precision cannot be changed after you set successful!
    u32 leastCount;//The timer can count down from >=least count to 0.
}CSP_TMRC_TmrType_t;

#endif //#ifndef _CSP_TIMER_PARA_H_

