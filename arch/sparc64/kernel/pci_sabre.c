/* pci_sabre.c: Sabre specific PCI controller support.
 *
 * Copyright (C) 1997, 1998, 1999, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998, 1999 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek   (jakub@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>

#include <asm/apb.h>
#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/smp.h>
#include <asm/oplib.h>
#include <asm/prom.h>

#include "pci_impl.h"
#include "iommu_common.h"

/* All SABRE registers are 64-bits.  The following accessor
 * routines are how they are accessed.  The REG parameter
 * is a physical address.
 */
#define sabre_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define sabre_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory")

/* SABRE PCI controller register offsets and definitions. */
#define SABRE_UE_AFSR		0x0030UL
#define  SABRE_UEAFSR_PDRD	 0x4000000000000000UL	/* Primary PCI DMA Read */
#define  SABRE_UEAFSR_PDWR	 0x2000000000000000UL	/* Primary PCI DMA Write */
#define  SABRE_UEAFSR_SDRD	 0x0800000000000000UL	/* Secondary PCI DMA Read */
#define  SABRE_UEAFSR_SDWR	 0x0400000000000000UL	/* Secondary PCI DMA Write */
#define  SABRE_UEAFSR_SDTE	 0x0200000000000000UL	/* Secondary DMA Translation Error */
#define  SABRE_UEAFSR_PDTE	 0x0100000000000000UL	/* Primary DMA Translation Error */
#define  SABRE_UEAFSR_BMSK	 0x0000ffff00000000UL	/* Bytemask */
#define  SABRE_UEAFSR_OFF	 0x00000000e0000000UL	/* Offset (AFAR bits [5:3] */
#define  SABRE_UEAFSR_BLK	 0x0000000000800000UL	/* Was block operation */
#define SABRE_UECE_AFAR		0x0038UL
#define SABRE_CE_AFSR		0x0040UL
#define  SABRE_CEAFSR_PDRD	 0x4000000000000000UL	/* Primary PCI DMA Read */
#define  SABRE_CEAFSR_PDWR	 0x2000000000000000UL	/* Primary PCI DMA Write */
#define  SABRE_CEAFSR_SDRD	 0x0800000000000000UL	/* Secondary PCI DMA Read */
#define  SABRE_CEAFSR_SDWR	 0x0400000000000000UL	/* Secondary PCI DMA Write */
#define  SABRE_CEAFSR_ESYND	 0x00ff000000000000UL	/* ECC Syndrome */
#define  SABRE_CEAFSR_BMSK	 0x0000ffff00000000UL	/* Bytemask */
#define  SABRE_CEAFSR_OFF	 0x00000000e0000000UL	/* Offset */
#define  SABRE_CEAFSR_BLK	 0x0000000000800000UL	/* Was block operation */
#define SABRE_UECE_AFAR_ALIAS	0x0048UL	/* Aliases to 0x0038 */
#define SABRE_IOMMU_CONTROL	0x0200UL
#define  SABRE_IOMMUCTRL_ERRSTS	 0x0000000006000000UL	/* Error status bits */
#define  SABRE_IOMMUCTRL_ERR	 0x0000000001000000UL	/* Error present in IOTLB */
#define  SABRE_IOMMUCTRL_LCKEN	 0x0000000000800000UL	/* IOTLB lock enable */
#define  SABRE_IOMMUCTRL_LCKPTR	 0x0000000000780000UL	/* IOTLB lock pointer */
#define  SABRE_IOMMUCTRL_TSBSZ	 0x0000000000070000UL	/* TSB Size */
#define  SABRE_IOMMU_TSBSZ_1K   0x0000000000000000
#define  SABRE_IOMMU_TSBSZ_2K   0x0000000000010000
#define  SABRE_IOMMU_TSBSZ_4K   0x0000000000020000
#define  SABRE_IOMMU_TSBSZ_8K   0x0000000000030000
#define  SABRE_IOMMU_TSBSZ_16K  0x0000000000040000
#define  SABRE_IOMMU_TSBSZ_32K  0x0000000000050000
#define  SABRE_IOMMU_TSBSZ_64K  0x0000000000060000
#define  SABRE_IOMMU_TSBSZ_128K 0x0000000000070000
#define  SABRE_IOMMUCTRL_TBWSZ	 0x0000000000000004UL	/* TSB assumed page size */
#define  SABRE_IOMMUCTRL_DENAB	 0x0000000000000002UL	/* Diagnostic Mode Enable */
#define  SABRE_IOMMUCTRL_ENAB	 0x0000000000000001UL	/* IOMMU Enable */
#define SABRE_IOMMU_TSBBASE	0x0208UL
#define SABRE_IOMMU_FLUSH	0x0210UL
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
#define SABRE_WRSYNC		0x1c20UL
#define SABRE_PCICTRL		0x2000UL
#define  SABRE_PCICTRL_MRLEN	 0x0000001000000000UL	/* Use MemoryReadLine for block loads/stores */
#define  SABRE_PCICTRL_SERR	 0x0000000400000000UL	/* Set when SERR asserted on PCI bus */
#define  SABRE_PCICTRL_ARBPARK	 0x0000000000200000UL	/* Bus Parking 0=Ultra-IIi 1=prev-bus-owner */
#define  SABRE_PCICTRL_CPUPRIO	 0x0000000000100000UL	/* Ultra-IIi granted every other bus cycle */
#define  SABRE_PCICTRL_ARBPRIO	 0x00000000000f0000UL	/* Slot which is granted every other bus cycle */
#define  SABRE_PCICTRL_ERREN	 0x0000000000000100UL	/* PCI Error Interrupt Enable */
#define  SABRE_PCICTRL_RTRYWE	 0x0000000000000080UL	/* DMA Flow Control 0=wait-if-possible 1=retry */
#define  SABRE_PCICTRL_AEN	 0x000000000000000fUL	/* Slot PCI arbitration enables */
#define SABRE_PIOAFSR		0x2010UL
#define  SABRE_PIOAFSR_PMA	 0x8000000000000000UL	/* Primary Master Abort */
#define  SABRE_PIOAFSR_PTA	 0x4000000000000000UL	/* Primary Target Abort */
#define  SABRE_PIOAFSR_PRTRY	 0x2000000000000000UL	/* Primary Excessive Retries */
#define  SABRE_PIOAFSR_PPERR	 0x1000000000000000UL	/* Primary Parity Error */
#define  SABRE_PIOAFSR_SMA	 0x0800000000000000UL	/* Secondary Master Abort */
#define  SABRE_PIOAFSR_STA	 0x0400000000000000UL	/* Secondary Target Abort */
#define  SABRE_PIOAFSR_SRTRY	 0x0200000000000000UL	/* Secondary Excessive Retries */
#define  SABRE_PIOAFSR_SPERR	 0x0100000000000000UL	/* Secondary Parity Error */
#define  SABRE_PIOAFSR_BMSK	 0x0000ffff00000000UL	/* Byte Mask */
#define  SABRE_PIOAFSR_BLK	 0x0000000080000000UL	/* Was Block Operation */
#define SABRE_PIOAFAR		0x2018UL
#define SABRE_PCIDIAG		0x2020UL
#define  SABRE_PCIDIAG_DRTRY	 0x0000000000000040UL	/* Disable PIO Retry Limit */
#define  SABRE_PCIDIAG_IPAPAR	 0x0000000000000008UL	/* Invert PIO Address Parity */
#define  SABRE_PCIDIAG_IPDPAR	 0x0000000000000004UL	/* Invert PIO Data Parity */
#define  SABRE_PCIDIAG_IDDPAR	 0x0000000000000002UL	/* Invert DMA Data Parity */
#define  SABRE_PCIDIAG_ELPBK	 0x0000000000000001UL	/* Loopback Enable - not supported */
#define SABRE_PCITASR		0x2028UL
#define  SABRE_PCITASR_EF	 0x0000000000000080UL	/* Respond to 0xe0000000-0xffffffff */
#define  SABRE_PCITASR_CD	 0x0000000000000040UL	/* Respond to 0xc0000000-0xdfffffff */
#define  SABRE_PCITASR_AB	 0x0000000000000020UL	/* Respond to 0xa0000000-0xbfffffff */
#define  SABRE_PCITASR_89	 0x0000000000000010UL	/* Respond to 0x80000000-0x9fffffff */
#define  SABRE_PCITASR_67	 0x0000000000000008UL	/* Respond to 0x60000000-0x7fffffff */
#define  SABRE_PCITASR_45	 0x0000000000000004UL	/* Respond to 0x40000000-0x5fffffff */
#define  SABRE_PCITASR_23	 0x0000000000000002UL	/* Respond to 0x20000000-0x3fffffff */
#define  SABRE_PCITASR_01	 0x0000000000000001UL	/* Respond to 0x00000000-0x1fffffff */
#define SABRE_PIOBUF_DIAG	0x5000UL
#define SABRE_DMABUF_DIAGLO	0x5100UL
#define SABRE_DMABUF_DIAGHI	0x51c0UL
#define SABRE_IMAP_GFX_ALIAS	0x6000UL	/* Aliases to 0x1098 */
#define SABRE_IMAP_EUPA_ALIAS	0x8000UL	/* Aliases to 0x10a0 */
#define SABRE_IOMMU_VADIAG	0xa400UL
#define SABRE_IOMMU_TCDIAG	0xa408UL
#define SABRE_IOMMU_TAG		0xa580UL
#define  SABRE_IOMMUTAG_ERRSTS	 0x0000000001800000UL	/* Error status bits */
#define  SABRE_IOMMUTAG_ERR	 0x0000000000400000UL	/* Error present */
#define  SABRE_IOMMUTAG_WRITE	 0x0000000000200000UL	/* Page is writable */
#define  SABRE_IOMMUTAG_STREAM	 0x0000000000100000UL	/* Streamable bit - unused */
#define  SABRE_IOMMUTAG_SIZE	 0x0000000000080000UL	/* 0=8k 1=16k */
#define  SABRE_IOMMUTAG_VPN	 0x000000000007ffffUL	/* Virtual Page Number [31:13] */
#define SABRE_IOMMU_DATA	0xa600UL
#define SABRE_IOMMUDATA_VALID	 0x0000000040000000UL	/* Valid */
#define SABRE_IOMMUDATA_USED	 0x0000000020000000UL	/* Used (for LRU algorithm) */
#define SABRE_IOMMUDATA_CACHE	 0x0000000010000000UL	/* Cacheable */
#define SABRE_IOMMUDATA_PPN	 0x00000000001fffffUL	/* Physical Page Number [33:13] */
#define SABRE_PCI_IRQSTATE	0xa800UL
#define SABRE_OBIO_IRQSTATE	0xa808UL
#define SABRE_FFBCFG		0xf000UL
#define  SABRE_FFBCFG_SPRQS	 0x000000000f000000	/* Slave P_RQST queue size */
#define  SABRE_FFBCFG_ONEREAD	 0x0000000000004000	/* Slave supports one outstanding read */
#define SABRE_MCCTRL0		0xf010UL
#define  SABRE_MCCTRL0_RENAB	 0x0000000080000000	/* Refresh Enable */
#define  SABRE_MCCTRL0_EENAB	 0x0000000010000000	/* Enable all ECC functions */
#define  SABRE_MCCTRL0_11BIT	 0x0000000000001000	/* Enable 11-bit column addressing */
#define  SABRE_MCCTRL0_DPP	 0x0000000000000f00	/* DIMM Pair Present Bits */
#define  SABRE_MCCTRL0_RINTVL	 0x00000000000000ff	/* Refresh Interval */
#define SABRE_MCCTRL1		0xf018UL
#define  SABRE_MCCTRL1_AMDC	 0x0000000038000000	/* Advance Memdata Clock */
#define  SABRE_MCCTRL1_ARDC	 0x0000000007000000	/* Advance DRAM Read Data Clock */
#define  SABRE_MCCTRL1_CSR	 0x0000000000e00000	/* CAS to RAS delay for CBR refresh */
#define  SABRE_MCCTRL1_CASRW	 0x00000000001c0000	/* CAS length for read/write */
#define  SABRE_MCCTRL1_RCD	 0x0000000000038000	/* RAS to CAS delay */
#define  SABRE_MCCTRL1_CP	 0x0000000000007000	/* CAS Precharge */
#define  SABRE_MCCTRL1_RP	 0x0000000000000e00	/* RAS Precharge */
#define  SABRE_MCCTRL1_RAS	 0x00000000000001c0	/* Length of RAS for refresh */
#define  SABRE_MCCTRL1_CASRW2	 0x0000000000000038	/* Must be same as CASRW */
#define  SABRE_MCCTRL1_RSC	 0x0000000000000007	/* RAS after CAS hold time */
#define SABRE_RESETCTRL		0xf020UL

#define SABRE_CONFIGSPACE	0x001000000UL
#define SABRE_IOSPACE		0x002000000UL
#define SABRE_IOSPACE_SIZE	0x000ffffffUL
#define SABRE_MEMSPACE		0x100000000UL
#define SABRE_MEMSPACE_SIZE	0x07fffffffUL

static int hummingbird_p;
static struct pci_bus *sabre_root_bus;

/* SABRE error handling support. */
static void sabre_check_iommu_error(struct pci_pbm_info *pbm,
				    unsigned long afsr,
				    unsigned long afar)
{
	struct iommu *iommu = pbm->iommu;
	unsigned long iommu_tag[16];
	unsigned long iommu_data[16];
	unsigned long flags;
	u64 control;
	int i;

	spin_lock_irqsave(&iommu->lock, flags);
	control = sabre_read(iommu->iommu_control);
	if (control & SABRE_IOMMUCTRL_ERR) {
		char *type_string;

		/* Clear the error encountered bit.
		 * NOTE: On Sabre this is write 1 to clear,
		 *       which is different from Psycho.
		 */
		sabre_write(iommu->iommu_control, control);
		switch((control & SABRE_IOMMUCTRL_ERRSTS) >> 25UL) {
		case 1:
			type_string = "Invalid Error";
			break;
		case 3:
			type_string = "ECC Error";
			break;
		default:
			type_string = "Unknown";
			break;
		};
		printk("%s: IOMMU Error, type[%s]\n",
		       pbm->name, type_string);

		/* Enter diagnostic mode and probe for error'd
		 * entries in the IOTLB.
		 */
		control &= ~(SABRE_IOMMUCTRL_ERRSTS | SABRE_IOMMUCTRL_ERR);
		sabre_write(iommu->iommu_control,
			    (control | SABRE_IOMMUCTRL_DENAB));
		for (i = 0; i < 16; i++) {
			unsigned long base = pbm->controller_regs;

			iommu_tag[i] =
				sabre_read(base + SABRE_IOMMU_TAG + (i * 8UL));
			iommu_data[i] =
				sabre_read(base + SABRE_IOMMU_DATA + (i * 8UL));
			sabre_write(base + SABRE_IOMMU_TAG + (i * 8UL), 0);
			sabre_write(base + SABRE_IOMMU_DATA + (i * 8UL), 0);
		}
		sabre_write(iommu->iommu_control, control);

		for (i = 0; i < 16; i++) {
			unsigned long tag, data;

			tag = iommu_tag[i];
			if (!(tag & SABRE_IOMMUTAG_ERR))
				continue;

			data = iommu_data[i];
			switch((tag & SABRE_IOMMUTAG_ERRSTS) >> 23UL) {
			case 1:
				type_string = "Invalid Error";
				break;
			case 3:
				type_string = "ECC Error";
				break;
			default:
				type_string = "Unknown";
				break;
			};
			printk("%s: IOMMU TAG(%d)[RAW(%016lx)error(%s)wr(%d)sz(%dK)vpg(%08lx)]\n",
			       pbm->name, i, tag, type_string,
			       ((tag & SABRE_IOMMUTAG_WRITE) ? 1 : 0),
			       ((tag & SABRE_IOMMUTAG_SIZE) ? 64 : 8),
			       ((tag & SABRE_IOMMUTAG_VPN) << IOMMU_PAGE_SHIFT));
			printk("%s: IOMMU DATA(%d)[RAW(%016lx)valid(%d)used(%d)cache(%d)ppg(%016lx)\n",
			       pbm->name, i, data,
			       ((data & SABRE_IOMMUDATA_VALID) ? 1 : 0),
			       ((data & SABRE_IOMMUDATA_USED) ? 1 : 0),
			       ((data & SABRE_IOMMUDATA_CACHE) ? 1 : 0),
			       ((data & SABRE_IOMMUDATA_PPN) << IOMMU_PAGE_SHIFT));
		}
	}
	spin_unlock_irqrestore(&iommu->lock, flags);
}

static irqreturn_t sabre_ue_intr(int irq, void *dev_id)
{
	struct pci_pbm_info *pbm = dev_id;
	unsigned long afsr_reg = pbm->controller_regs + SABRE_UE_AFSR;
	unsigned long afar_reg = pbm->controller_regs + SABRE_UECE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch uncorrectable error status. */
	afar = sabre_read(afar_reg);
	afsr = sabre_read(afsr_reg);

	/* Clear the primary/secondary error status bits. */
	error_bits = afsr &
		(SABRE_UEAFSR_PDRD | SABRE_UEAFSR_PDWR |
		 SABRE_UEAFSR_SDRD | SABRE_UEAFSR_SDWR |
		 SABRE_UEAFSR_SDTE | SABRE_UEAFSR_PDTE);
	if (!error_bits)
		return IRQ_NONE;
	sabre_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("%s: Uncorrectable Error, primary error type[%s%s]\n",
	       pbm->name,
	       ((error_bits & SABRE_UEAFSR_PDRD) ?
		"DMA Read" :
		((error_bits & SABRE_UEAFSR_PDWR) ?
		 "DMA Write" : "???")),
	       ((error_bits & SABRE_UEAFSR_PDTE) ?
		":Translation Error" : ""));
	printk("%s: bytemask[%04lx] dword_offset[%lx] was_block(%d)\n",
	       pbm->name,
	       (afsr & SABRE_UEAFSR_BMSK) >> 32UL,
	       (afsr & SABRE_UEAFSR_OFF) >> 29UL,
	       ((afsr & SABRE_UEAFSR_BLK) ? 1 : 0));
	printk("%s: UE AFAR [%016lx]\n", pbm->name, afar);
	printk("%s: UE Secondary errors [", pbm->name);
	reported = 0;
	if (afsr & SABRE_UEAFSR_SDRD) {
		reported++;
		printk("(DMA Read)");
	}
	if (afsr & SABRE_UEAFSR_SDWR) {
		reported++;
		printk("(DMA Write)");
	}
	if (afsr & SABRE_UEAFSR_SDTE) {
		reported++;
		printk("(Translation Error)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* Interrogate IOMMU for error status. */
	sabre_check_iommu_error(pbm, afsr, afar);

	return IRQ_HANDLED;
}

static irqreturn_t sabre_ce_intr(int irq, void *dev_id)
{
	struct pci_pbm_info *pbm = dev_id;
	unsigned long afsr_reg = pbm->controller_regs + SABRE_CE_AFSR;
	unsigned long afar_reg = pbm->controller_regs + SABRE_UECE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch error status. */
	afar = sabre_read(afar_reg);
	afsr = sabre_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SABRE_CEAFSR_PDRD | SABRE_CEAFSR_PDWR |
		 SABRE_CEAFSR_SDRD | SABRE_CEAFSR_SDWR);
	if (!error_bits)
		return IRQ_NONE;
	sabre_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("%s: Correctable Error, primary error type[%s]\n",
	       pbm->name,
	       ((error_bits & SABRE_CEAFSR_PDRD) ?
		"DMA Read" :
		((error_bits & SABRE_CEAFSR_PDWR) ?
		 "DMA Write" : "???")));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("%s: syndrome[%02lx] bytemask[%04lx] dword_offset[%lx] "
	       "was_block(%d)\n",
	       pbm->name,
	       (afsr & SABRE_CEAFSR_ESYND) >> 48UL,
	       (afsr & SABRE_CEAFSR_BMSK) >> 32UL,
	       (afsr & SABRE_CEAFSR_OFF) >> 29UL,
	       ((afsr & SABRE_CEAFSR_BLK) ? 1 : 0));
	printk("%s: CE AFAR [%016lx]\n", pbm->name, afar);
	printk("%s: CE Secondary errors [", pbm->name);
	reported = 0;
	if (afsr & SABRE_CEAFSR_SDRD) {
		reported++;
		printk("(DMA Read)");
	}
	if (afsr & SABRE_CEAFSR_SDWR) {
		reported++;
		printk("(DMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	return IRQ_HANDLED;
}

static irqreturn_t sabre_pcierr_intr_other(struct pci_pbm_info *pbm)
{
	unsigned long csr_reg, csr, csr_error_bits;
	irqreturn_t ret = IRQ_NONE;
	u16 stat;

	csr_reg = pbm->controller_regs + SABRE_PCICTRL;
	csr = sabre_read(csr_reg);
	csr_error_bits =
		csr & SABRE_PCICTRL_SERR;
	if (csr_error_bits) {
		/* Clear the errors.  */
		sabre_write(csr_reg, csr);

		/* Log 'em.  */
		if (csr_error_bits & SABRE_PCICTRL_SERR)
			printk("%s: PCI SERR signal asserted.\n",
			       pbm->name);
		ret = IRQ_HANDLED;
	}
	pci_bus_read_config_word(sabre_root_bus, 0,
				 PCI_STATUS, &stat);
	if (stat & (PCI_STATUS_PARITY |
		    PCI_STATUS_SIG_TARGET_ABORT |
		    PCI_STATUS_REC_TARGET_ABORT |
		    PCI_STATUS_REC_MASTER_ABORT |
		    PCI_STATUS_SIG_SYSTEM_ERROR)) {
		printk("%s: PCI bus error, PCI_STATUS[%04x]\n",
		       pbm->name, stat);
		pci_bus_write_config_word(sabre_root_bus, 0,
					  PCI_STATUS, 0xffff);
		ret = IRQ_HANDLED;
	}
	return ret;
}

static irqreturn_t sabre_pcierr_intr(int irq, void *dev_id)
{
	struct pci_pbm_info *pbm = dev_id;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int reported;

	afsr_reg = pbm->controller_regs + SABRE_PIOAFSR;
	afar_reg = pbm->controller_regs + SABRE_PIOAFAR;

	/* Latch error status. */
	afar = sabre_read(afar_reg);
	afsr = sabre_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SABRE_PIOAFSR_PMA | SABRE_PIOAFSR_PTA |
		 SABRE_PIOAFSR_PRTRY | SABRE_PIOAFSR_PPERR |
		 SABRE_PIOAFSR_SMA | SABRE_PIOAFSR_STA |
		 SABRE_PIOAFSR_SRTRY | SABRE_PIOAFSR_SPERR);
	if (!error_bits)
		return sabre_pcierr_intr_other(pbm);
	sabre_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("%s: PCI Error, primary error type[%s]\n",
	       pbm->name,
	       (((error_bits & SABRE_PIOAFSR_PMA) ?
		 "Master Abort" :
		 ((error_bits & SABRE_PIOAFSR_PTA) ?
		  "Target Abort" :
		  ((error_bits & SABRE_PIOAFSR_PRTRY) ?
		   "Excessive Retries" :
		   ((error_bits & SABRE_PIOAFSR_PPERR) ?
		    "Parity Error" : "???"))))));
	printk("%s: bytemask[%04lx] was_block(%d)\n",
	       pbm->name,
	       (afsr & SABRE_PIOAFSR_BMSK) >> 32UL,
	       (afsr & SABRE_PIOAFSR_BLK) ? 1 : 0);
	printk("%s: PCI AFAR [%016lx]\n", pbm->name, afar);
	printk("%s: PCI Secondary errors [", pbm->name);
	reported = 0;
	if (afsr & SABRE_PIOAFSR_SMA) {
		reported++;
		printk("(Master Abort)");
	}
	if (afsr & SABRE_PIOAFSR_STA) {
		reported++;
		printk("(Target Abort)");
	}
	if (afsr & SABRE_PIOAFSR_SRTRY) {
		reported++;
		printk("(Excessive Retries)");
	}
	if (afsr & SABRE_PIOAFSR_SPERR) {
		reported++;
		printk("(Parity Error)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* For the error types shown, scan both PCI buses for devices
	 * which have logged that error type.
	 */

	/* If we see a Target Abort, this could be the result of an
	 * IOMMU translation error of some sort.  It is extremely
	 * useful to log this information as usually it indicates
	 * a bug in the IOMMU support code or a PCI device driver.
	 */
	if (error_bits & (SABRE_PIOAFSR_PTA | SABRE_PIOAFSR_STA)) {
		sabre_check_iommu_error(pbm, afsr, afar);
		pci_scan_for_target_abort(pbm, pbm->pci_bus);
	}
	if (error_bits & (SABRE_PIOAFSR_PMA | SABRE_PIOAFSR_SMA))
		pci_scan_for_master_abort(pbm, pbm->pci_bus);

	/* For excessive retries, SABRE/PBM will abort the device
	 * and there is no way to specifically check for excessive
	 * retries in the config space status registers.  So what
	 * we hope is that we'll catch it via the master/target
	 * abort events.
	 */

	if (error_bits & (SABRE_PIOAFSR_PPERR | SABRE_PIOAFSR_SPERR))
		pci_scan_for_parity_error(pbm, pbm->pci_bus);

	return IRQ_HANDLED;
}

static void sabre_register_error_handlers(struct pci_pbm_info *pbm)
{
	struct device_node *dp = pbm->prom_node;
	struct of_device *op;
	unsigned long base = pbm->controller_regs;
	u64 tmp;
	int err;

	if (pbm->chip_type == PBM_CHIP_TYPE_SABRE)
		dp = dp->parent;

	op = of_find_device_by_node(dp);
	if (!op)
		return;

	/* Sabre/Hummingbird IRQ property layout is:
	 * 0: PCI ERR
	 * 1: UE ERR
	 * 2: CE ERR
	 * 3: POWER FAIL
	 */
	if (op->num_irqs < 4)
		return;

	/* We clear the error bits in the appropriate AFSR before
	 * registering the handler so that we don't get spurious
	 * interrupts.
	 */
	sabre_write(base + SABRE_UE_AFSR,
		    (SABRE_UEAFSR_PDRD | SABRE_UEAFSR_PDWR |
		     SABRE_UEAFSR_SDRD | SABRE_UEAFSR_SDWR |
		     SABRE_UEAFSR_SDTE | SABRE_UEAFSR_PDTE));

	err = request_irq(op->irqs[1], sabre_ue_intr, 0, "SABRE_UE", pbm);
	if (err)
		printk(KERN_WARNING "%s: Couldn't register UE, err=%d.\n",
		       pbm->name, err);

	sabre_write(base + SABRE_CE_AFSR,
		    (SABRE_CEAFSR_PDRD | SABRE_CEAFSR_PDWR |
		     SABRE_CEAFSR_SDRD | SABRE_CEAFSR_SDWR));

	err = request_irq(op->irqs[2], sabre_ce_intr, 0, "SABRE_CE", pbm);
	if (err)
		printk(KERN_WARNING "%s: Couldn't register CE, err=%d.\n",
		       pbm->name, err);
	err = request_irq(op->irqs[0], sabre_pcierr_intr, 0,
			  "SABRE_PCIERR", pbm);
	if (err)
		printk(KERN_WARNING "%s: Couldn't register PCIERR, err=%d.\n",
		       pbm->name, err);

	tmp = sabre_read(base + SABRE_PCICTRL);
	tmp |= SABRE_PCICTRL_ERREN;
	sabre_write(base + SABRE_PCICTRL, tmp);
}

static void apb_init(struct pci_bus *sabre_bus)
{
	struct pci_dev *pdev;

	list_for_each_entry(pdev, &sabre_bus->devices, bus_list) {
		if (pdev->vendor == PCI_VENDOR_ID_SUN &&
		    pdev->device == PCI_DEVICE_ID_SUN_SIMBA) {
			u16 word16;

			pci_read_config_word(pdev, PCI_COMMAND, &word16);
			word16 |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY |
				PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY |
				PCI_COMMAND_IO;
			pci_write_config_word(pdev, PCI_COMMAND, word16);

			/* Status register bits are "write 1 to clear". */
			pci_write_config_word(pdev, PCI_STATUS, 0xffff);
			pci_write_config_word(pdev, PCI_SEC_STATUS, 0xffff);

			/* Use a primary/seconday latency timer value
			 * of 64.
			 */
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 64);
			pci_write_config_byte(pdev, PCI_SEC_LATENCY_TIMER, 64);

			/* Enable reporting/forwarding of master aborts,
			 * parity, and SERR.
			 */
			pci_write_config_byte(pdev, PCI_BRIDGE_CONTROL,
					      (PCI_BRIDGE_CTL_PARITY |
					       PCI_BRIDGE_CTL_SERR |
					       PCI_BRIDGE_CTL_MASTER_ABORT));
		}
	}
}

static void __init sabre_scan_bus(struct pci_pbm_info *pbm)
{
	static int once;

	/* The APB bridge speaks to the Sabre host PCI bridge
	 * at 66Mhz, but the front side of APB runs at 33Mhz
	 * for both segments.
	 *
	 * Hummingbird systems do not use APB, so they run
	 * at 66MHZ.
	 */
	if (hummingbird_p)
		pbm->is_66mhz_capable = 1;
	else
		pbm->is_66mhz_capable = 0;

	/* This driver has not been verified to handle
	 * multiple SABREs yet, so trap this.
	 *
	 * Also note that the SABRE host bridge is hardwired
	 * to live at bus 0.
	 */
	if (once != 0) {
		prom_printf("SABRE: Multiple controllers unsupported.\n");
		prom_halt();
	}
	once++;

	pbm->pci_bus = pci_scan_one_pbm(pbm);
	if (!pbm->pci_bus)
		return;

	sabre_root_bus = pbm->pci_bus;

	apb_init(pbm->pci_bus);

	sabre_register_error_handlers(pbm);
}

static int sabre_iommu_init(struct pci_pbm_info *pbm,
			    int tsbsize, unsigned long dvma_offset,
			    u32 dma_mask)
{
	struct iommu *iommu = pbm->iommu;
	unsigned long i;
	u64 control;
	int err;

	/* Register addresses. */
	iommu->iommu_control  = pbm->controller_regs + SABRE_IOMMU_CONTROL;
	iommu->iommu_tsbbase  = pbm->controller_regs + SABRE_IOMMU_TSBBASE;
	iommu->iommu_flush    = pbm->controller_regs + SABRE_IOMMU_FLUSH;
	iommu->iommu_tags     = iommu->iommu_flush + (0xa580UL - 0x0210UL);
	iommu->write_complete_reg = pbm->controller_regs + SABRE_WRSYNC;
	/* Sabre's IOMMU lacks ctx flushing. */
	iommu->iommu_ctxflush = 0;
                                        
	/* Invalidate TLB Entries. */
	control = sabre_read(pbm->controller_regs + SABRE_IOMMU_CONTROL);
	control |= SABRE_IOMMUCTRL_DENAB;
	sabre_write(pbm->controller_regs + SABRE_IOMMU_CONTROL, control);

	for(i = 0; i < 16; i++) {
		sabre_write(pbm->controller_regs + SABRE_IOMMU_TAG + (i * 8UL), 0);
		sabre_write(pbm->controller_regs + SABRE_IOMMU_DATA + (i * 8UL), 0);
	}

	/* Leave diag mode enabled for full-flushing done
	 * in pci_iommu.c
	 */
	err = iommu_table_init(iommu, tsbsize * 1024 * 8,
			       dvma_offset, dma_mask, pbm->numa_node);
	if (err)
		return err;

	sabre_write(pbm->controller_regs + SABRE_IOMMU_TSBBASE,
		    __pa(iommu->page_table));

	control = sabre_read(pbm->controller_regs + SABRE_IOMMU_CONTROL);
	control &= ~(SABRE_IOMMUCTRL_TSBSZ | SABRE_IOMMUCTRL_TBWSZ);
	control |= SABRE_IOMMUCTRL_ENAB;
	switch(tsbsize) {
	case 64:
		control |= SABRE_IOMMU_TSBSZ_64K;
		break;
	case 128:
		control |= SABRE_IOMMU_TSBSZ_128K;
		break;
	default:
		prom_printf("iommu_init: Illegal TSB size %d\n", tsbsize);
		prom_halt();
		break;
	}
	sabre_write(pbm->controller_regs + SABRE_IOMMU_CONTROL, control);

	return 0;
}

static void __init sabre_pbm_init(struct pci_controller_info *p,
				  struct pci_pbm_info *pbm, struct device_node *dp)
{
	pbm->name = dp->full_name;
	printk("%s: SABRE PCI Bus Module\n", pbm->name);

	pbm->numa_node = -1;

	pbm->scan_bus = sabre_scan_bus;
	pbm->pci_ops = &sun4u_pci_ops;
	pbm->config_space_reg_bits = 8;

	pbm->index = pci_num_pbms++;

	pbm->chip_type = PBM_CHIP_TYPE_SABRE;
	pbm->parent = p;
	pbm->prom_node = dp;
	pci_get_pbm_props(pbm);

	pci_determine_mem_io_space(pbm);
}

void __init sabre_init(struct device_node *dp, char *model_name)
{
	const struct linux_prom64_registers *pr_regs;
	struct pci_controller_info *p;
	struct pci_pbm_info *pbm;
	struct iommu *iommu;
	int tsbsize;
	const u32 *vdma;
	u32 upa_portid, dma_mask;
	u64 clear_irq;

	hummingbird_p = 0;
	if (!strcmp(model_name, "pci108e,a001"))
		hummingbird_p = 1;
	else if (!strcmp(model_name, "SUNW,sabre")) {
		const char *compat = of_get_property(dp, "compatible", NULL);
		if (compat && !strcmp(compat, "pci108e,a001"))
			hummingbird_p = 1;
		if (!hummingbird_p) {
			struct device_node *dp;

			/* Of course, Sun has to encode things a thousand
			 * different ways, inconsistently.
			 */
			for_each_node_by_type(dp, "cpu") {
				if (!strcmp(dp->name, "SUNW,UltraSPARC-IIe"))
					hummingbird_p = 1;
			}
		}
	}

	p = kzalloc(sizeof(*p), GFP_ATOMIC);
	if (!p)
		goto fatal_memory_error;

	iommu = kzalloc(sizeof(*iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;
	pbm = &p->pbm_A;
	pbm->iommu = iommu;

	upa_portid = of_getintprop_default(dp, "upa-portid", 0xff);

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	pbm->portid = upa_portid;

	/*
	 * Map in SABRE register set and report the presence of this SABRE.
	 */
	
	pr_regs = of_get_property(dp, "reg", NULL);

	/*
	 * First REG in property is base of entire SABRE register space.
	 */
	pbm->controller_regs = pr_regs[0].phys_addr;

	/* Clear interrupts */

	/* PCI first */
	for (clear_irq = SABRE_ICLR_A_SLOT0; clear_irq < SABRE_ICLR_B_SLOT0 + 0x80; clear_irq += 8)
		sabre_write(pbm->controller_regs + clear_irq, 0x0UL);

	/* Then OBIO */
	for (clear_irq = SABRE_ICLR_SCSI; clear_irq < SABRE_ICLR_SCSI + 0x80; clear_irq += 8)
		sabre_write(pbm->controller_regs + clear_irq, 0x0UL);

	/* Error interrupts are enabled later after the bus scan. */
	sabre_write(pbm->controller_regs + SABRE_PCICTRL,
		    (SABRE_PCICTRL_MRLEN   | SABRE_PCICTRL_SERR |
		     SABRE_PCICTRL_ARBPARK | SABRE_PCICTRL_AEN));

	/* Now map in PCI config space for entire SABRE. */
	pbm->config_space =
		(pbm->controller_regs + SABRE_CONFIGSPACE);

	vdma = of_get_property(dp, "virtual-dma", NULL);

	dma_mask = vdma[0];
	switch(vdma[1]) {
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
			prom_printf("SABRE: strange virtual-dma size.\n");
			prom_halt();
	}

	if (sabre_iommu_init(pbm, tsbsize, vdma[0], dma_mask))
		goto fatal_memory_error;

	/*
	 * Look for APB underneath.
	 */
	sabre_pbm_init(p, pbm, dp);
	return;

fatal_memory_error:
	prom_printf("SABRE: Fatal memory allocation error.\n");
	prom_halt();
}
