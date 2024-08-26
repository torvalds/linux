// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
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

extern char __syscall_stub_start[];

void syscall_stub_dump_error(struct mm_id *mm_idp)
{
	struct stub_data *proc_data = (void *)mm_idp->stack;
	struct stub_syscall *sc;

	if (proc_data->syscall_data_len < 0 ||
	    proc_data->syscall_data_len >= ARRAY_SIZE(proc_data->syscall_data))
		panic("Syscall data was corrupted by stub (len is: %d, expected maximum: %d)!",
			proc_data->syscall_data_len,
			mm_idp->syscall_data_len);

	sc = &proc_data->syscall_data[proc_data->syscall_data_len];

	printk(UM_KERN_ERR "%s : length = %d, last offset = %d",
		__func__, mm_idp->syscall_data_len,
		proc_data->syscall_data_len);
	printk(UM_KERN_ERR "%s : stub syscall type %d failed, return value = 0x%lx\n",
		__func__, sc->syscall, proc_data->err);

	print_hex_dump(UM_KERN_ERR, "    syscall data: ", 0,
		       16, 4, sc, sizeof(*sc), 0);
}

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
		((unsigned long) stub_syscall_handler -
		 (unsigned long) __syscall_stub_start);
	syscall_regs[REGS_SP_INDEX] = STUB_DATA +
		offsetof(struct stub_data, sigstack) +
		sizeof(((struct stub_data *) 0)->sigstack) -
		sizeof(void *);

	return 0;
}

__initcall(init_syscall_regs);

static inline long do_syscall_stub(struct mm_id *mm_idp)
{
	struct stub_data *proc_data = (void *)mm_idp->stack;
	int n, i;
	int err, pid = mm_idp->pid;

	n = ptrace_setregs(pid, syscall_regs);
	if (n < 0) {
		printk(UM_KERN_ERR "Registers - \n");
		for (i = 0; i < MAX_REG_NR; i++)
			printk(UM_KERN_ERR "\t%d\t0x%lx\n", i, syscall_regs[i]);
		panic("%s : PTRACE_SETREGS failed, errno = %d\n",
		      __func__, -n);
	}

	/* Inform process how much we have filled in. */
	proc_data->syscall_data_len = mm_idp->syscall_data_len;

	err = ptrace(PTRACE_CONT, pid, 0, 0);
	if (err)
		panic("Failed to continue stub, pid = %d, errno = %d\n", pid,
		      errno);

	wait_stub_done(pid);

	/*
	 * proc_data->err will be non-zero if there was an (unexpected) error.
	 * In that case, syscall_data_len points to the last executed syscall,
	 * otherwise it will be zero (but we do not need to rely on that).
	 */
	if (proc_data->err < 0) {
		syscall_stub_dump_error(mm_idp);

		/* Store error code in case someone tries to add more syscalls */
		mm_idp->syscall_data_len = proc_data->err;
	} else {
		mm_idp->syscall_data_len = 0;
	}

	return mm_idp->syscall_data_len;
}

int syscall_stub_flush(struct mm_id *mm_idp)
{
	int res;

	if (mm_idp->syscall_data_len == 0)
		return 0;

	/* If an error happened already, report it and reset the state. */
	if (mm_idp->syscall_data_len < 0) {
		res = mm_idp->syscall_data_len;
		mm_idp->syscall_data_len = 0;
		return res;
	}

	res = do_syscall_stub(mm_idp);
	mm_idp->syscall_data_len = 0;

	return res;
}

struct stub_syscall *syscall_stub_alloc(struct mm_id *mm_idp)
{
	struct stub_syscall *sc;
	struct stub_data *proc_data = (struct stub_data *) mm_idp->stack;

	if (mm_idp->syscall_data_len > 0 &&
	    mm_idp->syscall_data_len == ARRAY_SIZE(proc_data->syscall_data))
		do_syscall_stub(mm_idp);

	if (mm_idp->syscall_data_len < 0) {
		/* Return dummy to retain error state. */
		sc = &proc_data->syscall_data[0];
	} else {
		sc = &proc_data->syscall_data[mm_idp->syscall_data_len];
		mm_idp->syscall_data_len += 1;
	}
	memset(sc, 0, sizeof(*sc));

	return sc;
}

static struct stub_syscall *syscall_stub_get_previous(struct mm_id *mm_idp,
						      int syscall_type,
						      unsigned long virt)
{
	if (mm_idp->syscall_data_len > 0) {
		struct stub_data *proc_data = (void *) mm_idp->stack;
		struct stub_syscall *sc;

		sc = &proc_data->syscall_data[mm_idp->syscall_data_len - 1];

		if (sc->syscall == syscall_type &&
		    sc->mem.addr + sc->mem.length == virt)
			return sc;
	}

	return NULL;
}

int map(struct mm_id *mm_idp, unsigned long virt, unsigned long len, int prot,
	int phys_fd, unsigned long long offset)
{
	struct stub_syscall *sc;

	/* Compress with previous syscall if that is possible */
	sc = syscall_stub_get_previous(mm_idp, STUB_SYSCALL_MMAP, virt);
	if (sc && sc->mem.prot == prot && sc->mem.fd == phys_fd &&
	    sc->mem.offset == MMAP_OFFSET(offset - sc->mem.length)) {
		sc->mem.length += len;
		return 0;
	}

	sc = syscall_stub_alloc(mm_idp);
	sc->syscall = STUB_SYSCALL_MMAP;
	sc->mem.addr = virt;
	sc->mem.length = len;
	sc->mem.prot = prot;
	sc->mem.fd = phys_fd;
	sc->mem.offset = MMAP_OFFSET(offset);

	return 0;
}

int unmap(struct mm_id *mm_idp, unsigned long addr, unsigned long len)
{
	struct stub_syscall *sc;

	/* Compress with previous syscall if that is possible */
	sc = syscall_stub_get_previous(mm_idp, STUB_SYSCALL_MUNMAP, addr);
	if (sc) {
		sc->mem.length += len;
		return 0;
	}

	sc = syscall_stub_alloc(mm_idp);
	sc->syscall = STUB_SYSCALL_MUNMAP;
	sc->mem.addr = addr;
	sc->mem.length = len;

	return 0;
}

int protect(struct mm_id *mm_idp, unsigned long addr, unsigned long len,
	    unsigned int prot)
{
	struct stub_syscall *sc;

	/* Compress with previous syscall if that is possible */
	sc = syscall_stub_get_previous(mm_idp, STUB_SYSCALL_MPROTECT, addr);
	if (sc && sc->mem.prot == prot) {
		sc->mem.length += len;
		return 0;
	}

	sc = syscall_stub_alloc(mm_idp);
	sc->syscall = STUB_SYSCALL_MPROTECT;
	sc->mem.addr = addr;
	sc->mem.length = len;
	sc->mem.prot = prot;

	return 0;
}
