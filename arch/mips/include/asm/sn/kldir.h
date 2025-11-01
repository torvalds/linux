/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_SN_KLDIR_H
#define _ASM_SN_KLDIR_H

#define KLDIR_MAGIC		0x434d5f53505f5357

#define KLDIR_OFF_MAGIC			0x00
#define KLDIR_OFF_OFFSET		0x08
#define KLDIR_OFF_POINTER		0x10
#define KLDIR_OFF_SIZE			0x18
#define KLDIR_OFF_COUNT			0x20
#define KLDIR_OFF_STRIDE		0x28

#define KLDIR_ENT_SIZE			0x40
#define KLDIR_MAX_ENTRIES		(0x400 / 0x40)

#ifndef __ASSEMBLER__
typedef struct kldir_ent_s {
	u64		magic;		/* Indicates validity of entry	    */
	off_t		offset;		/* Offset from start of node space  */
	unsigned long	pointer;	/* Pointer to area in some cases    */
	size_t		size;		/* Size in bytes		    */
	u64		count;		/* Repeat count if array, 1 if not  */
	size_t		stride;		/* Stride if array, 0 if not	    */
	char		rsvd[16];	/* Pad entry to 0x40 bytes	    */
	/* NOTE: These 16 bytes are used in the Partition KLDIR
	   entry to store partition info. Refer to klpart.h for this. */
} kldir_ent_t;
#endif /* !__ASSEMBLER__ */

#ifdef CONFIG_SGI_IP27
#include <asm/sn/sn0/kldir.h>
#endif

#endif /* _ASM_SN_KLDIR_H */
