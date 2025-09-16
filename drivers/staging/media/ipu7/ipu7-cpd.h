/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 - 2025 Intel Corporation
 */

#ifndef IPU7_CPD_H
#define IPU7_CPD_H

struct ipu7_device;

int ipu7_cpd_validate_cpd_file(struct ipu7_device *isp,
			       const void *cpd_file,
			       unsigned long cpd_file_size);
int ipu7_cpd_copy_binary(const void *cpd, const char *name,
			 void *code_region, u32 *entry);
#endif /* IPU7_CPD_H */
