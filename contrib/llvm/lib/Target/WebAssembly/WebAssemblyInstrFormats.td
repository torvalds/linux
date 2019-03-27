//=- WebAssemblyInstrFormats.td - WebAssembly Instr. Formats -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly instruction format definitions.
///
//===----------------------------------------------------------------------===//

// WebAssembly Instruction Format.
// We instantiate 2 of these for every actual instruction (register based
// and stack based), see below.
class WebAssemblyInst<bits<32> inst, string asmstr, string stack> : StackRel,
  Instruction {
  bits<32> Inst = inst; // Instruction encoding.
  string StackBased = stack;
  string BaseName = NAME;
  let Namespace   = "WebAssembly";
  let Pattern     = [];
  let AsmString   = asmstr;
}

// Normal instructions. Default instantiation of a WebAssemblyInst.
class NI<dag oops, dag iops, list<dag> pattern, string stack,
         string asmstr = "", bits<32> inst = -1>
    : WebAssemblyInst<inst, asmstr, stack> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  let Defs           = [ARGUMENTS];
}

// Generates both register and stack based versions of one actual instruction.
// We have 2 sets of operands (oops & iops) for the register and stack
// based version of this instruction, as well as the corresponding asmstr.
// The register versions have virtual-register operands which correspond to wasm
// locals or stack locations. Each use and def of the register corresponds to an
// implicit local.get / local.set or access of stack operands in wasm. These
// instructions are used for ISel and all MI passes. The stack versions of the
// instructions do not have register operands (they implicitly operate on the
// stack), and local.gets and local.sets are explicit. The register instructions
// are converted to their corresponding stack instructions before lowering to
// MC.
// Every instruction should want to be based on this multi-class to guarantee
// there is always an equivalent pair of instructions.
multiclass I<dag oops_r, dag iops_r, dag oops_s, dag iops_s,
             list<dag> pattern_r, string asmstr_r = "", string asmstr_s = "",
             bits<32> inst = -1> {
  let isCodeGenOnly = 1 in
  def "" : NI<oops_r, iops_r, pattern_r, "false", asmstr_r, inst>;
  let BaseName = NAME in
  def _S : NI<oops_s, iops_s, [], "true", asmstr_s, inst>;
}

// For instructions that have no register ops, so both sets are the same.
multiclass NRI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
               bits<32> inst = -1> {
  defm "": I<oops, iops, oops, iops, pattern, asmstr, asmstr, inst>;
}
