/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  arch/arm/include/asm/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the ARM architecture specific IDE code.
 */

#ifndef __ASMARM_IDE_H
#define __ASMARM_IDE_H

#ifdef __KERNEL__

#define __ide_mm_insw(port,addr,len)	readsw(port,addr,len)
#define __ide_mm_insl(port,addr,len)	readsl(port,addr,len)
#define __ide_mm_outsw(port,addr,len)	writesw(port,addr,len)
#define __ide_mm_outsl(port,addr,len)	writesl(port,addr,len)

#endif /* __KERNEL__ */

#endif /* __ASMARM_IDE_H */
