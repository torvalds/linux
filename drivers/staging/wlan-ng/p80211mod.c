/* src/p80211/p80211mod.c
*
* Module entry and exit for p80211
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*
* This file contains the p80211.o entry and exit points defined for linux
* kernel modules.
*
* Notes:
* - all module parameters for  p80211.o should be defined here.
*
* --------------------------------------------------------------------
*/

/*================================================================*/
/* System Includes */


#include <linux/version.h>

#include <linux/module.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,25))
#include <linux/moduleparam.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>

#include "version.h"
#include "wlan_compat.h"

/*================================================================*/
/* Project Includes */

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211req.h"

/*================================================================*/
/* Local Constants */


/*================================================================*/
/* Local Macros */


/*================================================================*/
/* Local Types */


/*================================================================*/
/* Local Static Definitions */

static char *version = "p80211.o: " WLAN_RELEASE;


/*----------------------------------------------------------------*/
/* --Module Parameters */

int wlan_watchdog = 5000;
module_param(wlan_watchdog, int, 0644);
MODULE_PARM_DESC(wlan_watchdog, "transmit timeout in milliseconds");

int wlan_wext_write = 0;
#if WIRELESS_EXT > 12
module_param(wlan_wext_write, int, 0644);
MODULE_PARM_DESC(wlan_wext_write, "enable write wireless extensions");
#endif

#ifdef WLAN_INCLUDE_DEBUG
int wlan_debug=0;
module_param(wlan_debug, int, 0644);
MODULE_PARM_DESC(wlan_debug, "p80211 debug level");
#endif

MODULE_LICENSE("Dual MPL/GPL");

/*================================================================*/
/* Local Function Declarations */

int	init_module(void);
void	cleanup_module(void);

/*================================================================*/
/* Function Definitions */

/*----------------------------------------------------------------
* init_module
*
* Module initialization routine, called once at module load time.
*
* Arguments:
*	none
*
* Returns:
*	0	- success
*	~0	- failure, module is unloaded.
*
* Side effects:
*	TODO: define
*
* Call context:
*	process thread (insmod or modprobe)
----------------------------------------------------------------*/
int init_module(void)
{
        DBFENTER;

#if 0
        printk(KERN_NOTICE "%s (%s) Loaded\n", version, WLAN_BUILD_DATE);
#endif

	p80211netdev_startup();
#ifdef CONFIG_HOTPLUG
	p80211_run_sbin_hotplug(NULL, WLAN_HOTPLUG_STARTUP);
#endif

        DBFEXIT;
        return 0;
}


/*----------------------------------------------------------------
* cleanup_module
*
* Called at module unload time.  This is our last chance to
* clean up after ourselves.
*
* Arguments:
*	none
*
* Returns:
*	nothing
*
* Side effects:
*	TODO: define
*
* Call context:
*	process thread
*
----------------------------------------------------------------*/
void cleanup_module(void)
{
        DBFENTER;

#ifdef CONFIG_HOTPLUG
	p80211_run_sbin_hotplug(NULL, WLAN_HOTPLUG_SHUTDOWN);
#endif
	p80211netdev_shutdown();
        printk(KERN_NOTICE "%s Unloaded\n", version);

        DBFEXIT;
        return;
}

EXPORT_SYMBOL(p80211netdev_hwremoved);
EXPORT_SYMBOL(register_wlandev);
EXPORT_SYMBOL(p80211netdev_rx);
EXPORT_SYMBOL(unregister_wlandev);
EXPORT_SYMBOL(wlan_setup);
EXPORT_SYMBOL(wlan_unsetup);
EXPORT_SYMBOL(p80211_suspend);
EXPORT_SYMBOL(p80211_resume);

EXPORT_SYMBOL(p80211skb_free);
EXPORT_SYMBOL(p80211skb_rxmeta_attach);

EXPORT_SYMBOL(p80211wext_event_associated);
