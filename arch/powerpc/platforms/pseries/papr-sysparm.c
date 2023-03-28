// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt)	"papr-sysparm: " fmt

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <asm/rtas.h>
#include <asm/papr-sysparm.h>
#include <asm/rtas-work-area.h>

struct papr_sysparm_buf *papr_sysparm_buf_alloc(void)
{
	struct papr_sysparm_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	return buf;
}

void papr_sysparm_buf_free(struct papr_sysparm_buf *buf)
{
	kfree(buf);
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
