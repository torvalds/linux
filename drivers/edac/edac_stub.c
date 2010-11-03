/*
 * common EDAC components that must be in kernel
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc.
 * 2010 (c) Advanced Micro Devices Inc.
 *	    Borislav Petkov <borislav.petkov@amd.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */
#include <linux/module.h>
#include <linux/edac.h>
#include <asm/atomic.h>
#include <asm/edac.h>

int edac_op_state = EDAC_OPSTATE_INVAL;
EXPORT_SYMBOL_GPL(edac_op_state);

atomic_t edac_handlers = ATOMIC_INIT(0);
EXPORT_SYMBOL_GPL(edac_handlers);

int edac_err_assert = 0;
EXPORT_SYMBOL_GPL(edac_err_assert);

static atomic_t edac_class_valid = ATOMIC_INIT(0);

/*
 * called to determine if there is an EDAC driver interested in
 * knowing an event (such as NMI) occurred
 */
int edac_handler_set(void)
{
	if (edac_op_state == EDAC_OPSTATE_POLL)
		return 0;

	return atomic_read(&edac_handlers);
}
EXPORT_SYMBOL_GPL(edac_handler_set);

/*
 * handler for NMI type of interrupts to assert error
 */
void edac_atomic_assert_error(void)
{
	edac_err_assert++;
}
EXPORT_SYMBOL_GPL(edac_atomic_assert_error);

/*
 * sysfs object: /sys/devices/system/edac
 *	need to export to other files
 */
struct sysdev_class edac_class = {
	.name = "edac",
};
EXPORT_SYMBOL_GPL(edac_class);

/* return pointer to the 'edac' node in sysfs */
struct sysdev_class *edac_get_sysfs_class(void)
{
	int err = 0;

	if (atomic_read(&edac_class_valid))
		goto out;

	/* create the /sys/devices/system/edac directory */
	err = sysdev_class_register(&edac_class);
	if (err) {
		printk(KERN_ERR "Error registering toplevel EDAC sysfs dir\n");
		return NULL;
	}

out:
	atomic_inc(&edac_class_valid);
	return &edac_class;
}
EXPORT_SYMBOL_GPL(edac_get_sysfs_class);

void edac_put_sysfs_class(void)
{
	/* last user unregisters it */
	if (atomic_dec_and_test(&edac_class_valid))
		sysdev_class_unregister(&edac_class);
}
EXPORT_SYMBOL_GPL(edac_put_sysfs_class);
