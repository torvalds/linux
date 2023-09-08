/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef VPU_BOOT_API_H
#define VPU_BOOT_API_H

/*
 * =========== FW API version information beginning ================
 *  The bellow values will be used to construct the version info this way:
 *  fw_bin_header->api_version[VPU_BOOT_API_VER_ID] = (VPU_BOOT_API_VER_MAJOR << 16) |
 *  VPU_BOOT_API_VER_MINOR;
 *  VPU_BOOT_API_VER_PATCH will be ignored. KMD and compatibility is not affected if this changes.
 */

/*
 * Major version changes that break backward compatibility.
 * Major version must start from 1 and can only be incremented.
 */
#define VPU_BOOT_API_VER_MAJOR 3

/*
 * Minor version changes when API backward compatibility is preserved.
 * Resets to 0 if Major version is incremented.
 */
#define VPU_BOOT_API_VER_MINOR 12

/*
 * API header changed (field names, documentation, formatting) but API itself has not been changed
 */
#define VPU_BOOT_API_VER_PATCH 2

/*
 * Index in the API version table
 * Must be unique for each API
 */
#define VPU_BOOT_API_VER_INDEX 0
/* ------------ FW API version information end ---------------------*/

#pragma pack(push, 1)

/*
 * Firmware image header format
 */
#define VPU_FW_HEADER_SIZE    4096
#define VPU_FW_HEADER_VERSION 0x1
#define VPU_FW_VERSION_SIZE   32
#define VPU_FW_API_VER_NUM    16

struct vpu_firmware_header {
	u32 header_version;
	u32 image_format;
	u64 image_load_address;
	u32 image_size;
	u64 entry_point;
	u8 vpu_version[VPU_FW_VERSION_SIZE];
	u32 compression_type;
	u64 firmware_version_load_address;
	u32 firmware_version_size;
	u64 boot_params_load_address;
	u32 api_version[VPU_FW_API_VER_NUM];
	/* Size of memory require for firmware execution */
	u32 runtime_size;
	u32 shave_nn_fw_size;
};

/*
 * Firmware boot parameters format
 */

#define VPU_BOOT_PLL_COUNT     3
#define VPU_BOOT_PLL_OUT_COUNT 4

/** Values for boot_type field */
#define VPU_BOOT_TYPE_COLDBOOT 0
#define VPU_BOOT_TYPE_WARMBOOT 1

/** Value for magic filed */
#define VPU_BOOT_PARAMS_MAGIC 0x10000

/** VPU scheduling mode. By default, OS scheduling is used. */
#define VPU_SCHEDULING_MODE_OS 0
#define VPU_SCHEDULING_MODE_HW 1

enum VPU_BOOT_L2_CACHE_CFG_TYPE {
	VPU_BOOT_L2_CACHE_CFG_UPA = 0,
	VPU_BOOT_L2_CACHE_CFG_NN = 1,
	VPU_BOOT_L2_CACHE_CFG_NUM = 2
};

/**
 * Logging destinations.
 *
 * Logging output can be directed to different logging destinations. This enum
 * defines the list of logging destinations supported by the VPU firmware (NOTE:
 * a specific VPU FW binary may support only a subset of such output
 * destinations, depending on the target platform and compile options).
 */
enum vpu_trace_destination {
	VPU_TRACE_DESTINATION_PIPEPRINT = 0x1,
	VPU_TRACE_DESTINATION_VERBOSE_TRACING = 0x2,
	VPU_TRACE_DESTINATION_NORTH_PEAK = 0x4,
};

/*
 * Processor bit shifts (for loggable HW components).
 */
#define VPU_TRACE_PROC_BIT_ARM	     0
#define VPU_TRACE_PROC_BIT_LRT	     1
#define VPU_TRACE_PROC_BIT_LNN	     2
#define VPU_TRACE_PROC_BIT_SHV_0     3
#define VPU_TRACE_PROC_BIT_SHV_1     4
#define VPU_TRACE_PROC_BIT_SHV_2     5
#define VPU_TRACE_PROC_BIT_SHV_3     6
#define VPU_TRACE_PROC_BIT_SHV_4     7
#define VPU_TRACE_PROC_BIT_SHV_5     8
#define VPU_TRACE_PROC_BIT_SHV_6     9
#define VPU_TRACE_PROC_BIT_SHV_7     10
#define VPU_TRACE_PROC_BIT_SHV_8     11
#define VPU_TRACE_PROC_BIT_SHV_9     12
#define VPU_TRACE_PROC_BIT_SHV_10    13
#define VPU_TRACE_PROC_BIT_SHV_11    14
#define VPU_TRACE_PROC_BIT_SHV_12    15
#define VPU_TRACE_PROC_BIT_SHV_13    16
#define VPU_TRACE_PROC_BIT_SHV_14    17
#define VPU_TRACE_PROC_BIT_SHV_15    18
#define VPU_TRACE_PROC_BIT_ACT_SHV_0 19
#define VPU_TRACE_PROC_BIT_ACT_SHV_1 20
#define VPU_TRACE_PROC_BIT_ACT_SHV_2 21
#define VPU_TRACE_PROC_BIT_ACT_SHV_3 22
#define VPU_TRACE_PROC_NO_OF_HW_DEVS 23

/* KMB HW component IDs are sequential, so define first and last IDs. */
#define VPU_TRACE_PROC_BIT_KMB_FIRST VPU_TRACE_PROC_BIT_LRT
#define VPU_TRACE_PROC_BIT_KMB_LAST  VPU_TRACE_PROC_BIT_SHV_15

struct vpu_boot_l2_cache_config {
	u8 use;
	u8 cfg;
};

struct vpu_warm_boot_section {
	u32 src;
	u32 dst;
	u32 size;
	u32 core_id;
	u32 is_clear_op;
};

struct vpu_boot_params {
	u32 magic;
	u32 vpu_id;
	u32 vpu_count;
	u32 pad0[5];
	/* Clock frequencies: 0x20 - 0xFF */
	u32 frequency;
	u32 pll[VPU_BOOT_PLL_COUNT][VPU_BOOT_PLL_OUT_COUNT];
	u32 perf_clk_frequency;
	u32 pad1[42];
	/* Memory regions: 0x100 - 0x1FF */
	u64 ipc_header_area_start;
	u32 ipc_header_area_size;
	u64 shared_region_base;
	u32 shared_region_size;
	u64 ipc_payload_area_start;
	u32 ipc_payload_area_size;
	u64 global_aliased_pio_base;
	u32 global_aliased_pio_size;
	u32 autoconfig;
	struct vpu_boot_l2_cache_config cache_defaults[VPU_BOOT_L2_CACHE_CFG_NUM];
	u64 global_memory_allocator_base;
	u32 global_memory_allocator_size;
	/**
	 * ShaveNN FW section VPU base address
	 * On VPU2.7 HW this address must be within 2GB range starting from L2C_PAGE_TABLE base
	 */
	u64 shave_nn_fw_base;
	u64 save_restore_ret_address; /* stores the address of FW's restore entry point */
	u32 pad2[43];
	/* IRQ re-direct numbers: 0x200 - 0x2FF */
	s32 watchdog_irq_mss;
	s32 watchdog_irq_nce;
	/* ARM -> VPU doorbell interrupt. ARM is notifying VPU of async command or compute job. */
	u32 host_to_vpu_irq;
	/* VPU -> ARM job done interrupt. VPU is notifying ARM of compute job completion. */
	u32 job_done_irq;
	/* VPU -> ARM IRQ line to use to request MMU update. */
	u32 mmu_update_request_irq;
	/* ARM -> VPU IRQ line to use to notify of MMU update completion. */
	u32 mmu_update_done_irq;
	/* ARM -> VPU IRQ line to use to request power level change. */
	u32 set_power_level_irq;
	/* VPU -> ARM IRQ line to use to notify of power level change completion. */
	u32 set_power_level_done_irq;
	/* VPU -> ARM IRQ line to use to notify of VPU idle state change */
	u32 set_vpu_idle_update_irq;
	/* VPU -> ARM IRQ line to use to request counter reset. */
	u32 metric_query_event_irq;
	/* ARM -> VPU IRQ line to use to notify of counter reset completion. */
	u32 metric_query_event_done_irq;
	/* VPU -> ARM IRQ line to use to notify of preemption completion. */
	u32 preemption_done_irq;
	/* Padding. */
	u32 pad3[52];
	/* Silicon information: 0x300 - 0x3FF */
	u32 host_version_id;
	u32 si_stepping;
	u64 device_id;
	u64 feature_exclusion;
	u64 sku;
	/** PLL ratio for minimum clock frequency */
	u32 min_freq_pll_ratio;
	/** PLL ratio for maximum clock frequency */
	u32 max_freq_pll_ratio;
	/**
	 * Initial log level threshold (messages with log level severity less than
	 * the threshold will not be logged); applies to every enabled logging
	 * destination and loggable HW component. See 'mvLog_t' enum for acceptable
	 * values.
	 */
	u32 default_trace_level;
	u32 boot_type;
	u64 punit_telemetry_sram_base;
	u64 punit_telemetry_sram_size;
	u32 vpu_telemetry_enable;
	u64 crit_tracing_buff_addr;
	u32 crit_tracing_buff_size;
	u64 verbose_tracing_buff_addr;
	u32 verbose_tracing_buff_size;
	u64 verbose_tracing_sw_component_mask; /* TO BE REMOVED */
	/**
	 * Mask of destinations to which logging messages are delivered; bitwise OR
	 * of values defined in vpu_trace_destination enum.
	 */
	u32 trace_destination_mask;
	/**
	 * Mask of hardware components for which logging is enabled; bitwise OR of
	 * bits defined by the VPU_TRACE_PROC_BIT_* macros.
	 */
	u64 trace_hw_component_mask;
	/** Mask of trace message formats supported by the driver */
	u64 tracing_buff_message_format_mask;
	u64 trace_reserved_1[2];
	/**
	 * Period at which the VPU reads the temp sensor values into MMIO, on
	 * platforms where that is necessary (in ms). 0 to disable reads.
	 */
	u32 temp_sensor_period_ms;
	/** PLL ratio for efficient clock frequency */
	u32 pn_freq_pll_ratio;
	u32 pad4[28];
	/* Warm boot information: 0x400 - 0x43F */
	u32 warm_boot_sections_count;
	u32 warm_boot_start_address_reference;
	u32 warm_boot_section_info_address_offset;
	u32 pad5[13];
	/* Power States transitions timestamps: 0x440 - 0x46F*/
	struct {
		/* VPU_IDLE -> VPU_ACTIVE transition initiated timestamp */
		u64 vpu_active_state_requested;
		/* VPU_IDLE -> VPU_ACTIVE transition completed timestamp */
		u64 vpu_active_state_achieved;
		/* VPU_ACTIVE -> VPU_IDLE transition initiated timestamp */
		u64 vpu_idle_state_requested;
		/* VPU_ACTIVE -> VPU_IDLE transition completed timestamp */
		u64 vpu_idle_state_achieved;
		/* VPU_IDLE -> VPU_STANDBY transition initiated timestamp */
		u64 vpu_standby_state_requested;
		/* VPU_IDLE -> VPU_STANDBY transition completed timestamp */
		u64 vpu_standby_state_achieved;
	} power_states_timestamps;
	/* VPU scheduling mode. Values defined by VPU_SCHEDULING_MODE_* macros. */
	u32 vpu_scheduling_mode;
	/* Present call period in milliseconds. */
	u32 vpu_focus_present_timer_ms;
	/* Unused/reserved: 0x478 - 0xFFF */
	u32 pad6[738];
};

/*
 * Magic numbers set between host and vpu to detect corruptio of tracing init
 */

#define VPU_TRACING_BUFFER_CANARY (0xCAFECAFE)

/* Tracing buffer message format definitions */
#define VPU_TRACING_FORMAT_STRING 0
#define VPU_TRACING_FORMAT_MIPI	  2
/*
 * Header of the tracing buffer.
 * The below defined header will be stored at the beginning of
 * each allocated tracing buffer, followed by a series of 256b
 * of ASCII trace message entries.
 */
struct vpu_tracing_buffer_header {
	/**
	 * Magic number set by host to detect corruption
	 * @see VPU_TRACING_BUFFER_CANARY
	 */
	u32 host_canary_start;
	/* offset from start of buffer for trace entries */
	u32 read_index;
	u32 pad_to_cache_line_size_0[14];
	/* End of first cache line */

	/**
	 * Magic number set by host to detect corruption
	 * @see VPU_TRACING_BUFFER_CANARY
	 */
	u32 vpu_canary_start;
	/* offset from start of buffer from write start */
	u32 write_index;
	/* counter for buffer wrapping */
	u32 wrap_count;
	/* legacy field - do not use */
	u32 reserved_0;
	/**
	 * Size of the log buffer include this header (@header_size) and space
	 * reserved for all messages. If @alignment` is greater that 0 the @Size
	 * must be multiple of @Alignment.
	 */
	u32 size;
	/* Header version */
	u16 header_version;
	/* Header size */
	u16 header_size;
	/*
	 * Format of the messages in the trace buffer
	 * 0 - null terminated string
	 * 1 - size + null terminated string
	 * 2 - MIPI-SysT encoding
	 */
	u32 format;
	/*
	 * Message alignment
	 * 0 - messages are place 1 after another
	 * n - every message starts and multiple on offset
	 */
	u32 alignment; /* 64, 128, 256 */
	/* Name of the logging entity, i.e "LRT", "LNN", "SHV0", etc */
	char name[16];
	u32 pad_to_cache_line_size_1[4];
	/* End of second cache line */
};

#pragma pack(pop)

#endif
