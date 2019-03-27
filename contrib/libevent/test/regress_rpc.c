/*
 * Copyright (c) 2003-2007 Niels Provos <provos@citi.umich.edu>
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

/* The old tests here need assertions to work. */
#undef NDEBUG

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "event2/event-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "event2/buffer.h"
#include "event2/event.h"
#include "event2/event_compat.h"
#include "event2/http.h"
#include "event2/http_compat.h"
#include "event2/http_struct.h"
#include "event2/rpc.h"
#include "event2/rpc.h"
#include "event2/rpc_struct.h"
#include "event2/tag.h"
#include "log-internal.h"

#include "regress.gen.h"

#include "regress.h"
#include "regress_testutils.h"

#ifndef NO_PYTHON_EXISTS

static struct evhttp *
http_setup(ev_uint16_t *pport)
{
	struct evhttp *myhttp;
	ev_uint16_t port;
	struct evhttp_bound_socket *sock;

	myhttp = evhttp_new(NULL);
	if (!myhttp)
		event_errx(1, "Could not start web server");

	/* Try a few different ports */
	sock = evhttp_bind_socket_with_handle(myhttp, "127.0.0.1", 0);
	if (!sock)
		event_errx(1, "Couldn't open web port");

	port = regress_get_socket_port(evhttp_bound_socket_get_fd(sock));

	*pport = port;
	return (myhttp);
}

EVRPC_HEADER(Message, msg, kill)
EVRPC_HEADER(NeverReply, msg, kill)

EVRPC_GENERATE(Message, msg, kill)
EVRPC_GENERATE(NeverReply, msg, kill)

static int need_input_hook = 0;
static int need_output_hook = 0;

static void
MessageCb(EVRPC_STRUCT(Message)* rpc, void *arg)
{
	struct kill* kill_reply = rpc->reply;

	if (need_input_hook) {
		struct evhttp_request* req = EVRPC_REQUEST_HTTP(rpc);
		const char *header = evhttp_find_header(
			req->input_headers, "X-Hook");
		assert(header);
		assert(strcmp(header, "input") == 0);
	}

	/* we just want to fill in some non-sense */
	EVTAG_ASSIGN(kill_reply, weapon, "dagger");
	EVTAG_ASSIGN(kill_reply, action, "wave around like an idiot");

	/* no reply to the RPC */
	EVRPC_REQUEST_DONE(rpc);
}

static EVRPC_STRUCT(NeverReply) *saved_rpc;

static void
NeverReplyCb(EVRPC_STRUCT(NeverReply)* rpc, void *arg)
{
	test_ok += 1;
	saved_rpc = rpc;
}

static void
rpc_setup(struct evhttp **phttp, ev_uint16_t *pport, struct evrpc_base **pbase)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;

	http = http_setup(&port);
	base = evrpc_init(http);

	EVRPC_REGISTER(base, Message, msg, kill, MessageCb, NULL);
	EVRPC_REGISTER(base, NeverReply, msg, kill, NeverReplyCb, NULL);

	*phttp = http;
	*pport = port;
	*pbase = base;

	need_input_hook = 0;
	need_output_hook = 0;
}

static void
rpc_teardown(struct evrpc_base *base)
{
	assert(EVRPC_UNREGISTER(base, Message) == 0);
	assert(EVRPC_UNREGISTER(base, NeverReply) == 0);

	evrpc_free(base);
}

static void
rpc_postrequest_failure(struct evhttp_request *req, void *arg)
{
	if (req->response_code != HTTP_SERVUNAVAIL) {

		fprintf(stderr, "FAILED (response code)\n");
		exit(1);
	}

	test_ok = 1;
	event_loopexit(NULL);
}

/*
 * Test a malformed payload submitted as an RPC
 */

static void
rpc_basic_test(void)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evhttp_connection *evcon = NULL;
	struct evhttp_request *req = NULL;

	rpc_setup(&http, &port, &base);

	evcon = evhttp_connection_new("127.0.0.1", port);
	tt_assert(evcon);

	/*
	 * At this point, we want to schedule an HTTP POST request
	 * server using our make request method.
	 */

	req = evhttp_request_new(rpc_postrequest_failure, NULL);
	tt_assert(req);

	/* Add the information that we care about */
	evhttp_add_header(req->output_headers, "Host", "somehost");
	evbuffer_add_printf(req->output_buffer, "Some Nonsense");

	if (evhttp_make_request(evcon, req,
		EVHTTP_REQ_POST,
		"/.rpc.Message") == -1) {
		tt_abort();
	}

	test_ok = 0;

	event_dispatch();

	evhttp_connection_free(evcon);

	rpc_teardown(base);

	tt_assert(test_ok == 1);

end:
	evhttp_free(http);
}

static void
rpc_postrequest_done(struct evhttp_request *req, void *arg)
{
	struct kill* kill_reply = NULL;

	if (req->response_code != HTTP_OK) {
		fprintf(stderr, "FAILED (response code)\n");
		exit(1);
	}

	kill_reply = kill_new();

	if ((kill_unmarshal(kill_reply, req->input_buffer)) == -1) {
		fprintf(stderr, "FAILED (unmarshal)\n");
		exit(1);
	}

	kill_free(kill_reply);

	test_ok = 1;
	event_loopexit(NULL);
}

static void
rpc_basic_message(void)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evhttp_connection *evcon = NULL;
	struct evhttp_request *req = NULL;
	struct msg *msg;

	rpc_setup(&http, &port, &base);

	evcon = evhttp_connection_new("127.0.0.1", port);
	tt_assert(evcon);

	/*
	 * At this point, we want to schedule an HTTP POST request
	 * server using our make request method.
	 */

	req = evhttp_request_new(rpc_postrequest_done, NULL);
	if (req == NULL) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	/* Add the information that we care about */
	evhttp_add_header(req->output_headers, "Host", "somehost");

	/* set up the basic message */
	msg = msg_new();
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");
	msg_marshal(req->output_buffer, msg);
	msg_free(msg);

	if (evhttp_make_request(evcon, req,
		EVHTTP_REQ_POST,
		"/.rpc.Message") == -1) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	test_ok = 0;

	event_dispatch();

	evhttp_connection_free(evcon);

	rpc_teardown(base);

end:
	evhttp_free(http);
}

static struct evrpc_pool *
rpc_pool_with_connection(ev_uint16_t port)
{
	struct evhttp_connection *evcon;
	struct evrpc_pool *pool;

	pool = evrpc_pool_new(NULL);
	assert(pool != NULL);

	evcon = evhttp_connection_new("127.0.0.1", port);
	assert(evcon != NULL);

	evrpc_pool_add_connection(pool, evcon);

	return (pool);
}

static void
GotKillCb(struct evrpc_status *status,
    struct msg *msg, struct kill *kill, void *arg)
{
	char *weapon;
	char *action;

	if (need_output_hook) {
		struct evhttp_request *req = status->http_req;
		const char *header = evhttp_find_header(
			req->input_headers, "X-Pool-Hook");
		assert(header);
		assert(strcmp(header, "ran") == 0);
	}

	if (status->error != EVRPC_STATUS_ERR_NONE)
		goto done;

	if (EVTAG_GET(kill, weapon, &weapon) == -1) {
		fprintf(stderr, "get weapon\n");
		goto done;
	}
	if (EVTAG_GET(kill, action, &action) == -1) {
		fprintf(stderr, "get action\n");
		goto done;
	}

	if (strcmp(weapon, "dagger"))
		goto done;

	if (strcmp(action, "wave around like an idiot"))
		goto done;

	test_ok += 1;

done:
	event_loopexit(NULL);
}

static void
GotKillCbTwo(struct evrpc_status *status,
    struct msg *msg, struct kill *kill, void *arg)
{
	char *weapon;
	char *action;

	if (status->error != EVRPC_STATUS_ERR_NONE)
		goto done;

	if (EVTAG_GET(kill, weapon, &weapon) == -1) {
		fprintf(stderr, "get weapon\n");
		goto done;
	}
	if (EVTAG_GET(kill, action, &action) == -1) {
		fprintf(stderr, "get action\n");
		goto done;
	}

	if (strcmp(weapon, "dagger"))
		goto done;

	if (strcmp(action, "wave around like an idiot"))
		goto done;

	test_ok += 1;

done:
	if (test_ok == 2)
		event_loopexit(NULL);
}

static int
rpc_hook_add_header(void *ctx, struct evhttp_request *req,
    struct evbuffer *evbuf, void *arg)
{
	const char *hook_type = arg;
	if (strcmp("input", hook_type) == 0)
		evhttp_add_header(req->input_headers, "X-Hook", hook_type);
	else
		evhttp_add_header(req->output_headers, "X-Hook", hook_type);

	assert(evrpc_hook_get_connection(ctx) != NULL);

	return (EVRPC_CONTINUE);
}

static int
rpc_hook_add_meta(void *ctx, struct evhttp_request *req,
    struct evbuffer *evbuf, void *arg)
{
	evrpc_hook_add_meta(ctx, "meta", "test", 5);

	assert(evrpc_hook_get_connection(ctx) != NULL);

	return (EVRPC_CONTINUE);
}

static int
rpc_hook_remove_header(void *ctx, struct evhttp_request *req,
    struct evbuffer *evbuf, void *arg)
{
	const char *header = evhttp_find_header(req->input_headers, "X-Hook");
	void *data = NULL;
	size_t data_len = 0;

	assert(header != NULL);
	assert(strcmp(header, arg) == 0);

	evhttp_remove_header(req->input_headers, "X-Hook");
	evhttp_add_header(req->input_headers, "X-Pool-Hook", "ran");

	assert(evrpc_hook_find_meta(ctx, "meta", &data, &data_len) == 0);
	assert(data != NULL);
	assert(data_len == 5);

	assert(evrpc_hook_get_connection(ctx) != NULL);

	return (EVRPC_CONTINUE);
}

static void
rpc_basic_client(void)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg = NULL;
	struct kill *kill = NULL;

	rpc_setup(&http, &port, &base);

	need_input_hook = 1;
	need_output_hook = 1;

	assert(evrpc_add_hook(base, EVRPC_INPUT, rpc_hook_add_header, (void*)"input")
	    != NULL);
	assert(evrpc_add_hook(base, EVRPC_OUTPUT, rpc_hook_add_header, (void*)"output")
	    != NULL);

	pool = rpc_pool_with_connection(port);
	tt_assert(pool);

	assert(evrpc_add_hook(pool, EVRPC_OUTPUT, rpc_hook_add_meta, NULL));
	assert(evrpc_add_hook(pool, EVRPC_INPUT, rpc_hook_remove_header, (void*)"output"));

	/* set up the basic message */
	msg = msg_new();
	tt_assert(msg);
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill = kill_new();

	EVRPC_MAKE_REQUEST(Message, pool, msg, kill,  GotKillCb, NULL);

	test_ok = 0;

	event_dispatch();

	tt_assert(test_ok == 1);

	/* we do it twice to make sure that reuse works correctly */
	kill_clear(kill);

	EVRPC_MAKE_REQUEST(Message, pool, msg, kill,  GotKillCb, NULL);

	event_dispatch();

	tt_assert(test_ok == 2);

	/* we do it trice to make sure other stuff works, too */
	kill_clear(kill);

	{
		struct evrpc_request_wrapper *ctx =
		    EVRPC_MAKE_CTX(Message, msg, kill,
			pool, msg, kill, GotKillCb, NULL);
		evrpc_make_request(ctx);
	}

	event_dispatch();

	rpc_teardown(base);

	tt_assert(test_ok == 3);

end:
	if (msg)
		msg_free(msg);
	if (kill)
		kill_free(kill);

	if (pool)
		evrpc_pool_free(pool);
	if (http)
		evhttp_free(http);

	need_input_hook = 0;
	need_output_hook = 0;
}

/*
 * We are testing that the second requests gets send over the same
 * connection after the first RPCs completes.
 */
static void
rpc_basic_queued_client(void)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg=NULL;
	struct kill *kill_one=NULL, *kill_two=NULL;

	rpc_setup(&http, &port, &base);

	pool = rpc_pool_with_connection(port);
	tt_assert(pool);

	/* set up the basic message */
	msg = msg_new();
	tt_assert(msg);
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill_one = kill_new();
	kill_two = kill_new();

	EVRPC_MAKE_REQUEST(Message, pool, msg, kill_one,  GotKillCbTwo, NULL);
	EVRPC_MAKE_REQUEST(Message, pool, msg, kill_two,  GotKillCb, NULL);

	test_ok = 0;

	event_dispatch();

	rpc_teardown(base);

	tt_assert(test_ok == 2);

end:
	if (msg)
		msg_free(msg);
	if (kill_one)
		kill_free(kill_one);
	if (kill_two)
		kill_free(kill_two);

	if (pool)
		evrpc_pool_free(pool);
	if (http)
		evhttp_free(http);
}

static void
GotErrorCb(struct evrpc_status *status,
    struct msg *msg, struct kill *kill, void *arg)
{
	if (status->error != EVRPC_STATUS_ERR_TIMEOUT)
		goto done;

	/* should never be complete but just to check */
	if (kill_complete(kill) == 0)
		goto done;

	test_ok += 1;

done:
	event_loopexit(NULL);
}

/* we just pause the rpc and continue it in the next callback */

struct rpc_hook_ctx_ {
	void *vbase;
	void *ctx;
};

static int hook_pause_cb_called=0;

static void
rpc_hook_pause_cb(evutil_socket_t fd, short what, void *arg)
{
	struct rpc_hook_ctx_ *ctx = arg;
	++hook_pause_cb_called;
	evrpc_resume_request(ctx->vbase, ctx->ctx, EVRPC_CONTINUE);
	free(arg);
}

static int
rpc_hook_pause(void *ctx, struct evhttp_request *req, struct evbuffer *evbuf,
    void *arg)
{
	struct rpc_hook_ctx_ *tmp = malloc(sizeof(*tmp));
	struct timeval tv;

	assert(tmp != NULL);
	tmp->vbase = arg;
	tmp->ctx = ctx;

	memset(&tv, 0, sizeof(tv));
	event_once(-1, EV_TIMEOUT, rpc_hook_pause_cb, tmp, &tv);
	return EVRPC_PAUSE;
}

static void
rpc_basic_client_with_pause(void)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg = NULL;
	struct kill *kill= NULL;

	rpc_setup(&http, &port, &base);

	assert(evrpc_add_hook(base, EVRPC_INPUT, rpc_hook_pause, base));
	assert(evrpc_add_hook(base, EVRPC_OUTPUT, rpc_hook_pause, base));

	pool = rpc_pool_with_connection(port);
	tt_assert(pool);
	assert(evrpc_add_hook(pool, EVRPC_INPUT, rpc_hook_pause, pool));
	assert(evrpc_add_hook(pool, EVRPC_OUTPUT, rpc_hook_pause, pool));

	/* set up the basic message */
	msg = msg_new();
	tt_assert(msg);
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill = kill_new();

	EVRPC_MAKE_REQUEST(Message, pool, msg, kill, GotKillCb, NULL);

	test_ok = 0;

	event_dispatch();

	tt_int_op(test_ok, ==, 1);
	tt_int_op(hook_pause_cb_called, ==, 4);

end:
	if (base)
		rpc_teardown(base);

	if (msg)
		msg_free(msg);
	if (kill)
		kill_free(kill);

	if (pool)
		evrpc_pool_free(pool);
	if (http)
		evhttp_free(http);
}

static void
rpc_client_timeout(void)
{
	ev_uint16_t port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg = NULL;
	struct kill *kill = NULL;

	rpc_setup(&http, &port, &base);

	pool = rpc_pool_with_connection(port);
	tt_assert(pool);

	/* set the timeout to 1 second. */
	evrpc_pool_set_timeout(pool, 1);

	/* set up the basic message */
	msg = msg_new();
	tt_assert(msg);
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill = kill_new();

	EVRPC_MAKE_REQUEST(NeverReply, pool, msg, kill, GotErrorCb, NULL);

	test_ok = 0;

	event_dispatch();

	/* free the saved RPC structure up */
	EVRPC_REQUEST_DONE(saved_rpc);

	rpc_teardown(base);

	tt_assert(test_ok == 2);

end:
	if (msg)
		msg_free(msg);
	if (kill)
		kill_free(kill);

	if (pool)
		evrpc_pool_free(pool);
	if (http)
		evhttp_free(http);
}

static void
rpc_test(void)
{
	struct msg *msg = NULL, *msg2 = NULL;
	struct kill *attack = NULL;
	struct run *run = NULL;
	struct evbuffer *tmp = evbuffer_new();
	struct timeval tv_start, tv_end;
	ev_uint32_t tag;
	int i;

	msg = msg_new();

	tt_assert(msg);

	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "phoenix");

	if (EVTAG_GET(msg, attack, &attack) == -1) {
		tt_abort_msg("Failed to set kill message.");
	}

	EVTAG_ASSIGN(attack, weapon, "feather");
	EVTAG_ASSIGN(attack, action, "tickle");
	for (i = 0; i < 3; ++i) {
		if (EVTAG_ARRAY_ADD_VALUE(attack, how_often, i) == NULL) {
			tt_abort_msg("Failed to add how_often.");
		}
	}

	evutil_gettimeofday(&tv_start, NULL);
	for (i = 0; i < 1000; ++i) {
		run = EVTAG_ARRAY_ADD(msg, run);
		if (run == NULL) {
			tt_abort_msg("Failed to add run message.");
		}
		EVTAG_ASSIGN(run, how, "very fast but with some data in it");
		EVTAG_ASSIGN(run, fixed_bytes,
		    (ev_uint8_t*)"012345678901234567890123");

		if (EVTAG_ARRAY_ADD_VALUE(
			    run, notes, "this is my note") == NULL) {
			tt_abort_msg("Failed to add note.");
		}
		if (EVTAG_ARRAY_ADD_VALUE(run, notes, "pps") == NULL) {
			tt_abort_msg("Failed to add note");
		}

		EVTAG_ASSIGN(run, large_number, 0xdead0a0bcafebeefLL);
		EVTAG_ARRAY_ADD_VALUE(run, other_numbers, 0xdead0a0b);
		EVTAG_ARRAY_ADD_VALUE(run, other_numbers, 0xbeefcafe);
	}

	if (msg_complete(msg) == -1)
		tt_abort_msg("Failed to make complete message.");

	evtag_marshal_msg(tmp, 0xdeaf, msg);

	if (evtag_peek(tmp, &tag) == -1)
		tt_abort_msg("Failed to peak tag.");

	if (tag != 0xdeaf)
		TT_DIE(("Got incorrect tag: %0x.", (unsigned)tag));

	msg2 = msg_new();
	if (evtag_unmarshal_msg(tmp, 0xdeaf, msg2) == -1)
		tt_abort_msg("Failed to unmarshal message.");

	evutil_gettimeofday(&tv_end, NULL);
	evutil_timersub(&tv_end, &tv_start, &tv_end);
	TT_BLATHER(("(%.1f us/add) ",
		(float)tv_end.tv_sec/(float)i * 1000000.0 +
		tv_end.tv_usec / (float)i));

	if (!EVTAG_HAS(msg2, from_name) ||
	    !EVTAG_HAS(msg2, to_name) ||
	    !EVTAG_HAS(msg2, attack)) {
		tt_abort_msg("Missing data structures.");
	}

	if (EVTAG_GET(msg2, attack, &attack) == -1) {
		tt_abort_msg("Could not get attack.");
	}

	if (EVTAG_ARRAY_LEN(msg2, run) != i) {
		tt_abort_msg("Wrong number of run messages.");
	}

	/* get the very first run message */
	if (EVTAG_ARRAY_GET(msg2, run, 0, &run) == -1) {
		tt_abort_msg("Failed to get run msg.");
	} else {
		/* verify the notes */
		char *note_one, *note_two;
		ev_uint64_t large_number;
		ev_uint32_t short_number;

		if (EVTAG_ARRAY_LEN(run, notes) != 2) {
			tt_abort_msg("Wrong number of note strings.");
		}

		if (EVTAG_ARRAY_GET(run, notes, 0, &note_one) == -1 ||
		    EVTAG_ARRAY_GET(run, notes, 1, &note_two) == -1) {
			tt_abort_msg("Could not get note strings.");
		}

		if (strcmp(note_one, "this is my note") ||
		    strcmp(note_two, "pps")) {
			tt_abort_msg("Incorrect note strings encoded.");
		}

		if (EVTAG_GET(run, large_number, &large_number) == -1 ||
		    large_number != 0xdead0a0bcafebeefLL) {
			tt_abort_msg("Incorrrect large_number.");
		}

		if (EVTAG_ARRAY_LEN(run, other_numbers) != 2) {
			tt_abort_msg("Wrong number of other_numbers.");
		}

		if (EVTAG_ARRAY_GET(
			    run, other_numbers, 0, &short_number) == -1) {
			tt_abort_msg("Could not get short number.");
		}
		tt_uint_op(short_number, ==, 0xdead0a0b);

	}
	tt_int_op(EVTAG_ARRAY_LEN(attack, how_often), ==, 3);

	for (i = 0; i < 3; ++i) {
		ev_uint32_t res;
		if (EVTAG_ARRAY_GET(attack, how_often, i, &res) == -1) {
			TT_DIE(("Cannot get %dth how_often msg.", i));
		}
		if ((int)res != i) {
			TT_DIE(("Wrong message encoded %d != %d", i, res));
		}
	}

	test_ok = 1;
end:
	if (msg)
		msg_free(msg);
	if (msg2)
		msg_free(msg2);
	if (tmp)
		evbuffer_free(tmp);
}

#define RPC_LEGACY(name)						\
	{ #name, run_legacy_test_fn, TT_FORK|TT_NEED_BASE|TT_LEGACY,	\
		    &legacy_setup,					\
		    rpc_##name }
#else
/* NO_PYTHON_EXISTS */

#define RPC_LEGACY(name) \
	{ #name, NULL, TT_SKIP, NULL, NULL }

#endif

struct testcase_t rpc_testcases[] = {
	RPC_LEGACY(basic_test),
	RPC_LEGACY(basic_message),
	RPC_LEGACY(basic_client),
	RPC_LEGACY(basic_queued_client),
	RPC_LEGACY(basic_client_with_pause),
	RPC_LEGACY(client_timeout),
	RPC_LEGACY(test),

	END_OF_TESTCASES,
};
