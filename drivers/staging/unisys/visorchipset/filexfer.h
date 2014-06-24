/* filexfer.h
 *
 * Copyright © 2013 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* This header file defines the interface that filexfer.c provides to other
 * code in the visorchipset driver.
 */

#ifndef __FILEXFER_H__
#define __FILEXFER_H__

#include "globals.h"
#include "controlvmchannel.h"
#include <linux/seq_file.h>

typedef void *(*GET_CONTIGUOUS_CONTROLVM_PAYLOAD_FUNC) (ulong min_size,
							ulong max_size,
							ulong *actual_size);

typedef BOOL
(*CONTROLVM_RESPOND_WITH_PAYLOAD_FUNC) (CONTROLVM_MESSAGE_HEADER *msgHdr,
					u64 fileRequestNumber,
					u64 dataSequenceNumber,
					int response,
					void *bucket, ulong payloadChunkSize,
					ulong payloadUsedBytes, BOOL partial);

typedef void
(*TRANSMITFILE_INIT_CONTEXT_FUNC)(void *ctx,
				  const CONTROLVM_MESSAGE_HEADER *hdr,
				  u64 file_request_number);
typedef void (*TRANSMITFILE_DUMP_FUNC) (struct seq_file *f, void *ctx,
					const char *pfx);
typedef int (*GET_CONTROLVM_FILEDATA_FUNC) (void *ctx,
					    void *buf, size_t bufsize,
					    BOOL buf_is_userspace,
					    size_t *bytes_transferred);
typedef void (*CONTROLVM_RESPOND_FUNC) (void *ctx, int response);

/* Call once to initialize filexfer.o.
 * req_context_bytes number of bytes the caller needs to keep track of each file
 * transfer conversation.  The <ctx_init_value> passed to filexfer_putFile() is
 * assumed to be this many bytes in size.  Code within filexfer.o will copy this
 * into a dynamically-allocated area, and pass back a pointer to that area in
 * callback functions.
 */
int filexfer_constructor(size_t req_context_bytes);

/* Call once to clean up filexfer.o */
void filexfer_destructor(void);

/* Call this to dump diagnostic info about all outstanding getFiles/putFiles */
void filexfer_dump(struct seq_file *f);

/* Call to transfer a file from the local filesystem (i.e., from the environment
 * where this driver is running) across the controlvm channel to a remote
 * environment.  1 or more controlvm responses will be sent as a result, each
 * of which whose payload contains file data.  Only the last controlvm message
 * will have Flags.partialCompletion==0.
 *
 *   msgHdr      the controlvm message header of the GETFILE request which
 *               we just received
 *   file_request_number  this is all data from the GETFILE request that
 *   uplink_index         define which file is to be transferred
 *   disk_index
 *   file_name
 *   get_contiguous_controlvm_payload  function to call when space is needed
 *                                     in the payload area
 *   controlvm_respond_with_payload    function to call to send each controlvm
 *                                     response containing file data as the
 *                                     payload; returns FALSE only if the
 *				       payload buffer was freed inline
 *   dump_func                         function to dump context data in
 *                                     human-readable format
 *
 *  Returns TRUE iff the file transfer request has been successfully initiated,
 *  or FALSE to indicate failure.
 */
BOOL
filexfer_getFile(CONTROLVM_MESSAGE_HEADER *msgHdr,
		 u64 file_request_number,
		 uint uplink_index,
		 uint disk_index,
		 char *file_name,
		 GET_CONTIGUOUS_CONTROLVM_PAYLOAD_FUNC
		 get_contiguous_controlvm_payload,
		 CONTROLVM_RESPOND_WITH_PAYLOAD_FUNC
		 controlvm_respond_with_payload,
		 TRANSMITFILE_DUMP_FUNC dump_func);

/* Call to create a file in the local filesystem (i.e., in the environment
 * where this driver is running) from data received as payload in
 * controlvm channel messages from a remote environment.  1 or more controlvm
 * messages will be received for this transfer, and only the last will have
 * Flags.partialCompletion==0.
 *
 *   msgHdr      the controlvm message header of the PUTFILE request which
 *               we just received
 *   file_request_number  this is all data from the PUTFILE request that
 *   uplink_index         define which file is to be created in the local
 *   disk_index           filesystem
 *   file_name
 *   init_context         function to call to initialize the
 *                        <req_context_bytes>-sized storage area returned by
 *                        this func; note that it would NOT be sufficient to
 *                        allow the caller to initialize this upon return, as
 *                        the the other user-supplied callbacks might have
 *                        already been called by then
 *   get_controlvm_filedata   function to call to obtain more data for the file
 *                            being written; refer to get_controlvm_filedata()
 *                            in visorchipset_main.c for a complete description
 *                            of parameters
 *   controlvm_end_putFile    function to call to indicate that creation of the
 *                            local file has completed;  set <response> to a
 *                            negative value to indicate an error
 *   dump_func                function to dump context data in human-readable
 *                            format
 *
 *  Returns a pointer to a dynamically-allocated storage area of size
 *  <req_context_bytes> which the caller can use, or NULL for error.  The
 *  caller should NEVER free the returned pointer, but should expect to receive
 *  it as the <ctx> argument when callback functions are called.
 */
void *filexfer_putFile(CONTROLVM_MESSAGE_HEADER *msgHdr,
		       u64 file_request_number,
		       uint uplink_index,
		       uint disk_index,
		       char *file_name,
		       TRANSMITFILE_INIT_CONTEXT_FUNC init_context,
		       GET_CONTROLVM_FILEDATA_FUNC get_controlvm_filedata,
		       CONTROLVM_RESPOND_FUNC controlvm_end_putFile,
		       TRANSMITFILE_DUMP_FUNC dump_func);

#endif
