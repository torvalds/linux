/*********************************************************************
 *
 * Filename:      irmod.c
 * Version:       0.9
 * Description:   IrDA stack main entry points
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:55:39 1997
 * Modified at:   Wed Jan  5 15:12:41 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1997, 1999-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2004 Jean Tourrilhes <jt@hpl.hp.com>
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

/*
 * This file contains the main entry points of the IrDA stack.
 * They are in this file and not af_irda.c because some developpers
 * are using the IrDA stack without the socket API (compiling out
 * af_irda.c).
 * Jean II
 */

#include <linux/module.h>
#include <linux/moduleparam.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>		/* notify_t */
#include <net/irda/irlap.h>		/* irlap_init */
#include <net/irda/irlmp.h>		/* irlmp_init */
#include <net/irda/iriap.h>		/* iriap_init */
#include <net/irda/irttp.h>		/* irttp_init */
#include <net/irda/irda_device.h>	/* irda_device_init */

/* Packet type handler.
 * Tell the kernel how IrDA packets should be handled.
 */
static struct packet_type irda_packet_type __read_mostly = {
	.type	= cpu_to_be16(ETH_P_IRDA),
	.func	= irlap_driver_rcv,	/* Packet type handler irlap_frame.c */
};

/*
 * Function irda_notify_init (notify)
 *
 *    Used for initializing the notify structure
 *
 */
void irda_notify_init(notify_t *notify)
{
	notify->data_indication = NULL;
	notify->udata_indication = NULL;
	notify->connect_confirm = NULL;
	notify->connect_indication = NULL;
	notify->disconnect_indication = NULL;
	notify->flow_indication = NULL;
	notify->status_indication = NULL;
	notify->instance = NULL;
	strlcpy(notify->name, "Unknown", sizeof(notify->name));
}
EXPORT_SYMBOL(irda_notify_init);

/*
 * Function irda_init (void)
 *
 *  Protocol stack initialisation entry point.
 *  Initialise the various components of the IrDA stack
 */
static int __init irda_init(void)
{
	int ret = 0;

	/* Lower layer of the stack */
	irlmp_init();
	irlap_init();

	/* Driver/dongle support */
	irda_device_init();

	/* Higher layers of the stack */
	iriap_init();
	irttp_init();
	ret = irsock_init();
	if (ret < 0)
		goto out_err_1;

	/* Add IrDA packet type (Start receiving packets) */
	dev_add_pack(&irda_packet_type);

	/* External APIs */
#ifdef CONFIG_PROC_FS
	irda_proc_register();
#endif
#ifdef CONFIG_SYSCTL
	ret = irda_sysctl_register();
	if (ret < 0)
		goto out_err_2;
#endif

	ret = irda_nl_register();
	if (ret < 0)
		goto out_err_3;

	return 0;

 out_err_3:
#ifdef CONFIG_SYSCTL
	irda_sysctl_unregister();
 out_err_2:
#endif
#ifdef CONFIG_PROC_FS
	irda_proc_unregister();
#endif

	/* Remove IrDA packet type (stop receiving packets) */
	dev_remove_pack(&irda_packet_type);

	/* Remove higher layers */
	irsock_cleanup();
 out_err_1:
	irttp_cleanup();
	iriap_cleanup();

	/* Remove lower layers */
	irda_device_cleanup();
	irlap_cleanup(); /* Must be done before irlmp_cleanup()! DB */

	/* Remove middle layer */
	irlmp_cleanup();


	return ret;
}

/*
 * Function irda_cleanup (void)
 *
 *  Protocol stack cleanup/removal entry point.
 *  Cleanup the various components of the IrDA stack
 */
static void __exit irda_cleanup(void)
{
	/* Remove External APIs */
	irda_nl_unregister();

#ifdef CONFIG_SYSCTL
	irda_sysctl_unregister();
#endif
#ifdef CONFIG_PROC_FS
	irda_proc_unregister();
#endif

	/* Remove IrDA packet type (stop receiving packets) */
	dev_remove_pack(&irda_packet_type);

	/* Remove higher layers */
	irsock_cleanup();
	irttp_cleanup();
	iriap_cleanup();

	/* Remove lower layers */
	irda_device_cleanup();
	irlap_cleanup(); /* Must be done before irlmp_cleanup()! DB */

	/* Remove middle layer */
	irlmp_cleanup();
}

/*
 * The IrDA stack must be initialised *before* drivers get initialised,
 * and *before* higher protocols (IrLAN/IrCOMM/IrNET) get initialised,
 * otherwise bad things will happen (hashbins will be NULL for example).
 * Those modules are at module_init()/device_initcall() level.
 *
 * On the other hand, it needs to be initialised *after* the basic
 * networking, the /proc/net filesystem and sysctl module. Those are
 * currently initialised in .../init/main.c (before initcalls).
 * Also, IrDA drivers needs to be initialised *after* the random number
 * generator (main stack and higher layer init don't need it anymore).
 *
 * Jean II
 */
device_initcall(irda_init);
module_exit(irda_cleanup);

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no> & Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("The Linux IrDA Protocol Stack");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_IRDA);
