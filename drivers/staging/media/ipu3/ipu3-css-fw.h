/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */

#ifndef __IPU3_CSS_FW_H
#define __IPU3_CSS_FW_H

/******************* Firmware file definitions *******************/

#define IMGU_FW_NAME			"intel/ipu3-fw.bin"

typedef u32 imgu_fw_ptr;

enum imgu_fw_type {
	IMGU_FW_SP_FIRMWARE,	/* Firmware for the SP */
	IMGU_FW_SP1_FIRMWARE,	/* Firmware for the SP1 */
	IMGU_FW_ISP_FIRMWARE,	/* Firmware for the ISP */
	IMGU_FW_BOOTLOADER_FIRMWARE,	/* Firmware for the BootLoader */
	IMGU_FW_ACC_FIRMWARE	/* Firmware for accelerations */
};

enum imgu_fw_acc_type {
	IMGU_FW_ACC_NONE,	/* Normal binary */
	IMGU_FW_ACC_OUTPUT,	/* Accelerator stage on output frame */
	IMGU_FW_ACC_VIEWFINDER,	/* Accelerator stage on viewfinder frame */
	IMGU_FW_ACC_STANDALONE,	/* Stand-alone acceleration */
};

struct imgu_fw_isp_parameter {
	u32 offset;		/* Offset in isp_<mem> config, params, etc. */
	u32 size;		/* Disabled if 0 */
};

struct imgu_fw_param_memory_offsets {
	struct {
		struct imgu_fw_isp_parameter lin;	/* lin_vmem_params */
		struct imgu_fw_isp_parameter tnr3;	/* tnr3_vmem_params */
		struct imgu_fw_isp_parameter xnr3;	/* xnr3_vmem_params */
	} vmem;
	struct {
		struct imgu_fw_isp_parameter tnr;
		struct imgu_fw_isp_parameter tnr3;	/* tnr3_params */
		struct imgu_fw_isp_parameter xnr3;	/* xnr3_params */
		struct imgu_fw_isp_parameter plane_io_config;	/* 192 bytes */
		struct imgu_fw_isp_parameter rgbir;	/* rgbir_params */
	} dmem;
};

struct imgu_fw_config_memory_offsets {
	struct {
		struct imgu_fw_isp_parameter iterator;
		struct imgu_fw_isp_parameter dvs;
		struct imgu_fw_isp_parameter output;
		struct imgu_fw_isp_parameter raw;
		struct imgu_fw_isp_parameter input_yuv;
		struct imgu_fw_isp_parameter tnr;
		struct imgu_fw_isp_parameter tnr3;
		struct imgu_fw_isp_parameter ref;
	} dmem;
};

struct imgu_fw_state_memory_offsets {
	struct {
		struct imgu_fw_isp_parameter tnr;
		struct imgu_fw_isp_parameter tnr3;
		struct imgu_fw_isp_parameter ref;
	} dmem;
};

union imgu_fw_all_memory_offsets {
	struct {
		u64 imgu_fw_mem_offsets[3]; /* params, config, state */
	} offsets;
	struct {
		u64 ptr;
	} array[IMGU_ABI_PARAM_CLASS_NUM];
};

struct imgu_fw_binary_xinfo {
	/* Part that is of interest to the SP. */
	struct imgu_abi_binary_info sp;

	/* Rest of the binary info, only interesting to the host. */
	u32 type;	/* enum imgu_fw_acc_type */

	u32 num_output_formats __aligned(8);
	u32 output_formats[IMGU_ABI_FRAME_FORMAT_NUM];	/* enum frame_format */

	/* number of supported vf formats */
	u32 num_vf_formats __aligned(8);
	/* types of supported vf formats */
	u32 vf_formats[IMGU_ABI_FRAME_FORMAT_NUM];	/* enum frame_format */
	u8 num_output_pins;
	imgu_fw_ptr xmem_addr;

	u64 imgu_fw_blob_descr_ptr __aligned(8);
	u32 blob_index __aligned(8);
	union imgu_fw_all_memory_offsets mem_offsets __aligned(8);
	struct imgu_fw_binary_xinfo *next __aligned(8);
};

struct imgu_fw_sp_info {
	u32 init_dmem_data;	/* data sect config, stored to dmem */
	u32 per_frame_data;	/* Per frame data, stored to dmem */
	u32 group;		/* Per pipeline data, loaded by dma */
	u32 output;		/* SP output data, loaded by dmem */
	u32 host_sp_queue;	/* Host <-> SP queues */
	u32 host_sp_com;	/* Host <-> SP commands */
	u32 isp_started;	/* P'ed from sensor thread, csim only */
	u32 sw_state;		/* Polled from css, enum imgu_abi_sp_swstate */
	u32 host_sp_queues_initialized;	/* Polled from the SP */
	u32 sleep_mode;		/* different mode to halt SP */
	u32 invalidate_tlb;	/* inform SP to invalidate mmu TLB */
	u32 debug_buffer_ddr_address;	/* the addr of DDR debug queue */

	/* input system perf count array */
	u32 perf_counter_input_system_error;
	u32 threads_stack;	/* sp thread's stack pointers */
	u32 threads_stack_size;	/* sp thread's stack sizes */
	u32 curr_binary_id;	/* current binary id */
	u32 raw_copy_line_count;	/* raw copy line counter */
	u32 ddr_parameter_address;	/* acc param ddrptr, sp dmem */
	u32 ddr_parameter_size;	/* acc param size, sp dmem */
	/* Entry functions */
	u32 sp_entry;		/* The SP entry function */
	u32 tagger_frames_addr;	/* Base address of tagger state */
};

struct imgu_fw_bl_info {
	u32 num_dma_cmds;	/* Number of cmds sent by CSS */
	u32 dma_cmd_list;	/* Dma command list sent by CSS */
	u32 sw_state;		/* Polled from css, enum imgu_abi_bl_swstate */
	/* Entry functions */
	u32 bl_entry;		/* The SP entry function */
};

struct imgu_fw_acc_info {
	u32 per_frame_data;	/* Dummy for now */
};

union imgu_fw_union {
	struct imgu_fw_binary_xinfo isp;	/* ISP info */
	struct imgu_fw_sp_info sp;	/* SP info */
	struct imgu_fw_sp_info sp1;	/* SP1 info */
	struct imgu_fw_bl_info bl;	/* Bootloader info */
	struct imgu_fw_acc_info acc;	/* Accelerator info */
};

struct imgu_fw_info {
	size_t header_size;	/* size of fw header */
	u32 type __aligned(8);	/* enum imgu_fw_type */
	union imgu_fw_union info;	/* Binary info */
	struct imgu_abi_blob_info blob;	/* Blob info */
	/* Dynamic part */
	u64 next;

	u32 loaded __aligned(8);	/* Firmware has been loaded */
	const u64 isp_code __aligned(8);	/* ISP pointer to code */
	/* Firmware handle between user space and kernel */
	u32 handle __aligned(8);
	/* Sections to copy from/to ISP */
	struct imgu_abi_isp_param_segments mem_initializers;
	/* Initializer for local ISP memories */
};

struct imgu_fw_bi_file_h {
	char version[64];	/* branch tag + week day + time */
	int binary_nr;		/* Number of binaries */
	unsigned int h_size;	/* sizeof(struct imgu_fw_bi_file_h) */
};

struct imgu_fw_header {
	struct imgu_fw_bi_file_h file_header;
	struct imgu_fw_info binary_header[1];	/* binary_nr items */
};

/******************* Firmware functions *******************/

int ipu3_css_fw_init(struct ipu3_css *css);
void ipu3_css_fw_cleanup(struct ipu3_css *css);

unsigned int ipu3_css_fw_obgrid_size(const struct imgu_fw_info *bi);
void *ipu3_css_fw_pipeline_params(struct ipu3_css *css, unsigned int pipe,
				  enum imgu_abi_param_class cls,
				  enum imgu_abi_memories mem,
				  struct imgu_fw_isp_parameter *par,
				  size_t par_size, void *binary_params);

#endif
