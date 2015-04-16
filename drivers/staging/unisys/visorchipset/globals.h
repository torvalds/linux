/* globals.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
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

#ifndef __VISORCHIPSET_GLOBALS_H__
#define __VISORCHIPSET_GLOBALS_H__

#include "diagnostics/appos_subsystems.h"
#include "timskmod.h"
#include "visorchipset.h"
#include "visorchipset_umode.h"
#include "version.h"

#define MYDRVNAME "visorchipset"

/* module parameters */

extern int visorchipset_testvnic;
extern int visorchipset_testvnicclient;
extern int visorchipset_testmsg;
extern int visorchipset_major;
extern int visorchipset_serverregwait;
extern int visorchipset_clientregwait;
extern int visorchipset_testteardown;
extern int visorchipset_disable_controlvm;
extern int visorchipset_crash_kernel;
extern int visorchipset_holdchipsetready;

#endif
