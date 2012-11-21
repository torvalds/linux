#ifndef __ASM_AVR32_PARAM_H
#define __ASM_AVR32_PARAM_H

#include <uapi/asm/param.h>

# define HZ		CONFIG_HZ
# define USER_HZ	100		/* User interfaces are in "ticks" */
# define CLOCKS_PER_SEC	(USER_HZ)	/* frequency at which times() counts */
#endif /* __ASM_AVR32_PARAM_H */
