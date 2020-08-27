// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * RTAS specific routines for PCI.
 *
 * Based on code from pci.c, chrp_pci.c and pSeries_pci.c
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/pgtable.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/rtas.h>
#include <asm/mpic.h>
#include <asm/ppc-pci.h>
#include <asm/eeh.h>

/* RTAS tokens */
static int read_pci_config;
static int write_pci_config;
static int ibm_read_pci_config;
static int ibm_write_pci_config;

static inline int config_access_valid(struct pci_dn *dn, int where)
{
	if (where < 256)
		return 1;
	if (where < 4096 && dn->pci_ext_config_space)
		return 1;

	return 0;
}

int rtas_read_config(struct pci_dn *pdn, int where, int size, u32 *val)
{
	int returnval = -1;
	unsigned long buid, addr;
	int ret;

	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!config_access_valid(pdn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;
#ifdef CONFIG_EEH
	if (pdn->edev && pdn->edev->pe &&
	    (pdn->edev->pe->state & EEH_PE_CFG_BLOCKED))
		return PCIBIOS_SET_FAILED;
#endif

	addr = rtas_config_addr(pdn->busno, pdn->devfn, where);
	buid = pdn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_read_pci_config, 4, 2, &returnval,
				addr, BUID_HI(buid), BUID_LO(buid), size);
	} else {
		ret = rtas_call(read_pci_config, 2, 2, &returnval, addr, size);
	}
	*val = returnval;

	if (ret)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int rtas_pci_read_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 *val)
{
	struct pci_dn *pdn;
	int ret;

	*val = 0xFFFFFFFF;

	pdn = pci_get_pdn_by_devfn(bus, devfn);

	/* Validity of pdn is checked in here */
	ret = rtas_read_config(pdn, where, size, val);
	if (*val == EEH_IO_ERROR_VALUE(size) &&
	    eeh_dev_check_failure(pdn_to_eeh_dev(pdn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return ret;
}

int rtas_write_config(struct pci_dn *pdn, int where, int size, u32 val)
{
	unsigned long buid, addr;
	int ret;

	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!config_access_valid(pdn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;
#ifdef CONFIG_EEH
	if (pdn->edev && pdn->edev->pe &&
	    (pdn->edev->pe->state & EEH_PE_CFG_BLOCKED))
		return PCIBIOS_SET_FAILED;
#endif

	addr = rtas_config_addr(pdn->busno, pdn->devfn, where);
	buid = pdn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_write_pci_config, 5, 1, NULL, addr,
			BUID_HI(buid), BUID_LO(buid), size, (ulong) val);
	} else {
		ret = rtas_call(write_pci_config, 3, 1, NULL, addr, size, (ulong)val);
	}

	if (ret)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int rtas_pci_write_config(struct pci_bus *bus,
				 unsigned int devfn,
				 int where, int size, u32 val)
{
	struct pci_dn *pdn;

	pdn = pci_get_pdn_by_devfn(bus, devfn);

	/* Validity of pdn is checked in here. */
	return rtas_write_config(pdn, where, size, val);
}

static struct pci_ops rtas_pci_ops = {
	.read = rtas_pci_read_config,
	.write = rtas_pci_write_config,
};

static int is_python(struct device_node *dev)
{
	const char *model = of_get_property(dev, "model", NULL);

	if (model && strstr(model, "Python"))
		return 1;

	return 0;
}

static void python_countermeasures(struct device_node *dev)
{
	struct resource registers;
	void __iomem *chip_regs;
	volatile u32 val;

	if (of_address_to_resource(dev, 0, &registers)) {
		printk(KERN_ERR "Can't get address for Python workarounds !\n");
		return;
	}

	/* Python's register file is 1 MB in size. */
	chip_regs = ioremap(registers.start & ~(0xfffffUL), 0x100000);

	/*
	 * Firmware doesn't always clear this bit which is critical
	 * for good performance - Anton
	 */

#define PRG_CL_RESET_VALID 0x00010000

	val = in_be32(chip_regs + 0xf6030);
	if (val & PRG_CL_RESET_VALID) {
		printk(KERN_INFO "Python workaround: ");
		val &= ~PRG_CL_RESET_VALID;
		out_be32(chip_regs + 0xf6030, val);
		/*
		 * We must read it back for changes to
		 * take effect
		 */
		val = in_be32(chip_regs + 0xf6030);
		printk("reg0: %x\n", val);
	}

	iounmap(chip_regs);
}

void __init init_pci_config_tokens(void)
{
	read_pci_config = rtas_token("read-pci-config");
	write_pci_config = rtas_token("write-pci-config");
	ibm_read_pci_config = rtas_token("ibm,read-pci-config");
	ibm_write_pci_config = rtas_token("ibm,write-pci-config");
}

unsigned long get_phb_buid(struct device_node *phb)
{
	struct resource r;

	if (ibm_read_pci_config == -1)
		return 0;
	if (of_address_to_resource(phb, 0, &r))
		return 0;
	return r.start;
}

static int phb_set_bus_ranges(struct device_node *dev,
			      struct pci_controller *phb)
{
	const __be32 *bus_range;
	unsigned int len;

	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		return 1;
 	}

	phb->first_busno = be32_to_cpu(bus_range[0]);
	phb->last_busno  = be32_to_cpu(bus_range[1]);

	return 0;
}

int rtas_setup_phb(struct pci_controller *phb)
{
	struct device_node *dev = phb->dn;

	if (is_python(dev))
		python_countermeasures(dev);

	if (phb_set_bus_ranges(dev, phb))
		return 1;

	phb->ops = &rtas_pci_ops;
	phb->buid = get_phb_buid(dev);

	return 0;
}
