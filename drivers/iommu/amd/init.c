// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 *         Leo Duran <leo.duran@amd.com>
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt
#define dev_fmt(fmt)    pr_fmt(fmt)

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/list.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/amd-iommu.h>
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/cc_platform.h>
#include <linux/iopoll.h>
#include <asm/pci-direct.h>
#include <asm/iommu.h>
#include <asm/apic.h>
#include <asm/gart.h>
#include <asm/x86_init.h>
#include <asm/io_apic.h>
#include <asm/irq_remapping.h>
#include <asm/set_memory.h>
#include <asm/sev.h>

#include <linux/crash_dump.h>

#include "amd_iommu.h"
#include "../irq_remapping.h"
#include "../iommu-pages.h"

/*
 * definitions for the ACPI scanning code
 */
#define IVRS_HEADER_LENGTH 48

#define ACPI_IVHD_TYPE_MAX_SUPPORTED	0x40
#define ACPI_IVMD_TYPE_ALL              0x20
#define ACPI_IVMD_TYPE                  0x21
#define ACPI_IVMD_TYPE_RANGE            0x22

#define IVHD_DEV_ALL                    0x01
#define IVHD_DEV_SELECT                 0x02
#define IVHD_DEV_SELECT_RANGE_START     0x03
#define IVHD_DEV_RANGE_END              0x04
#define IVHD_DEV_ALIAS                  0x42
#define IVHD_DEV_ALIAS_RANGE            0x43
#define IVHD_DEV_EXT_SELECT             0x46
#define IVHD_DEV_EXT_SELECT_RANGE       0x47
#define IVHD_DEV_SPECIAL		0x48
#define IVHD_DEV_ACPI_HID		0xf0

#define UID_NOT_PRESENT                 0
#define UID_IS_INTEGER                  1
#define UID_IS_CHARACTER                2

#define IVHD_SPECIAL_IOAPIC		1
#define IVHD_SPECIAL_HPET		2

#define IVHD_FLAG_HT_TUN_EN_MASK        0x01
#define IVHD_FLAG_PASSPW_EN_MASK        0x02
#define IVHD_FLAG_RESPASSPW_EN_MASK     0x04
#define IVHD_FLAG_ISOC_EN_MASK          0x08

#define IVMD_FLAG_EXCL_RANGE            0x08
#define IVMD_FLAG_IW                    0x04
#define IVMD_FLAG_IR                    0x02
#define IVMD_FLAG_UNITY_MAP             0x01

#define ACPI_DEVFLAG_INITPASS           0x01
#define ACPI_DEVFLAG_EXTINT             0x02
#define ACPI_DEVFLAG_NMI                0x04
#define ACPI_DEVFLAG_SYSMGT1            0x10
#define ACPI_DEVFLAG_SYSMGT2            0x20
#define ACPI_DEVFLAG_LINT0              0x40
#define ACPI_DEVFLAG_LINT1              0x80
#define ACPI_DEVFLAG_ATSDIS             0x10000000

#define IVRS_GET_SBDF_ID(seg, bus, dev, fn)	(((seg & 0xffff) << 16) | ((bus & 0xff) << 8) \
						 | ((dev & 0x1f) << 3) | (fn & 0x7))

/*
 * ACPI table definitions
 *
 * These data structures are laid over the table to parse the important values
 * out of it.
 */

/*
 * structure describing one IOMMU in the ACPI table. Typically followed by one
 * or more ivhd_entrys.
 */
struct ivhd_header {
	u8 type;
	u8 flags;
	u16 length;
	u16 devid;
	u16 cap_ptr;
	u64 mmio_phys;
	u16 pci_seg;
	u16 info;
	u32 efr_attr;

	/* Following only valid on IVHD type 11h and 40h */
	u64 efr_reg; /* Exact copy of MMIO_EXT_FEATURES */
	u64 efr_reg2;
} __attribute__((packed));

/*
 * A device entry describing which devices a specific IOMMU translates and
 * which requestor ids they use.
 */
struct ivhd_entry {
	u8 type;
	u16 devid;
	u8 flags;
	struct_group(ext_hid,
		u32 ext;
		u32 hidh;
	);
	u64 cid;
	u8 uidf;
	u8 uidl;
	u8 uid;
} __attribute__((packed));

/*
 * An AMD IOMMU memory definition structure. It defines things like exclusion
 * ranges for devices and regions that should be unity mapped.
 */
struct ivmd_header {
	u8 type;
	u8 flags;
	u16 length;
	u16 devid;
	u16 aux;
	u16 pci_seg;
	u8  resv[6];
	u64 range_start;
	u64 range_length;
} __attribute__((packed));

bool amd_iommu_dump;
bool amd_iommu_irq_remap __read_mostly;

enum io_pgtable_fmt amd_iommu_pgtable = AMD_IOMMU_V1;
/* Guest page table level */
int amd_iommu_gpt_level = PAGE_MODE_4_LEVEL;

int amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_VAPIC;
static int amd_iommu_xt_mode = IRQ_REMAP_XAPIC_MODE;

static bool amd_iommu_detected;
static bool amd_iommu_disabled __initdata;
static bool amd_iommu_force_enable __initdata;
static bool amd_iommu_irtcachedis;
static int amd_iommu_target_ivhd_type;

/* Global EFR and EFR2 registers */
u64 amd_iommu_efr;
u64 amd_iommu_efr2;

/* SNP is enabled on the system? */
bool amd_iommu_snp_en;
EXPORT_SYMBOL(amd_iommu_snp_en);

LIST_HEAD(amd_iommu_pci_seg_list);	/* list of all PCI segments */
LIST_HEAD(amd_iommu_list);		/* list of all AMD IOMMUs in the
					   system */

/* Number of IOMMUs present in the system */
static int amd_iommus_present;

/* IOMMUs have a non-present cache? */
bool amd_iommu_np_cache __read_mostly;
bool amd_iommu_iotlb_sup __read_mostly = true;

static bool amd_iommu_pc_present __read_mostly;
bool amdr_ivrs_remap_support __read_mostly;

bool amd_iommu_force_isolation __read_mostly;

unsigned long amd_iommu_pgsize_bitmap __ro_after_init = AMD_IOMMU_PGSIZES;

enum iommu_init_state {
	IOMMU_START_STATE,
	IOMMU_IVRS_DETECTED,
	IOMMU_ACPI_FINISHED,
	IOMMU_ENABLED,
	IOMMU_PCI_INIT,
	IOMMU_INTERRUPTS_EN,
	IOMMU_INITIALIZED,
	IOMMU_NOT_FOUND,
	IOMMU_INIT_ERROR,
	IOMMU_CMDLINE_DISABLED,
};

/* Early ioapic and hpet maps from kernel command line */
#define EARLY_MAP_SIZE		4
static struct devid_map __initdata early_ioapic_map[EARLY_MAP_SIZE];
static struct devid_map __initdata early_hpet_map[EARLY_MAP_SIZE];
static struct acpihid_map_entry __initdata early_acpihid_map[EARLY_MAP_SIZE];

static int __initdata early_ioapic_map_size;
static int __initdata early_hpet_map_size;
static int __initdata early_acpihid_map_size;

static bool __initdata cmdline_maps;

static enum iommu_init_state init_state = IOMMU_START_STATE;

static int amd_iommu_enable_interrupts(void);
static int __init iommu_go_to_state(enum iommu_init_state state);
static void init_device_table_dma(struct amd_iommu_pci_seg *pci_seg);

static bool amd_iommu_pre_enabled = true;

static u32 amd_iommu_ivinfo __initdata;

bool translation_pre_enabled(struct amd_iommu *iommu)
{
	return (iommu->flags & AMD_IOMMU_FLAG_TRANS_PRE_ENABLED);
}

static void clear_translation_pre_enabled(struct amd_iommu *iommu)
{
	iommu->flags &= ~AMD_IOMMU_FLAG_TRANS_PRE_ENABLED;
}

static void init_translation_status(struct amd_iommu *iommu)
{
	u64 ctrl;

	ctrl = readq(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	if (ctrl & (1<<CONTROL_IOMMU_EN))
		iommu->flags |= AMD_IOMMU_FLAG_TRANS_PRE_ENABLED;
}

static inline unsigned long tbl_size(int entry_size, int last_bdf)
{
	unsigned shift = PAGE_SHIFT +
			 get_order((last_bdf + 1) * entry_size);

	return 1UL << shift;
}

int amd_iommu_get_num_iommus(void)
{
	return amd_iommus_present;
}

/*
 * Iterate through all the IOMMUs to get common EFR
 * masks among all IOMMUs and warn if found inconsistency.
 */
static __init void get_global_efr(void)
{
	struct amd_iommu *iommu;

	for_each_iommu(iommu) {
		u64 tmp = iommu->features;
		u64 tmp2 = iommu->features2;

		if (list_is_first(&iommu->list, &amd_iommu_list)) {
			amd_iommu_efr = tmp;
			amd_iommu_efr2 = tmp2;
			continue;
		}

		if (amd_iommu_efr == tmp &&
		    amd_iommu_efr2 == tmp2)
			continue;

		pr_err(FW_BUG
		       "Found inconsistent EFR/EFR2 %#llx,%#llx (global %#llx,%#llx) on iommu%d (%04x:%02x:%02x.%01x).\n",
		       tmp, tmp2, amd_iommu_efr, amd_iommu_efr2,
		       iommu->index, iommu->pci_seg->id,
		       PCI_BUS_NUM(iommu->devid), PCI_SLOT(iommu->devid),
		       PCI_FUNC(iommu->devid));

		amd_iommu_efr &= tmp;
		amd_iommu_efr2 &= tmp2;
	}

	pr_info("Using global IVHD EFR:%#llx, EFR2:%#llx\n", amd_iommu_efr, amd_iommu_efr2);
}

/*
 * For IVHD type 0x11/0x40, EFR is also available via IVHD.
 * Default to IVHD EFR since it is available sooner
 * (i.e. before PCI init).
 */
static void __init early_iommu_features_init(struct amd_iommu *iommu,
					     struct ivhd_header *h)
{
	if (amd_iommu_ivinfo & IOMMU_IVINFO_EFRSUP) {
		iommu->features = h->efr_reg;
		iommu->features2 = h->efr_reg2;
	}
	if (amd_iommu_ivinfo & IOMMU_IVINFO_DMA_REMAP)
		amdr_ivrs_remap_support = true;
}

/* Access to l1 and l2 indexed register spaces */

static u32 iommu_read_l1(struct amd_iommu *iommu, u16 l1, u8 address)
{
	u32 val;

	pci_write_config_dword(iommu->dev, 0xf8, (address | l1 << 16));
	pci_read_config_dword(iommu->dev, 0xfc, &val);
	return val;
}

static void iommu_write_l1(struct amd_iommu *iommu, u16 l1, u8 address, u32 val)
{
	pci_write_config_dword(iommu->dev, 0xf8, (address | l1 << 16 | 1 << 31));
	pci_write_config_dword(iommu->dev, 0xfc, val);
	pci_write_config_dword(iommu->dev, 0xf8, (address | l1 << 16));
}

static u32 iommu_read_l2(struct amd_iommu *iommu, u8 address)
{
	u32 val;

	pci_write_config_dword(iommu->dev, 0xf0, address);
	pci_read_config_dword(iommu->dev, 0xf4, &val);
	return val;
}

static void iommu_write_l2(struct amd_iommu *iommu, u8 address, u32 val)
{
	pci_write_config_dword(iommu->dev, 0xf0, (address | 1 << 8));
	pci_write_config_dword(iommu->dev, 0xf4, val);
}

/****************************************************************************
 *
 * AMD IOMMU MMIO register space handling functions
 *
 * These functions are used to program the IOMMU device registers in
 * MMIO space required for that driver.
 *
 ****************************************************************************/

/*
 * This function set the exclusion range in the IOMMU. DMA accesses to the
 * exclusion range are passed through untranslated
 */
static void iommu_set_exclusion_range(struct amd_iommu *iommu)
{
	u64 start = iommu->exclusion_start & PAGE_MASK;
	u64 limit = (start + iommu->exclusion_length - 1) & PAGE_MASK;
	u64 entry;

	if (!iommu->exclusion_start)
		return;

	entry = start | MMIO_EXCL_ENABLE_MASK;
	memcpy_toio(iommu->mmio_base + MMIO_EXCL_BASE_OFFSET,
			&entry, sizeof(entry));

	entry = limit;
	memcpy_toio(iommu->mmio_base + MMIO_EXCL_LIMIT_OFFSET,
			&entry, sizeof(entry));
}

static void iommu_set_cwwb_range(struct amd_iommu *iommu)
{
	u64 start = iommu_virt_to_phys((void *)iommu->cmd_sem);
	u64 entry = start & PM_ADDR_MASK;

	if (!check_feature(FEATURE_SNP))
		return;

	/* Note:
	 * Re-purpose Exclusion base/limit registers for Completion wait
	 * write-back base/limit.
	 */
	memcpy_toio(iommu->mmio_base + MMIO_EXCL_BASE_OFFSET,
		    &entry, sizeof(entry));

	/* Note:
	 * Default to 4 Kbytes, which can be specified by setting base
	 * address equal to the limit address.
	 */
	memcpy_toio(iommu->mmio_base + MMIO_EXCL_LIMIT_OFFSET,
		    &entry, sizeof(entry));
}

/* Programs the physical address of the device table into the IOMMU hardware */
static void iommu_set_device_table(struct amd_iommu *iommu)
{
	u64 entry;
	u32 dev_table_size = iommu->pci_seg->dev_table_size;
	void *dev_table = (void *)get_dev_table(iommu);

	BUG_ON(iommu->mmio_base == NULL);

	entry = iommu_virt_to_phys(dev_table);
	entry |= (dev_table_size >> 12) - 1;
	memcpy_toio(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET,
			&entry, sizeof(entry));
}

/* Generic functions to enable/disable certain features of the IOMMU. */
void iommu_feature_enable(struct amd_iommu *iommu, u8 bit)
{
	u64 ctrl;

	ctrl = readq(iommu->mmio_base +  MMIO_CONTROL_OFFSET);
	ctrl |= (1ULL << bit);
	writeq(ctrl, iommu->mmio_base +  MMIO_CONTROL_OFFSET);
}

static void iommu_feature_disable(struct amd_iommu *iommu, u8 bit)
{
	u64 ctrl;

	ctrl = readq(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl &= ~(1ULL << bit);
	writeq(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

static void iommu_set_inv_tlb_timeout(struct amd_iommu *iommu, int timeout)
{
	u64 ctrl;

	ctrl = readq(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl &= ~CTRL_INV_TO_MASK;
	ctrl |= (timeout << CONTROL_INV_TIMEOUT) & CTRL_INV_TO_MASK;
	writeq(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

/* Function to enable the hardware */
static void iommu_enable(struct amd_iommu *iommu)
{
	iommu_feature_enable(iommu, CONTROL_IOMMU_EN);
}

static void iommu_disable(struct amd_iommu *iommu)
{
	if (!iommu->mmio_base)
		return;

	/* Disable command buffer */
	iommu_feature_disable(iommu, CONTROL_CMDBUF_EN);

	/* Disable event logging and event interrupts */
	iommu_feature_disable(iommu, CONTROL_EVT_INT_EN);
	iommu_feature_disable(iommu, CONTROL_EVT_LOG_EN);

	/* Disable IOMMU GA_LOG */
	iommu_feature_disable(iommu, CONTROL_GALOG_EN);
	iommu_feature_disable(iommu, CONTROL_GAINT_EN);

	/* Disable IOMMU PPR logging */
	iommu_feature_disable(iommu, CONTROL_PPRLOG_EN);
	iommu_feature_disable(iommu, CONTROL_PPRINT_EN);

	/* Disable IOMMU hardware itself */
	iommu_feature_disable(iommu, CONTROL_IOMMU_EN);

	/* Clear IRTE cache disabling bit */
	iommu_feature_disable(iommu, CONTROL_IRTCACHEDIS);
}

/*
 * mapping and unmapping functions for the IOMMU MMIO space. Each AMD IOMMU in
 * the system has one.
 */
static u8 __iomem * __init iommu_map_mmio_space(u64 address, u64 end)
{
	if (!request_mem_region(address, end, "amd_iommu")) {
		pr_err("Can not reserve memory region %llx-%llx for mmio\n",
			address, end);
		pr_err("This is a BIOS bug. Please contact your hardware vendor\n");
		return NULL;
	}

	return (u8 __iomem *)ioremap(address, end);
}

static void __init iommu_unmap_mmio_space(struct amd_iommu *iommu)
{
	if (iommu->mmio_base)
		iounmap(iommu->mmio_base);
	release_mem_region(iommu->mmio_phys, iommu->mmio_phys_end);
}

static inline u32 get_ivhd_header_size(struct ivhd_header *h)
{
	u32 size = 0;

	switch (h->type) {
	case 0x10:
		size = 24;
		break;
	case 0x11:
	case 0x40:
		size = 40;
		break;
	}
	return size;
}

/****************************************************************************
 *
 * The functions below belong to the first pass of AMD IOMMU ACPI table
 * parsing. In this pass we try to find out the highest device id this
 * code has to handle. Upon this information the size of the shared data
 * structures is determined later.
 *
 ****************************************************************************/

/*
 * This function calculates the length of a given IVHD entry
 */
static inline int ivhd_entry_length(u8 *ivhd)
{
	u32 type = ((struct ivhd_entry *)ivhd)->type;

	if (type < 0x80) {
		return 0x04 << (*ivhd >> 6);
	} else if (type == IVHD_DEV_ACPI_HID) {
		/* For ACPI_HID, offset 21 is uid len */
		return *((u8 *)ivhd + 21) + 22;
	}
	return 0;
}

/*
 * After reading the highest device id from the IOMMU PCI capability header
 * this function looks if there is a higher device id defined in the ACPI table
 */
static int __init find_last_devid_from_ivhd(struct ivhd_header *h)
{
	u8 *p = (void *)h, *end = (void *)h;
	struct ivhd_entry *dev;
	int last_devid = -EINVAL;

	u32 ivhd_size = get_ivhd_header_size(h);

	if (!ivhd_size) {
		pr_err("Unsupported IVHD type %#x\n", h->type);
		return -EINVAL;
	}

	p += ivhd_size;
	end += h->length;

	while (p < end) {
		dev = (struct ivhd_entry *)p;
		switch (dev->type) {
		case IVHD_DEV_ALL:
			/* Use maximum BDF value for DEV_ALL */
			return 0xffff;
		case IVHD_DEV_SELECT:
		case IVHD_DEV_RANGE_END:
		case IVHD_DEV_ALIAS:
		case IVHD_DEV_EXT_SELECT:
			/* all the above subfield types refer to device ids */
			if (dev->devid > last_devid)
				last_devid = dev->devid;
			break;
		default:
			break;
		}
		p += ivhd_entry_length(p);
	}

	WARN_ON(p != end);

	return last_devid;
}

static int __init check_ivrs_checksum(struct acpi_table_header *table)
{
	int i;
	u8 checksum = 0, *p = (u8 *)table;

	for (i = 0; i < table->length; ++i)
		checksum += p[i];
	if (checksum != 0) {
		/* ACPI table corrupt */
		pr_err(FW_BUG "IVRS invalid checksum\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * Iterate over all IVHD entries in the ACPI table and find the highest device
 * id which we need to handle. This is the first of three functions which parse
 * the ACPI table. So we check the checksum here.
 */
static int __init find_last_devid_acpi(struct acpi_table_header *table, u16 pci_seg)
{
	u8 *p = (u8 *)table, *end = (u8 *)table;
	struct ivhd_header *h;
	int last_devid, last_bdf = 0;

	p += IVRS_HEADER_LENGTH;

	end += table->length;
	while (p < end) {
		h = (struct ivhd_header *)p;
		if (h->pci_seg == pci_seg &&
		    h->type == amd_iommu_target_ivhd_type) {
			last_devid = find_last_devid_from_ivhd(h);

			if (last_devid < 0)
				return -EINVAL;
			if (last_devid > last_bdf)
				last_bdf = last_devid;
		}
		p += h->length;
	}
	WARN_ON(p != end);

	return last_bdf;
}

/****************************************************************************
 *
 * The following functions belong to the code path which parses the ACPI table
 * the second time. In this ACPI parsing iteration we allocate IOMMU specific
 * data structures, initialize the per PCI segment device/alias/rlookup table
 * and also basically initialize the hardware.
 *
 ****************************************************************************/

/* Allocate per PCI segment device table */
static inline int __init alloc_dev_table(struct amd_iommu_pci_seg *pci_seg)
{
	pci_seg->dev_table = iommu_alloc_pages(GFP_KERNEL | GFP_DMA32,
					       get_order(pci_seg->dev_table_size));
	if (!pci_seg->dev_table)
		return -ENOMEM;

	return 0;
}

static inline void free_dev_table(struct amd_iommu_pci_seg *pci_seg)
{
	iommu_free_pages(pci_seg->dev_table,
			 get_order(pci_seg->dev_table_size));
	pci_seg->dev_table = NULL;
}

/* Allocate per PCI segment IOMMU rlookup table. */
static inline int __init alloc_rlookup_table(struct amd_iommu_pci_seg *pci_seg)
{
	pci_seg->rlookup_table = iommu_alloc_pages(GFP_KERNEL,
						   get_order(pci_seg->rlookup_table_size));
	if (pci_seg->rlookup_table == NULL)
		return -ENOMEM;

	return 0;
}

static inline void free_rlookup_table(struct amd_iommu_pci_seg *pci_seg)
{
	iommu_free_pages(pci_seg->rlookup_table,
			 get_order(pci_seg->rlookup_table_size));
	pci_seg->rlookup_table = NULL;
}

static inline int __init alloc_irq_lookup_table(struct amd_iommu_pci_seg *pci_seg)
{
	pci_seg->irq_lookup_table = iommu_alloc_pages(GFP_KERNEL,
						      get_order(pci_seg->rlookup_table_size));
	kmemleak_alloc(pci_seg->irq_lookup_table,
		       pci_seg->rlookup_table_size, 1, GFP_KERNEL);
	if (pci_seg->irq_lookup_table == NULL)
		return -ENOMEM;

	return 0;
}

static inline void free_irq_lookup_table(struct amd_iommu_pci_seg *pci_seg)
{
	kmemleak_free(pci_seg->irq_lookup_table);
	iommu_free_pages(pci_seg->irq_lookup_table,
			 get_order(pci_seg->rlookup_table_size));
	pci_seg->irq_lookup_table = NULL;
}

static int __init alloc_alias_table(struct amd_iommu_pci_seg *pci_seg)
{
	int i;

	pci_seg->alias_table = iommu_alloc_pages(GFP_KERNEL,
						 get_order(pci_seg->alias_table_size));
	if (!pci_seg->alias_table)
		return -ENOMEM;

	/*
	 * let all alias entries point to itself
	 */
	for (i = 0; i <= pci_seg->last_bdf; ++i)
		pci_seg->alias_table[i] = i;

	return 0;
}

static void __init free_alias_table(struct amd_iommu_pci_seg *pci_seg)
{
	iommu_free_pages(pci_seg->alias_table,
			 get_order(pci_seg->alias_table_size));
	pci_seg->alias_table = NULL;
}

/*
 * Allocates the command buffer. This buffer is per AMD IOMMU. We can
 * write commands to that buffer later and the IOMMU will execute them
 * asynchronously
 */
static int __init alloc_command_buffer(struct amd_iommu *iommu)
{
	iommu->cmd_buf = iommu_alloc_pages(GFP_KERNEL,
					   get_order(CMD_BUFFER_SIZE));

	return iommu->cmd_buf ? 0 : -ENOMEM;
}

/*
 * Interrupt handler has processed all pending events and adjusted head
 * and tail pointer. Reset overflow mask and restart logging again.
 */
void amd_iommu_restart_log(struct amd_iommu *iommu, const char *evt_type,
			   u8 cntrl_intr, u8 cntrl_log,
			   u32 status_run_mask, u32 status_overflow_mask)
{
	u32 status;

	status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
	if (status & status_run_mask)
		return;

	pr_info_ratelimited("IOMMU %s log restarting\n", evt_type);

	iommu_feature_disable(iommu, cntrl_log);
	iommu_feature_disable(iommu, cntrl_intr);

	writel(status_overflow_mask, iommu->mmio_base + MMIO_STATUS_OFFSET);

	iommu_feature_enable(iommu, cntrl_intr);
	iommu_feature_enable(iommu, cntrl_log);
}

/*
 * This function restarts event logging in case the IOMMU experienced
 * an event log buffer overflow.
 */
void amd_iommu_restart_event_logging(struct amd_iommu *iommu)
{
	amd_iommu_restart_log(iommu, "Event", CONTROL_EVT_INT_EN,
			      CONTROL_EVT_LOG_EN, MMIO_STATUS_EVT_RUN_MASK,
			      MMIO_STATUS_EVT_OVERFLOW_MASK);
}

/*
 * This function restarts event logging in case the IOMMU experienced
 * GA log overflow.
 */
void amd_iommu_restart_ga_log(struct amd_iommu *iommu)
{
	amd_iommu_restart_log(iommu, "GA", CONTROL_GAINT_EN,
			      CONTROL_GALOG_EN, MMIO_STATUS_GALOG_RUN_MASK,
			      MMIO_STATUS_GALOG_OVERFLOW_MASK);
}

/*
 * This function resets the command buffer if the IOMMU stopped fetching
 * commands from it.
 */
static void amd_iommu_reset_cmd_buffer(struct amd_iommu *iommu)
{
	iommu_feature_disable(iommu, CONTROL_CMDBUF_EN);

	writel(0x00, iommu->mmio_base + MMIO_CMD_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);
	iommu->cmd_buf_head = 0;
	iommu->cmd_buf_tail = 0;

	iommu_feature_enable(iommu, CONTROL_CMDBUF_EN);
}

/*
 * This function writes the command buffer address to the hardware and
 * enables it.
 */
static void iommu_enable_command_buffer(struct amd_iommu *iommu)
{
	u64 entry;

	BUG_ON(iommu->cmd_buf == NULL);

	entry = iommu_virt_to_phys(iommu->cmd_buf);
	entry |= MMIO_CMD_SIZE_512;

	memcpy_toio(iommu->mmio_base + MMIO_CMD_BUF_OFFSET,
		    &entry, sizeof(entry));

	amd_iommu_reset_cmd_buffer(iommu);
}

/*
 * This function disables the command buffer
 */
static void iommu_disable_command_buffer(struct amd_iommu *iommu)
{
	iommu_feature_disable(iommu, CONTROL_CMDBUF_EN);
}

static void __init free_command_buffer(struct amd_iommu *iommu)
{
	iommu_free_pages(iommu->cmd_buf, get_order(CMD_BUFFER_SIZE));
}

void *__init iommu_alloc_4k_pages(struct amd_iommu *iommu, gfp_t gfp,
				  size_t size)
{
	int order = get_order(size);
	void *buf = iommu_alloc_pages(gfp, order);

	if (buf &&
	    check_feature(FEATURE_SNP) &&
	    set_memory_4k((unsigned long)buf, (1 << order))) {
		iommu_free_pages(buf, order);
		buf = NULL;
	}

	return buf;
}

/* allocates the memory where the IOMMU will log its events to */
static int __init alloc_event_buffer(struct amd_iommu *iommu)
{
	iommu->evt_buf = iommu_alloc_4k_pages(iommu, GFP_KERNEL,
					      EVT_BUFFER_SIZE);

	return iommu->evt_buf ? 0 : -ENOMEM;
}

static void iommu_enable_event_buffer(struct amd_iommu *iommu)
{
	u64 entry;

	BUG_ON(iommu->evt_buf == NULL);

	entry = iommu_virt_to_phys(iommu->evt_buf) | EVT_LEN_MASK;

	memcpy_toio(iommu->mmio_base + MMIO_EVT_BUF_OFFSET,
		    &entry, sizeof(entry));

	/* set head and tail to zero manually */
	writel(0x00, iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_EVT_TAIL_OFFSET);

	iommu_feature_enable(iommu, CONTROL_EVT_LOG_EN);
}

/*
 * This function disables the event log buffer
 */
static void iommu_disable_event_buffer(struct amd_iommu *iommu)
{
	iommu_feature_disable(iommu, CONTROL_EVT_LOG_EN);
}

static void __init free_event_buffer(struct amd_iommu *iommu)
{
	iommu_free_pages(iommu->evt_buf, get_order(EVT_BUFFER_SIZE));
}

static void free_ga_log(struct amd_iommu *iommu)
{
#ifdef CONFIG_IRQ_REMAP
	iommu_free_pages(iommu->ga_log, get_order(GA_LOG_SIZE));
	iommu_free_pages(iommu->ga_log_tail, get_order(8));
#endif
}

#ifdef CONFIG_IRQ_REMAP
static int iommu_ga_log_enable(struct amd_iommu *iommu)
{
	u32 status, i;
	u64 entry;

	if (!iommu->ga_log)
		return -EINVAL;

	entry = iommu_virt_to_phys(iommu->ga_log) | GA_LOG_SIZE_512;
	memcpy_toio(iommu->mmio_base + MMIO_GA_LOG_BASE_OFFSET,
		    &entry, sizeof(entry));
	entry = (iommu_virt_to_phys(iommu->ga_log_tail) &
		 (BIT_ULL(52)-1)) & ~7ULL;
	memcpy_toio(iommu->mmio_base + MMIO_GA_LOG_TAIL_OFFSET,
		    &entry, sizeof(entry));
	writel(0x00, iommu->mmio_base + MMIO_GA_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_GA_TAIL_OFFSET);


	iommu_feature_enable(iommu, CONTROL_GAINT_EN);
	iommu_feature_enable(iommu, CONTROL_GALOG_EN);

	for (i = 0; i < MMIO_STATUS_TIMEOUT; ++i) {
		status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
		if (status & (MMIO_STATUS_GALOG_RUN_MASK))
			break;
		udelay(10);
	}

	if (WARN_ON(i >= MMIO_STATUS_TIMEOUT))
		return -EINVAL;

	return 0;
}

static int iommu_init_ga_log(struct amd_iommu *iommu)
{
	if (!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir))
		return 0;

	iommu->ga_log = iommu_alloc_pages(GFP_KERNEL, get_order(GA_LOG_SIZE));
	if (!iommu->ga_log)
		goto err_out;

	iommu->ga_log_tail = iommu_alloc_pages(GFP_KERNEL, get_order(8));
	if (!iommu->ga_log_tail)
		goto err_out;

	return 0;
err_out:
	free_ga_log(iommu);
	return -EINVAL;
}
#endif /* CONFIG_IRQ_REMAP */

static int __init alloc_cwwb_sem(struct amd_iommu *iommu)
{
	iommu->cmd_sem = iommu_alloc_4k_pages(iommu, GFP_KERNEL, 1);

	return iommu->cmd_sem ? 0 : -ENOMEM;
}

static void __init free_cwwb_sem(struct amd_iommu *iommu)
{
	if (iommu->cmd_sem)
		iommu_free_page((void *)iommu->cmd_sem);
}

static void iommu_enable_xt(struct amd_iommu *iommu)
{
#ifdef CONFIG_IRQ_REMAP
	/*
	 * XT mode (32-bit APIC destination ID) requires
	 * GA mode (128-bit IRTE support) as a prerequisite.
	 */
	if (AMD_IOMMU_GUEST_IR_GA(amd_iommu_guest_ir) &&
	    amd_iommu_xt_mode == IRQ_REMAP_X2APIC_MODE)
		iommu_feature_enable(iommu, CONTROL_XT_EN);
#endif /* CONFIG_IRQ_REMAP */
}

static void iommu_enable_gt(struct amd_iommu *iommu)
{
	if (!check_feature(FEATURE_GT))
		return;

	iommu_feature_enable(iommu, CONTROL_GT_EN);
}

/* sets a specific bit in the device table entry. */
static void __set_dev_entry_bit(struct dev_table_entry *dev_table,
				u16 devid, u8 bit)
{
	int i = (bit >> 6) & 0x03;
	int _bit = bit & 0x3f;

	dev_table[devid].data[i] |= (1UL << _bit);
}

static void set_dev_entry_bit(struct amd_iommu *iommu, u16 devid, u8 bit)
{
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	return __set_dev_entry_bit(dev_table, devid, bit);
}

static int __get_dev_entry_bit(struct dev_table_entry *dev_table,
			       u16 devid, u8 bit)
{
	int i = (bit >> 6) & 0x03;
	int _bit = bit & 0x3f;

	return (dev_table[devid].data[i] & (1UL << _bit)) >> _bit;
}

static int get_dev_entry_bit(struct amd_iommu *iommu, u16 devid, u8 bit)
{
	struct dev_table_entry *dev_table = get_dev_table(iommu);

	return __get_dev_entry_bit(dev_table, devid, bit);
}

static bool __copy_device_table(struct amd_iommu *iommu)
{
	u64 int_ctl, int_tab_len, entry = 0;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;
	struct dev_table_entry *old_devtb = NULL;
	u32 lo, hi, devid, old_devtb_size;
	phys_addr_t old_devtb_phys;
	u16 dom_id, dte_v, irq_v;
	u64 tmp;

	/* Each IOMMU use separate device table with the same size */
	lo = readl(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET);
	hi = readl(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET + 4);
	entry = (((u64) hi) << 32) + lo;

	old_devtb_size = ((entry & ~PAGE_MASK) + 1) << 12;
	if (old_devtb_size != pci_seg->dev_table_size) {
		pr_err("The device table size of IOMMU:%d is not expected!\n",
			iommu->index);
		return false;
	}

	/*
	 * When SME is enabled in the first kernel, the entry includes the
	 * memory encryption mask(sme_me_mask), we must remove the memory
	 * encryption mask to obtain the true physical address in kdump kernel.
	 */
	old_devtb_phys = __sme_clr(entry) & PAGE_MASK;

	if (old_devtb_phys >= 0x100000000ULL) {
		pr_err("The address of old device table is above 4G, not trustworthy!\n");
		return false;
	}
	old_devtb = (cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT) && is_kdump_kernel())
		    ? (__force void *)ioremap_encrypted(old_devtb_phys,
							pci_seg->dev_table_size)
		    : memremap(old_devtb_phys, pci_seg->dev_table_size, MEMREMAP_WB);

	if (!old_devtb)
		return false;

	pci_seg->old_dev_tbl_cpy = iommu_alloc_pages(GFP_KERNEL | GFP_DMA32,
						     get_order(pci_seg->dev_table_size));
	if (pci_seg->old_dev_tbl_cpy == NULL) {
		pr_err("Failed to allocate memory for copying old device table!\n");
		memunmap(old_devtb);
		return false;
	}

	for (devid = 0; devid <= pci_seg->last_bdf; ++devid) {
		pci_seg->old_dev_tbl_cpy[devid] = old_devtb[devid];
		dom_id = old_devtb[devid].data[1] & DEV_DOMID_MASK;
		dte_v = old_devtb[devid].data[0] & DTE_FLAG_V;

		if (dte_v && dom_id) {
			pci_seg->old_dev_tbl_cpy[devid].data[0] = old_devtb[devid].data[0];
			pci_seg->old_dev_tbl_cpy[devid].data[1] = old_devtb[devid].data[1];
			/* Reserve the Domain IDs used by previous kernel */
			if (ida_alloc_range(&pdom_ids, dom_id, dom_id, GFP_ATOMIC) != dom_id) {
				pr_err("Failed to reserve domain ID 0x%x\n", dom_id);
				memunmap(old_devtb);
				return false;
			}
			/* If gcr3 table existed, mask it out */
			if (old_devtb[devid].data[0] & DTE_FLAG_GV) {
				tmp = DTE_GCR3_VAL_B(~0ULL) << DTE_GCR3_SHIFT_B;
				tmp |= DTE_GCR3_VAL_C(~0ULL) << DTE_GCR3_SHIFT_C;
				pci_seg->old_dev_tbl_cpy[devid].data[1] &= ~tmp;
				tmp = DTE_GCR3_VAL_A(~0ULL) << DTE_GCR3_SHIFT_A;
				tmp |= DTE_FLAG_GV;
				pci_seg->old_dev_tbl_cpy[devid].data[0] &= ~tmp;
			}
		}

		irq_v = old_devtb[devid].data[2] & DTE_IRQ_REMAP_ENABLE;
		int_ctl = old_devtb[devid].data[2] & DTE_IRQ_REMAP_INTCTL_MASK;
		int_tab_len = old_devtb[devid].data[2] & DTE_INTTABLEN_MASK;
		if (irq_v && (int_ctl || int_tab_len)) {
			if ((int_ctl != DTE_IRQ_REMAP_INTCTL) ||
			    (int_tab_len != DTE_INTTABLEN)) {
				pr_err("Wrong old irq remapping flag: %#x\n", devid);
				memunmap(old_devtb);
				return false;
			}

			pci_seg->old_dev_tbl_cpy[devid].data[2] = old_devtb[devid].data[2];
		}
	}
	memunmap(old_devtb);

	return true;
}

static bool copy_device_table(void)
{
	struct amd_iommu *iommu;
	struct amd_iommu_pci_seg *pci_seg;

	if (!amd_iommu_pre_enabled)
		return false;

	pr_warn("Translation is already enabled - trying to copy translation structures\n");

	/*
	 * All IOMMUs within PCI segment shares common device table.
	 * Hence copy device table only once per PCI segment.
	 */
	for_each_pci_segment(pci_seg) {
		for_each_iommu(iommu) {
			if (pci_seg->id != iommu->pci_seg->id)
				continue;
			if (!__copy_device_table(iommu))
				return false;
			break;
		}
	}

	return true;
}

void amd_iommu_apply_erratum_63(struct amd_iommu *iommu, u16 devid)
{
	int sysmgt;

	sysmgt = get_dev_entry_bit(iommu, devid, DEV_ENTRY_SYSMGT1) |
		 (get_dev_entry_bit(iommu, devid, DEV_ENTRY_SYSMGT2) << 1);

	if (sysmgt == 0x01)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_IW);
}

/*
 * This function takes the device specific flags read from the ACPI
 * table and sets up the device table entry with that information
 */
static void __init set_dev_entry_from_acpi(struct amd_iommu *iommu,
					   u16 devid, u32 flags, u32 ext_flags)
{
	if (flags & ACPI_DEVFLAG_INITPASS)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_INIT_PASS);
	if (flags & ACPI_DEVFLAG_EXTINT)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_EINT_PASS);
	if (flags & ACPI_DEVFLAG_NMI)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_NMI_PASS);
	if (flags & ACPI_DEVFLAG_SYSMGT1)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_SYSMGT1);
	if (flags & ACPI_DEVFLAG_SYSMGT2)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_SYSMGT2);
	if (flags & ACPI_DEVFLAG_LINT0)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_LINT0_PASS);
	if (flags & ACPI_DEVFLAG_LINT1)
		set_dev_entry_bit(iommu, devid, DEV_ENTRY_LINT1_PASS);

	amd_iommu_apply_erratum_63(iommu, devid);

	amd_iommu_set_rlookup_table(iommu, devid);
}

int __init add_special_device(u8 type, u8 id, u32 *devid, bool cmd_line)
{
	struct devid_map *entry;
	struct list_head *list;

	if (type == IVHD_SPECIAL_IOAPIC)
		list = &ioapic_map;
	else if (type == IVHD_SPECIAL_HPET)
		list = &hpet_map;
	else
		return -EINVAL;

	list_for_each_entry(entry, list, list) {
		if (!(entry->id == id && entry->cmd_line))
			continue;

		pr_info("Command-line override present for %s id %d - ignoring\n",
			type == IVHD_SPECIAL_IOAPIC ? "IOAPIC" : "HPET", id);

		*devid = entry->devid;

		return 0;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->id	= id;
	entry->devid	= *devid;
	entry->cmd_line	= cmd_line;

	list_add_tail(&entry->list, list);

	return 0;
}

static int __init add_acpi_hid_device(u8 *hid, u8 *uid, u32 *devid,
				      bool cmd_line)
{
	struct acpihid_map_entry *entry;
	struct list_head *list = &acpihid_map;

	list_for_each_entry(entry, list, list) {
		if (strcmp(entry->hid, hid) ||
		    (*uid && *entry->uid && strcmp(entry->uid, uid)) ||
		    !entry->cmd_line)
			continue;

		pr_info("Command-line override for hid:%s uid:%s\n",
			hid, uid);
		*devid = entry->devid;
		return 0;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	memcpy(entry->uid, uid, strlen(uid));
	memcpy(entry->hid, hid, strlen(hid));
	entry->devid = *devid;
	entry->cmd_line	= cmd_line;
	entry->root_devid = (entry->devid & (~0x7));

	pr_info("%s, add hid:%s, uid:%s, rdevid:%d\n",
		entry->cmd_line ? "cmd" : "ivrs",
		entry->hid, entry->uid, entry->root_devid);

	list_add_tail(&entry->list, list);
	return 0;
}

static int __init add_early_maps(void)
{
	int i, ret;

	for (i = 0; i < early_ioapic_map_size; ++i) {
		ret = add_special_device(IVHD_SPECIAL_IOAPIC,
					 early_ioapic_map[i].id,
					 &early_ioapic_map[i].devid,
					 early_ioapic_map[i].cmd_line);
		if (ret)
			return ret;
	}

	for (i = 0; i < early_hpet_map_size; ++i) {
		ret = add_special_device(IVHD_SPECIAL_HPET,
					 early_hpet_map[i].id,
					 &early_hpet_map[i].devid,
					 early_hpet_map[i].cmd_line);
		if (ret)
			return ret;
	}

	for (i = 0; i < early_acpihid_map_size; ++i) {
		ret = add_acpi_hid_device(early_acpihid_map[i].hid,
					  early_acpihid_map[i].uid,
					  &early_acpihid_map[i].devid,
					  early_acpihid_map[i].cmd_line);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Takes a pointer to an AMD IOMMU entry in the ACPI table and
 * initializes the hardware and our data structures with it.
 */
static int __init init_iommu_from_acpi(struct amd_iommu *iommu,
					struct ivhd_header *h)
{
	u8 *p = (u8 *)h;
	u8 *end = p, flags = 0;
	u16 devid = 0, devid_start = 0, devid_to = 0, seg_id;
	u32 dev_i, ext_flags = 0;
	bool alias = false;
	struct ivhd_entry *e;
	struct amd_iommu_pci_seg *pci_seg = iommu->pci_seg;
	u32 ivhd_size;
	int ret;


	ret = add_early_maps();
	if (ret)
		return ret;

	amd_iommu_apply_ivrs_quirks();

	/*
	 * First save the recommended feature enable bits from ACPI
	 */
	iommu->acpi_flags = h->flags;

	/*
	 * Done. Now parse the device entries
	 */
	ivhd_size = get_ivhd_header_size(h);
	if (!ivhd_size) {
		pr_err("Unsupported IVHD type %#x\n", h->type);
		return -EINVAL;
	}

	p += ivhd_size;

	end += h->length;


	while (p < end) {
		e = (struct ivhd_entry *)p;
		seg_id = pci_seg->id;

		switch (e->type) {
		case IVHD_DEV_ALL:

			DUMP_printk("  DEV_ALL\t\t\tflags: %02x\n", e->flags);

			for (dev_i = 0; dev_i <= pci_seg->last_bdf; ++dev_i)
				set_dev_entry_from_acpi(iommu, dev_i, e->flags, 0);
			break;
		case IVHD_DEV_SELECT:

			DUMP_printk("  DEV_SELECT\t\t\t devid: %04x:%02x:%02x.%x "
				    "flags: %02x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags);

			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);
			break;
		case IVHD_DEV_SELECT_RANGE_START:

			DUMP_printk("  DEV_SELECT_RANGE_START\t "
				    "devid: %04x:%02x:%02x.%x flags: %02x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags);

			devid_start = e->devid;
			flags = e->flags;
			ext_flags = 0;
			alias = false;
			break;
		case IVHD_DEV_ALIAS:

			DUMP_printk("  DEV_ALIAS\t\t\t devid: %04x:%02x:%02x.%x "
				    "flags: %02x devid_to: %02x:%02x.%x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags,
				    PCI_BUS_NUM(e->ext >> 8),
				    PCI_SLOT(e->ext >> 8),
				    PCI_FUNC(e->ext >> 8));

			devid = e->devid;
			devid_to = e->ext >> 8;
			set_dev_entry_from_acpi(iommu, devid   , e->flags, 0);
			set_dev_entry_from_acpi(iommu, devid_to, e->flags, 0);
			pci_seg->alias_table[devid] = devid_to;
			break;
		case IVHD_DEV_ALIAS_RANGE:

			DUMP_printk("  DEV_ALIAS_RANGE\t\t "
				    "devid: %04x:%02x:%02x.%x flags: %02x "
				    "devid_to: %04x:%02x:%02x.%x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags,
				    seg_id, PCI_BUS_NUM(e->ext >> 8),
				    PCI_SLOT(e->ext >> 8),
				    PCI_FUNC(e->ext >> 8));

			devid_start = e->devid;
			flags = e->flags;
			devid_to = e->ext >> 8;
			ext_flags = 0;
			alias = true;
			break;
		case IVHD_DEV_EXT_SELECT:

			DUMP_printk("  DEV_EXT_SELECT\t\t devid: %04x:%02x:%02x.%x "
				    "flags: %02x ext: %08x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags, e->ext);

			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags,
						e->ext);
			break;
		case IVHD_DEV_EXT_SELECT_RANGE:

			DUMP_printk("  DEV_EXT_SELECT_RANGE\t devid: "
				    "%04x:%02x:%02x.%x flags: %02x ext: %08x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags, e->ext);

			devid_start = e->devid;
			flags = e->flags;
			ext_flags = e->ext;
			alias = false;
			break;
		case IVHD_DEV_RANGE_END:

			DUMP_printk("  DEV_RANGE_END\t\t devid: %04x:%02x:%02x.%x\n",
				    seg_id, PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid));

			devid = e->devid;
			for (dev_i = devid_start; dev_i <= devid; ++dev_i) {
				if (alias) {
					pci_seg->alias_table[dev_i] = devid_to;
					set_dev_entry_from_acpi(iommu,
						devid_to, flags, ext_flags);
				}
				set_dev_entry_from_acpi(iommu, dev_i,
							flags, ext_flags);
			}
			break;
		case IVHD_DEV_SPECIAL: {
			u8 handle, type;
			const char *var;
			u32 devid;
			int ret;

			handle = e->ext & 0xff;
			devid = PCI_SEG_DEVID_TO_SBDF(seg_id, (e->ext >> 8));
			type   = (e->ext >> 24) & 0xff;

			if (type == IVHD_SPECIAL_IOAPIC)
				var = "IOAPIC";
			else if (type == IVHD_SPECIAL_HPET)
				var = "HPET";
			else
				var = "UNKNOWN";

			DUMP_printk("  DEV_SPECIAL(%s[%d])\t\tdevid: %04x:%02x:%02x.%x\n",
				    var, (int)handle,
				    seg_id, PCI_BUS_NUM(devid),
				    PCI_SLOT(devid),
				    PCI_FUNC(devid));

			ret = add_special_device(type, handle, &devid, false);
			if (ret)
				return ret;

			/*
			 * add_special_device might update the devid in case a
			 * command-line override is present. So call
			 * set_dev_entry_from_acpi after add_special_device.
			 */
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);

			break;
		}
		case IVHD_DEV_ACPI_HID: {
			u32 devid;
			u8 hid[ACPIHID_HID_LEN];
			u8 uid[ACPIHID_UID_LEN];
			int ret;

			if (h->type != 0x40) {
				pr_err(FW_BUG "Invalid IVHD device type %#x\n",
				       e->type);
				break;
			}

			BUILD_BUG_ON(sizeof(e->ext_hid) != ACPIHID_HID_LEN - 1);
			memcpy(hid, &e->ext_hid, ACPIHID_HID_LEN - 1);
			hid[ACPIHID_HID_LEN - 1] = '\0';

			if (!(*hid)) {
				pr_err(FW_BUG "Invalid HID.\n");
				break;
			}

			uid[0] = '\0';
			switch (e->uidf) {
			case UID_NOT_PRESENT:

				if (e->uidl != 0)
					pr_warn(FW_BUG "Invalid UID length.\n");

				break;
			case UID_IS_INTEGER:

				sprintf(uid, "%d", e->uid);

				break;
			case UID_IS_CHARACTER:

				memcpy(uid, &e->uid, e->uidl);
				uid[e->uidl] = '\0';

				break;
			default:
				break;
			}

			devid = PCI_SEG_DEVID_TO_SBDF(seg_id, e->devid);
			DUMP_printk("  DEV_ACPI_HID(%s[%s])\t\tdevid: %04x:%02x:%02x.%x\n",
				    hid, uid, seg_id,
				    PCI_BUS_NUM(devid),
				    PCI_SLOT(devid),
				    PCI_FUNC(devid));

			flags = e->flags;

			ret = add_acpi_hid_device(hid, uid, &devid, false);
			if (ret)
				return ret;

			/*
			 * add_special_device might update the devid in case a
			 * command-line override is present. So call
			 * set_dev_entry_from_acpi after add_special_device.
			 */
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);

			break;
		}
		default:
			break;
		}

		p += ivhd_entry_length(p);
	}

	return 0;
}

/* Allocate PCI segment data structure */
static struct amd_iommu_pci_seg *__init alloc_pci_segment(u16 id,
					  struct acpi_table_header *ivrs_base)
{
	struct amd_iommu_pci_seg *pci_seg;
	int last_bdf;

	/*
	 * First parse ACPI tables to find the largest Bus/Dev/Func we need to
	 * handle in this PCI segment. Upon this information the shared data
	 * structures for the PCI segments in the system will be allocated.
	 */
	last_bdf = find_last_devid_acpi(ivrs_base, id);
	if (last_bdf < 0)
		return NULL;

	pci_seg = kzalloc(sizeof(struct amd_iommu_pci_seg), GFP_KERNEL);
	if (pci_seg == NULL)
		return NULL;

	pci_seg->last_bdf = last_bdf;
	DUMP_printk("PCI segment : 0x%0x, last bdf : 0x%04x\n", id, last_bdf);
	pci_seg->dev_table_size     = tbl_size(DEV_TABLE_ENTRY_SIZE, last_bdf);
	pci_seg->alias_table_size   = tbl_size(ALIAS_TABLE_ENTRY_SIZE, last_bdf);
	pci_seg->rlookup_table_size = tbl_size(RLOOKUP_TABLE_ENTRY_SIZE, last_bdf);

	pci_seg->id = id;
	init_llist_head(&pci_seg->dev_data_list);
	INIT_LIST_HEAD(&pci_seg->unity_map);
	list_add_tail(&pci_seg->list, &amd_iommu_pci_seg_list);

	if (alloc_dev_table(pci_seg))
		return NULL;
	if (alloc_alias_table(pci_seg))
		return NULL;
	if (alloc_rlookup_table(pci_seg))
		return NULL;

	return pci_seg;
}

static struct amd_iommu_pci_seg *__init get_pci_segment(u16 id,
					struct acpi_table_header *ivrs_base)
{
	struct amd_iommu_pci_seg *pci_seg;

	for_each_pci_segment(pci_seg) {
		if (pci_seg->id == id)
			return pci_seg;
	}

	return alloc_pci_segment(id, ivrs_base);
}

static void __init free_pci_segments(void)
{
	struct amd_iommu_pci_seg *pci_seg, *next;

	for_each_pci_segment_safe(pci_seg, next) {
		list_del(&pci_seg->list);
		free_irq_lookup_table(pci_seg);
		free_rlookup_table(pci_seg);
		free_alias_table(pci_seg);
		free_dev_table(pci_seg);
		kfree(pci_seg);
	}
}

static void __init free_sysfs(struct amd_iommu *iommu)
{
	if (iommu->iommu.dev) {
		iommu_device_unregister(&iommu->iommu);
		iommu_device_sysfs_remove(&iommu->iommu);
	}
}

static void __init free_iommu_one(struct amd_iommu *iommu)
{
	free_sysfs(iommu);
	free_cwwb_sem(iommu);
	free_command_buffer(iommu);
	free_event_buffer(iommu);
	amd_iommu_free_ppr_log(iommu);
	free_ga_log(iommu);
	iommu_unmap_mmio_space(iommu);
	amd_iommu_iopf_uninit(iommu);
}

static void __init free_iommu_all(void)
{
	struct amd_iommu *iommu, *next;

	for_each_iommu_safe(iommu, next) {
		list_del(&iommu->list);
		free_iommu_one(iommu);
		kfree(iommu);
	}
}

/*
 * Family15h Model 10h-1fh erratum 746 (IOMMU Logging May Stall Translations)
 * Workaround:
 *     BIOS should disable L2B micellaneous clock gating by setting
 *     L2_L2B_CK_GATE_CONTROL[CKGateL2BMiscDisable](D0F2xF4_x90[2]) = 1b
 */
static void amd_iommu_erratum_746_workaround(struct amd_iommu *iommu)
{
	u32 value;

	if ((boot_cpu_data.x86 != 0x15) ||
	    (boot_cpu_data.x86_model < 0x10) ||
	    (boot_cpu_data.x86_model > 0x1f))
		return;

	pci_write_config_dword(iommu->dev, 0xf0, 0x90);
	pci_read_config_dword(iommu->dev, 0xf4, &value);

	if (value & BIT(2))
		return;

	/* Select NB indirect register 0x90 and enable writing */
	pci_write_config_dword(iommu->dev, 0xf0, 0x90 | (1 << 8));

	pci_write_config_dword(iommu->dev, 0xf4, value | 0x4);
	pci_info(iommu->dev, "Applying erratum 746 workaround\n");

	/* Clear the enable writing bit */
	pci_write_config_dword(iommu->dev, 0xf0, 0x90);
}

/*
 * Family15h Model 30h-3fh (IOMMU Mishandles ATS Write Permission)
 * Workaround:
 *     BIOS should enable ATS write permission check by setting
 *     L2_DEBUG_3[AtsIgnoreIWDis](D0F2xF4_x47[0]) = 1b
 */
static void amd_iommu_ats_write_check_workaround(struct amd_iommu *iommu)
{
	u32 value;

	if ((boot_cpu_data.x86 != 0x15) ||
	    (boot_cpu_data.x86_model < 0x30) ||
	    (boot_cpu_data.x86_model > 0x3f))
		return;

	/* Test L2_DEBUG_3[AtsIgnoreIWDis] == 1 */
	value = iommu_read_l2(iommu, 0x47);

	if (value & BIT(0))
		return;

	/* Set L2_DEBUG_3[AtsIgnoreIWDis] = 1 */
	iommu_write_l2(iommu, 0x47, value | BIT(0));

	pci_info(iommu->dev, "Applying ATS write check workaround\n");
}

/*
 * This function glues the initialization function for one IOMMU
 * together and also allocates the command buffer and programs the
 * hardware. It does NOT enable the IOMMU. This is done afterwards.
 */
static int __init init_iommu_one(struct amd_iommu *iommu, struct ivhd_header *h,
				 struct acpi_table_header *ivrs_base)
{
	struct amd_iommu_pci_seg *pci_seg;

	pci_seg = get_pci_segment(h->pci_seg, ivrs_base);
	if (pci_seg == NULL)
		return -ENOMEM;
	iommu->pci_seg = pci_seg;

	raw_spin_lock_init(&iommu->lock);
	atomic64_set(&iommu->cmd_sem_val, 0);

	/* Add IOMMU to internal data structures */
	list_add_tail(&iommu->list, &amd_iommu_list);
	iommu->index = amd_iommus_present++;

	if (unlikely(iommu->index >= MAX_IOMMUS)) {
		WARN(1, "System has more IOMMUs than supported by this driver\n");
		return -ENOSYS;
	}

	/*
	 * Copy data from ACPI table entry to the iommu struct
	 */
	iommu->devid   = h->devid;
	iommu->cap_ptr = h->cap_ptr;
	iommu->mmio_phys = h->mmio_phys;

	switch (h->type) {
	case 0x10:
		/* Check if IVHD EFR contains proper max banks/counters */
		if ((h->efr_attr != 0) &&
		    ((h->efr_attr & (0xF << 13)) != 0) &&
		    ((h->efr_attr & (0x3F << 17)) != 0))
			iommu->mmio_phys_end = MMIO_REG_END_OFFSET;
		else
			iommu->mmio_phys_end = MMIO_CNTR_CONF_OFFSET;

		/*
		 * Note: GA (128-bit IRTE) mode requires cmpxchg16b supports.
		 * GAM also requires GA mode. Therefore, we need to
		 * check cmpxchg16b support before enabling it.
		 */
		if (!boot_cpu_has(X86_FEATURE_CX16) ||
		    ((h->efr_attr & (0x1 << IOMMU_FEAT_GASUP_SHIFT)) == 0))
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY;
		break;
	case 0x11:
	case 0x40:
		if (h->efr_reg & (1 << 9))
			iommu->mmio_phys_end = MMIO_REG_END_OFFSET;
		else
			iommu->mmio_phys_end = MMIO_CNTR_CONF_OFFSET;

		/*
		 * Note: GA (128-bit IRTE) mode requires cmpxchg16b supports.
		 * XT, GAM also requires GA mode. Therefore, we need to
		 * check cmpxchg16b support before enabling them.
		 */
		if (!boot_cpu_has(X86_FEATURE_CX16) ||
		    ((h->efr_reg & (0x1 << IOMMU_EFR_GASUP_SHIFT)) == 0)) {
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY;
			break;
		}

		if (h->efr_reg & BIT(IOMMU_EFR_XTSUP_SHIFT))
			amd_iommu_xt_mode = IRQ_REMAP_X2APIC_MODE;

		early_iommu_features_init(iommu, h);

		break;
	default:
		return -EINVAL;
	}

	iommu->mmio_base = iommu_map_mmio_space(iommu->mmio_phys,
						iommu->mmio_phys_end);
	if (!iommu->mmio_base)
		return -ENOMEM;

	return init_iommu_from_acpi(iommu, h);
}

static int __init init_iommu_one_late(struct amd_iommu *iommu)
{
	int ret;

	if (alloc_cwwb_sem(iommu))
		return -ENOMEM;

	if (alloc_command_buffer(iommu))
		return -ENOMEM;

	if (alloc_event_buffer(iommu))
		return -ENOMEM;

	iommu->int_enabled = false;

	init_translation_status(iommu);
	if (translation_pre_enabled(iommu) && !is_kdump_kernel()) {
		iommu_disable(iommu);
		clear_translation_pre_enabled(iommu);
		pr_warn("Translation was enabled for IOMMU:%d but we are not in kdump mode\n",
			iommu->index);
	}
	if (amd_iommu_pre_enabled)
		amd_iommu_pre_enabled = translation_pre_enabled(iommu);

	if (amd_iommu_irq_remap) {
		ret = amd_iommu_create_irq_domain(iommu);
		if (ret)
			return ret;
	}

	/*
	 * Make sure IOMMU is not considered to translate itself. The IVRS
	 * table tells us so, but this is a lie!
	 */
	iommu->pci_seg->rlookup_table[iommu->devid] = NULL;

	return 0;
}

/**
 * get_highest_supported_ivhd_type - Look up the appropriate IVHD type
 * @ivrs: Pointer to the IVRS header
 *
 * This function search through all IVDB of the maximum supported IVHD
 */
static u8 get_highest_supported_ivhd_type(struct acpi_table_header *ivrs)
{
	u8 *base = (u8 *)ivrs;
	struct ivhd_header *ivhd = (struct ivhd_header *)
					(base + IVRS_HEADER_LENGTH);
	u8 last_type = ivhd->type;
	u16 devid = ivhd->devid;

	while (((u8 *)ivhd - base < ivrs->length) &&
	       (ivhd->type <= ACPI_IVHD_TYPE_MAX_SUPPORTED)) {
		u8 *p = (u8 *) ivhd;

		if (ivhd->devid == devid)
			last_type = ivhd->type;
		ivhd = (struct ivhd_header *)(p + ivhd->length);
	}

	return last_type;
}

/*
 * Iterates over all IOMMU entries in the ACPI table, allocates the
 * IOMMU structure and initializes it with init_iommu_one()
 */
static int __init init_iommu_all(struct acpi_table_header *table)
{
	u8 *p = (u8 *)table, *end = (u8 *)table;
	struct ivhd_header *h;
	struct amd_iommu *iommu;
	int ret;

	end += table->length;
	p += IVRS_HEADER_LENGTH;

	/* Phase 1: Process all IVHD blocks */
	while (p < end) {
		h = (struct ivhd_header *)p;
		if (*p == amd_iommu_target_ivhd_type) {

			DUMP_printk("device: %04x:%02x:%02x.%01x cap: %04x "
				    "flags: %01x info %04x\n",
				    h->pci_seg, PCI_BUS_NUM(h->devid),
				    PCI_SLOT(h->devid), PCI_FUNC(h->devid),
				    h->cap_ptr, h->flags, h->info);
			DUMP_printk("       mmio-addr: %016llx\n",
				    h->mmio_phys);

			iommu = kzalloc(sizeof(struct amd_iommu), GFP_KERNEL);
			if (iommu == NULL)
				return -ENOMEM;

			ret = init_iommu_one(iommu, h, table);
			if (ret)
				return ret;
		}
		p += h->length;

	}
	WARN_ON(p != end);

	/* Phase 2 : Early feature support check */
	get_global_efr();

	/* Phase 3 : Enabling IOMMU features */
	for_each_iommu(iommu) {
		ret = init_iommu_one_late(iommu);
		if (ret)
			return ret;
	}

	return 0;
}

static void init_iommu_perf_ctr(struct amd_iommu *iommu)
{
	u64 val;
	struct pci_dev *pdev = iommu->dev;

	if (!check_feature(FEATURE_PC))
		return;

	amd_iommu_pc_present = true;

	pci_info(pdev, "IOMMU performance counters supported\n");

	val = readl(iommu->mmio_base + MMIO_CNTR_CONF_OFFSET);
	iommu->max_banks = (u8) ((val >> 12) & 0x3f);
	iommu->max_counters = (u8) ((val >> 7) & 0xf);

	return;
}

static ssize_t amd_iommu_show_cap(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct amd_iommu *iommu = dev_to_amd_iommu(dev);
	return sysfs_emit(buf, "%x\n", iommu->cap);
}
static DEVICE_ATTR(cap, S_IRUGO, amd_iommu_show_cap, NULL);

static ssize_t amd_iommu_show_features(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	return sysfs_emit(buf, "%llx:%llx\n", amd_iommu_efr, amd_iommu_efr2);
}
static DEVICE_ATTR(features, S_IRUGO, amd_iommu_show_features, NULL);

static struct attribute *amd_iommu_attrs[] = {
	&dev_attr_cap.attr,
	&dev_attr_features.attr,
	NULL,
};

static struct attribute_group amd_iommu_group = {
	.name = "amd-iommu",
	.attrs = amd_iommu_attrs,
};

static const struct attribute_group *amd_iommu_groups[] = {
	&amd_iommu_group,
	NULL,
};

/*
 * Note: IVHD 0x11 and 0x40 also contains exact copy
 * of the IOMMU Extended Feature Register [MMIO Offset 0030h].
 * Default to EFR in IVHD since it is available sooner (i.e. before PCI init).
 */
static void __init late_iommu_features_init(struct amd_iommu *iommu)
{
	u64 features, features2;

	if (!(iommu->cap & (1 << IOMMU_CAP_EFR)))
		return;

	/* read extended feature bits */
	features = readq(iommu->mmio_base + MMIO_EXT_FEATURES);
	features2 = readq(iommu->mmio_base + MMIO_EXT_FEATURES2);

	if (!amd_iommu_efr) {
		amd_iommu_efr = features;
		amd_iommu_efr2 = features2;
		return;
	}

	/*
	 * Sanity check and warn if EFR values from
	 * IVHD and MMIO conflict.
	 */
	if (features != amd_iommu_efr ||
	    features2 != amd_iommu_efr2) {
		pr_warn(FW_WARN
			"EFR mismatch. Use IVHD EFR (%#llx : %#llx), EFR2 (%#llx : %#llx).\n",
			features, amd_iommu_efr,
			features2, amd_iommu_efr2);
	}
}

static int __init iommu_init_pci(struct amd_iommu *iommu)
{
	int cap_ptr = iommu->cap_ptr;
	int ret;

	iommu->dev = pci_get_domain_bus_and_slot(iommu->pci_seg->id,
						 PCI_BUS_NUM(iommu->devid),
						 iommu->devid & 0xff);
	if (!iommu->dev)
		return -ENODEV;

	/* Prevent binding other PCI device drivers to IOMMU devices */
	iommu->dev->match_driver = false;

	/* ACPI _PRT won't have an IRQ for IOMMU */
	iommu->dev->irq_managed = 1;

	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_CAP_HDR_OFFSET,
			      &iommu->cap);

	if (!(iommu->cap & (1 << IOMMU_CAP_IOTLB)))
		amd_iommu_iotlb_sup = false;

	late_iommu_features_init(iommu);

	if (check_feature(FEATURE_GT)) {
		int glxval;
		u64 pasmax;

		pasmax = FIELD_GET(FEATURE_PASMAX, amd_iommu_efr);
		iommu->iommu.max_pasids = (1 << (pasmax + 1)) - 1;

		BUG_ON(iommu->iommu.max_pasids & ~PASID_MASK);

		glxval = FIELD_GET(FEATURE_GLX, amd_iommu_efr);

		if (amd_iommu_max_glx_val == -1)
			amd_iommu_max_glx_val = glxval;
		else
			amd_iommu_max_glx_val = min(amd_iommu_max_glx_val, glxval);

		iommu_enable_gt(iommu);
	}

	if (check_feature(FEATURE_PPR) && amd_iommu_alloc_ppr_log(iommu))
		return -ENOMEM;

	if (iommu->cap & (1UL << IOMMU_CAP_NPCACHE)) {
		pr_info("Using strict mode due to virtualization\n");
		iommu_set_dma_strict();
		amd_iommu_np_cache = true;
	}

	init_iommu_perf_ctr(iommu);

	if (is_rd890_iommu(iommu->dev)) {
		int i, j;

		iommu->root_pdev =
			pci_get_domain_bus_and_slot(iommu->pci_seg->id,
						    iommu->dev->bus->number,
						    PCI_DEVFN(0, 0));

		/*
		 * Some rd890 systems may not be fully reconfigured by the
		 * BIOS, so it's necessary for us to store this information so
		 * it can be reprogrammed on resume
		 */
		pci_read_config_dword(iommu->dev, iommu->cap_ptr + 4,
				&iommu->stored_addr_lo);
		pci_read_config_dword(iommu->dev, iommu->cap_ptr + 8,
				&iommu->stored_addr_hi);

		/* Low bit locks writes to configuration space */
		iommu->stored_addr_lo &= ~1;

		for (i = 0; i < 6; i++)
			for (j = 0; j < 0x12; j++)
				iommu->stored_l1[i][j] = iommu_read_l1(iommu, i, j);

		for (i = 0; i < 0x83; i++)
			iommu->stored_l2[i] = iommu_read_l2(iommu, i);
	}

	amd_iommu_erratum_746_workaround(iommu);
	amd_iommu_ats_write_check_workaround(iommu);

	ret = iommu_device_sysfs_add(&iommu->iommu, &iommu->dev->dev,
			       amd_iommu_groups, "ivhd%d", iommu->index);
	if (ret)
		return ret;

	/*
	 * Allocate per IOMMU IOPF queue here so that in attach device path,
	 * PRI capable device can be added to IOPF queue
	 */
	if (amd_iommu_gt_ppr_supported()) {
		ret = amd_iommu_iopf_init(iommu);
		if (ret)
			return ret;
	}

	iommu_device_register(&iommu->iommu, &amd_iommu_ops, NULL);

	return pci_enable_device(iommu->dev);
}

static void print_iommu_info(void)
{
	int i;
	static const char * const feat_str[] = {
		"PreF", "PPR", "X2APIC", "NX", "GT", "[5]",
		"IA", "GA", "HE", "PC"
	};

	if (amd_iommu_efr) {
		pr_info("Extended features (%#llx, %#llx):", amd_iommu_efr, amd_iommu_efr2);

		for (i = 0; i < ARRAY_SIZE(feat_str); ++i) {
			if (check_feature(1ULL << i))
				pr_cont(" %s", feat_str[i]);
		}

		if (check_feature(FEATURE_GAM_VAPIC))
			pr_cont(" GA_vAPIC");

		if (check_feature(FEATURE_SNP))
			pr_cont(" SNP");

		pr_cont("\n");
	}

	if (irq_remapping_enabled) {
		pr_info("Interrupt remapping enabled\n");
		if (amd_iommu_xt_mode == IRQ_REMAP_X2APIC_MODE)
			pr_info("X2APIC enabled\n");
	}
	if (amd_iommu_pgtable == AMD_IOMMU_V2) {
		pr_info("V2 page table enabled (Paging mode : %d level)\n",
			amd_iommu_gpt_level);
	}
}

static int __init amd_iommu_init_pci(void)
{
	struct amd_iommu *iommu;
	struct amd_iommu_pci_seg *pci_seg;
	int ret;

	/* Init global identity domain before registering IOMMU */
	amd_iommu_init_identity_domain();

	for_each_iommu(iommu) {
		ret = iommu_init_pci(iommu);
		if (ret) {
			pr_err("IOMMU%d: Failed to initialize IOMMU Hardware (error=%d)!\n",
			       iommu->index, ret);
			goto out;
		}
		/* Need to setup range after PCI init */
		iommu_set_cwwb_range(iommu);
	}

	/*
	 * Order is important here to make sure any unity map requirements are
	 * fulfilled. The unity mappings are created and written to the device
	 * table during the iommu_init_pci() call.
	 *
	 * After that we call init_device_table_dma() to make sure any
	 * uninitialized DTE will block DMA, and in the end we flush the caches
	 * of all IOMMUs to make sure the changes to the device table are
	 * active.
	 */
	for_each_pci_segment(pci_seg)
		init_device_table_dma(pci_seg);

	for_each_iommu(iommu)
		amd_iommu_flush_all_caches(iommu);

	print_iommu_info();

out:
	return ret;
}

/****************************************************************************
 *
 * The following functions initialize the MSI interrupts for all IOMMUs
 * in the system. It's a bit challenging because there could be multiple
 * IOMMUs per PCI BDF but we can call pci_enable_msi(x) only once per
 * pci_dev.
 *
 ****************************************************************************/

static int iommu_setup_msi(struct amd_iommu *iommu)
{
	int r;

	r = pci_enable_msi(iommu->dev);
	if (r)
		return r;

	r = request_threaded_irq(iommu->dev->irq,
				 amd_iommu_int_handler,
				 amd_iommu_int_thread,
				 0, "AMD-Vi",
				 iommu);

	if (r) {
		pci_disable_msi(iommu->dev);
		return r;
	}

	return 0;
}

union intcapxt {
	u64	capxt;
	struct {
		u64	reserved_0		:  2,
			dest_mode_logical	:  1,
			reserved_1		:  5,
			destid_0_23		: 24,
			vector			:  8,
			reserved_2		: 16,
			destid_24_31		:  8;
	};
} __attribute__ ((packed));


static struct irq_chip intcapxt_controller;

static int intcapxt_irqdomain_activate(struct irq_domain *domain,
				       struct irq_data *irqd, bool reserve)
{
	return 0;
}

static void intcapxt_irqdomain_deactivate(struct irq_domain *domain,
					  struct irq_data *irqd)
{
}


static int intcapxt_irqdomain_alloc(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	int i, ret;

	if (!info || info->type != X86_IRQ_ALLOC_TYPE_AMDVI)
		return -EINVAL;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret < 0)
		return ret;

	for (i = virq; i < virq + nr_irqs; i++) {
		struct irq_data *irqd = irq_domain_get_irq_data(domain, i);

		irqd->chip = &intcapxt_controller;
		irqd->hwirq = info->hwirq;
		irqd->chip_data = info->data;
		__irq_set_handler(i, handle_edge_irq, 0, "edge");
	}

	return ret;
}

static void intcapxt_irqdomain_free(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs)
{
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}


static void intcapxt_unmask_irq(struct irq_data *irqd)
{
	struct amd_iommu *iommu = irqd->chip_data;
	struct irq_cfg *cfg = irqd_cfg(irqd);
	union intcapxt xt;

	xt.capxt = 0ULL;
	xt.dest_mode_logical = apic->dest_mode_logical;
	xt.vector = cfg->vector;
	xt.destid_0_23 = cfg->dest_apicid & GENMASK(23, 0);
	xt.destid_24_31 = cfg->dest_apicid >> 24;

	writeq(xt.capxt, iommu->mmio_base + irqd->hwirq);
}

static void intcapxt_mask_irq(struct irq_data *irqd)
{
	struct amd_iommu *iommu = irqd->chip_data;

	writeq(0, iommu->mmio_base + irqd->hwirq);
}


static int intcapxt_set_affinity(struct irq_data *irqd,
				 const struct cpumask *mask, bool force)
{
	struct irq_data *parent = irqd->parent_data;
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;
	return 0;
}

static int intcapxt_set_wake(struct irq_data *irqd, unsigned int on)
{
	return on ? -EOPNOTSUPP : 0;
}

static struct irq_chip intcapxt_controller = {
	.name			= "IOMMU-MSI",
	.irq_unmask		= intcapxt_unmask_irq,
	.irq_mask		= intcapxt_mask_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_affinity       = intcapxt_set_affinity,
	.irq_set_wake		= intcapxt_set_wake,
	.flags			= IRQCHIP_MASK_ON_SUSPEND,
};

static const struct irq_domain_ops intcapxt_domain_ops = {
	.alloc			= intcapxt_irqdomain_alloc,
	.free			= intcapxt_irqdomain_free,
	.activate		= intcapxt_irqdomain_activate,
	.deactivate		= intcapxt_irqdomain_deactivate,
};


static struct irq_domain *iommu_irqdomain;

static struct irq_domain *iommu_get_irqdomain(void)
{
	struct fwnode_handle *fn;

	/* No need for locking here (yet) as the init is single-threaded */
	if (iommu_irqdomain)
		return iommu_irqdomain;

	fn = irq_domain_alloc_named_fwnode("AMD-Vi-MSI");
	if (!fn)
		return NULL;

	iommu_irqdomain = irq_domain_create_hierarchy(x86_vector_domain, 0, 0,
						      fn, &intcapxt_domain_ops,
						      NULL);
	if (!iommu_irqdomain)
		irq_domain_free_fwnode(fn);

	return iommu_irqdomain;
}

static int __iommu_setup_intcapxt(struct amd_iommu *iommu, const char *devname,
				  int hwirq, irq_handler_t thread_fn)
{
	struct irq_domain *domain;
	struct irq_alloc_info info;
	int irq, ret;
	int node = dev_to_node(&iommu->dev->dev);

	domain = iommu_get_irqdomain();
	if (!domain)
		return -ENXIO;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_AMDVI;
	info.data = iommu;
	info.hwirq = hwirq;

	irq = irq_domain_alloc_irqs(domain, 1, node, &info);
	if (irq < 0) {
		irq_domain_remove(domain);
		return irq;
	}

	ret = request_threaded_irq(irq, amd_iommu_int_handler,
				   thread_fn, 0, devname, iommu);
	if (ret) {
		irq_domain_free_irqs(irq, 1);
		irq_domain_remove(domain);
		return ret;
	}

	return 0;
}

static int iommu_setup_intcapxt(struct amd_iommu *iommu)
{
	int ret;

	snprintf(iommu->evt_irq_name, sizeof(iommu->evt_irq_name),
		 "AMD-Vi%d-Evt", iommu->index);
	ret = __iommu_setup_intcapxt(iommu, iommu->evt_irq_name,
				     MMIO_INTCAPXT_EVT_OFFSET,
				     amd_iommu_int_thread_evtlog);
	if (ret)
		return ret;

	snprintf(iommu->ppr_irq_name, sizeof(iommu->ppr_irq_name),
		 "AMD-Vi%d-PPR", iommu->index);
	ret = __iommu_setup_intcapxt(iommu, iommu->ppr_irq_name,
				     MMIO_INTCAPXT_PPR_OFFSET,
				     amd_iommu_int_thread_pprlog);
	if (ret)
		return ret;

#ifdef CONFIG_IRQ_REMAP
	snprintf(iommu->ga_irq_name, sizeof(iommu->ga_irq_name),
		 "AMD-Vi%d-GA", iommu->index);
	ret = __iommu_setup_intcapxt(iommu, iommu->ga_irq_name,
				     MMIO_INTCAPXT_GALOG_OFFSET,
				     amd_iommu_int_thread_galog);
#endif

	return ret;
}

static int iommu_init_irq(struct amd_iommu *iommu)
{
	int ret;

	if (iommu->int_enabled)
		goto enable_faults;

	if (amd_iommu_xt_mode == IRQ_REMAP_X2APIC_MODE)
		ret = iommu_setup_intcapxt(iommu);
	else if (iommu->dev->msi_cap)
		ret = iommu_setup_msi(iommu);
	else
		ret = -ENODEV;

	if (ret)
		return ret;

	iommu->int_enabled = true;
enable_faults:

	if (amd_iommu_xt_mode == IRQ_REMAP_X2APIC_MODE)
		iommu_feature_enable(iommu, CONTROL_INTCAPXT_EN);

	iommu_feature_enable(iommu, CONTROL_EVT_INT_EN);

	return 0;
}

/****************************************************************************
 *
 * The next functions belong to the third pass of parsing the ACPI
 * table. In this last pass the memory mapping requirements are
 * gathered (like exclusion and unity mapping ranges).
 *
 ****************************************************************************/

static void __init free_unity_maps(void)
{
	struct unity_map_entry *entry, *next;
	struct amd_iommu_pci_seg *p, *pci_seg;

	for_each_pci_segment_safe(pci_seg, p) {
		list_for_each_entry_safe(entry, next, &pci_seg->unity_map, list) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
}

/* called for unity map ACPI definition */
static int __init init_unity_map_range(struct ivmd_header *m,
				       struct acpi_table_header *ivrs_base)
{
	struct unity_map_entry *e = NULL;
	struct amd_iommu_pci_seg *pci_seg;
	char *s;

	pci_seg = get_pci_segment(m->pci_seg, ivrs_base);
	if (pci_seg == NULL)
		return -ENOMEM;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	switch (m->type) {
	default:
		kfree(e);
		return 0;
	case ACPI_IVMD_TYPE:
		s = "IVMD_TYPEi\t\t\t";
		e->devid_start = e->devid_end = m->devid;
		break;
	case ACPI_IVMD_TYPE_ALL:
		s = "IVMD_TYPE_ALL\t\t";
		e->devid_start = 0;
		e->devid_end = pci_seg->last_bdf;
		break;
	case ACPI_IVMD_TYPE_RANGE:
		s = "IVMD_TYPE_RANGE\t\t";
		e->devid_start = m->devid;
		e->devid_end = m->aux;
		break;
	}
	e->address_start = PAGE_ALIGN(m->range_start);
	e->address_end = e->address_start + PAGE_ALIGN(m->range_length);
	e->prot = m->flags >> 1;

	/*
	 * Treat per-device exclusion ranges as r/w unity-mapped regions
	 * since some buggy BIOSes might lead to the overwritten exclusion
	 * range (exclusion_start and exclusion_length members). This
	 * happens when there are multiple exclusion ranges (IVMD entries)
	 * defined in ACPI table.
	 */
	if (m->flags & IVMD_FLAG_EXCL_RANGE)
		e->prot = (IVMD_FLAG_IW | IVMD_FLAG_IR) >> 1;

	DUMP_printk("%s devid_start: %04x:%02x:%02x.%x devid_end: "
		    "%04x:%02x:%02x.%x range_start: %016llx range_end: %016llx"
		    " flags: %x\n", s, m->pci_seg,
		    PCI_BUS_NUM(e->devid_start), PCI_SLOT(e->devid_start),
		    PCI_FUNC(e->devid_start), m->pci_seg,
		    PCI_BUS_NUM(e->devid_end),
		    PCI_SLOT(e->devid_end), PCI_FUNC(e->devid_end),
		    e->address_start, e->address_end, m->flags);

	list_add_tail(&e->list, &pci_seg->unity_map);

	return 0;
}

/* iterates over all memory definitions we find in the ACPI table */
static int __init init_memory_definitions(struct acpi_table_header *table)
{
	u8 *p = (u8 *)table, *end = (u8 *)table;
	struct ivmd_header *m;

	end += table->length;
	p += IVRS_HEADER_LENGTH;

	while (p < end) {
		m = (struct ivmd_header *)p;
		if (m->flags & (IVMD_FLAG_UNITY_MAP | IVMD_FLAG_EXCL_RANGE))
			init_unity_map_range(m, table);

		p += m->length;
	}

	return 0;
}

/*
 * Init the device table to not allow DMA access for devices
 */
static void init_device_table_dma(struct amd_iommu_pci_seg *pci_seg)
{
	u32 devid;
	struct dev_table_entry *dev_table = pci_seg->dev_table;

	if (dev_table == NULL)
		return;

	for (devid = 0; devid <= pci_seg->last_bdf; ++devid) {
		__set_dev_entry_bit(dev_table, devid, DEV_ENTRY_VALID);
		if (!amd_iommu_snp_en)
			__set_dev_entry_bit(dev_table, devid, DEV_ENTRY_TRANSLATION);
	}
}

static void __init uninit_device_table_dma(struct amd_iommu_pci_seg *pci_seg)
{
	u32 devid;
	struct dev_table_entry *dev_table = pci_seg->dev_table;

	if (dev_table == NULL)
		return;

	for (devid = 0; devid <= pci_seg->last_bdf; ++devid) {
		dev_table[devid].data[0] = 0ULL;
		dev_table[devid].data[1] = 0ULL;
	}
}

static void init_device_table(void)
{
	struct amd_iommu_pci_seg *pci_seg;
	u32 devid;

	if (!amd_iommu_irq_remap)
		return;

	for_each_pci_segment(pci_seg) {
		for (devid = 0; devid <= pci_seg->last_bdf; ++devid)
			__set_dev_entry_bit(pci_seg->dev_table,
					    devid, DEV_ENTRY_IRQ_TBL_EN);
	}
}

static void iommu_init_flags(struct amd_iommu *iommu)
{
	iommu->acpi_flags & IVHD_FLAG_HT_TUN_EN_MASK ?
		iommu_feature_enable(iommu, CONTROL_HT_TUN_EN) :
		iommu_feature_disable(iommu, CONTROL_HT_TUN_EN);

	iommu->acpi_flags & IVHD_FLAG_PASSPW_EN_MASK ?
		iommu_feature_enable(iommu, CONTROL_PASSPW_EN) :
		iommu_feature_disable(iommu, CONTROL_PASSPW_EN);

	iommu->acpi_flags & IVHD_FLAG_RESPASSPW_EN_MASK ?
		iommu_feature_enable(iommu, CONTROL_RESPASSPW_EN) :
		iommu_feature_disable(iommu, CONTROL_RESPASSPW_EN);

	iommu->acpi_flags & IVHD_FLAG_ISOC_EN_MASK ?
		iommu_feature_enable(iommu, CONTROL_ISOC_EN) :
		iommu_feature_disable(iommu, CONTROL_ISOC_EN);

	/*
	 * make IOMMU memory accesses cache coherent
	 */
	iommu_feature_enable(iommu, CONTROL_COHERENT_EN);

	/* Set IOTLB invalidation timeout to 1s */
	iommu_set_inv_tlb_timeout(iommu, CTRL_INV_TO_1S);
}

static void iommu_apply_resume_quirks(struct amd_iommu *iommu)
{
	int i, j;
	u32 ioc_feature_control;
	struct pci_dev *pdev = iommu->root_pdev;

	/* RD890 BIOSes may not have completely reconfigured the iommu */
	if (!is_rd890_iommu(iommu->dev) || !pdev)
		return;

	/*
	 * First, we need to ensure that the iommu is enabled. This is
	 * controlled by a register in the northbridge
	 */

	/* Select Northbridge indirect register 0x75 and enable writing */
	pci_write_config_dword(pdev, 0x60, 0x75 | (1 << 7));
	pci_read_config_dword(pdev, 0x64, &ioc_feature_control);

	/* Enable the iommu */
	if (!(ioc_feature_control & 0x1))
		pci_write_config_dword(pdev, 0x64, ioc_feature_control | 1);

	/* Restore the iommu BAR */
	pci_write_config_dword(iommu->dev, iommu->cap_ptr + 4,
			       iommu->stored_addr_lo);
	pci_write_config_dword(iommu->dev, iommu->cap_ptr + 8,
			       iommu->stored_addr_hi);

	/* Restore the l1 indirect regs for each of the 6 l1s */
	for (i = 0; i < 6; i++)
		for (j = 0; j < 0x12; j++)
			iommu_write_l1(iommu, i, j, iommu->stored_l1[i][j]);

	/* Restore the l2 indirect regs */
	for (i = 0; i < 0x83; i++)
		iommu_write_l2(iommu, i, iommu->stored_l2[i]);

	/* Lock PCI setup registers */
	pci_write_config_dword(iommu->dev, iommu->cap_ptr + 4,
			       iommu->stored_addr_lo | 1);
}

static void iommu_enable_ga(struct amd_iommu *iommu)
{
#ifdef CONFIG_IRQ_REMAP
	switch (amd_iommu_guest_ir) {
	case AMD_IOMMU_GUEST_IR_VAPIC:
	case AMD_IOMMU_GUEST_IR_LEGACY_GA:
		iommu_feature_enable(iommu, CONTROL_GA_EN);
		iommu->irte_ops = &irte_128_ops;
		break;
	default:
		iommu->irte_ops = &irte_32_ops;
		break;
	}
#endif
}

static void iommu_disable_irtcachedis(struct amd_iommu *iommu)
{
	iommu_feature_disable(iommu, CONTROL_IRTCACHEDIS);
}

static void iommu_enable_irtcachedis(struct amd_iommu *iommu)
{
	u64 ctrl;

	if (!amd_iommu_irtcachedis)
		return;

	/*
	 * Note:
	 * The support for IRTCacheDis feature is dertermined by
	 * checking if the bit is writable.
	 */
	iommu_feature_enable(iommu, CONTROL_IRTCACHEDIS);
	ctrl = readq(iommu->mmio_base +  MMIO_CONTROL_OFFSET);
	ctrl &= (1ULL << CONTROL_IRTCACHEDIS);
	if (ctrl)
		iommu->irtcachedis_enabled = true;
	pr_info("iommu%d (%#06x) : IRT cache is %s\n",
		iommu->index, iommu->devid,
		iommu->irtcachedis_enabled ? "disabled" : "enabled");
}

static void early_enable_iommu(struct amd_iommu *iommu)
{
	iommu_disable(iommu);
	iommu_init_flags(iommu);
	iommu_set_device_table(iommu);
	iommu_enable_command_buffer(iommu);
	iommu_enable_event_buffer(iommu);
	iommu_set_exclusion_range(iommu);
	iommu_enable_gt(iommu);
	iommu_enable_ga(iommu);
	iommu_enable_xt(iommu);
	iommu_enable_irtcachedis(iommu);
	iommu_enable(iommu);
	amd_iommu_flush_all_caches(iommu);
}

/*
 * This function finally enables all IOMMUs found in the system after
 * they have been initialized.
 *
 * Or if in kdump kernel and IOMMUs are all pre-enabled, try to copy
 * the old content of device table entries. Not this case or copy failed,
 * just continue as normal kernel does.
 */
static void early_enable_iommus(void)
{
	struct amd_iommu *iommu;
	struct amd_iommu_pci_seg *pci_seg;

	if (!copy_device_table()) {
		/*
		 * If come here because of failure in copying device table from old
		 * kernel with all IOMMUs enabled, print error message and try to
		 * free allocated old_dev_tbl_cpy.
		 */
		if (amd_iommu_pre_enabled)
			pr_err("Failed to copy DEV table from previous kernel.\n");

		for_each_pci_segment(pci_seg) {
			if (pci_seg->old_dev_tbl_cpy != NULL) {
				iommu_free_pages(pci_seg->old_dev_tbl_cpy,
						 get_order(pci_seg->dev_table_size));
				pci_seg->old_dev_tbl_cpy = NULL;
			}
		}

		for_each_iommu(iommu) {
			clear_translation_pre_enabled(iommu);
			early_enable_iommu(iommu);
		}
	} else {
		pr_info("Copied DEV table from previous kernel.\n");

		for_each_pci_segment(pci_seg) {
			iommu_free_pages(pci_seg->dev_table,
					 get_order(pci_seg->dev_table_size));
			pci_seg->dev_table = pci_seg->old_dev_tbl_cpy;
		}

		for_each_iommu(iommu) {
			iommu_disable_command_buffer(iommu);
			iommu_disable_event_buffer(iommu);
			iommu_disable_irtcachedis(iommu);
			iommu_enable_command_buffer(iommu);
			iommu_enable_event_buffer(iommu);
			iommu_enable_ga(iommu);
			iommu_enable_xt(iommu);
			iommu_enable_irtcachedis(iommu);
			iommu_set_device_table(iommu);
			amd_iommu_flush_all_caches(iommu);
		}
	}
}

static void enable_iommus_ppr(void)
{
	struct amd_iommu *iommu;

	if (!amd_iommu_gt_ppr_supported())
		return;

	for_each_iommu(iommu)
		amd_iommu_enable_ppr_log(iommu);
}

static void enable_iommus_vapic(void)
{
#ifdef CONFIG_IRQ_REMAP
	u32 status, i;
	struct amd_iommu *iommu;

	for_each_iommu(iommu) {
		/*
		 * Disable GALog if already running. It could have been enabled
		 * in the previous boot before kdump.
		 */
		status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
		if (!(status & MMIO_STATUS_GALOG_RUN_MASK))
			continue;

		iommu_feature_disable(iommu, CONTROL_GALOG_EN);
		iommu_feature_disable(iommu, CONTROL_GAINT_EN);

		/*
		 * Need to set and poll check the GALOGRun bit to zero before
		 * we can set/ modify GA Log registers safely.
		 */
		for (i = 0; i < MMIO_STATUS_TIMEOUT; ++i) {
			status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
			if (!(status & MMIO_STATUS_GALOG_RUN_MASK))
				break;
			udelay(10);
		}

		if (WARN_ON(i >= MMIO_STATUS_TIMEOUT))
			return;
	}

	if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir) &&
	    !check_feature(FEATURE_GAM_VAPIC)) {
		amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY_GA;
		return;
	}

	if (amd_iommu_snp_en &&
	    !FEATURE_SNPAVICSUP_GAM(amd_iommu_efr2)) {
		pr_warn("Force to disable Virtual APIC due to SNP\n");
		amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY_GA;
		return;
	}

	/* Enabling GAM and SNPAVIC support */
	for_each_iommu(iommu) {
		if (iommu_init_ga_log(iommu) ||
		    iommu_ga_log_enable(iommu))
			return;

		iommu_feature_enable(iommu, CONTROL_GAM_EN);
		if (amd_iommu_snp_en)
			iommu_feature_enable(iommu, CONTROL_SNPAVIC_EN);
	}

	amd_iommu_irq_ops.capability |= (1 << IRQ_POSTING_CAP);
	pr_info("Virtual APIC enabled\n");
#endif
}

static void disable_iommus(void)
{
	struct amd_iommu *iommu;

	for_each_iommu(iommu)
		iommu_disable(iommu);

#ifdef CONFIG_IRQ_REMAP
	if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir))
		amd_iommu_irq_ops.capability &= ~(1 << IRQ_POSTING_CAP);
#endif
}

/*
 * Suspend/Resume support
 * disable suspend until real resume implemented
 */

static void amd_iommu_resume(void)
{
	struct amd_iommu *iommu;

	for_each_iommu(iommu)
		iommu_apply_resume_quirks(iommu);

	/* re-load the hardware */
	for_each_iommu(iommu)
		early_enable_iommu(iommu);

	amd_iommu_enable_interrupts();
}

static int amd_iommu_suspend(void)
{
	/* disable IOMMUs to go out of the way for BIOS */
	disable_iommus();

	return 0;
}

static struct syscore_ops amd_iommu_syscore_ops = {
	.suspend = amd_iommu_suspend,
	.resume = amd_iommu_resume,
};

static void __init free_iommu_resources(void)
{
	kmem_cache_destroy(amd_iommu_irq_cache);
	amd_iommu_irq_cache = NULL;

	free_iommu_all();
	free_pci_segments();
}

/* SB IOAPIC is always on this device in AMD systems */
#define IOAPIC_SB_DEVID		((0x00 << 8) | PCI_DEVFN(0x14, 0))

static bool __init check_ioapic_information(void)
{
	const char *fw_bug = FW_BUG;
	bool ret, has_sb_ioapic;
	int idx;

	has_sb_ioapic = false;
	ret           = false;

	/*
	 * If we have map overrides on the kernel command line the
	 * messages in this function might not describe firmware bugs
	 * anymore - so be careful
	 */
	if (cmdline_maps)
		fw_bug = "";

	for (idx = 0; idx < nr_ioapics; idx++) {
		int devid, id = mpc_ioapic_id(idx);

		devid = get_ioapic_devid(id);
		if (devid < 0) {
			pr_err("%s: IOAPIC[%d] not in IVRS table\n",
				fw_bug, id);
			ret = false;
		} else if (devid == IOAPIC_SB_DEVID) {
			has_sb_ioapic = true;
			ret           = true;
		}
	}

	if (!has_sb_ioapic) {
		/*
		 * We expect the SB IOAPIC to be listed in the IVRS
		 * table. The system timer is connected to the SB IOAPIC
		 * and if we don't have it in the list the system will
		 * panic at boot time.  This situation usually happens
		 * when the BIOS is buggy and provides us the wrong
		 * device id for the IOAPIC in the system.
		 */
		pr_err("%s: No southbridge IOAPIC found\n", fw_bug);
	}

	if (!ret)
		pr_err("Disabling interrupt remapping\n");

	return ret;
}

static void __init free_dma_resources(void)
{
	ida_destroy(&pdom_ids);

	free_unity_maps();
}

static void __init ivinfo_init(void *ivrs)
{
	amd_iommu_ivinfo = *((u32 *)(ivrs + IOMMU_IVINFO_OFFSET));
}

/*
 * This is the hardware init function for AMD IOMMU in the system.
 * This function is called either from amd_iommu_init or from the interrupt
 * remapping setup code.
 *
 * This function basically parses the ACPI table for AMD IOMMU (IVRS)
 * four times:
 *
 *	1 pass) Discover the most comprehensive IVHD type to use.
 *
 *	2 pass) Find the highest PCI device id the driver has to handle.
 *		Upon this information the size of the data structures is
 *		determined that needs to be allocated.
 *
 *	3 pass) Initialize the data structures just allocated with the
 *		information in the ACPI table about available AMD IOMMUs
 *		in the system. It also maps the PCI devices in the
 *		system to specific IOMMUs
 *
 *	4 pass) After the basic data structures are allocated and
 *		initialized we update them with information about memory
 *		remapping requirements parsed out of the ACPI table in
 *		this last pass.
 *
 * After everything is set up the IOMMUs are enabled and the necessary
 * hotplug and suspend notifiers are registered.
 */
static int __init early_amd_iommu_init(void)
{
	struct acpi_table_header *ivrs_base;
	int remap_cache_sz, ret;
	acpi_status status;

	if (!amd_iommu_detected)
		return -ENODEV;

	status = acpi_get_table("IVRS", 0, &ivrs_base);
	if (status == AE_NOT_FOUND)
		return -ENODEV;
	else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);
		pr_err("IVRS table error: %s\n", err);
		return -EINVAL;
	}

	/*
	 * Validate checksum here so we don't need to do it when
	 * we actually parse the table
	 */
	ret = check_ivrs_checksum(ivrs_base);
	if (ret)
		goto out;

	ivinfo_init(ivrs_base);

	amd_iommu_target_ivhd_type = get_highest_supported_ivhd_type(ivrs_base);
	DUMP_printk("Using IVHD type %#x\n", amd_iommu_target_ivhd_type);

	/*
	 * now the data structures are allocated and basically initialized
	 * start the real acpi table scan
	 */
	ret = init_iommu_all(ivrs_base);
	if (ret)
		goto out;

	/* 5 level guest page table */
	if (cpu_feature_enabled(X86_FEATURE_LA57) &&
	    FIELD_GET(FEATURE_GATS, amd_iommu_efr) == GUEST_PGTABLE_5_LEVEL)
		amd_iommu_gpt_level = PAGE_MODE_5_LEVEL;

	if (amd_iommu_pgtable == AMD_IOMMU_V2) {
		if (!amd_iommu_v2_pgtbl_supported()) {
			pr_warn("Cannot enable v2 page table for DMA-API. Fallback to v1.\n");
			amd_iommu_pgtable = AMD_IOMMU_V1;
		}
	}

	/* Disable any previously enabled IOMMUs */
	if (!is_kdump_kernel() || amd_iommu_disabled)
		disable_iommus();

	if (amd_iommu_irq_remap)
		amd_iommu_irq_remap = check_ioapic_information();

	if (amd_iommu_irq_remap) {
		struct amd_iommu_pci_seg *pci_seg;
		/*
		 * Interrupt remapping enabled, create kmem_cache for the
		 * remapping tables.
		 */
		ret = -ENOMEM;
		if (!AMD_IOMMU_GUEST_IR_GA(amd_iommu_guest_ir))
			remap_cache_sz = MAX_IRQS_PER_TABLE * sizeof(u32);
		else
			remap_cache_sz = MAX_IRQS_PER_TABLE * (sizeof(u64) * 2);
		amd_iommu_irq_cache = kmem_cache_create("irq_remap_cache",
							remap_cache_sz,
							DTE_INTTAB_ALIGNMENT,
							0, NULL);
		if (!amd_iommu_irq_cache)
			goto out;

		for_each_pci_segment(pci_seg) {
			if (alloc_irq_lookup_table(pci_seg))
				goto out;
		}
	}

	ret = init_memory_definitions(ivrs_base);
	if (ret)
		goto out;

	/* init the device table */
	init_device_table();

out:
	/* Don't leak any ACPI memory */
	acpi_put_table(ivrs_base);

	return ret;
}

static int amd_iommu_enable_interrupts(void)
{
	struct amd_iommu *iommu;
	int ret = 0;

	for_each_iommu(iommu) {
		ret = iommu_init_irq(iommu);
		if (ret)
			goto out;
	}

	/*
	 * Interrupt handler is ready to process interrupts. Enable
	 * PPR and GA log interrupt for all IOMMUs.
	 */
	enable_iommus_vapic();
	enable_iommus_ppr();

out:
	return ret;
}

static bool __init detect_ivrs(void)
{
	struct acpi_table_header *ivrs_base;
	acpi_status status;
	int i;

	status = acpi_get_table("IVRS", 0, &ivrs_base);
	if (status == AE_NOT_FOUND)
		return false;
	else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);
		pr_err("IVRS table error: %s\n", err);
		return false;
	}

	acpi_put_table(ivrs_base);

	if (amd_iommu_force_enable)
		goto out;

	/* Don't use IOMMU if there is Stoney Ridge graphics */
	for (i = 0; i < 32; i++) {
		u32 pci_id;

		pci_id = read_pci_config(0, i, 0, 0);
		if ((pci_id & 0xffff) == 0x1002 && (pci_id >> 16) == 0x98e4) {
			pr_info("Disable IOMMU on Stoney Ridge\n");
			return false;
		}
	}

out:
	/* Make sure ACS will be enabled during PCI probe */
	pci_request_acs();

	return true;
}

static void iommu_snp_enable(void)
{
#ifdef CONFIG_KVM_AMD_SEV
	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return;
	/*
	 * The SNP support requires that IOMMU must be enabled, and is
	 * configured with V1 page table (DTE[Mode] = 0 is not supported).
	 */
	if (no_iommu || iommu_default_passthrough()) {
		pr_warn("SNP: IOMMU disabled or configured in passthrough mode, SNP cannot be supported.\n");
		goto disable_snp;
	}

	if (amd_iommu_pgtable != AMD_IOMMU_V1) {
		pr_warn("SNP: IOMMU is configured with V2 page table mode, SNP cannot be supported.\n");
		goto disable_snp;
	}

	amd_iommu_snp_en = check_feature(FEATURE_SNP);
	if (!amd_iommu_snp_en) {
		pr_warn("SNP: IOMMU SNP feature not enabled, SNP cannot be supported.\n");
		goto disable_snp;
	}

	pr_info("IOMMU SNP support enabled.\n");
	return;

disable_snp:
	cc_platform_clear(CC_ATTR_HOST_SEV_SNP);
#endif
}

/****************************************************************************
 *
 * AMD IOMMU Initialization State Machine
 *
 ****************************************************************************/

static int __init state_next(void)
{
	int ret = 0;

	switch (init_state) {
	case IOMMU_START_STATE:
		if (!detect_ivrs()) {
			init_state	= IOMMU_NOT_FOUND;
			ret		= -ENODEV;
		} else {
			init_state	= IOMMU_IVRS_DETECTED;
		}
		break;
	case IOMMU_IVRS_DETECTED:
		if (amd_iommu_disabled) {
			init_state = IOMMU_CMDLINE_DISABLED;
			ret = -EINVAL;
		} else {
			ret = early_amd_iommu_init();
			init_state = ret ? IOMMU_INIT_ERROR : IOMMU_ACPI_FINISHED;
		}
		break;
	case IOMMU_ACPI_FINISHED:
		early_enable_iommus();
		x86_platform.iommu_shutdown = disable_iommus;
		init_state = IOMMU_ENABLED;
		break;
	case IOMMU_ENABLED:
		register_syscore_ops(&amd_iommu_syscore_ops);
		iommu_snp_enable();
		ret = amd_iommu_init_pci();
		init_state = ret ? IOMMU_INIT_ERROR : IOMMU_PCI_INIT;
		break;
	case IOMMU_PCI_INIT:
		ret = amd_iommu_enable_interrupts();
		init_state = ret ? IOMMU_INIT_ERROR : IOMMU_INTERRUPTS_EN;
		break;
	case IOMMU_INTERRUPTS_EN:
		init_state = IOMMU_INITIALIZED;
		break;
	case IOMMU_INITIALIZED:
		/* Nothing to do */
		break;
	case IOMMU_NOT_FOUND:
	case IOMMU_INIT_ERROR:
	case IOMMU_CMDLINE_DISABLED:
		/* Error states => do nothing */
		ret = -EINVAL;
		break;
	default:
		/* Unknown state */
		BUG();
	}

	if (ret) {
		free_dma_resources();
		if (!irq_remapping_enabled) {
			disable_iommus();
			free_iommu_resources();
		} else {
			struct amd_iommu *iommu;
			struct amd_iommu_pci_seg *pci_seg;

			for_each_pci_segment(pci_seg)
				uninit_device_table_dma(pci_seg);

			for_each_iommu(iommu)
				amd_iommu_flush_all_caches(iommu);
		}
	}
	return ret;
}

static int __init iommu_go_to_state(enum iommu_init_state state)
{
	int ret = -EINVAL;

	while (init_state != state) {
		if (init_state == IOMMU_NOT_FOUND         ||
		    init_state == IOMMU_INIT_ERROR        ||
		    init_state == IOMMU_CMDLINE_DISABLED)
			break;
		ret = state_next();
	}

	return ret;
}

#ifdef CONFIG_IRQ_REMAP
int __init amd_iommu_prepare(void)
{
	int ret;

	amd_iommu_irq_remap = true;

	ret = iommu_go_to_state(IOMMU_ACPI_FINISHED);
	if (ret) {
		amd_iommu_irq_remap = false;
		return ret;
	}

	return amd_iommu_irq_remap ? 0 : -ENODEV;
}

int __init amd_iommu_enable(void)
{
	int ret;

	ret = iommu_go_to_state(IOMMU_ENABLED);
	if (ret)
		return ret;

	irq_remapping_enabled = 1;
	return amd_iommu_xt_mode;
}

void amd_iommu_disable(void)
{
	amd_iommu_suspend();
}

int amd_iommu_reenable(int mode)
{
	amd_iommu_resume();

	return 0;
}

int amd_iommu_enable_faulting(unsigned int cpu)
{
	/* We enable MSI later when PCI is initialized */
	return 0;
}
#endif

/*
 * This is the core init function for AMD IOMMU hardware in the system.
 * This function is called from the generic x86 DMA layer initialization
 * code.
 */
static int __init amd_iommu_init(void)
{
	struct amd_iommu *iommu;
	int ret;

	ret = iommu_go_to_state(IOMMU_INITIALIZED);
#ifdef CONFIG_GART_IOMMU
	if (ret && list_empty(&amd_iommu_list)) {
		/*
		 * We failed to initialize the AMD IOMMU - try fallback
		 * to GART if possible.
		 */
		gart_iommu_init();
	}
#endif

	for_each_iommu(iommu)
		amd_iommu_debugfs_setup(iommu);

	return ret;
}

static bool amd_iommu_sme_check(void)
{
	if (!cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT) ||
	    (boot_cpu_data.x86 != 0x17))
		return true;

	/* For Fam17h, a specific level of support is required */
	if (boot_cpu_data.microcode >= 0x08001205)
		return true;

	if ((boot_cpu_data.microcode >= 0x08001126) &&
	    (boot_cpu_data.microcode <= 0x080011ff))
		return true;

	pr_notice("IOMMU not currently supported when SME is active\n");

	return false;
}

/****************************************************************************
 *
 * Early detect code. This code runs at IOMMU detection time in the DMA
 * layer. It just looks if there is an IVRS ACPI table to detect AMD
 * IOMMUs
 *
 ****************************************************************************/
int __init amd_iommu_detect(void)
{
	int ret;

	if (no_iommu || (iommu_detected && !gart_iommu_aperture))
		return -ENODEV;

	if (!amd_iommu_sme_check())
		return -ENODEV;

	ret = iommu_go_to_state(IOMMU_IVRS_DETECTED);
	if (ret)
		return ret;

	amd_iommu_detected = true;
	iommu_detected = 1;
	x86_init.iommu.iommu_init = amd_iommu_init;

	return 1;
}

/****************************************************************************
 *
 * Parsing functions for the AMD IOMMU specific kernel command line
 * options.
 *
 ****************************************************************************/

static int __init parse_amd_iommu_dump(char *str)
{
	amd_iommu_dump = true;

	return 1;
}

static int __init parse_amd_iommu_intr(char *str)
{
	for (; *str; ++str) {
		if (strncmp(str, "legacy", 6) == 0) {
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY_GA;
			break;
		}
		if (strncmp(str, "vapic", 5) == 0) {
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_VAPIC;
			break;
		}
	}
	return 1;
}

static int __init parse_amd_iommu_options(char *str)
{
	if (!str)
		return -EINVAL;

	while (*str) {
		if (strncmp(str, "fullflush", 9) == 0) {
			pr_warn("amd_iommu=fullflush deprecated; use iommu.strict=1 instead\n");
			iommu_set_dma_strict();
		} else if (strncmp(str, "force_enable", 12) == 0) {
			amd_iommu_force_enable = true;
		} else if (strncmp(str, "off", 3) == 0) {
			amd_iommu_disabled = true;
		} else if (strncmp(str, "force_isolation", 15) == 0) {
			amd_iommu_force_isolation = true;
		} else if (strncmp(str, "pgtbl_v1", 8) == 0) {
			amd_iommu_pgtable = AMD_IOMMU_V1;
		} else if (strncmp(str, "pgtbl_v2", 8) == 0) {
			amd_iommu_pgtable = AMD_IOMMU_V2;
		} else if (strncmp(str, "irtcachedis", 11) == 0) {
			amd_iommu_irtcachedis = true;
		} else if (strncmp(str, "nohugepages", 11) == 0) {
			pr_info("Restricting V1 page-sizes to 4KiB");
			amd_iommu_pgsize_bitmap = AMD_IOMMU_PGSIZES_4K;
		} else if (strncmp(str, "v2_pgsizes_only", 15) == 0) {
			pr_info("Restricting V1 page-sizes to 4KiB/2MiB/1GiB");
			amd_iommu_pgsize_bitmap = AMD_IOMMU_PGSIZES_V2;
		} else {
			pr_notice("Unknown option - '%s'\n", str);
		}

		str += strcspn(str, ",");
		while (*str == ',')
			str++;
	}

	return 1;
}

static int __init parse_ivrs_ioapic(char *str)
{
	u32 seg = 0, bus, dev, fn;
	int id, i;
	u32 devid;

	if (sscanf(str, "=%d@%x:%x.%x", &id, &bus, &dev, &fn) == 4 ||
	    sscanf(str, "=%d@%x:%x:%x.%x", &id, &seg, &bus, &dev, &fn) == 5)
		goto found;

	if (sscanf(str, "[%d]=%x:%x.%x", &id, &bus, &dev, &fn) == 4 ||
	    sscanf(str, "[%d]=%x:%x:%x.%x", &id, &seg, &bus, &dev, &fn) == 5) {
		pr_warn("ivrs_ioapic%s option format deprecated; use ivrs_ioapic=%d@%04x:%02x:%02x.%d instead\n",
			str, id, seg, bus, dev, fn);
		goto found;
	}

	pr_err("Invalid command line: ivrs_ioapic%s\n", str);
	return 1;

found:
	if (early_ioapic_map_size == EARLY_MAP_SIZE) {
		pr_err("Early IOAPIC map overflow - ignoring ivrs_ioapic%s\n",
			str);
		return 1;
	}

	devid = IVRS_GET_SBDF_ID(seg, bus, dev, fn);

	cmdline_maps			= true;
	i				= early_ioapic_map_size++;
	early_ioapic_map[i].id		= id;
	early_ioapic_map[i].devid	= devid;
	early_ioapic_map[i].cmd_line	= true;

	return 1;
}

static int __init parse_ivrs_hpet(char *str)
{
	u32 seg = 0, bus, dev, fn;
	int id, i;
	u32 devid;

	if (sscanf(str, "=%d@%x:%x.%x", &id, &bus, &dev, &fn) == 4 ||
	    sscanf(str, "=%d@%x:%x:%x.%x", &id, &seg, &bus, &dev, &fn) == 5)
		goto found;

	if (sscanf(str, "[%d]=%x:%x.%x", &id, &bus, &dev, &fn) == 4 ||
	    sscanf(str, "[%d]=%x:%x:%x.%x", &id, &seg, &bus, &dev, &fn) == 5) {
		pr_warn("ivrs_hpet%s option format deprecated; use ivrs_hpet=%d@%04x:%02x:%02x.%d instead\n",
			str, id, seg, bus, dev, fn);
		goto found;
	}

	pr_err("Invalid command line: ivrs_hpet%s\n", str);
	return 1;

found:
	if (early_hpet_map_size == EARLY_MAP_SIZE) {
		pr_err("Early HPET map overflow - ignoring ivrs_hpet%s\n",
			str);
		return 1;
	}

	devid = IVRS_GET_SBDF_ID(seg, bus, dev, fn);

	cmdline_maps			= true;
	i				= early_hpet_map_size++;
	early_hpet_map[i].id		= id;
	early_hpet_map[i].devid		= devid;
	early_hpet_map[i].cmd_line	= true;

	return 1;
}

#define ACPIID_LEN (ACPIHID_UID_LEN + ACPIHID_HID_LEN)

static int __init parse_ivrs_acpihid(char *str)
{
	u32 seg = 0, bus, dev, fn;
	char *hid, *uid, *p, *addr;
	char acpiid[ACPIID_LEN] = {0};
	int i;

	addr = strchr(str, '@');
	if (!addr) {
		addr = strchr(str, '=');
		if (!addr)
			goto not_found;

		++addr;

		if (strlen(addr) > ACPIID_LEN)
			goto not_found;

		if (sscanf(str, "[%x:%x.%x]=%s", &bus, &dev, &fn, acpiid) == 4 ||
		    sscanf(str, "[%x:%x:%x.%x]=%s", &seg, &bus, &dev, &fn, acpiid) == 5) {
			pr_warn("ivrs_acpihid%s option format deprecated; use ivrs_acpihid=%s@%04x:%02x:%02x.%d instead\n",
				str, acpiid, seg, bus, dev, fn);
			goto found;
		}
		goto not_found;
	}

	/* We have the '@', make it the terminator to get just the acpiid */
	*addr++ = 0;

	if (strlen(str) > ACPIID_LEN + 1)
		goto not_found;

	if (sscanf(str, "=%s", acpiid) != 1)
		goto not_found;

	if (sscanf(addr, "%x:%x.%x", &bus, &dev, &fn) == 3 ||
	    sscanf(addr, "%x:%x:%x.%x", &seg, &bus, &dev, &fn) == 4)
		goto found;

not_found:
	pr_err("Invalid command line: ivrs_acpihid%s\n", str);
	return 1;

found:
	p = acpiid;
	hid = strsep(&p, ":");
	uid = p;

	if (!hid || !(*hid) || !uid) {
		pr_err("Invalid command line: hid or uid\n");
		return 1;
	}

	/*
	 * Ignore leading zeroes after ':', so e.g., AMDI0095:00
	 * will match AMDI0095:0 in the second strcmp in acpi_dev_hid_uid_match
	 */
	while (*uid == '0' && *(uid + 1))
		uid++;

	i = early_acpihid_map_size++;
	memcpy(early_acpihid_map[i].hid, hid, strlen(hid));
	memcpy(early_acpihid_map[i].uid, uid, strlen(uid));
	early_acpihid_map[i].devid = IVRS_GET_SBDF_ID(seg, bus, dev, fn);
	early_acpihid_map[i].cmd_line	= true;

	return 1;
}

__setup("amd_iommu_dump",	parse_amd_iommu_dump);
__setup("amd_iommu=",		parse_amd_iommu_options);
__setup("amd_iommu_intr=",	parse_amd_iommu_intr);
__setup("ivrs_ioapic",		parse_ivrs_ioapic);
__setup("ivrs_hpet",		parse_ivrs_hpet);
__setup("ivrs_acpihid",		parse_ivrs_acpihid);

bool amd_iommu_pasid_supported(void)
{
	/* CPU page table size should match IOMMU guest page table size */
	if (cpu_feature_enabled(X86_FEATURE_LA57) &&
	    amd_iommu_gpt_level != PAGE_MODE_5_LEVEL)
		return false;

	/*
	 * Since DTE[Mode]=0 is prohibited on SNP-enabled system
	 * (i.e. EFR[SNPSup]=1), IOMMUv2 page table cannot be used without
	 * setting up IOMMUv1 page table.
	 */
	return amd_iommu_gt_ppr_supported() && !amd_iommu_snp_en;
}

struct amd_iommu *get_amd_iommu(unsigned int idx)
{
	unsigned int i = 0;
	struct amd_iommu *iommu;

	for_each_iommu(iommu)
		if (i++ == idx)
			return iommu;
	return NULL;
}

/****************************************************************************
 *
 * IOMMU EFR Performance Counter support functionality. This code allows
 * access to the IOMMU PC functionality.
 *
 ****************************************************************************/

u8 amd_iommu_pc_get_max_banks(unsigned int idx)
{
	struct amd_iommu *iommu = get_amd_iommu(idx);

	if (iommu)
		return iommu->max_banks;

	return 0;
}

bool amd_iommu_pc_supported(void)
{
	return amd_iommu_pc_present;
}

u8 amd_iommu_pc_get_max_counters(unsigned int idx)
{
	struct amd_iommu *iommu = get_amd_iommu(idx);

	if (iommu)
		return iommu->max_counters;

	return 0;
}

static int iommu_pc_get_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr,
				u8 fxn, u64 *value, bool is_write)
{
	u32 offset;
	u32 max_offset_lim;

	/* Make sure the IOMMU PC resource is available */
	if (!amd_iommu_pc_present)
		return -ENODEV;

	/* Check for valid iommu and pc register indexing */
	if (WARN_ON(!iommu || (fxn > 0x28) || (fxn & 7)))
		return -ENODEV;

	offset = (u32)(((0x40 | bank) << 12) | (cntr << 8) | fxn);

	/* Limit the offset to the hw defined mmio region aperture */
	max_offset_lim = (u32)(((0x40 | iommu->max_banks) << 12) |
				(iommu->max_counters << 8) | 0x28);
	if ((offset < MMIO_CNTR_REG_OFFSET) ||
	    (offset > max_offset_lim))
		return -EINVAL;

	if (is_write) {
		u64 val = *value & GENMASK_ULL(47, 0);

		writel((u32)val, iommu->mmio_base + offset);
		writel((val >> 32), iommu->mmio_base + offset + 4);
	} else {
		*value = readl(iommu->mmio_base + offset + 4);
		*value <<= 32;
		*value |= readl(iommu->mmio_base + offset);
		*value &= GENMASK_ULL(47, 0);
	}

	return 0;
}

int amd_iommu_pc_get_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn, u64 *value)
{
	if (!iommu)
		return -EINVAL;

	return iommu_pc_get_set_reg(iommu, bank, cntr, fxn, value, false);
}

int amd_iommu_pc_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn, u64 *value)
{
	if (!iommu)
		return -EINVAL;

	return iommu_pc_get_set_reg(iommu, bank, cntr, fxn, value, true);
}

#ifdef CONFIG_KVM_AMD_SEV
static int iommu_page_make_shared(void *page)
{
	unsigned long paddr, pfn;

	paddr = iommu_virt_to_phys(page);
	/* Cbit maybe set in the paddr */
	pfn = __sme_clr(paddr) >> PAGE_SHIFT;

	if (!(pfn % PTRS_PER_PMD)) {
		int ret, level;
		bool assigned;

		ret = snp_lookup_rmpentry(pfn, &assigned, &level);
		if (ret) {
			pr_warn("IOMMU PFN %lx RMP lookup failed, ret %d\n", pfn, ret);
			return ret;
		}

		if (!assigned) {
			pr_warn("IOMMU PFN %lx not assigned in RMP table\n", pfn);
			return -EINVAL;
		}

		if (level > PG_LEVEL_4K) {
			ret = psmash(pfn);
			if (!ret)
				goto done;

			pr_warn("PSMASH failed for IOMMU PFN %lx huge RMP entry, ret: %d, level: %d\n",
				pfn, ret, level);
			return ret;
		}
	}

done:
	return rmp_make_shared(pfn, PG_LEVEL_4K);
}

static int iommu_make_shared(void *va, size_t size)
{
	void *page;
	int ret;

	if (!va)
		return 0;

	for (page = va; page < (va + size); page += PAGE_SIZE) {
		ret = iommu_page_make_shared(page);
		if (ret)
			return ret;
	}

	return 0;
}

int amd_iommu_snp_disable(void)
{
	struct amd_iommu *iommu;
	int ret;

	if (!amd_iommu_snp_en)
		return 0;

	for_each_iommu(iommu) {
		ret = iommu_make_shared(iommu->evt_buf, EVT_BUFFER_SIZE);
		if (ret)
			return ret;

		ret = iommu_make_shared(iommu->ppr_log, PPR_LOG_SIZE);
		if (ret)
			return ret;

		ret = iommu_make_shared((void *)iommu->cmd_sem, PAGE_SIZE);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amd_iommu_snp_disable);
#endif
