// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 *******************************************************************************/
#define _SDIO_OPS_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>

/* define SDIO_DEBUG_IO 1 */


/*  */
/*  Description: */
/*	The following mapping is for SDIO host local register space. */
/*  */
/*  Creadted by Roger, 2011.01.31. */
/*  */
static void HalSdioGetCmdAddr8723BSdio(
	struct adapter *adapter,
	u8 device_id,
	u32 addr,
	u32 *cmdaddr
)
{
	switch (device_id) {
	case SDIO_LOCAL_DEVICE_ID:
		*cmdaddr = ((SDIO_LOCAL_DEVICE_ID << 13) | (addr & SDIO_LOCAL_MSK));
		break;

	case WLAN_IOREG_DEVICE_ID:
		*cmdaddr = ((WLAN_IOREG_DEVICE_ID << 13) | (addr & WLAN_IOREG_MSK));
		break;

	case WLAN_TX_HIQ_DEVICE_ID:
		*cmdaddr = ((WLAN_TX_HIQ_DEVICE_ID << 13) | (addr & WLAN_FIFO_MSK));
		break;

	case WLAN_TX_MIQ_DEVICE_ID:
		*cmdaddr = ((WLAN_TX_MIQ_DEVICE_ID << 13) | (addr & WLAN_FIFO_MSK));
		break;

	case WLAN_TX_LOQ_DEVICE_ID:
		*cmdaddr = ((WLAN_TX_LOQ_DEVICE_ID << 13) | (addr & WLAN_FIFO_MSK));
		break;

	case WLAN_RX0FF_DEVICE_ID:
		*cmdaddr = ((WLAN_RX0FF_DEVICE_ID << 13) | (addr & WLAN_RX0FF_MSK));
		break;

	default:
		break;
	}
}

static u8 get_deviceid(u32 addr)
{
	u8 devide_id;
	u16 pseudo_id;

	pseudo_id = (u16)(addr >> 16);
	switch (pseudo_id) {
	case 0x1025:
		devide_id = SDIO_LOCAL_DEVICE_ID;
		break;

	case 0x1026:
		devide_id = WLAN_IOREG_DEVICE_ID;
		break;

	case 0x1031:
		devide_id = WLAN_TX_HIQ_DEVICE_ID;
		break;

	case 0x1032:
		devide_id = WLAN_TX_MIQ_DEVICE_ID;
		break;

	case 0x1033:
		devide_id = WLAN_TX_LOQ_DEVICE_ID;
		break;

	case 0x1034:
		devide_id = WLAN_RX0FF_DEVICE_ID;
		break;

	default:
		devide_id = WLAN_IOREG_DEVICE_ID;
		break;
	}

	return devide_id;
}

/*
 * Ref:
 *HalSdioGetCmdAddr8723BSdio()
 */
static u32 _cvrt2ftaddr(const u32 addr, u8 *pdevice_id, u16 *poffset)
{
	u8 device_id;
	u16 offset;
	u32 ftaddr;

	device_id = get_deviceid(addr);
	offset = 0;

	switch (device_id) {
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
		device_id = WLAN_IOREG_DEVICE_ID;
		offset = addr & WLAN_IOREG_MSK;
		break;
	}
	ftaddr = (device_id << 13) | offset;

	if (pdevice_id)
		*pdevice_id = device_id;
	if (poffset)
		*poffset = offset;

	return ftaddr;
}

static u8 sdio_read8(struct intf_hdl *intfhdl, u32 addr)
{
	u32 ftaddr;
	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);

	return sd_read8(intfhdl, ftaddr, NULL);
}

static u16 sdio_read16(struct intf_hdl *intfhdl, u32 addr)
{
	u32 ftaddr;
	__le16 le_tmp;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	sd_cmd52_read(intfhdl, ftaddr, 2, (u8 *)&le_tmp);

	return le16_to_cpu(le_tmp);
}

static u32 sdio_read32(struct intf_hdl *intfhdl, u32 addr)
{
	struct adapter *adapter;
	u8 mac_pwr_ctrl_on;
	u8 device_id;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	u32 val;
	s32 err;
	__le32 le_tmp;

	adapter = intfhdl->padapter;
	ftaddr = _cvrt2ftaddr(addr, &device_id, &offset);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (
		((device_id == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(!mac_pwr_ctrl_on) ||
		(adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
	) {
		err = sd_cmd52_read(intfhdl, ftaddr, 4, (u8 *)&le_tmp);
#ifdef SDIO_DEBUG_IO
		if (!err) {
#endif
			return le32_to_cpu(le_tmp);
#ifdef SDIO_DEBUG_IO
		}

		DBG_8192C(KERN_ERR "%s: Mac Power off, Read FAIL(%d)! addr = 0x%x\n", __func__, err, addr);
		return SDIO_ERR_VAL32;
#endif
	}

	/*  4 bytes alignment */
	shift = ftaddr & 0x3;
	if (shift == 0) {
		val = sd_read32(intfhdl, ftaddr, NULL);
	} else {
		u8 *tmpbuf;

		tmpbuf = rtw_malloc(8);
		if (!tmpbuf) {
			DBG_8192C(KERN_ERR "%s: Allocate memory FAIL!(size =8) addr = 0x%x\n", __func__, addr);
			return SDIO_ERR_VAL32;
		}

		ftaddr &= ~(u16)0x3;
		sd_read(intfhdl, ftaddr, 8, tmpbuf);
		memcpy(&le_tmp, tmpbuf + shift, 4);
		val = le32_to_cpu(le_tmp);

		kfree(tmpbuf);
	}
	return val;
}

static s32 sdio_readN(struct intf_hdl *intfhdl, u32 addr, u32 cnt, u8 *buf)
{
	struct adapter *adapter;
	u8 mac_pwr_ctrl_on;
	u8 device_id;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

	adapter = intfhdl->padapter;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &device_id, &offset);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (
		((device_id == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(!mac_pwr_ctrl_on) ||
		(adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
	)
		return sd_cmd52_read(intfhdl, ftaddr, cnt, buf);

	/*  4 bytes alignment */
	shift = ftaddr & 0x3;
	if (shift == 0) {
		err = sd_read(intfhdl, ftaddr, cnt, buf);
	} else {
		u8 *tmpbuf;
		u32 n;

		ftaddr &= ~(u16)0x3;
		n = cnt + shift;
		tmpbuf = rtw_malloc(n);
		if (!tmpbuf)
			return -1;

		err = sd_read(intfhdl, ftaddr, n, tmpbuf);
		if (!err)
			memcpy(buf, tmpbuf + shift, cnt);
		kfree(tmpbuf);
	}
	return err;
}

static s32 sdio_write8(struct intf_hdl *intfhdl, u32 addr, u8 val)
{
	u32 ftaddr;
	s32 err;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	sd_write8(intfhdl, ftaddr, val, &err);

	return err;
}

static s32 sdio_write16(struct intf_hdl *intfhdl, u32 addr, u16 val)
{
	u32 ftaddr;
	__le16 le_tmp;

	ftaddr = _cvrt2ftaddr(addr, NULL, NULL);
	le_tmp = cpu_to_le16(val);
	return sd_cmd52_write(intfhdl, ftaddr, 2, (u8 *)&le_tmp);
}

static s32 sdio_write32(struct intf_hdl *intfhdl, u32 addr, u32 val)
{
	struct adapter *adapter;
	u8 mac_pwr_ctrl_on;
	u8 device_id;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;
	__le32 le_tmp;

	adapter = intfhdl->padapter;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &device_id, &offset);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (
		((device_id == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(!mac_pwr_ctrl_on) ||
		(adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
	) {
		le_tmp = cpu_to_le32(val);

		return sd_cmd52_write(intfhdl, ftaddr, 4, (u8 *)&le_tmp);
	}

	/*  4 bytes alignment */
	shift = ftaddr & 0x3;
	if (shift == 0) {
		sd_write32(intfhdl, ftaddr, val, &err);
	} else {
		le_tmp = cpu_to_le32(val);
		err = sd_cmd52_write(intfhdl, ftaddr, 4, (u8 *)&le_tmp);
	}
	return err;
}

static s32 sdio_writeN(struct intf_hdl *intfhdl, u32 addr, u32 cnt, u8 *buf)
{
	struct adapter *adapter;
	u8 mac_pwr_ctrl_on;
	u8 device_id;
	u16 offset;
	u32 ftaddr;
	u8 shift;
	s32 err;

	adapter = intfhdl->padapter;
	err = 0;

	ftaddr = _cvrt2ftaddr(addr, &device_id, &offset);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (
		((device_id == WLAN_IOREG_DEVICE_ID) && (offset < 0x100)) ||
		(!mac_pwr_ctrl_on) ||
		(adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
	)
		return sd_cmd52_write(intfhdl, ftaddr, cnt, buf);

	shift = ftaddr & 0x3;
	if (shift == 0) {
		err = sd_write(intfhdl, ftaddr, cnt, buf);
	} else {
		u8 *tmpbuf;
		u32 n;

		ftaddr &= ~(u16)0x3;
		n = cnt + shift;
		tmpbuf = rtw_malloc(n);
		if (!tmpbuf)
			return -1;
		err = sd_read(intfhdl, ftaddr, 4, tmpbuf);
		if (err) {
			kfree(tmpbuf);
			return err;
		}
		memcpy(tmpbuf + shift, buf, cnt);
		err = sd_write(intfhdl, ftaddr, n, tmpbuf);
		kfree(tmpbuf);
	}
	return err;
}

static u8 sdio_f0_read8(struct intf_hdl *intfhdl, u32 addr)
{
	return sd_f0_read8(intfhdl, addr, NULL);
}

static void sdio_read_mem(
	struct intf_hdl *intfhdl,
	u32 addr,
	u32 cnt,
	u8 *rmem
)
{
	s32 err;

	err = sdio_readN(intfhdl, addr, cnt, rmem);
	/* TODO: Report error is err not zero */
}

static void sdio_write_mem(
	struct intf_hdl *intfhdl,
	u32 addr,
	u32 cnt,
	u8 *wmem
)
{
	sdio_writeN(intfhdl, addr, cnt, wmem);
}

/*
 * Description:
 *Read from RX FIFO
 *Round read size to block size,
 *and make sure data transfer will be done in one command.
 *
 * Parameters:
 *intfhdl	a pointer of intf_hdl
 *addr		port ID
 *cnt			size to read
 *rmem		address to put data
 *
 * Return:
 *_SUCCESS(1)		Success
 *_FAIL(0)		Fail
 */
static u32 sdio_read_port(
	struct intf_hdl *intfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem
)
{
	struct adapter *adapter;
	struct sdio_data *psdio;
	struct hal_com_data *hal;
	s32 err;

	adapter = intfhdl->padapter;
	psdio = &adapter_to_dvobj(adapter)->intf_data;
	hal = GET_HAL_DATA(adapter);

	HalSdioGetCmdAddr8723BSdio(adapter, addr, hal->SdioRxFIFOCnt++, &addr);

	if (cnt > psdio->block_transfer_len)
		cnt = _RND(cnt, psdio->block_transfer_len);

	err = _sd_read(intfhdl, addr, cnt, mem);

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
 *intfhdl	a pointer of intf_hdl
 *addr		port ID
 *cnt			size to write
 *wmem		data pointer to write
 *
 * Return:
 *_SUCCESS(1)		Success
 *_FAIL(0)		Fail
 */
static u32 sdio_write_port(
	struct intf_hdl *intfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem
)
{
	struct adapter *adapter;
	struct sdio_data *psdio;
	s32 err;
	struct xmit_buf *xmitbuf = (struct xmit_buf *)mem;

	adapter = intfhdl->padapter;
	psdio = &adapter_to_dvobj(adapter)->intf_data;

	if (!adapter->hw_init_completed) {
		DBG_871X("%s [addr = 0x%x cnt =%d] adapter->hw_init_completed == false\n", __func__, addr, cnt);
		return _FAIL;
	}

	cnt = round_up(cnt, 4);
	HalSdioGetCmdAddr8723BSdio(adapter, addr, cnt >> 2, &addr);

	if (cnt > psdio->block_transfer_len)
		cnt = _RND(cnt, psdio->block_transfer_len);

	err = sd_write(intfhdl, addr, cnt, xmitbuf->pdata);

	rtw_sctx_done_err(
		&xmitbuf->sctx,
		err ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS
	);

	if (err)
		return _FAIL;
	return _SUCCESS;
}

void sdio_set_intf_ops(struct adapter *adapter, struct _io_ops *ops)
{
	ops->_read8 = &sdio_read8;
	ops->_read16 = &sdio_read16;
	ops->_read32 = &sdio_read32;
	ops->_read_mem = &sdio_read_mem;
	ops->_read_port = &sdio_read_port;

	ops->_write8 = &sdio_write8;
	ops->_write16 = &sdio_write16;
	ops->_write32 = &sdio_write32;
	ops->_writeN = &sdio_writeN;
	ops->_write_mem = &sdio_write_mem;
	ops->_write_port = &sdio_write_port;

	ops->_sd_f0_read8 = sdio_f0_read8;
}

/*
 * Todo: align address to 4 bytes.
 */
static s32 _sdio_local_read(
	struct adapter *adapter,
	u32 addr,
	u32 cnt,
	u8 *buf
)
{
	struct intf_hdl *intfhdl;
	u8 mac_pwr_ctrl_on;
	s32 err;
	u8 *tmpbuf;
	u32 n;

	intfhdl = &adapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (!mac_pwr_ctrl_on)
		return _sd_cmd52_read(intfhdl, addr, cnt, buf);

	n = round_up(cnt, 4);
	tmpbuf = rtw_malloc(n);
	if (!tmpbuf)
		return -1;

	err = _sd_read(intfhdl, addr, n, tmpbuf);
	if (!err)
		memcpy(buf, tmpbuf, cnt);

	kfree(tmpbuf);

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 sdio_local_read(
	struct adapter *adapter,
	u32 addr,
	u32 cnt,
	u8 *buf
)
{
	struct intf_hdl *intfhdl;
	u8 mac_pwr_ctrl_on;
	s32 err;
	u8 *tmpbuf;
	u32 n;

	intfhdl = &adapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (
		(!mac_pwr_ctrl_on) ||
		(adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
	)
		return sd_cmd52_read(intfhdl, addr, cnt, buf);

	n = round_up(cnt, 4);
	tmpbuf = rtw_malloc(n);
	if (!tmpbuf)
		return -1;

	err = sd_read(intfhdl, addr, n, tmpbuf);
	if (!err)
		memcpy(buf, tmpbuf, cnt);

	kfree(tmpbuf);

	return err;
}

/*
 * Todo: align address to 4 bytes.
 */
s32 sdio_local_write(
	struct adapter *adapter,
	u32 addr,
	u32 cnt,
	u8 *buf
)
{
	struct intf_hdl *intfhdl;
	u8 mac_pwr_ctrl_on;
	s32 err;
	u8 *tmpbuf;

	if (addr & 0x3)
		DBG_8192C("%s, address must be 4 bytes alignment\n", __func__);

	if (cnt  & 0x3)
		DBG_8192C("%s, size must be the multiple of 4\n", __func__);

	intfhdl = &adapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (
		(!mac_pwr_ctrl_on) ||
		(adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
	)
		return sd_cmd52_write(intfhdl, addr, cnt, buf);

	tmpbuf = rtw_malloc(cnt);
	if (!tmpbuf)
		return -1;

	memcpy(tmpbuf, buf, cnt);

	err = sd_write(intfhdl, addr, cnt, tmpbuf);

	kfree(tmpbuf);

	return err;
}

u8 SdioLocalCmd52Read1Byte(struct adapter *adapter, u32 addr)
{
	u8 val = 0;
	struct intf_hdl *intfhdl = &adapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(intfhdl, addr, 1, &val);

	return val;
}

static u16 SdioLocalCmd52Read2Byte(struct adapter *adapter, u32 addr)
{
	__le16 val = 0;
	struct intf_hdl *intfhdl = &adapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_read(intfhdl, addr, 2, (u8 *)&val);

	return le16_to_cpu(val);
}

static u32 SdioLocalCmd53Read4Byte(struct adapter *adapter, u32 addr)
{

	u8 mac_pwr_ctrl_on;
	u32 val = 0;
	struct intf_hdl *intfhdl = &adapter->iopriv.intf;
	__le32 le_tmp;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &mac_pwr_ctrl_on);
	if (!mac_pwr_ctrl_on || adapter_to_pwrctl(adapter)->bFwCurrentInPSMode) {
		sd_cmd52_read(intfhdl, addr, 4, (u8 *)&le_tmp);
		val = le32_to_cpu(le_tmp);
	} else {
		val = sd_read32(intfhdl, addr, NULL);
	}
	return val;
}

void SdioLocalCmd52Write1Byte(struct adapter *adapter, u32 addr, u8 v)
{
	struct intf_hdl *intfhdl = &adapter->iopriv.intf;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	sd_cmd52_write(intfhdl, addr, 1, &v);
}

static void SdioLocalCmd52Write4Byte(struct adapter *adapter, u32 addr, u32 v)
{
	struct intf_hdl *intfhdl = &adapter->iopriv.intf;
	__le32 le_tmp;

	HalSdioGetCmdAddr8723BSdio(adapter, SDIO_LOCAL_DEVICE_ID, addr, &addr);
	le_tmp = cpu_to_le32(v);
	sd_cmd52_write(intfhdl, addr, 4, (u8 *)&le_tmp);
}

static s32 ReadInterrupt8723BSdio(struct adapter *adapter, u32 *phisr)
{
	u32 hisr, himr;
	u8 val8, hisr_len;

	if (!phisr)
		return false;

	himr = GET_HAL_DATA(adapter)->sdio_himr;

	/*  decide how many bytes need to be read */
	hisr_len = 0;
	while (himr) {
		hisr_len++;
		himr >>= 8;
	}

	hisr = 0;
	while (hisr_len != 0) {
		hisr_len--;
		val8 = SdioLocalCmd52Read1Byte(adapter, SDIO_REG_HISR + hisr_len);
		hisr |= (val8 << (8 * hisr_len));
	}

	*phisr = hisr;

	return true;
}

/*  */
/*	Description: */
/*		Initialize SDIO Host Interrupt Mask configuration variables for future use. */
/*  */
/*	Assumption: */
/*		Using SDIO Local register ONLY for configuration. */
/*  */
/*	Created by Roger, 2011.02.11. */
/*  */
void InitInterrupt8723BSdio(struct adapter *adapter)
{
	struct hal_com_data *haldata;

	haldata = GET_HAL_DATA(adapter);
	haldata->sdio_himr = (u32)(SDIO_HIMR_RX_REQUEST_MSK	|
				   SDIO_HIMR_AVAL_MSK		|
				   0);
}

/*  */
/*	Description: */
/*		Initialize System Host Interrupt Mask configuration variables for future use. */
/*  */
/*	Created by Roger, 2011.08.03. */
/*  */
void InitSysInterrupt8723BSdio(struct adapter *adapter)
{
	struct hal_com_data *haldata;

	haldata = GET_HAL_DATA(adapter);

	haldata->SysIntrMask = (0);
}

/*  */
/*	Description: */
/*		Enalbe SDIO Host Interrupt Mask configuration on SDIO local domain. */
/*  */
/*	Assumption: */
/*		1. Using SDIO Local register ONLY for configuration. */
/*		2. PASSIVE LEVEL */
/*  */
/*	Created by Roger, 2011.02.11. */
/*  */
void EnableInterrupt8723BSdio(struct adapter *adapter)
{
	struct hal_com_data *haldata;
	__le32 himr;
	u32 tmp;

	haldata = GET_HAL_DATA(adapter);

	himr = cpu_to_le32(haldata->sdio_himr);
	sdio_local_write(adapter, SDIO_REG_HIMR, 4, (u8 *)&himr);

	RT_TRACE(
		_module_hci_ops_c_,
		_drv_notice_,
		(
			"%s: enable SDIO HIMR = 0x%08X\n",
			__func__,
			haldata->sdio_himr
		)
	);

	/*  Update current system IMR settings */
	tmp = rtw_read32(adapter, REG_HSIMR);
	rtw_write32(adapter, REG_HSIMR, tmp | haldata->SysIntrMask);

	RT_TRACE(
		_module_hci_ops_c_,
		_drv_notice_,
		(
			"%s: enable HSIMR = 0x%08X\n",
			__func__,
			haldata->SysIntrMask
		)
	);

	/*  */
	/*  <Roger_Notes> There are some C2H CMDs have been sent before system interrupt is enabled, e.g., C2H, CPWM. */
	/*  So we need to clear all C2H events that FW has notified, otherwise FW won't schedule any commands anymore. */
	/*  2011.10.19. */
	/*  */
	rtw_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

/*  */
/*	Description: */
/*		Disable SDIO Host IMR configuration to mask unnecessary interrupt service. */
/*  */
/*	Assumption: */
/*		Using SDIO Local register ONLY for configuration. */
/*  */
/*	Created by Roger, 2011.02.11. */
/*  */
void DisableInterrupt8723BSdio(struct adapter *adapter)
{
	__le32 himr;

	himr = cpu_to_le32(SDIO_HIMR_DISABLED);
	sdio_local_write(adapter, SDIO_REG_HIMR, 4, (u8 *)&himr);
}

/*  */
/*	Description: */
/*		Using 0x100 to check the power status of FW. */
/*  */
/*	Assumption: */
/*		Using SDIO Local register ONLY for configuration. */
/*  */
/*	Created by Isaac, 2013.09.10. */
/*  */
u8 CheckIPSStatus(struct adapter *adapter)
{
	DBG_871X(
		"%s(): Read 0x100 = 0x%02x 0x86 = 0x%02x\n",
		__func__,
		rtw_read8(adapter, 0x100),
		rtw_read8(adapter, 0x86)
	);

	if (rtw_read8(adapter, 0x100) == 0xEA)
		return true;
	else
		return false;
}

static struct recv_buf *sd_recv_rxfifo(struct adapter *adapter, u32 size)
{
	u32 readsize, ret;
	u8 *readbuf;
	struct recv_priv *recv_priv;
	struct recv_buf	*recvbuf;

	/*  Patch for some SDIO Host 4 bytes issue */
	/*  ex. RK3188 */
	readsize = round_up(size, 4);

	/* 3 1. alloc recvbuf */
	recv_priv = &adapter->recvpriv;
	recvbuf = rtw_dequeue_recvbuf(&recv_priv->free_recv_buf_queue);
	if (!recvbuf) {
		DBG_871X_LEVEL(_drv_err_, "%s: alloc recvbuf FAIL!\n", __func__);
		return NULL;
	}

	/* 3 2. alloc skb */
	if (!recvbuf->pskb) {
		SIZE_PTR tmpaddr = 0;
		SIZE_PTR alignment = 0;

		recvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

		if (recvbuf->pskb) {
			recvbuf->pskb->dev = adapter->pnetdev;

			tmpaddr = (SIZE_PTR)recvbuf->pskb->data;
			alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
			skb_reserve(recvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
		}

		if (!recvbuf->pskb) {
			DBG_871X("%s: alloc_skb fail! read =%d\n", __func__, readsize);
			return NULL;
		}
	}

	/* 3 3. read data from rxfifo */
	readbuf = recvbuf->pskb->data;
	ret = sdio_read_port(&adapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, readsize, readbuf);
	if (ret == _FAIL) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("%s: read port FAIL!\n", __func__));
		return NULL;
	}

	/* 3 4. init recvbuf */
	recvbuf->len = size;
	recvbuf->phead = recvbuf->pskb->head;
	recvbuf->pdata = recvbuf->pskb->data;
	skb_set_tail_pointer(recvbuf->pskb, size);
	recvbuf->ptail = skb_tail_pointer(recvbuf->pskb);
	recvbuf->pend = skb_end_pointer(recvbuf->pskb);

	return recvbuf;
}

static void sd_rxhandler(struct adapter *adapter, struct recv_buf *recvbuf)
{
	struct recv_priv *recv_priv;
	struct __queue *pending_queue;

	recv_priv = &adapter->recvpriv;
	pending_queue = &recv_priv->recv_buf_pending_queue;

	/* 3 1. enqueue recvbuf */
	rtw_enqueue_recvbuf(recvbuf, pending_queue);

	/* 3 2. schedule tasklet */
	tasklet_schedule(&recv_priv->recv_tasklet);
}

void sd_int_dpc(struct adapter *adapter)
{
	struct hal_com_data *hal;
	struct dvobj_priv *dvobj;
	struct intf_hdl *intfhdl = &adapter->iopriv.intf;
	struct pwrctrl_priv *pwrctl;

	hal = GET_HAL_DATA(adapter);
	dvobj = adapter_to_dvobj(adapter);
	pwrctl = dvobj_to_pwrctl(dvobj);

	if (hal->sdio_hisr & SDIO_HISR_AVAL) {
		u8 freepage[4];

		_sdio_local_read(adapter, SDIO_REG_FREE_TXPG, 4, freepage);
		complete(&(adapter->xmitpriv.xmit_comp));
	}

	if (hal->sdio_hisr & SDIO_HISR_CPWM1) {
		struct reportpwrstate_parm report;

		del_timer_sync(&(pwrctl->pwr_rpwm_timer));

		report.state = SdioLocalCmd52Read1Byte(adapter, SDIO_REG_HCPWM1_8723B);

		_set_workitem(&(pwrctl->cpwm_event));
	}

	if (hal->sdio_hisr & SDIO_HISR_TXERR) {
		u8 *status;
		u32 addr;

		status = rtw_malloc(4);
		if (status) {
			addr = REG_TXDMA_STATUS;
			HalSdioGetCmdAddr8723BSdio(adapter, WLAN_IOREG_DEVICE_ID, addr, &addr);
			_sd_read(intfhdl, addr, 4, status);
			_sd_write(intfhdl, addr, 4, status);
			DBG_8192C("%s: SDIO_HISR_TXERR (0x%08x)\n", __func__, le32_to_cpu(*(u32 *)status));
			kfree(status);
		} else {
			DBG_8192C("%s: SDIO_HISR_TXERR, but can't allocate memory to read status!\n", __func__);
		}
	}

	if (hal->sdio_hisr & SDIO_HISR_TXBCNOK)
		DBG_8192C("%s: SDIO_HISR_TXBCNOK\n", __func__);

	if (hal->sdio_hisr & SDIO_HISR_TXBCNERR)
		DBG_8192C("%s: SDIO_HISR_TXBCNERR\n", __func__);
#ifndef CONFIG_C2H_PACKET_EN
	if (hal->sdio_hisr & SDIO_HISR_C2HCMD) {
		struct c2h_evt_hdr_88xx *c2h_evt;

		DBG_8192C("%s: C2H Command\n", __func__);
		c2h_evt = rtw_zmalloc(16);
		if (c2h_evt) {
			if (c2h_evt_read_88xx(adapter, (u8 *)c2h_evt) == _SUCCESS) {
				if (c2h_id_filter_ccx_8723b((u8 *)c2h_evt)) {
					/* Handle CCX report here */
					rtw_hal_c2h_handler(adapter, (u8 *)c2h_evt);
					kfree(c2h_evt);
				} else {
					rtw_c2h_wk_cmd(adapter, (u8 *)c2h_evt);
				}
			}
		} else {
			/* Error handling for malloc fail */
			if (rtw_cbuf_push(adapter->evtpriv.c2h_queue, NULL) != _SUCCESS)
				DBG_871X("%s rtw_cbuf_push fail\n", __func__);
			_set_workitem(&adapter->evtpriv.c2h_wk);
		}
	}
#endif

	if (hal->sdio_hisr & SDIO_HISR_RXFOVW)
		DBG_8192C("%s: Rx Overflow\n", __func__);

	if (hal->sdio_hisr & SDIO_HISR_RXERR)
		DBG_8192C("%s: Rx Error\n", __func__);

	if (hal->sdio_hisr & SDIO_HISR_RX_REQUEST) {
		struct recv_buf *recvbuf;
		int alloc_fail_time = 0;
		u32 hisr;

		hal->sdio_hisr ^= SDIO_HISR_RX_REQUEST;
		do {
			hal->SdioRxFIFOSize = SdioLocalCmd52Read2Byte(adapter, SDIO_REG_RX0_REQ_LEN);
			if (hal->SdioRxFIFOSize != 0) {
				recvbuf = sd_recv_rxfifo(adapter, hal->SdioRxFIFOSize);
				if (recvbuf)
					sd_rxhandler(adapter, recvbuf);
				else {
					alloc_fail_time++;
					DBG_871X("recvbuf is Null for %d times because alloc memory failed\n", alloc_fail_time);
					if (alloc_fail_time >= 10)
						break;
				}
				hal->SdioRxFIFOSize = 0;
			} else
				break;

			hisr = 0;
			ReadInterrupt8723BSdio(adapter, &hisr);
			hisr &= SDIO_HISR_RX_REQUEST;
			if (!hisr)
				break;
		} while (1);

		if (alloc_fail_time == 10)
			DBG_871X("exit because alloc memory failed more than 10 times\n");

	}
}

void sd_int_hdl(struct adapter *adapter)
{
	struct hal_com_data *hal;

	if (
		(adapter->bDriverStopped) || (adapter->bSurpriseRemoved)
	)
		return;

	hal = GET_HAL_DATA(adapter);

	hal->sdio_hisr = 0;
	ReadInterrupt8723BSdio(adapter, &hal->sdio_hisr);

	if (hal->sdio_hisr & hal->sdio_himr) {
		u32 v32;

		hal->sdio_hisr &= hal->sdio_himr;

		/*  clear HISR */
		v32 = hal->sdio_hisr & MASK_SDIO_HISR_CLEAR;
		if (v32)
			SdioLocalCmd52Write4Byte(adapter, SDIO_REG_HISR, v32);

		sd_int_dpc(adapter);
	} else {
		RT_TRACE(_module_hci_ops_c_, _drv_err_,
				("%s: HISR(0x%08x) and HIMR(0x%08x) not match!\n",
				__func__, hal->sdio_hisr, hal->sdio_himr));
	}
}

/*  */
/*	Description: */
/*		Query SDIO Local register to query current the number of Free TxPacketBuffer page. */
/*  */
/*	Assumption: */
/*		1. Running at PASSIVE_LEVEL */
/*		2. RT_TX_SPINLOCK is NOT acquired. */
/*  */
/*	Created by Roger, 2011.01.28. */
/*  */
u8 HalQueryTxBufferStatus8723BSdio(struct adapter *adapter)
{
	struct hal_com_data *hal;
	u32 numof_free_page;

	hal = GET_HAL_DATA(adapter);

	numof_free_page = SdioLocalCmd53Read4Byte(adapter, SDIO_REG_FREE_TXPG);

	memcpy(hal->SdioTxFIFOFreePage, &numof_free_page, 4);
	RT_TRACE(_module_hci_ops_c_, _drv_notice_,
			("%s: Free page for HIQ(%#x), MIDQ(%#x), LOWQ(%#x), PUBQ(%#x)\n",
			__func__,
			hal->SdioTxFIFOFreePage[HI_QUEUE_IDX],
			hal->SdioTxFIFOFreePage[MID_QUEUE_IDX],
			hal->SdioTxFIFOFreePage[LOW_QUEUE_IDX],
			hal->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX]));

	return true;
}

/*  */
/*	Description: */
/*		Query SDIO Local register to get the current number of TX OQT Free Space. */
/*  */
void HalQueryTxOQTBufferStatus8723BSdio(struct adapter *adapter)
{
	struct hal_com_data *haldata = GET_HAL_DATA(adapter);

	haldata->SdioTxOQTFreeSpace = SdioLocalCmd52Read1Byte(adapter, SDIO_REG_OQT_FREE_PG);
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
u8 RecvOnePkt(struct adapter *adapter, u32 size)
{
	struct recv_buf *recvbuf;
	struct dvobj_priv *sddev;
	struct sdio_func *func;

	u8 res = false;

	DBG_871X("+%s: size: %d+\n", __func__, size);

	if (!adapter) {
		DBG_871X(KERN_ERR "%s: adapter is NULL!\n", __func__);
		return false;
	}

	sddev = adapter_to_dvobj(adapter);
	psdio_data = &sddev->intf_data;
	func = psdio_data->func;

	if (size) {
		sdio_claim_host(func);
		recvbuf = sd_recv_rxfifo(adapter, size);

		if (recvbuf) {
			sd_rxhandler(adapter, recvbuf);
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
