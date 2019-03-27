//===- func-id-helper.h - XRay Function ID Conversion Helpers -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines helper tools dealing with XRay-generated function ids.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_XRAY_FUNC_ID_HELPER_H
#define LLVM_TOOLS_LLVM_XRAY_FUNC_ID_HELPER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include <unordered_map>

namespace llvm {
namespace xray {

// This class consolidates common operations related to Function IDs.
class FuncIdConversionHelper {
public:
  using FunctionAddressMap = std::unordered_map<int32_t, uint64_t>;

private:
  std::string BinaryInstrMap;
  symbolize::LLVMSymbolizer &Symbolizer;
  const FunctionAddressMap &FunctionAddresses;
  mutable llvm::DenseMap<int32_t, std::string> CachedNames;

public:
  FuncIdConversionHelper(std::string BinaryInstrMap,
                         symbolize::LLVMSymbolizer &Symbolizer,
                         const FunctionAddressMap &FunctionAddresses)
      : BinaryInstrMap(std::move(BinaryInstrMap)), Symbolizer(Symbolizer),
        FunctionAddresses(FunctionAddresses) {}

  // Returns the symbol or a string representation of the function id.
  std::string SymbolOrNumber(int32_t FuncId) const;

  // Returns the file and column from debug info for the given function id.
  std::string FileLineAndColumn(int32_t FuncId) const;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_XRAY_FUNC_ID_HELPER_H
