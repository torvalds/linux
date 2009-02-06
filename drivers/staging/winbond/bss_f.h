#ifndef __WINBOND_BSS_F_H
#define __WINBOND_BSS_F_H

#include "core.h"

struct PMKID_Information_Element;

//
// BSS descriptor DataBase management global function
//

void vBSSdescriptionInit(struct wbsoft_priv * adapter);
void vBSSfoundList(struct wbsoft_priv * adapter);
u8 boChanFilter(struct wbsoft_priv * adapter, u8 ChanNo);
u16 wBSSallocateEntry(struct wbsoft_priv * adapter);
u16 wBSSGetEntry(struct wbsoft_priv * adapter);
void vSimpleHouseKeeping(struct wbsoft_priv * adapter);
u16 wBSShouseKeeping(struct wbsoft_priv * adapter);
void ClearBSSdescpt(struct wbsoft_priv * adapter, u16 i);
u16 wBSSfindBssID(struct wbsoft_priv * adapter, u8 *pbBssid);
u16 wBSSfindDedicateCandidate(struct wbsoft_priv * adapter, struct SSID_Element *psSsid, u8 *pbBssid);
u16 wBSSfindMACaddr(struct wbsoft_priv * adapter, u8 *pbMacAddr);
u16 wBSSsearchMACaddr(struct wbsoft_priv * adapter, u8 *pbMacAddr, u8 band);
u16 wBSSaddScanData(struct wbsoft_priv *, u16, psRXDATA);
u16 wBSSUpdateScanData(struct wbsoft_priv * adapter, u16 wBssIdx, psRXDATA psRcvData);
u16 wBSScreateIBSSdata(struct wbsoft_priv * adapter, PWB_BSSDESCRIPTION psDesData);
void DesiredRate2BSSdescriptor(struct wbsoft_priv * adapter, PWB_BSSDESCRIPTION psDesData,
							 u8 *pBasicRateSet, u8 BasicRateCount,
							 u8 *pOperationRateSet, u8 OperationRateCount);
void DesiredRate2InfoElement(struct wbsoft_priv * adapter, u8	*addr, u16 *iFildOffset,
							 u8 *pBasicRateSet, u8 BasicRateCount,
							 u8 *pOperationRateSet, u8 OperationRateCount);
void BSSAddIBSSdata(struct wbsoft_priv * adapter, PWB_BSSDESCRIPTION psDesData);
unsigned char boCmpMacAddr( u8 *, u8 *);
unsigned char boCmpSSID(struct SSID_Element *psSSID1, struct SSID_Element *psSSID2);
u16 wBSSfindSSID(struct wbsoft_priv * adapter, struct SSID_Element *psSsid);
u16 wRoamingQuery(struct wbsoft_priv * adapter);
void vRateToBitmap(struct wbsoft_priv * adapter, u16 index);
u8 bRateToBitmapIndex(struct wbsoft_priv * adapter, u8 bRate);
u8 bBitmapToRate(u8 i);
unsigned char boIsERPsta(struct wbsoft_priv * adapter, u16 i);
unsigned char boCheckConnect(struct wbsoft_priv * adapter);
unsigned char boCheckSignal(struct wbsoft_priv * adapter);
void AddIBSSIe(struct wbsoft_priv * adapter,PWB_BSSDESCRIPTION psDesData );//added by ws for WPA_None06/01/04
void BssScanUpToDate(struct wbsoft_priv * adapter);
void BssUpToDate(struct wbsoft_priv * adapter);
void RateSort(u8 *RateArray, u8 num, u8 mode);
void RateReSortForSRate(struct wbsoft_priv * adapter, u8 *RateArray, u8 num);
void Assemble_IE(struct wbsoft_priv * adapter, u16 wBssIdx);
void SetMaxTxRate(struct wbsoft_priv * adapter);

void CreateWpaIE(struct wbsoft_priv * adapter, u16* iFildOffset, u8 *msg, struct  Management_Frame* msgHeader,
				 struct Association_Request_Frame_Body* msgBody, u16 iMSindex); //added by WS 05/14/05

#ifdef _WPA2_
void CreateRsnIE(struct wbsoft_priv * adapter, u16* iFildOffset, u8 *msg, struct  Management_Frame* msgHeader,
				 struct Association_Request_Frame_Body* msgBody, u16 iMSindex);//added by WS 05/14/05

u16 SearchPmkid(struct wbsoft_priv * adapter, struct  Management_Frame* msgHeader,
				   struct PMKID_Information_Element * AssoReq_PMKID );
#endif

#endif
