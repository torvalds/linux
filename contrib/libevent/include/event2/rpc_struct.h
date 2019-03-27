/*
 * Copyright (c) 2006-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVENT2_RPC_STRUCT_H_INCLUDED_
#define EVENT2_RPC_STRUCT_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

/** @file event2/rpc_struct.h

  Structures used by rpc.h.  Using these structures directly may harm
  forward compatibility: be careful!

 */

/**
 * provides information about the completed RPC request.
 */
struct evrpc_status {
#define EVRPC_STATUS_ERR_NONE		0
#define EVRPC_STATUS_ERR_TIMEOUT	1
#define EVRPC_STATUS_ERR_BADPAYLOAD	2
#define EVRPC_STATUS_ERR_UNSTARTED	3
#define EVRPC_STATUS_ERR_HOOKABORTED	4
	int error;

	/* for looking at headers or other information */
	struct evhttp_request *http_req;
};

/* the structure below needs to be synchronized with evrpc_req_generic */

/* Encapsulates a request */
struct evrpc {
	TAILQ_ENTRY(evrpc) next;

	/* the URI at which the request handler lives */
	const char* uri;

	/* creates a new request structure */
	void *(*request_new)(void *);
	void *request_new_arg;

	/* frees the request structure */
	void (*request_free)(void *);

	/* unmarshals the buffer into the proper request structure */
	int (*request_unmarshal)(void *, struct evbuffer *);

	/* creates a new reply structure */
	void *(*reply_new)(void *);
	void *reply_new_arg;

	/* frees the reply structure */
	void (*reply_free)(void *);

	/* verifies that the reply is valid */
	int (*reply_complete)(void *);

	/* marshals the reply into a buffer */
	void (*reply_marshal)(struct evbuffer*, void *);

	/* the callback invoked for each received rpc */
	void (*cb)(struct evrpc_req_generic *, void *);
	void *cb_arg;

	/* reference for further configuration */
	struct evrpc_base *base;
};

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_RPC_STRUCT_H_INCLUDED_ */
