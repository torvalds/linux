/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

extern const struct file_operations proc_log_operations;
extern uint8_t log_level;

int debug_log(int type, char *fmt, ...);
int log_open(struct inode *inode, struct file *file);
int log_release(struct inode *inode, struct file *file);
ssize_t log_read(struct file *file, char __user *buf, size_t count,
		 loff_t *ppos);
ssize_t log_write(struct file *file, const char __user *buf, size_t count,
		  loff_t *ppos);
unsigned int log_poll(struct file *file, poll_table *wait);
