// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2019-2025, Intel Corporation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/intel_dg_nvm_aux.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/types.h>

struct intel_dg_nvm {
	struct kref refcnt;
	void __iomem *base;
	size_t size;
	unsigned int nregions;
	struct {
		const char *name;
		u8 id;
		u64 offset;
		u64 size;
		unsigned int is_readable:1;
		unsigned int is_writable:1;
	} regions[] __counted_by(nregions);
};

#define NVM_TRIGGER_REG       0x00000000
#define NVM_VALSIG_REG        0x00000010
#define NVM_ADDRESS_REG       0x00000040
#define NVM_REGION_ID_REG     0x00000044
/*
 * [15:0]-Erase size = 0x0010 4K 0x0080 32K 0x0100 64K
 * [23:16]-Reserved
 * [31:24]-Erase MEM RegionID
 */
#define NVM_ERASE_REG         0x00000048
#define NVM_ACCESS_ERROR_REG  0x00000070
#define NVM_ADDRESS_ERROR_REG 0x00000074

/* Flash Valid Signature */
#define NVM_FLVALSIG          0x0FF0A55A

#define NVM_MAP_ADDR_MASK     GENMASK(7, 0)
#define NVM_MAP_ADDR_SHIFT    0x00000004

#define NVM_REGION_ID_DESCRIPTOR  0
/* Flash Region Base Address */
#define NVM_FRBA      0x40
/* Flash Region __n - Flash Descriptor Record */
#define NVM_FLREG(__n) (NVM_FRBA + ((__n) * 4))
/*  Flash Map 1 Register */
#define NVM_FLMAP1_REG  0x18
#define NVM_FLMSTR4_OFFSET 0x00C

#define NVM_ACCESS_ERROR_PCIE_MASK 0x7

#define NVM_FREG_BASE_MASK GENMASK(15, 0)
#define NVM_FREG_ADDR_MASK GENMASK(31, 16)
#define NVM_FREG_ADDR_SHIFT 12
#define NVM_FREG_MIN_REGION_SIZE 0xFFF

static inline void idg_nvm_set_region_id(struct intel_dg_nvm *nvm, u8 region)
{
	iowrite32((u32)region, nvm->base + NVM_REGION_ID_REG);
}

static inline u32 idg_nvm_error(struct intel_dg_nvm *nvm)
{
	void __iomem *base = nvm->base;

	u32 reg = ioread32(base + NVM_ACCESS_ERROR_REG) & NVM_ACCESS_ERROR_PCIE_MASK;

	/* reset error bits */
	if (reg)
		iowrite32(reg, base + NVM_ACCESS_ERROR_REG);

	return reg;
}

static inline u32 idg_nvm_read32(struct intel_dg_nvm *nvm, u32 address)
{
	void __iomem *base = nvm->base;

	iowrite32(address, base + NVM_ADDRESS_REG);

	return ioread32(base + NVM_TRIGGER_REG);
}

static inline u64 idg_nvm_read64(struct intel_dg_nvm *nvm, u32 address)
{
	void __iomem *base = nvm->base;

	iowrite32(address, base + NVM_ADDRESS_REG);

	return readq(base + NVM_TRIGGER_REG);
}

static void idg_nvm_write32(struct intel_dg_nvm *nvm, u32 address, u32 data)
{
	void __iomem *base = nvm->base;

	iowrite32(address, base + NVM_ADDRESS_REG);

	iowrite32(data, base + NVM_TRIGGER_REG);
}

static void idg_nvm_write64(struct intel_dg_nvm *nvm, u32 address, u64 data)
{
	void __iomem *base = nvm->base;

	iowrite32(address, base + NVM_ADDRESS_REG);

	writeq(data, base + NVM_TRIGGER_REG);
}

static int idg_nvm_get_access_map(struct intel_dg_nvm *nvm, u32 *access_map)
{
	u32 fmstr4_addr;
	u32 fmstr4;
	u32 flmap1;
	u32 fmba;

	idg_nvm_set_region_id(nvm, NVM_REGION_ID_DESCRIPTOR);

	flmap1 = idg_nvm_read32(nvm, NVM_FLMAP1_REG);
	if (idg_nvm_error(nvm))
		return -EIO;
	/* Get Flash Master Baser Address (FMBA) */
	fmba = (FIELD_GET(NVM_MAP_ADDR_MASK, flmap1) << NVM_MAP_ADDR_SHIFT);
	fmstr4_addr = fmba + NVM_FLMSTR4_OFFSET;

	fmstr4 = idg_nvm_read32(nvm, fmstr4_addr);
	if (idg_nvm_error(nvm))
		return -EIO;

	*access_map = fmstr4;
	return 0;
}

/*
 * Region read/write access encoded in the access map
 * in the following order from the lower bit:
 * [3:0] regions 12-15 read state
 * [7:4] regions 12-15 write state
 * [19:8] regions 0-11 read state
 * [31:20] regions 0-11 write state
 */
static bool idg_nvm_region_readable(u32 access_map, u8 region)
{
	if (region < 12)
		return access_map & BIT(region + 8); /* [19:8] */
	else
		return access_map & BIT(region - 12); /* [3:0] */
}

static bool idg_nvm_region_writable(u32 access_map, u8 region)
{
	if (region < 12)
		return access_map & BIT(region + 20); /* [31:20] */
	else
		return access_map & BIT(region - 8); /* [7:4] */
}

static int idg_nvm_is_valid(struct intel_dg_nvm *nvm)
{
	u32 is_valid;

	idg_nvm_set_region_id(nvm, NVM_REGION_ID_DESCRIPTOR);

	is_valid = idg_nvm_read32(nvm, NVM_VALSIG_REG);
	if (idg_nvm_error(nvm))
		return -EIO;

	if (is_valid != NVM_FLVALSIG)
		return -ENODEV;

	return 0;
}

__maybe_unused
static unsigned int idg_nvm_get_region(const struct intel_dg_nvm *nvm, loff_t from)
{
	unsigned int i;

	for (i = 0; i < nvm->nregions; i++) {
		if ((nvm->regions[i].offset + nvm->regions[i].size - 1) >= from &&
		    nvm->regions[i].offset <= from &&
		    nvm->regions[i].size != 0)
			break;
	}

	return i;
}

static ssize_t idg_nvm_rewrite_partial(struct intel_dg_nvm *nvm, loff_t to,
				       loff_t offset, size_t len, const u32 *newdata)
{
	u32 data = idg_nvm_read32(nvm, to);

	if (idg_nvm_error(nvm))
		return -EIO;

	memcpy((u8 *)&data + offset, newdata, len);

	idg_nvm_write32(nvm, to, data);
	if (idg_nvm_error(nvm))
		return -EIO;

	return len;
}

__maybe_unused
static ssize_t idg_write(struct intel_dg_nvm *nvm, u8 region,
			 loff_t to, size_t len, const unsigned char *buf)
{
	size_t len_s = len;
	size_t to_shift;
	size_t len8;
	size_t len4;
	ssize_t ret;
	size_t to4;
	size_t i;

	idg_nvm_set_region_id(nvm, region);

	to4 = ALIGN_DOWN(to, sizeof(u32));
	to_shift = min(sizeof(u32) - ((size_t)to - to4), len);
	if (to - to4) {
		ret = idg_nvm_rewrite_partial(nvm, to4, to - to4, to_shift, (u32 *)&buf[0]);
		if (ret < 0)
			return ret;

		buf += to_shift;
		to += to_shift;
		len_s -= to_shift;
	}

	len8 = ALIGN_DOWN(len_s, sizeof(u64));
	for (i = 0; i < len8; i += sizeof(u64)) {
		u64 data;

		memcpy(&data, &buf[i], sizeof(u64));
		idg_nvm_write64(nvm, to + i, data);
		if (idg_nvm_error(nvm))
			return -EIO;
	}

	len4 = len_s - len8;
	if (len4 >= sizeof(u32)) {
		u32 data;

		memcpy(&data, &buf[i], sizeof(u32));
		idg_nvm_write32(nvm, to + i, data);
		if (idg_nvm_error(nvm))
			return -EIO;
		i += sizeof(u32);
		len4 -= sizeof(u32);
	}

	if (len4 > 0) {
		ret = idg_nvm_rewrite_partial(nvm, to + i, 0, len4, (u32 *)&buf[i]);
		if (ret < 0)
			return ret;
	}

	return len;
}

__maybe_unused
static ssize_t idg_read(struct intel_dg_nvm *nvm, u8 region,
			loff_t from, size_t len, unsigned char *buf)
{
	size_t len_s = len;
	size_t from_shift;
	size_t from4;
	size_t len8;
	size_t len4;
	size_t i;

	idg_nvm_set_region_id(nvm, region);

	from4 = ALIGN_DOWN(from, sizeof(u32));
	from_shift = min(sizeof(u32) - ((size_t)from - from4), len);

	if (from - from4) {
		u32 data = idg_nvm_read32(nvm, from4);

		if (idg_nvm_error(nvm))
			return -EIO;
		memcpy(&buf[0], (u8 *)&data + (from - from4), from_shift);
		len_s -= from_shift;
		buf += from_shift;
		from += from_shift;
	}

	len8 = ALIGN_DOWN(len_s, sizeof(u64));
	for (i = 0; i < len8; i += sizeof(u64)) {
		u64 data = idg_nvm_read64(nvm, from + i);

		if (idg_nvm_error(nvm))
			return -EIO;

		memcpy(&buf[i], &data, sizeof(data));
	}

	len4 = len_s - len8;
	if (len4 >= sizeof(u32)) {
		u32 data = idg_nvm_read32(nvm, from + i);

		if (idg_nvm_error(nvm))
			return -EIO;
		memcpy(&buf[i], &data, sizeof(data));
		i += sizeof(u32);
		len4 -= sizeof(u32);
	}

	if (len4 > 0) {
		u32 data = idg_nvm_read32(nvm, from + i);

		if (idg_nvm_error(nvm))
			return -EIO;
		memcpy(&buf[i], &data, len4);
	}

	return len;
}

__maybe_unused
static ssize_t
idg_erase(struct intel_dg_nvm *nvm, u8 region, loff_t from, u64 len, u64 *fail_addr)
{
	void __iomem *base = nvm->base;
	const u32 block = 0x10;
	u64 i;

	for (i = 0; i < len; i += SZ_4K) {
		iowrite32(from + i, base + NVM_ADDRESS_REG);
		iowrite32(region << 24 | block, base + NVM_ERASE_REG);
		/* Since the writes are via sgunit
		 * we cannot do back to back erases.
		 */
		msleep(50);
	}
	return len;
}

static int intel_dg_nvm_init(struct intel_dg_nvm *nvm, struct device *device)
{
	u32 access_map = 0;
	unsigned int i, n;
	int ret;

	/* clean error register, previous errors are ignored */
	idg_nvm_error(nvm);

	ret = idg_nvm_is_valid(nvm);
	if (ret) {
		dev_err(device, "The MEM is not valid %d\n", ret);
		return ret;
	}

	if (idg_nvm_get_access_map(nvm, &access_map))
		return -EIO;

	for (i = 0, n = 0; i < nvm->nregions; i++) {
		u32 address, base, limit, region;
		u8 id = nvm->regions[i].id;

		address = NVM_FLREG(id);
		region = idg_nvm_read32(nvm, address);

		base = FIELD_GET(NVM_FREG_BASE_MASK, region) << NVM_FREG_ADDR_SHIFT;
		limit = (FIELD_GET(NVM_FREG_ADDR_MASK, region) << NVM_FREG_ADDR_SHIFT) |
			NVM_FREG_MIN_REGION_SIZE;

		dev_dbg(device, "[%d] %s: region: 0x%08X base: 0x%08x limit: 0x%08x\n",
			id, nvm->regions[i].name, region, base, limit);

		if (base >= limit || (i > 0 && limit == 0)) {
			dev_dbg(device, "[%d] %s: disabled\n",
				id, nvm->regions[i].name);
			nvm->regions[i].is_readable = 0;
			continue;
		}

		if (nvm->size < limit)
			nvm->size = limit;

		nvm->regions[i].offset = base;
		nvm->regions[i].size = limit - base + 1;
		/* No write access to descriptor; mask it out*/
		nvm->regions[i].is_writable = idg_nvm_region_writable(access_map, id);

		nvm->regions[i].is_readable = idg_nvm_region_readable(access_map, id);
		dev_dbg(device, "Registered, %s id=%d offset=%lld size=%lld rd=%d wr=%d\n",
			nvm->regions[i].name,
			nvm->regions[i].id,
			nvm->regions[i].offset,
			nvm->regions[i].size,
			nvm->regions[i].is_readable,
			nvm->regions[i].is_writable);

		if (nvm->regions[i].is_readable)
			n++;
	}

	dev_dbg(device, "Registered %d regions\n", n);

	/* Need to add 1 to the amount of memory
	 * so it is reported as an even block
	 */
	nvm->size += 1;

	return n;
}

static void intel_dg_nvm_release(struct kref *kref)
{
	struct intel_dg_nvm *nvm = container_of(kref, struct intel_dg_nvm, refcnt);
	int i;

	pr_debug("freeing intel_dg nvm\n");
	for (i = 0; i < nvm->nregions; i++)
		kfree(nvm->regions[i].name);
	kfree(nvm);
}

static int intel_dg_mtd_probe(struct auxiliary_device *aux_dev,
			      const struct auxiliary_device_id *aux_dev_id)
{
	struct intel_dg_nvm_dev *invm = auxiliary_dev_to_intel_dg_nvm_dev(aux_dev);
	struct intel_dg_nvm *nvm;
	struct device *device;
	unsigned int nregions;
	unsigned int i, n;
	int ret;

	device = &aux_dev->dev;

	/* count available regions */
	for (nregions = 0, i = 0; i < INTEL_DG_NVM_REGIONS; i++) {
		if (invm->regions[i].name)
			nregions++;
	}

	if (!nregions) {
		dev_err(device, "no regions defined\n");
		return -ENODEV;
	}

	nvm = kzalloc(struct_size(nvm, regions, nregions), GFP_KERNEL);
	if (!nvm)
		return -ENOMEM;

	kref_init(&nvm->refcnt);

	for (n = 0, i = 0; i < INTEL_DG_NVM_REGIONS; i++) {
		if (!invm->regions[i].name)
			continue;

		char *name = kasprintf(GFP_KERNEL, "%s.%s",
				       dev_name(&aux_dev->dev), invm->regions[i].name);
		if (!name)
			continue;
		nvm->regions[n].name = name;
		nvm->regions[n].id = i;
		n++;
	}
	nvm->nregions = n; /* in case where kasprintf fail */

	nvm->base = devm_ioremap_resource(device, &invm->bar);
	if (IS_ERR(nvm->base)) {
		ret = PTR_ERR(nvm->base);
		goto err;
	}

	ret = intel_dg_nvm_init(nvm, device);
	if (ret < 0) {
		dev_err(device, "cannot initialize nvm %d\n", ret);
		goto err;
	}

	dev_set_drvdata(&aux_dev->dev, nvm);

	return 0;

err:
	kref_put(&nvm->refcnt, intel_dg_nvm_release);
	return ret;
}

static void intel_dg_mtd_remove(struct auxiliary_device *aux_dev)
{
	struct intel_dg_nvm *nvm = dev_get_drvdata(&aux_dev->dev);

	if (!nvm)
		return;

	dev_set_drvdata(&aux_dev->dev, NULL);

	kref_put(&nvm->refcnt, intel_dg_nvm_release);
}

static const struct auxiliary_device_id intel_dg_mtd_id_table[] = {
	{
		.name = "i915.nvm",
	},
	{
		.name = "xe.nvm",
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(auxiliary, intel_dg_mtd_id_table);

static struct auxiliary_driver intel_dg_mtd_driver = {
	.probe  = intel_dg_mtd_probe,
	.remove = intel_dg_mtd_remove,
	.driver = {
		/* auxiliary_driver_register() sets .name to be the modname */
	},
	.id_table = intel_dg_mtd_id_table
};
module_auxiliary_driver(intel_dg_mtd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel DGFX MTD driver");
