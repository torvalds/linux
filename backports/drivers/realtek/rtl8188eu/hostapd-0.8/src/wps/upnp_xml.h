/*
 * UPnP XML helper routines
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#ifndef UPNP_XML_H
#define UPNP_XML_H

#include "http.h"

void xml_data_encode(struct wpabuf *buf, const char *data, int len);
void xml_add_tagged_data(struct wpabuf *buf, const char *tag,
			 const char *data);
char * xml_get_first_item(const char *doc, const char *item);
struct wpabuf * xml_get_base64_item(const char *data, const char *name,
				    enum http_reply_code *ret);

#endif /* UPNP_XML_H */
