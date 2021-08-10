// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include "mali_kbase_ccswe.h"
#include "mali_kbase_linux.h"

#include <linux/math64.h>
#include <linux/time.h>

static u64 kbasep_ccswe_cycle_at_no_lock(
	struct kbase_ccswe *self, u64 timestamp_ns)
{
	s64 diff_s, diff_ns;
	u32 gpu_freq;

	lockdep_assert_held(&self->access);

	diff_ns = timestamp_ns - self->timestamp_ns;
	gpu_freq = diff_ns > 0 ? self->gpu_freq : self->prev_gpu_freq;

	diff_s = div_s64(diff_ns, NSEC_PER_SEC);
	diff_ns -= diff_s * NSEC_PER_SEC;

	return self->cycles_elapsed + diff_s * gpu_freq
		+ div_s64(diff_ns * gpu_freq, NSEC_PER_SEC);
}

void kbase_ccswe_init(struct kbase_ccswe *self)
{
	memset(self, 0, sizeof(*self));

	spin_lock_init(&self->access);
}

u64 kbase_ccswe_cycle_at(struct kbase_ccswe *self, u64 timestamp_ns)
{
	unsigned long flags;
	u64 result;

	spin_lock_irqsave(&self->access, flags);
	result = kbasep_ccswe_cycle_at_no_lock(self, timestamp_ns);
	spin_unlock_irqrestore(&self->access, flags);

	return result;
}

void kbase_ccswe_freq_change(
	struct kbase_ccswe *self, u64 timestamp_ns, u32 gpu_freq)
{
	unsigned long flags;

	spin_lock_irqsave(&self->access, flags);

	/* The time must go only forward. */
	if (WARN_ON(timestamp_ns < self->timestamp_ns))
		goto exit;

	/* If this is the first frequency change, cycles_elapsed is zero. */
	if (self->timestamp_ns)
		self->cycles_elapsed = kbasep_ccswe_cycle_at_no_lock(
			self, timestamp_ns);

	self->timestamp_ns = timestamp_ns;
	self->prev_gpu_freq = self->gpu_freq;
	self->gpu_freq = gpu_freq;
exit:
	spin_unlock_irqrestore(&self->access, flags);
}

void kbase_ccswe_reset(struct kbase_ccswe *self)
{
	unsigned long flags;

	spin_lock_irqsave(&self->access, flags);

	self->timestamp_ns = 0;
	self->cycles_elapsed = 0;
	self->gpu_freq = 0;
	self->prev_gpu_freq = 0;

	spin_unlock_irqrestore(&self->access, flags);
}
