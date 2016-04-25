/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef _DIAG_CHANNEL_H_
#define _DIAG_CHANNEL_H_

/* Levels of severity for diagnostic events, in order from lowest severity to
 * highest (i.e. fatal errors are the most severe, and should always be logged,
 * but info events rarely need to be logged except during debugging). The
 * values DIAG_SEVERITY_ENUM_BEGIN and DIAG_SEVERITY_ENUM_END are not valid
 * severity values.  They exist merely to dilineate the list, so that future
 * additions won't require changes to the driver (i.e. when checking for
 * out-of-range severities in SetSeverity). The values DIAG_SEVERITY_OVERRIDE
 * and DIAG_SEVERITY_SHUTOFF are not valid severity values for logging events
 * but they are valid for controlling the amount of event data. Changes made
 * to the enum, need to be reflected in s-Par.
 */
enum diag_severity {
		DIAG_SEVERITY_VERBOSE = 0,
		DIAG_SEVERITY_INFO = 1,
		DIAG_SEVERITY_WARNING = 2,
		DIAG_SEVERITY_ERR = 3,
		DIAG_SEVERITY_PRINT = 4,
};

#endif
