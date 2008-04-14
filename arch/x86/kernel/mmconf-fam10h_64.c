/*
 * AMD Family 10h mmconfig enablement
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <asm/pci-direct.h>
#include <linux/sort.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/acpi.h>

#include "../pci/pci.h"

struct pci_hostbridge_probe {
	u32 bus;
	u32 slot;
	u32 vendor;
	u32 device;
};

static u64 __cpuinitdata fam10h_pci_mmconf_base;
static int __cpuinitdata fam10h_pci_mmconf_base_status;

static struct pci_hostbridge_probe pci_probes[] __cpuinitdata = {
	{ 0, 0x18, PCI_VENDOR_ID_AMD, 0x1200 },
	{ 0xff, 0, PCI_VENDOR_ID_AMD, 0x1200 },
};

struct range {
	u64 start;
	u64 end;
};

static int __cpuinit cmp_range(const void *x1, const void *x2)
{
	const struct range *r1 = x1;
	const struct range *r2 = x2;
	int start1, start2;

	start1 = r1->start >> 32;
	start2 = r2->start >> 32;

	return start1 - start2;
}

/*[47:0] */
/* need to avoid (0xfd<<32) and (0xfe<<32), ht used space */
#define FAM10H_PCI_MMCONF_BASE (0xfcULL<<32)
#define BASE_VALID(b) ((b != (0xfdULL << 32)) && (b != (0xfeULL << 32)))
static void __cpuinit get_fam10h_pci_mmconf_base(void)
{
	int i;
	unsigned bus;
	unsigned slot;
	int found;

	u64 val;
	u32 address;
	u64 tom2;
	u64 base = FAM10H_PCI_MMCONF_BASE;

	int hi_mmio_num;
	struct range range[8];

	/* only try to get setting from BSP */
	/* -1 or 1 */
	if (fam10h_pci_mmconf_base_status)
		return;

	if (!early_pci_allowed())
		goto fail;

	found = 0;
	for (i = 0; i < ARRAY_SIZE(pci_probes); i++) {
		u32 id;
		u16 device;
		u16 vendor;

		bus = pci_probes[i].bus;
		slot = pci_probes[i].slot;
		id = read_pci_config(bus, slot, 0, PCI_VENDOR_ID);

		vendor = id & 0xffff;
		device = (id>>16) & 0xffff;
		if (pci_probes[i].vendor == vendor &&
		    pci_probes[i].device == device) {
			found = 1;
			break;
		}
	}

	if (!found)
		goto fail;

	/* SYS_CFG */
	address = MSR_K8_SYSCFG;
	rdmsrl(address, val);

	/* TOP_MEM2 is not enabled? */
	if (!(val & (1<<21))) {
		tom2 = 0;
	} else {
		/* TOP_MEM2 */
		address = MSR_K8_TOP_MEM2;
		rdmsrl(address, val);
		tom2 = val & (0xffffULL<<32);
	}

	if (base <= tom2)
		base = tom2 + (1ULL<<32);

	/*
	 * need to check if the range is in the high mmio range that is
	 * above 4G
	 */
	hi_mmio_num = 0;
	for (i = 0; i < 8; i++) {
		u32 reg;
		u64 start;
		u64 end;
		reg = read_pci_config(bus, slot, 1, 0x80 + (i << 3));
		if (!(reg & 3))
			continue;

		start = (((u64)reg) << 8) & (0xffULL << 32); /* 39:16 on 31:8*/
		reg = read_pci_config(bus, slot, 1, 0x84 + (i << 3));
		end = (((u64)reg) << 8) & (0xffULL << 32); /* 39:16 on 31:8*/

		if (!end)
			continue;

		range[hi_mmio_num].start = start;
		range[hi_mmio_num].end = end;
		hi_mmio_num++;
	}

	if (!hi_mmio_num)
		goto out;

	/* sort the range */
	sort(range, hi_mmio_num, sizeof(struct range), cmp_range, NULL);

	if (range[hi_mmio_num - 1].end < base)
		goto out;
	if (range[0].start > base)
		goto out;

	/* need to find one window */
	base = range[0].start - (1ULL << 32);
	if ((base > tom2) && BASE_VALID(base))
		goto out;
	base = range[hi_mmio_num - 1].end + (1ULL << 32);
	if ((base > tom2) && BASE_VALID(base))
		goto out;
	/* need to find window between ranges */
	if (hi_mmio_num > 1)
	for (i = 0; i < hi_mmio_num - 1; i++) {
		if (range[i + 1].start > (range[i].end + (1ULL << 32))) {
			base = range[i].end + (1ULL << 32);
			if ((base > tom2) && BASE_VALID(base))
				goto out;
		}
	}

fail:
	fam10h_pci_mmconf_base_status = -1;
	return;
out:
	fam10h_pci_mmconf_base = base;
	fam10h_pci_mmconf_base_status = 1;
}

void __cpuinit fam10h_check_enable_mmcfg(void)
{
	u64 val;
	u32 address;

	if (!(pci_probe & PCI_CHECK_ENABLE_AMD_MMCONF))
		return;

	address = MSR_FAM10H_MMIO_CONF_BASE;
	rdmsrl(address, val);

	/* try to make sure that AP's setting is identical to BSP setting */
	if (val & FAM10H_MMIO_CONF_ENABLE) {
		unsigned busnbits;
		busnbits = (val >> FAM10H_MMIO_CONF_BUSRANGE_SHIFT) &
			FAM10H_MMIO_CONF_BUSRANGE_MASK;

		/* only trust the one handle 256 buses, if acpi=off */
		if (!acpi_pci_disabled || busnbits >= 8) {
			u64 base;
			base = val & (0xffffULL << 32);
			if (fam10h_pci_mmconf_base_status <= 0) {
				fam10h_pci_mmconf_base = base;
				fam10h_pci_mmconf_base_status = 1;
				return;
			} else if (fam10h_pci_mmconf_base ==  base)
				return;
		}
	}

	/*
	 * if it is not enabled, try to enable it and assume only one segment
	 * with 256 buses
	 */
	get_fam10h_pci_mmconf_base();
	if (fam10h_pci_mmconf_base_status <= 0)
		return;

	printk(KERN_INFO "Enable MMCONFIG on AMD Family 10h\n");
	val &= ~((FAM10H_MMIO_CONF_BASE_MASK<<FAM10H_MMIO_CONF_BASE_SHIFT) |
	     (FAM10H_MMIO_CONF_BUSRANGE_MASK<<FAM10H_MMIO_CONF_BUSRANGE_SHIFT));
	val |= fam10h_pci_mmconf_base | (8 << FAM10H_MMIO_CONF_BUSRANGE_SHIFT) |
	       FAM10H_MMIO_CONF_ENABLE;
	wrmsrl(address, val);
}

static int __devinit set_check_enable_amd_mmconf(const struct dmi_system_id *d)
{
        pci_probe |= PCI_CHECK_ENABLE_AMD_MMCONF;
        return 0;
}

static struct dmi_system_id __devinitdata mmconf_dmi_table[] = {
        {
                .callback = set_check_enable_amd_mmconf,
                .ident = "Sun Microsystems Machine",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "Sun Microsystems"),
                },
        },
	{}
};

void __init check_enable_amd_mmconf_dmi(void)
{
	dmi_check_system(mmconf_dmi_table);
}
