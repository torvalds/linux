// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/cachectl.h>
#include <asm/proc-fns.h>
#include <asm/fpu.h>
#include <asm/fp_udfiex_crtl.h>

SYSCALL_DEFINE6(mmap2, unsigned long, addr, unsigned long, len,
	       unsigned long, prot, unsigned long, flags,
	       unsigned long, fd, unsigned long, pgoff)
{
	if (pgoff & (~PAGE_MASK >> 12))
		return -EINVAL;

	return sys_mmap_pgoff(addr, len, prot, flags, fd,
			      pgoff >> (PAGE_SHIFT - 12));
}

SYSCALL_DEFINE4(fadvise64_64_wrapper,int, fd, int, advice, loff_t, offset,
					 loff_t, len)
{
	return sys_fadvise64_64(fd, offset, len, advice);
}

SYSCALL_DEFINE3(cacheflush, unsigned int, start, unsigned int, end, int, cache)
{
	struct vm_area_struct *vma;
	bool flushi = true, wbd = true;

	vma = find_vma(current->mm, start);
	if (!vma)
		return -EFAULT;
	switch (cache) {
	case ICACHE:
		wbd = false;
		break;
	case DCACHE:
		flushi = false;
		break;
	case BCACHE:
		break;
	default:
		return -EINVAL;
	}
	cpu_cache_wbinval_range_check(vma, start, end, flushi, wbd);

	return 0;
}

SYSCALL_DEFINE2(fp_udfiex_crtl, unsigned int, cmd, unsigned int, act)
{
#if IS_ENABLED(CONFIG_SUPPORT_DENORMAL_ARITHMETIC)
	int old_udf_iex;

	if (!used_math()) {
		load_fpu(&init_fpuregs);
		current->thread.fpu.UDF_IEX_trap = init_fpuregs.UDF_IEX_trap;
		set_used_math();
	}

	old_udf_iex = current->thread.fpu.UDF_IEX_trap;
	act &= (FPCSR_mskUDFE | FPCSR_mskIEXE);

	switch (cmd) {
	case DISABLE_UDF_IEX_TRAP:
		current->thread.fpu.UDF_IEX_trap &= ~act;
		break;
	case ENABLE_UDF_IEX_TRAP:
		current->thread.fpu.UDF_IEX_trap |= act;
		break;
	case GET_UDF_IEX_TRAP:
		break;
	default:
		return -EINVAL;
	}
	return old_udf_iex;
#else
	return -ENOTSUPP;
#endif
}
