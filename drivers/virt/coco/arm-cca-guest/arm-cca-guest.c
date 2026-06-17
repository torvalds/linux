// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Ltd.
 */

#include <linux/arm-smccc.h>
#include <linux/cc_platform.h>
#include <linux/kernel.h>
#include <linux/device-id/platform.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/tsm.h>
#include <linux/types.h>

#include <asm/rsi.h>

/**
 * struct arm_cca_token_info - a descriptor for the token buffer.
 * @granule:		PA of the granule to which the token will be written
 * @offset:		Offset within granule to start of buffer in bytes
 */
struct arm_cca_token_info {
	phys_addr_t     granule;
	unsigned long   offset;
};

/**
 * arm_cca_attestation_continue - Retrieve the attestation token data.
 *
 * @info: pointer to the arm_cca_token_info
 *
 * Attestation token generation is a long running operation and therefore
 * the token data may not be retrieved in a single call. Moreover, the
 * token retrieval operation must be requested on the same CPU on which the
 * attestation token generation was initialised.
 * This helper function must therefore be executed on the same CPU multiple
 * times until the entire token data is retrieved.
 */
static unsigned long
arm_cca_attestation_continue(struct arm_cca_token_info *info)
{
	unsigned long ret;
	unsigned long len;
	unsigned long size;

	size = RSI_GRANULE_SIZE - info->offset;
	ret = rsi_attestation_token_continue(info->granule, info->offset, size,
					     &len);
	info->offset += len;
	return ret;
}

/**
 * arm_cca_report_new - Generate a new attestation token.
 *
 * @report: pointer to the TSM report context information.
 * @data:  pointer to the context specific data for this module.
 *
 * Initialise the attestation token generation using the challenge data
 * passed in the TSM descriptor. Allocate memory for the attestation token
 * and retrieve the attestation token on the same CPU on which the
 * attestation token generation was initialised.
 *
 * The challenge data must be at least 32 bytes and no more than 64 bytes. If
 * less than 64 bytes are provided it will be zero padded to 64 bytes.
 *
 * Return:
 * * %0        - Attestation token generated successfully.
 * * %-EINVAL  - A parameter was not valid.
 * * %-ENOMEM  - Out of memory.
 * * %-EFAULT  - Failed to get IPA for memory page(s).
 */
static int arm_cca_report_new(struct tsm_report *report, void *data)
{
	int ret = 0;
	unsigned long rsi_result;
	long max_size;
	unsigned long token_size = 0;
	struct arm_cca_token_info info;
	void *buf;
	u8 *token __free(kvfree) = NULL;
	struct tsm_report_desc *desc = &report->desc;

	if (desc->inblob_len < 32 || desc->inblob_len > 64)
		return -EINVAL;

	/*
	 * The attestation token 'init' and 'continue' calls must be
	 * performed on the same CPU, so disable CPU migration around
	 * those operations.
	 */
	migrate_disable();

	max_size = rsi_attestation_token_init(desc->inblob, desc->inblob_len);
	if (max_size <= 0) {
		ret = -EINVAL;
		goto exit_migrate_enable;
	}

	/* Allocate outblob */
	token = kvzalloc(max_size, GFP_KERNEL);
	if (!token) {
		ret = -ENOMEM;
		goto exit_migrate_enable;
	}

	/*
	 * Since the outblob may not be physically contiguous, use a page
	 * to bounce the buffer from RMM.
	 */
	buf = alloc_pages_exact(RSI_GRANULE_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto exit_migrate_enable;
	}

	/* Get the PA of the memory page(s) that were allocated */
	info.granule = (unsigned long)virt_to_phys(buf);

	/* Loop until the token is ready or there is an error */
	do {
		/* Retrieve one RSI_GRANULE_SIZE data per loop iteration */
		info.offset = 0;
		do {
			/*
			 * Retrieve a sub-granule chunk of data per loop
			 * iteration.
			 */
			rsi_result = arm_cca_attestation_continue(&info);
		} while (rsi_result == RSI_INCOMPLETE &&
			 info.offset < RSI_GRANULE_SIZE);

		/* Break out in case of failure */
		if (rsi_result != RSI_SUCCESS && rsi_result != RSI_INCOMPLETE) {
			ret = -ENXIO;
			token_size = 0;
			goto exit_free_granule_page;
		}

		/*
		 * Copy the retrieved token data from the granule
		 * to the token buffer, ensuring that the RMM doesn't
		 * overflow the buffer.
		 */
		if (WARN_ON(token_size + info.offset > max_size))
			break;
		memcpy(&token[token_size], buf, info.offset);
		token_size += info.offset;
	} while (rsi_result == RSI_INCOMPLETE);

	report->outblob = no_free_ptr(token);
exit_free_granule_page:
	report->outblob_len = token_size;
	free_pages_exact(buf, RSI_GRANULE_SIZE);
exit_migrate_enable:
	migrate_enable();
	return ret;
}

static const struct tsm_report_ops arm_cca_tsm_ops = {
	.name = KBUILD_MODNAME,
	.report_new = arm_cca_report_new,
};

/**
 * arm_cca_guest_init - Register with the Trusted Security Module (TSM)
 * interface.
 *
 * Return:
 * * %0        - Registered successfully with the TSM interface.
 * * %-ENODEV  - The execution context is not an Arm Realm.
 * * %-EBUSY   - Already registered.
 */
static int __init arm_cca_guest_init(void)
{
	int ret;

	if (!is_realm_world())
		return -ENODEV;

	ret = tsm_report_register(&arm_cca_tsm_ops, NULL);
	if (ret < 0)
		pr_err("Error %d registering with TSM\n", ret);

	return ret;
}
module_init(arm_cca_guest_init);

/**
 * arm_cca_guest_exit - unregister with the Trusted Security Module (TSM)
 * interface.
 */
static void __exit arm_cca_guest_exit(void)
{
	tsm_report_unregister(&arm_cca_tsm_ops);
}
module_exit(arm_cca_guest_exit);

/* modalias, so userspace can autoload this module when RSI is available */
static const struct platform_device_id arm_cca_match[] __maybe_unused = {
	{ .name = RSI_PDEV_NAME },
	{ }
};

MODULE_DEVICE_TABLE(platform, arm_cca_match);
MODULE_AUTHOR("Sami Mujawar <sami.mujawar@arm.com>");
MODULE_DESCRIPTION("Arm CCA Guest TSM Driver");
MODULE_LICENSE("GPL");
