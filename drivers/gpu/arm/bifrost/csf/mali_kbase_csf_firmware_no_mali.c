// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include "mali_kbase.h"
#include "mali_kbase_csf_firmware.h"
#include "mali_kbase_csf_trace_buffer.h"
#include "mali_kbase_csf_timeout.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_reset_gpu.h"
#include "mali_kbase_ctx_sched.h"
#include "device/mali_kbase_device.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "mali_kbase_csf_scheduler.h"
#include "mmu/mali_kbase_mmu.h"
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/mutex.h>
#if (KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE)
#include <linux/set_memory.h>
#endif
#include <asm/arch_timer.h>

#ifdef CONFIG_MALI_BIFROST_DEBUG
/* Makes Driver wait indefinitely for an acknowledgment for the different
 * requests it sends to firmware. Otherwise the timeouts interfere with the
 * use of debugger for source-level debugging of firmware as Driver initiates
 * a GPU reset when a request times out, which always happen when a debugger
 * is connected.
 */
bool fw_debug; /* Default value of 0/false */
module_param(fw_debug, bool, 0444);
MODULE_PARM_DESC(fw_debug,
	"Enables effective use of a debugger for debugging firmware code.");
#endif

#define DUMMY_FW_PAGE_SIZE SZ_4K

/**
 * struct dummy_firmware_csi - Represents a dummy interface for MCU firmware CSs
 *
 * @cs_kernel_input:  CS kernel input memory region
 * @cs_kernel_output: CS kernel output memory region
 */
struct dummy_firmware_csi {
	u8 cs_kernel_input[DUMMY_FW_PAGE_SIZE];
	u8 cs_kernel_output[DUMMY_FW_PAGE_SIZE];
};

/**
 * struct dummy_firmware_csg - Represents a dummy interface for MCU firmware CSGs
 *
 * @csg_input:  CSG kernel input memory region
 * @csg_output: CSG kernel output memory region
 * @csi:               Dummy firmware CSIs
 */
struct dummy_firmware_csg {
	u8 csg_input[DUMMY_FW_PAGE_SIZE];
	u8 csg_output[DUMMY_FW_PAGE_SIZE];
	struct dummy_firmware_csi csi[8];
} dummy_firmware_csg;

/**
 * struct dummy_firmware_interface - Represents a dummy interface in the MCU firmware
 *
 * @global_input:  Global input memory region
 * @global_output: Global output memory region
 * @csg:   Dummy firmware CSGs
 * @node:  Interface objects are on the kbase_device:csf.firmware_interfaces
 *         list using this list_head to link them
 */
struct dummy_firmware_interface {
	u8 global_input[DUMMY_FW_PAGE_SIZE];
	u8 global_output[DUMMY_FW_PAGE_SIZE];
	struct dummy_firmware_csg csg[8];
	struct list_head node;
} dummy_firmware_interface;

#define CSF_GLB_REQ_CFG_MASK                                                   \
	(GLB_REQ_CFG_ALLOC_EN_MASK | GLB_REQ_CFG_PROGRESS_TIMER_MASK |         \
	 GLB_REQ_CFG_PWROFF_TIMER_MASK)

static inline u32 input_page_read(const u32 *const input, const u32 offset)
{
	WARN_ON(offset % sizeof(u32));

	return input[offset / sizeof(u32)];
}

static inline void input_page_write(u32 *const input, const u32 offset,
			const u32 value)
{
	WARN_ON(offset % sizeof(u32));

	input[offset / sizeof(u32)] = value;
}

static inline void input_page_partial_write(u32 *const input, const u32 offset,
			u32 value, u32 mask)
{
	WARN_ON(offset % sizeof(u32));

	input[offset / sizeof(u32)] =
		(input_page_read(input, offset) & ~mask) | (value & mask);
}

static inline u32 output_page_read(const u32 *const output, const u32 offset)
{
	WARN_ON(offset % sizeof(u32));

	return output[offset / sizeof(u32)];
}

static inline void output_page_write(u32 *const output, const u32 offset,
			const u32 value)
{
	WARN_ON(offset % sizeof(u32));

	output[offset / sizeof(u32)] = value;
}

/**
 * invent_memory_setup_entry() - Invent an "interface memory setup" section
 *
 * Invent an "interface memory setup" section similar to one from a firmware
 * image. If successful the interface will be added to the
 * kbase_device:csf.firmware_interfaces list.
 *
 * Return: 0 if successful, negative error code on failure
 *
 * @kbdev: Kbase device structure
 */
static int invent_memory_setup_entry(struct kbase_device *kbdev)
{
	struct dummy_firmware_interface *interface = NULL;

	/* Allocate enough memory for the struct dummy_firmware_interface.
	 */
	interface = kzalloc(sizeof(*interface), GFP_KERNEL);
	if (!interface)
		return -ENOMEM;

	kbdev->csf.shared_interface = interface;
	list_add(&interface->node, &kbdev->csf.firmware_interfaces);

	/* NO_MALI: Don't insert any firmware pages */
	return 0;
}

static void free_global_iface(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *iface = &kbdev->csf.global_iface;

	if (iface->groups) {
		unsigned int gid;

		for (gid = 0; gid < iface->group_num; ++gid)
			kfree(iface->groups[gid].streams);

		kfree(iface->groups);
		iface->groups = NULL;
	}
}

static int invent_cmd_stream_group_info(struct kbase_device *kbdev,
		struct kbase_csf_cmd_stream_group_info *ginfo,
		struct dummy_firmware_csg *csg)
{
	unsigned int sid;

	ginfo->input = csg->csg_input;
	ginfo->output = csg->csg_output;

	ginfo->kbdev = kbdev;
	ginfo->features = 0;
	ginfo->suspend_size = 64;
	ginfo->protm_suspend_size = 64;
	ginfo->stream_num = ARRAY_SIZE(csg->csi);
	ginfo->stream_stride = 0;

	ginfo->streams = kcalloc(ginfo->stream_num, sizeof(*ginfo->streams), GFP_KERNEL);
	if (ginfo->streams == NULL) {
		return -ENOMEM;
	}

	for (sid = 0; sid < ginfo->stream_num; ++sid) {
		struct kbase_csf_cmd_stream_info *stream = &ginfo->streams[sid];
		struct dummy_firmware_csi *csi = &csg->csi[sid];

		stream->input = csi->cs_kernel_input;
		stream->output = csi->cs_kernel_output;

		stream->kbdev = kbdev;
		stream->features =
			STREAM_FEATURES_WORK_REGISTERS_SET(0, 80) |
			STREAM_FEATURES_SCOREBOARDS_SET(0, 8) |
			STREAM_FEATURES_COMPUTE_SET(0, 1) |
			STREAM_FEATURES_FRAGMENT_SET(0, 1) |
			STREAM_FEATURES_TILER_SET(0, 1);
	}

	return 0;
}

static int invent_capabilities(struct kbase_device *kbdev)
{
	struct dummy_firmware_interface *interface = kbdev->csf.shared_interface;
	struct kbase_csf_global_iface *iface = &kbdev->csf.global_iface;
	unsigned int gid;

	iface->input = interface->global_input;
	iface->output = interface->global_output;

	iface->version = 1;
	iface->kbdev = kbdev;
	iface->features = 0;
	iface->prfcnt_size = 64;

	if (iface->version >= kbase_csf_interface_version(1, 1, 0)) {
		/* update rate=1, max event size = 1<<8 = 256 */
		iface->instr_features = 0x81;
	} else {
		iface->instr_features = 0;
	}

	iface->group_num = ARRAY_SIZE(interface->csg);
	iface->group_stride = 0;

	iface->groups = kcalloc(iface->group_num, sizeof(*iface->groups), GFP_KERNEL);
	if (iface->groups == NULL) {
		return -ENOMEM;
	}

	for (gid = 0; gid < iface->group_num; ++gid) {
		int err;

		err = invent_cmd_stream_group_info(kbdev, &iface->groups[gid],
			&interface->csg[gid]);
		if (err < 0) {
			free_global_iface(kbdev);
			return err;
		}
	}

	return 0;
}

void kbase_csf_read_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 *value)
{
	/* NO_MALI: Nothing to do here */
}


void kbase_csf_update_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 value)
{
	/* NO_MALI: Nothing to do here */
}

void kbase_csf_firmware_cs_input(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset,
	const u32 value)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "cs input w: reg %08x val %08x\n", offset, value);
	input_page_write(info->input, offset, value);

	if (offset == CS_REQ) {
		/* NO_MALI: Immediately acknowledge requests */
		output_page_write(info->output, CS_ACK, value);
	}
}

u32 kbase_csf_firmware_cs_input_read(
	const struct kbase_csf_cmd_stream_info *const info,
	const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = input_page_read(info->input, offset);

	dev_dbg(kbdev->dev, "cs input r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_cs_input_mask(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset,
	const u32 value, const u32 mask)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "cs input w: reg %08x val %08x mask %08x\n",
			offset, value, mask);

	/* NO_MALI: Go through kbase_csf_firmware_cs_input to capture writes */
	kbase_csf_firmware_cs_input(info, offset, (input_page_read(info->input, offset) & ~mask) | (value & mask));
}

u32 kbase_csf_firmware_cs_output(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = output_page_read(info->output, offset);

	dev_dbg(kbdev->dev, "cs output r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_csg_input(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset, const u32 value)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "csg input w: reg %08x val %08x\n",
			offset, value);
	input_page_write(info->input, offset, value);

	if (offset == CSG_REQ) {
		/* NO_MALI: Immediately acknowledge requests */
		output_page_write(info->output, CSG_ACK, value);
	}
}

u32 kbase_csf_firmware_csg_input_read(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = input_page_read(info->input, offset);

	dev_dbg(kbdev->dev, "csg input r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_csg_input_mask(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset, const u32 value, const u32 mask)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "csg input w: reg %08x val %08x mask %08x\n",
			offset, value, mask);

	/* NO_MALI: Go through kbase_csf_firmware_csg_input to capture writes */
	kbase_csf_firmware_csg_input(info, offset, (input_page_read(info->input, offset) & ~mask) | (value & mask));
}

u32 kbase_csf_firmware_csg_output(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = output_page_read(info->output, offset);

	dev_dbg(kbdev->dev, "csg output r: reg %08x val %08x\n", offset, val);
	return val;
}

static void
csf_firmware_prfcnt_process(const struct kbase_csf_global_iface *const iface,
			    const u32 glb_req)
{
	struct kbase_device *kbdev = iface->kbdev;
	u32 glb_ack = output_page_read(iface->output, GLB_ACK);
	/* If the value of GLB_REQ.PRFCNT_SAMPLE is different from the value of
	 * GLB_ACK.PRFCNT_SAMPLE, the CSF will sample the performance counters.
	 */
	if ((glb_req ^ glb_ack) & GLB_REQ_PRFCNT_SAMPLE_MASK) {
		/* NO_MALI only uses the first buffer in the ring buffer. */
		input_page_write(iface->input, GLB_PRFCNT_EXTRACT, 0);
		output_page_write(iface->output, GLB_PRFCNT_INSERT, 1);
		kbase_reg_write(kbdev, GPU_COMMAND, GPU_COMMAND_PRFCNT_SAMPLE);
	}

	/* Propagate enable masks to model if request to enable. */
	if (glb_req & GLB_REQ_PRFCNT_ENABLE_MASK) {
		u32 tiler_en, l2_en, sc_en;

		tiler_en = input_page_read(iface->input, GLB_PRFCNT_TILER_EN);
		l2_en = input_page_read(iface->input, GLB_PRFCNT_MMU_L2_EN);
		sc_en = input_page_read(iface->input, GLB_PRFCNT_SHADER_EN);

		/* NO_MALI platform enabled all CSHW counters by default. */
		kbase_reg_write(kbdev, PRFCNT_TILER_EN, tiler_en);
		kbase_reg_write(kbdev, PRFCNT_MMU_L2_EN, l2_en);
		kbase_reg_write(kbdev, PRFCNT_SHADER_EN, sc_en);
	}
}

void kbase_csf_firmware_global_input(
	const struct kbase_csf_global_iface *const iface, const u32 offset,
	const u32 value)
{
	const struct kbase_device * const kbdev = iface->kbdev;

	dev_dbg(kbdev->dev, "glob input w: reg %08x val %08x\n", offset, value);
	input_page_write(iface->input, offset, value);

	if (offset == GLB_REQ) {
		csf_firmware_prfcnt_process(iface, value);
		/* NO_MALI: Immediately acknowledge requests */
		output_page_write(iface->output, GLB_ACK, value);
	}
}

void kbase_csf_firmware_global_input_mask(
	const struct kbase_csf_global_iface *const iface, const u32 offset,
	const u32 value, const u32 mask)
{
	const struct kbase_device * const kbdev = iface->kbdev;

	dev_dbg(kbdev->dev, "glob input w: reg %08x val %08x mask %08x\n",
			offset, value, mask);

	/* NO_MALI: Go through kbase_csf_firmware_global_input to capture writes */
	kbase_csf_firmware_global_input(iface, offset, (input_page_read(iface->input, offset) & ~mask) | (value & mask));
}

u32 kbase_csf_firmware_global_input_read(
	const struct kbase_csf_global_iface *const iface, const u32 offset)
{
	const struct kbase_device * const kbdev = iface->kbdev;
	u32 const val = input_page_read(iface->input, offset);

	dev_dbg(kbdev->dev, "glob input r: reg %08x val %08x\n", offset, val);
	return val;
}

u32 kbase_csf_firmware_global_output(
	const struct kbase_csf_global_iface *const iface, const u32 offset)
{
	const struct kbase_device * const kbdev = iface->kbdev;
	u32 const val = output_page_read(iface->output, offset);

	dev_dbg(kbdev->dev, "glob output r: reg %08x val %08x\n", offset, val);
	return val;
}

/**
 * handle_internal_firmware_fatal - Handler for CS internal firmware fault.
 *
 * @kbdev:  Pointer to kbase device
 *
 * Report group fatal error to user space for all GPU command queue groups
 * in the device, terminate them and reset GPU.
 */
static void handle_internal_firmware_fatal(struct kbase_device *const kbdev)
{
	int as;

	for (as = 0; as < kbdev->nr_hw_address_spaces; as++) {
		unsigned long flags;
		struct kbase_context *kctx;
		struct kbase_fault fault;

		if (as == MCU_AS_NR)
			continue;

		/* Only handle the fault for an active address space. Lock is
		 * taken here to atomically get reference to context in an
		 * active address space and retain its refcount.
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kctx = kbase_ctx_sched_as_to_ctx_nolock(kbdev, as);

		if (kctx) {
			kbase_ctx_sched_retain_ctx_refcount(kctx);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		} else {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			continue;
		}

		fault = (struct kbase_fault) {
			.status = GPU_EXCEPTION_TYPE_SW_FAULT_1,
		};

		kbase_csf_ctx_handle_fault(kctx, &fault);
		kbase_ctx_sched_release_ctx_lock(kctx);
	}

	if (kbase_prepare_to_reset_gpu(kbdev,
				       RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
		kbase_reset_gpu(kbdev);
}

/**
 * firmware_error_worker - Worker function for handling firmware internal error
 *
 * @data: Pointer to a work_struct embedded in kbase device.
 *
 * Handle the CS internal firmware error
 */
static void firmware_error_worker(struct work_struct *const data)
{
	struct kbase_device *const kbdev =
		container_of(data, struct kbase_device, csf.fw_error_work);

	handle_internal_firmware_fatal(kbdev);
}

static bool global_request_complete(struct kbase_device *const kbdev,
				    u32 const req_mask)
{
	struct kbase_csf_global_iface *global_iface =
				&kbdev->csf.global_iface;
	bool complete = false;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	if ((kbase_csf_firmware_global_output(global_iface, GLB_ACK) &
	     req_mask) ==
	    (kbase_csf_firmware_global_input_read(global_iface, GLB_REQ) &
	     req_mask))
		complete = true;

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	return complete;
}

static int wait_for_global_request(struct kbase_device *const kbdev,
				   u32 const req_mask)
{
	const long wait_timeout =
		kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	long remaining;
	int err = 0;

	remaining = wait_event_timeout(kbdev->csf.event_wait,
				       global_request_complete(kbdev, req_mask),
				       wait_timeout);

	if (!remaining) {
		dev_warn(kbdev->dev, "Timed out waiting for global request %x to complete",
			 req_mask);
		err = -ETIMEDOUT;
	}

	return err;
}

static void set_global_request(
	const struct kbase_csf_global_iface *const global_iface,
	u32 const req_mask)
{
	u32 glb_req;

	kbase_csf_scheduler_spin_lock_assert_held(global_iface->kbdev);

	glb_req = kbase_csf_firmware_global_output(global_iface, GLB_ACK);
	glb_req ^= req_mask;
	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ, glb_req,
					     req_mask);
}

static void enable_endpoints_global(
	const struct kbase_csf_global_iface *const global_iface,
	u64 const shader_core_mask)
{
	kbase_csf_firmware_global_input(global_iface, GLB_ALLOC_EN_LO,
		shader_core_mask & U32_MAX);
	kbase_csf_firmware_global_input(global_iface, GLB_ALLOC_EN_HI,
		shader_core_mask >> 32);

	set_global_request(global_iface, GLB_REQ_CFG_ALLOC_EN_MASK);
}

static void enable_shader_poweroff_timer(struct kbase_device *const kbdev,
		const struct kbase_csf_global_iface *const global_iface)
{
	u32 pwroff_reg;

	if (kbdev->csf.firmware_hctl_core_pwr)
		pwroff_reg =
		    GLB_PWROFF_TIMER_TIMER_SOURCE_SET(DISABLE_GLB_PWROFF_TIMER,
			GLB_PWROFF_TIMER_TIMER_SOURCE_SYSTEM_TIMESTAMP);
	else
		pwroff_reg = kbdev->csf.mcu_core_pwroff_dur_count;

	kbase_csf_firmware_global_input(global_iface, GLB_PWROFF_TIMER,
					pwroff_reg);
	set_global_request(global_iface, GLB_REQ_CFG_PWROFF_TIMER_MASK);

	/* Save the programed reg value in its shadow field */
	kbdev->csf.mcu_core_pwroff_reg_shadow = pwroff_reg;
}

static void set_timeout_global(
	const struct kbase_csf_global_iface *const global_iface,
	u64 const timeout)
{
	kbase_csf_firmware_global_input(global_iface, GLB_PROGRESS_TIMER,
		timeout / GLB_PROGRESS_TIMER_TIMEOUT_SCALE);

	set_global_request(global_iface, GLB_REQ_CFG_PROGRESS_TIMER_MASK);
}

static void global_init(struct kbase_device *const kbdev, u64 core_mask)
{
	u32 const ack_irq_mask = GLB_ACK_IRQ_MASK_CFG_ALLOC_EN_MASK |
				 GLB_ACK_IRQ_MASK_PING_MASK |
				 GLB_ACK_IRQ_MASK_CFG_PROGRESS_TIMER_MASK |
				 GLB_ACK_IRQ_MASK_PROTM_ENTER_MASK |
				 GLB_ACK_IRQ_MASK_FIRMWARE_CONFIG_UPDATE_MASK |
				 GLB_ACK_IRQ_MASK_PROTM_EXIT_MASK |
				 GLB_ACK_IRQ_MASK_CFG_PWROFF_TIMER_MASK |
				 GLB_ACK_IRQ_MASK_IDLE_EVENT_MASK;

	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	/* Update shader core allocation enable mask */
	enable_endpoints_global(global_iface, core_mask);
	enable_shader_poweroff_timer(kbdev, global_iface);

	set_timeout_global(global_iface, kbase_csf_timeout_get(kbdev));

	/* Unmask the interrupts */
	kbase_csf_firmware_global_input(global_iface,
		GLB_ACK_IRQ_MASK, ack_irq_mask);

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);

	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

/**
 * global_init_on_boot - Sends a global request to control various features.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Currently only the request to enable endpoints and cycle counter is sent.
 *
 * Return: 0 on success, or negative on failure.
 */
static int global_init_on_boot(struct kbase_device *const kbdev)
{
	unsigned long flags;
	u64 core_mask;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	core_mask = kbase_pm_ca_get_core_mask(kbdev);
	kbdev->csf.firmware_hctl_core_pwr =
				kbase_pm_no_mcu_core_pwroff(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	global_init(kbdev, core_mask);

	return wait_for_global_request(kbdev, CSF_GLB_REQ_CFG_MASK);
}

void kbase_csf_firmware_global_reinit(struct kbase_device *kbdev,
				      u64 core_mask)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->csf.glb_init_request_pending = true;
	kbdev->csf.firmware_hctl_core_pwr =
				kbase_pm_no_mcu_core_pwroff(kbdev);
	global_init(kbdev, core_mask);
}

bool kbase_csf_firmware_global_reinit_complete(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	WARN_ON(!kbdev->csf.glb_init_request_pending);

	if (global_request_complete(kbdev, CSF_GLB_REQ_CFG_MASK))
		kbdev->csf.glb_init_request_pending = false;

	return !kbdev->csf.glb_init_request_pending;
}

void kbase_csf_firmware_update_core_attr(struct kbase_device *kbdev,
		bool update_core_pwroff_timer, bool update_core_mask, u64 core_mask)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	if (update_core_mask)
		enable_endpoints_global(&kbdev->csf.global_iface, core_mask);
	if (update_core_pwroff_timer)
		enable_shader_poweroff_timer(kbdev, &kbdev->csf.global_iface);

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

bool kbase_csf_firmware_core_attr_updated(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return global_request_complete(kbdev, GLB_REQ_CFG_ALLOC_EN_MASK |
					      GLB_REQ_CFG_PWROFF_TIMER_MASK);
}

static void kbase_csf_firmware_reload_worker(struct work_struct *work)
{
	struct kbase_device *kbdev = container_of(work, struct kbase_device,
						  csf.firmware_reload_work);
	unsigned long flags;

	/* Reboot the firmware */
	kbase_csf_firmware_enable_mcu(kbdev);

	/* Tell MCU state machine to transit to next state */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->csf.firmware_reloaded = true;
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_csf_firmware_trigger_reload(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->csf.firmware_reloaded = false;

	if (kbdev->csf.firmware_reload_needed) {
		kbdev->csf.firmware_reload_needed = false;
		queue_work(system_wq, &kbdev->csf.firmware_reload_work);
	} else {
		kbase_csf_firmware_enable_mcu(kbdev);
		kbdev->csf.firmware_reloaded = true;
	}
}

void kbase_csf_firmware_reload_completed(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (unlikely(!kbdev->csf.firmware_inited))
		return;

	/* Tell MCU state machine to transit to next state */
	kbdev->csf.firmware_reloaded = true;
	kbase_pm_update_state(kbdev);
}

static u32 convert_dur_to_idle_count(struct kbase_device *kbdev, const u32 dur_ms)
{
#define HYSTERESIS_VAL_UNIT_SHIFT (10)
	/* Get the cntfreq_el0 value, which drives the SYSTEM_TIMESTAMP */
	u64 freq = arch_timer_get_cntfrq();
	u64 dur_val = dur_ms;
	u32 cnt_val_u32, reg_val_u32;
	bool src_system_timestamp = freq > 0;

	if (!src_system_timestamp) {
		/* Get the cycle_counter source alternative */
		spin_lock(&kbdev->pm.clk_rtm.lock);
		if (kbdev->pm.clk_rtm.clks[0])
			freq = kbdev->pm.clk_rtm.clks[0]->clock_val;
		else
			dev_warn(kbdev->dev, "No GPU clock, unexpected intregration issue!");
		spin_unlock(&kbdev->pm.clk_rtm.lock);

		dev_info(kbdev->dev, "Can't get the timestamp frequency, "
			 "use cycle counter format with firmware idle hysteresis!");
	}

	/* Formula for dur_val = ((dur_ms/1000) * freq_HZ) >> 10) */
	dur_val = (dur_val * freq) >> HYSTERESIS_VAL_UNIT_SHIFT;
	dur_val = div_u64(dur_val, 1000);

	/* Interface limits the value field to S32_MAX */
	cnt_val_u32 = (dur_val > S32_MAX) ? S32_MAX : (u32)dur_val;

	reg_val_u32 = GLB_IDLE_TIMER_TIMEOUT_SET(0, cnt_val_u32);
	/* add the source flag */
	if (src_system_timestamp)
		reg_val_u32 = GLB_IDLE_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_IDLE_TIMER_TIMER_SOURCE_SYSTEM_TIMESTAMP);
	else
		reg_val_u32 = GLB_IDLE_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_IDLE_TIMER_TIMER_SOURCE_GPU_COUNTER);

	return reg_val_u32;
}

u32 kbase_csf_firmware_get_gpu_idle_hysteresis_time(struct kbase_device *kbdev)
{
	return kbdev->csf.gpu_idle_hysteresis_ms;
}

u32 kbase_csf_firmware_set_gpu_idle_hysteresis_time(struct kbase_device *kbdev, u32 dur)
{
	unsigned long flags;
	const u32 hysteresis_val = convert_dur_to_idle_count(kbdev, dur);

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbdev->csf.gpu_idle_hysteresis_ms = dur;
	kbdev->csf.gpu_idle_dur_count = hysteresis_val;
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	dev_dbg(kbdev->dev, "CSF set firmware idle hysteresis count-value: 0x%.8x",
		hysteresis_val);

	return hysteresis_val;
}

static u32 convert_dur_to_core_pwroff_count(struct kbase_device *kbdev, const u32 dur_us)
{
#define PWROFF_VAL_UNIT_SHIFT (10)
	/* Get the cntfreq_el0 value, which drives the SYSTEM_TIMESTAMP */
	u64 freq = arch_timer_get_cntfrq();
	u64 dur_val = dur_us;
	u32 cnt_val_u32, reg_val_u32;
	bool src_system_timestamp = freq > 0;

	if (!src_system_timestamp) {
		/* Get the cycle_counter source alternative */
		spin_lock(&kbdev->pm.clk_rtm.lock);
		if (kbdev->pm.clk_rtm.clks[0])
			freq = kbdev->pm.clk_rtm.clks[0]->clock_val;
		else
			dev_warn(kbdev->dev, "No GPU clock, unexpected integration issue!");
		spin_unlock(&kbdev->pm.clk_rtm.lock);

		dev_info(kbdev->dev, "Can't get the timestamp frequency, "
			 "use cycle counter with MCU Core Poweroff timer!");
	}

	/* Formula for dur_val = ((dur_us/1e6) * freq_HZ) >> 10) */
	dur_val = (dur_val * freq) >> HYSTERESIS_VAL_UNIT_SHIFT;
	dur_val = div_u64(dur_val, 1000000);

	/* Interface limits the value field to S32_MAX */
	cnt_val_u32 = (dur_val > S32_MAX) ? S32_MAX : (u32)dur_val;

	reg_val_u32 = GLB_PWROFF_TIMER_TIMEOUT_SET(0, cnt_val_u32);
	/* add the source flag */
	if (src_system_timestamp)
		reg_val_u32 = GLB_PWROFF_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_PWROFF_TIMER_TIMER_SOURCE_SYSTEM_TIMESTAMP);
	else
		reg_val_u32 = GLB_PWROFF_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_PWROFF_TIMER_TIMER_SOURCE_GPU_COUNTER);

	return reg_val_u32;
}

u32 kbase_csf_firmware_get_mcu_core_pwroff_time(struct kbase_device *kbdev)
{
	return kbdev->csf.mcu_core_pwroff_dur_us;
}

u32 kbase_csf_firmware_set_mcu_core_pwroff_time(struct kbase_device *kbdev, u32 dur)
{
	unsigned long flags;
	const u32 pwroff = convert_dur_to_core_pwroff_count(kbdev, dur);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->csf.mcu_core_pwroff_dur_us = dur;
	kbdev->csf.mcu_core_pwroff_dur_count = pwroff;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_dbg(kbdev->dev, "MCU Core Poweroff input update: 0x%.8x", pwroff);

	return pwroff;
}

int kbase_csf_firmware_early_init(struct kbase_device *kbdev)
{
	init_waitqueue_head(&kbdev->csf.event_wait);
	kbdev->csf.interrupt_received = false;
	kbdev->csf.fw_timeout_ms = CSF_FIRMWARE_TIMEOUT_MS;

	INIT_LIST_HEAD(&kbdev->csf.firmware_interfaces);
	INIT_LIST_HEAD(&kbdev->csf.firmware_config);
	INIT_LIST_HEAD(&kbdev->csf.firmware_trace_buffers.list);
	INIT_WORK(&kbdev->csf.firmware_reload_work,
		  kbase_csf_firmware_reload_worker);
	INIT_WORK(&kbdev->csf.fw_error_work, firmware_error_worker);

	mutex_init(&kbdev->csf.reg_lock);

	return 0;
}

int kbase_csf_firmware_init(struct kbase_device *kbdev)
{
	int ret;

	lockdep_assert_held(&kbdev->fw_load_lock);

	if (WARN_ON((kbdev->as_free & MCU_AS_BITMASK) == 0))
		return -EINVAL;
	kbdev->as_free &= ~MCU_AS_BITMASK;

	ret = kbase_mmu_init(kbdev, &kbdev->csf.mcu_mmu, NULL,
		BASE_MEM_GROUP_DEFAULT);

	if (ret != 0) {
		/* Release the address space */
		kbdev->as_free |= MCU_AS_BITMASK;
		return ret;
	}

	kbdev->csf.gpu_idle_hysteresis_ms = FIRMWARE_IDLE_HYSTERESIS_TIME_MS;
	kbdev->csf.gpu_idle_dur_count = convert_dur_to_idle_count(
		kbdev, FIRMWARE_IDLE_HYSTERESIS_TIME_MS);

	ret = kbase_mcu_shared_interface_region_tracker_init(kbdev);
	if (ret != 0) {
		dev_err(kbdev->dev,
			"Failed to setup the rb tree for managing shared interface segment\n");
		goto error;
	}

	ret = invent_memory_setup_entry(kbdev);
	if (ret != 0) {
		dev_err(kbdev->dev, "Failed to load firmware entry\n");
		goto error;
	}

	/* Make sure L2 cache is powered up */
	kbase_pm_wait_for_l2_powered(kbdev);

	/* NO_MALI: Don't init trace buffers */

	/* NO_MALI: Don't load the MMU tables or boot CSF firmware */

	ret = invent_capabilities(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_doorbell_mapping_init(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_setup_dummy_user_reg_page(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_scheduler_init(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_timeout_init(kbdev);
	if (ret != 0)
		goto error;

	ret = global_init_on_boot(kbdev);
	if (ret != 0)
		goto error;

	return 0;

error:
	kbase_csf_firmware_term(kbdev);
	return ret;
}

void kbase_csf_firmware_term(struct kbase_device *kbdev)
{
	cancel_work_sync(&kbdev->csf.fw_error_work);

	kbase_csf_timeout_term(kbdev);

	/* NO_MALI: Don't stop firmware or unload MMU tables */

	kbase_mmu_term(kbdev, &kbdev->csf.mcu_mmu);

	kbase_csf_scheduler_term(kbdev);

	kbase_csf_free_dummy_user_reg_page(kbdev);

	kbase_csf_doorbell_mapping_term(kbdev);

	free_global_iface(kbdev);

	/* Release the address space */
	kbdev->as_free |= MCU_AS_BITMASK;

	while (!list_empty(&kbdev->csf.firmware_interfaces)) {
		struct dummy_firmware_interface *interface;

		interface = list_first_entry(&kbdev->csf.firmware_interfaces,
				struct dummy_firmware_interface, node);
		list_del(&interface->node);

		/* NO_MALI: No cleanup in dummy interface necessary */

		kfree(interface);
	}

	/* NO_MALI: No trace buffers to terminate */

#ifndef MALI_KBASE_BUILD
	mali_kutf_fw_utf_entry_cleanup(kbdev);
#endif

	mutex_destroy(&kbdev->csf.reg_lock);

	/* This will also free up the region allocated for the shared interface
	 * entry parsed from the firmware image.
	 */
	kbase_mcu_shared_interface_region_tracker_term(kbdev);
}

void kbase_csf_firmware_enable_gpu_idle_timer(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	u32 glb_req;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	/* The scheduler is assumed to only call the enable when its internal
	 * state indicates that the idle timer has previously been disabled. So
	 * on entry the expected field values are:
	 *   1. GLOBAL_INPUT_BLOCK.GLB_REQ.IDLE_ENABLE: 0
	 *   2. GLOBAL_OUTPUT_BLOCK.GLB_ACK.IDLE_ENABLE: 0, or, on 1 -> 0
	 */

	glb_req = kbase_csf_firmware_global_input_read(global_iface, GLB_REQ);
	if (glb_req & GLB_REQ_IDLE_ENABLE_MASK)
		dev_err(kbdev->dev, "Incoherent scheduler state on REQ_IDLE_ENABLE!");

	kbase_csf_firmware_global_input(global_iface, GLB_IDLE_TIMER,
					kbdev->csf.gpu_idle_dur_count);

	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ,
				GLB_REQ_REQ_IDLE_ENABLE, GLB_REQ_IDLE_ENABLE_MASK);

	dev_dbg(kbdev->dev, "Enabling GPU idle timer with count-value: 0x%.8x",
		kbdev->csf.gpu_idle_dur_count);
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
}

void kbase_csf_firmware_disable_gpu_idle_timer(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ,
					GLB_REQ_REQ_IDLE_DISABLE,
					GLB_REQ_IDLE_DISABLE_MASK);

	dev_dbg(kbdev->dev, "Sending request to disable gpu idle timer");

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
}

void kbase_csf_firmware_ping(struct kbase_device *const kbdev)
{
	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_global_request(global_iface, GLB_REQ_PING_MASK);
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

int kbase_csf_firmware_ping_wait(struct kbase_device *const kbdev)
{
	kbase_csf_firmware_ping(kbdev);
	return wait_for_global_request(kbdev, GLB_REQ_PING_MASK);
}

int kbase_csf_firmware_set_timeout(struct kbase_device *const kbdev,
	u64 const timeout)
{
	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	unsigned long flags;
	int err;

	/* The 'reg_lock' is also taken and is held till the update is not
	 * complete, to ensure the update of timeout value by multiple Users
	 * gets serialized.
	 */
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_timeout_global(global_iface, timeout);
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	err = wait_for_global_request(kbdev, GLB_REQ_CFG_PROGRESS_TIMER_MASK);
	mutex_unlock(&kbdev->csf.reg_lock);

	return err;
}

void kbase_csf_enter_protected_mode(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_global_request(global_iface, GLB_REQ_PROTM_ENTER_MASK);
	dev_dbg(kbdev->dev, "Sending request to enter protected mode");
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	wait_for_global_request(kbdev, GLB_REQ_PROTM_ENTER_MASK);
}

void kbase_csf_firmware_trigger_mcu_halt(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_global_request(global_iface, GLB_REQ_HALT_MASK);
	dev_dbg(kbdev->dev, "Sending request to HALT MCU");
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

int kbase_csf_trigger_firmware_config_update(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	unsigned long flags;
	int err = 0;

	/* The 'reg_lock' is also taken and is held till the update is
	 * complete, to ensure the config update gets serialized.
	 */
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	set_global_request(global_iface, GLB_REQ_FIRMWARE_CONFIG_UPDATE_MASK);
	dev_dbg(kbdev->dev, "Sending request for FIRMWARE_CONFIG_UPDATE");
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	err = wait_for_global_request(kbdev,
				      GLB_REQ_FIRMWARE_CONFIG_UPDATE_MASK);
	mutex_unlock(&kbdev->csf.reg_lock);
	return err;
}

/**
 * copy_grp_and_stm - Copy CS and/or group data
 *
 * @iface:                Global CSF interface provided by
 *                        the firmware.
 * @group_data:           Pointer where to store all the group data
 *                        (sequentially).
 * @max_group_num:        The maximum number of groups to be read. Can be 0, in
 *                        which case group_data is unused.
 * @stream_data:          Pointer where to store all the stream data
 *                        (sequentially).
 * @max_total_stream_num: The maximum number of streams to be read.
 *                        Can be 0, in which case stream_data is unused.
 *
 * Return: Total number of CSs, summed across all groups.
 */
static u32 copy_grp_and_stm(
	const struct kbase_csf_global_iface * const iface,
	struct basep_cs_group_control * const group_data,
	u32 max_group_num,
	struct basep_cs_stream_control * const stream_data,
	u32 max_total_stream_num)
{
	u32 i, total_stream_num = 0;

	if (WARN_ON((max_group_num > 0) && !group_data))
		max_group_num = 0;

	if (WARN_ON((max_total_stream_num > 0) && !stream_data))
		max_total_stream_num = 0;

	for (i = 0; i < iface->group_num; i++) {
		u32 j;

		if (i < max_group_num) {
			group_data[i].features = iface->groups[i].features;
			group_data[i].stream_num = iface->groups[i].stream_num;
			group_data[i].suspend_size =
				iface->groups[i].suspend_size;
		}
		for (j = 0; j < iface->groups[i].stream_num; j++) {
			if (total_stream_num < max_total_stream_num)
				stream_data[total_stream_num].features =
					iface->groups[i].streams[j].features;
			total_stream_num++;
		}
	}

	return total_stream_num;
}

u32 kbase_csf_firmware_get_glb_iface(
	struct kbase_device *kbdev,
	struct basep_cs_group_control *const group_data,
	u32 const max_group_num,
	struct basep_cs_stream_control *const stream_data,
	u32 const max_total_stream_num, u32 *const glb_version,
	u32 *const features, u32 *const group_num, u32 *const prfcnt_size,
	u32 *const instr_features)
{
	const struct kbase_csf_global_iface * const iface =
		&kbdev->csf.global_iface;

	if (WARN_ON(!glb_version) || WARN_ON(!features) ||
	    WARN_ON(!group_num) || WARN_ON(!prfcnt_size) ||
	    WARN_ON(!instr_features))
		return 0;

	*glb_version = iface->version;
	*features = iface->features;
	*group_num = iface->group_num;
	*prfcnt_size = iface->prfcnt_size;
	*instr_features = iface->instr_features;

	return copy_grp_and_stm(iface, group_data, max_group_num,
		stream_data, max_total_stream_num);
}

const char *kbase_csf_firmware_get_timeline_metadata(
	struct kbase_device *kbdev, const char *name, size_t *size)
{
	if (WARN_ON(!kbdev) ||
		WARN_ON(!name) ||
		WARN_ON(!size)) {
		return NULL;
	}

	*size = 0;
	return NULL;
}

void kbase_csf_firmware_disable_mcu_wait(struct kbase_device *kbdev)
{
	/* NO_MALI: Nothing to do here */
}

int kbase_csf_firmware_mcu_shared_mapping_init(
		struct kbase_device *kbdev,
		unsigned int num_pages,
		unsigned long cpu_map_properties,
		unsigned long gpu_map_properties,
		struct kbase_csf_mapping *csf_mapping)
{
	struct tagged_addr *phys;
	struct kbase_va_region *va_reg;
	struct page **page_list;
	void *cpu_addr;
	int i, ret = 0;
	pgprot_t cpu_map_prot = PAGE_KERNEL;
	unsigned long gpu_map_prot;

	if (cpu_map_properties & PROT_READ)
		cpu_map_prot = PAGE_KERNEL_RO;

	if (kbdev->system_coherency == COHERENCY_ACE) {
		gpu_map_prot =
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT_ACE);
	} else {
		gpu_map_prot =
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
		cpu_map_prot = pgprot_writecombine(cpu_map_prot);
	};

	phys = kmalloc_array(num_pages, sizeof(*phys), GFP_KERNEL);
	if (!phys)
		goto out;

	page_list = kmalloc_array(num_pages, sizeof(*page_list), GFP_KERNEL);
	if (!page_list)
		goto page_list_alloc_error;

	ret = kbase_mem_pool_alloc_pages(
		&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
		num_pages, phys, false);
	if (ret <= 0)
		goto phys_mem_pool_alloc_error;

	for (i = 0; i < num_pages; i++)
		page_list[i] = as_page(phys[i]);

	cpu_addr = vmap(page_list, num_pages, VM_MAP, cpu_map_prot);
	if (!cpu_addr)
		goto vmap_error;

	va_reg = kbase_alloc_free_region(&kbdev->csf.shared_reg_rbtree, 0,
			num_pages, KBASE_REG_ZONE_MCU_SHARED);
	if (!va_reg)
		goto va_region_alloc_error;

	mutex_lock(&kbdev->csf.reg_lock);
	ret = kbase_add_va_region_rbtree(kbdev, va_reg, 0, num_pages, 1);
	va_reg->flags &= ~KBASE_REG_FREE;
	if (ret)
		goto va_region_add_error;
	mutex_unlock(&kbdev->csf.reg_lock);

	gpu_map_properties &= (KBASE_REG_GPU_RD | KBASE_REG_GPU_WR);
	gpu_map_properties |= gpu_map_prot;

	ret = kbase_mmu_insert_pages_no_flush(kbdev, &kbdev->csf.mcu_mmu,
			va_reg->start_pfn, &phys[0], num_pages,
			gpu_map_properties, KBASE_MEM_GROUP_CSF_FW);
	if (ret)
		goto mmu_insert_pages_error;

	kfree(page_list);
	csf_mapping->phys = phys;
	csf_mapping->cpu_addr = cpu_addr;
	csf_mapping->va_reg = va_reg;
	csf_mapping->num_pages = num_pages;

	return 0;

mmu_insert_pages_error:
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_remove_va_region(va_reg);
va_region_add_error:
	kbase_free_alloced_region(va_reg);
	mutex_unlock(&kbdev->csf.reg_lock);
va_region_alloc_error:
	vunmap(cpu_addr);
vmap_error:
	kbase_mem_pool_free_pages(
		&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
		num_pages, phys, false, false);

phys_mem_pool_alloc_error:
	kfree(page_list);
page_list_alloc_error:
	kfree(phys);
out:
	/* Zero-initialize the mapping to make sure that the termination
	 * function doesn't try to unmap or free random addresses.
	 */
	csf_mapping->phys = NULL;
	csf_mapping->cpu_addr = NULL;
	csf_mapping->va_reg = NULL;
	csf_mapping->num_pages = 0;

	return -ENOMEM;
}

void kbase_csf_firmware_mcu_shared_mapping_term(
		struct kbase_device *kbdev, struct kbase_csf_mapping *csf_mapping)
{
	if (csf_mapping->va_reg) {
		mutex_lock(&kbdev->csf.reg_lock);
		kbase_remove_va_region(csf_mapping->va_reg);
		kbase_free_alloced_region(csf_mapping->va_reg);
		mutex_unlock(&kbdev->csf.reg_lock);
	}

	if (csf_mapping->phys) {
		kbase_mem_pool_free_pages(
			&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
			csf_mapping->num_pages, csf_mapping->phys, false,
			false);
	}

	vunmap(csf_mapping->cpu_addr);
	kfree(csf_mapping->phys);
}
