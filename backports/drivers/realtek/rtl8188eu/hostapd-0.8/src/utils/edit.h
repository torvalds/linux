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

#ifndef EDIT_H
#define EDIT_H

int edit_init(void (*cmd_cb)(void *ctx, char *cmd),
	      void (*eof_cb)(void *ctx),
	      char ** (*completion_cb)(void *ctx, const char *cmd, int pos),
	      void *ctx, const char *history_file);
void edit_deinit(const char *history_file,
		 int (*filter_cb)(void *ctx, const char *cmd));
void edit_clear_line(void);
void edit_redraw(void);

#endif /* EDIT_H */
