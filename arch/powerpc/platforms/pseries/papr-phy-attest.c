// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "papr-phy-attest: " fmt

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
#include <uapi/asm/papr-physical-attestation.h>
#include "papr-rtas-common.h"

/**
 * struct rtas_phy_attest_params - Parameters (in and out) for
 * ibm,physical-attestation.
 *
 * @cmd:  In: Caller-provided attestation command buffer. Must be
 *        RTAS-addressable.
 * @work_area: In: Caller-provided work area buffer for attestation
 *             command structure
 *             Out: Caller-provided work area buffer for the response
 * @cmd_len:   In: Caller-provided attestation command structure
 *             length
 * @sequence:  In: Sequence number. Out: Next sequence number.
 * @written:   Out: Bytes written by ibm,physical-attestation to
 *             @work_area.
 * @status:    Out: RTAS call status.
 */
struct rtas_phy_attest_params {
	struct papr_phy_attest_io_block cmd;
	struct rtas_work_area *work_area;
	u32 cmd_len;
	u32 sequence;
	u32 written;
	s32 status;
};

/**
 * rtas_physical_attestation() - Call ibm,physical-attestation to
 * fill a work area buffer.
 * @params: See &struct rtas_phy_attest_params.
 *
 * Calls ibm,physical-attestation until it errors or successfully
 * deposits data into the supplied work area. Handles RTAS retry
 * statuses. Maps RTAS error statuses to reasonable errno values.
 *
 * The caller is expected to invoke rtas_physical_attestation()
 * multiple times to retrieve all the data for the provided
 * attestation command. Only one sequence should be in progress at
 * any time; starting a new sequence will disrupt any sequence
 * already in progress. Serialization of attestation retrieval
 * sequences is the responsibility of the caller.
 *
 * The caller should inspect @params.status to determine whether more
 * calls are needed to complete the sequence.
 *
 * Context: May sleep.
 * Return: -ve on error, 0 otherwise.
 */
static int rtas_physical_attestation(struct rtas_phy_attest_params *params)
{
	struct rtas_work_area *work_area;
	s32 fwrc, token;
	u32 rets[2];
	int ret;

	work_area = params->work_area;
	token = rtas_function_token(RTAS_FN_IBM_PHYSICAL_ATTESTATION);
	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	lockdep_assert_held(&rtas_ibm_physical_attestation_lock);

	do {
		fwrc = rtas_call(token, 3, 3, rets,
				 rtas_work_area_phys(work_area),
				 params->cmd_len,
				 params->sequence);
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case RTAS_HARDWARE_ERROR:
		ret = -EIO;
		break;
	case RTAS_INVALID_PARAMETER:
		ret = -EINVAL;
		break;
	case RTAS_SEQ_MORE_DATA:
		params->sequence = rets[0];
		fallthrough;
	case RTAS_SEQ_COMPLETE:
		params->written = rets[1];
		/*
		 * Kernel or firmware bug, do not continue.
		 */
		if (WARN(params->written > rtas_work_area_size(work_area),
			 "possible write beyond end of work area"))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("unexpected ibm,get-phy_attest status %d\n", fwrc);
		break;
	}

	params->status = fwrc;
	return ret;
}

/*
 * Internal physical-attestation sequence APIs. A physical-attestation
 * sequence is a series of calls to get ibm,physical-attestation
 * for a given attestation command. The sequence ends when an error
 * is encountered or all data for the attestation command has been
 * returned.
 */

/**
 * phy_attest_sequence_begin() - Begin a response data for attestation
 * command retrieval sequence.
 * @seq: user specified parameters for RTAS call from seq struct.
 *
 * Context: May sleep.
 */
static void phy_attest_sequence_begin(struct papr_rtas_sequence *seq)
{
	struct rtas_phy_attest_params *param;

	/*
	 * We could allocate the work area before acquiring the
	 * function lock, but that would allow concurrent requests to
	 * exhaust the limited work area pool for no benefit. So
	 * allocate the work area under the lock.
	 */
	mutex_lock(&rtas_ibm_physical_attestation_lock);
	param =  (struct rtas_phy_attest_params *)seq->params;
	param->work_area = rtas_work_area_alloc(SZ_4K);
	memcpy(rtas_work_area_raw_buf(param->work_area), &param->cmd,
			param->cmd_len);
	param->sequence = 1;
	param->status = 0;
}

/**
 * phy_attest_sequence_end() - Finalize a attestation command
 * response retrieval sequence.
 * @seq: Sequence state.
 *
 * Releases resources obtained by phy_attest_sequence_begin().
 */
static void phy_attest_sequence_end(struct papr_rtas_sequence *seq)
{
	struct rtas_phy_attest_params *param;

	param =  (struct rtas_phy_attest_params *)seq->params;
	rtas_work_area_free(param->work_area);
	mutex_unlock(&rtas_ibm_physical_attestation_lock);
	kfree(param);
}

/*
 * Generator function to be passed to papr_rtas_blob_generate().
 */
static const char *phy_attest_sequence_fill_work_area(struct papr_rtas_sequence *seq,
						size_t *len)
{
	struct rtas_phy_attest_params *p;
	bool init_state;

	p = (struct rtas_phy_attest_params *)seq->params;
	init_state = (p->written == 0) ? true : false;

	if (papr_rtas_sequence_should_stop(seq, p->status, init_state))
		return NULL;
	if (papr_rtas_sequence_set_err(seq, rtas_physical_attestation(p)))
		return NULL;
	*len = p->written;
	return rtas_work_area_raw_buf(p->work_area);
}

static const struct file_operations papr_phy_attest_handle_ops = {
	.read = papr_rtas_common_handle_read,
	.llseek = papr_rtas_common_handle_seek,
	.release = papr_rtas_common_handle_release,
};

/**
 * papr_phy_attest_create_handle() - Create a fd-based handle for
 * reading the response for the given attestation command.
 * @ulc: Attestation command in user memory; defines the scope of
 *       data for the attestation command to retrieve.
 *
 * Handler for PAPR_PHYSICAL_ATTESTATION_IOC_CREATE_HANDLE ioctl
 * command. Validates @ulc and instantiates an immutable response
 * "blob" for attestation command. The blob is attached to a file
 * descriptor for reading by user space. The memory backing the blob
 * is freed when the file is released.
 *
 * The entire requested response buffer for the attestation command
 * retrieved by this call and all necessary RTAS interactions are
 * performed before returning the fd to user space. This keeps the
 * read handler simple and ensures that kernel can prevent
 * interleaving ibm,physical-attestation call sequences.
 *
 * Return: The installed fd number if successful, -ve errno otherwise.
 */
static long papr_phy_attest_create_handle(struct papr_phy_attest_io_block __user *ulc)
{
	struct rtas_phy_attest_params *params;
	struct papr_rtas_sequence seq = {};
	int fd;

	/*
	 * Freed in phy_attest_sequence_end().
	 */
	params =  kzalloc(sizeof(*params), GFP_KERNEL_ACCOUNT);
	if (!params)
		return -ENOMEM;

	if (copy_from_user(&params->cmd, ulc,
			sizeof(struct papr_phy_attest_io_block)))
		return -EFAULT;

	params->cmd_len = be32_to_cpu(params->cmd.length);
	seq = (struct papr_rtas_sequence) {
		.begin = phy_attest_sequence_begin,
		.end = phy_attest_sequence_end,
		.work = phy_attest_sequence_fill_work_area,
	};

	seq.params = (void *)params;

	fd = papr_rtas_setup_file_interface(&seq,
			&papr_phy_attest_handle_ops,
			"[papr-physical-attestation]");

	return fd;
}

/*
 * Top-level ioctl handler for /dev/papr-physical-attestation.
 */
static long papr_phy_attest_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (__force void __user *)arg;
	long ret;

	switch (ioctl) {
	case PAPR_PHY_ATTEST_IOC_HANDLE:
		ret = papr_phy_attest_create_handle(argp);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static const struct file_operations papr_phy_attest_ops = {
	.unlocked_ioctl = papr_phy_attest_dev_ioctl,
};

static struct miscdevice papr_phy_attest_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "papr-physical-attestation",
	.fops = &papr_phy_attest_ops,
};

static __init int papr_phy_attest_init(void)
{
	if (!rtas_function_implemented(RTAS_FN_IBM_PHYSICAL_ATTESTATION))
		return -ENODEV;

	return misc_register(&papr_phy_attest_dev);
}
machine_device_initcall(pseries, papr_phy_attest_init);
