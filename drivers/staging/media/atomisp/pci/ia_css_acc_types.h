/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _IA_CSS_ACC_TYPES_H
#define _IA_CSS_ACC_TYPES_H

/* @file
 * This file contains types used for acceleration
 */

#include <system_local.h>	/* HAS_IRQ_MAP_VERSION_# */
#include <type_support.h>
#include <platform_support.h>
#include <debug_global.h>
#include <linux/bits.h>

#include "ia_css_types.h"
#include "ia_css_frame_format.h"

/* Should be included without the path.
   However, that requires adding the path to numerous makefiles
   that have nothing to do with isp parameters.
 */
#include "runtime/isp_param/interface/ia_css_isp_param_types.h"

/* Types for the acceleration API.
 * These should be moved to sh_css_internal.h once the old acceleration
 * argument handling has been completed.
 * After that, interpretation of these structures is no longer needed
 * in the kernel and HAL.
*/

/* Type of acceleration.
 */
enum ia_css_acc_type {
	IA_CSS_ACC_NONE,	/** Normal binary */
	IA_CSS_ACC_OUTPUT,	/** Accelerator stage on output frame */
	IA_CSS_ACC_VIEWFINDER,	/** Accelerator stage on viewfinder frame */
	IA_CSS_ACC_STANDALONE,	/** Stand-alone acceleration */
};

/* Cells types
 */
enum ia_css_cell_type {
	IA_CSS_SP0 = 0,
	IA_CSS_SP1,
	IA_CSS_ISP,
	MAX_NUM_OF_CELLS
};

/* Firmware types.
 */
enum ia_css_fw_type {
	ia_css_sp_firmware,		/** Firmware for the SP */
	ia_css_isp_firmware,		/** Firmware for the ISP */
	ia_css_bootloader_firmware,	/** Firmware for the BootLoader */
	ia_css_acc_firmware		/** Firmware for accelrations */
};

struct ia_css_blob_descr;

/* Blob descriptor.
 * This structure describes an SP or ISP blob.
 * It describes the test, data and bss sections as well as position in a
 * firmware file.
 * For convenience, it contains dynamic data after loading.
 */
struct ia_css_blob_info {
	/** Static blob data */
	u32 offset;		/** Blob offset in fw file */
	struct ia_css_isp_param_memory_offsets
		memory_offsets;  /** offset wrt hdr in bytes */
	u32 prog_name_offset;  /** offset wrt hdr in bytes */
	u32 size;			/** Size of blob */
	u32 padding_size;	/** total cummulative of bytes added due to section alignment */
	u32 icache_source;	/** Position of icache in blob */
	u32 icache_size;	/** Size of icache section */
	u32 icache_padding;/** bytes added due to icache section alignment */
	u32 text_source;	/** Position of text in blob */
	u32 text_size;		/** Size of text section */
	u32 text_padding;	/** bytes added due to text section alignment */
	u32 data_source;	/** Position of data in blob */
	u32 data_target;	/** Start of data in SP dmem */
	u32 data_size;		/** Size of text section */
	u32 data_padding;	/** bytes added due to data section alignment */
	u32 bss_target;	/** Start position of bss in SP dmem */
	u32 bss_size;		/** Size of bss section */
	/** Dynamic data filled by loader */
	CSS_ALIGN(const void  *code,
		  8);		/** Code section absolute pointer within fw, code = icache + text */
	CSS_ALIGN(const void  *data,
		  8);		/** Data section absolute pointer within fw, data = data + bss */
};

struct ia_css_binary_input_info {
	u32		min_width;
	u32		min_height;
	u32		max_width;
	u32		max_height;
	u32		source; /* memory, sensor, variable */
};

struct ia_css_binary_output_info {
	u32		min_width;
	u32		min_height;
	u32		max_width;
	u32		max_height;
	u32		num_chunks;
	u32		variable_format;
};

struct ia_css_binary_internal_info {
	u32		max_width;
	u32		max_height;
};

struct ia_css_binary_bds_info {
	u32		supported_bds_factors;
};

struct ia_css_binary_dvs_info {
	u32		max_envelope_width;
	u32		max_envelope_height;
};

struct ia_css_binary_vf_dec_info {
	u32		is_variable;
	u32		max_log_downscale;
};

struct ia_css_binary_s3a_info {
	u32		s3atbl_use_dmem;
	u32		fixed_s3a_deci_log;
};

/* DPC related binary info */
struct ia_css_binary_dpc_info {
	u32		bnr_lite; /** bnr lite enable flag */
};

struct ia_css_binary_iterator_info {
	u32		num_stripes;
	u32		row_stripes_height;
	u32		row_stripes_overlap_lines;
};

struct ia_css_binary_address_info {
	u32		isp_addresses;	/* Address in ISP dmem */
	u32		main_entry;	/* Address of entry fct */
	u32		in_frame;	/* Address in ISP dmem */
	u32		out_frame;	/* Address in ISP dmem */
	u32		in_data;	/* Address in ISP dmem */
	u32		out_data;	/* Address in ISP dmem */
	u32		sh_dma_cmd_ptr;     /* In ISP dmem */
};

struct ia_css_binary_uds_info {
	u16	bpp;
	u16	use_bci;
	u16	use_str;
	u16	woix;
	u16	woiy;
	u16	extra_out_vecs;
	u16	vectors_per_line_in;
	u16	vectors_per_line_out;
	u16	vectors_c_per_line_in;
	u16	vectors_c_per_line_out;
	u16	vmem_gdc_in_block_height_y;
	u16	vmem_gdc_in_block_height_c;
	/* uint16_t padding; */
};

struct ia_css_binary_pipeline_info {
	u32	mode;
	u32	isp_pipe_version;
	u32	pipelining;
	u32	c_subsampling;
	u32	top_cropping;
	u32	left_cropping;
	u32	variable_resolution;
};

struct ia_css_binary_block_info {
	u32	block_width;
	u32	block_height;
	u32	output_block_height;
};

/* Structure describing an ISP binary.
 * It describes the capabilities of a binary, like the maximum resolution,
 * support features, dma channels, uds features, etc.
 * This part is to be used by the SP.
 * Future refactoring should move binary properties to ia_css_binary_xinfo,
 * thereby making the SP code more binary independent.
 */
struct ia_css_binary_info {
	CSS_ALIGN(u32			id, 8); /* IA_CSS_BINARY_ID_* */
	struct ia_css_binary_pipeline_info	pipeline;
	struct ia_css_binary_input_info		input;
	struct ia_css_binary_output_info	output;
	struct ia_css_binary_internal_info	internal;
	struct ia_css_binary_bds_info		bds;
	struct ia_css_binary_dvs_info		dvs;
	struct ia_css_binary_vf_dec_info	vf_dec;
	struct ia_css_binary_s3a_info		s3a;
	struct ia_css_binary_dpc_info		dpc_bnr; /** DPC related binary info */
	struct ia_css_binary_iterator_info	iterator;
	struct ia_css_binary_address_info	addresses;
	struct ia_css_binary_uds_info		uds;
	struct ia_css_binary_block_info		block;
	struct ia_css_isp_param_isp_segments	mem_initializers;
	/* MW: Packing (related) bools in an integer ?? */
	struct {
		u8	reduced_pipe;
		u8	vf_veceven;
		u8	dis;
		u8	dvs_envelope;
		u8	uds;
		u8	dvs_6axis;
		u8	block_output;
		u8	streaming_dma;
		u8	ds;
		u8	bayer_fir_6db;
		u8	raw_binning;
		u8	continuous;
		u8	s3a;
		u8	fpnr;
		u8	sc;
		u8	macc;
		u8	output;
		u8	ref_frame;
		u8	tnr;
		u8	xnr;
		u8	params;
		u8	ca_gdc;
		u8	isp_addresses;
		u8	in_frame;
		u8	out_frame;
		u8	high_speed;
		u8	dpc;
		u8 padding[2];
	} enable;
	struct {
		/* DMA channel ID: [0,...,HIVE_ISP_NUM_DMA_CHANNELS> */
		u8	ref_y_channel;
		u8	ref_c_channel;
		u8	tnr_channel;
		u8	tnr_out_channel;
		u8	dvs_coords_channel;
		u8	output_channel;
		u8	c_channel;
		u8	vfout_channel;
		u8	vfout_c_channel;
		u8	vfdec_bits_per_pixel;
		u8	claimed_by_isp;
		u8 padding[2];
	} dma;
};

/* Structure describing an ISP binary.
 * It describes the capabilities of a binary, like the maximum resolution,
 * support features, dma channels, uds features, etc.
 */
struct ia_css_binary_xinfo {
	/* Part that is of interest to the SP. */
	struct ia_css_binary_info    sp;

	/* Rest of the binary info, only interesting to the host. */
	enum ia_css_acc_type	     type;

	CSS_ALIGN(s32	     num_output_formats, 8);
	enum ia_css_frame_format     output_formats[IA_CSS_FRAME_FORMAT_NUM];

	CSS_ALIGN(s32	     num_vf_formats, 8); /** number of supported vf formats */
	enum ia_css_frame_format
	vf_formats[IA_CSS_FRAME_FORMAT_NUM]; /** types of supported vf formats */
	u8			     num_output_pins;
	ia_css_ptr		     xmem_addr;

	CSS_ALIGN(const struct ia_css_blob_descr *blob, 8);
	CSS_ALIGN(u32 blob_index, 8);
	CSS_ALIGN(union ia_css_all_memory_offsets mem_offsets, 8);
	CSS_ALIGN(struct ia_css_binary_xinfo *next, 8);
};

/* Structure describing the Bootloader (an ISP binary).
 * It contains several address, either in ddr, isp_dmem or
 * the entry function in icache.
 */
struct ia_css_bl_info {
	u32 num_dma_cmds;	/** Number of cmds sent by CSS */
	u32 dma_cmd_list;	/** Dma command list sent by CSS */
	u32 sw_state;	/** Polled from css */
	/* Entry functions */
	u32 bl_entry;	/** The SP entry function */
};

/* Structure describing the SP binary.
 * It contains several address, either in ddr, sp_dmem or
 * the entry function in pmem.
 */
struct ia_css_sp_info {
	u32 init_dmem_data; /** data sect config, stored to dmem */
	u32 per_frame_data; /** Per frame data, stored to dmem */
	u32 group;		/** Per pipeline data, loaded by dma */
	u32 output;		/** SP output data, loaded by dmem */
	u32 host_sp_queue;	/** Host <-> SP queues */
	u32 host_sp_com;/** Host <-> SP commands */
	u32 isp_started;	/** Polled from sensor thread, csim only */
	u32 sw_state;	/** Polled from css */
	u32 host_sp_queues_initialized; /** Polled from the SP */
	u32 sleep_mode;  /** different mode to halt SP */
	u32 invalidate_tlb;		/** inform SP to invalidate mmu TLB */

	/* ISP2400 */
	u32 stop_copy_preview;       /** suspend copy and preview pipe when capture */

	u32 debug_buffer_ddr_address;	/** inform SP the address
	of DDR debug queue */
	u32 perf_counter_input_system_error; /** input system perf
	counter array */
#ifdef HAS_WATCHDOG_SP_THREAD_DEBUG
	u32 debug_wait; /** thread/pipe post mortem debug */
	u32 debug_stage; /** thread/pipe post mortem debug */
	u32 debug_stripe; /** thread/pipe post mortem debug */
#endif
	u32 threads_stack; /** sp thread's stack pointers */
	u32 threads_stack_size; /** sp thread's stack sizes */
	u32 curr_binary_id;        /** current binary id */
	u32 raw_copy_line_count;   /** raw copy line counter */
	u32 ddr_parameter_address; /** acc param ddrptr, sp dmem */
	u32 ddr_parameter_size;    /** acc param size, sp dmem */
	/* Entry functions */
	u32 sp_entry;	/** The SP entry function */
	u32 tagger_frames_addr;   /** Base address of tagger state */
};

/* The following #if is there because this header file is also included
   by SP and ISP code but they do not need this data and HIVECC has alignment
   issue with the firmware struct/union's.
   More permanent solution will be to refactor this include.
*/

/* Accelerator firmware information.
 */
struct ia_css_acc_info {
	u32 per_frame_data; /** Dummy for now */
};

/* Firmware information.
 */
union ia_css_fw_union {
	struct ia_css_binary_xinfo	isp; /** ISP info */
	struct ia_css_sp_info		sp;  /** SP info */
	struct ia_css_bl_info           bl;  /** Bootloader info */
	struct ia_css_acc_info		acc; /** Accelerator info */
};

/* Firmware information.
 */
struct ia_css_fw_info {
	size_t			 header_size; /** size of fw header */

	CSS_ALIGN(u32 type, 8);
	union ia_css_fw_union	 info; /** Binary info */
	struct ia_css_blob_info  blob; /** Blob info */
	/* Dynamic part */
	struct ia_css_fw_info   *next;

	CSS_ALIGN(u32       loaded, 8);	/** Firmware has been loaded */
	CSS_ALIGN(const u8 *isp_code, 8);  /** ISP pointer to code */
	/** Firmware handle between user space and kernel */
	CSS_ALIGN(u32	handle, 8);
	/** Sections to copy from/to ISP */
	struct ia_css_isp_param_css_segments mem_initializers;
	/** Initializer for local ISP memories */
};

struct ia_css_blob_descr {
	const unsigned char  *blob;
	struct ia_css_fw_info header;
	const char	     *name;
	union ia_css_all_memory_offsets mem_offsets;
};

struct ia_css_acc_fw;

/* Structure describing the SP binary of a stand-alone accelerator.
 */
struct ia_css_acc_sp {
	void (*init)(struct ia_css_acc_fw *);	/** init for crun */
	u32 sp_prog_name_offset;		/** program name offset wrt hdr in bytes */
	u32 sp_blob_offset;		/** blob offset wrt hdr in bytes */
	void	 *entry;			/** Address of sp entry point */
	u32 *css_abort;			/** SP dmem abort flag */
	void	 *isp_code;			/** SP dmem address holding xmem
						     address of isp code */
	struct ia_css_fw_info fw;		/** SP fw descriptor */
	const u8 *code;			/** ISP pointer of allocated SP code */
};

/* Acceleration firmware descriptor.
  * This descriptor descibes either SP code (stand-alone), or
  * ISP code (a separate pipeline stage).
  */
struct ia_css_acc_fw_hdr {
	enum ia_css_acc_type type;	/** Type of accelerator */
	u32	isp_prog_name_offset; /** program name offset wrt
						   header in bytes */
	u32	isp_blob_offset;      /** blob offset wrt header
						   in bytes */
	u32	isp_size;	      /** Size of isp blob */
	const u8  *isp_code;	      /** ISP pointer to code */
	struct ia_css_acc_sp  sp;  /** Standalone sp code */
	/** Firmware handle between user space and kernel */
	u32	handle;
	struct ia_css_data parameters; /** Current SP parameters */
};

/* Firmware structure.
  * This contains the header and actual blobs.
  * For standalone, it contains SP and ISP blob.
  * For a pipeline stage accelerator, it contains ISP code only.
  * Since its members are variable size, their offsets are described in the
  * header and computed using the access macros below.
  */
struct ia_css_acc_fw {
	struct ia_css_acc_fw_hdr header; /** firmware header */
	/*
	int8_t   isp_progname[];	  **< ISP program name
	int8_t   sp_progname[];	  **< SP program name, stand-alone only
	uint8_t sp_code[];  **< SP blob, stand-alone only
	uint8_t isp_code[]; **< ISP blob
	*/
};

/* Access macros for firmware */
#define IA_CSS_ACC_OFFSET(t, f, n) ((t)((uint8_t *)(f) + (f->header.n)))
#define IA_CSS_ACC_SP_PROG_NAME(f) IA_CSS_ACC_OFFSET(const char *, f, \
						 sp.sp_prog_name_offset)
#define IA_CSS_ACC_ISP_PROG_NAME(f) IA_CSS_ACC_OFFSET(const char *, f, \
						 isp_prog_name_offset)
#define IA_CSS_ACC_SP_CODE(f)      IA_CSS_ACC_OFFSET(uint8_t *, f, \
						 sp.sp_blob_offset)
#define IA_CSS_ACC_SP_DATA(f)      (IA_CSS_ACC_SP_CODE(f) + \
					(f)->header.sp.fw.blob.data_source)
#define IA_CSS_ACC_ISP_CODE(f)     IA_CSS_ACC_OFFSET(uint8_t*, f,\
						 isp_blob_offset)
#define IA_CSS_ACC_ISP_SIZE(f)     ((f)->header.isp_size)

/* Binary name follows header immediately */
#define IA_CSS_EXT_ISP_PROG_NAME(f)   ((const char *)(f) + (f)->blob.prog_name_offset)
#define IA_CSS_EXT_ISP_MEM_OFFSETS(f) \
	((const struct ia_css_memory_offsets *)((const char *)(f) + (f)->blob.mem_offsets))

enum ia_css_sp_sleep_mode {
	SP_DISABLE_SLEEP_MODE = 0,
	SP_SLEEP_AFTER_FRAME  = BIT(0),
	SP_SLEEP_AFTER_IRQ    = BIT(1),
};
#endif /* _IA_CSS_ACC_TYPES_H */
