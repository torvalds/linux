/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>

struct rtc_time;

/* config.c */
asmlinkage void sun3_init(void);

/* idprom.c */
void sun3_get_model(char *model);

/* intersil.c */
int sun3_hwclk(int set, struct rtc_time *t);

/* leds.c */
void sun3_leds(unsigned char byte);

/* mmu_emu.c */
void mmu_emu_init(unsigned long bootmem_end);
int mmu_emu_handle_fault(unsigned long vaddr, int read_flag, int kernel_fault);
void print_pte_vaddr(unsigned long vaddr);
