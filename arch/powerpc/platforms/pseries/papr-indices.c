// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "papr-indices: " fmt

#include <linux/build_bug.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/lockdep.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>
#include <asm/machdep.h>
#include <asm/rtas-work-area.h>
#include <asm/rtas.h>
#include <uapi/asm/papr-indices.h>
#include "papr-rtas-common.h"

/*
 * Function-specific return values for ibm,set-dynamic-indicator and
 * ibm,get-dynamic-sensor-state RTAS calls.
 * PAPR+ v2.13 7.3.18 and 7.3.19.
 */
#define RTAS_IBM_DYNAMIC_INDICE_NO_INDICATOR	-3

/**
 * struct rtas_get_indices_params - Parameters (in and out) for
 *                                      ibm,get-indices.
 * @is_sensor:	In: Caller-provided whether sensor or indicator.
 * @indice_type:In: Caller-provided indice (sensor or indicator) token
 * @work_area:	In: Caller-provided work area buffer for results.
 * @next:	In: Sequence number. Out: Next sequence number.
 * @status:	Out: RTAS call status.
 */
struct rtas_get_indices_params {
	u8 is_sensor;
	u32 indice_type;
	struct rtas_work_area *work_area;
	u32 next;
	s32 status;
};

/*
 * rtas_ibm_get_indices() - Call ibm,get-indices to fill a work area buffer.
 * @params: See &struct rtas_ibm_get_indices_params.
 *
 * Calls ibm,get-indices until it errors or successfully deposits data
 * into the supplied work area. Handles RTAS retry statuses. Maps RTAS
 * error statuses to reasonable errno values.
 *
 * The caller is expected to invoke rtas_ibm_get_indices() multiple times
 * to retrieve all indices data for the provided indice type. Only one
 * sequence should be in progress at any time; starting a new sequence
 * will disrupt any sequence already in progress. Serialization of
 * indices retrieval sequences is the responsibility of the caller.
 *
 * The caller should inspect @params.status to determine whether more
 * calls are needed to complete the sequence.
 *
 * Context: May sleep.
 * Return: -ve on error, 0 otherwise.
 */
static int rtas_ibm_get_indices(struct rtas_get_indices_params *params)
{
	struct rtas_work_area *work_area = params->work_area;
	const s32 token = rtas_function_token(RTAS_FN_IBM_GET_INDICES);
	u32 rets;
	s32 fwrc;
	int ret;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	lockdep_assert_held(&rtas_ibm_get_indices_lock);

	do {
		fwrc = rtas_call(token, 5, 2, &rets, params->is_sensor,
				params->indice_type,
				rtas_work_area_phys(work_area),
				rtas_work_area_size(work_area),
				params->next);
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case RTAS_HARDWARE_ERROR:
		ret = -EIO;
		break;
	case RTAS_INVALID_PARAMETER: /* Indicator type is not supported */
		ret = -EINVAL;
		break;
	case RTAS_SEQ_START_OVER:
		ret = -EAGAIN;
		pr_info_ratelimited("Indices changed during retrieval, retrying\n");
		params->next = 1;
		break;
	case RTAS_SEQ_MORE_DATA:
		params->next = rets;
		ret = 0;
		break;
	case RTAS_SEQ_COMPLETE:
		params->next = 0;
		ret = 0;
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("unexpected ibm,get-indices status %d\n", fwrc);
		break;
	}

	params->status = fwrc;
	return ret;
}

/*
 * Internal indices sequence APIs. A sequence is a series of calls to
 * ibm,get-indices for a given location code. The sequence ends when
 * an error is encountered or all indices for the input has been
 * returned.
 */

/*
 * indices_sequence_begin() - Begin a indices retrieval sequence.
 *
 * Context: May sleep.
 */
static void indices_sequence_begin(struct papr_rtas_sequence *seq)
{
	struct rtas_get_indices_params  *param;

	param = (struct rtas_get_indices_params *)seq->params;
	/*
	 * We could allocate the work area before acquiring the
	 * function lock, but that would allow concurrent requests to
	 * exhaust the limited work area pool for no benefit. So
	 * allocate the work area under the lock.
	 */
	mutex_lock(&rtas_ibm_get_indices_lock);
	param->work_area = rtas_work_area_alloc(RTAS_GET_INDICES_BUF_SIZE);
	param->next = 1;
	param->status = 0;
}

/*
 * indices_sequence_end() - Finalize a indices retrieval sequence.
 *
 * Releases resources obtained by indices_sequence_begin().
 */
static void indices_sequence_end(struct papr_rtas_sequence *seq)
{
	struct rtas_get_indices_params *param;

	param =  (struct rtas_get_indices_params *)seq->params;
	rtas_work_area_free(param->work_area);
	mutex_unlock(&rtas_ibm_get_indices_lock);
}

/*
 * Work function to be passed to papr_rtas_blob_generate().
 *
 * ibm,get-indices RTAS call fills the work area with the certain
 * format but does not return the bytes written in the buffer. So
 * instead of kernel parsing this work area to determine the buffer
 * length, copy the complete work area (RTAS_GET_INDICES_BUF_SIZE)
 * to the blob and let the user space to obtain the data.
 * Means RTAS_GET_INDICES_BUF_SIZE data will be returned for each
 * read().
 */

static const char *indices_sequence_fill_work_area(struct papr_rtas_sequence *seq,
						size_t *len)
{
	struct rtas_get_indices_params *p;
	bool init_state;

	p = (struct rtas_get_indices_params *)seq->params;
	init_state = (p->next == 1) ? true : false;

	if (papr_rtas_sequence_should_stop(seq, p->status, init_state))
		return NULL;
	if (papr_rtas_sequence_set_err(seq, rtas_ibm_get_indices(p)))
		return NULL;

	*len = RTAS_GET_INDICES_BUF_SIZE;
	return rtas_work_area_raw_buf(p->work_area);
}

/*
 * papr_indices_handle_read - returns indices blob data to the user space
 *
 * ibm,get-indices RTAS call fills the work area with the certian
 * format but does not return the bytes written in the buffer and
 * copied RTAS_GET_INDICES_BUF_SIZE data to the blob for each RTAS
 * call. So send RTAS_GET_INDICES_BUF_SIZE buffer to the user space
 * for each read().
 */
static ssize_t papr_indices_handle_read(struct file *file,
		char __user *buf, size_t size, loff_t *off)
{
	const struct papr_rtas_blob *blob = file->private_data;

	/* we should not instantiate a handle without any data attached. */
	if (!papr_rtas_blob_has_data(blob)) {
		pr_err_once("handle without data\n");
		return -EIO;
	}

	if (size < RTAS_GET_INDICES_BUF_SIZE) {
		pr_err_once("Invalid buffer length %ld, expect %d\n",
				size, RTAS_GET_INDICES_BUF_SIZE);
		return -EINVAL;
	} else if (size > RTAS_GET_INDICES_BUF_SIZE)
		size = RTAS_GET_INDICES_BUF_SIZE;

	return simple_read_from_buffer(buf, size, off, blob->data, blob->len);
}

static const struct file_operations papr_indices_handle_ops = {
	.read = papr_indices_handle_read,
	.llseek = papr_rtas_common_handle_seek,
	.release = papr_rtas_common_handle_release,
};

/*
 * papr_indices_create_handle() - Create a fd-based handle for reading
 *                                indices data
 * @ubuf: Input parameters to RTAS call such as whether sensor or indicator
 *        and indice type in user memory
 *
 * Handler for PAPR_INDICES_IOC_GET ioctl command. Validates @ubuf
 * and instantiates an immutable indices "blob" for it. The blob is
 * attached to a file descriptor for reading by user space. The memory
 * backing the blob is freed when the file is released.
 *
 * The entire requested indices is retrieved by this call and all
 * necessary RTAS interactions are performed before returning the fd
 * to user space. This keeps the read handler simple and ensures that
 * the kernel can prevent interleaving of ibm,get-indices call sequences.
 *
 * Return: The installed fd number if successful, -ve errno otherwise.
 */
static long papr_indices_create_handle(struct papr_indices_io_block __user *ubuf)
{
	struct papr_rtas_sequence seq = {};
	struct rtas_get_indices_params params = {};
	int fd;

	if (get_user(params.is_sensor, &ubuf->indices.is_sensor))
		return -EFAULT;

	if (get_user(params.indice_type, &ubuf->indices.indice_type))
		return -EFAULT;

	seq = (struct papr_rtas_sequence) {
		.begin = indices_sequence_begin,
		.end = indices_sequence_end,
		.work = indices_sequence_fill_work_area,
	};

	seq.params = &params;
	fd = papr_rtas_setup_file_interface(&seq,
			&papr_indices_handle_ops, "[papr-indices]");

	return fd;
}

/*
 * Create work area with the input parameters. This function is used
 * for both ibm,set-dynamic-indicator and ibm,get-dynamic-sensor-state
 * RTAS Calls.
 */
static struct rtas_work_area *
papr_dynamic_indice_buf_from_user(struct papr_indices_io_block __user *ubuf,
				struct papr_indices_io_block *kbuf)
{
	struct rtas_work_area *work_area;
	u32 length;
	__be32 len_be;

	if (copy_from_user(kbuf, ubuf, sizeof(*kbuf)))
		return ERR_PTR(-EFAULT);


	if (!string_is_terminated(kbuf->dynamic_param.location_code_str,
			ARRAY_SIZE(kbuf->dynamic_param.location_code_str)))
		return ERR_PTR(-EINVAL);

	/*
	 * The input data in the work area should be as follows:
	 * - 32-bit integer length of the location code string,
	 *   including NULL.
	 * - Location code string, NULL terminated, identifying the
	 *   token (sensor or indicator).
	 * PAPR 2.13 - R1–7.3.18–5 ibm,set-dynamic-indicator
	 *           - R1–7.3.19–5 ibm,get-dynamic-sensor-state
	 */
	/*
	 * Length that user space passed should also include NULL
	 * terminator.
	 */
	length = strlen(kbuf->dynamic_param.location_code_str) + 1;
	if (length > LOC_CODE_SIZE)
		return ERR_PTR(-EINVAL);

	len_be = cpu_to_be32(length);

	work_area = rtas_work_area_alloc(LOC_CODE_SIZE + sizeof(u32));
	memcpy(rtas_work_area_raw_buf(work_area), &len_be, sizeof(u32));
	memcpy((rtas_work_area_raw_buf(work_area) + sizeof(u32)),
			&kbuf->dynamic_param.location_code_str, length);

	return work_area;
}

/**
 * papr_dynamic_indicator_ioc_set - ibm,set-dynamic-indicator RTAS Call
 * PAPR 2.13 7.3.18
 *
 * @ubuf: Input parameters to RTAS call such as indicator token and
 *        new state.
 *
 * Returns success or -errno.
 */
static long papr_dynamic_indicator_ioc_set(struct papr_indices_io_block __user *ubuf)
{
	struct papr_indices_io_block kbuf;
	struct rtas_work_area *work_area;
	s32 fwrc, token, ret;

	token = rtas_function_token(RTAS_FN_IBM_SET_DYNAMIC_INDICATOR);
	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	mutex_lock(&rtas_ibm_set_dynamic_indicator_lock);
	work_area = papr_dynamic_indice_buf_from_user(ubuf, &kbuf);
	if (IS_ERR(work_area)) {
		ret = PTR_ERR(work_area);
		goto out;
	}

	do {
		fwrc = rtas_call(token, 3, 1, NULL,
				kbuf.dynamic_param.token,
				kbuf.dynamic_param.state,
				rtas_work_area_phys(work_area));
	} while (rtas_busy_delay(fwrc));

	rtas_work_area_free(work_area);

	switch (fwrc) {
	case RTAS_SUCCESS:
		ret = 0;
		break;
	case RTAS_IBM_DYNAMIC_INDICE_NO_INDICATOR:	/* No such indicator */
		ret = -EOPNOTSUPP;
		break;
	default:
		pr_err("unexpected ibm,set-dynamic-indicator result %d\n",
			fwrc);
		fallthrough;
	case RTAS_HARDWARE_ERROR:	/* Hardware/platform error */
		ret = -EIO;
		break;
	}

out:
	mutex_unlock(&rtas_ibm_set_dynamic_indicator_lock);
	return ret;
}

/**
 * papr_dynamic_sensor_ioc_get - ibm,get-dynamic-sensor-state RTAS Call
 * PAPR 2.13 7.3.19
 *
 * @ubuf: Input parameters to RTAS call such as sensor token
 *        Copies the state in user space buffer.
 *
 *
 * Returns success or -errno.
 */

static long papr_dynamic_sensor_ioc_get(struct papr_indices_io_block __user *ubuf)
{
	struct papr_indices_io_block kbuf;
	struct rtas_work_area *work_area;
	s32 fwrc, token, ret;
	u32 rets;

	token = rtas_function_token(RTAS_FN_IBM_GET_DYNAMIC_SENSOR_STATE);
	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	mutex_lock(&rtas_ibm_get_dynamic_sensor_state_lock);
	work_area = papr_dynamic_indice_buf_from_user(ubuf, &kbuf);
	if (IS_ERR(work_area)) {
		ret = PTR_ERR(work_area);
		goto out;
	}

	do {
		fwrc = rtas_call(token, 2, 2, &rets,
				kbuf.dynamic_param.token,
				rtas_work_area_phys(work_area));
	} while (rtas_busy_delay(fwrc));

	rtas_work_area_free(work_area);

	switch (fwrc) {
	case RTAS_SUCCESS:
		if (put_user(rets, &ubuf->dynamic_param.state))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	case RTAS_IBM_DYNAMIC_INDICE_NO_INDICATOR:	/* No such indicator */
		ret = -EOPNOTSUPP;
		break;
	default:
		pr_err("unexpected ibm,get-dynamic-sensor result %d\n",
				fwrc);
		fallthrough;
	case RTAS_HARDWARE_ERROR:	/* Hardware/platform error */
		ret = -EIO;
		break;
	}

out:
	mutex_unlock(&rtas_ibm_get_dynamic_sensor_state_lock);
	return ret;
}

/*
 * Top-level ioctl handler for /dev/papr-indices.
 */
static long papr_indices_dev_ioctl(struct file *filp, unsigned int ioctl,
				unsigned long arg)
{
	void __user *argp = (__force void __user *)arg;
	long ret;

	switch (ioctl) {
	case PAPR_INDICES_IOC_GET:
		ret = papr_indices_create_handle(argp);
		break;
	case PAPR_DYNAMIC_SENSOR_IOC_GET:
		ret = papr_dynamic_sensor_ioc_get(argp);
		break;
	case PAPR_DYNAMIC_INDICATOR_IOC_SET:
		if (filp->f_mode & FMODE_WRITE)
			ret = papr_dynamic_indicator_ioc_set(argp);
		else
			ret = -EBADF;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static const struct file_operations papr_indices_ops = {
	.unlocked_ioctl = papr_indices_dev_ioctl,
};

static struct miscdevice papr_indices_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "papr-indices",
	.fops = &papr_indices_ops,
};

static __init int papr_indices_init(void)
{
	if (!rtas_function_implemented(RTAS_FN_IBM_GET_INDICES))
		return -ENODEV;

	if (!rtas_function_implemented(RTAS_FN_IBM_SET_DYNAMIC_INDICATOR))
		return -ENODEV;

	if (!rtas_function_implemented(RTAS_FN_IBM_GET_DYNAMIC_SENSOR_STATE))
		return -ENODEV;

	return misc_register(&papr_indices_dev);
}
machine_device_initcall(pseries, papr_indices_init);
