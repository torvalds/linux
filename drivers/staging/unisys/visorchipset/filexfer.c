/* filexfer.c
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

/* Code here-in is the "glue" that connects controlvm messages with the
 * sparfilexfer driver, which is used to transfer file contents as payload
 * across the controlvm channel.
 */

#include "globals.h"
#include "controlvm.h"
#include "visorchipset.h"
#include "filexfer.h"

#ifdef ENABLE_SPARFILEXFER /* sparfilexfer kernel module enabled in build */
#include "sparfilexfer.h"

/* Driver-global memory */
static LIST_HEAD(Request_list);	/* list of struct any_request *, via
				 * req_list memb */

/* lock for above pool for allocation of any_request structs, and pool
* name; note that kmem_cache_create requires that we keep the storage
* for the pool name for the life of the pool
 */
static DEFINE_SPINLOCK(Request_list_lock);

static struct kmem_cache *Request_memory_pool;
static const char Request_memory_pool_name[] = "filexfer_request_pool";
size_t Caller_req_context_bytes = 0;	/* passed to filexfer_constructor() */

/* This structure defines a single controlvm GETFILE conversation, which
 * consists of a single controlvm request message and 1 or more controlvm
 * response messages.
 */
struct getfile_request {
	CONTROLVM_MESSAGE_HEADER controlvm_header;
	atomic_t buffers_in_use;
	GET_CONTIGUOUS_CONTROLVM_PAYLOAD_FUNC get_contiguous_controlvm_payload;
	CONTROLVM_RESPOND_WITH_PAYLOAD_FUNC controlvm_respond_with_payload;
};

/* This structure defines a single controlvm PUTFILE conversation, which
 * consists of a single controlvm request with a filename, and additional
 * controlvm messages with file data.
 */
struct putfile_request {
	GET_CONTROLVM_FILEDATA_FUNC get_controlvm_filedata;
	CONTROLVM_RESPOND_FUNC controlvm_end_putFile;
};

/* This structure defines a single file transfer operation, which can either
 * be a GETFILE or PUTFILE.
 */
struct any_request {
	struct list_head req_list;
	ulong2 file_request_number;
	ulong2 data_sequence_number;
	TRANSMITFILE_DUMP_FUNC dump_func;
	BOOL is_get;
	union {
		struct getfile_request get;
		struct putfile_request put;
	};
	/* Size of caller_context_data will be
	 * <Caller_req_context_bytes> bytes.  I aligned this because I
	 * am paranoid about what happens when an arbitrary data
	 * structure with unknown alignment requirements gets copied
	 * here.  I want caller_context_data to be aligned to the
	 * coarsest possible alignment boundary that could be required
	 * for any user data structure.
	 */
	u8 caller_context_data[1] __aligned(sizeof(ulong2));
};

/*
 * Links the any_request into the global list of allocated requests
 * (<Request_list>).
 */
static void
unit_tracking_create(struct list_head *dev_list_link)
{
	unsigned long flags;
	spin_lock_irqsave(&Request_list_lock, flags);
	list_add(dev_list_link, &Request_list);
	spin_unlock_irqrestore(&Request_list_lock, flags);
}

/* Unlinks a any_request from the global list (<Request_list>).
 */
static void
unit_tracking_destroy(struct list_head *dev_list_link)
{
	unsigned long flags;
	spin_lock_irqsave(&Request_list_lock, flags);
	list_del(dev_list_link);
	spin_unlock_irqrestore(&Request_list_lock, flags);
}

/* Allocate memory for and return a new any_request struct, and
 * link it to the global list of outstanding requests.
 */
static struct any_request *
alloc_request(char *fn, int ln)
{
	struct any_request *req = (struct any_request *)
	    (visorchipset_cache_alloc(Request_memory_pool,
				      FALSE,
				      fn, ln));
	if (!req)
		return NULL;
	memset(req, 0, sizeof(struct any_request) + Caller_req_context_bytes);
	unit_tracking_create(&req->req_list);
	return req;
}

/* Book-end for alloc_request().
 */
static void
free_request(struct any_request *req, char *fn, int ln)
{
	unit_tracking_destroy(&req->req_list);
	visorchipset_cache_free(Request_memory_pool, req, fn, ln);
}

/* Constructor for filexfer.o.
 */
int
filexfer_constructor(size_t req_context_bytes)
{
	int rc = -1;

	Caller_req_context_bytes = req_context_bytes;
	Request_memory_pool =
	    kmem_cache_create(Request_memory_pool_name,
			      sizeof(struct any_request) +
			      Caller_req_context_bytes,
			      0, SLAB_HWCACHE_ALIGN, NULL);
	if (!Request_memory_pool) {
		LOGERR("failed to alloc Request_memory_pool");
		rc = -ENOMEM;
		goto Away;
	}
	rc = 0;
Away:
	if (rc < 0) {
		if (Request_memory_pool) {
			kmem_cache_destroy(Request_memory_pool);
			Request_memory_pool = NULL;
		}
	}
	return rc;
}

/* Destructor for filexfer.o.
 */
void
filexfer_destructor(void)
{
	if (Request_memory_pool) {
		kmem_cache_destroy(Request_memory_pool);
		Request_memory_pool = NULL;
	}
}

/* This function will obtain an available chunk from the controlvm payload area,
 * store the size in bytes of the chunk in <actual_size>, and return a pointer
 * to the chunk.  The function is passed to the sparfilexfer driver, which calls
 * it whenever payload space is required to copy file data into.
 */
static void *
get_empty_bucket_for_getfile_data(void *context,
				  ulong min_size, ulong max_size,
				  ulong *actual_size)
{
	void *bucket;
	struct any_request *req = (struct any_request *) context;

	if (!req->is_get) {
		LOGERR("%s - unexpected call", __func__);
		return NULL;
	}
	bucket = (*req->get.get_contiguous_controlvm_payload)
	    (min_size, max_size, actual_size);
	if (bucket != NULL) {
		atomic_inc(&req->get.buffers_in_use);
		DBGINF("%s - sent %lu-byte buffer", __func__, *actual_size);
	}
	return bucket;
}

/* This function will send a controlvm response with data in the payload
 * (whose space was obtained with get_empty_bucket_for_getfile_data).  The
 * function is passed to the sparfilexfer driver, which calls it whenever it
 * wants to send file data back across the controlvm channel.
 */
static int
send_full_getfile_data_bucket(void *context, void *bucket,
			      ulong bucket_actual_size, ulong bucket_used_size)
{
	struct any_request *req = (struct any_request *) context;

	if (!req->is_get) {
		LOGERR("%s - unexpected call", __func__);
		return 0;
	}
	DBGINF("sending buffer for %lu/%lu",
	       bucket_used_size, bucket_actual_size);
	if (!(*req->get.controlvm_respond_with_payload)
	    (&req->get.controlvm_header,
	     req->file_request_number,
	     req->data_sequence_number++,
	     0, bucket, bucket_actual_size, bucket_used_size, TRUE))
		atomic_dec(&req->get.buffers_in_use);
	return 0;
}

/* This function will send a controlvm response indicating the end of a
 * GETFILE transfer.  The function is passed to the sparfilexfer driver.
 */
static void
send_end_of_getfile_data(void *context, int status)
{
	struct any_request *req = (struct any_request *) context;
	if (!req->is_get) {
		LOGERR("%s - unexpected call", __func__);
		return;
	}
	LOGINF("status=%d", status);
	(*req->get.controlvm_respond_with_payload)
	    (&req->get.controlvm_header,
	     req->file_request_number,
	     req->data_sequence_number++, status, NULL, 0, 0, FALSE);
	free_request(req, __FILE__, __LINE__);
	module_put(THIS_MODULE);
}

/* This function supplies data for a PUTFILE transfer.
 * The function is passed to the sparfilexfer driver.
 */
static int
get_putfile_data(void *context, void *pbuf, size_t bufsize,
		 BOOL buf_is_userspace, size_t *bytes_transferred)
{
	struct any_request *req = (struct any_request *) context;
	if (req->is_get) {
		LOGERR("%s - unexpected call", __func__);
		return -1;
	}
	return (*req->put.get_controlvm_filedata) (&req->caller_context_data[0],
						   pbuf, bufsize,
						   buf_is_userspace,
						   bytes_transferred);
}

/* This function is called to indicate the end of a PUTFILE transfer.
 * The function is passed to the sparfilexfer driver.
 */
static void
end_putfile(void *context, int status)
{
	struct any_request *req = (struct any_request *) context;
	if (req->is_get) {
		LOGERR("%s - unexpected call", __func__);
		return;
	}
	(*req->put.controlvm_end_putFile) (&req->caller_context_data[0],
					   status);
	free_request(req, __FILE__, __LINE__);
	module_put(THIS_MODULE);
}

/* Refer to filexfer.h for description. */
BOOL
filexfer_getFile(CONTROLVM_MESSAGE_HEADER *msgHdr,
		 ulong2 file_request_number,
		 uint uplink_index,
		 uint disk_index,
		 char *file_name,
		 GET_CONTIGUOUS_CONTROLVM_PAYLOAD_FUNC
		 get_contiguous_controlvm_payload,
		 CONTROLVM_RESPOND_WITH_PAYLOAD_FUNC
		 controlvm_respond_with_payload,
		 TRANSMITFILE_DUMP_FUNC dump_func)
{
	BOOL use_count_up = FALSE;
	BOOL failed = TRUE;
	struct any_request *req = alloc_request(__FILE__, __LINE__);

	if (!req) {
		LOGERR("allocation of any_request failed");
		goto Away;
	}
	/* We need to increment this module's use count because we're handing
	 * off pointers to functions within this module to be used by
	 * another module.
	 */
	__module_get(THIS_MODULE);
	use_count_up = TRUE;
	req->is_get = TRUE;
	req->file_request_number = file_request_number;
	req->data_sequence_number = 0;
	req->dump_func = dump_func;
	req->get.controlvm_header = *msgHdr;
	atomic_set(&req->get.buffers_in_use, 0);
	req->get.get_contiguous_controlvm_payload =
	    get_contiguous_controlvm_payload;
	req->get.controlvm_respond_with_payload =
	    controlvm_respond_with_payload;
	if (sparfilexfer_local2remote(req,	/* context, passed to
						 * callback funcs */
				      file_name,
				      file_request_number,
				      uplink_index,
				      disk_index,
				      get_empty_bucket_for_getfile_data,
				      send_full_getfile_data_bucket,
				      send_end_of_getfile_data) < 0) {
		LOGERR("sparfilexfer_local2remote failed");
		goto Away;
	}
	failed = FALSE;
Away:
	if (failed) {
		if (use_count_up) {
			module_put(THIS_MODULE);
			use_count_up = FALSE;
		}
		if (req) {
			free_request(req, __FILE__, __LINE__);
			req = NULL;
		}
		return FALSE;
	} else {
		return TRUE;
		/* success; send callbacks will be called for responses */
	}
}

/* Refer to filexfer.h for description. */
void *
filexfer_putFile(CONTROLVM_MESSAGE_HEADER *msgHdr,
		 ulong2 file_request_number,
		 uint uplink_index,
		 uint disk_index,
		 char *file_name,
		 TRANSMITFILE_INIT_CONTEXT_FUNC init_context,
		 GET_CONTROLVM_FILEDATA_FUNC get_controlvm_filedata,
		 CONTROLVM_RESPOND_FUNC controlvm_end_putFile,
		 TRANSMITFILE_DUMP_FUNC dump_func)
{
	BOOL use_count_up = FALSE;
	BOOL failed = TRUE;
	struct any_request *req = alloc_request(__FILE__, __LINE__);
	void *caller_ctx = NULL;

	if (!req) {
		LOGERR("allocation of any_request failed");
		goto Away;
	}
	caller_ctx = (void *) (&(req->caller_context_data[0]));
	/* We need to increment this module's use count because we're handing
	 * off pointers to functions within this module to be used by
	 * another module.
	 */
	__module_get(THIS_MODULE);
	use_count_up = TRUE;
	req->is_get = FALSE;
	req->file_request_number = file_request_number;
	req->data_sequence_number = 0;
	req->dump_func = dump_func;
	req->put.get_controlvm_filedata = get_controlvm_filedata;
	req->put.controlvm_end_putFile = controlvm_end_putFile;
	(*init_context) (caller_ctx, msgHdr, file_request_number);
	if (sparfilexfer_remote2local(req,	/* context, passed to
						 * callback funcs */
				      file_name,
				      file_request_number,
				      uplink_index,
				      disk_index,
				      get_putfile_data, end_putfile) < 0) {
		LOGERR("sparfilexfer_remote2local failed");
		goto Away;
	}
	failed = FALSE;
Away:
	if (failed) {
		if (use_count_up) {
			module_put(THIS_MODULE);
			use_count_up = FALSE;
		}
		if (req) {
			free_request(req, __FILE__, __LINE__);
			req = NULL;
		}
		return NULL;
	} else {
		return caller_ctx;
		/* success; callbacks will be called for responses */
	}
}

static void
dump_get_request(struct seq_file *f, struct getfile_request *getreq)
{
	seq_printf(f, "  buffers_in_use=%d\n",
		   atomic_read(&getreq->buffers_in_use));
}

static void
dump_put_request(struct seq_file *f, struct putfile_request *putreq)
{
}

static void
dump_request(struct seq_file *f, struct any_request *req)
{
	seq_printf(f, "* %s id=%llu seq=%llu\n",
		   ((req->is_get) ? "Get" : "Put"),
		   req->file_request_number, req->data_sequence_number);
	if (req->is_get)
		dump_get_request(f, &req->get);
	else
		dump_put_request(f, &req->put);
	if (req->dump_func)
		(*req->dump_func) (f, &(req->caller_context_data[0]), "  ");
}

void
filexfer_dump(struct seq_file *f)
{
	ulong flags;
	struct list_head *entry;

	seq_puts(f, "Outstanding TRANSMIT_FILE requests:\n");
	spin_lock_irqsave(&Request_list_lock, flags);
	list_for_each(entry, &Request_list) {
		struct any_request *req;
		req = list_entry(entry, struct any_request, req_list);
		dump_request(f, req);
	}
	spin_unlock_irqrestore(&Request_list_lock, flags);
}

#else				/* ifdef ENABLE_SPARFILEXFER */
int
filexfer_constructor(size_t req_context_bytes)
{
	return 0;		/* success */
}

void
filexfer_destructor(void)
{
}

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
		 TRANSMITFILE_DUMP_FUNC dump_func)
{
	/* since no sparfilexfer module exists to call, we just fail */
	return FALSE;
}

void *
filexfer_putFile(CONTROLVM_MESSAGE_HEADER *msgHdr,
		 u64 file_request_number,
		 uint uplink_index,
		 uint disk_index,
		 char *file_name,
		 TRANSMITFILE_INIT_CONTEXT_FUNC init_context,
		 GET_CONTROLVM_FILEDATA_FUNC get_controlvm_filedata,
		 CONTROLVM_RESPOND_FUNC controlvm_end_putFile,
		 TRANSMITFILE_DUMP_FUNC dump_func)
{
	/* since no sparfilexfer module exists to call, we just fail */
	return NULL;
}

void
filexfer_dump(struct seq_file *f)
{
}

#endif				/* ifdef ENABLE_SPARFILEXFER */
