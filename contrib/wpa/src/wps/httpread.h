/*
 * httpread - Manage reading file(s) from HTTP/TCP socket
 * Author: Ted Merrill
 * Copyright 2008 Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HTTPREAD_H
#define HTTPREAD_H

/* event types (passed to callback) */
enum httpread_event {
	HTTPREAD_EVENT_FILE_READY = 1,        /* including reply ready */
	HTTPREAD_EVENT_TIMEOUT = 2,
	HTTPREAD_EVENT_ERROR = 3      /* misc. error, esp malloc error */
};


/* header type detected
 * available to callback via call to httpread_reply_code_get()
 */
enum httpread_hdr_type {
	HTTPREAD_HDR_TYPE_UNKNOWN = 0,      /* none of the following */
	HTTPREAD_HDR_TYPE_REPLY = 1,        /* hdr begins w/ HTTP/ */
	HTTPREAD_HDR_TYPE_GET = 2,          /* hdr begins with GET<sp> */
	HTTPREAD_HDR_TYPE_HEAD = 3,         /* hdr begins with HEAD<sp> */
	HTTPREAD_HDR_TYPE_POST = 4,         /* hdr begins with POST<sp> */
	HTTPREAD_HDR_TYPE_PUT = 5,          /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_DELETE = 6,       /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_TRACE = 7,        /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_CONNECT = 8,      /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_NOTIFY = 9,       /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_M_SEARCH = 10,    /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_M_POST = 11,      /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_SUBSCRIBE = 12,   /* hdr begins with ... */
	HTTPREAD_HDR_TYPE_UNSUBSCRIBE = 13, /* hdr begins with ... */

	HTTPREAD_N_HDR_TYPES    /* keep last */
};


/* control instance -- opaque struct declaration
 */
struct httpread;


/* httpread_destroy -- if h is non-NULL, clean up
 * This must eventually be called by the application following
 * call of the application's callback and may be called
 * earlier if desired.
 */
void httpread_destroy(struct httpread *h);

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
	int sd,         /* descriptor of TCP socket to read from */
	void (*cb)(struct httpread *handle, void *cookie,
		    enum httpread_event e),  /* call on event */
	void *cookie,    /* pass to callback */
	int max_bytes,          /* maximum file size else abort it */
	int timeout_seconds     /* 0; or total duration timeout period */
	);

/* httpread_hdr_type_get -- When file is ready, returns header type.
 */
enum httpread_hdr_type httpread_hdr_type_get(struct httpread *h);


/* httpread_uri_get -- When file is ready, uri_get returns (translated) URI
 * or possibly NULL (which would be an error).
 */
char *httpread_uri_get(struct httpread *h);

/* httpread_reply_code_get -- When reply is ready, returns reply code */
int httpread_reply_code_get(struct httpread *h);


/* httpread_length_get -- When file is ready, returns file length. */
int httpread_length_get(struct httpread *h);

/* httpread_data_get -- When file is ready, returns file content
 * with null byte appened.
 * Might return NULL in some error condition.
 */
void * httpread_data_get(struct httpread *h);

/* httpread_hdr_get -- When file is ready, returns header content
 * with null byte appended.
 * Might return NULL in some error condition.
 */
char * httpread_hdr_get(struct httpread *h);

/* httpread_hdr_line_get -- When file is ready, returns pointer
 * to line within header content matching the given tag
 * (after the tag itself and any spaces/tabs).
 *
 * The tag should end with a colon for reliable matching.
 *
 * If not found, returns NULL;
 */
char * httpread_hdr_line_get(struct httpread *h, const char *tag);

#endif /* HTTPREAD_H */
