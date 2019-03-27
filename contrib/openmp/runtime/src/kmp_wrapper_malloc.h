/*
 * kmp_wrapper_malloc.h -- Wrappers for memory allocation routines
 *                         (malloc(), free(), and others).
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_WRAPPER_MALLOC_H
#define KMP_WRAPPER_MALLOC_H

/* This header serves for 3 purposes:
   1. Declaring standard memory allocation rourines in OS-independent way.
   2. Passing source location info through memory allocation wrappers.
   3. Enabling native memory debugging capabilities.

   1. Declaring standard memory allocation rourines in OS-independent way.
   -----------------------------------------------------------------------
   On Linux* OS, alloca() function is declared in <alloca.h> header, while on
   Windows* OS there is no <alloca.h> header, function _alloca() (note
   underscore!) is declared in <malloc.h>. This header eliminates these
   differences, so client code incluiding "kmp_wrapper_malloc.h" can rely on
   following routines:

        malloc
        calloc
        realloc
        free
        alloca

   in OS-independent way. It also enables memory tracking capabilities in debug
   build. (Currently it is available only on Windows* OS.)

   2. Passing source location info through memory allocation wrappers.
   -------------------------------------------------------------------
   Some tools may help debugging memory errors, for example, report memory
   leaks. However, memory allocation wrappers may hinder source location.
   For example:

   void * aligned_malloc( int size ) {
     void * ptr = malloc( size ); // All the memory leaks will be reported at
                                  // this line.
     // some adjustments...
     return ptr;
   };

   ptr = aligned_malloc( size ); // Memory leak will *not* be detected here. :-(

   To overcome the problem, information about original source location should
   be passed through all the memory allocation wrappers, for example:

   void * aligned_malloc( int size, char const * file, int line ) {
     void * ptr = _malloc_dbg( size, file, line );
     // some adjustments...
     return ptr;
   };
   void * ptr = aligned_malloc( size, __FILE__, __LINE__ );

   This is a good idea for debug, but passing additional arguments impacts
   performance. Disabling extra arguments in release version of the software
   introduces too many conditional compilation, which makes code unreadable.
   This header defines few macros and functions facilitating it:

   void * _aligned_malloc( int size KMP_SRC_LOC_DECL ) {
     void * ptr = malloc_src_loc( size KMP_SRC_LOC_PARM );
     // some adjustments...
     return ptr;
   };
   #define aligned_malloc( size ) _aligned_malloc( (size) KMP_SRC_LOC_CURR )
   // Use macro instead of direct call to function.

   void * ptr = aligned_malloc( size );  // Bingo! Memory leak will be
                                         // reported at this line.

   3. Enabling native memory debugging capabilities.
   -------------------------------------------------
   Some platforms may offer memory debugging capabilities. For example, debug
   version of Microsoft RTL tracks all memory allocations and can report memory
   leaks. This header enables this, and makes report more useful (see "Passing
   source location info through memory allocation wrappers").
*/

#include <stdlib.h>

#include "kmp_os.h"

// Include alloca() declaration.
#if KMP_OS_WINDOWS
#include <malloc.h> // Windows* OS: _alloca() declared in "malloc.h".
#if KMP_MSVC_COMPAT
#define alloca _alloca // Allow to use alloca() with no underscore.
#endif
#elif KMP_OS_DRAGONFLY || KMP_OS_FREEBSD || KMP_OS_NETBSD || KMP_OS_OPENBSD
// Declared in "stdlib.h".
#elif KMP_OS_UNIX
#include <alloca.h> // Linux* OS and OS X*: alloc() declared in "alloca".
#else
#error Unknown or unsupported OS.
#endif

/* KMP_SRC_LOC_DECL -- Declaring source location paramemters, to be used in
   function declaration.
   KMP_SRC_LOC_PARM -- Source location paramemters, to be used to pass
   parameters to underlying levels.
   KMP_SRC_LOC_CURR -- Source location arguments describing current location,
   to be used at top-level.

   Typical usage:
   void * _aligned_malloc( int size KMP_SRC_LOC_DECL ) {
     // Note: Comma is missed before KMP_SRC_LOC_DECL.
     KE_TRACE( 25, ( "called from %s:%d\n", KMP_SRC_LOC_PARM ) );
     ...
   }
   #define aligned_malloc( size ) _aligned_malloc( (size) KMP_SRC_LOC_CURR )
   // Use macro instead of direct call to function -- macro passes info
   // about current source location to the func.
*/
#if KMP_DEBUG
#define KMP_SRC_LOC_DECL , char const *_file_, int _line_
#define KMP_SRC_LOC_PARM , _file_, _line_
#define KMP_SRC_LOC_CURR , __FILE__, __LINE__
#else
#define KMP_SRC_LOC_DECL
#define KMP_SRC_LOC_PARM
#define KMP_SRC_LOC_CURR
#endif // KMP_DEBUG

/* malloc_src_loc() and free_src_loc() are pseudo-functions (really macros)
   with accepts extra arguments (source location info) in debug mode. They
   should be used in place of malloc() and free(), this allows enabling native
   memory debugging capabilities (if any).

   Typical usage:
   ptr = malloc_src_loc( size KMP_SRC_LOC_PARM );
   // Inside memory allocation wrapper, or
   ptr = malloc_src_loc( size KMP_SRC_LOC_CURR );
   // Outside of memory allocation wrapper.
*/
#define malloc_src_loc(args) _malloc_src_loc(args)
#define free_src_loc(args) _free_src_loc(args)
/* Depending on build mode (debug or release), malloc_src_loc is declared with
   1 or 3 parameters, but calls to malloc_src_loc() are always the same:

   ... malloc_src_loc( size KMP_SRC_LOC_PARM ); // or KMP_SRC_LOC_CURR

   Compiler issues warning/error "too few arguments in macro invocation".
   Declaring two macros, malloc_src_loc() and _malloc_src_loc(), overcomes the
   problem. */

#if KMP_DEBUG

#if KMP_OS_WINDOWS && _DEBUG
// KMP_DEBUG != _DEBUG. MS debug RTL is available only if _DEBUG is defined.

// Windows* OS has native memory debugging capabilities. Enable them.

#include <crtdbg.h>

#define KMP_MEM_BLOCK _CLIENT_BLOCK
#define malloc(size) _malloc_dbg((size), KMP_MEM_BLOCK, __FILE__, __LINE__)
#define calloc(num, size)                                                      \
  _calloc_dbg((num), (size), KMP_MEM_BLOCK, __FILE__, __LINE__)
#define realloc(ptr, size)                                                     \
  _realloc_dbg((ptr), (size), KMP_MEM_BLOCK, __FILE__, __LINE__)
#define free(ptr) _free_dbg((ptr), KMP_MEM_BLOCK)

#define _malloc_src_loc(size, file, line)                                      \
  _malloc_dbg((size), KMP_MEM_BLOCK, (file), (line))
#define _free_src_loc(ptr, file, line) _free_dbg((ptr), KMP_MEM_BLOCK)

#else

// Linux* OS, OS X*, or non-debug Windows* OS.

#define _malloc_src_loc(size, file, line) malloc((size))
#define _free_src_loc(ptr, file, line) free((ptr))

#endif

#else

// In release build malloc_src_loc() and free_src_loc() do not have extra
// parameters.
#define _malloc_src_loc(size) malloc((size))
#define _free_src_loc(ptr) free((ptr))

#endif // KMP_DEBUG

#endif // KMP_WRAPPER_MALLOC_H

// end of file //
