// Verbose terminate_handler -*- C++ -*-

// Copyright (C) 2001, 2002, 2004, 2005 Free Software Foundation
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <bits/c++config.h>

#if _GLIBCXX_HOSTED
#include <cstdlib>
#include <exception>
#include <exception_defines.h>
#include <cxxabi.h>
# include <cstdio>

using namespace std;
using namespace abi;

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  // A replacement for the standard terminate_handler which prints
  // more information about the terminating exception (if any) on
  // stderr.
  void __verbose_terminate_handler()
  {
    static bool terminating;
    if (terminating)
      {
	fputs("terminate called recursively\n", stderr);
	abort ();
      }
    terminating = true;

    // Make sure there was an exception; terminate is also called for an
    // attempt to rethrow when there is no suitable exception.
    type_info *t = __cxa_current_exception_type();
    if (t)
      {
	// Note that "name" is the mangled name.
	char const *name = t->name();
	{
	  int status = -1;
	  char *dem = 0;
	  
	  dem = __cxa_demangle(name, 0, 0, &status);

	  fputs("terminate called after throwing an instance of '", stderr);
	  if (status == 0)
	    fputs(dem, stderr);
	  else
	    fputs(name, stderr);
	  fputs("'\n", stderr);

	  if (status == 0)
	    free(dem);
	}

	// If the exception is derived from std::exception, we can
	// give more information.
	try { __throw_exception_again; }
#ifdef __EXCEPTIONS
	catch (exception &exc)
	  {
	    char const *w = exc.what();
	    fputs("  what():  ", stderr);
	    fputs(w, stderr);
	    fputs("\n", stderr);
          }
#endif
	catch (...) { }
      }
    else
      fputs("terminate called without an active exception\n", stderr);
    
    abort();
  }

_GLIBCXX_END_NAMESPACE

#endif
