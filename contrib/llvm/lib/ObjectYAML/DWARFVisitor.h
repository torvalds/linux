//===--- DWARFVisitor.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DWARFVISITOR_H
#define LLVM_OBJECTYAML_DWARFVISITOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {

namespace DWARFYAML {

struct Data;
struct Unit;
struct Entry;
struct FormValue;
struct AttributeAbbrev;

/// A class to visits DWARFYAML Compile Units and DIEs in preorder.
///
/// Extensions of this class can either maintain const or non-const references
/// to the DWARFYAML::Data object.
template <typename T> class VisitorImpl {
protected:
  T &DebugInfo;

  /// Visitor Functions
  /// @{
  virtual void onStartCompileUnit(Unit &CU) {}
  virtual void onEndCompileUnit(Unit &CU) {}
  virtual void onStartDIE(Unit &CU, Entry &DIE) {}
  virtual void onEndDIE(Unit &CU, Entry &DIE) {}
  virtual void onForm(AttributeAbbrev &AttAbbrev, FormValue &Value) {}
  /// @}

  /// Const Visitor Functions
  /// @{
  virtual void onStartCompileUnit(const Unit &CU) {}
  virtual void onEndCompileUnit(const Unit &CU) {}
  virtual void onStartDIE(const Unit &CU, const Entry &DIE) {}
  virtual void onEndDIE(const Unit &CU, const Entry &DIE) {}
  virtual void onForm(const AttributeAbbrev &AttAbbrev,
                      const FormValue &Value) {}
  /// @}

  /// Value visitors
  /// @{
  virtual void onValue(const uint8_t U) {}
  virtual void onValue(const uint16_t U) {}
  virtual void onValue(const uint32_t U) {}
  virtual void onValue(const uint64_t U, const bool LEB = false) {}
  virtual void onValue(const int64_t S, const bool LEB = false) {}
  virtual void onValue(const StringRef String) {}
  virtual void onValue(const MemoryBufferRef MBR) {}
  /// @}

public:
  VisitorImpl(T &DI) : DebugInfo(DI) {}

  virtual ~VisitorImpl() {}

  void traverseDebugInfo();

private:
  void onVariableSizeValue(uint64_t U, unsigned Size);
};

// Making the visior instantiations extern and explicit in the cpp file. This
// prevents them from being instantiated in every compile unit that uses the
// visitors.
extern template class VisitorImpl<DWARFYAML::Data>;
extern template class VisitorImpl<const DWARFYAML::Data>;

class Visitor : public VisitorImpl<Data> {
public:
  Visitor(Data &DI) : VisitorImpl<Data>(DI) {}
};

class ConstVisitor : public VisitorImpl<const Data> {
public:
  ConstVisitor(const Data &DI) : VisitorImpl<const Data>(DI) {}
};

} // namespace DWARFYAML
} // namespace llvm

#endif
