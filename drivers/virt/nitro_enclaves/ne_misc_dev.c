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
static int ne_set_kernel_param(const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops ne_cpu_pool_ops = {
	.get	= param_get_string,
	.set	= ne_set_kernel_param,
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
 * ne_check_enclaves_created() - Verify if at least one enclave has been created.
 * @void:	No parameters provided.
 *
 * Context: Process context.
 * Return:
 * * True if at least one enclave is created.
 * * False otherwise.
 */
static bool ne_check_enclaves_created(void)
{
	struct ne_pci_dev *ne_pci_dev = ne_devs.ne_pci_dev;
	bool ret = false;

	if (!ne_pci_dev)
		return ret;

	mutex_lock(&ne_pci_dev->enclaves_list_mutex);

	if (!list_empty(&ne_pci_dev->enclaves_list))
		ret = true;

	mutex_unlock(&ne_pci_dev->enclaves_list_mutex);

	return ret;
}

/**
 * ne_setup_cpu_pool() - Set the NE CPU pool after handling sanity checks such
 *			 as not sharing CPU cores with the primary / parent VM
 *			 or not using CPU 0, which should remain available for
 *			 the primary / parent VM. Offline the CPUs from the
 *			 pool after the checks passed.
 * @ne_cpu_list:	The CPU list used for setting NE CPU pool.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_setup_cpu_pool(const char *ne_cpu_list)
{
	int core_id = -1;
	unsigned int cpu = 0;
	cpumask_var_t cpu_pool;
	unsigned int cpu_sibling = 0;
	unsigned int i = 0;
	int numa_node = -1;
	int rc = -EINVAL;

	if (!zalloc_cpumask_var(&cpu_pool, GFP_KERNEL))
		return -ENOMEM;

	mutex_lock(&ne_cpu_pool.mutex);

	rc = cpulist_parse(ne_cpu_list, cpu_pool);
	if (rc < 0) {
		pr_err("%s: Error in cpulist parse [rc=%d]\n", ne_misc_dev.name, rc);

		goto free_pool_cpumask;
	}

	cpu = cpumask_any(cpu_pool);
	if (cpu >= nr_cpu_ids) {
		pr_err("%s: No CPUs available in CPU pool\n", ne_misc_dev.name);

		rc = -EINVAL;

		goto free_pool_cpumask;
	}

	/*
	 * Check if the CPUs are online, to further get info about them
	 * e.g. numa node, core id, siblings.
	 */
	for_each_cpu(cpu, cpu_pool)
		if (cpu_is_offline(cpu)) {
			pr_err("%s: CPU %d is offline, has to be online to get its metadata\n",
			       ne_misc_dev.name, cpu);

			rc = -EINVAL;

			goto free_pool_cpumask;
		}

	/*
	 * Check if the CPUs from the NE CPU pool are from the same NUMA node.
	 */
	for_each_cpu(cpu, cpu_pool)
		if (numa_node < 0) {
			numa_node = cpu_to_node(cpu);
			if (numa_node < 0) {
				pr_err("%s: Invalid NUMA node %d\n",
				       ne_misc_dev.name, numa_node);

				rc = -EINVAL;

				goto free_pool_cpumask;
			}
		} else {
			if (numa_node != cpu_to_node(cpu)) {
				pr_err("%s: CPUs with different NUMA nodes\n",
				       ne_misc_dev.name);

				rc = -EINVAL;

				goto free_pool_cpumask;
			}
		}

	/*
	 * Check if CPU 0 and its siblings are included in the provided CPU pool
	 * They should remain available for the primary / parent VM.
	 */
	if (cpumask_test_cpu(0, cpu_pool)) {
		pr_err("%s: CPU 0 has to remain available\n", ne_misc_dev.name);

		rc = -EINVAL;

		goto free_pool_cpumask;
	}

	for_each_cpu(cpu_sibling, topology_sibling_cpumask(0)) {
		if (cpumask_test_cpu(cpu_sibling, cpu_pool)) {
			pr_err("%s: CPU sibling %d for CPU 0 is in CPU pool\n",
			       ne_misc_dev.name, cpu_sibling);

			rc = -EINVAL;

			goto free_pool_cpumask;
		}
	}

	/*
	 * Check if CPU siblings are included in the provided CPU pool. The
	 * expectation is that full CPU cores are made available in the CPU pool
	 * for enclaves.
	 */
	for_each_cpu(cpu, cpu_pool) {
		for_each_cpu(cpu_sibling, topology_sibling_cpumask(cpu)) {
			if (!cpumask_test_cpu(cpu_sibling, cpu_pool)) {
				pr_err("%s: CPU %d is not in CPU pool\n",
				       ne_misc_dev.name, cpu_sibling);

				rc = -EINVAL;

				goto free_pool_cpumask;
			}
		}
	}

	/* Calculate the number of threads from a full CPU core. */
	cpu = cpumask_any(cpu_pool);
	for_each_cpu(cpu_sibling, topology_sibling_cpumask(cpu))
		ne_cpu_pool.nr_threads_per_core++;

	ne_cpu_pool.nr_parent_vm_cores = nr_cpu_ids / ne_cpu_pool.nr_threads_per_core;

	ne_cpu_pool.avail_threads_per_core = kcalloc(ne_cpu_pool.nr_parent_vm_cores,
					     sizeof(*ne_cpu_pool.avail_threads_per_core),
					     GFP_KERNEL);
	if (!ne_cpu_pool.avail_threads_per_core) {
		rc = -ENOMEM;

		goto free_pool_cpumask;
	}

	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++)
		if (!zalloc_cpumask_var(&ne_cpu_pool.avail_threads_per_core[i], GFP_KERNEL)) {
			rc = -ENOMEM;

			goto free_cores_cpumask;
		}

	/*
	 * Split the NE CPU pool in threads per core to keep the CPU topology
	 * after offlining the CPUs.
	 */
	for_each_cpu(cpu, cpu_pool) {
		core_id = topology_core_id(cpu);
		if (core_id < 0 || core_id >= ne_cpu_pool.nr_parent_vm_cores) {
			pr_err("%s: Invalid core id  %d for CPU %d\n",
			       ne_misc_dev.name, core_id, cpu);

			rc = -EINVAL;

			goto clear_cpumask;
		}

		cpumask_set_cpu(cpu, ne_cpu_pool.avail_threads_per_core[core_id]);
	}

	/*
	 * CPUs that are given to enclave(s) should not be considered online
	 * by Linux anymore, as the hypervisor will degrade them to floating.
	 * The physical CPUs (full cores) are carved out of the primary / parent
	 * VM and given to the enclave VM. The same number of vCPUs would run
	 * on less pCPUs for the primary / parent VM.
	 *
	 * We offline them here, to not degrade performance and expose correct
	 * topology to Linux and user space.
	 */
	for_each_cpu(cpu, cpu_pool) {
		rc = remove_cpu(cpu);
		if (rc != 0) {
			pr_err("%s: CPU %d is not offlined [rc=%d]\n",
			       ne_misc_dev.name, cpu, rc);

			goto online_cpus;
		}
	}

	free_cpumask_var(cpu_pool);

	ne_cpu_pool.numa_node = numa_node;

	mutex_unlock(&ne_cpu_pool.mutex);

	return 0;

online_cpus:
	for_each_cpu(cpu, cpu_pool)
		add_cpu(cpu);
clear_cpumask:
	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++)
		cpumask_clear(ne_cpu_pool.avail_threads_per_core[i]);
free_cores_cpumask:
	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++)
		free_cpumask_var(ne_cpu_pool.avail_threads_per_core[i]);
	kfree(ne_cpu_pool.avail_threads_per_core);
free_pool_cpumask:
	free_cpumask_var(cpu_pool);
	ne_cpu_pool.nr_parent_vm_cores = 0;
	ne_cpu_pool.nr_threads_per_core = 0;
	ne_cpu_pool.numa_node = -1;
	mutex_unlock(&ne_cpu_pool.mutex);

	return rc;
}

/**
 * ne_teardown_cpu_pool() - Online the CPUs from the NE CPU pool and cleanup the
 *			    CPU pool.
 * @void:	No parameters provided.
 *
 * Context: Process context.
 */
static void ne_teardown_cpu_pool(void)
{
	unsigned int cpu = 0;
	unsigned int i = 0;
	int rc = -EINVAL;

	mutex_lock(&ne_cpu_pool.mutex);

	if (!ne_cpu_pool.nr_parent_vm_cores) {
		mutex_unlock(&ne_cpu_pool.mutex);

		return;
	}

	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++) {
		for_each_cpu(cpu, ne_cpu_pool.avail_threads_per_core[i]) {
			rc = add_cpu(cpu);
			if (rc != 0)
				pr_err("%s: CPU %d is not onlined [rc=%d]\n",
				       ne_misc_dev.name, cpu, rc);
		}

		cpumask_clear(ne_cpu_pool.avail_threads_per_core[i]);

		free_cpumask_var(ne_cpu_pool.avail_threads_per_core[i]);
	}

	kfree(ne_cpu_pool.avail_threads_per_core);
	ne_cpu_pool.nr_parent_vm_cores = 0;
	ne_cpu_pool.nr_threads_per_core = 0;
	ne_cpu_pool.numa_node = -1;

	mutex_unlock(&ne_cpu_pool.mutex);
}

/**
 * ne_set_kernel_param() - Set the NE CPU pool value via the NE kernel parameter.
 * @val:	NE CPU pool string value.
 * @kp :	NE kernel parameter associated with the NE CPU pool.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_set_kernel_param(const char *val, const struct kernel_param *kp)
{
	char error_val[] = "";
	int rc = -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (ne_check_enclaves_created()) {
		pr_err("%s: The CPU pool is used by enclave(s)\n", ne_misc_dev.name);

		return -EPERM;
	}

	ne_teardown_cpu_pool();

	rc = ne_setup_cpu_pool(val);
	if (rc < 0) {
		pr_err("%s: Error in setup CPU pool [rc=%d]\n", ne_misc_dev.name, rc);

		param_set_copystring(error_val, kp);

		return rc;
	}

	rc = param_set_copystring(val, kp);
	if (rc < 0) {
		pr_err("%s: Error in param set copystring [rc=%d]\n", ne_misc_dev.name, rc);

		ne_teardown_cpu_pool();

		param_set_copystring(error_val, kp);

		return rc;
	}

	return 0;
}

/**
 * ne_donated_cpu() - Check if the provided CPU is already used by the enclave.
 * @ne_enclave :	Private data associated with the current enclave.
 * @cpu:		CPU to check if already used.
 *
 * Context: Process context. This function is called with the ne_enclave mutex held.
 * Return:
 * * True if the provided CPU is already used by the enclave.
 * * False otherwise.
 */
static bool ne_donated_cpu(struct ne_enclave *ne_enclave, unsigned int cpu)
{
	if (cpumask_test_cpu(cpu, ne_enclave->vcpu_ids))
		return true;

	return false;
}

/**
 * ne_get_unused_core_from_cpu_pool() - Get the id of a full core from the
 *					NE CPU pool.
 * @void:	No parameters provided.
 *
 * Context: Process context. This function is called with the ne_enclave and
 *	    ne_cpu_pool mutexes held.
 * Return:
 * * Core id.
 * * -1 if no CPU core available in the pool.
 */
static int ne_get_unused_core_from_cpu_pool(void)
{
	int core_id = -1;
	unsigned int i = 0;

	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++)
		if (!cpumask_empty(ne_cpu_pool.avail_threads_per_core[i])) {
			core_id = i;

			break;
		}

	return core_id;
}

/**
 * ne_set_enclave_threads_per_core() - Set the threads of the provided core in
 *				       the enclave data structure.
 * @ne_enclave :	Private data associated with the current enclave.
 * @core_id:		Core id to get its threads from the NE CPU pool.
 * @vcpu_id:		vCPU id part of the provided core.
 *
 * Context: Process context. This function is called with the ne_enclave and
 *	    ne_cpu_pool mutexes held.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_set_enclave_threads_per_core(struct ne_enclave *ne_enclave,
					   int core_id, u32 vcpu_id)
{
	unsigned int cpu = 0;

	if (core_id < 0 && vcpu_id == 0) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "No CPUs available in NE CPU pool\n");

		return -NE_ERR_NO_CPUS_AVAIL_IN_POOL;
	}

	if (core_id < 0) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "CPU %d is not in NE CPU pool\n", vcpu_id);

		return -NE_ERR_VCPU_NOT_IN_CPU_POOL;
	}

	if (core_id >= ne_enclave->nr_parent_vm_cores) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "Invalid core id %d - ne_enclave\n", core_id);

		return -NE_ERR_VCPU_INVALID_CPU_CORE;
	}

	for_each_cpu(cpu, ne_cpu_pool.avail_threads_per_core[core_id])
		cpumask_set_cpu(cpu, ne_enclave->threads_per_core[core_id]);

	cpumask_clear(ne_cpu_pool.avail_threads_per_core[core_id]);

	return 0;
}

/**
 * ne_get_cpu_from_cpu_pool() - Get a CPU from the NE CPU pool, either from the
 *				remaining sibling(s) of a CPU core or the first
 *				sibling of a new CPU core.
 * @ne_enclave :	Private data associated with the current enclave.
 * @vcpu_id:		vCPU to get from the NE CPU pool.
 *
 * Context: Process context. This function is called with the ne_enclave mutex held.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_get_cpu_from_cpu_pool(struct ne_enclave *ne_enclave, u32 *vcpu_id)
{
	int core_id = -1;
	unsigned int cpu = 0;
	unsigned int i = 0;
	int rc = -EINVAL;

	/*
	 * If previously allocated a thread of a core to this enclave, first
	 * check remaining sibling(s) for new CPU allocations, so that full
	 * CPU cores are used for the enclave.
	 */
	for (i = 0; i < ne_enclave->nr_parent_vm_cores; i++)
		for_each_cpu(cpu, ne_enclave->threads_per_core[i])
			if (!ne_donated_cpu(ne_enclave, cpu)) {
				*vcpu_id = cpu;

				return 0;
			}

	mutex_lock(&ne_cpu_pool.mutex);

	/*
	 * If no remaining siblings, get a core from the NE CPU pool and keep
	 * track of all the threads in the enclave threads per core data structure.
	 */
	core_id = ne_get_unused_core_from_cpu_pool();

	rc = ne_set_enclave_threads_per_core(ne_enclave, core_id, *vcpu_id);
	if (rc < 0)
		goto unlock_mutex;

	*vcpu_id = cpumask_any(ne_enclave->threads_per_core[core_id]);

	rc = 0;

unlock_mutex:
	mutex_unlock(&ne_cpu_pool.mutex);

	return rc;
}

/**
 * ne_get_vcpu_core_from_cpu_pool() - Get from the NE CPU pool the id of the
 *				      core associated with the provided vCPU.
 * @vcpu_id:	Provided vCPU id to get its associated core id.
 *
 * Context: Process context. This function is called with the ne_enclave and
 *	    ne_cpu_pool mutexes held.
 * Return:
 * * Core id.
 * * -1 if the provided vCPU is not in the pool.
 */
static int ne_get_vcpu_core_from_cpu_pool(u32 vcpu_id)
{
	int core_id = -1;
	unsigned int i = 0;

	for (i = 0; i < ne_cpu_pool.nr_parent_vm_cores; i++)
		if (cpumask_test_cpu(vcpu_id, ne_cpu_pool.avail_threads_per_core[i])) {
			core_id = i;

			break;
	}

	return core_id;
}

/**
 * ne_check_cpu_in_cpu_pool() - Check if the given vCPU is in the available CPUs
 *				from the pool.
 * @ne_enclave :	Private data associated with the current enclave.
 * @vcpu_id:		ID of the vCPU to check if available in the NE CPU pool.
 *
 * Context: Process context. This function is called with the ne_enclave mutex held.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_check_cpu_in_cpu_pool(struct ne_enclave *ne_enclave, u32 vcpu_id)
{
	int core_id = -1;
	unsigned int i = 0;
	int rc = -EINVAL;

	if (ne_donated_cpu(ne_enclave, vcpu_id)) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "CPU %d already used\n", vcpu_id);

		return -NE_ERR_VCPU_ALREADY_USED;
	}

	/*
	 * If previously allocated a thread of a core to this enclave, but not
	 * the full core, first check remaining sibling(s).
	 */
	for (i = 0; i < ne_enclave->nr_parent_vm_cores; i++)
		if (cpumask_test_cpu(vcpu_id, ne_enclave->threads_per_core[i]))
			return 0;

	mutex_lock(&ne_cpu_pool.mutex);

	/*
	 * If no remaining siblings, get from the NE CPU pool the core
	 * associated with the vCPU and keep track of all the threads in the
	 * enclave threads per core data structure.
	 */
	core_id = ne_get_vcpu_core_from_cpu_pool(vcpu_id);

	rc = ne_set_enclave_threads_per_core(ne_enclave, core_id, vcpu_id);
	if (rc < 0)
		goto unlock_mutex;

	rc = 0;

unlock_mutex:
	mutex_unlock(&ne_cpu_pool.mutex);

	return rc;
}

/**
 * ne_add_vcpu_ioctl() - Add a vCPU to the slot associated with the current
 *			 enclave.
 * @ne_enclave :	Private data associated with the current enclave.
 * @vcpu_id:		ID of the CPU to be associated with the given slot,
 *			apic id on x86.
 *
 * Context: Process context. This function is called with the ne_enclave mutex held.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_add_vcpu_ioctl(struct ne_enclave *ne_enclave, u32 vcpu_id)
{
	struct ne_pci_dev_cmd_reply cmd_reply = {};
	struct pci_dev *pdev = ne_devs.ne_pci_dev->pdev;
	int rc = -EINVAL;
	struct slot_add_vcpu_req slot_add_vcpu_req = {};

	if (ne_enclave->mm != current->mm)
		return -EIO;

	slot_add_vcpu_req.slot_uid = ne_enclave->slot_uid;
	slot_add_vcpu_req.vcpu_id = vcpu_id;

	rc = ne_do_request(pdev, SLOT_ADD_VCPU,
			   &slot_add_vcpu_req, sizeof(slot_add_vcpu_req),
			   &cmd_reply, sizeof(cmd_reply));
	if (rc < 0) {
		dev_err_ratelimited(ne_misc_dev.this_device,
				    "Error in slot add vCPU [rc=%d]\n", rc);

		return rc;
	}

	cpumask_set_cpu(vcpu_id, ne_enclave->vcpu_ids);

	ne_enclave->nr_vcpus++;

	return 0;
}

/**
 * ne_enclave_ioctl() - Ioctl function provided by the enclave file.
 * @file:	File associated with this ioctl function.
 * @cmd:	The command that is set for the ioctl call.
 * @arg:	The argument that is provided for the ioctl call.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static long ne_enclave_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ne_enclave *ne_enclave = file->private_data;

	switch (cmd) {
	case NE_ADD_VCPU: {
		int rc = -EINVAL;
		u32 vcpu_id = 0;

		if (copy_from_user(&vcpu_id, (void __user *)arg, sizeof(vcpu_id)))
			return -EFAULT;

		mutex_lock(&ne_enclave->enclave_info_mutex);

		if (ne_enclave->state != NE_STATE_INIT) {
			dev_err_ratelimited(ne_misc_dev.this_device,
					    "Enclave is not in init state\n");

			mutex_unlock(&ne_enclave->enclave_info_mutex);

			return -NE_ERR_NOT_IN_INIT_STATE;
		}

		if (vcpu_id >= (ne_enclave->nr_parent_vm_cores *
		    ne_enclave->nr_threads_per_core)) {
			dev_err_ratelimited(ne_misc_dev.this_device,
					    "vCPU id higher than max CPU id\n");

			mutex_unlock(&ne_enclave->enclave_info_mutex);

			return -NE_ERR_INVALID_VCPU;
		}

		if (!vcpu_id) {
			/* Use the CPU pool for choosing a CPU for the enclave. */
			rc = ne_get_cpu_from_cpu_pool(ne_enclave, &vcpu_id);
			if (rc < 0) {
				dev_err_ratelimited(ne_misc_dev.this_device,
						    "Error in get CPU from pool [rc=%d]\n",
						    rc);

				mutex_unlock(&ne_enclave->enclave_info_mutex);

				return rc;
			}
		} else {
			/* Check if the provided vCPU is available in the NE CPU pool. */
			rc = ne_check_cpu_in_cpu_pool(ne_enclave, vcpu_id);
			if (rc < 0) {
				dev_err_ratelimited(ne_misc_dev.this_device,
						    "Error in check CPU %d in pool [rc=%d]\n",
						    vcpu_id, rc);

				mutex_unlock(&ne_enclave->enclave_info_mutex);

				return rc;
			}
		}

		rc = ne_add_vcpu_ioctl(ne_enclave, vcpu_id);
		if (rc < 0) {
			mutex_unlock(&ne_enclave->enclave_info_mutex);

			return rc;
		}

		mutex_unlock(&ne_enclave->enclave_info_mutex);

		if (copy_to_user((void __user *)arg, &vcpu_id, sizeof(vcpu_id)))
			return -EFAULT;

		return 0;
	}

	case NE_GET_IMAGE_LOAD_INFO: {
		struct ne_image_load_info image_load_info = {};

		if (copy_from_user(&image_load_info, (void __user *)arg, sizeof(image_load_info)))
			return -EFAULT;

		mutex_lock(&ne_enclave->enclave_info_mutex);

		if (ne_enclave->state != NE_STATE_INIT) {
			dev_err_ratelimited(ne_misc_dev.this_device,
					    "Enclave is not in init state\n");

			mutex_unlock(&ne_enclave->enclave_info_mutex);

			return -NE_ERR_NOT_IN_INIT_STATE;
		}

		mutex_unlock(&ne_enclave->enclave_info_mutex);

		if (!image_load_info.flags ||
		    image_load_info.flags >= NE_IMAGE_LOAD_MAX_FLAG_VAL) {
			dev_err_ratelimited(ne_misc_dev.this_device,
					    "Incorrect flag in enclave image load info\n");

			return -NE_ERR_INVALID_FLAG_VALUE;
		}

		if (image_load_info.flags == NE_EIF_IMAGE)
			image_load_info.memory_offset = NE_EIF_LOAD_OFFSET;

		if (copy_to_user((void __user *)arg, &image_load_info, sizeof(image_load_info)))
			return -EFAULT;

		return 0;
	}

	default:
		return -ENOTTY;
	}

	return 0;
}

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
	.unlocked_ioctl	= ne_enclave_ioctl,
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

	ne_teardown_cpu_pool();
}

module_init(ne_init);
module_exit(ne_exit);

MODULE_AUTHOR("Amazon.com, Inc. or its affiliates");
MODULE_DESCRIPTION("Nitro Enclaves Driver");
MODULE_LICENSE("GPL v2");
