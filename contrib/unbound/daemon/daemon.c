/*
 * daemon/daemon.c - collection of workers that handles requests.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * The daemon consists of global settings and a number of workers.
 */

#include "config.h"
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#ifdef HAVE_OPENSSL_CONF_H
#include <openssl/conf.h>
#endif

#ifdef HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>

#ifdef HAVE_NSS
/* nss3 */
#include "nss.h"
#endif

#include "daemon/daemon.h"
#include "daemon/worker.h"
#include "daemon/remote.h"
#include "daemon/acl_list.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/data/msgreply.h"
#include "util/shm_side/shm_main.h"
#include "util/storage/lookup3.h"
#include "util/storage/slabhash.h"
#include "util/tcp_conn_limit.h"
#include "services/listen_dnsport.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "services/localzone.h"
#include "services/view.h"
#include "services/modstack.h"
#include "services/authzone.h"
#include "util/module.h"
#include "util/random.h"
#include "util/tube.h"
#include "util/net_help.h"
#include "sldns/keyraw.h"
#include "respip/respip.h"
#include <signal.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

/** How many quit requests happened. */
static int sig_record_quit = 0;
/** How many reload requests happened. */
static int sig_record_reload = 0;

#if HAVE_DECL_SSL_COMP_GET_COMPRESSION_METHODS
/** cleaner ssl memory freeup */
static void* comp_meth = NULL;
#endif
/** remove buffers for parsing and init */
int ub_c_lex_destroy(void);

/** used when no other sighandling happens, so we don't die
  * when multiple signals in quick succession are sent to us. 
  * @param sig: signal number.
  * @return signal handler return type (void or int).
  */
static RETSIGTYPE record_sigh(int sig)
{
#ifdef LIBEVENT_SIGNAL_PROBLEM
	/* cannot log, verbose here because locks may be held */
	/* quit on signal, no cleanup and statistics, 
	   because installed libevent version is not threadsafe */
	exit(0);
#endif 
	switch(sig)
	{
		case SIGTERM:
#ifdef SIGQUIT
		case SIGQUIT:
#endif
#ifdef SIGBREAK
		case SIGBREAK:
#endif
		case SIGINT:
			sig_record_quit++;
			break;
#ifdef SIGHUP
		case SIGHUP:
			sig_record_reload++;
			break;
#endif
#ifdef SIGPIPE
		case SIGPIPE:
			break;
#endif
		default:
			/* ignoring signal */
			break;
	}
}

/** 
 * Signal handling during the time when netevent is disabled.
 * Stores signals to replay later.
 */
static void
signal_handling_record(void)
{
	if( signal(SIGTERM, record_sigh) == SIG_ERR ||
#ifdef SIGQUIT
		signal(SIGQUIT, record_sigh) == SIG_ERR ||
#endif
#ifdef SIGBREAK
		signal(SIGBREAK, record_sigh) == SIG_ERR ||
#endif
#ifdef SIGHUP
		signal(SIGHUP, record_sigh) == SIG_ERR ||
#endif
#ifdef SIGPIPE
		signal(SIGPIPE, SIG_IGN) == SIG_ERR ||
#endif
		signal(SIGINT, record_sigh) == SIG_ERR
		)
		log_err("install sighandler: %s", strerror(errno));
}

/**
 * Replay old signals.
 * @param wrk: worker that handles signals.
 */
static void
signal_handling_playback(struct worker* wrk)
{
#ifdef SIGHUP
	if(sig_record_reload)
		worker_sighandler(SIGHUP, wrk);
#endif
	if(sig_record_quit)
		worker_sighandler(SIGTERM, wrk);
	sig_record_quit = 0;
	sig_record_reload = 0;
}

struct daemon* 
daemon_init(void)
{
	struct daemon* daemon = (struct daemon*)calloc(1, 
		sizeof(struct daemon));
#ifdef USE_WINSOCK
	int r;
	WSADATA wsa_data;
#endif
	if(!daemon)
		return NULL;
#ifdef USE_WINSOCK
	r = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if(r != 0) {
		fatal_exit("could not init winsock. WSAStartup: %s",
			wsa_strerror(r));
	}
#endif /* USE_WINSOCK */
	signal_handling_record();
	checklock_start();
#ifdef HAVE_SSL
#  ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#  endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	ERR_load_SSL_strings();
#endif
#  ifdef USE_GOST
	(void)sldns_key_EVP_load_gost_id();
#  endif
#  if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
	OpenSSL_add_all_algorithms();
#  else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
		| OPENSSL_INIT_ADD_ALL_DIGESTS
		| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#  endif
#  if HAVE_DECL_SSL_COMP_GET_COMPRESSION_METHODS
	/* grab the COMP method ptr because openssl leaks it */
	comp_meth = (void*)SSL_COMP_get_compression_methods();
#  endif
#  if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	(void)SSL_library_init();
#  else
	(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#  endif
#  if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED)
	if(!ub_openssl_lock_init())
		fatal_exit("could not init openssl locks");
#  endif
#elif defined(HAVE_NSS)
	if(NSS_NoDB_Init(NULL) != SECSuccess)
		fatal_exit("could not init NSS");
#endif /* HAVE_SSL or HAVE_NSS */
#ifdef HAVE_TZSET
	/* init timezone info while we are not chrooted yet */
	tzset();
#endif
	/* open /dev/random if needed */
	ub_systemseed((unsigned)time(NULL)^(unsigned)getpid()^0xe67);
	daemon->need_to_exit = 0;
	modstack_init(&daemon->mods);
	if(!(daemon->env = (struct module_env*)calloc(1, 
		sizeof(*daemon->env)))) {
		free(daemon);
		return NULL;
	}
	/* init edns_known_options */
	if(!edns_known_options_init(daemon->env)) {
		free(daemon->env);
		free(daemon);
		return NULL;
	}
	alloc_init(&daemon->superalloc, NULL, 0);
	daemon->acl = acl_list_create();
	if(!daemon->acl) {
		edns_known_options_delete(daemon->env);
		free(daemon->env);
		free(daemon);
		return NULL;
	}
	daemon->tcl = tcl_list_create();
	if(!daemon->tcl) {
		acl_list_delete(daemon->acl);
		edns_known_options_delete(daemon->env);
		free(daemon->env);
		free(daemon);
		return NULL;
	}
	if(gettimeofday(&daemon->time_boot, NULL) < 0)
		log_err("gettimeofday: %s", strerror(errno));
	daemon->time_last_stat = daemon->time_boot;
	if((daemon->env->auth_zones = auth_zones_create()) == 0) {
		acl_list_delete(daemon->acl);
		tcl_list_delete(daemon->tcl);
		edns_known_options_delete(daemon->env);
		free(daemon->env);
		free(daemon);
		return NULL;
	}
	return daemon;	
}

int 
daemon_open_shared_ports(struct daemon* daemon)
{
	log_assert(daemon);
	if(daemon->cfg->port != daemon->listening_port) {
		size_t i;
		struct listen_port* p0;
		daemon->reuseport = 0;
		/* free and close old ports */
		if(daemon->ports != NULL) {
			for(i=0; i<daemon->num_ports; i++)
				listening_ports_free(daemon->ports[i]);
			free(daemon->ports);
			daemon->ports = NULL;
		}
		/* see if we want to reuseport */
#ifdef SO_REUSEPORT
		if(daemon->cfg->so_reuseport && daemon->cfg->num_threads > 0)
			daemon->reuseport = 1;
#endif
		/* try to use reuseport */
		p0 = listening_ports_open(daemon->cfg, &daemon->reuseport);
		if(!p0) {
			listening_ports_free(p0);
			return 0;
		}
		if(daemon->reuseport) {
			/* reuseport was successful, allocate for it */
			daemon->num_ports = (size_t)daemon->cfg->num_threads;
		} else {
			/* do the normal, singleportslist thing,
			 * reuseport not enabled or did not work */
			daemon->num_ports = 1;
		}
		if(!(daemon->ports = (struct listen_port**)calloc(
			daemon->num_ports, sizeof(*daemon->ports)))) {
			listening_ports_free(p0);
			return 0;
		}
		daemon->ports[0] = p0;
		if(daemon->reuseport) {
			/* continue to use reuseport */
			for(i=1; i<daemon->num_ports; i++) {
				if(!(daemon->ports[i]=
					listening_ports_open(daemon->cfg,
						&daemon->reuseport))
					|| !daemon->reuseport ) {
					for(i=0; i<daemon->num_ports; i++)
						listening_ports_free(daemon->ports[i]);
					free(daemon->ports);
					daemon->ports = NULL;
					return 0;
				}
			}
		}
		daemon->listening_port = daemon->cfg->port;
	}
	if(!daemon->cfg->remote_control_enable && daemon->rc_port) {
		listening_ports_free(daemon->rc_ports);
		daemon->rc_ports = NULL;
		daemon->rc_port = 0;
	}
	if(daemon->cfg->remote_control_enable && 
		daemon->cfg->control_port != daemon->rc_port) {
		listening_ports_free(daemon->rc_ports);
		if(!(daemon->rc_ports=daemon_remote_open_ports(daemon->cfg)))
			return 0;
		daemon->rc_port = daemon->cfg->control_port;
	}
	return 1;
}

/**
 * Setup modules. setup module stack.
 * @param daemon: the daemon
 */
static void daemon_setup_modules(struct daemon* daemon)
{
	daemon->env->cfg = daemon->cfg;
	daemon->env->alloc = &daemon->superalloc;
	daemon->env->worker = NULL;
	daemon->env->need_to_validate = 0; /* set by module init below */
	if(!modstack_setup(&daemon->mods, daemon->cfg->module_conf, 
		daemon->env)) {
		fatal_exit("failed to setup modules");
	}
	log_edns_known_options(VERB_ALGO, daemon->env);
}

/**
 * Obtain allowed port numbers, concatenate the list, and shuffle them
 * (ready to be handed out to threads).
 * @param daemon: the daemon. Uses rand and cfg.
 * @param shufport: the portlist output.
 * @return number of ports available.
 */
static int daemon_get_shufport(struct daemon* daemon, int* shufport)
{
	int i, n, k, temp;
	int avail = 0;
	for(i=0; i<65536; i++) {
		if(daemon->cfg->outgoing_avail_ports[i]) {
			shufport[avail++] = daemon->cfg->
				outgoing_avail_ports[i];
		}
	}
	if(avail == 0)
		fatal_exit("no ports are permitted for UDP, add "
			"with outgoing-port-permit");
        /* Knuth shuffle */
	n = avail;
	while(--n > 0) {
		k = ub_random_max(daemon->rand, n+1); /* 0<= k<= n */
		temp = shufport[k];
		shufport[k] = shufport[n];
		shufport[n] = temp;
	}
	return avail;
}

/**
 * Allocate empty worker structures. With backptr and thread-number,
 * from 0..numthread initialised. Used as user arguments to new threads.
 * Creates the daemon random generator if it does not exist yet.
 * The random generator stays existing between reloads with a unique state.
 * @param daemon: the daemon with (new) config settings.
 */
static void 
daemon_create_workers(struct daemon* daemon)
{
	int i, numport;
	int* shufport;
	log_assert(daemon && daemon->cfg);
	if(!daemon->rand) {
		unsigned int seed = (unsigned int)time(NULL) ^ 
			(unsigned int)getpid() ^ 0x438;
		daemon->rand = ub_initstate(seed, NULL);
		if(!daemon->rand)
			fatal_exit("could not init random generator");
		hash_set_raninit((uint32_t)ub_random(daemon->rand));
	}
	shufport = (int*)calloc(65536, sizeof(int));
	if(!shufport)
		fatal_exit("out of memory during daemon init");
	numport = daemon_get_shufport(daemon, shufport);
	verbose(VERB_ALGO, "total of %d outgoing ports available", numport);
	
	daemon->num = (daemon->cfg->num_threads?daemon->cfg->num_threads:1);
	if(daemon->reuseport && (int)daemon->num < (int)daemon->num_ports) {
		log_warn("cannot reduce num-threads to %d because so-reuseport "
			"so continuing with %d threads.", (int)daemon->num,
			(int)daemon->num_ports);
		daemon->num = (int)daemon->num_ports;
	}
	daemon->workers = (struct worker**)calloc((size_t)daemon->num, 
		sizeof(struct worker*));
	if(!daemon->workers)
		fatal_exit("out of memory during daemon init");
	if(daemon->cfg->dnstap) {
#ifdef USE_DNSTAP
		daemon->dtenv = dt_create(daemon->cfg->dnstap_socket_path,
			(unsigned int)daemon->num);
		if (!daemon->dtenv)
			fatal_exit("dt_create failed");
		dt_apply_cfg(daemon->dtenv, daemon->cfg);
#else
		fatal_exit("dnstap enabled in config but not built with dnstap support");
#endif
	}
	for(i=0; i<daemon->num; i++) {
		if(!(daemon->workers[i] = worker_create(daemon, i,
			shufport+numport*i/daemon->num, 
			numport*(i+1)/daemon->num - numport*i/daemon->num)))
			/* the above is not ports/numthr, due to rounding */
			fatal_exit("could not create worker");
	}
	free(shufport);
}

#ifdef THREADS_DISABLED
/**
 * Close all pipes except for the numbered thread.
 * @param daemon: daemon to close pipes in.
 * @param thr: thread number 0..num-1 of thread to skip.
 */
static void close_other_pipes(struct daemon* daemon, int thr)
{
	int i;
	for(i=0; i<daemon->num; i++)
		if(i!=thr) {
			if(i==0) {
				/* only close read part, need to write stats */
				tube_close_read(daemon->workers[i]->cmd);
			} else {
				/* complete close channel to others */
				tube_delete(daemon->workers[i]->cmd);
				daemon->workers[i]->cmd = NULL;
			}
		}
}
#endif /* THREADS_DISABLED */

/**
 * Function to start one thread. 
 * @param arg: user argument.
 * @return: void* user return value could be used for thread_join results.
 */
static void* 
thread_start(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	int port_num = 0;
	log_thread_set(&worker->thread_num);
	ub_thread_blocksigs();
#ifdef THREADS_DISABLED
	/* close pipe ends used by main */
	tube_close_write(worker->cmd);
	close_other_pipes(worker->daemon, worker->thread_num);
#endif
#ifdef SO_REUSEPORT
	if(worker->daemon->cfg->so_reuseport)
		port_num = worker->thread_num % worker->daemon->num_ports;
	else
		port_num = 0;
#endif
	if(!worker_init(worker, worker->daemon->cfg,
			worker->daemon->ports[port_num], 0))
		fatal_exit("Could not initialize thread");

	worker_work(worker);
	return NULL;
}

/**
 * Fork and init the other threads. Main thread returns for special handling.
 * @param daemon: the daemon with other threads to fork.
 */
static void
daemon_start_others(struct daemon* daemon)
{
	int i;
	log_assert(daemon);
	verbose(VERB_ALGO, "start threads");
	/* skip i=0, is this thread */
	for(i=1; i<daemon->num; i++) {
		ub_thread_create(&daemon->workers[i]->thr_id,
			thread_start, daemon->workers[i]);
#ifdef THREADS_DISABLED
		/* close pipe end of child */
		tube_close_read(daemon->workers[i]->cmd);
#endif /* no threads */
	}
}

/**
 * Stop the other threads.
 * @param daemon: the daemon with other threads.
 */
static void
daemon_stop_others(struct daemon* daemon)
{
	int i;
	log_assert(daemon);
	verbose(VERB_ALGO, "stop threads");
	/* skip i=0, is this thread */
	/* use i=0 buffer for sending cmds; because we are #0 */
	for(i=1; i<daemon->num; i++) {
		worker_send_cmd(daemon->workers[i], worker_cmd_quit);
	}
	/* wait for them to quit */
	for(i=1; i<daemon->num; i++) {
		/* join it to make sure its dead */
		verbose(VERB_ALGO, "join %d", i);
		ub_thread_join(daemon->workers[i]->thr_id);
		verbose(VERB_ALGO, "join success %d", i);
	}
}

void 
daemon_fork(struct daemon* daemon)
{
	int have_view_respip_cfg = 0;

	log_assert(daemon);
	if(!(daemon->views = views_create()))
		fatal_exit("Could not create views: out of memory");
	/* create individual views and their localzone/data trees */
	if(!views_apply_cfg(daemon->views, daemon->cfg))
		fatal_exit("Could not set up views");

	if(!acl_list_apply_cfg(daemon->acl, daemon->cfg, daemon->views))
		fatal_exit("Could not setup access control list");
	if(!tcl_list_apply_cfg(daemon->tcl, daemon->cfg))
		fatal_exit("Could not setup TCP connection limits");
	if(daemon->cfg->dnscrypt) {
#ifdef USE_DNSCRYPT
		daemon->dnscenv = dnsc_create();
		if (!daemon->dnscenv)
			fatal_exit("dnsc_create failed");
		dnsc_apply_cfg(daemon->dnscenv, daemon->cfg);
#else
		fatal_exit("dnscrypt enabled in config but unbound was not built with "
				   "dnscrypt support");
#endif
	}
	/* create global local_zones */
	if(!(daemon->local_zones = local_zones_create()))
		fatal_exit("Could not create local zones: out of memory");
	if(!local_zones_apply_cfg(daemon->local_zones, daemon->cfg))
		fatal_exit("Could not set up local zones");

	/* process raw response-ip configuration data */
	if(!(daemon->respip_set = respip_set_create()))
		fatal_exit("Could not create response IP set");
	if(!respip_global_apply_cfg(daemon->respip_set, daemon->cfg))
		fatal_exit("Could not set up response IP set");
	if(!respip_views_apply_cfg(daemon->views, daemon->cfg,
		&have_view_respip_cfg))
		fatal_exit("Could not set up per-view response IP sets");
	daemon->use_response_ip = !respip_set_is_empty(daemon->respip_set) ||
		have_view_respip_cfg;
	
	/* read auth zonefiles */
	if(!auth_zones_apply_cfg(daemon->env->auth_zones, daemon->cfg, 1))
		fatal_exit("auth_zones could not be setup");

	/* setup modules */
	daemon_setup_modules(daemon);

	/* response-ip-xxx options don't work as expected without the respip
	 * module.  To avoid run-time operational surprise we reject such
	 * configuration. */
	if(daemon->use_response_ip &&
		modstack_find(&daemon->mods, "respip") < 0)
		fatal_exit("response-ip options require respip module");

	/* first create all the worker structures, so we can pass
	 * them to the newly created threads. 
	 */
	daemon_create_workers(daemon);

#if defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)
	/* in libev the first inited base gets signals */
	if(!worker_init(daemon->workers[0], daemon->cfg, daemon->ports[0], 1))
		fatal_exit("Could not initialize main thread");
#endif
	
	/* Now create the threads and init the workers.
	 * By the way, this is thread #0 (the main thread).
	 */
	daemon_start_others(daemon);

	/* Special handling for the main thread. This is the thread
	 * that handles signals and remote control.
	 */
#if !(defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP))
	/* libevent has the last inited base get signals (or any base) */
	if(!worker_init(daemon->workers[0], daemon->cfg, daemon->ports[0], 1))
		fatal_exit("Could not initialize main thread");
#endif
	signal_handling_playback(daemon->workers[0]);

	if (!shm_main_init(daemon))
		log_warn("SHM has failed");

	/* Start resolver service on main thread. */
#ifdef HAVE_SYSTEMD
	sd_notify(0, "READY=1");
#endif
	log_info("start of service (%s).", PACKAGE_STRING);
	worker_work(daemon->workers[0]);
#ifdef HAVE_SYSTEMD
	if (daemon->workers[0]->need_to_exit)
		sd_notify(0, "STOPPING=1");
	else
		sd_notify(0, "RELOADING=1");
#endif
	log_info("service stopped (%s).", PACKAGE_STRING);

	/* we exited! a signal happened! Stop other threads */
	daemon_stop_others(daemon);

	/* Shutdown SHM */
	shm_main_shutdown(daemon);

	daemon->need_to_exit = daemon->workers[0]->need_to_exit;
}

void 
daemon_cleanup(struct daemon* daemon)
{
	int i;
	log_assert(daemon);
	/* before stopping main worker, handle signals ourselves, so we
	   don't die on multiple reload signals for example. */
	signal_handling_record();
	log_thread_set(NULL);
	/* clean up caches because
	 * a) RRset IDs will be recycled after a reload, causing collisions
	 * b) validation config can change, thus rrset, msg, keycache clear */
	slabhash_clear(&daemon->env->rrset_cache->table);
	slabhash_clear(daemon->env->msg_cache);
	local_zones_delete(daemon->local_zones);
	daemon->local_zones = NULL;
	respip_set_delete(daemon->respip_set);
	daemon->respip_set = NULL;
	views_delete(daemon->views);
	daemon->views = NULL;
	if(daemon->env->auth_zones)
		auth_zones_cleanup(daemon->env->auth_zones);
	/* key cache is cleared by module desetup during next daemon_fork() */
	daemon_remote_clear(daemon->rc);
	for(i=0; i<daemon->num; i++)
		worker_delete(daemon->workers[i]);
	free(daemon->workers);
	daemon->workers = NULL;
	daemon->num = 0;
	alloc_clear_special(&daemon->superalloc);
#ifdef USE_DNSTAP
	dt_delete(daemon->dtenv);
	daemon->dtenv = NULL;
#endif
#ifdef USE_DNSCRYPT
	dnsc_delete(daemon->dnscenv);
	daemon->dnscenv = NULL;
#endif
	daemon->cfg = NULL;
}

void 
daemon_delete(struct daemon* daemon)
{
	size_t i;
	if(!daemon)
		return;
	modstack_desetup(&daemon->mods, daemon->env);
	daemon_remote_delete(daemon->rc);
	for(i = 0; i < daemon->num_ports; i++)
		listening_ports_free(daemon->ports[i]);
	free(daemon->ports);
	listening_ports_free(daemon->rc_ports);
	if(daemon->env) {
		slabhash_delete(daemon->env->msg_cache);
		rrset_cache_delete(daemon->env->rrset_cache);
		infra_delete(daemon->env->infra_cache);
		edns_known_options_delete(daemon->env);
		auth_zones_delete(daemon->env->auth_zones);
	}
	ub_randfree(daemon->rand);
	alloc_clear(&daemon->superalloc);
	acl_list_delete(daemon->acl);
	tcl_list_delete(daemon->tcl);
	free(daemon->chroot);
	free(daemon->pidfile);
	free(daemon->env);
#ifdef HAVE_SSL
	SSL_CTX_free((SSL_CTX*)daemon->listen_sslctx);
	SSL_CTX_free((SSL_CTX*)daemon->connect_sslctx);
#endif
	free(daemon);
	/* lex cleanup */
	ub_c_lex_destroy();
	/* libcrypto cleanup */
#ifdef HAVE_SSL
#  if defined(USE_GOST) && defined(HAVE_LDNS_KEY_EVP_UNLOAD_GOST)
	sldns_key_EVP_unload_gost();
#  endif
#  if HAVE_DECL_SSL_COMP_GET_COMPRESSION_METHODS && HAVE_DECL_SK_SSL_COMP_POP_FREE
#    ifndef S_SPLINT_S
#      if OPENSSL_VERSION_NUMBER < 0x10100000
	sk_SSL_COMP_pop_free(comp_meth, (void(*)())CRYPTO_free);
#      endif
#    endif
#  endif
#  ifdef HAVE_OPENSSL_CONFIG
	EVP_cleanup();
#  if OPENSSL_VERSION_NUMBER < 0x10100000
	ENGINE_cleanup();
#  endif
	CONF_modules_free();
#  endif
#  ifdef HAVE_CRYPTO_CLEANUP_ALL_EX_DATA
	CRYPTO_cleanup_all_ex_data(); /* safe, no more threads right now */
#  endif
#  ifdef HAVE_ERR_FREE_STRINGS
	ERR_free_strings();
#  endif
#  if OPENSSL_VERSION_NUMBER < 0x10100000
	RAND_cleanup();
#  endif
#  if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED)
	ub_openssl_lock_delete();
#  endif
#ifndef HAVE_ARC4RANDOM
	_ARC4_LOCK_DESTROY();
#endif
#elif defined(HAVE_NSS)
	NSS_Shutdown();
#endif /* HAVE_SSL or HAVE_NSS */
	checklock_stop();
#ifdef USE_WINSOCK
	if(WSACleanup() != 0) {
		log_err("Could not WSACleanup: %s", 
			wsa_strerror(WSAGetLastError()));
	}
#endif
}

void daemon_apply_cfg(struct daemon* daemon, struct config_file* cfg)
{
        daemon->cfg = cfg;
	config_apply(cfg);
	if(!slabhash_is_size(daemon->env->msg_cache, cfg->msg_cache_size,
	   	cfg->msg_cache_slabs)) {
		slabhash_delete(daemon->env->msg_cache);
		daemon->env->msg_cache = slabhash_create(cfg->msg_cache_slabs,
			HASH_DEFAULT_STARTARRAY, cfg->msg_cache_size,
			msgreply_sizefunc, query_info_compare,
			query_entry_delete, reply_info_delete, NULL);
		if(!daemon->env->msg_cache) {
			fatal_exit("malloc failure updating config settings");
		}
	}
	if((daemon->env->rrset_cache = rrset_cache_adjust(
		daemon->env->rrset_cache, cfg, &daemon->superalloc)) == 0)
		fatal_exit("malloc failure updating config settings");
	if((daemon->env->infra_cache = infra_adjust(daemon->env->infra_cache,
		cfg))==0)
		fatal_exit("malloc failure updating config settings");
}
