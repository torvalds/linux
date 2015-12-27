/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include "hfi.h"
#include "twsi.h"

/*
 * QSFP support for hfi driver, using "Two Wire Serial Interface" driver
 * in twsi.c
 */
#define I2C_MAX_RETRY 4

/*
 * Unlocked i2c write.  Must hold dd->qsfp_i2c_mutex.
 */
static int __i2c_write(struct hfi1_pportdata *ppd, u32 target, int i2c_addr,
		       int offset, void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	int ret, cnt;
	u8 *buff = bp;

	/* Make sure TWSI bus is in sane state. */
	ret = hfi1_twsi_reset(dd, target);
	if (ret) {
		hfi1_dev_porterr(dd, ppd->port,
				 "I2C interface Reset for write failed\n");
		return -EIO;
	}

	cnt = 0;
	while (cnt < len) {
		int wlen = len - cnt;

		ret = hfi1_twsi_blk_wr(dd, target, i2c_addr, offset,
				       buff + cnt, wlen);
		if (ret) {
			/* hfi1_twsi_blk_wr() 1 for error, else 0 */
			return -EIO;
		}
		offset += wlen;
		cnt += wlen;
	}

	/* Must wait min 20us between qsfp i2c transactions */
	udelay(20);

	return cnt;
}

int i2c_write(struct hfi1_pportdata *ppd, u32 target, int i2c_addr, int offset,
	      void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	int ret;

	ret = mutex_lock_interruptible(&dd->qsfp_i2c_mutex);
	if (!ret) {
		ret = __i2c_write(ppd, target, i2c_addr, offset, bp, len);
		mutex_unlock(&dd->qsfp_i2c_mutex);
	}

	return ret;
}

/*
 * Unlocked i2c read.  Must hold dd->qsfp_i2c_mutex.
 */
static int __i2c_read(struct hfi1_pportdata *ppd, u32 target, int i2c_addr,
		      int offset, void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	int ret, cnt, pass = 0;
	int stuck = 0;
	u8 *buff = bp;

	/* Make sure TWSI bus is in sane state. */
	ret = hfi1_twsi_reset(dd, target);
	if (ret) {
		hfi1_dev_porterr(dd, ppd->port,
				 "I2C interface Reset for read failed\n");
		ret = -EIO;
		stuck = 1;
		goto exit;
	}

	cnt = 0;
	while (cnt < len) {
		int rlen = len - cnt;

		ret = hfi1_twsi_blk_rd(dd, target, i2c_addr, offset,
				       buff + cnt, rlen);
		/* Some QSFP's fail first try. Retry as experiment */
		if (ret && cnt == 0 && ++pass < I2C_MAX_RETRY)
			continue;
		if (ret) {
			/* hfi1_twsi_blk_rd() 1 for error, else 0 */
			ret = -EIO;
			goto exit;
		}
		offset += rlen;
		cnt += rlen;
	}

	ret = cnt;

exit:
	if (stuck)
		dd_dev_err(dd, "I2C interface bus stuck non-idle\n");

	if (pass >= I2C_MAX_RETRY && ret)
		hfi1_dev_porterr(dd, ppd->port,
				 "I2C failed even retrying\n");
	else if (pass)
		hfi1_dev_porterr(dd, ppd->port, "I2C retries: %d\n", pass);

	/* Must wait min 20us between qsfp i2c transactions */
	udelay(20);

	return ret;
}

int i2c_read(struct hfi1_pportdata *ppd, u32 target, int i2c_addr, int offset,
	     void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	int ret;

	ret = mutex_lock_interruptible(&dd->qsfp_i2c_mutex);
	if (!ret) {
		ret = __i2c_read(ppd, target, i2c_addr, offset, bp, len);
		mutex_unlock(&dd->qsfp_i2c_mutex);
	}

	return ret;
}

int qsfp_write(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
	       int len)
{
	int count = 0;
	int offset;
	int nwrite;
	int ret;
	u8 page;

	ret = mutex_lock_interruptible(&ppd->dd->qsfp_i2c_mutex);
	if (ret)
		return ret;

	while (count < len) {
		/*
		 * Set the qsfp page based on a zero-based addresss
		 * and a page size of QSFP_PAGESIZE bytes.
		 */
		page = (u8)(addr / QSFP_PAGESIZE);

		ret = __i2c_write(ppd, target, QSFP_DEV,
					QSFP_PAGE_SELECT_BYTE_OFFS, &page, 1);
		if (ret != 1) {
			hfi1_dev_porterr(
			ppd->dd,
			ppd->port,
			"can't write QSFP_PAGE_SELECT_BYTE: %d\n", ret);
			ret = -EIO;
			break;
		}

		/* truncate write to end of page if crossing page boundary */
		offset = addr % QSFP_PAGESIZE;
		nwrite = len - count;
		if ((offset + nwrite) > QSFP_PAGESIZE)
			nwrite = QSFP_PAGESIZE - offset;

		ret = __i2c_write(ppd, target, QSFP_DEV, offset, bp + count,
					nwrite);
		if (ret <= 0)	/* stop on error or nothing read */
			break;

		count += ret;
		addr += ret;
	}

	mutex_unlock(&ppd->dd->qsfp_i2c_mutex);

	if (ret < 0)
		return ret;
	return count;
}

int qsfp_read(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
	      int len)
{
	int count = 0;
	int offset;
	int nread;
	int ret;
	u8 page;

	ret = mutex_lock_interruptible(&ppd->dd->qsfp_i2c_mutex);
	if (ret)
		return ret;

	while (count < len) {
		/*
		 * Set the qsfp page based on a zero-based address
		 * and a page size of QSFP_PAGESIZE bytes.
		 */
		page = (u8)(addr / QSFP_PAGESIZE);
		ret = __i2c_write(ppd, target, QSFP_DEV,
					QSFP_PAGE_SELECT_BYTE_OFFS, &page, 1);
		if (ret != 1) {
			hfi1_dev_porterr(
			ppd->dd,
			ppd->port,
			"can't write QSFP_PAGE_SELECT_BYTE: %d\n", ret);
			ret = -EIO;
			break;
		}

		/* truncate read to end of page if crossing page boundary */
		offset = addr % QSFP_PAGESIZE;
		nread = len - count;
		if ((offset + nread) > QSFP_PAGESIZE)
			nread = QSFP_PAGESIZE - offset;

		ret = __i2c_read(ppd, target, QSFP_DEV, offset, bp + count,
					nread);
		if (ret <= 0)	/* stop on error or nothing read */
			break;

		count += ret;
		addr += ret;
	}

	mutex_unlock(&ppd->dd->qsfp_i2c_mutex);

	if (ret < 0)
		return ret;
	return count;
}

/*
 * This function caches the QSFP memory range in 128 byte chunks.
 * As an example, the next byte after address 255 is byte 128 from
 * upper page 01H (if existing) rather than byte 0 from lower page 00H.
 */
int refresh_qsfp_cache(struct hfi1_pportdata *ppd, struct qsfp_data *cp)
{
	u32 target = ppd->dd->hfi1_id;
	int ret;
	unsigned long flags;
	u8 *cache = &cp->cache[0];

	/* ensure sane contents on invalid reads, for cable swaps */
	memset(cache, 0, (QSFP_MAX_NUM_PAGES*128));
	dd_dev_info(ppd->dd, "%s: called\n", __func__);
	if (!qsfp_mod_present(ppd)) {
		ret = -ENODEV;
		goto bail;
	}

	ret = qsfp_read(ppd, target, 0, cache, 256);
	if (ret != 256) {
		dd_dev_info(ppd->dd,
			"%s: Read of pages 00H failed, expected 256, got %d\n",
			__func__, ret);
		goto bail;
	}

	if (cache[0] != 0x0C && cache[0] != 0x0D)
		goto bail;

	/* Is paging enabled? */
	if (!(cache[2] & 4)) {

		/* Paging enabled, page 03 required */
		if ((cache[195] & 0xC0) == 0xC0) {
			/* all */
			ret = qsfp_read(ppd, target, 384, cache + 256, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 640, cache + 384, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
		} else if ((cache[195] & 0x80) == 0x80) {
			/* only page 2 and 3 */
			ret = qsfp_read(ppd, target, 640, cache + 384, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
		} else if ((cache[195] & 0x40) == 0x40) {
			/* only page 1 and 3 */
			ret = qsfp_read(ppd, target, 384, cache + 256, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
		} else {
			/* only page 3 */
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s: failed\n", __func__);
				goto bail;
			}
		}
	}

	spin_lock_irqsave(&ppd->qsfp_info.qsfp_lock, flags);
	ppd->qsfp_info.cache_valid = 1;
	ppd->qsfp_info.cache_refresh_required = 0;
	spin_unlock_irqrestore(&ppd->qsfp_info.qsfp_lock, flags);

	return 0;

bail:
	memset(cache, 0, (QSFP_MAX_NUM_PAGES*128));
	return ret;
}

const char * const hfi1_qsfp_devtech[16] = {
	"850nm VCSEL", "1310nm VCSEL", "1550nm VCSEL", "1310nm FP",
	"1310nm DFB", "1550nm DFB", "1310nm EML", "1550nm EML",
	"Cu Misc", "1490nm DFB", "Cu NoEq", "Cu Eq",
	"Undef", "Cu Active BothEq", "Cu FarEq", "Cu NearEq"
};

#define QSFP_DUMP_CHUNK 16 /* Holds longest string */
#define QSFP_DEFAULT_HDR_CNT 224

static const char *pwr_codes = "1.5W2.0W2.5W3.5W";

int qsfp_mod_present(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	u64 reg;

	reg = read_csr(dd, dd->hfi1_id ? ASIC_QSFP2_IN : ASIC_QSFP1_IN);
	return !(reg & QSFP_HFI0_MODPRST_N);
}

/*
 * This function maps QSFP memory addresses in 128 byte chunks in the following
 * fashion per the CableInfo SMA query definition in the IBA 1.3 spec/OPA Gen 1
 * spec
 * For addr 000-127, lower page 00h
 * For addr 128-255, upper page 00h
 * For addr 256-383, upper page 01h
 * For addr 384-511, upper page 02h
 * For addr 512-639, upper page 03h
 *
 * For addresses beyond this range, it returns the invalid range of data buffer
 * set to 0.
 * For upper pages that are optional, if they are not valid, returns the
 * particular range of bytes in the data buffer set to 0.
 */
int get_cable_info(struct hfi1_devdata *dd, u32 port_num, u32 addr, u32 len,
		   u8 *data)
{
	struct hfi1_pportdata *ppd;
	u32 excess_len = 0;
	int ret = 0;

	if (port_num > dd->num_pports || port_num < 1) {
		dd_dev_info(dd, "%s: Invalid port number %d\n",
				__func__, port_num);
		ret = -EINVAL;
		goto set_zeroes;
	}

	ppd = dd->pport + (port_num - 1);
	if (!qsfp_mod_present(ppd)) {
		ret = -ENODEV;
		goto set_zeroes;
	}

	if (!ppd->qsfp_info.cache_valid) {
		ret = -EINVAL;
		goto set_zeroes;
	}

	if (addr >= (QSFP_MAX_NUM_PAGES * 128)) {
		ret = -ERANGE;
		goto set_zeroes;
	}

	if ((addr + len) > (QSFP_MAX_NUM_PAGES * 128)) {
		excess_len = (addr + len) - (QSFP_MAX_NUM_PAGES * 128);
		memcpy(data, &ppd->qsfp_info.cache[addr], (len - excess_len));
		data += (len - excess_len);
		goto set_zeroes;
	}

	memcpy(data, &ppd->qsfp_info.cache[addr], len);
	return 0;

set_zeroes:
	memset(data, 0, excess_len);
	return ret;
}

int qsfp_dump(struct hfi1_pportdata *ppd, char *buf, int len)
{
	u8 *cache = &ppd->qsfp_info.cache[0];
	u8 bin_buff[QSFP_DUMP_CHUNK];
	char lenstr[6];
	int sofar, ret;
	int bidx = 0;
	u8 *atten = &cache[QSFP_ATTEN_OFFS];
	u8 *vendor_oui = &cache[QSFP_VOUI_OFFS];

	sofar = 0;
	lenstr[0] = ' ';
	lenstr[1] = '\0';

	if (ppd->qsfp_info.cache_valid) {

		if (QSFP_IS_CU(cache[QSFP_MOD_TECH_OFFS]))
			sprintf(lenstr, "%dM ", cache[QSFP_MOD_LEN_OFFS]);

		sofar += scnprintf(buf + sofar, len - sofar, "PWR:%.3sW\n",
				pwr_codes +
				(QSFP_PWR(cache[QSFP_MOD_PWR_OFFS]) * 4));

		sofar += scnprintf(buf + sofar, len - sofar, "TECH:%s%s\n",
				lenstr,
			hfi1_qsfp_devtech[(cache[QSFP_MOD_TECH_OFFS]) >> 4]);

		sofar += scnprintf(buf + sofar, len - sofar, "Vendor:%.*s\n",
				   QSFP_VEND_LEN, &cache[QSFP_VEND_OFFS]);

		sofar += scnprintf(buf + sofar, len - sofar, "OUI:%06X\n",
				   QSFP_OUI(vendor_oui));

		sofar += scnprintf(buf + sofar, len - sofar, "Part#:%.*s\n",
				   QSFP_PN_LEN, &cache[QSFP_PN_OFFS]);

		sofar += scnprintf(buf + sofar, len - sofar, "Rev:%.*s\n",
				   QSFP_REV_LEN, &cache[QSFP_REV_OFFS]);

		if (QSFP_IS_CU(cache[QSFP_MOD_TECH_OFFS]))
			sofar += scnprintf(buf + sofar, len - sofar,
				"Atten:%d, %d\n",
				QSFP_ATTEN_SDR(atten),
				QSFP_ATTEN_DDR(atten));

		sofar += scnprintf(buf + sofar, len - sofar, "Serial:%.*s\n",
				   QSFP_SN_LEN, &cache[QSFP_SN_OFFS]);

		sofar += scnprintf(buf + sofar, len - sofar, "Date:%.*s\n",
				   QSFP_DATE_LEN, &cache[QSFP_DATE_OFFS]);

		sofar += scnprintf(buf + sofar, len - sofar, "Lot:%.*s\n",
				   QSFP_LOT_LEN, &cache[QSFP_LOT_OFFS]);

		while (bidx < QSFP_DEFAULT_HDR_CNT) {
			int iidx;

			memcpy(bin_buff, &cache[bidx], QSFP_DUMP_CHUNK);
			for (iidx = 0; iidx < QSFP_DUMP_CHUNK; ++iidx) {
				sofar += scnprintf(buf + sofar, len-sofar,
					" %02X", bin_buff[iidx]);
			}
			sofar += scnprintf(buf + sofar, len - sofar, "\n");
			bidx += QSFP_DUMP_CHUNK;
		}
	}
	ret = sofar;
	return ret;
}
