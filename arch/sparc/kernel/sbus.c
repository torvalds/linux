/*
 * sbus.c: UltraSparc SBUS controller support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/page.h>
#include <asm/io.h>
#include <asm/upa.h>
#include <asm/cache.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/oplib.h>
#include <asm/starfire.h>

#include "iommu_common.h"

#define MAP_BASE	((u32)0xc0000000)

/* Offsets from iommu_regs */
#define SYSIO_IOMMUREG_BASE	0x2400UL
#define IOMMU_CONTROL	(0x2400UL - 0x2400UL)	/* IOMMU control register */
#define IOMMU_TSBBASE	(0x2408UL - 0x2400UL)	/* TSB base address register */
#define IOMMU_FLUSH	(0x2410UL - 0x2400UL)	/* IOMMU flush register */
#define IOMMU_VADIAG	(0x4400UL - 0x2400UL)	/* SBUS virtual address diagnostic */
#define IOMMU_TAGCMP	(0x4408UL - 0x2400UL)	/* TLB tag compare diagnostics */
#define IOMMU_LRUDIAG	(0x4500UL - 0x2400UL)	/* IOMMU LRU queue diagnostics */
#define IOMMU_TAGDIAG	(0x4580UL - 0x2400UL)	/* TLB tag diagnostics */
#define IOMMU_DRAMDIAG	(0x4600UL - 0x2400UL)	/* TLB data RAM diagnostics */

#define IOMMU_DRAM_VALID	(1UL << 30UL)

/* Offsets from strbuf_regs */
#define SYSIO_STRBUFREG_BASE	0x2800UL
#define STRBUF_CONTROL	(0x2800UL - 0x2800UL)	/* Control */
#define STRBUF_PFLUSH	(0x2808UL - 0x2800UL)	/* Page flush/invalidate */
#define STRBUF_FSYNC	(0x2810UL - 0x2800UL)	/* Flush synchronization */
#define STRBUF_DRAMDIAG	(0x5000UL - 0x2800UL)	/* data RAM diagnostic */
#define STRBUF_ERRDIAG	(0x5400UL - 0x2800UL)	/* error status diagnostics */
#define STRBUF_PTAGDIAG	(0x5800UL - 0x2800UL)	/* Page tag diagnostics */
#define STRBUF_LTAGDIAG	(0x5900UL - 0x2800UL)	/* Line tag diagnostics */

#define STRBUF_TAG_VALID	0x02UL

/* Enable 64-bit DVMA mode for the given device. */
void sbus_set_sbus64(struct device *dev, int bursts)
{
	struct iommu *iommu = dev->archdata.iommu;
	struct platform_device *op = to_platform_device(dev);
	const struct linux_prom_registers *regs;
	unsigned long cfg_reg;
	int slot;
	u64 val;

	regs = of_get_property(op->dev.of_node, "reg", NULL);
	if (!regs) {
		printk(KERN_ERR "sbus_set_sbus64: Cannot find regs for %s\n",
		       op->dev.of_node->full_name);
		return;
	}
	slot = regs->which_io;

	cfg_reg = iommu->write_complete_reg;
	switch (slot) {
	case 0:
		cfg_reg += 0x20UL;
		break;
	case 1:
		cfg_reg += 0x28UL;
		break;
	case 2:
		cfg_reg += 0x30UL;
		break;
	case 3:
		cfg_reg += 0x38UL;
		break;
	case 13:
		cfg_reg += 0x40UL;
		break;
	case 14:
		cfg_reg += 0x48UL;
		break;
	case 15:
		cfg_reg += 0x50UL;
		break;

	default:
		return;
	};

	val = upa_readq(cfg_reg);
	if (val & (1UL << 14UL)) {
		/* Extended transfer mode already enabled. */
		return;
	}

	val |= (1UL << 14UL);

	if (bursts & DMA_BURST8)
		val |= (1UL << 1UL);
	if (bursts & DMA_BURST16)
		val |= (1UL << 2UL);
	if (bursts & DMA_BURST32)
		val |= (1UL << 3UL);
	if (bursts & DMA_BURST64)
		val |= (1UL << 4UL);
	upa_writeq(val, cfg_reg);
}
EXPORT_SYMBOL(sbus_set_sbus64);

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

static unsigned int sbus_build_irq(struct platform_device *op, unsigned int ino)
{
	struct iommu *iommu = op->dev.archdata.iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned long imap, iclr;
	int sbus_level = 0;

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
		int sbus_slot = (ino & 0x18)>>3;
		
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

/* Error interrupt handling. */
#define SYSIO_UE_AFSR	0x0030UL
#define SYSIO_UE_AFAR	0x0038UL
#define  SYSIO_UEAFSR_PPIO  0x8000000000000000UL /* Primary PIO cause         */
#define  SYSIO_UEAFSR_PDRD  0x4000000000000000UL /* Primary DVMA read cause   */
#define  SYSIO_UEAFSR_PDWR  0x2000000000000000UL /* Primary DVMA write cause  */
#define  SYSIO_UEAFSR_SPIO  0x1000000000000000UL /* Secondary PIO is cause    */
#define  SYSIO_UEAFSR_SDRD  0x0800000000000000UL /* Secondary DVMA read cause */
#define  SYSIO_UEAFSR_SDWR  0x0400000000000000UL /* Secondary DVMA write cause*/
#define  SYSIO_UEAFSR_RESV1 0x03ff000000000000UL /* Reserved                  */
#define  SYSIO_UEAFSR_DOFF  0x0000e00000000000UL /* Doubleword Offset         */
#define  SYSIO_UEAFSR_SIZE  0x00001c0000000000UL /* Bad transfer size 2^SIZE  */
#define  SYSIO_UEAFSR_MID   0x000003e000000000UL /* UPA MID causing the fault */
#define  SYSIO_UEAFSR_RESV2 0x0000001fffffffffUL /* Reserved                  */
static irqreturn_t sysio_ue_handler(int irq, void *dev_id)
{
	struct platform_device *op = dev_id;
	struct iommu *iommu = op->dev.archdata.iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int reported, portid;

	afsr_reg = reg_base + SYSIO_UE_AFSR;
	afar_reg = reg_base + SYSIO_UE_AFAR;

	/* Latch error status. */
	afsr = upa_readq(afsr_reg);
	afar = upa_readq(afar_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SYSIO_UEAFSR_PPIO | SYSIO_UEAFSR_PDRD | SYSIO_UEAFSR_PDWR |
		 SYSIO_UEAFSR_SPIO | SYSIO_UEAFSR_SDRD | SYSIO_UEAFSR_SDWR);
	upa_writeq(error_bits, afsr_reg);

	portid = of_getintprop_default(op->dev.of_node, "portid", -1);

	/* Log the error. */
	printk("SYSIO[%x]: Uncorrectable ECC Error, primary error type[%s]\n",
	       portid,
	       (((error_bits & SYSIO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SYSIO_UEAFSR_PDRD) ?
		  "DVMA Read" :
		  ((error_bits & SYSIO_UEAFSR_PDWR) ?
		   "DVMA Write" : "???")))));
	printk("SYSIO[%x]: DOFF[%lx] SIZE[%lx] MID[%lx]\n",
	       portid,
	       (afsr & SYSIO_UEAFSR_DOFF) >> 45UL,
	       (afsr & SYSIO_UEAFSR_SIZE) >> 42UL,
	       (afsr & SYSIO_UEAFSR_MID) >> 37UL);
	printk("SYSIO[%x]: AFAR[%016lx]\n", portid, afar);
	printk("SYSIO[%x]: Secondary UE errors [", portid);
	reported = 0;
	if (afsr & SYSIO_UEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SYSIO_UEAFSR_SDRD) {
		reported++;
		printk("(DVMA Read)");
	}
	if (afsr & SYSIO_UEAFSR_SDWR) {
		reported++;
		printk("(DVMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	return IRQ_HANDLED;
}

#define SYSIO_CE_AFSR	0x0040UL
#define SYSIO_CE_AFAR	0x0048UL
#define  SYSIO_CEAFSR_PPIO  0x8000000000000000UL /* Primary PIO cause         */
#define  SYSIO_CEAFSR_PDRD  0x4000000000000000UL /* Primary DVMA read cause   */
#define  SYSIO_CEAFSR_PDWR  0x2000000000000000UL /* Primary DVMA write cause  */
#define  SYSIO_CEAFSR_SPIO  0x1000000000000000UL /* Secondary PIO cause       */
#define  SYSIO_CEAFSR_SDRD  0x0800000000000000UL /* Secondary DVMA read cause */
#define  SYSIO_CEAFSR_SDWR  0x0400000000000000UL /* Secondary DVMA write cause*/
#define  SYSIO_CEAFSR_RESV1 0x0300000000000000UL /* Reserved                  */
#define  SYSIO_CEAFSR_ESYND 0x00ff000000000000UL /* Syndrome Bits             */
#define  SYSIO_CEAFSR_DOFF  0x0000e00000000000UL /* Double Offset             */
#define  SYSIO_CEAFSR_SIZE  0x00001c0000000000UL /* Bad transfer size 2^SIZE  */
#define  SYSIO_CEAFSR_MID   0x000003e000000000UL /* UPA MID causing the fault */
#define  SYSIO_CEAFSR_RESV2 0x0000001fffffffffUL /* Reserved                  */
static irqreturn_t sysio_ce_handler(int irq, void *dev_id)
{
	struct platform_device *op = dev_id;
	struct iommu *iommu = op->dev.archdata.iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int reported, portid;

	afsr_reg = reg_base + SYSIO_CE_AFSR;
	afar_reg = reg_base + SYSIO_CE_AFAR;

	/* Latch error status. */
	afsr = upa_readq(afsr_reg);
	afar = upa_readq(afar_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SYSIO_CEAFSR_PPIO | SYSIO_CEAFSR_PDRD | SYSIO_CEAFSR_PDWR |
		 SYSIO_CEAFSR_SPIO | SYSIO_CEAFSR_SDRD | SYSIO_CEAFSR_SDWR);
	upa_writeq(error_bits, afsr_reg);

	portid = of_getintprop_default(op->dev.of_node, "portid", -1);

	printk("SYSIO[%x]: Correctable ECC Error, primary error type[%s]\n",
	       portid,
	       (((error_bits & SYSIO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SYSIO_CEAFSR_PDRD) ?
		  "DVMA Read" :
		  ((error_bits & SYSIO_CEAFSR_PDWR) ?
		   "DVMA Write" : "???")))));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("SYSIO[%x]: DOFF[%lx] ECC Syndrome[%lx] Size[%lx] MID[%lx]\n",
	       portid,
	       (afsr & SYSIO_CEAFSR_DOFF) >> 45UL,
	       (afsr & SYSIO_CEAFSR_ESYND) >> 48UL,
	       (afsr & SYSIO_CEAFSR_SIZE) >> 42UL,
	       (afsr & SYSIO_CEAFSR_MID) >> 37UL);
	printk("SYSIO[%x]: AFAR[%016lx]\n", portid, afar);

	printk("SYSIO[%x]: Secondary CE errors [", portid);
	reported = 0;
	if (afsr & SYSIO_CEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SYSIO_CEAFSR_SDRD) {
		reported++;
		printk("(DVMA Read)");
	}
	if (afsr & SYSIO_CEAFSR_SDWR) {
		reported++;
		printk("(DVMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	return IRQ_HANDLED;
}

#define SYSIO_SBUS_AFSR		0x2010UL
#define SYSIO_SBUS_AFAR		0x2018UL
#define  SYSIO_SBAFSR_PLE   0x8000000000000000UL /* Primary Late PIO Error    */
#define  SYSIO_SBAFSR_PTO   0x4000000000000000UL /* Primary SBUS Timeout      */
#define  SYSIO_SBAFSR_PBERR 0x2000000000000000UL /* Primary SBUS Error ACK    */
#define  SYSIO_SBAFSR_SLE   0x1000000000000000UL /* Secondary Late PIO Error  */
#define  SYSIO_SBAFSR_STO   0x0800000000000000UL /* Secondary SBUS Timeout    */
#define  SYSIO_SBAFSR_SBERR 0x0400000000000000UL /* Secondary SBUS Error ACK  */
#define  SYSIO_SBAFSR_RESV1 0x03ff000000000000UL /* Reserved                  */
#define  SYSIO_SBAFSR_RD    0x0000800000000000UL /* Primary was late PIO read */
#define  SYSIO_SBAFSR_RESV2 0x0000600000000000UL /* Reserved                  */
#define  SYSIO_SBAFSR_SIZE  0x00001c0000000000UL /* Size of transfer          */
#define  SYSIO_SBAFSR_MID   0x000003e000000000UL /* MID causing the error     */
#define  SYSIO_SBAFSR_RESV3 0x0000001fffffffffUL /* Reserved                  */
static irqreturn_t sysio_sbus_error_handler(int irq, void *dev_id)
{
	struct platform_device *op = dev_id;
	struct iommu *iommu = op->dev.archdata.iommu;
	unsigned long afsr_reg, afar_reg, reg_base;
	unsigned long afsr, afar, error_bits;
	int reported, portid;

	reg_base = iommu->write_complete_reg - 0x2000UL;
	afsr_reg = reg_base + SYSIO_SBUS_AFSR;
	afar_reg = reg_base + SYSIO_SBUS_AFAR;

	afsr = upa_readq(afsr_reg);
	afar = upa_readq(afar_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SYSIO_SBAFSR_PLE | SYSIO_SBAFSR_PTO | SYSIO_SBAFSR_PBERR |
		 SYSIO_SBAFSR_SLE | SYSIO_SBAFSR_STO | SYSIO_SBAFSR_SBERR);
	upa_writeq(error_bits, afsr_reg);

	portid = of_getintprop_default(op->dev.of_node, "portid", -1);

	/* Log the error. */
	printk("SYSIO[%x]: SBUS Error, primary error type[%s] read(%d)\n",
	       portid,
	       (((error_bits & SYSIO_SBAFSR_PLE) ?
		 "Late PIO Error" :
		 ((error_bits & SYSIO_SBAFSR_PTO) ?
		  "Time Out" :
		  ((error_bits & SYSIO_SBAFSR_PBERR) ?
		   "Error Ack" : "???")))),
	       (afsr & SYSIO_SBAFSR_RD) ? 1 : 0);
	printk("SYSIO[%x]: size[%lx] MID[%lx]\n",
	       portid,
	       (afsr & SYSIO_SBAFSR_SIZE) >> 42UL,
	       (afsr & SYSIO_SBAFSR_MID) >> 37UL);
	printk("SYSIO[%x]: AFAR[%016lx]\n", portid, afar);
	printk("SYSIO[%x]: Secondary SBUS errors [", portid);
	reported = 0;
	if (afsr & SYSIO_SBAFSR_SLE) {
		reported++;
		printk("(Late PIO Error)");
	}
	if (afsr & SYSIO_SBAFSR_STO) {
		reported++;
		printk("(Time Out)");
	}
	if (afsr & SYSIO_SBAFSR_SBERR) {
		reported++;
		printk("(Error Ack)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* XXX check iommu/strbuf for further error status XXX */

	return IRQ_HANDLED;
}

#define ECC_CONTROL	0x0020UL
#define  SYSIO_ECNTRL_ECCEN	0x8000000000000000UL /* Enable ECC Checking   */
#define  SYSIO_ECNTRL_UEEN	0x4000000000000000UL /* Enable UE Interrupts  */
#define  SYSIO_ECNTRL_CEEN	0x2000000000000000UL /* Enable CE Interrupts  */

#define SYSIO_UE_INO		0x34
#define SYSIO_CE_INO		0x35
#define SYSIO_SBUSERR_INO	0x36

static void __init sysio_register_error_handlers(struct platform_device *op)
{
	struct iommu *iommu = op->dev.archdata.iommu;
	unsigned long reg_base = iommu->write_complete_reg - 0x2000UL;
	unsigned int irq;
	u64 control;
	int portid;

	portid = of_getintprop_default(op->dev.of_node, "portid", -1);

	irq = sbus_build_irq(op, SYSIO_UE_INO);
	if (request_irq(irq, sysio_ue_handler, 0,
			"SYSIO_UE", op) < 0) {
		prom_printf("SYSIO[%x]: Cannot register UE interrupt.\n",
			    portid);
		prom_halt();
	}

	irq = sbus_build_irq(op, SYSIO_CE_INO);
	if (request_irq(irq, sysio_ce_handler, 0,
			"SYSIO_CE", op) < 0) {
		prom_printf("SYSIO[%x]: Cannot register CE interrupt.\n",
			    portid);
		prom_halt();
	}

	irq = sbus_build_irq(op, SYSIO_SBUSERR_INO);
	if (request_irq(irq, sysio_sbus_error_handler, 0,
			"SYSIO_SBERR", op) < 0) {
		prom_printf("SYSIO[%x]: Cannot register SBUS Error interrupt.\n",
			    portid);
		prom_halt();
	}

	/* Now turn the error interrupts on and also enable ECC checking. */
	upa_writeq((SYSIO_ECNTRL_ECCEN |
		    SYSIO_ECNTRL_UEEN  |
		    SYSIO_ECNTRL_CEEN),
		   reg_base + ECC_CONTROL);

	control = upa_readq(iommu->write_complete_reg);
	control |= 0x100UL; /* SBUS Error Interrupt Enable */
	upa_writeq(control, iommu->write_complete_reg);
}

/* Boot time initialization. */
static void __init sbus_iommu_init(struct platform_device *op)
{
	const struct linux_prom64_registers *pr;
	struct device_node *dp = op->dev.of_node;
	struct iommu *iommu;
	struct strbuf *strbuf;
	unsigned long regs, reg_base;
	int i, portid;
	u64 control;

	pr = of_get_property(dp, "reg", NULL);
	if (!pr) {
		prom_printf("sbus_iommu_init: Cannot map SYSIO "
			    "control registers.\n");
		prom_halt();
	}
	regs = pr->phys_addr;

	iommu = kzalloc(sizeof(*iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;
	strbuf = kzalloc(sizeof(*strbuf), GFP_ATOMIC);
	if (!strbuf)
		goto fatal_memory_error;

	op->dev.archdata.iommu = iommu;
	op->dev.archdata.stc = strbuf;
	op->dev.archdata.numa_node = -1;

	reg_base = regs + SYSIO_IOMMUREG_BASE;
	iommu->iommu_control = reg_base + IOMMU_CONTROL;
	iommu->iommu_tsbbase = reg_base + IOMMU_TSBBASE;
	iommu->iommu_flush = reg_base + IOMMU_FLUSH;
	iommu->iommu_tags = iommu->iommu_control +
		(IOMMU_TAGDIAG - IOMMU_CONTROL);

	reg_base = regs + SYSIO_STRBUFREG_BASE;
	strbuf->strbuf_control = reg_base + STRBUF_CONTROL;
	strbuf->strbuf_pflush = reg_base + STRBUF_PFLUSH;
	strbuf->strbuf_fsync = reg_base + STRBUF_FSYNC;

	strbuf->strbuf_enabled = 1;

	strbuf->strbuf_flushflag = (volatile unsigned long *)
		((((unsigned long)&strbuf->__flushflag_buf[0])
		  + 63UL)
		 & ~63UL);
	strbuf->strbuf_flushflag_pa = (unsigned long)
		__pa(strbuf->strbuf_flushflag);

	/* The SYSIO SBUS control register is used for dummy reads
	 * in order to ensure write completion.
	 */
	iommu->write_complete_reg = regs + 0x2000UL;

	portid = of_getintprop_default(op->dev.of_node, "portid", -1);
	printk(KERN_INFO "SYSIO: UPA portID %x, at %016lx\n",
	       portid, regs);

	/* Setup for TSB_SIZE=7, TBW_SIZE=0, MMU_DE=1, MMU_EN=1 */
	if (iommu_table_init(iommu, IO_TSB_SIZE, MAP_BASE, 0xffffffff, -1))
		goto fatal_memory_error;

	control = upa_readq(iommu->iommu_control);
	control = ((7UL << 16UL)	|
		   (0UL << 2UL)		|
		   (1UL << 1UL)		|
		   (1UL << 0UL));
	upa_writeq(control, iommu->iommu_control);

	/* Clean out any cruft in the IOMMU using
	 * diagnostic accesses.
	 */
	for (i = 0; i < 16; i++) {
		unsigned long dram, tag;

		dram = iommu->iommu_control + (IOMMU_DRAMDIAG - IOMMU_CONTROL);
		tag = iommu->iommu_control + (IOMMU_TAGDIAG - IOMMU_CONTROL);

		dram += (unsigned long)i * 8UL;
		tag += (unsigned long)i * 8UL;
		upa_writeq(0, dram);
		upa_writeq(0, tag);
	}
	upa_readq(iommu->write_complete_reg);

	/* Give the TSB to SYSIO. */
	upa_writeq(__pa(iommu->page_table), iommu->iommu_tsbbase);

	/* Setup streaming buffer, DE=1 SB_EN=1 */
	control = (1UL << 1UL) | (1UL << 0UL);
	upa_writeq(control, strbuf->strbuf_control);

	/* Clear out the tags using diagnostics. */
	for (i = 0; i < 16; i++) {
		unsigned long ptag, ltag;

		ptag = strbuf->strbuf_control +
			(STRBUF_PTAGDIAG - STRBUF_CONTROL);
		ltag = strbuf->strbuf_control +
			(STRBUF_LTAGDIAG - STRBUF_CONTROL);
		ptag += (unsigned long)i * 8UL;
		ltag += (unsigned long)i * 8UL;

		upa_writeq(0UL, ptag);
		upa_writeq(0UL, ltag);
	}

	/* Enable DVMA arbitration for all devices/slots. */
	control = upa_readq(iommu->write_complete_reg);
	control |= 0x3fUL;
	upa_writeq(control, iommu->write_complete_reg);

	/* Now some Xfire specific grot... */
	if (this_is_starfire)
		starfire_hookup(portid);

	sysio_register_error_handlers(op);
	return;

fatal_memory_error:
	prom_printf("sbus_iommu_init: Fatal memory allocation error.\n");
}

static int __init sbus_init(void)
{
	struct device_node *dp;

	for_each_node_by_name(dp, "sbus") {
		struct platform_device *op = of_find_device_by_node(dp);

		sbus_iommu_init(op);
		of_propagate_archdata(op);
	}

	return 0;
}

subsys_initcall(sbus_init);
