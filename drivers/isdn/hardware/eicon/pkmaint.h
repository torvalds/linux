
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __DIVA_XDI_OS_DEPENDENT_PACK_MAIN_ON_BYTE_INC__
#define __DIVA_XDI_OS_DEPENDENT_PACK_MAIN_ON_BYTE_INC__


/*
	Only one purpose of this compiler dependent file to pack
	structures, described in pc_maint.h so that no padding
	will be included.

	With microsoft compile it is done by "pshpack1.h" and
	after is restored by "poppack.h"
	*/


#include "pc_maint.h"


#endif

