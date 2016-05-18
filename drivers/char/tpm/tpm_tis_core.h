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
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __TPM_TIS_CORE_H__
#define __TPM_TIS_CORE_H__

#include "tpm.h"

struct tpm_tis_data {
	u16 manufacturer_id;
	int locality;
	int irq;
	bool irq_tested;
	wait_queue_head_t int_queue;
	wait_queue_head_t read_queue;
	const struct tpm_tis_phy_ops *phy_ops;
};

struct tpm_tis_phy_ops {
	int (*read_bytes)(struct tpm_tis_data *data, u32 addr, u16 len,
			  u8 *result);
	int (*write_bytes)(struct tpm_tis_data *data, u32 addr, u16 len,
			   u8 *value);
	int (*read16)(struct tpm_tis_data *data, u32 addr, u16 *result);
	int (*read32)(struct tpm_tis_data *data, u32 addr, u32 *result);
	int (*write32)(struct tpm_tis_data *data, u32 addr, u32 src);
};

static inline int tpm_tis_read_bytes(struct tpm_tis_data *data, u32 addr,
				     u16 len, u8 *result)
{
	return data->phy_ops->read_bytes(data, addr, len, result);
}

static inline int tpm_tis_read8(struct tpm_tis_data *data, u32 addr, u8 *result)
{
	return data->phy_ops->read_bytes(data, addr, 1, result);
}

static inline int tpm_tis_read16(struct tpm_tis_data *data, u32 addr,
				 u16 *result)
{
	return data->phy_ops->read16(data, addr, result);
}

static inline int tpm_tis_read32(struct tpm_tis_data *data, u32 addr,
				 u32 *result)
{
	return data->phy_ops->read32(data, addr, result);
}

static inline int tpm_tis_write_bytes(struct tpm_tis_data *data, u32 addr,
				      u16 len, u8 *value)
{
	return data->phy_ops->write_bytes(data, addr, len, value);
}

static inline int tpm_tis_write8(struct tpm_tis_data *data, u32 addr, u8 value)
{
	return data->phy_ops->write_bytes(data, addr, 1, &value);
}

static inline int tpm_tis_write32(struct tpm_tis_data *data, u32 addr,
				  u32 value)
{
	return data->phy_ops->write32(data, addr, value);
}

#endif
