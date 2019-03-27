/*
 * cachedb/redis.c - cachedb redis module
 *
 * Copyright (c) 2018, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains a module that uses the redis database to cache
 * dns responses.
 */

#include "config.h"
#ifdef USE_CACHEDB
#include "cachedb/redis.h"
#include "cachedb/cachedb.h"
#include "util/alloc.h"
#include "util/config_file.h"
#include "sldns/sbuffer.h"

#ifdef USE_REDIS
#include "hiredis/hiredis.h"

struct redis_moddata {
	redisContext** ctxs;	/* thread-specific redis contexts */
	int numctxs;		/* number of ctx entries */
	const char* server_host; /* server's IP address or host name */
	int server_port;	 /* server's TCP port */
	struct timeval timeout;	 /* timeout for connection setup and commands */
};

static redisContext*
redis_connect(const struct redis_moddata* moddata)
{
	redisContext* ctx;

	ctx = redisConnectWithTimeout(moddata->server_host,
		moddata->server_port, moddata->timeout);
	if(!ctx || ctx->err) {
		const char *errstr = "out of memory";
		if(ctx)
			errstr = ctx->errstr;
		log_err("failed to connect to redis server: %s", errstr);
		goto fail;
	}
	if(redisSetTimeout(ctx, moddata->timeout) != REDIS_OK) {
		log_err("failed to set redis timeout");
		goto fail;
	}
	return ctx;

  fail:
	if(ctx)
		redisFree(ctx);
	return NULL;
}

static int
redis_init(struct module_env* env, struct cachedb_env* cachedb_env)
{
	int i;
	struct redis_moddata* moddata = NULL;

	verbose(VERB_ALGO, "redis_init");

	moddata = calloc(1, sizeof(struct redis_moddata));
	if(!moddata) {
		log_err("out of memory");
		return 0;
	}
	moddata->numctxs = env->cfg->num_threads;
	moddata->ctxs = calloc(env->cfg->num_threads, sizeof(redisContext*));
	if(!moddata->ctxs) {
		log_err("out of memory");
		free(moddata);
		return 0;
	}
	/* note: server_host is a shallow reference to configured string.
	 * we don't have to free it in this module. */
	moddata->server_host = env->cfg->redis_server_host;
	moddata->server_port = env->cfg->redis_server_port;
	moddata->timeout.tv_sec = env->cfg->redis_timeout / 1000;
	moddata->timeout.tv_usec = (env->cfg->redis_timeout % 1000) * 1000;
	for(i = 0; i < moddata->numctxs; i++)
		moddata->ctxs[i] = redis_connect(moddata);
	cachedb_env->backend_data = moddata;
	return 1;
}

static void
redis_deinit(struct module_env* env, struct cachedb_env* cachedb_env)
{
	struct redis_moddata* moddata = (struct redis_moddata*)
		cachedb_env->backend_data;
	(void)env;

	verbose(VERB_ALGO, "redis_deinit");

	if(!moddata)
		return;
	if(moddata->ctxs) {
		int i;
		for(i = 0; i < moddata->numctxs; i++) {
			if(moddata->ctxs[i])
				redisFree(moddata->ctxs[i]);
		}
		free(moddata->ctxs);
	}
	free(moddata);
}

/*
 * Send a redis command and get a reply.  Unified so that it can be used for
 * both SET and GET.  If 'data' is non-NULL the command is supposed to be
 * SET and GET otherwise, but the implementation of this function is agnostic
 * about the semantics (except for logging): 'command', 'data', and 'data_len'
 * are opaquely passed to redisCommand().
 * This function first checks whether a connection with a redis server has
 * been established; if not it tries to set up a new one.
 * It returns redisReply returned from redisCommand() or NULL if some low
 * level error happens.  The caller is responsible to check the return value,
 * if it's non-NULL, it has to free it with freeReplyObject().
 */
static redisReply*
redis_command(struct module_env* env, struct cachedb_env* cachedb_env,
	const char* command, const uint8_t* data, size_t data_len)
{
	redisContext* ctx;
	redisReply* rep;
	struct redis_moddata* d = (struct redis_moddata*)
		cachedb_env->backend_data;

	/* We assume env->alloc->thread_num is a unique ID for each thread
	 * in [0, num-of-threads).  We could treat it as an error condition
	 * if the assumption didn't hold, but it seems to be a fundamental
	 * assumption throughout the unbound architecture, so we simply assert
	 * it. */
	log_assert(env->alloc->thread_num < d->numctxs);
	ctx = d->ctxs[env->alloc->thread_num];

	/* If we've not established a connection to the server or we've closed
	 * it on a failure, try to re-establish a new one.   Failures will be
	 * logged in redis_connect(). */
	if(!ctx) {
		ctx = redis_connect(d);
		d->ctxs[env->alloc->thread_num] = ctx;
	}
	if(!ctx)
		return NULL;

	/* Send the command and get a reply, synchronously. */
	rep = (redisReply*)redisCommand(ctx, command, data, data_len);
	if(!rep) {
		/* Once an error as a NULL-reply is returned the context cannot
		 * be reused and we'll need to set up a new connection. */
		log_err("redis_command: failed to receive a reply, "
			"closing connection: %s", ctx->errstr);
		redisFree(ctx);
		d->ctxs[env->alloc->thread_num] = NULL;
		return NULL;
	}

	/* Check error in reply to unify logging in that case.
	 * The caller may perform context-dependent checks and logging. */
	if(rep->type == REDIS_REPLY_ERROR)
		log_err("redis: %s resulted in an error: %s",
			data ? "set" : "get", rep->str);

	return rep;
}

static int
redis_lookup(struct module_env* env, struct cachedb_env* cachedb_env,
	char* key, struct sldns_buffer* result_buffer)
{
	redisReply* rep;
	char cmdbuf[4+(CACHEDB_HASHSIZE/8)*2+1]; /* "GET " + key */
	int n;
	int ret = 0;

	verbose(VERB_ALGO, "redis_lookup of %s", key);

	n = snprintf(cmdbuf, sizeof(cmdbuf), "GET %s", key);
	if(n < 0 || n >= (int)sizeof(cmdbuf)) {
		log_err("redis_lookup: unexpected failure to build command");
		return 0;
	}

	rep = redis_command(env, cachedb_env, cmdbuf, NULL, 0);
	if(!rep)
		return 0;
	switch (rep->type) {
	case REDIS_REPLY_NIL:
		verbose(VERB_ALGO, "redis_lookup: no data cached");
		break;
	case REDIS_REPLY_STRING:
		verbose(VERB_ALGO, "redis_lookup found %d bytes",
			(int)rep->len);
		if((size_t)rep->len > sldns_buffer_capacity(result_buffer)) {
			log_err("redis_lookup: replied data too long: %lu",
				(size_t)rep->len);
			break;
		}
		sldns_buffer_clear(result_buffer);
		sldns_buffer_write(result_buffer, rep->str, rep->len);
		sldns_buffer_flip(result_buffer);
		ret = 1;
		break;
	case REDIS_REPLY_ERROR:
		break;		/* already logged */
	default:
		log_err("redis_lookup: unexpected type of reply for (%d)",
			rep->type);
		break;
	}
	freeReplyObject(rep);
	return ret;
}

static void
redis_store(struct module_env* env, struct cachedb_env* cachedb_env,
	char* key, uint8_t* data, size_t data_len)
{
	redisReply* rep;
	char cmdbuf[4+(CACHEDB_HASHSIZE/8)*2+3+1]; /* "SET " + key + " %b" */
	int n;

	verbose(VERB_ALGO, "redis_store %s (%d bytes)", key, (int)data_len);

	/* build command to set to a binary safe string */
	n = snprintf(cmdbuf, sizeof(cmdbuf), "SET %s %%b", key);
	if(n < 0 || n >= (int)sizeof(cmdbuf)) {
		log_err("redis_store: unexpected failure to build command");
		return;
	}

	rep = redis_command(env, cachedb_env, cmdbuf, data, data_len);
	if(rep) {
		verbose(VERB_ALGO, "redis_store set completed");
		if(rep->type != REDIS_REPLY_STATUS &&
			rep->type != REDIS_REPLY_ERROR) {
			log_err("redis_store: unexpected type of reply (%d)",
				rep->type);
		}
		freeReplyObject(rep);
	}
}

struct cachedb_backend redis_backend = { "redis",
	redis_init, redis_deinit, redis_lookup, redis_store
};
#endif	/* USE_REDIS */
#endif /* USE_CACHEDB */
