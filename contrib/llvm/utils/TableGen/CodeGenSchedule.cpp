//===- CodeGenSchedule.cpp - Scheduling MachineModels ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines structures to encapsulate the machine model as described in
// the target description.
//
//===----------------------------------------------------------------------===//

#include "CodeGenSchedule.h"
#include "CodeGenInstruction.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include <algorithm>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "subtarget-emitter"

#ifndef NDEBUG
static void dumpIdxVec(ArrayRef<unsigned> V) {
  for (unsigned Idx : V)
    dbgs() << Idx << ", ";
}
#endif

namespace {

// (instrs a, b, ...) Evaluate and union all arguments. Identical to AddOp.
struct InstrsOp : public SetTheory::Operator {
  void apply(SetTheory &ST, DagInit *Expr, SetTheory::RecSet &Elts,
             ArrayRef<SMLoc> Loc) override {
    ST.evaluate(Expr->arg_begin(), Expr->arg_end(), Elts, Loc);
  }
};

// (instregex "OpcPat",...) Find all instructions matching an opcode pattern.
struct InstRegexOp : public SetTheory::Operator {
  const CodeGenTarget &Target;
  InstRegexOp(const CodeGenTarget &t): Target(t) {}

  /// Remove any text inside of parentheses from S.
  static std::string removeParens(llvm::StringRef S) {
    std::string Result;
    unsigned Paren = 0;
    // NB: We don't care about escaped parens here.
    for (char C : S) {
      switch (C) {
      case '(':
        ++Paren;
        break;
      case ')':
        --Paren;
        break;
      default:
        if (Paren == 0)
          Result += C;
      }
    }
    return Result;
  }

  void apply(SetTheory &ST, DagInit *Expr, SetTheory::RecSet &Elts,
             ArrayRef<SMLoc> Loc) override {
    ArrayRef<const CodeGenInstruction *> Instructions =
        Target.getInstructionsByEnumValue();

    unsigned NumGeneric = Target.getNumFixedInstructions();
    unsigned NumPseudos = Target.getNumPseudoInstructions();
    auto Generics = Instructions.slice(0, NumGeneric);
    auto Pseudos = Instructions.slice(NumGeneric, NumPseudos);
    auto NonPseudos = Instructions.slice(NumGeneric + NumPseudos);

    for (Init *Arg : make_range(Expr->arg_begin(), Expr->arg_end())) {
      StringInit *SI = dyn_cast<StringInit>(Arg);
      if (!SI)
        PrintFatalError(Loc, "instregex requires pattern string: " +
                                 Expr->getAsString());
      StringRef Original = SI->getValue();

      // Extract a prefix that we can binary search on.
      static const char RegexMetachars[] = "()^$|*+?.[]\\{}";
      auto FirstMeta = Original.find_first_of(RegexMetachars);

      // Look for top-level | or ?. We cannot optimize them to binary search.
      if (removeParens(Original).find_first_of("|?") != std::string::npos)
        FirstMeta = 0;

      Optional<Regex> Regexpr = None;
      StringRef Prefix = Original.substr(0, FirstMeta);
      StringRef PatStr = Original.substr(FirstMeta);
      if (!PatStr.empty()) {
        // For the rest use a python-style prefix match.
        std::string pat = PatStr;
        if (pat[0] != '^') {
          pat.insert(0, "^(");
          pat.insert(pat.end(), ')');
        }
        Regexpr = Regex(pat);
      }

      int NumMatches = 0;

      // The generic opcodes are unsorted, handle them manually.
      for (auto *Inst : Generics) {
        StringRef InstName = Inst->TheDef->getName();
        if (InstName.startswith(Prefix) &&
            (!Regexpr || Regexpr->match(InstName.substr(Prefix.size())))) {
          Elts.insert(Inst->TheDef);
          NumMatches++;
        }
      }

      // Target instructions are split into two ranges: pseudo instructions
      // first, than non-pseudos. Each range is in lexicographical order
      // sorted by name. Find the sub-ranges that start with our prefix.
      struct Comp {
        bool operator()(const CodeGenInstruction *LHS, StringRef RHS) {
          return LHS->TheDef->getName() < RHS;
        }
        bool operator()(StringRef LHS, const CodeGenInstruction *RHS) {
          return LHS < RHS->TheDef->getName() &&
                 !RHS->TheDef->getName().startswith(LHS);
        }
      };
      auto Range1 =
          std::equal_range(Pseudos.begin(), Pseudos.end(), Prefix, Comp());
      auto Range2 = std::equal_range(NonPseudos.begin(), NonPseudos.end(),
                                     Prefix, Comp());

      // For these ranges we know that instruction names start with the prefix.
      // Check if there's a regex that needs to be checked.
      const auto HandleNonGeneric = [&](const CodeGenInstruction *Inst) {
        StringRef InstName = Inst->TheDef->getName();
        if (!Regexpr || Regexpr->match(InstName.substr(Prefix.size()))) {
          Elts.insert(Inst->TheDef);
          NumMatches++;
        }
      };
      std::for_each(Range1.first, Range1.second, HandleNonGeneric);
      std::for_each(Range2.first, Range2.second, HandleNonGeneric);

      if (0 == NumMatches)
        PrintFatalError(Loc, "instregex has no matches: " + Original);
    }
  }
};

} // end anonymous namespace

/// CodeGenModels ctor interprets machine model records and populates maps.
CodeGenSchedModels::CodeGenSchedModels(RecordKeeper &RK,
                                       const CodeGenTarget &TGT):
  Records(RK), Target(TGT) {

  Sets.addFieldExpander("InstRW", "Instrs");

  // Allow Set evaluation to recognize the dags used in InstRW records:
  // (instrs Op1, Op1...)
  Sets.addOperator("instrs", llvm::make_unique<InstrsOp>());
  Sets.addOperator("instregex", llvm::make_unique<InstRegexOp>(Target));

  // Instantiate a CodeGenProcModel for each SchedMachineModel with the values
  // that are explicitly referenced in tablegen records. Resources associated
  // with each processor will be derived later. Populate ProcModelMap with the
  // CodeGenProcModel instances.
  collectProcModels();

  // Instantiate a CodeGenSchedRW for each SchedReadWrite record explicitly
  // defined, and populate SchedReads and SchedWrites vectors. Implicit
  // SchedReadWrites that represent sequences derived from expanded variant will
  // be inferred later.
  collectSchedRW();

  // Instantiate a CodeGenSchedClass for each unique SchedRW signature directly
  // required by an instruction definition, and populate SchedClassIdxMap. Set
  // NumItineraryClasses to the number of explicit itinerary classes referenced
  // by instructions. Set NumInstrSchedClasses to the number of itinerary
  // classes plus any classes implied by instructions that derive from class
  // Sched and provide SchedRW list. This does not infer any new classes from
  // SchedVariant.
  collectSchedClasses();

  // Find instruction itineraries for each processor. Sort and populate
  // CodeGenProcModel::ItinDefList. (Cycle-to-cycle itineraries). This requires
  // all itinerary classes to be discovered.
  collectProcItins();

  // Find ItinRW records for each processor and itinerary class.
  // (For per-operand resources mapped to itinerary classes).
  collectProcItinRW();

  // Find UnsupportedFeatures records for each processor.
  // (For per-operand resources mapped to itinerary classes).
  collectProcUnsupportedFeatures();

  // Infer new SchedClasses from SchedVariant.
  inferSchedClasses();

  // Populate each CodeGenProcModel's WriteResDefs, ReadAdvanceDefs, and
  // ProcResourceDefs.
  LLVM_DEBUG(
      dbgs() << "\n+++ RESOURCE DEFINITIONS (collectProcResources) +++\n");
  collectProcResources();

  // Collect optional processor description.
  collectOptionalProcessorInfo();

  // Check MCInstPredicate definitions.
  checkMCInstPredicates();

  // Check STIPredicate definitions.
  checkSTIPredicates();

  // Find STIPredicate definitions for each processor model, and construct
  // STIPredicateFunction objects.
  collectSTIPredicates();

  checkCompleteness();
}

void CodeGenSchedModels::checkSTIPredicates() const {
  DenseMap<StringRef, const Record *> Declarations;

  // There cannot be multiple declarations with the same name.
  const RecVec Decls = Records.getAllDerivedDefinitions("STIPredicateDecl");
  for (const Record *R : Decls) {
    StringRef Name = R->getValueAsString("Name");
    const auto It = Declarations.find(Name);
    if (It == Declarations.end()) {
      Declarations[Name] = R;
      continue;
    }

    PrintError(R->getLoc(), "STIPredicate " + Name + " multiply declared.");
    PrintNote(It->second->getLoc(), "Previous declaration was here.");
    PrintFatalError(R->getLoc(), "Invalid STIPredicateDecl found.");
  }

  // Disallow InstructionEquivalenceClasses with an empty instruction list.
  const RecVec Defs =
      Records.getAllDerivedDefinitions("InstructionEquivalenceClass");
  for (const Record *R : Defs) {
    RecVec Opcodes = R->getValueAsListOfDefs("Opcodes");
    if (Opcodes.empty()) {
      PrintFatalError(R->getLoc(), "Invalid InstructionEquivalenceClass "
                                   "defined with an empty opcode list.");
    }
  }
}

// Used by function `processSTIPredicate` to construct a mask of machine
// instruction operands.
static APInt constructOperandMask(ArrayRef<int64_t> Indices) {
  APInt OperandMask;
  if (Indices.empty())
    return OperandMask;

  int64_t MaxIndex = *std::max_element(Indices.begin(), Indices.end());
  assert(MaxIndex >= 0 && "Invalid negative indices in input!");
  OperandMask = OperandMask.zext(MaxIndex + 1);
  for (const int64_t Index : Indices) {
    assert(Index >= 0 && "Invalid negative indices!");
    OperandMask.setBit(Index);
  }

  return OperandMask;
}

static void
processSTIPredicate(STIPredicateFunction &Fn,
                    const DenseMap<Record *, unsigned> &ProcModelMap) {
  DenseMap<const Record *, unsigned> Opcode2Index;
  using OpcodeMapPair = std::pair<const Record *, OpcodeInfo>;
  std::vector<OpcodeMapPair> OpcodeMappings;
  std::vector<std::pair<APInt, APInt>> OpcodeMasks;

  DenseMap<const Record *, unsigned> Predicate2Index;
  unsigned NumUniquePredicates = 0;

  // Number unique predicates and opcodes used by InstructionEquivalenceClass
  // definitions. Each unique opcode will be associated with an OpcodeInfo
  // object.
  for (const Record *Def : Fn.getDefinitions()) {
    RecVec Classes = Def->getValueAsListOfDefs("Classes");
    for (const Record *EC : Classes) {
      const Record *Pred = EC->getValueAsDef("Predicate");
      if (Predicate2Index.find(Pred) == Predicate2Index.end())
        Predicate2Index[Pred] = NumUniquePredicates++;

      RecVec Opcodes = EC->getValueAsListOfDefs("Opcodes");
      for (const Record *Opcode : Opcodes) {
        if (Opcode2Index.find(Opcode) == Opcode2Index.end()) {
          Opcode2Index[Opcode] = OpcodeMappings.size();
          OpcodeMappings.emplace_back(Opcode, OpcodeInfo());
        }
      }
    }
  }

  // Initialize vector `OpcodeMasks` with default values.  We want to keep track
  // of which processors "use" which opcodes.  We also want to be able to
  // identify predicates that are used by different processors for a same
  // opcode.
  // This information is used later on by this algorithm to sort OpcodeMapping
  // elements based on their processor and predicate sets.
  OpcodeMasks.resize(OpcodeMappings.size());
  APInt DefaultProcMask(ProcModelMap.size(), 0);
  APInt DefaultPredMask(NumUniquePredicates, 0);
  for (std::pair<APInt, APInt> &MaskPair : OpcodeMasks)
    MaskPair = std::make_pair(DefaultProcMask, DefaultPredMask);

  // Construct a OpcodeInfo object for every unique opcode declared by an
  // InstructionEquivalenceClass definition.
  for (const Record *Def : Fn.getDefinitions()) {
    RecVec Classes = Def->getValueAsListOfDefs("Classes");
    const Record *SchedModel = Def->getValueAsDef("SchedModel");
    unsigned ProcIndex = ProcModelMap.find(SchedModel)->second;
    APInt ProcMask(ProcModelMap.size(), 0);
    ProcMask.setBit(ProcIndex);

    for (const Record *EC : Classes) {
      RecVec Opcodes = EC->getValueAsListOfDefs("Opcodes");

      std::vector<int64_t> OpIndices =
          EC->getValueAsListOfInts("OperandIndices");
      APInt OperandMask = constructOperandMask(OpIndices);

      const Record *Pred = EC->getValueAsDef("Predicate");
      APInt PredMask(NumUniquePredicates, 0);
      PredMask.setBit(Predicate2Index[Pred]);

      for (const Record *Opcode : Opcodes) {
        unsigned OpcodeIdx = Opcode2Index[Opcode];
        if (OpcodeMasks[OpcodeIdx].first[ProcIndex]) {
          std::string Message =
              "Opcode " + Opcode->getName().str() +
              " used by multiple InstructionEquivalenceClass definitions.";
          PrintFatalError(EC->getLoc(), Message);
        }
        OpcodeMasks[OpcodeIdx].first |= ProcMask;
        OpcodeMasks[OpcodeIdx].second |= PredMask;
        OpcodeInfo &OI = OpcodeMappings[OpcodeIdx].second;

        OI.addPredicateForProcModel(ProcMask, OperandMask, Pred);
      }
    }
  }

  // Sort OpcodeMappings elements based on their CPU and predicate masks.
  // As a last resort, order elements by opcode identifier.
  llvm::sort(OpcodeMappings,
             [&](const OpcodeMapPair &Lhs, const OpcodeMapPair &Rhs) {
               unsigned LhsIdx = Opcode2Index[Lhs.first];
               unsigned RhsIdx = Opcode2Index[Rhs.first];
               std::pair<APInt, APInt> &LhsMasks = OpcodeMasks[LhsIdx];
               std::pair<APInt, APInt> &RhsMasks = OpcodeMasks[RhsIdx];

               if (LhsMasks.first != RhsMasks.first) {
                 if (LhsMasks.first.countPopulation() <
                     RhsMasks.first.countPopulation())
                   return true;
                 return LhsMasks.first.countLeadingZeros() >
                        RhsMasks.first.countLeadingZeros();
               }

               if (LhsMasks.second != RhsMasks.second) {
                 if (LhsMasks.second.countPopulation() <
                     RhsMasks.second.countPopulation())
                   return true;
                 return LhsMasks.second.countLeadingZeros() >
                        RhsMasks.second.countLeadingZeros();
               }

               return LhsIdx < RhsIdx;
             });

  // Now construct opcode groups. Groups are used by the SubtargetEmitter when
  // expanding the body of a STIPredicate function. In particular, each opcode
  // group is expanded into a sequence of labels in a switch statement.
  // It identifies opcodes for which different processors define same predicates
  // and same opcode masks.
  for (OpcodeMapPair &Info : OpcodeMappings)
    Fn.addOpcode(Info.first, std::move(Info.second));
}

void CodeGenSchedModels::collectSTIPredicates() {
  // Map STIPredicateDecl records to elements of vector
  // CodeGenSchedModels::STIPredicates.
  DenseMap<const Record *, unsigned> Decl2Index;

  RecVec RV = Records.getAllDerivedDefinitions("STIPredicate");
  for (const Record *R : RV) {
    const Record *Decl = R->getValueAsDef("Declaration");

    const auto It = Decl2Index.find(Decl);
    if (It == Decl2Index.end()) {
      Decl2Index[Decl] = STIPredicates.size();
      STIPredicateFunction Predicate(Decl);
      Predicate.addDefinition(R);
      STIPredicates.emplace_back(std::move(Predicate));
      continue;
    }

    STIPredicateFunction &PreviousDef = STIPredicates[It->second];
    PreviousDef.addDefinition(R);
  }

  for (STIPredicateFunction &Fn : STIPredicates)
    processSTIPredicate(Fn, ProcModelMap);
}

void OpcodeInfo::addPredicateForProcModel(const llvm::APInt &CpuMask,
                                          const llvm::APInt &OperandMask,
                                          const Record *Predicate) {
  auto It = llvm::find_if(
      Predicates, [&OperandMask, &Predicate](const PredicateInfo &P) {
        return P.Predicate == Predicate && P.OperandMask == OperandMask;
      });
  if (It == Predicates.end()) {
    Predicates.emplace_back(CpuMask, OperandMask, Predicate);
    return;
  }
  It->ProcModelMask |= CpuMask;
}

void CodeGenSchedModels::checkMCInstPredicates() const {
  RecVec MCPredicates = Records.getAllDerivedDefinitions("TIIPredicate");
  if (MCPredicates.empty())
    return;

  // A target cannot have multiple TIIPredicate definitions with a same name.
  llvm::StringMap<const Record *> TIIPredicates(MCPredicates.size());
  for (const Record *TIIPred : MCPredicates) {
    StringRef Name = TIIPred->getValueAsString("FunctionName");
    StringMap<const Record *>::const_iterator It = TIIPredicates.find(Name);
    if (It == TIIPredicates.end()) {
      TIIPredicates[Name] = TIIPred;
      continue;
    }

    PrintError(TIIPred->getLoc(),
               "TIIPredicate " + Name + " is multiply defined.");
    PrintNote(It->second->getLoc(),
              " Previous definition of " + Name + " was here.");
    PrintFatalError(TIIPred->getLoc(),
                    "Found conflicting definitions of TIIPredicate.");
  }
}

void CodeGenSchedModels::collectRetireControlUnits() {
  RecVec Units = Records.getAllDerivedDefinitions("RetireControlUnit");

  for (Record *RCU : Units) {
    CodeGenProcModel &PM = getProcModel(RCU->getValueAsDef("SchedModel"));
    if (PM.RetireControlUnit) {
      PrintError(RCU->getLoc(),
                 "Expected a single RetireControlUnit definition");
      PrintNote(PM.RetireControlUnit->getLoc(),
                "Previous definition of RetireControlUnit was here");
    }
    PM.RetireControlUnit = RCU;
  }
}

void CodeGenSchedModels::collectLoadStoreQueueInfo() {
  RecVec Queues = Records.getAllDerivedDefinitions("MemoryQueue");

  for (Record *Queue : Queues) {
    CodeGenProcModel &PM = getProcModel(Queue->getValueAsDef("SchedModel"));
    if (Queue->isSubClassOf("LoadQueue")) {
      if (PM.LoadQueue) {
        PrintError(Queue->getLoc(),
                   "Expected a single LoadQueue definition");
        PrintNote(PM.LoadQueue->getLoc(),
                  "Previous definition of LoadQueue was here");
      }

      PM.LoadQueue = Queue;
    }

    if (Queue->isSubClassOf("StoreQueue")) {
      if (PM.StoreQueue) {
        PrintError(Queue->getLoc(),
                   "Expected a single StoreQueue definition");
        PrintNote(PM.LoadQueue->getLoc(),
                  "Previous definition of StoreQueue was here");
      }

      PM.StoreQueue = Queue;
    }
  }
}

/// Collect optional processor information.
void CodeGenSchedModels::collectOptionalProcessorInfo() {
  // Find register file definitions for each processor.
  collectRegisterFiles();

  // Collect processor RetireControlUnit descriptors if available.
  collectRetireControlUnits();

  // Collect information about load/store queues.
  collectLoadStoreQueueInfo();

  checkCompleteness();
}

/// Gather all processor models.
void CodeGenSchedModels::collectProcModels() {
  RecVec ProcRecords = Records.getAllDerivedDefinitions("Processor");
  llvm::sort(ProcRecords, LessRecordFieldName());

  // Reserve space because we can. Reallocation would be ok.
  ProcModels.reserve(ProcRecords.size()+1);

  // Use idx=0 for NoModel/NoItineraries.
  Record *NoModelDef = Records.getDef("NoSchedModel");
  Record *NoItinsDef = Records.getDef("NoItineraries");
  ProcModels.emplace_back(0, "NoSchedModel", NoModelDef, NoItinsDef);
  ProcModelMap[NoModelDef] = 0;

  // For each processor, find a unique machine model.
  LLVM_DEBUG(dbgs() << "+++ PROCESSOR MODELs (addProcModel) +++\n");
  for (Record *ProcRecord : ProcRecords)
    addProcModel(ProcRecord);
}

/// Get a unique processor model based on the defined MachineModel and
/// ProcessorItineraries.
void CodeGenSchedModels::addProcModel(Record *ProcDef) {
  Record *ModelKey = getModelOrItinDef(ProcDef);
  if (!ProcModelMap.insert(std::make_pair(ModelKey, ProcModels.size())).second)
    return;

  std::string Name = ModelKey->getName();
  if (ModelKey->isSubClassOf("SchedMachineModel")) {
    Record *ItinsDef = ModelKey->getValueAsDef("Itineraries");
    ProcModels.emplace_back(ProcModels.size(), Name, ModelKey, ItinsDef);
  }
  else {
    // An itinerary is defined without a machine model. Infer a new model.
    if (!ModelKey->getValueAsListOfDefs("IID").empty())
      Name = Name + "Model";
    ProcModels.emplace_back(ProcModels.size(), Name,
                            ProcDef->getValueAsDef("SchedModel"), ModelKey);
  }
  LLVM_DEBUG(ProcModels.back().dump());
}

// Recursively find all reachable SchedReadWrite records.
static void scanSchedRW(Record *RWDef, RecVec &RWDefs,
                        SmallPtrSet<Record*, 16> &RWSet) {
  if (!RWSet.insert(RWDef).second)
    return;
  RWDefs.push_back(RWDef);
  // Reads don't currently have sequence records, but it can be added later.
  if (RWDef->isSubClassOf("WriteSequence")) {
    RecVec Seq = RWDef->getValueAsListOfDefs("Writes");
    for (Record *WSRec : Seq)
      scanSchedRW(WSRec, RWDefs, RWSet);
  }
  else if (RWDef->isSubClassOf("SchedVariant")) {
    // Visit each variant (guarded by a different predicate).
    RecVec Vars = RWDef->getValueAsListOfDefs("Variants");
    for (Record *Variant : Vars) {
      // Visit each RW in the sequence selected by the current variant.
      RecVec Selected = Variant->getValueAsListOfDefs("Selected");
      for (Record *SelDef : Selected)
        scanSchedRW(SelDef, RWDefs, RWSet);
    }
  }
}

// Collect and sort all SchedReadWrites reachable via tablegen records.
// More may be inferred later when inferring new SchedClasses from variants.
void CodeGenSchedModels::collectSchedRW() {
  // Reserve idx=0 for invalid writes/reads.
  SchedWrites.resize(1);
  SchedReads.resize(1);

  SmallPtrSet<Record*, 16> RWSet;

  // Find all SchedReadWrites referenced by instruction defs.
  RecVec SWDefs, SRDefs;
  for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue()) {
    Record *SchedDef = Inst->TheDef;
    if (SchedDef->isValueUnset("SchedRW"))
      continue;
    RecVec RWs = SchedDef->getValueAsListOfDefs("SchedRW");
    for (Record *RW : RWs) {
      if (RW->isSubClassOf("SchedWrite"))
        scanSchedRW(RW, SWDefs, RWSet);
      else {
        assert(RW->isSubClassOf("SchedRead") && "Unknown SchedReadWrite");
        scanSchedRW(RW, SRDefs, RWSet);
      }
    }
  }
  // Find all ReadWrites referenced by InstRW.
  RecVec InstRWDefs = Records.getAllDerivedDefinitions("InstRW");
  for (Record *InstRWDef : InstRWDefs) {
    // For all OperandReadWrites.
    RecVec RWDefs = InstRWDef->getValueAsListOfDefs("OperandReadWrites");
    for (Record *RWDef : RWDefs) {
      if (RWDef->isSubClassOf("SchedWrite"))
        scanSchedRW(RWDef, SWDefs, RWSet);
      else {
        assert(RWDef->isSubClassOf("SchedRead") && "Unknown SchedReadWrite");
        scanSchedRW(RWDef, SRDefs, RWSet);
      }
    }
  }
  // Find all ReadWrites referenced by ItinRW.
  RecVec ItinRWDefs = Records.getAllDerivedDefinitions("ItinRW");
  for (Record *ItinRWDef : ItinRWDefs) {
    // For all OperandReadWrites.
    RecVec RWDefs = ItinRWDef->getValueAsListOfDefs("OperandReadWrites");
    for (Record *RWDef : RWDefs) {
      if (RWDef->isSubClassOf("SchedWrite"))
        scanSchedRW(RWDef, SWDefs, RWSet);
      else {
        assert(RWDef->isSubClassOf("SchedRead") && "Unknown SchedReadWrite");
        scanSchedRW(RWDef, SRDefs, RWSet);
      }
    }
  }
  // Find all ReadWrites referenced by SchedAlias. AliasDefs needs to be sorted
  // for the loop below that initializes Alias vectors.
  RecVec AliasDefs = Records.getAllDerivedDefinitions("SchedAlias");
  llvm::sort(AliasDefs, LessRecord());
  for (Record *ADef : AliasDefs) {
    Record *MatchDef = ADef->getValueAsDef("MatchRW");
    Record *AliasDef = ADef->getValueAsDef("AliasRW");
    if (MatchDef->isSubClassOf("SchedWrite")) {
      if (!AliasDef->isSubClassOf("SchedWrite"))
        PrintFatalError(ADef->getLoc(), "SchedWrite Alias must be SchedWrite");
      scanSchedRW(AliasDef, SWDefs, RWSet);
    }
    else {
      assert(MatchDef->isSubClassOf("SchedRead") && "Unknown SchedReadWrite");
      if (!AliasDef->isSubClassOf("SchedRead"))
        PrintFatalError(ADef->getLoc(), "SchedRead Alias must be SchedRead");
      scanSchedRW(AliasDef, SRDefs, RWSet);
    }
  }
  // Sort and add the SchedReadWrites directly referenced by instructions or
  // itinerary resources. Index reads and writes in separate domains.
  llvm::sort(SWDefs, LessRecord());
  for (Record *SWDef : SWDefs) {
    assert(!getSchedRWIdx(SWDef, /*IsRead=*/false) && "duplicate SchedWrite");
    SchedWrites.emplace_back(SchedWrites.size(), SWDef);
  }
  llvm::sort(SRDefs, LessRecord());
  for (Record *SRDef : SRDefs) {
    assert(!getSchedRWIdx(SRDef, /*IsRead-*/true) && "duplicate SchedWrite");
    SchedReads.emplace_back(SchedReads.size(), SRDef);
  }
  // Initialize WriteSequence vectors.
  for (CodeGenSchedRW &CGRW : SchedWrites) {
    if (!CGRW.IsSequence)
      continue;
    findRWs(CGRW.TheDef->getValueAsListOfDefs("Writes"), CGRW.Sequence,
            /*IsRead=*/false);
  }
  // Initialize Aliases vectors.
  for (Record *ADef : AliasDefs) {
    Record *AliasDef = ADef->getValueAsDef("AliasRW");
    getSchedRW(AliasDef).IsAlias = true;
    Record *MatchDef = ADef->getValueAsDef("MatchRW");
    CodeGenSchedRW &RW = getSchedRW(MatchDef);
    if (RW.IsAlias)
      PrintFatalError(ADef->getLoc(), "Cannot Alias an Alias");
    RW.Aliases.push_back(ADef);
  }
  LLVM_DEBUG(
      dbgs() << "\n+++ SCHED READS and WRITES (collectSchedRW) +++\n";
      for (unsigned WIdx = 0, WEnd = SchedWrites.size(); WIdx != WEnd; ++WIdx) {
        dbgs() << WIdx << ": ";
        SchedWrites[WIdx].dump();
        dbgs() << '\n';
      } for (unsigned RIdx = 0, REnd = SchedReads.size(); RIdx != REnd;
             ++RIdx) {
        dbgs() << RIdx << ": ";
        SchedReads[RIdx].dump();
        dbgs() << '\n';
      } RecVec RWDefs = Records.getAllDerivedDefinitions("SchedReadWrite");
      for (Record *RWDef
           : RWDefs) {
        if (!getSchedRWIdx(RWDef, RWDef->isSubClassOf("SchedRead"))) {
          StringRef Name = RWDef->getName();
          if (Name != "NoWrite" && Name != "ReadDefault")
            dbgs() << "Unused SchedReadWrite " << Name << '\n';
        }
      });
}

/// Compute a SchedWrite name from a sequence of writes.
std::string CodeGenSchedModels::genRWName(ArrayRef<unsigned> Seq, bool IsRead) {
  std::string Name("(");
  for (auto I = Seq.begin(), E = Seq.end(); I != E; ++I) {
    if (I != Seq.begin())
      Name += '_';
    Name += getSchedRW(*I, IsRead).Name;
  }
  Name += ')';
  return Name;
}

unsigned CodeGenSchedModels::getSchedRWIdx(const Record *Def,
                                           bool IsRead) const {
  const std::vector<CodeGenSchedRW> &RWVec = IsRead ? SchedReads : SchedWrites;
  const auto I = find_if(
      RWVec, [Def](const CodeGenSchedRW &RW) { return RW.TheDef == Def; });
  return I == RWVec.end() ? 0 : std::distance(RWVec.begin(), I);
}

bool CodeGenSchedModels::hasReadOfWrite(Record *WriteDef) const {
  for (const CodeGenSchedRW &Read : SchedReads) {
    Record *ReadDef = Read.TheDef;
    if (!ReadDef || !ReadDef->isSubClassOf("ProcReadAdvance"))
      continue;

    RecVec ValidWrites = ReadDef->getValueAsListOfDefs("ValidWrites");
    if (is_contained(ValidWrites, WriteDef)) {
      return true;
    }
  }
  return false;
}

static void splitSchedReadWrites(const RecVec &RWDefs,
                                 RecVec &WriteDefs, RecVec &ReadDefs) {
  for (Record *RWDef : RWDefs) {
    if (RWDef->isSubClassOf("SchedWrite"))
      WriteDefs.push_back(RWDef);
    else {
      assert(RWDef->isSubClassOf("SchedRead") && "unknown SchedReadWrite");
      ReadDefs.push_back(RWDef);
    }
  }
}

// Split the SchedReadWrites defs and call findRWs for each list.
void CodeGenSchedModels::findRWs(const RecVec &RWDefs,
                                 IdxVec &Writes, IdxVec &Reads) const {
  RecVec WriteDefs;
  RecVec ReadDefs;
  splitSchedReadWrites(RWDefs, WriteDefs, ReadDefs);
  findRWs(WriteDefs, Writes, false);
  findRWs(ReadDefs, Reads, true);
}

// Call getSchedRWIdx for all elements in a sequence of SchedRW defs.
void CodeGenSchedModels::findRWs(const RecVec &RWDefs, IdxVec &RWs,
                                 bool IsRead) const {
  for (Record *RWDef : RWDefs) {
    unsigned Idx = getSchedRWIdx(RWDef, IsRead);
    assert(Idx && "failed to collect SchedReadWrite");
    RWs.push_back(Idx);
  }
}

void CodeGenSchedModels::expandRWSequence(unsigned RWIdx, IdxVec &RWSeq,
                                          bool IsRead) const {
  const CodeGenSchedRW &SchedRW = getSchedRW(RWIdx, IsRead);
  if (!SchedRW.IsSequence) {
    RWSeq.push_back(RWIdx);
    return;
  }
  int Repeat =
    SchedRW.TheDef ? SchedRW.TheDef->getValueAsInt("Repeat") : 1;
  for (int i = 0; i < Repeat; ++i) {
    for (unsigned I : SchedRW.Sequence) {
      expandRWSequence(I, RWSeq, IsRead);
    }
  }
}

// Expand a SchedWrite as a sequence following any aliases that coincide with
// the given processor model.
void CodeGenSchedModels::expandRWSeqForProc(
  unsigned RWIdx, IdxVec &RWSeq, bool IsRead,
  const CodeGenProcModel &ProcModel) const {

  const CodeGenSchedRW &SchedWrite = getSchedRW(RWIdx, IsRead);
  Record *AliasDef = nullptr;
  for (const Record *Rec : SchedWrite.Aliases) {
    const CodeGenSchedRW &AliasRW = getSchedRW(Rec->getValueAsDef("AliasRW"));
    if (Rec->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = Rec->getValueAsDef("SchedModel");
      if (&getProcModel(ModelDef) != &ProcModel)
        continue;
    }
    if (AliasDef)
      PrintFatalError(AliasRW.TheDef->getLoc(), "Multiple aliases "
                      "defined for processor " + ProcModel.ModelName +
                      " Ensure only one SchedAlias exists per RW.");
    AliasDef = AliasRW.TheDef;
  }
  if (AliasDef) {
    expandRWSeqForProc(getSchedRWIdx(AliasDef, IsRead),
                       RWSeq, IsRead,ProcModel);
    return;
  }
  if (!SchedWrite.IsSequence) {
    RWSeq.push_back(RWIdx);
    return;
  }
  int Repeat =
    SchedWrite.TheDef ? SchedWrite.TheDef->getValueAsInt("Repeat") : 1;
  for (int I = 0, E = Repeat; I < E; ++I) {
    for (unsigned Idx : SchedWrite.Sequence) {
      expandRWSeqForProc(Idx, RWSeq, IsRead, ProcModel);
    }
  }
}

// Find the existing SchedWrite that models this sequence of writes.
unsigned CodeGenSchedModels::findRWForSequence(ArrayRef<unsigned> Seq,
                                               bool IsRead) {
  std::vector<CodeGenSchedRW> &RWVec = IsRead ? SchedReads : SchedWrites;

  auto I = find_if(RWVec, [Seq](CodeGenSchedRW &RW) {
    return makeArrayRef(RW.Sequence) == Seq;
  });
  // Index zero reserved for invalid RW.
  return I == RWVec.end() ? 0 : std::distance(RWVec.begin(), I);
}

/// Add this ReadWrite if it doesn't already exist.
unsigned CodeGenSchedModels::findOrInsertRW(ArrayRef<unsigned> Seq,
                                            bool IsRead) {
  assert(!Seq.empty() && "cannot insert empty sequence");
  if (Seq.size() == 1)
    return Seq.back();

  unsigned Idx = findRWForSequence(Seq, IsRead);
  if (Idx)
    return Idx;

  std::vector<CodeGenSchedRW> &RWVec = IsRead ? SchedReads : SchedWrites;
  unsigned RWIdx = RWVec.size();
  CodeGenSchedRW SchedRW(RWIdx, IsRead, Seq, genRWName(Seq, IsRead));
  RWVec.push_back(SchedRW);
  return RWIdx;
}

/// Visit all the instruction definitions for this target to gather and
/// enumerate the itinerary classes. These are the explicitly specified
/// SchedClasses. More SchedClasses may be inferred.
void CodeGenSchedModels::collectSchedClasses() {

  // NoItinerary is always the first class at Idx=0
  assert(SchedClasses.empty() && "Expected empty sched class");
  SchedClasses.emplace_back(0, "NoInstrModel",
                            Records.getDef("NoItinerary"));
  SchedClasses.back().ProcIndices.push_back(0);

  // Create a SchedClass for each unique combination of itinerary class and
  // SchedRW list.
  for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue()) {
    Record *ItinDef = Inst->TheDef->getValueAsDef("Itinerary");
    IdxVec Writes, Reads;
    if (!Inst->TheDef->isValueUnset("SchedRW"))
      findRWs(Inst->TheDef->getValueAsListOfDefs("SchedRW"), Writes, Reads);

    // ProcIdx == 0 indicates the class applies to all processors.
    unsigned SCIdx = addSchedClass(ItinDef, Writes, Reads, /*ProcIndices*/{0});
    InstrClassMap[Inst->TheDef] = SCIdx;
  }
  // Create classes for InstRW defs.
  RecVec InstRWDefs = Records.getAllDerivedDefinitions("InstRW");
  llvm::sort(InstRWDefs, LessRecord());
  LLVM_DEBUG(dbgs() << "\n+++ SCHED CLASSES (createInstRWClass) +++\n");
  for (Record *RWDef : InstRWDefs)
    createInstRWClass(RWDef);

  NumInstrSchedClasses = SchedClasses.size();

  bool EnableDump = false;
  LLVM_DEBUG(EnableDump = true);
  if (!EnableDump)
    return;

  LLVM_DEBUG(
      dbgs()
      << "\n+++ ITINERARIES and/or MACHINE MODELS (collectSchedClasses) +++\n");
  for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue()) {
    StringRef InstName = Inst->TheDef->getName();
    unsigned SCIdx = getSchedClassIdx(*Inst);
    if (!SCIdx) {
      LLVM_DEBUG({
        if (!Inst->hasNoSchedulingInfo)
          dbgs() << "No machine model for " << Inst->TheDef->getName() << '\n';
      });
      continue;
    }
    CodeGenSchedClass &SC = getSchedClass(SCIdx);
    if (SC.ProcIndices[0] != 0)
      PrintFatalError(Inst->TheDef->getLoc(), "Instruction's sched class "
                      "must not be subtarget specific.");

    IdxVec ProcIndices;
    if (SC.ItinClassDef->getName() != "NoItinerary") {
      ProcIndices.push_back(0);
      dbgs() << "Itinerary for " << InstName << ": "
             << SC.ItinClassDef->getName() << '\n';
    }
    if (!SC.Writes.empty()) {
      ProcIndices.push_back(0);
      LLVM_DEBUG({
        dbgs() << "SchedRW machine model for " << InstName;
        for (IdxIter WI = SC.Writes.begin(), WE = SC.Writes.end(); WI != WE;
             ++WI)
          dbgs() << " " << SchedWrites[*WI].Name;
        for (IdxIter RI = SC.Reads.begin(), RE = SC.Reads.end(); RI != RE; ++RI)
          dbgs() << " " << SchedReads[*RI].Name;
        dbgs() << '\n';
      });
    }
    const RecVec &RWDefs = SchedClasses[SCIdx].InstRWs;
    for (Record *RWDef : RWDefs) {
      const CodeGenProcModel &ProcModel =
          getProcModel(RWDef->getValueAsDef("SchedModel"));
      ProcIndices.push_back(ProcModel.Index);
      LLVM_DEBUG(dbgs() << "InstRW on " << ProcModel.ModelName << " for "
                        << InstName);
      IdxVec Writes;
      IdxVec Reads;
      findRWs(RWDef->getValueAsListOfDefs("OperandReadWrites"),
              Writes, Reads);
      LLVM_DEBUG({
        for (unsigned WIdx : Writes)
          dbgs() << " " << SchedWrites[WIdx].Name;
        for (unsigned RIdx : Reads)
          dbgs() << " " << SchedReads[RIdx].Name;
        dbgs() << '\n';
      });
    }
    // If ProcIndices contains zero, the class applies to all processors.
    LLVM_DEBUG({
      if (!std::count(ProcIndices.begin(), ProcIndices.end(), 0)) {
        for (const CodeGenProcModel &PM : ProcModels) {
          if (!std::count(ProcIndices.begin(), ProcIndices.end(), PM.Index))
            dbgs() << "No machine model for " << Inst->TheDef->getName()
                   << " on processor " << PM.ModelName << '\n';
        }
      }
    });
  }
}

// Get the SchedClass index for an instruction.
unsigned
CodeGenSchedModels::getSchedClassIdx(const CodeGenInstruction &Inst) const {
  return InstrClassMap.lookup(Inst.TheDef);
}

std::string
CodeGenSchedModels::createSchedClassName(Record *ItinClassDef,
                                         ArrayRef<unsigned> OperWrites,
                                         ArrayRef<unsigned> OperReads) {

  std::string Name;
  if (ItinClassDef && ItinClassDef->getName() != "NoItinerary")
    Name = ItinClassDef->getName();
  for (unsigned Idx : OperWrites) {
    if (!Name.empty())
      Name += '_';
    Name += SchedWrites[Idx].Name;
  }
  for (unsigned Idx : OperReads) {
    Name += '_';
    Name += SchedReads[Idx].Name;
  }
  return Name;
}

std::string CodeGenSchedModels::createSchedClassName(const RecVec &InstDefs) {

  std::string Name;
  for (RecIter I = InstDefs.begin(), E = InstDefs.end(); I != E; ++I) {
    if (I != InstDefs.begin())
      Name += '_';
    Name += (*I)->getName();
  }
  return Name;
}

/// Add an inferred sched class from an itinerary class and per-operand list of
/// SchedWrites and SchedReads. ProcIndices contains the set of IDs of
/// processors that may utilize this class.
unsigned CodeGenSchedModels::addSchedClass(Record *ItinClassDef,
                                           ArrayRef<unsigned> OperWrites,
                                           ArrayRef<unsigned> OperReads,
                                           ArrayRef<unsigned> ProcIndices) {
  assert(!ProcIndices.empty() && "expect at least one ProcIdx");

  auto IsKeyEqual = [=](const CodeGenSchedClass &SC) {
                     return SC.isKeyEqual(ItinClassDef, OperWrites, OperReads);
                   };

  auto I = find_if(make_range(schedClassBegin(), schedClassEnd()), IsKeyEqual);
  unsigned Idx = I == schedClassEnd() ? 0 : std::distance(schedClassBegin(), I);
  if (Idx || SchedClasses[0].isKeyEqual(ItinClassDef, OperWrites, OperReads)) {
    IdxVec PI;
    std::set_union(SchedClasses[Idx].ProcIndices.begin(),
                   SchedClasses[Idx].ProcIndices.end(),
                   ProcIndices.begin(), ProcIndices.end(),
                   std::back_inserter(PI));
    SchedClasses[Idx].ProcIndices = std::move(PI);
    return Idx;
  }
  Idx = SchedClasses.size();
  SchedClasses.emplace_back(Idx,
                            createSchedClassName(ItinClassDef, OperWrites,
                                                 OperReads),
                            ItinClassDef);
  CodeGenSchedClass &SC = SchedClasses.back();
  SC.Writes = OperWrites;
  SC.Reads = OperReads;
  SC.ProcIndices = ProcIndices;

  return Idx;
}

// Create classes for each set of opcodes that are in the same InstReadWrite
// definition across all processors.
void CodeGenSchedModels::createInstRWClass(Record *InstRWDef) {
  // ClassInstrs will hold an entry for each subset of Instrs in InstRWDef that
  // intersects with an existing class via a previous InstRWDef. Instrs that do
  // not intersect with an existing class refer back to their former class as
  // determined from ItinDef or SchedRW.
  SmallMapVector<unsigned, SmallVector<Record *, 8>, 4> ClassInstrs;
  // Sort Instrs into sets.
  const RecVec *InstDefs = Sets.expand(InstRWDef);
  if (InstDefs->empty())
    PrintFatalError(InstRWDef->getLoc(), "No matching instruction opcodes");

  for (Record *InstDef : *InstDefs) {
    InstClassMapTy::const_iterator Pos = InstrClassMap.find(InstDef);
    if (Pos == InstrClassMap.end())
      PrintFatalError(InstDef->getLoc(), "No sched class for instruction.");
    unsigned SCIdx = Pos->second;
    ClassInstrs[SCIdx].push_back(InstDef);
  }
  // For each set of Instrs, create a new class if necessary, and map or remap
  // the Instrs to it.
  for (auto &Entry : ClassInstrs) {
    unsigned OldSCIdx = Entry.first;
    ArrayRef<Record*> InstDefs = Entry.second;
    // If the all instrs in the current class are accounted for, then leave
    // them mapped to their old class.
    if (OldSCIdx) {
      const RecVec &RWDefs = SchedClasses[OldSCIdx].InstRWs;
      if (!RWDefs.empty()) {
        const RecVec *OrigInstDefs = Sets.expand(RWDefs[0]);
        unsigned OrigNumInstrs =
          count_if(*OrigInstDefs, [&](Record *OIDef) {
                     return InstrClassMap[OIDef] == OldSCIdx;
                   });
        if (OrigNumInstrs == InstDefs.size()) {
          assert(SchedClasses[OldSCIdx].ProcIndices[0] == 0 &&
                 "expected a generic SchedClass");
          Record *RWModelDef = InstRWDef->getValueAsDef("SchedModel");
          // Make sure we didn't already have a InstRW containing this
          // instruction on this model.
          for (Record *RWD : RWDefs) {
            if (RWD->getValueAsDef("SchedModel") == RWModelDef &&
                RWModelDef->getValueAsBit("FullInstRWOverlapCheck")) {
              for (Record *Inst : InstDefs) {
                PrintFatalError(InstRWDef->getLoc(), "Overlapping InstRW def " +
                            Inst->getName() + " also matches " +
                            RWD->getValue("Instrs")->getValue()->getAsString());
              }
            }
          }
          LLVM_DEBUG(dbgs() << "InstRW: Reuse SC " << OldSCIdx << ":"
                            << SchedClasses[OldSCIdx].Name << " on "
                            << RWModelDef->getName() << "\n");
          SchedClasses[OldSCIdx].InstRWs.push_back(InstRWDef);
          continue;
        }
      }
    }
    unsigned SCIdx = SchedClasses.size();
    SchedClasses.emplace_back(SCIdx, createSchedClassName(InstDefs), nullptr);
    CodeGenSchedClass &SC = SchedClasses.back();
    LLVM_DEBUG(dbgs() << "InstRW: New SC " << SCIdx << ":" << SC.Name << " on "
                      << InstRWDef->getValueAsDef("SchedModel")->getName()
                      << "\n");

    // Preserve ItinDef and Writes/Reads for processors without an InstRW entry.
    SC.ItinClassDef = SchedClasses[OldSCIdx].ItinClassDef;
    SC.Writes = SchedClasses[OldSCIdx].Writes;
    SC.Reads = SchedClasses[OldSCIdx].Reads;
    SC.ProcIndices.push_back(0);
    // If we had an old class, copy it's InstRWs to this new class.
    if (OldSCIdx) {
      Record *RWModelDef = InstRWDef->getValueAsDef("SchedModel");
      for (Record *OldRWDef : SchedClasses[OldSCIdx].InstRWs) {
        if (OldRWDef->getValueAsDef("SchedModel") == RWModelDef) {
          for (Record *InstDef : InstDefs) {
            PrintFatalError(OldRWDef->getLoc(), "Overlapping InstRW def " +
                       InstDef->getName() + " also matches " +
                       OldRWDef->getValue("Instrs")->getValue()->getAsString());
          }
        }
        assert(OldRWDef != InstRWDef &&
               "SchedClass has duplicate InstRW def");
        SC.InstRWs.push_back(OldRWDef);
      }
    }
    // Map each Instr to this new class.
    for (Record *InstDef : InstDefs)
      InstrClassMap[InstDef] = SCIdx;
    SC.InstRWs.push_back(InstRWDef);
  }
}

// True if collectProcItins found anything.
bool CodeGenSchedModels::hasItineraries() const {
  for (const CodeGenProcModel &PM : make_range(procModelBegin(),procModelEnd()))
    if (PM.hasItineraries())
      return true;
  return false;
}

// Gather the processor itineraries.
void CodeGenSchedModels::collectProcItins() {
  LLVM_DEBUG(dbgs() << "\n+++ PROBLEM ITINERARIES (collectProcItins) +++\n");
  for (CodeGenProcModel &ProcModel : ProcModels) {
    if (!ProcModel.hasItineraries())
      continue;

    RecVec ItinRecords = ProcModel.ItinsDef->getValueAsListOfDefs("IID");
    assert(!ItinRecords.empty() && "ProcModel.hasItineraries is incorrect");

    // Populate ItinDefList with Itinerary records.
    ProcModel.ItinDefList.resize(NumInstrSchedClasses);

    // Insert each itinerary data record in the correct position within
    // the processor model's ItinDefList.
    for (Record *ItinData : ItinRecords) {
      const Record *ItinDef = ItinData->getValueAsDef("TheClass");
      bool FoundClass = false;

      for (const CodeGenSchedClass &SC :
           make_range(schedClassBegin(), schedClassEnd())) {
        // Multiple SchedClasses may share an itinerary. Update all of them.
        if (SC.ItinClassDef == ItinDef) {
          ProcModel.ItinDefList[SC.Index] = ItinData;
          FoundClass = true;
        }
      }
      if (!FoundClass) {
        LLVM_DEBUG(dbgs() << ProcModel.ItinsDef->getName()
                          << " missing class for itinerary "
                          << ItinDef->getName() << '\n');
      }
    }
    // Check for missing itinerary entries.
    assert(!ProcModel.ItinDefList[0] && "NoItinerary class can't have rec");
    LLVM_DEBUG(
        for (unsigned i = 1, N = ProcModel.ItinDefList.size(); i < N; ++i) {
          if (!ProcModel.ItinDefList[i])
            dbgs() << ProcModel.ItinsDef->getName()
                   << " missing itinerary for class " << SchedClasses[i].Name
                   << '\n';
        });
  }
}

// Gather the read/write types for each itinerary class.
void CodeGenSchedModels::collectProcItinRW() {
  RecVec ItinRWDefs = Records.getAllDerivedDefinitions("ItinRW");
  llvm::sort(ItinRWDefs, LessRecord());
  for (Record *RWDef  : ItinRWDefs) {
    if (!RWDef->getValueInit("SchedModel")->isComplete())
      PrintFatalError(RWDef->getLoc(), "SchedModel is undefined");
    Record *ModelDef = RWDef->getValueAsDef("SchedModel");
    ProcModelMapTy::const_iterator I = ProcModelMap.find(ModelDef);
    if (I == ProcModelMap.end()) {
      PrintFatalError(RWDef->getLoc(), "Undefined SchedMachineModel "
                    + ModelDef->getName());
    }
    ProcModels[I->second].ItinRWDefs.push_back(RWDef);
  }
}

// Gather the unsupported features for processor models.
void CodeGenSchedModels::collectProcUnsupportedFeatures() {
  for (CodeGenProcModel &ProcModel : ProcModels) {
    for (Record *Pred : ProcModel.ModelDef->getValueAsListOfDefs("UnsupportedFeatures")) {
       ProcModel.UnsupportedFeaturesDefs.push_back(Pred);
    }
  }
}

/// Infer new classes from existing classes. In the process, this may create new
/// SchedWrites from sequences of existing SchedWrites.
void CodeGenSchedModels::inferSchedClasses() {
  LLVM_DEBUG(
      dbgs() << "\n+++ INFERRING SCHED CLASSES (inferSchedClasses) +++\n");
  LLVM_DEBUG(dbgs() << NumInstrSchedClasses << " instr sched classes.\n");

  // Visit all existing classes and newly created classes.
  for (unsigned Idx = 0; Idx != SchedClasses.size(); ++Idx) {
    assert(SchedClasses[Idx].Index == Idx && "bad SCIdx");

    if (SchedClasses[Idx].ItinClassDef)
      inferFromItinClass(SchedClasses[Idx].ItinClassDef, Idx);
    if (!SchedClasses[Idx].InstRWs.empty())
      inferFromInstRWs(Idx);
    if (!SchedClasses[Idx].Writes.empty()) {
      inferFromRW(SchedClasses[Idx].Writes, SchedClasses[Idx].Reads,
                  Idx, SchedClasses[Idx].ProcIndices);
    }
    assert(SchedClasses.size() < (NumInstrSchedClasses*6) &&
           "too many SchedVariants");
  }
}

/// Infer classes from per-processor itinerary resources.
void CodeGenSchedModels::inferFromItinClass(Record *ItinClassDef,
                                            unsigned FromClassIdx) {
  for (unsigned PIdx = 0, PEnd = ProcModels.size(); PIdx != PEnd; ++PIdx) {
    const CodeGenProcModel &PM = ProcModels[PIdx];
    // For all ItinRW entries.
    bool HasMatch = false;
    for (const Record *Rec : PM.ItinRWDefs) {
      RecVec Matched = Rec->getValueAsListOfDefs("MatchedItinClasses");
      if (!std::count(Matched.begin(), Matched.end(), ItinClassDef))
        continue;
      if (HasMatch)
        PrintFatalError(Rec->getLoc(), "Duplicate itinerary class "
                      + ItinClassDef->getName()
                      + " in ItinResources for " + PM.ModelName);
      HasMatch = true;
      IdxVec Writes, Reads;
      findRWs(Rec->getValueAsListOfDefs("OperandReadWrites"), Writes, Reads);
      inferFromRW(Writes, Reads, FromClassIdx, PIdx);
    }
  }
}

/// Infer classes from per-processor InstReadWrite definitions.
void CodeGenSchedModels::inferFromInstRWs(unsigned SCIdx) {
  for (unsigned I = 0, E = SchedClasses[SCIdx].InstRWs.size(); I != E; ++I) {
    assert(SchedClasses[SCIdx].InstRWs.size() == E && "InstrRWs was mutated!");
    Record *Rec = SchedClasses[SCIdx].InstRWs[I];
    const RecVec *InstDefs = Sets.expand(Rec);
    RecIter II = InstDefs->begin(), IE = InstDefs->end();
    for (; II != IE; ++II) {
      if (InstrClassMap[*II] == SCIdx)
        break;
    }
    // If this class no longer has any instructions mapped to it, it has become
    // irrelevant.
    if (II == IE)
      continue;
    IdxVec Writes, Reads;
    findRWs(Rec->getValueAsListOfDefs("OperandReadWrites"), Writes, Reads);
    unsigned PIdx = getProcModel(Rec->getValueAsDef("SchedModel")).Index;
    inferFromRW(Writes, Reads, SCIdx, PIdx); // May mutate SchedClasses.
  }
}

namespace {

// Helper for substituteVariantOperand.
struct TransVariant {
  Record *VarOrSeqDef;  // Variant or sequence.
  unsigned RWIdx;       // Index of this variant or sequence's matched type.
  unsigned ProcIdx;     // Processor model index or zero for any.
  unsigned TransVecIdx; // Index into PredTransitions::TransVec.

  TransVariant(Record *def, unsigned rwi, unsigned pi, unsigned ti):
    VarOrSeqDef(def), RWIdx(rwi), ProcIdx(pi), TransVecIdx(ti) {}
};

// Associate a predicate with the SchedReadWrite that it guards.
// RWIdx is the index of the read/write variant.
struct PredCheck {
  bool IsRead;
  unsigned RWIdx;
  Record *Predicate;

  PredCheck(bool r, unsigned w, Record *p): IsRead(r), RWIdx(w), Predicate(p) {}
};

// A Predicate transition is a list of RW sequences guarded by a PredTerm.
struct PredTransition {
  // A predicate term is a conjunction of PredChecks.
  SmallVector<PredCheck, 4> PredTerm;
  SmallVector<SmallVector<unsigned,4>, 16> WriteSequences;
  SmallVector<SmallVector<unsigned,4>, 16> ReadSequences;
  SmallVector<unsigned, 4> ProcIndices;
};

// Encapsulate a set of partially constructed transitions.
// The results are built by repeated calls to substituteVariants.
class PredTransitions {
  CodeGenSchedModels &SchedModels;

public:
  std::vector<PredTransition> TransVec;

  PredTransitions(CodeGenSchedModels &sm): SchedModels(sm) {}

  void substituteVariantOperand(const SmallVectorImpl<unsigned> &RWSeq,
                                bool IsRead, unsigned StartIdx);

  void substituteVariants(const PredTransition &Trans);

#ifndef NDEBUG
  void dump() const;
#endif

private:
  bool mutuallyExclusive(Record *PredDef, ArrayRef<PredCheck> Term);
  void getIntersectingVariants(
    const CodeGenSchedRW &SchedRW, unsigned TransIdx,
    std::vector<TransVariant> &IntersectingVariants);
  void pushVariant(const TransVariant &VInfo, bool IsRead);
};

} // end anonymous namespace

// Return true if this predicate is mutually exclusive with a PredTerm. This
// degenerates into checking if the predicate is mutually exclusive with any
// predicate in the Term's conjunction.
//
// All predicates associated with a given SchedRW are considered mutually
// exclusive. This should work even if the conditions expressed by the
// predicates are not exclusive because the predicates for a given SchedWrite
// are always checked in the order they are defined in the .td file. Later
// conditions implicitly negate any prior condition.
bool PredTransitions::mutuallyExclusive(Record *PredDef,
                                        ArrayRef<PredCheck> Term) {
  for (const PredCheck &PC: Term) {
    if (PC.Predicate == PredDef)
      return false;

    const CodeGenSchedRW &SchedRW = SchedModels.getSchedRW(PC.RWIdx, PC.IsRead);
    assert(SchedRW.HasVariants && "PredCheck must refer to a SchedVariant");
    RecVec Variants = SchedRW.TheDef->getValueAsListOfDefs("Variants");
    if (any_of(Variants, [PredDef](const Record *R) {
          return R->getValueAsDef("Predicate") == PredDef;
        }))
      return true;
  }
  return false;
}

static bool hasAliasedVariants(const CodeGenSchedRW &RW,
                               CodeGenSchedModels &SchedModels) {
  if (RW.HasVariants)
    return true;

  for (Record *Alias : RW.Aliases) {
    const CodeGenSchedRW &AliasRW =
      SchedModels.getSchedRW(Alias->getValueAsDef("AliasRW"));
    if (AliasRW.HasVariants)
      return true;
    if (AliasRW.IsSequence) {
      IdxVec ExpandedRWs;
      SchedModels.expandRWSequence(AliasRW.Index, ExpandedRWs, AliasRW.IsRead);
      for (unsigned SI : ExpandedRWs) {
        if (hasAliasedVariants(SchedModels.getSchedRW(SI, AliasRW.IsRead),
                               SchedModels))
          return true;
      }
    }
  }
  return false;
}

static bool hasVariant(ArrayRef<PredTransition> Transitions,
                       CodeGenSchedModels &SchedModels) {
  for (const PredTransition &PTI : Transitions) {
    for (const SmallVectorImpl<unsigned> &WSI : PTI.WriteSequences)
      for (unsigned WI : WSI)
        if (hasAliasedVariants(SchedModels.getSchedWrite(WI), SchedModels))
          return true;

    for (const SmallVectorImpl<unsigned> &RSI : PTI.ReadSequences)
      for (unsigned RI : RSI)
        if (hasAliasedVariants(SchedModels.getSchedRead(RI), SchedModels))
          return true;
  }
  return false;
}

// Populate IntersectingVariants with any variants or aliased sequences of the
// given SchedRW whose processor indices and predicates are not mutually
// exclusive with the given transition.
void PredTransitions::getIntersectingVariants(
  const CodeGenSchedRW &SchedRW, unsigned TransIdx,
  std::vector<TransVariant> &IntersectingVariants) {

  bool GenericRW = false;

  std::vector<TransVariant> Variants;
  if (SchedRW.HasVariants) {
    unsigned VarProcIdx = 0;
    if (SchedRW.TheDef->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = SchedRW.TheDef->getValueAsDef("SchedModel");
      VarProcIdx = SchedModels.getProcModel(ModelDef).Index;
    }
    // Push each variant. Assign TransVecIdx later.
    const RecVec VarDefs = SchedRW.TheDef->getValueAsListOfDefs("Variants");
    for (Record *VarDef : VarDefs)
      Variants.emplace_back(VarDef, SchedRW.Index, VarProcIdx, 0);
    if (VarProcIdx == 0)
      GenericRW = true;
  }
  for (RecIter AI = SchedRW.Aliases.begin(), AE = SchedRW.Aliases.end();
       AI != AE; ++AI) {
    // If either the SchedAlias itself or the SchedReadWrite that it aliases
    // to is defined within a processor model, constrain all variants to
    // that processor.
    unsigned AliasProcIdx = 0;
    if ((*AI)->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = (*AI)->getValueAsDef("SchedModel");
      AliasProcIdx = SchedModels.getProcModel(ModelDef).Index;
    }
    const CodeGenSchedRW &AliasRW =
      SchedModels.getSchedRW((*AI)->getValueAsDef("AliasRW"));

    if (AliasRW.HasVariants) {
      const RecVec VarDefs = AliasRW.TheDef->getValueAsListOfDefs("Variants");
      for (Record *VD : VarDefs)
        Variants.emplace_back(VD, AliasRW.Index, AliasProcIdx, 0);
    }
    if (AliasRW.IsSequence)
      Variants.emplace_back(AliasRW.TheDef, SchedRW.Index, AliasProcIdx, 0);
    if (AliasProcIdx == 0)
      GenericRW = true;
  }
  for (TransVariant &Variant : Variants) {
    // Don't expand variants if the processor models don't intersect.
    // A zero processor index means any processor.
    SmallVectorImpl<unsigned> &ProcIndices = TransVec[TransIdx].ProcIndices;
    if (ProcIndices[0] && Variant.ProcIdx) {
      unsigned Cnt = std::count(ProcIndices.begin(), ProcIndices.end(),
                                Variant.ProcIdx);
      if (!Cnt)
        continue;
      if (Cnt > 1) {
        const CodeGenProcModel &PM =
          *(SchedModels.procModelBegin() + Variant.ProcIdx);
        PrintFatalError(Variant.VarOrSeqDef->getLoc(),
                        "Multiple variants defined for processor " +
                        PM.ModelName +
                        " Ensure only one SchedAlias exists per RW.");
      }
    }
    if (Variant.VarOrSeqDef->isSubClassOf("SchedVar")) {
      Record *PredDef = Variant.VarOrSeqDef->getValueAsDef("Predicate");
      if (mutuallyExclusive(PredDef, TransVec[TransIdx].PredTerm))
        continue;
    }
    if (IntersectingVariants.empty()) {
      // The first variant builds on the existing transition.
      Variant.TransVecIdx = TransIdx;
      IntersectingVariants.push_back(Variant);
    }
    else {
      // Push another copy of the current transition for more variants.
      Variant.TransVecIdx = TransVec.size();
      IntersectingVariants.push_back(Variant);
      TransVec.push_back(TransVec[TransIdx]);
    }
  }
  if (GenericRW && IntersectingVariants.empty()) {
    PrintFatalError(SchedRW.TheDef->getLoc(), "No variant of this type has "
                    "a matching predicate on any processor");
  }
}

// Push the Reads/Writes selected by this variant onto the PredTransition
// specified by VInfo.
void PredTransitions::
pushVariant(const TransVariant &VInfo, bool IsRead) {
  PredTransition &Trans = TransVec[VInfo.TransVecIdx];

  // If this operand transition is reached through a processor-specific alias,
  // then the whole transition is specific to this processor.
  if (VInfo.ProcIdx != 0)
    Trans.ProcIndices.assign(1, VInfo.ProcIdx);

  IdxVec SelectedRWs;
  if (VInfo.VarOrSeqDef->isSubClassOf("SchedVar")) {
    Record *PredDef = VInfo.VarOrSeqDef->getValueAsDef("Predicate");
    Trans.PredTerm.emplace_back(IsRead, VInfo.RWIdx,PredDef);
    RecVec SelectedDefs = VInfo.VarOrSeqDef->getValueAsListOfDefs("Selected");
    SchedModels.findRWs(SelectedDefs, SelectedRWs, IsRead);
  }
  else {
    assert(VInfo.VarOrSeqDef->isSubClassOf("WriteSequence") &&
           "variant must be a SchedVariant or aliased WriteSequence");
    SelectedRWs.push_back(SchedModels.getSchedRWIdx(VInfo.VarOrSeqDef, IsRead));
  }

  const CodeGenSchedRW &SchedRW = SchedModels.getSchedRW(VInfo.RWIdx, IsRead);

  SmallVectorImpl<SmallVector<unsigned,4>> &RWSequences = IsRead
    ? Trans.ReadSequences : Trans.WriteSequences;
  if (SchedRW.IsVariadic) {
    unsigned OperIdx = RWSequences.size()-1;
    // Make N-1 copies of this transition's last sequence.
    RWSequences.insert(RWSequences.end(), SelectedRWs.size() - 1,
                       RWSequences[OperIdx]);
    // Push each of the N elements of the SelectedRWs onto a copy of the last
    // sequence (split the current operand into N operands).
    // Note that write sequences should be expanded within this loop--the entire
    // sequence belongs to a single operand.
    for (IdxIter RWI = SelectedRWs.begin(), RWE = SelectedRWs.end();
         RWI != RWE; ++RWI, ++OperIdx) {
      IdxVec ExpandedRWs;
      if (IsRead)
        ExpandedRWs.push_back(*RWI);
      else
        SchedModels.expandRWSequence(*RWI, ExpandedRWs, IsRead);
      RWSequences[OperIdx].insert(RWSequences[OperIdx].end(),
                                  ExpandedRWs.begin(), ExpandedRWs.end());
    }
    assert(OperIdx == RWSequences.size() && "missed a sequence");
  }
  else {
    // Push this transition's expanded sequence onto this transition's last
    // sequence (add to the current operand's sequence).
    SmallVectorImpl<unsigned> &Seq = RWSequences.back();
    IdxVec ExpandedRWs;
    for (IdxIter RWI = SelectedRWs.begin(), RWE = SelectedRWs.end();
         RWI != RWE; ++RWI) {
      if (IsRead)
        ExpandedRWs.push_back(*RWI);
      else
        SchedModels.expandRWSequence(*RWI, ExpandedRWs, IsRead);
    }
    Seq.insert(Seq.end(), ExpandedRWs.begin(), ExpandedRWs.end());
  }
}

// RWSeq is a sequence of all Reads or all Writes for the next read or write
// operand. StartIdx is an index into TransVec where partial results
// starts. RWSeq must be applied to all transitions between StartIdx and the end
// of TransVec.
void PredTransitions::substituteVariantOperand(
  const SmallVectorImpl<unsigned> &RWSeq, bool IsRead, unsigned StartIdx) {

  // Visit each original RW within the current sequence.
  for (SmallVectorImpl<unsigned>::const_iterator
         RWI = RWSeq.begin(), RWE = RWSeq.end(); RWI != RWE; ++RWI) {
    const CodeGenSchedRW &SchedRW = SchedModels.getSchedRW(*RWI, IsRead);
    // Push this RW on all partial PredTransitions or distribute variants.
    // New PredTransitions may be pushed within this loop which should not be
    // revisited (TransEnd must be loop invariant).
    for (unsigned TransIdx = StartIdx, TransEnd = TransVec.size();
         TransIdx != TransEnd; ++TransIdx) {
      // In the common case, push RW onto the current operand's sequence.
      if (!hasAliasedVariants(SchedRW, SchedModels)) {
        if (IsRead)
          TransVec[TransIdx].ReadSequences.back().push_back(*RWI);
        else
          TransVec[TransIdx].WriteSequences.back().push_back(*RWI);
        continue;
      }
      // Distribute this partial PredTransition across intersecting variants.
      // This will push a copies of TransVec[TransIdx] on the back of TransVec.
      std::vector<TransVariant> IntersectingVariants;
      getIntersectingVariants(SchedRW, TransIdx, IntersectingVariants);
      // Now expand each variant on top of its copy of the transition.
      for (std::vector<TransVariant>::const_iterator
             IVI = IntersectingVariants.begin(),
             IVE = IntersectingVariants.end();
           IVI != IVE; ++IVI) {
        pushVariant(*IVI, IsRead);
      }
    }
  }
}

// For each variant of a Read/Write in Trans, substitute the sequence of
// Read/Writes guarded by the variant. This is exponential in the number of
// variant Read/Writes, but in practice detection of mutually exclusive
// predicates should result in linear growth in the total number variants.
//
// This is one step in a breadth-first search of nested variants.
void PredTransitions::substituteVariants(const PredTransition &Trans) {
  // Build up a set of partial results starting at the back of
  // PredTransitions. Remember the first new transition.
  unsigned StartIdx = TransVec.size();
  TransVec.emplace_back();
  TransVec.back().PredTerm = Trans.PredTerm;
  TransVec.back().ProcIndices = Trans.ProcIndices;

  // Visit each original write sequence.
  for (SmallVectorImpl<SmallVector<unsigned,4>>::const_iterator
         WSI = Trans.WriteSequences.begin(), WSE = Trans.WriteSequences.end();
       WSI != WSE; ++WSI) {
    // Push a new (empty) write sequence onto all partial Transitions.
    for (std::vector<PredTransition>::iterator I =
           TransVec.begin() + StartIdx, E = TransVec.end(); I != E; ++I) {
      I->WriteSequences.emplace_back();
    }
    substituteVariantOperand(*WSI, /*IsRead=*/false, StartIdx);
  }
  // Visit each original read sequence.
  for (SmallVectorImpl<SmallVector<unsigned,4>>::const_iterator
         RSI = Trans.ReadSequences.begin(), RSE = Trans.ReadSequences.end();
       RSI != RSE; ++RSI) {
    // Push a new (empty) read sequence onto all partial Transitions.
    for (std::vector<PredTransition>::iterator I =
           TransVec.begin() + StartIdx, E = TransVec.end(); I != E; ++I) {
      I->ReadSequences.emplace_back();
    }
    substituteVariantOperand(*RSI, /*IsRead=*/true, StartIdx);
  }
}

// Create a new SchedClass for each variant found by inferFromRW. Pass
static void inferFromTransitions(ArrayRef<PredTransition> LastTransitions,
                                 unsigned FromClassIdx,
                                 CodeGenSchedModels &SchedModels) {
  // For each PredTransition, create a new CodeGenSchedTransition, which usually
  // requires creating a new SchedClass.
  for (ArrayRef<PredTransition>::iterator
         I = LastTransitions.begin(), E = LastTransitions.end(); I != E; ++I) {
    IdxVec OperWritesVariant;
    transform(I->WriteSequences, std::back_inserter(OperWritesVariant),
              [&SchedModels](ArrayRef<unsigned> WS) {
                return SchedModels.findOrInsertRW(WS, /*IsRead=*/false);
              });
    IdxVec OperReadsVariant;
    transform(I->ReadSequences, std::back_inserter(OperReadsVariant),
              [&SchedModels](ArrayRef<unsigned> RS) {
                return SchedModels.findOrInsertRW(RS, /*IsRead=*/true);
              });
    CodeGenSchedTransition SCTrans;
    SCTrans.ToClassIdx =
      SchedModels.addSchedClass(/*ItinClassDef=*/nullptr, OperWritesVariant,
                                OperReadsVariant, I->ProcIndices);
    SCTrans.ProcIndices.assign(I->ProcIndices.begin(), I->ProcIndices.end());
    // The final PredTerm is unique set of predicates guarding the transition.
    RecVec Preds;
    transform(I->PredTerm, std::back_inserter(Preds),
              [](const PredCheck &P) {
                return P.Predicate;
              });
    Preds.erase(std::unique(Preds.begin(), Preds.end()), Preds.end());
    SCTrans.PredTerm = std::move(Preds);
    SchedModels.getSchedClass(FromClassIdx)
        .Transitions.push_back(std::move(SCTrans));
  }
}

// Create new SchedClasses for the given ReadWrite list. If any of the
// ReadWrites refers to a SchedVariant, create a new SchedClass for each variant
// of the ReadWrite list, following Aliases if necessary.
void CodeGenSchedModels::inferFromRW(ArrayRef<unsigned> OperWrites,
                                     ArrayRef<unsigned> OperReads,
                                     unsigned FromClassIdx,
                                     ArrayRef<unsigned> ProcIndices) {
  LLVM_DEBUG(dbgs() << "INFER RW proc("; dumpIdxVec(ProcIndices);
             dbgs() << ") ");

  // Create a seed transition with an empty PredTerm and the expanded sequences
  // of SchedWrites for the current SchedClass.
  std::vector<PredTransition> LastTransitions;
  LastTransitions.emplace_back();
  LastTransitions.back().ProcIndices.append(ProcIndices.begin(),
                                            ProcIndices.end());

  for (unsigned WriteIdx : OperWrites) {
    IdxVec WriteSeq;
    expandRWSequence(WriteIdx, WriteSeq, /*IsRead=*/false);
    LastTransitions[0].WriteSequences.emplace_back();
    SmallVectorImpl<unsigned> &Seq = LastTransitions[0].WriteSequences.back();
    Seq.append(WriteSeq.begin(), WriteSeq.end());
    LLVM_DEBUG(dbgs() << "("; dumpIdxVec(Seq); dbgs() << ") ");
  }
  LLVM_DEBUG(dbgs() << " Reads: ");
  for (unsigned ReadIdx : OperReads) {
    IdxVec ReadSeq;
    expandRWSequence(ReadIdx, ReadSeq, /*IsRead=*/true);
    LastTransitions[0].ReadSequences.emplace_back();
    SmallVectorImpl<unsigned> &Seq = LastTransitions[0].ReadSequences.back();
    Seq.append(ReadSeq.begin(), ReadSeq.end());
    LLVM_DEBUG(dbgs() << "("; dumpIdxVec(Seq); dbgs() << ") ");
  }
  LLVM_DEBUG(dbgs() << '\n');

  // Collect all PredTransitions for individual operands.
  // Iterate until no variant writes remain.
  while (hasVariant(LastTransitions, *this)) {
    PredTransitions Transitions(*this);
    for (const PredTransition &Trans : LastTransitions)
      Transitions.substituteVariants(Trans);
    LLVM_DEBUG(Transitions.dump());
    LastTransitions.swap(Transitions.TransVec);
  }
  // If the first transition has no variants, nothing to do.
  if (LastTransitions[0].PredTerm.empty())
    return;

  // WARNING: We are about to mutate the SchedClasses vector. Do not refer to
  // OperWrites, OperReads, or ProcIndices after calling inferFromTransitions.
  inferFromTransitions(LastTransitions, FromClassIdx, *this);
}

// Check if any processor resource group contains all resource records in
// SubUnits.
bool CodeGenSchedModels::hasSuperGroup(RecVec &SubUnits, CodeGenProcModel &PM) {
  for (unsigned i = 0, e = PM.ProcResourceDefs.size(); i < e; ++i) {
    if (!PM.ProcResourceDefs[i]->isSubClassOf("ProcResGroup"))
      continue;
    RecVec SuperUnits =
      PM.ProcResourceDefs[i]->getValueAsListOfDefs("Resources");
    RecIter RI = SubUnits.begin(), RE = SubUnits.end();
    for ( ; RI != RE; ++RI) {
      if (!is_contained(SuperUnits, *RI)) {
        break;
      }
    }
    if (RI == RE)
      return true;
  }
  return false;
}

// Verify that overlapping groups have a common supergroup.
void CodeGenSchedModels::verifyProcResourceGroups(CodeGenProcModel &PM) {
  for (unsigned i = 0, e = PM.ProcResourceDefs.size(); i < e; ++i) {
    if (!PM.ProcResourceDefs[i]->isSubClassOf("ProcResGroup"))
      continue;
    RecVec CheckUnits =
      PM.ProcResourceDefs[i]->getValueAsListOfDefs("Resources");
    for (unsigned j = i+1; j < e; ++j) {
      if (!PM.ProcResourceDefs[j]->isSubClassOf("ProcResGroup"))
        continue;
      RecVec OtherUnits =
        PM.ProcResourceDefs[j]->getValueAsListOfDefs("Resources");
      if (std::find_first_of(CheckUnits.begin(), CheckUnits.end(),
                             OtherUnits.begin(), OtherUnits.end())
          != CheckUnits.end()) {
        // CheckUnits and OtherUnits overlap
        OtherUnits.insert(OtherUnits.end(), CheckUnits.begin(),
                          CheckUnits.end());
        if (!hasSuperGroup(OtherUnits, PM)) {
          PrintFatalError((PM.ProcResourceDefs[i])->getLoc(),
                          "proc resource group overlaps with "
                          + PM.ProcResourceDefs[j]->getName()
                          + " but no supergroup contains both.");
        }
      }
    }
  }
}

// Collect all the RegisterFile definitions available in this target.
void CodeGenSchedModels::collectRegisterFiles() {
  RecVec RegisterFileDefs = Records.getAllDerivedDefinitions("RegisterFile");

  // RegisterFiles is the vector of CodeGenRegisterFile.
  for (Record *RF : RegisterFileDefs) {
    // For each register file definition, construct a CodeGenRegisterFile object
    // and add it to the appropriate scheduling model.
    CodeGenProcModel &PM = getProcModel(RF->getValueAsDef("SchedModel"));
    PM.RegisterFiles.emplace_back(CodeGenRegisterFile(RF->getName(),RF));
    CodeGenRegisterFile &CGRF = PM.RegisterFiles.back();
    CGRF.MaxMovesEliminatedPerCycle =
        RF->getValueAsInt("MaxMovesEliminatedPerCycle");
    CGRF.AllowZeroMoveEliminationOnly =
        RF->getValueAsBit("AllowZeroMoveEliminationOnly");

    // Now set the number of physical registers as well as the cost of registers
    // in each register class.
    CGRF.NumPhysRegs = RF->getValueAsInt("NumPhysRegs");
    if (!CGRF.NumPhysRegs) {
      PrintFatalError(RF->getLoc(),
                      "Invalid RegisterFile with zero physical registers");
    }

    RecVec RegisterClasses = RF->getValueAsListOfDefs("RegClasses");
    std::vector<int64_t> RegisterCosts = RF->getValueAsListOfInts("RegCosts");
    ListInit *MoveElimInfo = RF->getValueAsListInit("AllowMoveElimination");
    for (unsigned I = 0, E = RegisterClasses.size(); I < E; ++I) {
      int Cost = RegisterCosts.size() > I ? RegisterCosts[I] : 1;

      bool AllowMoveElim = false;
      if (MoveElimInfo->size() > I) {
        BitInit *Val = cast<BitInit>(MoveElimInfo->getElement(I));
        AllowMoveElim = Val->getValue();
      }

      CGRF.Costs.emplace_back(RegisterClasses[I], Cost, AllowMoveElim);
    }
  }
}

// Collect and sort WriteRes, ReadAdvance, and ProcResources.
void CodeGenSchedModels::collectProcResources() {
  ProcResourceDefs = Records.getAllDerivedDefinitions("ProcResourceUnits");
  ProcResGroups = Records.getAllDerivedDefinitions("ProcResGroup");

  // Add any subtarget-specific SchedReadWrites that are directly associated
  // with processor resources. Refer to the parent SchedClass's ProcIndices to
  // determine which processors they apply to.
  for (const CodeGenSchedClass &SC :
       make_range(schedClassBegin(), schedClassEnd())) {
    if (SC.ItinClassDef) {
      collectItinProcResources(SC.ItinClassDef);
      continue;
    }

    // This class may have a default ReadWrite list which can be overriden by
    // InstRW definitions.
    for (Record *RW : SC.InstRWs) {
      Record *RWModelDef = RW->getValueAsDef("SchedModel");
      unsigned PIdx = getProcModel(RWModelDef).Index;
      IdxVec Writes, Reads;
      findRWs(RW->getValueAsListOfDefs("OperandReadWrites"), Writes, Reads);
      collectRWResources(Writes, Reads, PIdx);
    }

    collectRWResources(SC.Writes, SC.Reads, SC.ProcIndices);
  }
  // Add resources separately defined by each subtarget.
  RecVec WRDefs = Records.getAllDerivedDefinitions("WriteRes");
  for (Record *WR : WRDefs) {
    Record *ModelDef = WR->getValueAsDef("SchedModel");
    addWriteRes(WR, getProcModel(ModelDef).Index);
  }
  RecVec SWRDefs = Records.getAllDerivedDefinitions("SchedWriteRes");
  for (Record *SWR : SWRDefs) {
    Record *ModelDef = SWR->getValueAsDef("SchedModel");
    addWriteRes(SWR, getProcModel(ModelDef).Index);
  }
  RecVec RADefs = Records.getAllDerivedDefinitions("ReadAdvance");
  for (Record *RA : RADefs) {
    Record *ModelDef = RA->getValueAsDef("SchedModel");
    addReadAdvance(RA, getProcModel(ModelDef).Index);
  }
  RecVec SRADefs = Records.getAllDerivedDefinitions("SchedReadAdvance");
  for (Record *SRA : SRADefs) {
    if (SRA->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = SRA->getValueAsDef("SchedModel");
      addReadAdvance(SRA, getProcModel(ModelDef).Index);
    }
  }
  // Add ProcResGroups that are defined within this processor model, which may
  // not be directly referenced but may directly specify a buffer size.
  RecVec ProcResGroups = Records.getAllDerivedDefinitions("ProcResGroup");
  for (Record *PRG : ProcResGroups) {
    if (!PRG->getValueInit("SchedModel")->isComplete())
      continue;
    CodeGenProcModel &PM = getProcModel(PRG->getValueAsDef("SchedModel"));
    if (!is_contained(PM.ProcResourceDefs, PRG))
      PM.ProcResourceDefs.push_back(PRG);
  }
  // Add ProcResourceUnits unconditionally.
  for (Record *PRU : Records.getAllDerivedDefinitions("ProcResourceUnits")) {
    if (!PRU->getValueInit("SchedModel")->isComplete())
      continue;
    CodeGenProcModel &PM = getProcModel(PRU->getValueAsDef("SchedModel"));
    if (!is_contained(PM.ProcResourceDefs, PRU))
      PM.ProcResourceDefs.push_back(PRU);
  }
  // Finalize each ProcModel by sorting the record arrays.
  for (CodeGenProcModel &PM : ProcModels) {
    llvm::sort(PM.WriteResDefs, LessRecord());
    llvm::sort(PM.ReadAdvanceDefs, LessRecord());
    llvm::sort(PM.ProcResourceDefs, LessRecord());
    LLVM_DEBUG(
        PM.dump();
        dbgs() << "WriteResDefs: "; for (RecIter RI = PM.WriteResDefs.begin(),
                                         RE = PM.WriteResDefs.end();
                                         RI != RE; ++RI) {
          if ((*RI)->isSubClassOf("WriteRes"))
            dbgs() << (*RI)->getValueAsDef("WriteType")->getName() << " ";
          else
            dbgs() << (*RI)->getName() << " ";
        } dbgs() << "\nReadAdvanceDefs: ";
        for (RecIter RI = PM.ReadAdvanceDefs.begin(),
             RE = PM.ReadAdvanceDefs.end();
             RI != RE; ++RI) {
          if ((*RI)->isSubClassOf("ReadAdvance"))
            dbgs() << (*RI)->getValueAsDef("ReadType")->getName() << " ";
          else
            dbgs() << (*RI)->getName() << " ";
        } dbgs()
        << "\nProcResourceDefs: ";
        for (RecIter RI = PM.ProcResourceDefs.begin(),
             RE = PM.ProcResourceDefs.end();
             RI != RE; ++RI) { dbgs() << (*RI)->getName() << " "; } dbgs()
        << '\n');
    verifyProcResourceGroups(PM);
  }

  ProcResourceDefs.clear();
  ProcResGroups.clear();
}

void CodeGenSchedModels::checkCompleteness() {
  bool Complete = true;
  bool HadCompleteModel = false;
  for (const CodeGenProcModel &ProcModel : procModels()) {
    const bool HasItineraries = ProcModel.hasItineraries();
    if (!ProcModel.ModelDef->getValueAsBit("CompleteModel"))
      continue;
    for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue()) {
      if (Inst->hasNoSchedulingInfo)
        continue;
      if (ProcModel.isUnsupported(*Inst))
        continue;
      unsigned SCIdx = getSchedClassIdx(*Inst);
      if (!SCIdx) {
        if (Inst->TheDef->isValueUnset("SchedRW") && !HadCompleteModel) {
          PrintError("No schedule information for instruction '"
                     + Inst->TheDef->getName() + "'");
          Complete = false;
        }
        continue;
      }

      const CodeGenSchedClass &SC = getSchedClass(SCIdx);
      if (!SC.Writes.empty())
        continue;
      if (HasItineraries && SC.ItinClassDef != nullptr &&
          SC.ItinClassDef->getName() != "NoItinerary")
        continue;

      const RecVec &InstRWs = SC.InstRWs;
      auto I = find_if(InstRWs, [&ProcModel](const Record *R) {
        return R->getValueAsDef("SchedModel") == ProcModel.ModelDef;
      });
      if (I == InstRWs.end()) {
        PrintError("'" + ProcModel.ModelName + "' lacks information for '" +
                   Inst->TheDef->getName() + "'");
        Complete = false;
      }
    }
    HadCompleteModel = true;
  }
  if (!Complete) {
    errs() << "\n\nIncomplete schedule models found.\n"
      << "- Consider setting 'CompleteModel = 0' while developing new models.\n"
      << "- Pseudo instructions can be marked with 'hasNoSchedulingInfo = 1'.\n"
      << "- Instructions should usually have Sched<[...]> as a superclass, "
         "you may temporarily use an empty list.\n"
      << "- Instructions related to unsupported features can be excluded with "
         "list<Predicate> UnsupportedFeatures = [HasA,..,HasY]; in the "
         "processor model.\n\n";
    PrintFatalError("Incomplete schedule model");
  }
}

// Collect itinerary class resources for each processor.
void CodeGenSchedModels::collectItinProcResources(Record *ItinClassDef) {
  for (unsigned PIdx = 0, PEnd = ProcModels.size(); PIdx != PEnd; ++PIdx) {
    const CodeGenProcModel &PM = ProcModels[PIdx];
    // For all ItinRW entries.
    bool HasMatch = false;
    for (RecIter II = PM.ItinRWDefs.begin(), IE = PM.ItinRWDefs.end();
         II != IE; ++II) {
      RecVec Matched = (*II)->getValueAsListOfDefs("MatchedItinClasses");
      if (!std::count(Matched.begin(), Matched.end(), ItinClassDef))
        continue;
      if (HasMatch)
        PrintFatalError((*II)->getLoc(), "Duplicate itinerary class "
                        + ItinClassDef->getName()
                        + " in ItinResources for " + PM.ModelName);
      HasMatch = true;
      IdxVec Writes, Reads;
      findRWs((*II)->getValueAsListOfDefs("OperandReadWrites"), Writes, Reads);
      collectRWResources(Writes, Reads, PIdx);
    }
  }
}

void CodeGenSchedModels::collectRWResources(unsigned RWIdx, bool IsRead,
                                            ArrayRef<unsigned> ProcIndices) {
  const CodeGenSchedRW &SchedRW = getSchedRW(RWIdx, IsRead);
  if (SchedRW.TheDef) {
    if (!IsRead && SchedRW.TheDef->isSubClassOf("SchedWriteRes")) {
      for (unsigned Idx : ProcIndices)
        addWriteRes(SchedRW.TheDef, Idx);
    }
    else if (IsRead && SchedRW.TheDef->isSubClassOf("SchedReadAdvance")) {
      for (unsigned Idx : ProcIndices)
        addReadAdvance(SchedRW.TheDef, Idx);
    }
  }
  for (RecIter AI = SchedRW.Aliases.begin(), AE = SchedRW.Aliases.end();
       AI != AE; ++AI) {
    IdxVec AliasProcIndices;
    if ((*AI)->getValueInit("SchedModel")->isComplete()) {
      AliasProcIndices.push_back(
        getProcModel((*AI)->getValueAsDef("SchedModel")).Index);
    }
    else
      AliasProcIndices = ProcIndices;
    const CodeGenSchedRW &AliasRW = getSchedRW((*AI)->getValueAsDef("AliasRW"));
    assert(AliasRW.IsRead == IsRead && "cannot alias reads to writes");

    IdxVec ExpandedRWs;
    expandRWSequence(AliasRW.Index, ExpandedRWs, IsRead);
    for (IdxIter SI = ExpandedRWs.begin(), SE = ExpandedRWs.end();
         SI != SE; ++SI) {
      collectRWResources(*SI, IsRead, AliasProcIndices);
    }
  }
}

// Collect resources for a set of read/write types and processor indices.
void CodeGenSchedModels::collectRWResources(ArrayRef<unsigned> Writes,
                                            ArrayRef<unsigned> Reads,
                                            ArrayRef<unsigned> ProcIndices) {
  for (unsigned Idx : Writes)
    collectRWResources(Idx, /*IsRead=*/false, ProcIndices);

  for (unsigned Idx : Reads)
    collectRWResources(Idx, /*IsRead=*/true, ProcIndices);
}

// Find the processor's resource units for this kind of resource.
Record *CodeGenSchedModels::findProcResUnits(Record *ProcResKind,
                                             const CodeGenProcModel &PM,
                                             ArrayRef<SMLoc> Loc) const {
  if (ProcResKind->isSubClassOf("ProcResourceUnits"))
    return ProcResKind;

  Record *ProcUnitDef = nullptr;
  assert(!ProcResourceDefs.empty());
  assert(!ProcResGroups.empty());

  for (Record *ProcResDef : ProcResourceDefs) {
    if (ProcResDef->getValueAsDef("Kind") == ProcResKind
        && ProcResDef->getValueAsDef("SchedModel") == PM.ModelDef) {
      if (ProcUnitDef) {
        PrintFatalError(Loc,
                        "Multiple ProcessorResourceUnits associated with "
                        + ProcResKind->getName());
      }
      ProcUnitDef = ProcResDef;
    }
  }
  for (Record *ProcResGroup : ProcResGroups) {
    if (ProcResGroup == ProcResKind
        && ProcResGroup->getValueAsDef("SchedModel") == PM.ModelDef) {
      if (ProcUnitDef) {
        PrintFatalError(Loc,
                        "Multiple ProcessorResourceUnits associated with "
                        + ProcResKind->getName());
      }
      ProcUnitDef = ProcResGroup;
    }
  }
  if (!ProcUnitDef) {
    PrintFatalError(Loc,
                    "No ProcessorResources associated with "
                    + ProcResKind->getName());
  }
  return ProcUnitDef;
}

// Iteratively add a resource and its super resources.
void CodeGenSchedModels::addProcResource(Record *ProcResKind,
                                         CodeGenProcModel &PM,
                                         ArrayRef<SMLoc> Loc) {
  while (true) {
    Record *ProcResUnits = findProcResUnits(ProcResKind, PM, Loc);

    // See if this ProcResource is already associated with this processor.
    if (is_contained(PM.ProcResourceDefs, ProcResUnits))
      return;

    PM.ProcResourceDefs.push_back(ProcResUnits);
    if (ProcResUnits->isSubClassOf("ProcResGroup"))
      return;

    if (!ProcResUnits->getValueInit("Super")->isComplete())
      return;

    ProcResKind = ProcResUnits->getValueAsDef("Super");
  }
}

// Add resources for a SchedWrite to this processor if they don't exist.
void CodeGenSchedModels::addWriteRes(Record *ProcWriteResDef, unsigned PIdx) {
  assert(PIdx && "don't add resources to an invalid Processor model");

  RecVec &WRDefs = ProcModels[PIdx].WriteResDefs;
  if (is_contained(WRDefs, ProcWriteResDef))
    return;
  WRDefs.push_back(ProcWriteResDef);

  // Visit ProcResourceKinds referenced by the newly discovered WriteRes.
  RecVec ProcResDefs = ProcWriteResDef->getValueAsListOfDefs("ProcResources");
  for (RecIter WritePRI = ProcResDefs.begin(), WritePRE = ProcResDefs.end();
       WritePRI != WritePRE; ++WritePRI) {
    addProcResource(*WritePRI, ProcModels[PIdx], ProcWriteResDef->getLoc());
  }
}

// Add resources for a ReadAdvance to this processor if they don't exist.
void CodeGenSchedModels::addReadAdvance(Record *ProcReadAdvanceDef,
                                        unsigned PIdx) {
  RecVec &RADefs = ProcModels[PIdx].ReadAdvanceDefs;
  if (is_contained(RADefs, ProcReadAdvanceDef))
    return;
  RADefs.push_back(ProcReadAdvanceDef);
}

unsigned CodeGenProcModel::getProcResourceIdx(Record *PRDef) const {
  RecIter PRPos = find(ProcResourceDefs, PRDef);
  if (PRPos == ProcResourceDefs.end())
    PrintFatalError(PRDef->getLoc(), "ProcResource def is not included in "
                    "the ProcResources list for " + ModelName);
  // Idx=0 is reserved for invalid.
  return 1 + (PRPos - ProcResourceDefs.begin());
}

bool CodeGenProcModel::isUnsupported(const CodeGenInstruction &Inst) const {
  for (const Record *TheDef : UnsupportedFeaturesDefs) {
    for (const Record *PredDef : Inst.TheDef->getValueAsListOfDefs("Predicates")) {
      if (TheDef->getName() == PredDef->getName())
        return true;
    }
  }
  return false;
}

#ifndef NDEBUG
void CodeGenProcModel::dump() const {
  dbgs() << Index << ": " << ModelName << " "
         << (ModelDef ? ModelDef->getName() : "inferred") << " "
         << (ItinsDef ? ItinsDef->getName() : "no itinerary") << '\n';
}

void CodeGenSchedRW::dump() const {
  dbgs() << Name << (IsVariadic ? " (V) " : " ");
  if (IsSequence) {
    dbgs() << "(";
    dumpIdxVec(Sequence);
    dbgs() << ")";
  }
}

void CodeGenSchedClass::dump(const CodeGenSchedModels* SchedModels) const {
  dbgs() << "SCHEDCLASS " << Index << ":" << Name << '\n'
         << "  Writes: ";
  for (unsigned i = 0, N = Writes.size(); i < N; ++i) {
    SchedModels->getSchedWrite(Writes[i]).dump();
    if (i < N-1) {
      dbgs() << '\n';
      dbgs().indent(10);
    }
  }
  dbgs() << "\n  Reads: ";
  for (unsigned i = 0, N = Reads.size(); i < N; ++i) {
    SchedModels->getSchedRead(Reads[i]).dump();
    if (i < N-1) {
      dbgs() << '\n';
      dbgs().indent(10);
    }
  }
  dbgs() << "\n  ProcIdx: "; dumpIdxVec(ProcIndices); dbgs() << '\n';
  if (!Transitions.empty()) {
    dbgs() << "\n Transitions for Proc ";
    for (const CodeGenSchedTransition &Transition : Transitions) {
      dumpIdxVec(Transition.ProcIndices);
    }
  }
}

void PredTransitions::dump() const {
  dbgs() << "Expanded Variants:\n";
  for (std::vector<PredTransition>::const_iterator
         TI = TransVec.begin(), TE = TransVec.end(); TI != TE; ++TI) {
    dbgs() << "{";
    for (SmallVectorImpl<PredCheck>::const_iterator
           PCI = TI->PredTerm.begin(), PCE = TI->PredTerm.end();
         PCI != PCE; ++PCI) {
      if (PCI != TI->PredTerm.begin())
        dbgs() << ", ";
      dbgs() << SchedModels.getSchedRW(PCI->RWIdx, PCI->IsRead).Name
             << ":" << PCI->Predicate->getName();
    }
    dbgs() << "},\n  => {";
    for (SmallVectorImpl<SmallVector<unsigned,4>>::const_iterator
           WSI = TI->WriteSequences.begin(), WSE = TI->WriteSequences.end();
         WSI != WSE; ++WSI) {
      dbgs() << "(";
      for (SmallVectorImpl<unsigned>::const_iterator
             WI = WSI->begin(), WE = WSI->end(); WI != WE; ++WI) {
        if (WI != WSI->begin())
          dbgs() << ", ";
        dbgs() << SchedModels.getSchedWrite(*WI).Name;
      }
      dbgs() << "),";
    }
    dbgs() << "}\n";
  }
}
#endif // NDEBUG
