// SPDX-License-Identifier: GPL-2.0
/*
 * Request power readings for resources in a computing environment via
 * diag 0x324. diag 0x324 stores the power readings in the power information
 * block (pib).
 *
 * Copyright IBM Corp. 2024
 */

#define pr_fmt(fmt)	"diag324: " fmt
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/ioctl.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/diag.h>
#include <asm/sclp.h>
#include <asm/timex.h>
#include <uapi/asm/diag.h>
#include "diag_ioctl.h"

enum subcode {
	DIAG324_SUBC_0 = 0,
	DIAG324_SUBC_1 = 1,
	DIAG324_SUBC_2 = 2,
};

enum retcode {
	DIAG324_RET_SUCCESS		= 0x0001,
	DIAG324_RET_SUBC_NOTAVAIL	= 0x0103,
	DIAG324_RET_INSUFFICIENT_SIZE	= 0x0104,
	DIAG324_RET_READING_UNAVAILABLE	= 0x0105,
};

union diag324_response {
	u64 response;
	struct {
		u64 installed	: 32;
		u64		: 16;
		u64 rc		: 16;
	} sc0;
	struct {
		u64 format	: 16;
		u64		: 16;
		u64 pib_len	: 16;
		u64 rc		: 16;
	} sc1;
	struct {
		u64		: 48;
		u64 rc		: 16;
	} sc2;
};

union diag324_request {
	u64 request;
	struct {
		u64		: 32;
		u64 allocated	: 16;
		u64		: 12;
		u64 sc		: 4;
	} sc2;
};

struct pib {
	u32		: 8;
	u32 num		: 8;
	u32 len		: 16;
	u32		: 24;
	u32 hlen	: 8;
	u64		: 64;
	u64 intv;
	u8  r[];
} __packed;

struct pibdata {
	struct pib *pib;
	ktime_t expire;
	u64 sequence;
	size_t len;
	int rc;
};

static DEFINE_MUTEX(pibmutex);
static struct pibdata pibdata;

#define PIBWORK_DELAY (5 * NSEC_PER_SEC)

static void pibwork_handler(struct work_struct *work);
static DECLARE_DELAYED_WORK(pibwork, pibwork_handler);

static unsigned long diag324(unsigned long subcode, void *addr)
{
	union register_pair rp = { .even = (unsigned long)addr };

	diag_stat_inc(DIAG_STAT_X324);
	asm volatile("diag	%[rp],%[subcode],0x324"
		     : [rp] "+d" (rp.pair)
		     : [subcode] "d" (subcode)
		     : "memory");
	return rp.odd;
}

static void pibwork_handler(struct work_struct *work)
{
	struct pibdata *data = &pibdata;
	ktime_t timedout;

	mutex_lock(&pibmutex);
	timedout = ktime_add_ns(data->expire, PIBWORK_DELAY);
	if (ktime_before(ktime_get(), timedout)) {
		mod_delayed_work(system_percpu_wq, &pibwork, nsecs_to_jiffies(PIBWORK_DELAY));
		goto out;
	}
	vfree(data->pib);
	data->pib = NULL;
out:
	mutex_unlock(&pibmutex);
}

static void pib_update(struct pibdata *data)
{
	union diag324_request req = { .sc2.sc = DIAG324_SUBC_2, .sc2.allocated = data->len };
	union diag324_response res;
	int rc;

	memset(data->pib, 0, data->len);
	res.response = diag324(req.request, data->pib);
	switch (res.sc2.rc) {
	case DIAG324_RET_SUCCESS:
		rc = 0;
		break;
	case DIAG324_RET_SUBC_NOTAVAIL:
		rc = -ENOENT;
		break;
	case DIAG324_RET_INSUFFICIENT_SIZE:
		rc = -EMSGSIZE;
		break;
	case DIAG324_RET_READING_UNAVAILABLE:
		rc = -EBUSY;
		break;
	default:
		rc = -EINVAL;
	}
	data->rc = rc;
}

long diag324_pibbuf(unsigned long arg)
{
	struct diag324_pib __user *udata = (struct diag324_pib __user *)arg;
	struct pibdata *data = &pibdata;
	static bool first = true;
	u64 address;
	int rc;

	if (!data->len)
		return -EOPNOTSUPP;
	if (get_user(address, &udata->address))
		return -EFAULT;
	mutex_lock(&pibmutex);
	rc = -ENOMEM;
	if (!data->pib)
		data->pib = vmalloc(data->len);
	if (!data->pib)
		goto out;
	if (first || ktime_after(ktime_get(), data->expire)) {
		pib_update(data);
		data->sequence++;
		data->expire = ktime_add_ns(ktime_get(), tod_to_ns(data->pib->intv));
		mod_delayed_work(system_percpu_wq, &pibwork, nsecs_to_jiffies(PIBWORK_DELAY));
		first = false;
	}
	rc = data->rc;
	if (rc != 0 && rc != -EBUSY)
		goto out;
	rc = copy_to_user((void __user *)address, data->pib, data->pib->len);
	rc |= put_user(data->sequence, &udata->sequence);
	if (rc)
		rc = -EFAULT;
out:
	mutex_unlock(&pibmutex);
	return rc;
}

long diag324_piblen(unsigned long arg)
{
	struct pibdata *data = &pibdata;

	if (!data->len)
		return -EOPNOTSUPP;
	if (put_user(data->len, (size_t __user *)arg))
		return -EFAULT;
	return 0;
}

static int __init diag324_init(void)
{
	union diag324_response res;
	unsigned long installed;

	if (!sclp.has_diag324)
		return -EOPNOTSUPP;
	res.response = diag324(DIAG324_SUBC_0, NULL);
	if (res.sc0.rc != DIAG324_RET_SUCCESS)
		return -EOPNOTSUPP;
	installed = res.response;
	if (!test_bit_inv(DIAG324_SUBC_1, &installed))
		return -EOPNOTSUPP;
	if (!test_bit_inv(DIAG324_SUBC_2, &installed))
		return -EOPNOTSUPP;
	res.response = diag324(DIAG324_SUBC_1, NULL);
	if (res.sc1.rc != DIAG324_RET_SUCCESS)
		return -EOPNOTSUPP;
	pibdata.len = res.sc1.pib_len;
	return 0;
}
device_initcall(diag324_init);
