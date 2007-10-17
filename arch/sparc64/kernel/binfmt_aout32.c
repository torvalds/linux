/*
 *  linux/fs/binfmt_aout.c
 *
 *  Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *
 *  Hacked a bit by DaveM to make it work with 32-bit SunOS
 *  binaries on the sparc64 port.
 */

#include <linux/module.h>

#include <linux/sched.h>
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
#include <asm/mmu_context.h>

static int load_aout32_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout32_library(struct file*);
static int aout32_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit);

static struct linux_binfmt aout32_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_aout32_binary,
	.load_shlib	= load_aout32_library,
	.core_dump	= aout32_core_dump,
	.min_coredump	= PAGE_SIZE,
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

static int aout32_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit)
{
	mm_segment_t fs;
	int has_dumped = 0;
	unsigned long dump_start, dump_size;
	struct user dump;
#       define START_DATA(u)    (u.u_tsize)
#       define START_STACK(u)   ((regs->u_regs[UREG_FP]) & ~(PAGE_SIZE - 1))

	fs = get_fs();
	set_fs(KERNEL_DS);
	has_dumped = 1;
	current->flags |= PF_DUMPCORE;
       	strncpy(dump.u_comm, current->comm, sizeof(dump.u_comm));
	dump.signal = signr;
	dump_thread(regs, &dump);

/* If the size of the dump file exceeds the rlimit, then see what would happen
   if we wrote the stack, but not the data area.  */
	if (dump.u_dsize + dump.u_ssize > limit)
		dump.u_dsize = 0;

/* Make sure we have enough room to write the stack and data areas. */
	if (dump.u_ssize > limit)
		dump.u_ssize = 0;

/* make sure we actually have a data and stack area to dump */
	set_fs(USER_DS);
	if (!access_ok(VERIFY_READ, (void __user *) START_DATA(dump), dump.u_dsize))
		dump.u_dsize = 0;
	if (!access_ok(VERIFY_READ, (void __user *) START_STACK(dump), dump.u_ssize))
		dump.u_ssize = 0;

	set_fs(KERNEL_DS);
/* struct user */
	DUMP_WRITE(&dump,sizeof(dump));
/* now we start writing out the user space info */
	set_fs(USER_DS);
/* Dump the data area */
	if (dump.u_dsize != 0) {
		dump_start = START_DATA(dump);
		dump_size = dump.u_dsize;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Now prepare to dump the stack area */
	if (dump.u_ssize != 0) {
		dump_start = START_STACK(dump);
		dump_size = dump.u_ssize;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Finally dump the task struct.  Not be used by gdb, but could be useful */
	set_fs(KERNEL_DS);
	DUMP_WRITE(current,sizeof(*current));
end_coredump:
	set_fs(fs);
	return has_dumped;
}

/*
 * create_aout32_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */

static u32 __user *create_aout32_tables(char __user *p, struct linux_binprm *bprm)
{
	u32 __user *argv;
	u32 __user *envp;
	u32 __user *sp;
	int argc = bprm->argc;
	int envc = bprm->envc;

	sp = (u32 __user *)((-(unsigned long)sizeof(char *))&(unsigned long)p);

	/* This imposes the proper stack alignment for a new process. */
	sp = (u32 __user *) (((unsigned long) sp) & ~7);
	if ((envc+argc+3)&1)
		--sp;

	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_user(argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		char c;
		put_user(((u32)(unsigned long)(p)),argv++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(0,argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		char c;
		put_user(((u32)(unsigned long)(p)),envp++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(0,envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}

/*
 * These are the functions used to load a.out style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int load_aout32_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct exec ex;
	unsigned long error;
	unsigned long fd_offset;
	unsigned long rlim;
	unsigned long orig_thr_flags;
	int retval;

	ex = *((struct exec *) bprm->buf);		/* exec-header */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    bprm->file->f_path.dentry->d_inode->i_size < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
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

	/* OK, This is the point of no return */
	set_personality(PER_SUNOS);

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));
	current->mm->free_area_cache = current->mm->mmap_base;
	current->mm->cached_hole_size = 0;

	current->mm->mmap = NULL;
	compute_creds(bprm);
 	current->flags &= ~PF_FORKNOEXEC;
	if (N_MAGIC(ex) == NMAGIC) {
		loff_t pos = fd_offset;
		/* Fuck me plenty... */
		down_write(&current->mm->mmap_sem);	
		error = do_brk(N_TXTADDR(ex), ex.a_text);
		up_write(&current->mm->mmap_sem);
		bprm->file->f_op->read(bprm->file, (char __user *)N_TXTADDR(ex),
			  ex.a_text, &pos);
		down_write(&current->mm->mmap_sem);
		error = do_brk(N_DATADDR(ex), ex.a_data);
		up_write(&current->mm->mmap_sem);
		bprm->file->f_op->read(bprm->file, (char __user *)N_DATADDR(ex),
			  ex.a_data, &pos);
		goto beyond_if;
	}

	if (N_MAGIC(ex) == OMAGIC) {
		loff_t pos = fd_offset;
		down_write(&current->mm->mmap_sem);
		do_brk(N_TXTADDR(ex) & PAGE_MASK,
			ex.a_text+ex.a_data + PAGE_SIZE - 1);
		up_write(&current->mm->mmap_sem);
		bprm->file->f_op->read(bprm->file, (char __user *)N_TXTADDR(ex),
			  ex.a_text+ex.a_data, &pos);
	} else {
		static unsigned long error_time;
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) && (jiffies-error_time) > 5*HZ)
		{
			printk(KERN_NOTICE "executable not page aligned\n");
			error_time = jiffies;
		}

		if (!bprm->file->f_op->mmap) {
			loff_t pos = fd_offset;
			down_write(&current->mm->mmap_sem);
			do_brk(0, ex.a_text+ex.a_data);
			up_write(&current->mm->mmap_sem);
			bprm->file->f_op->read(bprm->file,
				  (char __user *)N_TXTADDR(ex),
				  ex.a_text+ex.a_data, &pos);
			goto beyond_if;
		}

	        down_write(&current->mm->mmap_sem);
		error = do_mmap(bprm->file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);
	        up_write(&current->mm->mmap_sem);

		if (error != N_TXTADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

	        down_write(&current->mm->mmap_sem);
 		error = do_mmap(bprm->file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
	        up_write(&current->mm->mmap_sem);
		if (error != N_DATADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	set_binfmt(&aout32_format);

	set_brk(current->mm->start_brk, current->mm->brk);

	/* Make sure STACK_TOP returns the right thing.  */
	orig_thr_flags = current_thread_info()->flags;
	current_thread_info()->flags |= _TIF_32BIT;

	retval = setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0) { 
		current_thread_info()->flags = orig_thr_flags;

		/* Someone check-me: is this error path enough? */ 
		send_sig(SIGKILL, current, 0); 
		return retval;
	}

	current->mm->start_stack =
		(unsigned long) create_aout32_tables((char __user *)bprm->p, bprm);
	tsb_context_switch(current->mm);

	start_thread32(regs, ex.a_entry, current->mm->start_stack);
	if (current->ptrace & PT_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
}

/* N.B. Move to .h file and use code in fs/binfmt_aout.c? */
static int load_aout32_library(struct file *file)
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
	    inode->i_size < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		goto out;
	}

	if (N_MAGIC(ex) == ZMAGIC && N_TXTOFF(ex) &&
	    (N_TXTOFF(ex) < inode->i_sb->s_blocksize)) {
		printk("N_TXTOFF < BLOCK_SIZE. Please convert library\n");
		goto out;
	}

	if (N_FLAGS(ex))
		goto out;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	/* Now use mmap to map the library into memory. */
	down_write(&current->mm->mmap_sem);
	error = do_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
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

static int __init init_aout32_binfmt(void)
{
	return register_binfmt(&aout32_format);
}

static void __exit exit_aout32_binfmt(void)
{
	unregister_binfmt(&aout32_format);
}

module_init(init_aout32_binfmt);
module_exit(exit_aout32_binfmt);
