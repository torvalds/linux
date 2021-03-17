// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010-2012 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * CAN driver for PEAK-System PCAN-PC Card
 * Derived from the PCAN project file driver/src/pcan_pccard.c
 * Copyright (C) 2006-2010 PEAK System-Technik GmbH
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include "sja1000.h"

MODULE_AUTHOR("Stephane Grosjean <s.grosjean@peak-system.com>");
MODULE_DESCRIPTION("CAN driver for PEAK-System PCAN-PC Cards");
MODULE_LICENSE("GPL v2");

/* PEAK-System PCMCIA driver name */
#define PCC_NAME		"peak_pcmcia"

#define PCC_CHAN_MAX		2

#define PCC_CAN_CLOCK		(16000000 / 2)

#define PCC_MANF_ID		0x0377
#define PCC_CARD_ID		0x0001

#define PCC_CHAN_SIZE		0x20
#define PCC_CHAN_OFF(c)		((c) * PCC_CHAN_SIZE)
#define PCC_COMN_OFF		(PCC_CHAN_OFF(PCC_CHAN_MAX))
#define PCC_COMN_SIZE		0x40

/* common area registers */
#define PCC_CCR			0x00
#define PCC_CSR			0x02
#define PCC_CPR			0x04
#define PCC_SPI_DIR		0x06
#define PCC_SPI_DOR		0x08
#define PCC_SPI_ADR		0x0a
#define PCC_SPI_IR		0x0c
#define PCC_FW_MAJOR		0x10
#define PCC_FW_MINOR		0x12

/* CCR bits */
#define PCC_CCR_CLK_16		0x00
#define PCC_CCR_CLK_10		0x01
#define PCC_CCR_CLK_21		0x02
#define PCC_CCR_CLK_8		0x03
#define PCC_CCR_CLK_MASK	PCC_CCR_CLK_8

#define PCC_CCR_RST_CHAN(c)	(0x01 << ((c) + 2))
#define PCC_CCR_RST_ALL		(PCC_CCR_RST_CHAN(0) | PCC_CCR_RST_CHAN(1))
#define PCC_CCR_RST_MASK	PCC_CCR_RST_ALL

/* led selection bits */
#define PCC_LED(c)		(1 << (c))
#define PCC_LED_ALL		(PCC_LED(0) | PCC_LED(1))

/* led state value */
#define PCC_LED_ON		0x00
#define PCC_LED_FAST		0x01
#define PCC_LED_SLOW		0x02
#define PCC_LED_OFF		0x03

#define PCC_CCR_LED_CHAN(s, c)	((s) << (((c) + 2) << 1))

#define PCC_CCR_LED_ON_CHAN(c)		PCC_CCR_LED_CHAN(PCC_LED_ON, c)
#define PCC_CCR_LED_FAST_CHAN(c)	PCC_CCR_LED_CHAN(PCC_LED_FAST, c)
#define PCC_CCR_LED_SLOW_CHAN(c)	PCC_CCR_LED_CHAN(PCC_LED_SLOW, c)
#define PCC_CCR_LED_OFF_CHAN(c)		PCC_CCR_LED_CHAN(PCC_LED_OFF, c)
#define PCC_CCR_LED_MASK_CHAN(c)	PCC_CCR_LED_OFF_CHAN(c)
#define PCC_CCR_LED_OFF_ALL		(PCC_CCR_LED_OFF_CHAN(0) | \
					 PCC_CCR_LED_OFF_CHAN(1))
#define PCC_CCR_LED_MASK		PCC_CCR_LED_OFF_ALL

#define PCC_CCR_INIT	(PCC_CCR_CLK_16 | PCC_CCR_RST_ALL | PCC_CCR_LED_OFF_ALL)

/* CSR bits */
#define PCC_CSR_SPI_BUSY		0x04

/* time waiting for SPI busy (prevent from infinite loop) */
#define PCC_SPI_MAX_BUSY_WAIT_MS	3

/* max count of reading the SPI status register waiting for a change */
/* (prevent from infinite loop) */
#define PCC_WRITE_MAX_LOOP		1000

/* max nb of int handled by that isr in one shot (prevent from infinite loop) */
#define PCC_ISR_MAX_LOOP		10

/* EEPROM chip instruction set */
/* note: EEPROM Read/Write instructions include A8 bit */
#define PCC_EEP_WRITE(a)	(0x02 | (((a) & 0x100) >> 5))
#define PCC_EEP_READ(a)		(0x03 | (((a) & 0x100) >> 5))
#define PCC_EEP_WRDI		0x04	/* EEPROM Write Disable */
#define PCC_EEP_RDSR		0x05	/* EEPROM Read Status Register */
#define PCC_EEP_WREN		0x06	/* EEPROM Write Enable */

/* EEPROM Status Register bits */
#define PCC_EEP_SR_WEN		0x02	/* EEPROM SR Write Enable bit */
#define PCC_EEP_SR_WIP		0x01	/* EEPROM SR Write In Progress bit */

/*
 * The board configuration is probably following:
 * RX1 is connected to ground.
 * TX1 is not connected.
 * CLKO is not connected.
 * Setting the OCR register to 0xDA is a good idea.
 * This means normal output mode, push-pull and the correct polarity.
 */
#define PCC_OCR			(OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL)

/*
 * In the CDR register, you should set CBP to 1.
 * You will probably also want to set the clock divider value to 7
 * (meaning direct oscillator output) because the second SJA1000 chip
 * is driven by the first one CLKOUT output.
 */
#define PCC_CDR			(CDR_CBP | CDR_CLKOUT_MASK)

struct pcan_channel {
	struct net_device *netdev;
	unsigned long prev_rx_bytes;
	unsigned long prev_tx_bytes;
};

/* PCAN-PC Card private structure */
struct pcan_pccard {
	struct pcmcia_device *pdev;
	int chan_count;
	struct pcan_channel channel[PCC_CHAN_MAX];
	u8 ccr;
	u8 fw_major;
	u8 fw_minor;
	void __iomem *ioport_addr;
	struct timer_list led_timer;
};

static struct pcmcia_device_id pcan_table[] = {
	PCMCIA_DEVICE_MANF_CARD(PCC_MANF_ID, PCC_CARD_ID),
	PCMCIA_DEVICE_NULL,
};

MODULE_DEVICE_TABLE(pcmcia, pcan_table);

static void pcan_set_leds(struct pcan_pccard *card, u8 mask, u8 state);

/*
 * start timer which controls leds state
 */
static void pcan_start_led_timer(struct pcan_pccard *card)
{
	if (!timer_pending(&card->led_timer))
		mod_timer(&card->led_timer, jiffies + HZ);
}

/*
 * stop the timer which controls leds state
 */
static void pcan_stop_led_timer(struct pcan_pccard *card)
{
	del_timer_sync(&card->led_timer);
}

/*
 * read a sja1000 register
 */
static u8 pcan_read_canreg(const struct sja1000_priv *priv, int port)
{
	return ioread8(priv->reg_base + port);
}

/*
 * write a sja1000 register
 */
static void pcan_write_canreg(const struct sja1000_priv *priv, int port, u8 v)
{
	struct pcan_pccard *card = priv->priv;
	int c = (priv->reg_base - card->ioport_addr) / PCC_CHAN_SIZE;

	/* sja1000 register changes control the leds state */
	if (port == SJA1000_MOD)
		switch (v) {
		case MOD_RM:
			/* Reset Mode: set led on */
			pcan_set_leds(card, PCC_LED(c), PCC_LED_ON);
			break;
		case 0x00:
			/* Normal Mode: led slow blinking and start led timer */
			pcan_set_leds(card, PCC_LED(c), PCC_LED_SLOW);
			pcan_start_led_timer(card);
			break;
		default:
			break;
		}

	iowrite8(v, priv->reg_base + port);
}

/*
 * read a register from the common area
 */
static u8 pcan_read_reg(struct pcan_pccard *card, int port)
{
	return ioread8(card->ioport_addr + PCC_COMN_OFF + port);
}

/*
 * write a register into the common area
 */
static void pcan_write_reg(struct pcan_pccard *card, int port, u8 v)
{
	/* cache ccr value */
	if (port == PCC_CCR) {
		if (card->ccr == v)
			return;
		card->ccr = v;
	}

	iowrite8(v, card->ioport_addr + PCC_COMN_OFF + port);
}

/*
 * check whether the card is present by checking its fw version numbers
 * against values read at probing time.
 */
static inline int pcan_pccard_present(struct pcan_pccard *card)
{
	return ((pcan_read_reg(card, PCC_FW_MAJOR) == card->fw_major) &&
		(pcan_read_reg(card, PCC_FW_MINOR) == card->fw_minor));
}

/*
 * wait for SPI engine while it is busy
 */
static int pcan_wait_spi_busy(struct pcan_pccard *card)
{
	unsigned long timeout = jiffies +
				msecs_to_jiffies(PCC_SPI_MAX_BUSY_WAIT_MS) + 1;

	/* be sure to read status at least once after sleeping */
	while (pcan_read_reg(card, PCC_CSR) & PCC_CSR_SPI_BUSY) {
		if (time_after(jiffies, timeout))
			return -EBUSY;
		schedule();
	}

	return 0;
}

/*
 * write data in device eeprom
 */
static int pcan_write_eeprom(struct pcan_pccard *card, u16 addr, u8 v)
{
	u8 status;
	int err, i;

	/* write instruction enabling write */
	pcan_write_reg(card, PCC_SPI_IR, PCC_EEP_WREN);
	err = pcan_wait_spi_busy(card);
	if (err)
		goto we_spi_err;

	/* wait until write enabled */
	for (i = 0; i < PCC_WRITE_MAX_LOOP; i++) {
		/* write instruction reading the status register */
		pcan_write_reg(card, PCC_SPI_IR, PCC_EEP_RDSR);
		err = pcan_wait_spi_busy(card);
		if (err)
			goto we_spi_err;

		/* get status register value and check write enable bit */
		status = pcan_read_reg(card, PCC_SPI_DIR);
		if (status & PCC_EEP_SR_WEN)
			break;
	}

	if (i >= PCC_WRITE_MAX_LOOP) {
		dev_err(&card->pdev->dev,
			"stop waiting to be allowed to write in eeprom\n");
		return -EIO;
	}

	/* set address and data */
	pcan_write_reg(card, PCC_SPI_ADR, addr & 0xff);
	pcan_write_reg(card, PCC_SPI_DOR, v);

	/*
	 * write instruction with bit[3] set according to address value:
	 * if addr refers to upper half of the memory array: bit[3] = 1
	 */
	pcan_write_reg(card, PCC_SPI_IR, PCC_EEP_WRITE(addr));
	err = pcan_wait_spi_busy(card);
	if (err)
		goto we_spi_err;

	/* wait while write in progress */
	for (i = 0; i < PCC_WRITE_MAX_LOOP; i++) {
		/* write instruction reading the status register */
		pcan_write_reg(card, PCC_SPI_IR, PCC_EEP_RDSR);
		err = pcan_wait_spi_busy(card);
		if (err)
			goto we_spi_err;

		/* get status register value and check write in progress bit */
		status = pcan_read_reg(card, PCC_SPI_DIR);
		if (!(status & PCC_EEP_SR_WIP))
			break;
	}

	if (i >= PCC_WRITE_MAX_LOOP) {
		dev_err(&card->pdev->dev,
			"stop waiting for write in eeprom to complete\n");
		return -EIO;
	}

	/* write instruction disabling write */
	pcan_write_reg(card, PCC_SPI_IR, PCC_EEP_WRDI);
	err = pcan_wait_spi_busy(card);
	if (err)
		goto we_spi_err;

	return 0;

we_spi_err:
	dev_err(&card->pdev->dev,
		"stop waiting (spi engine always busy) err %d\n", err);

	return err;
}

static void pcan_set_leds(struct pcan_pccard *card, u8 led_mask, u8 state)
{
	u8 ccr = card->ccr;
	int i;

	for (i = 0; i < card->chan_count; i++)
		if (led_mask & PCC_LED(i)) {
			/* clear corresponding led bits in ccr */
			ccr &= ~PCC_CCR_LED_MASK_CHAN(i);
			/* then set new bits */
			ccr |= PCC_CCR_LED_CHAN(state, i);
		}

	/* real write only if something has changed in ccr */
	pcan_write_reg(card, PCC_CCR, ccr);
}

/*
 * enable/disable CAN connectors power
 */
static inline void pcan_set_can_power(struct pcan_pccard *card, int onoff)
{
	int err;

	err = pcan_write_eeprom(card, 0, !!onoff);
	if (err)
		dev_err(&card->pdev->dev,
			"failed setting power %s to can connectors (err %d)\n",
			(onoff) ? "on" : "off", err);
}

/*
 * set leds state according to channel activity
 */
static void pcan_led_timer(struct timer_list *t)
{
	struct pcan_pccard *card = from_timer(card, t, led_timer);
	struct net_device *netdev;
	int i, up_count = 0;
	u8 ccr;

	ccr = card->ccr;
	for (i = 0; i < card->chan_count; i++) {
		/* default is: not configured */
		ccr &= ~PCC_CCR_LED_MASK_CHAN(i);
		ccr |= PCC_CCR_LED_ON_CHAN(i);

		netdev = card->channel[i].netdev;
		if (!netdev || !(netdev->flags & IFF_UP))
			continue;

		up_count++;

		/* no activity (but configured) */
		ccr &= ~PCC_CCR_LED_MASK_CHAN(i);
		ccr |= PCC_CCR_LED_SLOW_CHAN(i);

		/* if bytes counters changed, set fast blinking led */
		if (netdev->stats.rx_bytes != card->channel[i].prev_rx_bytes) {
			card->channel[i].prev_rx_bytes = netdev->stats.rx_bytes;
			ccr &= ~PCC_CCR_LED_MASK_CHAN(i);
			ccr |= PCC_CCR_LED_FAST_CHAN(i);
		}
		if (netdev->stats.tx_bytes != card->channel[i].prev_tx_bytes) {
			card->channel[i].prev_tx_bytes = netdev->stats.tx_bytes;
			ccr &= ~PCC_CCR_LED_MASK_CHAN(i);
			ccr |= PCC_CCR_LED_FAST_CHAN(i);
		}
	}

	/* write the new leds state */
	pcan_write_reg(card, PCC_CCR, ccr);

	/* restart timer (except if no more configured channels) */
	if (up_count)
		mod_timer(&card->led_timer, jiffies + HZ);
}

/*
 * interrupt service routine
 */
static irqreturn_t pcan_isr(int irq, void *dev_id)
{
	struct pcan_pccard *card = dev_id;
	int irq_handled;

	/* prevent from infinite loop */
	for (irq_handled = 0; irq_handled < PCC_ISR_MAX_LOOP; irq_handled++) {
		/* handle shared interrupt and next loop */
		int nothing_to_handle = 1;
		int i;

		/* check interrupt for each channel */
		for (i = 0; i < card->chan_count; i++) {
			struct net_device *netdev;

			/*
			 * check whether the card is present before calling
			 * sja1000_interrupt() to speed up hotplug detection
			 */
			if (!pcan_pccard_present(card)) {
				/* card unplugged during isr */
				return IRQ_NONE;
			}

			/*
			 * should check whether all or SJA1000_MAX_IRQ
			 * interrupts have been handled: loop again to be sure.
			 */
			netdev = card->channel[i].netdev;
			if (netdev &&
			    sja1000_interrupt(irq, netdev) == IRQ_HANDLED)
				nothing_to_handle = 0;
		}

		if (nothing_to_handle)
			break;
	}

	return (irq_handled) ? IRQ_HANDLED : IRQ_NONE;
}

/*
 * free all resources used by the channels and switch off leds and can power
 */
static void pcan_free_channels(struct pcan_pccard *card)
{
	int i;
	u8 led_mask = 0;

	for (i = 0; i < card->chan_count; i++) {
		struct net_device *netdev;
		char name[IFNAMSIZ];

		led_mask |= PCC_LED(i);

		netdev = card->channel[i].netdev;
		if (!netdev)
			continue;

		strlcpy(name, netdev->name, IFNAMSIZ);

		unregister_sja1000dev(netdev);

		free_sja1000dev(netdev);

		dev_info(&card->pdev->dev, "%s removed\n", name);
	}

	/* do it only if device not removed */
	if (pcan_pccard_present(card)) {
		pcan_set_leds(card, led_mask, PCC_LED_OFF);
		pcan_set_can_power(card, 0);
	}
}

/*
 * check if a CAN controller is present at the specified location
 */
static inline int pcan_channel_present(struct sja1000_priv *priv)
{
	/* make sure SJA1000 is in reset mode */
	pcan_write_canreg(priv, SJA1000_MOD, 1);
	pcan_write_canreg(priv, SJA1000_CDR, CDR_PELICAN);

	/* read reset-values */
	if (pcan_read_canreg(priv, SJA1000_CDR) == CDR_PELICAN)
		return 1;

	return 0;
}

static int pcan_add_channels(struct pcan_pccard *card)
{
	struct pcmcia_device *pdev = card->pdev;
	int i, err = 0;
	u8 ccr = PCC_CCR_INIT;

	/* init common registers (reset channels and leds off) */
	card->ccr = ~ccr;
	pcan_write_reg(card, PCC_CCR, ccr);

	/* wait 2ms before unresetting channels */
	usleep_range(2000, 3000);

	ccr &= ~PCC_CCR_RST_ALL;
	pcan_write_reg(card, PCC_CCR, ccr);

	/* create one network device per channel detected */
	for (i = 0; i < ARRAY_SIZE(card->channel); i++) {
		struct net_device *netdev;
		struct sja1000_priv *priv;

		netdev = alloc_sja1000dev(0);
		if (!netdev) {
			err = -ENOMEM;
			break;
		}

		/* update linkages */
		priv = netdev_priv(netdev);
		priv->priv = card;
		SET_NETDEV_DEV(netdev, &pdev->dev);
		netdev->dev_id = i;

		priv->irq_flags = IRQF_SHARED;
		netdev->irq = pdev->irq;
		priv->reg_base = card->ioport_addr + PCC_CHAN_OFF(i);

		/* check if channel is present */
		if (!pcan_channel_present(priv)) {
			dev_err(&pdev->dev, "channel %d not present\n", i);
			free_sja1000dev(netdev);
			continue;
		}

		priv->read_reg  = pcan_read_canreg;
		priv->write_reg = pcan_write_canreg;
		priv->can.clock.freq = PCC_CAN_CLOCK;
		priv->ocr = PCC_OCR;
		priv->cdr = PCC_CDR;

		/* Neither a slave device distributes the clock */
		if (i > 0)
			priv->cdr |= CDR_CLK_OFF;

		priv->flags |= SJA1000_CUSTOM_IRQ_HANDLER;

		/* register SJA1000 device */
		err = register_sja1000dev(netdev);
		if (err) {
			free_sja1000dev(netdev);
			continue;
		}

		card->channel[i].netdev = netdev;
		card->chan_count++;

		/* set corresponding led on in the new ccr */
		ccr &= ~PCC_CCR_LED_OFF_CHAN(i);

		dev_info(&pdev->dev,
			"%s on channel %d at 0x%p irq %d\n",
			netdev->name, i, priv->reg_base, pdev->irq);
	}

	/* write new ccr (change leds state) */
	pcan_write_reg(card, PCC_CCR, ccr);

	return err;
}

static int pcan_conf_check(struct pcmcia_device *pdev, void *priv_data)
{
	pdev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	pdev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8; /* only */
	pdev->io_lines = 10;

	/* This reserves IO space but doesn't actually enable it */
	return pcmcia_request_io(pdev);
}

/*
 * free all resources used by the device
 */
static void pcan_free(struct pcmcia_device *pdev)
{
	struct pcan_pccard *card = pdev->priv;

	if (!card)
		return;

	free_irq(pdev->irq, card);
	pcan_stop_led_timer(card);

	pcan_free_channels(card);

	ioport_unmap(card->ioport_addr);

	kfree(card);
	pdev->priv = NULL;
}

/*
 * setup PCMCIA socket and probe for PEAK-System PC-CARD
 */
static int pcan_probe(struct pcmcia_device *pdev)
{
	struct pcan_pccard *card;
	int err;

	pdev->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	err = pcmcia_loop_config(pdev, pcan_conf_check, NULL);
	if (err) {
		dev_err(&pdev->dev, "pcmcia_loop_config() error %d\n", err);
		goto probe_err_1;
	}

	if (!pdev->irq) {
		dev_err(&pdev->dev, "no irq assigned\n");
		err = -ENODEV;
		goto probe_err_1;
	}

	err = pcmcia_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pcmcia_enable_device failed err=%d\n",
			err);
		goto probe_err_1;
	}

	card = kzalloc(sizeof(struct pcan_pccard), GFP_KERNEL);
	if (!card) {
		err = -ENOMEM;
		goto probe_err_2;
	}

	card->pdev = pdev;
	pdev->priv = card;

	/* sja1000 api uses iomem */
	card->ioport_addr = ioport_map(pdev->resource[0]->start,
					resource_size(pdev->resource[0]));
	if (!card->ioport_addr) {
		dev_err(&pdev->dev, "couldn't map io port into io memory\n");
		err = -ENOMEM;
		goto probe_err_3;
	}
	card->fw_major = pcan_read_reg(card, PCC_FW_MAJOR);
	card->fw_minor = pcan_read_reg(card, PCC_FW_MINOR);

	/* display board name and firmware version */
	dev_info(&pdev->dev, "PEAK-System pcmcia card %s fw %d.%d\n",
		pdev->prod_id[1] ? pdev->prod_id[1] : "PCAN-PC Card",
		card->fw_major, card->fw_minor);

	/* detect available channels */
	pcan_add_channels(card);
	if (!card->chan_count) {
		err = -ENOMEM;
		goto probe_err_4;
	}

	/* init the timer which controls the leds */
	timer_setup(&card->led_timer, pcan_led_timer, 0);

	/* request the given irq */
	err = request_irq(pdev->irq, &pcan_isr, IRQF_SHARED, PCC_NAME, card);
	if (err) {
		dev_err(&pdev->dev, "couldn't request irq%d\n", pdev->irq);
		goto probe_err_5;
	}

	/* power on the connectors */
	pcan_set_can_power(card, 1);

	return 0;

probe_err_5:
	/* unregister can devices from network */
	pcan_free_channels(card);

probe_err_4:
	ioport_unmap(card->ioport_addr);

probe_err_3:
	kfree(card);
	pdev->priv = NULL;

probe_err_2:
	pcmcia_disable_device(pdev);

probe_err_1:
	return err;
}

/*
 * release claimed resources
 */
static void pcan_remove(struct pcmcia_device *pdev)
{
	pcan_free(pdev);
	pcmcia_disable_device(pdev);
}

static struct pcmcia_driver pcan_driver = {
	.name = PCC_NAME,
	.probe = pcan_probe,
	.remove = pcan_remove,
	.id_table = pcan_table,
};
module_pcmcia_driver(pcan_driver);
