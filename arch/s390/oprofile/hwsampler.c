/**
 * arch/s390/oprofile/hwsampler.c
 *
 * Copyright IBM Corp. 2010
 * Author: Heinz Graalfs <graalfs@de.ibm.com>
 */

#include <linux/kernel_stat.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/semaphore.h>
#include <linux/oom.h>
#include <linux/oprofile.h>
#include <asm/cpu_mf.h>
#include <asm/irq.h>

#include "hwsampler.h"
#include "op_counter.h"

#define MAX_NUM_SDB 511
#define MIN_NUM_SDB 1

#define ALERT_REQ_MASK   0x4000000000000000ul
#define BUFFER_FULL_MASK 0x8000000000000000ul

DECLARE_PER_CPU(struct hws_cpu_buffer, sampler_cpu_buffer);

struct hws_execute_parms {
	void *buffer;
	signed int rc;
};

DEFINE_PER_CPU(struct hws_cpu_buffer, sampler_cpu_buffer);
EXPORT_PER_CPU_SYMBOL(sampler_cpu_buffer);

static DEFINE_MUTEX(hws_sem);
static DEFINE_MUTEX(hws_sem_oom);

static unsigned char hws_flush_all;
static unsigned int hws_oom;
static struct workqueue_struct *hws_wq;

static unsigned int hws_state;
enum {
	HWS_INIT = 1,
	HWS_DEALLOCATED,
	HWS_STOPPED,
	HWS_STARTED,
	HWS_STOPPING };

/* set to 1 if called by kernel during memory allocation */
static unsigned char oom_killer_was_active;
/* size of SDBT and SDB as of allocate API */
static unsigned long num_sdbt = 100;
static unsigned long num_sdb = 511;
/* sampling interval (machine cycles) */
static unsigned long interval;

static unsigned long min_sampler_rate;
static unsigned long max_sampler_rate;

static int ssctl(void *buffer)
{
	int cc;

	/* set in order to detect a program check */
	cc = 1;

	asm volatile(
		"0: .insn s,0xB2870000,0(%1)\n"
		"1: ipm %0\n"
		"   srl %0,28\n"
		"2:\n"
		EX_TABLE(0b, 2b) EX_TABLE(1b, 2b)
		: "+d" (cc), "+a" (buffer)
		: "m" (*((struct hws_ssctl_request_block *)buffer))
		: "cc", "memory");

	return cc ? -EINVAL : 0 ;
}

static int qsi(void *buffer)
{
	int cc;
	cc = 1;

	asm volatile(
		"0: .insn s,0xB2860000,0(%1)\n"
		"1: lhi %0,0\n"
		"2:\n"
		EX_TABLE(0b, 2b) EX_TABLE(1b, 2b)
		: "=d" (cc), "+a" (buffer)
		: "m" (*((struct hws_qsi_info_block *)buffer))
		: "cc", "memory");

	return cc ? -EINVAL : 0;
}

static void execute_qsi(void *parms)
{
	struct hws_execute_parms *ep = parms;

	ep->rc = qsi(ep->buffer);
}

static void execute_ssctl(void *parms)
{
	struct hws_execute_parms *ep = parms;

	ep->rc = ssctl(ep->buffer);
}

static int smp_ctl_ssctl_stop(int cpu)
{
	int rc;
	struct hws_execute_parms ep;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	cb->ssctl.es = 0;
	cb->ssctl.cs = 0;

	ep.buffer = &cb->ssctl;
	smp_call_function_single(cpu, execute_ssctl, &ep, 1);
	rc = ep.rc;
	if (rc) {
		printk(KERN_ERR "hwsampler: CPU %d CPUMF SSCTL failed.\n", cpu);
		dump_stack();
	}

	ep.buffer = &cb->qsi;
	smp_call_function_single(cpu, execute_qsi, &ep, 1);

	if (cb->qsi.es || cb->qsi.cs) {
		printk(KERN_EMERG "CPUMF sampling did not stop properly.\n");
		dump_stack();
	}

	return rc;
}

static int smp_ctl_ssctl_deactivate(int cpu)
{
	int rc;
	struct hws_execute_parms ep;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	cb->ssctl.es = 1;
	cb->ssctl.cs = 0;

	ep.buffer = &cb->ssctl;
	smp_call_function_single(cpu, execute_ssctl, &ep, 1);
	rc = ep.rc;
	if (rc)
		printk(KERN_ERR "hwsampler: CPU %d CPUMF SSCTL failed.\n", cpu);

	ep.buffer = &cb->qsi;
	smp_call_function_single(cpu, execute_qsi, &ep, 1);

	if (cb->qsi.cs)
		printk(KERN_EMERG "CPUMF sampling was not set inactive.\n");

	return rc;
}

static int smp_ctl_ssctl_enable_activate(int cpu, unsigned long interval)
{
	int rc;
	struct hws_execute_parms ep;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	cb->ssctl.h = 1;
	cb->ssctl.tear = cb->first_sdbt;
	cb->ssctl.dear = *(unsigned long *) cb->first_sdbt;
	cb->ssctl.interval = interval;
	cb->ssctl.es = 1;
	cb->ssctl.cs = 1;

	ep.buffer = &cb->ssctl;
	smp_call_function_single(cpu, execute_ssctl, &ep, 1);
	rc = ep.rc;
	if (rc)
		printk(KERN_ERR "hwsampler: CPU %d CPUMF SSCTL failed.\n", cpu);

	ep.buffer = &cb->qsi;
	smp_call_function_single(cpu, execute_qsi, &ep, 1);
	if (ep.rc)
		printk(KERN_ERR "hwsampler: CPU %d CPUMF QSI failed.\n", cpu);

	return rc;
}

static int smp_ctl_qsi(int cpu)
{
	struct hws_execute_parms ep;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	ep.buffer = &cb->qsi;
	smp_call_function_single(cpu, execute_qsi, &ep, 1);

	return ep.rc;
}

static inline unsigned long *trailer_entry_ptr(unsigned long v)
{
	void *ret;

	ret = (void *)v;
	ret += PAGE_SIZE;
	ret -= sizeof(struct hws_trailer_entry);

	return (unsigned long *) ret;
}

static void hws_ext_handler(struct ext_code ext_code,
			    unsigned int param32, unsigned long param64)
{
	struct hws_cpu_buffer *cb = &__get_cpu_var(sampler_cpu_buffer);

	if (!(param32 & CPU_MF_INT_SF_MASK))
		return;

	kstat_cpu(smp_processor_id()).irqs[EXTINT_CPM]++;
	atomic_xchg(&cb->ext_params, atomic_read(&cb->ext_params) | param32);

	if (hws_wq)
		queue_work(hws_wq, &cb->worker);
}

static void worker(struct work_struct *work);

static void add_samples_to_oprofile(unsigned cpu, unsigned long *,
				unsigned long *dear);

static void init_all_cpu_buffers(void)
{
	int cpu;
	struct hws_cpu_buffer *cb;

	for_each_online_cpu(cpu) {
		cb = &per_cpu(sampler_cpu_buffer, cpu);
		memset(cb, 0, sizeof(struct hws_cpu_buffer));
	}
}

static int is_link_entry(unsigned long *s)
{
	return *s & 0x1ul ? 1 : 0;
}

static unsigned long *get_next_sdbt(unsigned long *s)
{
	return (unsigned long *) (*s & ~0x1ul);
}

static int prepare_cpu_buffers(void)
{
	int cpu;
	int rc;
	struct hws_cpu_buffer *cb;

	rc = 0;
	for_each_online_cpu(cpu) {
		cb = &per_cpu(sampler_cpu_buffer, cpu);
		atomic_set(&cb->ext_params, 0);
		cb->worker_entry = 0;
		cb->sample_overflow = 0;
		cb->req_alert = 0;
		cb->incorrect_sdbt_entry = 0;
		cb->invalid_entry_address = 0;
		cb->loss_of_sample_data = 0;
		cb->sample_auth_change_alert = 0;
		cb->finish = 0;
		cb->oom = 0;
		cb->stop_mode = 0;
	}

	return rc;
}

/*
 * allocate_sdbt() - allocate sampler memory
 * @cpu: the cpu for which sampler memory is allocated
 *
 * A 4K page is allocated for each requested SDBT.
 * A maximum of 511 4K pages are allocated for the SDBs in each of the SDBTs.
 * Set ALERT_REQ mask in each SDBs trailer.
 * Returns zero if successful, <0 otherwise.
 */
static int allocate_sdbt(int cpu)
{
	int j, k, rc;
	unsigned long *sdbt;
	unsigned long  sdb;
	unsigned long *tail;
	unsigned long *trailer;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	if (cb->first_sdbt)
		return -EINVAL;

	sdbt = NULL;
	tail = sdbt;

	for (j = 0; j < num_sdbt; j++) {
		sdbt = (unsigned long *)get_zeroed_page(GFP_KERNEL);

		mutex_lock(&hws_sem_oom);
		/* OOM killer might have been activated */
		barrier();
		if (oom_killer_was_active || !sdbt) {
			if (sdbt)
				free_page((unsigned long)sdbt);

			goto allocate_sdbt_error;
		}
		if (cb->first_sdbt == 0)
			cb->first_sdbt = (unsigned long)sdbt;

		/* link current page to tail of chain */
		if (tail)
			*tail = (unsigned long)(void *)sdbt + 1;

		mutex_unlock(&hws_sem_oom);

		for (k = 0; k < num_sdb; k++) {
			/* get and set SDB page */
			sdb = get_zeroed_page(GFP_KERNEL);

			mutex_lock(&hws_sem_oom);
			/* OOM killer might have been activated */
			barrier();
			if (oom_killer_was_active || !sdb) {
				if (sdb)
					free_page(sdb);

				goto allocate_sdbt_error;
			}
			*sdbt = sdb;
			trailer = trailer_entry_ptr(*sdbt);
			*trailer = ALERT_REQ_MASK;
			sdbt++;
			mutex_unlock(&hws_sem_oom);
		}
		tail = sdbt;
	}
	mutex_lock(&hws_sem_oom);
	if (oom_killer_was_active)
		goto allocate_sdbt_error;

	rc = 0;
	if (tail)
		*tail = (unsigned long)
			((void *)cb->first_sdbt) + 1;

allocate_sdbt_exit:
	mutex_unlock(&hws_sem_oom);
	return rc;

allocate_sdbt_error:
	rc = -ENOMEM;
	goto allocate_sdbt_exit;
}

/*
 * deallocate_sdbt() - deallocate all sampler memory
 *
 * For each online CPU all SDBT trees are deallocated.
 * Returns the number of freed pages.
 */
static int deallocate_sdbt(void)
{
	int cpu;
	int counter;

	counter = 0;

	for_each_online_cpu(cpu) {
		unsigned long start;
		unsigned long sdbt;
		unsigned long *curr;
		struct hws_cpu_buffer *cb;

		cb = &per_cpu(sampler_cpu_buffer, cpu);

		if (!cb->first_sdbt)
			continue;

		sdbt = cb->first_sdbt;
		curr = (unsigned long *) sdbt;
		start = sdbt;

		/* we'll free the SDBT after all SDBs are processed... */
		while (1) {
			if (!*curr || !sdbt)
				break;

			/* watch for link entry reset if found */
			if (is_link_entry(curr)) {
				curr = get_next_sdbt(curr);
				if (sdbt)
					free_page(sdbt);

				/* we are done if we reach the start */
				if ((unsigned long) curr == start)
					break;
				else
					sdbt = (unsigned long) curr;
			} else {
				/* process SDB pointer */
				if (*curr) {
					free_page(*curr);
					curr++;
				}
			}
			counter++;
		}
		cb->first_sdbt = 0;
	}
	return counter;
}

static int start_sampling(int cpu)
{
	int rc;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);
	rc = smp_ctl_ssctl_enable_activate(cpu, interval);
	if (rc) {
		printk(KERN_INFO "hwsampler: CPU %d ssctl failed.\n", cpu);
		goto start_exit;
	}

	rc = -EINVAL;
	if (!cb->qsi.es) {
		printk(KERN_INFO "hwsampler: CPU %d ssctl not enabled.\n", cpu);
		goto start_exit;
	}

	if (!cb->qsi.cs) {
		printk(KERN_INFO "hwsampler: CPU %d ssctl not active.\n", cpu);
		goto start_exit;
	}

	printk(KERN_INFO
		"hwsampler: CPU %d, CPUMF Sampling started, interval %lu.\n",
		cpu, interval);

	rc = 0;

start_exit:
	return rc;
}

static int stop_sampling(int cpu)
{
	unsigned long v;
	int rc;
	struct hws_cpu_buffer *cb;

	rc = smp_ctl_qsi(cpu);
	WARN_ON(rc);

	cb = &per_cpu(sampler_cpu_buffer, cpu);
	if (!rc && !cb->qsi.es)
		printk(KERN_INFO "hwsampler: CPU %d, already stopped.\n", cpu);

	rc = smp_ctl_ssctl_stop(cpu);
	if (rc) {
		printk(KERN_INFO "hwsampler: CPU %d, ssctl stop error %d.\n",
				cpu, rc);
		goto stop_exit;
	}

	printk(KERN_INFO "hwsampler: CPU %d, CPUMF Sampling stopped.\n", cpu);

stop_exit:
	v = cb->req_alert;
	if (v)
		printk(KERN_ERR "hwsampler: CPU %d CPUMF Request alert,"
				" count=%lu.\n", cpu, v);

	v = cb->loss_of_sample_data;
	if (v)
		printk(KERN_ERR "hwsampler: CPU %d CPUMF Loss of sample data,"
				" count=%lu.\n", cpu, v);

	v = cb->invalid_entry_address;
	if (v)
		printk(KERN_ERR "hwsampler: CPU %d CPUMF Invalid entry address,"
				" count=%lu.\n", cpu, v);

	v = cb->incorrect_sdbt_entry;
	if (v)
		printk(KERN_ERR
				"hwsampler: CPU %d CPUMF Incorrect SDBT address,"
				" count=%lu.\n", cpu, v);

	v = cb->sample_auth_change_alert;
	if (v)
		printk(KERN_ERR
				"hwsampler: CPU %d CPUMF Sample authorization change,"
				" count=%lu.\n", cpu, v);

	return rc;
}

static int check_hardware_prerequisites(void)
{
	if (!test_facility(68))
		return -EOPNOTSUPP;
	return 0;
}
/*
 * hws_oom_callback() - the OOM callback function
 *
 * In case the callback is invoked during memory allocation for the
 *  hw sampler, all obtained memory is deallocated and a flag is set
 *  so main sampler memory allocation can exit with a failure code.
 * In case the callback is invoked during sampling the hw sampler
 *  is deactivated for all CPUs.
 */
static int hws_oom_callback(struct notifier_block *nfb,
	unsigned long dummy, void *parm)
{
	unsigned long *freed;
	int cpu;
	struct hws_cpu_buffer *cb;

	freed = parm;

	mutex_lock(&hws_sem_oom);

	if (hws_state == HWS_DEALLOCATED) {
		/* during memory allocation */
		if (oom_killer_was_active == 0) {
			oom_killer_was_active = 1;
			*freed += deallocate_sdbt();
		}
	} else {
		int i;
		cpu = get_cpu();
		cb = &per_cpu(sampler_cpu_buffer, cpu);

		if (!cb->oom) {
			for_each_online_cpu(i) {
				smp_ctl_ssctl_deactivate(i);
				cb->oom = 1;
			}
			cb->finish = 1;

			printk(KERN_INFO
				"hwsampler: CPU %d, OOM notify during CPUMF Sampling.\n",
				cpu);
		}
	}

	mutex_unlock(&hws_sem_oom);

	return NOTIFY_OK;
}

static struct notifier_block hws_oom_notifier = {
	.notifier_call = hws_oom_callback
};

static int hws_cpu_callback(struct notifier_block *nfb,
	unsigned long action, void *hcpu)
{
	/* We do not have sampler space available for all possible CPUs.
	   All CPUs should be online when hw sampling is activated. */
	return (hws_state <= HWS_DEALLOCATED) ? NOTIFY_OK : NOTIFY_BAD;
}

static struct notifier_block hws_cpu_notifier = {
	.notifier_call = hws_cpu_callback
};

/**
 * hwsampler_deactivate() - set hardware sampling temporarily inactive
 * @cpu:  specifies the CPU to be set inactive.
 *
 * Returns 0 on success, !0 on failure.
 */
int hwsampler_deactivate(unsigned int cpu)
{
	/*
	 * Deactivate hw sampling temporarily and flush the buffer
	 * by pushing all the pending samples to oprofile buffer.
	 *
	 * This function can be called under one of the following conditions:
	 *     Memory unmap, task is exiting.
	 */
	int rc;
	struct hws_cpu_buffer *cb;

	rc = 0;
	mutex_lock(&hws_sem);

	cb = &per_cpu(sampler_cpu_buffer, cpu);
	if (hws_state == HWS_STARTED) {
		rc = smp_ctl_qsi(cpu);
		WARN_ON(rc);
		if (cb->qsi.cs) {
			rc = smp_ctl_ssctl_deactivate(cpu);
			if (rc) {
				printk(KERN_INFO
				"hwsampler: CPU %d, CPUMF Deactivation failed.\n", cpu);
				cb->finish = 1;
				hws_state = HWS_STOPPING;
			} else  {
				hws_flush_all = 1;
				/* Add work to queue to read pending samples.*/
				queue_work_on(cpu, hws_wq, &cb->worker);
			}
		}
	}
	mutex_unlock(&hws_sem);

	if (hws_wq)
		flush_workqueue(hws_wq);

	return rc;
}

/**
 * hwsampler_activate() - activate/resume hardware sampling which was deactivated
 * @cpu:  specifies the CPU to be set active.
 *
 * Returns 0 on success, !0 on failure.
 */
int hwsampler_activate(unsigned int cpu)
{
	/*
	 * Re-activate hw sampling. This should be called in pair with
	 * hwsampler_deactivate().
	 */
	int rc;
	struct hws_cpu_buffer *cb;

	rc = 0;
	mutex_lock(&hws_sem);

	cb = &per_cpu(sampler_cpu_buffer, cpu);
	if (hws_state == HWS_STARTED) {
		rc = smp_ctl_qsi(cpu);
		WARN_ON(rc);
		if (!cb->qsi.cs) {
			hws_flush_all = 0;
			rc = smp_ctl_ssctl_enable_activate(cpu, interval);
			if (rc) {
				printk(KERN_ERR
				"CPU %d, CPUMF activate sampling failed.\n",
					 cpu);
			}
		}
	}

	mutex_unlock(&hws_sem);

	return rc;
}

static int check_qsi_on_setup(void)
{
	int rc;
	unsigned int cpu;
	struct hws_cpu_buffer *cb;

	for_each_online_cpu(cpu) {
		cb = &per_cpu(sampler_cpu_buffer, cpu);
		rc = smp_ctl_qsi(cpu);
		WARN_ON(rc);
		if (rc)
			return -EOPNOTSUPP;

		if (!cb->qsi.as) {
			printk(KERN_INFO "hwsampler: CPUMF sampling is not authorized.\n");
			return -EINVAL;
		}

		if (cb->qsi.es) {
			printk(KERN_WARNING "hwsampler: CPUMF is still enabled.\n");
			rc = smp_ctl_ssctl_stop(cpu);
			if (rc)
				return -EINVAL;

			printk(KERN_INFO
				"CPU %d, CPUMF Sampling stopped now.\n", cpu);
		}
	}
	return 0;
}

static int check_qsi_on_start(void)
{
	unsigned int cpu;
	int rc;
	struct hws_cpu_buffer *cb;

	for_each_online_cpu(cpu) {
		cb = &per_cpu(sampler_cpu_buffer, cpu);
		rc = smp_ctl_qsi(cpu);
		WARN_ON(rc);

		if (!cb->qsi.as)
			return -EINVAL;

		if (cb->qsi.es)
			return -EINVAL;

		if (cb->qsi.cs)
			return -EINVAL;
	}
	return 0;
}

static void worker_on_start(unsigned int cpu)
{
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);
	cb->worker_entry = cb->first_sdbt;
}

static int worker_check_error(unsigned int cpu, int ext_params)
{
	int rc;
	unsigned long *sdbt;
	struct hws_cpu_buffer *cb;

	rc = 0;
	cb = &per_cpu(sampler_cpu_buffer, cpu);
	sdbt = (unsigned long *) cb->worker_entry;

	if (!sdbt || !*sdbt)
		return -EINVAL;

	if (ext_params & CPU_MF_INT_SF_PRA)
		cb->req_alert++;

	if (ext_params & CPU_MF_INT_SF_LSDA)
		cb->loss_of_sample_data++;

	if (ext_params & CPU_MF_INT_SF_IAE) {
		cb->invalid_entry_address++;
		rc = -EINVAL;
	}

	if (ext_params & CPU_MF_INT_SF_ISE) {
		cb->incorrect_sdbt_entry++;
		rc = -EINVAL;
	}

	if (ext_params & CPU_MF_INT_SF_SACA) {
		cb->sample_auth_change_alert++;
		rc = -EINVAL;
	}

	return rc;
}

static void worker_on_finish(unsigned int cpu)
{
	int rc, i;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	if (cb->finish) {
		rc = smp_ctl_qsi(cpu);
		WARN_ON(rc);
		if (cb->qsi.es) {
			printk(KERN_INFO
				"hwsampler: CPU %d, CPUMF Stop/Deactivate sampling.\n",
				cpu);
			rc = smp_ctl_ssctl_stop(cpu);
			if (rc)
				printk(KERN_INFO
					"hwsampler: CPU %d, CPUMF Deactivation failed.\n",
					cpu);

			for_each_online_cpu(i) {
				if (i == cpu)
					continue;
				if (!cb->finish) {
					cb->finish = 1;
					queue_work_on(i, hws_wq,
						&cb->worker);
				}
			}
		}
	}
}

static void worker_on_interrupt(unsigned int cpu)
{
	unsigned long *sdbt;
	unsigned char done;
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	sdbt = (unsigned long *) cb->worker_entry;

	done = 0;
	/* do not proceed if stop was entered,
	 * forget the buffers not yet processed */
	while (!done && !cb->stop_mode) {
		unsigned long *trailer;
		struct hws_trailer_entry *te;
		unsigned long *dear = 0;

		trailer = trailer_entry_ptr(*sdbt);
		/* leave loop if no more work to do */
		if (!(*trailer & BUFFER_FULL_MASK)) {
			done = 1;
			if (!hws_flush_all)
				continue;
		}

		te = (struct hws_trailer_entry *)trailer;
		cb->sample_overflow += te->overflow;

		add_samples_to_oprofile(cpu, sdbt, dear);

		/* reset trailer */
		xchg((unsigned char *) te, 0x40);

		/* advance to next sdb slot in current sdbt */
		sdbt++;
		/* in case link bit is set use address w/o link bit */
		if (is_link_entry(sdbt))
			sdbt = get_next_sdbt(sdbt);

		cb->worker_entry = (unsigned long)sdbt;
	}
}

static void add_samples_to_oprofile(unsigned int cpu, unsigned long *sdbt,
		unsigned long *dear)
{
	struct hws_data_entry *sample_data_ptr;
	unsigned long *trailer;

	trailer = trailer_entry_ptr(*sdbt);
	if (dear) {
		if (dear > trailer)
			return;
		trailer = dear;
	}

	sample_data_ptr = (struct hws_data_entry *)(*sdbt);

	while ((unsigned long *)sample_data_ptr < trailer) {
		struct pt_regs *regs = NULL;
		struct task_struct *tsk = NULL;

		/*
		 * Check sampling mode, 1 indicates basic (=customer) sampling
		 * mode.
		 */
		if (sample_data_ptr->def != 1) {
			/* sample slot is not yet written */
			break;
		} else {
			/* make sure we don't use it twice,
			 * the next time the sampler will set it again */
			sample_data_ptr->def = 0;
		}

		/* Get pt_regs. */
		if (sample_data_ptr->P == 1) {
			/* userspace sample */
			unsigned int pid = sample_data_ptr->prim_asn;
			if (!counter_config.user)
				goto skip_sample;
			rcu_read_lock();
			tsk = pid_task(find_vpid(pid), PIDTYPE_PID);
			if (tsk)
				regs = task_pt_regs(tsk);
			rcu_read_unlock();
		} else {
			/* kernelspace sample */
			if (!counter_config.kernel)
				goto skip_sample;
			regs = task_pt_regs(current);
		}

		mutex_lock(&hws_sem);
		oprofile_add_ext_hw_sample(sample_data_ptr->ia, regs, 0,
				!sample_data_ptr->P, tsk);
		mutex_unlock(&hws_sem);
	skip_sample:
		sample_data_ptr++;
	}
}

static void worker(struct work_struct *work)
{
	unsigned int cpu;
	int ext_params;
	struct hws_cpu_buffer *cb;

	cb = container_of(work, struct hws_cpu_buffer, worker);
	cpu = smp_processor_id();
	ext_params = atomic_xchg(&cb->ext_params, 0);

	if (!cb->worker_entry)
		worker_on_start(cpu);

	if (worker_check_error(cpu, ext_params))
		return;

	if (!cb->finish)
		worker_on_interrupt(cpu);

	if (cb->finish)
		worker_on_finish(cpu);
}

/**
 * hwsampler_allocate() - allocate memory for the hardware sampler
 * @sdbt:  number of SDBTs per online CPU (must be > 0)
 * @sdb:   number of SDBs per SDBT (minimum 1, maximum 511)
 *
 * Returns 0 on success, !0 on failure.
 */
int hwsampler_allocate(unsigned long sdbt, unsigned long sdb)
{
	int cpu, rc;
	mutex_lock(&hws_sem);

	rc = -EINVAL;
	if (hws_state != HWS_DEALLOCATED)
		goto allocate_exit;

	if (sdbt < 1)
		goto allocate_exit;

	if (sdb > MAX_NUM_SDB || sdb < MIN_NUM_SDB)
		goto allocate_exit;

	num_sdbt = sdbt;
	num_sdb = sdb;

	oom_killer_was_active = 0;
	register_oom_notifier(&hws_oom_notifier);

	for_each_online_cpu(cpu) {
		if (allocate_sdbt(cpu)) {
			unregister_oom_notifier(&hws_oom_notifier);
			goto allocate_error;
		}
	}
	unregister_oom_notifier(&hws_oom_notifier);
	if (oom_killer_was_active)
		goto allocate_error;

	hws_state = HWS_STOPPED;
	rc = 0;

allocate_exit:
	mutex_unlock(&hws_sem);
	return rc;

allocate_error:
	rc = -ENOMEM;
	printk(KERN_ERR "hwsampler: CPUMF Memory allocation failed.\n");
	goto allocate_exit;
}

/**
 * hwsampler_deallocate() - deallocate hardware sampler memory
 *
 * Returns 0 on success, !0 on failure.
 */
int hwsampler_deallocate(void)
{
	int rc;

	mutex_lock(&hws_sem);

	rc = -EINVAL;
	if (hws_state != HWS_STOPPED)
		goto deallocate_exit;

	measurement_alert_subclass_unregister();
	deallocate_sdbt();

	hws_state = HWS_DEALLOCATED;
	rc = 0;

deallocate_exit:
	mutex_unlock(&hws_sem);

	return rc;
}

unsigned long hwsampler_query_min_interval(void)
{
	return min_sampler_rate;
}

unsigned long hwsampler_query_max_interval(void)
{
	return max_sampler_rate;
}

unsigned long hwsampler_get_sample_overflow_count(unsigned int cpu)
{
	struct hws_cpu_buffer *cb;

	cb = &per_cpu(sampler_cpu_buffer, cpu);

	return cb->sample_overflow;
}

int hwsampler_setup(void)
{
	int rc;
	int cpu;
	struct hws_cpu_buffer *cb;

	mutex_lock(&hws_sem);

	rc = -EINVAL;
	if (hws_state)
		goto setup_exit;

	hws_state = HWS_INIT;

	init_all_cpu_buffers();

	rc = check_hardware_prerequisites();
	if (rc)
		goto setup_exit;

	rc = check_qsi_on_setup();
	if (rc)
		goto setup_exit;

	rc = -EINVAL;
	hws_wq = create_workqueue("hwsampler");
	if (!hws_wq)
		goto setup_exit;

	register_cpu_notifier(&hws_cpu_notifier);

	for_each_online_cpu(cpu) {
		cb = &per_cpu(sampler_cpu_buffer, cpu);
		INIT_WORK(&cb->worker, worker);
		rc = smp_ctl_qsi(cpu);
		WARN_ON(rc);
		if (min_sampler_rate != cb->qsi.min_sampl_rate) {
			if (min_sampler_rate) {
				printk(KERN_WARNING
					"hwsampler: different min sampler rate values.\n");
				if (min_sampler_rate < cb->qsi.min_sampl_rate)
					min_sampler_rate =
						cb->qsi.min_sampl_rate;
			} else
				min_sampler_rate = cb->qsi.min_sampl_rate;
		}
		if (max_sampler_rate != cb->qsi.max_sampl_rate) {
			if (max_sampler_rate) {
				printk(KERN_WARNING
					"hwsampler: different max sampler rate values.\n");
				if (max_sampler_rate > cb->qsi.max_sampl_rate)
					max_sampler_rate =
						cb->qsi.max_sampl_rate;
			} else
				max_sampler_rate = cb->qsi.max_sampl_rate;
		}
	}
	register_external_interrupt(0x1407, hws_ext_handler);

	hws_state = HWS_DEALLOCATED;
	rc = 0;

setup_exit:
	mutex_unlock(&hws_sem);
	return rc;
}

int hwsampler_shutdown(void)
{
	int rc;

	mutex_lock(&hws_sem);

	rc = -EINVAL;
	if (hws_state == HWS_DEALLOCATED || hws_state == HWS_STOPPED) {
		mutex_unlock(&hws_sem);

		if (hws_wq)
			flush_workqueue(hws_wq);

		mutex_lock(&hws_sem);

		if (hws_state == HWS_STOPPED) {
			measurement_alert_subclass_unregister();
			deallocate_sdbt();
		}
		if (hws_wq) {
			destroy_workqueue(hws_wq);
			hws_wq = NULL;
		}

		unregister_external_interrupt(0x1407, hws_ext_handler);
		hws_state = HWS_INIT;
		rc = 0;
	}
	mutex_unlock(&hws_sem);

	unregister_cpu_notifier(&hws_cpu_notifier);

	return rc;
}

/**
 * hwsampler_start_all() - start hardware sampling on all online CPUs
 * @rate:  specifies the used interval when samples are taken
 *
 * Returns 0 on success, !0 on failure.
 */
int hwsampler_start_all(unsigned long rate)
{
	int rc, cpu;

	mutex_lock(&hws_sem);

	hws_oom = 0;

	rc = -EINVAL;
	if (hws_state != HWS_STOPPED)
		goto start_all_exit;

	interval = rate;

	/* fail if rate is not valid */
	if (interval < min_sampler_rate || interval > max_sampler_rate)
		goto start_all_exit;

	rc = check_qsi_on_start();
	if (rc)
		goto start_all_exit;

	rc = prepare_cpu_buffers();
	if (rc)
		goto start_all_exit;

	for_each_online_cpu(cpu) {
		rc = start_sampling(cpu);
		if (rc)
			break;
	}
	if (rc) {
		for_each_online_cpu(cpu) {
			stop_sampling(cpu);
		}
		goto start_all_exit;
	}
	hws_state = HWS_STARTED;
	rc = 0;

start_all_exit:
	mutex_unlock(&hws_sem);

	if (rc)
		return rc;

	register_oom_notifier(&hws_oom_notifier);
	hws_oom = 1;
	hws_flush_all = 0;
	/* now let them in, 1407 CPUMF external interrupts */
	measurement_alert_subclass_register();

	return 0;
}

/**
 * hwsampler_stop_all() - stop hardware sampling on all online CPUs
 *
 * Returns 0 on success, !0 on failure.
 */
int hwsampler_stop_all(void)
{
	int tmp_rc, rc, cpu;
	struct hws_cpu_buffer *cb;

	mutex_lock(&hws_sem);

	rc = 0;
	if (hws_state == HWS_INIT) {
		mutex_unlock(&hws_sem);
		return rc;
	}
	hws_state = HWS_STOPPING;
	mutex_unlock(&hws_sem);

	for_each_online_cpu(cpu) {
		cb = &per_cpu(sampler_cpu_buffer, cpu);
		cb->stop_mode = 1;
		tmp_rc = stop_sampling(cpu);
		if (tmp_rc)
			rc = tmp_rc;
	}

	if (hws_wq)
		flush_workqueue(hws_wq);

	mutex_lock(&hws_sem);
	if (hws_oom) {
		unregister_oom_notifier(&hws_oom_notifier);
		hws_oom = 0;
	}
	hws_state = HWS_STOPPED;
	mutex_unlock(&hws_sem);

	return rc;
}
