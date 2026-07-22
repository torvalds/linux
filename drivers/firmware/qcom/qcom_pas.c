// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2010,2015,2019 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device/devres.h>
#include <linux/firmware/qcom/qcom_pas.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "qcom_pas.h"

static struct qcom_pas_ops *ops_ptr;

/**
 * devm_qcom_pas_context_alloc() - Allocate peripheral authentication service
 *				   context for a given peripheral
 *
 * PAS context is device-resource managed, so the caller does not need
 * to worry about freeing the context memory.
 *
 * @dev:	  PAS firmware device
 * @pas_id:	  peripheral authentication service id
 * @mem_phys:	  Subsystem reserve memory start address
 * @mem_size:	  Subsystem reserve memory size
 *
 * Return: The new PAS context, or ERR_PTR() on failure.
 */
struct qcom_pas_context *devm_qcom_pas_context_alloc(struct device *dev,
						     u32 pas_id,
						     phys_addr_t mem_phys,
						     size_t mem_size)
{
	struct qcom_pas_context *ctx;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->dev = dev;
	ctx->pas_id = pas_id;
	ctx->mem_phys = mem_phys;
	ctx->mem_size = mem_size;

	return ctx;
}
EXPORT_SYMBOL_GPL(devm_qcom_pas_context_alloc);

/**
 * qcom_pas_init_image() - Initialize peripheral authentication service state
 *			   machine for a given peripheral, using the metadata
 * @pas_id:	peripheral authentication service id
 * @metadata:	pointer to memory containing ELF header, program header table
 *		and optional blob of data used for authenticating the metadata
 *		and the rest of the firmware
 * @size:	size of the metadata
 * @ctx:	optional pas context
 *
 * Return: 0 on success.
 *
 * Upon successful return, the PAS metadata context (@ctx) will be used to
 * track the metadata allocation, this needs to be released by invoking
 * qcom_pas_metadata_release() by the caller.
 */
int qcom_pas_init_image(u32 pas_id, const void *metadata, size_t size,
			struct qcom_pas_context *ctx)
{
	if (!ops_ptr)
		return -ENODEV;

	return ops_ptr->init_image(ops_ptr->dev, pas_id, metadata, size, ctx);
}
EXPORT_SYMBOL_GPL(qcom_pas_init_image);

/**
 * qcom_pas_metadata_release() - release metadata context
 * @ctx:	pas context
 */
void qcom_pas_metadata_release(struct qcom_pas_context *ctx)
{
	if (!ops_ptr || !ctx || !ctx->ptr)
		return;

	ops_ptr->metadata_release(ops_ptr->dev, ctx);
}
EXPORT_SYMBOL_GPL(qcom_pas_metadata_release);

/**
 * qcom_pas_mem_setup() - Prepare the memory related to a given peripheral
 *			  for firmware loading
 * @pas_id:	peripheral authentication service id
 * @addr:	start address of memory area to prepare
 * @size:	size of the memory area to prepare
 *
 * Return: 0 on success.
 */
int qcom_pas_mem_setup(u32 pas_id, phys_addr_t addr, phys_addr_t size)
{
	if (!ops_ptr)
		return -ENODEV;

	return ops_ptr->mem_setup(ops_ptr->dev, pas_id, addr, size);
}
EXPORT_SYMBOL_GPL(qcom_pas_mem_setup);

/**
 * qcom_pas_get_rsc_table() - Retrieve the resource table in passed output buffer
 *			      for a given peripheral.
 *
 * Qualcomm remote processor may rely on both static and dynamic resources for
 * its functionality. Static resources typically refer to memory-mapped
 * addresses required by the subsystem and are often embedded within the
 * firmware binary and dynamic resources, such as shared memory in DDR etc.,
 * are determined at runtime during the boot process.
 *
 * On Qualcomm Technologies devices, it's possible that static resources are
 * not embedded in the firmware binary and instead are provided by TrustZone.
 * However, dynamic resources are always expected to come from TrustZone. This
 * indicates that for Qualcomm devices, all resources (static and dynamic) will
 * be provided by TrustZone PAS service.
 *
 * If the remote processor firmware binary does contain static resources, they
 * should be passed in input_rt. These will be forwarded to TrustZone for
 * authentication. TrustZone will then append the dynamic resources and return
 * the complete resource table in output_rt_tzm.
 *
 * If the remote processor firmware binary does not include a resource table,
 * the caller of this function should set input_rt as NULL and input_rt_size
 * as zero respectively.
 *
 * More about documentation on resource table data structures can be found in
 * include/linux/remoteproc.h
 *
 * @ctx:	    PAS context
 * @input_rt:       resource table buffer which is present in firmware binary
 * @input_rt_size:  size of the resource table present in firmware binary
 * @output_rt_size: TrustZone expects caller should pass worst case size for
 *		    the output_rt_tzm.
 *
 * Return:
 *  On success, returns a pointer to the allocated buffer containing the final
 *  resource table and output_rt_size will have actual resource table size from
 *  TrustZone. The caller is responsible for freeing the buffer. On failure,
 *  returns ERR_PTR(-errno).
 */
struct resource_table *qcom_pas_get_rsc_table(struct qcom_pas_context *ctx,
					      void *input_rt,
					      size_t input_rt_size,
					      size_t *output_rt_size)
{
	if (!ops_ptr)
		return ERR_PTR(-ENODEV);
	if (!ctx)
		return ERR_PTR(-EINVAL);

	return ops_ptr->get_rsc_table(ops_ptr->dev, ctx, input_rt,
				      input_rt_size, output_rt_size);
}
EXPORT_SYMBOL_GPL(qcom_pas_get_rsc_table);

/**
 * qcom_pas_auth_and_reset() - Authenticate the given peripheral firmware
 *			       and reset the remote processor
 * @pas_id:	peripheral authentication service id
 *
 * Return: 0 on success.
 */
int qcom_pas_auth_and_reset(u32 pas_id)
{
	if (!ops_ptr)
		return -ENODEV;

	return ops_ptr->auth_and_reset(ops_ptr->dev, pas_id);
}
EXPORT_SYMBOL_GPL(qcom_pas_auth_and_reset);

/**
 * qcom_pas_prepare_and_auth_reset() - Prepare, authenticate, and reset the
 *				       remote processor
 *
 * @ctx:	Context saved during call to devm_qcom_pas_context_alloc()
 *
 * This function performs the necessary steps to prepare a PAS subsystem,
 * authenticate it using the provided metadata, and initiate a reset sequence.
 *
 * It should be used when Linux is in control setting up the IOMMU hardware
 * for remote subsystem during secure firmware loading processes. The
 * preparation step sets up a shmbridge over the firmware memory before
 * TrustZone accesses the firmware memory region for authentication. The
 * authentication step verifies the integrity and authenticity of the firmware
 * or configuration using secure metadata. Finally, the reset step ensures the
 * subsystem starts in a clean and sane state.
 *
 * Return: 0 on success, negative errno on failure.
 */
int qcom_pas_prepare_and_auth_reset(struct qcom_pas_context *ctx)
{
	if (!ops_ptr)
		return -ENODEV;
	if (!ctx)
		return -EINVAL;

	return ops_ptr->prepare_and_auth_reset(ops_ptr->dev, ctx);
}
EXPORT_SYMBOL_GPL(qcom_pas_prepare_and_auth_reset);

/**
 * qcom_pas_set_remote_state() - Set the remote processor state
 * @state:	peripheral state
 * @pas_id:	peripheral authentication service id
 *
 * Return: 0 on success.
 */
int qcom_pas_set_remote_state(u32 state, u32 pas_id)
{
	if (!ops_ptr)
		return -ENODEV;

	return ops_ptr->set_remote_state(ops_ptr->dev, state, pas_id);
}
EXPORT_SYMBOL_GPL(qcom_pas_set_remote_state);

/**
 * qcom_pas_shutdown() - Shut down the remote processor
 * @pas_id:	peripheral authentication service id
 *
 * Return: 0 on success.
 */
int qcom_pas_shutdown(u32 pas_id)
{
	if (!ops_ptr)
		return -ENODEV;

	return ops_ptr->shutdown(ops_ptr->dev, pas_id);
}
EXPORT_SYMBOL_GPL(qcom_pas_shutdown);

/**
 * qcom_pas_supported() - Check if the peripheral authentication service is
 *			  supported for the given peripheral
 * @pas_id:	peripheral authentication service id
 *
 * Return: true if PAS is supported for this peripheral, otherwise false.
 */
bool qcom_pas_supported(u32 pas_id)
{
	if (!ops_ptr)
		return false;

	return ops_ptr->supported(ops_ptr->dev, pas_id);
}
EXPORT_SYMBOL_GPL(qcom_pas_supported);

/**
 * qcom_pas_is_available() - Check if the peripheral authentication service is
 *			     available. Note that it is mandatory for any PAS
 *			     client to invoke this API. If it returns true then
 *			     only any other PAS API can be invoked.
 *
 * Return: true if PAS is available, otherwise false.
 */
bool qcom_pas_is_available(void)
{
	/*
	 * The barrier for ops_ptr is intended to synchronize the data stores
	 * for the ops data structure when client drivers are in parallel
	 * checking for PAS service availability.
	 *
	 * Once the PAS backend becomes available, it is allowed for multiple
	 * threads to enter TZ for parallel bringup of co-processors during
	 * boot.
	 */
	return !!smp_load_acquire(&ops_ptr);
}
EXPORT_SYMBOL_GPL(qcom_pas_is_available);

void qcom_pas_ops_register(struct qcom_pas_ops *ops)
{
	if (!qcom_pas_is_available())
		/* Paired with smp_load_acquire() in qcom_pas_is_available() */
		smp_store_release(&ops_ptr, ops);
	else
		pr_err("qcom_pas: ops already registered by %s\n",
		       ops_ptr->drv_name);
}
EXPORT_SYMBOL_GPL(qcom_pas_ops_register);

void qcom_pas_ops_unregister(void)
{
	/* Paired with smp_load_acquire() in qcom_pas_is_available() */
	smp_store_release(&ops_ptr, NULL);
}
EXPORT_SYMBOL_GPL(qcom_pas_ops_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm generic TZ PAS driver");
