/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
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





/*
 * Base kernel device APIs
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_hwaccess_instr.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_config_defaults.h>

#include <mali_kbase_profiling_gator_api.h>

/* NOTE: Magic - 0x45435254 (TRCE in ASCII).
 * Supports tracing feature provided in the base module.
 * Please keep it in sync with the value of base module.
 */
#define TRACE_BUFFER_HEADER_SPECIAL 0x45435254

#if KBASE_TRACE_ENABLE
static const char *kbasep_trace_code_string[] = {
	/* IMPORTANT: USE OF SPECIAL #INCLUDE OF NON-STANDARD HEADER FILE
	 * THIS MUST BE USED AT THE START OF THE ARRAY */
#define KBASE_TRACE_CODE_MAKE_CODE(X) # X
#include "mali_kbase_trace_defs.h"
#undef  KBASE_TRACE_CODE_MAKE_CODE
};
#endif

#define DEBUG_MESSAGE_SIZE 256

static int kbasep_trace_init(struct kbase_device *kbdev);
static void kbasep_trace_term(struct kbase_device *kbdev);
static void kbasep_trace_hook_wrapper(void *param);

struct kbase_device *kbase_device_alloc(void)
{
	return kzalloc(sizeof(struct kbase_device), GFP_KERNEL);
}

static int kbase_device_as_init(struct kbase_device *kbdev, int i)
{
	const char format[] = "mali_mmu%d";
	char name[sizeof(format)];
	const char poke_format[] = "mali_mmu%d_poker";
	char poke_name[sizeof(poke_format)];

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		snprintf(poke_name, sizeof(poke_name), poke_format, i);

	snprintf(name, sizeof(name), format, i);

	kbdev->as[i].number = i;
	kbdev->as[i].fault_addr = 0ULL;

	kbdev->as[i].pf_wq = alloc_workqueue(name, 0, 1);
	if (!kbdev->as[i].pf_wq)
		return -EINVAL;

	INIT_WORK(&kbdev->as[i].work_pagefault, page_fault_worker);
	INIT_WORK(&kbdev->as[i].work_busfault, bus_fault_worker);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316)) {
		struct hrtimer *poke_timer = &kbdev->as[i].poke_timer;
		struct work_struct *poke_work = &kbdev->as[i].poke_work;

		kbdev->as[i].poke_wq = alloc_workqueue(poke_name, 0, 1);
		if (!kbdev->as[i].poke_wq) {
			destroy_workqueue(kbdev->as[i].pf_wq);
			return -EINVAL;
		}
		KBASE_DEBUG_ASSERT(!object_is_on_stack(poke_work));
		INIT_WORK(poke_work, kbasep_as_do_poke);

		hrtimer_init(poke_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

		poke_timer->function = kbasep_as_poke_timer_callback;

		kbdev->as[i].poke_refcount = 0;
		kbdev->as[i].poke_state = 0u;
	}

	return 0;
}

static void kbase_device_as_term(struct kbase_device *kbdev, int i)
{
	destroy_workqueue(kbdev->as[i].pf_wq);
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		destroy_workqueue(kbdev->as[i].poke_wq);
}

static int kbase_device_all_as_init(struct kbase_device *kbdev)
{
	int i, err;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		err = kbase_device_as_init(kbdev, i);
		if (err)
			goto free_workqs;
	}

	return 0;

free_workqs:
	for (; i > 0; i--)
		kbase_device_as_term(kbdev, i);

	return err;
}

static void kbase_device_all_as_term(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++)
		kbase_device_as_term(kbdev, i);
}

int kbase_device_init(struct kbase_device * const kbdev)
{
	int i, err;
#ifdef CONFIG_ARM64
	struct device_node *np = NULL;
#endif /* CONFIG_ARM64 */

	spin_lock_init(&kbdev->mmu_mask_change);
	mutex_init(&kbdev->mmu_hw_mutex);
#ifdef CONFIG_ARM64
	kbdev->cci_snoop_enabled = false;
	np = kbdev->dev->of_node;
	if (np != NULL) {
		if (of_property_read_u32(np, "snoop_enable_smc",
					&kbdev->snoop_enable_smc))
			kbdev->snoop_enable_smc = 0;
		if (of_property_read_u32(np, "snoop_disable_smc",
					&kbdev->snoop_disable_smc))
			kbdev->snoop_disable_smc = 0;
		/* Either both or none of the calls should be provided. */
		if (!((kbdev->snoop_disable_smc == 0
			&& kbdev->snoop_enable_smc == 0)
			|| (kbdev->snoop_disable_smc != 0
			&& kbdev->snoop_enable_smc != 0))) {
			WARN_ON(1);
			err = -EINVAL;
			goto fail;
		}
	}
#endif /* CONFIG_ARM64 */
	/* Get the list of workarounds for issues on the current HW
	 * (identified by the GPU_ID register)
	 */
	err = kbase_hw_set_issues_mask(kbdev);
	if (err)
		goto fail;

	/* Set the list of features available on the current HW
	 * (identified by the GPU_ID register)
	 */
	kbase_hw_set_features_mask(kbdev);

	kbase_gpuprops_set_features(kbdev);

	/* On Linux 4.0+, dma coherency is determined from device tree */
#if defined(CONFIG_ARM64) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
	set_dma_ops(kbdev->dev, &noncoherent_swiotlb_dma_ops);
#endif

	/* Workaround a pre-3.13 Linux issue, where dma_mask is NULL when our
	 * device structure was created by device-tree
	 */
	if (!kbdev->dev->dma_mask)
		kbdev->dev->dma_mask = &kbdev->dev->coherent_dma_mask;

	err = dma_set_mask(kbdev->dev,
			DMA_BIT_MASK(kbdev->gpu_props.mmu.pa_bits));
	if (err)
		goto dma_set_mask_failed;

	err = dma_set_coherent_mask(kbdev->dev,
			DMA_BIT_MASK(kbdev->gpu_props.mmu.pa_bits));
	if (err)
		goto dma_set_mask_failed;

	kbdev->nr_hw_address_spaces = kbdev->gpu_props.num_address_spaces;

	err = kbase_device_all_as_init(kbdev);
	if (err)
		goto as_init_failed;

	spin_lock_init(&kbdev->hwcnt.lock);

	err = kbasep_trace_init(kbdev);
	if (err)
		goto term_as;

	mutex_init(&kbdev->cacheclean_lock);

#ifdef CONFIG_MALI_BIFROST_TRACE_TIMELINE
	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; ++i)
		kbdev->timeline.slot_atoms_submitted[i] = 0;

	for (i = 0; i <= KBASEP_TIMELINE_PM_EVENT_LAST; ++i)
		atomic_set(&kbdev->timeline.pm_event_uid[i], 0);
#endif /* CONFIG_MALI_BIFROST_TRACE_TIMELINE */

	/* fbdump profiling controls set to 0 - fbdump not enabled until changed by gator */
	for (i = 0; i < FBDUMP_CONTROL_MAX; i++)
		kbdev->kbase_profiling_controls[i] = 0;

	kbase_debug_assert_register_hook(&kbasep_trace_hook_wrapper, kbdev);

	atomic_set(&kbdev->ctx_num, 0);

	err = kbase_instr_backend_init(kbdev);
	if (err)
		goto term_trace;

	kbdev->pm.dvfs_period = DEFAULT_PM_DVFS_PERIOD;

	kbdev->reset_timeout_ms = DEFAULT_RESET_TIMEOUT_MS;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
		kbdev->mmu_mode = kbase_mmu_mode_get_aarch64();
	else
		kbdev->mmu_mode = kbase_mmu_mode_get_lpae();

#ifdef CONFIG_MALI_BIFROST_DEBUG
	init_waitqueue_head(&kbdev->driver_inactive_wait);
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	return 0;
term_trace:
	kbasep_trace_term(kbdev);
term_as:
	kbase_device_all_as_term(kbdev);
as_init_failed:
dma_set_mask_failed:
fail:
	return err;
}

void kbase_device_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev);

#if KBASE_TRACE_ENABLE
	kbase_debug_assert_register_hook(NULL, NULL);
#endif

	kbase_instr_backend_term(kbdev);

	kbasep_trace_term(kbdev);

	kbase_device_all_as_term(kbdev);
}

void kbase_device_free(struct kbase_device *kbdev)
{
	kfree(kbdev);
}

int kbase_device_trace_buffer_install(
		struct kbase_context *kctx, u32 *tb, size_t size)
{
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(tb);

	/* Interface uses 16-bit value to track last accessed entry. Each entry
	 * is composed of two 32-bit words.
	 * This limits the size that can be handled without an overflow. */
	if (0xFFFF * (2 * sizeof(u32)) < size)
		return -EINVAL;

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

	return 0;
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

/*
 * Device trace functions
 */
#if KBASE_TRACE_ENABLE

static int kbasep_trace_init(struct kbase_device *kbdev)
{
	struct kbase_trace *rbuf;

	rbuf = kmalloc_array(KBASE_TRACE_SIZE, sizeof(*rbuf), GFP_KERNEL);

	if (!rbuf)
		return -EINVAL;

	kbdev->trace_rbuf = rbuf;
	spin_lock_init(&kbdev->trace_lock);
	return 0;
}

static void kbasep_trace_term(struct kbase_device *kbdev)
{
	kfree(kbdev->trace_rbuf);
}

static void kbasep_trace_format_msg(struct kbase_trace *trace_msg, char *buffer, int len)
{
	s32 written = 0;

	/* Initial part of message */
	written += MAX(snprintf(buffer + written, MAX(len - written, 0), "%d.%.6d,%d,%d,%s,%p,", (int)trace_msg->timestamp.tv_sec, (int)(trace_msg->timestamp.tv_nsec / 1000), trace_msg->thread_id, trace_msg->cpu, kbasep_trace_code_string[trace_msg->code], trace_msg->ctx), 0);

	if (trace_msg->katom)
		written += MAX(snprintf(buffer + written, MAX(len - written, 0), "atom %d (ud: 0x%llx 0x%llx)", trace_msg->atom_number, trace_msg->atom_udata[0], trace_msg->atom_udata[1]), 0);

	written += MAX(snprintf(buffer + written, MAX(len - written, 0), ",%.8llx,", trace_msg->gpu_addr), 0);

	/* NOTE: Could add function callbacks to handle different message types */
	/* Jobslot present */
	if (trace_msg->flags & KBASE_TRACE_FLAG_JOBSLOT)
		written += MAX(snprintf(buffer + written, MAX(len - written, 0), "%d", trace_msg->jobslot), 0);

	written += MAX(snprintf(buffer + written, MAX(len - written, 0), ","), 0);

	/* Refcount present */
	if (trace_msg->flags & KBASE_TRACE_FLAG_REFCOUNT)
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
		trace_msg->katom = false;
	} else {
		trace_msg->katom = true;
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

static void kbasep_trace_hook_wrapper(void *param)
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

void kbasep_trace_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_file("mali_trace", S_IRUGO,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_trace_debugfs_fops);
}

#else
void kbasep_trace_debugfs_init(struct kbase_device *kbdev)
{
}
#endif				/* CONFIG_DEBUG_FS */

#else				/* KBASE_TRACE_ENABLE  */
static int kbasep_trace_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return 0;
}

static void kbasep_trace_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void kbasep_trace_hook_wrapper(void *param)
{
	CSTD_UNUSED(param);
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

/*
 * Called by gator to control the production of
 * profiling information at runtime
 * */

void _mali_profiling_control(u32 action, u32 value)
{
	struct kbase_device *kbdev = NULL;

	/* find the first i.e. call with -1 */
	kbdev = kbase_find_device(-1);

	if (NULL != kbdev)
		kbase_set_profiling_control(kbdev, action, value);
}
KBASE_EXPORT_SYMBOL(_mali_profiling_control);

