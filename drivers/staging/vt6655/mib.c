/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: mib.c
 *
 * Purpose: Implement MIB Data Structure
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Functions:
 *      STAvUpdateIstStatCounter - Update ISR statistic counter
 *      STAvUpdate802_11Counter - Update 802.11 mib counter
 *
 * Revision History:
 *
 */

#include "mac.h"
#include "mib.h"

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*
 * Description: Update Isr Statistic Counter
 *
 * Parameters:
 *  In:
 *      pStatistic  - Pointer to Statistic Counter Data Structure
 *      wisr        - Interrupt status
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void STAvUpdateIsrStatCounter(PSStatCounter pStatistic, unsigned long dwIsr)
{
	/**********************/
	/* ABNORMAL interrupt */
	/**********************/
	/* not any IMR bit invoke irq */

	if (dwIsr == 0) {
		pStatistic->ISRStat.dwIsrUnknown++;
		return;
	}

/* Added by Kyle */
	if (dwIsr & ISR_TXDMA0)               /* ISR, bit0 */
		pStatistic->ISRStat.dwIsrTx0OK++;             /* TXDMA0 successful */

	if (dwIsr & ISR_AC0DMA)               /* ISR, bit1 */
		pStatistic->ISRStat.dwIsrAC0TxOK++;           /* AC0DMA successful */

	if (dwIsr & ISR_BNTX)                 /* ISR, bit2 */
		pStatistic->ISRStat.dwIsrBeaconTxOK++;        /* BeaconTx successful */

	if (dwIsr & ISR_RXDMA0)               /* ISR, bit3 */
		pStatistic->ISRStat.dwIsrRx0OK++;             /* Rx0 successful */

	if (dwIsr & ISR_TBTT)                 /* ISR, bit4 */
		pStatistic->ISRStat.dwIsrTBTTInt++;           /* TBTT successful */

	if (dwIsr & ISR_SOFTTIMER)            /* ISR, bit6 */
		pStatistic->ISRStat.dwIsrSTIMERInt++;

	if (dwIsr & ISR_WATCHDOG)             /* ISR, bit7 */
		pStatistic->ISRStat.dwIsrWatchDog++;

	if (dwIsr & ISR_FETALERR)             /* ISR, bit8 */
		pStatistic->ISRStat.dwIsrUnrecoverableError++;

	if (dwIsr & ISR_SOFTINT)              /* ISR, bit9 */
		pStatistic->ISRStat.dwIsrSoftInterrupt++;     /* software interrupt */

	if (dwIsr & ISR_MIBNEARFULL)          /* ISR, bit10 */
		pStatistic->ISRStat.dwIsrMIBNearfull++;

	if (dwIsr & ISR_RXNOBUF)              /* ISR, bit11 */
		pStatistic->ISRStat.dwIsrRxNoBuf++;           /* Rx No Buff */

	if (dwIsr & ISR_RXDMA1)               /* ISR, bit12 */
		pStatistic->ISRStat.dwIsrRx1OK++;             /* Rx1 successful */

	if (dwIsr & ISR_SOFTTIMER1)           /* ISR, bit21 */
		pStatistic->ISRStat.dwIsrSTIMER1Int++;
}

/*
 * Description: Update 802.11 mib counter
 *
 * Parameters:
 *  In:
 *      p802_11Counter  - Pointer to 802.11 mib counter
 *      pStatistic      - Pointer to Statistic Counter Data Structure
 *      dwCounter       - hardware counter for 802.11 mib
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void
STAvUpdate802_11Counter(
	PSDot11Counters         p802_11Counter,
	PSStatCounter           pStatistic,
	unsigned long dwCounter
)
{
	p802_11Counter->RTSSuccessCount += (unsigned long long)  (dwCounter & 0x000000ff);
	p802_11Counter->RTSFailureCount += (unsigned long long) ((dwCounter & 0x0000ff00) >> 8);
	p802_11Counter->ACKFailureCount += (unsigned long long) ((dwCounter & 0x00ff0000) >> 16);
	p802_11Counter->FCSErrorCount +=   (unsigned long long) ((dwCounter & 0xff000000) >> 24);
}
