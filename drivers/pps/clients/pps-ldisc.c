// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pps-ldisc.c -- PPS line discipline
 *
 * Copyright (C) 2008	Rodolfo Giometti <giometti@linux.it>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/pps_kernel.h>
#include <linux/bug.h>

static void pps_tty_dcd_change(struct tty_struct *tty, bool active)
{
	struct pps_device *pps;
	struct pps_event_time ts;

	pps_get_ts(&ts);

	pps = pps_lookup_dev(tty);
	/*
	 * This should never fail, but the ldisc locking is very
	 * convoluted, so don't crash just in case.
	 */
	if (WARN_ON_ONCE(pps == NULL))
		return;

	/* Now do the PPS event report */
	pps_event(pps, &ts, active ? PPS_CAPTUREASSERT :
			PPS_CAPTURECLEAR, NULL);

	dev_dbg(&pps->dev, "PPS %s at %lu\n",
			active ? "assert" : "clear", jiffies);
}

static int (*alias_n_tty_open)(struct tty_struct *tty);

static int pps_tty_open(struct tty_struct *tty)
{
	struct pps_source_info info;
	struct tty_driver *drv = tty->driver;
	int index = tty->index + drv->name_base;
	struct pps_device *pps;
	int ret;

	info.owner = THIS_MODULE;
	info.dev = NULL;
	snprintf(info.name, PPS_MAX_NAME_LEN, "%s%d", drv->driver_name, index);
	snprintf(info.path, PPS_MAX_NAME_LEN, "/dev/%s%d", drv->name, index);
	info.mode = PPS_CAPTUREBOTH | \
			PPS_OFFSETASSERT | PPS_OFFSETCLEAR | \
			PPS_CANWAIT | PPS_TSFMT_TSPEC;

	pps = pps_register_source(&info, PPS_CAPTUREBOTH | \
				PPS_OFFSETASSERT | PPS_OFFSETCLEAR);
	if (IS_ERR(pps)) {
		pr_err("cannot register PPS source \"%s\"\n", info.path);
		return PTR_ERR(pps);
	}
	pps->lookup_cookie = tty;

	/* Now open the base class N_TTY ldisc */
	ret = alias_n_tty_open(tty);
	if (ret < 0) {
		pr_err("cannot open tty ldisc \"%s\"\n", info.path);
		goto err_unregister;
	}

	dev_dbg(&pps->dev, "source \"%s\" added\n", info.path);

	return 0;

err_unregister:
	pps_unregister_source(pps);
	return ret;
}

static void (*alias_n_tty_close)(struct tty_struct *tty);

static void pps_tty_close(struct tty_struct *tty)
{
	struct pps_device *pps = pps_lookup_dev(tty);

	alias_n_tty_close(tty);

	if (WARN_ON(!pps))
		return;

	dev_info(&pps->dev, "removed\n");
	pps_unregister_source(pps);
}

static struct tty_ldisc_ops pps_ldisc_ops;

/*
 * Module stuff
 */

static int __init pps_tty_init(void)
{
	int err;

	/* Inherit the N_TTY's ops */
	n_tty_inherit_ops(&pps_ldisc_ops);

	/* Save N_TTY's open()/close() methods */
	alias_n_tty_open = pps_ldisc_ops.open;
	alias_n_tty_close = pps_ldisc_ops.close;

	/* Init PPS_TTY data */
	pps_ldisc_ops.owner = THIS_MODULE;
	pps_ldisc_ops.num = N_PPS;
	pps_ldisc_ops.name = "pps_tty";
	pps_ldisc_ops.dcd_change = pps_tty_dcd_change;
	pps_ldisc_ops.open = pps_tty_open;
	pps_ldisc_ops.close = pps_tty_close;

	err = tty_register_ldisc(&pps_ldisc_ops);
	if (err)
		pr_err("can't register PPS line discipline\n");
	else
		pr_info("PPS line discipline registered\n");

	return err;
}

static void __exit pps_tty_cleanup(void)
{
	tty_unregister_ldisc(&pps_ldisc_ops);
}

module_init(pps_tty_init);
module_exit(pps_tty_cleanup);

MODULE_ALIAS_LDISC(N_PPS);
MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("PPS TTY device driver");
MODULE_LICENSE("GPL");
