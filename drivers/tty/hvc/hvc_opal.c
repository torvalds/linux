/*
 * opal driver interface to hvc_console.c
 *
 * Copyright 2011 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
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
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/console.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/export.h>

#include <asm/hvconsole.h>
#include <asm/prom.h>
#include <asm/firmware.h>
#include <asm/hvsi.h>
#include <asm/udbg.h>
#include <asm/opal.h>

#include "hvc_console.h"

static const char hvc_opal_name[] = "hvc_opal";

static struct of_device_id hvc_opal_match[] __devinitdata = {
	{ .name = "serial", .compatible = "ibm,opal-console-raw" },
	{ .name = "serial", .compatible = "ibm,opal-console-hvsi" },
	{ },
};

typedef enum hv_protocol {
	HV_PROTOCOL_RAW,
	HV_PROTOCOL_HVSI
} hv_protocol_t;

struct hvc_opal_priv {
	hv_protocol_t		proto;	/* Raw data or HVSI packets */
	struct hvsi_priv	hvsi;	/* HVSI specific data */
};
static struct hvc_opal_priv *hvc_opal_privs[MAX_NR_HVC_CONSOLES];

/* For early boot console */
static struct hvc_opal_priv hvc_opal_boot_priv;
static u32 hvc_opal_boot_termno;

static const struct hv_ops hvc_opal_raw_ops = {
	.get_chars = opal_get_chars,
	.put_chars = opal_put_chars,
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
	.notifier_hangup = notifier_hangup_irq,
};

static int hvc_opal_hvsi_get_chars(uint32_t vtermno, char *buf, int count)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[vtermno];

	if (WARN_ON(!pv))
		return -ENODEV;

	return hvsilib_get_chars(&pv->hvsi, buf, count);
}

static int hvc_opal_hvsi_put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[vtermno];

	if (WARN_ON(!pv))
		return -ENODEV;

	return hvsilib_put_chars(&pv->hvsi, buf, count);
}

static int hvc_opal_hvsi_open(struct hvc_struct *hp, int data)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermno];
	int rc;

	pr_devel("HVSI@%x: do open !\n", hp->vtermno);

	rc = notifier_add_irq(hp, data);
	if (rc)
		return rc;

	return hvsilib_open(&pv->hvsi, hp);
}

static void hvc_opal_hvsi_close(struct hvc_struct *hp, int data)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermno];

	pr_devel("HVSI@%x: do close !\n", hp->vtermno);

	hvsilib_close(&pv->hvsi, hp);

	notifier_del_irq(hp, data);
}

void hvc_opal_hvsi_hangup(struct hvc_struct *hp, int data)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermno];

	pr_devel("HVSI@%x: do hangup !\n", hp->vtermno);

	hvsilib_close(&pv->hvsi, hp);

	notifier_hangup_irq(hp, data);
}

static int hvc_opal_hvsi_tiocmget(struct hvc_struct *hp)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermno];

	if (!pv)
		return -EINVAL;
	return pv->hvsi.mctrl;
}

static int hvc_opal_hvsi_tiocmset(struct hvc_struct *hp, unsigned int set,
				unsigned int clear)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermno];

	pr_devel("HVSI@%x: Set modem control, set=%x,clr=%x\n",
		 hp->vtermno, set, clear);

	if (set & TIOCM_DTR)
		hvsilib_write_mctrl(&pv->hvsi, 1);
	else if (clear & TIOCM_DTR)
		hvsilib_write_mctrl(&pv->hvsi, 0);

	return 0;
}

static const struct hv_ops hvc_opal_hvsi_ops = {
	.get_chars = hvc_opal_hvsi_get_chars,
	.put_chars = hvc_opal_hvsi_put_chars,
	.notifier_add = hvc_opal_hvsi_open,
	.notifier_del = hvc_opal_hvsi_close,
	.notifier_hangup = hvc_opal_hvsi_hangup,
	.tiocmget = hvc_opal_hvsi_tiocmget,
	.tiocmset = hvc_opal_hvsi_tiocmset,
};

static int __devinit hvc_opal_probe(struct platform_device *dev)
{
	const struct hv_ops *ops;
	struct hvc_struct *hp;
	struct hvc_opal_priv *pv;
	hv_protocol_t proto;
	unsigned int termno, boot = 0;
	const __be32 *reg;

	if (of_device_is_compatible(dev->dev.of_node, "ibm,opal-console-raw")) {
		proto = HV_PROTOCOL_RAW;
		ops = &hvc_opal_raw_ops;
	} else if (of_device_is_compatible(dev->dev.of_node,
					   "ibm,opal-console-hvsi")) {
		proto = HV_PROTOCOL_HVSI;
		ops = &hvc_opal_hvsi_ops;
	} else {
		pr_err("hvc_opal: Unknown protocol for %s\n",
		       dev->dev.of_node->full_name);
		return -ENXIO;
	}

	reg = of_get_property(dev->dev.of_node, "reg", NULL);
	termno = reg ? be32_to_cpup(reg) : 0;

	/* Is it our boot one ? */
	if (hvc_opal_privs[termno] == &hvc_opal_boot_priv) {
		pv = hvc_opal_privs[termno];
		boot = 1;
	} else if (hvc_opal_privs[termno] == NULL) {
		pv = kzalloc(sizeof(struct hvc_opal_priv), GFP_KERNEL);
		if (!pv)
			return -ENOMEM;
		pv->proto = proto;
		hvc_opal_privs[termno] = pv;
		if (proto == HV_PROTOCOL_HVSI)
			hvsilib_init(&pv->hvsi, opal_get_chars, opal_put_chars,
				     termno, 0);

		/* Instanciate now to establish a mapping index==vtermno */
		hvc_instantiate(termno, termno, ops);
	} else {
		pr_err("hvc_opal: Device %s has duplicate terminal number #%d\n",
		       dev->dev.of_node->full_name, termno);
		return -ENXIO;
	}

	pr_info("hvc%d: %s protocol on %s%s\n", termno,
		proto == HV_PROTOCOL_RAW ? "raw" : "hvsi",
		dev->dev.of_node->full_name,
		boot ? " (boot console)" : "");

	/* We don't do IRQ yet */
	hp = hvc_alloc(termno, 0, ops, MAX_VIO_PUT_CHARS);
	if (IS_ERR(hp))
		return PTR_ERR(hp);
	dev_set_drvdata(&dev->dev, hp);

	return 0;
}

static int __devexit hvc_opal_remove(struct platform_device *dev)
{
	struct hvc_struct *hp = dev_get_drvdata(&dev->dev);
	int rc, termno;

	termno = hp->vtermno;
	rc = hvc_remove(hp);
	if (rc == 0) {
		if (hvc_opal_privs[termno] != &hvc_opal_boot_priv)
			kfree(hvc_opal_privs[termno]);
		hvc_opal_privs[termno] = NULL;
	}
	return rc;
}

static struct platform_driver hvc_opal_driver = {
	.probe		= hvc_opal_probe,
	.remove		= __devexit_p(hvc_opal_remove),
	.driver		= {
		.name	= hvc_opal_name,
		.owner	= THIS_MODULE,
		.of_match_table	= hvc_opal_match,
	}
};

static int __init hvc_opal_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return -ENODEV;

	/* Register as a vio device to receive callbacks */
	return platform_driver_register(&hvc_opal_driver);
}
module_init(hvc_opal_init);

static void __exit hvc_opal_exit(void)
{
	platform_driver_unregister(&hvc_opal_driver);
}
module_exit(hvc_opal_exit);

static void udbg_opal_putc(char c)
{
	unsigned int termno = hvc_opal_boot_termno;
	int count = -1;

	if (c == '\n')
		udbg_opal_putc('\r');

	do {
		switch(hvc_opal_boot_priv.proto) {
		case HV_PROTOCOL_RAW:
			count = opal_put_chars(termno, &c, 1);
			break;
		case HV_PROTOCOL_HVSI:
			count = hvc_opal_hvsi_put_chars(termno, &c, 1);
			break;
		}
	} while(count == 0 || count == -EAGAIN);
}

static int udbg_opal_getc_poll(void)
{
	unsigned int termno = hvc_opal_boot_termno;
	int rc = 0;
	char c;

	switch(hvc_opal_boot_priv.proto) {
	case HV_PROTOCOL_RAW:
		rc = opal_get_chars(termno, &c, 1);
		break;
	case HV_PROTOCOL_HVSI:
		rc = hvc_opal_hvsi_get_chars(termno, &c, 1);
		break;
	}
	if (!rc)
		return -1;
	return c;
}

static int udbg_opal_getc(void)
{
	int ch;
	for (;;) {
		ch = udbg_opal_getc_poll();
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

static void udbg_init_opal_common(void)
{
	udbg_putc = udbg_opal_putc;
	udbg_getc = udbg_opal_getc;
	udbg_getc_poll = udbg_opal_getc_poll;
	tb_ticks_per_usec = 0x200; /* Make udelay not suck */
}

void __init hvc_opal_init_early(void)
{
	struct device_node *stdout_node = NULL;
	const u32 *termno;
	const char *name = NULL;
	const struct hv_ops *ops;
	u32 index;

	/* find the boot console from /chosen/stdout */
	if (of_chosen)
		name = of_get_property(of_chosen, "linux,stdout-path", NULL);
	if (name) {
		stdout_node = of_find_node_by_path(name);
		if (!stdout_node) {
			pr_err("hvc_opal: Failed to locate default console!\n");
			return;
		}
	} else {
		struct device_node *opal, *np;

		/* Current OPAL takeover doesn't provide the stdout
		 * path, so we hard wire it
		 */
		opal = of_find_node_by_path("/ibm,opal/consoles");
		if (opal)
			pr_devel("hvc_opal: Found consoles in new location\n");
		if (!opal) {
			opal = of_find_node_by_path("/ibm,opal");
			if (opal)
				pr_devel("hvc_opal: "
					 "Found consoles in old location\n");
		}
		if (!opal)
			return;
		for_each_child_of_node(opal, np) {
			if (!strcmp(np->name, "serial")) {
				stdout_node = np;
				break;
			}
		}
		of_node_put(opal);
	}
	if (!stdout_node)
		return;
	termno = of_get_property(stdout_node, "reg", NULL);
	index = termno ? *termno : 0;
	if (index >= MAX_NR_HVC_CONSOLES)
		return;
	hvc_opal_privs[index] = &hvc_opal_boot_priv;

	/* Check the protocol */
	if (of_device_is_compatible(stdout_node, "ibm,opal-console-raw")) {
		hvc_opal_boot_priv.proto = HV_PROTOCOL_RAW;
		ops = &hvc_opal_raw_ops;
		pr_devel("hvc_opal: Found RAW console\n");
	}
	else if (of_device_is_compatible(stdout_node,"ibm,opal-console-hvsi")) {
		hvc_opal_boot_priv.proto = HV_PROTOCOL_HVSI;
		ops = &hvc_opal_hvsi_ops;
		hvsilib_init(&hvc_opal_boot_priv.hvsi, opal_get_chars,
			     opal_put_chars, index, 1);
		/* HVSI, perform the handshake now */
		hvsilib_establish(&hvc_opal_boot_priv.hvsi);
		pr_devel("hvc_opal: Found HVSI console\n");
	} else
		goto out;
	hvc_opal_boot_termno = index;
	udbg_init_opal_common();
	add_preferred_console("hvc", index, NULL);
	hvc_instantiate(index, index, ops);
out:
	of_node_put(stdout_node);
}

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL_RAW
void __init udbg_init_debug_opal_raw(void)
{
	u32 index = CONFIG_PPC_EARLY_DEBUG_OPAL_VTERMNO;
	hvc_opal_privs[index] = &hvc_opal_boot_priv;
	hvc_opal_boot_priv.proto = HV_PROTOCOL_RAW;
	hvc_opal_boot_termno = index;
	udbg_init_opal_common();
}
#endif /* CONFIG_PPC_EARLY_DEBUG_OPAL_RAW */

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL_HVSI
void __init udbg_init_debug_opal_hvsi(void)
{
	u32 index = CONFIG_PPC_EARLY_DEBUG_OPAL_VTERMNO;
	hvc_opal_privs[index] = &hvc_opal_boot_priv;
	hvc_opal_boot_termno = index;
	udbg_init_opal_common();
	hvsilib_init(&hvc_opal_boot_priv.hvsi, opal_get_chars, opal_put_chars,
		     index, 1);
	hvsilib_establish(&hvc_opal_boot_priv.hvsi);
}
#endif /* CONFIG_PPC_EARLY_DEBUG_OPAL_HVSI */
