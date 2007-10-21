/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __TDA9887_H__
#define __TDA9887_H__

#include "tuner-driver.h"

/* ------------------------------------------------------------------------ */
#if defined(CONFIG_TUNER_TDA9887) || (defined(CONFIG_TUNER_TDA9887_MODULE) && defined(MODULE))
extern int tda9887_attach(struct tuner *t);
#else
static inline int tda9887_attach(struct tuner *t)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return -EINVAL;
}
#endif

#endif /* __TDA9887_H__ */
