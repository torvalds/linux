/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_GEN4_RAS_H_
#define ADF_GEN4_RAS_H_

#include <linux/bits.h>

struct adf_ras_ops;

/* ERRSOU0 Correctable error mask*/
#define ADF_GEN4_ERRSOU0_BIT				BIT(0)

/* HI AE Correctable error log */
#define ADF_GEN4_HIAECORERRLOG_CPP0			0x41A308

/* HI AE Correctable error log enable */
#define ADF_GEN4_HIAECORERRLOGENABLE_CPP0		0x41A318

void adf_gen4_init_ras_ops(struct adf_ras_ops *ras_ops);

#endif /* ADF_GEN4_RAS_H_ */
