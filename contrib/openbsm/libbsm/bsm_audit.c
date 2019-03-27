/*-
 * Copyright (c) 2004 Apple Inc.
 * Copyright (c) 2005 SPARTA, Inc.
 * All rights reserved.
 *
 * This code was developed in part by Robert N. M. Watson, Senior Principal
 * Scientist, SPARTA, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <config/config.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else
#include <compat/queue.h>
#endif

#include <bsm/audit_internal.h>
#include <bsm/libbsm.h>

#include <netinet/in.h>

#include <errno.h>
#ifdef HAVE_PTHREAD_MUTEX_LOCK
#include <pthread.h>
#endif
#include <stdlib.h>
#include <string.h>

/* array of used descriptors */
static au_record_t	*open_desc_table[MAX_AUDIT_RECORDS];

/* The current number of active record descriptors */
static int	audit_rec_count = 0;

/*
 * Records that can be recycled are maintained in the list given below.  The
 * maximum number of elements that can be present in this list is bounded by
 * MAX_AUDIT_RECORDS.  Memory allocated for these records are never freed.
 */
static LIST_HEAD(, au_record)	audit_free_q;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * This call frees a token_t and its internal data.
 */
void
au_free_token(token_t *tok)
{

	if (tok != NULL) {
		if (tok->t_data)
			free(tok->t_data);
		free(tok);
	}
}

/*
 * This call reserves memory for the audit record.  Memory must be guaranteed
 * before any auditable event can be generated.  The au_record_t structure
 * maintains a reference to the memory allocated above and also the list of
 * tokens associated with this record.  Descriptors are recyled once the
 * records are added to the audit trail following au_close().
 */
int
au_open(void)
{
	au_record_t *rec = NULL;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif

	if (audit_rec_count == 0)
		LIST_INIT(&audit_free_q);

	/*
	 * Find an unused descriptor, remove it from the free list, mark as
	 * used.
	 */
	if (!LIST_EMPTY(&audit_free_q)) {
		rec = LIST_FIRST(&audit_free_q);
		rec->used = 1;
		LIST_REMOVE(rec, au_rec_q);
	}

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif

	if (rec == NULL) {
		/*
		 * Create a new au_record_t if no descriptors are available.
		 */
		rec = malloc (sizeof(au_record_t));
		if (rec == NULL)
			return (-1);

		rec->data = malloc (MAX_AUDIT_RECORD_SIZE * sizeof(u_char));
		if (rec->data == NULL) {
			free(rec);
			errno = ENOMEM;
			return (-1);
		}

#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_lock(&mutex);
#endif

		if (audit_rec_count == MAX_AUDIT_RECORDS) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
			pthread_mutex_unlock(&mutex);
#endif
			free(rec->data);
			free(rec);

			/* XXX We need to increase size of MAX_AUDIT_RECORDS */
			errno = ENOMEM;
			return (-1);
		}
		rec->desc = audit_rec_count;
		open_desc_table[audit_rec_count] = rec;
		audit_rec_count++;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif

	}

	memset(rec->data, 0, MAX_AUDIT_RECORD_SIZE);

	TAILQ_INIT(&rec->token_q);
	rec->len = 0;
	rec->used = 1;

	return (rec->desc);
}

/*
 * Store the token with the record descriptor.
 *
 * Don't permit writing more to the buffer than would let the trailer be
 * appended later.
 */
int
au_write(int d, token_t *tok)
{
	au_record_t *rec;

	if (tok == NULL) {
		errno = EINVAL;
		return (-1); /* Invalid Token */
	}

	/* Write the token to the record descriptor */
	rec = open_desc_table[d];
	if ((rec == NULL) || (rec->used == 0)) {
		errno = EINVAL;
		return (-1); /* Invalid descriptor */
	}

	if (rec->len + tok->len + AUDIT_TRAILER_SIZE > MAX_AUDIT_RECORD_SIZE) {
		errno = ENOMEM;
		return (-1);
	}

	/* Add the token to the tail */
	/*
	 * XXX Not locking here -- we should not be writing to
	 * XXX the same descriptor from different threads
	 */
	TAILQ_INSERT_TAIL(&rec->token_q, tok, tokens);

	rec->len += tok->len; /* grow record length by token size bytes */

	/* Token should not be available after this call */
	tok = NULL;
	return (0); /* Success */
}

/*
 * Assemble an audit record out of its tokens, including allocating header and
 * trailer tokens.  Does not free the token chain, which must be done by the
 * caller if desirable.
 *
 * XXX: Assumes there is sufficient space for the header and trailer.
 */
static int
au_assemble(au_record_t *rec, short event)
{
#ifdef HAVE_AUDIT_SYSCALLS
	struct in6_addr *aptr;
	struct auditinfo_addr aia;
	struct timeval tm;
	size_t hdrsize;
#endif /* HAVE_AUDIT_SYSCALLS */
	token_t *header, *tok, *trailer;
	size_t tot_rec_size;
	u_char *dptr;
	int error;

#ifdef HAVE_AUDIT_SYSCALLS
	/*
	 * Grab the size of the address family stored in the kernel's audit
	 * state.
	 */
	aia.ai_termid.at_type = AU_IPv4;
	aia.ai_termid.at_addr[0] = INADDR_ANY;
	if (audit_get_kaudit(&aia, sizeof(aia)) != 0) {
		if (errno != ENOSYS && errno != EPERM)
			return (-1);
#endif /* HAVE_AUDIT_SYSCALLS */
		tot_rec_size = rec->len + AUDIT_HEADER_SIZE +
		    AUDIT_TRAILER_SIZE;
		header = au_to_header(tot_rec_size, event, 0);
#ifdef HAVE_AUDIT_SYSCALLS
	} else {
		if (gettimeofday(&tm, NULL) < 0)
			return (-1);
		switch (aia.ai_termid.at_type) {
		case AU_IPv4:
			hdrsize = (aia.ai_termid.at_addr[0] == INADDR_ANY) ?
			    AUDIT_HEADER_SIZE : AUDIT_HEADER_EX_SIZE(&aia);
			break;
		case AU_IPv6:
			aptr = (struct in6_addr *)&aia.ai_termid.at_addr[0];
			hdrsize =
			    (IN6_IS_ADDR_UNSPECIFIED(aptr)) ?
			    AUDIT_HEADER_SIZE : AUDIT_HEADER_EX_SIZE(&aia);
			break;
		default:
			return (-1);
		}
		tot_rec_size = rec->len + hdrsize + AUDIT_TRAILER_SIZE;
		/*
		 * A header size greater then AUDIT_HEADER_SIZE means
		 * that we are using an extended header.
		 */
		if (hdrsize > AUDIT_HEADER_SIZE)
			header = au_to_header32_ex_tm(tot_rec_size, event,
			    0, tm, &aia);
		else
			header = au_to_header(tot_rec_size, event, 0);
	}
#endif /* HAVE_AUDIT_SYSCALLS */
	if (header == NULL)
		return (-1);

	trailer = au_to_trailer(tot_rec_size);
	if (trailer == NULL) {
		error = errno;
		au_free_token(header);
		errno = error;
		return (-1);
	}

	TAILQ_INSERT_HEAD(&rec->token_q, header, tokens);
	TAILQ_INSERT_TAIL(&rec->token_q, trailer, tokens);

	rec->len = tot_rec_size;
	dptr = rec->data;

	TAILQ_FOREACH(tok, &rec->token_q, tokens) {
		memcpy(dptr, tok->t_data, tok->len);
		dptr += tok->len;
	}

	return (0);
}

/*
 * Given a record that is no longer of interest, tear it down and convert to a
 * free record.
 */
static void
au_teardown(au_record_t *rec)
{
	token_t *tok;

	/* Free the token list */
	while ((tok = TAILQ_FIRST(&rec->token_q)) != NULL) {
		TAILQ_REMOVE(&rec->token_q, tok, tokens);
		free(tok->t_data);
		free(tok);
	}

	rec->used = 0;
	rec->len = 0;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif

	/* Add the record to the freelist tail */
	LIST_INSERT_HEAD(&audit_free_q, rec, au_rec_q);

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
}

#ifdef HAVE_AUDIT_SYSCALLS
/*
 * Add the header token, identify any missing tokens.  Write out the tokens to
 * the record memory and finally, call audit.
 */
int
au_close(int d, int keep, short event)
{
	au_record_t *rec;
	size_t tot_rec_size;
	int retval = 0;

	rec = open_desc_table[d];
	if ((rec == NULL) || (rec->used == 0)) {
		errno = EINVAL;
		return (-1); /* Invalid descriptor */
	}

	if (keep == AU_TO_NO_WRITE) {
		retval = 0;
		goto cleanup;
	}

	tot_rec_size = rec->len + MAX_AUDIT_HEADER_SIZE + AUDIT_TRAILER_SIZE;

	if (tot_rec_size > MAX_AUDIT_RECORD_SIZE) {
		/*
		 * XXXRW: Since au_write() is supposed to prevent this, spew
		 * an error here.
		 */
		fprintf(stderr, "au_close failed");
		errno = ENOMEM;
		retval = -1;
		goto cleanup;
	}

	if (au_assemble(rec, event) < 0) {
		/*
		 * XXXRW: This is also not supposed to happen, but might if we
		 * are unable to allocate header and trailer memory.
		 */
		retval = -1;
		goto cleanup;
	}

	/* Call the kernel interface to audit */
	retval = audit(rec->data, rec->len);

cleanup:
	/* CLEANUP */
	au_teardown(rec);
	return (retval);
}
#endif /* HAVE_AUDIT_SYSCALLS */

/*
 * au_close(), except onto an in-memory buffer.  Buffer size as an argument,
 * record size returned via same argument on success.
 */
int
au_close_buffer(int d, short event, u_char *buffer, size_t *buflen)
{
	size_t tot_rec_size;
	au_record_t *rec;
	int retval;

	rec = open_desc_table[d];
	if ((rec == NULL) || (rec->used == 0)) {
		errno = EINVAL;
		return (-1);
	}

	retval = 0;
	tot_rec_size = rec->len + MAX_AUDIT_HEADER_SIZE + AUDIT_TRAILER_SIZE;
	if ((tot_rec_size > MAX_AUDIT_RECORD_SIZE) ||
	    (tot_rec_size > *buflen)) {
		/*
		 * XXXRW: See au_close() comment.
		 */
		fprintf(stderr, "au_close_buffer failed %zd", tot_rec_size);
		errno = ENOMEM;
		retval = -1;
		goto cleanup;
	}

	if (au_assemble(rec, event) < 0) {
		/* XXXRW: See au_close() comment. */
		retval = -1;
		goto cleanup;
	}

	memcpy(buffer, rec->data, rec->len);
	*buflen = rec->len;

cleanup:
	au_teardown(rec);
	return (retval);
}

/*
 * au_close_token() returns the byte format of a token_t.  This won't
 * generally be used by applications, but is quite useful for writing test
 * tools.  Will free the token on either success or failure.
 */
int
au_close_token(token_t *tok, u_char *buffer, size_t *buflen)
{

	if (tok->len > *buflen) {
		au_free_token(tok);
		errno = ENOMEM;
		return (EINVAL);
	}

	memcpy(buffer, tok->t_data, tok->len);
	*buflen = tok->len;
	au_free_token(tok);
	return (0);
}
