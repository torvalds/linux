// -*- C++ -*- std::terminate handler
// Copyright (C) 2002, 2003 Free Software Foundation
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING.  If not, write to
// the Free Software Foundation, 51 Franklin Street, Fifth Floor,
// Boston, MA 02110-1301, USA. 

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <bits/c++config.h>
#include "unwind-cxx.h"

/* We default to the talkative, informative handler in a normal hosted
   library.  This pulls in the demangler, the dyn-string utilities, and
   elements of the I/O library.  For a low-memory environment, you can return
   to the earlier "silent death" handler by including <cstdlib>, initializing
   to "std::abort", and rebuilding the library.  In a freestanding mode, we
   default to this latter approach.  */

#if ! _GLIBCXX_HOSTED
# include <cstdlib>
#endif

/* The current installed user handler.  */
std::terminate_handler __cxxabiv1::__terminate_handler =
#if _GLIBCXX_HOSTED
	__gnu_cxx::__verbose_terminate_handler;
#else
	std::abort;
#endif

