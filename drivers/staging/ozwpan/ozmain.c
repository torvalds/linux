/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/ieee80211.h>
#include "ozdbg.h"
#include "ozpd.h"
#include "ozproto.h"
#include "ozcdev.h"

unsigned int oz_dbg_mask = OZ_DEFAULT_DBG_MASK;

/*
 * The name of the 802.11 mac device. Empty string is the default value but a
 * value can be supplied as a parameter to the module. An empty string means
 * bind to nothing. '*' means bind to all netcards - this includes non-802.11
 * netcards. Bindings can be added later using an IOCTL.
 */
static char *g_net_dev = "";
module_param(g_net_dev, charp, S_IRUGO);
MODULE_PARM_DESC(g_net_dev, "The device(s) to bind to; "
	"'*' means all, '' (empty string; default) means none.");

/*
 * Context: process
 */
static int __init ozwpan_init(void)
{
	int err;

	err = oz_cdev_register();
	if (err)
		return err;
	err = oz_protocol_init(g_net_dev);
	if (err)
		goto err_protocol;
	oz_app_enable(OZ_APPID_USB, 1);
	oz_apps_init();
	return 0;

err_protocol:
	oz_cdev_deregister();
	return err;
}

/*
 * Context: process
 */
static void __exit ozwpan_exit(void)
{
	oz_protocol_term();
	oz_apps_term();
	oz_cdev_deregister();
}

module_init(ozwpan_init);
module_exit(ozwpan_exit);

MODULE_AUTHOR("Chris Kelly");
MODULE_DESCRIPTION("Ozmo Devices USB over WiFi hcd driver");
MODULE_VERSION("1.0.13");
MODULE_LICENSE("GPL");

