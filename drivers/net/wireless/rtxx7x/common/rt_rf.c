/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#include "rt_config.h"


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
NDIS_STATUS RT30xxWriteRFRegister(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			regID,
	IN	UCHAR			value)
{
	RF_CSR_CFG_STRUC	rfcsr = { { 0 } };
	UINT				i = 0;
#if defined(RT3593) || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	RF_CSR_CFG_EXT_STRUC RfCsrCfgExt = { { 0 } };
#endif /* RT3593 || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */



#if defined(RT3593) || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT3593(pAd) || IS_RT5390(pAd))
	{
		ASSERT((regID <= 63)); /* R0~R63*/

		do
		{
			RTMP_IO_READ32(pAd, RF_CSR_CFG, &RfCsrCfgExt.word);

			if (!RfCsrCfgExt.field.RF_CSR_KICK)
			{
				break;
			}
			
			i++;
		}
		
		while ((i < MAX_BUSY_COUNT) && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
			; /* Do nothing*/

		if ((i == MAX_BUSY_COUNT) || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Retry count exhausted or device removed!!!\n"));
			return STATUS_UNSUCCESSFUL;
		}

		RfCsrCfgExt.field.RF_CSR_WR = 1;
		RfCsrCfgExt.field.RF_CSR_KICK = 1;
		RfCsrCfgExt.field.TESTCSR_RFACC_REGNUM = regID; /* R0~R63*/
		RfCsrCfgExt.field.RF_CSR_DATA = value;
		
		RTMP_IO_WRITE32(pAd, RF_CSR_CFG, RfCsrCfgExt.word);

	}
	else
#endif /* RT3593  || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	{
		ASSERT((regID <= pAd->chipCap.MaxNumOfRfId)); /* R0~R31 or R63*/

		do
		{
			RTMP_IO_READ32(pAd, RF_CSR_CFG, &rfcsr.word);

			if (!rfcsr.field.RF_CSR_KICK)
				break;
			i++;
		}
		while ((i < RETRY_LIMIT) && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)));

		if ((i == RETRY_LIMIT) || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
		{
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Retry count exhausted or device removed!!!\n"));
			return STATUS_UNSUCCESSFUL;
		}

		if ((pAd->chipCap.RfReg17WtMethod == RF_REG_WT_METHOD_STEP_ON) &&
			(regID == RF_R17))
		{
			UINT32 IdRf;
			UCHAR RfValue;

			RT30xxReadRFRegister(pAd, RF_R17, &RfValue);

			rfcsr.field.RF_CSR_WR = 1;
			rfcsr.field.RF_CSR_KICK = 1;
			rfcsr.field.TESTCSR_RFACC_REGNUM = regID; /* R0~R31*/

			if (RfValue <= value)
			{
				for(IdRf=RfValue; IdRf<=value; IdRf++)
				{
					rfcsr.field.RF_CSR_DATA = IdRf;
					RTMP_IO_WRITE32(pAd, RF_CSR_CFG, rfcsr.word);
				}
			}
			else
			{
				for(IdRf=RfValue; IdRf>=value; IdRf--)
				{
					rfcsr.field.RF_CSR_DATA = IdRf;
					RTMP_IO_WRITE32(pAd, RF_CSR_CFG, rfcsr.word);
				}
			}
		}
		else
		{
			rfcsr.field.RF_CSR_WR = 1;
			rfcsr.field.RF_CSR_KICK = 1;
			rfcsr.field.TESTCSR_RFACC_REGNUM = regID; /* R0~R31*/
			rfcsr.field.RF_CSR_DATA = value;
			RTMP_IO_WRITE32(pAd, RF_CSR_CFG, rfcsr.word);
		}
	}

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
NDIS_STATUS RT30xxReadRFRegister(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			regID,
	IN	PUCHAR			pValue)
{
	RF_CSR_CFG_STRUC	rfcsr = { { 0 } };
	UINT				i=0, k=0;

#if defined(RT3593) || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	RF_CSR_CFG_EXT_STRUC RfCsrCfgExt = { { 0 } };
#endif /* RT3593 || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */



#if defined(RT3593) || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT3593(pAd) || IS_RT5390(pAd))
	{
		ASSERT((regID <= 63)); /* R0~R63*/
		
		for (i = 0; i < MAX_BUSY_COUNT; i++)
		{
			if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
				return STATUS_UNSUCCESSFUL;
			
			RTMP_IO_READ32(pAd, RF_CSR_CFG, &RfCsrCfgExt.word);

			if (RfCsrCfgExt.field.RF_CSR_KICK == BUSY)
			{
				continue;
			}
			
			RfCsrCfgExt.word = 0;
			RfCsrCfgExt.field.RF_CSR_WR = 0;
			RfCsrCfgExt.field.RF_CSR_KICK = 1;
			RfCsrCfgExt.field.TESTCSR_RFACC_REGNUM = regID; /* R0~R63*/
			
			RTMP_IO_WRITE32(pAd, RF_CSR_CFG, RfCsrCfgExt.word);
			
			for (k = 0; k < MAX_BUSY_COUNT; k++)
			{
				if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
					return STATUS_UNSUCCESSFUL;
				
				RTMP_IO_READ32(pAd, RF_CSR_CFG, &RfCsrCfgExt.word);

				if (RfCsrCfgExt.field.RF_CSR_KICK == IDLE)
				{
					break;
				}
			}
			
			if ((RfCsrCfgExt.field.RF_CSR_KICK == IDLE) && 
			     (RfCsrCfgExt.field.TESTCSR_RFACC_REGNUM == regID))
			{
				*pValue = (UCHAR)(RfCsrCfgExt.field.RF_CSR_DATA);
				break;
			}
		}

		if (RfCsrCfgExt.field.RF_CSR_KICK == BUSY)
		{																	
			DBGPRINT_ERR(("RF read R%d = 0x%X fail, i[%d], k[%d]\n", regID, (UINT32)RfCsrCfgExt.word, i, k));
			return STATUS_UNSUCCESSFUL;
		}
	}
	else
#endif /* defined(RT3593) || defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	{
		ASSERT((regID <= pAd->chipCap.MaxNumOfRfId)); /* R0~R63*/

		for (i=0; i<MAX_BUSY_COUNT; i++)
		{
			RTMP_IO_READ32(pAd, RF_CSR_CFG, &rfcsr.word);

			if (rfcsr.field.RF_CSR_KICK == BUSY)									
			{																
				continue;													
			}																
			rfcsr.word = 0;
			rfcsr.field.RF_CSR_WR = 0;
			rfcsr.field.RF_CSR_KICK = 1;
			rfcsr.field.TESTCSR_RFACC_REGNUM = regID;
			RTMP_IO_WRITE32(pAd, RF_CSR_CFG, rfcsr.word);
			for (k=0; k<MAX_BUSY_COUNT; k++)
			{
				RTMP_IO_READ32(pAd, RF_CSR_CFG, &rfcsr.word);

				if (rfcsr.field.RF_CSR_KICK == IDLE)
					break;
			}
			if ((rfcsr.field.RF_CSR_KICK == IDLE) &&
				(rfcsr.field.TESTCSR_RFACC_REGNUM == regID))
			{
				*pValue = (UCHAR)(rfcsr.field.RF_CSR_DATA);
				break;
			}
		}
		if (rfcsr.field.RF_CSR_KICK == BUSY)
		{																	
			DBGPRINT_ERR(("RF read R%d=0x%X fail, i[%d], k[%d]\n", regID, rfcsr.word,i,k));
			return STATUS_UNSUCCESSFUL;
		}
	}

	return STATUS_SUCCESS;
}


VOID NICInitRFRegisters(
	IN RTMP_ADAPTER *pAd)
{
	if (pAd->chipOps.AsicRfInit)
		pAd->chipOps.AsicRfInit(pAd);
}


#if 0
VOID RFMultiStepXoCode(
	IN	PRTMP_ADAPTER pAd,
	IN	UCHAR	rfRegID,
	IN	UCHAR	rfRegValue,
	IN	UCHAR	rfRegValuePre)
{
	UINT i = 0, count = 0;
	BOOLEAN bit7IsTrue = (rfRegValue & (0x80));

	rfRegValue &= (0x7F);
	rfRegValuePre &= (0x7F);

	if (rfRegValuePre == rfRegValue)
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("RFMultiStepXoCode--> Write Value 0x%02x, previous Value 0x%02x, bit7IsTrue = %d\n",rfRegValue, rfRegValuePre, bit7IsTrue));

		if (rfRegValue>rfRegValuePre)
	{
		/* Sequentially */
		for (i = rfRegValuePre; i<=rfRegValue; i++)
		{
			if (bit7IsTrue)
				i |=0x80;		
			RT30xxWriteRFRegister(pAd, rfRegID, i);
			count ++;
		}
	}
	else
	{
		/* one step */
		if (bit7IsTrue)
			rfRegValue |=0x80;	
		RT30xxWriteRFRegister(pAd, rfRegID, rfRegValue);
		count++;
	}
		DBGPRINT(RT_DEBUG_TRACE, ("RFMultiStepXoCode<-- Write Value 0x%02x, previous Value 0x%02x, running step count=%d\n",rfRegValue, rfRegValuePre,count));

}
#endif
#endif /* RTMP_RF_RW_SUPPORT */

