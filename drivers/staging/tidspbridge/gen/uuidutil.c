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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
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
void uuid_uuid_to_string(IN struct dsp_uuid *uuid_obj, OUT char *pszUuid,
			 IN s32 size)
{
	s32 i;			/* return result from snprintf. */

	DBC_REQUIRE(uuid_obj && pszUuid);

	i = snprintf(pszUuid, size,
		     "%.8X_%.4X_%.4X_%.2X%.2X_%.2X%.2X%.2X%.2X%.2X%.2X",
		     uuid_obj->ul_data1, uuid_obj->us_data2, uuid_obj->us_data3,
		     uuid_obj->uc_data4, uuid_obj->uc_data5,
		     uuid_obj->uc_data6[0], uuid_obj->uc_data6[1],
		     uuid_obj->uc_data6[2], uuid_obj->uc_data6[3],
		     uuid_obj->uc_data6[4], uuid_obj->uc_data6[5]);

	DBC_ENSURE(i != -1);
}

/*
 *  ======== htoi ========
 *  Purpose:
 *      Converts a hex value to a decimal integer.
 */

static int htoi(char c)
{
	switch (c) {
	case '0':
		return 0;
	case '1':
		return 1;
	case '2':
		return 2;
	case '3':
		return 3;
	case '4':
		return 4;
	case '5':
		return 5;
	case '6':
		return 6;
	case '7':
		return 7;
	case '8':
		return 8;
	case '9':
		return 9;
	case 'A':
		return 10;
	case 'B':
		return 11;
	case 'C':
		return 12;
	case 'D':
		return 13;
	case 'E':
		return 14;
	case 'F':
		return 15;
	case 'a':
		return 10;
	case 'b':
		return 11;
	case 'c':
		return 12;
	case 'd':
		return 13;
	case 'e':
		return 14;
	case 'f':
		return 15;
	}
	return 0;
}

/*
 *  ======== uuid_uuid_from_string ========
 *  Purpose:
 *      Converts a string to a struct dsp_uuid.
 */
void uuid_uuid_from_string(IN char *pszUuid, OUT struct dsp_uuid *uuid_obj)
{
	char c;
	s32 i, j;
	s32 result;
	char *temp = pszUuid;

	result = 0;
	for (i = 0; i < 8; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	uuid_obj->ul_data1 = result;

	/* Step over underscore */
	temp++;

	result = 0;
	for (i = 0; i < 4; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	uuid_obj->us_data2 = (u16) result;

	/* Step over underscore */
	temp++;

	result = 0;
	for (i = 0; i < 4; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	uuid_obj->us_data3 = (u16) result;

	/* Step over underscore */
	temp++;

	result = 0;
	for (i = 0; i < 2; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	uuid_obj->uc_data4 = (u8) result;

	result = 0;
	for (i = 0; i < 2; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	uuid_obj->uc_data5 = (u8) result;

	/* Step over underscore */
	temp++;

	for (j = 0; j < 6; j++) {
		result = 0;
		for (i = 0; i < 2; i++) {
			/* Get first character in string */
			c = *temp;

			/* Increase the results by new value */
			result *= 16;
			result += htoi(c);

			/* Go to next character in string */
			temp++;
		}
		uuid_obj->uc_data6[j] = (u8) result;
	}
}
