/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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

/* The name "gdc.h is already taken" */
#include "gdc_device.h"

#include "device_access.h"

#include "assert_support.h"

/*
 * Local function declarations
 */
STORAGE_CLASS_INLINE void gdc_reg_store(
	const gdc_ID_t		ID,
	const unsigned int	reg,
	const hrt_data		value);

STORAGE_CLASS_INLINE hrt_data gdc_reg_load(
	const gdc_ID_t		ID,
	const unsigned int	reg);


#ifndef __INLINE_GDC__
#include "gdc_private.h"
#endif /* __INLINE_GDC__ */

/*
 * Exported function implementations
 */
void gdc_lut_store(
	const gdc_ID_t		ID,
	const int			data[4][HRT_GDC_N])
{
	unsigned int i, lut_offset = HRT_GDC_LUT_IDX;

	assert(ID < N_GDC_ID);
	assert(HRT_GDC_LUT_COEFF_OFFSET <= (4*sizeof(hrt_data)));

	for (i = 0; i < HRT_GDC_N; i++) {
		hrt_data	entry_0 = data[0][i] & HRT_GDC_BCI_COEF_MASK;
		hrt_data	entry_1 = data[1][i] & HRT_GDC_BCI_COEF_MASK;
		hrt_data	entry_2 = data[2][i] & HRT_GDC_BCI_COEF_MASK;
		hrt_data	entry_3 = data[3][i] & HRT_GDC_BCI_COEF_MASK;

		hrt_data	word_0 = entry_0 |
			(entry_1 << HRT_GDC_LUT_COEFF_OFFSET);
		hrt_data	word_1 = entry_2 |
			(entry_3 << HRT_GDC_LUT_COEFF_OFFSET);

		gdc_reg_store(ID, lut_offset++, word_0);
		gdc_reg_store(ID, lut_offset++, word_1);
	}
return;
}

/*
 * Input LUT format:
 * c0[0-1023], c1[0-1023], c2[0-1023] c3[0-1023]
 *
 * Output LUT format (interleaved):
 * c0[0], c1[0], c2[0], c3[0], c0[1], c1[1], c2[1], c3[1], ....
 * c0[1023], c1[1023], c2[1023], c3[1023]
 *
 * The first format needs c0[0], c1[0] (which are 1024 words apart)
 * to program gdc LUT registers. This makes it difficult to do piecemeal
 * reads in SP side gdc_lut_store
 *
 * Interleaved format allows use of contiguous bytes to store into
 * gdc LUT registers.
 *
 * See gdc_lut_store() definition in host/gdc.c vs sp/gdc_private.h
 *
 */
void gdc_lut_convert_to_isp_format(const int in_lut[4][HRT_GDC_N],
	int out_lut[4][HRT_GDC_N])
{
	unsigned int i;
	int *out = (int *)out_lut;

	for (i = 0; i < HRT_GDC_N; i++) {
		out[0] = in_lut[0][i];
		out[1] = in_lut[1][i];
		out[2] = in_lut[2][i];
		out[3] = in_lut[3][i];
		out += 4;
	}
}

int gdc_get_unity(
	const gdc_ID_t		ID)
{
	assert(ID < N_GDC_ID);
	(void)ID;
return (int)(1UL << HRT_GDC_FRAC_BITS);
}


/*
 * Local function implementations
 */
STORAGE_CLASS_INLINE void gdc_reg_store(
	const gdc_ID_t		ID,
	const unsigned int	reg,
	const hrt_data		value)
{
	ia_css_device_store_uint32(GDC_BASE[ID] + reg*sizeof(hrt_data), value);
return;
}

STORAGE_CLASS_INLINE hrt_data gdc_reg_load(
	const gdc_ID_t		ID,
	const unsigned int	reg)
{
return ia_css_device_load_uint32(GDC_BASE[ID] + reg*sizeof(hrt_data));
}
