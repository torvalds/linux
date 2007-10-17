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
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/user32.h>
#include <asm/ia32.h>

#undef WARN_OLD
#undef CORE_DUMP /* probably broken */

static int load_aout_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout_library(struct file*);

#ifdef CORE_DUMP
static int aout_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit);

/*
 * fill in the user structure for a core dump..
 */
static void dump_thread32(struct pt_regs * regs, struct user32 * dump)
{
	u32 fs,gs;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->rsp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	dump->u_debugreg[0] = current->thread.debugreg0;  
	dump->u_debugreg[1] = current->thread.debugreg1;  
	dump->u_debugreg[2] = current->thread.debugreg2;  
	dump->u_debugreg[3] = current->thread.debugreg3;  
	dump->u_debugreg[4] = 0;  
	dump->u_debugreg[5] = 0;  
	dump->u_debugreg[6] = current->thread.debugreg6;  
	dump->u_debugreg[7] = current->thread.debugreg7;  

	if (dump->start_stack < 0xc0000000)
		dump->u_ssize = ((unsigned long) (0xc0000000 - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs.ebx = regs->rbx;
	dump->regs.ecx = regs->rcx;
	dump->regs.edx = regs->rdx;
	dump->regs.esi = regs->rsi;
	dump->regs.edi = regs->rdi;
	dump->regs.ebp = regs->rbp;
	dump->regs.eax = regs->rax;
	dump->regs.ds = current->thread.ds;
	dump->regs.es = current->thread.es;
	asm("movl %%fs,%0" : "=r" (fs)); dump->regs.fs = fs;
	asm("movl %%gs,%0" : "=r" (gs)); dump->regs.gs = gs; 
	dump->regs.orig_eax = regs->orig_rax;
	dump->regs.eip = regs->rip;
	dump->regs.cs = regs->cs;
	dump->regs.eflags = regs->eflags;
	dump->regs.esp = regs->rsp;
	dump->regs.ss = regs->ss;

#if 1 /* FIXME */
	dump->u_fpvalid = 0;
#else
	dump->u_fpvalid = dump_fpu (regs, &dump->i387);
#endif
}

#endif

static struct linux_binfmt aout_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_aout_binary,
	.load_shlib	= load_aout_library,
#ifdef CORE_DUMP
	.core_dump	= aout_core_dump,
#endif
	.min_coredump	= PAGE_SIZE
};

static void set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end <= start)
		return;
	down_write(&current->mm->mmap_sem);
	do_brk(start, end - start);
	up_write(&current->mm->mmap_sem);
}

#ifdef CORE_DUMP
/*
 * These are the only things you should do on a core-file: use only these
 * macros to write out all the necessary info.
 */

static int dump_write(struct file *file, const void *addr, int nr)
{
	return file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}

#define DUMP_WRITE(addr, nr)	\
	if (!dump_write(file, (void *)(addr), (nr))) \
		goto end_coredump;

#define DUMP_SEEK(offset) \
if (file->f_op->llseek) { \
	if (file->f_op->llseek(file,(offset),0) != (offset)) \
 		goto end_coredump; \
} else file->f_pos = (offset)

/*
 * Routine writes a core dump image in the current directory.
 * Currently only a stub-function.
 *
 * Note that setuid/setgid files won't make a core-dump if the uid/gid
 * changed due to the set[u|g]id. It's enforced by the "current->mm->dumpable"
 * field, which also makes sure the core-dumps won't be recursive if the
 * dumping of the process results in another error..
 */

static int aout_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit)
{
	mm_segment_t fs;
	int has_dumped = 0;
	unsigned long dump_start, dump_size;
	struct user32 dump;
#       define START_DATA(u)	(u.u_tsize << PAGE_SHIFT)
#       define START_STACK(u)   (u.start_stack)

	fs = get_fs();
	set_fs(KERNEL_DS);
	has_dumped = 1;
	current->flags |= PF_DUMPCORE;
       	strncpy(dump.u_comm, current->comm, sizeof(current->comm));
	dump.u_ar0 = (u32)(((unsigned long)(&dump.regs)) - ((unsigned long)(&dump)));
	dump.signal = signr;
	dump_thread32(regs, &dump);

/* If the size of the dump file exceeds the rlimit, then see what would happen
   if we wrote the stack, but not the data area.  */
	if ((dump.u_dsize + dump.u_ssize + 1) * PAGE_SIZE > limit)
		dump.u_dsize = 0;

/* Make sure we have enough room to write the stack and data areas. */
	if ((dump.u_ssize + 1) * PAGE_SIZE > limit)
		dump.u_ssize = 0;

/* make sure we actually have a data and stack area to dump */
	set_fs(USER_DS);
	if (!access_ok(VERIFY_READ, (void *) (unsigned long)START_DATA(dump), dump.u_dsize << PAGE_SHIFT))
		dump.u_dsize = 0;
	if (!access_ok(VERIFY_READ, (void *) (unsigned long)START_STACK(dump), dump.u_ssize << PAGE_SHIFT))
		dump.u_ssize = 0;

	set_fs(KERNEL_DS);
/* struct user */
	DUMP_WRITE(&dump,sizeof(dump));
/* Now dump all of the user data.  Include malloced stuff as well */
	DUMP_SEEK(PAGE_SIZE);
/* now we start writing out the user space info */
	set_fs(USER_DS);
/* Dump the data area */
	if (dump.u_dsize != 0) {
		dump_start = START_DATA(dump);
		dump_size = dump.u_dsize << PAGE_SHIFT;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Now prepare to dump the stack area */
	if (dump.u_ssize != 0) {
		dump_start = START_STACK(dump);
		dump_size = dump.u_ssize << PAGE_SHIFT;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Finally dump the task struct.  Not be used by gdb, but could be useful */
	set_fs(KERNEL_DS);
	DUMP_WRITE(current,sizeof(*current));
end_coredump:
	set_fs(fs);
	return has_dumped;
}
#endif

/*
 * create_aout_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static u32 __user *create_aout_tables(char __user *p, struct linux_binprm *bprm)
{
	u32 __user *argv;
	u32 __user *envp;
	u32 __user *sp;
	int argc = bprm->argc;
	int envc = bprm->envc;

	sp = (u32 __user *) ((-(unsigned long)sizeof(u32)) & (unsigned long) p);
	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_user((unsigned long) envp,--sp);
	put_user((unsigned long) argv,--sp);
	put_user(argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		char c;
		put_user((u32)(unsigned long)p,argv++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(0, argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		char c;
		put_user((u32)(unsigned long)p,envp++);
		do {
			get_user(c,p++);
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

static int load_aout_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct exec ex;
	unsigned long error;
	unsigned long fd_offset;
	unsigned long rlim;
	int retval;

	ex = *((struct exec *) bprm->buf);		/* exec-header */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    i_size_read(bprm->file->f_path.dentry->d_inode) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	fd_offset = N_TXTOFF(ex);

	/* Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = current->signal->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (ex.a_data + ex.a_bss > rlim)
		return -ENOMEM;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		return retval;

	regs->cs = __USER32_CS; 
	regs->r8 = regs->r9 = regs->r10 = regs->r11 = regs->r12 =
		regs->r13 = regs->r14 = regs->r15 = 0;

	/* OK, This is the point of no return */
	set_personality(PER_LINUX);
	set_thread_flag(TIF_IA32); 
	clear_thread_flag(TIF_ABI_PENDING);

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));
	current->mm->free_area_cache = TASK_UNMAPPED_BASE;
	current->mm->cached_hole_size = 0;

	current->mm->mmap = NULL;
	compute_creds(bprm);
 	current->flags &= ~PF_FORKNOEXEC;

	if (N_MAGIC(ex) == OMAGIC) {
		unsigned long text_addr, map_size;
		loff_t pos;

		text_addr = N_TXTADDR(ex);

		pos = 32;
		map_size = ex.a_text+ex.a_data;

		down_write(&current->mm->mmap_sem);
		error = do_brk(text_addr & PAGE_MASK, map_size);
		up_write(&current->mm->mmap_sem);

		if (error != (text_addr & PAGE_MASK)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

		error = bprm->file->f_op->read(bprm->file,
			 (char __user *)text_addr,
			  ex.a_text+ex.a_data, &pos);
		if ((signed long)error < 0) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
			 
		flush_icache_range(text_addr, text_addr+ex.a_text+ex.a_data);
	} else {
#ifdef WARN_OLD
		static unsigned long error_time, error_time2;
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) && (jiffies-error_time2) > 5*HZ)
		{
			printk(KERN_NOTICE "executable not page aligned\n");
			error_time2 = jiffies;
		}

		if ((fd_offset & ~PAGE_MASK) != 0 &&
		    (jiffies-error_time) > 5*HZ)
		{
			printk(KERN_WARNING 
			       "fd_offset is not page aligned. Please convert program: %s\n",
			       bprm->file->f_path.dentry->d_name.name);
			error_time = jiffies;
		}
#endif

		if (!bprm->file->f_op->mmap||((fd_offset & ~PAGE_MASK) != 0)) {
			loff_t pos = fd_offset;
			down_write(&current->mm->mmap_sem);
			do_brk(N_TXTADDR(ex), ex.a_text+ex.a_data);
			up_write(&current->mm->mmap_sem);
			bprm->file->f_op->read(bprm->file,
					(char __user *)N_TXTADDR(ex),
					ex.a_text+ex.a_data, &pos);
			flush_icache_range((unsigned long) N_TXTADDR(ex),
					   (unsigned long) N_TXTADDR(ex) +
					   ex.a_text+ex.a_data);
			goto beyond_if;
		}

		down_write(&current->mm->mmap_sem);
		error = do_mmap(bprm->file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE | MAP_32BIT,
			fd_offset);
		up_write(&current->mm->mmap_sem);

		if (error != N_TXTADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

		down_write(&current->mm->mmap_sem);
 		error = do_mmap(bprm->file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE | MAP_32BIT,
				fd_offset + ex.a_text);
		up_write(&current->mm->mmap_sem);
		if (error != N_DATADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	set_binfmt(&aout_format);

	set_brk(current->mm->start_brk, current->mm->brk);

	retval = setup_arg_pages(bprm, IA32_STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0) { 
		/* Someone check-me: is this error path enough? */ 
		send_sig(SIGKILL, current, 0); 
		return retval;
	}

	current->mm->start_stack =
		(unsigned long)create_aout_tables((char __user *)bprm->p, bprm);
	/* start thread */
	asm volatile("movl %0,%%fs" :: "r" (0)); \
	asm volatile("movl %0,%%es; movl %0,%%ds": :"r" (__USER32_DS));
	load_gs_index(0); 
	(regs)->rip = ex.a_entry;
	(regs)->rsp = current->mm->start_stack;
	(regs)->eflags = 0x200;
	(regs)->cs = __USER32_CS;
	(regs)->ss = __USER32_DS;
	regs->r8 = regs->r9 = regs->r10 = regs->r11 =
	regs->r12 = regs->r13 = regs->r14 = regs->r15 = 0;
	set_fs(USER_DS);
	if (unlikely(current->ptrace & PT_PTRACED)) {
		if (current->ptrace & PT_TRACE_EXEC)
			ptrace_notify ((PTRACE_EVENT_EXEC << 8) | SIGTRAP);
		else
			send_sig(SIGTRAP, current, 0);
	}
	return 0;
}

static int load_aout_library(struct file *file)
{
	struct inode * inode;
	unsigned long bss, start_addr, len;
	unsigned long error;
	int retval;
	struct exec ex;

	inode = file->f_path.dentry->d_inode;

	retval = -ENOEXEC;
	error = kernel_read(file, 0, (char *) &ex, sizeof(ex));
	if (error != sizeof(ex))
		goto out;

	/* We come in here for the regular a.out style of shared libraries */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || N_TRSIZE(ex) ||
	    N_DRSIZE(ex) || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    i_size_read(inode) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		goto out;
	}

	if (N_FLAGS(ex))
		goto out;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	if ((N_TXTOFF(ex) & ~PAGE_MASK) != 0) {
		loff_t pos = N_TXTOFF(ex);

#ifdef WARN_OLD
		static unsigned long error_time;
		if ((jiffies-error_time) > 5*HZ)
		{
			printk(KERN_WARNING 
			       "N_TXTOFF is not page aligned. Please convert library: %s\n",
			       file->f_path.dentry->d_name.name);
			error_time = jiffies;
		}
#endif
		down_write(&current->mm->mmap_sem);
		do_brk(start_addr, ex.a_text + ex.a_data + ex.a_bss);
		up_write(&current->mm->mmap_sem);
		
		file->f_op->read(file, (char __user *)start_addr,
			ex.a_text + ex.a_data, &pos);
		flush_icache_range((unsigned long) start_addr,
				   (unsigned long) start_addr + ex.a_text + ex.a_data);

		retval = 0;
		goto out;
	}
	/* Now use mmap to map the library into memory. */
	down_write(&current->mm->mmap_sem);
	error = do_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_32BIT,
			N_TXTOFF(ex));
	up_write(&current->mm->mmap_sem);
	retval = error;
	if (error != start_addr)
		goto out;

	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		down_write(&current->mm->mmap_sem);
		error = do_brk(start_addr + len, bss - len);
		up_write(&current->mm->mmap_sem);
		retval = error;
		if (error != start_addr + len)
			goto out;
	}
	retval = 0;
out:
	return retval;
}

static int __init init_aout_binfmt(void)
{
	return register_binfmt(&aout_format);
}

static void __exit exit_aout_binfmt(void)
{
	unregister_binfmt(&aout_format);
}

module_init(init_aout_binfmt);
module_exit(exit_aout_binfmt);
MODULE_LICENSE("GPL");
