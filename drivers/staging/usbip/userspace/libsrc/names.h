/*****************************************************************************/

/*
 *      names.h  --  USB name database manipulation routines
 *
 *      Copyright (C) 1999, 2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*
 *	Copyright (C) 2005 Takahiro Hirofuchi
 *	       - names_free() is added.
 */

/*****************************************************************************/

#ifndef _NAMES_H
#define _NAMES_H

#include <sys/types.h>

/* ---------------------------------------------------------------------- */

extern const char *names_vendor(u_int16_t vendorid);
extern const char *names_product(u_int16_t vendorid, u_int16_t productid);
extern const char *names_class(u_int8_t classid);
extern const char *names_subclass(u_int8_t classid, u_int8_t subclassid);
extern const char *names_protocol(u_int8_t classid, u_int8_t subclassid, u_int8_t protocolid);
extern const char *names_audioterminal(u_int16_t termt);
extern const char *names_hid(u_int8_t hidd);
extern const char *names_reporttag(u_int8_t rt);
extern const char *names_huts(unsigned int data);
extern const char *names_hutus(unsigned int data);
extern const char *names_langid(u_int16_t langid);
extern const char *names_physdes(u_int8_t ph);
extern const char *names_bias(u_int8_t b);
extern const char *names_countrycode(unsigned int countrycode);
extern int  names_init(char *n);
extern void names_free(void);

/* ---------------------------------------------------------------------- */
#endif /* _NAMES_H */
