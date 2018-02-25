/*
 *
 * (C) COPYRIGHT 2015 ARM Limited. All rights reserved.
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

#ifndef _KBASE_HWCNT_READER_H_
#define _KBASE_HWCNT_READER_H_

/* The ids of ioctl commands. */
#define KBASE_HWCNT_READER 0xBE
#define KBASE_HWCNT_READER_GET_HWVER       _IOR(KBASE_HWCNT_READER, 0x00, u32)
#define KBASE_HWCNT_READER_GET_BUFFER_SIZE _IOR(KBASE_HWCNT_READER, 0x01, u32)
#define KBASE_HWCNT_READER_DUMP            _IOW(KBASE_HWCNT_READER, 0x10, u32)
#define KBASE_HWCNT_READER_CLEAR           _IOW(KBASE_HWCNT_READER, 0x11, u32)
#define KBASE_HWCNT_READER_GET_BUFFER      _IOR(KBASE_HWCNT_READER, 0x20,\
		struct kbase_hwcnt_reader_metadata)
#define KBASE_HWCNT_READER_PUT_BUFFER      _IOW(KBASE_HWCNT_READER, 0x21,\
		struct kbase_hwcnt_reader_metadata)
#define KBASE_HWCNT_READER_SET_INTERVAL    _IOW(KBASE_HWCNT_READER, 0x30, u32)
#define KBASE_HWCNT_READER_ENABLE_EVENT    _IOW(KBASE_HWCNT_READER, 0x40, u32)
#define KBASE_HWCNT_READER_DISABLE_EVENT   _IOW(KBASE_HWCNT_READER, 0x41, u32)
#define KBASE_HWCNT_READER_GET_API_VERSION _IOW(KBASE_HWCNT_READER, 0xFF, u32)

/**
 * struct kbase_hwcnt_reader_metadata - hwcnt reader sample buffer metadata
 * @timestamp:  time when sample was collected
 * @event_id:   id of an event that triggered sample collection
 * @buffer_idx: position in sampling area where sample buffer was stored
 */
struct kbase_hwcnt_reader_metadata {
	u64 timestamp;
	u32 event_id;
	u32 buffer_idx;
};

/**
 * enum base_hwcnt_reader_event - hwcnt dumping events
 * @BASE_HWCNT_READER_EVENT_MANUAL:   manual request for dump
 * @BASE_HWCNT_READER_EVENT_PERIODIC: periodic dump
 * @BASE_HWCNT_READER_EVENT_PREJOB:   prejob dump request
 * @BASE_HWCNT_READER_EVENT_POSTJOB:  postjob dump request
 * @BASE_HWCNT_READER_EVENT_COUNT:    number of supported events
 */
enum base_hwcnt_reader_event {
	BASE_HWCNT_READER_EVENT_MANUAL,
	BASE_HWCNT_READER_EVENT_PERIODIC,
	BASE_HWCNT_READER_EVENT_PREJOB,
	BASE_HWCNT_READER_EVENT_POSTJOB,

	BASE_HWCNT_READER_EVENT_COUNT
};

#endif /* _KBASE_HWCNT_READER_H_ */

