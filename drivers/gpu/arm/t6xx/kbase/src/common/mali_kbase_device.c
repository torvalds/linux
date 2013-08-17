/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

/**
 * @file mali_kbase_device.c
 * Base kernel device APIs
 */
#ifdef __KERNEL__
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_defs.h>
#include <kbase/src/common/mali_kbase_hw.h>

/* NOTE: Magic - 0x45435254 (TRCE in ASCII).
 * Supports tracing feature provided in the base module.
 * Please keep it in sync with the value of base module.
 */
#define TRACE_BUFFER_HEADER_SPECIAL 0x45435254

#ifdef CONFIG_MALI_PLATFORM_CONFIG_VEXPRESS
#ifdef CONFIG_MALI_PLATFORM_FAKE
extern kbase_attribute config_attributes_hw_issue_8408[];
#endif /* CONFIG_MALI_PLATFORM_FAKE */
#endif /* CONFIG_MALI_PLATFORM_CONFIG_VEXPRESS */


#if KBASE_TRACE_ENABLE != 0
STATIC CONST char *kbasep_trace_code_string[] =
{
	/* IMPORTANT: USE OF SPECIAL #INCLUDE OF NON-STANDARD HEADER FILE
	 * THIS MUST BE USED AT THE START OF THE ARRAY */
#define KBASE_TRACE_CODE_MAKE_CODE( X ) # X
#include "mali_kbase_trace_defs.h"
#undef  KBASE_TRACE_CODE_MAKE_CODE
};
#endif

STATIC mali_error kbasep_trace_init( kbase_device *kbdev );
STATIC void kbasep_trace_term( kbase_device *kbdev );
STATIC void kbasep_trace_hook_wrapper( void *param );
static void kbasep_trace_debugfs_init(kbase_device *kbdev);

STATIC mali_error kbasep_list_trace_init( kbase_device *kbdev );
STATIC void kbasep_list_trace_term( kbase_device *kbdev );

void kbasep_as_do_poke(struct work_struct *work);
enum hrtimer_restart kbasep_reset_timer_callback(struct hrtimer *data);
void kbasep_reset_timeout_worker(struct work_struct *data);

kbase_device *kbase_device_alloc(void)
{
	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return NULL;
	}
	else
	{
		return kzalloc(sizeof(kbase_device), GFP_KERNEL);
	}
}

mali_error kbase_device_init(kbase_device *kbdev)
{
	int i; /* i used after the for loop, don't reuse ! */

	spin_lock_init(&kbdev->mmu_mask_change);

	/* Initialize platform specific context */
	if(MALI_FALSE == kbasep_platform_device_init(kbdev))
	{
		goto fail;
	}

	/* Ensure we can access the GPU registers */
	kbase_pm_register_access_enable(kbdev);

	/* Get the list of workarounds for issues on the current HW (identified by the GPU_ID register) */
	if (MALI_ERROR_NONE != kbase_hw_set_issues_mask(kbdev))
	{
		kbase_pm_register_access_disable(kbdev);
		goto free_platform;
	}

	/* Find out GPU properties based on the GPU feature registers */
	kbase_gpuprops_set(kbdev);

	kbdev->nr_hw_address_spaces = kbdev->gpu_props.num_address_spaces;

	/* We're done accessing the GPU registers for now. */
	kbase_pm_register_access_disable(kbdev);

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++)
	{
		const char format[] = "mali_mmu%d";
		char name[sizeof(format)];
		const char poke_format[] = "mali_mmu%d_poker"; /* BASE_HW_ISSUE_8316 */
		char poke_name[sizeof(poke_format)]; /* BASE_HW_ISSUE_8316 */

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		{
			if (0 > osk_snprintf(poke_name, sizeof(poke_name), poke_format, i))
			{
				goto free_workqs;
			}
		}

		if (0 > osk_snprintf(name, sizeof(name), format, i))
		{
			goto free_workqs;
		}

		kbdev->as[i].number = i;
		kbdev->as[i].fault_addr = 0ULL;
		/* Simulate failure to create the workqueue */
		if(OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
		{
			kbdev->as[i].pf_wq = NULL;
			goto free_workqs;
		}
		kbdev->as[i].pf_wq = alloc_workqueue(name, 0, 1);
		if (NULL == kbdev->as[i].pf_wq)
		{
			goto free_workqs;
		}

		mutex_init(&kbdev->as[i].transaction_mutex);
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		{
			struct hrtimer * poking_timer = &kbdev->as[i].poke_timer;

			/* Simulate failure to create the workqueue */
			if(OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
			{
				kbdev->as[i].poke_wq = NULL;
				destroy_workqueue(kbdev->as[i].pf_wq);
				goto free_workqs;
			}
			kbdev->as[i].poke_wq = alloc_workqueue(poke_name, 0, 1);
			if (NULL == kbdev->as[i].poke_wq)
			{
				destroy_workqueue(kbdev->as[i].pf_wq);
				goto free_workqs;
			}
			OSK_ASSERT(0 == object_is_on_stack(&kbdev->as[i].poke_work));
			INIT_WORK(&kbdev->as[i].poke_work, kbasep_as_do_poke);

			hrtimer_init(poking_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

			poking_timer->function = kbasep_as_poke_timer_callback;

			atomic_set(&kbdev->as[i].poke_refcount, 0);
		}
	}
	/* don't change i after this point */

	spin_lock_init(&kbdev->hwcnt.lock);

	kbdev->hwcnt.state = KBASE_INSTR_STATE_DISABLED;
	init_waitqueue_head(&kbdev->reset_wait);
	init_waitqueue_head(&kbdev->hwcnt.wait);
	kbdev->hwcnt.triggered = 0;

	/* Simulate failure to create the workqueue */
	if(OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		kbdev->reset_workq = NULL;
		goto free_workqs;
	}

	kbdev->reset_workq = alloc_workqueue("Mali reset workqueue", 0, 1);
	if (NULL == kbdev->reset_workq)
	{
		goto free_workqs;
	}

	OSK_ASSERT(0 == object_is_on_stack(&kbdev->reset_work));
	INIT_WORK(&kbdev->reset_work, kbasep_reset_timeout_worker);

	hrtimer_init(&kbdev->reset_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kbdev->reset_timer.function=kbasep_reset_timer_callback;

	if (( kbasep_trace_init( kbdev ) != MALI_ERROR_NONE ) || (kbasep_list_trace_init (kbdev) != MALI_ERROR_NONE))
	{
		goto free_reset_workq;
	}

	osk_debug_assert_register_hook( &kbasep_trace_hook_wrapper, kbdev );

#ifdef CONFIG_MALI_PLATFORM_CONFIG_VEXPRESS
#ifdef CONFIG_MALI_PLATFORM_FAKE
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
	{
		/* BASE_HW_ISSUE_8408 requires a configuration with different timeouts for
		 * the vexpress platform */
		kbdev->config_attributes = config_attributes_hw_issue_8408;
	}
#endif /* CONFIG_MALI_PLATFORM_FAKE */
#endif /* CONFIG_MALI_PLATFORM_CONFIG_VEXPRESS */

	return MALI_ERROR_NONE;

free_reset_workq:
	destroy_workqueue(kbdev->reset_workq);
free_workqs:
	while (i > 0)
	{
		i--;
		destroy_workqueue(kbdev->as[i].pf_wq);
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		{
			destroy_workqueue(kbdev->as[i].poke_wq);
		}
	}
free_platform:
	kbasep_platform_device_term(kbdev);
fail:
	return MALI_ERROR_FUNCTION_FAILED;
}

void kbase_device_term(kbase_device *kbdev)
{
	int i;

	OSK_ASSERT(kbdev);
#if KBASE_TRACE_ENABLE != 0
	osk_debug_assert_register_hook( NULL, NULL );
#endif

	kbasep_trace_term( kbdev );
	kbasep_list_trace_term( kbdev );

	destroy_workqueue(kbdev->reset_workq);

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++)
	{
		destroy_workqueue(kbdev->as[i].pf_wq);
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		{
			destroy_workqueue(kbdev->as[i].poke_wq);
		}
	}

	kbasep_platform_device_term(kbdev);
}

void kbase_device_free(kbase_device *kbdev)
{
	kfree(kbdev);
}

void kbase_device_trace_buffer_install(kbase_context * kctx, u32 * tb, size_t size)
{
	unsigned long flags;
	OSK_ASSERT(kctx);
	OSK_ASSERT(tb);

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

void kbase_device_trace_buffer_uninstall(kbase_context * kctx)
{
	unsigned long flags;
	OSK_ASSERT(kctx);
	spin_lock_irqsave(&kctx->jctx.tb_lock, flags);
	kctx->jctx.tb = NULL;
	kctx->jctx.tb_wrap_offset = 0;
	spin_unlock_irqrestore(&kctx->jctx.tb_lock, flags);
}

void kbase_device_trace_register_access(kbase_context * kctx, kbase_reg_access_type type, u16 reg_offset, u32 reg_value)
{
	unsigned long flags;
	spin_lock_irqsave(&kctx->jctx.tb_lock, flags);
	if (kctx->jctx.tb)
	{
		u16 wrap_count;
		u16 write_offset;
		u32 * tb = kctx->jctx.tb;
		u32 header_word;

		header_word = tb[1];
		OSK_ASSERT(0 == (header_word & 0x1));

		wrap_count = (header_word >> 1) & 0x7FFF;
		write_offset = (header_word >> 16) & 0xFFFF;

		/* mark as transaction in progress */
		tb[1] |= 0x1;
		mb();

		/* calculate new offset */
		write_offset++;
		if (write_offset == kctx->jctx.tb_wrap_offset)
		{
			/* wrap */
			write_offset = 1;
			wrap_count++;
			wrap_count &= 0x7FFF; /* 15bit wrap counter */
		}

		/* store the trace entry at the selected offset */
		tb[write_offset * 2 + 0] = (reg_offset & ~0x3) | ((type == REG_WRITE) ? 0x1 : 0x0);
		tb[write_offset * 2 + 1] = reg_value;

		mb();

		/* new header word */
		header_word = (write_offset << 16) | (wrap_count << 1) | 0x0; /* transaction complete */
		tb[1] = header_word;
	}
	spin_unlock_irqrestore(&kctx->jctx.tb_lock, flags);
}

void kbase_reg_write(kbase_device *kbdev, u16 offset, u32 value, kbase_context * kctx)
{
	OSK_ASSERT(kbdev->pm.gpu_powered);
	OSK_ASSERT(kctx==NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	OSK_PRINT_INFO(OSK_BASE_CORE, "w: reg %04x val %08x", offset, value);
	kbase_os_reg_write(kbdev, offset, value);
	if (kctx && kctx->jctx.tb) kbase_device_trace_register_access(kctx, REG_WRITE, offset, value);
}
KBASE_EXPORT_TEST_API(kbase_reg_write)

u32 kbase_reg_read(kbase_device *kbdev, u16 offset, kbase_context * kctx)
{
	u32 val;
	OSK_ASSERT(kbdev->pm.gpu_powered);
	OSK_ASSERT(kctx==NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	val = kbase_os_reg_read(kbdev, offset);
	OSK_PRINT_INFO(OSK_BASE_CORE, "r: reg %04x val %08x", offset, val);
	if (kctx && kctx->jctx.tb) kbase_device_trace_register_access(kctx, REG_READ, offset, val);
	return val;
}
KBASE_EXPORT_TEST_API(kbase_reg_read)

void kbase_report_gpu_fault(kbase_device *kbdev, int multiple)
{
	u32 status;
	u64 address;

	status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS), NULL);
	address = (u64)kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTADDRESS_HI), NULL) << 32;
	address |= kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTADDRESS_LO), NULL);

	OSK_PRINT_WARN(OSK_BASE_CORE, "GPU Fault 0x08%x (%s) at 0x%016llx", status, kbase_exception_name(status), address);
	if (multiple)
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "There were multiple GPU faults - some have not been reported\n");
	}
}

void kbase_gpu_interrupt(kbase_device * kbdev, u32 val)
{
	KBASE_TRACE_ADD( kbdev, CORE_GPU_IRQ, NULL, NULL, 0u, val );
	if (val & GPU_FAULT)
	{
		kbase_report_gpu_fault(kbdev, val & MULTIPLE_GPU_FAULTS);
	}

	if (val & RESET_COMPLETED)
	{
		kbase_pm_reset_done(kbdev);
	}

	if (val & PRFCNT_SAMPLE_COMPLETED)
	{
		kbase_instr_hwcnt_sample_done(kbdev);
	}

	if (val & CLEAN_CACHES_COMPLETED)
	{
		kbase_clean_caches_done(kbdev);
	}

	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val, NULL);

	/* kbase_pm_check_transitions must be called after the IRQ has been cleared. This is because it might trigger
	 * further power transitions and we don't want to miss the interrupt raised to notify us that these further
	 * transitions have finished.
	 */
	if (val & POWER_CHANGED_ALL)
	{
		kbase_pm_check_transitions(kbdev);
	}
}


/*
 * Device trace functions
 */
#if KBASE_TRACE_ENABLE != 0

STATIC mali_error kbasep_trace_init( kbase_device *kbdev )
{
	void *rbuf;

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		rbuf = NULL;
	}
	else
	{
		rbuf = kmalloc(sizeof(kbase_trace)*KBASE_TRACE_SIZE, GFP_KERNEL);
	}

	if (!rbuf)
		return MALI_ERROR_FUNCTION_FAILED;

	kbdev->trace_rbuf = rbuf;
	spin_lock_init(&kbdev->trace_lock);
	kbasep_trace_debugfs_init(kbdev);
	return MALI_ERROR_NONE;
}

STATIC void kbasep_trace_term( kbase_device *kbdev )
{
	kfree( kbdev->trace_rbuf );
}

void kbasep_trace_format_msg(kbase_trace *trace_msg, char *buffer, int len)
{
	s32 written = 0;

	/* Initial part of message */
	written += MAX( osk_snprintf(buffer+written, MAX(len-written,0),
	                             "%d.%.6d,%d,%d,%s,%p,",
	                             trace_msg->timestamp.tv_sec,
	                             trace_msg->timestamp.tv_nsec / 1000,
	                             trace_msg->thread_id,
	                             trace_msg->cpu,
	                             kbasep_trace_code_string[trace_msg->code],
	                             trace_msg->ctx), 0);


	if (trace_msg->katom != NULL)
	{
		kbase_jd_atom *atom = trace_msg->katom;
		int atom_number = atom - &atom->kctx->jctx.atoms[0];
		written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
		                             "atom %d (ud: 0x%llx 0x%llx)",
		                             atom_number, atom->udata.blob[0], atom->udata.blob[1]), 0);
	}

	written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
	                             ",%.8llx,",
	                             trace_msg->gpu_addr ), 0 );
	/* NOTE: Could add function callbacks to handle different message types */
	if ( (trace_msg->flags & KBASE_TRACE_FLAG_JOBSLOT) != MALI_FALSE )
	{
		/* Jobslot present */
		written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
									 "%d", trace_msg->jobslot), 0 );
	}
	written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
								 ","), 0 );

	if ( (trace_msg->flags & KBASE_TRACE_FLAG_REFCOUNT) != MALI_FALSE )
	{
		/* Refcount present */
		written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
									 "%d", trace_msg->refcount), 0 );
	}
	written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
								 ",", trace_msg->jobslot), 0 );

	/* Rest of message */
	written += MAX( osk_snprintf(buffer+written, MAX((int)len-written,0),
								 "0x%.8x", trace_msg->info_val), 0 );

}

void kbasep_trace_dump_msg( kbase_trace *trace_msg )
{
	char buffer[OSK_DEBUG_MESSAGE_SIZE];

	kbasep_trace_format_msg(trace_msg, buffer, OSK_DEBUG_MESSAGE_SIZE);
	OSK_PRINT( OSK_BASE_CORE, "%s", buffer );
}

void kbasep_trace_add(kbase_device *kbdev, kbase_trace_code code, void *ctx, kbase_jd_atom *katom, u64 gpu_addr,
                      u8 flags, int refcount, int jobslot, u32 info_val )
{
	unsigned long irqflags;
	kbase_trace *trace_msg;

	spin_lock_irqsave( &kbdev->trace_lock, irqflags);

	trace_msg = &kbdev->trace_rbuf[kbdev->trace_next_in];

	/* Fill the message */
	osk_debug_get_thread_info( &trace_msg->thread_id, &trace_msg->cpu );

	getnstimeofday(&trace_msg->timestamp);

	trace_msg->code     = code;
	trace_msg->ctx      = ctx;
	trace_msg->katom    = katom;
	trace_msg->gpu_addr = gpu_addr;
	trace_msg->jobslot  = jobslot;
	trace_msg->refcount = MIN((unsigned int)refcount, 0xFF) ;
	trace_msg->info_val = info_val;
	trace_msg->flags    = flags;

	/* Update the ringbuffer indices */
	kbdev->trace_next_in = (kbdev->trace_next_in + 1) & KBASE_TRACE_MASK;
	if ( kbdev->trace_next_in == kbdev->trace_first_out )
	{
		kbdev->trace_first_out = (kbdev->trace_first_out + 1) & KBASE_TRACE_MASK;
	}

	/* Done */

	spin_unlock_irqrestore( &kbdev->trace_lock, irqflags);
}

void kbasep_trace_clear(kbase_device *kbdev)
{
	unsigned long flags;
	spin_lock_irqsave( &kbdev->trace_lock, flags);
	kbdev->trace_first_out = kbdev->trace_next_in;
	spin_unlock_irqrestore( &kbdev->trace_lock, flags);
}

void kbasep_trace_dump(kbase_device *kbdev)
{
	unsigned long flags;
	u32 start;
	u32 end;


	OSK_PRINT( OSK_BASE_CORE, "Dumping trace:\nsecs,nthread,cpu,code,ctx,katom,gpu_addr,jobslot,refcount,info_val");
	spin_lock_irqsave( &kbdev->trace_lock, flags);
	start = kbdev->trace_first_out;
	end = kbdev->trace_next_in;

	while (start != end)
	{
		kbase_trace *trace_msg = &kbdev->trace_rbuf[start];
		kbasep_trace_dump_msg( trace_msg );

		start = (start + 1) & KBASE_TRACE_MASK;
	}
	OSK_PRINT( OSK_BASE_CORE, "TRACE_END");

	spin_unlock_irqrestore( &kbdev->trace_lock, flags);

	KBASE_TRACE_CLEAR(kbdev);
}

STATIC void kbasep_trace_hook_wrapper( void *param )
{
	kbase_device *kbdev = (kbase_device*)param;
	kbasep_trace_dump( kbdev );
}

#ifdef CONFIG_DEBUG_FS
struct trace_seq_state {
	kbase_trace trace_buf[KBASE_TRACE_SIZE];;
	u32 start;
	u32 end;
};

void *kbasep_trace_seq_start(struct seq_file *s, loff_t *pos)
{
	struct trace_seq_state *state = s->private;

	if (*pos >= KBASE_TRACE_SIZE)
		return NULL;

	return state;
}

void kbasep_trace_seq_stop(struct seq_file *s, void *data)
{
}

void *kbasep_trace_seq_next(struct seq_file *s, void *data, loff_t *pos)
{
	struct trace_seq_state *state = s->private;
	int i = (state->start + *pos) & KBASE_TRACE_MASK;
	if (i == state->end)
		return NULL;

	(*pos)++;

	return &state->trace_buf[i];
}

int kbasep_trace_seq_show(struct seq_file *s, void *data)
{
	kbase_trace *trace_msg = data;
	char buffer[OSK_DEBUG_MESSAGE_SIZE];

	kbasep_trace_format_msg(trace_msg, buffer, OSK_DEBUG_MESSAGE_SIZE);
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
	unsigned long flags;
	kbase_device *kbdev = inode->i_private;
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
	.open           = kbasep_trace_debugfs_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release_private,
};

static void kbasep_trace_debugfs_init(kbase_device *kbdev)
{
	debugfs_create_file("mali_trace", S_IRUGO, NULL, kbdev,
			    &kbasep_trace_debugfs_fops);
}
#else
static void kbasep_trace_debugfs_init(kbase_device *kbdev)
{

}
#endif /* CONFIG_DEBUG_FS */

#else /* KBASE_TRACE_ENABLE != 0 */
STATIC mali_error kbasep_trace_init( kbase_device *kbdev )
{
	CSTD_UNUSED(kbdev);
	return MALI_ERROR_NONE;
}

STATIC void kbasep_trace_term( kbase_device *kbdev )
{
	CSTD_UNUSED(kbdev);
}

STATIC void kbasep_trace_hook_wrapper( void *param )
{
	CSTD_UNUSED(param);
}

void kbasep_trace_add(kbase_device *kbdev, kbase_trace_code code, void *ctx, kbase_jd_atom *katom, u64 gpu_addr,
                      u8 flags, int refcount, int jobslot, u32 info_val )
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

void kbasep_trace_clear(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_trace_dump(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}
#endif /* KBASE_TRACE_ENABLE != 0 */

#if KBASE_LIST_TRACE_ENABLE != 0

STATIC mali_error kbasep_list_trace_init(kbase_device *kbdev)
{
	void *rbuf;

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		rbuf = NULL;
	}
	else
	{
		rbuf = kmalloc(sizeof(kbase_list_trace)*KBASE_LIST_TRACE_SIZE, GFP_KERNEL);
	}

	if (!rbuf)
		return MALI_ERROR_FUNCTION_FAILED;

	kbdev->trace_lists_rbuf = rbuf;
	kbdev->trace_lists_next_in = 0;
	kbdev->trace_lists_first_out = 0;
	spin_lock_init(&kbdev->trace_lists_lock);
	return MALI_ERROR_NONE;
}

STATIC void kbasep_list_trace_term(kbase_device *kbdev)
{
	kfree( kbdev->trace_lists_rbuf );
}

void kbasep_list_trace_add(u8 tracepoint_id, kbase_device *kbdev, kbase_jd_atom *katom,
		osk_dlist *list_id, mali_bool action, u8 list_type)
{
	kbase_list_trace *trace_entry;
	unsigned long flags;

	spin_lock_irqsave( &kbdev->trace_lists_lock, flags);
	trace_entry = &kbdev->trace_lists_rbuf[kbdev->trace_lists_next_in];

	getnstimeofday(&trace_entry->timestamp);
	trace_entry->tracepoint_id = tracepoint_id;
	trace_entry->katom = katom;
	trace_entry->list_id = list_id;
	trace_entry->action = action;
	trace_entry->list_type = list_type;
	kbdev->trace_lists_next_in = (kbdev->trace_lists_next_in + 1) % KBASE_LIST_TRACE_SIZE;
	if ( kbdev->trace_lists_next_in == kbdev->trace_lists_first_out )
	{
		kbdev->trace_lists_first_out = (kbdev->trace_lists_first_out + 1) % KBASE_LIST_TRACE_SIZE;
	}
	spin_unlock_irqrestore( &kbdev->trace_lists_lock, flags);
}

void kbasep_list_trace_dump(kbase_device *kbdev)
{
	unsigned long flags;
	u32 start;
	u32 end;

	printk(KERN_ERR "**********************************************");
	printk(KERN_ERR "Dumping list trace:\n Timestamp(ns)\t| tracepoint_id\t| katom\t| list\t| action\t| list type");
	spin_lock_irqsave( &kbdev->trace_lists_lock, flags);
	start = kbdev->trace_lists_first_out;
	end = kbdev->trace_lists_next_in;

	while (start != end)
	{
		kbase_list_trace *trace_msg = &kbdev->trace_lists_rbuf[start];
		char list_type[20];
		char action[10];

		switch(trace_msg->list_type) {
			case KBASE_TRACE_LIST_DEP_HEAD_0:
				strcpy(list_type, "dep_head[0]");
				break;
			case KBASE_TRACE_LIST_COMPLETED_JOBS:
				strcpy(list_type, "completed_jobs");
				break;
			case KBASE_TRACE_LIST_RUNNABLE_JOBS:
				strcpy(list_type, "runnable_jobs");
				break;
			case KBASE_TRACE_LIST_WAITING_SOFT_JOBS:
				strcpy(list_type, "waiting_soft_jobs");
				break;
			case KBASE_TRACE_LIST_EVENT_LIST:
				strcpy(list_type, "event_list");
				break;
			default:
				strcpy(list_type, "undefined_list_type");
				break;
		}

		switch(trace_msg->action) {
			case KBASE_TRACE_LIST_ADD:
				strcpy(action, "add");
				break;
			case KBASE_TRACE_LIST_DEL:
				strcpy(action, "delete");
				break;
			default:
				strcpy(action, "invalid");
				break;
		}

		printk(KERN_ERR "| %d.%.6d\t| %u\t| %p\t| %p\t| %s\t| %s\t|", (int)trace_msg->timestamp.tv_sec,
                (int)(trace_msg->timestamp.tv_nsec / 1000),
                trace_msg->tracepoint_id,
                trace_msg->katom,
                trace_msg->list_id,
                action,
                list_type);

		start = (start + 1) & KBASE_TRACE_MASK;
	}
	printk(KERN_ERR "************ list trace end *****************");

	kbdev->trace_lists_first_out = kbdev->trace_lists_next_in;
	spin_unlock_irqrestore( &kbdev->trace_lists_lock, flags);
}



#else /*KBASE_LIST_TRACE_ENABLE != 0*/

STATIC mali_error kbasep_list_trace_init(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return MALI_ERROR_NONE;
}

STATIC void kbasep_list_trace_term(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_list_trace_add(u8 tracepoint_id, kbase_device *kbdev, kbase_jd_atom *katom,
		osk_dlist *list_id, mali_bool action, u8 list_type)
{
	CSTD_UNUSED(tracepoint_id);
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(katom);
	CSTD_UNUSED(list_id);
	CSTD_UNUSED(action);
	CSTD_UNUSED(list_type);
}

void kbasep_list_trace_dump(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}
#endif /*KBASE_LIST_TRACE_ENABLE != 0*/



