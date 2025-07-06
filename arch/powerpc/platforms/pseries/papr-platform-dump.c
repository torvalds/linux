// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "papr-platform-dump: " fmt

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <asm/machdep.h>
#include <asm/rtas-work-area.h>
#include <asm/rtas.h>
#include <uapi/asm/papr-platform-dump.h>

/*
 * Function-specific return values for ibm,platform-dump, derived from
 * PAPR+ v2.13 7.3.3.4.1 "ibm,platform-dump RTAS Call".
 */
#define	RTAS_IBM_PLATFORM_DUMP_COMPLETE	0	/* Complete dump retrieved. */
#define	RTAS_IBM_PLATFORM_DUMP_CONTINUE	1	/* Continue dump */
#define	RTAS_NOT_AUTHORIZED		-9002	/* Not Authorized */

#define	RTAS_IBM_PLATFORM_DUMP_START	2 /* Linux status to start dump */

/**
 * struct ibm_platform_dump_params - Parameters (in and out) for
 *                                              ibm,platform-dump
 * @work_area:		In: work area buffer for results.
 * @buf_length:		In: work area buffer length in bytes
 * @dump_tag_hi:	In: Most-significant 32 bits of a Dump_Tag representing
 *                      an id of the dump being processed.
 * @dump_tag_lo:	In: Least-significant 32 bits of a Dump_Tag representing
 *                      an id of the dump being processed.
 * @sequence_hi:	In: Sequence number in most-significant 32 bits.
 *                      Out: Next sequence number in most-significant 32 bits.
 * @sequence_lo:	In: Sequence number in Least-significant 32 bits
 *                      Out: Next sequence number in Least-significant 32 bits.
 * @bytes_ret_hi:	Out: Bytes written in most-significant 32 bits.
 * @bytes_ret_lo:	Out: Bytes written in Least-significant 32 bits.
 * @status:		Out: RTAS call status.
 * @list:		Maintain the list of dumps are in progress. Can
 *                      retrieve multiple dumps with different dump IDs at
 *                      the same time but not with the same dump ID. This list
 *                      is used to determine whether the dump for the same ID
 *                      is in progress.
 */
struct ibm_platform_dump_params {
	struct rtas_work_area	*work_area;
	u32			buf_length;
	u32			dump_tag_hi;
	u32			dump_tag_lo;
	u32			sequence_hi;
	u32			sequence_lo;
	u32			bytes_ret_hi;
	u32			bytes_ret_lo;
	s32			status;
	struct list_head	list;
};

/*
 * Multiple dumps with different dump IDs can be retrieved at the same
 * time, but not with dame dump ID. platform_dump_list_mutex and
 * platform_dump_list are used to prevent this behavior.
 */
static DEFINE_MUTEX(platform_dump_list_mutex);
static LIST_HEAD(platform_dump_list);

/**
 * rtas_ibm_platform_dump() - Call ibm,platform-dump to fill a work area
 * buffer.
 * @params: See &struct ibm_platform_dump_params.
 * @buf_addr: Address of dump buffer (work_area)
 * @buf_length: Length of the buffer in bytes (min. 1024)
 *
 * Calls ibm,platform-dump until it errors or successfully deposits data
 * into the supplied work area. Handles RTAS retry statuses. Maps RTAS
 * error statuses to reasonable errno values.
 *
 * Can request multiple dumps with different dump IDs at the same time,
 * but not with the same dump ID which is prevented with the check in
 * the ioctl code (papr_platform_dump_create_handle()).
 *
 * The caller should inspect @params.status to determine whether more
 * calls are needed to complete the sequence.
 *
 * Context: May sleep.
 * Return: -ve on error, 0 for dump complete and 1 for continue dump
 */
static int rtas_ibm_platform_dump(struct ibm_platform_dump_params *params,
				phys_addr_t buf_addr, u32 buf_length)
{
	u32 rets[4];
	s32 fwrc;
	int ret = 0;

	do {
		fwrc = rtas_call(rtas_function_token(RTAS_FN_IBM_PLATFORM_DUMP),
				6, 5,
				rets,
				params->dump_tag_hi,
				params->dump_tag_lo,
				params->sequence_hi,
				params->sequence_lo,
				buf_addr,
				buf_length);
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case RTAS_HARDWARE_ERROR:
		ret = -EIO;
		break;
	case RTAS_NOT_AUTHORIZED:
		ret = -EPERM;
		break;
	case RTAS_IBM_PLATFORM_DUMP_CONTINUE:
	case RTAS_IBM_PLATFORM_DUMP_COMPLETE:
		params->sequence_hi = rets[0];
		params->sequence_lo = rets[1];
		params->bytes_ret_hi = rets[2];
		params->bytes_ret_lo = rets[3];
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("unexpected ibm,platform-dump status %d\n",
				fwrc);
		break;
	}

	params->status = fwrc;
	return ret;
}

/*
 * Platform dump is used with multiple RTAS calls to retrieve the
 * complete dump for the provided dump ID. Once the complete dump is
 * retrieved, the hypervisor returns dump complete status (0) for the
 * last RTAS call and expects the caller issues one more call with
 * NULL buffer to invalidate the dump so that the hypervisor can remove
 * the dump.
 *
 * After the specific dump is invalidated in the hypervisor, expect the
 * dump complete status for the new sequence - the user space initiates
 * new request for the same dump ID.
 */
static ssize_t papr_platform_dump_handle_read(struct file *file,
		char __user *buf, size_t size, loff_t *off)
{
	struct ibm_platform_dump_params *params = file->private_data;
	u64 total_bytes;
	s32 fwrc;

	/*
	 * Dump already completed with the previous read calls.
	 * In case if the user space issues further reads, returns
	 * -EINVAL.
	 */
	if (!params->buf_length) {
		pr_warn_once("Platform dump completed for dump ID %llu\n",
			(u64) (((u64)params->dump_tag_hi << 32) |
				params->dump_tag_lo));
		return -EINVAL;
	}

	/*
	 * The hypervisor returns status 0 if no more data available to
	 * download. The dump will be invalidated with ioctl (see below).
	 */
	if (params->status == RTAS_IBM_PLATFORM_DUMP_COMPLETE) {
		params->buf_length = 0;
		/*
		 * Returns 0 to the user space so that user
		 * space read stops.
		 */
		return 0;
	}

	if (size < SZ_1K) {
		pr_err_once("Buffer length should be minimum 1024 bytes\n");
		return -EINVAL;
	} else if (size > params->buf_length) {
		/*
		 * Allocate 4K work area. So if the user requests > 4K,
		 * resize the buffer length.
		 */
		size = params->buf_length;
	}

	fwrc = rtas_ibm_platform_dump(params,
			rtas_work_area_phys(params->work_area),
			size);
	if (fwrc < 0)
		return fwrc;

	total_bytes = (u64) (((u64)params->bytes_ret_hi << 32) |
			params->bytes_ret_lo);

	/*
	 * Kernel or firmware bug, do not continue.
	 */
	if (WARN(total_bytes > size, "possible write beyond end of work area"))
		return -EFAULT;

	if (copy_to_user(buf, rtas_work_area_raw_buf(params->work_area),
			total_bytes))
		return -EFAULT;

	return total_bytes;
}

static int papr_platform_dump_handle_release(struct inode *inode,
					struct file *file)
{
	struct ibm_platform_dump_params *params = file->private_data;

	if (params->work_area)
		rtas_work_area_free(params->work_area);

	mutex_lock(&platform_dump_list_mutex);
	list_del(&params->list);
	mutex_unlock(&platform_dump_list_mutex);

	kfree(params);
	file->private_data = NULL;
	return 0;
}

/*
 * This ioctl is used to invalidate the dump assuming the user space
 * issue this ioctl after obtain the complete dump.
 * Issue the last RTAS call with NULL buffer to invalidate the dump
 * which means dump will be freed in the hypervisor.
 */
static long papr_platform_dump_invalidate_ioctl(struct file *file,
				unsigned int ioctl, unsigned long arg)
{
	struct ibm_platform_dump_params *params;
	u64 __user *argp = (void __user *)arg;
	u64 param_dump_tag, dump_tag;

	if (ioctl != PAPR_PLATFORM_DUMP_IOC_INVALIDATE)
		return -ENOIOCTLCMD;

	if (get_user(dump_tag, argp))
		return -EFAULT;

	/*
	 * private_data is freeded during release(), so should not
	 * happen.
	 */
	if (!file->private_data) {
		pr_err("No valid FD to invalidate dump for the ID(%llu)\n",
				dump_tag);
		return -EINVAL;
	}

	params = file->private_data;
	param_dump_tag = (u64) (((u64)params->dump_tag_hi << 32) |
				params->dump_tag_lo);
	if (dump_tag != param_dump_tag) {
		pr_err("Invalid dump ID(%llu) to invalidate dump\n",
				dump_tag);
		return -EINVAL;
	}

	if (params->status != RTAS_IBM_PLATFORM_DUMP_COMPLETE) {
		pr_err("Platform dump is not complete, but requested "
			"to invalidate dump for ID(%llu)\n",
			dump_tag);
		return -EINPROGRESS;
	}

	return rtas_ibm_platform_dump(params, 0, 0);
}

static const struct file_operations papr_platform_dump_handle_ops = {
	.read = papr_platform_dump_handle_read,
	.release = papr_platform_dump_handle_release,
	.unlocked_ioctl	= papr_platform_dump_invalidate_ioctl,
};

/**
 * papr_platform_dump_create_handle() - Create a fd-based handle for
 * reading platform dump
 *
 * Handler for PAPR_PLATFORM_DUMP_IOC_CREATE_HANDLE ioctl command
 * Allocates RTAS parameter struct and work area and attached to the
 * file descriptor for reading by user space with the multiple RTAS
 * calls until the dump is completed. This memory allocation is freed
 * when the file is released.
 *
 * Multiple dump requests with different IDs are allowed at the same
 * time, but not with the same dump ID. So if the user space is
 * already opened file descriptor for the specific dump ID, return
 * -EALREADY for the next request.
 *
 * @dump_tag: Dump ID for the dump requested to retrieve from the
 *		hypervisor
 *
 * Return: The installed fd number if successful, -ve errno otherwise.
 */
static long papr_platform_dump_create_handle(u64 dump_tag)
{
	struct ibm_platform_dump_params *params;
	u64 param_dump_tag;
	struct file *file;
	long err;
	int fd;

	/*
	 * Return failure if the user space is already opened FD for
	 * the specific dump ID. This check will prevent multiple dump
	 * requests for the same dump ID at the same time. Generally
	 * should not expect this, but in case.
	 */
	list_for_each_entry(params, &platform_dump_list, list) {
		param_dump_tag = (u64) (((u64)params->dump_tag_hi << 32) |
					params->dump_tag_lo);
		if (dump_tag == param_dump_tag) {
			pr_err("Platform dump for ID(%llu) is already in progress\n",
					dump_tag);
			return -EALREADY;
		}
	}

	params =  kzalloc(sizeof(struct ibm_platform_dump_params),
			GFP_KERNEL_ACCOUNT);
	if (!params)
		return -ENOMEM;

	params->work_area = rtas_work_area_alloc(SZ_4K);
	params->buf_length = SZ_4K;
	params->dump_tag_hi = (u32)(dump_tag >> 32);
	params->dump_tag_lo = (u32)(dump_tag & 0x00000000ffffffffULL);
	params->status = RTAS_IBM_PLATFORM_DUMP_START;

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto free_area;
	}

	file = anon_inode_getfile_fmode("[papr-platform-dump]",
				&papr_platform_dump_handle_ops,
				(void *)params, O_RDONLY,
				FMODE_LSEEK | FMODE_PREAD);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto put_fd;
	}

	fd_install(fd, file);

	list_add(&params->list, &platform_dump_list);

	pr_info("%s (%d) initiated platform dump for dump tag %llu\n",
		current->comm, current->pid, dump_tag);
	return fd;
put_fd:
	put_unused_fd(fd);
free_area:
	rtas_work_area_free(params->work_area);
	kfree(params);
	return err;
}

/*
 * Top-level ioctl handler for /dev/papr-platform-dump.
 */
static long papr_platform_dump_dev_ioctl(struct file *filp,
					unsigned int ioctl,
					unsigned long arg)
{
	u64 __user *argp = (void __user *)arg;
	u64 dump_tag;
	long ret;

	if (get_user(dump_tag, argp))
		return -EFAULT;

	switch (ioctl) {
	case PAPR_PLATFORM_DUMP_IOC_CREATE_HANDLE:
		mutex_lock(&platform_dump_list_mutex);
		ret = papr_platform_dump_create_handle(dump_tag);
		mutex_unlock(&platform_dump_list_mutex);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static const struct file_operations papr_platform_dump_ops = {
	.unlocked_ioctl = papr_platform_dump_dev_ioctl,
};

static struct miscdevice papr_platform_dump_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "papr-platform-dump",
	.fops = &papr_platform_dump_ops,
};

static __init int papr_platform_dump_init(void)
{
	if (!rtas_function_implemented(RTAS_FN_IBM_PLATFORM_DUMP))
		return -ENODEV;

	return misc_register(&papr_platform_dump_dev);
}
machine_device_initcall(pseries, papr_platform_dump_init);
