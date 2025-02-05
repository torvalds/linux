/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Secure Processor device driver, security attributes
 *
 * Copyright (C) 2023-2024 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#ifndef __HSTI_H
#define __HSTI_H

extern struct attribute_group psp_security_attr_group;

int psp_init_hsti(struct psp_device *psp);

#endif /* __HSTI_H */
