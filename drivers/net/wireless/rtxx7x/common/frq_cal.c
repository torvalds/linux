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


#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
#ifdef CONFIG_STA_SUPPORT

#include	"rt_config.h"

/*
	Sometimes frequency will be shift for RT3593 we need to adjust it when
	the frequencey shift.
*/

/* Initialize the frequency calibration*/

/* Parameters*/
/*	pAd: The adapter data structure*/

/* Return Value:*/
/*	None*/

VOID InitFrequencyCalibration(
	IN PRTMP_ADAPTER pAd)
{
	BBP_R179_STRUC BbpR179 = {{0}};
	BBP_R180_STRUC BbpR180 = {{0}};
	BBP_R182_STRUC BbpR182 = {{0}};

	if (pAd->FreqCalibrationCtrl.bEnableFrequencyCalibration == TRUE)
	{	
		DBGPRINT(RT_DEBUG_TRACE, ("---> %s\n", __FUNCTION__));
		
		
		/* Initialize the RX_END_STATUS (1) for "Rx OFDM/CCK frequency offset report"*/
		
		if (IS_RT5390(pAd))
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R142, 1);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R143, BBP_R57); /* Rx OFDM/CCK frequency offset report*/
		}
		else if (IS_RT3390(pAd))
		{
			
			/* Initialize the RX_END_STATUS (1, 5) for "Rx OFDM/CCK frequency offset report"*/
			
			BbpR179.field.DataIndex1 = 1;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R179, BbpR179.byte);
			BbpR180.field.DataIndex2 = 5;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R180, BbpR180.byte);
			BbpR182.field.DataArray = BBP_R57; /* Rx OFDM/CCK frequency offset report*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R182, BbpR182.byte);
		}
		else
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: Not support IC type (MACVersion = 0x%X)\n", __FUNCTION__, pAd->MACVersion));
		}
		
		StopFrequencyCalibration(pAd);

		DBGPRINT(RT_DEBUG_TRACE, ("%s: frequency offset in the EEPROM = %ld\n", 
			__FUNCTION__, 
			pAd->RfFreqOffset));

		DBGPRINT(RT_DEBUG_TRACE, ("<--- %s\n", __FUNCTION__));
	}
}


/* To stop the frequency calibration algorithm*/

/* Parameters*/
/*	pAd: The adapter data structure*/

/* Return Value:*/
/*	None*/

VOID StopFrequencyCalibration(
	IN PRTMP_ADAPTER pAd)
{
	if (pAd->FreqCalibrationCtrl.bEnableFrequencyCalibration == TRUE)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("---> %s\n", __FUNCTION__));

		/* Base on the frequency offset of the EEPROM*/
		pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = (0x7F & ((CHAR)(pAd->RfFreqOffset))); /* C1 value control - Crystal calibration*/

		pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon = INVALID_FREQUENCY_OFFSET;

		pAd->FreqCalibrationCtrl.bSkipFirstFrequencyCalibration = TRUE;

		DBGPRINT(RT_DEBUG_TRACE, ("%s: pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = 0x%X\n", 
			__FUNCTION__, 
			pAd->FreqCalibrationCtrl.AdaptiveFreqOffset));
		
		DBGPRINT(RT_DEBUG_TRACE, ("<--- %s\n", __FUNCTION__));
	}
}



/* The frequency calibration algorithm*/

/* Parameters*/
/*	pAd: The adapter data structure*/

/* Return Value:*/
/*	None*/

VOID FrequencyCalibration(
	IN PRTMP_ADAPTER pAd)
{
	BOOLEAN bUpdateRFR = FALSE;
	UCHAR RFValue = 0;
	UCHAR PreRFValue = 0;
	CHAR HighFreqTriggerPoint = 0, LowFreqTriggerPoint = 0;
	CHAR DecreaseFreqOffset = 0, IncreaseFreqOffset = 0;
	
	/* Frequency calibration period: */
	/* a) 10 seconds: Check the reported frequency offset*/
	/* b) 500 ms: Update the RF frequency if possible*/
	
	if ((pAd->FreqCalibrationCtrl.bEnableFrequencyCalibration == TRUE) && 
	     (((pAd->FreqCalibrationCtrl.bApproachFrequency == FALSE) && ((pAd->Mlme.PeriodicRound % FREQUENCY_CALIBRATION_PERIOD) == 0)) || 
	       ((pAd->FreqCalibrationCtrl.bApproachFrequency == TRUE) && ((pAd->Mlme.PeriodicRound % (FREQUENCY_CALIBRATION_PERIOD / 20)) == 0))))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("---> %s\n", __FUNCTION__));

		if (pAd->FreqCalibrationCtrl.bSkipFirstFrequencyCalibration == TRUE)
		{
			pAd->FreqCalibrationCtrl.bSkipFirstFrequencyCalibration = FALSE;

			DBGPRINT(RT_DEBUG_TRACE, ("%s: Skip cuurent frequency calibration (avoid calibrating frequency at the time the STA is just link-up)\n", __FUNCTION__));
		}
		else
		{

			if (pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon != INVALID_FREQUENCY_OFFSET)
			{	
				/* Sync the thresholds*/
				
				if (pAd->FreqCalibrationCtrl.BeaconPhyMode == MODE_CCK) /* CCK*/
				{
					HighFreqTriggerPoint = HIGH_FREQUENCY_TRIGGER_POINT_CCK;
					LowFreqTriggerPoint = LOW_FREQUENCY_TRIGGER_POINT_CCK;

					DecreaseFreqOffset = DECREASE_FREQUENCY_OFFSET_CCK;
					IncreaseFreqOffset = INCREASE_FREQUENCY_OFFSET_CCK;
				}
				else /* OFDM*/
				{
					HighFreqTriggerPoint = HIGH_FREQUENCY_TRIGGER_POINT_OFDM;
					LowFreqTriggerPoint = LOW_FREQUENCY_TRIGGER_POINT_OFDM;

					DecreaseFreqOffset = DECREASE_FREQUENCY_OFFSET_OFDM;
					IncreaseFreqOffset = INCREASE_FREQUENCY_OFFSET_OFDM;
				}
				
				if ((pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon >= HighFreqTriggerPoint) || 
				     (pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon <= LowFreqTriggerPoint))
				{
					pAd->FreqCalibrationCtrl.bApproachFrequency = TRUE;
				}
				
				if (pAd->FreqCalibrationCtrl.bApproachFrequency == TRUE)
				{
					if ((pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon <= DecreaseFreqOffset) && 
					      (pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon >= IncreaseFreqOffset))
					{
						pAd->FreqCalibrationCtrl.bApproachFrequency = FALSE; /* Stop approaching frquency if -10 < reported frequency offset < 10*/
					}
					else if (pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon > DecreaseFreqOffset)
					{
						pAd->FreqCalibrationCtrl.AdaptiveFreqOffset--;
						if (IS_RT3390(pAd))
						{
							RT30xxReadRFRegister(pAd, RF_R23, (PUCHAR)(&RFValue));
							RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F));
							RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
							pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = RFValue; /* Keep modified RF R23 value */
							RT30xxWriteRFRegister(pAd, RF_R23, (UCHAR)RFValue);

							RT30xxReadRFRegister(pAd, RF_R07, (PUCHAR)(&RFValue));
							RFValue = ((RFValue & ~0x01) | 0x01); /* Tune_en (initiate VCO calibration (reset after completion)) */
							RT30xxWriteRFRegister(pAd, RF_R07, (UCHAR)RFValue);
						}
						else if (IS_RT5390(pAd))
						{
							RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)(&RFValue));
							PreRFValue = RFValue;
							RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F));
							RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
							pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = RFValue; /* Keep modified RF R17 value */
							if (PreRFValue != RFValue)
							{
								AsicSendCommandToMcu(pAd, 0x74, 0xff, RFValue, PreRFValue);
							}

							RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)&RFValue);
							RFValue = ((RFValue & ~0x80) | 0x80); /* vcocal_en (initiate VCO calibration (reset after completion)) - It should be at the end of RF configuration. */
							RT30xxWriteRFRegister(pAd, RF_R03, (UCHAR)RFValue);
						}
						else if (IS_RT3593(pAd))
						{
							RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)(&RFValue));
							RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F));
							RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
							pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = RFValue; // Keep modified RF R17 value
							RT30xxWriteRFRegister(pAd, RF_R17, (UCHAR)RFValue);

							RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)&RFValue);
							RFValue = ((RFValue & ~0x80) | 0x80); /* vcocal_en (initiate VCO calibration (reset after completion)) - It should be at the end of RF configuration. */
							RT30xxWriteRFRegister(pAd, RF_R03, (UCHAR)RFValue);
						}
						else
						{
							DBGPRINT(RT_DEBUG_ERROR, ("%s: Not support IC type (MACVersion = 0x%X)\n", __FUNCTION__, pAd->MACVersion));
						}

						DBGPRINT(RT_DEBUG_TRACE, ("%s: -- frequency offset = 0x%X\n", __FUNCTION__, pAd->FreqCalibrationCtrl.AdaptiveFreqOffset));
					}
					else if (pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon < IncreaseFreqOffset)
					{
						pAd->FreqCalibrationCtrl.AdaptiveFreqOffset++;
						if (IS_RT3390(pAd))
						{
							RT30xxReadRFRegister(pAd, RF_R23, (PUCHAR)(&RFValue));
							RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F));
							RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
							pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = RFValue; /* Keep modified RF R23 value */
							RT30xxWriteRFRegister(pAd, RF_R23, (UCHAR)RFValue);

							RT30xxReadRFRegister(pAd, RF_R07, (PUCHAR)(&RFValue));
							RFValue = ((RFValue & ~0x01) | 0x01); /* Tune_en (initiate VCO calibration (reset after completion)) */
							RT30xxWriteRFRegister(pAd, RF_R07, (UCHAR)RFValue);
						}
						else if (IS_RT5390(pAd))
						{
							RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)(&RFValue));
							PreRFValue = RFValue;
							RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F));
							RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
							pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = RFValue; /* Keep modified RF R17 value */
							if (PreRFValue != RFValue)
							{
								AsicSendCommandToMcu(pAd, 0x74, 0xff, RFValue, PreRFValue);
							}

							RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)&RFValue);
						RFValue = ((RFValue & ~0x80) | 0x80); /* vcocal_en (initiate VCO calibration (reset after completion)) - It should be at the end of RF configuration.*/
						RT30xxWriteRFRegister(pAd, RF_R03, (UCHAR)RFValue);
						}
						else if (IS_RT3593(pAd))
						{
							RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)(&RFValue));
							RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F));
							RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
							pAd->FreqCalibrationCtrl.AdaptiveFreqOffset = RFValue; /* Keep modified RF R17 value */
							RT30xxWriteRFRegister(pAd, RF_R17, (UCHAR)RFValue);

							RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)&RFValue);
							RFValue = ((RFValue & ~0x80) | 0x80); /* vcocal_en (initiate VCO calibration (reset after completion)) - It should be at the end of RF configuration. */
							RT30xxWriteRFRegister(pAd, RF_R03, (UCHAR)RFValue);
						}
						else
						{
							DBGPRINT(RT_DEBUG_ERROR, ("%s: Not support IC type (MACVersion = 0x%X)\n", __FUNCTION__, pAd->MACVersion));
						}
						DBGPRINT(RT_DEBUG_TRACE, ("%s: ++ frequency offset = 0x%X\n", __FUNCTION__, pAd->FreqCalibrationCtrl.AdaptiveFreqOffset));
					}
				}

				DBGPRINT(RT_DEBUG_TRACE, ("%s: AdaptiveFreqOffset = %d, LatestFreqOffsetOverBeacon = %d, bApproachFrequency = %d\n", 
					__FUNCTION__, 
					pAd->FreqCalibrationCtrl.AdaptiveFreqOffset, 
					pAd->FreqCalibrationCtrl.LatestFreqOffsetOverBeacon, 
					pAd->FreqCalibrationCtrl.bApproachFrequency));

			}
		}
		
		DBGPRINT(RT_DEBUG_TRACE, ("<--- %s\n", __FUNCTION__));
	}
}



/* Get the frequency offset*/

/* Parameters*/
/*	pAd: The adapter data structure*/
/*	pRxWI: Point to the RxWI structure*/

/* Return Value:*/
/*	Frequency offset*/

CHAR GetFrequencyOffset(
	IN PRTMP_ADAPTER pAd, 
	IN PRXWI_STRUC pRxWI)
{
	CHAR FreqOffset = 0;
	
	if (pAd->FreqCalibrationCtrl.bEnableFrequencyCalibration == TRUE)
	{	
		DBGPRINT(RT_DEBUG_INFO, ("---> %s\n", __FUNCTION__));

		FreqOffset = (CHAR)(pRxWI->FOFFSET);

		if ((FreqOffset < LOWERBOUND_OF_FREQUENCY_OFFSET) || 
		     (FreqOffset > UPPERBOUND_OF_FREQUENCY_OFFSET))
		{
			FreqOffset = INVALID_FREQUENCY_OFFSET;

			DBGPRINT(RT_DEBUG_ERROR, ("%s: (out-of-range) FreqOffset = %d\n", 
				__FUNCTION__, 
				FreqOffset));
		}

		DBGPRINT(RT_DEBUG_INFO, ("%s: FreqOffset = %d\n", 
			__FUNCTION__, 
			FreqOffset));

		DBGPRINT(RT_DEBUG_INFO, ("<--- %s\n", __FUNCTION__));
	}

	return FreqOffset;
}

#endif /* CONFIG_STA_SUPPORT */
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */

