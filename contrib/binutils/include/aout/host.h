/* host.h - Parameters about the a.out format, based on the host system
   on which the program is compiled. 

   Copyright 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Address of data segment in memory after it is loaded.
   It is up to you to define SEGMENT_SIZE on machines not listed here.  */
#ifndef SEGMENT_SIZE

#if defined(hp300) || defined(pyr)
#define SEGMENT_SIZE page_size
#endif

#ifdef	sony
#define	SEGMENT_SIZE	0x1000
#endif	/* Sony.  */

#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif

#if defined(m68k) && defined(PORTAR)
#define TARGET_PAGE_SIZE 0x400
#define SEGMENT_SIZE TARGET_PAGE_SIZE
#endif

#endif /*!defined(SEGMENT_SIZE)*/

