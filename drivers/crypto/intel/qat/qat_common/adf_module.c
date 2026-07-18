// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */

#include <crypto/algapi.h>
#include <linux/errno.h>
#include <linux/module.h>

#include "adf_common_drv.h"

static int __init adf_register_module(void)
{
	if (adf_init_misc_wq())
		goto err_misc_wq;

	if (adf_init_aer())
		goto err_aer;

	if (adf_init_pf_wq())
		goto err_pf_wq;

	if (adf_init_vf_wq())
		goto err_vf_wq;

	if (qat_crypto_register())
		goto err_crypto_register;

	if (qat_compression_register())
		goto err_compression_register;

	return 0;

err_compression_register:
	qat_crypto_unregister();
err_crypto_register:
	adf_exit_vf_wq();
err_vf_wq:
	adf_exit_pf_wq();
err_pf_wq:
	adf_exit_aer();
err_aer:
	adf_exit_misc_wq();
err_misc_wq:
	return -EFAULT;
}

static void __exit adf_unregister_module(void)
{
	adf_exit_misc_wq();
	adf_exit_aer();
	adf_exit_vf_wq();
	adf_exit_pf_wq();
	qat_crypto_unregister();
	qat_compression_unregister();
	adf_clean_vf_map(false);
}

module_init(adf_register_module);
module_exit(adf_unregister_module);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_ALIAS_CRYPTO("intel_qat");
MODULE_IMPORT_NS("CRYPTO_INTERNAL");
