/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_MCU_SHARED_REG_H_
#define _KBASE_CSF_MCU_SHARED_REG_H_

/**
 * kbase_csf_mcu_shared_set_group_csg_reg_active - Notify that the group is active on-slot with
 *                                                 scheduling action. Essential runtime resources
 *                                                 are bound with the group for it to run
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @group: Pointer to the group that is placed into active on-slot running by the scheduler.
 *
 */
void kbase_csf_mcu_shared_set_group_csg_reg_active(struct kbase_device *kbdev,
						   struct kbase_queue_group *group);

/**
 * kbase_csf_mcu_shared_set_group_csg_reg_unused - Notify that the group is placed off-slot with
 *                                                 scheduling action. Some of bound runtime
 *                                                 resources can be reallocated for others to use
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @group: Pointer to the group that is placed off-slot by the scheduler.
 *
 */
void kbase_csf_mcu_shared_set_group_csg_reg_unused(struct kbase_device *kbdev,
						   struct kbase_queue_group *group);

/**
 * kbase_csf_mcu_shared_group_update_pmode_map - Request to update the given group's protected
 *                                             suspend buffer pages to be mapped for supporting
 *                                             protected mode operations.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @group: Pointer to the group for attempting a protected mode suspend buffer binding/mapping.
 *
 * Return: 0 for success, the group has a protected suspend buffer region mapped. Otherwise an
 *         error code is returned.
 */
int kbase_csf_mcu_shared_group_update_pmode_map(struct kbase_device *kbdev,
						struct kbase_queue_group *group);

/**
 * kbase_csf_mcu_shared_clear_evicted_group_csg_reg - Clear any bound regions/mappings as the
 *                                                    given group is evicted out of the runtime
 *                                                    operations.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @group: Pointer to the group that has been evicted out of set of operational groups.
 *
 * This function will taken away any of the bindings/mappings immediately so the resources
 * are not tied up to the given group, which has been evicted out of scheduling action for
 * termination.
 */
void kbase_csf_mcu_shared_clear_evicted_group_csg_reg(struct kbase_device *kbdev,
						      struct kbase_queue_group *group);

/**
 * kbase_csf_mcu_shared_add_queue - Request to add a newly activated queue for a group to be
 *                                  run on slot.
 *
 * @kbdev:     Instance of a GPU platform device that implements a CSF interface.
 * @queue:     Pointer to the queue that requires some runtime resource to be bound for joining
 *             others that are already running on-slot with their bound group.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_mcu_shared_add_queue(struct kbase_device *kbdev, struct kbase_queue *queue);

/**
 * kbase_csf_mcu_shared_drop_stopped_queue - Request to drop a queue after it has been stopped
 *                                           from its operational state from a group.
 *
 * @kbdev:     Instance of a GPU platform device that implements a CSF interface.
 * @queue:     Pointer to the queue that has been stopped from operational state.
 *
 */
void kbase_csf_mcu_shared_drop_stopped_queue(struct kbase_device *kbdev, struct kbase_queue *queue);

/**
 * kbase_csf_mcu_shared_group_bind_csg_reg - Bind some required runtime resources to the given
 *                                           group for ready to run on-slot.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @group: Pointer to the queue group that requires the runtime resources.
 *
 * This function binds/maps the required suspend buffer pages and userio pages for the given
 * group, readying it to run on-slot.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_mcu_shared_group_bind_csg_reg(struct kbase_device *kbdev,
					    struct kbase_queue_group *group);

/**
 * kbase_csf_mcu_shared_regs_data_init - Allocate and initialize the MCU shared regions data for
 *                                       the given device.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function allocate and initialize the MCU shared VA regions for runtime operations
 * of the CSF scheduler.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_csf_mcu_shared_regs_data_init(struct kbase_device *kbdev);

/**
 * kbase_csf_mcu_shared_regs_data_term - Terminate the allocated MCU shared regions data for
 *                                       the given device.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function terminates the MCU shared VA regions allocated for runtime operations
 * of the CSF scheduler.
 */
void kbase_csf_mcu_shared_regs_data_term(struct kbase_device *kbdev);

#endif /* _KBASE_CSF_MCU_SHARED_REG_H_ */
