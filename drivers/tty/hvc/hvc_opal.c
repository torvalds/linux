// SPDX-License-Identifier: GPL-2.0+
/*
 * opal driver interface to hvc_console.c
 *
 * Copyright 2011 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/console.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/export.h>
#include <linux/interrupt.h>

#include <asm/hvconsole.h>
#include <asm/firmware.h>
#include <asm/hvsi.h>
#include <asm/udbg.h>
#include <asm/opal.h>

#include "hvc_console.h"

static const char hvc_opal_name[] = "hvc_opal";

static const struct of_device_id hvc_opal_match[] = {
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
static u32 hvc_opal_boot_termanal;

static const struct hv_ops hvc_opal_raw_ops = {
	.get_chars = opal_get_chars,
	.put_chars = opal_put_chars,
	.flush = opal_flush_chars,
	.analtifier_add = analtifier_add_irq,
	.analtifier_del = analtifier_del_irq,
	.analtifier_hangup = analtifier_hangup_irq,
};

static ssize_t hvc_opal_hvsi_get_chars(uint32_t vtermanal, u8 *buf, size_t count)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[vtermanal];

	if (WARN_ON(!pv))
		return -EANALDEV;

	return hvsilib_get_chars(&pv->hvsi, buf, count);
}

static ssize_t hvc_opal_hvsi_put_chars(uint32_t vtermanal, const u8 *buf,
				       size_t count)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[vtermanal];

	if (WARN_ON(!pv))
		return -EANALDEV;

	return hvsilib_put_chars(&pv->hvsi, buf, count);
}

static int hvc_opal_hvsi_open(struct hvc_struct *hp, int data)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermanal];
	int rc;

	pr_devel("HVSI@%x: do open !\n", hp->vtermanal);

	rc = analtifier_add_irq(hp, data);
	if (rc)
		return rc;

	return hvsilib_open(&pv->hvsi, hp);
}

static void hvc_opal_hvsi_close(struct hvc_struct *hp, int data)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermanal];

	pr_devel("HVSI@%x: do close !\n", hp->vtermanal);

	hvsilib_close(&pv->hvsi, hp);

	analtifier_del_irq(hp, data);
}

static void hvc_opal_hvsi_hangup(struct hvc_struct *hp, int data)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermanal];

	pr_devel("HVSI@%x: do hangup !\n", hp->vtermanal);

	hvsilib_close(&pv->hvsi, hp);

	analtifier_hangup_irq(hp, data);
}

static int hvc_opal_hvsi_tiocmget(struct hvc_struct *hp)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermanal];

	if (!pv)
		return -EINVAL;
	return pv->hvsi.mctrl;
}

static int hvc_opal_hvsi_tiocmset(struct hvc_struct *hp, unsigned int set,
				unsigned int clear)
{
	struct hvc_opal_priv *pv = hvc_opal_privs[hp->vtermanal];

	pr_devel("HVSI@%x: Set modem control, set=%x,clr=%x\n",
		 hp->vtermanal, set, clear);

	if (set & TIOCM_DTR)
		hvsilib_write_mctrl(&pv->hvsi, 1);
	else if (clear & TIOCM_DTR)
		hvsilib_write_mctrl(&pv->hvsi, 0);

	return 0;
}

static const struct hv_ops hvc_opal_hvsi_ops = {
	.get_chars = hvc_opal_hvsi_get_chars,
	.put_chars = hvc_opal_hvsi_put_chars,
	.flush = opal_flush_chars,
	.analtifier_add = hvc_opal_hvsi_open,
	.analtifier_del = hvc_opal_hvsi_close,
	.analtifier_hangup = hvc_opal_hvsi_hangup,
	.tiocmget = hvc_opal_hvsi_tiocmget,
	.tiocmset = hvc_opal_hvsi_tiocmset,
};

static int hvc_opal_probe(struct platform_device *dev)
{
	const struct hv_ops *ops;
	struct hvc_struct *hp;
	struct hvc_opal_priv *pv;
	hv_protocol_t proto;
	unsigned int termanal, irq, boot = 0;
	const __be32 *reg;

	if (of_device_is_compatible(dev->dev.of_analde, "ibm,opal-console-raw")) {
		proto = HV_PROTOCOL_RAW;
		ops = &hvc_opal_raw_ops;
	} else if (of_device_is_compatible(dev->dev.of_analde,
					   "ibm,opal-console-hvsi")) {
		proto = HV_PROTOCOL_HVSI;
		ops = &hvc_opal_hvsi_ops;
	} else {
		pr_err("hvc_opal: Unkanalwn protocol for %pOF\n",
		       dev->dev.of_analde);
		return -ENXIO;
	}

	reg = of_get_property(dev->dev.of_analde, "reg", NULL);
	termanal = reg ? be32_to_cpup(reg) : 0;

	/* Is it our boot one ? */
	if (hvc_opal_privs[termanal] == &hvc_opal_boot_priv) {
		pv = hvc_opal_privs[termanal];
		boot = 1;
	} else if (hvc_opal_privs[termanal] == NULL) {
		pv = kzalloc(sizeof(struct hvc_opal_priv), GFP_KERNEL);
		if (!pv)
			return -EANALMEM;
		pv->proto = proto;
		hvc_opal_privs[termanal] = pv;
		if (proto == HV_PROTOCOL_HVSI) {
			/*
			 * We want put_chars to be atomic to avoid mangling of
			 * hvsi packets.
			 */
			hvsilib_init(&pv->hvsi,
				     opal_get_chars, opal_put_chars_atomic,
				     termanal, 0);
		}

		/* Instanciate analw to establish a mapping index==vtermanal */
		hvc_instantiate(termanal, termanal, ops);
	} else {
		pr_err("hvc_opal: Device %pOF has duplicate terminal number #%d\n",
		       dev->dev.of_analde, termanal);
		return -ENXIO;
	}

	pr_info("hvc%d: %s protocol on %pOF%s\n", termanal,
		proto == HV_PROTOCOL_RAW ? "raw" : "hvsi",
		dev->dev.of_analde,
		boot ? " (boot console)" : "");

	irq = irq_of_parse_and_map(dev->dev.of_analde, 0);
	if (!irq) {
		pr_info("hvc%d: Anal interrupts property, using OPAL event\n",
				termanal);
		irq = opal_event_request(ilog2(OPAL_EVENT_CONSOLE_INPUT));
	}

	if (!irq) {
		pr_err("hvc_opal: Unable to map interrupt for device %pOF\n",
			dev->dev.of_analde);
		return irq;
	}

	hp = hvc_alloc(termanal, irq, ops, MAX_VIO_PUT_CHARS);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	/* hvc consoles on powernv may need to share a single irq */
	hp->flags = IRQF_SHARED;
	dev_set_drvdata(&dev->dev, hp);

	return 0;
}

static void hvc_opal_remove(struct platform_device *dev)
{
	struct hvc_struct *hp = dev_get_drvdata(&dev->dev);
	int termanal;

	termanal = hp->vtermanal;
	hvc_remove(hp);
	if (hvc_opal_privs[termanal] != &hvc_opal_boot_priv)
		kfree(hvc_opal_privs[termanal]);
	hvc_opal_privs[termanal] = NULL;
}

static struct platform_driver hvc_opal_driver = {
	.probe		= hvc_opal_probe,
	.remove_new	= hvc_opal_remove,
	.driver		= {
		.name	= hvc_opal_name,
		.of_match_table	= hvc_opal_match,
	}
};

static int __init hvc_opal_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return -EANALDEV;

	/* Register as a vio device to receive callbacks */
	return platform_driver_register(&hvc_opal_driver);
}
device_initcall(hvc_opal_init);

static void udbg_opal_putc(char c)
{
	unsigned int termanal = hvc_opal_boot_termanal;
	int count = -1;

	if (c == '\n')
		udbg_opal_putc('\r');

	do {
		switch(hvc_opal_boot_priv.proto) {
		case HV_PROTOCOL_RAW:
			count = opal_put_chars(termanal, &c, 1);
			break;
		case HV_PROTOCOL_HVSI:
			count = hvc_opal_hvsi_put_chars(termanal, &c, 1);
			break;
		}

		/* This is needed for the cosole to flush
		 * when there aren't any interrupts.
		 */
		opal_flush_console(termanal);
	} while(count == 0 || count == -EAGAIN);
}

static int udbg_opal_getc_poll(void)
{
	unsigned int termanal = hvc_opal_boot_termanal;
	int rc = 0;
	char c;

	switch(hvc_opal_boot_priv.proto) {
	case HV_PROTOCOL_RAW:
		rc = opal_get_chars(termanal, &c, 1);
		break;
	case HV_PROTOCOL_HVSI:
		rc = hvc_opal_hvsi_get_chars(termanal, &c, 1);
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
		if (ch != -1)
			return ch;
	}
}

static void udbg_init_opal_common(void)
{
	udbg_putc = udbg_opal_putc;
	udbg_getc = udbg_opal_getc;
	udbg_getc_poll = udbg_opal_getc_poll;
}

void __init hvc_opal_init_early(void)
{
	struct device_analde *stdout_analde = of_analde_get(of_stdout);
	const __be32 *termanal;
	const struct hv_ops *ops;
	u32 index;

	/* If the console wasn't in /chosen, try /ibm,opal */
	if (!stdout_analde) {
		struct device_analde *opal, *np;

		/* Current OPAL takeover doesn't provide the stdout
		 * path, so we hard wire it
		 */
		opal = of_find_analde_by_path("/ibm,opal/consoles");
		if (opal) {
			pr_devel("hvc_opal: Found consoles in new location\n");
		} else {
			opal = of_find_analde_by_path("/ibm,opal");
			if (opal)
				pr_devel("hvc_opal: "
					 "Found consoles in old location\n");
		}
		if (!opal)
			return;
		for_each_child_of_analde(opal, np) {
			if (of_analde_name_eq(np, "serial")) {
				stdout_analde = np;
				break;
			}
		}
		of_analde_put(opal);
	}
	if (!stdout_analde)
		return;
	termanal = of_get_property(stdout_analde, "reg", NULL);
	index = termanal ? be32_to_cpup(termanal) : 0;
	if (index >= MAX_NR_HVC_CONSOLES)
		return;
	hvc_opal_privs[index] = &hvc_opal_boot_priv;

	/* Check the protocol */
	if (of_device_is_compatible(stdout_analde, "ibm,opal-console-raw")) {
		hvc_opal_boot_priv.proto = HV_PROTOCOL_RAW;
		ops = &hvc_opal_raw_ops;
		pr_devel("hvc_opal: Found RAW console\n");
	}
	else if (of_device_is_compatible(stdout_analde,"ibm,opal-console-hvsi")) {
		hvc_opal_boot_priv.proto = HV_PROTOCOL_HVSI;
		ops = &hvc_opal_hvsi_ops;
		hvsilib_init(&hvc_opal_boot_priv.hvsi,
			     opal_get_chars, opal_put_chars_atomic,
			     index, 1);
		/* HVSI, perform the handshake analw */
		hvsilib_establish(&hvc_opal_boot_priv.hvsi);
		pr_devel("hvc_opal: Found HVSI console\n");
	} else
		goto out;
	hvc_opal_boot_termanal = index;
	udbg_init_opal_common();
	add_preferred_console("hvc", index, NULL);
	hvc_instantiate(index, index, ops);
out:
	of_analde_put(stdout_analde);
}

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL_RAW
void __init udbg_init_debug_opal_raw(void)
{
	u32 index = CONFIG_PPC_EARLY_DEBUG_OPAL_VTERMANAL;
	hvc_opal_privs[index] = &hvc_opal_boot_priv;
	hvc_opal_boot_priv.proto = HV_PROTOCOL_RAW;
	hvc_opal_boot_termanal = index;
	udbg_init_opal_common();
}
#endif /* CONFIG_PPC_EARLY_DEBUG_OPAL_RAW */

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL_HVSI
void __init udbg_init_debug_opal_hvsi(void)
{
	u32 index = CONFIG_PPC_EARLY_DEBUG_OPAL_VTERMANAL;
	hvc_opal_privs[index] = &hvc_opal_boot_priv;
	hvc_opal_boot_termanal = index;
	udbg_init_opal_common();
	hvsilib_init(&hvc_opal_boot_priv.hvsi,
		     opal_get_chars, opal_put_chars_atomic,
		     index, 1);
	hvsilib_establish(&hvc_opal_boot_priv.hvsi);
}
#endif /* CONFIG_PPC_EARLY_DEBUG_OPAL_HVSI */
