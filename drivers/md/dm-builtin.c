// SPDX-License-Identifier: GPL-2.0-only
#include "dm-core.h"

/*
 * The kobject release method must not be placed in the module itself,
 * otherwise we are subject to module unload races.
 *
 * The release method is called when the last reference to the kobject is
 * dropped. It may be called by any other kernel code that drops the last
 * reference.
 *
 * The release method suffers from module unload race. We may prevent the
 * module from being unloaded at the start of the release method (using
 * increased module reference count or synchronizing against the release
 * method), however there is no way to prevent the module from being
 * unloaded at the end of the release method.
 *
 * If this code were placed in the dm module, the following race may
 * happen:
 *  1. Some other process takes a reference to dm kobject
 *  2. The user issues ioctl function to unload the dm device
 *  3. dm_sysfs_exit calls kobject_put, however the object is not released
 *     because of the other reference taken at step 1
 *  4. dm_sysfs_exit waits on the completion
 *  5. The other process that took the reference in step 1 drops it,
 *     dm_kobject_release is called from this process
 *  6. dm_kobject_release calls complete()
 *  7. a reschedule happens before dm_kobject_release returns
 *  8. dm_sysfs_exit continues, the dm device is unloaded, module reference
 *     count is decremented
 *  9. The user unloads the dm module
 * 10. The other process that was rescheduled in step 7 continues to run,
 *     it is now executing code in unloaded module, so it crashes
 *
 * Note that if the process that takes the foreign reference to dm kobject
 * has a low priority and the system is sufficiently loaded with
 * higher-priority processes that prevent the low-priority process from
 * being scheduled long enough, this bug may really happen.
 *
 * In order to fix this module unload race, we place the release method
 * into a helper code that is compiled directly into the kernel.
 */

void dm_kobject_release(struct kobject *kobj)
{
	complete(dm_get_completion_from_kobject(kobj));
}
EXPORT_SYMBOL(dm_kobject_release);
