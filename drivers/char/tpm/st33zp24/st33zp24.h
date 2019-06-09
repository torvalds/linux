/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics TPM Linux driver for TPM ST33ZP24
 * Copyright (C) 2009 - 2016  STMicroelectronics
 */

#ifndef __LOCAL_ST33ZP24_H__
#define __LOCAL_ST33ZP24_H__

#define TPM_WRITE_DIRECTION	0x80
#define ST33ZP24_BUFSIZE	2048

struct st33zp24_dev {
	struct tpm_chip *chip;
	void *phy_id;
	const struct st33zp24_phy_ops *ops;
	int locality;
	int irq;
	u32 intrs;
	int io_lpcpd;
	wait_queue_head_t read_queue;
};


struct st33zp24_phy_ops {
	int (*send)(void *phy_id, u8 tpm_register, u8 *tpm_data, int tpm_size);
	int (*recv)(void *phy_id, u8 tpm_register, u8 *tpm_data, int tpm_size);
};

#ifdef CONFIG_PM_SLEEP
int st33zp24_pm_suspend(struct device *dev);
int st33zp24_pm_resume(struct device *dev);
#endif

int st33zp24_probe(void *phy_id, const struct st33zp24_phy_ops *ops,
		   struct device *dev, int irq, int io_lpcpd);
int st33zp24_remove(struct tpm_chip *chip);
#endif /* __LOCAL_ST33ZP24_H__ */
