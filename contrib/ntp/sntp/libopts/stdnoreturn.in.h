/* A substitute for ISO C11 <stdnoreturn.h>.

   Copyright 2012-2015 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

#ifndef noreturn

/* ISO C11 <stdnoreturn.h> for platforms that lack it.

   References:
   ISO C11 (latest free draft
   <http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>)
   section 7.23 */

/* The definition of _Noreturn is copied here.  */

#if 1200 <= _MSC_VER
/* Standard include files on this platform contain declarations like
   "__declspec (noreturn) void abort (void);".  "#define noreturn
   _Noreturn" would cause this declaration to be rewritten to the
   invalid "__declspec (__declspec (noreturn)) void abort (void);".
   Instead, define noreturn to empty, so that such declarations are
   rewritten to "__declspec () void abort (void);", which is
   equivalent to "void abort (void);"; this gives up on noreturn's
   advice to the compiler but at least it is valid code.  */
# define noreturn /*empty*/
#else
# define noreturn _Noreturn
#endif

/* Did he ever return?
   No he never returned
   And his fate is still unlearn'd ...
     -- Steiner J, Hawes BL.  M.T.A. (1949)  */

#endif /* noreturn */
