/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/*
 * Base kernel disjoint events helper functions
 */

#include <mali_kbase.h>

void kbase_disjoint_init(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	atomic_set(&kbdev->disjoint_event.count, 0);
	atomic_set(&kbdev->disjoint_event.state, 0);
}

/* increment the disjoint event count */
void kbase_disjoint_event(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	atomic_inc(&kbdev->disjoint_event.count);
}

/* increment the state and the event counter */
void kbase_disjoint_state_up(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	atomic_inc(&kbdev->disjoint_event.state);

	kbase_disjoint_event(kbdev);
}

/* decrement the state */
void kbase_disjoint_state_down(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(atomic_read(&kbdev->disjoint_event.state) > 0);

	kbase_disjoint_event(kbdev);

	atomic_dec(&kbdev->disjoint_event.state);
}

/* increments the count only if the state is > 0 */
void kbase_disjoint_event_potential(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	if (atomic_read(&kbdev->disjoint_event.state))
		kbase_disjoint_event(kbdev);
}

u32 kbase_disjoint_event_get(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	return atomic_read(&kbdev->disjoint_event.count);
}
KBASE_EXPORT_TEST_API(kbase_disjoint_event_get);
