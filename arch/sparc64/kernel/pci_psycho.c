/* $Id: pci_psycho.c,v 1.33 2002/02/01 00:58:33 davem Exp $
 * pci_psycho.c: PSYCHO/U2P specific PCI controller support.
 *
 * Copyright (C) 1997, 1998, 1999 David S. Miller (davem@caipfs.rutgers.edu)
 * Copyright (C) 1998, 1999 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek   (jakub@redhat.com)
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
#include <asm/starfire.h>

#include "pci_impl.h"
#include "iommu_common.h"

/* All PSYCHO registers are 64-bits.  The following accessor
 * routines are how they are accessed.  The REG parameter
 * is a physical address.
 */
#define psycho_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define psycho_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory")

/* Misc. PSYCHO PCI controller register offsets and definitions. */
#define PSYCHO_CONTROL		0x0010UL
#define  PSYCHO_CONTROL_IMPL	 0xf000000000000000UL /* Implementation of this PSYCHO*/
#define  PSYCHO_CONTROL_VER	 0x0f00000000000000UL /* Version of this PSYCHO       */
#define  PSYCHO_CONTROL_MID	 0x00f8000000000000UL /* UPA Module ID of PSYCHO      */
#define  PSYCHO_CONTROL_IGN	 0x0007c00000000000UL /* Interrupt Group Number       */
#define  PSYCHO_CONTROL_RESV     0x00003ffffffffff0UL /* Reserved                     */
#define  PSYCHO_CONTROL_APCKEN	 0x0000000000000008UL /* Address Parity Check Enable  */
#define  PSYCHO_CONTROL_APERR	 0x0000000000000004UL /* Incoming System Addr Parerr  */
#define  PSYCHO_CONTROL_IAP	 0x0000000000000002UL /* Invert UPA Parity            */
#define  PSYCHO_CONTROL_MODE	 0x0000000000000001UL /* PSYCHO clock mode            */
#define PSYCHO_PCIA_CTRL	0x2000UL
#define PSYCHO_PCIB_CTRL	0x4000UL
#define  PSYCHO_PCICTRL_RESV1	 0xfffffff000000000UL /* Reserved                     */
#define  PSYCHO_PCICTRL_SBH_ERR	 0x0000000800000000UL /* Streaming byte hole error    */
#define  PSYCHO_PCICTRL_SERR	 0x0000000400000000UL /* SERR signal asserted         */
#define  PSYCHO_PCICTRL_SPEED	 0x0000000200000000UL /* PCI speed (1 is U2P clock)   */
#define  PSYCHO_PCICTRL_RESV2	 0x00000001ffc00000UL /* Reserved                     */
#define  PSYCHO_PCICTRL_ARB_PARK 0x0000000000200000UL /* PCI arbitration parking      */
#define  PSYCHO_PCICTRL_RESV3	 0x00000000001ff800UL /* Reserved                     */
#define  PSYCHO_PCICTRL_SBH_INT	 0x0000000000000400UL /* Streaming byte hole int enab */
#define  PSYCHO_PCICTRL_WEN	 0x0000000000000200UL /* Power Mgmt Wake Enable       */
#define  PSYCHO_PCICTRL_EEN	 0x0000000000000100UL /* PCI Error Interrupt Enable   */
#define  PSYCHO_PCICTRL_RESV4	 0x00000000000000c0UL /* Reserved                     */
#define  PSYCHO_PCICTRL_AEN	 0x000000000000003fUL /* PCI DVMA Arbitration Enable  */

/* U2P Programmer's Manual, page 13-55, configuration space
 * address format:
 * 
 *  32             24 23 16 15    11 10       8 7   2  1 0
 * ---------------------------------------------------------
 * |0 0 0 0 0 0 0 0 1| bus | device | function | reg | 0 0 |
 * ---------------------------------------------------------
 */
#define PSYCHO_CONFIG_BASE(PBM)	\
	((PBM)->config_space | (1UL << 24))
#define PSYCHO_CONFIG_ENCODE(BUS, DEVFN, REG)	\
	(((unsigned long)(BUS)   << 16) |	\
	 ((unsigned long)(DEVFN) << 8)  |	\
	 ((unsigned long)(REG)))

static void *psycho_pci_config_mkaddr(struct pci_pbm_info *pbm,
				      unsigned char bus,
				      unsigned int devfn,
				      int where)
{
	if (!pbm)
		return NULL;
	return (void *)
		(PSYCHO_CONFIG_BASE(pbm) |
		 PSYCHO_CONFIG_ENCODE(bus, devfn, where));
}

static int psycho_out_of_range(struct pci_pbm_info *pbm,
			       unsigned char bus,
			       unsigned char devfn)
{
	return ((pbm->parent == 0) ||
		((pbm == &pbm->parent->pbm_B) &&
		 (bus == pbm->pci_first_busno) &&
		 PCI_SLOT(devfn) > 8) ||
		((pbm == &pbm->parent->pbm_A) &&
		 (bus == pbm->pci_first_busno) &&
		 PCI_SLOT(devfn) > 8));
}

/* PSYCHO PCI configuration space accessors. */

static int psycho_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
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

	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;
	switch (size) {
	case 1:
		pci_config_read8((u8 *)addr, &tmp8);
		*value = (u32) tmp8;
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_read_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read16((u16 *)addr, &tmp16);
		*value = (u32) tmp16;
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

static int psycho_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;

	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
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

static struct pci_ops psycho_ops = {
	.read =		psycho_read_pci_cfg,
	.write =	psycho_write_pci_cfg,
};

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

static unsigned long __onboard_imap_off[] = {
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
/*0x32*/	PSYCHO_IMAP_PMGMT
};
#define PSYCHO_ONBOARD_IRQ_BASE		0x20
#define PSYCHO_ONBOARD_IRQ_LAST		0x32
#define psycho_onboard_imap_offset(__ino) \
	__onboard_imap_off[(__ino) - PSYCHO_ONBOARD_IRQ_BASE]

#define PSYCHO_ICLR_A_SLOT0	0x1400UL
#define PSYCHO_ICLR_SCSI	0x1800UL

#define psycho_iclr_offset(ino)					      \
	((ino & 0x20) ? (PSYCHO_ICLR_SCSI + (((ino) & 0x1f) << 3)) :  \
			(PSYCHO_ICLR_A_SLOT0 + (((ino) & 0x1f)<<3)))

static unsigned int psycho_irq_build(struct pci_pbm_info *pbm,
				     struct pci_dev *pdev,
				     unsigned int ino)
{
	struct ino_bucket *bucket;
	unsigned long imap, iclr;
	unsigned long imap_off, iclr_off;
	int inofixup = 0;

	ino &= PCI_IRQ_INO;
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
	imap = pbm->controller_regs + imap_off;
	imap += 4;

	iclr_off = psycho_iclr_offset(ino);
	iclr = pbm->controller_regs + iclr_off;
	iclr += 4;

	if ((ino & 0x20) == 0)
		inofixup = ino & 0x03;

	bucket = __bucket(build_irq(inofixup, iclr, imap));
	bucket->flags |= IBF_PCI;

	return __irq(bucket);
}

/* PSYCHO error handling support. */
enum psycho_error_type {
	UE_ERR, CE_ERR, PCI_ERR
};

/* Helper function of IOMMU error checking, which checks out
 * the state of the streaming buffers.  The IOMMU lock is
 * held when this is called.
 *
 * For the PCI error case we know which PBM (and thus which
 * streaming buffer) caused the error, but for the uncorrectable
 * error case we do not.  So we always check both streaming caches.
 */
#define PSYCHO_STRBUF_CONTROL_A 0x2800UL
#define PSYCHO_STRBUF_CONTROL_B 0x4800UL
#define  PSYCHO_STRBUF_CTRL_LPTR    0x00000000000000f0UL /* LRU Lock Pointer */
#define  PSYCHO_STRBUF_CTRL_LENAB   0x0000000000000008UL /* LRU Lock Enable */
#define  PSYCHO_STRBUF_CTRL_RRDIS   0x0000000000000004UL /* Rerun Disable */
#define  PSYCHO_STRBUF_CTRL_DENAB   0x0000000000000002UL /* Diagnostic Mode Enable */
#define  PSYCHO_STRBUF_CTRL_ENAB    0x0000000000000001UL /* Streaming Buffer Enable */
#define PSYCHO_STRBUF_FLUSH_A   0x2808UL
#define PSYCHO_STRBUF_FLUSH_B   0x4808UL
#define PSYCHO_STRBUF_FSYNC_A   0x2810UL
#define PSYCHO_STRBUF_FSYNC_B   0x4810UL
#define PSYCHO_STC_DATA_A	0xb000UL
#define PSYCHO_STC_DATA_B	0xc000UL
#define PSYCHO_STC_ERR_A	0xb400UL
#define PSYCHO_STC_ERR_B	0xc400UL
#define  PSYCHO_STCERR_WRITE	 0x0000000000000002UL	/* Write Error */
#define  PSYCHO_STCERR_READ	 0x0000000000000001UL	/* Read Error */
#define PSYCHO_STC_TAG_A	0xb800UL
#define PSYCHO_STC_TAG_B	0xc800UL
#define  PSYCHO_STCTAG_PPN	 0x0fffffff00000000UL	/* Physical Page Number */
#define  PSYCHO_STCTAG_VPN	 0x00000000ffffe000UL	/* Virtual Page Number */
#define  PSYCHO_STCTAG_VALID	 0x0000000000000002UL	/* Valid */
#define  PSYCHO_STCTAG_WRITE	 0x0000000000000001UL	/* Writable */
#define PSYCHO_STC_LINE_A	0xb900UL
#define PSYCHO_STC_LINE_B	0xc900UL
#define  PSYCHO_STCLINE_LINDX	 0x0000000001e00000UL	/* LRU Index */
#define  PSYCHO_STCLINE_SPTR	 0x00000000001f8000UL	/* Dirty Data Start Pointer */
#define  PSYCHO_STCLINE_LADDR	 0x0000000000007f00UL	/* Line Address */
#define  PSYCHO_STCLINE_EPTR	 0x00000000000000fcUL	/* Dirty Data End Pointer */
#define  PSYCHO_STCLINE_VALID	 0x0000000000000002UL	/* Valid */
#define  PSYCHO_STCLINE_FOFN	 0x0000000000000001UL	/* Fetch Outstanding / Flush Necessary */

static DEFINE_SPINLOCK(stc_buf_lock);
static unsigned long stc_error_buf[128];
static unsigned long stc_tag_buf[16];
static unsigned long stc_line_buf[16];

static void __psycho_check_one_stc(struct pci_controller_info *p,
				   struct pci_pbm_info *pbm,
				   int is_pbm_a)
{
	struct pci_strbuf *strbuf = &pbm->stc;
	unsigned long regbase = p->pbm_A.controller_regs;
	unsigned long err_base, tag_base, line_base;
	u64 control;
	int i;

	if (is_pbm_a) {
		err_base = regbase + PSYCHO_STC_ERR_A;
		tag_base = regbase + PSYCHO_STC_TAG_A;
		line_base = regbase + PSYCHO_STC_LINE_A;
	} else {
		err_base = regbase + PSYCHO_STC_ERR_B;
		tag_base = regbase + PSYCHO_STC_TAG_B;
		line_base = regbase + PSYCHO_STC_LINE_B;
	}

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
	control = psycho_read(strbuf->strbuf_control);
	psycho_write(strbuf->strbuf_control,
		     (control | PSYCHO_STRBUF_CTRL_DENAB));
	for (i = 0; i < 128; i++) {
		unsigned long val;

		val = psycho_read(err_base + (i * 8UL));
		psycho_write(err_base + (i * 8UL), 0UL);
		stc_error_buf[i] = val;
	}
	for (i = 0; i < 16; i++) {
		stc_tag_buf[i] = psycho_read(tag_base + (i * 8UL));
		stc_line_buf[i] = psycho_read(line_base + (i * 8UL));
		psycho_write(tag_base + (i * 8UL), 0UL);
		psycho_write(line_base + (i * 8UL), 0UL);
	}

	/* OK, state is logged, exit diagnostic mode. */
	psycho_write(strbuf->strbuf_control, control);

	for (i = 0; i < 16; i++) {
		int j, saw_error, first, last;

		saw_error = 0;
		first = i * 8;
		last = first + 8;
		for (j = first; j < last; j++) {
			unsigned long errval = stc_error_buf[j];
			if (errval != 0) {
				saw_error++;
				printk("PSYCHO%d(PBM%c): STC_ERR(%d)[wr(%d)rd(%d)]\n",
				       p->index,
				       (is_pbm_a ? 'A' : 'B'),
				       j,
				       (errval & PSYCHO_STCERR_WRITE) ? 1 : 0,
				       (errval & PSYCHO_STCERR_READ) ? 1 : 0);
			}
		}
		if (saw_error != 0) {
			unsigned long tagval = stc_tag_buf[i];
			unsigned long lineval = stc_line_buf[i];
			printk("PSYCHO%d(PBM%c): STC_TAG(%d)[PA(%016lx)VA(%08lx)V(%d)W(%d)]\n",
			       p->index,
			       (is_pbm_a ? 'A' : 'B'),
			       i,
			       ((tagval & PSYCHO_STCTAG_PPN) >> 19UL),
			       (tagval & PSYCHO_STCTAG_VPN),
			       ((tagval & PSYCHO_STCTAG_VALID) ? 1 : 0),
			       ((tagval & PSYCHO_STCTAG_WRITE) ? 1 : 0));
			printk("PSYCHO%d(PBM%c): STC_LINE(%d)[LIDX(%lx)SP(%lx)LADDR(%lx)EP(%lx)"
			       "V(%d)FOFN(%d)]\n",
			       p->index,
			       (is_pbm_a ? 'A' : 'B'),
			       i,
			       ((lineval & PSYCHO_STCLINE_LINDX) >> 21UL),
			       ((lineval & PSYCHO_STCLINE_SPTR) >> 15UL),
			       ((lineval & PSYCHO_STCLINE_LADDR) >> 8UL),
			       ((lineval & PSYCHO_STCLINE_EPTR) >> 2UL),
			       ((lineval & PSYCHO_STCLINE_VALID) ? 1 : 0),
			       ((lineval & PSYCHO_STCLINE_FOFN) ? 1 : 0));
		}
	}

	spin_unlock(&stc_buf_lock);
}

static void __psycho_check_stc_error(struct pci_controller_info *p,
				     unsigned long afsr,
				     unsigned long afar,
				     enum psycho_error_type type)
{
	struct pci_pbm_info *pbm;

	pbm = &p->pbm_A;
	if (pbm->stc.strbuf_enabled)
		__psycho_check_one_stc(p, pbm, 1);

	pbm = &p->pbm_B;
	if (pbm->stc.strbuf_enabled)
		__psycho_check_one_stc(p, pbm, 0);
}

/* When an Uncorrectable Error or a PCI Error happens, we
 * interrogate the IOMMU state to see if it is the cause.
 */
#define PSYCHO_IOMMU_CONTROL	0x0200UL
#define  PSYCHO_IOMMU_CTRL_RESV     0xfffffffff9000000UL /* Reserved                      */
#define  PSYCHO_IOMMU_CTRL_XLTESTAT 0x0000000006000000UL /* Translation Error Status      */
#define  PSYCHO_IOMMU_CTRL_XLTEERR  0x0000000001000000UL /* Translation Error encountered */
#define  PSYCHO_IOMMU_CTRL_LCKEN    0x0000000000800000UL /* Enable translation locking    */
#define  PSYCHO_IOMMU_CTRL_LCKPTR   0x0000000000780000UL /* Translation lock pointer      */
#define  PSYCHO_IOMMU_CTRL_TSBSZ    0x0000000000070000UL /* TSB Size                      */
#define  PSYCHO_IOMMU_TSBSZ_1K      0x0000000000000000UL /* TSB Table 1024 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_2K      0x0000000000010000UL /* TSB Table 2048 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_4K      0x0000000000020000UL /* TSB Table 4096 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_8K      0x0000000000030000UL /* TSB Table 8192 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_16K     0x0000000000040000UL /* TSB Table 16k 8-byte entries  */
#define  PSYCHO_IOMMU_TSBSZ_32K     0x0000000000050000UL /* TSB Table 32k 8-byte entries  */
#define  PSYCHO_IOMMU_TSBSZ_64K     0x0000000000060000UL /* TSB Table 64k 8-byte entries  */
#define  PSYCHO_IOMMU_TSBSZ_128K    0x0000000000070000UL /* TSB Table 128k 8-byte entries */
#define  PSYCHO_IOMMU_CTRL_RESV2    0x000000000000fff8UL /* Reserved                      */
#define  PSYCHO_IOMMU_CTRL_TBWSZ    0x0000000000000004UL /* Assumed page size, 0=8k 1=64k */
#define  PSYCHO_IOMMU_CTRL_DENAB    0x0000000000000002UL /* Diagnostic mode enable        */
#define  PSYCHO_IOMMU_CTRL_ENAB     0x0000000000000001UL /* IOMMU Enable                  */
#define PSYCHO_IOMMU_TSBBASE	0x0208UL
#define PSYCHO_IOMMU_FLUSH	0x0210UL
#define PSYCHO_IOMMU_TAG	0xa580UL
#define  PSYCHO_IOMMU_TAG_ERRSTS (0x3UL << 23UL)
#define  PSYCHO_IOMMU_TAG_ERR	 (0x1UL << 22UL)
#define  PSYCHO_IOMMU_TAG_WRITE	 (0x1UL << 21UL)
#define  PSYCHO_IOMMU_TAG_STREAM (0x1UL << 20UL)
#define  PSYCHO_IOMMU_TAG_SIZE	 (0x1UL << 19UL)
#define  PSYCHO_IOMMU_TAG_VPAGE	 0x7ffffUL
#define PSYCHO_IOMMU_DATA	0xa600UL
#define  PSYCHO_IOMMU_DATA_VALID (1UL << 30UL)
#define  PSYCHO_IOMMU_DATA_CACHE (1UL << 28UL)
#define  PSYCHO_IOMMU_DATA_PPAGE 0xfffffffUL
static void psycho_check_iommu_error(struct pci_controller_info *p,
				     unsigned long afsr,
				     unsigned long afar,
				     enum psycho_error_type type)
{
	struct pci_iommu *iommu = p->pbm_A.iommu;
	unsigned long iommu_tag[16];
	unsigned long iommu_data[16];
	unsigned long flags;
	u64 control;
	int i;

	spin_lock_irqsave(&iommu->lock, flags);
	control = psycho_read(iommu->iommu_control);
	if (control & PSYCHO_IOMMU_CTRL_XLTEERR) {
		char *type_string;

		/* Clear the error encountered bit. */
		control &= ~PSYCHO_IOMMU_CTRL_XLTEERR;
		psycho_write(iommu->iommu_control, control);

		switch((control & PSYCHO_IOMMU_CTRL_XLTESTAT) >> 25UL) {
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
		printk("PSYCHO%d: IOMMU Error, type[%s]\n",
		       p->index, type_string);

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
		psycho_write(iommu->iommu_control,
			     control | PSYCHO_IOMMU_CTRL_DENAB);
		for (i = 0; i < 16; i++) {
			unsigned long base = p->pbm_A.controller_regs;

			iommu_tag[i] =
				psycho_read(base + PSYCHO_IOMMU_TAG + (i * 8UL));
			iommu_data[i] =
				psycho_read(base + PSYCHO_IOMMU_DATA + (i * 8UL));

			/* Now clear out the entry. */
			psycho_write(base + PSYCHO_IOMMU_TAG + (i * 8UL), 0);
			psycho_write(base + PSYCHO_IOMMU_DATA + (i * 8UL), 0);
		}

		/* Leave diagnostic mode. */
		psycho_write(iommu->iommu_control, control);

		for (i = 0; i < 16; i++) {
			unsigned long tag, data;

			tag = iommu_tag[i];
			if (!(tag & PSYCHO_IOMMU_TAG_ERR))
				continue;

			data = iommu_data[i];
			switch((tag & PSYCHO_IOMMU_TAG_ERRSTS) >> 23UL) {
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
			printk("PSYCHO%d: IOMMU TAG(%d)[error(%s) wr(%d) str(%d) sz(%dK) vpg(%08lx)]\n",
			       p->index, i, type_string,
			       ((tag & PSYCHO_IOMMU_TAG_WRITE) ? 1 : 0),
			       ((tag & PSYCHO_IOMMU_TAG_STREAM) ? 1 : 0),
			       ((tag & PSYCHO_IOMMU_TAG_SIZE) ? 64 : 8),
			       (tag & PSYCHO_IOMMU_TAG_VPAGE) << IOMMU_PAGE_SHIFT);
			printk("PSYCHO%d: IOMMU DATA(%d)[valid(%d) cache(%d) ppg(%016lx)]\n",
			       p->index, i,
			       ((data & PSYCHO_IOMMU_DATA_VALID) ? 1 : 0),
			       ((data & PSYCHO_IOMMU_DATA_CACHE) ? 1 : 0),
			       (data & PSYCHO_IOMMU_DATA_PPAGE) << IOMMU_PAGE_SHIFT);
		}
	}
	__psycho_check_stc_error(p, afsr, afar, type);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

/* Uncorrectable Errors.  Cause of the error and the address are
 * recorded in the UE_AFSR and UE_AFAR of PSYCHO.  They are errors
 * relating to UPA interface transactions.
 */
#define PSYCHO_UE_AFSR	0x0030UL
#define  PSYCHO_UEAFSR_PPIO	0x8000000000000000UL /* Primary PIO is cause         */
#define  PSYCHO_UEAFSR_PDRD	0x4000000000000000UL /* Primary DVMA read is cause   */
#define  PSYCHO_UEAFSR_PDWR	0x2000000000000000UL /* Primary DVMA write is cause  */
#define  PSYCHO_UEAFSR_SPIO	0x1000000000000000UL /* Secondary PIO is cause       */
#define  PSYCHO_UEAFSR_SDRD	0x0800000000000000UL /* Secondary DVMA read is cause */
#define  PSYCHO_UEAFSR_SDWR	0x0400000000000000UL /* Secondary DVMA write is cause*/
#define  PSYCHO_UEAFSR_RESV1	0x03ff000000000000UL /* Reserved                     */
#define  PSYCHO_UEAFSR_BMSK	0x0000ffff00000000UL /* Bytemask of failed transfer  */
#define  PSYCHO_UEAFSR_DOFF	0x00000000e0000000UL /* Doubleword Offset            */
#define  PSYCHO_UEAFSR_MID	0x000000001f000000UL /* UPA MID causing the fault    */
#define  PSYCHO_UEAFSR_BLK	0x0000000000800000UL /* Trans was block operation    */
#define  PSYCHO_UEAFSR_RESV2	0x00000000007fffffUL /* Reserved                     */
#define PSYCHO_UE_AFAR	0x0038UL

static irqreturn_t psycho_ue_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->pbm_A.controller_regs + PSYCHO_UE_AFSR;
	unsigned long afar_reg = p->pbm_A.controller_regs + PSYCHO_UE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch uncorrectable error status. */
	afar = psycho_read(afar_reg);
	afsr = psycho_read(afsr_reg);

	/* Clear the primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_UEAFSR_PPIO | PSYCHO_UEAFSR_PDRD | PSYCHO_UEAFSR_PDWR |
		 PSYCHO_UEAFSR_SPIO | PSYCHO_UEAFSR_SDRD | PSYCHO_UEAFSR_SDWR);
	if (!error_bits)
		return IRQ_NONE;
	psycho_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PSYCHO%d: Uncorrectable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & PSYCHO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & PSYCHO_UEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & PSYCHO_UEAFSR_PDWR) ?
		   "DMA Write" : "???")))));
	printk("PSYCHO%d: bytemask[%04lx] dword_offset[%lx] UPA_MID[%02lx] was_block(%d)\n",
	       p->index,
	       (afsr & PSYCHO_UEAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_UEAFSR_DOFF) >> 29UL,
	       (afsr & PSYCHO_UEAFSR_MID) >> 24UL,
	       ((afsr & PSYCHO_UEAFSR_BLK) ? 1 : 0));
	printk("PSYCHO%d: UE AFAR [%016lx]\n", p->index, afar);
	printk("PSYCHO%d: UE Secondary errors [", p->index);
	reported = 0;
	if (afsr & PSYCHO_UEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & PSYCHO_UEAFSR_SDRD) {
		reported++;
		printk("(DMA Read)");
	}
	if (afsr & PSYCHO_UEAFSR_SDWR) {
		reported++;
		printk("(DMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* Interrogate IOMMU for error status. */
	psycho_check_iommu_error(p, afsr, afar, UE_ERR);

	return IRQ_HANDLED;
}

/* Correctable Errors. */
#define PSYCHO_CE_AFSR	0x0040UL
#define  PSYCHO_CEAFSR_PPIO	0x8000000000000000UL /* Primary PIO is cause         */
#define  PSYCHO_CEAFSR_PDRD	0x4000000000000000UL /* Primary DVMA read is cause   */
#define  PSYCHO_CEAFSR_PDWR	0x2000000000000000UL /* Primary DVMA write is cause  */
#define  PSYCHO_CEAFSR_SPIO	0x1000000000000000UL /* Secondary PIO is cause       */
#define  PSYCHO_CEAFSR_SDRD	0x0800000000000000UL /* Secondary DVMA read is cause */
#define  PSYCHO_CEAFSR_SDWR	0x0400000000000000UL /* Secondary DVMA write is cause*/
#define  PSYCHO_CEAFSR_RESV1	0x0300000000000000UL /* Reserved                     */
#define  PSYCHO_CEAFSR_ESYND	0x00ff000000000000UL /* Syndrome Bits                */
#define  PSYCHO_CEAFSR_BMSK	0x0000ffff00000000UL /* Bytemask of failed transfer  */
#define  PSYCHO_CEAFSR_DOFF	0x00000000e0000000UL /* Double Offset                */
#define  PSYCHO_CEAFSR_MID	0x000000001f000000UL /* UPA MID causing the fault    */
#define  PSYCHO_CEAFSR_BLK	0x0000000000800000UL /* Trans was block operation    */
#define  PSYCHO_CEAFSR_RESV2	0x00000000007fffffUL /* Reserved                     */
#define PSYCHO_CE_AFAR	0x0040UL

static irqreturn_t psycho_ce_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->pbm_A.controller_regs + PSYCHO_CE_AFSR;
	unsigned long afar_reg = p->pbm_A.controller_regs + PSYCHO_CE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch error status. */
	afar = psycho_read(afar_reg);
	afsr = psycho_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_CEAFSR_PPIO | PSYCHO_CEAFSR_PDRD | PSYCHO_CEAFSR_PDWR |
		 PSYCHO_CEAFSR_SPIO | PSYCHO_CEAFSR_SDRD | PSYCHO_CEAFSR_SDWR);
	if (!error_bits)
		return IRQ_NONE;
	psycho_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PSYCHO%d: Correctable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & PSYCHO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & PSYCHO_CEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & PSYCHO_CEAFSR_PDWR) ?
		   "DMA Write" : "???")))));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("PSYCHO%d: syndrome[%02lx] bytemask[%04lx] dword_offset[%lx] "
	       "UPA_MID[%02lx] was_block(%d)\n",
	       p->index,
	       (afsr & PSYCHO_CEAFSR_ESYND) >> 48UL,
	       (afsr & PSYCHO_CEAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_CEAFSR_DOFF) >> 29UL,
	       (afsr & PSYCHO_CEAFSR_MID) >> 24UL,
	       ((afsr & PSYCHO_CEAFSR_BLK) ? 1 : 0));
	printk("PSYCHO%d: CE AFAR [%016lx]\n", p->index, afar);
	printk("PSYCHO%d: CE Secondary errors [", p->index);
	reported = 0;
	if (afsr & PSYCHO_CEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & PSYCHO_CEAFSR_SDRD) {
		reported++;
		printk("(DMA Read)");
	}
	if (afsr & PSYCHO_CEAFSR_SDWR) {
		reported++;
		printk("(DMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	return IRQ_HANDLED;
}

/* PCI Errors.  They are signalled by the PCI bus module since they
 * are associated with a specific bus segment.
 */
#define PSYCHO_PCI_AFSR_A	0x2010UL
#define PSYCHO_PCI_AFSR_B	0x4010UL
#define  PSYCHO_PCIAFSR_PMA	0x8000000000000000UL /* Primary Master Abort Error   */
#define  PSYCHO_PCIAFSR_PTA	0x4000000000000000UL /* Primary Target Abort Error   */
#define  PSYCHO_PCIAFSR_PRTRY	0x2000000000000000UL /* Primary Excessive Retries    */
#define  PSYCHO_PCIAFSR_PPERR	0x1000000000000000UL /* Primary Parity Error         */
#define  PSYCHO_PCIAFSR_SMA	0x0800000000000000UL /* Secondary Master Abort Error */
#define  PSYCHO_PCIAFSR_STA	0x0400000000000000UL /* Secondary Target Abort Error */
#define  PSYCHO_PCIAFSR_SRTRY	0x0200000000000000UL /* Secondary Excessive Retries  */
#define  PSYCHO_PCIAFSR_SPERR	0x0100000000000000UL /* Secondary Parity Error       */
#define  PSYCHO_PCIAFSR_RESV1	0x00ff000000000000UL /* Reserved                     */
#define  PSYCHO_PCIAFSR_BMSK	0x0000ffff00000000UL /* Bytemask of failed transfer  */
#define  PSYCHO_PCIAFSR_BLK	0x0000000080000000UL /* Trans was block operation    */
#define  PSYCHO_PCIAFSR_RESV2	0x0000000040000000UL /* Reserved                     */
#define  PSYCHO_PCIAFSR_MID	0x000000003e000000UL /* MID causing the error        */
#define  PSYCHO_PCIAFSR_RESV3	0x0000000001ffffffUL /* Reserved                     */
#define PSYCHO_PCI_AFAR_A	0x2018UL
#define PSYCHO_PCI_AFAR_B	0x4018UL

static irqreturn_t psycho_pcierr_intr_other(struct pci_pbm_info *pbm, int is_pbm_a)
{
	unsigned long csr_reg, csr, csr_error_bits;
	irqreturn_t ret = IRQ_NONE;
	u16 stat;

	if (is_pbm_a) {
		csr_reg = pbm->controller_regs + PSYCHO_PCIA_CTRL;
	} else {
		csr_reg = pbm->controller_regs + PSYCHO_PCIB_CTRL;
	}
	csr = psycho_read(csr_reg);
	csr_error_bits =
		csr & (PSYCHO_PCICTRL_SBH_ERR | PSYCHO_PCICTRL_SERR);
	if (csr_error_bits) {
		/* Clear the errors.  */
		psycho_write(csr_reg, csr);

		/* Log 'em.  */
		if (csr_error_bits & PSYCHO_PCICTRL_SBH_ERR)
			printk("%s: PCI streaming byte hole error asserted.\n",
			       pbm->name);
		if (csr_error_bits & PSYCHO_PCICTRL_SERR)
			printk("%s: PCI SERR signal asserted.\n", pbm->name);
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

static irqreturn_t psycho_pcierr_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_pbm_info *pbm = dev_id;
	struct pci_controller_info *p = pbm->parent;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int is_pbm_a, reported;

	is_pbm_a = (pbm == &pbm->parent->pbm_A);
	if (is_pbm_a) {
		afsr_reg = p->pbm_A.controller_regs + PSYCHO_PCI_AFSR_A;
		afar_reg = p->pbm_A.controller_regs + PSYCHO_PCI_AFAR_A;
	} else {
		afsr_reg = p->pbm_A.controller_regs + PSYCHO_PCI_AFSR_B;
		afar_reg = p->pbm_A.controller_regs + PSYCHO_PCI_AFAR_B;
	}

	/* Latch error status. */
	afar = psycho_read(afar_reg);
	afsr = psycho_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_PCIAFSR_PMA | PSYCHO_PCIAFSR_PTA |
		 PSYCHO_PCIAFSR_PRTRY | PSYCHO_PCIAFSR_PPERR |
		 PSYCHO_PCIAFSR_SMA | PSYCHO_PCIAFSR_STA |
		 PSYCHO_PCIAFSR_SRTRY | PSYCHO_PCIAFSR_SPERR);
	if (!error_bits)
		return psycho_pcierr_intr_other(pbm, is_pbm_a);
	psycho_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PSYCHO%d(PBM%c): PCI Error, primary error type[%s]\n",
	       p->index, (is_pbm_a ? 'A' : 'B'),
	       (((error_bits & PSYCHO_PCIAFSR_PMA) ?
		 "Master Abort" :
		 ((error_bits & PSYCHO_PCIAFSR_PTA) ?
		  "Target Abort" :
		  ((error_bits & PSYCHO_PCIAFSR_PRTRY) ?
		   "Excessive Retries" :
		   ((error_bits & PSYCHO_PCIAFSR_PPERR) ?
		    "Parity Error" : "???"))))));
	printk("PSYCHO%d(PBM%c): bytemask[%04lx] UPA_MID[%02lx] was_block(%d)\n",
	       p->index, (is_pbm_a ? 'A' : 'B'),
	       (afsr & PSYCHO_PCIAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_PCIAFSR_MID) >> 25UL,
	       (afsr & PSYCHO_PCIAFSR_BLK) ? 1 : 0);
	printk("PSYCHO%d(PBM%c): PCI AFAR [%016lx]\n",
	       p->index, (is_pbm_a ? 'A' : 'B'), afar);
	printk("PSYCHO%d(PBM%c): PCI Secondary errors [",
	       p->index, (is_pbm_a ? 'A' : 'B'));
	reported = 0;
	if (afsr & PSYCHO_PCIAFSR_SMA) {
		reported++;
		printk("(Master Abort)");
	}
	if (afsr & PSYCHO_PCIAFSR_STA) {
		reported++;
		printk("(Target Abort)");
	}
	if (afsr & PSYCHO_PCIAFSR_SRTRY) {
		reported++;
		printk("(Excessive Retries)");
	}
	if (afsr & PSYCHO_PCIAFSR_SPERR) {
		reported++;
		printk("(Parity Error)");
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
	if (error_bits & (PSYCHO_PCIAFSR_PTA | PSYCHO_PCIAFSR_STA)) {
		psycho_check_iommu_error(p, afsr, afar, PCI_ERR);
		pci_scan_for_target_abort(p, pbm, pbm->pci_bus);
	}
	if (error_bits & (PSYCHO_PCIAFSR_PMA | PSYCHO_PCIAFSR_SMA))
		pci_scan_for_master_abort(p, pbm, pbm->pci_bus);

	/* For excessive retries, PSYCHO/PBM will abort the device
	 * and there is no way to specifically check for excessive
	 * retries in the config space status registers.  So what
	 * we hope is that we'll catch it via the master/target
	 * abort events.
	 */

	if (error_bits & (PSYCHO_PCIAFSR_PPERR | PSYCHO_PCIAFSR_SPERR))
		pci_scan_for_parity_error(p, pbm, pbm->pci_bus);

	return IRQ_HANDLED;
}

/* XXX What about PowerFail/PowerManagement??? -DaveM */
#define PSYCHO_ECC_CTRL		0x0020
#define  PSYCHO_ECCCTRL_EE	 0x8000000000000000UL /* Enable ECC Checking */
#define  PSYCHO_ECCCTRL_UE	 0x4000000000000000UL /* Enable UE Interrupts */
#define  PSYCHO_ECCCTRL_CE	 0x2000000000000000UL /* Enable CE INterrupts */
#define PSYCHO_UE_INO		0x2e
#define PSYCHO_CE_INO		0x2f
#define PSYCHO_PCIERR_A_INO	0x30
#define PSYCHO_PCIERR_B_INO	0x31
static void psycho_register_error_handlers(struct pci_controller_info *p)
{
	struct pci_pbm_info *pbm = &p->pbm_A; /* arbitrary */
	unsigned long base = p->pbm_A.controller_regs;
	unsigned int irq, portid = pbm->portid;
	u64 tmp;

	/* Build IRQs and register handlers. */
	irq = psycho_irq_build(pbm, NULL, (portid << 6) | PSYCHO_UE_INO);
	if (request_irq(irq, psycho_ue_intr,
			SA_SHIRQ, "PSYCHO UE", p) < 0) {
		prom_printf("PSYCHO%d: Cannot register UE interrupt.\n",
			    p->index);
		prom_halt();
	}

	irq = psycho_irq_build(pbm, NULL, (portid << 6) | PSYCHO_CE_INO);
	if (request_irq(irq, psycho_ce_intr,
			SA_SHIRQ, "PSYCHO CE", p) < 0) {
		prom_printf("PSYCHO%d: Cannot register CE interrupt.\n",
			    p->index);
		prom_halt();
	}

	pbm = &p->pbm_A;
	irq = psycho_irq_build(pbm, NULL, (portid << 6) | PSYCHO_PCIERR_A_INO);
	if (request_irq(irq, psycho_pcierr_intr,
			SA_SHIRQ, "PSYCHO PCIERR", &p->pbm_A) < 0) {
		prom_printf("PSYCHO%d(PBMA): Cannot register PciERR interrupt.\n",
			    p->index);
		prom_halt();
	}

	pbm = &p->pbm_B;
	irq = psycho_irq_build(pbm, NULL, (portid << 6) | PSYCHO_PCIERR_B_INO);
	if (request_irq(irq, psycho_pcierr_intr,
			SA_SHIRQ, "PSYCHO PCIERR", &p->pbm_B) < 0) {
		prom_printf("PSYCHO%d(PBMB): Cannot register PciERR interrupt.\n",
			    p->index);
		prom_halt();
	}

	/* Enable UE and CE interrupts for controller. */
	psycho_write(base + PSYCHO_ECC_CTRL,
		     (PSYCHO_ECCCTRL_EE |
		      PSYCHO_ECCCTRL_UE |
		      PSYCHO_ECCCTRL_CE));

	/* Enable PCI Error interrupts and clear error
	 * bits for each PBM.
	 */
	tmp = psycho_read(base + PSYCHO_PCIA_CTRL);
	tmp |= (PSYCHO_PCICTRL_SERR |
		PSYCHO_PCICTRL_SBH_ERR |
		PSYCHO_PCICTRL_EEN);
	tmp &= ~(PSYCHO_PCICTRL_SBH_INT);
	psycho_write(base + PSYCHO_PCIA_CTRL, tmp);
		     
	tmp = psycho_read(base + PSYCHO_PCIB_CTRL);
	tmp |= (PSYCHO_PCICTRL_SERR |
		PSYCHO_PCICTRL_SBH_ERR |
		PSYCHO_PCICTRL_EEN);
	tmp &= ~(PSYCHO_PCICTRL_SBH_INT);
	psycho_write(base + PSYCHO_PCIB_CTRL, tmp);
}

/* PSYCHO boot time probing and initialization. */
static void psycho_resource_adjust(struct pci_dev *pdev,
				   struct resource *res,
				   struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

static void psycho_base_address_update(struct pci_dev *pdev, int resource)
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

static void pbm_config_busmastering(struct pci_pbm_info *pbm)
{
	u8 *addr;

	/* Set cache-line size to 64 bytes, this is actually
	 * a nop but I do it for completeness.
	 */
	addr = psycho_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PCI_CACHE_LINE_SIZE);
	pci_config_write8(addr, 64 / sizeof(u32));

	/* Set PBM latency timer to 64 PCI clocks. */
	addr = psycho_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PCI_LATENCY_TIMER);
	pci_config_write8(addr, 64);
}

static void pbm_scan_bus(struct pci_controller_info *p,
			 struct pci_pbm_info *pbm)
{
	struct pcidev_cookie *cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);

	if (!cookie) {
		prom_printf("PSYCHO: Critical allocation failure.\n");
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

static void psycho_scan_bus(struct pci_controller_info *p)
{
	pbm_config_busmastering(&p->pbm_B);
	p->pbm_B.is_66mhz_capable = 0;
	pbm_config_busmastering(&p->pbm_A);
	p->pbm_A.is_66mhz_capable = 1;
	pbm_scan_bus(p, &p->pbm_B);
	pbm_scan_bus(p, &p->pbm_A);

	/* After the PCI bus scan is complete, we can register
	 * the error interrupt handlers.
	 */
	psycho_register_error_handlers(p);
}

static void psycho_iommu_init(struct pci_controller_info *p)
{
	struct pci_iommu *iommu = p->pbm_A.iommu;
	unsigned long i;
	u64 control;

	/* Register addresses. */
	iommu->iommu_control  = p->pbm_A.controller_regs + PSYCHO_IOMMU_CONTROL;
	iommu->iommu_tsbbase  = p->pbm_A.controller_regs + PSYCHO_IOMMU_TSBBASE;
	iommu->iommu_flush    = p->pbm_A.controller_regs + PSYCHO_IOMMU_FLUSH;
	/* PSYCHO's IOMMU lacks ctx flushing. */
	iommu->iommu_ctxflush = 0;

	/* We use the main control register of PSYCHO as the write
	 * completion register.
	 */
	iommu->write_complete_reg = p->pbm_A.controller_regs + PSYCHO_CONTROL;

	/*
	 * Invalidate TLB Entries.
	 */
	control = psycho_read(p->pbm_A.controller_regs + PSYCHO_IOMMU_CONTROL);
	control |= PSYCHO_IOMMU_CTRL_DENAB;
	psycho_write(p->pbm_A.controller_regs + PSYCHO_IOMMU_CONTROL, control);
	for(i = 0; i < 16; i++) {
		psycho_write(p->pbm_A.controller_regs + PSYCHO_IOMMU_TAG + (i * 8UL), 0);
		psycho_write(p->pbm_A.controller_regs + PSYCHO_IOMMU_DATA + (i * 8UL), 0);
	}

	/* Leave diag mode enabled for full-flushing done
	 * in pci_iommu.c
	 */
	pci_iommu_table_init(iommu, IO_TSB_SIZE, 0xc0000000, 0xffffffff);

	psycho_write(p->pbm_A.controller_regs + PSYCHO_IOMMU_TSBBASE,
		     __pa(iommu->page_table));

	control = psycho_read(p->pbm_A.controller_regs + PSYCHO_IOMMU_CONTROL);
	control &= ~(PSYCHO_IOMMU_CTRL_TSBSZ | PSYCHO_IOMMU_CTRL_TBWSZ);
	control |= (PSYCHO_IOMMU_TSBSZ_128K | PSYCHO_IOMMU_CTRL_ENAB);
	psycho_write(p->pbm_A.controller_regs + PSYCHO_IOMMU_CONTROL, control);

	/* If necessary, hook us up for starfire IRQ translations. */
	if (this_is_starfire)
		p->starfire_cookie = starfire_hookup(p->pbm_A.portid);
	else
		p->starfire_cookie = NULL;
}

#define PSYCHO_IRQ_RETRY	0x1a00UL
#define PSYCHO_PCIA_DIAG	0x2020UL
#define PSYCHO_PCIB_DIAG	0x4020UL
#define  PSYCHO_PCIDIAG_RESV	 0xffffffffffffff80UL /* Reserved                     */
#define  PSYCHO_PCIDIAG_DRETRY	 0x0000000000000040UL /* Disable retry limit          */
#define  PSYCHO_PCIDIAG_DISYNC	 0x0000000000000020UL /* Disable DMA wr / irq sync    */
#define  PSYCHO_PCIDIAG_DDWSYNC	 0x0000000000000010UL /* Disable DMA wr / PIO rd sync */
#define  PSYCHO_PCIDIAG_IDDPAR	 0x0000000000000008UL /* Invert DMA data parity       */
#define  PSYCHO_PCIDIAG_IPDPAR	 0x0000000000000004UL /* Invert PIO data parity       */
#define  PSYCHO_PCIDIAG_IPAPAR	 0x0000000000000002UL /* Invert PIO address parity    */
#define  PSYCHO_PCIDIAG_LPBACK	 0x0000000000000001UL /* Enable loopback mode         */

static void psycho_controller_hwinit(struct pci_controller_info *p)
{
	u64 tmp;

	psycho_write(p->pbm_A.controller_regs + PSYCHO_IRQ_RETRY, 5);

	/* Enable arbiter for all PCI slots. */
	tmp = psycho_read(p->pbm_A.controller_regs + PSYCHO_PCIA_CTRL);
	tmp |= PSYCHO_PCICTRL_AEN;
	psycho_write(p->pbm_A.controller_regs + PSYCHO_PCIA_CTRL, tmp);

	tmp = psycho_read(p->pbm_A.controller_regs + PSYCHO_PCIB_CTRL);
	tmp |= PSYCHO_PCICTRL_AEN;
	psycho_write(p->pbm_A.controller_regs + PSYCHO_PCIB_CTRL, tmp);

	/* Disable DMA write / PIO read synchronization on
	 * both PCI bus segments.
	 * [ U2P Erratum 1243770, STP2223BGA data sheet ]
	 */
	tmp = psycho_read(p->pbm_A.controller_regs + PSYCHO_PCIA_DIAG);
	tmp |= PSYCHO_PCIDIAG_DDWSYNC;
	psycho_write(p->pbm_A.controller_regs + PSYCHO_PCIA_DIAG, tmp);

	tmp = psycho_read(p->pbm_A.controller_regs + PSYCHO_PCIB_DIAG);
	tmp |= PSYCHO_PCIDIAG_DDWSYNC;
	psycho_write(p->pbm_A.controller_regs + PSYCHO_PCIB_DIAG, tmp);
}

static void pbm_register_toplevel_resources(struct pci_controller_info *p,
					    struct pci_pbm_info *pbm)
{
	char *name = pbm->name;

	sprintf(name, "PSYCHO%d PBM%c",
		p->index,
		(pbm == &p->pbm_A ? 'A' : 'B'));
	pbm->io_space.name = pbm->mem_space.name = name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);
	pci_register_legacy_regions(&pbm->io_space,
				    &pbm->mem_space);
}

static void psycho_pbm_strbuf_init(struct pci_controller_info *p,
				   struct pci_pbm_info *pbm,
				   int is_pbm_a)
{
	unsigned long base = pbm->controller_regs;
	u64 control;

	if (is_pbm_a) {
		pbm->stc.strbuf_control  = base + PSYCHO_STRBUF_CONTROL_A;
		pbm->stc.strbuf_pflush   = base + PSYCHO_STRBUF_FLUSH_A;
		pbm->stc.strbuf_fsync    = base + PSYCHO_STRBUF_FSYNC_A;
	} else {
		pbm->stc.strbuf_control  = base + PSYCHO_STRBUF_CONTROL_B;
		pbm->stc.strbuf_pflush   = base + PSYCHO_STRBUF_FLUSH_B;
		pbm->stc.strbuf_fsync    = base + PSYCHO_STRBUF_FSYNC_B;
	}
	/* PSYCHO's streaming buffer lacks ctx flushing. */
	pbm->stc.strbuf_ctxflush      = 0;
	pbm->stc.strbuf_ctxmatch_base = 0;

	pbm->stc.strbuf_flushflag = (volatile unsigned long *)
		((((unsigned long)&pbm->stc.__flushflag_buf[0])
		  + 63UL)
		 & ~63UL);
	pbm->stc.strbuf_flushflag_pa = (unsigned long)
		__pa(pbm->stc.strbuf_flushflag);

	/* Enable the streaming buffer.  We have to be careful
	 * just in case OBP left it with LRU locking enabled.
	 *
	 * It is possible to control if PBM will be rerun on
	 * line misses.  Currently I just retain whatever setting
	 * OBP left us with.  All checks so far show it having
	 * a value of zero.
	 */
#undef PSYCHO_STRBUF_RERUN_ENABLE
#undef PSYCHO_STRBUF_RERUN_DISABLE
	control = psycho_read(pbm->stc.strbuf_control);
	control |= PSYCHO_STRBUF_CTRL_ENAB;
	control &= ~(PSYCHO_STRBUF_CTRL_LENAB | PSYCHO_STRBUF_CTRL_LPTR);
#ifdef PSYCHO_STRBUF_RERUN_ENABLE
	control &= ~(PSYCHO_STRBUF_CTRL_RRDIS);
#else
#ifdef PSYCHO_STRBUF_RERUN_DISABLE
	control |= PSYCHO_STRBUF_CTRL_RRDIS;
#endif
#endif
	psycho_write(pbm->stc.strbuf_control, control);

	pbm->stc.strbuf_enabled = 1;
}

#define PSYCHO_IOSPACE_A	0x002000000UL
#define PSYCHO_IOSPACE_B	0x002010000UL
#define PSYCHO_IOSPACE_SIZE	0x00000ffffUL
#define PSYCHO_MEMSPACE_A	0x100000000UL
#define PSYCHO_MEMSPACE_B	0x180000000UL
#define PSYCHO_MEMSPACE_SIZE	0x07fffffffUL

static void psycho_pbm_init(struct pci_controller_info *p,
			    int prom_node, int is_pbm_a)
{
	unsigned int busrange[2];
	struct pci_pbm_info *pbm;
	int err;

	if (is_pbm_a) {
		pbm = &p->pbm_A;
		pbm->pci_first_slot = 1;
		pbm->io_space.start = pbm->controller_regs + PSYCHO_IOSPACE_A;
		pbm->mem_space.start = pbm->controller_regs + PSYCHO_MEMSPACE_A;
	} else {
		pbm = &p->pbm_B;
		pbm->pci_first_slot = 2;
		pbm->io_space.start = pbm->controller_regs + PSYCHO_IOSPACE_B;
		pbm->mem_space.start = pbm->controller_regs + PSYCHO_MEMSPACE_B;
	}

	pbm->chip_type = PBM_CHIP_TYPE_PSYCHO;
	pbm->chip_version =
		prom_getintdefault(prom_node, "version#", 0);
	pbm->chip_revision =
		prom_getintdefault(prom_node, "module-revision#", 0);

	pbm->io_space.end = pbm->io_space.start + PSYCHO_IOSPACE_SIZE;
	pbm->io_space.flags = IORESOURCE_IO;
	pbm->mem_space.end = pbm->mem_space.start + PSYCHO_MEMSPACE_SIZE;
	pbm->mem_space.flags = IORESOURCE_MEM;
	pbm_register_toplevel_resources(p, pbm);

	pbm->parent = p;
	pbm->prom_node = prom_node;
	prom_getstring(prom_node, "name",
		       pbm->prom_name,
		       sizeof(pbm->prom_name));

	err = prom_getproperty(prom_node, "ranges",
			       (char *)pbm->pbm_ranges,
			       sizeof(pbm->pbm_ranges));
	if (err != -1)
		pbm->num_pbm_ranges =
			(err / sizeof(struct linux_prom_pci_ranges));
	else
		pbm->num_pbm_ranges = 0;

	err = prom_getproperty(prom_node, "interrupt-map",
			       (char *)pbm->pbm_intmap,
			       sizeof(pbm->pbm_intmap));
	if (err != -1) {
		pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
		err = prom_getproperty(prom_node, "interrupt-map-mask",
				       (char *)&pbm->pbm_intmask,
				       sizeof(pbm->pbm_intmask));
		if (err == -1) {
			prom_printf("PSYCHO-PBM: Fatal error, no "
				    "interrupt-map-mask.\n");
			prom_halt();
		}
	} else {
		pbm->num_pbm_intmap = 0;
		memset(&pbm->pbm_intmask, 0, sizeof(pbm->pbm_intmask));
	}

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("PSYCHO-PBM: Fatal error, no bus-range.\n");
		prom_halt();
	}
	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

	psycho_pbm_strbuf_init(p, pbm, is_pbm_a);
}

#define PSYCHO_CONFIGSPACE	0x001000000UL

void psycho_init(int node, char *model_name)
{
	struct linux_prom64_registers pr_regs[3];
	struct pci_controller_info *p;
	struct pci_iommu *iommu;
	u32 upa_portid;
	int is_pbm_a, err;

	upa_portid = prom_getintdefault(node, "upa-portid", 0xff);

	for(p = pci_controller_root; p; p = p->next) {
		if (p->pbm_A.portid == upa_portid) {
			is_pbm_a = (p->pbm_A.prom_node == 0);
			psycho_pbm_init(p, node, is_pbm_a);
			return;
		}
	}

	p = kzalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("PSYCHO: Fatal memory allocation error.\n");
		prom_halt();
	}
	iommu = kzalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("PSYCHO: Fatal memory allocation error.\n");
		prom_halt();
	}
	p->pbm_A.iommu = p->pbm_B.iommu = iommu;

	p->next = pci_controller_root;
	pci_controller_root = p;

	p->pbm_A.portid = upa_portid;
	p->pbm_B.portid = upa_portid;
	p->index = pci_num_controllers++;
	p->pbms_same_domain = 0;
	p->scan_bus = psycho_scan_bus;
	p->irq_build = psycho_irq_build;
	p->base_address_update = psycho_base_address_update;
	p->resource_adjust = psycho_resource_adjust;
	p->pci_ops = &psycho_ops;

	err = prom_getproperty(node, "reg",
			       (char *)&pr_regs[0],
			       sizeof(pr_regs));
	if (err == 0 || err == -1) {
		prom_printf("PSYCHO: Fatal error, no reg property.\n");
		prom_halt();
	}

	p->pbm_A.controller_regs = pr_regs[2].phys_addr;
	p->pbm_B.controller_regs = pr_regs[2].phys_addr;
	printk("PCI: Found PSYCHO, control regs at %016lx\n",
	       p->pbm_A.controller_regs);

	p->pbm_A.config_space = p->pbm_B.config_space =
		(pr_regs[2].phys_addr + PSYCHO_CONFIGSPACE);
	printk("PSYCHO: Shared PCI config space at %016lx\n",
	       p->pbm_A.config_space);

	/*
	 * Psycho's PCI MEM space is mapped to a 2GB aligned area, so
	 * we need to adjust our MEM space mask.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	psycho_controller_hwinit(p);

	psycho_iommu_init(p);

	is_pbm_a = ((pr_regs[0].phys_addr & 0x6000) == 0x2000);
	psycho_pbm_init(p, node, is_pbm_a);
}
