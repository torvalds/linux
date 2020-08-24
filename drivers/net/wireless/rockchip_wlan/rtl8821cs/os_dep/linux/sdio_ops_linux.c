/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
#define _SDIO_OPS_LINUX_C_

#include <drv_types.h>

inline bool rtw_is_sdio30(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	PSDIO_DATA psdio_data = &dvobj->intf_data;

	return (psdio_data->sd3_bus_mode) ? _TRUE : _FALSE;
}

static bool rtw_sdio_claim_host_needed(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	PSDIO_DATA sdio_data = &dvobj->intf_data;

	if (sdio_data->sys_sdio_irq_thd && sdio_data->sys_sdio_irq_thd == current)
		return _FALSE;
	return _TRUE;
}

inline void rtw_sdio_set_irq_thd(struct dvobj_priv *dvobj, _thread_hdl_ thd_hdl)
{
	PSDIO_DATA sdio_data = &dvobj->intf_data;

	sdio_data->sys_sdio_irq_thd = thd_hdl;
}
#ifndef RTW_HALMAC
u8 sd_f0_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	u8 v = 0;
	struct sdio_func *func;
	bool claim_needed;


	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return v;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	v = sdio_f0_readb(func, addr, err);
	if (claim_needed)
		sdio_release_host(func);
	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);


	return v;
}

void sd_f0_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	sdio_f0_writeb(func, v, addr, err);
	if (claim_needed)
		sdio_release_host(func);
	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, *err, addr, v);

}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 _sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	int err = 0, i;
	struct sdio_func *func;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;

	for (i = 0; i < cnt; i++) {
		pdata[i] = sdio_readb(func, addr + i, &err);
		if (err) {
			RTW_ERR("%s: FAIL!(%d) addr=0x%05x\n", __func__, err, addr + i);
			break;
		}
	}


	return err;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	int err = 0, i;
	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	err = _sd_cmd52_read(pintfhdl, addr, cnt, pdata);
	if (claim_needed)
		sdio_release_host(func);


	return err;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 _sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	int err = 0, i;
	struct sdio_func *func;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;

	for (i = 0; i < cnt; i++) {
		sdio_writeb(func, pdata[i], addr + i, &err);
		if (err) {
			RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, err, addr + i, pdata[i]);
			break;
		}
	}


	return err;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	int err = 0, i;
	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	err = _sd_cmd52_write(pintfhdl, addr, cnt, pdata);
	if (claim_needed)
		sdio_release_host(func);


	return err;
}

u8 _sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	u8 v = 0;
	struct sdio_func *func;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return v;
	}

	func = psdio->func;

	v = sdio_readb(func, addr, err);

	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);


	return v;
}

u8 sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	u8 v = 0;
	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return v;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	v = sdio_readb(func, addr, err);
	if (claim_needed)
		sdio_release_host(func);
	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);


	return v;
}

u16 sd_read16(struct intf_hdl *pintfhdl, u32 addr, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	u16 v = 0;
	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return v;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	v = sdio_readw(func, addr, err);
	if (claim_needed)
		sdio_release_host(func);
	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);


	return  v;
}

u32 _sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	u32 v = 0;
	struct sdio_func *func;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return v;
	}

	func = psdio->func;

	v = sdio_readl(func, addr, err);

	if (err && *err) {
		int i;

		RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x\n", __func__, *err, addr, v);

		*err = 0;
		for (i = 0; i < SD_IO_TRY_CNT; i++) {
			/* sdio_claim_host(func); */
			v = sdio_readl(func, addr, err);
			/* sdio_release_host(func); */
			if (*err == 0) {
				rtw_reset_continual_io_error(psdiodev);
				break;
			} else {
				RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);
				if ((-ESHUTDOWN == *err) || (-ENODEV == *err))
					rtw_set_surprise_removed(padapter);

				if (rtw_inc_and_chk_continual_io_error(psdiodev) == _TRUE) {
					rtw_set_surprise_removed(padapter);
					break;
				}

			}
		}

		if (i == SD_IO_TRY_CNT)
			RTW_ERR("%s: FAIL!(%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);
		else
			RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);

	}


	return  v;
}

u32 sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	u32 v = 0;
	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return v;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	v = sdio_readl(func, addr, err);
	if (claim_needed)
		sdio_release_host(func);

	if (err && *err) {
		int i;

		RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x\n", __func__, *err, addr, v);

		*err = 0;
		for (i = 0; i < SD_IO_TRY_CNT; i++) {
			if (claim_needed)
				sdio_claim_host(func);
			v = sdio_readl(func, addr, err);
			if (claim_needed)
				sdio_release_host(func);

			if (*err == 0) {
				rtw_reset_continual_io_error(psdiodev);
				break;
			} else {
				RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);
				if ((-ESHUTDOWN == *err) || (-ENODEV == *err))
					rtw_set_surprise_removed(padapter);

				if (rtw_inc_and_chk_continual_io_error(psdiodev) == _TRUE) {
					rtw_set_surprise_removed(padapter);
					break;
				}
			}
		}

		if (i == SD_IO_TRY_CNT)
			RTW_ERR("%s: FAIL!(%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);
		else
			RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);

	}


	return  v;
}

void sd_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;
	bool claim_needed;


	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return ;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	sdio_writeb(func, v, addr, err);
	if (claim_needed)
		sdio_release_host(func);
	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, *err, addr, v);

}

void sd_write16(struct intf_hdl *pintfhdl, u32 addr, u16 v, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return ;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	sdio_writew(func, v, addr, err);
	if (claim_needed)
		sdio_release_host(func);
	if (err && *err)
		RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%04x\n", __func__, *err, addr, v);

}

void _sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return ;
	}

	func = psdio->func;

	sdio_writel(func, v, addr, err);

	if (err && *err) {
		int i;

		RTW_ERR("%s: (%d) addr=0x%05x val=0x%08x\n", __func__, *err, addr, v);

		*err = 0;
		for (i = 0; i < SD_IO_TRY_CNT; i++) {
			sdio_writel(func, v, addr, err);
			if (*err == 0) {
				rtw_reset_continual_io_error(psdiodev);
				break;
			} else {
				RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);
				if ((-ESHUTDOWN == *err) || (-ENODEV == *err))
					rtw_set_surprise_removed(padapter);

				if (rtw_inc_and_chk_continual_io_error(psdiodev) == _TRUE) {
					rtw_set_surprise_removed(padapter);
					break;
				}
			}
		}

		if (i == SD_IO_TRY_CNT)
			RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%08x, try_cnt=%d\n", __func__, *err, addr, v, i);
		else
			RTW_ERR("%s: (%d) addr=0x%05x val=0x%08x, try_cnt=%d\n", __func__, *err, addr, v, i);

	}

}

void sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;
	struct sdio_func *func;
	bool claim_needed;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return ;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	sdio_writel(func, v, addr, err);
	if (claim_needed)
		sdio_release_host(func);

	if (err && *err) {
		int i;

		RTW_ERR("%s: (%d) addr=0x%05x val=0x%08x\n", __func__, *err, addr, v);

		*err = 0;
		for (i = 0; i < SD_IO_TRY_CNT; i++) {
			if (claim_needed)
				sdio_claim_host(func);
			sdio_writel(func, v, addr, err);
			if (claim_needed)
				sdio_release_host(func);
			if (*err == 0) {
				rtw_reset_continual_io_error(psdiodev);
				break;
			} else {
				RTW_ERR("%s: (%d) addr=0x%05x, val=0x%x, try_cnt=%d\n", __func__, *err, addr, v, i);
				if ((-ESHUTDOWN == *err) || (-ENODEV == *err))
					rtw_set_surprise_removed(padapter);

				if (rtw_inc_and_chk_continual_io_error(psdiodev) == _TRUE) {
					rtw_set_surprise_removed(padapter);
					break;
				}
			}
		}

		if (i == SD_IO_TRY_CNT)
			RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%08x, try_cnt=%d\n", __func__, *err, addr, v, i);
		else
			RTW_ERR("%s: (%d) addr=0x%05x val=0x%08x, try_cnt=%d\n", __func__, *err, addr, v, i);
	}

}
#endif /* !RTW_HALMAC */

/*
 * Use CMD53 to read data from SDIO device.
 * This function MUST be called after sdio_claim_host() or
 * in SDIO ISR(host had been claimed).
 *
 * Parameters:
 *	psdio	pointer of SDIO_DATA
 *	addr	address to read
 *	cnt		amount to read
 *	pdata	pointer to put data, this should be a "DMA:able scratch buffer"!
 *
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 _sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	int err = -EPERM;
	struct sdio_func *func;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;

	if (unlikely((cnt == 1) || (cnt == 2))) {
		int i;
		u8 *pbuf = (u8 *)pdata;

		for (i = 0; i < cnt; i++) {
			*(pbuf + i) = sdio_readb(func, addr + i, &err);

			if (err) {
				RTW_ERR("%s: FAIL!(%d) addr=0x%05x\n", __func__, err, addr);
				break;
			}
		}
		return err;
	}

	err = sdio_memcpy_fromio(func, pdata, addr, cnt);
	if (err)
		RTW_ERR("%s: FAIL(%d)! ADDR=%#x Size=%d\n", __func__, err, addr, cnt);

	if (err == (-ESHUTDOWN) || err == (-ENODEV) || err == (-ENOMEDIUM) || err == (-ETIMEDOUT))
		rtw_set_surprise_removed(padapter);


	return err;
}

/*
 * Use CMD53 to read data from SDIO device.
 *
 * Parameters:
 *	psdio	pointer of SDIO_DATA
 *	addr	address to read
 *	cnt		amount to read
 *	pdata	pointer to put data, this should be a "DMA:able scratch buffer"!
 *
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;
	bool claim_needed;
	s32 err = -EPERM;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}
	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	err = _sd_read(pintfhdl, addr, cnt, pdata);
	if (claim_needed)
		sdio_release_host(func);
	return err;
}

/*
 * Use CMD53 to write data to SDIO device.
 * This function MUST be called after sdio_claim_host() or
 * in SDIO ISR(host had been claimed).
 *
 * Parameters:
 *	psdio	pointer of SDIO_DATA
 *	addr	address to write
 *	cnt		amount to write
 *	pdata	data pointer, this should be a "DMA:able scratch buffer"!
 *
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 _sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;
	u32 size;
	s32 err = -EPERM;

	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;
	/*	size = sdio_align_size(func, cnt); */

	if (unlikely((cnt == 1) || (cnt == 2))) {
		int i;
		u8 *pbuf = (u8 *)pdata;

		for (i = 0; i < cnt; i++) {
			sdio_writeb(func, *(pbuf + i), addr + i, &err);
			if (err) {
				RTW_ERR("%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, err, addr, *(pbuf + i));
				break;
			}
		}

		return err;
	}

	size = cnt;
	err = sdio_memcpy_toio(func, addr, pdata, size);
	if (err)
		RTW_ERR("%s: FAIL(%d)! ADDR=%#x Size=%d(%d)\n", __func__, err, addr, cnt, size);


	return err;
}

/*
 * Use CMD53 to write data to SDIO device.
 *
 * Parameters:
 *  psdio	pointer of SDIO_DATA
 *  addr	address to write
 *  cnt		amount to write
 *  pdata	data pointer, this should be a "DMA:able scratch buffer"!
 *
 * Return:
 *  0		Success
 *  others	Fail
 */
s32 sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata)
{
	PADAPTER padapter;
	struct dvobj_priv *psdiodev;
	PSDIO_DATA psdio;

	struct sdio_func *func;
	bool claim_needed;
	s32 err = -EPERM;
	padapter = pintfhdl->padapter;
	psdiodev = pintfhdl->pintf_dev;
	psdio = &psdiodev->intf_data;

	if (rtw_is_surprise_removed(padapter)) {
		/* RTW_INFO(" %s (padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n",__FUNCTION__); */
		return err;
	}

	func = psdio->func;
	claim_needed = rtw_sdio_claim_host_needed(func);

	if (claim_needed)
		sdio_claim_host(func);
	err = _sd_write(pintfhdl, addr, cnt, pdata);
	if (claim_needed)
		sdio_release_host(func);
	return err;
}

#if 1
/*#define RTW_SDIO_DUMP*/
#ifdef RTW_SDIO_DUMP
#define DUMP_LEN_LMT	0	/* buffer dump size limit */
				/* unit: byte, 0 for no limit */
#else 
#define DUMP_LEN_LMT	32
#endif
#define GET_DUMP_LEN(len)	(DUMP_LEN_LMT ? rtw_min(len, DUMP_LEN_LMT) : len)

#ifdef DBG_SDIO
#if (DBG_SDIO >= 1)
static void sdio_dump_reg_by_cmd52(struct dvobj_priv *d,
				   u32 addr, size_t len, u8 *buf)
{
	struct sdio_func *func;
	size_t i;
	u8 val;
	u8 str[80], used = 0;
	u8 read_twice = 0;
	int error;


	if (buf)
		_rtw_memset(buf, 0xAE, len);
	func = dvobj_to_sdio_func(d);
	/*
	 * When register is WLAN IOREG,
	 * read twice to guarantee the result is correct.
	 */
	if (addr & 0x10000)
		read_twice = 1;

	_rtw_memset(str, 0, 80);
	used = 0;
	if (addr & 0xF) {
		used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%*s", (addr&0xF)*5, "");
	}
	for (i = 0; i < len; i++, addr++) {
		val = sdio_readb(func, addr, &error);
		if (read_twice)
			val = sdio_readb(func, addr, &error);
		if (error)
			break;

		if (buf)
			buf[i] = val;

		if (!(addr & 0xF))
			used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%02x ", val);
		if (((i + 1) < len) && ((addr + 1) & 0xF) == 0) {
			dev_err(&func->dev, "%s", str);
			_rtw_memset(str, 0, 80);
			used = 0;
		}
	}

	if (used) {
		dev_err(&func->dev, "%s", str);
		_rtw_memset(str, 0, 80);
		used = 0;
	}

	if (error)
		dev_err(&func->dev, "rtw_sdio_dbg: READ 0x%02x FAIL!", addr);
}

static void sdio_dump_cia(struct dvobj_priv *d, u32 addr, size_t len, u8 *buf)
{
	struct sdio_func *func;
	size_t i;
	u8 val;
	u8 str[80], used = 0;
	int error;


	if (buf)
		_rtw_memset(buf, 0xAE, len);
	func = dvobj_to_sdio_func(d);

	_rtw_memset(str, 0, 80);
	used = 0;
	if (addr & 0xF) {
		used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%*s", (addr&0xF)*5, "");
	}
	for (i = 0; i < len; i++, addr++) {
		val = sdio_f0_readb(func, addr, &error);
		if (error)
			break;

		if (buf)
			buf[i] = val;

		if (!(addr & 0xF))
			used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%02x ", val);
		if (((i + 1) < len) && ((addr + 1) & 0xF) == 0) {
			dev_err(&func->dev, "%s", str);
			_rtw_memset(str, 0, 80);
			used = 0;
		}
	}

	if (used) {
		dev_err(&func->dev, "%s", str);
		_rtw_memset(str, 0, 80);
		used = 0;
	}

	if (error)
		dev_err(&func->dev, "rtw_sdio_dbg: READ CIA 0x%02x FAIL!",
			addr);
}

#if (DBG_SDIO >= 2)
void rtw_sdio_dbg_reg_alloc(struct dvobj_priv *d);
#endif /* DBG_SDIO >= 2 */

/*
 * Dump register when CMD53 fail
 */
static void sdio_dump_dbg_reg(struct dvobj_priv *d, u8 write,
			      unsigned int addr, size_t len)
{
	struct sdio_data *sdio;
	struct sdio_func *func;
	u8 *buf = NULL;
#if (DBG_SDIO >= 2)
	u8 *msg;
#endif /* DBG_SDIO >= 2 */


	sdio = &d->intf_data;
	if (sdio->reg_dump_mark)
		return;
	func = dvobj_to_sdio_func(d);

	sdio->reg_dump_mark = sdio->cmd53_err_cnt;

#if (DBG_SDIO >= 2)
	if (!sdio->dbg_msg) {
		msg = rtw_zmalloc(80);
		if (msg) {
			sdio->dbg_msg = msg;
			sdio->dbg_msg_size = 80;
		}
	}
	if (sdio->dbg_msg_size) {
		snprintf(sdio->dbg_msg, sdio->dbg_msg_size,
			 "CMD53 %s 0x%05x, %zu bytes FAIL "
			 "at err_cnt=%d",
			 write?"WRITE":"READ",
			 addr, len, sdio->reg_dump_mark);
	}

	rtw_sdio_dbg_reg_alloc(d);
#endif /* DBG_SDIO >= 2 */

	/* MAC register */
	dev_err(&func->dev, "MAC register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_mac;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x10000, 0x800, buf);
	dev_err(&func->dev, "MAC Extend register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_mac_ext;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x11000, 0x800, buf);

	/* SDIO local register */
	dev_err(&func->dev, "SDIO Local register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_local;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x0, 0x100, buf);

	/* F0 */
	dev_err(&func->dev, "f0 register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_cia;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_cia(d, 0x0, 0x200, buf);
}
#endif /* DBG_SDIO >= 1 */
#endif /* DBG_SDIO */

/**
 *	Returns driver error code,
 *	0	no error
 *	-1	Level 1 error, critical error and can't be recovered
 *	-2	Level 2 error, normal error, retry to recover is possible
 */
static int linux_io_err_to_drv_err(int err)
{
	if (!err)
		return 0;

	/* critical error */
	if ((err == -ESHUTDOWN) ||
	    (err == -ENODEV) ||
	    (err == -ENOMEDIUM))
		return -1;

	/* other error */
	return -2;
}

/**
 *	rtw_sdio_raw_read - Read from SDIO device
 *	@d: driver object private data
 *	@addr: address to read
 *	@buf: buffer to store the data
 *	@len: number of bytes to read
 *	@fixed:
 *
 *	Reads from the address space of a SDIO device.
 *	Return value indicates if the transfer succeeded or not.
 */
int __must_check rtw_sdio_raw_read(struct dvobj_priv *d, unsigned int addr,
				   void *buf, size_t len, bool fixed)
{
	int error = -EPERM;
	bool f0, cmd52;
	struct sdio_func *func;
	bool claim_needed;
	u32 offset, i;


	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	f0 = RTW_SDIO_ADDR_F0_CHK(addr);
	cmd52 = RTW_SDIO_ADDR_CMD52_CHK(addr);

	/*
	 * Mask addr to remove driver defined bit and
	 * make sure addr is in valid range
	 */
	if (f0)
		addr &= 0xFFF;
	else
		addr &= 0x1FFFF;

#ifdef RTW_SDIO_DUMP
	if (f0)
		dev_dbg(&func->dev, "rtw_sdio: READ F0\n");
	else if (cmd52)
		dev_dbg(&func->dev, "rtw_sdio: READ use CMD52\n");
	else
		dev_dbg(&func->dev, "rtw_sdio: READ use CMD53\n");

	dev_dbg(&func->dev, "rtw_sdio: READ from 0x%05x\n", addr);
#endif /* RTW_SDIO_DUMP */

	if (claim_needed)
		sdio_claim_host(func);

	if (f0) {
		offset = addr;
		for (i = 0; i < len; i++, offset++) {
			((u8 *)buf)[i] = sdio_f0_readb(func, offset, &error);
			if (error)
				break;
#if 0
			dev_info(&func->dev, "%s: sdio f0 read 52 addr 0x%x, byte 0x%02x\n",
				 __func__, offset, ((u8 *)buf)[i]);
#endif
		}
	} else {
		if (cmd52) {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio read 52 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			offset = addr;
			for (i = 0; i < len; i++) {
				((u8 *)buf)[i] = sdio_readb(func, offset, &error);
				if (error)
					break;
#if 0
				dev_info(&func->dev, "%s: sdio read 52 addr 0x%x, byte 0x%02x\n",
					 __func__, offset, ((u8 *)buf)[i]);
#endif
				if (!fixed)
					offset++;
			}
		} else {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio read 53 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			if (fixed)
				error = sdio_readsb(func, buf, addr, len);
			else
				error = sdio_memcpy_fromio(func, buf, addr, len);
		}
	}

#ifdef DBG_SDIO
#if (DBG_SDIO >= 3)
	if (!error && !f0 && !cmd52
	    && (d->intf_data.dbg_enable
		&& d->intf_data.err_test && !d->intf_data.err_test_triggered
		&& ((addr & 0x10000)
		    || (!(addr & 0xE000)
			&& !((addr >= 0x40) && (addr < 0x48)))))) {
		d->intf_data.err_test_triggered = 1;
		error = -ETIMEDOUT;
		dev_warn(&func->dev, "Simulate error(%d) READ addr=0x%05x %zu bytes",
			 error, addr, len);
	}
#endif /* DBG_SDIO >= 3 */

	if (error) {
		if (f0 || cmd52) {
			d->intf_data.cmd52_err_cnt++;
		} else {
			d->intf_data.cmd53_err_cnt++;
#if (DBG_SDIO >= 1)
			sdio_dump_dbg_reg(d, 0, addr, len);
#endif /* DBG_SDIO >= 1 */
		}
	}
#endif /* DBG_SDIO */

	if (claim_needed)
		sdio_release_host(func);

#ifdef RTW_SDIO_DUMP
	print_hex_dump(KERN_DEBUG, "rtw_sdio: READ ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       buf, GET_DUMP_LEN(len), false);
#endif /* RTW_SDIO_DUMP */

	if (WARN_ON(error)) {
		dev_err(&func->dev, "%s: sdio read failed (%d)\n", __func__, error);
#ifndef RTW_SDIO_DUMP
		if (f0)
			dev_err(&func->dev, "rtw_sdio: READ F0\n");
		if (cmd52)
			dev_err(&func->dev, "rtw_sdio: READ use CMD52\n");
		else
			dev_err(&func->dev, "rtw_sdio: READ use CMD53\n");
		dev_err(&func->dev, "rtw_sdio: READ from 0x%05x, %zu bytes\n", addr, len);
		print_hex_dump(KERN_ERR, "rtw_sdio: READ ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, GET_DUMP_LEN(len), false);
#endif /* !RTW_SDIO_DUMP */
	}

	return linux_io_err_to_drv_err(error);
}

/**
 *	rtw_sdio_raw_write - Write to SDIO device
 *	@d: driver object private data
 *	@addr: address to write
 *	@buf: buffer that contains the data to write
 *	@len: number of bytes to write
 *	@fixed: address is fixed(FIFO) or incremented
 *
 *	Writes to the address space of a SDIO device.
 *	Return value indicates if the transfer succeeded or not.
 */
int __must_check rtw_sdio_raw_write(struct dvobj_priv *d, unsigned int addr,
				    void *buf, size_t len, bool fixed)
{
	int error = -EPERM;
	bool f0, cmd52;
	struct sdio_func *func;
	bool claim_needed;
	u32 offset, i;


	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	f0 = RTW_SDIO_ADDR_F0_CHK(addr);
	cmd52 = RTW_SDIO_ADDR_CMD52_CHK(addr);

	/*
	 * Mask addr to remove driver defined bit and
	 * make sure addr is in valid range
	 */
	if (f0)
		addr &= 0xFFF;
	else
		addr &= 0x1FFFF;

#ifdef RTW_SDIO_DUMP
	if (f0)
		dev_dbg(&func->dev, "rtw_sdio: WRITE F0\n");
	else if (cmd52)
		dev_dbg(&func->dev, "rtw_sdio: WRITE use CMD52\n");
	else
		dev_dbg(&func->dev, "rtw_sdio: WRITE use CMD53\n");
	dev_dbg(&func->dev, "rtw_sdio: WRITE to 0x%05x\n", addr);
	print_hex_dump(KERN_DEBUG, "rtw_sdio: WRITE ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       buf, GET_DUMP_LEN(len), false);
#endif /* RTW_SDIO_DUMP */

	if (claim_needed)
		sdio_claim_host(func);

	if (f0) {
		offset = addr;
		for (i = 0; i < len; i++, offset++) {
			sdio_f0_writeb(func, ((u8 *)buf)[i], offset, &error);
			if (error)
				break;
#if 0
			dev_info(&func->dev, "%s: sdio f0 write 52 addr 0x%x, byte 0x%02x\n",
				 __func__, offset, ((u8 *)buf)[i]);
#endif
		}
	} else {
		if (cmd52) {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio write 52 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			offset = addr;
			for (i = 0; i < len; i++) {
				sdio_writeb(func, ((u8 *)buf)[i], offset, &error);
				if (error)
					break;
#if 0
				dev_info(&func->dev, "%s: sdio write 52 addr 0x%x, byte 0x%02x\n",
					 __func__, offset, ((u8 *)buf)[i]);
#endif
				if (!fixed)
					offset++;
			}
		} else {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio write 53 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			if (fixed)
				error = sdio_writesb(func, addr, buf, len);
			else
				error = sdio_memcpy_toio(func, addr, buf, len);
		}
	}

#ifdef DBG_SDIO
	if (error) {
		if (f0 || cmd52) {
			d->intf_data.cmd52_err_cnt++;
		} else {
			d->intf_data.cmd53_err_cnt++;
#if (DBG_SDIO >= 1)
			sdio_dump_dbg_reg(d, 1, addr, len);
#endif /* DBG_SDIO >= 1 */
		}
	}
#endif /* DBG_SDIO */

	if (claim_needed)
		sdio_release_host(func);

	if (WARN_ON(error)) {
		dev_err(&func->dev, "%s: sdio write failed (%d)\n", __func__, error);
#ifndef RTW_SDIO_DUMP
		if (f0)
			dev_err(&func->dev, "rtw_sdio: WRITE F0\n");
		if (cmd52)
			dev_err(&func->dev, "rtw_sdio: WRITE use CMD52\n");
		else
			dev_err(&func->dev, "rtw_sdio: WRITE use CMD53\n");
		dev_err(&func->dev, "rtw_sdio: WRITE to 0x%05x, %zu bytes\n", addr, len);
		print_hex_dump(KERN_ERR, "rtw_sdio: WRITE ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, GET_DUMP_LEN(len), false);
#endif /* !RTW_SDIO_DUMP */
	}

	return linux_io_err_to_drv_err(error);
}
#endif
