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

/*  ----------------------------------- This */
#include <dspbridge/uuidutil.h>

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

	uuid_obj->data1 = uuid_hex_to_bin(sz_uuid, 8);
	sz_uuid += 8;

	/* Step over underscore */
	sz_uuid++;

	uuid_obj->data2 = (u16) uuid_hex_to_bin(sz_uuid, 4);
	sz_uuid += 4;

	/* Step over underscore */
	sz_uuid++;

	uuid_obj->data3 = (u16) uuid_hex_to_bin(sz_uuid, 4);
	sz_uuid += 4;

	/* Step over underscore */
	sz_uuid++;

	uuid_obj->data4 = (u8) uuid_hex_to_bin(sz_uuid, 2);
	sz_uuid += 2;

	uuid_obj->data5 = (u8) uuid_hex_to_bin(sz_uuid, 2);
	sz_uuid += 2;

	/* Step over underscore */
	sz_uuid++;

	for (j = 0; j < 6; j++) {
		uuid_obj->data6[j] = (u8) uuid_hex_to_bin(sz_uuid, 2);
		sz_uuid += 2;
	}
}
