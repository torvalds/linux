/*
 *
 * (C) COPYRIGHT 2012-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#include <mali_kbase.h>
#include <mali_kbase_jm.h>
#include <mali_kbase_hwaccess_jm.h>

#define CREATE_TRACE_POINTS

#ifdef CONFIG_MALI_TRACE_TIMELINE
#include "mali_timeline.h"

#include <linux/debugfs.h>
#include <linux/seq_file.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_atoms_in_flight);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_atom);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_gpu_slot_active);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_gpu_slot_action);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_gpu_power_active);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_l2_power_active);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_pm_event);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_slot_atom);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_pm_checktrans);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_context_active);

struct kbase_trace_timeline_desc {
	char *enum_str;
	char *desc;
	char *format;
	char *format_desc;
};

static struct kbase_trace_timeline_desc kbase_trace_timeline_desc_table[] = {
	#define KBASE_TIMELINE_TRACE_CODE(enum_val, desc, format, format_desc) { #enum_val, desc, format, format_desc }
	#include "mali_kbase_trace_timeline_defs.h"
	#undef KBASE_TIMELINE_TRACE_CODE
};

#define KBASE_NR_TRACE_CODES ARRAY_SIZE(kbase_trace_timeline_desc_table)

static void *kbasep_trace_timeline_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= KBASE_NR_TRACE_CODES)
		return NULL;

	return &kbase_trace_timeline_desc_table[*pos];
}

static void kbasep_trace_timeline_seq_stop(struct seq_file *s, void *data)
{
}

static void *kbasep_trace_timeline_seq_next(struct seq_file *s, void *data, loff_t *pos)
{
	(*pos)++;

	if (*pos == KBASE_NR_TRACE_CODES)
		return NULL;

	return &kbase_trace_timeline_desc_table[*pos];
}

static int kbasep_trace_timeline_seq_show(struct seq_file *s, void *data)
{
	struct kbase_trace_timeline_desc *trace_desc = data;

	seq_printf(s, "%s#%s#%s#%s\n", trace_desc->enum_str, trace_desc->desc, trace_desc->format, trace_desc->format_desc);
	return 0;
}


static const struct seq_operations kbasep_trace_timeline_seq_ops = {
	.start = kbasep_trace_timeline_seq_start,
	.next = kbasep_trace_timeline_seq_next,
	.stop = kbasep_trace_timeline_seq_stop,
	.show = kbasep_trace_timeline_seq_show,
};

static int kbasep_trace_timeline_debugfs_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &kbasep_trace_timeline_seq_ops);
}

static const struct file_operations kbasep_trace_timeline_debugfs_fops = {
	.open = kbasep_trace_timeline_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

void kbasep_trace_timeline_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_file("mali_timeline_defs",
			S_IRUGO, kbdev->mali_debugfs_directory, NULL,
			&kbasep_trace_timeline_debugfs_fops);
}

void kbase_timeline_job_slot_submit(struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, int js)
{
	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);

	if (kbdev->timeline.slot_atoms_submitted[js] > 0) {
		KBASE_TIMELINE_JOB_START_NEXT(kctx, js, 1);
	} else {
		base_atom_id atom_number = kbase_jd_atom_id(kctx, katom);

		KBASE_TIMELINE_JOB_START_HEAD(kctx, js, 1);
		KBASE_TIMELINE_JOB_START(kctx, js, atom_number);
	}
	++kbdev->timeline.slot_atoms_submitted[js];

	KBASE_TIMELINE_ATOMS_SUBMITTED(kctx, js, kbdev->timeline.slot_atoms_submitted[js]);
}

void kbase_timeline_job_slot_done(struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, int js,
		kbasep_js_atom_done_code done_code)
{
	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);

	if (done_code & KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT) {
		KBASE_TIMELINE_JOB_START_NEXT(kctx, js, 0);
	} else {
		/* Job finished in JS_HEAD */
		base_atom_id atom_number = kbase_jd_atom_id(kctx, katom);

		KBASE_TIMELINE_JOB_START_HEAD(kctx, js, 0);
		KBASE_TIMELINE_JOB_STOP(kctx, js, atom_number);

		/* see if we need to trace the job in JS_NEXT moving to JS_HEAD */
		if (kbase_backend_nr_atoms_submitted(kbdev, js)) {
			struct kbase_jd_atom *next_katom;
			struct kbase_context *next_kctx;

			/* Peek the next atom - note that the atom in JS_HEAD will already
			 * have been dequeued */
			next_katom = kbase_backend_inspect_head(kbdev, js);
			WARN_ON(!next_katom);
			next_kctx = next_katom->kctx;
			KBASE_TIMELINE_JOB_START_NEXT(next_kctx, js, 0);
			KBASE_TIMELINE_JOB_START_HEAD(next_kctx, js, 1);
			KBASE_TIMELINE_JOB_START(next_kctx, js, kbase_jd_atom_id(next_kctx, next_katom));
		}
	}

	--kbdev->timeline.slot_atoms_submitted[js];

	KBASE_TIMELINE_ATOMS_SUBMITTED(kctx, js, kbdev->timeline.slot_atoms_submitted[js]);
}

void kbase_timeline_pm_send_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event_sent)
{
	int uid = 0;
	int old_uid;

	/* If a producer already exists for the event, try to use their UID (multiple-producers) */
	uid = atomic_read(&kbdev->timeline.pm_event_uid[event_sent]);
	old_uid = uid;

	/* Get a new non-zero UID if we don't have one yet */
	while (!uid)
		uid = atomic_inc_return(&kbdev->timeline.pm_event_uid_counter);

	/* Try to use this UID */
	if (old_uid != atomic_cmpxchg(&kbdev->timeline.pm_event_uid[event_sent], old_uid, uid))
		/* If it changed, raced with another producer: we've lost this UID */
		uid = 0;

	KBASE_TIMELINE_PM_SEND_EVENT(kbdev, event_sent, uid);
}

void kbase_timeline_pm_check_handle_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event)
{
	int uid = atomic_read(&kbdev->timeline.pm_event_uid[event]);

	if (uid != 0) {
		if (uid != atomic_cmpxchg(&kbdev->timeline.pm_event_uid[event], uid, 0))
			/* If it changed, raced with another consumer: we've lost this UID */
			uid = 0;

		KBASE_TIMELINE_PM_HANDLE_EVENT(kbdev, event, uid);
	}
}

void kbase_timeline_pm_handle_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event)
{
	int uid = atomic_read(&kbdev->timeline.pm_event_uid[event]);

	if (uid != atomic_cmpxchg(&kbdev->timeline.pm_event_uid[event], uid, 0))
		/* If it changed, raced with another consumer: we've lost this UID */
		uid = 0;

	KBASE_TIMELINE_PM_HANDLE_EVENT(kbdev, event, uid);
}

void kbase_timeline_pm_l2_transition_start(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->pm.power_change_lock);
	/* Simply log the start of the transition */
	kbdev->timeline.l2_transitioning = true;
	KBASE_TIMELINE_POWERING_L2(kbdev);
}

void kbase_timeline_pm_l2_transition_done(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->pm.power_change_lock);
	/* Simply log the end of the transition */
	if (kbdev->timeline.l2_transitioning) {
		kbdev->timeline.l2_transitioning = false;
		KBASE_TIMELINE_POWERED_L2(kbdev);
	}
}

#endif /* CONFIG_MALI_TRACE_TIMELINE */
