/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _RTL871X_IOCTL_QUERY_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl871x_ioctl_query.h>
#include <wifi.h>


#ifdef PLATFORM_WINDOWS
//
// Added for WPA2-PSK, by Annie, 2005-09-20.
//
u8
query_802_11_capability(
	_adapter*		Adapter,
	u8*			pucBuf,
	u32 *		pulOutLen
)
{
	static NDIS_802_11_AUTHENTICATION_ENCRYPTION szAuthEnc[] = 
	{
		{Ndis802_11AuthModeOpen, Ndis802_11EncryptionDisabled}, 
		{Ndis802_11AuthModeOpen, Ndis802_11Encryption1Enabled},
		{Ndis802_11AuthModeShared, Ndis802_11EncryptionDisabled}, 
		{Ndis802_11AuthModeShared, Ndis802_11Encryption1Enabled},
		{Ndis802_11AuthModeWPA, Ndis802_11Encryption2Enabled}, 
		{Ndis802_11AuthModeWPA, Ndis802_11Encryption3Enabled},
		{Ndis802_11AuthModeWPAPSK, Ndis802_11Encryption2Enabled}, 
		{Ndis802_11AuthModeWPAPSK, Ndis802_11Encryption3Enabled},
		{Ndis802_11AuthModeWPANone, Ndis802_11Encryption2Enabled}, 
		{Ndis802_11AuthModeWPANone, Ndis802_11Encryption3Enabled},
		{Ndis802_11AuthModeWPA2, Ndis802_11Encryption2Enabled}, 
		{Ndis802_11AuthModeWPA2, Ndis802_11Encryption3Enabled},
		{Ndis802_11AuthModeWPA2PSK, Ndis802_11Encryption2Enabled}, 
		{Ndis802_11AuthModeWPA2PSK, Ndis802_11Encryption3Enabled}
	};	
	static ULONG	ulNumOfPairSupported = sizeof(szAuthEnc)/sizeof(NDIS_802_11_AUTHENTICATION_ENCRYPTION);
	NDIS_802_11_CAPABILITY * pCap = (NDIS_802_11_CAPABILITY *)pucBuf;
	u8*	pucAuthEncryptionSupported = (u8*) pCap->AuthenticationEncryptionSupported;


	pCap->Length = sizeof(NDIS_802_11_CAPABILITY);
	if(ulNumOfPairSupported > 1 )
		pCap->Length += 	(ulNumOfPairSupported-1) * sizeof(NDIS_802_11_AUTHENTICATION_ENCRYPTION);
	
	pCap->Version = 2;	
	pCap->NoOfPMKIDs = NUM_PMKID_CACHE;	
	pCap->NoOfAuthEncryptPairsSupported = ulNumOfPairSupported;

	if( sizeof (szAuthEnc) <= 240 )		// 240 = 256 - 4*4	// SecurityInfo.szCapability: only 256 bytes in size.
	{
		_memcpy( pucAuthEncryptionSupported, (u8*)szAuthEnc,  sizeof (szAuthEnc) );
		*pulOutLen = pCap->Length;
		return _TRUE;
	}
	else
	{
		*pulOutLen = 0;
		RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("_query_802_11_capability(): szAuthEnc size is too large.\n"));
		return _FALSE;
	}
}

u8 query_802_11_association_information(	_adapter *padapter,PNDIS_802_11_ASSOCIATION_INFORMATION	pAssocInfo)
{
	struct wlan_network *tgt_network;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct	security_priv  *psecuritypriv=&(padapter->securitypriv);
	NDIS_WLAN_BSSID_EX	*psecnetwork=(NDIS_WLAN_BSSID_EX*)&(psecuritypriv->sec_bss);					
	u8 *	pDest = (u8 *)pAssocInfo + sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);
	unsigned char i,*auth_ie,*supp_ie;

	//NdisZeroMemory(pAssocInfo, sizeof(NDIS_802_11_ASSOCIATION_INFORMATION));
	memset(pAssocInfo, 0, sizeof(NDIS_802_11_ASSOCIATION_INFORMATION));
	//pAssocInfo->Length = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);

	//------------------------------------------------------
	// Association Request related information
	//------------------------------------------------------
	// Req_1. AvailableRequestFixedIEs
	if(psecnetwork!=NULL){
		
	pAssocInfo->AvailableRequestFixedIEs |= NDIS_802_11_AI_REQFI_CAPABILITIES|NDIS_802_11_AI_REQFI_CURRENTAPADDRESS;
	pAssocInfo->RequestFixedIEs.Capabilities = (unsigned short)* & psecnetwork->IEs[10];
	_memcpy(pAssocInfo->RequestFixedIEs.CurrentAPAddress,
		& psecnetwork->MacAddress, 6);

	pAssocInfo->OffsetRequestIEs = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);

	if(check_fwstate( pmlmepriv, _FW_UNDER_LINKING|_FW_LINKED)==_TRUE)
	{
		
		if(psecuritypriv->ndisauthtype>=Ndis802_11AuthModeWPA2)
			pDest[0] =48;		//RSN Information Element
		else 
			pDest[0] =221;	//WPA(SSN) Information Element
		
		RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("\n Adapter->ndisauthtype==Ndis802_11AuthModeWPA)?0xdd:0x30 [%d]",pDest[0]));
		supp_ie=&psecuritypriv->supplicant_ie[0];
		for(i=0;i<supp_ie[0];i++)
		{
			RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("IEs [%d] = 0x%x \n\n", i,supp_ie[i]));
		}

		i=13;	//0~11 is fixed information element		
		RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("i= %d tgt_network->network.IELength=%d\n\n", i,(int)psecnetwork->IELength));
		while((i<supp_ie[0]) && (i<256)){
			if((unsigned char)supp_ie[i]==pDest[0]){
						_memcpy((u8 *)(pDest),
							&supp_ie[i], 
							supp_ie[1+i]+2);
			
				break;
			}
			
			i=i+supp_ie[i+1]+2;
			if(supp_ie[1+i]==0)
				i=i+1;
			RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("iteration i=%d IEs [%d] = 0x%x \n\n", i,i,supp_ie[i+1]));
			
		}
		

		pAssocInfo->RequestIELength += (2 + supp_ie[1+i]);// (2 + psecnetwork->IEs[1+i]+4);

	}
	

		RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("\n psecnetwork != NULL,fwstate==_FW_UNDER_LINKING \n"));

	}
	

	//------------------------------------------------------
	// Association Response related information
	//------------------------------------------------------

	if(check_fwstate( pmlmepriv, _FW_LINKED)==_TRUE)
	{
		tgt_network =&(pmlmepriv->cur_network);
		if(tgt_network!=NULL){
		pAssocInfo->AvailableResponseFixedIEs =
				NDIS_802_11_AI_RESFI_CAPABILITIES
				|NDIS_802_11_AI_RESFI_ASSOCIATIONID
				;

		pAssocInfo->ResponseFixedIEs.Capabilities =(unsigned short)* & tgt_network->network.IEs[10];
		pAssocInfo->ResponseFixedIEs.StatusCode = 0;
		pAssocInfo->ResponseFixedIEs.AssociationId =(unsigned short) tgt_network->aid;

		pDest = (u8 *)pAssocInfo + sizeof(NDIS_802_11_ASSOCIATION_INFORMATION)+pAssocInfo->RequestIELength;
		auth_ie=&psecuritypriv->authenticator_ie[0];

		for(i=0;i<auth_ie[0];i++)
			RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("IEs [%d] = 0x%x \n\n", i,auth_ie[i]));

		i=auth_ie[0]-12;
		if(i>0){
			_memcpy((u8 *)&pDest[0],&auth_ie[1],i);
			pAssocInfo->ResponseIELength =i; 
		}


		pAssocInfo->OffsetResponseIEs = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION) + pAssocInfo->RequestIELength;  


		RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("\n tgt_network != NULL,fwstate==_FW_LINKED \n"));
		}
	}												  	
	RT_TRACE(_module_rtl871x_ioctl_query_c_,_drv_info_,("\n exit query_802_11_association_information \n"));
_func_exit_;

	return _TRUE;
}
#endif

