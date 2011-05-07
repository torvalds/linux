//------------------------------------------------------------------------------
// <copyright file="credit_dist.c" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#define ATH_MODULE_NAME misc
#include "a_debug.h"
#include "htc_api.h"
#include "common_drv.h"

/********* CREDIT DISTRIBUTION FUNCTIONS ******************************************/

#define NO_VO_SERVICE 1 /* currently WMI only uses 3 data streams, so we leave VO service inactive */
#define CONFIG_GIVE_LOW_PRIORITY_STREAMS_MIN_CREDITS 1

#ifdef NO_VO_SERVICE
#define DATA_SVCS_USED 3
#else
#define DATA_SVCS_USED 4
#endif

static void RedistributeCredits(struct common_credit_state_info *pCredInfo,
                                struct htc_endpoint_credit_dist *pEPDistList);

static void SeekCredits(struct common_credit_state_info *pCredInfo,
                        struct htc_endpoint_credit_dist *pEPDistList);

/* reduce an ep's credits back to a set limit */
static INLINE void ReduceCredits(struct common_credit_state_info *pCredInfo,
                                struct htc_endpoint_credit_dist  *pEpDist,
                                int                       Limit)
{
    int credits;

        /* set the new limit */
    pEpDist->TxCreditsAssigned = Limit;

    if (pEpDist->TxCredits <= Limit) {
        return;
    }

        /* figure out how much to take away */
    credits = pEpDist->TxCredits - Limit;
        /* take them away */
    pEpDist->TxCredits -= credits;
    pCredInfo->CurrentFreeCredits += credits;
}

/* give an endpoint some credits from the free credit pool */
#define GiveCredits(pCredInfo,pEpDist,credits)      \
{                                                   \
    (pEpDist)->TxCredits += (credits);              \
    (pEpDist)->TxCreditsAssigned += (credits);      \
    (pCredInfo)->CurrentFreeCredits -= (credits);   \
}


/* default credit init callback.
 * This function is called in the context of HTCStart() to setup initial (application-specific)
 * credit distributions */
static void ar6000_credit_init(void                     *Context,
                               struct htc_endpoint_credit_dist *pEPList,
                               int                      TotalCredits)
{
    struct htc_endpoint_credit_dist *pCurEpDist;
    int                      count;
    struct common_credit_state_info *pCredInfo = (struct common_credit_state_info *)Context;

    pCredInfo->CurrentFreeCredits = TotalCredits;
    pCredInfo->TotalAvailableCredits = TotalCredits;

    pCurEpDist = pEPList;

        /* run through the list and initialize */
    while (pCurEpDist != NULL) {

            /* set minimums for each endpoint */
        pCurEpDist->TxCreditsMin = pCurEpDist->TxCreditsPerMaxMsg;

#ifdef CONFIG_GIVE_LOW_PRIORITY_STREAMS_MIN_CREDITS
 
      if (TotalCredits > 4)
      {
          if ((pCurEpDist->ServiceID == WMI_DATA_BK_SVC)  || (pCurEpDist->ServiceID == WMI_DATA_BE_SVC)){
                    /* assign at least min credits to lower than VO priority services */
                GiveCredits(pCredInfo,pCurEpDist,pCurEpDist->TxCreditsMin);
                    /* force active */
                SET_EP_ACTIVE(pCurEpDist);
          }
      }
 
#endif

        if (pCurEpDist->ServiceID == WMI_CONTROL_SVC) {
                /* give control service some credits */
            GiveCredits(pCredInfo,pCurEpDist,pCurEpDist->TxCreditsMin);
                /* control service is always marked active, it never goes inactive EVER */
            SET_EP_ACTIVE(pCurEpDist);
        } else if (pCurEpDist->ServiceID == WMI_DATA_BK_SVC) {
                /* this is the lowest priority data endpoint, save this off for easy access */
            pCredInfo->pLowestPriEpDist = pCurEpDist;
        }

        /* Streams have to be created (explicit | implicit)for all kinds
         * of traffic. BE endpoints are also inactive in the beginning.
         * When BE traffic starts it creates implicit streams that
         * redistributes credits.
         */

        /* note, all other endpoints have minimums set but are initially given NO credits.
         * Credits will be distributed as traffic activity demands */
        pCurEpDist = pCurEpDist->pNext;
    }

    if (pCredInfo->CurrentFreeCredits <= 0) {
        AR_DEBUG_PRINTF(ATH_LOG_INF, ("Not enough credits (%d) to do credit distributions \n", TotalCredits));
        A_ASSERT(false);
        return;
    }

        /* reset list */
    pCurEpDist = pEPList;
        /* now run through the list and set max operating credit limits for everyone */
    while (pCurEpDist != NULL) {
        if (pCurEpDist->ServiceID == WMI_CONTROL_SVC) {
                /* control service max is just 1 max message */
            pCurEpDist->TxCreditsNorm = pCurEpDist->TxCreditsPerMaxMsg;
        } else {
                /* for the remaining data endpoints, we assume that each TxCreditsPerMaxMsg are
                 * the same.
                 * We use a simple calculation here, we take the remaining credits and
                 * determine how many max messages this can cover and then set each endpoint's
                 * normal value equal to 3/4 this amount.
                 * */
            count = (pCredInfo->CurrentFreeCredits/pCurEpDist->TxCreditsPerMaxMsg) * pCurEpDist->TxCreditsPerMaxMsg;
            count = (count * 3) >> 2;
            count = max(count,pCurEpDist->TxCreditsPerMaxMsg);
                /* set normal */
            pCurEpDist->TxCreditsNorm = count;

        }
        pCurEpDist = pCurEpDist->pNext;
    }

}


/* default credit distribution callback
 * This callback is invoked whenever endpoints require credit distributions.
 * A lock is held while this function is invoked, this function shall NOT block.
 * The pEPDistList is a list of distribution structures in prioritized order as
 * defined by the call to the HTCSetCreditDistribution() api.
 *
 */
static void ar6000_credit_distribute(void                     *Context,
                                     struct htc_endpoint_credit_dist *pEPDistList,
                                     HTC_CREDIT_DIST_REASON   Reason)
{
    struct htc_endpoint_credit_dist *pCurEpDist;
    struct common_credit_state_info *pCredInfo = (struct common_credit_state_info *)Context;

    switch (Reason) {
        case HTC_CREDIT_DIST_SEND_COMPLETE :
            pCurEpDist = pEPDistList;
                /* we are given the start of the endpoint distribution list.
                 * There may be one or more endpoints to service.
                 * Run through the list and distribute credits */
            while (pCurEpDist != NULL) {

                if (pCurEpDist->TxCreditsToDist > 0) {
                        /* return the credits back to the endpoint */
                    pCurEpDist->TxCredits += pCurEpDist->TxCreditsToDist;
                        /* always zero out when we are done */
                    pCurEpDist->TxCreditsToDist = 0;

                    if (pCurEpDist->TxCredits > pCurEpDist->TxCreditsAssigned) {
                            /* reduce to the assigned limit, previous credit reductions
                             * could have caused the limit to change */
                        ReduceCredits(pCredInfo, pCurEpDist, pCurEpDist->TxCreditsAssigned);
                    }

                    if (pCurEpDist->TxCredits > pCurEpDist->TxCreditsNorm) {
                            /* oversubscribed endpoints need to reduce back to normal */
                        ReduceCredits(pCredInfo, pCurEpDist, pCurEpDist->TxCreditsNorm);
                    }
                
                    if (!IS_EP_ACTIVE(pCurEpDist)) {
                            /* endpoint is inactive, now check for messages waiting for credits */
                        if (pCurEpDist->TxQueueDepth == 0) {
                                /* EP is inactive and there are no pending messages, 
                                 * reduce credits back to zero to recover credits */
                            ReduceCredits(pCredInfo, pCurEpDist, 0);
                        }
                    }
                }

                pCurEpDist = pCurEpDist->pNext;
            }

            break;

        case HTC_CREDIT_DIST_ACTIVITY_CHANGE :
            RedistributeCredits(pCredInfo,pEPDistList);
            break;
        case HTC_CREDIT_DIST_SEEK_CREDITS :
            SeekCredits(pCredInfo,pEPDistList);
            break;
        case HTC_DUMP_CREDIT_STATE :
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Credit Distribution, total : %d, free : %d\n",
            								pCredInfo->TotalAvailableCredits, pCredInfo->CurrentFreeCredits));
            break;
        default:
            break;

    }

        /* sanity checks done after each distribution action */
    A_ASSERT(pCredInfo->CurrentFreeCredits <= pCredInfo->TotalAvailableCredits);
    A_ASSERT(pCredInfo->CurrentFreeCredits >= 0);

}

/* redistribute credits based on activity change */
static void RedistributeCredits(struct common_credit_state_info *pCredInfo,
                                struct htc_endpoint_credit_dist *pEPDistList)
{
    struct htc_endpoint_credit_dist *pCurEpDist = pEPDistList;

        /* walk through the list and remove credits from inactive endpoints */
    while (pCurEpDist != NULL) {

#ifdef CONFIG_GIVE_LOW_PRIORITY_STREAMS_MIN_CREDITS

        if ((pCurEpDist->ServiceID == WMI_DATA_BK_SVC)  || (pCurEpDist->ServiceID == WMI_DATA_BE_SVC)) {
              /* force low priority streams to always be active to retain their minimum credit distribution */
             SET_EP_ACTIVE(pCurEpDist);
        }
#endif

        if (pCurEpDist->ServiceID != WMI_CONTROL_SVC) {
            if (!IS_EP_ACTIVE(pCurEpDist)) {
                if (pCurEpDist->TxQueueDepth == 0) {
                        /* EP is inactive and there are no pending messages, reduce credits back to zero */
                    ReduceCredits(pCredInfo, pCurEpDist, 0);
                } else {
                        /* we cannot zero the credits assigned to this EP, but to keep
                         * the credits available for these leftover packets, reduce to
                         * a minimum */
                    ReduceCredits(pCredInfo, pCurEpDist, pCurEpDist->TxCreditsMin);
                }
            }
        }

        /* NOTE in the active case, we do not need to do anything further,
         * when an EP goes active and needs credits, HTC will call into
         * our distribution function using a reason code of HTC_CREDIT_DIST_SEEK_CREDITS  */

        pCurEpDist = pCurEpDist->pNext;
    }

}

/* HTC has an endpoint that needs credits, pEPDist is the endpoint in question */
static void SeekCredits(struct common_credit_state_info *pCredInfo,
                        struct htc_endpoint_credit_dist *pEPDist)
{
    struct htc_endpoint_credit_dist *pCurEpDist;
    int                      credits = 0;
    int                      need;

    do {

        if (pEPDist->ServiceID == WMI_CONTROL_SVC) {
                /* we never oversubscribe on the control service, this is not
                 * a high performance path and the target never holds onto control
                 * credits for too long */
            break;
        }

#ifdef CONFIG_GIVE_LOW_PRIORITY_STREAMS_MIN_CREDITS
        if (pEPDist->ServiceID == WMI_DATA_VI_SVC) {
            if ((pEPDist->TxCreditsAssigned >= pEPDist->TxCreditsNorm)) {
                 /* limit VI service from oversubscribing */
                 break;
            }
        }
 
        if (pEPDist->ServiceID == WMI_DATA_VO_SVC) {
            if ((pEPDist->TxCreditsAssigned >= pEPDist->TxCreditsNorm)) {
                 /* limit VO service from oversubscribing */
                break;
            }
        }
#else
        if (pEPDist->ServiceID == WMI_DATA_VI_SVC) {
            if ((pEPDist->TxCreditsAssigned >= pEPDist->TxCreditsNorm) ||
                (pCredInfo->CurrentFreeCredits <= pEPDist->TxCreditsPerMaxMsg)) {
                 /* limit VI service from oversubscribing */
                 /* at least one free credit will not be used by VI */
                 break;
            }
        }
 
        if (pEPDist->ServiceID == WMI_DATA_VO_SVC) {
            if ((pEPDist->TxCreditsAssigned >= pEPDist->TxCreditsNorm) ||
                (pCredInfo->CurrentFreeCredits <= pEPDist->TxCreditsPerMaxMsg)) {
                 /* limit VO service from oversubscribing */
                 /* at least one free credit will not be used by VO */
                break;
            }
        }
#endif

        /* for all other services, we follow a simple algorithm of
         * 1. checking the free pool for credits
         * 2. checking lower priority endpoints for credits to take */

            /* give what we can */
        credits = min(pCredInfo->CurrentFreeCredits,pEPDist->TxCreditsSeek);

        if (credits >= pEPDist->TxCreditsSeek) {
                /* we found some to fulfill the seek request */
            break;
        }

        /* we don't have enough in the free pool, try taking away from lower priority services
         *
         * The rule for taking away credits:
         *   1. Only take from lower priority endpoints
         *   2. Only take what is allocated above the minimum (never starve an endpoint completely)
         *   3. Only take what you need.
         *
         * */

            /* starting at the lowest priority */
        pCurEpDist = pCredInfo->pLowestPriEpDist;

            /* work backwards until we hit the endpoint again */
        while (pCurEpDist != pEPDist) {
                /* calculate how many we need so far */
            need = pEPDist->TxCreditsSeek - pCredInfo->CurrentFreeCredits;

            if ((pCurEpDist->TxCreditsAssigned - need) >= pCurEpDist->TxCreditsMin) {
                    /* the current one has been allocated more than it's minimum and it
                     * has enough credits assigned above it's minimum to fulfill our need
                     * try to take away just enough to fulfill our need */
                ReduceCredits(pCredInfo,
                              pCurEpDist,
                              pCurEpDist->TxCreditsAssigned - need);

                if (pCredInfo->CurrentFreeCredits >= pEPDist->TxCreditsSeek) {
                        /* we have enough */
                    break;
                }
            }

            pCurEpDist = pCurEpDist->pPrev;
        }

            /* return what we can get */
        credits = min(pCredInfo->CurrentFreeCredits,pEPDist->TxCreditsSeek);

    } while (false);

        /* did we find some credits? */
    if (credits) {
            /* give what we can */
        GiveCredits(pCredInfo, pEPDist, credits);
    }

}

/* initialize and setup credit distribution */
int ar6000_setup_credit_dist(HTC_HANDLE HTCHandle, struct common_credit_state_info *pCredInfo)
{
    HTC_SERVICE_ID servicepriority[5];

    A_MEMZERO(pCredInfo,sizeof(struct common_credit_state_info));

    servicepriority[0] = WMI_CONTROL_SVC;  /* highest */
    servicepriority[1] = WMI_DATA_VO_SVC;
    servicepriority[2] = WMI_DATA_VI_SVC;
    servicepriority[3] = WMI_DATA_BE_SVC;
    servicepriority[4] = WMI_DATA_BK_SVC; /* lowest */

        /* set callbacks and priority list */
    HTCSetCreditDistribution(HTCHandle,
                             pCredInfo,
                             ar6000_credit_distribute,
                             ar6000_credit_init,
                             servicepriority,
                             5);

    return 0;
}

