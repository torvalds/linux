/* $OpenBSD: channels.h,v 1.131 2018/06/06 18:22:41 djm Exp $ */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */
/*
 * Copyright (c) 1999, 2000, 2001, 2002 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef CHANNEL_H
#define CHANNEL_H

/* Definitions for channel types. */
#define SSH_CHANNEL_X11_LISTENER	1	/* Listening for inet X11 conn. */
#define SSH_CHANNEL_PORT_LISTENER	2	/* Listening on a port. */
#define SSH_CHANNEL_OPENING		3	/* waiting for confirmation */
#define SSH_CHANNEL_OPEN		4	/* normal open two-way channel */
#define SSH_CHANNEL_CLOSED		5	/* waiting for close confirmation */
#define SSH_CHANNEL_AUTH_SOCKET		6	/* authentication socket */
#define SSH_CHANNEL_X11_OPEN		7	/* reading first X11 packet */
#define SSH_CHANNEL_LARVAL		10	/* larval session */
#define SSH_CHANNEL_RPORT_LISTENER	11	/* Listening to a R-style port  */
#define SSH_CHANNEL_CONNECTING		12
#define SSH_CHANNEL_DYNAMIC		13
#define SSH_CHANNEL_ZOMBIE		14	/* Almost dead. */
#define SSH_CHANNEL_MUX_LISTENER	15	/* Listener for mux conn. */
#define SSH_CHANNEL_MUX_CLIENT		16	/* Conn. to mux slave */
#define SSH_CHANNEL_ABANDONED		17	/* Abandoned session, eg mux */
#define SSH_CHANNEL_UNIX_LISTENER	18	/* Listening on a domain socket. */
#define SSH_CHANNEL_RUNIX_LISTENER	19	/* Listening to a R-style domain socket. */
#define SSH_CHANNEL_MUX_PROXY		20	/* proxy channel for mux-slave */
#define SSH_CHANNEL_RDYNAMIC_OPEN	21	/* reverse SOCKS, parsing request */
#define SSH_CHANNEL_RDYNAMIC_FINISH	22	/* reverse SOCKS, finishing connect */
#define SSH_CHANNEL_MAX_TYPE		23

#define CHANNEL_CANCEL_PORT_STATIC	-1

/* TCP forwarding */
#define FORWARD_DENY		0
#define FORWARD_REMOTE		(1)
#define FORWARD_LOCAL		(1<<1)
#define FORWARD_ALLOW		(FORWARD_REMOTE|FORWARD_LOCAL)

#define FORWARD_ADM		0x100
#define FORWARD_USER		0x101

struct ssh;
struct Channel;
typedef struct Channel Channel;
struct fwd_perm_list;

typedef void channel_open_fn(struct ssh *, int, int, void *);
typedef void channel_callback_fn(struct ssh *, int, void *);
typedef int channel_infilter_fn(struct ssh *, struct Channel *, char *, int);
typedef void channel_filter_cleanup_fn(struct ssh *, int, void *);
typedef u_char *channel_outfilter_fn(struct ssh *, struct Channel *,
    u_char **, size_t *);

/* Channel success/failure callbacks */
typedef void channel_confirm_cb(struct ssh *, int, struct Channel *, void *);
typedef void channel_confirm_abandon_cb(struct ssh *, struct Channel *, void *);
struct channel_confirm {
	TAILQ_ENTRY(channel_confirm) entry;
	channel_confirm_cb *cb;
	channel_confirm_abandon_cb *abandon_cb;
	void *ctx;
};
TAILQ_HEAD(channel_confirms, channel_confirm);

/* Context for non-blocking connects */
struct channel_connect {
	char *host;
	int port;
	struct addrinfo *ai, *aitop;
};

/* Callbacks for mux channels back into client-specific code */
typedef int mux_callback_fn(struct ssh *, struct Channel *);

struct Channel {
	int     type;		/* channel type/state */
	int     self;		/* my own channel identifier */
	uint32_t remote_id;	/* channel identifier for remote peer */
	int	have_remote_id;	/* non-zero if remote_id is valid */

	u_int   istate;		/* input from channel (state of receive half) */
	u_int   ostate;		/* output to channel  (state of transmit half) */
	int     flags;		/* close sent/rcvd */
	int     rfd;		/* read fd */
	int     wfd;		/* write fd */
	int     efd;		/* extended fd */
	int     sock;		/* sock fd */
	int     ctl_chan;	/* control channel (multiplexed connections) */
	int     isatty;		/* rfd is a tty */
#ifdef _AIX
	int     wfd_isatty;	/* wfd is a tty */
#endif
	int	client_tty;	/* (client) TTY has been requested */
	int     force_drain;	/* force close on iEOF */
	time_t	notbefore;	/* Pause IO until deadline (time_t) */
	int     delayed;	/* post-select handlers for newly created
				 * channels are delayed until the first call
				 * to a matching pre-select handler.
				 * this way post-select handlers are not
				 * accidentally called if a FD gets reused */
	struct sshbuf *input;	/* data read from socket, to be sent over
				 * encrypted connection */
	struct sshbuf *output;	/* data received over encrypted connection for
				 * send on socket */
	struct sshbuf *extended;

	char    *path;
		/* path for unix domain sockets, or host name for forwards */
	int     listening_port;	/* port being listened for forwards */
	char   *listening_addr;	/* addr being listened for forwards */
	int     host_port;	/* remote port to connect for forwards */
	char   *remote_name;	/* remote hostname */

	u_int	remote_window;
	u_int	remote_maxpacket;
	u_int	local_window;
	u_int	local_window_max;
	u_int	local_consumed;
	u_int	local_maxpacket;
	int     extended_usage;
	int	single_connection;

	char   *ctype;		/* type */

	/* callback */
	channel_open_fn		*open_confirm;
	void			*open_confirm_ctx;
	channel_callback_fn	*detach_user;
	int			detach_close;
	struct channel_confirms	status_confirms;

	/* filter */
	channel_infilter_fn	*input_filter;
	channel_outfilter_fn	*output_filter;
	void			*filter_ctx;
	channel_filter_cleanup_fn *filter_cleanup;

	/* keep boundaries */
	int     		datagram;

	/* non-blocking connect */
	/* XXX make this a pointer so the structure can be opaque */
	struct channel_connect	connect_ctx;

	/* multiplexing protocol hook, called for each packet received */
	mux_callback_fn		*mux_rcb;
	void			*mux_ctx;
	int			mux_pause;
	int     		mux_downstream_id;
};

#define CHAN_EXTENDED_IGNORE		0
#define CHAN_EXTENDED_READ		1
#define CHAN_EXTENDED_WRITE		2

/* default window/packet sizes for tcp/x11-fwd-channel */
#define CHAN_SES_PACKET_DEFAULT	(32*1024)
#define CHAN_SES_WINDOW_DEFAULT	(64*CHAN_SES_PACKET_DEFAULT)
#define CHAN_TCP_PACKET_DEFAULT	(32*1024)
#define CHAN_TCP_WINDOW_DEFAULT	(64*CHAN_TCP_PACKET_DEFAULT)
#define CHAN_X11_PACKET_DEFAULT	(16*1024)
#define CHAN_X11_WINDOW_DEFAULT	(4*CHAN_X11_PACKET_DEFAULT)

/* possible input states */
#define CHAN_INPUT_OPEN			0
#define CHAN_INPUT_WAIT_DRAIN		1
#define CHAN_INPUT_WAIT_OCLOSE		2
#define CHAN_INPUT_CLOSED		3

/* possible output states */
#define CHAN_OUTPUT_OPEN		0
#define CHAN_OUTPUT_WAIT_DRAIN		1
#define CHAN_OUTPUT_WAIT_IEOF		2
#define CHAN_OUTPUT_CLOSED		3

#define CHAN_CLOSE_SENT			0x01
#define CHAN_CLOSE_RCVD			0x02
#define CHAN_EOF_SENT			0x04
#define CHAN_EOF_RCVD			0x08
#define CHAN_LOCAL			0x10

/* Read buffer size */
#define CHAN_RBUF	(16*1024)

/* Hard limit on number of channels */
#define CHANNELS_MAX_CHANNELS	(16*1024)

/* check whether 'efd' is still in use */
#define CHANNEL_EFD_INPUT_ACTIVE(c) \
	(c->extended_usage == CHAN_EXTENDED_READ && \
	(c->efd != -1 || \
	sshbuf_len(c->extended) > 0))
#define CHANNEL_EFD_OUTPUT_ACTIVE(c) \
	(c->extended_usage == CHAN_EXTENDED_WRITE && \
	c->efd != -1 && (!(c->flags & (CHAN_EOF_RCVD|CHAN_CLOSE_RCVD)) || \
	sshbuf_len(c->extended) > 0))

/* Add channel management structures to SSH transport instance */
void channel_init_channels(struct ssh *ssh);

/* channel management */

Channel	*channel_by_id(struct ssh *, int);
Channel	*channel_by_remote_id(struct ssh *, u_int);
Channel	*channel_lookup(struct ssh *, int);
Channel *channel_new(struct ssh *, char *, int, int, int, int,
	    u_int, u_int, int, char *, int);
void	 channel_set_fds(struct ssh *, int, int, int, int, int,
	    int, int, u_int);
void	 channel_free(struct ssh *, Channel *);
void	 channel_free_all(struct ssh *);
void	 channel_stop_listening(struct ssh *);

void	 channel_send_open(struct ssh *, int);
void	 channel_request_start(struct ssh *, int, char *, int);
void	 channel_register_cleanup(struct ssh *, int,
	    channel_callback_fn *, int);
void	 channel_register_open_confirm(struct ssh *, int,
	    channel_open_fn *, void *);
void	 channel_register_filter(struct ssh *, int, channel_infilter_fn *,
	    channel_outfilter_fn *, channel_filter_cleanup_fn *, void *);
void	 channel_register_status_confirm(struct ssh *, int,
	    channel_confirm_cb *, channel_confirm_abandon_cb *, void *);
void	 channel_cancel_cleanup(struct ssh *, int);
int	 channel_close_fd(struct ssh *, int *);
void	 channel_send_window_changes(struct ssh *);

/* mux proxy support */

int	 channel_proxy_downstream(struct ssh *, Channel *mc);
int	 channel_proxy_upstream(Channel *, int, u_int32_t, struct ssh *);

/* protocol handler */

int	 channel_input_data(int, u_int32_t, struct ssh *);
int	 channel_input_extended_data(int, u_int32_t, struct ssh *);
int	 channel_input_ieof(int, u_int32_t, struct ssh *);
int	 channel_input_oclose(int, u_int32_t, struct ssh *);
int	 channel_input_open_confirmation(int, u_int32_t, struct ssh *);
int	 channel_input_open_failure(int, u_int32_t, struct ssh *);
int	 channel_input_port_open(int, u_int32_t, struct ssh *);
int	 channel_input_window_adjust(int, u_int32_t, struct ssh *);
int	 channel_input_status_confirm(int, u_int32_t, struct ssh *);

/* file descriptor handling (read/write) */

void	 channel_prepare_select(struct ssh *, fd_set **, fd_set **, int *,
	     u_int*, time_t*);
void     channel_after_select(struct ssh *, fd_set *, fd_set *);
void     channel_output_poll(struct ssh *);

int      channel_not_very_much_buffered_data(struct ssh *);
void     channel_close_all(struct ssh *);
int      channel_still_open(struct ssh *);
char	*channel_open_message(struct ssh *);
int	 channel_find_open(struct ssh *);

/* tcp forwarding */
struct Forward;
struct ForwardOptions;
void	 channel_set_af(struct ssh *, int af);
void     channel_permit_all(struct ssh *, int);
void	 channel_add_permission(struct ssh *, int, int, char *, int);
void	 channel_clear_permission(struct ssh *, int, int);
void	 channel_disable_admin(struct ssh *, int);
void	 channel_update_permission(struct ssh *, int, int);
Channel	*channel_connect_to_port(struct ssh *, const char *, u_short,
	    char *, char *, int *, const char **);
Channel *channel_connect_to_path(struct ssh *, const char *, char *, char *);
Channel	*channel_connect_stdio_fwd(struct ssh *, const char*,
	    u_short, int, int);
Channel	*channel_connect_by_listen_address(struct ssh *, const char *,
	    u_short, char *, char *);
Channel	*channel_connect_by_listen_path(struct ssh *, const char *,
	    char *, char *);
int	 channel_request_remote_forwarding(struct ssh *, struct Forward *);
int	 channel_setup_local_fwd_listener(struct ssh *, struct Forward *,
	    struct ForwardOptions *);
int	 channel_request_rforward_cancel(struct ssh *, struct Forward *);
int	 channel_setup_remote_fwd_listener(struct ssh *, struct Forward *,
	    int *, struct ForwardOptions *);
int	 channel_cancel_rport_listener(struct ssh *, struct Forward *);
int	 channel_cancel_lport_listener(struct ssh *, struct Forward *,
	    int, struct ForwardOptions *);
int	 permitopen_port(const char *);

/* x11 forwarding */

void	 channel_set_x11_refuse_time(struct ssh *, u_int);
int	 x11_connect_display(struct ssh *);
int	 x11_create_display_inet(struct ssh *, int, int, int, u_int *, int **);
void	 x11_request_forwarding_with_spoofing(struct ssh *, int,
	    const char *, const char *, const char *, int);

/* channel close */

int	 chan_is_dead(struct ssh *, Channel *, int);
void	 chan_mark_dead(struct ssh *, Channel *);

/* channel events */

void	 chan_rcvd_oclose(struct ssh *, Channel *);
void	 chan_rcvd_eow(struct ssh *, Channel *);
void	 chan_read_failed(struct ssh *, Channel *);
void	 chan_ibuf_empty(struct ssh *, Channel *);
void	 chan_rcvd_ieof(struct ssh *, Channel *);
void	 chan_write_failed(struct ssh *, Channel *);
void	 chan_obuf_empty(struct ssh *, Channel *);

#endif
