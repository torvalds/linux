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

/* for the given bus number, return the CSR for reading an i2c line */
static inline u32 i2c_in_csr(u32 bus_num)
{
	return bus_num ? ASIC_QSFP2_IN : ASIC_QSFP1_IN;
}

/* for the given bus number, return the CSR for writing an i2c line */
static inline u32 i2c_oe_csr(u32 bus_num)
{
	return bus_num ? ASIC_QSFP2_OE : ASIC_QSFP1_OE;
}

static void hfi1_setsda(void *data, int state)
{
	struct hfi1_i2c_bus *bus = (struct hfi1_i2c_bus *)data;
	struct hfi1_devdata *dd = bus->controlling_dd;
	u64 reg;
	u32 target_oe;

	target_oe = i2c_oe_csr(bus->num);
	reg = read_csr(dd, target_oe);
	/*
	 * The OE bit value is inverted and connected to the pin.  When
	 * OE is 0 the pin is left to be pulled up, when the OE is 1
	 * the pin is driven low.  This matches the "open drain" or "open
	 * collector" convention.
	 */
	if (state)
		reg &= ~QSFP_HFI0_I2CDAT;
	else
		reg |= QSFP_HFI0_I2CDAT;
	write_csr(dd, target_oe, reg);
	/* do a read to force the write into the chip */
	(void)read_csr(dd, target_oe);
}

static void hfi1_setscl(void *data, int state)
{
	struct hfi1_i2c_bus *bus = (struct hfi1_i2c_bus *)data;
	struct hfi1_devdata *dd = bus->controlling_dd;
	u64 reg;
	u32 target_oe;

	target_oe = i2c_oe_csr(bus->num);
	reg = read_csr(dd, target_oe);
	/*
	 * The OE bit value is inverted and connected to the pin.  When
	 * OE is 0 the pin is left to be pulled up, when the OE is 1
	 * the pin is driven low.  This matches the "open drain" or "open
	 * collector" convention.
	 */
	if (state)
		reg &= ~QSFP_HFI0_I2CCLK;
	else
		reg |= QSFP_HFI0_I2CCLK;
	write_csr(dd, target_oe, reg);
	/* do a read to force the write into the chip */
	(void)read_csr(dd, target_oe);
}

static int hfi1_getsda(void *data)
{
	struct hfi1_i2c_bus *bus = (struct hfi1_i2c_bus *)data;
	u64 reg;
	u32 target_in;

	hfi1_setsda(data, 1);	/* clear OE so we do not pull line down */
	udelay(2);		/* 1us pull up + 250ns hold */

	target_in = i2c_in_csr(bus->num);
	reg = read_csr(bus->controlling_dd, target_in);
	return !!(reg & QSFP_HFI0_I2CDAT);
}

static int hfi1_getscl(void *data)
{
	struct hfi1_i2c_bus *bus = (struct hfi1_i2c_bus *)data;
	u64 reg;
	u32 target_in;

	hfi1_setscl(data, 1);	/* clear OE so we do not pull line down */
	udelay(2);		/* 1us pull up + 250ns hold */

	target_in = i2c_in_csr(bus->num);
	reg = read_csr(bus->controlling_dd, target_in);
	return !!(reg & QSFP_HFI0_I2CCLK);
}

/*
 * Allocate and initialize the given i2c bus number.
 * Returns NULL on failure.
 */
static struct hfi1_i2c_bus *init_i2c_bus(struct hfi1_devdata *dd,
					 struct hfi1_asic_data *ad, int num)
{
	struct hfi1_i2c_bus *bus;
	int ret;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return NULL;

	bus->controlling_dd = dd;
	bus->num = num;	/* our bus number */

	bus->algo.setsda = hfi1_setsda;
	bus->algo.setscl = hfi1_setscl;
	bus->algo.getsda = hfi1_getsda;
	bus->algo.getscl = hfi1_getscl;
	bus->algo.udelay = 5;
	bus->algo.timeout = usecs_to_jiffies(100000);
	bus->algo.data = bus;

	bus->adapter.owner = THIS_MODULE;
	bus->adapter.algo_data = &bus->algo;
	bus->adapter.dev.parent = &dd->pcidev->dev;
	snprintf(bus->adapter.name, sizeof(bus->adapter.name),
		 "hfi1_i2c%d", num);

	ret = i2c_bit_add_bus(&bus->adapter);
	if (ret) {
		dd_dev_info(dd, "%s: unable to add i2c bus %d, err %d\n",
			    __func__, num, ret);
		kfree(bus);
		return NULL;
	}

	return bus;
}

/*
 * Initialize i2c buses.
 * Return 0 on success, -errno on error.
 */
int set_up_i2c(struct hfi1_devdata *dd, struct hfi1_asic_data *ad)
{
	ad->i2c_bus0 = init_i2c_bus(dd, ad, 0);
	ad->i2c_bus1 = init_i2c_bus(dd, ad, 1);
	if (!ad->i2c_bus0 || !ad->i2c_bus1)
		return -ENOMEM;
	return 0;
};

static void clean_i2c_bus(struct hfi1_i2c_bus *bus)
{
	if (bus) {
		i2c_del_adapter(&bus->adapter);
		kfree(bus);
	}
}

void clean_up_i2c(struct hfi1_devdata *dd, struct hfi1_asic_data *ad)
{
	if (!ad)
		return;
	clean_i2c_bus(ad->i2c_bus0);
	ad->i2c_bus0 = NULL;
	clean_i2c_bus(ad->i2c_bus1);
	ad->i2c_bus1 = NULL;
}

static int i2c_bus_write(struct hfi1_devdata *dd, struct hfi1_i2c_bus *i2c,
			 u8 slave_addr, int offset, int offset_size,
			 u8 *data, u16 len)
{
	int ret;
	int num_msgs;
	u8 offset_bytes[2];
	struct i2c_msg msgs[2];

	switch (offset_size) {
	case 0:
		num_msgs = 1;
		msgs[0].addr = slave_addr;
		msgs[0].flags = 0;
		msgs[0].len = len;
		msgs[0].buf = data;
		break;
	case 2:
		offset_bytes[1] = (offset >> 8) & 0xff;
		fallthrough;
	case 1:
		num_msgs = 2;
		offset_bytes[0] = offset & 0xff;

		msgs[0].addr = slave_addr;
		msgs[0].flags = 0;
		msgs[0].len = offset_size;
		msgs[0].buf = offset_bytes;

		msgs[1].addr = slave_addr;
		msgs[1].flags = I2C_M_NOSTART;
		msgs[1].len = len;
		msgs[1].buf = data;
		break;
	default:
		return -EINVAL;
	}

	i2c->controlling_dd = dd;
	ret = i2c_transfer(&i2c->adapter, msgs, num_msgs);
	if (ret != num_msgs) {
		dd_dev_err(dd, "%s: bus %d, i2c slave 0x%x, offset 0x%x, len 0x%x; write failed, ret %d\n",
			   __func__, i2c->num, slave_addr, offset, len, ret);
		return ret < 0 ? ret : -EIO;
	}
	return 0;
}

static int i2c_bus_read(struct hfi1_devdata *dd, struct hfi1_i2c_bus *bus,
			u8 slave_addr, int offset, int offset_size,
			u8 *data, u16 len)
{
	int ret;
	int num_msgs;
	u8 offset_bytes[2];
	struct i2c_msg msgs[2];

	switch (offset_size) {
	case 0:
		num_msgs = 1;
		msgs[0].addr = slave_addr;
		msgs[0].flags = I2C_M_RD;
		msgs[0].len = len;
		msgs[0].buf = data;
		break;
	case 2:
		offset_bytes[1] = (offset >> 8) & 0xff;
		fallthrough;
	case 1:
		num_msgs = 2;
		offset_bytes[0] = offset & 0xff;

		msgs[0].addr = slave_addr;
		msgs[0].flags = 0;
		msgs[0].len = offset_size;
		msgs[0].buf = offset_bytes;

		msgs[1].addr = slave_addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = len;
		msgs[1].buf = data;
		break;
	default:
		return -EINVAL;
	}

	bus->controlling_dd = dd;
	ret = i2c_transfer(&bus->adapter, msgs, num_msgs);
	if (ret != num_msgs) {
		dd_dev_err(dd, "%s: bus %d, i2c slave 0x%x, offset 0x%x, len 0x%x; read failed, ret %d\n",
			   __func__, bus->num, slave_addr, offset, len, ret);
		return ret < 0 ? ret : -EIO;
	}
	return 0;
}

/*
 * Raw i2c write.  No set-up or lock checking.
 *
 * Return 0 on success, -errno on error.
 */
static int __i2c_write(struct hfi1_pportdata *ppd, u32 target, int i2c_addr,
		       int offset, void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	struct hfi1_i2c_bus *bus;
	u8 slave_addr;
	int offset_size;

	bus = target ? dd->asic_data->i2c_bus1 : dd->asic_data->i2c_bus0;
	slave_addr = (i2c_addr & 0xff) >> 1; /* convert to 7-bit addr */
	offset_size = (i2c_addr >> 8) & 0x3;
	return i2c_bus_write(dd, bus, slave_addr, offset, offset_size, bp, len);
}

/*
 * Caller must hold the i2c chain resource.
 *
 * Return number of bytes written, or -errno.
 */
int i2c_write(struct hfi1_pportdata *ppd, u32 target, int i2c_addr, int offset,
	      void *bp, int len)
{
	int ret;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	ret = __i2c_write(ppd, target, i2c_addr, offset, bp, len);
	if (ret)
		return ret;

	return len;
}

/*
 * Raw i2c read.  No set-up or lock checking.
 *
 * Return 0 on success, -errno on error.
 */
static int __i2c_read(struct hfi1_pportdata *ppd, u32 target, int i2c_addr,
		      int offset, void *bp, int len)
{
	struct hfi1_devdata *dd = ppd->dd;
	struct hfi1_i2c_bus *bus;
	u8 slave_addr;
	int offset_size;

	bus = target ? dd->asic_data->i2c_bus1 : dd->asic_data->i2c_bus0;
	slave_addr = (i2c_addr & 0xff) >> 1; /* convert to 7-bit addr */
	offset_size = (i2c_addr >> 8) & 0x3;
	return i2c_bus_read(dd, bus, slave_addr, offset, offset_size, bp, len);
}

/*
 * Caller must hold the i2c chain resource.
 *
 * Return number of bytes read, or -errno.
 */
int i2c_read(struct hfi1_pportdata *ppd, u32 target, int i2c_addr, int offset,
	     void *bp, int len)
{
	int ret;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	ret = __i2c_read(ppd, target, i2c_addr, offset, bp, len);
	if (ret)
		return ret;

	return len;
}

/*
 * Write page n, offset m of QSFP memory as defined by SFF 8636
 * by writing @addr = ((256 * n) + m)
 *
 * Caller must hold the i2c chain resource.
 *
 * Return number of bytes written or -errno.
 */
int qsfp_write(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
	       int len)
{
	int count = 0;
	int offset;
	int nwrite;
	int ret = 0;
	u8 page;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	while (count < len) {
		/*
		 * Set the qsfp page based on a zero-based address
		 * and a page size of QSFP_PAGESIZE bytes.
		 */
		page = (u8)(addr / QSFP_PAGESIZE);

		ret = __i2c_write(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				  QSFP_PAGE_SELECT_BYTE_OFFS, &page, 1);
		/* QSFPs require a 5-10msec delay after write operations */
		mdelay(5);
		if (ret) {
			hfi1_dev_porterr(ppd->dd, ppd->port,
					 "QSFP chain %d can't write QSFP_PAGE_SELECT_BYTE: %d\n",
					 target, ret);
			break;
		}

		offset = addr % QSFP_PAGESIZE;
		nwrite = len - count;
		/* truncate write to boundary if crossing boundary */
		if (((addr % QSFP_RW_BOUNDARY) + nwrite) > QSFP_RW_BOUNDARY)
			nwrite = QSFP_RW_BOUNDARY - (addr % QSFP_RW_BOUNDARY);

		ret = __i2c_write(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				  offset, bp + count, nwrite);
		/* QSFPs require a 5-10msec delay after write operations */
		mdelay(5);
		if (ret)	/* stop on error */
			break;

		count += nwrite;
		addr += nwrite;
	}

	if (ret < 0)
		return ret;
	return count;
}

/*
 * Perform a stand-alone single QSFP write.  Acquire the resource, do the
 * write, then release the resource.
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
 *
 * Return the number of bytes read or -errno.
 */
int qsfp_read(struct hfi1_pportdata *ppd, u32 target, int addr, void *bp,
	      int len)
{
	int count = 0;
	int offset;
	int nread;
	int ret = 0;
	u8 page;

	if (!check_chip_resource(ppd->dd, i2c_target(target), __func__))
		return -EACCES;

	while (count < len) {
		/*
		 * Set the qsfp page based on a zero-based address
		 * and a page size of QSFP_PAGESIZE bytes.
		 */
		page = (u8)(addr / QSFP_PAGESIZE);
		ret = __i2c_write(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				  QSFP_PAGE_SELECT_BYTE_OFFS, &page, 1);
		/* QSFPs require a 5-10msec delay after write operations */
		mdelay(5);
		if (ret) {
			hfi1_dev_porterr(ppd->dd, ppd->port,
					 "QSFP chain %d can't write QSFP_PAGE_SELECT_BYTE: %d\n",
					 target, ret);
			break;
		}

		offset = addr % QSFP_PAGESIZE;
		nread = len - count;
		/* truncate read to boundary if crossing boundary */
		if (((addr % QSFP_RW_BOUNDARY) + nread) > QSFP_RW_BOUNDARY)
			nread = QSFP_RW_BOUNDARY - (addr % QSFP_RW_BOUNDARY);

		ret = __i2c_read(ppd, target, QSFP_DEV | QSFP_OFFSET_SIZE,
				 offset, bp + count, nread);
		if (ret)	/* stop on error */
			break;

		count += nread;
		addr += nread;
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
	u32 excess_len = len;
	int ret = 0, offset = 0;

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

	if (addr <= QSFP_MONITOR_VAL_END &&
	    (addr + len) >= QSFP_MONITOR_VAL_START) {
		/* Overlap with the dynamic channel monitor range */
		if (addr < QSFP_MONITOR_VAL_START) {
			if (addr + len <= QSFP_MONITOR_VAL_END)
				len = addr + len - QSFP_MONITOR_VAL_START;
			else
				len = QSFP_MONITOR_RANGE;
			offset = QSFP_MONITOR_VAL_START - addr;
			addr = QSFP_MONITOR_VAL_START;
		} else if (addr == QSFP_MONITOR_VAL_START) {
			offset = 0;
			if (addr + len > QSFP_MONITOR_VAL_END)
				len = QSFP_MONITOR_RANGE;
		} else {
			offset = 0;
			if (addr + len > QSFP_MONITOR_VAL_END)
				len = QSFP_MONITOR_VAL_END - addr + 1;
		}
		/* Refresh the values of the dynamic monitors from the cable */
		ret = one_qsfp_read(ppd, dd->hfi1_id, addr, data + offset, len);
		if (ret != len) {
			ret = -EAGAIN;
			goto set_zeroes;
		}
	}

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
			snprintf(lenstr, sizeof(lenstr), "%dM ",
				 cache[QSFP_MOD_LEN_OFFS]);

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
