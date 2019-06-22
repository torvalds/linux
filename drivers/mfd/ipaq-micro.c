// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compaq iPAQ h3xxx Atmel microcontroller companion support
 *
 * This is an Atmel AT90LS8535 with a special flashed-in firmware that
 * implements the special protocol used by this driver.
 *
 * based on previous kernel 2.4 version by Andrew Christian
 * Author : Alessandro Gardich <gremlin@gremlin.it>
 * Author : Dmitry Artamonow <mad_soft@inbox.ru>
 * Author : Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ipaq-micro.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/list.h>

#include <mach/hardware.h>

static void ipaq_micro_trigger_tx(struct ipaq_micro *micro)
{
	struct ipaq_micro_txdev *tx = &micro->tx;
	struct ipaq_micro_msg *msg = micro->msg;
	int i, bp;
	u8 checksum;
	u32 val;

	bp = 0;
	tx->buf[bp++] = CHAR_SOF;

	checksum = ((msg->id & 0x0f) << 4) | (msg->tx_len & 0x0f);
	tx->buf[bp++] = checksum;

	for (i = 0; i < msg->tx_len; i++) {
		tx->buf[bp++] = msg->tx_data[i];
		checksum += msg->tx_data[i];
	}

	tx->buf[bp++] = checksum;
	tx->len = bp;
	tx->index = 0;

	/* Enable interrupt */
	val = readl(micro->base + UTCR3);
	val |= UTCR3_TIE;
	writel(val, micro->base + UTCR3);
}

int ipaq_micro_tx_msg(struct ipaq_micro *micro, struct ipaq_micro_msg *msg)
{
	unsigned long flags;

	dev_dbg(micro->dev, "TX msg: %02x, %d bytes\n", msg->id, msg->tx_len);

	spin_lock_irqsave(&micro->lock, flags);
	if (micro->msg) {
		list_add_tail(&msg->node, &micro->queue);
		spin_unlock_irqrestore(&micro->lock, flags);
		return 0;
	}
	micro->msg = msg;
	ipaq_micro_trigger_tx(micro);
	spin_unlock_irqrestore(&micro->lock, flags);
	return 0;
}
EXPORT_SYMBOL(ipaq_micro_tx_msg);

static void micro_rx_msg(struct ipaq_micro *micro, u8 id, int len, u8 *data)
{
	int i;

	dev_dbg(micro->dev, "RX msg: %02x, %d bytes\n", id, len);

	spin_lock(&micro->lock);
	switch (id) {
	case MSG_VERSION:
	case MSG_EEPROM_READ:
	case MSG_EEPROM_WRITE:
	case MSG_BACKLIGHT:
	case MSG_NOTIFY_LED:
	case MSG_THERMAL_SENSOR:
	case MSG_BATTERY:
		/* Handle synchronous messages */
		if (micro->msg && micro->msg->id == id) {
			struct ipaq_micro_msg *msg = micro->msg;

			memcpy(msg->rx_data, data, len);
			msg->rx_len = len;
			complete(&micro->msg->ack);
			if (!list_empty(&micro->queue)) {
				micro->msg = list_entry(micro->queue.next,
							struct ipaq_micro_msg,
							node);
				list_del_init(&micro->msg->node);
				ipaq_micro_trigger_tx(micro);
			} else
				micro->msg = NULL;
			dev_dbg(micro->dev, "OK RX message 0x%02x\n", id);
		} else {
			dev_err(micro->dev,
				"out of band RX message 0x%02x\n", id);
			if (!micro->msg)
				dev_info(micro->dev, "no message queued\n");
			else
				dev_info(micro->dev, "expected message %02x\n",
					 micro->msg->id);
		}
		break;
	case MSG_KEYBOARD:
		if (micro->key)
			micro->key(micro->key_data, len, data);
		else
			dev_dbg(micro->dev, "key message ignored, no handle\n");
		break;
	case MSG_TOUCHSCREEN:
		if (micro->ts)
			micro->ts(micro->ts_data, len, data);
		else
			dev_dbg(micro->dev, "touchscreen message ignored, no handle\n");
		break;
	default:
		dev_err(micro->dev,
			"unknown msg %d [%d] ", id, len);
		for (i = 0; i < len; ++i)
			pr_cont("0x%02x ", data[i]);
		pr_cont("\n");
	}
	spin_unlock(&micro->lock);
}

static void micro_process_char(struct ipaq_micro *micro, u8 ch)
{
	struct ipaq_micro_rxdev *rx = &micro->rx;

	switch (rx->state) {
	case STATE_SOF:	/* Looking for SOF */
		if (ch == CHAR_SOF)
			rx->state = STATE_ID; /* Next byte is the id and len */
		break;
	case STATE_ID: /* Looking for id and len byte */
		rx->id = (ch & 0xf0) >> 4;
		rx->len = (ch & 0x0f);
		rx->index = 0;
		rx->chksum = ch;
		rx->state = (rx->len > 0) ? STATE_DATA : STATE_CHKSUM;
		break;
	case STATE_DATA: /* Looking for 'len' data bytes */
		rx->chksum += ch;
		rx->buf[rx->index] = ch;
		if (++rx->index == rx->len)
			rx->state = STATE_CHKSUM;
		break;
	case STATE_CHKSUM: /* Looking for the checksum */
		if (ch == rx->chksum)
			micro_rx_msg(micro, rx->id, rx->len, rx->buf);
		rx->state = STATE_SOF;
		break;
	}
}

static void micro_rx_chars(struct ipaq_micro *micro)
{
	u32 status, ch;

	while ((status = readl(micro->base + UTSR1)) & UTSR1_RNE) {
		ch = readl(micro->base + UTDR);
		if (status & UTSR1_PRE)
			dev_err(micro->dev, "rx: parity error\n");
		else if (status & UTSR1_FRE)
			dev_err(micro->dev, "rx: framing error\n");
		else if (status & UTSR1_ROR)
			dev_err(micro->dev, "rx: overrun error\n");
		micro_process_char(micro, ch);
	}
}

static void ipaq_micro_get_version(struct ipaq_micro *micro)
{
	struct ipaq_micro_msg msg = {
		.id = MSG_VERSION,
	};

	ipaq_micro_tx_msg_sync(micro, &msg);
	if (msg.rx_len == 4) {
		memcpy(micro->version, msg.rx_data, 4);
		micro->version[4] = '\0';
	} else if (msg.rx_len == 9) {
		memcpy(micro->version, msg.rx_data, 4);
		micro->version[4] = '\0';
		/* Bytes 4-7 are "pack", byte 8 is "boot type" */
	} else {
		dev_err(micro->dev,
			"illegal version message %d bytes\n", msg.rx_len);
	}
}

static void ipaq_micro_eeprom_read(struct ipaq_micro *micro,
				   u8 address, u8 len, u8 *data)
{
	struct ipaq_micro_msg msg = {
		.id = MSG_EEPROM_READ,
	};
	u8 i;

	for (i = 0; i < len; i++) {
		msg.tx_data[0] = address + i;
		msg.tx_data[1] = 1;
		msg.tx_len = 2;
		ipaq_micro_tx_msg_sync(micro, &msg);
		memcpy(data + (i * 2), msg.rx_data, 2);
	}
}

static char *ipaq_micro_str(u8 *wchar, u8 len)
{
	char retstr[256];
	u8 i;

	for (i = 0; i < len / 2; i++)
		retstr[i] = wchar[i * 2];
	return kstrdup(retstr, GFP_KERNEL);
}

static u16 ipaq_micro_to_u16(u8 *data)
{
	return data[1] << 8 | data[0];
}

static void __init ipaq_micro_eeprom_dump(struct ipaq_micro *micro)
{
	u8 dump[256];
	char *str;

	ipaq_micro_eeprom_read(micro, 0, 128, dump);
	str = ipaq_micro_str(dump, 10);
	if (str) {
		dev_info(micro->dev, "HW version %s\n", str);
		kfree(str);
	}
	str = ipaq_micro_str(dump+10, 40);
	if (str) {
		dev_info(micro->dev, "serial number: %s\n", str);
		/* Feed the random pool with this */
		add_device_randomness(str, strlen(str));
		kfree(str);
	}
	str = ipaq_micro_str(dump+50, 20);
	if (str) {
		dev_info(micro->dev, "module ID: %s\n", str);
		kfree(str);
	}
	str = ipaq_micro_str(dump+70, 10);
	if (str) {
		dev_info(micro->dev, "product revision: %s\n", str);
		kfree(str);
	}
	dev_info(micro->dev, "product ID: %u\n", ipaq_micro_to_u16(dump+80));
	dev_info(micro->dev, "frame rate: %u fps\n",
		 ipaq_micro_to_u16(dump+82));
	dev_info(micro->dev, "page mode: %u\n", ipaq_micro_to_u16(dump+84));
	dev_info(micro->dev, "country ID: %u\n", ipaq_micro_to_u16(dump+86));
	dev_info(micro->dev, "color display: %s\n",
		 ipaq_micro_to_u16(dump+88) ? "yes" : "no");
	dev_info(micro->dev, "ROM size: %u MiB\n", ipaq_micro_to_u16(dump+90));
	dev_info(micro->dev, "RAM size: %u KiB\n", ipaq_micro_to_u16(dump+92));
	dev_info(micro->dev, "screen: %u x %u\n",
		 ipaq_micro_to_u16(dump+94), ipaq_micro_to_u16(dump+96));
}

static void micro_tx_chars(struct ipaq_micro *micro)
{
	struct ipaq_micro_txdev *tx = &micro->tx;
	u32 val;

	while ((tx->index < tx->len) &&
	       (readl(micro->base + UTSR1) & UTSR1_TNF)) {
		writel(tx->buf[tx->index], micro->base + UTDR);
		tx->index++;
	}

	/* Stop interrupts */
	val = readl(micro->base + UTCR3);
	val &= ~UTCR3_TIE;
	writel(val, micro->base + UTCR3);
}

static void micro_reset_comm(struct ipaq_micro *micro)
{
	struct ipaq_micro_rxdev *rx = &micro->rx;
	u32 val;

	if (micro->msg)
		complete(&micro->msg->ack);

	/* Initialize Serial channel protocol frame */
	rx->state = STATE_SOF;  /* Reset the state machine */

	/* Set up interrupts */
	writel(0x01, micro->sdlc + 0x0); /* Select UART mode */

	/* Clean up CR3 */
	writel(0x0, micro->base + UTCR3);

	/* Format: 8N1 */
	writel(UTCR0_8BitData | UTCR0_1StpBit, micro->base + UTCR0);

	/* Baud rate: 115200 */
	writel(0x0, micro->base + UTCR1);
	writel(0x1, micro->base + UTCR2);

	/* Clear SR0 */
	writel(0xff, micro->base + UTSR0);

	/* Enable RX int, disable TX int */
	writel(UTCR3_TXE | UTCR3_RXE | UTCR3_RIE, micro->base + UTCR3);
	val = readl(micro->base + UTCR3);
	val &= ~UTCR3_TIE;
	writel(val, micro->base + UTCR3);
}

static irqreturn_t micro_serial_isr(int irq, void *dev_id)
{
	struct ipaq_micro *micro = dev_id;
	struct ipaq_micro_txdev *tx = &micro->tx;
	u32 status;

	status = readl(micro->base + UTSR0);
	do {
		if (status & (UTSR0_RID | UTSR0_RFS)) {
			if (status & UTSR0_RID)
				/* Clear the Receiver IDLE bit */
				writel(UTSR0_RID, micro->base + UTSR0);
			micro_rx_chars(micro);
		}

		/* Clear break bits */
		if (status & (UTSR0_RBB | UTSR0_REB))
			writel(status & (UTSR0_RBB | UTSR0_REB),
			       micro->base + UTSR0);

		if (status & UTSR0_TFS)
			micro_tx_chars(micro);

		status = readl(micro->base + UTSR0);

	} while (((tx->index < tx->len) && (status & UTSR0_TFS)) ||
		 (status & (UTSR0_RFS | UTSR0_RID)));

	return IRQ_HANDLED;
}

static const struct mfd_cell micro_cells[] = {
	{ .name = "ipaq-micro-backlight", },
	{ .name = "ipaq-micro-battery", },
	{ .name = "ipaq-micro-keys", },
	{ .name = "ipaq-micro-ts", },
	{ .name = "ipaq-micro-leds", },
};

static int __maybe_unused micro_resume(struct device *dev)
{
	struct ipaq_micro *micro = dev_get_drvdata(dev);

	micro_reset_comm(micro);
	mdelay(10);

	return 0;
}

static int __init micro_probe(struct platform_device *pdev)
{
	struct ipaq_micro *micro;
	struct resource *res;
	int ret;
	int irq;

	micro = devm_kzalloc(&pdev->dev, sizeof(*micro), GFP_KERNEL);
	if (!micro)
		return -ENOMEM;

	micro->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	micro->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(micro->base))
		return PTR_ERR(micro->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -EINVAL;

	micro->sdlc = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(micro->sdlc))
		return PTR_ERR(micro->sdlc);

	micro_reset_comm(micro);

	irq = platform_get_irq(pdev, 0);
	if (!irq)
		return -EINVAL;
	ret = devm_request_irq(&pdev->dev, irq, micro_serial_isr,
			       IRQF_SHARED, "ipaq-micro",
			       micro);
	if (ret) {
		dev_err(&pdev->dev, "unable to grab serial port IRQ\n");
		return ret;
	} else
		dev_info(&pdev->dev, "grabbed serial port IRQ\n");

	spin_lock_init(&micro->lock);
	INIT_LIST_HEAD(&micro->queue);
	platform_set_drvdata(pdev, micro);

	ret = mfd_add_devices(&pdev->dev, pdev->id, micro_cells,
			      ARRAY_SIZE(micro_cells), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "error adding MFD cells");
		return ret;
	}

	/* Check version */
	ipaq_micro_get_version(micro);
	dev_info(&pdev->dev, "Atmel micro ASIC version %s\n", micro->version);
	ipaq_micro_eeprom_dump(micro);

	return 0;
}

static const struct dev_pm_ops micro_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, micro_resume)
};

static struct platform_driver micro_device_driver = {
	.driver   = {
		.name	= "ipaq-h3xxx-micro",
		.pm	= &micro_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(micro_device_driver, micro_probe);
