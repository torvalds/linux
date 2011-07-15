/*
 * Alignment access counters and corresponding user-space interfaces.
 *
 * Copyright (C) 2009 ST Microelectronics
 * Copyright (C) 2009 - 2010 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>
#include <asm/alignment.h>
#include <asm/processor.h>

static unsigned long se_user;
static unsigned long se_sys;
static unsigned long se_half;
static unsigned long se_word;
static unsigned long se_dword;
static unsigned long se_multi;
/* bitfield: 1: warn 2: fixup 4: signal -> combinations 2|4 && 1|2|4 are not
   valid! */
static int se_usermode = UM_WARN | UM_FIXUP;
/* 0: no warning 1: print a warning message, disabled by default */
static int se_kernmode_warn;

core_param(alignment, se_usermode, int, 0600);

void inc_unaligned_byte_access(void)
{
	se_half++;
}

void inc_unaligned_word_access(void)
{
	se_word++;
}

void inc_unaligned_dword_access(void)
{
	se_dword++;
}

void inc_unaligned_multi_access(void)
{
	se_multi++;
}

void inc_unaligned_user_access(void)
{
	se_user++;
}

void inc_unaligned_kernel_access(void)
{
	se_sys++;
}

/*
 * This defaults to the global policy which can be set from the command
 * line, while processes can overload their preferences via prctl().
 */
unsigned int unaligned_user_action(void)
{
	unsigned int action = se_usermode;

	if (current->thread.flags & SH_THREAD_UAC_SIGBUS) {
		action &= ~UM_FIXUP;
		action |= UM_SIGNAL;
	}

	if (current->thread.flags & SH_THREAD_UAC_NOPRINT)
		action &= ~UM_WARN;

	return action;
}

int get_unalign_ctl(struct task_struct *tsk, unsigned long addr)
{
	return put_user(tsk->thread.flags & SH_THREAD_UAC_MASK,
			(unsigned int __user *)addr);
}

int set_unalign_ctl(struct task_struct *tsk, unsigned int val)
{
	tsk->thread.flags = (tsk->thread.flags & ~SH_THREAD_UAC_MASK) |
			    (val & SH_THREAD_UAC_MASK);
	return 0;
}

void unaligned_fixups_notify(struct task_struct *tsk, insn_size_t insn,
			     struct pt_regs *regs)
{
	if (user_mode(regs) && (se_usermode & UM_WARN))
		pr_notice_ratelimited("Fixing up unaligned userspace access "
			  "in \"%s\" pid=%d pc=0x%p ins=0x%04hx\n",
			  tsk->comm, task_pid_nr(tsk),
			  (void *)instruction_pointer(regs), insn);
	else if (se_kernmode_warn)
		pr_notice_ratelimited("Fixing up unaligned kernel access "
			  "in \"%s\" pid=%d pc=0x%p ins=0x%04hx\n",
			  tsk->comm, task_pid_nr(tsk),
			  (void *)instruction_pointer(regs), insn);
}

static const char *se_usermode_action[] = {
	"ignored",
	"warn",
	"fixup",
	"fixup+warn",
	"signal",
	"signal+warn"
};

static int alignment_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "User:\t\t%lu\n", se_user);
	seq_printf(m, "System:\t\t%lu\n", se_sys);
	seq_printf(m, "Half:\t\t%lu\n", se_half);
	seq_printf(m, "Word:\t\t%lu\n", se_word);
	seq_printf(m, "DWord:\t\t%lu\n", se_dword);
	seq_printf(m, "Multi:\t\t%lu\n", se_multi);
	seq_printf(m, "User faults:\t%i (%s)\n", se_usermode,
			se_usermode_action[se_usermode]);
	seq_printf(m, "Kernel faults:\t%i (fixup%s)\n", se_kernmode_warn,
			se_kernmode_warn ? "+warn" : "");
	return 0;
}

static int alignment_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, alignment_proc_show, NULL);
}

static ssize_t alignment_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	int *data = PDE(file->f_path.dentry->d_inode)->data;
	char mode;

	if (count > 0) {
		if (get_user(mode, buffer))
			return -EFAULT;
		if (mode >= '0' && mode <= '5')
			*data = mode - '0';
	}
	return count;
}

static const struct file_operations alignment_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= alignment_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= alignment_proc_write,
};

/*
 * This needs to be done after sysctl_init, otherwise sys/ will be
 * overwritten.  Actually, this shouldn't be in sys/ at all since
 * it isn't a sysctl, and it doesn't contain sysctl information.
 * We now locate it in /proc/cpu/alignment instead.
 */
static int __init alignment_init(void)
{
	struct proc_dir_entry *dir, *res;

	dir = proc_mkdir("cpu", NULL);
	if (!dir)
		return -ENOMEM;

	res = proc_create_data("alignment", S_IWUSR | S_IRUGO, dir,
			       &alignment_proc_fops, &se_usermode);
	if (!res)
		return -ENOMEM;

        res = proc_create_data("kernel_alignment", S_IWUSR | S_IRUGO, dir,
			       &alignment_proc_fops, &se_kernmode_warn);
        if (!res)
                return -ENOMEM;

	return 0;
}
fs_initcall(alignment_init);
