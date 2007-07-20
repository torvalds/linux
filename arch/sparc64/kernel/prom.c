/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *  Adapted for sparc64 by David S. Miller davem@davemloft.net
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/module.h>

#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/oplib.h>
#include <asm/irq.h>
#include <asm/asi.h>
#include <asm/upa.h>
#include <asm/smp.h>

extern struct device_node *allnodes;	/* temporary while merging */

extern rwlock_t devtree_lock;	/* temporary while merging */

struct device_node *of_find_node_by_phandle(phandle handle)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->node == handle)
			break;

	return np;
}
EXPORT_SYMBOL(of_find_node_by_phandle);

int of_getintprop_default(struct device_node *np, const char *name, int def)
{
	struct property *prop;
	int len;

	prop = of_find_property(np, name, &len);
	if (!prop || len != 4)
		return def;

	return *(int *) prop->value;
}
EXPORT_SYMBOL(of_getintprop_default);

int of_set_property(struct device_node *dp, const char *name, void *val, int len)
{
	struct property **prevp;
	void *new_val;
	int err;

	new_val = kmalloc(len, GFP_KERNEL);
	if (!new_val)
		return -ENOMEM;

	memcpy(new_val, val, len);

	err = -ENODEV;

	write_lock(&devtree_lock);
	prevp = &dp->properties;
	while (*prevp) {
		struct property *prop = *prevp;

		if (!strcasecmp(prop->name, name)) {
			void *old_val = prop->value;
			int ret;

			ret = prom_setprop(dp->node, name, val, len);
			err = -EINVAL;
			if (ret >= 0) {
				prop->value = new_val;
				prop->length = len;

				if (OF_IS_DYNAMIC(prop))
					kfree(old_val);

				OF_MARK_DYNAMIC(prop);

				err = 0;
			}
			break;
		}
		prevp = &(*prevp)->next;
	}
	write_unlock(&devtree_lock);

	/* XXX Upate procfs if necessary... */

	return err;
}
EXPORT_SYMBOL(of_set_property);

static unsigned int prom_early_allocated;

static void * __init prom_early_alloc(unsigned long size)
{
	void *ret;

	ret = __alloc_bootmem(size, SMP_CACHE_BYTES, 0UL);
	if (ret != NULL)
		memset(ret, 0, size);

	prom_early_allocated += size;

	return ret;
}

#ifdef CONFIG_PCI
/* PSYCHO interrupt mapping support. */
#define PSYCHO_IMAP_A_SLOT0	0x0c00UL
#define PSYCHO_IMAP_B_SLOT0	0x0c20UL
static unsigned long psycho_pcislot_imap_offset(unsigned long ino)
{
	unsigned int bus =  (ino & 0x10) >> 4;
	unsigned int slot = (ino & 0x0c) >> 2;

	if (bus == 0)
		return PSYCHO_IMAP_A_SLOT0 + (slot * 8);
	else
		return PSYCHO_IMAP_B_SLOT0 + (slot * 8);
}

#define PSYCHO_IMAP_SCSI	0x1000UL
#define PSYCHO_IMAP_ETH		0x1008UL
#define PSYCHO_IMAP_BPP		0x1010UL
#define PSYCHO_IMAP_AU_REC	0x1018UL
#define PSYCHO_IMAP_AU_PLAY	0x1020UL
#define PSYCHO_IMAP_PFAIL	0x1028UL
#define PSYCHO_IMAP_KMS		0x1030UL
#define PSYCHO_IMAP_FLPY	0x1038UL
#define PSYCHO_IMAP_SHW		0x1040UL
#define PSYCHO_IMAP_KBD		0x1048UL
#define PSYCHO_IMAP_MS		0x1050UL
#define PSYCHO_IMAP_SER		0x1058UL
#define PSYCHO_IMAP_TIM0	0x1060UL
#define PSYCHO_IMAP_TIM1	0x1068UL
#define PSYCHO_IMAP_UE		0x1070UL
#define PSYCHO_IMAP_CE		0x1078UL
#define PSYCHO_IMAP_A_ERR	0x1080UL
#define PSYCHO_IMAP_B_ERR	0x1088UL
#define PSYCHO_IMAP_PMGMT	0x1090UL
#define PSYCHO_IMAP_GFX		0x1098UL
#define PSYCHO_IMAP_EUPA	0x10a0UL

static unsigned long __psycho_onboard_imap_off[] = {
/*0x20*/	PSYCHO_IMAP_SCSI,
/*0x21*/	PSYCHO_IMAP_ETH,
/*0x22*/	PSYCHO_IMAP_BPP,
/*0x23*/	PSYCHO_IMAP_AU_REC,
/*0x24*/	PSYCHO_IMAP_AU_PLAY,
/*0x25*/	PSYCHO_IMAP_PFAIL,
/*0x26*/	PSYCHO_IMAP_KMS,
/*0x27*/	PSYCHO_IMAP_FLPY,
/*0x28*/	PSYCHO_IMAP_SHW,
/*0x29*/	PSYCHO_IMAP_KBD,
/*0x2a*/	PSYCHO_IMAP_MS,
/*0x2b*/	PSYCHO_IMAP_SER,
/*0x2c*/	PSYCHO_IMAP_TIM0,
/*0x2d*/	PSYCHO_IMAP_TIM1,
/*0x2e*/	PSYCHO_IMAP_UE,
/*0x2f*/	PSYCHO_IMAP_CE,
/*0x30*/	PSYCHO_IMAP_A_ERR,
/*0x31*/	PSYCHO_IMAP_B_ERR,
/*0x32*/	PSYCHO_IMAP_PMGMT,
/*0x33*/	PSYCHO_IMAP_GFX,
/*0x34*/	PSYCHO_IMAP_EUPA,
};
#define PSYCHO_ONBOARD_IRQ_BASE		0x20
#define PSYCHO_ONBOARD_IRQ_LAST		0x34
#define psycho_onboard_imap_offset(__ino) \
	__psycho_onboard_imap_off[(__ino) - PSYCHO_ONBOARD_IRQ_BASE]

#define PSYCHO_ICLR_A_SLOT0	0x1400UL
#define PSYCHO_ICLR_SCSI	0x1800UL

#define psycho_iclr_offset(ino)					      \
	((ino & 0x20) ? (PSYCHO_ICLR_SCSI + (((ino) & 0x1f) << 3)) :  \
			(PSYCHO_ICLR_A_SLOT0 + (((ino) & 0x1f)<<3)))

static unsigned int psycho_irq_build(struct device_node *dp,
				     unsigned int ino,
				     void *_data)
{
	unsigned long controller_regs = (unsigned long) _data;
	unsigned long imap, iclr;
	unsigned long imap_off, iclr_off;
	int inofixup = 0;

	ino &= 0x3f;
	if (ino < PSYCHO_ONBOARD_IRQ_BASE) {
		/* PCI slot */
		imap_off = psycho_pcislot_imap_offset(ino);
	} else {
		/* Onboard device */
		if (ino > PSYCHO_ONBOARD_IRQ_LAST) {
			prom_printf("psycho_irq_build: Wacky INO [%x]\n", ino);
			prom_halt();
		}
		imap_off = psycho_onboard_imap_offset(ino);
	}

	/* Now build the IRQ bucket. */
	imap = controller_regs + imap_off;

	iclr_off = psycho_iclr_offset(ino);
	iclr = controller_regs + iclr_off;

	if ((ino & 0x20) == 0)
		inofixup = ino & 0x03;

	return build_irq(inofixup, iclr, imap);
}

static void __init psycho_irq_trans_init(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = psycho_irq_build;

	regs = of_get_property(dp, "reg", NULL);
	dp->irq_trans->data = (void *) regs[2].phys_addr;
}

#define sabre_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})

struct sabre_irq_data {
	unsigned long controller_regs;
	unsigned int pci_first_busno;
};
#define SABRE_CONFIGSPACE	0x001000000UL
#define SABRE_WRSYNC		0x1c20UL

#define SABRE_CONFIG_BASE(CONFIG_SPACE)	\
	(CONFIG_SPACE | (1UL << 24))
#define SABRE_CONFIG_ENCODE(BUS, DEVFN, REG)	\
	(((unsigned long)(BUS)   << 16) |	\
	 ((unsigned long)(DEVFN) << 8)  |	\
	 ((unsigned long)(REG)))

/* When a device lives behind a bridge deeper in the PCI bus topology
 * than APB, a special sequence must run to make sure all pending DMA
 * transfers at the time of IRQ delivery are visible in the coherency
 * domain by the cpu.  This sequence is to perform a read on the far
 * side of the non-APB bridge, then perform a read of Sabre's DMA
 * write-sync register.
 */
static void sabre_wsync_handler(unsigned int ino, void *_arg1, void *_arg2)
{
	unsigned int phys_hi = (unsigned int) (unsigned long) _arg1;
	struct sabre_irq_data *irq_data = _arg2;
	unsigned long controller_regs = irq_data->controller_regs;
	unsigned long sync_reg = controller_regs + SABRE_WRSYNC;
	unsigned long config_space = controller_regs + SABRE_CONFIGSPACE;
	unsigned int bus, devfn;
	u16 _unused;

	config_space = SABRE_CONFIG_BASE(config_space);

	bus = (phys_hi >> 16) & 0xff;
	devfn = (phys_hi >> 8) & 0xff;

	config_space |= SABRE_CONFIG_ENCODE(bus, devfn, 0x00);

	__asm__ __volatile__("membar #Sync\n\t"
			     "lduha [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (_unused)
			     : "r" ((u16 *) config_space),
			       "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");

	sabre_read(sync_reg);
}

#define SABRE_IMAP_A_SLOT0	0x0c00UL
#define SABRE_IMAP_B_SLOT0	0x0c20UL
#define SABRE_IMAP_SCSI		0x1000UL
#define SABRE_IMAP_ETH		0x1008UL
#define SABRE_IMAP_BPP		0x1010UL
#define SABRE_IMAP_AU_REC	0x1018UL
#define SABRE_IMAP_AU_PLAY	0x1020UL
#define SABRE_IMAP_PFAIL	0x1028UL
#define SABRE_IMAP_KMS		0x1030UL
#define SABRE_IMAP_FLPY		0x1038UL
#define SABRE_IMAP_SHW		0x1040UL
#define SABRE_IMAP_KBD		0x1048UL
#define SABRE_IMAP_MS		0x1050UL
#define SABRE_IMAP_SER		0x1058UL
#define SABRE_IMAP_UE		0x1070UL
#define SABRE_IMAP_CE		0x1078UL
#define SABRE_IMAP_PCIERR	0x1080UL
#define SABRE_IMAP_GFX		0x1098UL
#define SABRE_IMAP_EUPA		0x10a0UL
#define SABRE_ICLR_A_SLOT0	0x1400UL
#define SABRE_ICLR_B_SLOT0	0x1480UL
#define SABRE_ICLR_SCSI		0x1800UL
#define SABRE_ICLR_ETH		0x1808UL
#define SABRE_ICLR_BPP		0x1810UL
#define SABRE_ICLR_AU_REC	0x1818UL
#define SABRE_ICLR_AU_PLAY	0x1820UL
#define SABRE_ICLR_PFAIL	0x1828UL
#define SABRE_ICLR_KMS		0x1830UL
#define SABRE_ICLR_FLPY		0x1838UL
#define SABRE_ICLR_SHW		0x1840UL
#define SABRE_ICLR_KBD		0x1848UL
#define SABRE_ICLR_MS		0x1850UL
#define SABRE_ICLR_SER		0x1858UL
#define SABRE_ICLR_UE		0x1870UL
#define SABRE_ICLR_CE		0x1878UL
#define SABRE_ICLR_PCIERR	0x1880UL

static unsigned long sabre_pcislot_imap_offset(unsigned long ino)
{
	unsigned int bus =  (ino & 0x10) >> 4;
	unsigned int slot = (ino & 0x0c) >> 2;

	if (bus == 0)
		return SABRE_IMAP_A_SLOT0 + (slot * 8);
	else
		return SABRE_IMAP_B_SLOT0 + (slot * 8);
}

static unsigned long __sabre_onboard_imap_off[] = {
/*0x20*/	SABRE_IMAP_SCSI,
/*0x21*/	SABRE_IMAP_ETH,
/*0x22*/	SABRE_IMAP_BPP,
/*0x23*/	SABRE_IMAP_AU_REC,
/*0x24*/	SABRE_IMAP_AU_PLAY,
/*0x25*/	SABRE_IMAP_PFAIL,
/*0x26*/	SABRE_IMAP_KMS,
/*0x27*/	SABRE_IMAP_FLPY,
/*0x28*/	SABRE_IMAP_SHW,
/*0x29*/	SABRE_IMAP_KBD,
/*0x2a*/	SABRE_IMAP_MS,
/*0x2b*/	SABRE_IMAP_SER,
/*0x2c*/	0 /* reserved */,
/*0x2d*/	0 /* reserved */,
/*0x2e*/	SABRE_IMAP_UE,
/*0x2f*/	SABRE_IMAP_CE,
/*0x30*/	SABRE_IMAP_PCIERR,
/*0x31*/	0 /* reserved */,
/*0x32*/	0 /* reserved */,
/*0x33*/	SABRE_IMAP_GFX,
/*0x34*/	SABRE_IMAP_EUPA,
};
#define SABRE_ONBOARD_IRQ_BASE		0x20
#define SABRE_ONBOARD_IRQ_LAST		0x30
#define sabre_onboard_imap_offset(__ino) \
	__sabre_onboard_imap_off[(__ino) - SABRE_ONBOARD_IRQ_BASE]

#define sabre_iclr_offset(ino)					      \
	((ino & 0x20) ? (SABRE_ICLR_SCSI + (((ino) & 0x1f) << 3)) :  \
			(SABRE_ICLR_A_SLOT0 + (((ino) & 0x1f)<<3)))

static int sabre_device_needs_wsync(struct device_node *dp)
{
	struct device_node *parent = dp->parent;
	const char *parent_model, *parent_compat;

	/* This traversal up towards the root is meant to
	 * handle two cases:
	 *
	 * 1) non-PCI bus sitting under PCI, such as 'ebus'
	 * 2) the PCI controller interrupts themselves, which
	 *    will use the sabre_irq_build but do not need
	 *    the DMA synchronization handling
	 */
	while (parent) {
		if (!strcmp(parent->type, "pci"))
			break;
		parent = parent->parent;
	}

	if (!parent)
		return 0;

	parent_model = of_get_property(parent,
				       "model", NULL);
	if (parent_model &&
	    (!strcmp(parent_model, "SUNW,sabre") ||
	     !strcmp(parent_model, "SUNW,simba")))
		return 0;

	parent_compat = of_get_property(parent,
					"compatible", NULL);
	if (parent_compat &&
	    (!strcmp(parent_compat, "pci108e,a000") ||
	     !strcmp(parent_compat, "pci108e,a001")))
		return 0;

	return 1;
}

static unsigned int sabre_irq_build(struct device_node *dp,
				    unsigned int ino,
				    void *_data)
{
	struct sabre_irq_data *irq_data = _data;
	unsigned long controller_regs = irq_data->controller_regs;
	const struct linux_prom_pci_registers *regs;
	unsigned long imap, iclr;
	unsigned long imap_off, iclr_off;
	int inofixup = 0;
	int virt_irq;

	ino &= 0x3f;
	if (ino < SABRE_ONBOARD_IRQ_BASE) {
		/* PCI slot */
		imap_off = sabre_pcislot_imap_offset(ino);
	} else {
		/* onboard device */
		if (ino > SABRE_ONBOARD_IRQ_LAST) {
			prom_printf("sabre_irq_build: Wacky INO [%x]\n", ino);
			prom_halt();
		}
		imap_off = sabre_onboard_imap_offset(ino);
	}

	/* Now build the IRQ bucket. */
	imap = controller_regs + imap_off;

	iclr_off = sabre_iclr_offset(ino);
	iclr = controller_regs + iclr_off;

	if ((ino & 0x20) == 0)
		inofixup = ino & 0x03;

	virt_irq = build_irq(inofixup, iclr, imap);

	/* If the parent device is a PCI<->PCI bridge other than
	 * APB, we have to install a pre-handler to ensure that
	 * all pending DMA is drained before the interrupt handler
	 * is run.
	 */
	regs = of_get_property(dp, "reg", NULL);
	if (regs && sabre_device_needs_wsync(dp)) {
		irq_install_pre_handler(virt_irq,
					sabre_wsync_handler,
					(void *) (long) regs->phys_hi,
					(void *) irq_data);
	}

	return virt_irq;
}

static void __init sabre_irq_trans_init(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;
	struct sabre_irq_data *irq_data;
	const u32 *busrange;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = sabre_irq_build;

	irq_data = prom_early_alloc(sizeof(struct sabre_irq_data));

	regs = of_get_property(dp, "reg", NULL);
	irq_data->controller_regs = regs[0].phys_addr;

	busrange = of_get_property(dp, "bus-range", NULL);
	irq_data->pci_first_busno = busrange[0];

	dp->irq_trans->data = irq_data;
}

/* SCHIZO interrupt mapping support.  Unlike Psycho, for this controller the
 * imap/iclr registers are per-PBM.
 */
#define SCHIZO_IMAP_BASE	0x1000UL
#define SCHIZO_ICLR_BASE	0x1400UL

static unsigned long schizo_imap_offset(unsigned long ino)
{
	return SCHIZO_IMAP_BASE + (ino * 8UL);
}

static unsigned long schizo_iclr_offset(unsigned long ino)
{
	return SCHIZO_ICLR_BASE + (ino * 8UL);
}

static unsigned long schizo_ino_to_iclr(unsigned long pbm_regs,
					unsigned int ino)
{

	return pbm_regs + schizo_iclr_offset(ino);
}

static unsigned long schizo_ino_to_imap(unsigned long pbm_regs,
					unsigned int ino)
{
	return pbm_regs + schizo_imap_offset(ino);
}

#define schizo_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define schizo_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory")

static void tomatillo_wsync_handler(unsigned int ino, void *_arg1, void *_arg2)
{
	unsigned long sync_reg = (unsigned long) _arg2;
	u64 mask = 1UL << (ino & IMAP_INO);
	u64 val;
	int limit;

	schizo_write(sync_reg, mask);

	limit = 100000;
	val = 0;
	while (--limit) {
		val = schizo_read(sync_reg);
		if (!(val & mask))
			break;
	}
	if (limit <= 0) {
		printk("tomatillo_wsync_handler: DMA won't sync [%lx:%lx]\n",
		       val, mask);
	}

	if (_arg1) {
		static unsigned char cacheline[64]
			__attribute__ ((aligned (64)));

		__asm__ __volatile__("rd %%fprs, %0\n\t"
				     "or %0, %4, %1\n\t"
				     "wr %1, 0x0, %%fprs\n\t"
				     "stda %%f0, [%5] %6\n\t"
				     "wr %0, 0x0, %%fprs\n\t"
				     "membar #Sync"
				     : "=&r" (mask), "=&r" (val)
				     : "0" (mask), "1" (val),
				     "i" (FPRS_FEF), "r" (&cacheline[0]),
				     "i" (ASI_BLK_COMMIT_P));
	}
}

struct schizo_irq_data {
	unsigned long pbm_regs;
	unsigned long sync_reg;
	u32 portid;
	int chip_version;
};

static unsigned int schizo_irq_build(struct device_node *dp,
				     unsigned int ino,
				     void *_data)
{
	struct schizo_irq_data *irq_data = _data;
	unsigned long pbm_regs = irq_data->pbm_regs;
	unsigned long imap, iclr;
	int ign_fixup;
	int virt_irq;
	int is_tomatillo;

	ino &= 0x3f;

	/* Now build the IRQ bucket. */
	imap = schizo_ino_to_imap(pbm_regs, ino);
	iclr = schizo_ino_to_iclr(pbm_regs, ino);

	/* On Schizo, no inofixup occurs.  This is because each
	 * INO has it's own IMAP register.  On Psycho and Sabre
	 * there is only one IMAP register for each PCI slot even
	 * though four different INOs can be generated by each
	 * PCI slot.
	 *
	 * But, for JBUS variants (essentially, Tomatillo), we have
	 * to fixup the lowest bit of the interrupt group number.
	 */
	ign_fixup = 0;

	is_tomatillo = (irq_data->sync_reg != 0UL);

	if (is_tomatillo) {
		if (irq_data->portid & 1)
			ign_fixup = (1 << 6);
	}

	virt_irq = build_irq(ign_fixup, iclr, imap);

	if (is_tomatillo) {
		irq_install_pre_handler(virt_irq,
					tomatillo_wsync_handler,
					((irq_data->chip_version <= 4) ?
					 (void *) 1 : (void *) 0),
					(void *) irq_data->sync_reg);
	}

	return virt_irq;
}

static void __init __schizo_irq_trans_init(struct device_node *dp,
					   int is_tomatillo)
{
	const struct linux_prom64_registers *regs;
	struct schizo_irq_data *irq_data;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = schizo_irq_build;

	irq_data = prom_early_alloc(sizeof(struct schizo_irq_data));

	regs = of_get_property(dp, "reg", NULL);
	dp->irq_trans->data = irq_data;

	irq_data->pbm_regs = regs[0].phys_addr;
	if (is_tomatillo)
		irq_data->sync_reg = regs[3].phys_addr + 0x1a18UL;
	else
		irq_data->sync_reg = 0UL;
	irq_data->portid = of_getintprop_default(dp, "portid", 0);
	irq_data->chip_version = of_getintprop_default(dp, "version#", 0);
}

static void __init schizo_irq_trans_init(struct device_node *dp)
{
	__schizo_irq_trans_init(dp, 0);
}

static void __init tomatillo_irq_trans_init(struct device_node *dp)
{
	__schizo_irq_trans_init(dp, 1);
}

static unsigned int pci_sun4v_irq_build(struct device_node *dp,
					unsigned int devino,
					void *_data)
{
	u32 devhandle = (u32) (unsigned long) _data;

	return sun4v_build_irq(devhandle, devino);
}

static void __init pci_sun4v_irq_trans_init(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = pci_sun4v_irq_build;

	regs = of_get_property(dp, "reg", NULL);
	dp->irq_trans->data = (void *) (unsigned long)
		((regs->phys_addr >> 32UL) & 0x0fffffff);
}

struct fire_irq_data {
	unsigned long pbm_regs;
	u32 portid;
};

#define FIRE_IMAP_BASE	0x001000
#define FIRE_ICLR_BASE	0x001400

static unsigned long fire_imap_offset(unsigned long ino)
{
	return FIRE_IMAP_BASE + (ino * 8UL);
}

static unsigned long fire_iclr_offset(unsigned long ino)
{
	return FIRE_ICLR_BASE + (ino * 8UL);
}

static unsigned long fire_ino_to_iclr(unsigned long pbm_regs,
					    unsigned int ino)
{
	return pbm_regs + fire_iclr_offset(ino);
}

static unsigned long fire_ino_to_imap(unsigned long pbm_regs,
					    unsigned int ino)
{
	return pbm_regs + fire_imap_offset(ino);
}

static unsigned int fire_irq_build(struct device_node *dp,
					 unsigned int ino,
					 void *_data)
{
	struct fire_irq_data *irq_data = _data;
	unsigned long pbm_regs = irq_data->pbm_regs;
	unsigned long imap, iclr;
	unsigned long int_ctrlr;

	ino &= 0x3f;

	/* Now build the IRQ bucket. */
	imap = fire_ino_to_imap(pbm_regs, ino);
	iclr = fire_ino_to_iclr(pbm_regs, ino);

	/* Set the interrupt controller number.  */
	int_ctrlr = 1 << 6;
	upa_writeq(int_ctrlr, imap);

	/* The interrupt map registers do not have an INO field
	 * like other chips do.  They return zero in the INO
	 * field, and the interrupt controller number is controlled
	 * in bits 6 to 9.  So in order for build_irq() to get
	 * the INO right we pass it in as part of the fixup
	 * which will get added to the map register zero value
	 * read by build_irq().
	 */
	ino |= (irq_data->portid << 6);
	ino -= int_ctrlr;
	return build_irq(ino, iclr, imap);
}

static void __init fire_irq_trans_init(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;
	struct fire_irq_data *irq_data;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = fire_irq_build;

	irq_data = prom_early_alloc(sizeof(struct fire_irq_data));

	regs = of_get_property(dp, "reg", NULL);
	dp->irq_trans->data = irq_data;

	irq_data->pbm_regs = regs[0].phys_addr;
	irq_data->portid = of_getintprop_default(dp, "portid", 0);
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_SBUS
/* INO number to IMAP register offset for SYSIO external IRQ's.
 * This should conform to both Sunfire/Wildfire server and Fusion
 * desktop designs.
 */
#define SYSIO_IMAP_SLOT0	0x2c00UL
#define SYSIO_IMAP_SLOT1	0x2c08UL
#define SYSIO_IMAP_SLOT2	0x2c10UL
#define SYSIO_IMAP_SLOT3	0x2c18UL
#define SYSIO_IMAP_SCSI		0x3000UL
#define SYSIO_IMAP_ETH		0x3008UL
#define SYSIO_IMAP_BPP		0x3010UL
#define SYSIO_IMAP_AUDIO	0x3018UL
#define SYSIO_IMAP_PFAIL	0x3020UL
#define SYSIO_IMAP_KMS		0x3028UL
#define SYSIO_IMAP_FLPY		0x3030UL
#define SYSIO_IMAP_SHW		0x3038UL
#define SYSIO_IMAP_KBD		0x3040UL
#define SYSIO_IMAP_MS		0x3048UL
#define SYSIO_IMAP_SER		0x3050UL
#define SYSIO_IMAP_TIM0		0x3060UL
#define SYSIO_IMAP_TIM1		0x3068UL
#define SYSIO_IMAP_UE		0x3070UL
#define SYSIO_IMAP_CE		0x3078UL
#define SYSIO_IMAP_SBERR	0x3080UL
#define SYSIO_IMAP_PMGMT	0x3088UL
#define SYSIO_IMAP_GFX		0x3090UL
#define SYSIO_IMAP_EUPA		0x3098UL

#define bogon     ((unsigned long) -1)
static unsigned long sysio_irq_offsets[] = {
	/* SBUS Slot 0 --> 3, level 1 --> 7 */
	SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0,
	SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0, SYSIO_IMAP_SLOT0,
	SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1,
	SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1, SYSIO_IMAP_SLOT1,
	SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2,
	SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2, SYSIO_IMAP_SLOT2,
	SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3,
	SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3, SYSIO_IMAP_SLOT3,

	/* Onboard devices (not relevant/used on SunFire). */
	SYSIO_IMAP_SCSI,
	SYSIO_IMAP_ETH,
	SYSIO_IMAP_BPP,
	bogon,
	SYSIO_IMAP_AUDIO,
	SYSIO_IMAP_PFAIL,
	bogon,
	bogon,
	SYSIO_IMAP_KMS,
	SYSIO_IMAP_FLPY,
	SYSIO_IMAP_SHW,
	SYSIO_IMAP_KBD,
	SYSIO_IMAP_MS,
	SYSIO_IMAP_SER,
	bogon,
	bogon,
	SYSIO_IMAP_TIM0,
	SYSIO_IMAP_TIM1,
	bogon,
	bogon,
	SYSIO_IMAP_UE,
	SYSIO_IMAP_CE,
	SYSIO_IMAP_SBERR,
	SYSIO_IMAP_PMGMT,
	SYSIO_IMAP_GFX,
	SYSIO_IMAP_EUPA,
};

#undef bogon

#define NUM_SYSIO_OFFSETS ARRAY_SIZE(sysio_irq_offsets)

/* Convert Interrupt Mapping register pointer to associated
 * Interrupt Clear register pointer, SYSIO specific version.
 */
#define SYSIO_ICLR_UNUSED0	0x3400UL
#define SYSIO_ICLR_SLOT0	0x3408UL
#define SYSIO_ICLR_SLOT1	0x3448UL
#define SYSIO_ICLR_SLOT2	0x3488UL
#define SYSIO_ICLR_SLOT3	0x34c8UL
static unsigned long sysio_imap_to_iclr(unsigned long imap)
{
	unsigned long diff = SYSIO_ICLR_UNUSED0 - SYSIO_IMAP_SLOT0;
	return imap + diff;
}

static unsigned int sbus_of_build_irq(struct device_node *dp,
				      unsigned int ino,
				      void *_data)
{
	unsigned long reg_base = (unsigned long) _data;
	const struct linux_prom_registers *regs;
	unsigned long imap, iclr;
	int sbus_slot = 0;
	int sbus_level = 0;

	ino &= 0x3f;

	regs = of_get_property(dp, "reg", NULL);
	if (regs)
		sbus_slot = regs->which_io;

	if (ino < 0x20)
		ino += (sbus_slot * 8);

	imap = sysio_irq_offsets[ino];
	if (imap == ((unsigned long)-1)) {
		prom_printf("get_irq_translations: Bad SYSIO INO[%x]\n",
			    ino);
		prom_halt();
	}
	imap += reg_base;

	/* SYSIO inconsistency.  For external SLOTS, we have to select
	 * the right ICLR register based upon the lower SBUS irq level
	 * bits.
	 */
	if (ino >= 0x20) {
		iclr = sysio_imap_to_iclr(imap);
	} else {
		sbus_level = ino & 0x7;

		switch(sbus_slot) {
		case 0:
			iclr = reg_base + SYSIO_ICLR_SLOT0;
			break;
		case 1:
			iclr = reg_base + SYSIO_ICLR_SLOT1;
			break;
		case 2:
			iclr = reg_base + SYSIO_ICLR_SLOT2;
			break;
		default:
		case 3:
			iclr = reg_base + SYSIO_ICLR_SLOT3;
			break;
		};

		iclr += ((unsigned long)sbus_level - 1UL) * 8UL;
	}
	return build_irq(sbus_level, iclr, imap);
}

static void __init sbus_irq_trans_init(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = sbus_of_build_irq;

	regs = of_get_property(dp, "reg", NULL);
	dp->irq_trans->data = (void *) (unsigned long) regs->phys_addr;
}
#endif /* CONFIG_SBUS */


static unsigned int central_build_irq(struct device_node *dp,
				      unsigned int ino,
				      void *_data)
{
	struct device_node *central_dp = _data;
	struct of_device *central_op = of_find_device_by_node(central_dp);
	struct resource *res;
	unsigned long imap, iclr;
	u32 tmp;

	if (!strcmp(dp->name, "eeprom")) {
		res = &central_op->resource[5];
	} else if (!strcmp(dp->name, "zs")) {
		res = &central_op->resource[4];
	} else if (!strcmp(dp->name, "clock-board")) {
		res = &central_op->resource[3];
	} else {
		return ino;
	}

	imap = res->start + 0x00UL;
	iclr = res->start + 0x10UL;

	/* Set the INO state to idle, and disable.  */
	upa_writel(0, iclr);
	upa_readl(iclr);

	tmp = upa_readl(imap);
	tmp &= ~0x80000000;
	upa_writel(tmp, imap);

	return build_irq(0, iclr, imap);
}

static void __init central_irq_trans_init(struct device_node *dp)
{
	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = central_build_irq;

	dp->irq_trans->data = dp;
}

struct irq_trans {
	const char *name;
	void (*init)(struct device_node *);
};

#ifdef CONFIG_PCI
static struct irq_trans __initdata pci_irq_trans_table[] = {
	{ "SUNW,sabre", sabre_irq_trans_init },
	{ "pci108e,a000", sabre_irq_trans_init },
	{ "pci108e,a001", sabre_irq_trans_init },
	{ "SUNW,psycho", psycho_irq_trans_init },
	{ "pci108e,8000", psycho_irq_trans_init },
	{ "SUNW,schizo", schizo_irq_trans_init },
	{ "pci108e,8001", schizo_irq_trans_init },
	{ "SUNW,schizo+", schizo_irq_trans_init },
	{ "pci108e,8002", schizo_irq_trans_init },
	{ "SUNW,tomatillo", tomatillo_irq_trans_init },
	{ "pci108e,a801", tomatillo_irq_trans_init },
	{ "SUNW,sun4v-pci", pci_sun4v_irq_trans_init },
	{ "pciex108e,80f0", fire_irq_trans_init },
};
#endif

static unsigned int sun4v_vdev_irq_build(struct device_node *dp,
					 unsigned int devino,
					 void *_data)
{
	u32 devhandle = (u32) (unsigned long) _data;

	return sun4v_build_irq(devhandle, devino);
}

static void __init sun4v_vdev_irq_trans_init(struct device_node *dp)
{
	const struct linux_prom64_registers *regs;

	dp->irq_trans = prom_early_alloc(sizeof(struct of_irq_controller));
	dp->irq_trans->irq_build = sun4v_vdev_irq_build;

	regs = of_get_property(dp, "reg", NULL);
	dp->irq_trans->data = (void *) (unsigned long)
		((regs->phys_addr >> 32UL) & 0x0fffffff);
}

static void __init irq_trans_init(struct device_node *dp)
{
#ifdef CONFIG_PCI
	const char *model;
	int i;
#endif

#ifdef CONFIG_PCI
	model = of_get_property(dp, "model", NULL);
	if (!model)
		model = of_get_property(dp, "compatible", NULL);
	if (model) {
		for (i = 0; i < ARRAY_SIZE(pci_irq_trans_table); i++) {
			struct irq_trans *t = &pci_irq_trans_table[i];

			if (!strcmp(model, t->name))
				return t->init(dp);
		}
	}
#endif
#ifdef CONFIG_SBUS
	if (!strcmp(dp->name, "sbus") ||
	    !strcmp(dp->name, "sbi"))
		return sbus_irq_trans_init(dp);
#endif
	if (!strcmp(dp->name, "fhc") &&
	    !strcmp(dp->parent->name, "central"))
		return central_irq_trans_init(dp);
	if (!strcmp(dp->name, "virtual-devices"))
		return sun4v_vdev_irq_trans_init(dp);
}

static int is_root_node(const struct device_node *dp)
{
	if (!dp)
		return 0;

	return (dp->parent == NULL);
}

/* The following routines deal with the black magic of fully naming a
 * node.
 *
 * Certain well known named nodes are just the simple name string.
 *
 * Actual devices have an address specifier appended to the base name
 * string, like this "foo@addr".  The "addr" can be in any number of
 * formats, and the platform plus the type of the node determine the
 * format and how it is constructed.
 *
 * For children of the ROOT node, the naming convention is fixed and
 * determined by whether this is a sun4u or sun4v system.
 *
 * For children of other nodes, it is bus type specific.  So
 * we walk up the tree until we discover a "device_type" property
 * we recognize and we go from there.
 *
 * As an example, the boot device on my workstation has a full path:
 *
 *	/pci@1e,600000/ide@d/disk@0,0:c
 */
static void __init sun4v_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *rprop;
	u32 high_bits, low_bits, type;

	rprop = of_find_property(dp, "reg", NULL);
	if (!rprop)
		return;

	regs = rprop->value;
	if (!is_root_node(dp->parent)) {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			(unsigned int) (regs->phys_addr >> 32UL),
			(unsigned int) (regs->phys_addr & 0xffffffffUL));
		return;
	}

	type = regs->phys_addr >> 60UL;
	high_bits = (regs->phys_addr >> 32UL) & 0x0fffffffUL;
	low_bits = (regs->phys_addr & 0xffffffffUL);

	if (type == 0 || type == 8) {
		const char *prefix = (type == 0) ? "m" : "i";

		if (low_bits)
			sprintf(tmp_buf, "%s@%s%x,%x",
				dp->name, prefix,
				high_bits, low_bits);
		else
			sprintf(tmp_buf, "%s@%s%x",
				dp->name,
				prefix,
				high_bits);
	} else if (type == 12) {
		sprintf(tmp_buf, "%s@%x",
			dp->name, high_bits);
	}
}

static void __init sun4u_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	if (!is_root_node(dp->parent)) {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			(unsigned int) (regs->phys_addr >> 32UL),
			(unsigned int) (regs->phys_addr & 0xffffffffUL));
		return;
	}

	prop = of_find_property(dp, "upa-portid", NULL);
	if (!prop)
		prop = of_find_property(dp, "portid", NULL);
	if (prop) {
		unsigned long mask = 0xffffffffUL;

		if (tlb_type >= cheetah)
			mask = 0x7fffff;

		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			*(u32 *)prop->value,
			(unsigned int) (regs->phys_addr & mask));
	}
}

/* "name@slot,offset"  */
static void __init sbus_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		regs->which_io,
		regs->phys_addr);
}

/* "name@devnum[,func]" */
static void __init pci_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom_pci_registers *regs;
	struct property *prop;
	unsigned int devfn;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	devfn = (regs->phys_hi >> 8) & 0xff;
	if (devfn & 0x07) {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			devfn >> 3,
			devfn & 0x07);
	} else {
		sprintf(tmp_buf, "%s@%x",
			dp->name,
			devfn >> 3);
	}
}

/* "name@UPA_PORTID,offset" */
static void __init upa_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	prop = of_find_property(dp, "upa-portid", NULL);
	if (!prop)
		return;

	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		*(u32 *) prop->value,
		(unsigned int) (regs->phys_addr & 0xffffffffUL));
}

/* "name@reg" */
static void __init vdev_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	sprintf(tmp_buf, "%s@%x", dp->name, *regs);
}

/* "name@addrhi,addrlo" */
static void __init ebus_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		(unsigned int) (regs->phys_addr >> 32UL),
		(unsigned int) (regs->phys_addr & 0xffffffffUL));
}

/* "name@bus,addr" */
static void __init i2c_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	/* This actually isn't right... should look at the #address-cells
	 * property of the i2c bus node etc. etc.
	 */
	sprintf(tmp_buf, "%s@%x,%x",
		dp->name, regs[0], regs[1]);
}

/* "name@reg0[,reg1]" */
static void __init usb_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	if (prop->length == sizeof(u32) || regs[1] == 1) {
		sprintf(tmp_buf, "%s@%x",
			dp->name, regs[0]);
	} else {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name, regs[0], regs[1]);
	}
}

/* "name@reg0reg1[,reg2reg3]" */
static void __init ieee1394_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	if (regs[2] || regs[3]) {
		sprintf(tmp_buf, "%s@%08x%08x,%04x%08x",
			dp->name, regs[0], regs[1], regs[2], regs[3]);
	} else {
		sprintf(tmp_buf, "%s@%08x%08x",
			dp->name, regs[0], regs[1]);
	}
}

static void __init __build_path_component(struct device_node *dp, char *tmp_buf)
{
	struct device_node *parent = dp->parent;

	if (parent != NULL) {
		if (!strcmp(parent->type, "pci") ||
		    !strcmp(parent->type, "pciex"))
			return pci_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "sbus"))
			return sbus_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "upa"))
			return upa_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "ebus"))
			return ebus_path_component(dp, tmp_buf);
		if (!strcmp(parent->name, "usb") ||
		    !strcmp(parent->name, "hub"))
			return usb_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "i2c"))
			return i2c_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "firewire"))
			return ieee1394_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "virtual-devices"))
			return vdev_path_component(dp, tmp_buf);

		/* "isa" is handled with platform naming */
	}

	/* Use platform naming convention.  */
	if (tlb_type == hypervisor)
		return sun4v_path_component(dp, tmp_buf);
	else
		return sun4u_path_component(dp, tmp_buf);
}

static char * __init build_path_component(struct device_node *dp)
{
	char tmp_buf[64], *n;

	tmp_buf[0] = '\0';
	__build_path_component(dp, tmp_buf);
	if (tmp_buf[0] == '\0')
		strcpy(tmp_buf, dp->name);

	n = prom_early_alloc(strlen(tmp_buf) + 1);
	strcpy(n, tmp_buf);

	return n;
}

static char * __init build_full_name(struct device_node *dp)
{
	int len, ourlen, plen;
	char *n;

	plen = strlen(dp->parent->full_name);
	ourlen = strlen(dp->path_component_name);
	len = ourlen + plen + 2;

	n = prom_early_alloc(len);
	strcpy(n, dp->parent->full_name);
	if (!is_root_node(dp->parent)) {
		strcpy(n + plen, "/");
		plen++;
	}
	strcpy(n + plen, dp->path_component_name);

	return n;
}

static unsigned int unique_id;

static struct property * __init build_one_prop(phandle node, char *prev, char *special_name, void *special_val, int special_len)
{
	static struct property *tmp = NULL;
	struct property *p;

	if (tmp) {
		p = tmp;
		memset(p, 0, sizeof(*p) + 32);
		tmp = NULL;
	} else {
		p = prom_early_alloc(sizeof(struct property) + 32);
		p->unique_id = unique_id++;
	}

	p->name = (char *) (p + 1);
	if (special_name) {
		strcpy(p->name, special_name);
		p->length = special_len;
		p->value = prom_early_alloc(special_len);
		memcpy(p->value, special_val, special_len);
	} else {
		if (prev == NULL) {
			prom_firstprop(node, p->name);
		} else {
			prom_nextprop(node, prev, p->name);
		}
		if (strlen(p->name) == 0) {
			tmp = p;
			return NULL;
		}
		p->length = prom_getproplen(node, p->name);
		if (p->length <= 0) {
			p->length = 0;
		} else {
			p->value = prom_early_alloc(p->length + 1);
			prom_getproperty(node, p->name, p->value, p->length);
			((unsigned char *)p->value)[p->length] = '\0';
		}
	}
	return p;
}

static struct property * __init build_prop_list(phandle node)
{
	struct property *head, *tail;

	head = tail = build_one_prop(node, NULL,
				     ".node", &node, sizeof(node));

	tail->next = build_one_prop(node, NULL, NULL, NULL, 0);
	tail = tail->next;
	while(tail) {
		tail->next = build_one_prop(node, tail->name,
					    NULL, NULL, 0);
		tail = tail->next;
	}

	return head;
}

static char * __init get_one_property(phandle node, const char *name)
{
	char *buf = "<NULL>";
	int len;

	len = prom_getproplen(node, name);
	if (len > 0) {
		buf = prom_early_alloc(len);
		prom_getproperty(node, name, buf, len);
	}

	return buf;
}

static struct device_node * __init create_node(phandle node, struct device_node *parent)
{
	struct device_node *dp;

	if (!node)
		return NULL;

	dp = prom_early_alloc(sizeof(*dp));
	dp->unique_id = unique_id++;
	dp->parent = parent;

	kref_init(&dp->kref);

	dp->name = get_one_property(node, "name");
	dp->type = get_one_property(node, "device_type");
	dp->node = node;

	dp->properties = build_prop_list(node);

	irq_trans_init(dp);

	return dp;
}

static struct device_node * __init build_tree(struct device_node *parent, phandle node, struct device_node ***nextp)
{
	struct device_node *ret = NULL, *prev_sibling = NULL;
	struct device_node *dp;

	while (1) {
		dp = create_node(node, parent);
		if (!dp)
			break;

		if (prev_sibling)
			prev_sibling->sibling = dp;

		if (!ret)
			ret = dp;
		prev_sibling = dp;

		*(*nextp) = dp;
		*nextp = &dp->allnext;

		dp->path_component_name = build_path_component(dp);
		dp->full_name = build_full_name(dp);

		dp->child = build_tree(dp, prom_getchild(node), nextp);

		node = prom_getsibling(node);
	}

	return ret;
}

static const char *get_mid_prop(void)
{
	return (tlb_type == spitfire ? "upa-portid" : "portid");
}

struct device_node *of_find_node_by_cpuid(int cpuid)
{
	struct device_node *dp;
	const char *mid_prop = get_mid_prop();

	for_each_node_by_type(dp, "cpu") {
		int id = of_getintprop_default(dp, mid_prop, -1);
		const char *this_mid_prop = mid_prop;

		if (id < 0) {
			this_mid_prop = "cpuid";
			id = of_getintprop_default(dp, this_mid_prop, -1);
		}

		if (id < 0) {
			prom_printf("OF: Serious problem, cpu lacks "
				    "%s property", this_mid_prop);
			prom_halt();
		}
		if (cpuid == id)
			return dp;
	}
	return NULL;
}

static void __init of_fill_in_cpu_data(void)
{
	struct device_node *dp;
	const char *mid_prop = get_mid_prop();

	ncpus_probed = 0;
	for_each_node_by_type(dp, "cpu") {
		int cpuid = of_getintprop_default(dp, mid_prop, -1);
		const char *this_mid_prop = mid_prop;
		struct device_node *portid_parent;
		int portid = -1;

		portid_parent = NULL;
		if (cpuid < 0) {
			this_mid_prop = "cpuid";
			cpuid = of_getintprop_default(dp, this_mid_prop, -1);
			if (cpuid >= 0) {
				int limit = 2;

				portid_parent = dp;
				while (limit--) {
					portid_parent = portid_parent->parent;
					if (!portid_parent)
						break;
					portid = of_getintprop_default(portid_parent,
								       "portid", -1);
					if (portid >= 0)
						break;
				}
			}
		}

		if (cpuid < 0) {
			prom_printf("OF: Serious problem, cpu lacks "
				    "%s property", this_mid_prop);
			prom_halt();
		}

		ncpus_probed++;

#ifdef CONFIG_SMP
		if (cpuid >= NR_CPUS)
			continue;
#else
		/* On uniprocessor we only want the values for the
		 * real physical cpu the kernel booted onto, however
		 * cpu_data() only has one entry at index 0.
		 */
		if (cpuid != real_hard_smp_processor_id())
			continue;
		cpuid = 0;
#endif

		cpu_data(cpuid).clock_tick =
			of_getintprop_default(dp, "clock-frequency", 0);

		if (portid_parent) {
			cpu_data(cpuid).dcache_size =
				of_getintprop_default(dp, "l1-dcache-size",
						      16 * 1024);
			cpu_data(cpuid).dcache_line_size =
				of_getintprop_default(dp, "l1-dcache-line-size",
						      32);
			cpu_data(cpuid).icache_size =
				of_getintprop_default(dp, "l1-icache-size",
						      8 * 1024);
			cpu_data(cpuid).icache_line_size =
				of_getintprop_default(dp, "l1-icache-line-size",
						      32);
			cpu_data(cpuid).ecache_size =
				of_getintprop_default(dp, "l2-cache-size", 0);
			cpu_data(cpuid).ecache_line_size =
				of_getintprop_default(dp, "l2-cache-line-size", 0);
			if (!cpu_data(cpuid).ecache_size ||
			    !cpu_data(cpuid).ecache_line_size) {
				cpu_data(cpuid).ecache_size =
					of_getintprop_default(portid_parent,
							      "l2-cache-size",
							      (4 * 1024 * 1024));
				cpu_data(cpuid).ecache_line_size =
					of_getintprop_default(portid_parent,
							      "l2-cache-line-size", 64);
			}

			cpu_data(cpuid).core_id = portid + 1;
			cpu_data(cpuid).proc_id = portid;
#ifdef CONFIG_SMP
			sparc64_multi_core = 1;
#endif
		} else {
			cpu_data(cpuid).dcache_size =
				of_getintprop_default(dp, "dcache-size", 16 * 1024);
			cpu_data(cpuid).dcache_line_size =
				of_getintprop_default(dp, "dcache-line-size", 32);

			cpu_data(cpuid).icache_size =
				of_getintprop_default(dp, "icache-size", 16 * 1024);
			cpu_data(cpuid).icache_line_size =
				of_getintprop_default(dp, "icache-line-size", 32);

			cpu_data(cpuid).ecache_size =
				of_getintprop_default(dp, "ecache-size",
						      (4 * 1024 * 1024));
			cpu_data(cpuid).ecache_line_size =
				of_getintprop_default(dp, "ecache-line-size", 64);

			cpu_data(cpuid).core_id = 0;
			cpu_data(cpuid).proc_id = -1;
		}

#ifdef CONFIG_SMP
		cpu_set(cpuid, cpu_present_map);
		cpu_set(cpuid, cpu_possible_map);
#endif
	}

	smp_fill_in_sib_core_maps();
}

void __init prom_build_devicetree(void)
{
	struct device_node **nextp;

	allnodes = create_node(prom_root_node, NULL);
	allnodes->path_component_name = "";
	allnodes->full_name = "/";

	nextp = &allnodes->allnext;
	allnodes->child = build_tree(allnodes,
				     prom_getchild(allnodes->node),
				     &nextp);
	printk("PROM: Built device tree with %u bytes of memory.\n",
	       prom_early_allocated);

	if (tlb_type != hypervisor)
		of_fill_in_cpu_data();
}
