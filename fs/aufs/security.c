/*
 * Copyright (C) 2012 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * securityf_file_mmap
 */

#include <linux/mman.h>
#include <linux/security.h>
#include "aufs.h"

/* cf. linux/include/linux/mman.h: calc_vm_prot_bits() */
#define AuConv_VM_PROT(f, b)	_calc_vm_trans(f, VM_##b, PROT_##b)

static unsigned long au_arch_prot_conv(unsigned long flags)
{
	/* currently ppc64 only */
#ifdef CONFIG_PPC64
	/* cf. linux/arch/powerpc/include/asm/mman.h */
	AuDebugOn(arch_calc_vm_prot_bits(-1) != VM_SAO);
	return AuConv_VM_PROT(flags, SAO);
#else
	AuDebugOn(arch_calc_vm_prot_bits(-1));
	return 0;
#endif
}

static unsigned long au_prot_conv(unsigned long flags)
{
	return AuConv_VM_PROT(flags, READ)
		| AuConv_VM_PROT(flags, WRITE)
		| AuConv_VM_PROT(flags, EXEC)
		| au_arch_prot_conv(flags);
}

/* cf. linux/include/linux/mman.h: calc_vm_flag_bits() */
#define AuConv_VM_MAP(f, b)	_calc_vm_trans(f, VM_##b, MAP_##b)

static unsigned long au_flag_conv(unsigned long flags)
{
	return AuConv_VM_MAP(flags, GROWSDOWN)
		| AuConv_VM_MAP(flags, DENYWRITE)
		| AuConv_VM_MAP(flags, EXECUTABLE)
		| AuConv_VM_MAP(flags, LOCKED);
}

struct au_security_mmap_file_args {
	int *errp;
	struct file *h_file;
	struct vm_area_struct *vma;
};

/*
 * unnecessary to call security_mmap_file() since it doesn't have file as its
 * argument.
 */
static void au_call_security_mmap_file(void *args)
{
	struct au_security_mmap_file_args *a = args;
	*a->errp = security_mmap_file(a->h_file, au_prot_conv(a->vma->vm_flags),
				      au_flag_conv(a->vma->vm_flags));
}

int au_security_mmap_file(struct file *h_file, struct vm_area_struct *vma)
{
	int err, wkq_err;
	struct au_security_mmap_file_args args = {
		.errp	= &err,
		.h_file	= h_file,
		.vma	= vma
	};

	wkq_err = au_wkq_wait(au_call_security_mmap_file, &args);
	if (unlikely(wkq_err))
		err = wkq_err;
	return err;
}
