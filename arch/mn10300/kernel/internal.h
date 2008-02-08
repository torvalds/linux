/* Internal definitions for the arch part of the core kernel
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

/*
 * kthread.S
 */
extern int kernel_thread_helper(int);

/*
 * entry.S
 */
extern void ret_from_fork(struct task_struct *) __attribute__((noreturn));
