/*
 * uClinux flat-format executables
 *
 * Copyright 2003-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2
 */

#ifndef __BLACKFIN_FLAT_H__
#define __BLACKFIN_FLAT_H__

#include <asm/unaligned.h>

#define	flat_argvp_envp_on_stack()		0
#define	flat_old_ram_flag(flags)		(flags)

extern unsigned long bfin_get_addr_from_rp (u32 *ptr, u32 relval,
					u32 flags, u32 *persistent);

extern void bfin_put_addr_at_rp(u32 *ptr, u32 addr, u32 relval);

/* The amount by which a relocation can exceed the program image limits
   without being regarded as an error.  */

#define	flat_reloc_valid(reloc, size)	((reloc) <= (size))

static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr, u32 *persistent)
{
	*addr = bfin_get_addr_from_rp(rp, relval, flags, persistent);
	return 0;
}

static inline int flat_put_addr_at_rp(u32 __user *rp, u32 val, u32 relval)
{
	bfin_put_addr_at_rp(rp, val, relval);
	return 0;
}

/* Convert a relocation entry into an address.  */
static inline unsigned long
flat_get_relocate_addr (unsigned long relval)
{
	return relval & 0x03ffffff; /* Mask out top 6 bits */
}

static inline int flat_set_persistent(unsigned long relval,
				      unsigned long *persistent)
{
	int type = (relval >> 26) & 7;
	if (type == 3) {
		*persistent = relval << 16;
		return 1;
	}
	return 0;
}

static inline int flat_addr_absolute(unsigned long relval)
{
	return (relval & (1 << 29)) != 0;
}

#endif				/* __BLACKFIN_FLAT_H__ */
