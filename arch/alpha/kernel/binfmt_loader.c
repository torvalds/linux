#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm_types.h>
#include <linux/binfmts.h>
#include <linux/a.out.h>

static int load_binary(struct linux_binprm *bprm, struct pt_regs *regs)
{
	struct exec *eh = (struct exec *)bprm->buf;
	unsigned long loader;
	struct file *file;
	int retval;

	if (eh->fh.f_magic != 0x183 || (eh->fh.f_flags & 0x3000) != 0x3000)
		return -ENOEXEC;

	if (bprm->loader)
		return -ENOEXEC;

	allow_write_access(bprm->file);
	fput(bprm->file);
	bprm->file = NULL;

	loader = bprm->vma->vm_end - sizeof(void *);

	file = open_exec("/sbin/loader");
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	/* Remember if the application is TASO.  */
	bprm->taso = eh->ah.entry < 0x100000000UL;

	bprm->file = file;
	bprm->loader = loader;
	retval = prepare_binprm(bprm);
	if (retval < 0)
		return retval;
	return search_binary_handler(bprm,regs);
}

static struct linux_binfmt loader_format = {
	.load_binary	= load_binary,
};

static int __init init_loader_binfmt(void)
{
	return register_binfmt(&loader_format);
}
arch_initcall(init_loader_binfmt);
