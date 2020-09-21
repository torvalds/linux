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

static long ne_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations ne_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.unlocked_ioctl	= ne_ioctl,
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

/**
 * ne_enclave_poll() - Poll functionality used for enclave out-of-band events.
 * @file:	File associated with this poll function.
 * @wait:	Poll table data structure.
 *
 * Context: Process context.
 * Return:
 * * Poll mask.
 */
static __poll_t ne_enclave_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;
	struct ne_enclave *ne_enclave = file->private_data;

	poll_wait(file, &ne_enclave->eventq, wait);

	if (!ne_enclave->has_event)
		return mask;

	mask = POLLHUP;

	return mask;
}

static const struct file_operations ne_enclave_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.poll		= ne_enclave_poll,
};

/**
 * ne_create_vm_ioctl() - Alloc slot to be associated with an enclave. Create
 *			  enclave file descriptor to be further used for enclave
 *			  resources handling e.g. memory regions and CPUs.
 * @ne_pci_dev :	Private data associated with the PCI device.
 * @slot_uid:		Generated unique slot id associated with an enclave.
 *
 * Context: Process context. This function is called with the ne_pci_dev enclave
 *	    mutex held.
 * Return:
 * * Enclave fd on success.
 * * Negative return value on failure.
 */
static int ne_create_vm_ioctl(struct ne_pci_dev *ne_pci_dev, u64 *slot_uid)
{
	struct ne_pci_dev_cmd_reply cmd_reply = {};
	int enclave_fd = -1;
	struct file *enclave_file = NULL;
	unsigned int i = 0;
	struct ne_enclave *ne_enclave = NULL;
	struct pci_dev *pdev = ne_pci_dev->pdev;
	int rc = -EINVAL;
	struct slot_alloc_req slot_alloc_req = {};

	mutex_lock(&ne_cpu_pool.mutex);

	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++)
		if (!cpumask_empty(ne_cpu_pool.avail_threads_per_core[i]))
			break;

	if (i == ne_cpu_pool.nr_parent_vm_cores) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "No CPUs available in CPU pool\n");

		mutex_unlock(&ne_cpu_pool.mutex);

		return -NE_ERR_NO_CPUS_AVAIL_IN_POOL;
	}

	mutex_unlock(&ne_cpu_pool.mutex);

	ne_enclave = kzalloc(sizeof(*ne_enclave), GFP_KERNEL);
	if (!ne_enclave)
		return -ENOMEM;

	mutex_lock(&ne_cpu_pool.mutex);

	ne_enclave->nr_parent_vm_cores = ne_cpu_pool.nr_parent_vm_cores;
	ne_enclave->nr_threads_per_core = ne_cpu_pool.nr_threads_per_core;
	ne_enclave->numa_node = ne_cpu_pool.numa_node;

	mutex_unlock(&ne_cpu_pool.mutex);

	ne_enclave->threads_per_core = kcalloc(ne_enclave->nr_parent_vm_cores,
		sizeof(*ne_enclave->threads_per_core), GFP_KERNEL);
	if (!ne_enclave->threads_per_core) {
		rc = -ENOMEM;

		goto free_ne_enclave;
	}

	for (i = 0; i < ne_enclave->nr_parent_vm_cores; i++)
		if (!zalloc_cpumask_var(&ne_enclave->threads_per_core[i], GFP_KERNEL)) {
			rc = -ENOMEM;

			goto free_cpumask;
		}

	if (!zalloc_cpumask_var(&ne_enclave->vcpu_ids, GFP_KERNEL)) {
		rc = -ENOMEM;

		goto free_cpumask;
	}

	enclave_fd = get_unused_fd_flags(O_CLOEXEC);
	if (enclave_fd < 0) {
		rc = enclave_fd;

		dev_err_ratelimited(ne_misc_dev.this_device,
				    "Error in getting unused fd [rc=%d]\n", rc);

		goto free_cpumask;
	}

	enclave_file = anon_inode_getfile("ne-vm", &ne_enclave_fops, ne_enclave, O_RDWR);
	if (IS_ERR(enclave_file)) {
		rc = PTR_ERR(enclave_file);

		dev_err_ratelimited(ne_misc_dev.this_device,
				    "Error in anon inode get file [rc=%d]\n", rc);

		goto put_fd;
	}

	rc = ne_do_request(pdev, SLOT_ALLOC,
			   &slot_alloc_req, sizeof(slot_alloc_req),
			   &cmd_reply, sizeof(cmd_reply));
	if (rc < 0) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "Error in slot alloc [rc=%d]\n", rc);

		goto put_file;
	}

	init_waitqueue_head(&ne_enclave->eventq);
	ne_enclave->has_event = false;
	mutex_init(&ne_enclave->enclave_info_mutex);
	ne_enclave->max_mem_regions = cmd_reply.mem_regions;
	INIT_LIST_HEAD(&ne_enclave->mem_regions_list);
	ne_enclave->mm = current->mm;
	ne_enclave->slot_uid = cmd_reply.slot_uid;
	ne_enclave->state = NE_STATE_INIT;

	list_add(&ne_enclave->enclave_list_entry, &ne_pci_dev->enclaves_list);

	*slot_uid = ne_enclave->slot_uid;

	fd_install(enclave_fd, enclave_file);

	return enclave_fd;

put_file:
	fput(enclave_file);
put_fd:
	put_unused_fd(enclave_fd);
free_cpumask:
	free_cpumask_var(ne_enclave->vcpu_ids);
	for (i = 0; i < ne_enclave->nr_parent_vm_cores; i++)
		free_cpumask_var(ne_enclave->threads_per_core[i]);
	kfree(ne_enclave->threads_per_core);
free_ne_enclave:
	kfree(ne_enclave);

	return rc;
}

/**
 * ne_ioctl() - Ioctl function provided by the NE misc device.
 * @file:	File associated with this ioctl function.
 * @cmd:	The command that is set for the ioctl call.
 * @arg:	The argument that is provided for the ioctl call.
 *
 * Context: Process context.
 * Return:
 * * Ioctl result (e.g. enclave file descriptor) on success.
 * * Negative return value on failure.
 */
static long ne_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case NE_CREATE_VM: {
		int enclave_fd = -1;
		struct file *enclave_file = NULL;
		struct ne_pci_dev *ne_pci_dev = ne_devs.ne_pci_dev;
		int rc = -EINVAL;
		u64 slot_uid = 0;

		mutex_lock(&ne_pci_dev->enclaves_list_mutex);

		enclave_fd = ne_create_vm_ioctl(ne_pci_dev, &slot_uid);
		if (enclave_fd < 0) {
			rc = enclave_fd;

			mutex_unlock(&ne_pci_dev->enclaves_list_mutex);

			return rc;
		}

		mutex_unlock(&ne_pci_dev->enclaves_list_mutex);

		if (copy_to_user((void __user *)arg, &slot_uid, sizeof(slot_uid))) {
			enclave_file = fget(enclave_fd);
			/* Decrement file refs to have release() called. */
			fput(enclave_file);
			fput(enclave_file);
			put_unused_fd(enclave_fd);

			return -EFAULT;
		}

		return enclave_fd;
	}

	default:
		return -ENOTTY;
	}

	return 0;
}

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
