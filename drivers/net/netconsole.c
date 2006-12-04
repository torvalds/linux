/*
 *  linux/drivers/net/netconsole.c
 *
 *  Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 *
 *  This file contains the implementation of an IRQ-safe, crash-safe
 *  kernel console implementation that outputs kernel messages to the
 *  network.
 *
 * Modification history:
 *
 * 2001-09-17    started by Ingo Molnar.
 * 2003-08-11    2.6 port by Matt Mackall
 *               simplified options
 *               generic card hooks
 *               works non-modular
 * 2003-09-07    rewritten with netpoll api
 */

/****************************************************************
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2, or (at your option)
 *      any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty_driver.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/smp.h>
#include <linux/netpoll.h>

MODULE_AUTHOR("Maintainer: Matt Mackall <mpm@selenic.com>");
MODULE_DESCRIPTION("Console driver for network interfaces");
MODULE_LICENSE("GPL");

static char config[256];
module_param_string(netconsole, config, 256, 0);
MODULE_PARM_DESC(netconsole, " netconsole=[src-port]@[src-ip]/[dev],[tgt-port]@<tgt-ip>/[tgt-macaddr]\n");

static struct netpoll np = {
	.name = "netconsole",
	.dev_name = "eth0",
	.local_port = 6665,
	.remote_port = 6666,
	.remote_mac = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
};
static int configured = 0;

#define MAX_PRINT_CHUNK 1000

static void write_msg(struct console *con, const char *msg, unsigned int len)
{
	int frag, left;
	unsigned long flags;

	if (!np.dev)
		return;

	local_irq_save(flags);

	for(left = len; left; ) {
		frag = min(left, MAX_PRINT_CHUNK);
		netpoll_send_udp(&np, msg, frag);
		msg += frag;
		left -= frag;
	}

	local_irq_restore(flags);
}

static struct console netconsole = {
	.name = "netcon",
	.flags = CON_ENABLED | CON_PRINTBUFFER,
	.write = write_msg
};

static int option_setup(char *opt)
{
	configured = !netpoll_parse_options(&np, opt);
	return 1;
}

__setup("netconsole=", option_setup);

static int init_netconsole(void)
{
	int err;

	if(strlen(config))
		option_setup(config);

	if(!configured) {
		printk("netconsole: not configured, aborting\n");
		return 0;
	}

	err = netpoll_setup(&np);
	if (err)
		return err;

	register_console(&netconsole);
	printk(KERN_INFO "netconsole: network logging started\n");
	return 0;
}

static void cleanup_netconsole(void)
{
	unregister_console(&netconsole);
	netpoll_cleanup(&np);
}

module_init(init_netconsole);
module_exit(cleanup_netconsole);
