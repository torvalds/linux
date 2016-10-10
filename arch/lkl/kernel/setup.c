#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/personality.h>
#include <linux/reboot.h>
#include <linux/tick.h>
#include <linux/fs.h>
#include <linux/start_kernel.h>
#include <linux/syscalls.h>
#include <asm/host_ops.h>
#include <asm/irq.h>
#include <asm/unistd.h>
#include <asm/syscalls.h>

struct lkl_host_operations *lkl_ops;
static char cmd_line[COMMAND_LINE_SIZE];
static void *idle_sem;
static void *init_sem;
static void *halt_sem;
static bool halt;
static int is_running;
void (*pm_power_off)(void) = NULL;
static unsigned long mem_size;

long lkl_panic_blink(int state)
{
	lkl_ops->panic();
	return 0;
}

void __init setup_arch(char **cl)
{
	*cl = cmd_line;
	panic_blink = lkl_panic_blink;
	bootmem_init(mem_size);
}

static void __init lkl_run_kernel(void *arg)
{
	threads_init();
	start_kernel();
}

int __init lkl_start_kernel(struct lkl_host_operations *ops,
			unsigned long _mem_size,
			const char *fmt, ...)
{
	va_list ap;
	int ret;

	lkl_ops = ops;
	mem_size = _mem_size;

	va_start(ap, fmt);
	ret = vsnprintf(boot_command_line, COMMAND_LINE_SIZE, fmt, ap);
	va_end(ap);

	if (ops->virtio_devices)
		strncpy(boot_command_line + ret, ops->virtio_devices,
			COMMAND_LINE_SIZE - ret);

	memcpy(cmd_line, boot_command_line, COMMAND_LINE_SIZE);

	init_sem = lkl_ops->sem_alloc(0);
	if (!init_sem)
		return -ENOMEM;

	idle_sem = lkl_ops->sem_alloc(0);
	if (!idle_sem) {
		ret = -ENOMEM;
		goto out_free_init_sem;
	}

	ret = lkl_ops->thread_create(lkl_run_kernel, NULL);
	if (!ret) {
		ret = -ENOMEM;
		goto out_free_idle_sem;
	}

	lkl_ops->sem_down(init_sem);

	is_running = 1;
	return 0;

out_free_idle_sem:
	lkl_ops->sem_free(idle_sem);

out_free_init_sem:
	lkl_ops->sem_free(init_sem);

	return ret;
}

int lkl_is_running(void)
{
	return is_running;
}

void machine_halt(void)
{
	halt = true;
}

void machine_power_off(void)
{
	machine_halt();
}

void machine_restart(char *unused)
{
	machine_halt();
}

long lkl_sys_halt(void)
{
	long err;
	long params[6] = { 0, };

	halt_sem = lkl_ops->sem_alloc(0);
	if (!halt_sem)
		return -ENOMEM;

	err = lkl_syscall(__NR_reboot, params);
	if (err < 0) {
		lkl_ops->sem_free(halt_sem);
		return err;
	}

	lkl_ops->sem_down(halt_sem);

	lkl_ops->sem_free(halt_sem);
	lkl_ops->sem_free(idle_sem);
	lkl_ops->sem_free(init_sem);

	free_initial_syscall_thread();

	free_mem();

	return 0;
}

void arch_cpu_idle(void)
{
	if (halt) {
		threads_cleanup();

		/* Shutdown the clockevents source. */
		tick_suspend_local();

		is_running = false;
		lkl_ops->sem_up(halt_sem);
		lkl_ops->thread_exit();
	}

	lkl_ops->sem_down(idle_sem);

	local_irq_enable();
}

void wakeup_cpu(void)
{
        if (!halt)
                lkl_ops->sem_up(idle_sem);
}

static int lkl_run_init(struct linux_binprm *bprm);

static struct linux_binfmt lkl_run_init_binfmt = {
	.module		= THIS_MODULE,
	.load_binary	= lkl_run_init,
};

static int lkl_run_init(struct linux_binprm *bprm)
{
	int ret;

	if (strcmp("/init", bprm->filename) != 0)
		return -EINVAL;

	ret = flush_old_exec(bprm);
	if (ret)
		return ret;
	set_personality(PER_LINUX);
	setup_new_exec(bprm);
	install_exec_creds(bprm);

	set_binfmt(&lkl_run_init_binfmt);

	initial_syscall_thread(init_sem);

	kernel_halt();

	return 0;
}


/* skip mounting the "real" rootfs. ramfs is good enough. */
static int __init fs_setup(void)
{
	int fd;

	fd = sys_open("/init", O_CREAT, 0700);
	WARN_ON(fd < 0);
	sys_close(fd);

	register_binfmt(&lkl_run_init_binfmt);

	return 0;
}
late_initcall(fs_setup);
