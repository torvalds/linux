/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_DEBUG_KTRACE_DEFS_JM_H_
#define _KBASE_DEBUG_KTRACE_DEFS_JM_H_

/**
 * DOC: KTrace version history, JM variant
 * 1.0:
 * - Original version (implicit, header did not carry version information)
 * 2.0:
 * - Introduced version information into the header
 * - some changes of parameter names in header
 * - trace now uses all 64-bits of info_val
 * - Non-JM specific parts moved to using info_val instead of refcount/gpu_addr
 */
#define KBASE_KTRACE_VERSION_MAJOR 2
#define KBASE_KTRACE_VERSION_MINOR 0

/* indicates if the trace message has a valid refcount member */
#define KBASE_KTRACE_FLAG_JM_REFCOUNT (((kbase_ktrace_flag_t)1) << 0)
/* indicates if the trace message has a valid jobslot member */
#define KBASE_KTRACE_FLAG_JM_JOBSLOT  (((kbase_ktrace_flag_t)1) << 1)
/* indicates if the trace message has valid atom related info. */
#define KBASE_KTRACE_FLAG_JM_ATOM     (((kbase_ktrace_flag_t)1) << 2)


/**
 * struct kbase_ktrace_backend - backend specific part of a trace message
 *
 * @atom_udata:  Copy of the user data sent for the atom in base_jd_submit.
 *               Only valid if KBASE_KTRACE_FLAG_JM_ATOM is set in @flags
 * @gpu_addr:    GPU address, usually of the job-chain represented by an atom.
 * @atom_number: id of the atom for which trace message was added. Only valid
 *               if KBASE_KTRACE_FLAG_JM_ATOM is set in @flags
 * @code:        Identifies the event, refer to enum kbase_ktrace_code.
 * @flags:       indicates information about the trace message itself. Used
 *               during dumping of the message.
 * @jobslot:     job-slot for which trace message was added, valid only for
 *               job-slot management events.
 * @refcount:    reference count for the context, valid for certain events
 *               related to scheduler core and policy.
 */
struct kbase_ktrace_backend {
	/* Place 64 and 32-bit members together */
	u64 atom_udata[2]; /* Only valid for KBASE_KTRACE_FLAG_JM_ATOM */
	u64 gpu_addr;
	int atom_number; /* Only valid for KBASE_KTRACE_FLAG_JM_ATOM */
	/* Pack smaller members together */
	kbase_ktrace_code_t code;
	kbase_ktrace_flag_t flags;
	u8 jobslot;
	u8 refcount;
};

#endif /* _KBASE_DEBUG_KTRACE_DEFS_JM_H_ */
