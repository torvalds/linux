/* $OpenBSD: ssh_api.h,v 1.2 2018/04/10 00:10:49 djm Exp $ */
/*
 * Copyright (c) 2012 Markus Friedl.  All rights reserved.
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

#ifndef API_H
#define API_H

#include <sys/types.h>
#include <signal.h>

#include "openbsd-compat/sys-queue.h"

#include "cipher.h"
#include "sshkey.h"
#include "kex.h"
#include "ssh.h"
#include "ssh2.h"
#include "packet.h"

struct kex_params {
	char *proposal[PROPOSAL_MAX];
};

/* public SSH API functions */

/*
 * ssh_init() create a ssh connection object with given (optional)
 * key exchange parameters.
 */
int	ssh_init(struct ssh **, int is_server, struct kex_params *kex_params);

/*
 * release ssh connection state.
 */
void	ssh_free(struct ssh *);

/*
 * attach application specific data to the connection state
 */
void	ssh_set_app_data(struct ssh *, void *);
void	*ssh_get_app_data(struct ssh *);

/*
 * ssh_add_hostkey() registers a private/public hostkey for an ssh
 * connection.
 * ssh_add_hostkey() needs to be called before a key exchange is
 * initiated with ssh_packet_next().
 * private hostkeys are required if we need to act as a server.
 * public hostkeys are used to verify the servers hostkey.
 */
int	ssh_add_hostkey(struct ssh *ssh, struct sshkey *key);

/*
 * ssh_set_verify_host_key_callback() registers a callback function
 * which should be called instead of the default verification. The
 * function given must return 0 if the hostkey is ok, -1 if the
 * verification has failed.
 */
int	ssh_set_verify_host_key_callback(struct ssh *ssh,
    int (*cb)(struct sshkey *, struct ssh *));

/*
 * ssh_packet_next() advances to the next input packet and returns
 * the packet type in typep.
 * ssh_packet_next() works by processing an input byte-stream,
 * decrypting the received data and hiding the key-exchange from
 * the caller.
 * ssh_packet_next() sets typep if there is no new packet available.
 * in this case the caller must fill the input byte-stream by passing
 * the data received over network to ssh_input_append().
 * additionally, the caller needs to send the resulting output
 * byte-stream back over the network. otherwise the key exchange
 * would not proceed. the output byte-stream is accessed through
 * ssh_output_ptr().
 */
int	ssh_packet_next(struct ssh *ssh, u_char *typep);

/*
 * ssh_packet_payload() returns a pointer to the raw payload data of
 * the current input packet and the length of this payload.
 * the payload is accessible until ssh_packet_next() is called again.
 */
const u_char	*ssh_packet_payload(struct ssh *ssh, size_t *lenp);

/*
 * ssh_packet_put() creates an encrypted packet with the given type
 * and payload.
 * the encrypted packet is appended to the output byte-stream.
 */
int	ssh_packet_put(struct ssh *ssh, int type, const u_char *data,
    size_t len);

/*
 * ssh_input_space() checks if 'len' bytes can be appended to the
 * input byte-stream.
 */
int	ssh_input_space(struct ssh *ssh, size_t len);

/*
 * ssh_input_append() appends data to the input byte-stream.
 */
int	ssh_input_append(struct ssh *ssh, const u_char *data, size_t len);

/*
 * ssh_output_space() checks if 'len' bytes can be appended to the
 * output byte-stream. XXX
 */
int	ssh_output_space(struct ssh *ssh, size_t len);

/*
 * ssh_output_ptr() retrieves both a pointer and the length of the
 * current output byte-stream. the bytes need to be sent over the
 * network. the number of bytes that have been successfully sent can
 * be removed from the output byte-stream with ssh_output_consume().
 */
const u_char	*ssh_output_ptr(struct ssh *ssh, size_t *len);

/*
 * ssh_output_consume() removes the given number of bytes from
 * the output byte-stream.
 */
int	ssh_output_consume(struct ssh *ssh, size_t len);

#endif
