// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

/**
 * DOC: Enclave lifetime management driver for Nitro Enclaves (NE).
 * Nitro is a hypervisor that has been developed by Amazon.
 */

#include <linux/anon_inodes.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/hugetlb.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nitro_enclaves.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <uapi/linux/vm_sockets.h>

#include "ne_misc_dev.h"
#include "ne_pci_dev.h"

/**
 * NE_CPUS_SIZE - Size for max 128 CPUs, for now, in a cpu-list string, comma
 *		  separated. The NE CPU pool includes CPUs from a single NUMA
 *		  node.
 */
#define NE_CPUS_SIZE		(512)

/**
 * NE_EIF_LOAD_OFFSET - The offset where to copy the Enclave Image Format (EIF)
 *			image in enclave memory.
 */
#define NE_EIF_LOAD_OFFSET	(8 * 1024UL * 1024UL)

/**
 * NE_MIN_ENCLAVE_MEM_SIZE - The minimum memory size an enclave can be launched
 *			     with.
 */
#define NE_MIN_ENCLAVE_MEM_SIZE	(64 * 1024UL * 1024UL)

/**
 * NE_MIN_MEM_REGION_SIZE - The minimum size of an enclave memory region.
 */
#define NE_MIN_MEM_REGION_SIZE	(2 * 1024UL * 1024UL)

/**
 * NE_PARENT_VM_CID - The CID for the vsock device of the primary / parent VM.
 */
#define NE_PARENT_VM_CID	(3)

static const struct file_operations ne_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
};

static struct miscdevice ne_misc_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "nitro_enclaves",
	.fops	= &ne_fops,
	.mode	= 0660,
};

struct ne_devs ne_devs = {
	.ne_misc_dev	= &ne_misc_dev,
};

/*
 * TODO: Update logic to create new sysfs entries instead of using
 * a kernel parameter e.g. if multiple sysfs files needed.
 */
static const struct kernel_param_ops ne_cpu_pool_ops = {
	.get	= param_get_string,
};

static char ne_cpus[NE_CPUS_SIZE];
static struct kparam_string ne_cpus_arg = {
	.maxlen	= sizeof(ne_cpus),
	.string	= ne_cpus,
};

module_param_cb(ne_cpus, &ne_cpu_pool_ops, &ne_cpus_arg, 0644);
/* https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html#cpu-lists */
MODULE_PARM_DESC(ne_cpus, "<cpu-list> - CPU pool used for Nitro Enclaves");

/**
 * struct ne_cpu_pool - CPU pool used for Nitro Enclaves.
 * @avail_threads_per_core:	Available full CPU cores to be dedicated to
 *				enclave(s). The cpumasks from the array, indexed
 *				by core id, contain all the threads from the
 *				available cores, that are not set for created
 *				enclave(s). The full CPU cores are part of the
 *				NE CPU pool.
 * @mutex:			Mutex for the access to the NE CPU pool.
 * @nr_parent_vm_cores :	The size of the available threads per core array.
 *				The total number of CPU cores available on the
 *				primary / parent VM.
 * @nr_threads_per_core:	The number of threads that a full CPU core has.
 * @numa_node:			NUMA node of the CPUs in the pool.
 */
struct ne_cpu_pool {
	cpumask_var_t	*avail_threads_per_core;
	struct mutex	mutex;
	unsigned int	nr_parent_vm_cores;
	unsigned int	nr_threads_per_core;
	int		numa_node;
};

static struct ne_cpu_pool ne_cpu_pool;

static int __init ne_init(void)
{
	mutex_init(&ne_cpu_pool.mutex);

	return pci_register_driver(&ne_pci_driver);
}

static void __exit ne_exit(void)
{
	pci_unregister_driver(&ne_pci_driver);
}

module_init(ne_init);
module_exit(ne_exit);

MODULE_AUTHOR("Amazon.com, Inc. or its affiliates");
MODULE_DESCRIPTION("Nitro Enclaves Driver");
MODULE_LICENSE("GPL v2");
