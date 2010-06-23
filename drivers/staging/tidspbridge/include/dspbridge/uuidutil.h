/*
 * uuidutil.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This file contains the specification of UUID helper functions.
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

#ifndef UUIDUTIL_
#define UUIDUTIL_

#define MAXUUIDLEN  37

/*
 *  ======== uuid_uuid_to_string ========
 *  Purpose:
 *      Converts a dsp_uuid to an ANSI string.
 *  Parameters:
 *      uuid_obj:      Pointer to a dsp_uuid object.
 *      pszUuid:    Pointer to a buffer to receive a NULL-terminated UUID
 *                  string.
 *      size:	    Maximum size of the pszUuid string.
 *  Returns:
 *  Requires:
 *      uuid_obj & pszUuid are non-NULL values.
 *  Ensures:
 *      Lenghth of pszUuid is less than MAXUUIDLEN.
 *  Details:
 *      UUID string limit currently set at MAXUUIDLEN.
 */
void uuid_uuid_to_string(IN struct dsp_uuid *uuid_obj, OUT char *pszUuid,
			 s32 size);

/*
 *  ======== uuid_uuid_from_string ========
 *  Purpose:
 *      Converts an ANSI string to a dsp_uuid.
 *  Parameters:
 *      pszUuid:    Pointer to a string that represents a dsp_uuid object.
 *      uuid_obj:      Pointer to a dsp_uuid object.
 *  Returns:
 *  Requires:
 *      uuid_obj & pszUuid are non-NULL values.
 *  Ensures:
 *  Details:
 *      We assume the string representation of a UUID has the following format:
 *      "12345678_1234_1234_1234_123456789abc".
 */
extern void uuid_uuid_from_string(IN char *pszUuid,
				  OUT struct dsp_uuid *uuid_obj);

#endif /* UUIDUTIL_ */
