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

/* version.h */

/*  Common version/release info needed by all components goes here.
 *  (This file must compile cleanly in all environments.)
 *  Ultimately, this will be combined with defines generated dynamically as
 *  part of the sysgen, and some of the defines below may in fact end up
 *  being replaced with dynamically generated ones.
 */
#ifndef __VERSION_H__
#define __VERSION_H__

#define SPARVER1 "1"
#define SPARVER2 "0"
#define SPARVER3 "0"
#define SPARVER4 "0"

#define  VERSION        SPARVER1 "." SPARVER2 "." SPARVER3 "." SPARVER4

/* Here are various version forms needed in Windows environments.
 */
#define VISOR_PRODUCTVERSION      SPARVERCOMMA
#define VISOR_PRODUCTVERSION_STR  SPARVER1 "." SPARVER2 "." SPARVER3 "." \
	SPARVER4
#define VISOR_OBJECTVERSION_STR   SPARVER1 "," SPARVER2 "," SPARVER3 "," \
	SPARVER4

#define  COPYRIGHT      "Unisys Corporation"
#define  COPYRIGHTDATE  "2010 - 2013"

#endif
