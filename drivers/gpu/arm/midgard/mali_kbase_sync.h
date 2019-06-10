/*
 *
 * (C) COPYRIGHT 2012-2017 ARM Limited. All rights reserved.
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



/**
 * @file mali_kbase_sync.h
 *
 * This file contains our internal "API" for explicit fences.
 * It hides the implementation details of the actual explicit fence mechanism
 * used (Android fences or sync file with DMA fences).
 */

#ifndef MALI_KBASE_SYNC_H
#define MALI_KBASE_SYNC_H

#include <linux/syscalls.h>
#ifdef CONFIG_SYNC
#include <sync.h>
#endif
#ifdef CONFIG_SYNC_FILE
#include "mali_kbase_fence_defs.h"
#include <linux/sync_file.h>
#endif

#include "mali_kbase.h"

/**
 * struct kbase_sync_fence_info - Information about a fence
 * @fence: Pointer to fence (type is void*, as underlaying struct can differ)
 * @name: The name given to this fence when it was created
 * @status: < 0 means error, 0 means active, 1 means signaled
 *
 * Use kbase_sync_fence_in_info_get() or kbase_sync_fence_out_info_get()
 * to get the information.
 */
struct kbase_sync_fence_info {
	void *fence;
	char name[32];
	int status;
};

/**
 * kbase_sync_fence_stream_create() - Create a stream object
 * @name: Name of stream (only used to ease debugging/visualization)
 * @out_fd: A file descriptor representing the created stream object
 *
 * Can map down to a timeline implementation in some implementations.
 * Exposed as a file descriptor.
 * Life-time controlled via the file descriptor:
 * - dup to add a ref
 * - close to remove a ref
 *
 * return: 0 on success, < 0 on error
 */
int kbase_sync_fence_stream_create(const char *name, int *const out_fd);

/**
 * kbase_sync_fence_out_create Create an explicit output fence to specified atom
 * @katom: Atom to assign the new explicit fence to
 * @stream_fd: File descriptor for stream object to create fence on
 *
 * return: Valid file descriptor to fence or < 0 on error
 */
int kbase_sync_fence_out_create(struct kbase_jd_atom *katom, int stream_fd);

/**
 * kbase_sync_fence_in_from_fd() Assigns an existing fence to specified atom
 * @katom: Atom to assign the existing explicit fence to
 * @fd: File descriptor to an existing fence
 *
 * Assigns an explicit input fence to atom.
 * This can later be waited for by calling @kbase_sync_fence_in_wait
 *
 * return: 0 on success, < 0 on error
 */
int kbase_sync_fence_in_from_fd(struct kbase_jd_atom *katom, int fd);

/**
 * kbase_sync_fence_validate() - Validate a fd to be a valid fence
 * @fd: File descriptor to check
 *
 * This function is only usable to catch unintentional user errors early,
 * it does not stop malicious code changing the fd after this function returns.
 *
 * return 0: if fd is for a valid fence, < 0 if invalid
 */
int kbase_sync_fence_validate(int fd);

/**
 * kbase_sync_fence_out_trigger - Signal explicit output fence attached on katom
 * @katom: Atom with an explicit fence to signal
 * @result: < 0 means signal with error, 0 >= indicates success
 *
 * Signal output fence attached on katom and remove the fence from the atom.
 *
 * return: The "next" event code for atom, typically JOB_CANCELLED or EVENT_DONE
 */
enum base_jd_event_code
kbase_sync_fence_out_trigger(struct kbase_jd_atom *katom, int result);

/**
 * kbase_sync_fence_in_wait() - Wait for explicit input fence to be signaled
 * @katom: Atom with explicit fence to wait for
 *
 * If the fence is already signaled, then 0 is returned, and the caller must
 * continue processing of the katom.
 *
 * If the fence isn't already signaled, then this kbase_sync framework will
 * take responsibility to continue the processing once the fence is signaled.
 *
 * return: 0 if already signaled, otherwise 1
 */
int kbase_sync_fence_in_wait(struct kbase_jd_atom *katom);

/**
 * kbase_sync_fence_in_cancel_wait() - Cancel explicit input fence waits
 * @katom: Atom to cancel wait for
 *
 * This function is fully responsible for continuing processing of this atom
 * (remove_waiting_soft_job + finish_soft_job + jd_done + js_sched_all)
 */
void kbase_sync_fence_in_cancel_wait(struct kbase_jd_atom *katom);

/**
 * kbase_sync_fence_in_remove() - Remove the input fence from the katom
 * @katom: Atom to remove explicit input fence for
 *
 * This will also release the corresponding reference.
 */
void kbase_sync_fence_in_remove(struct kbase_jd_atom *katom);

/**
 * kbase_sync_fence_out_remove() - Remove the output fence from the katom
 * @katom: Atom to remove explicit output fence for
 *
 * This will also release the corresponding reference.
 */
void kbase_sync_fence_out_remove(struct kbase_jd_atom *katom);

/**
 * kbase_sync_fence_close_fd() - Close a file descriptor representing a fence
 * @fd: File descriptor to close
 */
static inline void kbase_sync_fence_close_fd(int fd)
{
	ksys_close(fd);
}

/**
 * kbase_sync_fence_in_info_get() - Retrieves information about input fence
 * @katom: Atom to get fence information from
 * @info: Struct to be filled with fence information
 *
 * return: 0 on success, < 0 on error
 */
int kbase_sync_fence_in_info_get(struct kbase_jd_atom *katom,
				 struct kbase_sync_fence_info *info);

/**
 * kbase_sync_fence_out_info_get() - Retrieves information about output fence
 * @katom: Atom to get fence information from
 * @info: Struct to be filled with fence information
 *
 * return: 0 on success, < 0 on error
 */
int kbase_sync_fence_out_info_get(struct kbase_jd_atom *katom,
				  struct kbase_sync_fence_info *info);

/**
 * kbase_sync_status_string() - Get string matching @status
 * @status: Value of fence status.
 *
 * return: Pointer to string describing @status.
 */
const char *kbase_sync_status_string(int status);

/*
 * Internal worker used to continue processing of atom.
 */
void kbase_sync_fence_wait_worker(struct work_struct *data);

#ifdef CONFIG_MALI_FENCE_DEBUG
/**
 * kbase_sync_fence_in_dump() Trigger a debug dump of atoms input fence state
 * @katom: Atom to trigger fence debug dump for
 */
void kbase_sync_fence_in_dump(struct kbase_jd_atom *katom);
#endif

#endif /* MALI_KBASE_SYNC_H */
