/*
 * kmp_import.cpp
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

/* Object generated from this source file is linked to Windows* OS DLL import
   library (libompmd.lib) only! It is not a part of regular static or dynamic
   OpenMP RTL. Any code that just needs to go in the libompmd.lib (but not in
   libompmt.lib and libompmd.dll) should be placed in this file. */

#ifdef __cplusplus
extern "C" {
#endif

/*These symbols are required for mutual exclusion with Microsoft OpenMP RTL
  (and compatibility with MS Compiler). */

int _You_must_link_with_exactly_one_OpenMP_library = 1;
int _You_must_link_with_Intel_OpenMP_library = 1;
int _You_must_link_with_Microsoft_OpenMP_library = 1;

#ifdef __cplusplus
}
#endif

// end of file //
