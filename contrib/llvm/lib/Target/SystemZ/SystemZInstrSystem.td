//==- SystemZInstrSystem.td - SystemZ system instructions -*- tblgen-*-----==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The instructions in this file implement SystemZ system-level instructions.
// Most of these instructions are privileged or semi-privileged.  They are
// not used for code generation, but are provided for use with the assembler
// and disassembler only.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Program-Status Word Instructions.
//===----------------------------------------------------------------------===//

// Extract PSW.
let hasSideEffects = 1, Uses = [CC] in
  def EPSW : InherentDualRRE<"epsw", 0xB98D, GR32>;

// Load PSW (extended).
let hasSideEffects = 1, Defs = [CC] in {
  def LPSW : SideEffectUnaryS<"lpsw", 0x8200, null_frag, 8>;
  def LPSWE : SideEffectUnaryS<"lpswe", 0xB2B2, null_frag, 16>;
}

// Insert PSW key.
let Uses = [R2L], Defs = [R2L] in
  def IPK : SideEffectInherentS<"ipk", 0xB20B, null_frag>;

// Set PSW key from address.
let hasSideEffects = 1 in
  def SPKA : SideEffectAddressS<"spka", 0xB20A, null_frag>;

// Set system mask.
let hasSideEffects = 1 in
  def SSM : SideEffectUnaryS<"ssm", 0x8000, null_frag, 1>;

// Store then AND/OR system mask.
let hasSideEffects = 1 in {
  def STNSM : StoreSI<"stnsm", 0xAC, null_frag, imm32zx8>;
  def STOSM : StoreSI<"stosm", 0xAD, null_frag, imm32zx8>;
}

// Insert address space control.
let hasSideEffects = 1 in
  def IAC : InherentRRE<"iac", 0xB224, GR32, null_frag>;

// Set address space control (fast).
let hasSideEffects = 1 in {
  def SAC : SideEffectAddressS<"sac", 0xB219, null_frag>;
  def SACF : SideEffectAddressS<"sacf", 0xB279, null_frag>;
}

//===----------------------------------------------------------------------===//
// Control Register Instructions.
//===----------------------------------------------------------------------===//

let hasSideEffects = 1 in {
  // Load control.
  def LCTL : LoadMultipleRS<"lctl", 0xB7, CR64>;
  def LCTLG : LoadMultipleRSY<"lctlg", 0xEB2F, CR64>;

  // Store control.
  def STCTL : StoreMultipleRS<"stctl", 0xB6, CR64>;
  def STCTG : StoreMultipleRSY<"stctg", 0xEB25, CR64>;
}

// Extract primary ASN (and instance).
let hasSideEffects = 1 in {
  def EPAR : InherentRRE<"epar", 0xB226, GR32, null_frag>;
  def EPAIR : InherentRRE<"epair", 0xB99A, GR64, null_frag>;
}

// Extract secondary ASN (and instance).
let hasSideEffects = 1 in {
  def ESAR : InherentRRE<"esar", 0xB227, GR32, null_frag>;
  def ESAIR : InherentRRE<"esair", 0xB99B, GR64, null_frag>;
}

// Set secondary ASN (and instance).
let hasSideEffects = 1 in {
  def SSAR : SideEffectUnaryRRE<"ssar", 0xB225, GR32, null_frag>;
  def SSAIR : SideEffectUnaryRRE<"ssair", 0xB99F, GR64, null_frag>;
}

// Extract and set extended authority.
let hasSideEffects = 1 in
  def ESEA : UnaryTiedRRE<"esea", 0xB99D, GR32>;

//===----------------------------------------------------------------------===//
// Prefix-Register Instructions.
//===----------------------------------------------------------------------===//

// Set prefix.
let hasSideEffects = 1 in
  def SPX : SideEffectUnaryS<"spx", 0xB210, null_frag, 4>;

// Store prefix.
let hasSideEffects = 1 in
  def STPX : StoreInherentS<"stpx", 0xB211, null_frag, 4>;

//===----------------------------------------------------------------------===//
// Storage-Key and Real Memory Instructions.
//===----------------------------------------------------------------------===//

// Insert storage key extended.
let hasSideEffects = 1 in
  def ISKE : BinaryRRE<"iske", 0xB229, null_frag, GR32, GR64>;

// Insert virtual storage key.
let hasSideEffects = 1 in
  def IVSK : BinaryRRE<"ivsk", 0xB223, null_frag, GR32, GR64>;

// Set storage key extended.
let hasSideEffects = 1, Defs = [CC] in
  defm SSKE : SideEffectTernaryRRFcOpt<"sske", 0xB22B, GR32, GR64>;

// Reset reference bit extended.
let hasSideEffects = 1, Defs = [CC] in
  def RRBE : SideEffectBinaryRRE<"rrbe", 0xB22A, GR32, GR64>;

// Reset reference bits multiple.
let Predicates = [FeatureResetReferenceBitsMultiple], hasSideEffects = 1 in
  def RRBM : UnaryRRE<"rrbm", 0xB9AE, null_frag, GR64, GR64>;

// Insert reference bits multiple.
let Predicates = [FeatureInsertReferenceBitsMultiple], hasSideEffects = 1 in
  def IRBM : UnaryRRE<"irbm", 0xB9AC, null_frag, GR64, GR64>;

// Perform frame management function.
let hasSideEffects = 1 in
  def PFMF : SideEffectBinaryMemRRE<"pfmf", 0xB9AF, GR32, GR64>;

// Test block.
let hasSideEffects = 1, mayStore = 1, Uses = [R0D], Defs = [R0D, CC] in
  def TB : SideEffectBinaryRRE<"tb", 0xB22C, GR64, GR64>;

// Page in / out.
let mayLoad = 1, mayStore = 1, Defs = [CC] in {
  def PGIN : SideEffectBinaryRRE<"pgin", 0xB22E, GR64, GR64>;
  def PGOUT : SideEffectBinaryRRE<"pgout", 0xB22F, GR64, GR64>;
}

//===----------------------------------------------------------------------===//
// Dynamic-Address-Translation Instructions.
//===----------------------------------------------------------------------===//

// Invalidate page table entry.
let hasSideEffects = 1 in
  defm IPTE : SideEffectQuaternaryRRFaOptOpt<"ipte", 0xB221, GR64, GR32, GR32>;

// Invalidate DAT table entry.
let hasSideEffects = 1 in
  defm IDTE : SideEffectQuaternaryRRFbOpt<"idte", 0xB98E, GR64, GR64, GR64>;

// Compare and replace DAT table entry.
let Predicates = [FeatureEnhancedDAT2], hasSideEffects = 1, Defs = [CC] in
  defm CRDTE : SideEffectQuaternaryRRFbOpt<"crdte", 0xB98F, GR128, GR128, GR64>;

// Purge TLB.
let hasSideEffects = 1 in
  def PTLB : SideEffectInherentS<"ptlb", 0xB20D, null_frag>;

// Compare and swap and purge.
let hasSideEffects = 1, Defs = [CC] in {
  def CSP : CmpSwapRRE<"csp", 0xB250, GR128, GR64>;
  def CSPG : CmpSwapRRE<"cspg", 0xB98A, GR128, GR64>;
}

// Load page-table-entry address.
let hasSideEffects = 1, Defs = [CC] in
  def LPTEA : TernaryRRFb<"lptea", 0xB9AA, GR64, GR64, GR64>;

// Load real address.
let hasSideEffects = 1, Defs = [CC] in {
  defm LRA : LoadAddressRXPair<"lra", 0xB1, 0xE313, null_frag>;
  def LRAG : LoadAddressRXY<"lrag", 0xE303, null_frag, laaddr20pair>;
}

// Store real address.
def STRAG : StoreSSE<"strag", 0xE502>;

// Load using real address.
let mayLoad = 1 in {
 def LURA : UnaryRRE<"lura", 0xB24B, null_frag, GR32, GR64>;
 def LURAG : UnaryRRE<"lurag", 0xB905, null_frag, GR64, GR64>;
}

// Store using real address.
let mayStore = 1 in {
 def STURA : SideEffectBinaryRRE<"stura", 0xB246, GR32, GR64>;
 def STURG : SideEffectBinaryRRE<"sturg", 0xB925, GR64, GR64>;
}

// Test protection.
let hasSideEffects = 1, Defs = [CC] in
  def TPROT : SideEffectBinarySSE<"tprot", 0xE501>;

//===----------------------------------------------------------------------===//
// Memory-move Instructions.
//===----------------------------------------------------------------------===//

// Move with key.
let mayLoad = 1, mayStore = 1, Defs = [CC] in
  def MVCK : MemoryBinarySSd<"mvck", 0xD9, GR64>;

// Move to primary / secondary.
let mayLoad = 1, mayStore = 1, Defs = [CC] in {
  def MVCP : MemoryBinarySSd<"mvcp", 0xDA, GR64>;
  def MVCS : MemoryBinarySSd<"mvcs", 0xDB, GR64>;
}

// Move with source / destination key.
let mayLoad = 1, mayStore = 1, Uses = [R0L, R1L] in {
  def MVCSK : SideEffectBinarySSE<"mvcsk", 0xE50E>;
  def MVCDK : SideEffectBinarySSE<"mvcdk", 0xE50F>;
}

// Move with optional specifications.
let mayLoad = 1, mayStore = 1, Uses = [R0L] in
  def MVCOS : SideEffectTernarySSF<"mvcos", 0xC80, GR64>;

// Move page.
let mayLoad = 1, mayStore = 1, Uses = [R0L], Defs = [CC] in
  def MVPG : SideEffectBinaryRRE<"mvpg", 0xB254, GR64, GR64>;

//===----------------------------------------------------------------------===//
// Address-Space Instructions.
//===----------------------------------------------------------------------===//

// Load address space parameters.
let hasSideEffects = 1, Defs = [CC] in
  def LASP : SideEffectBinarySSE<"lasp", 0xE500>;

// Purge ALB.
let hasSideEffects = 1 in
  def PALB : SideEffectInherentRRE<"palb", 0xB248>;

// Program call.
let hasSideEffects = 1 in
  def PC : SideEffectAddressS<"pc", 0xB218, null_frag>;

// Program return.
let hasSideEffects = 1, Defs = [CC] in
  def PR : SideEffectInherentE<"pr", 0x0101>;

// Program transfer (with instance).
let hasSideEffects = 1 in {
  def PT : SideEffectBinaryRRE<"pt", 0xB228, GR32, GR64>;
  def PTI : SideEffectBinaryRRE<"pti", 0xB99E, GR64, GR64>;
}

// Resume program.
let hasSideEffects = 1, Defs = [CC] in
  def RP : SideEffectAddressS<"rp", 0xB277, null_frag>;

// Branch in subspace group.
let hasSideEffects = 1 in
  def BSG : UnaryRRE<"bsg", 0xB258, null_frag, GR64, GR64>;

// Branch and set authority.
let hasSideEffects = 1 in
  def BSA : UnaryRRE<"bsa", 0xB25A, null_frag, GR64, GR64>;

// Test access.
let Defs = [CC] in
  def TAR : SideEffectBinaryRRE<"tar", 0xB24C, AR32, GR32>;

//===----------------------------------------------------------------------===//
// Linkage-Stack Instructions.
//===----------------------------------------------------------------------===//

// Branch and stack.
let hasSideEffects = 1 in
  def BAKR : SideEffectBinaryRRE<"bakr", 0xB240, GR64, GR64>;

// Extract stacked registers.
let hasSideEffects = 1 in {
  def EREG : SideEffectBinaryRRE<"ereg", 0xB249, GR32, GR32>;
  def EREGG : SideEffectBinaryRRE<"eregg", 0xB90E, GR64, GR64>;
}

// Extract stacked state.
let hasSideEffects = 1, Defs = [CC] in
  def ESTA : UnaryRRE<"esta", 0xB24A, null_frag, GR128, GR32>;

// Modify stacked state.
let hasSideEffects = 1 in
  def MSTA : SideEffectUnaryRRE<"msta", 0xB247, GR128, null_frag>;

//===----------------------------------------------------------------------===//
// Time-Related Instructions.
//===----------------------------------------------------------------------===//

// Perform timing facility function.
let hasSideEffects = 1, mayLoad = 1, Uses = [R0L, R1D], Defs = [CC] in
  def PTFF : SideEffectInherentE<"ptff", 0x0104>;

// Set clock.
let hasSideEffects = 1, Defs = [CC] in
  def SCK : SideEffectUnaryS<"sck", 0xB204, null_frag, 8>;

// Set clock programmable field.
let hasSideEffects = 1, Uses = [R0L] in
  def SCKPF : SideEffectInherentE<"sckpf", 0x0107>;

// Set clock comparator.
let hasSideEffects = 1 in
  def SCKC : SideEffectUnaryS<"sckc", 0xB206, null_frag, 8>;

// Set CPU timer.
let hasSideEffects = 1 in
  def SPT : SideEffectUnaryS<"spt", 0xB208, null_frag, 8>;

// Store clock (fast / extended).
let hasSideEffects = 1, Defs = [CC] in {
  def STCK  : StoreInherentS<"stck",  0xB205, null_frag, 8>;
  def STCKF : StoreInherentS<"stckf", 0xB27C, null_frag, 8>;
  def STCKE : StoreInherentS<"stcke", 0xB278, null_frag, 16>;
}

// Store clock comparator.
let hasSideEffects = 1 in
  def STCKC : StoreInherentS<"stckc", 0xB207, null_frag, 8>;

// Store CPU timer.
let hasSideEffects = 1 in
  def STPT : StoreInherentS<"stpt", 0xB209, null_frag, 8>;

//===----------------------------------------------------------------------===//
// CPU-Related Instructions.
//===----------------------------------------------------------------------===//

// Store CPU address.
let hasSideEffects = 1 in
  def STAP : StoreInherentS<"stap", 0xB212, null_frag, 2>;

// Store CPU ID.
let hasSideEffects = 1 in
  def STIDP : StoreInherentS<"stidp", 0xB202, null_frag, 8>;

// Store system information.
let hasSideEffects = 1, Uses = [R0L, R1L], Defs = [R0L, CC] in
  def STSI : StoreInherentS<"stsi", 0xB27D, null_frag, 0>;

// Store facility list.
let hasSideEffects = 1 in
  def STFL : StoreInherentS<"stfl", 0xB2B1, null_frag, 4>;

// Store facility list extended.
let hasSideEffects = 1, Uses = [R0D], Defs = [R0D, CC] in
  def STFLE : StoreInherentS<"stfle", 0xB2B0, null_frag, 0>;

// Extract CPU attribute.
let hasSideEffects = 1 in
  def ECAG : BinaryRSY<"ecag", 0xEB4C, null_frag, GR64>;

// Extract CPU time.
let hasSideEffects = 1, mayLoad = 1, Defs = [R0D, R1D] in
  def ECTG : SideEffectTernarySSF<"ectg", 0xC81, GR64>;

// Perform topology function.
let hasSideEffects = 1 in
  def PTF : UnaryTiedRRE<"ptf", 0xB9A2, GR64>;

// Perform cryptographic key management operation.
let Predicates = [FeatureMessageSecurityAssist3],
    hasSideEffects = 1, Uses = [R0L, R1D] in
  def PCKMO : SideEffectInherentRRE<"pckmo", 0xB928>;

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions.
//===----------------------------------------------------------------------===//

// Supervisor call.
let hasSideEffects = 1, isCall = 1, Defs = [CC] in
  def SVC : SideEffectUnaryI<"svc", 0x0A, imm32zx8>;

// Monitor call.
let hasSideEffects = 1, isCall = 1 in
  def MC : SideEffectBinarySI<"mc", 0xAF, imm32zx8>;

// Diagnose.
let hasSideEffects = 1, isCall = 1 in
  def DIAG : SideEffectTernaryRS<"diag", 0x83, GR32, GR32>;

// Trace.
let hasSideEffects = 1, mayLoad = 1 in {
  def TRACE : SideEffectTernaryRS<"trace", 0x99, GR32, GR32>;
  def TRACG : SideEffectTernaryRSY<"tracg", 0xEB0F, GR64, GR64>;
}

// Trap.
let hasSideEffects = 1 in {
  def TRAP2 : SideEffectInherentE<"trap2", 0x01FF>;
  def TRAP4 : SideEffectAddressS<"trap4", 0xB2FF, null_frag>;
}

// Signal processor.
let hasSideEffects = 1, Defs = [CC] in
  def SIGP : SideEffectTernaryRS<"sigp", 0xAE, GR64, GR64>;

// Signal adapter.
let hasSideEffects = 1, Uses = [R0D, R1D, R2D, R3D], Defs = [CC] in
  def SIGA : SideEffectAddressS<"siga", 0xB274, null_frag>;

// Start interpretive execution.
let hasSideEffects = 1, Defs = [CC] in
  def SIE : SideEffectUnaryS<"sie", 0xB214, null_frag, 0>;

//===----------------------------------------------------------------------===//
// CPU-Measurement Facility Instructions (SA23-2260).
//===----------------------------------------------------------------------===//

// Load program parameter
let hasSideEffects = 1 in
  def LPP : SideEffectUnaryS<"lpp", 0xB280, null_frag, 8>;

// Extract coprocessor-group address.
let hasSideEffects = 1, Defs = [CC] in
  def ECPGA : UnaryRRE<"ecpga", 0xB2ED, null_frag, GR32, GR64>;

// Extract CPU counter.
let hasSideEffects = 1, Defs = [CC] in
  def ECCTR : UnaryRRE<"ecctr", 0xB2E4, null_frag, GR64, GR64>;

// Extract peripheral counter.
let hasSideEffects = 1, Defs = [CC] in
  def EPCTR : UnaryRRE<"epctr", 0xB2E5, null_frag, GR64, GR64>;

// Load CPU-counter-set controls.
let hasSideEffects = 1, Defs = [CC] in
  def LCCTL : SideEffectUnaryS<"lcctl", 0xB284, null_frag, 8>;

// Load peripheral-counter-set controls.
let hasSideEffects = 1, Defs = [CC] in
  def LPCTL : SideEffectUnaryS<"lpctl", 0xB285, null_frag, 8>;

// Load sampling controls.
let hasSideEffects = 1, Defs = [CC] in
  def LSCTL : SideEffectUnaryS<"lsctl", 0xB287, null_frag, 0>;

// Query sampling information.
let hasSideEffects = 1 in
  def QSI : StoreInherentS<"qsi", 0xB286, null_frag, 0>;

// Query counter information.
let hasSideEffects = 1 in
  def QCTRI : StoreInherentS<"qctri", 0xB28E, null_frag, 0>;

// Set CPU counter.
let hasSideEffects = 1, Defs = [CC] in
  def SCCTR : SideEffectBinaryRRE<"scctr", 0xB2E0, GR64, GR64>;

// Set peripheral counter.
let hasSideEffects = 1, Defs = [CC] in
  def SPCTR : SideEffectBinaryRRE<"spctr", 0xB2E1, GR64, GR64>;

//===----------------------------------------------------------------------===//
// I/O Instructions (Principles of Operation, Chapter 14).
//===----------------------------------------------------------------------===//

// Clear subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def CSCH : SideEffectInherentS<"csch", 0xB230, null_frag>;

// Halt subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def HSCH : SideEffectInherentS<"hsch", 0xB231, null_frag>;

// Modify subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def MSCH : SideEffectUnaryS<"msch", 0xB232, null_frag, 0>;

// Resume subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def RSCH : SideEffectInherentS<"rsch", 0xB238, null_frag>;

// Start subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def SSCH : SideEffectUnaryS<"ssch", 0xB233, null_frag, 0>;

// Store subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def STSCH : StoreInherentS<"stsch", 0xB234, null_frag, 0>;

// Test subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def TSCH : StoreInherentS<"tsch", 0xB235, null_frag, 0>;

// Cancel subchannel.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def XSCH : SideEffectInherentS<"xsch", 0xB276, null_frag>;

// Reset channel path.
let hasSideEffects = 1, Uses = [R1L], Defs = [CC] in
  def RCHP : SideEffectInherentS<"rchp", 0xB23B, null_frag>;

// Set channel monitor.
let hasSideEffects = 1, mayLoad = 1, Uses = [R1L, R2D] in
  def SCHM : SideEffectInherentS<"schm", 0xB23C, null_frag>;

// Store channel path status.
let hasSideEffects = 1 in
  def STCPS : StoreInherentS<"stcps", 0xB23A, null_frag, 0>;

// Store channel report word.
let hasSideEffects = 1, Defs = [CC] in
  def STCRW : StoreInherentS<"stcrw", 0xB239, null_frag, 0>;

// Test pending interruption.
let hasSideEffects = 1, Defs = [CC] in
  def TPI : StoreInherentS<"tpi", 0xB236, null_frag, 0>;

// Set address limit.
let hasSideEffects = 1, Uses = [R1L] in
  def SAL : SideEffectInherentS<"sal", 0xB237, null_frag>;

