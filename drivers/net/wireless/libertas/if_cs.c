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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
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
};



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
		printk(KERN_INFO "##inb %08x<%02x\n", reg, val);
	return val;
}
static inline unsigned int if_cs_read16(struct if_cs_card *card, uint reg)
{
	unsigned int val = ioread16(card->iobase + reg);
	if (debug_output)
		printk(KERN_INFO "##inw %08x<%04x\n", reg, val);
	return val;
}
static inline void if_cs_read16_rep(
	struct if_cs_card *card,
	uint reg,
	void *buf,
	unsigned long count)
{
	if (debug_output)
		printk(KERN_INFO "##insw %08x<(0x%lx words)\n",
			reg, count);
	ioread16_rep(card->iobase + reg, buf, count);
}

static inline void if_cs_write8(struct if_cs_card *card, uint reg, u8 val)
{
	if (debug_output)
		printk(KERN_INFO "##outb %08x>%02x\n", reg, val);
	iowrite8(val, card->iobase + reg);
}

static inline void if_cs_write16(struct if_cs_card *card, uint reg, u16 val)
{
	if (debug_output)
		printk(KERN_INFO "##outw %08x>%04x\n", reg, val);
	iowrite16(val, card->iobase + reg);
}

static inline void if_cs_write16_rep(
	struct if_cs_card *card,
	uint reg,
	void *buf,
	unsigned long count)
{
	if (debug_output)
		printk(KERN_INFO "##outsw %08x>(0x%lx words)\n",
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

	for (i = 0; i < 1000; i++) {
		u8 val = if_cs_read8(card, addr);
		if (val == reg)
			return i;
		udelay(500);
	}
	return -ETIME;
}



/* Host control registers and their bit definitions */

#define IF_CS_H_STATUS			0x00000000
#define IF_CS_H_STATUS_TX_OVER		0x0001
#define IF_CS_H_STATUS_RX_OVER		0x0002
#define IF_CS_H_STATUS_DNLD_OVER	0x0004

#define IF_CS_H_INT_CAUSE		0x00000002
#define IF_CS_H_IC_TX_OVER		0x0001
#define IF_CS_H_IC_RX_OVER		0x0002
#define IF_CS_H_IC_DNLD_OVER		0x0004
#define IF_CS_H_IC_POWER_DOWN		0x0008
#define IF_CS_H_IC_HOST_EVENT		0x0010
#define IF_CS_H_IC_MASK			0x001f

#define IF_CS_H_INT_MASK		0x00000004
#define	IF_CS_H_IM_MASK			0x001f

#define IF_CS_H_WRITE_LEN		0x00000014

#define IF_CS_H_WRITE			0x00000016

#define IF_CS_H_CMD_LEN			0x00000018

#define IF_CS_H_CMD			0x0000001A

#define IF_CS_C_READ_LEN		0x00000024

#define IF_CS_H_READ			0x00000010

/* Card control registers and their bit definitions */

#define IF_CS_C_STATUS			0x00000020
#define IF_CS_C_S_TX_DNLD_RDY		0x0001
#define IF_CS_C_S_RX_UPLD_RDY		0x0002
#define IF_CS_C_S_CMD_DNLD_RDY		0x0004
#define IF_CS_C_S_CMD_UPLD_RDY		0x0008
#define IF_CS_C_S_CARDEVENT		0x0010
#define IF_CS_C_S_MASK			0x001f
#define IF_CS_C_S_STATUS_MASK		0x7f00
/* The following definitions should be the same as the MRVDRV_ ones */

#if MRVDRV_CMD_DNLD_RDY != IF_CS_C_S_CMD_DNLD_RDY
#error MRVDRV_CMD_DNLD_RDY and IF_CS_C_S_CMD_DNLD_RDY not in sync
#endif
#if MRVDRV_CMD_UPLD_RDY != IF_CS_C_S_CMD_UPLD_RDY
#error MRVDRV_CMD_UPLD_RDY and IF_CS_C_S_CMD_UPLD_RDY not in sync
#endif
#if MRVDRV_CARDEVENT != IF_CS_C_S_CARDEVENT
#error MRVDRV_CARDEVENT and IF_CS_C_S_CARDEVENT not in sync
#endif

#define IF_CS_C_INT_CAUSE		0x00000022
#define	IF_CS_C_IC_MASK			0x001f

#define IF_CS_C_SQ_READ_LOW		0x00000028
#define IF_CS_C_SQ_HELPER_OK		0x10

#define IF_CS_C_CMD_LEN			0x00000030

#define IF_CS_C_CMD			0x00000012

#define IF_CS_SCRATCH			0x0000003F



/********************************************************************/
/* Interrupts                                                       */
/********************************************************************/

static inline void if_cs_enable_ints(struct if_cs_card *card)
{
	lbs_deb_enter(LBS_DEB_CS);
	if_cs_write16(card, IF_CS_H_INT_MASK, 0);
}

static inline void if_cs_disable_ints(struct if_cs_card *card)
{
	lbs_deb_enter(LBS_DEB_CS);
	if_cs_write16(card, IF_CS_H_INT_MASK, IF_CS_H_IM_MASK);
}

static irqreturn_t if_cs_interrupt(int irq, void *data)
{
	struct if_cs_card *card = data;
	u16 int_cause;

	lbs_deb_enter(LBS_DEB_CS);

	int_cause = if_cs_read16(card, IF_CS_C_INT_CAUSE);
	if (int_cause == 0x0) {
		/* Not for us */
		return IRQ_NONE;

	} else if (int_cause == 0xffff) {
		/* Read in junk, the card has probably been removed */
		card->priv->surpriseremoved = 1;
		return IRQ_HANDLED;
	} else {
		if (int_cause & IF_CS_H_IC_TX_OVER)
			lbs_host_to_card_done(card->priv);

		/* clear interrupt */
		if_cs_write16(card, IF_CS_C_INT_CAUSE, int_cause & IF_CS_C_IC_MASK);
	}
	spin_lock(&card->priv->driver_lock);
	lbs_interrupt(card->priv);
	spin_unlock(&card->priv->driver_lock);

	return IRQ_HANDLED;
}




/********************************************************************/
/* I/O                                                              */
/********************************************************************/

/*
 * Called from if_cs_host_to_card to send a command to the hardware
 */
static int if_cs_send_cmd(struct lbs_private *priv, u8 *buf, u16 nb)
{
	struct if_cs_card *card = (struct if_cs_card *)priv->card;
	int ret = -1;
	int loops = 0;

	lbs_deb_enter(LBS_DEB_CS);

	/* Is hardware ready? */
	while (1) {
		u16 val = if_cs_read16(card, IF_CS_C_STATUS);
		if (val & IF_CS_C_S_CMD_DNLD_RDY)
			break;
		if (++loops > 100) {
			lbs_pr_err("card not ready for commands\n");
			goto done;
		}
		mdelay(1);
	}

	if_cs_write16(card, IF_CS_H_CMD_LEN, nb);

	if_cs_write16_rep(card, IF_CS_H_CMD, buf, nb / 2);
	/* Are we supposed to transfer an odd amount of bytes? */
	if (nb & 1)
		if_cs_write8(card, IF_CS_H_CMD, buf[nb-1]);

	/* "Assert the download over interrupt command in the Host
	 * status register" */
	if_cs_write16(card, IF_CS_H_STATUS, IF_CS_H_STATUS_DNLD_OVER);

	/* "Assert the download over interrupt command in the Card
	 * interrupt case register" */
	if_cs_write16(card, IF_CS_H_INT_CAUSE, IF_CS_H_IC_DNLD_OVER);
	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %d", ret);
	return ret;
}


/*
 * Called from if_cs_host_to_card to send a data to the hardware
 */
static void if_cs_send_data(struct lbs_private *priv, u8 *buf, u16 nb)
{
	struct if_cs_card *card = (struct if_cs_card *)priv->card;

	lbs_deb_enter(LBS_DEB_CS);

	if_cs_write16(card, IF_CS_H_WRITE_LEN, nb);

	/* write even number of bytes, then odd byte if necessary */
	if_cs_write16_rep(card, IF_CS_H_WRITE, buf, nb / 2);
	if (nb & 1)
		if_cs_write8(card, IF_CS_H_WRITE, buf[nb-1]);

	if_cs_write16(card, IF_CS_H_STATUS, IF_CS_H_STATUS_TX_OVER);
	if_cs_write16(card, IF_CS_H_INT_CAUSE, IF_CS_H_STATUS_TX_OVER);

	lbs_deb_leave(LBS_DEB_CS);
}


/*
 * Get the command result out of the card.
 */
static int if_cs_receive_cmdres(struct lbs_private *priv, u8 *data, u32 *len)
{
	int ret = -1;
	u16 val;

	lbs_deb_enter(LBS_DEB_CS);

	/* is hardware ready? */
	val = if_cs_read16(priv->card, IF_CS_C_STATUS);
	if ((val & IF_CS_C_S_CMD_UPLD_RDY) == 0) {
		lbs_pr_err("card not ready for CMD\n");
		goto out;
	}

	*len = if_cs_read16(priv->card, IF_CS_C_CMD_LEN);
	if ((*len == 0) || (*len > LBS_CMD_BUFFER_SIZE)) {
		lbs_pr_err("card cmd buffer has invalid # of bytes (%d)\n", *len);
		goto out;
	}

	/* read even number of bytes, then odd byte if necessary */
	if_cs_read16_rep(priv->card, IF_CS_C_CMD, data, *len/sizeof(u16));
	if (*len & 1)
		data[*len-1] = if_cs_read8(priv->card, IF_CS_C_CMD);

	/* This is a workaround for a firmware that reports too much
	 * bytes */
	*len -= 8;
	ret = 0;
out:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %d, len %d", ret, *len);
	return ret;
}


static struct sk_buff *if_cs_receive_data(struct lbs_private *priv)
{
	struct sk_buff *skb = NULL;
	u16 len;
	u8 *data;

	lbs_deb_enter(LBS_DEB_CS);

	len = if_cs_read16(priv->card, IF_CS_C_READ_LEN);
	if (len == 0 || len > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE) {
		lbs_pr_err("card data buffer has invalid # of bytes (%d)\n", len);
		priv->stats.rx_dropped++;
		printk(KERN_INFO "##HS %s:%d TODO\n", __FUNCTION__, __LINE__);
		goto dat_err;
	}

	//TODO: skb = dev_alloc_skb(len+ETH_FRAME_LEN+MRVDRV_SNAP_HEADER_LEN+EXTRA_LEN);
	skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE + 2);
	if (!skb)
		goto out;
	skb_put(skb, len);
	skb_reserve(skb, 2);/* 16 byte align */
	data = skb->data;

	/* read even number of bytes, then odd byte if necessary */
	if_cs_read16_rep(priv->card, IF_CS_H_READ, data, len/sizeof(u16));
	if (len & 1)
		data[len-1] = if_cs_read8(priv->card, IF_CS_H_READ);

dat_err:
	if_cs_write16(priv->card, IF_CS_H_STATUS, IF_CS_H_STATUS_RX_OVER);
	if_cs_write16(priv->card, IF_CS_H_INT_CAUSE, IF_CS_H_IC_RX_OVER);

out:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %p", skb);
	return skb;
}



/********************************************************************/
/* Firmware                                                         */
/********************************************************************/

/*
 * Tries to program the helper firmware.
 *
 * Return 0 on success
 */
static int if_cs_prog_helper(struct if_cs_card *card)
{
	int ret = 0;
	int sent = 0;
	u8  scratch;
	const struct firmware *fw;

	lbs_deb_enter(LBS_DEB_CS);

	scratch = if_cs_read8(card, IF_CS_SCRATCH);

	/* "If the value is 0x5a, the firmware is already
	 * downloaded successfully"
	 */
	if (scratch == 0x5a)
		goto done;

	/* "If the value is != 00, it is invalid value of register */
	if (scratch != 0x00) {
		ret = -ENODEV;
		goto done;
	}

	/* TODO: make firmware file configurable */
	ret = request_firmware(&fw, "libertas_cs_helper.fw",
		&handle_to_dev(card->p_dev));
	if (ret) {
		lbs_pr_err("can't load helper firmware\n");
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
		/* printk(KERN_INFO "//HS %d loading %d of %d bytes\n",
			__LINE__, sent, fw->size); */

		/* "write the number of bytes to be sent to the I/O Command
		 * write length register" */
		if_cs_write16(card, IF_CS_H_CMD_LEN, count);

		/* "write this to I/O Command port register as 16 bit writes */
		if (count)
			if_cs_write16_rep(card, IF_CS_H_CMD,
				&fw->data[sent],
				count >> 1);

		/* "Assert the download over interrupt command in the Host
		 * status register" */
		if_cs_write8(card, IF_CS_H_STATUS, IF_CS_H_STATUS_DNLD_OVER);

		/* "Assert the download over interrupt command in the Card
		 * interrupt case register" */
		if_cs_write16(card, IF_CS_H_INT_CAUSE, IF_CS_H_IC_DNLD_OVER);

		/* "The host polls the Card Status register ... for 50 ms before
		   declaring a failure */
		ret = if_cs_poll_while_fw_download(card, IF_CS_C_STATUS,
			IF_CS_C_S_CMD_DNLD_RDY);
		if (ret < 0) {
			lbs_pr_err("can't download helper at 0x%x, ret %d\n",
				sent, ret);
			goto done;
		}

		if (count == 0)
			break;

		sent += count;
	}

	release_firmware(fw);
	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %d", ret);
	return ret;
}


static int if_cs_prog_real(struct if_cs_card *card)
{
	const struct firmware *fw;
	int ret = 0;
	int retry = 0;
	int len = 0;
	int sent;

	lbs_deb_enter(LBS_DEB_CS);

	/* TODO: make firmware file configurable */
	ret = request_firmware(&fw, "libertas_cs.fw",
		&handle_to_dev(card->p_dev));
	if (ret) {
		lbs_pr_err("can't load firmware\n");
		ret = -ENODEV;
		goto done;
	}
	lbs_deb_cs("fw size %td\n", fw->size);

	ret = if_cs_poll_while_fw_download(card, IF_CS_C_SQ_READ_LOW, IF_CS_C_SQ_HELPER_OK);
	if (ret < 0) {
		int i;
		lbs_pr_err("helper firmware doesn't answer\n");
		for (i = 0; i < 0x50; i += 2)
			printk(KERN_INFO "## HS %02x: %04x\n",
				i, if_cs_read16(card, i));
		goto err_release;
	}

	for (sent = 0; sent < fw->size; sent += len) {
		len = if_cs_read16(card, IF_CS_C_SQ_READ_LOW);
		/* printk(KERN_INFO "//HS %d loading %d of %d bytes\n",
			__LINE__, sent, fw->size); */
		if (len & 1) {
			retry++;
			lbs_pr_info("odd, need to retry this firmware block\n");
		} else {
			retry = 0;
		}

		if (retry > 20) {
			lbs_pr_err("could not download firmware\n");
			ret = -ENODEV;
			goto err_release;
		}
		if (retry) {
			sent -= len;
		}


		if_cs_write16(card, IF_CS_H_CMD_LEN, len);

		if_cs_write16_rep(card, IF_CS_H_CMD,
			&fw->data[sent],
			(len+1) >> 1);
		if_cs_write8(card, IF_CS_H_STATUS, IF_CS_H_STATUS_DNLD_OVER);
		if_cs_write16(card, IF_CS_H_INT_CAUSE, IF_CS_H_IC_DNLD_OVER);

		ret = if_cs_poll_while_fw_download(card, IF_CS_C_STATUS,
			IF_CS_C_S_CMD_DNLD_RDY);
		if (ret < 0) {
			lbs_pr_err("can't download firmware at 0x%x\n", sent);
			goto err_release;
		}
	}

	ret = if_cs_poll_while_fw_download(card, IF_CS_SCRATCH, 0x5a);
	if (ret < 0) {
		lbs_pr_err("firmware download failed\n");
		goto err_release;
	}

	ret = 0;
	goto done;


err_release:
	release_firmware(fw);

done:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %d", ret);
	return ret;
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

	lbs_deb_enter_args(LBS_DEB_CS, "type %d, bytes %d", type, nb);

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
		lbs_pr_err("%s: unsupported type %d\n", __FUNCTION__, type);
	}

	lbs_deb_leave_args(LBS_DEB_CS, "ret %d", ret);
	return ret;
}


static int if_cs_get_int_status(struct lbs_private *priv, u8 *ireg)
{
	struct if_cs_card *card = (struct if_cs_card *)priv->card;
	int ret = 0;
	u16 int_cause;
	*ireg = 0;

	lbs_deb_enter(LBS_DEB_CS);

	if (priv->surpriseremoved)
		goto out;

	int_cause = if_cs_read16(card, IF_CS_C_INT_CAUSE) & IF_CS_C_IC_MASK;
	if_cs_write16(card, IF_CS_C_INT_CAUSE, int_cause);

	*ireg = if_cs_read16(card, IF_CS_C_STATUS) & IF_CS_C_S_MASK;

	if (!*ireg)
		goto sbi_get_int_status_exit;

sbi_get_int_status_exit:

	/* is there a data packet for us? */
	if (*ireg & IF_CS_C_S_RX_UPLD_RDY) {
		struct sk_buff *skb = if_cs_receive_data(priv);
		lbs_process_rxed_packet(priv, skb);
		*ireg &= ~IF_CS_C_S_RX_UPLD_RDY;
	}

	if (*ireg & IF_CS_C_S_TX_DNLD_RDY) {
		priv->dnld_sent = DNLD_RES_RECEIVED;
	}

	/* Card has a command result for us */
	if (*ireg & IF_CS_C_S_CMD_UPLD_RDY) {
		ret = if_cs_receive_cmdres(priv, priv->upld_buf, &priv->upld_len);
		if (ret < 0)
			lbs_pr_err("could not receive cmd from card\n");
	}

out:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %d, ireg 0x%x, hisregcpy 0x%x", ret, *ireg, priv->hisregcpy);
	return ret;
}


static int if_cs_read_event_cause(struct lbs_private *priv)
{
	lbs_deb_enter(LBS_DEB_CS);

	priv->eventcause = (if_cs_read16(priv->card, IF_CS_C_STATUS) & IF_CS_C_S_STATUS_MASK) >> 5;
	if_cs_write16(priv->card, IF_CS_H_INT_CAUSE, IF_CS_H_IC_HOST_EVENT);

	return 0;
}



/********************************************************************/
/* Card Services                                                    */
/********************************************************************/

/*
 * After a card is removed, if_cs_release() will unregister the
 * device, and release the PCMCIA configuration.  If the device is
 * still open, this will be postponed until it is closed.
 */
static void if_cs_release(struct pcmcia_device *p_dev)
{
	struct if_cs_card *card = p_dev->priv;

	lbs_deb_enter(LBS_DEB_CS);

	free_irq(p_dev->irq.AssignedIRQ, card);
	pcmcia_disable_device(p_dev);
	if (card->iobase)
		ioport_unmap(card->iobase);

	lbs_deb_leave(LBS_DEB_CS);
}


/*
 * This creates an "instance" of the driver, allocating local data
 * structures for one device.  The device is registered with Card
 * Services.
 *
 * The dev_link structure is initialized, but we don't actually
 * configure the card at this point -- we wait until we receive a card
 * insertion event.
 */
static int if_cs_probe(struct pcmcia_device *p_dev)
{
	int ret = -ENOMEM;
	struct lbs_private *priv;
	struct if_cs_card *card;
	/* CIS parsing */
	tuple_t tuple;
	cisparse_t parse;
	cistpl_cftable_entry_t *cfg = &parse.cftable_entry;
	cistpl_io_t *io = &cfg->io;
	u_char buf[64];

	lbs_deb_enter(LBS_DEB_CS);

	card = kzalloc(sizeof(struct if_cs_card), GFP_KERNEL);
	if (!card) {
		lbs_pr_err("error in kzalloc\n");
		goto out;
	}
	card->p_dev = p_dev;
	p_dev->priv = card;

	p_dev->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;
	p_dev->irq.Handler = NULL;
	p_dev->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;

	p_dev->conf.Attributes = 0;
	p_dev->conf.IntType = INT_MEMORY_AND_IO;

	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	if ((ret = pcmcia_get_first_tuple(p_dev, &tuple)) != 0 ||
	    (ret = pcmcia_get_tuple_data(p_dev, &tuple)) != 0 ||
	    (ret = pcmcia_parse_tuple(p_dev, &tuple, &parse)) != 0)
	{
		lbs_pr_err("error in pcmcia_get_first_tuple etc\n");
		goto out1;
	}

	p_dev->conf.ConfigIndex = cfg->index;

	/* Do we need to allocate an interrupt? */
	if (cfg->irq.IRQInfo1) {
		p_dev->conf.Attributes |= CONF_ENABLE_IRQ;
	}

	/* IO window settings */
	if (cfg->io.nwin != 1) {
		lbs_pr_err("wrong CIS (check number of IO windows)\n");
		ret = -ENODEV;
		goto out1;
	}
	p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	p_dev->io.BasePort1 = io->win[0].base;
	p_dev->io.NumPorts1 = io->win[0].len;

	/* This reserves IO space but doesn't actually enable it */
	ret = pcmcia_request_io(p_dev, &p_dev->io);
	if (ret) {
		lbs_pr_err("error in pcmcia_request_io\n");
		goto out1;
	}

	/*
	 * Allocate an interrupt line.  Note that this does not assign
	 * a handler to the interrupt, unless the 'Handler' member of
	 * the irq structure is initialized.
	 */
	if (p_dev->conf.Attributes & CONF_ENABLE_IRQ) {
		ret = pcmcia_request_irq(p_dev, &p_dev->irq);
		if (ret) {
			lbs_pr_err("error in pcmcia_request_irq\n");
			goto out1;
		}
	}

	/* Initialize io access */
	card->iobase = ioport_map(p_dev->io.BasePort1, p_dev->io.NumPorts1);
	if (!card->iobase) {
		lbs_pr_err("error in ioport_map\n");
		ret = -EIO;
		goto out1;
	}

	/*
	 * This actually configures the PCMCIA socket -- setting up
	 * the I/O windows and the interrupt mapping, and putting the
	 * card and host interface into "Memory and IO" mode.
	 */
	ret = pcmcia_request_configuration(p_dev, &p_dev->conf);
	if (ret) {
		lbs_pr_err("error in pcmcia_request_configuration\n");
		goto out2;
	}

	/* Finally, report what we've done */
	lbs_deb_cs("irq %d, io 0x%04x-0x%04x\n",
	       p_dev->irq.AssignedIRQ, p_dev->io.BasePort1,
	       p_dev->io.BasePort1 + p_dev->io.NumPorts1 - 1);


	/* Load the firmware early, before calling into libertas.ko */
	ret = if_cs_prog_helper(card);
	if (ret == 0)
		ret = if_cs_prog_real(card);
	if (ret)
		goto out2;

	/* Make this card known to the libertas driver */
	priv = lbs_add_card(card, &p_dev->dev);
	if (!priv) {
		ret = -ENOMEM;
		goto out2;
	}

	/* Store pointers to our call-back functions */
	card->priv = priv;
	priv->card = card;
	priv->hw_host_to_card     = if_cs_host_to_card;
	priv->hw_get_int_status   = if_cs_get_int_status;
	priv->hw_read_event_cause = if_cs_read_event_cause;

	priv->fw_ready = 1;

	/* Now actually get the IRQ */
	ret = request_irq(p_dev->irq.AssignedIRQ, if_cs_interrupt,
		IRQF_SHARED, DRV_NAME, card);
	if (ret) {
		lbs_pr_err("error in request_irq\n");
		goto out3;
	}

	/* Clear any interrupt cause that happend while sending
	 * firmware/initializing card */
	if_cs_write16(card, IF_CS_C_INT_CAUSE, IF_CS_C_IC_MASK);
	if_cs_enable_ints(card);

	/* And finally bring the card up */
	if (lbs_start_card(priv) != 0) {
		lbs_pr_err("could not activate card\n");
		goto out3;
	}

	ret = 0;
	goto out;

out3:
	lbs_remove_card(priv);
out2:
	ioport_unmap(card->iobase);
out1:
	pcmcia_disable_device(p_dev);
out:
	lbs_deb_leave_args(LBS_DEB_CS, "ret %d", ret);
	return ret;
}


/*
 * This deletes a driver "instance".  The device is de-registered with
 * Card Services.  If it has been released, all local data structures
 * are freed.  Otherwise, the structures will be freed when the device
 * is released.
 */
static void if_cs_detach(struct pcmcia_device *p_dev)
{
	struct if_cs_card *card = p_dev->priv;

	lbs_deb_enter(LBS_DEB_CS);

	lbs_stop_card(card->priv);
	lbs_remove_card(card->priv);
	if_cs_disable_ints(card);
	if_cs_release(p_dev);
	kfree(card);

	lbs_deb_leave(LBS_DEB_CS);
}



/********************************************************************/
/* Module initialization                                            */
/********************************************************************/

static struct pcmcia_device_id if_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x02df, 0x8103),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, if_cs_ids);


static struct pcmcia_driver lbs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= DRV_NAME,
	},
	.probe		= if_cs_probe,
	.remove		= if_cs_detach,
	.id_table       = if_cs_ids,
};


static int __init if_cs_init(void)
{
	int ret;

	lbs_deb_enter(LBS_DEB_CS);
	ret = pcmcia_register_driver(&lbs_driver);
	lbs_deb_leave(LBS_DEB_CS);
	return ret;
}


static void __exit if_cs_exit(void)
{
	lbs_deb_enter(LBS_DEB_CS);
	pcmcia_unregister_driver(&lbs_driver);
	lbs_deb_leave(LBS_DEB_CS);
}


module_init(if_cs_init);
module_exit(if_cs_exit);
