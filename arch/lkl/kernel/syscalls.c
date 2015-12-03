#include <linux/stat.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/task_work.h>
#include <linux/syscalls.h>
#include <asm/host_ops.h>
#include <asm/syscalls.h>

typedef long (*syscall_handler_t)(long arg1, ...);

#undef __SYSCALL
#define __SYSCALL(nr, sym) [nr] = (syscall_handler_t)sym,

syscall_handler_t syscall_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] =  (syscall_handler_t)sys_ni_syscall,
#include <asm/unistd.h>
};

static struct syscall_queue {
	struct list_head list;
	wait_queue_head_t wqh;
} syscall_queue;

struct syscall {
	long no, *params, ret;
	void *sem;
	struct list_head lh;
};

static struct syscall *dequeue_syscall(struct syscall_queue *sq)
{
	struct syscall *s = NULL;

	if (!list_empty(&sq->list)) {
		s = list_first_entry(&sq->list, typeof(*s), lh);
		list_del(&s->lh);
	}

	return s;
}

static long run_syscall(struct syscall *s)
{
	int ret;

	if (s->no < 0 || s->no >= __NR_syscalls)
		ret = -ENOSYS;
	else
		ret = syscall_table[s->no](s->params[0], s->params[1],
					   s->params[2], s->params[3],
					   s->params[4], s->params[5]);
	s->ret = ret;

	task_work_run();

	if (s->sem)
		lkl_ops->sem_up(s->sem);
	return ret;
}

int run_syscalls(void)
{
	struct syscall_queue *sq = &syscall_queue;
	struct syscall *s;

	current->flags &= ~PF_KTHREAD;

	snprintf(current->comm, sizeof(current->comm), "init");

	while (1) {
		wait_event(sq->wqh, (s = dequeue_syscall(sq)) != NULL);

		if (s->no == __NR_reboot)
			break;

		run_syscall(s);
	}

	s->ret = 0;
	lkl_ops->sem_up(s->sem);

	return 0;
}

static irqreturn_t syscall_irq_handler(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();
	struct syscall *s = regs->irq_data;

	list_add_tail(&s->lh, &syscall_queue.list);
	wake_up(&syscall_queue.wqh);

	return IRQ_HANDLED;
}

static struct irqaction syscall_irqaction  = {
	.handler = syscall_irq_handler,
	.flags = IRQF_NOBALANCING,
	.dev_id = &syscall_irqaction,
	.name = "syscall"
};

static int syscall_irq;

long lkl_syscall(long no, long *params)
{
	struct syscall s;

	s.no = no;
	s.params = params;

	s.sem = lkl_ops->sem_alloc(0);
	if (!s.sem)
		return -ENOMEM;

	lkl_trigger_irq(syscall_irq, &s);

	lkl_ops->sem_down(s.sem);
	lkl_ops->sem_free(s.sem);

	return s.ret;
}

asmlinkage
ssize_t sys_lkl_pwrite64(unsigned int fd, const char *buf, size_t count,
			 off_t pos_hi, off_t pos_lo)
{
	return sys_pwrite64(fd, buf, count, ((loff_t)pos_hi << 32) + pos_lo);
}

asmlinkage
ssize_t sys_lkl_pread64(unsigned int fd, char *buf, size_t count,
			off_t pos_hi, off_t pos_lo)
{
	return sys_pread64(fd, buf, count, ((loff_t)pos_hi << 32) + pos_lo);
}

int __init syscall_init(void)
{
	INIT_LIST_HEAD(&syscall_queue.list);
	init_waitqueue_head(&syscall_queue.wqh);

	syscall_irq = lkl_get_free_irq("syscall");
	setup_irq(syscall_irq, &syscall_irqaction);

	pr_info("lkl: syscall interface initialized\n");
	return 0;
}
late_initcall(syscall_init);
