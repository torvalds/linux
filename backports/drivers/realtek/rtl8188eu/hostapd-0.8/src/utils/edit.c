/*
 * Command line editing and history
 * Copyright (c) 2010, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include <termios.h>

#include "common.h"
#include "eloop.h"
#include "list.h"
#include "edit.h"

#define CMD_BUF_LEN 256
static char cmdbuf[CMD_BUF_LEN];
static int cmdbuf_pos = 0;
static int cmdbuf_len = 0;

#define HISTORY_MAX 100

struct edit_history {
	struct dl_list list;
	char str[1];
};

static struct dl_list history_list;
static struct edit_history *history_curr;

static void *edit_cb_ctx;
static void (*edit_cmd_cb)(void *ctx, char *cmd);
static void (*edit_eof_cb)(void *ctx);
static char ** (*edit_completion_cb)(void *ctx, const char *cmd, int pos) =
	NULL;

static struct termios prevt, newt;


#define CLEAR_END_LINE "\e[K"


void edit_clear_line(void)
{
	int i;
	putchar('\r');
	for (i = 0; i < cmdbuf_len + 2; i++)
		putchar(' ');
}


static void move_start(void)
{
	cmdbuf_pos = 0;
	edit_redraw();
}


static void move_end(void)
{
	cmdbuf_pos = cmdbuf_len;
	edit_redraw();
}


static void move_left(void)
{
	if (cmdbuf_pos > 0) {
		cmdbuf_pos--;
		edit_redraw();
	}
}


static void move_right(void)
{
	if (cmdbuf_pos < cmdbuf_len) {
		cmdbuf_pos++;
		edit_redraw();
	}
}


static void move_word_left(void)
{
	while (cmdbuf_pos > 0 && cmdbuf[cmdbuf_pos - 1] == ' ')
		cmdbuf_pos--;
	while (cmdbuf_pos > 0 && cmdbuf[cmdbuf_pos - 1] != ' ')
		cmdbuf_pos--;
	edit_redraw();
}


static void move_word_right(void)
{
	while (cmdbuf_pos < cmdbuf_len && cmdbuf[cmdbuf_pos] == ' ')
		cmdbuf_pos++;
	while (cmdbuf_pos < cmdbuf_len && cmdbuf[cmdbuf_pos] != ' ')
		cmdbuf_pos++;
	edit_redraw();
}


static void delete_left(void)
{
	if (cmdbuf_pos == 0)
		return;

	edit_clear_line();
	os_memmove(cmdbuf + cmdbuf_pos - 1, cmdbuf + cmdbuf_pos,
		   cmdbuf_len - cmdbuf_pos);
	cmdbuf_pos--;
	cmdbuf_len--;
	edit_redraw();
}


static void delete_current(void)
{
	if (cmdbuf_pos == cmdbuf_len)
		return;

	edit_clear_line();
	os_memmove(cmdbuf + cmdbuf_pos, cmdbuf + cmdbuf_pos + 1,
		   cmdbuf_len - cmdbuf_pos);
	cmdbuf_len--;
	edit_redraw();
}


static void delete_word(void)
{
	int pos;

	edit_clear_line();
	pos = cmdbuf_pos;
	while (pos > 0 && cmdbuf[pos - 1] == ' ')
		pos--;
	while (pos > 0 && cmdbuf[pos - 1] != ' ')
		pos--;
	os_memmove(cmdbuf + pos, cmdbuf + cmdbuf_pos, cmdbuf_len - cmdbuf_pos);
	cmdbuf_len -= cmdbuf_pos - pos;
	cmdbuf_pos = pos;
	edit_redraw();
}


static void clear_left(void)
{
	if (cmdbuf_pos == 0)
		return;

	edit_clear_line();
	os_memmove(cmdbuf, cmdbuf + cmdbuf_pos, cmdbuf_len - cmdbuf_pos);
	cmdbuf_len -= cmdbuf_pos;
	cmdbuf_pos = 0;
	edit_redraw();
}


static void clear_right(void)
{
	if (cmdbuf_pos == cmdbuf_len)
		return;

	edit_clear_line();
	cmdbuf_len = cmdbuf_pos;
	edit_redraw();
}


static void history_add(const char *str)
{
	struct edit_history *h, *match = NULL, *last = NULL;
	size_t len, count = 0;

	if (str[0] == '\0')
		return;

	dl_list_for_each(h, &history_list, struct edit_history, list) {
		if (os_strcmp(str, h->str) == 0) {
			match = h;
			break;
		}
		last = h;
		count++;
	}

	if (match) {
		dl_list_del(&h->list);
		dl_list_add(&history_list, &h->list);
		history_curr = h;
		return;
	}

	if (count >= HISTORY_MAX && last) {
		dl_list_del(&last->list);
		os_free(last);
	}

	len = os_strlen(str);
	h = os_zalloc(sizeof(*h) + len);
	if (h == NULL)
		return;
	dl_list_add(&history_list, &h->list);
	os_strlcpy(h->str, str, len + 1);
	history_curr = h;
}


static void history_use(void)
{
	edit_clear_line();
	cmdbuf_len = cmdbuf_pos = os_strlen(history_curr->str);
	os_memcpy(cmdbuf, history_curr->str, cmdbuf_len);
	edit_redraw();
}


static void history_prev(void)
{
	if (history_curr == NULL)
		return;

	if (history_curr ==
	    dl_list_first(&history_list, struct edit_history, list)) {
		cmdbuf[cmdbuf_len] = '\0';
		history_add(cmdbuf);
	}

	history_use();

	if (history_curr ==
	    dl_list_last(&history_list, struct edit_history, list))
		return;

	history_curr = dl_list_entry(history_curr->list.next,
				     struct edit_history, list);
}


static void history_next(void)
{
	if (history_curr == NULL ||
	    history_curr ==
	    dl_list_first(&history_list, struct edit_history, list))
		return;

	history_curr = dl_list_entry(history_curr->list.prev,
				     struct edit_history, list);
	history_use();
}


static void history_read(const char *fname)
{
	FILE *f;
	char buf[CMD_BUF_LEN], *pos;

	f = fopen(fname, "r");
	if (f == NULL)
		return;

	while (fgets(buf, CMD_BUF_LEN, f)) {
		for (pos = buf; *pos; pos++) {
			if (*pos == '\r' || *pos == '\n') {
				*pos = '\0';
				break;
			}
		}
		history_add(buf);
	}

	fclose(f);
}


static void history_write(const char *fname,
			  int (*filter_cb)(void *ctx, const char *cmd))
{
	FILE *f;
	struct edit_history *h;

	f = fopen(fname, "w");
	if (f == NULL)
		return;

	dl_list_for_each_reverse(h, &history_list, struct edit_history, list) {
		if (filter_cb && filter_cb(edit_cb_ctx, h->str))
			continue;
		fprintf(f, "%s\n", h->str);
	}

	fclose(f);
}


static void history_debug_dump(void)
{
	struct edit_history *h;
	edit_clear_line();
	printf("\r");
	dl_list_for_each_reverse(h, &history_list, struct edit_history, list)
		printf("%s%s\n", h == history_curr ? "[C]" : "", h->str);
	edit_redraw();
}


static void insert_char(int c)
{
	if (cmdbuf_len >= (int) sizeof(cmdbuf) - 1)
		return;
	if (cmdbuf_len == cmdbuf_pos) {
		cmdbuf[cmdbuf_pos++] = c;
		cmdbuf_len++;
		putchar(c);
		fflush(stdout);
	} else {
		os_memmove(cmdbuf + cmdbuf_pos + 1, cmdbuf + cmdbuf_pos,
			   cmdbuf_len - cmdbuf_pos);
		cmdbuf[cmdbuf_pos++] = c;
		cmdbuf_len++;
		edit_redraw();
	}
}


static void process_cmd(void)
{

	if (cmdbuf_len == 0) {
		printf("\n> ");
		fflush(stdout);
		return;
	}
	printf("\n");
	cmdbuf[cmdbuf_len] = '\0';
	history_add(cmdbuf);
	cmdbuf_pos = 0;
	cmdbuf_len = 0;
	edit_cmd_cb(edit_cb_ctx, cmdbuf);
	printf("> ");
	fflush(stdout);
}


static void free_completions(char **c)
{
	int i;
	if (c == NULL)
		return;
	for (i = 0; c[i]; i++)
		os_free(c[i]);
	os_free(c);
}


static int filter_strings(char **c, char *str, size_t len)
{
	int i, j;

	for (i = 0, j = 0; c[j]; j++) {
		if (os_strncasecmp(c[j], str, len) == 0) {
			if (i != j) {
				c[i] = c[j];
				c[j] = NULL;
			}
			i++;
		} else {
			os_free(c[j]);
			c[j] = NULL;
		}
	}
	c[i] = NULL;
	return i;
}


static int common_len(const char *a, const char *b)
{
	int len = 0;
	while (a[len] && a[len] == b[len])
		len++;
	return len;
}


static int max_common_length(char **c)
{
	int len, i;

	len = os_strlen(c[0]);
	for (i = 1; c[i]; i++) {
		int same = common_len(c[0], c[i]);
		if (same < len)
			len = same;
	}

	return len;
}


static int cmp_str(const void *a, const void *b)
{
	return os_strcmp(* (const char **) a, * (const char **) b);
}

static void complete(int list)
{
	char **c;
	int i, len, count;
	int start, end;
	int room, plen, add_space;

	if (edit_completion_cb == NULL)
		return;

	cmdbuf[cmdbuf_len] = '\0';
	c = edit_completion_cb(edit_cb_ctx, cmdbuf, cmdbuf_pos);
	if (c == NULL)
		return;

	end = cmdbuf_pos;
	start = end;
	while (start > 0 && cmdbuf[start - 1] != ' ')
		start--;
	plen = end - start;

	count = filter_strings(c, &cmdbuf[start], plen);
	if (count == 0) {
		free_completions(c);
		return;
	}

	len = max_common_length(c);
	if (len <= plen && count > 1) {
		if (list) {
			qsort(c, count, sizeof(char *), cmp_str);
			edit_clear_line();
			printf("\r");
			for (i = 0; c[i]; i++)
				printf("%s%s", i > 0 ? " " : "", c[i]);
			printf("\n");
			edit_redraw();
		}
		free_completions(c);
		return;
	}
	len -= plen;

	room = sizeof(cmdbuf) - 1 - cmdbuf_len;
	if (room < len)
		len = room;
	add_space = count == 1 && len < room;

	os_memmove(cmdbuf + cmdbuf_pos + len + add_space, cmdbuf + cmdbuf_pos,
		   cmdbuf_len - cmdbuf_pos);
	os_memcpy(&cmdbuf[cmdbuf_pos - plen], c[0], plen + len);
	if (add_space)
		cmdbuf[cmdbuf_pos + len] = ' ';

	cmdbuf_pos += len + add_space;
	cmdbuf_len += len + add_space;

	edit_redraw();

	free_completions(c);
}


enum edit_key_code {
	EDIT_KEY_NONE = 256,
	EDIT_KEY_TAB,
	EDIT_KEY_UP,
	EDIT_KEY_DOWN,
	EDIT_KEY_RIGHT,
	EDIT_KEY_LEFT,
	EDIT_KEY_ENTER,
	EDIT_KEY_BACKSPACE,
	EDIT_KEY_INSERT,
	EDIT_KEY_DELETE,
	EDIT_KEY_HOME,
	EDIT_KEY_END,
	EDIT_KEY_PAGE_UP,
	EDIT_KEY_PAGE_DOWN,
	EDIT_KEY_F1,
	EDIT_KEY_F2,
	EDIT_KEY_F3,
	EDIT_KEY_F4,
	EDIT_KEY_F5,
	EDIT_KEY_F6,
	EDIT_KEY_F7,
	EDIT_KEY_F8,
	EDIT_KEY_F9,
	EDIT_KEY_F10,
	EDIT_KEY_F11,
	EDIT_KEY_F12,
	EDIT_KEY_CTRL_UP,
	EDIT_KEY_CTRL_DOWN,
	EDIT_KEY_CTRL_RIGHT,
	EDIT_KEY_CTRL_LEFT,
	EDIT_KEY_CTRL_A,
	EDIT_KEY_CTRL_B,
	EDIT_KEY_CTRL_D,
	EDIT_KEY_CTRL_E,
	EDIT_KEY_CTRL_F,
	EDIT_KEY_CTRL_G,
	EDIT_KEY_CTRL_H,
	EDIT_KEY_CTRL_J,
	EDIT_KEY_CTRL_K,
	EDIT_KEY_CTRL_L,
	EDIT_KEY_CTRL_N,
	EDIT_KEY_CTRL_O,
	EDIT_KEY_CTRL_P,
	EDIT_KEY_CTRL_R,
	EDIT_KEY_CTRL_T,
	EDIT_KEY_CTRL_U,
	EDIT_KEY_CTRL_V,
	EDIT_KEY_CTRL_W,
	EDIT_KEY_ALT_UP,
	EDIT_KEY_ALT_DOWN,
	EDIT_KEY_ALT_RIGHT,
	EDIT_KEY_ALT_LEFT,
	EDIT_KEY_SHIFT_UP,
	EDIT_KEY_SHIFT_DOWN,
	EDIT_KEY_SHIFT_RIGHT,
	EDIT_KEY_SHIFT_LEFT,
	EDIT_KEY_ALT_SHIFT_UP,
	EDIT_KEY_ALT_SHIFT_DOWN,
	EDIT_KEY_ALT_SHIFT_RIGHT,
	EDIT_KEY_ALT_SHIFT_LEFT,
	EDIT_KEY_EOF
};

static void show_esc_buf(const char *esc_buf, char c, int i)
{
	edit_clear_line();
	printf("\rESC buffer '%s' c='%c' [%d]\n", esc_buf, c, i);
	edit_redraw();
}


static enum edit_key_code esc_seq_to_key1_no(char last)
{
	switch (last) {
	case 'A':
		return EDIT_KEY_UP;
	case 'B':
		return EDIT_KEY_DOWN;
	case 'C':
		return EDIT_KEY_RIGHT;
	case 'D':
		return EDIT_KEY_LEFT;
	default:
		return EDIT_KEY_NONE;
	}
}


static enum edit_key_code esc_seq_to_key1_shift(char last)
{
	switch (last) {
	case 'A':
		return EDIT_KEY_SHIFT_UP;
	case 'B':
		return EDIT_KEY_SHIFT_DOWN;
	case 'C':
		return EDIT_KEY_SHIFT_RIGHT;
	case 'D':
		return EDIT_KEY_SHIFT_LEFT;
	default:
		return EDIT_KEY_NONE;
	}
}


static enum edit_key_code esc_seq_to_key1_alt(char last)
{
	switch (last) {
	case 'A':
		return EDIT_KEY_ALT_UP;
	case 'B':
		return EDIT_KEY_ALT_DOWN;
	case 'C':
		return EDIT_KEY_ALT_RIGHT;
	case 'D':
		return EDIT_KEY_ALT_LEFT;
	default:
		return EDIT_KEY_NONE;
	}
}


static enum edit_key_code esc_seq_to_key1_alt_shift(char last)
{
	switch (last) {
	case 'A':
		return EDIT_KEY_ALT_SHIFT_UP;
	case 'B':
		return EDIT_KEY_ALT_SHIFT_DOWN;
	case 'C':
		return EDIT_KEY_ALT_SHIFT_RIGHT;
	case 'D':
		return EDIT_KEY_ALT_SHIFT_LEFT;
	default:
		return EDIT_KEY_NONE;
	}
}


static enum edit_key_code esc_seq_to_key1_ctrl(char last)
{
	switch (last) {
	case 'A':
		return EDIT_KEY_CTRL_UP;
	case 'B':
		return EDIT_KEY_CTRL_DOWN;
	case 'C':
		return EDIT_KEY_CTRL_RIGHT;
	case 'D':
		return EDIT_KEY_CTRL_LEFT;
	default:
		return EDIT_KEY_NONE;
	}
}


static enum edit_key_code esc_seq_to_key1(int param1, int param2, char last)
{
	/* ESC-[<param1>;<param2><last> */

	if (param1 < 0 && param2 < 0)
		return esc_seq_to_key1_no(last);

	if (param1 == 1 && param2 == 2)
		return esc_seq_to_key1_shift(last);

	if (param1 == 1 && param2 == 3)
		return esc_seq_to_key1_alt(last);

	if (param1 == 1 && param2 == 4)
		return esc_seq_to_key1_alt_shift(last);

	if (param1 == 1 && param2 == 5)
		return esc_seq_to_key1_ctrl(last);

	if (param2 < 0) {
		if (last != '~')
			return EDIT_KEY_NONE;
		switch (param1) {
		case 2:
			return EDIT_KEY_INSERT;
		case 3:
			return EDIT_KEY_DELETE;
		case 5:
			return EDIT_KEY_PAGE_UP;
		case 6:
			return EDIT_KEY_PAGE_DOWN;
		case 15:
			return EDIT_KEY_F5;
		case 17:
			return EDIT_KEY_F6;
		case 18:
			return EDIT_KEY_F7;
		case 19:
			return EDIT_KEY_F8;
		case 20:
			return EDIT_KEY_F9;
		case 21:
			return EDIT_KEY_F10;
		case 23:
			return EDIT_KEY_F11;
		case 24:
			return EDIT_KEY_F12;
		}
	}

	return EDIT_KEY_NONE;
}


static enum edit_key_code esc_seq_to_key2(int param1, int param2, char last)
{
	/* ESC-O<param1>;<param2><last> */

	if (param1 >= 0 || param2 >= 0)
		return EDIT_KEY_NONE;

	switch (last) {
	case 'F':
		return EDIT_KEY_END;
	case 'H':
		return EDIT_KEY_HOME;
	case 'P':
		return EDIT_KEY_F1;
	case 'Q':
		return EDIT_KEY_F2;
	case 'R':
		return EDIT_KEY_F3;
	case 'S':
		return EDIT_KEY_F4;
	default:
		return EDIT_KEY_NONE;
	}
}


static enum edit_key_code esc_seq_to_key(char *seq)
{
	char last, *pos;
	int param1 = -1, param2 = -1;
	enum edit_key_code ret = EDIT_KEY_NONE;

	last = '\0';
	for (pos = seq; *pos; pos++)
		last = *pos;

	if (seq[1] >= '0' && seq[1] <= '9') {
		param1 = atoi(&seq[1]);
		pos = os_strchr(seq, ';');
		if (pos)
			param2 = atoi(pos + 1);
	}

	if (seq[0] == '[')
		ret = esc_seq_to_key1(param1, param2, last);
	else if (seq[0] == 'O')
		ret = esc_seq_to_key2(param1, param2, last);

	if (ret != EDIT_KEY_NONE)
		return ret;

	edit_clear_line();
	printf("\rUnknown escape sequence '%s'\n", seq);
	edit_redraw();
	return EDIT_KEY_NONE;
}


static enum edit_key_code edit_read_key(int sock)
{
	int c;
	unsigned char buf[1];
	int res;
	static int esc = -1;
	static char esc_buf[7];

	res = read(sock, buf, 1);
	if (res < 0)
		perror("read");
	if (res <= 0)
		return EDIT_KEY_EOF;

	c = buf[0];

	if (esc >= 0) {
		if (c == 27 /* ESC */) {
			esc = 0;
			return EDIT_KEY_NONE;
		}

		if (esc == 6) {
			show_esc_buf(esc_buf, c, 0);
			esc = -1;
		} else {
			esc_buf[esc++] = c;
			esc_buf[esc] = '\0';
		}
	}

	if (esc == 1) {
		if (esc_buf[0] != '[' && esc_buf[0] != 'O') {
			show_esc_buf(esc_buf, c, 1);
			esc = -1;
			return EDIT_KEY_NONE;
		} else
			return EDIT_KEY_NONE; /* Escape sequence continues */
	}

	if (esc > 1) {
		if ((c >= '0' && c <= '9') || c == ';')
			return EDIT_KEY_NONE; /* Escape sequence continues */

		if (c == '~' || (c >= 'A' && c <= 'Z')) {
			esc = -1;
			return esc_seq_to_key(esc_buf);
		}

		show_esc_buf(esc_buf, c, 2);
		esc = -1;
		return EDIT_KEY_NONE;
	}

	switch (c) {
	case 1:
		return EDIT_KEY_CTRL_A;
	case 2:
		return EDIT_KEY_CTRL_B;
	case 4:
		return EDIT_KEY_CTRL_D;
	case 5:
		return EDIT_KEY_CTRL_E;
	case 6:
		return EDIT_KEY_CTRL_F;
	case 7:
		return EDIT_KEY_CTRL_G;
	case 8:
		return EDIT_KEY_CTRL_H;
	case 9:
		return EDIT_KEY_TAB;
	case 10:
		return EDIT_KEY_CTRL_J;
	case 13: /* CR */
		return EDIT_KEY_ENTER;
	case 11:
		return EDIT_KEY_CTRL_K;
	case 12:
		return EDIT_KEY_CTRL_L;
	case 14:
		return EDIT_KEY_CTRL_N;
	case 15:
		return EDIT_KEY_CTRL_O;
	case 16:
		return EDIT_KEY_CTRL_P;
	case 18:
		return EDIT_KEY_CTRL_R;
	case 20:
		return EDIT_KEY_CTRL_T;
	case 21:
		return EDIT_KEY_CTRL_U;
	case 22:
		return EDIT_KEY_CTRL_V;
	case 23:
		return EDIT_KEY_CTRL_W;
	case 27: /* ESC */
		esc = 0;
		return EDIT_KEY_NONE;
	case 127:
		return EDIT_KEY_BACKSPACE;
	default:
		return c;
	}
}


static char search_buf[21];
static int search_skip;

static char * search_find(void)
{
	struct edit_history *h;
	size_t len = os_strlen(search_buf);
	int skip = search_skip;

	if (len == 0)
		return NULL;

	dl_list_for_each(h, &history_list, struct edit_history, list) {
		if (os_strstr(h->str, search_buf)) {
			if (skip == 0)
				return h->str;
			skip--;
		}
	}

	search_skip = 0;
	return NULL;
}


static void search_redraw(void)
{
	char *match = search_find();
	printf("\rsearch '%s': %s" CLEAR_END_LINE,
	       search_buf, match ? match : "");
	printf("\rsearch '%s", search_buf);
	fflush(stdout);
}


static void search_start(void)
{
	edit_clear_line();
	search_buf[0] = '\0';
	search_skip = 0;
	search_redraw();
}


static void search_clear(void)
{
	search_redraw();
	printf("\r" CLEAR_END_LINE);
}


static void search_stop(void)
{
	char *match = search_find();
	search_buf[0] = '\0';
	search_clear();
	if (match) {
		os_strlcpy(cmdbuf, match, CMD_BUF_LEN);
		cmdbuf_len = os_strlen(cmdbuf);
		cmdbuf_pos = cmdbuf_len;
	}
	edit_redraw();
}


static void search_cancel(void)
{
	search_buf[0] = '\0';
	search_clear();
	edit_redraw();
}


static void search_backspace(void)
{
	size_t len;
	len = os_strlen(search_buf);
	if (len == 0)
		return;
	search_buf[len - 1] = '\0';
	search_skip = 0;
	search_redraw();
}


static void search_next(void)
{
	search_skip++;
	search_find();
	search_redraw();
}


static void search_char(char c)
{
	size_t len;
	len = os_strlen(search_buf);
	if (len == sizeof(search_buf) - 1)
		return;
	search_buf[len] = c;
	search_buf[len + 1] = '\0';
	search_skip = 0;
	search_redraw();
}


static enum edit_key_code search_key(enum edit_key_code c)
{
	switch (c) {
	case EDIT_KEY_ENTER:
	case EDIT_KEY_CTRL_J:
	case EDIT_KEY_LEFT:
	case EDIT_KEY_RIGHT:
	case EDIT_KEY_HOME:
	case EDIT_KEY_END:
	case EDIT_KEY_CTRL_A:
	case EDIT_KEY_CTRL_E:
		search_stop();
		return c;
	case EDIT_KEY_DOWN:
	case EDIT_KEY_UP:
		search_cancel();
		return EDIT_KEY_EOF;
	case EDIT_KEY_CTRL_H:
	case EDIT_KEY_BACKSPACE:
		search_backspace();
		break;
	case EDIT_KEY_CTRL_R:
		search_next();
		break;
	default:
		if (c >= 32 && c <= 255)
			search_char(c);
		break;
	}

	return EDIT_KEY_NONE;
}


static void edit_read_char(int sock, void *eloop_ctx, void *sock_ctx)
{
	static int last_tab = 0;
	static int search = 0;
	enum edit_key_code c;

	c = edit_read_key(sock);

	if (search) {
		c = search_key(c);
		if (c == EDIT_KEY_NONE)
			return;
		search = 0;
		if (c == EDIT_KEY_EOF)
			return;
	}

	if (c != EDIT_KEY_TAB && c != EDIT_KEY_NONE)
		last_tab = 0;

	switch (c) {
	case EDIT_KEY_NONE:
		break;
	case EDIT_KEY_EOF:
		edit_eof_cb(edit_cb_ctx);
		break;
	case EDIT_KEY_TAB:
		complete(last_tab);
		last_tab = 1;
		break;
	case EDIT_KEY_UP:
	case EDIT_KEY_CTRL_P:
		history_prev();
		break;
	case EDIT_KEY_DOWN:
	case EDIT_KEY_CTRL_N:
		history_next();
		break;
	case EDIT_KEY_RIGHT:
	case EDIT_KEY_CTRL_F:
		move_right();
		break;
	case EDIT_KEY_LEFT:
	case EDIT_KEY_CTRL_B:
		move_left();
		break;
	case EDIT_KEY_CTRL_RIGHT:
		move_word_right();
		break;
	case EDIT_KEY_CTRL_LEFT:
		move_word_left();
		break;
	case EDIT_KEY_DELETE:
		delete_current();
		break;
	case EDIT_KEY_END:
		move_end();
		break;
	case EDIT_KEY_HOME:
	case EDIT_KEY_CTRL_A:
		move_start();
		break;
	case EDIT_KEY_F2:
		history_debug_dump();
		break;
	case EDIT_KEY_CTRL_D:
		if (cmdbuf_len > 0) {
			delete_current();
			return;
		}
		printf("\n");
		edit_eof_cb(edit_cb_ctx);
		break;
	case EDIT_KEY_CTRL_E:
		move_end();
		break;
	case EDIT_KEY_CTRL_H:
	case EDIT_KEY_BACKSPACE:
		delete_left();
		break;
	case EDIT_KEY_ENTER:
	case EDIT_KEY_CTRL_J:
		process_cmd();
		break;
	case EDIT_KEY_CTRL_K:
		clear_right();
		break;
	case EDIT_KEY_CTRL_L:
		edit_clear_line();
		edit_redraw();
		break;
	case EDIT_KEY_CTRL_R:
		search = 1;
		search_start();
		break;
	case EDIT_KEY_CTRL_U:
		clear_left();
		break;
	case EDIT_KEY_CTRL_W:
		delete_word();
		break;
	default:
		if (c >= 32 && c <= 255)
			insert_char(c);
		break;
	}
}


int edit_init(void (*cmd_cb)(void *ctx, char *cmd),
	      void (*eof_cb)(void *ctx),
	      char ** (*completion_cb)(void *ctx, const char *cmd, int pos),
	      void *ctx, const char *history_file)
{
	dl_list_init(&history_list);
	history_curr = NULL;
	if (history_file)
		history_read(history_file);

	edit_cb_ctx = ctx;
	edit_cmd_cb = cmd_cb;
	edit_eof_cb = eof_cb;
	edit_completion_cb = completion_cb;

	tcgetattr(STDIN_FILENO, &prevt);
	newt = prevt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	eloop_register_read_sock(STDIN_FILENO, edit_read_char, NULL, NULL);

	printf("> ");
	fflush(stdout);

	return 0;
}


void edit_deinit(const char *history_file,
		 int (*filter_cb)(void *ctx, const char *cmd))
{
	struct edit_history *h;
	if (history_file)
		history_write(history_file, filter_cb);
	while ((h = dl_list_first(&history_list, struct edit_history, list))) {
		dl_list_del(&h->list);
		os_free(h);
	}
	edit_clear_line();
	putchar('\r');
	fflush(stdout);
	eloop_unregister_read_sock(STDIN_FILENO);
	tcsetattr(STDIN_FILENO, TCSANOW, &prevt);
}


void edit_redraw(void)
{
	char tmp;
	cmdbuf[cmdbuf_len] = '\0';
	printf("\r> %s", cmdbuf);
	if (cmdbuf_pos != cmdbuf_len) {
		tmp = cmdbuf[cmdbuf_pos];
		cmdbuf[cmdbuf_pos] = '\0';
		printf("\r> %s", cmdbuf);
		cmdbuf[cmdbuf_pos] = tmp;
	}
	fflush(stdout);
}
