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

#ifndef __GDC_PUBLIC_H_INCLUDED__
#define __GDC_PUBLIC_H_INCLUDED__

/*! Write the bicubic interpolation table of GDC[ID]

 \param	ID[in]				GDC identifier
 \param data[in]			The data matrix to be written

 \pre
	- data must point to a matrix[4][HRT_GDC_N]

 \implementation dependent
	- The value of "HRT_GDC_N" is device specific
	- The LUT should not be partially written
	- The LUT format is a quadri-phase interpolation
	  table. The layout is device specific
	- The range of the values data[n][m] is device
	  specific

 \return none, GDC[ID].lut[0...3][0...HRT_GDC_N-1] = data
 */
extern void gdc_lut_store(
	const gdc_ID_t		ID,
	const int			data[4][HRT_GDC_N]);

/*! Convert the bicubic interpolation table of GDC[ID] to the ISP-specific format

 \param	ID[in]				GDC identifier
 \param in_lut[in]			The data matrix to be converted
 \param out_lut[out]			The data matrix as the output of conversion
 */
extern void gdc_lut_convert_to_isp_format(
	const int in_lut[4][HRT_GDC_N],
	int out_lut[4][HRT_GDC_N]);

/*! Return the integer representation of 1.0 of GDC[ID]
 
 \param	ID[in]				GDC identifier

 \return unity
 */
extern int gdc_get_unity(
	const gdc_ID_t		ID);

#endif /* __GDC_PUBLIC_H_INCLUDED__ */
