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

#ifndef	_AUDITDISTD_H_
#define	_AUDITDISTD_H_

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <compat/compat.h>

#include "proto.h"

/*
 * Version history:
 * 0 - initial version
 */
#define	ADIST_VERSION	0

#define	ADIST_ROLE_UNDEF	0
#define	ADIST_ROLE_SENDER	1
#define	ADIST_ROLE_RECEIVER	2

#define	ADIST_USER			"auditdistd"
#define	ADIST_TIMEOUT			20
#define	ADIST_CONFIG			"/etc/security/auditdistd.conf"
#define	ADIST_TCP_PORT			"7878"
#define	ADIST_LISTEN_TLS_TCP4		"tls://0.0.0.0:" ADIST_TCP_PORT
#define	ADIST_LISTEN_TLS_TCP6		"tls://[::]:" ADIST_TCP_PORT
#define	ADIST_PIDFILE			"/var/run/auditdistd.pid"
#define	ADIST_DIRECTORY_SENDER		"/var/audit/dist"
#define	ADIST_DIRECTORY_RECEIVER	"/var/audit/remote"
#define	ADIST_CERTFILE			"/etc/security/auditdistd.cert.pem"
#define	ADIST_KEYFILE			"/etc/security/auditdistd.key.pem"

#define	ADIST_ERROR_WRONG_ORDER		1
#define	ADIST_ERROR_INVALID_NAME	2
#define	ADIST_ERROR_OPEN_OLD		3
#define	ADIST_ERROR_CREATE		4
#define	ADIST_ERROR_OPEN		5
#define	ADIST_ERROR_READ		6
#define	ADIST_ERROR_WRITE		7
#define	ADIST_ERROR_RENAME		8

#define	ADIST_ADDRSIZE		1024
#define	ADIST_HOSTSIZE		256
#define	ADIST_PATHSIZE		256
#define	ADIST_PASSWORDSIZE	128
#define	ADIST_FINGERPRINTSIZE	256

/* Number of seconds to sleep between reconnect retries or keepalive packets. */
#define	ADIST_KEEPALIVE	10

struct adist_listen {
	/* Address to listen on. */
	char	 adl_addr[ADIST_ADDRSIZE];
	/* Protocol-specific data. */
	struct proto_conn *adl_conn;
	TAILQ_ENTRY(adist_listen) adl_next;
};

struct adist_config {
	/* Our name. */
	char	adc_name[ADIST_HOSTSIZE];
	/* PID file path. */
	char	adc_pidfile[PATH_MAX];
	/* Connection timeout. */
	int	adc_timeout;
	/* Path to receiver's certificate file. */
	char	adc_certfile[PATH_MAX];
	/* Path to receiver's private key file. */
	char	adc_keyfile[PATH_MAX];
	/* List of addresses to listen on. */
	TAILQ_HEAD(, adist_listen) adc_listen;
	/* List of hosts. */
	TAILQ_HEAD(, adist_host) adc_hosts;
};

#define	ADIST_COMPRESSION_NONE	0
#define	ADIST_COMPRESSION_LZF	1

#define	ADIST_CHECKSUM_NONE	0
#define	ADIST_CHECKSUM_CRC32	1
#define	ADIST_CHECKSUM_SHA256	2

/*
 * Structure that describes single host (either sender or receiver).
 */
struct adist_host {
	/* Host name. */
	char	adh_name[ADIST_HOSTSIZE];
	/* Host role: ADIST_ROLE_{SENDER,RECEIVER}. */
	int	adh_role;
	/* Protocol version negotiated. */
	int	adh_version;

	/* Local address to bind to. */
	char	adh_localaddr[ADIST_ADDRSIZE];
	/* Address of the remote component. */
	char	adh_remoteaddr[ADIST_ADDRSIZE];
	/* Connection with remote host. */
	struct proto_conn *adh_remote;
	/* Connection was reestablished, reset the state. */
	bool	adh_reset;

	/*
	 * Directory from which audit trail files should be send in
	 * ADIST_ROLE_SENDER case or stored into in ADIST_ROLE_RECEIVER case.
	 */
	char	adh_directory[PATH_MAX];
	/* Compression algorithm. Currently unused. */
	int	adh_compression;
	/* Checksum algorithm. Currently unused. */
	int	adh_checksum;

	/* Sender's password. */
	char	adh_password[ADIST_PASSWORDSIZE];
	/* Fingerprint of receiver's public key. */
	char	adh_fingerprint[ADIST_FINGERPRINTSIZE];

	/* PID of child worker process. 0 - no child. */
	pid_t	adh_worker_pid;
	/* Connection requests from sender to main. */
	struct proto_conn *adh_conn;

	/* Receiver-specific fields. */
	char	 adh_trail_name[ADIST_PATHSIZE];
	int	 adh_trail_fd;
	int	 adh_trail_dirfd;
	DIR	*adh_trail_dirfp;
	/* Sender-specific fields. */
	uint64_t adh_trail_offset;

	/* Next resource. */
	TAILQ_ENTRY(adist_host) adh_next;
};

#define	ADIST_BYTEORDER_UNDEFINED	0
#define	ADIST_BYTEORDER_LITTLE_ENDIAN	1
#define	ADIST_BYTEORDER_BIG_ENDIAN	2

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define	ADIST_BYTEORDER	ADIST_BYTEORDER_LITTLE_ENDIAN
#elif _BYTE_ORDER == _BIG_ENDIAN
#define	ADIST_BYTEORDER	ADIST_BYTEORDER_BIG_ENDIAN
#else
#error Unknown byte order.
#endif

struct adpkt {
	uint8_t		adp_byteorder;
#define	ADIST_CMD_UNDEFINED	0
#define	ADIST_CMD_OPEN		1
#define	ADIST_CMD_APPEND	2
#define	ADIST_CMD_CLOSE		3
#define	ADIST_CMD_KEEPALIVE	4
#define	ADIST_CMD_ERROR		5
	uint8_t		adp_cmd;
	uint64_t	adp_seq;
	uint32_t	adp_datasize;
	unsigned char	adp_data[0];
} __packed;

struct adreq {
	int			adr_error;
	TAILQ_ENTRY(adreq)	adr_next;
	struct adpkt		adr_packet;
};

#define	adr_byteorder	adr_packet.adp_byteorder
#define	adr_cmd		adr_packet.adp_cmd
#define	adr_seq		adr_packet.adp_seq
#define	adr_datasize	adr_packet.adp_datasize
#define	adr_data	adr_packet.adp_data

#define	ADPKT_SIZE(adreq)	(sizeof((adreq)->adr_packet) + (adreq)->adr_datasize)

struct adrep {
	uint8_t		adrp_byteorder;
	uint64_t	adrp_seq;
	uint16_t	adrp_error;
} __packed;

#define	ADIST_QUEUE_SIZE	16
#define	ADIST_BUF_SIZE		65536

#define	QUEUE_TAKE(adreq, list, timeout)	do {			\
	mtx_lock(list##_lock);						\
	if ((timeout) == 0) {						\
		while (((adreq) = TAILQ_FIRST(list)) == NULL)		\
			cv_wait(list##_cond, list##_lock);		\
	} else {							\
		(adreq) = TAILQ_FIRST(list);				\
		if ((adreq) == NULL) {					\
			cv_timedwait(list##_cond, list##_lock,		\
			    (timeout));					\
			(adreq) = TAILQ_FIRST(list);			\
		}							\
	}								\
	if ((adreq) != NULL)						\
		TAILQ_REMOVE((list), (adreq), adr_next);		\
	mtx_unlock(list##_lock);					\
} while (0)
#define	QUEUE_INSERT(adreq, list)	do {				\
	bool _wakeup;							\
									\
	mtx_lock(list##_lock);						\
	_wakeup = TAILQ_EMPTY(list);					\
	TAILQ_INSERT_TAIL((list), (adreq), adr_next);			\
	mtx_unlock(list##_lock);					\
	if (_wakeup)							\
		cv_signal(list##_cond);					\
} while (0)
#define	QUEUE_CONCAT2(tolist, fromlist1, fromlist2)	do {		\
	bool _wakeup;							\
									\
	mtx_lock(tolist##_lock);					\
	_wakeup = TAILQ_EMPTY(tolist);					\
	mtx_lock(fromlist1##_lock);					\
	TAILQ_CONCAT((tolist), (fromlist1), adr_next);			\
	mtx_unlock(fromlist1##_lock);					\
	mtx_lock(fromlist2##_lock);					\
	TAILQ_CONCAT((tolist), (fromlist2), adr_next);			\
	mtx_unlock(fromlist2##_lock);					\
	mtx_unlock(tolist##_lock);					\
	if (_wakeup)							\
		cv_signal(tolist##_cond);				\
} while (0)
#define	QUEUE_WAIT(list)	do {					\
	mtx_lock(list##_lock);						\
	while (TAILQ_EMPTY(list))					\
		cv_wait(list##_cond, list##_lock);			\
	mtx_unlock(list##_lock);					\
} while (0)

extern const char *cfgpath;
extern bool sigexit_received;
extern struct pidfh *pfh;

void descriptors_cleanup(struct adist_host *adhost);
void descriptors_assert(const struct adist_host *adhost, int pjdlogmode);

void adist_sender(struct adist_config *config, struct adist_host *adhost);
void adist_receiver(struct adist_config *config, struct adist_host *adhost);

struct adist_config *yy_config_parse(const char *config, bool exitonerror);
void yy_config_free(struct adist_config *config);

void yyerror(const char *);
int yylex(void);

#endif	/* !_AUDITDISTD_H_ */
