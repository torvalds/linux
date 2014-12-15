/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/
/**
 * @file sii_osal_linux_timer.c
 *
 * @brief This file provides the Linux implementation of the timer support
 *           defined by the Silicon Image Operating System Abstraction Layer (OSAL)
 *           specification.
 *
 * $Author: Dave Canfield
 * $Rev: $
 * $Date: March. 16, 2011
 *
 *****************************************************************************/

#define SII_OSAL_LINUX_TIMER_C

/***** #include statements ***************************************************/
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include "sii_hal.h"
//#include "sii_hal_priv.h"
#include "osal/include/osal.h"

/***** local macro definitions ***********************************************/

/** Convert a value specified in milliseconds to nanoseconds */
#define MSEC_TO_NSEC(x)        (x * 1000000UL)


#define MAX_TIMER_NAME_LEN        64


/***** local type definitions ************************************************/

/* Define structure used to maintain list of outstanding timer objects. */
typedef struct _SiiOsTimerInfo_t {
    struct    list_head         listEntry;
    struct    work_struct       workItem;
    uint8_t                     flags;
    char                        timerName[MAX_TIMER_NAME_LEN];
    struct hrtimer              hrTimer;
    timerCallbackHandler_t      callbackHandler;
    void                        *callbackParam;
    uint32_t                    timeMsec;
    bool                        bPeriodic;
} timerObject_t;

// timerObject_t flag field definitions
#define TIMER_OBJ_FLAG_WORK_IP    0x01    // timer's work item callback is in process
#define TIMER_OBJ_FLAG_DEL_REQ    0x02    // a request is pending to delete this timer


/***** local variable declarations *******************************************/

static struct list_head            timerList;
static struct workqueue_struct        *timerWorkQueue;



/***** local function prototypes *********************************************/


/***** global variable declarations *******************************************/


/***** local functions *******************************************************/


/*****************************************************************************/
/*
 *  @brief This function is the unit of work that has been placed on the
 *         work queue when a timer expires.
 *
 *  @param[in]    work    Pointer to the workItem field of the timerObject_t
 *                      that is responsible for this function being called.
 *
 *  @return     Nothing
 *
 *****************************************************************************/
static void WorkHandler(struct work_struct *work)
{
    timerObject_t        *pTimerObj = container_of(work, timerObject_t, workItem);


    pTimerObj->flags |= TIMER_OBJ_FLAG_WORK_IP;

    if (HalAcquireIsrLock() == HAL_RET_SUCCESS) {
        if(pTimerObj->callbackHandler)
            (pTimerObj->callbackHandler)(pTimerObj->callbackParam);
        HalReleaseIsrLock();
    }

    pTimerObj->flags &= ~TIMER_OBJ_FLAG_WORK_IP;

    if(pTimerObj->flags & TIMER_OBJ_FLAG_DEL_REQ)
    {
        // Deletion of this timer was requested during the execution of
        // the callback handler so go ahead and delete it now.
        kfree(pTimerObj);
    }
}



/*****************************************************************************/
/*
 *  @brief Timer callback handler.
 *
 *  @param[in]    timer    Pointer to the timer structure responsible for this
 *                      function being called.
 *
 *  @return     Returns HRTIMER_RESTART if the timer is periodic or
 *              HRTIMER_NORESTART if the timer is not periodic.
 *
 *****************************************************************************/
static enum hrtimer_restart TimerHandler(struct hrtimer *timer)
{
    SiiOsTimer_t pTimerObj = container_of(timer, timerObject_t, hrTimer);

    queue_work(timerWorkQueue, &pTimerObj->workItem);

    return HRTIMER_NORESTART;
}



/***** public functions ******************************************************/



/*****************************************************************************/
/**
 * @brief Initialize OSAL timer support.
 *
 *****************************************************************************/
SiiOsStatus_t SiiOsInit(uint32_t maxChannels)
{
    // Initialize list head used to track allocated timer objects.
    INIT_LIST_HEAD(&timerList);

    timerWorkQueue = create_workqueue("Sii_timer_work");

    if(timerWorkQueue == NULL)
    {
        return SII_OS_STATUS_ERR_NOT_AVAIL;
    }

    return SII_OS_STATUS_SUCCESS;
}



/*****************************************************************************/
/**
 * @brief Terminate OSAL timer support.
 *
 *****************************************************************************/
SiiOsStatus_t SiiOsTerm(void)
{
    SiiOsTimer_t timerObj;
    int         status;

    // Make sure all outstanding timer objects are canceled and the
    // memory allocated for them is freed.
    while(!list_empty(&timerList)) {

        timerObj = list_first_entry(&timerList, timerObject_t, listEntry);
        status = hrtimer_try_to_cancel(&timerObj->hrTimer);
        if(status >= 0)
        {
            list_del(&timerObj->listEntry);
            kfree(timerObj);
        }
    }

    flush_workqueue(timerWorkQueue);
    destroy_workqueue(timerWorkQueue);
    timerWorkQueue = NULL;

    return SII_OS_STATUS_SUCCESS;
}


bool_t SiiOsTimerValidCheck(SiiOsTimer_t timerId)
{
    SiiOsTimer_t timer;

    list_for_each_entry(timer, &timerList, listEntry)
    {
        if(timer == timerId)
        {
            break;
        }
    }
    
    if(timer != timerId)
    {
        SII_DEBUG_PRINT(MSG_ERR,"Invalid timerId %p received\n",
                        timerId);
        return false;
    }
    return true;
}


/*****************************************************************************/
/**
 * @brief Allocate a new OSAL timer object.
 *
 *****************************************************************************/
SiiOsStatus_t SiiOsTimerCreate(const char *pName, void(*pTimerFunction)(void *pArg),
                                 void *pTimerArg,
                                 SiiOsTimer_t *pTimerId)
{
    SiiOsTimer_t timerObj;
    SiiOsStatus_t        status = SII_OS_STATUS_SUCCESS;


    if(pTimerFunction == NULL)
    {
        return SII_OS_STATUS_ERR_INVALID_PARAM;
    }

    timerObj = kmalloc(sizeof(timerObject_t), GFP_KERNEL);
    if(timerObj == NULL)
    {
        return SII_OS_STATUS_ERR_NOT_AVAIL;
    }

    strncpy(timerObj->timerName, pName, MAX_TIMER_NAME_LEN-1);
    timerObj->timerName[MAX_TIMER_NAME_LEN-1] = 0;

    timerObj->callbackHandler = pTimerFunction;
    timerObj->callbackParam = pTimerArg;
    timerObj->flags = 0;

    INIT_WORK(&timerObj->workItem, WorkHandler);

    list_add(&timerObj->listEntry, &timerList);

    hrtimer_init(&timerObj->hrTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    timerObj->hrTimer.function = TimerHandler;

    *pTimerId = timerObj;
    return status;
}



/*****************************************************************************/
/**
 * @brief Delete a previously allocated OSAL timer object.
 *
 *****************************************************************************/
SiiOsStatus_t SiiOsTimerDelete(SiiOsTimer_t *pTimerId)
{
    SiiOsTimer_t timer;

    if (SiiOsTimerValidCheck(*pTimerId))
    {
        timer = *pTimerId;
        list_del(&timer->listEntry);

        hrtimer_cancel(&timer->hrTimer);

        if(timer->flags & TIMER_OBJ_FLAG_WORK_IP)
        {
            // Request to delete timer object came from within the timer's
            // callback handler.  If we were to proceed with the timer deletion
            // we would deadlock at cancel_work_sync().  So instead just flag
            // that the user wants the timer deleted.  Later when the timer
            // callback completes the timer's work handler will complete the
            // process of deleting this timer.
            timer->flags |= TIMER_OBJ_FLAG_DEL_REQ;
        }
        else
        {
            cancel_work_sync(&timer->workItem);
            kfree(timer);
            *pTimerId = NULL;
        }
    }
    else
    {
        return SII_OS_STATUS_ERR_INVALID_PARAM;
    }

    return SII_OS_STATUS_SUCCESS;
}


SiiOsStatus_t  SiiOsTimerStart(SiiOsTimer_t timerId, uint32_t time_msec)
{
    SiiOsTimer_t  timer;
    ktime_t timer_period;

    if (SiiOsTimerValidCheck(timerId))
    {
        long secs=0;
        timer = timerId;

        secs=time_msec/1000;
        time_msec %= 1000;
        timer_period = ktime_set(secs, MSEC_TO_NSEC(time_msec));
        hrtimer_start(&timer->hrTimer, timer_period, HRTIMER_MODE_REL);
    }
    else
    {
        return SII_OS_STATUS_ERR_INVALID_PARAM;
    }

    return SII_OS_STATUS_SUCCESS;
}

SiiOsStatus_t  SiiOsTimerStop(SiiOsTimer_t timerId)
{
    SiiOsTimer_t  timer;

    if (SiiOsTimerValidCheck(timerId))
    {
        timer = timerId;
        hrtimer_cancel(&timer->hrTimer);
    }
    else
    {
        return SII_OS_STATUS_ERR_INVALID_PARAM;
    }

    return SII_OS_STATUS_SUCCESS;
}

