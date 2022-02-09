/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <uapi/linux/kfd_ioctl.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ptrace.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <asm/processor.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_svm.h"
#include "amdgpu_amdkfd.h"
#include "kfd_smi_events.h"
#include "amdgpu_dma_buf.h"

static long kfd_ioctl(struct file *, unsigned int, unsigned long);
static int kfd_open(struct inode *, struct file *);
static int kfd_release(struct inode *, struct file *);
static int kfd_mmap(struct file *, struct vm_area_struct *);

static const char kfd_dev_name[] = "kfd";

static const struct file_operations kfd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kfd_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open = kfd_open,
	.release = kfd_release,
	.mmap = kfd_mmap,
};

static int kfd_char_dev_major = -1;
static struct class *kfd_class;
struct device *kfd_device;

int kfd_chardev_init(void)
{
	int err = 0;

	kfd_char_dev_major = register_chrdev(0, kfd_dev_name, &kfd_fops);
	err = kfd_char_dev_major;
	if (err < 0)
		goto err_register_chrdev;

	kfd_class = class_create(THIS_MODULE, kfd_dev_name);
	err = PTR_ERR(kfd_class);
	if (IS_ERR(kfd_class))
		goto err_class_create;

	kfd_device = device_create(kfd_class, NULL,
					MKDEV(kfd_char_dev_major, 0),
					NULL, kfd_dev_name);
	err = PTR_ERR(kfd_device);
	if (IS_ERR(kfd_device))
		goto err_device_create;

	return 0;

err_device_create:
	class_destroy(kfd_class);
err_class_create:
	unregister_chrdev(kfd_char_dev_major, kfd_dev_name);
err_register_chrdev:
	return err;
}

void kfd_chardev_exit(void)
{
	device_destroy(kfd_class, MKDEV(kfd_char_dev_major, 0));
	class_destroy(kfd_class);
	unregister_chrdev(kfd_char_dev_major, kfd_dev_name);
	kfd_device = NULL;
}

struct device *kfd_chardev(void)
{
	return kfd_device;
}


static int kfd_open(struct inode *inode, struct file *filep)
{
	struct kfd_process *process;
	bool is_32bit_user_mode;

	if (iminor(inode) != 0)
		return -ENODEV;

	is_32bit_user_mode = in_compat_syscall();

	if (is_32bit_user_mode) {
		dev_warn(kfd_device,
			"Process %d (32-bit) failed to open /dev/kfd\n"
			"32-bit processes are not supported by amdkfd\n",
			current->pid);
		return -EPERM;
	}

	process = kfd_create_process(filep);
	if (IS_ERR(process))
		return PTR_ERR(process);

	if (kfd_is_locked()) {
		dev_dbg(kfd_device, "kfd is locked!\n"
				"process %d unreferenced", process->pasid);
		kfd_unref_process(process);
		return -EAGAIN;
	}

	/* filep now owns the reference returned by kfd_create_process */
	filep->private_data = process;

	dev_dbg(kfd_device, "process %d opened, compat mode (32 bit) - %d\n",
		process->pasid, process->is_32bit_user_mode);

	return 0;
}

static int kfd_release(struct inode *inode, struct file *filep)
{
	struct kfd_process *process = filep->private_data;

	if (process)
		kfd_unref_process(process);

	return 0;
}

static int kfd_ioctl_get_version(struct file *filep, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_get_version_args *args = data;

	args->major_version = KFD_IOCTL_MAJOR_VERSION;
	args->minor_version = KFD_IOCTL_MINOR_VERSION;

	return 0;
}

static int set_queue_properties_from_user(struct queue_properties *q_properties,
				struct kfd_ioctl_create_queue_args *args)
{
	if (args->queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("Queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("Queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok((const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("Can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("Ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	if (!access_ok((const void __user *) args->read_pointer_address,
			sizeof(uint32_t))) {
		pr_err("Can't access read pointer\n");
		return -EFAULT;
	}

	if (!access_ok((const void __user *) args->write_pointer_address,
			sizeof(uint32_t))) {
		pr_err("Can't access write pointer\n");
		return -EFAULT;
	}

	if (args->eop_buffer_address &&
		!access_ok((const void __user *) args->eop_buffer_address,
			sizeof(uint32_t))) {
		pr_debug("Can't access eop buffer");
		return -EFAULT;
	}

	if (args->ctx_save_restore_address &&
		!access_ok((const void __user *) args->ctx_save_restore_address,
			sizeof(uint32_t))) {
		pr_debug("Can't access ctx save restore buffer");
		return -EFAULT;
	}

	q_properties->is_interop = false;
	q_properties->is_gws = false;
	q_properties->queue_percent = args->queue_percentage;
	q_properties->priority = args->queue_priority;
	q_properties->queue_address = args->ring_base_address;
	q_properties->queue_size = args->ring_size;
	q_properties->read_ptr = (uint32_t *) args->read_pointer_address;
	q_properties->write_ptr = (uint32_t *) args->write_pointer_address;
	q_properties->eop_ring_buffer_address = args->eop_buffer_address;
	q_properties->eop_ring_buffer_size = args->eop_buffer_size;
	q_properties->ctx_save_restore_area_address =
			args->ctx_save_restore_address;
	q_properties->ctx_save_restore_area_size = args->ctx_save_restore_size;
	q_properties->ctl_stack_size = args->ctl_stack_size;
	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE ||
		args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->type = KFD_QUEUE_TYPE_COMPUTE;
	else if (args->queue_type == KFD_IOC_QUEUE_TYPE_SDMA)
		q_properties->type = KFD_QUEUE_TYPE_SDMA;
	else if (args->queue_type == KFD_IOC_QUEUE_TYPE_SDMA_XGMI)
		q_properties->type = KFD_QUEUE_TYPE_SDMA_XGMI;
	else
		return -ENOTSUPP;

	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->format = KFD_QUEUE_FORMAT_AQL;
	else
		q_properties->format = KFD_QUEUE_FORMAT_PM4;

	pr_debug("Queue Percentage: %d, %d\n",
			q_properties->queue_percent, args->queue_percentage);

	pr_debug("Queue Priority: %d, %d\n",
			q_properties->priority, args->queue_priority);

	pr_debug("Queue Address: 0x%llX, 0x%llX\n",
			q_properties->queue_address, args->ring_base_address);

	pr_debug("Queue Size: 0x%llX, %u\n",
			q_properties->queue_size, args->ring_size);

	pr_debug("Queue r/w Pointers: %px, %px\n",
			q_properties->read_ptr,
			q_properties->write_ptr);

	pr_debug("Queue Format: %d\n", q_properties->format);

	pr_debug("Queue EOP: 0x%llX\n", q_properties->eop_ring_buffer_address);

	pr_debug("Queue CTX save area: 0x%llX\n",
			q_properties->ctx_save_restore_area_address);

	return 0;
}

static int kfd_ioctl_create_queue(struct file *filep, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_create_queue_args *args = data;
	struct kfd_dev *dev;
	int err = 0;
	unsigned int queue_id;
	struct kfd_process_device *pdd;
	struct queue_properties q_properties;
	uint32_t doorbell_offset_in_process = 0;

	memset(&q_properties, 0, sizeof(struct queue_properties));

	pr_debug("Creating queue ioctl\n");

	err = set_queue_properties_from_user(&q_properties, args);
	if (err)
		return err;

	pr_debug("Looking for gpu id 0x%x\n", args->gpu_id);

	mutex_lock(&p->mutex);

	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		err = -EINVAL;
		goto err_pdd;
	}
	dev = pdd->dev;

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto err_bind_process;
	}

	pr_debug("Creating queue for PASID 0x%x on gpu 0x%x\n",
			p->pasid,
			dev->id);

	err = pqm_create_queue(&p->pqm, dev, filep, &q_properties, &queue_id, NULL, NULL, NULL,
			&doorbell_offset_in_process);
	if (err != 0)
		goto err_create_queue;

	args->queue_id = queue_id;


	/* Return gpu_id as doorbell offset for mmap usage */
	args->doorbell_offset = KFD_MMAP_TYPE_DOORBELL;
	args->doorbell_offset |= KFD_MMAP_GPU_ID(args->gpu_id);
	if (KFD_IS_SOC15(dev))
		/* On SOC15 ASICs, include the doorbell offset within the
		 * process doorbell frame, which is 2 pages.
		 */
		args->doorbell_offset |= doorbell_offset_in_process;

	mutex_unlock(&p->mutex);

	pr_debug("Queue id %d was created successfully\n", args->queue_id);

	pr_debug("Ring buffer address == 0x%016llX\n",
			args->ring_base_address);

	pr_debug("Read ptr address    == 0x%016llX\n",
			args->read_pointer_address);

	pr_debug("Write ptr address   == 0x%016llX\n",
			args->write_pointer_address);

	return 0;

err_create_queue:
err_bind_process:
err_pdd:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_destroy_queue(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	struct kfd_ioctl_destroy_queue_args *args = data;

	pr_debug("Destroying queue id %d for pasid 0x%x\n",
				args->queue_id,
				p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_destroy_queue(&p->pqm, args->queue_id);

	mutex_unlock(&p->mutex);
	return retval;
}

static int kfd_ioctl_update_queue(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	struct kfd_ioctl_update_queue_args *args = data;
	struct queue_properties properties;

	if (args->queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("Queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("Queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok((const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("Can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("Ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	properties.queue_address = args->ring_base_address;
	properties.queue_size = args->ring_size;
	properties.queue_percent = args->queue_percentage;
	properties.priority = args->queue_priority;

	pr_debug("Updating queue id %d for pasid 0x%x\n",
			args->queue_id, p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_update_queue_properties(&p->pqm, args->queue_id, &properties);

	mutex_unlock(&p->mutex);

	return retval;
}

static int kfd_ioctl_set_cu_mask(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	const int max_num_cus = 1024;
	struct kfd_ioctl_set_cu_mask_args *args = data;
	struct mqd_update_info minfo = {0};
	uint32_t __user *cu_mask_ptr = (uint32_t __user *)args->cu_mask_ptr;
	size_t cu_mask_size = sizeof(uint32_t) * (args->num_cu_mask / 32);

	if ((args->num_cu_mask % 32) != 0) {
		pr_debug("num_cu_mask 0x%x must be a multiple of 32",
				args->num_cu_mask);
		return -EINVAL;
	}

	minfo.cu_mask.count = args->num_cu_mask;
	if (minfo.cu_mask.count == 0) {
		pr_debug("CU mask cannot be 0");
		return -EINVAL;
	}

	/* To prevent an unreasonably large CU mask size, set an arbitrary
	 * limit of max_num_cus bits.  We can then just drop any CU mask bits
	 * past max_num_cus bits and just use the first max_num_cus bits.
	 */
	if (minfo.cu_mask.count > max_num_cus) {
		pr_debug("CU mask cannot be greater than 1024 bits");
		minfo.cu_mask.count = max_num_cus;
		cu_mask_size = sizeof(uint32_t) * (max_num_cus/32);
	}

	minfo.cu_mask.ptr = kzalloc(cu_mask_size, GFP_KERNEL);
	if (!minfo.cu_mask.ptr)
		return -ENOMEM;

	retval = copy_from_user(minfo.cu_mask.ptr, cu_mask_ptr, cu_mask_size);
	if (retval) {
		pr_debug("Could not copy CU mask from userspace");
		retval = -EFAULT;
		goto out;
	}

	minfo.update_flag = UPDATE_FLAG_CU_MASK;

	mutex_lock(&p->mutex);

	retval = pqm_update_mqd(&p->pqm, args->queue_id, &minfo);

	mutex_unlock(&p->mutex);

out:
	kfree(minfo.cu_mask.ptr);
	return retval;
}

static int kfd_ioctl_get_queue_wave_state(struct file *filep,
					  struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_queue_wave_state_args *args = data;
	int r;

	mutex_lock(&p->mutex);

	r = pqm_get_wave_state(&p->pqm, args->queue_id,
			       (void __user *)args->ctl_stack_address,
			       &args->ctl_stack_used_size,
			       &args->save_area_used_size);

	mutex_unlock(&p->mutex);

	return r;
}

static int kfd_ioctl_set_memory_policy(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_memory_policy_args *args = data;
	int err = 0;
	struct kfd_process_device *pdd;
	enum cache_policy default_policy, alternate_policy;

	if (args->default_policy != KFD_IOC_CACHE_POLICY_COHERENT
	    && args->default_policy != KFD_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	if (args->alternate_policy != KFD_IOC_CACHE_POLICY_COHERENT
	    && args->alternate_policy != KFD_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		err = -EINVAL;
		goto err_pdd;
	}

	pdd = kfd_bind_process_to_device(pdd->dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	default_policy = (args->default_policy == KFD_IOC_CACHE_POLICY_COHERENT)
			 ? cache_policy_coherent : cache_policy_noncoherent;

	alternate_policy =
		(args->alternate_policy == KFD_IOC_CACHE_POLICY_COHERENT)
		   ? cache_policy_coherent : cache_policy_noncoherent;

	if (!pdd->dev->dqm->ops.set_cache_memory_policy(pdd->dev->dqm,
				&pdd->qpd,
				default_policy,
				alternate_policy,
				(void __user *)args->alternate_aperture_base,
				args->alternate_aperture_size))
		err = -EINVAL;

out:
err_pdd:
	mutex_unlock(&p->mutex);

	return err;
}

static int kfd_ioctl_set_trap_handler(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_trap_handler_args *args = data;
	int err = 0;
	struct kfd_process_device *pdd;

	mutex_lock(&p->mutex);

	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		err = -EINVAL;
		goto err_pdd;
	}

	pdd = kfd_bind_process_to_device(pdd->dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	kfd_process_set_trap_handler(&pdd->qpd, args->tba_addr, args->tma_addr);

out:
err_pdd:
	mutex_unlock(&p->mutex);

	return err;
}

static int kfd_ioctl_dbg_register(struct file *filep,
				struct kfd_process *p, void *data)
{
	return -EPERM;
}

static int kfd_ioctl_dbg_unregister(struct file *filep,
				struct kfd_process *p, void *data)
{
	return -EPERM;
}

static int kfd_ioctl_dbg_address_watch(struct file *filep,
					struct kfd_process *p, void *data)
{
	return -EPERM;
}

/* Parse and generate fixed size data structure for wave control */
static int kfd_ioctl_dbg_wave_control(struct file *filep,
					struct kfd_process *p, void *data)
{
	return -EPERM;
}

static int kfd_ioctl_get_clock_counters(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_clock_counters_args *args = data;
	struct kfd_process_device *pdd;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	mutex_unlock(&p->mutex);
	if (pdd)
		/* Reading GPU clock counter from KGD */
		args->gpu_clock_counter = amdgpu_amdkfd_get_gpu_clock_counter(pdd->dev->adev);
	else
		/* Node without GPU resource */
		args->gpu_clock_counter = 0;

	/* No access to rdtsc. Using raw monotonic time */
	args->cpu_clock_counter = ktime_get_raw_ns();
	args->system_clock_counter = ktime_get_boottime_ns();

	/* Since the counter is in nano-seconds we use 1GHz frequency */
	args->system_clock_freq = 1000000000;

	return 0;
}


static int kfd_ioctl_get_process_apertures(struct file *filp,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_process_apertures_args *args = data;
	struct kfd_process_device_apertures *pAperture;
	int i;

	dev_dbg(kfd_device, "get apertures for PASID 0x%x", p->pasid);

	args->num_of_nodes = 0;

	mutex_lock(&p->mutex);
	/* Run over all pdd of the process */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		pAperture =
			&args->process_apertures[args->num_of_nodes];
		pAperture->gpu_id = pdd->dev->id;
		pAperture->lds_base = pdd->lds_base;
		pAperture->lds_limit = pdd->lds_limit;
		pAperture->gpuvm_base = pdd->gpuvm_base;
		pAperture->gpuvm_limit = pdd->gpuvm_limit;
		pAperture->scratch_base = pdd->scratch_base;
		pAperture->scratch_limit = pdd->scratch_limit;

		dev_dbg(kfd_device,
			"node id %u\n", args->num_of_nodes);
		dev_dbg(kfd_device,
			"gpu id %u\n", pdd->dev->id);
		dev_dbg(kfd_device,
			"lds_base %llX\n", pdd->lds_base);
		dev_dbg(kfd_device,
			"lds_limit %llX\n", pdd->lds_limit);
		dev_dbg(kfd_device,
			"gpuvm_base %llX\n", pdd->gpuvm_base);
		dev_dbg(kfd_device,
			"gpuvm_limit %llX\n", pdd->gpuvm_limit);
		dev_dbg(kfd_device,
			"scratch_base %llX\n", pdd->scratch_base);
		dev_dbg(kfd_device,
			"scratch_limit %llX\n", pdd->scratch_limit);

		if (++args->num_of_nodes >= NUM_OF_SUPPORTED_GPUS)
			break;
	}
	mutex_unlock(&p->mutex);

	return 0;
}

static int kfd_ioctl_get_process_apertures_new(struct file *filp,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_process_apertures_new_args *args = data;
	struct kfd_process_device_apertures *pa;
	int ret;
	int i;

	dev_dbg(kfd_device, "get apertures for PASID 0x%x", p->pasid);

	if (args->num_of_nodes == 0) {
		/* Return number of nodes, so that user space can alloacate
		 * sufficient memory
		 */
		mutex_lock(&p->mutex);
		args->num_of_nodes = p->n_pdds;
		goto out_unlock;
	}

	/* Fill in process-aperture information for all available
	 * nodes, but not more than args->num_of_nodes as that is
	 * the amount of memory allocated by user
	 */
	pa = kzalloc((sizeof(struct kfd_process_device_apertures) *
				args->num_of_nodes), GFP_KERNEL);
	if (!pa)
		return -ENOMEM;

	mutex_lock(&p->mutex);

	if (!p->n_pdds) {
		args->num_of_nodes = 0;
		kfree(pa);
		goto out_unlock;
	}

	/* Run over all pdd of the process */
	for (i = 0; i < min(p->n_pdds, args->num_of_nodes); i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		pa[i].gpu_id = pdd->dev->id;
		pa[i].lds_base = pdd->lds_base;
		pa[i].lds_limit = pdd->lds_limit;
		pa[i].gpuvm_base = pdd->gpuvm_base;
		pa[i].gpuvm_limit = pdd->gpuvm_limit;
		pa[i].scratch_base = pdd->scratch_base;
		pa[i].scratch_limit = pdd->scratch_limit;

		dev_dbg(kfd_device,
			"gpu id %u\n", pdd->dev->id);
		dev_dbg(kfd_device,
			"lds_base %llX\n", pdd->lds_base);
		dev_dbg(kfd_device,
			"lds_limit %llX\n", pdd->lds_limit);
		dev_dbg(kfd_device,
			"gpuvm_base %llX\n", pdd->gpuvm_base);
		dev_dbg(kfd_device,
			"gpuvm_limit %llX\n", pdd->gpuvm_limit);
		dev_dbg(kfd_device,
			"scratch_base %llX\n", pdd->scratch_base);
		dev_dbg(kfd_device,
			"scratch_limit %llX\n", pdd->scratch_limit);
	}
	mutex_unlock(&p->mutex);

	args->num_of_nodes = i;
	ret = copy_to_user(
			(void __user *)args->kfd_process_device_apertures_ptr,
			pa,
			(i * sizeof(struct kfd_process_device_apertures)));
	kfree(pa);
	return ret ? -EFAULT : 0;

out_unlock:
	mutex_unlock(&p->mutex);
	return 0;
}

static int kfd_ioctl_create_event(struct file *filp, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_create_event_args *args = data;
	int err;

	/* For dGPUs the event page is allocated in user mode. The
	 * handle is passed to KFD with the first call to this IOCTL
	 * through the event_page_offset field.
	 */
	if (args->event_page_offset) {
		mutex_lock(&p->mutex);
		err = kfd_kmap_event_page(p, args->event_page_offset);
		mutex_unlock(&p->mutex);
		if (err)
			return err;
	}

	err = kfd_event_create(filp, p, args->event_type,
				args->auto_reset != 0, args->node_id,
				&args->event_id, &args->event_trigger_data,
				&args->event_page_offset,
				&args->event_slot_index);

	pr_debug("Created event (id:0x%08x) (%s)\n", args->event_id, __func__);
	return err;
}

static int kfd_ioctl_destroy_event(struct file *filp, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_destroy_event_args *args = data;

	return kfd_event_destroy(p, args->event_id);
}

static int kfd_ioctl_set_event(struct file *filp, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_set_event_args *args = data;

	return kfd_set_event(p, args->event_id);
}

static int kfd_ioctl_reset_event(struct file *filp, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_reset_event_args *args = data;

	return kfd_reset_event(p, args->event_id);
}

static int kfd_ioctl_wait_events(struct file *filp, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_wait_events_args *args = data;
	int err;

	err = kfd_wait_on_events(p, args->num_events,
			(void __user *)args->events_ptr,
			(args->wait_for_all != 0),
			args->timeout, &args->wait_result);

	return err;
}
static int kfd_ioctl_set_scratch_backing_va(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_scratch_backing_va_args *args = data;
	struct kfd_process_device *pdd;
	struct kfd_dev *dev;
	long err;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		err = -EINVAL;
		goto err_pdd;
	}
	dev = pdd->dev;

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_fail;
	}

	pdd->qpd.sh_hidden_private_base = args->va_addr;

	mutex_unlock(&p->mutex);

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS &&
	    pdd->qpd.vmid != 0 && dev->kfd2kgd->set_scratch_backing_va)
		dev->kfd2kgd->set_scratch_backing_va(
			dev->adev, args->va_addr, pdd->qpd.vmid);

	return 0;

bind_process_to_device_fail:
err_pdd:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_get_tile_config(struct file *filep,
		struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_tile_config_args *args = data;
	struct kfd_process_device *pdd;
	struct tile_config config;
	int err = 0;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	mutex_unlock(&p->mutex);
	if (!pdd)
		return -EINVAL;

	amdgpu_amdkfd_get_tile_config(pdd->dev->adev, &config);

	args->gb_addr_config = config.gb_addr_config;
	args->num_banks = config.num_banks;
	args->num_ranks = config.num_ranks;

	if (args->num_tile_configs > config.num_tile_configs)
		args->num_tile_configs = config.num_tile_configs;
	err = copy_to_user((void __user *)args->tile_config_ptr,
			config.tile_config_ptr,
			args->num_tile_configs * sizeof(uint32_t));
	if (err) {
		args->num_tile_configs = 0;
		return -EFAULT;
	}

	if (args->num_macro_tile_configs > config.num_macro_tile_configs)
		args->num_macro_tile_configs =
				config.num_macro_tile_configs;
	err = copy_to_user((void __user *)args->macro_tile_config_ptr,
			config.macro_tile_config_ptr,
			args->num_macro_tile_configs * sizeof(uint32_t));
	if (err) {
		args->num_macro_tile_configs = 0;
		return -EFAULT;
	}

	return 0;
}

static int kfd_ioctl_acquire_vm(struct file *filep, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_acquire_vm_args *args = data;
	struct kfd_process_device *pdd;
	struct file *drm_file;
	int ret;

	drm_file = fget(args->drm_fd);
	if (!drm_file)
		return -EINVAL;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		ret = -EINVAL;
		goto err_pdd;
	}

	if (pdd->drm_file) {
		ret = pdd->drm_file == drm_file ? 0 : -EBUSY;
		goto err_drm_file;
	}

	ret = kfd_process_device_init_vm(pdd, drm_file);
	if (ret)
		goto err_unlock;

	/* On success, the PDD keeps the drm_file reference */
	mutex_unlock(&p->mutex);

	return 0;

err_unlock:
err_pdd:
err_drm_file:
	mutex_unlock(&p->mutex);
	fput(drm_file);
	return ret;
}

bool kfd_dev_is_large_bar(struct kfd_dev *dev)
{
	struct kfd_local_mem_info mem_info;

	if (debug_largebar) {
		pr_debug("Simulate large-bar allocation on non large-bar machine\n");
		return true;
	}

	if (dev->use_iommu_v2)
		return false;

	amdgpu_amdkfd_get_local_mem_info(dev->adev, &mem_info);
	if (mem_info.local_mem_size_private == 0 &&
			mem_info.local_mem_size_public > 0)
		return true;
	return false;
}

static int kfd_ioctl_alloc_memory_of_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_alloc_memory_of_gpu_args *args = data;
	struct kfd_process_device *pdd;
	void *mem;
	struct kfd_dev *dev;
	int idr_handle;
	long err;
	uint64_t offset = args->mmap_offset;
	uint32_t flags = args->flags;

	if (args->size == 0)
		return -EINVAL;

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)
	/* Flush pending deferred work to avoid racing with deferred actions
	 * from previous memory map changes (e.g. munmap).
	 */
	svm_range_list_lock_and_flush_work(&p->svms, current->mm);
	mutex_lock(&p->svms.lock);
	mmap_write_unlock(current->mm);
	if (interval_tree_iter_first(&p->svms.objects,
				     args->va_addr >> PAGE_SHIFT,
				     (args->va_addr + args->size - 1) >> PAGE_SHIFT)) {
		pr_err("Address: 0x%llx already allocated by SVM\n",
			args->va_addr);
		mutex_unlock(&p->svms.lock);
		return -EADDRINUSE;
	}
	mutex_unlock(&p->svms.lock);
#endif
	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		err = -EINVAL;
		goto err_pdd;
	}

	dev = pdd->dev;

	if ((flags & KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC) &&
		(flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) &&
		!kfd_dev_is_large_bar(dev)) {
		pr_err("Alloc host visible vram on small bar is not allowed\n");
		err = -EINVAL;
		goto err_large_bar;
	}

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto err_unlock;
	}

	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL) {
		if (args->size != kfd_doorbell_process_slice(dev)) {
			err = -EINVAL;
			goto err_unlock;
		}
		offset = kfd_get_process_doorbells(pdd);
	} else if (flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
		if (args->size != PAGE_SIZE) {
			err = -EINVAL;
			goto err_unlock;
		}
		offset = dev->adev->rmmio_remap.bus_addr;
		if (!offset) {
			err = -ENOMEM;
			goto err_unlock;
		}
	}

	err = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		dev->adev, args->va_addr, args->size,
		pdd->drm_priv, (struct kgd_mem **) &mem, &offset,
		flags, false);

	if (err)
		goto err_unlock;

	idr_handle = kfd_process_device_create_obj_handle(pdd, mem);
	if (idr_handle < 0) {
		err = -EFAULT;
		goto err_free;
	}

	/* Update the VRAM usage count */
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
		WRITE_ONCE(pdd->vram_usage, pdd->vram_usage + args->size);

	mutex_unlock(&p->mutex);

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);
	args->mmap_offset = offset;

	/* MMIO is mapped through kfd device
	 * Generate a kfd mmap offset
	 */
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)
		args->mmap_offset = KFD_MMAP_TYPE_MMIO
					| KFD_MMAP_GPU_ID(args->gpu_id);

	return 0;

err_free:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->adev, (struct kgd_mem *)mem,
					       pdd->drm_priv, NULL);
err_unlock:
err_pdd:
err_large_bar:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_free_memory_of_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_free_memory_of_gpu_args *args = data;
	struct kfd_process_device *pdd;
	void *mem;
	int ret;
	uint64_t size = 0;

	mutex_lock(&p->mutex);
	/*
	 * Safeguard to prevent user space from freeing signal BO.
	 * It will be freed at process termination.
	 */
	if (p->signal_handle && (p->signal_handle == args->handle)) {
		pr_err("Free signal BO is not allowed\n");
		ret = -EPERM;
		goto err_unlock;
	}

	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(args->handle));
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		ret = -EINVAL;
		goto err_pdd;
	}

	mem = kfd_process_device_translate_handle(
		pdd, GET_IDR_HANDLE(args->handle));
	if (!mem) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = amdgpu_amdkfd_gpuvm_free_memory_of_gpu(pdd->dev->adev,
				(struct kgd_mem *)mem, pdd->drm_priv, &size);

	/* If freeing the buffer failed, leave the handle in place for
	 * clean-up during process tear-down.
	 */
	if (!ret)
		kfd_process_device_remove_obj_handle(
			pdd, GET_IDR_HANDLE(args->handle));

	WRITE_ONCE(pdd->vram_usage, pdd->vram_usage - size);

err_unlock:
err_pdd:
	mutex_unlock(&p->mutex);
	return ret;
}

static bool kfd_flush_tlb_after_unmap(struct kfd_dev *dev) {
	return KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 2) ||
	       (KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 1) &&
	        dev->adev->sdma.instance[0].fw_version >= 18) ||
	       KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 0);
}

static int kfd_ioctl_map_memory_to_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_map_memory_to_gpu_args *args = data;
	struct kfd_process_device *pdd, *peer_pdd;
	void *mem;
	struct kfd_dev *dev;
	long err = 0;
	int i;
	uint32_t *devices_arr = NULL;
	bool table_freed = false;

	if (!args->n_devices) {
		pr_debug("Device IDs array empty\n");
		return -EINVAL;
	}
	if (args->n_success > args->n_devices) {
		pr_debug("n_success exceeds n_devices\n");
		return -EINVAL;
	}

	devices_arr = kmalloc_array(args->n_devices, sizeof(*devices_arr),
				    GFP_KERNEL);
	if (!devices_arr)
		return -ENOMEM;

	err = copy_from_user(devices_arr,
			     (void __user *)args->device_ids_array_ptr,
			     args->n_devices * sizeof(*devices_arr));
	if (err != 0) {
		err = -EFAULT;
		goto copy_from_user_failed;
	}

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(args->handle));
	if (!pdd) {
		err = -EINVAL;
		goto get_process_device_data_failed;
	}
	dev = pdd->dev;

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_failed;
	}

	mem = kfd_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	if (!mem) {
		err = -ENOMEM;
		goto get_mem_obj_from_handle_failed;
	}

	for (i = args->n_success; i < args->n_devices; i++) {
		peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
		if (!peer_pdd) {
			pr_debug("Getting device by id failed for 0x%x\n",
				 devices_arr[i]);
			err = -EINVAL;
			goto get_mem_obj_from_handle_failed;
		}

		peer_pdd = kfd_bind_process_to_device(peer_pdd->dev, p);
		if (IS_ERR(peer_pdd)) {
			err = PTR_ERR(peer_pdd);
			goto get_mem_obj_from_handle_failed;
		}

		err = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
			peer_pdd->dev->adev, (struct kgd_mem *)mem,
			peer_pdd->drm_priv, &table_freed);
		if (err) {
			pr_err("Failed to map to gpu %d/%d\n",
			       i, args->n_devices);
			goto map_memory_to_gpu_failed;
		}
		args->n_success = i+1;
	}

	mutex_unlock(&p->mutex);

	err = amdgpu_amdkfd_gpuvm_sync_memory(dev->adev, (struct kgd_mem *) mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	/* Flush TLBs after waiting for the page table updates to complete */
	if (table_freed || !kfd_flush_tlb_after_unmap(dev)) {
		for (i = 0; i < args->n_devices; i++) {
			peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
			if (WARN_ON_ONCE(!peer_pdd))
				continue;
			kfd_flush_tlb(peer_pdd, TLB_FLUSH_LEGACY);
		}
	}
	kfree(devices_arr);

	return err;

get_process_device_data_failed:
bind_process_to_device_failed:
get_mem_obj_from_handle_failed:
map_memory_to_gpu_failed:
	mutex_unlock(&p->mutex);
copy_from_user_failed:
sync_memory_failed:
	kfree(devices_arr);

	return err;
}

static int kfd_ioctl_unmap_memory_from_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_unmap_memory_from_gpu_args *args = data;
	struct kfd_process_device *pdd, *peer_pdd;
	void *mem;
	long err = 0;
	uint32_t *devices_arr = NULL, i;

	if (!args->n_devices) {
		pr_debug("Device IDs array empty\n");
		return -EINVAL;
	}
	if (args->n_success > args->n_devices) {
		pr_debug("n_success exceeds n_devices\n");
		return -EINVAL;
	}

	devices_arr = kmalloc_array(args->n_devices, sizeof(*devices_arr),
				    GFP_KERNEL);
	if (!devices_arr)
		return -ENOMEM;

	err = copy_from_user(devices_arr,
			     (void __user *)args->device_ids_array_ptr,
			     args->n_devices * sizeof(*devices_arr));
	if (err != 0) {
		err = -EFAULT;
		goto copy_from_user_failed;
	}

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(args->handle));
	if (!pdd) {
		err = -EINVAL;
		goto bind_process_to_device_failed;
	}

	mem = kfd_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	if (!mem) {
		err = -ENOMEM;
		goto get_mem_obj_from_handle_failed;
	}

	for (i = args->n_success; i < args->n_devices; i++) {
		peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
		if (!peer_pdd) {
			err = -EINVAL;
			goto get_mem_obj_from_handle_failed;
		}
		err = amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
			peer_pdd->dev->adev, (struct kgd_mem *)mem, peer_pdd->drm_priv);
		if (err) {
			pr_err("Failed to unmap from gpu %d/%d\n",
			       i, args->n_devices);
			goto unmap_memory_from_gpu_failed;
		}
		args->n_success = i+1;
	}
	mutex_unlock(&p->mutex);

	if (kfd_flush_tlb_after_unmap(pdd->dev)) {
		err = amdgpu_amdkfd_gpuvm_sync_memory(pdd->dev->adev,
				(struct kgd_mem *) mem, true);
		if (err) {
			pr_debug("Sync memory failed, wait interrupted by user signal\n");
			goto sync_memory_failed;
		}

		/* Flush TLBs after waiting for the page table updates to complete */
		for (i = 0; i < args->n_devices; i++) {
			peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
			if (WARN_ON_ONCE(!peer_pdd))
				continue;
			kfd_flush_tlb(peer_pdd, TLB_FLUSH_HEAVYWEIGHT);
		}
	}
	kfree(devices_arr);

	return 0;

bind_process_to_device_failed:
get_mem_obj_from_handle_failed:
unmap_memory_from_gpu_failed:
	mutex_unlock(&p->mutex);
copy_from_user_failed:
sync_memory_failed:
	kfree(devices_arr);
	return err;
}

static int kfd_ioctl_alloc_queue_gws(struct file *filep,
		struct kfd_process *p, void *data)
{
	int retval;
	struct kfd_ioctl_alloc_queue_gws_args *args = data;
	struct queue *q;
	struct kfd_dev *dev;

	mutex_lock(&p->mutex);
	q = pqm_get_user_queue(&p->pqm, args->queue_id);

	if (q) {
		dev = q->device;
	} else {
		retval = -EINVAL;
		goto out_unlock;
	}

	if (!dev->gws) {
		retval = -ENODEV;
		goto out_unlock;
	}

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		retval = -ENODEV;
		goto out_unlock;
	}

	retval = pqm_set_gws(&p->pqm, args->queue_id, args->num_gws ? dev->gws : NULL);
	mutex_unlock(&p->mutex);

	args->first_gws = 0;
	return retval;

out_unlock:
	mutex_unlock(&p->mutex);
	return retval;
}

static int kfd_ioctl_get_dmabuf_info(struct file *filep,
		struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_dmabuf_info_args *args = data;
	struct kfd_dev *dev = NULL;
	struct amdgpu_device *dmabuf_adev;
	void *metadata_buffer = NULL;
	uint32_t flags;
	unsigned int i;
	int r;

	/* Find a KFD GPU device that supports the get_dmabuf_info query */
	for (i = 0; kfd_topology_enum_kfd_devices(i, &dev) == 0; i++)
		if (dev)
			break;
	if (!dev)
		return -EINVAL;

	if (args->metadata_ptr) {
		metadata_buffer = kzalloc(args->metadata_size, GFP_KERNEL);
		if (!metadata_buffer)
			return -ENOMEM;
	}

	/* Get dmabuf info from KGD */
	r = amdgpu_amdkfd_get_dmabuf_info(dev->adev, args->dmabuf_fd,
					  &dmabuf_adev, &args->size,
					  metadata_buffer, args->metadata_size,
					  &args->metadata_size, &flags);
	if (r)
		goto exit;

	/* Reverse-lookup gpu_id from kgd pointer */
	dev = kfd_device_by_adev(dmabuf_adev);
	if (!dev) {
		r = -EINVAL;
		goto exit;
	}
	args->gpu_id = dev->id;
	args->flags = flags;

	/* Copy metadata buffer to user mode */
	if (metadata_buffer) {
		r = copy_to_user((void __user *)args->metadata_ptr,
				 metadata_buffer, args->metadata_size);
		if (r != 0)
			r = -EFAULT;
	}

exit:
	kfree(metadata_buffer);

	return r;
}

static int kfd_ioctl_import_dmabuf(struct file *filep,
				   struct kfd_process *p, void *data)
{
	struct kfd_ioctl_import_dmabuf_args *args = data;
	struct kfd_process_device *pdd;
	struct dma_buf *dmabuf;
	int idr_handle;
	uint64_t size;
	void *mem;
	int r;

	dmabuf = dma_buf_get(args->dmabuf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		r = -EINVAL;
		goto err_unlock;
	}

	pdd = kfd_bind_process_to_device(pdd->dev, p);
	if (IS_ERR(pdd)) {
		r = PTR_ERR(pdd);
		goto err_unlock;
	}

	r = amdgpu_amdkfd_gpuvm_import_dmabuf(pdd->dev->adev, dmabuf,
					      args->va_addr, pdd->drm_priv,
					      (struct kgd_mem **)&mem, &size,
					      NULL);
	if (r)
		goto err_unlock;

	idr_handle = kfd_process_device_create_obj_handle(pdd, mem);
	if (idr_handle < 0) {
		r = -EFAULT;
		goto err_free;
	}

	mutex_unlock(&p->mutex);
	dma_buf_put(dmabuf);

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);

	return 0;

err_free:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(pdd->dev->adev, (struct kgd_mem *)mem,
					       pdd->drm_priv, NULL);
err_unlock:
	mutex_unlock(&p->mutex);
	dma_buf_put(dmabuf);
	return r;
}

/* Handle requests for watching SMI events */
static int kfd_ioctl_smi_events(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_smi_events_args *args = data;
	struct kfd_process_device *pdd;

	mutex_lock(&p->mutex);

	pdd = kfd_process_device_data_by_id(p, args->gpuid);
	mutex_unlock(&p->mutex);
	if (!pdd)
		return -EINVAL;

	return kfd_smi_event_open(pdd->dev, &args->anon_fd);
}

static int kfd_ioctl_set_xnack_mode(struct file *filep,
				    struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_xnack_mode_args *args = data;
	int r = 0;

	mutex_lock(&p->mutex);
	if (args->xnack_enabled >= 0) {
		if (!list_empty(&p->pqm.queues)) {
			pr_debug("Process has user queues running\n");
			mutex_unlock(&p->mutex);
			return -EBUSY;
		}
		if (args->xnack_enabled && !kfd_process_xnack_mode(p, true))
			r = -EPERM;
		else
			p->xnack_enabled = args->xnack_enabled;
	} else {
		args->xnack_enabled = p->xnack_enabled;
	}
	mutex_unlock(&p->mutex);

	return r;
}

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)
static int kfd_ioctl_svm(struct file *filep, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_svm_args *args = data;
	int r = 0;

	pr_debug("start 0x%llx size 0x%llx op 0x%x nattr 0x%x\n",
		 args->start_addr, args->size, args->op, args->nattr);

	if ((args->start_addr & ~PAGE_MASK) || (args->size & ~PAGE_MASK))
		return -EINVAL;
	if (!args->start_addr || !args->size)
		return -EINVAL;

	r = svm_ioctl(p, args->op, args->start_addr, args->size, args->nattr,
		      args->attrs);

	return r;
}
#else
static int kfd_ioctl_svm(struct file *filep, struct kfd_process *p, void *data)
{
	return -EPERM;
}
#endif

static int criu_checkpoint_process(struct kfd_process *p,
			     uint8_t __user *user_priv_data,
			     uint64_t *priv_offset)
{
	struct kfd_criu_process_priv_data process_priv;
	int ret;

	memset(&process_priv, 0, sizeof(process_priv));

	process_priv.version = KFD_CRIU_PRIV_VERSION;
	/* For CR, we don't consider negative xnack mode which is used for
	 * querying without changing it, here 0 simply means disabled and 1
	 * means enabled so retry for finding a valid PTE.
	 */
	process_priv.xnack_mode = p->xnack_enabled ? 1 : 0;

	ret = copy_to_user(user_priv_data + *priv_offset,
				&process_priv, sizeof(process_priv));

	if (ret) {
		pr_err("Failed to copy process information to user\n");
		ret = -EFAULT;
	}

	*priv_offset += sizeof(process_priv);
	return ret;
}

static int criu_checkpoint_devices(struct kfd_process *p,
			     uint32_t num_devices,
			     uint8_t __user *user_addr,
			     uint8_t __user *user_priv_data,
			     uint64_t *priv_offset)
{
	struct kfd_criu_device_priv_data *device_priv = NULL;
	struct kfd_criu_device_bucket *device_buckets = NULL;
	int ret = 0, i;

	device_buckets = kvzalloc(num_devices * sizeof(*device_buckets), GFP_KERNEL);
	if (!device_buckets) {
		ret = -ENOMEM;
		goto exit;
	}

	device_priv = kvzalloc(num_devices * sizeof(*device_priv), GFP_KERNEL);
	if (!device_priv) {
		ret = -ENOMEM;
		goto exit;
	}

	for (i = 0; i < num_devices; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		device_buckets[i].user_gpu_id = pdd->user_gpu_id;
		device_buckets[i].actual_gpu_id = pdd->dev->id;

		/*
		 * priv_data does not contain useful information for now and is reserved for
		 * future use, so we do not set its contents.
		 */
	}

	ret = copy_to_user(user_addr, device_buckets, num_devices * sizeof(*device_buckets));
	if (ret) {
		pr_err("Failed to copy device information to user\n");
		ret = -EFAULT;
		goto exit;
	}

	ret = copy_to_user(user_priv_data + *priv_offset,
			   device_priv,
			   num_devices * sizeof(*device_priv));
	if (ret) {
		pr_err("Failed to copy device information to user\n");
		ret = -EFAULT;
	}
	*priv_offset += num_devices * sizeof(*device_priv);

exit:
	kvfree(device_buckets);
	kvfree(device_priv);
	return ret;
}

uint32_t get_process_num_bos(struct kfd_process *p)
{
	uint32_t num_of_bos = 0;
	int i;

	/* Run over all PDDs of the process */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		void *mem;
		int id;

		idr_for_each_entry(&pdd->alloc_idr, mem, id) {
			struct kgd_mem *kgd_mem = (struct kgd_mem *)mem;

			if ((uint64_t)kgd_mem->va > pdd->gpuvm_base)
				num_of_bos++;
		}
	}
	return num_of_bos;
}

static int criu_get_prime_handle(struct drm_gem_object *gobj, int flags,
				      u32 *shared_fd)
{
	struct dma_buf *dmabuf;
	int ret;

	dmabuf = amdgpu_gem_prime_export(gobj, flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		pr_err("dmabuf export failed for the BO\n");
		return ret;
	}

	ret = dma_buf_fd(dmabuf, flags);
	if (ret < 0) {
		pr_err("dmabuf create fd failed, ret:%d\n", ret);
		goto out_free_dmabuf;
	}

	*shared_fd = ret;
	return 0;

out_free_dmabuf:
	dma_buf_put(dmabuf);
	return ret;
}

static int criu_checkpoint_bos(struct kfd_process *p,
			       uint32_t num_bos,
			       uint8_t __user *user_bos,
			       uint8_t __user *user_priv_data,
			       uint64_t *priv_offset)
{
	struct kfd_criu_bo_bucket *bo_buckets;
	struct kfd_criu_bo_priv_data *bo_privs;
	int ret = 0, pdd_index, bo_index = 0, id;
	void *mem;

	bo_buckets = kvzalloc(num_bos * sizeof(*bo_buckets), GFP_KERNEL);
	if (!bo_buckets) {
		ret = -ENOMEM;
		goto exit;
	}

	bo_privs = kvzalloc(num_bos * sizeof(*bo_privs), GFP_KERNEL);
	if (!bo_privs) {
		ret = -ENOMEM;
		goto exit;
	}

	for (pdd_index = 0; pdd_index < p->n_pdds; pdd_index++) {
		struct kfd_process_device *pdd = p->pdds[pdd_index];
		struct amdgpu_bo *dumper_bo;
		struct kgd_mem *kgd_mem;

		idr_for_each_entry(&pdd->alloc_idr, mem, id) {
			struct kfd_criu_bo_bucket *bo_bucket;
			struct kfd_criu_bo_priv_data *bo_priv;
			int i, dev_idx = 0;

			if (!mem) {
				ret = -ENOMEM;
				goto exit;
			}

			kgd_mem = (struct kgd_mem *)mem;
			dumper_bo = kgd_mem->bo;

			if ((uint64_t)kgd_mem->va <= pdd->gpuvm_base)
				continue;

			bo_bucket = &bo_buckets[bo_index];
			bo_priv = &bo_privs[bo_index];

			bo_bucket->gpu_id = pdd->user_gpu_id;
			bo_bucket->addr = (uint64_t)kgd_mem->va;
			bo_bucket->size = amdgpu_bo_size(dumper_bo);
			bo_bucket->alloc_flags = (uint32_t)kgd_mem->alloc_flags;
			bo_priv->idr_handle = id;

			if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
				ret = amdgpu_ttm_tt_get_userptr(&dumper_bo->tbo,
								&bo_priv->user_addr);
				if (ret) {
					pr_err("Failed to obtain user address for user-pointer bo\n");
					goto exit;
				}
			}
			if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
				ret = criu_get_prime_handle(&dumper_bo->tbo.base,
						bo_bucket->alloc_flags &
						KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ? DRM_RDWR : 0,
						&bo_bucket->dmabuf_fd);
				if (ret)
					goto exit;
			}
			if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL)
				bo_bucket->offset = KFD_MMAP_TYPE_DOORBELL |
					KFD_MMAP_GPU_ID(pdd->dev->id);
			else if (bo_bucket->alloc_flags &
				KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)
				bo_bucket->offset = KFD_MMAP_TYPE_MMIO |
					KFD_MMAP_GPU_ID(pdd->dev->id);
			else
				bo_bucket->offset = amdgpu_bo_mmap_offset(dumper_bo);

			for (i = 0; i < p->n_pdds; i++) {
				if (amdgpu_amdkfd_bo_mapped_to_dev(p->pdds[i]->dev->adev, kgd_mem))
					bo_priv->mapped_gpuids[dev_idx++] = p->pdds[i]->user_gpu_id;
			}

			pr_debug("bo_size = 0x%llx, bo_addr = 0x%llx bo_offset = 0x%llx\n"
					"gpu_id = 0x%x alloc_flags = 0x%x idr_handle = 0x%x",
					bo_bucket->size,
					bo_bucket->addr,
					bo_bucket->offset,
					bo_bucket->gpu_id,
					bo_bucket->alloc_flags,
					bo_priv->idr_handle);
			bo_index++;
		}
	}

	ret = copy_to_user(user_bos, bo_buckets, num_bos * sizeof(*bo_buckets));
	if (ret) {
		pr_err("Failed to copy BO information to user\n");
		ret = -EFAULT;
		goto exit;
	}

	ret = copy_to_user(user_priv_data + *priv_offset, bo_privs, num_bos * sizeof(*bo_privs));
	if (ret) {
		pr_err("Failed to copy BO priv information to user\n");
		ret = -EFAULT;
		goto exit;
	}

	*priv_offset += num_bos * sizeof(*bo_privs);

exit:
	while (ret && bo_index--) {
		if (bo_buckets[bo_index].alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
			close_fd(bo_buckets[bo_index].dmabuf_fd);
	}

	kvfree(bo_buckets);
	kvfree(bo_privs);
	return ret;
}

static int criu_get_process_object_info(struct kfd_process *p,
					uint32_t *num_devices,
					uint32_t *num_bos,
					uint32_t *num_objects,
					uint64_t *objs_priv_size)
{
	uint64_t queues_priv_data_size, svm_priv_data_size, priv_size;
	uint32_t num_queues, num_events, num_svm_ranges;
	int ret;

	*num_devices = p->n_pdds;
	*num_bos = get_process_num_bos(p);

	ret = kfd_process_get_queue_info(p, &num_queues, &queues_priv_data_size);
	if (ret)
		return ret;

	num_events = kfd_get_num_events(p);

	ret = svm_range_get_info(p, &num_svm_ranges, &svm_priv_data_size);
	if (ret)
		return ret;

	*num_objects = num_queues + num_events + num_svm_ranges;

	if (objs_priv_size) {
		priv_size = sizeof(struct kfd_criu_process_priv_data);
		priv_size += *num_devices * sizeof(struct kfd_criu_device_priv_data);
		priv_size += *num_bos * sizeof(struct kfd_criu_bo_priv_data);
		priv_size += queues_priv_data_size;
		priv_size += num_events * sizeof(struct kfd_criu_event_priv_data);
		priv_size += svm_priv_data_size;
		*objs_priv_size = priv_size;
	}
	return 0;
}

static int criu_checkpoint(struct file *filep,
			   struct kfd_process *p,
			   struct kfd_ioctl_criu_args *args)
{
	int ret;
	uint32_t num_devices, num_bos, num_objects;
	uint64_t priv_size, priv_offset = 0;

	if (!args->devices || !args->bos || !args->priv_data)
		return -EINVAL;

	mutex_lock(&p->mutex);

	if (!p->n_pdds) {
		pr_err("No pdd for given process\n");
		ret = -ENODEV;
		goto exit_unlock;
	}

	/* Confirm all process queues are evicted */
	if (!p->queues_paused) {
		pr_err("Cannot dump process when queues are not in evicted state\n");
		/* CRIU plugin did not call op PROCESS_INFO before checkpointing */
		ret = -EINVAL;
		goto exit_unlock;
	}

	ret = criu_get_process_object_info(p, &num_devices, &num_bos, &num_objects, &priv_size);
	if (ret)
		goto exit_unlock;

	if (num_devices != args->num_devices ||
	    num_bos != args->num_bos ||
	    num_objects != args->num_objects ||
	    priv_size != args->priv_data_size) {

		ret = -EINVAL;
		goto exit_unlock;
	}

	/* each function will store private data inside priv_data and adjust priv_offset */
	ret = criu_checkpoint_process(p, (uint8_t __user *)args->priv_data, &priv_offset);
	if (ret)
		goto exit_unlock;

	ret = criu_checkpoint_devices(p, num_devices, (uint8_t __user *)args->devices,
				(uint8_t __user *)args->priv_data, &priv_offset);
	if (ret)
		goto exit_unlock;

	ret = criu_checkpoint_bos(p, num_bos, (uint8_t __user *)args->bos,
			    (uint8_t __user *)args->priv_data, &priv_offset);
	if (ret)
		goto exit_unlock;

	if (num_objects) {
		ret = kfd_criu_checkpoint_queues(p, (uint8_t __user *)args->priv_data,
						 &priv_offset);
		if (ret)
			goto close_bo_fds;

		ret = kfd_criu_checkpoint_events(p, (uint8_t __user *)args->priv_data,
						 &priv_offset);
		if (ret)
			goto close_bo_fds;

		ret = kfd_criu_checkpoint_svm(p, (uint8_t __user *)args->priv_data, &priv_offset);
		if (ret)
			goto close_bo_fds;
	}

close_bo_fds:
	if (ret) {
		/* If IOCTL returns err, user assumes all FDs opened in criu_dump_bos are closed */
		uint32_t i;
		struct kfd_criu_bo_bucket *bo_buckets = (struct kfd_criu_bo_bucket *) args->bos;

		for (i = 0; i < num_bos; i++) {
			if (bo_buckets[i].alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
				close_fd(bo_buckets[i].dmabuf_fd);
		}
	}

exit_unlock:
	mutex_unlock(&p->mutex);
	if (ret)
		pr_err("Failed to dump CRIU ret:%d\n", ret);
	else
		pr_debug("CRIU dump ret:%d\n", ret);

	return ret;
}

static int criu_restore_process(struct kfd_process *p,
				struct kfd_ioctl_criu_args *args,
				uint64_t *priv_offset,
				uint64_t max_priv_data_size)
{
	int ret = 0;
	struct kfd_criu_process_priv_data process_priv;

	if (*priv_offset + sizeof(process_priv) > max_priv_data_size)
		return -EINVAL;

	ret = copy_from_user(&process_priv,
				(void __user *)(args->priv_data + *priv_offset),
				sizeof(process_priv));
	if (ret) {
		pr_err("Failed to copy process private information from user\n");
		ret = -EFAULT;
		goto exit;
	}
	*priv_offset += sizeof(process_priv);

	if (process_priv.version != KFD_CRIU_PRIV_VERSION) {
		pr_err("Invalid CRIU API version (checkpointed:%d current:%d)\n",
			process_priv.version, KFD_CRIU_PRIV_VERSION);
		return -EINVAL;
	}

	pr_debug("Setting XNACK mode\n");
	if (process_priv.xnack_mode && !kfd_process_xnack_mode(p, true)) {
		pr_err("xnack mode cannot be set\n");
		ret = -EPERM;
		goto exit;
	} else {
		pr_debug("set xnack mode: %d\n", process_priv.xnack_mode);
		p->xnack_enabled = process_priv.xnack_mode;
	}

exit:
	return ret;
}

static int criu_restore_devices(struct kfd_process *p,
				struct kfd_ioctl_criu_args *args,
				uint64_t *priv_offset,
				uint64_t max_priv_data_size)
{
	struct kfd_criu_device_bucket *device_buckets;
	struct kfd_criu_device_priv_data *device_privs;
	int ret = 0;
	uint32_t i;

	if (args->num_devices != p->n_pdds)
		return -EINVAL;

	if (*priv_offset + (args->num_devices * sizeof(*device_privs)) > max_priv_data_size)
		return -EINVAL;

	device_buckets = kmalloc_array(args->num_devices, sizeof(*device_buckets), GFP_KERNEL);
	if (!device_buckets)
		return -ENOMEM;

	ret = copy_from_user(device_buckets, (void __user *)args->devices,
				args->num_devices * sizeof(*device_buckets));
	if (ret) {
		pr_err("Failed to copy devices buckets from user\n");
		ret = -EFAULT;
		goto exit;
	}

	for (i = 0; i < args->num_devices; i++) {
		struct kfd_dev *dev;
		struct kfd_process_device *pdd;
		struct file *drm_file;

		/* device private data is not currently used */

		if (!device_buckets[i].user_gpu_id) {
			pr_err("Invalid user gpu_id\n");
			ret = -EINVAL;
			goto exit;
		}

		dev = kfd_device_by_id(device_buckets[i].actual_gpu_id);
		if (!dev) {
			pr_err("Failed to find device with gpu_id = %x\n",
				device_buckets[i].actual_gpu_id);
			ret = -EINVAL;
			goto exit;
		}

		pdd = kfd_get_process_device_data(dev, p);
		if (!pdd) {
			pr_err("Failed to get pdd for gpu_id = %x\n",
					device_buckets[i].actual_gpu_id);
			ret = -EINVAL;
			goto exit;
		}
		pdd->user_gpu_id = device_buckets[i].user_gpu_id;

		drm_file = fget(device_buckets[i].drm_fd);
		if (!drm_file) {
			pr_err("Invalid render node file descriptor sent from plugin (%d)\n",
				device_buckets[i].drm_fd);
			ret = -EINVAL;
			goto exit;
		}

		if (pdd->drm_file) {
			ret = -EINVAL;
			goto exit;
		}

		/* create the vm using render nodes for kfd pdd */
		if (kfd_process_device_init_vm(pdd, drm_file)) {
			pr_err("could not init vm for given pdd\n");
			/* On success, the PDD keeps the drm_file reference */
			fput(drm_file);
			ret = -EINVAL;
			goto exit;
		}
		/*
		 * pdd now already has the vm bound to render node so below api won't create a new
		 * exclusive kfd mapping but use existing one with renderDXXX but is still needed
		 * for iommu v2 binding  and runtime pm.
		 */
		pdd = kfd_bind_process_to_device(dev, p);
		if (IS_ERR(pdd)) {
			ret = PTR_ERR(pdd);
			goto exit;
		}
	}

	/*
	 * We are not copying device private data from user as we are not using the data for now,
	 * but we still adjust for its private data.
	 */
	*priv_offset += args->num_devices * sizeof(*device_privs);

exit:
	kfree(device_buckets);
	return ret;
}

static int criu_restore_bos(struct kfd_process *p,
			    struct kfd_ioctl_criu_args *args,
			    uint64_t *priv_offset,
			    uint64_t max_priv_data_size)
{
	struct kfd_criu_bo_bucket *bo_buckets;
	struct kfd_criu_bo_priv_data *bo_privs;
	const bool criu_resume = true;
	bool flush_tlbs = false;
	int ret = 0, j = 0;
	uint32_t i;

	if (*priv_offset + (args->num_bos * sizeof(*bo_privs)) > max_priv_data_size)
		return -EINVAL;

	/* Prevent MMU notifications until stage-4 IOCTL (CRIU_RESUME) is received */
	amdgpu_amdkfd_block_mmu_notifications(p->kgd_process_info);

	bo_buckets = kvmalloc_array(args->num_bos, sizeof(*bo_buckets), GFP_KERNEL);
	if (!bo_buckets)
		return -ENOMEM;

	ret = copy_from_user(bo_buckets, (void __user *)args->bos,
			     args->num_bos * sizeof(*bo_buckets));
	if (ret) {
		pr_err("Failed to copy BOs information from user\n");
		ret = -EFAULT;
		goto exit;
	}

	bo_privs = kvmalloc_array(args->num_bos, sizeof(*bo_privs), GFP_KERNEL);
	if (!bo_privs) {
		ret = -ENOMEM;
		goto exit;
	}

	ret = copy_from_user(bo_privs, (void __user *)args->priv_data + *priv_offset,
			     args->num_bos * sizeof(*bo_privs));
	if (ret) {
		pr_err("Failed to copy BOs information from user\n");
		ret = -EFAULT;
		goto exit;
	}
	*priv_offset += args->num_bos * sizeof(*bo_privs);

	/* Create and map new BOs */
	for (i = 0; i < args->num_bos; i++) {
		struct kfd_criu_bo_bucket *bo_bucket;
		struct kfd_criu_bo_priv_data *bo_priv;
		struct kfd_dev *dev;
		struct kfd_process_device *pdd;
		struct kgd_mem *kgd_mem;
		void *mem;
		u64 offset;
		int idr_handle;

		bo_bucket = &bo_buckets[i];
		bo_priv = &bo_privs[i];

		pr_debug("kfd restore ioctl - bo_bucket[%d]:\n", i);
		pr_debug("size = 0x%llx, bo_addr = 0x%llx bo_offset = 0x%llx\n"
			"gpu_id = 0x%x alloc_flags = 0x%x\n"
			"idr_handle = 0x%x\n",
			bo_bucket->size,
			bo_bucket->addr,
			bo_bucket->offset,
			bo_bucket->gpu_id,
			bo_bucket->alloc_flags,
			bo_priv->idr_handle);

		pdd = kfd_process_device_data_by_id(p, bo_bucket->gpu_id);
		if (!pdd) {
			pr_err("Failed to get pdd\n");
			ret = -ENODEV;
			goto exit;
		}
		dev = pdd->dev;

		if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL) {
			pr_debug("restore ioctl: KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL\n");
			if (bo_bucket->size != kfd_doorbell_process_slice(dev)) {
				ret = -EINVAL;
				goto exit;
			}
			offset = kfd_get_process_doorbells(pdd);
		} else if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
			/* MMIO BOs need remapped bus address */
			pr_debug("restore ioctl :KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP\n");
			if (bo_bucket->size != PAGE_SIZE) {
				pr_err("Invalid page size\n");
				ret = -EINVAL;
				goto exit;
			}
			offset = dev->adev->rmmio_remap.bus_addr;
			if (!offset) {
				pr_err("amdgpu_amdkfd_get_mmio_remap_phys_addr failed\n");
				ret = -ENOMEM;
				goto exit;
			}
		} else if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
			offset = bo_priv->user_addr;
		}
		/* Create the BO */
		ret = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(dev->adev,
						bo_bucket->addr,
						bo_bucket->size,
						pdd->drm_priv,
						(struct kgd_mem **) &mem,
						&offset,
						bo_bucket->alloc_flags,
						criu_resume);
		if (ret) {
			pr_err("Could not create the BO\n");
			ret = -ENOMEM;
			goto exit;
		}
		pr_debug("New BO created: size = 0x%llx, bo_addr = 0x%llx bo_offset = 0x%llx\n",
			bo_bucket->size, bo_bucket->addr, offset);

		/* Restore previuos IDR handle */
		pr_debug("Restoring old IDR handle for the BO");
		idr_handle = idr_alloc(&pdd->alloc_idr, mem,
				       bo_priv->idr_handle,
				       bo_priv->idr_handle + 1, GFP_KERNEL);

		if (idr_handle < 0) {
			pr_err("Could not allocate idr\n");
			amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->adev,
						(struct kgd_mem *)mem,
						pdd->drm_priv, NULL);
			ret = -ENOMEM;
			goto exit;
		}

		if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL)
			bo_bucket->restored_offset = KFD_MMAP_TYPE_DOORBELL |
				KFD_MMAP_GPU_ID(pdd->dev->id);
		if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
			bo_bucket->restored_offset = KFD_MMAP_TYPE_MMIO |
				KFD_MMAP_GPU_ID(pdd->dev->id);
		} else if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_GTT) {
			bo_bucket->restored_offset = offset;
			pr_debug("updating offset for GTT\n");
		} else if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
			bo_bucket->restored_offset = offset;
			/* Update the VRAM usage count */
			WRITE_ONCE(pdd->vram_usage, pdd->vram_usage + bo_bucket->size);
			pr_debug("updating offset for VRAM\n");
		}

		/* now map these BOs to GPU/s */
		for (j = 0; j < p->n_pdds; j++) {
			struct kfd_dev *peer;
			struct kfd_process_device *peer_pdd;
			bool table_freed = false;

			if (!bo_priv->mapped_gpuids[j])
				break;

			peer_pdd = kfd_process_device_data_by_id(p, bo_priv->mapped_gpuids[j]);
			if (!peer_pdd) {
				ret = -EINVAL;
				goto exit;
			}
			peer = peer_pdd->dev;

			peer_pdd = kfd_bind_process_to_device(peer, p);
			if (IS_ERR(peer_pdd)) {
				ret = PTR_ERR(peer_pdd);
				goto exit;
			}
			pr_debug("map mem in restore ioctl -> 0x%llx\n",
				 ((struct kgd_mem *)mem)->va);
			ret = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(peer->adev,
				(struct kgd_mem *)mem, peer_pdd->drm_priv, &table_freed);
			if (ret) {
				pr_err("Failed to map to gpu %d/%d\n", j, p->n_pdds);
				goto exit;
			}
			if (table_freed)
				flush_tlbs = true;
		}

		ret = amdgpu_amdkfd_gpuvm_sync_memory(dev->adev,
						      (struct kgd_mem *) mem, true);
		if (ret) {
			pr_debug("Sync memory failed, wait interrupted by user signal\n");
			goto exit;
		}

		pr_debug("map memory was successful for the BO\n");
		/* create the dmabuf object and export the bo */
		kgd_mem = (struct kgd_mem *)mem;
		if (bo_bucket->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
			ret = criu_get_prime_handle(&kgd_mem->bo->tbo.base,
						    DRM_RDWR,
						    &bo_bucket->dmabuf_fd);
			if (ret)
				goto exit;
		}
	} /* done */

	if (flush_tlbs) {
		/* Flush TLBs after waiting for the page table updates to complete */
		for (j = 0; j < p->n_pdds; j++) {
			struct kfd_dev *peer;
			struct kfd_process_device *pdd = p->pdds[j];
			struct kfd_process_device *peer_pdd;

			peer = kfd_device_by_id(pdd->dev->id);
			if (WARN_ON_ONCE(!peer))
				continue;
			peer_pdd = kfd_get_process_device_data(peer, p);
			if (WARN_ON_ONCE(!peer_pdd))
				continue;
			kfd_flush_tlb(peer_pdd, TLB_FLUSH_LEGACY);
		}
	}

	/* Copy only the buckets back so user can read bo_buckets[N].restored_offset */
	ret = copy_to_user((void __user *)args->bos,
				bo_buckets,
				(args->num_bos * sizeof(*bo_buckets)));
	if (ret)
		ret = -EFAULT;

exit:
	while (ret && i--) {
		if (bo_buckets[i].alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
			close_fd(bo_buckets[i].dmabuf_fd);
	}
	kvfree(bo_buckets);
	kvfree(bo_privs);
	return ret;
}

static int criu_restore_objects(struct file *filep,
				struct kfd_process *p,
				struct kfd_ioctl_criu_args *args,
				uint64_t *priv_offset,
				uint64_t max_priv_data_size)
{
	int ret = 0;
	uint32_t i;

	BUILD_BUG_ON(offsetof(struct kfd_criu_queue_priv_data, object_type));
	BUILD_BUG_ON(offsetof(struct kfd_criu_event_priv_data, object_type));
	BUILD_BUG_ON(offsetof(struct kfd_criu_svm_range_priv_data, object_type));

	for (i = 0; i < args->num_objects; i++) {
		uint32_t object_type;

		if (*priv_offset + sizeof(object_type) > max_priv_data_size) {
			pr_err("Invalid private data size\n");
			return -EINVAL;
		}

		ret = get_user(object_type, (uint32_t __user *)(args->priv_data + *priv_offset));
		if (ret) {
			pr_err("Failed to copy private information from user\n");
			goto exit;
		}

		switch (object_type) {
		case KFD_CRIU_OBJECT_TYPE_QUEUE:
			ret = kfd_criu_restore_queue(p, (uint8_t __user *)args->priv_data,
						     priv_offset, max_priv_data_size);
			if (ret)
				goto exit;
			break;
		case KFD_CRIU_OBJECT_TYPE_EVENT:
			ret = kfd_criu_restore_event(filep, p, (uint8_t __user *)args->priv_data,
						     priv_offset, max_priv_data_size);
			if (ret)
				goto exit;
			break;
		case KFD_CRIU_OBJECT_TYPE_SVM_RANGE:
			ret = kfd_criu_restore_svm(p, (uint8_t __user *)args->priv_data,
						     priv_offset, max_priv_data_size);
			if (ret)
				goto exit;
			break;
		default:
			pr_err("Invalid object type:%u at index:%d\n", object_type, i);
			ret = -EINVAL;
			goto exit;
		}
	}
exit:
	return ret;
}

static int criu_restore(struct file *filep,
			struct kfd_process *p,
			struct kfd_ioctl_criu_args *args)
{
	uint64_t priv_offset = 0;
	int ret = 0;

	pr_debug("CRIU restore (num_devices:%u num_bos:%u num_objects:%u priv_data_size:%llu)\n",
		 args->num_devices, args->num_bos, args->num_objects, args->priv_data_size);

	if (!args->bos || !args->devices || !args->priv_data || !args->priv_data_size ||
	    !args->num_devices || !args->num_bos)
		return -EINVAL;

	mutex_lock(&p->mutex);

	/*
	 * Set the process to evicted state to avoid running any new queues before all the memory
	 * mappings are ready.
	 */
	ret = kfd_process_evict_queues(p);
	if (ret)
		goto exit_unlock;

	/* Each function will adjust priv_offset based on how many bytes they consumed */
	ret = criu_restore_process(p, args, &priv_offset, args->priv_data_size);
	if (ret)
		goto exit_unlock;

	ret = criu_restore_devices(p, args, &priv_offset, args->priv_data_size);
	if (ret)
		goto exit_unlock;

	ret = criu_restore_bos(p, args, &priv_offset, args->priv_data_size);
	if (ret)
		goto exit_unlock;

	ret = criu_restore_objects(filep, p, args, &priv_offset, args->priv_data_size);
	if (ret)
		goto exit_unlock;

	if (priv_offset != args->priv_data_size) {
		pr_err("Invalid private data size\n");
		ret = -EINVAL;
	}

exit_unlock:
	mutex_unlock(&p->mutex);
	if (ret)
		pr_err("Failed to restore CRIU ret:%d\n", ret);
	else
		pr_debug("CRIU restore successful\n");

	return ret;
}

static int criu_unpause(struct file *filep,
			struct kfd_process *p,
			struct kfd_ioctl_criu_args *args)
{
	int ret;

	mutex_lock(&p->mutex);

	if (!p->queues_paused) {
		mutex_unlock(&p->mutex);
		return -EINVAL;
	}

	ret = kfd_process_restore_queues(p);
	if (ret)
		pr_err("Failed to unpause queues ret:%d\n", ret);
	else
		p->queues_paused = false;

	mutex_unlock(&p->mutex);

	return ret;
}

static int criu_resume(struct file *filep,
			struct kfd_process *p,
			struct kfd_ioctl_criu_args *args)
{
	struct kfd_process *target = NULL;
	struct pid *pid = NULL;
	int ret = 0;

	pr_debug("Inside %s, target pid for criu restore: %d\n", __func__,
		 args->pid);

	pid = find_get_pid(args->pid);
	if (!pid) {
		pr_err("Cannot find pid info for %i\n", args->pid);
		return -ESRCH;
	}

	pr_debug("calling kfd_lookup_process_by_pid\n");
	target = kfd_lookup_process_by_pid(pid);

	put_pid(pid);

	if (!target) {
		pr_debug("Cannot find process info for %i\n", args->pid);
		return -ESRCH;
	}

	mutex_lock(&target->mutex);
	ret = kfd_criu_resume_svm(target);
	if (ret) {
		pr_err("kfd_criu_resume_svm failed for %i\n", args->pid);
		goto exit;
	}

	ret =  amdgpu_amdkfd_criu_resume(target->kgd_process_info);
	if (ret)
		pr_err("amdgpu_amdkfd_criu_resume failed for %i\n", args->pid);

exit:
	mutex_unlock(&target->mutex);

	kfd_unref_process(target);
	return ret;
}

static int criu_process_info(struct file *filep,
				struct kfd_process *p,
				struct kfd_ioctl_criu_args *args)
{
	int ret = 0;

	mutex_lock(&p->mutex);

	if (!p->n_pdds) {
		pr_err("No pdd for given process\n");
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = kfd_process_evict_queues(p);
	if (ret)
		goto err_unlock;

	p->queues_paused = true;

	args->pid = task_pid_nr_ns(p->lead_thread,
					task_active_pid_ns(p->lead_thread));

	ret = criu_get_process_object_info(p, &args->num_devices, &args->num_bos,
					   &args->num_objects, &args->priv_data_size);
	if (ret)
		goto err_unlock;

	dev_dbg(kfd_device, "Num of devices:%u bos:%u objects:%u priv_data_size:%lld\n",
				args->num_devices, args->num_bos, args->num_objects,
				args->priv_data_size);

err_unlock:
	if (ret) {
		kfd_process_restore_queues(p);
		p->queues_paused = false;
	}
	mutex_unlock(&p->mutex);
	return ret;
}

static int kfd_ioctl_criu(struct file *filep, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_criu_args *args = data;
	int ret;

	dev_dbg(kfd_device, "CRIU operation: %d\n", args->op);
	switch (args->op) {
	case KFD_CRIU_OP_PROCESS_INFO:
		ret = criu_process_info(filep, p, args);
		break;
	case KFD_CRIU_OP_CHECKPOINT:
		ret = criu_checkpoint(filep, p, args);
		break;
	case KFD_CRIU_OP_UNPAUSE:
		ret = criu_unpause(filep, p, args);
		break;
	case KFD_CRIU_OP_RESTORE:
		ret = criu_restore(filep, p, args);
		break;
	case KFD_CRIU_OP_RESUME:
		ret = criu_resume(filep, p, args);
		break;
	default:
		dev_dbg(kfd_device, "Unsupported CRIU operation:%d\n", args->op);
		ret = -EINVAL;
		break;
	}

	if (ret)
		dev_dbg(kfd_device, "CRIU operation:%d err:%d\n", args->op, ret);

	return ret;
}

#define AMDKFD_IOCTL_DEF(ioctl, _func, _flags) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func, .flags = _flags, \
			    .cmd_drv = 0, .name = #ioctl}

/** Ioctl table */
static const struct amdkfd_ioctl_desc amdkfd_ioctls[] = {
	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_VERSION,
			kfd_ioctl_get_version, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CREATE_QUEUE,
			kfd_ioctl_create_queue, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DESTROY_QUEUE,
			kfd_ioctl_destroy_queue, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_MEMORY_POLICY,
			kfd_ioctl_set_memory_policy, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_CLOCK_COUNTERS,
			kfd_ioctl_get_clock_counters, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_PROCESS_APERTURES,
			kfd_ioctl_get_process_apertures, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_UPDATE_QUEUE,
			kfd_ioctl_update_queue, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CREATE_EVENT,
			kfd_ioctl_create_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DESTROY_EVENT,
			kfd_ioctl_destroy_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_EVENT,
			kfd_ioctl_set_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_RESET_EVENT,
			kfd_ioctl_reset_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_WAIT_EVENTS,
			kfd_ioctl_wait_events, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_REGISTER_DEPRECATED,
			kfd_ioctl_dbg_register, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_UNREGISTER_DEPRECATED,
			kfd_ioctl_dbg_unregister, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_ADDRESS_WATCH_DEPRECATED,
			kfd_ioctl_dbg_address_watch, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_WAVE_CONTROL_DEPRECATED,
			kfd_ioctl_dbg_wave_control, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_SCRATCH_BACKING_VA,
			kfd_ioctl_set_scratch_backing_va, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_TILE_CONFIG,
			kfd_ioctl_get_tile_config, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_TRAP_HANDLER,
			kfd_ioctl_set_trap_handler, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_PROCESS_APERTURES_NEW,
			kfd_ioctl_get_process_apertures_new, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ACQUIRE_VM,
			kfd_ioctl_acquire_vm, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ALLOC_MEMORY_OF_GPU,
			kfd_ioctl_alloc_memory_of_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_FREE_MEMORY_OF_GPU,
			kfd_ioctl_free_memory_of_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_MAP_MEMORY_TO_GPU,
			kfd_ioctl_map_memory_to_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_UNMAP_MEMORY_FROM_GPU,
			kfd_ioctl_unmap_memory_from_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_CU_MASK,
			kfd_ioctl_set_cu_mask, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_QUEUE_WAVE_STATE,
			kfd_ioctl_get_queue_wave_state, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_DMABUF_INFO,
				kfd_ioctl_get_dmabuf_info, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_IMPORT_DMABUF,
				kfd_ioctl_import_dmabuf, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ALLOC_QUEUE_GWS,
			kfd_ioctl_alloc_queue_gws, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SMI_EVENTS,
			kfd_ioctl_smi_events, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SVM, kfd_ioctl_svm, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_XNACK_MODE,
			kfd_ioctl_set_xnack_mode, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CRIU_OP,
			kfd_ioctl_criu, KFD_IOC_FLAG_CHECKPOINT_RESTORE),

};

#define AMDKFD_CORE_IOCTL_COUNT	ARRAY_SIZE(amdkfd_ioctls)

static long kfd_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct kfd_process *process;
	amdkfd_ioctl_t *func;
	const struct amdkfd_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;
	bool ptrace_attached = false;

	if (nr >= AMDKFD_CORE_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= AMDKFD_COMMAND_START) && (nr < AMDKFD_COMMAND_END)) {
		u32 amdkfd_size;

		ioctl = &amdkfd_ioctls[nr];

		amdkfd_size = _IOC_SIZE(ioctl->cmd);
		usize = asize = _IOC_SIZE(cmd);
		if (amdkfd_size > asize)
			asize = amdkfd_size;

		cmd = ioctl->cmd;
	} else
		goto err_i1;

	dev_dbg(kfd_device, "ioctl cmd 0x%x (#0x%x), arg 0x%lx\n", cmd, nr, arg);

	/* Get the process struct from the filep. Only the process
	 * that opened /dev/kfd can use the file descriptor. Child
	 * processes need to create their own KFD device context.
	 */
	process = filep->private_data;

	rcu_read_lock();
	if ((ioctl->flags & KFD_IOC_FLAG_CHECKPOINT_RESTORE) &&
	    ptrace_parent(process->lead_thread) == current)
		ptrace_attached = true;
	rcu_read_unlock();

	if (process->lead_thread != current->group_leader
	    && !ptrace_attached) {
		dev_dbg(kfd_device, "Using KFD FD in wrong process\n");
		retcode = -EBADF;
		goto err_i1;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(kfd_device, "no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	/*
	 * Versions of docker shipped in Ubuntu 18.xx and 20.xx do not support
	 * CAP_CHECKPOINT_RESTORE, so we also allow access if CAP_SYS_ADMIN as CAP_SYS_ADMIN is a
	 * more priviledged access.
	 */
	if (unlikely(ioctl->flags & KFD_IOC_FLAG_CHECKPOINT_RESTORE)) {
		if (!capable(CAP_CHECKPOINT_RESTORE) &&
						!capable(CAP_SYS_ADMIN)) {
			retcode = -EACCES;
			goto err_i1;
		}
	}

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kmalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto err_i1;
			}
		}
		if (asize > usize)
			memset(kdata + usize, 0, asize - usize);
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize) != 0) {
			retcode = -EFAULT;
			goto err_i1;
		}
	} else if (cmd & IOC_OUT) {
		memset(kdata, 0, usize);
	}

	retcode = func(filep, process, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize) != 0)
			retcode = -EFAULT;

err_i1:
	if (!ioctl)
		dev_dbg(kfd_device, "invalid ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			  task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	if (retcode)
		dev_dbg(kfd_device, "ioctl cmd (#0x%x), arg 0x%lx, ret = %d\n",
				nr, arg, retcode);

	return retcode;
}

static int kfd_mmio_mmap(struct kfd_dev *dev, struct kfd_process *process,
		      struct vm_area_struct *vma)
{
	phys_addr_t address;
	int ret;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	address = dev->adev->rmmio_remap.bus_addr;

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE |
				VM_DONTDUMP | VM_PFNMAP;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	pr_debug("pasid 0x%x mapping mmio page\n"
		 "     target user address == 0x%08llX\n"
		 "     physical address    == 0x%08llX\n"
		 "     vm_flags            == 0x%04lX\n"
		 "     size                == 0x%04lX\n",
		 process->pasid, (unsigned long long) vma->vm_start,
		 address, vma->vm_flags, PAGE_SIZE);

	ret = io_remap_pfn_range(vma,
				vma->vm_start,
				address >> PAGE_SHIFT,
				PAGE_SIZE,
				vma->vm_page_prot);
	return ret;
}


static int kfd_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct kfd_process *process;
	struct kfd_dev *dev = NULL;
	unsigned long mmap_offset;
	unsigned int gpu_id;

	process = kfd_get_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	mmap_offset = vma->vm_pgoff << PAGE_SHIFT;
	gpu_id = KFD_MMAP_GET_GPU_ID(mmap_offset);
	if (gpu_id)
		dev = kfd_device_by_id(gpu_id);

	switch (mmap_offset & KFD_MMAP_TYPE_MASK) {
	case KFD_MMAP_TYPE_DOORBELL:
		if (!dev)
			return -ENODEV;
		return kfd_doorbell_mmap(dev, process, vma);

	case KFD_MMAP_TYPE_EVENTS:
		return kfd_event_mmap(process, vma);

	case KFD_MMAP_TYPE_RESERVED_MEM:
		if (!dev)
			return -ENODEV;
		return kfd_reserved_mem_mmap(dev, process, vma);
	case KFD_MMAP_TYPE_MMIO:
		if (!dev)
			return -ENODEV;
		return kfd_mmio_mmap(dev, process, vma);
	}

	return -EFAULT;
}
