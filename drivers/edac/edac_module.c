/*
 * edac_module.c
 *
 * (C) 2007 www.softwarebitmaker.com
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Author: Doug Thompson <dougthompson@xmission.com>
 *
 */
#include <linux/edac.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_VERSION "Ver: 2.1.0 " __DATE__

#ifdef CONFIG_EDAC_DEBUG
/* Values of 0 to 4 will generate output */
int edac_debug_level = 2;
EXPORT_SYMBOL_GPL(edac_debug_level);
#endif

/* scope is to module level only */
struct workqueue_struct *edac_workqueue;

/*
 * edac_op_state_to_string()
 */
char *edac_op_state_to_string(int opstate)
{
	if (opstate == OP_RUNNING_POLL)
		return "POLLED";
	else if (opstate == OP_RUNNING_INTERRUPT)
		return "INTERRUPT";
	else if (opstate == OP_RUNNING_POLL_INTR)
		return "POLL-INTR";
	else if (opstate == OP_ALLOC)
		return "ALLOC";
	else if (opstate == OP_OFFLINE)
		return "OFFLINE";

	return "UNKNOWN";
}

/*
 * edac_workqueue_setup
 *	initialize the edac work queue for polling operations
 */
static int edac_workqueue_setup(void)
{
	edac_workqueue = create_singlethread_workqueue("edac-poller");
	if (edac_workqueue == NULL)
		return -ENODEV;
	else
		return 0;
}

/*
 * edac_workqueue_teardown
 *	teardown the edac workqueue
 */
static void edac_workqueue_teardown(void)
{
	if (edac_workqueue) {
		flush_workqueue(edac_workqueue);
		destroy_workqueue(edac_workqueue);
		edac_workqueue = NULL;
	}
}

/*
 * edac_init
 *      module initialization entry point
 */
static int __init edac_init(void)
{
	int err = 0;

	edac_printk(KERN_INFO, EDAC_MC, EDAC_VERSION "\n");

	/*
	 * Harvest and clear any boot/initialization PCI parity errors
	 *
	 * FIXME: This only clears errors logged by devices present at time of
	 *      module initialization.  We should also do an initial clear
	 *      of each newly hotplugged device.
	 */
	edac_pci_clear_parity_errors();

	/*
	 * now set up the mc_kset under the edac class object
	 */
	err = edac_sysfs_setup_mc_kset();
	if (err)
		goto error;

	/* Setup/Initialize the workq for this core */
	err = edac_workqueue_setup();
	if (err) {
		edac_printk(KERN_ERR, EDAC_MC, "init WorkQueue failure\n");
		goto workq_fail;
	}

	return 0;

	/* Error teardown stack */
workq_fail:
	edac_sysfs_teardown_mc_kset();

error:
	return err;
}

/*
 * edac_exit()
 *      module exit/termination function
 */
static void __exit edac_exit(void)
{
	debugf0("%s()\n", __func__);

	/* tear down the various subsystems */
	edac_workqueue_teardown();
	edac_sysfs_teardown_mc_kset();
}

/*
 * Inform the kernel of our entry and exit points
 */
module_init(edac_init);
module_exit(edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Doug Thompson www.softwarebitmaker.com, et al");
MODULE_DESCRIPTION("Core library routines for EDAC reporting");

/* refer to *_sysfs.c files for parameters that are exported via sysfs */

#ifdef CONFIG_EDAC_DEBUG
module_param(edac_debug_level, int, 0644);
MODULE_PARM_DESC(edac_debug_level, "Debug level");
#endif
