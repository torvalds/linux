/*
 *
 * (C) COPYRIGHT 2012-2015 ARM Limited. All rights reserved.
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



#ifndef _KBASE_DEBUG_JOB_FAULT_H
#define _KBASE_DEBUG_JOB_FAULT_H

#include <mali_kbase.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define REGISTER_DUMP_TERMINATION_FLAG 0xFFFFFFFF

/**
 * kbase_debug_job_fault_dev_init - Create the fault event wait queue
 *		per device and initialize the required lists.
 * @kbdev:	Device pointer
 *
 * Return: Zero on success or a negative error code.
 */
int kbase_debug_job_fault_dev_init(struct kbase_device *kbdev);

/**
 * kbase_debug_job_fault_debugfs_init - Initialize job fault debug sysfs
 * @kbdev:	Device pointer
 */
void kbase_debug_job_fault_debugfs_init(struct kbase_device *kbdev);

/**
 * kbase_debug_job_fault_dev_term - Clean up resources created in
 *		kbase_debug_job_fault_dev_init.
 * @kbdev:	Device pointer
 */
void kbase_debug_job_fault_dev_term(struct kbase_device *kbdev);

/**
 * kbase_debug_job_fault_context_init - Initialize the relevant
 *		data structure per context
 * @kctx: KBase context pointer
 */
void kbase_debug_job_fault_context_init(struct kbase_context *kctx);

/**
 * kbase_debug_job_fault_context_term - Release the relevant
 *		resource per context
 * @kctx: KBase context pointer
 */
void kbase_debug_job_fault_context_term(struct kbase_context *kctx);

/**
 * kbase_debug_job_fault_process - Process the failed job.
 *      It will send a event and wake up the job fault waiting queue
 *      Then create a work queue to wait for job dump finish
 *      This function should be called in the interrupt handler and before
 *      jd_done that make sure the jd_done_worker will be delayed until the
 *      job dump finish
 * @katom: The failed atom pointer
 * @completion_code: the job status
 * @return true if dump is going on
 */
bool kbase_debug_job_fault_process(struct kbase_jd_atom *katom,
		u32 completion_code);


/**
 * kbase_debug_job_fault_reg_snapshot_init - Set the interested registers
 *      address during the job fault process, the relevant registers will
 *      be saved when a job fault happen
 * @kctx: KBase context pointer
 * @reg_range: Maximum register address space
 * @return true if initializing successfully
 */
bool kbase_debug_job_fault_reg_snapshot_init(struct kbase_context *kctx,
		int reg_range);

/**
 * kbase_job_fault_get_reg_snapshot - Read the interested registers for
 *      failed job dump
 * @kctx: KBase context pointer
 * @return true if getting registers successfully
 */
bool kbase_job_fault_get_reg_snapshot(struct kbase_context *kctx);

#endif  /*_KBASE_DEBUG_JOB_FAULT_H*/
