// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/unzip_vle_lz4.c
 *
 * Copyright (C) 2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "unzip_vle.h"

#if Z_EROFS_CLUSTER_MAX_PAGES > Z_EROFS_VLE_INLINE_PAGEVECS
#define EROFS_PERCPU_NR_PAGES   Z_EROFS_CLUSTER_MAX_PAGES
#else
#define EROFS_PERCPU_NR_PAGES   Z_EROFS_VLE_INLINE_PAGEVECS
#endif

static struct {
	char data[PAGE_SIZE * EROFS_PERCPU_NR_PAGES];
} erofs_pcpubuf[NR_CPUS];

int z_erofs_vle_plain_copy(struct page **compressed_pages,
			   unsigned clusterpages,
			   struct page **pages,
			   unsigned nr_pages,
			   unsigned short pageofs)
{
	unsigned i, j;
	void *src = NULL;
	const unsigned righthalf = PAGE_SIZE - pageofs;
	char *percpu_data;
	bool mirrored[Z_EROFS_CLUSTER_MAX_PAGES] = { 0 };

	preempt_disable();
	percpu_data = erofs_pcpubuf[smp_processor_id()].data;

	j = 0;
	for (i = 0; i < nr_pages; j = i++) {
		struct page *page = pages[i];
		void *dst;

		if (page == NULL) {
			if (src != NULL) {
				if (!mirrored[j])
					kunmap_atomic(src);
				src = NULL;
			}
			continue;
		}

		dst = kmap_atomic(page);

		for (; j < clusterpages; ++j) {
			if (compressed_pages[j] != page)
				continue;

			DBG_BUGON(mirrored[j]);
			memcpy(percpu_data + j * PAGE_SIZE, dst, PAGE_SIZE);
			mirrored[j] = true;
			break;
		}

		if (i) {
			if (src == NULL)
				src = mirrored[i-1] ?
					percpu_data + (i-1) * PAGE_SIZE :
					kmap_atomic(compressed_pages[i-1]);

			memcpy(dst, src + righthalf, pageofs);

			if (!mirrored[i-1])
				kunmap_atomic(src);

			if (unlikely(i >= clusterpages)) {
				kunmap_atomic(dst);
				break;
			}
		}

		if (!righthalf)
			src = NULL;
		else {
			src = mirrored[i] ? percpu_data + i * PAGE_SIZE :
				kmap_atomic(compressed_pages[i]);

			memcpy(dst + pageofs, src, righthalf);
		}

		kunmap_atomic(dst);
	}

	if (src != NULL && !mirrored[j])
		kunmap_atomic(src);

	preempt_enable();
	return 0;
}

extern int z_erofs_unzip_lz4(void *in, void *out, size_t inlen, size_t outlen);

int z_erofs_vle_unzip_fast_percpu(struct page **compressed_pages,
				  unsigned clusterpages,
				  struct page **pages,
				  unsigned outlen,
				  unsigned short pageofs,
				  void (*endio)(struct page *))
{
	void *vin, *vout;
	unsigned nr_pages, i, j;
	int ret;

	if (outlen + pageofs > EROFS_PERCPU_NR_PAGES * PAGE_SIZE)
		return -ENOTSUPP;

	nr_pages = DIV_ROUND_UP(outlen + pageofs, PAGE_SIZE);

	if (clusterpages == 1)
		vin = kmap_atomic(compressed_pages[0]);
	else
		vin = erofs_vmap(compressed_pages, clusterpages);

	preempt_disable();
	vout = erofs_pcpubuf[smp_processor_id()].data;

	ret = z_erofs_unzip_lz4(vin, vout + pageofs,
		clusterpages * PAGE_SIZE, outlen);

	if (ret >= 0) {
		outlen = ret;
		ret = 0;
	}

	for (i = 0; i < nr_pages; ++i) {
		j = min((unsigned)PAGE_SIZE - pageofs, outlen);

		if (pages[i] != NULL) {
			if (ret < 0)
				SetPageError(pages[i]);
			else if (clusterpages == 1 && pages[i] == compressed_pages[0])
				memcpy(vin + pageofs, vout + pageofs, j);
			else {
				void *dst = kmap_atomic(pages[i]);

				memcpy(dst + pageofs, vout + pageofs, j);
				kunmap_atomic(dst);
			}
			endio(pages[i]);
		}
		vout += PAGE_SIZE;
		outlen -= j;
		pageofs = 0;
	}
	preempt_enable();

	if (clusterpages == 1)
		kunmap_atomic(vin);
	else
		erofs_vunmap(vin, clusterpages);

	return ret;
}

int z_erofs_vle_unzip_vmap(struct page **compressed_pages,
			   unsigned clusterpages,
			   void *vout,
			   unsigned llen,
			   unsigned short pageofs,
			   bool overlapped)
{
	void *vin;
	unsigned i;
	int ret;

	if (overlapped) {
		preempt_disable();
		vin = erofs_pcpubuf[smp_processor_id()].data;

		for (i = 0; i < clusterpages; ++i) {
			void *t = kmap_atomic(compressed_pages[i]);

			memcpy(vin + PAGE_SIZE *i, t, PAGE_SIZE);
			kunmap_atomic(t);
		}
	} else if (clusterpages == 1)
		vin = kmap_atomic(compressed_pages[0]);
	else {
		vin = erofs_vmap(compressed_pages, clusterpages);
	}

	ret = z_erofs_unzip_lz4(vin, vout + pageofs,
		clusterpages * PAGE_SIZE, llen);
	if (ret > 0)
		ret = 0;

	if (!overlapped) {
		if (clusterpages == 1)
			kunmap_atomic(vin);
		else {
			erofs_vunmap(vin, clusterpages);
		}
	} else
		preempt_enable();

	return ret;
}

