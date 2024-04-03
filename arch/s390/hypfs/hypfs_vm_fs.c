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
#include <asm/extable.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/timex.h>
#include "hypfs_vm.h"
#include "hypfs.h"

#define ATTRIBUTE(dir, name, member) \
do { \
	void *rc; \
	rc = hypfs_create_u64(dir, name, member); \
	if (IS_ERR(rc)) \
		return PTR_ERR(rc); \
} while (0)

static int hypfs_vm_create_guest(struct dentry *systems_dir,
				 struct diag2fc_data *data)
{
	char guest_name[DIAG2FC_NAME_LEN + 1] = {};
	struct dentry *guest_dir, *cpus_dir, *samples_dir, *mem_dir;
	int dedicated_flag, capped_value;

	capped_value = (data->flags & 0x00000006) >> 1;
	dedicated_flag = (data->flags & 0x00000008) >> 3;

	/* guest dir */
	memcpy(guest_name, data->guest_name, DIAG2FC_NAME_LEN);
	EBCASC(guest_name, DIAG2FC_NAME_LEN);
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

	data = diag2fc_store(diag2fc_guest_query, &count, 0);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* Hypervisor Info */
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
		rc = hypfs_vm_create_guest(dir, &data[i]);
		if (rc)
			goto failed;
	}
	diag2fc_free(data);
	return 0;

failed:
	diag2fc_free(data);
	return rc;
}
