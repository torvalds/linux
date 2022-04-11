/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_BOOT_H
#define BOOT_BOOT_H

void startup_kernel(void);
void detect_memory(void);
void store_ipl_parmblock(void);
void setup_boot_command_line(void);
void parse_boot_command_line(void);
void setup_memory_end(void);
void verify_facilities(void);
void print_missing_facilities(void);
void print_pgm_check_info(void);
unsigned long get_random_base(unsigned long safe_addr);

extern int kaslr_enabled;
extern const char kernel_version[];

unsigned long read_ipl_report(unsigned long safe_offset);

#endif /* BOOT_BOOT_H */
