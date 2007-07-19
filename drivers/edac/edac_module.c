/*
 * edac_module.c
 *
 * (C) 2007 www.douglaskthompson.com
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Author: Doug Thompson <norsk5@xmission.com>
 *
 */
#include <linux/edac.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_MC_VERSION "Ver: 2.0.4 " __DATE__

#ifdef CONFIG_EDAC_DEBUG
/* Values of 0 to 4 will generate output */
int edac_debug_level = 1;
EXPORT_SYMBOL_GPL(edac_debug_level);
#endif

/* scope is to module level only */
struct workqueue_struct *edac_workqueue;

/*
 * sysfs object: /sys/devices/system/edac
 *	need to export to other files in this modules
 */
static struct sysdev_class edac_class = {
	set_kset_name("edac"),
};
static int edac_class_valid = 0;

/*
 * edac_op_state_toString()
 */
char *edac_op_state_toString(int opstate)
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
 * edac_get_edac_class()
 *
 *	return pointer to the edac class of 'edac'
 */
struct sysdev_class *edac_get_edac_class(void)
{
	struct sysdev_class *classptr = NULL;

	if (edac_class_valid)
		classptr = &edac_class;

	return classptr;
}

/*
 * edac_register_sysfs_edac_name()
 *
 *	register the 'edac' into /sys/devices/system
 *
 * return:
 *	0  success
 *	!0 error
 */
static int edac_register_sysfs_edac_name(void)
{
	int err;

	/* create the /sys/devices/system/edac directory */
	err = sysdev_class_register(&edac_class);

	if (err) {
		debugf1("%s() error=%d\n", __func__, err);
		return err;
	}

	edac_class_valid = 1;
	return 0;
}

/*
 * sysdev_class_unregister()
 *
 *	unregister the 'edac' from /sys/devices/system
 */
static void edac_unregister_sysfs_edac_name(void)
{
	/* only if currently registered, then unregister it */
	if (edac_class_valid)
		sysdev_class_unregister(&edac_class);

	edac_class_valid = 0;
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

	edac_printk(KERN_INFO, EDAC_MC, EDAC_MC_VERSION "\n");

	/*
	 * Harvest and clear any boot/initialization PCI parity errors
	 *
	 * FIXME: This only clears errors logged by devices present at time of
	 *      module initialization.  We should also do an initial clear
	 *      of each newly hotplugged device.
	 */
	edac_pci_clear_parity_errors();

	/*
	 * perform the registration of the /sys/devices/system/edac object
	 */
	if (edac_register_sysfs_edac_name()) {
		edac_printk(KERN_ERR, EDAC_MC,
			"Error initializing 'edac' kobject\n");
		err = -ENODEV;
		goto error;
	}

	/* Create the MC sysfs entries, must be first
	 */
	if (edac_sysfs_memctrl_setup()) {
		edac_printk(KERN_ERR, EDAC_MC,
			"Error initializing sysfs code\n");
		err = -ENODEV;
		goto error_sysfs;
	}

	/* Setup/Initialize the edac_device system */
	err = edac_workqueue_setup();
	if (err) {
		edac_printk(KERN_ERR, EDAC_MC, "init WorkQueue failure\n");
		goto error_mem;
	}

	return 0;

	/* Error teardown stack */
error_mem:
	edac_sysfs_memctrl_teardown();
error_sysfs:
	edac_unregister_sysfs_edac_name();
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
	edac_sysfs_memctrl_teardown();
	edac_unregister_sysfs_edac_name();
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
