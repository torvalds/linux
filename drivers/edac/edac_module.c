
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/edac.h>

#include "edac_mc.h"
#include "edac_module.h"

#define EDAC_MC_VERSION "Ver: 2.0.3" __DATE__

#ifdef CONFIG_EDAC_DEBUG
/* Values of 0 to 4 will generate output */
int edac_debug_level = 1;
EXPORT_SYMBOL_GPL(edac_debug_level);
#endif

/* scope is to module level only */
struct workqueue_struct *edac_workqueue;

/* private to this file */
static struct task_struct *edac_thread;


/*
 * sysfs object: /sys/devices/system/edac
 *	need to export to other files in this modules
 */
static struct sysdev_class edac_class = {
	set_kset_name("edac"),
};
static int edac_class_valid = 0;

/*
 * edac_get_edac_class()
 *
 *	return pointer to the edac class of 'edac'
 */
struct sysdev_class *edac_get_edac_class(void)
{
	struct sysdev_class *classptr=NULL;

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
 * Check MC status every edac_get_poll_msec().
 * Check PCI status every edac_get_poll_msec() as well.
 *
 * This where the work gets done for edac.
 *
 * SMP safe, doesn't use NMI, and auto-rate-limits.
 */
static void do_edac_check(void)
{
	debugf3("%s()\n", __func__);

	/* perform the poll activities */
	edac_check_mc_devices();
	edac_pci_do_parity_check();
}

/*
 * handler for EDAC to check if NMI type handler has asserted interrupt
 */
static int edac_assert_error_check_and_clear(void)
{
	int vreg;

	if(edac_op_state == EDAC_OPSTATE_POLL)
		return 1;

	vreg = atomic_read(&edac_err_assert);
	if(vreg) {
		atomic_set(&edac_err_assert, 0);
		return 1;
	}

	return 0;
}

/*
 * Action thread for EDAC to perform the POLL operations
 */
static int edac_kernel_thread(void *arg)
{
	int msec;

	while (!kthread_should_stop()) {
		if(edac_assert_error_check_and_clear())
			do_edac_check();

		/* goto sleep for the interval */
		msec = (HZ * edac_get_poll_msec()) / 1000;
		schedule_timeout_interruptible(msec);
		try_to_freeze();
	}

	return 0;
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
	 * 	module initialization.  We should also do an initial clear
	 *	of each newly hotplugged device.
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

	/* Create the PCI parity sysfs entries */
	if (edac_sysfs_pci_setup()) {
		edac_printk(KERN_ERR, EDAC_MC,
			"PCI: Error initializing sysfs code\n");
		err = -ENODEV;
		goto error_mem;
	}

	/* Setup/Initialize the edac_device system */
	err = edac_workqueue_setup();
	if (err) {
		edac_printk(KERN_ERR, EDAC_MC, "init WorkQueue failure\n");
		goto error_pci;
	}

	/* create our kernel thread */
	edac_thread = kthread_run(edac_kernel_thread, NULL, "kedac");

	if (IS_ERR(edac_thread)) {
		err = PTR_ERR(edac_thread);
		goto error_work;
	}

	return 0;

	/* Error teardown stack */
error_work:
	edac_workqueue_teardown();
error_pci:
	edac_sysfs_pci_teardown();
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
	kthread_stop(edac_thread);

	/* tear down the various subsystems*/
	edac_workqueue_teardown();
	edac_sysfs_memctrl_teardown();
	edac_sysfs_pci_teardown();
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

