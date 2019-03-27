/*===- InstrProfilingNameVar.c - profile name variable setup  -------------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#include "InstrProfiling.h"

/* char __llvm_profile_filename[1]
 *
 * The runtime should only provide its own definition of this symbol when the
 * user has not specified one. Set this up by moving the runtime's copy of this
 * symbol to an object file within the archive.
 */
COMPILER_RT_WEAK char INSTR_PROF_PROFILE_NAME_VAR[1] = {0};
