/*
 * comedi_fc.h
 * This is a place for code driver writers wish to share between
 * two or more drivers. These functions are meant to be used only
 * by drivers, they are NOT part of the kcomedilib API!
 *
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2002 Frank Mori Hess
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _COMEDI_FC_H
#define _COMEDI_FC_H

#include "../comedidev.h"

/*
 * This file will be removed once drivers have migrated to using the
 * replacement functions in "comedidev.h".
 */

static inline int cfc_check_trigger_src(unsigned int *src, unsigned int flags)
{
	return comedi_check_trigger_src(src, flags);
}

static inline int cfc_check_trigger_is_unique(unsigned int src)
{
	return comedi_check_trigger_is_unique(src);
}

static inline int cfc_check_trigger_arg_is(unsigned int *arg, unsigned int val)
{
	return comedi_check_trigger_arg_is(arg, val);
}

static inline int cfc_check_trigger_arg_min(unsigned int *arg,
					    unsigned int val)
{
	return comedi_check_trigger_arg_min(arg, val);
}

static inline int cfc_check_trigger_arg_max(unsigned int *arg,
					    unsigned int val)
{
	return comedi_check_trigger_arg_max(arg, val);
}

#endif /* _COMEDI_FC_H */
