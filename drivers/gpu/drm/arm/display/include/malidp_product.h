/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _MALIDP_PRODUCT_H_
#define _MALIDP_PRODUCT_H_

/* Product identification */
#define MALIDP_CORE_ID(__product, __major, __minor, __status) \
	((((__product) & 0xFFFF) << 16) | (((__major) & 0xF) << 12) | \
	(((__minor) & 0xF) << 8) | ((__status) & 0xFF))

#define MALIDP_CORE_ID_PRODUCT_ID(__core_id) ((__u32)(__core_id) >> 16)
#define MALIDP_CORE_ID_MAJOR(__core_id)      (((__u32)(__core_id) >> 12) & 0xF)
#define MALIDP_CORE_ID_MINOR(__core_id)      (((__u32)(__core_id) >> 8) & 0xF)
#define MALIDP_CORE_ID_STATUS(__core_id)     (((__u32)(__core_id)) & 0xFF)

/* Mali-display product IDs */
#define MALIDP_D71_PRODUCT_ID   0x0071

union komeda_config_id {
	struct {
		__u32	max_line_sz:16,
			n_pipelines:2,
			n_scalers:2, /* number of scalers per pipeline */
			n_layers:3, /* number of layers per pipeline */
			n_richs:3, /* number of rich layers per pipeline */
			reserved_bits:6;
	};
	__u32 value;
};

#endif /* _MALIDP_PRODUCT_H_ */
