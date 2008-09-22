/*
 * include/asm-sh/sh03/io.h
 *
 * Copyright 2004 Interface Co.,Ltd. Saito.K
 *
 * IO functions for an Interface CTP/PCI-SH03
 */

#ifndef _ASM_SH_IO_SH03_H
#define _ASM_SH_IO_SH03_H

#include <linux/time.h>

#define IRL0_IRQ	2
#define IRL0_PRIORITY	13
#define IRL1_IRQ	5
#define IRL1_PRIORITY	10
#define IRL2_IRQ	8
#define IRL2_PRIORITY	7
#define IRL3_IRQ	11
#define IRL3_PRIORITY	4

void heartbeat_sh03(void);

#endif /* _ASM_SH_IO_SH03_H */
