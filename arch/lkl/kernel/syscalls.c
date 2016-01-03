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

struct syscall {
	long no, *params, ret;
	void *sem;
};

static struct syscall_thread_data {
	wait_queue_head_t wqh;
	struct syscall *s;
	void *mutex, *completion;
} syscall_thread_data;


static struct syscall *dequeue_syscall(struct syscall_thread_data *data)
{

	return (struct syscall *)__sync_fetch_and_and((long *)&data->s, 0);
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
	struct syscall_thread_data *data = &syscall_thread_data;
	struct syscall *s;

	current->flags &= ~PF_KTHREAD;

	snprintf(current->comm, sizeof(current->comm), "init");

	while (1) {
		wait_event(data->wqh, (s = dequeue_syscall(data)) != NULL);

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
	wake_up(&syscall_thread_data.wqh);

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
	struct syscall_thread_data *data = &syscall_thread_data;
	struct syscall s;

	s.no = no;
	s.params = params;
	s.sem = data->completion;

	lkl_ops->sem_down(data->mutex);
	data->s = &s;
	lkl_trigger_irq(syscall_irq, NULL);
	lkl_ops->sem_down(data->completion);
	lkl_ops->sem_up(data->mutex);

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
	struct syscall_thread_data *data = &syscall_thread_data;

	init_waitqueue_head(&data->wqh);
	data->mutex = lkl_ops->sem_alloc(1);
	data->completion = lkl_ops->sem_alloc(0);
	BUG_ON(!data->mutex || !data->completion);

	syscall_irq = lkl_get_free_irq("syscall");
	setup_irq(syscall_irq, &syscall_irqaction);

	pr_info("lkl: syscall interface initialized (irq%d)\n", syscall_irq);
	return 0;
}
late_initcall(syscall_init);
