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
#include <linux/platform_device.h>
#include <asm/host_ops.h>
#include <asm/syscalls.h>
#include <asm/syscalls_32.h>

struct syscall_thread_data;
static asmlinkage long sys_create_syscall_thread(struct syscall_thread_data *);
static asmlinkage long sys_virtio_mmio_device_add(long base, long size,
						  unsigned int irq);

typedef long (*syscall_handler_t)(long arg1, ...);

#undef __SYSCALL
#define __SYSCALL(nr, sym) [nr] = (syscall_handler_t)sym,

syscall_handler_t syscall_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] =  (syscall_handler_t)sys_ni_syscall,
#include <asm/unistd.h>

#if __BITS_PER_LONG == 32
#include <asm/unistd_32.h>
#endif
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
	struct list_head list;
	bool stop;
	struct completion stopped;
} default_syscall_thread_data;

static LIST_HEAD(syscall_threads);

static struct syscall *dequeue_syscall(struct syscall_thread_data *data)
{

	return (struct syscall *)__sync_fetch_and_and((long *)&data->s, 0);
}

static long run_syscall(struct syscall *s)
{
	long ret;

	if (s->no < 0 || s->no >= __NR_syscalls)
		ret = -ENOSYS;
	else {
		ret = syscall_table[s->no](s->params[0], s->params[1],
					   s->params[2], s->params[3],
					   s->params[4], s->params[5]);
	}
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

static void cleanup_syscall_threads(void);

int syscall_thread(void *_data)
{
	struct syscall_thread_data *data;
	struct syscall *s;
	int ret;
	static int count;

	data = (struct syscall_thread_data *)_data;
	init_waitqueue_head(&data->wqh);
	list_add(&data->list, &syscall_threads);
	init_completion(&data->stopped);

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
		wait_event(data->wqh,
			   (s = dequeue_syscall(data)) != NULL || data->stop);

		if (data->stop || s->no == __NR_reboot)
			break;

		run_syscall(s);

		lkl_ops->sem_up(data->completion);
	}

	if (data == &default_syscall_thread_data)
		cleanup_syscall_threads();

	pr_info("lkl: exiting syscall thread %s\n", current->comm);

	list_del(&data->list);

	free_irq(data->irq, data);
	lkl_put_irq(data->irq, "syscall");

	if (data->stop) {
		complete(&data->stopped);
	} else {
		s->ret = 0;
		lkl_ops->sem_up(data->completion);
	}

	return 0;
}

static unsigned int syscall_thread_data_key;

static int syscall_thread_data_init(struct syscall_thread_data *data,
				    void *completion)
{
	data->mutex = lkl_ops->sem_alloc(1);
	if (!data->mutex)
		return -ENOMEM;

	if (!completion)
		data->completion = lkl_ops->sem_alloc(0);
	else
		data->completion = completion;
	if (!data->completion) {
		lkl_ops->sem_free(data->mutex);
		data->mutex = NULL;
		return -ENOMEM;
	}

	return 0;
}

static long __lkl_syscall(struct syscall_thread_data *data, long no,
			  long *params)
{
	struct syscall s;

	s.no = no;
	s.params = params;

	lkl_ops->sem_down(data->mutex);
	data->s = &s;
	lkl_trigger_irq(data->irq);
	lkl_ops->sem_down(data->completion);
	lkl_ops->sem_up(data->mutex);

	return s.ret;
}

static struct syscall_thread_data *__lkl_create_syscall_thread(void)
{
	struct syscall_thread_data *data;
	long params[6], ret;

	if (!lkl_ops->tls_set)
		return ERR_PTR(-ENOTSUPP);

	data = lkl_ops->mem_alloc(sizeof(*data));
	if (!data)
		return ERR_PTR(-ENOMEM);

	memset(data, 0, sizeof(*data));

	ret = syscall_thread_data_init(data, NULL);
	if (ret < 0)
		goto out_free;

	ret = lkl_ops->tls_set(syscall_thread_data_key, data);
	if (ret < 0)
		goto out_free;

	params[0] = (long)data;
	ret = __lkl_syscall(&default_syscall_thread_data,
			    __NR_create_syscall_thread, params);
	if (ret < 0)
		goto out_free;

	lkl_ops->sem_down(data->completion);

	return data;

out_free:
	lkl_ops->sem_free(data->completion);
	lkl_ops->sem_free(data->mutex);
	lkl_ops->mem_free(data);

	return ERR_PTR(ret);
}

int lkl_create_syscall_thread(void)
{
	struct syscall_thread_data *data = __lkl_create_syscall_thread();

	if (IS_ERR(data))
		return PTR_ERR(data);
	return 0;
}

static int kernel_stop_syscall_thread(struct syscall_thread_data *data)
{
	data->stop = true;
	wake_up(&data->wqh);
	wait_for_completion(&data->stopped);

	return 0;
}

static int __lkl_stop_syscall_thread(struct syscall_thread_data *data,
				     bool host)
{
	long ret, params[6];

	if (host)
		ret = __lkl_syscall(data, __NR_reboot, params);
	else
		ret = kernel_stop_syscall_thread(data);
	if (ret)
		return ret;

	lkl_ops->sem_free(data->completion);
	lkl_ops->sem_free(data->mutex);
	lkl_ops->mem_free(data);

	return 0;
}

int lkl_stop_syscall_thread(void)
{
	struct syscall_thread_data *data = NULL;
	int ret;

	if (lkl_ops->tls_get)
		data = lkl_ops->tls_get(syscall_thread_data_key);
	if (!data)
		return -EINVAL;

	ret = __lkl_stop_syscall_thread(data, true);
	if (!ret && lkl_ops->tls_set)
		lkl_ops->tls_set(syscall_thread_data_key, NULL);
	return ret;
}

static int auto_syscall_threads = true;
static int __init setup_auto_syscall_threads(char *str)
{
	get_option (&str, &auto_syscall_threads);

	return 1;
}
__setup("lkl_auto_syscall_threads=", setup_auto_syscall_threads);


long lkl_syscall(long no, long *params)
{
	struct syscall_thread_data *data = NULL;

	if (auto_syscall_threads && lkl_ops->tls_get) {
		data = lkl_ops->tls_get(syscall_thread_data_key);
		if (!data) {
			data = __lkl_create_syscall_thread();
			if (!data)
				lkl_puts("failed to create syscall thread\n");
		}
	}
	if (!data || no == __NR_reboot)
		data = &default_syscall_thread_data;

	return __lkl_syscall(data, no, params);
}

static asmlinkage long
sys_create_syscall_thread(struct syscall_thread_data *data)
{
	pid_t pid;

	pid = kernel_thread(syscall_thread, data, CLONE_VM | CLONE_FS |
			CLONE_FILES | CLONE_THREAD | CLONE_SIGHAND | SIGCHLD);
	if (pid < 0)
		return pid;

	return 0;
}


/*
 * A synchronization algorithm between cleanup_syscall_threads (which terminates
 * all remaining syscall threads) and destructors functions (which frees a
 * syscall thread as soon as the associated host thread terminates) is required
 * since destructor functions run in host context and is not subject to kernel
 * scheduling.
 *
 * An atomic counter is used to count the number of running destructor functions
 * and allows the cleanup function to wait for the running destructor functions
 * to complete.
 *
 * The cleanup functions adds MAX_SYSCALL_THREADS to this counter and this
 * allows the destructor functions to check if the cleanup process has started
 * and abort execution. This prevents "late" destructors from trying to free the
 * syscall threads.
 *
 * This algorithm assumes that we never have more the MAX_SYSCALL_THREADS
 * running.
 */
#define MAX_SYSCALL_THREADS 1000000
static unsigned int destrs;

/*
 * This is called when the host thread terminates if auto_syscall_threads is
 * enabled. We use it to remove the associated kernel syscall thread since it is
 * not going to be used anymore.
 *
 * Note that this run in host context, not kernel context.
 *
 * To avoid races between the destructor and lkl_sys_halt we announce that a
 * destructor is running and also check to see if lkl_sys_halt is running, in
 * which case we bail out - the kernel thread is going to be / has been stopped
 * by lkl_sys_halt.
 */
static void syscall_thread_destructor(void *_data)
{
	struct syscall_thread_data *data = _data;

	if (!data)
		return;

	if (__sync_fetch_and_add(&destrs, 1) < MAX_SYSCALL_THREADS)
		__lkl_stop_syscall_thread(data, true);
	__sync_fetch_and_sub(&destrs, 1);
}

static void cleanup_syscall_threads(void)
{
	struct syscall_thread_data *i = NULL, *aux;

	/* announce destructors that we are stopping */
	__sync_fetch_and_add(&destrs, MAX_SYSCALL_THREADS);

	/* wait for any pending destructors to complete */
	while (__sync_fetch_and_add(&destrs, 0) > MAX_SYSCALL_THREADS)
		schedule_timeout(1);

	/* no more destructors, we can safely remove the remaining threads */
	list_for_each_entry_safe(i, aux, &syscall_threads, list) {
		if (i == &default_syscall_thread_data)
			continue;
		__lkl_stop_syscall_thread(i, false);
	}
}

int initial_syscall_thread(void *sem)
{
	void (*destr)(void *) = NULL;
	int ret = 0;

	if (auto_syscall_threads)
		destr = syscall_thread_destructor;

	if (lkl_ops->tls_alloc)
		ret = lkl_ops->tls_alloc(&syscall_thread_data_key, destr);
	if (ret)
		return ret;

	init_pid_ns.child_reaper = 0;

	ret = syscall_thread_data_init(&default_syscall_thread_data, sem);
	if (ret)
		goto out;

	ret = syscall_thread(&default_syscall_thread_data);

out:
	if (lkl_ops->tls_free)
		lkl_ops->tls_free(syscall_thread_data_key);


	return ret;
}

void free_initial_syscall_thread(void)
{
	lkl_ops->sem_free(default_syscall_thread_data.mutex);
	lkl_ops->sem_free(default_syscall_thread_data.completion);
}

SYSCALL_DEFINE3(virtio_mmio_device_add, long, base, long, size, unsigned int,
		irq)
{
	struct platform_device *pdev;
	int ret;

	struct resource res[] = {
		[0] = {
		       .start = base,
		       .end = base + size - 1,
		       .flags = IORESOURCE_MEM,
		       },
		[1] = {
		       .start = irq,
		       .end = irq,
		       .flags = IORESOURCE_IRQ,
		       },
	};

	pdev = platform_device_alloc("virtio-mmio", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		dev_err(&pdev->dev, "%s: Unable to device alloc for virtio-mmio\n", __func__);
		return -ENOMEM;
	}

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n", __func__, pdev->name, pdev->id);
		goto exit_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Unable to add %s%d\n", __func__, pdev->name, pdev->id);
		goto exit_release_pdev;
	}

	return pdev->id;

exit_release_pdev:
	platform_device_del(pdev);
exit_device_put:
	platform_device_put(pdev);

	return ret;
}
