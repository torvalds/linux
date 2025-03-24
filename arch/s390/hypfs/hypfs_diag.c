// SPDX-License-Identifier: GPL-2.0
/*
 *    Hypervisor filesystem for Linux on s390. Diag 204 and 224
 *    implementation.
 *
 *    Copyright IBM Corp. 2006, 2008
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#define KMSG_COMPONENT "hypfs"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include "hypfs_diag.h"
#include "hypfs.h"

#define DBFS_D204_HDR_VERSION	0

static enum diag204_sc diag204_store_sc;	/* used subcode for store */
static enum diag204_format diag204_info_type;	/* used diag 204 data format */

static void *diag204_buf;		/* 4K aligned buffer for diag204 data */
static int diag204_buf_pages;		/* number of pages for diag204 data */

enum diag204_format diag204_get_info_type(void)
{
	return diag204_info_type;
}

static void diag204_set_info_type(enum diag204_format type)
{
	diag204_info_type = type;
}

/* Diagnose 204 functions */
/*
 * For the old diag subcode 4 with simple data format we have to use real
 * memory. If we use subcode 6 or 7 with extended data format, we can (and
 * should) use vmalloc, since we need a lot of memory in that case. Currently
 * up to 93 pages!
 */

static void diag204_free_buffer(void)
{
	vfree(diag204_buf);
	diag204_buf = NULL;
}

void *diag204_get_buffer(enum diag204_format fmt, int *pages)
{
	if (diag204_buf) {
		*pages = diag204_buf_pages;
		return diag204_buf;
	}
	if (fmt == DIAG204_INFO_SIMPLE) {
		*pages = 1;
	} else {/* DIAG204_INFO_EXT */
		*pages = diag204((unsigned long)DIAG204_SUBC_RSI |
				 (unsigned long)DIAG204_INFO_EXT, 0, NULL);
		if (*pages <= 0)
			return ERR_PTR(-EOPNOTSUPP);
	}
	diag204_buf = __vmalloc_node(array_size(*pages, PAGE_SIZE),
				     PAGE_SIZE, GFP_KERNEL, NUMA_NO_NODE,
				     __builtin_return_address(0));
	if (!diag204_buf)
		return ERR_PTR(-ENOMEM);
	diag204_buf_pages = *pages;
	return diag204_buf;
}

/*
 * diag204_probe() has to find out, which type of diagnose 204 implementation
 * we have on our machine. Currently there are three possible scanarios:
 *   - subcode 4   + simple data format (only one page)
 *   - subcode 4-6 + extended data format
 *   - subcode 4-7 + extended data format
 *
 * Subcode 5 is used to retrieve the size of the data, provided by subcodes
 * 6 and 7. Subcode 7 basically has the same function as subcode 6. In addition
 * to subcode 6 it provides also information about secondary cpus.
 * In order to get as much information as possible, we first try
 * subcode 7, then 6 and if both fail, we use subcode 4.
 */

static int diag204_probe(void)
{
	void *buf;
	int pages, rc;

	buf = diag204_get_buffer(DIAG204_INFO_EXT, &pages);
	if (!IS_ERR(buf)) {
		if (diag204((unsigned long)DIAG204_SUBC_STIB7 |
			    (unsigned long)DIAG204_INFO_EXT, pages, buf) >= 0) {
			diag204_store_sc = DIAG204_SUBC_STIB7;
			diag204_set_info_type(DIAG204_INFO_EXT);
			goto out;
		}
		if (diag204((unsigned long)DIAG204_SUBC_STIB6 |
			    (unsigned long)DIAG204_INFO_EXT, pages, buf) >= 0) {
			diag204_store_sc = DIAG204_SUBC_STIB6;
			diag204_set_info_type(DIAG204_INFO_EXT);
			goto out;
		}
		diag204_free_buffer();
	}

	/* subcodes 6 and 7 failed, now try subcode 4 */

	buf = diag204_get_buffer(DIAG204_INFO_SIMPLE, &pages);
	if (IS_ERR(buf)) {
		rc = PTR_ERR(buf);
		goto fail_alloc;
	}
	if (diag204((unsigned long)DIAG204_SUBC_STIB4 |
		    (unsigned long)DIAG204_INFO_SIMPLE, pages, buf) >= 0) {
		diag204_store_sc = DIAG204_SUBC_STIB4;
		diag204_set_info_type(DIAG204_INFO_SIMPLE);
		goto out;
	} else {
		rc = -EOPNOTSUPP;
		goto fail_store;
	}
out:
	rc = 0;
fail_store:
	diag204_free_buffer();
fail_alloc:
	return rc;
}

int diag204_store(void *buf, int pages)
{
	unsigned long subcode;
	int rc;

	subcode = diag204_get_info_type();
	subcode |= diag204_store_sc;
	if (diag204_has_bif())
		subcode |= DIAG204_BIF_BIT;
	while (1) {
		rc = diag204(subcode, pages, buf);
		if (rc != -EBUSY)
			break;
		if (signal_pending(current))
			return -ERESTARTSYS;
		schedule_timeout_interruptible(DIAG204_BUSY_WAIT);
	}
	return rc < 0 ? rc : 0;
}

struct dbfs_d204_hdr {
	u64	len;		/* Length of d204 buffer without header */
	u16	version;	/* Version of header */
	u8	sc;		/* Used subcode */
	char	reserved[53];
} __attribute__ ((packed));

struct dbfs_d204 {
	struct dbfs_d204_hdr	hdr;	/* 64 byte header */
	char			buf[];	/* d204 buffer */
} __attribute__ ((packed));

static int dbfs_d204_create(void **data, void **data_free_ptr, size_t *size)
{
	struct dbfs_d204 *d204;
	int rc, buf_size;
	void *base;

	buf_size = PAGE_SIZE * (diag204_buf_pages + 1) + sizeof(d204->hdr);
	base = vzalloc(buf_size);
	if (!base)
		return -ENOMEM;
	d204 = PTR_ALIGN(base + sizeof(d204->hdr), PAGE_SIZE) - sizeof(d204->hdr);
	rc = diag204_store(d204->buf, diag204_buf_pages);
	if (rc) {
		vfree(base);
		return rc;
	}
	d204->hdr.version = DBFS_D204_HDR_VERSION;
	d204->hdr.len = PAGE_SIZE * diag204_buf_pages;
	d204->hdr.sc = diag204_store_sc;
	*data = d204;
	*data_free_ptr = base;
	*size = d204->hdr.len + sizeof(struct dbfs_d204_hdr);
	return 0;
}

static struct hypfs_dbfs_file dbfs_file_d204 = {
	.name		= "diag_204",
	.data_create	= dbfs_d204_create,
	.data_free	= vfree,
};

__init int hypfs_diag_init(void)
{
	int rc;

	if (diag204_probe()) {
		pr_info("The hardware system does not support hypfs\n");
		return -ENODATA;
	}

	if (diag204_get_info_type() == DIAG204_INFO_EXT)
		hypfs_dbfs_create_file(&dbfs_file_d204);

	rc = hypfs_diag_fs_init();
	if (rc)
		pr_err("The hardware system does not provide all functions required by hypfs\n");
	return rc;
}

void hypfs_diag_exit(void)
{
	hypfs_diag_fs_exit();
	diag204_free_buffer();
	hypfs_dbfs_remove_file(&dbfs_file_d204);
}
