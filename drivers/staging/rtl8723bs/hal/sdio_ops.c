/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 *******************************************************************************/
#define _SDIO_OPS_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>

/* define SDIO_DEBUG_IO 1 */


/*  */
/*  Description: */
/* 	The following mapping is for SDIO host local register space. */
/*  */
/*  Creadted by Roger, 2011.01.31. */
/*  */
static void HalSdioGetCmdAddr8723BSdio(
	struct adapter *padapter,
	u8 DeviceID,
	u32 Addr,
	u32 *pCmdAddr
)
{
	switch (DeviceID) {
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
	switch (pseudoId) {
	case 0x1025:
		devideId = SDIO_LOCAL_DEVICE_ID;
		break;

	case 0x1026:
		devideId = WLAN_IOREG_DEVICE_ID;
		break;

/* 		case 0x1027: */
/* 			devideId = SDIO_FIRMWARE_FIFO; */
/* 			break; */

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
/* 			devideId = (u8)((addr >> 13) & 0xF); */
		devideId = WLAN_IOREG_DEVICE_ID;
		break;
	}

	return devideId;
}

/*
 * Ref:
 *HalSdioGetCmdAddr8723BSdio()
 */
static u32 _cvrt2ftaddr(const u32 addr, u8 *pdeviceId, u16 *poffset)
{
	u8 deviceId;
	u16 offset;
	u32 ftaddr;


	deviceId = get_deviceid(addr);
	offset = 0;

	switch (deviceId) {
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

	if (pdeviceId)
		*pdeviceId = deviceId;
	if (poffset)
		*poffset = offset;

	return ftaddr;
}

static u8 sdio_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	u32 ftaddr;
	u8 val;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	val = sd_read8(pintfhdl, ftaddr, NULL);
	return val;
}

static u16 sdio_read16(struct intf_hdl *pintfhdl, u32 addr)
{
	u32 ftaddr;
	u16 val;
	__le16 le_tmp;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	sd_cmd52_read(pintfhdl, ftaddr, 2, (u8 *)&le_tmp);
	val = le16_to_cpu(le_tmp);
	return val;
}

static u32 sdio_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	struct adapter *padapter;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	u32 val;
	s32 err;
	__le32 le_tmp;

	padapter = pintfhdl->padapter;
	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (
		((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(false == bMacPwrCtrlOn) ||
		(true == adapter_to_pwrctl(padapter)->bFwCurrentInPSMode)
	) {
		err = sd_cmd52_read(pintfhdl, ftaddr, 4, (u8 *)&le_tmp);
#ifdef SDIO_DEBUG_IO
		if (!err) {
#endif
			val = le32_to_cpu(le_tmp);
			return val;
#ifdef SDIO_DEBUG_IO
		}

		DBG_8192C(KERN_ERR "%s: Mac Power off, Read FAIL(%d)! addr = 0x%x\n", __func__, err, addr);
		return SDIO_ERR_VAL32;
#endif
	}

	/*  4 bytes alignment */
	shift = ftaddr & 0x3;
	if (shift == 0) {
		val = sd_read32(pintfhdl, ftaddr, NULL);
	} else {
		u8 *ptmpbuf;

		ptmpbuf = rtw_malloc(8);
		if (NULL == ptmpbuf) {
			DBG_8192C(KERN_ERR "%s: Allocate memory FAIL!(size =8) addr = 0x%x\n", __func__, addr);
			return SDIO_ERR_VAL32;
		}

		ftaddr &= ~(u16)0x3;
		sd_read(pintfhdl, ftaddr, 8, ptmpbuf);
		memcpy(&le_tmp, ptmpbuf+shift, 4);
		val = le32_to_cpu(le_tmp);

		kfree(ptmpbuf);
	}
	return val;
}

static s32 sdio_readN(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pbuf)
{
	struct adapter *padapter;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

	padapter = pintfhdl->padapter;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (
		((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(false == bMacPwrCtrlOn) ||
		(true == adapter_to_pwrctl(padapter)->bFwCurrentInPSMode)
	) {
		err = sd_cmd52_read(pintfhdl, ftaddr, cnt, pbuf);
		return err;
	}

	/*  4 bytes alignment */
	shift = ftaddr & 0x3;
	if (shift == 0) {
		err = sd_read(pintfhdl, ftaddr, cnt, pbuf);
	} else {
		u8 *ptmpbuf;
		u32 n;

		ftaddr &= ~(u16)0x3;
		n = cnt + shift;
		ptmpbuf = rtw_malloc(n);
		if (NULL == ptmpbuf)
			return -1;

		err = sd_read(pintfhdl, ftaddr, n, ptmpbuf);
		if (!err)
			memcpy(pbuf, ptmpbuf+shift, cnt);
		kfree(ptmpbuf);
	}
	return err;
}

static s32 sdio_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u32 ftaddr;
	s32 err;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	sd_write8(pintfhdl, ftaddr, val, &err);

	return err;
}

static s32 sdio_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	u32 ftaddr;
	s32 err;
	__le16 le_tmp;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	le_tmp = cpu_to_le16(val);
	err = sd_cmd52_write(pintfhdl, ftaddr, 2, (u8 *)&le_tmp);

	return err;
}

static s32 sdio_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	struct adapter *padapter;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;
	__le32 le_tmp;

	padapter = pintfhdl->padapter;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (
		((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(!bMacPwrCtrlOn) ||
		(adapter_to_pwrctl(padapter)->bFwCurrentInPSMode)
	) {
		le_tmp = cpu_to_le32(val);
		err = sd_cmd52_write(pintfhdl, ftaddr, 4, (u8 *)&le_tmp);
		return err;
	}

	/*  4 bytes alignment */
	shift = ftaddr & 0x3;
	if (shift == 0) {
		sd_write32(pintfhdl, ftaddr, val, &err);
	} else {
		le_tmp = cpu_to_le32(val);
		err = sd_cmd52_write(pintfhdl, ftaddr, 4, (u8 *)&le_tmp);
	}
	return err;
}

static s32 sdio_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pbuf)
{
	struct adapter *padapter;
	u8 bMacPwrCtrlOn;
	u8 deviceId;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

	padapter = pintfhdl->padapter;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &deviceId, &offset);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (
		((deviceId == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(false == bMacPwrCtrlOn) ||
		(true == adapter_to_pwrctl(padapter)->bFwCurrentInPSMode)
	) {
		err = sd_cmd52_write(pintfhdl, ftaddr, cnt, pbuf);
		return err;
	}

	shift = ftaddr & 0x3;
	if (shift == 0) {
		err = sd_write(pintfhdl, ftaddr, cnt, pbuf);
	} else {
		u8 *ptmpbuf;
		u32 n;

		ftaddr &= ~(u16)0x3;
		n = cnt + shift;
		ptmpbuf = rtw_malloc(n);
		if (NULL == ptmpbuf)
			return -1;
		err = sd_read(pintfhdl, ftaddr, 4, ptmpbuf);
		if (err) {
			kfree(ptmpbuf);
			return err;
		}
		memcpy(ptmpbuf+shift, pbuf, cnt);
		err = sd_write(pintfhdl, ftaddr, n, ptmpbuf);
		kfree(ptmpbuf);
	}
	return err;
}

static u8 sdio_f0_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	return sd_f0_read8(pintfhdl, addr, NULL);
}

static void sdio_read_mem(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *rmem
)
{
	s32 err;

	err = sdio_readN(pintfhdl, addr, cnt, rmem);
	/* TODO: Report error is err not zero */
}

static void sdio_write_mem(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *wmem
)
{
	sdio_writeN(pintfhdl, addr, cnt, wmem);
}

/*
 * Description:
 *Read from RX FIFO
 *Round read size to block size,
 *and make sure data transfer will be done in one command.
 *
 * Parameters:
 *pintfhdl	a pointer of intf_hdl
 *addr		port ID
 *cnt			size to read
 *rmem		address to put data
 *
 * Return:
 *_SUCCESS(1)		Success
 *_FAIL(0)		Fail
 */
static u32 sdio_read_port(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem
)
{
	struct adapter *padapter;
	PSDIO_DATA psdio;
	struct hal_com_data *phal;
	u32 oldcnt;
#ifdef SDIO_DYNAMIC_ALLOC_MEM
	u8 *oldmem;
#endif
	s32 err;


	padapter = pintfhdl->padapter;
	psdio = &adapter_to_dvobj(padapter)->intf_data;
	phal = GET_HAL_DATA(padapter);

	HalSdioGetCmdAddr8723BSdio(padapter, addr, phal->SdioRxFIFOCnt++, &addr);

	oldcnt = cnt;
	if (cnt > psdio->block_transfer_len)
		cnt = _RND(cnt, psdio->block_transfer_len);
/* 	cnt = sdio_align_size(cnt); */

	if (oldcnt != cnt) {
#ifdef SDIO_DYNAMIC_ALLOC_MEM
		oldmem = mem;
		mem = rtw_malloc(cnt);
		if (mem == NULL) {
			DBG_8192C(KERN_WARNING "%s: allocate memory %d bytes fail!\n", __func__, cnt);
			mem = oldmem;
			oldmem == NULL;
		}
#else
		/*  in this case, caller should gurante the buffer is big enough */
		/*  to receive data after alignment */
#endif
	}

	err = _sd_read(pintfhdl, addr, cnt, mem);

#ifdef SDIO_DYNAMIC_ALLOC_MEM
	if ((oldcnt != cnt) && (oldmem)) {
		memcpy(oldmem, mem, oldcnt);
		kfree(mem);
	}
#endif

	if (err)
		return _FAIL;
	return _SUCCESS;
}

/*
 * Description:
 *Write to TX FIFO
 *Align write size block size,
 *and make sure data could be written in one command.
 *
 * Parameters:
 *pintfhdl	a pointer of intf_hdl
 *addr		port ID
 *cnt			size to write
 *wmem		data pointer to write
 *
 * Return:
 *_SUCCESS(1)		Success
 *_FAIL(0)		Fail
 */
static u32 sdio_write_port(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem
)
{
	struct adapter *padapter;
	PSDIO_DATA psdio;
	s32 err;
	struct xmit_buf *xmitbuf = (struct xmit_buf *)mem;

	padapter = pintfhdl->padapter;
	psdio = &adapter_to_dvobj(padapter)->intf_data;

	if (padapter->hw_init_completed == false) {
		DBG_871X("%s [addr = 0x%x cnt =%d] padapter->hw_init_completed == false\n", __func__, addr, cnt);
		return _FAIL;
	}

	cnt = _RND4(cnt);
	HalSdioGetCmdAddr8723BSdio(padapter, addr, cnt >> 2, &addr);

	if (cnt > psdio->block_transfer_len)
		cnt = _RND(cnt, psdio->block_transfer_len);
/* 	cnt = sdio_align_size(cnt); */

	err = sd_write(pintfhdl, addr, cnt, xmitbuf->pdata);

	rtw_sctx_done_err(
		&xmitbuf->sctx,
		err ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS
	);

	if (err)
		return _FAIL;
	return _SUCCESS;
}

void sdio_set_intf_ops(struct adapter *padapter, struct _io_ops *pops)
{
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

	pops->_sd_f0_read8 = sdio_f0_read8;
}

/*
 * Todo: align address to 4 bytes.
 */
static s32 _sdio_local_read(
	struct adapter *padapter,
	u32 addr,
	u32 cnt,
	u8 *pbuf
)
{
	struct intf_hdl *pintfhdl;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;
	u32 n;


	pintfhdl = &padapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (false == bMacPwrCtrlOn) {
		err = _sd_cmd52_read(pintfhdl, addr, cnt, pbuf);
		return err;
	}

	n = RND4(cnt);
	ptmpbuf = rtw_malloc(n);
	if (!ptmpbuf)
		return (-1);

	err = _sd_read(pintfhdl, addr, n, ptmpbuf);
	if (!err)
		memcpy(pbuf, ptmpbuf, cnt);

	kfree(ptmpbuf);

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 sdio_local_read(
	struct adapter *padapter,
	u32 addr,
	u32 cnt,
	u8 *pbuf
)
{
	struct intf_hdl *pintfhdl;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;
	u32 n;

	pintfhdl = &padapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (
		(false == bMacPwrCtrlOn) ||
		(true == adapter_to_pwrctl(padapter)->bFwCurrentInPSMode)
	) {
		err = sd_cmd52_read(pintfhdl, addr, cnt, pbuf);
		return err;
	}

	n = RND4(cnt);
	ptmpbuf = rtw_malloc(n);
	if (!ptmpbuf)
		return (-1);

	err = sd_read(pintfhdl, addr, n, ptmpbuf);
	if (!err)
		memcpy(pbuf, ptmpbuf, cnt);

	kfree(ptmpbuf);

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 sdio_local_write(
	struct adapter *padapter,
	u32 addr,
	u32 cnt,
	u8 *pbuf
)
{
	struct intf_hdl *pintfhdl;
	u8 bMacPwrCtrlOn;
	s32 err;
	u8 *ptmpbuf;

	if (addr & 0x3)
		DBG_8192C("%s, address must be 4 bytes alignment\n", __func__);

	if (cnt  & 0x3)
		DBG_8192C("%s, size must be the multiple of 4\n", __func__);

	pintfhdl = &padapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (
		(false == bMacPwrCtrlOn) ||
		(true == adapter_to_pwrctl(padapter)->bFwCurrentInPSMode)
	) {
		err = sd_cmd52_write(pintfhdl, addr, cnt, pbuf);
		return err;
	}

	ptmpbuf = rtw_malloc(cnt);
	if (!ptmpbuf)
		return (-1);

	memcpy(ptmpbuf, pbuf, cnt);

	err = sd_write(pintfhdl, addr, cnt, ptmpbuf);

	kfree(ptmpbuf);

	return err;
}

u8 SdioLocalCmd52Read1Byte(struct adapter *padapter, u32 addr)
{
	u8 val = 0;
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(pintfhdl, addr, 1, &val);

	return val;
}

static u16 SdioLocalCmd52Read2Byte(struct adapter *padapter, u32 addr)
{
	__le16 val = 0;
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(pintfhdl, addr, 2, (u8 *)&val);

	return le16_to_cpu(val);
}

static u32 SdioLocalCmd53Read4Byte(struct adapter *padapter, u32 addr)
{

	u8 bMacPwrCtrlOn;
	u32 val = 0;
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;
	__le32 le_tmp;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (!bMacPwrCtrlOn || adapter_to_pwrctl(padapter)->bFwCurrentInPSMode) {
		sd_cmd52_read(pintfhdl, addr, 4, (u8 *)&le_tmp);
		val = le32_to_cpu(le_tmp);
	} else {
		val = sd_read32(pintfhdl, addr, NULL);
	}
	return val;
}

void SdioLocalCmd52Write1Byte(struct adapter *padapter, u32 addr, u8 v)
{
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_write(pintfhdl, addr, 1, &v);
}

static void SdioLocalCmd52Write4Byte(struct adapter *padapter, u32 addr, u32 v)
{
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;
	__le32 le_tmp;

	HalSdioGetCmdAddr8723BSdio(padapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	le_tmp = cpu_to_le32(v);
	sd_cmd52_write(pintfhdl, addr, 4, (u8 *)&le_tmp);
}

static s32 ReadInterrupt8723BSdio(struct adapter *padapter, u32 *phisr)
{
	u32 hisr, himr;
	u8 val8, hisr_len;


	if (phisr == NULL)
		return false;

	himr = GET_HAL_DATA(padapter)->sdio_himr;

	/*  decide how many bytes need to be read */
	hisr_len = 0;
	while (himr) {
		hisr_len++;
		himr >>= 8;
	}

	hisr = 0;
	while (hisr_len != 0) {
		hisr_len--;
		val8 = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_HISR+hisr_len);
		hisr |= (val8 << (8*hisr_len));
	}

	*phisr = hisr;

	return true;
}

/*  */
/* 	Description: */
/* 		Initialize SDIO Host Interrupt Mask configuration variables for future use. */
/*  */
/* 	Assumption: */
/* 		Using SDIO Local register ONLY for configuration. */
/*  */
/* 	Created by Roger, 2011.02.11. */
/*  */
void InitInterrupt8723BSdio(struct adapter *padapter)
{
	struct hal_com_data *pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->sdio_himr = (u32)(		\
								SDIO_HIMR_RX_REQUEST_MSK			|
								SDIO_HIMR_AVAL_MSK					|
/* 								SDIO_HIMR_TXERR_MSK				| */
/* 								SDIO_HIMR_RXERR_MSK				| */
/* 								SDIO_HIMR_TXFOVW_MSK				| */
/* 								SDIO_HIMR_RXFOVW_MSK				| */
/* 								SDIO_HIMR_TXBCNOK_MSK				| */
/* 								SDIO_HIMR_TXBCNERR_MSK			| */
/* 								SDIO_HIMR_BCNERLY_INT_MSK			| */
/* 								SDIO_HIMR_C2HCMD_MSK				| */
/* 								SDIO_HIMR_HSISR_IND_MSK			| */
/* 								SDIO_HIMR_GTINT3_IND_MSK			| */
/* 								SDIO_HIMR_GTINT4_IND_MSK			| */
/* 								SDIO_HIMR_PSTIMEOUT_MSK			| */
/* 								SDIO_HIMR_OCPINT_MSK				| */
/* 								SDIO_HIMR_ATIMEND_MSK				| */
/* 								SDIO_HIMR_ATIMEND_E_MSK			| */
/* 								SDIO_HIMR_CTWEND_MSK				| */
								0);
}

/*  */
/* 	Description: */
/* 		Initialize System Host Interrupt Mask configuration variables for future use. */
/*  */
/* 	Created by Roger, 2011.08.03. */
/*  */
void InitSysInterrupt8723BSdio(struct adapter *padapter)
{
	struct hal_com_data *pHalData;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->SysIntrMask = (		\
/* 							HSIMR_GPIO12_0_INT_EN			| */
/* 							HSIMR_SPS_OCP_INT_EN			| */
/* 							HSIMR_RON_INT_EN				| */
/* 							HSIMR_PDNINT_EN				| */
/* 							HSIMR_GPIO9_INT_EN				| */
							0);
}

#ifdef CONFIG_WOWLAN
/*  */
/* 	Description: */
/* 		Clear corresponding SDIO Host ISR interrupt service. */
/*  */
/* 	Assumption: */
/* 		Using SDIO Local register ONLY for configuration. */
/*  */
/* 	Created by Roger, 2011.02.11. */
/*  */
void ClearInterrupt8723BSdio(struct adapter *padapter)
{
	struct hal_com_data *pHalData;
	u8 *clear;


	if (true == padapter->bSurpriseRemoved)
		return;

	pHalData = GET_HAL_DATA(padapter);
	clear = rtw_zmalloc(4);

	/*  Clear corresponding HISR Content if needed */
	*(__le32 *)clear = cpu_to_le32(pHalData->sdio_hisr & MASK_SDIO_HISR_CLEAR);
	if (*(__le32 *)clear) {
		/*  Perform write one clear operation */
		sdio_local_write(padapter, SDIO_REG_HISR, 4, clear);
	}

	kfree(clear);
}
#endif

/*  */
/* 	Description: */
/* 		Enalbe SDIO Host Interrupt Mask configuration on SDIO local domain. */
/*  */
/* 	Assumption: */
/* 		1. Using SDIO Local register ONLY for configuration. */
/* 		2. PASSIVE LEVEL */
/*  */
/* 	Created by Roger, 2011.02.11. */
/*  */
void EnableInterrupt8723BSdio(struct adapter *padapter)
{
	struct hal_com_data *pHalData;
	__le32 himr;
	u32 tmp;

	pHalData = GET_HAL_DATA(padapter);

	himr = cpu_to_le32(pHalData->sdio_himr);
	sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8 *)&himr);

	RT_TRACE(
		_module_hci_ops_c_,
		_drv_notice_,
		(
			"%s: enable SDIO HIMR = 0x%08X\n",
			__func__,
			pHalData->sdio_himr
		)
	);

	/*  Update current system IMR settings */
	tmp = rtw_read32(padapter, REG_HSIMR);
	rtw_write32(padapter, REG_HSIMR, tmp | pHalData->SysIntrMask);

	RT_TRACE(
		_module_hci_ops_c_,
		_drv_notice_,
		(
			"%s: enable HSIMR = 0x%08X\n",
			__func__,
			pHalData->SysIntrMask
		)
	);

	/*  */
	/*  <Roger_Notes> There are some C2H CMDs have been sent before system interrupt is enabled, e.g., C2H, CPWM. */
	/*  So we need to clear all C2H events that FW has notified, otherwise FW won't schedule any commands anymore. */
	/*  2011.10.19. */
	/*  */
	rtw_write8(padapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

/*  */
/* 	Description: */
/* 		Disable SDIO Host IMR configuration to mask unnecessary interrupt service. */
/*  */
/* 	Assumption: */
/* 		Using SDIO Local register ONLY for configuration. */
/*  */
/* 	Created by Roger, 2011.02.11. */
/*  */
void DisableInterrupt8723BSdio(struct adapter *padapter)
{
	__le32 himr;

	himr = cpu_to_le32(SDIO_HIMR_DISABLED);
	sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8 *)&himr);
}

/*  */
/* 	Description: */
/* 		Using 0x100 to check the power status of FW. */
/*  */
/* 	Assumption: */
/* 		Using SDIO Local register ONLY for configuration. */
/*  */
/* 	Created by Isaac, 2013.09.10. */
/*  */
u8 CheckIPSStatus(struct adapter *padapter)
{
	DBG_871X(
		"%s(): Read 0x100 = 0x%02x 0x86 = 0x%02x\n",
		__func__,
		rtw_read8(padapter, 0x100),
		rtw_read8(padapter, 0x86)
	);

	if (rtw_read8(padapter, 0x100) == 0xEA)
		return true;
	else
		return false;
}

static struct recv_buf *sd_recv_rxfifo(struct adapter *padapter, u32 size)
{
	u32 readsize, ret;
	u8 *preadbuf;
	struct recv_priv *precvpriv;
	struct recv_buf	*precvbuf;


	/*  Patch for some SDIO Host 4 bytes issue */
	/*  ex. RK3188 */
	readsize = RND4(size);

	/* 3 1. alloc recvbuf */
	precvpriv = &padapter->recvpriv;
	precvbuf = rtw_dequeue_recvbuf(&precvpriv->free_recv_buf_queue);
	if (precvbuf == NULL) {
		DBG_871X_LEVEL(_drv_err_, "%s: alloc recvbuf FAIL!\n", __func__);
		return NULL;
	}

	/* 3 2. alloc skb */
	if (precvbuf->pskb == NULL) {
		SIZE_PTR tmpaddr = 0;
		SIZE_PTR alignment = 0;

		precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

		if (precvbuf->pskb) {
			precvbuf->pskb->dev = padapter->pnetdev;

			tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
			alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
			skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
		}

		if (precvbuf->pskb == NULL) {
			DBG_871X("%s: alloc_skb fail! read =%d\n", __func__, readsize);
			return NULL;
		}
	}

	/* 3 3. read data from rxfifo */
	preadbuf = precvbuf->pskb->data;
	ret = sdio_read_port(&padapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);
	if (ret == _FAIL) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("%s: read port FAIL!\n", __func__));
		return NULL;
	}


	/* 3 4. init recvbuf */
	precvbuf->len = size;
	precvbuf->phead = precvbuf->pskb->head;
	precvbuf->pdata = precvbuf->pskb->data;
	skb_set_tail_pointer(precvbuf->pskb, size);
	precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
	precvbuf->pend = skb_end_pointer(precvbuf->pskb);

	return precvbuf;
}

static void sd_rxhandler(struct adapter *padapter, struct recv_buf *precvbuf)
{
	struct recv_priv *precvpriv;
	struct __queue *ppending_queue;

	precvpriv = &padapter->recvpriv;
	ppending_queue = &precvpriv->recv_buf_pending_queue;

	/* 3 1. enqueue recvbuf */
	rtw_enqueue_recvbuf(precvbuf, ppending_queue);

	/* 3 2. schedule tasklet */
	tasklet_schedule(&precvpriv->recv_tasklet);
}

void sd_int_dpc(struct adapter *padapter)
{
	struct hal_com_data *phal;
	struct dvobj_priv *dvobj;
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;
	struct pwrctrl_priv *pwrctl;


	phal = GET_HAL_DATA(padapter);
	dvobj = adapter_to_dvobj(padapter);
	pwrctl = dvobj_to_pwrctl(dvobj);

	if (phal->sdio_hisr & SDIO_HISR_AVAL) {
		u8 freepage[4];

		_sdio_local_read(padapter, SDIO_REG_FREE_TXPG, 4, freepage);
		up(&(padapter->xmitpriv.xmit_sema));
	}

	if (phal->sdio_hisr & SDIO_HISR_CPWM1) {
		struct reportpwrstate_parm report;

		u8 bcancelled;
		_cancel_timer(&(pwrctl->pwr_rpwm_timer), &bcancelled);

		report.state = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_HCPWM1_8723B);

		/* cpwm_int_hdl(padapter, &report); */
		_set_workitem(&(pwrctl->cpwm_event));
	}

	if (phal->sdio_hisr & SDIO_HISR_TXERR) {
		u8 *status;
		u32 addr;

		status = rtw_malloc(4);
		if (status) {
			addr = REG_TXDMA_STATUS;
			HalSdioGetCmdAddr8723BSdio(padapter, WLAN_IOREG_DEVICE_ID, addr, &addr);
			_sd_read(pintfhdl, addr, 4, status);
			_sd_write(pintfhdl, addr, 4, status);
			DBG_8192C("%s: SDIO_HISR_TXERR (0x%08x)\n", __func__, le32_to_cpu(*(u32 *)status));
			kfree(status);
		} else {
			DBG_8192C("%s: SDIO_HISR_TXERR, but can't allocate memory to read status!\n", __func__);
		}
	}

	if (phal->sdio_hisr & SDIO_HISR_TXBCNOK) {
		DBG_8192C("%s: SDIO_HISR_TXBCNOK\n", __func__);
	}

	if (phal->sdio_hisr & SDIO_HISR_TXBCNERR) {
		DBG_8192C("%s: SDIO_HISR_TXBCNERR\n", __func__);
	}
#ifndef CONFIG_C2H_PACKET_EN
	if (phal->sdio_hisr & SDIO_HISR_C2HCMD) {
		struct c2h_evt_hdr_88xx *c2h_evt;

		DBG_8192C("%s: C2H Command\n", __func__);
		c2h_evt = rtw_zmalloc(16);
		if (c2h_evt != NULL) {
			if (rtw_hal_c2h_evt_read(padapter, (u8 *)c2h_evt) == _SUCCESS) {
				if (c2h_id_filter_ccx_8723b((u8 *)c2h_evt)) {
					/* Handle CCX report here */
					rtw_hal_c2h_handler(padapter, (u8 *)c2h_evt);
					kfree((u8 *)c2h_evt);
				} else {
					rtw_c2h_wk_cmd(padapter, (u8 *)c2h_evt);
				}
			}
		} else {
			/* Error handling for malloc fail */
			if (rtw_cbuf_push(padapter->evtpriv.c2h_queue, NULL) != _SUCCESS)
				DBG_871X("%s rtw_cbuf_push fail\n", __func__);
			_set_workitem(&padapter->evtpriv.c2h_wk);
		}
	}
#endif

	if (phal->sdio_hisr & SDIO_HISR_RXFOVW) {
		DBG_8192C("%s: Rx Overflow\n", __func__);
	}

	if (phal->sdio_hisr & SDIO_HISR_RXERR) {
		DBG_8192C("%s: Rx Error\n", __func__);
	}

	if (phal->sdio_hisr & SDIO_HISR_RX_REQUEST) {
		struct recv_buf *precvbuf;
		int alloc_fail_time = 0;
		u32 hisr;

/* 		DBG_8192C("%s: RX Request, size =%d\n", __func__, phal->SdioRxFIFOSize); */
		phal->sdio_hisr ^= SDIO_HISR_RX_REQUEST;
		do {
			phal->SdioRxFIFOSize = SdioLocalCmd52Read2Byte(padapter, SDIO_REG_RX0_REQ_LEN);
			if (phal->SdioRxFIFOSize != 0) {
				precvbuf = sd_recv_rxfifo(padapter, phal->SdioRxFIFOSize);
				if (precvbuf)
					sd_rxhandler(padapter, precvbuf);
				else {
					alloc_fail_time++;
					DBG_871X("precvbuf is Null for %d times because alloc memory failed\n", alloc_fail_time);
					if (alloc_fail_time >= 10)
						break;
				}
				phal->SdioRxFIFOSize = 0;
			} else
				break;

			hisr = 0;
			ReadInterrupt8723BSdio(padapter, &hisr);
			hisr &= SDIO_HISR_RX_REQUEST;
			if (!hisr)
				break;
		} while (1);

		if (alloc_fail_time == 10)
			DBG_871X("exit because alloc memory failed more than 10 times\n");

	}
}

void sd_int_hdl(struct adapter *padapter)
{
	struct hal_com_data *phal;


	if (
		(padapter->bDriverStopped == true) ||
		(padapter->bSurpriseRemoved == true)
	)
		return;

	phal = GET_HAL_DATA(padapter);

	phal->sdio_hisr = 0;
	ReadInterrupt8723BSdio(padapter, &phal->sdio_hisr);

	if (phal->sdio_hisr & phal->sdio_himr) {
		u32 v32;

		phal->sdio_hisr &= phal->sdio_himr;

		/*  clear HISR */
		v32 = phal->sdio_hisr & MASK_SDIO_HISR_CLEAR;
		if (v32) {
			SdioLocalCmd52Write4Byte(padapter, SDIO_REG_HISR, v32);
		}

		sd_int_dpc(padapter);
	} else {
		RT_TRACE(_module_hci_ops_c_, _drv_err_,
				("%s: HISR(0x%08x) and HIMR(0x%08x) not match!\n",
				__func__, phal->sdio_hisr, phal->sdio_himr));
	}
}

/*  */
/* 	Description: */
/* 		Query SDIO Local register to query current the number of Free TxPacketBuffer page. */
/*  */
/* 	Assumption: */
/* 		1. Running at PASSIVE_LEVEL */
/* 		2. RT_TX_SPINLOCK is NOT acquired. */
/*  */
/* 	Created by Roger, 2011.01.28. */
/*  */
u8 HalQueryTxBufferStatus8723BSdio(struct adapter *padapter)
{
	struct hal_com_data *phal;
	u32 NumOfFreePage;
	/* _irqL irql; */


	phal = GET_HAL_DATA(padapter);

	NumOfFreePage = SdioLocalCmd53Read4Byte(padapter, SDIO_REG_FREE_TXPG);

	/* spin_lock_bh(&phal->SdioTxFIFOFreePageLock); */
	memcpy(phal->SdioTxFIFOFreePage, &NumOfFreePage, 4);
	RT_TRACE(_module_hci_ops_c_, _drv_notice_,
			("%s: Free page for HIQ(%#x), MIDQ(%#x), LOWQ(%#x), PUBQ(%#x)\n",
			__func__,
			phal->SdioTxFIFOFreePage[HI_QUEUE_IDX],
			phal->SdioTxFIFOFreePage[MID_QUEUE_IDX],
			phal->SdioTxFIFOFreePage[LOW_QUEUE_IDX],
			phal->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX]));
	/* spin_unlock_bh(&phal->SdioTxFIFOFreePageLock); */

	return true;
}

/*  */
/* 	Description: */
/* 		Query SDIO Local register to get the current number of TX OQT Free Space. */
/*  */
u8 HalQueryTxOQTBufferStatus8723BSdio(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	pHalData->SdioTxOQTFreeSpace = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_OQT_FREE_PG);
	return true;
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
u8 RecvOnePkt(struct adapter *padapter, u32 size)
{
	struct recv_buf *precvbuf;
	struct dvobj_priv *psddev;
	PSDIO_DATA psdio_data;
	struct sdio_func *func;

	u8 res = false;

	DBG_871X("+%s: size: %d+\n", __func__, size);

	if (padapter == NULL) {
		DBG_871X(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return false;
	}

	psddev = adapter_to_dvobj(padapter);
	psdio_data = &psddev->intf_data;
	func = psdio_data->func;

	if (size) {
		sdio_claim_host(func);
		precvbuf = sd_recv_rxfifo(padapter, size);

		if (precvbuf) {
			/* printk("Completed Recv One Pkt.\n"); */
			sd_rxhandler(padapter, precvbuf);
			res = true;
		} else {
			res = false;
		}
		sdio_release_host(func);
	}
	DBG_871X("-%s-\n", __func__);
	return res;
}
#endif /* CONFIG_WOWLAN */
