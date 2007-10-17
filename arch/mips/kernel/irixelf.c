/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * irixelf.c: Code to load IRIX ELF executables conforming to the MIPS ABI.
 *            Based off of work by Eric Youngdale.
 *
 * Copyright (C) 1993 - 1994 Eric Youngdale <ericy@cais.com>
 * Copyright (C) 1996 - 2004 David S. Miller <dm@engr.sgi.com>
 * Copyright (C) 2004 - 2005 Steven J. Hill <sjhill@realitydiluted.com>
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/personality.h>
#include <linux/elfcore.h>

#include <asm/mipsregs.h>
#include <asm/namei.h>
#include <asm/prctl.h>
#include <asm/uaccess.h>

#define DLINFO_ITEMS 12

#include <linux/elf.h>

static int load_irix_binary(struct linux_binprm * bprm, struct pt_regs * regs);
static int load_irix_library(struct file *);
static int irix_core_dump(long signr, struct pt_regs * regs,
                          struct file *file, unsigned long limit);

static struct linux_binfmt irix_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_irix_binary,
	.load_shlib	= load_irix_library,
	.core_dump	= irix_core_dump,
	.min_coredump	= PAGE_SIZE,
};

/* Debugging routines. */
static char *get_elf_p_type(Elf32_Word p_type)
{
#ifdef DEBUG
	switch (p_type) {
	case PT_NULL:
		return "PT_NULL";
		break;

	case PT_LOAD:
		return "PT_LOAD";
		break;

	case PT_DYNAMIC:
		return "PT_DYNAMIC";
		break;

	case PT_INTERP:
		return "PT_INTERP";
		break;

	case PT_NOTE:
		return "PT_NOTE";
		break;

	case PT_SHLIB:
		return "PT_SHLIB";
		break;

	case PT_PHDR:
		return "PT_PHDR";
		break;

	case PT_LOPROC:
		return "PT_LOPROC/REGINFO";
		break;

	case PT_HIPROC:
		return "PT_HIPROC";
		break;

	default:
		return "PT_BOGUS";
		break;
	}
#endif
}

static void print_elfhdr(struct elfhdr *ehp)
{
	int i;

	pr_debug("ELFHDR: e_ident<");
	for (i = 0; i < (EI_NIDENT - 1); i++)
		pr_debug("%x ", ehp->e_ident[i]);
	pr_debug("%x>\n", ehp->e_ident[i]);
	pr_debug("        e_type[%04x] e_machine[%04x] e_version[%08lx]\n",
	         (unsigned short) ehp->e_type, (unsigned short) ehp->e_machine,
	         (unsigned long) ehp->e_version);
	pr_debug("        e_entry[%08lx] e_phoff[%08lx] e_shoff[%08lx] "
	         "e_flags[%08lx]\n",
	         (unsigned long) ehp->e_entry, (unsigned long) ehp->e_phoff,
	         (unsigned long) ehp->e_shoff, (unsigned long) ehp->e_flags);
	pr_debug("        e_ehsize[%04x] e_phentsize[%04x] e_phnum[%04x]\n",
	         (unsigned short) ehp->e_ehsize,
	         (unsigned short) ehp->e_phentsize,
	         (unsigned short) ehp->e_phnum);
	pr_debug("        e_shentsize[%04x] e_shnum[%04x] e_shstrndx[%04x]\n",
	         (unsigned short) ehp->e_shentsize,
	         (unsigned short) ehp->e_shnum,
	         (unsigned short) ehp->e_shstrndx);
}

static void print_phdr(int i, struct elf_phdr *ep)
{
	pr_debug("PHDR[%d]: p_type[%s] p_offset[%08lx] p_vaddr[%08lx] "
	         "p_paddr[%08lx]\n", i, get_elf_p_type(ep->p_type),
	         (unsigned long) ep->p_offset, (unsigned long) ep->p_vaddr,
	         (unsigned long) ep->p_paddr);
	pr_debug("         p_filesz[%08lx] p_memsz[%08lx] p_flags[%08lx] "
	         "p_align[%08lx]\n", (unsigned long) ep->p_filesz,
	         (unsigned long) ep->p_memsz, (unsigned long) ep->p_flags,
	         (unsigned long) ep->p_align);
}

static void dump_phdrs(struct elf_phdr *ep, int pnum)
{
	int i;

	for (i = 0; i < pnum; i++, ep++) {
		if ((ep->p_type == PT_LOAD) ||
		    (ep->p_type == PT_INTERP) ||
		    (ep->p_type == PT_PHDR))
			print_phdr(i, ep);
	}
}

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


/* We need to explicitly zero any fractional pages
 * after the data section (i.e. bss).  This would
 * contain the junk from the file that should not
 * be in memory.
 */
static void padzero(unsigned long elf_bss)
{
	unsigned long nbyte;

	nbyte = elf_bss & (PAGE_SIZE-1);
	if (nbyte) {
		nbyte = PAGE_SIZE - nbyte;
		clear_user((void __user *) elf_bss, nbyte);
	}
}

static unsigned long * create_irix_tables(char * p, int argc, int envc,
	struct elfhdr * exec, unsigned int load_addr,
	unsigned int interp_load_addr, struct pt_regs *regs,
	struct elf_phdr *ephdr)
{
	elf_addr_t *argv;
	elf_addr_t *envp;
	elf_addr_t *sp, *csp;

	pr_debug("create_irix_tables: p[%p] argc[%d] envc[%d] "
	         "load_addr[%08x] interp_load_addr[%08x]\n",
	         p, argc, envc, load_addr, interp_load_addr);

	sp = (elf_addr_t *) (~15UL & (unsigned long) p);
	csp = sp;
	csp -= exec ? DLINFO_ITEMS*2 : 2;
	csp -= envc+1;
	csp -= argc+1;
	csp -= 1;		/* argc itself */
	if ((unsigned long)csp & 15UL) {
		sp -= (16UL - ((unsigned long)csp & 15UL)) / sizeof(*sp);
	}

	/*
	 * Put the ELF interpreter info on the stack
	 */
#define NEW_AUX_ENT(nr, id, val) \
	  __put_user((id), sp+(nr*2)); \
	  __put_user((val), sp+(nr*2+1)); \

	sp -= 2;
	NEW_AUX_ENT(0, AT_NULL, 0);

	if (exec) {
		sp -= 11*2;

		NEW_AUX_ENT(0, AT_PHDR, load_addr + exec->e_phoff);
		NEW_AUX_ENT(1, AT_PHENT, sizeof(struct elf_phdr));
		NEW_AUX_ENT(2, AT_PHNUM, exec->e_phnum);
		NEW_AUX_ENT(3, AT_PAGESZ, ELF_EXEC_PAGESIZE);
		NEW_AUX_ENT(4, AT_BASE, interp_load_addr);
		NEW_AUX_ENT(5, AT_FLAGS, 0);
		NEW_AUX_ENT(6, AT_ENTRY, (elf_addr_t) exec->e_entry);
		NEW_AUX_ENT(7, AT_UID, (elf_addr_t) current->uid);
		NEW_AUX_ENT(8, AT_EUID, (elf_addr_t) current->euid);
		NEW_AUX_ENT(9, AT_GID, (elf_addr_t) current->gid);
		NEW_AUX_ENT(10, AT_EGID, (elf_addr_t) current->egid);
	}
#undef NEW_AUX_ENT

	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;

	__put_user((elf_addr_t)argc, --sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		__put_user((unsigned long)p, argv++);
		p += strlen_user(p);
	}
	__put_user((unsigned long) NULL, argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		__put_user((unsigned long)p, envp++);
		p += strlen_user(p);
	}
	__put_user((unsigned long) NULL, envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}


/* This is much more generalized than the library routine read function,
 * so we keep this separate.  Technically the library read function
 * is only provided so that we can read a.out libraries that have
 * an ELF header.
 */
static unsigned int load_irix_interp(struct elfhdr * interp_elf_ex,
				     struct file * interpreter,
				     unsigned int *interp_load_addr)
{
	struct elf_phdr *elf_phdata  =  NULL;
	struct elf_phdr *eppnt;
	unsigned int len;
	unsigned int load_addr;
	int elf_bss;
	int retval;
	unsigned int last_bss;
	int error;
	int i;
	unsigned int k;

	elf_bss = 0;
	last_bss = 0;
	error = load_addr = 0;

	print_elfhdr(interp_elf_ex);

	/* First of all, some simple consistency checks */
	if ((interp_elf_ex->e_type != ET_EXEC &&
	     interp_elf_ex->e_type != ET_DYN) ||
	     !interpreter->f_op->mmap) {
		printk("IRIX interp has bad e_type %d\n", interp_elf_ex->e_type);
		return 0xffffffff;
	}

	/* Now read in all of the header information */
	if (sizeof(struct elf_phdr) * interp_elf_ex->e_phnum > PAGE_SIZE) {
	    printk("IRIX interp header bigger than a page (%d)\n",
		   (sizeof(struct elf_phdr) * interp_elf_ex->e_phnum));
	    return 0xffffffff;
	}

	elf_phdata = kmalloc(sizeof(struct elf_phdr) * interp_elf_ex->e_phnum,
			     GFP_KERNEL);

	if (!elf_phdata) {
		printk("Cannot kmalloc phdata for IRIX interp.\n");
		return 0xffffffff;
	}

	/* If the size of this structure has changed, then punt, since
	 * we will be doing the wrong thing.
	 */
	if (interp_elf_ex->e_phentsize != 32) {
		printk("IRIX interp e_phentsize == %d != 32 ",
		       interp_elf_ex->e_phentsize);
		kfree(elf_phdata);
		return 0xffffffff;
	}

	retval = kernel_read(interpreter, interp_elf_ex->e_phoff,
			   (char *) elf_phdata,
			   sizeof(struct elf_phdr) * interp_elf_ex->e_phnum);

	dump_phdrs(elf_phdata, interp_elf_ex->e_phnum);

	eppnt = elf_phdata;
	for (i = 0; i < interp_elf_ex->e_phnum; i++, eppnt++) {
		if (eppnt->p_type == PT_LOAD) {
			int elf_type = MAP_PRIVATE | MAP_DENYWRITE;
			int elf_prot = 0;
			unsigned long vaddr = 0;
			if (eppnt->p_flags & PF_R)
				elf_prot =  PROT_READ;
			if (eppnt->p_flags & PF_W)
				elf_prot |= PROT_WRITE;
			if (eppnt->p_flags & PF_X)
				elf_prot |= PROT_EXEC;
			elf_type |= MAP_FIXED;
			vaddr = eppnt->p_vaddr;

			pr_debug("INTERP do_mmap"
			         "(%p, %08lx, %08lx, %08lx, %08lx, %08lx) ",
			         interpreter, vaddr,
			         (unsigned long)
			         (eppnt->p_filesz + (eppnt->p_vaddr & 0xfff)),
			         (unsigned long)
			         elf_prot, (unsigned long) elf_type,
			         (unsigned long)
			         (eppnt->p_offset & 0xfffff000));

			down_write(&current->mm->mmap_sem);
			error = do_mmap(interpreter, vaddr,
			eppnt->p_filesz + (eppnt->p_vaddr & 0xfff),
			elf_prot, elf_type,
			eppnt->p_offset & 0xfffff000);
			up_write(&current->mm->mmap_sem);

			if (error < 0 && error > -1024) {
				printk("Aieee IRIX interp mmap error=%d\n",
				       error);
				break;  /* Real error */
			}
			pr_debug("error=%08lx ", (unsigned long) error);
			if (!load_addr && interp_elf_ex->e_type == ET_DYN) {
				load_addr = error;
				pr_debug("load_addr = error ");
			}

			/*
			 * Find the end of the file  mapping for this phdr, and
			 * keep track of the largest address we see for this.
			 */
			k = eppnt->p_vaddr + eppnt->p_filesz;
			if (k > elf_bss)
				elf_bss = k;

			/* Do the same thing for the memory mapping - between
			 * elf_bss and last_bss is the bss section.
			 */
			k = eppnt->p_memsz + eppnt->p_vaddr;
			if (k > last_bss)
				last_bss = k;
			pr_debug("\n");
		}
	}

	/* Now use mmap to map the library into memory. */
	if (error < 0 && error > -1024) {
		pr_debug("got error %d\n", error);
		kfree(elf_phdata);
		return 0xffffffff;
	}

	/* Now fill out the bss section.  First pad the last page up
	 * to the page boundary, and then perform a mmap to make sure
	 * that there are zero-mapped pages up to and including the
	 * last bss page.
	 */
	pr_debug("padzero(%08lx) ", (unsigned long) (elf_bss));
	padzero(elf_bss);
	len = (elf_bss + 0xfff) & 0xfffff000; /* What we have mapped so far */

	pr_debug("last_bss[%08lx] len[%08lx]\n", (unsigned long) last_bss,
	         (unsigned long) len);

	/* Map the last of the bss segment */
	if (last_bss > len) {
		down_write(&current->mm->mmap_sem);
		do_brk(len, (last_bss - len));
		up_write(&current->mm->mmap_sem);
	}
	kfree(elf_phdata);

	*interp_load_addr = load_addr;
	return ((unsigned int) interp_elf_ex->e_entry);
}

/* Check sanity of IRIX elf executable header. */
static int verify_binary(struct elfhdr *ehp, struct linux_binprm *bprm)
{
	if (memcmp(ehp->e_ident, ELFMAG, SELFMAG) != 0)
		return -ENOEXEC;

	/* First of all, some simple consistency checks */
	if ((ehp->e_type != ET_EXEC && ehp->e_type != ET_DYN) ||
	    !bprm->file->f_op->mmap) {
		return -ENOEXEC;
	}

	/* XXX Don't support N32 or 64bit binaries yet because they can
	 * XXX and do execute 64 bit instructions and expect all registers
	 * XXX to be 64 bit as well.  We need to make the kernel save
	 * XXX all registers as 64bits on cpu's capable of this at
	 * XXX exception time plus frob the XTLB exception vector.
	 */
	if ((ehp->e_flags & EF_MIPS_ABI2))
		return -ENOEXEC;

	return 0;
}

/*
 * This is where the detailed check is performed. Irix binaries
 * use interpreters with 'libc.so' in the name, so this function
 * can differentiate between Linux and Irix binaries.
 */
static inline int look_for_irix_interpreter(char **name,
					    struct file **interpreter,
					    struct elfhdr *interp_elf_ex,
					    struct elf_phdr *epp,
					    struct linux_binprm *bprm, int pnum)
{
	int i;
	int retval = -EINVAL;
	struct file *file = NULL;

	*name = NULL;
	for (i = 0; i < pnum; i++, epp++) {
		if (epp->p_type != PT_INTERP)
			continue;

		/* It is illegal to have two interpreters for one executable. */
		if (*name != NULL)
			goto out;

		*name = kmalloc(epp->p_filesz + strlen(IRIX_EMUL), GFP_KERNEL);
		if (!*name)
			return -ENOMEM;

		strcpy(*name, IRIX_EMUL);
		retval = kernel_read(bprm->file, epp->p_offset, (*name + 16),
		                     epp->p_filesz);
		if (retval < 0)
			goto out;

		file = open_exec(*name);
		if (IS_ERR(file)) {
			retval = PTR_ERR(file);
			goto out;
		}
		retval = kernel_read(file, 0, bprm->buf, 128);
		if (retval < 0)
			goto dput_and_out;

		*interp_elf_ex = *(struct elfhdr *) bprm->buf;
	}
	*interpreter = file;
	return 0;

dput_and_out:
	fput(file);
out:
	kfree(*name);
	return retval;
}

static inline int verify_irix_interpreter(struct elfhdr *ihp)
{
	if (memcmp(ihp->e_ident, ELFMAG, SELFMAG) != 0)
		return -ELIBBAD;
	return 0;
}

#define EXEC_MAP_FLAGS (MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE)

static inline void map_executable(struct file *fp, struct elf_phdr *epp, int pnum,
				  unsigned int *estack, unsigned int *laddr,
				  unsigned int *scode, unsigned int *ebss,
				  unsigned int *ecode, unsigned int *edata,
				  unsigned int *ebrk)
{
	unsigned int tmp;
	int i, prot;

	for (i = 0; i < pnum; i++, epp++) {
		if (epp->p_type != PT_LOAD)
			continue;

		/* Map it. */
		prot  = (epp->p_flags & PF_R) ? PROT_READ : 0;
		prot |= (epp->p_flags & PF_W) ? PROT_WRITE : 0;
		prot |= (epp->p_flags & PF_X) ? PROT_EXEC : 0;
	        down_write(&current->mm->mmap_sem);
		(void) do_mmap(fp, (epp->p_vaddr & 0xfffff000),
			       (epp->p_filesz + (epp->p_vaddr & 0xfff)),
			       prot, EXEC_MAP_FLAGS,
			       (epp->p_offset & 0xfffff000));
	        up_write(&current->mm->mmap_sem);

		/* Fixup location tracking vars. */
		if ((epp->p_vaddr & 0xfffff000) < *estack)
			*estack = (epp->p_vaddr & 0xfffff000);
		if (!*laddr)
			*laddr = epp->p_vaddr - epp->p_offset;
		if (epp->p_vaddr < *scode)
			*scode = epp->p_vaddr;

		tmp = epp->p_vaddr + epp->p_filesz;
		if (tmp > *ebss)
			*ebss = tmp;
		if ((epp->p_flags & PF_X) && *ecode < tmp)
			*ecode = tmp;
		if (*edata < tmp)
			*edata = tmp;

		tmp = epp->p_vaddr + epp->p_memsz;
		if (tmp > *ebrk)
			*ebrk = tmp;
	}

}

static inline int map_interpreter(struct elf_phdr *epp, struct elfhdr *ihp,
				  struct file *interp, unsigned int *iladdr,
				  int pnum, mm_segment_t old_fs,
				  unsigned int *eentry)
{
	int i;

	*eentry = 0xffffffff;
	for (i = 0; i < pnum; i++, epp++) {
		if (epp->p_type != PT_INTERP)
			continue;

		/* We should have fielded this error elsewhere... */
		if (*eentry != 0xffffffff)
			return -1;

		set_fs(old_fs);
		*eentry = load_irix_interp(ihp, interp, iladdr);
		old_fs = get_fs();
		set_fs(get_ds());

		fput(interp);

		if (*eentry == 0xffffffff)
			return -1;
	}
	return 0;
}

/*
 * IRIX maps a page at 0x200000 that holds information about the
 * process and the system, here we map the page and fill the
 * structure
 */
static void irix_map_prda_page(void)
{
	unsigned long v;
	struct prda *pp;

	down_write(&current->mm->mmap_sem);
	v =  do_brk(PRDA_ADDRESS, PAGE_SIZE);
	up_write(&current->mm->mmap_sem);

	if (v < 0)
		return;

	pp = (struct prda *) v;
	pp->prda_sys.t_pid  = current->pid;
	pp->prda_sys.t_prid = read_c0_prid();
	pp->prda_sys.t_rpid = current->pid;

	/* We leave the rest set to zero */
}



/* These are the functions used to load ELF style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */
static int load_irix_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct elfhdr elf_ex, interp_elf_ex;
	struct file *interpreter;
	struct elf_phdr *elf_phdata, *elf_ihdr, *elf_ephdr;
	unsigned int load_addr, elf_bss, elf_brk;
	unsigned int elf_entry, interp_load_addr = 0;
	unsigned int start_code, end_code, end_data, elf_stack;
	int retval, has_interp, has_ephdr, size, i;
	char *elf_interpreter;
	mm_segment_t old_fs;

	load_addr = 0;
	has_interp = has_ephdr = 0;
	elf_ihdr = elf_ephdr = NULL;
	elf_ex = *((struct elfhdr *) bprm->buf);
	retval = -ENOEXEC;

	if (verify_binary(&elf_ex, bprm))
		goto out;

	/*
	 * Telling -o32 static binaries from Linux and Irix apart from each
	 * other is difficult. There are 2 differences to be noted for static
	 * binaries from the 2 operating systems:
	 *
	 *    1) Irix binaries have their .text section before their .init
	 *       section. Linux binaries are just the opposite.
	 *
	 *    2) Irix binaries usually have <= 12 sections and Linux
	 *       binaries have > 20.
	 *
	 * We will use Method #2 since Method #1 would require us to read in
	 * the section headers which is way too much overhead. This appears
	 * to work for everything we have ran into so far. If anyone has a
	 * better method to tell the binaries apart, I'm listening.
	 */
	if (elf_ex.e_shnum > 20)
		goto out;

	print_elfhdr(&elf_ex);

	/* Now read in all of the header information */
	size = elf_ex.e_phentsize * elf_ex.e_phnum;
	if (size > 65536)
		goto out;
	elf_phdata = kmalloc(size, GFP_KERNEL);
	if (elf_phdata == NULL) {
		retval = -ENOMEM;
		goto out;
	}

	retval = kernel_read(bprm->file, elf_ex.e_phoff, (char *)elf_phdata, size);
	if (retval < 0)
		goto out_free_ph;

	dump_phdrs(elf_phdata, elf_ex.e_phnum);

	/* Set some things for later. */
	for (i = 0; i < elf_ex.e_phnum; i++) {
		switch (elf_phdata[i].p_type) {
		case PT_INTERP:
			has_interp = 1;
			elf_ihdr = &elf_phdata[i];
			break;
		case PT_PHDR:
			has_ephdr = 1;
			elf_ephdr = &elf_phdata[i];
			break;
		};
	}

	pr_debug("\n");

	elf_bss = 0;
	elf_brk = 0;

	elf_stack = 0xffffffff;
	elf_interpreter = NULL;
	start_code = 0xffffffff;
	end_code = 0;
	end_data = 0;

	/*
	 * If we get a return value, we change the value to be ENOEXEC
	 * so that we can exit gracefully and the main binary format
	 * search loop in 'fs/exec.c' will move onto the next handler
	 * which should be the normal ELF binary handler.
	 */
	retval = look_for_irix_interpreter(&elf_interpreter, &interpreter,
					   &interp_elf_ex, elf_phdata, bprm,
					   elf_ex.e_phnum);
	if (retval) {
		retval = -ENOEXEC;
		goto out_free_file;
	}

	if (elf_interpreter) {
		retval = verify_irix_interpreter(&interp_elf_ex);
		if (retval)
			goto out_free_interp;
	}

	/* OK, we are done with that, now set up the arg stuff,
	 * and then start this sucker up.
	 */
	retval = -E2BIG;
	if (!bprm->sh_bang && !bprm->p)
		goto out_free_interp;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		goto out_free_dentry;

	/* OK, This is the point of no return */
	current->mm->end_data = 0;
	current->mm->end_code = 0;
	current->mm->mmap = NULL;
	current->flags &= ~PF_FORKNOEXEC;
	elf_entry = (unsigned int) elf_ex.e_entry;

	/* Do this so that we can load the interpreter, if need be.  We will
	 * change some of these later.
	 */
	setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	current->mm->start_stack = bprm->p;

	/* At this point, we assume that the image should be loaded at
	 * fixed address, not at a variable address.
	 */
	old_fs = get_fs();
	set_fs(get_ds());

	map_executable(bprm->file, elf_phdata, elf_ex.e_phnum, &elf_stack,
	               &load_addr, &start_code, &elf_bss, &end_code,
	               &end_data, &elf_brk);

	if (elf_interpreter) {
		retval = map_interpreter(elf_phdata, &interp_elf_ex,
					 interpreter, &interp_load_addr,
					 elf_ex.e_phnum, old_fs, &elf_entry);
		kfree(elf_interpreter);
		if (retval) {
			set_fs(old_fs);
			printk("Unable to load IRIX ELF interpreter\n");
			send_sig(SIGSEGV, current, 0);
			retval = 0;
			goto out_free_file;
		}
	}

	set_fs(old_fs);

	kfree(elf_phdata);
	set_personality(PER_IRIX32);
	set_binfmt(&irix_format);
	compute_creds(bprm);
	current->flags &= ~PF_FORKNOEXEC;
	bprm->p = (unsigned long)
	  create_irix_tables((char *)bprm->p, bprm->argc, bprm->envc,
			(elf_interpreter ? &elf_ex : NULL),
			load_addr, interp_load_addr, regs, elf_ephdr);
	current->mm->start_brk = current->mm->brk = elf_brk;
	current->mm->end_code = end_code;
	current->mm->start_code = start_code;
	current->mm->end_data = end_data;
	current->mm->start_stack = bprm->p;

	/* Calling set_brk effectively mmaps the pages that we need for the
	 * bss and break sections.
	 */
	set_brk(elf_bss, elf_brk);

	/*
	 * IRIX maps a page at 0x200000 which holds some system
	 * information.  Programs depend on this.
	 */
	irix_map_prda_page();

	padzero(elf_bss);

	pr_debug("(start_brk) %lx\n" , (long) current->mm->start_brk);
	pr_debug("(end_code) %lx\n" , (long) current->mm->end_code);
	pr_debug("(start_code) %lx\n" , (long) current->mm->start_code);
	pr_debug("(end_data) %lx\n" , (long) current->mm->end_data);
	pr_debug("(start_stack) %lx\n" , (long) current->mm->start_stack);
	pr_debug("(brk) %lx\n" , (long) current->mm->brk);

#if 0 /* XXX No fucking way dude... */
	/* Why this, you ask???  Well SVr4 maps page 0 as read-only,
	 * and some applications "depend" upon this behavior.
	 * Since we do not have the power to recompile these, we
	 * emulate the SVr4 behavior.  Sigh.
	 */
	down_write(&current->mm->mmap_sem);
	(void) do_mmap(NULL, 0, 4096, PROT_READ | PROT_EXEC,
		       MAP_FIXED | MAP_PRIVATE, 0);
	up_write(&current->mm->mmap_sem);
#endif

	start_thread(regs, elf_entry, bprm->p);
	if (current->ptrace & PT_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
out:
	return retval;

out_free_dentry:
	allow_write_access(interpreter);
	fput(interpreter);
out_free_interp:
	kfree(elf_interpreter);
out_free_file:
out_free_ph:
	kfree(elf_phdata);
	goto out;
}

/* This is really simpleminded and specialized - we are loading an
 * a.out library that is given an ELF header.
 */
static int load_irix_library(struct file *file)
{
	struct elfhdr elf_ex;
	struct elf_phdr *elf_phdata  =  NULL;
	unsigned int len = 0;
	int elf_bss = 0;
	int retval;
	unsigned int bss;
	int error;
	int i, j, k;

	error = kernel_read(file, 0, (char *) &elf_ex, sizeof(elf_ex));
	if (error != sizeof(elf_ex))
		return -ENOEXEC;

	if (memcmp(elf_ex.e_ident, ELFMAG, SELFMAG) != 0)
		return -ENOEXEC;

	/* First of all, some simple consistency checks. */
	if (elf_ex.e_type != ET_EXEC || elf_ex.e_phnum > 2 ||
	   !file->f_op->mmap)
		return -ENOEXEC;

	/* Now read in all of the header information. */
	if (sizeof(struct elf_phdr) * elf_ex.e_phnum > PAGE_SIZE)
		return -ENOEXEC;

	elf_phdata = kmalloc(sizeof(struct elf_phdr) * elf_ex.e_phnum, GFP_KERNEL);
	if (elf_phdata == NULL)
		return -ENOMEM;

	retval = kernel_read(file, elf_ex.e_phoff, (char *) elf_phdata,
			   sizeof(struct elf_phdr) * elf_ex.e_phnum);

	j = 0;
	for (i=0; i<elf_ex.e_phnum; i++)
		if ((elf_phdata + i)->p_type == PT_LOAD) j++;

	if (j != 1)  {
		kfree(elf_phdata);
		return -ENOEXEC;
	}

	while (elf_phdata->p_type != PT_LOAD) elf_phdata++;

	/* Now use mmap to map the library into memory. */
	down_write(&current->mm->mmap_sem);
	error = do_mmap(file,
			elf_phdata->p_vaddr & 0xfffff000,
			elf_phdata->p_filesz + (elf_phdata->p_vaddr & 0xfff),
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			elf_phdata->p_offset & 0xfffff000);
	up_write(&current->mm->mmap_sem);

	k = elf_phdata->p_vaddr + elf_phdata->p_filesz;
	if (k > elf_bss) elf_bss = k;

	if (error != (elf_phdata->p_vaddr & 0xfffff000)) {
		kfree(elf_phdata);
		return error;
	}

	padzero(elf_bss);

	len = (elf_phdata->p_filesz + elf_phdata->p_vaddr+ 0xfff) & 0xfffff000;
	bss = elf_phdata->p_memsz + elf_phdata->p_vaddr;
	if (bss > len) {
	  down_write(&current->mm->mmap_sem);
	  do_brk(len, bss-len);
	  up_write(&current->mm->mmap_sem);
	}
	kfree(elf_phdata);
	return 0;
}

/* Called through irix_syssgi() to map an elf image given an FD,
 * a phdr ptr USER_PHDRP in userspace, and a count CNT telling how many
 * phdrs there are in the USER_PHDRP array.  We return the vaddr the
 * first phdr was successfully mapped to.
 */
unsigned long irix_mapelf(int fd, struct elf_phdr __user *user_phdrp, int cnt)
{
	unsigned long type, vaddr, filesz, offset, flags;
	struct elf_phdr __user *hp;
	struct file *filp;
	int i, retval;

	pr_debug("irix_mapelf: fd[%d] user_phdrp[%p] cnt[%d]\n",
	         fd, user_phdrp, cnt);

	/* First get the verification out of the way. */
	hp = user_phdrp;
	if (!access_ok(VERIFY_READ, hp, (sizeof(struct elf_phdr) * cnt))) {
		pr_debug("irix_mapelf: bad pointer to ELF PHDR!\n");

		return -EFAULT;
	}

	dump_phdrs(user_phdrp, cnt);

	for (i = 0; i < cnt; i++, hp++) {
		if (__get_user(type, &hp->p_type))
			return -EFAULT;
		if (type != PT_LOAD) {
			printk("irix_mapelf: One section is not PT_LOAD!\n");
			return -ENOEXEC;
		}
	}

	filp = fget(fd);
	if (!filp)
		return -EACCES;
	if (!filp->f_op) {
		printk("irix_mapelf: Bogon filp!\n");
		fput(filp);
		return -EACCES;
	}

	hp = user_phdrp;
	for (i = 0; i < cnt; i++, hp++) {
		int prot;

		retval = __get_user(vaddr, &hp->p_vaddr);
		retval |= __get_user(filesz, &hp->p_filesz);
		retval |= __get_user(offset, &hp->p_offset);
		retval |= __get_user(flags, &hp->p_flags);
		if (retval)
			return retval;

		prot  = (flags & PF_R) ? PROT_READ : 0;
		prot |= (flags & PF_W) ? PROT_WRITE : 0;
		prot |= (flags & PF_X) ? PROT_EXEC : 0;

		down_write(&current->mm->mmap_sem);
		retval = do_mmap(filp, (vaddr & 0xfffff000),
				 (filesz + (vaddr & 0xfff)),
				 prot, (MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE),
				 (offset & 0xfffff000));
		up_write(&current->mm->mmap_sem);

		if (retval != (vaddr & 0xfffff000)) {
			printk("irix_mapelf: do_mmap fails with %d!\n", retval);
			fput(filp);
			return retval;
		}
	}

	pr_debug("irix_mapelf: Success, returning %08lx\n",
		 (unsigned long) user_phdrp->p_vaddr);

	fput(filp);

	if (__get_user(vaddr, &user_phdrp->p_vaddr))
		return -EFAULT;

	return vaddr;
}

/*
 * ELF core dumper
 *
 * Modelled on fs/exec.c:aout_core_dump()
 * Jeremy Fitzhardinge <jeremy@sw.oz.au>
 */

/* These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
static int dump_write(struct file *file, const void __user *addr, int nr)
{
	return file->f_op->write(file, (const char __user *) addr, nr, &file->f_pos) == nr;
}

static int dump_seek(struct file *file, off_t off)
{
	if (file->f_op->llseek) {
		if (file->f_op->llseek(file, off, 0) != off)
			return 0;
	} else
		file->f_pos = off;
	return 1;
}

/* Decide whether a segment is worth dumping; default is yes to be
 * sure (missing info is worse than too much; etc).
 * Personally I'd include everything, and use the coredump limit...
 *
 * I think we should skip something. But I am not sure how. H.J.
 */
static inline int maydump(struct vm_area_struct *vma)
{
	if (!(vma->vm_flags & (VM_READ|VM_WRITE|VM_EXEC)))
		return 0;
#if 1
	if (vma->vm_flags & (VM_WRITE|VM_GROWSUP|VM_GROWSDOWN))
		return 1;
	if (vma->vm_flags & (VM_READ|VM_EXEC|VM_EXECUTABLE|VM_SHARED))
		return 0;
#endif
	return 1;
}

/* An ELF note in memory. */
struct memelfnote
{
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += roundup(strlen(en->name) + 1, 4);
	sz += roundup(en->datasz, 4);

	return sz;
}

#define DUMP_WRITE(addr, nr)	\
	if (!dump_write(file, (addr), (nr))) \
		goto end_coredump;
#define DUMP_SEEK(off)	\
	if (!dump_seek(file, (off))) \
		goto end_coredump;

static int writenote(struct memelfnote *men, struct file *file)
{
	struct elf_note en;

	en.n_namesz = strlen(men->name) + 1;
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	DUMP_WRITE(&en, sizeof(en));
	DUMP_WRITE(men->name, en.n_namesz);
	/* XXX - cast from long long to long to avoid need for libgcc.a */
	DUMP_SEEK(roundup((unsigned long)file->f_pos, 4));	/* XXX */
	DUMP_WRITE(men->data, men->datasz);
	DUMP_SEEK(roundup((unsigned long)file->f_pos, 4));	/* XXX */

	return 1;

end_coredump:
	return 0;
}
#undef DUMP_WRITE
#undef DUMP_SEEK

#define DUMP_WRITE(addr, nr)	\
	if (!dump_write(file, (addr), (nr))) \
		goto end_coredump;
#define DUMP_SEEK(off)	\
	if (!dump_seek(file, (off))) \
		goto end_coredump;

/* Actual dumper.
 *
 * This is a two-pass process; first we find the offsets of the bits,
 * and then they are actually written out.  If we run out of core limit
 * we just truncate.
 */
static int irix_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit)
{
	int has_dumped = 0;
	mm_segment_t fs;
	int segs;
	int i;
	size_t size;
	struct vm_area_struct *vma;
	struct elfhdr elf;
	off_t offset = 0, dataoff;
	int numnote = 3;
	struct memelfnote notes[3];
	struct elf_prstatus prstatus;	/* NT_PRSTATUS */
	elf_fpregset_t fpu;		/* NT_PRFPREG */
	struct elf_prpsinfo psinfo;	/* NT_PRPSINFO */

	/* Count what's needed to dump, up to the limit of coredump size. */
	segs = 0;
	size = 0;
	for (vma = current->mm->mmap; vma != NULL; vma = vma->vm_next) {
		if (maydump(vma))
		{
			int sz = vma->vm_end-vma->vm_start;

			if (size+sz >= limit)
				break;
			else
				size += sz;
		}

		segs++;
	}
	pr_debug("irix_core_dump: %d segs taking %d bytes\n", segs, size);

	/* Set up header. */
	memcpy(elf.e_ident, ELFMAG, SELFMAG);
	elf.e_ident[EI_CLASS] = ELFCLASS32;
	elf.e_ident[EI_DATA] = ELFDATA2LSB;
	elf.e_ident[EI_VERSION] = EV_CURRENT;
	elf.e_ident[EI_OSABI] = ELF_OSABI;
	memset(elf.e_ident+EI_PAD, 0, EI_NIDENT-EI_PAD);

	elf.e_type = ET_CORE;
	elf.e_machine = ELF_ARCH;
	elf.e_version = EV_CURRENT;
	elf.e_entry = 0;
	elf.e_phoff = sizeof(elf);
	elf.e_shoff = 0;
	elf.e_flags = 0;
	elf.e_ehsize = sizeof(elf);
	elf.e_phentsize = sizeof(struct elf_phdr);
	elf.e_phnum = segs+1;		/* Include notes. */
	elf.e_shentsize = 0;
	elf.e_shnum = 0;
	elf.e_shstrndx = 0;

	fs = get_fs();
	set_fs(KERNEL_DS);

	has_dumped = 1;
	current->flags |= PF_DUMPCORE;

	DUMP_WRITE(&elf, sizeof(elf));
	offset += sizeof(elf);				/* Elf header. */
	offset += (segs+1) * sizeof(struct elf_phdr);	/* Program headers. */

	/* Set up the notes in similar form to SVR4 core dumps made
	 * with info from their /proc.
	 */
	memset(&psinfo, 0, sizeof(psinfo));
	memset(&prstatus, 0, sizeof(prstatus));

	notes[0].name = "CORE";
	notes[0].type = NT_PRSTATUS;
	notes[0].datasz = sizeof(prstatus);
	notes[0].data = &prstatus;
	prstatus.pr_info.si_signo = prstatus.pr_cursig = signr;
	prstatus.pr_sigpend = current->pending.signal.sig[0];
	prstatus.pr_sighold = current->blocked.sig[0];
	psinfo.pr_pid = prstatus.pr_pid = current->pid;
	psinfo.pr_ppid = prstatus.pr_ppid = current->parent->pid;
	psinfo.pr_pgrp = prstatus.pr_pgrp = process_group(current);
	psinfo.pr_sid = prstatus.pr_sid = process_session(current);
	if (current->pid == current->tgid) {
		/*
		 * This is the record for the group leader.  Add in the
		 * cumulative times of previous dead threads.  This total
		 * won't include the time of each live thread whose state
		 * is included in the core dump.  The final total reported
		 * to our parent process when it calls wait4 will include
		 * those sums as well as the little bit more time it takes
		 * this and each other thread to finish dying after the
		 * core dump synchronization phase.
		 */
		jiffies_to_timeval(current->utime + current->signal->utime,
		                   &prstatus.pr_utime);
		jiffies_to_timeval(current->stime + current->signal->stime,
		                   &prstatus.pr_stime);
	} else {
		jiffies_to_timeval(current->utime, &prstatus.pr_utime);
		jiffies_to_timeval(current->stime, &prstatus.pr_stime);
	}
	jiffies_to_timeval(current->signal->cutime, &prstatus.pr_cutime);
	jiffies_to_timeval(current->signal->cstime, &prstatus.pr_cstime);

	if (sizeof(elf_gregset_t) != sizeof(struct pt_regs)) {
		printk("sizeof(elf_gregset_t) (%d) != sizeof(struct pt_regs) "
		       "(%d)\n", sizeof(elf_gregset_t), sizeof(struct pt_regs));
	} else {
		*(struct pt_regs *)&prstatus.pr_reg = *regs;
	}

	notes[1].name = "CORE";
	notes[1].type = NT_PRPSINFO;
	notes[1].datasz = sizeof(psinfo);
	notes[1].data = &psinfo;
	i = current->state ? ffz(~current->state) + 1 : 0;
	psinfo.pr_state = i;
	psinfo.pr_sname = (i < 0 || i > 5) ? '.' : "RSDZTD"[i];
	psinfo.pr_zomb = psinfo.pr_sname == 'Z';
	psinfo.pr_nice = task_nice(current);
	psinfo.pr_flag = current->flags;
	psinfo.pr_uid = current->uid;
	psinfo.pr_gid = current->gid;
	{
		int i, len;

		set_fs(fs);

		len = current->mm->arg_end - current->mm->arg_start;
		len = len >= ELF_PRARGSZ ? ELF_PRARGSZ : len;
		(void *) copy_from_user(&psinfo.pr_psargs,
			       (const char __user *)current->mm->arg_start, len);
		for (i = 0; i < len; i++)
			if (psinfo.pr_psargs[i] == 0)
				psinfo.pr_psargs[i] = ' ';
		psinfo.pr_psargs[len] = 0;

		set_fs(KERNEL_DS);
	}
	strlcpy(psinfo.pr_fname, current->comm, sizeof(psinfo.pr_fname));

	/* Try to dump the FPU. */
	prstatus.pr_fpvalid = dump_fpu(regs, &fpu);
	if (!prstatus.pr_fpvalid) {
		numnote--;
	} else {
		notes[2].name = "CORE";
		notes[2].type = NT_PRFPREG;
		notes[2].datasz = sizeof(fpu);
		notes[2].data = &fpu;
	}

	/* Write notes phdr entry. */
	{
		struct elf_phdr phdr;
		int sz = 0;

		for (i = 0; i < numnote; i++)
			sz += notesize(&notes[i]);

		phdr.p_type = PT_NOTE;
		phdr.p_offset = offset;
		phdr.p_vaddr = 0;
		phdr.p_paddr = 0;
		phdr.p_filesz = sz;
		phdr.p_memsz = 0;
		phdr.p_flags = 0;
		phdr.p_align = 0;

		offset += phdr.p_filesz;
		DUMP_WRITE(&phdr, sizeof(phdr));
	}

	/* Page-align dumped data. */
	dataoff = offset = roundup(offset, PAGE_SIZE);

	/* Write program headers for segments dump. */
	for (vma = current->mm->mmap, i = 0;
		i < segs && vma != NULL; vma = vma->vm_next) {
		struct elf_phdr phdr;
		size_t sz;

		i++;

		sz = vma->vm_end - vma->vm_start;

		phdr.p_type = PT_LOAD;
		phdr.p_offset = offset;
		phdr.p_vaddr = vma->vm_start;
		phdr.p_paddr = 0;
		phdr.p_filesz = maydump(vma) ? sz : 0;
		phdr.p_memsz = sz;
		offset += phdr.p_filesz;
		phdr.p_flags = vma->vm_flags & VM_READ ? PF_R : 0;
		if (vma->vm_flags & VM_WRITE)
			phdr.p_flags |= PF_W;
		if (vma->vm_flags & VM_EXEC)
			phdr.p_flags |= PF_X;
		phdr.p_align = PAGE_SIZE;

		DUMP_WRITE(&phdr, sizeof(phdr));
	}

	for (i = 0; i < numnote; i++)
		if (!writenote(&notes[i], file))
			goto end_coredump;

	set_fs(fs);

	DUMP_SEEK(dataoff);

	for (i = 0, vma = current->mm->mmap;
	    i < segs && vma != NULL;
	    vma = vma->vm_next) {
		unsigned long addr = vma->vm_start;
		unsigned long len = vma->vm_end - vma->vm_start;

		if (!maydump(vma))
			continue;
		i++;
		pr_debug("elf_core_dump: writing %08lx %lx\n", addr, len);
		DUMP_WRITE((void __user *)addr, len);
	}

	if ((off_t) file->f_pos != offset) {
		/* Sanity check. */
		printk("elf_core_dump: file->f_pos (%ld) != offset (%ld)\n",
		       (off_t) file->f_pos, offset);
	}

end_coredump:
	set_fs(fs);
	return has_dumped;
}

static int __init init_irix_binfmt(void)
{
	extern int init_inventory(void);
	extern asmlinkage unsigned long sys_call_table;
	extern asmlinkage unsigned long sys_call_table_irix5;

	init_inventory();

	/*
	 * Copy the IRIX5 syscall table (8000 bytes) into the main syscall
	 * table. The IRIX5 calls are located by an offset of 8000 bytes
	 * from the beginning of the main table.
	 */
	memcpy((void *) ((unsigned long) &sys_call_table + 8000),
		&sys_call_table_irix5, 8000);

	return register_binfmt(&irix_format);
}

static void __exit exit_irix_binfmt(void)
{
	/*
	 * Remove the Irix ELF loader.
	 */
	unregister_binfmt(&irix_format);
}

module_init(init_irix_binfmt)
module_exit(exit_irix_binfmt)
