/* Default definitions for progress macros.
   Copyright 1994 Free Software Foundation, Inc.

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* The default definitions below are intended to be replaced by real
   definitions, if building the tools for an interactive programming
   environment.  */

#ifndef _PROGRESS_H
#define _PROGRESS_H

#ifndef START_PROGRESS
#define START_PROGRESS(STR,N)
#endif

#ifndef PROGRESS
#define PROGRESS(X)
#endif

#ifndef END_PROGRESS
#define END_PROGRESS(STR)
#endif

#endif /* _PROGRESS_H */
