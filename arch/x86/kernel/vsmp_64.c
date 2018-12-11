/*
 * vSMPowered(tm) systems specific initialization
 * Copyright (C) 2005 ScaleMP Inc.
 *
 * Use of this code is subject to the terms and conditions of the
 * GNU general public license version 2. See "COPYING" or
 * http://www.gnu.org/licenses/gpl.html
 *
 * Ravikiran Thirumalai <kiran@scalemp.com>,
 * Shai Fultheim <shai@scalemp.com>
 * Paravirt ops integration: Glauber de Oliveira Costa <gcosta@redhat.com>,
 *			     Ravikiran Thirumalai <kiran@scalemp.com>
 */

#include <linux/init.h>
#include <linux/pci_ids.h>
#include <linux/pci_regs.h>
#include <linux/smp.h>
#include <linux/irq.h>

#include <asm/apic.h>
#include <asm/pci-direct.h>
#include <asm/io.h>
#include <asm/paravirt.h>
#include <asm/setup.h>

#define TOPOLOGY_REGISTER_OFFSET 0x10

#ifdef CONFIG_PCI
static void __init set_vsmp_ctl(void)
{
	void __iomem *address;
	unsigned int cap, ctl, cfg;

	/* set vSMP magic bits to indicate vSMP capable kernel */
	cfg = read_pci_config(0, 0x1f, 0, PCI_BASE_ADDRESS_0);
	address = early_ioremap(cfg, 8);
	cap = readl(address);
	ctl = readl(address + 4);
	printk(KERN_INFO "vSMP CTL: capabilities:0x%08x  control:0x%08x\n",
	       cap, ctl);

	/* If possible, let the vSMP foundation route the interrupt optimally */
#ifdef CONFIG_SMP
	if (cap & ctl & BIT(8)) {
		ctl &= ~BIT(8);

#ifdef CONFIG_PROC_FS
		/* Don't let users change irq affinity via procfs */
		no_irq_affinity = 1;
#endif
	}
#endif

	writel(ctl, address + 4);
	ctl = readl(address + 4);
	pr_info("vSMP CTL: control set to:0x%08x\n", ctl);

	early_iounmap(address, 8);
}
static int is_vsmp = -1;

static void __init detect_vsmp_box(void)
{
	is_vsmp = 0;

	if (!early_pci_allowed())
		return;

	/* Check if we are running on a ScaleMP vSMPowered box */
	if (read_pci_config(0, 0x1f, 0, PCI_VENDOR_ID) ==
	     (PCI_VENDOR_ID_SCALEMP | (PCI_DEVICE_ID_SCALEMP_VSMP_CTL << 16)))
		is_vsmp = 1;
}

static int is_vsmp_box(void)
{
	if (is_vsmp != -1)
		return is_vsmp;
	else {
		WARN_ON_ONCE(1);
		return 0;
	}
}

#else
static void __init detect_vsmp_box(void)
{
}
static int is_vsmp_box(void)
{
	return 0;
}
static void __init set_vsmp_ctl(void)
{
}
#endif

static void __init vsmp_cap_cpus(void)
{
#if !defined(CONFIG_X86_VSMP) && defined(CONFIG_SMP) && defined(CONFIG_PCI)
	void __iomem *address;
	unsigned int cfg, topology, node_shift, maxcpus;

	/*
	 * CONFIG_X86_VSMP is not configured, so limit the number CPUs to the
	 * ones present in the first board, unless explicitly overridden by
	 * setup_max_cpus
	 */
	if (setup_max_cpus != NR_CPUS)
		return;

	/* Read the vSMP Foundation topology register */
	cfg = read_pci_config(0, 0x1f, 0, PCI_BASE_ADDRESS_0);
	address = early_ioremap(cfg + TOPOLOGY_REGISTER_OFFSET, 4);
	if (WARN_ON(!address))
		return;

	topology = readl(address);
	node_shift = (topology >> 16) & 0x7;
	if (!node_shift)
		/* The value 0 should be decoded as 8 */
		node_shift = 8;
	maxcpus = (topology & ((1 << node_shift) - 1)) + 1;

	pr_info("vSMP CTL: Capping CPUs to %d (CONFIG_X86_VSMP is unset)\n",
		maxcpus);
	setup_max_cpus = maxcpus;
	early_iounmap(address, 4);
#endif
}

static int apicid_phys_pkg_id(int initial_apic_id, int index_msb)
{
	return hard_smp_processor_id() >> index_msb;
}

static void vsmp_apic_post_init(void)
{
	/* need to update phys_pkg_id */
	apic->phys_pkg_id = apicid_phys_pkg_id;
}

void __init vsmp_init(void)
{
	detect_vsmp_box();
	if (!is_vsmp_box())
		return;

	x86_platform.apic_post_init = vsmp_apic_post_init;

	vsmp_cap_cpus();

	set_vsmp_ctl();
	return;
}
