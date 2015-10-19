/******************************************************************************
 *
 * Copyright(c) 2014 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include <drv_types.h>
#include <hal_data.h>

// A mapping from HalData to ODM.
ODM_BOARD_TYPE_E boardType(u8 InterfaceSel)
{
    ODM_BOARD_TYPE_E        board	= ODM_BOARD_DEFAULT;

#ifdef CONFIG_PCI_HCI
	INTERFACE_SELECT_PCIE   pcie 	= (INTERFACE_SELECT_PCIE)InterfaceSel;
	switch (pcie) 
	{
        case INTF_SEL0_SOLO_MINICARD:       
            board |= ODM_BOARD_MINICARD;
            break;
        case INTF_SEL1_BT_COMBO_MINICARD:   
            board |= ODM_BOARD_BT;
			board |= ODM_BOARD_MINICARD;
            break;
        default:
            board = ODM_BOARD_DEFAULT;
            break;
	}                                

#elif defined(CONFIG_USB_HCI)
	INTERFACE_SELECT_USB    usb 	= (INTERFACE_SELECT_USB)InterfaceSel;
	switch (usb) 
	{
	    case INTF_SEL1_USB_High_Power:      
	        board |= ODM_BOARD_EXT_LNA;
	        board |= ODM_BOARD_EXT_PA;			
	        break;
	    case INTF_SEL2_MINICARD:            
	        board |= ODM_BOARD_MINICARD;
	        break;
	    case INTF_SEL4_USB_Combo:           
	        board |= ODM_BOARD_BT;
	        break;
	    case INTF_SEL5_USB_Combo_MF:        
	        board |= ODM_BOARD_BT;
	        break;
	    case INTF_SEL0_USB: 			
	    case INTF_SEL3_USB_Solo:            			
	    default:
	        board = ODM_BOARD_DEFAULT;
	        break;
	}
	
#endif	
	//DBG_871X("===> boardType(): (pHalData->InterfaceSel, pDM_Odm->BoardType) = (%d, %d)\n", InterfaceSel, board);

	return board;
}

void Init_ODM_ComInfo(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	int i;

	_rtw_memset(pDM_Odm,0,sizeof(*pDM_Odm));

	pDM_Odm->Adapter = adapter;

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_PLATFORM, ODM_CE);

	if (adapter->interface_type == RTW_GSPI)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_INTERFACE, ODM_ITRF_SDIO);
	else
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_INTERFACE, adapter->interface_type);

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_MP_TEST_CHIP, IS_NORMAL_CHIP(pHalData->VersionID));

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_PATCH_ID, pHalData->CustomerID);

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_BWIFI_TEST, adapter->registrypriv.wifi_spec);


	if (pHalData->rf_type == RF_1T1R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_1T1R);
	else if (pHalData->rf_type == RF_1T2R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_1T2R);
	else if (pHalData->rf_type == RF_2T2R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_2T2R);
	else if (pHalData->rf_type == RF_2T2R_GREEN)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_2T2R_GREEN);
	else if (pHalData->rf_type == RF_2T3R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_2T3R);
	else if (pHalData->rf_type == RF_2T4R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_2T4R);
	else if (pHalData->rf_type == RF_3T3R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_3T3R);
	else if (pHalData->rf_type == RF_3T4R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_3T4R);
	else if (pHalData->rf_type == RF_4T4R)
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_4T4R);
	else
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_XTXR);
	

{
	//1 ======= BoardType: ODM_CMNINFO_BOARD_TYPE =======
	u8 odm_board_type = ODM_BOARD_DEFAULT;

	if (pHalData->ExternalLNA_2G != 0) {
		odm_board_type |= ODM_BOARD_EXT_LNA;
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_EXT_LNA, 1);
	}
	if (pHalData->ExternalLNA_5G != 0) {
		odm_board_type |= ODM_BOARD_EXT_LNA_5G;
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_5G_EXT_LNA, 1);
	}
	if (pHalData->ExternalPA_2G != 0) {
		odm_board_type |= ODM_BOARD_EXT_PA;
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_EXT_PA, 1);
	}
	if (pHalData->ExternalPA_5G != 0) {
		odm_board_type |= ODM_BOARD_EXT_PA_5G;
		ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_5G_EXT_PA, 1);
	}
	if (pHalData->EEPROMBluetoothCoexist)
		odm_board_type |= ODM_BOARD_BT;	

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_BOARD_TYPE, odm_board_type);
	//1 ============== End of BoardType ==============
}

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_DOMAIN_CODE_2G, pHalData->Regulation2_4G);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_DOMAIN_CODE_5G, pHalData->Regulation5G);

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_GPA, pHalData->TypeGPA);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_APA, pHalData->TypeAPA);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_GLNA, pHalData->TypeGLNA);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_ALNA, pHalData->TypeALNA);

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RFE_TYPE, pHalData->RFEType);

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_EXT_TRSW, 0);

	/* Pointer reference */
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_TX_UNI, &(dvobj->traffic_stat.tx_bytes));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_RX_UNI, &(dvobj->traffic_stat.rx_bytes));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_WM_MODE, &(pmlmeext->cur_wireless_mode));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BAND, &(pHalData->CurrentBandType));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_FORCED_RATE, &(pHalData->ForcedDataRate));

	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SEC_CHNL_OFFSET, &(pHalData->nCur40MhzPrimeSC));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SEC_MODE, &(adapter->securitypriv.dot11PrivacyAlgrthm));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BW, &(pHalData->CurrentChannelBW));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_CHNL, &( pHalData->CurrentChannel));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_NET_CLOSED, &(adapter->net_closed));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_FORCED_IGI_LB, &(pHalData->u1ForcedIgiLb));

	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SCAN, &(pmlmepriv->bScanInProcess));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_POWER_SAVING, &(pwrctl->bpower_saving));

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
		ODM_CmnInfoPtrArrayHook(pDM_Odm, ODM_CMNINFO_STA_STATUS, i, NULL);

	/* TODO */
	//ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BT_OPERATION, _FALSE);
	//ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BT_DISABLE_EDCA, _FALSE);
}

