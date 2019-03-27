//===-- SlotMapping.h - Slot number mapping for unnamed values --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the SlotMapping struct.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_SLOTMAPPING_H
#define LLVM_ASMPARSER_SLOTMAPPING_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/TrackingMDRef.h"
#include <map>
#include <vector>

namespace llvm {

class GlobalValue;
class Type;

/// This struct contains the mappings from the slot numbers to unnamed metadata
/// nodes, global values and types. It also contains the mapping for the named
/// types.
/// It can be used to save the parsing state of an LLVM IR module so that the
/// textual references to the values in the module can be parsed outside of the
/// module's source.
struct SlotMapping {
  std::vector<GlobalValue *> GlobalValues;
  std::map<unsigned, TrackingMDNodeRef> MetadataNodes;
  StringMap<Type *> NamedTypes;
  std::map<unsigned, Type *> Types;
};

} // end namespace llvm

#endif
