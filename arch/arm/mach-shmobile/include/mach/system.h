#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

static inline void arch_reset(char mode, const char *cmd)
{
	soft_restart(0);
}

#endif
