/*
 * Copyright (C) 2017 James.Bottomley@HansenPartnership.com
 *
 * GPLv2
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

	rc = tpm2_init_space(&priv->space);
	if (rc) {
		kfree(priv);
		return -ENOMEM;
	}

	tpm_common_open(file, chip, &priv->priv);

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

ssize_t tpmrm_write(struct file *file, const char __user *buf,
		   size_t size, loff_t *off)
{
	struct file_priv *fpriv = file->private_data;
	struct tpmrm_priv *priv = container_of(fpriv, struct tpmrm_priv, priv);

	return tpm_common_write(file, buf, size, off, &priv->space);
}

const struct file_operations tpmrm_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpmrm_open,
	.read = tpm_common_read,
	.write = tpmrm_write,
	.release = tpmrm_release,
};

