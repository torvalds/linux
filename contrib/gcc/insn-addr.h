/* Macros to support INSN_ADDRESSES
   Copyright (C) 2000 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_INSN_ADDR_H
#define GCC_INSN_ADDR_H 

#include "varray.h"

extern GTY(()) varray_type insn_addresses_;
extern int insn_current_address;

#define INSN_ADDRESSES_DEFN() varray_type insn_addresses_
#define INSN_ADDRESSES(id) VARRAY_INT (insn_addresses_, (id))
#define INSN_ADDRESSES_ALLOC(size) \
  VARRAY_INT_INIT (insn_addresses_, (size), "insn_addresses")
#define INSN_ADDRESSES_FREE() (insn_addresses_ = 0)
#define INSN_ADDRESSES_SET_P() (insn_addresses_ != 0)
#define INSN_ADDRESSES_SIZE() VARRAY_SIZE (insn_addresses_)
#define INSN_ADDRESSES_NEW(insn, addr) do \
  {									\
    unsigned insn_uid__ = INSN_UID ((insn));				\
    int insn_addr__ = (addr);						\
									\
    if (INSN_ADDRESSES_SET_P ())					\
      {									\
	if (INSN_ADDRESSES_SIZE () <= insn_uid__)			\
	  VARRAY_GROW (insn_addresses_, insn_uid__ + 1);		\
	INSN_ADDRESSES (insn_uid__) = insn_addr__;			\
      }									\
  }									\
while (0)

#endif /* ! GCC_INSN_ADDR_H */
