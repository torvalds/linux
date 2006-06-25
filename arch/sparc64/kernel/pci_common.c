/* $Id: pci_common.c,v 1.29 2002/02/01 00:56:03 davem Exp $
 * pci_common.c: PCI controller common support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <asm/pbm.h>
#include <asm/prom.h>

#include "pci_impl.h"

/* Pass "pci=irq_verbose" on the kernel command line to enable this.  */
int pci_irq_verbose;

/* Fix self device of BUS and hook it into BUS->self.
 * The pci_scan_bus does not do this for the host bridge.
 */
void __init pci_fixup_host_bridge_self(struct pci_bus *pbus)
{
	struct pci_dev *pdev;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		if (pdev->class >> 8 == PCI_CLASS_BRIDGE_HOST) {
			pbus->self = pdev;
			return;
		}
	}

	prom_printf("PCI: Critical error, cannot find host bridge PDEV.\n");
	prom_halt();
}

/* Find the OBP PROM device tree node for a PCI device.  */
static struct device_node * __init
find_device_prom_node(struct pci_pbm_info *pbm, struct pci_dev *pdev,
		      struct device_node *bus_node,
		      struct linux_prom_pci_registers **pregs,
		      int *nregs)
{
	struct device_node *dp;

	*nregs = 0;

	/*
	 * Return the PBM's PROM node in case we are it's PCI device,
	 * as the PBM's reg property is different to standard PCI reg
	 * properties. We would delete this device entry otherwise,
	 * which confuses XFree86's device probing...
	 */
	if ((pdev->bus->number == pbm->pci_bus->number) && (pdev->devfn == 0) &&
	    (pdev->vendor == PCI_VENDOR_ID_SUN) &&
	    (pdev->device == PCI_DEVICE_ID_SUN_PBM ||
	     pdev->device == PCI_DEVICE_ID_SUN_SCHIZO ||
	     pdev->device == PCI_DEVICE_ID_SUN_TOMATILLO ||
	     pdev->device == PCI_DEVICE_ID_SUN_SABRE ||
	     pdev->device == PCI_DEVICE_ID_SUN_HUMMINGBIRD))
		return bus_node;

	dp = bus_node->child;
	while (dp) {
		struct linux_prom_pci_registers *regs;
		struct property *prop;
		int len;

		prop = of_find_property(dp, "reg", &len);
		if (!prop)
			goto do_next_sibling;

		regs = prop->value;
		if (((regs[0].phys_hi >> 8) & 0xff) == pdev->devfn) {
			*pregs = regs;
			*nregs = len / sizeof(struct linux_prom_pci_registers);
			return dp;
		}

	do_next_sibling:
		dp = dp->sibling;
	}

	return NULL;
}

/* Older versions of OBP on PCI systems encode 64-bit MEM
 * space assignments incorrectly, this fixes them up.  We also
 * take the opportunity here to hide other kinds of bogus
 * assignments.
 */
static void __init fixup_obp_assignments(struct pci_dev *pdev,
					 struct pcidev_cookie *pcp)
{
	int i;

	if (pdev->vendor == PCI_VENDOR_ID_AL &&
	    (pdev->device == PCI_DEVICE_ID_AL_M7101 ||
	     pdev->device == PCI_DEVICE_ID_AL_M1533)) {
		int i;

		/* Zap all of the normal resources, they are
		 * meaningless and generate bogus resource collision
		 * messages.  This is OpenBoot's ill-fated attempt to
		 * represent the implicit resources that these devices
		 * have.
		 */
		pcp->num_prom_assignments = 0;
		for (i = 0; i < 6; i++) {
			pdev->resource[i].start =
				pdev->resource[i].end =
				pdev->resource[i].flags = 0;
		}
		pdev->resource[PCI_ROM_RESOURCE].start =
			pdev->resource[PCI_ROM_RESOURCE].end =
			pdev->resource[PCI_ROM_RESOURCE].flags = 0;
		return;
	}

	for (i = 0; i < pcp->num_prom_assignments; i++) {
		struct linux_prom_pci_registers *ap;
		int space;

		ap = &pcp->prom_assignments[i];
		space = ap->phys_hi >> 24;
		if ((space & 0x3) == 2 &&
		    (space & 0x4) != 0) {
			ap->phys_hi &= ~(0x7 << 24);
			ap->phys_hi |= 0x3 << 24;
		}
	}
}

/* Fill in the PCI device cookie sysdata for the given
 * PCI device.  This cookie is the means by which one
 * can get to OBP and PCI controller specific information
 * for a PCI device.
 */
static void __init pdev_cookie_fillin(struct pci_pbm_info *pbm,
				      struct pci_dev *pdev,
				      struct device_node *bus_node)
{
	struct linux_prom_pci_registers *pregs = NULL;
	struct pcidev_cookie *pcp;
	struct device_node *dp;
	struct property *prop;
	int nregs, len;

	dp = find_device_prom_node(pbm, pdev, bus_node,
				   &pregs, &nregs);
	if (!dp) {
		/* If it is not in the OBP device tree then
		 * there must be a damn good reason for it.
		 *
		 * So what we do is delete the device from the
		 * PCI device tree completely.  This scenario
		 * is seen, for example, on CP1500 for the
		 * second EBUS/HappyMeal pair if the external
		 * connector for it is not present.
		 */
		pci_remove_bus_device(pdev);
		return;
	}

	pcp = kzalloc(sizeof(*pcp), GFP_ATOMIC);
	if (pcp == NULL) {
		prom_printf("PCI_COOKIE: Fatal malloc error, aborting...\n");
		prom_halt();
	}
	pcp->pbm = pbm;
	pcp->prom_node = dp;
	memcpy(pcp->prom_regs, pregs,
	       nregs * sizeof(struct linux_prom_pci_registers));
	pcp->num_prom_regs = nregs;

	/* We can't have the pcidev_cookie assignments be just
	 * direct pointers into the property value, since they
	 * are potentially modified by the probing process.
	 */
	prop = of_find_property(dp, "assigned-addresses", &len);
	if (!prop) {
		pcp->num_prom_assignments = 0;
	} else {
		memcpy(pcp->prom_assignments, prop->value, len);
		pcp->num_prom_assignments =
			(len / sizeof(pcp->prom_assignments[0]));
	}

	if (strcmp(dp->name, "ebus") == 0) {
		struct linux_prom_ebus_ranges *erng;
		int iter;

		/* EBUS is special... */
		prop = of_find_property(dp, "ranges", &len);
		if (!prop) {
			prom_printf("EBUS: Fatal error, no range property\n");
			prom_halt();
		}
		erng = prop->value;
		len = (len / sizeof(erng[0]));
		for (iter = 0; iter < len; iter++) {
			struct linux_prom_ebus_ranges *ep = &erng[iter];
			struct linux_prom_pci_registers *ap;

			ap = &pcp->prom_assignments[iter];

			ap->phys_hi = ep->parent_phys_hi;
			ap->phys_mid = ep->parent_phys_mid;
			ap->phys_lo = ep->parent_phys_lo;
			ap->size_hi = 0;
			ap->size_lo = ep->size;
		}
		pcp->num_prom_assignments = len;
	}

	fixup_obp_assignments(pdev, pcp);

	pdev->sysdata = pcp;
}

void __init pci_fill_in_pbm_cookies(struct pci_bus *pbus,
				    struct pci_pbm_info *pbm,
				    struct device_node *dp)
{
	struct pci_dev *pdev, *pdev_next;
	struct pci_bus *this_pbus, *pbus_next;

	/* This must be _safe because the cookie fillin
	   routine can delete devices from the tree.  */
	list_for_each_entry_safe(pdev, pdev_next, &pbus->devices, bus_list)
		pdev_cookie_fillin(pbm, pdev, dp);

	list_for_each_entry_safe(this_pbus, pbus_next, &pbus->children, node) {
		struct pcidev_cookie *pcp = this_pbus->self->sysdata;

		pci_fill_in_pbm_cookies(this_pbus, pbm, pcp->prom_node);
	}
}

static void __init bad_assignment(struct pci_dev *pdev,
				  struct linux_prom_pci_registers *ap,
				  struct resource *res,
				  int do_prom_halt)
{
	prom_printf("PCI: Bogus PROM assignment. BUS[%02x] DEVFN[%x]\n",
		    pdev->bus->number, pdev->devfn);
	if (ap)
		prom_printf("PCI: phys[%08x:%08x:%08x] size[%08x:%08x]\n",
			    ap->phys_hi, ap->phys_mid, ap->phys_lo,
			    ap->size_hi, ap->size_lo);
	if (res)
		prom_printf("PCI: RES[%016lx-->%016lx:(%lx)]\n",
			    res->start, res->end, res->flags);
	if (do_prom_halt)
		prom_halt();
}

static struct resource *
__init get_root_resource(struct linux_prom_pci_registers *ap,
			 struct pci_pbm_info *pbm)
{
	int space = (ap->phys_hi >> 24) & 3;

	switch (space) {
	case 0:
		/* Configuration space, silently ignore it. */
		return NULL;

	case 1:
		/* 16-bit IO space */
		return &pbm->io_space;

	case 2:
		/* 32-bit MEM space */
		return &pbm->mem_space;

	case 3:
		/* 64-bit MEM space, these are allocated out of
		 * the 32-bit mem_space range for the PBM, ie.
		 * we just zero out the upper 32-bits.
		 */
		return &pbm->mem_space;

	default:
		printk("PCI: What is resource space %x?\n", space);
		return NULL;
	};
}

static struct resource *
__init get_device_resource(struct linux_prom_pci_registers *ap,
			   struct pci_dev *pdev)
{
	struct resource *res;
	int breg = (ap->phys_hi & 0xff);

	switch (breg) {
	case  PCI_ROM_ADDRESS:
		/* Unfortunately I have seen several cases where
		 * buggy FCODE uses a space value of '1' (I/O space)
		 * in the register property for the ROM address
		 * so disable this sanity check for now.
		 */
#if 0
	{
		int space = (ap->phys_hi >> 24) & 3;

		/* It had better be MEM space. */
		if (space != 2)
			bad_assignment(pdev, ap, NULL, 0);
	}
#endif
		res = &pdev->resource[PCI_ROM_RESOURCE];
		break;

	case PCI_BASE_ADDRESS_0:
	case PCI_BASE_ADDRESS_1:
	case PCI_BASE_ADDRESS_2:
	case PCI_BASE_ADDRESS_3:
	case PCI_BASE_ADDRESS_4:
	case PCI_BASE_ADDRESS_5:
		res = &pdev->resource[(breg - PCI_BASE_ADDRESS_0) / 4];
		break;

	default:
		bad_assignment(pdev, ap, NULL, 0);
		res = NULL;
		break;
	};

	return res;
}

static int __init pdev_resource_collisions_expected(struct pci_dev *pdev)
{
	if (pdev->vendor != PCI_VENDOR_ID_SUN)
		return 0;

	if (pdev->device == PCI_DEVICE_ID_SUN_RIO_EBUS ||
	    pdev->device == PCI_DEVICE_ID_SUN_RIO_1394 ||
	    pdev->device == PCI_DEVICE_ID_SUN_RIO_USB)
		return 1;

	return 0;
}

static void __init pdev_record_assignments(struct pci_pbm_info *pbm,
					   struct pci_dev *pdev)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	int i;

	for (i = 0; i < pcp->num_prom_assignments; i++) {
		struct linux_prom_pci_registers *ap;
		struct resource *root, *res;

		/* The format of this property is specified in
		 * the PCI Bus Binding to IEEE1275-1994.
		 */
		ap = &pcp->prom_assignments[i];
		root = get_root_resource(ap, pbm);
		res = get_device_resource(ap, pdev);
		if (root == NULL || res == NULL ||
		    res->flags == 0)
			continue;

		/* Ok we know which resource this PROM assignment is
		 * for, sanity check it.
		 */
		if ((res->start & 0xffffffffUL) != ap->phys_lo)
			bad_assignment(pdev, ap, res, 1);

		/* If it is a 64-bit MEM space assignment, verify that
		 * the resource is too and that the upper 32-bits match.
		 */
		if (((ap->phys_hi >> 24) & 3) == 3) {
			if (((res->flags & IORESOURCE_MEM) == 0) ||
			    ((res->flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
			     != PCI_BASE_ADDRESS_MEM_TYPE_64))
				bad_assignment(pdev, ap, res, 1);
			if ((res->start >> 32) != ap->phys_mid)
				bad_assignment(pdev, ap, res, 1);

			/* PBM cannot generate cpu initiated PIOs
			 * to the full 64-bit space.  Therefore the
			 * upper 32-bits better be zero.  If it is
			 * not, just skip it and we will assign it
			 * properly ourselves.
			 */
			if ((res->start >> 32) != 0UL) {
				printk(KERN_ERR "PCI: OBP assigns out of range MEM address "
				       "%016lx for region %ld on device %s\n",
				       res->start, (res - &pdev->resource[0]), pci_name(pdev));
				continue;
			}
		}

		/* Adjust the resource into the physical address space
		 * of this PBM.
		 */
		pbm->parent->resource_adjust(pdev, res, root);

		if (request_resource(root, res) < 0) {
			/* OK, there is some conflict.  But this is fine
			 * since we'll reassign it in the fixup pass.
			 *
			 * We notify the user that OBP made an error if it
			 * is a case we don't expect.
			 */
			if (!pdev_resource_collisions_expected(pdev)) {
				printk(KERN_ERR "PCI: Address space collision on region %ld "
				       "[%016lx:%016lx] of device %s\n",
				       (res - &pdev->resource[0]),
				       res->start, res->end,
				       pci_name(pdev));
			}
		}
	}
}

void __init pci_record_assignments(struct pci_pbm_info *pbm,
				   struct pci_bus *pbus)
{
	struct pci_dev *dev;
	struct pci_bus *bus;

	list_for_each_entry(dev, &pbus->devices, bus_list)
		pdev_record_assignments(pbm, dev);

	list_for_each_entry(bus, &pbus->children, node)
		pci_record_assignments(pbm, bus);
}

/* Return non-zero if PDEV has implicit I/O resources even
 * though it may not have an I/O base address register
 * active.
 */
static int __init has_implicit_io(struct pci_dev *pdev)
{
	int class = pdev->class >> 8;

	if (class == PCI_CLASS_NOT_DEFINED ||
	    class == PCI_CLASS_NOT_DEFINED_VGA ||
	    class == PCI_CLASS_STORAGE_IDE ||
	    (pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY)
		return 1;

	return 0;
}

static void __init pdev_assign_unassigned(struct pci_pbm_info *pbm,
					  struct pci_dev *pdev)
{
	u32 reg;
	u16 cmd;
	int i, io_seen, mem_seen;

	io_seen = mem_seen = 0;
	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *root, *res;
		unsigned long size, min, max, align;

		res = &pdev->resource[i];

		if (res->flags & IORESOURCE_IO)
			io_seen++;
		else if (res->flags & IORESOURCE_MEM)
			mem_seen++;

		/* If it is already assigned or the resource does
		 * not exist, there is nothing to do.
		 */
		if (res->parent != NULL || res->flags == 0UL)
			continue;

		/* Determine the root we allocate from. */
		if (res->flags & IORESOURCE_IO) {
			root = &pbm->io_space;
			min = root->start + 0x400UL;
			max = root->end;
		} else {
			root = &pbm->mem_space;
			min = root->start;
			max = min + 0x80000000UL;
		}

		size = res->end - res->start;
		align = size + 1;
		if (allocate_resource(root, res, size + 1, min, max, align, NULL, NULL) < 0) {
			/* uh oh */
			prom_printf("PCI: Failed to allocate resource %d for %s\n",
				    i, pci_name(pdev));
			prom_halt();
		}

		/* Update PCI config space. */
		pbm->parent->base_address_update(pdev, i);
	}

	/* Special case, disable the ROM.  Several devices
	 * act funny (ie. do not respond to memory space writes)
	 * when it is left enabled.  A good example are Qlogic,ISP
	 * adapters.
	 */
	pci_read_config_dword(pdev, PCI_ROM_ADDRESS, &reg);
	reg &= ~PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, PCI_ROM_ADDRESS, reg);

	/* If we saw I/O or MEM resources, enable appropriate
	 * bits in PCI command register.
	 */
	if (io_seen || mem_seen) {
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		if (io_seen || has_implicit_io(pdev))
			cmd |= PCI_COMMAND_IO;
		if (mem_seen)
			cmd |= PCI_COMMAND_MEMORY;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}

	/* If this is a PCI bridge or an IDE controller,
	 * enable bus mastering.  In the former case also
	 * set the cache line size correctly.
	 */
	if (((pdev->class >> 8) == PCI_CLASS_BRIDGE_PCI) ||
	    (((pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) &&
	     ((pdev->class & 0x80) != 0))) {
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);

		if ((pdev->class >> 8) == PCI_CLASS_BRIDGE_PCI)
			pci_write_config_byte(pdev,
					      PCI_CACHE_LINE_SIZE,
					      (64 / sizeof(u32)));
	}
}

void __init pci_assign_unassigned(struct pci_pbm_info *pbm,
				  struct pci_bus *pbus)
{
	struct pci_dev *dev;
	struct pci_bus *bus;

	list_for_each_entry(dev, &pbus->devices, bus_list)
		pdev_assign_unassigned(pbm, dev);

	list_for_each_entry(bus, &pbus->children, node)
		pci_assign_unassigned(pbm, bus);
}

static inline unsigned int pci_slot_swivel(struct pci_pbm_info *pbm,
					   struct pci_dev *toplevel_pdev,
					   struct pci_dev *pdev,
					   unsigned int interrupt)
{
	unsigned int ret;

	if (unlikely(interrupt < 1 || interrupt > 4)) {
		printk("%s: Device %s interrupt value of %u is strange.\n",
		       pbm->name, pci_name(pdev), interrupt);
		return interrupt;
	}

	ret = ((interrupt - 1 + (PCI_SLOT(pdev->devfn) & 3)) & 3) + 1;

	if (pci_irq_verbose)
		printk("%s: %s IRQ Swivel %s [%x:%x] -> [%x]\n",
		       pbm->name, pci_name(toplevel_pdev), pci_name(pdev),
		       interrupt, PCI_SLOT(pdev->devfn), ret);

	return ret;
}

static inline unsigned int pci_apply_intmap(struct pci_pbm_info *pbm,
					    struct pci_dev *toplevel_pdev,
					    struct pci_dev *pbus,
					    struct pci_dev *pdev,
					    unsigned int interrupt,
					    struct device_node **cnode)
{
	struct linux_prom_pci_intmap *imap;
	struct linux_prom_pci_intmask *imask;
	struct pcidev_cookie *pbus_pcp = pbus->sysdata;
	struct pcidev_cookie *pdev_pcp = pdev->sysdata;
	struct linux_prom_pci_registers *pregs = pdev_pcp->prom_regs;
	struct property *prop;
	int plen, num_imap, i;
	unsigned int hi, mid, lo, irq, orig_interrupt;

	*cnode = pbus_pcp->prom_node;

	prop = of_find_property(pbus_pcp->prom_node, "interrupt-map", &plen);
	if (!prop ||
	    (plen % sizeof(struct linux_prom_pci_intmap)) != 0) {
		printk("%s: Device %s interrupt-map has bad len %d\n",
		       pbm->name, pci_name(pbus), plen);
		goto no_intmap;
	}
	imap = prop->value;
	num_imap = plen / sizeof(struct linux_prom_pci_intmap);

	prop = of_find_property(pbus_pcp->prom_node, "interrupt-map-mask", &plen);
	if (!prop ||
	    (plen % sizeof(struct linux_prom_pci_intmask)) != 0) {
		printk("%s: Device %s interrupt-map-mask has bad len %d\n",
		       pbm->name, pci_name(pbus), plen);
		goto no_intmap;
	}
	imask = prop->value;

	orig_interrupt = interrupt;

	hi   = pregs->phys_hi & imask->phys_hi;
	mid  = pregs->phys_mid & imask->phys_mid;
	lo   = pregs->phys_lo & imask->phys_lo;
	irq  = interrupt & imask->interrupt;

	for (i = 0; i < num_imap; i++) {
		if (imap[i].phys_hi  == hi   &&
		    imap[i].phys_mid == mid  &&
		    imap[i].phys_lo  == lo   &&
		    imap[i].interrupt == irq) {
			*cnode = of_find_node_by_phandle(imap[i].cnode);
			interrupt = imap[i].cinterrupt;
		}
	}

	if (pci_irq_verbose)
		printk("%s: %s MAP BUS %s DEV %s [%x] -> [%x]\n",
		       pbm->name, pci_name(toplevel_pdev),
		       pci_name(pbus), pci_name(pdev),
		       orig_interrupt, interrupt);

no_intmap:
	return interrupt;
}

/* For each PCI bus on the way to the root:
 * 1) If it has an interrupt-map property, apply it.
 * 2) Else, swivel the interrupt number based upon the PCI device number.
 *
 * Return the "IRQ controller" node.  If this is the PBM's device node,
 * all interrupt translations are complete, else we should use that node's
 * "reg" property to apply the PBM's "interrupt-{map,mask}" to the interrupt.
 */
static struct device_node * __init
pci_intmap_match_to_root(struct pci_pbm_info *pbm,
			 struct pci_dev *pdev,
			 unsigned int *interrupt)
{
	struct pci_dev *toplevel_pdev = pdev;
	struct pcidev_cookie *toplevel_pcp = toplevel_pdev->sysdata;
	struct device_node *cnode = toplevel_pcp->prom_node;

	while (pdev->bus->number != pbm->pci_first_busno) {
		struct pci_dev *pbus = pdev->bus->self;
		struct pcidev_cookie *pcp = pbus->sysdata;
		struct property *prop;

		prop = of_find_property(pcp->prom_node, "interrupt-map", NULL);
		if (!prop) {
			*interrupt = pci_slot_swivel(pbm, toplevel_pdev,
						     pdev, *interrupt);
			cnode = pcp->prom_node;
		} else {
			*interrupt = pci_apply_intmap(pbm, toplevel_pdev,
						      pbus, pdev,
						      *interrupt, &cnode);

			while (pcp->prom_node != cnode &&
			       pbus->bus->number != pbm->pci_first_busno) {
				pbus = pbus->bus->self;
				pcp = pbus->sysdata;
			}
		}
		pdev = pbus;

		if (cnode == pbm->prom_node)
			break;
	}

	return cnode;
}

static int __init pci_intmap_match(struct pci_dev *pdev, unsigned int *interrupt)
{
	struct pcidev_cookie *dev_pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = dev_pcp->pbm;
	struct linux_prom_pci_registers *reg;
	struct device_node *cnode;
	struct property *prop;
	unsigned int hi, mid, lo, irq;
	int i, plen;

	cnode = pci_intmap_match_to_root(pbm, pdev, interrupt);
	if (cnode == pbm->prom_node)
		goto success;

	prop = of_find_property(cnode, "reg", &plen);
	if (!prop ||
	    (plen % sizeof(struct linux_prom_pci_registers)) != 0) {
		printk("%s: OBP node %s reg property has bad len %d\n",
		       pbm->name, cnode->full_name, plen);
		goto fail;
	}
	reg = prop->value;

	hi   = reg[0].phys_hi & pbm->pbm_intmask->phys_hi;
	mid  = reg[0].phys_mid & pbm->pbm_intmask->phys_mid;
	lo   = reg[0].phys_lo & pbm->pbm_intmask->phys_lo;
	irq  = *interrupt & pbm->pbm_intmask->interrupt;

	for (i = 0; i < pbm->num_pbm_intmap; i++) {
		struct linux_prom_pci_intmap *intmap;

		intmap = &pbm->pbm_intmap[i];

		if (intmap->phys_hi  == hi  &&
		    intmap->phys_mid == mid &&
		    intmap->phys_lo  == lo  &&
		    intmap->interrupt == irq) {
			*interrupt = intmap->cinterrupt;
			goto success;
		}
	}

fail:
	return 0;

success:
	if (pci_irq_verbose)
		printk("%s: Routing bus[%2x] slot[%2x] to INO[%02x]\n",
		       pbm->name,
		       pdev->bus->number, PCI_SLOT(pdev->devfn),
		       *interrupt);
	return 1;
}

static void __init pdev_fixup_irq(struct pci_dev *pdev)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct pci_controller_info *p = pbm->parent;
	unsigned int portid = pbm->portid;
	unsigned int prom_irq;
	struct device_node *dp = pcp->prom_node;
	struct property *prop;

	/* If this is an empty EBUS device, sometimes OBP fails to
	 * give it a valid fully specified interrupts property.
	 * The EBUS hooked up to SunHME on PCI I/O boards of
	 * Ex000 systems is one such case.
	 *
	 * The interrupt is not important so just ignore it.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_SUN &&
	    pdev->device == PCI_DEVICE_ID_SUN_EBUS &&
	    !dp->child) {
		pdev->irq = 0;
		return;
	}

	prop = of_find_property(dp, "interrupts", NULL);
	if (!prop) {
		pdev->irq = 0;
		return;
	}
	prom_irq = *(unsigned int *) prop->value;

	if (tlb_type != hypervisor) {
		/* Fully specified already? */
		if (((prom_irq & PCI_IRQ_IGN) >> 6) == portid) {
			pdev->irq = p->irq_build(pbm, pdev, prom_irq);
			goto have_irq;
		}

		/* An onboard device? (bit 5 set) */
		if ((prom_irq & PCI_IRQ_INO) & 0x20) {
			pdev->irq = p->irq_build(pbm, pdev, (portid << 6 | prom_irq));
			goto have_irq;
		}
	}

	/* Can we find a matching entry in the interrupt-map? */
	if (pci_intmap_match(pdev, &prom_irq)) {
		pdev->irq = p->irq_build(pbm, pdev, (portid << 6) | prom_irq);
		goto have_irq;
	}

	/* Ok, we have to do it the hard way. */
	{
		unsigned int bus, slot, line;

		bus = (pbm == &pbm->parent->pbm_B) ? (1 << 4) : 0;

		/* If we have a legal interrupt property, use it as
		 * the IRQ line.
		 */
		if (prom_irq > 0 && prom_irq < 5) {
			line = ((prom_irq - 1) & 3);
		} else {
			u8 pci_irq_line;

			/* Else just directly consult PCI config space. */
			pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pci_irq_line);
			line = ((pci_irq_line - 1) & 3);
		}

		/* Now figure out the slot.
		 *
		 * Basically, device number zero on the top-level bus is
		 * always the PCI host controller.  Slot 0 is then device 1.
		 * PBM A supports two external slots (0 and 1), and PBM B
		 * supports 4 external slots (0, 1, 2, and 3).  On-board PCI
		 * devices are wired to device numbers outside of these
		 * ranges. -DaveM
 		 */
		if (pdev->bus->number == pbm->pci_first_busno) {
			slot = PCI_SLOT(pdev->devfn) - pbm->pci_first_slot;
		} else {
			struct pci_dev *bus_dev;

			/* Underneath a bridge, use slot number of parent
			 * bridge which is closest to the PBM.
			 */
			bus_dev = pdev->bus->self;
			while (bus_dev->bus &&
			       bus_dev->bus->number != pbm->pci_first_busno)
				bus_dev = bus_dev->bus->self;

			slot = PCI_SLOT(bus_dev->devfn) - pbm->pci_first_slot;
		}
		slot = slot << 2;

		pdev->irq = p->irq_build(pbm, pdev,
					 ((portid << 6) & PCI_IRQ_IGN) |
					 (bus | slot | line));
	}

have_irq:
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE,
			      pdev->irq & PCI_IRQ_INO);
}

void __init pci_fixup_irq(struct pci_pbm_info *pbm,
			  struct pci_bus *pbus)
{
	struct pci_dev *dev;
	struct pci_bus *bus;

	list_for_each_entry(dev, &pbus->devices, bus_list)
		pdev_fixup_irq(dev);

	list_for_each_entry(bus, &pbus->children, node)
		pci_fixup_irq(pbm, bus);
}

static void pdev_setup_busmastering(struct pci_dev *pdev, int is_66mhz)
{
	u16 cmd;
	u8 hdr_type, min_gnt, ltimer;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	/* Read it back, if the mastering bit did not
	 * get set, the device does not support bus
	 * mastering so we have nothing to do here.
	 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if ((cmd & PCI_COMMAND_MASTER) == 0)
		return;

	/* Set correct cache line size, 64-byte on all
	 * Sparc64 PCI systems.  Note that the value is
	 * measured in 32-bit words.
	 */
	pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE,
			      64 / sizeof(u32));

	pci_read_config_byte(pdev, PCI_HEADER_TYPE, &hdr_type);
	hdr_type &= ~0x80;
	if (hdr_type != PCI_HEADER_TYPE_NORMAL)
		return;

	/* If the latency timer is already programmed with a non-zero
	 * value, assume whoever set it (OBP or whoever) knows what
	 * they are doing.
	 */
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &ltimer);
	if (ltimer != 0)
		return;

	/* XXX Since I'm tipping off the min grant value to
	 * XXX choose a suitable latency timer value, I also
	 * XXX considered making use of the max latency value
	 * XXX as well.  Unfortunately I've seen too many bogusly
	 * XXX low settings for it to the point where it lacks
	 * XXX any usefulness.  In one case, an ethernet card
	 * XXX claimed a min grant of 10 and a max latency of 5.
	 * XXX Now, if I had two such cards on the same bus I
	 * XXX could not set the desired burst period (calculated
	 * XXX from min grant) without violating the max latency
	 * XXX bound.  Duh...
	 * XXX
	 * XXX I blame dumb PC bios implementors for stuff like
	 * XXX this, most of them don't even try to do something
	 * XXX sensible with latency timer values and just set some
	 * XXX default value (usually 32) into every device.
	 */

	pci_read_config_byte(pdev, PCI_MIN_GNT, &min_gnt);

	if (min_gnt == 0) {
		/* If no min_gnt setting then use a default
		 * value.
		 */
		if (is_66mhz)
			ltimer = 16;
		else
			ltimer = 32;
	} else {
		int shift_factor;

		if (is_66mhz)
			shift_factor = 2;
		else
			shift_factor = 3;

		/* Use a default value when the min_gnt value
		 * is erroneously high.
		 */
		if (((unsigned int) min_gnt << shift_factor) > 512 ||
		    ((min_gnt << shift_factor) & 0xff) == 0) {
			ltimer = 8 << shift_factor;
		} else {
			ltimer = min_gnt << shift_factor;
		}
	}

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, ltimer);
}

void pci_determine_66mhz_disposition(struct pci_pbm_info *pbm,
				     struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	int all_are_66mhz;
	u16 status;

	if (pbm->is_66mhz_capable == 0) {
		all_are_66mhz = 0;
		goto out;
	}

	all_are_66mhz = 1;
	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		pci_read_config_word(pdev, PCI_STATUS, &status);
		if (!(status & PCI_STATUS_66MHZ)) {
			all_are_66mhz = 0;
			break;
		}
	}
out:
	pbm->all_devs_66mhz = all_are_66mhz;

	printk("PCI%d(PBM%c): Bus running at %dMHz\n",
	       pbm->parent->index,
	       (pbm == &pbm->parent->pbm_A) ? 'A' : 'B',
	       (all_are_66mhz ? 66 : 33));
}

void pci_setup_busmastering(struct pci_pbm_info *pbm,
			    struct pci_bus *pbus)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	int is_66mhz;

	is_66mhz = pbm->is_66mhz_capable && pbm->all_devs_66mhz;

	list_for_each_entry(dev, &pbus->devices, bus_list)
		pdev_setup_busmastering(dev, is_66mhz);

	list_for_each_entry(bus, &pbus->children, node)
		pci_setup_busmastering(pbm, bus);
}

void pci_register_legacy_regions(struct resource *io_res,
				 struct resource *mem_res)
{
	struct resource *p;

	/* VGA Video RAM. */
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->name = "Video RAM area";
	p->start = mem_res->start + 0xa0000UL;
	p->end = p->start + 0x1ffffUL;
	p->flags = IORESOURCE_BUSY;
	request_resource(mem_res, p);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->name = "System ROM";
	p->start = mem_res->start + 0xf0000UL;
	p->end = p->start + 0xffffUL;
	p->flags = IORESOURCE_BUSY;
	request_resource(mem_res, p);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return;

	p->name = "Video ROM";
	p->start = mem_res->start + 0xc0000UL;
	p->end = p->start + 0x7fffUL;
	p->flags = IORESOURCE_BUSY;
	request_resource(mem_res, p);
}

/* Generic helper routines for PCI error reporting. */
void pci_scan_for_target_abort(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	struct pci_bus *bus;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_SIG_TARGET_ABORT |
				   PCI_STATUS_REC_TARGET_ABORT));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("PCI%d(PBM%c): Device [%s] saw Target Abort [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_target_abort(p, pbm, bus);
}

void pci_scan_for_master_abort(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	struct pci_bus *bus;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_REC_MASTER_ABORT));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("PCI%d(PBM%c): Device [%s] received Master Abort [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_master_abort(p, pbm, bus);
}

void pci_scan_for_parity_error(struct pci_controller_info *p,
			       struct pci_pbm_info *pbm,
			       struct pci_bus *pbus)
{
	struct pci_dev *pdev;
	struct pci_bus *bus;

	list_for_each_entry(pdev, &pbus->devices, bus_list) {
		u16 status, error_bits;

		pci_read_config_word(pdev, PCI_STATUS, &status);
		error_bits =
			(status & (PCI_STATUS_PARITY |
				   PCI_STATUS_DETECTED_PARITY));
		if (error_bits) {
			pci_write_config_word(pdev, PCI_STATUS, error_bits);
			printk("PCI%d(PBM%c): Device [%s] saw Parity Error [%016x]\n",
			       p->index, ((pbm == &p->pbm_A) ? 'A' : 'B'),
			       pci_name(pdev), status);
		}
	}

	list_for_each_entry(bus, &pbus->children, node)
		pci_scan_for_parity_error(p, pbm, bus);
}
