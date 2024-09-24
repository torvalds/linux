/*
 *
 * (C) COPYRIGHT 2018-2024 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_device.h"
#include "ethosn_network.h"

#include "ethosn_backport.h"
#include "ethosn_firmware.h"
#include "ethosn_smc.h"
#include "scylla_addr_fields_public.h"
#include "scylla_regs_public.h"

#include <linux/firmware.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/poll.h>
#include <linux/jiffies.h>

#define ETHOSN_NAME_PREFIX  "ethosn"

/* Magic (FourCC) number to identify the big firmware binary */
#define ETHOSN_BIG_FW_MAGIC ('E' | ('N' << 8) | ('F' << 16) | ('W' << 24))

/* Number of bits the MCU Vector Table address is shifted. */
#define SYSCTLR0_INITVTOR_SHIFT         7

/* Timeout in us when resetting the Ethos-N */
#define ETHOSN_RESET_TIMEOUT_US         (10 * 1000 * 1000)
#define ETHOSN_RESET_WAIT_US            1000

/* Regset32 entry */
#define REGSET32(r) { __stringify(r), \
		      TOP_REG(DL1_RP, DL1_ ## r) - TOP_REG(0, 0) }

static int firmware_log_severity = ETHOSN_LOG_INFO;
module_param(firmware_log_severity, int, 0660);

bool firmware_log_to_kernel_log = true;
module_param(firmware_log_to_kernel_log, bool, 0660);

static int ethosn_queue_size = 65536;
module_param_named(queue_size, ethosn_queue_size, int, 0440);

static bool profiling_enabled;
module_param_named(profiling, profiling_enabled, bool, 0664);

/* Clock frequency expressed in MHz */
static int clock_frequency = 1000;
module_param_named(clock_frequency, clock_frequency, int, 0440);

static bool stashing_enabled = true;
module_param_named(stashing, stashing_enabled, bool, 0440);

/* Exposes global access to the most-recently created Ethos-N core for testing
 * purposes.
 */
static struct ethosn_core *ethosn_global_core_for_testing;

static void __iomem *ethosn_top_reg_addr(void __iomem *const top_regs,
					 const u32 page,
					 const u32 offset)
{
	return (u8 __iomem *)top_regs + (TOP_REG(page, offset) - TOP_REG(0, 0));
}

resource_size_t to_ethosn_addr(const resource_size_t linux_addr,
			       const struct ethosn_addr_map *addr_map)
{
	const resource_size_t region_addr = addr_map->region;
	const resource_size_t region_extend = addr_map->extension;
	const resource_size_t region_size = 1 << REGION_SHIFT;
	resource_size_t ethosn_addr;

	/* Verify that region addresses are a multiple of the region size. */
	if ((region_addr | region_extend) & ETHOSN_REGION_MASK)
		return -EFAULT;

	/*
	 * Verify that the Linux address lies between the region extend and the
	 * region size.
	 */
	if ((linux_addr < region_extend) ||
	    (linux_addr >= (region_extend + region_size)))
		return -EFAULT;

	/* Combine the region address with the region offset. */
	ethosn_addr = region_addr | (linux_addr & ETHOSN_REGION_MASK);

	return ethosn_addr;
}

/**
 * ethosn_mailbox_init() - Initialize the mailbox structure.
 * @core:	Pointer to Ethos-N core.
 *
 * Return: 0 on success, else error code.
 */
static int ethosn_mailbox_init(struct ethosn_core *core)
{
	struct ethosn_mailbox *mailbox = core->mailbox->cpu_addr;
	struct ethosn_queue *request = core->mailbox_request->cpu_addr;
	struct ethosn_queue *response = core->mailbox_response->cpu_addr;
	resource_size_t mailbox_addr;

	dev_dbg(core->dev, "%s\n", __func__);

	/* Clear memory */
	memset(mailbox, 0, core->mailbox->size);
	memset(request, 0, core->mailbox_request->size);
	memset(response, 0, core->mailbox_response->size);

	/* Setup queue sizes */
	request->capacity = core->mailbox_request->size -
			    sizeof(struct ethosn_queue);
	response->capacity = core->mailbox_response->size -
			     sizeof(struct ethosn_queue);

	/* Set severity, and make sure it's in the range [PANIC, VERBOSE]. */
	mailbox->severity = max(min(firmware_log_severity,
				    ETHOSN_LOG_VERBOSE), ETHOSN_LOG_PANIC);

	/* Set Ethos-N addresses from mailbox to queues */
	mailbox->request = to_ethosn_addr(core->mailbox_request->iova_addr,
					  &core->work_data_map);
	if (IS_ERR_VALUE((unsigned long)mailbox->request))
		return -EFAULT;

	mailbox->response = to_ethosn_addr(core->mailbox_response->iova_addr,
					   &core->work_data_map);
	if (IS_ERR_VALUE((unsigned long)mailbox->response))
		return -EFAULT;

	/* Store mailbox address in GP2 */
	mailbox_addr = to_ethosn_addr(core->mailbox->iova_addr,
				      &core->work_data_map);
	if (IS_ERR_VALUE((unsigned long)mailbox_addr))
		return -EFAULT;

	/* Sync memory to device */
	ethosn_dma_sync_for_device(core->main_allocator, core->mailbox);
	ethosn_dma_sync_for_device(core->main_allocator, core->mailbox_request);
	ethosn_dma_sync_for_device(core->main_allocator,
				   core->mailbox_response);

	/* Store mailbox CU address in GP2 */
	ethosn_write_top_reg(core, DL1_RP, GP_MAILBOX, mailbox_addr);

	return 0;
}

#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)

static void debug_monitor_channel_timer_callback(struct timer_list *timer)
{
	struct ethosn_core *core = container_of(timer, struct ethosn_core,
						debug_monitor_channel_timer);

	wake_up_poll(&core->debug_monitor_channel_read_poll_wqh, EPOLLIN);
}

#endif

/**
 * ethosn_debug_monitor_channel_init() - Initialize the debug_monitor_channel
 * structure.
 * @core:	Pointer to Ethos-N core.
 *
 * Return: 0 on success, else error code.
 */
static int ethosn_debug_monitor_channel_init(struct ethosn_core *core)
{
#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)
	struct ethosn_debug_monitor_channel *debug_monitor_channel =
		core->debug_monitor_channel->cpu_addr;
	struct ethosn_queue *request =
		core->debug_monitor_channel_request->cpu_addr;
	struct ethosn_queue *response =
		core->debug_monitor_channel_response->cpu_addr;
	resource_size_t debug_monitor_channel_addr;

	dev_dbg(core->dev, "%s\n", __func__);

	/* Clear memory */
	memset(debug_monitor_channel, 0, core->debug_monitor_channel->size);
	memset(request, 0, core->debug_monitor_channel_request->size);
	memset(response, 0, core->debug_monitor_channel_response->size);

	/* Setup queue sizes */
	request->capacity = core->debug_monitor_channel_request->size -
			    sizeof(struct ethosn_queue);
	response->capacity = core->debug_monitor_channel_response->size -
			     sizeof(struct ethosn_queue);

	/* Set Ethos-N addresses from debug_monitor_channel to queues */
	debug_monitor_channel->request = to_ethosn_addr(
		core->debug_monitor_channel_request->iova_addr,
		&core->work_data_map);
	if (IS_ERR_VALUE((unsigned long)debug_monitor_channel->request))
		return -EFAULT;

	debug_monitor_channel->response = to_ethosn_addr(
		core->debug_monitor_channel_response->iova_addr,
		&core->work_data_map);
	if (IS_ERR_VALUE((unsigned long)debug_monitor_channel->response))
		return -EFAULT;

	debug_monitor_channel_addr = to_ethosn_addr(
		core->debug_monitor_channel->iova_addr,
		&core->work_data_map);
	if (IS_ERR_VALUE((unsigned long)debug_monitor_channel_addr))
		return -EFAULT;

	init_waitqueue_head(&core->debug_monitor_channel_read_poll_wqh);

	timer_setup(&core->debug_monitor_channel_timer,
		    debug_monitor_channel_timer_callback, 0);

	/* Sync memory to device */
	ethosn_dma_sync_for_device(core->main_allocator,
				   core->debug_monitor_channel);
	ethosn_dma_sync_for_device(core->main_allocator,
				   core->debug_monitor_channel_request);
	ethosn_dma_sync_for_device(core->main_allocator,
				   core->debug_monitor_channel_response);

	ethosn_write_top_reg(core, DL1_RP, GP_DEBUG_MONITOR_CHANNEL,
			     debug_monitor_channel_addr);
#endif

	return 0;
}

/**
 * mailbox_alloc() - Allocate the mailbox.
 * @core:	Pointer to Ethos-N core.
 *
 * Return: 0 on success, else error code.
 */
static int mailbox_alloc(struct ethosn_core *core)
{
	struct ethosn_dma_allocator *allocator = core->main_allocator;
	resource_size_t mailbox_end;
	resource_size_t request_end;
	resource_size_t response_end;
	int ret = -ENOMEM;

	core->mailbox =
		ethosn_dma_alloc_and_map(
			allocator,
			sizeof(struct ethosn_mailbox),
			ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
			ETHOSN_STREAM_WORKING_DATA,
			GFP_KERNEL,
			"mailbox-header");
	if (IS_ERR_OR_NULL(core->mailbox)) {
		dev_warn(core->dev,
			 "Failed to allocate memory for mailbox");
		goto err_exit;
	}

	core->mailbox_request =
		ethosn_dma_alloc_and_map(
			allocator,
			sizeof(struct ethosn_queue) +
			core->queue_size,
			ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
			ETHOSN_STREAM_WORKING_DATA,
			GFP_KERNEL,
			"mailbox-request");
	if (IS_ERR_OR_NULL(core->mailbox_request)) {
		dev_warn(core->dev,
			 "Failed to allocate memory for mailbox request queue");
		goto err_free_mailbox;
	}

	core->mailbox_response =
		ethosn_dma_alloc_and_map(allocator,
					 sizeof(struct ethosn_queue) +
					 core->queue_size,
					 ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
					 ETHOSN_STREAM_WORKING_DATA,
					 GFP_KERNEL,
					 "mailbox-response");
	if (IS_ERR_OR_NULL(core->mailbox_response)) {
		dev_warn(core->dev,
			 "Failed to allocate memory for mailbox response queue");
		goto err_free_mailbox_request;
	}

	mailbox_end = core->mailbox->iova_addr + core->mailbox->size;
	request_end = core->mailbox_request->iova_addr +
		      core->mailbox_request->size;
	response_end = core->mailbox_response->iova_addr +
		       core->mailbox_response->size;

	core->mailbox_size =
		roundup_pow_of_two(max3(mailbox_end, request_end,
					response_end) -
				   ethosn_dma_get_addr_base(
					   core->main_allocator,
					   ETHOSN_STREAM_WORKING_DATA));

	dev_dbg(core->dev, "Mailbox size: %zu bytes\n", core->mailbox_size);

	core->mailbox_message =
		devm_kzalloc(core->parent->dev, core->queue_size,
			     GFP_KERNEL);
	if (!core->mailbox_message)
		goto err_free_mailbox_response;

	core->num_pongs_received = 0;

	return 0;

err_free_mailbox_response:
	ethosn_dma_unmap_and_release(allocator, &core->mailbox_response);
err_free_mailbox_request:
	ethosn_dma_unmap_and_release(allocator, &core->mailbox_request);
err_free_mailbox:
	ethosn_dma_unmap_and_release(allocator, &core->mailbox);
err_exit:

	return ret;
}

/**
 * firmware_log_alloc() - Allocate the firmware_log buffer.
 * @core:	Pointer to Ethos-N core.
 *
 * Return: 0 on success, else error code.
 */
static int firmware_log_alloc(struct ethosn_core *core)
{
	/* Note that we use the same size for the buffer here as for the
	 * mailbox queues. This is fairly arbitrary.
	 */
	if (kfifo_alloc(&core->firmware_log, core->queue_size, 0) != 0) {
		dev_err(core->dev,
			"Failed to allocate memory for firmware_log");

		return -ENOMEM;
	}

	/* Initialize the firmware_log */
	init_waitqueue_head(&core->firmware_log_read_poll_wqh);

	return 0;
}

/**
 * debug_monitor_channel_alloc() - Allocate the debug_monitor_channel.
 * @core:	Pointer to Ethos-N core.
 *
 * Return: 0 on success, else error code.
 */
static int debug_monitor_channel_alloc(struct ethosn_core *core)
{
#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)
	struct ethosn_dma_allocator *allocator = core->main_allocator;
	int ret = -ENOMEM;

	core->debug_monitor_channel =
		ethosn_dma_alloc_and_map(
			allocator,
			sizeof(struct ethosn_debug_monitor_channel),
			ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
			ETHOSN_STREAM_WORKING_DATA,
			GFP_KERNEL,
			"debug_monitor_channel-header");
	if (IS_ERR_OR_NULL(core->debug_monitor_channel)) {
		dev_warn(core->dev,
			 "Failed to allocate memory for debug_monitor_channel");
		goto err_exit;
	}

	/* Note that we use the same size for the queues here as we do for the
	 * mailbox queues. This is fairly arbitrary.
	 */
	core->debug_monitor_channel_request =
		ethosn_dma_alloc_and_map(
			allocator,
			sizeof(struct ethosn_queue) +
			core->queue_size,
			ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
			ETHOSN_STREAM_WORKING_DATA,
			GFP_KERNEL,
			"debug_monitor_channel-request");
	if (IS_ERR_OR_NULL(core->debug_monitor_channel_request)) {
		dev_warn(core->dev,
			 "Failed to allocate memory for debug_monitor_channel request queue");
		goto err_free_debug_monitor_channel;
	}

	core->debug_monitor_channel_response =
		ethosn_dma_alloc_and_map(allocator,
					 sizeof(struct ethosn_queue) +
					 core->queue_size,
					 ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
					 ETHOSN_STREAM_WORKING_DATA,
					 GFP_KERNEL,
					 "debug_monitor_channel-response");
	if (IS_ERR_OR_NULL(core->debug_monitor_channel_response)) {
		dev_warn(core->dev,
			 "Failed to allocate memory for debug_monitor_channel response queue");
		goto err_free_debug_monitor_channel_request;
	}

	return 0;

	ethosn_dma_unmap_and_release(allocator,
				     core->debug_monitor_channel_response);
err_free_debug_monitor_channel_request:
	ethosn_dma_unmap_and_release(allocator,
				     core->debug_monitor_channel_request);
err_free_debug_monitor_channel:
	ethosn_dma_unmap_and_release(allocator, core->debug_monitor_channel);
err_exit:

	return ret;
#else

	return 0;
#endif
}

/**
 * ethosn_mailbox_free() - Free the mailbox.
 * @core:	Pointer to Ethos-N core.
 */
static void ethosn_mailbox_free(struct ethosn_core *core)
{
	ethosn_dma_unmap_and_release(core->main_allocator, &core->mailbox);

	ethosn_dma_unmap_and_release(core->main_allocator,
				     &core->mailbox_request);

	ethosn_dma_unmap_and_release(core->main_allocator,
				     &core->mailbox_response);

	if (core->mailbox_message) {
		devm_kfree(core->parent->dev, core->mailbox_message);
		core->mailbox_message = NULL;
	}
}

/**
 * ethosn_firmware_log_free() - Free the firmware_log buffer.
 * @core:	Pointer to Ethos-N core.
 */
static void ethosn_firmware_log_free(struct ethosn_core *core)
{
	if (kfifo_initialized(&core->firmware_log))
		kfifo_free(&core->firmware_log);
}

/**
 * ethosn_debug_monitor_channel_free() - Free the debug_monitor_channel.
 * @core:	Pointer to Ethos-N core.
 */
static void ethosn_debug_monitor_channel_free(struct ethosn_core *core)
{
#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)
	del_timer(&core->debug_monitor_channel_timer);

	ethosn_dma_unmap_and_release(core->main_allocator,
				     core->debug_monitor_channel);
	core->debug_monitor_channel = NULL;

	ethosn_dma_unmap_and_release(core->main_allocator,
				     core->debug_monitor_channel_request);
	core->debug_monitor_channel_request = NULL;

	ethosn_dma_unmap_and_release(core->main_allocator,
				     core->debug_monitor_channel_response);
	core->debug_monitor_channel_response = NULL;
#endif
}

void ethosn_write_top_reg(struct ethosn_core *core,
			  const u32 page,
			  const u32 offset,
			  const u32 value)
{
	/* This function is called in a lot of places and we don't have
	 * a good way of reporting errors in some of them, so we
	 * just warn and ignore the write.
	 */
	if (WARN_ON(!core))
		return;

	if (WARN_ON(!core->top_regs))
		return;

	iowrite32(value, ethosn_top_reg_addr(core->top_regs, page, offset));
}

/* Exported for use by test module * */
EXPORT_SYMBOL(ethosn_write_top_reg);

u32 ethosn_read_top_reg(struct ethosn_core *core,
			const u32 page,
			const u32 offset)
{
	/* This function is called in a lot of places and we don't have
	 * a good way of reporting errors in some of them, so we warn
	 * and return a hopefully "safe" value to avoid crashing the
	 * whole kernel.
	 */
	if (WARN_ON(!core))
		return 0;

	if (WARN_ON(!core->top_regs))
		return 0;

	return ioread32(ethosn_top_reg_addr(core->top_regs, page, offset));
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_read_top_reg);

/**
 * ethosn_boot_firmware() - Boot firmware.
 * @core:	Pointer to Ethos-N core.
 *
 * Return: 0 on success, else error code.
 */
static int ethosn_boot_firmware(struct ethosn_core *core)
{
#ifdef ETHOSN_TZMP1
	dev_dbg(core->dev, "%s: Firmware boot through SMC.\n", __func__);

	return ethosn_smc_core_boot_firmware(core->dev, core->phys_addr);
#else
	struct dl1_sysctlr0_r sysctlr0 = { .word = 0 };

	dev_dbg(core->dev, "%s: Firmware boot through hardware directly.\n",
		__func__);

	/* Set firmware init address and release CPU wait */
	sysctlr0.bits.cpuwait = 0;
	sysctlr0.bits.initvtor =
		to_ethosn_addr(core->firmware_vtable_dma_addr,
			       &core->firmware_map) >>
		SYSCTLR0_INITVTOR_SHIFT;
	ethosn_write_top_reg(core, DL1_RP, DL1_SYSCTLR0, sysctlr0.word);

	return 0;
#endif
}

void ethosn_notify_firmware(struct ethosn_core *core)
{
	struct dl1_setirq_int_r irq = {
		.bits          = {
			.event = 1,
		}
	};

	ethosn_write_top_reg(core, DL1_RP, DL1_SETIRQ_INT,
			     irq.word);
}

#ifndef ETHOSN_NS

/**
 * ethosn_core_has_protected_allocator()
 * @core:	Pointer to Ethos-N core
 * Return:	True if assigned with a protected asset_allocator
 */
static bool ethosn_core_has_protected_allocator(struct ethosn_core *core)
{
	struct ethosn_device *ethosn = core->parent;

	return ethosn->asset_allocator[core->set_alloc_id]->is_protected;
}

#endif

static int ethosn_core_reset(struct ethosn_core *core,
			     bool hard_reset,
			     bool halt,
			     uint32_t alloc_id)
{
	const char *reset_type = hard_reset ? "hard" : "soft";

#ifdef ETHOSN_NS
	struct dl1_sysctlr0_r sysctlr0 = { .word = 0 };
	unsigned int timeout;

	core->set_is_protected = false;

	dev_info(core->dev, "%s reset the hardware directly.\n", reset_type);

	/* Initiate hard reset */
	if (hard_reset)
		sysctlr0.bits.hard_rstreq = 1;
	else
		/* Soft reset, allow new AXI requests DRAIN_DFC_ALLOW_AXI=1 */
		sysctlr0.bits.soft_rstreq = 1;

	ethosn_write_top_reg(core, DL1_RP, DL1_SYSCTLR0, sysctlr0.word);

	/* Wait for reset to complete */
	for (timeout = 0; timeout < ETHOSN_RESET_TIMEOUT_US;
	     timeout += ETHOSN_RESET_WAIT_US) {
		uint32_t reset_status;

		sysctlr0.word = ethosn_read_top_reg(core, DL1_RP, DL1_SYSCTLR0);

		if (hard_reset)
			reset_status = sysctlr0.bits.hard_rstreq;
		else
			reset_status = sysctlr0.bits.soft_rstreq;

		if (reset_status == 0)
			break;

		udelay(ETHOSN_RESET_WAIT_US);
	}

	if (timeout >= ETHOSN_RESET_TIMEOUT_US) {
		/* DL1_SYSCTLR0 might contain useful information (e.g.lockup) */
		uint32_t sysctlr0 = ethosn_read_top_reg(core, DL1_RP,
							DL1_SYSCTLR0);

		dev_err(core->dev,
			"Failed to %s reset the hardware. SYSCTLR0=0x%x\n",
			reset_type, sysctlr0);

		return -EFAULT;
	}

	return 0;

#else
	const bool smmu_available = core->parent->smmu_available;
	const struct ethosn_smc_aux_config aux_config = {
		.bits              = {
			.level_irq = core->force_firmware_level_interrupts,
			.stashing  = (ethosn_stashing_enabled() &&
				      smmu_available)
		}
	};

	dev_info(core->dev, "%s reset the hardware through SMC.\n", reset_type);
	core->set_is_protected = ethosn_core_has_protected_allocator(core);

	/*
	 * Access to DL1 registers is blocked in secure mode so reset is done
	 * with a SMC call. The call will block until the reset is done or
	 * timeout.
	 *
	 * Only the first asset allocator is being used right now so the
	 * allocator index is hardcoded to 0.
	 */
	if (ethosn_smc_core_reset(core->dev, core->phys_addr, alloc_id, halt,
				  hard_reset, core->set_is_protected,
				  &aux_config))
		return -ETIME;

	return 0;
#endif
}

int ethosn_reset(struct ethosn_core *core,
		 bool halt,
		 uint32_t alloc_id)
{
	int ret = -EINVAL;

	atomic_set(&core->is_configured, 0);

	ret = ethosn_core_reset(core, false, halt, alloc_id);
	if (ret)
		ret = ethosn_core_reset(core, true, halt, alloc_id);

	/* Halt reset will not configure the NPU */
	if (!ret && !halt)
		atomic_set(&core->is_configured, 1);

	return ret;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_reset);

void ethosn_set_power_ctrl(struct ethosn_core *core,
			   bool clk_on)
{
	struct dl1_pwrctlr_r pwrctlr = { .word = 0 };

	pwrctlr.bits.active = clk_on;
	ethosn_write_top_reg(core, DL1_RP, DL1_PWRCTLR, pwrctlr.word);
}

#ifdef ETHOSN_NS

/**
 * ethosn_get_smmu_stream_id() - get SMMU stream id.
 * @top_allocator:	Pointer to top level allocator.
 * @stream:		Stream type for which SMMU stream ID is requested for.
 * @id:			u32 for the stream id to go into.
 *
 * Return: Negative error code on error, zero otherwise
 */
static int ethosn_get_smmu_stream_id(struct ethosn_dma_allocator *top_allocator,
				     enum  ethosn_stream_type stream,
				     u32 *id)
{
	struct ethosn_dma_sub_allocator *sub_allocator;

	sub_allocator = ethosn_get_sub_allocator(top_allocator, stream);

	if (IS_ERR_OR_NULL(sub_allocator))
		return -EINVAL;

	*id = sub_allocator->smmu_stream_id;

	return 0;
}

/**
 * ethosn_set_smmu_stream_ids() - Configure the mmu stream IDs.
 * @core:	Pointer to Ethos-N core for main allocator
 * @alloc_id:	Index to specifc asset allocator to use
 *
 * Return: Negative error code on error, zero otherwise
 */
static int ethosn_set_smmu_stream_ids(struct ethosn_core *core,
				      uint32_t alloc_id)
{
	struct ethosn_device *ethosn = core->parent;

	struct ethosn_dma_allocator *main_alloc = core->main_allocator;
	struct ethosn_dma_allocator *asset_alloc;

	u32 smmu_stream_id;
	int ret = -EINVAL;

	if (alloc_id >= ethosn->num_asset_allocs)
		return ret;

	asset_alloc = ethosn->asset_allocator[alloc_id];

	/* Main Allocator */

	/* Firmware */
	ret = ethosn_get_smmu_stream_id(main_alloc, ETHOSN_STREAM_FIRMWARE,
					&smmu_stream_id);
	if (ret)
		return ret;

	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM0_MMUSID, smmu_stream_id);
	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM4_MMUSID, smmu_stream_id);

	/* Working Data */
	ret = ethosn_get_smmu_stream_id(main_alloc, ETHOSN_STREAM_WORKING_DATA,
					&smmu_stream_id);
	if (ret)
		return ret;

	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM1_MMUSID, smmu_stream_id);

	/* Asset Allocator */

	/* Command Stream */
	ret = ethosn_get_smmu_stream_id(asset_alloc,
					ETHOSN_STREAM_COMMAND_STREAM,
					&smmu_stream_id);
	if (ret)
		return ret;

	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM2_MMUSID, smmu_stream_id);

	/* Weight Data */
	ret = ethosn_get_smmu_stream_id(asset_alloc, ETHOSN_STREAM_WEIGHT_DATA,
					&smmu_stream_id);
	if (ret)
		return ret;

	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM5_MMUSID, smmu_stream_id);

	/* Input / Output Buffers */
	ret = ethosn_get_smmu_stream_id(asset_alloc, ETHOSN_STREAM_IO_BUFFER,
					&smmu_stream_id);
	if (ret)
		return ret;

	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM6_MMUSID, smmu_stream_id);
	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM8_MMUSID, smmu_stream_id);

	/* Intermediate Buffers */
	ret = ethosn_get_smmu_stream_id(asset_alloc,
					ETHOSN_STREAM_INTERMEDIATE_BUFFER,
					&smmu_stream_id);
	if (ret)
		return ret;

	ethosn_write_top_reg(core, DL1_RP, DL1_STREAM7_MMUSID, smmu_stream_id);

	return 0;
}

/**
 * ethosn_set_events() - Configure NPU events
 * @core:	Pointer to Ethos-N core
 */
static void ethosn_set_events(struct ethosn_core *core)
{
	const struct dl1_sysctlr1_r sysctlr1 = {
		.bits                 = {
			.mcu_setevnt  = 1,
			.tsu_evnt     = 1,
			.rxev_degroup = 1,
			.rxev_evnt    = 1,

			/* Configure the NPU to send interrupts to the
			 * NCU MCU when errors occur in the hardware.
			 * The firmware will dump some error information
			 * in the GP registers and we will print this out.
			 *
			 * Note that even though it is possible to handle
			 * these errors on the host side instead, we have
			 * less information available here (e.g. no access
			 * to TOP_ERR_CAUSE) so it is better for the
			 * firmware to handle it initially so that it
			 * can dump out more information.
			 */
			.err_tolr_irq = 1,
			.err_func_irq = 1,
			.err_recv_irq = 1,
		}
	};

	dev_dbg(core->dev,
		"Setting DL1_SYSCTLR1 to 0x%08x.\n", sysctlr1.word);

	ethosn_write_top_reg(core, DL1_RP, DL1_SYSCTLR1, sysctlr1.word);
}

/**
 * ethosn_set_aux_ctrl() - Configure auxiliary controls
 * @core:	Pointer to Ethos-N core
 */
static void ethosn_set_aux_ctrl(struct ethosn_core *core)
{
	const bool smmu_available = core->parent->smmu_available;
	struct dl1_auxctlr_r auxctlr;

	auxctlr.word = ethosn_read_top_reg(core, DL1_RP, DL1_AUXCTLR);
	auxctlr.bits.increase_outstanding_writes = 1U;

	/* Configure interrupt and stashing behavior */
	auxctlr.bits.dis_edgeirq = core->force_firmware_level_interrupts;
	if (ethosn_stashing_enabled() && smmu_available) {
		auxctlr.bits.stash_ahead = 5U;
		auxctlr.bits.stash_issue = 10U;
	}

	/* Disable error interrupts being sent to the host when the hardware
	 * runs into errors. Note these are different to the error interrupt
	 * sent by the firmware, which is still enabled.
	 * The firmware handles hardware errors itself and then informs us
	 * via the error interrupt, so we will still get informed but
	 * after the firmware has dumped some useful debugging information.
	 * Technically we could leave these interrupts enabled and then
	 * we would get informed as well as the firmware being informed,
	 * and there are some issues with this:
	 *  1 - the kernel might see the HW error before the firmware has
	 *      had a chance to dump more detailed error information.
	 *  2 - this seems to result in the host being spammed with
	 *      thousands of interrupts resulting in a hang. It's not
	 *      clear why this is happening.
	 *
	 * Note that "unrecoverable" errors (see HW error categorisation
	 * definitions) are left enabled as these cannot be handled by
	 * the firmware.
	 */
	auxctlr.bits.dis_ext_err_recoverable = 1;
	auxctlr.bits.dis_ext_err_functional = 1;
	auxctlr.bits.dis_ext_err_tolerable = 1;

	dev_dbg(core->dev,
		"Setting DL1_AUXCTLR to 0x%08x.\n", auxctlr.word);

	ethosn_write_top_reg(core, DL1_RP, DL1_AUXCTLR, auxctlr.word);
}

/**
 * ethosn_set_addr_ext() - Configure address extension for stream
 * @core:	Pointer to Ethos-N core
 * @stream:	stream to configure (must be 0-2)
 * @offset:	Offset to apply. Lower 29 bits will be ignored
 *
 * Return: Negative error code on error, zero otherwise
 */
static int ethosn_set_addr_ext(struct ethosn_core *core,
			       unsigned int stream,
			       ethosn_address_t offset)
{
	static const u32 stream_to_page[] = {
		DL1_STREAM0_ADDRESS_EXTEND,
		DL1_STREAM1_ADDRESS_EXTEND,
		DL1_STREAM2_ADDRESS_EXTEND,
	};
	struct dl1_stream0_address_extend_r ext = { .word = 0 };

	if (stream >= ARRAY_SIZE(stream_to_page)) {
		dev_err(core->dev, "Illegal stream %u for address extension.\n",
			stream);

		return -EINVAL;
	}

	ext.bits.addrextend = offset >> REGION_SHIFT;

	dev_dbg(core->dev,
		"NPU stream %u address extension set to 0x%llx.\n",
		stream, offset);
	ethosn_write_top_reg(core, DL1_RP, stream_to_page[stream],
			     ext.word);

	return 0;
}

#endif

/**
 * ethosn_set_addr_map() - Set address map to use for stream
 * @core:	Pointer to Ethos-N core
 * @stream:	stream to configure (must be 0-2)
 * @offset:	Offset to apply. Lower 29 bits will be ignored
 * @addr_map:	Address map to be used later to convert to Ethos-N addresses
 *
 * Return: Negative error code on error, zero otherwise
 */
static int ethosn_set_addr_map(struct ethosn_core *core,
			       unsigned int stream,
			       ethosn_address_t offset,
			       struct ethosn_addr_map *addr_map)
{
	static const u32 stream_to_offset[] = {
		0,
		REGION_EXT_RAM0 << REGION_SHIFT,
		REGION_EXT_RAM1 << REGION_SHIFT,
	};

	if (stream >= ARRAY_SIZE(stream_to_offset)) {
		dev_err(core->dev, "Illegal stream %u for address map.\n",
			stream);

		return -EINVAL;
	}

	if (!addr_map) {
		dev_err(core->dev, "Invalid address map location.\n");

		return -EINVAL;
	}

	if (addr_map) {
		addr_map->region = stream_to_offset[stream];
		addr_map->extension = offset & ~ETHOSN_REGION_MASK;
	}

	return 0;
}

static int get_gp_offset(struct ethosn_core *core,
			 unsigned int index)
{
	static const int index_to_offset[] = {
		DL1_GP0,
		DL1_GP1,
		DL1_GP2,
		DL1_GP3,
		DL1_GP4,
		DL1_GP5,
		DL1_GP6,
		DL1_GP7
	};

	if (index >= ARRAY_SIZE(index_to_offset)) {
		dev_err(core->dev,
			"Illegal index %u of general purpose register.\n",
			index);

		return -EFAULT;
	}

	return index_to_offset[index];
}

void ethosn_dump_gps(struct ethosn_core *core)
{
	int offset;
	unsigned int i;

	for (i = 0; i < 8; i++) {
		offset = get_gp_offset(core, i);
		if (offset < 0)
			break;

		dev_err(core->dev,
			"GP%u=0x%08x\n",
			i, ethosn_read_top_reg(core, DL1_RP, offset));
	}
}

/****************************************************************************
 * Mailbox
 ****************************************************************************/

/**
 * ethosn_read_message() - Read message from queue.
 * @queue:	Pointer to queue.
 * @header:	Pointer to message header.
 * @data:	Pointer to data buffer.
 * @length:	Maximum length of data buffer.
 *
 * Return: Number of messages read on success, else error code.
 */
int ethosn_read_message(struct ethosn_core *core,
			struct ethosn_message_header *header,
			void *data,
			size_t length)
{
	struct ethosn_queue *queue = core->mailbox_response->cpu_addr;
	bool ret;

	ethosn_dma_sync_for_cpu(core->main_allocator, core->mailbox_response);

	ret = ethosn_queue_read(queue, (uint8_t *)header,
				sizeof(struct ethosn_message_header));
	if (!ret)
		return 0;

	dev_dbg(core->dev,
		"Received message. type=%u, length=%u, read=%u, write=%u.\n",
		header->type, header->length, queue->read,
		queue->write);

	if (length < header->length) {
		dev_err(core->dev,
			"Message too large to read. header.length=%u, length=%zu.\n",
			header->length, length);

		ethosn_queue_skip(queue, header->length);

		return -ENOMEM;
	}

	/*
	 * We assume that if there is any data in the queue at all
	 * then the full message is available. Partial messages should not be
	 * observable, as the firmware updates its write pointer only
	 * once the full message is written.
	 */
	ret = ethosn_queue_read(queue, data, header->length);
	if (!ret) {
		dev_err(core->dev,
			"Failed to read message payload. size=%u, queue capacity=%u\n",
			header->length, queue->capacity);

		return -EFAULT;
	}

	ethosn_dma_sync_for_device(core->main_allocator,
				   core->mailbox_response);

	if (core->profiling.config.enable_profiling)
		++core->profiling.mailbox_messages_received;

	return 1;
}

/* Exported for use by test module * */
EXPORT_SYMBOL(ethosn_read_message);

/**
 * ethosn_write_message() - Write message to queue.
 * @queue:	Pointer to queue.
 * @type:	Message type.
 * @data:	Pointer to data buffer.
 * @length:	Length of data buffer.
 *
 * Return: 0 on success, else error code.
 */
int ethosn_write_message(struct ethosn_core *core,
			 enum ethosn_message_type type,
			 void *data,
			 size_t length)
{
	struct ethosn_queue *queue = core->mailbox_request->cpu_addr;
	struct ethosn_message_header header = {
		.type   = type,
		.length = length
	};
	bool ret;
	uint32_t write_pending;
	const uint8_t *buffers[2] = {
		(const uint8_t *)&header, (const uint8_t *)data
	};
	uint32_t sizes[2] = { sizeof(header), length };

	ethosn_dma_sync_for_cpu(core->main_allocator, core->mailbox_request);

	dev_dbg(core->dev,
		"Write message. type=%u, length=%zu, read=%u, write=%u.\n",
		type, length, queue->read, queue->write);

	ret =
		ethosn_queue_write(queue, buffers, sizes, 2,
				   &write_pending);
	if (!ret)
		return ret;

	/*
	 * Sync the data before committing the updated write pointer so that
	 * the "reading" side (e.g. CU firmware) can't read invalid data.
	 */
	ethosn_dma_sync_for_device(core->main_allocator, core->mailbox_request);

	/*
	 * Update the write pointer after all the data has been written.
	 */
	queue->write = write_pending;

	/* Sync the write pointer */
	ethosn_dma_sync_for_device(core->main_allocator, core->mailbox_request);
	ethosn_notify_firmware(core);

	if (core->profiling.config.enable_profiling)
		++core->profiling.mailbox_messages_sent;

	return 0;
}

/* Exported for use by test module * */
EXPORT_SYMBOL(ethosn_write_message);

int ethosn_send_fw_hw_capabilities_request(struct ethosn_core *core)
{
	/* If it's a firmware reboot (i.e. capabilities have been
	 * already received once) don't request caps again
	 */
	if (core->fw_and_hw_caps.size > 0U)
		return 0;

	dev_dbg(core->dev, "-> FW & HW capabilities\n");

	return ethosn_write_message(core, ETHOSN_MESSAGE_FW_HW_CAPS_REQUEST,
				    NULL, 0);
}

/*
 * Note we do not use the profiling config in ethosn->profiling, because if we
 * are in the process of updating that, it may not yet have been committed.
 * Instead we take the arguments explicitly.
 */
static int ethosn_send_configure_profiling(struct ethosn_core *core,
					   bool enable,
					   uint32_t num_hw_counters,
					   enum
					   ethosn_profiling_hw_counter_types *
					   hw_counters,
					   struct ethosn_dma_info *buffer)
{
	struct ethosn_firmware_profiling_configuration fw_new_config = { 0 };
	int i;

	if (num_hw_counters > ETHOSN_PROFILING_MAX_HW_COUNTERS) {
		dev_err(core->dev,
			"Invalid number of hardware profiling counters\n");

		return -EINVAL;
	}

	if (!IS_ERR_OR_NULL(buffer)) {
		fw_new_config.buffer_size = buffer->size;
		fw_new_config.buffer_address =
			to_ethosn_addr(buffer->iova_addr,
				       &core->work_data_map);
		if (IS_ERR_VALUE((unsigned long)fw_new_config.buffer_address)) {
			dev_err(core->dev,
				"Error converting firmware profiling buffer to_ethosn_addr.\n");

			return -EFAULT;
		}

		fw_new_config.num_hw_counters = num_hw_counters;
		for (i = 0; i < num_hw_counters; ++i)
			fw_new_config.hw_counters[i] = hw_counters[i];
	} else {
		fw_new_config.buffer_address = 0;
		fw_new_config.buffer_size = 0;
	}

	/* Enable profiling only after all the configs have been checked
	 * and set
	 */
	fw_new_config.enable_profiling = enable;

	dev_dbg(core->dev,
		"-> Configure profiling, enable_profiling=%d, buffer_address=0x%08llx, buffer_size=%d\n",
		fw_new_config.enable_profiling, fw_new_config.buffer_address,
		fw_new_config.buffer_size);

	return ethosn_write_message(core, ETHOSN_MESSAGE_CONFIGURE_PROFILING,
				    &fw_new_config, sizeof(fw_new_config));
}

int ethosn_configure_firmware_profiling(struct ethosn_core *core,
					struct ethosn_profiling_config *
					new_config)
{
	int ret = -ENOMEM;

	/* If we are already waiting for the firmware to acknowledge use of a
	 * new buffer then we cannot allocate another.
	 * We must wait for it to acknowledge first.
	 */
	if (core->profiling.is_waiting_for_firmware_ack) {
		dev_err(core->dev,
			"Already waiting for firmware to acknowledge new profiling config.\n");

		ret = -EINVAL;
		goto ret;
	}

	/* Allocate new profiling buffer.
	 * Note we do not overwrite the existing buffer yet, as the firmware may
	 * still be using it
	 */
	if (new_config->enable_profiling &&
	    new_config->firmware_buffer_size > 0) {
		struct ethosn_profiling_buffer *buffer;

		core->profiling.firmware_buffer_pending =
			ethosn_dma_alloc_and_map(
				core->main_allocator,
				new_config->firmware_buffer_size,
				ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
				ETHOSN_STREAM_WORKING_DATA,
				GFP_KERNEL,
				"profiling-firmware-buffer");
		if (IS_ERR(core->profiling.firmware_buffer_pending)) {
			dev_err(core->dev,
				"Error allocating firmware profiling buffer.\n");

			goto ret;
		}

		/* Initialize the firmware_write_index. */
		buffer =
			(struct ethosn_profiling_buffer *)
			core->profiling.firmware_buffer_pending->cpu_addr;
		buffer->firmware_write_index = 0;
		ethosn_dma_sync_for_device(
			core->main_allocator,
			core->profiling.firmware_buffer_pending);
	} else {
		core->profiling.firmware_buffer_pending = NULL;
	}

	core->profiling.is_waiting_for_firmware_ack = true;

	ret = ethosn_send_configure_profiling(
		core, new_config->enable_profiling,
		new_config->num_hw_counters,
		new_config->hw_counters,
		core->profiling.firmware_buffer_pending);
	if (ret != 0) {
		dev_err(core->dev, "ethosn_send_configure_profiling failed.\n");

		goto free_buf;
	}

	return 0;

free_buf:
	ethosn_dma_unmap_and_release(
		core->main_allocator,
		&core->profiling.firmware_buffer_pending);
ret:

	return ret;
}

int ethosn_configure_firmware_profiling_ack(struct ethosn_core *core)
{
	if (!core->profiling.is_waiting_for_firmware_ack) {
		dev_err(core->dev,
			"Unexpected configure profiling ack from firmware.\n");

		return -EINVAL;
	}

	/* We can now free the old buffer (if any), as we know the firmware is
	 * no longer writing to it
	 */
	ethosn_dma_unmap_and_release(core->main_allocator,
				     &core->profiling.firmware_buffer);

	/* What used to be the pending buffer is now the proper one. */
	core->profiling.firmware_buffer =
		core->profiling.firmware_buffer_pending;
	core->profiling.firmware_buffer_pending = NULL;
	core->profiling.is_waiting_for_firmware_ack = false;
	core->profiling.wall_clock_time_at_firmware_zero =
		ktime_get_real_ns();

	return 0;
}

int ethosn_send_ping(struct ethosn_core *core)
{
	dev_dbg(core->dev, "-> Ping\n");

	return ethosn_write_message(core, ETHOSN_MESSAGE_PING, NULL, 0);
}

int ethosn_send_inference(struct ethosn_core *core,
			  dma_addr_t buffer_array,
			  uint64_t user_arg)
{
	struct ethosn_message_inference_request request;

	request.buffer_array = to_ethosn_addr(buffer_array, &core->dma_map);
	request.user_argument = user_arg;

	dev_dbg(core->dev,
		"-> Inference. buffer_array=0x%08llx, user_args=0x%llx\n",
		request.buffer_array, request.user_argument);

	return ethosn_write_message(core, ETHOSN_MESSAGE_INFERENCE_REQUEST,
				    &request,
				    sizeof(request));
}

/****************************************************************************
 * Firmware
 ****************************************************************************/

/**
 * firmware_dma_map() - DMA map firmware
 * @core:		Pointer to Ethos-N core with firmware to DMA map
 * @unpriv_stack_offset:Offset to the unprivileged stack in the loaded firmware
 * @end:		Offset to the end of the loaded firmware
 *
 * Return: 0 on success, else error code.
 */
static int firmware_dma_map(struct ethosn_core *core,
			    size_t unpriv_stack_offset,
			    size_t end)
{
	struct ethosn_dma_prot_range prot_ranges[2];
	int ret;

	/* Code, PLE kernels and vector table can be read-only */
	prot_ranges[0].start = 0;
	prot_ranges[0].end = unpriv_stack_offset;
	prot_ranges[0].prot = ETHOSN_PROT_READ;
	/* Stacks need to be writable */
	prot_ranges[1].start = unpriv_stack_offset;
	prot_ranges[1].end = end;
	prot_ranges[1].prot = ETHOSN_PROT_READ | ETHOSN_PROT_WRITE;

	ret = ethosn_dma_map_with_prot_ranges(core->main_allocator,
					      core->firmware,
					      prot_ranges,
					      ARRAY_SIZE(prot_ranges));
	if (ret < 0) {
		dev_err(core->dev,
			"%s: ethosn_dma_map_with_prot_ranges failed: %d\n",
			__func__, ret);

		return ret;
	}

	return 0;
}

#ifdef ETHOSN_TZMP1

/**
 * protected_firmware_map() - Map protected firmware
 * @core:	Pointer to Ethos-N core
 *
 * Return: 0 on success, else error code.
 */
static int protected_firmware_map(struct ethosn_core *core)
{
	const struct ethosn_protected_firmware *firmware =
		&core->parent->protected_firmware;
	int ret;

	/*
	 * The protected firmware will always be the same so do nothing if it's
	 * already setup.
	 */
	if (core->firmware) {
		dev_info(core->dev, "Protected firmware already mapped\n");

		return 0;
	}

	dev_info(core->dev, "Mapping protected firmware\n");

	core->firmware = ethosn_dma_firmware_from_protected(
		core->main_allocator, firmware->addr, firmware->size);
	if (IS_ERR(core->firmware)) {
		ret = PTR_ERR(core->firmware);
		dev_err(core->dev,
			"%s: Failed to setup DMA for firmware in protected memory: %d\n",
			__func__, ret);
		goto error;
	}

	ret = firmware_dma_map(core, firmware->stack_offset, firmware->size);
	if (ret) {
		dev_err(core->dev,
			"%s: Failed to DMA map firmware in protected memory: %d\n",
			__func__, ret);
		goto free_dma_info;
	}

	return 0;

free_dma_info:
	ethosn_dma_release(core->main_allocator, &core->firmware);
error:
	core->firmware = NULL;

	return ret;
}

#else

static bool validate_big_fw_header(struct ethosn_core *core,
				   struct ethosn_big_fw *big_fw)
{
	struct dl1_npu_id_r npu_id;
	uint32_t arch;

	if (big_fw->fw_magic != ETHOSN_BIG_FW_MAGIC) {
		dev_err(core->dev,
			"Unable to identify BIG FW. Invalid magic number: 0x%04x\n",
			big_fw->fw_magic);

		return false;
	}

	if (big_fw->fw_ver_major != ETHOSN_FIRMWARE_VERSION_MAJOR) {
		dev_err(core->dev,
			"Unsupported BIG FW version: %u.%u.%u. Version %u.x.x is required.\n",
			big_fw->fw_ver_major, big_fw->fw_ver_minor,
			big_fw->fw_ver_patch, ETHOSN_FIRMWARE_VERSION_MAJOR);

		return false;
	}

	npu_id.word = ethosn_read_top_reg(core, DL1_RP, DL1_NPU_ID);
	arch = npu_id.bits.arch_major << 24 |
	       npu_id.bits.arch_minor << 16 |
	       npu_id.bits.arch_rev;

	dev_dbg(core->dev,
		"NPU reported arch version %u.%u.%u. BIG FW Magic: 0x%04x, version: %u.%u.%u\n",
		npu_id.bits.arch_major,
		npu_id.bits.arch_minor,
		npu_id.bits.arch_rev,
		big_fw->fw_magic,
		big_fw->fw_ver_major,
		big_fw->fw_ver_minor,
		big_fw->fw_ver_patch);

	if (big_fw->arch_min > arch || arch > big_fw->arch_max) {
		dev_err(core->dev, "BIG FW is not compatible.\n");

		return false;
	}

	return true;
}

/**
 * firmware_load() - Load firmware binary
 * @core:	Pointer to Ethos-N core
 *
 * Return: 0 on success, else error code.
 */
static int firmware_load(struct ethosn_core *core)
{
	const struct firmware *fw;
	struct ethosn_big_fw *big_fw;
	int ret = -ENOMEM;
	uint32_t load_src_offset = 0;
	uint32_t load_src_size = 0;
	bool is_first_core = (core == core->parent->core[0]);
	bool is_carveout_second_core = !core->parent->smmu_available &&
				       !is_first_core;

	dev_dbg(core->dev, "Requesting firmware\n");

	/* Request firmware binary */
	ret = request_firmware(&fw, "ethosn.bin", core->parent->dev);
	if (ret) {
		dev_err(core->dev,
			"No firmware found. Was looking for ethosn.bin.\n");

		return ret;
	}

	big_fw = (struct ethosn_big_fw *)fw->data;

	if (!validate_big_fw_header(core, big_fw)) {
		ret = -EINVAL;
		goto release_fw;
	}

	dev_info(core->dev,
		 "Found FW. arch_min=0x%08x, arch_max=0x%08x, offset=0x%08x, size=0x%08x\n",
		 big_fw->arch_min,
		 big_fw->arch_max,
		 big_fw->offset,
		 big_fw->size);
	dev_dbg(core->dev, "Firmware asset offsets+sizes:\n");

	dev_dbg(core->dev, "  Code: 0x%08x + 0x%08x\n",
		big_fw->code_offset,
		big_fw->code_size);
	dev_dbg(core->dev, "  PLE: 0x%08x + 0x%08x\n",
		big_fw->ple_offset,
		big_fw->ple_size);
	dev_dbg(core->dev, "  Vector table: 0x%08x + 0x%08x\n",
		big_fw->vector_table_offset,
		big_fw->vector_table_size);
	dev_dbg(core->dev, "  Unpriv stack: 0x%08x + 0x%08x\n",
		big_fw->unpriv_stack_offset,
		big_fw->unpriv_stack_size);
	dev_dbg(core->dev, "  Priv stack: 0x%08x + 0x%08x\n",
		big_fw->priv_stack_offset,
		big_fw->priv_stack_size);

	/* First check that the assets are in the expected
	 * order, otherwise the following logic might be wrong.
	 */
	if (big_fw->code_offset >= big_fw->ple_offset ||
	    big_fw->ple_offset >= big_fw->vector_table_offset ||
	    big_fw->vector_table_offset >= big_fw->unpriv_stack_offset ||
	    big_fw->unpriv_stack_offset >= big_fw->priv_stack_offset) {
		dev_err(core->dev, "%s: Firmware assets in wrong order\n",
			__func__);
		ret = -EINVAL;
		goto release_fw;
	}

	/* For carveout dual core, we re-use the code loaded for the first core
	 * to run the second core, and thus avoid having to use
	 * position-independent code in the firmware. We therefore only need
	 * to load the vector table and stacks into memory for the second core.
	 */
	load_src_offset =
		is_carveout_second_core ? big_fw->vector_table_offset : 0;
	load_src_size = is_carveout_second_core ? big_fw->size -
			big_fw->vector_table_offset : big_fw->size;

	/* Allocate space for the parts of the binary that we need to load.
	 * No mapping yet - we do that later as each asset has different flags.
	 * We still need to do the allocation in one go though, as the firmware
	 * layout in memory needs to be identical to the layout of the binary,
	 * as the firmware code makes assumptions about this.
	 */
	core->firmware = ethosn_dma_alloc(core->main_allocator,
					  load_src_size,
					  ETHOSN_STREAM_FIRMWARE,
					  GFP_KERNEL,
					  "firmware");

	if (IS_ERR_OR_NULL(core->firmware)) {
		dev_err(core->dev, "%s: ethosn_dma_alloc failed: %ld\n",
			__func__, PTR_ERR(core->firmware));
		ret = -ENOMEM;
		goto release_fw;
	}

	ethosn_dma_sync_for_cpu(core->main_allocator, core->firmware);

	/* Copy firmware binary into the allocation */
	memcpy(core->firmware->cpu_addr,
	       fw->data + big_fw->offset + load_src_offset,
	       load_src_size);
	ethosn_dma_sync_for_device(core->main_allocator, core->firmware);

	/* Map each asset (separately, as we need different protection
	 * for some assets)
	 */
	ret = firmware_dma_map(core,
			       big_fw->unpriv_stack_offset - load_src_offset,
			       load_src_size);
	if (ret < 0) {
		dev_err(core->dev, "%s: Failed to DMA map firmware: %d\n",
			__func__, ret);
		goto free_firmware;
	}

	/* Firmware must be allocated at the start of the carveout/smmu region,
	 * so
	 * that assumptions made in the MPU setup (and other places) are valid.
	 * For core 1 in a dual-core carveout setup though, this can't happen
	 * and we sacrifice some security in the MPU setup.
	 */
	if (!is_carveout_second_core &&
	    (core->firmware->iova_addr & ETHOSN_REGION_MASK) != 0) {
		dev_err(core->dev,
			"ethosn_dma_alloc for firmware on core 0 must be at address zero\n");
		ret = -EINVAL;
		goto release_fw;
	}

	/* Remember the vector table address, as we'll need this to boot the
	 * firmware.
	 */
	core->firmware_vtable_dma_addr = core->firmware->iova_addr +
					 big_fw->vector_table_offset -
					 load_src_offset;

	/* For core 1 in a dual-core carveout setup, the vector table hardcoded
	 * into firmware binary will point to the first core's privileged stack,
	 * which we need to fix up to instead point to the second core's
	 * privileged stack which we just allocated.
	 */
	if (is_carveout_second_core) {
		uint32_t *vtable = core->firmware->cpu_addr +
				   big_fw->vector_table_offset -
				   load_src_offset;
		/* Calculate bottom of the privileged stack */
		uint32_t new_stack_pointer =
			(core->firmware->iova_addr + big_fw->priv_stack_offset +
			 big_fw->priv_stack_size - load_src_offset) &
			ETHOSN_REGION_MASK;

		ethosn_dma_sync_for_cpu(core->main_allocator, core->firmware);

		dev_dbg(core->dev,
			"Updating carveout second core vtable stack pointer from 0x%x to 0x%x\n",
			vtable[0], new_stack_pointer);
		vtable[0] = new_stack_pointer;

		ethosn_dma_sync_for_device(core->main_allocator,
					   core->firmware);
	}

	release_firmware(fw);

	return 0;

free_firmware:
	ethosn_dma_release(core->main_allocator, &core->firmware);

release_fw:
	release_firmware(fw);

	return ret;
}

#endif

int ethosn_reset_and_start_ethosn(struct ethosn_core *core,
				  uint32_t alloc_id)
{
	int timeout;
	int ret;
	struct ethosn_device *parent = core->parent;
	const ethosn_address_t firmware_base_addr =
		ethosn_dma_get_addr_base(core->main_allocator,
					 ETHOSN_STREAM_FIRMWARE);
	const ethosn_address_t work_data_base_addr =
		ethosn_dma_get_addr_base(core->main_allocator,
					 ETHOSN_STREAM_WORKING_DATA);
	const ethosn_address_t command_stream_base_addr =
		ethosn_dma_get_addr_base(parent->asset_allocator[alloc_id],
					 ETHOSN_STREAM_COMMAND_STREAM);

	dev_info(core->dev, "Reset core device\n");

	core->firmware_running = false;

	/* Clear any outstanding configuration */
	if (core->profiling.is_waiting_for_firmware_ack) {
		ret = ethosn_configure_firmware_profiling_ack(core);
		if (ret)
			return ret;
	}

	/* Record which asset allocator the core is being programmed for */
	core->set_alloc_id = alloc_id;

	/* Reset the Ethos-N core. Note that this doesn't run the NCU MCU,
	 * so the firmware won't yet be executing code.
	 */
	ret = ethosn_reset(core, false, alloc_id);
	if (ret)
		return ret;

	/* In a secure build, TF-A will perform the setup below */
#ifdef ETHOSN_NS
	/* Setup the SMMU Stream IDs if iommu is present */
	if (parent->smmu_available) {
		ret = ethosn_set_smmu_stream_ids(core, alloc_id);
		if (ret)
			return ret;
	}

	/* Configure address extension for stream 0, 1 and 2 */
	ret = ethosn_set_addr_ext(core, ETHOSN_STREAM_FIRMWARE,
				  firmware_base_addr);
	if (ret)
		return ret;

	ret = ethosn_set_addr_ext(core, ETHOSN_STREAM_WORKING_DATA,
				  work_data_base_addr);
	if (ret)
		return ret;

	ret = ethosn_set_addr_ext(core, ETHOSN_STREAM_COMMAND_STREAM,
				  command_stream_base_addr);
	if (ret)
		return ret;

	/* Configure NPU events */
	ethosn_set_events(core);

	/* Configure Auxiliary controls */
	ethosn_set_aux_ctrl(core);
#endif

	/* Setup maps for address translation */
	ret = ethosn_set_addr_map(core, ETHOSN_STREAM_FIRMWARE,
				  firmware_base_addr, &core->firmware_map);
	if (ret)
		return ret;

	ret = ethosn_set_addr_map(core, ETHOSN_STREAM_WORKING_DATA,
				  work_data_base_addr, &core->work_data_map);
	if (ret)
		return ret;

	ret = ethosn_set_addr_map(core, ETHOSN_STREAM_COMMAND_STREAM,
				  command_stream_base_addr, &core->dma_map);
	if (ret)
		return ret;

	/* Initialize the mailbox */
	ret = ethosn_mailbox_init(core);
	if (ret)
		return ret;

	/* Initialize the debug_monitor_channel */
	ret = ethosn_debug_monitor_channel_init(core);
	if (ret)
		return ret;

	/* Init memory regions */
	ethosn_write_top_reg(core, DL1_RP, GP_MAILBOX_SIZE,
			     core->mailbox_size);
	ethosn_write_top_reg(core, DL1_RP, GP_COMMAND_STREAM_SIZE,
			     ethosn_dma_get_addr_size(
				     parent->asset_allocator[0],
				     ETHOSN_STREAM_COMMAND_STREAM));

	/* Clear GP_BOOT_SUCCESS so we know when it is set by the firmware*/
	ethosn_write_top_reg(core, DL1_RP, GP_BOOT_SUCCESS, 0);

	/* Boot the firmware */
	ret = ethosn_boot_firmware(core);
	if (ret)
		return ret;

	dev_info(core->dev, "Waiting for core device\n");

	/* Wait for firmware to set GP_BOOT_SUCCESS which indicates that it has
	 * booted
	 */
	for (timeout = 0; timeout < ETHOSN_RESET_TIMEOUT_US;
	     timeout += ETHOSN_RESET_WAIT_US) {
		if (ethosn_read_top_reg(core, DL1_RP,
					GP_BOOT_SUCCESS) ==
		    ETHOSN_FIRMWARE_BOOT_SUCCESS_MAGIC)
			break;

		udelay(ETHOSN_RESET_WAIT_US);
	}

	if (timeout >= ETHOSN_RESET_TIMEOUT_US) {
		/* DL1_SYSCTLR0 might contain useful information (e.g.lockup) */
		uint32_t sysctlr0 = ethosn_read_top_reg(core, DL1_RP,
							DL1_SYSCTLR0);

		dev_err(core->dev,
			"Timeout while waiting for core device. SYSCTLR0=0x%x.\n",
			sysctlr0);

		return -ETIME;
	}

	/* Firmware is now up and running */
	core->firmware_running = true;

	/* Ping firmware */
	ret = ethosn_send_ping(core);
	if (ret != 0)
		return ret;

	/* Send FW and HW capabilities request */
	ret = ethosn_send_fw_hw_capabilities_request(core);
	if (ret != 0)
		return ret;

	/* Set FW's profiling state. This is also set whenever profiling is
	 * enabled/disabled, but we need to do it on each reboot in case
	 * the firmware crashes, so that its profiling state is restored.
	 */
	ret = ethosn_configure_firmware_profiling(core,
						  &core->profiling.config);

	if (ret != 0)
		return ret;

	return 0;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_reset_and_start_ethosn);

/**
 * ethosn_firmware_deinit - Free firmware resources.
 * @core:		Pointer to Ethos-N core.
 */
static void ethosn_firmware_deinit(struct ethosn_core *core)
{
	ethosn_dma_unmap_and_release(core->main_allocator, &core->firmware);
}

/****************************************************************************
 * Debugfs
 ****************************************************************************/

/**
 * mailbox_fops_read - Mailbox read file operation.
 * @file:		File handle.
 * @buf_user:		User space buffer.
 * @count:		Size of user space buffer.
 * @position:		Current file position.
 *
 * Return: Number of bytes read, else error code.
 */
static ssize_t mailbox_fops_read(struct file *file,
				 char __user *buf_user,
				 size_t count,
				 loff_t *position)
{
	struct ethosn_core *core = file->f_inode->i_private;
	char buf[200];
	size_t n = 0;
	int ret;

	ret = mutex_lock_interruptible(&core->mutex);
	if (ret)
		return ret;

	if (core->mailbox_request) {
		struct ethosn_queue *queue = core->mailbox_request->cpu_addr;

		ethosn_dma_sync_for_cpu(core->main_allocator,
					core->mailbox_request);

		n += scnprintf(&buf[n], sizeof(buf) - n,
			       "Request queue : %llx\n",
			       core->mailbox_request->iova_addr);
		n += scnprintf(&buf[n], sizeof(buf) - n, "    capacity  : %u\n",
			       queue->capacity);
		n += scnprintf(&buf[n], sizeof(buf) - n, "    read      : %u\n",
			       queue->read);
		n += scnprintf(&buf[n], sizeof(buf) - n, "    write     : %u\n",
			       queue->write);
	}

	if (core->mailbox_response) {
		struct ethosn_queue *queue = core->mailbox_response->cpu_addr;

		ethosn_dma_sync_for_cpu(core->main_allocator,
					core->mailbox_response);

		n += scnprintf(&buf[n], sizeof(buf) - n,
			       "Response queue: %llx\n",
			       core->mailbox_response->iova_addr);
		n += scnprintf(&buf[n], sizeof(buf) - n, "    capacity  : %u\n",
			       queue->capacity);
		n += scnprintf(&buf[n], sizeof(buf) - n, "    read      : %u\n",
			       queue->read);
		n += scnprintf(&buf[n], sizeof(buf) - n, "    write     : %u\n",
			       queue->write);
	}

	if (core->mailbox) {
		struct ethosn_mailbox *mailbox = core->mailbox->cpu_addr;

		ethosn_dma_sync_for_cpu(core->main_allocator, core->mailbox);

		n += scnprintf(&buf[n], sizeof(buf) - n, "Severity      : %u\n",
			       mailbox->severity);
	}

	mutex_unlock(&core->mutex);

	return simple_read_from_buffer(buf_user, count, position, buf, n);
}

/**
 * firmware_log_fops_read - Firmware log read file operation.
 * @file:		File handle.
 * @buf_user:		User space buffer.
 * @count:		Size of user space buffer.
 * @position:		Current file position.
 *
 * Return: Number of bytes read, else error code.
 */
static ssize_t firmware_log_fops_read(struct file *file,
				      char __user *buf_user,
				      size_t count,
				      loff_t *position)
{
	struct ethosn_core *core = file->f_inode->i_private;
	ssize_t ret;
	unsigned int copied;

	/* If no data available, block until some arrives, unless the
	 * non-blocking flag is set.
	 */
	if (kfifo_is_empty(&core->firmware_log)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(
			    core->firmware_log_read_poll_wqh,
			    !kfifo_is_empty(&core->firmware_log)))
			return -EINTR; /* In case of Ctrl-C, for example */
	}

	ret = kfifo_to_user(&core->firmware_log, buf_user, count,
			    &copied);

	if (ret != 0)
		return ret;

	return copied;
}

/**
 * firmware_log_fops_poll - Called when a userspace process polls the
 *			    firmware_log debugfs entry,
 *                          to check if data can be read from it
 */
static __poll_t firmware_log_fops_poll(struct file *file,
				       struct poll_table_struct *wait)
{
	struct ethosn_core *core = file->f_inode->i_private;

	/* Register our wait queue. This will be signalled when new log
	 * messages arrive
	 */
	poll_wait(file, &core->firmware_log_read_poll_wqh, wait);

	/* Check if data is available for reading.
	 */
	if (!kfifo_is_empty(&core->firmware_log))
		return (EPOLLIN | EPOLLRDNORM);

	return 0;
}

/**
 * firmware_profiling_read - Called when a userspace process reads the
 *			     firmware_profiling debugfs entry,
 *                           to retrieve profiling entries.
 *
 * The kernel maintains the user's fd offset as normal and this function
 * handles mapping that offset into the circular buffer.
 * It is not possible for the fd read offset to "overtake" the firmware's
 * write pointer (the function prevents it) - this means userspace
 * can never read into uninitialised data or read older entries that it has
 * already seen.
 * When the fd offset reaches the size of the buffer, it will keep increasing
 * beyond the size, but read operations will interpret this as modulo the
 * buffer size. There is no mechanism in place to prevent the firmware
 * write pointer from overtaking any of the userspace fd offsets
 * (which is deliberate - we don't want to stall the firmware based on any
 * user-space processes not reading profiling data fast enough).
 * This means that it is possible for a process reading from the fd to observe
 * a "skip" in the data if it is not reading it fast enough.
 *
 * @file:		File handle.
 * @buf_user:		User space buffer.
 * @count:		Size of user space buffer.
 * @position:		Current file position.
 *
 * Return: Number of bytes read, else error code.
 */
static ssize_t firmware_profiling_read(struct file *file,
				       char __user *buf_user,
				       size_t count,
				       loff_t *position)
{
	struct ethosn_core *core = file->f_inode->i_private;
	ssize_t ret;
	ssize_t num_bytes_read;
	size_t buffer_entries_offset;
	size_t buffer_entries_count;
	size_t buffer_entries_size_bytes;
	loff_t read_buffer_offset;
	struct ethosn_profiling_buffer *buffer;
	uint32_t firmware_write_offset;

	/* Make sure the profiling buffer isn't deallocated underneath us */
	ret = mutex_lock_interruptible(&core->mutex);
	if (ret != 0)
		return ret;

	/* Report error if profiling is not enabled (i.e. no profiling buffer
	 * allocated)
	 */
	if (IS_ERR_OR_NULL(core->profiling.firmware_buffer)) {
		ret = -EINVAL;
		goto cleanup;
	}

	ethosn_dma_sync_for_cpu(core->main_allocator,
				core->profiling.firmware_buffer);

	/* Calculate size etc. of the buffer. */
	buffer =
		(struct ethosn_profiling_buffer *)
		core->profiling.firmware_buffer->cpu_addr;

	buffer_entries_offset =
		offsetof(struct ethosn_profiling_buffer, entries);
	buffer_entries_count =
		(core->profiling.config.firmware_buffer_size -
		 buffer_entries_offset) /
		sizeof(struct ethosn_profiling_entry);
	buffer_entries_size_bytes = buffer_entries_count *
				    sizeof(struct ethosn_profiling_entry);

	/* Convert from file offset to position in the buffer.
	 * This accounts for the fact that the buffer is circular so the file
	 * offset may be larger than the actual buffer size.
	 */
	read_buffer_offset = *position % buffer_entries_size_bytes;

	/* Copy firmware_write_index as the firmware may write to this in the
	 * background.
	 */
	firmware_write_offset = buffer->firmware_write_index *
				sizeof(struct ethosn_profiling_entry);

	if (read_buffer_offset < firmware_write_offset) {
		/* Firmware has written data further down the buffer, but not
		 * enough to wrap around.
		 */
		num_bytes_read = simple_read_from_buffer(buf_user, count,
							 &read_buffer_offset,
							 buffer->entries,
							 firmware_write_offset);
	} else if (read_buffer_offset > firmware_write_offset) {
		/* Firmware has written data further down the buffer and then
		 * wrapped around.
		 * First read the remaining data at the bottom of the buffer,
		 * all the way to the end.
		 */
		num_bytes_read = simple_read_from_buffer(
			buf_user, count, &read_buffer_offset,
			buffer->entries, buffer_entries_size_bytes);

		/* Then, if the user buffer has any space left, continue
		 * reading data from the top of the buffer.
		 */
		if (num_bytes_read > 0 && num_bytes_read < count) {
			read_buffer_offset = 0;
			num_bytes_read += simple_read_from_buffer(
				buf_user + num_bytes_read,
				count - num_bytes_read, &read_buffer_offset,
				buffer->entries, firmware_write_offset);
		}
	} else {
		/* No more data available (or the firmware has written so much
		 * that it has wrapped around to exactly where it was)
		 */
		num_bytes_read = 0;
	}

	ret = num_bytes_read;

	/* Update user's file offset */
	if (num_bytes_read > 0)
		*position += num_bytes_read;

cleanup:
	mutex_unlock(&core->mutex);

	return ret;
}

#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)

/**
 * debug_monitor_channel_read - Called when a userspace process reads the
 *			        debug_monitor_channel debugfs entry,
 *                              to read data from the firmware debug monitor
 *
 *
 * @file:		File handle.
 * @buf_user:		User space buffer.
 * @count:		Size of user space buffer.
 * @position:		Current file position (not used).
 *
 * Return: Number of bytes read, else error code.
 */
static ssize_t debug_monitor_channel_read(struct file *file,
					  char __user *buf_user,
					  size_t count,
					  loff_t *position)
{
	struct ethosn_core *core = file->f_inode->i_private;
	ssize_t num_bytes_read;
	struct ethosn_queue *queue;
	ssize_t ret;

	/* Report error if the response queue has not been set up */
	if (IS_ERR_OR_NULL(core->debug_monitor_channel_response))
		return -EINVAL;

	queue = core->debug_monitor_channel_response->cpu_addr;

	/* Block, waiting for at least one byte to be available */
	while (1) {
		/* Sync the queue header so that we can read an up-to-date write
		 * pointer from the firmware
		 */
		ethosn_dma_sync_for_cpu(core->main_allocator,
					core->debug_monitor_channel_response);
		if (ethosn_queue_get_size(queue) > 0)
			/* Data available */
			break;

		/* Support non-blocking read, if enabled */
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Allow Ctrl-C (or similar) to break out */
		if (signal_pending(current) != 0)
			return -EINTR;
	}

	/* Copy data to userspace.This is a simple / slow implementation that
	 * avoids having to create a kernel space buffer
	 */
	num_bytes_read = 0;
	while (num_bytes_read < count) {
		uint8_t data;

		ret = ethosn_queue_read(queue, &data, 1);
		if (!ret)
			break; /* No more bytes available. */

		ret = put_user(data, buf_user);
		if (ret)
			return ret;

		++num_bytes_read;
		++buf_user;
	}

	ret = num_bytes_read;

	/* Sync the queue header so that the firmware can read our up-to-date
	 * read pointer
	 */
	ethosn_dma_sync_for_device(core->main_allocator,
				   core->debug_monitor_channel_response);

	return ret;
}

/**
 * debug_monitor_channel_write - Called when a userspace process writes the
 *			         debug_monitor_channel debugfs entry,
 *                               to write data to the firmware debug monitor
 *
 *
 * @file:		File handle.
 * @buf_user:		User space buffer.
 * @count:		Size of user space buffer.
 * @position:		Current file position (not used).
 *
 * Return: Number of bytes read, else error code.
 */
static ssize_t debug_monitor_channel_write(struct file *file,
					   const __user char *buf_user,
					   size_t count,
					   loff_t *position)
{
	struct ethosn_core *core = file->f_inode->i_private;
	ssize_t num_bytes_written;
	struct ethosn_queue *queue;
	bool ret;
	uint32_t write_pending;

	/* Report error if the response queue has not been set up */
	if (IS_ERR_OR_NULL(core->debug_monitor_channel_request))
		return -EINVAL;

	queue = core->debug_monitor_channel_request->cpu_addr;

	/* Sync the queue header so that we can read an up-to-date read pointer
	 * from the firmware
	 */
	ethosn_dma_sync_for_cpu(core->main_allocator,
				core->debug_monitor_channel_request);

	/* Copy data from userspace. This is a simple/slow implementation that
	 * avoids having to create a kernel space buffer
	 */
	num_bytes_written = 0;
	while (num_bytes_written < count) {
		uint8_t data;
		const uint8_t *buffers[1] = { &data };
		const uint32_t sizes[1] = { 1 };

		ret = get_user(data, buf_user);
		if (ret)
			return ret;

		ret = ethosn_queue_write(queue, buffers, sizes, 1,
					 &write_pending);
		if (!ret)
			break; /* No space left in queue. */

		++num_bytes_written;
		++buf_user;

		/* Sync the queue data so that the firmware can read our new
		 * data
		 */
		ethosn_dma_sync_for_device(core->main_allocator,
					   core->debug_monitor_channel_request);

		/*
		 * Update the write pointer after the data has been written and
		 * is visible to the firmware.
		 */
		queue->write = write_pending;

		/* Sync the new write pointer to make sure it's visible to the
		 * firmware.
		 */
		ethosn_dma_sync_for_device(core->main_allocator,
					   core->debug_monitor_channel_request);
	}

	return num_bytes_written;
}

/**
 * debug_monitor_channel_poll - Called when a userspace process polls the
 *			        debug_monitor_channel debugfs entry,
 *                              to check if data can be read/written to it
 */
static __poll_t debug_monitor_channel_poll(struct file *file,
					   struct poll_table_struct *wait)
{
	struct ethosn_core *core = file->f_inode->i_private;
	struct ethosn_queue *queue;
	__poll_t ret = 0;

	/* Can't be read/written if the response queue has not been set up */
	if (IS_ERR_OR_NULL(core->debug_monitor_channel_response))
		return 0;

	queue = core->debug_monitor_channel_response->cpu_addr;

	/* Register our wait queue and start a timer which will signal the wait
	 * queue after a short delay.
	 * We don't have a better way of knowing when data arrives (e.g. an
	 * interrupt like
	 * we do for the mailbox.
	 */
	poll_wait(file, &core->debug_monitor_channel_read_poll_wqh, wait);
	mod_timer(&core->debug_monitor_channel_timer,
		  jiffies + msecs_to_jiffies(10));

	/* Check if data is available for reading.
	 * First sync the queue header so that we can read an up-to-date write
	 * pointer from the firmware
	 */
	ethosn_dma_sync_for_cpu(core->main_allocator,
				core->debug_monitor_channel_response);
	if (ethosn_queue_get_size(queue) > 0)
		ret = (EPOLLIN | EPOLLRDNORM); /* Data is available*/

	/* Unconditionally declare that data can be written. This might not be
	 * accurate if the firmware has got behind on reading data and the
	 * request queue is full, but we don't implement this check.
	 */
	ret |= (EPOLLOUT | EPOLLWRNORM);

	return ret;
}

#endif

static void dfs_deinit(struct ethosn_core *core)
{
	debugfs_remove_recursive(core->debug_dir);
	core->debug_dir = NULL;
}

static void dfs_init(struct ethosn_core *core)
{
	static const struct debugfs_reg32 regs[] = {
#ifndef ETHOSN_TZMP1
		REGSET32(SYSCTLR0),
		REGSET32(SYSCTLR1),
#endif
		REGSET32(PWRCTLR),
		REGSET32(CLRIRQ_EXT),
		REGSET32(SETIRQ_INT),
		REGSET32(IRQ_STATUS),
		REGSET32(GP0),
		REGSET32(GP1),
		REGSET32(GP2),
		REGSET32(GP3),
		REGSET32(GP4),
		REGSET32(GP5),
		REGSET32(GP6),
		REGSET32(GP7),
		REGSET32(STREAM0_ADDRESS_EXTEND),
		REGSET32(NPU_ID),
		REGSET32(UNIT_COUNT),
		REGSET32(MCE_FEATURES),
		REGSET32(DFC_FEATURES),
		REGSET32(PLE_FEATURES),
		REGSET32(WD_FEATURES),
		REGSET32(ECOID)
	};
	static const struct file_operations mailbox_fops = {
		.owner = THIS_MODULE,
		.read  = &mailbox_fops_read
	};
	static const struct file_operations firmware_log_fops = {
		.owner  = THIS_MODULE,
		.llseek = noop_llseek,
		.read   = &firmware_log_fops_read,
		.poll   = &firmware_log_fops_poll,
	};
	static const struct file_operations firmware_profiling_fops = {
		.owner = THIS_MODULE,
		.read  = &firmware_profiling_read
	};

#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)
	static const struct file_operations debug_monitor_channel_fops = {
		.owner = THIS_MODULE,
		.read  = &debug_monitor_channel_read,
		.write = &debug_monitor_channel_write,
		.poll  = &debug_monitor_channel_poll,
	};
#endif
	char name[16];

	/* Create debugfs directory */
	snprintf(name, sizeof(name), "core%u", core->core_id);
	core->debug_dir = debugfs_create_dir(name, core->parent->debug_dir);
	if (IS_ERR_OR_NULL(core->debug_dir))
		return;

	/* Register map */
	core->debug_regset.regs = regs;
	core->debug_regset.nregs = ARRAY_SIZE(regs);
	core->debug_regset.base = core->top_regs;
	debugfs_create_regset32("registers", 0400, core->debug_dir,
				&core->debug_regset);

	/* Mailbox */
	debugfs_create_file("mailbox", 0400, core->debug_dir, core,
			    &mailbox_fops);

	/* Firmware log */
	debugfs_create_file("firmware_log", 0400, core->debug_dir, core,
			    &firmware_log_fops);

	/* Expose the firmware's profiling stream to user-space as a file. */
	debugfs_create_file("firmware_profiling", 0400, core->debug_dir,
			    core,
			    &firmware_profiling_fops);
	debugfs_create_u64("wall_clock_time_at_firmware_zero", 0400,
			   core->debug_dir,
			   &core->profiling.wall_clock_time_at_firmware_zero);

#if defined(ETHOSN_KERNEL_MODULE_DEBUG_MONITOR)

	/* Expose the debug monitor channel to user-space as a file, for GDB to
	 * connect to
	 */
	debugfs_create_file("debug_monitor_channel", 0440,
			    core->debug_dir, core, &debug_monitor_channel_fops);
#endif
}

/****************************************************************************
 * Device setup
 ****************************************************************************/

int ethosn_device_init(struct ethosn_core *core)
{
	int ret;

	/* Round up queue size to next power of 2 */
	core->queue_size = roundup_pow_of_two(ethosn_queue_size);

	/* Initialize debugfs */
	dfs_init(core);

#ifdef ETHOSN_TZMP1

	/*
	 * Firmware has already been loaded into the protected memory by TF-A
	 * during boot so it only needs to be mapped
	 */
	ret = protected_firmware_map(core);
	if (ret) {
		dev_err(core->dev,
			"%s: Failed to map protected firmware %d\n",
			__func__, ret);

		return ret;
	}

#else

	ret = firmware_load(core);
	if (ret) {
		dev_err(core->dev,
			"%s: firmware_load failed with %i\n",
			__func__, ret);

		return ret;
	}

#endif

	/* Allocate the mailbox structure */
	ret = mailbox_alloc(core);
	if (ret)
		goto remove_debugfs;

	/* Allocate the firmware log */
	ret = firmware_log_alloc(core);
	if (ret)
		goto remove_debugfs;

	/* Allocate the debug_monitor_channel structure */
	ret = debug_monitor_channel_alloc(core);
	if (ret)
		goto remove_debugfs;

	/* For multi-npu, we test only the first NPU */
	if (ethosn_global_core_for_testing == NULL)
		ethosn_global_core_for_testing = core;

	/* Completed the device initialization */
	atomic_set(&core->init_done, 1);

	return 0;

remove_debugfs:
	dfs_deinit(core);

	return ret;
}

void ethosn_device_deinit(struct ethosn_core *core)
{
	int ret;

	/* Verify that the core is initialized */
	if (atomic_read(&core->init_done) == 0)
		return;

	ret = mutex_lock_interruptible(&core->mutex);
	if (ret)
		return;

	/* Started the device de-initialization */
	atomic_set(&core->init_done, 0);

	ethosn_global_core_for_testing = NULL;

	ethosn_reset(core, false, core->set_alloc_id);
	ethosn_firmware_deinit(core);
	ethosn_mailbox_free(core);
	ethosn_firmware_log_free(core);
	ethosn_debug_monitor_channel_free(core);
	dfs_deinit(core);
	mutex_unlock(&core->mutex);
	if (core->fw_and_hw_caps.data) {
		devm_kfree(core->parent->dev, core->fw_and_hw_caps.data);
		core->fw_and_hw_caps.data = NULL;
	}

	if (!IS_ERR_OR_NULL(core->profiling.firmware_buffer))
		ethosn_dma_unmap_and_release(
			core->main_allocator,
			&core->profiling.firmware_buffer);

	if (!IS_ERR_OR_NULL(core->profiling.firmware_buffer_pending))
		ethosn_dma_unmap_and_release(
			core->main_allocator,
			&core->profiling.firmware_buffer_pending);
}

static void ethosn_release_reserved_mem(void *const dev)
{
	of_reserved_mem_device_release((struct device *)dev);
}

int ethosn_init_reserved_mem(struct device *const dev)
{
	int ret;

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, ethosn_release_reserved_mem,
					dev);
}

bool ethosn_profiling_enabled(void)
{
	return profiling_enabled;
}

bool ethosn_mailbox_empty(struct ethosn_queue *queue)
{
	return (queue->read == queue->write);
}

int ethosn_clock_frequency(void)
{
	return clock_frequency;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_clock_frequency);

bool ethosn_stashing_enabled(void)
{
	return stashing_enabled;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_stashing_enabled);

struct ethosn_core *ethosn_get_global_core_for_testing(void)
{
	return ethosn_global_core_for_testing;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_get_global_core_for_testing);
