/*
 * JavaScript Object Notation (JSON) parser (RFC7159)
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef JSON_H
#define JSON_H

struct json_token {
	enum json_type {
		JSON_VALUE,
		JSON_OBJECT,
		JSON_ARRAY,
		JSON_STRING,
		JSON_NUMBER,
		JSON_BOOLEAN,
		JSON_NULL,
	} type;
	enum json_parsing_state {
		JSON_EMPTY,
		JSON_STARTED,
		JSON_WAITING_VALUE,
		JSON_COMPLETED,
	} state;
	char *name;
	char *string;
	int number;
	struct json_token *parent, *child, *sibling;
};

void json_escape_string(char *txt, size_t maxlen, const char *data, size_t len);
struct json_token * json_parse(const char *data, size_t data_len);
void json_free(struct json_token *json);
struct json_token * json_get_member(struct json_token *json, const char *name);
struct wpabuf * json_get_member_base64url(struct json_token *json,
					  const char *name);
void json_print_tree(struct json_token *root, char *buf, size_t buflen);

#endif /* JSON_H */
