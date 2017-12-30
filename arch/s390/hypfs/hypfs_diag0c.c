// SPDX-License-Identifier: GPL-2.0
/*
 * Hypervisor filesystem for Linux on s390
 *
 * Diag 0C implementation
 *
 * Copyright IBM Corp. 2014
 */

#include <linux/slab.h>
#include <linux/cpu.h>
#include <asm/diag.h>
#include <asm/hypfs.h>
#include "hypfs.h"

#define DBFS_D0C_HDR_VERSION 0

/*
 * Execute diagnose 0c in 31 bit mode
 */
static void diag0c(struct hypfs_diag0c_entry *entry)
{
	diag_stat_inc(DIAG_STAT_X00C);
	asm volatile (
		"	sam31\n"
		"	diag	%0,%0,0x0c\n"
		"	sam64\n"
		: /* no output register */
		: "a" (entry)
		: "memory");
}

/*
 * Get hypfs_diag0c_entry from CPU vector and store diag0c data
 */
static void diag0c_fn(void *data)
{
	diag0c(((void **) data)[smp_processor_id()]);
}

/*
 * Allocate buffer and store diag 0c data
 */
static void *diag0c_store(unsigned int *count)
{
	struct hypfs_diag0c_data *diag0c_data;
	unsigned int cpu_count, cpu, i;
	void **cpu_vec;

	get_online_cpus();
	cpu_count = num_online_cpus();
	cpu_vec = kmalloc(sizeof(*cpu_vec) * num_possible_cpus(), GFP_KERNEL);
	if (!cpu_vec)
		goto fail_put_online_cpus;
	/* Note: Diag 0c needs 8 byte alignment and real storage */
	diag0c_data = kzalloc(sizeof(struct hypfs_diag0c_hdr) +
			      cpu_count * sizeof(struct hypfs_diag0c_entry),
			      GFP_KERNEL | GFP_DMA);
	if (!diag0c_data)
		goto fail_kfree_cpu_vec;
	i = 0;
	/* Fill CPU vector for each online CPU */
	for_each_online_cpu(cpu) {
		diag0c_data->entry[i].cpu = cpu;
		cpu_vec[cpu] = &diag0c_data->entry[i++];
	}
	/* Collect data all CPUs */
	on_each_cpu(diag0c_fn, cpu_vec, 1);
	*count = cpu_count;
	kfree(cpu_vec);
	put_online_cpus();
	return diag0c_data;

fail_kfree_cpu_vec:
	kfree(cpu_vec);
fail_put_online_cpus:
	put_online_cpus();
	return ERR_PTR(-ENOMEM);
}

/*
 * Hypfs DBFS callback: Free diag 0c data
 */
static void dbfs_diag0c_free(const void *data)
{
	kfree(data);
}

/*
 * Hypfs DBFS callback: Create diag 0c data
 */
static int dbfs_diag0c_create(void **data, void **data_free_ptr, size_t *size)
{
	struct hypfs_diag0c_data *diag0c_data;
	unsigned int count;

	diag0c_data = diag0c_store(&count);
	if (IS_ERR(diag0c_data))
		return PTR_ERR(diag0c_data);
	memset(&diag0c_data->hdr, 0, sizeof(diag0c_data->hdr));
	get_tod_clock_ext(diag0c_data->hdr.tod_ext);
	diag0c_data->hdr.len = count * sizeof(struct hypfs_diag0c_entry);
	diag0c_data->hdr.version = DBFS_D0C_HDR_VERSION;
	diag0c_data->hdr.count = count;
	*data = diag0c_data;
	*data_free_ptr = diag0c_data;
	*size = diag0c_data->hdr.len + sizeof(struct hypfs_diag0c_hdr);
	return 0;
}

/*
 * Hypfs DBFS file structure
 */
static struct hypfs_dbfs_file dbfs_file_0c = {
	.name		= "diag_0c",
	.data_create	= dbfs_diag0c_create,
	.data_free	= dbfs_diag0c_free,
};

/*
 * Initialize diag 0c interface for z/VM
 */
int __init hypfs_diag0c_init(void)
{
	if (!MACHINE_IS_VM)
		return 0;
	return hypfs_dbfs_create_file(&dbfs_file_0c);
}

/*
 * Shutdown diag 0c interface for z/VM
 */
void hypfs_diag0c_exit(void)
{
	if (!MACHINE_IS_VM)
		return;
	hypfs_dbfs_remove_file(&dbfs_file_0c);
}
