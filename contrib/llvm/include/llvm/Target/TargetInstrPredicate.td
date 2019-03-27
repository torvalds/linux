//===- TargetInstrPredicate.td - ---------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines class MCInstPredicate and its subclasses.
//
// MCInstPredicate definitions are used by target scheduling models to describe
// constraints on instructions.
//
// Here is an example of an MCInstPredicate definition in tablegen:
//
// def MCInstPredicateExample : CheckAll<[
//    CheckOpcode<[BLR]>,
//    CheckIsRegOperand<0>,
//    CheckNot<CheckRegOperand<0, LR>>]>;
//
// The syntax for MCInstPredicate is declarative, and predicate definitions can
// be composed together in order to generate more complex constraints.
//
// The `CheckAll` from the example defines a composition of three different
// predicates.  Definition `MCInstPredicateExample` identifies instructions
// whose opcode is BLR, and whose first operand is a register different from
// register `LR`.
//
// Every MCInstPredicate class has a well-known semantic in tablegen. For
// example, `CheckOpcode` is a special type of predicate used to describe a
// constraint on the value of an instruction opcode.
//
// MCInstPredicate definitions are typically used by scheduling models to
// construct MCSchedPredicate definitions (see the definition of class
// MCSchedPredicate in llvm/Target/TargetSchedule.td).
// In particular, an MCSchedPredicate can be used instead of a SchedPredicate
// when defining the set of SchedReadVariant and SchedWriteVariant of a
// processor scheduling model.
//
// The `MCInstPredicateExample` definition above is equivalent (and therefore
// could replace) the following definition from a previous ExynosM3 model (see
// AArch64SchedExynosM3.td):
//
// def M3BranchLinkFastPred  : SchedPredicate<[{
//    MI->getOpcode() == AArch64::BLR &&
//    MI->getOperand(0).isReg() &&
//    MI->getOperand(0).getReg() != AArch64::LR}]>;
//
// The main advantage of using MCInstPredicate instead of SchedPredicate is
// portability: users don't need to specify predicates in C++. As a consequence
// of this, MCInstPredicate definitions are not bound to a particular
// representation (i.e. MachineInstr vs MCInst).
//
// Tablegen backends know how to expand MCInstPredicate definitions into actual
// C++ code that works on MachineInstr (and/or MCInst).
//
// Instances of class PredicateExpander (see utils/Tablegen/PredicateExpander.h)
// know how to expand a predicate. For each MCInstPredicate class, there must be
// an "expand" method available in the PredicateExpander interface.
//
// For example, a `CheckOpcode` predicate is expanded using method
// `PredicateExpander::expandCheckOpcode()`.
//
// New MCInstPredicate classes must be added to this file. For each new class
// XYZ, an "expandXYZ" method must be added to the PredicateExpander.
//
//===----------------------------------------------------------------------===//

// Forward declarations.
class Instruction;
class SchedMachineModel;

// A generic machine instruction predicate.
class MCInstPredicate;

class MCTrue  : MCInstPredicate;   // A predicate that always evaluates to True.
class MCFalse : MCInstPredicate;   // A predicate that always evaluates to False.
def TruePred  : MCTrue;
def FalsePred : MCFalse;

// A predicate used to negate the outcome of another predicate.
// It allows to easily express "set difference" operations. For example, it
// makes it easy to describe a check that tests if an opcode is not part of a
// set of opcodes.
class CheckNot<MCInstPredicate P> : MCInstPredicate {
  MCInstPredicate Pred = P;
}

// This class is used as a building block to define predicates on instruction
// operands. It is used to reference a specific machine operand.
class MCOperandPredicate<int Index> : MCInstPredicate {
  int OpIndex = Index;
}

// Return true if machine operand at position `Index` is a register operand.
class CheckIsRegOperand<int Index> : MCOperandPredicate<Index>;

// Return true if machine operand at position `Index` is an immediate operand.
class CheckIsImmOperand<int Index> : MCOperandPredicate<Index>;

// Check if machine operands at index `First` and index `Second` both reference
// the same register.
class CheckSameRegOperand<int First, int Second> : MCInstPredicate {
  int FirstIndex = First;
  int SecondIndex = Second;
}

// Base class for checks on register/immediate operands.
// It allows users to define checks like:
//    MyFunction(MI->getOperand(Index).getImm()) == Val;
//
// In the example above, `MyFunction` is a function that takes as input an
// immediate operand value, and returns another value. Field `FunctionMapper` is
// the name of the function to call on the operand value.
class CheckOperandBase<int Index, string Fn = ""> : MCOperandPredicate<Index> {
  string FunctionMapper = Fn;
}

// Check that the machine register operand at position `Index` references
// register R. This predicate assumes that we already checked that the machine
// operand at position `Index` is a register operand.
class CheckRegOperand<int Index, Register R> : CheckOperandBase<Index> {
  Register Reg = R;
}

// Check if register operand at index `Index` is the invalid register.
class CheckInvalidRegOperand<int Index> : CheckOperandBase<Index>;

// Check that the operand at position `Index` is immediate `Imm`.
// If field `FunctionMapper` is a non-empty string, then function
// `FunctionMapper` is applied to the operand value, and the return value is then
// compared against `Imm`.
class CheckImmOperand<int Index, int Imm> : CheckOperandBase<Index> {
  int ImmVal = Imm;
}

// Similar to CheckImmOperand, however the immediate is not a literal number.
// This is useful when we want to compare the value of an operand against an
// enum value, and we know the actual integer value of that enum.
class CheckImmOperand_s<int Index, string Value> : CheckOperandBase<Index> {
  string ImmVal = Value;
}

// Expands to a call to `FunctionMapper` if field `FunctionMapper` is set.
// Otherwise, it expands to a CheckNot<CheckInvalidRegOperand<Index>>.
class CheckRegOperandSimple<int Index> : CheckOperandBase<Index>;

// Expands to a call to `FunctionMapper` if field `FunctionMapper` is set.
// Otherwise, it simply evaluates to TruePred.
class CheckImmOperandSimple<int Index> : CheckOperandBase<Index>;

// Check that the operand at position `Index` is immediate value zero.
class CheckZeroOperand<int Index> : CheckImmOperand<Index, 0>;

// Check that the instruction has exactly `Num` operands.
class CheckNumOperands<int Num> : MCInstPredicate {
  int NumOps = Num;
}

// Check that the instruction opcode is one of the opcodes in set `Opcodes`.
// This is a simple set membership query. The easier way to check if an opcode
// is not a member of the set is by using a `CheckNot<CheckOpcode<[...]>>`
// sequence.
class CheckOpcode<list<Instruction> Opcodes> : MCInstPredicate {
  list<Instruction> ValidOpcodes = Opcodes;
}

// Check that the instruction opcode is a pseudo opcode member of the set
// `Opcodes`.  This check is always expanded to "false" if we are generating
// code for MCInst.
class CheckPseudo<list<Instruction> Opcodes> : CheckOpcode<Opcodes>;

// A non-portable predicate. Only to use as a last resort when a block of code
// cannot possibly be converted in a declarative way using other MCInstPredicate
// classes. This check is always expanded to "false" when generating code for
// MCInst.
class CheckNonPortable<string Code> : MCInstPredicate {
  string CodeBlock = Code;
}

// A sequence of predicates. It is used as the base class for CheckAll, and
// CheckAny. It allows to describe compositions of predicates.
class CheckPredicateSequence<list<MCInstPredicate> Preds> : MCInstPredicate {
  list<MCInstPredicate> Predicates = Preds;
}

// Check that all of the predicates in `Preds` evaluate to true.
class CheckAll<list<MCInstPredicate> Sequence>
    : CheckPredicateSequence<Sequence>;

// Check that at least one of the predicates in `Preds` evaluates to true.
class CheckAny<list<MCInstPredicate> Sequence>
    : CheckPredicateSequence<Sequence>;


// Used to expand the body of a function predicate. See the definition of
// TIIPredicate below.
class MCStatement;

// Expands to a return statement. The return expression is a boolean expression
// described by a MCInstPredicate.
class MCReturnStatement<MCInstPredicate predicate> : MCStatement {
  MCInstPredicate Pred = predicate;
}

// Used to automatically construct cases of a switch statement where the switch
// variable is an instruction opcode. There is a 'case' for every opcode in the
// `opcodes` list, and each case is associated with MCStatement `caseStmt`.
class MCOpcodeSwitchCase<list<Instruction> opcodes, MCStatement caseStmt> {
  list<Instruction> Opcodes = opcodes;
  MCStatement CaseStmt = caseStmt;
}

// Expands to a switch statement. The switch variable is an instruction opcode.
// The auto-generated switch is populated by a number of cases based on the
// `cases` list in input. A default case is automatically generated, and it
// evaluates to `default`.
class MCOpcodeSwitchStatement<list<MCOpcodeSwitchCase> cases,
                              MCStatement default> : MCStatement {
  list<MCOpcodeSwitchCase> Cases = cases;
  MCStatement DefaultCase = default;
}

// Base class for function predicates.
class FunctionPredicateBase<string name, MCStatement body> {
  string FunctionName = name;
  MCStatement Body = body;
}

// Check that a call to method `Name` in class "XXXInstrInfo" (where XXX is
// the name of a target) returns true.
//
// TIIPredicate definitions are used to model calls to the target-specific
// InstrInfo. A TIIPredicate is treated specially by the InstrInfoEmitter
// tablegen backend, which will use it to automatically generate a definition in
// the target specific `InstrInfo` class.
//
// There cannot be multiple TIIPredicate definitions with the same name for the
// same target.
class TIIPredicate<string Name, MCStatement body>
    : FunctionPredicateBase<Name, body>, MCInstPredicate;

// A function predicate that takes as input a machine instruction, and returns
// a boolean value.
//
// This predicate is expanded into a function call by the PredicateExpander.
// In particular, the PredicateExpander would either expand this predicate into
// a call to `MCInstFn`, or into a call to`MachineInstrFn` depending on whether
// it is lowering predicates for MCInst or MachineInstr.
//
// In this context, `MCInstFn` and `MachineInstrFn` are both function names.
class CheckFunctionPredicate<string MCInstFn, string MachineInstrFn> : MCInstPredicate {
  string MCInstFnName = MCInstFn;
  string MachineInstrFnName = MachineInstrFn;
}

// Used to classify machine instructions based on a machine instruction
// predicate.
//
// Let IC be an InstructionEquivalenceClass definition, and MI a machine
// instruction.  We say that MI belongs to the equivalence class described by IC
// if and only if the following two conditions are met:
//  a) MI's opcode is in the `opcodes` set, and
//  b) `Predicate` evaluates to true when applied to MI.
//
// Instances of this class can be used by processor scheduling models to
// describe instructions that have a property in common.  For example,
// InstructionEquivalenceClass definitions can be used to identify the set of
// dependency breaking instructions for a processor model.
//
// An (optional) list of operand indices can be used to further describe
// properties that apply to instruction operands. For example, it can be used to
// identify register uses of a dependency breaking instructions that are not in
// a RAW dependency.
class InstructionEquivalenceClass<list<Instruction> opcodes,
                                  MCInstPredicate pred,
                                  list<int> operands = []> {
  list<Instruction> Opcodes = opcodes;
  MCInstPredicate Predicate = pred;
  list<int> OperandIndices = operands;
}

// Used by processor models to describe dependency breaking instructions.
//
// This is mainly an alias for InstructionEquivalenceClass.  Input operand
// `BrokenDeps` identifies the set of "broken dependencies". There is one bit
// per each implicit and explicit input operand.  An empty set of broken
// dependencies means: "explicit input register operands are independent."
class DepBreakingClass<list<Instruction> opcodes, MCInstPredicate pred,
                       list<int> BrokenDeps = []>
    : InstructionEquivalenceClass<opcodes, pred, BrokenDeps>;

// A function descriptor used to describe the signature of a predicate methods
// which will be expanded by the STIPredicateExpander into a tablegen'd
// XXXGenSubtargetInfo class member definition (here, XXX is a target name).
//
// It describes the signature of a TargetSubtarget hook, as well as a few extra
// properties. Examples of extra properties are:
//  - The default return value for the auto-generate function hook.
//  - A list of subtarget hooks (Delegates) that are called from this function.
//
class STIPredicateDecl<string name, MCInstPredicate default = FalsePred,
                       bit overrides = 1, bit expandForMC = 1,
                       bit updatesOpcodeMask = 0,
                       list<STIPredicateDecl> delegates = []> {
  string Name = name;

  MCInstPredicate DefaultReturnValue = default;

  // True if this method is declared as virtual in class TargetSubtargetInfo.
  bit OverridesBaseClassMember = overrides;

  // True if we need an equivalent predicate function in the MC layer.
  bit ExpandForMC = expandForMC;

  // True if the autogenerated method has a extra in/out APInt param used as a
  // mask of operands.
  bit UpdatesOpcodeMask = updatesOpcodeMask;

  // A list of STIPredicates used by this definition to delegate part of the
  // computation. For example, STIPredicateFunction `isDependencyBreaking()`
  // delegates to `isZeroIdiom()` part of its computation.
  list<STIPredicateDecl> Delegates = delegates;
}

// A predicate function definition member of class `XXXGenSubtargetInfo`.
//
// If `Declaration.ExpandForMC` is true, then SubtargetEmitter
// will also expand another definition of this method that accepts a MCInst.
class STIPredicate<STIPredicateDecl declaration,
                   list<InstructionEquivalenceClass> classes> {
  STIPredicateDecl Declaration = declaration;
  list<InstructionEquivalenceClass> Classes = classes;
  SchedMachineModel SchedModel = ?;
}

// Convenience classes and definitions used by processor scheduling models to
// describe dependency breaking instructions and move elimination candidates.
let UpdatesOpcodeMask = 1 in {

def IsZeroIdiomDecl : STIPredicateDecl<"isZeroIdiom">;

let Delegates = [IsZeroIdiomDecl] in
def IsDepBreakingDecl : STIPredicateDecl<"isDependencyBreaking">;

} // UpdatesOpcodeMask

def IsOptimizableRegisterMoveDecl
    : STIPredicateDecl<"isOptimizableRegisterMove">;

class IsZeroIdiomFunction<list<DepBreakingClass> classes>
    : STIPredicate<IsZeroIdiomDecl, classes>;

class IsDepBreakingFunction<list<DepBreakingClass> classes>
    : STIPredicate<IsDepBreakingDecl, classes>;

class IsOptimizableRegisterMove<list<InstructionEquivalenceClass> classes>
    : STIPredicate<IsOptimizableRegisterMoveDecl, classes>;
