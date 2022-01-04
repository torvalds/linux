/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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

/*
 * Kinstr_prfcnt, used to provide an ioctl for userspace access to
 * performance counters.
 */
#ifndef _KBASE_KINSTR_PRFCNT_H_
#define _KBASE_KINSTR_PRFCNT_H_

#include <uapi/gpu/arm/bifrost/mali_kbase_hwcnt_reader.h>

struct kbase_kinstr_prfcnt_context;
struct kbase_hwcnt_virtualizer;
struct kbase_ioctl_hwcnt_reader_setup;
struct kbase_ioctl_kinstr_prfcnt_enum_info;
union kbase_ioctl_kinstr_prfcnt_setup;

/**
 * kbase_kinstr_prfcnt_init() - Initialize a kinstr_prfcnt context.
 * @hvirt:          Non-NULL pointer to the hardware counter virtualizer.
 * @out_kinstr_ctx: Non-NULL pointer to where the pointer to the created
 *                  kinstr_prfcnt context will be stored on success.
 *
 * On creation, the suspend count of the context will be 0.
 *
 * Return: 0 on success, else error code.
 */
int kbase_kinstr_prfcnt_init(
	struct kbase_hwcnt_virtualizer *hvirt,
	struct kbase_kinstr_prfcnt_context **out_kinstr_ctx);

/**
 * kbase_kinstr_prfcnt_term() - Terminate a kinstr_prfcnt context.
 * @kinstr_ctx: Pointer to the kinstr_prfcnt context to be terminated.
 */
void kbase_kinstr_prfcnt_term(struct kbase_kinstr_prfcnt_context *kinstr_ctx);

/**
 * kbase_kinstr_prfcnt_suspend() - Increment the suspend count of the context.
 * @kinstr_ctx: Non-NULL pointer to the kinstr_prfcnt context to be suspended.
 *
 * After this function call returns, it is guaranteed that all timers and
 * workers in kinstr_prfcnt will be canceled, and will not be re-triggered until
 * after the context has been resumed. In effect, this means no new counter
 * dumps will occur for any existing or subsequently added periodic clients.
 */
void kbase_kinstr_prfcnt_suspend(struct kbase_kinstr_prfcnt_context *kinstr_ctx);

/**
 * kbase_kinstr_prfcnt_resume() - Decrement the suspend count of the context.
 * @kinstr_ctx: Non-NULL pointer to the kinstr_prfcnt context to be resumed.
 *
 * If a call to this function decrements the suspend count from 1 to 0, then
 * normal operation of kinstr_prfcnt will be resumed (i.e. counter dumps will once
 * again be automatically triggered for all periodic clients).
 *
 * It is only valid to call this function one time for each prior returned call
 * to kbase_kinstr_prfcnt_suspend.
 */
void kbase_kinstr_prfcnt_resume(struct kbase_kinstr_prfcnt_context *kinstr_ctx);

#if MALI_KERNEL_TEST_API
/**
 * kbasep_kinstr_prfcnt_get_block_info_list() - Get list of all block types
 *                                              with their information.
 * @metadata:  Non-NULL pointer to the hardware counter metadata.
 * @block_set: Which SET the blocks will represent.
 * @item_arr:  Non-NULL pointer to array of enumeration items to populate.
 * @arr_idx:   Non-NULL pointer to index of array @item_arr.
 *
 * Populate list of counter blocks with information for enumeration.
 *
 * Return: 0 on success, else error code.
 */
int kbasep_kinstr_prfcnt_get_block_info_list(const struct kbase_hwcnt_metadata *metadata,
					     size_t block_set, struct prfcnt_enum_item *item_arr,
					     size_t *arr_idx);

/**
 * kbasep_kinstr_prfcnt_get_sample_md_count() - Get count of sample
 *                                              metadata items.
 * @metadata: Non-NULL pointer to the hardware counter metadata.
 *
 * Return: Number of metadata items for available blocks in each sample.
 */
size_t kbasep_kinstr_prfcnt_get_sample_md_count(const struct kbase_hwcnt_metadata *metadata);

/**
 * kbasep_kinstr_prfcnt_set_block_meta_items() - Populate a sample's block meta
 *                                               item array.
 * @dst:             Non-NULL pointer to the sample's dump buffer object.
 * @block_meta_base: Non-NULL double pointer to the start of the block meta
 *                   data items.
 * @base_addr:       Address of allocated pages for array of samples. Used
 *                   to calculate offset of block values.
 * @counter_set:     The SET which blocks represent.
 *
 * Return: 0 on success, else error code.
 */
int kbasep_kinstr_prfcnt_set_block_meta_items(struct kbase_hwcnt_dump_buffer *dst,
					      struct prfcnt_metadata **block_meta_base,
					      u64 base_addr, u8 counter_set);
#endif /* MALI_KERNEL_TEST_API */

/**
 * kbase_kinstr_prfcnt_enum_info - Enumerate performance counter information.
 * @kinstr_ctx: Non-NULL pointer to the kinstr_prfcnt context.
 * @enum_info:  Non-NULL pointer to the enumeration information.
 *
 * Enumerate which counter blocks and banks exist, and what counters are
 * available within them.
 *
 * Return: 0 on success, else error code.
 */
int kbase_kinstr_prfcnt_enum_info(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info);

/**
 * kbase_kinstr_prfcnt_setup() - Set up a new hardware counter reader client.
 * @kinstr_ctx: Non-NULL pointer to the kinstr_prfcnt context.
 * @setup:      Non-NULL pointer to the hwcnt reader configuration.
 *
 * Start a session between a user client and the kinstr_prfcnt component.
 * A file descriptor shall be provided to the client as a handle to the
 * hardware counter reader client that represents the session.
 *
 * Return: file descriptor on success, else error code.
 */
int kbase_kinstr_prfcnt_setup(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
			      union kbase_ioctl_kinstr_prfcnt_setup *setup);

#endif /* _KBASE_KINSTR_PRFCNT_H_ */
