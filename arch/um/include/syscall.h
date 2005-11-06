/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYSCALL_USER_H
#define __SYSCALL_USER_H

extern int record_syscall_start(int syscall);
extern void record_syscall_end(int index, long result);

#endif
