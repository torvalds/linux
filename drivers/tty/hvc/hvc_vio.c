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
#include <linux/module.h>

#include <asm/hvconsole.h>
#include <asm/vio.h>
#include <asm/prom.h>
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

struct hvterm_priv {
	u32			termno;	/* HV term number */
	hv_protocol_t		proto;	/* Raw data or HVSI packets */
	struct hvsi_priv	hvsi;	/* HVSI specific data */
	spinlock_t		buf_lock;
	char			buf[SIZE_VIO_GET_CHARS];
	int			left;
	int			offset;
};
static struct hvterm_priv *hvterm_privs[MAX_NR_HVC_CONSOLES];
/* For early boot console */
static struct hvterm_priv hvterm_priv0;

static int hvterm_raw_get_chars(uint32_t vtermno, char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];
	unsigned long i;
	unsigned long flags;
	int got;

	if (WARN_ON(!pv))
		return 0;

	spin_lock_irqsave(&pv->buf_lock, flags);

	if (pv->left == 0) {
		pv->offset = 0;
		pv->left = hvc_get_chars(pv->termno, pv->buf, count);

		/*
		 * Work around a HV bug where it gives us a null
		 * after every \r.  -- paulus
		 */
		for (i = 1; i < pv->left; ++i) {
			if (pv->buf[i] == 0 && pv->buf[i-1] == '\r') {
				--pv->left;
				if (i < pv->left) {
					memmove(&pv->buf[i], &pv->buf[i+1],
						pv->left - i);
				}
			}
		}
	}

	got = min(count, pv->left);
	memcpy(buf, &pv->buf[pv->offset], got);
	pv->offset += got;
	pv->left -= got;

	spin_unlock_irqrestore(&pv->buf_lock, flags);

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

static int hvterm_hvsi_get_chars(uint32_t vtermno, char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];

	if (WARN_ON(!pv))
		return 0;

	return hvsilib_get_chars(&pv->hvsi, buf, count);
}

static int hvterm_hvsi_put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct hvterm_priv *pv = hvterm_privs[vtermno];

	if (WARN_ON(!pv))
		return 0;

	return hvsilib_put_chars(&pv->hvsi, buf, count);
}

static int hvterm_hvsi_open(struct hvc_struct *hp, int data)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];
	int rc;

	pr_devel("HVSI@%x: open !\n", pv->termno);

	rc = notifier_add_irq(hp, data);
	if (rc)
		return rc;

	return hvsilib_open(&pv->hvsi, hp);
}

static void hvterm_hvsi_close(struct hvc_struct *hp, int data)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	pr_devel("HVSI@%x: do close !\n", pv->termno);

	hvsilib_close(&pv->hvsi, hp);

	notifier_del_irq(hp, data);
}

void hvterm_hvsi_hangup(struct hvc_struct *hp, int data)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	pr_devel("HVSI@%x: do hangup !\n", pv->termno);

	hvsilib_close(&pv->hvsi, hp);

	notifier_hangup_irq(hp, data);
}

static int hvterm_hvsi_tiocmget(struct hvc_struct *hp)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	if (!pv)
		return -EINVAL;
	return pv->hvsi.mctrl;
}

static int hvterm_hvsi_tiocmset(struct hvc_struct *hp, unsigned int set,
				unsigned int clear)
{
	struct hvterm_priv *pv = hvterm_privs[hp->vtermno];

	pr_devel("HVSI@%x: Set modem control, set=%x,clr=%x\n",
		 pv->termno, set, clear);

	if (set & TIOCM_DTR)
		hvsilib_write_mctrl(&pv->hvsi, 1);
	else if (clear & TIOCM_DTR)
		hvsilib_write_mctrl(&pv->hvsi, 0);

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

static void udbg_hvc_putc(char c)
{
	int count = -1;

	if (!hvterm_privs[0])
		return;

	if (c == '\n')
		udbg_hvc_putc('\r');

	do {
		switch(hvterm_privs[0]->proto) {
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

	if (!hvterm_privs[0])
		return -1;

	switch(hvterm_privs[0]->proto) {
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

	if (!hvterm_privs[0])
		return -1;

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

static int hvc_vio_probe(struct vio_dev *vdev,
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
		spin_lock_init(&pv->buf_lock);
		hvterm_privs[termno] = pv;
		hvsilib_init(&pv->hvsi, hvc_get_chars, hvc_put_chars,
			     pv->termno, 0);
	}

	hp = hvc_alloc(termno, vdev->irq, ops, MAX_VIO_PUT_CHARS);
	if (IS_ERR(hp))
		return PTR_ERR(hp);
	dev_set_drvdata(&vdev->dev, hp);

	/* register udbg if it's not there already for console 0 */
	if (hp->index == 0 && !udbg_putc) {
		udbg_putc = udbg_hvc_putc;
		udbg_getc = udbg_hvc_getc;
		udbg_getc_poll = udbg_hvc_getc_poll;
	}

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
	.remove		= hvc_vio_remove,
	.name		= hvc_driver_name,
};

static int __init hvc_vio_init(void)
{
	int rc;

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
	spin_lock_init(&hvterm_priv0.buf_lock);
	hvterm_privs[0] = &hvterm_priv0;

	/* Check the protocol */
	if (of_device_is_compatible(stdout_node, "hvterm1")) {
		hvterm_priv0.proto = HV_PROTOCOL_RAW;
		ops = &hvterm_raw_ops;
	}
	else if (of_device_is_compatible(stdout_node, "hvterm-protocol")) {
		hvterm_priv0.proto = HV_PROTOCOL_HVSI;
		ops = &hvterm_hvsi_ops;
		hvsilib_init(&hvterm_priv0.hvsi, hvc_get_chars, hvc_put_chars,
			     hvterm_priv0.termno, 1);
		/* HVSI, perform the handshake now */
		hvsilib_establish(&hvterm_priv0.hvsi);
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
	spin_lock_init(&hvterm_priv0.buf_lock);
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
	spin_lock_init(&hvterm_priv0.buf_lock);
	udbg_putc = udbg_hvc_putc;
	udbg_getc = udbg_hvc_getc;
	udbg_getc_poll = udbg_hvc_getc_poll;
	hvsilib_init(&hvterm_priv0.hvsi, hvc_get_chars, hvc_put_chars,
		     hvterm_priv0.termno, 1);
	hvsilib_establish(&hvterm_priv0.hvsi);
}
#endif /* CONFIG_PPC_EARLY_DEBUG_LPAR_HVSI */
