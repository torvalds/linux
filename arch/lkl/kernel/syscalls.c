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
#include <linux/kthread.h>
#include <asm/host_ops.h>
#include <asm/syscalls.h>

struct syscall_thread_data;
static asmlinkage long sys_create_syscall_thread(struct syscall_thread_data *);

typedef long (*syscall_handler_t)(long arg1, ...);

#undef __SYSCALL
#define __SYSCALL(nr, sym) [nr] = (syscall_handler_t)sym,

syscall_handler_t syscall_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] =  (syscall_handler_t)sys_ni_syscall,
#include <asm/unistd.h>
};

struct syscall {
	long no, *params, ret;
};

static struct syscall_thread_data {
	struct syscall *s;
	void *mutex, *completion;
	int irq;
	/* to be accessed from Linux context only */
	wait_queue_head_t wqh;
} default_syscall_thread_data;

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

	return ret;
}

static irqreturn_t syscall_irq_handler(int irq, void *dev_id)
{
	struct syscall_thread_data *data = (struct syscall_thread_data *)dev_id;

	wake_up(&data->wqh);

	return IRQ_HANDLED;
}

int syscall_thread(void *_data)
{
	struct syscall_thread_data *data;
	struct syscall *s;
	int ret;
	static int count;

	data = (struct syscall_thread_data *)_data;
	init_waitqueue_head(&data->wqh);

	snprintf(current->comm, sizeof(current->comm), "ksyscalld%d", count++);

	data->irq = lkl_get_free_irq("syscall");
	if (data->irq < 0) {
		pr_err("lkl: %s: failed to allocate irq: %d\n", __func__,
		       data->irq);
		return data->irq;
	}

	ret = request_irq(data->irq, syscall_irq_handler, 0, current->comm,
			  data);
	if (ret) {
		pr_err("lkl: %s: failed to request irq %d: %d\n", __func__,
		       data->irq, ret);
		lkl_put_irq(data->irq, "syscall");
		data->irq = -1;
		return ret;
	}

	pr_info("lkl: syscall thread %s initialized (irq%d)\n", current->comm,
		data->irq);

	/* system call thread is ready */
	lkl_ops->sem_up(data->completion);

	while (1) {
		wait_event(data->wqh, (s = dequeue_syscall(data)) != NULL);

		if (s->no == __NR_reboot)
			break;

		run_syscall(s);

		lkl_ops->sem_up(data->completion);
	}

	free_irq(data->irq, data);
	lkl_put_irq(data->irq, "syscall");

	s->ret = 0;
	lkl_ops->sem_up(data->completion);

	return 0;
}

static unsigned int syscall_thread_data_key;

static int syscall_thread_data_init(struct syscall_thread_data *data,
				    void *completion)
{
	data->s = NULL;

	data->mutex = lkl_ops->sem_alloc(1);
	if (!data->mutex)
		return -ENOMEM;

	if (!completion)
		data->completion = lkl_ops->sem_alloc(0);
	else
		data->completion = completion;
	if (!data->completion) {
		lkl_ops->sem_free(data->mutex);
		return -ENOMEM;
	}

	return 0;
}

long lkl_syscall(long no, long *params)
{
	struct syscall_thread_data *data = NULL;
	struct syscall s;

	if (lkl_ops->tls_get)
		data = lkl_ops->tls_get(syscall_thread_data_key);
	if (!data)
		data = &default_syscall_thread_data;

	s.no = no;
	s.params = params;

	lkl_ops->sem_down(data->mutex);
	data->s = &s;
	lkl_trigger_irq(data->irq);
	lkl_ops->sem_down(data->completion);
	lkl_ops->sem_up(data->mutex);

	if (no == __NR_reboot) {
		lkl_ops->sem_free(data->completion);
		lkl_ops->sem_free(data->mutex);
		if (data != &default_syscall_thread_data)
			lkl_ops->mem_free(data);
	}

	return s.ret;
}

static int syscall_threads;

int lkl_create_syscall_thread(void)
{
	struct syscall_thread_data *data;
	long params[6], ret;

	if (!lkl_ops->tls_set)
		return -ENOTSUPP;

	data = lkl_ops->mem_alloc(sizeof(*data));
	if (!data)
		return -ENOMEM;

	ret = syscall_thread_data_init(data, NULL);
	if (ret < 0) {
		lkl_ops->mem_free(data);
		return ret;
	}

	params[0] = (long)data;
	ret = lkl_syscall(__NR_create_syscall_thread, params);
	if (ret < 0) {
		lkl_ops->sem_free(data->completion);
		lkl_ops->sem_free(data->mutex);
		lkl_put_irq(data->irq, "syscall");
		lkl_ops->mem_free(data);
		return ret;
	}

	lkl_ops->sem_down(data->completion);

	ret = lkl_ops->tls_set(syscall_thread_data_key, data);
	if (ret < 0) {
		lkl_ops->sem_free(data->completion);
		lkl_ops->sem_free(data->mutex);
		lkl_put_irq(data->irq, "syscall");
		lkl_ops->mem_free(data);
		return ret;
	}

	__sync_fetch_and_add(&syscall_threads, 1);

	return 0;
}

int lkl_stop_syscall_thread(void)
{
	struct syscall_thread_data *data;
	long params[6] = { 0, };
	int ret;

	if (!lkl_ops->tls_get || !lkl_ops->tls_set)
		return -ENOTSUPP;

	data = lkl_ops->tls_get(syscall_thread_data_key);
	if (!data || data == &default_syscall_thread_data)
		return -EINVAL;

	ret = lkl_syscall(__NR_reboot, params);
	if (ret < 0)
		return ret;

	ret = lkl_ops->tls_set(syscall_thread_data_key, NULL);
	if (ret)
		return ret;

	params[0] = 0;
	params[3] = WEXITED;
	ret = lkl_syscall(__NR_waitid, params);
	if (ret < 0)
		return ret;

	__sync_fetch_and_sub(&syscall_threads, 1);
	return 0;
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

static asmlinkage long
sys_create_syscall_thread(struct syscall_thread_data *data)
{
	pid_t pid;

	pid = kernel_thread(syscall_thread, data, CLONE_VM | CLONE_FS |
			    CLONE_FILES | SIGCHLD);
	if (pid < 0)
		return pid;

	return 0;
}

int initial_syscall_thread(void *sem)
{
	int ret = 0;

	if (lkl_ops->tls_alloc)
		ret = lkl_ops->tls_alloc(&syscall_thread_data_key);
	if (ret)
		return ret;

	init_pid_ns.child_reaper = 0;

	ret = syscall_thread_data_init(&default_syscall_thread_data, sem);
	if (ret) {
		if (lkl_ops->tls_free)
			lkl_ops->tls_free(syscall_thread_data_key);
		return ret;
	}

	ret = syscall_thread(&default_syscall_thread_data);
	if (lkl_ops->tls_free) {
		lkl_ops->tls_free(syscall_thread_data_key);
		__sync_synchronize();
		BUG_ON(syscall_threads);
	}

	return ret;
}

