/*
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the default sampling buffer format
 * for the Linux/ia64 perfmon-2 subsystem.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>
#include <asm/delay.h>
#include <linux/smp.h>

#include <asm/perfmon.h>
#include <asm/perfmon_default_smpl.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("perfmon default sampling format");
MODULE_LICENSE("GPL");

#define DEFAULT_DEBUG 1

#ifdef DEFAULT_DEBUG
#define DPRINT(a) \
	do { \
		if (unlikely(pfm_sysctl.debug >0)) { printk("%s.%d: CPU%d ", __FUNCTION__, __LINE__, smp_processor_id()); printk a; } \
	} while (0)

#define DPRINT_ovfl(a) \
	do { \
		if (unlikely(pfm_sysctl.debug > 0 && pfm_sysctl.debug_ovfl >0)) { printk("%s.%d: CPU%d ", __FUNCTION__, __LINE__, smp_processor_id()); printk a; } \
	} while (0)

#else
#define DPRINT(a)
#define DPRINT_ovfl(a)
#endif

static int
default_validate(struct task_struct *task, unsigned int flags, int cpu, void *data)
{
	pfm_default_smpl_arg_t *arg = (pfm_default_smpl_arg_t*)data;
	int ret = 0;

	if (data == NULL) {
		DPRINT(("[%d] no argument passed\n", task->pid));
		return -EINVAL;
	}

	DPRINT(("[%d] validate flags=0x%x CPU%d\n", task->pid, flags, cpu));

	/*
	 * must hold at least the buffer header + one minimally sized entry
	 */
	if (arg->buf_size < PFM_DEFAULT_SMPL_MIN_BUF_SIZE) return -EINVAL;

	DPRINT(("buf_size=%lu\n", arg->buf_size));

	return ret;
}

static int
default_get_size(struct task_struct *task, unsigned int flags, int cpu, void *data, unsigned long *size)
{
	pfm_default_smpl_arg_t *arg = (pfm_default_smpl_arg_t *)data;

	/*
	 * size has been validated in default_validate
	 */
	*size = arg->buf_size;

	return 0;
}

static int
default_init(struct task_struct *task, void *buf, unsigned int flags, int cpu, void *data)
{
	pfm_default_smpl_hdr_t *hdr;
	pfm_default_smpl_arg_t *arg = (pfm_default_smpl_arg_t *)data;

	hdr = (pfm_default_smpl_hdr_t *)buf;

	hdr->hdr_version      = PFM_DEFAULT_SMPL_VERSION;
	hdr->hdr_buf_size     = arg->buf_size;
	hdr->hdr_cur_offs     = sizeof(*hdr);
	hdr->hdr_overflows    = 0UL;
	hdr->hdr_count        = 0UL;

	DPRINT(("[%d] buffer=%p buf_size=%lu hdr_size=%lu hdr_version=%u cur_offs=%lu\n",
		task->pid,
		buf,
		hdr->hdr_buf_size,
		sizeof(*hdr),
		hdr->hdr_version,
		hdr->hdr_cur_offs));

	return 0;
}

static int
default_handler(struct task_struct *task, void *buf, pfm_ovfl_arg_t *arg, struct pt_regs *regs, unsigned long stamp)
{
	pfm_default_smpl_hdr_t *hdr;
	pfm_default_smpl_entry_t *ent;
	void *cur, *last;
	unsigned long *e, entry_size;
	unsigned int npmds, i;
	unsigned char ovfl_pmd;
	unsigned char ovfl_notify;

	if (unlikely(buf == NULL || arg == NULL|| regs == NULL || task == NULL)) {
		DPRINT(("[%d] invalid arguments buf=%p arg=%p\n", task->pid, buf, arg));
		return -EINVAL;
	}

	hdr         = (pfm_default_smpl_hdr_t *)buf;
	cur         = buf+hdr->hdr_cur_offs;
	last        = buf+hdr->hdr_buf_size;
	ovfl_pmd    = arg->ovfl_pmd;
	ovfl_notify = arg->ovfl_notify;

	/*
	 * precheck for sanity
	 */
	if ((last - cur) < PFM_DEFAULT_MAX_ENTRY_SIZE) goto full;

	npmds = hweight64(arg->smpl_pmds[0]);

	ent = (pfm_default_smpl_entry_t *)cur;

	prefetch(arg->smpl_pmds_values);

	entry_size = sizeof(*ent) + (npmds << 3);

	/* position for first pmd */
	e = (unsigned long *)(ent+1);

	hdr->hdr_count++;

	DPRINT_ovfl(("[%d] count=%lu cur=%p last=%p free_bytes=%lu ovfl_pmd=%d ovfl_notify=%d npmds=%u\n",
			task->pid,
			hdr->hdr_count,
			cur, last,
			last-cur,
			ovfl_pmd,
			ovfl_notify, npmds));

	/*
	 * current = task running at the time of the overflow.
	 *
	 * per-task mode:
	 * 	- this is ususally the task being monitored.
	 * 	  Under certain conditions, it might be a different task
	 *
	 * system-wide:
	 * 	- this is not necessarily the task controlling the session
	 */
	ent->pid            = current->pid;
	ent->ovfl_pmd  	    = ovfl_pmd;
	ent->last_reset_val = arg->pmd_last_reset; //pmd[0].reg_last_reset_val;

	/*
	 * where did the fault happen (includes slot number)
	 */
	ent->ip = regs->cr_iip | ((regs->cr_ipsr >> 41) & 0x3);

	ent->tstamp    = stamp;
	ent->cpu       = smp_processor_id();
	ent->set       = arg->active_set;
	ent->tgid      = current->tgid;

	/*
	 * selectively store PMDs in increasing index number
	 */
	if (npmds) {
		unsigned long *val = arg->smpl_pmds_values;
		for(i=0; i < npmds; i++) {
			*e++ = *val++;
		}
	}

	/*
	 * update position for next entry
	 */
	hdr->hdr_cur_offs += entry_size;
	cur               += entry_size;

	/*
	 * post check to avoid losing the last sample
	 */
	if ((last - cur) < PFM_DEFAULT_MAX_ENTRY_SIZE) goto full;

	/*
	 * keep same ovfl_pmds, ovfl_notify
	 */
	arg->ovfl_ctrl.bits.notify_user     = 0;
	arg->ovfl_ctrl.bits.block_task      = 0;
	arg->ovfl_ctrl.bits.mask_monitoring = 0;
	arg->ovfl_ctrl.bits.reset_ovfl_pmds = 1; /* reset before returning from interrupt handler */

	return 0;
full:
	DPRINT_ovfl(("sampling buffer full free=%lu, count=%lu, ovfl_notify=%d\n", last-cur, hdr->hdr_count, ovfl_notify));

	/*
	 * increment number of buffer overflow.
	 * important to detect duplicate set of samples.
	 */
	hdr->hdr_overflows++;

	/*
	 * if no notification requested, then we saturate the buffer
	 */
	if (ovfl_notify == 0) {
		arg->ovfl_ctrl.bits.notify_user     = 0;
		arg->ovfl_ctrl.bits.block_task      = 0;
		arg->ovfl_ctrl.bits.mask_monitoring = 1;
		arg->ovfl_ctrl.bits.reset_ovfl_pmds = 0;
	} else {
		arg->ovfl_ctrl.bits.notify_user     = 1;
		arg->ovfl_ctrl.bits.block_task      = 1; /* ignored for non-blocking context */
		arg->ovfl_ctrl.bits.mask_monitoring = 1;
		arg->ovfl_ctrl.bits.reset_ovfl_pmds = 0; /* no reset now */
	}
	return -1; /* we are full, sorry */
}

static int
default_restart(struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs)
{
	pfm_default_smpl_hdr_t *hdr;

	hdr = (pfm_default_smpl_hdr_t *)buf;

	hdr->hdr_count    = 0UL;
	hdr->hdr_cur_offs = sizeof(*hdr);

	ctrl->bits.mask_monitoring = 0;
	ctrl->bits.reset_ovfl_pmds = 1; /* uses long-reset values */

	return 0;
}

static int
default_exit(struct task_struct *task, void *buf, struct pt_regs *regs)
{
	DPRINT(("[%d] exit(%p)\n", task->pid, buf));
	return 0;
}

static pfm_buffer_fmt_t default_fmt={
 	.fmt_name 	    = "default_format",
 	.fmt_uuid	    = PFM_DEFAULT_SMPL_UUID,
 	.fmt_arg_size	    = sizeof(pfm_default_smpl_arg_t),
 	.fmt_validate	    = default_validate,
 	.fmt_getsize	    = default_get_size,
 	.fmt_init	    = default_init,
 	.fmt_handler	    = default_handler,
 	.fmt_restart	    = default_restart,
 	.fmt_restart_active = default_restart,
 	.fmt_exit	    = default_exit,
};

static int __init
pfm_default_smpl_init_module(void)
{
	int ret;

	ret = pfm_register_buffer_fmt(&default_fmt);
	if (ret == 0) {
		printk("perfmon_default_smpl: %s v%u.%u registered\n",
			default_fmt.fmt_name,
			PFM_DEFAULT_SMPL_VERSION_MAJ,
			PFM_DEFAULT_SMPL_VERSION_MIN);
	} else {
		printk("perfmon_default_smpl: %s cannot register ret=%d\n",
			default_fmt.fmt_name,
			ret);
	}

	return ret;
}

static void __exit
pfm_default_smpl_cleanup_module(void)
{
	int ret;
	ret = pfm_unregister_buffer_fmt(default_fmt.fmt_uuid);

	printk("perfmon_default_smpl: unregister %s=%d\n", default_fmt.fmt_name, ret);
}

module_init(pfm_default_smpl_init_module);
module_exit(pfm_default_smpl_cleanup_module);

