//===- MIRParser.h - MIR serialization format parser ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This MIR serialization library is currently a work in progress. It can't
// serialize machine functions at this time.
//
// This file declares the functions that parse the MIR serialization format
// files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRPARSER_MIRPARSER_H
#define LLVM_CODEGEN_MIRPARSER_MIRPARSER_H

#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {

class StringRef;
class MIRParserImpl;
class MachineModuleInfo;
class SMDiagnostic;

/// This class initializes machine functions by applying the state loaded from
/// a MIR file.
class MIRParser {
  std::unique_ptr<MIRParserImpl> Impl;

public:
  MIRParser(std::unique_ptr<MIRParserImpl> Impl);
  MIRParser(const MIRParser &) = delete;
  ~MIRParser();

  /// Parses the optional LLVM IR module in the MIR file.
  ///
  /// A new, empty module is created if the LLVM IR isn't present.
  /// \returns nullptr if a parsing error occurred.
  std::unique_ptr<Module> parseIRModule();

  /// Parses MachineFunctions in the MIR file and add them to the given
  /// MachineModuleInfo \p MMI.
  ///
  /// \returns true if an error occurred.
  bool parseMachineFunctions(Module &M, MachineModuleInfo &MMI);
};

/// This function is the main interface to the MIR serialization format parser.
///
/// It reads in a MIR file and returns a MIR parser that can parse the embedded
/// LLVM IR module and initialize the machine functions by parsing the machine
/// function's state.
///
/// \param Filename - The name of the file to parse.
/// \param Error - Error result info.
/// \param Context - Context which will be used for the parsed LLVM IR module.
std::unique_ptr<MIRParser> createMIRParserFromFile(StringRef Filename,
                                                   SMDiagnostic &Error,
                                                   LLVMContext &Context);

/// This function is another interface to the MIR serialization format parser.
///
/// It returns a MIR parser that works with the given memory buffer and that can
/// parse the embedded LLVM IR module and initialize the machine functions by
/// parsing the machine function's state.
///
/// \param Contents - The MemoryBuffer containing the machine level IR.
/// \param Context - Context which will be used for the parsed LLVM IR module.
std::unique_ptr<MIRParser>
createMIRParser(std::unique_ptr<MemoryBuffer> Contents, LLVMContext &Context);

} // end namespace llvm

#endif // LLVM_CODEGEN_MIRPARSER_MIRPARSER_H
