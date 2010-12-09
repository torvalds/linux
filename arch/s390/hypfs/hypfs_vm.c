/*
 *    Hypervisor filesystem for Linux on s390. z/VM implementation.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/ebcdic.h>
#include <asm/timex.h>
#include "hypfs.h"

#define NAME_LEN 8
#define DBFS_D2FC_HDR_VERSION 0

static char local_guest[] = "        ";
static char all_guests[] = "*       ";
static char *guest_query;

static struct dentry *dbfs_d2fc_file;

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
	__u32 cpu_min;
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

	asm volatile(
		"	diag    %0,%1,0x2fc\n"
		"0:\n"
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

static void diag2fc_free(void *data)
{
	vfree(data);
}

#define ATTRIBUTE(sb, dir, name, member) \
do { \
	void *rc; \
	rc = hypfs_create_u64(sb, dir, name, member); \
	if (IS_ERR(rc)) \
		return PTR_ERR(rc); \
} while(0)

static int hpyfs_vm_create_guest(struct super_block *sb,
				 struct dentry *systems_dir,
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
	guest_dir = hypfs_mkdir(sb, systems_dir, guest_name);
	if (IS_ERR(guest_dir))
		return PTR_ERR(guest_dir);
	ATTRIBUTE(sb, guest_dir, "onlinetime_us", data->el_time);

	/* logical cpu information */
	cpus_dir = hypfs_mkdir(sb, guest_dir, "cpus");
	if (IS_ERR(cpus_dir))
		return PTR_ERR(cpus_dir);
	ATTRIBUTE(sb, cpus_dir, "cputime_us", data->used_cpu);
	ATTRIBUTE(sb, cpus_dir, "capped", capped_value);
	ATTRIBUTE(sb, cpus_dir, "dedicated", dedicated_flag);
	ATTRIBUTE(sb, cpus_dir, "count", data->vcpus);
	ATTRIBUTE(sb, cpus_dir, "weight_min", data->cpu_min);
	ATTRIBUTE(sb, cpus_dir, "weight_max", data->cpu_max);
	ATTRIBUTE(sb, cpus_dir, "weight_cur", data->cpu_shares);

	/* memory information */
	mem_dir = hypfs_mkdir(sb, guest_dir, "mem");
	if (IS_ERR(mem_dir))
		return PTR_ERR(mem_dir);
	ATTRIBUTE(sb, mem_dir, "min_KiB", data->mem_min_kb);
	ATTRIBUTE(sb, mem_dir, "max_KiB", data->mem_max_kb);
	ATTRIBUTE(sb, mem_dir, "used_KiB", data->mem_used_kb);
	ATTRIBUTE(sb, mem_dir, "share_KiB", data->mem_share_kb);

	/* samples */
	samples_dir = hypfs_mkdir(sb, guest_dir, "samples");
	if (IS_ERR(samples_dir))
		return PTR_ERR(samples_dir);
	ATTRIBUTE(sb, samples_dir, "cpu_using", data->cpu_use_samp);
	ATTRIBUTE(sb, samples_dir, "cpu_delay", data->cpu_delay_samp);
	ATTRIBUTE(sb, samples_dir, "mem_delay", data->page_wait_samp);
	ATTRIBUTE(sb, samples_dir, "idle", data->idle_samp);
	ATTRIBUTE(sb, samples_dir, "other", data->other_samp);
	ATTRIBUTE(sb, samples_dir, "total", data->total_samp);
	return 0;
}

int hypfs_vm_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir, *file;
	struct diag2fc_data *data;
	unsigned int count = 0;
	int rc, i;

	data = diag2fc_store(guest_query, &count, 0);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* Hpervisor Info */
	dir = hypfs_mkdir(sb, root, "hyp");
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		goto failed;
	}
	file = hypfs_create_str(sb, dir, "type", "z/VM Hypervisor");
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		goto failed;
	}

	/* physical cpus */
	dir = hypfs_mkdir(sb, root, "cpus");
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		goto failed;
	}
	file = hypfs_create_u64(sb, dir, "count", data->lcpus);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		goto failed;
	}

	/* guests */
	dir = hypfs_mkdir(sb, root, "systems");
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		goto failed;
	}

	for (i = 0; i < count; i++) {
		rc = hpyfs_vm_create_guest(sb, dir, &(data[i]));
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
	char	tod_ext[16];	/* TOD clock for d2fc */
	u64	count;		/* Number of VM guests in d2fc buffer */
	char	reserved[30];
} __attribute__ ((packed));

struct dbfs_d2fc {
	struct dbfs_d2fc_hdr	hdr;	/* 64 byte header */
	char			buf[];	/* d2fc buffer */
} __attribute__ ((packed));

static int dbfs_d2fc_open(struct inode *inode, struct file *file)
{
	struct dbfs_d2fc *data;
	unsigned int count;

	data = diag2fc_store(guest_query, &count, sizeof(data->hdr));
	if (IS_ERR(data))
		return PTR_ERR(data);
	get_clock_ext(data->hdr.tod_ext);
	data->hdr.len = count * sizeof(struct diag2fc_data);
	data->hdr.version = DBFS_D2FC_HDR_VERSION;
	data->hdr.count = count;
	memset(&data->hdr.reserved, 0, sizeof(data->hdr.reserved));
	file->private_data = data;
	return nonseekable_open(inode, file);
}

static int dbfs_d2fc_release(struct inode *inode, struct file *file)
{
	diag2fc_free(file->private_data);
	return 0;
}

static ssize_t dbfs_d2fc_read(struct file *file, char __user *buf,
				    size_t size, loff_t *ppos)
{
	struct dbfs_d2fc *data = file->private_data;

	return simple_read_from_buffer(buf, size, ppos, data, data->hdr.len +
				       sizeof(struct dbfs_d2fc_hdr));
}

static const struct file_operations dbfs_d2fc_ops = {
	.open		= dbfs_d2fc_open,
	.read		= dbfs_d2fc_read,
	.release	= dbfs_d2fc_release,
	.llseek		= no_llseek,
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

	dbfs_d2fc_file = debugfs_create_file("diag_2fc", 0400, hypfs_dbfs_dir,
					     NULL, &dbfs_d2fc_ops);
	if (IS_ERR(dbfs_d2fc_file))
		return PTR_ERR(dbfs_d2fc_file);

	return 0;
}

void hypfs_vm_exit(void)
{
	if (!MACHINE_IS_VM)
		return;
	debugfs_remove(dbfs_d2fc_file);
}
