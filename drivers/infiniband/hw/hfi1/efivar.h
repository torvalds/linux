/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

#ifndef _HFI1_EFIVAR_H
#define _HFI1_EFIVAR_H

#include <linux/efi.h>

#include "hfi.h"

int read_hfi1_efi_var(struct hfi1_devdata *dd, const char *kind,
		      unsigned long *size, void **return_data);

#endif /* _HFI1_EFIVAR_H */
