// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Apex chip.
 *
 * Copyright (C) 2018 Google, Inc.
 */

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include "apex.h"

#include "gasket_core.h"
#include "gasket_interrupt.h"
#include "gasket_logging.h"
#include "gasket_page_table.h"
#include "gasket_sysfs.h"

/* Constants */
#define APEX_DEVICE_NAME "Apex"
#define APEX_DRIVER_VERSION "1.0"

/* CSRs are in BAR 2. */
#define APEX_BAR_INDEX 2

#define APEX_PCI_VENDOR_ID 0x1ac1
#define APEX_PCI_DEVICE_ID 0x089a

/* Bar Offsets. */
#define APEX_BAR_OFFSET 0
#define APEX_CM_OFFSET 0x1000000

/* The sizes of each Apex BAR 2. */
#define APEX_BAR_BYTES 0x100000
#define APEX_CH_MEM_BYTES (PAGE_SIZE * MAX_NUM_COHERENT_PAGES)

/* The number of user-mappable memory ranges in BAR2 of a Apex chip. */
#define NUM_REGIONS 3

/* The number of nodes in a Apex chip. */
#define NUM_NODES 1

/*
 * The total number of entries in the page table. Should match the value read
 * from the register APEX_BAR2_REG_KERNEL_HIB_PAGE_TABLE_SIZE.
 */
#define APEX_PAGE_TABLE_TOTAL_ENTRIES 8192

/* Enumeration of the supported sysfs entries. */
enum sysfs_attribute_type {
	ATTR_KERNEL_HIB_PAGE_TABLE_SIZE,
	ATTR_KERNEL_HIB_SIMPLE_PAGE_TABLE_SIZE,
	ATTR_KERNEL_HIB_NUM_ACTIVE_PAGES,
};

/*
 * Register offsets into BAR2 memory.
 * Only values necessary for driver implementation are defined.
 */
enum apex_bar2_regs {
	APEX_BAR2_REG_SCU_BASE = 0x1A300,
	APEX_BAR2_REG_KERNEL_HIB_PAGE_TABLE_SIZE = 0x46000,
	APEX_BAR2_REG_KERNEL_HIB_EXTENDED_TABLE = 0x46008,
	APEX_BAR2_REG_KERNEL_HIB_TRANSLATION_ENABLE = 0x46010,
	APEX_BAR2_REG_KERNEL_HIB_INSTR_QUEUE_INTVECCTL = 0x46018,
	APEX_BAR2_REG_KERNEL_HIB_INPUT_ACTV_QUEUE_INTVECCTL = 0x46020,
	APEX_BAR2_REG_KERNEL_HIB_PARAM_QUEUE_INTVECCTL = 0x46028,
	APEX_BAR2_REG_KERNEL_HIB_OUTPUT_ACTV_QUEUE_INTVECCTL = 0x46030,
	APEX_BAR2_REG_KERNEL_HIB_SC_HOST_INTVECCTL = 0x46038,
	APEX_BAR2_REG_KERNEL_HIB_TOP_LEVEL_INTVECCTL = 0x46040,
	APEX_BAR2_REG_KERNEL_HIB_FATAL_ERR_INTVECCTL = 0x46048,
	APEX_BAR2_REG_KERNEL_HIB_DMA_PAUSE = 0x46050,
	APEX_BAR2_REG_KERNEL_HIB_DMA_PAUSE_MASK = 0x46058,
	APEX_BAR2_REG_KERNEL_HIB_STATUS_BLOCK_DELAY = 0x46060,
	APEX_BAR2_REG_KERNEL_HIB_MSIX_PENDING_BIT_ARRAY0 = 0x46068,
	APEX_BAR2_REG_KERNEL_HIB_MSIX_PENDING_BIT_ARRAY1 = 0x46070,
	APEX_BAR2_REG_KERNEL_HIB_PAGE_TABLE_INIT = 0x46078,
	APEX_BAR2_REG_KERNEL_HIB_MSIX_TABLE_INIT = 0x46080,
	APEX_BAR2_REG_KERNEL_WIRE_INT_PENDING_BIT_ARRAY = 0x48778,
	APEX_BAR2_REG_KERNEL_WIRE_INT_MASK_ARRAY = 0x48780,
	APEX_BAR2_REG_USER_HIB_DMA_PAUSE = 0x486D8,
	APEX_BAR2_REG_USER_HIB_DMA_PAUSED = 0x486E0,
	APEX_BAR2_REG_IDLEGENERATOR_IDLEGEN_IDLEREGISTER = 0x4A000,
	APEX_BAR2_REG_KERNEL_HIB_PAGE_TABLE = 0x50000,

	/* Error registers - Used mostly for debug */
	APEX_BAR2_REG_USER_HIB_ERROR_STATUS = 0x86f0,
	APEX_BAR2_REG_SCALAR_CORE_ERROR_STATUS = 0x41a0,
};

/* Addresses for packed registers. */
#define APEX_BAR2_REG_AXI_QUIESCE (APEX_BAR2_REG_SCU_BASE + 0x2C)
#define APEX_BAR2_REG_GCB_CLOCK_GATE (APEX_BAR2_REG_SCU_BASE + 0x14)
#define APEX_BAR2_REG_SCU_0 (APEX_BAR2_REG_SCU_BASE + 0xc)
#define APEX_BAR2_REG_SCU_1 (APEX_BAR2_REG_SCU_BASE + 0x10)
#define APEX_BAR2_REG_SCU_2 (APEX_BAR2_REG_SCU_BASE + 0x14)
#define APEX_BAR2_REG_SCU_3 (APEX_BAR2_REG_SCU_BASE + 0x18)
#define APEX_BAR2_REG_SCU_4 (APEX_BAR2_REG_SCU_BASE + 0x1c)
#define APEX_BAR2_REG_SCU_5 (APEX_BAR2_REG_SCU_BASE + 0x20)

#define SCU3_RG_PWR_STATE_OVR_BIT_OFFSET 26
#define SCU3_RG_PWR_STATE_OVR_MASK_WIDTH 2
#define SCU3_CUR_RST_GCB_BIT_MASK 0x10
#define SCU2_RG_RST_GCB_BIT_MASK 0xc

/* Configuration for page table. */
static struct gasket_page_table_config apex_page_table_configs[NUM_NODES] = {
	{
		.id = 0,
		.mode = GASKET_PAGE_TABLE_MODE_NORMAL,
		.total_entries = APEX_PAGE_TABLE_TOTAL_ENTRIES,
		.base_reg = APEX_BAR2_REG_KERNEL_HIB_PAGE_TABLE,
		.extended_reg = APEX_BAR2_REG_KERNEL_HIB_EXTENDED_TABLE,
		.extended_bit = APEX_EXTENDED_SHIFT,
	},
};

/* Function declarations */
static int __init apex_init(void);
static void apex_exit(void);

static int apex_add_dev_cb(struct gasket_dev *gasket_dev);

static int apex_sysfs_setup_cb(struct gasket_dev *gasket_dev);

static int apex_device_cleanup(struct gasket_dev *gasket_dev);

static int apex_device_open_cb(struct gasket_dev *gasket_dev);

static ssize_t sysfs_show(
	struct device *device, struct device_attribute *attr, char *buf);

static int apex_reset(struct gasket_dev *gasket_dev, uint type);

static int apex_get_status(struct gasket_dev *gasket_dev);

static bool apex_ioctl_check_permissions(struct file *file, uint cmd);

static long apex_ioctl(struct file *file, uint cmd, void __user *argp);

static long apex_clock_gating(struct gasket_dev *gasket_dev,
			      struct apex_gate_clock_ioctl __user *argp);

static int apex_enter_reset(struct gasket_dev *gasket_dev, uint type);

static int apex_quit_reset(struct gasket_dev *gasket_dev, uint type);

static bool is_gcb_in_reset(struct gasket_dev *gasket_dev);

/* Data definitions */

/* The data necessary to display this file's sysfs entries. */
static struct gasket_sysfs_attribute apex_sysfs_attrs[] = {
	GASKET_SYSFS_RO(node_0_page_table_entries, sysfs_show,
			ATTR_KERNEL_HIB_PAGE_TABLE_SIZE),
	GASKET_SYSFS_RO(node_0_simple_page_table_entries, sysfs_show,
			ATTR_KERNEL_HIB_SIMPLE_PAGE_TABLE_SIZE),
	GASKET_SYSFS_RO(node_0_num_mapped_pages, sysfs_show,
			ATTR_KERNEL_HIB_NUM_ACTIVE_PAGES),
	GASKET_END_OF_ATTR_ARRAY
};

static const struct pci_device_id apex_pci_ids[] = {
	{ PCI_DEVICE(APEX_PCI_VENDOR_ID, APEX_PCI_DEVICE_ID) }, { 0 }
};

/* The regions in the BAR2 space that can be mapped into user space. */
static const struct gasket_mappable_region mappable_regions[NUM_REGIONS] = {
	{ 0x40000, 0x1000 },
	{ 0x44000, 0x1000 },
	{ 0x48000, 0x1000 },
};

static const struct gasket_mappable_region cm_mappable_regions[1] = { { 0x0,
	APEX_CH_MEM_BYTES } };

/* Interrupt descriptors for Apex */
static struct gasket_interrupt_desc apex_interrupts[] = {
	{
		APEX_INTERRUPT_INSTR_QUEUE,
		APEX_BAR2_REG_KERNEL_HIB_INSTR_QUEUE_INTVECCTL,
		UNPACKED,
	},
	{
		APEX_INTERRUPT_INPUT_ACTV_QUEUE,
		APEX_BAR2_REG_KERNEL_HIB_INPUT_ACTV_QUEUE_INTVECCTL,
		UNPACKED
	},
	{
		APEX_INTERRUPT_PARAM_QUEUE,
		APEX_BAR2_REG_KERNEL_HIB_PARAM_QUEUE_INTVECCTL,
		UNPACKED
	},
	{
		APEX_INTERRUPT_OUTPUT_ACTV_QUEUE,
		APEX_BAR2_REG_KERNEL_HIB_OUTPUT_ACTV_QUEUE_INTVECCTL,
		UNPACKED
	},
	{
		APEX_INTERRUPT_SC_HOST_0,
		APEX_BAR2_REG_KERNEL_HIB_SC_HOST_INTVECCTL,
		PACK_0
	},
	{
		APEX_INTERRUPT_SC_HOST_1,
		APEX_BAR2_REG_KERNEL_HIB_SC_HOST_INTVECCTL,
		PACK_1
	},
	{
		APEX_INTERRUPT_SC_HOST_2,
		APEX_BAR2_REG_KERNEL_HIB_SC_HOST_INTVECCTL,
		PACK_2
	},
	{
		APEX_INTERRUPT_SC_HOST_3,
		APEX_BAR2_REG_KERNEL_HIB_SC_HOST_INTVECCTL,
		PACK_3
	},
	{
		APEX_INTERRUPT_TOP_LEVEL_0,
		APEX_BAR2_REG_KERNEL_HIB_TOP_LEVEL_INTVECCTL,
		PACK_0
	},
	{
		APEX_INTERRUPT_TOP_LEVEL_1,
		APEX_BAR2_REG_KERNEL_HIB_TOP_LEVEL_INTVECCTL,
		PACK_1
	},
	{
		APEX_INTERRUPT_TOP_LEVEL_2,
		APEX_BAR2_REG_KERNEL_HIB_TOP_LEVEL_INTVECCTL,
		PACK_2
	},
	{
		APEX_INTERRUPT_TOP_LEVEL_3,
		APEX_BAR2_REG_KERNEL_HIB_TOP_LEVEL_INTVECCTL,
		PACK_3
	},
	{
		APEX_INTERRUPT_FATAL_ERR,
		APEX_BAR2_REG_KERNEL_HIB_FATAL_ERR_INTVECCTL,
		UNPACKED
	},
};

static struct gasket_driver_desc apex_desc = {
	.name = "apex",
	.driver_version = APEX_DRIVER_VERSION,
	.major = 120,
	.minor = 0,
	.module = THIS_MODULE,
	.pci_id_table = apex_pci_ids,

	.num_page_tables = NUM_NODES,
	.page_table_bar_index = APEX_BAR_INDEX,
	.page_table_configs = apex_page_table_configs,
	.page_table_extended_bit = APEX_EXTENDED_SHIFT,

	.bar_descriptions = {
		GASKET_UNUSED_BAR,
		GASKET_UNUSED_BAR,
		{ APEX_BAR_BYTES, (VM_WRITE | VM_READ), APEX_BAR_OFFSET,
			NUM_REGIONS, mappable_regions, PCI_BAR },
		GASKET_UNUSED_BAR,
		GASKET_UNUSED_BAR,
		GASKET_UNUSED_BAR,
	},
	.coherent_buffer_description = {
		APEX_CH_MEM_BYTES,
		(VM_WRITE | VM_READ),
		APEX_CM_OFFSET,
	},
	.interrupt_type = PCI_MSIX,
	.interrupt_bar_index = APEX_BAR_INDEX,
	.num_interrupts = APEX_INTERRUPT_COUNT,
	.interrupts = apex_interrupts,
	.interrupt_pack_width = 7,

	.add_dev_cb = apex_add_dev_cb,
	.remove_dev_cb = NULL,

	.enable_dev_cb = NULL,
	.disable_dev_cb = NULL,

	.sysfs_setup_cb = apex_sysfs_setup_cb,
	.sysfs_cleanup_cb = NULL,

	.device_open_cb = apex_device_open_cb,
	.device_close_cb = apex_device_cleanup,

	.ioctl_handler_cb = apex_ioctl,
	.device_status_cb = apex_get_status,
	.hardware_revision_cb = NULL,
	.device_reset_cb = apex_reset,
};

/* Module registration boilerplate */
MODULE_DESCRIPTION("Google Apex driver");
MODULE_VERSION(APEX_DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("John Joseph <jnjoseph@google.com>");
MODULE_DEVICE_TABLE(pci, apex_pci_ids);
module_init(apex_init);
module_exit(apex_exit);

/* Allows device to enter power save upon driver close(). */
static int allow_power_save;

/* Allows SW based clock gating. */
static int allow_sw_clock_gating;

/* Allows HW based clock gating. */
/* Note: this is not mutual exclusive with SW clock gating. */
static int allow_hw_clock_gating = 1;

/* Act as if only GCB is instantiated. */
static int bypass_top_level;

module_param(allow_power_save, int, 0644);
module_param(allow_sw_clock_gating, int, 0644);
module_param(allow_hw_clock_gating, int, 0644);
module_param(bypass_top_level, int, 0644);

static int __init apex_init(void)
{
	return gasket_register_device(&apex_desc);
}

static void apex_exit(void)
{
	gasket_unregister_device(&apex_desc);
}

static int apex_add_dev_cb(struct gasket_dev *gasket_dev)
{
	ulong page_table_ready, msix_table_ready;
	int retries = 0;

	apex_reset(gasket_dev, 0);

	while (retries < APEX_RESET_RETRY) {
		page_table_ready =
			gasket_dev_read_64(
				gasket_dev, APEX_BAR_INDEX,
				APEX_BAR2_REG_KERNEL_HIB_PAGE_TABLE_INIT);
		msix_table_ready =
			gasket_dev_read_64(
				gasket_dev, APEX_BAR_INDEX,
				APEX_BAR2_REG_KERNEL_HIB_MSIX_TABLE_INIT);
		if (page_table_ready && msix_table_ready)
			break;
		schedule_timeout(msecs_to_jiffies(APEX_RESET_DELAY));
		retries++;
	}

	if (retries == APEX_RESET_RETRY) {
		if (!page_table_ready)
			gasket_log_error(
				gasket_dev, "Page table init timed out.");
		if (!msix_table_ready)
			gasket_log_error(
				gasket_dev, "MSI-X table init timed out.");
		return -ETIMEDOUT;
	}

	return 0;
}

static int apex_sysfs_setup_cb(struct gasket_dev *gasket_dev)
{
	return gasket_sysfs_create_entries(
		gasket_dev->dev_info.device, apex_sysfs_attrs);
}

/* On device open, we want to perform a core reinit reset. */
static int apex_device_open_cb(struct gasket_dev *gasket_dev)
{
	return gasket_reset_nolock(gasket_dev, APEX_CHIP_REINIT_RESET);
}

/**
 * apex_get_status - Set device status.
 * @dev: Apex device struct.
 *
 * Description: Check the device status registers and set the driver status
 *		to ALIVE or DEAD.
 *
 *		Returns 0 if status is ALIVE, a negative error number otherwise.
 */
static int apex_get_status(struct gasket_dev *gasket_dev)
{
	/* TODO: Check device status. */
	return GASKET_STATUS_ALIVE;
}

/**
 * apex_device_cleanup - Clean up Apex HW after close.
 * @gasket_dev: Gasket device pointer.
 *
 * Description: Resets the Apex hardware. Called on final close via
 * device_close_cb.
 */
static int apex_device_cleanup(struct gasket_dev *gasket_dev)
{
	u64 scalar_error;
	u64 hib_error;
	int ret = 0;

	hib_error = gasket_dev_read_64(
		gasket_dev, APEX_BAR_INDEX,
		APEX_BAR2_REG_USER_HIB_ERROR_STATUS);
	scalar_error = gasket_dev_read_64(
		gasket_dev, APEX_BAR_INDEX,
		APEX_BAR2_REG_SCALAR_CORE_ERROR_STATUS);

	gasket_log_debug(
		gasket_dev,
		"%s 0x%p hib_error 0x%llx scalar_error "
		"0x%llx.",
		__func__,
		gasket_dev, hib_error, scalar_error);

	if (allow_power_save)
		ret = apex_enter_reset(gasket_dev, APEX_CHIP_REINIT_RESET);

	return ret;
}

/**
 * apex_reset - Quits reset.
 * @gasket_dev: Gasket device pointer.
 *
 * Description: Resets the hardware, then quits reset.
 * Called on device open.
 *
 */
static int apex_reset(struct gasket_dev *gasket_dev, uint type)
{
	int ret;

	if (bypass_top_level)
		return 0;

	if (!is_gcb_in_reset(gasket_dev)) {
		/* We are not in reset - toggle the reset bit so as to force
		 * re-init of custom block
		 */
		gasket_log_debug(gasket_dev, "%s: toggle reset.", __func__);

		ret = apex_enter_reset(gasket_dev, type);
		if (ret)
			return ret;
	}
	ret = apex_quit_reset(gasket_dev, type);

	return ret;
}

/*
 * Enters GCB reset state.
 */
static int apex_enter_reset(struct gasket_dev *gasket_dev, uint type)
{
	if (bypass_top_level)
		return 0;

	/*
	 * Software reset:
	 * Enable sleep mode
	 *  - Software force GCB idle
	 *    - Enable GCB idle
	 */
	gasket_read_modify_write_64(
		gasket_dev, APEX_BAR_INDEX,
		APEX_BAR2_REG_IDLEGENERATOR_IDLEGEN_IDLEREGISTER, 0x0, 1, 32);

	/*    - Initiate DMA pause */
	gasket_dev_write_64(gasket_dev, 1, APEX_BAR_INDEX,
			    APEX_BAR2_REG_USER_HIB_DMA_PAUSE);

	/*    - Wait for DMA pause complete. */
	if (gasket_wait_with_reschedule(gasket_dev, APEX_BAR_INDEX,
					APEX_BAR2_REG_USER_HIB_DMA_PAUSED, 1, 1,
					APEX_RESET_DELAY, APEX_RESET_RETRY)) {
		gasket_log_error(gasket_dev,
				 "DMAs did not quiesce within timeout (%d ms)",
				 APEX_RESET_RETRY * APEX_RESET_DELAY);
		return -ETIMEDOUT;
	}

	/*  - Enable GCB reset (0x1 to rg_rst_gcb) */
	gasket_read_modify_write_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_2, 0x1, 2, 2);

	/*  - Enable GCB clock Gate (0x1 to rg_gated_gcb) */
	gasket_read_modify_write_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_2, 0x1, 2, 18);

	/*  - Enable GCB memory shut down (0x3 to rg_force_ram_sd) */
	gasket_read_modify_write_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3, 0x3, 2, 14);

	/*    - Wait for RAM shutdown. */
	if (gasket_wait_with_reschedule(gasket_dev, APEX_BAR_INDEX,
					APEX_BAR2_REG_SCU_3, 1 << 6, 1 << 6,
					APEX_RESET_DELAY, APEX_RESET_RETRY)) {
		gasket_log_error(
			gasket_dev,
			"RAM did not shut down within timeout (%d ms)",
			APEX_RESET_RETRY * APEX_RESET_DELAY);
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Quits GCB reset state.
 */
static int apex_quit_reset(struct gasket_dev *gasket_dev, uint type)
{
	u32 val0, val1;

	if (bypass_top_level)
		return 0;

	/*
	 * Disable sleep mode:
	 *  - Disable GCB memory shut down:
	 *    - b00: Not forced (HW controlled)
	 *    - b1x: Force disable
	 */
	gasket_read_modify_write_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3, 0x0, 2, 14);

	/*
	 *  - Disable software clock gate:
	 *    - b00: Not forced (HW controlled)
	 *    - b1x: Force disable
	 */
	gasket_read_modify_write_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_2, 0x0, 2, 18);

	/*
	 *  - Disable GCB reset (rg_rst_gcb):
	 *    - b00: Not forced (HW controlled)
	 *    - b1x: Force disable = Force not Reset
	 */
	gasket_read_modify_write_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_2, 0x2, 2, 2);

	/*    - Wait for RAM enable. */
	if (gasket_wait_with_reschedule(gasket_dev, APEX_BAR_INDEX,
					APEX_BAR2_REG_SCU_3, 1 << 6, 0,
					APEX_RESET_DELAY, APEX_RESET_RETRY)) {
		gasket_log_error(
			gasket_dev,
			"RAM did not enable within timeout (%d ms)",
			APEX_RESET_RETRY * APEX_RESET_DELAY);
		return -ETIMEDOUT;
	}

	/*    - Wait for Reset complete. */
	if (gasket_wait_with_reschedule(gasket_dev, APEX_BAR_INDEX,
					APEX_BAR2_REG_SCU_3,
					SCU3_CUR_RST_GCB_BIT_MASK, 0,
					APEX_RESET_DELAY, APEX_RESET_RETRY)) {
		gasket_log_error(
			gasket_dev,
			"GCB did not leave reset within timeout (%d ms)",
			APEX_RESET_RETRY * APEX_RESET_DELAY);
		return -ETIMEDOUT;
	}

	if (!allow_hw_clock_gating) {
		val0 = gasket_dev_read_32(
			gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3);
		/* Inactive and Sleep mode are disabled. */
		gasket_read_modify_write_32(
			gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3, 0x3,
			SCU3_RG_PWR_STATE_OVR_MASK_WIDTH,
			SCU3_RG_PWR_STATE_OVR_BIT_OFFSET);
		val1 = gasket_dev_read_32(
			gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3);
		gasket_log_debug(
			gasket_dev, "Disallow HW clock gating 0x%x -> 0x%x",
			val0, val1);
	} else {
		val0 = gasket_dev_read_32(
			gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3);
		/* Inactive mode enabled - Sleep mode disabled. */
		gasket_read_modify_write_32(
			gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3, 2,
			SCU3_RG_PWR_STATE_OVR_MASK_WIDTH,
			SCU3_RG_PWR_STATE_OVR_BIT_OFFSET);
		val1 = gasket_dev_read_32(
			gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3);
		gasket_log_debug(
			gasket_dev, "Allow HW clock gating 0x%x -> 0x%x", val0,
			val1);
	}

	return 0;
}

/*
 * Determines if GCB is in reset state.
 */
static bool is_gcb_in_reset(struct gasket_dev *gasket_dev)
{
	u32 val = gasket_dev_read_32(
		gasket_dev, APEX_BAR_INDEX, APEX_BAR2_REG_SCU_3);

	/* Masks rg_rst_gcb bit of SCU_CTRL_2 */
	return (val & SCU3_CUR_RST_GCB_BIT_MASK);
}

/*
 * Check permissions for Apex ioctls.
 * @file: File pointer from ioctl.
 * @cmd: ioctl command.
 *
 * Returns true if the current user may execute this ioctl, and false otherwise.
 */
static bool apex_ioctl_check_permissions(struct file *filp, uint cmd)
{
	return !!(filp->f_mode & FMODE_WRITE);
}

/*
 * Apex-specific ioctl handler.
 */
static long apex_ioctl(struct file *filp, uint cmd, void __user *argp)
{
	struct gasket_dev *gasket_dev = filp->private_data;

	if (!apex_ioctl_check_permissions(filp, cmd))
		return -EPERM;

	switch (cmd) {
	case APEX_IOCTL_GATE_CLOCK:
		return apex_clock_gating(gasket_dev, argp);
	default:
		return -ENOTTY; /* unknown command */
	}
}

/*
 * Gates or un-gates Apex clock.
 * @gasket_dev: Gasket device pointer.
 * @argp: User ioctl arg, pointer to a apex_gate_clock_ioctl struct.
 */
static long apex_clock_gating(struct gasket_dev *gasket_dev,
			      struct apex_gate_clock_ioctl __user *argp)
{
	struct apex_gate_clock_ioctl ibuf;

	if (bypass_top_level || !allow_sw_clock_gating)
		return 0;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	gasket_log_debug(gasket_dev, "%s %llu", __func__, ibuf.enable);

	if (ibuf.enable) {
		/* Quiesce AXI, gate GCB clock. */
		gasket_read_modify_write_32(
		    gasket_dev, APEX_BAR_INDEX,
		    APEX_BAR2_REG_AXI_QUIESCE, 0x1, 1, 16);
		gasket_read_modify_write_32(
		    gasket_dev, APEX_BAR_INDEX,
		    APEX_BAR2_REG_GCB_CLOCK_GATE, 0x1, 2, 18);
	} else {
		/* Un-gate GCB clock, un-quiesce AXI. */
		gasket_read_modify_write_32(
		    gasket_dev, APEX_BAR_INDEX,
		    APEX_BAR2_REG_GCB_CLOCK_GATE, 0x0, 2, 18);
		gasket_read_modify_write_32(
		    gasket_dev, APEX_BAR_INDEX,
		    APEX_BAR2_REG_AXI_QUIESCE, 0x0, 1, 16);
	}
	return 0;
}

/*
 * Display driver sysfs entries.
 * @device: Kernel device structure.
 * @attr: Attribute to display.
 * @buf: Buffer to which to write output.
 *
 * Description: Looks up the driver data and file-specific attribute data (the
 * type of the attribute), then fills "buf" accordingly.
 */
static ssize_t sysfs_show(
	struct device *device, struct device_attribute *attr, char *buf)
{
	int ret;
	struct gasket_dev *gasket_dev;
	struct gasket_sysfs_attribute *gasket_attr;
	enum sysfs_attribute_type type;

	gasket_dev = gasket_sysfs_get_device_data(device);
	if (!gasket_dev) {
		gasket_nodev_error("No Apex device sysfs mapping found");
		return -ENODEV;
	}

	gasket_attr = gasket_sysfs_get_attr(device, attr);
	if (!gasket_attr) {
		gasket_nodev_error("No Apex device sysfs attr data found");
		gasket_sysfs_put_device_data(device, gasket_dev);
		return -ENODEV;
	}

	type = (enum sysfs_attribute_type)gasket_sysfs_get_attr(device, attr);
	switch (type) {
	case ATTR_KERNEL_HIB_PAGE_TABLE_SIZE:
		ret = scnprintf(buf, PAGE_SIZE, "%u\n",
				gasket_page_table_num_entries(
					gasket_dev->page_table[0]));
		break;
	case ATTR_KERNEL_HIB_SIMPLE_PAGE_TABLE_SIZE:
		ret = scnprintf(buf, PAGE_SIZE, "%u\n",
				gasket_page_table_num_entries(
					gasket_dev->page_table[0]));
		break;
	case ATTR_KERNEL_HIB_NUM_ACTIVE_PAGES:
		ret = scnprintf(buf, PAGE_SIZE, "%u\n",
				gasket_page_table_num_active_pages(
					gasket_dev->page_table[0]));
		break;
	default:
		gasket_log_debug(
			gasket_dev, "Unknown attribute: %s", attr->attr.name);
		ret = 0;
		break;
	}

	gasket_sysfs_put_attr(device, gasket_attr);
	gasket_sysfs_put_device_data(device, gasket_dev);
	return ret;
}
