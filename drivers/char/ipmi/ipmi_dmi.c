// SPDX-License-Identifier: GPL-2.0+
/*
 * A hack to create a platform device from a DMI entry.  This will
 * allow autoloading of the IPMI drive based on SMBIOS entries.
 */

#include <linux/ipmi.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include "ipmi_si_sm.h"
#include "ipmi_dmi.h"

#define IPMI_DMI_TYPE_KCS	0x01
#define IPMI_DMI_TYPE_SMIC	0x02
#define IPMI_DMI_TYPE_BT	0x03
#define IPMI_DMI_TYPE_SSIF	0x04

struct ipmi_dmi_info {
	enum si_type si_type;
	u32 flags;
	unsigned long addr;
	u8 slave_addr;
	struct ipmi_dmi_info *next;
};

static struct ipmi_dmi_info *ipmi_dmi_infos;

static int ipmi_dmi_nr __initdata;

static void __init dmi_add_platform_ipmi(unsigned long base_addr,
					 u32 flags,
					 u8 slave_addr,
					 int irq,
					 int offset,
					 int type)
{
	struct platform_device *pdev;
	struct resource r[4];
	unsigned int num_r = 1, size;
	struct property_entry p[5];
	unsigned int pidx = 0;
	char *name, *override;
	int rv;
	enum si_type si_type;
	struct ipmi_dmi_info *info;

	memset(p, 0, sizeof(p));

	name = "dmi-ipmi-si";
	override = "ipmi_si";
	switch (type) {
	case IPMI_DMI_TYPE_SSIF:
		name = "dmi-ipmi-ssif";
		override = "ipmi_ssif";
		offset = 1;
		size = 1;
		si_type = SI_TYPE_INVALID;
		break;
	case IPMI_DMI_TYPE_BT:
		size = 3;
		si_type = SI_BT;
		break;
	case IPMI_DMI_TYPE_KCS:
		size = 2;
		si_type = SI_KCS;
		break;
	case IPMI_DMI_TYPE_SMIC:
		size = 2;
		si_type = SI_SMIC;
		break;
	default:
		pr_err("ipmi:dmi: Invalid IPMI type: %d\n", type);
		return;
	}

	if (si_type != SI_TYPE_INVALID)
		p[pidx++] = PROPERTY_ENTRY_U8("ipmi-type", si_type);

	p[pidx++] = PROPERTY_ENTRY_U8("slave-addr", slave_addr);
	p[pidx++] = PROPERTY_ENTRY_U8("addr-source", SI_SMBIOS);

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_warn("ipmi:dmi: Could not allocate dmi info\n");
	} else {
		info->si_type = si_type;
		info->flags = flags;
		info->addr = base_addr;
		info->slave_addr = slave_addr;
		info->next = ipmi_dmi_infos;
		ipmi_dmi_infos = info;
	}

	pdev = platform_device_alloc(name, ipmi_dmi_nr);
	if (!pdev) {
		pr_err("ipmi:dmi: Error allocation IPMI platform device\n");
		return;
	}
	pdev->driver_override = kasprintf(GFP_KERNEL, "%s",
					  override);
	if (!pdev->driver_override)
		goto err;

	if (type == IPMI_DMI_TYPE_SSIF) {
		p[pidx++] = PROPERTY_ENTRY_U16("i2c-addr", base_addr);
		goto add_properties;
	}

	memset(r, 0, sizeof(r));

	r[0].start = base_addr;
	r[0].end = r[0].start + offset - 1;
	r[0].name = "IPMI Address 1";
	r[0].flags = flags;

	if (size > 1) {
		r[1].start = r[0].start + offset;
		r[1].end = r[1].start + offset - 1;
		r[1].name = "IPMI Address 2";
		r[1].flags = flags;
		num_r++;
	}

	if (size > 2) {
		r[2].start = r[1].start + offset;
		r[2].end = r[2].start + offset - 1;
		r[2].name = "IPMI Address 3";
		r[2].flags = flags;
		num_r++;
	}

	if (irq) {
		r[num_r].start = irq;
		r[num_r].end = irq;
		r[num_r].name = "IPMI IRQ";
		r[num_r].flags = IORESOURCE_IRQ;
		num_r++;
	}

	rv = platform_device_add_resources(pdev, r, num_r);
	if (rv) {
		dev_err(&pdev->dev,
			"ipmi:dmi: Unable to add resources: %d\n", rv);
		goto err;
	}

add_properties:
	rv = platform_device_add_properties(pdev, p);
	if (rv) {
		dev_err(&pdev->dev,
			"ipmi:dmi: Unable to add properties: %d\n", rv);
		goto err;
	}

	rv = platform_device_add(pdev);
	if (rv) {
		dev_err(&pdev->dev, "ipmi:dmi: Unable to add device: %d\n", rv);
		goto err;
	}

	ipmi_dmi_nr++;
	return;

err:
	platform_device_put(pdev);
}

/*
 * Look up the slave address for a given interface.  This is here
 * because ACPI doesn't have a slave address while SMBIOS does, but we
 * prefer using ACPI so the ACPI code can use the IPMI namespace.
 * This function allows an ACPI-specified IPMI device to look up the
 * slave address from the DMI table.
 */
int ipmi_dmi_get_slave_addr(enum si_type si_type, u32 flags,
			    unsigned long base_addr)
{
	struct ipmi_dmi_info *info = ipmi_dmi_infos;

	while (info) {
		if (info->si_type == si_type &&
		    info->flags == flags &&
		    info->addr == base_addr)
			return info->slave_addr;
		info = info->next;
	}

	return 0;
}
EXPORT_SYMBOL(ipmi_dmi_get_slave_addr);

#define DMI_IPMI_MIN_LENGTH	0x10
#define DMI_IPMI_VER2_LENGTH	0x12
#define DMI_IPMI_TYPE		4
#define DMI_IPMI_SLAVEADDR	6
#define DMI_IPMI_ADDR		8
#define DMI_IPMI_ACCESS		0x10
#define DMI_IPMI_IRQ		0x11
#define DMI_IPMI_IO_MASK	0xfffe

static void __init dmi_decode_ipmi(const struct dmi_header *dm)
{
	const u8	*data = (const u8 *) dm;
	u32             flags = IORESOURCE_IO;
	unsigned long	base_addr;
	u8              len = dm->length;
	u8              slave_addr;
	int             irq = 0, offset;
	int             type;

	if (len < DMI_IPMI_MIN_LENGTH)
		return;

	type = data[DMI_IPMI_TYPE];
	slave_addr = data[DMI_IPMI_SLAVEADDR];

	memcpy(&base_addr, data + DMI_IPMI_ADDR, sizeof(unsigned long));
	if (!base_addr) {
		pr_err("Base address is zero, assuming no IPMI interface\n");
		return;
	}
	if (len >= DMI_IPMI_VER2_LENGTH) {
		if (type == IPMI_DMI_TYPE_SSIF) {
			offset = 0;
			flags = 0;
			base_addr = data[DMI_IPMI_ADDR] >> 1;
			if (base_addr == 0) {
				/*
				 * Some broken systems put the I2C address in
				 * the slave address field.  We try to
				 * accommodate them here.
				 */
				base_addr = data[DMI_IPMI_SLAVEADDR] >> 1;
				slave_addr = 0;
			}
		} else {
			if (base_addr & 1) {
				/* I/O */
				base_addr &= DMI_IPMI_IO_MASK;
			} else {
				/* Memory */
				flags = IORESOURCE_MEM;
			}

			/*
			 * If bit 4 of byte 0x10 is set, then the lsb
			 * for the address is odd.
			 */
			base_addr |= (data[DMI_IPMI_ACCESS] >> 4) & 1;

			irq = data[DMI_IPMI_IRQ];

			/*
			 * The top two bits of byte 0x10 hold the
			 * register spacing.
			 */
			switch ((data[DMI_IPMI_ACCESS] >> 6) & 3) {
			case 0: /* Byte boundaries */
				offset = 1;
				break;
			case 1: /* 32-bit boundaries */
				offset = 4;
				break;
			case 2: /* 16-byte boundaries */
				offset = 16;
				break;
			default:
				pr_err("ipmi:dmi: Invalid offset: 0\n");
				return;
			}
		}
	} else {
		/* Old DMI spec. */
		/*
		 * Note that technically, the lower bit of the base
		 * address should be 1 if the address is I/O and 0 if
		 * the address is in memory.  So many systems get that
		 * wrong (and all that I have seen are I/O) so we just
		 * ignore that bit and assume I/O.  Systems that use
		 * memory should use the newer spec, anyway.
		 */
		base_addr = base_addr & DMI_IPMI_IO_MASK;
		offset = 1;
	}

	dmi_add_platform_ipmi(base_addr, flags, slave_addr, irq,
			      offset, type);
}

static int __init scan_for_dmi_ipmi(void)
{
	const struct dmi_device *dev = NULL;

	while ((dev = dmi_find_device(DMI_DEV_TYPE_IPMI, NULL, dev)))
		dmi_decode_ipmi((const struct dmi_header *) dev->device_data);

	return 0;
}
subsys_initcall(scan_for_dmi_ipmi);
