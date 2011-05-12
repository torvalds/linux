/*
 * vio driver interface to hvc_console.c
 *
 * This code was moved here to allow the remaining code to be reused as a
 * generic polling mode with semi-reliable transport driver core to the
 * console and tty subsystems.
 *
 *
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2004 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 * Copyright (C) 2004 IBM Corporation
 *
 * Additional Author(s):
 *  Ryan S. Arnold <rsa@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * TODO:
 *
 *   - handle error in sending hvsi protocol packets
 *   - retry nego on subsequent sends ?
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/console.h>

#include <asm/hvconsole.h>
#include <asm/vio.h>
#include <asm/prom.h>
#include <asm/firmware.h>
#include <asm/hvsi.h>
#include <asm/udbg.h>

#include "hvc_console.h"

static const char hvc_driver_name[] = "hvc_console";

static struct vio_device_id hvc_driver_table[] __devinitdata = {
	{"serial", "hvterm1"},
#ifndef HVC_OLD_HVSI
	{"serial", "hvterm-protocol"},
#endif
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, hvc_driver_table);

typedef enum hv_protocol {
	HV_PROTOCOL_RAW,
	HV_PROTOCOL_HVSI
} hv_protocol_t;

#define HV_INBUF_SIZE		255

struct hvterm_priv {
	u32		termno;		/* HV term number */
	hv_protocol_t	proto;		/* Raw data or HVSI packets */
	unsigned int	inbuf_len;	/* Data in input buffer */
	unsigned char	inbuf[HV_INBUF_SIZE];
	unsigned int	inbuf_cur;	/* Cursor in input buffer */
	unsigned int	inbuf_pktlen;	/* HVSI packet lenght from cursor */
	atomic_t	seqno;		/* HVSI packet sequence number */
	unsigned int	opened:1;	/* HVSI driver opened */
	unsigned int	established:1;	/* HVSI protocol established */
	unsigned int 	is_console:1;	/* Used as a kernel console device */
	unsigned int	mctrl_update:1;	/* HVSI modem control updated */
	unsigned short	mctrl;		/* HVSI modem control */
	struct tty_struct *tty;		/* TTY structure */
};
static struct hvterm_priv *hvterm_privs[MAX_NR_HVC_CONSOLES];

/* For early boot console */
static struct hvterm_priv hvterm_priv0;

static int hvterm_raw_get_chars(uint32_t vtermno, char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];
	unsigned long got, i;

	if (WARN_ON(!pv))
		return 0;

	/*
	 * Vio firmware will read up to SIZE_VIO_GET_CHARS at its own discretion
	 * so we play safe and avoid the situation where got > count which could
	 * overload the flip buffer.
	 */
	if (count < SIZE_VIO_GET_CHARS)
		return -EAGAIN;

	got = hvc_get_chars(pv->termno, buf, count);

	/*
	 * Work around a HV bug where it gives us a null
	 * after every \r.  -- paulus
	 */
	for (i = 1; i < got; ++i) {
		if (buf[i] == 0 && buf[i-1] == '\r') {
			--got;
			if (i < got)
				memmove(&buf[i], &buf[i+1], got - i);
		}
	}
	return got;
}

static int hvterm_raw_put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];

	if (WARN_ON(!pv))
		return 0;

	return hvc_put_chars(pv->termno, buf, count);
}

static const struct hv_ops hvterm_raw_ops = {
	.get_chars = hvterm_raw_get_chars,
	.put_chars = hvterm_raw_put_chars,
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
	.notifier_hangup = notifier_hangup_irq,
};

static int hvterm_hvsi_send_packet(struct hvterm_priv *pv, struct hvsi_header *packet)
{
	packet->seqno = atomic_inc_return(&pv->seqno);

	/* Assumes that always succeeds, works in practice */
	return hvc_put_chars(pv->termno, (char *)packet, packet->len);
}

static void hvterm_hvsi_start_handshake(struct hvterm_priv *pv)
{
	struct hvsi_query q;

	/* Reset state */
	pv->established = 0;
	atomic_set(&pv->seqno, 0);

	pr_devel("HVSI@%x: Handshaking started\n", pv->termno);

	/* Send version query */
	q.hdr.type = VS_QUERY_PACKET_HEADER;
	q.hdr.len = sizeof(struct hvsi_query);
	q.verb = VSV_SEND_VERSION_NUMBER;
	hvterm_hvsi_send_packet(pv, &q.hdr);
}

static int hvterm_hvsi_send_close(struct hvterm_priv *pv)
{
	struct hvsi_control ctrl;

	pv->established = 0;

	ctrl.hdr.type = VS_CONTROL_PACKET_HEADER;
	ctrl.hdr.len = sizeof(struct hvsi_control);
	ctrl.verb = VSV_CLOSE_PROTOCOL;
	return hvterm_hvsi_send_packet(pv, &ctrl.hdr);
}

static void hvterm_cd_change(struct hvterm_priv *pv, int cd)
{
	if (cd)
		pv->mctrl |= TIOCM_CD;
	else {
		pv->mctrl &= ~TIOCM_CD;

		/* We copy the existing hvsi driver semantics
		 * here which are to trigger a hangup when
		 * we get a carrier loss.
		 * Closing our connection to the server will
		 * do just that.
		 */
		if (!pv->is_console && pv->opened) {
			pr_devel("HVSI@%x Carrier lost, hanging up !\n",
				 pv->termno);
			hvterm_hvsi_send_close(pv);
		}
	}
}

static void hvterm_hvsi_got_control(struct hvterm_priv *pv)
{
	struct hvsi_control *pkt = (struct hvsi_control *)pv->inbuf;

	switch (pkt->verb) {
	case VSV_CLOSE_PROTOCOL:
		/* We restart the handshaking */
		hvterm_hvsi_start_handshake(pv);
		break;
	case VSV_MODEM_CTL_UPDATE:
		/* Transition of carrier detect */
		hvterm_cd_change(pv, pkt->word & HVSI_TSCD);
		break;
	}
}

static void hvterm_hvsi_got_query(struct hvterm_priv *pv)
{
	struct hvsi_query *pkt = (struct hvsi_query *)pv->inbuf;
	struct hvsi_query_response r;

	/* We only handle version queries */
	if (pkt->verb != VSV_SEND_VERSION_NUMBER)
		return;

	pr_devel("HVSI@%x: Got version query, sending response...\n",
		 pv->termno);

	/* Send version response */
	r.hdr.type = VS_QUERY_RESPONSE_PACKET_HEADER;
	r.hdr.len = sizeof(struct hvsi_query_response);
	r.verb = VSV_SEND_VERSION_NUMBER;
	r.u.version = HVSI_VERSION;
	r.query_seqno = pkt->hdr.seqno;
	hvterm_hvsi_send_packet(pv, &r.hdr);

	/* Assume protocol is open now */
	pv->established = 1;
}

static void hvterm_hvsi_got_response(struct hvterm_priv *pv)
{
	struct hvsi_query_response *r = (struct hvsi_query_response *)pv->inbuf;

	switch(r->verb) {
	case VSV_SEND_MODEM_CTL_STATUS:
		hvterm_cd_change(pv, r->u.mctrl_word & HVSI_TSCD);
		pv->mctrl_update = 1;
		break;
	}
}

static int hvterm_hvsi_check_packet(struct hvterm_priv *pv)
{
	u8 len, type;

	/* Check header validity. If it's invalid, we ditch
	 * the whole buffer and hope we eventually resync
	 */
	if (pv->inbuf[0] < 0xfc) {
		pv->inbuf_len = pv->inbuf_pktlen = 0;
		return 0;
	}
	type = pv->inbuf[0];
	len = pv->inbuf[1];

	/* Packet incomplete ? */
	if (pv->inbuf_len < len)
		return 0;

	pr_devel("HVSI@%x: Got packet type %x len %d bytes:\n",
		 pv->termno, type, len);

	/* We have a packet, yay ! Handle it */
	switch(type) {
	case VS_DATA_PACKET_HEADER:
		pv->inbuf_pktlen = len - 4;
		pv->inbuf_cur = 4;
		return 1;
	case VS_CONTROL_PACKET_HEADER:
		hvterm_hvsi_got_control(pv);
		break;
	case VS_QUERY_PACKET_HEADER:
		hvterm_hvsi_got_query(pv);
		break;
	case VS_QUERY_RESPONSE_PACKET_HEADER:
		hvterm_hvsi_got_response(pv);
		break;
	}

	/* Swallow packet and retry */
	pv->inbuf_len -= len;
	memmove(pv->inbuf, &pv->inbuf[len], pv->inbuf_len);
	return 1;
}

static int hvterm_hvsi_get_packet(struct hvterm_priv *pv)
{
	/* If we have room in the buffer, ask HV for more */
	if (pv->inbuf_len < HV_INBUF_SIZE)
		pv->inbuf_len += hvc_get_chars(pv->termno,
					       &pv->inbuf[pv->inbuf_len],
					       HV_INBUF_SIZE - pv->inbuf_len);
	/*
	 * If we have at least 4 bytes in the buffer, check for
	 * a full packet and retry
	 */
	if (pv->inbuf_len >= 4)
		return hvterm_hvsi_check_packet(pv);
	return 0;
}

static int hvterm_hvsi_get_chars(uint32_t vtermno, char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];
	unsigned int tries, read = 0;

	if (WARN_ON(!pv))
		return 0;

	/* If we aren't open, dont do anything in order to avoid races
	 * with connection establishment. The hvc core will call this
	 * before we have returned from notifier_add(), and we need to
	 * avoid multiple users playing with the receive buffer
	 */
	if (!pv->opened)
		return 0;

	/* We try twice, once with what data we have and once more
	 * after we try to fetch some more from the hypervisor
	 */
	for (tries = 1; count && tries < 2; tries++) {
		/* Consume existing data packet */
		if (pv->inbuf_pktlen) {
			unsigned int l = min(count, (int)pv->inbuf_pktlen);
			memcpy(&buf[read], &pv->inbuf[pv->inbuf_cur], l);
			pv->inbuf_cur += l;
			pv->inbuf_pktlen -= l;
			count -= l;
			read += l;
		}
		if (count == 0)
			break;

		/* Data packet fully consumed, move down remaning data */
		if (pv->inbuf_cur) {
			pv->inbuf_len -= pv->inbuf_cur;
			memmove(pv->inbuf, &pv->inbuf[pv->inbuf_cur], pv->inbuf_len);
			pv->inbuf_cur = 0;
		}

		/* Try to get another packet */
		if (hvterm_hvsi_get_packet(pv))
			tries--;
	}
	if (!pv->established) {
		pr_devel("HVSI@%x: returning -EPIPE\n", pv->termno);
		return -EPIPE;
	}
	return read;
}

static int hvterm_hvsi_put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];
	struct hvsi_data dp;
	int rc, adjcount = min(count, HVSI_MAX_OUTGOING_DATA);

	if (WARN_ON(!pv))
		return 0;

	dp.hdr.type = VS_DATA_PACKET_HEADER;
	dp.hdr.len = adjcount + sizeof(struct hvsi_header);
	memcpy(dp.data, buf, adjcount);
	rc = hvterm_hvsi_send_packet(pv, &dp.hdr);
	if (rc <= 0)
		return rc;
	return adjcount;
}

static void maybe_msleep(unsigned long ms)
{
	/* During early boot, IRQs are disabled, use mdelay */
	if (irqs_disabled())
		mdelay(ms);
	else
		msleep(ms);
}

static int hvterm_hvsi_read_mctrl(struct hvterm_priv *pv)
{
	struct hvsi_query q;
	int rc, timeout;

	pr_devel("HVSI@%x: Querying modem control status...\n",
		 pv->termno);

	pv->mctrl_update = 0;
	q.hdr.type = VS_QUERY_PACKET_HEADER;
	q.hdr.len = sizeof(struct hvsi_query);
	q.hdr.seqno = atomic_inc_return(&pv->seqno);
	q.verb = VSV_SEND_MODEM_CTL_STATUS;
	rc = hvterm_hvsi_send_packet(pv, &q.hdr);
	if (rc <= 0) {
		pr_devel("HVSI@%x: Error %d...\n", pv->termno, rc);
		return rc;
	}

	/* Try for up to 1s */
	for (timeout = 0; timeout < 1000; timeout++) {
		if (!pv->established)
			return -ENXIO;
		if (pv->mctrl_update)
			return 0;
		if (!hvterm_hvsi_get_packet(pv))
			maybe_msleep(1);
	}
	return -EIO;
}

static int hvterm_hvsi_write_mctrl(struct hvterm_priv *pv, int dtr)
{
	struct hvsi_control ctrl;

	pr_devel("HVSI@%x: %s DTR...\n", pv->termno,
		 dtr ? "Setting" : "Clearing");

	ctrl.hdr.type = VS_CONTROL_PACKET_HEADER,
	ctrl.hdr.len = sizeof(struct hvsi_control);
	ctrl.verb = VSV_SET_MODEM_CTL;
	ctrl.mask = HVSI_TSDTR;
	ctrl.word = dtr ? HVSI_TSDTR : 0;
	if (dtr)
		pv->mctrl |= TIOCM_DTR;
	else
		pv->mctrl &= ~TIOCM_DTR;
	return hvterm_hvsi_send_packet(pv, &ctrl.hdr);
}

static void hvterm_hvsi_establish(struct hvterm_priv *pv)
{
	int timeout;

	/* Try for up to 10ms, there can be a packet to
	 * start the process waiting for us...
	 */
	for (timeout = 0; timeout < 10; timeout++) {
		if (pv->established)
			goto established;
		if (!hvterm_hvsi_get_packet(pv))
			maybe_msleep(1);
	}

	/* Failed, send a close connection packet just
	 * in case
	 */
	hvterm_hvsi_send_close(pv);

	/* Then restart handshake */
	hvterm_hvsi_start_handshake(pv);

	/* Try for up to 100ms */
	for (timeout = 0; timeout < 100; timeout++) {
		if (pv->established)
			goto established;
		if (!hvterm_hvsi_get_packet(pv))
			maybe_msleep(1);
	}

	if (!pv->established) {
		pr_devel("HVSI@%x: Timeout handshaking, giving up !\n",
			 pv->termno);
		return;
	}
 established:
	/* Query modem control lines */
	hvterm_hvsi_read_mctrl(pv);

	/* Set our own DTR */
	hvterm_hvsi_write_mctrl(pv, 1);

	/* Set the opened flag so reads are allowed */
	wmb();
	pv->opened = 1;
}

static int hvterm_hvsi_open(struct hvc_struct *hp, int data)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];
	int rc;

	pr_devel("HVSI@%x: open !\n", pv->termno);

	rc = notifier_add_irq(hp, data);
	if (rc)
		return rc;

	/* Keep track of the tty data structure */
	pv->tty = tty_kref_get(hp->tty);

	hvterm_hvsi_establish(pv);
	return 0;
}

static void hvterm_hvsi_shutdown(struct hvc_struct *hp, struct hvterm_priv *pv)
{
	unsigned long flags;

	if (!pv->is_console) {
		pr_devel("HVSI@%x: Not a console, tearing down\n",
			 pv->termno);

		/* Clear opened, synchronize with khvcd */
		spin_lock_irqsave(&hp->lock, flags);
		pv->opened = 0;
		spin_unlock_irqrestore(&hp->lock, flags);

		/* Clear our own DTR */
		if (!pv->tty || (pv->tty->termios->c_cflag & HUPCL))
			hvterm_hvsi_write_mctrl(pv, 0);

		/* Tear down the connection */
		hvterm_hvsi_send_close(pv);
	}

	if (pv->tty)
		tty_kref_put(pv->tty);
	pv->tty = NULL;
}

static void hvterm_hvsi_close(struct hvc_struct *hp, int data)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	pr_devel("HVSI@%x: close !\n", pv->termno);

	hvterm_hvsi_shutdown(hp, pv);

	notifier_del_irq(hp, data);
}

void hvterm_hvsi_hangup(struct hvc_struct *hp, int data)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	pr_devel("HVSI@%x: hangup !\n", pv->termno);

	hvterm_hvsi_shutdown(hp, pv);

	notifier_hangup_irq(hp, data);
}

static int hvterm_hvsi_tiocmget(struct hvc_struct *hp)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	if (!pv)
		return -EINVAL;
	return pv->mctrl;
}

static int hvterm_hvsi_tiocmset(struct hvc_struct *hp, unsigned int set,
				unsigned int clear)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	pr_devel("HVSI@%x: Set modem control, set=%x,clr=%x\n",
		 pv->termno, set, clear);

	if (set & TIOCM_DTR)
		hvterm_hvsi_write_mctrl(pv, 1);
	else if (clear & TIOCM_DTR)
		hvterm_hvsi_write_mctrl(pv, 0);

	return 0;
}

static const struct hv_ops hvterm_hvsi_ops = {
	.get_chars = hvterm_hvsi_get_chars,
	.put_chars = hvterm_hvsi_put_chars,
	.notifier_add = hvterm_hvsi_open,
	.notifier_del = hvterm_hvsi_close,
	.notifier_hangup = hvterm_hvsi_hangup,
	.tiocmget = hvterm_hvsi_tiocmget,
	.tiocmset = hvterm_hvsi_tiocmset,
};

static int __devinit hvc_vio_probe(struct vio_dev *vdev,
				   const struct vio_device_id *id)
{
	const struct hv_ops *ops;
	struct hvc_struct *hp;
	struct hvterm_priv *pv;
	hv_protocol_t proto;
	int i, termno = -1;

	/* probed with invalid parameters. */
	if (!vdev || !id)
		return -EPERM;

	if (of_device_is_compatible(vdev->dev.of_node, "hvterm1")) {
		proto = HV_PROTOCOL_RAW;
		ops = &hvterm_raw_ops;
	} else if (of_device_is_compatible(vdev->dev.of_node, "hvterm-protocol")) {
		proto = HV_PROTOCOL_HVSI;
		ops = &hvterm_hvsi_ops;
	} else {
		pr_err("hvc_vio: Unkown protocol for %s\n", vdev->dev.of_node->full_name);
		return -ENXIO;
	}

	pr_devel("hvc_vio_probe() device %s, using %s protocol\n",
		 vdev->dev.of_node->full_name,
		 proto == HV_PROTOCOL_RAW ? "raw" : "hvsi");

	/* Is it our boot one ? */
	if (hvterm_privs[0] == &hvterm_priv0 &&
	    vdev->unit_address == hvterm_priv0.termno) {
		pv = hvterm_privs[0];
		termno = 0;
		pr_devel("->boot console, using termno 0\n");
	}
	/* nope, allocate a new one */
	else {
		for (i = 0; i < MAX_NR_HVC_CONSOLES && termno < 0; i++)
			if (!hvterm_privs[i])
				termno = i;
		pr_devel("->non-boot console, using termno %d\n", termno);
		if (termno < 0)
			return -ENODEV;
		pv = kzalloc(sizeof(struct hvterm_priv), GFP_KERNEL);
		if (!pv)
			return -ENOMEM;
		pv->termno = vdev->unit_address;
		pv->proto = proto;
		hvterm_privs[termno] = pv;
	}

	hp = hvc_alloc(termno, vdev->irq, ops, MAX_VIO_PUT_CHARS);
	if (IS_ERR(hp))
		return PTR_ERR(hp);
	dev_set_drvdata(&vdev->dev, hp);

	return 0;
}

static int __devexit hvc_vio_remove(struct vio_dev *vdev)
{
	struct hvc_struct *hp = dev_get_drvdata(&vdev->dev);
	int rc, termno;

	termno = hp->vtermno;
	rc = hvc_remove(hp);
	if (rc == 0) {
		if (hvterm_privs[termno] != &hvterm_priv0)
			kfree(hvterm_privs[termno]);
		hvterm_privs[termno] = NULL;
	}
	return rc;
}

static struct vio_driver hvc_vio_driver = {
	.id_table	= hvc_driver_table,
	.probe		= hvc_vio_probe,
	.remove		= __devexit_p(hvc_vio_remove),
	.driver		= {
		.name	= hvc_driver_name,
		.owner	= THIS_MODULE,
	}
};

static int __init hvc_vio_init(void)
{
	int rc;

	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return -EIO;

	/* Register as a vio device to receive callbacks */
	rc = vio_register_driver(&hvc_vio_driver);

	return rc;
}
module_init(hvc_vio_init); /* after drivers/char/hvc_console.c */

static void __exit hvc_vio_exit(void)
{
	vio_unregister_driver(&hvc_vio_driver);
}
module_exit(hvc_vio_exit);

static void udbg_hvc_putc(char c)
{
	int count = -1;

	if (c == '\n')
		udbg_hvc_putc('\r');

	do {
		switch(hvterm_priv0.proto) {
		case HV_PROTOCOL_RAW:
			count = hvterm_raw_put_chars(0, &c, 1);
			break;
		case HV_PROTOCOL_HVSI:
			count = hvterm_hvsi_put_chars(0, &c, 1);
			break;
		}
	} while(count == 0);
}

static int udbg_hvc_getc_poll(void)
{
	int rc = 0;
	char c;

	switch(hvterm_priv0.proto) {
	case HV_PROTOCOL_RAW:
		rc = hvterm_raw_get_chars(0, &c, 1);
		break;
	case HV_PROTOCOL_HVSI:
		rc = hvterm_hvsi_get_chars(0, &c, 1);
		break;
	}
	if (!rc)
		return -1;
	return c;
}

static int udbg_hvc_getc(void)
{
	int ch;
	for (;;) {
		ch = udbg_hvc_getc_poll();
		if (ch == -1) {
			/* This shouldn't be needed...but... */
			volatile unsigned long delay;
			for (delay=0; delay < 2000000; delay++)
				;
		} else {
			return ch;
		}
	}
}

void __init hvc_vio_init_early(void)
{
	struct device_node *stdout_node;
	const u32 *termno;
	const char *name;
	const struct hv_ops *ops;

	/* find the boot console from /chosen/stdout */
	if (!of_chosen)
		return;
	name = of_get_property(of_chosen, "linux,stdout-path", NULL);
	if (name == NULL)
		return;
	stdout_node = of_find_node_by_path(name);
	if (!stdout_node)
		return;
	name = of_get_property(stdout_node, "name", NULL);
	if (!name) {
		printk(KERN_WARNING "stdout node missing 'name' property!\n");
		goto out;
	}

	/* Check if it's a virtual terminal */
	if (strncmp(name, "vty", 3) != 0)
		goto out;
	termno = of_get_property(stdout_node, "reg", NULL);
	if (termno == NULL)
		goto out;
	hvterm_priv0.termno = *termno;
	hvterm_priv0.is_console = 1;
	hvterm_privs[0] = &hvterm_priv0;

	/* Check the protocol */
	if (of_device_is_compatible(stdout_node, "hvterm1")) {
		hvterm_priv0.proto = HV_PROTOCOL_RAW;
		ops = &hvterm_raw_ops;
	}
	else if (of_device_is_compatible(stdout_node, "hvterm-protocol")) {
		hvterm_priv0.proto = HV_PROTOCOL_HVSI;
		ops = &hvterm_hvsi_ops;
		/* HVSI, perform the handshake now */
		hvterm_hvsi_establish(&hvterm_priv0);
	} else
		goto out;
	udbg_putc = udbg_hvc_putc;
	udbg_getc = udbg_hvc_getc;
	udbg_getc_poll = udbg_hvc_getc_poll;
#ifdef HVC_OLD_HVSI
	/* When using the old HVSI driver don't register the HVC
	 * backend for HVSI, only do udbg
	 */
	if (hvterm_priv0.proto == HV_PROTOCOL_HVSI)
		goto out;
#endif
	add_preferred_console("hvc", 0, NULL);
	hvc_instantiate(0, 0, ops);
out:
	of_node_put(stdout_node);
}

/* call this from early_init() for a working debug console on
 * vterm capable LPAR machines
 */
#ifdef CONFIG_PPC_EARLY_DEBUG_LPAR
void __init udbg_init_debug_lpar(void)
{
	hvterm_privs[0] = &hvterm_priv0;
	hvterm_priv0.termno = 0;
	hvterm_priv0.proto = HV_PROTOCOL_RAW;
	hvterm_priv0.is_console = 1;
	udbg_putc = udbg_hvc_putc;
	udbg_getc = udbg_hvc_getc;
	udbg_getc_poll = udbg_hvc_getc_poll;
}
#endif /* CONFIG_PPC_EARLY_DEBUG_LPAR */

#ifdef CONFIG_PPC_EARLY_DEBUG_LPAR_HVSI
void __init udbg_init_debug_lpar_hvsi(void)
{
	hvterm_privs[0] = &hvterm_priv0;
	hvterm_priv0.termno = CONFIG_PPC_EARLY_DEBUG_HVSI_VTERMNO;
	hvterm_priv0.proto = HV_PROTOCOL_HVSI;
	hvterm_priv0.is_console = 1;
	udbg_putc = udbg_hvc_putc;
	udbg_getc = udbg_hvc_getc;
	udbg_getc_poll = udbg_hvc_getc_poll;
	hvterm_hvsi_establish(&hvterm_priv0);
}
#endif /* CONFIG_PPC_EARLY_DEBUG_LPAR_HVSI */
