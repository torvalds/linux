/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_BOOT_H
#define BOOT_BOOT_H

void startup_kernel(void);
void detect_memory(void);
void store_ipl_parmblock(void);
void setup_boot_command_line(void);
void parse_boot_command_line(void);
void setup_memory_end(void);
void print_missing_facilities(void);

#endif /* BOOT_BOOT_H */
