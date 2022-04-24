/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_FIRMWARE_H_
#define _KBASE_CSF_FIRMWARE_H_

#include "device/mali_kbase_device.h"
#include <csf/mali_kbase_csf_registers.h>

/*
 * PAGE_KERNEL_RO was only defined on 32bit ARM in 4.19 in:
 * Commit a3266bd49c721e2e0a71f352d83713fbd60caadb
 * Author: Luis R. Rodriguez <mcgrof@kernel.org>
 * Date:   Fri Aug 17 15:46:29 2018 -0700
 *
 * mm: provide a fallback for PAGE_KERNEL_RO for architectures
 *
 * Some architectures do not define certain PAGE_KERNEL_* flags, this is
 * either because:
 *
 * a) The way to implement some of these flags is *not yet ported*, or
 * b) The architecture *has no way* to describe them
 *
 * [snip]
 *
 * This can be removed once support of 32bit ARM kernels predating 4.19 is no
 * longer required.
 */
#ifndef PAGE_KERNEL_RO
#define PAGE_KERNEL_RO PAGE_KERNEL
#endif

/* Address space number to claim for the firmware. */
#define MCU_AS_NR 0
#define MCU_AS_BITMASK (1 << MCU_AS_NR)

/* Number of available Doorbells */
#define CSF_NUM_DOORBELL ((u8)24)

/* Offset to the first HW doorbell page */
#define CSF_HW_DOORBELL_PAGE_OFFSET ((u32)0x80000)

/* Size of HW Doorbell page, used to calculate the offset to subsequent pages */
#define CSF_HW_DOORBELL_PAGE_SIZE ((u32)0x10000)

/* Doorbell 0 is used by the driver. */
#define CSF_KERNEL_DOORBELL_NR ((u32)0)

/* Offset of name inside a trace buffer entry in the firmware image */
#define TRACE_BUFFER_ENTRY_NAME_OFFSET (0x1C)

/* All implementations of the host interface with major version 0 must comply
 * with these restrictions:
 */
/* GLB_GROUP_NUM: At least 3 CSGs, but no more than 31 */
#define MIN_SUPPORTED_CSGS 3
#define MAX_SUPPORTED_CSGS 31
/* GROUP_STREAM_NUM: At least 8 CSs per CSG, but no more than 32 */
#define MIN_SUPPORTED_STREAMS_PER_GROUP 8
/* MAX_SUPPORTED_STREAMS_PER_GROUP: Maximum CSs per csg. */
#define MAX_SUPPORTED_STREAMS_PER_GROUP 32

struct kbase_device;


/**
 * struct kbase_csf_mapping - Memory mapping for CSF memory.
 * @phys:      Physical memory allocation used by the mapping.
 * @cpu_addr:  Starting CPU address for the mapping.
 * @va_reg:    GPU virtual address region for the mapping.
 * @num_pages: Size of the mapping, in memory pages.
 */
struct kbase_csf_mapping {
	struct tagged_addr *phys;
	void *cpu_addr;
	struct kbase_va_region *va_reg;
	unsigned int num_pages;
};

/**
 * struct kbase_csf_trace_buffers - List and state of firmware trace buffers.
 * @list:       List of trace buffers descriptors.
 * @mcu_rw:     Metadata for the MCU shared memory mapping used for
 *              GPU-readable,writable/CPU-writable variables.
 * @mcu_write:  Metadata for the MCU shared memory mapping used for
 *              GPU-writable/CPU-readable variables.
 */
struct kbase_csf_trace_buffers {
	struct list_head list;
	struct kbase_csf_mapping mcu_rw;
	struct kbase_csf_mapping mcu_write;
};

/**
 * struct kbase_csf_cmd_stream_info - CSI provided by the firmware.
 *
 * @kbdev: Address of the instance of a GPU platform device that implements
 *         this interface.
 * @features: Bit field of CS features (e.g. which types of jobs
 *            are supported). Bits 7:0 specify the number of work registers(-1).
 *            Bits 11:8 specify the number of scoreboard entries(-1).
 * @input: Address of CSI input page.
 * @output: Address of CSI output page.
 */
struct kbase_csf_cmd_stream_info {
	struct kbase_device *kbdev;
	u32 features;
	void *input;
	void *output;
};

/**
 * kbase_csf_firmware_cs_input() - Set a word in a CS's input page
 *
 * @info: CSI provided by the firmware.
 * @offset: Offset of the word to be written, in bytes.
 * @value: Value to be written.
 */
void kbase_csf_firmware_cs_input(
	const struct kbase_csf_cmd_stream_info *info, u32 offset, u32 value);

/**
 * kbase_csf_firmware_cs_input_read() - Read a word in a CS's input page
 *
 * Return: Value of the word read from the CS's input page.
 *
 * @info: CSI provided by the firmware.
 * @offset: Offset of the word to be read, in bytes.
 */
u32 kbase_csf_firmware_cs_input_read(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset);

/**
 * kbase_csf_firmware_cs_input_mask() - Set part of a word in a CS's input page
 *
 * @info: CSI provided by the firmware.
 * @offset: Offset of the word to be modified, in bytes.
 * @value: Value to be written.
 * @mask: Bitmask with the bits to be modified set.
 */
void kbase_csf_firmware_cs_input_mask(
	const struct kbase_csf_cmd_stream_info *info, u32 offset,
	u32 value, u32 mask);

/**
 * kbase_csf_firmware_cs_output() - Read a word in a CS's output page
 *
 * Return: Value of the word read from the CS's output page.
 *
 * @info: CSI provided by the firmware.
 * @offset: Offset of the word to be read, in bytes.
 */
u32 kbase_csf_firmware_cs_output(
	const struct kbase_csf_cmd_stream_info *info, u32 offset);
/**
 * struct kbase_csf_cmd_stream_group_info - CSG interface provided by the
 *                                          firmware.
 *
 * @kbdev: Address of the instance of a GPU platform device that implements
 *         this interface.
 * @features: Bit mask of features. Reserved bits should be 0, and should
 *            be ignored.
 * @input: Address of global interface input page.
 * @output: Address of global interface output page.
 * @suspend_size: Size in bytes for normal suspend buffer for the CSG
 * @protm_suspend_size: Size in bytes for protected mode suspend buffer
 *                      for the CSG.
 * @stream_num: Number of CSs in the CSG.
 * @stream_stride: Stride in bytes in JASID0 virtual address between
 *                 CS capability structures.
 * @streams: Address of an array of CS capability structures.
 */
struct kbase_csf_cmd_stream_group_info {
	struct kbase_device *kbdev;
	u32 features;
	void *input;
	void *output;
	u32 suspend_size;
	u32 protm_suspend_size;
	u32 stream_num;
	u32 stream_stride;
	struct kbase_csf_cmd_stream_info *streams;
};

/**
 * kbase_csf_firmware_csg_input() - Set a word in a CSG's input page
 *
 * @info: CSG interface provided by the firmware.
 * @offset: Offset of the word to be written, in bytes.
 * @value: Value to be written.
 */
void kbase_csf_firmware_csg_input(
	const struct kbase_csf_cmd_stream_group_info *info, u32 offset,
	u32 value);

/**
 * kbase_csf_firmware_csg_input_read() - Read a word in a CSG's input page
 *
 * Return: Value of the word read from the CSG's input page.
 *
 * @info: CSG interface provided by the firmware.
 * @offset: Offset of the word to be read, in bytes.
 */
u32 kbase_csf_firmware_csg_input_read(
	const struct kbase_csf_cmd_stream_group_info *info, u32 offset);

/**
 * kbase_csf_firmware_csg_input_mask() - Set part of a word in a CSG's
 *                                       input page
 *
 * @info: CSG interface provided by the firmware.
 * @offset: Offset of the word to be modified, in bytes.
 * @value: Value to be written.
 * @mask: Bitmask with the bits to be modified set.
 */
void kbase_csf_firmware_csg_input_mask(
	const struct kbase_csf_cmd_stream_group_info *info, u32 offset,
	u32 value, u32 mask);

/**
 * kbase_csf_firmware_csg_output()- Read a word in a CSG's output page
 *
 * Return: Value of the word read from the CSG's output page.
 *
 * @info: CSG interface provided by the firmware.
 * @offset: Offset of the word to be read, in bytes.
 */
u32 kbase_csf_firmware_csg_output(
	const struct kbase_csf_cmd_stream_group_info *info, u32 offset);

/**
 * struct kbase_csf_global_iface - Global CSF interface
 *                                 provided by the firmware.
 *
 * @kbdev: Address of the instance of a GPU platform device that implements
 *         this interface.
 * @version: Bits 31:16 hold the major version number and 15:0 hold the minor
 *           version number. A higher minor version is backwards-compatible
 *           with a lower minor version for the same major version.
 * @features: Bit mask of features (e.g. whether certain types of job can
 *            be suspended). Reserved bits should be 0, and should be ignored.
 * @input: Address of global interface input page.
 * @output: Address of global interface output page.
 * @group_num: Number of CSGs supported.
 * @group_stride: Stride in bytes in JASID0 virtual address between
 *                CSG capability structures.
 * @prfcnt_size: Performance counters size.
 * @instr_features: Instrumentation features. (csf >= 1.1.0)
 * @groups: Address of an array of CSG capability structures.
 */
struct kbase_csf_global_iface {
	struct kbase_device *kbdev;
	u32 version;
	u32 features;
	void *input;
	void *output;
	u32 group_num;
	u32 group_stride;
	u32 prfcnt_size;
	u32 instr_features;
	struct kbase_csf_cmd_stream_group_info *groups;
};

/**
 * kbase_csf_firmware_global_input() - Set a word in the global input page
 *
 * @iface: CSF interface provided by the firmware.
 * @offset: Offset of the word to be written, in bytes.
 * @value: Value to be written.
 */
void kbase_csf_firmware_global_input(
	const struct kbase_csf_global_iface *iface, u32 offset, u32 value);

/**
 * kbase_csf_firmware_global_input_mask() - Set part of a word in the global
 *                                          input page
 *
 * @iface: CSF interface provided by the firmware.
 * @offset: Offset of the word to be modified, in bytes.
 * @value: Value to be written.
 * @mask: Bitmask with the bits to be modified set.
 */
void kbase_csf_firmware_global_input_mask(
	const struct kbase_csf_global_iface *iface, u32 offset,
	u32 value, u32 mask);

/**
 * kbase_csf_firmware_global_input_read() - Read a word in a global input page
 *
 * Return: Value of the word read from the global input page.
 *
 * @info: CSG interface provided by the firmware.
 * @offset: Offset of the word to be read, in bytes.
 */
u32 kbase_csf_firmware_global_input_read(
	const struct kbase_csf_global_iface *info, u32 offset);

/**
 * kbase_csf_firmware_global_output() - Read a word in the global output page
 *
 * Return: Value of the word read from the global output page.
 *
 * @iface: CSF interface provided by the firmware.
 * @offset: Offset of the word to be read, in bytes.
 */
u32 kbase_csf_firmware_global_output(
	const struct kbase_csf_global_iface *iface, u32 offset);

/**
 * kbase_csf_ring_doorbell() - Ring the doorbell
 *
 * @kbdev:       An instance of the GPU platform device
 * @doorbell_nr: Index of the HW doorbell page
 */
void kbase_csf_ring_doorbell(struct kbase_device *kbdev, int doorbell_nr);

/**
 * kbase_csf_read_firmware_memory - Read a value in a GPU address
 *
 * @kbdev:     Device pointer
 * @gpu_addr:  GPU address to read
 * @value:     output pointer to which the read value will be written.
 *
 * This function read a value in a GPU address that belongs to
 * a private firmware memory region. The function assumes that the location
 * is not permanently mapped on the CPU address space, therefore it maps it
 * and then unmaps it to access it independently.
 */
void kbase_csf_read_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 *value);

/**
 * kbase_csf_update_firmware_memory - Write a value in a GPU address
 *
 * @kbdev:     Device pointer
 * @gpu_addr:  GPU address to write
 * @value:     Value to write
 *
 * This function writes a given value in a GPU address that belongs to
 * a private firmware memory region. The function assumes that the destination
 * is not permanently mapped on the CPU address space, therefore it maps it
 * and then unmaps it to access it independently.
 */
void kbase_csf_update_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 value);

/**
 * kbase_csf_firmware_early_init() - Early initializatin for the firmware.
 * @kbdev: Kbase device
 *
 * Initialize resources related to the firmware. Must be called at kbase probe.
 *
 * Return: 0 if successful, negative error code on failure
 */
int kbase_csf_firmware_early_init(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_init() - Load the firmware for the CSF MCU
 * @kbdev: Kbase device
 *
 * Request the firmware from user space and load it into memory.
 *
 * Return: 0 if successful, negative error code on failure
 */
int kbase_csf_firmware_init(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_term() - Unload the firmware
 * @kbdev: Kbase device
 *
 * Frees the memory allocated by kbase_csf_firmware_init()
 */
void kbase_csf_firmware_term(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_ping - Send the ping request to firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * The function sends the ping request to firmware.
 */
void kbase_csf_firmware_ping(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_ping_wait - Send the ping request to firmware and waits.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * The function sends the ping request to firmware and waits to confirm it is
 * alive.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_firmware_ping_wait(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_set_timeout - Set a hardware endpoint progress timeout.
 *
 * @kbdev:   Instance of a GPU platform device that implements a CSF interface.
 * @timeout: The maximum number of GPU cycles that is allowed to elapse
 *           without forward progress before the driver terminates a GPU
 *           command queue group.
 *
 * Configures the progress timeout value used by the firmware to decide
 * when to report that a task is not making progress on an endpoint.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_firmware_set_timeout(struct kbase_device *kbdev, u64 timeout);

/**
 * kbase_csf_enter_protected_mode - Send the Global request to firmware to
 *                                  enter protected mode.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * The function must be called with kbdev->csf.scheduler.interrupt_lock held
 * and it does not wait for the protected mode entry to complete.
 */
void kbase_csf_enter_protected_mode(struct kbase_device *kbdev);

/**
 * kbase_csf_wait_protected_mode_enter - Wait for the completion of PROTM_ENTER
 *                                       Global request sent to firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function needs to be called after kbase_csf_enter_protected_mode() to
 * wait for the GPU to actually enter protected mode. GPU reset is triggered if
 * the wait is unsuccessful.
 */
void kbase_csf_wait_protected_mode_enter(struct kbase_device *kbdev);

static inline bool kbase_csf_firmware_mcu_halted(struct kbase_device *kbdev)
{
#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	return true;
#else
	return (kbase_reg_read(kbdev, GPU_CONTROL_REG(MCU_STATUS)) ==
		MCU_STATUS_HALTED);
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
}

/**
 * kbase_csf_firmware_trigger_mcu_halt - Send the Global request to firmware to
 *                                       halt its operation and bring itself
 *                                       into a known internal state for warm
 *                                       boot later.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_trigger_mcu_halt(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_enable_mcu - Send the command to enable MCU
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_enable_mcu(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_disable_mcu - Send the command to disable MCU
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_disable_mcu(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_disable_mcu_wait - Wait for the MCU to reach disabled
 *                                       status.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_disable_mcu_wait(struct kbase_device *kbdev);

#ifdef KBASE_PM_RUNTIME
/**
 * kbase_csf_firmware_trigger_mcu_sleep - Send the command to put MCU in sleep
 *                                        state.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_trigger_mcu_sleep(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_is_mcu_in_sleep - Check if sleep request has completed
 *                                      and MCU has halted.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: true if sleep request has completed, otherwise false.
 */
bool kbase_csf_firmware_is_mcu_in_sleep(struct kbase_device *kbdev);
#endif

/**
 * kbase_csf_firmware_trigger_reload() - Trigger the reboot of MCU firmware, for
 *                                       the cold boot case firmware image would
 *                                       be reloaded from filesystem into memory.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_trigger_reload(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_reload_completed - The reboot of MCU firmware has
 *                                       completed.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
void kbase_csf_firmware_reload_completed(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_global_reinit - Send the Global configuration requests
 *                                    after the reboot of MCU firmware.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @core_mask: Mask of the enabled shader cores.
 */
void kbase_csf_firmware_global_reinit(struct kbase_device *kbdev,
				      u64 core_mask);

/**
 * kbase_csf_firmware_global_reinit_complete - Check the Global configuration
 *                      requests, sent after the reboot of MCU firmware, have
 *                      completed or not.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: true if the Global configuration requests completed otherwise false.
 */
bool kbase_csf_firmware_global_reinit_complete(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_update_core_attr - Send the Global configuration request
 *                                       to update the requested core attribute
 *                                       changes.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @update_core_pwroff_timer: If true, signal the firmware needs to update
 *                            the MCU power-off timer value.
 * @update_core_mask:         If true, need to do the core_mask update with
 *                            the supplied core_mask value.
 * @core_mask:                New core mask value if update_core_mask is true,
 *                            otherwise unused.
 */
void kbase_csf_firmware_update_core_attr(struct kbase_device *kbdev,
		bool update_core_pwroff_timer, bool update_core_mask, u64 core_mask);

/**
 * kbase_csf_firmware_core_attr_updated - Check the Global configuration
 *                  request has completed or not, that was sent to update
 *                  the core attributes.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: true if the Global configuration request to update the core
 *         attributes has completed, otherwise false.
 */
bool kbase_csf_firmware_core_attr_updated(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_get_glb_iface - Request the global control block of CSF
 *                                      interface capabilities
 *
 * @kbdev:                 Kbase device.
 * @group_data:            Pointer where to store all the group data
 *                         (sequentially).
 * @max_group_num:         The maximum number of groups to be read.
 *                         Can be 0, in which case group_data is unused.
 * @stream_data:           Pointer where to store all the CS data
 *                         (sequentially).
 * @max_total_stream_num:  The maximum number of CSs to be read.
 *                         Can be 0, in which case stream_data is unused.
 * @glb_version:           Where to store the global interface version.
 * @features:              Where to store a bit mask of features (e.g.
 *                         whether certain types of job can be suspended).
 * @group_num:             Where to store the number of CSGs
 *                         supported.
 * @prfcnt_size:           Where to store the size of CSF performance counters,
 *                         in bytes. Bits 31:16 hold the size of firmware
 *                         performance counter data and 15:0 hold the size of
 *                         hardware performance counter data.
 * @instr_features:        Instrumentation features. Bits 7:4 hold the max size
 *                         of events. Bits 3:0 hold the offset update rate.
 *                         (csf >= 1,1,0)
 *
 * Return: Total number of CSs, summed across all groups.
 */
u32 kbase_csf_firmware_get_glb_iface(
	struct kbase_device *kbdev, struct basep_cs_group_control *group_data,
	u32 max_group_num, struct basep_cs_stream_control *stream_data,
	u32 max_total_stream_num, u32 *glb_version, u32 *features,
	u32 *group_num, u32 *prfcnt_size, u32 *instr_features);

/**
 * kbase_csf_firmware_get_timeline_metadata - Get CSF firmware header timeline
 *                                            metadata content
 *
 * @kbdev:        Kbase device.
 * @name:         Name of the metadata which metadata content to be returned.
 * @size:         Metadata size if specified metadata found.
 *
 * Return: The firmware timeline metadata content which match @p name.
 */
const char *kbase_csf_firmware_get_timeline_metadata(struct kbase_device *kbdev,
	const char *name, size_t *size);

/**
 * kbase_csf_firmware_mcu_shared_mapping_init - Allocate and map MCU shared memory.
 *
 * @kbdev:              Kbase device the memory mapping shall belong to.
 * @num_pages:          Number of memory pages to map.
 * @cpu_map_properties: Either PROT_READ or PROT_WRITE.
 * @gpu_map_properties: Either KBASE_REG_GPU_RD or KBASE_REG_GPU_WR.
 * @csf_mapping:        Object where to write metadata for the memory mapping.
 *
 * This helper function allocates memory and maps it on both the CPU
 * and the GPU address spaces. Most of the properties of the mapping
 * are implicit and will be automatically determined by the function,
 * e.g. whether memory is cacheable.
 *
 * The client is only expected to specify whether the mapping is readable
 * or writable in the CPU and the GPU address spaces; any other flag
 * will be ignored by the function.
 *
 * Return: 0 if success, or an error code on failure.
 */
int kbase_csf_firmware_mcu_shared_mapping_init(
		struct kbase_device *kbdev,
		unsigned int num_pages,
		unsigned long cpu_map_properties,
		unsigned long gpu_map_properties,
		struct kbase_csf_mapping *csf_mapping);

/**
 * kbase_csf_firmware_mcu_shared_mapping_term - Unmap and free MCU shared memory.
 *
 * @kbdev:       Device pointer.
 * @csf_mapping: Metadata of the memory mapping to terminate.
 */
void kbase_csf_firmware_mcu_shared_mapping_term(
		struct kbase_device *kbdev, struct kbase_csf_mapping *csf_mapping);

#ifdef CONFIG_MALI_BIFROST_DEBUG 
extern bool fw_debug;
#endif

static inline long kbase_csf_timeout_in_jiffies(const unsigned int msecs)
{
#ifdef CONFIG_MALI_BIFROST_DEBUG
	return (fw_debug ? MAX_SCHEDULE_TIMEOUT : msecs_to_jiffies(msecs));
#else
	return msecs_to_jiffies(msecs);
#endif
}

/**
 * kbase_csf_firmware_enable_gpu_idle_timer() - Activate the idle hysteresis
 *                                              monitoring operation
 *
 * @kbdev: Kbase device structure
 *
 * Program the firmware interface with its configured hysteresis count value
 * and enable the firmware to act on it. The Caller is
 * assumed to hold the kbdev->csf.scheduler.interrupt_lock.
 */
void kbase_csf_firmware_enable_gpu_idle_timer(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_disable_gpu_idle_timer() - Disable the idle time
 *                                             hysteresis monitoring operation
 *
 * @kbdev: Kbase device structure
 *
 * Program the firmware interface to disable the idle hysteresis timer. The
 * Caller is assumed to hold the kbdev->csf.scheduler.interrupt_lock.
 */
void kbase_csf_firmware_disable_gpu_idle_timer(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_get_gpu_idle_hysteresis_time - Get the firmware GPU idle
 *                                               detection hysteresis duration
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: the internally recorded hysteresis (nominal) value.
 */
u32 kbase_csf_firmware_get_gpu_idle_hysteresis_time(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_set_gpu_idle_hysteresis_time - Set the firmware GPU idle
 *                                               detection hysteresis duration
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @dur:     The duration value (unit: milliseconds) for the configuring
 *           hysteresis field for GPU idle detection
 *
 * The supplied value will be recorded internally without any change. But the
 * actual field value will be subject to hysteresis source frequency scaling
 * and maximum value limiting. The default source will be SYSTEM_TIMESTAMP
 * counter. But in case the platform is not able to supply it, the GPU
 * CYCLE_COUNTER source will be used as an alternative. Bit-31 on the
 * returned value is the source configuration flag, and it is set to '1'
 * when CYCLE_COUNTER alternative source is used.
 *
 * Return: the actual internally configured hysteresis field value.
 */
u32 kbase_csf_firmware_set_gpu_idle_hysteresis_time(struct kbase_device *kbdev, u32 dur);

/**
 * kbase_csf_firmware_get_mcu_core_pwroff_time - Get the MCU shader Core power-off
 *                                               time value
 *
 * @kbdev:   Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: the internally recorded MCU shader Core power-off (nominal) timeout value. The unit
 *         of the value is in micro-seconds.
 */
u32 kbase_csf_firmware_get_mcu_core_pwroff_time(struct kbase_device *kbdev);

/**
 * kbase_csf_firmware_set_mcu_core_pwroff_time - Set the MCU shader Core power-off
 *                                               time value
 *
 * @kbdev:   Instance of a GPU platform device that implements a CSF interface.
 * @dur:     The duration value (unit: micro-seconds) for configuring MCU
 *           core power-off timer, when the shader cores' power
 *           transitions are delegated to the MCU (normal operational
 *           mode)
 *
 * The supplied value will be recorded internally without any change. But the
 * actual field value will be subject to core power-off timer source frequency
 * scaling and maximum value limiting. The default source will be
 * SYSTEM_TIMESTAMP counter. But in case the platform is not able to supply it,
 * the GPU CYCLE_COUNTER source will be used as an alternative. Bit-31 on the
 * returned value is the source configuration flag, and it is set to '1'
 * when CYCLE_COUNTER alternative source is used.
 *
 * The configured MCU shader Core power-off timer will only have effect when the host
 * driver has delegated the shader cores' power management to MCU.
 *
 * Return: the actual internal core power-off timer value in register defined
 *         format.
 */
u32 kbase_csf_firmware_set_mcu_core_pwroff_time(struct kbase_device *kbdev, u32 dur);

/**
 * kbase_csf_interface_version - Helper function to build the full firmware
 *                               interface version in a format compatible with
 *                               GLB_VERSION register
 *
 * @major:     major version of csf interface
 * @minor:     minor version of csf interface
 * @patch:     patch version of csf interface
 *
 * Return: firmware interface version
 */
static inline u32 kbase_csf_interface_version(u32 major, u32 minor, u32 patch)
{
	return ((major << GLB_VERSION_MAJOR_SHIFT) |
		(minor << GLB_VERSION_MINOR_SHIFT) |
		(patch << GLB_VERSION_PATCH_SHIFT));
}

/**
 * kbase_csf_trigger_firmware_config_update - Send a firmware config update.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Any changes done to firmware configuration entry or tracebuffer entry
 * requires a GPU silent reset to reflect the configuration changes
 * requested, but if Firmware.header.entry.bit(30) is set then we can request a
 * FIRMWARE_CONFIG_UPDATE rather than doing a silent reset.
 *
 * Return: 0 if success, or negative error code on failure.
 */
int kbase_csf_trigger_firmware_config_update(struct kbase_device *kbdev);
#endif
