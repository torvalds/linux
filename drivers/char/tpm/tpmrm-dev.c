// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 James.Bottomley@HansenPartnership.com
 */
#include <linux/slab.h>
#include "tpm-dev.h"

struct tpmrm_priv {
	struct file_priv priv;
	struct tpm_space space;
};

static int tpmrm_open(struct inode *inode, struct file *file)
{
	struct tpm_chip *chip;
	struct tpmrm_priv *priv;
	int rc;

	chip = container_of(inode->i_cdev, struct tpm_chip, cdevs);
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	rc = tpm2_init_space(&priv->space, TPM2_SPACE_BUFFER_SIZE);
	if (rc) {
		kfree(priv);
		return -ENOMEM;
	}

	tpm_common_open(file, chip, &priv->priv, &priv->space);

	return 0;
}

static int tpmrm_release(struct inode *inode, struct file *file)
{
	struct file_priv *fpriv = file->private_data;
	struct tpmrm_priv *priv = container_of(fpriv, struct tpmrm_priv, priv);

	tpm_common_release(file, fpriv);
	tpm2_del_space(fpriv->chip, &priv->space);
	kfree(priv);

	return 0;
}

const struct file_operations tpmrm_fops = {
	.owner = THIS_MODULE,
	.open = tpmrm_open,
	.read = tpm_common_read,
	.write = tpm_common_write,
	.poll = tpm_common_poll,
	.release = tpmrm_release,
};
