/*
 * Driver for Nuvoton Technology Corporation w83667hg/w83677hg-i CIR
 *
 * Copyright (C) 2010 Jarod Wilson <jarod@redhat.com>
 * Copyright (C) 2009 Nuvoton PS Team
 *
 * Special thanks to Nuvoton for providing hardware, spec sheets and
 * sample code upon which portions of this driver are based. Indirect
 * thanks also to Maxim Levitsky, whose ene_ir driver this driver is
 * modeled after.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pnp.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <media/rc-core.h>
#include <linux/pci_ids.h>

#include "nuvoton-cir.h"

/* write val to config reg */
static inline void nvt_cr_write(struct nvt_dev *nvt, u8 val, u8 reg)
{
	outb(reg, nvt->cr_efir);
	outb(val, nvt->cr_efdr);
}

/* read val from config reg */
static inline u8 nvt_cr_read(struct nvt_dev *nvt, u8 reg)
{
	outb(reg, nvt->cr_efir);
	return inb(nvt->cr_efdr);
}

/* update config register bit without changing other bits */
static inline void nvt_set_reg_bit(struct nvt_dev *nvt, u8 val, u8 reg)
{
	u8 tmp = nvt_cr_read(nvt, reg) | val;
	nvt_cr_write(nvt, tmp, reg);
}

/* clear config register bit without changing other bits */
static inline void nvt_clear_reg_bit(struct nvt_dev *nvt, u8 val, u8 reg)
{
	u8 tmp = nvt_cr_read(nvt, reg) & ~val;
	nvt_cr_write(nvt, tmp, reg);
}

/* enter extended function mode */
static inline void nvt_efm_enable(struct nvt_dev *nvt)
{
	/* Enabling Extended Function Mode explicitly requires writing 2x */
	outb(EFER_EFM_ENABLE, nvt->cr_efir);
	outb(EFER_EFM_ENABLE, nvt->cr_efir);
}

/* exit extended function mode */
static inline void nvt_efm_disable(struct nvt_dev *nvt)
{
	outb(EFER_EFM_DISABLE, nvt->cr_efir);
}

/*
 * When you want to address a specific logical device, write its logical
 * device number to CR_LOGICAL_DEV_SEL, then enable/disable by writing
 * 0x1/0x0 respectively to CR_LOGICAL_DEV_EN.
 */
static inline void nvt_select_logical_dev(struct nvt_dev *nvt, u8 ldev)
{
	outb(CR_LOGICAL_DEV_SEL, nvt->cr_efir);
	outb(ldev, nvt->cr_efdr);
}

/* write val to cir config register */
static inline void nvt_cir_reg_write(struct nvt_dev *nvt, u8 val, u8 offset)
{
	outb(val, nvt->cir_addr + offset);
}

/* read val from cir config register */
static u8 nvt_cir_reg_read(struct nvt_dev *nvt, u8 offset)
{
	u8 val;

	val = inb(nvt->cir_addr + offset);

	return val;
}

/* write val to cir wake register */
static inline void nvt_cir_wake_reg_write(struct nvt_dev *nvt,
					  u8 val, u8 offset)
{
	outb(val, nvt->cir_wake_addr + offset);
}

/* read val from cir wake config register */
static u8 nvt_cir_wake_reg_read(struct nvt_dev *nvt, u8 offset)
{
	u8 val;

	val = inb(nvt->cir_wake_addr + offset);

	return val;
}

#define pr_reg(text, ...) \
	printk(KERN_INFO KBUILD_MODNAME ": " text, ## __VA_ARGS__)

/* dump current cir register contents */
static void cir_dump_regs(struct nvt_dev *nvt)
{
	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);

	pr_reg("%s: Dump CIR logical device registers:\n", NVT_DRIVER_NAME);
	pr_reg(" * CR CIR ACTIVE :   0x%x\n",
	       nvt_cr_read(nvt, CR_LOGICAL_DEV_EN));
	pr_reg(" * CR CIR BASE ADDR: 0x%x\n",
	       (nvt_cr_read(nvt, CR_CIR_BASE_ADDR_HI) << 8) |
		nvt_cr_read(nvt, CR_CIR_BASE_ADDR_LO));
	pr_reg(" * CR CIR IRQ NUM:   0x%x\n",
	       nvt_cr_read(nvt, CR_CIR_IRQ_RSRC));

	nvt_efm_disable(nvt);

	pr_reg("%s: Dump CIR registers:\n", NVT_DRIVER_NAME);
	pr_reg(" * IRCON:     0x%x\n", nvt_cir_reg_read(nvt, CIR_IRCON));
	pr_reg(" * IRSTS:     0x%x\n", nvt_cir_reg_read(nvt, CIR_IRSTS));
	pr_reg(" * IREN:      0x%x\n", nvt_cir_reg_read(nvt, CIR_IREN));
	pr_reg(" * RXFCONT:   0x%x\n", nvt_cir_reg_read(nvt, CIR_RXFCONT));
	pr_reg(" * CP:        0x%x\n", nvt_cir_reg_read(nvt, CIR_CP));
	pr_reg(" * CC:        0x%x\n", nvt_cir_reg_read(nvt, CIR_CC));
	pr_reg(" * SLCH:      0x%x\n", nvt_cir_reg_read(nvt, CIR_SLCH));
	pr_reg(" * SLCL:      0x%x\n", nvt_cir_reg_read(nvt, CIR_SLCL));
	pr_reg(" * FIFOCON:   0x%x\n", nvt_cir_reg_read(nvt, CIR_FIFOCON));
	pr_reg(" * IRFIFOSTS: 0x%x\n", nvt_cir_reg_read(nvt, CIR_IRFIFOSTS));
	pr_reg(" * SRXFIFO:   0x%x\n", nvt_cir_reg_read(nvt, CIR_SRXFIFO));
	pr_reg(" * TXFCONT:   0x%x\n", nvt_cir_reg_read(nvt, CIR_TXFCONT));
	pr_reg(" * STXFIFO:   0x%x\n", nvt_cir_reg_read(nvt, CIR_STXFIFO));
	pr_reg(" * FCCH:      0x%x\n", nvt_cir_reg_read(nvt, CIR_FCCH));
	pr_reg(" * FCCL:      0x%x\n", nvt_cir_reg_read(nvt, CIR_FCCL));
	pr_reg(" * IRFSM:     0x%x\n", nvt_cir_reg_read(nvt, CIR_IRFSM));
}

/* dump current cir wake register contents */
static void cir_wake_dump_regs(struct nvt_dev *nvt)
{
	u8 i, fifo_len;

	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);

	pr_reg("%s: Dump CIR WAKE logical device registers:\n",
	       NVT_DRIVER_NAME);
	pr_reg(" * CR CIR WAKE ACTIVE :   0x%x\n",
	       nvt_cr_read(nvt, CR_LOGICAL_DEV_EN));
	pr_reg(" * CR CIR WAKE BASE ADDR: 0x%x\n",
	       (nvt_cr_read(nvt, CR_CIR_BASE_ADDR_HI) << 8) |
		nvt_cr_read(nvt, CR_CIR_BASE_ADDR_LO));
	pr_reg(" * CR CIR WAKE IRQ NUM:   0x%x\n",
	       nvt_cr_read(nvt, CR_CIR_IRQ_RSRC));

	nvt_efm_disable(nvt);

	pr_reg("%s: Dump CIR WAKE registers\n", NVT_DRIVER_NAME);
	pr_reg(" * IRCON:          0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRCON));
	pr_reg(" * IRSTS:          0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRSTS));
	pr_reg(" * IREN:           0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_IREN));
	pr_reg(" * FIFO CMP DEEP:  0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_CMP_DEEP));
	pr_reg(" * FIFO CMP TOL:   0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_CMP_TOL));
	pr_reg(" * FIFO COUNT:     0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_COUNT));
	pr_reg(" * SLCH:           0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_SLCH));
	pr_reg(" * SLCL:           0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_SLCL));
	pr_reg(" * FIFOCON:        0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFOCON));
	pr_reg(" * SRXFSTS:        0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_SRXFSTS));
	pr_reg(" * SAMPLE RX FIFO: 0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_SAMPLE_RX_FIFO));
	pr_reg(" * WR FIFO DATA:   0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_WR_FIFO_DATA));
	pr_reg(" * RD FIFO ONLY:   0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY));
	pr_reg(" * RD FIFO ONLY IDX: 0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY_IDX));
	pr_reg(" * FIFO IGNORE:    0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_IGNORE));
	pr_reg(" * IRFSM:          0x%x\n",
	       nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRFSM));

	fifo_len = nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_COUNT);
	pr_reg("%s: Dump CIR WAKE FIFO (len %d)\n", NVT_DRIVER_NAME, fifo_len);
	pr_reg("* Contents = ");
	for (i = 0; i < fifo_len; i++)
		printk(KERN_CONT "%02x ",
		       nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY));
	printk(KERN_CONT "\n");
}

/* detect hardware features */
static int nvt_hw_detect(struct nvt_dev *nvt)
{
	unsigned long flags;
	u8 chip_major, chip_minor;
	int ret = 0;
	char chip_id[12];
	bool chip_unknown = false;

	nvt_efm_enable(nvt);

	/* Check if we're wired for the alternate EFER setup */
	chip_major = nvt_cr_read(nvt, CR_CHIP_ID_HI);
	if (chip_major == 0xff) {
		nvt->cr_efir = CR_EFIR2;
		nvt->cr_efdr = CR_EFDR2;
		nvt_efm_enable(nvt);
		chip_major = nvt_cr_read(nvt, CR_CHIP_ID_HI);
	}

	chip_minor = nvt_cr_read(nvt, CR_CHIP_ID_LO);

	/* these are the known working chip revisions... */
	switch (chip_major) {
	case CHIP_ID_HIGH_667:
		strcpy(chip_id, "w83667hg\0");
		if (chip_minor != CHIP_ID_LOW_667)
			chip_unknown = true;
		break;
	case CHIP_ID_HIGH_677B:
		strcpy(chip_id, "w83677hg\0");
		if (chip_minor != CHIP_ID_LOW_677B2 &&
		    chip_minor != CHIP_ID_LOW_677B3)
			chip_unknown = true;
		break;
	case CHIP_ID_HIGH_677C:
		strcpy(chip_id, "w83677hg-c\0");
		if (chip_minor != CHIP_ID_LOW_677C)
			chip_unknown = true;
		break;
	default:
		strcpy(chip_id, "w836x7hg\0");
		chip_unknown = true;
		break;
	}

	/* warn, but still let the driver load, if we don't know this chip */
	if (chip_unknown)
		nvt_pr(KERN_WARNING, "%s: unknown chip, id: 0x%02x 0x%02x, "
		       "it may not work...", chip_id, chip_major, chip_minor);
	else
		nvt_dbg("%s: chip id: 0x%02x 0x%02x",
			chip_id, chip_major, chip_minor);

	nvt_efm_disable(nvt);

	spin_lock_irqsave(&nvt->nvt_lock, flags);
	nvt->chip_major = chip_major;
	nvt->chip_minor = chip_minor;
	spin_unlock_irqrestore(&nvt->nvt_lock, flags);

	return ret;
}

static void nvt_cir_ldev_init(struct nvt_dev *nvt)
{
	u8 val, psreg, psmask, psval;

	if (nvt->chip_major == CHIP_ID_HIGH_667) {
		psreg = CR_MULTIFUNC_PIN_SEL;
		psmask = MULTIFUNC_PIN_SEL_MASK;
		psval = MULTIFUNC_ENABLE_CIR | MULTIFUNC_ENABLE_CIRWB;
	} else {
		psreg = CR_OUTPUT_PIN_SEL;
		psmask = OUTPUT_PIN_SEL_MASK;
		psval = OUTPUT_ENABLE_CIR | OUTPUT_ENABLE_CIRWB;
	}

	/* output pin selection: enable CIR, with WB sensor enabled */
	val = nvt_cr_read(nvt, psreg);
	val &= psmask;
	val |= psval;
	nvt_cr_write(nvt, val, psreg);

	/* Select CIR logical device and enable */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	nvt_cr_write(nvt, nvt->cir_addr >> 8, CR_CIR_BASE_ADDR_HI);
	nvt_cr_write(nvt, nvt->cir_addr & 0xff, CR_CIR_BASE_ADDR_LO);

	nvt_cr_write(nvt, nvt->cir_irq, CR_CIR_IRQ_RSRC);

	nvt_dbg("CIR initialized, base io port address: 0x%lx, irq: %d",
		nvt->cir_addr, nvt->cir_irq);
}

static void nvt_cir_wake_ldev_init(struct nvt_dev *nvt)
{
	/* Select ACPI logical device, enable it and CIR Wake */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_ACPI);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	/* Enable CIR Wake via PSOUT# (Pin60) */
	nvt_set_reg_bit(nvt, CIR_WAKE_ENABLE_BIT, CR_ACPI_CIR_WAKE);

	/* enable cir interrupt of mouse/keyboard IRQ event */
	nvt_set_reg_bit(nvt, CIR_INTR_MOUSE_IRQ_BIT, CR_ACPI_IRQ_EVENTS);

	/* enable pme interrupt of cir wakeup event */
	nvt_set_reg_bit(nvt, PME_INTR_CIR_PASS_BIT, CR_ACPI_IRQ_EVENTS2);

	/* Select CIR Wake logical device and enable */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	nvt_cr_write(nvt, nvt->cir_wake_addr >> 8, CR_CIR_BASE_ADDR_HI);
	nvt_cr_write(nvt, nvt->cir_wake_addr & 0xff, CR_CIR_BASE_ADDR_LO);

	nvt_cr_write(nvt, nvt->cir_wake_irq, CR_CIR_IRQ_RSRC);

	nvt_dbg("CIR Wake initialized, base io port address: 0x%lx, irq: %d",
		nvt->cir_wake_addr, nvt->cir_wake_irq);
}

/* clear out the hardware's cir rx fifo */
static void nvt_clear_cir_fifo(struct nvt_dev *nvt)
{
	u8 val;

	val = nvt_cir_reg_read(nvt, CIR_FIFOCON);
	nvt_cir_reg_write(nvt, val | CIR_FIFOCON_RXFIFOCLR, CIR_FIFOCON);
}

/* clear out the hardware's cir wake rx fifo */
static void nvt_clear_cir_wake_fifo(struct nvt_dev *nvt)
{
	u8 val;

	val = nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFOCON);
	nvt_cir_wake_reg_write(nvt, val | CIR_WAKE_FIFOCON_RXFIFOCLR,
			       CIR_WAKE_FIFOCON);
}

/* clear out the hardware's cir tx fifo */
static void nvt_clear_tx_fifo(struct nvt_dev *nvt)
{
	u8 val;

	val = nvt_cir_reg_read(nvt, CIR_FIFOCON);
	nvt_cir_reg_write(nvt, val | CIR_FIFOCON_TXFIFOCLR, CIR_FIFOCON);
}

/* enable RX Trigger Level Reach and Packet End interrupts */
static void nvt_set_cir_iren(struct nvt_dev *nvt)
{
	u8 iren;

	iren = CIR_IREN_RTR | CIR_IREN_PE;
	nvt_cir_reg_write(nvt, iren, CIR_IREN);
}

static void nvt_cir_regs_init(struct nvt_dev *nvt)
{
	/* set sample limit count (PE interrupt raised when reached) */
	nvt_cir_reg_write(nvt, CIR_RX_LIMIT_COUNT >> 8, CIR_SLCH);
	nvt_cir_reg_write(nvt, CIR_RX_LIMIT_COUNT & 0xff, CIR_SLCL);

	/* set fifo irq trigger levels */
	nvt_cir_reg_write(nvt, CIR_FIFOCON_TX_TRIGGER_LEV |
			  CIR_FIFOCON_RX_TRIGGER_LEV, CIR_FIFOCON);

	/*
	 * Enable TX and RX, specify carrier on = low, off = high, and set
	 * sample period (currently 50us)
	 */
	nvt_cir_reg_write(nvt,
			  CIR_IRCON_TXEN | CIR_IRCON_RXEN |
			  CIR_IRCON_RXINV | CIR_IRCON_SAMPLE_PERIOD_SEL,
			  CIR_IRCON);

	/* clear hardware rx and tx fifos */
	nvt_clear_cir_fifo(nvt);
	nvt_clear_tx_fifo(nvt);

	/* clear any and all stray interrupts */
	nvt_cir_reg_write(nvt, 0xff, CIR_IRSTS);

	/* and finally, enable interrupts */
	nvt_set_cir_iren(nvt);
}

static void nvt_cir_wake_regs_init(struct nvt_dev *nvt)
{
	/* set number of bytes needed for wake from s3 (default 65) */
	nvt_cir_wake_reg_write(nvt, CIR_WAKE_FIFO_CMP_BYTES,
			       CIR_WAKE_FIFO_CMP_DEEP);

	/* set tolerance/variance allowed per byte during wake compare */
	nvt_cir_wake_reg_write(nvt, CIR_WAKE_CMP_TOLERANCE,
			       CIR_WAKE_FIFO_CMP_TOL);

	/* set sample limit count (PE interrupt raised when reached) */
	nvt_cir_wake_reg_write(nvt, CIR_RX_LIMIT_COUNT >> 8, CIR_WAKE_SLCH);
	nvt_cir_wake_reg_write(nvt, CIR_RX_LIMIT_COUNT & 0xff, CIR_WAKE_SLCL);

	/* set cir wake fifo rx trigger level (currently 67) */
	nvt_cir_wake_reg_write(nvt, CIR_WAKE_FIFOCON_RX_TRIGGER_LEV,
			       CIR_WAKE_FIFOCON);

	/*
	 * Enable TX and RX, specific carrier on = low, off = high, and set
	 * sample period (currently 50us)
	 */
	nvt_cir_wake_reg_write(nvt, CIR_WAKE_IRCON_MODE0 | CIR_WAKE_IRCON_RXEN |
			       CIR_WAKE_IRCON_R | CIR_WAKE_IRCON_RXINV |
			       CIR_WAKE_IRCON_SAMPLE_PERIOD_SEL,
			       CIR_WAKE_IRCON);

	/* clear cir wake rx fifo */
	nvt_clear_cir_wake_fifo(nvt);

	/* clear any and all stray interrupts */
	nvt_cir_wake_reg_write(nvt, 0xff, CIR_WAKE_IRSTS);
}

static void nvt_enable_wake(struct nvt_dev *nvt)
{
	nvt_efm_enable(nvt);

	nvt_select_logical_dev(nvt, LOGICAL_DEV_ACPI);
	nvt_set_reg_bit(nvt, CIR_WAKE_ENABLE_BIT, CR_ACPI_CIR_WAKE);
	nvt_set_reg_bit(nvt, CIR_INTR_MOUSE_IRQ_BIT, CR_ACPI_IRQ_EVENTS);
	nvt_set_reg_bit(nvt, PME_INTR_CIR_PASS_BIT, CR_ACPI_IRQ_EVENTS2);

	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	nvt_efm_disable(nvt);

	nvt_cir_wake_reg_write(nvt, CIR_WAKE_IRCON_MODE0 | CIR_WAKE_IRCON_RXEN |
			       CIR_WAKE_IRCON_R | CIR_WAKE_IRCON_RXINV |
			       CIR_WAKE_IRCON_SAMPLE_PERIOD_SEL,
			       CIR_WAKE_IRCON);
	nvt_cir_wake_reg_write(nvt, 0xff, CIR_WAKE_IRSTS);
	nvt_cir_wake_reg_write(nvt, 0, CIR_WAKE_IREN);
}

/* rx carrier detect only works in learning mode, must be called w/nvt_lock */
static u32 nvt_rx_carrier_detect(struct nvt_dev *nvt)
{
	u32 count, carrier, duration = 0;
	int i;

	count = nvt_cir_reg_read(nvt, CIR_FCCL) |
		nvt_cir_reg_read(nvt, CIR_FCCH) << 8;

	for (i = 0; i < nvt->pkts; i++) {
		if (nvt->buf[i] & BUF_PULSE_BIT)
			duration += nvt->buf[i] & BUF_LEN_MASK;
	}

	duration *= SAMPLE_PERIOD;

	if (!count || !duration) {
		nvt_pr(KERN_NOTICE, "Unable to determine carrier! (c:%u, d:%u)",
		       count, duration);
		return 0;
	}

	carrier = MS_TO_NS(count) / duration;

	if ((carrier > MAX_CARRIER) || (carrier < MIN_CARRIER))
		nvt_dbg("WTF? Carrier frequency out of range!");

	nvt_dbg("Carrier frequency: %u (count %u, duration %u)",
		carrier, count, duration);

	return carrier;
}

/*
 * set carrier frequency
 *
 * set carrier on 2 registers: CP & CC
 * always set CP as 0x81
 * set CC by SPEC, CC = 3MHz/carrier - 1
 */
static int nvt_set_tx_carrier(struct rc_dev *dev, u32 carrier)
{
	struct nvt_dev *nvt = dev->priv;
	u16 val;

	nvt_cir_reg_write(nvt, 1, CIR_CP);
	val = 3000000 / (carrier) - 1;
	nvt_cir_reg_write(nvt, val & 0xff, CIR_CC);

	nvt_dbg("cp: 0x%x cc: 0x%x\n",
		nvt_cir_reg_read(nvt, CIR_CP), nvt_cir_reg_read(nvt, CIR_CC));

	return 0;
}

/*
 * nvt_tx_ir
 *
 * 1) clean TX fifo first (handled by AP)
 * 2) copy data from user space
 * 3) disable RX interrupts, enable TX interrupts: TTR & TFU
 * 4) send 9 packets to TX FIFO to open TTR
 * in interrupt_handler:
 * 5) send all data out
 * go back to write():
 * 6) disable TX interrupts, re-enable RX interupts
 *
 * The key problem of this function is user space data may larger than
 * driver's data buf length. So nvt_tx_ir() will only copy TX_BUF_LEN data to
 * buf, and keep current copied data buf num in cur_buf_num. But driver's buf
 * number may larger than TXFCONT (0xff). So in interrupt_handler, it has to
 * set TXFCONT as 0xff, until buf_count less than 0xff.
 */
static int nvt_tx_ir(struct rc_dev *dev, int *txbuf, u32 n)
{
	struct nvt_dev *nvt = dev->priv;
	unsigned long flags;
	size_t cur_count;
	unsigned int i;
	u8 iren;
	int ret;

	spin_lock_irqsave(&nvt->tx.lock, flags);

	if (n >= TX_BUF_LEN) {
		nvt->tx.buf_count = cur_count = TX_BUF_LEN;
		ret = TX_BUF_LEN;
	} else {
		nvt->tx.buf_count = cur_count = n;
		ret = n;
	}

	memcpy(nvt->tx.buf, txbuf, nvt->tx.buf_count);

	nvt->tx.cur_buf_num = 0;

	/* save currently enabled interrupts */
	iren = nvt_cir_reg_read(nvt, CIR_IREN);

	/* now disable all interrupts, save TFU & TTR */
	nvt_cir_reg_write(nvt, CIR_IREN_TFU | CIR_IREN_TTR, CIR_IREN);

	nvt->tx.tx_state = ST_TX_REPLY;

	nvt_cir_reg_write(nvt, CIR_FIFOCON_TX_TRIGGER_LEV_8 |
			  CIR_FIFOCON_RXFIFOCLR, CIR_FIFOCON);

	/* trigger TTR interrupt by writing out ones, (yes, it's ugly) */
	for (i = 0; i < 9; i++)
		nvt_cir_reg_write(nvt, 0x01, CIR_STXFIFO);

	spin_unlock_irqrestore(&nvt->tx.lock, flags);

	wait_event(nvt->tx.queue, nvt->tx.tx_state == ST_TX_REQUEST);

	spin_lock_irqsave(&nvt->tx.lock, flags);
	nvt->tx.tx_state = ST_TX_NONE;
	spin_unlock_irqrestore(&nvt->tx.lock, flags);

	/* restore enabled interrupts to prior state */
	nvt_cir_reg_write(nvt, iren, CIR_IREN);

	return ret;
}

/* dump contents of the last rx buffer we got from the hw rx fifo */
static void nvt_dump_rx_buf(struct nvt_dev *nvt)
{
	int i;

	printk(KERN_DEBUG "%s (len %d): ", __func__, nvt->pkts);
	for (i = 0; (i < nvt->pkts) && (i < RX_BUF_LEN); i++)
		printk(KERN_CONT "0x%02x ", nvt->buf[i]);
	printk(KERN_CONT "\n");
}

/*
 * Process raw data in rx driver buffer, store it in raw IR event kfifo,
 * trigger decode when appropriate.
 *
 * We get IR data samples one byte at a time. If the msb is set, its a pulse,
 * otherwise its a space. The lower 7 bits are the count of SAMPLE_PERIOD
 * (default 50us) intervals for that pulse/space. A discrete signal is
 * followed by a series of 0x7f packets, then either 0x7<something> or 0x80
 * to signal more IR coming (repeats) or end of IR, respectively. We store
 * sample data in the raw event kfifo until we see 0x7<something> (except f)
 * or 0x80, at which time, we trigger a decode operation.
 */
static void nvt_process_rx_ir_data(struct nvt_dev *nvt)
{
	DEFINE_IR_RAW_EVENT(rawir);
	u32 carrier;
	u8 sample;
	int i;

	nvt_dbg_verbose("%s firing", __func__);

	if (debug)
		nvt_dump_rx_buf(nvt);

	if (nvt->carrier_detect_enabled)
		carrier = nvt_rx_carrier_detect(nvt);

	nvt_dbg_verbose("Processing buffer of len %d", nvt->pkts);

	init_ir_raw_event(&rawir);

	for (i = 0; i < nvt->pkts; i++) {
		sample = nvt->buf[i];

		rawir.pulse = ((sample & BUF_PULSE_BIT) != 0);
		rawir.duration = US_TO_NS((sample & BUF_LEN_MASK)
					  * SAMPLE_PERIOD);

		nvt_dbg("Storing %s with duration %d",
			rawir.pulse ? "pulse" : "space", rawir.duration);

		ir_raw_event_store_with_filter(nvt->rdev, &rawir);

		/*
		 * BUF_PULSE_BIT indicates end of IR data, BUF_REPEAT_BYTE
		 * indicates end of IR signal, but new data incoming. In both
		 * cases, it means we're ready to call ir_raw_event_handle
		 */
		if ((sample == BUF_PULSE_BIT) && (i + 1 < nvt->pkts)) {
			nvt_dbg("Calling ir_raw_event_handle (signal end)\n");
			ir_raw_event_handle(nvt->rdev);
		}
	}

	nvt->pkts = 0;

	nvt_dbg("Calling ir_raw_event_handle (buffer empty)\n");
	ir_raw_event_handle(nvt->rdev);

	nvt_dbg_verbose("%s done", __func__);
}

static void nvt_handle_rx_fifo_overrun(struct nvt_dev *nvt)
{
	nvt_pr(KERN_WARNING, "RX FIFO overrun detected, flushing data!");

	nvt->pkts = 0;
	nvt_clear_cir_fifo(nvt);
	ir_raw_event_reset(nvt->rdev);
}

/* copy data from hardware rx fifo into driver buffer */
static void nvt_get_rx_ir_data(struct nvt_dev *nvt)
{
	unsigned long flags;
	u8 fifocount, val;
	unsigned int b_idx;
	bool overrun = false;
	int i;

	/* Get count of how many bytes to read from RX FIFO */
	fifocount = nvt_cir_reg_read(nvt, CIR_RXFCONT);
	/* if we get 0xff, probably means the logical dev is disabled */
	if (fifocount == 0xff)
		return;
	/* watch out for a fifo overrun condition */
	else if (fifocount > RX_BUF_LEN) {
		overrun = true;
		fifocount = RX_BUF_LEN;
	}

	nvt_dbg("attempting to fetch %u bytes from hw rx fifo", fifocount);

	spin_lock_irqsave(&nvt->nvt_lock, flags);

	b_idx = nvt->pkts;

	/* This should never happen, but lets check anyway... */
	if (b_idx + fifocount > RX_BUF_LEN) {
		nvt_process_rx_ir_data(nvt);
		b_idx = 0;
	}

	/* Read fifocount bytes from CIR Sample RX FIFO register */
	for (i = 0; i < fifocount; i++) {
		val = nvt_cir_reg_read(nvt, CIR_SRXFIFO);
		nvt->buf[b_idx + i] = val;
	}

	nvt->pkts += fifocount;
	nvt_dbg("%s: pkts now %d", __func__, nvt->pkts);

	nvt_process_rx_ir_data(nvt);

	if (overrun)
		nvt_handle_rx_fifo_overrun(nvt);

	spin_unlock_irqrestore(&nvt->nvt_lock, flags);
}

static void nvt_cir_log_irqs(u8 status, u8 iren)
{
	nvt_pr(KERN_INFO, "IRQ 0x%02x (IREN 0x%02x) :%s%s%s%s%s%s%s%s%s",
		status, iren,
		status & CIR_IRSTS_RDR	? " RDR"	: "",
		status & CIR_IRSTS_RTR	? " RTR"	: "",
		status & CIR_IRSTS_PE	? " PE"		: "",
		status & CIR_IRSTS_RFO	? " RFO"	: "",
		status & CIR_IRSTS_TE	? " TE"		: "",
		status & CIR_IRSTS_TTR	? " TTR"	: "",
		status & CIR_IRSTS_TFU	? " TFU"	: "",
		status & CIR_IRSTS_GH	? " GH"		: "",
		status & ~(CIR_IRSTS_RDR | CIR_IRSTS_RTR | CIR_IRSTS_PE |
			   CIR_IRSTS_RFO | CIR_IRSTS_TE | CIR_IRSTS_TTR |
			   CIR_IRSTS_TFU | CIR_IRSTS_GH) ? " ?" : "");
}

static bool nvt_cir_tx_inactive(struct nvt_dev *nvt)
{
	unsigned long flags;
	bool tx_inactive;
	u8 tx_state;

	spin_lock_irqsave(&nvt->tx.lock, flags);
	tx_state = nvt->tx.tx_state;
	spin_unlock_irqrestore(&nvt->tx.lock, flags);

	tx_inactive = (tx_state == ST_TX_NONE);

	return tx_inactive;
}

/* interrupt service routine for incoming and outgoing CIR data */
static irqreturn_t nvt_cir_isr(int irq, void *data)
{
	struct nvt_dev *nvt = data;
	u8 status, iren, cur_state;
	unsigned long flags;

	nvt_dbg_verbose("%s firing", __func__);

	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);
	nvt_efm_disable(nvt);

	/*
	 * Get IR Status register contents. Write 1 to ack/clear
	 *
	 * bit: reg name      - description
	 *   7: CIR_IRSTS_RDR - RX Data Ready
	 *   6: CIR_IRSTS_RTR - RX FIFO Trigger Level Reach
	 *   5: CIR_IRSTS_PE  - Packet End
	 *   4: CIR_IRSTS_RFO - RX FIFO Overrun (RDR will also be set)
	 *   3: CIR_IRSTS_TE  - TX FIFO Empty
	 *   2: CIR_IRSTS_TTR - TX FIFO Trigger Level Reach
	 *   1: CIR_IRSTS_TFU - TX FIFO Underrun
	 *   0: CIR_IRSTS_GH  - Min Length Detected
	 */
	status = nvt_cir_reg_read(nvt, CIR_IRSTS);
	if (!status) {
		nvt_dbg_verbose("%s exiting, IRSTS 0x0", __func__);
		nvt_cir_reg_write(nvt, 0xff, CIR_IRSTS);
		return IRQ_RETVAL(IRQ_NONE);
	}

	/* ack/clear all irq flags we've got */
	nvt_cir_reg_write(nvt, status, CIR_IRSTS);
	nvt_cir_reg_write(nvt, 0, CIR_IRSTS);

	/* Interrupt may be shared with CIR Wake, bail if CIR not enabled */
	iren = nvt_cir_reg_read(nvt, CIR_IREN);
	if (!iren) {
		nvt_dbg_verbose("%s exiting, CIR not enabled", __func__);
		return IRQ_RETVAL(IRQ_NONE);
	}

	if (debug)
		nvt_cir_log_irqs(status, iren);

	if (status & CIR_IRSTS_RTR) {
		/* FIXME: add code for study/learn mode */
		/* We only do rx if not tx'ing */
		if (nvt_cir_tx_inactive(nvt))
			nvt_get_rx_ir_data(nvt);
	}

	if (status & CIR_IRSTS_PE) {
		if (nvt_cir_tx_inactive(nvt))
			nvt_get_rx_ir_data(nvt);

		spin_lock_irqsave(&nvt->nvt_lock, flags);

		cur_state = nvt->study_state;

		spin_unlock_irqrestore(&nvt->nvt_lock, flags);

		if (cur_state == ST_STUDY_NONE)
			nvt_clear_cir_fifo(nvt);
	}

	if (status & CIR_IRSTS_TE)
		nvt_clear_tx_fifo(nvt);

	if (status & CIR_IRSTS_TTR) {
		unsigned int pos, count;
		u8 tmp;

		spin_lock_irqsave(&nvt->tx.lock, flags);

		pos = nvt->tx.cur_buf_num;
		count = nvt->tx.buf_count;

		/* Write data into the hardware tx fifo while pos < count */
		if (pos < count) {
			nvt_cir_reg_write(nvt, nvt->tx.buf[pos], CIR_STXFIFO);
			nvt->tx.cur_buf_num++;
		/* Disable TX FIFO Trigger Level Reach (TTR) interrupt */
		} else {
			tmp = nvt_cir_reg_read(nvt, CIR_IREN);
			nvt_cir_reg_write(nvt, tmp & ~CIR_IREN_TTR, CIR_IREN);
		}

		spin_unlock_irqrestore(&nvt->tx.lock, flags);

	}

	if (status & CIR_IRSTS_TFU) {
		spin_lock_irqsave(&nvt->tx.lock, flags);
		if (nvt->tx.tx_state == ST_TX_REPLY) {
			nvt->tx.tx_state = ST_TX_REQUEST;
			wake_up(&nvt->tx.queue);
		}
		spin_unlock_irqrestore(&nvt->tx.lock, flags);
	}

	nvt_dbg_verbose("%s done", __func__);
	return IRQ_RETVAL(IRQ_HANDLED);
}

/* Interrupt service routine for CIR Wake */
static irqreturn_t nvt_cir_wake_isr(int irq, void *data)
{
	u8 status, iren, val;
	struct nvt_dev *nvt = data;
	unsigned long flags;

	nvt_dbg_wake("%s firing", __func__);

	status = nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRSTS);
	if (!status)
		return IRQ_RETVAL(IRQ_NONE);

	if (status & CIR_WAKE_IRSTS_IR_PENDING)
		nvt_clear_cir_wake_fifo(nvt);

	nvt_cir_wake_reg_write(nvt, status, CIR_WAKE_IRSTS);
	nvt_cir_wake_reg_write(nvt, 0, CIR_WAKE_IRSTS);

	/* Interrupt may be shared with CIR, bail if Wake not enabled */
	iren = nvt_cir_wake_reg_read(nvt, CIR_WAKE_IREN);
	if (!iren) {
		nvt_dbg_wake("%s exiting, wake not enabled", __func__);
		return IRQ_RETVAL(IRQ_HANDLED);
	}

	if ((status & CIR_WAKE_IRSTS_PE) &&
	    (nvt->wake_state == ST_WAKE_START)) {
		while (nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY_IDX)) {
			val = nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY);
			nvt_dbg("setting wake up key: 0x%x", val);
		}

		nvt_cir_wake_reg_write(nvt, 0, CIR_WAKE_IREN);
		spin_lock_irqsave(&nvt->nvt_lock, flags);
		nvt->wake_state = ST_WAKE_FINISH;
		spin_unlock_irqrestore(&nvt->nvt_lock, flags);
	}

	nvt_dbg_wake("%s done", __func__);
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void nvt_enable_cir(struct nvt_dev *nvt)
{
	/* set function enable flags */
	nvt_cir_reg_write(nvt, CIR_IRCON_TXEN | CIR_IRCON_RXEN |
			  CIR_IRCON_RXINV | CIR_IRCON_SAMPLE_PERIOD_SEL,
			  CIR_IRCON);

	nvt_efm_enable(nvt);

	/* enable the CIR logical device */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	nvt_efm_disable(nvt);

	/* clear all pending interrupts */
	nvt_cir_reg_write(nvt, 0xff, CIR_IRSTS);

	/* enable interrupts */
	nvt_set_cir_iren(nvt);
}

static void nvt_disable_cir(struct nvt_dev *nvt)
{
	/* disable CIR interrupts */
	nvt_cir_reg_write(nvt, 0, CIR_IREN);

	/* clear any and all pending interrupts */
	nvt_cir_reg_write(nvt, 0xff, CIR_IRSTS);

	/* clear all function enable flags */
	nvt_cir_reg_write(nvt, 0, CIR_IRCON);

	/* clear hardware rx and tx fifos */
	nvt_clear_cir_fifo(nvt);
	nvt_clear_tx_fifo(nvt);

	nvt_efm_enable(nvt);

	/* disable the CIR logical device */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);
	nvt_cr_write(nvt, LOGICAL_DEV_DISABLE, CR_LOGICAL_DEV_EN);

	nvt_efm_disable(nvt);
}

static int nvt_open(struct rc_dev *dev)
{
	struct nvt_dev *nvt = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&nvt->nvt_lock, flags);
	nvt_enable_cir(nvt);
	spin_unlock_irqrestore(&nvt->nvt_lock, flags);

	return 0;
}

static void nvt_close(struct rc_dev *dev)
{
	struct nvt_dev *nvt = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&nvt->nvt_lock, flags);
	nvt_disable_cir(nvt);
	spin_unlock_irqrestore(&nvt->nvt_lock, flags);
}

/* Allocate memory, probe hardware, and initialize everything */
static int nvt_probe(struct pnp_dev *pdev, const struct pnp_device_id *dev_id)
{
	struct nvt_dev *nvt;
	struct rc_dev *rdev;
	int ret = -ENOMEM;

	nvt = kzalloc(sizeof(struct nvt_dev), GFP_KERNEL);
	if (!nvt)
		return ret;

	/* input device for IR remote (and tx) */
	rdev = rc_allocate_device();
	if (!rdev)
		goto failure;

	ret = -ENODEV;
	/* validate pnp resources */
	if (!pnp_port_valid(pdev, 0) ||
	    pnp_port_len(pdev, 0) < CIR_IOREG_LENGTH) {
		dev_err(&pdev->dev, "IR PNP Port not valid!\n");
		goto failure;
	}

	if (!pnp_irq_valid(pdev, 0)) {
		dev_err(&pdev->dev, "PNP IRQ not valid!\n");
		goto failure;
	}

	if (!pnp_port_valid(pdev, 1) ||
	    pnp_port_len(pdev, 1) < CIR_IOREG_LENGTH) {
		dev_err(&pdev->dev, "Wake PNP Port not valid!\n");
		goto failure;
	}

	nvt->cir_addr = pnp_port_start(pdev, 0);
	nvt->cir_irq  = pnp_irq(pdev, 0);

	nvt->cir_wake_addr = pnp_port_start(pdev, 1);
	/* irq is always shared between cir and cir wake */
	nvt->cir_wake_irq  = nvt->cir_irq;

	nvt->cr_efir = CR_EFIR;
	nvt->cr_efdr = CR_EFDR;

	spin_lock_init(&nvt->nvt_lock);
	spin_lock_init(&nvt->tx.lock);

	pnp_set_drvdata(pdev, nvt);
	nvt->pdev = pdev;

	init_waitqueue_head(&nvt->tx.queue);

	ret = nvt_hw_detect(nvt);
	if (ret)
		goto failure;

	/* Initialize CIR & CIR Wake Logical Devices */
	nvt_efm_enable(nvt);
	nvt_cir_ldev_init(nvt);
	nvt_cir_wake_ldev_init(nvt);
	nvt_efm_disable(nvt);

	/* Initialize CIR & CIR Wake Config Registers */
	nvt_cir_regs_init(nvt);
	nvt_cir_wake_regs_init(nvt);

	/* Set up the rc device */
	rdev->priv = nvt;
	rdev->driver_type = RC_DRIVER_IR_RAW;
	rdev->allowed_protos = RC_TYPE_ALL;
	rdev->open = nvt_open;
	rdev->close = nvt_close;
	rdev->tx_ir = nvt_tx_ir;
	rdev->s_tx_carrier = nvt_set_tx_carrier;
	rdev->input_name = "Nuvoton w836x7hg Infrared Remote Transceiver";
	rdev->input_phys = "nuvoton/cir0";
	rdev->input_id.bustype = BUS_HOST;
	rdev->input_id.vendor = PCI_VENDOR_ID_WINBOND2;
	rdev->input_id.product = nvt->chip_major;
	rdev->input_id.version = nvt->chip_minor;
	rdev->dev.parent = &pdev->dev;
	rdev->driver_name = NVT_DRIVER_NAME;
	rdev->map_name = RC_MAP_RC6_MCE;
	rdev->timeout = MS_TO_NS(100);
	/* rx resolution is hardwired to 50us atm, 1, 25, 100 also possible */
	rdev->rx_resolution = US_TO_NS(CIR_SAMPLE_PERIOD);
#if 0
	rdev->min_timeout = XYZ;
	rdev->max_timeout = XYZ;
	/* tx bits */
	rdev->tx_resolution = XYZ;
#endif

	ret = -EBUSY;
	/* now claim resources */
	if (!request_region(nvt->cir_addr,
			    CIR_IOREG_LENGTH, NVT_DRIVER_NAME))
		goto failure;

	if (request_irq(nvt->cir_irq, nvt_cir_isr, IRQF_SHARED,
			NVT_DRIVER_NAME, (void *)nvt))
		goto failure;

	if (!request_region(nvt->cir_wake_addr,
			    CIR_IOREG_LENGTH, NVT_DRIVER_NAME))
		goto failure;

	if (request_irq(nvt->cir_wake_irq, nvt_cir_wake_isr, IRQF_SHARED,
			NVT_DRIVER_NAME, (void *)nvt))
		goto failure;

	ret = rc_register_device(rdev);
	if (ret)
		goto failure;

	device_init_wakeup(&pdev->dev, true);
	nvt->rdev = rdev;
	nvt_pr(KERN_NOTICE, "driver has been successfully loaded\n");
	if (debug) {
		cir_dump_regs(nvt);
		cir_wake_dump_regs(nvt);
	}

	return 0;

failure:
	if (nvt->cir_irq)
		free_irq(nvt->cir_irq, nvt);
	if (nvt->cir_addr)
		release_region(nvt->cir_addr, CIR_IOREG_LENGTH);

	if (nvt->cir_wake_irq)
		free_irq(nvt->cir_wake_irq, nvt);
	if (nvt->cir_wake_addr)
		release_region(nvt->cir_wake_addr, CIR_IOREG_LENGTH);

	rc_free_device(rdev);
	kfree(nvt);

	return ret;
}

static void __devexit nvt_remove(struct pnp_dev *pdev)
{
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&nvt->nvt_lock, flags);
	/* disable CIR */
	nvt_cir_reg_write(nvt, 0, CIR_IREN);
	nvt_disable_cir(nvt);
	/* enable CIR Wake (for IR power-on) */
	nvt_enable_wake(nvt);
	spin_unlock_irqrestore(&nvt->nvt_lock, flags);

	/* free resources */
	free_irq(nvt->cir_irq, nvt);
	free_irq(nvt->cir_wake_irq, nvt);
	release_region(nvt->cir_addr, CIR_IOREG_LENGTH);
	release_region(nvt->cir_wake_addr, CIR_IOREG_LENGTH);

	rc_unregister_device(nvt->rdev);

	kfree(nvt);
}

static int nvt_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);
	unsigned long flags;

	nvt_dbg("%s called", __func__);

	/* zero out misc state tracking */
	spin_lock_irqsave(&nvt->nvt_lock, flags);
	nvt->study_state = ST_STUDY_NONE;
	nvt->wake_state = ST_WAKE_NONE;
	spin_unlock_irqrestore(&nvt->nvt_lock, flags);

	spin_lock_irqsave(&nvt->tx.lock, flags);
	nvt->tx.tx_state = ST_TX_NONE;
	spin_unlock_irqrestore(&nvt->tx.lock, flags);

	/* disable all CIR interrupts */
	nvt_cir_reg_write(nvt, 0, CIR_IREN);

	nvt_efm_enable(nvt);

	/* disable cir logical dev */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);
	nvt_cr_write(nvt, LOGICAL_DEV_DISABLE, CR_LOGICAL_DEV_EN);

	nvt_efm_disable(nvt);

	/* make sure wake is enabled */
	nvt_enable_wake(nvt);

	return 0;
}

static int nvt_resume(struct pnp_dev *pdev)
{
	int ret = 0;
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);

	nvt_dbg("%s called", __func__);

	/* open interrupt */
	nvt_set_cir_iren(nvt);

	/* Enable CIR logical device */
	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	nvt_efm_disable(nvt);

	nvt_cir_regs_init(nvt);
	nvt_cir_wake_regs_init(nvt);

	return ret;
}

static void nvt_shutdown(struct pnp_dev *pdev)
{
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);
	nvt_enable_wake(nvt);
}

static const struct pnp_device_id nvt_ids[] = {
	{ "WEC0530", 0 },   /* CIR */
	{ "NTN0530", 0 },   /* CIR for new chip's pnp id*/
	{ "", 0 },
};

static struct pnp_driver nvt_driver = {
	.name		= NVT_DRIVER_NAME,
	.id_table	= nvt_ids,
	.flags		= PNP_DRIVER_RES_DO_NOT_CHANGE,
	.probe		= nvt_probe,
	.remove		= __devexit_p(nvt_remove),
	.suspend	= nvt_suspend,
	.resume		= nvt_resume,
	.shutdown	= nvt_shutdown,
};

int nvt_init(void)
{
	return pnp_register_driver(&nvt_driver);
}

void nvt_exit(void)
{
	pnp_unregister_driver(&nvt_driver);
}

module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging output");

MODULE_DEVICE_TABLE(pnp, nvt_ids);
MODULE_DESCRIPTION("Nuvoton W83667HG-A & W83677HG-I CIR driver");

MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
MODULE_LICENSE("GPL");

module_init(nvt_init);
module_exit(nvt_exit);
