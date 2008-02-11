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
#include <asm/pci-direct.h>
#include <asm/io.h>
#include <asm/paravirt.h>

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

static void vsmp_restore_fl(unsigned long flags)
{
	if (flags & X86_EFLAGS_IF)
		flags &= ~X86_EFLAGS_AC;
	else
		flags |= X86_EFLAGS_AC;
	native_restore_fl(flags);
}

static void vsmp_irq_disable(void)
{
	unsigned long flags = native_save_fl();

	native_restore_fl((flags & ~X86_EFLAGS_IF) | X86_EFLAGS_AC);
}

static void vsmp_irq_enable(void)
{
	unsigned long flags = native_save_fl();

	native_restore_fl((flags | X86_EFLAGS_IF) & (~X86_EFLAGS_AC));
}

static unsigned __init vsmp_patch(u8 type, u16 clobbers, void *ibuf,
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

void __init vsmp_init(void)
{
	void *address;
	unsigned int cap, ctl, cfg;

	if (!early_pci_allowed())
		return;

	/* Check if we are running on a ScaleMP vSMP box */
	if ((read_pci_config_16(0, 0x1f, 0, PCI_VENDOR_ID) !=
	     PCI_VENDOR_ID_SCALEMP) ||
	    (read_pci_config_16(0, 0x1f, 0, PCI_DEVICE_ID) !=
	     PCI_DEVICE_ID_SCALEMP_VSMP_CTL))
		return;

	/* set vSMP magic bits to indicate vSMP capable kernel */
	cfg = read_pci_config(0, 0x1f, 0, PCI_BASE_ADDRESS_0);
	address = early_ioremap(cfg, 8);
	cap = readl(address);
	ctl = readl(address + 4);
	printk(KERN_INFO "vSMP CTL: capabilities:0x%08x  control:0x%08x\n",
	       cap, ctl);
	if (cap & ctl & (1 << 4)) {
		/* Turn on vSMP IRQ fastpath handling (see system.h) */
		ctl &= ~(1 << 4);
		writel(ctl, address + 4);
		ctl = readl(address + 4);
		printk(KERN_INFO "vSMP CTL: control set to:0x%08x\n", ctl);
	}

	early_iounmap(address, 8);
	return;
}
