#ifndef __WINBOND_BSS_F_H
#define __WINBOND_BSS_F_H

#include "adapter.h"

struct PMKID_Information_Element;

//
// BSS descriptor DataBase management global function
//

void vBSSdescriptionInit(struct wb35_adapter * adapter);
void vBSSfoundList(struct wb35_adapter * adapter);
u8 boChanFilter(struct wb35_adapter * adapter, u8 ChanNo);
u16 wBSSallocateEntry(struct wb35_adapter * adapter);
u16 wBSSGetEntry(struct wb35_adapter * adapter);
void vSimpleHouseKeeping(struct wb35_adapter * adapter);
u16 wBSShouseKeeping(struct wb35_adapter * adapter);
void ClearBSSdescpt(struct wb35_adapter * adapter, u16 i);
u16 wBSSfindBssID(struct wb35_adapter * adapter, u8 *pbBssid);
u16 wBSSfindDedicateCandidate(struct wb35_adapter * adapter, struct SSID_Element *psSsid, u8 *pbBssid);
u16 wBSSfindMACaddr(struct wb35_adapter * adapter, u8 *pbMacAddr);
u16 wBSSsearchMACaddr(struct wb35_adapter * adapter, u8 *pbMacAddr, u8 band);
u16 wBSSaddScanData(struct wb35_adapter *, u16, psRXDATA);
u16 wBSSUpdateScanData(struct wb35_adapter * adapter, u16 wBssIdx, psRXDATA psRcvData);
u16 wBSScreateIBSSdata(struct wb35_adapter * adapter, PWB_BSSDESCRIPTION psDesData);
void DesiredRate2BSSdescriptor(struct wb35_adapter * adapter, PWB_BSSDESCRIPTION psDesData,
							 u8 *pBasicRateSet, u8 BasicRateCount,
							 u8 *pOperationRateSet, u8 OperationRateCount);
void DesiredRate2InfoElement(struct wb35_adapter * adapter, u8	*addr, u16 *iFildOffset,
							 u8 *pBasicRateSet, u8 BasicRateCount,
							 u8 *pOperationRateSet, u8 OperationRateCount);
void BSSAddIBSSdata(struct wb35_adapter * adapter, PWB_BSSDESCRIPTION psDesData);
unsigned char boCmpMacAddr( u8 *, u8 *);
unsigned char boCmpSSID(struct SSID_Element *psSSID1, struct SSID_Element *psSSID2);
u16 wBSSfindSSID(struct wb35_adapter * adapter, struct SSID_Element *psSsid);
u16 wRoamingQuery(struct wb35_adapter * adapter);
void vRateToBitmap(struct wb35_adapter * adapter, u16 index);
u8 bRateToBitmapIndex(struct wb35_adapter * adapter, u8 bRate);
u8 bBitmapToRate(u8 i);
unsigned char boIsERPsta(struct wb35_adapter * adapter, u16 i);
unsigned char boCheckConnect(struct wb35_adapter * adapter);
unsigned char boCheckSignal(struct wb35_adapter * adapter);
void AddIBSSIe(struct wb35_adapter * adapter,PWB_BSSDESCRIPTION psDesData );//added by ws for WPA_None06/01/04
void BssScanUpToDate(struct wb35_adapter * adapter);
void BssUpToDate(struct wb35_adapter * adapter);
void RateSort(u8 *RateArray, u8 num, u8 mode);
void RateReSortForSRate(struct wb35_adapter * adapter, u8 *RateArray, u8 num);
void Assemble_IE(struct wb35_adapter * adapter, u16 wBssIdx);
void SetMaxTxRate(struct wb35_adapter * adapter);

void CreateWpaIE(struct wb35_adapter * adapter, u16* iFildOffset, u8 *msg, struct  Management_Frame* msgHeader,
				 struct Association_Request_Frame_Body* msgBody, u16 iMSindex); //added by WS 05/14/05

#ifdef _WPA2_
void CreateRsnIE(struct wb35_adapter * adapter, u16* iFildOffset, u8 *msg, struct  Management_Frame* msgHeader,
				 struct Association_Request_Frame_Body* msgBody, u16 iMSindex);//added by WS 05/14/05

u16 SearchPmkid(struct wb35_adapter * adapter, struct  Management_Frame* msgHeader,
				   struct PMKID_Information_Element * AssoReq_PMKID );
#endif

#endif
