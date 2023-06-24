/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.
 */
#ifndef __ASM_FW_H_
#define __ASM_FW_H_

#include <asm/bootinfo.h>	/* For cleaner code... */

extern int fw_argc;
extern int *_fw_argv;
extern int *_fw_envp;

/*
 * Most firmware like YAMON, PMON, etc. pass arguments and environment
 * variables as 32-bit pointers. These take care of sign extension.
 */
#define fw_argv(index)		((char *)(long)_fw_argv[(index)])
#define fw_envp(index)		((char *)(long)_fw_envp[(index)])

extern void fw_init_cmdline(void);
extern char *fw_getcmdline(void);
extern void fw_meminit(void);
extern char *fw_getenv(char *name);
extern unsigned long fw_getenvl(char *name);
extern void fw_init_early_console(void);

#endif /* __ASM_FW_H_ */
