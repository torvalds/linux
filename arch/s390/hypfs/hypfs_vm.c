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
#include "hypfs.h"

#define NAME_LEN 8

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

static struct diag2fc_data *diag2fc_store(char *query, int *count)
{
	int size;
	struct diag2fc_data *data;

	do {
		size = diag2fc(0, query, NULL);
		if (size < 0)
			return ERR_PTR(-EACCES);
		data = vmalloc(size);
		if (!data)
			return ERR_PTR(-ENOMEM);
		if (diag2fc(size, query, data) == 0)
			break;
		vfree(data);
	} while (1);
	*count = (size / sizeof(*data));

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
	strstrip(guest_name);
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
	int rc, i, count = 0;

	data = diag2fc_store(guest_query, &count);
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

int hypfs_vm_init(void)
{
	if (diag2fc(0, all_guests, NULL) > 0)
		guest_query = all_guests;
	else if (diag2fc(0, local_guest, NULL) > 0)
		guest_query = local_guest;
	else
		return -EACCES;

	return 0;
}
