/*
 * STMicroelectronics TPM Linux driver for TPM ST33ZP24
 * Copyright (C) 2009 - 2015  STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LOCAL_ST33ZP24_H__
#define __LOCAL_ST33ZP24_H__

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
