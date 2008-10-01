//
// BSS descriptor DataBase management global function
//

void vBSSdescriptionInit(PWB32_ADAPTER Adapter);
void vBSSfoundList(PWB32_ADAPTER Adapter);
u8 boChanFilter(PWB32_ADAPTER Adapter, u8 ChanNo);
u16 wBSSallocateEntry(PWB32_ADAPTER Adapter);
u16 wBSSGetEntry(PWB32_ADAPTER Adapter);
void vSimpleHouseKeeping(PWB32_ADAPTER Adapter);
u16 wBSShouseKeeping(PWB32_ADAPTER Adapter);
void ClearBSSdescpt(PWB32_ADAPTER Adapter, u16 i);
u16 wBSSfindBssID(PWB32_ADAPTER Adapter, u8 *pbBssid);
u16 wBSSfindDedicateCandidate(PWB32_ADAPTER Adapter, struct SSID_Element *psSsid, u8 *pbBssid);
u16 wBSSfindMACaddr(PWB32_ADAPTER Adapter, u8 *pbMacAddr);
u16 wBSSsearchMACaddr(PWB32_ADAPTER Adapter, u8 *pbMacAddr, u8 band);
u16 wBSSaddScanData(PWB32_ADAPTER, u16, psRXDATA);
u16 wBSSUpdateScanData(PWB32_ADAPTER Adapter, u16 wBssIdx, psRXDATA psRcvData);
u16 wBSScreateIBSSdata(PWB32_ADAPTER Adapter, PWB_BSSDESCRIPTION psDesData);
void DesiredRate2BSSdescriptor(PWB32_ADAPTER Adapter, PWB_BSSDESCRIPTION psDesData,
							 u8 *pBasicRateSet, u8 BasicRateCount,
							 u8 *pOperationRateSet, u8 OperationRateCount);
void DesiredRate2InfoElement(PWB32_ADAPTER Adapter, u8	*addr, u16 *iFildOffset,
							 u8 *pBasicRateSet, u8 BasicRateCount,
							 u8 *pOperationRateSet, u8 OperationRateCount);
void BSSAddIBSSdata(PWB32_ADAPTER Adapter, PWB_BSSDESCRIPTION psDesData);
unsigned char boCmpMacAddr( PUCHAR, PUCHAR );
unsigned char boCmpSSID(struct SSID_Element *psSSID1, struct SSID_Element *psSSID2);
u16 wBSSfindSSID(PWB32_ADAPTER Adapter, struct SSID_Element *psSsid);
u16 wRoamingQuery(PWB32_ADAPTER Adapter);
void vRateToBitmap(PWB32_ADAPTER Adapter, u16 index);
u8 bRateToBitmapIndex(PWB32_ADAPTER Adapter, u8 bRate);
u8 bBitmapToRate(u8 i);
unsigned char boIsERPsta(PWB32_ADAPTER Adapter, u16 i);
unsigned char boCheckConnect(PWB32_ADAPTER Adapter);
unsigned char boCheckSignal(PWB32_ADAPTER Adapter);
void AddIBSSIe(PWB32_ADAPTER Adapter,PWB_BSSDESCRIPTION psDesData );//added by ws for WPA_None06/01/04
void BssScanUpToDate(PWB32_ADAPTER Adapter);
void BssUpToDate(PWB32_ADAPTER Adapter);
void RateSort(u8 *RateArray, u8 num, u8 mode);
void RateReSortForSRate(PWB32_ADAPTER Adapter, u8 *RateArray, u8 num);
void Assemble_IE(PWB32_ADAPTER Adapter, u16 wBssIdx);
void SetMaxTxRate(PWB32_ADAPTER Adapter);

void CreateWpaIE(PWB32_ADAPTER Adapter, u16* iFildOffset, PUCHAR msg, struct  Management_Frame* msgHeader,
				 struct Association_Request_Frame_Body* msgBody, u16 iMSindex); //added by WS 05/14/05

#ifdef _WPA2_
void CreateRsnIE(PWB32_ADAPTER Adapter, u16* iFildOffset, PUCHAR msg, struct  Management_Frame* msgHeader,
				 struct Association_Request_Frame_Body* msgBody, u16 iMSindex);//added by WS 05/14/05

u16 SearchPmkid(PWB32_ADAPTER Adapter, struct  Management_Frame* msgHeader,
				   struct PMKID_Information_Element * AssoReq_PMKID );
#endif





