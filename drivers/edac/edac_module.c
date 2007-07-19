
#include <linux/freezer.h>
#include <linux/kthread.h>

#include "edac_mc.h"
#include "edac_module.h"

#define EDAC_MC_VERSION "Ver: 2.0.3" __DATE__

#ifdef CONFIG_EDAC_DEBUG
/* Values of 0 to 4 will generate output */
int edac_debug_level = 1;
EXPORT_SYMBOL_GPL(edac_debug_level);
#endif

static struct task_struct *edac_thread;

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
 * Action thread for EDAC to perform the POLL operations
 */
static int edac_kernel_thread(void *arg)
{
	int msec;

	while (!kthread_should_stop()) {

		do_edac_check();

		/* goto sleep for the interval */
		msec = (HZ * edac_get_poll_msec()) / 1000;
		schedule_timeout_interruptible(msec);
		try_to_freeze();
	}

	return 0;
}

/*
 * edac_init
 *      module initialization entry point
 */
static int __init edac_init(void)
{
	edac_printk(KERN_INFO, EDAC_MC, EDAC_MC_VERSION "\n");

	/*
	 * Harvest and clear any boot/initialization PCI parity errors
	 *
	 * FIXME: This only clears errors logged by devices present at time of
	 * 	module initialization.  We should also do an initial clear
	 *	of each newly hotplugged device.
	 */
	edac_pci_clear_parity_errors();

	/* Create the MC sysfs entries */
	if (edac_sysfs_memctrl_setup()) {
		edac_printk(KERN_ERR, EDAC_MC,
			"Error initializing sysfs code\n");
		return -ENODEV;
	}

	/* Create the PCI parity sysfs entries */
	if (edac_sysfs_pci_setup()) {
		edac_sysfs_memctrl_teardown();
		edac_printk(KERN_ERR, EDAC_MC,
			"PCI: Error initializing sysfs code\n");
		return -ENODEV;
	}

	/* create our kernel thread */
	edac_thread = kthread_run(edac_kernel_thread, NULL, "kedac");

	if (IS_ERR(edac_thread)) {
		/* remove the sysfs entries */
		edac_sysfs_memctrl_teardown();
		edac_sysfs_pci_teardown();
		return PTR_ERR(edac_thread);
	}

	return 0;
}

/*
 * edac_exit()
 *      module exit/termination function
 */
static void __exit edac_exit(void)
{
	debugf0("%s()\n", __func__);
	kthread_stop(edac_thread);

	/* tear down the sysfs device */
	edac_sysfs_memctrl_teardown();
	edac_sysfs_pci_teardown();
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

