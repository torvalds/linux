/* SPDX-License-Identifier: GPL-2.0-only */

struct console;

/* config.c */
void mvme16x_cons_write(struct console *co, const char *str, unsigned count);
