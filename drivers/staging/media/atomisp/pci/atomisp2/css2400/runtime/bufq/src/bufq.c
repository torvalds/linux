/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "assert_support.h"		/* assert */
#include "ia_css_buffer.h"
#include "sp.h"
#include "ia_css_bufq.h"		/* Bufq API's */
#include "ia_css_queue.h"		/* ia_css_queue_t */
#include "sw_event_global.h"		/* Event IDs.*/
#include "ia_css_eventq.h"		/* ia_css_eventq_recv()*/
#include "ia_css_debug.h"		/* ia_css_debug_dtrace*/
#include "sh_css_internal.h"		/* sh_css_queue_type */
#include "sp_local.h"			/* sp_address_of */
#include "ia_css_util.h"		/* ia_css_convert_errno()*/
#include "sh_css_firmware.h"		/* sh_css_sp_fw*/

#define BUFQ_DUMP_FILE_NAME_PREFIX_SIZE 256

static char prefix[BUFQ_DUMP_FILE_NAME_PREFIX_SIZE] = {0};

/*********************************************************/
/* Global Queue objects used by CSS                      */
/*********************************************************/

#ifndef ISP2401

struct sh_css_queues {
	/* Host2SP buffer queue */
	ia_css_queue_t host2sp_buffer_queue_handles
		[SH_CSS_MAX_SP_THREADS][SH_CSS_MAX_NUM_QUEUES];
	/* SP2Host buffer queue */
	ia_css_queue_t sp2host_buffer_queue_handles
		[SH_CSS_MAX_NUM_QUEUES];

	/* Host2SP event queue */
	ia_css_queue_t host2sp_psys_event_queue_handle;

	/* SP2Host event queue */
	ia_css_queue_t sp2host_psys_event_queue_handle;

#if !defined(HAS_NO_INPUT_SYSTEM)
	/* Host2SP ISYS event queue */
	ia_css_queue_t host2sp_isys_event_queue_handle;

	/* SP2Host ISYS event queue */
	ia_css_queue_t sp2host_isys_event_queue_handle;
#endif
	/* Tagger command queue */
	ia_css_queue_t host2sp_tag_cmd_queue_handle;
};

#else

struct sh_css_queues {
	/* Host2SP buffer queue */
	ia_css_queue_t host2sp_buffer_queue_handles
		[SH_CSS_MAX_SP_THREADS][SH_CSS_MAX_NUM_QUEUES];
	/* SP2Host buffer queue */
	ia_css_queue_t sp2host_buffer_queue_handles
		[SH_CSS_MAX_NUM_QUEUES];

	/* Host2SP event queue */
	ia_css_queue_t host2sp_psys_event_queue_handle;

	/* SP2Host event queue */
	ia_css_queue_t sp2host_psys_event_queue_handle;

#if !defined(HAS_NO_INPUT_SYSTEM)
	/* Host2SP ISYS event queue */
	ia_css_queue_t host2sp_isys_event_queue_handle;

	/* SP2Host ISYS event queue */
	ia_css_queue_t sp2host_isys_event_queue_handle;

	/* Tagger command queue */
	ia_css_queue_t host2sp_tag_cmd_queue_handle;
#endif
};

#endif

/*******************************************************
*** Static variables
********************************************************/
static struct sh_css_queues css_queues;

static int buffer_type_to_queue_id_map[SH_CSS_MAX_SP_THREADS][IA_CSS_NUM_DYNAMIC_BUFFER_TYPE];
static bool queue_availability[SH_CSS_MAX_SP_THREADS][SH_CSS_MAX_NUM_QUEUES];

/*******************************************************
*** Static functions
********************************************************/
static void map_buffer_type_to_queue_id(
	unsigned int thread_id,
	enum ia_css_buffer_type buf_type
	);
static void unmap_buffer_type_to_queue_id(
	unsigned int thread_id,
	enum ia_css_buffer_type buf_type
	);

static ia_css_queue_t *bufq_get_qhandle(
	enum sh_css_queue_type type,
	enum sh_css_queue_id id,
	int thread
	);

/*******************************************************
*** Public functions
********************************************************/
void ia_css_queue_map_init(void)
{
	unsigned int i, j;

	for (i = 0; i < SH_CSS_MAX_SP_THREADS; i++) {
		for (j = 0; j < SH_CSS_MAX_NUM_QUEUES; j++)
			queue_availability[i][j] = true;
	}

	for (i = 0; i < SH_CSS_MAX_SP_THREADS; i++) {
		for (j = 0; j < IA_CSS_NUM_DYNAMIC_BUFFER_TYPE; j++)
			buffer_type_to_queue_id_map[i][j] = SH_CSS_INVALID_QUEUE_ID;
	}
}

void ia_css_queue_map(
	unsigned int thread_id,
	enum ia_css_buffer_type buf_type,
	bool map)
{
	assert(buf_type < IA_CSS_NUM_DYNAMIC_BUFFER_TYPE);
	assert(thread_id < SH_CSS_MAX_SP_THREADS);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_queue_map() enter: buf_type=%d, thread_id=%d\n", buf_type, thread_id);

	if (map)
		map_buffer_type_to_queue_id(thread_id, buf_type);
	else
		unmap_buffer_type_to_queue_id(thread_id, buf_type);
}

/*
 * @brief Query the internal queue ID.
 */
bool ia_css_query_internal_queue_id(
	enum ia_css_buffer_type buf_type,
	unsigned int thread_id,
	enum sh_css_queue_id *val)
{
	IA_CSS_ENTER("buf_type=%d, thread_id=%d, val = %p", buf_type, thread_id, val);

	if ((val == NULL) || (thread_id >= SH_CSS_MAX_SP_THREADS) || (buf_type >= IA_CSS_NUM_DYNAMIC_BUFFER_TYPE)) {
		IA_CSS_LEAVE("return_val = false");
		return false;
	}

	*val = buffer_type_to_queue_id_map[thread_id][buf_type];
	if ((*val == SH_CSS_INVALID_QUEUE_ID) || (*val >= SH_CSS_MAX_NUM_QUEUES)) {
		IA_CSS_LOG("INVALID queue ID MAP = %d\n", *val);
		IA_CSS_LEAVE("return_val = false");
		return false;
	}
	IA_CSS_LEAVE("return_val = true");
	return true;
}

/*******************************************************
*** Static functions
********************************************************/
static void map_buffer_type_to_queue_id(
	unsigned int thread_id,
	enum ia_css_buffer_type buf_type)
{
	unsigned int i;

	assert(thread_id < SH_CSS_MAX_SP_THREADS);
	assert(buf_type < IA_CSS_NUM_DYNAMIC_BUFFER_TYPE);
	assert(buffer_type_to_queue_id_map[thread_id][buf_type] == SH_CSS_INVALID_QUEUE_ID);

	/* queue 0 is reserved for parameters because it doesn't depend on events */
	if (buf_type == IA_CSS_BUFFER_TYPE_PARAMETER_SET) {
		assert(queue_availability[thread_id][IA_CSS_PARAMETER_SET_QUEUE_ID]);
		queue_availability[thread_id][IA_CSS_PARAMETER_SET_QUEUE_ID] = false;
		buffer_type_to_queue_id_map[thread_id][buf_type] = IA_CSS_PARAMETER_SET_QUEUE_ID;
		return;
	}

	/* queue 1 is reserved for per frame parameters because it doesn't depend on events */
	if (buf_type == IA_CSS_BUFFER_TYPE_PER_FRAME_PARAMETER_SET) {
		assert(queue_availability[thread_id][IA_CSS_PER_FRAME_PARAMETER_SET_QUEUE_ID]);
		queue_availability[thread_id][IA_CSS_PER_FRAME_PARAMETER_SET_QUEUE_ID] = false;
		buffer_type_to_queue_id_map[thread_id][buf_type] = IA_CSS_PER_FRAME_PARAMETER_SET_QUEUE_ID;
		return;
	}

	for (i = SH_CSS_QUEUE_C_ID; i < SH_CSS_MAX_NUM_QUEUES; i++) {
		if (queue_availability[thread_id][i]) {
			queue_availability[thread_id][i] = false;
			buffer_type_to_queue_id_map[thread_id][buf_type] = i;
			break;
		}
	}

	assert(i != SH_CSS_MAX_NUM_QUEUES);
	return;
}

static void unmap_buffer_type_to_queue_id(
	unsigned int thread_id,
	enum ia_css_buffer_type buf_type)
{
	int queue_id;

	assert(thread_id < SH_CSS_MAX_SP_THREADS);
	assert(buf_type < IA_CSS_NUM_DYNAMIC_BUFFER_TYPE);
	assert(buffer_type_to_queue_id_map[thread_id][buf_type] != SH_CSS_INVALID_QUEUE_ID);

	queue_id = buffer_type_to_queue_id_map[thread_id][buf_type];
	buffer_type_to_queue_id_map[thread_id][buf_type] = SH_CSS_INVALID_QUEUE_ID;
	queue_availability[thread_id][queue_id] = true;
}


static ia_css_queue_t *bufq_get_qhandle(
	enum sh_css_queue_type type,
	enum sh_css_queue_id id,
	int thread)
{
	ia_css_queue_t *q = NULL;

	switch (type) {
	case sh_css_host2sp_buffer_queue:
		if ((thread >= SH_CSS_MAX_SP_THREADS) || (thread < 0) ||
			(id == SH_CSS_INVALID_QUEUE_ID))
			break;
		q = &css_queues.host2sp_buffer_queue_handles[thread][id];
		break;
	case sh_css_sp2host_buffer_queue:
		if (id == SH_CSS_INVALID_QUEUE_ID)
			break;
		q = &css_queues.sp2host_buffer_queue_handles[id];
		break;
	case sh_css_host2sp_psys_event_queue:
		q = &css_queues.host2sp_psys_event_queue_handle;
		break;
	case sh_css_sp2host_psys_event_queue:
		q = &css_queues.sp2host_psys_event_queue_handle;
		break;
#if !defined(HAS_NO_INPUT_SYSTEM)
	case sh_css_host2sp_isys_event_queue:
		q = &css_queues.host2sp_isys_event_queue_handle;
		break;
	case sh_css_sp2host_isys_event_queue:
		q = &css_queues.sp2host_isys_event_queue_handle;
		break;
#endif
	case sh_css_host2sp_tag_cmd_queue:
		q = &css_queues.host2sp_tag_cmd_queue_handle;
		break;
	default:
		break;
	}

	return q;
}

/* Local function to initialize a buffer queue. This reduces
 * the chances of copy-paste errors or typos.
 */
static inline void
init_bufq(unsigned int desc_offset,
	  unsigned int elems_offset,
	  ia_css_queue_t *handle)
{
	const struct ia_css_fw_info *fw;
	unsigned int q_base_addr;
	ia_css_queue_remote_t remoteq;

	fw = &sh_css_sp_fw;
	q_base_addr = fw->info.sp.host_sp_queue;

	/* Setup queue location as SP and proc id as SP0_ID */
	remoteq.location = IA_CSS_QUEUE_LOC_SP;
	remoteq.proc_id = SP0_ID;
	remoteq.cb_desc_addr = q_base_addr + desc_offset;
	remoteq.cb_elems_addr = q_base_addr + elems_offset;
	/* Initialize the queue instance and obtain handle */
	ia_css_queue_remote_init(handle, &remoteq);
}

void ia_css_bufq_init(void)
{
	int i, j;

	IA_CSS_ENTER_PRIVATE("");

	/* Setup all the local queue descriptors for Host2SP Buffer Queues */
	for (i = 0; i < SH_CSS_MAX_SP_THREADS; i++)
		for (j = 0; j < SH_CSS_MAX_NUM_QUEUES; j++) {
			init_bufq((uint32_t)offsetof(struct host_sp_queues, host2sp_buffer_queues_desc[i][j]),
				  (uint32_t)offsetof(struct host_sp_queues, host2sp_buffer_queues_elems[i][j]),
				  &css_queues.host2sp_buffer_queue_handles[i][j]);
		}

	/* Setup all the local queue descriptors for SP2Host Buffer Queues */
	for (i = 0; i < SH_CSS_MAX_NUM_QUEUES; i++) {
		init_bufq(offsetof(struct host_sp_queues, sp2host_buffer_queues_desc[i]),
			  offsetof(struct host_sp_queues, sp2host_buffer_queues_elems[i]),
			  &css_queues.sp2host_buffer_queue_handles[i]);
	}

	/* Host2SP event queue*/
	init_bufq((uint32_t)offsetof(struct host_sp_queues, host2sp_psys_event_queue_desc),
		  (uint32_t)offsetof(struct host_sp_queues, host2sp_psys_event_queue_elems),
		  &css_queues.host2sp_psys_event_queue_handle);

	/* SP2Host event queue */
	init_bufq((uint32_t)offsetof(struct host_sp_queues, sp2host_psys_event_queue_desc),
		  (uint32_t)offsetof(struct host_sp_queues, sp2host_psys_event_queue_elems),
		  &css_queues.sp2host_psys_event_queue_handle);

#if !defined(HAS_NO_INPUT_SYSTEM)
	/* Host2SP ISYS event queue */
	init_bufq((uint32_t)offsetof(struct host_sp_queues, host2sp_isys_event_queue_desc),
		  (uint32_t)offsetof(struct host_sp_queues, host2sp_isys_event_queue_elems),
		  &css_queues.host2sp_isys_event_queue_handle);

	/* SP2Host ISYS event queue*/
	init_bufq((uint32_t)offsetof(struct host_sp_queues, sp2host_isys_event_queue_desc),
		  (uint32_t)offsetof(struct host_sp_queues, sp2host_isys_event_queue_elems),
		  &css_queues.sp2host_isys_event_queue_handle);

	/* Host2SP tagger command queue */
	init_bufq((uint32_t)offsetof(struct host_sp_queues, host2sp_tag_cmd_queue_desc),
		  (uint32_t)offsetof(struct host_sp_queues, host2sp_tag_cmd_queue_elems),
		  &css_queues.host2sp_tag_cmd_queue_handle);
#endif

	IA_CSS_LEAVE_PRIVATE("");
}

enum ia_css_err ia_css_bufq_enqueue_buffer(
	int thread_index,
	int queue_id,
	uint32_t item)
{
	enum ia_css_err return_err = IA_CSS_SUCCESS;
	ia_css_queue_t *q;
	int error;

	IA_CSS_ENTER_PRIVATE("queue_id=%d", queue_id);
	if ((thread_index >= SH_CSS_MAX_SP_THREADS) || (thread_index < 0) ||
			(queue_id == SH_CSS_INVALID_QUEUE_ID))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	/* Get the queue for communication */
	q = bufq_get_qhandle(sh_css_host2sp_buffer_queue,
		queue_id,
		thread_index);
	if (q != NULL) {
		error = ia_css_queue_enqueue(q, item);
		return_err = ia_css_convert_errno(error);
	} else {
		IA_CSS_ERROR("queue is not initialized");
		return_err = IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}

	IA_CSS_LEAVE_ERR_PRIVATE(return_err);
	return return_err;
}

enum ia_css_err ia_css_bufq_dequeue_buffer(
	int queue_id,
	uint32_t *item)
{
	enum ia_css_err return_err;
	int error = 0;
	ia_css_queue_t *q;

	IA_CSS_ENTER_PRIVATE("queue_id=%d", queue_id);
	if ((item == NULL) ||
	    (queue_id <= SH_CSS_INVALID_QUEUE_ID) ||
	    (queue_id >= SH_CSS_MAX_NUM_QUEUES)
	   )
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	q = bufq_get_qhandle(sh_css_sp2host_buffer_queue,
		queue_id,
		-1);
	if (q != NULL) {
		error = ia_css_queue_dequeue(q, item);
		return_err = ia_css_convert_errno(error);
	} else {
		IA_CSS_ERROR("queue is not initialized");
		return_err = IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}

	IA_CSS_LEAVE_ERR_PRIVATE(return_err);
	return return_err;
}

enum ia_css_err ia_css_bufq_enqueue_psys_event(
	uint8_t evt_id,
	uint8_t evt_payload_0,
	uint8_t evt_payload_1,
	uint8_t evt_payload_2)
{
	enum ia_css_err return_err;
	int error = 0;
	ia_css_queue_t *q;

	IA_CSS_ENTER_PRIVATE("evt_id=%d", evt_id);
	q = bufq_get_qhandle(sh_css_host2sp_psys_event_queue, -1, -1);
	if (NULL == q) {
		IA_CSS_ERROR("queue is not initialized");
		return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}

	error = ia_css_eventq_send(q,
			evt_id, evt_payload_0, evt_payload_1, evt_payload_2);

	return_err = ia_css_convert_errno(error);
	IA_CSS_LEAVE_ERR_PRIVATE(return_err);
	return return_err;
}

enum  ia_css_err ia_css_bufq_dequeue_psys_event(
	uint8_t item[BUFQ_EVENT_SIZE])
{
	enum ia_css_err;
	int error = 0;
	ia_css_queue_t *q;

	/* No ENTER/LEAVE in this function since this is polled
	 * by some test apps. Enablign logging here floods the log
	 * files which may cause timeouts. */
	if (item == NULL)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	q = bufq_get_qhandle(sh_css_sp2host_psys_event_queue, -1, -1);
	if (NULL == q) {
		IA_CSS_ERROR("queue is not initialized");
		return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}
	error = ia_css_eventq_recv(q, item);

	return ia_css_convert_errno(error);

}

enum  ia_css_err ia_css_bufq_dequeue_isys_event(
	uint8_t item[BUFQ_EVENT_SIZE])
{
#if !defined(HAS_NO_INPUT_SYSTEM)
	enum ia_css_err;
	int error = 0;
	ia_css_queue_t *q;

	/* No ENTER/LEAVE in this function since this is polled
	 * by some test apps. Enablign logging here floods the log
	 * files which may cause timeouts. */
	if (item == NULL)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	q = bufq_get_qhandle(sh_css_sp2host_isys_event_queue, -1, -1);
	if (q == NULL) {
		IA_CSS_ERROR("queue is not initialized");
		return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}
	error = ia_css_eventq_recv(q, item);
	return ia_css_convert_errno(error);
#else
	(void)item;
	return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
#endif
}

enum ia_css_err ia_css_bufq_enqueue_isys_event(uint8_t evt_id)
{
#if !defined(HAS_NO_INPUT_SYSTEM)
	enum ia_css_err return_err;
	int error = 0;
	ia_css_queue_t *q;

	IA_CSS_ENTER_PRIVATE("event_id=%d", evt_id);
	q = bufq_get_qhandle(sh_css_host2sp_isys_event_queue, -1, -1);
	if (q == NULL) {
		IA_CSS_ERROR("queue is not initialized");
		return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}

	error = ia_css_eventq_send(q, evt_id, 0, 0, 0);
	return_err = ia_css_convert_errno(error);
	IA_CSS_LEAVE_ERR_PRIVATE(return_err);
	return return_err;
#else
	(void)evt_id;
	return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
#endif
}

enum ia_css_err ia_css_bufq_enqueue_tag_cmd(
	uint32_t item)
{
#if !defined(HAS_NO_INPUT_SYSTEM)
	enum ia_css_err return_err;
	int error = 0;
	ia_css_queue_t *q;

	IA_CSS_ENTER_PRIVATE("item=%d", item);
	q = bufq_get_qhandle(sh_css_host2sp_tag_cmd_queue, -1, -1);
	if (NULL == q) {
		IA_CSS_ERROR("queue is not initialized");
		return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
	}
	error = ia_css_queue_enqueue(q, item);

	return_err = ia_css_convert_errno(error);
	IA_CSS_LEAVE_ERR_PRIVATE(return_err);
	return return_err;
#else
	(void)item;
	return IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;
#endif
}

enum ia_css_err ia_css_bufq_deinit(void)
{
	return IA_CSS_SUCCESS;
}

static void bufq_dump_queue_info(const char *prefix, ia_css_queue_t *qhandle)
{
	uint32_t free = 0, used = 0;
	assert(prefix != NULL && qhandle != NULL);
	ia_css_queue_get_used_space(qhandle, &used);
	ia_css_queue_get_free_space(qhandle, &free);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s: used=%u free=%u\n",
		prefix, used, free);

}

void ia_css_bufq_dump_queue_info(void)
{
	int i, j;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "Queue Information:\n");

	for (i = 0; i < SH_CSS_MAX_SP_THREADS; i++) {
		for (j = 0; j < SH_CSS_MAX_NUM_QUEUES; j++) {
			snprintf(prefix, BUFQ_DUMP_FILE_NAME_PREFIX_SIZE,
				"host2sp_buffer_queue[%u][%u]", i, j);
			bufq_dump_queue_info(prefix,
				&css_queues.host2sp_buffer_queue_handles[i][j]);
		}
	}

	for (i = 0; i < SH_CSS_MAX_NUM_QUEUES; i++) {
		snprintf(prefix, BUFQ_DUMP_FILE_NAME_PREFIX_SIZE,
			"sp2host_buffer_queue[%u]", i);
		bufq_dump_queue_info(prefix,
			&css_queues.sp2host_buffer_queue_handles[i]);
	}
	bufq_dump_queue_info("host2sp_psys_event",
		&css_queues.host2sp_psys_event_queue_handle);
	bufq_dump_queue_info("sp2host_psys_event",
		&css_queues.sp2host_psys_event_queue_handle);

#if !defined(HAS_NO_INPUT_SYSTEM)
	bufq_dump_queue_info("host2sp_isys_event",
		&css_queues.host2sp_isys_event_queue_handle);
	bufq_dump_queue_info("sp2host_isys_event",
		&css_queues.sp2host_isys_event_queue_handle);
	bufq_dump_queue_info("host2sp_tag_cmd",
		&css_queues.host2sp_tag_cmd_queue_handle);
#endif
}
