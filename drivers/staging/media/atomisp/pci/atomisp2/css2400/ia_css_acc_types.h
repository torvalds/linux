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

/** @file
 * This file contains types used for acceleration
 */

#include <system_types.h>	/* HAS_IRQ_MAP_VERSION_# */
#include <type_support.h>
#include <platform_support.h>
#include <debug_global.h>

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

/** Type of acceleration.
 */
enum ia_css_acc_type {
	IA_CSS_ACC_NONE,	/**< Normal binary */
	IA_CSS_ACC_OUTPUT,	/**< Accelerator stage on output frame */
	IA_CSS_ACC_VIEWFINDER,	/**< Accelerator stage on viewfinder frame */
	IA_CSS_ACC_STANDALONE,	/**< Stand-alone acceleration */
};

/** Cells types
 */
enum ia_css_cell_type {
	IA_CSS_SP0 = 0,
	IA_CSS_SP1,
	IA_CSS_ISP,
	MAX_NUM_OF_CELLS
};

/** Firmware types.
 */
enum ia_css_fw_type {
	ia_css_sp_firmware,		/**< Firmware for the SP */
	ia_css_isp_firmware,	/**< Firmware for the ISP */
	ia_css_bootloader_firmware, /**< Firmware for the BootLoader */
	ia_css_acc_firmware		/**< Firmware for accelrations */
};

struct ia_css_blob_descr;

/** Blob descriptor.
 * This structure describes an SP or ISP blob.
 * It describes the test, data and bss sections as well as position in a
 * firmware file.
 * For convenience, it contains dynamic data after loading.
 */
struct ia_css_blob_info {
	/**< Static blob data */
	uint32_t offset;		/**< Blob offset in fw file */
	struct ia_css_isp_param_memory_offsets memory_offsets;  /**< offset wrt hdr in bytes */
	uint32_t prog_name_offset;  /**< offset wrt hdr in bytes */
	uint32_t size;			/**< Size of blob */
	uint32_t padding_size;	/**< total cummulative of bytes added due to section alignment */
	uint32_t icache_source;	/**< Position of icache in blob */
	uint32_t icache_size;	/**< Size of icache section */
	uint32_t icache_padding;/**< bytes added due to icache section alignment */
	uint32_t text_source;	/**< Position of text in blob */
	uint32_t text_size;		/**< Size of text section */
	uint32_t text_padding;	/**< bytes added due to text section alignment */
	uint32_t data_source;	/**< Position of data in blob */
	uint32_t data_target;	/**< Start of data in SP dmem */
	uint32_t data_size;		/**< Size of text section */
	uint32_t data_padding;	/**< bytes added due to data section alignment */
	uint32_t bss_target;	/**< Start position of bss in SP dmem */
	uint32_t bss_size;		/**< Size of bss section */
	/**< Dynamic data filled by loader */
	CSS_ALIGN(const void  *code, 8);		/**< Code section absolute pointer within fw, code = icache + text */
	CSS_ALIGN(const void  *data, 8);		/**< Data section absolute pointer within fw, data = data + bss */
};

struct ia_css_binary_input_info {
	uint32_t		min_width;
	uint32_t		min_height;
	uint32_t		max_width;
	uint32_t		max_height;
	uint32_t		source; /* memory, sensor, variable */
};

struct ia_css_binary_output_info {
	uint32_t		min_width;
	uint32_t		min_height;
	uint32_t		max_width;
	uint32_t		max_height;
	uint32_t		num_chunks;
	uint32_t		variable_format;
};

struct ia_css_binary_internal_info {
	uint32_t		max_width;
	uint32_t		max_height;
};

struct ia_css_binary_bds_info {
	uint32_t		supported_bds_factors;
};

struct ia_css_binary_dvs_info {
	uint32_t		max_envelope_width;
	uint32_t		max_envelope_height;
};

struct ia_css_binary_vf_dec_info {
	uint32_t		is_variable;
	uint32_t		max_log_downscale;
};

struct ia_css_binary_s3a_info {
	uint32_t		s3atbl_use_dmem;
	uint32_t		fixed_s3a_deci_log;
};

/** DPC related binary info */
struct ia_css_binary_dpc_info {
	uint32_t		bnr_lite; /**< bnr lite enable flag */
};

struct ia_css_binary_iterator_info {
	uint32_t		num_stripes;
	uint32_t		row_stripes_height;
	uint32_t		row_stripes_overlap_lines;
};

struct ia_css_binary_address_info {
	uint32_t		isp_addresses;	/* Address in ISP dmem */
	uint32_t		main_entry;	/* Address of entry fct */
	uint32_t		in_frame;	/* Address in ISP dmem */
	uint32_t		out_frame;	/* Address in ISP dmem */
	uint32_t		in_data;	/* Address in ISP dmem */
	uint32_t		out_data;	/* Address in ISP dmem */
	uint32_t		sh_dma_cmd_ptr;     /* In ISP dmem */
};

struct ia_css_binary_uds_info {
	uint16_t	bpp;
	uint16_t	use_bci;
	uint16_t	use_str;
	uint16_t	woix;
	uint16_t	woiy;
	uint16_t	extra_out_vecs;
	uint16_t	vectors_per_line_in;
	uint16_t	vectors_per_line_out;
	uint16_t	vectors_c_per_line_in;
	uint16_t	vectors_c_per_line_out;
	uint16_t	vmem_gdc_in_block_height_y;
	uint16_t	vmem_gdc_in_block_height_c;
	/* uint16_t padding; */
};

struct ia_css_binary_pipeline_info {
	uint32_t	mode;
	uint32_t	isp_pipe_version;
	uint32_t	pipelining;
	uint32_t	c_subsampling;
	uint32_t	top_cropping;
	uint32_t	left_cropping;
	uint32_t	variable_resolution;
};

struct ia_css_binary_block_info {
	uint32_t	block_width;
	uint32_t	block_height;
	uint32_t	output_block_height;
};

/** Structure describing an ISP binary.
 * It describes the capabilities of a binary, like the maximum resolution,
 * support features, dma channels, uds features, etc.
 * This part is to be used by the SP.
 * Future refactoring should move binary properties to ia_css_binary_xinfo,
 * thereby making the SP code more binary independent.
 */
struct ia_css_binary_info {
	CSS_ALIGN(uint32_t			id, 8); /* IA_CSS_BINARY_ID_* */
	struct ia_css_binary_pipeline_info	pipeline;
	struct ia_css_binary_input_info		input;
	struct ia_css_binary_output_info	output;
	struct ia_css_binary_internal_info	internal;
	struct ia_css_binary_bds_info		bds;
	struct ia_css_binary_dvs_info		dvs;
	struct ia_css_binary_vf_dec_info	vf_dec;
	struct ia_css_binary_s3a_info		s3a;
	struct ia_css_binary_dpc_info		dpc_bnr; /**< DPC related binary info */
	struct ia_css_binary_iterator_info	iterator;
	struct ia_css_binary_address_info	addresses;
	struct ia_css_binary_uds_info		uds;
	struct ia_css_binary_block_info		block;
	struct ia_css_isp_param_isp_segments	mem_initializers;
/* MW: Packing (related) bools in an integer ?? */
	struct {
#ifdef ISP2401
		uint8_t	luma_only;
		uint8_t	input_yuv;
		uint8_t	input_raw;
#endif
		uint8_t	reduced_pipe;
		uint8_t	vf_veceven;
		uint8_t	dis;
		uint8_t	dvs_envelope;
		uint8_t	uds;
		uint8_t	dvs_6axis;
		uint8_t	block_output;
		uint8_t	streaming_dma;
		uint8_t	ds;
		uint8_t	bayer_fir_6db;
		uint8_t	raw_binning;
		uint8_t	continuous;
		uint8_t	s3a;
		uint8_t	fpnr;
		uint8_t	sc;
		uint8_t	macc;
		uint8_t	output;
		uint8_t	ref_frame;
		uint8_t	tnr;
		uint8_t	xnr;
		uint8_t	params;
		uint8_t	ca_gdc;
		uint8_t	isp_addresses;
		uint8_t	in_frame;
		uint8_t	out_frame;
		uint8_t	high_speed;
		uint8_t	dpc;
		uint8_t padding[2];
	} enable;
	struct {
/* DMA channel ID: [0,...,HIVE_ISP_NUM_DMA_CHANNELS> */
		uint8_t	ref_y_channel;
		uint8_t	ref_c_channel;
		uint8_t	tnr_channel;
		uint8_t	tnr_out_channel;
		uint8_t	dvs_coords_channel;
		uint8_t	output_channel;
		uint8_t	c_channel;
		uint8_t	vfout_channel;
		uint8_t	vfout_c_channel;
		uint8_t	vfdec_bits_per_pixel;
		uint8_t	claimed_by_isp;
		uint8_t padding[2];
	} dma;
};

/** Structure describing an ISP binary.
 * It describes the capabilities of a binary, like the maximum resolution,
 * support features, dma channels, uds features, etc.
 */
struct ia_css_binary_xinfo {
	/* Part that is of interest to the SP. */
	struct ia_css_binary_info    sp;

	/* Rest of the binary info, only interesting to the host. */
	enum ia_css_acc_type	     type;
	CSS_ALIGN(int32_t	     num_output_formats, 8);
	enum ia_css_frame_format     output_formats[IA_CSS_FRAME_FORMAT_NUM];
	CSS_ALIGN(int32_t	     num_vf_formats, 8); /**< number of supported vf formats */
	enum ia_css_frame_format     vf_formats[IA_CSS_FRAME_FORMAT_NUM]; /**< types of supported vf formats */
	uint8_t			     num_output_pins;
	ia_css_ptr		     xmem_addr;
	CSS_ALIGN(const struct ia_css_blob_descr *blob, 8);
	CSS_ALIGN(uint32_t blob_index, 8);
	CSS_ALIGN(union ia_css_all_memory_offsets mem_offsets, 8);
	CSS_ALIGN(struct ia_css_binary_xinfo *next, 8);
};

/** Structure describing the Bootloader (an ISP binary).
 * It contains several address, either in ddr, isp_dmem or
 * the entry function in icache.
 */
struct ia_css_bl_info {
	uint32_t num_dma_cmds;	/**< Number of cmds sent by CSS */
	uint32_t dma_cmd_list;	/**< Dma command list sent by CSS */
	uint32_t sw_state;	/**< Polled from css */
	/* Entry functions */
	uint32_t bl_entry;	/**< The SP entry function */
};

/** Structure describing the SP binary.
 * It contains several address, either in ddr, sp_dmem or
 * the entry function in pmem.
 */
struct ia_css_sp_info {
	uint32_t init_dmem_data; /**< data sect config, stored to dmem */
	uint32_t per_frame_data; /**< Per frame data, stored to dmem */
	uint32_t group;		/**< Per pipeline data, loaded by dma */
	uint32_t output;		/**< SP output data, loaded by dmem */
	uint32_t host_sp_queue;	/**< Host <-> SP queues */
	uint32_t host_sp_com;/**< Host <-> SP commands */
	uint32_t isp_started;	/**< Polled from sensor thread, csim only */
	uint32_t sw_state;	/**< Polled from css */
	uint32_t host_sp_queues_initialized; /**< Polled from the SP */
	uint32_t sleep_mode;  /**< different mode to halt SP */
	uint32_t invalidate_tlb;		/**< inform SP to invalidate mmu TLB */
#ifndef ISP2401
	uint32_t stop_copy_preview;       /**< suspend copy and preview pipe when capture */
#endif
	uint32_t debug_buffer_ddr_address;	/**< inform SP the address
	of DDR debug queue */
	uint32_t perf_counter_input_system_error; /**< input system perf
	counter array */
#ifdef HAS_WATCHDOG_SP_THREAD_DEBUG
	uint32_t debug_wait; /**< thread/pipe post mortem debug */
	uint32_t debug_stage; /**< thread/pipe post mortem debug */
	uint32_t debug_stripe; /**< thread/pipe post mortem debug */
#endif
	uint32_t threads_stack; /**< sp thread's stack pointers */
	uint32_t threads_stack_size; /**< sp thread's stack sizes */
	uint32_t curr_binary_id;        /**< current binary id */
	uint32_t raw_copy_line_count;   /**< raw copy line counter */
	uint32_t ddr_parameter_address; /**< acc param ddrptr, sp dmem */
	uint32_t ddr_parameter_size;    /**< acc param size, sp dmem */
	/* Entry functions */
	uint32_t sp_entry;	/**< The SP entry function */
	uint32_t tagger_frames_addr;   /**< Base address of tagger state */
};

/* The following #if is there because this header file is also included
   by SP and ISP code but they do not need this data and HIVECC has alignment
   issue with the firmware struct/union's.
   More permanent solution will be to refactor this include.
*/
#if !defined(__ISP)
/** Accelerator firmware information.
 */
struct ia_css_acc_info {
	uint32_t per_frame_data; /**< Dummy for now */
};

/** Firmware information.
 */
union ia_css_fw_union {
	struct ia_css_binary_xinfo	isp; /**< ISP info */
	struct ia_css_sp_info		sp;  /**< SP info */
	struct ia_css_bl_info           bl;  /**< Bootloader info */
	struct ia_css_acc_info		acc; /**< Accelerator info */
};

/** Firmware information.
 */
struct ia_css_fw_info {
	size_t			 header_size; /**< size of fw header */
	CSS_ALIGN(uint32_t type, 8);
	union ia_css_fw_union	 info; /**< Binary info */
	struct ia_css_blob_info  blob; /**< Blob info */
	/* Dynamic part */
	struct ia_css_fw_info   *next;
	CSS_ALIGN(uint32_t       loaded, 8);	/**< Firmware has been loaded */
	CSS_ALIGN(const uint8_t *isp_code, 8);  /**< ISP pointer to code */
	/**< Firmware handle between user space and kernel */
	CSS_ALIGN(uint32_t	handle, 8);
	/**< Sections to copy from/to ISP */
	struct ia_css_isp_param_css_segments mem_initializers;
	/**< Initializer for local ISP memories */
};

struct ia_css_blob_descr {
	const unsigned char  *blob;
	struct ia_css_fw_info header;
	const char	     *name;
	union ia_css_all_memory_offsets mem_offsets;
};

struct ia_css_acc_fw;

/** Structure describing the SP binary of a stand-alone accelerator.
 */
struct ia_css_acc_sp {
	void (*init)(struct ia_css_acc_fw *);	/**< init for crun */
	uint32_t sp_prog_name_offset;		/**< program name offset wrt hdr in bytes */
	uint32_t sp_blob_offset;		/**< blob offset wrt hdr in bytes */
	void	 *entry;			/**< Address of sp entry point */
	uint32_t *css_abort;			/**< SP dmem abort flag */
	void	 *isp_code;			/**< SP dmem address holding xmem
						     address of isp code */
	struct ia_css_fw_info fw;		/**< SP fw descriptor */
	const uint8_t *code;			/**< ISP pointer of allocated SP code */
};

/** Acceleration firmware descriptor.
  * This descriptor descibes either SP code (stand-alone), or
  * ISP code (a separate pipeline stage).
  */
struct ia_css_acc_fw_hdr {
	enum ia_css_acc_type type;	/**< Type of accelerator */
	uint32_t	isp_prog_name_offset; /**< program name offset wrt
						   header in bytes */
	uint32_t	isp_blob_offset;      /**< blob offset wrt header
						   in bytes */
	uint32_t	isp_size;	      /**< Size of isp blob */
	const uint8_t  *isp_code;	      /**< ISP pointer to code */
	struct ia_css_acc_sp  sp;  /**< Standalone sp code */
	/**< Firmware handle between user space and kernel */
	uint32_t	handle;
	struct ia_css_data parameters; /**< Current SP parameters */
};

/** Firmware structure.
  * This contains the header and actual blobs.
  * For standalone, it contains SP and ISP blob.
  * For a pipeline stage accelerator, it contains ISP code only.
  * Since its members are variable size, their offsets are described in the
  * header and computed using the access macros below.
  */
struct ia_css_acc_fw {
	struct ia_css_acc_fw_hdr header; /**< firmware header */
	/*
	int8_t   isp_progname[];	  **< ISP program name
	int8_t   sp_progname[];	  **< SP program name, stand-alone only
	uint8_t sp_code[];  **< SP blob, stand-alone only
	uint8_t isp_code[]; **< ISP blob
	*/
};

/* Access macros for firmware */
#define IA_CSS_ACC_OFFSET(t, f, n) ((t)((uint8_t *)(f)+(f->header.n)))
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
#define IA_CSS_EXT_ISP_PROG_NAME(f)   ((const char *)(f)+(f)->blob.prog_name_offset)
#define IA_CSS_EXT_ISP_MEM_OFFSETS(f) \
	((const struct ia_css_memory_offsets *)((const char *)(f)+(f)->blob.mem_offsets))

#endif /* !defined(__ISP) */

enum ia_css_sp_sleep_mode {
	SP_DISABLE_SLEEP_MODE = 0,
	SP_SLEEP_AFTER_FRAME = 1 << 0,
	SP_SLEEP_AFTER_IRQ = 1 << 1
};
#endif /* _IA_CSS_ACC_TYPES_H */
