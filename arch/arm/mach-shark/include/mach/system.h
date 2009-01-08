/*
 * arch/arm/mach-shark/include/mach/system.h
 *
 * by Alexander Schulz
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

/* Found in arch/mach-shark/core.c */
extern void arch_reset(char mode);

static inline void arch_idle(void)
{
}

#endif
