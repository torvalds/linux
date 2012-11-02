/*
 * omap iommu: omap2/3 architecture specific functions
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>,
 *		Paul Mundt and Toshihiro Kobayashi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/omap-iommu.h>
#include <linux/slab.h>
#include <linux/stringify.h>

#include <plat/iommu.h>

/*
 * omap2 architecture specific register bit definitions
 */
#define IOMMU_ARCH_VERSION	0x00000011

/* SYSCONF */
#define MMU_SYS_IDLE_SHIFT	3
#define MMU_SYS_IDLE_FORCE	(0 << MMU_SYS_IDLE_SHIFT)
#define MMU_SYS_IDLE_NONE	(1 << MMU_SYS_IDLE_SHIFT)
#define MMU_SYS_IDLE_SMART	(2 << MMU_SYS_IDLE_SHIFT)
#define MMU_SYS_IDLE_MASK	(3 << MMU_SYS_IDLE_SHIFT)

#define MMU_SYS_SOFTRESET	(1 << 1)
#define MMU_SYS_AUTOIDLE	1

/* SYSSTATUS */
#define MMU_SYS_RESETDONE	1

/* IRQSTATUS & IRQENABLE */
#define MMU_IRQ_MULTIHITFAULT	(1 << 4)
#define MMU_IRQ_TABLEWALKFAULT	(1 << 3)
#define MMU_IRQ_EMUMISS		(1 << 2)
#define MMU_IRQ_TRANSLATIONFAULT	(1 << 1)
#define MMU_IRQ_TLBMISS		(1 << 0)

#define __MMU_IRQ_FAULT		\
	(MMU_IRQ_MULTIHITFAULT | MMU_IRQ_EMUMISS | MMU_IRQ_TRANSLATIONFAULT)
#define MMU_IRQ_MASK		\
	(__MMU_IRQ_FAULT | MMU_IRQ_TABLEWALKFAULT | MMU_IRQ_TLBMISS)
#define MMU_IRQ_TWL_MASK	(__MMU_IRQ_FAULT | MMU_IRQ_TABLEWALKFAULT)
#define MMU_IRQ_TLB_MISS_MASK	(__MMU_IRQ_FAULT | MMU_IRQ_TLBMISS)

/* MMU_CNTL */
#define MMU_CNTL_SHIFT		1
#define MMU_CNTL_MASK		(7 << MMU_CNTL_SHIFT)
#define MMU_CNTL_EML_TLB	(1 << 3)
#define MMU_CNTL_TWL_EN		(1 << 2)
#define MMU_CNTL_MMU_EN		(1 << 1)

#define get_cam_va_mask(pgsz)				\
	(((pgsz) == MMU_CAM_PGSZ_16M) ? 0xff000000 :	\
	 ((pgsz) == MMU_CAM_PGSZ_1M)  ? 0xfff00000 :	\
	 ((pgsz) == MMU_CAM_PGSZ_64K) ? 0xffff0000 :	\
	 ((pgsz) == MMU_CAM_PGSZ_4K)  ? 0xfffff000 : 0)


static void __iommu_set_twl(struct omap_iommu *obj, bool on)
{
	u32 l = iommu_read_reg(obj, MMU_CNTL);

	if (on)
		iommu_write_reg(obj, MMU_IRQ_TWL_MASK, MMU_IRQENABLE);
	else
		iommu_write_reg(obj, MMU_IRQ_TLB_MISS_MASK, MMU_IRQENABLE);

	l &= ~MMU_CNTL_MASK;
	if (on)
		l |= (MMU_CNTL_MMU_EN | MMU_CNTL_TWL_EN);
	else
		l |= (MMU_CNTL_MMU_EN);

	iommu_write_reg(obj, l, MMU_CNTL);
}


static int omap2_iommu_enable(struct omap_iommu *obj)
{
	u32 l, pa;
	unsigned long timeout;

	if (!obj->iopgd || !IS_ALIGNED((u32)obj->iopgd,  SZ_16K))
		return -EINVAL;

	pa = virt_to_phys(obj->iopgd);
	if (!IS_ALIGNED(pa, SZ_16K))
		return -EINVAL;

	iommu_write_reg(obj, MMU_SYS_SOFTRESET, MMU_SYSCONFIG);

	timeout = jiffies + msecs_to_jiffies(20);
	do {
		l = iommu_read_reg(obj, MMU_SYSSTATUS);
		if (l & MMU_SYS_RESETDONE)
			break;
	} while (!time_after(jiffies, timeout));

	if (!(l & MMU_SYS_RESETDONE)) {
		dev_err(obj->dev, "can't take mmu out of reset\n");
		return -ENODEV;
	}

	l = iommu_read_reg(obj, MMU_REVISION);
	dev_info(obj->dev, "%s: version %d.%d\n", obj->name,
		 (l >> 4) & 0xf, l & 0xf);

	l = iommu_read_reg(obj, MMU_SYSCONFIG);
	l &= ~MMU_SYS_IDLE_MASK;
	l |= (MMU_SYS_IDLE_SMART | MMU_SYS_AUTOIDLE);
	iommu_write_reg(obj, l, MMU_SYSCONFIG);

	iommu_write_reg(obj, pa, MMU_TTB);

	__iommu_set_twl(obj, true);

	return 0;
}

static void omap2_iommu_disable(struct omap_iommu *obj)
{
	u32 l = iommu_read_reg(obj, MMU_CNTL);

	l &= ~MMU_CNTL_MASK;
	iommu_write_reg(obj, l, MMU_CNTL);
	iommu_write_reg(obj, MMU_SYS_IDLE_FORCE, MMU_SYSCONFIG);

	dev_dbg(obj->dev, "%s is shutting down\n", obj->name);
}

static void omap2_iommu_set_twl(struct omap_iommu *obj, bool on)
{
	__iommu_set_twl(obj, false);
}

static u32 omap2_iommu_fault_isr(struct omap_iommu *obj, u32 *ra)
{
	u32 stat, da;
	u32 errs = 0;

	stat = iommu_read_reg(obj, MMU_IRQSTATUS);
	stat &= MMU_IRQ_MASK;
	if (!stat) {
		*ra = 0;
		return 0;
	}

	da = iommu_read_reg(obj, MMU_FAULT_AD);
	*ra = da;

	if (stat & MMU_IRQ_TLBMISS)
		errs |= OMAP_IOMMU_ERR_TLB_MISS;
	if (stat & MMU_IRQ_TRANSLATIONFAULT)
		errs |= OMAP_IOMMU_ERR_TRANS_FAULT;
	if (stat & MMU_IRQ_EMUMISS)
		errs |= OMAP_IOMMU_ERR_EMU_MISS;
	if (stat & MMU_IRQ_TABLEWALKFAULT)
		errs |= OMAP_IOMMU_ERR_TBLWALK_FAULT;
	if (stat & MMU_IRQ_MULTIHITFAULT)
		errs |= OMAP_IOMMU_ERR_MULTIHIT_FAULT;
	iommu_write_reg(obj, stat, MMU_IRQSTATUS);

	return errs;
}

static void omap2_tlb_read_cr(struct omap_iommu *obj, struct cr_regs *cr)
{
	cr->cam = iommu_read_reg(obj, MMU_READ_CAM);
	cr->ram = iommu_read_reg(obj, MMU_READ_RAM);
}

static void omap2_tlb_load_cr(struct omap_iommu *obj, struct cr_regs *cr)
{
	iommu_write_reg(obj, cr->cam | MMU_CAM_V, MMU_CAM);
	iommu_write_reg(obj, cr->ram, MMU_RAM);
}

static u32 omap2_cr_to_virt(struct cr_regs *cr)
{
	u32 page_size = cr->cam & MMU_CAM_PGSZ_MASK;
	u32 mask = get_cam_va_mask(cr->cam & page_size);

	return cr->cam & mask;
}

static struct cr_regs *omap2_alloc_cr(struct omap_iommu *obj,
						struct iotlb_entry *e)
{
	struct cr_regs *cr;

	if (e->da & ~(get_cam_va_mask(e->pgsz))) {
		dev_err(obj->dev, "%s:\twrong alignment: %08x\n", __func__,
			e->da);
		return ERR_PTR(-EINVAL);
	}

	cr = kmalloc(sizeof(*cr), GFP_KERNEL);
	if (!cr)
		return ERR_PTR(-ENOMEM);

	cr->cam = (e->da & MMU_CAM_VATAG_MASK) | e->prsvd | e->pgsz | e->valid;
	cr->ram = e->pa | e->endian | e->elsz | e->mixed;

	return cr;
}

static inline int omap2_cr_valid(struct cr_regs *cr)
{
	return cr->cam & MMU_CAM_V;
}

static u32 omap2_get_pte_attr(struct iotlb_entry *e)
{
	u32 attr;

	attr = e->mixed << 5;
	attr |= e->endian;
	attr |= e->elsz >> 3;
	attr <<= (((e->pgsz == MMU_CAM_PGSZ_4K) ||
			(e->pgsz == MMU_CAM_PGSZ_64K)) ? 0 : 6);
	return attr;
}

static ssize_t
omap2_dump_cr(struct omap_iommu *obj, struct cr_regs *cr, char *buf)
{
	char *p = buf;

	/* FIXME: Need more detail analysis of cam/ram */
	p += sprintf(p, "%08x %08x %01x\n", cr->cam, cr->ram,
					(cr->cam & MMU_CAM_P) ? 1 : 0);

	return p - buf;
}

#define pr_reg(name)							\
	do {								\
		ssize_t bytes;						\
		const char *str = "%20s: %08x\n";			\
		const int maxcol = 32;					\
		bytes = snprintf(p, maxcol, str, __stringify(name),	\
				 iommu_read_reg(obj, MMU_##name));	\
		p += bytes;						\
		len -= bytes;						\
		if (len < maxcol)					\
			goto out;					\
	} while (0)

static ssize_t
omap2_iommu_dump_ctx(struct omap_iommu *obj, char *buf, ssize_t len)
{
	char *p = buf;

	pr_reg(REVISION);
	pr_reg(SYSCONFIG);
	pr_reg(SYSSTATUS);
	pr_reg(IRQSTATUS);
	pr_reg(IRQENABLE);
	pr_reg(WALKING_ST);
	pr_reg(CNTL);
	pr_reg(FAULT_AD);
	pr_reg(TTB);
	pr_reg(LOCK);
	pr_reg(LD_TLB);
	pr_reg(CAM);
	pr_reg(RAM);
	pr_reg(GFLUSH);
	pr_reg(FLUSH_ENTRY);
	pr_reg(READ_CAM);
	pr_reg(READ_RAM);
	pr_reg(EMU_FAULT_AD);
out:
	return p - buf;
}

static void omap2_iommu_save_ctx(struct omap_iommu *obj)
{
	int i;
	u32 *p = obj->ctx;

	for (i = 0; i < (MMU_REG_SIZE / sizeof(u32)); i++) {
		p[i] = iommu_read_reg(obj, i * sizeof(u32));
		dev_dbg(obj->dev, "%s\t[%02d] %08x\n", __func__, i, p[i]);
	}

	BUG_ON(p[0] != IOMMU_ARCH_VERSION);
}

static void omap2_iommu_restore_ctx(struct omap_iommu *obj)
{
	int i;
	u32 *p = obj->ctx;

	for (i = 0; i < (MMU_REG_SIZE / sizeof(u32)); i++) {
		iommu_write_reg(obj, p[i], i * sizeof(u32));
		dev_dbg(obj->dev, "%s\t[%02d] %08x\n", __func__, i, p[i]);
	}

	BUG_ON(p[0] != IOMMU_ARCH_VERSION);
}

static void omap2_cr_to_e(struct cr_regs *cr, struct iotlb_entry *e)
{
	e->da		= cr->cam & MMU_CAM_VATAG_MASK;
	e->pa		= cr->ram & MMU_RAM_PADDR_MASK;
	e->valid	= cr->cam & MMU_CAM_V;
	e->pgsz		= cr->cam & MMU_CAM_PGSZ_MASK;
	e->endian	= cr->ram & MMU_RAM_ENDIAN_MASK;
	e->elsz		= cr->ram & MMU_RAM_ELSZ_MASK;
	e->mixed	= cr->ram & MMU_RAM_MIXED;
}

static const struct iommu_functions omap2_iommu_ops = {
	.version	= IOMMU_ARCH_VERSION,

	.enable		= omap2_iommu_enable,
	.disable	= omap2_iommu_disable,
	.set_twl	= omap2_iommu_set_twl,
	.fault_isr	= omap2_iommu_fault_isr,

	.tlb_read_cr	= omap2_tlb_read_cr,
	.tlb_load_cr	= omap2_tlb_load_cr,

	.cr_to_e	= omap2_cr_to_e,
	.cr_to_virt	= omap2_cr_to_virt,
	.alloc_cr	= omap2_alloc_cr,
	.cr_valid	= omap2_cr_valid,
	.dump_cr	= omap2_dump_cr,

	.get_pte_attr	= omap2_get_pte_attr,

	.save_ctx	= omap2_iommu_save_ctx,
	.restore_ctx	= omap2_iommu_restore_ctx,
	.dump_ctx	= omap2_iommu_dump_ctx,
};

static int __init omap2_iommu_init(void)
{
	return omap_install_iommu_arch(&omap2_iommu_ops);
}
module_init(omap2_iommu_init);

static void __exit omap2_iommu_exit(void)
{
	omap_uninstall_iommu_arch(&omap2_iommu_ops);
}
module_exit(omap2_iommu_exit);

MODULE_AUTHOR("Hiroshi DOYU, Paul Mundt and Toshihiro Kobayashi");
MODULE_DESCRIPTION("omap iommu: omap2/3 architecture specific functions");
MODULE_LICENSE("GPL v2");
