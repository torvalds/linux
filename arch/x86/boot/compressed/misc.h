#ifndef BOOT_COMPRESSED_MISC_H
#define BOOT_COMPRESSED_MISC_H

/*
 * we have to be careful, because no indirections are allowed here, and
 * paravirt_ops is a kind of one. As it will only run in baremetal anyway,
 * we just keep it from happening
 */
#undef CONFIG_PARAVIRT
#ifdef CONFIG_X86_32
#define _ASM_X86_DESC_H 1
#endif

#include <linux/linkage.h>
#include <linux/screen_info.h>
#include <linux/elf.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/bootparam.h>

#define BOOT_BOOT_H
#include "../ctype.h"

/* misc.c */
extern struct boot_params *real_mode;		/* Pointer to real-mode data */
void __putstr(int error, const char *s);
#define putstr(__x)  __putstr(0, __x)
#define puts(__x)  __putstr(0, __x)

/* cmdline.c */
int cmdline_find_option(const char *option, char *buffer, int bufsize);
int cmdline_find_option_bool(const char *option);

/* early_serial_console.c */
extern int early_serial_base;
void console_init(void);

#endif
