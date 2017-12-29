// SPDX-License-Identifier: GPL-2.0
/*
 *    Hypervisor filesystem for Linux on s390. z/VM implementation.
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/timex.h>
#include "hypfs.h"

#define NAME_LEN 8
#define DBFS_D2FC_HDR_VERSION 0

static char local_guest[] = "        ";
static char all_guests[] = "*       ";
static char *guest_query;

struct diag2fc_data {
	__u32 version;
	__u32 flags;
	__u64 used_cpu;
	__u64 el_time;
	__u64 mem_min_kb;
	__u64 mem_max_kb;
	__u64 mem_share_kb;
	__u64 mem_used_kb;
	__u32 pcpus;
	__u32 lcpus;
	__u32 vcpus;
	__u32 ocpus;
	__u32 cpu_max;
	__u32 cpu_shares;
	__u32 cpu_use_samp;
	__u32 cpu_delay_samp;
	__u32 page_wait_samp;
	__u32 idle_samp;
	__u32 other_samp;
	__u32 total_samp;
	char  guest_name[NAME_LEN];
};

struct diag2fc_parm_list {
	char userid[NAME_LEN];
	char aci_grp[NAME_LEN];
	__u64 addr;
	__u32 size;
	__u32 fmt;
};

static int diag2fc(int size, char* query, void *addr)
{
	unsigned long residual_cnt;
	unsigned long rc;
	struct diag2fc_parm_list parm_list;

	memcpy(parm_list.userid, query, NAME_LEN);
	ASCEBC(parm_list.userid, NAME_LEN);
	parm_list.addr = (unsigned long) addr ;
	parm_list.size = size;
	parm_list.fmt = 0x02;
	memset(parm_list.aci_grp, 0x40, NAME_LEN);
	rc = -1;

	diag_stat_inc(DIAG_STAT_X2FC);
	asm volatile(
		"	diag    %0,%1,0x2fc\n"
		"0:	nopr	%%r7\n"
		EX_TABLE(0b,0b)
		: "=d" (residual_cnt), "+d" (rc) : "0" (&parm_list) : "memory");

	if ((rc != 0 ) && (rc != -2))
		return rc;
	else
		return -residual_cnt;
}

/*
 * Allocate buffer for "query" and store diag 2fc at "offset"
 */
static void *diag2fc_store(char *query, unsigned int *count, int offset)
{
	void *data;
	int size;

	do {
		size = diag2fc(0, query, NULL);
		if (size < 0)
			return ERR_PTR(-EACCES);
		data = vmalloc(size + offset);
		if (!data)
			return ERR_PTR(-ENOMEM);
		if (diag2fc(size, query, data + offset) == 0)
			break;
		vfree(data);
	} while (1);
	*count = (size / sizeof(struct diag2fc_data));

	return data;
}

static void diag2fc_free(const void *data)
{
	vfree(data);
}

#define ATTRIBUTE(dir, name, member) \
do { \
	void *rc; \
	rc = hypfs_create_u64(dir, name, member); \
	if (IS_ERR(rc)) \
		return PTR_ERR(rc); \
} while(0)

static int hpyfs_vm_create_guest(struct dentry *systems_dir,
				 struct diag2fc_data *data)
{
	char guest_name[NAME_LEN + 1] = {};
	struct dentry *guest_dir, *cpus_dir, *samples_dir, *mem_dir;
	int dedicated_flag, capped_value;

	capped_value = (data->flags & 0x00000006) >> 1;
	dedicated_flag = (data->flags & 0x00000008) >> 3;

	/* guest dir */
	memcpy(guest_name, data->guest_name, NAME_LEN);
	EBCASC(guest_name, NAME_LEN);
	strim(guest_name);
	guest_dir = hypfs_mkdir(systems_dir, guest_name);
	if (IS_ERR(guest_dir))
		return PTR_ERR(guest_dir);
	ATTRIBUTE(guest_dir, "onlinetime_us", data->el_time);

	/* logical cpu information */
	cpus_dir = hypfs_mkdir(guest_dir, "cpus");
	if (IS_ERR(cpus_dir))
		return PTR_ERR(cpus_dir);
	ATTRIBUTE(cpus_dir, "cputime_us", data->used_cpu);
	ATTRIBUTE(cpus_dir, "capped", capped_value);
	ATTRIBUTE(cpus_dir, "dedicated", dedicated_flag);
	ATTRIBUTE(cpus_dir, "count", data->vcpus);
	/*
	 * Note: The "weight_min" attribute got the wrong name.
	 * The value represents the number of non-stopped (operating)
	 * CPUS.
	 */
	ATTRIBUTE(cpus_dir, "weight_min", data->ocpus);
	ATTRIBUTE(cpus_dir, "weight_max", data->cpu_max);
	ATTRIBUTE(cpus_dir, "weight_cur", data->cpu_shares);

	/* memory information */
	mem_dir = hypfs_mkdir(guest_dir, "mem");
	if (IS_ERR(mem_dir))
		return PTR_ERR(mem_dir);
	ATTRIBUTE(mem_dir, "min_KiB", data->mem_min_kb);
	ATTRIBUTE(mem_dir, "max_KiB", data->mem_max_kb);
	ATTRIBUTE(mem_dir, "used_KiB", data->mem_used_kb);
	ATTRIBUTE(mem_dir, "share_KiB", data->mem_share_kb);

	/* samples */
	samples_dir = hypfs_mkdir(guest_dir, "samples");
	if (IS_ERR(samples_dir))
		return PTR_ERR(samples_dir);
	ATTRIBUTE(samples_dir, "cpu_using", data->cpu_use_samp);
	ATTRIBUTE(samples_dir, "cpu_delay", data->cpu_delay_samp);
	ATTRIBUTE(samples_dir, "mem_delay", data->page_wait_samp);
	ATTRIBUTE(samples_dir, "idle", data->idle_samp);
	ATTRIBUTE(samples_dir, "other", data->other_samp);
	ATTRIBUTE(samples_dir, "total", data->total_samp);
	return 0;
}

int hypfs_vm_create_files(struct dentry *root)
{
	struct dentry *dir, *file;
	struct diag2fc_data *data;
	unsigned int count = 0;
	int rc, i;

	data = diag2fc_store(guest_query, &count, 0);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* Hpervisor Info */
	dir = hypfs_mkdir(root, "hyp");
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		goto failed;
	}
	file = hypfs_create_str(dir, "type", "z/VM Hypervisor");
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		goto failed;
	}

	/* physical cpus */
	dir = hypfs_mkdir(root, "cpus");
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		goto failed;
	}
	file = hypfs_create_u64(dir, "count", data->lcpus);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		goto failed;
	}

	/* guests */
	dir = hypfs_mkdir(root, "systems");
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		goto failed;
	}

	for (i = 0; i < count; i++) {
		rc = hpyfs_vm_create_guest(dir, &(data[i]));
		if (rc)
			goto failed;
	}
	diag2fc_free(data);
	return 0;

failed:
	diag2fc_free(data);
	return rc;
}

struct dbfs_d2fc_hdr {
	u64	len;		/* Length of d2fc buffer without header */
	u16	version;	/* Version of header */
	char	tod_ext[STORE_CLOCK_EXT_SIZE]; /* TOD clock for d2fc */
	u64	count;		/* Number of VM guests in d2fc buffer */
	char	reserved[30];
} __attribute__ ((packed));

struct dbfs_d2fc {
	struct dbfs_d2fc_hdr	hdr;	/* 64 byte header */
	char			buf[];	/* d2fc buffer */
} __attribute__ ((packed));

static int dbfs_diag2fc_create(void **data, void **data_free_ptr, size_t *size)
{
	struct dbfs_d2fc *d2fc;
	unsigned int count;

	d2fc = diag2fc_store(guest_query, &count, sizeof(d2fc->hdr));
	if (IS_ERR(d2fc))
		return PTR_ERR(d2fc);
	get_tod_clock_ext(d2fc->hdr.tod_ext);
	d2fc->hdr.len = count * sizeof(struct diag2fc_data);
	d2fc->hdr.version = DBFS_D2FC_HDR_VERSION;
	d2fc->hdr.count = count;
	memset(&d2fc->hdr.reserved, 0, sizeof(d2fc->hdr.reserved));
	*data = d2fc;
	*data_free_ptr = d2fc;
	*size = d2fc->hdr.len + sizeof(struct dbfs_d2fc_hdr);
	return 0;
}

static struct hypfs_dbfs_file dbfs_file_2fc = {
	.name		= "diag_2fc",
	.data_create	= dbfs_diag2fc_create,
	.data_free	= diag2fc_free,
};

int hypfs_vm_init(void)
{
	if (!MACHINE_IS_VM)
		return 0;
	if (diag2fc(0, all_guests, NULL) > 0)
		guest_query = all_guests;
	else if (diag2fc(0, local_guest, NULL) > 0)
		guest_query = local_guest;
	else
		return -EACCES;
	return hypfs_dbfs_create_file(&dbfs_file_2fc);
}

void hypfs_vm_exit(void)
{
	if (!MACHINE_IS_VM)
		return;
	hypfs_dbfs_remove_file(&dbfs_file_2fc);
}
