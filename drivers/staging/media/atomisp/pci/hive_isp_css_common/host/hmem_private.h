/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __HMEM_PRIVATE_H_INCLUDED__
#define __HMEM_PRIVATE_H_INCLUDED__

#include "hmem_public.h"

#include "assert_support.h"

STORAGE_CLASS_HMEM_C size_t sizeof_hmem(
    const hmem_ID_t		ID)
{
	assert(ID < N_HMEM_ID);
	(void)ID;
	return HMEM_SIZE * sizeof(hmem_data_t);
}

#endif /* __HMEM_PRIVATE_H_INCLUDED__ */
