/*
** asm/bootinfo.h -- Definition of the Linux/m68k boot information structure
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
*/

#ifndef _M68K_BOOTINFO_H
#define _M68K_BOOTINFO_H

#include <uapi/asm/bootinfo.h>


#ifndef __ASSEMBLY__

#ifdef CONFIG_BOOTINFO_PROC
extern void save_bootinfo(const struct bi_record *bi);
#else
static inline void save_bootinfo(const struct bi_record *bi) {}
#endif

#ifdef CONFIG_UBOOT
void process_uboot_commandline(char *commandp, int size);
#else
static inline void process_uboot_commandline(char *commandp, int size) {}
#endif

#endif /* __ASSEMBLY__ */


#endif /* _M68K_BOOTINFO_H */
