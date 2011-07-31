/*
 * uuidutil.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This file contains the implementation of UUID helper functions.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <linux/types.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- This */
#include <dspbridge/uuidutil.h>

/*
 *  ======== uuid_uuid_to_string ========
 *  Purpose:
 *      Converts a struct dsp_uuid to a string.
 *      Note: snprintf format specifier is:
 *      %[flags] [width] [.precision] [{h | l | I64 | L}]type
 */
void uuid_uuid_to_string(struct dsp_uuid *uuid_obj, char *sz_uuid,
			 s32 size)
{
	s32 i;			/* return result from snprintf. */

	DBC_REQUIRE(uuid_obj && sz_uuid);

	i = snprintf(sz_uuid, size,
		     "%.8X_%.4X_%.4X_%.2X%.2X_%.2X%.2X%.2X%.2X%.2X%.2X",
		     uuid_obj->ul_data1, uuid_obj->us_data2, uuid_obj->us_data3,
		     uuid_obj->uc_data4, uuid_obj->uc_data5,
		     uuid_obj->uc_data6[0], uuid_obj->uc_data6[1],
		     uuid_obj->uc_data6[2], uuid_obj->uc_data6[3],
		     uuid_obj->uc_data6[4], uuid_obj->uc_data6[5]);

	DBC_ENSURE(i != -1);
}

static s32 uuid_hex_to_bin(char *buf, s32 len)
{
	s32 i;
	s32 result = 0;
	int value;

	for (i = 0; i < len; i++) {
		value = hex_to_bin(*buf++);
		result *= 16;
		if (value > 0)
			result += value;
	}

	return result;
}

/*
 *  ======== uuid_uuid_from_string ========
 *  Purpose:
 *      Converts a string to a struct dsp_uuid.
 */
void uuid_uuid_from_string(char *sz_uuid, struct dsp_uuid *uuid_obj)
{
	s32 j;

	uuid_obj->ul_data1 = uuid_hex_to_bin(sz_uuid, 8);
	sz_uuid += 8;

	/* Step over underscore */
	sz_uuid++;

	uuid_obj->us_data2 = (u16) uuid_hex_to_bin(sz_uuid, 4);
	sz_uuid += 4;

	/* Step over underscore */
	sz_uuid++;

	uuid_obj->us_data3 = (u16) uuid_hex_to_bin(sz_uuid, 4);
	sz_uuid += 4;

	/* Step over underscore */
	sz_uuid++;

	uuid_obj->uc_data4 = (u8) uuid_hex_to_bin(sz_uuid, 2);
	sz_uuid += 2;

	uuid_obj->uc_data5 = (u8) uuid_hex_to_bin(sz_uuid, 2);
	sz_uuid += 2;

	/* Step over underscore */
	sz_uuid++;

	for (j = 0; j < 6; j++) {
		uuid_obj->uc_data6[j] = (u8) uuid_hex_to_bin(sz_uuid, 2);
		sz_uuid += 2;
	}
}
