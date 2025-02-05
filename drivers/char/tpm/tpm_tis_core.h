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

#include "tpm.h"

enum tis_access {
	TPM_ACCESS_VALID = 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY = 0x20,
	TPM_ACCESS_REQUEST_PENDING = 0x04,
	TPM_ACCESS_REQUEST_USE = 0x02,
};

enum tis_status {
	TPM_STS_VALID = 0x80,
	TPM_STS_COMMAND_READY = 0x40,
	TPM_STS_GO = 0x20,
	TPM_STS_DATA_AVAIL = 0x10,
	TPM_STS_DATA_EXPECT = 0x08,
	TPM_STS_RESPONSE_RETRY = 0x02,
	TPM_STS_READ_ZERO = 0x23, /* bits that must be zero on read */
};

enum tis_int_flags {
	TPM_GLOBAL_INT_ENABLE = 0x80000000,
	TPM_INTF_BURST_COUNT_STATIC = 0x100,
	TPM_INTF_CMD_READY_INT = 0x080,
	TPM_INTF_INT_EDGE_FALLING = 0x040,
	TPM_INTF_INT_EDGE_RISING = 0x020,
	TPM_INTF_INT_LEVEL_LOW = 0x010,
	TPM_INTF_INT_LEVEL_HIGH = 0x008,
	TPM_INTF_LOCALITY_CHANGE_INT = 0x004,
	TPM_INTF_STS_VALID_INT = 0x002,
	TPM_INTF_DATA_AVAIL_INT = 0x001,
};

enum tis_defaults {
	TIS_MEM_LEN = 0x5000,
	TIS_SHORT_TIMEOUT = 750,	/* ms */
	TIS_LONG_TIMEOUT = 2000,	/* 2 sec */
	TIS_TIMEOUT_MIN_ATML = 14700,	/* usecs */
	TIS_TIMEOUT_MAX_ATML = 15000,	/* usecs */
};

/* Some timeout values are needed before it is known whether the chip is
 * TPM 1.0 or TPM 2.0.
 */
#define TIS_TIMEOUT_A_MAX	max_t(int, TIS_SHORT_TIMEOUT, TPM2_TIMEOUT_A)
#define TIS_TIMEOUT_B_MAX	max_t(int, TIS_LONG_TIMEOUT, TPM2_TIMEOUT_B)
#define TIS_TIMEOUT_C_MAX	max_t(int, TIS_SHORT_TIMEOUT, TPM2_TIMEOUT_C)
#define TIS_TIMEOUT_D_MAX	max_t(int, TIS_SHORT_TIMEOUT, TPM2_TIMEOUT_D)

#define	TPM_ACCESS(l)			(0x0000 | ((l) << 12))
#define	TPM_INT_ENABLE(l)		(0x0008 | ((l) << 12))
#define	TPM_INT_VECTOR(l)		(0x000C | ((l) << 12))
#define	TPM_INT_STATUS(l)		(0x0010 | ((l) << 12))
#define	TPM_INTF_CAPS(l)		(0x0014 | ((l) << 12))
#define	TPM_STS(l)			(0x0018 | ((l) << 12))
#define	TPM_STS3(l)			(0x001b | ((l) << 12))
#define	TPM_DATA_FIFO(l)		(0x0024 | ((l) << 12))

#define	TPM_DID_VID(l)			(0x0F00 | ((l) << 12))
#define	TPM_RID(l)			(0x0F04 | ((l) << 12))

#define LPC_CNTRL_OFFSET		0x84
#define LPC_CLKRUN_EN			(1 << 2)
#define INTEL_LEGACY_BLK_BASE_ADDR	0xFED08000
#define ILB_REMAP_SIZE			0x100

enum tpm_tis_flags {
	TPM_TIS_ITPM_WORKAROUND		= 0,
	TPM_TIS_INVALID_STATUS		= 1,
	TPM_TIS_DEFAULT_CANCELLATION	= 2,
	TPM_TIS_IRQ_TESTED		= 3,
};

struct tpm_tis_data {
	struct tpm_chip *chip;
	u16 manufacturer_id;
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
