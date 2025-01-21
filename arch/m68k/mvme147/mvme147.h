/* SPDX-License-Identifier: GPL-2.0-only */

struct console;

/* config.c */
void mvme147_scc_write(struct console *co, const char *str, unsigned int count);
