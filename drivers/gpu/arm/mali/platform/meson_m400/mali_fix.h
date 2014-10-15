#ifndef MALI_FIX_H
#define MALI_FIX_H

#define MMU_INT_NONE 0
#define MMU_INT_HIT  1
#define MMU_INT_TOP  2
#define MMU_INT_BOT  3

extern void malifix_init(void);
extern void malifix_exit(void);
extern void malifix_set_mmu_int_process_state(int, int);
extern int  malifix_get_mmu_int_process_state(int);
extern int mali_meson_is_revb(void);

#endif /* MALI_FIX_H */
