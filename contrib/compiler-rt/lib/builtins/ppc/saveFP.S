//===-- saveFP.S - Implement saveFP ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// Helper function used by compiler to save ppc floating point registers in
// function prologs.  This routines also saves r0 in the LR slot.
// If the compiler wants to save f27..f31, it does a "bl saveFP+52"
//
// This function should never be exported by a shared library.  Each linkage
// unit carries its own copy of this function.
//
DEFINE_COMPILERRT_PRIVATE_FUNCTION_UNMANGLED(saveFP)
	stfd    f14,-144(r1)
        stfd    f15,-136(r1)
        stfd    f16,-128(r1)
        stfd    f17,-120(r1)
        stfd    f18,-112(r1)
        stfd    f19,-104(r1)
        stfd    f20,-96(r1)
        stfd    f21,-88(r1)
        stfd    f22,-80(r1)
        stfd    f23,-72(r1)
        stfd    f24,-64(r1)
        stfd    f25,-56(r1)
        stfd    f26,-48(r1)
        stfd    f27,-40(r1)
        stfd    f28,-32(r1)
        stfd    f29,-24(r1)
        stfd    f30,-16(r1)
        stfd    f31,-8(r1)
        stw      r0,8(r1)
        blr

NO_EXEC_STACK_DIRECTIVE

