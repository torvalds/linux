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
#include <asm/pci-direct.h>
#include <asm/amd_iommu_types.h>
#include <asm/amd_iommu.h>
#include <asm/gart.h>

/*
 * definitions for the ACPI scanning code
 */
#define UPDATE_LAST_BDF(x) do {\
	if ((x) > amd_iommu_last_bdf) \
		amd_iommu_last_bdf = (x); \
	} while (0);

#define DEVID(bus, devfn) (((bus) << 8) | (devfn))
#define PCI_BUS(x) (((x) >> 8) & 0xff)
#define IVRS_HEADER_LENGTH 48
#define TBL_SIZE(x) (1 << (PAGE_SHIFT + get_order(amd_iommu_last_bdf * (x))))

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

struct ivhd_entry {
	u8 type;
	u16 devid;
	u8 flags;
	u32 ext;
} __attribute__((packed));

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

static int __initdata amd_iommu_disable;

u16 amd_iommu_last_bdf;
struct list_head amd_iommu_unity_map;
unsigned amd_iommu_aperture_order = 26;
int amd_iommu_isolate;

struct list_head amd_iommu_list;
struct dev_table_entry *amd_iommu_dev_table;
u16 *amd_iommu_alias_table;
struct amd_iommu **amd_iommu_rlookup_table;
struct protection_domain **amd_iommu_pd_table;
unsigned long *amd_iommu_pd_alloc_bitmap;

static u32 dev_table_size;
static u32 alias_table_size;
static u32 rlookup_table_size;

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

static void __init iommu_set_device_table(struct amd_iommu *iommu)
{
	u32 entry;

	BUG_ON(iommu->mmio_base == NULL);

	entry = virt_to_phys(amd_iommu_dev_table);
	entry |= (dev_table_size >> 12) - 1;
	memcpy_toio(iommu->mmio_base + MMIO_DEV_TABLE_OFFSET,
			&entry, sizeof(entry));
}

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

	ctrl = (u64)readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
	ctrl &= ~(1 << bit);
	writel(ctrl, iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

void __init iommu_enable(struct amd_iommu *iommu)
{
	u32 ctrl;

	printk(KERN_INFO "AMD IOMMU: Enabling IOMMU at ");
	print_devid(iommu->devid, 0);
	printk(" cap 0x%hx\n", iommu->cap_ptr);

	iommu_feature_enable(iommu, CONTROL_IOMMU_EN);
	ctrl = readl(iommu->mmio_base + MMIO_CONTROL_OFFSET);
}

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

static int __init find_last_devid_on_pci(int bus, int dev, int fn, int cap_ptr)
{
	u32 cap;

	cap = read_pci_config(bus, dev, fn, cap_ptr+MMIO_RANGE_OFFSET);
	UPDATE_LAST_BDF(DEVID(MMIO_GET_BUS(cap), MMIO_GET_LD(cap)));

	return 0;
}

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
			UPDATE_LAST_BDF(dev->devid);
			break;
		default:
			break;
		}
		p += 0x04 << (*p >> 6);
	}

	WARN_ON(p != end);

	return 0;
}

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

static u8 * __init alloc_command_buffer(struct amd_iommu *iommu)
{
	u8 *cmd_buf = (u8 *)__get_free_pages(GFP_KERNEL,
			get_order(CMD_BUFFER_SIZE));
	u64 entry = 0;

	if (cmd_buf == NULL)
		return NULL;

	iommu->cmd_buf_size = CMD_BUFFER_SIZE;

	memset(cmd_buf, 0, CMD_BUFFER_SIZE);

	entry = (u64)virt_to_phys(cmd_buf);
	entry |= MMIO_CMD_SIZE_512;
	memcpy_toio(iommu->mmio_base + MMIO_CMD_BUF_OFFSET,
			&entry, sizeof(entry));

	iommu_feature_enable(iommu, CONTROL_CMDBUF_EN);

	return cmd_buf;
}

static void __init free_command_buffer(struct amd_iommu *iommu)
{
	if (iommu->cmd_buf)
		free_pages((unsigned long)iommu->cmd_buf,
				get_order(CMD_BUFFER_SIZE));
}

static void set_dev_entry_bit(u16 devid, u8 bit)
{
	int i = (bit >> 5) & 0x07;
	int _bit = bit & 0x1f;

	amd_iommu_dev_table[devid].data[i] |= (1 << _bit);
}

static void __init set_dev_entry_from_acpi(u16 devid, u32 flags, u32 ext_flags)
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
}

static void __init set_iommu_for_device(struct amd_iommu *iommu, u16 devid)
{
	amd_iommu_rlookup_table[devid] = iommu;
}

static void __init set_device_exclusion_range(u16 devid, struct ivmd_header *m)
{
	struct amd_iommu *iommu = amd_iommu_rlookup_table[devid];

	if (!(m->flags & IVMD_FLAG_EXCL_RANGE))
		return;

	if (iommu) {
		set_dev_entry_bit(m->devid, DEV_ENTRY_EX);
		iommu->exclusion_start = m->range_start;
		iommu->exclusion_length = m->range_length;
	}
}

static void __init init_iommu_from_pci(struct amd_iommu *iommu)
{
	int bus = PCI_BUS(iommu->devid);
	int dev = PCI_SLOT(iommu->devid);
	int fn  = PCI_FUNC(iommu->devid);
	int cap_ptr = iommu->cap_ptr;
	u32 range;

	iommu->cap = read_pci_config(bus, dev, fn, cap_ptr+MMIO_CAP_HDR_OFFSET);

	range = read_pci_config(bus, dev, fn, cap_ptr+MMIO_RANGE_OFFSET);
	iommu->first_device = DEVID(MMIO_GET_BUS(range), MMIO_GET_FD(range));
	iommu->last_device = DEVID(MMIO_GET_BUS(range), MMIO_GET_LD(range));
}

static void __init init_iommu_from_acpi(struct amd_iommu *iommu,
					struct ivhd_header *h)
{
	u8 *p = (u8 *)h;
	u8 *end = p, flags = 0;
	u16 dev_i, devid = 0, devid_start = 0, devid_to = 0;
	u32 ext_flags = 0;
	bool alias = 0;
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
				set_dev_entry_from_acpi(dev_i, e->flags, 0);
			break;
		case IVHD_DEV_SELECT:
			devid = e->devid;
			set_dev_entry_from_acpi(devid, e->flags, 0);
			break;
		case IVHD_DEV_SELECT_RANGE_START:
			devid_start = e->devid;
			flags = e->flags;
			ext_flags = 0;
			alias = 0;
			break;
		case IVHD_DEV_ALIAS:
			devid = e->devid;
			devid_to = e->ext >> 8;
			set_dev_entry_from_acpi(devid, e->flags, 0);
			amd_iommu_alias_table[devid] = devid_to;
			break;
		case IVHD_DEV_ALIAS_RANGE:
			devid_start = e->devid;
			flags = e->flags;
			devid_to = e->ext >> 8;
			ext_flags = 0;
			alias = 1;
			break;
		case IVHD_DEV_EXT_SELECT:
			devid = e->devid;
			set_dev_entry_from_acpi(devid, e->flags, e->ext);
			break;
		case IVHD_DEV_EXT_SELECT_RANGE:
			devid_start = e->devid;
			flags = e->flags;
			ext_flags = e->ext;
			alias = 0;
			break;
		case IVHD_DEV_RANGE_END:
			devid = e->devid;
			for (dev_i = devid_start; dev_i <= devid; ++dev_i) {
				if (alias)
					amd_iommu_alias_table[dev_i] = devid_to;
				set_dev_entry_from_acpi(
						amd_iommu_alias_table[dev_i],
						flags, ext_flags);
			}
			break;
		default:
			break;
		}

		p += 0x04 << (e->type >> 6);
	}
}

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

static int __init init_iommu_one(struct amd_iommu *iommu, struct ivhd_header *h)
{
	spin_lock_init(&iommu->lock);
	list_add_tail(&iommu->list, &amd_iommu_list);

	/*
	 * Copy data from ACPI table entry to the iommu struct
	 */
	iommu->devid = h->devid;
	iommu->cap_ptr = h->cap_ptr;
	iommu->mmio_phys = h->mmio_phys;
	iommu->mmio_base = iommu_map_mmio_space(h->mmio_phys);
	if (!iommu->mmio_base)
		return -ENOMEM;

	iommu_set_device_table(iommu);
	iommu->cmd_buf = alloc_command_buffer(iommu);
	if (!iommu->cmd_buf)
		return -ENOMEM;

	init_iommu_from_pci(iommu);
	init_iommu_from_acpi(iommu, h);
	init_iommu_devices(iommu);

	return 0;
}

static int __init init_iommu_all(struct acpi_table_header *table)
{
	u8 *p = (u8 *)table, *end = (u8 *)table;
	struct ivhd_header *h;
	struct amd_iommu *iommu;
	int ret;

	INIT_LIST_HEAD(&amd_iommu_list);

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

static void __init free_unity_maps(void)
{
	struct unity_map_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &amd_iommu_unity_map, list) {
		list_del(&entry->list);
		kfree(entry);
	}
}

static int __init init_exclusion_range(struct ivmd_header *m)
{
	int i;

	switch (m->type) {
	case ACPI_IVMD_TYPE:
		set_device_exclusion_range(m->devid, m);
		break;
	case ACPI_IVMD_TYPE_ALL:
		for (i = 0; i < amd_iommu_last_bdf; ++i)
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

static int __init init_memory_definitions(struct acpi_table_header *table)
{
	u8 *p = (u8 *)table, *end = (u8 *)table;
	struct ivmd_header *m;

	INIT_LIST_HEAD(&amd_iommu_unity_map);

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

static void __init enable_iommus(void)
{
	struct amd_iommu *iommu;

	list_for_each_entry(iommu, &amd_iommu_list, list) {
		iommu_set_exclusion_range(iommu);
		iommu_enable(iommu);
	}
}

int __init amd_iommu_init(void)
{
	int i, ret = 0;


	if (amd_iommu_disable) {
		printk(KERN_INFO "AMD IOMMU disabled by kernel command line\n");
		return 0;
	}

	/*
	 * First parse ACPI tables to find the largest Bus/Dev/Func
	 * we need to handle. Upon this information the shared data
	 * structures for the IOMMUs in the system will be allocated
	 */
	if (acpi_table_parse("IVRS", find_last_devid_acpi) != 0)
		return -ENODEV;

	dev_table_size     = TBL_SIZE(DEV_TABLE_ENTRY_SIZE);
	alias_table_size   = TBL_SIZE(ALIAS_TABLE_ENTRY_SIZE);
	rlookup_table_size = TBL_SIZE(RLOOKUP_TABLE_ENTRY_SIZE);

	ret = -ENOMEM;

	/* Device table - directly used by all IOMMUs */
	amd_iommu_dev_table = (void *)__get_free_pages(GFP_KERNEL,
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
	amd_iommu_rlookup_table = (void *)__get_free_pages(GFP_KERNEL,
			get_order(rlookup_table_size));
	if (amd_iommu_rlookup_table == NULL)
		goto free;

	/*
	 * Protection Domain table - maps devices to protection domains
	 * This table has the same size as the rlookup_table
	 */
	amd_iommu_pd_table = (void *)__get_free_pages(GFP_KERNEL,
				     get_order(rlookup_table_size));
	if (amd_iommu_pd_table == NULL)
		goto free;

	amd_iommu_pd_alloc_bitmap = (void *)__get_free_pages(GFP_KERNEL,
					    get_order(MAX_DOMAIN_ID/8));
	if (amd_iommu_pd_alloc_bitmap == NULL)
		goto free;

	/*
	 * memory is allocated now; initialize the device table with all zeroes
	 * and let all alias entries point to itself
	 */
	memset(amd_iommu_dev_table, 0, dev_table_size);
	for (i = 0; i < amd_iommu_last_bdf; ++i)
		amd_iommu_alias_table[i] = i;

	memset(amd_iommu_pd_table, 0, rlookup_table_size);
	memset(amd_iommu_pd_alloc_bitmap, 0, MAX_DOMAIN_ID / 8);

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

out:
	return ret;

free:
	if (amd_iommu_pd_alloc_bitmap)
		free_pages((unsigned long)amd_iommu_pd_alloc_bitmap, 1);

	if (amd_iommu_pd_table)
		free_pages((unsigned long)amd_iommu_pd_table,
				get_order(rlookup_table_size));

	if (amd_iommu_rlookup_table)
		free_pages((unsigned long)amd_iommu_rlookup_table,
				get_order(rlookup_table_size));

	if (amd_iommu_alias_table)
		free_pages((unsigned long)amd_iommu_alias_table,
				get_order(alias_table_size));

	if (amd_iommu_dev_table)
		free_pages((unsigned long)amd_iommu_dev_table,
				get_order(dev_table_size));

	free_iommu_all();

	free_unity_maps();

	goto out;
}

static int __init early_amd_iommu_detect(struct acpi_table_header *table)
{
	return 0;
}

void __init amd_iommu_detect(void)
{
	if (swiotlb || no_iommu || iommu_detected)
		return;

	if (amd_iommu_disable)
		return;

	if (acpi_table_parse("IVRS", early_amd_iommu_detect) == 0) {
		iommu_detected = 1;
#ifdef CONFIG_GART_IOMMU
		gart_iommu_aperture_disabled = 1;
		gart_iommu_aperture = 0;
#endif
	}
}

static int __init parse_amd_iommu_options(char *str)
{
	for (; *str; ++str) {
		if (strcmp(str, "off") == 0)
			amd_iommu_disable = 1;
		if (strcmp(str, "isolate") == 0)
			amd_iommu_isolate = 1;
	}

	return 1;
}

static int __init parse_amd_iommu_size_options(char *str)
{
	for (; *str; ++str) {
		if (strcmp(str, "32M") == 0)
			amd_iommu_aperture_order = 25;
		if (strcmp(str, "64M") == 0)
			amd_iommu_aperture_order = 26;
		if (strcmp(str, "128M") == 0)
			amd_iommu_aperture_order = 27;
		if (strcmp(str, "256M") == 0)
			amd_iommu_aperture_order = 28;
		if (strcmp(str, "512M") == 0)
			amd_iommu_aperture_order = 29;
		if (strcmp(str, "1G") == 0)
			amd_iommu_aperture_order = 30;
	}

	return 1;
}

__setup("amd_iommu=", parse_amd_iommu_options);
__setup("amd_iommu_size=", parse_amd_iommu_size_options);
