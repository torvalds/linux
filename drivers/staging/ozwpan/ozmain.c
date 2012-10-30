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
#include "ozconfig.h"
#include "ozpd.h"
#include "ozproto.h"
#include "ozcdev.h"
#include "oztrace.h"
#include "ozevent.h"
/*------------------------------------------------------------------------------
 * The name of the 802.11 mac device. Empty string is the default value but a
 * value can be supplied as a parameter to the module. An empty string means
 * bind to nothing. '*' means bind to all netcards - this includes non-802.11
 * netcards. Bindings can be added later using an IOCTL.
 */
char *g_net_dev = "";
/*------------------------------------------------------------------------------
 * Context: process
 */
static int __init ozwpan_init(void)
{
	oz_event_init();
	oz_cdev_register();
	oz_protocol_init(g_net_dev);
	oz_app_enable(OZ_APPID_USB, 1);
	oz_apps_init();
#ifdef CONFIG_DEBUG_FS
	oz_debugfs_init();
#endif
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
static void __exit ozwpan_exit(void)
{
	oz_protocol_term();
	oz_apps_term();
	oz_cdev_deregister();
	oz_event_term();
#ifdef CONFIG_DEBUG_FS
	oz_debugfs_remove();
#endif
}
/*------------------------------------------------------------------------------
 */
module_param(g_net_dev, charp, S_IRUGO);
module_init(ozwpan_init);
module_exit(ozwpan_exit);

MODULE_AUTHOR("Chris Kelly");
MODULE_DESCRIPTION("Ozmo Devices USB over WiFi hcd driver");
MODULE_VERSION("1.0.13");
MODULE_LICENSE("GPL");

