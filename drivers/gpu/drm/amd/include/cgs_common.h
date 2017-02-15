/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#ifndef _CGS_COMMON_H
#define _CGS_COMMON_H

#include "amd_shared.h"

struct cgs_device;

/**
 * enum cgs_gpu_mem_type - GPU memory types
 */
enum cgs_gpu_mem_type {
	CGS_GPU_MEM_TYPE__VISIBLE_FB,
	CGS_GPU_MEM_TYPE__INVISIBLE_FB,
	CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB,
	CGS_GPU_MEM_TYPE__INVISIBLE_CONTIG_FB,
	CGS_GPU_MEM_TYPE__GART_CACHEABLE,
	CGS_GPU_MEM_TYPE__GART_WRITECOMBINE
};

/**
 * enum cgs_ind_reg - Indirect register spaces
 */
enum cgs_ind_reg {
	CGS_IND_REG__MMIO,
	CGS_IND_REG__PCIE,
	CGS_IND_REG__SMC,
	CGS_IND_REG__UVD_CTX,
	CGS_IND_REG__DIDT,
	CGS_IND_REG_GC_CAC,
	CGS_IND_REG__AUDIO_ENDPT
};

/**
 * enum cgs_clock - Clocks controlled by the SMU
 */
enum cgs_clock {
	CGS_CLOCK__SCLK,
	CGS_CLOCK__MCLK,
	CGS_CLOCK__VCLK,
	CGS_CLOCK__DCLK,
	CGS_CLOCK__ECLK,
	CGS_CLOCK__ACLK,
	CGS_CLOCK__ICLK,
	/* ... */
};

/**
 * enum cgs_engine - Engines that can be statically power-gated
 */
enum cgs_engine {
	CGS_ENGINE__UVD,
	CGS_ENGINE__VCE,
	CGS_ENGINE__VP8,
	CGS_ENGINE__ACP_DMA,
	CGS_ENGINE__ACP_DSP0,
	CGS_ENGINE__ACP_DSP1,
	CGS_ENGINE__ISP,
	/* ... */
};

/**
 * enum cgs_voltage_planes - Voltage planes for external camera HW
 */
enum cgs_voltage_planes {
	CGS_VOLTAGE_PLANE__SENSOR0,
	CGS_VOLTAGE_PLANE__SENSOR1,
	/* ... */
};

/*
 * enum cgs_ucode_id - Firmware types for different IPs
 */
enum cgs_ucode_id {
	CGS_UCODE_ID_SMU = 0,
	CGS_UCODE_ID_SMU_SK,
	CGS_UCODE_ID_SDMA0,
	CGS_UCODE_ID_SDMA1,
	CGS_UCODE_ID_CP_CE,
	CGS_UCODE_ID_CP_PFP,
	CGS_UCODE_ID_CP_ME,
	CGS_UCODE_ID_CP_MEC,
	CGS_UCODE_ID_CP_MEC_JT1,
	CGS_UCODE_ID_CP_MEC_JT2,
	CGS_UCODE_ID_GMCON_RENG,
	CGS_UCODE_ID_RLC_G,
	CGS_UCODE_ID_STORAGE,
	CGS_UCODE_ID_MAXIMUM,
};

enum cgs_system_info_id {
	CGS_SYSTEM_INFO_ADAPTER_BDF_ID = 1,
	CGS_SYSTEM_INFO_PCIE_GEN_INFO,
	CGS_SYSTEM_INFO_PCIE_MLW,
	CGS_SYSTEM_INFO_PCIE_DEV,
	CGS_SYSTEM_INFO_PCIE_REV,
	CGS_SYSTEM_INFO_CG_FLAGS,
	CGS_SYSTEM_INFO_PG_FLAGS,
	CGS_SYSTEM_INFO_GFX_CU_INFO,
	CGS_SYSTEM_INFO_GFX_SE_INFO,
	CGS_SYSTEM_INFO_PCIE_SUB_SYS_ID,
	CGS_SYSTEM_INFO_PCIE_SUB_SYS_VENDOR_ID,
	CGS_SYSTEM_INFO_ID_MAXIMUM,
};

struct cgs_system_info {
	uint64_t			size;
	enum cgs_system_info_id		info_id;
	union {
		void			*ptr;
		uint64_t		value;
	};
	uint64_t			padding[13];
};

/*
 * enum cgs_resource_type - GPU resource type
 */
enum cgs_resource_type {
	CGS_RESOURCE_TYPE_MMIO = 0,
	CGS_RESOURCE_TYPE_FB,
	CGS_RESOURCE_TYPE_IO,
	CGS_RESOURCE_TYPE_DOORBELL,
	CGS_RESOURCE_TYPE_ROM,
};

/**
 * struct cgs_clock_limits - Clock limits
 *
 * Clocks are specified in 10KHz units.
 */
struct cgs_clock_limits {
	unsigned min;		/**< Minimum supported frequency */
	unsigned max;		/**< Maxumim supported frequency */
	unsigned sustainable;	/**< Thermally sustainable frequency */
};

/**
 * struct cgs_firmware_info - Firmware information
 */
struct cgs_firmware_info {
	uint16_t		version;
	uint16_t		fw_version;
	uint16_t		feature_version;
	uint32_t		image_size;
	uint64_t		mc_addr;

	/* only for smc firmware */
	uint32_t		ucode_start_address;

	void			*kptr;
};

struct cgs_mode_info {
	uint32_t		refresh_rate;
	uint32_t		ref_clock;
	uint32_t		vblank_time_us;
};

struct cgs_display_info {
	uint32_t		display_count;
	uint32_t		active_display_mask;
	struct cgs_mode_info *mode_info;
};

typedef unsigned long cgs_handle_t;

#define CGS_ACPI_METHOD_ATCS          0x53435441
#define CGS_ACPI_METHOD_ATIF          0x46495441
#define CGS_ACPI_METHOD_ATPX          0x58505441
#define CGS_ACPI_FIELD_METHOD_NAME                      0x00000001
#define CGS_ACPI_FIELD_INPUT_ARGUMENT_COUNT             0x00000002
#define CGS_ACPI_MAX_BUFFER_SIZE     256
#define CGS_ACPI_TYPE_ANY                      0x00
#define CGS_ACPI_TYPE_INTEGER               0x01
#define CGS_ACPI_TYPE_STRING                0x02
#define CGS_ACPI_TYPE_BUFFER                0x03
#define CGS_ACPI_TYPE_PACKAGE               0x04

struct cgs_acpi_method_argument {
	uint32_t type;
	uint32_t data_length;
	union{
		uint32_t value;
		void *pointer;
	};
};

struct cgs_acpi_method_info {
	uint32_t size;
	uint32_t field;
	uint32_t input_count;
	uint32_t name;
	struct cgs_acpi_method_argument *pinput_argument;
	uint32_t output_count;
	struct cgs_acpi_method_argument *poutput_argument;
	uint32_t padding[9];
};

/**
 * cgs_gpu_mem_info() - Return information about memory heaps
 * @cgs_device: opaque device handle
 * @type:	memory type
 * @mc_start:	Start MC address of the heap (output)
 * @mc_size:	MC address space size (output)
 * @mem_size:	maximum amount of memory available for allocation (output)
 *
 * This function returns information about memory heaps. The type
 * parameter is used to select the memory heap. The mc_start and
 * mc_size for GART heaps may be bigger than the memory available for
 * allocation.
 *
 * mc_start and mc_size are undefined for non-contiguous FB memory
 * types, since buffers allocated with these types may or may not be
 * GART mapped.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_gpu_mem_info_t)(struct cgs_device *cgs_device, enum cgs_gpu_mem_type type,
				  uint64_t *mc_start, uint64_t *mc_size,
				  uint64_t *mem_size);

/**
 * cgs_gmap_kmem() - map kernel memory to GART aperture
 * @cgs_device:	opaque device handle
 * @kmem:	pointer to kernel memory
 * @size:	size to map
 * @min_offset: minimum offset from start of GART aperture
 * @max_offset: maximum offset from start of GART aperture
 * @kmem_handle: kernel memory handle (output)
 * @mcaddr:	MC address (output)
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_gmap_kmem_t)(struct cgs_device *cgs_device, void *kmem, uint64_t size,
			       uint64_t min_offset, uint64_t max_offset,
			       cgs_handle_t *kmem_handle, uint64_t *mcaddr);

/**
 * cgs_gunmap_kmem() - unmap kernel memory
 * @cgs_device:	opaque device handle
 * @kmem_handle: kernel memory handle returned by gmap_kmem
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_gunmap_kmem_t)(struct cgs_device *cgs_device, cgs_handle_t kmem_handle);

/**
 * cgs_alloc_gpu_mem() - Allocate GPU memory
 * @cgs_device:	opaque device handle
 * @type:	memory type
 * @size:	size in bytes
 * @align:	alignment in bytes
 * @min_offset: minimum offset from start of heap
 * @max_offset: maximum offset from start of heap
 * @handle:	memory handle (output)
 *
 * The memory types CGS_GPU_MEM_TYPE_*_CONTIG_FB force contiguous
 * memory allocation. This guarantees that the MC address returned by
 * cgs_gmap_gpu_mem is not mapped through the GART. The non-contiguous
 * FB memory types may be GART mapped depending on memory
 * fragmentation and memory allocator policies.
 *
 * If min/max_offset are non-0, the allocation will be forced to
 * reside between these offsets in its respective memory heap. The
 * base address that the offset relates to, depends on the memory
 * type.
 *
 * - CGS_GPU_MEM_TYPE__*_CONTIG_FB: FB MC base address
 * - CGS_GPU_MEM_TYPE__GART_*:	    GART aperture base address
 * - others:			    undefined, don't use with max_offset
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_alloc_gpu_mem_t)(struct cgs_device *cgs_device, enum cgs_gpu_mem_type type,
				   uint64_t size, uint64_t align,
				   uint64_t min_offset, uint64_t max_offset,
				   cgs_handle_t *handle);

/**
 * cgs_free_gpu_mem() - Free GPU memory
 * @cgs_device:	opaque device handle
 * @handle:	memory handle returned by alloc or import
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_free_gpu_mem_t)(struct cgs_device *cgs_device, cgs_handle_t handle);

/**
 * cgs_gmap_gpu_mem() - GPU-map GPU memory
 * @cgs_device:	opaque device handle
 * @handle:	memory handle returned by alloc or import
 * @mcaddr:	MC address (output)
 *
 * Ensures that a buffer is GPU accessible and returns its MC address.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_gmap_gpu_mem_t)(struct cgs_device *cgs_device, cgs_handle_t handle,
				  uint64_t *mcaddr);

/**
 * cgs_gunmap_gpu_mem() - GPU-unmap GPU memory
 * @cgs_device:	opaque device handle
 * @handle:	memory handle returned by alloc or import
 *
 * Allows the buffer to be migrated while it's not used by the GPU.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_gunmap_gpu_mem_t)(struct cgs_device *cgs_device, cgs_handle_t handle);

/**
 * cgs_kmap_gpu_mem() - Kernel-map GPU memory
 *
 * @cgs_device:	opaque device handle
 * @handle:	memory handle returned by alloc or import
 * @map:	Kernel virtual address the memory was mapped to (output)
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_kmap_gpu_mem_t)(struct cgs_device *cgs_device, cgs_handle_t handle,
				  void **map);

/**
 * cgs_kunmap_gpu_mem() - Kernel-unmap GPU memory
 * @cgs_device:	opaque device handle
 * @handle:	memory handle returned by alloc or import
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_kunmap_gpu_mem_t)(struct cgs_device *cgs_device, cgs_handle_t handle);

/**
 * cgs_read_register() - Read an MMIO register
 * @cgs_device:	opaque device handle
 * @offset:	register offset
 *
 * Return:  register value
 */
typedef uint32_t (*cgs_read_register_t)(struct cgs_device *cgs_device, unsigned offset);

/**
 * cgs_write_register() - Write an MMIO register
 * @cgs_device:	opaque device handle
 * @offset:	register offset
 * @value:	register value
 */
typedef void (*cgs_write_register_t)(struct cgs_device *cgs_device, unsigned offset,
				     uint32_t value);

/**
 * cgs_read_ind_register() - Read an indirect register
 * @cgs_device:	opaque device handle
 * @offset:	register offset
 *
 * Return:  register value
 */
typedef uint32_t (*cgs_read_ind_register_t)(struct cgs_device *cgs_device, enum cgs_ind_reg space,
					    unsigned index);

/**
 * cgs_write_ind_register() - Write an indirect register
 * @cgs_device:	opaque device handle
 * @offset:	register offset
 * @value:	register value
 */
typedef void (*cgs_write_ind_register_t)(struct cgs_device *cgs_device, enum cgs_ind_reg space,
					 unsigned index, uint32_t value);

/**
 * cgs_read_pci_config_byte() - Read byte from PCI configuration space
 * @cgs_device:	opaque device handle
 * @addr:	address
 *
 * Return:  Value read
 */
typedef uint8_t (*cgs_read_pci_config_byte_t)(struct cgs_device *cgs_device, unsigned addr);

/**
 * cgs_read_pci_config_word() - Read word from PCI configuration space
 * @cgs_device:	opaque device handle
 * @addr:	address, must be word-aligned
 *
 * Return:  Value read
 */
typedef uint16_t (*cgs_read_pci_config_word_t)(struct cgs_device *cgs_device, unsigned addr);

/**
 * cgs_read_pci_config_dword() - Read dword from PCI configuration space
 * @cgs_device:	opaque device handle
 * @addr:	address, must be dword-aligned
 *
 * Return:  Value read
 */
typedef uint32_t (*cgs_read_pci_config_dword_t)(struct cgs_device *cgs_device,
						unsigned addr);

/**
 * cgs_write_pci_config_byte() - Write byte to PCI configuration space
 * @cgs_device:	opaque device handle
 * @addr:	address
 * @value:	value to write
 */
typedef void (*cgs_write_pci_config_byte_t)(struct cgs_device *cgs_device, unsigned addr,
					    uint8_t value);

/**
 * cgs_write_pci_config_word() - Write byte to PCI configuration space
 * @cgs_device:	opaque device handle
 * @addr:	address, must be word-aligned
 * @value:	value to write
 */
typedef void (*cgs_write_pci_config_word_t)(struct cgs_device *cgs_device, unsigned addr,
					    uint16_t value);

/**
 * cgs_write_pci_config_dword() - Write byte to PCI configuration space
 * @cgs_device:	opaque device handle
 * @addr:	address, must be dword-aligned
 * @value:	value to write
 */
typedef void (*cgs_write_pci_config_dword_t)(struct cgs_device *cgs_device, unsigned addr,
					     uint32_t value);


/**
 * cgs_get_pci_resource() - provide access to a device resource (PCI BAR)
 * @cgs_device:	opaque device handle
 * @resource_type:	Type of Resource (MMIO, IO, ROM, FB, DOORBELL)
 * @size:	size of the region
 * @offset:	offset from the start of the region
 * @resource_base:	base address (not including offset) returned
 *
 * Return: 0 on success, -errno otherwise
 */
typedef int (*cgs_get_pci_resource_t)(struct cgs_device *cgs_device,
				      enum cgs_resource_type resource_type,
				      uint64_t size,
				      uint64_t offset,
				      uint64_t *resource_base);

/**
 * cgs_atom_get_data_table() - Get a pointer to an ATOM BIOS data table
 * @cgs_device:	opaque device handle
 * @table:	data table index
 * @size:	size of the table (output, may be NULL)
 * @frev:	table format revision (output, may be NULL)
 * @crev:	table content revision (output, may be NULL)
 *
 * Return: Pointer to start of the table, or NULL on failure
 */
typedef const void *(*cgs_atom_get_data_table_t)(
	struct cgs_device *cgs_device, unsigned table,
	uint16_t *size, uint8_t *frev, uint8_t *crev);

/**
 * cgs_atom_get_cmd_table_revs() - Get ATOM BIOS command table revisions
 * @cgs_device:	opaque device handle
 * @table:	data table index
 * @frev:	table format revision (output, may be NULL)
 * @crev:	table content revision (output, may be NULL)
 *
 * Return: 0 on success, -errno otherwise
 */
typedef int (*cgs_atom_get_cmd_table_revs_t)(struct cgs_device *cgs_device, unsigned table,
					     uint8_t *frev, uint8_t *crev);

/**
 * cgs_atom_exec_cmd_table() - Execute an ATOM BIOS command table
 * @cgs_device: opaque device handle
 * @table:	command table index
 * @args:	arguments
 *
 * Return: 0 on success, -errno otherwise
 */
typedef int (*cgs_atom_exec_cmd_table_t)(struct cgs_device *cgs_device,
					 unsigned table, void *args);

/**
 * cgs_create_pm_request() - Create a power management request
 * @cgs_device:	opaque device handle
 * @request:	handle of created PM request (output)
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_create_pm_request_t)(struct cgs_device *cgs_device, cgs_handle_t *request);

/**
 * cgs_destroy_pm_request() - Destroy a power management request
 * @cgs_device:	opaque device handle
 * @request:	handle of created PM request
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_destroy_pm_request_t)(struct cgs_device *cgs_device, cgs_handle_t request);

/**
 * cgs_set_pm_request() - Activate or deactiveate a PM request
 * @cgs_device:	opaque device handle
 * @request:	PM request handle
 * @active:	0 = deactivate, non-0 = activate
 *
 * While a PM request is active, its minimum clock requests are taken
 * into account as the requested engines are powered up. When the
 * request is inactive, the engines may be powered down and clocks may
 * be lower, depending on other PM requests by other driver
 * components.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_set_pm_request_t)(struct cgs_device *cgs_device, cgs_handle_t request,
				    int active);

/**
 * cgs_pm_request_clock() - Request a minimum frequency for a specific clock
 * @cgs_device:	opaque device handle
 * @request:	PM request handle
 * @clock:	which clock?
 * @freq:	requested min. frequency in 10KHz units (0 to clear request)
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_pm_request_clock_t)(struct cgs_device *cgs_device, cgs_handle_t request,
				      enum cgs_clock clock, unsigned freq);

/**
 * cgs_pm_request_engine() - Request an engine to be powered up
 * @cgs_device:	opaque device handle
 * @request:	PM request handle
 * @engine:	which engine?
 * @powered:	0 = powered down, non-0 = powered up
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_pm_request_engine_t)(struct cgs_device *cgs_device, cgs_handle_t request,
				       enum cgs_engine engine, int powered);

/**
 * cgs_pm_query_clock_limits() - Query clock frequency limits
 * @cgs_device:	opaque device handle
 * @clock:	which clock?
 * @limits:	clock limits
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_pm_query_clock_limits_t)(struct cgs_device *cgs_device,
					   enum cgs_clock clock,
					   struct cgs_clock_limits *limits);

/**
 * cgs_set_camera_voltages() - Apply specific voltages to PMIC voltage planes
 * @cgs_device:	opaque device handle
 * @mask:	bitmask of voltages to change (1<<CGS_VOLTAGE_PLANE__xyz|...)
 * @voltages:	pointer to array of voltage values in 1mV units
 *
 * Return: 0 on success, -errno otherwise
 */
typedef int (*cgs_set_camera_voltages_t)(struct cgs_device *cgs_device, uint32_t mask,
					 const uint32_t *voltages);
/**
 * cgs_get_firmware_info - Get the firmware information from core driver
 * @cgs_device: opaque device handle
 * @type: the firmware type
 * @info: returend firmware information
 *
 * Return: 0 on success, -errno otherwise
 */
typedef int (*cgs_get_firmware_info)(struct cgs_device *cgs_device,
				     enum cgs_ucode_id type,
				     struct cgs_firmware_info *info);

typedef int (*cgs_rel_firmware)(struct cgs_device *cgs_device,
					 enum cgs_ucode_id type);

typedef int(*cgs_set_powergating_state)(struct cgs_device *cgs_device,
				  enum amd_ip_block_type block_type,
				  enum amd_powergating_state state);

typedef int(*cgs_set_clockgating_state)(struct cgs_device *cgs_device,
				  enum amd_ip_block_type block_type,
				  enum amd_clockgating_state state);

typedef int(*cgs_get_active_displays_info)(
					struct cgs_device *cgs_device,
					struct cgs_display_info *info);

typedef int (*cgs_notify_dpm_enabled)(struct cgs_device *cgs_device, bool enabled);

typedef int (*cgs_call_acpi_method)(struct cgs_device *cgs_device,
					uint32_t acpi_method,
					uint32_t acpi_function,
					void *pinput, void *poutput,
					uint32_t output_count,
					uint32_t input_size,
					uint32_t output_size);

typedef int (*cgs_query_system_info)(struct cgs_device *cgs_device,
				struct cgs_system_info *sys_info);

typedef int (*cgs_is_virtualization_enabled_t)(void *cgs_device);

typedef int (*cgs_enter_safe_mode)(struct cgs_device *cgs_device, bool en);

struct cgs_ops {
	/* memory management calls (similar to KFD interface) */
	cgs_gpu_mem_info_t gpu_mem_info;
	cgs_gmap_kmem_t gmap_kmem;
	cgs_gunmap_kmem_t gunmap_kmem;
	cgs_alloc_gpu_mem_t alloc_gpu_mem;
	cgs_free_gpu_mem_t free_gpu_mem;
	cgs_gmap_gpu_mem_t gmap_gpu_mem;
	cgs_gunmap_gpu_mem_t gunmap_gpu_mem;
	cgs_kmap_gpu_mem_t kmap_gpu_mem;
	cgs_kunmap_gpu_mem_t kunmap_gpu_mem;
	/* MMIO access */
	cgs_read_register_t read_register;
	cgs_write_register_t write_register;
	cgs_read_ind_register_t read_ind_register;
	cgs_write_ind_register_t write_ind_register;
	/* PCI configuration space access */
	cgs_read_pci_config_byte_t read_pci_config_byte;
	cgs_read_pci_config_word_t read_pci_config_word;
	cgs_read_pci_config_dword_t read_pci_config_dword;
	cgs_write_pci_config_byte_t write_pci_config_byte;
	cgs_write_pci_config_word_t write_pci_config_word;
	cgs_write_pci_config_dword_t write_pci_config_dword;
	/* PCI resources */
	cgs_get_pci_resource_t get_pci_resource;
	/* ATOM BIOS */
	cgs_atom_get_data_table_t atom_get_data_table;
	cgs_atom_get_cmd_table_revs_t atom_get_cmd_table_revs;
	cgs_atom_exec_cmd_table_t atom_exec_cmd_table;
	/* Power management */
	cgs_create_pm_request_t create_pm_request;
	cgs_destroy_pm_request_t destroy_pm_request;
	cgs_set_pm_request_t set_pm_request;
	cgs_pm_request_clock_t pm_request_clock;
	cgs_pm_request_engine_t pm_request_engine;
	cgs_pm_query_clock_limits_t pm_query_clock_limits;
	cgs_set_camera_voltages_t set_camera_voltages;
	/* Firmware Info */
	cgs_get_firmware_info get_firmware_info;
	cgs_rel_firmware rel_firmware;
	/* cg pg interface*/
	cgs_set_powergating_state set_powergating_state;
	cgs_set_clockgating_state set_clockgating_state;
	/* display manager */
	cgs_get_active_displays_info get_active_displays_info;
	/* notify dpm enabled */
	cgs_notify_dpm_enabled notify_dpm_enabled;
	/* ACPI */
	cgs_call_acpi_method call_acpi_method;
	/* get system info */
	cgs_query_system_info query_system_info;
	cgs_is_virtualization_enabled_t is_virtualization_enabled;
	cgs_enter_safe_mode enter_safe_mode;
};

struct cgs_os_ops; /* To be define in OS-specific CGS header */

struct cgs_device
{
	const struct cgs_ops *ops;
	const struct cgs_os_ops *os_ops;
	/* to be embedded at the start of driver private structure */
};

/* Convenience macros that make CGS indirect function calls look like
 * normal function calls */
#define CGS_CALL(func,dev,...) \
	(((struct cgs_device *)dev)->ops->func(dev, ##__VA_ARGS__))
#define CGS_OS_CALL(func,dev,...) \
	(((struct cgs_device *)dev)->os_ops->func(dev, ##__VA_ARGS__))

#define cgs_gpu_mem_info(dev,type,mc_start,mc_size,mem_size)		\
	CGS_CALL(gpu_mem_info,dev,type,mc_start,mc_size,mem_size)
#define cgs_gmap_kmem(dev,kmem,size,min_off,max_off,kmem_handle,mcaddr)	\
	CGS_CALL(gmap_kmem,dev,kmem,size,min_off,max_off,kmem_handle,mcaddr)
#define cgs_gunmap_kmem(dev,kmem_handle)	\
	CGS_CALL(gunmap_kmem,dev,keme_handle)
#define cgs_alloc_gpu_mem(dev,type,size,align,min_off,max_off,handle)	\
	CGS_CALL(alloc_gpu_mem,dev,type,size,align,min_off,max_off,handle)
#define cgs_free_gpu_mem(dev,handle)		\
	CGS_CALL(free_gpu_mem,dev,handle)
#define cgs_gmap_gpu_mem(dev,handle,mcaddr)	\
	CGS_CALL(gmap_gpu_mem,dev,handle,mcaddr)
#define cgs_gunmap_gpu_mem(dev,handle)		\
	CGS_CALL(gunmap_gpu_mem,dev,handle)
#define cgs_kmap_gpu_mem(dev,handle,map)	\
	CGS_CALL(kmap_gpu_mem,dev,handle,map)
#define cgs_kunmap_gpu_mem(dev,handle)		\
	CGS_CALL(kunmap_gpu_mem,dev,handle)

#define cgs_read_register(dev,offset)		\
	CGS_CALL(read_register,dev,offset)
#define cgs_write_register(dev,offset,value)		\
	CGS_CALL(write_register,dev,offset,value)
#define cgs_read_ind_register(dev,space,index)		\
	CGS_CALL(read_ind_register,dev,space,index)
#define cgs_write_ind_register(dev,space,index,value)		\
	CGS_CALL(write_ind_register,dev,space,index,value)

#define cgs_read_pci_config_byte(dev,addr)	\
	CGS_CALL(read_pci_config_byte,dev,addr)
#define cgs_read_pci_config_word(dev,addr)	\
	CGS_CALL(read_pci_config_word,dev,addr)
#define cgs_read_pci_config_dword(dev,addr)		\
	CGS_CALL(read_pci_config_dword,dev,addr)
#define cgs_write_pci_config_byte(dev,addr,value)	\
	CGS_CALL(write_pci_config_byte,dev,addr,value)
#define cgs_write_pci_config_word(dev,addr,value)	\
	CGS_CALL(write_pci_config_word,dev,addr,value)
#define cgs_write_pci_config_dword(dev,addr,value)	\
	CGS_CALL(write_pci_config_dword,dev,addr,value)

#define cgs_atom_get_data_table(dev,table,size,frev,crev)	\
	CGS_CALL(atom_get_data_table,dev,table,size,frev,crev)
#define cgs_atom_get_cmd_table_revs(dev,table,frev,crev)	\
	CGS_CALL(atom_get_cmd_table_revs,dev,table,frev,crev)
#define cgs_atom_exec_cmd_table(dev,table,args)		\
	CGS_CALL(atom_exec_cmd_table,dev,table,args)

#define cgs_create_pm_request(dev,request)	\
	CGS_CALL(create_pm_request,dev,request)
#define cgs_destroy_pm_request(dev,request)		\
	CGS_CALL(destroy_pm_request,dev,request)
#define cgs_set_pm_request(dev,request,active)		\
	CGS_CALL(set_pm_request,dev,request,active)
#define cgs_pm_request_clock(dev,request,clock,freq)		\
	CGS_CALL(pm_request_clock,dev,request,clock,freq)
#define cgs_pm_request_engine(dev,request,engine,powered)	\
	CGS_CALL(pm_request_engine,dev,request,engine,powered)
#define cgs_pm_query_clock_limits(dev,clock,limits)		\
	CGS_CALL(pm_query_clock_limits,dev,clock,limits)
#define cgs_set_camera_voltages(dev,mask,voltages)	\
	CGS_CALL(set_camera_voltages,dev,mask,voltages)
#define cgs_get_firmware_info(dev, type, info)	\
	CGS_CALL(get_firmware_info, dev, type, info)
#define cgs_rel_firmware(dev, type)	\
	CGS_CALL(rel_firmware, dev, type)
#define cgs_set_powergating_state(dev, block_type, state)	\
	CGS_CALL(set_powergating_state, dev, block_type, state)
#define cgs_set_clockgating_state(dev, block_type, state)	\
	CGS_CALL(set_clockgating_state, dev, block_type, state)
#define cgs_notify_dpm_enabled(dev, enabled)	\
	CGS_CALL(notify_dpm_enabled, dev, enabled)

#define cgs_get_active_displays_info(dev, info)	\
	CGS_CALL(get_active_displays_info, dev, info)

#define cgs_call_acpi_method(dev, acpi_method, acpi_function, pintput, poutput, output_count, input_size, output_size)	\
	CGS_CALL(call_acpi_method, dev, acpi_method, acpi_function, pintput, poutput, output_count, input_size, output_size)
#define cgs_query_system_info(dev, sys_info)	\
	CGS_CALL(query_system_info, dev, sys_info)
#define cgs_get_pci_resource(cgs_device, resource_type, size, offset, \
	resource_base) \
	CGS_CALL(get_pci_resource, cgs_device, resource_type, size, offset, \
	resource_base)

#define cgs_is_virtualization_enabled(cgs_device) \
		CGS_CALL(is_virtualization_enabled, cgs_device)

#define cgs_enter_safe_mode(cgs_device, en) \
		CGS_CALL(enter_safe_mode, cgs_device, en)

#endif /* _CGS_COMMON_H */
