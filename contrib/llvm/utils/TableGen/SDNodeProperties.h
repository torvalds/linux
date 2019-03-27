//===- SDNodeProperties.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_SDNODEPROPERTIES_H
#define LLVM_UTILS_TABLEGEN_SDNODEPROPERTIES_H

namespace llvm {

class Record;

// SelectionDAG node properties.
//  SDNPMemOperand: indicates that a node touches memory and therefore must
//                  have an associated memory operand that describes the access.
enum SDNP {
  SDNPCommutative,
  SDNPAssociative,
  SDNPHasChain,
  SDNPOutGlue,
  SDNPInGlue,
  SDNPOptInGlue,
  SDNPMayLoad,
  SDNPMayStore,
  SDNPSideEffect,
  SDNPMemOperand,
  SDNPVariadic,
  SDNPWantRoot,
  SDNPWantParent
};

unsigned parseSDPatternOperatorProperties(Record *R);

}

#endif
