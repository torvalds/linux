/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bug handling for PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2012 GUAN Xue-tao
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
