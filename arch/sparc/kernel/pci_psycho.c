/* pci_psycho.c: PSYCHO/U2P specific PCI controller support.
 *
 * Copyright (C) 1997, 1998, 1999, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998, 1999 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek   (jakub@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>

#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/starfire.h>
#include <asm/prom.h>
#include <asm/upa.h>

#include "pci_impl.h"
#include "iommu_common.h"
#include "psycho_common.h"

#define DRIVER_NAME	"psycho"
#define PFX		DRIVER_NAME ": "

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

/* PSYCHO error handling support. */

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
#define PSYCHO_STC_TAG_A	0xb800UL
#define PSYCHO_STC_TAG_B	0xc800UL
#define PSYCHO_STC_LINE_A	0xb900UL
#define PSYCHO_STC_LINE_B	0xc900UL

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
#define PSYCHO_IOMMU_DATA	0xa600UL

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

static irqreturn_t psycho_ue_intr(int irq, void *dev_id)
{
	struct pci_pbm_info *pbm = dev_id;
	unsigned long afsr_reg = pbm->controller_regs + PSYCHO_UE_AFSR;
	unsigned long afar_reg = pbm->controller_regs + PSYCHO_UE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch uncorrectable error status. */
	afar = upa_readq(afar_reg);
	afsr = upa_readq(afsr_reg);

	/* Clear the primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_UEAFSR_PPIO | PSYCHO_UEAFSR_PDRD | PSYCHO_UEAFSR_PDWR |
		 PSYCHO_UEAFSR_SPIO | PSYCHO_UEAFSR_SDRD | PSYCHO_UEAFSR_SDWR);
	if (!error_bits)
		return IRQ_NONE;
	upa_writeq(error_bits, afsr_reg);

	/* Log the error. */
	printk("%s: Uncorrectable Error, primary error type[%s]\n",
	       pbm->name,
	       (((error_bits & PSYCHO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & PSYCHO_UEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & PSYCHO_UEAFSR_PDWR) ?
		   "DMA Write" : "???")))));
	printk("%s: bytemask[%04lx] dword_offset[%lx] UPA_MID[%02lx] was_block(%d)\n",
	       pbm->name,
	       (afsr & PSYCHO_UEAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_UEAFSR_DOFF) >> 29UL,
	       (afsr & PSYCHO_UEAFSR_MID) >> 24UL,
	       ((afsr & PSYCHO_UEAFSR_BLK) ? 1 : 0));
	printk("%s: UE AFAR [%016lx]\n", pbm->name, afar);
	printk("%s: UE Secondary errors [", pbm->name);
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

	/* Interrogate both IOMMUs for error status. */
	psycho_check_iommu_error(pbm, afsr, afar, UE_ERR);
	if (pbm->sibling)
		psycho_check_iommu_error(pbm->sibling, afsr, afar, UE_ERR);

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

static irqreturn_t psycho_ce_intr(int irq, void *dev_id)
{
	struct pci_pbm_info *pbm = dev_id;
	unsigned long afsr_reg = pbm->controller_regs + PSYCHO_CE_AFSR;
	unsigned long afar_reg = pbm->controller_regs + PSYCHO_CE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch error status. */
	afar = upa_readq(afar_reg);
	afsr = upa_readq(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_CEAFSR_PPIO | PSYCHO_CEAFSR_PDRD | PSYCHO_CEAFSR_PDWR |
		 PSYCHO_CEAFSR_SPIO | PSYCHO_CEAFSR_SDRD | PSYCHO_CEAFSR_SDWR);
	if (!error_bits)
		return IRQ_NONE;
	upa_writeq(error_bits, afsr_reg);

	/* Log the error. */
	printk("%s: Correctable Error, primary error type[%s]\n",
	       pbm->name,
	       (((error_bits & PSYCHO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & PSYCHO_CEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & PSYCHO_CEAFSR_PDWR) ?
		   "DMA Write" : "???")))));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("%s: syndrome[%02lx] bytemask[%04lx] dword_offset[%lx] "
	       "UPA_MID[%02lx] was_block(%d)\n",
	       pbm->name,
	       (afsr & PSYCHO_CEAFSR_ESYND) >> 48UL,
	       (afsr & PSYCHO_CEAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_CEAFSR_DOFF) >> 29UL,
	       (afsr & PSYCHO_CEAFSR_MID) >> 24UL,
	       ((afsr & PSYCHO_CEAFSR_BLK) ? 1 : 0));
	printk("%s: CE AFAR [%016lx]\n", pbm->name, afar);
	printk("%s: CE Secondary errors [", pbm->name);
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
#define PSYCHO_PCI_AFAR_A	0x2018UL
#define PSYCHO_PCI_AFAR_B	0x4018UL

/* XXX What about PowerFail/PowerManagement??? -DaveM */
#define PSYCHO_ECC_CTRL		0x0020
#define  PSYCHO_ECCCTRL_EE	 0x8000000000000000UL /* Enable ECC Checking */
#define  PSYCHO_ECCCTRL_UE	 0x4000000000000000UL /* Enable UE Interrupts */
#define  PSYCHO_ECCCTRL_CE	 0x2000000000000000UL /* Enable CE INterrupts */
static void psycho_register_error_handlers(struct pci_pbm_info *pbm)
{
	struct platform_device *op = of_find_device_by_node(pbm->op->dev.of_node);
	unsigned long base = pbm->controller_regs;
	u64 tmp;
	int err;

	if (!op)
		return;

	/* Psycho interrupt property order is:
	 * 0: PCIERR INO for this PBM
	 * 1: UE ERR
	 * 2: CE ERR
	 * 3: POWER FAIL
	 * 4: SPARE HARDWARE
	 * 5: POWER MANAGEMENT
	 */

	if (op->archdata.num_irqs < 6)
		return;

	/* We really mean to ignore the return result here.  Two
	 * PCI controller share the same interrupt numbers and
	 * drive the same front-end hardware.
	 */
	err = request_irq(op->archdata.irqs[1], psycho_ue_intr, IRQF_SHARED,
			  "PSYCHO_UE", pbm);
	err = request_irq(op->archdata.irqs[2], psycho_ce_intr, IRQF_SHARED,
			  "PSYCHO_CE", pbm);

	/* This one, however, ought not to fail.  We can just warn
	 * about it since the system can still operate properly even
	 * if this fails.
	 */
	err = request_irq(op->archdata.irqs[0], psycho_pcierr_intr, IRQF_SHARED,
			  "PSYCHO_PCIERR", pbm);
	if (err)
		printk(KERN_WARNING "%s: Could not register PCIERR, "
		       "err=%d\n", pbm->name, err);

	/* Enable UE and CE interrupts for controller. */
	upa_writeq((PSYCHO_ECCCTRL_EE |
		    PSYCHO_ECCCTRL_UE |
		    PSYCHO_ECCCTRL_CE), base + PSYCHO_ECC_CTRL);

	/* Enable PCI Error interrupts and clear error
	 * bits for each PBM.
	 */
	tmp = upa_readq(base + PSYCHO_PCIA_CTRL);
	tmp |= (PSYCHO_PCICTRL_SERR |
		PSYCHO_PCICTRL_SBH_ERR |
		PSYCHO_PCICTRL_EEN);
	tmp &= ~(PSYCHO_PCICTRL_SBH_INT);
	upa_writeq(tmp, base + PSYCHO_PCIA_CTRL);
		     
	tmp = upa_readq(base + PSYCHO_PCIB_CTRL);
	tmp |= (PSYCHO_PCICTRL_SERR |
		PSYCHO_PCICTRL_SBH_ERR |
		PSYCHO_PCICTRL_EEN);
	tmp &= ~(PSYCHO_PCICTRL_SBH_INT);
	upa_writeq(tmp, base + PSYCHO_PCIB_CTRL);
}

/* PSYCHO boot time probing and initialization. */
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

static void psycho_scan_bus(struct pci_pbm_info *pbm,
			    struct device *parent)
{
	pbm_config_busmastering(pbm);
	pbm->is_66mhz_capable = 0;
	pbm->pci_bus = pci_scan_one_pbm(pbm, parent);

	/* After the PCI bus scan is complete, we can register
	 * the error interrupt handlers.
	 */
	psycho_register_error_handlers(pbm);
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

static void psycho_controller_hwinit(struct pci_pbm_info *pbm)
{
	u64 tmp;

	upa_writeq(5, pbm->controller_regs + PSYCHO_IRQ_RETRY);

	/* Enable arbiter for all PCI slots. */
	tmp = upa_readq(pbm->controller_regs + PSYCHO_PCIA_CTRL);
	tmp |= PSYCHO_PCICTRL_AEN;
	upa_writeq(tmp, pbm->controller_regs + PSYCHO_PCIA_CTRL);

	tmp = upa_readq(pbm->controller_regs + PSYCHO_PCIB_CTRL);
	tmp |= PSYCHO_PCICTRL_AEN;
	upa_writeq(tmp, pbm->controller_regs + PSYCHO_PCIB_CTRL);

	/* Disable DMA write / PIO read synchronization on
	 * both PCI bus segments.
	 * [ U2P Erratum 1243770, STP2223BGA data sheet ]
	 */
	tmp = upa_readq(pbm->controller_regs + PSYCHO_PCIA_DIAG);
	tmp |= PSYCHO_PCIDIAG_DDWSYNC;
	upa_writeq(tmp, pbm->controller_regs + PSYCHO_PCIA_DIAG);

	tmp = upa_readq(pbm->controller_regs + PSYCHO_PCIB_DIAG);
	tmp |= PSYCHO_PCIDIAG_DDWSYNC;
	upa_writeq(tmp, pbm->controller_regs + PSYCHO_PCIB_DIAG);
}

static void psycho_pbm_strbuf_init(struct pci_pbm_info *pbm,
				   int is_pbm_a)
{
	unsigned long base = pbm->controller_regs;
	u64 control;

	if (is_pbm_a) {
		pbm->stc.strbuf_control  = base + PSYCHO_STRBUF_CONTROL_A;
		pbm->stc.strbuf_pflush   = base + PSYCHO_STRBUF_FLUSH_A;
		pbm->stc.strbuf_fsync    = base + PSYCHO_STRBUF_FSYNC_A;
		pbm->stc.strbuf_err_stat = base + PSYCHO_STC_ERR_A;
		pbm->stc.strbuf_tag_diag = base + PSYCHO_STC_TAG_A;
		pbm->stc.strbuf_line_diag= base + PSYCHO_STC_LINE_A;
	} else {
		pbm->stc.strbuf_control  = base + PSYCHO_STRBUF_CONTROL_B;
		pbm->stc.strbuf_pflush   = base + PSYCHO_STRBUF_FLUSH_B;
		pbm->stc.strbuf_fsync    = base + PSYCHO_STRBUF_FSYNC_B;
		pbm->stc.strbuf_err_stat = base + PSYCHO_STC_ERR_B;
		pbm->stc.strbuf_tag_diag = base + PSYCHO_STC_TAG_B;
		pbm->stc.strbuf_line_diag= base + PSYCHO_STC_LINE_B;
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
	control = upa_readq(pbm->stc.strbuf_control);
	control |= PSYCHO_STRBUF_CTRL_ENAB;
	control &= ~(PSYCHO_STRBUF_CTRL_LENAB | PSYCHO_STRBUF_CTRL_LPTR);
#ifdef PSYCHO_STRBUF_RERUN_ENABLE
	control &= ~(PSYCHO_STRBUF_CTRL_RRDIS);
#else
#ifdef PSYCHO_STRBUF_RERUN_DISABLE
	control |= PSYCHO_STRBUF_CTRL_RRDIS;
#endif
#endif
	upa_writeq(control, pbm->stc.strbuf_control);

	pbm->stc.strbuf_enabled = 1;
}

#define PSYCHO_IOSPACE_A	0x002000000UL
#define PSYCHO_IOSPACE_B	0x002010000UL
#define PSYCHO_IOSPACE_SIZE	0x00000ffffUL
#define PSYCHO_MEMSPACE_A	0x100000000UL
#define PSYCHO_MEMSPACE_B	0x180000000UL
#define PSYCHO_MEMSPACE_SIZE	0x07fffffffUL

static void psycho_pbm_init(struct pci_pbm_info *pbm,
			    struct platform_device *op, int is_pbm_a)
{
	psycho_pbm_init_common(pbm, op, "PSYCHO", PBM_CHIP_TYPE_PSYCHO);
	psycho_pbm_strbuf_init(pbm, is_pbm_a);
	psycho_scan_bus(pbm, &op->dev);
}

static struct pci_pbm_info *psycho_find_sibling(u32 upa_portid)
{
	struct pci_pbm_info *pbm;

	for (pbm = pci_pbm_root; pbm; pbm = pbm->next) {
		if (pbm->portid == upa_portid)
			return pbm;
	}
	return NULL;
}

#define PSYCHO_CONFIGSPACE	0x001000000UL

static int psycho_probe(struct platform_device *op)
{
	const struct linux_prom64_registers *pr_regs;
	struct device_node *dp = op->dev.of_node;
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	int is_pbm_a, err;
	u32 upa_portid;

	upa_portid = of_getintprop_default(dp, "upa-portid", 0xff);

	err = -ENOMEM;
	pbm = kzalloc(sizeof(*pbm), GFP_KERNEL);
	if (!pbm) {
		printk(KERN_ERR PFX "Cannot allocate pci_pbm_info.\n");
		goto out_err;
	}

	pbm->sibling = psycho_find_sibling(upa_portid);
	if (pbm->sibling) {
		iommu = pbm->sibling->iommu;
	} else {
		iommu = kzalloc(sizeof(struct iommu), GFP_KERNEL);
		if (!iommu) {
			printk(KERN_ERR PFX "Cannot allocate PBM iommu.\n");
			goto out_free_controller;
		}
	}

	pbm->iommu = iommu;
	pbm->portid = upa_portid;

	pr_regs = of_get_property(dp, "reg", NULL);
	err = -ENODEV;
	if (!pr_regs) {
		printk(KERN_ERR PFX "No reg property.\n");
		goto out_free_iommu;
	}

	is_pbm_a = ((pr_regs[0].phys_addr & 0x6000) == 0x2000);

	pbm->controller_regs = pr_regs[2].phys_addr;
	pbm->config_space = (pr_regs[2].phys_addr + PSYCHO_CONFIGSPACE);

	if (is_pbm_a) {
		pbm->pci_afsr = pbm->controller_regs + PSYCHO_PCI_AFSR_A;
		pbm->pci_afar = pbm->controller_regs + PSYCHO_PCI_AFAR_A;
		pbm->pci_csr  = pbm->controller_regs + PSYCHO_PCIA_CTRL;
	} else {
		pbm->pci_afsr = pbm->controller_regs + PSYCHO_PCI_AFSR_B;
		pbm->pci_afar = pbm->controller_regs + PSYCHO_PCI_AFAR_B;
		pbm->pci_csr  = pbm->controller_regs + PSYCHO_PCIB_CTRL;
	}

	psycho_controller_hwinit(pbm);
	if (!pbm->sibling) {
		err = psycho_iommu_init(pbm, 128, 0xc0000000,
					0xffffffff, PSYCHO_CONTROL);
		if (err)
			goto out_free_iommu;

		/* If necessary, hook us up for starfire IRQ translations. */
		if (this_is_starfire)
			starfire_hookup(pbm->portid);
	}

	psycho_pbm_init(pbm, op, is_pbm_a);

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	if (pbm->sibling)
		pbm->sibling->sibling = pbm;

	dev_set_drvdata(&op->dev, pbm);

	return 0;

out_free_iommu:
	if (!pbm->sibling)
		kfree(pbm->iommu);

out_free_controller:
	kfree(pbm);

out_err:
	return err;
}

static const struct of_device_id psycho_match[] = {
	{
		.name = "pci",
		.compatible = "pci108e,8000",
	},
	{},
};

static struct platform_driver psycho_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = psycho_match,
	},
	.probe		= psycho_probe,
};

static int __init psycho_init(void)
{
	return platform_driver_register(&psycho_driver);
}

subsys_initcall(psycho_init);
