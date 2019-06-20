/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
/*

The purpose of rtw_io.c

a. provides the API

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface


Compiler Flag Option:

1. CONFIG_SDIO_HCI:
    a. USE_SYNC_IRP:  Only sync operations are provided.
    b. USE_ASYNC_IRP:Both sync/async operations are provided.

2. CONFIG_USB_HCI:
   a. USE_ASYNC_IRP: Both sync/async operations are provided.

3. CONFIG_CFIO_HCI:
   b. USE_SYNC_IRP: Only sync operations are provided.


Only sync read/rtw_write_mem operations are provided.

jackson@realtek.com.tw

*/

#define _RTW_IO_C_

#include <drv_types.h>
#include <hal_data.h>

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_PLATFORM_RTL8197D)
	#define rtw_le16_to_cpu(val)		val
	#define rtw_le32_to_cpu(val)		val
	#define rtw_cpu_to_le16(val)		val
	#define rtw_cpu_to_le32(val)		val
#else
	#define rtw_le16_to_cpu(val)		le16_to_cpu(val)
	#define rtw_le32_to_cpu(val)		le32_to_cpu(val)
	#define rtw_cpu_to_le16(val)		cpu_to_le16(val)
	#define rtw_cpu_to_le32(val)		cpu_to_le32(val)
#endif


u8 _rtw_read8(_adapter *adapter, u32 addr)
{
	u8 r_val;
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u8(*_read8)(struct intf_hdl *pintfhdl, u32 addr);
	_read8 = pintfhdl->io_ops._read8;

	r_val = _read8(pintfhdl, addr);
	return r_val;
}

u16 _rtw_read16(_adapter *adapter, u32 addr)
{
	u16 r_val;
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u16(*_read16)(struct intf_hdl *pintfhdl, u32 addr);
	_read16 = pintfhdl->io_ops._read16;

	r_val = _read16(pintfhdl, addr);
	return rtw_le16_to_cpu(r_val);
}

u32 _rtw_read32(_adapter *adapter, u32 addr)
{
	u32 r_val;
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32(*_read32)(struct intf_hdl *pintfhdl, u32 addr);
	_read32 = pintfhdl->io_ops._read32;

	r_val = _read32(pintfhdl, addr);
	return rtw_le32_to_cpu(r_val);

}

int _rtw_write8(_adapter *adapter, u32 addr, u8 val)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret;
	_write8 = pintfhdl->io_ops._write8;

	ret = _write8(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write16(_adapter *adapter, u32 addr, u16 val)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret;
	_write16 = pintfhdl->io_ops._write16;

	val = rtw_cpu_to_le16(val);
	ret = _write16(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write32(_adapter *adapter, u32 addr, u32 val)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret;
	_write32 = pintfhdl->io_ops._write32;

	val = rtw_cpu_to_le32(val);
	ret = _write32(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

int _rtw_writeN(_adapter *adapter, u32 addr , u32 length , u8 *pdata)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl	*pintfhdl = (struct intf_hdl *)(&(pio_priv->intf));
	int (*_writeN)(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata);
	int ret;
	_writeN = pintfhdl->io_ops._writeN;

	ret = _writeN(pintfhdl, addr, length, pdata);

	return RTW_STATUS_CODE(ret);
}

#ifdef CONFIG_SDIO_HCI
u8 _rtw_sd_f0_read8(_adapter *adapter, u32 addr)
{
	u8 r_val = 0x00;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	u8(*_sd_f0_read8)(struct intf_hdl *pintfhdl, u32 addr);

	_sd_f0_read8 = pintfhdl->io_ops._sd_f0_read8;

	if (_sd_f0_read8)
		r_val = _sd_f0_read8(pintfhdl, addr);
	else
		RTW_WARN(FUNC_ADPT_FMT" _sd_f0_read8 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return r_val;
}

#ifdef CONFIG_SDIO_INDIRECT_ACCESS
u8 _rtw_sd_iread8(_adapter *adapter, u32 addr)
{
	u8 r_val = 0x00;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	u8(*_sd_iread8)(struct intf_hdl *pintfhdl, u32 addr);

	_sd_iread8 = pintfhdl->io_ops._sd_iread8;

	if (_sd_iread8)
		r_val = _sd_iread8(pintfhdl, addr);
	else
		RTW_ERR(FUNC_ADPT_FMT" _sd_iread8 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return r_val;
}

u16 _rtw_sd_iread16(_adapter *adapter, u32 addr)
{
	u16 r_val = 0x00;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	u16(*_sd_iread16)(struct intf_hdl *pintfhdl, u32 addr);

	_sd_iread16 = pintfhdl->io_ops._sd_iread16;

	if (_sd_iread16)
		r_val = _sd_iread16(pintfhdl, addr);
	else
		RTW_ERR(FUNC_ADPT_FMT" _sd_iread16 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return r_val;
}

u32 _rtw_sd_iread32(_adapter *adapter, u32 addr)
{
	u32 r_val = 0x00;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	u32(*_sd_iread32)(struct intf_hdl *pintfhdl, u32 addr);

	_sd_iread32 = pintfhdl->io_ops._sd_iread32;

	if (_sd_iread32)
		r_val = _sd_iread32(pintfhdl, addr);
	else
		RTW_ERR(FUNC_ADPT_FMT" _sd_iread32 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return r_val;
}

int _rtw_sd_iwrite8(_adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	int (*_sd_iwrite8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret = -1;

	_sd_iwrite8 = pintfhdl->io_ops._sd_iwrite8;

	if (_sd_iwrite8)
		ret = _sd_iwrite8(pintfhdl, addr, val);
	else
		RTW_ERR(FUNC_ADPT_FMT" _sd_iwrite8 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return RTW_STATUS_CODE(ret);
}

int _rtw_sd_iwrite16(_adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	int (*_sd_iwrite16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret = -1;

	_sd_iwrite16 = pintfhdl->io_ops._sd_iwrite16;

	if (_sd_iwrite16)
		ret = _sd_iwrite16(pintfhdl, addr, val);
	else
		RTW_ERR(FUNC_ADPT_FMT" _sd_iwrite16 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return RTW_STATUS_CODE(ret);
}
int _rtw_sd_iwrite32(_adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	int (*_sd_iwrite32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret = -1;

	_sd_iwrite32 = pintfhdl->io_ops._sd_iwrite32;

	if (_sd_iwrite32)
		ret = _sd_iwrite32(pintfhdl, addr, val);
	else
		RTW_ERR(FUNC_ADPT_FMT" _sd_iwrite32 callback is NULL\n", FUNC_ADPT_ARG(adapter));

	return RTW_STATUS_CODE(ret);
}

#endif /* CONFIG_SDIO_INDIRECT_ACCESS */

#endif /* CONFIG_SDIO_HCI */

int _rtw_write8_async(_adapter *adapter, u32 addr, u8 val)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write8_async)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	int ret;
	_write8_async = pintfhdl->io_ops._write8_async;

	ret = _write8_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write16_async(_adapter *adapter, u32 addr, u16 val)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write16_async)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	int ret;
	_write16_async = pintfhdl->io_ops._write16_async;
	val = rtw_cpu_to_le16(val);
	ret = _write16_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}
int _rtw_write32_async(_adapter *adapter, u32 addr, u32 val)
{
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	int (*_write32_async)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	int ret;
	_write32_async = pintfhdl->io_ops._write32_async;
	val = rtw_cpu_to_le32(val);
	ret = _write32_async(pintfhdl, addr, val);

	return RTW_STATUS_CODE(ret);
}

void _rtw_read_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	void (*_read_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);


	if (RTW_CANNOT_RUN(adapter)) {
		return;
	}

	_read_mem = pintfhdl->io_ops._read_mem;

	_read_mem(pintfhdl, addr, cnt, pmem);


}

void _rtw_write_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	void (*_write_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);


	_write_mem = pintfhdl->io_ops._write_mem;

	_write_mem(pintfhdl, addr, cnt, pmem);


}

void _rtw_read_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32(*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);


	if (RTW_CANNOT_RUN(adapter)) {
		return;
	}

	_read_port = pintfhdl->io_ops._read_port;

	_read_port(pintfhdl, addr, cnt, pmem);


}

void _rtw_read_port_cancel(_adapter *adapter)
{
	void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);

	_read_port_cancel = pintfhdl->io_ops._read_port_cancel;

	RTW_DISABLE_FUNC(adapter, DF_RX_BIT);

	if (_read_port_cancel)
		_read_port_cancel(pintfhdl);
}

u32 _rtw_write_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	u32(*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	/* struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue; */
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32 ret = _SUCCESS;


	_write_port = pintfhdl->io_ops._write_port;

	ret = _write_port(pintfhdl, addr, cnt, pmem);


	return ret;
}

u32 _rtw_write_port_and_wait(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem, int timeout_ms)
{
	int ret = _SUCCESS;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pmem;
	struct submit_ctx sctx;

	rtw_sctx_init(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = _rtw_write_port(adapter, addr, cnt, pmem);

	if (ret == _SUCCESS) {
		ret = rtw_sctx_wait(&sctx, __func__);

		if (ret != _SUCCESS)
			pxmitbuf->sctx = NULL;
	}

	return ret;
}

void _rtw_write_port_cancel(_adapter *adapter)
{
	void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);

	_write_port_cancel = pintfhdl->io_ops._write_port_cancel;

	RTW_DISABLE_FUNC(adapter, DF_TX_BIT);

	if (_write_port_cancel)
		_write_port_cancel(pintfhdl);
}
int rtw_init_io_priv(_adapter *padapter, void (*set_intf_ops)(_adapter *padapter, struct _io_ops *pops))
{
	struct io_priv	*piopriv = &padapter->iopriv;
	struct intf_hdl *pintf = &piopriv->intf;

	if (set_intf_ops == NULL)
		return _FAIL;

	piopriv->padapter = padapter;
	pintf->padapter = padapter;
	pintf->pintf_dev = adapter_to_dvobj(padapter);

	set_intf_ops(padapter, &pintf->io_ops);

	return _SUCCESS;
}

/*
* Increase and check if the continual_io_error of this @param dvobjprive is larger than MAX_CONTINUAL_IO_ERR
* @return _TRUE:
* @return _FALSE:
*/
int rtw_inc_and_chk_continual_io_error(struct dvobj_priv *dvobj)
{
	int ret = _FALSE;
	int value;

	value = ATOMIC_INC_RETURN(&dvobj->continual_io_error);
	if (value > MAX_CONTINUAL_IO_ERR) {
		RTW_INFO("[dvobj:%p][ERROR] continual_io_error:%d > %d\n", dvobj, value, MAX_CONTINUAL_IO_ERR);
		ret = _TRUE;
	} else {
		/* RTW_INFO("[dvobj:%p] continual_io_error:%d\n", dvobj, value); */
	}
	return ret;
}

/*
* Set the continual_io_error of this @param dvobjprive to 0
*/
void rtw_reset_continual_io_error(struct dvobj_priv *dvobj)
{
	ATOMIC_SET(&dvobj->continual_io_error, 0);
}

#ifdef DBG_IO
#define RTW_IO_SNIFF_TYPE_RANGE	0 /* specific address range is accessed */
#define RTW_IO_SNIFF_TYPE_VALUE	1 /* value match for sniffed range */

struct rtw_io_sniff_ent {
	u8 chip;
	u8 hci;
	u32 addr;
	u8 type;
	union {
		u32 end_addr;
		struct {
			u32 mask;
			u32 val;
			bool equal;
		} vm; /* value match */
	} u;
	bool trace;
	char *tag;
};

#define RTW_IO_SNIFF_RANGE_ENT(_chip, _hci, _addr, _end_addr, _trace, _tag) \
	{.chip = _chip, .hci = _hci, .addr = _addr, .u.end_addr = _end_addr, .trace = _trace, .tag = _tag, .type = RTW_IO_SNIFF_TYPE_RANGE,}

#define RTW_IO_SNIFF_VALUE_ENT(_chip, _hci, _addr, _mask, _val, _equal, _trace, _tag) \
	{.chip = _chip, .hci = _hci, .addr = _addr, .u.vm.mask = _mask, .u.vm.val = _val, .u.vm.equal = _equal, .trace = _trace, .tag = _tag, .type = RTW_IO_SNIFF_TYPE_VALUE,}

/* part or all sniffed range is enabled (not all 0) */
#define RTW_IO_SNIFF_EN_ENT(_chip, _hci, _addr, _mask, _trace, _tag) \
	{.chip = _chip, .hci = _hci, .addr = _addr, .u.vm.mask = _mask, .u.vm.val = 0, .u.vm.equal = 0, .trace = _trace, .tag = _tag, .type = RTW_IO_SNIFF_TYPE_VALUE,}

/* part or all sniffed range is disabled (not all 1) */
#define RTW_IO_SNIFF_DIS_ENT(_chip, _hci, _addr, _mask, _trace, _tag) \
	{.chip = _chip, .hci = _hci, .addr = _addr, .u.vm.mask = _mask, .u.vm.val = 0xFFFFFFFF, .u.vm.equal = 0, .trace = _trace, .tag = _tag, .type = RTW_IO_SNIFF_TYPE_VALUE,}

const struct rtw_io_sniff_ent read_sniff[] = {
#ifdef DBG_IO_HCI_EN_CHK
	RTW_IO_SNIFF_EN_ENT(MAX_CHIP_TYPE, RTW_SDIO, 0x02, 0x1FC, 1, "SDIO 0x02[8:2] not all 0"),
	RTW_IO_SNIFF_EN_ENT(MAX_CHIP_TYPE, RTW_USB, 0x02, 0x1E0, 1, "USB 0x02[8:5] not all 0"),
	RTW_IO_SNIFF_EN_ENT(MAX_CHIP_TYPE, RTW_PCIE, 0x02, 0x01C, 1, "PCI 0x02[4:2] not all 0"),
#endif
#ifdef DBG_IO_SNIFF_EXAMPLE
	RTW_IO_SNIFF_RANGE_ENT(MAX_CHIP_TYPE, 0, 0x522, 0x522, 0, "read TXPAUSE"),
	RTW_IO_SNIFF_DIS_ENT(MAX_CHIP_TYPE, 0, 0x02, 0x3, 0, "0x02[1:0] not all 1"),
#endif
};

const int read_sniff_num = sizeof(read_sniff) / sizeof(struct rtw_io_sniff_ent);

const struct rtw_io_sniff_ent write_sniff[] = {
#ifdef DBG_IO_HCI_EN_CHK
	RTW_IO_SNIFF_EN_ENT(MAX_CHIP_TYPE, RTW_SDIO, 0x02, 0x1FC, 1, "SDIO 0x02[8:2] not all 0"),
	RTW_IO_SNIFF_EN_ENT(MAX_CHIP_TYPE, RTW_USB, 0x02, 0x1E0, 1, "USB 0x02[8:5] not all 0"),
	RTW_IO_SNIFF_EN_ENT(MAX_CHIP_TYPE, RTW_PCIE, 0x02, 0x01C, 1, "PCI 0x02[4:2] not all 0"),
#endif
#ifdef DBG_IO_8822C_1TX_PATH_EN
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x1a04, 0xc0000000, 0x02, 1, 0, "write tx_path_en_cck A enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x1a04, 0xc0000000, 0x01, 1, 0, "write tx_path_en_cck B enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x1a04, 0xc0000000, 0x03, 1, 1, "write tx_path_en_cck AB enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x820, 0x03, 0x01, 1, 0, "write tx_path_en_ofdm_1sts A enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x820, 0x03, 0x02, 1, 0, "write tx_path_en_ofdm_1sts B enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x820, 0x03, 0x03, 1, 1, "write tx_path_en_ofdm_1sts AB enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x820, 0x30, 0x01, 1, 0, "write tx_path_en_ofdm_2sts A enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x820, 0x30, 0x02, 1, 0, "write tx_path_en_ofdm_2sts B enabled"),
	RTW_IO_SNIFF_VALUE_ENT(RTL8822C, 0, 0x820, 0x30, 0x03, 1, 1, "write tx_path_en_ofdm_2sts AB enabled"),
#endif
#ifdef DBG_IO_SNIFF_EXAMPLE
	RTW_IO_SNIFF_RANGE_ENT(MAX_CHIP_TYPE, 0, 0x522, 0x522, 0, "write TXPAUSE"),
	RTW_IO_SNIFF_DIS_ENT(MAX_CHIP_TYPE, 0, 0x02, 0x3, 0, "0x02[1:0] not all 1"),
#endif
};

const int write_sniff_num = sizeof(write_sniff) / sizeof(struct rtw_io_sniff_ent);

static bool match_io_sniff_ranges(_adapter *adapter
	, const struct rtw_io_sniff_ent *sniff, int i, u32 addr, u16 len)
{

	/* check if IO range after sniff end address */
	if (addr > sniff->u.end_addr)
		return 0;

	return 1;
}

static bool match_io_sniff_value(_adapter *adapter
	, const struct rtw_io_sniff_ent *sniff, int i, u32 addr, u8 len, u32 val)
{
	u8 sniff_len;
	s8 mask_shift;
	u32 mask;
	s8 value_shift;
	u32 value;
	bool ret = 0;

	/* check if IO range after sniff end address */
	sniff_len = 4;
	while (!(sniff->u.vm.mask & (0xFF << ((sniff_len - 1) * 8)))) {
		sniff_len--;
		if (sniff_len == 0)
			goto exit;
	}
	if (sniff->addr + sniff_len <= addr)
		goto exit;

	/* align to IO addr */
	mask_shift = (sniff->addr - addr) * 8;
	value_shift = mask_shift + bitshift(sniff->u.vm.mask);
	if (mask_shift > 0)
		mask = sniff->u.vm.mask << mask_shift;
	else if (mask_shift < 0)
		mask = sniff->u.vm.mask >> -mask_shift;
	else
		mask = sniff->u.vm.mask;

	if (value_shift > 0)
		value = sniff->u.vm.val << value_shift;
	else if (mask_shift < 0)
		value = sniff->u.vm.val >> -value_shift;
	else
		value = sniff->u.vm.val;

	if ((sniff->u.vm.equal && (mask & val) == (mask & value))
		|| (!sniff->u.vm.equal && (mask & val) != (mask & value))
	) {
		ret = 1;
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" addr:0x%x len:%u val:0x%x (i:%d sniff_len:%u m_shift:%d mask:0x%x v_shifd:%d value:0x%x equal:%d)\n"
				, FUNC_ADPT_ARG(adapter), addr, len, val, i, sniff_len, mask_shift, mask, value_shift, value, sniff->u.vm.equal);
	}

exit:
	return ret;
}

static bool match_io_sniff(_adapter *adapter
	, const struct rtw_io_sniff_ent *sniff, int i, u32 addr, u8 len, u32 val)
{
	bool ret = 0;

	if (sniff->chip != MAX_CHIP_TYPE
		&& sniff->chip != rtw_get_chip_type(adapter))
		goto exit;
	if (sniff->hci
		&& !(sniff->hci & rtw_get_intf_type(adapter)))
		goto exit;
	if (sniff->addr >= addr + len) /* IO range below sniff start address */
		goto exit;

	switch (sniff->type) {
	case RTW_IO_SNIFF_TYPE_RANGE:
		ret = match_io_sniff_ranges(adapter, sniff, i, addr, len);
		break;
	case RTW_IO_SNIFF_TYPE_VALUE:
		if (len == 1 || len == 2 || len == 4)
			ret = match_io_sniff_value(adapter, sniff, i, addr, len, val);
		break;
	default:
		rtw_warn_on(1);
		break;
	}

exit:
	return ret;
}

u32 match_read_sniff(_adapter *adapter, u32 addr, u16 len, u32 val)
{
	int i;
	bool trace = 0;
	u32 match = 0;

	for (i = 0; i < read_sniff_num; i++) {
		if (match_io_sniff(adapter, &read_sniff[i], i, addr, len, val)) {
			match++;
			trace |= read_sniff[i].trace;
			if (read_sniff[i].tag)
				RTW_INFO("DBG_IO TAG %s\n", read_sniff[i].tag);
		}
	}

	rtw_warn_on(trace);

	return match;
}

u32 match_write_sniff(_adapter *adapter, u32 addr, u16 len, u32 val)
{
	int i;
	bool trace = 0;
	u32 match = 0;

	for (i = 0; i < write_sniff_num; i++) {
		if (match_io_sniff(adapter, &write_sniff[i], i, addr, len, val)) {
			match++;
			trace |= write_sniff[i].trace;
			if (write_sniff[i].tag)
				RTW_INFO("DBG_IO TAG %s\n", write_sniff[i].tag);
		}
	}

	rtw_warn_on(trace);

	return match;
}

struct rf_sniff_ent {
	u8 path;
	u16 reg;
	u32 mask;
};

struct rf_sniff_ent rf_read_sniff_ranges[] = {
	/* example for all path addr 0x55 with all RF Reg mask */
	/* {MAX_RF_PATH, 0x55, bRFRegOffsetMask}, */
};

struct rf_sniff_ent rf_write_sniff_ranges[] = {
	/* example for all path addr 0x55 with all RF Reg mask */
	/* {MAX_RF_PATH, 0x55, bRFRegOffsetMask}, */
};

int rf_read_sniff_num = sizeof(rf_read_sniff_ranges) / sizeof(struct rf_sniff_ent);
int rf_write_sniff_num = sizeof(rf_write_sniff_ranges) / sizeof(struct rf_sniff_ent);

bool match_rf_read_sniff_ranges(_adapter *adapter, u8 path, u32 addr, u32 mask)
{
	int i;

	for (i = 0; i < rf_read_sniff_num; i++) {
		if (rf_read_sniff_ranges[i].path == MAX_RF_PATH || rf_read_sniff_ranges[i].path == path)
			if (addr == rf_read_sniff_ranges[i].reg && (mask & rf_read_sniff_ranges[i].mask))
				return _TRUE;
	}

	return _FALSE;
}

bool match_rf_write_sniff_ranges(_adapter *adapter, u8 path, u32 addr, u32 mask)
{
	int i;

	for (i = 0; i < rf_write_sniff_num; i++) {
		if (rf_write_sniff_ranges[i].path == MAX_RF_PATH || rf_write_sniff_ranges[i].path == path)
			if (addr == rf_write_sniff_ranges[i].reg && (mask & rf_write_sniff_ranges[i].mask))
				return _TRUE;
	}

	return _FALSE;
}

u8 dbg_rtw_read8(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u8 val = _rtw_read8(adapter, addr);

	if (match_read_sniff(adapter, addr, 1, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_read8(0x%04x) return 0x%02x\n"
			, caller, line, addr, val);
	}

	return val;
}

u16 dbg_rtw_read16(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u16 val = _rtw_read16(adapter, addr);

	if (match_read_sniff(adapter, addr, 2, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_read16(0x%04x) return 0x%04x\n"
			, caller, line, addr, val);
	}

	return val;
}

u32 dbg_rtw_read32(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u32 val = _rtw_read32(adapter, addr);

	if (match_read_sniff(adapter, addr, 4, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_read32(0x%04x) return 0x%08x\n"
			, caller, line, addr, val);
	}

	return val;
}

int dbg_rtw_write8(_adapter *adapter, u32 addr, u8 val, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, 1, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_write8(0x%04x, 0x%02x)\n"
			, caller, line, addr, val);
	}

	return _rtw_write8(adapter, addr, val);
}
int dbg_rtw_write16(_adapter *adapter, u32 addr, u16 val, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, 2, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_write16(0x%04x, 0x%04x)\n"
			, caller, line, addr, val);
	}

	return _rtw_write16(adapter, addr, val);
}
int dbg_rtw_write32(_adapter *adapter, u32 addr, u32 val, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, 4, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_write32(0x%04x, 0x%08x)\n"
			, caller, line, addr, val);
	}

	return _rtw_write32(adapter, addr, val);
}
int dbg_rtw_writeN(_adapter *adapter, u32 addr , u32 length , u8 *data, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, length, 0)) {
		RTW_INFO("DBG_IO %s:%d rtw_writeN(0x%04x, %u)\n"
			, caller, line, addr, length);
	}

	return _rtw_writeN(adapter, addr, length, data);
}

#ifdef CONFIG_SDIO_HCI
u8 dbg_rtw_sd_f0_read8(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u8 val = _rtw_sd_f0_read8(adapter, addr);

#if 0
	if (match_read_sniff(adapter, addr, 1, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_f0_read8(0x%04x) return 0x%02x\n"
			, caller, line, addr, val);
	}
#endif

	return val;
}

#ifdef CONFIG_SDIO_INDIRECT_ACCESS
u8 dbg_rtw_sd_iread8(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u8 val = rtw_sd_iread8(adapter, addr);

	if (match_read_sniff(adapter, addr, 1, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_iread8(0x%04x) return 0x%02x\n"
			, caller, line, addr, val);
	}

	return val;
}

u16 dbg_rtw_sd_iread16(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u16 val = _rtw_sd_iread16(adapter, addr);

	if (match_read_sniff(adapter, addr, 2, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_iread16(0x%04x) return 0x%04x\n"
			, caller, line, addr, val);
	}

	return val;
}

u32 dbg_rtw_sd_iread32(_adapter *adapter, u32 addr, const char *caller, const int line)
{
	u32 val = _rtw_sd_iread32(adapter, addr);

	if (match_read_sniff(adapter, addr, 4, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_iread32(0x%04x) return 0x%08x\n"
			, caller, line, addr, val);
	}

	return val;
}

int dbg_rtw_sd_iwrite8(_adapter *adapter, u32 addr, u8 val, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, 1, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_iwrite8(0x%04x, 0x%02x)\n"
			, caller, line, addr, val);
	}

	return _rtw_sd_iwrite8(adapter, addr, val);
}
int dbg_rtw_sd_iwrite16(_adapter *adapter, u32 addr, u16 val, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, 2, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_iwrite16(0x%04x, 0x%04x)\n"
			, caller, line, addr, val);
	}

	return _rtw_sd_iwrite16(adapter, addr, val);
}
int dbg_rtw_sd_iwrite32(_adapter *adapter, u32 addr, u32 val, const char *caller, const int line)
{
	if (match_write_sniff(adapter, addr, 4, val)) {
		RTW_INFO("DBG_IO %s:%d rtw_sd_iwrite32(0x%04x, 0x%08x)\n"
			, caller, line, addr, val);
	}

	return _rtw_sd_iwrite32(adapter, addr, val);
}

#endif /* CONFIG_SDIO_INDIRECT_ACCESS */

#endif /* CONFIG_SDIO_HCI */

#endif
