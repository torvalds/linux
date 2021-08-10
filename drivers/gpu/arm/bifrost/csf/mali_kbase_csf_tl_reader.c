// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#include "mali_kbase_csf_tl_reader.h"

#include "mali_kbase_csf_trace_buffer.h"
#include "mali_kbase_reset_gpu.h"

#include "tl/mali_kbase_tlstream.h"
#include "tl/mali_kbase_tl_serialize.h"
#include "tl/mali_kbase_tracepoints.h"

#include "mali_kbase_pm.h"
#include "mali_kbase_hwaccess_time.h"

#include <linux/gcd.h>
#include <linux/math64.h>
#include <asm/arch_timer.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include "tl/mali_kbase_timeline_priv.h"
#include <linux/debugfs.h>

#if (KERNEL_VERSION(4, 7, 0) > LINUX_VERSION_CODE)
#define DEFINE_DEBUGFS_ATTRIBUTE DEFINE_SIMPLE_ATTRIBUTE
#endif
#endif

/* Name of the CSFFW timeline tracebuffer. */
#define KBASE_CSFFW_TRACEBUFFER_NAME "timeline"
/* Name of the timeline header metatadata */
#define KBASE_CSFFW_TIMELINE_HEADER_NAME "timeline_header"

/**
 * struct kbase_csffw_tl_message - CSFFW timeline message.
 *
 * @msg_id: Message ID.
 * @timestamp: Timestamp of the event.
 * @cycle_counter: Cycle number of the event.
 *
 * Contain fields that are common for all CSFFW timeline messages.
 */
struct kbase_csffw_tl_message {
	u32 msg_id;
	u64 timestamp;
	u64 cycle_counter;
} __packed __aligned(4);

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int kbase_csf_tl_debugfs_poll_interval_read(void *data, u64 *val)
{
	struct kbase_device *kbdev = (struct kbase_device *)data;
	struct kbase_csf_tl_reader *self = &kbdev->timeline->csf_tl_reader;

	*val = self->timer_interval;

	return 0;
}

static int kbase_csf_tl_debugfs_poll_interval_write(void *data, u64 val)
{
	struct kbase_device *kbdev = (struct kbase_device *)data;
	struct kbase_csf_tl_reader *self = &kbdev->timeline->csf_tl_reader;

	if (val > KBASE_CSF_TL_READ_INTERVAL_MAX || val < KBASE_CSF_TL_READ_INTERVAL_MIN) {
		return -EINVAL;
	}

	self->timer_interval = (u32)val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(kbase_csf_tl_poll_interval_fops,
		kbase_csf_tl_debugfs_poll_interval_read,
		kbase_csf_tl_debugfs_poll_interval_write, "%llu\n");


void kbase_csf_tl_reader_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_file("csf_tl_poll_interval_in_ms", S_IRUGO | S_IWUSR,
		kbdev->debugfs_instr_directory, kbdev,
		&kbase_csf_tl_poll_interval_fops);

}
#endif

/**
 * get_cpu_gpu_time() - Get current CPU and GPU timestamps.
 *
 * @kbdev:	Kbase device.
 * @cpu_ts:	Output CPU timestamp.
 * @gpu_ts:	Output GPU timestamp.
 * @gpu_cycle:  Output GPU cycle counts.
 */
static void get_cpu_gpu_time(
	struct kbase_device *kbdev,
	u64 *cpu_ts,
	u64 *gpu_ts,
	u64 *gpu_cycle)
{
	struct timespec64 ts;

	kbase_pm_context_active(kbdev);
	kbase_backend_get_gpu_time(kbdev, gpu_cycle, gpu_ts, &ts);
	kbase_pm_context_idle(kbdev);

	if (cpu_ts)
		*cpu_ts = ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}


/**
 * kbase_ts_converter_init() - Initialize system timestamp converter.
 *
 * @self:	System Timestamp Converter instance.
 * @kbdev:	Kbase device pointer
 *
 * Return: Zero on success, -1 otherwise.
 */
static int kbase_ts_converter_init(
	struct kbase_ts_converter *self,
	struct kbase_device *kbdev)
{
	u64 cpu_ts = 0;
	u64 gpu_ts = 0;
	u64 freq;
	u64 common_factor;

	get_cpu_gpu_time(kbdev, &cpu_ts, &gpu_ts, NULL);
	freq = arch_timer_get_cntfrq();

	if (!freq) {
		dev_warn(kbdev->dev, "arch_timer_get_rate() is zero!");
		return -1;
	}

	common_factor = gcd(NSEC_PER_SEC, freq);

	self->multiplier = div64_u64(NSEC_PER_SEC, common_factor);
	self->divisor = div64_u64(freq, common_factor);
	self->offset =
		cpu_ts - div64_u64(gpu_ts * self->multiplier, self->divisor);

	return 0;
}

/**
 * kbase_ts_converter_convert() - Convert GPU timestamp to CPU timestamp.
 *
 * @self:	System Timestamp Converter instance.
 * @gpu_ts:	System timestamp value to converter.
 *
 * Return: The CPU timestamp.
 */
static void kbase_ts_converter_convert(
	const struct kbase_ts_converter *self,
	u64 *gpu_ts)
{
	u64 old_gpu_ts = *gpu_ts;
	*gpu_ts = div64_u64(old_gpu_ts * self->multiplier,
		self->divisor) + self->offset;
}

/**
 * tl_reader_overflow_notify() - Emit stream overflow tracepoint.
 *
 * @self:		CSFFW TL Reader instance.
 * @msg_buf_start:	Start of the message.
 * @msg_buf_end:	End of the message buffer.
 */
static void tl_reader_overflow_notify(
	const struct kbase_csf_tl_reader *self,
	u8 *const msg_buf_start,
	u8 *const msg_buf_end)
{
	struct kbase_device *kbdev = self->kbdev;
	struct kbase_csffw_tl_message message = {0};

	/* Reuse the timestamp and cycle count from current event if possible */
	if (msg_buf_start + sizeof(message) <= msg_buf_end)
		memcpy(&message, msg_buf_start, sizeof(message));

	KBASE_TLSTREAM_TL_KBASE_CSFFW_TLSTREAM_OVERFLOW(
		kbdev, message.timestamp, message.cycle_counter);
}

/**
 * tl_reader_overflow_check() - Check if an overflow has happened
 *
 * @self:	CSFFW TL Reader instance.
 * @event_id:	Incoming event id.
 *
 * Return: True, if an overflow has happened, False otherwise.
 */
static bool tl_reader_overflow_check(
	struct kbase_csf_tl_reader *self,
	u16 event_id)
{
	struct kbase_device *kbdev = self->kbdev;
	bool has_overflow = false;

	/* 0 is a special event_id and reserved for the very first tracepoint
	 * after reset, we should skip overflow check when reset happened.
	 */
	if (event_id != 0) {
		has_overflow = self->got_first_event
			&& self->expected_event_id != event_id;

		if (has_overflow)
			dev_warn(kbdev->dev,
				"CSFFW overflow, event_id: %u, expected: %u.",
				event_id, self->expected_event_id);
	}

	self->got_first_event = true;
	self->expected_event_id = event_id + 1;
	/* When event_id reaches its max value, it skips 0 and wraps to 1. */
	if (self->expected_event_id == 0)
		self->expected_event_id++;

	return has_overflow;
}

/**
 * tl_reader_reset() - Reset timeline tracebuffer reader state machine.
 *
 * @self:	CSFFW TL Reader instance.
 *
 * Reset the reader to the default state, i.e. set all the
 * mutable fields to zero.
 */
static void tl_reader_reset(struct kbase_csf_tl_reader *self)
{
	self->got_first_event = false;
	self->is_active = false;
	self->expected_event_id = 0;
	self->tl_header.btc = 0;
}

int kbase_csf_tl_reader_flush_buffer(struct kbase_csf_tl_reader *self)
{
	int ret = 0;
	struct kbase_device *kbdev = self->kbdev;
	struct kbase_tlstream *stream = self->stream;

	u8  *read_buffer = self->read_buffer;
	const size_t read_buffer_size = sizeof(self->read_buffer);

	u32 bytes_read;
	u8 *csffw_data_begin;
	u8 *csffw_data_end;
	u8 *csffw_data_it;

	unsigned long flags;

	spin_lock_irqsave(&self->read_lock, flags);

	/* If not running, early exit. */
	if (!self->is_active) {
		spin_unlock_irqrestore(&self->read_lock, flags);
		return -EBUSY;
	}

	/* Copying the whole buffer in a single shot. We assume
	 * that the buffer will not contain partially written messages.
	 */
	bytes_read = kbase_csf_firmware_trace_buffer_read_data(
		self->trace_buffer, read_buffer, read_buffer_size);
	csffw_data_begin = read_buffer;
	csffw_data_end   = read_buffer + bytes_read;

	for (csffw_data_it = csffw_data_begin;
	     csffw_data_it < csffw_data_end;) {
		u32 event_header;
		u16 event_id;
		u16 event_size;
		unsigned long acq_flags;
		char *buffer;

		/* Can we safely read event_id? */
		if (csffw_data_it + sizeof(event_header) > csffw_data_end) {
			dev_warn(
				kbdev->dev,
				"Unable to parse CSFFW tracebuffer event header.");
				ret = -EBUSY;
			break;
		}

		/* Read and parse the event header. */
		memcpy(&event_header, csffw_data_it, sizeof(event_header));
		event_id   = (event_header >> 0)  & 0xFFFF;
		event_size = (event_header >> 16) & 0xFFFF;
		csffw_data_it += sizeof(event_header);

		/* Detect if an overflow has happened. */
		if (tl_reader_overflow_check(self, event_id))
			tl_reader_overflow_notify(self,
				csffw_data_it,
				csffw_data_end);

		/* Can we safely read the message body? */
		if (csffw_data_it + event_size > csffw_data_end) {
			dev_warn(kbdev->dev,
				"event_id: %u, can't read with event_size: %u.",
				event_id, event_size);
				ret = -EBUSY;
			break;
		}

		/* Convert GPU timestamp to CPU timestamp. */
		{
			struct kbase_csffw_tl_message *msg =
				(struct kbase_csffw_tl_message *) csffw_data_it;
			kbase_ts_converter_convert(
				&self->ts_converter,
				&msg->timestamp);
		}

		/* Copy the message out to the tl_stream. */
		buffer = kbase_tlstream_msgbuf_acquire(
			stream, event_size, &acq_flags);
		kbasep_serialize_bytes(buffer, 0, csffw_data_it, event_size);
		kbase_tlstream_msgbuf_release(stream, acq_flags);
		csffw_data_it += event_size;
	}

	spin_unlock_irqrestore(&self->read_lock, flags);
	return ret;
}

static void kbasep_csf_tl_reader_read_callback(struct timer_list *timer)
{
	struct kbase_csf_tl_reader *self =
		container_of(timer, struct kbase_csf_tl_reader, read_timer);

	int rcode;

	kbase_csf_tl_reader_flush_buffer(self);

	rcode = mod_timer(&self->read_timer,
		jiffies + msecs_to_jiffies(self->timer_interval));

	CSTD_UNUSED(rcode);
}

/**
 * tl_reader_init_late() - Late CSFFW TL Reader initialization.
 *
 * @self:	CSFFW TL Reader instance.
 * @kbdev:	Kbase device.
 *
 * Late initialization is done once at kbase_csf_tl_reader_start() time.
 * This is because the firmware image is not parsed
 * by the kbase_csf_tl_reader_init() time.
 *
 * Return: Zero on success, -1 otherwise.
 */
static int tl_reader_init_late(
	struct kbase_csf_tl_reader *self,
	struct kbase_device *kbdev)
{
	struct firmware_trace_buffer *tb;
	size_t hdr_size = 0;
	const char *hdr = NULL;

	if (self->kbdev)
		return 0;

	tb = kbase_csf_firmware_get_trace_buffer(
		kbdev, KBASE_CSFFW_TRACEBUFFER_NAME);
	hdr = kbase_csf_firmware_get_timeline_metadata(
		kbdev, KBASE_CSFFW_TIMELINE_HEADER_NAME, &hdr_size);

	if (!tb) {
		dev_warn(
			kbdev->dev,
			"'%s' tracebuffer is not present in the firmware image.",
			KBASE_CSFFW_TRACEBUFFER_NAME);
		return -1;
	}

	if (!hdr) {
		dev_warn(
			kbdev->dev,
			"'%s' timeline metadata is not present in the firmware image.",
			KBASE_CSFFW_TIMELINE_HEADER_NAME);
		return -1;
	}

	if (kbase_ts_converter_init(&self->ts_converter, kbdev)) {
		return -1;
	}

	self->kbdev = kbdev;
	self->trace_buffer = tb;
	self->tl_header.data = hdr;
	self->tl_header.size = hdr_size;

	return 0;
}

/**
 * tl_reader_update_enable_bit() - Update the first bit of a CSFFW tracebuffer.
 *
 * @self:	CSFFW TL Reader instance.
 * @value:	The value to set.
 *
 * Update the first bit of a CSFFW tracebufer and then reset the GPU.
 * This is to make these changes visible to the MCU.
 *
 * Return: 0 on success, or negative error code for failure.
 */
static int tl_reader_update_enable_bit(
	struct kbase_csf_tl_reader *self,
	bool value)
{
	int err = 0;

	err = kbase_csf_firmware_trace_buffer_update_trace_enable_bit(
		self->trace_buffer, 0, value);

	return err;
}

void kbase_csf_tl_reader_init(struct kbase_csf_tl_reader *self,
	struct kbase_tlstream *stream)
{
	self->timer_interval = KBASE_CSF_TL_READ_INTERVAL_DEFAULT;

	kbase_timer_setup(&self->read_timer,
		kbasep_csf_tl_reader_read_callback);

	self->stream = stream;

	/* This will be initialized by tl_reader_init_late() */
	self->kbdev = NULL;
	self->trace_buffer = NULL;
	self->tl_header.data = NULL;
	self->tl_header.size = 0;

	spin_lock_init(&self->read_lock);

	tl_reader_reset(self);
}

void kbase_csf_tl_reader_term(struct kbase_csf_tl_reader *self)
{
	del_timer_sync(&self->read_timer);
}

int kbase_csf_tl_reader_start(struct kbase_csf_tl_reader *self,
	struct kbase_device *kbdev)
{
	int rcode;

	/* If already running, early exit. */
	if (self->is_active)
		return 0;

	if (tl_reader_init_late(self, kbdev)) {
		return -EINVAL;
	}

	tl_reader_reset(self);

	self->is_active = true;
	/* Set bytes to copy to the header size. This is to trigger copying
	 * of the header to the user space.
	 */
	self->tl_header.btc = self->tl_header.size;

	/* Enable the tracebuffer on the CSFFW side. */
	rcode = tl_reader_update_enable_bit(self, true);
	if (rcode != 0)
		return rcode;

	rcode = mod_timer(&self->read_timer,
		jiffies + msecs_to_jiffies(self->timer_interval));

	return 0;
}

void kbase_csf_tl_reader_stop(struct kbase_csf_tl_reader *self)
{
	unsigned long flags;

	/* If is not running, early exit. */
	if (!self->is_active)
		return;

	/* Disable the tracebuffer on the CSFFW side. */
	tl_reader_update_enable_bit(self, false);

	del_timer_sync(&self->read_timer);

	spin_lock_irqsave(&self->read_lock, flags);

	tl_reader_reset(self);

	spin_unlock_irqrestore(&self->read_lock, flags);
}

void kbase_csf_tl_reader_reset(struct kbase_csf_tl_reader *self)
{
	u64 gpu_cycle = 0;
	struct kbase_device *kbdev = self->kbdev;

	if (!kbdev)
		return;

	kbase_csf_tl_reader_flush_buffer(self);

	get_cpu_gpu_time(kbdev, NULL, NULL, &gpu_cycle);
	KBASE_TLSTREAM_TL_KBASE_CSFFW_RESET(kbdev, gpu_cycle);
}
