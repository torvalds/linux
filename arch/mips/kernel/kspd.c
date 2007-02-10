/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/list.h>

#include <asm/vpe.h>
#include <asm/rtlx.h>
#include <asm/kspd.h>

static struct workqueue_struct *workqueue = NULL;
static struct work_struct work;

extern unsigned long cpu_khz;

struct mtsp_syscall {
	int cmd;
	unsigned char abi;
	unsigned char size;
};

struct mtsp_syscall_ret {
	int retval;
	int errno;
};

struct mtsp_syscall_generic {
	int arg0;
	int arg1;
	int arg2;
	int arg3;
	int arg4;
	int arg5;
	int arg6;
};

static struct list_head kspd_notifylist;
static int sp_stopping = 0;

/* these should match with those in the SDE kit */
#define MTSP_SYSCALL_BASE	0
#define MTSP_SYSCALL_EXIT	(MTSP_SYSCALL_BASE + 0)
#define MTSP_SYSCALL_OPEN	(MTSP_SYSCALL_BASE + 1)
#define MTSP_SYSCALL_READ	(MTSP_SYSCALL_BASE + 2)
#define MTSP_SYSCALL_WRITE	(MTSP_SYSCALL_BASE + 3)
#define MTSP_SYSCALL_CLOSE	(MTSP_SYSCALL_BASE + 4)
#define MTSP_SYSCALL_LSEEK32	(MTSP_SYSCALL_BASE + 5)
#define MTSP_SYSCALL_ISATTY	(MTSP_SYSCALL_BASE + 6)
#define MTSP_SYSCALL_GETTIME	(MTSP_SYSCALL_BASE + 7)
#define MTSP_SYSCALL_PIPEFREQ	(MTSP_SYSCALL_BASE + 8)
#define MTSP_SYSCALL_GETTOD	(MTSP_SYSCALL_BASE + 9)

#define MTSP_O_RDONLY		0x0000
#define MTSP_O_WRONLY		0x0001
#define MTSP_O_RDWR		0x0002
#define MTSP_O_NONBLOCK		0x0004
#define MTSP_O_APPEND		0x0008
#define MTSP_O_SHLOCK		0x0010
#define MTSP_O_EXLOCK		0x0020
#define MTSP_O_ASYNC		0x0040
#define MTSP_O_FSYNC		O_SYNC
#define MTSP_O_NOFOLLOW		0x0100
#define MTSP_O_SYNC		0x0080
#define MTSP_O_CREAT		0x0200
#define MTSP_O_TRUNC		0x0400
#define MTSP_O_EXCL		0x0800
#define MTSP_O_BINARY		0x8000

#define SP_VPE 1

struct apsp_table  {
	int sp;
	int ap;
};

/* we might want to do the mode flags too */
struct apsp_table open_flags_table[] = {
	{ MTSP_O_RDWR, O_RDWR },
	{ MTSP_O_WRONLY, O_WRONLY },
	{ MTSP_O_CREAT, O_CREAT },
	{ MTSP_O_TRUNC, O_TRUNC },
	{ MTSP_O_NONBLOCK, O_NONBLOCK },
	{ MTSP_O_APPEND, O_APPEND },
	{ MTSP_O_NOFOLLOW, O_NOFOLLOW }
};

struct apsp_table syscall_command_table[] = {
	{ MTSP_SYSCALL_OPEN, __NR_open },
	{ MTSP_SYSCALL_CLOSE, __NR_close },
	{ MTSP_SYSCALL_READ, __NR_read },
	{ MTSP_SYSCALL_WRITE, __NR_write },
	{ MTSP_SYSCALL_LSEEK32, __NR_lseek }
};

static int sp_syscall(int num, int arg0, int arg1, int arg2, int arg3)
{
	register long int _num  __asm__ ("$2") = num;
	register long int _arg0  __asm__ ("$4") = arg0;
	register long int _arg1  __asm__ ("$5") = arg1;
	register long int _arg2  __asm__ ("$6") = arg2;
	register long int _arg3  __asm__ ("$7") = arg3;

	mm_segment_t old_fs;

	old_fs = get_fs();
 	set_fs(KERNEL_DS);

  	__asm__ __volatile__ (
 	"	syscall					\n"
 	: "=r" (_num), "=r" (_arg3)
 	: "r" (_num), "r" (_arg0), "r" (_arg1), "r" (_arg2), "r" (_arg3));

	set_fs(old_fs);

	/* $a3 is error flag */
	if (_arg3)
		return -_num;

	return _num;
}

static int translate_syscall_command(int cmd)
{
	int i;
	int ret = -1;

	for (i = 0; i < ARRAY_SIZE(syscall_command_table); i++) {
		if ((cmd == syscall_command_table[i].sp))
			return syscall_command_table[i].ap;
	}

	return ret;
}

static unsigned int translate_open_flags(int flags)
{
	int i;
	unsigned int ret = 0;

	for (i = 0; i < (sizeof(open_flags_table) / sizeof(struct apsp_table));
	     i++) {
		if( (flags & open_flags_table[i].sp) ) {
			ret |= open_flags_table[i].ap;
		}
	}

	return ret;
}


static void sp_setfsuidgid( uid_t uid, gid_t gid)
{
	current->fsuid = uid;
	current->fsgid = gid;

	key_fsuid_changed(current);
	key_fsgid_changed(current);
}

/*
 * Expects a request to be on the sysio channel. Reads it.  Decides whether
 * its a linux syscall and runs it, or whatever.  Puts the return code back
 * into the request and sends the whole thing back.
 */
void sp_work_handle_request(void)
{
	struct mtsp_syscall sc;
	struct mtsp_syscall_generic generic;
	struct mtsp_syscall_ret ret;
	struct kspd_notifications *n;
	struct timeval tv;
	struct timezone tz;
	int cmd;

	char *vcwd;
	mm_segment_t old_fs;
	int size;

	ret.retval = -1;

	if (!rtlx_read(RTLX_CHANNEL_SYSIO, &sc, sizeof(struct mtsp_syscall), 0)) {
		printk(KERN_ERR "Expected request but nothing to read\n");
		return;
	}

	size = sc.size;

	if (size) {
		if (!rtlx_read(RTLX_CHANNEL_SYSIO, &generic, size, 0)) {
			printk(KERN_ERR "Expected request but nothing to read\n");
			return;
		}
	}

	/* Run the syscall at the priviledge of the user who loaded the
	   SP program */

	if (vpe_getuid(SP_VPE))
		sp_setfsuidgid( vpe_getuid(SP_VPE), vpe_getgid(SP_VPE));

	switch (sc.cmd) {
	/* needs the flags argument translating from SDE kit to
	   linux */
 	case MTSP_SYSCALL_PIPEFREQ:
 		ret.retval = cpu_khz * 1000;
 		ret.errno = 0;
 		break;

 	case MTSP_SYSCALL_GETTOD:
 		memset(&tz, 0, sizeof(tz));
 		if ((ret.retval = sp_syscall(__NR_gettimeofday, (int)&tv,
 		                             (int)&tz, 0,0)) == 0)
		ret.retval = tv.tv_sec;

		ret.errno = errno;
		break;

 	case MTSP_SYSCALL_EXIT:
		list_for_each_entry(n, &kspd_notifylist, list)
 			n->kspd_sp_exit(SP_VPE);
		sp_stopping = 1;

		printk(KERN_DEBUG "KSPD got exit syscall from SP exitcode %d\n",
		       generic.arg0);
 		break;

 	case MTSP_SYSCALL_OPEN:
 		generic.arg1 = translate_open_flags(generic.arg1);

 		vcwd = vpe_getcwd(SP_VPE);

 		/* change to the cwd of the process that loaded the SP program */
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		sys_chdir(vcwd);
		set_fs(old_fs);

 		sc.cmd = __NR_open;

		/* fall through */

  	default:
 		if ((sc.cmd >= __NR_Linux) &&
		    (sc.cmd <= (__NR_Linux +  __NR_Linux_syscalls)) )
			cmd = sc.cmd;
		else
			cmd = translate_syscall_command(sc.cmd);

		if (cmd >= 0) {
			ret.retval = sp_syscall(cmd, generic.arg0, generic.arg1,
			                        generic.arg2, generic.arg3);
			ret.errno = errno;
		} else
 			printk(KERN_WARNING
			       "KSPD: Unknown SP syscall number %d\n", sc.cmd);
		break;
 	} /* switch */

	if (vpe_getuid(SP_VPE))
		sp_setfsuidgid( 0, 0);

	if ((rtlx_write(RTLX_CHANNEL_SYSIO, &ret, sizeof(struct mtsp_syscall_ret), 0))
	    < sizeof(struct mtsp_syscall_ret))
		printk("KSPD: sp_work_handle_request failed to send to SP\n");
}

static void sp_cleanup(void)
{
	struct files_struct *files = current->files;
	int i, j;
	struct fdtable *fdt;

	j = 0;

	/*
	 * It is safe to dereference the fd table without RCU or
	 * ->file_lock
	 */
	fdt = files_fdtable(files);
	for (;;) {
		unsigned long set;
		i = j * __NFDBITS;
		if (i >= fdt->max_fds)
			break;
		set = fdt->open_fds->fds_bits[j++];
		while (set) {
			if (set & 1) {
				struct file * file = xchg(&fdt->fd[i], NULL);
				if (file)
					filp_close(file, files);
			}
			i++;
			set >>= 1;
		}
	}
}

static int channel_open = 0;

/* the work handler */
static void sp_work(struct work_struct *unused)
{
	if (!channel_open) {
		if( rtlx_open(RTLX_CHANNEL_SYSIO, 1) != 0) {
			printk("KSPD: unable to open sp channel\n");
			sp_stopping = 1;
		} else {
			channel_open++;
			printk(KERN_DEBUG "KSPD: SP channel opened\n");
		}
	} else {
		/* wait for some data, allow it to sleep */
		rtlx_read_poll(RTLX_CHANNEL_SYSIO, 1);

		/* Check we haven't been woken because we are stopping */
		if (!sp_stopping)
			sp_work_handle_request();
	}

	if (!sp_stopping)
		queue_work(workqueue, &work);
	else
		sp_cleanup();
}

static void startwork(int vpe)
{
	sp_stopping = channel_open = 0;

	if (workqueue == NULL) {
		if ((workqueue = create_singlethread_workqueue("kspd")) == NULL) {
			printk(KERN_ERR "unable to start kspd\n");
			return;
		}

		INIT_WORK(&work, sp_work);
		queue_work(workqueue, &work);
	} else
		queue_work(workqueue, &work);

}

static void stopwork(int vpe)
{
	sp_stopping = 1;

	printk(KERN_DEBUG "KSPD: SP stopping\n");
}

void kspd_notify(struct kspd_notifications *notify)
{
	list_add(&notify->list, &kspd_notifylist);
}

static struct vpe_notifications notify;
static int kspd_module_init(void)
{
	INIT_LIST_HEAD(&kspd_notifylist);

	notify.start = startwork;
	notify.stop = stopwork;
	vpe_notify(SP_VPE, &notify);

	return 0;
}

static void kspd_module_exit(void)
{

}

module_init(kspd_module_init);
module_exit(kspd_module_exit);

MODULE_DESCRIPTION("MIPS KSPD");
MODULE_AUTHOR("Elizabeth Oldham, MIPS Technologies, Inc.");
MODULE_LICENSE("GPL");
