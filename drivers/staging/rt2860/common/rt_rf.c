/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	rt_rf.c

	Abstract:
	Ralink Wireless driver RF related functions

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/

#include "../rt_config.h"

#ifdef RTMP_RF_RW_SUPPORT
/*
	========================================================================

	Routine Description: Write RT30xx RF register through MAC

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RT30xxWriteRFRegister(struct rt_rtmp_adapter *pAd,
				  u8 regID, u8 value)
{
	RF_CSR_CFG_STRUC rfcsr;
	u32 i = 0;

	do {
		RTMP_IO_READ32(pAd, RF_CSR_CFG, &rfcsr.word);

		if (!rfcsr.field.RF_CSR_KICK)
			break;
		i++;
	}
	while ((i < RETRY_LIMIT)
	       && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)));

	if ((i == RETRY_LIMIT)
	    || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("Retry count exhausted or device removed!\n"));
		return STATUS_UNSUCCESSFUL;
	}

	rfcsr.field.RF_CSR_WR = 1;
	rfcsr.field.RF_CSR_KICK = 1;
	rfcsr.field.TESTCSR_RFACC_REGNUM = regID;
	rfcsr.field.RF_CSR_DATA = value;

	RTMP_IO_WRITE32(pAd, RF_CSR_CFG, rfcsr.word);

	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description: Read RT30xx RF register through MAC

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RT30xxReadRFRegister(struct rt_rtmp_adapter *pAd,
				 u8 regID, u8 *pValue)
{
	RF_CSR_CFG_STRUC rfcsr;
	u32 i = 0, k = 0;

	for (i = 0; i < MAX_BUSY_COUNT; i++) {
		RTMP_IO_READ32(pAd, RF_CSR_CFG, &rfcsr.word);

		if (rfcsr.field.RF_CSR_KICK == BUSY) {
			continue;
		}
		rfcsr.word = 0;
		rfcsr.field.RF_CSR_WR = 0;
		rfcsr.field.RF_CSR_KICK = 1;
		rfcsr.field.TESTCSR_RFACC_REGNUM = regID;
		RTMP_IO_WRITE32(pAd, RF_CSR_CFG, rfcsr.word);
		for (k = 0; k < MAX_BUSY_COUNT; k++) {
			RTMP_IO_READ32(pAd, RF_CSR_CFG, &rfcsr.word);

			if (rfcsr.field.RF_CSR_KICK == IDLE)
				break;
		}
		if ((rfcsr.field.RF_CSR_KICK == IDLE) &&
		    (rfcsr.field.TESTCSR_RFACC_REGNUM == regID)) {
			*pValue = (u8)rfcsr.field.RF_CSR_DATA;
			break;
		}
	}
	if (rfcsr.field.RF_CSR_KICK == BUSY) {
		DBGPRINT_ERR("RF read R%d=0x%x fail, i[%d], k[%d]\n", regID, rfcsr.word, i, k);
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}

void NICInitRFRegisters(struct rt_rtmp_adapter *pAd)
{
	if (pAd->chipOps.AsicRfInit)
		pAd->chipOps.AsicRfInit(pAd);
}

void RtmpChipOpsRFHook(struct rt_rtmp_adapter *pAd)
{
	struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;

	pChipOps->pRFRegTable = NULL;
	pChipOps->AsicRfInit = NULL;
	pChipOps->AsicRfTurnOn = NULL;
	pChipOps->AsicRfTurnOff = NULL;
	pChipOps->AsicReverseRfFromSleepMode = NULL;
	pChipOps->AsicHaltAction = NULL;
	/* We depends on RfICType and MACVersion to assign the corresponding operation callbacks. */

#ifdef RT30xx
	if (IS_RT30xx(pAd)) {
		pChipOps->pRFRegTable = RT30xx_RFRegTable;
		pChipOps->AsicHaltAction = RT30xxHaltAction;
#ifdef RT3070
		if ((IS_RT3070(pAd) || IS_RT3071(pAd))
		    && (pAd->infType == RTMP_DEV_INF_USB)) {
			pChipOps->AsicRfInit = NICInitRT3070RFRegisters;
			if (IS_RT3071(pAd)) {
				pChipOps->AsicRfTurnOff =
				    RT30xxLoadRFSleepModeSetup;
				pChipOps->AsicReverseRfFromSleepMode =
				    RT30xxReverseRFSleepModeSetup;
			}
		}
#endif /* RT3070 // */
#ifdef RT3090
		if (IS_RT3090(pAd) && (pAd->infType == RTMP_DEV_INF_PCI)) {
			pChipOps->AsicRfTurnOff = RT30xxLoadRFSleepModeSetup;
			pChipOps->AsicRfInit = NICInitRT3090RFRegisters;
			pChipOps->AsicReverseRfFromSleepMode =
			    RT30xxReverseRFSleepModeSetup;
		}
#endif /* RT3090 // */
	}
#endif /* RT30xx // */
}

#endif /* RTMP_RF_RW_SUPPORT // */
