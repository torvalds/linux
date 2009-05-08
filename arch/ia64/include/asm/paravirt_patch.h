/******************************************************************************
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __ASM_PARAVIRT_PATCH_H
#define __ASM_PARAVIRT_PATCH_H

#ifdef __ASSEMBLY__

	.section .paravirt_branches, "a"
	.previous
#define PARAVIRT_PATCH_SITE_BR(type)		\
	{					\
	[1:] ;					\
	br.cond.sptk.many 2f ;			\
	nop.b 0 ;				\
	nop.b 0;; ;				\
	} ;					\
	2:					\
	.xdata8 ".paravirt_branches", 1b, type

#else

#include <linux/stringify.h>
#include <asm/intrinsics.h>

/* for binary patch */
struct paravirt_patch_site_bundle {
	void		*sbundle;
	void		*ebundle;
	unsigned long	type;
};

/* label means the beginning of new bundle */
#define paravirt_alt_bundle(instr, privop)				\
	"\t998:\n"							\
	"\t" instr "\n"							\
	"\t999:\n"							\
	"\t.pushsection .paravirt_bundles, \"a\"\n"			\
	"\t.popsection\n"						\
	"\t.xdata8 \".paravirt_bundles\", 998b, 999b, "			\
	__stringify(privop) "\n"


struct paravirt_patch_bundle_elem {
	const void	*sbundle;
	const void	*ebundle;
	unsigned long	type;
};


struct paravirt_patch_site_inst {
	unsigned long	stag;
	unsigned long	etag;
	unsigned long	type;
};

#define paravirt_alt_inst(instr, privop)				\
	"\t[998:]\n"							\
	"\t" instr "\n"							\
	"\t[999:]\n"							\
	"\t.pushsection .paravirt_insts, \"a\"\n"			\
	"\t.popsection\n"						\
	"\t.xdata8 \".paravirt_insts\", 998b, 999b, "			\
	__stringify(privop) "\n"

struct paravirt_patch_site_branch {
	unsigned long	tag;
	unsigned long	type;
};

struct paravirt_patch_branch_target {
	const void	*entry;
	unsigned long	type;
};

void
__paravirt_patch_apply_branch(
	unsigned long tag, unsigned long type,
	const struct paravirt_patch_branch_target *entries,
	unsigned int nr_entries);

void
paravirt_patch_reloc_br(unsigned long tag, const void *target);

void
paravirt_patch_reloc_brl(unsigned long tag, const void *target);


#if defined(ASM_SUPPORTED) && defined(CONFIG_PARAVIRT)
unsigned long
ia64_native_patch_bundle(void *sbundle, void *ebundle, unsigned long type);

unsigned long
__paravirt_patch_apply_bundle(void *sbundle, void *ebundle, unsigned long type,
			      const struct paravirt_patch_bundle_elem *elems,
			      unsigned long nelems,
			      const struct paravirt_patch_bundle_elem **found);

void
paravirt_patch_apply_bundle(const struct paravirt_patch_site_bundle *start,
			    const struct paravirt_patch_site_bundle *end);

void
paravirt_patch_apply_inst(const struct paravirt_patch_site_inst *start,
			  const struct paravirt_patch_site_inst *end);

void paravirt_patch_apply(void);
#else
#define paravirt_patch_apply_bundle(start, end)	do { } while (0)
#define paravirt_patch_apply_inst(start, end)	do { } while (0)
#define paravirt_patch_apply()			do { } while (0)
#endif

#endif /* !__ASSEMBLEY__ */

#endif /* __ASM_PARAVIRT_PATCH_H */

/*
 * Local variables:
 * mode: C
 * c-set-style: "linux"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */
