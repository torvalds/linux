/*
 * httpread - Manage reading file(s) from HTTP/TCP socket
 * Author: Ted Merrill
 * Copyright 2008 Atheros Communications
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * The files are buffered via internal callbacks from eloop, then presented to
 * an application callback routine when completely read into memory. May also
 * be used if no file is expected but just to get the header, including HTTP
 * replies (e.g. HTTP/1.1 200 OK etc.).
 *
 * This does not attempt to be an optimally efficient implementation, but does
 * attempt to be of reasonably small size and memory consumption; assuming that
 * only small files are to be read. A maximum file size is provided by
 * application and enforced.
 *
 * It is assumed that the application does not expect any of the following:
 * -- transfer encoding other than chunked
 * -- trailer fields
 * It is assumed that, even if the other side requested that the connection be
 * kept open, that we will close it (thus HTTP messages sent by application
 * should have the connection closed field); this is allowed by HTTP/1.1 and
 * simplifies things for us.
 *
 * Other limitations:
 * -- HTTP header may not exceed a hard-coded size.
 *
 * Notes:
 * This code would be massively simpler without some of the new features of
 * HTTP/1.1, especially chunked data.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "httpread.h"


/* Tunable parameters */
#define HTTPREAD_READBUF_SIZE 1024      /* read in chunks of this size */
#define HTTPREAD_HEADER_MAX_SIZE 4096   /* max allowed for headers */
#define HTTPREAD_BODYBUF_DELTA 4096     /* increase allocation by this */

#if 0
/* httpread_debug -- set this global variable > 0 e.g. from debugger
 * to enable debugs (larger numbers for more debugs)
 * Make this a #define of 0 to eliminate the debugging code.
 */
int httpread_debug = 99;
#else
#define httpread_debug 0        /* eliminates even the debugging code */
#endif


/* control instance -- actual definition (opaque to application)
 */
struct httpread {
	/* information from creation */
	int sd;         /* descriptor of TCP socket to read from */
	void (*cb)(struct httpread *handle, void *cookie,
		    enum httpread_event e);  /* call on event */
	void *cookie;   /* pass to callback */
	int max_bytes;          /* maximum file size else abort it */
	int timeout_seconds;            /* 0 or total duration timeout period */

	/* dynamically used information follows */
	int sd_registered;      /* nonzero if we need to unregister socket */
	int to_registered;      /* nonzero if we need to unregister timeout */

	int got_hdr;            /* nonzero when header is finalized */
	char hdr[HTTPREAD_HEADER_MAX_SIZE+1];   /* headers stored here */
	int hdr_nbytes;

	enum httpread_hdr_type hdr_type;
	int version;            /* 1 if we've seen 1.1 */
	int reply_code;         /* for type REPLY, e.g. 200 for HTTP/1.1 200 OK */
	int got_content_length; /* true if we know content length for sure */
	int content_length;     /* body length,  iff got_content_length */
	int chunked;            /* nonzero for chunked data */
	char *uri;

	int got_body;           /* nonzero when body is finalized */
	char *body;
	int body_nbytes;
	int body_alloc_nbytes;  /* amount allocated */

	int got_file;           /* here when we are done */

	/* The following apply if data is chunked: */
	int in_chunk_data;      /* 0=in/at header, 1=in the data or tail*/
	int chunk_start;        /* offset in body of chunk hdr or data */
	int chunk_size;         /* data of chunk (not hdr or ending CRLF)*/
	int in_trailer;         /* in header fields after data (chunked only)*/
	enum trailer_state {
		trailer_line_begin = 0,
		trailer_empty_cr,       /* empty line + CR */
		trailer_nonempty,
		trailer_nonempty_cr,
	} trailer_state;
};


/* Check words for equality, where words consist of graphical characters
 * delimited by whitespace
 * Returns nonzero if "equal" doing case insensitive comparison.
 */
static int word_eq(char *s1, char *s2)
{
	int c1;
	int c2;
	int end1 = 0;
	int end2 = 0;
	for (;;) {
		c1 = *s1++;
		c2 = *s2++;
		if (isalpha(c1) && isupper(c1))
			c1 = tolower(c1);
		if (isalpha(c2) && isupper(c2))
			c2 = tolower(c2);
		end1 = !isgraph(c1);
		end2 = !isgraph(c2);
		if (end1 || end2 || c1 != c2)
			break;
	}
	return end1 && end2;  /* reached end of both words? */
}


/* convert hex to binary
 * Requires that c have been previously tested true with isxdigit().
 */
static int hex_value(int c)
{
	if (isdigit(c))
		return c - '0';
	if (islower(c))
		return 10 + c - 'a';
	return 10 + c - 'A';
}


static void httpread_timeout_handler(void *eloop_data, void *user_ctx);

/* httpread_destroy -- if h is non-NULL, clean up
 * This must eventually be called by the application following
 * call of the application's callback and may be called
 * earlier if desired.
 */
void httpread_destroy(struct httpread *h)
{
	if (httpread_debug >= 10)
		wpa_printf(MSG_DEBUG, "ENTER httpread_destroy(%p)", h);
	if (!h)
		return;

	if (h->to_registered)
		eloop_cancel_timeout(httpread_timeout_handler, NULL, h);
	h->to_registered = 0;
	if (h->sd_registered)
		eloop_unregister_sock(h->sd, EVENT_TYPE_READ);
	h->sd_registered = 0;
	os_free(h->body);
	os_free(h->uri);
	os_memset(h, 0, sizeof(*h));  /* aid debugging */
	h->sd = -1;     /* aid debugging */
	os_free(h);
}


/* httpread_timeout_handler -- called on excessive total duration
 */
static void httpread_timeout_handler(void *eloop_data, void *user_ctx)
{
	struct httpread *h = user_ctx;
	wpa_printf(MSG_DEBUG, "httpread timeout (%p)", h);
	h->to_registered = 0;   /* is self-cancelling */
	(*h->cb)(h, h->cookie, HTTPREAD_EVENT_TIMEOUT);
}


/* Analyze options only so far as is needed to correctly obtain the file.
 * The application can look at the raw header to find other options.
 */
static int httpread_hdr_option_analyze(
	struct httpread *h,
	char *hbp       /* pointer to current line in header buffer */
	)
{
	if (word_eq(hbp, "CONTENT-LENGTH:")) {
		while (isgraph(*hbp))
			hbp++;
		while (*hbp == ' ' || *hbp == '\t')
			hbp++;
		if (!isdigit(*hbp))
			return -1;
		h->content_length = atol(hbp);
		h->got_content_length = 1;
		return 0;
	}
	if (word_eq(hbp, "TRANSFER_ENCODING:") ||
	    word_eq(hbp, "TRANSFER-ENCODING:")) {
		while (isgraph(*hbp))
			hbp++;
		while (*hbp == ' ' || *hbp == '\t')
			hbp++;
		/* There should (?) be no encodings of interest
		 * other than chunked...
		 */
		if (word_eq(hbp, "CHUNKED")) {
			h->chunked = 1;
			h->in_chunk_data = 0;
			/* ignore possible ;<parameters> */
		}
		return 0;
	}
	/* skip anything we don't know, which is a lot */
	return 0;
}


static int httpread_hdr_analyze(struct httpread *h)
{
	char *hbp = h->hdr;      /* pointer into h->hdr */
	int standard_first_line = 1;

	/* First line is special */
	h->hdr_type = HTTPREAD_HDR_TYPE_UNKNOWN;
	if (!isgraph(*hbp))
		goto bad;
	if (os_strncmp(hbp, "HTTP/", 5) == 0) {
		h->hdr_type = HTTPREAD_HDR_TYPE_REPLY;
		standard_first_line = 0;
		hbp += 5;
		if (hbp[0] == '1' && hbp[1] == '.' &&
		    isdigit(hbp[2]) && hbp[2] != '0')
			h->version = 1;
		while (isgraph(*hbp))
			hbp++;
		while (*hbp == ' ' || *hbp == '\t')
			hbp++;
		if (!isdigit(*hbp))
			goto bad;
		h->reply_code = atol(hbp);
	} else if (word_eq(hbp, "GET"))
		h->hdr_type = HTTPREAD_HDR_TYPE_GET;
	else if (word_eq(hbp, "HEAD"))
		h->hdr_type = HTTPREAD_HDR_TYPE_HEAD;
	else if (word_eq(hbp, "POST"))
		h->hdr_type = HTTPREAD_HDR_TYPE_POST;
	else if (word_eq(hbp, "PUT"))
		h->hdr_type = HTTPREAD_HDR_TYPE_PUT;
	else if (word_eq(hbp, "DELETE"))
		h->hdr_type = HTTPREAD_HDR_TYPE_DELETE;
	else if (word_eq(hbp, "TRACE"))
		h->hdr_type = HTTPREAD_HDR_TYPE_TRACE;
	else if (word_eq(hbp, "CONNECT"))
		h->hdr_type = HTTPREAD_HDR_TYPE_CONNECT;
	else if (word_eq(hbp, "NOTIFY"))
		h->hdr_type = HTTPREAD_HDR_TYPE_NOTIFY;
	else if (word_eq(hbp, "M-SEARCH"))
		h->hdr_type = HTTPREAD_HDR_TYPE_M_SEARCH;
	else if (word_eq(hbp, "M-POST"))
		h->hdr_type = HTTPREAD_HDR_TYPE_M_POST;
	else if (word_eq(hbp, "SUBSCRIBE"))
		h->hdr_type = HTTPREAD_HDR_TYPE_SUBSCRIBE;
	else if (word_eq(hbp, "UNSUBSCRIBE"))
		h->hdr_type = HTTPREAD_HDR_TYPE_UNSUBSCRIBE;
	else {
	}

	if (standard_first_line) {
		char *rawuri;
		char *uri;
		/* skip type */
		while (isgraph(*hbp))
			hbp++;
		while (*hbp == ' ' || *hbp == '\t')
			hbp++;
		/* parse uri.
		 * Find length, allocate memory for translated
		 * copy, then translate by changing %<hex><hex>
		 * into represented value.
		 */
		rawuri = hbp;
		while (isgraph(*hbp))
			hbp++;
		h->uri = os_malloc((hbp - rawuri) + 1);
		if (h->uri == NULL)
			goto bad;
		uri = h->uri;
		while (rawuri < hbp) {
			int c = *rawuri;
			if (c == '%' &&
			    isxdigit(rawuri[1]) && isxdigit(rawuri[2])) {
				*uri++ = (hex_value(rawuri[1]) << 4) |
					hex_value(rawuri[2]);
				rawuri += 3;
			} else {
				*uri++ = c;
				rawuri++;
			}
		}
		*uri = 0;       /* null terminate */
		while (isgraph(*hbp))
			hbp++;
		while (*hbp == ' ' || *hbp == '\t')
			hbp++;
		/* get version */
		if (0 == strncmp(hbp, "HTTP/", 5)) {
			hbp += 5;
			if (hbp[0] == '1' && hbp[1] == '.' &&
			    isdigit(hbp[2]) && hbp[2] != '0')
				h->version = 1;
		}
	}
	/* skip rest of line */
	while (*hbp)
		if (*hbp++ == '\n')
			break;

	/* Remainder of lines are options, in any order;
	 * or empty line to terminate
	 */
	for (;;) {
		/* Empty line to terminate */
		if (hbp[0] == '\n' ||
		    (hbp[0] == '\r' && hbp[1] == '\n'))
			break;
		if (!isgraph(*hbp))
			goto bad;
		if (httpread_hdr_option_analyze(h, hbp))
			goto bad;
		/* skip line */
		while (*hbp)
			if (*hbp++ == '\n')
				break;
	}

	/* chunked overrides content-length always */
	if (h->chunked)
		h->got_content_length = 0;

	/* For some types, we should not try to read a body
	 * This is in addition to the application determining
	 * that we should not read a body.
	 */
	switch (h->hdr_type) {
	case HTTPREAD_HDR_TYPE_REPLY:
		/* Some codes can have a body and some not.
		 * For now, just assume that any other than 200
		 * do not...
		 */
		if (h->reply_code != 200)
			h->max_bytes = 0;
		break;
	case HTTPREAD_HDR_TYPE_GET:
	case HTTPREAD_HDR_TYPE_HEAD:
		/* in practice it appears that it is assumed
		 * that GETs have a body length of 0... ?
		 */
		if (h->chunked == 0 && h->got_content_length == 0)
			h->max_bytes = 0;
		break;
	case HTTPREAD_HDR_TYPE_POST:
	case HTTPREAD_HDR_TYPE_PUT:
	case HTTPREAD_HDR_TYPE_DELETE:
	case HTTPREAD_HDR_TYPE_TRACE:
	case HTTPREAD_HDR_TYPE_CONNECT:
	case HTTPREAD_HDR_TYPE_NOTIFY:
	case HTTPREAD_HDR_TYPE_M_SEARCH:
	case HTTPREAD_HDR_TYPE_M_POST:
	case HTTPREAD_HDR_TYPE_SUBSCRIBE:
	case HTTPREAD_HDR_TYPE_UNSUBSCRIBE:
	default:
		break;
	}

	return 0;

bad:
	/* Error */
	return -1;
}


/* httpread_read_handler -- called when socket ready to read
 *
 * Note: any extra data we read past end of transmitted file is ignored;
 * if we were to support keeping connections open for multiple files then
 * this would have to be addressed.
 */
static void httpread_read_handler(int sd, void *eloop_ctx, void *sock_ctx)
{
	struct httpread *h = sock_ctx;
	int nread;
	char *rbp;      /* pointer into read buffer */
	char *hbp;      /* pointer into header buffer */
	char *bbp;      /* pointer into body buffer */
	char readbuf[HTTPREAD_READBUF_SIZE];  /* temp use to read into */

	if (httpread_debug >= 20)
		wpa_printf(MSG_DEBUG, "ENTER httpread_read_handler(%p)", h);

	/* read some at a time, then search for the interal
	 * boundaries between header and data and etc.
	 */
	nread = read(h->sd, readbuf, sizeof(readbuf));
	if (nread < 0)
		goto bad;
	if (nread == 0) {
		/* end of transmission... this may be normal
		 * or may be an error... in some cases we can't
		 * tell which so we must assume it is normal then.
		 */
		if (!h->got_hdr) {
			/* Must at least have completed header */
			wpa_printf(MSG_DEBUG, "httpread premature eof(%p)", h);
			goto bad;
		}
		if (h->chunked || h->got_content_length) {
			/* Premature EOF; e.g. dropped connection */
			wpa_printf(MSG_DEBUG,
				   "httpread premature eof(%p) %d/%d",
				   h, h->body_nbytes,
				   h->content_length);
			goto bad;
		}
		/* No explicit length, hopefully we have all the data
		 * although dropped connections can cause false
		 * end
		 */
		if (httpread_debug >= 10)
			wpa_printf(MSG_DEBUG, "httpread ok eof(%p)", h);
			h->got_body = 1;
			goto got_file;
	}
	rbp = readbuf;

	/* Header consists of text lines (terminated by both CR and LF)
	 * and an empty line (CR LF only).
	 */
	if (!h->got_hdr) {
		hbp = h->hdr + h->hdr_nbytes;
		/* add to headers until:
		 *      -- we run out of data in read buffer
		 *      -- or, we run out of header buffer room
		 *      -- or, we get double CRLF in headers
		 */
		for (;;) {
			if (nread == 0)
				goto get_more;
			if (h->hdr_nbytes == HTTPREAD_HEADER_MAX_SIZE) {
				goto bad;
			}
			*hbp++ = *rbp++;
			nread--;
			h->hdr_nbytes++;
			if (h->hdr_nbytes >= 4 &&
			    hbp[-1] == '\n' &&
			    hbp[-2] == '\r' &&
			    hbp[-3] == '\n' &&
			    hbp[-4] == '\r' ) {
				h->got_hdr = 1;
				*hbp = 0;       /* null terminate */
				break;
			}
		}
		/* here we've just finished reading the header */
		if (httpread_hdr_analyze(h)) {
			wpa_printf(MSG_DEBUG, "httpread bad hdr(%p)", h);
			goto bad;
		}
		if (h->max_bytes == 0) {
			if (httpread_debug >= 10)
				wpa_printf(MSG_DEBUG,
					   "httpread no body hdr end(%p)", h);
			goto got_file;
		}
		if (h->got_content_length && h->content_length == 0) {
			if (httpread_debug >= 10)
				wpa_printf(MSG_DEBUG,
					   "httpread zero content length(%p)",
					   h);
			goto got_file;
		}
	}

	/* Certain types of requests never have data and so
	 * must be specially recognized.
	 */
	if (!os_strncasecmp(h->hdr, "SUBSCRIBE", 9) ||
	    !os_strncasecmp(h->hdr, "UNSUBSCRIBE", 11) ||
	    !os_strncasecmp(h->hdr, "HEAD", 4) ||
	    !os_strncasecmp(h->hdr, "GET", 3)) {
		if (!h->got_body) {
			if (httpread_debug >= 10)
				wpa_printf(MSG_DEBUG,
					   "httpread NO BODY for sp. type");
		}
		h->got_body = 1;
		goto got_file;
	}

	/* Data can be just plain binary data, or if "chunked"
	 * consists of chunks each with a header, ending with
	 * an ending header.
	 */
	if (nread == 0)
		goto get_more;
	if (!h->got_body) {
		/* Here to get (more of) body */
		/* ensure we have enough room for worst case for body
		 * plus a null termination character
		 */
		if (h->body_alloc_nbytes < (h->body_nbytes + nread + 1)) {
			char *new_body;
			int new_alloc_nbytes;

			if (h->body_nbytes >= h->max_bytes)
				goto bad;
			new_alloc_nbytes = h->body_alloc_nbytes +
				HTTPREAD_BODYBUF_DELTA;
			/* For content-length case, the first time
			 * through we allocate the whole amount
			 * we need.
			 */
			if (h->got_content_length &&
			    new_alloc_nbytes < (h->content_length + 1))
				new_alloc_nbytes = h->content_length + 1;
			if ((new_body = os_realloc(h->body, new_alloc_nbytes))
			    == NULL)
				goto bad;

			h->body = new_body;
			h->body_alloc_nbytes = new_alloc_nbytes;
		}
		/* add bytes */
		bbp = h->body + h->body_nbytes;
		for (;;) {
			int ncopy;
			/* See if we need to stop */
			if (h->chunked && h->in_chunk_data == 0) {
				/* in chunk header */
				char *cbp = h->body + h->chunk_start;
				if (bbp-cbp >= 2 && bbp[-2] == '\r' &&
				    bbp[-1] == '\n') {
					/* end of chunk hdr line */
					/* hdr line consists solely
					 * of a hex numeral and CFLF
					 */
					if (!isxdigit(*cbp))
						goto bad;
					h->chunk_size = strtoul(cbp, NULL, 16);
					/* throw away chunk header
					 * so we have only real data
					 */
					h->body_nbytes = h->chunk_start;
					bbp = cbp;
					if (h->chunk_size == 0) {
						/* end of chunking */
						/* trailer follows */
						h->in_trailer = 1;
						if (httpread_debug >= 20)
							wpa_printf(
								MSG_DEBUG,
								"httpread end chunks(%p)", h);
						break;
					}
					h->in_chunk_data = 1;
					/* leave chunk_start alone */
				}
			} else if (h->chunked) {
				/* in chunk data */
				if ((h->body_nbytes - h->chunk_start) ==
				    (h->chunk_size + 2)) {
					/* end of chunk reached,
					 * new chunk starts
					 */
					/* check chunk ended w/ CRLF
					 * which we'll throw away
					 */
					if (bbp[-1] == '\n' &&
					    bbp[-2] == '\r') {
					} else
						goto bad;
					h->body_nbytes -= 2;
					bbp -= 2;
					h->chunk_start = h->body_nbytes;
					h->in_chunk_data = 0;
					h->chunk_size = 0; /* just in case */
				}
			} else if (h->got_content_length &&
				   h->body_nbytes >= h->content_length) {
				h->got_body = 1;
				if (httpread_debug >= 10)
					wpa_printf(
						MSG_DEBUG,
						"httpread got content(%p)", h);
				goto got_file;
			}
			if (nread <= 0)
				break;
			/* Now transfer. Optimize using memcpy where we can. */
			if (h->chunked && h->in_chunk_data) {
				/* copy up to remainder of chunk data
				 * plus the required CR+LF at end
				 */
				ncopy = (h->chunk_start + h->chunk_size + 2) -
					h->body_nbytes;
			} else if (h->chunked) {
				/*in chunk header -- don't optimize */
				*bbp++ = *rbp++;
				nread--;
				h->body_nbytes++;
				continue;
			} else if (h->got_content_length) {
				ncopy = h->content_length - h->body_nbytes;
			} else {
				ncopy = nread;
			}
			/* Note: should never be 0 */
			if (ncopy > nread)
				ncopy = nread;
			os_memcpy(bbp, rbp, ncopy);
			bbp += ncopy;
			h->body_nbytes += ncopy;
			rbp += ncopy;
			nread -= ncopy;
		}       /* body copy loop */
	}       /* !got_body */
	if (h->chunked && h->in_trailer) {
		/* If "chunked" then there is always a trailer,
		 * consisting of zero or more non-empty lines
		 * ending with CR LF and then an empty line w/ CR LF.
		 * We do NOT support trailers except to skip them --
		 * this is supported (generally) by the http spec.
		 */
		bbp = h->body + h->body_nbytes;
		for (;;) {
			int c;
			if (nread <= 0)
				break;
			c = *rbp++;
			nread--;
			switch (h->trailer_state) {
			case trailer_line_begin:
				if (c == '\r')
					h->trailer_state = trailer_empty_cr;
				else
					h->trailer_state = trailer_nonempty;
				break;
			case trailer_empty_cr:
				/* end empty line */
				if (c == '\n') {
					h->trailer_state = trailer_line_begin;
					h->in_trailer = 0;
					if (httpread_debug >= 10)
						wpa_printf(
							MSG_DEBUG,
							"httpread got content(%p)", h);
					h->got_body = 1;
					goto got_file;
				}
				h->trailer_state = trailer_nonempty;
				break;
			case trailer_nonempty:
				if (c == '\r')
					h->trailer_state = trailer_nonempty_cr;
				break;
			case trailer_nonempty_cr:
				if (c == '\n')
					h->trailer_state = trailer_line_begin;
				else
					h->trailer_state = trailer_nonempty;
				break;
			}
		}
	}
	goto get_more;

bad:
	/* Error */
	wpa_printf(MSG_DEBUG, "httpread read/parse failure (%p)", h);
	(*h->cb)(h, h->cookie, HTTPREAD_EVENT_ERROR);
	return;

get_more:
	return;

got_file:
	if (httpread_debug >= 10)
		wpa_printf(MSG_DEBUG,
			   "httpread got file %d bytes type %d",
			   h->body_nbytes, h->hdr_type);
	/* Null terminate for convenience of some applications */
	if (h->body)
		h->body[h->body_nbytes] = 0; /* null terminate */
	h->got_file = 1;
	/* Assume that we do NOT support keeping connection alive,
	 * and just in case somehow we don't get destroyed right away,
	 * unregister now.
	 */
	if (h->sd_registered)
		eloop_unregister_sock(h->sd, EVENT_TYPE_READ);
	h->sd_registered = 0;
	/* The application can destroy us whenever they feel like...
	 * cancel timeout.
	 */
	if (h->to_registered)
		eloop_cancel_timeout(httpread_timeout_handler, NULL, h);
	h->to_registered = 0;
	(*h->cb)(h, h->cookie, HTTPREAD_EVENT_FILE_READY);
}


/* httpread_create -- start a new reading session making use of eloop.
 * The new instance will use the socket descriptor for reading (until
 * it gets a file and not after) but will not close the socket, even
 * when the instance is destroyed (the application must do that).
 * Return NULL on error.
 *
 * Provided that httpread_create successfully returns a handle,
 * the callback fnc is called to handle httpread_event events.
 * The caller should do destroy on any errors or unknown events.
 *
 * Pass max_bytes == 0 to not read body at all (required for e.g.
 * reply to HEAD request).
 */
struct httpread * httpread_create(
	int sd,	 /* descriptor of TCP socket to read from */
	void (*cb)(struct httpread *handle, void *cookie,
		   enum httpread_event e),  /* call on event */
	void *cookie,    /* pass to callback */
	int max_bytes,	  /* maximum body size else abort it */
	int timeout_seconds     /* 0; or total duration timeout period */
	)
{
	struct httpread *h = NULL;

	h = os_zalloc(sizeof(*h));
	if (h == NULL)
		goto fail;
	h->sd = sd;
	h->cb = cb;
	h->cookie = cookie;
	h->max_bytes = max_bytes;
	h->timeout_seconds = timeout_seconds;

	if (timeout_seconds > 0) {
		if (eloop_register_timeout(timeout_seconds, 0,
					   httpread_timeout_handler,
					   NULL, h)) {
			/* No way to recover (from malloc failure) */
			goto fail;
		}
		h->to_registered = 1;
	}
	if (eloop_register_sock(sd, EVENT_TYPE_READ, httpread_read_handler,
				NULL, h)) {
		/* No way to recover (from malloc failure) */
		goto fail;
	}
	h->sd_registered = 1;
	return h;

fail:

	/* Error */
	httpread_destroy(h);
	return NULL;
}


/* httpread_hdr_type_get -- When file is ready, returns header type. */
enum httpread_hdr_type httpread_hdr_type_get(struct httpread *h)
{
	return h->hdr_type;
}


/* httpread_uri_get -- When file is ready, uri_get returns (translated) URI
 * or possibly NULL (which would be an error).
 */
char * httpread_uri_get(struct httpread *h)
{
	return h->uri;
}


/* httpread_reply_code_get -- When reply is ready, returns reply code */
int httpread_reply_code_get(struct httpread *h)
{
	return h->reply_code;
}


/* httpread_length_get -- When file is ready, returns file length. */
int httpread_length_get(struct httpread *h)
{
	return h->body_nbytes;
}


/* httpread_data_get -- When file is ready, returns file content
 * with null byte appened.
 * Might return NULL in some error condition.
 */
void * httpread_data_get(struct httpread *h)
{
	return h->body ? h->body : "";
}


/* httpread_hdr_get -- When file is ready, returns header content
 * with null byte appended.
 * Might return NULL in some error condition.
 */
char * httpread_hdr_get(struct httpread *h)
{
	return h->hdr;
}


/* httpread_hdr_line_get -- When file is ready, returns pointer
 * to line within header content matching the given tag
 * (after the tag itself and any spaces/tabs).
 *
 * The tag should end with a colon for reliable matching.
 *
 * If not found, returns NULL;
 */
char * httpread_hdr_line_get(struct httpread *h, const char *tag)
{
	int tag_len = os_strlen(tag);
	char *hdr = h->hdr;
	hdr = os_strchr(hdr, '\n');
	if (hdr == NULL)
		return NULL;
	hdr++;
	for (;;) {
		if (!os_strncasecmp(hdr, tag, tag_len)) {
			hdr += tag_len;
			while (*hdr == ' ' || *hdr == '\t')
				hdr++;
			return hdr;
		}
		hdr = os_strchr(hdr, '\n');
		if (hdr == NULL)
			return NULL;
		hdr++;
	}
}
