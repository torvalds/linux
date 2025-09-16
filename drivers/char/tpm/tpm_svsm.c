// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 *
 * Driver for the vTPM defined by the AMD SVSM spec [1].
 *
 * The specification defines a protocol that a SEV-SNP guest OS can use to
 * discover and talk to a vTPM emulated by the Secure VM Service Module (SVSM)
 * in the guest context, but at a more privileged level (usually VMPL0).
 *
 * [1] "Secure VM Service Module for SEV-SNP Guests"
 *     Publication # 58019 Revision: 1.00
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/tpm_svsm.h>

#include <asm/sev.h>

#include "tpm.h"

struct tpm_svsm_priv {
	void *buffer;
};

static int tpm_svsm_send(struct tpm_chip *chip, u8 *buf, size_t bufsiz,
			 size_t cmd_len)
{
	struct tpm_svsm_priv *priv = dev_get_drvdata(&chip->dev);
	int ret;

	ret = svsm_vtpm_cmd_request_fill(priv->buffer, 0, buf, cmd_len);
	if (ret)
		return ret;

	/*
	 * The SVSM call uses the same buffer for the command and for the
	 * response, so after this call, the buffer will contain the response.
	 *
	 * Note: we have to use an internal buffer because the device in SVSM
	 * expects the svsm_vtpm header + data to be physically contiguous.
	 */
	ret = snp_svsm_vtpm_send_command(priv->buffer);
	if (ret)
		return ret;

	return svsm_vtpm_cmd_response_parse(priv->buffer, buf, bufsiz);
}

static struct tpm_class_ops tpm_chip_ops = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.send = tpm_svsm_send,
};

static int __init tpm_svsm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tpm_svsm_priv *priv;
	struct tpm_chip *chip;
	int err;

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/*
	 * The maximum buffer supported is one page (see SVSM_VTPM_MAX_BUFFER
	 * in tpm_svsm.h).
	 */
	priv->buffer = (void *)devm_get_free_pages(dev, GFP_KERNEL, 0);
	if (!priv->buffer)
		return -ENOMEM;

	chip = tpmm_chip_alloc(dev, &tpm_chip_ops);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	dev_set_drvdata(&chip->dev, priv);

	chip->flags |= TPM_CHIP_FLAG_SYNC;
	err = tpm2_probe(chip);
	if (err)
		return err;

	err = tpm_chip_register(chip);
	if (err)
		return err;

	dev_info(dev, "SNP SVSM vTPM %s device\n",
		 (chip->flags & TPM_CHIP_FLAG_TPM2) ? "2.0" : "1.2");

	return 0;
}

static void __exit tpm_svsm_remove(struct platform_device *pdev)
{
	struct tpm_chip *chip = platform_get_drvdata(pdev);

	tpm_chip_unregister(chip);
}

/*
 * tpm_svsm_remove() lives in .exit.text. For drivers registered via
 * module_platform_driver_probe() this is ok because they cannot get unbound
 * at runtime. So mark the driver struct with __refdata to prevent modpost
 * triggering a section mismatch warning.
 */
static struct platform_driver tpm_svsm_driver __refdata = {
	.remove = __exit_p(tpm_svsm_remove),
	.driver = {
		.name = "tpm-svsm",
	},
};

module_platform_driver_probe(tpm_svsm_driver, tpm_svsm_probe);

MODULE_DESCRIPTION("SNP SVSM vTPM Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tpm-svsm");
