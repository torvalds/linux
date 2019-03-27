/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <asn1-common.h>

typedef struct heim_ipc *heim_ipc;
typedef struct heim_sipc *heim_sipc;
typedef struct heim_icred *heim_icred;
typedef struct heim_isemaphore *heim_isemaphore;
typedef struct heim_octet_string heim_idata;
typedef struct heim_sipc_call *heim_sipc_call;

/* common */

void
heim_ipc_free_cred(heim_icred);

uid_t
heim_ipc_cred_get_uid(heim_icred);

gid_t
heim_ipc_cred_get_gid(heim_icred);

pid_t
heim_ipc_cred_get_pid(heim_icred);

pid_t
heim_ipc_cred_get_session(heim_icred);

void
heim_ipc_main(void);

heim_isemaphore
heim_ipc_semaphore_create(long);

long
heim_ipc_semaphore_wait(heim_isemaphore, time_t);

long
heim_ipc_semaphore_signal(heim_isemaphore);

void
heim_ipc_semaphore_release(heim_isemaphore);

#define HEIM_IPC_WAIT_FOREVER ((time_t)-1)

void
heim_ipc_free_data(heim_idata *);

/* client */

int
heim_ipc_init_context(const char *, heim_ipc *);

void
heim_ipc_free_context(heim_ipc);

int
heim_ipc_call(heim_ipc, const heim_idata *, heim_idata *, heim_icred *);

int
heim_ipc_async(heim_ipc, const heim_idata *, void *, void (*func)(void *, int, heim_idata *, heim_icred));

/* server */

#define HEIM_SIPC_TYPE_IPC		1
#define HEIM_SIPC_TYPE_UINT32		2
#define HEIM_SIPC_TYPE_HTTP		4

typedef void
(*heim_ipc_complete)(heim_sipc_call, int, heim_idata *);

typedef void
(*heim_ipc_callback)(void *, const heim_idata *,
		     const heim_icred, heim_ipc_complete, heim_sipc_call);


int
heim_sipc_launchd_mach_init(const char *, heim_ipc_callback,
			    void *, heim_sipc *);

int
heim_sipc_stream_listener(int, int, heim_ipc_callback,
			  void *, heim_sipc *);

int
heim_sipc_service_unix(const char *, heim_ipc_callback,
		       void *, heim_sipc *);


void
heim_sipc_timeout(time_t);

void
heim_sipc_set_timeout_handler(void (*)(void));

void
heim_sipc_free_context(heim_sipc);
