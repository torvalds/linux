/*

  Driver for the Marvell 8385 based compact flash WLAN cards.

  (C) 2007 by Holger Schurig <hs4233@mail.mn-solutions.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include <linux/io.h>

#define DRV_NAME "libertas_cs"

#include "decl.h"
#include "defs.h"
#include "dev.h"


/********************************************************************/
/* Module stuff                                                     */
/********************************************************************/

MODULE_AUTHOR("Holger Schurig <hs4233@mail.mn-solutions.de>");
MODULE_DESCRIPTION("Driver for Marvell 83xx compact flash WLAN cards");
MODULE_LICENSE("GPL");



/********************************************************************/
/* Data structures                                                  */
/********************************************************************/

struct if_cs_card {
	struct pcmcia_device *p_dev;
	struct lbs_private *priv;
	void __iomem *iobase;
	bool align_regs;
	u32 model;
};


enum {
	MODEL_UNKNOWN = 0x00,
	MODEL_8305 = 0x01,
	MODEL_8381 = 0x02,
	MODEL_8385 = 0x03
};

static const struct lbs_fw_table fw_table[] = {
	{ MODEL_8305, "libertas/cf8305.bin", NULL },
	{ MODEL_8305, "libertas_cs_helper.fw", NULL },
	{ MODEL_8381, "libertas/cf8381_helper.bin", "libertas/cf8381.bin" },
	{ MODEL_8381, "libertas_cs_helper.fw", "libertas_cs.fw" },
	{ MODEL_8385, "libertas/cf8385_helper.bin", "libertas/cf8385.bin" },
	{ MODEL_8385, "libertas_cs_helper.fw", "libertas_cs.fw" },
	{ 0, NULL, NULL }
};
MODULE_FIRMWARE("libertas/cf8305.bin");
MODULE_FIRMWARE("libertas/cf8381_helper.bin");
MODULE_FIRMWARE("libertas/cf8381.bin");
MODULE_FIRMWARE("libertas/cf8385_helper.bin");
MODULE_FIRMWARE("libertas/cf8385.bin");
MODULE_FIRMWARE("libertas_cs_helper.fw");
MODULE_FIRMWARE("libertas_cs.fw");


/********************************************************************/
/* Hardware access                                                  */
/********************************************************************/

/* This define enables wrapper functions which allow you
   to dump all register accesses. You normally won't this,
   except for development */
/* #define DEBUG_IO */

#ifdef DEBUG_IO
static int debug_output = 0;
#else
/* This way the compiler optimizes the printk's away */
#define debug_output 0
#endif

static inline unsigned int if_cs_read8(struct if_cs_card *card, uint reg)
{
	unsigned int val = ioread8(card->iobase + reg);
	if (debug_output)
		printk(KERN_INFO "inb %08x<%02x\n", reg, val);
	return val;
}
static inline unsigned int if_cs_read16(struct if_cs_card *card, uint reg)
{
	unsigned int val = ioread16(card->iobase + reg);
	if (debug_output)
		printk(KERN_INFO "inw %08x<%04x\n", reg, val);
	return val;
}
static inline void if_cs_read16_rep(
	struct if_cs_card *card,
	uint reg,
	void *buf,
	unsigned long count)
{
	if (debug_output)
		printk(KERN_INFO "insw %08x<(0x%lx words)\n",
			reg, count);
	ioread16_rep(card->iobase + reg, buf, count);
}

static inline void if_cs_write8(struct if_cs_card *card, uint reg, u8 val)
{
	if (debug_output)
		printk(KERN_INFO "outb %08x>%02x\n", reg, val);
	iowrite8(val, card->iobase + reg);
}

static inline void if_cs_write16(struct if_cs_card *card, uint reg, u16 val)
{
	if (debug_output)
		printk(KERN_INFO "outw %08x>%04x\n", reg, val);
	iowrite16(val, card->iobase + reg);
}

static inline void if_cs_write16_rep(
	struct if_cs_card *card,
	uint reg,
	const void *buf,
	unsigned long count)
{
	if (debug_output)
		printk(KERN_INFO "outsw %08x>(0x%lx words)\n",
			reg, count);
	iowrite16_rep(card->iobase + reg, buf, count);
}


/*
 * I know that polling/delaying is frowned upon. However, this procedure
 * with polling is needed while downloading the firmware. At this stage,
 * the hardware does unfortunately not create any interrupts.
 *
 * Fortunately, this function is never used once the firmware is in
 * the card. :-)
 *
 * As a reference, see the "Firmware Specification v5.1", page 18
 * and 19. I did not follow their suggested timing to the word,
 * but this works nice & fast anyway.
 */
static int if_cs_poll_while_fw_download(struct if_cs_card *card, uint addr, u8 reg)
{
	int i;

	for (i = 0; i < 100000; i++) {
		u8 val = if_cs_read8(card, addr);
		if (val == reg)
			return 0;
		udelay(5);
	}
	return -ETIME;
}



/*
 * First the bitmasks for the host/card interrupt/status registers:
 */
#define IF_CS_BIT_TX			0x0001
#define IF_CS_BIT_RX			0x0002
#define IF_CS_BIT_COMMAND		0x0004
#define IF_CS_BIT_RESP			0x0008
#define IF_CS_BIT_EVENT			0x0010
#define	IF_CS_BIT_MASK			0x001f



/*
 * It's not really clear to me what the host status register is for. It
 * needs to be set almost in union with "host int cause". The following
 * bits from above are used:
 *
 *   IF_CS_BIT_TX         driver downloaded a data packet
 *   IF_CS_BIT_RX         driver got a data packet
 *   IF_CS_BIT_COMMAND    driver downloaded a command
 *   IF_CS_BIT_RESP       not used (has some meaning with powerdown)
 *   IF_CS_BIT_EVENT      driver read a host event
 */
#define IF_CS_HOST_STATUS		0x00000000

/*
 * With the host int cause register can the host (that is, Linux) cause
 * an interrupt in the firmware, to tell the firmware about those events:
 *
 *   IF_CS_BIT_TX         a data packet has been downloaded
 *   IF_CS_BIT_RX         a received data packet has retrieved
 *   IF_CS_BIT_COMMAND    a firmware block or a command has been downloaded
 *   IF_CS_BIT_RESP       not used (has some meaning with powerdown)
 *   IF_CS_BIT_EVENT      a host event (link lost etc) has been retrieved
 */
#define IF_CS_HOST_INT_CAUSE		0x00000002

/*
 * The host int mask register is used to enable/disable interrupt.  However,
 * I have the suspicion that disabled interrupts are lost.
 */
#define IF_CS_HOST_INT_MASK		0x00000004

/*
 * Used to send or receive data packets:
 */
#define IF_CS_WRITE			0x00000016
#define IF_CS_WRITE_LEN			0x00000014
#define IF_CS_READ			0x00000010
#define IF_CS_READ_LEN			0x00000024

/*
 * Used to send commands (and to send firmware block) and to
 * receive command responses:
 */
#define IF_CS_CMD			0x0000001A
#define IF_CS_CMD_LEN			0x00000018
#define IF_CS_RESP			0x00000012
#define IF_CS_RESP_LEN			0x00000030

/*
 * The card status registers shows what the card/firmware actually
 * accepts:
 *
 *   IF_CS_BIT_TX        you may send a data packet
 *   IF_CS_BIT_RX        you may retrieve a data packet
 *   IF_CS_BIT_COMMAND   you may send a command
 *   IF_CS_BIT_RESP      you may retrieve a command response
 *   IF_CS_BIT_EVENT     the card has a event for use (link lost, snr low etc)
 *
 * When reading this register several times, you will get back the same
 * results --- with one exception: the IF_CS_BIT_EVENT clear itself
 * automatically.
 *
 * Not that we don't rely on BIT_RX,_BIT_RESP or BIT_EVENT because
 * we handle this via the card int cause register.
 */
#define IF_CS_CARD_STATUS		0x00000020
#define IF_CS_CARD_STATUS_MASK		0x7f00

/*
 * The card int cause register is used by the card/firmware to notify us
 * about the following events:
 *
 *   IF_CS_BIT_TX        a data packet has successfully been sentx
 *   IF_CS_BIT_RX        a data packet has been received and can be retrieved
 *   IF_CS_BIT_COMMAND   not used
 *   IF_CS_BIT_RESP      the firmware has a command response for us
 *   IF_CS_BIT_EVENT     the card has a event for use (link lost, snr low etc)
 */
#define IF_CS_CARD_INT_CAUSE		0x00000022

/*
 * This is used to for handshaking with the card's bootloader/helper image
 * to synchronize downloading of firmware blocks.
 */
#define IF_CS_SQ_READ_LOW		0x00000028
#define IF_CS_SQ_HELPER_OK		0x10

/*
 * The scratch register tells us ...
 *
 * IF_CS_SCRATCH_BOOT_OK     the bootloader runs
 * IF_CS_SCRATCH_HELPER_OK   the helper firmware already runs
 */
#define IF_CS_SCRATCH			0x0000003F
#define IF_CS_SCRATCH_BOOT_OK		0x00
#define IF_CS_SCRATCH_HELPER_OK		0x5a

/*
 * Used to detect ancient chips:
 */
#define IF_CS_PRODUCT_ID		0x0000001C
#define IF_CS_CF8385_B1_REV		0x12
#define IF_CS_CF8381_B3_REV		0x04
#define IF_CS_CF8305_B1_REV		0x03

/*
 * Used to detect other cards than CF8385 since their revisions of silicon
 * doesn't match those from CF8385, eg. CF8381 B3 works with this driver.
 */
#define CF8305_MANFID		0x02db
#define CF8305_CARDID		0x8103
#define CF8381_MANFID		0x02db
#define CF8381_CARDID		0x6064
#define CF8385_MANFID		0x02df
#define CF8385_CARDID		0x8103

/*
 * FIXME: just use the 'driver_info' field of 'struct pcmcia_device_id' when
 * that gets fixed.  Currently there's no way to access it from the probe hook.
 */
static inline u32 get_model(u16 manf_id, u16 card_id)
{
	/* NOTE: keep in sync with if_cs_ids */
	if (manf_id == CF8305_MANFID && card_id == CF8305_CARDID)
		return MODEL_8305;
	else if (manf_id == CF8381_MANFID && card_id == CF8381_CARDID)
		return MODEL_8381;
	else if (manf_id == CF8385_MANFID && card_id == CF8385_CARDID)
		return MODEL_8385;
	return MODEL_UNKNOWN;
}

/********************************************************************/
/* I/O and interrupt handling                                       */
/********************************************************************/

static inline void if_cs_enable_ints(struct if_cs_card *card)
{
	if_cs_write16(card, IF_CS_HOST_INT_MASK, 0);
}

static inline void if_cs_disable_ints(struct if_cs_card *card)
{
	if_cs_write16(card, IF_CS_HOST_INT_MASK, IF_CS_BIT_MASK);
}

/*
 * Called from if_cs_host_to_card to send a command to the hardware
 */
static int if_cs_send_cmd(struct lbs_private *priv, u8 *buf, u16 nb)
{
	struct if_cs_card *card = (struct if_cs_card *)priv->card;
	int ret = -1;
	int loops = 0;

	if_cs_disable_ints(card);

	/* Is hardware ready? */
	while (1) {
		u16 status = if_cs_read16(card, IF_CS_CARD_STATUS);
		if (status & IF_CS_BIT_COMMAND)
			break;
		if (++loops > 100) {
			netdev_err(priv->dev, "card not ready for commands\n");
			goto done;
		}
		mdelay(1);
	}

	if_cs_write16(card, IF_CS_CMD_LEN, nb);

	if_cs_write16_rep(card, IF_CS_CMD, buf, nb / 2);
	/* Are we supposed to transfer an odd amount of bytes? */
	if (nb & 1)
		if_cs_write8(card, IF_CS_CMD, buf[nb-1]);

	/* "Assert the download over interrupt command in the Host
	 * status register" */
	if_cs_write16(card, IF_CS_HOST_STATUS, IF_CS_BIT_COMMAND);

	/* "Assert the download over interrupt command in the Card
	 * interrupt case register" */
	if_cs_write16(card, IF_CS_HOST_INT_CAUSE, IF_CS_BIT_COMMAND);
	ret = 0;

done:
	if_cs_enable_ints(card);
	return ret;
}

/*
 * Called from if_cs_host_to_card to send a data to the hardware
 */
static void if_cs_send_data(struct lbs_private *priv, u8 *buf, u16 nb)
{
	struct if_cs_card *card = (struct if_cs_card *)priv->card;
	u16 status;

	if_cs_disable_ints(card);

	status = if_cs_read16(card, IF_CS_CARD_STATUS);
	BUG_ON((status & IF_CS_BIT_TX) == 0);

	if_cs_write16(card, IF_CS_WRITE_LEN, nb);

	/* write even number of bytes, then odd byte if necessary */
	if_cs_write16_rep(card, IF_CS_WRITE, buf, nb / 2);
	if (nb & 1)
		if_cs_write8(card, IF_CS_WRITE, buf[nb-1]);

	if_cs_write16(card, IF_CS_HOST_STATUS, IF_CS_BIT_TX);
	if_cs_write16(card, IF_CS_HOST_INT_CAUSE, IF_CS_BIT_TX);
	if_cs_enable_ints(card);
}

/*
 * Get the command result out of the card.
 */
static int if_cs_receive_cmdres(struct lbs_private *priv, u8 *data, u32 *len)
{
	unsigned long flags;
	int ret = -1;
	u16 status;

	/* is hardware ready? */
	status = if_cs_read16(priv->card, IF_CS_CARD_STATUS);
	if ((status & IF_CS_BIT_RESP) == 0) {
		netdev_err(priv->dev, "no cmd response in card\n");
		*len = 0;
		goto out;
	}

	*len = if_cs_read16(priv->card, IF_CS_RESP_LEN);
	if ((*len == 0) || (*len > LBS_CMD_BUFFER_SIZE)) {
		netdev_err(priv->dev,
			   "card cmd buffer has invalid # of bytes (%d)\n",
			   *len);
		goto out;
	}

	/* read even number of bytes, then odd byte if necessary */
	if_cs_read16_rep(priv->card, IF_CS_RESP, data, *len/sizeof(u16));
	if (*len & 1)
		data[*len-1] = if_cs_read8(priv->card, IF_CS_RESP);

	/* This is a workaround for a firmware that reports too much
	 * bytes */
	*len -= 8;
	ret = 0;

	/* Clear this flag again */
	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->dnld_sent = DNLD_RES_RECEIVED;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

out:
	return ret;
}

static struct sk_buff *if_cs_receive_data(struct lbs_private *priv)
{
	struct sk_buff *skb = NULL;
	u16 len;
	u8 *data;

	len = if_cs_read16(priv->card, IF_CS_READ_LEN);
	if (len == 0 || len > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE) {
		netdev_err(priv->dev,
			   "card data buffer has invalid # of bytes (%d)\n",
			   len);
		priv->dev->stats.rx_dropped++;
		goto dat_err;
	}

	skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE + 2);
	if (!skb)
		goto out;
	skb_put(skb, len);
	skb_reserve(skb, 2);/* 16 byte align */
	data = skb->data;

	/* read even number of bytes, then odd byte if necessary */
	if_cs_read16_rep(priv->card, IF_CS_READ, data, len/sizeof(u16));
	if (len & 1)
		data[len-1] = if_cs_read8(priv->card, IF_CS_READ);

dat_err:
	if_cs_write16(priv->card, IF_CS_HOST_STATUS, IF_CS_BIT_RX);
	if_cs_write16(priv->card, IF_CS_HOST_INT_CAUSE, IF_CS_BIT_RX);

out:
	return skb;
}

static irqreturn_t if_cs_interrupt(int irq, void *data)
{
	struct if_cs_card *card = data;
	struct lbs_private *priv = card->priv;
	u16 cause;

	/* Ask card interrupt cause register if there is something for us */
	cause = if_cs_read16(card, IF_CS_CARD_INT_CAUSE);
	lbs_deb_cs("cause 0x%04x\n", cause);

	if (cause == 0) {
		/* Not for us */
		return IRQ_NONE;
	}

	if (cause == 0xffff) {
		/* Read in junk, the card has probably been removed */
		card->priv->surpriseremoved = 1;
		return IRQ_HANDLED;
	}

	if (cause & IF_CS_BIT_RX) {
		struct sk_buff *skb;
		lbs_deb_cs("rx packet\n");
		skb = if_cs_receive_data(priv);
		if (skb)
			lbs_process_rxed_packet(priv, skb);
	}

	if (cause & IF_CS_BIT_TX) {
		lbs_deb_cs("tx done\n");
		lbs_host_to_card_done(priv);
	}

	if (cause & IF_CS_BIT_RESP) {
		unsigned long flags;
		u8 i;

		lbs_deb_cs("cmd resp\n");
		spin_lock_irqsave(&priv->driver_lock, flags);
		i = (priv->resp_idx == 0) ? 1 : 0;
		spin_unlock_irqrestore(&priv->driver_lock, flags);

		BUG_ON(priv->resp_len[i]);
		if_cs_receive_cmdres(priv, priv->resp_buf[i],
			&priv->resp_len[i]);

		spin_lock_irqsave(&priv->driver_lock, flags);
		lbs_notify_command_response(priv, i);
		spin_unlock_irqrestore(&priv->driver_lock, flags);
	}

	if (cause & IF_CS_BIT_EVENT) {
		u16 status = if_cs_read16(priv->card, IF_CS_CARD_STATUS);
		if_cs_write16(priv->card, IF_CS_HOST_INT_CAUSE,
			IF_CS_BIT_EVENT);
		lbs_queue_event(priv, (status & IF_CS_CARD_STATUS_MASK) >> 8);
	}

	/* Clear interrupt cause */
	if_cs_write16(card, IF_CS_CARD_INT_CAUSE, cause & IF_CS_BIT_MASK);

	return IRQ_HANDLED;
}




/********************************************************************/
/* Firmware                                                         */
/********************************************************************/

/*
 * Tries to program the helper firmware.
 *
 * Return 0 on success
 */
static int if_cs_prog_helper(struct if_cs_card *card, const struct firmware *fw)
{
	int ret = 0;
	int sent = 0;
	u8  scratch;

	/*
	 * This is the only place where an unaligned register access happens on
	 * the CF8305 card, therefore for the sake of speed of the driver, we do
	 * the alignment correction here.
	 */
	if (card->align_regs)
		scratch = if_cs_read16(card, IF_CS_SCRATCH) >> 8;
	else
		scratch = if_cs_read8(card, IF_CS_SCRATCH);

	/* "If the value is 0x5a, the firmware is already
	 * downloaded successfully"
	 */
	if (scratch == IF_CS_SCRATCH_HELPER_OK)
		goto done;

	/* "If the value is != 00, it is invalid value of register */
	if (scratch != IF_CS_SCRATCH_BOOT_OK) {
		ret = -ENODEV;
		goto done;
	}

	lbs_deb_cs("helper size %td\n", fw->size);

	/* "Set the 5 bytes of the helper image to 0" */
	/* Not needed, this contains an ARM branch instruction */

	for (;;) {
		/* "the number of bytes to send is 256" */
		int count = 256;
		int remain = fw->size - sent;

		if (remain < count)
			count = remain;

		/*
		 * "write the number of bytes to be sent to the I/O Command
		 * write length register"
		 */
		if_cs_write16(card, IF_CS_CMD_LEN, count);

		/* "write this to I/O Command port register as 16 bit writes */
		if (count)
			if_cs_write16_rep(card, IF_CS_CMD,
				&fw->data[sent],
				count >> 1);

		/*
		 * "Assert the download over interrupt command in the Host
		 * status register"
		 */
		if_cs_write8(card, IF_CS_HOST_STATUS, IF_CS_BIT_COMMAND);

		/*
		 * "Assert the download over interrupt command in the Card
		 * interrupt case register"
		 */
		if_cs_write16(card, IF_CS_HOST_INT_CAUSE, IF_CS_BIT_COMMAND);

		/*
		 * "The host polls the Card Status register ... for 50 ms before
		 * declaring a failure"
		 */
		ret = if_cs_poll_while_fw_download(card, IF_CS_CARD_STATUS,
			IF_CS_BIT_COMMAND);
		if (ret < 0) {
			pr_err("can't download helper at 0x%x, ret %d\n",
			       sent, ret);
			goto done;
		}

		if (count == 0)
			break;

		sent += count;
	}

done:
	return ret;
}


static int if_cs_prog_real(struct if_cs_card *card, const struct firmware *fw)
{
	int ret = 0;
	int retry = 0;
	int len = 0;
	int sent;

	lbs_deb_cs("fw size %td\n", fw->size);

	ret = if_cs_poll_while_fw_download(card, IF_CS_SQ_READ_LOW,
		IF_CS_SQ_HELPER_OK);
	if (ret < 0) {
		pr_err("helper firmware doesn't answer\n");
		goto done;
	}

	for (sent = 0; sent < fw->size; sent += len) {
		len = if_cs_read16(card, IF_CS_SQ_READ_LOW);
		if (len & 1) {
			retry++;
			pr_info("odd, need to retry this firmware block\n");
		} else {
			retry = 0;
		}

		if (retry > 20) {
			pr_err("could not download firmware\n");
			ret = -ENODEV;
			goto done;
		}
		if (retry) {
			sent -= len;
		}


		if_cs_write16(card, IF_CS_CMD_LEN, len);

		if_cs_write16_rep(card, IF_CS_CMD,
			&fw->data[sent],
			(len+1) >> 1);
		if_cs_write8(card, IF_CS_HOST_STATUS, IF_CS_BIT_COMMAND);
		if_cs_write16(card, IF_CS_HOST_INT_CAUSE, IF_CS_BIT_COMMAND);

		ret = if_cs_poll_while_fw_download(card, IF_CS_CARD_STATUS,
			IF_CS_BIT_COMMAND);
		if (ret < 0) {
			pr_err("can't download firmware at 0x%x\n", sent);
			goto done;
		}
	}

	ret = if_cs_poll_while_fw_download(card, IF_CS_SCRATCH, 0x5a);
	if (ret < 0)
		pr_err("firmware download failed\n");

done:
	return ret;
}

static void if_cs_prog_firmware(struct lbs_private *priv, int ret,
				 const struct firmware *helper,
				 const struct firmware *mainfw)
{
	struct if_cs_card *card = priv->card;

	if (ret) {
		pr_err("failed to find firmware (%d)\n", ret);
		return;
	}

	/* Load the firmware */
	ret = if_cs_prog_helper(card, helper);
	if (ret == 0 && (card->model != MODEL_8305))
		ret = if_cs_prog_real(card, mainfw);
	if (ret)
		return;

	/* Now actually get the IRQ */
	ret = request_irq(card->p_dev->irq, if_cs_interrupt,
		IRQF_SHARED, DRV_NAME, card);
	if (ret) {
		pr_err("error in request_irq\n");
		return;
	}

	/*
	 * Clear any interrupt cause that happened while sending
	 * firmware/initializing card
	 */
	if_cs_write16(card, IF_CS_CARD_INT_CAUSE, IF_CS_BIT_MASK);
	if_cs_enable_ints(card);

	/* And finally bring the card up */
	priv->fw_ready = 1;
	if (lbs_start_card(priv) != 0) {
		pr_err("could not activate card\n");
		free_irq(card->p_dev->irq, card);
	}
}


/********************************************************************/
/* Callback functions for libertas.ko                               */
/********************************************************************/

/* Send commands or data packets to the card */
static int if_cs_host_to_card(struct lbs_private *priv,
	u8 type,
	u8 *buf,
	u16 nb)
{
	int ret = -1;

	switch (type) {
	case MVMS_DAT:
		priv->dnld_sent = DNLD_DATA_SENT;
		if_cs_send_data(priv, buf, nb);
		ret = 0;
		break;
	case MVMS_CMD:
		priv->dnld_sent = DNLD_CMD_SENT;
		ret = if_cs_send_cmd(priv, buf, nb);
		break;
	default:
		netdev_err(priv->dev, "%s: unsupported type %d\n",
			   __func__, type);
	}

	return ret;
}


static void if_cs_release(struct pcmcia_device *p_dev)
{
	struct if_cs_card *card = p_dev->priv;

	free_irq(p_dev->irq, card);
	pcmcia_disable_device(p_dev);
	if (card->iobase)
		ioport_unmap(card->iobase);
}


static int if_cs_ioprobe(struct pcmcia_device *p_dev, void *priv_data)
{
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_AUTO;

	if (p_dev->resource[1]->end) {
		pr_err("wrong CIS (check number of IO windows)\n");
		return -ENODEV;
	}

	/* This reserves IO space but doesn't actually enable it */
	return pcmcia_request_io(p_dev);
}

static int if_cs_probe(struct pcmcia_device *p_dev)
{
	int ret = -ENOMEM;
	unsigned int prod_id;
	struct lbs_private *priv;
	struct if_cs_card *card;

	card = kzalloc(sizeof(struct if_cs_card), GFP_KERNEL);
	if (!card)
		goto out;

	card->p_dev = p_dev;
	p_dev->priv = card;

	p_dev->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	if (pcmcia_loop_config(p_dev, if_cs_ioprobe, NULL)) {
		pr_err("error in pcmcia_loop_config\n");
		goto out1;
	}

	/*
	 * Allocate an interrupt line.  Note that this does not assign
	 * a handler to the interrupt, unless the 'Handler' member of
	 * the irq structure is initialized.
	 */
	if (!p_dev->irq)
		goto out1;

	/* Initialize io access */
	card->iobase = ioport_map(p_dev->resource[0]->start,
				resource_size(p_dev->resource[0]));
	if (!card->iobase) {
		pr_err("error in ioport_map\n");
		ret = -EIO;
		goto out1;
	}

	ret = pcmcia_enable_device(p_dev);
	if (ret) {
		pr_err("error in pcmcia_enable_device\n");
		goto out2;
	}

	/* Finally, report what we've done */
	lbs_deb_cs("irq %d, io %pR", p_dev->irq, p_dev->resource[0]);

	/*
	 * Most of the libertas cards can do unaligned register access, but some
	 * weird ones cannot. That's especially true for the CF8305 card.
	 */
	card->align_regs = false;

	card->model = get_model(p_dev->manf_id, p_dev->card_id);
	if (card->model == MODEL_UNKNOWN) {
		pr_err("unsupported manf_id 0x%04x / card_id 0x%04x\n",
		       p_dev->manf_id, p_dev->card_id);
		ret = -ENODEV;
		goto out2;
	}

	/* Check if we have a current silicon */
	prod_id = if_cs_read8(card, IF_CS_PRODUCT_ID);
	if (card->model == MODEL_8305) {
		card->align_regs = true;
		if (prod_id < IF_CS_CF8305_B1_REV) {
			pr_err("8305 rev B0 and older are not supported\n");
			ret = -ENODEV;
			goto out2;
		}
	}

	if ((card->model == MODEL_8381) && prod_id < IF_CS_CF8381_B3_REV) {
		pr_err("8381 rev B2 and older are not supported\n");
		ret = -ENODEV;
		goto out2;
	}

	if ((card->model == MODEL_8385) && prod_id < IF_CS_CF8385_B1_REV) {
		pr_err("8385 rev B0 and older are not supported\n");
		ret = -ENODEV;
		goto out2;
	}

	/* Make this card known to the libertas driver */
	priv = lbs_add_card(card, &p_dev->dev);
	if (!priv) {
		ret = -ENOMEM;
		goto out2;
	}

	/* Set up fields in lbs_private */
	card->priv = priv;
	priv->card = card;
	priv->hw_host_to_card = if_cs_host_to_card;
	priv->enter_deep_sleep = NULL;
	priv->exit_deep_sleep = NULL;
	priv->reset_deep_sleep_wakeup = NULL;

	/* Get firmware */
	ret = lbs_get_firmware_async(priv, &p_dev->dev, card->model, fw_table,
				     if_cs_prog_firmware);
	if (ret) {
		pr_err("failed to find firmware (%d)\n", ret);
		goto out3;
	}

	goto out;

out3:
	lbs_remove_card(priv);
out2:
	ioport_unmap(card->iobase);
out1:
	pcmcia_disable_device(p_dev);
out:
	return ret;
}


static void if_cs_detach(struct pcmcia_device *p_dev)
{
	struct if_cs_card *card = p_dev->priv;

	lbs_stop_card(card->priv);
	lbs_remove_card(card->priv);
	if_cs_disable_ints(card);
	if_cs_release(p_dev);
	kfree(card);
}



/********************************************************************/
/* Module initialization                                            */
/********************************************************************/

static const struct pcmcia_device_id if_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(CF8305_MANFID, CF8305_CARDID),
	PCMCIA_DEVICE_MANF_CARD(CF8381_MANFID, CF8381_CARDID),
	PCMCIA_DEVICE_MANF_CARD(CF8385_MANFID, CF8385_CARDID),
	/* NOTE: keep in sync with get_model() */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, if_cs_ids);

static struct pcmcia_driver lbs_driver = {
	.owner		= THIS_MODULE,
	.name		= DRV_NAME,
	.probe		= if_cs_probe,
	.remove		= if_cs_detach,
	.id_table       = if_cs_ids,
};
module_pcmcia_driver(lbs_driver);
