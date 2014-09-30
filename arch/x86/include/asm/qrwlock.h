#ifndef _ASM_X86_QRWLOCK_H
#define _ASM_X86_QRWLOCK_H

#include <asm-generic/qrwlock_types.h>

#ifndef CONFIG_X86_PPRO_FENCE
#define queue_write_unlock queue_write_unlock
static inline void queue_write_unlock(struct qrwlock *lock)
{
        barrier();
        ACCESS_ONCE(*(u8 *)&lock->cnts) = 0;
}
#endif

#include <asm-generic/qrwlock.h>

#endif /* _ASM_X86_QRWLOCK_H */
