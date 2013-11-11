/*
** asm/setup.h -- Definition of the Linux/m68k setup information
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created 09/29/92 by Greg Harp
**
** 5/2/94 Roman Hodek:
**   Added bi_atari part of the machine dependent union bi_un; for now it
**   contains just a model field to distinguish between TT and Falcon.
** 26/7/96 Roman Zippel:
**   Renamed to setup.h; added some useful macros to allow gcc some
**   optimizations if possible.
** 5/10/96 Geert Uytterhoeven:
**   Redesign of the boot information structure; moved boot information
**   structure to bootinfo.h
*/

#ifndef _UAPI_M68K_SETUP_H
#define _UAPI_M68K_SETUP_H



    /*
     *  Linux/m68k Architectures
     */

#define MACH_AMIGA    1
#define MACH_ATARI    2
#define MACH_MAC      3
#define MACH_APOLLO   4
#define MACH_SUN3     5
#define MACH_MVME147  6
#define MACH_MVME16x  7
#define MACH_BVME6000 8
#define MACH_HP300    9
#define MACH_Q40     10
#define MACH_SUN3X   11
#define MACH_M54XX   12

#define COMMAND_LINE_SIZE 256



    /*
     *  CPU, FPU and MMU types
     *
     *  Note: we may rely on the following equalities:
     *
     *      CPU_68020 == MMU_68851
     *      CPU_68030 == MMU_68030
     *      CPU_68040 == FPU_68040 == MMU_68040
     *      CPU_68060 == FPU_68060 == MMU_68060
     */

#define CPUB_68020     0
#define CPUB_68030     1
#define CPUB_68040     2
#define CPUB_68060     3
#define CPUB_COLDFIRE  4

#define CPU_68020      (1<<CPUB_68020)
#define CPU_68030      (1<<CPUB_68030)
#define CPU_68040      (1<<CPUB_68040)
#define CPU_68060      (1<<CPUB_68060)
#define CPU_COLDFIRE   (1<<CPUB_COLDFIRE)

#define FPUB_68881     0
#define FPUB_68882     1
#define FPUB_68040     2                       /* Internal FPU */
#define FPUB_68060     3                       /* Internal FPU */
#define FPUB_SUNFPA    4                       /* Sun-3 FPA */
#define FPUB_COLDFIRE  5                       /* ColdFire FPU */

#define FPU_68881      (1<<FPUB_68881)
#define FPU_68882      (1<<FPUB_68882)
#define FPU_68040      (1<<FPUB_68040)
#define FPU_68060      (1<<FPUB_68060)
#define FPU_SUNFPA     (1<<FPUB_SUNFPA)
#define FPU_COLDFIRE   (1<<FPUB_COLDFIRE)

#define MMUB_68851     0
#define MMUB_68030     1                       /* Internal MMU */
#define MMUB_68040     2                       /* Internal MMU */
#define MMUB_68060     3                       /* Internal MMU */
#define MMUB_APOLLO    4                       /* Custom Apollo */
#define MMUB_SUN3      5                       /* Custom Sun-3 */
#define MMUB_COLDFIRE  6                       /* Internal MMU */

#define MMU_68851      (1<<MMUB_68851)
#define MMU_68030      (1<<MMUB_68030)
#define MMU_68040      (1<<MMUB_68040)
#define MMU_68060      (1<<MMUB_68060)
#define MMU_SUN3       (1<<MMUB_SUN3)
#define MMU_APOLLO     (1<<MMUB_APOLLO)
#define MMU_COLDFIRE   (1<<MMUB_COLDFIRE)


#endif /* _UAPI_M68K_SETUP_H */
