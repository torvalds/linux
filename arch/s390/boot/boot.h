/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_BOOT_H
#define BOOT_BOOT_H

#include <linux/types.h>

#define BOOT_STACK_OFFSET 0x8000

#ifndef __ASSEMBLY__

#include <linux/compiler.h>

void startup_kernel(void);
unsigned long detect_memory(void);
bool is_ipl_block_dump(void);
void store_ipl_parmblock(void);
void setup_boot_command_line(void);
void parse_boot_command_line(void);
void verify_facilities(void);
void print_missing_facilities(void);
void print_pgm_check_info(void);
unsigned long get_random_base(unsigned long safe_addr);
void __printf(1, 2) decompressor_printk(const char *fmt, ...);

extern const char kernel_version[];
extern unsigned long memory_limit;
extern int vmalloc_size_set;
extern int kaslr_enabled;

unsigned long read_ipl_report(unsigned long safe_offset);

#endif /* __ASSEMBLY__ */
#endif /* BOOT_BOOT_H */
