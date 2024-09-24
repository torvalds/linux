/*
 *
 * (C) COPYRIGHT 2018-2023 Arm Limited.
 * (C) COPYRIGHT 2023 Axis Communications AB.
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

#include "scylla_addr_fields_public.h"
#include "scylla_regs_public.h"
#include "ethosn_buffer.h"
#include "ethosn_device.h"
#include "ethosn_firmware.h"
#include "ethosn_proc_mem_allocator.h"
#include "ethosn_network.h"
#include "ethosn_core.h"
#include "ethosn_main_allocator.h"
#include "ethosn_asset_allocator.h"
#include "ethosn_sub_allocator.h"
#include "ethosn_smc.h"
#include "uapi/ethosn.h"

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/iommu.h>
#include <linux/pm_runtime.h>
#include <linux/wait.h>

#define ETHOSN_DRIVER_NAME    "ethosn"
#define ETHOSN_STR(s) #s
#define ETHOSN_DRIVER_VERSION_STR(major, minor, patch) \
	ETHOSN_STR(major) "." ETHOSN_STR(minor) "." ETHOSN_STR(patch)
#define ETHOSN_DRIVER_VERSION ETHOSN_DRIVER_VERSION_STR( \
		ETHOSN_KERNEL_MODULE_VERSION_MAJOR,	 \
		ETHOSN_KERNEL_MODULE_VERSION_MINOR,	 \
		ETHOSN_KERNEL_MODULE_VERSION_PATCH)

#define ETHOSN_MAX_DEVICES (1U << MINORBITS)

#define ETHOSN_SMMU_MAX_ADDR_BITS 49

#define TOP_REG_SIZE \
	(TOP_REG(REGPAGE_MASK, REGOFFSET_MASK) - TOP_REG(0, 0) + 1)

/* Timeout in us when pinging the Ethos-N and waiting for a pong. */
#define ETHOSN_PING_TIMEOUT_US (10 * 1000 * 1000)
#define ETHOSN_PING_WAIT_US 1000

#define ETHOSN_MAX_NUM_IRQS 3
#define ETHOSN_TZMP1_CORE_NUM_MAX 1U

static int ethosn_major;
static struct ethosn_device *ethosn_global_device_for_testing;
static DEFINE_IDA(ethosn_ida);

/* Ethos-N class infrastructure */
static struct class ethosn_class = {
	.name = ETHOSN_DRIVER_NAME,
};

static void __iomem *ethosn_map_iomem(const struct ethosn_core *const core,
				      const struct resource *const res,
				      const resource_size_t size)
{
	const resource_size_t rsize = resource_size(res);
	void __iomem *ptr;
	char *full_res_name;

	dev_dbg(core->dev,
		"Mapping resource. name=%s, start=%llx, size=%llu\n",
		res->name, res->start, size);

	/* Check resource size */
	if (rsize < size) {
		dev_err(core->dev,
			"'%s' resource not found or not big enough: %llu < %llu\n",
			res->name, rsize, size);

		return IOMEM_ERR_PTR(-EINVAL);
	}

	full_res_name = devm_kasprintf(core->parent->dev, GFP_KERNEL,
				       "%s : %s",
				       of_node_full_name(
					       core->parent->dev->of_node),
				       res->name);
	if (!full_res_name)
		return IOMEM_ERR_PTR(-ENOMEM);

	/* Reserve address space */
	if (!devm_request_mem_region(core->parent->dev, res->start, size,
				     full_res_name)) {
		dev_err(core->dev,
			"can't request region for resource %pR\n",
			res);

		return IOMEM_ERR_PTR(-EBUSY);
	}

	/* Map address space */
	ptr = devm_ioremap(core->parent->dev, res->start, size);
	if (IS_ERR(ptr))
		dev_err(core->dev,
			"failed to map '%s': start=%llu size=%llu\n",
			res->name, res->start, size);

	return ptr;
}

static const char *severity_to_kern_level(const struct device *dev,
					  uint32_t severity)
{
	switch (severity) {
	case ETHOSN_LOG_PANIC: {
		return KERN_CRIT;
	}
	case ETHOSN_LOG_ERROR: {
		return KERN_ERR;
	}
	case ETHOSN_LOG_WARNING: {
		return KERN_WARNING;
	}
	case ETHOSN_LOG_INFO: {
		return KERN_INFO;
	}
	case ETHOSN_LOG_DEBUG:
	/* Fall-through */
	case ETHOSN_LOG_VERBOSE: {
		return KERN_DEBUG;
	}
	default: {
		dev_err(dev, "Unknown text message severity level: %u\n",
			severity);

		return KERN_ERR;
	}
	}
}

static const char *err_msg_status_to_str(uint32_t status)
{
	switch (status) {
	case ETHOSN_ERROR_STATUS_INVALID_STATE:

		return "Invalid state";
	case ETHOSN_ERROR_STATUS_INVALID_MESSAGE:

		return "Invalid message";
	case ETHOSN_ERROR_STATUS_FAILED:

		return "Request failed";
	default: {
		return "Unknown status";
	}
	}
}

static const char *err_msg_type_to_str(uint32_t message_type)
{
	switch (message_type) {
	case ETHOSN_MESSAGE_INFERENCE_REQUEST: {
		return "Inference request";
	}
	case ETHOSN_MESSAGE_CONFIGURE_PROFILING: {
		return "Configure profiling request";
	}
	case ETHOSN_MESSAGE_FW_HW_CAPS_REQUEST: {
		return "FW & HW capabilities request";
	}
	default: {
		return "Unknown request";
	}
	}
}

/**
 * rtrim() - Trim characters from end of string.
 *
 * Remove any character found in trim from the end of str.
 *
 * Return: Pointer to str.
 */
static char *rtrim(char *str,
		   const char *trim)
{
	char *end = str + strlen(str);

	/*
	 * Loop from end to begin of string. Break the loop if end char does not
	 * match any char in trim.
	 */
	while (end-- > str)
		if (!strchr(trim, *end))
			break;

	end[1] = '\0';

	return str;
}

/**
 * reset_profiling_counters() - Resets all profiling counters
 *
 */
static void reset_profiling_counters(struct ethosn_core *core)
{
	core->profiling.mailbox_messages_sent = 0;
	core->profiling.mailbox_messages_received = 0;
	core->profiling.rpm_suspend_count = 0;
	core->profiling.rpm_resume_count = 0;
	core->profiling.pm_suspend_count = 0;
	core->profiling.pm_resume_count = 0;
}

static void update_busy_core(struct ethosn_core *core)
{
	struct ethosn_device *ethosn = core->parent;

	uint32_t core_id = core->core_id;
	uint32_t core_mask = (1 << core_id);

	if ((ethosn->current_busy_cores & core_mask) == 0) {
		dev_err(core->dev,
			"Scheduler has scheduled an inference on the wrong core");
		ethosn->status_mask |= (1 << WRONG_CORE_SCHEDULE);
	} else {
		ethosn->current_busy_cores &= ~(1 << core->core_id);
	}

	/* If after clearing our core id, the current_busy_cores
	 * isn't zero, it means that another inference is executing
	 * concurrently.
	 */
	if (ethosn->current_busy_cores != 0) {
		dev_info(ethosn->dev, "Concurrent inferences detected");
		ethosn->status_mask |=
			(1 << CONCURRENT_INFERENCE_DETECTED);
	}
}

static void write_to_firmware_log(struct ethosn_core *core,
				  const char *text,
				  size_t len_exc_terminator)
{
	/* Plus one for the newline that we append */
	size_t num_bytes_required = len_exc_terminator + 1U;

	/* Copy to firmware log file. Overwrite old data if necessary */
	if (kfifo_avail(&core->firmware_log) < num_bytes_required) {
		/* Unfortunately there is no API to skip multiple bytes,
		 * so we do it ourselves
		 */
		size_t num_bytes_to_skip = num_bytes_required -
					   kfifo_avail(
			&core->firmware_log);
		core->firmware_log.kfifo.out += num_bytes_to_skip;
	}

	kfifo_in(&core->firmware_log, text, len_exc_terminator);
	kfifo_put(&core->firmware_log, '\n');

	wake_up_interruptible(&core->firmware_log_read_poll_wqh);
}

static int handle_message(struct ethosn_core *core)
{
	struct ethosn_message_header header;
	int ret;

	/* Read message from queue. Reserve one byte for end of string. */
	ret = ethosn_read_message(core, &header, core->mailbox_message,
				  core->queue_size - 1);
	if (ret <= 0)
		return ret;

	dev_dbg(core->dev, "Message. type=%u, length=%u\n",
		header.type, header.length);

	switch (header.type) {
	case ETHOSN_MESSAGE_FW_HW_CAPS_RESPONSE: {
		dev_dbg(core->dev, "<- FW & HW capabilities\n");

		/* Free previous memory (if any) */
		if (core->fw_and_hw_caps.data)
			devm_kfree(core->parent->dev,
				   core->fw_and_hw_caps.data);

		/* Allocate new memory */
		core->fw_and_hw_caps.data = devm_kzalloc(core->parent->dev,
							 header.length,
							 GFP_KERNEL);
		if (!core->fw_and_hw_caps.data)
			return -ENOMEM;

		/* Copy data returned from firmware into our storage, so that it
		 * can be passed to user-space when queried
		 */
		memcpy(core->fw_and_hw_caps.data, core->mailbox_message,
		       header.length);
		core->fw_and_hw_caps.size = header.length;
		break;
	}
	case ETHOSN_MESSAGE_INFERENCE_RESPONSE: {
		struct ethosn_message_inference_response *rsp =
			core->mailbox_message;
		struct ethosn_inference *inference = (void *)rsp->user_argument;
		int status;

		dev_dbg(core->dev,
			"<- Inference. user_arg=0x%llx, status=%u, cycle_count=%llu\n",
			rsp->user_argument, rsp->status, rsp->cycle_count);

		status = rsp->status == ETHOSN_INFERENCE_STATUS_OK ?
			 ETHOSN_INFERENCE_COMPLETED : ETHOSN_INFERENCE_ERROR;

		update_busy_core(core);
		ethosn_set_inference_done(core, inference, status,
					  rsp->cycle_count);
		break;
	}
	case ETHOSN_MESSAGE_PONG: {
		++core->num_pongs_received;
		dev_dbg(core->dev, "<- Pong\n");
		break;
	}
	case ETHOSN_MESSAGE_TEXT: {
		struct ethosn_message_text *text = core->mailbox_message;
		size_t msg_len_excluding_terminator = header.length - offsetof(
			struct ethosn_message_text, text);
		/* Null terminate str. One byte has been reserved for this. */
		text->text[msg_len_excluding_terminator] = '\0';

		/* Always write to the dedicated firmware log file. */
		write_to_firmware_log(core, text->text,
				      msg_len_excluding_terminator);

		/* Optionally print to kernel log. This is very slow when there
		 * are lots of log messages so it is optional.
		 * The log messages can be viewed through the
		 * above buffer using the debugfs entry instead in this case.
		 */
		if (firmware_log_to_kernel_log)
			dev_printk(severity_to_kern_level(core->dev,
							  text->severity),
				   core->dev, "<- Text. text=\"%s\"\n",
				   rtrim(text->text, "\n"));

		break;
	}
	case ETHOSN_MESSAGE_CONFIGURE_PROFILING_ACK: {
		dev_dbg(core->dev,
			"<- Configured profiling\n");
		ethosn_configure_firmware_profiling_ack(core);
		break;
	}
	case ETHOSN_MESSAGE_ERROR_RESPONSE: {
		struct ethosn_message_error_response *rsp =
			core->mailbox_message;

		dev_err(core->dev, "<- %s(%u) error. status=%s(%u)\n",
			err_msg_type_to_str(rsp->type), rsp->type,
			err_msg_status_to_str(rsp->status), rsp->status);

		return -EFAULT;
	}
	default: {
		dev_warn(core->dev,
			 "Unsupported message type. Type=%u, Length=%u, ret=%d.\n",
			 header.type, header.length, ret);
		break;
	}
	}

	return 1;
}

/* Prints the given value of the IRQ_STATUS register in a friendly format
 * to the given buffer.
 * Returns the buffer pointer provided (useful for calling
 * this in a printf format arg).
 */
static char *print_irq_status_reg(char *buffer,
				  size_t buffer_size,
				  struct dl1_irq_status_r status)
{
	char *buffer_start = buffer;
	char *buffer_end = buffer + buffer_size;

	buffer += snprintf(buffer, buffer_end - buffer, "0x%08x", status.word);
	if (status.word != 0) {
		buffer += snprintf(buffer, buffer_end - buffer, " (");
		if (status.bits.setirq_err)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " setirq_err");

		if (status.bits.setirq_dbg)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " setirq_dbg");

		if (status.bits.setirq_job)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " setirq_job");

		if (status.bits.tsu_dbg)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " tsu_dbg");

		if (status.bits.pmu_dbg)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " pmu_dbg");

		if (status.bits.tol_err)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " tol_err");

		if (status.bits.func_err)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " func_err");

		if (status.bits.rec_err)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " rec_err");

		if (status.bits.unrec_err)
			buffer += snprintf(buffer, buffer_end - buffer,
					   " unrec_err");

		snprintf(buffer, buffer_end - buffer, " )");
	}

	return buffer_start;
}

/* Prints the firmware dump information provided by the firmware in the
 * GP registers, if one is present.
 * Returns true if dump was present and has been printed, false if not.
 */
static bool print_firmware_dump_if_present(struct ethosn_core *core)
{
	int gp = 0;
	struct ethosn_firmware_dump dump = { 0 };

	/* Read the dump struct from the GP registers, by copying one
	 * word at a time.
	 */
	for (gp = 0; gp < 8; ++gp)
		*((uint32_t *)&dump + gp) =
			ethosn_read_top_reg(core, DL1_RP,
					    DL1_GP0 + gp * (DL1_GP1 - DL1_GP0));

	if (dump.magic != ETHOSN_FIRMWARE_DUMP_MAGIC)
		return false;

	dev_err(core->dev, "======== Firmware dump ========\n");
	dev_err(core->dev, "  pc  = 0x%08x\n", dump.pc);
	dev_err(core->dev, "  ISR = %d\n", dump.ISR);
	dev_err(core->dev, "  CFSR\n");
	dev_err(core->dev, "    MMFSR\n");
	dev_err(core->dev, "      MMARVALID   = %d\n",
		dump.CFSR_MMFSR_MMARVALID);
	dev_err(core->dev, "      MSTKERR     = %d\n", dump.CFSR_MMFSR_MSTKERR);
	dev_err(core->dev, "      MUNSTKERR   = %d\n",
		dump.CFSR_MMFSR_MUNSTKERR);
	dev_err(core->dev, "      DACCVIOL    = %d\n",
		dump.CFSR_MMFSR_DACCVIOL);
	dev_err(core->dev, "      IACCVIOL    = %d\n",
		dump.CFSR_MMFSR_IACCVIOL);
	dev_err(core->dev, "    BFSR\n");
	dev_err(core->dev, "      BFARVALID   = %d\n",
		dump.CFSR_BFSR_BFARVALID);
	dev_err(core->dev, "      STKERR      = %d\n", dump.CFSR_BFSR_STKERR);
	dev_err(core->dev, "      UNSTKERR    = %d\n", dump.CFSR_BFSR_UNSTKERR);
	dev_err(core->dev, "      IMPRECISERR = %d\n",
		dump.CFSR_BFSR_IMPRECISERR);
	dev_err(core->dev, "      PRECISERR   = %d\n",
		dump.CFSR_BFSR_PRECISERR);
	dev_err(core->dev, "      IBUSERR     = %d\n", dump.CFSR_BFSR_IBUSERR);
	dev_err(core->dev, "    UFSR\n");
	dev_err(core->dev, "      DIVBYZERO   = %d\n",
		dump.CFSR_UFSR_DIVBYZERO);
	dev_err(core->dev, "      UNALIGNED   = %d\n",
		dump.CFSR_UFSR_UNALIGNED);
	dev_err(core->dev, "      NOCP        = %d\n", dump.CFSR_UFSR_NOCP);
	dev_err(core->dev, "      INVPC       = %d\n", dump.CFSR_UFSR_INVPC);
	dev_err(core->dev, "      INVSTATE    = %d\n", dump.CFSR_UFSR_INVSTATE);
	dev_err(core->dev, "      UNDEFINSTR  = %d\n",
		dump.CFSR_UFSR_UNDEFINSTR);
	dev_err(core->dev, "  HFSR\n");
	dev_err(core->dev, "    FORCED        = %d\n", dump.HFSR_FORCED);
	dev_err(core->dev, "    VECTTBL       = %d\n", dump.HFSR_VECTTBL);
	dev_err(core->dev, "  MMFAR           = 0x%08x\n", dump.MMFAR);
	dev_err(core->dev, "  BFAR            = 0x%08x\n", dump.BFAR);
	dev_err(core->dev, "  TOP_ERR_CAUSE\n");
	dev_err(core->dev, "    ENGINE_RAM_CORRECTABLE_ERR     = %d\n",
		dump.TOP_ERR_CAUSE_ENGINE_RAM_CORRECTABLE_ERR);
	dev_err(core->dev, "    ENGINE_RAM_UNCORRECTABLE_ERR   = %d\n",
		dump.TOP_ERR_CAUSE_ENGINE_RAM_UNCORRECTABLE_ERR);
	dev_err(core->dev, "    TOP_TOLERABLE_RAM_ERR          = %d\n",
		dump.TOP_ERR_CAUSE_TOP_TOLERABLE_RAM_ERR);
	dev_err(core->dev, "    TOP_RECOVERABLE_RAM_ERR        = %d\n",
		dump.TOP_ERR_CAUSE_TOP_RECOVERABLE_RAM_ERR);
	dev_err(core->dev, "    MCU_LOCKUP_ERR                 = %d\n",
		dump.TOP_ERR_CAUSE_MCU_LOCKUP_ERR);
	dev_err(core->dev, "    MCU_INSTR_ERR                  = %d\n",
		dump.TOP_ERR_CAUSE_MCU_INSTR_ERR);
	dev_err(core->dev, "    MCU_DATA_READ_ERR              = %d\n",
		dump.TOP_ERR_CAUSE_MCU_DATA_READ_ERR);
	dev_err(core->dev, "    MCU_DATA_WRITE_ERR             = %d\n",
		dump.TOP_ERR_CAUSE_MCU_DATA_WRITE_ERR);
	dev_err(core->dev, "    DMA_READ_ERR                   = %d\n",
		dump.TOP_ERR_CAUSE_DMA_READ_ERR);
	dev_err(core->dev, "    DMA_WRITE_ERR                  = %d\n",
		dump.TOP_ERR_CAUSE_DMA_WRITE_ERR);
	dev_err(core->dev, "    STASH_TRANSLATION_ERR          = %d\n",
		dump.TOP_ERR_CAUSE_STASH_TRANSLATION_ERR);
	dev_err(core->dev, "    DMA_QUEUE_PROGRAMMING_ERR      = %d\n",
		dump.TOP_ERR_CAUSE_DMA_QUEUE_PROGRAMMING_ERR);
	dev_err(core->dev, "    PWRCTLR_ACTIVE_PROGRAMMING_ERR = %d\n",
		dump.TOP_ERR_CAUSE_PWRCTLR_ACTIVE_PROGRAMMING_ERR);
	dev_err(core->dev, "    STASH_TRANS_PROGRAMMING_ERR    = %d\n",
		dump.TOP_ERR_CAUSE_STASH_TRANS_PROGRAMMING_ERR);
	dev_err(core->dev, "    TSU_EVENT_OVERFLOW_ERR         = %d\n",
		dump.TOP_ERR_CAUSE_TSU_EVENT_OVERFLOW_ERR);
	dev_err(core->dev, "    STRIPE_PROGRAMMING_ERR         = %d\n",
		dump.TOP_ERR_CAUSE_STRIPE_PROGRAMMING_ERR);
	dev_err(core->dev, "    STRIPE_WRITE_WHILE_BUSY_ERR    = %d\n",
		dump.TOP_ERR_CAUSE_STRIPE_WRITE_WHILE_BUSY_ERR);
	dev_err(core->dev, "    BLOCK_PROGRAMMING_ERR          = %d\n",
		dump.TOP_ERR_CAUSE_BLOCK_PROGRAMMING_ERR);
	dev_err(core->dev, "    BLOCK_WRITE_WHILE_BUSY_ERR     = %d\n",
		dump.TOP_ERR_CAUSE_BLOCK_WRITE_WHILE_BUSY_ERR);
	dev_err(core->dev, "    SHADOW_ERR                     = %d\n",
		dump.TOP_ERR_CAUSE_SHADOW_ERR);
	dev_err(core->dev, "    ENGINE_FUNC_ERR                = %d\n",
		dump.TOP_ERR_CAUSE_ENGINE_FUNC_ERR);
	dev_err(core->dev, "  TOP_ERR_ADDRESS\n");
	dev_err(core->dev, "    ADDRESS                        = 0x%04x\n",
		dump.TOP_ERR_ADDRESS_ADDRESS);
	dev_err(core->dev, "    BANK                           = %d\n",
		dump.TOP_ERR_ADDRESS_BANK);
	dev_err(core->dev, "    NCU_MCU_ICACHE_TAG             = %d\n",
		dump.TOP_ERR_ADDRESS_NCU_MCU_ICACHE_TAG);
	dev_err(core->dev, "    NCU_MCU_ICACHE_DATA            = %d\n",
		dump.TOP_ERR_ADDRESS_NCU_MCU_ICACHE_DATA);
	dev_err(core->dev, "    NCU_MCU_DCACHE_TAG             = %d\n",
		dump.TOP_ERR_ADDRESS_NCU_MCU_DCACHE_TAG);
	dev_err(core->dev, "    NCU_MCU_DCACHE_DATA            = %d\n",
		dump.TOP_ERR_ADDRESS_NCU_MCU_DCACHE_DATA);
	dev_err(core->dev, "    DFC_ROB                        = %d\n",
		dump.TOP_ERR_ADDRESS_DFC_ROB);
	dev_err(core->dev, "    DFC_COMPRESSOR_SIM             = %d\n",
		dump.TOP_ERR_ADDRESS_DFC_COMPRESSOR_SIM);
	dev_err(core->dev, "    DFC_COMPRESSOR_REM             = %d\n",
		dump.TOP_ERR_ADDRESS_DFC_COMPRESSOR_REM);
	dev_err(core->dev, "    DFC_COMPRESSOR_UNARY           = %d\n",
		dump.TOP_ERR_ADDRESS_DFC_COMPRESSOR_UNARY);
	dev_err(core->dev, "    DFC_DECOMPRESSOR               = %d\n",
		dump.TOP_ERR_ADDRESS_DFC_DECOMPRESSOR);
	dev_err(core->dev, "    ERR_MULTI                      = %d\n",
		dump.TOP_ERR_ADDRESS_ERR_MULTI);
	dev_err(core->dev, "    ERR_UNCORRECTED                = %d\n",
		dump.TOP_ERR_ADDRESS_ERR_UNCORRECTED);
	if (dump.cesWithError == 0) {
		dev_err(core->dev, "  cesWithError = <none>\n");
	} else {
		dev_err(core->dev,
			"  cesWithError                                =%s%s%s%s%s%s%s%s\n",
			(dump.cesWithError & (1 << 0)) ? " 0" : "",
			(dump.cesWithError & (1 << 1)) ? " 1" : "",
			(dump.cesWithError & (1 << 2)) ? " 2" : "",
			(dump.cesWithError & (1 << 3)) ? " 3" : "",
			(dump.cesWithError & (1 << 4)) ? " 4" : "",
			(dump.cesWithError & (1 << 5)) ? " 5" : "",
			(dump.cesWithError & (1 << 6)) ? " 6" : "",
			(dump.cesWithError & (1 << 7)) ? " 7" : "");
		dev_err(core->dev, "  CE_ERR_CAUSE\n");
		dev_err(core->dev,
			"    CE_ERR_CAUSE_ENGINE_RAM_CORRECTABLE_ERR   = %d\n",
			dump.CE_ERR_CAUSE_ENGINE_RAM_CORRECTABLE_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_ENGINE_RAM_UNCORRECTABLE_ERR = %d\n",
			dump.CE_ERR_CAUSE_ENGINE_RAM_UNCORRECTABLE_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_MCU_LOCKUP_ERR               = %d\n",
			dump.CE_ERR_CAUSE_MCU_LOCKUP_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_MCU_INSTR_ERR                = %d\n",
			dump.CE_ERR_CAUSE_MCU_INSTR_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_MCU_DATA_READ_ERR            = %d\n",
			dump.CE_ERR_CAUSE_MCU_DATA_READ_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_MCU_DATA_WRITE_ERR           = %d\n",
			dump.CE_ERR_CAUSE_MCU_DATA_WRITE_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_UDMA_LOAD_ERR                = %d\n",
			dump.CE_ERR_CAUSE_UDMA_LOAD_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_UDMA_STORE_ERR               = %d\n",
			dump.CE_ERR_CAUSE_UDMA_STORE_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_MCU_ILLEGAL_COPROC_ERR       = %d\n",
			dump.CE_ERR_CAUSE_MCU_ILLEGAL_COPROC_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_UDMA_COLLISION_ERR           = %d\n",
			dump.CE_ERR_CAUSE_UDMA_COLLISION_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_RF_RD_COLLISION_ERR          = %d\n",
			dump.CE_ERR_CAUSE_RF_RD_COLLISION_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_RF_WR_COLLISION_ERR          = %d\n",
			dump.CE_ERR_CAUSE_RF_WR_COLLISION_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_VE_DIV_0_ERR                 = %d\n",
			dump.CE_ERR_CAUSE_VE_DIV_0_ERR);
		dev_err(core->dev,
			"    CE_ERR_CAUSE_PLE_LANE_ERR                 = %d\n",
			dump.CE_ERR_CAUSE_PLE_LANE_ERR);
		dev_err(core->dev, "  CE_ERR_ADDRESS\n");
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_ADDRESS                    = 0x%04x\n",
			dump.CE_ERR_ADDRESS_ADDRESS);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_BANK                       = %d\n",
			dump.CE_ERR_ADDRESS_BANK);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_DFC_EMC0                   = %d\n",
			dump.CE_ERR_ADDRESS_DFC_EMC0);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_DFC_EMC1                   = %d\n",
			dump.CE_ERR_ADDRESS_DFC_EMC1);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_DFC_EMC2                   = %d\n",
			dump.CE_ERR_ADDRESS_DFC_EMC2);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_DFC_EMC3                   = %d\n",
			dump.CE_ERR_ADDRESS_DFC_EMC3);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_MCE_OFM0                   = %d\n",
			dump.CE_ERR_ADDRESS_MCE_OFM0);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_MCE_OFM1                   = %d\n",
			dump.CE_ERR_ADDRESS_MCE_OFM1);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_MCE_OFM2                   = %d\n",
			dump.CE_ERR_ADDRESS_MCE_OFM2);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_MCE_OFM3                   = %d\n",
			dump.CE_ERR_ADDRESS_MCE_OFM3);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_PLE_INPUT0                 = %d\n",
			dump.CE_ERR_ADDRESS_PLE_INPUT0);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_PLE_INPUT1                 = %d\n",
			dump.CE_ERR_ADDRESS_PLE_INPUT1);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_PLE_INPUT2                 = %d\n",
			dump.CE_ERR_ADDRESS_PLE_INPUT2);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_PLE_INPUT3                 = %d\n",
			dump.CE_ERR_ADDRESS_PLE_INPUT3);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_PLE_OUTPUT                 = %d\n",
			dump.CE_ERR_ADDRESS_PLE_OUTPUT);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_PLE_MCU                    = %d\n",
			dump.CE_ERR_ADDRESS_PLE_MCU);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_ERR_MULTI                  = %d\n",
			dump.CE_ERR_ADDRESS_ERR_MULTI);
		dev_err(core->dev,
			"    CE_ERR_ADDRESS_ERR_UNCORRECTED            = %d\n",
			dump.CE_ERR_ADDRESS_ERR_UNCORRECTED);
	}

	dev_err(core->dev, "======== End of firmware dump ========\n");

	return true;
}

/**
 * ethosn_irq_bottom() - IRQ bottom handler
 * @work:	Work structure part of Ethos-N core
 *
 * Execute bottom half of interrupt in process context.
 */
static void ethosn_irq_bottom(struct work_struct *work)
{
	struct ethosn_core *core = container_of(work, struct ethosn_core,
						irq_work);
	struct dl1_irq_status_r status;
	bool error_status;
	int ret;
	size_t num_messages_handled = 0;
	char buffer[256] = { 0 };

	ret = mutex_lock_interruptible(&core->mutex);
	if (ret)
		return;

	if (atomic_read(&core->init_done) == 0)
		goto end;

	dev_dbg(core->dev, "Irq bottom, handling messages");

	/* Handle mailbox messages. Note we do this before checking for errors
	 * so that we get as much debugging
	 * information from the firmware as possible before resetting it.
	 */
	do {
		ret = handle_message(core);

		/* Limit the number of message we handle in one call to this
		 * function, otherwise
		 * it can lock-up the driver for too long.
		 */
		++num_messages_handled;
		if (num_messages_handled > 10) {
			/* Re-queue this handler so that we can process the
			 * remaining messages later
			 */
			queue_work(core->irq_wq, &core->irq_work);
			goto end;
		}
	} while (ret > 0);

	/* Read and clear the IRQ status bits. */
	status.word = atomic_xchg(&core->irq_status, 0);

	dev_dbg(core->dev,
		"Irq bottom, IRQ_STATUS = %s\n",
		print_irq_status_reg(buffer, sizeof(buffer), status));

	error_status = status.bits.setirq_err || status.bits.tol_err ||
		       status.bits.func_err || status.bits.rec_err ||
		       status.bits.unrec_err;

	/* Mailbox or IRQ error reported. Reset firmware. */
	if (ret < 0 || error_status) {
		/* Failure may happen before the firmware is deemed running. */
		if (error_status) {
			uint32_t sysctlr0;

			dev_err(core->dev,
				"Error reported by IRQ_STATUS: %s. Will reset core after dump.\n",
				print_irq_status_reg(buffer, sizeof(buffer),
						     status));

			/* DL1_SYSCTLR0 might contain useful
			 * information (e.g.lockup)
			 */
			sysctlr0 = ethosn_read_top_reg(core, DL1_RP,
						       DL1_SYSCTLR0);
			dev_err(core->dev, "SYSCTLR0=0x%x.\n", sysctlr0);

			/* Print the firmware dump struct, if one was provided
			 * by the firmware,
			 * otherwise fall back to simple GP dump which might
			 * provide some insight.
			 */
			if (!print_firmware_dump_if_present(core)) {
				dev_err(core->dev,
					"Firmware dump not present, dumping raw GPs:\n");
				ethosn_dump_gps(core);
			}
		} else {
			dev_err(core->dev,
				"Reset core due to mailbox error. status=%d\n",
				ret);
		}

		if (core->firmware_running) {
			(void)ethosn_reset_and_start_ethosn(core,
							    core->set_alloc_id);
			if (core->current_inference)
				ethosn_set_inference_done(core,
							  core->current_inference,
							  ETHOSN_INFERENCE_ERROR,
							  0);
		}
	}

end:

	mutex_unlock(&core->mutex);
}

/**
 * ethosn_irq_top() - IRQ top handler
 * @irq:	IRQ number.
 * @dev:	User argument, Ethos-N core.
 *
 * Handle IRQ in interrupt context. Clear the interrupt and trigger bottom
 * half to handle the rest of the interrupt.
 */
static irqreturn_t ethosn_irq_top(const int irq,
				  void *dev)
{
	struct ethosn_core *const core = dev;
	struct dl1_irq_status_r status;
	struct dl1_clrirq_ext_r clear = { .word = 0 };

	if (atomic_read(&core->is_configured) == 0)
		return IRQ_NONE;

	status.word = ethosn_read_top_reg(core, DL1_RP, DL1_IRQ_STATUS);

	/* Save the IRQ status for the bottom half (ethosn_irq_bottom). */
	atomic_or(status.word, &core->irq_status);

	/* This was not meant for us */
	if (status.word == 0)
		return IRQ_NONE;

	/* Clear the interrupts now that we have recorded them.
	 * This is very important for level-sensitive interrupts, as this
	 * handler will
	 * keep firing (and hang the kernel) until they are cleared.
	 * There is a fixed mapping between the IRQ_STATUS bits and which
	 * interrupt needs clearing, but to keep it simple we just clear them
	 * all.
	 */
	clear.bits.err = 1;
	clear.bits.debug = 1;
	clear.bits.job = 1;

	ethosn_write_top_reg(core, DL1_RP, DL1_CLRIRQ_EXT,
			     clear.word);

	/* Defer to work queue. */
	queue_work(core->irq_wq, &core->irq_work);

	return IRQ_HANDLED;
}

/**
 * ethosn_init_interrupt() - Register IRQ handlers
 * @core: Ethos-N core
 * @irq_numbers: List of IRQ numbers that we should listen to.
 * @irq_flags: List of the IRQF_TRIGGER_ flags to use for each corresponding irq
 *             number in the irq_numbers array.
 * @num_irqs: Number of valid entries in the irq_numbers and irq_flags arrays.
 *
 * Return:
 * * 0 - Success
 * * Negative error code
 */
static int ethosn_init_interrupt(struct ethosn_core *const core,
				 const int irq_numbers[ETHOSN_MAX_NUM_IRQS],
				 const unsigned long
				 irq_flags[ETHOSN_MAX_NUM_IRQS],
				 unsigned int num_irqs)
{
	int ret;
	int irq_idx;

	/* Create a work queue to handle the IRQs that come in. We do only a
	 * minimal amount of work in the IRQ handler itself ("ethosn_irq_top")
	 * and
	 * defer the rest of the work to this work queue ("ethosn_irq_bottom").
	 * Note we must do this before registering any handlers, as the handler
	 * callback uses this work queue.
	 */
	core->irq_wq = create_singlethread_workqueue("ethosn_workqueue");
	if (!core->irq_wq) {
		dev_err(core->dev, "Failed to create work queue\n");

		return -EINVAL;
	}

	INIT_WORK(&core->irq_work, ethosn_irq_bottom);

	/* Register an IRQ handler for each number requested.
	 * We use the same handler for each of these as we check the type of
	 * interrupt using the Ethos-N's IRQ status register, and so don't need
	 * to
	 * differentiate based on the IRQ number.
	 */
	for (irq_idx = 0; irq_idx < num_irqs; ++irq_idx) {
		const int irq_num = irq_numbers[irq_idx];
		const unsigned long this_irq_flags = irq_flags[irq_idx];

		dev_dbg(core->dev, "Requesting IRQ %d with flags 0x%lx\n",
			irq_num, this_irq_flags);

		ret = devm_request_irq(core->dev, irq_num,
				       &ethosn_irq_top,
				       this_irq_flags, ETHOSN_DRIVER_NAME,
				       core);
		if (ret) {
			dev_err(core->dev, "Failed to request IRQ %d\n",
				irq_num);

			return ret;
		}
	}

	return 0;
}

static ssize_t num_cores_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct ethosn_device *ethosn = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", ethosn->num_cores);
}

static ssize_t status_mask_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct ethosn_device *ethosn = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%#x\n",
			 ethosn->status_mask);
}

static const DEVICE_ATTR_RO(num_cores);
static const DEVICE_ATTR_RO(status_mask);

static const struct attribute *attrs[] = {
	&dev_attr_num_cores.attr,
	&dev_attr_status_mask.attr,
	NULL
};

/**
 * ethosn_open() - Open the Ethos-N core node.
 *
 * Open the device node and stored the Ethos-N core handle in the file private
 * data.
 */
static int ethosn_open(struct inode *inode,
		       struct file *file)
{
	file->private_data =
		container_of(inode->i_cdev, struct ethosn_device, cdev);

	return nonseekable_open(inode, file);
}

/**
 * ethosn_ioctl() - Take commands from user space
 * @filep:	File struct.
 * @cmd:	See IOCTL in ethosn.h.
 * @arg:	Command argument.
 *
 * Return: 0 on success, else error code.
 */
static long ethosn_ioctl(struct file *const filep,
			 unsigned int cmd,
			 unsigned long arg)
{
	struct ethosn_device *ethosn = filep->private_data;
	void __user *const udata = (void __user *)arg;
	int ret = 0;

	switch (cmd) {
	case ETHOSN_IOCTL_GET_VERSION: {
		struct ethosn_kernel_module_version act_version = {
			.major = ETHOSN_KERNEL_MODULE_VERSION_MAJOR,
			.minor = ETHOSN_KERNEL_MODULE_VERSION_MINOR,
			.patch = ETHOSN_KERNEL_MODULE_VERSION_PATCH,
		};

		if (copy_to_user(udata, &act_version, sizeof(act_version))) {
			ret = -EFAULT;
			break;
		}

		break;
	}
	case ETHOSN_IOCTL_CREATE_PROC_MEM_ALLOCATOR: {
		struct ethosn_proc_mem_allocator_req alloc_req;
		pid_t pid = task_pid_vnr(current);

		if (copy_from_user(&alloc_req, udata, sizeof(alloc_req))) {
			ret = -EFAULT;
			break;
		}

		dev_dbg(ethosn->dev,
			"IOCTL: Create process memory allocator\n");

		ret = mutex_lock_interruptible(&ethosn->mutex);
		if (ret)
			break;

		ret = ethosn_process_mem_allocator_create(ethosn, pid,
							  alloc_req.is_protected);

		mutex_unlock(&ethosn->mutex);

		dev_dbg(ethosn->dev,
			"IOCTL: Created process mem allocator. fd=%d for pid = %d\n",
			ret, pid);

		break;
	}
	case ETHOSN_IOCTL_FW_HW_CAPABILITIES: {
		/* In the case of multicore, we get the hardware capabilities
		 * for core[0]. As both the cores are of the same variant,
		 * so it should just be fine.
		 */
		struct ethosn_core *core = ethosn->core[0];

		ret = mutex_lock_interruptible(&core->mutex);
		if (ret)
			break;

		/* If the user provided a NULL pointer then simply return the
		 * size of the data.
		 * If they provided a valid pointer then copy the data to them
		 */
		if (!udata) {
			ret = core->fw_and_hw_caps.size;
		} else {
			if (copy_to_user(udata, core->fw_and_hw_caps.data,
					 core->fw_and_hw_caps.size)) {
				dev_warn(core->dev,
					 "Failed to copy firmware and hardware capabilities to user.\n");
				ret = -EFAULT;
			} else {
				ret = 0;
			}
		}

		/* It may happen that users ask for capabilities before the
		 * firmware has responded, in that case a fault is reported
		 */
		if (core->fw_and_hw_caps.size == 0)
			ret = -EAGAIN;

		mutex_unlock(&core->mutex);

		break;
	}
	case ETHOSN_IOCTL_CONFIGURE_PROFILING: {
		struct ethosn_core *core = ethosn->core[0];
		struct ethosn_profiling_config new_config;

		if (!ethosn_profiling_enabled()) {
			ret = -EACCES;
			dev_err(core->dev, "Profiling: access denied\n");
			break;
		}

		pm_runtime_get_sync(core->dev);

		ret = mutex_lock_interruptible(&core->mutex);
		if (ret)
			goto configure_profiling_put;

		if (copy_from_user(&new_config, udata, sizeof(new_config))) {
			ret = -EFAULT;
			goto configure_profiling_mutex;
		}

		dev_dbg(core->dev,
			"IOCTL: Configure profiling. enable_profiling=%u, firmware_buffer_size=%u num_hw_counters=%d\n",
			new_config.enable_profiling,
			new_config.firmware_buffer_size,
			new_config.num_hw_counters);

		/* Forward new state to the firmware */
		ret = ethosn_configure_firmware_profiling(core, &new_config);

		if (ret != 0)
			goto configure_profiling_mutex;

		if (core->profiling.config.enable_profiling &&
		    !new_config.enable_profiling)
			reset_profiling_counters(core);

		core->profiling.config = new_config;

configure_profiling_mutex:
		mutex_unlock(&core->mutex);
configure_profiling_put:
		pm_runtime_mark_last_busy(core->dev);
		pm_runtime_put(core->dev);

		break;
	}
	case ETHOSN_IOCTL_GET_COUNTER_VALUE: {
		struct ethosn_core *core = ethosn->core[0];
		enum ethosn_poll_counter_name counter_name;

		ret = mutex_lock_interruptible(&core->mutex);
		if (ret)
			break;

		if (!core->profiling.config.enable_profiling) {
			ret = -ENODATA;
			dev_err(core->dev, "Profiling counter: no data\n");
			goto get_counter_value_end;
		}

		if (copy_from_user(&counter_name, udata,
				   sizeof(counter_name))) {
			ret = -EFAULT;
			dev_err(core->dev,
				"Profiling counter: error in copy_from_user\n");
			goto get_counter_value_end;
		}

		switch (counter_name) {
		case ETHOSN_POLL_COUNTER_NAME_MAILBOX_MESSAGES_SENT:
			ret = core->profiling.mailbox_messages_sent;
			break;
		case ETHOSN_POLL_COUNTER_NAME_MAILBOX_MESSAGES_RECEIVED:
			ret = core->profiling.mailbox_messages_received;
			break;
		case ETHOSN_POLL_COUNTER_NAME_RPM_SUSPEND:
			ret = core->profiling.rpm_suspend_count;
			break;
		case ETHOSN_POLL_COUNTER_NAME_RPM_RESUME:
			ret = core->profiling.rpm_resume_count;
			break;
		case ETHOSN_POLL_COUNTER_NAME_PM_SUSPEND:
			ret = core->profiling.pm_suspend_count;
			break;
		case ETHOSN_POLL_COUNTER_NAME_PM_RESUME:
			ret = core->profiling.pm_resume_count;
			break;
		default:
			ret = -EINVAL;
			dev_err(core->dev,
				"Profiling counter: invalid counter_name\n");
			break;
		}

get_counter_value_end:
		mutex_unlock(&core->mutex);

		break;
	}
	case ETHOSN_IOCTL_GET_CLOCK_FREQUENCY: {
		struct ethosn_core *core = ethosn->core[0];

		ret = mutex_lock_interruptible(&core->mutex);
		if (ret)
			break;

		dev_dbg(core->dev,
			"IOCTL: Get clock frequency\n");

		ret = ethosn_clock_frequency();

		mutex_unlock(&core->mutex);

		break;
	}
	case ETHOSN_IOCTL_PING: {
		struct ethosn_core *core = ethosn->core[0];
		uint32_t num_pongs_before = core->num_pongs_received;
		int timeout;

		pm_runtime_get_sync(core->dev);

		/* Send a ping */
		ret = mutex_lock_interruptible(&core->mutex);
		if (ret)
			goto ping_put;

		ret = ethosn_send_ping(core);

		mutex_unlock(&core->mutex);

		if (ret != 0)
			goto ping_put;

		/* Wait for a pong to come back, with a timeout. */
		for (timeout = 0; timeout < ETHOSN_PING_TIMEOUT_US;
		     timeout += ETHOSN_PING_WAIT_US) {
			if (core->num_pongs_received > num_pongs_before)
				break;

			udelay(ETHOSN_PING_WAIT_US);
		}

		if (timeout >= ETHOSN_PING_TIMEOUT_US) {
			dev_err(core->dev,
				"Timeout while waiting for device to pong\n");
			ret = -ETIME;
			goto ping_put;
		}

		ret = 0;
ping_put:
		pm_runtime_mark_last_busy(core->dev);
		pm_runtime_put(core->dev);

		break;
	}
	default: {
		ret = -EINVAL;
		break;
	}
	}

	return ret;
}

static void ethosn_device_release(void *const opaque)
{
	struct ethosn_device *const ethosn = opaque;
	struct cdev *const cdev = &ethosn->cdev;
	int i = 0;

	while (i < ethosn->num_cores) {
		ethosn_set_power_ctrl(ethosn->core[i], false);
		if (ethosn->core[i]->irq_wq)
			destroy_workqueue(ethosn->core[i]->irq_wq);

		++i;
	}

	sysfs_remove_files(&ethosn->dev->kobj, attrs);
	debugfs_remove_recursive(ethosn->debug_dir);

	device_destroy(&ethosn_class, cdev->dev);
	cdev_del(cdev);
	ida_simple_remove(&ethosn_ida, MINOR(cdev->dev));
}

static int ethosn_device_create(struct ethosn_device *ethosn,
				int id)
{
	static const struct file_operations ethosn_fops = {
		.owner          = THIS_MODULE,
		.open           = &ethosn_open,
		.unlocked_ioctl = &ethosn_ioctl,
#ifdef CONFIG_COMPAT
		.compat_ioctl   = &ethosn_ioctl,
#endif
	};

	struct device *sysdev;
	dev_t devt;
	int ret;

	devt = MKDEV(ethosn_major, id);

	cdev_init(&ethosn->cdev, &ethosn_fops);
	ethosn->cdev.owner = THIS_MODULE;

	ret = cdev_add(&ethosn->cdev, devt, 1);
	if (ret) {
		dev_err(ethosn->dev, "unable to add character device\n");

		return ret;
	}

	sysdev = device_create(&ethosn_class, ethosn->dev, devt, ethosn,
			       "ethosn%d", id);
	if (IS_ERR(sysdev)) {
		dev_err(ethosn->dev, "device register failed\n");
		ret = PTR_ERR(sysdev);
		goto err_remove_chardev;
	}

	ret = sysfs_create_files(&ethosn->dev->kobj, attrs);
	if (ret)
		goto destroy_device;

	return devm_add_action_or_reset(ethosn->dev,
					ethosn_device_release,
					ethosn);

destroy_device:
	device_destroy(&ethosn_class, ethosn->cdev.dev);
err_remove_chardev:
	cdev_del(&ethosn->cdev);

	return ret;
}

static int ethosn_create_carveout_asset_allocator(struct ethosn_device *ethosn)
{
	int ret;

	ethosn->asset_allocator =
		devm_kzalloc(ethosn->dev,
			     sizeof(struct ethosn_dma_allocator *),
			     GFP_KERNEL);

	if (IS_ERR_OR_NULL(ethosn->asset_allocator))
		return PTR_ERR(ethosn->asset_allocator);

	ethosn->num_asset_allocs = 1;
	ethosn->asset_allocator[0] =
		ethosn_dma_top_allocator_create(ethosn->dev,
						ETHOSN_ALLOCATOR_CARVEOUT);
	ethosn->asset_allocator[0]->pid = ETHOSN_INVALID_PID;

	if (IS_ERR_OR_NULL(ethosn->asset_allocator[0]))
		return PTR_ERR(ethosn->asset_allocator[0]);

	/* Carveout case doesn't distinguish between streams, will get its
	 * address base from the device tree and doesn't use a speculative page
	 * so giving IO BUFFER and 0 as the parameters are still required.
	 */
	ret = ethosn_dma_sub_allocator_create(ethosn->dev,
					      ethosn->asset_allocator[0],
					      ETHOSN_STREAM_IO_BUFFER,
					      0U,
					      0U,
					      false);

	if (ret) {
		ethosn_dma_top_allocator_destroy(ethosn->dev,
						 &ethosn->asset_allocator[0]);

		return ret;
	}

	return 0;
}

static void ethosn_destroy_carveout_asset_allocators(
	struct ethosn_device *ethosn)
{
	ethosn_dma_sub_allocator_destroy(ethosn->asset_allocator[0],
					 ETHOSN_STREAM_IO_BUFFER);

	ethosn_dma_top_allocator_destroy(ethosn->dev,
					 &ethosn->asset_allocator[0]);
}

static int ethosn_create_carveout_main_allocator(struct ethosn_core *core)
{
	int ret;

	core->main_allocator =
		ethosn_dma_top_allocator_create(core->dev,
						ETHOSN_ALLOCATOR_CARVEOUT);

	if (IS_ERR_OR_NULL(core->main_allocator))
		return PTR_ERR(core->main_allocator);

	/* Carveout case doesn't distinguish between streams, will get its
	 * address base from the device tree and doesn't use a speculative page
	 * so giving IO BUFFER and 0 as the parameters are still required.
	 */
	ret = ethosn_dma_sub_allocator_create(core->dev,
					      core->main_allocator,
					      ETHOSN_STREAM_IO_BUFFER,
					      0U,
					      0U,
					      false);

	if (ret) {
		ethosn_dma_top_allocator_destroy(core->dev,
						 &core->main_allocator);

		return ret;
	}

	return 0;
}

void ethosn_destroy_carveout_main_allocator(struct ethosn_core *core)
{
	ethosn_dma_sub_allocator_destroy(core->main_allocator,
					 ETHOSN_STREAM_IO_BUFFER);

	ethosn_dma_top_allocator_destroy(core->dev, &core->main_allocator);
}

#ifdef ETHOSN_TZMP1

static int ethosn_check_smc_firmware_version(const struct device *dev)
{
	int ret = 0;
	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;

	ret = ethosn_smc_get_firmware_version(dev, &major, &minor, &patch);
	/* Error already printed by ethosn_smc_get_firmware_version */
	if (ret)
		return ret;

	dev_dbg(dev,
		"Firmware version from SiP service: %u.%u.%u\n",
		major, minor, patch);

	if (major != ETHOSN_FIRMWARE_VERSION_MAJOR) {
		dev_err(dev,
			"Unsupported firmware version from SiP service: %u.%u.%u. Version %u.x.x is required.\n",
			major, minor, patch, ETHOSN_FIRMWARE_VERSION_MAJOR);

		return -EPROTO;
	}

	return 0;
}

static int ethosn_populate_protected_firmware(struct ethosn_device *ethosn)
{
	struct ethosn_protected_firmware *firmware =
		&ethosn->protected_firmware;
	int ret;

	ret = ethosn_check_smc_firmware_version(ethosn->dev);
	if (ret)
		return ret;

	ret = ethosn_smc_get_firmware_mem_info(ethosn->dev,
					       &firmware->addr,
					       &firmware->size);
	if (ret)
		return ret;

	ret = ethosn_smc_get_firmware_offsets(ethosn->dev,
					      &firmware->ple_offset,
					      &firmware->stack_offset);
	if (ret)
		return ret;

	ret = ethosn_smc_get_firmware_va_map(ethosn->dev,
					     &firmware->firmware_addr_base,
					     &firmware->working_data_addr_base,
					     &firmware->command_stream_addr_base);
	if (ret)
		return ret;

	return 0;
}

#endif

#ifndef ETHOSN_NO_SMC
static int ethosn_check_smc_core_secure_status(const struct device *dev,
					       phys_addr_t core_addr)
{
	int ret = ethosn_smc_is_secure(dev, core_addr);

#ifdef ETHOSN_NS

	/*
	 * If the SiP service is available verify the NPU's secure status.
	 * If not, assume it's non-secure.
	 */
	if (ret == 1) {
		dev_err(dev,
			"Core in secure mode, non-secure kernel not supported.\n");

		return -EPERM;
	}

#else   /* Secure or TZMP1 */
	if (ret < 0)
		return ret;

	if (ret == 0) {
		dev_err(dev,
			"Core in non-secure mode is not supported for secure or TZMP1 kernel.\n");

		return -EPERM;
	}

#endif  /* Secure or TZMP1 */

	return 0;
}

#endif

/**
 * ethosn_driver_probe() - Do common probing functionality
 * @core:	ethosn core struct
 * @top_regs:	Register memory resource
 * @irq_numbers: List of IRQ numbers that we should listen to.
 * @irq_flags: List of the IRQF_TRIGGER_ flags to use for each corresponding irq
 *             number in the irq_numbers array.
 * @num_irqs: Number of valid entries in the irq_numbers and irq_flags arrays.
 * @force_firmware_level_interrupts: Whether to tell the firmware to send
 *                                   level-sensitive interrupts in all cases.
 */
static int ethosn_driver_probe(struct ethosn_core *core,
			       const struct resource *const top_regs,
			       const int irq_numbers[ETHOSN_MAX_NUM_IRQS],
			       const unsigned long
			       irq_flags[ETHOSN_MAX_NUM_IRQS],
			       unsigned int num_irqs,
			       bool force_firmware_level_interrupts)
{
	struct ethosn_profiling_config config = {};
	const phys_addr_t core_addr = top_regs->start;
	int ret = 0;
	const bool smmu_available = core->parent->smmu_available;

	#ifndef ETHOSN_NO_SMC
	ret = ethosn_check_smc_core_secure_status(core->dev, core_addr);
	#endif

	if (ret)
		return ret;

	mutex_init(&core->mutex);

	core->phys_addr = core_addr;
	core->top_regs = ethosn_map_iomem(core, top_regs, TOP_REG_SIZE);
	if (IS_ERR(core->top_regs))
		return PTR_ERR(core->top_regs);

	ret = ethosn_init_interrupt(core, irq_numbers, irq_flags, num_irqs);
	if (ret)
		return ret;

	/* Remember that we need to tell the firmware to use level interrupts.
	 * We can't do this now because the Ethos-N hasn't been turned on yet.
	 */
	core->force_firmware_level_interrupts =
		force_firmware_level_interrupts;

	/* Only create an allocator in the carveout case as the SMMU case
	 * implements main allocators as child nodes which will be populated
	 * by the platform driver for those nodes.
	 */
	if (!smmu_available) {
		ret = ethosn_create_carveout_main_allocator(core);
		if (ret)
			return ret;
	}

	/* Default to profiling disabled */
	config.enable_profiling = false;
	core->profiling.config = config;
	reset_profiling_counters(core);

	core->profiling.is_waiting_for_firmware_ack = false;
	core->profiling.firmware_buffer = NULL;
	core->profiling.firmware_buffer_pending = NULL;

	ret = ethosn_device_init(core);
	if (ret)
		goto destroy_allocator;

	/* Default to first asset allocator */
	ret = ethosn_reset_and_start_ethosn(core, 0);
	if (ret)
		goto device_deinit;

	pm_runtime_mark_last_busy(core->dev);
	pm_runtime_put_autosuspend(core->dev);

	dev_info(core->dev, "Ethos-N is running\n");

	return 0;

device_deinit:
	ethosn_device_deinit(core);
destroy_allocator:
	if (!smmu_available)
		ethosn_destroy_carveout_main_allocator(core);

	return ret;
}

/*****************************************************************************
 * Platform device
 *****************************************************************************/

/**
 * ethosn_pdev_enum_interrupts() - Do platform specific interrupt enumeration
 * @pdev: Platform device
 * @irq_numbers: List of IRQ numbers that we should listen to.
 * @irq_flags: List of the IRQF_TRIGGER_ flags to use for each corresponding irq
 *             number in the irq_numbers array.
 * @force_firmware_level_interrupts: Whether to tell the firmware to send
 *                                   level-sensitive interrupts in all cases.
 * Return:
 * * Number of valid entries in the irq_numbers and irq_flags arrays.
 * * -EINVAL - Invalid number of IRQ in dtb
 */
static int ethosn_pdev_enum_interrupts(struct platform_device *pdev,
				       int *irq_numbers,
				       unsigned long *irq_flags,
				       bool *force_firmware_level_interrupts)
{
	int num_irqs = 0;
	int irq_count = platform_irq_count(pdev);
	int irq_idx;
	bool force_level_interrupts = true;

	if (irq_count > ETHOSN_MAX_NUM_IRQS) {
		dev_err(&pdev->dev, "Invalid number of IRQs %d > %d", irq_count,
			ETHOSN_MAX_NUM_IRQS);

		return -EINVAL;
	}

	/* Get details of all the IRQs defined in the device tree.
	 * Depending on the system configuration there may be just one
	 * or several.
	 * It is also possible that more than one IRQ is combined onto
	 * the same line.
	 */
	for (irq_idx = 0;
	     irq_idx < irq_count;
	     ++irq_idx) {
		int irq_idx_existing;
		int irq_number;
		unsigned long flags = IRQF_SHARED;
		struct resource *resource =
			platform_get_resource(pdev, IORESOURCE_IRQ,
					      irq_idx);
		if (!resource) {
			dev_err(&pdev->dev,
				"platform_get_resource failed for IRQ index %d.\n",
				irq_idx);

			return -EINVAL;
		}

		if (resource->flags & IORESOURCE_IRQ_HIGHEDGE) {
			flags |= IRQF_TRIGGER_RISING;
			force_level_interrupts = false;
		} else if (resource->flags & IORESOURCE_IRQ_HIGHLEVEL) {
			flags |= IRQF_TRIGGER_HIGH;
		} else {
			dev_err(&pdev->dev,
				"Invalid interrupt configuration for IRQ index %d.\n",
				irq_idx);

			return -EINVAL;
		}

		irq_number = platform_get_irq(pdev, irq_idx);
		if (irq_number < 0) {
			dev_err(&pdev->dev,
				"platform_get_irq failed for IRQ index %d.\n",
				irq_idx);

			return -EINVAL;
		}

		/* Check if we have already seen an IRQ with the same
		 * number. (i.e. a line that is shared for more than
		 * one interrupt).
		 */
		for (irq_idx_existing = 0; irq_idx_existing < num_irqs;
		     ++irq_idx_existing)
			if (irq_numbers[irq_idx_existing] == irq_number)
				break;

		if (irq_idx_existing == num_irqs) {
			/* Not a shared line.
			 * Store the irq number and flags for use by
			 * ethosn_driver_probe.
			 */
			irq_numbers[num_irqs] = irq_number;
			if (strcmp(resource->name, "job") == 0) {
				irq_flags[num_irqs] = flags;
			} else if (strcmp(resource->name, "err") == 0) {
				irq_flags[num_irqs] = flags;
			} else if (strcmp(resource->name, "debug") ==
				   0) {
				irq_flags[num_irqs] = flags;
			} else {
				dev_err(&pdev->dev,
					"Unknown interrupt name '%s'.\n",
					resource->name);

				return -EINVAL;
			}

			++num_irqs;
		} else {
			/* If the line is shared, then this must be a
			 * level-based interrupt, so modify any
			 * previously set flags.
			 * We must also tell the firmware to send level-
			 * sensitive interrupts in all cases, so they
			 * can be safely ORed.
			 */
			irq_flags[irq_idx_existing] =
				IRQF_SHARED | IRQF_TRIGGER_HIGH;
			force_level_interrupts = true;
		}
	}

	*force_firmware_level_interrupts = force_level_interrupts;

	return num_irqs;
}

/**
 * ethosn_pdev_remove() - Do platform specific remove
 * @pdev: Platform device
 * Return:
 * * 0 - OK
 */
static int ethosn_pdev_remove(struct platform_device *pdev)
{
	struct ethosn_device *ethosn =
		dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "Depopulating children");
	/* Force depopulating children */
	of_platform_depopulate(&pdev->dev);

	/* Note that this function can get called if ethosn_pdev_probe fails
	 * halfway through, so we can't rely on things having been fully set up
	 */
	if (ethosn && !ethosn->smmu_available)
		ethosn_destroy_carveout_asset_allocators(ethosn);

	return 0;
}

/**
 * ethosn_pdev_num_compat() - Count number of compatible child nodes
 * @pdev: Platform device
 * @compatible: Compatible string to search for
 * Return:
 * * Number of nodes with that compatible string
 */
static unsigned int ethosn_pdev_num_compat(struct platform_device *pdev,
					   const char *compatible)
{
	unsigned int num_compatible = 0;
	struct device_node *node = NULL;

	for_each_available_child_of_node(pdev->dev.of_node, node) {
		if (of_device_is_compatible(node, compatible))
			num_compatible++;
	}

	dev_info(&pdev->dev, "num of %s = %u\n", compatible, num_compatible);

	return num_compatible;
}

static int ethosn_pdev_get_smmu_status_from_dts(struct device *dev,
						bool *smmu_available)
{
	struct device_node *asset_allocator_node = NULL;
	struct device_node *child = NULL;
	bool iommu_found = false;
	int len = 0;

	/* Both main and asset allocators should contain a child node
	 * with the iommu property, and should not exist in the
	 * device tree without this property. It is sufficent to check
	 * the asset allocator for an iommu property.
	 */
	for_each_available_child_of_node(dev->of_node, child) {
		if (of_device_is_compatible(child,
					    ETHOSN_ASSET_ALLOC_DRIVER_NAME))
			break;
	}

	if (!child)
		goto exit;

	asset_allocator_node = child;

	/* if asset allocator is present then iommu should be present
	 * in its child nodes. But this is checked explicitly here
	 * anyways. Also its enough to check for only one node of asset
	 * allocator.
	 */
	child = of_get_next_available_child(asset_allocator_node, NULL);
	iommu_found = !IS_ERR_OR_NULL(of_find_property(child,
						       "iommus",
						       &len));
	if (!iommu_found)
		return -EINVAL;

exit:
	*smmu_available = iommu_found;

	return 0;
}

static int ethosn_check_main_allocator(const struct ethosn_core *core)
{
	struct ethosn_device *ethosn;
	struct ethosn_dma_allocator *main_allocator;

	if (!core)
		return -EINVAL;

	ethosn = core->parent;
	if (!ethosn)
		return -EINVAL;

	main_allocator = core->main_allocator;
	if (!main_allocator) {
		dev_err(core->dev, "Main allocator not found.\n");

		return -ENODEV;
	}

	if (!ethosn_get_sub_allocator(main_allocator, ETHOSN_STREAM_FIRMWARE)) {
		dev_err(core->dev,
			"Firmware sub allocator probe failed.\n");

		return -ENODEV;
	}

	if (!ethosn_get_sub_allocator(main_allocator,
				      ETHOSN_STREAM_WORKING_DATA)) {
		dev_err(core->dev,
			"Working data sub allocator probe failed.\n");

		return -ENODEV;
	}

	return 0;
}

static int ethosn_check_asset_allocators(const struct ethosn_device *ethosn,
					 unsigned int num_of_asset_allocs)
{
	struct ethosn_dma_allocator **dma_allocator_list;
	enum ethosn_stream_type stream_type;
	unsigned int idx;

	dma_allocator_list = ethosn->asset_allocator;
	if (!dma_allocator_list) {
		dev_err(ethosn->dev, "No asset allocators found\n");

		return -EINVAL;
	}

	if (num_of_asset_allocs != ethosn->num_asset_allocs) {
		dev_err(ethosn->dev,
			"Asset allocator probe failed expected %u successfully probed %u\n",
			num_of_asset_allocs, ethosn->num_asset_allocs);

		return -EINVAL;
	}

	for (idx = 0; idx < num_of_asset_allocs; idx++) {
		stream_type = ETHOSN_STREAM_COMMAND_STREAM;
		if (!ethosn_get_sub_allocator(dma_allocator_list[idx],
					      stream_type))
			goto sub_alloc_err;

		stream_type = ETHOSN_STREAM_WEIGHT_DATA;
		if (!ethosn_get_sub_allocator(dma_allocator_list[idx],
					      stream_type))
			goto sub_alloc_err;

		stream_type = ETHOSN_STREAM_IO_BUFFER;
		if (!ethosn_get_sub_allocator(dma_allocator_list[idx],
					      stream_type))
			goto sub_alloc_err;

		stream_type = ETHOSN_STREAM_INTERMEDIATE_BUFFER;
		if (!ethosn_get_sub_allocator(dma_allocator_list[idx],
					      stream_type))
			goto sub_alloc_err;
	}

	return 0;

sub_alloc_err:
	dev_err(ethosn->dev,
		"Sub allocator probe failed for asset %u stream type %u\n", idx,
		stream_type);

	return -ENODEV;
}

static int ethosn_check_allocator_probe_status(struct ethosn_device *ethosn,
					       unsigned int num_of_asset_allocs)
{
	int idx;
	int ret;

	for (idx = 0; idx < ethosn->num_cores; ++idx) {
		ret = ethosn_check_main_allocator(ethosn->core[idx]);
		if (ret) {
			dev_err(ethosn->dev,
				"Failed to probe main allocator for core %u\n",
				idx);

			return ret;
		}
	}

	ret = ethosn_check_asset_allocators(ethosn, num_of_asset_allocs);
	if (ret)
		dev_err(ethosn->dev, "Failed to probe all asset allocators\n");

	return ret;
}

/**
 * ethosn_pdev_probe() - Do platform specific probing
 * @pdev: Platform device
 * Return:
 * * 0 - OK
 * * -EINVAL - Invalid argument
 */
static int ethosn_pdev_probe(struct platform_device *pdev)
{
	int ret = -ENOMEM;
	int num_irqs;
	int irq_numbers[ETHOSN_MAX_NUM_IRQS];
	unsigned long irq_flags[ETHOSN_MAX_NUM_IRQS];
	bool force_firmware_level_interrupts = false;
	unsigned int num_of_npus = 0;
	unsigned int num_of_asset_allocs = 0;
	int resource_idx = 0;
	struct ethosn_device *ethosn = NULL;
	int platform_id = -1;
	char name[16];
	bool smmu_available;

	ret = ethosn_pdev_get_smmu_status_from_dts(&pdev->dev, &smmu_available);

	if (ret) {
		dev_err(&pdev->dev, "Failed to locate iommus in allocators\n");

		return ret;
	}

#ifdef ETHOSN_TZMP1
	if (!smmu_available) {
		dev_err(&pdev->dev,
			"TZMP1 support requires the NPU to use an SMMU\n");

		return -EINVAL;
	}

#endif

	dma_set_mask_and_coherent(&pdev->dev,
				  DMA_BIT_MASK(ETHOSN_SMMU_MAX_ADDR_BITS));

	num_of_npus = ethosn_pdev_num_compat(pdev,
					     ETHOSN_CORE_DRIVER_NAME);
	if (num_of_npus == 0) {
		dev_info(&pdev->dev, "Failed to probe any NPU\n");

		return -EINVAL;
	}

#ifdef ETHOSN_TZMP1
	if (num_of_npus > ETHOSN_TZMP1_CORE_NUM_MAX) {
		dev_err(&pdev->dev, "TZMP1 only supports one core. Found %u\n",
			num_of_npus);

		return -EINVAL;
	}

#else
	if (num_of_npus > ETHOSN_CORE_NUM_MAX) {
		dev_err(&pdev->dev, "Invalid number of cores, max = %d\n",
			ETHOSN_CORE_NUM_MAX);

		return -EINVAL;
	}

#endif

#ifndef ETHOSN_NS
	ret = ethosn_smc_version_check(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev,
			"SiP service required for secure or TZMP1 kernel.\n");

		return -EPERM;
	}

#endif

	platform_id = ida_simple_get(&ethosn_ida, 0,
				     ETHOSN_MAX_DEVICES,
				     GFP_KERNEL);
	if (platform_id < 0)
		return platform_id;

	/* We need to allocate the parent device (ie struct
	 * ethosn_parent_device) only for the first time.
	 */
	dev_dbg(&pdev->dev, "Probing Ethos-N device id %u with %u core%s\n",
		platform_id, num_of_npus, num_of_npus > 1 ? "s" : "");

	ethosn = devm_kzalloc(&pdev->dev, sizeof(*ethosn),
			      GFP_KERNEL);
	if (!ethosn)
		goto err_early_exit;

	/* Store the first device for testing. If there is more than one device,
	 * just use the first one.
	 */
	if (!ethosn_global_device_for_testing)
		ethosn_global_device_for_testing = ethosn;

	ethosn->parent_id = platform_id;
	ethosn->dev = &pdev->dev;

	ethosn->current_busy_cores = 0;
	ethosn->status_mask = 0;
	ethosn->smmu_available = smmu_available;

#ifdef ETHOSN_TZMP1

	ret = ethosn_populate_protected_firmware(ethosn);
	if (ret)
		goto err_free_ethosn;

#endif  /* ETHOSN_TZMP1 */

	snprintf(name, sizeof(name), "ethosn%d", ethosn->parent_id);
	ethosn->debug_dir = debugfs_create_dir(name, NULL);
	if (IS_ERR_OR_NULL(ethosn->debug_dir)) {
		if (PTR_ERR(ethosn->debug_dir) == -ENODEV) {
			dev_dbg(&pdev->dev,
				"debugfs is not enabled in the kernel, so no ethosn debugfs nodes will be created");
		} else {
			dev_err(&pdev->dev,
				"debugfs_create_dir failed with: %ld",
				PTR_ERR(ethosn->debug_dir));
			goto err_free_ethosn;
		}
	}

	INIT_LIST_HEAD(&ethosn->queue.inference_queue);

	/* Only create an allocator in the carveout case as the SMMU case
	 * implements asset allocators as child nodes which will be populated
	 * by the platform driver for those nodes.
	 */
	if (!smmu_available) {
		ret = ethosn_create_carveout_asset_allocator(ethosn);
		if (ret)
			goto err_remove_debugfs;
	} else {
		num_of_asset_allocs =
			ethosn_pdev_num_compat(pdev,
					       ETHOSN_ASSET_ALLOC_DRIVER_NAME);

		if (num_of_asset_allocs == 0) {
			dev_info(&pdev->dev,
				 "Failed to probe any asset allocators\n");

			ret = -EINVAL;
			goto err_free_ethosn;
		}

		if (num_of_asset_allocs > ETHOSN_ASSET_ALLOC_NUM_MAX) {
			dev_err(&pdev->dev,
				"Invalid number of asset allocators, max = %d\n",
				ETHOSN_ASSET_ALLOC_NUM_MAX);

			ret = -EINVAL;
			goto err_free_ethosn;
		}

		/* Allocate space for num_of_asset_allocs asset allocators */
		ethosn->asset_allocator =
			devm_kzalloc(&pdev->dev,
				     (sizeof(struct ethosn_dma_allocator *) *
				      num_of_asset_allocs),
				     GFP_KERNEL);

		if (!ethosn->asset_allocator)
			goto err_free_ethosn;
	}

	/* Allocate space for num_of_npus ethosn cores */
	ethosn->core = devm_kzalloc(&pdev->dev,
				    (sizeof(struct ethosn_core *) *
				     num_of_npus),
				    GFP_KERNEL);

	if (!ethosn->core)
		goto err_destroy_allocator;

	dev_set_drvdata(&pdev->dev, ethosn);

	/* We need to populate child platform devices once parent
	 * device has been allocated and passed as device driver data
	 */
	dev_dbg(&pdev->dev, "Populating children\n");

	ret = of_platform_default_populate(pdev->dev.of_node, NULL, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to populate child devices\n");

		goto err_free_core_list;
	}

	/*
	 * Child device probing errors are not propagated back to the populate
	 * call so verify that the expected number of cores were setup
	 */
	if (ethosn->num_cores != num_of_npus) {
		dev_err(&pdev->dev, "Failed to populate all child devices\n");

		goto err_depopulate_device;
	}

	/* check if all main, asset allocators and their sub allocators are
	 * successfully probed.
	 */
	if (smmu_available) {
		ret = ethosn_check_allocator_probe_status(ethosn,
							  num_of_asset_allocs);
		if (ret)
			goto err_depopulate_device;
	}

	dev_dbg(&pdev->dev, "Populated %d children\n", ethosn->num_cores);

	mutex_init(&ethosn->mutex);

	mutex_init(&ethosn->queue.inference_queue_mutex);

	/* Currently we assume that the reserved memory is
	 * common to all the NPUs
	 */
	dev_dbg(&pdev->dev, "Init reserved mem\n");

	ret = ethosn_init_reserved_mem(&pdev->dev);
	if (ret)
		dev_dbg(&pdev->dev,
			"Reserved mem not present or init failed\n");

	if (!smmu_available)
		if (ret)
			goto err_depopulate_device;

	dev_dbg(&pdev->dev, "smmu available\n");
	/* Enumerate irqs */
	num_irqs = ethosn_pdev_enum_interrupts(
		pdev,
		irq_numbers,
		irq_flags,
		&force_firmware_level_interrupts);

	if (num_irqs < 0) {
		ret = num_irqs;
		goto err_depopulate_device;
	}

	/* At this point, all child device have been populated.
	 * Now the ethosn cores can be properly probed.
	 */
	for (resource_idx = 0; resource_idx < ethosn->num_cores;
	     ++resource_idx) {
		struct resource *top_regs = platform_get_resource(
			pdev,
			IORESOURCE_MEM,
			resource_idx);

		if (!ethosn->core[resource_idx]->dev) {
			dev_err(&pdev->dev,
				"NULL ethosn-core device reference");

			ret = -EINVAL;
			goto err_depopulate_device;
		}

		if (!top_regs) {
			dev_err(ethosn->core[resource_idx]->dev,
				"ethosn-core address not specified");

			ret = -EINVAL;
			goto err_depopulate_device;
		}

		ret = ethosn_driver_probe(ethosn->core[resource_idx], top_regs,
					  irq_numbers, irq_flags, num_irqs,
					  force_firmware_level_interrupts);
		if (ret)
			goto err_depopulate_device;
	}

	ret = ethosn_device_create(ethosn, platform_id);
	if (ret)
		goto err_depopulate_device;

	return 0;

err_depopulate_device:
	ethosn_pdev_remove(pdev);
err_free_core_list:
	devm_kfree(&pdev->dev, ethosn->core);

err_destroy_allocator:
	if (!smmu_available)
		ethosn_destroy_carveout_asset_allocators(ethosn);
	else
		devm_kfree(&pdev->dev, ethosn->asset_allocator);

err_remove_debugfs:
	if (!IS_ERR_OR_NULL(ethosn->debug_dir))
		debugfs_remove_recursive(ethosn->debug_dir);

err_free_ethosn:
	devm_kfree(&pdev->dev, ethosn);
	dev_set_drvdata(&pdev->dev, NULL);

err_early_exit:
	ida_simple_remove(&ethosn_ida, platform_id);

	return ret;
}

struct ethosn_device *ethosn_get_global_device_for_testing(void)
{
	return ethosn_global_device_for_testing;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_get_global_device_for_testing);

static const struct of_device_id ethosn_pdev_match[] = {
	{ .compatible = ETHOSN_DRIVER_NAME },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, ethosn_pdev_match);

static struct platform_driver ethosn_pdev_driver = {
	.probe                  = &ethosn_pdev_probe,
	.remove                 = &ethosn_pdev_remove,
	.driver                 = {
		.name           = ETHOSN_DRIVER_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(ethosn_pdev_match),
	},
};

/*****************************************************************************
 * Module initialization and destruction
 *****************************************************************************/

static int ethosn_major_init(void)
{
	dev_t devt;
	int ret;

	ret = alloc_chrdev_region(&devt, 0, ETHOSN_MAX_DEVICES,
				  ETHOSN_DRIVER_NAME);
	if (ret)
		return ret;

	ethosn_major = MAJOR(devt);

	return 0;
}

static void ethosn_major_cleanup(void)
{
	unregister_chrdev_region(MKDEV(ethosn_major, 0), ETHOSN_MAX_DEVICES);
}

static int ethosn_class_init(void)
{
	int ret;

	/* This is the first time in here, set everything up properly */
	ret = ethosn_major_init();
	if (ret)
		return ret;

	ret = class_register(&ethosn_class);
	if (ret) {
		pr_err("class_register failed for device\n");
		goto cleanup_ethosn;
	}

	return 0;

cleanup_ethosn:
	ethosn_major_cleanup();

	return ret;
}

static void ethosn_class_release(void)
{
	class_unregister(&ethosn_class);
	ethosn_major_cleanup();
}

static int __init ethosn_init(void)
{
	int ret = ethosn_class_init();

	if (ret)
		return ret;

	ret = ethosn_main_allocator_platform_driver_register();

	if (ret)
		return ret;

	ret = ethosn_mem_stream_platform_driver_register();

	if (ret)
		return ret;

	ret = ethosn_core_platform_driver_register();

	if (ret)
		return ret;

	ret = ethosn_asset_allocator_platform_driver_register();

	if (ret)
		return ret;

	return platform_driver_register(&ethosn_pdev_driver);
}

static void __exit ethosn_exit(void)
{
	ethosn_core_platform_driver_unregister();
	ethosn_mem_stream_platform_driver_unregister();
	ethosn_asset_allocator_platform_driver_unregister();
	ethosn_main_allocator_platform_driver_unregister();
	platform_driver_unregister(&ethosn_pdev_driver);
	ethosn_class_release();
}

module_init(ethosn_init)
module_exit(ethosn_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arm Limited");
MODULE_DESCRIPTION("Arm Ethos-N Driver");
MODULE_VERSION(ETHOSN_DRIVER_VERSION);
/* From kernel version 5.15, the dma_buf symbols are in a separate namespace */
#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
MODULE_IMPORT_NS(DMA_BUF);
#endif
