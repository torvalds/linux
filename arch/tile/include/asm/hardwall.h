/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Provide methods for access control of per-cpu resources like
 * UDN, IDN, or IPI.
 */
#ifndef _ASM_TILE_HARDWALL_H
#define _ASM_TILE_HARDWALL_H

#include <uapi/asm/hardwall.h>

/* /proc hooks for hardwall. */
struct proc_dir_entry;
#ifdef CONFIG_HARDWALL
void proc_tile_hardwall_init(struct proc_dir_entry *root);
int proc_pid_hardwall(struct task_struct *task, char *buffer);
#else
static inline void proc_tile_hardwall_init(struct proc_dir_entry *root) {}
#endif
#endif /* _ASM_TILE_HARDWALL_H */
