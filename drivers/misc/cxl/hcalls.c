// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 IBM Corp.
 */


#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include "hcalls.h"
#include "trace.h"

#define CXL_HCALL_TIMEOUT 60000
#define CXL_HCALL_TIMEOUT_DOWNLOAD 120000

#define H_ATTACH_CA_PROCESS    0x344
#define H_CONTROL_CA_FUNCTION  0x348
#define H_DETACH_CA_PROCESS    0x34C
#define H_COLLECT_CA_INT_INFO  0x350
#define H_CONTROL_CA_FAULTS    0x354
#define H_DOWNLOAD_CA_FUNCTION 0x35C
#define H_DOWNLOAD_CA_FACILITY 0x364
#define H_CONTROL_CA_FACILITY  0x368

#define H_CONTROL_CA_FUNCTION_RESET                   1 /* perform a reset */
#define H_CONTROL_CA_FUNCTION_SUSPEND_PROCESS         2 /* suspend a process from being executed */
#define H_CONTROL_CA_FUNCTION_RESUME_PROCESS          3 /* resume a process to be executed */
#define H_CONTROL_CA_FUNCTION_READ_ERR_STATE          4 /* read the error state */
#define H_CONTROL_CA_FUNCTION_GET_AFU_ERR             5 /* collect the AFU error buffer */
#define H_CONTROL_CA_FUNCTION_GET_CONFIG              6 /* collect configuration record */
#define H_CONTROL_CA_FUNCTION_GET_DOWNLOAD_STATE      7 /* query to return download status */
#define H_CONTROL_CA_FUNCTION_TERMINATE_PROCESS       8 /* terminate the process before completion */
#define H_CONTROL_CA_FUNCTION_COLLECT_VPD             9 /* collect VPD */
#define H_CONTROL_CA_FUNCTION_GET_FUNCTION_ERR_INT   11 /* read the function-wide error data based on an interrupt */
#define H_CONTROL_CA_FUNCTION_ACK_FUNCTION_ERR_INT   12 /* acknowledge function-wide error data based on an interrupt */
#define H_CONTROL_CA_FUNCTION_GET_ERROR_LOG          13 /* retrieve the Platform Log ID (PLID) of an error log */

#define H_CONTROL_CA_FAULTS_RESPOND_PSL         1
#define H_CONTROL_CA_FAULTS_RESPOND_AFU         2

#define H_CONTROL_CA_FACILITY_RESET             1 /* perform a reset */
#define H_CONTROL_CA_FACILITY_COLLECT_VPD       2 /* collect VPD */

#define H_DOWNLOAD_CA_FACILITY_DOWNLOAD         1 /* download adapter image */
#define H_DOWNLOAD_CA_FACILITY_VALIDATE         2 /* validate adapter image */


#define _CXL_LOOP_HCALL(call, rc, retbuf, fn, ...)			\
	{								\
		unsigned int delay, total_delay = 0;			\
		u64 token = 0;						\
									\
		memset(retbuf, 0, sizeof(retbuf));			\
		while (1) {						\
			rc = call(fn, retbuf, __VA_ARGS__, token);	\
			token = retbuf[0];				\
			if (rc != H_BUSY && !H_IS_LONG_BUSY(rc))	\
				break;					\
									\
			if (rc == H_BUSY)				\
				delay = 10;				\
			else						\
				delay = get_longbusy_msecs(rc);		\
									\
			total_delay += delay;				\
			if (total_delay > CXL_HCALL_TIMEOUT) {		\
				WARN(1, "Warning: Giving up waiting for CXL hcall " \
					"%#x after %u msec\n", fn, total_delay); \
				rc = H_BUSY;				\
				break;					\
			}						\
			msleep(delay);					\
		}							\
	}
#define CXL_H_WAIT_UNTIL_DONE(...)  _CXL_LOOP_HCALL(plpar_hcall, __VA_ARGS__)
#define CXL_H9_WAIT_UNTIL_DONE(...) _CXL_LOOP_HCALL(plpar_hcall9, __VA_ARGS__)

#define _PRINT_MSG(rc, format, ...)					\
	{								\
		if ((rc != H_SUCCESS) && (rc != H_CONTINUE))		\
			pr_err(format, __VA_ARGS__);			\
		else							\
			pr_devel(format, __VA_ARGS__);			\
	}								\


static char *afu_op_names[] = {
	"UNKNOWN_OP",		/* 0 undefined */
	"RESET",		/* 1 */
	"SUSPEND_PROCESS",	/* 2 */
	"RESUME_PROCESS",	/* 3 */
	"READ_ERR_STATE",	/* 4 */
	"GET_AFU_ERR",		/* 5 */
	"GET_CONFIG",		/* 6 */
	"GET_DOWNLOAD_STATE",	/* 7 */
	"TERMINATE_PROCESS",	/* 8 */
	"COLLECT_VPD",		/* 9 */
	"UNKNOWN_OP",		/* 10 undefined */
	"GET_FUNCTION_ERR_INT",	/* 11 */
	"ACK_FUNCTION_ERR_INT",	/* 12 */
	"GET_ERROR_LOG",	/* 13 */
};

static char *control_adapter_op_names[] = {
	"UNKNOWN_OP",		/* 0 undefined */
	"RESET",		/* 1 */
	"COLLECT_VPD",		/* 2 */
};

static char *download_op_names[] = {
	"UNKNOWN_OP",		/* 0 undefined */
	"DOWNLOAD",		/* 1 */
	"VALIDATE",		/* 2 */
};

static char *op_str(unsigned int op, char *name_array[], int array_len)
{
	if (op >= array_len)
		return "UNKNOWN_OP";
	return name_array[op];
}

#define OP_STR(op, name_array)      op_str(op, name_array, ARRAY_SIZE(name_array))

#define OP_STR_AFU(op)              OP_STR(op, afu_op_names)
#define OP_STR_CONTROL_ADAPTER(op)  OP_STR(op, control_adapter_op_names)
#define OP_STR_DOWNLOAD_ADAPTER(op) OP_STR(op, download_op_names)


long cxl_h_attach_process(u64 unit_address,
			struct cxl_process_element_hcall *element,
			u64 *process_token, u64 *mmio_addr, u64 *mmio_size)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	CXL_H_WAIT_UNTIL_DONE(rc, retbuf, H_ATTACH_CA_PROCESS, unit_address, virt_to_phys(element));
	_PRINT_MSG(rc, "cxl_h_attach_process(%#.16llx, %#.16lx): %li\n",
		unit_address, virt_to_phys(element), rc);
	trace_cxl_hcall_attach(unit_address, virt_to_phys(element), retbuf[0], retbuf[1], retbuf[2], rc);

	pr_devel("token: 0x%.8lx mmio_addr: 0x%lx mmio_size: 0x%lx\nProcess Element Structure:\n",
		retbuf[0], retbuf[1], retbuf[2]);
	cxl_dump_debug_buffer(element, sizeof(*element));

	switch (rc) {
	case H_SUCCESS:       /* The process info is attached to the coherent platform function */
		*process_token = retbuf[0];
		if (mmio_addr)
			*mmio_addr = retbuf[1];
		if (mmio_size)
			*mmio_size = retbuf[2];
		return 0;
	case H_PARAMETER:     /* An incorrect parameter was supplied. */
	case H_FUNCTION:      /* The function is not supported. */
		return -EINVAL;
	case H_AUTHORITY:     /* The partition does not have authority to perform this hcall */
	case H_RESOURCE:      /* The coherent platform function does not have enough additional resource to attach the process */
	case H_HARDWARE:      /* A hardware event prevented the attach operation */
	case H_STATE:         /* The coherent platform function is not in a valid state */
	case H_BUSY:
		return -EBUSY;
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

/**
 * cxl_h_detach_process - Detach a process element from a coherent
 *                        platform function.
 */
long cxl_h_detach_process(u64 unit_address, u64 process_token)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	CXL_H_WAIT_UNTIL_DONE(rc, retbuf, H_DETACH_CA_PROCESS, unit_address, process_token);
	_PRINT_MSG(rc, "cxl_h_detach_process(%#.16llx, 0x%.8llx): %li\n", unit_address, process_token, rc);
	trace_cxl_hcall_detach(unit_address, process_token, rc);

	switch (rc) {
	case H_SUCCESS:       /* The process was detached from the coherent platform function */
		return 0;
	case H_PARAMETER:     /* An incorrect parameter was supplied. */
		return -EINVAL;
	case H_AUTHORITY:     /* The partition does not have authority to perform this hcall */
	case H_RESOURCE:      /* The function has page table mappings for MMIO */
	case H_HARDWARE:      /* A hardware event prevented the detach operation */
	case H_STATE:         /* The coherent platform function is not in a valid state */
	case H_BUSY:
		return -EBUSY;
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

/**
 * cxl_h_control_function - This H_CONTROL_CA_FUNCTION hypervisor call allows
 *                          the partition to manipulate or query
 *                          certain coherent platform function behaviors.
 */
static long cxl_h_control_function(u64 unit_address, u64 op,
				   u64 p1, u64 p2, u64 p3, u64 p4, u64 *out)
{
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];
	long rc;

	CXL_H9_WAIT_UNTIL_DONE(rc, retbuf, H_CONTROL_CA_FUNCTION, unit_address, op, p1, p2, p3, p4);
	_PRINT_MSG(rc, "cxl_h_control_function(%#.16llx, %s(%#llx, %#llx, %#llx, %#llx, R4: %#lx)): %li\n",
		unit_address, OP_STR_AFU(op), p1, p2, p3, p4, retbuf[0], rc);
	trace_cxl_hcall_control_function(unit_address, OP_STR_AFU(op), p1, p2, p3, p4, retbuf[0], rc);

	switch (rc) {
	case H_SUCCESS:       /* The operation is completed for the coherent platform function */
		if ((op == H_CONTROL_CA_FUNCTION_GET_FUNCTION_ERR_INT ||
		     op == H_CONTROL_CA_FUNCTION_READ_ERR_STATE ||
		     op == H_CONTROL_CA_FUNCTION_COLLECT_VPD))
			*out = retbuf[0];
		return 0;
	case H_PARAMETER:     /* An incorrect parameter was supplied. */
	case H_FUNCTION:      /* The function is not supported. */
	case H_NOT_FOUND:     /* The operation supplied was not valid */
	case H_NOT_AVAILABLE: /* The operation cannot be performed because the AFU has not been downloaded */
	case H_SG_LIST:       /* An block list entry was invalid */
		return -EINVAL;
	case H_AUTHORITY:     /* The partition does not have authority to perform this hcall */
	case H_RESOURCE:      /* The function has page table mappings for MMIO */
	case H_HARDWARE:      /* A hardware event prevented the attach operation */
	case H_STATE:         /* The coherent platform function is not in a valid state */
	case H_BUSY:
		return -EBUSY;
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

/**
 * cxl_h_reset_afu - Perform a reset to the coherent platform function.
 */
long cxl_h_reset_afu(u64 unit_address)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_RESET,
				0, 0, 0, 0,
				NULL);
}

/**
 * cxl_h_suspend_process - Suspend a process from being executed
 * Parameter1 = process-token as returned from H_ATTACH_CA_PROCESS when
 *              process was attached.
 */
long cxl_h_suspend_process(u64 unit_address, u64 process_token)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_SUSPEND_PROCESS,
				process_token, 0, 0, 0,
				NULL);
}

/**
 * cxl_h_resume_process - Resume a process to be executed
 * Parameter1 = process-token as returned from H_ATTACH_CA_PROCESS when
 *              process was attached.
 */
long cxl_h_resume_process(u64 unit_address, u64 process_token)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_RESUME_PROCESS,
				process_token, 0, 0, 0,
				NULL);
}

/**
 * cxl_h_read_error_state - Checks the error state of the coherent
 *                          platform function.
 * R4 contains the error state
 */
long cxl_h_read_error_state(u64 unit_address, u64 *state)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_READ_ERR_STATE,
				0, 0, 0, 0,
				state);
}

/**
 * cxl_h_get_afu_err - collect the AFU error buffer
 * Parameter1 = byte offset into error buffer to retrieve, valid values
 *              are between 0 and (ibm,error-buffer-size - 1)
 * Parameter2 = 4K aligned real address of error buffer, to be filled in
 * Parameter3 = length of error buffer, valid values are 4K or less
 */
long cxl_h_get_afu_err(u64 unit_address, u64 offset,
		u64 buf_address, u64 len)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_GET_AFU_ERR,
				offset, buf_address, len, 0,
				NULL);
}

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
		u64 buf_address, u64 len)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_GET_CONFIG,
				cr_num, offset, buf_address, len,
				NULL);
}

/**
 * cxl_h_terminate_process - Terminate the process before completion
 * Parameter1 = process-token as returned from H_ATTACH_CA_PROCESS when
 *              process was attached.
 */
long cxl_h_terminate_process(u64 unit_address, u64 process_token)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_TERMINATE_PROCESS,
				process_token, 0, 0, 0,
				NULL);
}

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
		       u64 num, u64 *out)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_COLLECT_VPD,
				record, list_address, num, 0,
				out);
}

/**
 * cxl_h_get_fn_error_interrupt - Read the function-wide error data based on an interrupt
 */
long cxl_h_get_fn_error_interrupt(u64 unit_address, u64 *reg)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_GET_FUNCTION_ERR_INT,
				0, 0, 0, 0, reg);
}

/**
 * cxl_h_ack_fn_error_interrupt - Acknowledge function-wide error data
 *                                based on an interrupt
 * Parameter1 = value to write to the function-wide error interrupt register
 */
long cxl_h_ack_fn_error_interrupt(u64 unit_address, u64 value)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_ACK_FUNCTION_ERR_INT,
				value, 0, 0, 0,
				NULL);
}

/**
 * cxl_h_get_error_log - Retrieve the Platform Log ID (PLID) of
 *                       an error log
 */
long cxl_h_get_error_log(u64 unit_address, u64 value)
{
	return cxl_h_control_function(unit_address,
				H_CONTROL_CA_FUNCTION_GET_ERROR_LOG,
				0, 0, 0, 0,
				NULL);
}

/**
 * cxl_h_collect_int_info - Collect interrupt info about a coherent
 *                          platform function after an interrupt occurred.
 */
long cxl_h_collect_int_info(u64 unit_address, u64 process_token,
			    struct cxl_irq_info *info)
{
	long rc;

	BUG_ON(sizeof(*info) != sizeof(unsigned long[PLPAR_HCALL9_BUFSIZE]));

	rc = plpar_hcall9(H_COLLECT_CA_INT_INFO, (unsigned long *) info,
			unit_address, process_token);
	_PRINT_MSG(rc, "cxl_h_collect_int_info(%#.16llx, 0x%llx): %li\n",
		unit_address, process_token, rc);
	trace_cxl_hcall_collect_int_info(unit_address, process_token, rc);

	switch (rc) {
	case H_SUCCESS:     /* The interrupt info is returned in return registers. */
		pr_devel("dsisr:%#llx, dar:%#llx, dsr:%#llx, pid_tid:%#llx, afu_err:%#llx, errstat:%#llx\n",
			info->dsisr, info->dar, info->dsr, info->reserved,
			info->afu_err, info->errstat);
		return 0;
	case H_PARAMETER:   /* An incorrect parameter was supplied. */
		return -EINVAL;
	case H_AUTHORITY:   /* The partition does not have authority to perform this hcall. */
	case H_HARDWARE:    /* A hardware event prevented the collection of the interrupt info.*/
	case H_STATE:       /* The coherent platform function is not in a valid state to collect interrupt info. */
		return -EBUSY;
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

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
			  u64 control_mask, u64 reset_mask)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	memset(retbuf, 0, sizeof(retbuf));

	rc = plpar_hcall(H_CONTROL_CA_FAULTS, retbuf, unit_address,
			H_CONTROL_CA_FAULTS_RESPOND_PSL, process_token,
			control_mask, reset_mask);
	_PRINT_MSG(rc, "cxl_h_control_faults(%#.16llx, 0x%llx, %#llx, %#llx): %li (%#lx)\n",
		unit_address, process_token, control_mask, reset_mask,
		rc, retbuf[0]);
	trace_cxl_hcall_control_faults(unit_address, process_token,
				control_mask, reset_mask, retbuf[0], rc);

	switch (rc) {
	case H_SUCCESS:    /* Faults were successfully controlled for the function. */
		return 0;
	case H_PARAMETER:  /* An incorrect parameter was supplied. */
		return -EINVAL;
	case H_HARDWARE:   /* A hardware event prevented the control of faults. */
	case H_STATE:      /* The function was in an invalid state. */
	case H_AUTHORITY:  /* The partition does not have authority to perform this hcall; the coherent platform facilities may need to be licensed. */
		return -EBUSY;
	case H_FUNCTION:   /* The function is not supported */
	case H_NOT_FOUND:  /* The operation supplied was not valid */
		return -EINVAL;
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

/**
 * cxl_h_control_facility - This H_CONTROL_CA_FACILITY hypervisor call
 *                          allows the partition to manipulate or query
 *                          certain coherent platform facility behaviors.
 */
static long cxl_h_control_facility(u64 unit_address, u64 op,
				   u64 p1, u64 p2, u64 p3, u64 p4, u64 *out)
{
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];
	long rc;

	CXL_H9_WAIT_UNTIL_DONE(rc, retbuf, H_CONTROL_CA_FACILITY, unit_address, op, p1, p2, p3, p4);
	_PRINT_MSG(rc, "cxl_h_control_facility(%#.16llx, %s(%#llx, %#llx, %#llx, %#llx, R4: %#lx)): %li\n",
		unit_address, OP_STR_CONTROL_ADAPTER(op), p1, p2, p3, p4, retbuf[0], rc);
	trace_cxl_hcall_control_facility(unit_address, OP_STR_CONTROL_ADAPTER(op), p1, p2, p3, p4, retbuf[0], rc);

	switch (rc) {
	case H_SUCCESS:       /* The operation is completed for the coherent platform facility */
		if (op == H_CONTROL_CA_FACILITY_COLLECT_VPD)
			*out = retbuf[0];
		return 0;
	case H_PARAMETER:     /* An incorrect parameter was supplied. */
	case H_FUNCTION:      /* The function is not supported. */
	case H_NOT_FOUND:     /* The operation supplied was not valid */
	case H_NOT_AVAILABLE: /* The operation cannot be performed because the AFU has not been downloaded */
	case H_SG_LIST:       /* An block list entry was invalid */
		return -EINVAL;
	case H_AUTHORITY:     /* The partition does not have authority to perform this hcall */
	case H_RESOURCE:      /* The function has page table mappings for MMIO */
	case H_HARDWARE:      /* A hardware event prevented the attach operation */
	case H_STATE:         /* The coherent platform facility is not in a valid state */
	case H_BUSY:
		return -EBUSY;
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

/**
 * cxl_h_reset_adapter - Perform a reset to the coherent platform facility.
 */
long cxl_h_reset_adapter(u64 unit_address)
{
	return cxl_h_control_facility(unit_address,
				H_CONTROL_CA_FACILITY_RESET,
				0, 0, 0, 0,
				NULL);
}

/**
 * cxl_h_collect_vpd - Collect VPD for the coherent platform function.
 * Parameter1 = 4K naturally aligned real buffer containing block
 *              list entries
 * Parameter2 = number of block list entries in the block list, valid
 *              values are between 0 and 256
 */
long cxl_h_collect_vpd_adapter(u64 unit_address, u64 list_address,
			       u64 num, u64 *out)
{
	return cxl_h_control_facility(unit_address,
				H_CONTROL_CA_FACILITY_COLLECT_VPD,
				list_address, num, 0, 0,
				out);
}

/**
 * cxl_h_download_facility - This H_DOWNLOAD_CA_FACILITY
 *                    hypervisor call provide platform support for
 *                    downloading a base adapter image to the coherent
 *                    platform facility, and for validating the entire
 *                    image after the download.
 * Parameters
 *    op: operation to perform to the coherent platform function
 *      Download: operation = 1, the base image in the coherent platform
 *                               facility is first erased, and then
 *                               programmed using the image supplied
 *                               in the scatter/gather list.
 *      Validate: operation = 2, the base image in the coherent platform
 *                               facility is compared with the image
 *                               supplied in the scatter/gather list.
 *    list_address: 4K naturally aligned real buffer containing
 *                  scatter/gather list entries.
 *    num: number of block list entries in the scatter/gather list.
 */
static long cxl_h_download_facility(u64 unit_address, u64 op,
				    u64 list_address, u64 num,
				    u64 *out)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	unsigned int delay, total_delay = 0;
	u64 token = 0;
	long rc;

	if (*out != 0)
		token = *out;

	memset(retbuf, 0, sizeof(retbuf));
	while (1) {
		rc = plpar_hcall(H_DOWNLOAD_CA_FACILITY, retbuf,
				 unit_address, op, list_address, num,
				 token);
		token = retbuf[0];
		if (rc != H_BUSY && !H_IS_LONG_BUSY(rc))
			break;

		if (rc != H_BUSY) {
			delay = get_longbusy_msecs(rc);
			total_delay += delay;
			if (total_delay > CXL_HCALL_TIMEOUT_DOWNLOAD) {
				WARN(1, "Warning: Giving up waiting for CXL hcall "
					"%#x after %u msec\n",
					H_DOWNLOAD_CA_FACILITY, total_delay);
				rc = H_BUSY;
				break;
			}
			msleep(delay);
		}
	}
	_PRINT_MSG(rc, "cxl_h_download_facility(%#.16llx, %s(%#llx, %#llx), %#lx): %li\n",
		 unit_address, OP_STR_DOWNLOAD_ADAPTER(op), list_address, num, retbuf[0], rc);
	trace_cxl_hcall_download_facility(unit_address, OP_STR_DOWNLOAD_ADAPTER(op), list_address, num, retbuf[0], rc);

	switch (rc) {
	case H_SUCCESS:       /* The operation is completed for the coherent platform facility */
		return 0;
	case H_PARAMETER:     /* An incorrect parameter was supplied */
	case H_FUNCTION:      /* The function is not supported. */
	case H_SG_LIST:       /* An block list entry was invalid */
	case H_BAD_DATA:      /* Image verification failed */
		return -EINVAL;
	case H_AUTHORITY:     /* The partition does not have authority to perform this hcall */
	case H_RESOURCE:      /* The function has page table mappings for MMIO */
	case H_HARDWARE:      /* A hardware event prevented the attach operation */
	case H_STATE:         /* The coherent platform facility is not in a valid state */
	case H_BUSY:
		return -EBUSY;
	case H_CONTINUE:
		*out = retbuf[0];
		return 1;  /* More data is needed for the complete image */
	default:
		WARN(1, "Unexpected return code: %lx", rc);
		return -EINVAL;
	}
}

/**
 * cxl_h_download_adapter_image - Download the base image to the coherent
 *                                platform facility.
 */
long cxl_h_download_adapter_image(u64 unit_address,
				  u64 list_address, u64 num,
				  u64 *out)
{
	return cxl_h_download_facility(unit_address,
				       H_DOWNLOAD_CA_FACILITY_DOWNLOAD,
				       list_address, num, out);
}

/**
 * cxl_h_validate_adapter_image - Validate the base image in the coherent
 *                                platform facility.
 */
long cxl_h_validate_adapter_image(u64 unit_address,
				  u64 list_address, u64 num,
				  u64 *out)
{
	return cxl_h_download_facility(unit_address,
				       H_DOWNLOAD_CA_FACILITY_VALIDATE,
				       list_address, num, out);
}
