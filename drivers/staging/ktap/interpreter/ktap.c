/*
 * ktap.c - ktapvm kernel module main entry
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * this file is the first file to be compile, add CONFIG_ checking in here.
 * See Requirements in doc/introduction.txt
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
#error "Currently ktap don't support kernel older than 3.1"
#endif

#if !CONFIG_EVENT_TRACING
#error "Please enable CONFIG_EVENT_TRACING before compile ktap"
#endif

#if !CONFIG_PERF_EVENTS
#error "Please enable CONFIG_PERF_EVENTS before compile ktap"
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include "../include/ktap.h"

static int load_trunk(struct ktap_parm *parm, unsigned long **buff)
{
	int ret;
	unsigned long *vmstart;

	vmstart = vmalloc(parm->trunk_len);
	if (!vmstart)
		return -ENOMEM;

	ret = copy_from_user(vmstart, (void __user *)parm->trunk,
			     parm->trunk_len);
	if (ret < 0) {
		vfree(vmstart);
		return -EFAULT;
	}

	*buff = vmstart;
	return 0;
}

int gettimeofday_us(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	return tv.tv_sec * USEC_PER_SEC + tv.tv_usec;
}

struct dentry *kp_dir_dentry;
static atomic_t kp_is_running = ATOMIC_INIT(0);

/* Ktap Main Entry */
static int ktap_main(struct file *file, ktap_parm *parm)
{
	unsigned long *buff = NULL;
	ktap_state *ks;
	ktap_closure *cl;
	int start_time, delta_time;
	int ret;

	if (atomic_inc_return(&kp_is_running) != 1) {
		atomic_dec(&kp_is_running);
		pr_info("only one ktap thread allow to run\n");
		return -EBUSY;
	}

	start_time = gettimeofday_us();

	ks = kp_newstate(parm, kp_dir_dentry);
	if (unlikely(!ks)) {
		ret = -ENOEXEC;
		goto out;
	}

	file->private_data = ks;

	ret = load_trunk(parm, &buff);
	if (ret) {
		pr_err("cannot load file\n");
		goto out;
	}

	cl = kp_load(ks, (unsigned char *)buff);

	vfree(buff);

	if (cl) {
		/* optimize bytecode before excuting */
		kp_optimize_code(ks, 0, cl->l.p);

		delta_time = gettimeofday_us() - start_time;
		kp_verbose_printf(ks, "booting time: %d (us)\n", delta_time);
		kp_call(ks, ks->top - 1, 0);
	}

	kp_final_exit(ks);

 out:
	atomic_dec(&kp_is_running);	
	return ret;
}


static void print_version(void)
{
}

static long ktap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	ktap_parm parm;
	int ret;

	switch (cmd) {
	case KTAP_CMD_IOC_VERSION:
		print_version();
		return 0;
	case KTAP_CMD_IOC_RUN:
		ret = copy_from_user(&parm, (void __user *)arg,
				     sizeof(ktap_parm));
		if (ret < 0)
			return -EFAULT;

		return ktap_main(file, &parm);
	default:
		return -EINVAL;
	};

        return 0;
}

static const struct file_operations ktap_fops = {
	.llseek                 = no_llseek,
	.unlocked_ioctl         = ktap_ioctl,
};

static long ktapvm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int new_fd, err;
	struct file *new_file;

	new_fd = get_unused_fd();
	if (new_fd < 0)
		return new_fd;

	new_file = anon_inode_getfile("[ktap]", &ktap_fops, NULL, O_RDWR);
	if (IS_ERR(new_file)) {
		err = PTR_ERR(new_file);
		put_unused_fd(new_fd);
		return err;
	}

	file->private_data = NULL;
	fd_install(new_fd, new_file);
	return new_fd;
}

static const struct file_operations ktapvm_fops = {
	.owner  = THIS_MODULE,
	.unlocked_ioctl         = ktapvm_ioctl,
};

unsigned int kp_stub_exit_instr;

static int __init init_ktap(void)
{
	struct dentry *ktapvm_dentry;

	kp_dir_dentry = debugfs_create_dir("ktap", NULL);
	if (!kp_dir_dentry) {
		pr_err("ktap: debugfs_create_dir failed\n");
		return -1;
	}

	ktapvm_dentry = debugfs_create_file("ktapvm", 0444, kp_dir_dentry, NULL,
					    &ktapvm_fops);

	if (!ktapvm_dentry) {
		pr_err("ktapvm: cannot create ktapvm file\n");
		debugfs_remove_recursive(kp_dir_dentry);
		return -1;
	}

	SET_OPCODE(kp_stub_exit_instr, OP_EXIT);

	return 0;
}

static void __exit exit_ktap(void)
{
	debugfs_remove_recursive(kp_dir_dentry);
}

module_init(init_ktap);
module_exit(exit_ktap);

MODULE_AUTHOR("Jovi Zhangwei <jovi.zhangwei@gmail.com>");
MODULE_DESCRIPTION("ktap");
MODULE_LICENSE("GPL");

int kp_max_exec_count = 10000;
module_param_named(max_exec_count, kp_max_exec_count, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_exec_count, "non-mainthread max instruction execution count");

