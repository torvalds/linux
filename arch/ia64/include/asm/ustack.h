#ifndef _ASM_IA64_USTACK_H
#define _ASM_IA64_USTACK_H

#include <asm/page.h>
#include <uapi/asm/ustack.h>

/* The absolute hard limit for stack size is 1/2 of the mappable space in the region */
#define MAX_USER_STACK_SIZE	(RGN_MAP_LIMIT/2)
#define STACK_TOP		(0x6000000000000000UL + RGN_MAP_LIMIT)
#define STACK_TOP_MAX		STACK_TOP
#endif /* _ASM_IA64_USTACK_H */
