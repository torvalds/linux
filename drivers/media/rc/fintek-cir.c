/*
 * Driver for Feature Integration Technology Inc. (aka Fintek) LPC CIR
 *
 * Copyright (C) 2011 Jarod Wilson <jarod@redhat.com>
 *
 * Special thanks to Fintek for providing hardware and spec sheets.
 * This driver is based upon the nuvoton, ite and ene drivers for
 * similar hardware.
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

#include "fintek-cir.h"

/* write val to config reg */
static inline void fintek_cr_write(struct fintek_dev *fintek, u8 val, u8 reg)
{
	fit_dbg("%s: reg 0x%02x, val 0x%02x  (ip/dp: %02x/%02x)",
		__func__, reg, val, fintek->cr_ip, fintek->cr_dp);
	outb(reg, fintek->cr_ip);
	outb(val, fintek->cr_dp);
}

/* read val from config reg */
static inline u8 fintek_cr_read(struct fintek_dev *fintek, u8 reg)
{
	u8 val;

	outb(reg, fintek->cr_ip);
	val = inb(fintek->cr_dp);

	fit_dbg("%s: reg 0x%02x, val 0x%02x  (ip/dp: %02x/%02x)",
		__func__, reg, val, fintek->cr_ip, fintek->cr_dp);
	return val;
}

/* update config register bit without changing other bits */
static inline void fintek_set_reg_bit(struct fintek_dev *fintek, u8 val, u8 reg)
{
	u8 tmp = fintek_cr_read(fintek, reg) | val;
	fintek_cr_write(fintek, tmp, reg);
}

/* clear config register bit without changing other bits */
static inline void fintek_clear_reg_bit(struct fintek_dev *fintek, u8 val, u8 reg)
{
	u8 tmp = fintek_cr_read(fintek, reg) & ~val;
	fintek_cr_write(fintek, tmp, reg);
}

/* enter config mode */
static inline void fintek_config_mode_enable(struct fintek_dev *fintek)
{
	/* Enabling Config Mode explicitly requires writing 2x */
	outb(CONFIG_REG_ENABLE, fintek->cr_ip);
	outb(CONFIG_REG_ENABLE, fintek->cr_ip);
}

/* exit config mode */
static inline void fintek_config_mode_disable(struct fintek_dev *fintek)
{
	outb(CONFIG_REG_DISABLE, fintek->cr_ip);
}

/*
 * When you want to address a specific logical device, write its logical
 * device number to GCR_LOGICAL_DEV_NO
 */
static inline void fintek_select_logical_dev(struct fintek_dev *fintek, u8 ldev)
{
	fintek_cr_write(fintek, ldev, GCR_LOGICAL_DEV_NO);
}

/* write val to cir config register */
static inline void fintek_cir_reg_write(struct fintek_dev *fintek, u8 val, u8 offset)
{
	outb(val, fintek->cir_addr + offset);
}

/* read val from cir config register */
static u8 fintek_cir_reg_read(struct fintek_dev *fintek, u8 offset)
{
	u8 val;

	val = inb(fintek->cir_addr + offset);

	return val;
}

#define pr_reg(text, ...) \
	printk(KERN_INFO KBUILD_MODNAME ": " text, ## __VA_ARGS__)

/* dump current cir register contents */
static void cir_dump_regs(struct fintek_dev *fintek)
{
	fintek_config_mode_enable(fintek);
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);

	pr_reg("%s: Dump CIR logical device registers:\n", FINTEK_DRIVER_NAME);
	pr_reg(" * CR CIR BASE ADDR: 0x%x\n",
	       (fintek_cr_read(fintek, CIR_CR_BASE_ADDR_HI) << 8) |
		fintek_cr_read(fintek, CIR_CR_BASE_ADDR_LO));
	pr_reg(" * CR CIR IRQ NUM:   0x%x\n",
	       fintek_cr_read(fintek, CIR_CR_IRQ_SEL));

	fintek_config_mode_disable(fintek);

	pr_reg("%s: Dump CIR registers:\n", FINTEK_DRIVER_NAME);
	pr_reg(" * STATUS:     0x%x\n", fintek_cir_reg_read(fintek, CIR_STATUS));
	pr_reg(" * CONTROL:    0x%x\n", fintek_cir_reg_read(fintek, CIR_CONTROL));
	pr_reg(" * RX_DATA:    0x%x\n", fintek_cir_reg_read(fintek, CIR_RX_DATA));
	pr_reg(" * TX_CONTROL: 0x%x\n", fintek_cir_reg_read(fintek, CIR_TX_CONTROL));
	pr_reg(" * TX_DATA:    0x%x\n", fintek_cir_reg_read(fintek, CIR_TX_DATA));
}

/* detect hardware features */
static int fintek_hw_detect(struct fintek_dev *fintek)
{
	unsigned long flags;
	u8 chip_major, chip_minor;
	u8 vendor_major, vendor_minor;
	u8 portsel, ir_class;
	u16 vendor, chip;
	int ret = 0;

	fintek_config_mode_enable(fintek);

	/* Check if we're using config port 0x4e or 0x2e */
	portsel = fintek_cr_read(fintek, GCR_CONFIG_PORT_SEL);
	if (portsel == 0xff) {
		fit_pr(KERN_INFO, "first portsel read was bunk, trying alt");
		fintek_config_mode_disable(fintek);
		fintek->cr_ip = CR_INDEX_PORT2;
		fintek->cr_dp = CR_DATA_PORT2;
		fintek_config_mode_enable(fintek);
		portsel = fintek_cr_read(fintek, GCR_CONFIG_PORT_SEL);
	}
	fit_dbg("portsel reg: 0x%02x", portsel);

	ir_class = fintek_cir_reg_read(fintek, CIR_CR_CLASS);
	fit_dbg("ir_class reg: 0x%02x", ir_class);

	switch (ir_class) {
	case CLASS_RX_2TX:
	case CLASS_RX_1TX:
		fintek->hw_tx_capable = true;
		break;
	case CLASS_RX_ONLY:
	default:
		fintek->hw_tx_capable = false;
		break;
	}

	chip_major = fintek_cr_read(fintek, GCR_CHIP_ID_HI);
	chip_minor = fintek_cr_read(fintek, GCR_CHIP_ID_LO);
	chip  = chip_major << 8 | chip_minor;

	vendor_major = fintek_cr_read(fintek, GCR_VENDOR_ID_HI);
	vendor_minor = fintek_cr_read(fintek, GCR_VENDOR_ID_LO);
	vendor = vendor_major << 8 | vendor_minor;

	if (vendor != VENDOR_ID_FINTEK)
		fit_pr(KERN_WARNING, "Unknown vendor ID: 0x%04x", vendor);
	else
		fit_dbg("Read Fintek vendor ID from chip");

	fintek_config_mode_disable(fintek);

	spin_lock_irqsave(&fintek->fintek_lock, flags);
	fintek->chip_major  = chip_major;
	fintek->chip_minor  = chip_minor;
	fintek->chip_vendor = vendor;

	/*
	 * Newer reviews of this chipset uses port 8 instead of 5
	 */
	if ((chip != 0x0408) || (chip != 0x0804))
		fintek->logical_dev_cir = LOGICAL_DEV_CIR_REV2;
	else
		fintek->logical_dev_cir = LOGICAL_DEV_CIR_REV1;

	spin_unlock_irqrestore(&fintek->fintek_lock, flags);

	return ret;
}

static void fintek_cir_ldev_init(struct fintek_dev *fintek)
{
	/* Select CIR logical device and enable */
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);
	fintek_cr_write(fintek, LOGICAL_DEV_ENABLE, CIR_CR_DEV_EN);

	/* Write allocated CIR address and IRQ information to hardware */
	fintek_cr_write(fintek, fintek->cir_addr >> 8, CIR_CR_BASE_ADDR_HI);
	fintek_cr_write(fintek, fintek->cir_addr & 0xff, CIR_CR_BASE_ADDR_LO);

	fintek_cr_write(fintek, fintek->cir_irq, CIR_CR_IRQ_SEL);

	fit_dbg("CIR initialized, base io address: 0x%lx, irq: %d (len: %d)",
		fintek->cir_addr, fintek->cir_irq, fintek->cir_port_len);
}

/* enable CIR interrupts */
static void fintek_enable_cir_irq(struct fintek_dev *fintek)
{
	fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_EN, CIR_STATUS);
}

static void fintek_cir_regs_init(struct fintek_dev *fintek)
{
	/* clear any and all stray interrupts */
	fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_MASK, CIR_STATUS);

	/* and finally, enable interrupts */
	fintek_enable_cir_irq(fintek);
}

static void fintek_enable_wake(struct fintek_dev *fintek)
{
	fintek_config_mode_enable(fintek);
	fintek_select_logical_dev(fintek, LOGICAL_DEV_ACPI);

	/* Allow CIR PME's to wake system */
	fintek_set_reg_bit(fintek, ACPI_WAKE_EN_CIR_BIT, LDEV_ACPI_WAKE_EN_REG);
	/* Enable CIR PME's */
	fintek_set_reg_bit(fintek, ACPI_PME_CIR_BIT, LDEV_ACPI_PME_EN_REG);
	/* Clear CIR PME status register */
	fintek_set_reg_bit(fintek, ACPI_PME_CIR_BIT, LDEV_ACPI_PME_CLR_REG);
	/* Save state */
	fintek_set_reg_bit(fintek, ACPI_STATE_CIR_BIT, LDEV_ACPI_STATE_REG);

	fintek_config_mode_disable(fintek);
}

static int fintek_cmdsize(u8 cmd, u8 subcmd)
{
	int datasize = 0;

	switch (cmd) {
	case BUF_COMMAND_NULL:
		if (subcmd == BUF_HW_CMD_HEADER)
			datasize = 1;
		break;
	case BUF_HW_CMD_HEADER:
		if (subcmd == BUF_CMD_G_REVISION)
			datasize = 2;
		break;
	case BUF_COMMAND_HEADER:
		switch (subcmd) {
		case BUF_CMD_S_CARRIER:
		case BUF_CMD_S_TIMEOUT:
		case BUF_RSP_PULSE_COUNT:
			datasize = 2;
			break;
		case BUF_CMD_SIG_END:
		case BUF_CMD_S_TXMASK:
		case BUF_CMD_S_RXSENSOR:
			datasize = 1;
			break;
		}
	}

	return datasize;
}

/* process ir data stored in driver buffer */
static void fintek_process_rx_ir_data(struct fintek_dev *fintek)
{
	DEFINE_IR_RAW_EVENT(rawir);
	u8 sample;
	int i;

	for (i = 0; i < fintek->pkts; i++) {
		sample = fintek->buf[i];
		switch (fintek->parser_state) {
		case CMD_HEADER:
			fintek->cmd = sample;
			if ((fintek->cmd == BUF_COMMAND_HEADER) ||
			    ((fintek->cmd & BUF_COMMAND_MASK) !=
			     BUF_PULSE_BIT)) {
				fintek->parser_state = SUBCMD;
				continue;
			}
			fintek->rem = (fintek->cmd & BUF_LEN_MASK);
			fit_dbg("%s: rem: 0x%02x", __func__, fintek->rem);
			if (fintek->rem)
				fintek->parser_state = PARSE_IRDATA;
			else
				ir_raw_event_reset(fintek->rdev);
			break;
		case SUBCMD:
			fintek->rem = fintek_cmdsize(fintek->cmd, sample);
			fintek->parser_state = CMD_DATA;
			break;
		case CMD_DATA:
			fintek->rem--;
			break;
		case PARSE_IRDATA:
			fintek->rem--;
			init_ir_raw_event(&rawir);
			rawir.pulse = ((sample & BUF_PULSE_BIT) != 0);
			rawir.duration = US_TO_NS((sample & BUF_SAMPLE_MASK)
					  * CIR_SAMPLE_PERIOD);

			fit_dbg("Storing %s with duration %d",
				rawir.pulse ? "pulse" : "space",
				rawir.duration);
			ir_raw_event_store_with_filter(fintek->rdev, &rawir);
			break;
		}

		if ((fintek->parser_state != CMD_HEADER) && !fintek->rem)
			fintek->parser_state = CMD_HEADER;
	}

	fintek->pkts = 0;

	fit_dbg("Calling ir_raw_event_handle");
	ir_raw_event_handle(fintek->rdev);
}

/* copy data from hardware rx register into driver buffer */
static void fintek_get_rx_ir_data(struct fintek_dev *fintek, u8 rx_irqs)
{
	unsigned long flags;
	u8 sample, status;

	spin_lock_irqsave(&fintek->fintek_lock, flags);

	/*
	 * We must read data from CIR_RX_DATA until the hardware IR buffer
	 * is empty and clears the RX_TIMEOUT and/or RX_RECEIVE flags in
	 * the CIR_STATUS register
	 */
	do {
		sample = fintek_cir_reg_read(fintek, CIR_RX_DATA);
		fit_dbg("%s: sample: 0x%02x", __func__, sample);

		fintek->buf[fintek->pkts] = sample;
		fintek->pkts++;

		status = fintek_cir_reg_read(fintek, CIR_STATUS);
		if (!(status & CIR_STATUS_IRQ_EN))
			break;
	} while (status & rx_irqs);

	fintek_process_rx_ir_data(fintek);

	spin_unlock_irqrestore(&fintek->fintek_lock, flags);
}

static void fintek_cir_log_irqs(u8 status)
{
	fit_pr(KERN_INFO, "IRQ 0x%02x:%s%s%s%s%s", status,
		status & CIR_STATUS_IRQ_EN	? " IRQEN"	: "",
		status & CIR_STATUS_TX_FINISH	? " TXF"	: "",
		status & CIR_STATUS_TX_UNDERRUN	? " TXU"	: "",
		status & CIR_STATUS_RX_TIMEOUT	? " RXTO"	: "",
		status & CIR_STATUS_RX_RECEIVE	? " RXOK"	: "");
}

/* interrupt service routine for incoming and outgoing CIR data */
static irqreturn_t fintek_cir_isr(int irq, void *data)
{
	struct fintek_dev *fintek = data;
	u8 status, rx_irqs;

	fit_dbg_verbose("%s firing", __func__);

	fintek_config_mode_enable(fintek);
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);
	fintek_config_mode_disable(fintek);

	/*
	 * Get IR Status register contents. Write 1 to ack/clear
	 *
	 * bit: reg name    - description
	 *   3: TX_FINISH   - TX is finished
	 *   2: TX_UNDERRUN - TX underrun
	 *   1: RX_TIMEOUT  - RX data timeout
	 *   0: RX_RECEIVE  - RX data received
	 */
	status = fintek_cir_reg_read(fintek, CIR_STATUS);
	if (!(status & CIR_STATUS_IRQ_MASK) || status == 0xff) {
		fit_dbg_verbose("%s exiting, IRSTS 0x%02x", __func__, status);
		fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_MASK, CIR_STATUS);
		return IRQ_RETVAL(IRQ_NONE);
	}

	if (debug)
		fintek_cir_log_irqs(status);

	rx_irqs = status & (CIR_STATUS_RX_RECEIVE | CIR_STATUS_RX_TIMEOUT);
	if (rx_irqs)
		fintek_get_rx_ir_data(fintek, rx_irqs);

	/* ack/clear all irq flags we've got */
	fintek_cir_reg_write(fintek, status, CIR_STATUS);

	fit_dbg_verbose("%s done", __func__);
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void fintek_enable_cir(struct fintek_dev *fintek)
{
	/* set IRQ enabled */
	fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_EN, CIR_STATUS);

	fintek_config_mode_enable(fintek);

	/* enable the CIR logical device */
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);
	fintek_cr_write(fintek, LOGICAL_DEV_ENABLE, CIR_CR_DEV_EN);

	fintek_config_mode_disable(fintek);

	/* clear all pending interrupts */
	fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_MASK, CIR_STATUS);

	/* enable interrupts */
	fintek_enable_cir_irq(fintek);
}

static void fintek_disable_cir(struct fintek_dev *fintek)
{
	fintek_config_mode_enable(fintek);

	/* disable the CIR logical device */
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);
	fintek_cr_write(fintek, LOGICAL_DEV_DISABLE, CIR_CR_DEV_EN);

	fintek_config_mode_disable(fintek);
}

static int fintek_open(struct rc_dev *dev)
{
	struct fintek_dev *fintek = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&fintek->fintek_lock, flags);
	fintek_enable_cir(fintek);
	spin_unlock_irqrestore(&fintek->fintek_lock, flags);

	return 0;
}

static void fintek_close(struct rc_dev *dev)
{
	struct fintek_dev *fintek = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&fintek->fintek_lock, flags);
	fintek_disable_cir(fintek);
	spin_unlock_irqrestore(&fintek->fintek_lock, flags);
}

/* Allocate memory, probe hardware, and initialize everything */
static int fintek_probe(struct pnp_dev *pdev, const struct pnp_device_id *dev_id)
{
	struct fintek_dev *fintek;
	struct rc_dev *rdev;
	int ret = -ENOMEM;

	fintek = kzalloc(sizeof(struct fintek_dev), GFP_KERNEL);
	if (!fintek)
		return ret;

	/* input device for IR remote (and tx) */
	rdev = rc_allocate_device();
	if (!rdev)
		goto failure;

	ret = -ENODEV;
	/* validate pnp resources */
	if (!pnp_port_valid(pdev, 0)) {
		dev_err(&pdev->dev, "IR PNP Port not valid!\n");
		goto failure;
	}

	if (!pnp_irq_valid(pdev, 0)) {
		dev_err(&pdev->dev, "IR PNP IRQ not valid!\n");
		goto failure;
	}

	fintek->cir_addr = pnp_port_start(pdev, 0);
	fintek->cir_irq  = pnp_irq(pdev, 0);
	fintek->cir_port_len = pnp_port_len(pdev, 0);

	fintek->cr_ip = CR_INDEX_PORT;
	fintek->cr_dp = CR_DATA_PORT;

	spin_lock_init(&fintek->fintek_lock);

	ret = -EBUSY;
	/* now claim resources */
	if (!request_region(fintek->cir_addr,
			    fintek->cir_port_len, FINTEK_DRIVER_NAME))
		goto failure;

	if (request_irq(fintek->cir_irq, fintek_cir_isr, IRQF_SHARED,
			FINTEK_DRIVER_NAME, (void *)fintek))
		goto failure;

	pnp_set_drvdata(pdev, fintek);
	fintek->pdev = pdev;

	ret = fintek_hw_detect(fintek);
	if (ret)
		goto failure;

	/* Initialize CIR & CIR Wake Logical Devices */
	fintek_config_mode_enable(fintek);
	fintek_cir_ldev_init(fintek);
	fintek_config_mode_disable(fintek);

	/* Initialize CIR & CIR Wake Config Registers */
	fintek_cir_regs_init(fintek);

	/* Set up the rc device */
	rdev->priv = fintek;
	rdev->driver_type = RC_DRIVER_IR_RAW;
	rdev->allowed_protos = RC_TYPE_ALL;
	rdev->open = fintek_open;
	rdev->close = fintek_close;
	rdev->input_name = FINTEK_DESCRIPTION;
	rdev->input_phys = "fintek/cir0";
	rdev->input_id.bustype = BUS_HOST;
	rdev->input_id.vendor = VENDOR_ID_FINTEK;
	rdev->input_id.product = fintek->chip_major;
	rdev->input_id.version = fintek->chip_minor;
	rdev->dev.parent = &pdev->dev;
	rdev->driver_name = FINTEK_DRIVER_NAME;
	rdev->map_name = RC_MAP_RC6_MCE;
	rdev->timeout = US_TO_NS(1000);
	/* rx resolution is hardwired to 50us atm, 1, 25, 100 also possible */
	rdev->rx_resolution = US_TO_NS(CIR_SAMPLE_PERIOD);

	ret = rc_register_device(rdev);
	if (ret)
		goto failure;

	device_init_wakeup(&pdev->dev, true);
	fintek->rdev = rdev;
	fit_pr(KERN_NOTICE, "driver has been successfully loaded\n");
	if (debug)
		cir_dump_regs(fintek);

	return 0;

failure:
	if (fintek->cir_irq)
		free_irq(fintek->cir_irq, fintek);
	if (fintek->cir_addr)
		release_region(fintek->cir_addr, fintek->cir_port_len);

	rc_free_device(rdev);
	kfree(fintek);

	return ret;
}

static void __devexit fintek_remove(struct pnp_dev *pdev)
{
	struct fintek_dev *fintek = pnp_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&fintek->fintek_lock, flags);
	/* disable CIR */
	fintek_disable_cir(fintek);
	fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_MASK, CIR_STATUS);
	/* enable CIR Wake (for IR power-on) */
	fintek_enable_wake(fintek);
	spin_unlock_irqrestore(&fintek->fintek_lock, flags);

	/* free resources */
	free_irq(fintek->cir_irq, fintek);
	release_region(fintek->cir_addr, fintek->cir_port_len);

	rc_unregister_device(fintek->rdev);

	kfree(fintek);
}

static int fintek_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	struct fintek_dev *fintek = pnp_get_drvdata(pdev);
	unsigned long flags;

	fit_dbg("%s called", __func__);

	spin_lock_irqsave(&fintek->fintek_lock, flags);

	/* disable all CIR interrupts */
	fintek_cir_reg_write(fintek, CIR_STATUS_IRQ_MASK, CIR_STATUS);

	spin_unlock_irqrestore(&fintek->fintek_lock, flags);

	fintek_config_mode_enable(fintek);

	/* disable cir logical dev */
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);
	fintek_cr_write(fintek, LOGICAL_DEV_DISABLE, CIR_CR_DEV_EN);

	fintek_config_mode_disable(fintek);

	/* make sure wake is enabled */
	fintek_enable_wake(fintek);

	return 0;
}

static int fintek_resume(struct pnp_dev *pdev)
{
	int ret = 0;
	struct fintek_dev *fintek = pnp_get_drvdata(pdev);

	fit_dbg("%s called", __func__);

	/* open interrupt */
	fintek_enable_cir_irq(fintek);

	/* Enable CIR logical device */
	fintek_config_mode_enable(fintek);
	fintek_select_logical_dev(fintek, fintek->logical_dev_cir);
	fintek_cr_write(fintek, LOGICAL_DEV_ENABLE, CIR_CR_DEV_EN);

	fintek_config_mode_disable(fintek);

	fintek_cir_regs_init(fintek);

	return ret;
}

static void fintek_shutdown(struct pnp_dev *pdev)
{
	struct fintek_dev *fintek = pnp_get_drvdata(pdev);
	fintek_enable_wake(fintek);
}

static const struct pnp_device_id fintek_ids[] = {
	{ "FIT0002", 0 },   /* CIR */
	{ "", 0 },
};

static struct pnp_driver fintek_driver = {
	.name		= FINTEK_DRIVER_NAME,
	.id_table	= fintek_ids,
	.flags		= PNP_DRIVER_RES_DO_NOT_CHANGE,
	.probe		= fintek_probe,
	.remove		= __devexit_p(fintek_remove),
	.suspend	= fintek_suspend,
	.resume		= fintek_resume,
	.shutdown	= fintek_shutdown,
};

int fintek_init(void)
{
	return pnp_register_driver(&fintek_driver);
}

void fintek_exit(void)
{
	pnp_unregister_driver(&fintek_driver);
}

module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging output");

MODULE_DEVICE_TABLE(pnp, fintek_ids);
MODULE_DESCRIPTION(FINTEK_DESCRIPTION " driver");

MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
MODULE_LICENSE("GPL");

module_init(fintek_init);
module_exit(fintek_exit);
