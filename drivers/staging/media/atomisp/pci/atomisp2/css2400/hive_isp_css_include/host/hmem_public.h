/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __HMEM_PUBLIC_H_INCLUDED__
#define __HMEM_PUBLIC_H_INCLUDED__

#include <linux/types.h>		/* size_t */

/*! Return the size of HMEM[ID]

 \param	ID[in]				HMEM identifier

 \Note: The size is the byte size of the area it occupies
		in the address map. I.e. disregarding internal structure

 \return sizeof(HMEM[ID])
 */
STORAGE_CLASS_HMEM_H size_t sizeof_hmem(
	const hmem_ID_t		ID);

#endif /* __HMEM_PUBLIC_H_INCLUDED__ */
