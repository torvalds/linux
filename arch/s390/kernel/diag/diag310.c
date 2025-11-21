// SPDX-License-Identifier: GPL-2.0
/*
 * Request memory topology information via diag0x310.
 *
 * Copyright IBM Corp. 2025
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <asm/diag.h>
#include <asm/sclp.h>
#include <uapi/asm/diag.h>
#include "diag_ioctl.h"

#define DIAG310_LEVELMIN 1
#define DIAG310_LEVELMAX 6

enum diag310_sc {
	DIAG310_SUBC_0 = 0,
	DIAG310_SUBC_1 = 1,
	DIAG310_SUBC_4 = 4,
	DIAG310_SUBC_5 = 5
};

enum diag310_retcode {
	DIAG310_RET_SUCCESS	= 0x0001,
	DIAG310_RET_BUSY	= 0x0101,
	DIAG310_RET_OPNOTSUPP	= 0x0102,
	DIAG310_RET_SC4_INVAL	= 0x0401,
	DIAG310_RET_SC4_NODATA	= 0x0402,
	DIAG310_RET_SC5_INVAL	= 0x0501,
	DIAG310_RET_SC5_NODATA	= 0x0502,
	DIAG310_RET_SC5_ESIZE	= 0x0503
};

union diag310_response {
	u64 response;
	struct {
		u64 result	: 32;
		u64		: 16;
		u64 rc		: 16;
	};
};

union diag310_req_subcode {
	u64 subcode;
	struct {
		u64		: 48;
		u64 st		: 8;
		u64 sc		: 8;
	};
};

union diag310_req_size {
	u64 size;
	struct {
		u64 page_count	: 32;
		u64		: 32;
	};
};

static inline unsigned long diag310(unsigned long subcode, unsigned long size, void *addr)
{
	union register_pair rp = { .even = (unsigned long)addr, .odd = size };

	diag_stat_inc(DIAG_STAT_X310);
	asm volatile("diag	%[rp],%[subcode],0x310"
		     : [rp] "+d" (rp.pair)
		     : [subcode] "d" (subcode)
		     : "memory");
	return rp.odd;
}

static int diag310_result_to_errno(unsigned int result)
{
	switch (result) {
	case DIAG310_RET_BUSY:
		return -EBUSY;
	case DIAG310_RET_OPNOTSUPP:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int diag310_get_subcode_mask(unsigned long *mask)
{
	union diag310_response res;

	res.response = diag310(DIAG310_SUBC_0, 0, NULL);
	if (res.rc != DIAG310_RET_SUCCESS)
		return diag310_result_to_errno(res.rc);
	*mask = res.response;
	return 0;
}

static int diag310_get_memtop_stride(unsigned long *stride)
{
	union diag310_response res;

	res.response = diag310(DIAG310_SUBC_1, 0, NULL);
	if (res.rc != DIAG310_RET_SUCCESS)
		return diag310_result_to_errno(res.rc);
	*stride = res.result;
	return 0;
}

static int diag310_get_memtop_size(unsigned long *pages, unsigned long level)
{
	union diag310_req_subcode req = { .sc = DIAG310_SUBC_4, .st = level };
	union diag310_response res;

	res.response = diag310(req.subcode, 0, NULL);
	switch (res.rc) {
	case DIAG310_RET_SUCCESS:
		*pages = res.result;
		return 0;
	case DIAG310_RET_SC4_NODATA:
		return -ENODATA;
	case DIAG310_RET_SC4_INVAL:
		return -EINVAL;
	default:
		return diag310_result_to_errno(res.rc);
	}
}

static int diag310_store_topology_map(void *buf, unsigned long pages, unsigned long level)
{
	union diag310_req_subcode req_sc = { .sc = DIAG310_SUBC_5, .st = level };
	union diag310_req_size req_size = { .page_count = pages };
	union diag310_response res;

	res.response = diag310(req_sc.subcode, req_size.size, buf);
	switch (res.rc) {
	case DIAG310_RET_SUCCESS:
		return 0;
	case DIAG310_RET_SC5_NODATA:
		return -ENODATA;
	case DIAG310_RET_SC5_ESIZE:
		return -EOVERFLOW;
	case DIAG310_RET_SC5_INVAL:
		return -EINVAL;
	default:
		return diag310_result_to_errno(res.rc);
	}
}

static int diag310_check_features(void)
{
	static int features_available;
	unsigned long mask;
	int rc;

	if (READ_ONCE(features_available))
		return 0;
	if (!sclp.has_diag310)
		return -EOPNOTSUPP;
	rc = diag310_get_subcode_mask(&mask);
	if (rc)
		return rc;
	if (!test_bit_inv(DIAG310_SUBC_1, &mask))
		return -EOPNOTSUPP;
	if (!test_bit_inv(DIAG310_SUBC_4, &mask))
		return -EOPNOTSUPP;
	if (!test_bit_inv(DIAG310_SUBC_5, &mask))
		return -EOPNOTSUPP;
	WRITE_ONCE(features_available, 1);
	return 0;
}

static int memtop_get_stride_len(unsigned long *res)
{
	static unsigned long memtop_stride;
	unsigned long stride;
	int rc;

	stride = READ_ONCE(memtop_stride);
	if (!stride) {
		rc = diag310_get_memtop_stride(&stride);
		if (rc)
			return rc;
		WRITE_ONCE(memtop_stride, stride);
	}
	*res = stride;
	return 0;
}

static int memtop_get_page_count(unsigned long *res, unsigned long level)
{
	static unsigned long memtop_pages[DIAG310_LEVELMAX];
	unsigned long pages;
	int rc;

	if (level > DIAG310_LEVELMAX || level < DIAG310_LEVELMIN)
		return -EINVAL;
	pages = READ_ONCE(memtop_pages[level - 1]);
	if (!pages) {
		rc = diag310_get_memtop_size(&pages, level);
		if (rc)
			return rc;
		WRITE_ONCE(memtop_pages[level - 1], pages);
	}
	*res = pages;
	return 0;
}

long diag310_memtop_stride(unsigned long arg)
{
	size_t __user *argp = (void __user *)arg;
	unsigned long stride;
	int rc;

	rc = diag310_check_features();
	if (rc)
		return rc;
	rc = memtop_get_stride_len(&stride);
	if (rc)
		return rc;
	if (put_user(stride, argp))
		return -EFAULT;
	return 0;
}

long diag310_memtop_len(unsigned long arg)
{
	size_t __user *argp = (void __user *)arg;
	unsigned long pages, level;
	int rc;

	rc = diag310_check_features();
	if (rc)
		return rc;
	if (get_user(level, argp))
		return -EFAULT;
	rc = memtop_get_page_count(&pages, level);
	if (rc)
		return rc;
	if (put_user(pages * PAGE_SIZE, argp))
		return -EFAULT;
	return 0;
}

long diag310_memtop_buf(unsigned long arg)
{
	struct diag310_memtop __user *udata = (struct diag310_memtop __user *)arg;
	unsigned long level, pages, data_size;
	u64 address;
	void *buf;
	int rc;

	rc = diag310_check_features();
	if (rc)
		return rc;
	if (get_user(level, &udata->nesting_lvl))
		return -EFAULT;
	if (get_user(address, &udata->address))
		return -EFAULT;
	rc = memtop_get_page_count(&pages, level);
	if (rc)
		return rc;
	data_size = pages * PAGE_SIZE;
	buf = __vmalloc_node(data_size, PAGE_SIZE, GFP_KERNEL | __GFP_ZERO | __GFP_ACCOUNT,
			     NUMA_NO_NODE, __builtin_return_address(0));
	if (!buf)
		return -ENOMEM;
	rc = diag310_store_topology_map(buf, pages, level);
	if (rc)
		goto out;
	if (copy_to_user((void __user *)address, buf, data_size))
		rc = -EFAULT;
out:
	vfree(buf);
	return rc;
}
