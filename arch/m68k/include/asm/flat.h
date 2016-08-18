/*
 * flat.h -- uClinux flat-format executables
 */

#ifndef __M68KNOMMU_FLAT_H__
#define __M68KNOMMU_FLAT_H__

#define	flat_argvp_envp_on_stack()		1
#define	flat_old_ram_flag(flags)		(flags)
#define	flat_reloc_valid(reloc, size)		((reloc) <= (size))
#define	flat_get_addr_from_rp(rp, relval, flags, p) \
	({ unsigned long __val; __get_user_unaligned(__val, rp); __val; })
#define	flat_put_addr_at_rp(rp, val, relval)	__put_user_unaligned(val, rp)
#define	flat_get_relocate_addr(rel)		(rel)

static inline int flat_set_persistent(unsigned long relval,
				      unsigned long *persistent)
{
	return 0;
}

#define FLAT_PLAT_INIT(regs) \
	do { \
		if (current->mm) \
			(regs)->d5 = current->mm->start_data; \
	} while (0)

#endif /* __M68KNOMMU_FLAT_H__ */
