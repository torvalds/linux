/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
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
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <asm/pci-direct.h>
#include <asm/amd_iommu_types.h>
#include <asm/amd_iommu.h>
#include <asm/iommu.h>
#include <asm/gart.h>

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

#define IVHD_FLAG_HT_TUN_EN             0x00
#define IVHD_FLAG_PASSPW_EN             0x01
#define IVHD_FLAG_RESPASSPW_EN          0x02
#define IVHD_FLAG_ISOC_EN               0x03

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

static int __initdata amd_iommu_detected;

u16 amd_iommu_last_bdf;			/* largest PCI device id we have
					   to handle */
LIST_HEAD(amd_iommu_unity_map);		/* a list of required unity mappings
					   we find in ACPI */
unsigned amd_iommu_aperture_order = 26; /* size of aperture in power of 2 */
bool amd_iommu_isolate = true;		/* if true, device isolation is
					   enabled */
bool amd_iommu_unmap_flush;		/* if true, flush on every unmap */

LIST_HEAD(amd_iommu_list);		/* list of all AMD IOMMUs in the
					   system */

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
 * The pd table (protection domain table) is used to find the protection domain
 * data structure a device belongs to. Indexed with the PCI device id too.
 */
struct protection_domain **amd_iommu_pd_table;

/*
 * AMD IOMMU allows up to 2^16 differend protection domains. This is a bitmap
 * to know which ones are already in use.
 */
unsigned long *amd_iommu_pd_alloc_bitmap;

static u32 dev_table_size;	/* size of the device table */
static u32 alias_table_size;	/* size of the alias table */
static u32 rlookup_table_size;	/* size if the rlookup table */

static inline void update_last_devid(u16 devid)
{
	if (devid > amd_iommu_last_bdf)
		amd_iommu_last_bdf = devid;
}

static inline unsigned long tbl_size(int entry_size)
{
	unsigned shift = PAGE_SHIFT +
			 get_order(amd_iommu_last_bdf * entry_size);

	return 1UL << shift;
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
static void __init iommu_set_exclusion_range(struct amd_iommu *iommu)
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
static void __init iommu_set_device_table(struct amd_iommu *iommu)
{
	u64 entry;

	BUG_ON(iommu->mmio_base == NULL);

	entry = virt_to_phys(amd_iommu_dev_table);
	entry |= (dev_table_size >> 12) - 1;
	memcpy_toio(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET,
			&entry, sizeof(entry));
}

/* Generic functions to enable/disable certain features of the IOMMU. */
static void __init iommu_feature_enable(struct amd_iommu *iommu, u8 bit)
{
	u32 ctrl;

	ctrl = readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl |= (1 << bit);
	writel(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

static void __init iommu_feature_disable(struct amd_iommu *iommu, u8 bit)
{
	u32 ctrl;

	ctrl = readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl &= ~(1 << bit);
	writel(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

/* Function to enable the hardware */
static void __init iommu_enable(struct amd_iommu *iommu)
{
	printk(KERN_INFO "AMD IOMMU: Enabling IOMMU at %s cap 0x%hx\n",
	       dev_name(&iommu->dev->dev), iommu->cap_ptr);

	iommu_feature_enable(iommu, CONTROL_IOMMU_EN);
}

/* Function to enable IOMMU event logging and event interrupts */
static void __init iommu_enable_event_logging(struct amd_iommu *iommu)
{
	iommu_feature_enable(iommu, CONTROL_EVT_LOG_EN);
	iommu_feature_enable(iommu, CONTROL_EVT_INT_EN);
}

/*
 * mapping and unmapping functions for the IOMMU MMIO space. Each AMD IOMMU in
 * the system has one.
 */
static u8 * __init iommu_map_mmio_space(u64 address)
{
	u8 *ret;

	if (!request_mem_region(address, MMIO_REGION_LENGTH, "amd_iommu"))
		return NULL;

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
	if (checksum != 0)
		/* ACPI table corrupt */
		return -ENODEV;

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
	u64 entry;

	if (cmd_buf == NULL)
		return NULL;

	iommu->cmd_buf_size = CMD_BUFFER_SIZE;

	entry = (u64)virt_to_phys(cmd_buf);
	entry |= MMIO_CMD_SIZE_512;
	memcpy_toio(iommu->mmio_base + MMIO_CMD_BUF_OFFSET,
			&entry, sizeof(entry));

	/* set head and tail to zero manually */
	writel(0x00, iommu->mmio_base + MMIO_CMD_HEAD_OFFSET);
	writel(0x00, iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);

	iommu_feature_enable(iommu, CONTROL_CMDBUF_EN);

	return cmd_buf;
}

static void __init free_command_buffer(struct amd_iommu *iommu)
{
	free_pages((unsigned long)iommu->cmd_buf,
		   get_order(iommu->cmd_buf_size));
}

/* allocates the memory where the IOMMU will log its events to */
static u8 * __init alloc_event_buffer(struct amd_iommu *iommu)
{
	u64 entry;
	iommu->evt_buf = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						get_order(EVT_BUFFER_SIZE));

	if (iommu->evt_buf == NULL)
		return NULL;

	entry = (u64)virt_to_phys(iommu->evt_buf) | EVT_LEN_MASK;
	memcpy_toio(iommu->mmio_base + MMIO_EVT_BUF_OFFSET,
		    &entry, sizeof(entry));

	iommu->evt_buf_size = EVT_BUFFER_SIZE;

	return iommu->evt_buf;
}

static void __init free_event_buffer(struct amd_iommu *iommu)
{
	free_pages((unsigned long)iommu->evt_buf, get_order(EVT_BUFFER_SIZE));
}

/* sets a specific bit in the device table entry. */
static void set_dev_entry_bit(u16 devid, u8 bit)
{
	int i = (bit >> 5) & 0x07;
	int _bit = bit & 0x1f;

	amd_iommu_dev_table[devid].data[i] |= (1 << _bit);
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
	u32 range, misc;

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
	u16 dev_i, devid = 0, devid_start = 0, devid_to = 0;
	u32 ext_flags = 0;
	bool alias = false;
	struct ivhd_entry *e;

	/*
	 * First set the recommended feature enable bits from ACPI
	 * into the IOMMU control registers
	 */
	h->flags & IVHD_FLAG_HT_TUN_EN ?
		iommu_feature_enable(iommu, CONTROL_HT_TUN_EN) :
		iommu_feature_disable(iommu, CONTROL_HT_TUN_EN);

	h->flags & IVHD_FLAG_PASSPW_EN ?
		iommu_feature_enable(iommu, CONTROL_PASSPW_EN) :
		iommu_feature_disable(iommu, CONTROL_PASSPW_EN);

	h->flags & IVHD_FLAG_RESPASSPW_EN ?
		iommu_feature_enable(iommu, CONTROL_RESPASSPW_EN) :
		iommu_feature_disable(iommu, CONTROL_RESPASSPW_EN);

	h->flags & IVHD_FLAG_ISOC_EN ?
		iommu_feature_enable(iommu, CONTROL_ISOC_EN) :
		iommu_feature_disable(iommu, CONTROL_ISOC_EN);

	/*
	 * make IOMMU memory accesses cache coherent
	 */
	iommu_feature_enable(iommu, CONTROL_COHERENT_EN);

	/*
	 * Done. Now parse the device entries
	 */
	p += sizeof(struct ivhd_header);
	end += h->length;

	while (p < end) {
		e = (struct ivhd_entry *)p;
		switch (e->type) {
		case IVHD_DEV_ALL:
			for (dev_i = iommu->first_device;
					dev_i <= iommu->last_device; ++dev_i)
				set_dev_entry_from_acpi(iommu, dev_i,
							e->flags, 0);
			break;
		case IVHD_DEV_SELECT:
			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);
			break;
		case IVHD_DEV_SELECT_RANGE_START:
			devid_start = e->devid;
			flags = e->flags;
			ext_flags = 0;
			alias = false;
			break;
		case IVHD_DEV_ALIAS:
			devid = e->devid;
			devid_to = e->ext >> 8;
			set_dev_entry_from_acpi(iommu, devid, e->flags, 0);
			amd_iommu_alias_table[devid] = devid_to;
			break;
		case IVHD_DEV_ALIAS_RANGE:
			devid_start = e->devid;
			flags = e->flags;
			devid_to = e->ext >> 8;
			ext_flags = 0;
			alias = true;
			break;
		case IVHD_DEV_EXT_SELECT:
			devid = e->devid;
			set_dev_entry_from_acpi(iommu, devid, e->flags,
						e->ext);
			break;
		case IVHD_DEV_EXT_SELECT_RANGE:
			devid_start = e->devid;
			flags = e->flags;
			ext_flags = e->ext;
			alias = false;
			break;
		case IVHD_DEV_RANGE_END:
			devid = e->devid;
			for (dev_i = devid_start; dev_i <= devid; ++dev_i) {
				if (alias)
					amd_iommu_alias_table[dev_i] = devid_to;
				set_dev_entry_from_acpi(iommu,
						amd_iommu_alias_table[dev_i],
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
	u16 i;

	for (i = iommu->first_device; i <= iommu->last_device; ++i)
		set_iommu_for_device(iommu, i);

	return 0;
}

static void __init free_iommu_one(struct amd_iommu *iommu)
{
	free_command_buffer(iommu);
	free_event_buffer(iommu);
	iommu_unmap_mmio_space(iommu);
}

static void __init free_iommu_all(void)
{
	struct amd_iommu *iommu, *next;

	list_for_each_entry_safe(iommu, next, &amd_iommu_list, list) {
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
	list_add_tail(&iommu->list, &amd_iommu_list);

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

	iommu_set_device_table(iommu);
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
			iommu = kzalloc(sizeof(struct amd_iommu), GFP_KERNEL);
			if (iommu == NULL)
				return -ENOMEM;
			ret = init_iommu_one(iommu, h);
			if (ret)
				return ret;
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

static int __init iommu_setup_msix(struct amd_iommu *iommu)
{
	struct amd_iommu *curr;
	struct msix_entry entries[32]; /* only 32 supported by AMD IOMMU */
	int nvec = 0, i;

	list_for_each_entry(curr, &amd_iommu_list, list) {
		if (curr->dev == iommu->dev) {
			entries[nvec].entry = curr->evt_msi_num;
			entries[nvec].vector = 0;
			curr->int_enabled = true;
			nvec++;
		}
	}

	if (pci_enable_msix(iommu->dev, entries, nvec)) {
		pci_disable_msix(iommu->dev);
		return 1;
	}

	for (i = 0; i < nvec; ++i) {
		int r = request_irq(entries->vector, amd_iommu_int_handler,
				    IRQF_SAMPLE_RANDOM,
				    "AMD IOMMU",
				    NULL);
		if (r)
			goto out_free;
	}

	return 0;

out_free:
	for (i -= 1; i >= 0; --i)
		free_irq(entries->vector, NULL);

	pci_disable_msix(iommu->dev);

	return 1;
}

static int __init iommu_setup_msi(struct amd_iommu *iommu)
{
	int r;
	struct amd_iommu *curr;

	list_for_each_entry(curr, &amd_iommu_list, list) {
		if (curr->dev == iommu->dev)
			curr->int_enabled = true;
	}


	if (pci_enable_msi(iommu->dev))
		return 1;

	r = request_irq(iommu->dev->irq, amd_iommu_int_handler,
			IRQF_SAMPLE_RANDOM,
			"AMD IOMMU",
			NULL);

	if (r) {
		pci_disable_msi(iommu->dev);
		return 1;
	}

	return 0;
}

static int __init iommu_init_msi(struct amd_iommu *iommu)
{
	if (iommu->int_enabled)
		return 0;

	if (pci_find_capability(iommu->dev, PCI_CAP_ID_MSIX))
		return iommu_setup_msix(iommu);
	else if (pci_find_capability(iommu->dev, PCI_CAP_ID_MSI))
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

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	switch (m->type) {
	default:
	case ACPI_IVMD_TYPE:
		e->devid_start = e->devid_end = m->devid;
		break;
	case ACPI_IVMD_TYPE_ALL:
		e->devid_start = 0;
		e->devid_end = amd_iommu_last_bdf;
		break;
	case ACPI_IVMD_TYPE_RANGE:
		e->devid_start = m->devid;
		e->devid_end = m->aux;
		break;
	}
	e->address_start = PAGE_ALIGN(m->range_start);
	e->address_end = e->address_start + PAGE_ALIGN(m->range_length);
	e->prot = m->flags >> 1;

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
	u16 devid;

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid) {
		set_dev_entry_bit(devid, DEV_ENTRY_VALID);
		set_dev_entry_bit(devid, DEV_ENTRY_TRANSLATION);
	}
}

/*
 * This function finally enables all IOMMUs found in the system after
 * they have been initialized
 */
static void __init enable_iommus(void)
{
	struct amd_iommu *iommu;

	list_for_each_entry(iommu, &amd_iommu_list, list) {
		iommu_set_exclusion_range(iommu);
		iommu_init_msi(iommu);
		iommu_enable_event_logging(iommu);
		iommu_enable(iommu);
	}
}

/*
 * Suspend/Resume support
 * disable suspend until real resume implemented
 */

static int amd_iommu_resume(struct sys_device *dev)
{
	return 0;
}

static int amd_iommu_suspend(struct sys_device *dev, pm_message_t state)
{
	return -EINVAL;
}

static struct sysdev_class amd_iommu_sysdev_class = {
	.name = "amd_iommu",
	.suspend = amd_iommu_suspend,
	.resume = amd_iommu_resume,
};

static struct sys_device device_amd_iommu = {
	.id = 0,
	.cls = &amd_iommu_sysdev_class,
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
int __init amd_iommu_init(void)
{
	int i, ret = 0;


	if (no_iommu) {
		printk(KERN_INFO "AMD IOMMU disabled by kernel command line\n");
		return 0;
	}

	if (!amd_iommu_detected)
		return -ENODEV;

	/*
	 * First parse ACPI tables to find the largest Bus/Dev/Func
	 * we need to handle. Upon this information the shared data
	 * structures for the IOMMUs in the system will be allocated
	 */
	if (acpi_table_parse("IVRS", find_last_devid_acpi) != 0)
		return -ENODEV;

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

	/*
	 * Protection Domain table - maps devices to protection domains
	 * This table has the same size as the rlookup_table
	 */
	amd_iommu_pd_table = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
				     get_order(rlookup_table_size));
	if (amd_iommu_pd_table == NULL)
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

	/*
	 * now the data structures are allocated and basically initialized
	 * start the real acpi table scan
	 */
	ret = -ENODEV;
	if (acpi_table_parse("IVRS", init_iommu_all) != 0)
		goto free;

	if (acpi_table_parse("IVRS", init_memory_definitions) != 0)
		goto free;

	ret = sysdev_class_register(&amd_iommu_sysdev_class);
	if (ret)
		goto free;

	ret = sysdev_register(&device_amd_iommu);
	if (ret)
		goto free;

	ret = amd_iommu_init_dma_ops();
	if (ret)
		goto free;

	enable_iommus();

	printk(KERN_INFO "AMD IOMMU: aperture size is %d MB\n",
			(1 << (amd_iommu_aperture_order-20)));

	printk(KERN_INFO "AMD IOMMU: device isolation ");
	if (amd_iommu_isolate)
		printk("enabled\n");
	else
		printk("disabled\n");

	if (amd_iommu_unmap_flush)
		printk(KERN_INFO "AMD IOMMU: IO/TLB flush on unmap enabled\n");
	else
		printk(KERN_INFO "AMD IOMMU: Lazy IO/TLB flushing enabled\n");

out:
	return ret;

free:
	free_pages((unsigned long)amd_iommu_pd_alloc_bitmap,
		   get_order(MAX_DOMAIN_ID/8));

	free_pages((unsigned long)amd_iommu_pd_table,
		   get_order(rlookup_table_size));

	free_pages((unsigned long)amd_iommu_rlookup_table,
		   get_order(rlookup_table_size));

	free_pages((unsigned long)amd_iommu_alias_table,
		   get_order(alias_table_size));

	free_pages((unsigned long)amd_iommu_dev_table,
		   get_order(dev_table_size));

	free_iommu_all();

	free_unity_maps();

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

void __init amd_iommu_detect(void)
{
	if (swiotlb || no_iommu || (iommu_detected && !gart_iommu_aperture))
		return;

	if (acpi_table_parse("IVRS", early_amd_iommu_detect) == 0) {
		iommu_detected = 1;
		amd_iommu_detected = 1;
#ifdef CONFIG_GART_IOMMU
		gart_iommu_aperture_disabled = 1;
		gart_iommu_aperture = 0;
#endif
	}
}

/****************************************************************************
 *
 * Parsing functions for the AMD IOMMU specific kernel command line
 * options.
 *
 ****************************************************************************/

static int __init parse_amd_iommu_options(char *str)
{
	for (; *str; ++str) {
		if (strncmp(str, "isolate", 7) == 0)
			amd_iommu_isolate = true;
		if (strncmp(str, "share", 5) == 0)
			amd_iommu_isolate = false;
		if (strncmp(str, "fullflush", 9) == 0)
			amd_iommu_unmap_flush = true;
	}

	return 1;
}

static int __init parse_amd_iommu_size_options(char *str)
{
	unsigned order = PAGE_SHIFT + get_order(memparse(str, &str));

	if ((order > 24) && (order < 31))
		amd_iommu_aperture_order = order;

	return 1;
}

__setup("amd_iommu=", parse_amd_iommu_options);
__setup("amd_iommu_size=", parse_amd_iommu_size_options);
