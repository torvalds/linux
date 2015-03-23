/*
 * Minimal command line editing
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

#include "common.h"
#include "eloop.h"
#include "edit.h"


#define CMD_BUF_LEN 256
static char cmdbuf[CMD_BUF_LEN];
static int cmdbuf_pos = 0;

static void *edit_cb_ctx;
static void (*edit_cmd_cb)(void *ctx, char *cmd);
static void (*edit_eof_cb)(void *ctx);


static void edit_read_char(int sock, void *eloop_ctx, void *sock_ctx)
{
	int c;
	unsigned char buf[1];
	int res;

	res = read(sock, buf, 1);
	if (res < 0)
		perror("read");
	if (res <= 0) {
		edit_eof_cb(edit_cb_ctx);
		return;
	}
	c = buf[0];

	if (c == '\r' || c == '\n') {
		cmdbuf[cmdbuf_pos] = '\0';
		cmdbuf_pos = 0;
		edit_cmd_cb(edit_cb_ctx, cmdbuf);
		printf("> ");
		fflush(stdout);
		return;
	}

	if (c >= 32 && c <= 255) {
		if (cmdbuf_pos < (int) sizeof(cmdbuf) - 1) {
			cmdbuf[cmdbuf_pos++] = c;
		}
	}
}


int edit_init(void (*cmd_cb)(void *ctx, char *cmd),
	      void (*eof_cb)(void *ctx),
	      char ** (*completion_cb)(void *ctx, const char *cmd, int pos),
	      void *ctx, const char *history_file)
{
	edit_cb_ctx = ctx;
	edit_cmd_cb = cmd_cb;
	edit_eof_cb = eof_cb;
	eloop_register_read_sock(STDIN_FILENO, edit_read_char, NULL, NULL);

	printf("> ");
	fflush(stdout);

	return 0;
}


void edit_deinit(const char *history_file,
		 int (*filter_cb)(void *ctx, const char *cmd))
{
	eloop_unregister_read_sock(STDIN_FILENO);
}


void edit_clear_line(void)
{
}


void edit_redraw(void)
{
	cmdbuf[cmdbuf_pos] = '\0';
	printf("\r> %s", cmdbuf);
}
