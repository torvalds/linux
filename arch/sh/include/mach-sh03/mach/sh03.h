/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SH03_H
#define __ASM_SH_SH03_H

/*
 * linux/include/asm-sh/sh03/sh03.h
 *
 * Copyright (C) 2004  Interface Co., Ltd. Saito.K
 *
 * Interface CTP/PCI-SH03 support
 */

#define PA_PCI_IO       (0xbe240000)    /* PCI I/O space */
#define PA_PCI_MEM      (0xbd000000)    /* PCI MEM space */

#define PCIPAR          (0xa4000cf8)    /* PCI Config address */
#define PCIPDR          (0xa4000cfc)    /* PCI Config data    */

#endif  /* __ASM_SH_SH03_H */
