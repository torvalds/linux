/* Native-dependent code for SPARC.

   Copyright 2003 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef SPARC_NAT_H
#define SPARC_NAT_H 1

struct sparc_gregset;

extern const struct sparc_gregset *sparc_gregset;
extern void (*sparc_supply_gregset) (const struct sparc_gregset *,
				     struct regcache *, int , const void *);
extern void (*sparc_collect_gregset) (const struct sparc_gregset *,
				      const struct regcache *, int, void *);
extern void (*sparc_supply_fpregset) (struct regcache *, int , const void *);
extern void (*sparc_collect_fpregset) (const struct regcache *, int , void *);
extern int (*sparc_gregset_supplies_p) (int);
extern int (*sparc_fpregset_supplies_p) (int);

extern int sparc32_gregset_supplies_p (int regnum);
extern int sparc32_fpregset_supplies_p (int regnum);

#endif /* sparc-nat.h */
