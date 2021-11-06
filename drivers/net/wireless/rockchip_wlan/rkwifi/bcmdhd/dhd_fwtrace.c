/*
 * Firmware trace handling on the DHD side. Kernel thread reads the trace data and writes
 * to the file and implements various utility functions.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id$
 */

#ifdef BCMINTERNAL

#ifdef DHD_FWTRACE

#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>

#include <dhd_fwtrace.h>

static int fwtrace_write_to_file(uint8 *buf, uint16 buf_len, dhd_pub_t *dhdp);
static int fwtrace_close_file(dhd_pub_t *dhdp);
static int fwtrace_open_file(uint32 fw_trace_enabled, dhd_pub_t *dhdp);
static fwtrace_buf_t *fwtrace_get_trace_data_ptr(dhd_pub_t *dhdp);
static void fwtrace_free_trace_buf(dhd_pub_t *dhdp);

typedef struct fwtrace_info {
	struct file *fw_trace_fp;
	int file_index;
	int part_index;
	int trace_buf_index;
	int trace_buf_count;
	uint16 overflow_counter;

	char trace_file[TRACE_FILE_NAME_LEN];

	fwtrace_buf_t *trace_data_ptr;

	uint16 prev_seq;

	uint32 fwtrace_enable;		/* Enable firmware tracing and the
					 * trace file management.
					 */
	struct mutex fwtrace_lock;	/* Synchronization between the
					 * ioctl and the kernel thread.
					 */
	dhd_dma_buf_t fwtrace_buf;	/* firmware trace buffer */
} fwtrace_info_t;

int
dhd_fwtrace_attach(dhd_pub_t *dhdp)
{
	fwtrace_info_t *fwtrace_info;

	/* Allocate prot structure */
	if (!(fwtrace_info = (fwtrace_info_t *)VMALLOCZ(dhdp->osh, sizeof(*fwtrace_info)))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		return (BCME_NOMEM);
	}

	bzero(fwtrace_info, sizeof(*fwtrace_info));
	dhdp->fwtrace_info = fwtrace_info;

	mutex_init(&dhdp->fwtrace_info->fwtrace_lock);

	DHD_INFO(("allocated DHD fwtrace\n"));

	return BCME_OK;
}

int
dhd_fwtrace_detach(dhd_pub_t *dhdp)
{
	fwtrace_info_t *fwtrace_info;

	DHD_TRACE(("%s: %d\n", __FUNCTION__, __LINE__));

	if (!dhdp) {
		return BCME_OK;
	}

	if (!dhdp->fwtrace_info) {
		return BCME_OK;
	}

	fwtrace_info = dhdp->fwtrace_info;

	dhd_dma_buf_free(dhdp, &dhdp->fwtrace_info->fwtrace_buf);

	/* close the file if valid */
	if (!(IS_ERR_OR_NULL(dhdp->fwtrace_info->fw_trace_fp))) {
		(void) filp_close(dhdp->fwtrace_info->fw_trace_fp, 0);
	}

	mutex_destroy(&dhdp->fwtrace_info->fwtrace_lock);

	VMFREE(dhdp->osh, fwtrace_info, sizeof(*fwtrace_info));

	dhdp->fwtrace_info = NULL;

	DHD_INFO(("Deallocated DHD fwtrace_info\n"));

	return (BCME_OK);
}

uint16
get_fw_trace_overflow_counter(dhd_pub_t *dhdp)
{
	return (dhdp->fwtrace_info->overflow_counter);
}

void
process_fw_trace_data(dhd_pub_t *dhdp)
{
	fwtrace_info_t *fwtrace_info = dhdp->fwtrace_info;
	uint16 length;
	uint16 incoming_seq;
	uint32 trace_buf_index = fwtrace_info->trace_buf_index;
	fwtrace_buf_t * trace_buf;
	fwtrace_buf_t * curr_buf;

	mutex_lock(&fwtrace_info->fwtrace_lock);

	if (fwtrace_info->fw_trace_fp == NULL) {
		goto done;
	}

	if ((trace_buf = fwtrace_get_trace_data_ptr(dhdp)) == NULL) {
		goto done;
	}

	do {
		curr_buf = trace_buf + trace_buf_index;

		length = curr_buf->info.length;
		/* If the incoming length is 0, means nothing is updated by the firmware */
		if (length == 0) {
			break;
		}

		incoming_seq = curr_buf->info.seq_num;

		if (((uint16)(fwtrace_info->prev_seq + 1) != incoming_seq) &&
			length != sizeof(*curr_buf)) {
			DHD_ERROR(("*** invalid trace len idx = %u, length = %u, "
				"cur seq = %u, in-seq = %u \n",
				trace_buf_index, length,
				fwtrace_info->prev_seq, incoming_seq));
			break;
		}

		DHD_TRACE(("*** TRACE BUS: IDX:%d, in-seq:%d(prev-%d), ptr:%p(%llu), len:%d\n",
				trace_buf_index, incoming_seq, fwtrace_info->prev_seq,
				curr_buf, (uint64)curr_buf, length));

		/* Write trace data to a file */
		if (fwtrace_write_to_file((uint8 *) curr_buf, length, dhdp) != BCME_OK) {
			DHD_ERROR(("*** fwtrace_write_to_file has failed \n"));
			break;
		}

		/* Reset length after consuming the fwtrace data */
		curr_buf->info.length = 0;

		if ((fwtrace_info->prev_seq + 1) != incoming_seq) {
			DHD_ERROR(("*** seq mismatch, index = %u, length = %u, "
				"cur seq = %u, in-seq = %u \n",
				trace_buf_index, length,
				fwtrace_info->prev_seq, incoming_seq));
		}
		fwtrace_info->prev_seq = incoming_seq;

		trace_buf_index++;
		trace_buf_index &= (fwtrace_info->trace_buf_count - 1u);
		fwtrace_info->trace_buf_index = trace_buf_index;
	} while (true);

done:
	mutex_unlock(&fwtrace_info->fwtrace_lock);
	return;
}

/*
 * Write the incoming trace data to a file. The maximum file size is 1MB. After that
 * the trace data is saved into a new file.
 */
static int
fwtrace_write_to_file(uint8 *buf, uint16 buf_len, dhd_pub_t *dhdp)
{
	fwtrace_info_t *fwtrace_info = dhdp->fwtrace_info;
	int ret_val = BCME_OK;
	int ret_val_1 = 0;
	mm_segment_t old_fs;
	loff_t pos = 0;
	struct kstat stat;
	int error;

	/* Change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (buf == NULL) {
		ret_val = BCME_ERROR;
		goto done;
	}

	if (IS_ERR_OR_NULL(fwtrace_info->fw_trace_fp)) {
		ret_val = BCME_ERROR;
		goto done;
	}

	//
	// Get the file size
	// if the size + buf_len > TRACE_FILE_SIZE, then write to a different file.
	//
	error = vfs_stat(fwtrace_info->trace_file, &stat);
	if (error) {
		DHD_ERROR(("vfs_stat has failed with error code = %d\n", error));
		goto done;
	}

	if ((int) stat.size + buf_len > TRACE_FILE_SIZE) {
		fwtrace_close_file(dhdp);
		(fwtrace_info->part_index)++;
		fwtrace_open_file(TRUE, dhdp);
	}

	pos = fwtrace_info->fw_trace_fp->f_pos;
	/* Write buf to file */
	ret_val_1 = vfs_write(fwtrace_info->fw_trace_fp,
	                      (char *) buf, (uint32) buf_len, &pos);
	if (ret_val_1 < 0) {
		DHD_ERROR(("write file error, err = %d\n", ret_val_1));
		ret_val = BCME_ERROR;
		goto done;
	}
	fwtrace_info->fw_trace_fp->f_pos = pos;

	/* Sync file from filesystem to physical media */
	ret_val_1 = vfs_fsync(fwtrace_info->fw_trace_fp, 0);
	if (ret_val_1 < 0) {
		DHD_ERROR(("sync file error, error = %d\n", ret_val_1));
		ret_val = BCME_ERROR;
		goto done;
	}

done:
	/* restore previous address limit */
	set_fs(old_fs);
	return (ret_val);
}

/*
 * Start the trace, gets called from the ioctl handler.
 */
int
fw_trace_start(dhd_pub_t *dhdp, uint32 fw_trace_enabled)
{
	int ret_val = BCME_OK;

	(dhdp->fwtrace_info->file_index)++;
	dhdp->fwtrace_info->part_index = 1;

	dhdp->fwtrace_info->trace_buf_index = 0;

	mutex_lock(&dhdp->fwtrace_info->fwtrace_lock);
	ret_val = fwtrace_open_file(fw_trace_enabled, dhdp);
	if (ret_val == BCME_OK) {
		dhdp->fwtrace_info->fwtrace_enable = fw_trace_enabled;
	}
	mutex_unlock(&dhdp->fwtrace_info->fwtrace_lock);

	return (ret_val);
}

/*
 * Stop the trace collection and close the file descriptor.
 */
int
fw_trace_stop(dhd_pub_t *dhdp)
{
	int ret_val = BCME_OK;

	/* Check to see if there is any trace data */
	process_fw_trace_data(dhdp);

	mutex_lock(&dhdp->fwtrace_info->fwtrace_lock); /* acquire lock */
	/* flush the trace buffer */
	ret_val = fwtrace_close_file(dhdp);

	/* free the trace buffer */
	fwtrace_free_trace_buf(dhdp);
	mutex_unlock(&dhdp->fwtrace_info->fwtrace_lock); /* release the lock */

	return (ret_val);
}

/*
 * The trace file format is: fw_trace_w_part_x_y_z
 *     where w is the file index, x is the part index,
 *	     y is in seconds and z is in milliseconds
 *
 *     fw_trace_1_part_1_1539298163209110
 *     fw_trace_1_part_2_1539298194739003  etc.
 *
 */
static int
fwtrace_open_file(uint32 fw_trace_enabled, dhd_pub_t *dhdp)
{
	fwtrace_info_t *fwtrace_info = dhdp->fwtrace_info;
	int ret_val = BCME_OK;
	uint32 file_mode;
	char ts_str[DEBUG_DUMP_TIME_BUF_LEN];

	if (fw_trace_enabled) {
		if (!(IS_ERR_OR_NULL(fwtrace_info->fw_trace_fp))) {
			(void) filp_close(fwtrace_info->fw_trace_fp, 0);
		}

		DHD_INFO((" *** Creating the trace file \n"));

		file_mode = O_CREAT | O_WRONLY | O_SYNC;
		clear_debug_dump_time(ts_str);
		get_debug_dump_time(ts_str);

		snprintf(fwtrace_info->trace_file,
		         sizeof(fwtrace_info->trace_file),
		         "%sfw_trace_%d_part_%d_%x_%s",
		         DHD_COMMON_DUMP_PATH, fwtrace_info->file_index,
		         fwtrace_info->part_index,
		         dhd_bus_get_bp_base(dhdp),
		         ts_str);

		fwtrace_info->fw_trace_fp =
		        filp_open(fwtrace_info->trace_file, file_mode, 0664);

		if (IS_ERR(fwtrace_info->fw_trace_fp)) {
			DHD_ERROR(("Unable to create the fw trace file file: %s\n",
			           fwtrace_info->trace_file));
			ret_val = BCME_ERROR;
			goto done;
		}
	}

done:
	return (ret_val);
}

static int
fwtrace_close_file(dhd_pub_t *dhdp)
{
	int ret_val = BCME_OK;

	if (!(IS_ERR_OR_NULL(dhdp->fwtrace_info->fw_trace_fp))) {
		(void) filp_close(dhdp->fwtrace_info->fw_trace_fp, 0);
	}

	dhdp->fwtrace_info->fw_trace_fp = NULL;

	return (ret_val);
}

#define FWTRACE_HADDR_PARAMS_SIZE	256u
#define FW_TRACE_FLUSH			0x8u /* bit 3 */

static int send_fw_trace_val(dhd_pub_t *dhdp, int val);

/*
 * Initialize FWTRACE.
 * Allocate trace buffer and open trace file.
 */
int
fwtrace_init(dhd_pub_t *dhdp)
{
	int ret_val = BCME_OK;
	fwtrace_hostaddr_info_t host_buf_info;

	if (dhdp->fwtrace_info->fwtrace_buf.va != NULL) {
		/* Already initialized */
		goto done;
	}

	ret_val = fwtrace_get_haddr(dhdp, &host_buf_info);

	if (ret_val != BCME_OK) {
		goto done;
	}

	DHD_INFO(("dhd_get_trace_haddr: addr = %llx, len = %u\n",
		host_buf_info.haddr.u64, host_buf_info.num_bufs));

	/* Initialize and setup the file */
	ret_val = fw_trace_start(dhdp, TRUE);

done:
	return ret_val;
}

/*
 * Process the fwtrace set command to enable/disable firmware tracing.
 * Always, enable preceeds with disable.
 */
int
handle_set_fwtrace(dhd_pub_t *dhdp, uint32 val)
{
	int ret, ret_val = BCME_OK;

	/* On set, consider only lower two bytes for now */
	dhdp->fwtrace_info->fwtrace_enable = (val & 0xFFFF);

	if (val & FW_TRACE_FLUSH) { /* only flush the trace buffer */
		if ((ret_val = send_fw_trace_val(dhdp, val)) != BCME_OK) {
			goto done;
		}
	} else if (val == 0) { /* disable the tracing */
		/* Disable the trace in the firmware */
		if ((ret_val = send_fw_trace_val(dhdp, val)) != BCME_OK) {
			goto done;
		}

		/* cleanup in the driver */
		fw_trace_stop(dhdp);
	} else {		/* enable the tracing */
		fwtrace_hostaddr_info_t haddr_info;

		ret_val = fwtrace_init(dhdp);
		if (ret_val != BCME_OK) {
			goto done;
		}

		if ((ret_val = fwtrace_get_haddr(dhdp, &haddr_info)) != BCME_OK) {
			DHD_ERROR(("%s: set dhd_iovar has failed for "
				"fw_trace_haddr, "
				"ret=%d\n", __FUNCTION__, ret_val));
			goto done;
		}

		ret = dhd_iovar(dhdp, 0, "dngl:fwtrace_haddr",
		                (char *) &haddr_info, sizeof(haddr_info),
		                NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: set dhd_iovar has failed for "
			           "fwtrace_haddr, "
			           "ret=%d\n", __FUNCTION__, ret));
			ret_val = BCME_NOMEM;
			goto done;
		}

		/* Finaly, enable the trace in the firmware */
		if ((ret_val = send_fw_trace_val(dhdp, val)) != BCME_OK) {
			goto done;
		}
	}
done:
	return (ret_val);
}

/*
 * Send dngl:fwtrace IOVAR to the firmware.
 */

static int
send_fw_trace_val(dhd_pub_t *dhdp, int val)
{
	int ret_val = BCME_OK;

	if ((ret_val = dhd_iovar(dhdp, 0, "dngl:fwtrace", (char *)&val, sizeof(val),
	                         NULL, 0, TRUE)) < 0) {
		DHD_ERROR(("%s: set dhd_iovar has failed fwtrace, "
		           "ret=%d\n", __FUNCTION__, ret_val));
	}

	return (ret_val);
}

/*
 * Returns the virual address for the firmware trace buffer.
 * DHD monitors this buffer for an update from the firmware.
 */
static fwtrace_buf_t *
fwtrace_get_trace_data_ptr(dhd_pub_t *dhdp)
{
	return ((fwtrace_buf_t *) dhdp->fwtrace_info->fwtrace_buf.va);
}

int
fwtrace_get_haddr(dhd_pub_t *dhdp,  fwtrace_hostaddr_info_t *haddr_info)
{
	int ret_val = BCME_NOMEM;
	int num_host_buffers = FWTRACE_NUM_HOST_BUFFERS;

	if (haddr_info == NULL) {
		ret_val = BCME_BADARG;
		goto done;
	}

	if (dhdp->fwtrace_info->fwtrace_buf.va != NULL) {
		/* Use the existing buffer and send to the firmware */
		haddr_info->haddr.u64 = HTOL64(*(uint64 *)
			&dhdp->fwtrace_info->fwtrace_buf.pa);
		haddr_info->num_bufs = dhdp->fwtrace_info->trace_buf_count;
		haddr_info->buf_len = sizeof(fwtrace_buf_t);
		ret_val = BCME_OK;
		goto done;
	}

	do {
		/* Initialize firmware trace buffer */
		if (dhd_dma_buf_alloc(dhdp, &dhdp->fwtrace_info->fwtrace_buf,
			sizeof(fwtrace_buf_t) * num_host_buffers) == BCME_OK) {
			dhdp->fwtrace_info->trace_buf_count = num_host_buffers;
			ret_val = BCME_OK;
			break;
		}

		DHD_ERROR(("%s: Allocing %d buffers of size %lu bytes failed\n",
			__FUNCTION__, num_host_buffers,
			sizeof(fwtrace_buf_t) * num_host_buffers));

		/* Retry with smaller numbers */
		num_host_buffers >>= 1;
	} while (num_host_buffers > 0);

	haddr_info->haddr.u64 = HTOL64(*(uint64 *)&dhdp->fwtrace_info->fwtrace_buf.pa);
	haddr_info->num_bufs = num_host_buffers;
	haddr_info->buf_len = sizeof(fwtrace_buf_t);

	DHD_INFO(("Firmware trace buffer, host address = %llx, count = %u \n",
	          haddr_info->haddr.u64,
	          haddr_info->num_bufs));
done:
	return (ret_val);
}

/*
 * Frees the host buffer.
 */
static void
fwtrace_free_trace_buf(dhd_pub_t *dhdp)
{
	dhd_dma_buf_free(dhdp, &dhdp->fwtrace_info->fwtrace_buf);
	return;
}

#endif	/* DHD_FWTRACE */

#endif /* BCMINTERNAL */
