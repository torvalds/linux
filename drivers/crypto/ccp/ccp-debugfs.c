/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/ccp.h>

#include "ccp-dev.h"

/* DebugFS helpers */
#define	OBUFP		(obuf + oboff)
#define	OBUFLEN		512
#define	OBUFSPC		(OBUFLEN - oboff)
#define	OSCNPRINTF(fmt, ...) \
		scnprintf(OBUFP, OBUFSPC, fmt, ## __VA_ARGS__)

#define BUFLEN	63

#define	RI_VERSION_NUM	0x0000003F
#define	RI_AES_PRESENT	0x00000040
#define	RI_3DES_PRESENT	0x00000080
#define	RI_SHA_PRESENT	0x00000100
#define	RI_RSA_PRESENT	0x00000200
#define	RI_ECC_PRESENT	0x00000400
#define	RI_ZDE_PRESENT	0x00000800
#define	RI_ZCE_PRESENT	0x00001000
#define	RI_TRNG_PRESENT	0x00002000
#define	RI_ELFC_PRESENT	0x00004000
#define	RI_ELFC_SHIFT	14
#define	RI_NUM_VQM	0x00078000
#define	RI_NVQM_SHIFT	15
#define	RI_NVQM(r)	(((r) * RI_NUM_VQM) >> RI_NVQM_SHIFT)
#define	RI_LSB_ENTRIES	0x0FF80000
#define	RI_NLSB_SHIFT	19
#define	RI_NLSB(r)	(((r) * RI_LSB_ENTRIES) >> RI_NLSB_SHIFT)

static ssize_t ccp5_debugfs_info_read(struct file *filp, char __user *ubuf,
				      size_t count, loff_t *offp)
{
	struct ccp_device *ccp = filp->private_data;
	unsigned int oboff = 0;
	unsigned int regval;
	ssize_t ret;
	char *obuf;

	if (!ccp)
		return 0;

	obuf = kmalloc(OBUFLEN, GFP_KERNEL);
	if (!obuf)
		return -ENOMEM;

	oboff += OSCNPRINTF("Device name: %s\n", ccp->name);
	oboff += OSCNPRINTF("   RNG name: %s\n", ccp->rngname);
	oboff += OSCNPRINTF("   # Queues: %d\n", ccp->cmd_q_count);
	oboff += OSCNPRINTF("     # Cmds: %d\n", ccp->cmd_count);

	regval = ioread32(ccp->io_regs + CMD5_PSP_CCP_VERSION);
	oboff += OSCNPRINTF("    Version: %d\n", regval & RI_VERSION_NUM);
	oboff += OSCNPRINTF("    Engines:");
	if (regval & RI_AES_PRESENT)
		oboff += OSCNPRINTF(" AES");
	if (regval & RI_3DES_PRESENT)
		oboff += OSCNPRINTF(" 3DES");
	if (regval & RI_SHA_PRESENT)
		oboff += OSCNPRINTF(" SHA");
	if (regval & RI_RSA_PRESENT)
		oboff += OSCNPRINTF(" RSA");
	if (regval & RI_ECC_PRESENT)
		oboff += OSCNPRINTF(" ECC");
	if (regval & RI_ZDE_PRESENT)
		oboff += OSCNPRINTF(" ZDE");
	if (regval & RI_ZCE_PRESENT)
		oboff += OSCNPRINTF(" ZCE");
	if (regval & RI_TRNG_PRESENT)
		oboff += OSCNPRINTF(" TRNG");
	oboff += OSCNPRINTF("\n");
	oboff += OSCNPRINTF("     Queues: %d\n",
		   (regval & RI_NUM_VQM) >> RI_NVQM_SHIFT);
	oboff += OSCNPRINTF("LSB Entries: %d\n",
		   (regval & RI_LSB_ENTRIES) >> RI_NLSB_SHIFT);

	ret = simple_read_from_buffer(ubuf, count, offp, obuf, oboff);
	kfree(obuf);

	return ret;
}

/* Return a formatted buffer containing the current
 * statistics across all queues for a CCP.
 */
static ssize_t ccp5_debugfs_stats_read(struct file *filp, char __user *ubuf,
				       size_t count, loff_t *offp)
{
	struct ccp_device *ccp = filp->private_data;
	unsigned long total_xts_aes_ops = 0;
	unsigned long total_3des_ops = 0;
	unsigned long total_aes_ops = 0;
	unsigned long total_sha_ops = 0;
	unsigned long total_rsa_ops = 0;
	unsigned long total_ecc_ops = 0;
	unsigned long total_pt_ops = 0;
	unsigned long total_ops = 0;
	unsigned int oboff = 0;
	ssize_t ret = 0;
	unsigned int i;
	char *obuf;

	for (i = 0; i < ccp->cmd_q_count; i++) {
		struct ccp_cmd_queue *cmd_q = &ccp->cmd_q[i];

		total_ops += cmd_q->total_ops;
		total_aes_ops += cmd_q->total_aes_ops;
		total_xts_aes_ops += cmd_q->total_xts_aes_ops;
		total_3des_ops += cmd_q->total_3des_ops;
		total_sha_ops += cmd_q->total_sha_ops;
		total_rsa_ops += cmd_q->total_rsa_ops;
		total_pt_ops += cmd_q->total_pt_ops;
		total_ecc_ops += cmd_q->total_ecc_ops;
	}

	obuf = kmalloc(OBUFLEN, GFP_KERNEL);
	if (!obuf)
		return -ENOMEM;

	oboff += OSCNPRINTF("Total Interrupts Handled: %ld\n",
			    ccp->total_interrupts);
	oboff += OSCNPRINTF("        Total Operations: %ld\n",
			    total_ops);
	oboff += OSCNPRINTF("                     AES: %ld\n",
			    total_aes_ops);
	oboff += OSCNPRINTF("                 XTS AES: %ld\n",
			    total_xts_aes_ops);
	oboff += OSCNPRINTF("                     SHA: %ld\n",
			    total_3des_ops);
	oboff += OSCNPRINTF("                     SHA: %ld\n",
			    total_sha_ops);
	oboff += OSCNPRINTF("                     RSA: %ld\n",
			    total_rsa_ops);
	oboff += OSCNPRINTF("               Pass-Thru: %ld\n",
			    total_pt_ops);
	oboff += OSCNPRINTF("                     ECC: %ld\n",
			    total_ecc_ops);

	ret = simple_read_from_buffer(ubuf, count, offp, obuf, oboff);
	kfree(obuf);

	return ret;
}

/* Reset the counters in a queue
 */
static void ccp5_debugfs_reset_queue_stats(struct ccp_cmd_queue *cmd_q)
{
	cmd_q->total_ops = 0L;
	cmd_q->total_aes_ops = 0L;
	cmd_q->total_xts_aes_ops = 0L;
	cmd_q->total_3des_ops = 0L;
	cmd_q->total_sha_ops = 0L;
	cmd_q->total_rsa_ops = 0L;
	cmd_q->total_pt_ops = 0L;
	cmd_q->total_ecc_ops = 0L;
}

/* A value was written to the stats variable, which
 * should be used to reset the queue counters across
 * that device.
 */
static ssize_t ccp5_debugfs_stats_write(struct file *filp,
					const char __user *ubuf,
					size_t count, loff_t *offp)
{
	struct ccp_device *ccp = filp->private_data;
	int i;

	for (i = 0; i < ccp->cmd_q_count; i++)
		ccp5_debugfs_reset_queue_stats(&ccp->cmd_q[i]);
	ccp->total_interrupts = 0L;

	return count;
}

/* Return a formatted buffer containing the current information
 * for that queue
 */
static ssize_t ccp5_debugfs_queue_read(struct file *filp, char __user *ubuf,
				       size_t count, loff_t *offp)
{
	struct ccp_cmd_queue *cmd_q = filp->private_data;
	unsigned int oboff = 0;
	unsigned int regval;
	ssize_t ret;
	char *obuf;

	if (!cmd_q)
		return 0;

	obuf = kmalloc(OBUFLEN, GFP_KERNEL);
	if (!obuf)
		return -ENOMEM;

	oboff += OSCNPRINTF("  Total Queue Operations: %ld\n",
			    cmd_q->total_ops);
	oboff += OSCNPRINTF("                     AES: %ld\n",
			    cmd_q->total_aes_ops);
	oboff += OSCNPRINTF("                 XTS AES: %ld\n",
			    cmd_q->total_xts_aes_ops);
	oboff += OSCNPRINTF("                     SHA: %ld\n",
			    cmd_q->total_3des_ops);
	oboff += OSCNPRINTF("                     SHA: %ld\n",
			    cmd_q->total_sha_ops);
	oboff += OSCNPRINTF("                     RSA: %ld\n",
			    cmd_q->total_rsa_ops);
	oboff += OSCNPRINTF("               Pass-Thru: %ld\n",
			    cmd_q->total_pt_ops);
	oboff += OSCNPRINTF("                     ECC: %ld\n",
			    cmd_q->total_ecc_ops);

	regval = ioread32(cmd_q->reg_int_enable);
	oboff += OSCNPRINTF("      Enabled Interrupts:");
	if (regval & INT_EMPTY_QUEUE)
		oboff += OSCNPRINTF(" EMPTY");
	if (regval & INT_QUEUE_STOPPED)
		oboff += OSCNPRINTF(" STOPPED");
	if (regval & INT_ERROR)
		oboff += OSCNPRINTF(" ERROR");
	if (regval & INT_COMPLETION)
		oboff += OSCNPRINTF(" COMPLETION");
	oboff += OSCNPRINTF("\n");

	ret = simple_read_from_buffer(ubuf, count, offp, obuf, oboff);
	kfree(obuf);

	return ret;
}

/* A value was written to the stats variable for a
 * queue. Reset the queue counters to this value.
 */
static ssize_t ccp5_debugfs_queue_write(struct file *filp,
					const char __user *ubuf,
					size_t count, loff_t *offp)
{
	struct ccp_cmd_queue *cmd_q = filp->private_data;

	ccp5_debugfs_reset_queue_stats(cmd_q);

	return count;
}

static const struct file_operations ccp_debugfs_info_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ccp5_debugfs_info_read,
	.write = NULL,
};

static const struct file_operations ccp_debugfs_queue_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ccp5_debugfs_queue_read,
	.write = ccp5_debugfs_queue_write,
};

static const struct file_operations ccp_debugfs_stats_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ccp5_debugfs_stats_read,
	.write = ccp5_debugfs_stats_write,
};

static struct dentry *ccp_debugfs_dir;
static DEFINE_RWLOCK(ccp_debugfs_lock);

#define	MAX_NAME_LEN	20

void ccp5_debugfs_setup(struct ccp_device *ccp)
{
	struct ccp_cmd_queue *cmd_q;
	char name[MAX_NAME_LEN + 1];
	struct dentry *debugfs_info;
	struct dentry *debugfs_stats;
	struct dentry *debugfs_q_instance;
	struct dentry *debugfs_q_stats;
	unsigned long flags;
	int i;

	if (!debugfs_initialized())
		return;

	write_lock_irqsave(&ccp_debugfs_lock, flags);
	if (!ccp_debugfs_dir)
		ccp_debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	write_unlock_irqrestore(&ccp_debugfs_lock, flags);
	if (!ccp_debugfs_dir)
		return;

	ccp->debugfs_instance = debugfs_create_dir(ccp->name, ccp_debugfs_dir);
	if (!ccp->debugfs_instance)
		goto err;

	debugfs_info = debugfs_create_file("info", 0400,
					   ccp->debugfs_instance, ccp,
					   &ccp_debugfs_info_ops);
	if (!debugfs_info)
		goto err;

	debugfs_stats = debugfs_create_file("stats", 0600,
					    ccp->debugfs_instance, ccp,
					    &ccp_debugfs_stats_ops);
	if (!debugfs_stats)
		goto err;

	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		snprintf(name, MAX_NAME_LEN - 1, "q%d", cmd_q->id);

		debugfs_q_instance =
			debugfs_create_dir(name, ccp->debugfs_instance);
		if (!debugfs_q_instance)
			goto err;

		debugfs_q_stats =
			debugfs_create_file("stats", 0600,
					    debugfs_q_instance, cmd_q,
					    &ccp_debugfs_queue_ops);
		if (!debugfs_q_stats)
			goto err;
	}

	return;

err:
	debugfs_remove_recursive(ccp->debugfs_instance);
}

void ccp5_debugfs_destroy(void)
{
	debugfs_remove_recursive(ccp_debugfs_dir);
}
