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
#include "hypfs.h"

#define TMP_SIZE 64		/* size of temporary buffers */

#define DBFS_D204_HDR_VERSION	0

static char *diag224_cpu_names;			/* diag 224 name table */
static enum diag204_sc diag204_store_sc;	/* used subcode for store */
static enum diag204_format diag204_info_type;	/* used diag 204 data format */

static void *diag204_buf;		/* 4K aligned buffer for diag204 data */
static void *diag204_buf_vmalloc;	/* vmalloc pointer for diag204 data */
static int diag204_buf_pages;		/* number of pages for diag204 data */

static struct dentry *dbfs_d204_file;

/*
 * DIAG 204 member access functions.
 *
 * Since we have two different diag 204 data formats for old and new s390
 * machines, we do not access the structs directly, but use getter functions for
 * each struct member instead. This should make the code more readable.
 */

/* Time information block */

static inline int info_blk_hdr__size(enum diag204_format type)
{
	if (type == DIAG204_INFO_SIMPLE)
		return sizeof(struct diag204_info_blk_hdr);
	else /* DIAG204_INFO_EXT */
		return sizeof(struct diag204_x_info_blk_hdr);
}

static inline __u8 info_blk_hdr__npar(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_info_blk_hdr *)hdr)->npar;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_info_blk_hdr *)hdr)->npar;
}

static inline __u8 info_blk_hdr__flags(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_info_blk_hdr *)hdr)->flags;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_info_blk_hdr *)hdr)->flags;
}

static inline __u16 info_blk_hdr__pcpus(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_info_blk_hdr *)hdr)->phys_cpus;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_info_blk_hdr *)hdr)->phys_cpus;
}

/* Partition header */

static inline int part_hdr__size(enum diag204_format type)
{
	if (type == DIAG204_INFO_SIMPLE)
		return sizeof(struct diag204_part_hdr);
	else /* DIAG204_INFO_EXT */
		return sizeof(struct diag204_x_part_hdr);
}

static inline __u8 part_hdr__rcpus(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_part_hdr *)hdr)->cpus;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_part_hdr *)hdr)->rcpus;
}

static inline void part_hdr__part_name(enum diag204_format type, void *hdr,
				       char *name)
{
	if (type == DIAG204_INFO_SIMPLE)
		memcpy(name, ((struct diag204_part_hdr *)hdr)->part_name,
		       DIAG204_LPAR_NAME_LEN);
	else /* DIAG204_INFO_EXT */
		memcpy(name, ((struct diag204_x_part_hdr *)hdr)->part_name,
		       DIAG204_LPAR_NAME_LEN);
	EBCASC(name, DIAG204_LPAR_NAME_LEN);
	name[DIAG204_LPAR_NAME_LEN] = 0;
	strim(name);
}

/* CPU info block */

static inline int cpu_info__size(enum diag204_format type)
{
	if (type == DIAG204_INFO_SIMPLE)
		return sizeof(struct diag204_cpu_info);
	else /* DIAG204_INFO_EXT */
		return sizeof(struct diag204_x_cpu_info);
}

static inline __u8 cpu_info__ctidx(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_cpu_info *)hdr)->ctidx;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_cpu_info *)hdr)->ctidx;
}

static inline __u16 cpu_info__cpu_addr(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_cpu_info *)hdr)->cpu_addr;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_cpu_info *)hdr)->cpu_addr;
}

static inline __u64 cpu_info__acc_time(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_cpu_info *)hdr)->acc_time;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_cpu_info *)hdr)->acc_time;
}

static inline __u64 cpu_info__lp_time(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_cpu_info *)hdr)->lp_time;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_cpu_info *)hdr)->lp_time;
}

static inline __u64 cpu_info__online_time(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return 0;	/* online_time not available in simple info */
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_cpu_info *)hdr)->online_time;
}

/* Physical header */

static inline int phys_hdr__size(enum diag204_format type)
{
	if (type == DIAG204_INFO_SIMPLE)
		return sizeof(struct diag204_phys_hdr);
	else /* DIAG204_INFO_EXT */
		return sizeof(struct diag204_x_phys_hdr);
}

static inline __u8 phys_hdr__cpus(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_phys_hdr *)hdr)->cpus;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_phys_hdr *)hdr)->cpus;
}

/* Physical CPU info block */

static inline int phys_cpu__size(enum diag204_format type)
{
	if (type == DIAG204_INFO_SIMPLE)
		return sizeof(struct diag204_phys_cpu);
	else /* DIAG204_INFO_EXT */
		return sizeof(struct diag204_x_phys_cpu);
}

static inline __u16 phys_cpu__cpu_addr(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_phys_cpu *)hdr)->cpu_addr;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_phys_cpu *)hdr)->cpu_addr;
}

static inline __u64 phys_cpu__mgm_time(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_phys_cpu *)hdr)->mgm_time;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_phys_cpu *)hdr)->mgm_time;
}

static inline __u64 phys_cpu__ctidx(enum diag204_format type, void *hdr)
{
	if (type == DIAG204_INFO_SIMPLE)
		return ((struct diag204_phys_cpu *)hdr)->ctidx;
	else /* DIAG204_INFO_EXT */
		return ((struct diag204_x_phys_cpu *)hdr)->ctidx;
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
	if (!diag204_buf)
		return;
	if (diag204_buf_vmalloc) {
		vfree(diag204_buf_vmalloc);
		diag204_buf_vmalloc = NULL;
	} else {
		free_pages((unsigned long) diag204_buf, 0);
	}
	diag204_buf = NULL;
}

static void *page_align_ptr(void *ptr)
{
	return (void *) PAGE_ALIGN((unsigned long) ptr);
}

static void *diag204_alloc_vbuf(int pages)
{
	/* The buffer has to be page aligned! */
	diag204_buf_vmalloc = vmalloc(array_size(PAGE_SIZE, (pages + 1)));
	if (!diag204_buf_vmalloc)
		return ERR_PTR(-ENOMEM);
	diag204_buf = page_align_ptr(diag204_buf_vmalloc);
	diag204_buf_pages = pages;
	return diag204_buf;
}

static void *diag204_alloc_rbuf(void)
{
	diag204_buf = (void*)__get_free_pages(GFP_KERNEL,0);
	if (!diag204_buf)
		return ERR_PTR(-ENOMEM);
	diag204_buf_pages = 1;
	return diag204_buf;
}

static void *diag204_get_buffer(enum diag204_format fmt, int *pages)
{
	if (diag204_buf) {
		*pages = diag204_buf_pages;
		return diag204_buf;
	}
	if (fmt == DIAG204_INFO_SIMPLE) {
		*pages = 1;
		return diag204_alloc_rbuf();
	} else {/* DIAG204_INFO_EXT */
		*pages = diag204((unsigned long)DIAG204_SUBC_RSI |
				 (unsigned long)DIAG204_INFO_EXT, 0, NULL);
		if (*pages <= 0)
			return ERR_PTR(-ENOSYS);
		else
			return diag204_alloc_vbuf(*pages);
	}
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
			diag204_info_type = DIAG204_INFO_EXT;
			goto out;
		}
		if (diag204((unsigned long)DIAG204_SUBC_STIB6 |
			    (unsigned long)DIAG204_INFO_EXT, pages, buf) >= 0) {
			diag204_store_sc = DIAG204_SUBC_STIB6;
			diag204_info_type = DIAG204_INFO_EXT;
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
		diag204_info_type = DIAG204_INFO_SIMPLE;
		goto out;
	} else {
		rc = -ENOSYS;
		goto fail_store;
	}
out:
	rc = 0;
fail_store:
	diag204_free_buffer();
fail_alloc:
	return rc;
}

static int diag204_do_store(void *buf, int pages)
{
	int rc;

	rc = diag204((unsigned long) diag204_store_sc |
		     (unsigned long) diag204_info_type, pages, buf);
	return rc < 0 ? -ENOSYS : 0;
}

static void *diag204_store(void)
{
	void *buf;
	int pages, rc;

	buf = diag204_get_buffer(diag204_info_type, &pages);
	if (IS_ERR(buf))
		goto out;
	rc = diag204_do_store(buf, pages);
	if (rc)
		return ERR_PTR(rc);
out:
	return buf;
}

/* Diagnose 224 functions */

static int diag224_get_name_table(void)
{
	/* memory must be below 2GB */
	diag224_cpu_names = (char *) __get_free_page(GFP_KERNEL | GFP_DMA);
	if (!diag224_cpu_names)
		return -ENOMEM;
	if (diag224(diag224_cpu_names)) {
		free_page((unsigned long) diag224_cpu_names);
		return -EOPNOTSUPP;
	}
	EBCASC(diag224_cpu_names + 16, (*diag224_cpu_names + 1) * 16);
	return 0;
}

static void diag224_delete_name_table(void)
{
	free_page((unsigned long) diag224_cpu_names);
}

static int diag224_idx2name(int index, char *name)
{
	memcpy(name, diag224_cpu_names + ((index + 1) * DIAG204_CPU_NAME_LEN),
	       DIAG204_CPU_NAME_LEN);
	name[DIAG204_CPU_NAME_LEN] = 0;
	strim(name);
	return 0;
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
	d204 = page_align_ptr(base + sizeof(d204->hdr)) - sizeof(d204->hdr);
	rc = diag204_do_store(d204->buf, diag204_buf_pages);
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
		pr_err("The hardware system does not support hypfs\n");
		return -ENODATA;
	}

	if (diag204_info_type == DIAG204_INFO_EXT)
		hypfs_dbfs_create_file(&dbfs_file_d204);

	if (MACHINE_IS_LPAR) {
		rc = diag224_get_name_table();
		if (rc) {
			pr_err("The hardware system does not provide all "
			       "functions required by hypfs\n");
			debugfs_remove(dbfs_d204_file);
			return rc;
		}
	}
	return 0;
}

void hypfs_diag_exit(void)
{
	debugfs_remove(dbfs_d204_file);
	diag224_delete_name_table();
	diag204_free_buffer();
	hypfs_dbfs_remove_file(&dbfs_file_d204);
}

/*
 * Functions to create the directory structure
 * *******************************************
 */

static int hypfs_create_cpu_files(struct dentry *cpus_dir, void *cpu_info)
{
	struct dentry *cpu_dir;
	char buffer[TMP_SIZE];
	void *rc;

	snprintf(buffer, TMP_SIZE, "%d", cpu_info__cpu_addr(diag204_info_type,
							    cpu_info));
	cpu_dir = hypfs_mkdir(cpus_dir, buffer);
	rc = hypfs_create_u64(cpu_dir, "mgmtime",
			      cpu_info__acc_time(diag204_info_type, cpu_info) -
			      cpu_info__lp_time(diag204_info_type, cpu_info));
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	rc = hypfs_create_u64(cpu_dir, "cputime",
			      cpu_info__lp_time(diag204_info_type, cpu_info));
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	if (diag204_info_type == DIAG204_INFO_EXT) {
		rc = hypfs_create_u64(cpu_dir, "onlinetime",
				      cpu_info__online_time(diag204_info_type,
							    cpu_info));
		if (IS_ERR(rc))
			return PTR_ERR(rc);
	}
	diag224_idx2name(cpu_info__ctidx(diag204_info_type, cpu_info), buffer);
	rc = hypfs_create_str(cpu_dir, "type", buffer);
	return PTR_ERR_OR_ZERO(rc);
}

static void *hypfs_create_lpar_files(struct dentry *systems_dir, void *part_hdr)
{
	struct dentry *cpus_dir;
	struct dentry *lpar_dir;
	char lpar_name[DIAG204_LPAR_NAME_LEN + 1];
	void *cpu_info;
	int i;

	part_hdr__part_name(diag204_info_type, part_hdr, lpar_name);
	lpar_name[DIAG204_LPAR_NAME_LEN] = 0;
	lpar_dir = hypfs_mkdir(systems_dir, lpar_name);
	if (IS_ERR(lpar_dir))
		return lpar_dir;
	cpus_dir = hypfs_mkdir(lpar_dir, "cpus");
	if (IS_ERR(cpus_dir))
		return cpus_dir;
	cpu_info = part_hdr + part_hdr__size(diag204_info_type);
	for (i = 0; i < part_hdr__rcpus(diag204_info_type, part_hdr); i++) {
		int rc;
		rc = hypfs_create_cpu_files(cpus_dir, cpu_info);
		if (rc)
			return ERR_PTR(rc);
		cpu_info += cpu_info__size(diag204_info_type);
	}
	return cpu_info;
}

static int hypfs_create_phys_cpu_files(struct dentry *cpus_dir, void *cpu_info)
{
	struct dentry *cpu_dir;
	char buffer[TMP_SIZE];
	void *rc;

	snprintf(buffer, TMP_SIZE, "%i", phys_cpu__cpu_addr(diag204_info_type,
							    cpu_info));
	cpu_dir = hypfs_mkdir(cpus_dir, buffer);
	if (IS_ERR(cpu_dir))
		return PTR_ERR(cpu_dir);
	rc = hypfs_create_u64(cpu_dir, "mgmtime",
			      phys_cpu__mgm_time(diag204_info_type, cpu_info));
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	diag224_idx2name(phys_cpu__ctidx(diag204_info_type, cpu_info), buffer);
	rc = hypfs_create_str(cpu_dir, "type", buffer);
	return PTR_ERR_OR_ZERO(rc);
}

static void *hypfs_create_phys_files(struct dentry *parent_dir, void *phys_hdr)
{
	int i;
	void *cpu_info;
	struct dentry *cpus_dir;

	cpus_dir = hypfs_mkdir(parent_dir, "cpus");
	if (IS_ERR(cpus_dir))
		return cpus_dir;
	cpu_info = phys_hdr + phys_hdr__size(diag204_info_type);
	for (i = 0; i < phys_hdr__cpus(diag204_info_type, phys_hdr); i++) {
		int rc;
		rc = hypfs_create_phys_cpu_files(cpus_dir, cpu_info);
		if (rc)
			return ERR_PTR(rc);
		cpu_info += phys_cpu__size(diag204_info_type);
	}
	return cpu_info;
}

int hypfs_diag_create_files(struct dentry *root)
{
	struct dentry *systems_dir, *hyp_dir;
	void *time_hdr, *part_hdr;
	int i, rc;
	void *buffer, *ptr;

	buffer = diag204_store();
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	systems_dir = hypfs_mkdir(root, "systems");
	if (IS_ERR(systems_dir)) {
		rc = PTR_ERR(systems_dir);
		goto err_out;
	}
	time_hdr = (struct x_info_blk_hdr *)buffer;
	part_hdr = time_hdr + info_blk_hdr__size(diag204_info_type);
	for (i = 0; i < info_blk_hdr__npar(diag204_info_type, time_hdr); i++) {
		part_hdr = hypfs_create_lpar_files(systems_dir, part_hdr);
		if (IS_ERR(part_hdr)) {
			rc = PTR_ERR(part_hdr);
			goto err_out;
		}
	}
	if (info_blk_hdr__flags(diag204_info_type, time_hdr) &
	    DIAG204_LPAR_PHYS_FLG) {
		ptr = hypfs_create_phys_files(root, part_hdr);
		if (IS_ERR(ptr)) {
			rc = PTR_ERR(ptr);
			goto err_out;
		}
	}
	hyp_dir = hypfs_mkdir(root, "hyp");
	if (IS_ERR(hyp_dir)) {
		rc = PTR_ERR(hyp_dir);
		goto err_out;
	}
	ptr = hypfs_create_str(hyp_dir, "type", "LPAR Hypervisor");
	if (IS_ERR(ptr)) {
		rc = PTR_ERR(ptr);
		goto err_out;
	}
	rc = 0;

err_out:
	return rc;
}
