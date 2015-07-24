/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2015 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium, Inc. for more information
 **********************************************************************/

/*! \file response_manager.h
 *  \brief Host Driver:  Response queues for host instructions.
 */

#ifndef __RESPONSE_MANAGER_H__
#define __RESPONSE_MANAGER_H__

/** Maximum ordered requests to process in every invocation of
 * lio_process_ordered_list(). The function will continue to process requests
 * as long as it can find one that has finished processing. If it keeps
 * finding requests that have completed, the function can run for ever. The
 * value defined here sets an upper limit on the number of requests it can
 * process before it returns control to the poll thread.
 */
#define  MAX_ORD_REQS_TO_PROCESS   4096

/** Head of a response list. There are several response lists in the
 *  system. One for each response order- Unordered, ordered
 *  and 1 for noresponse entries on each instruction queue.
 */
struct octeon_response_list {
	/** List structure to add delete pending entries to */
	struct list_head head;

	/** A lock for this response list */
	spinlock_t lock;

	atomic_t pending_req_count;
};

/** The type of response list.
 */
enum {
	OCTEON_ORDERED_LIST = 0,
	OCTEON_UNORDERED_NONBLOCKING_LIST = 1,
	OCTEON_UNORDERED_BLOCKING_LIST = 2,
	OCTEON_ORDERED_SC_LIST = 3
};

/** Response Order values for a Octeon Request. */
enum {
	OCTEON_RESP_ORDERED = 0,
	OCTEON_RESP_UNORDERED = 1,
	OCTEON_RESP_NORESPONSE = 2
};

/** Error codes  used in Octeon Host-Core communication.
 *
 *   31            16 15            0
 *   ---------------------------------
 *   |               |               |
 *   ---------------------------------
 *   Error codes are 32-bit wide. The upper 16-bits, called Major Error Number,
 *   are reserved to identify the group to which the error code belongs. The
 *   lower 16-bits, called Minor Error Number, carry the actual code.
 *
 *   So error codes are (MAJOR NUMBER << 16)| MINOR_NUMBER.
 */

/*------------   Error codes used by host driver   -----------------*/
#define DRIVER_MAJOR_ERROR_CODE           0x0000

/**  A value of 0x00000000 indicates no error i.e. success */
#define DRIVER_ERROR_NONE                 0x00000000

/**  (Major number: 0x0000; Minor Number: 0x0001) */
#define DRIVER_ERROR_REQ_PENDING          0x00000001
#define DRIVER_ERROR_REQ_TIMEOUT          0x00000003
#define DRIVER_ERROR_REQ_EINTR            0x00000004
#define DRIVER_ERROR_REQ_ENXIO            0x00000006
#define DRIVER_ERROR_REQ_ENOMEM           0x0000000C
#define DRIVER_ERROR_REQ_EINVAL           0x00000016
#define DRIVER_ERROR_REQ_FAILED           0x000000ff

/** Status for a request.
 * If a request is not queued to Octeon by the driver, the driver returns
 * an error condition that's describe by one of the OCTEON_REQ_ERR_* value
 * below. If the request is successfully queued, the driver will return
 * a OCTEON_REQUEST_PENDING status. OCTEON_REQUEST_TIMEOUT and
 * OCTEON_REQUEST_INTERRUPTED are only returned by the driver if the
 * response for request failed to arrive before a time-out period or if
 * the request processing * got interrupted due to a signal respectively.
 */
enum {
	OCTEON_REQUEST_DONE = (DRIVER_ERROR_NONE),
	OCTEON_REQUEST_PENDING = (DRIVER_ERROR_REQ_PENDING),
	OCTEON_REQUEST_TIMEOUT = (DRIVER_ERROR_REQ_TIMEOUT),
	OCTEON_REQUEST_INTERRUPTED = (DRIVER_ERROR_REQ_EINTR),
	OCTEON_REQUEST_NO_DEVICE = (0x00000021),
	OCTEON_REQUEST_NOT_RUNNING,
	OCTEON_REQUEST_INVALID_IQ,
	OCTEON_REQUEST_INVALID_BUFCNT,
	OCTEON_REQUEST_INVALID_RESP_ORDER,
	OCTEON_REQUEST_NO_MEMORY,
	OCTEON_REQUEST_INVALID_BUFSIZE,
	OCTEON_REQUEST_NO_PENDING_ENTRY,
	OCTEON_REQUEST_NO_IQ_SPACE = (0x7FFFFFFF)

};

/** Initialize the response lists. The number of response lists to create is
 * given by count.
 * @param octeon_dev      - the octeon device structure.
 */
int octeon_setup_response_list(struct octeon_device *octeon_dev);

void octeon_delete_response_list(struct octeon_device *octeon_dev);

/** Check the status of first entry in the ordered list. If the instruction at
 * that entry finished processing or has timed-out, the entry is cleaned.
 * @param octeon_dev  - the octeon device structure.
 * @param force_quit - the request is forced to timeout if this is 1
 * @return 1 if the ordered list is empty, 0 otherwise.
 */
int lio_process_ordered_list(struct octeon_device *octeon_dev,
			     u32 force_quit);

#endif
