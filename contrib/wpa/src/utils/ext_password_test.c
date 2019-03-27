/*
 * External password backend
 * Copyright (c) 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "ext_password_i.h"


struct ext_password_test_data {
	char *params;
};


static void * ext_password_test_init(const char *params)
{
	struct ext_password_test_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	if (params)
		data->params = os_strdup(params);

	return data;
}


static void ext_password_test_deinit(void *ctx)
{
	struct ext_password_test_data *data = ctx;

	str_clear_free(data->params);
	os_free(data);
}


static struct wpabuf * ext_password_test_get(void *ctx, const char *name)
{
	struct ext_password_test_data *data = ctx;
	char *pos, *pos2;
	size_t nlen;

	wpa_printf(MSG_DEBUG, "EXT PW TEST: get(%s)", name);

	pos = data->params;
	if (pos == NULL)
		return NULL;
	nlen = os_strlen(name);

	while (pos && *pos) {
		if (os_strncmp(pos, name, nlen) == 0 && pos[nlen] == '=') {
			struct wpabuf *buf;
			pos += nlen + 1;
			pos2 = pos;
			while (*pos2 != '|' && *pos2 != '\0')
				pos2++;
			buf = ext_password_alloc(pos2 - pos);
			if (buf == NULL)
				return NULL;
			wpabuf_put_data(buf, pos, pos2 - pos);
			wpa_hexdump_ascii_key(MSG_DEBUG, "EXT PW TEST: value",
					      wpabuf_head(buf),
					      wpabuf_len(buf));
			return buf;
		}

		pos = os_strchr(pos + 1, '|');
		if (pos)
			pos++;
	}

	wpa_printf(MSG_DEBUG, "EXT PW TEST: get(%s) - not found", name);

	return NULL;
}


const struct ext_password_backend ext_password_test = {
	.name = "test",
	.init = ext_password_test_init,
	.deinit = ext_password_test_deinit,
	.get = ext_password_test_get,
};
