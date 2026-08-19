// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/firmware/qcom/qcom_pas.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include "qcom_pas.h"

/*
 * Peripheral Authentication Service (PAS) supported.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define TA_QCOM_PAS_IS_SUPPORTED		1

/*
 * PAS capabilities.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [out] params[1].value.a:	PAS capability flags
 */
#define TA_QCOM_PAS_CAPABILITIES		2

/*
 * PAS image initialization.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].memref:	Loadable firmware metadata
 */
#define TA_QCOM_PAS_INIT_IMAGE			3

/*
 * PAS memory setup.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[0].value.b:	Relocatable firmware size
 * [in]  params[1].value.a:	32bit LSB relocatable firmware memory address
 * [in]  params[1].value.b:	32bit MSB relocatable firmware memory address
 */
#define TA_QCOM_PAS_MEM_SETUP			4

/*
 * PAS get resource table.
 *
 * [in]     params[0].value.a:	Unique 32bit remote processor identifier
 * [inout]  params[1].memref:	Resource table config
 */
#define TA_QCOM_PAS_GET_RESOURCE_TABLE		5

/*
 * PAS image authentication and co-processor reset.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[0].value.b:	Firmware size
 * [in]  params[1].value.a:	32bit LSB firmware memory address
 * [in]  params[1].value.b:	32bit MSB firmware memory address
 * [in]  params[2].memref:	Optional fw memory space shared/lent
 */
#define TA_QCOM_PAS_AUTH_AND_RESET		6

/*
 * PAS co-processor set suspend/resume state.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[0].value.b:	Co-processor state identifier
 */
#define TA_QCOM_PAS_SET_REMOTE_STATE		7

/*
 * PAS co-processor shutdown.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define TA_QCOM_PAS_SHUTDOWN			8

#define TEE_NUM_PARAMS				4

/**
 * struct qcom_pas_tee_private - PAS service private data
 * @dev:		PAS service device.
 * @ctx:		TEE context handler.
 * @session_id:		PAS TA session identifier.
 */
struct qcom_pas_tee_private {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
};

static bool qcom_pas_tee_supported(struct device *dev, u32 pas_id)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_IS_SUPPORTED,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = pas_id
		}
	};
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS not supported, pas_id: %d, ret: %d, err: 0x%x\n",
			pas_id, ret, inv_arg.ret);
		return false;
	}

	return true;
}

static int qcom_pas_tee_init_image(struct device *dev, u32 pas_id,
				   const void *metadata, size_t size,
				   struct qcom_pas_context *ctx)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_INIT_IMAGE,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = pas_id
		},
		[1] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT,
		}
	};
	struct tee_shm *mdata_shm;
	u8 *mdata_buf = NULL;
	int ret;

	mdata_shm = tee_shm_alloc_kernel_buf(data->ctx, size);
	if (IS_ERR(mdata_shm)) {
		dev_err(dev, "mdata_shm allocation failed\n");
		return PTR_ERR(mdata_shm);
	}

	mdata_buf = tee_shm_get_va(mdata_shm, 0);
	if (IS_ERR(mdata_buf)) {
		dev_err(dev, "mdata_buf get VA failed\n");
		tee_shm_free(mdata_shm);
		return PTR_ERR(mdata_buf);
	}
	memcpy(mdata_buf, metadata, size);

	param[1].u.memref.shm = mdata_shm;
	param[1].u.memref.size = size;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS init image failed, pas_id: %d, ret: %d, err: 0x%x\n",
			pas_id, ret, inv_arg.ret);
		tee_shm_free(mdata_shm);
		return ret ?: -EINVAL;
	}

	if (ctx)
		ctx->ptr = (void *)mdata_shm;
	else
		tee_shm_free(mdata_shm);

	return ret;
}

static int qcom_pas_tee_mem_setup(struct device *dev, u32 pas_id,
				  phys_addr_t addr, phys_addr_t size)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_MEM_SETUP,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = pas_id,
			.u.value.b = size,
		},
		[1] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = lower_32_bits(addr),
			.u.value.b = upper_32_bits(addr),
		}
	};
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS mem setup failed, pas_id: %d, ret: %d, err: 0x%x\n",
			pas_id, ret, inv_arg.ret);
		return ret ?: -EINVAL;
	}

	return ret;
}

DEFINE_FREE(shm_free, struct tee_shm *, tee_shm_free(_T))

static void *qcom_pas_tee_get_rsc_table(struct device *dev,
					struct qcom_pas_context *ctx,
					void *input_rt, size_t input_rt_size,
					size_t *output_rt_size)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_GET_RESOURCE_TABLE,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = ctx->pas_id,
		},
		[1] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT,
			.u.memref.size = input_rt_size,
		}
	};
	void *rt_buf = NULL;
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS get RT failed, pas_id: %d, ret: %d, err: 0x%x\n",
			ctx->pas_id, ret, inv_arg.ret);
		return ret ? ERR_PTR(ret) : ERR_PTR(-EINVAL);
	}

	if (param[1].u.memref.size >= input_rt_size) {
		struct tee_shm *rt_shm __free(shm_free) =
			tee_shm_alloc_kernel_buf(data->ctx,
						 param[1].u.memref.size);
		void *rt_shm_va;

		if (IS_ERR_OR_NULL(rt_shm)) {
			dev_err(dev, "rt_shm allocation failed\n");
			rt_shm = NULL;
			return ERR_PTR(-ENOMEM);
		}

		rt_shm_va = tee_shm_get_va(rt_shm, 0);
		if (IS_ERR(rt_shm_va)) {
			dev_err(dev, "rt_shm get VA failed\n");
			return ERR_CAST(rt_shm_va);
		}
		memcpy(rt_shm_va, input_rt, input_rt_size);

		param[1].u.memref.shm = rt_shm;
		ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
		if (ret < 0 || inv_arg.ret != 0) {
			dev_err(dev, "PAS get RT failed, pas_id: %d, ret: %d, err: 0x%x\n",
				ctx->pas_id, ret, inv_arg.ret);
			return ret ? ERR_PTR(ret) : ERR_PTR(-EINVAL);
		}

		if (param[1].u.memref.size) {
			*output_rt_size = param[1].u.memref.size;
			rt_buf = kmemdup(rt_shm_va, *output_rt_size, GFP_KERNEL);
			if (!rt_buf)
				return ERR_PTR(-ENOMEM);
		}
	} else {
		*output_rt_size = 0;
	}

	return rt_buf;
}

static int __qcom_pas_tee_auth_and_reset(struct device *dev, u32 pas_id,
					 phys_addr_t mem_phys, size_t mem_size)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_AUTH_AND_RESET,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = pas_id,
			.u.value.b = mem_size,
		},
		[1] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = lower_32_bits(mem_phys),
			.u.value.b = upper_32_bits(mem_phys),
		},
		/* Reserved for fw memory space to be shared or lent */
		[2] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT,
		}
	};
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS auth reset failed, pas_id: %d, ret: %d, err: 0x%x\n",
			pas_id, ret, inv_arg.ret);
		return ret ?: -EINVAL;
	}

	return ret;
}

static int qcom_pas_tee_auth_and_reset(struct device *dev, u32 pas_id)
{
	return __qcom_pas_tee_auth_and_reset(dev, pas_id, 0, 0);
}

static int qcom_pas_tee_prepare_and_auth_reset(struct device *dev,
					       struct qcom_pas_context *ctx)
{
	return __qcom_pas_tee_auth_and_reset(dev, ctx->pas_id, ctx->mem_phys,
					     ctx->mem_size);
}

static int qcom_pas_tee_set_remote_state(struct device *dev, u32 state,
					 u32 pas_id)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_SET_REMOTE_STATE,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = pas_id,
			.u.value.b = state,
		}
	};
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS set remote state failed, pas_id: %d, ret: %d, err: 0x%x\n",
			pas_id, ret, inv_arg.ret);
		return ret ?: -EINVAL;
	}

	return ret;
}

static int qcom_pas_tee_shutdown(struct device *dev, u32 pas_id)
{
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = TA_QCOM_PAS_SHUTDOWN,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS
	};
	struct tee_param param[4] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = pas_id
		}
	};
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev, "PAS shutdown failed, pas_id: %d, ret: %d, err: 0x%x\n",
			pas_id, ret, inv_arg.ret);
		return ret ?: -EINVAL;
	}

	return ret;
}

static void qcom_pas_tee_metadata_release(struct device *dev,
					  struct qcom_pas_context *ctx)
{
	struct tee_shm *mdata_shm = ctx->ptr;

	tee_shm_free(mdata_shm);
	ctx->ptr = NULL;
}

static struct qcom_pas_ops qcom_pas_ops_tee = {
	.drv_name		= "qcom-pas-tee",
	.supported		= qcom_pas_tee_supported,
	.init_image		= qcom_pas_tee_init_image,
	.mem_setup		= qcom_pas_tee_mem_setup,
	.get_rsc_table		= qcom_pas_tee_get_rsc_table,
	.auth_and_reset		= qcom_pas_tee_auth_and_reset,
	.prepare_and_auth_reset	= qcom_pas_tee_prepare_and_auth_reset,
	.set_remote_state	= qcom_pas_tee_set_remote_state,
	.shutdown		= qcom_pas_tee_shutdown,
	.metadata_release	= qcom_pas_tee_metadata_release,
};

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return ver->impl_id == TEE_IMPL_ID_OPTEE;
}

static int qcom_pas_tee_probe(struct tee_client_device *pas_dev)
{
	struct device *dev = &pas_dev->dev;
	struct qcom_pas_tee_private *data;
	struct tee_ioctl_open_session_arg sess_arg = {
		.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL
	};
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(data->ctx))
		return -ENODEV;

	export_uuid(sess_arg.uuid, &pas_dev->id.uuid);
	ret = tee_client_open_session(data->ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		dev_err(dev, "tee_client_open_session failed, ret: %d, err: 0x%x\n",
			ret, sess_arg.ret);
		tee_client_close_context(data->ctx);
		return ret ?: -EINVAL;
	}

	data->session_id = sess_arg.session;
	dev_set_drvdata(dev, data);
	qcom_pas_ops_tee.dev = dev;
	qcom_pas_ops_register(&qcom_pas_ops_tee);

	return ret;
}

static void qcom_pas_tee_remove(struct tee_client_device *pas_dev)
{
	struct device *dev = &pas_dev->dev;
	struct qcom_pas_tee_private *data = dev_get_drvdata(dev);

	qcom_pas_ops_unregister();
	tee_client_close_session(data->ctx, data->session_id);
	tee_client_close_context(data->ctx);
}

static const struct tee_client_device_id qcom_pas_tee_id_table[] = {
	{UUID_INIT(0xcff7d191, 0x7ca0, 0x4784,
		   0xaf, 0x13, 0x48, 0x22, 0x3b, 0x9a, 0x4f, 0xbe)},
	{}
};
MODULE_DEVICE_TABLE(tee, qcom_pas_tee_id_table);

static struct tee_client_driver optee_pas_tee_driver = {
	.probe		= qcom_pas_tee_probe,
	.remove		= qcom_pas_tee_remove,
	.id_table	= qcom_pas_tee_id_table,
	.driver		= {
		.name		= "qcom-pas-tee",
	},
};

module_tee_client_driver(optee_pas_tee_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm PAS TEE driver");
