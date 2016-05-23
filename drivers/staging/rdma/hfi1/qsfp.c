/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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
 * Raw i2c write.  No set-up or lock checking.
 */
static int __i2c_write(struct hfi1_pportdata *ppd, u32 target, int i2c_addr,
		       int offset, void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	int ret, cnt;
	u8 *buff = bp;

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

/*
 * Caller must hold the i2c chain resource.
 */
int i2c_write(struct hfi1_pportdata *ppd, u32 target, int i2c_addr, int offset,
	      void *bp, int len)
{
	int ret;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	/* make sure the TWSI bus is in a sane state */
	ret = hfi1_twsi_reset(ppd->dd, target);
	if (ret) {
		hfi1_dev_porterr(ppd->dd, ppd->port,
				 "I2C chain %d write interface reset failed\n",
				 target);
		return ret;
	}

	return __i2c_write(ppd, target, i2c_addr, offset, bp, len);
}

/*
 * Raw i2c read.  No set-up or lock checking.
 */
static int __i2c_read(struct hfi1_pportdata *ppd, u32 target, int i2c_addr,
		      int offset, void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	int ret, cnt, pass = 0;
	int orig_offset = offset;

	cnt = 0;
	while (cnt < len) {
		int rlen = len - cnt;

		ret = hfi1_twsi_blk_rd(dd, target, i2c_addr, offset,
				       bp + cnt, rlen);
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
	if (ret < 0) {
		hfi1_dev_porterr(dd, ppd->port,
				 "I2C chain %d read failed, addr 0x%x, offset 0x%x, len %d\n",
				 target, i2c_addr, orig_offset, len);
	}

	/* Must wait min 20us between qsfp i2c transactions */
	udelay(20);

	return ret;
}

/*
 * Caller must hold the i2c chain resource.
 */
int i2c_read(struct hfi1_pportdata *ppd, u32 target, int i2c_addr, int offset,
	     void *bp, int len)
{
	int ret;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	/* make sure the TWSI bus is in a sane state */
	ret = hfi1_twsi_reset(ppd->dd, target);
	if (ret) {
		hfi1_dev_porterr(ppd->dd, ppd->port,
				 "I2C chain %d read interface reset failed\n",
				 target);
		return ret;
	}

	return __i2c_read(ppd, target, i2c_addr, offset, bp, len);
}

/*
 * Write page n, offset m of QSFP memory as defined by SFF 8636
 * by writing @addr = ((256 * n) + m)
 *
 * Caller must hold the i2c chain resource.
 */
int qsfp_write(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
	       int len)
{
	int count = 0;
	int offset;
	int nwrite;
	int ret;
	u8 page;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	/* make sure the TWSI bus is in a sane state */
	ret = hfi1_twsi_reset(ppd->dd, target);
	if (ret) {
		hfi1_dev_porterr(ppd->dd, ppd->port,
				 "QSFP chain %d write interface reset failed\n",
				 target);
		return ret;
	}

	while (count < len) {
		/*
		 * Set the qsfp page based on a zero-based address
		 * and a page size of QSFP_PAGESIZE bytes.
		 */
		page = (u8)(addr / QSFP_PAGESIZE);

		ret = __i2c_write(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				  QSFP_PAGE_SELECT_BYTE_OFFS, &page, 1);
		if (ret != 1) {
			hfi1_dev_porterr(ppd->dd, ppd->port,
					 "QSFP chain %d can't write QSFP_PAGE_SELECT_BYTE: %d\n",
					 target, ret);
			ret = -EIO;
			break;
		}

		offset = addr % QSFP_PAGESIZE;
		nwrite = len - count;
		/* truncate write to boundary if crossing boundary */
		if (((addr % QSFP_RW_BOUNDARY) + nwrite) > QSFP_RW_BOUNDARY)
			nwrite = QSFP_RW_BOUNDARY - (addr % QSFP_RW_BOUNDARY);

		ret = __i2c_write(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				  offset, bp + count, nwrite);
		if (ret <= 0)	/* stop on error or nothing written */
			break;

		count += ret;
		addr += ret;
	}

	if (ret < 0)
		return ret;
	return count;
}

/*
 * Perform a stand-alone single QSFP write.  Acquire the resource, do the
 * read, then release the resource.
 */
int one_qsfp_write(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
		   int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	u32 resource = qsfp_resource(dd);
	int ret;

	ret = acquire_chip_resource(dd, resource, QSFP_WAIT);
	if (ret)
		return ret;
	ret = qsfp_write(ppd, target, addr, bp, len);
	release_chip_resource(dd, resource);

	return ret;
}

/*
 * Access page n, offset m of QSFP memory as defined by SFF 8636
 * by reading @addr = ((256 * n) + m)
 *
 * Caller must hold the i2c chain resource.
 */
int qsfp_read(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
	      int len)
{
	int count = 0;
	int offset;
	int nread;
	int ret;
	u8 page;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	/* make sure the TWSI bus is in a sane state */
	ret = hfi1_twsi_reset(ppd->dd, target);
	if (ret) {
		hfi1_dev_porterr(ppd->dd, ppd->port,
				 "QSFP chain %d read interface reset failed\n",
				 target);
		return ret;
	}

	while (count < len) {
		/*
		 * Set the qsfp page based on a zero-based address
		 * and a page size of QSFP_PAGESIZE bytes.
		 */
		page = (u8)(addr / QSFP_PAGESIZE);
		ret = __i2c_write(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				  QSFP_PAGE_SELECT_BYTE_OFFS, &page, 1);
		if (ret != 1) {
			hfi1_dev_porterr(ppd->dd, ppd->port,
					 "QSFP chain %d can't write QSFP_PAGE_SELECT_BYTE: %d\n",
					 target, ret);
			ret = -EIO;
			break;
		}

		offset = addr % QSFP_PAGESIZE;
		nread = len - count;
		/* truncate read to boundary if crossing boundary */
		if (((addr % QSFP_RW_BOUNDARY) + nread) > QSFP_RW_BOUNDARY)
			nread = QSFP_RW_BOUNDARY - (addr % QSFP_RW_BOUNDARY);

		/* QSFPs require a 5-10msec delay after write operations */
		mdelay(5);
		ret = __i2c_read(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				 offset, bp + count, nread);
		if (ret <= 0)	/* stop on error or nothing read */
			break;

		count += ret;
		addr += ret;
	}

	if (ret < 0)
		return ret;
	return count;
}

/*
 * Perform a stand-alone single QSFP read.  Acquire the resource, do the
 * read, then release the resource.
 */
int one_qsfp_read(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
		  int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	u32 resource = qsfp_resource(dd);
	int ret;

	ret = acquire_chip_resource(dd, resource, QSFP_WAIT);
	if (ret)
		return ret;
	ret = qsfp_read(ppd, target, addr, bp, len);
	release_chip_resource(dd, resource);

	return ret;
}

/*
 * This function caches the QSFP memory range in 128 byte chunks.
 * As an example, the next byte after address 255 is byte 128 from
 * upper page 01H (if existing) rather than byte 0 from lower page 00H.
 * Access page n, offset m of QSFP memory as defined by SFF 8636
 * in the cache by reading byte ((128 * n) + m)
 * The calls to qsfp_{read,write} in this function correctly handle the
 * address map difference between this mapping and the mapping implemented
 * by those functions
 *
 * The caller must be holding the QSFP i2c chain resource.
 */
int refresh_qsfp_cache(struct hfi1_pportdata *ppd, struct qsfp_data *cp)
{
	u32 target = ppd->dd->hfi1_id;
	int ret;
	unsigned long flags;
	u8 *cache = &cp->cache[0];

	/* ensure sane contents on invalid reads, for cable swaps */
	memset(cache, 0, (QSFP_MAX_NUM_PAGES * 128));
	spin_lock_irqsave(&ppd->qsfp_info.qsfp_lock, flags);
	ppd->qsfp_info.cache_valid = 0;
	spin_unlock_irqrestore(&ppd->qsfp_info.qsfp_lock, flags);

	if (!qsfp_mod_present(ppd)) {
		ret = -ENODEV;
		goto bail;
	}

	ret = qsfp_read(ppd, target, 0, cache, QSFP_PAGESIZE);
	if (ret != QSFP_PAGESIZE) {
		dd_dev_info(ppd->dd,
			    "%s: Page 0 read failed, expected %d, got %d\n",
			    __func__, QSFP_PAGESIZE, ret);
		goto bail;
	}

	/* Is paging enabled? */
	if (!(cache[2] & 4)) {
		/* Paging enabled, page 03 required */
		if ((cache[195] & 0xC0) == 0xC0) {
			/* all */
			ret = qsfp_read(ppd, target, 384, cache + 256, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 640, cache + 384, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
		} else if ((cache[195] & 0x80) == 0x80) {
			/* only page 2 and 3 */
			ret = qsfp_read(ppd, target, 640, cache + 384, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
		} else if ((cache[195] & 0x40) == 0x40) {
			/* only page 1 and 3 */
			ret = qsfp_read(ppd, target, 384, cache + 256, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
				goto bail;
			}
		} else {
			/* only page 3 */
			ret = qsfp_read(ppd, target, 896, cache + 512, 128);
			if (ret <= 0 || ret != 128) {
				dd_dev_info(ppd->dd, "%s failed\n", __func__);
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
	memset(cache, 0, (QSFP_MAX_NUM_PAGES * 128));
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

#define QSFP_PWR(pbyte) (((pbyte) >> 6) & 3)
#define QSFP_HIGH_PWR(pbyte) ((pbyte) & 3)
/* For use with QSFP_HIGH_PWR macro */
#define QSFP_HIGH_PWR_UNUSED	0 /* Bits [1:0] = 00 implies low power module */

/*
 * Takes power class byte [Page 00 Byte 129] in SFF 8636
 * Returns power class as integer (1 through 7, per SFF 8636 rev 2.4)
 */
int get_qsfp_power_class(u8 power_byte)
{
	if (QSFP_HIGH_PWR(power_byte) == QSFP_HIGH_PWR_UNUSED)
		/* power classes count from 1, their bit encodings from 0 */
		return (QSFP_PWR(power_byte) + 1);
	/*
	 * 00 in the high power classes stands for unused, bringing
	 * balance to the off-by-1 offset above, we add 4 here to
	 * account for the difference between the low and high power
	 * groups
	 */
	return (QSFP_HIGH_PWR(power_byte) + 4);
}

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

static const char *pwr_codes[8] = {"N/AW",
				  "1.5W",
				  "2.0W",
				  "2.5W",
				  "3.5W",
				  "4.0W",
				  "4.5W",
				  "5.0W"
				 };

int qsfp_dump(struct hfi1_pportdata *ppd, char *buf, int len)
{
	u8 *cache = &ppd->qsfp_info.cache[0];
	u8 bin_buff[QSFP_DUMP_CHUNK];
	char lenstr[6];
	int sofar;
	int bidx = 0;
	u8 *atten = &cache[QSFP_ATTEN_OFFS];
	u8 *vendor_oui = &cache[QSFP_VOUI_OFFS];
	u8 power_byte = 0;

	sofar = 0;
	lenstr[0] = ' ';
	lenstr[1] = '\0';

	if (ppd->qsfp_info.cache_valid) {
		if (QSFP_IS_CU(cache[QSFP_MOD_TECH_OFFS]))
			sprintf(lenstr, "%dM ", cache[QSFP_MOD_LEN_OFFS]);

		power_byte = cache[QSFP_MOD_PWR_OFFS];
		sofar += scnprintf(buf + sofar, len - sofar, "PWR:%.3sW\n",
				pwr_codes[get_qsfp_power_class(power_byte)]);

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
				sofar += scnprintf(buf + sofar, len - sofar,
					" %02X", bin_buff[iidx]);
			}
			sofar += scnprintf(buf + sofar, len - sofar, "\n");
			bidx += QSFP_DUMP_CHUNK;
		}
	}
	return sofar;
}
