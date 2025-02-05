// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004 IBM Corporation
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Copyright (C) 2013 Obsidian Research Corp
 * Jason Gunthorpe <jgunthorpe@obsidianresearch.com>
 *
 * Device file system interface to the TPM
 */
#include <linux/slab.h>
#include "tpm-dev.h"

static int tpm_open(struct inode *inode, struct file *file)
{
	struct tpm_chip *chip;
	struct file_priv *priv;

	chip = container_of(inode->i_cdev, struct tpm_chip, cdev);

	/* It's assured that the chip will be opened just once,
	 * by the check of is_open variable, which is protected
	 * by driver_lock. */
	if (test_and_set_bit(0, &chip->is_open)) {
		dev_dbg(&chip->dev, "Another process owns this TPM\n");
		return -EBUSY;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		goto out;

	tpm_common_open(file, chip, priv, NULL);

	return 0;

 out:
	clear_bit(0, &chip->is_open);
	return -ENOMEM;
}

/*
 * Called on file close
 */
static int tpm_release(struct inode *inode, struct file *file)
{
	struct file_priv *priv = file->private_data;

	tpm_common_release(file, priv);
	clear_bit(0, &priv->chip->is_open);
	kfree(priv);

	return 0;
}

const struct file_operations tpm_fops = {
	.owner = THIS_MODULE,
	.open = tpm_open,
	.read = tpm_common_read,
	.write = tpm_common_write,
	.poll = tpm_common_poll,
	.release = tpm_release,
};
