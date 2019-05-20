/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
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
#include <linux/amd-iommu.h>
#include <linux/export.h>
#include <linux/iommu.h>
#include <linux/kmemleak.h>
#include <linux/mem_encrypt.h>
#include <asm/pci-direct.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/x86_init.h>
#include <asm/iommu_table.h>
#include <asm/io_apic.h>
#include <asm/irq_remapping.h>

#include <linux/crash_dump.h>
#include "amd_iommu_proto.h"
#include "amd_iommu_types.h"
#include "irq_remapping.h"

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
#define IVMD_FLAG_UNITY_MAP             0x01

#define ACPI_DEVFLAG_INITPASS           0x01
#define ACPI_DEVFLAG_EXTINT             0x02
#define ACPI_DEVFLAG_NMI                0x04
#define ACPI_DEVFLAG_SYSMGT1            0x10
#define ACPI_DEVFLAG_SYSMGT2            0x20
#define ACPI_DEVFLAG_LINT0              0x40
#define ACPI_DEVFLAG_LINT1              0x80
#define ACPI_DEVFLAG_ATSDIS             0x10000000

#define LOOP_TIMEOUT	100000
/*
 * ACPI table definitions
 *
 * These data structures are laid over the table to parse the important values
 * out of it.
 */

extern const struct iommu_ops amd_iommu_ops;

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
	u64 res;
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
	u32 hidh;
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
	u64 resv;
	u64 range_start;
	u64 range_length;
} __attribute__((packed));

bool amd_iommu_dump;
bool amd_iommu_irq_remap __read_mostly;

int amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_VAPIC;
static int amd_iommu_xt_mode = IRQ_REMAP_X2APIC_MODE;

static bool amd_iommu_detected;
static bool __initdata amd_iommu_disabled;
static int amd_iommu_target_ivhd_type;

u16 amd_iommu_last_bdf;			/* largest PCI device id we have
					   to handle */
LIST_HEAD(amd_iommu_unity_map);		/* a list of required unity mappings
					   we find in ACPI */
bool amd_iommu_unmap_flush;		/* if true, flush on every unmap */

LIST_HEAD(amd_iommu_list);		/* list of all AMD IOMMUs in the
					   system */

/* Array to assign indices to IOMMUs*/
struct amd_iommu *amd_iommus[MAX_IOMMUS];

/* Number of IOMMUs present in the system */
static int amd_iommus_present;

/* IOMMUs have a non-present cache? */
bool amd_iommu_np_cache __read_mostly;
bool amd_iommu_iotlb_sup __read_mostly = true;

u32 amd_iommu_max_pasid __read_mostly = ~0;

bool amd_iommu_v2_present __read_mostly;
static bool amd_iommu_pc_present __read_mostly;

bool amd_iommu_force_isolation __read_mostly;

/*
 * Pointer to the device table which is shared by all AMD IOMMUs
 * it is indexed by the PCI device id or the HT unit id and contains
 * information about the domain the device belongs to as well as the
 * page table root pointer.
 */
struct dev_table_entry *amd_iommu_dev_table;
/*
 * Pointer to a device table which the content of old device table
 * will be copied to. It's only be used in kdump kernel.
 */
static struct dev_table_entry *old_dev_tbl_cpy;

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
EXPORT_SYMBOL(amd_iommu_rlookup_table);

/*
 * This table is used to find the irq remapping table for a given device id
 * quickly.
 */
struct irq_remap_table **irq_lookup_table;

/*
 * AMD IOMMU allows up to 2^16 different protection domains. This is a bitmap
 * to know which ones are already in use.
 */
unsigned long *amd_iommu_pd_alloc_bitmap;

static u32 dev_table_size;	/* size of the device table */
static u32 alias_table_size;	/* size of the alias table */
static u32 rlookup_table_size;	/* size if the rlookup table */

enum iommu_init_state {
	IOMMU_START_STATE,
	IOMMU_IVRS_DETECTED,
	IOMMU_ACPI_FINISHED,
	IOMMU_ENABLED,
	IOMMU_PCI_INIT,
	IOMMU_INTERRUPTS_EN,
	IOMMU_DMA_OPS,
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
static void init_device_table_dma(void);

static bool amd_iommu_pre_enabled = true;

bool translation_pre_enabled(struct amd_iommu *iommu)
{
	return (iommu->flags & AMD_IOMMU_FLAG_TRANS_PRE_ENABLED);
}
EXPORT_SYMBOL(translation_pre_enabled);

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

int amd_iommu_get_num_iommus(void)
{
	return amd_iommus_present;
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

/* Programs the physical address of the device table into the IOMMU hardware */
static void iommu_set_device_table(struct amd_iommu *iommu)
{
	u64 entry;

	BUG_ON(iommu->mmio_base == NULL);

	entry = iommu_virt_to_phys(amd_iommu_dev_table);
	entry |= (dev_table_size >> 12) - 1;
	memcpy_toio(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET,
			&entry, sizeof(entry));
}

/* Generic functions to enable/disable certain features of the IOMMU. */
static void iommu_feature_enable(struct amd_iommu *iommu, u8 bit)
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
	/* Disable command buffer */
	iommu_feature_disable(iommu, CONTROL_CMDBUF_EN);

	/* Disable event logging and event interrupts */
	iommu_feature_disable(iommu, CONTROL_EVT_INT_EN);
	iommu_feature_disable(iommu, CONTROL_EVT_LOG_EN);

	/* Disable IOMMU GA_LOG */
	iommu_feature_disable(iommu, CONTROL_GALOG_EN);
	iommu_feature_disable(iommu, CONTROL_GAINT_EN);

	/* Disable IOMMU hardware itself */
	iommu_feature_disable(iommu, CONTROL_IOMMU_EN);
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

	return (u8 __iomem *)ioremap_nocache(address, end);
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
			update_last_devid(0xffff);
			break;
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
static int __init find_last_devid_acpi(struct acpi_table_header *table)
{
	u8 *p = (u8 *)table, *end = (u8 *)table;
	struct ivhd_header *h;

	p += IVRS_HEADER_LENGTH;

	end += table->length;
	while (p < end) {
		h = (struct ivhd_header *)p;
		if (h->type == amd_iommu_target_ivhd_type) {
			int ret = find_last_devid_from_ivhd(h);

			if (ret)
				return ret;
		}
		p += h->length;
	}
	WARN_ON(p != end);

	return 0;
}

/****************************************************************************
 *
 * The following functions belong to the code path which parses the ACPI table
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
static int __init alloc_command_buffer(struct amd_iommu *iommu)
{
	iommu->cmd_buf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						  get_order(CMD_BUFFER_SIZE));

	return iommu->cmd_buf ? 0 : -ENOMEM;
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
	free_pages((unsigned long)iommu->cmd_buf, get_order(CMD_BUFFER_SIZE));
}

/* allocates the memory where the IOMMU will log its events to */
static int __init alloc_event_buffer(struct amd_iommu *iommu)
{
	iommu->evt_buf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						  get_order(EVT_BUFFER_SIZE));

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
	free_pages((unsigned long)iommu->evt_buf, get_order(EVT_BUFFER_SIZE));
}

/* allocates the memory where the IOMMU will log its events to */
static int __init alloc_ppr_log(struct amd_iommu *iommu)
{
	iommu->ppr_log = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						  get_order(PPR_LOG_SIZE));

	return iommu->ppr_log ? 0 : -ENOMEM;
}

static void iommu_enable_ppr_log(struct amd_iommu *iommu)
{
	u64 entry;

	if (iommu->ppr_log == NULL)
		return;

	entry = iommu_virt_to_phys(iommu->ppr_log) | PPR_LOG_SIZE_512;

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

static void free_ga_log(struct amd_iommu *iommu)
{
#ifdef CONFIG_IRQ_REMAP
	if (iommu->ga_log)
		free_pages((unsigned long)iommu->ga_log,
			    get_order(GA_LOG_SIZE));
	if (iommu->ga_log_tail)
		free_pages((unsigned long)iommu->ga_log_tail,
			    get_order(8));
#endif
}

static int iommu_ga_log_enable(struct amd_iommu *iommu)
{
#ifdef CONFIG_IRQ_REMAP
	u32 status, i;

	if (!iommu->ga_log)
		return -EINVAL;

	status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);

	/* Check if already running */
	if (status & (MMIO_STATUS_GALOG_RUN_MASK))
		return 0;

	iommu_feature_enable(iommu, CONTROL_GAINT_EN);
	iommu_feature_enable(iommu, CONTROL_GALOG_EN);

	for (i = 0; i < LOOP_TIMEOUT; ++i) {
		status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
		if (status & (MMIO_STATUS_GALOG_RUN_MASK))
			break;
	}

	if (i >= LOOP_TIMEOUT)
		return -EINVAL;
#endif /* CONFIG_IRQ_REMAP */
	return 0;
}

#ifdef CONFIG_IRQ_REMAP
static int iommu_init_ga_log(struct amd_iommu *iommu)
{
	u64 entry;

	if (!AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir))
		return 0;

	iommu->ga_log = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(GA_LOG_SIZE));
	if (!iommu->ga_log)
		goto err_out;

	iommu->ga_log_tail = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(8));
	if (!iommu->ga_log_tail)
		goto err_out;

	entry = iommu_virt_to_phys(iommu->ga_log) | GA_LOG_SIZE_512;
	memcpy_toio(iommu->mmio_base + MMIO_GA_LOG_BASE_OFFSET,
		    &entry, sizeof(entry));
	entry = (iommu_virt_to_phys(iommu->ga_log_tail) &
		 (BIT_ULL(52)-1)) & ~7ULL;
	memcpy_toio(iommu->mmio_base + MMIO_GA_LOG_TAIL_OFFSET,
		    &entry, sizeof(entry));
	writel(0x00, iommu->mmio_base + MMIO_GA_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_GA_TAIL_OFFSET);

	return 0;
err_out:
	free_ga_log(iommu);
	return -EINVAL;
}
#endif /* CONFIG_IRQ_REMAP */

static int iommu_init_ga(struct amd_iommu *iommu)
{
	int ret = 0;

#ifdef CONFIG_IRQ_REMAP
	/* Note: We have already checked GASup from IVRS table.
	 *       Now, we need to make sure that GAMSup is set.
	 */
	if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir) &&
	    !iommu_feature(iommu, FEATURE_GAM_VAPIC))
		amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY_GA;

	ret = iommu_init_ga_log(iommu);
#endif /* CONFIG_IRQ_REMAP */

	return ret;
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


static bool copy_device_table(void)
{
	u64 int_ctl, int_tab_len, entry = 0, last_entry = 0;
	struct dev_table_entry *old_devtb = NULL;
	u32 lo, hi, devid, old_devtb_size;
	phys_addr_t old_devtb_phys;
	struct amd_iommu *iommu;
	u16 dom_id, dte_v, irq_v;
	gfp_t gfp_flag;
	u64 tmp;

	if (!amd_iommu_pre_enabled)
		return false;

	pr_warn("Translation is already enabled - trying to copy translation structures\n");
	for_each_iommu(iommu) {
		/* All IOMMUs should use the same device table with the same size */
		lo = readl(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET);
		hi = readl(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET + 4);
		entry = (((u64) hi) << 32) + lo;
		if (last_entry && last_entry != entry) {
			pr_err("IOMMU:%d should use the same dev table as others!\n",
				iommu->index);
			return false;
		}
		last_entry = entry;

		old_devtb_size = ((entry & ~PAGE_MASK) + 1) << 12;
		if (old_devtb_size != dev_table_size) {
			pr_err("The device table size of IOMMU:%d is not expected!\n",
				iommu->index);
			return false;
		}
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
	old_devtb = (sme_active() && is_kdump_kernel())
		    ? (__force void *)ioremap_encrypted(old_devtb_phys,
							dev_table_size)
		    : memremap(old_devtb_phys, dev_table_size, MEMREMAP_WB);

	if (!old_devtb)
		return false;

	gfp_flag = GFP_KERNEL | __GFP_ZERO | GFP_DMA32;
	old_dev_tbl_cpy = (void *)__get_free_pages(gfp_flag,
				get_order(dev_table_size));
	if (old_dev_tbl_cpy == NULL) {
		pr_err("Failed to allocate memory for copying old device table!\n");
		return false;
	}

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid) {
		old_dev_tbl_cpy[devid] = old_devtb[devid];
		dom_id = old_devtb[devid].data[1] & DEV_DOMID_MASK;
		dte_v = old_devtb[devid].data[0] & DTE_FLAG_V;

		if (dte_v && dom_id) {
			old_dev_tbl_cpy[devid].data[0] = old_devtb[devid].data[0];
			old_dev_tbl_cpy[devid].data[1] = old_devtb[devid].data[1];
			__set_bit(dom_id, amd_iommu_pd_alloc_bitmap);
			/* If gcr3 table existed, mask it out */
			if (old_devtb[devid].data[0] & DTE_FLAG_GV) {
				tmp = DTE_GCR3_VAL_B(~0ULL) << DTE_GCR3_SHIFT_B;
				tmp |= DTE_GCR3_VAL_C(~0ULL) << DTE_GCR3_SHIFT_C;
				old_dev_tbl_cpy[devid].data[1] &= ~tmp;
				tmp = DTE_GCR3_VAL_A(~0ULL) << DTE_GCR3_SHIFT_A;
				tmp |= DTE_FLAG_GV;
				old_dev_tbl_cpy[devid].data[0] &= ~tmp;
			}
		}

		irq_v = old_devtb[devid].data[2] & DTE_IRQ_REMAP_ENABLE;
		int_ctl = old_devtb[devid].data[2] & DTE_IRQ_REMAP_INTCTL_MASK;
		int_tab_len = old_devtb[devid].data[2] & DTE_IRQ_TABLE_LEN_MASK;
		if (irq_v && (int_ctl || int_tab_len)) {
			if ((int_ctl != DTE_IRQ_REMAP_INTCTL) ||
			    (int_tab_len != DTE_IRQ_TABLE_LEN)) {
				pr_err("Wrong old irq remapping flag: %#x\n", devid);
				return false;
			}

		        old_dev_tbl_cpy[devid].data[2] = old_devtb[devid].data[2];
		}
	}
	memunmap(old_devtb);

	return true;
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

static int __init add_special_device(u8 type, u8 id, u16 *devid, bool cmd_line)
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

static int __init add_acpi_hid_device(u8 *hid, u8 *uid, u16 *devid,
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
 * Reads the device exclusion range from ACPI and initializes the IOMMU with
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
		set_dev_entry_bit(devid, DEV_ENTRY_EX);
		iommu->exclusion_start = m->range_start;
		iommu->exclusion_length = m->range_length;
	}
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
	u16 devid = 0, devid_start = 0, devid_to = 0;
	u32 dev_i, ext_flags = 0;
	bool alias = false;
	struct ivhd_entry *e;
	u32 ivhd_size;
	int ret;


	ret = add_early_maps();
	if (ret)
		return ret;

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
		switch (e->type) {
		case IVHD_DEV_ALL:

			DUMP_printk("  DEV_ALL\t\t\tflags: %02x\n", e->flags);

			for (dev_i = 0; dev_i <= amd_iommu_last_bdf; ++dev_i)
				set_dev_entry_from_acpi(iommu, dev_i, e->flags, 0);
			break;
		case IVHD_DEV_SELECT:

			DUMP_printk("  DEV_SELECT\t\t\t devid: %02x:%02x.%x "
				    "flags: %02x\n",
				    PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags);

			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);
			break;
		case IVHD_DEV_SELECT_RANGE_START:

			DUMP_printk("  DEV_SELECT_RANGE_START\t "
				    "devid: %02x:%02x.%x flags: %02x\n",
				    PCI_BUS_NUM(e->devid),
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
				    PCI_BUS_NUM(e->devid),
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
			amd_iommu_alias_table[devid] = devid_to;
			break;
		case IVHD_DEV_ALIAS_RANGE:

			DUMP_printk("  DEV_ALIAS_RANGE\t\t "
				    "devid: %02x:%02x.%x flags: %02x "
				    "devid_to: %02x:%02x.%x\n",
				    PCI_BUS_NUM(e->devid),
				    PCI_SLOT(e->devid),
				    PCI_FUNC(e->devid),
				    e->flags,
				    PCI_BUS_NUM(e->ext >> 8),
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
				    PCI_BUS_NUM(e->devid),
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
				    PCI_BUS_NUM(e->devid),
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
				    PCI_BUS_NUM(e->devid),
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
		case IVHD_DEV_SPECIAL: {
			u8 handle, type;
			const char *var;
			u16 devid;
			int ret;

			handle = e->ext & 0xff;
			devid  = (e->ext >>  8) & 0xffff;
			type   = (e->ext >> 24) & 0xff;

			if (type == IVHD_SPECIAL_IOAPIC)
				var = "IOAPIC";
			else if (type == IVHD_SPECIAL_HPET)
				var = "HPET";
			else
				var = "UNKNOWN";

			DUMP_printk("  DEV_SPECIAL(%s[%d])\t\tdevid: %02x:%02x.%x\n",
				    var, (int)handle,
				    PCI_BUS_NUM(devid),
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
			u16 devid;
			u8 hid[ACPIHID_HID_LEN] = {0};
			u8 uid[ACPIHID_UID_LEN] = {0};
			int ret;

			if (h->type != 0x40) {
				pr_err(FW_BUG "Invalid IVHD device type %#x\n",
				       e->type);
				break;
			}

			memcpy(hid, (u8 *)(&e->ext), ACPIHID_HID_LEN - 1);
			hid[ACPIHID_HID_LEN - 1] = '\0';

			if (!(*hid)) {
				pr_err(FW_BUG "Invalid HID.\n");
				break;
			}

			switch (e->uidf) {
			case UID_NOT_PRESENT:

				if (e->uidl != 0)
					pr_warn(FW_BUG "Invalid UID length.\n");

				break;
			case UID_IS_INTEGER:

				sprintf(uid, "%d", e->uid);

				break;
			case UID_IS_CHARACTER:

				memcpy(uid, (u8 *)(&e->uid), ACPIHID_UID_LEN - 1);
				uid[ACPIHID_UID_LEN - 1] = '\0';

				break;
			default:
				break;
			}

			devid = e->devid;
			DUMP_printk("  DEV_ACPI_HID(%s[%s])\t\tdevid: %02x:%02x.%x\n",
				    hid, uid,
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

static void __init free_iommu_one(struct amd_iommu *iommu)
{
	free_command_buffer(iommu);
	free_event_buffer(iommu);
	free_ppr_log(iommu);
	free_ga_log(iommu);
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
 * This function clues the initialization function for one IOMMU
 * together and also allocates the command buffer and programs the
 * hardware. It does NOT enable the IOMMU. This is done afterwards.
 */
static int __init init_iommu_one(struct amd_iommu *iommu, struct ivhd_header *h)
{
	int ret;

	raw_spin_lock_init(&iommu->lock);

	/* Add IOMMU to internal data structures */
	list_add_tail(&iommu->list, &amd_iommu_list);
	iommu->index = amd_iommus_present++;

	if (unlikely(iommu->index >= MAX_IOMMUS)) {
		WARN(1, "System has more IOMMUs than supported by this driver\n");
		return -ENOSYS;
	}

	/* Index is fine - add IOMMU to the array */
	amd_iommus[iommu->index] = iommu;

	/*
	 * Copy data from ACPI table entry to the iommu struct
	 */
	iommu->devid   = h->devid;
	iommu->cap_ptr = h->cap_ptr;
	iommu->pci_seg = h->pci_seg;
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
		if (((h->efr_attr & (0x1 << IOMMU_FEAT_GASUP_SHIFT)) == 0))
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY;
		if (((h->efr_attr & (0x1 << IOMMU_FEAT_XTSUP_SHIFT)) == 0))
			amd_iommu_xt_mode = IRQ_REMAP_XAPIC_MODE;
		break;
	case 0x11:
	case 0x40:
		if (h->efr_reg & (1 << 9))
			iommu->mmio_phys_end = MMIO_REG_END_OFFSET;
		else
			iommu->mmio_phys_end = MMIO_CNTR_CONF_OFFSET;
		if (((h->efr_reg & (0x1 << IOMMU_EFR_GASUP_SHIFT)) == 0))
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY;
		if (((h->efr_reg & (0x1 << IOMMU_EFR_XTSUP_SHIFT)) == 0))
			amd_iommu_xt_mode = IRQ_REMAP_XAPIC_MODE;
		break;
	default:
		return -EINVAL;
	}

	iommu->mmio_base = iommu_map_mmio_space(iommu->mmio_phys,
						iommu->mmio_phys_end);
	if (!iommu->mmio_base)
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

	ret = init_iommu_from_acpi(iommu, h);
	if (ret)
		return ret;

	ret = amd_iommu_create_irq_domain(iommu);
	if (ret)
		return ret;

	/*
	 * Make sure IOMMU is not considered to translate itself. The IVRS
	 * table tells us so, but this is a lie!
	 */
	amd_iommu_rlookup_table[iommu->devid] = NULL;

	return 0;
}

/**
 * get_highest_supported_ivhd_type - Look up the appropriate IVHD type
 * @ivrs          Pointer to the IVRS header
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

	while (p < end) {
		h = (struct ivhd_header *)p;
		if (*p == amd_iommu_target_ivhd_type) {

			DUMP_printk("device: %02x:%02x.%01x cap: %04x "
				    "seg: %d flags: %01x info %04x\n",
				    PCI_BUS_NUM(h->devid), PCI_SLOT(h->devid),
				    PCI_FUNC(h->devid), h->cap_ptr,
				    h->pci_seg, h->flags, h->info);
			DUMP_printk("       mmio-addr: %016llx\n",
				    h->mmio_phys);

			iommu = kzalloc(sizeof(struct amd_iommu), GFP_KERNEL);
			if (iommu == NULL)
				return -ENOMEM;

			ret = init_iommu_one(iommu, h);
			if (ret)
				return ret;
		}
		p += h->length;

	}
	WARN_ON(p != end);

	return 0;
}

static int iommu_pc_get_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr,
				u8 fxn, u64 *value, bool is_write);

static void init_iommu_perf_ctr(struct amd_iommu *iommu)
{
	struct pci_dev *pdev = iommu->dev;
	u64 val = 0xabcd, val2 = 0;

	if (!iommu_feature(iommu, FEATURE_PC))
		return;

	amd_iommu_pc_present = true;

	/* Check if the performance counters can be written to */
	if ((iommu_pc_get_set_reg(iommu, 0, 0, 0, &val, true)) ||
	    (iommu_pc_get_set_reg(iommu, 0, 0, 0, &val2, false)) ||
	    (val != val2)) {
		pci_err(pdev, "Unable to write to IOMMU perf counter.\n");
		amd_iommu_pc_present = false;
		return;
	}

	pci_info(pdev, "IOMMU performance counters supported\n");

	val = readl(iommu->mmio_base + MMIO_CNTR_CONF_OFFSET);
	iommu->max_banks = (u8) ((val >> 12) & 0x3f);
	iommu->max_counters = (u8) ((val >> 7) & 0xf);
}

static ssize_t amd_iommu_show_cap(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct amd_iommu *iommu = dev_to_amd_iommu(dev);
	return sprintf(buf, "%x\n", iommu->cap);
}
static DEVICE_ATTR(cap, S_IRUGO, amd_iommu_show_cap, NULL);

static ssize_t amd_iommu_show_features(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct amd_iommu *iommu = dev_to_amd_iommu(dev);
	return sprintf(buf, "%llx\n", iommu->features);
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

static int __init iommu_init_pci(struct amd_iommu *iommu)
{
	int cap_ptr = iommu->cap_ptr;
	u32 range, misc, low, high;
	int ret;

	iommu->dev = pci_get_domain_bus_and_slot(0, PCI_BUS_NUM(iommu->devid),
						 iommu->devid & 0xff);
	if (!iommu->dev)
		return -ENODEV;

	/* Prevent binding other PCI device drivers to IOMMU devices */
	iommu->dev->match_driver = false;

	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_CAP_HDR_OFFSET,
			      &iommu->cap);
	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_RANGE_OFFSET,
			      &range);
	pci_read_config_dword(iommu->dev, cap_ptr + MMIO_MISC_OFFSET,
			      &misc);

	if (!(iommu->cap & (1 << IOMMU_CAP_IOTLB)))
		amd_iommu_iotlb_sup = false;

	/* read extended feature bits */
	low  = readl(iommu->mmio_base + MMIO_EXT_FEATURES);
	high = readl(iommu->mmio_base + MMIO_EXT_FEATURES + 4);

	iommu->features = ((u64)high << 32) | low;

	if (iommu_feature(iommu, FEATURE_GT)) {
		int glxval;
		u32 max_pasid;
		u64 pasmax;

		pasmax = iommu->features & FEATURE_PASID_MASK;
		pasmax >>= FEATURE_PASID_SHIFT;
		max_pasid  = (1 << (pasmax + 1)) - 1;

		amd_iommu_max_pasid = min(amd_iommu_max_pasid, max_pasid);

		BUG_ON(amd_iommu_max_pasid & ~PASID_MASK);

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

	if (iommu_feature(iommu, FEATURE_PPR) && alloc_ppr_log(iommu))
		return -ENOMEM;

	ret = iommu_init_ga(iommu);
	if (ret)
		return ret;

	if (iommu->cap & (1UL << IOMMU_CAP_NPCACHE))
		amd_iommu_np_cache = true;

	init_iommu_perf_ctr(iommu);

	if (is_rd890_iommu(iommu->dev)) {
		int i, j;

		iommu->root_pdev =
			pci_get_domain_bus_and_slot(0, iommu->dev->bus->number,
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

	iommu_device_sysfs_add(&iommu->iommu, &iommu->dev->dev,
			       amd_iommu_groups, "ivhd%d", iommu->index);
	iommu_device_set_ops(&iommu->iommu, &amd_iommu_ops);
	iommu_device_register(&iommu->iommu);

	return pci_enable_device(iommu->dev);
}

static void print_iommu_info(void)
{
	static const char * const feat_str[] = {
		"PreF", "PPR", "X2APIC", "NX", "GT", "[5]",
		"IA", "GA", "HE", "PC"
	};
	struct amd_iommu *iommu;

	for_each_iommu(iommu) {
		struct pci_dev *pdev = iommu->dev;
		int i;

		pci_info(pdev, "Found IOMMU cap 0x%hx\n", iommu->cap_ptr);

		if (iommu->cap & (1 << IOMMU_CAP_EFR)) {
			pci_info(pdev, "Extended features (%#llx):\n",
				 iommu->features);
			for (i = 0; i < ARRAY_SIZE(feat_str); ++i) {
				if (iommu_feature(iommu, (1ULL << i)))
					pr_cont(" %s", feat_str[i]);
			}

			if (iommu->features & FEATURE_GAM_VAPIC)
				pr_cont(" GA_vAPIC");

			pr_cont("\n");
		}
	}
	if (irq_remapping_enabled) {
		pr_info("Interrupt remapping enabled\n");
		if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir))
			pr_info("Virtual APIC enabled\n");
		if (amd_iommu_xt_mode == IRQ_REMAP_X2APIC_MODE)
			pr_info("X2APIC enabled\n");
	}
}

static int __init amd_iommu_init_pci(void)
{
	struct amd_iommu *iommu;
	int ret = 0;

	for_each_iommu(iommu) {
		ret = iommu_init_pci(iommu);
		if (ret)
			break;
	}

	/*
	 * Order is important here to make sure any unity map requirements are
	 * fulfilled. The unity mappings are created and written to the device
	 * table during the amd_iommu_init_api() call.
	 *
	 * After that we call init_device_table_dma() to make sure any
	 * uninitialized DTE will block DMA, and in the end we flush the caches
	 * of all IOMMUs to make sure the changes to the device table are
	 * active.
	 */
	ret = amd_iommu_init_api();

	init_device_table_dma();

	for_each_iommu(iommu)
		iommu_flush_all_caches(iommu);

	if (!ret)
		print_iommu_info();

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

	iommu->int_enabled = true;

	return 0;
}

static int iommu_init_msi(struct amd_iommu *iommu)
{
	int ret;

	if (iommu->int_enabled)
		goto enable_faults;

	if (iommu->dev->msi_cap)
		ret = iommu_setup_msi(iommu);
	else
		ret = -ENODEV;

	if (ret)
		return ret;

enable_faults:
	iommu_feature_enable(iommu, CONTROL_EVT_INT_EN);

	if (iommu->ppr_log != NULL)
		iommu_feature_enable(iommu, CONTROL_PPFINT_EN);

	iommu_ga_log_enable(iommu);

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
	struct unity_map_entry *e = NULL;
	char *s;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	if (m->flags & IVMD_FLAG_EXCL_RANGE)
		init_exclusion_range(m);

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
		    PCI_BUS_NUM(e->devid_start), PCI_SLOT(e->devid_start),
		    PCI_FUNC(e->devid_start), PCI_BUS_NUM(e->devid_end),
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
		if (m->flags & (IVMD_FLAG_UNITY_MAP | IVMD_FLAG_EXCL_RANGE))
			init_unity_map_range(m);

		p += m->length;
	}

	return 0;
}

/*
 * Init the device table to not allow DMA access for devices
 */
static void init_device_table_dma(void)
{
	u32 devid;

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid) {
		set_dev_entry_bit(devid, DEV_ENTRY_VALID);
		set_dev_entry_bit(devid, DEV_ENTRY_TRANSLATION);
	}
}

static void __init uninit_device_table_dma(void)
{
	u32 devid;

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid) {
		amd_iommu_dev_table[devid].data[0] = 0ULL;
		amd_iommu_dev_table[devid].data[1] = 0ULL;
	}
}

static void init_device_table(void)
{
	u32 devid;

	if (!amd_iommu_irq_remap)
		return;

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid)
		set_dev_entry_bit(devid, DEV_ENTRY_IRQ_TBL_EN);
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
		iommu_feature_enable(iommu, CONTROL_GAM_EN);
		/* Fall through */
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

static void early_enable_iommu(struct amd_iommu *iommu)
{
	iommu_disable(iommu);
	iommu_init_flags(iommu);
	iommu_set_device_table(iommu);
	iommu_enable_command_buffer(iommu);
	iommu_enable_event_buffer(iommu);
	iommu_set_exclusion_range(iommu);
	iommu_enable_ga(iommu);
	iommu_enable_xt(iommu);
	iommu_enable(iommu);
	iommu_flush_all_caches(iommu);
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


	if (!copy_device_table()) {
		/*
		 * If come here because of failure in copying device table from old
		 * kernel with all IOMMUs enabled, print error message and try to
		 * free allocated old_dev_tbl_cpy.
		 */
		if (amd_iommu_pre_enabled)
			pr_err("Failed to copy DEV table from previous kernel.\n");
		if (old_dev_tbl_cpy != NULL)
			free_pages((unsigned long)old_dev_tbl_cpy,
					get_order(dev_table_size));

		for_each_iommu(iommu) {
			clear_translation_pre_enabled(iommu);
			early_enable_iommu(iommu);
		}
	} else {
		pr_info("Copied DEV table from previous kernel.\n");
		free_pages((unsigned long)amd_iommu_dev_table,
				get_order(dev_table_size));
		amd_iommu_dev_table = old_dev_tbl_cpy;
		for_each_iommu(iommu) {
			iommu_disable_command_buffer(iommu);
			iommu_disable_event_buffer(iommu);
			iommu_enable_command_buffer(iommu);
			iommu_enable_event_buffer(iommu);
			iommu_enable_ga(iommu);
			iommu_enable_xt(iommu);
			iommu_set_device_table(iommu);
			iommu_flush_all_caches(iommu);
		}
	}

#ifdef CONFIG_IRQ_REMAP
	if (AMD_IOMMU_GUEST_IR_VAPIC(amd_iommu_guest_ir))
		amd_iommu_irq_ops.capability |= (1 << IRQ_POSTING_CAP);
#endif
}

static void enable_iommus_v2(void)
{
	struct amd_iommu *iommu;

	for_each_iommu(iommu) {
		iommu_enable_ppr_log(iommu);
		iommu_enable_gt(iommu);
	}
}

static void enable_iommus(void)
{
	early_enable_iommus();

	enable_iommus_v2();
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
	enable_iommus();

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
	kmemleak_free(irq_lookup_table);
	free_pages((unsigned long)irq_lookup_table,
		   get_order(rlookup_table_size));
	irq_lookup_table = NULL;

	kmem_cache_destroy(amd_iommu_irq_cache);
	amd_iommu_irq_cache = NULL;

	free_pages((unsigned long)amd_iommu_rlookup_table,
		   get_order(rlookup_table_size));
	amd_iommu_rlookup_table = NULL;

	free_pages((unsigned long)amd_iommu_alias_table,
		   get_order(alias_table_size));
	amd_iommu_alias_table = NULL;

	free_pages((unsigned long)amd_iommu_dev_table,
		   get_order(dev_table_size));
	amd_iommu_dev_table = NULL;

	free_iommu_all();

#ifdef CONFIG_GART_IOMMU
	/*
	 * We failed to initialize the AMD IOMMU - try fallback to GART
	 * if possible.
	 */
	gart_iommu_init();

#endif
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
	free_pages((unsigned long)amd_iommu_pd_alloc_bitmap,
		   get_order(MAX_DOMAIN_ID/8));
	amd_iommu_pd_alloc_bitmap = NULL;

	free_unity_maps();
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
	acpi_status status;
	int i, remap_cache_sz, ret = 0;

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

	amd_iommu_target_ivhd_type = get_highest_supported_ivhd_type(ivrs_base);
	DUMP_printk("Using IVHD type %#x\n", amd_iommu_target_ivhd_type);

	/*
	 * First parse ACPI tables to find the largest Bus/Dev/Func
	 * we need to handle. Upon this information the shared data
	 * structures for the IOMMUs in the system will be allocated
	 */
	ret = find_last_devid_acpi(ivrs_base);
	if (ret)
		goto out;

	dev_table_size     = tbl_size(DEV_TABLE_ENTRY_SIZE);
	alias_table_size   = tbl_size(ALIAS_TABLE_ENTRY_SIZE);
	rlookup_table_size = tbl_size(RLOOKUP_TABLE_ENTRY_SIZE);

	/* Device table - directly used by all IOMMUs */
	ret = -ENOMEM;
	amd_iommu_dev_table = (void *)__get_free_pages(
				      GFP_KERNEL | __GFP_ZERO | GFP_DMA32,
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
		goto out;

	/* IOMMU rlookup table - find the IOMMU for a specific device */
	amd_iommu_rlookup_table = (void *)__get_free_pages(
			GFP_KERNEL | __GFP_ZERO,
			get_order(rlookup_table_size));
	if (amd_iommu_rlookup_table == NULL)
		goto out;

	amd_iommu_pd_alloc_bitmap = (void *)__get_free_pages(
					    GFP_KERNEL | __GFP_ZERO,
					    get_order(MAX_DOMAIN_ID/8));
	if (amd_iommu_pd_alloc_bitmap == NULL)
		goto out;

	/*
	 * let all alias entries point to itself
	 */
	for (i = 0; i <= amd_iommu_last_bdf; ++i)
		amd_iommu_alias_table[i] = i;

	/*
	 * never allocate domain 0 because its used as the non-allocated and
	 * error value placeholder
	 */
	__set_bit(0, amd_iommu_pd_alloc_bitmap);

	/*
	 * now the data structures are allocated and basically initialized
	 * start the real acpi table scan
	 */
	ret = init_iommu_all(ivrs_base);
	if (ret)
		goto out;

	/* Disable any previously enabled IOMMUs */
	if (!is_kdump_kernel() || amd_iommu_disabled)
		disable_iommus();

	if (amd_iommu_irq_remap)
		amd_iommu_irq_remap = check_ioapic_information();

	if (amd_iommu_irq_remap) {
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
							IRQ_TABLE_ALIGNMENT,
							0, NULL);
		if (!amd_iommu_irq_cache)
			goto out;

		irq_lookup_table = (void *)__get_free_pages(
				GFP_KERNEL | __GFP_ZERO,
				get_order(rlookup_table_size));
		kmemleak_alloc(irq_lookup_table, rlookup_table_size,
			       1, GFP_KERNEL);
		if (!irq_lookup_table)
			goto out;
	}

	ret = init_memory_definitions(ivrs_base);
	if (ret)
		goto out;

	/* init the device table */
	init_device_table();

out:
	/* Don't leak any ACPI memory */
	acpi_put_table(ivrs_base);
	ivrs_base = NULL;

	return ret;
}

static int amd_iommu_enable_interrupts(void)
{
	struct amd_iommu *iommu;
	int ret = 0;

	for_each_iommu(iommu) {
		ret = iommu_init_msi(iommu);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static bool detect_ivrs(void)
{
	struct acpi_table_header *ivrs_base;
	acpi_status status;

	status = acpi_get_table("IVRS", 0, &ivrs_base);
	if (status == AE_NOT_FOUND)
		return false;
	else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);
		pr_err("IVRS table error: %s\n", err);
		return false;
	}

	acpi_put_table(ivrs_base);

	/* Make sure ACS will be enabled during PCI probe */
	pci_request_acs();

	return true;
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
		ret = early_amd_iommu_init();
		init_state = ret ? IOMMU_INIT_ERROR : IOMMU_ACPI_FINISHED;
		if (init_state == IOMMU_ACPI_FINISHED && amd_iommu_disabled) {
			pr_info("AMD IOMMU disabled on kernel command-line\n");
			free_dma_resources();
			free_iommu_resources();
			init_state = IOMMU_CMDLINE_DISABLED;
			ret = -EINVAL;
		}
		break;
	case IOMMU_ACPI_FINISHED:
		early_enable_iommus();
		x86_platform.iommu_shutdown = disable_iommus;
		init_state = IOMMU_ENABLED;
		break;
	case IOMMU_ENABLED:
		register_syscore_ops(&amd_iommu_syscore_ops);
		ret = amd_iommu_init_pci();
		init_state = ret ? IOMMU_INIT_ERROR : IOMMU_PCI_INIT;
		enable_iommus_v2();
		break;
	case IOMMU_PCI_INIT:
		ret = amd_iommu_enable_interrupts();
		init_state = ret ? IOMMU_INIT_ERROR : IOMMU_INTERRUPTS_EN;
		break;
	case IOMMU_INTERRUPTS_EN:
		ret = amd_iommu_init_dma_ops();
		init_state = ret ? IOMMU_INIT_ERROR : IOMMU_DMA_OPS;
		break;
	case IOMMU_DMA_OPS:
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
	if (ret)
		return ret;
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

int __init amd_iommu_enable_faulting(void)
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
	if (ret) {
		free_dma_resources();
		if (!irq_remapping_enabled) {
			disable_iommus();
			free_iommu_resources();
		} else {
			uninit_device_table_dma();
			for_each_iommu(iommu)
				iommu_flush_all_caches(iommu);
		}
	}

	for_each_iommu(iommu)
		amd_iommu_debugfs_setup(iommu);

	return ret;
}

static bool amd_iommu_sme_check(void)
{
	if (!sme_active() || (boot_cpu_data.x86 != 0x17))
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
			amd_iommu_guest_ir = AMD_IOMMU_GUEST_IR_LEGACY;
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

static int __init parse_ivrs_ioapic(char *str)
{
	unsigned int bus, dev, fn;
	int ret, id, i;
	u16 devid;

	ret = sscanf(str, "[%d]=%x:%x.%x", &id, &bus, &dev, &fn);

	if (ret != 4) {
		pr_err("Invalid command line: ivrs_ioapic%s\n", str);
		return 1;
	}

	if (early_ioapic_map_size == EARLY_MAP_SIZE) {
		pr_err("Early IOAPIC map overflow - ignoring ivrs_ioapic%s\n",
			str);
		return 1;
	}

	devid = ((bus & 0xff) << 8) | ((dev & 0x1f) << 3) | (fn & 0x7);

	cmdline_maps			= true;
	i				= early_ioapic_map_size++;
	early_ioapic_map[i].id		= id;
	early_ioapic_map[i].devid	= devid;
	early_ioapic_map[i].cmd_line	= true;

	return 1;
}

static int __init parse_ivrs_hpet(char *str)
{
	unsigned int bus, dev, fn;
	int ret, id, i;
	u16 devid;

	ret = sscanf(str, "[%d]=%x:%x.%x", &id, &bus, &dev, &fn);

	if (ret != 4) {
		pr_err("Invalid command line: ivrs_hpet%s\n", str);
		return 1;
	}

	if (early_hpet_map_size == EARLY_MAP_SIZE) {
		pr_err("Early HPET map overflow - ignoring ivrs_hpet%s\n",
			str);
		return 1;
	}

	devid = ((bus & 0xff) << 8) | ((dev & 0x1f) << 3) | (fn & 0x7);

	cmdline_maps			= true;
	i				= early_hpet_map_size++;
	early_hpet_map[i].id		= id;
	early_hpet_map[i].devid		= devid;
	early_hpet_map[i].cmd_line	= true;

	return 1;
}

static int __init parse_ivrs_acpihid(char *str)
{
	u32 bus, dev, fn;
	char *hid, *uid, *p;
	char acpiid[ACPIHID_UID_LEN + ACPIHID_HID_LEN] = {0};
	int ret, i;

	ret = sscanf(str, "[%x:%x.%x]=%s", &bus, &dev, &fn, acpiid);
	if (ret != 4) {
		pr_err("Invalid command line: ivrs_acpihid(%s)\n", str);
		return 1;
	}

	p = acpiid;
	hid = strsep(&p, ":");
	uid = p;

	if (!hid || !(*hid) || !uid) {
		pr_err("Invalid command line: hid or uid\n");
		return 1;
	}

	i = early_acpihid_map_size++;
	memcpy(early_acpihid_map[i].hid, hid, strlen(hid));
	memcpy(early_acpihid_map[i].uid, uid, strlen(uid));
	early_acpihid_map[i].devid =
		((bus & 0xff) << 8) | ((dev & 0x1f) << 3) | (fn & 0x7);
	early_acpihid_map[i].cmd_line	= true;

	return 1;
}

__setup("amd_iommu_dump",	parse_amd_iommu_dump);
__setup("amd_iommu=",		parse_amd_iommu_options);
__setup("amd_iommu_intr=",	parse_amd_iommu_intr);
__setup("ivrs_ioapic",		parse_ivrs_ioapic);
__setup("ivrs_hpet",		parse_ivrs_hpet);
__setup("ivrs_acpihid",		parse_ivrs_acpihid);

IOMMU_INIT_FINISH(amd_iommu_detect,
		  gart_iommu_hole_init,
		  NULL,
		  NULL);

bool amd_iommu_v2_supported(void)
{
	return amd_iommu_v2_present;
}
EXPORT_SYMBOL(amd_iommu_v2_supported);

struct amd_iommu *get_amd_iommu(unsigned int idx)
{
	unsigned int i = 0;
	struct amd_iommu *iommu;

	for_each_iommu(iommu)
		if (i++ == idx)
			return iommu;
	return NULL;
}
EXPORT_SYMBOL(get_amd_iommu);

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
EXPORT_SYMBOL(amd_iommu_pc_get_max_banks);

bool amd_iommu_pc_supported(void)
{
	return amd_iommu_pc_present;
}
EXPORT_SYMBOL(amd_iommu_pc_supported);

u8 amd_iommu_pc_get_max_counters(unsigned int idx)
{
	struct amd_iommu *iommu = get_amd_iommu(idx);

	if (iommu)
		return iommu->max_counters;

	return 0;
}
EXPORT_SYMBOL(amd_iommu_pc_get_max_counters);

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
EXPORT_SYMBOL(amd_iommu_pc_get_reg);

int amd_iommu_pc_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn, u64 *value)
{
	if (!iommu)
		return -EINVAL;

	return iommu_pc_get_set_reg(iommu, bank, cntr, fxn, value, true);
}
EXPORT_SYMBOL(amd_iommu_pc_set_reg);
