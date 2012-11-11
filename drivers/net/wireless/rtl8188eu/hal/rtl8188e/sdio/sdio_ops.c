/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 *******************************************************************************/
#define _SDIO_OPS_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>
#include <rtl8188e_spec.h>

//#define SDIO_DEBUG_IO 1

#define SDIO_ERR_VAL8	0xEA
#define SDIO_ERR_VAL16	0xEAEA
#define SDIO_ERR_VAL32	0xEAEAEAEA

extern u8 sd_f0_read8(PSDIO_DATA psdio, u32 addr, s32 *err);
extern void sd_f0_write8(PSDIO_DATA psdio, u32 addr, s32 *err);

extern s32 _sd_cmd52_read(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);
extern s32 _sd_cmd52_write(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);
extern s32 sd_cmd52_read(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);
extern s32 sd_cmd52_write(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);

extern u8 sd_read8(PSDIO_DATA psdio, u32 addr, s32 *err);
extern u16 sd_read16(PSDIO_DATA psdio, u32 addr, s32 *err);
extern u32 sd_read32(PSDIO_DATA psdio, u32 addr, s32 *err);
extern s32 _sd_read(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);
extern s32 sd_read(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);
extern void sd_write8(PSDIO_DATA psdio, u32 addr, u8 v, s32 *err);
extern void sd_write16(PSDIO_DATA psdio, u32 addr, u16 v, s32 *err);
extern void sd_write32(PSDIO_DATA psdio, u32 addr, u32 v, s32 *err);
extern s32 _sd_write(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);
extern s32 sd_write(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata);

//
// Description:
//	The following mapping is for SDIO host local register space.
//
// Creadted by Roger, 2011.01.31.
//
static void HalSdioGetCmdAddr8723ASdio(
	IN	PADAPTER			padapter,
	IN 	u8				DeviceID,
	IN	u32				Addr,
	OUT	u32*				pCmdAddr
	)
{
	switch (DeviceID)
	{
		case SDIO_LOCAL_DEVICE_ID:
			*pCmdAddr = ((SDIO_LOCAL_DEVICE_ID << 13) | (Addr & SDIO_LOCAL_MSK));
			break;

		case WLAN_IOREG_DEVICE_ID:
			*pCmdAddr = ((WLAN_IOREG_DEVICE_ID << 13) | (Addr & WLAN_IOREG_MSK));
			break;

		case WLAN_TX_HIQ_DEVICE_ID:
			*pCmdAddr = ((WLAN_TX_HIQ_DEVICE_ID << 13) | (Addr & WLAN_FIFO_MSK));
			break;

		case WLAN_TX_MIQ_DEVICE_ID:
			*pCmdAddr = ((WLAN_TX_MIQ_DEVICE_ID << 13) | (Addr & WLAN_FIFO_MSK));
			break;

		case WLAN_TX_LOQ_DEVICE_ID:
			*pCmdAddr = ((WLAN_TX_LOQ_DEVICE_ID << 13) | (Addr & WLAN_FIFO_MSK));
			break;

		case WLAN_RX0FF_DEVICE_ID:
			*pCmdAddr = ((WLAN_RX0FF_DEVICE_ID << 13) | (Addr & WLAN_RX0FF_MSK));
			break;

		default:
			break;
	}
}

static u8 get_deviceid(u32 addr)
{
	u8 devideId;
	u16 pseudoId;


	pseudoId = (u16)(addr >> 16);
	switch (pseudoId)
	{
		case 0x1025:
			devideId = SDIO_LOCAL_DEVICE_ID;
			break;

		case 0x1026:
			devideId = WLAN_IOREG_DEVICE_ID;
			break;

//		case 0x1027:
//			devideId = SDIO_FIRMWARE_FIFO;
//			break;

		case 0x1031:
			devideId = WLAN_TX_HIQ_DEVICE_ID;
			break;

		case 0x1032:
			devideId = WLAN_TX_MIQ_DEVICE_ID;
			break;

		case 0x1033:
			devideId = WLAN_TX_LOQ_DEVICE_ID;
			break;

		case 0x1034:
			devideId = WLAN_RX0FF_DEVICE_ID;
			break;

		default:
//			devideId = (u8)((addr >> 13) & 0xF);
			devideId = WLAN_IOREG_DEVICE_ID;
			break;
	}

	return devideId;
}

/*
 * Ref:
 *	HalSdioGetCmdAddr8723ASdio()
 */
static u32 _cvrt2ftaddr(const u32 addr, u8 *pdeviceId, u16 *poffset)
{
	u8 deviceId;
	u16 offset;
	u32 ftaddr;


	deviceId = get_deviceid(addr);
	offset = 0;

	switch (deviceId)
	{
		case SDIO_LOCAL_DEVICE_ID:
			offset = addr & SDIO_LOCAL_MSK;
			break;

		case WLAN_TX_HIQ_DEVICE_ID:
		case WLAN_TX_MIQ_DEVICE_ID:
		case WLAN_TX_LOQ_DEVICE_ID:
			offset = addr & WLAN_FIFO_MSK;
			break;

		case WLAN_RX0FF_DEVICE_ID:
			offset = addr & WLAN_RX0FF_MSK;
			break;

		case WLAN_IOREG_DEVICE_ID:
		default:
			deviceId = WLAN_IOREG_DEVICE_ID;
			offset = addr & WLAN_IOREG_MSK;
			break;
	}
	ftaddr = (deviceId << 13) | offset;

	if (pdeviceId) *pdeviceId = deviceId;
	if (poffset) *poffset = offset;

	return ftaddr;
}

u8 sdio_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u32 ftaddr;
	u8 val;

_func_enter_;

	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	val = sd_read8(psdio, ftaddr, NULL);

_func_exit_;

	return val;
}

u16 sdio_read16(struct intf_hdl *pintfhdl, u32 addr)
{
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u32 ftaddr;
	u16 val;

_func_enter_;

	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	sd_cmd52_read(psdio, ftaddr, 2, (u8*)&val);	
	val = le16_to_cpu(val);

_func_exit_;

	return val;
}

u32 sdio_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	u32 val;
	s32 err;

_func_enter_;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

        rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100))
		|| (_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
        {
		err = sd_cmd52_read(psdio, ftaddr, 4, (u8*)&val);
#ifdef SDIO_DEBUG_IO
		if (!err) {
#endif
			val = le32_to_cpu(val);
			return val;
#ifdef SDIO_DEBUG_IO
		}

		DBG_871X(KERN_ERR "%s: Mac Power off, Read FAIL(%d)! addr=0x%x\n", __func__, err, addr);
		return SDIO_ERR_VAL32;
#endif
	}

	// 4 bytes alignment
	shift = ftaddr & 0x3;
	if (shift == 0) {
		val = sd_read32(psdio, ftaddr, NULL);
	} else {
		u8 *ptmpbuf;
				
		ptmpbuf = (u8*)rtw_malloc(8);
		if (NULL == ptmpbuf) {
			DBG_871X(KERN_ERR "%s: Allocate memory FAIL!(size=8) addr=0x%x\n", __func__, addr);
			return SDIO_ERR_VAL32;
		}

		ftaddr &= ~(u16)0x3;
		sd_read(psdio, ftaddr, 8, ptmpbuf);
		_rtw_memcpy(&val, ptmpbuf+shift, 4);
		val = le32_to_cpu(val);

		rtw_mfree(ptmpbuf, 8);
	}

_func_exit_;

	return val;
}

s32 sdio_readN(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8* pbuf)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

_func_enter_;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100))
		|| (_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		err = sd_cmd52_read(psdio, ftaddr, cnt, pbuf);
		return err;
	}

	// 4 bytes alignment
	shift = ftaddr & 0x3;
	if (shift == 0) {
		err = sd_read(psdio, ftaddr, cnt, pbuf);
	} else {
		u8 *ptmpbuf;
		u32 n;

		ftaddr &= ~(u16)0x3;
		n = cnt + shift;
		ptmpbuf = rtw_malloc(n);
		if (NULL == ptmpbuf) return -1;
		err = sd_read(psdio, ftaddr, n, ptmpbuf);
		if (!err)
			_rtw_memcpy(pbuf, ptmpbuf+shift, cnt);
		rtw_mfree(ptmpbuf, n);
	}

_func_exit_;

	return err;
}

s32 sdio_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u32 ftaddr;
	s32 err;

_func_enter_;

	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	sd_write8(psdio, ftaddr, val, &err);

_func_exit_;

	return err;
}

s32 sdio_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u32 ftaddr;
	u8 shift;
	s32 err;

_func_enter_;

	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	val = cpu_to_le16(val);
	err = sd_cmd52_write(psdio, ftaddr, 2, (u8*)&val);

_func_exit_;

	return err;
}

s32 sdio_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

_func_enter_;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100))
		|| (_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		val = cpu_to_le32(val);
		err = sd_cmd52_write(psdio, ftaddr, 4, (u8*)&val);
		return err;
	}

	// 4 bytes alignment
	shift = ftaddr & 0x3;
#if 1
	if (shift == 0)
	{
		sd_write32(psdio, ftaddr, val, &err);
	}
	else
	{
		val = cpu_to_le32(val);
		err = sd_cmd52_write(psdio, ftaddr, 4, (u8*)&val);
	}
#else
	if (shift == 0) {	
		sd_write32(psdio, ftaddr, val, &err);
	} else {
		u8 *ptmpbuf;

		ptmpbuf = (u8*)rtw_malloc(8);
		if (NULL == ptmpbuf) return (-1);

		ftaddr &= ~(u16)0x3;
		err = sd_read(psdio, ftaddr, 8, ptmpbuf);
		if (err) {
			_rtw_mfree(ptmpbuf, 8);
			return err;
		}
		val = cpu_to_le32(val);
		_rtw_memcpy(ptmpbuf+shift, &val, 4);
		err = sd_write(psdio, ftaddr, 8, ptmpbuf);
		
		rtw_mfree(ptmpbuf, 8);
	}
#endif	

_func_exit_;

	return err;
}

s32 sdio_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8* pbuf)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

_func_enter_;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100))
		|| (_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		err = sd_cmd52_write(psdio, ftaddr, cnt, pbuf);
		return err;
	}

	shift = ftaddr & 0x3;
	if (shift == 0) {
		err = sd_write(psdio, ftaddr, cnt, pbuf);
	} else {
		u8 *ptmpbuf;
		u32 n;

		ftaddr &= ~(u16)0x3;
		n = cnt + shift;
		ptmpbuf = rtw_malloc(n);
		if (NULL == ptmpbuf) return -1;
		err = sd_read(psdio, ftaddr, 4, ptmpbuf);
		if (err) {
			rtw_mfree(ptmpbuf, n);
			return err;
		}
		_rtw_memcpy(ptmpbuf+shift, pbuf, cnt);
		err = sd_write(psdio, ftaddr, n, ptmpbuf);
		rtw_mfree(ptmpbuf, n);
	}

_func_exit_;

	return err;
}

void sdio_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	s32 err;

_func_enter_;

	err = sdio_readN(pintfhdl, addr, cnt, rmem);

_func_exit_;
}

void sdio_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
_func_enter_;

	sdio_writeN(pintfhdl, addr, cnt, wmem);

_func_exit_;
}

/*
 * Description:
 *	Read from RX FIFO
 *	Round read size to block size,
 *	and make sure data transfer will be done in one command.
 *
 * Parameters:
 *	pintfhdl	a pointer of intf_hdl
 *	addr		port ID
 *	cnt			size to read
 *	rmem		address to put data
 *
 * Return:
 *	_SUCCESS(1)		Success
 *	_FAIL(0)		Fail
 */
static u32 sdio_read_port(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem)
{
	PADAPTER padapter = pintfhdl->padapter;
	PSDIO_DATA psdio= &padapter->dvobjpriv.intf_data;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	s32 err;

	HalSdioGetCmdAddr8723ASdio(padapter, addr, pHalData->SdioRxFIFOCnt++, &addr);


	cnt = _RND4(cnt);
	if (cnt > psdio->block_transfer_len)
		cnt = _RND(cnt, psdio->block_transfer_len);
	
//	cnt = sdio_align_size(cnt);

	err = _sd_read(psdio, addr, cnt, mem);
	//err = sd_read(psdio, addr, cnt, mem);
	


	if (err) return _FAIL;
	return _SUCCESS;
}

/*
 * Description:
 *	Write to TX FIFO
 *	Align write size block size,
 *	and make sure data could be written in one command.
 *
 * Parameters:
 *	pintfhdl	a pointer of intf_hdl
 *	addr		port ID
 *	cnt			size to write
 *	wmem		data pointer to write
 *
 * Return:
 *	_SUCCESS(1)		Success
 *	_FAIL(0)		Fail
 */
static u32 sdio_write_port(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem)
{
	PADAPTER padapter;
	PSDIO_DATA psdio;
	s32 err;


	padapter = pintfhdl->padapter;
	psdio = &padapter->dvobjpriv.intf_data;

	cnt = _RND4(cnt);
	HalSdioGetCmdAddr8723ASdio(padapter, addr, cnt >> 2, &addr);

	if (cnt > psdio->block_transfer_len)
		cnt = _RND(cnt, psdio->block_transfer_len);
//	cnt = sdio_align_size(cnt);

	err = sd_write(psdio, addr, cnt, mem);

	if (err) return _FAIL;
	return _SUCCESS;
}

void sdio_set_intf_ops(struct _io_ops *pops)
{
_func_enter_;

	pops->_read8 = &sdio_read8;
	pops->_read16 = &sdio_read16;
	pops->_read32 = &sdio_read32;
	pops->_read_mem = &sdio_read_mem;
	pops->_read_port = &sdio_read_port;

	pops->_write8 = &sdio_write8;
	pops->_write16 = &sdio_write16;
	pops->_write32 = &sdio_write32;
	pops->_writeN = &sdio_writeN;
	pops->_write_mem = &sdio_write_mem;
	pops->_write_port = &sdio_write_port;

_func_exit_;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 _sdio_local_read(
	PADAPTER	padapter,
	u32		addr,
	u32		cnt,
	u8		*pbuf)
{
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;
	u32 n;


	psdio = &padapter->dvobjpriv.intf_data;

	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if ((_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
//		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		err = _sd_cmd52_read(psdio, addr, cnt, pbuf);
		return err;
	}

        n = RND4(cnt);
	ptmpbuf = (u8*)rtw_malloc(n);
	if(!ptmpbuf)
		return (-1);

	err = _sd_read(psdio, addr, n, ptmpbuf);
	if (!err)
		_rtw_memcpy(pbuf, ptmpbuf, cnt);

	if(ptmpbuf)
		rtw_mfree(ptmpbuf, n);	

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 sdio_local_read(
	PADAPTER	padapter,
	u32		addr,
	u32		cnt,
	u8		*pbuf)
{
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;
	u32 n;


	psdio = &padapter->dvobjpriv.intf_data;

	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if ((_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		err = sd_cmd52_read(psdio, addr, cnt, pbuf);
		return err;
	}

        n = RND4(cnt);
	ptmpbuf = (u8*)rtw_malloc(n);
	if(!ptmpbuf)
		return (-1);

	err = sd_read(psdio, addr, n, ptmpbuf);
	if (!err)
		_rtw_memcpy(pbuf, ptmpbuf, cnt);

	if(ptmpbuf)
		rtw_mfree(ptmpbuf, n);	

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 _sdio_local_write(
	PADAPTER	padapter,
	u32		addr,
	u32		cnt,
	u8		*pbuf)
{
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;


#ifdef CONFIG_DEBUG_RTL819X
	if(addr & 0x3)
		DBG_8192C("%s, address must be 4 bytes alignment\n", __FUNCTION__);

	if(cnt  & 0x3)
		DBG_8192C("%s, size must be the multiple of 4 \n", __FUNCTION__);
#endif

	psdio = &padapter->dvobjpriv.intf_data;

	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if ((_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
//		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		err = _sd_cmd52_write(psdio, addr, cnt, pbuf);
		return err;
	}

        ptmpbuf = (u8*)rtw_malloc(cnt);
	if(!ptmpbuf)
		return (-1);

	_rtw_memcpy(ptmpbuf, pbuf, cnt);

	err = _sd_write(psdio, addr, cnt, ptmpbuf);
	
	if (ptmpbuf)
		rtw_mfree(ptmpbuf, cnt);

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 sdio_local_write(
	PADAPTER	padapter,
	u32		addr,
	u32		cnt,
	u8		*pbuf)
{
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;


#ifdef CONFIG_DEBUG_RTL819X
	if(addr & 0x3)
		DBG_8192C("%s, address must be 4 bytes alignment\n", __FUNCTION__);

	if(cnt  & 0x3)
		DBG_8192C("%s, size must be the multiple of 4 \n", __FUNCTION__);
#endif

	psdio = &padapter->dvobjpriv.intf_data;

	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if ((_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		err = sd_cmd52_write(psdio, addr, cnt, pbuf);
		return err;
	}

        ptmpbuf = (u8*)rtw_malloc(cnt);
	if(!ptmpbuf)
		return (-1);

	_rtw_memcpy(ptmpbuf, pbuf, cnt);

	err = sd_write(psdio, addr, cnt, ptmpbuf);

	if (ptmpbuf)
		rtw_mfree(ptmpbuf, cnt);

	return err;
}

u8 SdioLocalCmd52Read1Byte(PADAPTER padapter, u32 addr)
{
	PSDIO_DATA psdio;
	u8 val = 0;


	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(psdio, addr, 1, &val);

	return val;
}

u16 SdioLocalCmd52Read2Byte(PADAPTER padapter, u32 addr)
{
	PSDIO_DATA psdio;
	u16 val = 0;


	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(psdio, addr, 2, (u8*)&val);

	val = le16_to_cpu(val);

	return val;
}

u32 SdioLocalCmd52Read4Byte(PADAPTER padapter, u32 addr)
{
	PSDIO_DATA psdio;
	u32 val = 0;


	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(psdio, addr, 4, (u8*)&val);

	val = le32_to_cpu(val);

	return val;
}

u32 SdioLocalCmd53Read4Byte(PADAPTER padapter, u32 addr)
{
	PSDIO_DATA psdio;
	u8 bMacPwrCtrlOn;
	u32 val;


	val = 0;
	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if ((_FALSE == bMacPwrCtrlOn)
#ifdef CONFIG_LPS_LCLK
		|| (_TRUE == padapter->pwrctrlpriv.bFwCurrentInPSMode)
#endif
		)
	{
		sd_cmd52_read(psdio, addr, 4, (u8*)&val);
		val = le32_to_cpu(val);
	}
	else
	val = sd_read32(psdio, addr, NULL);

	return val;
}

void SdioLocalCmd52Write1Byte(PADAPTER padapter, u32 addr, u8 v)
{
	PSDIO_DATA psdio;


	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_write(psdio, addr, 1, &v);
}

void SdioLocalCmd52Write2Byte(PADAPTER padapter, u32 addr, u16 v)
{
	PSDIO_DATA psdio;


	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	v = cpu_to_le16(v);
	sd_cmd52_write(psdio, addr, 2, (u8*)&v);
}

void SdioLocalCmd52Write4Byte(PADAPTER padapter, u32 addr, u32 v)
{
	PSDIO_DATA psdio;


	psdio = &padapter->dvobjpriv.intf_data;
	HalSdioGetCmdAddr8723ASdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	v = cpu_to_le32(v);
	sd_cmd52_write(psdio, addr, 4, (u8*)&v);
}

#if 0
void
DumpLoggedInterruptHistory8723Sdio(
	PADAPTER		padapter
)
{
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(padapter);
	u4Byte				DebugLevel = DBG_LOUD;

	if (DBG_Var.DbgPrintIsr == 0)
		return;

	DBG_ChkDrvResource(padapter);


	if(pHalData->InterruptLog.nISR_RX_REQUEST)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# RX_REQUEST[%ld]\t\n", pHalData->InterruptLog.nISR_RX_REQUEST));

	if(pHalData->InterruptLog.nISR_AVAL)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# AVAL[%ld]\t\n", pHalData->InterruptLog.nISR_AVAL));

	if(pHalData->InterruptLog.nISR_TXERR)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# TXERR[%ld]\t\n", pHalData->InterruptLog.nISR_TXERR));

	if(pHalData->InterruptLog.nISR_RXERR)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# RXERR[%ld]\t\n", pHalData->InterruptLog.nISR_RXERR));

	if(pHalData->InterruptLog.nISR_TXFOVW)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# TXFOVW[%ld]\t\n", pHalData->InterruptLog.nISR_TXFOVW));

	if(pHalData->InterruptLog.nISR_RXFOVW)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# RXFOVW[%ld]\t\n", pHalData->InterruptLog.nISR_RXFOVW));

	if(pHalData->InterruptLog.nISR_TXBCNOK)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# TXBCNOK[%ld]\t\n", pHalData->InterruptLog.nISR_TXBCNOK));

	if(pHalData->InterruptLog.nISR_TXBCNERR)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# TXBCNERR[%ld]\t\n", pHalData->InterruptLog.nISR_TXBCNERR));

	if(pHalData->InterruptLog.nISR_BCNERLY_INT)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# BCNERLY_INT[%ld]\t\n", pHalData->InterruptLog.nISR_BCNERLY_INT));

	if(pHalData->InterruptLog.nISR_C2HCMD)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# C2HCMD[%ld]\t\n", pHalData->InterruptLog.nISR_C2HCMD));

	if(pHalData->InterruptLog.nISR_CPWM1)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# CPWM1L[%ld]\t\n", pHalData->InterruptLog.nISR_CPWM1));

	if(pHalData->InterruptLog.nISR_CPWM2)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# CPWM2[%ld]\t\n", pHalData->InterruptLog.nISR_CPWM2));

	if(pHalData->InterruptLog.nISR_HSISR_IND)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# HSISR_IND[%ld]\t\n", pHalData->InterruptLog.nISR_HSISR_IND));

	if(pHalData->InterruptLog.nISR_GTINT3_IND)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# GTINT3_IND[%ld]\t\n", pHalData->InterruptLog.nISR_GTINT3_IND));

	if(pHalData->InterruptLog.nISR_GTINT4_IND)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# GTINT4_IND[%ld]\t\n", pHalData->InterruptLog.nISR_GTINT4_IND));

	if(pHalData->InterruptLog.nISR_PSTIMEOUT)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# PSTIMEOUT[%ld]\t\n", pHalData->InterruptLog.nISR_PSTIMEOUT));

	if(pHalData->InterruptLog.nISR_OCPINT)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# OCPINT[%ld]\t\n", pHalData->InterruptLog.nISR_OCPINT));

	if(pHalData->InterruptLog.nISR_ATIMEND)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# ATIMEND[%ld]\t\n", pHalData->InterruptLog.nISR_ATIMEND));

	if(pHalData->InterruptLog.nISR_ATIMEND_E)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# ATIMEND_E[%ld]\t\n", pHalData->InterruptLog.nISR_ATIMEND_E));

	if(pHalData->InterruptLog.nISR_CTWEND)
		RT_TRACE(COMP_SEND|COMP_RECV, DebugLevel, ("# CTWEND[%ld]\t\n", pHalData->InterruptLog.nISR_CTWEND));
}

void
LogInterruptHistory8723Sdio(
	PADAPTER			padapter,
	PRT_ISR_CONTENT	pIsrContent
)
{
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(padapter);

	if((pHalData->IntrMask[0] & SDIO_HIMR_RX_REQUEST_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_RX_REQUEST))
		pHalData->InterruptLog.nISR_RX_REQUEST ++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_AVAL_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_AVAL))
		pHalData->InterruptLog.nISR_AVAL++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_TXERR_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_TXERR))
		pHalData->InterruptLog.nISR_TXERR++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_RXERR_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_RXERR))
		pHalData->InterruptLog.nISR_RXERR++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_TXFOVW_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_TXFOVW))
		pHalData->InterruptLog.nISR_TXFOVW++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_RXFOVW_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_RXFOVW))
		pHalData->InterruptLog.nISR_RXFOVW++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_TXBCNOK_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_TXBCNOK))
		pHalData->InterruptLog.nISR_TXBCNOK++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_TXBCNERR_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_TXBCNERR))
		pHalData->InterruptLog.nISR_TXBCNERR++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_BCNERLY_INT_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_BCNERLY_INT))
		pHalData->InterruptLog.nISR_BCNERLY_INT ++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_C2HCMD_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_C2HCMD))
		pHalData->InterruptLog.nISR_C2HCMD++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_CPWM1_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_CPWM1))
		pHalData->InterruptLog.nISR_CPWM1++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_CPWM2_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_CPWM2))
		pHalData->InterruptLog.nISR_CPWM2++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_HSISR_IND_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_HSISR_IND))
		pHalData->InterruptLog.nISR_HSISR_IND++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_GTINT3_IND_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_GTINT3_IND))
		pHalData->InterruptLog.nISR_GTINT3_IND++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_GTINT4_IND_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_GTINT4_IND))
		pHalData->InterruptLog.nISR_GTINT4_IND++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_PSTIMEOUT_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_PSTIMEOUT))
		pHalData->InterruptLog.nISR_PSTIMEOUT++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_OCPINT_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_OCPINT))
		pHalData->InterruptLog.nISR_OCPINT++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_ATIMEND_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_ATIMEND))
		pHalData->InterruptLog.nISR_ATIMEND++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_ATIMEND_E_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_ATIMEND_E))
		pHalData->InterruptLog.nISR_ATIMEND_E++;
	if((pHalData->IntrMask[0] & SDIO_HIMR_CTWEND_MSK) &&
		(pIsrContent->IntArray[0] & SDIO_HISR_CTWEND))
		pHalData->InterruptLog.nISR_CTWEND++;

}

void
DumpHardwareProfile8723Sdio(
	IN	PADAPTER		padapter
)
{
	DumpLoggedInterruptHistory8723Sdio(padapter);
}
#endif

//
//	Description:
//		Initialize SDIO Host Interrupt Mask configuration variables for future use.
//
//	Assumption:
//		Using SDIO Local register ONLY for configuration.
//
//	Created by Roger, 2011.02.11.
//
void InitInterrupt8188ESdio(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->sdio_himr = (u32)(			\
								SDIO_HIMR_RX_REQUEST_MSK			|
//								SDIO_HIMR_AVAL_MSK					|
//								SDIO_HIMR_TXERR_MSK				|
//								SDIO_HIMR_RXERR_MSK				|
//								SDIO_HIMR_TXFOVW_MSK				|
//								SDIO_HIMR_RXFOVW_MSK				|
//								SDIO_HIMR_TXBCNOK_MSK				|
//								SDIO_HIMR_TXBCNERR_MSK			       |
//								SDIO_HIMR_BCNERLY_INT_MSK			|						
//								SDIO_HIMR_C2HCMD_MSK				|
#ifdef CONFIG_LPS_LCLK
								SDIO_HIMR_CPWM1_MSK				|
//								SDIO_HIMR_CPWM2_MSK				|
#endif
//								SDIO_HIMR_HSISR_IND_MSK			|
//								SDIO_HIMR_GTINT3_IND_MSK			|
//								SDIO_HIMR_GTINT4_IND_MSK			|
//								SDIO_HIMR_PSTIMEOUT_MSK			|
//								SDIO_HIMR_OCPINT_MSK				|
//								SDIO_HIMR_ATIMEND_MSK				|
//								SDIO_HIMR_ATIMEND_E_MSK			|
//								SDIO_HIMR_CTWEND_MSK				|
								0);
}

//
//	Description:
//		Clear corresponding SDIO Host ISR interrupt service.
//
//	Assumption:
//		Using SDIO Local register ONLY for configuration.
//
//	Created by Roger, 2011.02.11.
//
void ClearInterrupt8723ASdio(PADAPTER padapter)
{
	u32 tmp = 0;
	tmp = SdioLocalCmd52Read4Byte(padapter, SDIO_REG_HISR);
	SdioLocalCmd52Write4Byte(padapter, SDIO_REG_HISR, tmp);
//	padapter->IsrContent.IntArray[0] = 0;
	padapter->IsrContent = 0;
}

//
//	Description:
//		Enalbe SDIO Host Interrupt Mask configuration on SDIO local domain.
//
//	Assumption:
//		1. Using SDIO Local register ONLY for configuration.
//		2. PASSIVE LEVEL
//
//	Created by Roger, 2011.02.11.
//
void EnableInterrupt8188ESdio(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);
#if 0
	SdioLocalCmd52Write4Byte(padapter, SDIO_REG_HIMR, pHalData->sdio_himr);
#else
{
	u32 himr;
	himr = cpu_to_le32(pHalData->sdio_himr);
	sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8*)&himr);
}
#endif

	//
	// <Roger_Notes> There are some C2H CMDs have been sent before system interrupt is enabled, e.g., C2H, CPWM.
	// So we need to clear all C2H events that FW has notified, otherwise FW won't schedule any commands anymore.
	// 2011.10.19.
	//
	rtw_write8(padapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);

}

//
//	Description:
//		Disable SDIO Host IMR configuration to mask unnecessary interrupt service.
//
//	Assumption:
//		Using SDIO Local register ONLY for configuration.
//
//	Created by Roger, 2011.02.11.
//
void DisableInterrupt8188ESdio(PADAPTER padapter)
{
	// Clear all ISRs
	//PlatformEFSdioLocalCmd52Write4Byte(Adapter, SDIO_REG_HISR, pHalData->IntrMaskToClear[0]);
	//PlatformEFIOWrite4Byte(Adapter, REG_HSISR, pHalData->SysIntrMaskToClear[0]);

	// Clear all IMRs
#if 0
	SdioLocalCmd52Write4Byte(padapter, SDIO_REG_HIMR, SDIO_HIMR_DISABLED);
#else
	u32 himr;
	himr = cpu_to_le32(SDIO_HIMR_DISABLED);
	sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8*)&himr);
#endif
}

//
//	Description:
//		Update SDIO Host Interrupt Mask configuration on SDIO local domain.
//
//	Assumption:
//		1. Using SDIO Local register ONLY for configuration.
//		2. PASSIVE LEVEL
//
//	Created by Roger, 2011.02.11.
//
void UpdateInterruptMask8188ESdio(PADAPTER padapter, u32 AddMSR, u32 RemoveMSR)
{
	HAL_DATA_TYPE *pHalData;


	pHalData = GET_HAL_DATA(padapter);

	if (AddMSR)
		pHalData->sdio_himr |= AddMSR;

	if (RemoveMSR)
		pHalData->sdio_himr &= (~RemoveMSR);

	DisableInterrupt8188ESdio(padapter);
	EnableInterrupt8188ESdio(padapter);
}

#ifdef CONFIG_MAC_LOOPBACK_DRIVER
static void sd_recv_loopback(PADAPTER padapter, u32 size)
{
	PLOOPBACKDATA ploopback;
	u32 readsize, allocsize;
	u8 *preadbuf;


	readsize = size;
	printk("%s: read size=%d\n", __func__, readsize);
	allocsize = _RND(readsize, padapter->dvobjpriv.intf_data.block_transfer_len);

	ploopback = padapter->ploopback;
	if (ploopback) {
		ploopback->rxsize = readsize;
		preadbuf = ploopback->rxbuf;
	}
	else {
		preadbuf = rtw_malloc(allocsize);
		if (preadbuf == NULL) {
			printk("%s: malloc fail size=%d\n", __func__, allocsize);
			return;
		}
	}

//	rtw_read_port(padapter, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);
	sdio_read_port(&padapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);

	if (ploopback)
		_rtw_up_sema(&ploopback->sema);
	else {
		u32 i;

		printk("%s: drop pkt\n", __func__);
		for (i = 0; i < readsize; i+=4) {
			printk("%08X", *(u32*)(preadbuf + i));
			if ((i+4) & 0x1F) printk(" ");
			else printk("\n");
		}
		printk("\n");
		rtw_mfree(preadbuf, allocsize);
	}
}
#endif // CONFIG_MAC_LOOPBACK_DRIVER

static struct recv_buf* sd_recv_rxfifo(PADAPTER padapter, u32 size)
{
	u32 readsize, allocsize, ret;
	u8 *preadbuf;
	_pkt *ppkt;
	struct recv_priv *precvpriv;
	struct recv_buf	*precvbuf;


	readsize = size;

	//3 1. alloc skb
	// align to block size
	allocsize = _RND(readsize, padapter->dvobjpriv.intf_data.block_transfer_len);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
	ppkt = dev_alloc_skb(allocsize);
#else
	ppkt = netdev_alloc_skb(padapter->pnetdev, allocsize);
#endif
	if (ppkt == NULL) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("%s: alloc_skb fail! alloc=%d read=%d\n", __FUNCTION__, allocsize, readsize));
		return NULL;
	}

	//3 2. read data from rxfifo
	preadbuf = skb_put(ppkt, readsize);
//	rtw_read_port(padapter, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);
	ret = sdio_read_port(&padapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);
	if (ret == _FAIL) {
		dev_kfree_skb_any(ppkt);
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("%s: read port FAIL!\n", __FUNCTION__));
		return NULL;
	}

	//3 3. alloc recvbuf
	precvpriv = &padapter->recvpriv;
	precvbuf = rtw_dequeue_recvbuf(&precvpriv->free_recv_buf_queue);
	if (precvbuf == NULL) {
		dev_kfree_skb_any(ppkt);
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("%s: alloc recvbuf FAIL!\n", __FUNCTION__));
		return NULL;
	}

	//3 4. init recvbuf
	precvbuf->pskb = ppkt;

	precvbuf->len = ppkt->len;

	precvbuf->phead = ppkt->head;
	precvbuf->pdata = ppkt->data;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
	precvbuf->ptail = ppkt->head + ppkt->tail;
	precvbuf->pend = ppkt->head + ppkt->end;
#else
	precvbuf->ptail = ppkt->tail;
	precvbuf->pend = ppkt->end;
#endif

	return precvbuf;
}

static void sd_rxhandler(PADAPTER padapter, struct recv_buf *precvbuf)
{
	struct recv_priv *precvpriv;
	_queue *ppending_queue;


	precvpriv = &padapter->recvpriv;
	ppending_queue = &precvpriv->recv_buf_pending_queue;

	if (_rtw_queue_empty(ppending_queue) == _TRUE)
	{
		//3 1. enqueue recvbuf
		rtw_enqueue_recvbuf(precvbuf, ppending_queue);
		
		//3 2. schedule tasklet
#ifdef PLATFORM_LINUX
		tasklet_schedule(&precvpriv->recv_tasklet);
#endif
	} else{
		rtw_enqueue_recvbuf(precvbuf, ppending_queue);
	}

}

void sd_int_dpc(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (pHalData->sdio_hisr & SDIO_HISR_CPWM1)
	{
		struct reportpwrstate_parm report;

		_sdio_local_read(padapter, SDIO_REG_HCPWM1, 1, &report.state);
#ifdef CONFIG_LPS_LCLK
		//88e's cpwm value only change BIT0, so driver need to add PS_STATE_S2 for LPS flow.
		//modify by Thomas. 2012/4/2.
		report.state |= PS_STATE_S2;
		//cpwm_int_hdl(padapter, &report);
		_set_workitem(&padapter->pwrctrlpriv.cpwm_event);
#endif
	}

	if (pHalData->sdio_hisr & SDIO_HISR_TXERR)
	{
		u8 *status;
		u32 addr;
	
		status = _rtw_malloc(4);
		if (status)
		{
			addr = REG_TXDMA_STATUS;
			HalSdioGetCmdAddr8723ASdio(padapter, WLAN_IOREG_DEVICE_ID, addr, &addr);
			_sd_read(&padapter->dvobjpriv.intf_data, addr, 4, status);
			_sd_write(&padapter->dvobjpriv.intf_data, addr, 4, status);
			printk("%s: SDIO_HISR_TXERR (0x%08x)\n", __func__, le32_to_cpu(*(u32*)status));
			_rtw_mfree(status, 4);
		} else {
			printk("%s: SDIO_HISR_TXERR, but can't allocate memory to read status!\n", __func__);
		}		
	}

#ifdef CONFIG_INTERRUPT_BASED_TXBCN

	#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
	if (pHalData->sdio_hisr & SDIO_HISR_BCNERLY_INT)
	#endif
	#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	if (pHalData->sdio_hisr & (SDIO_HISR_TXBCNOK|SDIO_HISR_TXBCNERR))
	#endif	
	{		
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

		#if 0 //for debug
		if (pHalData->sdio_hisr & SDIO_HISR_BCNERLY_INT)
			printk("%s: SDIO_HISR_BCNERLY_INT\n", __func__);		
		
		if (pHalData->sdio_hisr & SDIO_HISR_TXBCNOK)	
			printk("%s: SDIO_HISR_TXBCNOK\n", __func__);
	
		if (pHalData->sdio_hisr & SDIO_HISR_TXBCNERR)	
			printk("%s: SDIO_HISR_TXBCNERR\n", __func__);
		#endif

		
		if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
		{
			//send_beacon(padapter);
			if(pmlmepriv->update_bcn == _TRUE)
			{
				//tx_beacon_hdl(padapter, NULL);
				set_tx_beacon_cmd(padapter);
			}
		}
	}
#endif //CONFIG_INTERRUPT_BASED_TXBCN

	
	if (pHalData->sdio_hisr & SDIO_HISR_C2HCMD)
	{
		printk("%s: C2H Command\n", __func__);
	}

	if (pHalData->sdio_hisr & SDIO_HISR_RX_REQUEST)
	{
		struct recv_buf *precvbuf;

		//printk("%s: RX Request, size=%d\n", __func__, phal->SdioRxFIFOSize);
		pHalData->sdio_hisr ^= SDIO_HISR_RX_REQUEST;
#ifdef CONFIG_MAC_LOOPBACK_DRIVER
		sd_recv_loopback(padapter, pHalData->SdioRxFIFOSize);
#else
		//Sometimes rx length will be zero. driver need to use cmd53 read again.
		if(pHalData->SdioRxFIFOSize == 0)
		{
			u8 data[4];

			_sdio_local_read(padapter, SDIO_REG_RX0_REQ_LEN, 4, data);

			pHalData->SdioRxFIFOSize = le16_to_cpu(*(u16*)data);
		}

		if(pHalData->SdioRxFIFOSize)
		{
			precvbuf = sd_recv_rxfifo(padapter, pHalData->SdioRxFIFOSize);

			if (precvbuf)
				sd_rxhandler(padapter, precvbuf);

			pHalData->SdioRxFIFOSize = 0;
		}
#endif

	}
	
}

void sd_int_hdl(PADAPTER padapter)
{
	u8 data[6];
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	if ((padapter->bDriverStopped == _TRUE) ||
	    (padapter->bSurpriseRemoved == _TRUE))
		return;

	_sdio_local_read(padapter, SDIO_REG_HISR, 6, data);
	pHalData->sdio_hisr = le32_to_cpu(*(u32*)data);
	pHalData->SdioRxFIFOSize = le16_to_cpu(*(u16*)&data[4]);

	if (pHalData->sdio_hisr & pHalData->sdio_himr)
	{
		u32 v32;

		pHalData->sdio_hisr &= pHalData->sdio_himr;

		// clear HISR
		v32 = pHalData->sdio_hisr & MASK_SDIO_HISR_CLEAR;
		if (v32) {
			v32 = cpu_to_le32(v32);
			_sdio_local_write(padapter, SDIO_REG_HISR, 4, (u8*)&v32);
		}

		sd_int_dpc(padapter);
		
	} 
	else 
	{
		RT_TRACE(_module_hci_ops_c_, _drv_err_,
				("%s: HISR(0x%08x) and HIMR(0x%08x) not match!\n",
				__FUNCTION__, pHalData->sdio_hisr, pHalData->sdio_himr));
	}
	
}

//
//	Description:
//		Query SDIO Local register to query current the number of Free TxPacketBuffer page.
//
//	Assumption:
//		1. Running at PASSIVE_LEVEL
//		2. RT_TX_SPINLOCK is NOT acquired.
//
//	Created by Roger, 2011.01.28.
//
u8 HalQueryTxBufferStatus8189ESdio(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32 NumOfFreePage;
//	_irqL irql;


	pHalData = GET_HAL_DATA(padapter);

	NumOfFreePage = SdioLocalCmd53Read4Byte(padapter, SDIO_REG_FREE_TXPG);

//	_enter_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);
	_rtw_memcpy(pHalData->SdioTxFIFOFreePage, &NumOfFreePage, 4);
	RT_TRACE(_module_hci_ops_c_, _drv_notice_,
			("%s: Free page for HIQ(%#x),MIDQ(%#x),LOWQ(%#x),PUBQ(%#x)\n",
			__FUNCTION__,
			pHalData->SdioTxFIFOFreePage[HI_QUEUE_IDX],
			pHalData->SdioTxFIFOFreePage[MID_QUEUE_IDX],
			pHalData->SdioTxFIFOFreePage[LOW_QUEUE_IDX],
			pHalData->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX]));
/*
	printk("%s: Free page for HIQ(%#x),MIDQ(%#x),LOWQ(%#x),PUBQ(%#x)\n",
			__FUNCTION__,
			pHalData->SdioTxFIFOFreePage[HI_QUEUE_IDX],
			pHalData->SdioTxFIFOFreePage[MID_QUEUE_IDX],
			pHalData->SdioTxFIFOFreePage[LOW_QUEUE_IDX],
			pHalData->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX]);
*/	
//	_exit_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);

	return _TRUE;
}

