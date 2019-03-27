/*
 * daemon/remote.h - remote control for the unbound daemon.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 * This file contains the remote control functionality for the daemon.
 * The remote control can be performed using either the commandline
 * unbound-control tool, or a SSLv3/TLS capable web browser. 
 * The channel is secured using SSLv3 or TLSv1, and certificates.
 * Both the server and the client(control tool) have their own keys.
 */

#ifndef DAEMON_REMOTE_H
#define DAEMON_REMOTE_H
#ifdef HAVE_OPENSSL_SSL_H
#include "openssl/ssl.h"
#endif
struct config_file;
struct listen_list;
struct listen_port;
struct worker;
struct comm_reply;
struct comm_point;
struct daemon_remote;

/** number of milliseconds timeout on incoming remote control handshake */
#define REMOTE_CONTROL_TCP_TIMEOUT 120000

/**
 * a busy control command connection, SSL state
 */
struct rc_state {
	/** the next item in list */
	struct rc_state* next;
	/** the commpoint */
	struct comm_point* c;
	/** in the handshake part */
	enum { rc_none, rc_hs_read, rc_hs_write } shake_state;
#ifdef HAVE_SSL
	/** the ssl state */
	SSL* ssl;
#endif
	/** file descriptor */
        int fd;
	/** the rc this is part of */
	struct daemon_remote* rc;
};

/**
 * The remote control tool state.
 * The state is only created for the first thread, other threads
 * are called from this thread.  Only the first threads listens to
 * the control port.  The other threads do not, but are called on the
 * command channel(pipe) from the first thread.
 */
struct daemon_remote {
	/** the worker for this remote control */
	struct worker* worker;
	/** commpoints for accepting remote control connections */
	struct listen_list* accept_list;
	/* if certificates are used */
	int use_cert;
	/** number of active commpoints that are handling remote control */
	int active;
	/** max active commpoints */
	int max_active;
	/** current commpoints busy; should be a short list, malloced */
	struct rc_state* busy_list;
#ifdef HAVE_SSL
	/** the SSL context for creating new SSL streams */
	SSL_CTX* ctx;
#endif
};

/**
 * Connection to print to, either SSL or plain over fd
 */
struct remote_stream {
#ifdef HAVE_SSL
	/** SSL structure, nonNULL if using SSL */
	SSL* ssl;
#endif
	/** file descriptor for plain transfer */
	int fd;
};
typedef struct remote_stream RES;

/**
 * Create new remote control state for the daemon.
 * @param cfg: config file with key file settings.
 * @return new state, or NULL on failure.
 */
struct daemon_remote* daemon_remote_create(struct config_file* cfg);

/**
 * remote control state to delete.
 * @param rc: state to delete.
 */
void daemon_remote_delete(struct daemon_remote* rc);

/**
 * remote control state to clear up. Busy and accept points are closed.
 * Does not delete the rc itself, or the ssl context (with its keys).
 * @param rc: state to clear.
 */
void daemon_remote_clear(struct daemon_remote* rc);

/**
 * Open and create listening ports for remote control.
 * @param cfg: config options.
 * @return list of ports or NULL on failure.
 *	can be freed with listening_ports_free().
 */
struct listen_port* daemon_remote_open_ports(struct config_file* cfg);

/**
 * Setup comm points for accepting remote control connections.
 * @param rc: state
 * @param ports: already opened ports.
 * @param worker: worker with communication base. and links to command channels.
 * @return false on error.
 */
int daemon_remote_open_accept(struct daemon_remote* rc, 
	struct listen_port* ports, struct worker* worker);

/**
 * Stop accept handlers for TCP (until enabled again)
 * @param rc: state
 */
void daemon_remote_stop_accept(struct daemon_remote* rc);

/**
 * Stop accept handlers for TCP (until enabled again)
 * @param rc: state
 */
void daemon_remote_start_accept(struct daemon_remote* rc);

/**
 * Handle nonthreaded remote cmd execution.
 * @param worker: this worker (the remote worker).
 */
void daemon_remote_exec(struct worker* worker);

#ifdef HAVE_SSL
/** 
 * Print fixed line of text over ssl connection in blocking mode
 * @param ssl: print to
 * @param text: the text.
 * @return false on connection failure.
 */
int ssl_print_text(RES* ssl, const char* text);

/** 
 * printf style printing to the ssl connection
 * @param ssl: the RES connection to print to. Blocking.
 * @param format: printf style format string.
 * @return success or false on a network failure.
 */
int ssl_printf(RES* ssl, const char* format, ...)
        ATTR_FORMAT(printf, 2, 3);

/**
 * Read until \n is encountered
 * If stream signals EOF, the string up to then is returned (without \n).
 * @param ssl: the RES connection to read from. blocking.
 * @param buf: buffer to read to.
 * @param max: size of buffer.
 * @return false on connection failure.
 */
int ssl_read_line(RES* ssl, char* buf, size_t max);
#endif /* HAVE_SSL */

#endif /* DAEMON_REMOTE_H */
