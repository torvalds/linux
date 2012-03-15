/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *         Leo Duran <leo.duran@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/amd-iommu.h>
#include <linux/export.h>
#include <asm/pci-direct.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/x86_init.h>
#include <asm/iommu_table.h>

#include "amd_iommu_proto.h"
#include "amd_iommu_types.h"

/*
 * definitions for the ACPI scanning code
 */
#define IVRS_HEADER_LENGTH 48

#define ACPI_IVHD_TYPE                  0x10
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

#define IVHD_FLAG_HT_TUN_EN_MASK        0x01
#define IVHD_FLAG_PASSPW_EN_MASK        0x02
#define IVHD_FLAG_RESPASSPW_EN_MASK     0x04
#define IVHD_FLAG_ISOC_EN_MASK          0x08

#define IVMD_FLAG_EXCL_RANGE            0x08
#define IVMD_FLAG_UNITY_MAP             0x01

#define ACPI_DEVFLAG_INITPASS           0x01
#define ACPI_DEVFLAG_EXTINT             0x02
#define ACPI_DEVFLAG_NMI                0x04
#define ACPI_DEVFLAG_SYSMGT1            0x10
#define ACPI_DEVFLAG_SYSMGT2            0x20
#define ACPI_DEVFLAG_LINT0              0x40
#define ACPI_DEVFLAG_LINT1              0x80
#define ACPI_DEVFLAG_ATSDIS             0x10000000

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
	u32 reserved;
} __attribute__((packed));

/*
 * A device entry describing which devices a specific IOMMU translates and
 * which requestor ids they use.
 */
struct ivhd_entry {
	u8 type;
	u16 devid;
	u8 flags;
	u32 ext;
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
	u64 resv;
	u64 range_start;
	u64 range_length;
} __attribute__((packed));

bool amd_iommu_dump;

static int __initdata amd_iommu_detected;
static bool __initdata amd_iommu_disabled;

u16 amd_iommu_last_bdf;			/* largest PCI device id we have
					   to handle */
LIST_HEAD(amd_iommu_unity_map);		/* a list of required unity mappings
					   we find in ACPI */
bool amd_iommu_unmap_flush;		/* if true, flush on every unmap */

LIST_HEAD(amd_iommu_list);		/* list of all AMD IOMMUs in the
					   system */

/* Array to assign indices to IOMMUs*/
struct amd_iommu *amd_iommus[MAX_IOMMUS];
int amd_iommus_present;

/* IOMMUs have a non-present cache? */
bool amd_iommu_np_cache __read_mostly;
bool amd_iommu_iotlb_sup __read_mostly = true;

u32 amd_iommu_max_pasids __read_mostly = ~0;

bool amd_iommu_v2_present __read_mostly;

bool amd_iommu_force_isolation __read_mostly;

/*
 * The ACPI table parsing functions set this variable on an error
 */
static int __initdata amd_iommu_init_err;

/*
 * List of protection domains - used during resume
 */
LIST_HEAD(amd_iommu_pd_list);
spinlock_t amd_iommu_pd_lock;

/*
 * Pointer to the device table which is shared by all AMD IOMMUs
 * it is indexed by the PCI device id or the HT unit id and contains
 * information about the domain the device belongs to as well as the
 * page table root pointer.
 */
struct dev_table_entry *amd_iommu_dev_table;

/*
 * The alias table is a driver specific data structure which contains the
 * mappings of the PCI device ids to the actual requestor ids on the IOMMU.
 * More than one device can share the same requestor id.
 */
u16 *amd_iommu_alias_table;

/*
 * The rlookup table is used to find the IOMMU which is responsible
 * for a specific device. It is also indexed by the PCI device id.
 */
struct amd_iommu **amd_iommu_rlookup_table;

/*
 * AMD IOMMU allows up to 2^16 differend protection domains. This is a bitmap
 * to know which ones are already in use.
 */
unsigned long *amd_iommu_pd_alloc_bitmap;

static u32 dev_table_size;	/* size of the device table */
static u32 alias_table_size;	/* size of the alias table */
static u32 rlookup_table_size;	/* size if the rlookup table */

/*
 * This function flushes all internal caches of
 * the IOMMU used by this driver.
 */
extern void iommu_flush_all_caches(struct amd_iommu *iommu);

static inline void update_last_devid(u16 devid)
{
	if (devid > amd_iommu_last_bdf)
		amd_iommu_last_bdf = devid;
}

static inline unsigned long tbl_size(int entry_size)
{
	unsigned shift = PAGE_SHIFT +
			 get_order(((int)amd_iommu_last_bdf + 1) * entry_size);

	return 1UL << shift;
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
	u64 limit = (start + iommu->exclusion_length) & PAGE_MASK;
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

/* Programs the physical address of the device table into the IOMMU hardware */
static void iommu_set_device_table(struct amd_iommu *iommu)
{
	u64 entry;

	BUG_ON(iommu->mmio_base == NULL);

	entry = virt_to_phys(amd_iommu_dev_table);
	entry |= (dev_table_size >> 12) - 1;
	memcpy_toio(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET,
			&entry, sizeof(entry));
}

/* Generic functions to enable/disable certain features of the IOMMU. */
static void iommu_feature_enable(struct amd_iommu *iommu, u8 bit)
{
	u32 ctrl;

	ctrl = readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl |= (1 << bit);
	writel(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

static void iommu_feature_disable(struct amd_iommu *iommu, u8 bit)
{
	u32 ctrl;

	ctrl = readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl &= ~(1 << bit);
	writel(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

static void iommu_set_inv_tlb_timeout(struct amd_iommu *iommu, int timeout)
{
	u32 ctrl;

	ctrl = readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl &= ~CTRL_INV_TO_MASK;
	ctrl |= (timeout << CONTROL_INV_TIMEOUT) & CTRL_INV_TO_MASK;
	writel(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

/* Function to enable the hardware */
static void iommu_enable(struct amd_iommu *iommu)
{
	static const char * const feat_str[] = {
		"PreF", "PPR", "X2APIC", "NX", "GT", "[5]",
		"IA", "GA", "HE", "PC", NULL
	};
	int i;

	printk(KERN_INFO "AMD-Vi: Enabling IOMMU at %s cap 0x%hx",
	       dev_name(&iommu->dev->dev), iommu->cap_ptr);

	if (iommu->cap & (1 << IOMMU_CAP_EFR)) {
		printk(KERN_CONT " extended features: ");
		for (i = 0; feat_str[i]; ++i)
			if (iommu_feature(iommu, (1ULL << i)))
				printk(KERN_CONT " %s", feat_str[i]);
	}
	printk(KERN_CONT "\n");

	iommu_feature_enable(iommu, CONTROL_IOMMU_EN);
}

static void iommu_disable(struct amd_iommu *iommu)
{
	/* Disable command buffer */
	iommu_feature_disable(iommu, CONTROL_CMDBUF_EN);

	/* Disable event logging and event interrupts */
	iommu_feature_disable(iommu, CONTROL_EVT_INT_EN);
	iommu_feature_disable(iommu, CONTROL_EVT_LOG_EN);

	/* Disable IOMMU hardware itself */
	iommu_feature_disable(iommu, CONTROL_IOMMU_EN);
}

/*
 * mapping and unmapping functions for the IOMMU MMIO space. Each AMD IOMMU in
 * the system has one.
 */
static u8 * __init iommu_map_mmio_space(u64 address)
{
	u8 *ret;

	if (!request_mem_region(address, MMIO_REGION_LENGTH, "amd_iommu")) {
		pr_err("AMD-Vi: Can not reserve memory region %llx for mmio\n",
			address);
		pr_err("AMD-Vi: This is a BIOS bug. Please contact your hardware vendor\n");
		return NULL;
	}

	ret = ioremap_nocache(address, MMIO_REGION_LENGTH);
	if (ret != NULL)
		return ret;

	release_mem_region(address, MMIO_REGION_LENGTH);

	return NULL;
}

static void __init iommu_unmap_mmio_space(struct amd_iommu *iommu)
{
	if (iommu->mmio_base)
		iounmap(iommu->mmio_base);
	release_mem_region(iommu->mmio_phys, MMIO_REGION_LENGTH);
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
	return 0x04 << (*ivhd >> 6);
}

/*
 * This function reads the last device id the IOMMU has to handle from the PCI
 * capability header for this IOMMU
 */
static int __init find_last_devid_on_pci(int bus, int dev, int fn, int cap_ptr)
{
	u32 cap;

	cap = read_pci_config(bus, dev, fn, cap_ptr+MMIO_RANGE_OFFSET);
	update_last_devid(calc_devid(MMIO_GET_BUS(cap), MMIO_GET_LD(cap)));

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

	p += sizeof(*h);
	end += h->length;

	find_last_devid_on_pci(PCI_BUS(h->devid),
			PCI_SLOT(h->devid),
			PCI_FUNC(h->devid),
			h->cap_ptr);

	while (p < end) {
		dev = (struct ivhd_entry *)p;
		switch (dev->type) {
		case IVHD_DEV_SELECT:
		case IVHD_DEV_RANGE_END:
		case IVHD_DEV_ALIAS:
		case IVHD_DEV_EXT_SELECT:
			/* all the above subfield types refer to device ids */
			update_last_devid(dev->devid);
			break;
		default:
			break;
		}
		p += ivhd_entry_length(p);
	}

	WARN_ON(p != end);

	return 0;
}

/*
 * Iterate over all IVHD entries in the ACPI table and find the highest device
 * id which we need to handle. This is the first of three functions which parse
 * the ACPI table. So we check the checksum here.
 */
static int __init find_last_devid_acpi(struct acpi_table_header *table)
{
	int i;
	u8 checksum = 0, *p = (u8 *)table, *end = (u8 *)table;
	struct ivhd_header *h;

	/*
	 * Validate checksum here so we don't need to do it when
	 * we actually parse the table
	 */
	for (i = 0; i < table->length; ++i)
		checksum += p[i];
	if (checksum != 0) {
		/* ACPI table corrupt */
		amd_iommu_init_err = -ENODEV;
		return 0;
	}

	p += IVRS_HEADER_LENGTH;

	end += table->length;
	while (p < end) {
		h = (struct ivhd_header *)p;
		switch (h->type) {
		case ACPI_IVHD_TYPE:
			find_last_devid_from_ivhd(h);
			break;
		default:
			break;
		}
		p += h->length;
	}
	WARN_ON(p != end);

	return 0;
}

/****************************************************************************
 *
 * The following functions belong the the code path which parses the ACPI table
 * the second time. In this ACPI parsing iteration we allocate IOMMU specific
 * data structures, initialize the device/alias/rlookup table and also
 * basically initialize the hardware.
 *
 ****************************************************************************/

/*
 * Allocates the command buffer. This buffer is per AMD IOMMU. We can
 * write commands to that buffer later and the IOMMU will execute them
 * asynchronously
 */
static u8 * __init alloc_command_buffer(struct amd_iommu *iommu)
{
	u8 *cmd_buf = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
			get_order(CMD_BUFFER_SIZE));

	if (cmd_buf == NULL)
		return NULL;

	iommu->cmd_buf_size = CMD_BUFFER_SIZE | CMD_BUFFER_UNINITIALIZED;

	return cmd_buf;
}

/*
 * This function resets the command buffer if the IOMMU stopped fetching
 * commands from it.
 */
void amd_iommu_reset_cmd_buffer(struct amd_iommu *iommu)
{
	iommu_feature_disable(iommu, CONTROL_CMDBUF_EN);

	writel(0x00, iommu->mmio_base + MMIO_CMD_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);

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

	entry = (u64)virt_to_phys(iommu->cmd_buf);
	entry |= MMIO_CMD_SIZE_512;

	memcpy_toio(iommu->mmio_base + MMIO_CMD_BUF_OFFSET,
		    &entry, sizeof(entry));

	amd_iommu_reset_cmd_buffer(iommu);
	iommu->cmd_buf_size &= ~(CMD_BUFFER_UNINITIALIZED);
}

static void __init free_command_buffer(struct amd_iommu *iommu)
{
	free_pages((unsigned long)iommu->cmd_buf,
		   get_order(iommu->cmd_buf_size & ~(CMD_BUFFER_UNINITIALIZED)));
}

/* allocates the memory where the IOMMU will log its events to */
static u8 * __init alloc_event_buffer(struct amd_iommu *iommu)
{
	iommu->evt_buf = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						get_order(EVT_BUFFER_SIZE));

	if (iommu->evt_buf == NULL)
		return NULL;

	iommu->evt_buf_size = EVT_BUFFER_SIZE;

	return iommu->evt_buf;
}

static void iommu_enable_event_buffer(struct amd_iommu *iommu)
{
	u64 entry;

	BUG_ON(iommu->evt_buf == NULL);

	entry = (u64)virt_to_phys(iommu->evt_buf) | EVT_LEN_MASK;

	memcpy_toio(iommu->mmio_base + MMIO_EVT_BUF_OFFSET,
		    &entry, sizeof(entry));

	/* set head and tail to zero manually */
	writel(0x00, iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_EVT_TAIL_OFFSET);

	iommu_feature_enable(iommu, CONTROL_EVT_LOG_EN);
}

static void __init free_event_buffer(struct amd_iommu *iommu)
{
	free_pages((unsigned long)iommu->evt_buf, get_order(EVT_BUFFER_SIZE));
}

/* allocates the memory where the IOMMU will log its events to */
static u8 * __init alloc_ppr_log(struct amd_iommu *iommu)
{
	iommu->ppr_log = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						get_order(PPR_LOG_SIZE));

	if (iommu->ppr_log == NULL)
		return NULL;

	return iommu->ppr_log;
}

static void iommu_enable_ppr_log(struct amd_iommu *iommu)
{
	u64 entry;

	if (iommu->ppr_log == NULL)
		return;

	entry = (u64)virt_to_phys(iommu->ppr_log) | PPR_LOG_SIZE_512;

	memcpy_toio(iommu->mmio_base + MMIO_PPR_LOG_OFFSET,
		    &entry, sizeof(entry));

	/* set head and tail to zero manually */
	writel(0x00, iommu->mmio_base + MMIO_PPR_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_PPR_TAIL_OFFSET);

	iommu_feature_enable(iommu, CONTROL_PPFLOG_EN);
	iommu_feature_enable(iommu, CONTROL_PPR_EN);
}

static void __init free_ppr_log(struct amd_iommu *iommu)
{
	if (iommu->ppr_log == NULL)
		return;

	free_pages((unsigned long)iommu->ppr_log, get_order(PPR_LOG_SIZE));
}

static void iommu_enable_gt(struct amd_iommu *iommu)
{
	if (!iommu_feature(iommu, FEATURE_GT))
		return;

	iommu_feature_enable(iommu, CONTROL_GT_EN);
}

/* sets a specific bit in the device table entry. */
static void set_dev_entry_bit(u16 devid, u8 bit)
{
	int i = (bit >> 6) & 0x03;
	int _bit = bit & 0x3f;

	amd_iommu_dev_table[devid].data[i] |= (1UL << _bit);
}

static int get_dev_entry_bit(u16 devid, u8 bit)
{
	int i = (bit >> 6) & 0x03;
	int _bit = bit & 0x3f;

	return (amd_iommu_dev_table[devid].data[i] & (1UL << _bit)) >> _bit;
}


void amd_iommu_apply_erratum_63(u16 devid)
{
	int sysmgt;

	sysmgt = get_dev_entry_bit(devid, DEV_ENTRY_SYSMGT1) |
		 (get_dev_entry_bit(devid, DEV_ENTRY_SYSMGT2) << 1);

	if (sysmgt == 0x01)
		set_dev_entry_bit(devid, DEV_ENTRY_IW);
}

/* Writes the specific IOMMU for a device into the rlookup table */
static void __init set_iommu_for_device(struct amd_iommu *iommu, u16 devid)
{
	amd_iommu_rlookup_table[devid] = iommu;
}

/*
 * This function takes the device specific flags read from the ACPI
 * table and sets up the device table entry with that information
 */
static void __init set_dev_entry_from_acpi(struct amd_iommu *iommu,
					   u16 devid, u32 flags, u32 ext_flags)
{
	if (flags & ACPI_DEVFLAG_INITPASS)
		set_dev_entry_bit(devid, DEV_ENTRY_INIT_PASS);
	if (flags & ACPI_DEVFLAG_EXTINT)
		set_dev_entry_bit(devid, DEV_ENTRY_EINT_PASS);
	if (flags & ACPI_DEVFLAG_NMI)
		set_dev_entry_bit(devid, DEV_ENTRY_NMI_PASS);
	if (flags & ACPI_DEVFLAG_SYSMGT1)
		set_dev_entry_bit(devid, DEV_ENTRY_SYSMGT1);
	if (flags & ACPI_DEVFLAG_SYSMGT2)
		set_dev_entry_bit(devid, DEV_ENTRY_SYSMGT2);
	if (flags & ACPI_DEVFLAG_LINT0)
		set_dev_entry_bit(devid, DEV_ENTRY_LINT0_PASS);
	if (flags & ACPI_DEVFLAG_LINT1)
		set_dev_entry_bit(devid, DEV_ENTRY_LINT1_PASS);

	amd_iommu_apply_erratum_63(devid);

	set_iommu_for_device(iommu, devid);
}

/*
 * Reads the device exclusion range from ACPI and initialize IOMMU with
 * it
 */
static void __init set_device_exclusion_range(u16 devid, struct ivmd_header *m)
{
	struct amd_iommu *iommu = amd_iommu_rlookup_table[devid];

	if (!(m->flags & IVMD_FLAG_EXCL_RANGE))
		return;

	if (iommu) {
		/*
		 * We only can configure exclusion ranges per IOMMU, not
		 * per device. But we can enable the exclusion range per
		 * device. This is done here
		 */
		set_dev_entry_bit(m->devid, DEV_ENTRY_EX);
		iommu->exclusion_start = m->range_start;
		iommu->exclusion_length = m->range_length;
	}
}

/*
 * This function reads some important data from the IOMMU PCI space and
 * initializes the driver data structure with it. It reads the hardware
 * capabilities and the first/last device entries
 */
static void __init init_iommu_from_pci(struct amd_iommu *iommu)
{
	int cap_ptr = iommu->cap_ptr;
	u32 range, misc, low, high;
	int i, j;

	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_CAP_HDR_OFFSET,
			      &iommu->cap);
	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_RANGE_OFFSET,
			      &range);
	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_MISC_OFFSET,
			      &misc);

	iommu->first_device = calc_devid(MMIO_GET_BUS(range),
					 MMIO_GET_FD(range));
	iommu->last_device = calc_devid(MMIO_GET_BUS(range),
					MMIO_GET_LD(range));
	iommu->evt_msi_num = MMIO_MSI_NUM(misc);

	if (!(iommu->cap & (1 << IOMMU_CAP_IOTLB)))
		amd_iommu_iotlb_sup = false;

	/* read extended feature bits */
	low  = readl(iommu->mmio_base + MMIO_EXT_FEATURES);
	high = readl(iommu->mmio_base + MMIO_EXT_FEATURES + 4);

	iommu->features = ((u64)high << 32) | low;

	if (iommu_feature(iommu, FEATURE_GT)) {
		int glxval;
		u32 pasids;
		u64 shift;

		shift   = iommu->features & FEATURE_PASID_MASK;
		shift >>= FEATURE_PASID_SHIFT;
		pasids  = (1 << shift);

		amd_iommu_max_pasids = min(amd_iommu_max_pasids, pasids);

		glxval   = iommu->features & FEATURE_GLXVAL_MASK;
		glxval >>= FEATURE_GLXVAL_SHIFT;

		if (amd_iommu_max_glx_val == -1)
			amd_iommu_max_glx_val = glxval;
		else
			amd_iommu_max_glx_val = min(amd_iommu_max_glx_val, glxval);
	}

	if (iommu_feature(iommu, FEATURE_GT) &&
	    iommu_feature(iommu, FEATURE_PPR)) {
		iommu->is_iommu_v2   = true;
		amd_iommu_v2_present = true;
	}

	if (!is_rd890_iommu(iommu->dev))
		return;

	/*
	 * Some rd890 systems may not be fully reconfigured by the BIOS, so
	 * it's necessary for us to store this information so it can be
	 * reprogrammed on resume
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

/*
 * Takes a pointer to an AMD IOMMU entry in the ACPI table and
 * initializes the hardware and our data structures with it.
 */
static void __init init_iommu_from_acpi(struct amd_iommu *iommu,
					struct ivhd_header *h)
{
	u8 *p = (u8 *)h;
	u8 *end = p, flags = 0;
	u16 devid = 0, devid_start = 0, devid_to = 0;
	u32 dev_i, ext_flags = 0;
	bool alias = false;
	struct ivhd_entry *e;

	/*
	 * First save the recommended feature enable bits from ACPI
	 */
	iommu->acpi_flags = h->flags;

	/*
	 * Done. Now parse the device entries
	 */
	p += sizeof(struct ivhd_header);
	end += h->length;


	while (p < end) {
		e = (struct ivhd_entry *)p;
		switch (e->type) {
		case IVHD_DEV_ALL:

			DUMP_printk("  DEV_ALL\t\t\t first devid: %02x:%02x.%x"
				    " last device %02x:%02x.%x flags: %02x\n",
				    PCI_BUS(iommu->first_device),
				    PCI_SLOT(iommu->first_device),
				    PCI_FUNC(iommu->first_device),
				    PCI_BUS(iommu->last_device),
				    PCI_SLOT(iommu->last_device),
				    PCI_FUNC(iommu->last_device),
				    e->flags);

			for (dev_i = iommu->first_device;
					dev_i <= iommu->last_device; ++dev_i)
				set_dev_entry_from_acpi(iommu, dev_i,
							e->flags, 0);
			break;
		case IVHD_DEV_SELECT:

			DUMP_printk("  DEV_SELECT\t\t\t devid: %02x:%02x.%x "
				    "flags: %02x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags);

			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);
			break;
		case IVHD_DEV_SELECT_RANGE_START:

			DUMP_printk("  DEV_SELECT_RANGE_START\t "
				    "devid: %02x:%02x.%x flags: %02x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags);

			devid_start = e->devid;
			flags = e->flags;
			ext_flags = 0;
			alias = false;
			break;
		case IVHD_DEV_ALIAS:

			DUMP_printk("  DEV_ALIAS\t\t\t devid: %02x:%02x.%x "
				    "flags: %02x devid_to: %02x:%02x.%x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags,
				    PCI_BUS(e->ext >> 8),
				    PCI_SLOT(e->ext >> 8),
				    PCI_FUNC(e->ext >> 8));

			devid = e->devid;
			devid_to = e->ext >> 8;
			set_dev_entry_from_acpi(iommu, devid   , e->flags, 0);
			set_dev_entry_from_acpi(iommu, devid_to, e->flags, 0);
			amd_iommu_alias_table[devid] = devid_to;
			break;
		case IVHD_DEV_ALIAS_RANGE:

			DUMP_printk("  DEV_ALIAS_RANGE\t\t "
				    "devid: %02x:%02x.%x flags: %02x "
				    "devid_to: %02x:%02x.%x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags,
				    PCI_BUS(e->ext >> 8),
				    PCI_SLOT(e->ext >> 8),
				    PCI_FUNC(e->ext >> 8));

			devid_start = e->devid;
			flags = e->flags;
			devid_to = e->ext >> 8;
			ext_flags = 0;
			alias = true;
			break;
		case IVHD_DEV_EXT_SELECT:

			DUMP_printk("  DEV_EXT_SELECT\t\t devid: %02x:%02x.%x "
				    "flags: %02x ext: %08x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags, e->ext);

			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags,
						e->ext);
			break;
		case IVHD_DEV_EXT_SELECT_RANGE:

			DUMP_printk("  DEV_EXT_SELECT_RANGE\t devid: "
				    "%02x:%02x.%x flags: %02x ext: %08x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags, e->ext);

			devid_start = e->devid;
			flags = e->flags;
			ext_flags = e->ext;
			alias = false;
			break;
		case IVHD_DEV_RANGE_END:

			DUMP_printk("  DEV_RANGE_END\t\t devid: %02x:%02x.%x\n",
				    PCI_BUS(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid));

			devid = e->devid;
			for (dev_i = devid_start; dev_i <= devid; ++dev_i) {
				if (alias) {
					amd_iommu_alias_table[dev_i] = devid_to;
					set_dev_entry_from_acpi(iommu,
						devid_to, flags, ext_flags);
				}
				set_dev_entry_from_acpi(iommu, dev_i,
							flags, ext_flags);
			}
			break;
		default:
			break;
		}

		p += ivhd_entry_length(p);
	}
}

/* Initializes the device->iommu mapping for the driver */
static int __init init_iommu_devices(struct amd_iommu *iommu)
{
	u32 i;

	for (i = iommu->first_device; i <= iommu->last_device; ++i)
		set_iommu_for_device(iommu, i);

	return 0;
}

static void __init free_iommu_one(struct amd_iommu *iommu)
{
	free_command_buffer(iommu);
	free_event_buffer(iommu);
	free_ppr_log(iommu);
	iommu_unmap_mmio_space(iommu);
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
 * This function clues the initialization function for one IOMMU
 * together and also allocates the command buffer and programs the
 * hardware. It does NOT enable the IOMMU. This is done afterwards.
 */
static int __init init_iommu_one(struct amd_iommu *iommu, struct ivhd_header *h)
{
	spin_lock_init(&iommu->lock);

	/* Add IOMMU to internal data structures */
	list_add_tail(&iommu->list, &amd_iommu_list);
	iommu->index             = amd_iommus_present++;

	if (unlikely(iommu->index >= MAX_IOMMUS)) {
		WARN(1, "AMD-Vi: System has more IOMMUs than supported by this driver\n");
		return -ENOSYS;
	}

	/* Index is fine - add IOMMU to the array */
	amd_iommus[iommu->index] = iommu;

	/*
	 * Copy data from ACPI table entry to the iommu struct
	 */
	iommu->dev = pci_get_bus_and_slot(PCI_BUS(h->devid), h->devid & 0xff);
	if (!iommu->dev)
		return 1;

	iommu->cap_ptr = h->cap_ptr;
	iommu->pci_seg = h->pci_seg;
	iommu->mmio_phys = h->mmio_phys;
	iommu->mmio_base = iommu_map_mmio_space(h->mmio_phys);
	if (!iommu->mmio_base)
		return -ENOMEM;

	iommu->cmd_buf = alloc_command_buffer(iommu);
	if (!iommu->cmd_buf)
		return -ENOMEM;

	iommu->evt_buf = alloc_event_buffer(iommu);
	if (!iommu->evt_buf)
		return -ENOMEM;

	iommu->int_enabled = false;

	init_iommu_from_pci(iommu);
	init_iommu_from_acpi(iommu, h);
	init_iommu_devices(iommu);

	if (iommu_feature(iommu, FEATURE_PPR)) {
		iommu->ppr_log = alloc_ppr_log(iommu);
		if (!iommu->ppr_log)
			return -ENOMEM;
	}

	if (iommu->cap & (1UL << IOMMU_CAP_NPCACHE))
		amd_iommu_np_cache = true;

	return pci_enable_device(iommu->dev);
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

	while (p < end) {
		h = (struct ivhd_header *)p;
		switch (*p) {
		case ACPI_IVHD_TYPE:

			DUMP_printk("device: %02x:%02x.%01x cap: %04x "
				    "seg: %d flags: %01x info %04x\n",
				    PCI_BUS(h->devid), PCI_SLOT(h->devid),
				    PCI_FUNC(h->devid), h->cap_ptr,
				    h->pci_seg, h->flags, h->info);
			DUMP_printk("       mmio-addr: %016llx\n",
				    h->mmio_phys);

			iommu = kzalloc(sizeof(struct amd_iommu), GFP_KERNEL);
			if (iommu == NULL) {
				amd_iommu_init_err = -ENOMEM;
				return 0;
			}

			ret = init_iommu_one(iommu, h);
			if (ret) {
				amd_iommu_init_err = ret;
				return 0;
			}
			break;
		default:
			break;
		}
		p += h->length;

	}
	WARN_ON(p != end);

	return 0;
}

/****************************************************************************
 *
 * The following functions initialize the MSI interrupts for all IOMMUs
 * in the system. Its a bit challenging because there could be multiple
 * IOMMUs per PCI BDF but we can call pci_enable_msi(x) only once per
 * pci_dev.
 *
 ****************************************************************************/

static int iommu_setup_msi(struct amd_iommu *iommu)
{
	int r;

	if (pci_enable_msi(iommu->dev))
		return 1;

	r = request_threaded_irq(iommu->dev->irq,
				 amd_iommu_int_handler,
				 amd_iommu_int_thread,
				 0, "AMD-Vi",
				 iommu->dev);

	if (r) {
		pci_disable_msi(iommu->dev);
		return 1;
	}

	iommu->int_enabled = true;
	iommu_feature_enable(iommu, CONTROL_EVT_INT_EN);

	if (iommu->ppr_log != NULL)
		iommu_feature_enable(iommu, CONTROL_PPFINT_EN);

	return 0;
}

static int iommu_init_msi(struct amd_iommu *iommu)
{
	if (iommu->int_enabled)
		return 0;

	if (pci_find_capability(iommu->dev, PCI_CAP_ID_MSI))
		return iommu_setup_msi(iommu);

	return 1;
}

/****************************************************************************
 *
 * The next functions belong to the third pass of parsing the ACPI
 * table. In this last pass the memory mapping requirements are
 * gathered (like exclusion and unity mapping reanges).
 *
 ****************************************************************************/

static void __init free_unity_maps(void)
{
	struct unity_map_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &amd_iommu_unity_map, list) {
		list_del(&entry->list);
		kfree(entry);
	}
}

/* called when we find an exclusion range definition in ACPI */
static int __init init_exclusion_range(struct ivmd_header *m)
{
	int i;

	switch (m->type) {
	case ACPI_IVMD_TYPE:
		set_device_exclusion_range(m->devid, m);
		break;
	case ACPI_IVMD_TYPE_ALL:
		for (i = 0; i <= amd_iommu_last_bdf; ++i)
			set_device_exclusion_range(i, m);
		break;
	case ACPI_IVMD_TYPE_RANGE:
		for (i = m->devid; i <= m->aux; ++i)
			set_device_exclusion_range(i, m);
		break;
	default:
		break;
	}

	return 0;
}

/* called for unity map ACPI definition */
static int __init init_unity_map_range(struct ivmd_header *m)
{
	struct unity_map_entry *e = 0;
	char *s;

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
		e->devid_end = amd_iommu_last_bdf;
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

	DUMP_printk("%s devid_start: %02x:%02x.%x devid_end: %02x:%02x.%x"
		    " range_start: %016llx range_end: %016llx flags: %x\n", s,
		    PCI_BUS(e->devid_start), PCI_SLOT(e->devid_start),
		    PCI_FUNC(e->devid_start), PCI_BUS(e->devid_end),
		    PCI_SLOT(e->devid_end), PCI_FUNC(e->devid_end),
		    e->address_start, e->address_end, m->flags);

	list_add_tail(&e->list, &amd_iommu_unity_map);

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
		if (m->flags & IVMD_FLAG_EXCL_RANGE)
			init_exclusion_range(m);
		else if (m->flags & IVMD_FLAG_UNITY_MAP)
			init_unity_map_range(m);

		p += m->length;
	}

	return 0;
}

/*
 * Init the device table to not allow DMA access for devices and
 * suppress all page faults
 */
static void init_device_table(void)
{
	u32 devid;

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid) {
		set_dev_entry_bit(devid, DEV_ENTRY_VALID);
		set_dev_entry_bit(devid, DEV_ENTRY_TRANSLATION);
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
	struct pci_dev *pdev = NULL;

	/* RD890 BIOSes may not have completely reconfigured the iommu */
	if (!is_rd890_iommu(iommu->dev))
		return;

	/*
	 * First, we need to ensure that the iommu is enabled. This is
	 * controlled by a register in the northbridge
	 */
	pdev = pci_get_bus_and_slot(iommu->dev->bus->number, PCI_DEVFN(0, 0));

	if (!pdev)
		return;

	/* Select Northbridge indirect register 0x75 and enable writing */
	pci_write_config_dword(pdev, 0x60, 0x75 | (1 << 7));
	pci_read_config_dword(pdev, 0x64, &ioc_feature_control);

	/* Enable the iommu */
	if (!(ioc_feature_control & 0x1))
		pci_write_config_dword(pdev, 0x64, ioc_feature_control | 1);

	pci_dev_put(pdev);

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

/*
 * This function finally enables all IOMMUs found in the system after
 * they have been initialized
 */
static void enable_iommus(void)
{
	struct amd_iommu *iommu;

	for_each_iommu(iommu) {
		iommu_disable(iommu);
		iommu_init_flags(iommu);
		iommu_set_device_table(iommu);
		iommu_enable_command_buffer(iommu);
		iommu_enable_event_buffer(iommu);
		iommu_enable_ppr_log(iommu);
		iommu_enable_gt(iommu);
		iommu_set_exclusion_range(iommu);
		iommu_init_msi(iommu);
		iommu_enable(iommu);
		iommu_flush_all_caches(iommu);
	}
}

static void disable_iommus(void)
{
	struct amd_iommu *iommu;

	for_each_iommu(iommu)
		iommu_disable(iommu);
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
	enable_iommus();
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

/*
 * This is the core init function for AMD IOMMU hardware in the system.
 * This function is called from the generic x86 DMA layer initialization
 * code.
 *
 * This function basically parses the ACPI table for AMD IOMMU (IVRS)
 * three times:
 *
 *	1 pass) Find the highest PCI device id the driver has to handle.
 *		Upon this information the size of the data structures is
 *		determined that needs to be allocated.
 *
 *	2 pass) Initialize the data structures just allocated with the
 *		information in the ACPI table about available AMD IOMMUs
 *		in the system. It also maps the PCI devices in the
 *		system to specific IOMMUs
 *
 *	3 pass) After the basic data structures are allocated and
 *		initialized we update them with information about memory
 *		remapping requirements parsed out of the ACPI table in
 *		this last pass.
 *
 * After that the hardware is initialized and ready to go. In the last
 * step we do some Linux specific things like registering the driver in
 * the dma_ops interface and initializing the suspend/resume support
 * functions. Finally it prints some information about AMD IOMMUs and
 * the driver state and enables the hardware.
 */
static int __init amd_iommu_init(void)
{
	int i, ret = 0;

	/*
	 * First parse ACPI tables to find the largest Bus/Dev/Func
	 * we need to handle. Upon this information the shared data
	 * structures for the IOMMUs in the system will be allocated
	 */
	if (acpi_table_parse("IVRS", find_last_devid_acpi) != 0)
		return -ENODEV;

	ret = amd_iommu_init_err;
	if (ret)
		goto out;

	dev_table_size     = tbl_size(DEV_TABLE_ENTRY_SIZE);
	alias_table_size   = tbl_size(ALIAS_TABLE_ENTRY_SIZE);
	rlookup_table_size = tbl_size(RLOOKUP_TABLE_ENTRY_SIZE);

	ret = -ENOMEM;

	/* Device table - directly used by all IOMMUs */
	amd_iommu_dev_table = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
				      get_order(dev_table_size));
	if (amd_iommu_dev_table == NULL)
		goto out;

	/*
	 * Alias table - map PCI Bus/Dev/Func to Bus/Dev/Func the
	 * IOMMU see for that device
	 */
	amd_iommu_alias_table = (void *)__get_free_pages(GFP_KERNEL,
			get_order(alias_table_size));
	if (amd_iommu_alias_table == NULL)
		goto free;

	/* IOMMU rlookup table - find the IOMMU for a specific device */
	amd_iommu_rlookup_table = (void *)__get_free_pages(
			GFP_KERNEL | __GFP_ZERO,
			get_order(rlookup_table_size));
	if (amd_iommu_rlookup_table == NULL)
		goto free;

	amd_iommu_pd_alloc_bitmap = (void *)__get_free_pages(
					    GFP_KERNEL | __GFP_ZERO,
					    get_order(MAX_DOMAIN_ID/8));
	if (amd_iommu_pd_alloc_bitmap == NULL)
		goto free;

	/* init the device table */
	init_device_table();

	/*
	 * let all alias entries point to itself
	 */
	for (i = 0; i <= amd_iommu_last_bdf; ++i)
		amd_iommu_alias_table[i] = i;

	/*
	 * never allocate domain 0 because its used as the non-allocated and
	 * error value placeholder
	 */
	amd_iommu_pd_alloc_bitmap[0] = 1;

	spin_lock_init(&amd_iommu_pd_lock);

	/*
	 * now the data structures are allocated and basically initialized
	 * start the real acpi table scan
	 */
	ret = -ENODEV;
	if (acpi_table_parse("IVRS", init_iommu_all) != 0)
		goto free;

	if (amd_iommu_init_err) {
		ret = amd_iommu_init_err;
		goto free;
	}

	if (acpi_table_parse("IVRS", init_memory_definitions) != 0)
		goto free;

	if (amd_iommu_init_err) {
		ret = amd_iommu_init_err;
		goto free;
	}

	ret = amd_iommu_init_devices();
	if (ret)
		goto free;

	enable_iommus();

	if (iommu_pass_through)
		ret = amd_iommu_init_passthrough();
	else
		ret = amd_iommu_init_dma_ops();

	if (ret)
		goto free_disable;

	amd_iommu_init_api();

	amd_iommu_init_notifier();

	register_syscore_ops(&amd_iommu_syscore_ops);

	if (iommu_pass_through)
		goto out;

	if (amd_iommu_unmap_flush)
		printk(KERN_INFO "AMD-Vi: IO/TLB flush on unmap enabled\n");
	else
		printk(KERN_INFO "AMD-Vi: Lazy IO/TLB flushing enabled\n");

	x86_platform.iommu_shutdown = disable_iommus;
out:
	return ret;

free_disable:
	disable_iommus();

free:
	amd_iommu_uninit_devices();

	free_pages((unsigned long)amd_iommu_pd_alloc_bitmap,
		   get_order(MAX_DOMAIN_ID/8));

	free_pages((unsigned long)amd_iommu_rlookup_table,
		   get_order(rlookup_table_size));

	free_pages((unsigned long)amd_iommu_alias_table,
		   get_order(alias_table_size));

	free_pages((unsigned long)amd_iommu_dev_table,
		   get_order(dev_table_size));

	free_iommu_all();

	free_unity_maps();

#ifdef CONFIG_GART_IOMMU
	/*
	 * We failed to initialize the AMD IOMMU - try fallback to GART
	 * if possible.
	 */
	gart_iommu_init();

#endif

	goto out;
}

/****************************************************************************
 *
 * Early detect code. This code runs at IOMMU detection time in the DMA
 * layer. It just looks if there is an IVRS ACPI table to detect AMD
 * IOMMUs
 *
 ****************************************************************************/
static int __init early_amd_iommu_detect(struct acpi_table_header *table)
{
	return 0;
}

int __init amd_iommu_detect(void)
{
	if (no_iommu || (iommu_detected && !gart_iommu_aperture))
		return -ENODEV;

	if (amd_iommu_disabled)
		return -ENODEV;

	if (acpi_table_parse("IVRS", early_amd_iommu_detect) == 0) {
		iommu_detected = 1;
		amd_iommu_detected = 1;
		x86_init.iommu.iommu_init = amd_iommu_init;

		/* Make sure ACS will be enabled */
		pci_request_acs();
		return 1;
	}
	return -ENODEV;
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

static int __init parse_amd_iommu_options(char *str)
{
	for (; *str; ++str) {
		if (strncmp(str, "fullflush", 9) == 0)
			amd_iommu_unmap_flush = true;
		if (strncmp(str, "off", 3) == 0)
			amd_iommu_disabled = true;
		if (strncmp(str, "force_isolation", 15) == 0)
			amd_iommu_force_isolation = true;
	}

	return 1;
}

__setup("amd_iommu_dump", parse_amd_iommu_dump);
__setup("amd_iommu=", parse_amd_iommu_options);

IOMMU_INIT_FINISH(amd_iommu_detect,
		  gart_iommu_hole_init,
		  0,
		  0);

bool amd_iommu_v2_supported(void)
{
	return amd_iommu_v2_present;
}
EXPORT_SYMBOL(amd_iommu_v2_supported);
