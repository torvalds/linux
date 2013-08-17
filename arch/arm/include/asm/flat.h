/*
 * arch/arm/include/asm/flat.h -- uClinux flat-format executables
 */

#ifndef __ARM_FLAT_H__
#define __ARM_FLAT_H__

#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		(flags)
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))
#define	flat_get_addr_from_rp(rp, relval, flags, persistent) get_unaligned(rp)
#define	flat_put_addr_at_rp(rp, val, relval)	put_unaligned(val,rp)
#define	flat_get_relocate_addr(rel)		(rel)
#define	flat_set_persistent(relval, p)		0

#endif /* __ARM_FLAT_H__ */
