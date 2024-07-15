// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt)	"papr-sysparm: " fmt

#include <linux/anon_inodes.h>
#include <linux/bug.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/machdep.h>
#include <asm/papr-sysparm.h>
#include <asm/rtas-work-area.h>
#include <asm/rtas.h>

struct papr_sysparm_buf *papr_sysparm_buf_alloc(void)
{
	struct papr_sysparm_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	return buf;
}

void papr_sysparm_buf_free(struct papr_sysparm_buf *buf)
{
	kfree(buf);
}

static size_t papr_sysparm_buf_get_length(const struct papr_sysparm_buf *buf)
{
	return be16_to_cpu(buf->len);
}

static void papr_sysparm_buf_set_length(struct papr_sysparm_buf *buf, size_t length)
{
	WARN_ONCE(length > sizeof(buf->val),
		  "bogus length %zu, clamping to safe value", length);
	length = min(sizeof(buf->val), length);
	buf->len = cpu_to_be16(length);
}

/*
 * For use on buffers returned from ibm,get-system-parameter before
 * returning them to callers. Ensures the encoded length of valid data
 * cannot overrun buf->val[].
 */
static void papr_sysparm_buf_clamp_length(struct papr_sysparm_buf *buf)
{
	papr_sysparm_buf_set_length(buf, papr_sysparm_buf_get_length(buf));
}

/*
 * Perform some basic diligence on the system parameter buffer before
 * submitting it to RTAS.
 */
static bool papr_sysparm_buf_can_submit(const struct papr_sysparm_buf *buf)
{
	/*
	 * Firmware ought to reject buffer lengths that exceed the
	 * maximum specified in PAPR, but there's no reason for the
	 * kernel to allow them either.
	 */
	if (papr_sysparm_buf_get_length(buf) > sizeof(buf->val))
		return false;

	return true;
}

/**
 * papr_sysparm_get() - Retrieve the value of a PAPR system parameter.
 * @param: PAPR system parameter token as described in
 *         7.3.16 "System Parameters Option".
 * @buf: A &struct papr_sysparm_buf as returned from papr_sysparm_buf_alloc().
 *
 * Place the result of querying the specified parameter, if available,
 * in @buf. The result includes a be16 length header followed by the
 * value, which may be a string or binary data. See &struct papr_sysparm_buf.
 *
 * Since there is at least one parameter (60, OS Service Entitlement
 * Status) where the results depend on the incoming contents of the
 * work area, the caller-supplied buffer is copied unmodified into the
 * work area before calling ibm,get-system-parameter.
 *
 * A defined parameter may not be implemented on a given system, and
 * some implemented parameters may not be available to all partitions
 * on a system. A parameter's disposition may change at any time due
 * to system configuration changes or partition migration.
 *
 * Context: This function may sleep.
 *
 * Return: 0 on success, -errno otherwise. @buf is unmodified on error.
 */
int papr_sysparm_get(papr_sysparm_t param, struct papr_sysparm_buf *buf)
{
	const s32 token = rtas_function_token(RTAS_FN_IBM_GET_SYSTEM_PARAMETER);
	struct rtas_work_area *work_area;
	s32 fwrc;
	int ret;

	might_sleep();

	if (WARN_ON(!buf))
		return -EFAULT;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	if (!papr_sysparm_buf_can_submit(buf))
		return -EINVAL;

	work_area = rtas_work_area_alloc(sizeof(*buf));

	memcpy(rtas_work_area_raw_buf(work_area), buf, sizeof(*buf));

	do {
		fwrc = rtas_call(token, 3, 1, NULL, param.token,
				 rtas_work_area_phys(work_area),
				 rtas_work_area_size(work_area));
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case 0:
		ret = 0;
		memcpy(buf, rtas_work_area_raw_buf(work_area), sizeof(*buf));
		papr_sysparm_buf_clamp_length(buf);
		break;
	case -3: /* parameter not implemented */
		ret = -EOPNOTSUPP;
		break;
	case -9002: /* this partition not authorized to retrieve this parameter */
		ret = -EPERM;
		break;
	case -9999: /* "parameter error" e.g. the buffer is too small */
		ret = -EINVAL;
		break;
	default:
		pr_err("unexpected ibm,get-system-parameter result %d\n", fwrc);
		fallthrough;
	case -1: /* Hardware/platform error */
		ret = -EIO;
		break;
	}

	rtas_work_area_free(work_area);

	return ret;
}

int papr_sysparm_set(papr_sysparm_t param, const struct papr_sysparm_buf *buf)
{
	const s32 token = rtas_function_token(RTAS_FN_IBM_SET_SYSTEM_PARAMETER);
	struct rtas_work_area *work_area;
	s32 fwrc;
	int ret;

	might_sleep();

	if (WARN_ON(!buf))
		return -EFAULT;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	if (!papr_sysparm_buf_can_submit(buf))
		return -EINVAL;

	work_area = rtas_work_area_alloc(sizeof(*buf));

	memcpy(rtas_work_area_raw_buf(work_area), buf, sizeof(*buf));

	do {
		fwrc = rtas_call(token, 2, 1, NULL, param.token,
				 rtas_work_area_phys(work_area));
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case 0:
		ret = 0;
		break;
	case -3: /* parameter not supported */
		ret = -EOPNOTSUPP;
		break;
	case -9002: /* this partition not authorized to modify this parameter */
		ret = -EPERM;
		break;
	case -9999: /* "parameter error" e.g. invalid input data */
		ret = -EINVAL;
		break;
	default:
		pr_err("unexpected ibm,set-system-parameter result %d\n", fwrc);
		fallthrough;
	case -1: /* Hardware/platform error */
		ret = -EIO;
		break;
	}

	rtas_work_area_free(work_area);

	return ret;
}

static struct papr_sysparm_buf *
papr_sysparm_buf_from_user(const struct papr_sysparm_io_block __user *user_iob)
{
	struct papr_sysparm_buf *kern_spbuf;
	long err;
	u16 len;

	/*
	 * The length of valid data that userspace claims to be in
	 * user_iob->data[].
	 */
	if (get_user(len, &user_iob->length))
		return ERR_PTR(-EFAULT);

	static_assert(sizeof(user_iob->data) >= PAPR_SYSPARM_MAX_INPUT);
	static_assert(sizeof(kern_spbuf->val) >= PAPR_SYSPARM_MAX_INPUT);

	if (len > PAPR_SYSPARM_MAX_INPUT)
		return ERR_PTR(-EINVAL);

	kern_spbuf = papr_sysparm_buf_alloc();
	if (!kern_spbuf)
		return ERR_PTR(-ENOMEM);

	papr_sysparm_buf_set_length(kern_spbuf, len);

	if (len > 0 && copy_from_user(kern_spbuf->val, user_iob->data, len)) {
		err = -EFAULT;
		goto free_sysparm_buf;
	}

	return kern_spbuf;

free_sysparm_buf:
	papr_sysparm_buf_free(kern_spbuf);
	return ERR_PTR(err);
}

static int papr_sysparm_buf_to_user(const struct papr_sysparm_buf *kern_spbuf,
				    struct papr_sysparm_io_block __user *user_iob)
{
	u16 len_out = papr_sysparm_buf_get_length(kern_spbuf);

	if (put_user(len_out, &user_iob->length))
		return -EFAULT;

	static_assert(sizeof(user_iob->data) >= PAPR_SYSPARM_MAX_OUTPUT);
	static_assert(sizeof(kern_spbuf->val) >= PAPR_SYSPARM_MAX_OUTPUT);

	if (copy_to_user(user_iob->data, kern_spbuf->val, PAPR_SYSPARM_MAX_OUTPUT))
		return -EFAULT;

	return 0;
}

static long papr_sysparm_ioctl_get(struct papr_sysparm_io_block __user *user_iob)
{
	struct papr_sysparm_buf *kern_spbuf;
	papr_sysparm_t param;
	long ret;

	if (get_user(param.token, &user_iob->parameter))
		return -EFAULT;

	kern_spbuf = papr_sysparm_buf_from_user(user_iob);
	if (IS_ERR(kern_spbuf))
		return PTR_ERR(kern_spbuf);

	ret = papr_sysparm_get(param, kern_spbuf);
	if (ret)
		goto free_sysparm_buf;

	ret = papr_sysparm_buf_to_user(kern_spbuf, user_iob);
	if (ret)
		goto free_sysparm_buf;

	ret = 0;

free_sysparm_buf:
	papr_sysparm_buf_free(kern_spbuf);
	return ret;
}


static long papr_sysparm_ioctl_set(struct papr_sysparm_io_block __user *user_iob)
{
	struct papr_sysparm_buf *kern_spbuf;
	papr_sysparm_t param;
	long ret;

	if (get_user(param.token, &user_iob->parameter))
		return -EFAULT;

	kern_spbuf = papr_sysparm_buf_from_user(user_iob);
	if (IS_ERR(kern_spbuf))
		return PTR_ERR(kern_spbuf);

	ret = papr_sysparm_set(param, kern_spbuf);
	if (ret)
		goto free_sysparm_buf;

	ret = 0;

free_sysparm_buf:
	papr_sysparm_buf_free(kern_spbuf);
	return ret;
}

static long papr_sysparm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (__force void __user *)arg;
	long ret;

	switch (ioctl) {
	case PAPR_SYSPARM_IOC_GET:
		ret = papr_sysparm_ioctl_get(argp);
		break;
	case PAPR_SYSPARM_IOC_SET:
		if (filp->f_mode & FMODE_WRITE)
			ret = papr_sysparm_ioctl_set(argp);
		else
			ret = -EBADF;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static const struct file_operations papr_sysparm_ops = {
	.unlocked_ioctl = papr_sysparm_ioctl,
};

static struct miscdevice papr_sysparm_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "papr-sysparm",
	.fops = &papr_sysparm_ops,
};

static __init int papr_sysparm_init(void)
{
	if (!rtas_function_implemented(RTAS_FN_IBM_GET_SYSTEM_PARAMETER))
		return -ENODEV;

	return misc_register(&papr_sysparm_dev);
}
machine_device_initcall(pseries, papr_sysparm_init);
