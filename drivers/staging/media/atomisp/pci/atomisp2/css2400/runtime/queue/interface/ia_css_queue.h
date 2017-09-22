#ifndef ISP2401
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
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef __IA_CSS_QUEUE_H
#define __IA_CSS_QUEUE_H

#include <platform_support.h>
#include <type_support.h>

#include "ia_css_queue_comm.h"
#include "../src/queue_access.h"

/* Local Queue object descriptor */
struct ia_css_queue_local {
	ia_css_circbuf_desc_t *cb_desc; /*Circbuf desc for local queues*/
	ia_css_circbuf_elem_t *cb_elems; /*Circbuf elements*/
};
typedef struct ia_css_queue_local ia_css_queue_local_t;

/* Handle for queue object*/
typedef struct ia_css_queue ia_css_queue_t;


/*****************************************************************************
 * Queue Public APIs
 *****************************************************************************/
/** @brief Initialize a local queue instance.
 *
 * @param[out] qhandle. Handle to queue instance for use with API
 * @param[in]  desc.   Descriptor with queue properties filled-in
 * @return     0      - Successful init of local queue instance.
 * @return     EINVAL - Invalid argument.
 *
 */
extern int ia_css_queue_local_init(
			ia_css_queue_t *qhandle,
			ia_css_queue_local_t *desc);

/** @brief Initialize a remote queue instance
 *
 * @param[out] qhandle. Handle to queue instance for use with API
 * @param[in]  desc.   Descriptor with queue properties filled-in
 * @return     0      - Successful init of remote queue instance.
 * @return     EINVAL - Invalid argument.
 */
extern int ia_css_queue_remote_init(
			ia_css_queue_t *qhandle,
			ia_css_queue_remote_t *desc);

/** @brief Uninitialize a queue instance
 *
 * @param[in]  qhandle. Handle to queue instance
 * @return     0 - Successful uninit.
 *
 */
extern int ia_css_queue_uninit(
			ia_css_queue_t *qhandle);

/** @brief Enqueue an item in the queue instance
 *
 * @param[in]  qhandle. Handle to queue instance
 * @param[in]  item.    Object to be enqueued.
 * @return     0       - Successful enqueue.
 * @return     EINVAL  - Invalid argument.
 * @return     ENOBUFS - Queue is full.
 *
 */
extern int ia_css_queue_enqueue(
			ia_css_queue_t *qhandle,
			uint32_t item);

/** @brief Dequeue an item from the queue instance
 *
 * @param[in]  qhandle. Handle to queue instance
 * @param[out] item.    Object to be dequeued into this item.

 * @return     0       - Successful dequeue.
 * @return     EINVAL  - Invalid argument.
 * @return     ENODATA - Queue is empty.
 *
 */
extern int ia_css_queue_dequeue(
			ia_css_queue_t *qhandle,
			uint32_t *item);

/** @brief Check if the queue is empty
 *
 * @param[in]  qhandle.  Handle to queue instance
 * @param[in]  is_empty  True if empty, False if not.
 * @return     0       - Successful access state.
 * @return     EINVAL  - Invalid argument.
 * @return     ENOSYS  - Function not implemented.
 *
 */
extern int ia_css_queue_is_empty(
			ia_css_queue_t *qhandle,
			bool *is_empty);

/** @brief Check if the queue is full
 *
 * @param[in]  qhandle.  Handle to queue instance
 * @param[in]  is_full   True if Full, False if not.
 * @return     0       - Successfully access state.
 * @return     EINVAL  - Invalid argument.
 * @return     ENOSYS  - Function not implemented.
 *
 */
extern int ia_css_queue_is_full(
			ia_css_queue_t *qhandle,
			bool *is_full);

/** @brief Get used space in the queue
 *
 * @param[in]  qhandle.  Handle to queue instance
 * @param[in]  size      Number of available elements in the queue
 * @return     0       - Successfully access state.
 * @return     EINVAL  - Invalid argument.
 *
 */
extern int ia_css_queue_get_used_space(
			ia_css_queue_t *qhandle,
			uint32_t *size);

/** @brief Get free space in the queue
 *
 * @param[in]  qhandle.  Handle to queue instance
 * @param[in]  size      Number of free elements in the queue
 * @return     0       - Successfully access state.
 * @return     EINVAL  - Invalid argument.
 *
 */
extern int ia_css_queue_get_free_space(
			ia_css_queue_t *qhandle,
			uint32_t *size);

/** @brief Peek at an element in the queue
 *
 * @param[in]  qhandle.  Handle to queue instance
 * @param[in]  offset   Offset of element to peek,
 * 			 starting from head of queue
 * @param[in]  element   Value of element returned
 * @return     0       - Successfully access state.
 * @return     EINVAL  - Invalid argument.
 *
 */
extern int ia_css_queue_peek(
		ia_css_queue_t *qhandle,
		uint32_t offset,
		uint32_t *element);

/** @brief Get the usable size for the queue
 *
 * @param[in]  qhandle. Handle to queue instance
 * @param[out] size     Size value to be returned here.
 * @return     0       - Successful get size.
 * @return     EINVAL  - Invalid argument.
 * @return     ENOSYS  - Function not implemented.
 *
 */
extern int ia_css_queue_get_size(
		ia_css_queue_t *qhandle,
		uint32_t *size);

#endif /* __IA_CSS_QUEUE_H */

