// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2021 Intel Corporation */
#include <linux/crc8.h>
#include <linux/pci.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_utils.h"

/* CRC Calculation */
DECLARE_CRC8_TABLE(pfvf_crc8_table);
#define ADF_PFVF_CRC8_POLYNOMIAL 0x97

void adf_pfvf_crc_init(void)
{
	crc8_populate_msb(pfvf_crc8_table, ADF_PFVF_CRC8_POLYNOMIAL);
}

u8 adf_pfvf_calc_blkmsg_crc(u8 const *buf, u8 buf_len)
{
	return crc8(pfvf_crc8_table, buf, buf_len, CRC8_INIT_VALUE);
}

static bool set_value_on_csr_msg(struct adf_accel_dev *accel_dev, u32 *csr_msg,
				 u32 value, const struct pfvf_field_format *fmt)
{
	if (unlikely((value & fmt->mask) != value)) {
		dev_err(&GET_DEV(accel_dev),
			"PFVF message value 0x%X out of range, %u max allowed\n",
			value, fmt->mask);
		return false;
	}

	*csr_msg |= value << fmt->offset;

	return true;
}

u32 adf_pfvf_csr_msg_of(struct adf_accel_dev *accel_dev,
			struct pfvf_message msg,
			const struct pfvf_csr_format *fmt)
{
	u32 csr_msg = 0;

	if (!set_value_on_csr_msg(accel_dev, &csr_msg, msg.type, &fmt->type) ||
	    !set_value_on_csr_msg(accel_dev, &csr_msg, msg.data, &fmt->data))
		return 0;

	return csr_msg | ADF_PFVF_MSGORIGIN_SYSTEM;
}

struct pfvf_message adf_pfvf_message_of(struct adf_accel_dev *accel_dev, u32 csr_msg,
					const struct pfvf_csr_format *fmt)
{
	struct pfvf_message msg = { 0 };

	msg.type = (csr_msg >> fmt->type.offset) & fmt->type.mask;
	msg.data = (csr_msg >> fmt->data.offset) & fmt->data.mask;

	if (unlikely(!msg.type))
		dev_err(&GET_DEV(accel_dev),
			"Invalid PFVF msg with no type received\n");

	return msg;
}
