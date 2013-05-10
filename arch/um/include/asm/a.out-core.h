/* a.out coredump register dumper
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef __UM_A_OUT_CORE_H
#define __UM_A_OUT_CORE_H

#ifdef __KERNEL__

#include <linux/user.h>

/*
 * fill in the user structure for an a.out core dump
 */
static inline void aout_dump_thread(struct pt_regs *regs, struct user *u)
{
}

#endif /* __KERNEL__ */
#endif /* __UM_A_OUT_CORE_H */
