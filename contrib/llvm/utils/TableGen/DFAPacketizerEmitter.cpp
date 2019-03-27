//===- DFAPacketizerEmitter.cpp - Packetization DFA for a VLIW machine ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class parses the Schedule.td file and produces an API that can be used
// to reason about whether an instruction can be added to a packet on a VLIW
// architecture. The class internally generates a deterministic finite
// automaton (DFA) that models all possible mappings of machine instructions
// to functional units as instructions are added to a packet.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "dfa-emitter"

#include "CodeGenTarget.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

// --------------------------------------------------------------------
// Definitions shared between DFAPacketizer.cpp and DFAPacketizerEmitter.cpp

// DFA_MAX_RESTERMS * DFA_MAX_RESOURCES must fit within sizeof DFAInput.
// This is verified in DFAPacketizer.cpp:DFAPacketizer::DFAPacketizer.
//
// e.g. terms x resource bit combinations that fit in uint32_t:
//      4 terms x 8  bits = 32 bits
//      3 terms x 10 bits = 30 bits
//      2 terms x 16 bits = 32 bits
//
// e.g. terms x resource bit combinations that fit in uint64_t:
//      8 terms x 8  bits = 64 bits
//      7 terms x 9  bits = 63 bits
//      6 terms x 10 bits = 60 bits
//      5 terms x 12 bits = 60 bits
//      4 terms x 16 bits = 64 bits <--- current
//      3 terms x 21 bits = 63 bits
//      2 terms x 32 bits = 64 bits
//
#define DFA_MAX_RESTERMS        4   // The max # of AND'ed resource terms.
#define DFA_MAX_RESOURCES       16  // The max # of resource bits in one term.

typedef uint64_t                DFAInput;
typedef int64_t                 DFAStateInput;
#define DFA_TBLTYPE             "int64_t" // For generating DFAStateInputTable.

namespace {

  DFAInput addDFAFuncUnits(DFAInput Inp, unsigned FuncUnits) {
    return (Inp << DFA_MAX_RESOURCES) | FuncUnits;
  }

  /// Return the DFAInput for an instruction class input vector.
  /// This function is used in both DFAPacketizer.cpp and in
  /// DFAPacketizerEmitter.cpp.
  DFAInput getDFAInsnInput(const std::vector<unsigned> &InsnClass) {
    DFAInput InsnInput = 0;
    assert((InsnClass.size() <= DFA_MAX_RESTERMS) &&
           "Exceeded maximum number of DFA terms");
    for (auto U : InsnClass)
      InsnInput = addDFAFuncUnits(InsnInput, U);
    return InsnInput;
  }

} // end anonymous namespace

// --------------------------------------------------------------------

#ifndef NDEBUG
// To enable debugging, run llvm-tblgen with: "-debug-only dfa-emitter".
//
// dbgsInsnClass - When debugging, print instruction class stages.
//
void dbgsInsnClass(const std::vector<unsigned> &InsnClass);
//
// dbgsStateInfo - When debugging, print the set of state info.
//
void dbgsStateInfo(const std::set<unsigned> &stateInfo);
//
// dbgsIndent - When debugging, indent by the specified amount.
//
void dbgsIndent(unsigned indent);
#endif

//
// class DFAPacketizerEmitter: class that generates and prints out the DFA
// for resource tracking.
//
namespace {

class DFAPacketizerEmitter {
private:
  std::string TargetName;
  //
  // allInsnClasses is the set of all possible resources consumed by an
  // InstrStage.
  //
  std::vector<std::vector<unsigned>> allInsnClasses;
  RecordKeeper &Records;

public:
  DFAPacketizerEmitter(RecordKeeper &R);

  //
  // collectAllFuncUnits - Construct a map of function unit names to bits.
  //
  int collectAllFuncUnits(std::vector<Record*> &ProcItinList,
                           std::map<std::string, unsigned> &FUNameToBitsMap,
                           int &maxResources,
                           raw_ostream &OS);

  //
  // collectAllComboFuncs - Construct a map from a combo function unit bit to
  //                        the bits of all included functional units.
  //
  int collectAllComboFuncs(std::vector<Record*> &ComboFuncList,
                           std::map<std::string, unsigned> &FUNameToBitsMap,
                           std::map<unsigned, unsigned> &ComboBitToBitsMap,
                           raw_ostream &OS);

  //
  // collectOneInsnClass - Populate allInsnClasses with one instruction class.
  //
  int collectOneInsnClass(const std::string &ProcName,
                           std::vector<Record*> &ProcItinList,
                           std::map<std::string, unsigned> &FUNameToBitsMap,
                           Record *ItinData,
                           raw_ostream &OS);

  //
  // collectAllInsnClasses - Populate allInsnClasses which is a set of units
  // used in each stage.
  //
  int collectAllInsnClasses(const std::string &ProcName,
                           std::vector<Record*> &ProcItinList,
                           std::map<std::string, unsigned> &FUNameToBitsMap,
                           std::vector<Record*> &ItinDataList,
                           int &maxStages,
                           raw_ostream &OS);

  void run(raw_ostream &OS);
};

//
// State represents the usage of machine resources if the packet contains
// a set of instruction classes.
//
// Specifically, currentState is a set of bit-masks.
// The nth bit in a bit-mask indicates whether the nth resource is being used
// by this state. The set of bit-masks in a state represent the different
// possible outcomes of transitioning to this state.
// For example: consider a two resource architecture: resource L and resource M
// with three instruction classes: L, M, and L_or_M.
// From the initial state (currentState = 0x00), if we add instruction class
// L_or_M we will transition to a state with currentState = [0x01, 0x10]. This
// represents the possible resource states that can result from adding a L_or_M
// instruction
//
// Another way of thinking about this transition is we are mapping a NDFA with
// two states [0x01] and [0x10] into a DFA with a single state [0x01, 0x10].
//
// A State instance also contains a collection of transitions from that state:
// a map from inputs to new states.
//
class State {
 public:
  static int currentStateNum;
  // stateNum is the only member used for equality/ordering, all other members
  // can be mutated even in const State objects.
  const int stateNum;
  mutable bool isInitial;
  mutable std::set<unsigned> stateInfo;
  typedef std::map<std::vector<unsigned>, const State *> TransitionMap;
  mutable TransitionMap Transitions;

  State();

  bool operator<(const State &s) const {
    return stateNum < s.stateNum;
  }

  //
  // canMaybeAddInsnClass - Quickly verifies if an instruction of type InsnClass
  // may be a valid transition from this state i.e., can an instruction of type
  // InsnClass be added to the packet represented by this state.
  //
  // Note that for multiple stages, this quick check does not take into account
  // any possible resource competition between the stages themselves.  That is
  // enforced in AddInsnClassStages which checks the cross product of all
  // stages for resource availability (which is a more involved check).
  //
  bool canMaybeAddInsnClass(std::vector<unsigned> &InsnClass,
                        std::map<unsigned, unsigned> &ComboBitToBitsMap) const;

  //
  // AddInsnClass - Return all combinations of resource reservation
  // which are possible from this state (PossibleStates).
  //
  // PossibleStates is the set of valid resource states that ensue from valid
  // transitions.
  //
  void AddInsnClass(std::vector<unsigned> &InsnClass,
                        std::map<unsigned, unsigned> &ComboBitToBitsMap,
                        std::set<unsigned> &PossibleStates) const;

  //
  // AddInsnClassStages - Return all combinations of resource reservation
  // resulting from the cross product of all stages for this InsnClass
  // which are possible from this state (PossibleStates).
  //
  void AddInsnClassStages(std::vector<unsigned> &InsnClass,
                        std::map<unsigned, unsigned> &ComboBitToBitsMap,
                        unsigned chkstage, unsigned numstages,
                        unsigned prevState, unsigned origState,
                        DenseSet<unsigned> &VisitedResourceStates,
                        std::set<unsigned> &PossibleStates) const;

  //
  // addTransition - Add a transition from this state given the input InsnClass
  //
  void addTransition(std::vector<unsigned> InsnClass, const State *To) const;

  //
  // hasTransition - Returns true if there is a transition from this state
  // given the input InsnClass
  //
  bool hasTransition(std::vector<unsigned> InsnClass) const;
};

//
// class DFA: deterministic finite automaton for processor resource tracking.
//
class DFA {
public:
  DFA() = default;

  // Set of states. Need to keep this sorted to emit the transition table.
  typedef std::set<State> StateSet;
  StateSet states;

  State *currentState = nullptr;

  //
  // Modify the DFA.
  //
  const State &newState();

  //
  // writeTable: Print out a table representing the DFA.
  //
  void writeTableAndAPI(raw_ostream &OS, const std::string &ClassName,
                 int numInsnClasses = 0,
                 int maxResources = 0, int numCombos = 0, int maxStages = 0);
};

} // end anonymous namespace

#ifndef NDEBUG
// To enable debugging, run llvm-tblgen with: "-debug-only dfa-emitter".
//
// dbgsInsnClass - When debugging, print instruction class stages.
//
void dbgsInsnClass(const std::vector<unsigned> &InsnClass) {
  LLVM_DEBUG(dbgs() << "InsnClass: ");
  for (unsigned i = 0; i < InsnClass.size(); ++i) {
    if (i > 0) {
      LLVM_DEBUG(dbgs() << ", ");
    }
    LLVM_DEBUG(dbgs() << "0x" << Twine::utohexstr(InsnClass[i]));
  }
  DFAInput InsnInput = getDFAInsnInput(InsnClass);
  LLVM_DEBUG(dbgs() << " (input: 0x" << Twine::utohexstr(InsnInput) << ")");
}

//
// dbgsStateInfo - When debugging, print the set of state info.
//
void dbgsStateInfo(const std::set<unsigned> &stateInfo) {
  LLVM_DEBUG(dbgs() << "StateInfo: ");
  unsigned i = 0;
  for (std::set<unsigned>::iterator SI = stateInfo.begin();
       SI != stateInfo.end(); ++SI, ++i) {
    unsigned thisState = *SI;
    if (i > 0) {
      LLVM_DEBUG(dbgs() << ", ");
    }
    LLVM_DEBUG(dbgs() << "0x" << Twine::utohexstr(thisState));
  }
}

//
// dbgsIndent - When debugging, indent by the specified amount.
//
void dbgsIndent(unsigned indent) {
  for (unsigned i = 0; i < indent; ++i) {
    LLVM_DEBUG(dbgs() << " ");
  }
}
#endif // NDEBUG

//
// Constructors and destructors for State and DFA
//
State::State() :
  stateNum(currentStateNum++), isInitial(false) {}

//
// addTransition - Add a transition from this state given the input InsnClass
//
void State::addTransition(std::vector<unsigned> InsnClass, const State *To)
      const {
  assert(!Transitions.count(InsnClass) &&
      "Cannot have multiple transitions for the same input");
  Transitions[InsnClass] = To;
}

//
// hasTransition - Returns true if there is a transition from this state
// given the input InsnClass
//
bool State::hasTransition(std::vector<unsigned> InsnClass) const {
  return Transitions.count(InsnClass) > 0;
}

//
// AddInsnClass - Return all combinations of resource reservation
// which are possible from this state (PossibleStates).
//
// PossibleStates is the set of valid resource states that ensue from valid
// transitions.
//
void State::AddInsnClass(std::vector<unsigned> &InsnClass,
                        std::map<unsigned, unsigned> &ComboBitToBitsMap,
                        std::set<unsigned> &PossibleStates) const {
  //
  // Iterate over all resource states in currentState.
  //
  unsigned numstages = InsnClass.size();
  assert((numstages > 0) && "InsnClass has no stages");

  for (std::set<unsigned>::iterator SI = stateInfo.begin();
       SI != stateInfo.end(); ++SI) {
    unsigned thisState = *SI;

    DenseSet<unsigned> VisitedResourceStates;

    LLVM_DEBUG(dbgs() << "  thisState: 0x" << Twine::utohexstr(thisState)
                      << "\n");
    AddInsnClassStages(InsnClass, ComboBitToBitsMap,
                                numstages - 1, numstages,
                                thisState, thisState,
                                VisitedResourceStates, PossibleStates);
  }
}

void State::AddInsnClassStages(std::vector<unsigned> &InsnClass,
                        std::map<unsigned, unsigned> &ComboBitToBitsMap,
                        unsigned chkstage, unsigned numstages,
                        unsigned prevState, unsigned origState,
                        DenseSet<unsigned> &VisitedResourceStates,
                        std::set<unsigned> &PossibleStates) const {
  assert((chkstage < numstages) && "AddInsnClassStages: stage out of range");
  unsigned thisStage = InsnClass[chkstage];

  LLVM_DEBUG({
    dbgsIndent((1 + numstages - chkstage) << 1);
    dbgs() << "AddInsnClassStages " << chkstage << " (0x"
           << Twine::utohexstr(thisStage) << ") from ";
    dbgsInsnClass(InsnClass);
    dbgs() << "\n";
  });

  //
  // Iterate over all possible resources used in thisStage.
  // For ex: for thisStage = 0x11, all resources = {0x01, 0x10}.
  //
  for (unsigned int j = 0; j < DFA_MAX_RESOURCES; ++j) {
    unsigned resourceMask = (0x1 << j);
    if (resourceMask & thisStage) {
      unsigned combo = ComboBitToBitsMap[resourceMask];
      if (combo && ((~prevState & combo) != combo)) {
        LLVM_DEBUG(dbgs() << "\tSkipped Add 0x" << Twine::utohexstr(prevState)
                          << " - combo op 0x" << Twine::utohexstr(resourceMask)
                          << " (0x" << Twine::utohexstr(combo)
                          << ") cannot be scheduled\n");
        continue;
      }
      //
      // For each possible resource used in thisStage, generate the
      // resource state if that resource was used.
      //
      unsigned ResultingResourceState = prevState | resourceMask | combo;
      LLVM_DEBUG({
        dbgsIndent((2 + numstages - chkstage) << 1);
        dbgs() << "0x" << Twine::utohexstr(prevState) << " | 0x"
               << Twine::utohexstr(resourceMask);
        if (combo)
          dbgs() << " | 0x" << Twine::utohexstr(combo);
        dbgs() << " = 0x" << Twine::utohexstr(ResultingResourceState) << " ";
      });

      //
      // If this is the final stage for this class
      //
      if (chkstage == 0) {
        //
        // Check if the resulting resource state can be accommodated in this
        // packet.
        // We compute resource OR prevState (originally started as origState).
        // If the result of the OR is different than origState, it implies
        // that there is at least one resource that can be used to schedule
        // thisStage in the current packet.
        // Insert ResultingResourceState into PossibleStates only if we haven't
        // processed ResultingResourceState before.
        //
        if (ResultingResourceState != prevState) {
          if (VisitedResourceStates.count(ResultingResourceState) == 0) {
            VisitedResourceStates.insert(ResultingResourceState);
            PossibleStates.insert(ResultingResourceState);
            LLVM_DEBUG(dbgs()
                       << "\tResultingResourceState: 0x"
                       << Twine::utohexstr(ResultingResourceState) << "\n");
          } else {
            LLVM_DEBUG(dbgs() << "\tSkipped Add - state already seen\n");
          }
        } else {
          LLVM_DEBUG(dbgs()
                     << "\tSkipped Add - no final resources available\n");
        }
      } else {
        //
        // If the current resource can be accommodated, check the next
        // stage in InsnClass for available resources.
        //
        if (ResultingResourceState != prevState) {
          LLVM_DEBUG(dbgs() << "\n");
          AddInsnClassStages(InsnClass, ComboBitToBitsMap,
                                chkstage - 1, numstages,
                                ResultingResourceState, origState,
                                VisitedResourceStates, PossibleStates);
        } else {
          LLVM_DEBUG(dbgs() << "\tSkipped Add - no resources available\n");
        }
      }
    }
  }
}

//
// canMaybeAddInsnClass - Quickly verifies if an instruction of type InsnClass
// may be a valid transition from this state i.e., can an instruction of type
// InsnClass be added to the packet represented by this state.
//
// Note that this routine is performing conservative checks that can be
// quickly executed acting as a filter before calling AddInsnClassStages.
// Any cases allowed through here will be caught later in AddInsnClassStages
// which performs the more expensive exact check.
//
bool State::canMaybeAddInsnClass(std::vector<unsigned> &InsnClass,
                    std::map<unsigned, unsigned> &ComboBitToBitsMap) const {
  for (std::set<unsigned>::const_iterator SI = stateInfo.begin();
       SI != stateInfo.end(); ++SI) {
    // Check to see if all required resources are available.
    bool available = true;

    // Inspect each stage independently.
    // note: This is a conservative check as we aren't checking for
    //       possible resource competition between the stages themselves
    //       The full cross product is examined later in AddInsnClass.
    for (unsigned i = 0; i < InsnClass.size(); ++i) {
      unsigned resources = *SI;
      if ((~resources & InsnClass[i]) == 0) {
        available = false;
        break;
      }
      // Make sure _all_ resources for a combo function are available.
      // note: This is a quick conservative check as it won't catch an
      //       unscheduleable combo if this stage is an OR expression
      //       containing a combo.
      //       These cases are caught later in AddInsnClass.
      unsigned combo = ComboBitToBitsMap[InsnClass[i]];
      if (combo && ((~resources & combo) != combo)) {
        LLVM_DEBUG(dbgs() << "\tSkipped canMaybeAdd 0x"
                          << Twine::utohexstr(resources) << " - combo op 0x"
                          << Twine::utohexstr(InsnClass[i]) << " (0x"
                          << Twine::utohexstr(combo)
                          << ") cannot be scheduled\n");
        available = false;
        break;
      }
    }

    if (available) {
      return true;
    }
  }
  return false;
}

const State &DFA::newState() {
  auto IterPair = states.insert(State());
  assert(IterPair.second && "State already exists");
  return *IterPair.first;
}

int State::currentStateNum = 0;

DFAPacketizerEmitter::DFAPacketizerEmitter(RecordKeeper &R):
  TargetName(CodeGenTarget(R).getName()), Records(R) {}

//
// writeTableAndAPI - Print out a table representing the DFA and the
// associated API to create a DFA packetizer.
//
// Format:
// DFAStateInputTable[][2] = pairs of <Input, Transition> for all valid
//                           transitions.
// DFAStateEntryTable[i] = Index of the first entry in DFAStateInputTable for
//                         the ith state.
//
//
void DFA::writeTableAndAPI(raw_ostream &OS, const std::string &TargetName,
                           int numInsnClasses,
                           int maxResources, int numCombos, int maxStages) {
  unsigned numStates = states.size();

  LLVM_DEBUG(dbgs() << "-------------------------------------------------------"
                       "----------------------\n");
  LLVM_DEBUG(dbgs() << "writeTableAndAPI\n");
  LLVM_DEBUG(dbgs() << "Total states: " << numStates << "\n");

  OS << "namespace llvm {\n";

  OS << "\n// Input format:\n";
  OS << "#define DFA_MAX_RESTERMS        " << DFA_MAX_RESTERMS
     << "\t// maximum AND'ed resource terms\n";
  OS << "#define DFA_MAX_RESOURCES       " << DFA_MAX_RESOURCES
     << "\t// maximum resource bits in one term\n";

  OS << "\n// " << TargetName << "DFAStateInputTable[][2] = "
     << "pairs of <Input, NextState> for all valid\n";
  OS << "//                           transitions.\n";
  OS << "// " << numStates << "\tstates\n";
  OS << "// " << numInsnClasses << "\tinstruction classes\n";
  OS << "// " << maxResources << "\tresources max\n";
  OS << "// " << numCombos << "\tcombo resources\n";
  OS << "// " << maxStages << "\tstages max\n";
  OS << "const " << DFA_TBLTYPE << " "
     << TargetName << "DFAStateInputTable[][2] = {\n";

  // This table provides a map to the beginning of the transitions for State s
  // in DFAStateInputTable.
  std::vector<int> StateEntry(numStates+1);
  static const std::string SentinelEntry = "{-1, -1}";

  // Tracks the total valid transitions encountered so far. It is used
  // to construct the StateEntry table.
  int ValidTransitions = 0;
  DFA::StateSet::iterator SI = states.begin();
  for (unsigned i = 0; i < numStates; ++i, ++SI) {
    assert ((SI->stateNum == (int) i) && "Mismatch in state numbers");
    StateEntry[i] = ValidTransitions;
    for (State::TransitionMap::iterator
        II = SI->Transitions.begin(), IE = SI->Transitions.end();
        II != IE; ++II) {
      OS << "{0x" << Twine::utohexstr(getDFAInsnInput(II->first)) << ", "
         << II->second->stateNum << "},\t";
    }
    ValidTransitions += SI->Transitions.size();

    // If there are no valid transitions from this stage, we need a sentinel
    // transition.
    if (ValidTransitions == StateEntry[i]) {
      OS << SentinelEntry << ",\t";
      ++ValidTransitions;
    }

    OS << " // state " << i << ": " << StateEntry[i];
    if (StateEntry[i] != (ValidTransitions-1)) {   // More than one transition.
       OS << "-" << (ValidTransitions-1);
    }
    OS << "\n";
  }

  // Print out a sentinel entry at the end of the StateInputTable. This is
  // needed to iterate over StateInputTable in DFAPacketizer::ReadTable()
  OS << SentinelEntry << "\t";
  OS << " // state " << numStates << ": " << ValidTransitions;
  OS << "\n";

  OS << "};\n\n";
  OS << "// " << TargetName << "DFAStateEntryTable[i] = "
     << "Index of the first entry in DFAStateInputTable for\n";
  OS << "//                         "
     << "the ith state.\n";
  OS << "// " << numStates << " states\n";
  OS << "const unsigned int " << TargetName << "DFAStateEntryTable[] = {\n";

  // Multiply i by 2 since each entry in DFAStateInputTable is a set of
  // two numbers.
  unsigned lastState = 0;
  for (unsigned i = 0; i < numStates; ++i) {
    if (i && ((i % 10) == 0)) {
        lastState = i-1;
        OS << "   // states " << (i-10) << ":" << lastState << "\n";
    }
    OS << StateEntry[i] << ", ";
  }

  // Print out the index to the sentinel entry in StateInputTable
  OS << ValidTransitions << ", ";
  OS << "   // states " << (lastState+1) << ":" << numStates << "\n";

  OS << "};\n";
  OS << "} // namespace\n";

  //
  // Emit DFA Packetizer tables if the target is a VLIW machine.
  //
  std::string SubTargetClassName = TargetName + "GenSubtargetInfo";
  OS << "\n" << "#include \"llvm/CodeGen/DFAPacketizer.h\"\n";
  OS << "namespace llvm {\n";
  OS << "DFAPacketizer *" << SubTargetClassName << "::"
     << "createDFAPacketizer(const InstrItineraryData *IID) const {\n"
     << "   return new DFAPacketizer(IID, " << TargetName
     << "DFAStateInputTable, " << TargetName << "DFAStateEntryTable);\n}\n\n";
  OS << "} // End llvm namespace \n";
}

//
// collectAllFuncUnits - Construct a map of function unit names to bits.
//
int DFAPacketizerEmitter::collectAllFuncUnits(
                            std::vector<Record*> &ProcItinList,
                            std::map<std::string, unsigned> &FUNameToBitsMap,
                            int &maxFUs,
                            raw_ostream &OS) {
  LLVM_DEBUG(dbgs() << "-------------------------------------------------------"
                       "----------------------\n");
  LLVM_DEBUG(dbgs() << "collectAllFuncUnits");
  LLVM_DEBUG(dbgs() << " (" << ProcItinList.size() << " itineraries)\n");

  int totalFUs = 0;
  // Parse functional units for all the itineraries.
  for (unsigned i = 0, N = ProcItinList.size(); i < N; ++i) {
    Record *Proc = ProcItinList[i];
    std::vector<Record*> FUs = Proc->getValueAsListOfDefs("FU");

    LLVM_DEBUG(dbgs() << "    FU:" << i << " (" << FUs.size() << " FUs) "
                      << Proc->getName());

    // Convert macros to bits for each stage.
    unsigned numFUs = FUs.size();
    for (unsigned j = 0; j < numFUs; ++j) {
      assert ((j < DFA_MAX_RESOURCES) &&
                      "Exceeded maximum number of representable resources");
      unsigned FuncResources = (unsigned) (1U << j);
      FUNameToBitsMap[FUs[j]->getName()] = FuncResources;
      LLVM_DEBUG(dbgs() << " " << FUs[j]->getName() << ":0x"
                        << Twine::utohexstr(FuncResources));
    }
    if (((int) numFUs) > maxFUs) {
      maxFUs = numFUs;
    }
    totalFUs += numFUs;
    LLVM_DEBUG(dbgs() << "\n");
  }
  return totalFUs;
}

//
// collectAllComboFuncs - Construct a map from a combo function unit bit to
//                        the bits of all included functional units.
//
int DFAPacketizerEmitter::collectAllComboFuncs(
                            std::vector<Record*> &ComboFuncList,
                            std::map<std::string, unsigned> &FUNameToBitsMap,
                            std::map<unsigned, unsigned> &ComboBitToBitsMap,
                            raw_ostream &OS) {
  LLVM_DEBUG(dbgs() << "-------------------------------------------------------"
                       "----------------------\n");
  LLVM_DEBUG(dbgs() << "collectAllComboFuncs");
  LLVM_DEBUG(dbgs() << " (" << ComboFuncList.size() << " sets)\n");

  int numCombos = 0;
  for (unsigned i = 0, N = ComboFuncList.size(); i < N; ++i) {
    Record *Func = ComboFuncList[i];
    std::vector<Record*> FUs = Func->getValueAsListOfDefs("CFD");

    LLVM_DEBUG(dbgs() << "    CFD:" << i << " (" << FUs.size() << " combo FUs) "
                      << Func->getName() << "\n");

    // Convert macros to bits for each stage.
    for (unsigned j = 0, N = FUs.size(); j < N; ++j) {
      assert ((j < DFA_MAX_RESOURCES) &&
                      "Exceeded maximum number of DFA resources");
      Record *FuncData = FUs[j];
      Record *ComboFunc = FuncData->getValueAsDef("TheComboFunc");
      const std::vector<Record*> &FuncList =
                                   FuncData->getValueAsListOfDefs("FuncList");
      const std::string &ComboFuncName = ComboFunc->getName();
      unsigned ComboBit = FUNameToBitsMap[ComboFuncName];
      unsigned ComboResources = ComboBit;
      LLVM_DEBUG(dbgs() << "      combo: " << ComboFuncName << ":0x"
                        << Twine::utohexstr(ComboResources) << "\n");
      for (unsigned k = 0, M = FuncList.size(); k < M; ++k) {
        std::string FuncName = FuncList[k]->getName();
        unsigned FuncResources = FUNameToBitsMap[FuncName];
        LLVM_DEBUG(dbgs() << "        " << FuncName << ":0x"
                          << Twine::utohexstr(FuncResources) << "\n");
        ComboResources |= FuncResources;
      }
      ComboBitToBitsMap[ComboBit] = ComboResources;
      numCombos++;
      LLVM_DEBUG(dbgs() << "          => combo bits: " << ComboFuncName << ":0x"
                        << Twine::utohexstr(ComboBit) << " = 0x"
                        << Twine::utohexstr(ComboResources) << "\n");
    }
  }
  return numCombos;
}

//
// collectOneInsnClass - Populate allInsnClasses with one instruction class
//
int DFAPacketizerEmitter::collectOneInsnClass(const std::string &ProcName,
                        std::vector<Record*> &ProcItinList,
                        std::map<std::string, unsigned> &FUNameToBitsMap,
                        Record *ItinData,
                        raw_ostream &OS) {
  const std::vector<Record*> &StageList =
    ItinData->getValueAsListOfDefs("Stages");

  // The number of stages.
  unsigned NStages = StageList.size();

  LLVM_DEBUG(dbgs() << "    " << ItinData->getValueAsDef("TheClass")->getName()
                    << "\n");

  std::vector<unsigned> UnitBits;

  // Compute the bitwise or of each unit used in this stage.
  for (unsigned i = 0; i < NStages; ++i) {
    const Record *Stage = StageList[i];

    // Get unit list.
    const std::vector<Record*> &UnitList =
      Stage->getValueAsListOfDefs("Units");

    LLVM_DEBUG(dbgs() << "        stage:" << i << " [" << UnitList.size()
                      << " units]:");
    unsigned dbglen = 26;  // cursor after stage dbgs

    // Compute the bitwise or of each unit used in this stage.
    unsigned UnitBitValue = 0;
    for (unsigned j = 0, M = UnitList.size(); j < M; ++j) {
      // Conduct bitwise or.
      std::string UnitName = UnitList[j]->getName();
      LLVM_DEBUG(dbgs() << " " << j << ":" << UnitName);
      dbglen += 3 + UnitName.length();
      assert(FUNameToBitsMap.count(UnitName));
      UnitBitValue |= FUNameToBitsMap[UnitName];
    }

    if (UnitBitValue != 0)
      UnitBits.push_back(UnitBitValue);

    while (dbglen <= 64) {   // line up bits dbgs
        dbglen += 8;
        LLVM_DEBUG(dbgs() << "\t");
    }
    LLVM_DEBUG(dbgs() << " (bits: 0x" << Twine::utohexstr(UnitBitValue)
                      << ")\n");
  }

  if (!UnitBits.empty())
    allInsnClasses.push_back(UnitBits);

  LLVM_DEBUG({
    dbgs() << "        ";
    dbgsInsnClass(UnitBits);
    dbgs() << "\n";
  });

  return NStages;
}

//
// collectAllInsnClasses - Populate allInsnClasses which is a set of units
// used in each stage.
//
int DFAPacketizerEmitter::collectAllInsnClasses(const std::string &ProcName,
                            std::vector<Record*> &ProcItinList,
                            std::map<std::string, unsigned> &FUNameToBitsMap,
                            std::vector<Record*> &ItinDataList,
                            int &maxStages,
                            raw_ostream &OS) {
  // Collect all instruction classes.
  unsigned M = ItinDataList.size();

  int numInsnClasses = 0;
  LLVM_DEBUG(dbgs() << "-------------------------------------------------------"
                       "----------------------\n"
                    << "collectAllInsnClasses " << ProcName << " (" << M
                    << " classes)\n");

  // Collect stages for each instruction class for all itinerary data
  for (unsigned j = 0; j < M; j++) {
    Record *ItinData = ItinDataList[j];
    int NStages = collectOneInsnClass(ProcName, ProcItinList,
                                      FUNameToBitsMap, ItinData, OS);
    if (NStages > maxStages) {
      maxStages = NStages;
    }
    numInsnClasses++;
  }
  return numInsnClasses;
}

//
// Run the worklist algorithm to generate the DFA.
//
void DFAPacketizerEmitter::run(raw_ostream &OS) {
  // Collect processor iteraries.
  std::vector<Record*> ProcItinList =
    Records.getAllDerivedDefinitions("ProcessorItineraries");

  //
  // Collect the Functional units.
  //
  std::map<std::string, unsigned> FUNameToBitsMap;
  int maxResources = 0;
  collectAllFuncUnits(ProcItinList,
                              FUNameToBitsMap, maxResources, OS);

  //
  // Collect the Combo Functional units.
  //
  std::map<unsigned, unsigned> ComboBitToBitsMap;
  std::vector<Record*> ComboFuncList =
    Records.getAllDerivedDefinitions("ComboFuncUnits");
  int numCombos = collectAllComboFuncs(ComboFuncList,
                              FUNameToBitsMap, ComboBitToBitsMap, OS);

  //
  // Collect the itineraries.
  //
  int maxStages = 0;
  int numInsnClasses = 0;
  for (unsigned i = 0, N = ProcItinList.size(); i < N; i++) {
    Record *Proc = ProcItinList[i];

    // Get processor itinerary name.
    const std::string &ProcName = Proc->getName();

    // Skip default.
    if (ProcName == "NoItineraries")
      continue;

    // Sanity check for at least one instruction itinerary class.
    unsigned NItinClasses =
      Records.getAllDerivedDefinitions("InstrItinClass").size();
    if (NItinClasses == 0)
      return;

    // Get itinerary data list.
    std::vector<Record*> ItinDataList = Proc->getValueAsListOfDefs("IID");

    // Collect all instruction classes
    numInsnClasses += collectAllInsnClasses(ProcName, ProcItinList,
                          FUNameToBitsMap, ItinDataList, maxStages, OS);
  }

  //
  // Run a worklist algorithm to generate the DFA.
  //
  DFA D;
  const State *Initial = &D.newState();
  Initial->isInitial = true;
  Initial->stateInfo.insert(0x0);
  SmallVector<const State*, 32> WorkList;
  std::map<std::set<unsigned>, const State*> Visited;

  WorkList.push_back(Initial);

  //
  // Worklist algorithm to create a DFA for processor resource tracking.
  // C = {set of InsnClasses}
  // Begin with initial node in worklist. Initial node does not have
  // any consumed resources,
  //     ResourceState = 0x0
  // Visited = {}
  // While worklist != empty
  //    S = first element of worklist
  //    For every instruction class C
  //      if we can accommodate C in S:
  //          S' = state with resource states = {S Union C}
  //          Add a new transition: S x C -> S'
  //          If S' is not in Visited:
  //             Add S' to worklist
  //             Add S' to Visited
  //
  while (!WorkList.empty()) {
    const State *current = WorkList.pop_back_val();
    LLVM_DEBUG({
      dbgs() << "---------------------\n";
      dbgs() << "Processing state: " << current->stateNum << " - ";
      dbgsStateInfo(current->stateInfo);
      dbgs() << "\n";
    });
    for (unsigned i = 0; i < allInsnClasses.size(); i++) {
      std::vector<unsigned> InsnClass = allInsnClasses[i];
      LLVM_DEBUG({
        dbgs() << i << " ";
        dbgsInsnClass(InsnClass);
        dbgs() << "\n";
      });

      std::set<unsigned> NewStateResources;
      //
      // If we haven't already created a transition for this input
      // and the state can accommodate this InsnClass, create a transition.
      //
      if (!current->hasTransition(InsnClass) &&
          current->canMaybeAddInsnClass(InsnClass, ComboBitToBitsMap)) {
        const State *NewState = nullptr;
        current->AddInsnClass(InsnClass, ComboBitToBitsMap, NewStateResources);
        if (NewStateResources.empty()) {
          LLVM_DEBUG(dbgs() << "  Skipped - no new states generated\n");
          continue;
        }

        LLVM_DEBUG({
          dbgs() << "\t";
          dbgsStateInfo(NewStateResources);
          dbgs() << "\n";
        });

        //
        // If we have seen this state before, then do not create a new state.
        //
        auto VI = Visited.find(NewStateResources);
        if (VI != Visited.end()) {
          NewState = VI->second;
          LLVM_DEBUG({
            dbgs() << "\tFound existing state: " << NewState->stateNum
                   << " - ";
            dbgsStateInfo(NewState->stateInfo);
            dbgs() << "\n";
          });
        } else {
          NewState = &D.newState();
          NewState->stateInfo = NewStateResources;
          Visited[NewStateResources] = NewState;
          WorkList.push_back(NewState);
          LLVM_DEBUG({
            dbgs() << "\tAccepted new state: " << NewState->stateNum << " - ";
            dbgsStateInfo(NewState->stateInfo);
            dbgs() << "\n";
          });
        }

        current->addTransition(InsnClass, NewState);
      }
    }
  }

  // Print out the table.
  D.writeTableAndAPI(OS, TargetName,
               numInsnClasses, maxResources, numCombos, maxStages);
}

namespace llvm {

void EmitDFAPacketizer(RecordKeeper &RK, raw_ostream &OS) {
  emitSourceFileHeader("Target DFA Packetizer Tables", OS);
  DFAPacketizerEmitter(RK).run(OS);
}

} // end namespace llvm
