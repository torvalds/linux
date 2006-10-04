#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>

#include <asm/uaccess.h>

#include "spufs.h"

/**
 * sys_spu_run - run code loaded into an SPU
 *
 * @unpc:    next program counter for the SPU
 * @ustatus: status of the SPU
 *
 * This system call transfers the control of execution of a
 * user space thread to an SPU. It will return when the
 * SPU has finished executing or when it hits an error
 * condition and it will be interrupted if a signal needs
 * to be delivered to a handler in user space.
 *
 * The next program counter is set to the passed value
 * before the SPU starts fetching code and the user space
 * pointer gets updated with the new value when returning
 * from kernel space.
 *
 * The status value returned from spu_run reflects the
 * value of the spu_status register after the SPU has stopped.
 *
 */
static long do_spu_run(struct file *filp,
			__u32 __user *unpc,
			__u32 __user *ustatus)
{
	long ret;
	struct spufs_inode_info *i;
	u32 npc, status;

	ret = -EFAULT;
	if (get_user(npc, unpc))
		goto out;

	/* check if this file was created by spu_create */
	ret = -EINVAL;
	if (filp->f_op != &spufs_context_fops)
		goto out;

	i = SPUFS_I(filp->f_dentry->d_inode);
	ret = spufs_run_spu(filp, i->i_ctx, &npc, &status);

	if (put_user(npc, unpc))
		ret = -EFAULT;

	if (ustatus && put_user(status, ustatus))
		ret = -EFAULT;
out:
	return ret;
}

#ifndef MODULE
asmlinkage long sys_spu_run(int fd, __u32 __user *unpc, __u32 __user *ustatus)
{
	int fput_needed;
	struct file *filp;
	long ret;

	ret = -EBADF;
	filp = fget_light(fd, &fput_needed);
	if (filp) {
		ret = do_spu_run(filp, unpc, ustatus);
		fput_light(filp, fput_needed);
	}

	return ret;
}
#endif

asmlinkage long sys_spu_create(const char __user *pathname,
					unsigned int flags, mode_t mode)
{
	char *tmp;
	int ret;

	tmp = getname(pathname);
	ret = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		struct nameidata nd;

		ret = path_lookup(tmp, LOOKUP_PARENT|
				LOOKUP_OPEN|LOOKUP_CREATE, &nd);
		if (!ret) {
			ret = spufs_create(&nd, flags, mode);
			path_release(&nd);
		}
		putname(tmp);
	}

	return ret;
}

struct spufs_calls spufs_calls = {
	.create_thread = sys_spu_create,
	.spu_run = do_spu_run,
	.owner = THIS_MODULE,
};
