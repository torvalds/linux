//===- SDNodeProperties.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SDNodeProperties.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

unsigned llvm::parseSDPatternOperatorProperties(Record *R) {
  unsigned Properties = 0;
  for (Record *Property : R->getValueAsListOfDefs("Properties")) {
    if (Property->getName() == "SDNPCommutative") {
      Properties |= 1 << SDNPCommutative;
    } else if (Property->getName() == "SDNPAssociative") {
      Properties |= 1 << SDNPAssociative;
    } else if (Property->getName() == "SDNPHasChain") {
      Properties |= 1 << SDNPHasChain;
    } else if (Property->getName() == "SDNPOutGlue") {
      Properties |= 1 << SDNPOutGlue;
    } else if (Property->getName() == "SDNPInGlue") {
      Properties |= 1 << SDNPInGlue;
    } else if (Property->getName() == "SDNPOptInGlue") {
      Properties |= 1 << SDNPOptInGlue;
    } else if (Property->getName() == "SDNPMayStore") {
      Properties |= 1 << SDNPMayStore;
    } else if (Property->getName() == "SDNPMayLoad") {
      Properties |= 1 << SDNPMayLoad;
    } else if (Property->getName() == "SDNPSideEffect") {
      Properties |= 1 << SDNPSideEffect;
    } else if (Property->getName() == "SDNPMemOperand") {
      Properties |= 1 << SDNPMemOperand;
    } else if (Property->getName() == "SDNPVariadic") {
      Properties |= 1 << SDNPVariadic;
    } else {
      PrintFatalError("Unknown SD Node property '" +
                      Property->getName() + "' on node '" +
                      R->getName() + "'!");
    }
  }

  return Properties;
}
