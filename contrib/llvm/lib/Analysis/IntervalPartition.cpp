//===- IntervalPartition.cpp - Interval Partition module code -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the definition of the IntervalPartition class, which
// calculates and represent the interval partition of a function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IntervalPartition.h"
#include "llvm/Analysis/Interval.h"
#include "llvm/Analysis/IntervalIterator.h"
#include "llvm/Pass.h"
#include <cassert>
#include <utility>

using namespace llvm;

char IntervalPartition::ID = 0;

INITIALIZE_PASS(IntervalPartition, "intervals",
                "Interval Partition Construction", true, true)

//===----------------------------------------------------------------------===//
// IntervalPartition Implementation
//===----------------------------------------------------------------------===//

// releaseMemory - Reset state back to before function was analyzed
void IntervalPartition::releaseMemory() {
  for (unsigned i = 0, e = Intervals.size(); i != e; ++i)
    delete Intervals[i];
  IntervalMap.clear();
  Intervals.clear();
  RootInterval = nullptr;
}

void IntervalPartition::print(raw_ostream &O, const Module*) const {
  for(unsigned i = 0, e = Intervals.size(); i != e; ++i)
    Intervals[i]->print(O);
}

// addIntervalToPartition - Add an interval to the internal list of intervals,
// and then add mappings from all of the basic blocks in the interval to the
// interval itself (in the IntervalMap).
void IntervalPartition::addIntervalToPartition(Interval *I) {
  Intervals.push_back(I);

  // Add mappings for all of the basic blocks in I to the IntervalPartition
  for (Interval::node_iterator It = I->Nodes.begin(), End = I->Nodes.end();
       It != End; ++It)
    IntervalMap.insert(std::make_pair(*It, I));
}

// updatePredecessors - Interval generation only sets the successor fields of
// the interval data structures.  After interval generation is complete,
// run through all of the intervals and propagate successor info as
// predecessor info.
void IntervalPartition::updatePredecessors(Interval *Int) {
  BasicBlock *Header = Int->getHeaderNode();
  for (BasicBlock *Successor : Int->Successors)
    getBlockInterval(Successor)->Predecessors.push_back(Header);
}

// IntervalPartition ctor - Build the first level interval partition for the
// specified function...
bool IntervalPartition::runOnFunction(Function &F) {
  // Pass false to intervals_begin because we take ownership of it's memory
  function_interval_iterator I = intervals_begin(&F, false);
  assert(I != intervals_end(&F) && "No intervals in function!?!?!");

  addIntervalToPartition(RootInterval = *I);

  ++I;  // After the first one...

  // Add the rest of the intervals to the partition.
  for (function_interval_iterator E = intervals_end(&F); I != E; ++I)
    addIntervalToPartition(*I);

  // Now that we know all of the successor information, propagate this to the
  // predecessors for each block.
  for (unsigned i = 0, e = Intervals.size(); i != e; ++i)
    updatePredecessors(Intervals[i]);
  return false;
}

// IntervalPartition ctor - Build a reduced interval partition from an
// existing interval graph.  This takes an additional boolean parameter to
// distinguish it from a copy constructor.  Always pass in false for now.
IntervalPartition::IntervalPartition(IntervalPartition &IP, bool)
  : FunctionPass(ID) {
  assert(IP.getRootInterval() && "Cannot operate on empty IntervalPartitions!");

  // Pass false to intervals_begin because we take ownership of it's memory
  interval_part_interval_iterator I = intervals_begin(IP, false);
  assert(I != intervals_end(IP) && "No intervals in interval partition!?!?!");

  addIntervalToPartition(RootInterval = *I);

  ++I;  // After the first one...

  // Add the rest of the intervals to the partition.
  for (interval_part_interval_iterator E = intervals_end(IP); I != E; ++I)
    addIntervalToPartition(*I);

  // Now that we know all of the successor information, propagate this to the
  // predecessors for each block.
  for (unsigned i = 0, e = Intervals.size(); i != e; ++i)
    updatePredecessors(Intervals[i]);
}
