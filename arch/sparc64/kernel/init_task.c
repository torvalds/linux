#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

EXPORT_SYMBOL(init_mm);

/* .text section in head.S is aligned at 2 page boundary and this gets linked
 * right after that so that the init_thread_union is aligned properly as well.
 * We really don't need this special alignment like the Intel does, but
 * I do it anyways for completeness.
 */
__asm__ (".text");
union thread_union init_thread_union = { INIT_THREAD_INFO(init_task) };

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
EXPORT_SYMBOL(init_task);

__asm__(".data");
struct task_struct init_task = INIT_TASK(init_task);
