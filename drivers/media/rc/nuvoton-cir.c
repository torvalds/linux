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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

static void nvt_clear_cir_wake_fifo(struct nvt_dev *nvt);

static const struct nvt_chip nvt_chips[] = {
	{ "w83667hg", NVT_W83667HG },
	{ "NCT6775F", NVT_6775F },
	{ "NCT6776F", NVT_6776F },
	{ "NCT6779D", NVT_6779D },
};

static inline struct device *nvt_get_dev(const struct nvt_dev *nvt)
{
	return nvt->rdev->dev.parent;
}

static inline bool is_w83667hg(struct nvt_dev *nvt)
{
	return nvt->chip_ver == NVT_W83667HG;
}

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
static inline int nvt_efm_enable(struct nvt_dev *nvt)
{
	if (!request_muxed_region(nvt->cr_efir, 2, NVT_DRIVER_NAME))
		return -EBUSY;

	/* Enabling Extended Function Mode explicitly requires writing 2x */
	outb(EFER_EFM_ENABLE, nvt->cr_efir);
	outb(EFER_EFM_ENABLE, nvt->cr_efir);

	return 0;
}

/* exit extended function mode */
static inline void nvt_efm_disable(struct nvt_dev *nvt)
{
	outb(EFER_EFM_DISABLE, nvt->cr_efir);

	release_region(nvt->cr_efir, 2);
}

/*
 * When you want to address a specific logical device, write its logical
 * device number to CR_LOGICAL_DEV_SEL, then enable/disable by writing
 * 0x1/0x0 respectively to CR_LOGICAL_DEV_EN.
 */
static inline void nvt_select_logical_dev(struct nvt_dev *nvt, u8 ldev)
{
	nvt_cr_write(nvt, ldev, CR_LOGICAL_DEV_SEL);
}

/* select and enable logical device with setting EFM mode*/
static inline void nvt_enable_logical_dev(struct nvt_dev *nvt, u8 ldev)
{
	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, ldev);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);
	nvt_efm_disable(nvt);
}

/* select and disable logical device with setting EFM mode*/
static inline void nvt_disable_logical_dev(struct nvt_dev *nvt, u8 ldev)
{
	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, ldev);
	nvt_cr_write(nvt, LOGICAL_DEV_DISABLE, CR_LOGICAL_DEV_EN);
	nvt_efm_disable(nvt);
}

/* write val to cir config register */
static inline void nvt_cir_reg_write(struct nvt_dev *nvt, u8 val, u8 offset)
{
	outb(val, nvt->cir_addr + offset);
}

/* read val from cir config register */
static u8 nvt_cir_reg_read(struct nvt_dev *nvt, u8 offset)
{
	return inb(nvt->cir_addr + offset);
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
	return inb(nvt->cir_wake_addr + offset);
}

/* don't override io address if one is set already */
static void nvt_set_ioaddr(struct nvt_dev *nvt, unsigned long *ioaddr)
{
	unsigned long old_addr;

	old_addr = nvt_cr_read(nvt, CR_CIR_BASE_ADDR_HI) << 8;
	old_addr |= nvt_cr_read(nvt, CR_CIR_BASE_ADDR_LO);

	if (old_addr)
		*ioaddr = old_addr;
	else {
		nvt_cr_write(nvt, *ioaddr >> 8, CR_CIR_BASE_ADDR_HI);
		nvt_cr_write(nvt, *ioaddr & 0xff, CR_CIR_BASE_ADDR_LO);
	}
}

static void nvt_write_wakeup_codes(struct rc_dev *dev,
				   const u8 *wbuf, int count)
{
	u8 tolerance, config;
	struct nvt_dev *nvt = dev->priv;
	int i;

	/* hardcode the tolerance to 10% */
	tolerance = DIV_ROUND_UP(count, 10);

	spin_lock(&nvt->lock);

	nvt_clear_cir_wake_fifo(nvt);
	nvt_cir_wake_reg_write(nvt, count, CIR_WAKE_FIFO_CMP_DEEP);
	nvt_cir_wake_reg_write(nvt, tolerance, CIR_WAKE_FIFO_CMP_TOL);

	config = nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRCON);

	/* enable writes to wake fifo */
	nvt_cir_wake_reg_write(nvt, config | CIR_WAKE_IRCON_MODE1,
			       CIR_WAKE_IRCON);

	if (count)
		pr_info("Wake samples (%d) =", count);
	else
		pr_info("Wake sample fifo cleared");

	for (i = 0; i < count; i++)
		nvt_cir_wake_reg_write(nvt, wbuf[i], CIR_WAKE_WR_FIFO_DATA);

	nvt_cir_wake_reg_write(nvt, config, CIR_WAKE_IRCON);

	spin_unlock(&nvt->lock);
}

static ssize_t wakeup_data_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rc_dev *rc_dev = to_rc_dev(dev);
	struct nvt_dev *nvt = rc_dev->priv;
	int fifo_len, duration;
	unsigned long flags;
	ssize_t buf_len = 0;
	int i;

	spin_lock_irqsave(&nvt->lock, flags);

	fifo_len = nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_COUNT);
	fifo_len = min(fifo_len, WAKEUP_MAX_SIZE);

	/* go to first element to be read */
	while (nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY_IDX))
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY);

	for (i = 0; i < fifo_len; i++) {
		duration = nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY);
		duration = (duration & BUF_LEN_MASK) * SAMPLE_PERIOD;
		buf_len += snprintf(buf + buf_len, PAGE_SIZE - buf_len,
				    "%d ", duration);
	}
	buf_len += snprintf(buf + buf_len, PAGE_SIZE - buf_len, "\n");

	spin_unlock_irqrestore(&nvt->lock, flags);

	return buf_len;
}

static ssize_t wakeup_data_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct rc_dev *rc_dev = to_rc_dev(dev);
	u8 wake_buf[WAKEUP_MAX_SIZE];
	char **argv;
	int i, count;
	unsigned int val;
	ssize_t ret;

	argv = argv_split(GFP_KERNEL, buf, &count);
	if (!argv)
		return -ENOMEM;
	if (!count || count > WAKEUP_MAX_SIZE) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < count; i++) {
		ret = kstrtouint(argv[i], 10, &val);
		if (ret)
			goto out;
		val = DIV_ROUND_CLOSEST(val, SAMPLE_PERIOD);
		if (!val || val > 0x7f) {
			ret = -EINVAL;
			goto out;
		}
		wake_buf[i] = val;
		/* sequence must start with a pulse */
		if (i % 2 == 0)
			wake_buf[i] |= BUF_PULSE_BIT;
	}

	nvt_write_wakeup_codes(rc_dev, wake_buf, count);

	ret = len;
out:
	argv_free(argv);
	return ret;
}
static DEVICE_ATTR_RW(wakeup_data);

/* dump current cir register contents */
static void cir_dump_regs(struct nvt_dev *nvt)
{
	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);

	pr_info("%s: Dump CIR logical device registers:\n", NVT_DRIVER_NAME);
	pr_info(" * CR CIR ACTIVE :   0x%x\n",
		nvt_cr_read(nvt, CR_LOGICAL_DEV_EN));
	pr_info(" * CR CIR BASE ADDR: 0x%x\n",
		(nvt_cr_read(nvt, CR_CIR_BASE_ADDR_HI) << 8) |
		nvt_cr_read(nvt, CR_CIR_BASE_ADDR_LO));
	pr_info(" * CR CIR IRQ NUM:   0x%x\n",
		nvt_cr_read(nvt, CR_CIR_IRQ_RSRC));

	nvt_efm_disable(nvt);

	pr_info("%s: Dump CIR registers:\n", NVT_DRIVER_NAME);
	pr_info(" * IRCON:     0x%x\n", nvt_cir_reg_read(nvt, CIR_IRCON));
	pr_info(" * IRSTS:     0x%x\n", nvt_cir_reg_read(nvt, CIR_IRSTS));
	pr_info(" * IREN:      0x%x\n", nvt_cir_reg_read(nvt, CIR_IREN));
	pr_info(" * RXFCONT:   0x%x\n", nvt_cir_reg_read(nvt, CIR_RXFCONT));
	pr_info(" * CP:        0x%x\n", nvt_cir_reg_read(nvt, CIR_CP));
	pr_info(" * CC:        0x%x\n", nvt_cir_reg_read(nvt, CIR_CC));
	pr_info(" * SLCH:      0x%x\n", nvt_cir_reg_read(nvt, CIR_SLCH));
	pr_info(" * SLCL:      0x%x\n", nvt_cir_reg_read(nvt, CIR_SLCL));
	pr_info(" * FIFOCON:   0x%x\n", nvt_cir_reg_read(nvt, CIR_FIFOCON));
	pr_info(" * IRFIFOSTS: 0x%x\n", nvt_cir_reg_read(nvt, CIR_IRFIFOSTS));
	pr_info(" * SRXFIFO:   0x%x\n", nvt_cir_reg_read(nvt, CIR_SRXFIFO));
	pr_info(" * TXFCONT:   0x%x\n", nvt_cir_reg_read(nvt, CIR_TXFCONT));
	pr_info(" * STXFIFO:   0x%x\n", nvt_cir_reg_read(nvt, CIR_STXFIFO));
	pr_info(" * FCCH:      0x%x\n", nvt_cir_reg_read(nvt, CIR_FCCH));
	pr_info(" * FCCL:      0x%x\n", nvt_cir_reg_read(nvt, CIR_FCCL));
	pr_info(" * IRFSM:     0x%x\n", nvt_cir_reg_read(nvt, CIR_IRFSM));
}

/* dump current cir wake register contents */
static void cir_wake_dump_regs(struct nvt_dev *nvt)
{
	u8 i, fifo_len;

	nvt_efm_enable(nvt);
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);

	pr_info("%s: Dump CIR WAKE logical device registers:\n",
		NVT_DRIVER_NAME);
	pr_info(" * CR CIR WAKE ACTIVE :   0x%x\n",
		nvt_cr_read(nvt, CR_LOGICAL_DEV_EN));
	pr_info(" * CR CIR WAKE BASE ADDR: 0x%x\n",
		(nvt_cr_read(nvt, CR_CIR_BASE_ADDR_HI) << 8) |
		nvt_cr_read(nvt, CR_CIR_BASE_ADDR_LO));
	pr_info(" * CR CIR WAKE IRQ NUM:   0x%x\n",
		nvt_cr_read(nvt, CR_CIR_IRQ_RSRC));

	nvt_efm_disable(nvt);

	pr_info("%s: Dump CIR WAKE registers\n", NVT_DRIVER_NAME);
	pr_info(" * IRCON:          0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRCON));
	pr_info(" * IRSTS:          0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRSTS));
	pr_info(" * IREN:           0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_IREN));
	pr_info(" * FIFO CMP DEEP:  0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_CMP_DEEP));
	pr_info(" * FIFO CMP TOL:   0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_CMP_TOL));
	pr_info(" * FIFO COUNT:     0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_COUNT));
	pr_info(" * SLCH:           0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_SLCH));
	pr_info(" * SLCL:           0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_SLCL));
	pr_info(" * FIFOCON:        0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFOCON));
	pr_info(" * SRXFSTS:        0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_SRXFSTS));
	pr_info(" * SAMPLE RX FIFO: 0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_SAMPLE_RX_FIFO));
	pr_info(" * WR FIFO DATA:   0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_WR_FIFO_DATA));
	pr_info(" * RD FIFO ONLY:   0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY));
	pr_info(" * RD FIFO ONLY IDX: 0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY_IDX));
	pr_info(" * FIFO IGNORE:    0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_IGNORE));
	pr_info(" * IRFSM:          0x%x\n",
		nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRFSM));

	fifo_len = nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFO_COUNT);
	pr_info("%s: Dump CIR WAKE FIFO (len %d)\n", NVT_DRIVER_NAME, fifo_len);
	pr_info("* Contents =");
	for (i = 0; i < fifo_len; i++)
		pr_cont(" %02x",
			nvt_cir_wake_reg_read(nvt, CIR_WAKE_RD_FIFO_ONLY));
	pr_cont("\n");
}

static inline const char *nvt_find_chip(struct nvt_dev *nvt, int id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nvt_chips); i++)
		if ((id & SIO_ID_MASK) == nvt_chips[i].chip_ver) {
			nvt->chip_ver = nvt_chips[i].chip_ver;
			return nvt_chips[i].name;
		}

	return NULL;
}


/* detect hardware features */
static int nvt_hw_detect(struct nvt_dev *nvt)
{
	struct device *dev = nvt_get_dev(nvt);
	const char *chip_name;
	int chip_id;

	nvt_efm_enable(nvt);

	/* Check if we're wired for the alternate EFER setup */
	nvt->chip_major = nvt_cr_read(nvt, CR_CHIP_ID_HI);
	if (nvt->chip_major == 0xff) {
		nvt_efm_disable(nvt);
		nvt->cr_efir = CR_EFIR2;
		nvt->cr_efdr = CR_EFDR2;
		nvt_efm_enable(nvt);
		nvt->chip_major = nvt_cr_read(nvt, CR_CHIP_ID_HI);
	}
	nvt->chip_minor = nvt_cr_read(nvt, CR_CHIP_ID_LO);

	nvt_efm_disable(nvt);

	chip_id = nvt->chip_major << 8 | nvt->chip_minor;
	if (chip_id == NVT_INVALID) {
		dev_err(dev, "No device found on either EFM port\n");
		return -ENODEV;
	}

	chip_name = nvt_find_chip(nvt, chip_id);

	/* warn, but still let the driver load, if we don't know this chip */
	if (!chip_name)
		dev_warn(dev,
			 "unknown chip, id: 0x%02x 0x%02x, it may not work...",
			 nvt->chip_major, nvt->chip_minor);
	else
		dev_info(dev, "found %s or compatible: chip id: 0x%02x 0x%02x",
			 chip_name, nvt->chip_major, nvt->chip_minor);

	return 0;
}

static void nvt_cir_ldev_init(struct nvt_dev *nvt)
{
	u8 val, psreg, psmask, psval;

	if (is_w83667hg(nvt)) {
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

	/* Select CIR logical device */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR);

	nvt_set_ioaddr(nvt, &nvt->cir_addr);

	nvt_cr_write(nvt, nvt->cir_irq, CR_CIR_IRQ_RSRC);

	nvt_dbg("CIR initialized, base io port address: 0x%lx, irq: %d",
		nvt->cir_addr, nvt->cir_irq);
}

static void nvt_cir_wake_ldev_init(struct nvt_dev *nvt)
{
	/* Select ACPI logical device and anable it */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_ACPI);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	/* Enable CIR Wake via PSOUT# (Pin60) */
	nvt_set_reg_bit(nvt, CIR_WAKE_ENABLE_BIT, CR_ACPI_CIR_WAKE);

	/* enable pme interrupt of cir wakeup event */
	nvt_set_reg_bit(nvt, PME_INTR_CIR_PASS_BIT, CR_ACPI_IRQ_EVENTS2);

	/* Select CIR Wake logical device */
	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);

	nvt_set_ioaddr(nvt, &nvt->cir_wake_addr);

	nvt_dbg("CIR Wake initialized, base io port address: 0x%lx",
		nvt->cir_wake_addr);
}

/* clear out the hardware's cir rx fifo */
static void nvt_clear_cir_fifo(struct nvt_dev *nvt)
{
	u8 val = nvt_cir_reg_read(nvt, CIR_FIFOCON);
	nvt_cir_reg_write(nvt, val | CIR_FIFOCON_RXFIFOCLR, CIR_FIFOCON);
}

/* clear out the hardware's cir wake rx fifo */
static void nvt_clear_cir_wake_fifo(struct nvt_dev *nvt)
{
	u8 val, config;

	config = nvt_cir_wake_reg_read(nvt, CIR_WAKE_IRCON);

	/* clearing wake fifo works in learning mode only */
	nvt_cir_wake_reg_write(nvt, config & ~CIR_WAKE_IRCON_MODE0,
			       CIR_WAKE_IRCON);

	val = nvt_cir_wake_reg_read(nvt, CIR_WAKE_FIFOCON);
	nvt_cir_wake_reg_write(nvt, val | CIR_WAKE_FIFOCON_RXFIFOCLR,
			       CIR_WAKE_FIFOCON);

	nvt_cir_wake_reg_write(nvt, config, CIR_WAKE_IRCON);
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

	iren = CIR_IREN_RTR | CIR_IREN_PE | CIR_IREN_RFO;
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

	/* enable the CIR logical device */
	nvt_enable_logical_dev(nvt, LOGICAL_DEV_CIR);
}

static void nvt_cir_wake_regs_init(struct nvt_dev *nvt)
{
	/*
	 * Disable RX, set specific carrier on = low, off = high,
	 * and sample period (currently 50us)
	 */
	nvt_cir_wake_reg_write(nvt, CIR_WAKE_IRCON_MODE0 |
			       CIR_WAKE_IRCON_R | CIR_WAKE_IRCON_RXINV |
			       CIR_WAKE_IRCON_SAMPLE_PERIOD_SEL,
			       CIR_WAKE_IRCON);

	/* clear any and all stray interrupts */
	nvt_cir_wake_reg_write(nvt, 0xff, CIR_WAKE_IRSTS);

	/* enable the CIR WAKE logical device */
	nvt_enable_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);
}

static void nvt_enable_wake(struct nvt_dev *nvt)
{
	unsigned long flags;

	nvt_efm_enable(nvt);

	nvt_select_logical_dev(nvt, LOGICAL_DEV_ACPI);
	nvt_set_reg_bit(nvt, CIR_WAKE_ENABLE_BIT, CR_ACPI_CIR_WAKE);
	nvt_set_reg_bit(nvt, PME_INTR_CIR_PASS_BIT, CR_ACPI_IRQ_EVENTS2);

	nvt_select_logical_dev(nvt, LOGICAL_DEV_CIR_WAKE);
	nvt_cr_write(nvt, LOGICAL_DEV_ENABLE, CR_LOGICAL_DEV_EN);

	nvt_efm_disable(nvt);

	spin_lock_irqsave(&nvt->lock, flags);

	nvt_cir_wake_reg_write(nvt, CIR_WAKE_IRCON_MODE0 | CIR_WAKE_IRCON_RXEN |
			       CIR_WAKE_IRCON_R | CIR_WAKE_IRCON_RXINV |
			       CIR_WAKE_IRCON_SAMPLE_PERIOD_SEL,
			       CIR_WAKE_IRCON);
	nvt_cir_wake_reg_write(nvt, 0xff, CIR_WAKE_IRSTS);
	nvt_cir_wake_reg_write(nvt, 0, CIR_WAKE_IREN);

	spin_unlock_irqrestore(&nvt->lock, flags);
}

#if 0 /* Currently unused */
/* rx carrier detect only works in learning mode, must be called w/lock */
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
		dev_notice(nvt_get_dev(nvt),
			   "Unable to determine carrier! (c:%u, d:%u)",
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
#endif
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

	if (carrier == 0)
		return -EINVAL;

	nvt_cir_reg_write(nvt, 1, CIR_CP);
	val = 3000000 / (carrier) - 1;
	nvt_cir_reg_write(nvt, val & 0xff, CIR_CC);

	nvt_dbg("cp: 0x%x cc: 0x%x\n",
		nvt_cir_reg_read(nvt, CIR_CP), nvt_cir_reg_read(nvt, CIR_CC));

	return 0;
}

static int nvt_ir_raw_set_wakeup_filter(struct rc_dev *dev,
					struct rc_scancode_filter *sc_filter)
{
	u8 buf_val;
	int i, ret, count;
	unsigned int val;
	struct ir_raw_event *raw;
	u8 wake_buf[WAKEUP_MAX_SIZE];
	bool complete;

	/* Require mask to be set */
	if (!sc_filter->mask)
		return 0;

	raw = kmalloc_array(WAKEUP_MAX_SIZE, sizeof(*raw), GFP_KERNEL);
	if (!raw)
		return -ENOMEM;

	ret = ir_raw_encode_scancode(dev->wakeup_protocol, sc_filter->data,
				     raw, WAKEUP_MAX_SIZE);
	complete = (ret != -ENOBUFS);
	if (!complete)
		ret = WAKEUP_MAX_SIZE;
	else if (ret < 0)
		goto out_raw;

	/* Inspect the ir samples */
	for (i = 0, count = 0; i < ret && count < WAKEUP_MAX_SIZE; ++i) {
		/* NS to US */
		val = DIV_ROUND_UP(raw[i].duration, 1000L) / SAMPLE_PERIOD;

		/* Split too large values into several smaller ones */
		while (val > 0 && count < WAKEUP_MAX_SIZE) {
			/* Skip last value for better comparison tolerance */
			if (complete && i == ret - 1 && val < BUF_LEN_MASK)
				break;

			/* Clamp values to BUF_LEN_MASK at most */
			buf_val = (val > BUF_LEN_MASK) ? BUF_LEN_MASK : val;

			wake_buf[count] = buf_val;
			val -= buf_val;
			if ((raw[i]).pulse)
				wake_buf[count] |= BUF_PULSE_BIT;
			count++;
		}
	}

	nvt_write_wakeup_codes(dev, wake_buf, count);
	ret = 0;
out_raw:
	kfree(raw);

	return ret;
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
static int nvt_tx_ir(struct rc_dev *dev, unsigned *txbuf, unsigned n)
{
	struct nvt_dev *nvt = dev->priv;
	unsigned long flags;
	unsigned int i;
	u8 iren;
	int ret;

	spin_lock_irqsave(&nvt->lock, flags);

	ret = min((unsigned)(TX_BUF_LEN / sizeof(unsigned)), n);
	nvt->tx.buf_count = (ret * sizeof(unsigned));

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

	spin_unlock_irqrestore(&nvt->lock, flags);

	wait_event(nvt->tx.queue, nvt->tx.tx_state == ST_TX_REQUEST);

	spin_lock_irqsave(&nvt->lock, flags);
	nvt->tx.tx_state = ST_TX_NONE;
	spin_unlock_irqrestore(&nvt->lock, flags);

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
	u8 sample;
	int i;

	nvt_dbg_verbose("%s firing", __func__);

	if (debug)
		nvt_dump_rx_buf(nvt);

	nvt_dbg_verbose("Processing buffer of len %d", nvt->pkts);

	for (i = 0; i < nvt->pkts; i++) {
		sample = nvt->buf[i];

		rawir.pulse = ((sample & BUF_PULSE_BIT) != 0);
		rawir.duration = US_TO_NS((sample & BUF_LEN_MASK)
					  * SAMPLE_PERIOD);

		nvt_dbg("Storing %s with duration %d",
			rawir.pulse ? "pulse" : "space", rawir.duration);

		ir_raw_event_store_with_filter(nvt->rdev, &rawir);
	}

	nvt->pkts = 0;

	nvt_dbg("Calling ir_raw_event_handle\n");
	ir_raw_event_handle(nvt->rdev);

	nvt_dbg_verbose("%s done", __func__);
}

static void nvt_handle_rx_fifo_overrun(struct nvt_dev *nvt)
{
	dev_warn(nvt_get_dev(nvt), "RX FIFO overrun detected, flushing data!");

	nvt->pkts = 0;
	nvt_clear_cir_fifo(nvt);
	ir_raw_event_reset(nvt->rdev);
}

/* copy data from hardware rx fifo into driver buffer */
static void nvt_get_rx_ir_data(struct nvt_dev *nvt)
{
	u8 fifocount;
	int i;

	/* Get count of how many bytes to read from RX FIFO */
	fifocount = nvt_cir_reg_read(nvt, CIR_RXFCONT);

	nvt_dbg("attempting to fetch %u bytes from hw rx fifo", fifocount);

	/* Read fifocount bytes from CIR Sample RX FIFO register */
	for (i = 0; i < fifocount; i++)
		nvt->buf[i] = nvt_cir_reg_read(nvt, CIR_SRXFIFO);

	nvt->pkts = fifocount;
	nvt_dbg("%s: pkts now %d", __func__, nvt->pkts);

	nvt_process_rx_ir_data(nvt);
}

static void nvt_cir_log_irqs(u8 status, u8 iren)
{
	nvt_dbg("IRQ 0x%02x (IREN 0x%02x) :%s%s%s%s%s%s%s%s%s",
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
	return nvt->tx.tx_state == ST_TX_NONE;
}

/* interrupt service routine for incoming and outgoing CIR data */
static irqreturn_t nvt_cir_isr(int irq, void *data)
{
	struct nvt_dev *nvt = data;
	u8 status, iren;

	nvt_dbg_verbose("%s firing", __func__);

	spin_lock(&nvt->lock);

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
	iren = nvt_cir_reg_read(nvt, CIR_IREN);

	/* At least NCT6779D creates a spurious interrupt when the
	 * logical device is being disabled.
	 */
	if (status == 0xff && iren == 0xff) {
		spin_unlock(&nvt->lock);
		nvt_dbg_verbose("Spurious interrupt detected");
		return IRQ_HANDLED;
	}

	/* IRQ may be shared with CIR WAKE, therefore check for each
	 * status bit whether the related interrupt source is enabled
	 */
	if (!(status & iren)) {
		spin_unlock(&nvt->lock);
		nvt_dbg_verbose("%s exiting, IRSTS 0x0", __func__);
		return IRQ_NONE;
	}

	/* ack/clear all irq flags we've got */
	nvt_cir_reg_write(nvt, status, CIR_IRSTS);
	nvt_cir_reg_write(nvt, 0, CIR_IRSTS);

	nvt_cir_log_irqs(status, iren);

	if (status & CIR_IRSTS_RFO)
		nvt_handle_rx_fifo_overrun(nvt);

	else if (status & (CIR_IRSTS_RTR | CIR_IRSTS_PE)) {
		/* We only do rx if not tx'ing */
		if (nvt_cir_tx_inactive(nvt))
			nvt_get_rx_ir_data(nvt);
	}

	if (status & CIR_IRSTS_TE)
		nvt_clear_tx_fifo(nvt);

	if (status & CIR_IRSTS_TTR) {
		unsigned int pos, count;
		u8 tmp;

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
	}

	if (status & CIR_IRSTS_TFU) {
		if (nvt->tx.tx_state == ST_TX_REPLY) {
			nvt->tx.tx_state = ST_TX_REQUEST;
			wake_up(&nvt->tx.queue);
		}
	}

	spin_unlock(&nvt->lock);

	nvt_dbg_verbose("%s done", __func__);
	return IRQ_HANDLED;
}

static void nvt_disable_cir(struct nvt_dev *nvt)
{
	unsigned long flags;

	spin_lock_irqsave(&nvt->lock, flags);

	/* disable CIR interrupts */
	nvt_cir_reg_write(nvt, 0, CIR_IREN);

	/* clear any and all pending interrupts */
	nvt_cir_reg_write(nvt, 0xff, CIR_IRSTS);

	/* clear all function enable flags */
	nvt_cir_reg_write(nvt, 0, CIR_IRCON);

	/* clear hardware rx and tx fifos */
	nvt_clear_cir_fifo(nvt);
	nvt_clear_tx_fifo(nvt);

	spin_unlock_irqrestore(&nvt->lock, flags);

	/* disable the CIR logical device */
	nvt_disable_logical_dev(nvt, LOGICAL_DEV_CIR);
}

static int nvt_open(struct rc_dev *dev)
{
	struct nvt_dev *nvt = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&nvt->lock, flags);

	/* set function enable flags */
	nvt_cir_reg_write(nvt, CIR_IRCON_TXEN | CIR_IRCON_RXEN |
			  CIR_IRCON_RXINV | CIR_IRCON_SAMPLE_PERIOD_SEL,
			  CIR_IRCON);

	/* clear all pending interrupts */
	nvt_cir_reg_write(nvt, 0xff, CIR_IRSTS);

	/* enable interrupts */
	nvt_set_cir_iren(nvt);

	spin_unlock_irqrestore(&nvt->lock, flags);

	/* enable the CIR logical device */
	nvt_enable_logical_dev(nvt, LOGICAL_DEV_CIR);

	return 0;
}

static void nvt_close(struct rc_dev *dev)
{
	struct nvt_dev *nvt = dev->priv;

	nvt_disable_cir(nvt);
}

/* Allocate memory, probe hardware, and initialize everything */
static int nvt_probe(struct pnp_dev *pdev, const struct pnp_device_id *dev_id)
{
	struct nvt_dev *nvt;
	struct rc_dev *rdev;
	int ret;

	nvt = devm_kzalloc(&pdev->dev, sizeof(struct nvt_dev), GFP_KERNEL);
	if (!nvt)
		return -ENOMEM;

	/* input device for IR remote (and tx) */
	nvt->rdev = devm_rc_allocate_device(&pdev->dev, RC_DRIVER_IR_RAW);
	if (!nvt->rdev)
		return -ENOMEM;
	rdev = nvt->rdev;

	/* activate pnp device */
	ret = pnp_activate_dev(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Could not activate PNP device!\n");
		return ret;
	}

	/* validate pnp resources */
	if (!pnp_port_valid(pdev, 0) ||
	    pnp_port_len(pdev, 0) < CIR_IOREG_LENGTH) {
		dev_err(&pdev->dev, "IR PNP Port not valid!\n");
		return -EINVAL;
	}

	if (!pnp_irq_valid(pdev, 0)) {
		dev_err(&pdev->dev, "PNP IRQ not valid!\n");
		return -EINVAL;
	}

	if (!pnp_port_valid(pdev, 1) ||
	    pnp_port_len(pdev, 1) < CIR_IOREG_LENGTH) {
		dev_err(&pdev->dev, "Wake PNP Port not valid!\n");
		return -EINVAL;
	}

	nvt->cir_addr = pnp_port_start(pdev, 0);
	nvt->cir_irq  = pnp_irq(pdev, 0);

	nvt->cir_wake_addr = pnp_port_start(pdev, 1);

	nvt->cr_efir = CR_EFIR;
	nvt->cr_efdr = CR_EFDR;

	spin_lock_init(&nvt->lock);

	pnp_set_drvdata(pdev, nvt);

	init_waitqueue_head(&nvt->tx.queue);

	ret = nvt_hw_detect(nvt);
	if (ret)
		return ret;

	/* Initialize CIR & CIR Wake Logical Devices */
	nvt_efm_enable(nvt);
	nvt_cir_ldev_init(nvt);
	nvt_cir_wake_ldev_init(nvt);
	nvt_efm_disable(nvt);

	/*
	 * Initialize CIR & CIR Wake Config Registers
	 * and enable logical devices
	 */
	nvt_cir_regs_init(nvt);
	nvt_cir_wake_regs_init(nvt);

	/* Set up the rc device */
	rdev->priv = nvt;
	rdev->allowed_protocols = RC_BIT_ALL_IR_DECODER;
	rdev->allowed_wakeup_protocols = RC_BIT_ALL_IR_ENCODER;
	rdev->encode_wakeup = true;
	rdev->open = nvt_open;
	rdev->close = nvt_close;
	rdev->tx_ir = nvt_tx_ir;
	rdev->s_tx_carrier = nvt_set_tx_carrier;
	rdev->s_wakeup_filter = nvt_ir_raw_set_wakeup_filter;
	rdev->input_name = "Nuvoton w836x7hg Infrared Remote Transceiver";
	rdev->input_phys = "nuvoton/cir0";
	rdev->input_id.bustype = BUS_HOST;
	rdev->input_id.vendor = PCI_VENDOR_ID_WINBOND2;
	rdev->input_id.product = nvt->chip_major;
	rdev->input_id.version = nvt->chip_minor;
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
	ret = devm_rc_register_device(&pdev->dev, rdev);
	if (ret)
		return ret;

	/* now claim resources */
	if (!devm_request_region(&pdev->dev, nvt->cir_addr,
			    CIR_IOREG_LENGTH, NVT_DRIVER_NAME))
		return -EBUSY;

	ret = devm_request_irq(&pdev->dev, nvt->cir_irq, nvt_cir_isr,
			       IRQF_SHARED, NVT_DRIVER_NAME, nvt);
	if (ret)
		return ret;

	if (!devm_request_region(&pdev->dev, nvt->cir_wake_addr,
			    CIR_IOREG_LENGTH, NVT_DRIVER_NAME "-wake"))
		return -EBUSY;

	ret = device_create_file(&rdev->dev, &dev_attr_wakeup_data);
	if (ret)
		return ret;

	device_init_wakeup(&pdev->dev, true);

	dev_notice(&pdev->dev, "driver has been successfully loaded\n");
	if (debug) {
		cir_dump_regs(nvt);
		cir_wake_dump_regs(nvt);
	}

	return 0;
}

static void nvt_remove(struct pnp_dev *pdev)
{
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);

	device_remove_file(&nvt->rdev->dev, &dev_attr_wakeup_data);

	nvt_disable_cir(nvt);

	/* enable CIR Wake (for IR power-on) */
	nvt_enable_wake(nvt);
}

static int nvt_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);
	unsigned long flags;

	nvt_dbg("%s called", __func__);

	spin_lock_irqsave(&nvt->lock, flags);

	nvt->tx.tx_state = ST_TX_NONE;

	/* disable all CIR interrupts */
	nvt_cir_reg_write(nvt, 0, CIR_IREN);

	spin_unlock_irqrestore(&nvt->lock, flags);

	/* disable cir logical dev */
	nvt_disable_logical_dev(nvt, LOGICAL_DEV_CIR);

	/* make sure wake is enabled */
	nvt_enable_wake(nvt);

	return 0;
}

static int nvt_resume(struct pnp_dev *pdev)
{
	struct nvt_dev *nvt = pnp_get_drvdata(pdev);

	nvt_dbg("%s called", __func__);

	nvt_cir_regs_init(nvt);
	nvt_cir_wake_regs_init(nvt);

	return 0;
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
	.remove		= nvt_remove,
	.suspend	= nvt_suspend,
	.resume		= nvt_resume,
	.shutdown	= nvt_shutdown,
};

module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging output");

MODULE_DEVICE_TABLE(pnp, nvt_ids);
MODULE_DESCRIPTION("Nuvoton W83667HG-A & W83677HG-I CIR driver");

MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
MODULE_LICENSE("GPL");

module_pnp_driver(nvt_driver);
