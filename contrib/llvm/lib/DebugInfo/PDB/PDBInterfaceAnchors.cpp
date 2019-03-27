//===- PDBInterfaceAnchors.h - defines class anchor funcions ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Class anchors are necessary per the LLVM Coding style guide, to ensure that
// the vtable is only generated in this object file, and not in every object
// file that incldues the corresponding header.
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/IPDBDataStream.h"
#include "llvm/DebugInfo/PDB/IPDBFrameData.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/IPDBSectionContrib.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/IPDBTable.h"

using namespace llvm;
using namespace llvm::pdb;

IPDBSession::~IPDBSession() = default;

IPDBDataStream::~IPDBDataStream() = default;

IPDBRawSymbol::~IPDBRawSymbol() = default;

IPDBLineNumber::~IPDBLineNumber() = default;

IPDBTable::~IPDBTable() = default;

IPDBInjectedSource::~IPDBInjectedSource() = default;

IPDBSectionContrib::~IPDBSectionContrib() = default;

IPDBFrameData::~IPDBFrameData() = default;
