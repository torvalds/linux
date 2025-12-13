// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 ARM Limited, All Rights Reserved.
 */

#define pr_fmt(fmt)	"GICv5 IRS: " fmt

#include <linux/kmemleak.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-v5.h>

/*
 * Hardcoded ID_BITS limit for systems supporting only a 1-level IST
 * table. Systems supporting only a 1-level IST table aren't expected
 * to require more than 2^12 LPIs. Tweak as required.
 */
#define LPI_ID_BITS_LINEAR		12

#define IRS_FLAGS_NON_COHERENT		BIT(0)

static DEFINE_PER_CPU_READ_MOSTLY(struct gicv5_irs_chip_data *, per_cpu_irs_data);
static LIST_HEAD(irs_nodes);

static u32 irs_readl_relaxed(struct gicv5_irs_chip_data *irs_data,
			     const u32 reg_offset)
{
	return readl_relaxed(irs_data->irs_base + reg_offset);
}

static void irs_writel_relaxed(struct gicv5_irs_chip_data *irs_data,
			       const u32 val, const u32 reg_offset)
{
	writel_relaxed(val, irs_data->irs_base + reg_offset);
}

static u64 irs_readq_relaxed(struct gicv5_irs_chip_data *irs_data,
			     const u32 reg_offset)
{
	return readq_relaxed(irs_data->irs_base + reg_offset);
}

static void irs_writeq_relaxed(struct gicv5_irs_chip_data *irs_data,
			       const u64 val, const u32 reg_offset)
{
	writeq_relaxed(val, irs_data->irs_base + reg_offset);
}

/*
 * The polling wait (in gicv5_wait_for_op_s_atomic()) on a GIC register
 * provides the memory barriers (through MMIO accessors)
 * required to synchronize CPU and GIC access to IST memory.
 */
static int gicv5_irs_ist_synchronise(struct gicv5_irs_chip_data *irs_data)
{
	return gicv5_wait_for_op_atomic(irs_data->irs_base, GICV5_IRS_IST_STATUSR,
					GICV5_IRS_IST_STATUSR_IDLE, NULL);
}

static int __init gicv5_irs_init_ist_linear(struct gicv5_irs_chip_data *irs_data,
					    unsigned int lpi_id_bits,
					    unsigned int istsz)
{
	size_t l2istsz;
	u32 n, cfgr;
	void *ist;
	u64 baser;
	int ret;

	/* Taken from GICv5 specifications 10.2.1.13 IRS_IST_BASER */
	n = max(5, lpi_id_bits + 1 + istsz);

	l2istsz = BIT(n + 1);
	/*
	 * Check memory requirements. For a linear IST we cap the
	 * number of ID bits to a value that should never exceed
	 * kmalloc interface memory allocation limits, so this
	 * check is really belt and braces.
	 */
	if (l2istsz > KMALLOC_MAX_SIZE) {
		u8 lpi_id_cap = ilog2(KMALLOC_MAX_SIZE) - 2 + istsz;

		pr_warn("Limiting LPI ID bits from %u to %u\n",
			lpi_id_bits, lpi_id_cap);
		lpi_id_bits = lpi_id_cap;
		l2istsz = KMALLOC_MAX_SIZE;
	}

	ist = kzalloc(l2istsz, GFP_KERNEL);
	if (!ist)
		return -ENOMEM;

	if (irs_data->flags & IRS_FLAGS_NON_COHERENT)
		dcache_clean_inval_poc((unsigned long)ist,
				       (unsigned long)ist + l2istsz);
	else
		dsb(ishst);

	cfgr = FIELD_PREP(GICV5_IRS_IST_CFGR_STRUCTURE,
			  GICV5_IRS_IST_CFGR_STRUCTURE_LINEAR)	|
	       FIELD_PREP(GICV5_IRS_IST_CFGR_ISTSZ, istsz)	|
	       FIELD_PREP(GICV5_IRS_IST_CFGR_L2SZ,
			  GICV5_IRS_IST_CFGR_L2SZ_4K)		|
	       FIELD_PREP(GICV5_IRS_IST_CFGR_LPI_ID_BITS, lpi_id_bits);
	irs_writel_relaxed(irs_data, cfgr, GICV5_IRS_IST_CFGR);

	gicv5_global_data.ist.l2 = false;

	baser = (virt_to_phys(ist) & GICV5_IRS_IST_BASER_ADDR_MASK) |
		FIELD_PREP(GICV5_IRS_IST_BASER_VALID, 0x1);
	irs_writeq_relaxed(irs_data, baser, GICV5_IRS_IST_BASER);

	ret = gicv5_irs_ist_synchronise(irs_data);
	if (ret) {
		kfree(ist);
		return ret;
	}
	kmemleak_ignore(ist);

	return 0;
}

static int __init gicv5_irs_init_ist_two_level(struct gicv5_irs_chip_data *irs_data,
					       unsigned int lpi_id_bits,
					       unsigned int istsz,
					       unsigned int l2sz)
{
	__le64 *l1ist;
	u32 cfgr, n;
	size_t l1sz;
	u64 baser;
	int ret;

	/* Taken from GICv5 specifications 10.2.1.13 IRS_IST_BASER */
	n = max(5, lpi_id_bits - ((10 - istsz) + (2 * l2sz)) + 2);

	l1sz = BIT(n + 1);

	l1ist = kzalloc(l1sz, GFP_KERNEL);
	if (!l1ist)
		return -ENOMEM;

	if (irs_data->flags & IRS_FLAGS_NON_COHERENT)
		dcache_clean_inval_poc((unsigned long)l1ist,
				       (unsigned long)l1ist + l1sz);
	else
		dsb(ishst);

	cfgr = FIELD_PREP(GICV5_IRS_IST_CFGR_STRUCTURE,
			  GICV5_IRS_IST_CFGR_STRUCTURE_TWO_LEVEL)	|
	       FIELD_PREP(GICV5_IRS_IST_CFGR_ISTSZ, istsz)		|
	       FIELD_PREP(GICV5_IRS_IST_CFGR_L2SZ, l2sz)		|
	       FIELD_PREP(GICV5_IRS_IST_CFGR_LPI_ID_BITS, lpi_id_bits);
	irs_writel_relaxed(irs_data, cfgr, GICV5_IRS_IST_CFGR);

	/*
	 * The L2SZ determine bits required at L2 level. Number of bytes
	 * required by metadata is reported through istsz - the number of bits
	 * covered by L2 entries scales accordingly.
	 */
	gicv5_global_data.ist.l2_size = BIT(11 + (2 * l2sz) + 1);
	gicv5_global_data.ist.l2_bits = (10 - istsz) + (2 * l2sz);
	gicv5_global_data.ist.l1ist_addr = l1ist;
	gicv5_global_data.ist.l2 = true;

	baser = (virt_to_phys(l1ist) & GICV5_IRS_IST_BASER_ADDR_MASK) |
		FIELD_PREP(GICV5_IRS_IST_BASER_VALID, 0x1);
	irs_writeq_relaxed(irs_data, baser, GICV5_IRS_IST_BASER);

	ret = gicv5_irs_ist_synchronise(irs_data);
	if (ret) {
		kfree(l1ist);
		return ret;
	}

	return 0;
}

/*
 * Alloc L2 IST entries on demand.
 *
 * Locking/serialization is guaranteed by irqdomain core code by
 * taking the hierarchical domain struct irq_domain.root->mutex.
 */
int gicv5_irs_iste_alloc(const u32 lpi)
{
	struct gicv5_irs_chip_data *irs_data;
	unsigned int index;
	u32 l2istr, l2bits;
	__le64 *l1ist;
	size_t l2size;
	void *l2ist;
	int ret;

	if (!gicv5_global_data.ist.l2)
		return 0;

	irs_data = per_cpu(per_cpu_irs_data, smp_processor_id());
	if (!irs_data)
		return -ENOENT;

	l2size = gicv5_global_data.ist.l2_size;
	l2bits = gicv5_global_data.ist.l2_bits;
	l1ist = gicv5_global_data.ist.l1ist_addr;
	index = lpi >> l2bits;

	if (FIELD_GET(GICV5_ISTL1E_VALID, le64_to_cpu(l1ist[index])))
		return 0;

	l2ist = kzalloc(l2size, GFP_KERNEL);
	if (!l2ist)
		return -ENOMEM;

	l1ist[index] = cpu_to_le64(virt_to_phys(l2ist) & GICV5_ISTL1E_L2_ADDR_MASK);

	if (irs_data->flags & IRS_FLAGS_NON_COHERENT) {
		dcache_clean_inval_poc((unsigned long)l2ist,
				       (unsigned long)l2ist + l2size);
		dcache_clean_poc((unsigned long)(l1ist + index),
				 (unsigned long)(l1ist + index) + sizeof(*l1ist));
	} else {
		dsb(ishst);
	}

	l2istr = FIELD_PREP(GICV5_IRS_MAP_L2_ISTR_ID, lpi);
	irs_writel_relaxed(irs_data, l2istr, GICV5_IRS_MAP_L2_ISTR);

	ret = gicv5_irs_ist_synchronise(irs_data);
	if (ret) {
		l1ist[index] = 0;
		kfree(l2ist);
		return ret;
	}
	kmemleak_ignore(l2ist);

	/*
	 * Make sure we invalidate the cache line pulled before the IRS
	 * had a chance to update the L1 entry and mark it valid.
	 */
	if (irs_data->flags & IRS_FLAGS_NON_COHERENT) {
		/*
		 * gicv5_irs_ist_synchronise() includes memory
		 * barriers (MMIO accessors) required to guarantee that the
		 * following dcache invalidation is not executed before the
		 * IST mapping operation has completed.
		 */
		dcache_inval_poc((unsigned long)(l1ist + index),
				 (unsigned long)(l1ist + index) + sizeof(*l1ist));
	}

	return 0;
}

/*
 * Try to match the L2 IST size to the pagesize, and if this is not possible
 * pick the smallest supported L2 size in order to minimise the requirement for
 * physically contiguous blocks of memory as page-sized allocations are
 * guaranteed to be physically contiguous, and are by definition the easiest to
 * find.
 *
 * Fall back to the smallest supported size (in the event that the pagesize
 * itself is not supported) again serves to make it easier to find physically
 * contiguous blocks of memory.
 */
static unsigned int gicv5_irs_l2_sz(u32 idr2)
{
	switch (PAGE_SIZE) {
	case SZ_64K:
		if (GICV5_IRS_IST_L2SZ_SUPPORT_64KB(idr2))
			return GICV5_IRS_IST_CFGR_L2SZ_64K;
		fallthrough;
	case SZ_4K:
		if (GICV5_IRS_IST_L2SZ_SUPPORT_4KB(idr2))
			return GICV5_IRS_IST_CFGR_L2SZ_4K;
		fallthrough;
	case SZ_16K:
		if (GICV5_IRS_IST_L2SZ_SUPPORT_16KB(idr2))
			return GICV5_IRS_IST_CFGR_L2SZ_16K;
		break;
	}

	if (GICV5_IRS_IST_L2SZ_SUPPORT_4KB(idr2))
		return GICV5_IRS_IST_CFGR_L2SZ_4K;

	return GICV5_IRS_IST_CFGR_L2SZ_64K;
}

static int __init gicv5_irs_init_ist(struct gicv5_irs_chip_data *irs_data)
{
	u32 lpi_id_bits, idr2_id_bits, idr2_min_lpi_id_bits, l2_iste_sz, l2sz;
	u32 l2_iste_sz_split, idr2;
	bool two_levels, istmd;
	u64 baser;
	int ret;

	baser = irs_readq_relaxed(irs_data, GICV5_IRS_IST_BASER);
	if (FIELD_GET(GICV5_IRS_IST_BASER_VALID, baser)) {
		pr_err("IST is marked as valid already; cannot allocate\n");
		return -EPERM;
	}

	idr2 = irs_readl_relaxed(irs_data, GICV5_IRS_IDR2);
	two_levels = !!FIELD_GET(GICV5_IRS_IDR2_IST_LEVELS, idr2);

	idr2_id_bits = FIELD_GET(GICV5_IRS_IDR2_ID_BITS, idr2);
	idr2_min_lpi_id_bits = FIELD_GET(GICV5_IRS_IDR2_MIN_LPI_ID_BITS, idr2);

	/*
	 * For two level tables we are always supporting the maximum allowed
	 * number of IDs.
	 *
	 * For 1-level tables, we should support a number of bits that
	 * is >= min_lpi_id_bits but cap it to LPI_ID_BITS_LINEAR lest
	 * the level 1-table gets too large and its memory allocation
	 * may fail.
	 */
	if (two_levels) {
		lpi_id_bits = idr2_id_bits;
	} else {
		lpi_id_bits = max(LPI_ID_BITS_LINEAR, idr2_min_lpi_id_bits);
		lpi_id_bits = min(lpi_id_bits, idr2_id_bits);
	}

	/*
	 * Cap the ID bits according to the CPUIF supported ID bits
	 */
	lpi_id_bits = min(lpi_id_bits, gicv5_global_data.cpuif_id_bits);

	if (two_levels)
		l2sz = gicv5_irs_l2_sz(idr2);

	istmd = !!FIELD_GET(GICV5_IRS_IDR2_ISTMD, idr2);

	l2_iste_sz = GICV5_IRS_IST_CFGR_ISTSZ_4;

	if (istmd) {
		l2_iste_sz_split = FIELD_GET(GICV5_IRS_IDR2_ISTMD_SZ, idr2);

		if (lpi_id_bits < l2_iste_sz_split)
			l2_iste_sz = GICV5_IRS_IST_CFGR_ISTSZ_8;
		else
			l2_iste_sz = GICV5_IRS_IST_CFGR_ISTSZ_16;
	}

	/*
	 * Follow GICv5 specification recommendation to opt in for two
	 * level tables (ref: 10.2.1.14 IRS_IST_CFGR).
	 */
	if (two_levels && (lpi_id_bits > ((10 - l2_iste_sz) + (2 * l2sz)))) {
		ret = gicv5_irs_init_ist_two_level(irs_data, lpi_id_bits,
						   l2_iste_sz, l2sz);
	} else {
		ret = gicv5_irs_init_ist_linear(irs_data, lpi_id_bits,
						l2_iste_sz);
	}
	if (ret)
		return ret;

	gicv5_init_lpis(BIT(lpi_id_bits));

	return 0;
}

struct iaffid_entry {
	u16	iaffid;
	bool	valid;
};

static DEFINE_PER_CPU(struct iaffid_entry, cpu_iaffid);

int gicv5_irs_cpu_to_iaffid(int cpuid, u16 *iaffid)
{
	if (!per_cpu(cpu_iaffid, cpuid).valid) {
		pr_err("IAFFID for CPU %d has not been initialised\n", cpuid);
		return -ENODEV;
	}

	*iaffid = per_cpu(cpu_iaffid, cpuid).iaffid;

	return 0;
}

struct gicv5_irs_chip_data *gicv5_irs_lookup_by_spi_id(u32 spi_id)
{
	struct gicv5_irs_chip_data *irs_data;
	u32 min, max;

	list_for_each_entry(irs_data, &irs_nodes, entry) {
		if (!irs_data->spi_range)
			continue;

		min = irs_data->spi_min;
		max = irs_data->spi_min + irs_data->spi_range - 1;
		if (spi_id >= min && spi_id <= max)
			return irs_data;
	}

	return NULL;
}

static int gicv5_irs_wait_for_spi_op(struct gicv5_irs_chip_data *irs_data)
{
	u32 statusr;
	int ret;

	ret = gicv5_wait_for_op_atomic(irs_data->irs_base, GICV5_IRS_SPI_STATUSR,
				       GICV5_IRS_SPI_STATUSR_IDLE, &statusr);
	if (ret)
		return ret;

	return !!FIELD_GET(GICV5_IRS_SPI_STATUSR_V, statusr) ? 0 : -EIO;
}

static int gicv5_irs_wait_for_irs_pe(struct gicv5_irs_chip_data *irs_data,
				     bool selr)
{
	bool valid = true;
	u32 statusr;
	int ret;

	ret = gicv5_wait_for_op_atomic(irs_data->irs_base, GICV5_IRS_PE_STATUSR,
				       GICV5_IRS_PE_STATUSR_IDLE, &statusr);
	if (ret)
		return ret;

	if (selr)
		valid = !!FIELD_GET(GICV5_IRS_PE_STATUSR_V, statusr);

	return valid ? 0 : -EIO;
}

static int gicv5_irs_wait_for_pe_selr(struct gicv5_irs_chip_data *irs_data)
{
	return gicv5_irs_wait_for_irs_pe(irs_data, true);
}

static int gicv5_irs_wait_for_pe_cr0(struct gicv5_irs_chip_data *irs_data)
{
	return gicv5_irs_wait_for_irs_pe(irs_data, false);
}

int gicv5_spi_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gicv5_irs_chip_data *irs_data = d->chip_data;
	u32 selr, cfgr;
	bool level;
	int ret;

	/*
	 * There is no distinction between HIGH/LOW for level IRQs
	 * and RISING/FALLING for edge IRQs in the architecture,
	 * hence consider them equivalent.
	 */
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
		level = false;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_LEVEL_LOW:
		level = true;
		break;
	default:
		return -EINVAL;
	}

	guard(raw_spinlock)(&irs_data->spi_config_lock);

	selr = FIELD_PREP(GICV5_IRS_SPI_SELR_ID, d->hwirq);
	irs_writel_relaxed(irs_data, selr, GICV5_IRS_SPI_SELR);
	ret = gicv5_irs_wait_for_spi_op(irs_data);
	if (ret)
		return ret;

	cfgr = FIELD_PREP(GICV5_IRS_SPI_CFGR_TM, level);
	irs_writel_relaxed(irs_data, cfgr, GICV5_IRS_SPI_CFGR);

	return gicv5_irs_wait_for_spi_op(irs_data);
}

static int gicv5_irs_wait_for_idle(struct gicv5_irs_chip_data *irs_data)
{
	return gicv5_wait_for_op_atomic(irs_data->irs_base, GICV5_IRS_CR0,
					GICV5_IRS_CR0_IDLE, NULL);
}

void gicv5_irs_syncr(void)
{
	struct gicv5_irs_chip_data *irs_data;
	u32 syncr;

	irs_data = list_first_entry_or_null(&irs_nodes, struct gicv5_irs_chip_data, entry);
	if (WARN_ON_ONCE(!irs_data))
		return;

	syncr = FIELD_PREP(GICV5_IRS_SYNCR_SYNC, 1);
	irs_writel_relaxed(irs_data, syncr, GICV5_IRS_SYNCR);

	gicv5_wait_for_op(irs_data->irs_base, GICV5_IRS_SYNC_STATUSR,
			  GICV5_IRS_SYNC_STATUSR_IDLE);
}

int gicv5_irs_register_cpu(int cpuid)
{
	struct gicv5_irs_chip_data *irs_data;
	u32 selr, cr0;
	u16 iaffid;
	int ret;

	ret = gicv5_irs_cpu_to_iaffid(cpuid, &iaffid);
	if (ret) {
		pr_err("IAFFID for CPU %d has not been initialised\n", cpuid);
		return ret;
	}

	irs_data = per_cpu(per_cpu_irs_data, cpuid);
	if (!irs_data) {
		pr_err("No IRS associated with CPU %u\n", cpuid);
		return -ENXIO;
	}

	selr = FIELD_PREP(GICV5_IRS_PE_SELR_IAFFID, iaffid);
	irs_writel_relaxed(irs_data, selr, GICV5_IRS_PE_SELR);

	ret = gicv5_irs_wait_for_pe_selr(irs_data);
	if (ret) {
		pr_err("IAFFID 0x%x used in IRS_PE_SELR is invalid\n", iaffid);
		return -ENXIO;
	}

	cr0 = FIELD_PREP(GICV5_IRS_PE_CR0_DPS, 0x1);
	irs_writel_relaxed(irs_data, cr0, GICV5_IRS_PE_CR0);

	ret = gicv5_irs_wait_for_pe_cr0(irs_data);
	if (ret)
		return ret;

	pr_debug("CPU %d enabled PE IAFFID 0x%x\n", cpuid, iaffid);

	return 0;
}

static void __init gicv5_irs_init_bases(struct gicv5_irs_chip_data *irs_data,
					void __iomem *irs_base,
					struct fwnode_handle *handle)
{
	struct device_node *np = to_of_node(handle);
	u32 cr0, cr1;

	irs_data->fwnode = handle;
	irs_data->irs_base = irs_base;

	if (of_property_read_bool(np, "dma-noncoherent")) {
		/*
		 * A non-coherent IRS implies that some cache levels cannot be
		 * used coherently by the cores and GIC. Our only option is to mark
		 * memory attributes for the GIC as non-cacheable; by default,
		 * non-cacheable memory attributes imply outer-shareable
		 * shareability, the value written into IRS_CR1_SH is ignored.
		 */
		cr1 = FIELD_PREP(GICV5_IRS_CR1_VPED_WA, GICV5_NO_WRITE_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VPED_RA, GICV5_NO_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VMD_WA, GICV5_NO_WRITE_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VMD_RA, GICV5_NO_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VPET_RA, GICV5_NO_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VMT_RA, GICV5_NO_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_IST_WA, GICV5_NO_WRITE_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_IST_RA, GICV5_NO_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_IC, GICV5_NON_CACHE)		|
			FIELD_PREP(GICV5_IRS_CR1_OC, GICV5_NON_CACHE);
		irs_data->flags |= IRS_FLAGS_NON_COHERENT;
	} else {
		cr1 = FIELD_PREP(GICV5_IRS_CR1_VPED_WA, GICV5_WRITE_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VPED_RA, GICV5_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VMD_WA, GICV5_WRITE_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VMD_RA, GICV5_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VPET_RA, GICV5_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_VMT_RA, GICV5_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_IST_WA, GICV5_WRITE_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_IST_RA, GICV5_READ_ALLOC)	|
			FIELD_PREP(GICV5_IRS_CR1_IC, GICV5_WB_CACHE)		|
			FIELD_PREP(GICV5_IRS_CR1_OC, GICV5_WB_CACHE)		|
			FIELD_PREP(GICV5_IRS_CR1_SH, GICV5_INNER_SHARE);
	}

	irs_writel_relaxed(irs_data, cr1, GICV5_IRS_CR1);

	cr0 = FIELD_PREP(GICV5_IRS_CR0_IRSEN, 0x1);
	irs_writel_relaxed(irs_data, cr0, GICV5_IRS_CR0);
	gicv5_irs_wait_for_idle(irs_data);
}

static int __init gicv5_irs_of_init_affinity(struct device_node *node,
					     struct gicv5_irs_chip_data *irs_data,
					     u8 iaffid_bits)
{
	/*
	 * Detect IAFFID<->CPU mappings from the device tree and
	 * record IRS<->CPU topology information.
	 */
	u16 iaffid_mask = GENMASK(iaffid_bits - 1, 0);
	int ret, i, ncpus, niaffids;

	ncpus = of_count_phandle_with_args(node, "cpus", NULL);
	if (ncpus < 0)
		return -EINVAL;

	niaffids = of_property_count_elems_of_size(node, "arm,iaffids",
						   sizeof(u16));
	if (niaffids != ncpus)
		return -EINVAL;

	u16 *iaffids __free(kfree) = kcalloc(niaffids, sizeof(*iaffids), GFP_KERNEL);
	if (!iaffids)
		return -ENOMEM;

	ret = of_property_read_u16_array(node, "arm,iaffids", iaffids, niaffids);
	if (ret)
		return ret;

	for (i = 0; i < ncpus; i++) {
		struct device_node *cpu_node;
		int cpu;

		cpu_node = of_parse_phandle(node, "cpus", i);
		if (!cpu_node) {
			pr_warn(FW_BUG "Erroneous CPU node phandle\n");
			continue;
		}

		cpu = of_cpu_node_to_id(cpu_node);
		of_node_put(cpu_node);
		if (cpu < 0)
			continue;

		if (iaffids[i] & ~iaffid_mask) {
			pr_warn("CPU %d iaffid 0x%x exceeds IRS iaffid bits\n",
				cpu, iaffids[i]);
			continue;
		}

		per_cpu(cpu_iaffid, cpu).iaffid = iaffids[i];
		per_cpu(cpu_iaffid, cpu).valid = true;

		/* We also know that the CPU is connected to this IRS */
		per_cpu(per_cpu_irs_data, cpu) = irs_data;
	}

	return ret;
}

static void irs_setup_pri_bits(u32 idr1)
{
	switch (FIELD_GET(GICV5_IRS_IDR1_PRIORITY_BITS, idr1)) {
	case GICV5_IRS_IDR1_PRIORITY_BITS_1BITS:
		gicv5_global_data.irs_pri_bits = 1;
		break;
	case GICV5_IRS_IDR1_PRIORITY_BITS_2BITS:
		gicv5_global_data.irs_pri_bits = 2;
		break;
	case GICV5_IRS_IDR1_PRIORITY_BITS_3BITS:
		gicv5_global_data.irs_pri_bits = 3;
		break;
	case GICV5_IRS_IDR1_PRIORITY_BITS_4BITS:
		gicv5_global_data.irs_pri_bits = 4;
		break;
	case GICV5_IRS_IDR1_PRIORITY_BITS_5BITS:
		gicv5_global_data.irs_pri_bits = 5;
		break;
	default:
		pr_warn("Detected wrong IDR priority bits value 0x%lx\n",
			FIELD_GET(GICV5_IRS_IDR1_PRIORITY_BITS, idr1));
		gicv5_global_data.irs_pri_bits = 1;
		break;
	}
}

static int __init gicv5_irs_init(struct device_node *node)
{
	struct gicv5_irs_chip_data *irs_data;
	void __iomem *irs_base;
	u32 idr, spi_count;
	u8 iaffid_bits;
	int ret;

	irs_data = kzalloc(sizeof(*irs_data), GFP_KERNEL);
	if (!irs_data)
		return -ENOMEM;

	raw_spin_lock_init(&irs_data->spi_config_lock);

	ret = of_property_match_string(node, "reg-names", "ns-config");
	if (ret < 0) {
		pr_err("%pOF: ns-config reg-name not present\n", node);
		goto out_err;
	}

	irs_base = of_io_request_and_map(node, ret, of_node_full_name(node));
	if (IS_ERR(irs_base)) {
		pr_err("%pOF: unable to map GICv5 IRS registers\n", node);
		ret = PTR_ERR(irs_base);
		goto out_err;
	}

	gicv5_irs_init_bases(irs_data, irs_base, &node->fwnode);

	idr = irs_readl_relaxed(irs_data, GICV5_IRS_IDR1);
	iaffid_bits = FIELD_GET(GICV5_IRS_IDR1_IAFFID_BITS, idr) + 1;

	ret = gicv5_irs_of_init_affinity(node, irs_data, iaffid_bits);
	if (ret) {
		pr_err("Failed to parse CPU IAFFIDs from the device tree!\n");
		goto out_iomem;
	}

	idr = irs_readl_relaxed(irs_data, GICV5_IRS_IDR2);
	if (WARN(!FIELD_GET(GICV5_IRS_IDR2_LPI, idr),
		 "LPI support not available - no IPIs, can't proceed\n")) {
		ret = -ENODEV;
		goto out_iomem;
	}

	idr = irs_readl_relaxed(irs_data, GICV5_IRS_IDR7);
	irs_data->spi_min = FIELD_GET(GICV5_IRS_IDR7_SPI_BASE, idr);

	idr = irs_readl_relaxed(irs_data, GICV5_IRS_IDR6);
	irs_data->spi_range = FIELD_GET(GICV5_IRS_IDR6_SPI_IRS_RANGE, idr);

	if (irs_data->spi_range) {
		pr_info("%s detected SPI range [%u-%u]\n",
						of_node_full_name(node),
						irs_data->spi_min,
						irs_data->spi_min +
						irs_data->spi_range - 1);
	}

	/*
	 * Do the global setting only on the first IRS.
	 * Global properties (iaffid_bits, global spi count) are guaranteed to
	 * be consistent across IRSes by the architecture.
	 */
	if (list_empty(&irs_nodes)) {

		idr = irs_readl_relaxed(irs_data, GICV5_IRS_IDR1);
		irs_setup_pri_bits(idr);

		idr = irs_readl_relaxed(irs_data, GICV5_IRS_IDR5);

		spi_count = FIELD_GET(GICV5_IRS_IDR5_SPI_RANGE, idr);
		gicv5_global_data.global_spi_count = spi_count;

		gicv5_init_lpi_domain();

		pr_debug("Detected %u SPIs globally\n", spi_count);
	}

	list_add_tail(&irs_data->entry, &irs_nodes);

	return 0;

out_iomem:
	iounmap(irs_base);
out_err:
	kfree(irs_data);
	return ret;
}

void __init gicv5_irs_remove(void)
{
	struct gicv5_irs_chip_data *irs_data, *tmp_data;

	gicv5_free_lpi_domain();
	gicv5_deinit_lpis();

	list_for_each_entry_safe(irs_data, tmp_data, &irs_nodes, entry) {
		iounmap(irs_data->irs_base);
		list_del(&irs_data->entry);
		kfree(irs_data);
	}
}

int __init gicv5_irs_enable(void)
{
	struct gicv5_irs_chip_data *irs_data;
	int ret;

	irs_data = list_first_entry_or_null(&irs_nodes,
					    struct gicv5_irs_chip_data, entry);
	if (!irs_data)
		return -ENODEV;

	ret = gicv5_irs_init_ist(irs_data);
	if (ret) {
		pr_err("Failed to init IST\n");
		return ret;
	}

	return 0;
}

void __init gicv5_irs_its_probe(void)
{
	struct gicv5_irs_chip_data *irs_data;

	list_for_each_entry(irs_data, &irs_nodes, entry)
		gicv5_its_of_probe(to_of_node(irs_data->fwnode));
}

int __init gicv5_irs_of_probe(struct device_node *parent)
{
	struct device_node *np;
	int ret;

	for_each_available_child_of_node(parent, np) {
		if (!of_device_is_compatible(np, "arm,gic-v5-irs"))
			continue;

		ret = gicv5_irs_init(np);
		if (ret)
			pr_err("Failed to init IRS %s\n", np->full_name);
	}

	return list_empty(&irs_nodes) ? -ENODEV : 0;
}
