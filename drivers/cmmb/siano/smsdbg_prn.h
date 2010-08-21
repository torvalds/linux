/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

 This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************/

#ifndef _SMS_DBG_H_
#define _SMS_DBG_H_

#include <linux/kernel.h>
#include <linux/module.h>

/************************************************************************/
/* Debug Zones definitions.                                             */
/************************************************************************/
#undef PERROR
#  define PERROR(fmt, args...) \
	printk(KERN_ERR "spibus error: line %d- %s(): " fmt, __LINE__,\
	  __func__, ## args)
#undef PWARNING
#  define PWARNING(fmt, args...) \
	printk(KERN_WARNING "spibus warning: line %d- %s(): " fmt, __LINE__,  \
	__func__, ## args)

/* the debug macro - conditional compilation from the makefile */
// to enable log


//ROCK Enbale Interruption
//#define SPIBUS_DEBUG 1

#undef PDEBUG			/* undef it, just in case */
#ifdef SPIBUS_DEBUG

#define PDEBUG(fmt, args...) \
	printk(KERN_DEBUG " " fmt,## args)

#else
#  define PDEBUG(fmt, args...)	/* not debugging: nothing */
#endif

/* The following defines are used for printing and
are mandatory for compilation. */
#define TXT(str) str
#define PRN_DBG(str) PDEBUG str
#define PRN_ERR(str) PERROR str

#endif /*_SMS_DBG_H_*/
