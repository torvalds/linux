// WebAssemblyInstrExceptRef.td-WebAssembly except_ref codegen --*- tablegen -*-
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly except_ref operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

defm SELECT_EXCEPT_REF : I<(outs EXCEPT_REF:$dst),
                           (ins EXCEPT_REF:$lhs, EXCEPT_REF:$rhs, I32:$cond),
                           (outs), (ins),
                           [(set EXCEPT_REF:$dst,
                            (select I32:$cond, EXCEPT_REF:$lhs,
                             EXCEPT_REF:$rhs))],
                           "except_ref.select\t$dst, $lhs, $rhs, $cond",
                           "except_ref.select", 0x1b>;

def : Pat<(select (i32 (setne I32:$cond, 0)), EXCEPT_REF:$lhs, EXCEPT_REF:$rhs),
          (SELECT_EXCEPT_REF EXCEPT_REF:$lhs, EXCEPT_REF:$rhs, I32:$cond)>;
def : Pat<(select (i32 (seteq I32:$cond, 0)), EXCEPT_REF:$lhs, EXCEPT_REF:$rhs),
          (SELECT_EXCEPT_REF EXCEPT_REF:$rhs, EXCEPT_REF:$lhs, I32:$cond)>;
