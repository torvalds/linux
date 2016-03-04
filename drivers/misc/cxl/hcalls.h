/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _HCALLS_H
#define _HCALLS_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/hvcall.h>
#include "cxl.h"

#define SG_BUFFER_SIZE 4096
#define SG_MAX_ENTRIES 256

struct sg_list {
	u64 phys_addr;
	u64 len;
};

/*
 * This is straight out of PAPR, but replacing some of the compound fields with
 * a single field, where they were identical to the register layout.
 *
 * The 'flags' parameter regroups the various bit-fields
 */
#define CXL_PE_CSRP_VALID			(1ULL << 63)
#define CXL_PE_PROBLEM_STATE			(1ULL << 62)
#define CXL_PE_SECONDARY_SEGMENT_TBL_SRCH	(1ULL << 61)
#define CXL_PE_TAGS_ACTIVE			(1ULL << 60)
#define CXL_PE_USER_STATE			(1ULL << 59)
#define CXL_PE_TRANSLATION_ENABLED		(1ULL << 58)
#define CXL_PE_64_BIT				(1ULL << 57)
#define CXL_PE_PRIVILEGED_PROCESS		(1ULL << 56)

#define CXL_PROCESS_ELEMENT_VERSION 1
struct cxl_process_element_hcall {
	__be64 version;
	__be64 flags;
	u8     reserved0[12];
	__be32 pslVirtualIsn;
	u8     applicationVirtualIsnBitmap[256];
	u8     reserved1[144];
	struct cxl_process_element_common common;
	u8     reserved4[12];
} __packed;

#define H_STATE_NORMAL              1
#define H_STATE_DISABLE             2
#define H_STATE_TEMP_UNAVAILABLE    3
#define H_STATE_PERM_UNAVAILABLE    4

/* NOTE: element must be a logical real address, and must be pinned */
long cxl_h_attach_process(u64 unit_address, struct cxl_process_element_hcall *element,
			u64 *process_token, u64 *mmio_addr, u64 *mmio_size);

/**
 * cxl_h_detach_process - Detach a process element from a coherent
 *                        platform function.
 */
long cxl_h_detach_process(u64 unit_address, u64 process_token);

/**
 * cxl_h_reset_afu - Perform a reset to the coherent platform function.
 */
long cxl_h_reset_afu(u64 unit_address);

/**
 * cxl_h_suspend_process - Suspend a process from being executed
 * Parameter1 = process-token as returned from H_ATTACH_CA_PROCESS when
 *              process was attached.
 */
long cxl_h_suspend_process(u64 unit_address, u64 process_token);

/**
 * cxl_h_resume_process - Resume a process to be executed
 * Parameter1 = process-token as returned from H_ATTACH_CA_PROCESS when
 *              process was attached.
 */
long cxl_h_resume_process(u64 unit_address, u64 process_token);

/**
 * cxl_h_read_error_state - Reads the error state of the coherent
 *                          platform function.
 * R4 contains the error state
 */
long cxl_h_read_error_state(u64 unit_address, u64 *state);

/**
 * cxl_h_get_afu_err - collect the AFU error buffer
 * Parameter1 = byte offset into error buffer to retrieve, valid values
 *              are between 0 and (ibm,error-buffer-size - 1)
 * Parameter2 = 4K aligned real address of error buffer, to be filled in
 * Parameter3 = length of error buffer, valid values are 4K or less
 */
long cxl_h_get_afu_err(u64 unit_address, u64 offset, u64 buf_address, u64 len);

/**
 * cxl_h_get_config - collect configuration record for the
 *                    coherent platform function
 * Parameter1 = # of configuration record to retrieve, valid values are
 *              between 0 and (ibm,#config-records - 1)
 * Parameter2 = byte offset into configuration record to retrieve,
 *              valid values are between 0 and (ibm,config-record-size - 1)
 * Parameter3 = 4K aligned real address of configuration record buffer,
 *              to be filled in
 * Parameter4 = length of configuration buffer, valid values are 4K or less
 */
long cxl_h_get_config(u64 unit_address, u64 cr_num, u64 offset,
		u64 buf_address, u64 len);

/**
 * cxl_h_terminate_process - Terminate the process before completion
 * Parameter1 = process-token as returned from H_ATTACH_CA_PROCESS when
 *              process was attached.
 */
long cxl_h_terminate_process(u64 unit_address, u64 process_token);

/**
 * cxl_h_collect_vpd - Collect VPD for the coherent platform function.
 * Parameter1 = # of VPD record to retrieve, valid values are between 0
 *              and (ibm,#config-records - 1).
 * Parameter2 = 4K naturally aligned real buffer containing block
 *              list entries
 * Parameter3 = number of block list entries in the block list, valid
 *              values are between 0 and 256
 */
long cxl_h_collect_vpd(u64 unit_address, u64 record, u64 list_address,
		       u64 num, u64 *out);

/**
 * cxl_h_get_fn_error_interrupt - Read the function-wide error data based on an interrupt
 */
long cxl_h_get_fn_error_interrupt(u64 unit_address, u64 *reg);

/**
 * cxl_h_ack_fn_error_interrupt - Acknowledge function-wide error data
 *                                based on an interrupt
 * Parameter1 = value to write to the function-wide error interrupt register
 */
long cxl_h_ack_fn_error_interrupt(u64 unit_address, u64 value);

/**
 * cxl_h_get_error_log - Retrieve the Platform Log ID (PLID) of
 *                       an error log
 */
long cxl_h_get_error_log(u64 unit_address, u64 value);

/**
 * cxl_h_collect_int_info - Collect interrupt info about a coherent
 *                          platform function after an interrupt occurred.
 */
long cxl_h_collect_int_info(u64 unit_address, u64 process_token,
			struct cxl_irq_info *info);

/**
 * cxl_h_control_faults - Control the operation of a coherent platform
 *                        function after a fault occurs.
 *
 * Parameters
 *    control-mask: value to control the faults
 *                  looks like PSL_TFC_An shifted >> 32
 *    reset-mask: mask to control reset of function faults
 *                Set reset_mask = 1 to reset PSL errors
 */
long cxl_h_control_faults(u64 unit_address, u64 process_token,
			u64 control_mask, u64 reset_mask);

/**
 * cxl_h_reset_adapter - Perform a reset to the coherent platform facility.
 */
long cxl_h_reset_adapter(u64 unit_address);

/**
 * cxl_h_collect_vpd - Collect VPD for the coherent platform function.
 * Parameter1 = 4K naturally aligned real buffer containing block
 *              list entries
 * Parameter2 = number of block list entries in the block list, valid
 *              values are between 0 and 256
 */
long cxl_h_collect_vpd_adapter(u64 unit_address, u64 list_address,
			       u64 num, u64 *out);

/**
 * cxl_h_download_adapter_image - Download the base image to the coherent
 *                                platform facility.
 */
long cxl_h_download_adapter_image(u64 unit_address,
				  u64 list_address, u64 num,
				  u64 *out);

/**
 * cxl_h_validate_adapter_image - Validate the base image in the coherent
 *                                platform facility.
 */
long cxl_h_validate_adapter_image(u64 unit_address,
				  u64 list_address, u64 num,
				  u64 *out);
#endif /* _HCALLS_H */
