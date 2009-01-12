#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/fs.h>
#include <linux/mqueue.h>
#include <asm/uaccess.h>


static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);
struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_mm);
EXPORT_SYMBOL(init_task);

union thread_union init_thread_union
	__attribute__((section(".data.init_thread")))
	= { INIT_THREAD_INFO(init_task) };
