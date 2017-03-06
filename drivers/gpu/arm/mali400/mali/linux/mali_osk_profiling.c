/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/sched.h>

#include <mali_profiling_gator_api.h>
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"
#include "mali_uk_types.h"
#include "mali_osk_profiling.h"
#include "mali_linux_trace.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_l2_cache.h"
#include "mali_user_settings_db.h"
#include "mali_executor.h"
#include "mali_memory_manager.h"

#define MALI_PROFILING_STREAM_DATA_DEFAULT_SIZE 100
#define MALI_PROFILING_STREAM_HOLD_TIME 1000000         /*1 ms */

#define MALI_PROFILING_STREAM_BUFFER_SIZE       (1 << 12)
#define MALI_PROFILING_STREAM_BUFFER_NUM        100

/**
 * Define the mali profiling stream struct.
 */
typedef struct mali_profiling_stream {
	u8 data[MALI_PROFILING_STREAM_BUFFER_SIZE];
	u32 used_size;
	struct list_head list;
} mali_profiling_stream;

typedef struct mali_profiling_stream_list {
	spinlock_t spin_lock;
	struct list_head free_list;
	struct list_head queue_list;
} mali_profiling_stream_list;

static const char mali_name[] = "4xx";
static const char utgard_setup_version[] = "ANNOTATE_SETUP 1\n";

static u32 profiling_sample_rate = 0;
static u32 first_sw_counter_index = 0;

static mali_bool l2_cache_counter_if_enabled = MALI_FALSE;
static u32 num_counters_enabled = 0;
static u32 mem_counters_enabled = 0;

static _mali_osk_atomic_t stream_fd_if_used;

static wait_queue_head_t stream_fd_wait_queue;
static mali_profiling_counter *global_mali_profiling_counters = NULL;
static u32 num_global_mali_profiling_counters = 0;

static mali_profiling_stream_list *global_mali_stream_list = NULL;
static mali_profiling_stream *mali_counter_stream = NULL;
static mali_profiling_stream *mali_core_activity_stream = NULL;
static u64 mali_core_activity_stream_dequeue_time = 0;
static spinlock_t mali_activity_lock;
static u32 mali_activity_cores_num =  0;
static struct hrtimer profiling_sampling_timer;

const char *_mali_mem_counter_descriptions[] = _MALI_MEM_COUTNER_DESCRIPTIONS;
const char *_mali_special_counter_descriptions[] = _MALI_SPCIAL_COUNTER_DESCRIPTIONS;

static u32 current_profiling_pid = 0;

static void _mali_profiling_stream_list_destory(mali_profiling_stream_list *profiling_stream_list)
{
	mali_profiling_stream *profiling_stream, *tmp_profiling_stream;
	MALI_DEBUG_ASSERT_POINTER(profiling_stream_list);

	list_for_each_entry_safe(profiling_stream, tmp_profiling_stream, &profiling_stream_list->free_list, list) {
		list_del(&profiling_stream->list);
		kfree(profiling_stream);
	}

	list_for_each_entry_safe(profiling_stream, tmp_profiling_stream, &profiling_stream_list->queue_list, list) {
		list_del(&profiling_stream->list);
		kfree(profiling_stream);
	}

	kfree(profiling_stream_list);
}

static void _mali_profiling_global_stream_list_free(void)
{
	mali_profiling_stream *profiling_stream, *tmp_profiling_stream;
	unsigned long irq_flags;

	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);
	spin_lock_irqsave(&global_mali_stream_list->spin_lock, irq_flags);
	list_for_each_entry_safe(profiling_stream, tmp_profiling_stream, &global_mali_stream_list->queue_list, list) {
		profiling_stream->used_size = 0;
		list_move(&profiling_stream->list, &global_mali_stream_list->free_list);
	}
	spin_unlock_irqrestore(&global_mali_stream_list->spin_lock, irq_flags);
}

static _mali_osk_errcode_t _mali_profiling_global_stream_list_dequeue(struct list_head *stream_list, mali_profiling_stream **new_mali_profiling_stream)
{
	unsigned long irq_flags;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_OK;
	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);
	MALI_DEBUG_ASSERT_POINTER(stream_list);

	spin_lock_irqsave(&global_mali_stream_list->spin_lock, irq_flags);

	if (!list_empty(stream_list)) {
		*new_mali_profiling_stream = list_entry(stream_list->next, mali_profiling_stream, list);
		list_del_init(&(*new_mali_profiling_stream)->list);
	} else {
		ret = _MALI_OSK_ERR_NOMEM;
	}

	spin_unlock_irqrestore(&global_mali_stream_list->spin_lock, irq_flags);

	return ret;
}

static void _mali_profiling_global_stream_list_queue(struct list_head *stream_list, mali_profiling_stream *current_mali_profiling_stream)
{
	unsigned long irq_flags;
	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);
	MALI_DEBUG_ASSERT_POINTER(stream_list);

	spin_lock_irqsave(&global_mali_stream_list->spin_lock, irq_flags);
	list_add_tail(&current_mali_profiling_stream->list, stream_list);
	spin_unlock_irqrestore(&global_mali_stream_list->spin_lock, irq_flags);
}

static mali_bool _mali_profiling_global_stream_queue_list_if_empty(void)
{
	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);
	return list_empty(&global_mali_stream_list->queue_list);
}

static u32 _mali_profiling_global_stream_queue_list_next_size(void)
{
	unsigned long irq_flags;
	u32 size = 0;
	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);

	spin_lock_irqsave(&global_mali_stream_list->spin_lock, irq_flags);
	if (!list_empty(&global_mali_stream_list->queue_list)) {
		mali_profiling_stream *next_mali_profiling_stream =
			list_entry(global_mali_stream_list->queue_list.next, mali_profiling_stream, list);
		size = next_mali_profiling_stream->used_size;
	}
	spin_unlock_irqrestore(&global_mali_stream_list->spin_lock, irq_flags);
	return size;
}

/* The mali profiling stream file operations functions. */
static ssize_t _mali_profiling_stream_read(
	struct file *filp,
	char __user *buffer,
	size_t      size,
	loff_t      *f_pos);

static unsigned int  _mali_profiling_stream_poll(struct file *filp, poll_table *wait);

static int  _mali_profiling_stream_release(struct inode *inode, struct file *filp);

/* The timeline stream file operations structure. */
static const struct file_operations mali_profiling_stream_fops = {
	.release = _mali_profiling_stream_release,
	.read    = _mali_profiling_stream_read,
	.poll    = _mali_profiling_stream_poll,
};

static ssize_t _mali_profiling_stream_read(
	struct file *filp,
	char __user *buffer,
	size_t      size,
	loff_t      *f_pos)
{
	u32 copy_len = 0;
	mali_profiling_stream *current_mali_profiling_stream;
	u32 used_size;
	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);

	while (!_mali_profiling_global_stream_queue_list_if_empty()) {
		used_size = _mali_profiling_global_stream_queue_list_next_size();
		if (used_size <= ((u32)size - copy_len)) {
			current_mali_profiling_stream = NULL;
			_mali_profiling_global_stream_list_dequeue(&global_mali_stream_list->queue_list,
					&current_mali_profiling_stream);
			MALI_DEBUG_ASSERT_POINTER(current_mali_profiling_stream);
			if (copy_to_user(&buffer[copy_len], current_mali_profiling_stream->data, current_mali_profiling_stream->used_size)) {
				current_mali_profiling_stream->used_size = 0;
				_mali_profiling_global_stream_list_queue(&global_mali_stream_list->free_list, current_mali_profiling_stream);
				return -EFAULT;
			}
			copy_len += current_mali_profiling_stream->used_size;
			current_mali_profiling_stream->used_size = 0;
			_mali_profiling_global_stream_list_queue(&global_mali_stream_list->free_list, current_mali_profiling_stream);
		} else {
			break;
		}
	}
	return (ssize_t)copy_len;
}

static unsigned int  _mali_profiling_stream_poll(struct file *filp, poll_table *wait)
{
	poll_wait(filp, &stream_fd_wait_queue, wait);
	if (!_mali_profiling_global_stream_queue_list_if_empty())
		return POLLIN;
	return 0;
}

static int  _mali_profiling_stream_release(struct inode *inode, struct file *filp)
{
	_mali_osk_atomic_init(&stream_fd_if_used, 0);
	return 0;
}

/* The funs for control packet and stream data.*/
static void _mali_profiling_set_packet_size(unsigned char *const buf, const u32 size)
{
	u32 i;

	for (i = 0; i < sizeof(size); ++i)
		buf[i] = (size >> 8 * i) & 0xFF;
}

static u32 _mali_profiling_get_packet_size(unsigned char *const buf)
{
	u32 i;
	u32 size = 0;
	for (i = 0; i < sizeof(size); ++i)
		size |= (u32)buf[i] << 8 * i;
	return size;
}

static u32 _mali_profiling_read_packet_int(unsigned char *const buf, u32 *const pos, u32 const packet_size)
{
	u64 int_value = 0;
	u8 shift = 0;
	u8 byte_value = ~0;

	while ((byte_value & 0x80) != 0) {
		if ((*pos) >= packet_size)
			return -1;
		byte_value = buf[*pos];
		*pos += 1;
		int_value |= (u32)(byte_value & 0x7f) << shift;
		shift += 7;
	}

	if (shift < 8 * sizeof(int_value) && (byte_value & 0x40) != 0) {
		int_value |= -(1 << shift);
	}

	return int_value;
}

static u32 _mali_profiling_pack_int(u8 *const buf, u32 const buf_size, u32 const pos, s32 value)
{
	u32 add_bytes = 0;
	int more = 1;
	while (more) {
		/* low order 7 bits of val */
		char byte_value = value & 0x7f;
		value >>= 7;

		if ((value == 0 && (byte_value & 0x40) == 0) || (value == -1 && (byte_value & 0x40) != 0)) {
			more = 0;
		} else {
			byte_value |= 0x80;
		}

		if ((pos + add_bytes) >= buf_size)
			return 0;
		buf[pos + add_bytes] = byte_value;
		add_bytes++;
	}

	return add_bytes;
}

static int _mali_profiling_pack_long(uint8_t *const buf, u32 const buf_size, u32 const pos, s64 val)
{
	int add_bytes = 0;
	int more = 1;
	while (more) {
		/* low order 7 bits of x */
		char byte_value = val & 0x7f;
		val >>= 7;

		if ((val == 0 && (byte_value & 0x40) == 0) || (val == -1 && (byte_value & 0x40) != 0)) {
			more = 0;
		} else {
			byte_value |= 0x80;
		}

		MALI_DEBUG_ASSERT((pos + add_bytes) < buf_size);
		buf[pos + add_bytes] = byte_value;
		add_bytes++;
	}

	return add_bytes;
}

static void _mali_profiling_stream_add_counter(mali_profiling_stream *profiling_stream, s64 current_time, u32 key, u32 counter_value)
{
	u32 add_size = STREAM_HEADER_SIZE;
	MALI_DEBUG_ASSERT_POINTER(profiling_stream);
	MALI_DEBUG_ASSERT((profiling_stream->used_size) < MALI_PROFILING_STREAM_BUFFER_SIZE);

	profiling_stream->data[profiling_stream->used_size] = STREAM_HEADER_COUNTER_VALUE;

	add_size += _mali_profiling_pack_long(profiling_stream->data, MALI_PROFILING_STREAM_BUFFER_SIZE,
					      profiling_stream->used_size + add_size, current_time);
	add_size += _mali_profiling_pack_int(profiling_stream->data, MALI_PROFILING_STREAM_BUFFER_SIZE,
					     profiling_stream->used_size + add_size, (s32)0);
	add_size += _mali_profiling_pack_int(profiling_stream->data, MALI_PROFILING_STREAM_BUFFER_SIZE,
					     profiling_stream->used_size + add_size, (s32)key);
	add_size += _mali_profiling_pack_int(profiling_stream->data, MALI_PROFILING_STREAM_BUFFER_SIZE,
					     profiling_stream->used_size + add_size, (s32)counter_value);

	_mali_profiling_set_packet_size(profiling_stream->data + profiling_stream->used_size + 1,
					add_size - STREAM_HEADER_SIZE);

	profiling_stream->used_size += add_size;
}

/* The callback function for sampling timer.*/
static enum hrtimer_restart  _mali_profiling_sampling_counters(struct hrtimer *timer)
{
	u32 counter_index;
	s64 current_time;
	MALI_DEBUG_ASSERT_POINTER(global_mali_profiling_counters);
	MALI_DEBUG_ASSERT_POINTER(global_mali_stream_list);

	MALI_DEBUG_ASSERT(NULL == mali_counter_stream);
	if (_MALI_OSK_ERR_OK == _mali_profiling_global_stream_list_dequeue(
		    &global_mali_stream_list->free_list, &mali_counter_stream)) {

		MALI_DEBUG_ASSERT_POINTER(mali_counter_stream);
		MALI_DEBUG_ASSERT(0 == mali_counter_stream->used_size);

		/* Capture l2 cache counter values if enabled */
		if (MALI_TRUE == l2_cache_counter_if_enabled) {
			int i, j = 0;
			_mali_profiling_l2_counter_values l2_counters_values;
			_mali_profiling_get_l2_counters(&l2_counters_values);

			for (i  = COUNTER_L2_0_C0; i <= COUNTER_L2_2_C1; i++) {
				if (0 == (j % 2))
					_mali_osk_profiling_record_global_counters(i, l2_counters_values.cores[j / 2].value0);
				else
					_mali_osk_profiling_record_global_counters(i, l2_counters_values.cores[j / 2].value1);
				j++;
			}
		}

		current_time = (s64)_mali_osk_boot_time_get_ns();

		/* Add all enabled counter values into stream */
		for (counter_index = 0; counter_index < num_global_mali_profiling_counters; counter_index++) {
			/* No need to sample these couners here. */
			if (global_mali_profiling_counters[counter_index].enabled) {
				if ((global_mali_profiling_counters[counter_index].counter_id >= FIRST_MEM_COUNTER &&
				     global_mali_profiling_counters[counter_index].counter_id <= LAST_MEM_COUNTER)
				    || (global_mali_profiling_counters[counter_index].counter_id == COUNTER_VP_ACTIVITY)
				    || (global_mali_profiling_counters[counter_index].counter_id == COUNTER_FP_ACTIVITY)
				    || (global_mali_profiling_counters[counter_index].counter_id == COUNTER_FILMSTRIP)) {

					continue;
				}

				if (global_mali_profiling_counters[counter_index].counter_id >= COUNTER_L2_0_C0 &&
				    global_mali_profiling_counters[counter_index].counter_id <= COUNTER_L2_2_C1) {

					u32 prev_val = global_mali_profiling_counters[counter_index].prev_counter_value;

					_mali_profiling_stream_add_counter(mali_counter_stream, current_time, global_mali_profiling_counters[counter_index].key,
									   global_mali_profiling_counters[counter_index].current_counter_value - prev_val);

					prev_val = global_mali_profiling_counters[counter_index].current_counter_value;

					global_mali_profiling_counters[counter_index].prev_counter_value = prev_val;
				} else {

					if (global_mali_profiling_counters[counter_index].counter_id == COUNTER_TOTAL_ALLOC_PAGES) {
						u32 total_alloc_mem = _mali_ukk_report_memory_usage();
						global_mali_profiling_counters[counter_index].current_counter_value = total_alloc_mem / _MALI_OSK_MALI_PAGE_SIZE;
					}
					_mali_profiling_stream_add_counter(mali_counter_stream, current_time, global_mali_profiling_counters[counter_index].key,
									   global_mali_profiling_counters[counter_index].current_counter_value);
					if (global_mali_profiling_counters[counter_index].counter_id < FIRST_SPECIAL_COUNTER)
						global_mali_profiling_counters[counter_index].current_counter_value = 0;
				}
			}
		}
		_mali_profiling_global_stream_list_queue(&global_mali_stream_list->queue_list, mali_counter_stream);
		mali_counter_stream = NULL;
	} else {
		MALI_DEBUG_PRINT(1, ("Not enough mali profiling stream buffer!\n"));
	}

	wake_up_interruptible(&stream_fd_wait_queue);

	/*Enable the sampling timer again*/
	if (0 != num_counters_enabled && 0 != profiling_sample_rate) {
		hrtimer_forward_now(&profiling_sampling_timer, ns_to_ktime(profiling_sample_rate));
		return HRTIMER_RESTART;
	}
	return HRTIMER_NORESTART;
}

static void _mali_profiling_sampling_core_activity_switch(int counter_id, int core, u32 activity, u32 pid)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&mali_activity_lock, irq_flags);
	if (activity == 0)
		mali_activity_cores_num--;
	else
		mali_activity_cores_num++;
	spin_unlock_irqrestore(&mali_activity_lock, irq_flags);

	if (NULL != global_mali_profiling_counters) {
		int i ;
		for (i = 0; i < num_global_mali_profiling_counters; i++) {
			if (counter_id == global_mali_profiling_counters[i].counter_id && global_mali_profiling_counters[i].enabled) {
				u64 current_time = _mali_osk_boot_time_get_ns();
				u32 add_size = STREAM_HEADER_SIZE;

				if (NULL != mali_core_activity_stream) {
					if ((mali_core_activity_stream_dequeue_time +  MALI_PROFILING_STREAM_HOLD_TIME < current_time) ||
					    (MALI_PROFILING_STREAM_DATA_DEFAULT_SIZE > MALI_PROFILING_STREAM_BUFFER_SIZE
					     - mali_core_activity_stream->used_size)) {
						_mali_profiling_global_stream_list_queue(&global_mali_stream_list->queue_list, mali_core_activity_stream);
						mali_core_activity_stream = NULL;
						wake_up_interruptible(&stream_fd_wait_queue);
					}
				}

				if (NULL == mali_core_activity_stream) {
					if (_MALI_OSK_ERR_OK == _mali_profiling_global_stream_list_dequeue(
						    &global_mali_stream_list->free_list, &mali_core_activity_stream)) {
						mali_core_activity_stream_dequeue_time = current_time;
					} else {
						MALI_DEBUG_PRINT(1, ("Not enough mali profiling stream buffer!\n"));
						wake_up_interruptible(&stream_fd_wait_queue);
						break;
					}

				}

				mali_core_activity_stream->data[mali_core_activity_stream->used_size] = STREAM_HEADER_CORE_ACTIVITY;

				add_size += _mali_profiling_pack_long(mali_core_activity_stream->data,
								      MALI_PROFILING_STREAM_BUFFER_SIZE, mali_core_activity_stream->used_size + add_size, (s64)current_time);
				add_size += _mali_profiling_pack_int(mali_core_activity_stream->data,
								     MALI_PROFILING_STREAM_BUFFER_SIZE, mali_core_activity_stream->used_size + add_size, core);
				add_size += _mali_profiling_pack_int(mali_core_activity_stream->data,
								     MALI_PROFILING_STREAM_BUFFER_SIZE, mali_core_activity_stream->used_size + add_size, (s32)global_mali_profiling_counters[i].key);
				add_size += _mali_profiling_pack_int(mali_core_activity_stream->data,
								     MALI_PROFILING_STREAM_BUFFER_SIZE, mali_core_activity_stream->used_size + add_size, activity);
				add_size += _mali_profiling_pack_int(mali_core_activity_stream->data,
								     MALI_PROFILING_STREAM_BUFFER_SIZE, mali_core_activity_stream->used_size + add_size, pid);

				_mali_profiling_set_packet_size(mali_core_activity_stream->data + mali_core_activity_stream->used_size + 1,
								add_size - STREAM_HEADER_SIZE);

				mali_core_activity_stream->used_size += add_size;

				if (0 == mali_activity_cores_num) {
					_mali_profiling_global_stream_list_queue(&global_mali_stream_list->queue_list, mali_core_activity_stream);
					mali_core_activity_stream = NULL;
					wake_up_interruptible(&stream_fd_wait_queue);
				}

				break;
			}
		}
	}
}

static mali_bool _mali_profiling_global_counters_init(void)
{
	int core_id, counter_index, counter_number, counter_id;
	u32 num_l2_cache_cores;
	u32 num_pp_cores;
	u32 num_gp_cores = 1;

	MALI_DEBUG_ASSERT(NULL == global_mali_profiling_counters);
	num_pp_cores = mali_pp_get_glob_num_pp_cores();
	num_l2_cache_cores =    mali_l2_cache_core_get_glob_num_l2_cores();

	num_global_mali_profiling_counters = 3 * (num_gp_cores + num_pp_cores) + 2 * num_l2_cache_cores
					     + MALI_PROFILING_SW_COUNTERS_NUM
					     + MALI_PROFILING_SPECIAL_COUNTERS_NUM
					     + MALI_PROFILING_MEM_COUNTERS_NUM;
	global_mali_profiling_counters = _mali_osk_calloc(num_global_mali_profiling_counters, sizeof(mali_profiling_counter));

	if (NULL == global_mali_profiling_counters)
		return MALI_FALSE;

	counter_index = 0;
	/*Vertex processor counters */
	for (core_id = 0; core_id < num_gp_cores; core_id ++) {
		global_mali_profiling_counters[counter_index].counter_id = ACTIVITY_VP_0 + core_id;
		_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
				   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_VP_%d_active", mali_name, core_id);

		for (counter_number = 0; counter_number < 2; counter_number++) {
			counter_index++;
			global_mali_profiling_counters[counter_index].counter_id = COUNTER_VP_0_C0 + (2 * core_id) + counter_number;
			_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
					   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_VP_%d_cnt%d", mali_name, core_id, counter_number);
		}
	}

	/* Fragment processors' counters */
	for (core_id = 0; core_id < num_pp_cores; core_id++) {
		counter_index++;
		global_mali_profiling_counters[counter_index].counter_id = ACTIVITY_FP_0 + core_id;
		_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
				   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_FP_%d_active", mali_name, core_id);

		for (counter_number = 0; counter_number < 2; counter_number++) {
			counter_index++;
			global_mali_profiling_counters[counter_index].counter_id = COUNTER_FP_0_C0 + (2 * core_id) + counter_number;
			_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
					   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_FP_%d_cnt%d", mali_name, core_id, counter_number);
		}
	}

	/* L2 Cache counters */
	for (core_id = 0; core_id < num_l2_cache_cores; core_id++) {
		for (counter_number = 0; counter_number < 2; counter_number++) {
			counter_index++;
			global_mali_profiling_counters[counter_index].counter_id = COUNTER_L2_0_C0 + (2 * core_id) + counter_number;
			_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
					   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_L2_%d_cnt%d", mali_name, core_id, counter_number);
		}
	}

	/* Now set up the software counter entries */
	for (counter_id = FIRST_SW_COUNTER; counter_id <= LAST_SW_COUNTER; counter_id++) {
		counter_index++;

		if (0 == first_sw_counter_index)
			first_sw_counter_index = counter_index;

		global_mali_profiling_counters[counter_index].counter_id = counter_id;
		_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
				   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_SW_%d", mali_name, counter_id - FIRST_SW_COUNTER);
	}

	/* Now set up the special counter entries */
	for (counter_id = FIRST_SPECIAL_COUNTER; counter_id <= LAST_SPECIAL_COUNTER; counter_id++) {

		counter_index++;
		_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
				   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_%s",
				   mali_name, _mali_special_counter_descriptions[counter_id - FIRST_SPECIAL_COUNTER]);

		global_mali_profiling_counters[counter_index].counter_id = counter_id;
	}

	/* Now set up the mem counter entries*/
	for (counter_id = FIRST_MEM_COUNTER; counter_id <= LAST_MEM_COUNTER; counter_id++) {

		counter_index++;
		_mali_osk_snprintf(global_mali_profiling_counters[counter_index].counter_name,
				   sizeof(global_mali_profiling_counters[counter_index].counter_name), "ARM_Mali-%s_%s",
				   mali_name, _mali_mem_counter_descriptions[counter_id - FIRST_MEM_COUNTER]);

		global_mali_profiling_counters[counter_index].counter_id = counter_id;
	}

	MALI_DEBUG_ASSERT((counter_index + 1) == num_global_mali_profiling_counters);

	return MALI_TRUE;
}

void _mali_profiling_notification_mem_counter(struct mali_session_data *session, u32 counter_id, u32 key, int enable)
{

	MALI_DEBUG_ASSERT_POINTER(session);

	if (NULL != session) {
		_mali_osk_notification_t *notification;
		_mali_osk_notification_queue_t *queue;

		queue = session->ioctl_queue;
		MALI_DEBUG_ASSERT(NULL != queue);

		notification = _mali_osk_notification_create(_MALI_NOTIFICATION_ANNOTATE_PROFILING_MEM_COUNTER,
				sizeof(_mali_uk_annotate_profiling_mem_counter_s));

		if (NULL != notification) {
			_mali_uk_annotate_profiling_mem_counter_s *data = notification->result_buffer;
			data->counter_id = counter_id;
			data->key = key;
			data->enable = enable;

			_mali_osk_notification_queue_send(queue, notification);
		} else {
			MALI_PRINT_ERROR(("Failed to create notification object!\n"));
		}
	} else {
		MALI_PRINT_ERROR(("Failed to find the right session!\n"));
	}
}

void _mali_profiling_notification_enable(struct mali_session_data *session, u32 sampling_rate, int enable)
{
	MALI_DEBUG_ASSERT_POINTER(session);

	if (NULL != session) {
		_mali_osk_notification_t *notification;
		_mali_osk_notification_queue_t *queue;

		queue = session->ioctl_queue;
		MALI_DEBUG_ASSERT(NULL != queue);

		notification = _mali_osk_notification_create(_MALI_NOTIFICATION_ANNOTATE_PROFILING_ENABLE,
				sizeof(_mali_uk_annotate_profiling_enable_s));

		if (NULL != notification) {
			_mali_uk_annotate_profiling_enable_s *data = notification->result_buffer;
			data->sampling_rate = sampling_rate;
			data->enable = enable;

			_mali_osk_notification_queue_send(queue, notification);
		} else {
			MALI_PRINT_ERROR(("Failed to create notification object!\n"));
		}
	} else {
		MALI_PRINT_ERROR(("Failed to find the right session!\n"));
	}
}


_mali_osk_errcode_t _mali_osk_profiling_init(mali_bool auto_start)
{
	int i;
	mali_profiling_stream *new_mali_profiling_stream = NULL;
	mali_profiling_stream_list *new_mali_profiling_stream_list = NULL;
	if (MALI_TRUE == auto_start) {
		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE, MALI_TRUE);
	}

	/*Init the global_mali_stream_list*/
	MALI_DEBUG_ASSERT(NULL == global_mali_stream_list);
	new_mali_profiling_stream_list = (mali_profiling_stream_list *)kmalloc(sizeof(mali_profiling_stream_list), GFP_KERNEL);

	if (NULL == new_mali_profiling_stream_list) {
		return _MALI_OSK_ERR_NOMEM;
	}

	spin_lock_init(&new_mali_profiling_stream_list->spin_lock);
	INIT_LIST_HEAD(&new_mali_profiling_stream_list->free_list);
	INIT_LIST_HEAD(&new_mali_profiling_stream_list->queue_list);

	spin_lock_init(&mali_activity_lock);
	mali_activity_cores_num =  0;

	for (i = 0; i < MALI_PROFILING_STREAM_BUFFER_NUM; i++) {
		new_mali_profiling_stream = (mali_profiling_stream *)kmalloc(sizeof(mali_profiling_stream), GFP_KERNEL);
		if (NULL == new_mali_profiling_stream) {
			_mali_profiling_stream_list_destory(new_mali_profiling_stream_list);
			return _MALI_OSK_ERR_NOMEM;
		}

		INIT_LIST_HEAD(&new_mali_profiling_stream->list);
		new_mali_profiling_stream->used_size = 0;
		list_add_tail(&new_mali_profiling_stream->list, &new_mali_profiling_stream_list->free_list);

	}

	_mali_osk_atomic_init(&stream_fd_if_used, 0);
	init_waitqueue_head(&stream_fd_wait_queue);

	hrtimer_init(&profiling_sampling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	profiling_sampling_timer.function = _mali_profiling_sampling_counters;

	global_mali_stream_list = new_mali_profiling_stream_list;

	return _MALI_OSK_ERR_OK;
}

void _mali_osk_profiling_term(void)
{
	if (0 != profiling_sample_rate) {
		hrtimer_cancel(&profiling_sampling_timer);
		profiling_sample_rate = 0;
	}
	_mali_osk_atomic_term(&stream_fd_if_used);

	if (NULL != global_mali_profiling_counters) {
		_mali_osk_free(global_mali_profiling_counters);
		global_mali_profiling_counters = NULL;
		num_global_mali_profiling_counters = 0;
	}

	if (NULL != global_mali_stream_list) {
		_mali_profiling_stream_list_destory(global_mali_stream_list);
		global_mali_stream_list = NULL;
	}

}

void _mali_osk_profiling_stop_sampling(u32 pid)
{
	if (pid == current_profiling_pid) {

		int i;
		/* Reset all counter states when closing connection.*/
		for (i = 0; i < num_global_mali_profiling_counters; ++i) {
			_mali_profiling_set_event(global_mali_profiling_counters[i].counter_id, MALI_HW_CORE_NO_COUNTER);
			global_mali_profiling_counters[i].enabled = 0;
			global_mali_profiling_counters[i].prev_counter_value = 0;
			global_mali_profiling_counters[i].current_counter_value = 0;
		}
		l2_cache_counter_if_enabled = MALI_FALSE;
		num_counters_enabled = 0;
		mem_counters_enabled = 0;
		_mali_profiling_control(FBDUMP_CONTROL_ENABLE, 0);
		_mali_profiling_control(SW_COUNTER_ENABLE, 0);
		/* Delete sampling timer when closing connection. */
		if (0 != profiling_sample_rate) {
			hrtimer_cancel(&profiling_sampling_timer);
			profiling_sample_rate = 0;
		}
		current_profiling_pid = 0;
	}
}

void    _mali_osk_profiling_add_event(u32 event_id, u32 data0, u32 data1, u32 data2, u32 data3, u32 data4)
{
	/*Record the freq & volt to global_mali_profiling_counters here. */
	if (0 != profiling_sample_rate) {
		u32 channel;
		u32 state;
		channel = (event_id >> 16) & 0xFF;
		state = ((event_id >> 24) & 0xF) << 24;

		switch (state) {
		case MALI_PROFILING_EVENT_TYPE_SINGLE:
			if ((MALI_PROFILING_EVENT_CHANNEL_GPU >> 16) == channel) {
				u32 reason = (event_id & 0xFFFF);
				if (MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE == reason) {
					_mali_osk_profiling_record_global_counters(COUNTER_FREQUENCY, data0);
					_mali_osk_profiling_record_global_counters(COUNTER_VOLTAGE, data1);
				}
			}
			break;
		case MALI_PROFILING_EVENT_TYPE_START:
			if ((MALI_PROFILING_EVENT_CHANNEL_GP0 >> 16) == channel) {
				_mali_profiling_sampling_core_activity_switch(COUNTER_VP_ACTIVITY, 0, 1, data1);
			} else if (channel >= (MALI_PROFILING_EVENT_CHANNEL_PP0 >> 16) &&
				   (MALI_PROFILING_EVENT_CHANNEL_PP7 >> 16) >= channel) {
				u32 core_id = channel - (MALI_PROFILING_EVENT_CHANNEL_PP0 >> 16);
				_mali_profiling_sampling_core_activity_switch(COUNTER_FP_ACTIVITY, core_id, 1, data1);
			}
			break;
		case MALI_PROFILING_EVENT_TYPE_STOP:
			if ((MALI_PROFILING_EVENT_CHANNEL_GP0 >> 16) == channel) {
				_mali_profiling_sampling_core_activity_switch(COUNTER_VP_ACTIVITY, 0, 0, 0);
			} else if (channel >= (MALI_PROFILING_EVENT_CHANNEL_PP0 >> 16) &&
				   (MALI_PROFILING_EVENT_CHANNEL_PP7 >> 16) >= channel) {
				u32 core_id = channel - (MALI_PROFILING_EVENT_CHANNEL_PP0 >> 16);
				_mali_profiling_sampling_core_activity_switch(COUNTER_FP_ACTIVITY, core_id, 0, 0);
			}
			break;
		default:
			break;
		}
	}
	trace_mali_timeline_event(event_id, data0, data1, data2, data3, data4);
}

void _mali_osk_profiling_report_sw_counters(u32 *counters)
{
	trace_mali_sw_counters(_mali_osk_get_pid(), _mali_osk_get_tid(), NULL, counters);
}

void _mali_osk_profiling_record_global_counters(int counter_id, u32 value)
{
	if (NULL != global_mali_profiling_counters) {
		int i ;
		for (i = 0; i < num_global_mali_profiling_counters; i++) {
			if (counter_id == global_mali_profiling_counters[i].counter_id && global_mali_profiling_counters[i].enabled) {
				global_mali_profiling_counters[i].current_counter_value = value;
				break;
			}
		}
	}
}

_mali_osk_errcode_t _mali_ukk_profiling_add_event(_mali_uk_profiling_add_event_s *args)
{
	/* Always add process and thread identificator in the first two data elements for events from user space */
	_mali_osk_profiling_add_event(args->event_id, _mali_osk_get_pid(), _mali_osk_get_tid(), args->data[2], args->data[3], args->data[4]);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_sw_counters_report(_mali_uk_sw_counters_report_s *args)
{
	u32 *counters = (u32 *)(uintptr_t)args->counters;

	_mali_osk_profiling_report_sw_counters(counters);

	if (NULL != global_mali_profiling_counters) {
		int i;
		for (i = 0; i < MALI_PROFILING_SW_COUNTERS_NUM; i ++) {
			if (global_mali_profiling_counters[first_sw_counter_index + i].enabled) {
				global_mali_profiling_counters[first_sw_counter_index + i].current_counter_value = *(counters + i);
			}
		}
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_profiling_stream_fd_get(_mali_uk_profiling_stream_fd_get_s *args)
{
	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	MALI_DEBUG_ASSERT_POINTER(session);

	if (1 == _mali_osk_atomic_inc_return(&stream_fd_if_used)) {

		s32 fd = anon_inode_getfd("[mali_profiling_stream]", &mali_profiling_stream_fops,
					  session,
					  O_RDONLY | O_CLOEXEC);

		args->stream_fd = fd;
		if (0 > fd) {
			_mali_osk_atomic_dec(&stream_fd_if_used);
			return _MALI_OSK_ERR_FAULT;
		}
		args->stream_fd = fd;
	} else {
		_mali_osk_atomic_dec(&stream_fd_if_used);
		args->stream_fd = -1;
		return _MALI_OSK_ERR_BUSY;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_profiling_control_set(_mali_uk_profiling_control_set_s *args)
{
	u32 control_packet_size;
	u32 output_buffer_size;

	struct  mali_session_data *session = (struct mali_session_data *)(uintptr_t)args->ctx;
	MALI_DEBUG_ASSERT_POINTER(session);

	if (NULL == global_mali_profiling_counters && MALI_FALSE == _mali_profiling_global_counters_init()) {
		MALI_PRINT_ERROR(("Failed to create global_mali_profiling_counters.\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	control_packet_size = args->control_packet_size;
	output_buffer_size = args->response_packet_size;

	if (0 != control_packet_size) {
		u8 control_type;
		u8 *control_packet_data;
		u8 *response_packet_data;
		u32 version_length = sizeof(utgard_setup_version) - 1;

		control_packet_data = (u8 *)(uintptr_t)args->control_packet_data;
		MALI_DEBUG_ASSERT_POINTER(control_packet_data);
		response_packet_data = (u8 *)(uintptr_t)args->response_packet_data;
		MALI_DEBUG_ASSERT_POINTER(response_packet_data);

		/*Decide if need to ignore Utgard setup version.*/
		if (control_packet_size >= version_length) {
			if (0 == memcmp(control_packet_data, utgard_setup_version, version_length)) {
				if (control_packet_size == version_length) {
					args->response_packet_size = 0;
					return _MALI_OSK_ERR_OK;
				} else {
					control_packet_data += version_length;
					control_packet_size -= version_length;
				}
			}
		}

		current_profiling_pid = _mali_osk_get_pid();

		control_type = control_packet_data[0];
		switch (control_type) {
		case PACKET_HEADER_COUNTERS_REQUEST: {
			int i;

			if (PACKET_HEADER_SIZE > control_packet_size ||
			    control_packet_size !=  _mali_profiling_get_packet_size(control_packet_data + 1)) {
				MALI_PRINT_ERROR(("Wrong control packet  size, type 0x%x,size 0x%x.\n", control_packet_data[0], control_packet_size));
				return _MALI_OSK_ERR_FAULT;
			}

			/* Send supported counters */
			if (PACKET_HEADER_SIZE > output_buffer_size)
				return _MALI_OSK_ERR_FAULT;

			*response_packet_data = PACKET_HEADER_COUNTERS_ACK;
			args->response_packet_size = PACKET_HEADER_SIZE;

			for (i = 0; i < num_global_mali_profiling_counters; ++i) {
				u32 name_size = strlen(global_mali_profiling_counters[i].counter_name);

				if ((args->response_packet_size + name_size + 1) > output_buffer_size) {
					MALI_PRINT_ERROR(("Response packet data is too large..\n"));
					return _MALI_OSK_ERR_FAULT;
				}

				memcpy(response_packet_data + args->response_packet_size,
				       global_mali_profiling_counters[i].counter_name, name_size + 1);

				args->response_packet_size += (name_size + 1);

				if (global_mali_profiling_counters[i].counter_id == COUNTER_VP_ACTIVITY) {
					args->response_packet_size += _mali_profiling_pack_int(response_packet_data,
								      output_buffer_size, args->response_packet_size, (s32)1);
				} else if (global_mali_profiling_counters[i].counter_id == COUNTER_FP_ACTIVITY) {
					args->response_packet_size += _mali_profiling_pack_int(response_packet_data,
								      output_buffer_size, args->response_packet_size, (s32)mali_pp_get_glob_num_pp_cores());
				} else {
					args->response_packet_size += _mali_profiling_pack_int(response_packet_data,
								      output_buffer_size, args->response_packet_size, (s32) - 1);
				}
			}

			_mali_profiling_set_packet_size(response_packet_data + 1, args->response_packet_size);
			break;
		}

		case PACKET_HEADER_COUNTERS_ENABLE: {
			int i;
			u32 request_pos = PACKET_HEADER_SIZE;
			mali_bool sw_counter_if_enabled = MALI_FALSE;

			if (PACKET_HEADER_SIZE > control_packet_size ||
			    control_packet_size !=  _mali_profiling_get_packet_size(control_packet_data + 1)) {
				MALI_PRINT_ERROR(("Wrong control packet  size , type 0x%x,size 0x%x.\n", control_packet_data[0], control_packet_size));
				return _MALI_OSK_ERR_FAULT;
			}

			/* Init all counter states before enable requested counters.*/
			for (i = 0; i < num_global_mali_profiling_counters; ++i) {
				_mali_profiling_set_event(global_mali_profiling_counters[i].counter_id, MALI_HW_CORE_NO_COUNTER);
				global_mali_profiling_counters[i].enabled = 0;
				global_mali_profiling_counters[i].prev_counter_value = 0;
				global_mali_profiling_counters[i].current_counter_value = 0;

				if (global_mali_profiling_counters[i].counter_id >= FIRST_MEM_COUNTER &&
				    global_mali_profiling_counters[i].counter_id <= LAST_MEM_COUNTER) {
					_mali_profiling_notification_mem_counter(session, global_mali_profiling_counters[i].counter_id, 0, 0);
				}
			}

			l2_cache_counter_if_enabled = MALI_FALSE;
			num_counters_enabled = 0;
			mem_counters_enabled = 0;
			_mali_profiling_control(FBDUMP_CONTROL_ENABLE, 0);
			_mali_profiling_control(SW_COUNTER_ENABLE, 0);
			_mali_profiling_notification_enable(session, 0, 0);

			/* Enable requested counters */
			while (request_pos < control_packet_size) {
				u32 begin = request_pos;
				u32 event;
				u32 key;

				/* Check the counter name which should be ended with null */
				while (request_pos < control_packet_size && control_packet_data[request_pos] != '\0') {
					++request_pos;
				}

				if (request_pos >= control_packet_size)
					return _MALI_OSK_ERR_FAULT;

				++request_pos;
				event = _mali_profiling_read_packet_int(control_packet_data, &request_pos, control_packet_size);
				key = _mali_profiling_read_packet_int(control_packet_data, &request_pos, control_packet_size);

				for (i = 0; i < num_global_mali_profiling_counters; ++i) {
					u32 name_size = strlen((char *)(control_packet_data + begin));

					if (strncmp(global_mali_profiling_counters[i].counter_name, (char *)(control_packet_data + begin), name_size) == 0) {
						if (!sw_counter_if_enabled && (FIRST_SW_COUNTER <= global_mali_profiling_counters[i].counter_id
									       && global_mali_profiling_counters[i].counter_id <= LAST_SW_COUNTER)) {
							sw_counter_if_enabled = MALI_TRUE;
							_mali_profiling_control(SW_COUNTER_ENABLE, 1);
						}

						if (COUNTER_FILMSTRIP == global_mali_profiling_counters[i].counter_id) {
							_mali_profiling_control(FBDUMP_CONTROL_ENABLE, 1);
							_mali_profiling_control(FBDUMP_CONTROL_RATE, event & 0xff);
							_mali_profiling_control(FBDUMP_CONTROL_RESIZE_FACTOR, (event >> 8) & 0xff);
						}

						if (global_mali_profiling_counters[i].counter_id >= FIRST_MEM_COUNTER &&
						    global_mali_profiling_counters[i].counter_id <= LAST_MEM_COUNTER) {
							_mali_profiling_notification_mem_counter(session, global_mali_profiling_counters[i].counter_id,
									key, 1);
							mem_counters_enabled++;
						}

						global_mali_profiling_counters[i].counter_event = event;
						global_mali_profiling_counters[i].key = key;
						global_mali_profiling_counters[i].enabled = 1;

						_mali_profiling_set_event(global_mali_profiling_counters[i].counter_id,
									  global_mali_profiling_counters[i].counter_event);
						num_counters_enabled++;
						break;
					}
				}

				if (i == num_global_mali_profiling_counters) {
					MALI_PRINT_ERROR(("Counter name does not match for type %u.\n", control_type));
					return _MALI_OSK_ERR_FAULT;
				}
			}

			if (PACKET_HEADER_SIZE <= output_buffer_size) {
				*response_packet_data = PACKET_HEADER_ACK;
				_mali_profiling_set_packet_size(response_packet_data + 1, PACKET_HEADER_SIZE);
				args->response_packet_size = PACKET_HEADER_SIZE;
			} else {
				return _MALI_OSK_ERR_FAULT;
			}

			break;
		}

		case PACKET_HEADER_START_CAPTURE_VALUE: {
			u32 live_rate;
			u32 request_pos = PACKET_HEADER_SIZE;

			if (PACKET_HEADER_SIZE > control_packet_size ||
			    control_packet_size !=  _mali_profiling_get_packet_size(control_packet_data + 1)) {
				MALI_PRINT_ERROR(("Wrong control packet  size , type 0x%x,size 0x%x.\n", control_packet_data[0], control_packet_size));
				return _MALI_OSK_ERR_FAULT;
			}

			/* Read samping rate in nanoseconds and live rate, start capture.*/
			profiling_sample_rate =  _mali_profiling_read_packet_int(control_packet_data,
						 &request_pos, control_packet_size);

			live_rate = _mali_profiling_read_packet_int(control_packet_data, &request_pos, control_packet_size);

			if (PACKET_HEADER_SIZE <= output_buffer_size) {
				*response_packet_data = PACKET_HEADER_ACK;
				_mali_profiling_set_packet_size(response_packet_data + 1, PACKET_HEADER_SIZE);
				args->response_packet_size = PACKET_HEADER_SIZE;
			} else {
				return _MALI_OSK_ERR_FAULT;
			}

			if (0 != num_counters_enabled && 0 != profiling_sample_rate) {
				_mali_profiling_global_stream_list_free();
				if (mem_counters_enabled > 0) {
					_mali_profiling_notification_enable(session, profiling_sample_rate, 1);
				}
				hrtimer_start(&profiling_sampling_timer,
					      ktime_set(profiling_sample_rate / 1000000000, profiling_sample_rate % 1000000000),
					      HRTIMER_MODE_REL_PINNED);
			}

			break;
		}
		default:
			MALI_PRINT_ERROR(("Unsupported  profiling packet header type %u.\n", control_type));
			args->response_packet_size  = 0;
			return _MALI_OSK_ERR_FAULT;
		}
	} else {
		_mali_osk_profiling_stop_sampling(current_profiling_pid);
		_mali_profiling_notification_enable(session, 0, 0);
	}

	return _MALI_OSK_ERR_OK;
}

/**
 * Called by gator.ko to set HW counters
 *
 * @param counter_id The counter ID.
 * @param event_id Event ID that the counter should count (HW counter value from TRM).
 *
 * @return 1 on success, 0 on failure.
 */
int _mali_profiling_set_event(u32 counter_id, s32 event_id)
{
	if (COUNTER_VP_0_C0 == counter_id) {
		mali_gp_job_set_gp_counter_src0(event_id);
	} else if (COUNTER_VP_0_C1 == counter_id) {
		mali_gp_job_set_gp_counter_src1(event_id);
	} else if (COUNTER_FP_0_C0 <= counter_id && COUNTER_FP_7_C1 >= counter_id) {
		/*
		 * Two compatibility notes for this function:
		 *
		 * 1) Previously the DDK allowed per core counters.
		 *
		 *    This did not make much sense on Mali-450 with the "virtual PP core" concept,
		 *    so this option was removed, and only the same pair of HW counters was allowed on all cores,
		 *    beginning with r3p2 release.
		 *
		 *    Starting with r4p0, it is now possible to set different HW counters for the different sub jobs.
		 *    This should be almost the same, since sub job 0 is designed to run on core 0,
		 *    sub job 1 on core 1, and so on.
		 *
		 *    The scheduling of PP sub jobs is not predictable, and this often led to situations where core 0 ran 2
		 *    sub jobs, while for instance core 1 ran zero. Having the counters set per sub job would thus increase
		 *    the predictability of the returned data (as you would be guaranteed data for all the selected HW counters).
		 *
		 *    PS: Core scaling needs to be disabled in order to use this reliably (goes for both solutions).
		 *
		 *    The framework/#defines with Gator still indicates that the counter is for a particular core,
		 *    but this is internally used as a sub job ID instead (no translation needed).
		 *
		 *  2) Global/default vs per sub job counters
		 *
		 *     Releases before r3p2 had only per PP core counters.
		 *     r3p2 releases had only one set of default/global counters which applied to all PP cores
		 *     Starting with r4p0, we have both a set of default/global counters,
		 *     and individual counters per sub job (equal to per core).
		 *
		 *     To keep compatibility with Gator/DS-5/streamline, the following scheme is used:
		 *
		 *     r3p2 release; only counters set for core 0 is handled,
		 *     this is applied as the default/global set of counters, and will thus affect all cores.
		 *
		 *     r4p0 release; counters set for core 0 is applied as both the global/default set of counters,
		 *     and counters for sub job 0.
		 *     Counters set for core 1-7 is only applied for the corresponding sub job.
		 *
		 *     This should allow the DS-5/Streamline GUI to have a simple mode where it only allows setting the
		 *     values for core 0, and thus this will be applied to all PP sub jobs/cores.
		 *     Advanced mode will also be supported, where individual pairs of HW counters can be selected.
		 *
		 *     The GUI will (until it is updated) still refer to cores instead of sub jobs, but this is probably
		 *     something we can live with!
		 *
		 *     Mali-450 note: Each job is not divided into a deterministic number of sub jobs, as the HW DLBU
		 *     automatically distributes the load between whatever number of cores is available at this particular time.
		 *     A normal PP job on Mali-450 is thus considered a single (virtual) job, and it will thus only be possible
		 *     to use a single pair of HW counters (even if the job ran on multiple PP cores).
		 *     In other words, only the global/default pair of PP HW counters will be used for normal Mali-450 jobs.
		 */
		u32 sub_job = (counter_id - COUNTER_FP_0_C0) >> 1;
		u32 counter_src = (counter_id - COUNTER_FP_0_C0) & 1;
		if (0 == counter_src) {
			mali_pp_job_set_pp_counter_sub_job_src0(sub_job, event_id);
			if (0 == sub_job) {
				mali_pp_job_set_pp_counter_global_src0(event_id);
			}
		} else {
			mali_pp_job_set_pp_counter_sub_job_src1(sub_job, event_id);
			if (0 == sub_job) {
				mali_pp_job_set_pp_counter_global_src1(event_id);
			}
		}
	} else if (COUNTER_L2_0_C0 <= counter_id && COUNTER_L2_2_C1 >= counter_id) {
		u32 core_id = (counter_id - COUNTER_L2_0_C0) >> 1;
		struct mali_l2_cache_core *l2_cache_core = mali_l2_cache_core_get_glob_l2_core(core_id);

		if (NULL != l2_cache_core) {
			u32 counter_src = (counter_id - COUNTER_L2_0_C0) & 1;
			mali_l2_cache_core_set_counter_src(l2_cache_core,
							   counter_src, event_id);
			l2_cache_counter_if_enabled = MALI_TRUE;
		}
	} else {
		return 0; /* Failure, unknown event */
	}

	return 1; /* success */
}

/**
 * Called by gator.ko to retrieve the L2 cache counter values for all L2 cache cores.
 * The L2 cache counters are unique in that they are polled by gator, rather than being
 * transmitted via the tracepoint mechanism.
 *
 * @param values Pointer to a _mali_profiling_l2_counter_values structure where
 *               the counter sources and values will be output
 * @return 0 if all went well; otherwise, return the mask with the bits set for the powered off cores
 */
u32 _mali_profiling_get_l2_counters(_mali_profiling_l2_counter_values *values)
{
	u32 l2_cores_num = mali_l2_cache_core_get_glob_num_l2_cores();
	u32 i;

	MALI_DEBUG_ASSERT(l2_cores_num <= 3);

	for (i = 0; i < l2_cores_num; i++) {
		struct mali_l2_cache_core *l2_cache = mali_l2_cache_core_get_glob_l2_core(i);

		if (NULL == l2_cache) {
			continue;
		}

		mali_l2_cache_core_get_counter_values(l2_cache,
						      &values->cores[i].source0,
						      &values->cores[i].value0,
						      &values->cores[i].source1,
						      &values->cores[i].value1);
	}

	return 0;
}

/**
 * Called by gator to control the production of profiling information at runtime.
 */
void _mali_profiling_control(u32 action, u32 value)
{
	switch (action) {
	case FBDUMP_CONTROL_ENABLE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_COLORBUFFER_CAPTURE_ENABLED, (value == 0 ? MALI_FALSE : MALI_TRUE));
		break;
	case FBDUMP_CONTROL_RATE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_N_FRAMES, value);
		break;
	case SW_COUNTER_ENABLE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_COUNTER_ENABLED, value);
		break;
	case FBDUMP_CONTROL_RESIZE_FACTOR:
		mali_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_RESIZE_FACTOR, value);
		break;
	default:
		break;  /* Ignore unimplemented actions */
	}
}

/**
 * Called by gator to get mali api version.
 */
u32 _mali_profiling_get_api_version(void)
{
	return MALI_PROFILING_API_VERSION;
}

/**
* Called by gator to get the data about Mali instance in use:
* product id, version, number of cores
*/
void _mali_profiling_get_mali_version(struct _mali_profiling_mali_version *values)
{
	values->mali_product_id = (u32)mali_kernel_core_get_product_id();
	values->mali_version_major = mali_kernel_core_get_gpu_major_version();
	values->mali_version_minor = mali_kernel_core_get_gpu_minor_version();
	values->num_of_l2_cores = mali_l2_cache_core_get_glob_num_l2_cores();
	values->num_of_fp_cores = mali_executor_get_num_cores_total();
	values->num_of_vp_cores = 1;
}


EXPORT_SYMBOL(_mali_profiling_set_event);
EXPORT_SYMBOL(_mali_profiling_get_l2_counters);
EXPORT_SYMBOL(_mali_profiling_control);
EXPORT_SYMBOL(_mali_profiling_get_api_version);
EXPORT_SYMBOL(_mali_profiling_get_mali_version);
