// SPDX-License-Identifier: GPL-2.0+
/*
 * A hack to create a platform device from a DMI entry.  This will
 * allow autoloading of the IPMI drive based on SMBIOS entries.
 */

#define pr_fmt(fmt) "%s" fmt, "ipmi:dmi: "
#define dev_fmt pr_fmt

#include <linux/ipmi.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include "ipmi_dmi.h"
#include "ipmi_plat_data.h"

#define IPMI_DMI_TYPE_KCS	0x01
#define IPMI_DMI_TYPE_SMIC	0x02
#define IPMI_DMI_TYPE_BT	0x03
#define IPMI_DMI_TYPE_SSIF	0x04

struct ipmi_dmi_info {
	enum si_type si_type;
	unsigned int space; /* addr space for si, intf# for ssif */
	unsigned long addr;
	u8 slave_addr;
	struct ipmi_dmi_info *next;
};

static struct ipmi_dmi_info *ipmi_dmi_infos;

static int ipmi_dmi_nr __initdata;

static void __init dmi_add_platform_ipmi(unsigned long base_addr,
					 unsigned int space,
					 u8 slave_addr,
					 int irq,
					 int offset,
					 int type)
{
	const char *name;
	struct ipmi_dmi_info *info;
	struct ipmi_plat_data p;

	memset(&p, 0, sizeof(p));

	name = "dmi-ipmi-si";
	p.iftype = IPMI_PLAT_IF_SI;
	switch (type) {
	case IPMI_DMI_TYPE_SSIF:
		name = "dmi-ipmi-ssif";
		p.iftype = IPMI_PLAT_IF_SSIF;
		p.type = SI_TYPE_INVALID;
		break;
	case IPMI_DMI_TYPE_BT:
		p.type = SI_BT;
		break;
	case IPMI_DMI_TYPE_KCS:
		p.type = SI_KCS;
		break;
	case IPMI_DMI_TYPE_SMIC:
		p.type = SI_SMIC;
		break;
	default:
		pr_err("Invalid IPMI type: %d\n", type);
		return;
	}

	p.addr = base_addr;
	p.space = space;
	p.regspacing = offset;
	p.irq = irq;
	p.slave_addr = slave_addr;
	p.addr_source = SI_SMBIOS;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_warn("Could not allocate dmi info\n");
	} else {
		info->si_type = p.type;
		info->space = space;
		info->addr = base_addr;
		info->slave_addr = slave_addr;
		info->next = ipmi_dmi_infos;
		ipmi_dmi_infos = info;
	}

	if (ipmi_platform_add(name, ipmi_dmi_nr, &p))
		ipmi_dmi_nr++;
}

/*
 * Look up the slave address for a given interface.  This is here
 * because ACPI doesn't have a slave address while SMBIOS does, but we
 * prefer using ACPI so the ACPI code can use the IPMI namespace.
 * This function allows an ACPI-specified IPMI device to look up the
 * slave address from the DMI table.
 */
int ipmi_dmi_get_slave_addr(enum si_type si_type, unsigned int space,
			    unsigned long base_addr)
{
	struct ipmi_dmi_info *info = ipmi_dmi_infos;

	while (info) {
		if (info->si_type == si_type &&
		    info->space == space &&
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
	const u8 *data = (const u8 *) dm;
	int space = IPMI_IO_ADDR_SPACE;
	unsigned long base_addr;
	u8 len = dm->length;
	u8 slave_addr;
	int irq = 0, offset = 0;
	int type;

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
			space = 0; /* Match I2C interface 0. */
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
				space = IPMI_MEM_ADDR_SPACE;
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
				pr_err("Invalid offset: 0\n");
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

	dmi_add_platform_ipmi(base_addr, space, slave_addr, irq,
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
