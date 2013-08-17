/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 		Samsung Electronics System LSI. modify
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
******************************************************************************/

#if !defined(__OEMFUNCS_H__)
#define __OEMFUNCS_H__

#if defined (__cplusplus)
extern "C" {
#endif


#define OEM_EXCHANGE_POWER_STATE	(1<<0)
#define OEM_DEVICE_MEMORY_POWER		(1<<1)
#define OEM_DISPLAY_POWER			(1<<2)
#define OEM_GET_EXT_FUNCS			(1<<3)
#define OEM_COMPLETE_PREPARE_DISPLAY		(1<<4)

typedef struct OEM_ACCESS_INFO_TAG
{
	IMG_UINT32		ui32Size;
	IMG_UINT32  	ui32FBPhysBaseAddress;
	IMG_UINT32		ui32FBMemAvailable;	
	IMG_UINT32  	ui32SysPhysBaseAddress;
	IMG_UINT32		ui32SysSize;
	IMG_UINT32		ui32DevIRQ;
} OEM_ACCESS_INFO, *POEM_ACCESS_INFO; 
 

typedef IMG_UINT32   (*PFN_SRV_BRIDGEDISPATCH)( IMG_UINT32  Ioctl,
												IMG_BYTE   *pInBuf,
												IMG_UINT32  InBufLen, 
											    IMG_BYTE   *pOutBuf,
												IMG_UINT32  OutBufLen,
												IMG_UINT32 *pdwBytesTransferred);


typedef PVRSRV_ERROR (*PFN_SRV_READREGSTRING)(PPVRSRV_REGISTRY_INFO psRegInfo);


typedef struct PVRSRV_DC_OEM_JTABLE_TAG
{
	PFN_SRV_BRIDGEDISPATCH			pfnOEMBridgeDispatch;
	PFN_SRV_READREGSTRING			pfnOEMReadRegistryString;
	PFN_SRV_READREGSTRING			pfnOEMWriteRegistryString;

} PVRSRV_DC_OEM_JTABLE;
#if defined(__cplusplus)
}
#endif

#endif	
