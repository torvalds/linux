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
    ap_cfg.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    Fonchi    02-13-2007      created
*/

#ifndef _AP_WDS_H_
#define _AP_WDS_H_

#define WDS_ENTRY_RETRY_INTERVAL	(100 * OS_HZ / 1000)


static inline BOOLEAN WDS_IF_UP_CHECK(
	IN  PRTMP_ADAPTER   pAd,
	IN  ULONG ifidx)
{
	if ((pAd->flg_wds_init != TRUE) ||
		(ifidx >= MAX_WDS_ENTRY))
		return FALSE;

//	if (pAd->WdsTab.WdsEntry[ifidx].dev->flags & IFF_UP)
// Patch for wds ,when dirver call apmlmeperiod => APMlmeDynamicTxRateSwitching check if wds device ready
if ((pAd->WdsTab.WdsEntry[ifidx].dev != NULL) && (pAd->WdsTab.WdsEntry[ifidx].dev->flags & IFF_UP))
		return TRUE;

	return FALSE;
}

LONG WdsEntryAlloc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pAddr);

VOID WdsEntryDel(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pAddr);

MAC_TABLE_ENTRY *MacTableInsertWDSEntry(
	IN  PRTMP_ADAPTER   pAd,
	IN  PUCHAR pAddr,
	UINT WdsTabIdx);

BOOLEAN MacTableDeleteWDSEntry(
	IN PRTMP_ADAPTER pAd,
	IN USHORT wcid,
	IN PUCHAR pAddr);


BOOLEAN ApWdsAllowToSendPacket(
	IN RTMP_ADAPTER *pAd,
	IN PNDIS_PACKET pPacket,
	OUT	UCHAR		*pWcid);

MAC_TABLE_ENTRY *WdsTableLookupByWcid(
    IN  PRTMP_ADAPTER   pAd,
	IN UCHAR wcid,
	IN PUCHAR pAddr,
	IN BOOLEAN bResetIdelCount);

MAC_TABLE_ENTRY *WdsTableLookup(
    IN  PRTMP_ADAPTER   pAd,
    IN  PUCHAR          pAddr,
	IN BOOLEAN bResetIdelCount);

MAC_TABLE_ENTRY *FindWdsEntry(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Wcid,
	IN PUCHAR			pAddr,
	IN UINT32			PhyMode);

VOID WdsTableMaintenance(
    IN PRTMP_ADAPTER    pAd);

VOID RT28xx_WDS_Init(
	IN PRTMP_ADAPTER pAd,
	IN PNET_DEV net_dev);

VOID RT28xx_WDS_Close(
	IN PRTMP_ADAPTER pAd);

VOID RT28xx_WDS_Remove(
	IN PRTMP_ADAPTER pAd);

VOID WdsDown(
	IN PRTMP_ADAPTER pAd);

VOID AsicUpdateWdsRxWCIDTable(
	IN PRTMP_ADAPTER pAd);

VOID AsicUpdateWdsEncryption(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR wcid);

VOID WdsPeerBeaconProc(
	IN PRTMP_ADAPTER pAd,
	IN PMAC_TABLE_ENTRY pEntry,
	IN USHORT CapabilityInfo,
	IN UCHAR MaxSupportedRateIn500Kbps,
	IN UCHAR MaxSupportedRateLen,
	IN BOOLEAN bWmmCapable,
	IN ULONG ClientRalinkIe,
	IN HT_CAPABILITY_IE *pHtCapability,
	IN UCHAR HtCapabilityLen);

VOID APWdsInitialize(
	IN PRTMP_ADAPTER pAd);

INT	Show_WdsTable_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

VOID rtmp_read_wds_from_file(
			IN  PRTMP_ADAPTER pAd,
			PSTRING tmpbuf,
			PSTRING buffer);

VOID WdsPrepareWepKeyFromMainBss(
	IN  PRTMP_ADAPTER pAd);

INT WdsVirtualIFSendPackets(
	IN PNDIS_PACKET pSkb,
	IN PNET_DEV dev);

INT WdsVirtualIF_open(
	IN PNET_DEV dev);

INT WdsVirtualIF_close(
	IN PNET_DEV dev);

INT WdsVirtualIF_ioctl(
	IN PNET_DEV net_dev,
	IN OUT struct ifreq *rq,
	IN INT cmd);

/*
	==========================================================================
	Description:
		Check the WDS Entry is valid or not.
	==========================================================================
 */
static inline BOOLEAN ValidWdsEntry(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR WdsIndex)
{
	BOOLEAN result;
	PMAC_TABLE_ENTRY pMacEntry;

	do
	{
		if (WdsIndex >= MAX_WDS_ENTRY)
		{
			result = FALSE;
			break;
		}

		if (pAd->WdsTab.WdsEntry[WdsIndex].Valid != TRUE)
		{
			result = FALSE;
			break;
		}

		if ((pAd->WdsTab.WdsEntry[WdsIndex].MacTabMatchWCID==0)
			|| (pAd->WdsTab.WdsEntry[WdsIndex].MacTabMatchWCID >= MAX_LEN_OF_MAC_TABLE))
		{
			result = FALSE;
			break;
		}

		pMacEntry = &pAd->MacTab.Content[pAd->WdsTab.WdsEntry[WdsIndex].MacTabMatchWCID];
		if (pMacEntry->ValidAsWDS != TRUE)
		{
			result = FALSE;
			break;
		}

		result = TRUE;
	} while(FALSE);

	return result;
}
#endif // _AP_WDS_H_ //
