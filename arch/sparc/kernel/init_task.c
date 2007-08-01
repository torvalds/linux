#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);
struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_mm);
EXPORT_SYMBOL(init_task);

/* .text section in head.S is aligned at 8k boundary and this gets linked
 * right after that so that the init_thread_union is aligned properly as well.
 * If this is not aligned on a 8k boundry, then you should change code
 * in etrap.S which assumes it.
 */
union thread_union init_thread_union
	__attribute__((section (".text\"\n\t#")))
	__attribute__((aligned (THREAD_SIZE)))
	= { INIT_THREAD_INFO(init_task) };
