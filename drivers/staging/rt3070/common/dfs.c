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
    ap_dfs.c

    Abstract:
    Support DFS function.

    Revision History:
    Who       When            What
    --------  ----------      ----------------------------------------------
    Fonchi    03-12-2007      created
*/

#include "../rt_config.h"

typedef struct _RADAR_DURATION_TABLE
{
	ULONG RDDurRegion;
	ULONG RadarSignalDuration;
	ULONG Tolerance;
} RADAR_DURATION_TABLE, *PRADAR_DURATION_TABLE;


static UCHAR RdIdleTimeTable[MAX_RD_REGION][4] =
{
	{9, 250, 250, 250},		// CE
	{4, 250, 250, 250},		// FCC
	{4, 250, 250, 250},		// JAP
	{15, 250, 250, 250},	// JAP_W53
	{4, 250, 250, 250}		// JAP_W56
};

/*
	========================================================================

	Routine Description:
		Bbp Radar detection routine

	Arguments:
		pAd 	Pointer to our adapter

	Return Value:

	========================================================================
*/
VOID BbpRadarDetectionStart(
	IN PRTMP_ADAPTER pAd)
{
	UINT8 RadarPeriod;

	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 114, 0x02);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 121, 0x20);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 122, 0x00);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 123, 0x08/*0x80*/);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 124, 0x28);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, 125, 0xff);

	RadarPeriod = ((UINT)RdIdleTimeTable[pAd->CommonCfg.RadarDetect.RDDurRegion][0] + (UINT)pAd->CommonCfg.RadarDetect.DfsSessionTime) < 250 ?
			(RdIdleTimeTable[pAd->CommonCfg.RadarDetect.RDDurRegion][0] + pAd->CommonCfg.RadarDetect.DfsSessionTime) : 250;

	RTMP_IO_WRITE8(pAd, 0x7020, 0x1d);
	RTMP_IO_WRITE8(pAd, 0x7021, 0x40);

	RadarDetectionStart(pAd, 0, RadarPeriod);
	return;
}

/*
	========================================================================

	Routine Description:
		Bbp Radar detection routine

	Arguments:
		pAd 	Pointer to our adapter

	Return Value:

	========================================================================
*/
VOID BbpRadarDetectionStop(
	IN PRTMP_ADAPTER pAd)
{
	RTMP_IO_WRITE8(pAd, 0x7020, 0x1d);
	RTMP_IO_WRITE8(pAd, 0x7021, 0x60);

	RadarDetectionStop(pAd);
	return;
}

/*
	========================================================================

	Routine Description:
		Radar detection routine

	Arguments:
		pAd 	Pointer to our adapter

	Return Value:

	========================================================================
*/
VOID RadarDetectionStart(
	IN PRTMP_ADAPTER pAd,
	IN BOOLEAN CTSProtect,
	IN UINT8 CTSPeriod)
{
	UINT8 DfsActiveTime = (pAd->CommonCfg.RadarDetect.DfsSessionTime & 0x1f);
	UINT8 CtsProtect = (CTSProtect == 1) ? 0x02 : 0x01; // CTS protect.

	if (CTSProtect != 0)
	{
		switch(pAd->CommonCfg.RadarDetect.RDDurRegion)
		{
		case FCC:
		case JAP_W56:
			CtsProtect = 0x03;
			break;

		case CE:
		case JAP_W53:
		default:
			CtsProtect = 0x02;
			break;
		}
	}
	else
		CtsProtect = 0x01;


	// send start-RD with CTS protection command to MCU
	// highbyte [7]		reserve
	// highbyte [6:5]	0x: stop Carrier/Radar detection
	// highbyte [10]:	Start Carrier/Radar detection without CTS protection, 11: Start Carrier/Radar detection with CTS protection
	// highbyte [4:0]	Radar/carrier detection duration. In 1ms.

	// lowbyte [7:0]	Radar/carrier detection period, in 1ms.
	AsicSendCommandToMcu(pAd, 0x60, 0xff, CTSPeriod, DfsActiveTime | (CtsProtect << 5));
	//AsicSendCommandToMcu(pAd, 0x63, 0xff, 10, 0);

	return;
}

/*
	========================================================================

	Routine Description:
		Radar detection routine

	Arguments:
		pAd 	Pointer to our adapter

	Return Value:
		TRUE	Found radar signal
		FALSE	Not found radar signal

	========================================================================
*/
VOID RadarDetectionStop(
	IN PRTMP_ADAPTER	pAd)
{
	DBGPRINT(RT_DEBUG_TRACE,("RadarDetectionStop.\n"));
	AsicSendCommandToMcu(pAd, 0x60, 0xff, 0x00, 0x00);	// send start-RD with CTS protection command to MCU

	return;
}

/*
	========================================================================

	Routine Description:
		Radar channel check routine

	Arguments:
		pAd 	Pointer to our adapter

	Return Value:
		TRUE	need to do radar detect
		FALSE	need not to do radar detect

	========================================================================
*/
BOOLEAN RadarChannelCheck(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ch)
{
#if 1
	INT		i;
	BOOLEAN result = FALSE;

	for (i=0; i<pAd->ChannelListNum; i++)
	{
		if (Ch == pAd->ChannelList[i].Channel)
		{
			result = pAd->ChannelList[i].DfsReq;
			break;
		}
	}

	return result;
#else
	INT		i;
	UCHAR	Channel[15]={52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140};

	for (i=0; i<15; i++)
	{
		if (Ch == Channel[i])
		{
			break;
		}
	}

	if (i != 15)
		return TRUE;
	else
		return FALSE;
#endif
}

ULONG JapRadarType(
	IN PRTMP_ADAPTER pAd)
{
	ULONG		i;
	const UCHAR	Channel[15]={52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140};

	if (pAd->CommonCfg.RadarDetect.RDDurRegion != JAP)
	{
		return pAd->CommonCfg.RadarDetect.RDDurRegion;
	}

	for (i=0; i<15; i++)
	{
		if (pAd->CommonCfg.Channel == Channel[i])
		{
			break;
		}
	}

	if (i < 4)
		return JAP_W53;
	else if (i < 15)
		return JAP_W56;
	else
		return JAP; // W52

}

ULONG RTMPBbpReadRadarDuration(
	IN PRTMP_ADAPTER	pAd)
{
	UINT8 byteValue = 0;
	ULONG result;

	BBP_IO_READ8_BY_REG_ID(pAd, BBP_R115, &byteValue);

	result = 0;
	switch (byteValue)
	{
	case 1: // radar signal detected by pulse mode.
	case 2: // radar signal detected by width mode.
		result = RTMPReadRadarDuration(pAd);
		break;

	case 0: // No radar signal.
	default:

		result = 0;
		break;
	}

	return result;
}

ULONG RTMPReadRadarDuration(
	IN PRTMP_ADAPTER	pAd)
{
	ULONG result = 0;

#ifdef DFS_SUPPORT
	UINT8 duration1 = 0, duration2 = 0, duration3 = 0;

	BBP_IO_READ8_BY_REG_ID(pAd, BBP_R116, &duration1);
	BBP_IO_READ8_BY_REG_ID(pAd, BBP_R117, &duration2);
	BBP_IO_READ8_BY_REG_ID(pAd, BBP_R118, &duration3);
	result = (duration1 << 16) + (duration2 << 8) + duration3;
#endif // DFS_SUPPORT //

	return result;

}

VOID RTMPCleanRadarDuration(
	IN PRTMP_ADAPTER	pAd)
{
	return;
}

/*
    ========================================================================
    Routine Description:
        Radar wave detection. The API should be invoke each second.

    Arguments:
        pAd         - Adapter pointer

    Return Value:
        None

    ========================================================================
*/
VOID ApRadarDetectPeriodic(
	IN PRTMP_ADAPTER pAd)
{
	INT	i;

	pAd->CommonCfg.RadarDetect.InServiceMonitorCount++;

	for (i=0; i<pAd->ChannelListNum; i++)
	{
		if (pAd->ChannelList[i].RemainingTimeForUse > 0)
		{
			pAd->ChannelList[i].RemainingTimeForUse --;
			if ((pAd->Mlme.PeriodicRound%5) == 0)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("RadarDetectPeriodic - ch=%d, RemainingTimeForUse=%d\n", pAd->ChannelList[i].Channel, pAd->ChannelList[i].RemainingTimeForUse));
			}
		}
	}

	//radar detect
	if ((pAd->CommonCfg.Channel > 14)
		&& (pAd->CommonCfg.bIEEE80211H == 1)
		&& RadarChannelCheck(pAd, pAd->CommonCfg.Channel))
	{
		RadarDetectPeriodic(pAd);
	}

	return;
}

// Periodic Radar detection, switch channel will occur in RTMPHandleTBTTInterrupt()
// Before switch channel, driver needs doing channel switch announcement.
VOID RadarDetectPeriodic(
	IN PRTMP_ADAPTER	pAd)
{
	// need to check channel availability, after switch channel
	if (pAd->CommonCfg.RadarDetect.RDMode != RD_SILENCE_MODE)
			return;

	// channel availability check time is 60sec, use 65 for assurance
	if (pAd->CommonCfg.RadarDetect.RDCount++ > pAd->CommonCfg.RadarDetect.ChMovingTime)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("Not found radar signal, start send beacon and radar detection in service monitor\n\n"));
			BbpRadarDetectionStop(pAd);
		AsicEnableBssSync(pAd);
		pAd->CommonCfg.RadarDetect.RDMode = RD_NORMAL_MODE;


		return;
	}

	return;
}


/*
    ==========================================================================
    Description:
		change channel moving time for DFS testing.

	Arguments:
	    pAdapter                    Pointer to our adapter
	    wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
               1.) iwpriv ra0 set ChMovTime=[value]
    ==========================================================================
*/
INT Set_ChMovingTime_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR arg)
{
	UINT8 Value;

	Value = simple_strtol(arg, 0, 10);

	pAd->CommonCfg.RadarDetect.ChMovingTime = Value;

	DBGPRINT(RT_DEBUG_TRACE, ("%s:: %d\n", __FUNCTION__,
		pAd->CommonCfg.RadarDetect.ChMovingTime));

	return TRUE;
}

INT Set_LongPulseRadarTh_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR arg)
{
	UINT8 Value;

	Value = simple_strtol(arg, 0, 10) > 10 ? 10 : simple_strtol(arg, 0, 10);

	pAd->CommonCfg.RadarDetect.LongPulseRadarTh = Value;

	DBGPRINT(RT_DEBUG_TRACE, ("%s:: %d\n", __FUNCTION__,
		pAd->CommonCfg.RadarDetect.LongPulseRadarTh));

	return TRUE;
}


