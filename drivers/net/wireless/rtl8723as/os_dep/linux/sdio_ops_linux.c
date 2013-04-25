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
#define _SDIO_OPS_LINUX_C_

#include <drv_types.h>

#include <linux/mmc/sdio_func.h>


u8 sd_f0_read8(PSDIO_DATA psdio, u32 addr, s32 *err)
{
	u8 v;
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	v = sdio_f0_readb(func, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);

_func_exit_;

	return v;
}

void sd_f0_write8(PSDIO_DATA psdio, u32 addr, u8 v, s32 *err)
{
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	sdio_f0_writeb(func, v, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, *err, addr, v);

_func_exit_;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 _sd_cmd52_read(PSDIO_DATA psdio, u32 addr, u32 cnt, u8 *pdata)
{
	int err, i;
	struct sdio_func *func;

_func_enter_;

	err = 0;
	func = psdio->func;

	for (i = 0; i < cnt; i++) {
		pdata[i] = sdio_readb(func, addr+i, &err);
		if (err) {
			DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x\n", __func__, err, addr);
			break;
		}
	}

_func_exit_;

	return err;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 sd_cmd52_read(PSDIO_DATA psdio, u32 addr, u32 cnt, u8 *pdata)
{
	int err, i;
	struct sdio_func *func;

_func_enter_;

	err = 0;
	func = psdio->func;

	sdio_claim_host(func);
	err = _sd_cmd52_read(psdio, addr, cnt, pdata);
	sdio_release_host(func);

_func_exit_;

	return err;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 _sd_cmd52_write(PSDIO_DATA psdio, u32 addr, u32 cnt, u8 *pdata)
{
	int err, i;
	struct sdio_func *func;

_func_enter_;

	err = 0;
	func = psdio->func;

	for (i = 0; i < cnt; i++) {
		sdio_writeb(func, pdata[i], addr+i, &err);
		if (err) {
			DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, err, addr, pdata[i]);
			break;
		}
	}

_func_exit_;

	return err;
}

/*
 * Return:
 *	0		Success
 *	others	Fail
 */
s32 sd_cmd52_write(PSDIO_DATA psdio, u32 addr, u32 cnt, u8 *pdata)
{
	int err, i;
	struct sdio_func *func;

_func_enter_;

	err = 0;
	func = psdio->func;

	sdio_claim_host(func);
	err = _sd_cmd52_write(psdio, addr, cnt, pdata);
	sdio_release_host(func);

_func_exit_;

	return err;
}

u8 sd_read8(PSDIO_DATA psdio, u32 addr, s32 *err)
{
	u8 v;
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	v = sdio_readb(func, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);

_func_exit_;

	return v;
}

u16 sd_read16(PSDIO_DATA psdio, u32 addr, s32 *err)
{
	u16 v;
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	v = sdio_readw(func, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);

_func_exit_;

	return  v;
}

u32 sd_read32(PSDIO_DATA psdio, u32 addr, s32 *err)
{
	u32 v;
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	v = sdio_readl(func, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x\n", __func__, *err, addr);

_func_exit_;

	return  v;
}

void sd_write8(PSDIO_DATA psdio, u32 addr, u8 v, s32 *err)
{
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	sdio_writeb(func, v, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, *err, addr, v);

_func_exit_;
}

void sd_write16(PSDIO_DATA psdio, u32 addr, u16 v, s32 *err)
{
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	sdio_writew(func, v, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x val=0x%04x\n", __func__, *err, addr, v);

_func_exit_;
}

void sd_write32(PSDIO_DATA psdio, u32 addr, u32 v, s32 *err)
{
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	sdio_claim_host(func);
	sdio_writel(func, v, addr, err);
	sdio_release_host(func);
	if (err && *err)
		DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x val=0x%08x\n", __func__, *err, addr, v);

_func_exit_;
}

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
s32 _sd_read(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata)
{
	int err;
	struct sdio_func *func;

_func_enter_;

	func = psdio->func;

	if (unlikely((cnt==1) || (cnt==2)))
	{
		int i;
		u8 *pbuf = (u8*)pdata;

		for (i = 0; i < cnt; i++)
		{
			*(pbuf+i) = sdio_readb(func, addr+i, &err);

			if (err) {
				DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x\n", __func__, err, addr);
				break;
			}
		}
		return err;
	}

	err = sdio_memcpy_fromio(func, pdata, addr, cnt);
	if (err) {
		DBG_871X(KERN_ERR "%s: FAIL(%d)! ADDR=%#x Size=%d\n", __func__, err, addr, cnt);
	}

_func_exit_;

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
s32 sd_read(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata)
{
	s32 err;
	struct sdio_func *func;


	func = psdio->func;

	sdio_claim_host(func);
	err = _sd_read(psdio, addr, cnt, pdata);
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
s32 _sd_write(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata)
{
	int err;
	struct sdio_func *func;
	u32 size;

_func_enter_;

	func = psdio->func;
//	size = sdio_align_size(func, cnt);

	if (unlikely((cnt==1) || (cnt==2)))
	{
		int i;
		u8 *pbuf = (u8*)pdata;

		for (i = 0; i < cnt; i++)
		{
			sdio_writeb(func, *(pbuf+i), addr+i, &err);
			if (err) {
				DBG_871X(KERN_ERR "%s: FAIL!(%d) addr=0x%05x val=0x%02x\n", __func__, err, addr, *(pbuf+i));
				break;
			}
		}

		return err;
	}

	size = cnt;
	err = sdio_memcpy_toio(func, addr, pdata, size);
	if (err) {
		DBG_871X(KERN_ERR "%s: FAIL(%d)! ADDR=%#x Size=%d(%d)\n", __func__, err, addr, cnt, size);
	}

_func_exit_;

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
s32 sd_write(PSDIO_DATA psdio, u32 addr, u32 cnt, void *pdata)
{
	s32 err;
	struct sdio_func *func;


	func = psdio->func;

	sdio_claim_host(func);
	err = _sd_write(psdio, addr, cnt, pdata);
	sdio_release_host(func);

	return err;
}

