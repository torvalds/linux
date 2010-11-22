/*
 *  arch/s390/hypfs/hypfs_diag.c
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
#include <asm/ebcdic.h>
#include "hypfs.h"

#define LPAR_NAME_LEN 8		/* lpar name len in diag 204 data */
#define CPU_NAME_LEN 16		/* type name len of cpus in diag224 name table */
#define TMP_SIZE 64		/* size of temporary buffers */

#define DBFS_D204_HDR_VERSION	0

/* diag 204 subcodes */
enum diag204_sc {
	SUBC_STIB4 = 4,
	SUBC_RSI = 5,
	SUBC_STIB6 = 6,
	SUBC_STIB7 = 7
};

/* The two available diag 204 data formats */
enum diag204_format {
	INFO_SIMPLE = 0,
	INFO_EXT = 0x00010000
};

/* bit is set in flags, when physical cpu info is included in diag 204 data */
#define LPAR_PHYS_FLG  0x80

static char *diag224_cpu_names;			/* diag 224 name table */
static enum diag204_sc diag204_store_sc;	/* used subcode for store */
static enum diag204_format diag204_info_type;	/* used diag 204 data format */

static void *diag204_buf;		/* 4K aligned buffer for diag204 data */
static void *diag204_buf_vmalloc;	/* vmalloc pointer for diag204 data */
static int diag204_buf_pages;		/* number of pages for diag204 data */

static struct dentry *dbfs_d204_file;

/*
 * DIAG 204 data structures and member access functions.
 *
 * Since we have two different diag 204 data formats for old and new s390
 * machines, we do not access the structs directly, but use getter functions for
 * each struct member instead. This should make the code more readable.
 */

/* Time information block */

struct info_blk_hdr {
	__u8  npar;
	__u8  flags;
	__u16 tslice;
	__u16 phys_cpus;
	__u16 this_part;
	__u64 curtod;
} __attribute__ ((packed));

struct x_info_blk_hdr {
	__u8  npar;
	__u8  flags;
	__u16 tslice;
	__u16 phys_cpus;
	__u16 this_part;
	__u64 curtod1;
	__u64 curtod2;
	char reserved[40];
} __attribute__ ((packed));

static inline int info_blk_hdr__size(enum diag204_format type)
{
	if (type == INFO_SIMPLE)
		return sizeof(struct info_blk_hdr);
	else /* INFO_EXT */
		return sizeof(struct x_info_blk_hdr);
}

static inline __u8 info_blk_hdr__npar(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct info_blk_hdr *)hdr)->npar;
	else /* INFO_EXT */
		return ((struct x_info_blk_hdr *)hdr)->npar;
}

static inline __u8 info_blk_hdr__flags(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct info_blk_hdr *)hdr)->flags;
	else /* INFO_EXT */
		return ((struct x_info_blk_hdr *)hdr)->flags;
}

static inline __u16 info_blk_hdr__pcpus(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct info_blk_hdr *)hdr)->phys_cpus;
	else /* INFO_EXT */
		return ((struct x_info_blk_hdr *)hdr)->phys_cpus;
}

/* Partition header */

struct part_hdr {
	__u8 pn;
	__u8 cpus;
	char reserved[6];
	char part_name[LPAR_NAME_LEN];
} __attribute__ ((packed));

struct x_part_hdr {
	__u8  pn;
	__u8  cpus;
	__u8  rcpus;
	__u8  pflag;
	__u32 mlu;
	char  part_name[LPAR_NAME_LEN];
	char  lpc_name[8];
	char  os_name[8];
	__u64 online_cs;
	__u64 online_es;
	__u8  upid;
	char  reserved1[3];
	__u32 group_mlu;
	char  group_name[8];
	char  reserved2[32];
} __attribute__ ((packed));

static inline int part_hdr__size(enum diag204_format type)
{
	if (type == INFO_SIMPLE)
		return sizeof(struct part_hdr);
	else /* INFO_EXT */
		return sizeof(struct x_part_hdr);
}

static inline __u8 part_hdr__rcpus(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct part_hdr *)hdr)->cpus;
	else /* INFO_EXT */
		return ((struct x_part_hdr *)hdr)->rcpus;
}

static inline void part_hdr__part_name(enum diag204_format type, void *hdr,
				       char *name)
{
	if (type == INFO_SIMPLE)
		memcpy(name, ((struct part_hdr *)hdr)->part_name,
		       LPAR_NAME_LEN);
	else /* INFO_EXT */
		memcpy(name, ((struct x_part_hdr *)hdr)->part_name,
		       LPAR_NAME_LEN);
	EBCASC(name, LPAR_NAME_LEN);
	name[LPAR_NAME_LEN] = 0;
	strim(name);
}

struct cpu_info {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	__u8  cflag;
	__u16 weight;
	__u64 acc_time;
	__u64 lp_time;
} __attribute__ ((packed));

struct x_cpu_info {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	__u8  cflag;
	__u16 weight;
	__u64 acc_time;
	__u64 lp_time;
	__u16 min_weight;
	__u16 cur_weight;
	__u16 max_weight;
	char  reseved2[2];
	__u64 online_time;
	__u64 wait_time;
	__u32 pma_weight;
	__u32 polar_weight;
	char  reserved3[40];
} __attribute__ ((packed));

/* CPU info block */

static inline int cpu_info__size(enum diag204_format type)
{
	if (type == INFO_SIMPLE)
		return sizeof(struct cpu_info);
	else /* INFO_EXT */
		return sizeof(struct x_cpu_info);
}

static inline __u8 cpu_info__ctidx(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct cpu_info *)hdr)->ctidx;
	else /* INFO_EXT */
		return ((struct x_cpu_info *)hdr)->ctidx;
}

static inline __u16 cpu_info__cpu_addr(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct cpu_info *)hdr)->cpu_addr;
	else /* INFO_EXT */
		return ((struct x_cpu_info *)hdr)->cpu_addr;
}

static inline __u64 cpu_info__acc_time(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct cpu_info *)hdr)->acc_time;
	else /* INFO_EXT */
		return ((struct x_cpu_info *)hdr)->acc_time;
}

static inline __u64 cpu_info__lp_time(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct cpu_info *)hdr)->lp_time;
	else /* INFO_EXT */
		return ((struct x_cpu_info *)hdr)->lp_time;
}

static inline __u64 cpu_info__online_time(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return 0;	/* online_time not available in simple info */
	else /* INFO_EXT */
		return ((struct x_cpu_info *)hdr)->online_time;
}

/* Physical header */

struct phys_hdr {
	char reserved1[1];
	__u8 cpus;
	char reserved2[6];
	char mgm_name[8];
} __attribute__ ((packed));

struct x_phys_hdr {
	char reserved1[1];
	__u8 cpus;
	char reserved2[6];
	char mgm_name[8];
	char reserved3[80];
} __attribute__ ((packed));

static inline int phys_hdr__size(enum diag204_format type)
{
	if (type == INFO_SIMPLE)
		return sizeof(struct phys_hdr);
	else /* INFO_EXT */
		return sizeof(struct x_phys_hdr);
}

static inline __u8 phys_hdr__cpus(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct phys_hdr *)hdr)->cpus;
	else /* INFO_EXT */
		return ((struct x_phys_hdr *)hdr)->cpus;
}

/* Physical CPU info block */

struct phys_cpu {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	char  reserved2[3];
	__u64 mgm_time;
	char  reserved3[8];
} __attribute__ ((packed));

struct x_phys_cpu {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	char  reserved2[3];
	__u64 mgm_time;
	char  reserved3[80];
} __attribute__ ((packed));

static inline int phys_cpu__size(enum diag204_format type)
{
	if (type == INFO_SIMPLE)
		return sizeof(struct phys_cpu);
	else /* INFO_EXT */
		return sizeof(struct x_phys_cpu);
}

static inline __u16 phys_cpu__cpu_addr(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct phys_cpu *)hdr)->cpu_addr;
	else /* INFO_EXT */
		return ((struct x_phys_cpu *)hdr)->cpu_addr;
}

static inline __u64 phys_cpu__mgm_time(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct phys_cpu *)hdr)->mgm_time;
	else /* INFO_EXT */
		return ((struct x_phys_cpu *)hdr)->mgm_time;
}

static inline __u64 phys_cpu__ctidx(enum diag204_format type, void *hdr)
{
	if (type == INFO_SIMPLE)
		return ((struct phys_cpu *)hdr)->ctidx;
	else /* INFO_EXT */
		return ((struct x_phys_cpu *)hdr)->ctidx;
}

/* Diagnose 204 functions */

static int diag204(unsigned long subcode, unsigned long size, void *addr)
{
	register unsigned long _subcode asm("0") = subcode;
	register unsigned long _size asm("1") = size;

	asm volatile(
		"	diag	%2,%0,0x204\n"
		"0:\n"
		EX_TABLE(0b,0b)
		: "+d" (_subcode), "+d" (_size) : "d" (addr) : "memory");
	if (_subcode)
		return -1;
	return _size;
}

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
	diag204_buf_vmalloc = vmalloc(PAGE_SIZE * (pages + 1));
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
	if (fmt == INFO_SIMPLE) {
		*pages = 1;
		return diag204_alloc_rbuf();
	} else {/* INFO_EXT */
		*pages = diag204((unsigned long)SUBC_RSI |
				 (unsigned long)INFO_EXT, 0, NULL);
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

	buf = diag204_get_buffer(INFO_EXT, &pages);
	if (!IS_ERR(buf)) {
		if (diag204((unsigned long)SUBC_STIB7 |
			    (unsigned long)INFO_EXT, pages, buf) >= 0) {
			diag204_store_sc = SUBC_STIB7;
			diag204_info_type = INFO_EXT;
			goto out;
		}
		if (diag204((unsigned long)SUBC_STIB6 |
			    (unsigned long)INFO_EXT, pages, buf) >= 0) {
			diag204_store_sc = SUBC_STIB6;
			diag204_info_type = INFO_EXT;
			goto out;
		}
		diag204_free_buffer();
	}

	/* subcodes 6 and 7 failed, now try subcode 4 */

	buf = diag204_get_buffer(INFO_SIMPLE, &pages);
	if (IS_ERR(buf)) {
		rc = PTR_ERR(buf);
		goto fail_alloc;
	}
	if (diag204((unsigned long)SUBC_STIB4 |
		    (unsigned long)INFO_SIMPLE, pages, buf) >= 0) {
		diag204_store_sc = SUBC_STIB4;
		diag204_info_type = INFO_SIMPLE;
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

static int diag224(void *ptr)
{
	int rc = -EOPNOTSUPP;

	asm volatile(
		"	diag	%1,%2,0x224\n"
		"0:	lhi	%0,0x0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) :"d" (0), "d" (ptr) : "memory");
	return rc;
}

static int diag224_get_name_table(void)
{
	/* memory must be below 2GB */
	diag224_cpu_names = kmalloc(PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	if (!diag224_cpu_names)
		return -ENOMEM;
	if (diag224(diag224_cpu_names)) {
		kfree(diag224_cpu_names);
		return -EOPNOTSUPP;
	}
	EBCASC(diag224_cpu_names + 16, (*diag224_cpu_names + 1) * 16);
	return 0;
}

static void diag224_delete_name_table(void)
{
	kfree(diag224_cpu_names);
}

static int diag224_idx2name(int index, char *name)
{
	memcpy(name, diag224_cpu_names + ((index + 1) * CPU_NAME_LEN),
		CPU_NAME_LEN);
	name[CPU_NAME_LEN] = 0;
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

struct dbfs_d204_private {
	struct dbfs_d204	*d204;	/* Aligned d204 data with header */
	void			*base;	/* Base pointer (needed for vfree) */
};

static int dbfs_d204_open(struct inode *inode, struct file *file)
{
	struct dbfs_d204_private *data;
	struct dbfs_d204 *d204;
	int rc, buf_size;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	buf_size = PAGE_SIZE * (diag204_buf_pages + 1) + sizeof(d204->hdr);
	data->base = vmalloc(buf_size);
	if (!data->base) {
		rc = -ENOMEM;
		goto fail_kfree_data;
	}
	memset(data->base, 0, buf_size);
	d204 = page_align_ptr(data->base + sizeof(d204->hdr))
		- sizeof(d204->hdr);
	rc = diag204_do_store(&d204->buf, diag204_buf_pages);
	if (rc)
		goto fail_vfree_base;
	d204->hdr.version = DBFS_D204_HDR_VERSION;
	d204->hdr.len = PAGE_SIZE * diag204_buf_pages;
	d204->hdr.sc = diag204_store_sc;
	data->d204 = d204;
	file->private_data = data;
	return nonseekable_open(inode, file);

fail_vfree_base:
	vfree(data->base);
fail_kfree_data:
	kfree(data);
	return rc;
}

static int dbfs_d204_release(struct inode *inode, struct file *file)
{
	struct dbfs_d204_private *data = file->private_data;

	vfree(data->base);
	kfree(data);
	return 0;
}

static ssize_t dbfs_d204_read(struct file *file, char __user *buf,
			      size_t size, loff_t *ppos)
{
	struct dbfs_d204_private *data = file->private_data;

	return simple_read_from_buffer(buf, size, ppos, data->d204,
				       data->d204->hdr.len +
				       sizeof(data->d204->hdr));
}

static const struct file_operations dbfs_d204_ops = {
	.open		= dbfs_d204_open,
	.read		= dbfs_d204_read,
	.release	= dbfs_d204_release,
	.llseek		= no_llseek,
};

static int hypfs_dbfs_init(void)
{
	dbfs_d204_file = debugfs_create_file("diag_204", 0400, hypfs_dbfs_dir,
					     NULL, &dbfs_d204_ops);
	if (IS_ERR(dbfs_d204_file))
		return PTR_ERR(dbfs_d204_file);
	return 0;
}

__init int hypfs_diag_init(void)
{
	int rc;

	if (diag204_probe()) {
		pr_err("The hardware system does not support hypfs\n");
		return -ENODATA;
	}
	if (diag204_info_type == INFO_EXT) {
		rc = hypfs_dbfs_init();
		if (rc)
			return rc;
	}
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
}

/*
 * Functions to create the directory structure
 * *******************************************
 */

static int hypfs_create_cpu_files(struct super_block *sb,
				  struct dentry *cpus_dir, void *cpu_info)
{
	struct dentry *cpu_dir;
	char buffer[TMP_SIZE];
	void *rc;

	snprintf(buffer, TMP_SIZE, "%d", cpu_info__cpu_addr(diag204_info_type,
							    cpu_info));
	cpu_dir = hypfs_mkdir(sb, cpus_dir, buffer);
	rc = hypfs_create_u64(sb, cpu_dir, "mgmtime",
			      cpu_info__acc_time(diag204_info_type, cpu_info) -
			      cpu_info__lp_time(diag204_info_type, cpu_info));
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	rc = hypfs_create_u64(sb, cpu_dir, "cputime",
			      cpu_info__lp_time(diag204_info_type, cpu_info));
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	if (diag204_info_type == INFO_EXT) {
		rc = hypfs_create_u64(sb, cpu_dir, "onlinetime",
				      cpu_info__online_time(diag204_info_type,
							    cpu_info));
		if (IS_ERR(rc))
			return PTR_ERR(rc);
	}
	diag224_idx2name(cpu_info__ctidx(diag204_info_type, cpu_info), buffer);
	rc = hypfs_create_str(sb, cpu_dir, "type", buffer);
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	return 0;
}

static void *hypfs_create_lpar_files(struct super_block *sb,
				     struct dentry *systems_dir, void *part_hdr)
{
	struct dentry *cpus_dir;
	struct dentry *lpar_dir;
	char lpar_name[LPAR_NAME_LEN + 1];
	void *cpu_info;
	int i;

	part_hdr__part_name(diag204_info_type, part_hdr, lpar_name);
	lpar_name[LPAR_NAME_LEN] = 0;
	lpar_dir = hypfs_mkdir(sb, systems_dir, lpar_name);
	if (IS_ERR(lpar_dir))
		return lpar_dir;
	cpus_dir = hypfs_mkdir(sb, lpar_dir, "cpus");
	if (IS_ERR(cpus_dir))
		return cpus_dir;
	cpu_info = part_hdr + part_hdr__size(diag204_info_type);
	for (i = 0; i < part_hdr__rcpus(diag204_info_type, part_hdr); i++) {
		int rc;
		rc = hypfs_create_cpu_files(sb, cpus_dir, cpu_info);
		if (rc)
			return ERR_PTR(rc);
		cpu_info += cpu_info__size(diag204_info_type);
	}
	return cpu_info;
}

static int hypfs_create_phys_cpu_files(struct super_block *sb,
				       struct dentry *cpus_dir, void *cpu_info)
{
	struct dentry *cpu_dir;
	char buffer[TMP_SIZE];
	void *rc;

	snprintf(buffer, TMP_SIZE, "%i", phys_cpu__cpu_addr(diag204_info_type,
							    cpu_info));
	cpu_dir = hypfs_mkdir(sb, cpus_dir, buffer);
	if (IS_ERR(cpu_dir))
		return PTR_ERR(cpu_dir);
	rc = hypfs_create_u64(sb, cpu_dir, "mgmtime",
			      phys_cpu__mgm_time(diag204_info_type, cpu_info));
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	diag224_idx2name(phys_cpu__ctidx(diag204_info_type, cpu_info), buffer);
	rc = hypfs_create_str(sb, cpu_dir, "type", buffer);
	if (IS_ERR(rc))
		return PTR_ERR(rc);
	return 0;
}

static void *hypfs_create_phys_files(struct super_block *sb,
				     struct dentry *parent_dir, void *phys_hdr)
{
	int i;
	void *cpu_info;
	struct dentry *cpus_dir;

	cpus_dir = hypfs_mkdir(sb, parent_dir, "cpus");
	if (IS_ERR(cpus_dir))
		return cpus_dir;
	cpu_info = phys_hdr + phys_hdr__size(diag204_info_type);
	for (i = 0; i < phys_hdr__cpus(diag204_info_type, phys_hdr); i++) {
		int rc;
		rc = hypfs_create_phys_cpu_files(sb, cpus_dir, cpu_info);
		if (rc)
			return ERR_PTR(rc);
		cpu_info += phys_cpu__size(diag204_info_type);
	}
	return cpu_info;
}

int hypfs_diag_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *systems_dir, *hyp_dir;
	void *time_hdr, *part_hdr;
	int i, rc;
	void *buffer, *ptr;

	buffer = diag204_store();
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	systems_dir = hypfs_mkdir(sb, root, "systems");
	if (IS_ERR(systems_dir)) {
		rc = PTR_ERR(systems_dir);
		goto err_out;
	}
	time_hdr = (struct x_info_blk_hdr *)buffer;
	part_hdr = time_hdr + info_blk_hdr__size(diag204_info_type);
	for (i = 0; i < info_blk_hdr__npar(diag204_info_type, time_hdr); i++) {
		part_hdr = hypfs_create_lpar_files(sb, systems_dir, part_hdr);
		if (IS_ERR(part_hdr)) {
			rc = PTR_ERR(part_hdr);
			goto err_out;
		}
	}
	if (info_blk_hdr__flags(diag204_info_type, time_hdr) & LPAR_PHYS_FLG) {
		ptr = hypfs_create_phys_files(sb, root, part_hdr);
		if (IS_ERR(ptr)) {
			rc = PTR_ERR(ptr);
			goto err_out;
		}
	}
	hyp_dir = hypfs_mkdir(sb, root, "hyp");
	if (IS_ERR(hyp_dir)) {
		rc = PTR_ERR(hyp_dir);
		goto err_out;
	}
	ptr = hypfs_create_str(sb, hyp_dir, "type", "LPAR Hypervisor");
	if (IS_ERR(ptr)) {
		rc = PTR_ERR(ptr);
		goto err_out;
	}
	rc = 0;

err_out:
	return rc;
}
