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

