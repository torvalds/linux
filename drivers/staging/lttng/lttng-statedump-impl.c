/*
 * lttng-statedump.c
 *
 * Linux Trace Toolkit Next Generation Kernel State Dump
 *
 * Copyright 2005 Jean-Hugues Deschenes <jean-hugues.deschenes@polymtl.ca>
 * Copyright 2006-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Changes:
 *	Eric Clement:                   Add listing of network IP interface
 *	2006, 2007 Mathieu Desnoyers	Fix kernel threads
 *	                                Various updates
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include <linux/cpu.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fdtable.h>
#include <linux/swap.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#include "lttng-events.h"
#include "wrapper/irqdesc.h"
#include "wrapper/spinlock.h"
#include "wrapper/fdtable.h"
#include "wrapper/nsproxy.h"
#include "wrapper/irq.h"

#ifdef CONFIG_LTTNG_HAS_LIST_IRQ
#include <linux/irq.h>
#endif

/* Define the tracepoints, but do not build the probes */
#define CREATE_TRACE_POINTS
#define TRACE_INCLUDE_PATH ../instrumentation/events/lttng-module
#define TRACE_INCLUDE_FILE lttng-statedump
#include "instrumentation/events/lttng-module/lttng-statedump.h"

struct lttng_fd_ctx {
	char *page;
	struct lttng_session *session;
	struct task_struct *p;
};

/*
 * Protected by the trace lock.
 */
static struct delayed_work cpu_work[NR_CPUS];
static DECLARE_WAIT_QUEUE_HEAD(statedump_wq);
static atomic_t kernel_threads_to_run;

enum lttng_thread_type {
	LTTNG_USER_THREAD = 0,
	LTTNG_KERNEL_THREAD = 1,
};

enum lttng_execution_mode {
	LTTNG_USER_MODE = 0,
	LTTNG_SYSCALL = 1,
	LTTNG_TRAP = 2,
	LTTNG_IRQ = 3,
	LTTNG_SOFTIRQ = 4,
	LTTNG_MODE_UNKNOWN = 5,
};

enum lttng_execution_submode {
	LTTNG_NONE = 0,
	LTTNG_UNKNOWN = 1,
};

enum lttng_process_status {
	LTTNG_UNNAMED = 0,
	LTTNG_WAIT_FORK = 1,
	LTTNG_WAIT_CPU = 2,
	LTTNG_EXIT = 3,
	LTTNG_ZOMBIE = 4,
	LTTNG_WAIT = 5,
	LTTNG_RUN = 6,
	LTTNG_DEAD = 7,
};

#ifdef CONFIG_INET
static
void lttng_enumerate_device(struct lttng_session *session,
		struct net_device *dev)
{
	struct in_device *in_dev;
	struct in_ifaddr *ifa;

	if (dev->flags & IFF_UP) {
		in_dev = in_dev_get(dev);
		if (in_dev) {
			for (ifa = in_dev->ifa_list; ifa != NULL;
			     ifa = ifa->ifa_next) {
				trace_lttng_statedump_network_interface(
					session, dev, ifa);
			}
			in_dev_put(in_dev);
		}
	} else {
		trace_lttng_statedump_network_interface(
			session, dev, NULL);
	}
}

static
int lttng_enumerate_network_ip_interface(struct lttng_session *session)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	for_each_netdev(&init_net, dev)
		lttng_enumerate_device(session, dev);
	read_unlock(&dev_base_lock);

	return 0;
}
#else /* CONFIG_INET */
static inline
int lttng_enumerate_network_ip_interface(struct lttng_session *session)
{
	return 0;
}
#endif /* CONFIG_INET */

static
int lttng_dump_one_fd(const void *p, struct file *file, unsigned int fd)
{
	const struct lttng_fd_ctx *ctx = p;
	const char *s = d_path(&file->f_path, ctx->page, PAGE_SIZE);

	if (IS_ERR(s)) {
		struct dentry *dentry = file->f_path.dentry;

		/* Make sure we give at least some info */
		spin_lock(&dentry->d_lock);
		trace_lttng_statedump_file_descriptor(ctx->session, ctx->p, fd,
			dentry->d_name.name);
		spin_unlock(&dentry->d_lock);
		goto end;
	}
	trace_lttng_statedump_file_descriptor(ctx->session, ctx->p, fd, s);
end:
	return 0;
}

static
void lttng_enumerate_task_fd(struct lttng_session *session,
		struct task_struct *p, char *tmp)
{
	struct lttng_fd_ctx ctx = { .page = tmp, .session = session, .p = p };

	task_lock(p);
	lttng_iterate_fd(p->files, 0, lttng_dump_one_fd, &ctx);
	task_unlock(p);
}

static
int lttng_enumerate_file_descriptors(struct lttng_session *session)
{
	struct task_struct *p;
	char *tmp = (char *) __get_free_page(GFP_KERNEL);

	/* Enumerate active file descriptors */
	rcu_read_lock();
	for_each_process(p)
		lttng_enumerate_task_fd(session, p, tmp);
	rcu_read_unlock();
	free_page((unsigned long) tmp);
	return 0;
}

#if 0
/*
 * FIXME: we cannot take a mmap_sem while in a RCU read-side critical section
 * (scheduling in atomic). Normally, the tasklist lock protects this kind of
 * iteration, but it is not exported to modules.
 */
static
void lttng_enumerate_task_vm_maps(struct lttng_session *session,
		struct task_struct *p)
{
	struct mm_struct *mm;
	struct vm_area_struct *map;
	unsigned long ino;

	/* get_task_mm does a task_lock... */
	mm = get_task_mm(p);
	if (!mm)
		return;

	map = mm->mmap;
	if (map) {
		down_read(&mm->mmap_sem);
		while (map) {
			if (map->vm_file)
				ino = map->vm_file->f_dentry->d_inode->i_ino;
			else
				ino = 0;
			trace_lttng_statedump_vm_map(session, p, map, ino);
			map = map->vm_next;
		}
		up_read(&mm->mmap_sem);
	}
	mmput(mm);
}

static
int lttng_enumerate_vm_maps(struct lttng_session *session)
{
	struct task_struct *p;

	rcu_read_lock();
	for_each_process(p)
		lttng_enumerate_task_vm_maps(session, p);
	rcu_read_unlock();
	return 0;
}
#endif

#ifdef CONFIG_LTTNG_HAS_LIST_IRQ

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
#define irq_desc_get_chip(desc) get_irq_desc_chip(desc)
#endif

static
void lttng_list_interrupts(struct lttng_session *session)
{
	unsigned int irq;
	unsigned long flags = 0;
	struct irq_desc *desc;

#define irq_to_desc	wrapper_irq_to_desc
	/* needs irq_desc */
	for_each_irq_desc(irq, desc) {
		struct irqaction *action;
		const char *irq_chip_name =
			irq_desc_get_chip(desc)->name ? : "unnamed_irq_chip";

		local_irq_save(flags);
		wrapper_desc_spin_lock(&desc->lock);
		for (action = desc->action; action; action = action->next) {
			trace_lttng_statedump_interrupt(session,
				irq, irq_chip_name, action);
		}
		wrapper_desc_spin_unlock(&desc->lock);
		local_irq_restore(flags);
	}
#undef irq_to_desc
}
#else
static inline
void lttng_list_interrupts(struct lttng_session *session)
{
}
#endif

static
void lttng_statedump_process_ns(struct lttng_session *session,
		struct task_struct *p,
		enum lttng_thread_type type,
		enum lttng_execution_mode mode,
		enum lttng_execution_submode submode,
		enum lttng_process_status status)
{
	struct nsproxy *proxy;
	struct pid_namespace *pid_ns;

	rcu_read_lock();
	proxy = task_nsproxy(p);
	if (proxy) {
		pid_ns = lttng_get_proxy_pid_ns(proxy);
		do {
			trace_lttng_statedump_process_state(session,
				p, type, mode, submode, status, pid_ns);
			pid_ns = pid_ns->parent;
		} while (pid_ns);
	} else {
		trace_lttng_statedump_process_state(session,
			p, type, mode, submode, status, NULL);
	}
	rcu_read_unlock();
}

static
int lttng_enumerate_process_states(struct lttng_session *session)
{
	struct task_struct *g, *p;

	rcu_read_lock();
	for_each_process(g) {
		p = g;
		do {
			enum lttng_execution_mode mode =
				LTTNG_MODE_UNKNOWN;
			enum lttng_execution_submode submode =
				LTTNG_UNKNOWN;
			enum lttng_process_status status;
			enum lttng_thread_type type;

			task_lock(p);
			if (p->exit_state == EXIT_ZOMBIE)
				status = LTTNG_ZOMBIE;
			else if (p->exit_state == EXIT_DEAD)
				status = LTTNG_DEAD;
			else if (p->state == TASK_RUNNING) {
				/* Is this a forked child that has not run yet? */
				if (list_empty(&p->rt.run_list))
					status = LTTNG_WAIT_FORK;
				else
					/*
					 * All tasks are considered as wait_cpu;
					 * the viewer will sort out if the task
					 * was really running at this time.
					 */
					status = LTTNG_WAIT_CPU;
			} else if (p->state &
				(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) {
				/* Task is waiting for something to complete */
				status = LTTNG_WAIT;
			} else
				status = LTTNG_UNNAMED;
			submode = LTTNG_NONE;

			/*
			 * Verification of t->mm is to filter out kernel
			 * threads; Viewer will further filter out if a
			 * user-space thread was in syscall mode or not.
			 */
			if (p->mm)
				type = LTTNG_USER_THREAD;
			else
				type = LTTNG_KERNEL_THREAD;
			lttng_statedump_process_ns(session,
				p, type, mode, submode, status);
			task_unlock(p);
		} while_each_thread(g, p);
	}
	rcu_read_unlock();

	return 0;
}

static
void lttng_statedump_work_func(struct work_struct *work)
{
	if (atomic_dec_and_test(&kernel_threads_to_run))
		/* If we are the last thread, wake up do_lttng_statedump */
		wake_up(&statedump_wq);
}

static
int do_lttng_statedump(struct lttng_session *session)
{
	int cpu;

	trace_lttng_statedump_start(session);
	lttng_enumerate_process_states(session);
	lttng_enumerate_file_descriptors(session);
	/* FIXME lttng_enumerate_vm_maps(session); */
	lttng_list_interrupts(session);
	lttng_enumerate_network_ip_interface(session);

	/* TODO lttng_dump_idt_table(session); */
	/* TODO lttng_dump_softirq_vec(session); */
	/* TODO lttng_list_modules(session); */
	/* TODO lttng_dump_swap_files(session); */

	/*
	 * Fire off a work queue on each CPU. Their sole purpose in life
	 * is to guarantee that each CPU has been in a state where is was in
	 * syscall mode (i.e. not in a trap, an IRQ or a soft IRQ).
	 */
	get_online_cpus();
	atomic_set(&kernel_threads_to_run, num_online_cpus());
	for_each_online_cpu(cpu) {
		INIT_DELAYED_WORK(&cpu_work[cpu], lttng_statedump_work_func);
		schedule_delayed_work_on(cpu, &cpu_work[cpu], 0);
	}
	/* Wait for all threads to run */
	__wait_event(statedump_wq, (atomic_read(&kernel_threads_to_run) == 0));
	put_online_cpus();
	/* Our work is done */
	trace_lttng_statedump_end(session);
	return 0;
}

/*
 * Called with session mutex held.
 */
int lttng_statedump_start(struct lttng_session *session)
{
	return do_lttng_statedump(session);
}
EXPORT_SYMBOL_GPL(lttng_statedump_start);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Jean-Hugues Deschenes");
MODULE_DESCRIPTION("Linux Trace Toolkit Next Generation Statedump");
