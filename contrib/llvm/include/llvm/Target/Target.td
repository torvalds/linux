//===- Target.td - Target Independent TableGen interface ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces which should be
// implemented by each target which is using a TableGen based code generator.
//
//===----------------------------------------------------------------------===//

// Include all information about LLVM intrinsics.
include "llvm/IR/Intrinsics.td"

//===----------------------------------------------------------------------===//
// Register file description - These classes are used to fill in the target
// description classes.

class RegisterClass; // Forward def

class HwMode<string FS> {
  // A string representing subtarget features that turn on this HW mode.
  // For example, "+feat1,-feat2" will indicate that the mode is active
  // when "feat1" is enabled and "feat2" is disabled at the same time.
  // Any other features are not checked.
  // When multiple modes are used, they should be mutually exclusive,
  // otherwise the results are unpredictable.
  string Features = FS;
}

// A special mode recognized by tablegen. This mode is considered active
// when no other mode is active. For targets that do not use specific hw
// modes, this is the only mode.
def DefaultMode : HwMode<"">;

// A class used to associate objects with HW modes. It is only intended to
// be used as a base class, where the derived class should contain a member
// "Objects", which is a list of the same length as the list of modes.
// The n-th element on the Objects list will be associated with the n-th
// element on the Modes list.
class HwModeSelect<list<HwMode> Ms> {
  list<HwMode> Modes = Ms;
}

// A common class that implements a counterpart of ValueType, which is
// dependent on a HW mode. This class inherits from ValueType itself,
// which makes it possible to use objects of this class where ValueType
// objects could be used. This is specifically applicable to selection
// patterns.
class ValueTypeByHwMode<list<HwMode> Ms, list<ValueType> Ts>
    : HwModeSelect<Ms>, ValueType<0, 0> {
  // The length of this list must be the same as the length of Ms.
  list<ValueType> Objects = Ts;
}

// A class representing the register size, spill size and spill alignment
// in bits of a register.
class RegInfo<int RS, int SS, int SA> {
  int RegSize = RS;         // Register size in bits.
  int SpillSize = SS;       // Spill slot size in bits.
  int SpillAlignment = SA;  // Spill slot alignment in bits.
}

// The register size/alignment information, parameterized by a HW mode.
class RegInfoByHwMode<list<HwMode> Ms = [], list<RegInfo> Ts = []>
    : HwModeSelect<Ms> {
  // The length of this list must be the same as the length of Ms.
  list<RegInfo> Objects = Ts;
}

// SubRegIndex - Use instances of SubRegIndex to identify subregisters.
class SubRegIndex<int size, int offset = 0> {
  string Namespace = "";

  // Size - Size (in bits) of the sub-registers represented by this index.
  int Size = size;

  // Offset - Offset of the first bit that is part of this sub-register index.
  // Set it to -1 if the same index is used to represent sub-registers that can
  // be at different offsets (for example when using an index to access an
  // element in a register tuple).
  int Offset = offset;

  // ComposedOf - A list of two SubRegIndex instances, [A, B].
  // This indicates that this SubRegIndex is the result of composing A and B.
  // See ComposedSubRegIndex.
  list<SubRegIndex> ComposedOf = [];

  // CoveringSubRegIndices - A list of two or more sub-register indexes that
  // cover this sub-register.
  //
  // This field should normally be left blank as TableGen can infer it.
  //
  // TableGen automatically detects sub-registers that straddle the registers
  // in the SubRegs field of a Register definition. For example:
  //
  //   Q0    = dsub_0 -> D0, dsub_1 -> D1
  //   Q1    = dsub_0 -> D2, dsub_1 -> D3
  //   D1_D2 = dsub_0 -> D1, dsub_1 -> D2
  //   QQ0   = qsub_0 -> Q0, qsub_1 -> Q1
  //
  // TableGen will infer that D1_D2 is a sub-register of QQ0. It will be given
  // the synthetic index dsub_1_dsub_2 unless some SubRegIndex is defined with
  // CoveringSubRegIndices = [dsub_1, dsub_2].
  list<SubRegIndex> CoveringSubRegIndices = [];
}

// ComposedSubRegIndex - A sub-register that is the result of composing A and B.
// Offset is set to the sum of A and B's Offsets. Size is set to B's Size.
class ComposedSubRegIndex<SubRegIndex A, SubRegIndex B>
  : SubRegIndex<B.Size, !if(!eq(A.Offset, -1), -1,
                        !if(!eq(B.Offset, -1), -1,
                            !add(A.Offset, B.Offset)))> {
  // See SubRegIndex.
  let ComposedOf = [A, B];
}

// RegAltNameIndex - The alternate name set to use for register operands of
// this register class when printing.
class RegAltNameIndex {
  string Namespace = "";
}
def NoRegAltName : RegAltNameIndex;

// Register - You should define one instance of this class for each register
// in the target machine.  String n will become the "name" of the register.
class Register<string n, list<string> altNames = []> {
  string Namespace = "";
  string AsmName = n;
  list<string> AltNames = altNames;

  // Aliases - A list of registers that this register overlaps with.  A read or
  // modification of this register can potentially read or modify the aliased
  // registers.
  list<Register> Aliases = [];

  // SubRegs - A list of registers that are parts of this register. Note these
  // are "immediate" sub-registers and the registers within the list do not
  // themselves overlap. e.g. For X86, EAX's SubRegs list contains only [AX],
  // not [AX, AH, AL].
  list<Register> SubRegs = [];

  // SubRegIndices - For each register in SubRegs, specify the SubRegIndex used
  // to address it. Sub-sub-register indices are automatically inherited from
  // SubRegs.
  list<SubRegIndex> SubRegIndices = [];

  // RegAltNameIndices - The alternate name indices which are valid for this
  // register.
  list<RegAltNameIndex> RegAltNameIndices = [];

  // DwarfNumbers - Numbers used internally by gcc/gdb to identify the register.
  // These values can be determined by locating the <target>.h file in the
  // directory llvmgcc/gcc/config/<target>/ and looking for REGISTER_NAMES.  The
  // order of these names correspond to the enumeration used by gcc.  A value of
  // -1 indicates that the gcc number is undefined and -2 that register number
  // is invalid for this mode/flavour.
  list<int> DwarfNumbers = [];

  // CostPerUse - Additional cost of instructions using this register compared
  // to other registers in its class. The register allocator will try to
  // minimize the number of instructions using a register with a CostPerUse.
  // This is used by the x86-64 and ARM Thumb targets where some registers
  // require larger instruction encodings.
  int CostPerUse = 0;

  // CoveredBySubRegs - When this bit is set, the value of this register is
  // completely determined by the value of its sub-registers.  For example, the
  // x86 register AX is covered by its sub-registers AL and AH, but EAX is not
  // covered by its sub-register AX.
  bit CoveredBySubRegs = 0;

  // HWEncoding - The target specific hardware encoding for this register.
  bits<16> HWEncoding = 0;

  bit isArtificial = 0;
}

// RegisterWithSubRegs - This can be used to define instances of Register which
// need to specify sub-registers.
// List "subregs" specifies which registers are sub-registers to this one. This
// is used to populate the SubRegs and AliasSet fields of TargetRegisterDesc.
// This allows the code generator to be careful not to put two values with
// overlapping live ranges into registers which alias.
class RegisterWithSubRegs<string n, list<Register> subregs> : Register<n> {
  let SubRegs = subregs;
}

// DAGOperand - An empty base class that unifies RegisterClass's and other forms
// of Operand's that are legal as type qualifiers in DAG patterns.  This should
// only ever be used for defining multiclasses that are polymorphic over both
// RegisterClass's and other Operand's.
class DAGOperand {
  string OperandNamespace = "MCOI";
  string DecoderMethod = "";
}

// RegisterClass - Now that all of the registers are defined, and aliases
// between registers are defined, specify which registers belong to which
// register classes.  This also defines the default allocation order of
// registers by register allocators.
//
class RegisterClass<string namespace, list<ValueType> regTypes, int alignment,
                    dag regList, RegAltNameIndex idx = NoRegAltName>
  : DAGOperand {
  string Namespace = namespace;

  // The register size/alignment information, parameterized by a HW mode.
  RegInfoByHwMode RegInfos;

  // RegType - Specify the list ValueType of the registers in this register
  // class.  Note that all registers in a register class must have the same
  // ValueTypes.  This is a list because some targets permit storing different
  // types in same register, for example vector values with 128-bit total size,
  // but different count/size of items, like SSE on x86.
  //
  list<ValueType> RegTypes = regTypes;

  // Size - Specify the spill size in bits of the registers.  A default value of
  // zero lets tablgen pick an appropriate size.
  int Size = 0;

  // Alignment - Specify the alignment required of the registers when they are
  // stored or loaded to memory.
  //
  int Alignment = alignment;

  // CopyCost - This value is used to specify the cost of copying a value
  // between two registers in this register class. The default value is one
  // meaning it takes a single instruction to perform the copying. A negative
  // value means copying is extremely expensive or impossible.
  int CopyCost = 1;

  // MemberList - Specify which registers are in this class.  If the
  // allocation_order_* method are not specified, this also defines the order of
  // allocation used by the register allocator.
  //
  dag MemberList = regList;

  // AltNameIndex - The alternate register name to use when printing operands
  // of this register class. Every register in the register class must have
  // a valid alternate name for the given index.
  RegAltNameIndex altNameIndex = idx;

  // isAllocatable - Specify that the register class can be used for virtual
  // registers and register allocation.  Some register classes are only used to
  // model instruction operand constraints, and should have isAllocatable = 0.
  bit isAllocatable = 1;

  // AltOrders - List of alternative allocation orders. The default order is
  // MemberList itself, and that is good enough for most targets since the
  // register allocators automatically remove reserved registers and move
  // callee-saved registers to the end.
  list<dag> AltOrders = [];

  // AltOrderSelect - The body of a function that selects the allocation order
  // to use in a given machine function. The code will be inserted in a
  // function like this:
  //
  //   static inline unsigned f(const MachineFunction &MF) { ... }
  //
  // The function should return 0 to select the default order defined by
  // MemberList, 1 to select the first AltOrders entry and so on.
  code AltOrderSelect = [{}];

  // Specify allocation priority for register allocators using a greedy
  // heuristic. Classes with higher priority values are assigned first. This is
  // useful as it is sometimes beneficial to assign registers to highly
  // constrained classes first. The value has to be in the range [0,63].
  int AllocationPriority = 0;

  // The diagnostic type to present when referencing this operand in a match
  // failure error message. If this is empty, the default Match_InvalidOperand
  // diagnostic type will be used. If this is "<name>", a Match_<name> enum
  // value will be generated and used for this operand type. The target
  // assembly parser is responsible for converting this into a user-facing
  // diagnostic message.
  string DiagnosticType = "";

  // A diagnostic message to emit when an invalid value is provided for this
  // register class when it is being used an an assembly operand. If this is
  // non-empty, an anonymous diagnostic type enum value will be generated, and
  // the assembly matcher will provide a function to map from diagnostic types
  // to message strings.
  string DiagnosticString = "";
}

// The memberList in a RegisterClass is a dag of set operations. TableGen
// evaluates these set operations and expand them into register lists. These
// are the most common operation, see test/TableGen/SetTheory.td for more
// examples of what is possible:
//
// (add R0, R1, R2) - Set Union. Each argument can be an individual register, a
// register class, or a sub-expression. This is also the way to simply list
// registers.
//
// (sub GPR, SP) - Set difference. Subtract the last arguments from the first.
//
// (and GPR, CSR) - Set intersection. All registers from the first set that are
// also in the second set.
//
// (sequence "R%u", 0, 15) -> [R0, R1, ..., R15]. Generate a sequence of
// numbered registers.  Takes an optional 4th operand which is a stride to use
// when generating the sequence.
//
// (shl GPR, 4) - Remove the first N elements.
//
// (trunc GPR, 4) - Truncate after the first N elements.
//
// (rotl GPR, 1) - Rotate N places to the left.
//
// (rotr GPR, 1) - Rotate N places to the right.
//
// (decimate GPR, 2) - Pick every N'th element, starting with the first.
//
// (interleave A, B, ...) - Interleave the elements from each argument list.
//
// All of these operators work on ordered sets, not lists. That means
// duplicates are removed from sub-expressions.

// Set operators. The rest is defined in TargetSelectionDAG.td.
def sequence;
def decimate;
def interleave;

// RegisterTuples - Automatically generate super-registers by forming tuples of
// sub-registers. This is useful for modeling register sequence constraints
// with pseudo-registers that are larger than the architectural registers.
//
// The sub-register lists are zipped together:
//
//   def EvenOdd : RegisterTuples<[sube, subo], [(add R0, R2), (add R1, R3)]>;
//
// Generates the same registers as:
//
//   let SubRegIndices = [sube, subo] in {
//     def R0_R1 : RegisterWithSubRegs<"", [R0, R1]>;
//     def R2_R3 : RegisterWithSubRegs<"", [R2, R3]>;
//   }
//
// The generated pseudo-registers inherit super-classes and fields from their
// first sub-register. Most fields from the Register class are inferred, and
// the AsmName and Dwarf numbers are cleared.
//
// RegisterTuples instances can be used in other set operations to form
// register classes and so on. This is the only way of using the generated
// registers.
class RegisterTuples<list<SubRegIndex> Indices, list<dag> Regs> {
  // SubRegs - N lists of registers to be zipped up. Super-registers are
  // synthesized from the first element of each SubRegs list, the second
  // element and so on.
  list<dag> SubRegs = Regs;

  // SubRegIndices - N SubRegIndex instances. This provides the names of the
  // sub-registers in the synthesized super-registers.
  list<SubRegIndex> SubRegIndices = Indices;
}


//===----------------------------------------------------------------------===//
// DwarfRegNum - This class provides a mapping of the llvm register enumeration
// to the register numbering used by gcc and gdb.  These values are used by a
// debug information writer to describe where values may be located during
// execution.
class DwarfRegNum<list<int> Numbers> {
  // DwarfNumbers - Numbers used internally by gcc/gdb to identify the register.
  // These values can be determined by locating the <target>.h file in the
  // directory llvmgcc/gcc/config/<target>/ and looking for REGISTER_NAMES.  The
  // order of these names correspond to the enumeration used by gcc.  A value of
  // -1 indicates that the gcc number is undefined and -2 that register number
  // is invalid for this mode/flavour.
  list<int> DwarfNumbers = Numbers;
}

// DwarfRegAlias - This class declares that a given register uses the same dwarf
// numbers as another one. This is useful for making it clear that the two
// registers do have the same number. It also lets us build a mapping
// from dwarf register number to llvm register.
class DwarfRegAlias<Register reg> {
      Register DwarfAlias = reg;
}

//===----------------------------------------------------------------------===//
// Pull in the common support for MCPredicate (portable scheduling predicates).
//
include "llvm/Target/TargetInstrPredicate.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for scheduling
//
include "llvm/Target/TargetSchedule.td"

class Predicate; // Forward def

//===----------------------------------------------------------------------===//
// Instruction set description - These classes correspond to the C++ classes in
// the Target/TargetInstrInfo.h file.
//
class Instruction {
  string Namespace = "";

  dag OutOperandList;       // An dag containing the MI def operand list.
  dag InOperandList;        // An dag containing the MI use operand list.
  string AsmString = "";    // The .s format to print the instruction with.

  // Pattern - Set to the DAG pattern for this instruction, if we know of one,
  // otherwise, uninitialized.
  list<dag> Pattern;

  // The follow state will eventually be inferred automatically from the
  // instruction pattern.

  list<Register> Uses = []; // Default to using no non-operand registers
  list<Register> Defs = []; // Default to modifying no non-operand registers

  // Predicates - List of predicates which will be turned into isel matching
  // code.
  list<Predicate> Predicates = [];

  // Size - Size of encoded instruction, or zero if the size cannot be determined
  // from the opcode.
  int Size = 0;

  // DecoderNamespace - The "namespace" in which this instruction exists, on
  // targets like ARM which multiple ISA namespaces exist.
  string DecoderNamespace = "";

  // Code size, for instruction selection.
  // FIXME: What does this actually mean?
  int CodeSize = 0;

  // Added complexity passed onto matching pattern.
  int AddedComplexity  = 0;

  // These bits capture information about the high-level semantics of the
  // instruction.
  bit isReturn     = 0;     // Is this instruction a return instruction?
  bit isBranch     = 0;     // Is this instruction a branch instruction?
  bit isEHScopeReturn = 0;  // Does this instruction end an EH scope?
  bit isIndirectBranch = 0; // Is this instruction an indirect branch?
  bit isCompare    = 0;     // Is this instruction a comparison instruction?
  bit isMoveImm    = 0;     // Is this instruction a move immediate instruction?
  bit isMoveReg    = 0;     // Is this instruction a move register instruction?
  bit isBitcast    = 0;     // Is this instruction a bitcast instruction?
  bit isSelect     = 0;     // Is this instruction a select instruction?
  bit isBarrier    = 0;     // Can control flow fall through this instruction?
  bit isCall       = 0;     // Is this instruction a call instruction?
  bit isAdd        = 0;     // Is this instruction an add instruction?
  bit isTrap       = 0;     // Is this instruction a trap instruction?
  bit canFoldAsLoad = 0;    // Can this be folded as a simple memory operand?
  bit mayLoad      = ?;     // Is it possible for this inst to read memory?
  bit mayStore     = ?;     // Is it possible for this inst to write memory?
  bit isConvertibleToThreeAddress = 0;  // Can this 2-addr instruction promote?
  bit isCommutable = 0;     // Is this 3 operand instruction commutable?
  bit isTerminator = 0;     // Is this part of the terminator for a basic block?
  bit isReMaterializable = 0; // Is this instruction re-materializable?
  bit isPredicable = 0;     // Is this instruction predicable?
  bit hasDelaySlot = 0;     // Does this instruction have an delay slot?
  bit usesCustomInserter = 0; // Pseudo instr needing special help.
  bit hasPostISelHook = 0;  // To be *adjusted* after isel by target hook.
  bit hasCtrlDep   = 0;     // Does this instruction r/w ctrl-flow chains?
  bit isNotDuplicable = 0;  // Is it unsafe to duplicate this instruction?
  bit isConvergent = 0;     // Is this instruction convergent?
  bit isAsCheapAsAMove = 0; // As cheap (or cheaper) than a move instruction.
  bit hasExtraSrcRegAllocReq = 0; // Sources have special regalloc requirement?
  bit hasExtraDefRegAllocReq = 0; // Defs have special regalloc requirement?
  bit isRegSequence = 0;    // Is this instruction a kind of reg sequence?
                            // If so, make sure to override
                            // TargetInstrInfo::getRegSequenceLikeInputs.
  bit isPseudo     = 0;     // Is this instruction a pseudo-instruction?
                            // If so, won't have encoding information for
                            // the [MC]CodeEmitter stuff.
  bit isExtractSubreg = 0;  // Is this instruction a kind of extract subreg?
                             // If so, make sure to override
                             // TargetInstrInfo::getExtractSubregLikeInputs.
  bit isInsertSubreg = 0;   // Is this instruction a kind of insert subreg?
                            // If so, make sure to override
                            // TargetInstrInfo::getInsertSubregLikeInputs.
  bit variadicOpsAreDefs = 0; // Are variadic operands definitions?

  // Does the instruction have side effects that are not captured by any
  // operands of the instruction or other flags?
  bit hasSideEffects = ?;

  // Is this instruction a "real" instruction (with a distinct machine
  // encoding), or is it a pseudo instruction used for codegen modeling
  // purposes.
  // FIXME: For now this is distinct from isPseudo, above, as code-gen-only
  // instructions can (and often do) still have encoding information
  // associated with them. Once we've migrated all of them over to true
  // pseudo-instructions that are lowered to real instructions prior to
  // the printer/emitter, we can remove this attribute and just use isPseudo.
  //
  // The intended use is:
  // isPseudo: Does not have encoding information and should be expanded,
  //   at the latest, during lowering to MCInst.
  //
  // isCodeGenOnly: Does have encoding information and can go through to the
  //   CodeEmitter unchanged, but duplicates a canonical instruction
  //   definition's encoding and should be ignored when constructing the
  //   assembler match tables.
  bit isCodeGenOnly = 0;

  // Is this instruction a pseudo instruction for use by the assembler parser.
  bit isAsmParserOnly = 0;

  // This instruction is not expected to be queried for scheduling latencies
  // and therefore needs no scheduling information even for a complete
  // scheduling model.
  bit hasNoSchedulingInfo = 0;

  InstrItinClass Itinerary = NoItinerary;// Execution steps used for scheduling.

  // Scheduling information from TargetSchedule.td.
  list<SchedReadWrite> SchedRW;

  string Constraints = "";  // OperandConstraint, e.g. $src = $dst.

  /// DisableEncoding - List of operand names (e.g. "$op1,$op2") that should not
  /// be encoded into the output machineinstr.
  string DisableEncoding = "";

  string PostEncoderMethod = "";
  string DecoderMethod = "";

  // Is the instruction decoder method able to completely determine if the
  // given instruction is valid or not. If the TableGen definition of the
  // instruction specifies bitpattern A??B where A and B are static bits, the
  // hasCompleteDecoder flag says whether the decoder method fully handles the
  // ?? space, i.e. if it is a final arbiter for the instruction validity.
  // If not then the decoder attempts to continue decoding when the decoder
  // method fails.
  //
  // This allows to handle situations where the encoding is not fully
  // orthogonal. Example:
  // * InstA with bitpattern 0b0000????,
  // * InstB with bitpattern 0b000000?? but the associated decoder method
  //   DecodeInstB() returns Fail when ?? is 0b00 or 0b11.
  //
  // The decoder tries to decode a bitpattern that matches both InstA and
  // InstB bitpatterns first as InstB (because it is the most specific
  // encoding). In the default case (hasCompleteDecoder = 1), when
  // DecodeInstB() returns Fail the bitpattern gets rejected. By setting
  // hasCompleteDecoder = 0 in InstB, the decoder is informed that
  // DecodeInstB() is not able to determine if all possible values of ?? are
  // valid or not. If DecodeInstB() returns Fail the decoder will attempt to
  // decode the bitpattern as InstA too.
  bit hasCompleteDecoder = 1;

  /// Target-specific flags. This becomes the TSFlags field in TargetInstrDesc.
  bits<64> TSFlags = 0;

  ///@name Assembler Parser Support
  ///@{

  string AsmMatchConverter = "";

  /// TwoOperandAliasConstraint - Enable TableGen to auto-generate a
  /// two-operand matcher inst-alias for a three operand instruction.
  /// For example, the arm instruction "add r3, r3, r5" can be written
  /// as "add r3, r5". The constraint is of the same form as a tied-operand
  /// constraint. For example, "$Rn = $Rd".
  string TwoOperandAliasConstraint = "";

  /// Assembler variant name to use for this instruction. If specified then
  /// instruction will be presented only in MatchTable for this variant. If
  /// not specified then assembler variants will be determined based on
  /// AsmString
  string AsmVariantName = "";

  ///@}

  /// UseNamedOperandTable - If set, the operand indices of this instruction
  /// can be queried via the getNamedOperandIdx() function which is generated
  /// by TableGen.
  bit UseNamedOperandTable = 0;

  /// Should FastISel ignore this instruction. For certain ISAs, they have
  /// instructions which map to the same ISD Opcode, value type operands and
  /// instruction selection predicates. FastISel cannot handle such cases, but
  /// SelectionDAG can.
  bit FastISelShouldIgnore = 0;
}

/// PseudoInstExpansion - Expansion information for a pseudo-instruction.
/// Which instruction it expands to and how the operands map from the
/// pseudo.
class PseudoInstExpansion<dag Result> {
  dag ResultInst = Result;     // The instruction to generate.
  bit isPseudo = 1;
}

/// Predicates - These are extra conditionals which are turned into instruction
/// selector matching code. Currently each predicate is just a string.
class Predicate<string cond> {
  string CondString = cond;

  /// AssemblerMatcherPredicate - If this feature can be used by the assembler
  /// matcher, this is true.  Targets should set this by inheriting their
  /// feature from the AssemblerPredicate class in addition to Predicate.
  bit AssemblerMatcherPredicate = 0;

  /// AssemblerCondString - Name of the subtarget feature being tested used
  /// as alternative condition string used for assembler matcher.
  /// e.g. "ModeThumb" is translated to "(Bits & ModeThumb) != 0".
  ///      "!ModeThumb" is translated to "(Bits & ModeThumb) == 0".
  /// It can also list multiple features separated by ",".
  /// e.g. "ModeThumb,FeatureThumb2" is translated to
  ///      "(Bits & ModeThumb) != 0 && (Bits & FeatureThumb2) != 0".
  string AssemblerCondString = "";

  /// PredicateName - User-level name to use for the predicate. Mainly for use
  /// in diagnostics such as missing feature errors in the asm matcher.
  string PredicateName = "";

  /// Setting this to '1' indicates that the predicate must be recomputed on
  /// every function change. Most predicates can leave this at '0'.
  ///
  /// Ignored by SelectionDAG, it always recomputes the predicate on every use.
  bit RecomputePerFunction = 0;
}

/// NoHonorSignDependentRounding - This predicate is true if support for
/// sign-dependent-rounding is not enabled.
def NoHonorSignDependentRounding
 : Predicate<"!TM.Options.HonorSignDependentRoundingFPMath()">;

class Requires<list<Predicate> preds> {
  list<Predicate> Predicates = preds;
}

/// ops definition - This is just a simple marker used to identify the operand
/// list for an instruction. outs and ins are identical both syntactically and
/// semantically; they are used to define def operands and use operands to
/// improve readibility. This should be used like this:
///     (outs R32:$dst), (ins R32:$src1, R32:$src2) or something similar.
def ops;
def outs;
def ins;

/// variable_ops definition - Mark this instruction as taking a variable number
/// of operands.
def variable_ops;


/// PointerLikeRegClass - Values that are designed to have pointer width are
/// derived from this.  TableGen treats the register class as having a symbolic
/// type that it doesn't know, and resolves the actual regclass to use by using
/// the TargetRegisterInfo::getPointerRegClass() hook at codegen time.
class PointerLikeRegClass<int Kind> {
  int RegClassKind = Kind;
}


/// ptr_rc definition - Mark this operand as being a pointer value whose
/// register class is resolved dynamically via a callback to TargetInstrInfo.
/// FIXME: We should probably change this to a class which contain a list of
/// flags. But currently we have but one flag.
def ptr_rc : PointerLikeRegClass<0>;

/// unknown definition - Mark this operand as being of unknown type, causing
/// it to be resolved by inference in the context it is used.
class unknown_class;
def unknown : unknown_class;

/// AsmOperandClass - Representation for the kinds of operands which the target
/// specific parser can create and the assembly matcher may need to distinguish.
///
/// Operand classes are used to define the order in which instructions are
/// matched, to ensure that the instruction which gets matched for any
/// particular list of operands is deterministic.
///
/// The target specific parser must be able to classify a parsed operand into a
/// unique class which does not partially overlap with any other classes. It can
/// match a subset of some other class, in which case the super class field
/// should be defined.
class AsmOperandClass {
  /// The name to use for this class, which should be usable as an enum value.
  string Name = ?;

  /// The super classes of this operand.
  list<AsmOperandClass> SuperClasses = [];

  /// The name of the method on the target specific operand to call to test
  /// whether the operand is an instance of this class. If not set, this will
  /// default to "isFoo", where Foo is the AsmOperandClass name. The method
  /// signature should be:
  ///   bool isFoo() const;
  string PredicateMethod = ?;

  /// The name of the method on the target specific operand to call to add the
  /// target specific operand to an MCInst. If not set, this will default to
  /// "addFooOperands", where Foo is the AsmOperandClass name. The method
  /// signature should be:
  ///   void addFooOperands(MCInst &Inst, unsigned N) const;
  string RenderMethod = ?;

  /// The name of the method on the target specific operand to call to custom
  /// handle the operand parsing. This is useful when the operands do not relate
  /// to immediates or registers and are very instruction specific (as flags to
  /// set in a processor register, coprocessor number, ...).
  string ParserMethod = ?;

  // The diagnostic type to present when referencing this operand in a
  // match failure error message. By default, use a generic "invalid operand"
  // diagnostic. The target AsmParser maps these codes to text.
  string DiagnosticType = "";

  /// A diagnostic message to emit when an invalid value is provided for this
  /// operand.
  string DiagnosticString = "";

  /// Set to 1 if this operand is optional and not always required. Typically,
  /// the AsmParser will emit an error when it finishes parsing an
  /// instruction if it hasn't matched all the operands yet.  However, this
  /// error will be suppressed if all of the remaining unmatched operands are
  /// marked as IsOptional.
  ///
  /// Optional arguments must be at the end of the operand list.
  bit IsOptional = 0;

  /// The name of the method on the target specific asm parser that returns the
  /// default operand for this optional operand. This method is only used if
  /// IsOptional == 1. If not set, this will default to "defaultFooOperands",
  /// where Foo is the AsmOperandClass name. The method signature should be:
  ///   std::unique_ptr<MCParsedAsmOperand> defaultFooOperands() const;
  string DefaultMethod = ?;
}

def ImmAsmOperand : AsmOperandClass {
  let Name = "Imm";
}

/// Operand Types - These provide the built-in operand types that may be used
/// by a target.  Targets can optionally provide their own operand types as
/// needed, though this should not be needed for RISC targets.
class Operand<ValueType ty> : DAGOperand {
  ValueType Type = ty;
  string PrintMethod = "printOperand";
  string EncoderMethod = "";
  bit hasCompleteDecoder = 1;
  string OperandType = "OPERAND_UNKNOWN";
  dag MIOperandInfo = (ops);

  // MCOperandPredicate - Optionally, a code fragment operating on
  // const MCOperand &MCOp, and returning a bool, to indicate if
  // the value of MCOp is valid for the specific subclass of Operand
  code MCOperandPredicate;

  // ParserMatchClass - The "match class" that operands of this type fit
  // in. Match classes are used to define the order in which instructions are
  // match, to ensure that which instructions gets matched is deterministic.
  //
  // The target specific parser must be able to classify an parsed operand into
  // a unique class, which does not partially overlap with any other classes. It
  // can match a subset of some other class, in which case the AsmOperandClass
  // should declare the other operand as one of its super classes.
  AsmOperandClass ParserMatchClass = ImmAsmOperand;
}

class RegisterOperand<RegisterClass regclass, string pm = "printOperand">
  : DAGOperand {
  // RegClass - The register class of the operand.
  RegisterClass RegClass = regclass;
  // PrintMethod - The target method to call to print register operands of
  // this type. The method normally will just use an alt-name index to look
  // up the name to print. Default to the generic printOperand().
  string PrintMethod = pm;

  // EncoderMethod - The target method name to call to encode this register
  // operand.
  string EncoderMethod = "";

  // ParserMatchClass - The "match class" that operands of this type fit
  // in. Match classes are used to define the order in which instructions are
  // match, to ensure that which instructions gets matched is deterministic.
  //
  // The target specific parser must be able to classify an parsed operand into
  // a unique class, which does not partially overlap with any other classes. It
  // can match a subset of some other class, in which case the AsmOperandClass
  // should declare the other operand as one of its super classes.
  AsmOperandClass ParserMatchClass;

  string OperandType = "OPERAND_REGISTER";

  // When referenced in the result of a CodeGen pattern, GlobalISel will
  // normally copy the matched operand to the result. When this is set, it will
  // emit a special copy that will replace zero-immediates with the specified
  // zero-register.
  Register GIZeroRegister = ?;
}

let OperandType = "OPERAND_IMMEDIATE" in {
def i1imm  : Operand<i1>;
def i8imm  : Operand<i8>;
def i16imm : Operand<i16>;
def i32imm : Operand<i32>;
def i64imm : Operand<i64>;

def f32imm : Operand<f32>;
def f64imm : Operand<f64>;
}

// Register operands for generic instructions don't have an MVT, but do have
// constraints linking the operands (e.g. all operands of a G_ADD must
// have the same LLT).
class TypedOperand<string Ty> : Operand<untyped> {
  let OperandType = Ty;
  bit IsPointer = 0;
}

def type0 : TypedOperand<"OPERAND_GENERIC_0">;
def type1 : TypedOperand<"OPERAND_GENERIC_1">;
def type2 : TypedOperand<"OPERAND_GENERIC_2">;
def type3 : TypedOperand<"OPERAND_GENERIC_3">;
def type4 : TypedOperand<"OPERAND_GENERIC_4">;
def type5 : TypedOperand<"OPERAND_GENERIC_5">;

let IsPointer = 1 in {
  def ptype0 : TypedOperand<"OPERAND_GENERIC_0">;
  def ptype1 : TypedOperand<"OPERAND_GENERIC_1">;
  def ptype2 : TypedOperand<"OPERAND_GENERIC_2">;
  def ptype3 : TypedOperand<"OPERAND_GENERIC_3">;
  def ptype4 : TypedOperand<"OPERAND_GENERIC_4">;
  def ptype5 : TypedOperand<"OPERAND_GENERIC_5">;
}

/// zero_reg definition - Special node to stand for the zero register.
///
def zero_reg;

/// All operands which the MC layer classifies as predicates should inherit from
/// this class in some manner. This is already handled for the most commonly
/// used PredicateOperand, but may be useful in other circumstances.
class PredicateOp;

/// OperandWithDefaultOps - This Operand class can be used as the parent class
/// for an Operand that needs to be initialized with a default value if
/// no value is supplied in a pattern.  This class can be used to simplify the
/// pattern definitions for instructions that have target specific flags
/// encoded as immediate operands.
class OperandWithDefaultOps<ValueType ty, dag defaultops>
  : Operand<ty> {
  dag DefaultOps = defaultops;
}

/// PredicateOperand - This can be used to define a predicate operand for an
/// instruction.  OpTypes specifies the MIOperandInfo for the operand, and
/// AlwaysVal specifies the value of this predicate when set to "always
/// execute".
class PredicateOperand<ValueType ty, dag OpTypes, dag AlwaysVal>
  : OperandWithDefaultOps<ty, AlwaysVal>, PredicateOp {
  let MIOperandInfo = OpTypes;
}

/// OptionalDefOperand - This is used to define a optional definition operand
/// for an instruction. DefaultOps is the register the operand represents if
/// none is supplied, e.g. zero_reg.
class OptionalDefOperand<ValueType ty, dag OpTypes, dag defaultops>
  : OperandWithDefaultOps<ty, defaultops> {
  let MIOperandInfo = OpTypes;
}


// InstrInfo - This class should only be instantiated once to provide parameters
// which are global to the target machine.
//
class InstrInfo {
  // Target can specify its instructions in either big or little-endian formats.
  // For instance, while both Sparc and PowerPC are big-endian platforms, the
  // Sparc manual specifies its instructions in the format [31..0] (big), while
  // PowerPC specifies them using the format [0..31] (little).
  bit isLittleEndianEncoding = 0;

  // The instruction properties mayLoad, mayStore, and hasSideEffects are unset
  // by default, and TableGen will infer their value from the instruction
  // pattern when possible.
  //
  // Normally, TableGen will issue an error it it can't infer the value of a
  // property that hasn't been set explicitly. When guessInstructionProperties
  // is set, it will guess a safe value instead.
  //
  // This option is a temporary migration help. It will go away.
  bit guessInstructionProperties = 1;

  // TableGen's instruction encoder generator has support for matching operands
  // to bit-field variables both by name and by position. While matching by
  // name is preferred, this is currently not possible for complex operands,
  // and some targets still reply on the positional encoding rules. When
  // generating a decoder for such targets, the positional encoding rules must
  // be used by the decoder generator as well.
  //
  // This option is temporary; it will go away once the TableGen decoder
  // generator has better support for complex operands and targets have
  // migrated away from using positionally encoded operands.
  bit decodePositionallyEncodedOperands = 0;

  // When set, this indicates that there will be no overlap between those
  // operands that are matched by ordering (positional operands) and those
  // matched by name.
  //
  // This option is temporary; it will go away once the TableGen decoder
  // generator has better support for complex operands and targets have
  // migrated away from using positionally encoded operands.
  bit noNamedPositionallyEncodedOperands = 0;
}

// Standard Pseudo Instructions.
// This list must match TargetOpcodes.h and CodeGenTarget.cpp.
// Only these instructions are allowed in the TargetOpcode namespace.
// Ensure mayLoad and mayStore have a default value, so as not to break
// targets that set guessInstructionProperties=0. Any local definition of
// mayLoad/mayStore takes precedence over these default values.
class StandardPseudoInstruction : Instruction {
  let mayLoad = 0;
  let mayStore = 0;
  let isCodeGenOnly = 1;
  let isPseudo = 1;
  let hasNoSchedulingInfo = 1;
  let Namespace = "TargetOpcode";
}
def PHI : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins variable_ops);
  let AsmString = "PHINODE";
  let hasSideEffects = 0;
}
def INLINEASM : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  let hasSideEffects = 0;  // Note side effect is encoded in an operand.
}
def CFI_INSTRUCTION : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = 1;
  let hasSideEffects = 0;
  let isNotDuplicable = 1;
}
def EH_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = 1;
  let hasSideEffects = 0;
  let isNotDuplicable = 1;
}
def GC_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = 1;
  let hasSideEffects = 0;
  let isNotDuplicable = 1;
}
def ANNOTATION_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = 1;
  let hasSideEffects = 0;
  let isNotDuplicable = 1;
}
def KILL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  let hasSideEffects = 0;
}
def EXTRACT_SUBREG : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$supersrc, i32imm:$subidx);
  let AsmString = "";
  let hasSideEffects = 0;
}
def INSERT_SUBREG : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$supersrc, unknown:$subsrc, i32imm:$subidx);
  let AsmString = "";
  let hasSideEffects = 0;
  let Constraints = "$supersrc = $dst";
}
def IMPLICIT_DEF : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins);
  let AsmString = "";
  let hasSideEffects = 0;
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
}
def SUBREG_TO_REG : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$implsrc, unknown:$subsrc, i32imm:$subidx);
  let AsmString = "";
  let hasSideEffects = 0;
}
def COPY_TO_REGCLASS : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$src, i32imm:$regclass);
  let AsmString = "";
  let hasSideEffects = 0;
  let isAsCheapAsAMove = 1;
}
def DBG_VALUE : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "DBG_VALUE";
  let hasSideEffects = 0;
}
def DBG_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$label);
  let AsmString = "DBG_LABEL";
  let hasSideEffects = 0;
}
def REG_SEQUENCE : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$supersrc, variable_ops);
  let AsmString = "";
  let hasSideEffects = 0;
  let isAsCheapAsAMove = 1;
}
def COPY : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$src);
  let AsmString = "";
  let hasSideEffects = 0;
  let isAsCheapAsAMove = 1;
  let hasNoSchedulingInfo = 0;
}
def BUNDLE : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "BUNDLE";
  let hasSideEffects = 1;
}
def LIFETIME_START : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "LIFETIME_START";
  let hasSideEffects = 0;
}
def LIFETIME_END : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "LIFETIME_END";
  let hasSideEffects = 0;
}
def STACKMAP : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i64imm:$id, i32imm:$nbytes, variable_ops);
  let hasSideEffects = 1;
  let isCall = 1;
  let mayLoad = 1;
  let usesCustomInserter = 1;
}
def PATCHPOINT : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins i64imm:$id, i32imm:$nbytes, unknown:$callee,
                       i32imm:$nargs, i32imm:$cc, variable_ops);
  let hasSideEffects = 1;
  let isCall = 1;
  let mayLoad = 1;
  let usesCustomInserter = 1;
}
def STATEPOINT : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let usesCustomInserter = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 1;
  let isCall = 1;
}
def LOAD_STACK_GUARD : StandardPseudoInstruction {
  let OutOperandList = (outs ptr_rc:$dst);
  let InOperandList = (ins);
  let mayLoad = 1;
  bit isReMaterializable = 1;
  let hasSideEffects = 0;
  bit isPseudo = 1;
}
def LOCAL_ESCAPE : StandardPseudoInstruction {
  // This instruction is really just a label. It has to be part of the chain so
  // that it doesn't get dropped from the DAG, but it produces nothing and has
  // no side effects.
  let OutOperandList = (outs);
  let InOperandList = (ins ptr_rc:$symbol, i32imm:$id);
  let hasSideEffects = 0;
  let hasCtrlDep = 1;
}
def FAULTING_OP : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins variable_ops);
  let usesCustomInserter = 1;
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let isTerminator = 1;
  let isBranch = 1;
}
def PATCHABLE_OP : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let usesCustomInserter = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 1;
}
def PATCHABLE_FUNCTION_ENTER : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "# XRay Function Enter.";
  let usesCustomInserter = 1;
  let hasSideEffects = 0;
}
def PATCHABLE_RET : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "# XRay Function Patchable RET.";
  let usesCustomInserter = 1;
  let hasSideEffects = 1;
  let isTerminator = 1;
  let isReturn = 1;
}
def PATCHABLE_FUNCTION_EXIT : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "# XRay Function Exit.";
  let usesCustomInserter = 1;
  let hasSideEffects = 0; // FIXME: is this correct?
  let isReturn = 0; // Original return instruction will follow
}
def PATCHABLE_TAIL_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "# XRay Tail Call Exit.";
  let usesCustomInserter = 1;
  let hasSideEffects = 1;
  let isReturn = 1;
}
def PATCHABLE_EVENT_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptr_rc:$event, i8imm:$size);
  let AsmString = "# XRay Custom Event Log.";
  let usesCustomInserter = 1;
  let isCall = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 1;
}
def PATCHABLE_TYPED_EVENT_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i16imm:$type, ptr_rc:$event, i32imm:$size);
  let AsmString = "# XRay Typed Event Log.";
  let usesCustomInserter = 1;
  let isCall = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 1;
}
def FENTRY_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "# FEntry call";
  let usesCustomInserter = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let hasSideEffects = 1;
}
def ICALL_BRANCH_FUNNEL : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  let hasSideEffects = 1;
}

// Generic opcodes used in GlobalISel.
include "llvm/Target/GenericOpcodes.td"

//===----------------------------------------------------------------------===//
// AsmParser - This class can be implemented by targets that wish to implement
// .s file parsing.
//
// Subtargets can have multiple different assembly parsers (e.g. AT&T vs Intel
// syntax on X86 for example).
//
class AsmParser {
  // AsmParserClassName - This specifies the suffix to use for the asmparser
  // class.  Generated AsmParser classes are always prefixed with the target
  // name.
  string AsmParserClassName  = "AsmParser";

  // AsmParserInstCleanup - If non-empty, this is the name of a custom member
  // function of the AsmParser class to call on every matched instruction.
  // This can be used to perform target specific instruction post-processing.
  string AsmParserInstCleanup  = "";

  // ShouldEmitMatchRegisterName - Set to false if the target needs a hand
  // written register name matcher
  bit ShouldEmitMatchRegisterName = 1;

  // Set to true if the target needs a generated 'alternative register name'
  // matcher.
  //
  // This generates a function which can be used to lookup registers from
  // their aliases. This function will fail when called on targets where
  // several registers share the same alias (i.e. not a 1:1 mapping).
  bit ShouldEmitMatchRegisterAltName = 0;

  // Set to true if MatchRegisterName and MatchRegisterAltName functions
  // should be generated even if there are duplicate register names. The
  // target is responsible for coercing aliased registers as necessary
  // (e.g. in validateTargetOperandClass), and there are no guarantees about
  // which numeric register identifier will be returned in the case of
  // multiple matches.
  bit AllowDuplicateRegisterNames = 0;

  // HasMnemonicFirst - Set to false if target instructions don't always
  // start with a mnemonic as the first token.
  bit HasMnemonicFirst = 1;

  // ReportMultipleNearMisses -
  // When 0, the assembly matcher reports an error for one encoding or operand
  // that did not match the parsed instruction.
  // When 1, the assmebly matcher returns a list of encodings that were close
  // to matching the parsed instruction, so to allow more detailed error
  // messages.
  bit ReportMultipleNearMisses = 0;
}
def DefaultAsmParser : AsmParser;

//===----------------------------------------------------------------------===//
// AsmParserVariant - Subtargets can have multiple different assembly parsers
// (e.g. AT&T vs Intel syntax on X86 for example). This class can be
// implemented by targets to describe such variants.
//
class AsmParserVariant {
  // Variant - AsmParsers can be of multiple different variants.  Variants are
  // used to support targets that need to parser multiple formats for the
  // assembly language.
  int Variant = 0;

  // Name - The AsmParser variant name (e.g., AT&T vs Intel).
  string Name = "";

  // CommentDelimiter - If given, the delimiter string used to recognize
  // comments which are hard coded in the .td assembler strings for individual
  // instructions.
  string CommentDelimiter = "";

  // RegisterPrefix - If given, the token prefix which indicates a register
  // token. This is used by the matcher to automatically recognize hard coded
  // register tokens as constrained registers, instead of tokens, for the
  // purposes of matching.
  string RegisterPrefix = "";

  // TokenizingCharacters - Characters that are standalone tokens
  string TokenizingCharacters = "[]*!";

  // SeparatorCharacters - Characters that are not tokens
  string SeparatorCharacters = " \t,";

  // BreakCharacters - Characters that start new identifiers
  string BreakCharacters = "";
}
def DefaultAsmParserVariant : AsmParserVariant;

/// AssemblerPredicate - This is a Predicate that can be used when the assembler
/// matches instructions and aliases.
class AssemblerPredicate<string cond, string name = ""> {
  bit AssemblerMatcherPredicate = 1;
  string AssemblerCondString = cond;
  string PredicateName = name;
}

/// TokenAlias - This class allows targets to define assembler token
/// operand aliases. That is, a token literal operand which is equivalent
/// to another, canonical, token literal. For example, ARM allows:
///   vmov.u32 s4, #0  -> vmov.i32, #0
/// 'u32' is a more specific designator for the 32-bit integer type specifier
/// and is legal for any instruction which accepts 'i32' as a datatype suffix.
///   def : TokenAlias<".u32", ".i32">;
///
/// This works by marking the match class of 'From' as a subclass of the
/// match class of 'To'.
class TokenAlias<string From, string To> {
  string FromToken = From;
  string ToToken = To;
}

/// MnemonicAlias - This class allows targets to define assembler mnemonic
/// aliases.  This should be used when all forms of one mnemonic are accepted
/// with a different mnemonic.  For example, X86 allows:
///   sal %al, 1    -> shl %al, 1
///   sal %ax, %cl  -> shl %ax, %cl
///   sal %eax, %cl -> shl %eax, %cl
/// etc.  Though "sal" is accepted with many forms, all of them are directly
/// translated to a shl, so it can be handled with (in the case of X86, it
/// actually has one for each suffix as well):
///   def : MnemonicAlias<"sal", "shl">;
///
/// Mnemonic aliases are mapped before any other translation in the match phase,
/// and do allow Requires predicates, e.g.:
///
///  def : MnemonicAlias<"pushf", "pushfq">, Requires<[In64BitMode]>;
///  def : MnemonicAlias<"pushf", "pushfl">, Requires<[In32BitMode]>;
///
/// Mnemonic aliases can also be constrained to specific variants, e.g.:
///
///  def : MnemonicAlias<"pushf", "pushfq", "att">, Requires<[In64BitMode]>;
///
/// If no variant (e.g., "att" or "intel") is specified then the alias is
/// applied unconditionally.
class MnemonicAlias<string From, string To, string VariantName = ""> {
  string FromMnemonic = From;
  string ToMnemonic = To;
  string AsmVariantName = VariantName;

  // Predicates - Predicates that must be true for this remapping to happen.
  list<Predicate> Predicates = [];
}

/// InstAlias - This defines an alternate assembly syntax that is allowed to
/// match an instruction that has a different (more canonical) assembly
/// representation.
class InstAlias<string Asm, dag Result, int Emit = 1, string VariantName = ""> {
  string AsmString = Asm;      // The .s format to match the instruction with.
  dag ResultInst = Result;     // The MCInst to generate.

  // This determines which order the InstPrinter detects aliases for
  // printing. A larger value makes the alias more likely to be
  // emitted. The Instruction's own definition is notionally 0.5, so 0
  // disables printing and 1 enables it if there are no conflicting aliases.
  int EmitPriority = Emit;

  // Predicates - Predicates that must be true for this to match.
  list<Predicate> Predicates = [];

  // If the instruction specified in Result has defined an AsmMatchConverter
  // then setting this to 1 will cause the alias to use the AsmMatchConverter
  // function when converting the OperandVector into an MCInst instead of the
  // function that is generated by the dag Result.
  // Setting this to 0 will cause the alias to ignore the Result instruction's
  // defined AsmMatchConverter and instead use the function generated by the
  // dag Result.
  bit UseInstAsmMatchConverter = 1;

  // Assembler variant name to use for this alias. If not specified then
  // assembler variants will be determined based on AsmString
  string AsmVariantName = VariantName;
}

//===----------------------------------------------------------------------===//
// AsmWriter - This class can be implemented by targets that need to customize
// the format of the .s file writer.
//
// Subtargets can have multiple different asmwriters (e.g. AT&T vs Intel syntax
// on X86 for example).
//
class AsmWriter {
  // AsmWriterClassName - This specifies the suffix to use for the asmwriter
  // class.  Generated AsmWriter classes are always prefixed with the target
  // name.
  string AsmWriterClassName  = "InstPrinter";

  // PassSubtarget - Determines whether MCSubtargetInfo should be passed to
  // the various print methods.
  // FIXME: Remove after all ports are updated.
  int PassSubtarget = 0;

  // Variant - AsmWriters can be of multiple different variants.  Variants are
  // used to support targets that need to emit assembly code in ways that are
  // mostly the same for different targets, but have minor differences in
  // syntax.  If the asmstring contains {|} characters in them, this integer
  // will specify which alternative to use.  For example "{x|y|z}" with Variant
  // == 1, will expand to "y".
  int Variant = 0;
}
def DefaultAsmWriter : AsmWriter;


//===----------------------------------------------------------------------===//
// Target - This class contains the "global" target information
//
class Target {
  // InstructionSet - Instruction set description for this target.
  InstrInfo InstructionSet;

  // AssemblyParsers - The AsmParser instances available for this target.
  list<AsmParser> AssemblyParsers = [DefaultAsmParser];

  /// AssemblyParserVariants - The AsmParserVariant instances available for
  /// this target.
  list<AsmParserVariant> AssemblyParserVariants = [DefaultAsmParserVariant];

  // AssemblyWriters - The AsmWriter instances available for this target.
  list<AsmWriter> AssemblyWriters = [DefaultAsmWriter];

  // AllowRegisterRenaming - Controls whether this target allows
  // post-register-allocation renaming of registers.  This is done by
  // setting hasExtraDefRegAllocReq and hasExtraSrcRegAllocReq to 1
  // for all opcodes if this flag is set to 0.
  int AllowRegisterRenaming = 0;
}

//===----------------------------------------------------------------------===//
// SubtargetFeature - A characteristic of the chip set.
//
class SubtargetFeature<string n, string a,  string v, string d,
                       list<SubtargetFeature> i = []> {
  // Name - Feature name.  Used by command line (-mattr=) to determine the
  // appropriate target chip.
  //
  string Name = n;

  // Attribute - Attribute to be set by feature.
  //
  string Attribute = a;

  // Value - Value the attribute to be set to by feature.
  //
  string Value = v;

  // Desc - Feature description.  Used by command line (-mattr=) to display help
  // information.
  //
  string Desc = d;

  // Implies - Features that this feature implies are present. If one of those
  // features isn't set, then this one shouldn't be set either.
  //
  list<SubtargetFeature> Implies = i;
}

/// Specifies a Subtarget feature that this instruction is deprecated on.
class Deprecated<SubtargetFeature dep> {
  SubtargetFeature DeprecatedFeatureMask = dep;
}

/// A custom predicate used to determine if an instruction is
/// deprecated or not.
class ComplexDeprecationPredicate<string dep> {
  string ComplexDeprecationPredicate = dep;
}

//===----------------------------------------------------------------------===//
// Processor chip sets - These values represent each of the chip sets supported
// by the scheduler.  Each Processor definition requires corresponding
// instruction itineraries.
//
class Processor<string n, ProcessorItineraries pi, list<SubtargetFeature> f> {
  // Name - Chip set name.  Used by command line (-mcpu=) to determine the
  // appropriate target chip.
  //
  string Name = n;

  // SchedModel - The machine model for scheduling and instruction cost.
  //
  SchedMachineModel SchedModel = NoSchedModel;

  // ProcItin - The scheduling information for the target processor.
  //
  ProcessorItineraries ProcItin = pi;

  // Features - list of
  list<SubtargetFeature> Features = f;
}

// ProcessorModel allows subtargets to specify the more general
// SchedMachineModel instead if a ProcessorItinerary. Subtargets will
// gradually move to this newer form.
//
// Although this class always passes NoItineraries to the Processor
// class, the SchedMachineModel may still define valid Itineraries.
class ProcessorModel<string n, SchedMachineModel m, list<SubtargetFeature> f>
  : Processor<n, NoItineraries, f> {
  let SchedModel = m;
}

//===----------------------------------------------------------------------===//
// InstrMapping - This class is used to create mapping tables to relate
// instructions with each other based on the values specified in RowFields,
// ColFields, KeyCol and ValueCols.
//
class InstrMapping {
  // FilterClass - Used to limit search space only to the instructions that
  // define the relationship modeled by this InstrMapping record.
  string FilterClass;

  // RowFields - List of fields/attributes that should be same for all the
  // instructions in a row of the relation table. Think of this as a set of
  // properties shared by all the instructions related by this relationship
  // model and is used to categorize instructions into subgroups. For instance,
  // if we want to define a relation that maps 'Add' instruction to its
  // predicated forms, we can define RowFields like this:
  //
  // let RowFields = BaseOp
  // All add instruction predicated/non-predicated will have to set their BaseOp
  // to the same value.
  //
  // def Add: { let BaseOp = 'ADD'; let predSense = 'nopred' }
  // def Add_predtrue: { let BaseOp = 'ADD'; let predSense = 'true' }
  // def Add_predfalse: { let BaseOp = 'ADD'; let predSense = 'false'  }
  list<string> RowFields = [];

  // List of fields/attributes that are same for all the instructions
  // in a column of the relation table.
  // Ex: let ColFields = 'predSense' -- It means that the columns are arranged
  // based on the 'predSense' values. All the instruction in a specific
  // column have the same value and it is fixed for the column according
  // to the values set in 'ValueCols'.
  list<string> ColFields = [];

  // Values for the fields/attributes listed in 'ColFields'.
  // Ex: let KeyCol = 'nopred' -- It means that the key instruction (instruction
  // that models this relation) should be non-predicated.
  // In the example above, 'Add' is the key instruction.
  list<string> KeyCol = [];

  // List of values for the fields/attributes listed in 'ColFields', one for
  // each column in the relation table.
  //
  // Ex: let ValueCols = [['true'],['false']] -- It adds two columns in the
  // table. First column requires all the instructions to have predSense
  // set to 'true' and second column requires it to be 'false'.
  list<list<string> > ValueCols = [];
}

//===----------------------------------------------------------------------===//
// Pull in the common support for calling conventions.
//
include "llvm/Target/TargetCallingConv.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for DAG isel generation.
//
include "llvm/Target/TargetSelectionDAG.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for Global ISel register bank info generation.
//
include "llvm/Target/GlobalISel/RegisterBank.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for DAG isel generation.
//
include "llvm/Target/GlobalISel/Target.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for the Global ISel DAG-based selector generation.
//
include "llvm/Target/GlobalISel/SelectionDAGCompat.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for Pfm Counters generation.
//
include "llvm/Target/TargetPfmCounters.td"
