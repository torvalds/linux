/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _KBASE_DEBUG_KTRACE_DEFS_JM_H_
#define _KBASE_DEBUG_KTRACE_DEFS_JM_H_

#if KBASE_KTRACE_TARGET_RBUF
/**
 * DOC: KTrace version history, JM variant
 *
 * 1.0:
 * Original version (implicit, header did not carry version information).
 *
 * 2.0:
 * Introduced version information into the header.
 *
 * Some changes of parameter names in header.
 *
 * Trace now uses all 64-bits of info_val.
 *
 * Non-JM specific parts moved to using info_val instead of refcount/gpu_addr.
 *
 * 2.1:
 * kctx field is no longer a pointer, and is now an ID of the format %d_%u as
 * used by kctx directories in mali debugfs entries: (tgid creating the kctx),
 * (unique kctx id).
 *
 * ftrace backend now outputs kctx field (as %d_%u format).
 *
 */
#define KBASE_KTRACE_VERSION_MAJOR 2
#define KBASE_KTRACE_VERSION_MINOR 1
#endif /* KBASE_KTRACE_TARGET_RBUF */

/*
 * Note: mali_kbase_debug_ktrace_jm.h needs these value even if the RBUF target
 * is disabled (they get discarded with CSTD_UNUSED(), but they're still
 * referenced)
 */

/* indicates if the trace message has a valid refcount member */
#define KBASE_KTRACE_FLAG_JM_REFCOUNT (((kbase_ktrace_flag_t)1) << 0)
/* indicates if the trace message has a valid jobslot member */
#define KBASE_KTRACE_FLAG_JM_JOBSLOT  (((kbase_ktrace_flag_t)1) << 1)
/* indicates if the trace message has valid atom related info. */
#define KBASE_KTRACE_FLAG_JM_ATOM     (((kbase_ktrace_flag_t)1) << 2)

#if KBASE_KTRACE_TARGET_RBUF
/* Collect all the flags together for debug checking */
#define KBASE_KTRACE_FLAG_BACKEND_ALL \
		(KBASE_KTRACE_FLAG_JM_REFCOUNT | KBASE_KTRACE_FLAG_JM_JOBSLOT \
		| KBASE_KTRACE_FLAG_JM_ATOM)

/**
 * union kbase_ktrace_backend - backend specific part of a trace message
 * Contains only a struct but is a union such that it is compatible with
 * generic JM and CSF KTrace calls.
 *
 * @gpu:             gpu union member
 * @gpu.atom_udata:  Copy of the user data sent for the atom in base_jd_submit.
 *                   Only valid if KBASE_KTRACE_FLAG_JM_ATOM is set in @flags
 * @gpu.gpu_addr:    GPU address, usually of the job-chain represented by an
 *                   atom.
 * @gpu.atom_number: id of the atom for which trace message was added. Only
 *                   valid if KBASE_KTRACE_FLAG_JM_ATOM is set in @flags
 * @gpu.code:        Identifies the event, refer to enum kbase_ktrace_code.
 * @gpu.flags:       indicates information about the trace message itself. Used
 *                   during dumping of the message.
 * @gpu.jobslot:     job-slot for which trace message was added, valid only for
 *                   job-slot management events.
 * @gpu.refcount:    reference count for the context, valid for certain events
 *                   related to scheduler core and policy.
 */
union kbase_ktrace_backend {
	struct {
		/* Place 64 and 32-bit members together */
		u64 atom_udata[2]; /* Only valid for
				    * KBASE_KTRACE_FLAG_JM_ATOM
				    */
		u64 gpu_addr;
		int atom_number; /* Only valid for KBASE_KTRACE_FLAG_JM_ATOM */
		/* Pack smaller members together */
		kbase_ktrace_code_t code;
		kbase_ktrace_flag_t flags;
		u8 jobslot;
		u8 refcount;
	} gpu;
};
#endif /* KBASE_KTRACE_TARGET_RBUF */

#endif /* _KBASE_DEBUG_KTRACE_DEFS_JM_H_ */
