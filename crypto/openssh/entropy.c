/*
 * Copyright (c) 2001 Damien Miller.  All rights reserved.
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

#include "includes.h"

#ifdef WITH_OPENSSL

#include <sys/types.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_UN_H
# include <sys/un.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h> /* for offsetof */

#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include "openbsd-compat/openssl-compat.h"

#include "ssh.h"
#include "misc.h"
#include "xmalloc.h"
#include "atomicio.h"
#include "pathnames.h"
#include "log.h"
#include "sshbuf.h"
#include "ssherr.h"

/*
 * Portable OpenSSH PRNG seeding:
 * If OpenSSL has not "internally seeded" itself (e.g. pulled data from
 * /dev/random), then collect RANDOM_SEED_SIZE bytes of randomness from
 * PRNGd.
 */
#ifndef OPENSSL_PRNG_ONLY

#define RANDOM_SEED_SIZE 48

/*
 * Collect 'len' bytes of entropy into 'buf' from PRNGD/EGD daemon
 * listening either on 'tcp_port', or via Unix domain socket at *
 * 'socket_path'.
 * Either a non-zero tcp_port or a non-null socket_path must be
 * supplied.
 * Returns 0 on success, -1 on error
 */
int
get_random_bytes_prngd(unsigned char *buf, int len,
    unsigned short tcp_port, char *socket_path)
{
	int fd, addr_len, rval, errors;
	u_char msg[2];
	struct sockaddr_storage addr;
	struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
	struct sockaddr_un *addr_un = (struct sockaddr_un *)&addr;
	mysig_t old_sigpipe;

	/* Sanity checks */
	if (socket_path == NULL && tcp_port == 0)
		fatal("You must specify a port or a socket");
	if (socket_path != NULL &&
	    strlen(socket_path) >= sizeof(addr_un->sun_path))
		fatal("Random pool path is too long");
	if (len <= 0 || len > 255)
		fatal("Too many bytes (%d) to read from PRNGD", len);

	memset(&addr, '\0', sizeof(addr));

	if (tcp_port != 0) {
		addr_in->sin_family = AF_INET;
		addr_in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr_in->sin_port = htons(tcp_port);
		addr_len = sizeof(*addr_in);
	} else {
		addr_un->sun_family = AF_UNIX;
		strlcpy(addr_un->sun_path, socket_path,
		    sizeof(addr_un->sun_path));
		addr_len = offsetof(struct sockaddr_un, sun_path) +
		    strlen(socket_path) + 1;
	}

	old_sigpipe = signal(SIGPIPE, SIG_IGN);

	errors = 0;
	rval = -1;
reopen:
	fd = socket(addr.ss_family, SOCK_STREAM, 0);
	if (fd == -1) {
		error("Couldn't create socket: %s", strerror(errno));
		goto done;
	}

	if (connect(fd, (struct sockaddr*)&addr, addr_len) == -1) {
		if (tcp_port != 0) {
			error("Couldn't connect to PRNGD port %d: %s",
			    tcp_port, strerror(errno));
		} else {
			error("Couldn't connect to PRNGD socket \"%s\": %s",
			    addr_un->sun_path, strerror(errno));
		}
		goto done;
	}

	/* Send blocking read request to PRNGD */
	msg[0] = 0x02;
	msg[1] = len;

	if (atomicio(vwrite, fd, msg, sizeof(msg)) != sizeof(msg)) {
		if (errno == EPIPE && errors < 10) {
			close(fd);
			errors++;
			goto reopen;
		}
		error("Couldn't write to PRNGD socket: %s",
		    strerror(errno));
		goto done;
	}

	if (atomicio(read, fd, buf, len) != (size_t)len) {
		if (errno == EPIPE && errors < 10) {
			close(fd);
			errors++;
			goto reopen;
		}
		error("Couldn't read from PRNGD socket: %s",
		    strerror(errno));
		goto done;
	}

	rval = 0;
done:
	signal(SIGPIPE, old_sigpipe);
	if (fd != -1)
		close(fd);
	return rval;
}

static int
seed_from_prngd(unsigned char *buf, size_t bytes)
{
#ifdef PRNGD_PORT
	debug("trying egd/prngd port %d", PRNGD_PORT);
	if (get_random_bytes_prngd(buf, bytes, PRNGD_PORT, NULL) == 0)
		return 0;
#endif
#ifdef PRNGD_SOCKET
	debug("trying egd/prngd socket %s", PRNGD_SOCKET);
	if (get_random_bytes_prngd(buf, bytes, 0, PRNGD_SOCKET) == 0)
		return 0;
#endif
	return -1;
}

void
rexec_send_rng_seed(struct sshbuf *m)
{
	u_char buf[RANDOM_SEED_SIZE];
	size_t len = sizeof(buf);
	int r;

	if (RAND_bytes(buf, sizeof(buf)) <= 0) {
		error("Couldn't obtain random bytes (error %ld)",
		    ERR_get_error());
		len = 0;
	}
	if ((r = sshbuf_put_string(m, buf, len)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	explicit_bzero(buf, sizeof(buf));
}

void
rexec_recv_rng_seed(struct sshbuf *m)
{
	u_char *buf = NULL;
	size_t len = 0;
	int r;

	if ((r = sshbuf_get_string_direct(m, &buf, &len)) != 0
		fatal("%s: buffer error: %s", __func__, ssh_err(r));

	debug3("rexec_recv_rng_seed: seeding rng with %u bytes", len);
	RAND_add(buf, len, len);
}
#endif /* OPENSSL_PRNG_ONLY */

void
seed_rng(void)
{
#ifndef OPENSSL_PRNG_ONLY
	unsigned char buf[RANDOM_SEED_SIZE];
#endif
	if (!ssh_compatible_openssl(OPENSSL_VERSION_NUMBER, SSLeay()))
		fatal("OpenSSL version mismatch. Built against %lx, you "
		    "have %lx", (u_long)OPENSSL_VERSION_NUMBER, SSLeay());

#ifndef OPENSSL_PRNG_ONLY
	if (RAND_status() == 1) {
		debug3("RNG is ready, skipping seeding");
		return;
	}

	if (seed_from_prngd(buf, sizeof(buf)) == -1)
		fatal("Could not obtain seed from PRNGd");
	RAND_add(buf, sizeof(buf), sizeof(buf));
	memset(buf, '\0', sizeof(buf));

#endif /* OPENSSL_PRNG_ONLY */
	if (RAND_status() != 1)
		fatal("PRNG is not seeded");
}

#else /* WITH_OPENSSL */

/* Handled in arc4random() */
void
seed_rng(void)
{
}

#endif /* WITH_OPENSSL */
