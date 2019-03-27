/* $OpenBSD: mux.c,v 1.75 2018/07/31 03:07:24 djm Exp $ */
/*
 * Copyright (c) 2002-2008 Damien Miller <djm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* ssh session multiplexing support */

/*
 * TODO:
 *   - Better signalling from master to slave, especially passing of
 *      error messages
 *   - Better fall-back from mux slave error to new connection.
 *   - ExitOnForwardingFailure
 *   - Maybe extension mechanisms for multi-X11/multi-agent forwarding
 *   - Support ~^Z in mux slaves.
 *   - Inspect or control sessions in master.
 *   - If we ever support the "signal" channel request, send signals on
 *     sessions in master.
 */

#include "includes.h"
__RCSID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifdef HAVE_POLL_H
#include <poll.h>
#else
# ifdef HAVE_SYS_POLL_H
#  include <sys/poll.h>
# endif
#endif

#ifdef HAVE_UTIL_H
# include <util.h>
#endif

#include "openbsd-compat/sys-queue.h"
#include "xmalloc.h"
#include "log.h"
#include "ssh.h"
#include "ssh2.h"
#include "pathnames.h"
#include "misc.h"
#include "match.h"
#include "sshbuf.h"
#include "channels.h"
#include "msg.h"
#include "packet.h"
#include "monitor_fdpass.h"
#include "sshpty.h"
#include "sshkey.h"
#include "readconf.h"
#include "clientloop.h"
#include "ssherr.h"

/* from ssh.c */
extern int tty_flag;
extern Options options;
extern int stdin_null_flag;
extern char *host;
extern int subsystem_flag;
extern struct sshbuf *command;
extern volatile sig_atomic_t quit_pending;

/* Context for session open confirmation callback */
struct mux_session_confirm_ctx {
	u_int want_tty;
	u_int want_subsys;
	u_int want_x_fwd;
	u_int want_agent_fwd;
	struct sshbuf *cmd;
	char *term;
	struct termios tio;
	char **env;
	u_int rid;
};

/* Context for stdio fwd open confirmation callback */
struct mux_stdio_confirm_ctx {
	u_int rid;
};

/* Context for global channel callback */
struct mux_channel_confirm_ctx {
	u_int cid;	/* channel id */
	u_int rid;	/* request id */
	int fid;	/* forward id */
};

/* fd to control socket */
int muxserver_sock = -1;

/* client request id */
u_int muxclient_request_id = 0;

/* Multiplexing control command */
u_int muxclient_command = 0;

/* Set when signalled. */
static volatile sig_atomic_t muxclient_terminate = 0;

/* PID of multiplex server */
static u_int muxserver_pid = 0;

static Channel *mux_listener_channel = NULL;

struct mux_master_state {
	int hello_rcvd;
};

/* mux protocol messages */
#define MUX_MSG_HELLO		0x00000001
#define MUX_C_NEW_SESSION	0x10000002
#define MUX_C_ALIVE_CHECK	0x10000004
#define MUX_C_TERMINATE		0x10000005
#define MUX_C_OPEN_FWD		0x10000006
#define MUX_C_CLOSE_FWD		0x10000007
#define MUX_C_NEW_STDIO_FWD	0x10000008
#define MUX_C_STOP_LISTENING	0x10000009
#define MUX_C_PROXY		0x1000000f
#define MUX_S_OK		0x80000001
#define MUX_S_PERMISSION_DENIED	0x80000002
#define MUX_S_FAILURE		0x80000003
#define MUX_S_EXIT_MESSAGE	0x80000004
#define MUX_S_ALIVE		0x80000005
#define MUX_S_SESSION_OPENED	0x80000006
#define MUX_S_REMOTE_PORT	0x80000007
#define MUX_S_TTY_ALLOC_FAIL	0x80000008
#define MUX_S_PROXY		0x8000000f

/* type codes for MUX_C_OPEN_FWD and MUX_C_CLOSE_FWD */
#define MUX_FWD_LOCAL   1
#define MUX_FWD_REMOTE  2
#define MUX_FWD_DYNAMIC 3

static void mux_session_confirm(struct ssh *, int, int, void *);
static void mux_stdio_confirm(struct ssh *, int, int, void *);

static int process_mux_master_hello(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_new_session(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_alive_check(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_terminate(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_open_fwd(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_close_fwd(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_stdio_fwd(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_stop_listening(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);
static int process_mux_proxy(struct ssh *, u_int,
	    Channel *, struct sshbuf *, struct sshbuf *);

static const struct {
	u_int type;
	int (*handler)(struct ssh *, u_int, Channel *,
	    struct sshbuf *, struct sshbuf *);
} mux_master_handlers[] = {
	{ MUX_MSG_HELLO, process_mux_master_hello },
	{ MUX_C_NEW_SESSION, process_mux_new_session },
	{ MUX_C_ALIVE_CHECK, process_mux_alive_check },
	{ MUX_C_TERMINATE, process_mux_terminate },
	{ MUX_C_OPEN_FWD, process_mux_open_fwd },
	{ MUX_C_CLOSE_FWD, process_mux_close_fwd },
	{ MUX_C_NEW_STDIO_FWD, process_mux_stdio_fwd },
	{ MUX_C_STOP_LISTENING, process_mux_stop_listening },
	{ MUX_C_PROXY, process_mux_proxy },
	{ 0, NULL }
};

/* Cleanup callback fired on closure of mux slave _session_ channel */
/* ARGSUSED */
static void
mux_master_session_cleanup_cb(struct ssh *ssh, int cid, void *unused)
{
	Channel *cc, *c = channel_by_id(ssh, cid);

	debug3("%s: entering for channel %d", __func__, cid);
	if (c == NULL)
		fatal("%s: channel_by_id(%i) == NULL", __func__, cid);
	if (c->ctl_chan != -1) {
		if ((cc = channel_by_id(ssh, c->ctl_chan)) == NULL)
			fatal("%s: channel %d missing control channel %d",
			    __func__, c->self, c->ctl_chan);
		c->ctl_chan = -1;
		cc->remote_id = 0;
		cc->have_remote_id = 0;
		chan_rcvd_oclose(ssh, cc);
	}
	channel_cancel_cleanup(ssh, c->self);
}

/* Cleanup callback fired on closure of mux slave _control_ channel */
/* ARGSUSED */
static void
mux_master_control_cleanup_cb(struct ssh *ssh, int cid, void *unused)
{
	Channel *sc, *c = channel_by_id(ssh, cid);

	debug3("%s: entering for channel %d", __func__, cid);
	if (c == NULL)
		fatal("%s: channel_by_id(%i) == NULL", __func__, cid);
	if (c->have_remote_id) {
		if ((sc = channel_by_id(ssh, c->remote_id)) == NULL)
			fatal("%s: channel %d missing session channel %u",
			    __func__, c->self, c->remote_id);
		c->remote_id = 0;
		c->have_remote_id = 0;
		sc->ctl_chan = -1;
		if (sc->type != SSH_CHANNEL_OPEN &&
		    sc->type != SSH_CHANNEL_OPENING) {
			debug2("%s: channel %d: not open", __func__, sc->self);
			chan_mark_dead(ssh, sc);
		} else {
			if (sc->istate == CHAN_INPUT_OPEN)
				chan_read_failed(ssh, sc);
			if (sc->ostate == CHAN_OUTPUT_OPEN)
				chan_write_failed(ssh, sc);
		}
	}
	channel_cancel_cleanup(ssh, c->self);
}

/* Check mux client environment variables before passing them to mux master. */
static int
env_permitted(char *env)
{
	int i, ret;
	char name[1024], *cp;

	if ((cp = strchr(env, '=')) == NULL || cp == env)
		return 0;
	ret = snprintf(name, sizeof(name), "%.*s", (int)(cp - env), env);
	if (ret <= 0 || (size_t)ret >= sizeof(name)) {
		error("env_permitted: name '%.100s...' too long", env);
		return 0;
	}

	for (i = 0; i < options.num_send_env; i++)
		if (match_pattern(name, options.send_env[i]))
			return 1;

	return 0;
}

/* Mux master protocol message handlers */

static int
process_mux_master_hello(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	u_int ver;
	struct mux_master_state *state = (struct mux_master_state *)c->mux_ctx;
	int r;

	if (state == NULL)
		fatal("%s: channel %d: c->mux_ctx == NULL", __func__, c->self);
	if (state->hello_rcvd) {
		error("%s: HELLO received twice", __func__);
		return -1;
	}
	if ((r = sshbuf_get_u32(m, &ver)) != 0) {
		error("%s: malformed message: %s", __func__, ssh_err(r));
		return -1;
	}
	if (ver != SSHMUX_VER) {
		error("Unsupported multiplexing protocol version %d "
		    "(expected %d)", ver, SSHMUX_VER);
		return -1;
	}
	debug2("%s: channel %d slave version %u", __func__, c->self, ver);

	/* No extensions are presently defined */
	while (sshbuf_len(m) > 0) {
		char *name = NULL;

		if ((r = sshbuf_get_cstring(m, &name, NULL)) != 0 ||
		    (r = sshbuf_skip_string(m)) != 0) { /* value */
			error("%s: malformed extension: %s",
			    __func__, ssh_err(r));
			return -1;
		}
		debug2("Unrecognised slave extension \"%s\"", name);
		free(name);
	}
	state->hello_rcvd = 1;
	return 0;
}

/* Enqueue a "ok" response to the reply buffer */
static void
reply_ok(struct sshbuf *reply, u_int rid)
{
	int r;

	if ((r = sshbuf_put_u32(reply, MUX_S_OK)) != 0 ||
	    (r = sshbuf_put_u32(reply, rid)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));
}

/* Enqueue an error response to the reply buffer */
static void
reply_error(struct sshbuf *reply, u_int type, u_int rid, const char *msg)
{
	int r;

	if ((r = sshbuf_put_u32(reply, type)) != 0 ||
	    (r = sshbuf_put_u32(reply, rid)) != 0 ||
	    (r = sshbuf_put_cstring(reply, msg)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));
}

static int
process_mux_new_session(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	Channel *nc;
	struct mux_session_confirm_ctx *cctx;
	char *cmd, *cp;
	u_int i, j, env_len, escape_char, window, packetmax;
	int r, new_fd[3];

	/* Reply for SSHMUX_COMMAND_OPEN */
	cctx = xcalloc(1, sizeof(*cctx));
	cctx->term = NULL;
	cctx->rid = rid;
	cmd = NULL;
	cctx->env = NULL;
	env_len = 0;
	if ((r = sshbuf_skip_string(m)) != 0 || /* reserved */
	    (r = sshbuf_get_u32(m, &cctx->want_tty)) != 0 ||
	    (r = sshbuf_get_u32(m, &cctx->want_x_fwd)) != 0 ||
	    (r = sshbuf_get_u32(m, &cctx->want_agent_fwd)) != 0 ||
	    (r = sshbuf_get_u32(m, &cctx->want_subsys)) != 0 ||
	    (r = sshbuf_get_u32(m, &escape_char)) != 0 ||
	    (r = sshbuf_get_cstring(m, &cctx->term, NULL)) != 0 ||
	    (r = sshbuf_get_cstring(m, &cmd, NULL)) != 0) {
 malf:
		free(cmd);
		for (j = 0; j < env_len; j++)
			free(cctx->env[j]);
		free(cctx->env);
		free(cctx->term);
		free(cctx);
		error("%s: malformed message", __func__);
		return -1;
	}

#define MUX_MAX_ENV_VARS	4096
	while (sshbuf_len(m) > 0) {
		if ((r = sshbuf_get_cstring(m, &cp, NULL)) != 0)
			goto malf;
		if (!env_permitted(cp)) {
			free(cp);
			continue;
		}
		cctx->env = xreallocarray(cctx->env, env_len + 2,
		    sizeof(*cctx->env));
		cctx->env[env_len++] = cp;
		cctx->env[env_len] = NULL;
		if (env_len > MUX_MAX_ENV_VARS) {
			error(">%d environment variables received, ignoring "
			    "additional", MUX_MAX_ENV_VARS);
			break;
		}
	}

	debug2("%s: channel %d: request tty %d, X %d, agent %d, subsys %d, "
	    "term \"%s\", cmd \"%s\", env %u", __func__, c->self,
	    cctx->want_tty, cctx->want_x_fwd, cctx->want_agent_fwd,
	    cctx->want_subsys, cctx->term, cmd, env_len);

	if ((cctx->cmd = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put(cctx->cmd, cmd, strlen(cmd))) != 0)
		fatal("%s: sshbuf_put: %s", __func__, ssh_err(r));
	free(cmd);
	cmd = NULL;

	/* Gather fds from client */
	for(i = 0; i < 3; i++) {
		if ((new_fd[i] = mm_receive_fd(c->sock)) == -1) {
			error("%s: failed to receive fd %d from slave",
			    __func__, i);
			for (j = 0; j < i; j++)
				close(new_fd[j]);
			for (j = 0; j < env_len; j++)
				free(cctx->env[j]);
			free(cctx->env);
			free(cctx->term);
			sshbuf_free(cctx->cmd);
			free(cctx);
			reply_error(reply, MUX_S_FAILURE, rid,
			    "did not receive file descriptors");
			return -1;
		}
	}

	debug3("%s: got fds stdin %d, stdout %d, stderr %d", __func__,
	    new_fd[0], new_fd[1], new_fd[2]);

	/* XXX support multiple child sessions in future */
	if (c->have_remote_id) {
		debug2("%s: session already open", __func__);
		reply_error(reply, MUX_S_FAILURE, rid,
		    "Multiple sessions not supported");
 cleanup:
		close(new_fd[0]);
		close(new_fd[1]);
		close(new_fd[2]);
		free(cctx->term);
		if (env_len != 0) {
			for (i = 0; i < env_len; i++)
				free(cctx->env[i]);
			free(cctx->env);
		}
		sshbuf_free(cctx->cmd);
		free(cctx);
		return 0;
	}

	if (options.control_master == SSHCTL_MASTER_ASK ||
	    options.control_master == SSHCTL_MASTER_AUTO_ASK) {
		if (!ask_permission("Allow shared connection to %s? ", host)) {
			debug2("%s: session refused by user", __func__);
			reply_error(reply, MUX_S_PERMISSION_DENIED, rid,
			    "Permission denied");
			goto cleanup;
		}
	}

	/* Try to pick up ttymodes from client before it goes raw */
	if (cctx->want_tty && tcgetattr(new_fd[0], &cctx->tio) == -1)
		error("%s: tcgetattr: %s", __func__, strerror(errno));

	/* enable nonblocking unless tty */
	if (!isatty(new_fd[0]))
		set_nonblock(new_fd[0]);
	if (!isatty(new_fd[1]))
		set_nonblock(new_fd[1]);
	if (!isatty(new_fd[2]))
		set_nonblock(new_fd[2]);

	window = CHAN_SES_WINDOW_DEFAULT;
	packetmax = CHAN_SES_PACKET_DEFAULT;
	if (cctx->want_tty) {
		window >>= 1;
		packetmax >>= 1;
	}

	nc = channel_new(ssh, "session", SSH_CHANNEL_OPENING,
	    new_fd[0], new_fd[1], new_fd[2], window, packetmax,
	    CHAN_EXTENDED_WRITE, "client-session", /*nonblock*/0);

	nc->ctl_chan = c->self;		/* link session -> control channel */
	c->remote_id = nc->self; 	/* link control -> session channel */
	c->have_remote_id = 1;

	if (cctx->want_tty && escape_char != 0xffffffff) {
		channel_register_filter(ssh, nc->self,
		    client_simple_escape_filter, NULL,
		    client_filter_cleanup,
		    client_new_escape_filter_ctx((int)escape_char));
	}

	debug2("%s: channel_new: %d linked to control channel %d",
	    __func__, nc->self, nc->ctl_chan);

	channel_send_open(ssh, nc->self);
	channel_register_open_confirm(ssh, nc->self, mux_session_confirm, cctx);
	c->mux_pause = 1; /* stop handling messages until open_confirm done */
	channel_register_cleanup(ssh, nc->self,
	    mux_master_session_cleanup_cb, 1);

	/* reply is deferred, sent by mux_session_confirm */
	return 0;
}

static int
process_mux_alive_check(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	int r;

	debug2("%s: channel %d: alive check", __func__, c->self);

	/* prepare reply */
	if ((r = sshbuf_put_u32(reply, MUX_S_ALIVE)) != 0 ||
	    (r = sshbuf_put_u32(reply, rid)) != 0 ||
	    (r = sshbuf_put_u32(reply, (u_int)getpid())) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));

	return 0;
}

static int
process_mux_terminate(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	debug2("%s: channel %d: terminate request", __func__, c->self);

	if (options.control_master == SSHCTL_MASTER_ASK ||
	    options.control_master == SSHCTL_MASTER_AUTO_ASK) {
		if (!ask_permission("Terminate shared connection to %s? ",
		    host)) {
			debug2("%s: termination refused by user", __func__);
			reply_error(reply, MUX_S_PERMISSION_DENIED, rid,
			    "Permission denied");
			return 0;
		}
	}

	quit_pending = 1;
	reply_ok(reply, rid);
	/* XXX exit happens too soon - message never makes it to client */
	return 0;
}

static char *
format_forward(u_int ftype, struct Forward *fwd)
{
	char *ret;

	switch (ftype) {
	case MUX_FWD_LOCAL:
		xasprintf(&ret, "local forward %.200s:%d -> %.200s:%d",
		    (fwd->listen_path != NULL) ? fwd->listen_path :
		    (fwd->listen_host == NULL) ?
		    (options.fwd_opts.gateway_ports ? "*" : "LOCALHOST") :
		    fwd->listen_host, fwd->listen_port,
		    (fwd->connect_path != NULL) ? fwd->connect_path :
		    fwd->connect_host, fwd->connect_port);
		break;
	case MUX_FWD_DYNAMIC:
		xasprintf(&ret, "dynamic forward %.200s:%d -> *",
		    (fwd->listen_host == NULL) ?
		    (options.fwd_opts.gateway_ports ? "*" : "LOCALHOST") :
		     fwd->listen_host, fwd->listen_port);
		break;
	case MUX_FWD_REMOTE:
		xasprintf(&ret, "remote forward %.200s:%d -> %.200s:%d",
		    (fwd->listen_path != NULL) ? fwd->listen_path :
		    (fwd->listen_host == NULL) ?
		    "LOCALHOST" : fwd->listen_host,
		    fwd->listen_port,
		    (fwd->connect_path != NULL) ? fwd->connect_path :
		    fwd->connect_host, fwd->connect_port);
		break;
	default:
		fatal("%s: unknown forward type %u", __func__, ftype);
	}
	return ret;
}

static int
compare_host(const char *a, const char *b)
{
	if (a == NULL && b == NULL)
		return 1;
	if (a == NULL || b == NULL)
		return 0;
	return strcmp(a, b) == 0;
}

static int
compare_forward(struct Forward *a, struct Forward *b)
{
	if (!compare_host(a->listen_host, b->listen_host))
		return 0;
	if (!compare_host(a->listen_path, b->listen_path))
		return 0;
	if (a->listen_port != b->listen_port)
		return 0;
	if (!compare_host(a->connect_host, b->connect_host))
		return 0;
	if (!compare_host(a->connect_path, b->connect_path))
		return 0;
	if (a->connect_port != b->connect_port)
		return 0;

	return 1;
}

static void
mux_confirm_remote_forward(struct ssh *ssh, int type, u_int32_t seq, void *ctxt)
{
	struct mux_channel_confirm_ctx *fctx = ctxt;
	char *failmsg = NULL;
	struct Forward *rfwd;
	Channel *c;
	struct sshbuf *out;
	int r;

	if ((c = channel_by_id(ssh, fctx->cid)) == NULL) {
		/* no channel for reply */
		error("%s: unknown channel", __func__);
		return;
	}
	if ((out = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if (fctx->fid >= options.num_remote_forwards ||
	    (options.remote_forwards[fctx->fid].connect_path == NULL &&
	    options.remote_forwards[fctx->fid].connect_host == NULL)) {
		xasprintf(&failmsg, "unknown forwarding id %d", fctx->fid);
		goto fail;
	}
	rfwd = &options.remote_forwards[fctx->fid];
	debug("%s: %s for: listen %d, connect %s:%d", __func__,
	    type == SSH2_MSG_REQUEST_SUCCESS ? "success" : "failure",
	    rfwd->listen_port, rfwd->connect_path ? rfwd->connect_path :
	    rfwd->connect_host, rfwd->connect_port);
	if (type == SSH2_MSG_REQUEST_SUCCESS) {
		if (rfwd->listen_port == 0) {
			rfwd->allocated_port = packet_get_int();
			debug("Allocated port %u for mux remote forward"
			    " to %s:%d", rfwd->allocated_port,
			    rfwd->connect_host, rfwd->connect_port);
			if ((r = sshbuf_put_u32(out,
			    MUX_S_REMOTE_PORT)) != 0 ||
			    (r = sshbuf_put_u32(out, fctx->rid)) != 0 ||
			    (r = sshbuf_put_u32(out,
			    rfwd->allocated_port)) != 0)
				fatal("%s: reply: %s", __func__, ssh_err(r));
			channel_update_permission(ssh, rfwd->handle,
			   rfwd->allocated_port);
		} else {
			reply_ok(out, fctx->rid);
		}
		goto out;
	} else {
		if (rfwd->listen_port == 0)
			channel_update_permission(ssh, rfwd->handle, -1);
		if (rfwd->listen_path != NULL)
			xasprintf(&failmsg, "remote port forwarding failed for "
			    "listen path %s", rfwd->listen_path);
		else
			xasprintf(&failmsg, "remote port forwarding failed for "
			    "listen port %d", rfwd->listen_port);

                debug2("%s: clearing registered forwarding for listen %d, "
		    "connect %s:%d", __func__, rfwd->listen_port,
		    rfwd->connect_path ? rfwd->connect_path :
		    rfwd->connect_host, rfwd->connect_port);

		free(rfwd->listen_host);
		free(rfwd->listen_path);
		free(rfwd->connect_host);
		free(rfwd->connect_path);
		memset(rfwd, 0, sizeof(*rfwd));
	}
 fail:
	error("%s: %s", __func__, failmsg);
	reply_error(out, MUX_S_FAILURE, fctx->rid, failmsg);
	free(failmsg);
 out:
	if ((r = sshbuf_put_stringb(c->output, out)) != 0)
		fatal("%s: sshbuf_put_stringb: %s", __func__, ssh_err(r));
	sshbuf_free(out);
	if (c->mux_pause <= 0)
		fatal("%s: mux_pause %d", __func__, c->mux_pause);
	c->mux_pause = 0; /* start processing messages again */
}

static int
process_mux_open_fwd(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	struct Forward fwd;
	char *fwd_desc = NULL;
	char *listen_addr, *connect_addr;
	u_int ftype;
	u_int lport, cport;
	int r, i, ret = 0, freefwd = 1;

	memset(&fwd, 0, sizeof(fwd));

	/* XXX - lport/cport check redundant */
	if ((r = sshbuf_get_u32(m, &ftype)) != 0 ||
	    (r = sshbuf_get_cstring(m, &listen_addr, NULL)) != 0 ||
	    (r = sshbuf_get_u32(m, &lport)) != 0 ||
	    (r = sshbuf_get_cstring(m, &connect_addr, NULL)) != 0 ||
	    (r = sshbuf_get_u32(m, &cport)) != 0 ||
	    (lport != (u_int)PORT_STREAMLOCAL && lport > 65535) ||
	    (cport != (u_int)PORT_STREAMLOCAL && cport > 65535)) {
		error("%s: malformed message", __func__);
		ret = -1;
		goto out;
	}
	if (*listen_addr == '\0') {
		free(listen_addr);
		listen_addr = NULL;
	}
	if (*connect_addr == '\0') {
		free(connect_addr);
		connect_addr = NULL;
	}

	memset(&fwd, 0, sizeof(fwd));
	fwd.listen_port = lport;
	if (fwd.listen_port == PORT_STREAMLOCAL)
		fwd.listen_path = listen_addr;
	else
		fwd.listen_host = listen_addr;
	fwd.connect_port = cport;
	if (fwd.connect_port == PORT_STREAMLOCAL)
		fwd.connect_path = connect_addr;
	else
		fwd.connect_host = connect_addr;

	debug2("%s: channel %d: request %s", __func__, c->self,
	    (fwd_desc = format_forward(ftype, &fwd)));

	if (ftype != MUX_FWD_LOCAL && ftype != MUX_FWD_REMOTE &&
	    ftype != MUX_FWD_DYNAMIC) {
		logit("%s: invalid forwarding type %u", __func__, ftype);
 invalid:
		free(listen_addr);
		free(connect_addr);
		reply_error(reply, MUX_S_FAILURE, rid,
		    "Invalid forwarding request");
		return 0;
	}
	if (ftype == MUX_FWD_DYNAMIC && fwd.listen_path) {
		logit("%s: streamlocal and dynamic forwards "
		    "are mutually exclusive", __func__);
		goto invalid;
	}
	if (fwd.listen_port != PORT_STREAMLOCAL && fwd.listen_port >= 65536) {
		logit("%s: invalid listen port %u", __func__,
		    fwd.listen_port);
		goto invalid;
	}
	if ((fwd.connect_port != PORT_STREAMLOCAL &&
	    fwd.connect_port >= 65536) ||
	    (ftype != MUX_FWD_DYNAMIC && ftype != MUX_FWD_REMOTE &&
	    fwd.connect_port == 0)) {
		logit("%s: invalid connect port %u", __func__,
		    fwd.connect_port);
		goto invalid;
	}
	if (ftype != MUX_FWD_DYNAMIC && fwd.connect_host == NULL &&
	    fwd.connect_path == NULL) {
		logit("%s: missing connect host", __func__);
		goto invalid;
	}

	/* Skip forwards that have already been requested */
	switch (ftype) {
	case MUX_FWD_LOCAL:
	case MUX_FWD_DYNAMIC:
		for (i = 0; i < options.num_local_forwards; i++) {
			if (compare_forward(&fwd,
			    options.local_forwards + i)) {
 exists:
				debug2("%s: found existing forwarding",
				    __func__);
				reply_ok(reply, rid);
				goto out;
			}
		}
		break;
	case MUX_FWD_REMOTE:
		for (i = 0; i < options.num_remote_forwards; i++) {
			if (!compare_forward(&fwd, options.remote_forwards + i))
				continue;
			if (fwd.listen_port != 0)
				goto exists;
			debug2("%s: found allocated port", __func__);
			if ((r = sshbuf_put_u32(reply,
			    MUX_S_REMOTE_PORT)) != 0 ||
			    (r = sshbuf_put_u32(reply, rid)) != 0 ||
			    (r = sshbuf_put_u32(reply,
			    options.remote_forwards[i].allocated_port)) != 0)
				fatal("%s: reply: %s", __func__, ssh_err(r));
			goto out;
		}
		break;
	}

	if (options.control_master == SSHCTL_MASTER_ASK ||
	    options.control_master == SSHCTL_MASTER_AUTO_ASK) {
		if (!ask_permission("Open %s on %s?", fwd_desc, host)) {
			debug2("%s: forwarding refused by user", __func__);
			reply_error(reply, MUX_S_PERMISSION_DENIED, rid,
			    "Permission denied");
			goto out;
		}
	}

	if (ftype == MUX_FWD_LOCAL || ftype == MUX_FWD_DYNAMIC) {
		if (!channel_setup_local_fwd_listener(ssh, &fwd,
		    &options.fwd_opts)) {
 fail:
			logit("slave-requested %s failed", fwd_desc);
			reply_error(reply, MUX_S_FAILURE, rid,
			    "Port forwarding failed");
			goto out;
		}
		add_local_forward(&options, &fwd);
		freefwd = 0;
	} else {
		struct mux_channel_confirm_ctx *fctx;

		fwd.handle = channel_request_remote_forwarding(ssh, &fwd);
		if (fwd.handle < 0)
			goto fail;
		add_remote_forward(&options, &fwd);
		fctx = xcalloc(1, sizeof(*fctx));
		fctx->cid = c->self;
		fctx->rid = rid;
		fctx->fid = options.num_remote_forwards - 1;
		client_register_global_confirm(mux_confirm_remote_forward,
		    fctx);
		freefwd = 0;
		c->mux_pause = 1; /* wait for mux_confirm_remote_forward */
		/* delayed reply in mux_confirm_remote_forward */
		goto out;
	}
	reply_ok(reply, rid);
 out:
	free(fwd_desc);
	if (freefwd) {
		free(fwd.listen_host);
		free(fwd.listen_path);
		free(fwd.connect_host);
		free(fwd.connect_path);
	}
	return ret;
}

static int
process_mux_close_fwd(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	struct Forward fwd, *found_fwd;
	char *fwd_desc = NULL;
	const char *error_reason = NULL;
	char *listen_addr = NULL, *connect_addr = NULL;
	u_int ftype;
	int r, i, ret = 0;
	u_int lport, cport;

	memset(&fwd, 0, sizeof(fwd));

	if ((r = sshbuf_get_u32(m, &ftype)) != 0 ||
	    (r = sshbuf_get_cstring(m, &listen_addr, NULL)) != 0 ||
	    (r = sshbuf_get_u32(m, &lport)) != 0 ||
	    (r = sshbuf_get_cstring(m, &connect_addr, NULL)) != 0 ||
	    (r = sshbuf_get_u32(m, &cport)) != 0 ||
	    (lport != (u_int)PORT_STREAMLOCAL && lport > 65535) ||
	    (cport != (u_int)PORT_STREAMLOCAL && cport > 65535)) {
		error("%s: malformed message", __func__);
		ret = -1;
		goto out;
	}

	if (*listen_addr == '\0') {
		free(listen_addr);
		listen_addr = NULL;
	}
	if (*connect_addr == '\0') {
		free(connect_addr);
		connect_addr = NULL;
	}

	memset(&fwd, 0, sizeof(fwd));
	fwd.listen_port = lport;
	if (fwd.listen_port == PORT_STREAMLOCAL)
		fwd.listen_path = listen_addr;
	else
		fwd.listen_host = listen_addr;
	fwd.connect_port = cport;
	if (fwd.connect_port == PORT_STREAMLOCAL)
		fwd.connect_path = connect_addr;
	else
		fwd.connect_host = connect_addr;

	debug2("%s: channel %d: request cancel %s", __func__, c->self,
	    (fwd_desc = format_forward(ftype, &fwd)));

	/* make sure this has been requested */
	found_fwd = NULL;
	switch (ftype) {
	case MUX_FWD_LOCAL:
	case MUX_FWD_DYNAMIC:
		for (i = 0; i < options.num_local_forwards; i++) {
			if (compare_forward(&fwd,
			    options.local_forwards + i)) {
				found_fwd = options.local_forwards + i;
				break;
			}
		}
		break;
	case MUX_FWD_REMOTE:
		for (i = 0; i < options.num_remote_forwards; i++) {
			if (compare_forward(&fwd,
			    options.remote_forwards + i)) {
				found_fwd = options.remote_forwards + i;
				break;
			}
		}
		break;
	}

	if (found_fwd == NULL)
		error_reason = "port not forwarded";
	else if (ftype == MUX_FWD_REMOTE) {
		/*
		 * This shouldn't fail unless we confused the host/port
		 * between options.remote_forwards and permitted_opens.
		 * However, for dynamic allocated listen ports we need
		 * to use the actual listen port.
		 */
		if (channel_request_rforward_cancel(ssh, found_fwd) == -1)
			error_reason = "port not in permitted opens";
	} else {	/* local and dynamic forwards */
		/* Ditto */
		if (channel_cancel_lport_listener(ssh, &fwd, fwd.connect_port,
		    &options.fwd_opts) == -1)
			error_reason = "port not found";
	}

	if (error_reason != NULL)
		reply_error(reply, MUX_S_FAILURE, rid, error_reason);
	else {
		reply_ok(reply, rid);
		free(found_fwd->listen_host);
		free(found_fwd->listen_path);
		free(found_fwd->connect_host);
		free(found_fwd->connect_path);
		found_fwd->listen_host = found_fwd->connect_host = NULL;
		found_fwd->listen_path = found_fwd->connect_path = NULL;
		found_fwd->listen_port = found_fwd->connect_port = 0;
	}
 out:
	free(fwd_desc);
	free(listen_addr);
	free(connect_addr);

	return ret;
}

static int
process_mux_stdio_fwd(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	Channel *nc;
	char *chost = NULL;
	u_int cport, i, j;
	int r, new_fd[2];
	struct mux_stdio_confirm_ctx *cctx;

	if ((r = sshbuf_skip_string(m)) != 0 || /* reserved */
	    (r = sshbuf_get_cstring(m, &chost, NULL)) != 0 ||
	    (r = sshbuf_get_u32(m, &cport)) != 0) {
		free(chost);
		error("%s: malformed message", __func__);
		return -1;
	}

	debug2("%s: channel %d: request stdio fwd to %s:%u",
	    __func__, c->self, chost, cport);

	/* Gather fds from client */
	for(i = 0; i < 2; i++) {
		if ((new_fd[i] = mm_receive_fd(c->sock)) == -1) {
			error("%s: failed to receive fd %d from slave",
			    __func__, i);
			for (j = 0; j < i; j++)
				close(new_fd[j]);
			free(chost);

			/* prepare reply */
			reply_error(reply, MUX_S_FAILURE, rid,
			    "did not receive file descriptors");
			return -1;
		}
	}

	debug3("%s: got fds stdin %d, stdout %d", __func__,
	    new_fd[0], new_fd[1]);

	/* XXX support multiple child sessions in future */
	if (c->have_remote_id) {
		debug2("%s: session already open", __func__);
		reply_error(reply, MUX_S_FAILURE, rid,
		    "Multiple sessions not supported");
 cleanup:
		close(new_fd[0]);
		close(new_fd[1]);
		free(chost);
		return 0;
	}

	if (options.control_master == SSHCTL_MASTER_ASK ||
	    options.control_master == SSHCTL_MASTER_AUTO_ASK) {
		if (!ask_permission("Allow forward to %s:%u? ",
		    chost, cport)) {
			debug2("%s: stdio fwd refused by user", __func__);
			reply_error(reply, MUX_S_PERMISSION_DENIED, rid,
			    "Permission denied");
			goto cleanup;
		}
	}

	/* enable nonblocking unless tty */
	if (!isatty(new_fd[0]))
		set_nonblock(new_fd[0]);
	if (!isatty(new_fd[1]))
		set_nonblock(new_fd[1]);

	nc = channel_connect_stdio_fwd(ssh, chost, cport, new_fd[0], new_fd[1]);
	free(chost);

	nc->ctl_chan = c->self;		/* link session -> control channel */
	c->remote_id = nc->self; 	/* link control -> session channel */
	c->have_remote_id = 1;

	debug2("%s: channel_new: %d linked to control channel %d",
	    __func__, nc->self, nc->ctl_chan);

	channel_register_cleanup(ssh, nc->self,
	    mux_master_session_cleanup_cb, 1);

	cctx = xcalloc(1, sizeof(*cctx));
	cctx->rid = rid;
	channel_register_open_confirm(ssh, nc->self, mux_stdio_confirm, cctx);
	c->mux_pause = 1; /* stop handling messages until open_confirm done */

	/* reply is deferred, sent by mux_session_confirm */
	return 0;
}

/* Callback on open confirmation in mux master for a mux stdio fwd session. */
static void
mux_stdio_confirm(struct ssh *ssh, int id, int success, void *arg)
{
	struct mux_stdio_confirm_ctx *cctx = arg;
	Channel *c, *cc;
	struct sshbuf *reply;
	int r;

	if (cctx == NULL)
		fatal("%s: cctx == NULL", __func__);
	if ((c = channel_by_id(ssh, id)) == NULL)
		fatal("%s: no channel for id %d", __func__, id);
	if ((cc = channel_by_id(ssh, c->ctl_chan)) == NULL)
		fatal("%s: channel %d lacks control channel %d", __func__,
		    id, c->ctl_chan);
	if ((reply = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);

	if (!success) {
		debug3("%s: sending failure reply", __func__);
		reply_error(reply, MUX_S_FAILURE, cctx->rid,
		    "Session open refused by peer");
		/* prepare reply */
		goto done;
	}

	debug3("%s: sending success reply", __func__);
	/* prepare reply */
	if ((r = sshbuf_put_u32(reply, MUX_S_SESSION_OPENED)) != 0 ||
	    (r = sshbuf_put_u32(reply, cctx->rid)) != 0 ||
	    (r = sshbuf_put_u32(reply, c->self)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));

 done:
	/* Send reply */
	if ((r = sshbuf_put_stringb(cc->output, reply)) != 0)
		fatal("%s: sshbuf_put_stringb: %s", __func__, ssh_err(r));
	sshbuf_free(reply);

	if (cc->mux_pause <= 0)
		fatal("%s: mux_pause %d", __func__, cc->mux_pause);
	cc->mux_pause = 0; /* start processing messages again */
	c->open_confirm_ctx = NULL;
	free(cctx);
}

static int
process_mux_stop_listening(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	debug("%s: channel %d: stop listening", __func__, c->self);

	if (options.control_master == SSHCTL_MASTER_ASK ||
	    options.control_master == SSHCTL_MASTER_AUTO_ASK) {
		if (!ask_permission("Disable further multiplexing on shared "
		    "connection to %s? ", host)) {
			debug2("%s: stop listen refused by user", __func__);
			reply_error(reply, MUX_S_PERMISSION_DENIED, rid,
			    "Permission denied");
			return 0;
		}
	}

	if (mux_listener_channel != NULL) {
		channel_free(ssh, mux_listener_channel);
		client_stop_mux();
		free(options.control_path);
		options.control_path = NULL;
		mux_listener_channel = NULL;
		muxserver_sock = -1;
	}

	reply_ok(reply, rid);
	return 0;
}

static int
process_mux_proxy(struct ssh *ssh, u_int rid,
    Channel *c, struct sshbuf *m, struct sshbuf *reply)
{
	int r;

	debug("%s: channel %d: proxy request", __func__, c->self);

	c->mux_rcb = channel_proxy_downstream;
	if ((r = sshbuf_put_u32(reply, MUX_S_PROXY)) != 0 ||
	    (r = sshbuf_put_u32(reply, rid)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));

	return 0;
}

/* Channel callbacks fired on read/write from mux slave fd */
static int
mux_master_read_cb(struct ssh *ssh, Channel *c)
{
	struct mux_master_state *state = (struct mux_master_state *)c->mux_ctx;
	struct sshbuf *in = NULL, *out = NULL;
	u_int type, rid, i;
	int r, ret = -1;

	if ((out = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);

	/* Setup ctx and  */
	if (c->mux_ctx == NULL) {
		state = xcalloc(1, sizeof(*state));
		c->mux_ctx = state;
		channel_register_cleanup(ssh, c->self,
		    mux_master_control_cleanup_cb, 0);

		/* Send hello */
		if ((r = sshbuf_put_u32(out, MUX_MSG_HELLO)) != 0 ||
		    (r = sshbuf_put_u32(out, SSHMUX_VER)) != 0)
			fatal("%s: reply: %s", __func__, ssh_err(r));
		/* no extensions */
		if ((r = sshbuf_put_stringb(c->output, out)) != 0)
			fatal("%s: sshbuf_put_stringb: %s",
			    __func__, ssh_err(r));
		debug3("%s: channel %d: hello sent", __func__, c->self);
		ret = 0;
		goto out;
	}

	/* Channel code ensures that we receive whole packets */
	if ((r = sshbuf_froms(c->input, &in)) != 0) {
 malf:
		error("%s: malformed message", __func__);
		goto out;
	}

	if ((r = sshbuf_get_u32(in, &type)) != 0)
		goto malf;
	debug3("%s: channel %d packet type 0x%08x len %zu",
	    __func__, c->self, type, sshbuf_len(in));

	if (type == MUX_MSG_HELLO)
		rid = 0;
	else {
		if (!state->hello_rcvd) {
			error("%s: expected MUX_MSG_HELLO(0x%08x), "
			    "received 0x%08x", __func__, MUX_MSG_HELLO, type);
			goto out;
		}
		if ((r = sshbuf_get_u32(in, &rid)) != 0)
			goto malf;
	}

	for (i = 0; mux_master_handlers[i].handler != NULL; i++) {
		if (type == mux_master_handlers[i].type) {
			ret = mux_master_handlers[i].handler(ssh, rid,
			    c, in, out);
			break;
		}
	}
	if (mux_master_handlers[i].handler == NULL) {
		error("%s: unsupported mux message 0x%08x", __func__, type);
		reply_error(out, MUX_S_FAILURE, rid, "unsupported request");
		ret = 0;
	}
	/* Enqueue reply packet */
	if (sshbuf_len(out) != 0) {
		if ((r = sshbuf_put_stringb(c->output, out)) != 0)
			fatal("%s: sshbuf_put_stringb: %s",
			    __func__, ssh_err(r));
	}
 out:
	sshbuf_free(in);
	sshbuf_free(out);
	return ret;
}

void
mux_exit_message(struct ssh *ssh, Channel *c, int exitval)
{
	struct sshbuf *m;
	Channel *mux_chan;
	int r;

	debug3("%s: channel %d: exit message, exitval %d", __func__, c->self,
	    exitval);

	if ((mux_chan = channel_by_id(ssh, c->ctl_chan)) == NULL)
		fatal("%s: channel %d missing mux channel %d",
		    __func__, c->self, c->ctl_chan);

	/* Append exit message packet to control socket output queue */
	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_S_EXIT_MESSAGE)) != 0 ||
	    (r = sshbuf_put_u32(m, c->self)) != 0 ||
	    (r = sshbuf_put_u32(m, exitval)) != 0 ||
	    (r = sshbuf_put_stringb(mux_chan->output, m)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));
	sshbuf_free(m);
}

void
mux_tty_alloc_failed(struct ssh *ssh, Channel *c)
{
	struct sshbuf *m;
	Channel *mux_chan;
	int r;

	debug3("%s: channel %d: TTY alloc failed", __func__, c->self);

	if ((mux_chan = channel_by_id(ssh, c->ctl_chan)) == NULL)
		fatal("%s: channel %d missing mux channel %d",
		    __func__, c->self, c->ctl_chan);

	/* Append exit message packet to control socket output queue */
	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_S_TTY_ALLOC_FAIL)) != 0 ||
	    (r = sshbuf_put_u32(m, c->self)) != 0 ||
	    (r = sshbuf_put_stringb(mux_chan->output, m)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));
	sshbuf_free(m);
}

/* Prepare a mux master to listen on a Unix domain socket. */
void
muxserver_listen(struct ssh *ssh)
{
	mode_t old_umask;
	char *orig_control_path = options.control_path;
	char rbuf[16+1];
	u_int i, r;
	int oerrno;

	if (options.control_path == NULL ||
	    options.control_master == SSHCTL_MASTER_NO)
		return;

	debug("setting up multiplex master socket");

	/*
	 * Use a temporary path before listen so we can pseudo-atomically
	 * establish the listening socket in its final location to avoid
	 * other processes racing in between bind() and listen() and hitting
	 * an unready socket.
	 */
	for (i = 0; i < sizeof(rbuf) - 1; i++) {
		r = arc4random_uniform(26+26+10);
		rbuf[i] = (r < 26) ? 'a' + r :
		    (r < 26*2) ? 'A' + r - 26 :
		    '0' + r - 26 - 26;
	}
	rbuf[sizeof(rbuf) - 1] = '\0';
	options.control_path = NULL;
	xasprintf(&options.control_path, "%s.%s", orig_control_path, rbuf);
	debug3("%s: temporary control path %s", __func__, options.control_path);

	old_umask = umask(0177);
	muxserver_sock = unix_listener(options.control_path, 64, 0);
	oerrno = errno;
	umask(old_umask);
	if (muxserver_sock < 0) {
		if (oerrno == EINVAL || oerrno == EADDRINUSE) {
			error("ControlSocket %s already exists, "
			    "disabling multiplexing", options.control_path);
 disable_mux_master:
			if (muxserver_sock != -1) {
				close(muxserver_sock);
				muxserver_sock = -1;
			}
			free(orig_control_path);
			free(options.control_path);
			options.control_path = NULL;
			options.control_master = SSHCTL_MASTER_NO;
			return;
		} else {
			/* unix_listener() logs the error */
			cleanup_exit(255);
		}
	}

	/* Now atomically "move" the mux socket into position */
	if (link(options.control_path, orig_control_path) != 0) {
		if (errno != EEXIST) {
			fatal("%s: link mux listener %s => %s: %s", __func__,
			    options.control_path, orig_control_path,
			    strerror(errno));
		}
		error("ControlSocket %s already exists, disabling multiplexing",
		    orig_control_path);
		unlink(options.control_path);
		goto disable_mux_master;
	}
	unlink(options.control_path);
	free(options.control_path);
	options.control_path = orig_control_path;

	set_nonblock(muxserver_sock);

	mux_listener_channel = channel_new(ssh, "mux listener",
	    SSH_CHANNEL_MUX_LISTENER, muxserver_sock, muxserver_sock, -1,
	    CHAN_TCP_WINDOW_DEFAULT, CHAN_TCP_PACKET_DEFAULT,
	    0, options.control_path, 1);
	mux_listener_channel->mux_rcb = mux_master_read_cb;
	debug3("%s: mux listener channel %d fd %d", __func__,
	    mux_listener_channel->self, mux_listener_channel->sock);
}

/* Callback on open confirmation in mux master for a mux client session. */
static void
mux_session_confirm(struct ssh *ssh, int id, int success, void *arg)
{
	struct mux_session_confirm_ctx *cctx = arg;
	const char *display;
	Channel *c, *cc;
	int i, r;
	struct sshbuf *reply;

	if (cctx == NULL)
		fatal("%s: cctx == NULL", __func__);
	if ((c = channel_by_id(ssh, id)) == NULL)
		fatal("%s: no channel for id %d", __func__, id);
	if ((cc = channel_by_id(ssh, c->ctl_chan)) == NULL)
		fatal("%s: channel %d lacks control channel %d", __func__,
		    id, c->ctl_chan);
	if ((reply = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);

	if (!success) {
		debug3("%s: sending failure reply", __func__);
		reply_error(reply, MUX_S_FAILURE, cctx->rid,
		    "Session open refused by peer");
		goto done;
	}

	display = getenv("DISPLAY");
	if (cctx->want_x_fwd && options.forward_x11 && display != NULL) {
		char *proto, *data;

		/* Get reasonable local authentication information. */
		if (client_x11_get_proto(ssh, display, options.xauth_location,
		    options.forward_x11_trusted, options.forward_x11_timeout,
		    &proto, &data) == 0) {
			/* Request forwarding with authentication spoofing. */
			debug("Requesting X11 forwarding with authentication "
			    "spoofing.");
			x11_request_forwarding_with_spoofing(ssh, id,
			    display, proto, data, 1);
			/* XXX exit_on_forward_failure */
			client_expect_confirm(ssh, id, "X11 forwarding",
			    CONFIRM_WARN);
		}
	}

	if (cctx->want_agent_fwd && options.forward_agent) {
		debug("Requesting authentication agent forwarding.");
		channel_request_start(ssh, id, "auth-agent-req@openssh.com", 0);
		packet_send();
	}

	client_session2_setup(ssh, id, cctx->want_tty, cctx->want_subsys,
	    cctx->term, &cctx->tio, c->rfd, cctx->cmd, cctx->env);

	debug3("%s: sending success reply", __func__);
	/* prepare reply */
	if ((r = sshbuf_put_u32(reply, MUX_S_SESSION_OPENED)) != 0 ||
	    (r = sshbuf_put_u32(reply, cctx->rid)) != 0 ||
	    (r = sshbuf_put_u32(reply, c->self)) != 0)
		fatal("%s: reply: %s", __func__, ssh_err(r));

 done:
	/* Send reply */
	if ((r = sshbuf_put_stringb(cc->output, reply)) != 0)
		fatal("%s: sshbuf_put_stringb: %s", __func__, ssh_err(r));
	sshbuf_free(reply);

	if (cc->mux_pause <= 0)
		fatal("%s: mux_pause %d", __func__, cc->mux_pause);
	cc->mux_pause = 0; /* start processing messages again */
	c->open_confirm_ctx = NULL;
	sshbuf_free(cctx->cmd);
	free(cctx->term);
	if (cctx->env != NULL) {
		for (i = 0; cctx->env[i] != NULL; i++)
			free(cctx->env[i]);
		free(cctx->env);
	}
	free(cctx);
}

/* ** Multiplexing client support */

/* Exit signal handler */
static void
control_client_sighandler(int signo)
{
	muxclient_terminate = signo;
}

/*
 * Relay signal handler - used to pass some signals from mux client to
 * mux master.
 */
static void
control_client_sigrelay(int signo)
{
	int save_errno = errno;

	if (muxserver_pid > 1)
		kill(muxserver_pid, signo);

	errno = save_errno;
}

static int
mux_client_read(int fd, struct sshbuf *b, size_t need)
{
	size_t have;
	ssize_t len;
	u_char *p;
	struct pollfd pfd;
	int r;

	pfd.fd = fd;
	pfd.events = POLLIN;
	if ((r = sshbuf_reserve(b, need, &p)) != 0)
		fatal("%s: reserve: %s", __func__, ssh_err(r));
	for (have = 0; have < need; ) {
		if (muxclient_terminate) {
			errno = EINTR;
			return -1;
		}
		len = read(fd, p + have, need - have);
		if (len < 0) {
			switch (errno) {
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
			case EWOULDBLOCK:
#endif
			case EAGAIN:
				(void)poll(&pfd, 1, -1);
				/* FALLTHROUGH */
			case EINTR:
				continue;
			default:
				return -1;
			}
		}
		if (len == 0) {
			errno = EPIPE;
			return -1;
		}
		have += (size_t)len;
	}
	return 0;
}

static int
mux_client_write_packet(int fd, struct sshbuf *m)
{
	struct sshbuf *queue;
	u_int have, need;
	int r, oerrno, len;
	const u_char *ptr;
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLOUT;
	if ((queue = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_stringb(queue, m)) != 0)
		fatal("%s: sshbuf_put_stringb: %s", __func__, ssh_err(r));

	need = sshbuf_len(queue);
	ptr = sshbuf_ptr(queue);

	for (have = 0; have < need; ) {
		if (muxclient_terminate) {
			sshbuf_free(queue);
			errno = EINTR;
			return -1;
		}
		len = write(fd, ptr + have, need - have);
		if (len < 0) {
			switch (errno) {
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
			case EWOULDBLOCK:
#endif
			case EAGAIN:
				(void)poll(&pfd, 1, -1);
				/* FALLTHROUGH */
			case EINTR:
				continue;
			default:
				oerrno = errno;
				sshbuf_free(queue);
				errno = oerrno;
				return -1;
			}
		}
		if (len == 0) {
			sshbuf_free(queue);
			errno = EPIPE;
			return -1;
		}
		have += (u_int)len;
	}
	sshbuf_free(queue);
	return 0;
}

static int
mux_client_read_packet(int fd, struct sshbuf *m)
{
	struct sshbuf *queue;
	size_t need, have;
	const u_char *ptr;
	int r, oerrno;

	if ((queue = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if (mux_client_read(fd, queue, 4) != 0) {
		if ((oerrno = errno) == EPIPE)
			debug3("%s: read header failed: %s", __func__,
			    strerror(errno));
		sshbuf_free(queue);
		errno = oerrno;
		return -1;
	}
	need = PEEK_U32(sshbuf_ptr(queue));
	if (mux_client_read(fd, queue, need) != 0) {
		oerrno = errno;
		debug3("%s: read body failed: %s", __func__, strerror(errno));
		sshbuf_free(queue);
		errno = oerrno;
		return -1;
	}
	if ((r = sshbuf_get_string_direct(queue, &ptr, &have)) != 0 ||
	    (r = sshbuf_put(m, ptr, have)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	sshbuf_free(queue);
	return 0;
}

static int
mux_client_hello_exchange(int fd)
{
	struct sshbuf *m;
	u_int type, ver;
	int r, ret = -1;

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_MSG_HELLO)) != 0 ||
	    (r = sshbuf_put_u32(m, SSHMUX_VER)) != 0)
		fatal("%s: hello: %s", __func__, ssh_err(r));
	/* no extensions */

	if (mux_client_write_packet(fd, m) != 0) {
		debug("%s: write packet: %s", __func__, strerror(errno));
		goto out;
	}

	sshbuf_reset(m);

	/* Read their HELLO */
	if (mux_client_read_packet(fd, m) != 0) {
		debug("%s: read packet failed", __func__);
		goto out;
	}

	if ((r = sshbuf_get_u32(m, &type)) != 0)
		fatal("%s: decode type: %s", __func__, ssh_err(r));
	if (type != MUX_MSG_HELLO) {
		error("%s: expected HELLO (%u) received %u",
		    __func__, MUX_MSG_HELLO, type);
		goto out;
	}
	if ((r = sshbuf_get_u32(m, &ver)) != 0)
		fatal("%s: decode version: %s", __func__, ssh_err(r));
	if (ver != SSHMUX_VER) {
		error("Unsupported multiplexing protocol version %d "
		    "(expected %d)", ver, SSHMUX_VER);
		goto out;
	}
	debug2("%s: master version %u", __func__, ver);
	/* No extensions are presently defined */
	while (sshbuf_len(m) > 0) {
		char *name = NULL;

		if ((r = sshbuf_get_cstring(m, &name, NULL)) != 0 ||
		    (r = sshbuf_skip_string(m)) != 0) { /* value */
			error("%s: malformed extension: %s",
			    __func__, ssh_err(r));
			goto out;
		}
		debug2("Unrecognised master extension \"%s\"", name);
		free(name);
	}
	/* success */
	ret = 0;
 out:
	sshbuf_free(m);
	return ret;
}

static u_int
mux_client_request_alive(int fd)
{
	struct sshbuf *m;
	char *e;
	u_int pid, type, rid;
	int r;

	debug3("%s: entering", __func__);

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_C_ALIVE_CHECK)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));

	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	sshbuf_reset(m);

	/* Read their reply */
	if (mux_client_read_packet(fd, m) != 0) {
		sshbuf_free(m);
		return 0;
	}

	if ((r = sshbuf_get_u32(m, &type)) != 0)
		fatal("%s: decode type: %s", __func__, ssh_err(r));
	if (type != MUX_S_ALIVE) {
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		fatal("%s: master returned error: %s", __func__, e);
	}

	if ((r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode remote ID: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);
	if ((r = sshbuf_get_u32(m, &pid)) != 0)
		fatal("%s: decode PID: %s", __func__, ssh_err(r));
	sshbuf_free(m);

	debug3("%s: done pid = %u", __func__, pid);

	muxclient_request_id++;

	return pid;
}

static void
mux_client_request_terminate(int fd)
{
	struct sshbuf *m;
	char *e;
	u_int type, rid;
	int r;

	debug3("%s: entering", __func__);

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_C_TERMINATE)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));

	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	sshbuf_reset(m);

	/* Read their reply */
	if (mux_client_read_packet(fd, m) != 0) {
		/* Remote end exited already */
		if (errno == EPIPE) {
			sshbuf_free(m);
			return;
		}
		fatal("%s: read from master failed: %s",
		    __func__, strerror(errno));
	}

	if ((r = sshbuf_get_u32(m, &type)) != 0 ||
	    (r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);
	switch (type) {
	case MUX_S_OK:
		break;
	case MUX_S_PERMISSION_DENIED:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		fatal("Master refused termination request: %s", e);
	case MUX_S_FAILURE:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		fatal("%s: termination request failed: %s", __func__, e);
	default:
		fatal("%s: unexpected response from master 0x%08x",
		    __func__, type);
	}
	sshbuf_free(m);
	muxclient_request_id++;
}

static int
mux_client_forward(int fd, int cancel_flag, u_int ftype, struct Forward *fwd)
{
	struct sshbuf *m;
	char *e, *fwd_desc;
	const char *lhost, *chost;
	u_int type, rid;
	int r;

	fwd_desc = format_forward(ftype, fwd);
	debug("Requesting %s %s",
	    cancel_flag ? "cancellation of" : "forwarding of", fwd_desc);
	free(fwd_desc);

	type = cancel_flag ? MUX_C_CLOSE_FWD : MUX_C_OPEN_FWD;
	if (fwd->listen_path != NULL)
		lhost = fwd->listen_path;
	else if (fwd->listen_host == NULL)
		lhost = "";
	else if (*fwd->listen_host == '\0')
		lhost = "*";
	else
		lhost = fwd->listen_host;

	if (fwd->connect_path != NULL)
		chost = fwd->connect_path;
	else if (fwd->connect_host == NULL)
		chost = "";
	else
		chost = fwd->connect_host;

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, type)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0 ||
	    (r = sshbuf_put_u32(m, ftype)) != 0 ||
	    (r = sshbuf_put_cstring(m, lhost)) != 0 ||
	    (r = sshbuf_put_u32(m, fwd->listen_port)) != 0 ||
	    (r = sshbuf_put_cstring(m, chost)) != 0 ||
	    (r = sshbuf_put_u32(m, fwd->connect_port)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));

	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	sshbuf_reset(m);

	/* Read their reply */
	if (mux_client_read_packet(fd, m) != 0) {
		sshbuf_free(m);
		return -1;
	}

	if ((r = sshbuf_get_u32(m, &type)) != 0 ||
	    (r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);

	switch (type) {
	case MUX_S_OK:
		break;
	case MUX_S_REMOTE_PORT:
		if (cancel_flag)
			fatal("%s: got MUX_S_REMOTE_PORT for cancel", __func__);
		if ((r = sshbuf_get_u32(m, &fwd->allocated_port)) != 0)
			fatal("%s: decode port: %s", __func__, ssh_err(r));
		verbose("Allocated port %u for remote forward to %s:%d",
		    fwd->allocated_port,
		    fwd->connect_host ? fwd->connect_host : "",
		    fwd->connect_port);
		if (muxclient_command == SSHMUX_COMMAND_FORWARD)
			fprintf(stdout, "%i\n", fwd->allocated_port);
		break;
	case MUX_S_PERMISSION_DENIED:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		sshbuf_free(m);
		error("Master refused forwarding request: %s", e);
		return -1;
	case MUX_S_FAILURE:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		sshbuf_free(m);
		error("%s: forwarding request failed: %s", __func__, e);
		return -1;
	default:
		fatal("%s: unexpected response from master 0x%08x",
		    __func__, type);
	}
	sshbuf_free(m);

	muxclient_request_id++;
	return 0;
}

static int
mux_client_forwards(int fd, int cancel_flag)
{
	int i, ret = 0;

	debug3("%s: %s forwardings: %d local, %d remote", __func__,
	    cancel_flag ? "cancel" : "request",
	    options.num_local_forwards, options.num_remote_forwards);

	/* XXX ExitOnForwardingFailure */
	for (i = 0; i < options.num_local_forwards; i++) {
		if (mux_client_forward(fd, cancel_flag,
		    options.local_forwards[i].connect_port == 0 ?
		    MUX_FWD_DYNAMIC : MUX_FWD_LOCAL,
		    options.local_forwards + i) != 0)
			ret = -1;
	}
	for (i = 0; i < options.num_remote_forwards; i++) {
		if (mux_client_forward(fd, cancel_flag, MUX_FWD_REMOTE,
		    options.remote_forwards + i) != 0)
			ret = -1;
	}
	return ret;
}

static int
mux_client_request_session(int fd)
{
	struct sshbuf *m;
	char *e;
	const char *term;
	u_int echar, rid, sid, esid, exitval, type, exitval_seen;
	extern char **environ;
	int r, i, devnull, rawmode;

	debug3("%s: entering", __func__);

	if ((muxserver_pid = mux_client_request_alive(fd)) == 0) {
		error("%s: master alive request failed", __func__);
		return -1;
	}

	signal(SIGPIPE, SIG_IGN);

	if (stdin_null_flag) {
		if ((devnull = open(_PATH_DEVNULL, O_RDONLY)) == -1)
			fatal("open(/dev/null): %s", strerror(errno));
		if (dup2(devnull, STDIN_FILENO) == -1)
			fatal("dup2: %s", strerror(errno));
		if (devnull > STDERR_FILENO)
			close(devnull);
	}

	if ((term = getenv("TERM")) == NULL)
		term = "";
	echar = 0xffffffff;
	if (options.escape_char != SSH_ESCAPECHAR_NONE)
	    echar = (u_int)options.escape_char;

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_C_NEW_SESSION)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0 ||
	    (r = sshbuf_put_string(m, NULL, 0)) != 0 || /* reserved */
	    (r = sshbuf_put_u32(m, tty_flag)) != 0 ||
	    (r = sshbuf_put_u32(m, options.forward_x11)) != 0 ||
	    (r = sshbuf_put_u32(m, options.forward_agent)) != 0 ||
	    (r = sshbuf_put_u32(m, subsystem_flag)) != 0 ||
	    (r = sshbuf_put_u32(m, echar)) != 0 ||
	    (r = sshbuf_put_cstring(m, term)) != 0 ||
	    (r = sshbuf_put_stringb(m, command)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));

	/* Pass environment */
	if (options.num_send_env > 0 && environ != NULL) {
		for (i = 0; environ[i] != NULL; i++) {
			if (!env_permitted(environ[i]))
				continue;
			if ((r = sshbuf_put_cstring(m, environ[i])) != 0)
				fatal("%s: request: %s", __func__, ssh_err(r));
		}
	}
	for (i = 0; i < options.num_setenv; i++) {
		if ((r = sshbuf_put_cstring(m, options.setenv[i])) != 0)
			fatal("%s: request: %s", __func__, ssh_err(r));
	}

	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	/* Send the stdio file descriptors */
	if (mm_send_fd(fd, STDIN_FILENO) == -1 ||
	    mm_send_fd(fd, STDOUT_FILENO) == -1 ||
	    mm_send_fd(fd, STDERR_FILENO) == -1)
		fatal("%s: send fds failed", __func__);

	debug3("%s: session request sent", __func__);

	/* Read their reply */
	sshbuf_reset(m);
	if (mux_client_read_packet(fd, m) != 0) {
		error("%s: read from master failed: %s",
		    __func__, strerror(errno));
		sshbuf_free(m);
		return -1;
	}

	if ((r = sshbuf_get_u32(m, &type)) != 0 ||
	    (r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);

	switch (type) {
	case MUX_S_SESSION_OPENED:
		if ((r = sshbuf_get_u32(m, &sid)) != 0)
			fatal("%s: decode ID: %s", __func__, ssh_err(r));
		break;
	case MUX_S_PERMISSION_DENIED:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		error("Master refused session request: %s", e);
		sshbuf_free(m);
		return -1;
	case MUX_S_FAILURE:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		error("%s: session request failed: %s", __func__, e);
		sshbuf_free(m);
		return -1;
	default:
		sshbuf_free(m);
		error("%s: unexpected response from master 0x%08x",
		    __func__, type);
		return -1;
	}
	muxclient_request_id++;

	if (pledge("stdio proc tty", NULL) == -1)
		fatal("%s pledge(): %s", __func__, strerror(errno));
	platform_pledge_mux();

	signal(SIGHUP, control_client_sighandler);
	signal(SIGINT, control_client_sighandler);
	signal(SIGTERM, control_client_sighandler);
	signal(SIGWINCH, control_client_sigrelay);

	rawmode = tty_flag;
	if (tty_flag)
		enter_raw_mode(options.request_tty == REQUEST_TTY_FORCE);

	/*
	 * Stick around until the controlee closes the client_fd.
	 * Before it does, it is expected to write an exit message.
	 * This process must read the value and wait for the closure of
	 * the client_fd; if this one closes early, the multiplex master will
	 * terminate early too (possibly losing data).
	 */
	for (exitval = 255, exitval_seen = 0;;) {
		sshbuf_reset(m);
		if (mux_client_read_packet(fd, m) != 0)
			break;
		if ((r = sshbuf_get_u32(m, &type)) != 0)
			fatal("%s: decode type: %s", __func__, ssh_err(r));
		switch (type) {
		case MUX_S_TTY_ALLOC_FAIL:
			if ((r = sshbuf_get_u32(m, &esid)) != 0)
				fatal("%s: decode ID: %s",
				    __func__, ssh_err(r));
			if (esid != sid)
				fatal("%s: tty alloc fail on unknown session: "
				    "my id %u theirs %u",
				    __func__, sid, esid);
			leave_raw_mode(options.request_tty ==
			    REQUEST_TTY_FORCE);
			rawmode = 0;
			continue;
		case MUX_S_EXIT_MESSAGE:
			if ((r = sshbuf_get_u32(m, &esid)) != 0)
				fatal("%s: decode ID: %s",
				    __func__, ssh_err(r));
			if (esid != sid)
				fatal("%s: exit on unknown session: "
				    "my id %u theirs %u",
				    __func__, sid, esid);
			if (exitval_seen)
				fatal("%s: exitval sent twice", __func__);
			if ((r = sshbuf_get_u32(m, &exitval)) != 0)
				fatal("%s: decode exit value: %s",
				    __func__, ssh_err(r));
			exitval_seen = 1;
			continue;
		default:
			if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
				fatal("%s: decode error: %s",
				    __func__, ssh_err(r));
			fatal("%s: master returned error: %s", __func__, e);
		}
	}

	close(fd);
	if (rawmode)
		leave_raw_mode(options.request_tty == REQUEST_TTY_FORCE);

	if (muxclient_terminate) {
		debug2("Exiting on signal: %s", strsignal(muxclient_terminate));
		exitval = 255;
	} else if (!exitval_seen) {
		debug2("Control master terminated unexpectedly");
		exitval = 255;
	} else
		debug2("Received exit status from master %d", exitval);

	if (tty_flag && options.log_level != SYSLOG_LEVEL_QUIET)
		fprintf(stderr, "Shared connection to %s closed.\r\n", host);

	exit(exitval);
}

static int
mux_client_proxy(int fd)
{
	struct sshbuf *m;
	char *e;
	u_int type, rid;
	int r;

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_C_PROXY)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));
	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	sshbuf_reset(m);

	/* Read their reply */
	if (mux_client_read_packet(fd, m) != 0) {
		sshbuf_free(m);
		return 0;
	}
	if ((r = sshbuf_get_u32(m, &type)) != 0 ||
	    (r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);
	if (type != MUX_S_PROXY) {
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		fatal("%s: master returned error: %s", __func__, e);
	}
	sshbuf_free(m);

	debug3("%s: done", __func__);
	muxclient_request_id++;
	return 0;
}

static int
mux_client_request_stdio_fwd(int fd)
{
	struct sshbuf *m;
	char *e;
	u_int type, rid, sid;
	int r, devnull;

	debug3("%s: entering", __func__);

	if ((muxserver_pid = mux_client_request_alive(fd)) == 0) {
		error("%s: master alive request failed", __func__);
		return -1;
	}

	signal(SIGPIPE, SIG_IGN);

	if (stdin_null_flag) {
		if ((devnull = open(_PATH_DEVNULL, O_RDONLY)) == -1)
			fatal("open(/dev/null): %s", strerror(errno));
		if (dup2(devnull, STDIN_FILENO) == -1)
			fatal("dup2: %s", strerror(errno));
		if (devnull > STDERR_FILENO)
			close(devnull);
	}

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_C_NEW_STDIO_FWD)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0 ||
	    (r = sshbuf_put_string(m, NULL, 0)) != 0 || /* reserved */
	    (r = sshbuf_put_cstring(m, options.stdio_forward_host)) != 0 ||
	    (r = sshbuf_put_u32(m, options.stdio_forward_port)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));

	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	/* Send the stdio file descriptors */
	if (mm_send_fd(fd, STDIN_FILENO) == -1 ||
	    mm_send_fd(fd, STDOUT_FILENO) == -1)
		fatal("%s: send fds failed", __func__);

	if (pledge("stdio proc tty", NULL) == -1)
		fatal("%s pledge(): %s", __func__, strerror(errno));
	platform_pledge_mux();

	debug3("%s: stdio forward request sent", __func__);

	/* Read their reply */
	sshbuf_reset(m);

	if (mux_client_read_packet(fd, m) != 0) {
		error("%s: read from master failed: %s",
		    __func__, strerror(errno));
		sshbuf_free(m);
		return -1;
	}

	if ((r = sshbuf_get_u32(m, &type)) != 0 ||
	    (r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);
	switch (type) {
	case MUX_S_SESSION_OPENED:
		if ((r = sshbuf_get_u32(m, &sid)) != 0)
			fatal("%s: decode ID: %s", __func__, ssh_err(r));
		debug("%s: master session id: %u", __func__, sid);
		break;
	case MUX_S_PERMISSION_DENIED:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		sshbuf_free(m);
		fatal("Master refused stdio forwarding request: %s", e);
	case MUX_S_FAILURE:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		sshbuf_free(m);
		fatal("Stdio forwarding request failed: %s", e);
	default:
		sshbuf_free(m);
		error("%s: unexpected response from master 0x%08x",
		    __func__, type);
		return -1;
	}
	muxclient_request_id++;

	signal(SIGHUP, control_client_sighandler);
	signal(SIGINT, control_client_sighandler);
	signal(SIGTERM, control_client_sighandler);
	signal(SIGWINCH, control_client_sigrelay);

	/*
	 * Stick around until the controlee closes the client_fd.
	 */
	sshbuf_reset(m);
	if (mux_client_read_packet(fd, m) != 0) {
		if (errno == EPIPE ||
		    (errno == EINTR && muxclient_terminate != 0))
			return 0;
		fatal("%s: mux_client_read_packet: %s",
		    __func__, strerror(errno));
	}
	fatal("%s: master returned unexpected message %u", __func__, type);
}

static void
mux_client_request_stop_listening(int fd)
{
	struct sshbuf *m;
	char *e;
	u_int type, rid;
	int r;

	debug3("%s: entering", __func__);

	if ((m = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new", __func__);
	if ((r = sshbuf_put_u32(m, MUX_C_STOP_LISTENING)) != 0 ||
	    (r = sshbuf_put_u32(m, muxclient_request_id)) != 0)
		fatal("%s: request: %s", __func__, ssh_err(r));

	if (mux_client_write_packet(fd, m) != 0)
		fatal("%s: write packet: %s", __func__, strerror(errno));

	sshbuf_reset(m);

	/* Read their reply */
	if (mux_client_read_packet(fd, m) != 0)
		fatal("%s: read from master failed: %s",
		    __func__, strerror(errno));

	if ((r = sshbuf_get_u32(m, &type)) != 0 ||
	    (r = sshbuf_get_u32(m, &rid)) != 0)
		fatal("%s: decode: %s", __func__, ssh_err(r));
	if (rid != muxclient_request_id)
		fatal("%s: out of sequence reply: my id %u theirs %u",
		    __func__, muxclient_request_id, rid);

	switch (type) {
	case MUX_S_OK:
		break;
	case MUX_S_PERMISSION_DENIED:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		fatal("Master refused stop listening request: %s", e);
	case MUX_S_FAILURE:
		if ((r = sshbuf_get_cstring(m, &e, NULL)) != 0)
			fatal("%s: decode error: %s", __func__, ssh_err(r));
		fatal("%s: stop listening request failed: %s", __func__, e);
	default:
		fatal("%s: unexpected response from master 0x%08x",
		    __func__, type);
	}
	sshbuf_free(m);
	muxclient_request_id++;
}

/* Multiplex client main loop. */
int
muxclient(const char *path)
{
	struct sockaddr_un addr;
	int sock;
	u_int pid;

	if (muxclient_command == 0) {
		if (options.stdio_forward_host != NULL)
			muxclient_command = SSHMUX_COMMAND_STDIO_FWD;
		else
			muxclient_command = SSHMUX_COMMAND_OPEN;
	}

	switch (options.control_master) {
	case SSHCTL_MASTER_AUTO:
	case SSHCTL_MASTER_AUTO_ASK:
		debug("auto-mux: Trying existing master");
		/* FALLTHROUGH */
	case SSHCTL_MASTER_NO:
		break;
	default:
		return -1;
	}

	memset(&addr, '\0', sizeof(addr));
	addr.sun_family = AF_UNIX;

	if (strlcpy(addr.sun_path, path,
	    sizeof(addr.sun_path)) >= sizeof(addr.sun_path))
		fatal("ControlPath too long ('%s' >= %u bytes)", path,
		     (unsigned int)sizeof(addr.sun_path));

	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
		fatal("%s socket(): %s", __func__, strerror(errno));

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		switch (muxclient_command) {
		case SSHMUX_COMMAND_OPEN:
		case SSHMUX_COMMAND_STDIO_FWD:
			break;
		default:
			fatal("Control socket connect(%.100s): %s", path,
			    strerror(errno));
		}
		if (errno == ECONNREFUSED &&
		    options.control_master != SSHCTL_MASTER_NO) {
			debug("Stale control socket %.100s, unlinking", path);
			unlink(path);
		} else if (errno == ENOENT) {
			debug("Control socket \"%.100s\" does not exist", path);
		} else {
			error("Control socket connect(%.100s): %s", path,
			    strerror(errno));
		}
		close(sock);
		return -1;
	}
	set_nonblock(sock);

	if (mux_client_hello_exchange(sock) != 0) {
		error("%s: master hello exchange failed", __func__);
		close(sock);
		return -1;
	}

	switch (muxclient_command) {
	case SSHMUX_COMMAND_ALIVE_CHECK:
		if ((pid = mux_client_request_alive(sock)) == 0)
			fatal("%s: master alive check failed", __func__);
		fprintf(stderr, "Master running (pid=%u)\r\n", pid);
		exit(0);
	case SSHMUX_COMMAND_TERMINATE:
		mux_client_request_terminate(sock);
		if (options.log_level != SYSLOG_LEVEL_QUIET)
			fprintf(stderr, "Exit request sent.\r\n");
		exit(0);
	case SSHMUX_COMMAND_FORWARD:
		if (mux_client_forwards(sock, 0) != 0)
			fatal("%s: master forward request failed", __func__);
		exit(0);
	case SSHMUX_COMMAND_OPEN:
		if (mux_client_forwards(sock, 0) != 0) {
			error("%s: master forward request failed", __func__);
			return -1;
		}
		mux_client_request_session(sock);
		return -1;
	case SSHMUX_COMMAND_STDIO_FWD:
		mux_client_request_stdio_fwd(sock);
		exit(0);
	case SSHMUX_COMMAND_STOP:
		mux_client_request_stop_listening(sock);
		if (options.log_level != SYSLOG_LEVEL_QUIET)
			fprintf(stderr, "Stop listening request sent.\r\n");
		exit(0);
	case SSHMUX_COMMAND_CANCEL_FWD:
		if (mux_client_forwards(sock, 1) != 0)
			error("%s: master cancel forward request failed",
			    __func__);
		exit(0);
	case SSHMUX_COMMAND_PROXY:
		mux_client_proxy(sock);
		return (sock);
	default:
		fatal("unrecognised muxclient_command %d", muxclient_command);
	}
}
