/* $Id: pci_schizo.c,v 1.24 2002/01/23 11:27:32 davem Exp $
 * pci_schizo.c: SCHIZO/TOMATILLO specific PCI controller support.
 *
 * Copyright (C) 2001, 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/upa.h>
#include <asm/pstate.h>

#include "pci_impl.h"
#include "iommu_common.h"

/* All SCHIZO registers are 64-bits.  The following accessor
 * routines are how they are accessed.  The REG parameter
 * is a physical address.
 */
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

/* This is a convention that at least Excalibur and Merlin
 * follow.  I suppose the SCHIZO used in Starcat and friends
 * will do similar.
 *
 * The only way I could see this changing is if the newlink
 * block requires more space in Schizo's address space than
 * they predicted, thus requiring an address space reorg when
 * the newer Schizo is taped out.
 */

/* Streaming buffer control register. */
#define SCHIZO_STRBUF_CTRL_LPTR    0x00000000000000f0UL /* LRU Lock Pointer */
#define SCHIZO_STRBUF_CTRL_LENAB   0x0000000000000008UL /* LRU Lock Enable */
#define SCHIZO_STRBUF_CTRL_RRDIS   0x0000000000000004UL /* Rerun Disable */
#define SCHIZO_STRBUF_CTRL_DENAB   0x0000000000000002UL /* Diagnostic Mode Enable */
#define SCHIZO_STRBUF_CTRL_ENAB    0x0000000000000001UL /* Streaming Buffer Enable */

/* IOMMU control register. */
#define SCHIZO_IOMMU_CTRL_RESV     0xfffffffff9000000UL /* Reserved                      */
#define SCHIZO_IOMMU_CTRL_XLTESTAT 0x0000000006000000UL /* Translation Error Status      */
#define SCHIZO_IOMMU_CTRL_XLTEERR  0x0000000001000000UL /* Translation Error encountered */
#define SCHIZO_IOMMU_CTRL_LCKEN    0x0000000000800000UL /* Enable translation locking    */
#define SCHIZO_IOMMU_CTRL_LCKPTR   0x0000000000780000UL /* Translation lock pointer      */
#define SCHIZO_IOMMU_CTRL_TSBSZ    0x0000000000070000UL /* TSB Size                      */
#define SCHIZO_IOMMU_TSBSZ_1K      0x0000000000000000UL /* TSB Table 1024 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_2K      0x0000000000010000UL /* TSB Table 2048 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_4K      0x0000000000020000UL /* TSB Table 4096 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_8K      0x0000000000030000UL /* TSB Table 8192 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_16K     0x0000000000040000UL /* TSB Table 16k 8-byte entries  */
#define SCHIZO_IOMMU_TSBSZ_32K     0x0000000000050000UL /* TSB Table 32k 8-byte entries  */
#define SCHIZO_IOMMU_TSBSZ_64K     0x0000000000060000UL /* TSB Table 64k 8-byte entries  */
#define SCHIZO_IOMMU_TSBSZ_128K    0x0000000000070000UL /* TSB Table 128k 8-byte entries */
#define SCHIZO_IOMMU_CTRL_RESV2    0x000000000000fff8UL /* Reserved                      */
#define SCHIZO_IOMMU_CTRL_TBWSZ    0x0000000000000004UL /* Assumed page size, 0=8k 1=64k */
#define SCHIZO_IOMMU_CTRL_DENAB    0x0000000000000002UL /* Diagnostic mode enable        */
#define SCHIZO_IOMMU_CTRL_ENAB     0x0000000000000001UL /* IOMMU Enable                  */

/* Schizo config space address format is nearly identical to
 * that of PSYCHO:
 *
 *  32             24 23 16 15    11 10       8 7   2  1 0
 * ---------------------------------------------------------
 * |0 0 0 0 0 0 0 0 0| bus | device | function | reg | 0 0 |
 * ---------------------------------------------------------
 */
#define SCHIZO_CONFIG_BASE(PBM)	((PBM)->config_space)
#define SCHIZO_CONFIG_ENCODE(BUS, DEVFN, REG)	\
	(((unsigned long)(BUS)   << 16) |	\
	 ((unsigned long)(DEVFN) << 8)  |	\
	 ((unsigned long)(REG)))

static void *schizo_pci_config_mkaddr(struct pci_pbm_info *pbm,
				      unsigned char bus,
				      unsigned int devfn,
				      int where)
{
	if (!pbm)
		return NULL;
	bus -= pbm->pci_first_busno;
	return (void *)
		(SCHIZO_CONFIG_BASE(pbm) |
		 SCHIZO_CONFIG_ENCODE(bus, devfn, where));
}

/* Just make sure the bus number is in range.  */
static int schizo_out_of_range(struct pci_pbm_info *pbm,
			       unsigned char bus,
			       unsigned char devfn)
{
	if (bus < pbm->pci_first_busno ||
	    bus > pbm->pci_last_busno)
		return 1;
	return 0;
}

/* SCHIZO PCI configuration space accessors. */

static int schizo_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			       int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;
	u16 tmp16;
	u8 tmp8;

	switch (size) {
	case 1:
		*value = 0xff;
		break;
	case 2:
		*value = 0xffff;
		break;
	case 4:
		*value = 0xffffffff;
		break;
	}

	addr = schizo_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (schizo_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;
	switch (size) {
	case 1:
		pci_config_read8((u8 *)addr, &tmp8);
		*value = tmp8;
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_read_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read16((u16 *)addr, &tmp16);
		*value = tmp16;
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_read_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read32(addr, value);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int schizo_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;

	addr = schizo_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (schizo_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		pci_config_write8((u8 *)addr, value);
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_write_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_write16((u16 *)addr, value);
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_write_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}

		pci_config_write32(addr, value);
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops schizo_ops = {
	.read =		schizo_read_pci_cfg,
	.write =	schizo_write_pci_cfg,
};

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

static void tomatillo_wsync_handler(struct ino_bucket *bucket, void *_arg1, void *_arg2)
{
	unsigned long sync_reg = (unsigned long) _arg2;
	u64 mask = 1UL << (__irq_ino(__irq(bucket)) & IMAP_INO);
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

static unsigned long schizo_ino_to_iclr(struct pci_pbm_info *pbm,
					unsigned int ino)
{
	ino &= PCI_IRQ_INO;
	return pbm->pbm_regs + schizo_iclr_offset(ino) + 4;
}

static unsigned long schizo_ino_to_imap(struct pci_pbm_info *pbm,
					unsigned int ino)
{
	ino &= PCI_IRQ_INO;
	return pbm->pbm_regs + schizo_imap_offset(ino) + 4;
}

static unsigned int schizo_irq_build(struct pci_pbm_info *pbm,
				     struct pci_dev *pdev,
				     unsigned int ino)
{
	unsigned long imap, iclr;
	int ign_fixup;
	int virt_irq;

	ino &= PCI_IRQ_INO;

	/* Now build the IRQ bucket. */
	imap = schizo_ino_to_imap(pbm, ino);
	iclr = schizo_ino_to_iclr(pbm, ino);

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
	if (pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO) {
		if (pbm->portid & 1)
			ign_fixup = (1 << 6);
	}

	virt_irq = build_irq(ign_fixup, iclr, imap, IBF_PCI);

	if (pdev && pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO) {
		irq_install_pre_handler(virt_irq,
					tomatillo_wsync_handler,
					((pbm->chip_version <= 4) ?
					 (void *) 1 : (void *) 0),
					(void *) pbm->sync_reg);
	}

	return virt_irq;
}

/* SCHIZO error handling support. */
enum schizo_error_type {
	UE_ERR, CE_ERR, PCI_ERR, SAFARI_ERR
};

static DEFINE_SPINLOCK(stc_buf_lock);
static unsigned long stc_error_buf[128];
static unsigned long stc_tag_buf[16];
static unsigned long stc_line_buf[16];

#define SCHIZO_UE_INO		0x30 /* Uncorrectable ECC error */
#define SCHIZO_CE_INO		0x31 /* Correctable ECC error */
#define SCHIZO_PCIERR_A_INO	0x32 /* PBM A PCI bus error */
#define SCHIZO_PCIERR_B_INO	0x33 /* PBM B PCI bus error */
#define SCHIZO_SERR_INO		0x34 /* Safari interface error */

struct pci_pbm_info *pbm_for_ino(struct pci_controller_info *p, u32 ino)
{
	ino &= IMAP_INO;
	if (p->pbm_A.ino_bitmap & (1UL << ino))
		return &p->pbm_A;
	if (p->pbm_B.ino_bitmap & (1UL << ino))
		return &p->pbm_B;

	printk("PCI%d: No ino_bitmap entry for ino[%x], bitmaps "
	       "PBM_A[%016lx] PBM_B[%016lx]",
	       p->index, ino,
	       p->pbm_A.ino_bitmap,
	       p->pbm_B.ino_bitmap);
	printk("PCI%d: Using PBM_A, report this problem immediately.\n",
	       p->index);

	return &p->pbm_A;
}

static void schizo_clear_other_err_intr(struct pci_controller_info *p, int irq)
{
	struct pci_pbm_info *pbm;
	unsigned long iclr;

	/* Do not clear the interrupt for the other PCI bus.
	 *
	 * This "ACK both PBM IRQs" only needs to be performed
	 * for chip-wide error interrupts.
	 */
	if ((irq & IMAP_INO) == SCHIZO_PCIERR_A_INO ||
	    (irq & IMAP_INO) == SCHIZO_PCIERR_B_INO)
		return;

	pbm = pbm_for_ino(p, irq);
	if (pbm == &p->pbm_A)
		pbm = &p->pbm_B;
	else
		pbm = &p->pbm_A;

	schizo_irq_build(pbm, NULL,
			 (pbm->portid << 6) | (irq & IMAP_INO));

	iclr = schizo_ino_to_iclr(pbm,
				  (pbm->portid << 6) | (irq & IMAP_INO));
	upa_writel(ICLR_IDLE, iclr);
}

#define SCHIZO_STC_ERR	0xb800UL /* --> 0xba00 */
#define SCHIZO_STC_TAG	0xba00UL /* --> 0xba80 */
#define SCHIZO_STC_LINE	0xbb00UL /* --> 0xbb80 */

#define SCHIZO_STCERR_WRITE	0x2UL
#define SCHIZO_STCERR_READ	0x1UL

#define SCHIZO_STCTAG_PPN	0x3fffffff00000000UL
#define SCHIZO_STCTAG_VPN	0x00000000ffffe000UL
#define SCHIZO_STCTAG_VALID	0x8000000000000000UL
#define SCHIZO_STCTAG_READ	0x4000000000000000UL

#define SCHIZO_STCLINE_LINDX	0x0000000007800000UL
#define SCHIZO_STCLINE_SPTR	0x000000000007e000UL
#define SCHIZO_STCLINE_LADDR	0x0000000000001fc0UL
#define SCHIZO_STCLINE_EPTR	0x000000000000003fUL
#define SCHIZO_STCLINE_VALID	0x0000000000600000UL
#define SCHIZO_STCLINE_FOFN	0x0000000000180000UL

static void __schizo_check_stc_error_pbm(struct pci_pbm_info *pbm,
					 enum schizo_error_type type)
{
	struct pci_strbuf *strbuf = &pbm->stc;
	unsigned long regbase = pbm->pbm_regs;
	unsigned long err_base, tag_base, line_base;
	u64 control;
	int i;

	err_base = regbase + SCHIZO_STC_ERR;
	tag_base = regbase + SCHIZO_STC_TAG;
	line_base = regbase + SCHIZO_STC_LINE;

	spin_lock(&stc_buf_lock);

	/* This is __REALLY__ dangerous.  When we put the
	 * streaming buffer into diagnostic mode to probe
	 * it's tags and error status, we _must_ clear all
	 * of the line tag valid bits before re-enabling
	 * the streaming buffer.  If any dirty data lives
	 * in the STC when we do this, we will end up
	 * invalidating it before it has a chance to reach
	 * main memory.
	 */
	control = schizo_read(strbuf->strbuf_control);
	schizo_write(strbuf->strbuf_control,
		     (control | SCHIZO_STRBUF_CTRL_DENAB));
	for (i = 0; i < 128; i++) {
		unsigned long val;

		val = schizo_read(err_base + (i * 8UL));
		schizo_write(err_base + (i * 8UL), 0UL);
		stc_error_buf[i] = val;
	}
	for (i = 0; i < 16; i++) {
		stc_tag_buf[i] = schizo_read(tag_base + (i * 8UL));
		stc_line_buf[i] = schizo_read(line_base + (i * 8UL));
		schizo_write(tag_base + (i * 8UL), 0UL);
		schizo_write(line_base + (i * 8UL), 0UL);
	}

	/* OK, state is logged, exit diagnostic mode. */
	schizo_write(strbuf->strbuf_control, control);

	for (i = 0; i < 16; i++) {
		int j, saw_error, first, last;

		saw_error = 0;
		first = i * 8;
		last = first + 8;
		for (j = first; j < last; j++) {
			unsigned long errval = stc_error_buf[j];
			if (errval != 0) {
				saw_error++;
				printk("%s: STC_ERR(%d)[wr(%d)rd(%d)]\n",
				       pbm->name,
				       j,
				       (errval & SCHIZO_STCERR_WRITE) ? 1 : 0,
				       (errval & SCHIZO_STCERR_READ) ? 1 : 0);
			}
		}
		if (saw_error != 0) {
			unsigned long tagval = stc_tag_buf[i];
			unsigned long lineval = stc_line_buf[i];
			printk("%s: STC_TAG(%d)[PA(%016lx)VA(%08lx)V(%d)R(%d)]\n",
			       pbm->name,
			       i,
			       ((tagval & SCHIZO_STCTAG_PPN) >> 19UL),
			       (tagval & SCHIZO_STCTAG_VPN),
			       ((tagval & SCHIZO_STCTAG_VALID) ? 1 : 0),
			       ((tagval & SCHIZO_STCTAG_READ) ? 1 : 0));

			/* XXX Should spit out per-bank error information... -DaveM */
			printk("%s: STC_LINE(%d)[LIDX(%lx)SP(%lx)LADDR(%lx)EP(%lx)"
			       "V(%d)FOFN(%d)]\n",
			       pbm->name,
			       i,
			       ((lineval & SCHIZO_STCLINE_LINDX) >> 23UL),
			       ((lineval & SCHIZO_STCLINE_SPTR) >> 13UL),
			       ((lineval & SCHIZO_STCLINE_LADDR) >> 6UL),
			       ((lineval & SCHIZO_STCLINE_EPTR) >> 0UL),
			       ((lineval & SCHIZO_STCLINE_VALID) ? 1 : 0),
			       ((lineval & SCHIZO_STCLINE_FOFN) ? 1 : 0));
		}
	}

	spin_unlock(&stc_buf_lock);
}

/* IOMMU is per-PBM in Schizo, so interrogate both for anonymous
 * controller level errors.
 */

#define SCHIZO_IOMMU_TAG	0xa580UL
#define SCHIZO_IOMMU_DATA	0xa600UL

#define SCHIZO_IOMMU_TAG_CTXT	0x0000001ffe000000UL
#define SCHIZO_IOMMU_TAG_ERRSTS	0x0000000001800000UL
#define SCHIZO_IOMMU_TAG_ERR	0x0000000000400000UL
#define SCHIZO_IOMMU_TAG_WRITE	0x0000000000200000UL
#define SCHIZO_IOMMU_TAG_STREAM	0x0000000000100000UL
#define SCHIZO_IOMMU_TAG_SIZE	0x0000000000080000UL
#define SCHIZO_IOMMU_TAG_VPAGE	0x000000000007ffffUL

#define SCHIZO_IOMMU_DATA_VALID	0x0000000100000000UL
#define SCHIZO_IOMMU_DATA_CACHE	0x0000000040000000UL
#define SCHIZO_IOMMU_DATA_PPAGE	0x000000003fffffffUL

static void schizo_check_iommu_error_pbm(struct pci_pbm_info *pbm,
					 enum schizo_error_type type)
{
	struct pci_iommu *iommu = pbm->iommu;
	unsigned long iommu_tag[16];
	unsigned long iommu_data[16];
	unsigned long flags;
	u64 control;
	int i;

	spin_lock_irqsave(&iommu->lock, flags);
	control = schizo_read(iommu->iommu_control);
	if (control & SCHIZO_IOMMU_CTRL_XLTEERR) {
		unsigned long base;
		char *type_string;

		/* Clear the error encountered bit. */
		control &= ~SCHIZO_IOMMU_CTRL_XLTEERR;
		schizo_write(iommu->iommu_control, control);

		switch((control & SCHIZO_IOMMU_CTRL_XLTESTAT) >> 25UL) {
		case 0:
			type_string = "Protection Error";
			break;
		case 1:
			type_string = "Invalid Error";
			break;
		case 2:
			type_string = "TimeOut Error";
			break;
		case 3:
		default:
			type_string = "ECC Error";
			break;
		};
		printk("%s: IOMMU Error, type[%s]\n",
		       pbm->name, type_string);

		/* Put the IOMMU into diagnostic mode and probe
		 * it's TLB for entries with error status.
		 *
		 * It is very possible for another DVMA to occur
		 * while we do this probe, and corrupt the system
		 * further.  But we are so screwed at this point
		 * that we are likely to crash hard anyways, so
		 * get as much diagnostic information to the
		 * console as we can.
		 */
		schizo_write(iommu->iommu_control,
			     control | SCHIZO_IOMMU_CTRL_DENAB);

		base = pbm->pbm_regs;

		for (i = 0; i < 16; i++) {
			iommu_tag[i] =
				schizo_read(base + SCHIZO_IOMMU_TAG + (i * 8UL));
			iommu_data[i] =
				schizo_read(base + SCHIZO_IOMMU_DATA + (i * 8UL));

			/* Now clear out the entry. */
			schizo_write(base + SCHIZO_IOMMU_TAG + (i * 8UL), 0);
			schizo_write(base + SCHIZO_IOMMU_DATA + (i * 8UL), 0);
		}

		/* Leave diagnostic mode. */
		schizo_write(iommu->iommu_control, control);

		for (i = 0; i < 16; i++) {
			unsigned long tag, data;

			tag = iommu_tag[i];
			if (!(tag & SCHIZO_IOMMU_TAG_ERR))
				continue;

			data = iommu_data[i];
			switch((tag & SCHIZO_IOMMU_TAG_ERRSTS) >> 23UL) {
			case 0:
				type_string = "Protection Error";
				break;
			case 1:
				type_string = "Invalid Error";
				break;
			case 2:
				type_string = "TimeOut Error";
				break;
			case 3:
			default:
				type_string = "ECC Error";
				break;
			};
			printk("%s: IOMMU TAG(%d)[error(%s) ctx(%x) wr(%d) str(%d) "
			       "sz(%dK) vpg(%08lx)]\n",
			       pbm->name, i, type_string,
			       (int)((tag & SCHIZO_IOMMU_TAG_CTXT) >> 25UL),
			       ((tag & SCHIZO_IOMMU_TAG_WRITE) ? 1 : 0),
			       ((tag & SCHIZO_IOMMU_TAG_STREAM) ? 1 : 0),
			       ((tag & SCHIZO_IOMMU_TAG_SIZE) ? 64 : 8),
			       (tag & SCHIZO_IOMMU_TAG_VPAGE) << IOMMU_PAGE_SHIFT);
			printk("%s: IOMMU DATA(%d)[valid(%d) cache(%d) ppg(%016lx)]\n",
			       pbm->name, i,
			       ((data & SCHIZO_IOMMU_DATA_VALID) ? 1 : 0),
			       ((data & SCHIZO_IOMMU_DATA_CACHE) ? 1 : 0),
			       (data & SCHIZO_IOMMU_DATA_PPAGE) << IOMMU_PAGE_SHIFT);
		}
	}
	if (pbm->stc.strbuf_enabled)
		__schizo_check_stc_error_pbm(pbm, type);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void schizo_check_iommu_error(struct pci_controller_info *p,
				     enum schizo_error_type type)
{
	schizo_check_iommu_error_pbm(&p->pbm_A, type);
	schizo_check_iommu_error_pbm(&p->pbm_B, type);
}

/* Uncorrectable ECC error status gathering. */
#define SCHIZO_UE_AFSR	0x10030UL
#define SCHIZO_UE_AFAR	0x10038UL

#define SCHIZO_UEAFSR_PPIO	0x8000000000000000UL /* Safari */
#define SCHIZO_UEAFSR_PDRD	0x4000000000000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_PDWR	0x2000000000000000UL /* Safari */
#define SCHIZO_UEAFSR_SPIO	0x1000000000000000UL /* Safari */
#define SCHIZO_UEAFSR_SDMA	0x0800000000000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_ERRPNDG	0x0300000000000000UL /* Safari */
#define SCHIZO_UEAFSR_BMSK	0x000003ff00000000UL /* Safari */
#define SCHIZO_UEAFSR_QOFF	0x00000000c0000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_AID	0x000000001f000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_PARTIAL	0x0000000000800000UL /* Safari */
#define SCHIZO_UEAFSR_OWNEDIN	0x0000000000400000UL /* Safari */
#define SCHIZO_UEAFSR_MTAGSYND	0x00000000000f0000UL /* Safari */
#define SCHIZO_UEAFSR_MTAG	0x000000000000e000UL /* Safari */
#define SCHIZO_UEAFSR_ECCSYND	0x00000000000001ffUL /* Safari */

static irqreturn_t schizo_ue_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->pbm_B.controller_regs + SCHIZO_UE_AFSR;
	unsigned long afar_reg = p->pbm_B.controller_regs + SCHIZO_UE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported, limit;

	/* Latch uncorrectable error status. */
	afar = schizo_read(afar_reg);

	/* If either of the error pending bits are set in the
	 * AFSR, the error status is being actively updated by
	 * the hardware and we must re-read to get a clean value.
	 */
	limit = 1000;
	do {
		afsr = schizo_read(afsr_reg);
	} while ((afsr & SCHIZO_UEAFSR_ERRPNDG) != 0 && --limit);

	/* Clear the primary/secondary error status bits. */
	error_bits = afsr &
		(SCHIZO_UEAFSR_PPIO | SCHIZO_UEAFSR_PDRD | SCHIZO_UEAFSR_PDWR |
		 SCHIZO_UEAFSR_SPIO | SCHIZO_UEAFSR_SDMA);
	if (!error_bits)
		return IRQ_NONE;
	schizo_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PCI%d: Uncorrectable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & SCHIZO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SCHIZO_UEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & SCHIZO_UEAFSR_PDWR) ?
		   "DMA Write" : "???")))));
	printk("PCI%d: bytemask[%04lx] qword_offset[%lx] SAFARI_AID[%02lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_BMSK) >> 32UL,
	       (afsr & SCHIZO_UEAFSR_QOFF) >> 30UL,
	       (afsr & SCHIZO_UEAFSR_AID) >> 24UL);
	printk("PCI%d: partial[%d] owned_in[%d] mtag[%lx] mtag_synd[%lx] ecc_sync[%lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_PARTIAL) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_OWNEDIN) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_MTAG) >> 13UL,
	       (afsr & SCHIZO_UEAFSR_MTAGSYND) >> 16UL,
	       (afsr & SCHIZO_UEAFSR_ECCSYND) >> 0UL);
	printk("PCI%d: UE AFAR [%016lx]\n", p->index, afar);
	printk("PCI%d: UE Secondary errors [", p->index);
	reported = 0;
	if (afsr & SCHIZO_UEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SCHIZO_UEAFSR_SDMA) {
		reported++;
		printk("(DMA)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* Interrogate IOMMU for error status. */
	schizo_check_iommu_error(p, UE_ERR);

	schizo_clear_other_err_intr(p, irq);

	return IRQ_HANDLED;
}

#define SCHIZO_CE_AFSR	0x10040UL
#define SCHIZO_CE_AFAR	0x10048UL

#define SCHIZO_CEAFSR_PPIO	0x8000000000000000UL
#define SCHIZO_CEAFSR_PDRD	0x4000000000000000UL
#define SCHIZO_CEAFSR_PDWR	0x2000000000000000UL
#define SCHIZO_CEAFSR_SPIO	0x1000000000000000UL
#define SCHIZO_CEAFSR_SDMA	0x0800000000000000UL
#define SCHIZO_CEAFSR_ERRPNDG	0x0300000000000000UL
#define SCHIZO_CEAFSR_BMSK	0x000003ff00000000UL
#define SCHIZO_CEAFSR_QOFF	0x00000000c0000000UL
#define SCHIZO_CEAFSR_AID	0x000000001f000000UL
#define SCHIZO_CEAFSR_PARTIAL	0x0000000000800000UL
#define SCHIZO_CEAFSR_OWNEDIN	0x0000000000400000UL
#define SCHIZO_CEAFSR_MTAGSYND	0x00000000000f0000UL
#define SCHIZO_CEAFSR_MTAG	0x000000000000e000UL
#define SCHIZO_CEAFSR_ECCSYND	0x00000000000001ffUL

static irqreturn_t schizo_ce_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->pbm_B.controller_regs + SCHIZO_CE_AFSR;
	unsigned long afar_reg = p->pbm_B.controller_regs + SCHIZO_CE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported, limit;

	/* Latch error status. */
	afar = schizo_read(afar_reg);

	/* If either of the error pending bits are set in the
	 * AFSR, the error status is being actively updated by
	 * the hardware and we must re-read to get a clean value.
	 */
	limit = 1000;
	do {
		afsr = schizo_read(afsr_reg);
	} while ((afsr & SCHIZO_UEAFSR_ERRPNDG) != 0 && --limit);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SCHIZO_CEAFSR_PPIO | SCHIZO_CEAFSR_PDRD | SCHIZO_CEAFSR_PDWR |
		 SCHIZO_CEAFSR_SPIO | SCHIZO_CEAFSR_SDMA);
	if (!error_bits)
		return IRQ_NONE;
	schizo_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PCI%d: Correctable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & SCHIZO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SCHIZO_CEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & SCHIZO_CEAFSR_PDWR) ?
		   "DMA Write" : "???")))));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("PCI%d: bytemask[%04lx] qword_offset[%lx] SAFARI_AID[%02lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_BMSK) >> 32UL,
	       (afsr & SCHIZO_UEAFSR_QOFF) >> 30UL,
	       (afsr & SCHIZO_UEAFSR_AID) >> 24UL);
	printk("PCI%d: partial[%d] owned_in[%d] mtag[%lx] mtag_synd[%lx] ecc_sync[%lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_PARTIAL) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_OWNEDIN) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_MTAG) >> 13UL,
	       (afsr & SCHIZO_UEAFSR_MTAGSYND) >> 16UL,
	       (afsr & SCHIZO_UEAFSR_ECCSYND) >> 0UL);
	printk("PCI%d: CE AFAR [%016lx]\n", p->index, afar);
	printk("PCI%d: CE Secondary errors [", p->index);
	reported = 0;
	if (afsr & SCHIZO_CEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SCHIZO_CEAFSR_SDMA) {
		reported++;
		printk("(DMA)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	schizo_clear_other_err_intr(p, irq);

	return IRQ_HANDLED;
}

#define SCHIZO_PCI_AFSR	0x2010UL
#define SCHIZO_PCI_AFAR	0x2018UL

#define SCHIZO_PCIAFSR_PMA	0x8000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PTA	0x4000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PRTRY	0x2000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PPERR	0x1000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PTTO	0x0800000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PUNUS	0x0400000000000000UL /* Schizo */
#define SCHIZO_PCIAFSR_SMA	0x0200000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_STA	0x0100000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_SRTRY	0x0080000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_SPERR	0x0040000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_STTO	0x0020000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_SUNUS	0x0010000000000000UL /* Schizo */
#define SCHIZO_PCIAFSR_BMSK	0x000003ff00000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_BLK	0x0000000080000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_CFG	0x0000000040000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_MEM	0x0000000020000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_IO	0x0000000010000000UL /* Schizo/Tomatillo */

#define SCHIZO_PCI_CTRL		(0x2000UL)
#define SCHIZO_PCICTRL_BUS_UNUS	(1UL << 63UL) /* Safari */
#define SCHIZO_PCICTRL_DTO_INT	(1UL << 61UL) /* Tomatillo */
#define SCHIZO_PCICTRL_ARB_PRIO (0x1ff << 52UL) /* Tomatillo */
#define SCHIZO_PCICTRL_ESLCK	(1UL << 51UL) /* Safari */
#define SCHIZO_PCICTRL_ERRSLOT	(7UL << 48UL) /* Safari */
#define SCHIZO_PCICTRL_TTO_ERR	(1UL << 38UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_RTRY_ERR	(1UL << 37UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_DTO_ERR	(1UL << 36UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_SBH_ERR	(1UL << 35UL) /* Safari */
#define SCHIZO_PCICTRL_SERR	(1UL << 34UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PCISPD	(1UL << 33UL) /* Safari */
#define SCHIZO_PCICTRL_MRM_PREF	(1UL << 30UL) /* Tomatillo */
#define SCHIZO_PCICTRL_RDO_PREF	(1UL << 29UL) /* Tomatillo */
#define SCHIZO_PCICTRL_RDL_PREF	(1UL << 28UL) /* Tomatillo */
#define SCHIZO_PCICTRL_PTO	(3UL << 24UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PTO_SHIFT 24UL
#define SCHIZO_PCICTRL_TRWSW	(7UL << 21UL) /* Tomatillo */
#define SCHIZO_PCICTRL_F_TGT_A	(1UL << 20UL) /* Tomatillo */
#define SCHIZO_PCICTRL_S_DTO_INT (1UL << 19UL) /* Safari */
#define SCHIZO_PCICTRL_F_TGT_RT	(1UL << 19UL) /* Tomatillo */
#define SCHIZO_PCICTRL_SBH_INT	(1UL << 18UL) /* Safari */
#define SCHIZO_PCICTRL_T_DTO_INT (1UL << 18UL) /* Tomatillo */
#define SCHIZO_PCICTRL_EEN	(1UL << 17UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PARK	(1UL << 16UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PCIRST	(1UL <<  8UL) /* Safari */
#define SCHIZO_PCICTRL_ARB_S	(0x3fUL << 0UL) /* Safari */
#define SCHIZO_PCICTRL_ARB_T	(0xffUL << 0UL) /* Tomatillo */

static irqreturn_t schizo_pcierr_intr_other(struct pci_pbm_info *pbm)
{
	unsigned long csr_reg, csr, csr_error_bits;
	irqreturn_t ret = IRQ_NONE;
	u16 stat;

	csr_reg = pbm->pbm_regs + SCHIZO_PCI_CTRL;
	csr = schizo_read(csr_reg);
	csr_error_bits =
		csr & (SCHIZO_PCICTRL_BUS_UNUS |
		       SCHIZO_PCICTRL_TTO_ERR |
		       SCHIZO_PCICTRL_RTRY_ERR |
		       SCHIZO_PCICTRL_DTO_ERR |
		       SCHIZO_PCICTRL_SBH_ERR |
		       SCHIZO_PCICTRL_SERR);
	if (csr_error_bits) {
		/* Clear the errors.  */
		schizo_write(csr_reg, csr);

		/* Log 'em.  */
		if (csr_error_bits & SCHIZO_PCICTRL_BUS_UNUS)
			printk("%s: Bus unusable error asserted.\n",
			       pbm->name);
		if (csr_error_bits & SCHIZO_PCICTRL_TTO_ERR)
			printk("%s: PCI TRDY# timeout error asserted.\n",
			       pbm->name);
		if (csr_error_bits & SCHIZO_PCICTRL_RTRY_ERR)
			printk("%s: PCI excessive retry error asserted.\n",
			       pbm->name);
		if (csr_error_bits & SCHIZO_PCICTRL_DTO_ERR)
			printk("%s: PCI discard timeout error asserted.\n",
			       pbm->name);
		if (csr_error_bits & SCHIZO_PCICTRL_SBH_ERR)
			printk("%s: PCI streaming byte hole error asserted.\n",
			       pbm->name);
		if (csr_error_bits & SCHIZO_PCICTRL_SERR)
			printk("%s: PCI SERR signal asserted.\n",
			       pbm->name);
		ret = IRQ_HANDLED;
	}
	pci_read_config_word(pbm->pci_bus->self, PCI_STATUS, &stat);
	if (stat & (PCI_STATUS_PARITY |
		    PCI_STATUS_SIG_TARGET_ABORT |
		    PCI_STATUS_REC_TARGET_ABORT |
		    PCI_STATUS_REC_MASTER_ABORT |
		    PCI_STATUS_SIG_SYSTEM_ERROR)) {
		printk("%s: PCI bus error, PCI_STATUS[%04x]\n",
		       pbm->name, stat);
		pci_write_config_word(pbm->pci_bus->self, PCI_STATUS, 0xffff);
		ret = IRQ_HANDLED;
	}
	return ret;
}

static irqreturn_t schizo_pcierr_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_pbm_info *pbm = dev_id;
	struct pci_controller_info *p = pbm->parent;
	unsigned long afsr_reg, afar_reg, base;
	unsigned long afsr, afar, error_bits;
	int reported;

	base = pbm->pbm_regs;

	afsr_reg = base + SCHIZO_PCI_AFSR;
	afar_reg = base + SCHIZO_PCI_AFAR;

	/* Latch error status. */
	afar = schizo_read(afar_reg);
	afsr = schizo_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		 SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		 SCHIZO_PCIAFSR_PTTO | SCHIZO_PCIAFSR_PUNUS |
		 SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		 SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		 SCHIZO_PCIAFSR_STTO | SCHIZO_PCIAFSR_SUNUS);
	if (!error_bits)
		return schizo_pcierr_intr_other(pbm);
	schizo_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("%s: PCI Error, primary error type[%s]\n",
	       pbm->name,
	       (((error_bits & SCHIZO_PCIAFSR_PMA) ?
		 "Master Abort" :
		 ((error_bits & SCHIZO_PCIAFSR_PTA) ?
		  "Target Abort" :
		  ((error_bits & SCHIZO_PCIAFSR_PRTRY) ?
		   "Excessive Retries" :
		   ((error_bits & SCHIZO_PCIAFSR_PPERR) ?
		    "Parity Error" :
		    ((error_bits & SCHIZO_PCIAFSR_PTTO) ?
		     "Timeout" :
		     ((error_bits & SCHIZO_PCIAFSR_PUNUS) ?
		      "Bus Unusable" : "???"))))))));
	printk("%s: bytemask[%04lx] was_block(%d) space(%s)\n",
	       pbm->name,
	       (afsr & SCHIZO_PCIAFSR_BMSK) >> 32UL,
	       (afsr & SCHIZO_PCIAFSR_BLK) ? 1 : 0,
	       ((afsr & SCHIZO_PCIAFSR_CFG) ?
		"Config" :
		((afsr & SCHIZO_PCIAFSR_MEM) ?
		 "Memory" :
		 ((afsr & SCHIZO_PCIAFSR_IO) ?
		  "I/O" : "???"))));
	printk("%s: PCI AFAR [%016lx]\n",
	       pbm->name, afar);
	printk("%s: PCI Secondary errors [",
	       pbm->name);
	reported = 0;
	if (afsr & SCHIZO_PCIAFSR_SMA) {
		reported++;
		printk("(Master Abort)");
	}
	if (afsr & SCHIZO_PCIAFSR_STA) {
		reported++;
		printk("(Target Abort)");
	}
	if (afsr & SCHIZO_PCIAFSR_SRTRY) {
		reported++;
		printk("(Excessive Retries)");
	}
	if (afsr & SCHIZO_PCIAFSR_SPERR) {
		reported++;
		printk("(Parity Error)");
	}
	if (afsr & SCHIZO_PCIAFSR_STTO) {
		reported++;
		printk("(Timeout)");
	}
	if (afsr & SCHIZO_PCIAFSR_SUNUS) {
		reported++;
		printk("(Bus Unusable)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* For the error types shown, scan PBM's PCI bus for devices
	 * which have logged that error type.
	 */

	/* If we see a Target Abort, this could be the result of an
	 * IOMMU translation error of some sort.  It is extremely
	 * useful to log this information as usually it indicates
	 * a bug in the IOMMU support code or a PCI device driver.
	 */
	if (error_bits & (SCHIZO_PCIAFSR_PTA | SCHIZO_PCIAFSR_STA)) {
		schizo_check_iommu_error(p, PCI_ERR);
		pci_scan_for_target_abort(p, pbm, pbm->pci_bus);
	}
	if (error_bits & (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_SMA))
		pci_scan_for_master_abort(p, pbm, pbm->pci_bus);

	/* For excessive retries, PSYCHO/PBM will abort the device
	 * and there is no way to specifically check for excessive
	 * retries in the config space status registers.  So what
	 * we hope is that we'll catch it via the master/target
	 * abort events.
	 */

	if (error_bits & (SCHIZO_PCIAFSR_PPERR | SCHIZO_PCIAFSR_SPERR))
		pci_scan_for_parity_error(p, pbm, pbm->pci_bus);

	schizo_clear_other_err_intr(p, irq);

	return IRQ_HANDLED;
}

#define SCHIZO_SAFARI_ERRLOG	0x10018UL

#define SAFARI_ERRLOG_ERROUT	0x8000000000000000UL

#define BUS_ERROR_BADCMD	0x4000000000000000UL /* Schizo/Tomatillo */
#define BUS_ERROR_SSMDIS	0x2000000000000000UL /* Safari */
#define BUS_ERROR_BADMA		0x1000000000000000UL /* Safari */
#define BUS_ERROR_BADMB		0x0800000000000000UL /* Safari */
#define BUS_ERROR_BADMC		0x0400000000000000UL /* Safari */
#define BUS_ERROR_SNOOP_GR	0x0000000000200000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_PCI	0x0000000000100000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RD	0x0000000000080000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RDS	0x0000000000020000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RDSA	0x0000000000010000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_OWN	0x0000000000008000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RDO	0x0000000000004000UL /* Tomatillo */
#define BUS_ERROR_CPU1PS	0x0000000000002000UL /* Safari */
#define BUS_ERROR_WDATA_PERR	0x0000000000002000UL /* Tomatillo */
#define BUS_ERROR_CPU1PB	0x0000000000001000UL /* Safari */
#define BUS_ERROR_CTRL_PERR	0x0000000000001000UL /* Tomatillo */
#define BUS_ERROR_CPU0PS	0x0000000000000800UL /* Safari */
#define BUS_ERROR_SNOOP_ERR	0x0000000000000800UL /* Tomatillo */
#define BUS_ERROR_CPU0PB	0x0000000000000400UL /* Safari */
#define BUS_ERROR_JBUS_ILL_B	0x0000000000000400UL /* Tomatillo */
#define BUS_ERROR_CIQTO		0x0000000000000200UL /* Safari */
#define BUS_ERROR_LPQTO		0x0000000000000100UL /* Safari */
#define BUS_ERROR_JBUS_ILL_C	0x0000000000000100UL /* Tomatillo */
#define BUS_ERROR_SFPQTO	0x0000000000000080UL /* Safari */
#define BUS_ERROR_UFPQTO	0x0000000000000040UL /* Safari */
#define BUS_ERROR_RD_PERR	0x0000000000000040UL /* Tomatillo */
#define BUS_ERROR_APERR		0x0000000000000020UL /* Safari/Tomatillo */
#define BUS_ERROR_UNMAP		0x0000000000000010UL /* Safari/Tomatillo */
#define BUS_ERROR_BUSERR	0x0000000000000004UL /* Safari/Tomatillo */
#define BUS_ERROR_TIMEOUT	0x0000000000000002UL /* Safari/Tomatillo */
#define BUS_ERROR_ILL		0x0000000000000001UL /* Safari */

/* We only expect UNMAP errors here.  The rest of the Safari errors
 * are marked fatal and thus cause a system reset.
 */
static irqreturn_t schizo_safarierr_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	u64 errlog;

	errlog = schizo_read(p->pbm_B.controller_regs + SCHIZO_SAFARI_ERRLOG);
	schizo_write(p->pbm_B.controller_regs + SCHIZO_SAFARI_ERRLOG,
		     errlog & ~(SAFARI_ERRLOG_ERROUT));

	if (!(errlog & BUS_ERROR_UNMAP)) {
		printk("PCI%d: Unexpected Safari/JBUS error interrupt, errlog[%016lx]\n",
		       p->index, errlog);

		schizo_clear_other_err_intr(p, irq);
		return IRQ_HANDLED;
	}

	printk("PCI%d: Safari/JBUS interrupt, UNMAPPED error, interrogating IOMMUs.\n",
	       p->index);
	schizo_check_iommu_error(p, SAFARI_ERR);

	schizo_clear_other_err_intr(p, irq);
	return IRQ_HANDLED;
}

/* Nearly identical to PSYCHO equivalents... */
#define SCHIZO_ECC_CTRL		0x10020UL
#define  SCHIZO_ECCCTRL_EE	 0x8000000000000000UL /* Enable ECC Checking */
#define  SCHIZO_ECCCTRL_UE	 0x4000000000000000UL /* Enable UE Interrupts */
#define  SCHIZO_ECCCTRL_CE	 0x2000000000000000UL /* Enable CE INterrupts */

#define SCHIZO_SAFARI_ERRCTRL	0x10008UL
#define  SCHIZO_SAFERRCTRL_EN	 0x8000000000000000UL
#define SCHIZO_SAFARI_IRQCTRL	0x10010UL
#define  SCHIZO_SAFIRQCTRL_EN	 0x8000000000000000UL

/* How the Tomatillo IRQs are routed around is pure guesswork here.
 *
 * All the Tomatillo devices I see in prtconf dumps seem to have only
 * a single PCI bus unit attached to it.  It would seem they are seperate
 * devices because their PortID (ie. JBUS ID) values are all different
 * and thus the registers are mapped to totally different locations.
 *
 * However, two Tomatillo's look "similar" in that the only difference
 * in their PortID is the lowest bit.
 *
 * So if we were to ignore this lower bit, it certainly looks like two
 * PCI bus units of the same Tomatillo.  I still have not really
 * figured this out...
 */
static void tomatillo_register_error_handlers(struct pci_controller_info *p)
{
	struct pci_pbm_info *pbm;
	unsigned int irq;
	u64 tmp, err_mask, err_no_mask;

	/* Build IRQs and register handlers. */
	pbm = pbm_for_ino(p, SCHIZO_UE_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_UE_INO);
	if (request_irq(irq, schizo_ue_intr,
			SA_SHIRQ, "TOMATILLO UE", p) < 0) {
		prom_printf("%s: Cannot register UE interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_UE_INO));
	upa_writel(tmp, (pbm->pbm_regs +
			 schizo_imap_offset(SCHIZO_UE_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_CE_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_CE_INO);
	if (request_irq(irq, schizo_ce_intr,
			SA_SHIRQ, "TOMATILLO CE", p) < 0) {
		prom_printf("%s: Cannot register CE interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_CE_INO));
	upa_writel(tmp, (pbm->pbm_regs +
			 schizo_imap_offset(SCHIZO_CE_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_PCIERR_A_INO);
	irq = schizo_irq_build(pbm, NULL, ((pbm->portid << 6) |
					   SCHIZO_PCIERR_A_INO));
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "TOMATILLO PCIERR", pbm) < 0) {
		prom_printf("%s: Cannot register PBM A PciERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, ((pbm->portid << 6) |
						 SCHIZO_PCIERR_A_INO)));
	upa_writel(tmp, (pbm->pbm_regs +
			 schizo_imap_offset(SCHIZO_PCIERR_A_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_PCIERR_B_INO);
	irq = schizo_irq_build(pbm, NULL, ((pbm->portid << 6) |
					    SCHIZO_PCIERR_B_INO));
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "TOMATILLO PCIERR", pbm) < 0) {
		prom_printf("%s: Cannot register PBM B PciERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, ((pbm->portid << 6) |
						 SCHIZO_PCIERR_B_INO)));
	upa_writel(tmp, (pbm->pbm_regs +
			 schizo_imap_offset(SCHIZO_PCIERR_B_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_SERR_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_SERR_INO);
	if (request_irq(irq, schizo_safarierr_intr,
			SA_SHIRQ, "TOMATILLO SERR", p) < 0) {
		prom_printf("%s: Cannot register SafariERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, ((pbm->portid << 6) |
						 SCHIZO_SERR_INO)));
	upa_writel(tmp, (pbm->pbm_regs +
			 schizo_imap_offset(SCHIZO_SERR_INO) + 4));

	/* Enable UE and CE interrupts for controller. */
	schizo_write(p->pbm_A.controller_regs + SCHIZO_ECC_CTRL,
		     (SCHIZO_ECCCTRL_EE |
		      SCHIZO_ECCCTRL_UE |
		      SCHIZO_ECCCTRL_CE));

	schizo_write(p->pbm_B.controller_regs + SCHIZO_ECC_CTRL,
		     (SCHIZO_ECCCTRL_EE |
		      SCHIZO_ECCCTRL_UE |
		      SCHIZO_ECCCTRL_CE));

	/* Enable PCI Error interrupts and clear error
	 * bits.
	 */
	err_mask = (SCHIZO_PCICTRL_BUS_UNUS |
		    SCHIZO_PCICTRL_TTO_ERR |
		    SCHIZO_PCICTRL_RTRY_ERR |
		    SCHIZO_PCICTRL_SERR |
		    SCHIZO_PCICTRL_EEN);

	err_no_mask = SCHIZO_PCICTRL_DTO_ERR;

	tmp = schizo_read(p->pbm_A.pbm_regs + SCHIZO_PCI_CTRL);
	tmp |= err_mask;
	tmp &= ~err_no_mask;
	schizo_write(p->pbm_A.pbm_regs + SCHIZO_PCI_CTRL, tmp);

	tmp = schizo_read(p->pbm_B.pbm_regs + SCHIZO_PCI_CTRL);
	tmp |= err_mask;
	tmp &= ~err_no_mask;
	schizo_write(p->pbm_B.pbm_regs + SCHIZO_PCI_CTRL, tmp);

	err_mask = (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		    SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		    SCHIZO_PCIAFSR_PTTO |
		    SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		    SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		    SCHIZO_PCIAFSR_STTO);

	schizo_write(p->pbm_A.pbm_regs + SCHIZO_PCI_AFSR, err_mask);
	schizo_write(p->pbm_B.pbm_regs + SCHIZO_PCI_AFSR, err_mask);

	err_mask = (BUS_ERROR_BADCMD | BUS_ERROR_SNOOP_GR |
		    BUS_ERROR_SNOOP_PCI | BUS_ERROR_SNOOP_RD |
		    BUS_ERROR_SNOOP_RDS | BUS_ERROR_SNOOP_RDSA |
		    BUS_ERROR_SNOOP_OWN | BUS_ERROR_SNOOP_RDO |
		    BUS_ERROR_WDATA_PERR | BUS_ERROR_CTRL_PERR |
		    BUS_ERROR_SNOOP_ERR | BUS_ERROR_JBUS_ILL_B |
		    BUS_ERROR_JBUS_ILL_C | BUS_ERROR_RD_PERR |
		    BUS_ERROR_APERR | BUS_ERROR_UNMAP |
		    BUS_ERROR_BUSERR | BUS_ERROR_TIMEOUT);

	schizo_write(p->pbm_A.controller_regs + SCHIZO_SAFARI_ERRCTRL,
		     (SCHIZO_SAFERRCTRL_EN | err_mask));
	schizo_write(p->pbm_B.controller_regs + SCHIZO_SAFARI_ERRCTRL,
		     (SCHIZO_SAFERRCTRL_EN | err_mask));

	schizo_write(p->pbm_A.controller_regs + SCHIZO_SAFARI_IRQCTRL,
		     (SCHIZO_SAFIRQCTRL_EN | (BUS_ERROR_UNMAP)));
	schizo_write(p->pbm_B.controller_regs + SCHIZO_SAFARI_IRQCTRL,
		     (SCHIZO_SAFIRQCTRL_EN | (BUS_ERROR_UNMAP)));
}

static void schizo_register_error_handlers(struct pci_controller_info *p)
{
	struct pci_pbm_info *pbm;
	unsigned int irq;
	u64 tmp, err_mask, err_no_mask;

	/* Build IRQs and register handlers. */
	pbm = pbm_for_ino(p, SCHIZO_UE_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_UE_INO);
	if (request_irq(irq, schizo_ue_intr,
			SA_SHIRQ, "SCHIZO UE", p) < 0) {
		prom_printf("%s: Cannot register UE interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_UE_INO));
	upa_writel(tmp, (pbm->pbm_regs + schizo_imap_offset(SCHIZO_UE_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_CE_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_CE_INO);
	if (request_irq(irq, schizo_ce_intr,
			SA_SHIRQ, "SCHIZO CE", p) < 0) {
		prom_printf("%s: Cannot register CE interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_CE_INO));
	upa_writel(tmp, (pbm->pbm_regs + schizo_imap_offset(SCHIZO_CE_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_PCIERR_A_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_PCIERR_A_INO);
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "SCHIZO PCIERR", pbm) < 0) {
		prom_printf("%s: Cannot register PBM A PciERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_PCIERR_A_INO));
	upa_writel(tmp, (pbm->pbm_regs + schizo_imap_offset(SCHIZO_PCIERR_A_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_PCIERR_B_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_PCIERR_B_INO);
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "SCHIZO PCIERR", &p->pbm_B) < 0) {
		prom_printf("%s: Cannot register PBM B PciERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_PCIERR_B_INO));
	upa_writel(tmp, (pbm->pbm_regs + schizo_imap_offset(SCHIZO_PCIERR_B_INO) + 4));

	pbm = pbm_for_ino(p, SCHIZO_SERR_INO);
	irq = schizo_irq_build(pbm, NULL, (pbm->portid << 6) | SCHIZO_SERR_INO);
	if (request_irq(irq, schizo_safarierr_intr,
			SA_SHIRQ, "SCHIZO SERR", p) < 0) {
		prom_printf("%s: Cannot register SafariERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	tmp = upa_readl(schizo_ino_to_imap(pbm, (pbm->portid << 6) | SCHIZO_SERR_INO));
	upa_writel(tmp, (pbm->pbm_regs + schizo_imap_offset(SCHIZO_SERR_INO) + 4));

	/* Enable UE and CE interrupts for controller. */
	schizo_write(p->pbm_A.controller_regs + SCHIZO_ECC_CTRL,
		     (SCHIZO_ECCCTRL_EE |
		      SCHIZO_ECCCTRL_UE |
		      SCHIZO_ECCCTRL_CE));

	err_mask = (SCHIZO_PCICTRL_BUS_UNUS |
		    SCHIZO_PCICTRL_ESLCK |
		    SCHIZO_PCICTRL_TTO_ERR |
		    SCHIZO_PCICTRL_RTRY_ERR |
		    SCHIZO_PCICTRL_SBH_ERR |
		    SCHIZO_PCICTRL_SERR |
		    SCHIZO_PCICTRL_EEN);

	err_no_mask = (SCHIZO_PCICTRL_DTO_ERR |
		       SCHIZO_PCICTRL_SBH_INT);

	/* Enable PCI Error interrupts and clear error
	 * bits for each PBM.
	 */
	tmp = schizo_read(p->pbm_A.pbm_regs + SCHIZO_PCI_CTRL);
	tmp |= err_mask;
	tmp &= ~err_no_mask;
	schizo_write(p->pbm_A.pbm_regs + SCHIZO_PCI_CTRL, tmp);

	schizo_write(p->pbm_A.pbm_regs + SCHIZO_PCI_AFSR,
		     (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		      SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		      SCHIZO_PCIAFSR_PTTO | SCHIZO_PCIAFSR_PUNUS |
		      SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		      SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		      SCHIZO_PCIAFSR_STTO | SCHIZO_PCIAFSR_SUNUS));

	tmp = schizo_read(p->pbm_B.pbm_regs + SCHIZO_PCI_CTRL);
	tmp |= err_mask;
	tmp &= ~err_no_mask;
	schizo_write(p->pbm_B.pbm_regs + SCHIZO_PCI_CTRL, tmp);

	schizo_write(p->pbm_B.pbm_regs + SCHIZO_PCI_AFSR,
		     (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		      SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		      SCHIZO_PCIAFSR_PTTO | SCHIZO_PCIAFSR_PUNUS |
		      SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		      SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		      SCHIZO_PCIAFSR_STTO | SCHIZO_PCIAFSR_SUNUS));

	/* Make all Safari error conditions fatal except unmapped
	 * errors which we make generate interrupts.
	 */
	err_mask = (BUS_ERROR_BADCMD | BUS_ERROR_SSMDIS |
		    BUS_ERROR_BADMA | BUS_ERROR_BADMB |
		    BUS_ERROR_BADMC |
		    BUS_ERROR_CPU1PS | BUS_ERROR_CPU1PB |
		    BUS_ERROR_CPU0PS | BUS_ERROR_CPU0PB |
		    BUS_ERROR_CIQTO |
		    BUS_ERROR_LPQTO | BUS_ERROR_SFPQTO |
		    BUS_ERROR_UFPQTO | BUS_ERROR_APERR |
		    BUS_ERROR_BUSERR | BUS_ERROR_TIMEOUT |
		    BUS_ERROR_ILL);
#if 1
	/* XXX Something wrong with some Excalibur systems
	 * XXX Sun is shipping.  The behavior on a 2-cpu
	 * XXX machine is that both CPU1 parity error bits
	 * XXX are set and are immediately set again when
	 * XXX their error status bits are cleared.  Just
	 * XXX ignore them for now.  -DaveM
	 */
	err_mask &= ~(BUS_ERROR_CPU1PS | BUS_ERROR_CPU1PB |
		      BUS_ERROR_CPU0PS | BUS_ERROR_CPU0PB);
#endif

	schizo_write(p->pbm_A.controller_regs + SCHIZO_SAFARI_ERRCTRL,
		     (SCHIZO_SAFERRCTRL_EN | err_mask));

	schizo_write(p->pbm_A.controller_regs + SCHIZO_SAFARI_IRQCTRL,
		     (SCHIZO_SAFIRQCTRL_EN | (BUS_ERROR_UNMAP)));
}

static void pbm_config_busmastering(struct pci_pbm_info *pbm)
{
	u8 *addr;

	/* Set cache-line size to 64 bytes, this is actually
	 * a nop but I do it for completeness.
	 */
	addr = schizo_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PCI_CACHE_LINE_SIZE);
	pci_config_write8(addr, 64 / sizeof(u32));

	/* Set PBM latency timer to 64 PCI clocks. */
	addr = schizo_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PCI_LATENCY_TIMER);
	pci_config_write8(addr, 64);
}

static void pbm_scan_bus(struct pci_controller_info *p,
			 struct pci_pbm_info *pbm)
{
	struct pcidev_cookie *cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);

	if (!cookie) {
		prom_printf("%s: Critical allocation failure.\n", pbm->name);
		prom_halt();
	}

	/* All we care about is the PBM. */
	cookie->pbm = pbm;

	pbm->pci_bus = pci_scan_bus(pbm->pci_first_busno,
				    p->pci_ops,
				    pbm);
	pci_fixup_host_bridge_self(pbm->pci_bus);
	pbm->pci_bus->self->sysdata = cookie;

	pci_fill_in_pbm_cookies(pbm->pci_bus, pbm, pbm->prom_node);
	pci_record_assignments(pbm, pbm->pci_bus);
	pci_assign_unassigned(pbm, pbm->pci_bus);
	pci_fixup_irq(pbm, pbm->pci_bus);
	pci_determine_66mhz_disposition(pbm, pbm->pci_bus);
	pci_setup_busmastering(pbm, pbm->pci_bus);
}

static void __schizo_scan_bus(struct pci_controller_info *p,
			      int chip_type)
{
	if (!p->pbm_B.prom_node || !p->pbm_A.prom_node) {
		printk("PCI: Only one PCI bus module of controller found.\n");
		printk("PCI: Ignoring entire controller.\n");
		return;
	}

	pbm_config_busmastering(&p->pbm_B);
	p->pbm_B.is_66mhz_capable =
		prom_getbool(p->pbm_B.prom_node, "66mhz-capable");
	pbm_config_busmastering(&p->pbm_A);
	p->pbm_A.is_66mhz_capable =
		prom_getbool(p->pbm_A.prom_node, "66mhz-capable");
	pbm_scan_bus(p, &p->pbm_B);
	pbm_scan_bus(p, &p->pbm_A);

	/* After the PCI bus scan is complete, we can register
	 * the error interrupt handlers.
	 */
	if (chip_type == PBM_CHIP_TYPE_TOMATILLO)
		tomatillo_register_error_handlers(p);
	else
		schizo_register_error_handlers(p);
}

static void schizo_scan_bus(struct pci_controller_info *p)
{
	__schizo_scan_bus(p, PBM_CHIP_TYPE_SCHIZO);
}

static void tomatillo_scan_bus(struct pci_controller_info *p)
{
	__schizo_scan_bus(p, PBM_CHIP_TYPE_TOMATILLO);
}

static void schizo_base_address_update(struct pci_dev *pdev, int resource)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct resource *res, *root;
	u32 reg;
	int where, size, is_64bit;

	res = &pdev->resource[resource];
	if (resource < 6) {
		where = PCI_BASE_ADDRESS_0 + (resource * 4);
	} else if (resource == PCI_ROM_RESOURCE) {
		where = pdev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}

	is_64bit = 0;
	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else {
		root = &pbm->mem_space;
		if ((res->flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
		    == PCI_BASE_ADDRESS_MEM_TYPE_64)
			is_64bit = 1;
	}

	size = res->end - res->start;
	pci_read_config_dword(pdev, where, &reg);
	reg = ((reg & size) |
	       (((u32)(res->start - root->start)) & ~size));
	if (resource == PCI_ROM_RESOURCE) {
		reg |= PCI_ROM_ADDRESS_ENABLE;
		res->flags |= IORESOURCE_ROM_ENABLE;
	}
	pci_write_config_dword(pdev, where, reg);

	/* This knows that the upper 32-bits of the address
	 * must be zero.  Our PCI common layer enforces this.
	 */
	if (is_64bit)
		pci_write_config_dword(pdev, where + 4, 0);
}

static void schizo_resource_adjust(struct pci_dev *pdev,
				   struct resource *res,
				   struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

/* Use ranges property to determine where PCI MEM, I/O, and Config
 * space are for this PCI bus module.
 */
static void schizo_determine_mem_io_space(struct pci_pbm_info *pbm)
{
	int i, saw_cfg, saw_mem, saw_io;

	saw_cfg = saw_mem = saw_io = 0;
	for (i = 0; i < pbm->num_pbm_ranges; i++) {
		struct linux_prom_pci_ranges *pr = &pbm->pbm_ranges[i];
		unsigned long a;
		int type;

		type = (pr->child_phys_hi >> 24) & 0x3;
		a = (((unsigned long)pr->parent_phys_hi << 32UL) |
		     ((unsigned long)pr->parent_phys_lo  <<  0UL));

		switch (type) {
		case 0:
			/* PCI config space, 16MB */
			pbm->config_space = a;
			saw_cfg = 1;
			break;

		case 1:
			/* 16-bit IO space, 16MB */
			pbm->io_space.start = a;
			pbm->io_space.end = a + ((16UL*1024UL*1024UL) - 1UL);
			pbm->io_space.flags = IORESOURCE_IO;
			saw_io = 1;
			break;

		case 2:
			/* 32-bit MEM space, 2GB */
			pbm->mem_space.start = a;
			pbm->mem_space.end = a + (0x80000000UL - 1UL);
			pbm->mem_space.flags = IORESOURCE_MEM;
			saw_mem = 1;
			break;

		default:
			break;
		};
	}

	if (!saw_cfg || !saw_io || !saw_mem) {
		prom_printf("%s: Fatal error, missing %s PBM range.\n",
			    pbm->name,
			    ((!saw_cfg ?
			      "CFG" :
			      (!saw_io ?
			       "IO" : "MEM"))));
		prom_halt();
	}

	printk("%s: PCI CFG[%lx] IO[%lx] MEM[%lx]\n",
	       pbm->name,
	       pbm->config_space,
	       pbm->io_space.start,
	       pbm->mem_space.start);
}

static void pbm_register_toplevel_resources(struct pci_controller_info *p,
					    struct pci_pbm_info *pbm)
{
	pbm->io_space.name = pbm->mem_space.name = pbm->name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);
	pci_register_legacy_regions(&pbm->io_space,
				    &pbm->mem_space);
}

#define SCHIZO_STRBUF_CONTROL		(0x02800UL)
#define SCHIZO_STRBUF_FLUSH		(0x02808UL)
#define SCHIZO_STRBUF_FSYNC		(0x02810UL)
#define SCHIZO_STRBUF_CTXFLUSH		(0x02818UL)
#define SCHIZO_STRBUF_CTXMATCH		(0x10000UL)

static void schizo_pbm_strbuf_init(struct pci_pbm_info *pbm)
{
	unsigned long base = pbm->pbm_regs;
	u64 control;

	if (pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO) {
		/* TOMATILLO lacks streaming cache.  */
		return;
	}

	/* SCHIZO has context flushing. */
	pbm->stc.strbuf_control		= base + SCHIZO_STRBUF_CONTROL;
	pbm->stc.strbuf_pflush		= base + SCHIZO_STRBUF_FLUSH;
	pbm->stc.strbuf_fsync		= base + SCHIZO_STRBUF_FSYNC;
	pbm->stc.strbuf_ctxflush	= base + SCHIZO_STRBUF_CTXFLUSH;
	pbm->stc.strbuf_ctxmatch_base	= base + SCHIZO_STRBUF_CTXMATCH;

	pbm->stc.strbuf_flushflag = (volatile unsigned long *)
		((((unsigned long)&pbm->stc.__flushflag_buf[0])
		  + 63UL)
		 & ~63UL);
	pbm->stc.strbuf_flushflag_pa = (unsigned long)
		__pa(pbm->stc.strbuf_flushflag);

	/* Turn off LRU locking and diag mode, enable the
	 * streaming buffer and leave the rerun-disable
	 * setting however OBP set it.
	 */
	control = schizo_read(pbm->stc.strbuf_control);
	control &= ~(SCHIZO_STRBUF_CTRL_LPTR |
		     SCHIZO_STRBUF_CTRL_LENAB |
		     SCHIZO_STRBUF_CTRL_DENAB);
	control |= SCHIZO_STRBUF_CTRL_ENAB;
	schizo_write(pbm->stc.strbuf_control, control);

	pbm->stc.strbuf_enabled = 1;
}

#define SCHIZO_IOMMU_CONTROL		(0x00200UL)
#define SCHIZO_IOMMU_TSBBASE		(0x00208UL)
#define SCHIZO_IOMMU_FLUSH		(0x00210UL)
#define SCHIZO_IOMMU_CTXFLUSH		(0x00218UL)

static void schizo_pbm_iommu_init(struct pci_pbm_info *pbm)
{
	struct pci_iommu *iommu = pbm->iommu;
	unsigned long i, tagbase, database;
	u32 vdma[2], dma_mask;
	u64 control;
	int err, tsbsize;

	err = prom_getproperty(pbm->prom_node, "virtual-dma",
			       (char *)&vdma[0], sizeof(vdma));
	if (err == 0 || err == -1) {
		/* No property, use default values. */
		vdma[0] = 0xc0000000;
		vdma[1] = 0x40000000;
	}

	dma_mask = vdma[0];
	switch (vdma[1]) {
		case 0x20000000:
			dma_mask |= 0x1fffffff;
			tsbsize = 64;
			break;

		case 0x40000000:
			dma_mask |= 0x3fffffff;
			tsbsize = 128;
			break;

		case 0x80000000:
			dma_mask |= 0x7fffffff;
			tsbsize = 128;
			break;

		default:
			prom_printf("SCHIZO: strange virtual-dma size.\n");
			prom_halt();
	};

	/* Register addresses, SCHIZO has iommu ctx flushing. */
	iommu->iommu_control  = pbm->pbm_regs + SCHIZO_IOMMU_CONTROL;
	iommu->iommu_tsbbase  = pbm->pbm_regs + SCHIZO_IOMMU_TSBBASE;
	iommu->iommu_flush    = pbm->pbm_regs + SCHIZO_IOMMU_FLUSH;
	iommu->iommu_ctxflush = pbm->pbm_regs + SCHIZO_IOMMU_CTXFLUSH;

	/* We use the main control/status register of SCHIZO as the write
	 * completion register.
	 */
	iommu->write_complete_reg = pbm->controller_regs + 0x10000UL;

	/*
	 * Invalidate TLB Entries.
	 */
	control = schizo_read(iommu->iommu_control);
	control |= SCHIZO_IOMMU_CTRL_DENAB;
	schizo_write(iommu->iommu_control, control);

	tagbase = SCHIZO_IOMMU_TAG, database = SCHIZO_IOMMU_DATA;

	for(i = 0; i < 16; i++) {
		schizo_write(pbm->pbm_regs + tagbase + (i * 8UL), 0);
		schizo_write(pbm->pbm_regs + database + (i * 8UL), 0);
	}

	/* Leave diag mode enabled for full-flushing done
	 * in pci_iommu.c
	 */
	pci_iommu_table_init(iommu, tsbsize * 8 * 1024, vdma[0], dma_mask);

	schizo_write(iommu->iommu_tsbbase, __pa(iommu->page_table));

	control = schizo_read(iommu->iommu_control);
	control &= ~(SCHIZO_IOMMU_CTRL_TSBSZ | SCHIZO_IOMMU_CTRL_TBWSZ);
	switch (tsbsize) {
	case 64:
		control |= SCHIZO_IOMMU_TSBSZ_64K;
		break;
	case 128:
		control |= SCHIZO_IOMMU_TSBSZ_128K;
		break;
	};

	control |= SCHIZO_IOMMU_CTRL_ENAB;
	schizo_write(iommu->iommu_control, control);
}

#define SCHIZO_PCI_IRQ_RETRY	(0x1a00UL)
#define  SCHIZO_IRQ_RETRY_INF	 0xffUL

#define SCHIZO_PCI_DIAG			(0x2020UL)
#define  SCHIZO_PCIDIAG_D_BADECC	(1UL << 10UL) /* Disable BAD ECC errors (Schizo) */
#define  SCHIZO_PCIDIAG_D_BYPASS	(1UL <<  9UL) /* Disable MMU bypass mode (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_D_TTO		(1UL <<  8UL) /* Disable TTO errors (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_D_RTRYARB	(1UL <<  7UL) /* Disable retry arbitration (Schizo) */
#define  SCHIZO_PCIDIAG_D_RETRY		(1UL <<  6UL) /* Disable retry limit (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_D_INTSYNC	(1UL <<  5UL) /* Disable interrupt/DMA synch (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_I_DMA_PARITY	(1UL <<  3UL) /* Invert DMA parity (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_I_PIOD_PARITY	(1UL <<  2UL) /* Invert PIO data parity (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_I_PIOA_PARITY	(1UL <<  1UL) /* Invert PIO address parity (Schizo/Tomatillo) */

#define TOMATILLO_PCI_IOC_CSR		(0x2248UL)
#define TOMATILLO_IOC_PART_WPENAB	0x0000000000080000UL
#define TOMATILLO_IOC_RDMULT_PENAB	0x0000000000040000UL
#define TOMATILLO_IOC_RDONE_PENAB	0x0000000000020000UL
#define TOMATILLO_IOC_RDLINE_PENAB	0x0000000000010000UL
#define TOMATILLO_IOC_RDMULT_PLEN	0x000000000000c000UL
#define TOMATILLO_IOC_RDMULT_PLEN_SHIFT	14UL
#define TOMATILLO_IOC_RDONE_PLEN	0x0000000000003000UL
#define TOMATILLO_IOC_RDONE_PLEN_SHIFT	12UL
#define TOMATILLO_IOC_RDLINE_PLEN	0x0000000000000c00UL
#define TOMATILLO_IOC_RDLINE_PLEN_SHIFT	10UL
#define TOMATILLO_IOC_PREF_OFF		0x00000000000003f8UL
#define TOMATILLO_IOC_PREF_OFF_SHIFT	3UL
#define TOMATILLO_IOC_RDMULT_CPENAB	0x0000000000000004UL
#define TOMATILLO_IOC_RDONE_CPENAB	0x0000000000000002UL
#define TOMATILLO_IOC_RDLINE_CPENAB	0x0000000000000001UL

#define TOMATILLO_PCI_IOC_TDIAG		(0x2250UL)
#define TOMATILLO_PCI_IOC_DDIAG		(0x2290UL)

static void schizo_pbm_hw_init(struct pci_pbm_info *pbm)
{
	u64 tmp;

	schizo_write(pbm->pbm_regs + SCHIZO_PCI_IRQ_RETRY, 5);

	tmp = schizo_read(pbm->pbm_regs + SCHIZO_PCI_CTRL);

	/* Enable arbiter for all PCI slots.  */
	tmp |= 0xff;

	if (pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO &&
	    pbm->chip_version >= 0x2)
		tmp |= 0x3UL << SCHIZO_PCICTRL_PTO_SHIFT;

	if (!prom_getbool(pbm->prom_node, "no-bus-parking"))
		tmp |= SCHIZO_PCICTRL_PARK;
	else
		tmp &= ~SCHIZO_PCICTRL_PARK;

	if (pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO &&
	    pbm->chip_version <= 0x1)
		tmp |= SCHIZO_PCICTRL_DTO_INT;
	else
		tmp &= ~SCHIZO_PCICTRL_DTO_INT;

	if (pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO)
		tmp |= (SCHIZO_PCICTRL_MRM_PREF |
			SCHIZO_PCICTRL_RDO_PREF |
			SCHIZO_PCICTRL_RDL_PREF);

	schizo_write(pbm->pbm_regs + SCHIZO_PCI_CTRL, tmp);

	tmp = schizo_read(pbm->pbm_regs + SCHIZO_PCI_DIAG);
	tmp &= ~(SCHIZO_PCIDIAG_D_RTRYARB |
		 SCHIZO_PCIDIAG_D_RETRY |
		 SCHIZO_PCIDIAG_D_INTSYNC);
	schizo_write(pbm->pbm_regs + SCHIZO_PCI_DIAG, tmp);

	if (pbm->chip_type == PBM_CHIP_TYPE_TOMATILLO) {
		/* Clear prefetch lengths to workaround a bug in
		 * Jalapeno...
		 */
		tmp = (TOMATILLO_IOC_PART_WPENAB |
		       (1 << TOMATILLO_IOC_PREF_OFF_SHIFT) |
		       TOMATILLO_IOC_RDMULT_CPENAB |
		       TOMATILLO_IOC_RDONE_CPENAB |
		       TOMATILLO_IOC_RDLINE_CPENAB);

		schizo_write(pbm->pbm_regs + TOMATILLO_PCI_IOC_CSR,
			     tmp);
	}
}

static void schizo_pbm_init(struct pci_controller_info *p,
			    int prom_node, u32 portid,
			    int chip_type)
{
	struct linux_prom64_registers pr_regs[4];
	unsigned int busrange[2];
	struct pci_pbm_info *pbm;
	const char *chipset_name;
	u32 ino_bitmap[2];
	int is_pbm_a;
	int err;

	switch (chip_type) {
	case PBM_CHIP_TYPE_TOMATILLO:
		chipset_name = "TOMATILLO";
		break;

	case PBM_CHIP_TYPE_SCHIZO_PLUS:
		chipset_name = "SCHIZO+";
		break;

	case PBM_CHIP_TYPE_SCHIZO:
	default:
		chipset_name = "SCHIZO";
		break;
	};

	/* For SCHIZO, three OBP regs:
	 * 1) PBM controller regs
	 * 2) Schizo front-end controller regs (same for both PBMs)
	 * 3) PBM PCI config space
	 *
	 * For TOMATILLO, four OBP regs:
	 * 1) PBM controller regs
	 * 2) Tomatillo front-end controller regs
	 * 3) PBM PCI config space
	 * 4) Ichip regs
	 */
	err = prom_getproperty(prom_node, "reg",
			       (char *)&pr_regs[0],
			       sizeof(pr_regs));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no reg property.\n",
			    chipset_name);
		prom_halt();
	}

	is_pbm_a = ((pr_regs[0].phys_addr & 0x00700000) == 0x00600000);

	if (is_pbm_a)
		pbm = &p->pbm_A;
	else
		pbm = &p->pbm_B;

	pbm->portid = portid;
	pbm->parent = p;
	pbm->prom_node = prom_node;
	pbm->pci_first_slot = 1;

	pbm->chip_type = chip_type;
	pbm->chip_version =
		prom_getintdefault(prom_node, "version#", 0);
	pbm->chip_revision =
		prom_getintdefault(prom_node, "module-revision#", 0);

	pbm->pbm_regs = pr_regs[0].phys_addr;
	pbm->controller_regs = pr_regs[1].phys_addr - 0x10000UL;

	if (chip_type == PBM_CHIP_TYPE_TOMATILLO)
		pbm->sync_reg = pr_regs[3].phys_addr + 0x1a18UL;

	sprintf(pbm->name,
		(chip_type == PBM_CHIP_TYPE_TOMATILLO ?
		 "TOMATILLO%d PBM%c" :
		 "SCHIZO%d PBM%c"),
		p->index,
		(pbm == &p->pbm_A ? 'A' : 'B'));

	printk("%s: ver[%x:%x], portid %x, "
	       "cregs[%lx] pregs[%lx]\n",
	       pbm->name,
	       pbm->chip_version, pbm->chip_revision,
	       pbm->portid,
	       pbm->controller_regs,
	       pbm->pbm_regs);

	schizo_pbm_hw_init(pbm);

	prom_getstring(prom_node, "name",
		       pbm->prom_name,
		       sizeof(pbm->prom_name));

	err = prom_getproperty(prom_node, "ranges",
			       (char *) pbm->pbm_ranges,
			       sizeof(pbm->pbm_ranges));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no ranges property.\n",
			    pbm->name);
		prom_halt();
	}

	pbm->num_pbm_ranges =
		(err / sizeof(struct linux_prom_pci_ranges));

	schizo_determine_mem_io_space(pbm);
	pbm_register_toplevel_resources(p, pbm);

	err = prom_getproperty(prom_node, "interrupt-map",
			       (char *)pbm->pbm_intmap,
			       sizeof(pbm->pbm_intmap));
	if (err != -1) {
		pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
		err = prom_getproperty(prom_node, "interrupt-map-mask",
				       (char *)&pbm->pbm_intmask,
				       sizeof(pbm->pbm_intmask));
		if (err == -1) {
			prom_printf("%s: Fatal error, no "
				    "interrupt-map-mask.\n", pbm->name);
			prom_halt();
		}
	} else {
		pbm->num_pbm_intmap = 0;
		memset(&pbm->pbm_intmask, 0, sizeof(pbm->pbm_intmask));
	}

	err = prom_getproperty(prom_node, "ino-bitmap",
			       (char *) &ino_bitmap[0],
			       sizeof(ino_bitmap));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no ino-bitmap.\n", pbm->name);
		prom_halt();
	}
	pbm->ino_bitmap = (((u64)ino_bitmap[1] << 32UL) |
			   ((u64)ino_bitmap[0] <<  0UL));

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no bus-range.\n", pbm->name);
		prom_halt();
	}
	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

	schizo_pbm_iommu_init(pbm);
	schizo_pbm_strbuf_init(pbm);
}

static inline int portid_compare(u32 x, u32 y, int chip_type)
{
	if (chip_type == PBM_CHIP_TYPE_TOMATILLO) {
		if (x == (y ^ 1))
			return 1;
		return 0;
	}
	return (x == y);
}

static void __schizo_init(int node, char *model_name, int chip_type)
{
	struct pci_controller_info *p;
	struct pci_iommu *iommu;
	int is_pbm_a;
	u32 portid;

	portid = prom_getintdefault(node, "portid", 0xff);

	for(p = pci_controller_root; p; p = p->next) {
		struct pci_pbm_info *pbm;

		if (p->pbm_A.prom_node && p->pbm_B.prom_node)
			continue;

		pbm = (p->pbm_A.prom_node ?
		       &p->pbm_A :
		       &p->pbm_B);

		if (portid_compare(pbm->portid, portid, chip_type)) {
			is_pbm_a = (p->pbm_A.prom_node == 0);
			schizo_pbm_init(p, node, portid, chip_type);
			return;
		}
	}

	p = kzalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}

	iommu = kzalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	p->pbm_A.iommu = iommu;

	iommu = kzalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	p->pbm_B.iommu = iommu;

	p->next = pci_controller_root;
	pci_controller_root = p;

	p->index = pci_num_controllers++;
	p->pbms_same_domain = 0;
	p->scan_bus = (chip_type == PBM_CHIP_TYPE_TOMATILLO ?
		       tomatillo_scan_bus :
		       schizo_scan_bus);
	p->irq_build = schizo_irq_build;
	p->base_address_update = schizo_base_address_update;
	p->resource_adjust = schizo_resource_adjust;
	p->pci_ops = &schizo_ops;

	/* Like PSYCHO we have a 2GB aligned area for memory space. */
	pci_memspace_mask = 0x7fffffffUL;

	schizo_pbm_init(p, node, portid, chip_type);
}

void schizo_init(int node, char *model_name)
{
	__schizo_init(node, model_name, PBM_CHIP_TYPE_SCHIZO);
}

void schizo_plus_init(int node, char *model_name)
{
	__schizo_init(node, model_name, PBM_CHIP_TYPE_SCHIZO_PLUS);
}

void tomatillo_init(int node, char *model_name)
{
	__schizo_init(node, model_name, PBM_CHIP_TYPE_TOMATILLO);
}
