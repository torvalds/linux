/*
 * Tegra host1x Debug
 *
 * Copyright (c) 2011-2013 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __HOST1X_DEBUG_H
#define __HOST1X_DEBUG_H

#include <linux/debugfs.h>
#include <linux/seq_file.h>

struct host1x;

struct output {
	void (*fn)(void *ctx, const char *str, size_t len);
	void *ctx;
	char buf[256];
};

static inline void write_to_seqfile(void *ctx, const char *str, size_t len)
{
	seq_write((struct seq_file *)ctx, str, len);
}

static inline void write_to_printk(void *ctx, const char *str, size_t len)
{
	pr_info("%s", str);
}

void __printf(2, 3) host1x_debug_output(struct output *o, const char *fmt, ...);

extern unsigned int host1x_debug_trace_cmdbuf;

void host1x_debug_init(struct host1x *host1x);
void host1x_debug_deinit(struct host1x *host1x);
void host1x_debug_dump(struct host1x *host1x);
void host1x_debug_dump_syncpts(struct host1x *host1x);

#endif
