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

#include "hi_locl.h"

#if defined(__APPLE__) && defined(HAVE_GCD)

#include "heim_ipc.h"
#include "heim_ipc_asyncServer.h"

#include <dispatch/dispatch.h>
#include <mach/mach.h>

static dispatch_once_t jobqinited = 0;
static dispatch_queue_t jobq = NULL;
static dispatch_queue_t syncq;

struct mach_ctx {
    mach_port_t server;
    char *name;
};

static int
mach_release(void *ctx);

static int
mach_init(const char *service, void **ctx)
{
    struct mach_ctx *ipc;
    mach_port_t sport;
    int ret;

    dispatch_once(&jobqinited, ^{
	    jobq = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	    syncq = dispatch_queue_create("heim-ipc-syncq", NULL);
	});

    ret = bootstrap_look_up(bootstrap_port, service, &sport);
    if (ret)
	return ret;

    ipc = malloc(sizeof(*ipc));
    if (ipc == NULL) {
	mach_port_destroy(mach_task_self(), sport);
	return ENOMEM;
    }

    ipc->server = sport;
    ipc->name = strdup(service);
    if (ipc->name == NULL) {
	mach_release(ipc);
	return ENOMEM;
    }

    *ctx = ipc;

    return 0;
}

static int
mach_ipc(void *ctx,
	 const heim_idata *request, heim_idata *response,
	 heim_icred *cred)
{
    struct mach_ctx *ipc = ctx;
    heim_ipc_message_inband_t requestin;
    mach_msg_type_number_t requestin_length = 0;
    heim_ipc_message_outband_t requestout = NULL;
    mach_msg_type_number_t requestout_length = 0;
    heim_ipc_message_inband_t replyin;
    mach_msg_type_number_t replyin_length;
    heim_ipc_message_outband_t replyout;
    mach_msg_type_number_t replyout_length;
    int ret, errorcode, retries = 0;

    memcpy(requestin, request->data, request->length);
    requestin_length = request->length;

    while (retries < 2) {
	__block mach_port_t sport;

	dispatch_sync(syncq, ^{ sport = ipc->server; });

	ret = mheim_ipc_call(sport,
			     requestin, requestin_length,
			     requestout, requestout_length,
			     &errorcode,
			     replyin, &replyin_length,
			     &replyout, &replyout_length);
	if (ret == MACH_SEND_INVALID_DEST) {
	    mach_port_t nport;
	    /* race other threads to get a new port */
	    ret = bootstrap_look_up(bootstrap_port, ipc->name, &nport);
	    if (ret)
		return ret;
	    dispatch_sync(syncq, ^{
		    /* check if we lost the race to lookup the port */
		    if (sport != ipc->server) {
			mach_port_deallocate(mach_task_self(), nport);
		    } else {
			mach_port_deallocate(mach_task_self(), ipc->server);
			ipc->server = nport;
		    }
		});
	    retries++;
	} else if (ret) {
	    return ret;
	} else
	    break;
    }
    if (retries >= 2)
	return EINVAL;

    if (errorcode) {
	if (replyout_length)
	    vm_deallocate (mach_task_self (), (vm_address_t) replyout,
			   replyout_length);
	return errorcode;
    }

    if (replyout_length) {
	response->data = malloc(replyout_length);
	if (response->data == NULL) {
	    vm_deallocate (mach_task_self (), (vm_address_t) replyout,
			   replyout_length);
	    return ENOMEM;
	}
	memcpy(response->data, replyout, replyout_length);
	response->length = replyout_length;
	vm_deallocate (mach_task_self (), (vm_address_t) replyout,
		       replyout_length);
    } else {
	response->data = malloc(replyin_length);
	if (response->data == NULL)
	    return ENOMEM;
	memcpy(response->data, replyin, replyin_length);
	response->length = replyin_length;
    }

    return 0;
}

struct async_client {
    mach_port_t mp;
    dispatch_source_t source;
    dispatch_queue_t queue;
    void (*func)(void *, int, heim_idata *, heim_icred);
    void *userctx;
};

kern_return_t
mheim_ado_acall_reply(mach_port_t server_port,
		      audit_token_t client_creds,
		      int returnvalue,
		      heim_ipc_message_inband_t replyin,
		      mach_msg_type_number_t replyinCnt,
		      heim_ipc_message_outband_t replyout,
		      mach_msg_type_number_t replyoutCnt)
{
    struct async_client *c = dispatch_get_context(dispatch_get_current_queue());
    heim_idata response;

    if (returnvalue) {
	response.data = NULL;
	response.length = 0;
    } else if (replyoutCnt) {
	response.data = replyout;
	response.length = replyoutCnt;
    } else {
	response.data = replyin;
	response.length = replyinCnt;
    }

    (*c->func)(c->userctx, returnvalue, &response, NULL);

    if (replyoutCnt)
	vm_deallocate (mach_task_self (), (vm_address_t) replyout, replyoutCnt);

    dispatch_source_cancel(c->source);

    return 0;


}


static int
mach_async(void *ctx, const heim_idata *request, void *userctx,
	   void (*func)(void *, int, heim_idata *, heim_icred))
{
    struct mach_ctx *ipc = ctx;
    heim_ipc_message_inband_t requestin;
    mach_msg_type_number_t requestin_length = 0;
    heim_ipc_message_outband_t requestout = NULL;
    mach_msg_type_number_t requestout_length = 0;
    int ret, retries = 0;
    kern_return_t kr;
    struct async_client *c;

    /* first create the service that will catch the reply from the server */
    /* XXX these object should be cached and reused */

    c = malloc(sizeof(*c));
    if (c == NULL)
	return ENOMEM;

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &c->mp);
    if (kr != KERN_SUCCESS)
	return EINVAL;

    c->queue = dispatch_queue_create("heim-ipc-async-client", NULL);
    c->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, c->mp, 0, c->queue);
    dispatch_set_context(c->queue, c);

    dispatch_source_set_event_handler(c->source, ^{
	    dispatch_mig_server(c->source,
				sizeof(union __RequestUnion__mheim_ado_mheim_aipc_subsystem),
				mheim_aipc_server);
	});

    dispatch_source_set_cancel_handler(c->source, ^{
	    mach_port_mod_refs(mach_task_self(), c->mp,
			       MACH_PORT_RIGHT_RECEIVE, -1);
	    dispatch_release(c->queue);
	    dispatch_release(c->source);
	    free(c);
	});

    c->func = func;
    c->userctx = userctx;

    dispatch_resume(c->source);

    /* ok, send the message */

    memcpy(requestin, request->data, request->length);
    requestin_length = request->length;

    while (retries < 2) {
	__block mach_port_t sport;

	dispatch_sync(syncq, ^{ sport = ipc->server; });

	ret = mheim_ipc_call_request(sport, c->mp,
				     requestin, requestin_length,
				     requestout, requestout_length);
	if (ret == MACH_SEND_INVALID_DEST) {
	    ret = bootstrap_look_up(bootstrap_port, ipc->name, &sport);
	    if (ret) {
		dispatch_source_cancel(c->source);
		return ret;
	    }
	    mach_port_deallocate(mach_task_self(), ipc->server);
	    ipc->server = sport;
	    retries++;
	} else if (ret) {
	    dispatch_source_cancel(c->source);
	    return ret;
	} else
	    break;
    }
    if (retries >= 2) {
	dispatch_source_cancel(c->source);
	return EINVAL;
    }

    return 0;
}

static int
mach_release(void *ctx)
{
    struct mach_ctx *ipc = ctx;
    if (ipc->server != MACH_PORT_NULL)
	mach_port_deallocate(mach_task_self(), ipc->server);
    free(ipc->name);
    free(ipc);
    return 0;
}

#endif

struct path_ctx {
    char *path;
    int fd;
};

static int common_release(void *);

static int
connect_unix(struct path_ctx *s)
{
    struct sockaddr_un addr;

    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, s->path, sizeof(addr.sun_path));

    s->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->fd < 0)
	return errno;
    rk_cloexec(s->fd);

    if (connect(s->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
	close(s->fd);
	return errno;
    }

    return 0;
}

static int
common_path_init(const char *service,
		 const char *file,
		 void **ctx)
{
    struct path_ctx *s;

    s = malloc(sizeof(*s));
    if (s == NULL)
	return ENOMEM;
    s->fd = -1;

    asprintf(&s->path, "/var/run/.heim_%s-%s", service, file);

    *ctx = s;

    return 0;
}

static int
unix_socket_init(const char *service,
		 void **ctx)
{
    int ret;

    ret = common_path_init(service, "socket", ctx);
    if (ret)
	return ret;
    ret = connect_unix(*ctx);
    if (ret)
	common_release(*ctx);

    return ret;
}

static int
unix_socket_ipc(void *ctx,
		const heim_idata *req, heim_idata *rep,
		heim_icred *cred)
{
    struct path_ctx *s = ctx;
    uint32_t len = htonl(req->length);
    uint32_t rv;
    int retval;

    if (cred)
	*cred = NULL;

    rep->data = NULL;
    rep->length = 0;

    if (net_write(s->fd, &len, sizeof(len)) != sizeof(len))
	return -1;
    if (net_write(s->fd, req->data, req->length) != (ssize_t)req->length)
	return -1;

    if (net_read(s->fd, &len, sizeof(len)) != sizeof(len))
	return -1;
    if (net_read(s->fd, &rv, sizeof(rv)) != sizeof(rv))
	return -1;
    retval = ntohl(rv);

    rep->length = ntohl(len);
    if (rep->length > 0) {
	rep->data = malloc(rep->length);
	if (rep->data == NULL)
	    return -1;
	if (net_read(s->fd, rep->data, rep->length) != (ssize_t)rep->length)
	    return -1;
    } else
	rep->data = NULL;

    return retval;
}

int
common_release(void *ctx)
{
    struct path_ctx *s = ctx;
    if (s->fd >= 0)
	close(s->fd);
    free(s->path);
    free(s);
    return 0;
}

#ifdef HAVE_DOOR

static int
door_init(const char *service,
	  void **ctx)
{
    ret = common_path_init(context, service, "door", ctx);
    if (ret)
	return ret;
    ret = connect_door(*ctx);
    if (ret)
	common_release(*ctx);
    return ret;
}

static int
door_ipc(void *ctx,
	 const heim_idata *request, heim_idata *response,
	 heim_icred *cred)
{
    door_arg_t arg;
    int ret;

    arg.data_ptr = request->data;
    arg.data_size = request->length;
    arg.desc_ptr = NULL;
    arg.desc_num = 0;
    arg.rbuf = NULL;
    arg.rsize = 0;

    ret = door_call(fd, &arg);
    close(fd);
    if (ret != 0)
	return errno;

    response->data = malloc(arg.rsize);
    if (response->data == NULL) {
	munmap(arg.rbuf, arg.rsize);
	return ENOMEM;
    }
    memcpy(response->data, arg.rbuf, arg.rsize);
    response->length = arg.rsize;
    munmap(arg.rbuf, arg.rsize);

    return ret;
}

#endif

struct hipc_ops {
    const char *prefix;
    int (*init)(const char *, void **);
    int (*release)(void *);
    int (*ipc)(void *,const heim_idata *, heim_idata *, heim_icred *);
    int (*async)(void *, const heim_idata *, void *,
		 void (*)(void *, int, heim_idata *, heim_icred));
};

struct hipc_ops ipcs[] = {
#if defined(__APPLE__) && defined(HAVE_GCD)
    { "MACH", mach_init, mach_release, mach_ipc, mach_async },
#endif
#ifdef HAVE_DOOR
    { "DOOR", door_init, common_release, door_ipc, NULL }
#endif
    { "UNIX", unix_socket_init, common_release, unix_socket_ipc, NULL }
};

struct heim_ipc {
    struct hipc_ops *ops;
    void *ctx;
};


int
heim_ipc_init_context(const char *name, heim_ipc *ctx)
{
    unsigned int i;
    int ret, any = 0;

    for(i = 0; i < sizeof(ipcs)/sizeof(ipcs[0]); i++) {
	size_t prefix_len = strlen(ipcs[i].prefix);
	heim_ipc c;
	if(strncmp(ipcs[i].prefix, name, prefix_len) == 0
	   && name[prefix_len] == ':')  {
	} else if (strncmp("ANY:", name, 4) == 0) {
	    prefix_len = 3;
	    any = 1;
	} else
	    continue;

	c = calloc(1, sizeof(*c));
	if (c == NULL)
	    return ENOMEM;

	c->ops = &ipcs[i];

	ret = (c->ops->init)(name + prefix_len + 1, &c->ctx);
	if (ret) {
	    free(c);
	    if (any)
		continue;
	    return ret;
	}

	*ctx = c;
	return 0;
    }

    return ENOENT;
}

void
heim_ipc_free_context(heim_ipc ctx)
{
    (ctx->ops->release)(ctx->ctx);
    free(ctx);
}

int
heim_ipc_call(heim_ipc ctx, const heim_idata *snd, heim_idata *rcv,
	      heim_icred *cred)
{
    if (cred)
	*cred = NULL;
    return (ctx->ops->ipc)(ctx->ctx, snd, rcv, cred);
}

int
heim_ipc_async(heim_ipc ctx, const heim_idata *snd, void *userctx,
	       void (*func)(void *, int, heim_idata *, heim_icred))
{
    if (ctx->ops->async == NULL) {
	heim_idata rcv;
	heim_icred cred = NULL;
	int ret;

	ret = (ctx->ops->ipc)(ctx->ctx, snd, &rcv, &cred);
	(*func)(userctx, ret, &rcv, cred);
	heim_ipc_free_cred(cred);
	free(rcv.data);
	return ret;
    } else {
	return (ctx->ops->async)(ctx->ctx, snd, userctx, func);
    }
}
