/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Header file for the real-mode kernel code
 */

#ifndef BOOT_BOOT_H
#define BOOT_BOOT_H

#define STACK_SIZE	512	/* Minimum number of bytes for stack */

#ifndef __ASSEMBLY__

#include <stdarg.h>
#include <linux/types.h>
#include <linux/edd.h>
#include <asm/boot.h>
#include <asm/setup.h>
#include "bitops.h"
#include <asm/cpufeature.h>
#include <asm/processor-flags.h>
#include "ctype.h"

/* Useful macros */
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

extern struct setup_header hdr;
extern struct boot_params boot_params;

#define cpu_relax()	asm volatile("rep; nop")

/* Basic port I/O */
static inline void outb(u8 v, u16 port)
{
	asm volatile("outb %0,%1" : : "a" (v), "dN" (port));
}
static inline u8 inb(u16 port)
{
	u8 v;
	asm volatile("inb %1,%0" : "=a" (v) : "dN" (port));
	return v;
}

static inline void outw(u16 v, u16 port)
{
	asm volatile("outw %0,%1" : : "a" (v), "dN" (port));
}
static inline u16 inw(u16 port)
{
	u16 v;
	asm volatile("inw %1,%0" : "=a" (v) : "dN" (port));
	return v;
}

static inline void outl(u32 v, u16 port)
{
	asm volatile("outl %0,%1" : : "a" (v), "dN" (port));
}
static inline u32 inl(u16 port)
{
	u32 v;
	asm volatile("inl %1,%0" : "=a" (v) : "dN" (port));
	return v;
}

static inline void io_delay(void)
{
	const u16 DELAY_PORT = 0x80;
	asm volatile("outb %%al,%0" : : "dN" (DELAY_PORT));
}

/* These functions are used to reference data in other segments. */

static inline u16 ds(void)
{
	u16 seg;
	asm("movw %%ds,%0" : "=rm" (seg));
	return seg;
}

static inline void set_fs(u16 seg)
{
	asm volatile("movw %0,%%fs" : : "rm" (seg));
}
static inline u16 fs(void)
{
	u16 seg;
	asm volatile("movw %%fs,%0" : "=rm" (seg));
	return seg;
}

static inline void set_gs(u16 seg)
{
	asm volatile("movw %0,%%gs" : : "rm" (seg));
}
static inline u16 gs(void)
{
	u16 seg;
	asm volatile("movw %%gs,%0" : "=rm" (seg));
	return seg;
}

typedef unsigned int addr_t;

static inline u8 rdfs8(addr_t addr)
{
	u8 v;
	asm volatile("movb %%fs:%1,%0" : "=q" (v) : "m" (*(u8 *)addr));
	return v;
}
static inline u16 rdfs16(addr_t addr)
{
	u16 v;
	asm volatile("movw %%fs:%1,%0" : "=r" (v) : "m" (*(u16 *)addr));
	return v;
}
static inline u32 rdfs32(addr_t addr)
{
	u32 v;
	asm volatile("movl %%fs:%1,%0" : "=r" (v) : "m" (*(u32 *)addr));
	return v;
}

static inline void wrfs8(u8 v, addr_t addr)
{
	asm volatile("movb %1,%%fs:%0" : "+m" (*(u8 *)addr) : "qi" (v));
}
static inline void wrfs16(u16 v, addr_t addr)
{
	asm volatile("movw %1,%%fs:%0" : "+m" (*(u16 *)addr) : "ri" (v));
}
static inline void wrfs32(u32 v, addr_t addr)
{
	asm volatile("movl %1,%%fs:%0" : "+m" (*(u32 *)addr) : "ri" (v));
}

static inline u8 rdgs8(addr_t addr)
{
	u8 v;
	asm volatile("movb %%gs:%1,%0" : "=q" (v) : "m" (*(u8 *)addr));
	return v;
}
static inline u16 rdgs16(addr_t addr)
{
	u16 v;
	asm volatile("movw %%gs:%1,%0" : "=r" (v) : "m" (*(u16 *)addr));
	return v;
}
static inline u32 rdgs32(addr_t addr)
{
	u32 v;
	asm volatile("movl %%gs:%1,%0" : "=r" (v) : "m" (*(u32 *)addr));
	return v;
}

static inline void wrgs8(u8 v, addr_t addr)
{
	asm volatile("movb %1,%%gs:%0" : "+m" (*(u8 *)addr) : "qi" (v));
}
static inline void wrgs16(u16 v, addr_t addr)
{
	asm volatile("movw %1,%%gs:%0" : "+m" (*(u16 *)addr) : "ri" (v));
}
static inline void wrgs32(u32 v, addr_t addr)
{
	asm volatile("movl %1,%%gs:%0" : "+m" (*(u32 *)addr) : "ri" (v));
}

/* Note: these only return true/false, not a signed return value! */
static inline int memcmp(const void *s1, const void *s2, size_t len)
{
	u8 diff;
	asm("repe; cmpsb; setnz %0"
	    : "=qm" (diff), "+D" (s1), "+S" (s2), "+c" (len));
	return diff;
}

static inline int memcmp_fs(const void *s1, addr_t s2, size_t len)
{
	u8 diff;
	asm volatile("fs; repe; cmpsb; setnz %0"
		     : "=qm" (diff), "+D" (s1), "+S" (s2), "+c" (len));
	return diff;
}
static inline int memcmp_gs(const void *s1, addr_t s2, size_t len)
{
	u8 diff;
	asm volatile("gs; repe; cmpsb; setnz %0"
		     : "=qm" (diff), "+D" (s1), "+S" (s2), "+c" (len));
	return diff;
}

/* Heap -- available for dynamic lists. */
extern char _end[];
extern char *HEAP;
extern char *heap_end;
#define RESET_HEAP() ((void *)( HEAP = _end ))
static inline char *__get_heap(size_t s, size_t a, size_t n)
{
	char *tmp;

	HEAP = (char *)(((size_t)HEAP+(a-1)) & ~(a-1));
	tmp = HEAP;
	HEAP += s*n;
	return tmp;
}
#define GET_HEAP(type, n) \
	((type *)__get_heap(sizeof(type),__alignof__(type),(n)))

static inline bool heap_free(size_t n)
{
	return (int)(heap_end-HEAP) >= (int)n;
}

/* copy.S */

void copy_to_fs(addr_t dst, void *src, size_t len);
void *copy_from_fs(void *dst, addr_t src, size_t len);
void copy_to_gs(addr_t dst, void *src, size_t len);
void *copy_from_gs(void *dst, addr_t src, size_t len);
void *memcpy(void *dst, void *src, size_t len);
void *memset(void *dst, int c, size_t len);

#define memcpy(d,s,l) __builtin_memcpy(d,s,l)
#define memset(d,c,l) __builtin_memset(d,c,l)

/* a20.c */
int enable_a20(void);

/* apm.c */
int query_apm_bios(void);

/* bioscall.c */
struct biosregs {
	union {
		struct {
			u32 edi;
			u32 esi;
			u32 ebp;
			u32 _esp;
			u32 ebx;
			u32 edx;
			u32 ecx;
			u32 eax;
			u32 _fsgs;
			u32 _dses;
			u32 eflags;
		};
		struct {
			u16 di, hdi;
			u16 si, hsi;
			u16 bp, hbp;
			u16 _sp, _hsp;
			u16 bx, hbx;
			u16 dx, hdx;
			u16 cx, hcx;
			u16 ax, hax;
			u16 gs, fs;
			u16 es, ds;
			u16 flags, hflags;
		};
		struct {
			u8 dil, dih, edi2, edi3;
			u8 sil, sih, esi2, esi3;
			u8 bpl, bph, ebp2, ebp3;
			u8 _spl, _sph, _esp2, _esp3;
			u8 bl, bh, ebx2, ebx3;
			u8 dl, dh, edx2, edx3;
			u8 cl, ch, ecx2, ecx3;
			u8 al, ah, eax2, eax3;
		};
	};
};
void intcall(u8 int_no, const struct biosregs *ireg, struct biosregs *oreg);

/* cmdline.c */
int __cmdline_find_option(unsigned long cmdline_ptr, const char *option, char *buffer, int bufsize);
int __cmdline_find_option_bool(unsigned long cmdline_ptr, const char *option);
static inline int cmdline_find_option(const char *option, char *buffer, int bufsize)
{
	unsigned long cmd_line_ptr = boot_params.hdr.cmd_line_ptr;

	if (cmd_line_ptr >= 0x100000)
		return -1;      /* inaccessible */

	return __cmdline_find_option(cmd_line_ptr, option, buffer, bufsize);
}

static inline int cmdline_find_option_bool(const char *option)
{
	unsigned long cmd_line_ptr = boot_params.hdr.cmd_line_ptr;

	if (cmd_line_ptr >= 0x100000)
		return -1;      /* inaccessible */

	return __cmdline_find_option_bool(cmd_line_ptr, option);
}


/* cpu.c, cpucheck.c */
struct cpu_features {
	int level;		/* Family, or 64 for x86-64 */
	int model;
	u32 flags[NCAPINTS];
};
extern struct cpu_features cpu;
int check_cpu(int *cpu_level_ptr, int *req_level_ptr, u32 **err_flags_ptr);
int validate_cpu(void);

/* early_serial_console.c */
extern int early_serial_base;
void console_init(void);

/* edd.c */
void query_edd(void);

/* header.S */
void __attribute__((noreturn)) die(void);

/* mca.c */
int query_mca(void);

/* memory.c */
int detect_memory(void);

/* pm.c */
void __attribute__((noreturn)) go_to_protected_mode(void);

/* pmjump.S */
void __attribute__((noreturn))
	protected_mode_jump(u32 entrypoint, u32 bootparams);

/* printf.c */
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list args);
int printf(const char *fmt, ...);

/* regs.c */
void initregs(struct biosregs *regs);

/* string.c */
int strcmp(const char *str1, const char *str2);
int strncmp(const char *cs, const char *ct, size_t count);
size_t strnlen(const char *s, size_t maxlen);
unsigned int atou(const char *s);
unsigned long long simple_strtoull(const char *cp, char **endp, unsigned int base);

/* tty.c */
void puts(const char *);
void putchar(int);
int getchar(void);
void kbd_flush(void);
int getchar_timeout(void);

/* video.c */
void set_video(void);

/* video-mode.c */
int set_mode(u16 mode);
int mode_defined(u16 mode);
void probe_cards(int unsafe);

/* video-vesa.c */
void vesa_store_edid(void);

#endif /* __ASSEMBLY__ */

#endif /* BOOT_BOOT_H */
