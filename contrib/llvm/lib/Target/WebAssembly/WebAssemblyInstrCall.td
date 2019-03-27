//===- WebAssemblyInstrCall.td-WebAssembly Call codegen support -*- tablegen -*-
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly Call operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

// TODO: addr64: These currently assume the callee address is 32-bit.
// FIXME: add $type to first call_indirect asmstr (and maybe $flags)

// Call sequence markers. These have an immediate which represents the amount of
// stack space to allocate or free, which is used for varargs lowering.
let Uses = [SP32, SP64], Defs = [SP32, SP64], isCodeGenOnly = 1 in {
defm ADJCALLSTACKDOWN : NRI<(outs), (ins i32imm:$amt, i32imm:$amt2),
                            [(WebAssemblycallseq_start timm:$amt, timm:$amt2)]>;
defm ADJCALLSTACKUP : NRI<(outs), (ins i32imm:$amt, i32imm:$amt2),
                          [(WebAssemblycallseq_end timm:$amt, timm:$amt2)]>;
} // isCodeGenOnly = 1

multiclass CALL<WebAssemblyRegClass vt, string prefix> {
  defm CALL_#vt : I<(outs vt:$dst), (ins function32_op:$callee, variable_ops),
                    (outs), (ins function32_op:$callee),
                    [(set vt:$dst, (WebAssemblycall1 (i32 imm:$callee)))],
                    !strconcat(prefix, "call\t$dst, $callee"),
                    !strconcat(prefix, "call\t$callee"),
                    0x10>;

  let isCodeGenOnly = 1 in {
    defm PCALL_INDIRECT_#vt : I<(outs vt:$dst), (ins I32:$callee, variable_ops),
                                (outs), (ins I32:$callee),
                               [(set vt:$dst, (WebAssemblycall1 I32:$callee))],
                               "PSEUDO CALL INDIRECT\t$callee",
                               "PSEUDO CALL INDIRECT\t$callee">;
  } // isCodeGenOnly = 1

  defm CALL_INDIRECT_#vt : I<(outs vt:$dst),
                             (ins TypeIndex:$type, i32imm:$flags, variable_ops),
                             (outs), (ins TypeIndex:$type, i32imm:$flags),
                             [],
                             !strconcat(prefix, "call_indirect\t$dst"),
                             !strconcat(prefix, "call_indirect\t$type"),
                             0x11>;
}

multiclass SIMD_CALL<ValueType vt, string prefix> {

  defm CALL_#vt : I<(outs V128:$dst), (ins function32_op:$callee, variable_ops),
                    (outs), (ins function32_op:$callee),
                    [(set (vt V128:$dst),
                      (WebAssemblycall1 (i32 imm:$callee)))],
                    !strconcat(prefix, "call\t$dst, $callee"),
                    !strconcat(prefix, "call\t$callee"),
                    0x10>,
                  Requires<[HasSIMD128]>;

  let isCodeGenOnly = 1 in {
    defm PCALL_INDIRECT_#vt : I<(outs V128:$dst),
                                (ins I32:$callee, variable_ops),
                                (outs), (ins I32:$callee),
                                [(set (vt V128:$dst),
                                      (WebAssemblycall1 I32:$callee))],
                                "PSEUDO CALL INDIRECT\t$callee",
                                "PSEUDO CALL INDIRECT\t$callee">,
                              Requires<[HasSIMD128]>;
  } // isCodeGenOnly = 1

  defm CALL_INDIRECT_#vt : I<(outs V128:$dst),
                             (ins TypeIndex:$type, i32imm:$flags, variable_ops),
                             (outs), (ins TypeIndex:$type, i32imm:$flags),
                             [],
                             !strconcat(prefix, "call_indirect\t$dst"),
                             !strconcat(prefix, "call_indirect\t$type"),
                             0x11>,
                           Requires<[HasSIMD128]>;
}

let Uses = [SP32, SP64], isCall = 1 in {
  defm "" : CALL<I32, "i32.">;
  defm "" : CALL<I64, "i64.">;
  defm "" : CALL<F32, "f32.">;
  defm "" : CALL<F64, "f64.">;
  defm "" : CALL<EXCEPT_REF, "except_ref.">;
  defm "" : SIMD_CALL<v16i8, "v128.">;
  defm "" : SIMD_CALL<v8i16, "v128.">;
  defm "" : SIMD_CALL<v4i32, "v128.">;
  defm "" : SIMD_CALL<v2i64, "v128.">;
  defm "" : SIMD_CALL<v4f32, "v128.">;
  defm "" : SIMD_CALL<v2f64, "v128.">;

  defm CALL_VOID : I<(outs), (ins function32_op:$callee, variable_ops),
                     (outs), (ins function32_op:$callee),
                     [(WebAssemblycall0 (i32 imm:$callee))],
                     "call    \t$callee", "call\t$callee", 0x10>;

  let isCodeGenOnly = 1 in {
    defm PCALL_INDIRECT_VOID : I<(outs), (ins I32:$callee, variable_ops),
                                 (outs), (ins I32:$callee),
                                 [(WebAssemblycall0 I32:$callee)],
                                 "PSEUDO CALL INDIRECT\t$callee",
                                 "PSEUDO CALL INDIRECT\t$callee">;
  } // isCodeGenOnly = 1

  defm CALL_INDIRECT_VOID : I<(outs),
                              (ins TypeIndex:$type, i32imm:$flags,
                                variable_ops),
                              (outs), (ins TypeIndex:$type, i32imm:$flags),
                              [],
                              "call_indirect\t", "call_indirect\t$type",
                              0x11>;
} // Uses = [SP32,SP64], isCall = 1

// Patterns for matching a direct call to a global address.
def : Pat<(i32 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_I32 tglobaladdr:$callee)>;
def : Pat<(i64 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_I64 tglobaladdr:$callee)>;
def : Pat<(f32 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_F32 tglobaladdr:$callee)>;
def : Pat<(f64 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_F64 tglobaladdr:$callee)>;
def : Pat<(v16i8 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_v16i8 tglobaladdr:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v8i16 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_v8i16 tglobaladdr:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v4i32 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_v4i32 tglobaladdr:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v2i64 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_v2i64 tglobaladdr:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v4f32 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_v4f32 tglobaladdr:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v2f64 (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_v2f64 tglobaladdr:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(ExceptRef
           (WebAssemblycall1 (WebAssemblywrapper tglobaladdr:$callee))),
          (CALL_EXCEPT_REF tglobaladdr:$callee)>;
def : Pat<(WebAssemblycall0 (WebAssemblywrapper tglobaladdr:$callee)),
          (CALL_VOID tglobaladdr:$callee)>;

// Patterns for matching a direct call to an external symbol.
def : Pat<(i32 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_I32 texternalsym:$callee)>;
def : Pat<(i64 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_I64 texternalsym:$callee)>;
def : Pat<(f32 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_F32 texternalsym:$callee)>;
def : Pat<(f64 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_F64 texternalsym:$callee)>;
def : Pat<(v16i8 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_v16i8 texternalsym:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v8i16 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_v8i16 texternalsym:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v4i32 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_v4i32 texternalsym:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v2i64 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_v2i64 texternalsym:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v4f32 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_v4f32 texternalsym:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(v2f64 (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_v2f64 texternalsym:$callee)>, Requires<[HasSIMD128]>;
def : Pat<(ExceptRef
           (WebAssemblycall1 (WebAssemblywrapper texternalsym:$callee))),
          (CALL_EXCEPT_REF texternalsym:$callee)>;
def : Pat<(WebAssemblycall0 (WebAssemblywrapper texternalsym:$callee)),
          (CALL_VOID texternalsym:$callee)>;
