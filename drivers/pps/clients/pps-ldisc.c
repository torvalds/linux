/*
 * pps-ldisc.c -- PPS line discipline
 *
 *
 * Copyright (C) 2008	Rodolfo Giometti <giometti@linux.it>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/pps_kernel.h>

#define PPS_TTY_MAGIC		0x0001

static void pps_tty_dcd_change(struct tty_struct *tty, unsigned int status,
				struct pps_event_time *ts)
{
	struct pps_device *pps = pps_lookup_dev(tty);

	BUG_ON(pps == NULL);

	/* Now do the PPS event report */
	pps_event(pps, ts, status ? PPS_CAPTUREASSERT :
			PPS_CAPTURECLEAR, NULL);

	dev_dbg(pps->dev, "PPS %s at %lu\n",
			status ? "assert" : "clear", jiffies);
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
	if (pps == NULL) {
		pr_err("cannot register PPS source \"%s\"\n", info.path);
		return -ENOMEM;
	}
	pps->lookup_cookie = tty;

	/* Now open the base class N_TTY ldisc */
	ret = alias_n_tty_open(tty);
	if (ret < 0) {
		pr_err("cannot open tty ldisc \"%s\"\n", info.path);
		goto err_unregister;
	}

	dev_info(pps->dev, "source \"%s\" added\n", info.path);

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

	dev_info(pps->dev, "removed\n");
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
	pps_ldisc_ops.magic = PPS_TTY_MAGIC;
	pps_ldisc_ops.name = "pps_tty";
	pps_ldisc_ops.dcd_change = pps_tty_dcd_change;
	pps_ldisc_ops.open = pps_tty_open;
	pps_ldisc_ops.close = pps_tty_close;

	err = tty_register_ldisc(N_PPS, &pps_ldisc_ops);
	if (err)
		pr_err("can't register PPS line discipline\n");
	else
		pr_info("PPS line discipline registered\n");

	return err;
}

static void __exit pps_tty_cleanup(void)
{
	int err;

	err = tty_unregister_ldisc(N_PPS);
	if (err)
		pr_err("can't unregister PPS line discipline\n");
	else
		pr_info("PPS line discipline removed\n");
}

module_init(pps_tty_init);
module_exit(pps_tty_cleanup);

MODULE_ALIAS_LDISC(N_PPS);
MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("PPS TTY device driver");
MODULE_LICENSE("GPL");
