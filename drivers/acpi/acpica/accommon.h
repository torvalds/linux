/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: accommon.h - Common include files for generation of ACPICA source
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACCOMMON_H__
#define __ACCOMMON_H__

/*
 * Common set of includes for all ACPICA source files.
 * We put them here because we don't want to duplicate them
 * in the source code again and again.
 *
 * Note: The order of these include files is important.
 */
#include <acpi/acconfig.h>	/* Global configuration constants */
#include "acmacros.h"		/* C macros */
#include "aclocal.h"		/* Internal data types */
#include "acobject.h"		/* ACPI internal object */
#include "acstruct.h"		/* Common structures */
#include "acglobal.h"		/* All global variables */
#include "achware.h"		/* Hardware defines and interfaces */
#include "acutils.h"		/* Utility interfaces */
#ifndef ACPI_USE_SYSTEM_CLIBRARY
#include "acclib.h"		/* C library interfaces */
#endif				/* !ACPI_USE_SYSTEM_CLIBRARY */

#endif				/* __ACCOMMON_H__ */
