/*
 * kvm_lib.c: Compile some libraries for kvm-intel module.
 *
 *	Just include kernel's library, and disable symbols export.
 * 	Copyright (C) 2008, Intel Corporation.
 *  	Xiantao Zhang  (xiantao.zhang@intel.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#undef CONFIG_MODULES
#include "../../../lib/vsprintf.c"
#include "../../../lib/ctype.c"
