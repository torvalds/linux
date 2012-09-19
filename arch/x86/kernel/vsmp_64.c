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

#if defined CONFIG_PCI && defined CONFIG_PARAVIRT
/*
 * Interrupt control on vSMPowered systems:
 * ~AC is a shadow of IF.  If IF is 'on' AC should be 'off'
 * and vice versa.
 */

static unsigned long vsmp_save_fl(void)
{
	unsigned long flags = native_save_fl();

	if (!(flags & X86_EFLAGS_IF) || (flags & X86_EFLAGS_AC))
		flags &= ~X86_EFLAGS_IF;
	return flags;
}
PV_CALLEE_SAVE_REGS_THUNK(vsmp_save_fl);

static void vsmp_restore_fl(unsigned long flags)
{
	if (flags & X86_EFLAGS_IF)
		flags &= ~X86_EFLAGS_AC;
	else
		flags |= X86_EFLAGS_AC;
	native_restore_fl(flags);
}
PV_CALLEE_SAVE_REGS_THUNK(vsmp_restore_fl);

static void vsmp_irq_disable(void)
{
	unsigned long flags = native_save_fl();

	native_restore_fl((flags & ~X86_EFLAGS_IF) | X86_EFLAGS_AC);
}
PV_CALLEE_SAVE_REGS_THUNK(vsmp_irq_disable);

static void vsmp_irq_enable(void)
{
	unsigned long flags = native_save_fl();

	native_restore_fl((flags | X86_EFLAGS_IF) & (~X86_EFLAGS_AC));
}
PV_CALLEE_SAVE_REGS_THUNK(vsmp_irq_enable);

static unsigned __init_or_module vsmp_patch(u8 type, u16 clobbers, void *ibuf,
				  unsigned long addr, unsigned len)
{
	switch (type) {
	case PARAVIRT_PATCH(pv_irq_ops.irq_enable):
	case PARAVIRT_PATCH(pv_irq_ops.irq_disable):
	case PARAVIRT_PATCH(pv_irq_ops.save_fl):
	case PARAVIRT_PATCH(pv_irq_ops.restore_fl):
		return paravirt_patch_default(type, clobbers, ibuf, addr, len);
	default:
		return native_patch(type, clobbers, ibuf, addr, len);
	}

}

static void __init set_vsmp_pv_ops(void)
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

	if (cap & ctl & (1 << 4)) {
		/* Setup irq ops and turn on vSMP  IRQ fastpath handling */
		pv_irq_ops.irq_disable = PV_CALLEE_SAVE(vsmp_irq_disable);
		pv_irq_ops.irq_enable  = PV_CALLEE_SAVE(vsmp_irq_enable);
		pv_irq_ops.save_fl  = PV_CALLEE_SAVE(vsmp_save_fl);
		pv_irq_ops.restore_fl  = PV_CALLEE_SAVE(vsmp_restore_fl);
		pv_init_ops.patch = vsmp_patch;
		ctl &= ~(1 << 4);
	}
	writel(ctl, address + 4);
	ctl = readl(address + 4);
	pr_info("vSMP CTL: control set to:0x%08x\n", ctl);

	early_iounmap(address, 8);
}
#else
static void __init set_vsmp_pv_ops(void)
{
}
#endif

#ifdef CONFIG_PCI
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

int is_vsmp_box(void)
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
int is_vsmp_box(void)
{
	return 0;
}
#endif

static void __init vsmp_cap_cpus(void)
{
#if !defined(CONFIG_X86_VSMP) && defined(CONFIG_SMP)
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

/*
 * In vSMP, all cpus should be capable of handling interrupts, regardless of
 * the APIC used.
 */
static void fill_vector_allocation_domain(int cpu, struct cpumask *retmask,
					  const struct cpumask *mask)
{
	cpumask_setall(retmask);
}

static void vsmp_apic_post_init(void)
{
	/* need to update phys_pkg_id */
	apic->phys_pkg_id = apicid_phys_pkg_id;
	apic->vector_allocation_domain = fill_vector_allocation_domain;
}

void __init vsmp_init(void)
{
	detect_vsmp_box();
	if (!is_vsmp_box())
		return;

	x86_platform.apic_post_init = vsmp_apic_post_init;

	vsmp_cap_cpus();

	set_vsmp_pv_ops();
	return;
}
