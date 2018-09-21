/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_X86_32
# include <asm/suspend_32.h>
#else
# include <asm/suspend_64.h>
#endif
extern unsigned long restore_jump_address __visible;
extern unsigned long jump_address_phys;
extern unsigned long restore_cr3 __visible;
extern unsigned long temp_level4_pgt __visible;
extern unsigned long relocated_restore_code __visible;
extern int relocate_restore_code(void);
/* Defined in hibernate_asm_32/64.S */
extern asmlinkage __visible int restore_image(void);
