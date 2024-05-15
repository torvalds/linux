/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */
#ifndef ADF_PFVF_UTILS_H
#define ADF_PFVF_UTILS_H

#include <linux/types.h>
#include "adf_pfvf_msg.h"

/* How long to wait for far side to acknowledge receipt */
#define ADF_PFVF_MSG_ACK_DELAY_US	4
#define ADF_PFVF_MSG_ACK_MAX_DELAY_US	(1 * USEC_PER_SEC)

u8 adf_pfvf_calc_blkmsg_crc(u8 const *buf, u8 buf_len);
void adf_pfvf_crc_init(void);

struct pfvf_field_format {
	u8  offset;
	u32 mask;
};

struct pfvf_csr_format {
	struct pfvf_field_format type;
	struct pfvf_field_format data;
};

u32 adf_pfvf_csr_msg_of(struct adf_accel_dev *accel_dev, struct pfvf_message msg,
			const struct pfvf_csr_format *fmt);
struct pfvf_message adf_pfvf_message_of(struct adf_accel_dev *accel_dev, u32 raw_msg,
					const struct pfvf_csr_format *fmt);

static inline u8 adf_vf_compat_checker(u8 vf_compat_ver)
{
	if (vf_compat_ver == 0)
		return ADF_PF2VF_VF_INCOMPATIBLE;

	if (vf_compat_ver <= ADF_PFVF_COMPAT_THIS_VERSION)
		return ADF_PF2VF_VF_COMPATIBLE;

	return ADF_PF2VF_VF_COMPAT_UNKNOWN;
}

#endif /* ADF_PFVF_UTILS_H */
