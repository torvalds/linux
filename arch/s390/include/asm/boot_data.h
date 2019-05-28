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

#endif /* _ASM_S390_BOOT_DATA_H */
