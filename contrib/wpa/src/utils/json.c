/*
 * JavaScript Object Notation (JSON) parser (RFC7159)
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "base64.h"
#include "json.h"

#define JSON_MAX_DEPTH 10
#define JSON_MAX_TOKENS 500


void json_escape_string(char *txt, size_t maxlen, const char *data, size_t len)
{
	char *end = txt + maxlen;
	size_t i;

	for (i = 0; i < len; i++) {
		if (txt + 4 >= end)
			break;

		switch (data[i]) {
		case '\"':
			*txt++ = '\\';
			*txt++ = '\"';
			break;
		case '\\':
			*txt++ = '\\';
			*txt++ = '\\';
			break;
		case '\n':
			*txt++ = '\\';
			*txt++ = 'n';
			break;
		case '\r':
			*txt++ = '\\';
			*txt++ = 'r';
			break;
		case '\t':
			*txt++ = '\\';
			*txt++ = 't';
			break;
		default:
			if (data[i] >= 32 && data[i] <= 126) {
				*txt++ = data[i];
			} else {
				txt += os_snprintf(txt, end - txt, "\\u%04x",
						   data[i]);
			}
			break;
		}
	}

	*txt = '\0';
}


static char * json_parse_string(const char **json_pos, const char *end)
{
	const char *pos = *json_pos;
	char *str, *spos, *s_end;
	size_t max_len, buf_len;
	u8 bin[2];

	pos++; /* skip starting quote */

	max_len = end - pos + 1;
	buf_len = max_len > 10 ? 10 : max_len;
	str = os_malloc(buf_len);
	if (!str)
		return NULL;
	spos = str;
	s_end = str + buf_len;

	for (; pos < end; pos++) {
		if (buf_len < max_len && s_end - spos < 3) {
			char *tmp;
			int idx;

			idx = spos - str;
			buf_len *= 2;
			if (buf_len > max_len)
				buf_len = max_len;
			tmp = os_realloc(str, buf_len);
			if (!tmp)
				goto fail;
			str = tmp;
			spos = str + idx;
			s_end = str + buf_len;
		}

		switch (*pos) {
		case '\"': /* end string */
			*spos = '\0';
			/* caller will move to the next position */
			*json_pos = pos;
			return str;
		case '\\':
			pos++;
			switch (*pos) {
			case '"':
			case '\\':
			case '/':
				*spos++ = *pos;
				break;
			case 'n':
				*spos++ = '\n';
				break;
			case 'r':
				*spos++ = '\r';
				break;
			case 't':
				*spos++ = '\t';
				break;
			case 'u':
				if (end - pos < 5 ||
				    hexstr2bin(pos + 1, bin, 2) < 0 ||
				    bin[1] == 0x00) {
					wpa_printf(MSG_DEBUG,
						   "JSON: Invalid \\u escape");
					goto fail;
				}
				if (bin[0] == 0x00) {
					*spos++ = bin[1];
				} else {
					*spos++ = bin[0];
					*spos++ = bin[1];
				}
				pos += 4;
				break;
			default:
				wpa_printf(MSG_DEBUG,
					   "JSON: Unknown escape '%c'", *pos);
				goto fail;
			}
			break;
		default:
			*spos++ = *pos;
			break;
		}
	}

fail:
	os_free(str);
	return NULL;
}


static int json_parse_number(const char **json_pos, const char *end,
			     int *ret_val)
{
	const char *pos = *json_pos;
	size_t len;
	char *str;

	for (; pos < end; pos++) {
		if (*pos != '-' && (*pos < '0' || *pos > '9')) {
			pos--;
			break;
		}
	}
	if (pos < *json_pos)
		return -1;
	len = pos - *json_pos + 1;
	str = os_malloc(len + 1);
	if (!str)
		return -1;
	os_memcpy(str, *json_pos, len);
	str[len] = '\0';

	*ret_val = atoi(str);
	os_free(str);
	*json_pos = pos;
	return 0;
}


static int json_check_tree_state(struct json_token *token)
{
	if (!token)
		return 0;
	if (json_check_tree_state(token->child) < 0 ||
	    json_check_tree_state(token->sibling) < 0)
		return -1;
	if (token->state != JSON_COMPLETED) {
		wpa_printf(MSG_DEBUG,
			   "JSON: Unexpected token state %d (name=%s type=%d)",
			   token->state, token->name ? token->name : "N/A",
			   token->type);
		return -1;
	}
	return 0;
}


static struct json_token * json_alloc_token(unsigned int *tokens)
{
	(*tokens)++;
	if (*tokens > JSON_MAX_TOKENS) {
		wpa_printf(MSG_DEBUG, "JSON: Maximum token limit exceeded");
		return NULL;
	}
	return os_zalloc(sizeof(struct json_token));
}


struct json_token * json_parse(const char *data, size_t data_len)
{
	struct json_token *root = NULL, *curr_token = NULL, *token = NULL;
	const char *pos, *end;
	char *str;
	int num;
	unsigned int depth = 0;
	unsigned int tokens = 0;

	pos = data;
	end = data + data_len;

	for (; pos < end; pos++) {
		switch (*pos) {
		case '[': /* start array */
		case '{': /* start object */
			if (!curr_token) {
				token = json_alloc_token(&tokens);
				if (!token)
					goto fail;
				if (!root)
					root = token;
			} else if (curr_token->state == JSON_WAITING_VALUE) {
				token = curr_token;
			} else if (curr_token->parent &&
				   curr_token->parent->type == JSON_ARRAY &&
				   curr_token->parent->state == JSON_STARTED &&
				   curr_token->state == JSON_EMPTY) {
				token = curr_token;
			} else {
				wpa_printf(MSG_DEBUG,
					   "JSON: Invalid state for start array/object");
				goto fail;
			}
			depth++;
			if (depth > JSON_MAX_DEPTH) {
				wpa_printf(MSG_DEBUG,
					   "JSON: Max depth exceeded");
				goto fail;
			}
			token->type = *pos == '[' ? JSON_ARRAY : JSON_OBJECT;
			token->state = JSON_STARTED;
			token->child = json_alloc_token(&tokens);
			if (!token->child)
				goto fail;
			curr_token = token->child;
			curr_token->parent = token;
			curr_token->state = JSON_EMPTY;
			break;
		case ']': /* end array */
		case '}': /* end object */
			if (!curr_token || !curr_token->parent ||
			    curr_token->parent->state != JSON_STARTED) {
				wpa_printf(MSG_DEBUG,
					   "JSON: Invalid state for end array/object");
				goto fail;
			}
			depth--;
			curr_token = curr_token->parent;
			if ((*pos == ']' &&
			     curr_token->type != JSON_ARRAY) ||
			    (*pos == '}' &&
			     curr_token->type != JSON_OBJECT)) {
				wpa_printf(MSG_DEBUG,
					   "JSON: Array/Object mismatch");
				goto fail;
			}
			if (curr_token->child->state == JSON_EMPTY &&
			    !curr_token->child->child &&
			    !curr_token->child->sibling) {
				/* Remove pending child token since the
				 * array/object was empty. */
				json_free(curr_token->child);
				curr_token->child = NULL;
			}
			curr_token->state = JSON_COMPLETED;
			break;
		case '\"': /* string */
			str = json_parse_string(&pos, end);
			if (!str)
				goto fail;
			if (!curr_token) {
				token = json_alloc_token(&tokens);
				if (!token)
					goto fail;
				token->type = JSON_STRING;
				token->string = str;
				token->state = JSON_COMPLETED;
			} else if (curr_token->parent &&
				   curr_token->parent->type == JSON_ARRAY &&
				   curr_token->parent->state == JSON_STARTED &&
				   curr_token->state == JSON_EMPTY) {
				curr_token->string = str;
				curr_token->state = JSON_COMPLETED;
				curr_token->type = JSON_STRING;
				wpa_printf(MSG_MSGDUMP,
					   "JSON: String value: '%s'",
					   curr_token->string);
			} else if (curr_token->state == JSON_EMPTY) {
				curr_token->type = JSON_VALUE;
				curr_token->name = str;
				curr_token->state = JSON_STARTED;
			} else if (curr_token->state == JSON_WAITING_VALUE) {
				curr_token->string = str;
				curr_token->state = JSON_COMPLETED;
				curr_token->type = JSON_STRING;
				wpa_printf(MSG_MSGDUMP,
					   "JSON: String value: '%s' = '%s'",
					   curr_token->name,
					   curr_token->string);
			} else {
				wpa_printf(MSG_DEBUG,
					   "JSON: Invalid state for a string");
				os_free(str);
				goto fail;
			}
			break;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			/* ignore whitespace */
			break;
		case ':': /* name/value separator */
			if (!curr_token || curr_token->state != JSON_STARTED)
				goto fail;
			curr_token->state = JSON_WAITING_VALUE;
			break;
		case ',': /* member separator */
			if (!curr_token)
				goto fail;
			curr_token->sibling = json_alloc_token(&tokens);
			if (!curr_token->sibling)
				goto fail;
			curr_token->sibling->parent = curr_token->parent;
			curr_token = curr_token->sibling;
			curr_token->state = JSON_EMPTY;
			break;
		case 't': /* true */
		case 'f': /* false */
		case 'n': /* null */
			if (!((end - pos >= 4 &&
			       os_strncmp(pos, "true", 4) == 0) ||
			      (end - pos >= 5 &&
			       os_strncmp(pos, "false", 5) == 0) ||
			      (end - pos >= 4 &&
			       os_strncmp(pos, "null", 4) == 0))) {
				wpa_printf(MSG_DEBUG,
					   "JSON: Invalid literal name");
				goto fail;
			}
			if (!curr_token) {
				token = json_alloc_token(&tokens);
				if (!token)
					goto fail;
				curr_token = token;
			} else if (curr_token->state == JSON_WAITING_VALUE) {
				wpa_printf(MSG_MSGDUMP,
					   "JSON: Literal name: '%s' = %c",
					   curr_token->name, *pos);
			} else if (curr_token->parent &&
				   curr_token->parent->type == JSON_ARRAY &&
				   curr_token->parent->state == JSON_STARTED &&
				   curr_token->state == JSON_EMPTY) {
				wpa_printf(MSG_MSGDUMP,
					   "JSON: Literal name: %c", *pos);
			} else {
				wpa_printf(MSG_DEBUG,
					   "JSON: Invalid state for a literal name");
				goto fail;
			}
			switch (*pos) {
			case 't':
				curr_token->type = JSON_BOOLEAN;
				curr_token->number = 1;
				pos += 3;
				break;
			case 'f':
				curr_token->type = JSON_BOOLEAN;
				curr_token->number = 0;
				pos += 4;
				break;
			case 'n':
				curr_token->type = JSON_NULL;
				pos += 3;
				break;
			}
			curr_token->state = JSON_COMPLETED;
			break;
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			/* number */
			if (json_parse_number(&pos, end, &num) < 0)
				goto fail;
			if (!curr_token) {
				token = json_alloc_token(&tokens);
				if (!token)
					goto fail;
				token->type = JSON_NUMBER;
				token->number = num;
				token->state = JSON_COMPLETED;
			} else if (curr_token->state == JSON_WAITING_VALUE) {
				curr_token->number = num;
				curr_token->state = JSON_COMPLETED;
				curr_token->type = JSON_NUMBER;
				wpa_printf(MSG_MSGDUMP,
					   "JSON: Number value: '%s' = '%d'",
					   curr_token->name,
					   curr_token->number);
			} else if (curr_token->parent &&
				   curr_token->parent->type == JSON_ARRAY &&
				   curr_token->parent->state == JSON_STARTED &&
				   curr_token->state == JSON_EMPTY) {
				curr_token->number = num;
				curr_token->state = JSON_COMPLETED;
				curr_token->type = JSON_NUMBER;
				wpa_printf(MSG_MSGDUMP,
					   "JSON: Number value: %d",
					   curr_token->number);
			} else {
				wpa_printf(MSG_DEBUG,
					   "JSON: Invalid state for a number");
				goto fail;
			}
			break;
		default:
			wpa_printf(MSG_DEBUG,
				   "JSON: Unexpected JSON character: %c", *pos);
			goto fail;
		}

		if (!root)
			root = token;
		if (!curr_token)
			curr_token = token;
	}

	if (json_check_tree_state(root) < 0) {
		wpa_printf(MSG_DEBUG, "JSON: Incomplete token in the tree");
		goto fail;
	}

	return root;
fail:
	wpa_printf(MSG_DEBUG, "JSON: Parsing failed");
	json_free(root);
	return NULL;
}


void json_free(struct json_token *json)
{
	if (!json)
		return;
	json_free(json->child);
	json_free(json->sibling);
	os_free(json->name);
	os_free(json->string);
	os_free(json);
}


struct json_token * json_get_member(struct json_token *json, const char *name)
{
	struct json_token *token, *ret = NULL;

	if (!json || json->type != JSON_OBJECT)
		return NULL;
	/* Return last matching entry */
	for (token = json->child; token; token = token->sibling) {
		if (token->name && os_strcmp(token->name, name) == 0)
			ret = token;
	}
	return ret;
}


struct wpabuf * json_get_member_base64url(struct json_token *json,
					  const char *name)
{
	struct json_token *token;
	unsigned char *buf;
	size_t buflen;
	struct wpabuf *ret;

	token = json_get_member(json, name);
	if (!token || token->type != JSON_STRING)
		return NULL;
	buf = base64_url_decode((const unsigned char *) token->string,
				os_strlen(token->string), &buflen);
	if (!buf)
		return NULL;
	ret = wpabuf_alloc_ext_data(buf, buflen);
	if (!ret)
		os_free(buf);

	return ret;
}


static const char * json_type_str(enum json_type type)
{
	switch (type) {
	case JSON_VALUE:
		return "VALUE";
	case JSON_OBJECT:
		return "OBJECT";
	case JSON_ARRAY:
		return "ARRAY";
	case JSON_STRING:
		return "STRING";
	case JSON_NUMBER:
		return "NUMBER";
	case JSON_BOOLEAN:
		return "BOOLEAN";
	case JSON_NULL:
		return "NULL";
	}
	return "??";
}


static void json_print_token(struct json_token *token, int depth,
			     char *buf, size_t buflen)
{
	size_t len;
	int ret;

	if (!token)
		return;
	len = os_strlen(buf);
	ret = os_snprintf(buf + len, buflen - len, "[%d:%s:%s]",
			  depth, json_type_str(token->type),
			  token->name ? token->name : "");
	if (os_snprintf_error(buflen - len, ret)) {
		buf[len] = '\0';
		return;
	}
	json_print_token(token->child, depth + 1, buf, buflen);
	json_print_token(token->sibling, depth, buf, buflen);
}


void json_print_tree(struct json_token *root, char *buf, size_t buflen)
{
	buf[0] = '\0';
	json_print_token(root, 1, buf, buflen);
}
