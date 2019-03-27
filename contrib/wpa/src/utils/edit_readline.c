/*
 * Command line editing and history wrapper for readline
 * Copyright (c) 2010, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "eloop.h"
#include "edit.h"


static void *edit_cb_ctx;
static void (*edit_cmd_cb)(void *ctx, char *cmd);
static void (*edit_eof_cb)(void *ctx);
static char ** (*edit_completion_cb)(void *ctx, const char *cmd, int pos) =
	NULL;

static char **pending_completions = NULL;


static void readline_free_completions(void)
{
	int i;
	if (pending_completions == NULL)
		return;
	for (i = 0; pending_completions[i]; i++)
		os_free(pending_completions[i]);
	os_free(pending_completions);
	pending_completions = NULL;
}


static char * readline_completion_func(const char *text, int state)
{
	static int pos = 0;
	static size_t len = 0;

	if (pending_completions == NULL) {
		rl_attempted_completion_over = 1;
		return NULL;
	}

	if (state == 0) {
		pos = 0;
		len = os_strlen(text);
	}
	for (; pending_completions[pos]; pos++) {
		if (strncmp(pending_completions[pos], text, len) == 0)
			return strdup(pending_completions[pos++]);
	}

	rl_attempted_completion_over = 1;
	return NULL;
}


static char ** readline_completion(const char *text, int start, int end)
{
	readline_free_completions();
	if (edit_completion_cb)
		pending_completions = edit_completion_cb(edit_cb_ctx,
							 rl_line_buffer, end);
	return rl_completion_matches(text, readline_completion_func);
}


static void edit_read_char(int sock, void *eloop_ctx, void *sock_ctx)
{
	rl_callback_read_char();
}


static void trunc_nl(char *str)
{
	char *pos = str;
	while (*pos != '\0') {
		if (*pos == '\n') {
			*pos = '\0';
			break;
		}
		pos++;
	}
}


static void readline_cmd_handler(char *cmd)
{
	if (cmd && *cmd) {
		HIST_ENTRY *h;
		while (next_history())
			;
		h = previous_history();
		if (h == NULL || os_strcmp(cmd, h->line) != 0)
			add_history(cmd);
		next_history();
	}
	if (cmd == NULL) {
		edit_eof_cb(edit_cb_ctx);
		return;
	}
	trunc_nl(cmd);
	edit_cmd_cb(edit_cb_ctx, cmd);
}


int edit_init(void (*cmd_cb)(void *ctx, char *cmd),
	      void (*eof_cb)(void *ctx),
	      char ** (*completion_cb)(void *ctx, const char *cmd, int pos),
	      void *ctx, const char *history_file, const char *ps)
{
	edit_cb_ctx = ctx;
	edit_cmd_cb = cmd_cb;
	edit_eof_cb = eof_cb;
	edit_completion_cb = completion_cb;

	rl_attempted_completion_function = readline_completion;
	if (history_file) {
		read_history(history_file);
		stifle_history(100);
	}

	eloop_register_read_sock(STDIN_FILENO, edit_read_char, NULL, NULL);

	if (ps) {
		size_t blen = os_strlen(ps) + 3;
		char *ps2 = os_malloc(blen);
		if (ps2) {
			os_snprintf(ps2, blen, "%s> ", ps);
			rl_callback_handler_install(ps2, readline_cmd_handler);
			os_free(ps2);
			return 0;
		}
	}

	rl_callback_handler_install("> ", readline_cmd_handler);

	return 0;
}


void edit_deinit(const char *history_file,
		 int (*filter_cb)(void *ctx, const char *cmd))
{
	rl_set_prompt("");
	rl_replace_line("", 0);
	rl_redisplay();
	rl_callback_handler_remove();
	readline_free_completions();

	eloop_unregister_read_sock(STDIN_FILENO);

	if (history_file) {
		/* Save command history, excluding lines that may contain
		 * passwords. */
		HIST_ENTRY *h;
		history_set_pos(0);
		while ((h = current_history())) {
			char *p = h->line;
			while (*p == ' ' || *p == '\t')
				p++;
			if (filter_cb && filter_cb(edit_cb_ctx, p)) {
				h = remove_history(where_history());
				if (h) {
					free(h->line);
					free(h->data);
					free(h);
				} else
					next_history();
			} else
				next_history();
		}
		write_history(history_file);
	}
}


void edit_clear_line(void)
{
}


void edit_redraw(void)
{
	rl_on_new_line();
	rl_redisplay();
}
