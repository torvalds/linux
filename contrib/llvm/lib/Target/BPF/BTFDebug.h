//===- BTFDebug.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains support for writing BTF debug info.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BTFDEBUG_H
#define LLVM_LIB_TARGET_BPF_BTFDEBUG_H

#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/DebugHandlerBase.h"
#include <unordered_map>
#include "BTF.h"

namespace llvm {

class AsmPrinter;
class BTFDebug;
class DIType;
class MCStreamer;
class MCSymbol;
class MachineFunction;

/// The base class for BTF type generation.
class BTFTypeBase {
protected:
  uint8_t Kind;
  uint32_t Id;
  struct BTF::CommonType BTFType;

public:
  virtual ~BTFTypeBase() = default;
  void setId(uint32_t Id) { this->Id = Id; }
  uint32_t getId() { return Id; }
  uint32_t roundupToBytes(uint32_t NumBits) { return (NumBits + 7) >> 3; }
  /// Get the size of this BTF type entry.
  virtual uint32_t getSize() { return BTF::CommonTypeSize; }
  /// Complete BTF type generation after all related DebugInfo types
  /// have been visited so their BTF type id's are available
  /// for cross referece.
  virtual void completeType(BTFDebug &BDebug) {}
  /// Emit types for this BTF type entry.
  virtual void emitType(MCStreamer &OS);
};

/// Handle several derived types include pointer, const,
/// volatile, typedef and restrict.
class BTFTypeDerived : public BTFTypeBase {
  const DIDerivedType *DTy;

public:
  BTFTypeDerived(const DIDerivedType *Ty, unsigned Tag);
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle struct or union forward declaration.
class BTFTypeFwd : public BTFTypeBase {
  StringRef Name;

public:
  BTFTypeFwd(StringRef Name, bool IsUnion);
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle int type.
class BTFTypeInt : public BTFTypeBase {
  StringRef Name;
  uint32_t IntVal; ///< Encoding, offset, bits

public:
  BTFTypeInt(uint32_t Encoding, uint32_t SizeInBits, uint32_t OffsetInBits,
             StringRef TypeName);
  uint32_t getSize() { return BTFTypeBase::getSize() + sizeof(uint32_t); }
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle enumerate type.
class BTFTypeEnum : public BTFTypeBase {
  const DICompositeType *ETy;
  std::vector<struct BTF::BTFEnum> EnumValues;

public:
  BTFTypeEnum(const DICompositeType *ETy, uint32_t NumValues);
  uint32_t getSize() {
    return BTFTypeBase::getSize() + EnumValues.size() * BTF::BTFEnumSize;
  }
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle array type.
class BTFTypeArray : public BTFTypeBase {
  const DICompositeType *ATy;
  struct BTF::BTFArray ArrayInfo;

public:
  BTFTypeArray(const DICompositeType *ATy);
  uint32_t getSize() { return BTFTypeBase::getSize() + BTF::BTFArraySize; }
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle struct/union type.
class BTFTypeStruct : public BTFTypeBase {
  const DICompositeType *STy;
  bool HasBitField;
  std::vector<struct BTF::BTFMember> Members;

public:
  BTFTypeStruct(const DICompositeType *STy, bool IsStruct, bool HasBitField,
                uint32_t NumMembers);
  uint32_t getSize() {
    return BTFTypeBase::getSize() + Members.size() * BTF::BTFMemberSize;
  }
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle function pointer.
class BTFTypeFuncProto : public BTFTypeBase {
  const DISubroutineType *STy;
  std::unordered_map<uint32_t, StringRef> FuncArgNames;
  std::vector<struct BTF::BTFParam> Parameters;

public:
  BTFTypeFuncProto(const DISubroutineType *STy, uint32_t NumParams,
                   const std::unordered_map<uint32_t, StringRef> &FuncArgNames);
  uint32_t getSize() {
    return BTFTypeBase::getSize() + Parameters.size() * BTF::BTFParamSize;
  }
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// Handle subprogram
class BTFTypeFunc : public BTFTypeBase {
  StringRef Name;

public:
  BTFTypeFunc(StringRef FuncName, uint32_t ProtoTypeId);
  uint32_t getSize() { return BTFTypeBase::getSize(); }
  void completeType(BTFDebug &BDebug);
  void emitType(MCStreamer &OS);
};

/// String table.
class BTFStringTable {
  /// String table size in bytes.
  uint32_t Size;
  /// A mapping from string table offset to the index
  /// of the Table. It is used to avoid putting
  /// duplicated strings in the table.
  std::unordered_map<uint32_t, uint32_t> OffsetToIdMap;
  /// A vector of strings to represent the string table.
  std::vector<std::string> Table;

public:
  BTFStringTable() : Size(0) {}
  uint32_t getSize() { return Size; }
  std::vector<std::string> &getTable() { return Table; }
  /// Add a string to the string table and returns its offset
  /// in the table.
  uint32_t addString(StringRef S);
};

/// Represent one func and its type id.
struct BTFFuncInfo {
  const MCSymbol *Label; ///< Func MCSymbol
  uint32_t TypeId;       ///< Type id referring to .BTF type section
};

/// Represent one line info.
struct BTFLineInfo {
  MCSymbol *Label;      ///< MCSymbol identifying insn for the lineinfo
  uint32_t FileNameOff; ///< file name offset in the .BTF string table
  uint32_t LineOff;     ///< line offset in the .BTF string table
  uint32_t LineNum;     ///< the line number
  uint32_t ColumnNum;   ///< the column number
};

/// Collect and emit BTF information.
class BTFDebug : public DebugHandlerBase {
  MCStreamer &OS;
  bool SkipInstruction;
  bool LineInfoGenerated;
  uint32_t SecNameOff;
  uint32_t ArrayIndexTypeId;
  BTFStringTable StringTable;
  std::vector<std::unique_ptr<BTFTypeBase>> TypeEntries;
  std::unordered_map<const DIType *, uint32_t> DIToIdMap;
  std::unordered_map<uint32_t, std::vector<BTFFuncInfo>> FuncInfoTable;
  std::unordered_map<uint32_t, std::vector<BTFLineInfo>> LineInfoTable;
  StringMap<std::vector<std::string>> FileContent;

  /// Add types to TypeEntries.
  /// @{
  /// Add types to TypeEntries and DIToIdMap.
  void addType(std::unique_ptr<BTFTypeBase> TypeEntry, const DIType *Ty);
  /// Add types to TypeEntries only and return type id.
  uint32_t addType(std::unique_ptr<BTFTypeBase> TypeEntry);
  /// @}

  /// IR type visiting functions.
  /// @{
  void visitTypeEntry(const DIType *Ty);
  void visitBasicType(const DIBasicType *BTy);
  void visitSubroutineType(
      const DISubroutineType *STy, bool ForSubprog,
      const std::unordered_map<uint32_t, StringRef> &FuncArgNames,
      uint32_t &TypeId);
  void visitFwdDeclType(const DICompositeType *CTy, bool IsUnion);
  void visitCompositeType(const DICompositeType *CTy);
  void visitStructType(const DICompositeType *STy, bool IsStruct);
  void visitArrayType(const DICompositeType *ATy);
  void visitEnumType(const DICompositeType *ETy);
  void visitDerivedType(const DIDerivedType *DTy);
  /// @}

  /// Get the file content for the subprogram. Certain lines of the file
  /// later may be put into string table and referenced by line info.
  std::string populateFileContent(const DISubprogram *SP);

  /// Construct a line info.
  void constructLineInfo(const DISubprogram *SP, MCSymbol *Label, uint32_t Line,
                         uint32_t Column);

  /// Emit common header of .BTF and .BTF.ext sections.
  void emitCommonHeader();

  /// Emit the .BTF section.
  void emitBTFSection();

  /// Emit the .BTF.ext section.
  void emitBTFExtSection();

protected:
  /// Gather pre-function debug information.
  void beginFunctionImpl(const MachineFunction *MF) override;

  /// Post process after all instructions in this function are processed.
  void endFunctionImpl(const MachineFunction *MF) override;

public:
  BTFDebug(AsmPrinter *AP);

  /// Get the special array index type id.
  uint32_t getArrayIndexTypeId() {
    assert(ArrayIndexTypeId);
    return ArrayIndexTypeId;
  }

  /// Add string to the string table.
  size_t addString(StringRef S) { return StringTable.addString(S); }

  /// Get the type id for a particular DIType.
  uint32_t getTypeId(const DIType *Ty) {
    assert(Ty && "Invalid null Type");
    assert(DIToIdMap.find(Ty) != DIToIdMap.end() &&
           "DIType not added in the BDIToIdMap");
    return DIToIdMap[Ty];
  }

  void setSymbolSize(const MCSymbol *Symbol, uint64_t Size) override {}

  /// Process beginning of an instruction.
  void beginInstruction(const MachineInstr *MI) override;

  /// Complete all the types and emit the BTF sections.
  void endModule() override;
};

} // end namespace llvm

#endif
