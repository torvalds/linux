#if USE_DEBUGGER
/*
 * kmp_debugger.h -- debugger support.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_DEBUGGER_H
#define KMP_DEBUGGER_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* This external variable can be set by any debugger to flag to the runtime
   that we are currently executing inside a debugger.  This will allow the
   debugger to override the number of threads spawned in a parallel region by
   using __kmp_omp_num_threads() (below).
   * When __kmp_debugging is TRUE, each team and each task gets a unique integer
   identifier that can be used by debugger to conveniently identify teams and
   tasks.
   * The debugger has access to __kmp_omp_debug_struct_info which contains
   information about the OpenMP library's important internal structures.  This
   access will allow the debugger to read detailed information from the typical
   OpenMP constructs (teams, threads, tasking, etc. ) during a debugging
   session and offer detailed and useful information which the user can probe
   about the OpenMP portion of their code. */
extern int __kmp_debugging; /* Boolean whether currently debugging OpenMP RTL */
// Return number of threads specified by the debugger for given parallel region.
/* The ident field, which represents a source file location, is used to check if
   the debugger has changed the number of threads for the parallel region at
   source file location ident.  This way, specific parallel regions' number of
   threads can be changed at the debugger's request. */
int __kmp_omp_num_threads(ident_t const *ident);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // KMP_DEBUGGER_H

#endif // USE_DEBUGGER
