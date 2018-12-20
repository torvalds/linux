/*
 * Bug handling for PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2012 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_BUG_H__
#define __UNICORE_BUG_H__

#include <asm-generic/bug.h>

struct pt_regs;
struct siginfo;

extern void die(const char *msg, struct pt_regs *regs, int err);
extern void uc32_notify_die(const char *str, struct pt_regs *regs,
		int sig, int code, void __user *addr,
		unsigned long err, unsigned long trap);

#endif /* __UNICORE_BUG_H__ */
