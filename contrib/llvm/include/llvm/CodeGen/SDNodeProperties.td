//===- SDNodeProperties.td - Common code for DAG isels ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class SDNodeProperty;

// Selection DAG Pattern Operations
class SDPatternOperator {
  list<SDNodeProperty> Properties = [];
}

//===----------------------------------------------------------------------===//
// Selection DAG Node Properties.
//
// Note: These are hard coded into tblgen.
//
def SDNPCommutative : SDNodeProperty;   // X op Y == Y op X
def SDNPAssociative : SDNodeProperty;   // (X op Y) op Z == X op (Y op Z)
def SDNPHasChain    : SDNodeProperty;   // R/W chain operand and result
def SDNPOutGlue     : SDNodeProperty;   // Write a flag result
def SDNPInGlue      : SDNodeProperty;   // Read a flag operand
def SDNPOptInGlue   : SDNodeProperty;   // Optionally read a flag operand
def SDNPMayStore    : SDNodeProperty;   // May write to memory, sets 'mayStore'.
def SDNPMayLoad     : SDNodeProperty;   // May read memory, sets 'mayLoad'.
def SDNPSideEffect  : SDNodeProperty;   // Sets 'HasUnmodelledSideEffects'.
def SDNPMemOperand  : SDNodeProperty;   // Touches memory, has assoc MemOperand
def SDNPVariadic    : SDNodeProperty;   // Node has variable arguments.
def SDNPWantRoot    : SDNodeProperty;   // ComplexPattern gets the root of match
def SDNPWantParent  : SDNodeProperty;   // ComplexPattern gets the parent
