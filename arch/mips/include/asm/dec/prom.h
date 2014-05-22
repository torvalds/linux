/*
 *	include/asm-mips/dec/prom.h
 *
 *	DECstation PROM interface.
 *
 *	Copyright (C) 2002  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Based on arch/mips/dec/prom/prom.h by the Anonymous.
 */
#ifndef _ASM_DEC_PROM_H
#define _ASM_DEC_PROM_H

#include <linux/types.h>

#include <asm/addrspace.h>

/*
 * PMAX/3MAX PROM entry points for DS2100/3100's and DS5000/2xx's.
 * Many of these will work for MIPSen as well!
 */
#define VEC_RESET		(u64 *)CKSEG1ADDR(0x1fc00000)
							/* Prom base address */

#define PMAX_PROM_ENTRY(x)	(VEC_RESET + (x))	/* Prom jump table */

#define PMAX_PROM_HALT		PMAX_PROM_ENTRY(2)	/* valid on MIPSen */
#define PMAX_PROM_AUTOBOOT	PMAX_PROM_ENTRY(5)	/* valid on MIPSen */
#define PMAX_PROM_OPEN		PMAX_PROM_ENTRY(6)
#define PMAX_PROM_READ		PMAX_PROM_ENTRY(7)
#define PMAX_PROM_CLOSE		PMAX_PROM_ENTRY(10)
#define PMAX_PROM_LSEEK		PMAX_PROM_ENTRY(11)
#define PMAX_PROM_GETCHAR	PMAX_PROM_ENTRY(12)
#define PMAX_PROM_PUTCHAR	PMAX_PROM_ENTRY(13)	/* 12 on MIPSen */
#define PMAX_PROM_GETS		PMAX_PROM_ENTRY(15)
#define PMAX_PROM_PRINTF	PMAX_PROM_ENTRY(17)
#define PMAX_PROM_GETENV	PMAX_PROM_ENTRY(33)	/* valid on MIPSen */


/*
 * Magic number indicating REX PROM available on DECstation.  Found in
 * register a2 on transfer of control to program from PROM.
 */
#define REX_PROM_MAGIC		0x30464354

#ifdef CONFIG_64BIT

#define prom_is_rex(magic)	1	/* KN04 and KN05 are REX PROMs.  */

#else /* !CONFIG_64BIT */

#define prom_is_rex(magic)	((magic) == REX_PROM_MAGIC)

#endif /* !CONFIG_64BIT */


/*
 * 3MIN/MAXINE PROM entry points for DS5000/1xx's, DS5000/xx's and
 * DS5000/2x0.
 */
#define REX_PROM_GETBITMAP	0x84/4	/* get mem bitmap */
#define REX_PROM_GETCHAR	0x24/4	/* getch() */
#define REX_PROM_GETENV		0x64/4	/* get env. variable */
#define REX_PROM_GETSYSID	0x80/4	/* get system id */
#define REX_PROM_GETTCINFO	0xa4/4
#define REX_PROM_PRINTF		0x30/4	/* printf() */
#define REX_PROM_SLOTADDR	0x6c/4	/* slotaddr */
#define REX_PROM_BOOTINIT	0x54/4	/* open() */
#define REX_PROM_BOOTREAD	0x58/4	/* read() */
#define REX_PROM_CLEARCACHE	0x7c/4


/*
 * Used by rex_getbitmap().
 */
typedef struct {
	int pagesize;
	unsigned char bitmap[0];
} memmap;


/*
 * Function pointers as read from a PROM's callback vector.
 */
extern int (*__rex_bootinit)(void);
extern int (*__rex_bootread)(void);
extern int (*__rex_getbitmap)(memmap *);
extern unsigned long *(*__rex_slot_address)(int);
extern void *(*__rex_gettcinfo)(void);
extern int (*__rex_getsysid)(void);
extern void (*__rex_clear_cache)(void);

extern int (*__prom_getchar)(void);
extern char *(*__prom_getenv)(char *);
extern int (*__prom_printf)(char *, ...);

extern int (*__pmax_open)(char*, int);
extern int (*__pmax_lseek)(int, long, int);
extern int (*__pmax_read)(int, void *, int);
extern int (*__pmax_close)(int);


#ifdef CONFIG_64BIT

/*
 * On MIPS64 we have to call PROM functions via a helper
 * dispatcher to accommodate ABI incompatibilities.
 */
#define __DEC_PROM_O32(fun, arg) fun arg __asm__(#fun); \
				 __asm__(#fun " = call_o32")

int __DEC_PROM_O32(_rex_bootinit, (int (*)(void), void *));
int __DEC_PROM_O32(_rex_bootread, (int (*)(void), void *));
int __DEC_PROM_O32(_rex_getbitmap, (int (*)(memmap *), void *, memmap *));
unsigned long *__DEC_PROM_O32(_rex_slot_address,
			     (unsigned long *(*)(int), void *, int));
void *__DEC_PROM_O32(_rex_gettcinfo, (void *(*)(void), void *));
int __DEC_PROM_O32(_rex_getsysid, (int (*)(void), void *));
void __DEC_PROM_O32(_rex_clear_cache, (void (*)(void), void *));

int __DEC_PROM_O32(_prom_getchar, (int (*)(void), void *));
char *__DEC_PROM_O32(_prom_getenv, (char *(*)(char *), void *, char *));
int __DEC_PROM_O32(_prom_printf, (int (*)(char *, ...), void *, char *, ...));


#define rex_bootinit()		_rex_bootinit(__rex_bootinit, NULL)
#define rex_bootread()		_rex_bootread(__rex_bootread, NULL)
#define rex_getbitmap(x)	_rex_getbitmap(__rex_getbitmap, NULL, x)
#define rex_slot_address(x)	_rex_slot_address(__rex_slot_address, NULL, x)
#define rex_gettcinfo()		_rex_gettcinfo(__rex_gettcinfo, NULL)
#define rex_getsysid()		_rex_getsysid(__rex_getsysid, NULL)
#define rex_clear_cache()	_rex_clear_cache(__rex_clear_cache, NULL)

#define prom_getchar()		_prom_getchar(__prom_getchar, NULL)
#define prom_getenv(x)		_prom_getenv(__prom_getenv, NULL, x)
#define prom_printf(x...)	_prom_printf(__prom_printf, NULL, x)

#else /* !CONFIG_64BIT */

/*
 * On plain MIPS we just call PROM functions directly.
 */
#define rex_bootinit		__rex_bootinit
#define rex_bootread		__rex_bootread
#define rex_getbitmap		__rex_getbitmap
#define rex_slot_address	__rex_slot_address
#define rex_gettcinfo		__rex_gettcinfo
#define rex_getsysid		__rex_getsysid
#define rex_clear_cache		__rex_clear_cache

#define prom_getchar		__prom_getchar
#define prom_getenv		__prom_getenv
#define prom_printf		__prom_printf

#define pmax_open		__pmax_open
#define pmax_lseek		__pmax_lseek
#define pmax_read		__pmax_read
#define pmax_close		__pmax_close

#endif /* !CONFIG_64BIT */


extern void prom_meminit(u32);
extern void prom_identify_arch(u32);
extern void prom_init_cmdline(s32, s32 *, u32);

extern void register_prom_console(void);
extern void unregister_prom_console(void);

#endif /* _ASM_DEC_PROM_H */
