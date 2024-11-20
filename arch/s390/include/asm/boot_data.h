/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_BOOT_DATA_H

#include <asm/setup.h>
#include <asm/ipl.h>

extern char early_command_line[COMMAND_LINE_SIZE];
extern struct ipl_parameter_block ipl_block;
extern int ipl_block_valid;
extern int ipl_secure_flag;

extern unsigned long ipl_cert_list_addr;
extern unsigned long ipl_cert_list_size;

extern unsigned long early_ipl_comp_list_addr;
extern unsigned long early_ipl_comp_list_size;

extern char boot_rb[PAGE_SIZE * 2];
extern size_t boot_rb_off;

#define boot_rb_foreach(cb)							\
	do {									\
		size_t off = boot_rb_off + strlen(boot_rb + boot_rb_off) + 1;	\
		size_t len;							\
		for (; off < sizeof(boot_rb) && (len = strlen(boot_rb + off)); off += len + 1) \
			cb(boot_rb + off);					\
		for (off = 0; off < boot_rb_off && (len = strlen(boot_rb + off)); off += len + 1) \
			cb(boot_rb + off);					\
	} while (0)

#endif /* _ASM_S390_BOOT_DATA_H */
