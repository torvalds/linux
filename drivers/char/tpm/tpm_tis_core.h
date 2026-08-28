/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2005, 2006 IBM Corporation
 * Copyright (C) 2014, 2015 Intel Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2, revision 1.0.
 */

#ifndef __TPM_TIS_CORE_H__
#define __TPM_TIS_CORE_H__

#include <linux/tpm_ptp.h>
#include "tpm.h"

enum tpm_tis_flags {
	TPM_TIS_ITPM_WORKAROUND		= 0,
	TPM_TIS_INVALID_STATUS		= 1,
	TPM_TIS_DEFAULT_CANCELLATION	= 2,
	TPM_TIS_IRQ_TESTED		= 3,
	TPM_TIS_STATUS_VALID_RETRY	= 4,
	TPM_TIS_SETTLE_AFTER_RELINQUISH	= 5,
};

struct tpm_tis_data {
	struct tpm_chip *chip;
	u32 did_vid;
	struct mutex locality_count_mutex;
	unsigned int locality_count;
	int locality;
	int irq;
	struct work_struct free_irq_work;
	unsigned long last_unhandled_irq;
	unsigned int unhandled_irqs;
	unsigned int int_mask;
	unsigned long flags;
	void __iomem *ilb_base_addr;
	u16 clkrun_enabled;
	wait_queue_head_t int_queue;
	wait_queue_head_t read_queue;
	const struct tpm_tis_phy_ops *phy_ops;
	unsigned short rng_quality;
	unsigned int timeout_min; /* usecs */
	unsigned int timeout_max; /* usecs */
};

/*
 * IO modes to indicate how many bytes should be read/written at once in the
 * tpm_tis_phy_ops read_bytes/write_bytes calls. Use TPM_TIS_PHYS_8 to
 * receive/transmit byte-wise, TPM_TIS_PHYS_16 for two bytes etc.
 */
enum tpm_tis_io_mode {
	TPM_TIS_PHYS_8,
	TPM_TIS_PHYS_16,
	TPM_TIS_PHYS_32,
};

struct tpm_tis_phy_ops {
	/* data is passed in little endian */
	int (*read_bytes)(struct tpm_tis_data *data, u32 addr, u16 len,
			  u8 *result, enum tpm_tis_io_mode mode);
	int (*write_bytes)(struct tpm_tis_data *data, u32 addr, u16 len,
			   const u8 *value, enum tpm_tis_io_mode mode);
	int (*verify_crc)(struct tpm_tis_data *data, size_t len,
			  const u8 *value);
};

static inline int tpm_tis_read_bytes(struct tpm_tis_data *data, u32 addr,
				     u16 len, u8 *result)
{
	return data->phy_ops->read_bytes(data, addr, len, result,
					 TPM_TIS_PHYS_8);
}

static inline int tpm_tis_read8(struct tpm_tis_data *data, u32 addr, u8 *result)
{
	return data->phy_ops->read_bytes(data, addr, 1, result, TPM_TIS_PHYS_8);
}

static inline int tpm_tis_read16(struct tpm_tis_data *data, u32 addr,
				 u16 *result)
{
	__le16 result_le;
	int rc;

	rc = data->phy_ops->read_bytes(data, addr, sizeof(u16),
				       (u8 *)&result_le, TPM_TIS_PHYS_16);
	if (!rc)
		*result = le16_to_cpu(result_le);

	return rc;
}

static inline int tpm_tis_read32(struct tpm_tis_data *data, u32 addr,
				 u32 *result)
{
	__le32 result_le;
	int rc;

	rc = data->phy_ops->read_bytes(data, addr, sizeof(u32),
				       (u8 *)&result_le, TPM_TIS_PHYS_32);
	if (!rc)
		*result = le32_to_cpu(result_le);

	return rc;
}

static inline int tpm_tis_write_bytes(struct tpm_tis_data *data, u32 addr,
				      u16 len, const u8 *value)
{
	return data->phy_ops->write_bytes(data, addr, len, value,
					  TPM_TIS_PHYS_8);
}

static inline int tpm_tis_write8(struct tpm_tis_data *data, u32 addr, u8 value)
{
	return data->phy_ops->write_bytes(data, addr, 1, &value,
					  TPM_TIS_PHYS_8);
}

static inline int tpm_tis_write32(struct tpm_tis_data *data, u32 addr,
				  u32 value)
{
	__le32 value_le;
	int rc;

	value_le = cpu_to_le32(value);
	rc =  data->phy_ops->write_bytes(data, addr, sizeof(u32),
					 (u8 *)&value_le, TPM_TIS_PHYS_32);
	return rc;
}

static inline int tpm_tis_verify_crc(struct tpm_tis_data *data, size_t len,
				     const u8 *value)
{
	if (!data->phy_ops->verify_crc)
		return 0;
	return data->phy_ops->verify_crc(data, len, value);
}

static inline bool is_bsw(void)
{
#ifdef CONFIG_X86
	return (boot_cpu_data.x86_vfm == INTEL_ATOM_AIRMONT) ? 1 : 0;
#else
	return false;
#endif
}

void tpm_tis_remove(struct tpm_chip *chip);
int tpm_tis_core_init(struct device *dev, struct tpm_tis_data *priv, int irq,
		      const struct tpm_tis_phy_ops *phy_ops,
		      acpi_handle acpi_dev_handle);

#ifdef CONFIG_PM_SLEEP
int tpm_tis_resume(struct device *dev);
#endif

#endif
