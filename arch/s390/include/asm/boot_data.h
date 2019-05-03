/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_BOOT_DATA_H

#include <asm/setup.h>
#include <asm/ipl.h>

extern char early_command_line[COMMAND_LINE_SIZE];
extern struct ipl_parameter_block early_ipl_block;
extern int early_ipl_block_valid;

#endif /* _ASM_S390_BOOT_DATA_H */
