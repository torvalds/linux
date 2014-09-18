/* sparstop.h
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

#ifndef __SPARSTOP_H__
#define __SPARSTOP_H__

#include "timskmod.h"
#include "version.h"
#include <linux/ctype.h>

typedef void (*SPARSTOP_COMPLETE_FUNC) (void *context, int status);

int sp_stop(void *context, SPARSTOP_COMPLETE_FUNC get_complete_func);
void test_remove_stop_device(void);

#endif
