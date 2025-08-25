// SPDX-License-Identifier: GPL-2.0+
/*
 *  EFI variable service via TEE
 *
 *  Copyright (C) 2022 Linaro
 */

#include <linux/efi.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/tee.h>
#include <linux/tee_drv.h>
#include <linux/ucs2_string.h>
#include "mm_communication.h"

static struct efivars tee_efivars;
static struct efivar_operations tee_efivar_ops;

static size_t max_buffer_size; /* comm + var + func + data */
static size_t max_payload_size; /* func + data */

struct tee_stmm_efi_private {
	struct tee_context *ctx;
	u32 session;
	struct device *dev;
};

static struct tee_stmm_efi_private pvt_data;

/* UUID of the stmm PTA */
static const struct tee_client_device_id tee_stmm_efi_id_table[] = {
	{PTA_STMM_UUID},
	{}
};

static int tee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	/* currently only OP-TEE is supported as a communication path */
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

/**
 * tee_mm_communicate() - Pass a buffer to StandaloneMM running in TEE
 *
 * @comm_buf:		locally allocated communication buffer
 * @dsize:		buffer size
 * Return:		status code
 */
static efi_status_t tee_mm_communicate(void *comm_buf, size_t dsize)
{
	size_t buf_size;
	struct efi_mm_communicate_header *mm_hdr;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param[4];
	struct tee_shm *shm = NULL;
	int rc;

	if (!comm_buf)
		return EFI_INVALID_PARAMETER;

	mm_hdr = (struct efi_mm_communicate_header *)comm_buf;
	buf_size = mm_hdr->message_len + sizeof(efi_guid_t) + sizeof(size_t);

	if (dsize != buf_size)
		return EFI_INVALID_PARAMETER;

	shm = tee_shm_register_kernel_buf(pvt_data.ctx, comm_buf, buf_size);
	if (IS_ERR(shm)) {
		dev_err(pvt_data.dev, "Unable to register shared memory\n");
		return EFI_UNSUPPORTED;
	}

	memset(&arg, 0, sizeof(arg));
	arg.func = PTA_STMM_CMD_COMMUNICATE;
	arg.session = pvt_data.session;
	arg.num_params = 4;

	memset(param, 0, sizeof(param));
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.size = buf_size;
	param[0].u.memref.shm = shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	rc = tee_client_invoke_func(pvt_data.ctx, &arg, param);
	tee_shm_free(shm);

	if (rc < 0 || arg.ret != 0) {
		dev_err(pvt_data.dev,
			"PTA_STMM_CMD_COMMUNICATE invoke error: 0x%x\n", arg.ret);
		return EFI_DEVICE_ERROR;
	}

	switch (param[1].u.value.a) {
	case ARM_SVC_SPM_RET_SUCCESS:
		return EFI_SUCCESS;

	case ARM_SVC_SPM_RET_INVALID_PARAMS:
		return EFI_INVALID_PARAMETER;

	case ARM_SVC_SPM_RET_DENIED:
		return EFI_ACCESS_DENIED;

	case ARM_SVC_SPM_RET_NO_MEMORY:
		return EFI_OUT_OF_RESOURCES;

	default:
		return EFI_ACCESS_DENIED;
	}
}

/**
 * mm_communicate() - Adjust the communication buffer to StandAlonneMM and send
 * it to TEE
 *
 * @comm_buf:		locally allocated communication buffer, buffer should
 *			be enough big to have some headers and payload
 * @payload_size:	payload size
 * Return:		status code
 */
static efi_status_t mm_communicate(u8 *comm_buf, size_t payload_size)
{
	size_t dsize;
	efi_status_t ret;
	struct efi_mm_communicate_header *mm_hdr;
	struct smm_variable_communicate_header *var_hdr;

	dsize = payload_size + MM_COMMUNICATE_HEADER_SIZE +
		MM_VARIABLE_COMMUNICATE_SIZE;
	mm_hdr = (struct efi_mm_communicate_header *)comm_buf;
	var_hdr = (struct smm_variable_communicate_header *)mm_hdr->data;

	ret = tee_mm_communicate(comm_buf, dsize);
	if (ret != EFI_SUCCESS) {
		dev_err(pvt_data.dev, "%s failed!\n", __func__);
		return ret;
	}

	return var_hdr->ret_status;
}

#define COMM_BUF_SIZE(__payload_size)	(MM_COMMUNICATE_HEADER_SIZE + \
					 MM_VARIABLE_COMMUNICATE_SIZE + \
					 (__payload_size))

/**
 * setup_mm_hdr() -	Allocate a buffer for StandAloneMM and initialize the
 *			header data.
 *
 * @dptr:		pointer address to store allocated buffer
 * @payload_size:	payload size
 * @func:		standAloneMM function number
 * @ret:		EFI return code
 * Return:		pointer to corresponding StandAloneMM function buffer or NULL
 */
static void *setup_mm_hdr(u8 **dptr, size_t payload_size, size_t func,
			  efi_status_t *ret)
{
	const efi_guid_t mm_var_guid = EFI_MM_VARIABLE_GUID;
	struct efi_mm_communicate_header *mm_hdr;
	struct smm_variable_communicate_header *var_hdr;
	u8 *comm_buf;

	/* In the init function we initialize max_buffer_size with
	 * get_max_payload(). So skip the test if max_buffer_size is initialized
	 * StandAloneMM will perform similar checks and drop the buffer if it's
	 * too long
	 */
	if (max_buffer_size &&
	    max_buffer_size < (MM_COMMUNICATE_HEADER_SIZE +
			       MM_VARIABLE_COMMUNICATE_SIZE + payload_size)) {
		*ret = EFI_INVALID_PARAMETER;
		return NULL;
	}

	comm_buf = alloc_pages_exact(COMM_BUF_SIZE(payload_size),
				     GFP_KERNEL | __GFP_ZERO);
	if (!comm_buf) {
		*ret = EFI_OUT_OF_RESOURCES;
		return NULL;
	}

	mm_hdr = (struct efi_mm_communicate_header *)comm_buf;
	memcpy(&mm_hdr->header_guid, &mm_var_guid, sizeof(mm_hdr->header_guid));
	mm_hdr->message_len = MM_VARIABLE_COMMUNICATE_SIZE + payload_size;

	var_hdr = (struct smm_variable_communicate_header *)mm_hdr->data;
	var_hdr->function = func;
	if (dptr)
		*dptr = comm_buf;
	*ret = EFI_SUCCESS;

	return var_hdr->data;
}

/**
 * get_max_payload() - Get variable payload size from StandAloneMM.
 *
 * @size:    size of the variable in storage
 * Return:   status code
 */
static efi_status_t get_max_payload(size_t *size)
{
	struct smm_variable_payload_size *var_payload = NULL;
	size_t payload_size;
	u8 *comm_buf = NULL;
	efi_status_t ret;

	if (!size)
		return EFI_INVALID_PARAMETER;

	payload_size = sizeof(*var_payload);
	var_payload = setup_mm_hdr(&comm_buf, payload_size,
				   SMM_VARIABLE_FUNCTION_GET_PAYLOAD_SIZE,
				   &ret);
	if (!var_payload)
		return EFI_OUT_OF_RESOURCES;

	ret = mm_communicate(comm_buf, payload_size);
	if (ret != EFI_SUCCESS)
		goto out;

	/* Make sure the buffer is big enough for storing variables */
	if (var_payload->size < MM_VARIABLE_ACCESS_HEADER_SIZE + 0x20) {
		ret = EFI_DEVICE_ERROR;
		goto out;
	}
	*size = var_payload->size;
	/*
	 * There seems to be a bug in EDK2 miscalculating the boundaries and
	 * size checks, so deduct 2 more bytes to fulfill this requirement. Fix
	 * it up here to ensure backwards compatibility with older versions
	 * (cf. StandaloneMmPkg/Drivers/StandaloneMmCpu/AArch64/EventHandle.c.
	 * sizeof (EFI_MM_COMMUNICATE_HEADER) instead the size minus the
	 * flexible array member).
	 *
	 * size is guaranteed to be > 2 due to checks on the beginning.
	 */
	*size -= 2;
out:
	free_pages_exact(comm_buf, COMM_BUF_SIZE(payload_size));
	return ret;
}

static efi_status_t get_property_int(u16 *name, size_t name_size,
				     const efi_guid_t *vendor,
				     struct var_check_property *var_property)
{
	struct smm_variable_var_check_property *smm_property;
	size_t payload_size;
	u8 *comm_buf = NULL;
	efi_status_t ret;

	memset(var_property, 0, sizeof(*var_property));
	payload_size = sizeof(*smm_property) + name_size;
	if (payload_size > max_payload_size)
		return EFI_INVALID_PARAMETER;

	smm_property = setup_mm_hdr(
		&comm_buf, payload_size,
		SMM_VARIABLE_FUNCTION_VAR_CHECK_VARIABLE_PROPERTY_GET, &ret);
	if (!smm_property)
		return EFI_OUT_OF_RESOURCES;

	memcpy(&smm_property->guid, vendor, sizeof(smm_property->guid));
	smm_property->name_size = name_size;
	memcpy(smm_property->name, name, name_size);

	ret = mm_communicate(comm_buf, payload_size);
	/*
	 * Currently only R/O property is supported in StMM.
	 * Variables that are not set to R/O will not set the property in StMM
	 * and the call will return EFI_NOT_FOUND. We are setting the
	 * properties to 0x0 so checking against that is enough for the
	 * EFI_NOT_FOUND case.
	 */
	if (ret == EFI_NOT_FOUND)
		ret = EFI_SUCCESS;
	if (ret != EFI_SUCCESS)
		goto out;
	memcpy(var_property, &smm_property->property, sizeof(*var_property));

out:
	free_pages_exact(comm_buf, COMM_BUF_SIZE(payload_size));
	return ret;
}

static efi_status_t tee_get_variable(u16 *name, efi_guid_t *vendor,
				     u32 *attributes, unsigned long *data_size,
				     void *data)
{
	struct var_check_property var_property;
	struct smm_variable_access *var_acc;
	size_t payload_size;
	size_t name_size;
	size_t tmp_dsize;
	u8 *comm_buf = NULL;
	efi_status_t ret;

	if (!name || !vendor || !data_size)
		return EFI_INVALID_PARAMETER;

	name_size = (ucs2_strnlen(name, EFI_VAR_NAME_LEN) + 1) * sizeof(u16);
	if (name_size > max_payload_size - MM_VARIABLE_ACCESS_HEADER_SIZE)
		return EFI_INVALID_PARAMETER;

	/* Trim output buffer size */
	tmp_dsize = *data_size;
	if (name_size + tmp_dsize >
	    max_payload_size - MM_VARIABLE_ACCESS_HEADER_SIZE) {
		tmp_dsize = max_payload_size - MM_VARIABLE_ACCESS_HEADER_SIZE -
			    name_size;
	}

	payload_size = MM_VARIABLE_ACCESS_HEADER_SIZE + name_size + tmp_dsize;
	var_acc = setup_mm_hdr(&comm_buf, payload_size,
			       SMM_VARIABLE_FUNCTION_GET_VARIABLE, &ret);
	if (!var_acc)
		return EFI_OUT_OF_RESOURCES;

	/* Fill in contents */
	memcpy(&var_acc->guid, vendor, sizeof(var_acc->guid));
	var_acc->data_size = tmp_dsize;
	var_acc->name_size = name_size;
	var_acc->attr = attributes ? *attributes : 0;
	memcpy(var_acc->name, name, name_size);

	ret = mm_communicate(comm_buf, payload_size);
	if (ret == EFI_SUCCESS || ret == EFI_BUFFER_TOO_SMALL)
		/* Update with reported data size for trimmed case */
		*data_size = var_acc->data_size;
	if (ret != EFI_SUCCESS)
		goto out;

	ret = get_property_int(name, name_size, vendor, &var_property);
	if (ret != EFI_SUCCESS)
		goto out;

	if (attributes)
		*attributes = var_acc->attr;

	if (!data) {
		ret = EFI_INVALID_PARAMETER;
		goto out;
	}
	memcpy(data, (u8 *)var_acc->name + var_acc->name_size,
	       var_acc->data_size);
out:
	free_pages_exact(comm_buf, COMM_BUF_SIZE(payload_size));
	return ret;
}

static efi_status_t tee_get_next_variable(unsigned long *name_size,
					  efi_char16_t *name, efi_guid_t *guid)
{
	struct smm_variable_getnext *var_getnext;
	size_t payload_size;
	size_t out_name_size;
	size_t in_name_size;
	u8 *comm_buf = NULL;
	efi_status_t ret;

	if (!name_size || !name || !guid)
		return EFI_INVALID_PARAMETER;

	out_name_size = *name_size;
	in_name_size = (ucs2_strnlen(name, EFI_VAR_NAME_LEN) + 1) * sizeof(u16);

	if (out_name_size < in_name_size)
		return EFI_INVALID_PARAMETER;

	if (in_name_size > max_payload_size - MM_VARIABLE_GET_NEXT_HEADER_SIZE)
		return EFI_INVALID_PARAMETER;

	/* Trim output buffer size */
	if (out_name_size > max_payload_size - MM_VARIABLE_GET_NEXT_HEADER_SIZE)
		out_name_size =
			max_payload_size - MM_VARIABLE_GET_NEXT_HEADER_SIZE;

	payload_size = MM_VARIABLE_GET_NEXT_HEADER_SIZE + out_name_size;
	var_getnext = setup_mm_hdr(&comm_buf, payload_size,
				   SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME,
				   &ret);
	if (!var_getnext)
		return EFI_OUT_OF_RESOURCES;

	/* Fill in contents */
	memcpy(&var_getnext->guid, guid, sizeof(var_getnext->guid));
	var_getnext->name_size = out_name_size;
	memcpy(var_getnext->name, name, in_name_size);
	memset((u8 *)var_getnext->name + in_name_size, 0x0,
	       out_name_size - in_name_size);

	ret = mm_communicate(comm_buf, payload_size);
	if (ret == EFI_SUCCESS || ret == EFI_BUFFER_TOO_SMALL) {
		/* Update with reported data size for trimmed case */
		*name_size = var_getnext->name_size;
	}
	if (ret != EFI_SUCCESS)
		goto out;

	memcpy(guid, &var_getnext->guid, sizeof(*guid));
	memcpy(name, var_getnext->name, var_getnext->name_size);

out:
	free_pages_exact(comm_buf, COMM_BUF_SIZE(payload_size));
	return ret;
}

static efi_status_t tee_set_variable(efi_char16_t *name, efi_guid_t *vendor,
				     u32 attributes, unsigned long data_size,
				     void *data)
{
	efi_status_t ret;
	struct var_check_property var_property;
	struct smm_variable_access *var_acc;
	size_t payload_size;
	size_t name_size;
	u8 *comm_buf = NULL;

	if (!name || name[0] == 0 || !vendor)
		return EFI_INVALID_PARAMETER;

	if (data_size > 0 && !data)
		return EFI_INVALID_PARAMETER;

	/* Check payload size */
	name_size = (ucs2_strnlen(name, EFI_VAR_NAME_LEN) + 1) * sizeof(u16);
	payload_size = MM_VARIABLE_ACCESS_HEADER_SIZE + name_size + data_size;
	if (payload_size > max_payload_size)
		return EFI_INVALID_PARAMETER;

	/*
	 * Allocate the buffer early, before switching to RW (if needed)
	 * so we won't need to account for any failures in reading/setting
	 * the properties, if the allocation fails
	 */
	var_acc = setup_mm_hdr(&comm_buf, payload_size,
			       SMM_VARIABLE_FUNCTION_SET_VARIABLE, &ret);
	if (!var_acc)
		return EFI_OUT_OF_RESOURCES;

	/*
	 * The API has the ability to override RO flags. If no RO check was
	 * requested switch the variable to RW for the duration of this call
	 */
	ret = get_property_int(name, name_size, vendor, &var_property);
	if (ret != EFI_SUCCESS) {
		dev_err(pvt_data.dev, "Getting variable property failed\n");
		goto out;
	}

	if (var_property.property & VAR_CHECK_VARIABLE_PROPERTY_READ_ONLY) {
		ret = EFI_WRITE_PROTECTED;
		goto out;
	}

	/* Fill in contents */
	memcpy(&var_acc->guid, vendor, sizeof(var_acc->guid));
	var_acc->data_size = data_size;
	var_acc->name_size = name_size;
	var_acc->attr = attributes;
	memcpy(var_acc->name, name, name_size);
	memcpy((u8 *)var_acc->name + name_size, data, data_size);

	ret = mm_communicate(comm_buf, payload_size);
	dev_dbg(pvt_data.dev, "Set Variable %s %d %lx\n", __FILE__, __LINE__, ret);
out:
	free_pages_exact(comm_buf, COMM_BUF_SIZE(payload_size));
	return ret;
}

static efi_status_t tee_set_variable_nonblocking(efi_char16_t *name,
						 efi_guid_t *vendor,
						 u32 attributes,
						 unsigned long data_size,
						 void *data)
{
	return EFI_UNSUPPORTED;
}

static efi_status_t tee_query_variable_info(u32 attributes,
					    u64 *max_variable_storage_size,
					    u64 *remain_variable_storage_size,
					    u64 *max_variable_size)
{
	struct smm_variable_query_info *mm_query_info;
	size_t payload_size;
	efi_status_t ret;
	u8 *comm_buf;

	payload_size = sizeof(*mm_query_info);
	mm_query_info = setup_mm_hdr(&comm_buf, payload_size,
				SMM_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO,
				&ret);
	if (!mm_query_info)
		return EFI_OUT_OF_RESOURCES;

	mm_query_info->attr = attributes;
	ret = mm_communicate(comm_buf, payload_size);
	if (ret != EFI_SUCCESS)
		goto out;
	*max_variable_storage_size = mm_query_info->max_variable_storage;
	*remain_variable_storage_size =
		mm_query_info->remaining_variable_storage;
	*max_variable_size = mm_query_info->max_variable_size;

out:
	free_pages_exact(comm_buf, COMM_BUF_SIZE(payload_size));
	return ret;
}

static void tee_stmm_efi_close_context(void *data)
{
	tee_client_close_context(pvt_data.ctx);
}

static void tee_stmm_efi_close_session(void *data)
{
	tee_client_close_session(pvt_data.ctx, pvt_data.session);
}

static void tee_stmm_restore_efivars_generic_ops(void)
{
	efivars_unregister(&tee_efivars);
	efivars_generic_ops_register();
}

static int tee_stmm_efi_probe(struct device *dev)
{
	struct tee_ioctl_open_session_arg sess_arg;
	efi_status_t ret;
	int rc;

	pvt_data.ctx = tee_client_open_context(NULL, tee_ctx_match, NULL, NULL);
	if (IS_ERR(pvt_data.ctx))
		return -ENODEV;

	rc = devm_add_action_or_reset(dev, tee_stmm_efi_close_context, NULL);
	if (rc)
		return rc;

	/* Open session with StMM PTA */
	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &tee_stmm_efi_id_table[0].uuid);
	rc = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		return -EINVAL;
	}
	pvt_data.session = sess_arg.session;
	pvt_data.dev = dev;
	rc = devm_add_action_or_reset(dev, tee_stmm_efi_close_session, NULL);
	if (rc)
		return rc;

	ret = get_max_payload(&max_payload_size);
	if (ret != EFI_SUCCESS)
		return -EIO;

	max_buffer_size = MM_COMMUNICATE_HEADER_SIZE +
			  MM_VARIABLE_COMMUNICATE_SIZE +
			  max_payload_size;

	tee_efivar_ops.get_variable		= tee_get_variable;
	tee_efivar_ops.get_next_variable	= tee_get_next_variable;
	tee_efivar_ops.set_variable		= tee_set_variable;
	tee_efivar_ops.set_variable_nonblocking	= tee_set_variable_nonblocking;
	tee_efivar_ops.query_variable_store	= efi_query_variable_store;
	tee_efivar_ops.query_variable_info	= tee_query_variable_info;

	efivars_generic_ops_unregister();
	pr_info("Using TEE-based EFI runtime variable services\n");
	efivars_register(&tee_efivars, &tee_efivar_ops);

	return 0;
}

static int tee_stmm_efi_remove(struct device *dev)
{
	tee_stmm_restore_efivars_generic_ops();

	return 0;
}

MODULE_DEVICE_TABLE(tee, tee_stmm_efi_id_table);

static struct tee_client_driver tee_stmm_efi_driver = {
	.id_table	= tee_stmm_efi_id_table,
	.driver		= {
		.name		= "tee-stmm-efi",
		.bus		= &tee_bus_type,
		.probe		= tee_stmm_efi_probe,
		.remove		= tee_stmm_efi_remove,
	},
};

static int __init tee_stmm_efi_mod_init(void)
{
	return driver_register(&tee_stmm_efi_driver.driver);
}

static void __exit tee_stmm_efi_mod_exit(void)
{
	driver_unregister(&tee_stmm_efi_driver.driver);
}

module_init(tee_stmm_efi_mod_init);
module_exit(tee_stmm_efi_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilias Apalodimas <ilias.apalodimas@linaro.org>");
MODULE_AUTHOR("Masahisa Kojima <masahisa.kojima@linaro.org>");
MODULE_DESCRIPTION("TEE based EFI runtime variable service driver");
