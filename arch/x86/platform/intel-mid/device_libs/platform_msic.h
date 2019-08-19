/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * platform_msic.h: MSIC platform data header file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */
#ifndef _PLATFORM_MSIC_H_
#define _PLATFORM_MSIC_H_

extern struct intel_msic_platform_data msic_pdata;

void *msic_generic_platform_data(void *info, enum intel_msic_block block);

#endif
