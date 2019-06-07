/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  mpc30x.h, Include file for Victor MP-C303/304.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#ifndef __VICTOR_MPC30X_H
#define __VICTOR_MPC30X_H

#include <asm/vr41xx/irq.h>

/*
 * General-Purpose I/O Pin Number
 */
#define VRC4173_PIN			1
#define MQ200_PIN			4

/*
 * Interrupt Number
 */
#define VRC4173_CASCADE_IRQ		GIU_IRQ(VRC4173_PIN)
#define MQ200_IRQ			GIU_IRQ(MQ200_PIN)

#endif /* __VICTOR_MPC30X_H */
