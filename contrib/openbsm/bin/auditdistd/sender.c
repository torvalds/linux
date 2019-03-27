/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config/config.h>

#include <sys/param.h>
#if defined(HAVE_SYS_ENDIAN_H) && defined(HAVE_BSWAP)
#include <sys/endian.h>
#else /* !HAVE_SYS_ENDIAN_H || !HAVE_BSWAP */
#ifdef HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#else /* !HAVE_MACHINE_ENDIAN_H */
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else /* !HAVE_ENDIAN_H */
#error "No supported endian.h"
#endif /* !HAVE_ENDIAN_H */
#endif /* !HAVE_MACHINE_ENDIAN_H */
#include <compat/endian.h>
#endif /* !HAVE_SYS_ENDIAN_H || !HAVE_BSWAP */
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <signal.h>
#include <string.h>
#include <strings.h>

#include <openssl/hmac.h>

#ifndef HAVE_SIGTIMEDWAIT
#include "sigtimedwait.h"
#endif

#include "auditdistd.h"
#include "pjdlog.h"
#include "proto.h"
#include "sandbox.h"
#include "subr.h"
#include "synch.h"
#include "trail.h"

static struct adist_config *adcfg;
static struct adist_host *adhost;

static pthread_rwlock_t adist_remote_lock;
static pthread_mutex_t adist_remote_mtx;
static pthread_cond_t adist_remote_cond;
static struct trail *adist_trail;

static TAILQ_HEAD(, adreq) adist_free_list;
static pthread_mutex_t adist_free_list_lock;
static pthread_cond_t adist_free_list_cond;
static TAILQ_HEAD(, adreq) adist_send_list;
static pthread_mutex_t adist_send_list_lock;
static pthread_cond_t adist_send_list_cond;
static TAILQ_HEAD(, adreq) adist_recv_list;
static pthread_mutex_t adist_recv_list_lock;
static pthread_cond_t adist_recv_list_cond;

static void
init_environment(void)
{
	struct adreq *adreq;
	unsigned int ii;

	rw_init(&adist_remote_lock);
	mtx_init(&adist_remote_mtx);
	cv_init(&adist_remote_cond);
	TAILQ_INIT(&adist_free_list);
	mtx_init(&adist_free_list_lock);
	cv_init(&adist_free_list_cond);
	TAILQ_INIT(&adist_send_list);
	mtx_init(&adist_send_list_lock);
	cv_init(&adist_send_list_cond);
	TAILQ_INIT(&adist_recv_list);
	mtx_init(&adist_recv_list_lock);
	cv_init(&adist_recv_list_cond);

	for (ii = 0; ii < ADIST_QUEUE_SIZE; ii++) {
		adreq = malloc(sizeof(*adreq) + ADIST_BUF_SIZE);
		if (adreq == NULL) {
			pjdlog_exitx(EX_TEMPFAIL,
			    "Unable to allocate %zu bytes of memory for adreq object.",
			    sizeof(*adreq) + ADIST_BUF_SIZE);
		}
		adreq->adr_byteorder = ADIST_BYTEORDER;
		adreq->adr_cmd = ADIST_CMD_UNDEFINED;
		adreq->adr_seq = 0;
		adreq->adr_datasize = 0;
		TAILQ_INSERT_TAIL(&adist_free_list, adreq, adr_next);
	}
}

static int
sender_connect(void)
{
	unsigned char rnd[32], hash[32], resp[32];
	struct proto_conn *conn;
	char welcome[8];
	int16_t val;

	val = 1;
	if (proto_send(adhost->adh_conn, &val, sizeof(val)) < 0) {
		pjdlog_exit(EX_TEMPFAIL,
		    "Unable to send connection request to parent");
	}
	if (proto_recv(adhost->adh_conn, &val, sizeof(val)) < 0) {
		pjdlog_exit(EX_TEMPFAIL,
		    "Unable to receive reply to connection request from parent");
	}
	if (val != 0) {
		errno = val;
		pjdlog_errno(LOG_WARNING, "Unable to connect to %s",
		    adhost->adh_remoteaddr);
		return (-1);
	}
	if (proto_connection_recv(adhost->adh_conn, true, &conn) < 0) {
		pjdlog_exit(EX_TEMPFAIL,
		    "Unable to receive connection from parent");
	}
	if (proto_connect_wait(conn, adcfg->adc_timeout) < 0) {
		pjdlog_errno(LOG_WARNING, "Unable to connect to %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Connected to %s.", adhost->adh_remoteaddr);
	/* Error in setting timeout is not critical, but why should it fail? */
	if (proto_timeout(conn, adcfg->adc_timeout) < 0)
		pjdlog_errno(LOG_WARNING, "Unable to set connection timeout");
	else
		pjdlog_debug(1, "Timeout set to %d.", adcfg->adc_timeout);

	/* Exchange welcome message, which includes version number. */
	(void)snprintf(welcome, sizeof(welcome), "ADIST%02d", ADIST_VERSION);
	if (proto_send(conn, welcome, sizeof(welcome)) < 0) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to send welcome message to %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Welcome message sent (%s).", welcome);
	bzero(welcome, sizeof(welcome));
	if (proto_recv(conn, welcome, sizeof(welcome)) < 0) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive welcome message from %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	if (strncmp(welcome, "ADIST", 5) != 0 || !isdigit(welcome[5]) ||
	    !isdigit(welcome[6]) || welcome[7] != '\0') {
		pjdlog_warning("Invalid welcome message from %s.",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Welcome message received (%s).", welcome);
	/*
	 * Receiver can only reply with version number lower or equal to
	 * the one we sent.
	 */
	adhost->adh_version = atoi(welcome + 5);
	if (adhost->adh_version > ADIST_VERSION) {
		pjdlog_warning("Invalid version number from %s (%d received, up to %d supported).",
		    adhost->adh_remoteaddr, adhost->adh_version, ADIST_VERSION);
		proto_close(conn);
		return (-1);
	}

	pjdlog_debug(1, "Version %d negotiated with %s.", adhost->adh_version,
	    adhost->adh_remoteaddr);

	if (proto_send(conn, adcfg->adc_name, sizeof(adcfg->adc_name)) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to send name to %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Name (%s) sent.", adcfg->adc_name);

	if (proto_recv(conn, rnd, sizeof(rnd)) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to receive challenge from %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Challenge received.");

	if (HMAC(EVP_sha256(), adhost->adh_password,
	    (int)strlen(adhost->adh_password), rnd, (int)sizeof(rnd), hash,
	    NULL) == NULL) {
		pjdlog_warning("Unable to generate response.");
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Response generated.");

	if (proto_send(conn, hash, sizeof(hash)) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to send response to %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Response sent.");

	if (adist_random(rnd, sizeof(rnd)) == -1) {
		pjdlog_warning("Unable to generate challenge.");
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Challenge generated.");

	if (proto_send(conn, rnd, sizeof(rnd)) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to send challenge to %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Challenge sent.");

	if (proto_recv(conn, resp, sizeof(resp)) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to receive response from %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Response received.");

	if (HMAC(EVP_sha256(), adhost->adh_password,
	    (int)strlen(adhost->adh_password), rnd, (int)sizeof(rnd), hash,
	    NULL) == NULL) {
		pjdlog_warning("Unable to generate hash.");
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Hash generated.");

	if (memcmp(resp, hash, sizeof(hash)) != 0) {
		pjdlog_warning("Invalid response from %s (wrong password?).",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_info("Receiver authenticated.");

	if (proto_recv(conn, &adhost->adh_trail_offset,
	    sizeof(adhost->adh_trail_offset)) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive size of the most recent trail file from %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	adhost->adh_trail_offset = le64toh(adhost->adh_trail_offset);
	if (proto_recv(conn, &adhost->adh_trail_name,
	    sizeof(adhost->adh_trail_name)) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive name of the most recent trail file from %s",
		    adhost->adh_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	pjdlog_debug(1, "Trail name (%s) and offset (%ju) received.",
	    adhost->adh_trail_name, (uintmax_t)adhost->adh_trail_offset);

	rw_wlock(&adist_remote_lock);
	mtx_lock(&adist_remote_mtx);
	PJDLOG_ASSERT(adhost->adh_remote == NULL);
	PJDLOG_ASSERT(conn != NULL);
	adhost->adh_remote = conn;
	mtx_unlock(&adist_remote_mtx);
	rw_unlock(&adist_remote_lock);
	cv_signal(&adist_remote_cond);

	return (0);
}

static void
sender_disconnect(void)
{

	rw_wlock(&adist_remote_lock);
	/*
	 * Check for a race between dropping rlock and acquiring wlock -
	 * another thread can close connection in-between.
	 */
	if (adhost->adh_remote == NULL) {
		rw_unlock(&adist_remote_lock);
		return;
	}
	pjdlog_debug(2, "Closing connection to %s.", adhost->adh_remoteaddr);
	proto_close(adhost->adh_remote);
	mtx_lock(&adist_remote_mtx);
	adhost->adh_remote = NULL;
	adhost->adh_reset = true;
	adhost->adh_trail_name[0] = '\0';
	adhost->adh_trail_offset = 0;
	mtx_unlock(&adist_remote_mtx);
	rw_unlock(&adist_remote_lock);

	pjdlog_warning("Disconnected from %s.", adhost->adh_remoteaddr);

	/* Move all in-flight requests back onto free list. */
	QUEUE_CONCAT2(&adist_free_list, &adist_send_list, &adist_recv_list);
}

static void
adreq_fill(struct adreq *adreq, uint8_t cmd, const unsigned char *data,
    size_t size)
{
	static uint64_t seq = 1;

	PJDLOG_ASSERT(size <= ADIST_BUF_SIZE);

	switch (cmd) {
	case ADIST_CMD_OPEN:
	case ADIST_CMD_CLOSE:
		PJDLOG_ASSERT(data != NULL && size == 0);
		size = strlen(data) + 1;
		break;
	case ADIST_CMD_APPEND:
		PJDLOG_ASSERT(data != NULL && size > 0);
		break;
	case ADIST_CMD_KEEPALIVE:
	case ADIST_CMD_ERROR:
		PJDLOG_ASSERT(data == NULL && size == 0);
		break;
	default:
		PJDLOG_ABORT("Invalid command (%hhu).", cmd);
	}

	adreq->adr_cmd = cmd;
	adreq->adr_seq = seq++;
	adreq->adr_datasize = size;
	/* Don't copy if data is already in out buffer. */
	if (data != NULL && data != adreq->adr_data)
		bcopy(data, adreq->adr_data, size);
}

static bool
read_thread_wait(void)
{
	bool newfile = false;

	mtx_lock(&adist_remote_mtx);
	if (adhost->adh_reset) {
reset:
		adhost->adh_reset = false;
		if (trail_filefd(adist_trail) != -1)
			trail_close(adist_trail);
		trail_reset(adist_trail);
		while (adhost->adh_remote == NULL)
			cv_wait(&adist_remote_cond, &adist_remote_mtx);
		trail_start(adist_trail, adhost->adh_trail_name,
		    adhost->adh_trail_offset);
		newfile = true;
	}
	mtx_unlock(&adist_remote_mtx);
	while (trail_filefd(adist_trail) == -1) {
		newfile = true;
		wait_for_dir();
		/*
		 * We may have been disconnected and reconnected in the
		 * meantime, check if reset is set.
		 */
		mtx_lock(&adist_remote_mtx);
		if (adhost->adh_reset)
			goto reset;
		mtx_unlock(&adist_remote_mtx);
		if (trail_filefd(adist_trail) == -1)
			trail_next(adist_trail);
	}
	if (newfile) {
		pjdlog_debug(1, "Trail file \"%s/%s\" opened.",
		    adhost->adh_directory,
		    trail_filename(adist_trail));
		(void)wait_for_file_init(trail_filefd(adist_trail));
	}
	return (newfile);
}

static void *
read_thread(void *arg __unused)
{
	struct adreq *adreq;
	ssize_t done;
	bool newfile;

	pjdlog_debug(1, "%s started.", __func__);

	for (;;) {
		newfile = read_thread_wait();
		QUEUE_TAKE(adreq, &adist_free_list, 0);
		if (newfile) {
			adreq_fill(adreq, ADIST_CMD_OPEN,
			    trail_filename(adist_trail), 0);
			newfile = false;
			goto move;
		}

		done = read(trail_filefd(adist_trail), adreq->adr_data,
		    ADIST_BUF_SIZE);
		if (done == -1) {
			off_t offset;
			int error;

			error = errno;
			offset = lseek(trail_filefd(adist_trail), 0, SEEK_CUR);
			errno = error;
			pjdlog_errno(LOG_ERR,
			    "Error while reading \"%s/%s\" at offset %jd",
			    adhost->adh_directory, trail_filename(adist_trail),
			    offset);
			trail_close(adist_trail);
			adreq_fill(adreq, ADIST_CMD_ERROR, NULL, 0);
			goto move;
		} else if (done == 0) {
			/* End of file. */
			pjdlog_debug(3, "End of \"%s/%s\".",
			    adhost->adh_directory, trail_filename(adist_trail));
			if (!trail_switch(adist_trail)) {
				/* More audit records can arrive. */
				mtx_lock(&adist_free_list_lock);
				TAILQ_INSERT_TAIL(&adist_free_list, adreq,
				    adr_next);
				mtx_unlock(&adist_free_list_lock);
				wait_for_file();
				continue;
			}
			adreq_fill(adreq, ADIST_CMD_CLOSE,
			    trail_filename(adist_trail), 0);
			trail_close(adist_trail);
			goto move;
		}

		adreq_fill(adreq, ADIST_CMD_APPEND, adreq->adr_data, done);
move:
		pjdlog_debug(3,
		    "read thread: Moving request %p to the send queue (%hhu).",
		    adreq, adreq->adr_cmd);
		QUEUE_INSERT(adreq, &adist_send_list);
	}
	/* NOTREACHED */
	return (NULL);
}

static void
keepalive_send(void)
{
	struct adreq *adreq;

	rw_rlock(&adist_remote_lock);
	if (adhost->adh_remote == NULL) {
		rw_unlock(&adist_remote_lock);
		return;
	}
	rw_unlock(&adist_remote_lock);

	mtx_lock(&adist_free_list_lock);
	adreq = TAILQ_FIRST(&adist_free_list);
	if (adreq != NULL)
		TAILQ_REMOVE(&adist_free_list, adreq, adr_next);
	mtx_unlock(&adist_free_list_lock);
	if (adreq == NULL)
		return;

	adreq_fill(adreq, ADIST_CMD_KEEPALIVE, NULL, 0);

	QUEUE_INSERT(adreq, &adist_send_list);

	pjdlog_debug(3, "keepalive_send: Request sent.");
}

static void *
send_thread(void *arg __unused)
{
	time_t lastcheck, now;
	struct adreq *adreq;

	pjdlog_debug(1, "%s started.", __func__);

	lastcheck = time(NULL);

	for (;;) {
		pjdlog_debug(3, "send thread: Taking request.");
		for (;;) {
			QUEUE_TAKE(adreq, &adist_send_list, ADIST_KEEPALIVE);
			if (adreq != NULL)
				break;
			now = time(NULL);
			if (lastcheck + ADIST_KEEPALIVE <= now) {
				keepalive_send();
				lastcheck = now;
			}
		}
		PJDLOG_ASSERT(adreq != NULL);
		pjdlog_debug(3, "send thread: (%p) Got request %hhu.", adreq,
		    adreq->adr_cmd);
		/*
		 * Protect connection from disappearing.
		 */
		rw_rlock(&adist_remote_lock);
		/*
		 * Move the request to the recv queue first to avoid race
		 * where the recv thread receives the reply before we move
		 * the request to the recv queue.
		 */
		QUEUE_INSERT(adreq, &adist_recv_list);
		if (adhost->adh_remote == NULL ||
		    proto_send(adhost->adh_remote, &adreq->adr_packet,
		    ADPKT_SIZE(adreq)) == -1) {
			rw_unlock(&adist_remote_lock);
			pjdlog_debug(1,
			    "send thread: (%p) Unable to send request.", adreq);
			if (adhost->adh_remote != NULL)
				sender_disconnect();
			continue;
		} else {
			pjdlog_debug(3, "Request %p sent successfully.", adreq);
			adreq_log(LOG_DEBUG, 2, -1, adreq,
			    "send: (%p) Request sent: ", adreq);
			rw_unlock(&adist_remote_lock);
		}
	}
	/* NOTREACHED */
	return (NULL);
}

static void
adrep_decode_header(struct adrep *adrep)
{

	/* Byte-swap only if the receiver is using different byte order. */
	if (adrep->adrp_byteorder != ADIST_BYTEORDER) {
		adrep->adrp_byteorder = ADIST_BYTEORDER;
		adrep->adrp_seq = bswap64(adrep->adrp_seq);
		adrep->adrp_error = bswap16(adrep->adrp_error);
	}
}

static void *
recv_thread(void *arg __unused)
{
	struct adrep adrep;
	struct adreq *adreq;

	pjdlog_debug(1, "%s started.", __func__);

	for (;;) {
		/* Wait until there is anything to receive. */
		QUEUE_WAIT(&adist_recv_list);
		pjdlog_debug(3, "recv thread: Got something.");
		rw_rlock(&adist_remote_lock);
		if (adhost->adh_remote == NULL) {
			/*
			 * Connection is dead.
			 * There is a short race in sender_disconnect() between
			 * setting adh_remote to NULL and removing entries from
			 * the recv list, which can result in us being here.
			 * To avoid just spinning, wait for 0.1s.
			 */
			rw_unlock(&adist_remote_lock);
			usleep(100000);
			continue;
		}
		if (proto_recv(adhost->adh_remote, &adrep,
		    sizeof(adrep)) == -1) {
			rw_unlock(&adist_remote_lock);
			pjdlog_errno(LOG_ERR, "Unable to receive reply");
			sender_disconnect();
			continue;
		}
		rw_unlock(&adist_remote_lock);
		adrep_decode_header(&adrep);
		/*
		 * Find the request that was just confirmed.
		 */
		mtx_lock(&adist_recv_list_lock);
		TAILQ_FOREACH(adreq, &adist_recv_list, adr_next) {
			if (adreq->adr_seq == adrep.adrp_seq) {
				TAILQ_REMOVE(&adist_recv_list, adreq,
				    adr_next);
				break;
			}
		}
		if (adreq == NULL) {
			/*
			 * If we disconnected in the meantime, just continue.
			 * On disconnect sender_disconnect() clears the queue,
			 * we can use that.
			 */
			if (TAILQ_EMPTY(&adist_recv_list)) {
				mtx_unlock(&adist_recv_list_lock);
				continue;
			}
			mtx_unlock(&adist_recv_list_lock);
			pjdlog_error("Found no request matching received 'seq' field (%ju).",
			    (uintmax_t)adrep.adrp_seq);
			sender_disconnect();
			continue;
		}
		mtx_unlock(&adist_recv_list_lock);
		adreq_log(LOG_DEBUG, 2, -1, adreq,
		    "recv thread: (%p) Request confirmed: ", adreq);
		pjdlog_debug(3, "recv thread: (%p) Got request %hhu.", adreq,
		    adreq->adr_cmd);
		if (adrep.adrp_error != 0) {
			pjdlog_error("Receiver returned error (%s), disconnecting.",
			    adist_errstr((int)adrep.adrp_error));
			sender_disconnect();
			continue;
		}
		if (adreq->adr_cmd == ADIST_CMD_CLOSE)
			trail_unlink(adist_trail, adreq->adr_data);
		pjdlog_debug(3, "Request received successfully.");
		QUEUE_INSERT(adreq, &adist_free_list);
	}
	/* NOTREACHED */
	return (NULL);
}

static void
guard_check_connection(void)
{

	PJDLOG_ASSERT(adhost->adh_role == ADIST_ROLE_SENDER);

	rw_rlock(&adist_remote_lock);
	if (adhost->adh_remote != NULL) {
		rw_unlock(&adist_remote_lock);
		pjdlog_debug(3, "remote_guard: Connection to %s is ok.",
		    adhost->adh_remoteaddr);
		return;
	}

	/*
	 * Upgrade the lock. It doesn't have to be atomic as no other thread
	 * can change connection status from disconnected to connected.
	 */
	rw_unlock(&adist_remote_lock);
	pjdlog_debug(1, "remote_guard: Reconnecting to %s.",
	    adhost->adh_remoteaddr);
	if (sender_connect() == 0) {
		pjdlog_info("Successfully reconnected to %s.",
		    adhost->adh_remoteaddr);
	} else {
		pjdlog_debug(1, "remote_guard: Reconnect to %s failed.",
		    adhost->adh_remoteaddr);
	}
}

/*
 * Thread guards remote connections and reconnects when needed, handles
 * signals, etc.
 */
static void *
guard_thread(void *arg __unused)
{
	struct timespec timeout;
	time_t lastcheck, now;
	sigset_t mask;
	int signo;

	lastcheck = time(NULL);

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGINT) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGTERM) == 0);

	timeout.tv_sec = ADIST_KEEPALIVE;
	timeout.tv_nsec = 0;
	signo = -1;

	for (;;) {
		switch (signo) {
		case SIGINT:
		case SIGTERM:
			sigexit_received = true;
			pjdlog_exitx(EX_OK,
			    "Termination signal received, exiting.");
			break;
		default:
			break;
		}

		pjdlog_debug(3, "remote_guard: Checking connections.");
		now = time(NULL);
		if (lastcheck + ADIST_KEEPALIVE <= now) {
			guard_check_connection();
			lastcheck = now;
		}
		signo = sigtimedwait(&mask, NULL, &timeout);
	}
	/* NOTREACHED */
	return (NULL);
}

void
adist_sender(struct adist_config *config, struct adist_host *adh)
{
	pthread_t td;
	pid_t pid;
	int error, mode, debuglevel;

	/*
	 * Create communication channel for sending connection requests from
	 * child to parent.
	 */
	if (proto_connect(NULL, "socketpair://", -1, &adh->adh_conn) == -1) {
		pjdlog_errno(LOG_ERR,
		    "Unable to create connection sockets between child and parent");
		return;
	}

	pid = fork();
	if (pid == -1) {
		pjdlog_errno(LOG_ERR, "Unable to fork");
		proto_close(adh->adh_conn);
		adh->adh_conn = NULL;
		return;
	}

	if (pid > 0) {
		/* This is parent. */
		adh->adh_worker_pid = pid;
		/* Declare that we are receiver. */
		proto_recv(adh->adh_conn, NULL, 0);
		return;
	}

	adcfg = config;
	adhost = adh;

	mode = pjdlog_mode_get();
	debuglevel = pjdlog_debug_get();

	/* Declare that we are sender. */
	proto_send(adhost->adh_conn, NULL, 0);

	descriptors_cleanup(adhost);

#ifdef TODO
	descriptors_assert(adhost, mode);
#endif

	pjdlog_init(mode);
	pjdlog_debug_set(debuglevel);
	pjdlog_prefix_set("[%s] (%s) ", adhost->adh_name,
	    role2str(adhost->adh_role));
#ifdef HAVE_SETPROCTITLE
	setproctitle("[%s] (%s) ", adhost->adh_name,
	    role2str(adhost->adh_role));
#endif

	/*
	 * The sender process should be able to remove entries from its
	 * trail directory, but it should not be able to write to the
	 * trail files, only read from them.
	 */
	adist_trail = trail_new(adhost->adh_directory, false);
	if (adist_trail == NULL)
		exit(EX_OSFILE);

	if (sandbox(ADIST_USER, true, "auditdistd: %s (%s)",
	    role2str(adhost->adh_role), adhost->adh_name) != 0) {
		exit(EX_CONFIG);
	}
	pjdlog_info("Privileges successfully dropped.");

	/*
	 * We can ignore wait_for_dir_init() failures. It will fall back to
	 * using sleep(3).
	 */
	(void)wait_for_dir_init(trail_dirfd(adist_trail));

	init_environment();
	if (sender_connect() == 0) {
		pjdlog_info("Successfully connected to %s.",
		    adhost->adh_remoteaddr);
	}
	adhost->adh_reset = true;

	/*
	 * Create the guard thread first, so we can handle signals from the
	 * very begining.
	 */
	error = pthread_create(&td, NULL, guard_thread, NULL);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, send_thread, NULL);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, recv_thread, NULL);
	PJDLOG_ASSERT(error == 0);
	(void)read_thread(NULL);
}
