// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <init.h>
#include <as-layout.h>
#include <mm_id.h>
#include <os.h>
#include <ptrace_user.h>
#include <registers.h>
#include <skas.h>
#include <sysdep/ptrace.h>
#include <sysdep/stub.h>
#include "../internal.h"

extern char batch_syscall_stub[], __syscall_stub_start[];

static inline unsigned long *check_init_stack(struct mm_id * mm_idp,
					      unsigned long *stack)
{
	if (stack == NULL) {
		stack = (unsigned long *) mm_idp->stack + 2;
		*stack = 0;
	}
	return stack;
}

static unsigned long syscall_regs[MAX_REG_NR];

static int __init init_syscall_regs(void)
{
	get_safe_registers(syscall_regs, NULL);
	syscall_regs[REGS_IP_INDEX] = STUB_CODE +
		((unsigned long) batch_syscall_stub -
		 (unsigned long) __syscall_stub_start);
	syscall_regs[REGS_SP_INDEX] = STUB_DATA;

	return 0;
}

__initcall(init_syscall_regs);

static inline long do_syscall_stub(struct mm_id * mm_idp, void **addr)
{
	int n, i;
	long ret, offset;
	unsigned long * data;
	unsigned long * syscall;
	int err, pid = mm_idp->u.pid;

	n = ptrace_setregs(pid, syscall_regs);
	if (n < 0) {
		printk(UM_KERN_ERR "Registers - \n");
		for (i = 0; i < MAX_REG_NR; i++)
			printk(UM_KERN_ERR "\t%d\t0x%lx\n", i, syscall_regs[i]);
		panic("%s : PTRACE_SETREGS failed, errno = %d\n",
		      __func__, -n);
	}

	err = ptrace(PTRACE_CONT, pid, 0, 0);
	if (err)
		panic("Failed to continue stub, pid = %d, errno = %d\n", pid,
		      errno);

	wait_stub_done(pid);

	/*
	 * When the stub stops, we find the following values on the
	 * beginning of the stack:
	 * (long )return_value
	 * (long )offset to failed sycall-data (0, if no error)
	 */
	ret = *((unsigned long *) mm_idp->stack);
	offset = *((unsigned long *) mm_idp->stack + 1);
	if (offset) {
		data = (unsigned long *)(mm_idp->stack + offset - STUB_DATA);
		printk(UM_KERN_ERR "%s : ret = %ld, offset = %ld, data = %p\n",
		       __func__, ret, offset, data);
		syscall = (unsigned long *)((unsigned long)data + data[0]);
		printk(UM_KERN_ERR "%s: syscall %ld failed, return value = 0x%lx, expected return value = 0x%lx\n",
		       __func__, syscall[0], ret, syscall[7]);
		printk(UM_KERN_ERR "    syscall parameters: 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
		       syscall[1], syscall[2], syscall[3],
		       syscall[4], syscall[5], syscall[6]);
		for (n = 1; n < data[0]/sizeof(long); n++) {
			if (n == 1)
				printk(UM_KERN_ERR "    additional syscall data:");
			if (n % 4 == 1)
				printk("\n" UM_KERN_ERR "      ");
			printk("  0x%lx", data[n]);
		}
		if (n > 1)
			printk("\n");
	}
	else ret = 0;

	*addr = check_init_stack(mm_idp, NULL);

	return ret;
}

long run_syscall_stub(struct mm_id * mm_idp, int syscall,
		      unsigned long *args, long expected, void **addr,
		      int done)
{
	unsigned long *stack = check_init_stack(mm_idp, *addr);

	*stack += sizeof(long);
	stack += *stack / sizeof(long);

	*stack++ = syscall;
	*stack++ = args[0];
	*stack++ = args[1];
	*stack++ = args[2];
	*stack++ = args[3];
	*stack++ = args[4];
	*stack++ = args[5];
	*stack++ = expected;
	*stack = 0;

	if (!done && ((((unsigned long) stack) & ~UM_KERN_PAGE_MASK) <
		     UM_KERN_PAGE_SIZE - 10 * sizeof(long))) {
		*addr = stack;
		return 0;
	}

	return do_syscall_stub(mm_idp, addr);
}

long syscall_stub_data(struct mm_id * mm_idp,
		       unsigned long *data, int data_count,
		       void **addr, void **stub_addr)
{
	unsigned long *stack;
	int ret = 0;

	/*
	 * If *addr still is uninitialized, it *must* contain NULL.
	 * Thus in this case do_syscall_stub correctly won't be called.
	 */
	if ((((unsigned long) *addr) & ~UM_KERN_PAGE_MASK) >=
	   UM_KERN_PAGE_SIZE - (10 + data_count) * sizeof(long)) {
		ret = do_syscall_stub(mm_idp, addr);
		/* in case of error, don't overwrite data on stack */
		if (ret)
			return ret;
	}

	stack = check_init_stack(mm_idp, *addr);
	*addr = stack;

	*stack = data_count * sizeof(long);

	memcpy(stack + 1, data, data_count * sizeof(long));

	*stub_addr = (void *)(((unsigned long)(stack + 1) &
			       ~UM_KERN_PAGE_MASK) + STUB_DATA);

	return 0;
}

int map(struct mm_id * mm_idp, unsigned long virt, unsigned long len, int prot,
	int phys_fd, unsigned long long offset, int done, void **data)
{
	int ret;
	unsigned long args[] = { virt, len, prot,
				 MAP_SHARED | MAP_FIXED, phys_fd,
				 MMAP_OFFSET(offset) };

	ret = run_syscall_stub(mm_idp, STUB_MMAP_NR, args, virt,
			       data, done);

	return ret;
}

int unmap(struct mm_id * mm_idp, unsigned long addr, unsigned long len,
	  int done, void **data)
{
	int ret;
	unsigned long args[] = { (unsigned long) addr, len, 0, 0, 0,
				 0 };

	ret = run_syscall_stub(mm_idp, __NR_munmap, args, 0,
			       data, done);

	return ret;
}

int protect(struct mm_id * mm_idp, unsigned long addr, unsigned long len,
	    unsigned int prot, int done, void **data)
{
	int ret;
	unsigned long args[] = { addr, len, prot, 0, 0, 0 };

	ret = run_syscall_stub(mm_idp, __NR_mprotect, args, 0,
			       data, done);

	return ret;
}
