/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/mgmt/cnm_timer.c#1 $
*/

/*! \file   "cnm_timer.c"
    \brief

*/

/*******************************************************************************
* Copyright (c) 2009 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: cnm_timer.c $
 *
 * 12 13 2011 cm.chang
 * [WCXRP00001136] [All Wi-Fi][Driver] Add wake lock for pending timer
 * Add wake lock if timer timeout value is smaller than 5 seconds
 *
 * 02 24 2011 cp.wu
 * [WCXRP00000490] [MT6620 Wi-Fi][Driver][Win32] modify kalMsleep() implementation because NdisMSleep() won't sleep long enough for specified interval such as 500ms
 * modify cnm_timer and hem_mbox APIs to be thread safe to ease invoking restrictions
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * cnm_timer has been migrated.
 *
 * 05 28 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support sleep notification to host
 *
 * 05 19 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add some checking assertions
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Return timer token back to COS when entering wait off state
 *
 * 01 11 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Remove compiling warning
 *
 * 01 08 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support longer timeout interval to 45 days from 65secu1rwduu`wvpghlqg|fh+fmdkb
 *
 * 01 06 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Fix system time is 32KHz instead of 1ms
 *
 * 01 04 2010 tehuang.liu
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * For working out the first connection Chariot-verified version
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Place rRootTimer.rNextExpiredSysTime = rExpiredSysTime; before set timer
 *
 * Oct 30 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * In cnmTimerInitialize(), just stop timer if it was already created.
 *
 * Oct 30 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Move the external reference for Lint to precomp.h
 *
 * Oct 30 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix lint warning
 *
 * Oct 28 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the time to do the time out check.
*
* \param[in] rTimeout Time out interval from current time.
*
* \retval TRUE Success.
*
*/
/*----------------------------------------------------------------------------*/
static BOOLEAN
cnmTimerSetTimer (
    IN P_ADAPTER_T prAdapter,
    IN OS_SYSTIME  rTimeout
    )
{
    P_ROOT_TIMER        prRootTimer;
    BOOLEAN             fgNeedWakeLock;

    ASSERT(prAdapter);

    prRootTimer = &prAdapter->rRootTimer;

    kalSetTimer(prAdapter->prGlueInfo, rTimeout);

    if (rTimeout <= SEC_TO_SYSTIME(WAKE_LOCK_MAX_TIME)) {
        fgNeedWakeLock = TRUE;

        if (!prRootTimer->fgWakeLocked) {
            KAL_WAKE_LOCK(prAdapter, &prRootTimer->rWakeLock);
            prRootTimer->fgWakeLocked = TRUE;
        }
    }
    else {
        fgNeedWakeLock = FALSE;
    }

    return fgNeedWakeLock;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to initialize a root timer.
*
* \param[in] prAdapter
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmTimerInitialize (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROOT_TIMER    prRootTimer;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    prRootTimer = &prAdapter->rRootTimer;

    /* Note: glue layer have configured timer */

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
    LINK_INITIALIZE(&prRootTimer->rLinkHead);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);

    KAL_WAKE_LOCK_INIT(prAdapter, &prRootTimer->rWakeLock, "WLAN Timer");
    prRootTimer->fgWakeLocked = FALSE;
    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to destroy a root timer.
*        When WIFI is off, the token shall be returned back to system.
*
* \param[in]
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmTimerDestroy (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROOT_TIMER    prRootTimer;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    prRootTimer = &prAdapter->rRootTimer;

    if (prRootTimer->fgWakeLocked) {
        KAL_WAKE_UNLOCK(prAdapter, &prRootTimer->rWakeLock);
        prRootTimer->fgWakeLocked = FALSE;
    }
    KAL_WAKE_LOCK_DESTROY(prAdapter, &prRootTimer->rWakeLock);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
    LINK_INITIALIZE(&prRootTimer->rLinkHead);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);

    /* Note: glue layer will be responsible for timer destruction */

    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to initialize a timer.
*
* \param[in] prTimer Pointer to a timer structure.
* \param[in] pfnFunc Pointer to the call back function.
* \param[in] u4Data Parameter for call back function.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmTimerInitTimer (
    IN P_ADAPTER_T              prAdapter,
    IN P_TIMER_T                prTimer,
    IN PFN_MGMT_TIMEOUT_FUNC    pfFunc,
    IN UINT_32                  u4Data
    )
{
    ASSERT(prAdapter);

    ASSERT(prTimer);

#if DBG
    /* Note: NULL function pointer is permitted for HEM POWER */
    if (pfFunc == NULL) {
        DBGLOG(CNM, WARN, ("Init timer with NULL callback function!\n"));
    }
#endif

#if DBG
    ASSERT(prAdapter->rRootTimer.rLinkHead.prNext);
    {
        P_LINK_T            prTimerList;
        P_LINK_ENTRY_T      prLinkEntry;
        P_TIMER_T           prPendingTimer;

        prTimerList = &(prAdapter->rRootTimer.rLinkHead);

        LINK_FOR_EACH(prLinkEntry, prTimerList) {
            prPendingTimer = LINK_ENTRY(prLinkEntry, TIMER_T, rLinkEntry);
            ASSERT(prPendingTimer);
            ASSERT(prPendingTimer != prTimer);
        }
    }
#endif

    LINK_ENTRY_INITIALIZE(&prTimer->rLinkEntry);

    prTimer->pfMgmtTimeOutFunc  = pfFunc;
    prTimer->u4Data             = u4Data;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to stop a timer.
*
* \param[in] prTimer Pointer to a timer structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
static VOID
cnmTimerStopTimer_impl (
    IN P_ADAPTER_T              prAdapter,
    IN P_TIMER_T                prTimer,
    IN BOOLEAN                  fgAcquireSpinlock
    )
{
    P_ROOT_TIMER    prRootTimer;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(prTimer);

    prRootTimer = &prAdapter->rRootTimer;

    if (fgAcquireSpinlock) {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
    }

    if (timerPendingTimer(prTimer)) {
        LINK_REMOVE_KNOWN_ENTRY(&prRootTimer->rLinkHead,
                    &prTimer->rLinkEntry);

        /* Reduce dummy timeout for power saving, especially HIF activity.
         * If two or more timers exist and being removed timer is smallest,
         * this dummy timeout will still happen, but it is OK.
         */
        if (LINK_IS_EMPTY(&prRootTimer->rLinkHead)) {
            kalCancelTimer(prAdapter->prGlueInfo);

            if (fgAcquireSpinlock && prRootTimer->fgWakeLocked) {
                KAL_WAKE_UNLOCK(prAdapter, &prRootTimer->rWakeLock);
                prRootTimer->fgWakeLocked = FALSE;
            }
        }
    }

    if (fgAcquireSpinlock) {
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to stop a timer.
*
* \param[in] prTimer Pointer to a timer structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmTimerStopTimer (
    IN P_ADAPTER_T              prAdapter,
    IN P_TIMER_T                prTimer
    )
{
    ASSERT(prAdapter);
    ASSERT(prTimer);

    cnmTimerStopTimer_impl(prAdapter, prTimer, TRUE);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to start a timer with wake_lock.
*
* \param[in] prTimer Pointer to a timer structure.
* \param[in] u4TimeoutMs Timeout to issue the timer and call back function
*                        (unit: ms).
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmTimerStartTimer (
    IN P_ADAPTER_T      prAdapter,
    IN P_TIMER_T        prTimer,
    IN UINT_32          u4TimeoutMs
    )
{
    P_ROOT_TIMER    prRootTimer;
    P_LINK_T        prTimerList;
    OS_SYSTIME      rExpiredSysTime, rTimeoutSystime;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(prTimer);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);

    prRootTimer = &prAdapter->rRootTimer;
    prTimerList= &prRootTimer->rLinkHead;

    /* If timeout interval is larger than 1 minute, the mod value is set
     * to the timeout value first, then per minutue.
     */
    if (u4TimeoutMs > MSEC_PER_MIN) {
        ASSERT(u4TimeoutMs <= ((UINT_32)0xFFFF * MSEC_PER_MIN));

        prTimer->u2Minutes = (UINT_16)(u4TimeoutMs / MSEC_PER_MIN);
        u4TimeoutMs -= (prTimer->u2Minutes * MSEC_PER_MIN);
        if (u4TimeoutMs == 0) {
            u4TimeoutMs = MSEC_PER_MIN;
            prTimer->u2Minutes--;
        }
    }
    else {
        prTimer->u2Minutes = 0;
    }

    /* The assertion check if MSEC_TO_SYSTIME() may be overflow. */
    ASSERT(u4TimeoutMs < (((UINT_32)0x80000000 - MSEC_PER_SEC) / KAL_HZ));
    rTimeoutSystime = MSEC_TO_SYSTIME(u4TimeoutMs);
    rExpiredSysTime = kalGetTimeTick() + rTimeoutSystime;

    /* If no timer pending or the fast time interval is used. */
    if (LINK_IS_EMPTY(prTimerList) ||
        TIME_BEFORE(rExpiredSysTime, prRootTimer->rNextExpiredSysTime)) {

        prRootTimer->rNextExpiredSysTime = rExpiredSysTime;
        cnmTimerSetTimer(prAdapter, rTimeoutSystime);
    }

    /* Add this timer to checking list */
    prTimer->rExpiredSysTime = rExpiredSysTime;

    if (!timerPendingTimer(prTimer)) {
        LINK_INSERT_TAIL(prTimerList, &prTimer->rLinkEntry);
    }

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);

    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routines is called to check the timer list.
*
* \param[in]
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmTimerDoTimeOutCheck (
    IN P_ADAPTER_T      prAdapter
    )
{
    P_ROOT_TIMER        prRootTimer;
    P_LINK_T            prTimerList;
    P_LINK_ENTRY_T      prLinkEntry;
    P_TIMER_T           prTimer;
    OS_SYSTIME          rCurSysTime;
    PFN_MGMT_TIMEOUT_FUNC   pfMgmtTimeOutFunc;
    UINT_32             u4TimeoutData;
    BOOLEAN             fgNeedWakeLock;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    /* acquire spin lock */
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);

    prRootTimer = &prAdapter->rRootTimer;
    prTimerList= &prRootTimer->rLinkHead;

    rCurSysTime = kalGetTimeTick();

    /* Set the permitted max timeout value for new one */
    prRootTimer->rNextExpiredSysTime = rCurSysTime + MGMT_MAX_TIMEOUT_INTERVAL;

    LINK_FOR_EACH(prLinkEntry, prTimerList) {
        prTimer = LINK_ENTRY(prLinkEntry, TIMER_T, rLinkEntry);
        ASSERT(prTimer);

        /* Check if this entry is timeout. */
        if (!TIME_BEFORE(rCurSysTime, prTimer->rExpiredSysTime)) {
            cnmTimerStopTimer_impl(prAdapter, prTimer, FALSE);

            pfMgmtTimeOutFunc = prTimer->pfMgmtTimeOutFunc;
            u4TimeoutData = prTimer->u4Data;

            if (prTimer->u2Minutes > 0) {
                prTimer->u2Minutes--;
                prTimer->rExpiredSysTime =
                    rCurSysTime + MSEC_TO_SYSTIME(MSEC_PER_MIN);
                LINK_INSERT_TAIL(prTimerList, &prTimer->rLinkEntry);
            }
            else if (pfMgmtTimeOutFunc) {
                KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
                (pfMgmtTimeOutFunc)(prAdapter, u4TimeoutData);
                KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
            }

            /* Search entire list again because of nest del and add timers
             * and current MGMT_TIMER could be volatile after stopped
             */
            prLinkEntry = (P_LINK_ENTRY_T)prTimerList;

            prRootTimer->rNextExpiredSysTime =
                    rCurSysTime + MGMT_MAX_TIMEOUT_INTERVAL;
        }
        else if (TIME_BEFORE(prTimer->rExpiredSysTime,
                             prRootTimer->rNextExpiredSysTime)) {
            prRootTimer->rNextExpiredSysTime = prTimer->rExpiredSysTime;
        }
    } /* end of for loop */

    /* Setup the prNext timeout event. It is possible the timer was already
     * set in the above timeout callback function.
     */
    fgNeedWakeLock = FALSE;
    if (!LINK_IS_EMPTY(prTimerList)) {
        ASSERT(TIME_AFTER(prRootTimer->rNextExpiredSysTime, rCurSysTime));

        fgNeedWakeLock = cnmTimerSetTimer(prAdapter, (OS_SYSTIME)
            ((INT_32)prRootTimer->rNextExpiredSysTime - (INT_32)rCurSysTime));
    }

    if (prRootTimer->fgWakeLocked && !fgNeedWakeLock) {
        KAL_WAKE_UNLOCK(prAdapter, &prRootTimer->rWakeLock);
        prRootTimer->fgWakeLocked = FALSE;
    }

    /* release spin lock */
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TIMER);
}


