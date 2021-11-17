// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm_types.h>
#include <linux/binfmts.h>
#include <linux/a.out.h>

static int load_binary(struct linux_binprm *bprm)
{
	struct exec *eh = (struct exec *)bprm->buf;
	unsigned long loader;
	struct file *file;
	int retval;

	if (eh->fh.f_magic != 0x183 || (eh->fh.f_flags & 0x3000) != 0x3000)
		return -ENOEXEC;

	if (bprm->loader)
		return -ENOEXEC;

	loader = bprm->vma->vm_end - sizeof(void *);

	file = open_exec("/sbin/loader");
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	/* Remember if the application is TASO.  */
	bprm->taso = eh->ah.entry < 0x100000000UL;

	bprm->interpreter = file;
	bprm->loader = loader;
	return 0;
}

static struct linux_binfmt loader_format = {
	.load_binary	= load_binary,
};

static int __init init_loader_binfmt(void)
{
	insert_binfmt(&loader_format);
	return 0;
}
arch_initcall(init_loader_binfmt);
