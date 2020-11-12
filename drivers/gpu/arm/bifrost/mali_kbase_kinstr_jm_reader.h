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

/*
 * mali_kbase_kinstr_jm_reader.h
 * Provides an ioctl API to read kernel atom state changes. The flow of the
 * API is:
 *    1. Obtain the file descriptor with ``KBASE_IOCTL_KINSTR_JM_FD``
 *    2. Determine the buffer structure layout via the above ioctl's returned
 *       size and version fields in ``struct kbase_kinstr_jm_fd_out``
 *    4. Poll the file descriptor for ``POLLIN``
 *    5. Get data with read() on the fd
 *    6. Use the structure version to understand how to read the data from the
 *       buffer
 *    7. Repeat 4-6
 *    8. Close the file descriptor
 */

#ifndef _KBASE_KINSTR_JM_READER_H_
#define _KBASE_KINSTR_JM_READER_H_

/**
 * enum kbase_kinstr_jm_reader_atom_state - Determines the work state of an atom
 * @KBASE_KINSTR_JM_READER_ATOM_STATE_QUEUE:    Signifies that an atom has
 *                                              entered a hardware queue
 * @KBASE_KINSTR_JM_READER_ATOM_STATE_START:    Signifies that work has started
 *                                              on an atom
 * @KBASE_KINSTR_JM_READER_ATOM_STATE_STOP:     Signifies that work has stopped
 *                                              on an atom
 * @KBASE_KINSTR_JM_READER_ATOM_STATE_COMPLETE: Signifies that work has
 *                                              completed on an atom
 * @KBASE_KINSTR_JM_READER_ATOM_STATE_COUNT:    The number of state enumerations
 *
 * We can add new states to the end of this if they do not break the existing
 * state machine. Old user mode code can gracefully ignore states they do not
 * understand.
 *
 * If we need to make a breaking change to the state machine, we can do that by
 * changing the version reported by KBASE_IOCTL_KINSTR_JM_FD. This will
 * mean that old user mode code will fail to understand the new state field in
 * the structure and gracefully not use the state change API.
 */
enum kbase_kinstr_jm_reader_atom_state {
	KBASE_KINSTR_JM_READER_ATOM_STATE_QUEUE,
	KBASE_KINSTR_JM_READER_ATOM_STATE_START,
	KBASE_KINSTR_JM_READER_ATOM_STATE_STOP,
	KBASE_KINSTR_JM_READER_ATOM_STATE_COMPLETE,
	KBASE_KINSTR_JM_READER_ATOM_STATE_COUNT
};

#endif /* _KBASE_KINSTR_JM_READER_H_ */
