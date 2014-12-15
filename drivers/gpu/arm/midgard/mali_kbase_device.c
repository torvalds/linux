/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_device.c
 * Base kernel device APIs
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_hw.h>

#include <mali_kbase_profiling_gator_api.h>

/* NOTE: Magic - 0x45435254 (TRCE in ASCII).
 * Supports tracing feature provided in the base module.
 * Please keep it in sync with the value of base module.
 */
#define TRACE_BUFFER_HEADER_SPECIAL 0x45435254

#if defined(CONFIG_MALI_PLATFORM_VEXPRESS) || defined(CONFIG_MALI_PLATFORM_VEXPRESS_VIRTEX7_40MHZ)
#ifdef CONFIG_MALI_PLATFORM_FAKE
extern struct kbase_attribute config_attributes_hw_issue_8408[];
#endif				/* CONFIG_MALI_PLATFORM_FAKE */
#endif				/* CONFIG_MALI_PLATFORM_VEXPRESS || CONFIG_MALI_PLATFORM_VEXPRESS_VIRTEX7_40MHZ */

#if KBASE_TRACE_ENABLE
STATIC CONST char *kbasep_trace_code_string[] = {
	/* IMPORTANT: USE OF SPECIAL #INCLUDE OF NON-STANDARD HEADER FILE
	 * THIS MUST BE USED AT THE START OF THE ARRAY */
#define KBASE_TRACE_CODE_MAKE_CODE(X) # X
#include "mali_kbase_trace_defs.h"
#undef  KBASE_TRACE_CODE_MAKE_CODE
};
#endif

#define DEBUG_MESSAGE_SIZE 256

STATIC mali_error kbasep_trace_init(struct kbase_device *kbdev);
STATIC void kbasep_trace_term(struct kbase_device *kbdev);
STATIC void kbasep_trace_hook_wrapper(void *param);
#if KBASE_TRACE_ENABLE
STATIC void kbasep_trace_debugfs_init(struct kbase_device *kbdev);
STATIC void kbasep_trace_debugfs_term(struct kbase_device *kbdev);
#endif

struct kbase_device *kbase_device_alloc(void)
{
	return kzalloc(sizeof(struct kbase_device), GFP_KERNEL);
}

mali_error kbase_device_init(struct kbase_device * const kbdev)
{
	int i;			/* i used after the for loop, don't reuse ! */

	spin_lock_init(&kbdev->mmu_mask_change);

	/* Initialize platform specific context */
	if (MALI_FALSE == kbasep_platform_device_init(kbdev))
		goto fail;

	/* Ensure we can access the GPU registers */
	kbase_pm_register_access_enable(kbdev);

	/* Find out GPU properties based on the GPU feature registers */
	kbase_gpuprops_set(kbdev);

	/* Get the list of workarounds for issues on the current HW (identified by the GPU_ID register) */
	if (MALI_ERROR_NONE != kbase_hw_set_issues_mask(kbdev)) {
		kbase_pm_register_access_disable(kbdev);
		goto free_platform;
	}
	/* Set the list of features available on the current HW (identified by the GPU_ID register) */
	kbase_hw_set_features_mask(kbdev);

#if defined(CONFIG_ARM64)
	set_dma_ops(kbdev->dev, &noncoherent_swiotlb_dma_ops);
#endif

	/* Workaround a pre-3.13 Linux issue, where dma_mask is NULL when our
	 * device structure was created by device-tree
	 */
	if (!kbdev->dev->dma_mask)
		kbdev->dev->dma_mask = &kbdev->dev->coherent_dma_mask;

	if (dma_set_mask(kbdev->dev,
			DMA_BIT_MASK(kbdev->gpu_props.mmu.pa_bits)))
		goto dma_set_mask_failed;

	if (dma_set_coherent_mask(kbdev->dev,
			DMA_BIT_MASK(kbdev->gpu_props.mmu.pa_bits)))
		goto dma_set_mask_failed;

	if (kbase_mem_lowlevel_init(kbdev))
		goto mem_lowlevel_init_failed;

	kbdev->nr_hw_address_spaces = kbdev->gpu_props.num_address_spaces;

	/* We're done accessing the GPU registers for now. */
	kbase_pm_register_access_disable(kbdev);

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		const char format[] = "mali_mmu%d";
		char name[sizeof(format)];
		const char poke_format[] = "mali_mmu%d_poker";	/* BASE_HW_ISSUE_8316 */
		char poke_name[sizeof(poke_format)];	/* BASE_HW_ISSUE_8316 */

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316)) {
			if (0 > snprintf(poke_name, sizeof(poke_name), poke_format, i))
				goto free_workqs;
		}

		if (0 > snprintf(name, sizeof(name), format, i))
			goto free_workqs;

		kbdev->as[i].number = i;
		kbdev->as[i].fault_addr = 0ULL;

		kbdev->as[i].pf_wq = alloc_workqueue(name, 0, 1);
		if (NULL == kbdev->as[i].pf_wq)
			goto free_workqs;

		mutex_init(&kbdev->as[i].transaction_mutex);

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316)) {
			struct hrtimer *poking_timer = &kbdev->as[i].poke_timer;

			kbdev->as[i].poke_wq = alloc_workqueue(poke_name, 0, 1);
			if (NULL == kbdev->as[i].poke_wq) {
				destroy_workqueue(kbdev->as[i].pf_wq);
				goto free_workqs;
			}
			KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&kbdev->as[i].poke_work));
			INIT_WORK(&kbdev->as[i].poke_work, kbasep_as_do_poke);

			hrtimer_init(poking_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

			poking_timer->function = kbasep_as_poke_timer_callback;

			kbdev->as[i].poke_refcount = 0;
			kbdev->as[i].poke_state = 0u;
		}
	}
	/* don't change i after this point */

	spin_lock_init(&kbdev->hwcnt.lock);

	kbdev->hwcnt.state = KBASE_INSTR_STATE_DISABLED;
	init_waitqueue_head(&kbdev->reset_wait);
	init_waitqueue_head(&kbdev->hwcnt.wait);
	init_waitqueue_head(&kbdev->hwcnt.cache_clean_wait);
	INIT_WORK(&kbdev->hwcnt.cache_clean_work, kbasep_cache_clean_worker);
	kbdev->hwcnt.triggered = 0;

	kbdev->hwcnt.cache_clean_wq = alloc_workqueue("Mali cache cleaning workqueue",
	                                              0, 1);
	if (NULL == kbdev->hwcnt.cache_clean_wq)
		goto free_workqs;

#if KBASE_GPU_RESET_EN
	kbdev->reset_workq = alloc_workqueue("Mali reset workqueue", 0, 1);
	if (NULL == kbdev->reset_workq)
		goto free_cache_clean_workq;

	KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&kbdev->reset_work));
	INIT_WORK(&kbdev->reset_work, kbasep_reset_timeout_worker);

	hrtimer_init(&kbdev->reset_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kbdev->reset_timer.function = kbasep_reset_timer_callback;

	if (kbasep_trace_init(kbdev) != MALI_ERROR_NONE)
		goto free_reset_workq;
#else
	if (kbasep_trace_init(kbdev) != MALI_ERROR_NONE)
		goto free_cache_clean_workq;
#endif /* KBASE_GPU_RESET_EN */

	mutex_init(&kbdev->cacheclean_lock);
	atomic_set(&kbdev->keep_gpu_powered_count, 0);

#ifdef CONFIG_MALI_TRACE_TIMELINE
	for (i = 0; i < BASE_JM_SUBMIT_SLOTS; ++i)
		kbdev->timeline.slot_atoms_submitted[i] = 0;

	for (i = 0; i <= KBASEP_TIMELINE_PM_EVENT_LAST; ++i)
		atomic_set(&kbdev->timeline.pm_event_uid[i], 0);
#endif /* CONFIG_MALI_TRACE_TIMELINE */

	/* fbdump profiling controls set to 0 - fbdump not enabled until changed by gator */
	for (i = 0; i < FBDUMP_CONTROL_MAX; i++)
		kbdev->kbase_profiling_controls[i] = 0;

		kbase_debug_assert_register_hook(&kbasep_trace_hook_wrapper, kbdev);

#if defined(CONFIG_MALI_PLATFORM_VEXPRESS) || defined(CONFIG_MALI_PLATFORM_VEXPRESS_VIRTEX7_40MHZ)
#ifdef CONFIG_MALI_PLATFORM_FAKE
	/* BASE_HW_ISSUE_8408 requires a configuration with different timeouts for
	 * the vexpress platform */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
		kbdev->config_attributes = config_attributes_hw_issue_8408;
#endif				/* CONFIG_MALI_PLATFORM_FAKE */
#endif				/* CONFIG_MALI_PLATFORM_VEXPRESS || CONFIG_MALI_PLATFORM_VEXPRESS_VIRTEX7_40MHZ */

	atomic_set(&kbdev->ctx_num, 0);

	return MALI_ERROR_NONE;
#if KBASE_GPU_RESET_EN
free_reset_workq:
	destroy_workqueue(kbdev->reset_workq);
#endif /* KBASE_GPU_RESET_EN */
free_cache_clean_workq:
	destroy_workqueue(kbdev->hwcnt.cache_clean_wq);
 free_workqs:
	while (i > 0) {
		i--;
		destroy_workqueue(kbdev->as[i].pf_wq);
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
			destroy_workqueue(kbdev->as[i].poke_wq);
	}
	kbase_mem_lowlevel_term(kbdev);
mem_lowlevel_init_failed:
dma_set_mask_failed:
free_platform:
	kbasep_platform_device_term(kbdev);
fail:
	return MALI_ERROR_FUNCTION_FAILED;
}

void kbase_device_term(struct kbase_device *kbdev)
{
	int i;

	KBASE_DEBUG_ASSERT(kbdev);

#if KBASE_TRACE_ENABLE
	kbase_debug_assert_register_hook(NULL, NULL);
#endif

	kbasep_trace_term(kbdev);

#if KBASE_GPU_RESET_EN
	destroy_workqueue(kbdev->reset_workq);
#endif

	destroy_workqueue(kbdev->hwcnt.cache_clean_wq);

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		destroy_workqueue(kbdev->as[i].pf_wq);
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
			destroy_workqueue(kbdev->as[i].poke_wq);
	}

	kbase_mem_lowlevel_term(kbdev);
	kbasep_platform_device_term(kbdev);
}

void kbase_device_free(struct kbase_device *kbdev)
{
	kfree(kbdev);
}

void kbase_device_trace_buffer_install(struct kbase_context *kctx, u32 *tb, size_t size)
{
	unsigned long flags;
	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(tb);

	/* set up the header */
	/* magic number in the first 4 bytes */
	tb[0] = TRACE_BUFFER_HEADER_SPECIAL;
	/* Store (write offset = 0, wrap counter = 0, transaction active = no)
	 * write offset 0 means never written.
	 * Offsets 1 to (wrap_offset - 1) used to store values when trace started
	 */
	tb[1] = 0;

	/* install trace buffer */
	spin_lock_irqsave(&kctx->jctx.tb_lock, flags);
	kctx->jctx.tb_wrap_offset = size / 8;
	kctx->jctx.tb = tb;
	spin_unlock_irqrestore(&kctx->jctx.tb_lock, flags);
}

void kbase_device_trace_buffer_uninstall(struct kbase_context *kctx)
{
	unsigned long flags;
	KBASE_DEBUG_ASSERT(kctx);
	spin_lock_irqsave(&kctx->jctx.tb_lock, flags);
	kctx->jctx.tb = NULL;
	kctx->jctx.tb_wrap_offset = 0;
	spin_unlock_irqrestore(&kctx->jctx.tb_lock, flags);
}

void kbase_device_trace_register_access(struct kbase_context *kctx, enum kbase_reg_access_type type, u16 reg_offset, u32 reg_value)
{
	unsigned long flags;
	spin_lock_irqsave(&kctx->jctx.tb_lock, flags);
	if (kctx->jctx.tb) {
		u16 wrap_count;
		u16 write_offset;
		u32 *tb = kctx->jctx.tb;
		u32 header_word;

		header_word = tb[1];
		KBASE_DEBUG_ASSERT(0 == (header_word & 0x1));

		wrap_count = (header_word >> 1) & 0x7FFF;
		write_offset = (header_word >> 16) & 0xFFFF;

		/* mark as transaction in progress */
		tb[1] |= 0x1;
		mb();

		/* calculate new offset */
		write_offset++;
		if (write_offset == kctx->jctx.tb_wrap_offset) {
			/* wrap */
			write_offset = 1;
			wrap_count++;
			wrap_count &= 0x7FFF;	/* 15bit wrap counter */
		}

		/* store the trace entry at the selected offset */
		tb[write_offset * 2 + 0] = (reg_offset & ~0x3) | ((type == REG_WRITE) ? 0x1 : 0x0);
		tb[write_offset * 2 + 1] = reg_value;
		mb();

		/* new header word */
		header_word = (write_offset << 16) | (wrap_count << 1) | 0x0;	/* transaction complete */
		tb[1] = header_word;
	}
	spin_unlock_irqrestore(&kctx->jctx.tb_lock, flags);
}

void kbase_reg_write(struct kbase_device *kbdev, u16 offset, u32 value, struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kbdev->pm.gpu_powered);
	KBASE_DEBUG_ASSERT(kctx == NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);
	dev_dbg(kbdev->dev, "w: reg %04x val %08x", offset, value);
	kbase_os_reg_write(kbdev, offset, value);
	if (kctx && kctx->jctx.tb)
		kbase_device_trace_register_access(kctx, REG_WRITE, offset, value);
}

KBASE_EXPORT_TEST_API(kbase_reg_write)

u32 kbase_reg_read(struct kbase_device *kbdev, u16 offset, struct kbase_context *kctx)
{
	u32 val;
	KBASE_DEBUG_ASSERT(kbdev->pm.gpu_powered);
	KBASE_DEBUG_ASSERT(kctx == NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);
	val = kbase_os_reg_read(kbdev, offset);
	dev_dbg(kbdev->dev, "r: reg %04x val %08x", offset, val);
	if (kctx && kctx->jctx.tb)
		kbase_device_trace_register_access(kctx, REG_READ, offset, val);
	return val;
}

KBASE_EXPORT_TEST_API(kbase_reg_read)

#if KBASE_PM_EN
void kbase_report_gpu_fault(struct kbase_device *kbdev, int multiple)
{
	u32 status;
	u64 address;

	status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS), NULL);
	address = (u64) kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTADDRESS_HI), NULL) << 32;
	address |= kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTADDRESS_LO), NULL);

	dev_warn(kbdev->dev, "GPU Fault 0x%08x (%s) at 0x%016llx", status & 0xFF, kbase_exception_name(status), address);
	if (multiple)
		dev_warn(kbdev->dev, "There were multiple GPU faults - some have not been reported\n");
}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, NULL, 0u, val);
	if (val & GPU_FAULT)
		kbase_report_gpu_fault(kbdev, val & MULTIPLE_GPU_FAULTS);

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	if (val & PRFCNT_SAMPLE_COMPLETED)
		kbase_instr_hwcnt_sample_done(kbdev);

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, NULL, 0u, val);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val, NULL);

	/* kbase_pm_check_transitions must be called after the IRQ has been cleared. This is because it might trigger
	 * further power transitions and we don't want to miss the interrupt raised to notify us that these further
	 * transitions have finished.
	 */
	if (val & POWER_CHANGED_ALL) {
		mali_bool cores_are_available;
		unsigned long flags;

		KBASE_TIMELINE_PM_CHECKTRANS(kbdev, SW_FLOW_PM_CHECKTRANS_GPU_INTERRUPT_START);
		spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);
		cores_are_available = kbase_pm_check_transitions_nolock(kbdev);
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
		KBASE_TIMELINE_PM_CHECKTRANS(kbdev, SW_FLOW_PM_CHECKTRANS_GPU_INTERRUPT_END);

		if (cores_are_available) {
			/* Fast-path Job Scheduling on PM IRQ */
			int js;
			/* Log timelining information that a change in state has completed */
			kbase_timeline_pm_handle_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);

			spin_lock_irqsave(&kbdev->js_data.runpool_irq.lock, flags);
			/* A simplified check to ensure the last context hasn't exited
			 * after dropping the PM lock whilst doing a PM IRQ: any bits set
			 * in 'submit_allowed' indicate that we have a context in the
			 * runpool (which can't leave whilst we hold this lock). It is
			 * sometimes zero even when we have a context in the runpool, but
			 * that's no problem because we'll be unable to submit jobs
			 * anyway */
			if (kbdev->js_data.runpool_irq.submit_allowed)
				for (js = 0; js < kbdev->gpu_props.num_job_slots; ++js) {
					mali_bool needs_retry;
					s8 submitted_count = 0;
					needs_retry = kbasep_js_try_run_next_job_on_slot_irq_nolock(kbdev, js, &submitted_count);
					/* Don't need to retry outside of IRQ context - this can
					 * only happen if we submitted too many in one IRQ, such
					 * that they were completing faster than we could
					 * submit. In this case, a job IRQ will fire to cause more
					 * work to be submitted in some way */
					CSTD_UNUSED(needs_retry);
				}
			spin_unlock_irqrestore(&kbdev->js_data.runpool_irq.lock, flags);
		}
	}
	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, NULL, 0u, val);
}
#endif  /* KBASE_PM_EN */
/*
 * Device trace functions
 */
#if KBASE_TRACE_ENABLE

STATIC mali_error kbasep_trace_init(struct kbase_device *kbdev)
{
	void *rbuf;

	rbuf = kmalloc(sizeof(struct kbase_trace) * KBASE_TRACE_SIZE, GFP_KERNEL);

	if (!rbuf)
		return MALI_ERROR_FUNCTION_FAILED;

	kbdev->trace_rbuf = rbuf;
	spin_lock_init(&kbdev->trace_lock);
	kbasep_trace_debugfs_init(kbdev);
	return MALI_ERROR_NONE;
}

STATIC void kbasep_trace_term(struct kbase_device *kbdev)
{
	kbasep_trace_debugfs_term(kbdev);
	kfree(kbdev->trace_rbuf);
}

static void kbasep_trace_format_msg(struct kbase_trace *trace_msg, char *buffer, int len)
{
	s32 written = 0;

	/* Initial part of message */
	written += MAX(snprintf(buffer + written, MAX(len - written, 0), "%d.%.6d,%d,%d,%s,%p,", (int)trace_msg->timestamp.tv_sec, (int)(trace_msg->timestamp.tv_nsec / 1000), trace_msg->thread_id, trace_msg->cpu, kbasep_trace_code_string[trace_msg->code], trace_msg->ctx), 0);

	if (trace_msg->katom != MALI_FALSE) {
		written += MAX(snprintf(buffer + written, MAX(len - written, 0), "atom %d (ud: 0x%llx 0x%llx)", trace_msg->atom_number, trace_msg->atom_udata[0], trace_msg->atom_udata[1]), 0);
	}

	written += MAX(snprintf(buffer + written, MAX(len - written, 0), ",%.8llx,", trace_msg->gpu_addr), 0);

	/* NOTE: Could add function callbacks to handle different message types */
	/* Jobslot present */
	if ((trace_msg->flags & KBASE_TRACE_FLAG_JOBSLOT) != MALI_FALSE)
		written += MAX(snprintf(buffer + written, MAX(len - written, 0), "%d", trace_msg->jobslot), 0);

	written += MAX(snprintf(buffer + written, MAX(len - written, 0), ","), 0);

	/* Refcount present */
	if ((trace_msg->flags & KBASE_TRACE_FLAG_REFCOUNT) != MALI_FALSE)
		written += MAX(snprintf(buffer + written, MAX(len - written, 0), "%d", trace_msg->refcount), 0);

	written += MAX(snprintf(buffer + written, MAX(len - written, 0), ","), 0);

	/* Rest of message */
	written += MAX(snprintf(buffer + written, MAX(len - written, 0), "0x%.8lx", trace_msg->info_val), 0);

}

static void kbasep_trace_dump_msg(struct kbase_device *kbdev, struct kbase_trace *trace_msg)
{
	char buffer[DEBUG_MESSAGE_SIZE];

	kbasep_trace_format_msg(trace_msg, buffer, DEBUG_MESSAGE_SIZE);
	dev_dbg(kbdev->dev, "%s", buffer);
}

void kbasep_trace_add(struct kbase_device *kbdev, enum kbase_trace_code code, void *ctx, struct kbase_jd_atom *katom, u64 gpu_addr, u8 flags, int refcount, int jobslot, unsigned long info_val)
{
	unsigned long irqflags;
	struct kbase_trace *trace_msg;

	spin_lock_irqsave(&kbdev->trace_lock, irqflags);

	trace_msg = &kbdev->trace_rbuf[kbdev->trace_next_in];

	/* Fill the message */
	trace_msg->thread_id = task_pid_nr(current);
	trace_msg->cpu = task_cpu(current);

	getnstimeofday(&trace_msg->timestamp);

	trace_msg->code = code;
	trace_msg->ctx = ctx;

	if (NULL == katom) {
		trace_msg->katom = MALI_FALSE;
	} else {
		trace_msg->katom = MALI_TRUE;
		trace_msg->atom_number = kbase_jd_atom_id(katom->kctx, katom);
		trace_msg->atom_udata[0] = katom->udata.blob[0];
		trace_msg->atom_udata[1] = katom->udata.blob[1];
	}

	trace_msg->gpu_addr = gpu_addr;
	trace_msg->jobslot = jobslot;
	trace_msg->refcount = MIN((unsigned int)refcount, 0xFF);
	trace_msg->info_val = info_val;
	trace_msg->flags = flags;

	/* Update the ringbuffer indices */
	kbdev->trace_next_in = (kbdev->trace_next_in + 1) & KBASE_TRACE_MASK;
	if (kbdev->trace_next_in == kbdev->trace_first_out)
		kbdev->trace_first_out = (kbdev->trace_first_out + 1) & KBASE_TRACE_MASK;

	/* Done */

	spin_unlock_irqrestore(&kbdev->trace_lock, irqflags);
}

void kbasep_trace_clear(struct kbase_device *kbdev)
{
	unsigned long flags;
	spin_lock_irqsave(&kbdev->trace_lock, flags);
	kbdev->trace_first_out = kbdev->trace_next_in;
	spin_unlock_irqrestore(&kbdev->trace_lock, flags);
}

void kbasep_trace_dump(struct kbase_device *kbdev)
{
	unsigned long flags;
	u32 start;
	u32 end;

	dev_dbg(kbdev->dev, "Dumping trace:\nsecs,nthread,cpu,code,ctx,katom,gpu_addr,jobslot,refcount,info_val");
	spin_lock_irqsave(&kbdev->trace_lock, flags);
	start = kbdev->trace_first_out;
	end = kbdev->trace_next_in;

	while (start != end) {
		struct kbase_trace *trace_msg = &kbdev->trace_rbuf[start];
		kbasep_trace_dump_msg(kbdev, trace_msg);

		start = (start + 1) & KBASE_TRACE_MASK;
	}
	dev_dbg(kbdev->dev, "TRACE_END");

	spin_unlock_irqrestore(&kbdev->trace_lock, flags);

	KBASE_TRACE_CLEAR(kbdev);
}

STATIC void kbasep_trace_hook_wrapper(void *param)
{
	struct kbase_device *kbdev = (struct kbase_device *)param;
	kbasep_trace_dump(kbdev);
}

#ifdef CONFIG_DEBUG_FS
struct trace_seq_state {
	struct kbase_trace trace_buf[KBASE_TRACE_SIZE];
	u32 start;
	u32 end;
};

static void *kbasep_trace_seq_start(struct seq_file *s, loff_t *pos)
{
	struct trace_seq_state *state = s->private;
	int i;

	if (*pos > KBASE_TRACE_SIZE)
		return NULL;
	i = state->start + *pos;
	if ((state->end >= state->start && i >= state->end) ||
			i >= state->end + KBASE_TRACE_SIZE)
		return NULL;

	i &= KBASE_TRACE_MASK;

	return &state->trace_buf[i];
}

static void kbasep_trace_seq_stop(struct seq_file *s, void *data)
{
}

static void *kbasep_trace_seq_next(struct seq_file *s, void *data, loff_t *pos)
{
	struct trace_seq_state *state = s->private;
	int i;

	(*pos)++;

	i = (state->start + *pos) & KBASE_TRACE_MASK;
	if (i == state->end)
		return NULL;

	return &state->trace_buf[i];
}

static int kbasep_trace_seq_show(struct seq_file *s, void *data)
{
	struct kbase_trace *trace_msg = data;
	char buffer[DEBUG_MESSAGE_SIZE];

	kbasep_trace_format_msg(trace_msg, buffer, DEBUG_MESSAGE_SIZE);
	seq_printf(s, "%s\n", buffer);
	return 0;
}

static const struct seq_operations kbasep_trace_seq_ops = {
	.start = kbasep_trace_seq_start,
	.next = kbasep_trace_seq_next,
	.stop = kbasep_trace_seq_stop,
	.show = kbasep_trace_seq_show,
};

static int kbasep_trace_debugfs_open(struct inode *inode, struct file *file)
{
	struct kbase_device *kbdev = inode->i_private;
	unsigned long flags;

	struct trace_seq_state *state;

	state = __seq_open_private(file, &kbasep_trace_seq_ops, sizeof(*state));
	if (!state)
		return -ENOMEM;

	spin_lock_irqsave(&kbdev->trace_lock, flags);
	state->start = kbdev->trace_first_out;
	state->end = kbdev->trace_next_in;
	memcpy(state->trace_buf, kbdev->trace_rbuf, sizeof(state->trace_buf));
	spin_unlock_irqrestore(&kbdev->trace_lock, flags);

	return 0;
}

static const struct file_operations kbasep_trace_debugfs_fops = {
	.open = kbasep_trace_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

STATIC void kbasep_trace_debugfs_init(struct kbase_device *kbdev)
{
	kbdev->trace_dentry = debugfs_create_file("mali_trace", S_IRUGO,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_trace_debugfs_fops);
}

STATIC void kbasep_trace_debugfs_term(struct kbase_device *kbdev)
{
	debugfs_remove(kbdev->trace_dentry);
	kbdev->trace_dentry = NULL;
}
#else
STATIC void kbasep_trace_debugfs_init(struct kbase_device *kbdev)
{

}
STATIC void kbasep_trace_debugfs_term(struct kbase_device *kbdev)
{

}
#endif				/* CONFIG_DEBUG_FS */

#else				/* KBASE_TRACE_ENABLE  */
STATIC mali_error kbasep_trace_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return MALI_ERROR_NONE;
}

STATIC void kbasep_trace_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

STATIC void kbasep_trace_hook_wrapper(void *param)
{
	CSTD_UNUSED(param);
}

void kbasep_trace_add(struct kbase_device *kbdev, enum kbase_trace_code code, void *ctx, struct kbase_jd_atom *katom, u64 gpu_addr, u8 flags, int refcount, int jobslot, unsigned long info_val)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(code);
	CSTD_UNUSED(ctx);
	CSTD_UNUSED(katom);
	CSTD_UNUSED(gpu_addr);
	CSTD_UNUSED(flags);
	CSTD_UNUSED(refcount);
	CSTD_UNUSED(jobslot);
	CSTD_UNUSED(info_val);
}

void kbasep_trace_clear(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_trace_dump(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}
#endif				/* KBASE_TRACE_ENABLE  */

void kbase_set_profiling_control(struct kbase_device *kbdev, u32 control, u32 value)
{
	switch (control) {
	case FBDUMP_CONTROL_ENABLE:
		/* fall through */
	case FBDUMP_CONTROL_RATE:
		/* fall through */
	case SW_COUNTER_ENABLE:
		/* fall through */
	case FBDUMP_CONTROL_RESIZE_FACTOR:
		kbdev->kbase_profiling_controls[control] = value;
		break;
	default:
		dev_err(kbdev->dev, "Profiling control %d not found\n", control);
		break;
	}
}

u32 kbase_get_profiling_control(struct kbase_device *kbdev, u32 control)
{
	u32 ret_value = 0;

	switch (control) {
	case FBDUMP_CONTROL_ENABLE:
		/* fall through */
	case FBDUMP_CONTROL_RATE:
		/* fall through */
	case SW_COUNTER_ENABLE:
		/* fall through */
	case FBDUMP_CONTROL_RESIZE_FACTOR:
		ret_value = kbdev->kbase_profiling_controls[control];
		break;
	default:
		dev_err(kbdev->dev, "Profiling control %d not found\n", control);
		break;
	}

	return ret_value;
}

/*
 * Called by gator to control the production of
 * profiling information at runtime
 * */

void _mali_profiling_control(u32 action, u32 value)
{
	struct kbase_device *kbdev = NULL;

	/* find the first i.e. call with -1 */
	kbdev = kbase_find_device(-1);

	if (NULL != kbdev) {
		kbase_set_profiling_control(kbdev, action, value);
	}
}

KBASE_EXPORT_SYMBOL(_mali_profiling_control);
