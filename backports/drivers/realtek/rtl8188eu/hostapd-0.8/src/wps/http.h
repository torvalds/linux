/*
 * HTTP for WPS
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#ifndef HTTP_H
#define HTTP_H

enum http_reply_code {
	HTTP_OK = 200,
	HTTP_BAD_REQUEST = 400,
	UPNP_INVALID_ACTION = 401,
	UPNP_INVALID_ARGS = 402,
	HTTP_NOT_FOUND = 404,
	HTTP_PRECONDITION_FAILED = 412,
	HTTP_INTERNAL_SERVER_ERROR = 500,
	HTTP_UNIMPLEMENTED = 501,
	UPNP_ACTION_FAILED = 501,
	UPNP_ARG_VALUE_INVALID = 600,
	UPNP_ARG_VALUE_OUT_OF_RANGE = 601,
	UPNP_OUT_OF_MEMORY = 603
};

#endif /* HTTP_H */
