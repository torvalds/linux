// SPDX-License-Identifier: GPL-2.0-only
/*
 *  a.out loader for x86-64
 *
 *  Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *  Hacked together by Andi Kleen
 */

#include <linux/module.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/perf_event.h>
#include <linux/sched/task_stack.h>

#include <linux/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/user32.h>
#include <asm/ia32.h>

#undef WARN_OLD

static int load_aout_binary(struct linux_binprm *);
static int load_aout_library(struct file *);

static struct linux_binfmt aout_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_aout_binary,
	.load_shlib	= load_aout_library,
};

static int set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end <= start)
		return 0;
	return vm_brk(start, end - start);
}


/*
 * create_aout_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static u32 __user *create_aout_tables(char __user *p, struct linux_binprm *bprm)
{
	u32 __user *argv, *envp, *sp;
	int argc = bprm->argc, envc = bprm->envc;

	sp = (u32 __user *) ((-(unsigned long)sizeof(u32)) & (unsigned long) p);
	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_user((unsigned long) envp, --sp);
	put_user((unsigned long) argv, --sp);
	put_user(argc, --sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-- > 0) {
		char c;

		put_user((u32)(unsigned long)p, argv++);
		do {
			get_user(c, p++);
		} while (c);
	}
	put_user(0, argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-- > 0) {
		char c;

		put_user((u32)(unsigned long)p, envp++);
		do {
			get_user(c, p++);
		} while (c);
	}
	put_user(0, envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}

/*
 * These are the functions used to load a.out style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */
static int load_aout_binary(struct linux_binprm *bprm)
{
	unsigned long error, fd_offset, rlim;
	struct pt_regs *regs = current_pt_regs();
	struct exec ex;
	int retval;

	ex = *((struct exec *) bprm->buf);		/* exec-header */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    i_size_read(file_inode(bprm->file)) <
	    ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	fd_offset = N_TXTOFF(ex);

	/* Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = rlimit(RLIMIT_DATA);
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (ex.a_data + ex.a_bss > rlim)
		return -ENOMEM;

	/* Flush all traces of the currently running executable */
	retval = begin_new_exec(bprm);
	if (retval)
		return retval;

	/* OK, This is the point of no return */
	set_personality(PER_LINUX);
	set_personality_ia32(false);

	setup_new_exec(bprm);

	regs->cs = __USER32_CS;
	regs->r8 = regs->r9 = regs->r10 = regs->r11 = regs->r12 =
		regs->r13 = regs->r14 = regs->r15 = 0;

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));

	retval = setup_arg_pages(bprm, IA32_STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0)
		return retval;

	if (N_MAGIC(ex) == OMAGIC) {
		unsigned long text_addr, map_size;

		text_addr = N_TXTADDR(ex);
		map_size = ex.a_text+ex.a_data;

		error = vm_brk(text_addr & PAGE_MASK, map_size);

		if (error)
			return error;

		error = read_code(bprm->file, text_addr, 32,
				  ex.a_text + ex.a_data);
		if ((signed long)error < 0)
			return error;
	} else {
#ifdef WARN_OLD
		static unsigned long error_time, error_time2;
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) &&
				time_after(jiffies, error_time2 + 5*HZ)) {
			printk(KERN_NOTICE "executable not page aligned\n");
			error_time2 = jiffies;
		}

		if ((fd_offset & ~PAGE_MASK) != 0 &&
			    time_after(jiffies, error_time + 5*HZ)) {
			printk(KERN_WARNING
			       "fd_offset is not page aligned. Please convert "
			       "program: %pD\n",
			       bprm->file);
			error_time = jiffies;
		}
#endif

		if (!bprm->file->f_op->mmap || (fd_offset & ~PAGE_MASK) != 0) {
			error = vm_brk(N_TXTADDR(ex), ex.a_text+ex.a_data);
			if (error)
				return error;

			read_code(bprm->file, N_TXTADDR(ex), fd_offset,
					ex.a_text+ex.a_data);
			goto beyond_if;
		}

		error = vm_mmap(bprm->file, N_TXTADDR(ex), ex.a_text,
				PROT_READ | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE |
				MAP_EXECUTABLE | MAP_32BIT,
				fd_offset);

		if (error != N_TXTADDR(ex))
			return error;

		error = vm_mmap(bprm->file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE |
				MAP_EXECUTABLE | MAP_32BIT,
				fd_offset + ex.a_text);
		if (error != N_DATADDR(ex))
			return error;
	}

beyond_if:
	error = set_brk(current->mm->start_brk, current->mm->brk);
	if (error)
		return error;

	set_binfmt(&aout_format);

	current->mm->start_stack =
		(unsigned long)create_aout_tables((char __user *)bprm->p, bprm);
	/* start thread */
	loadsegment(fs, 0);
	loadsegment(ds, __USER32_DS);
	loadsegment(es, __USER32_DS);
	load_gs_index(0);
	(regs)->ip = ex.a_entry;
	(regs)->sp = current->mm->start_stack;
	(regs)->flags = 0x200;
	(regs)->cs = __USER32_CS;
	(regs)->ss = __USER32_DS;
	regs->r8 = regs->r9 = regs->r10 = regs->r11 =
	regs->r12 = regs->r13 = regs->r14 = regs->r15 = 0;
	set_fs(USER_DS);
	return 0;
}

static int load_aout_library(struct file *file)
{
	unsigned long bss, start_addr, len, error;
	int retval;
	struct exec ex;
	loff_t pos = 0;

	retval = -ENOEXEC;
	error = kernel_read(file, &ex, sizeof(ex), &pos);
	if (error != sizeof(ex))
		goto out;

	/* We come in here for the regular a.out style of shared libraries */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || N_TRSIZE(ex) ||
	    N_DRSIZE(ex) || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    i_size_read(file_inode(file)) <
	    ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		goto out;
	}

	if (N_FLAGS(ex))
		goto out;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	if ((N_TXTOFF(ex) & ~PAGE_MASK) != 0) {
#ifdef WARN_OLD
		static unsigned long error_time;
		if (time_after(jiffies, error_time + 5*HZ)) {
			printk(KERN_WARNING
			       "N_TXTOFF is not page aligned. Please convert "
			       "library: %pD\n",
			       file);
			error_time = jiffies;
		}
#endif
		retval = vm_brk(start_addr, ex.a_text + ex.a_data + ex.a_bss);
		if (retval)
			goto out;

		read_code(file, start_addr, N_TXTOFF(ex),
			  ex.a_text + ex.a_data);
		retval = 0;
		goto out;
	}
	/* Now use mmap to map the library into memory. */
	error = vm_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_32BIT,
			N_TXTOFF(ex));
	retval = error;
	if (error != start_addr)
		goto out;

	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		retval = vm_brk(start_addr + len, bss - len);
		if (retval)
			goto out;
	}
	retval = 0;
out:
	return retval;
}

static int __init init_aout_binfmt(void)
{
	register_binfmt(&aout_format);
	return 0;
}

static void __exit exit_aout_binfmt(void)
{
	unregister_binfmt(&aout_format);
}

module_init(init_aout_binfmt);
module_exit(exit_aout_binfmt);
MODULE_LICENSE("GPL");
