/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_DC_H
#define ADF_DC_H

struct adf_accel_dev;

enum adf_dc_algo {
	QAT_DEFLATE,
	QAT_LZ4,
	QAT_LZ4S,
	QAT_ZSTD,
};

int qat_comp_build_ctx(struct adf_accel_dev *accel_dev, void *ctx, enum adf_dc_algo algo);

#endif /* ADF_DC_H */
