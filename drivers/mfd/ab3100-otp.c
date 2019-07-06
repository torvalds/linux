// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/mfd/ab3100_otp.c
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * Driver to read out OTP from the AB3100 Mixed-signal circuit
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

/* The OTP registers */
#define AB3100_OTP0		0xb0
#define AB3100_OTP1		0xb1
#define AB3100_OTP2		0xb2
#define AB3100_OTP3		0xb3
#define AB3100_OTP4		0xb4
#define AB3100_OTP5		0xb5
#define AB3100_OTP6		0xb6
#define AB3100_OTP7		0xb7
#define AB3100_OTPP		0xbf

/**
 * struct ab3100_otp
 * @dev containing device
 * @locked whether the OTP is locked, after locking, no more bits
 *       can be changed but before locking it is still possible
 *       to change bits from 1->0.
 * @freq clocking frequency for the OTP, this frequency is either
 *       32768Hz or 1MHz/30
 * @paf product activation flag, indicates whether this is a real
 *       product (paf true) or a lab board etc (paf false)
 * @imeich if this is set it is possible to override the
 *       IMEI number found in the tac, fac and svn fields with
 *       (secured) software
 * @cid customer ID
 * @tac type allocation code of the IMEI
 * @fac final assembly code of the IMEI
 * @svn software version number of the IMEI
 * @debugfs a debugfs file used when dumping to file
 */
struct ab3100_otp {
	struct device *dev;
	bool locked;
	u32 freq;
	bool paf;
	bool imeich;
	u16 cid:14;
	u32 tac:20;
	u8 fac;
	u32 svn:20;
	struct dentry *debugfs;
};

static int __init ab3100_otp_read(struct ab3100_otp *otp)
{
	u8 otpval[8];
	u8 otpp;
	int err;

	err = abx500_get_register_interruptible(otp->dev, 0,
		AB3100_OTPP, &otpp);
	if (err) {
		dev_err(otp->dev, "unable to read OTPP register\n");
		return err;
	}

	err = abx500_get_register_page_interruptible(otp->dev, 0,
		AB3100_OTP0, otpval, 8);
	if (err) {
		dev_err(otp->dev, "unable to read OTP register page\n");
		return err;
	}

	/* Cache OTP properties, they never change by nature */
	otp->locked = (otpp & 0x80);
	otp->freq = (otpp & 0x40) ? 32768 : 34100;
	otp->paf = (otpval[1] & 0x80);
	otp->imeich = (otpval[1] & 0x40);
	otp->cid = ((otpval[1] << 8) | otpval[0]) & 0x3fff;
	otp->tac = ((otpval[4] & 0x0f) << 16) | (otpval[3] << 8) | otpval[2];
	otp->fac = ((otpval[5] & 0x0f) << 4) | (otpval[4] >> 4);
	otp->svn = (otpval[7] << 12) | (otpval[6] << 4) | (otpval[5] >> 4);
	return 0;
}

/*
 * This is a simple debugfs human-readable file that dumps out
 * the contents of the OTP.
 */
#ifdef CONFIG_DEBUG_FS
static int ab3100_show_otp(struct seq_file *s, void *v)
{
	struct ab3100_otp *otp = s->private;

	seq_printf(s, "OTP is %s\n", otp->locked ? "LOCKED" : "UNLOCKED");
	seq_printf(s, "OTP clock switch startup is %uHz\n", otp->freq);
	seq_printf(s, "PAF is %s\n", otp->paf ? "SET" : "NOT SET");
	seq_printf(s, "IMEI is %s\n", otp->imeich ?
		   "CHANGEABLE" : "NOT CHANGEABLE");
	seq_printf(s, "CID: 0x%04x (decimal: %d)\n", otp->cid, otp->cid);
	seq_printf(s, "IMEI: %u-%u-%u\n", otp->tac, otp->fac, otp->svn);
	return 0;
}

static int ab3100_otp_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab3100_show_otp, inode->i_private);
}

static const struct file_operations ab3100_otp_operations = {
	.open		= ab3100_otp_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init ab3100_otp_init_debugfs(struct device *dev,
					  struct ab3100_otp *otp)
{
	otp->debugfs = debugfs_create_file("ab3100_otp", S_IFREG | S_IRUGO,
					   NULL, otp,
					   &ab3100_otp_operations);
	if (!otp->debugfs) {
		dev_err(dev, "AB3100 debugfs OTP file registration failed!\n");
		return -ENOENT;
	}
	return 0;
}

static void __exit ab3100_otp_exit_debugfs(struct ab3100_otp *otp)
{
	debugfs_remove(otp->debugfs);
}
#else
/* Compile this out if debugfs not selected */
static inline int __init ab3100_otp_init_debugfs(struct device *dev,
						 struct ab3100_otp *otp)
{
	return 0;
}

static inline void __exit ab3100_otp_exit_debugfs(struct ab3100_otp *otp)
{
}
#endif

#define SHOW_AB3100_ATTR(name) \
static ssize_t ab3100_otp_##name##_show(struct device *dev, \
			       struct device_attribute *attr, \
			       char *buf) \
{\
	struct ab3100_otp *otp = dev_get_drvdata(dev); \
	return sprintf(buf, "%u\n", otp->name); \
}

SHOW_AB3100_ATTR(locked)
SHOW_AB3100_ATTR(freq)
SHOW_AB3100_ATTR(paf)
SHOW_AB3100_ATTR(imeich)
SHOW_AB3100_ATTR(cid)
SHOW_AB3100_ATTR(fac)
SHOW_AB3100_ATTR(tac)
SHOW_AB3100_ATTR(svn)

static struct device_attribute ab3100_otp_attrs[] = {
	__ATTR(locked, S_IRUGO, ab3100_otp_locked_show, NULL),
	__ATTR(freq, S_IRUGO, ab3100_otp_freq_show, NULL),
	__ATTR(paf, S_IRUGO, ab3100_otp_paf_show, NULL),
	__ATTR(imeich, S_IRUGO, ab3100_otp_imeich_show, NULL),
	__ATTR(cid, S_IRUGO, ab3100_otp_cid_show, NULL),
	__ATTR(fac, S_IRUGO, ab3100_otp_fac_show, NULL),
	__ATTR(tac, S_IRUGO, ab3100_otp_tac_show, NULL),
	__ATTR(svn, S_IRUGO, ab3100_otp_svn_show, NULL),
};

static int __init ab3100_otp_probe(struct platform_device *pdev)
{
	struct ab3100_otp *otp;
	int err = 0;
	int i;

	otp = devm_kzalloc(&pdev->dev, sizeof(struct ab3100_otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp->dev = &pdev->dev;

	/* Replace platform data coming in with a local struct */
	platform_set_drvdata(pdev, otp);

	err = ab3100_otp_read(otp);
	if (err)
		return err;

	dev_info(&pdev->dev, "AB3100 OTP readout registered\n");

	/* sysfs entries */
	for (i = 0; i < ARRAY_SIZE(ab3100_otp_attrs); i++) {
		err = device_create_file(&pdev->dev,
					 &ab3100_otp_attrs[i]);
		if (err)
			goto err;
	}

	/* debugfs entries */
	err = ab3100_otp_init_debugfs(&pdev->dev, otp);
	if (err)
		goto err;

	return 0;

err:
	while (--i >= 0)
		device_remove_file(&pdev->dev, &ab3100_otp_attrs[i]);
	return err;
}

static int __exit ab3100_otp_remove(struct platform_device *pdev)
{
	struct ab3100_otp *otp = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(ab3100_otp_attrs); i++)
		device_remove_file(&pdev->dev,
				   &ab3100_otp_attrs[i]);
	ab3100_otp_exit_debugfs(otp);
	return 0;
}

static struct platform_driver ab3100_otp_driver = {
	.driver = {
		.name = "ab3100-otp",
	},
	.remove	 = __exit_p(ab3100_otp_remove),
};

module_platform_driver_probe(ab3100_otp_driver, ab3100_otp_probe);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("AB3100 OTP Readout Driver");
MODULE_LICENSE("GPL");
