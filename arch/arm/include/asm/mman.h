#ifndef __ARM_MMAN_H__
#define __ARM_MMAN_H__

#include <asm-generic/mman.h>

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define MAP_LOCKED	0x2000		/* pages are locked */
#define MAP_NORESERVE	0x4000		/* don't check for reservations */
#define MAP_POPULATE	0x8000		/* populate (prefault) page tables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */

#endif /* __ARM_MMAN_H__ */
