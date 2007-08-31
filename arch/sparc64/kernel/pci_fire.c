/* pci_fire.c: Sun4u platform PCI-E controller support.
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/irq.h>

#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/irq.h>

#include "pci_impl.h"

#define fire_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define fire_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory")

static void pci_fire_scan_bus(struct pci_pbm_info *pbm)
{
	pbm->pci_bus = pci_scan_one_pbm(pbm);

	/* XXX register error interrupt handlers XXX */
}

#define FIRE_IOMMU_CONTROL	0x40000UL
#define FIRE_IOMMU_TSBBASE	0x40008UL
#define FIRE_IOMMU_FLUSH	0x40100UL
#define FIRE_IOMMU_FLUSHINV	0x40108UL

static int pci_fire_pbm_iommu_init(struct pci_pbm_info *pbm)
{
	struct iommu *iommu = pbm->iommu;
	u32 vdma[2], dma_mask;
	u64 control;
	int tsbsize, err;

	/* No virtual-dma property on these guys, use largest size.  */
	vdma[0] = 0xc0000000; /* base */
	vdma[1] = 0x40000000; /* size */
	dma_mask = 0xffffffff;
	tsbsize = 128;

	/* Register addresses. */
	iommu->iommu_control  = pbm->pbm_regs + FIRE_IOMMU_CONTROL;
	iommu->iommu_tsbbase  = pbm->pbm_regs + FIRE_IOMMU_TSBBASE;
	iommu->iommu_flush    = pbm->pbm_regs + FIRE_IOMMU_FLUSH;
	iommu->iommu_flushinv = pbm->pbm_regs + FIRE_IOMMU_FLUSHINV;

	/* We use the main control/status register of FIRE as the write
	 * completion register.
	 */
	iommu->write_complete_reg = pbm->controller_regs + 0x410000UL;

	/*
	 * Invalidate TLB Entries.
	 */
	fire_write(iommu->iommu_flushinv, ~(u64)0);

	err = iommu_table_init(iommu, tsbsize * 8 * 1024, vdma[0], dma_mask);
	if (err)
		return err;

	fire_write(iommu->iommu_tsbbase, __pa(iommu->page_table) | 0x7UL);

	control = fire_read(iommu->iommu_control);
	control |= (0x00000400 /* TSB cache snoop enable */	|
		    0x00000300 /* Cache mode */			|
		    0x00000002 /* Bypass enable */		|
		    0x00000001 /* Translation enable */);
	fire_write(iommu->iommu_control, control);

	return 0;
}

#ifdef CONFIG_PCI_MSI
struct pci_msiq_entry {
	u64		word0;
#define MSIQ_WORD0_RESV			0x8000000000000000UL
#define MSIQ_WORD0_FMT_TYPE		0x7f00000000000000UL
#define MSIQ_WORD0_FMT_TYPE_SHIFT	56
#define MSIQ_WORD0_LEN			0x00ffc00000000000UL
#define MSIQ_WORD0_LEN_SHIFT		46
#define MSIQ_WORD0_ADDR0		0x00003fff00000000UL
#define MSIQ_WORD0_ADDR0_SHIFT		32
#define MSIQ_WORD0_RID			0x00000000ffff0000UL
#define MSIQ_WORD0_RID_SHIFT		16
#define MSIQ_WORD0_DATA0		0x000000000000ffffUL
#define MSIQ_WORD0_DATA0_SHIFT		0

#define MSIQ_TYPE_MSG			0x6
#define MSIQ_TYPE_MSI32			0xb
#define MSIQ_TYPE_MSI64			0xf

	u64		word1;
#define MSIQ_WORD1_ADDR1		0xffffffffffff0000UL
#define MSIQ_WORD1_ADDR1_SHIFT		16
#define MSIQ_WORD1_DATA1		0x000000000000ffffUL
#define MSIQ_WORD1_DATA1_SHIFT		0

	u64		resv[6];
};

/* All MSI registers are offset from pbm->pbm_regs */
#define EVENT_QUEUE_BASE_ADDR_REG	0x010000UL
#define  EVENT_QUEUE_BASE_ADDR_ALL_ONES	0xfffc000000000000UL

#define EVENT_QUEUE_CONTROL_SET(EQ)	(0x011000UL + (EQ) * 0x8UL)
#define  EVENT_QUEUE_CONTROL_SET_OFLOW	0x0200000000000000UL
#define  EVENT_QUEUE_CONTROL_SET_EN	0x0000100000000000UL

#define EVENT_QUEUE_CONTROL_CLEAR(EQ)	(0x011200UL + (EQ) * 0x8UL)
#define  EVENT_QUEUE_CONTROL_CLEAR_OF	0x0200000000000000UL
#define  EVENT_QUEUE_CONTROL_CLEAR_E2I	0x0000800000000000UL
#define  EVENT_QUEUE_CONTROL_CLEAR_DIS	0x0000100000000000UL

#define EVENT_QUEUE_STATE(EQ)		(0x011400UL + (EQ) * 0x8UL)
#define  EVENT_QUEUE_STATE_MASK		0x0000000000000007UL
#define  EVENT_QUEUE_STATE_IDLE		0x0000000000000001UL
#define  EVENT_QUEUE_STATE_ACTIVE	0x0000000000000002UL
#define  EVENT_QUEUE_STATE_ERROR	0x0000000000000004UL

#define EVENT_QUEUE_TAIL(EQ)		(0x011600UL + (EQ) * 0x8UL)
#define  EVENT_QUEUE_TAIL_OFLOW		0x0200000000000000UL
#define  EVENT_QUEUE_TAIL_VAL		0x000000000000007fUL

#define EVENT_QUEUE_HEAD(EQ)		(0x011800UL + (EQ) * 0x8UL)
#define  EVENT_QUEUE_HEAD_VAL		0x000000000000007fUL

#define MSI_MAP(MSI)			(0x020000UL + (MSI) * 0x8UL)
#define  MSI_MAP_VALID			0x8000000000000000UL
#define  MSI_MAP_EQWR_N			0x4000000000000000UL
#define  MSI_MAP_EQNUM			0x000000000000003fUL

#define MSI_CLEAR(MSI)			(0x028000UL + (MSI) * 0x8UL)
#define  MSI_CLEAR_EQWR_N		0x4000000000000000UL

#define IMONDO_DATA0			0x02C000UL
#define  IMONDO_DATA0_DATA		0xffffffffffffffc0UL

#define IMONDO_DATA1			0x02C008UL
#define  IMONDO_DATA1_DATA		0xffffffffffffffffUL

#define MSI_32BIT_ADDR			0x034000UL
#define  MSI_32BIT_ADDR_VAL		0x00000000ffff0000UL

#define MSI_64BIT_ADDR			0x034008UL
#define  MSI_64BIT_ADDR_VAL		0xffffffffffff0000UL

/* For now this just runs as a pre-handler for the real interrupt handler.
 * So we just walk through the queue and ACK all the entries, update the
 * head pointer, and return.
 *
 * In the longer term it would be nice to do something more integrated
 * wherein we can pass in some of this MSI info to the drivers.  This
 * would be most useful for PCIe fabric error messages, although we could
 * invoke those directly from the loop here in order to pass the info around.
 */
static void pci_msi_prehandler(unsigned int ino, void *data1, void *data2)
{
	unsigned long msiqid, orig_head, head, type_fmt, type;
	struct pci_pbm_info *pbm = data1;
	struct pci_msiq_entry *base, *ep;

	msiqid = (unsigned long) data2;

	head = fire_read(pbm->pbm_regs + EVENT_QUEUE_HEAD(msiqid));

	orig_head = head;
	base = (pbm->msi_queues + ((msiqid - pbm->msiq_first) * 8192));
	ep = &base[head];
	while ((ep->word0 & MSIQ_WORD0_FMT_TYPE) != 0) {
		unsigned long msi_num;

		type_fmt = ((ep->word0 & MSIQ_WORD0_FMT_TYPE) >>
			    MSIQ_WORD0_FMT_TYPE_SHIFT);
		type = (type_fmt >>3);
		if (unlikely(type != MSIQ_TYPE_MSI32 &&
			     type != MSIQ_TYPE_MSI64))
			goto bad_type;

		msi_num = ((ep->word0 & MSIQ_WORD0_DATA0) >>
			   MSIQ_WORD0_DATA0_SHIFT);

		fire_write(pbm->pbm_regs + MSI_CLEAR(msi_num),
			   MSI_CLEAR_EQWR_N);

		/* Clear the entry.  */
		ep->word0 &= ~MSIQ_WORD0_FMT_TYPE;

		/* Go to next entry in ring.  */
		head++;
		if (head >= pbm->msiq_ent_count)
			head = 0;
		ep = &base[head];
	}

	if (likely(head != orig_head)) {
		/* ACK entries by updating head pointer.  */
		fire_write(pbm->pbm_regs +
			   EVENT_QUEUE_HEAD(msiqid),
			   head);
	}
	return;

bad_type:
	printk(KERN_EMERG "MSI: Entry has bad type %lx\n", type);
	return;
}

static int msi_bitmap_alloc(struct pci_pbm_info *pbm)
{
	unsigned long size, bits_per_ulong;

	bits_per_ulong = sizeof(unsigned long) * 8;
	size = (pbm->msi_num + (bits_per_ulong - 1)) & ~(bits_per_ulong - 1);
	size /= 8;
	BUG_ON(size % sizeof(unsigned long));

	pbm->msi_bitmap = kzalloc(size, GFP_KERNEL);
	if (!pbm->msi_bitmap)
		return -ENOMEM;

	return 0;
}

static void msi_bitmap_free(struct pci_pbm_info *pbm)
{
	kfree(pbm->msi_bitmap);
	pbm->msi_bitmap = NULL;
}

static int msi_queue_alloc(struct pci_pbm_info *pbm)
{
	unsigned long pages, order, i;

	order = get_order(512 * 1024);
	pages = __get_free_pages(GFP_KERNEL | __GFP_COMP, order);
	if (pages == 0UL) {
		printk(KERN_ERR "MSI: Cannot allocate MSI queues (o=%lu).\n",
		       order);
		return -ENOMEM;
	}
	memset((char *)pages, 0, PAGE_SIZE << order);
	pbm->msi_queues = (void *) pages;

	fire_write(pbm->pbm_regs + EVENT_QUEUE_BASE_ADDR_REG,
		   (EVENT_QUEUE_BASE_ADDR_ALL_ONES |
		    __pa(pbm->msi_queues)));

	fire_write(pbm->pbm_regs + IMONDO_DATA0,
		   pbm->portid << 6);
	fire_write(pbm->pbm_regs + IMONDO_DATA1, 0);

	fire_write(pbm->pbm_regs + MSI_32BIT_ADDR,
		   pbm->msi32_start);
	fire_write(pbm->pbm_regs + MSI_64BIT_ADDR,
		   pbm->msi64_start);

	for (i = 0; i < pbm->msiq_num; i++) {
		fire_write(pbm->pbm_regs + EVENT_QUEUE_HEAD(i), 0);
		fire_write(pbm->pbm_regs + EVENT_QUEUE_TAIL(i), 0);
	}

	return 0;
}

static int alloc_msi(struct pci_pbm_info *pbm)
{
	int i;

	for (i = 0; i < pbm->msi_num; i++) {
		if (!test_and_set_bit(i, pbm->msi_bitmap))
			return i + pbm->msi_first;
	}

	return -ENOENT;
}

static void free_msi(struct pci_pbm_info *pbm, int msi_num)
{
	msi_num -= pbm->msi_first;
	clear_bit(msi_num, pbm->msi_bitmap);
}

static int pci_setup_msi_irq(unsigned int *virt_irq_p,
			     struct pci_dev *pdev,
			     struct msi_desc *entry)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	unsigned long devino, msiqid, cregs, imap_off;
	struct msi_msg msg;
	int msi_num, err;
	u64 val;

	*virt_irq_p = 0;

	msi_num = alloc_msi(pbm);
	if (msi_num < 0)
		return msi_num;

	cregs = (unsigned long) pbm->pbm_regs;

	err = sun4u_build_msi(pbm->portid, virt_irq_p,
			      pbm->msiq_first_devino,
			      (pbm->msiq_first_devino +
			       pbm->msiq_num),
			      cregs + 0x001000UL,
			      cregs + 0x001400UL);
	if (err < 0)
		goto out_err;
	devino = err;

	imap_off = 0x001000UL + (devino * 0x8UL);

	val = fire_read(pbm->pbm_regs + imap_off);
	val |= (1UL << 63) | (1UL << 6);
	fire_write(pbm->pbm_regs + imap_off, val);

	msiqid = ((devino - pbm->msiq_first_devino) +
		  pbm->msiq_first);

	fire_write(pbm->pbm_regs +
		   EVENT_QUEUE_CONTROL_SET(msiqid),
		   EVENT_QUEUE_CONTROL_SET_EN);

	val = fire_read(pbm->pbm_regs + MSI_MAP(msi_num));
	val &= ~(MSI_MAP_EQNUM);
	val |= msiqid;
	fire_write(pbm->pbm_regs + MSI_MAP(msi_num), val);

	fire_write(pbm->pbm_regs + MSI_CLEAR(msi_num),
		   MSI_CLEAR_EQWR_N);

	val = fire_read(pbm->pbm_regs + MSI_MAP(msi_num));
	val |= MSI_MAP_VALID;
	fire_write(pbm->pbm_regs + MSI_MAP(msi_num), val);

	sparc64_set_msi(*virt_irq_p, msi_num);

	if (entry->msi_attrib.is_64) {
		msg.address_hi = pbm->msi64_start >> 32;
		msg.address_lo = pbm->msi64_start & 0xffffffff;
	} else {
		msg.address_hi = 0;
		msg.address_lo = pbm->msi32_start;
	}
	msg.data = msi_num;

	set_irq_msi(*virt_irq_p, entry);
	write_msi_msg(*virt_irq_p, &msg);

	irq_install_pre_handler(*virt_irq_p,
				pci_msi_prehandler,
				pbm, (void *) msiqid);

	return 0;

out_err:
	free_msi(pbm, msi_num);
	return err;
}

static void pci_teardown_msi_irq(unsigned int virt_irq,
				 struct pci_dev *pdev)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	unsigned long msiqid, msi_num;
	u64 val;

	msi_num = sparc64_get_msi(virt_irq);

	val = fire_read(pbm->pbm_regs + MSI_MAP(msi_num));

	msiqid = (val & MSI_MAP_EQNUM);

	val &= ~MSI_MAP_VALID;
	fire_write(pbm->pbm_regs + MSI_MAP(msi_num), val);

	fire_write(pbm->pbm_regs + EVENT_QUEUE_CONTROL_CLEAR(msiqid),
		   EVENT_QUEUE_CONTROL_CLEAR_DIS);

	free_msi(pbm, msi_num);

	/* The sun4u_destroy_msi() will liberate the devino and thus the MSIQ
	 * allocation.
	 */
	sun4u_destroy_msi(virt_irq);
}

static void pci_fire_msi_init(struct pci_pbm_info *pbm)
{
	const u32 *val;
	int len;

	val = of_get_property(pbm->prom_node, "#msi-eqs", &len);
	if (!val || len != 4)
		goto no_msi;
	pbm->msiq_num = *val;
	if (pbm->msiq_num) {
		const struct msiq_prop {
			u32 first_msiq;
			u32 num_msiq;
			u32 first_devino;
		} *mqp;
		const struct msi_range_prop {
			u32 first_msi;
			u32 num_msi;
		} *mrng;
		const struct addr_range_prop {
			u32 msi32_high;
			u32 msi32_low;
			u32 msi32_len;
			u32 msi64_high;
			u32 msi64_low;
			u32 msi64_len;
		} *arng;

		val = of_get_property(pbm->prom_node, "msi-eq-size", &len);
		if (!val || len != 4)
			goto no_msi;

		pbm->msiq_ent_count = *val;

		mqp = of_get_property(pbm->prom_node,
				      "msi-eq-to-devino", &len);
		if (!mqp)
			mqp = of_get_property(pbm->prom_node,
					      "msi-eq-devino", &len);
		if (!mqp || len != sizeof(struct msiq_prop))
			goto no_msi;

		pbm->msiq_first = mqp->first_msiq;
		pbm->msiq_first_devino = mqp->first_devino;

		val = of_get_property(pbm->prom_node, "#msi", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msi_num = *val;

		mrng = of_get_property(pbm->prom_node, "msi-ranges", &len);
		if (!mrng || len != sizeof(struct msi_range_prop))
			goto no_msi;
		pbm->msi_first = mrng->first_msi;

		val = of_get_property(pbm->prom_node, "msi-data-mask", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msi_data_mask = *val;

		val = of_get_property(pbm->prom_node, "msix-data-width", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msix_data_width = *val;

		arng = of_get_property(pbm->prom_node, "msi-address-ranges",
				       &len);
		if (!arng || len != sizeof(struct addr_range_prop))
			goto no_msi;
		pbm->msi32_start = ((u64)arng->msi32_high << 32) |
			(u64) arng->msi32_low;
		pbm->msi64_start = ((u64)arng->msi64_high << 32) |
			(u64) arng->msi64_low;
		pbm->msi32_len = arng->msi32_len;
		pbm->msi64_len = arng->msi64_len;

		if (msi_bitmap_alloc(pbm))
			goto no_msi;

		if (msi_queue_alloc(pbm)) {
			msi_bitmap_free(pbm);
			goto no_msi;
		}

		printk(KERN_INFO "%s: MSI Queue first[%u] num[%u] count[%u] "
		       "devino[0x%x]\n",
		       pbm->name,
		       pbm->msiq_first, pbm->msiq_num,
		       pbm->msiq_ent_count,
		       pbm->msiq_first_devino);
		printk(KERN_INFO "%s: MSI first[%u] num[%u] mask[0x%x] "
		       "width[%u]\n",
		       pbm->name,
		       pbm->msi_first, pbm->msi_num, pbm->msi_data_mask,
		       pbm->msix_data_width);
		printk(KERN_INFO "%s: MSI addr32[0x%lx:0x%x] "
		       "addr64[0x%lx:0x%x]\n",
		       pbm->name,
		       pbm->msi32_start, pbm->msi32_len,
		       pbm->msi64_start, pbm->msi64_len);
		printk(KERN_INFO "%s: MSI queues at RA [%016lx]\n",
		       pbm->name,
		       __pa(pbm->msi_queues));
	}
	pbm->setup_msi_irq = pci_setup_msi_irq;
	pbm->teardown_msi_irq = pci_teardown_msi_irq;

	return;

no_msi:
	pbm->msiq_num = 0;
	printk(KERN_INFO "%s: No MSI support.\n", pbm->name);
}
#else /* CONFIG_PCI_MSI */
static void pci_fire_msi_init(struct pci_pbm_info *pbm)
{
}
#endif /* !(CONFIG_PCI_MSI) */

/* Based at pbm->controller_regs */
#define FIRE_PARITY_CONTROL	0x470010UL
#define  FIRE_PARITY_ENAB	0x8000000000000000UL
#define FIRE_FATAL_RESET_CTL	0x471028UL
#define  FIRE_FATAL_RESET_SPARE	0x0000000004000000UL
#define  FIRE_FATAL_RESET_MB	0x0000000002000000UL
#define  FIRE_FATAL_RESET_CPE	0x0000000000008000UL
#define  FIRE_FATAL_RESET_APE	0x0000000000004000UL
#define  FIRE_FATAL_RESET_PIO	0x0000000000000040UL
#define  FIRE_FATAL_RESET_JW	0x0000000000000004UL
#define  FIRE_FATAL_RESET_JI	0x0000000000000002UL
#define  FIRE_FATAL_RESET_JR	0x0000000000000001UL
#define FIRE_CORE_INTR_ENABLE	0x471800UL

/* Based at pbm->pbm_regs */
#define FIRE_TLU_CTRL		0x80000UL
#define  FIRE_TLU_CTRL_TIM	0x00000000da000000UL
#define  FIRE_TLU_CTRL_QDET	0x0000000000000100UL
#define  FIRE_TLU_CTRL_CFG	0x0000000000000001UL
#define FIRE_TLU_DEV_CTRL	0x90008UL
#define FIRE_TLU_LINK_CTRL	0x90020UL
#define FIRE_TLU_LINK_CTRL_CLK	0x0000000000000040UL
#define FIRE_LPU_RESET		0xe2008UL
#define FIRE_LPU_LLCFG		0xe2200UL
#define  FIRE_LPU_LLCFG_VC0	0x0000000000000100UL
#define FIRE_LPU_FCTRL_UCTRL	0xe2240UL
#define  FIRE_LPU_FCTRL_UCTRL_N	0x0000000000000002UL
#define  FIRE_LPU_FCTRL_UCTRL_P	0x0000000000000001UL
#define FIRE_LPU_TXL_FIFOP	0xe2430UL
#define FIRE_LPU_LTSSM_CFG2	0xe2788UL
#define FIRE_LPU_LTSSM_CFG3	0xe2790UL
#define FIRE_LPU_LTSSM_CFG4	0xe2798UL
#define FIRE_LPU_LTSSM_CFG5	0xe27a0UL
#define FIRE_DMC_IENAB		0x31800UL
#define FIRE_DMC_DBG_SEL_A	0x53000UL
#define FIRE_DMC_DBG_SEL_B	0x53008UL
#define FIRE_PEC_IENAB		0x51800UL

static void pci_fire_hw_init(struct pci_pbm_info *pbm)
{
	u64 val;

	fire_write(pbm->controller_regs + FIRE_PARITY_CONTROL,
		   FIRE_PARITY_ENAB);

	fire_write(pbm->controller_regs + FIRE_FATAL_RESET_CTL,
		   (FIRE_FATAL_RESET_SPARE |
		    FIRE_FATAL_RESET_MB |
		    FIRE_FATAL_RESET_CPE |
		    FIRE_FATAL_RESET_APE |
		    FIRE_FATAL_RESET_PIO |
		    FIRE_FATAL_RESET_JW |
		    FIRE_FATAL_RESET_JI |
		    FIRE_FATAL_RESET_JR));

	fire_write(pbm->controller_regs + FIRE_CORE_INTR_ENABLE, ~(u64)0);

	val = fire_read(pbm->pbm_regs + FIRE_TLU_CTRL);
	val |= (FIRE_TLU_CTRL_TIM |
		FIRE_TLU_CTRL_QDET |
		FIRE_TLU_CTRL_CFG);
	fire_write(pbm->pbm_regs + FIRE_TLU_CTRL, val);
	fire_write(pbm->pbm_regs + FIRE_TLU_DEV_CTRL, 0);
	fire_write(pbm->pbm_regs + FIRE_TLU_LINK_CTRL,
		   FIRE_TLU_LINK_CTRL_CLK);

	fire_write(pbm->pbm_regs + FIRE_LPU_RESET, 0);
	fire_write(pbm->pbm_regs + FIRE_LPU_LLCFG,
		   FIRE_LPU_LLCFG_VC0);
	fire_write(pbm->pbm_regs + FIRE_LPU_FCTRL_UCTRL,
		   (FIRE_LPU_FCTRL_UCTRL_N |
		    FIRE_LPU_FCTRL_UCTRL_P));
	fire_write(pbm->pbm_regs + FIRE_LPU_TXL_FIFOP,
		   ((0xffff << 16) | (0x0000 << 0)));
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG2, 3000000);
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG3, 500000);
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG4,
		   (2 << 16) | (140 << 8));
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG5, 0);

	fire_write(pbm->pbm_regs + FIRE_DMC_IENAB, ~(u64)0);
	fire_write(pbm->pbm_regs + FIRE_DMC_DBG_SEL_A, 0);
	fire_write(pbm->pbm_regs + FIRE_DMC_DBG_SEL_B, 0);

	fire_write(pbm->pbm_regs + FIRE_PEC_IENAB, ~(u64)0);
}

static int pci_fire_pbm_init(struct pci_controller_info *p,
			     struct device_node *dp, u32 portid)
{
	const struct linux_prom64_registers *regs;
	struct pci_pbm_info *pbm;
	int err;

	if ((portid & 1) == 0)
		pbm = &p->pbm_A;
	else
		pbm = &p->pbm_B;

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	pbm->scan_bus = pci_fire_scan_bus;
	pbm->pci_ops = &sun4u_pci_ops;
	pbm->config_space_reg_bits = 12;

	pbm->index = pci_num_pbms++;

	pbm->portid = portid;
	pbm->parent = p;
	pbm->prom_node = dp;
	pbm->name = dp->full_name;

	regs = of_get_property(dp, "reg", NULL);
	pbm->pbm_regs = regs[0].phys_addr;
	pbm->controller_regs = regs[1].phys_addr - 0x410000UL;

	printk("%s: SUN4U PCIE Bus Module\n", pbm->name);

	pci_determine_mem_io_space(pbm);

	pci_get_pbm_props(pbm);

	pci_fire_hw_init(pbm);

	err = pci_fire_pbm_iommu_init(pbm);
	if (err)
		return err;

	pci_fire_msi_init(pbm);

	return 0;
}

static inline int portid_compare(u32 x, u32 y)
{
	if (x == (y ^ 1))
		return 1;
	return 0;
}

void fire_pci_init(struct device_node *dp, const char *model_name)
{
	struct pci_controller_info *p;
	u32 portid = of_getintprop_default(dp, "portid", 0xff);
	struct iommu *iommu;
	struct pci_pbm_info *pbm;

	for (pbm = pci_pbm_root; pbm; pbm = pbm->next) {
		if (portid_compare(pbm->portid, portid)) {
			if (pci_fire_pbm_init(pbm->parent, dp, portid))
				goto fatal_memory_error;
			return;
		}
	}

	p = kzalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p)
		goto fatal_memory_error;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	p->pbm_A.iommu = iommu;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	p->pbm_B.iommu = iommu;

	/* XXX MSI support XXX */

	/* Like PSYCHO and SCHIZO we have a 2GB aligned area
	 * for memory space.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	if (pci_fire_pbm_init(p, dp, portid))
		goto fatal_memory_error;

	return;

fatal_memory_error:
	prom_printf("PCI_FIRE: Fatal memory allocation error.\n");
	prom_halt();
}
