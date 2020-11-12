/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSFFW_TL_READER_H_
#define _KBASE_CSFFW_TL_READER_H_

#include <linux/spinlock.h>
#include <linux/timer.h>
#include <asm/page.h>

/** The number of pages used for CSFFW trace buffer. Can be tweaked. */
#define KBASE_CSF_TL_BUFFER_NR_PAGES 4
/** CSFFW Timeline read polling minimum period in milliseconds. */
#define KBASE_CSF_TL_READ_INTERVAL_MIN 20
/** CSFFW Timeline read polling default period in milliseconds. */
#define KBASE_CSF_TL_READ_INTERVAL_DEFAULT 200
/** CSFFW Timeline read polling maximum period in milliseconds. */
#define KBASE_CSF_TL_READ_INTERVAL_MAX (60*1000)

struct firmware_trace_buffer;
struct kbase_tlstream;
struct kbase_device;

/**
 * System timestamp to CPU timestamp converter state.
 *
 * @multiplier:	Numerator of the converter's fraction.
 * @divisor:	Denominator of the converter's fraction.
 * @offset:	Converter's offset term.
 *
 * According to Generic timer spec, system timer:
 * - Increments at a fixed frequency
 * - Starts operating from zero
 *
 * Hence CPU time is a linear function of System Time.
 *
 * CPU_ts = alpha * SYS_ts + beta
 *
 * Where
 * - alpha = 10^9/SYS_ts_freq
 * - beta is calculated by two timer samples taken at the same time:
 *   beta = CPU_ts_s - SYS_ts_s * alpha
 *
 * Since alpha is a rational number, we minimizing possible
 * rounding error by simplifying the ratio. Thus alpha is stored
 * as a simple `multiplier / divisor` ratio.
 *
 */
struct kbase_ts_converter {
	u64 multiplier;
	u64 divisor;
	s64 offset;
};

/**
 * struct kbase_csf_tl_reader - CSFFW timeline reader state.
 *
 * @read_timer:        Timer used for periodical tracebufer reading.
 * @timer_interval:    Timer polling period in milliseconds.
 * @stream:            Timeline stream where to the tracebuffer content
 *                     is copied.
 * @kbdev:             KBase device.
 * @trace_buffer:      CSF Firmware timeline tracebuffer.
 * @tl_header.data:    CSFFW Timeline header content.
 * @tl_header.size:    CSFFW Timeline header size.
 * @tl_header.btc:     CSFFW Timeline header remaining bytes to copy to
 *                     the user space.
 * @ts_converter:      Timestamp converter state.
 * @got_first_event:   True, if a CSFFW timelime session has been enabled
 *                     and the first event was received.
 * @is_active:         True, if a CSFFW timelime session has been enabled.
 * @expected_event_id: The last 16 bit event ID received from CSFFW. It
 *                     is only valid when got_first_event is true.
 * @read_buffer:       Temporary buffer used for CSFFW timeline data
 *                     reading from the tracebufer.
 */
struct kbase_csf_tl_reader {
	struct timer_list read_timer;
	u32 timer_interval;
	struct kbase_tlstream *stream;

	struct kbase_device *kbdev;
	struct firmware_trace_buffer *trace_buffer;
	struct {
		const char *data;
		size_t size;
		size_t btc;
	} tl_header;
	struct kbase_ts_converter ts_converter;

	bool got_first_event;
	bool is_active;
	u16 expected_event_id;

	u8 read_buffer[PAGE_SIZE * KBASE_CSF_TL_BUFFER_NR_PAGES];
	spinlock_t read_lock;
};

/**
 * kbase_csf_tl_reader_init() - Initialize CSFFW Timelime Stream Reader.
 *
 * @self:	CSFFW TL Reader instance.
 * @stream:	Destination timeline stream.
 */
void kbase_csf_tl_reader_init(struct kbase_csf_tl_reader *self,
	struct kbase_tlstream *stream);

/**
 * kbase_csf_tl_reader_term() - Terminate CSFFW Timelime Stream Reader.
 *
 * @self:	CSFFW TL Reader instance.
 */
void kbase_csf_tl_reader_term(struct kbase_csf_tl_reader *self);

/**
 *  kbase_csf_tl_reader_flush_buffer() -
 *   Flush trace from buffer into CSFFW timeline stream.
 *
 * @self:    CSFFW TL Reader instance.
 */

void kbase_csf_tl_reader_flush_buffer(struct kbase_csf_tl_reader *self);

/**
 * kbase_csf_tl_reader_start() -
 *	Start asynchronous copying of CSFFW timeline stream.
 *
 * @self:	CSFFW TL Reader instance.
 * @kbdev:	Kbase device.
 *
 * Return: zero on success, a negative error code otherwise.
 */
int kbase_csf_tl_reader_start(struct kbase_csf_tl_reader *self,
	struct kbase_device *kbdev);

/**
 * kbase_csf_tl_reader_stop() -
 *	Stop asynchronous copying of CSFFW timeline stream.
 *
 * @self:	CSFFW TL Reader instance.
 */
void kbase_csf_tl_reader_stop(struct kbase_csf_tl_reader *self);

#ifdef CONFIG_DEBUG_FS
/**
 * kbase_csf_tl_reader_debugfs_init() -
 *	Initialize debugfs for CSFFW Timelime Stream Reader.
 *
 * @kbdev:	Kbase device.
 */
void kbase_csf_tl_reader_debugfs_init(struct kbase_device *kbdev);
#endif

/**
 * kbase_csf_tl_reader_reset() -
 *	Reset CSFFW timeline reader, it should be called before reset CSFFW.
 *
 * @self:	CSFFW TL Reader instance.
 */
void kbase_csf_tl_reader_reset(struct kbase_csf_tl_reader *self);

#endif /* _KBASE_CSFFW_TL_READER_H_ */
