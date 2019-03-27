//===- AMDKernelCodeTUtils.cpp --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file - utility functions to parse/print amd_kernel_code_t structure
//
//===----------------------------------------------------------------------===//

#include "AMDKernelCodeTUtils.h"
#include "SIDefines.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;

static ArrayRef<StringRef> get_amd_kernel_code_t_FldNames() {
  static StringRef const Table[] = {
    "", // not found placeholder
#define RECORD(name, altName, print, parse) #name
#include "AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return makeArrayRef(Table);
}

static ArrayRef<StringRef> get_amd_kernel_code_t_FldAltNames() {
  static StringRef const Table[] = {
    "", // not found placeholder
#define RECORD(name, altName, print, parse) #altName
#include "AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return makeArrayRef(Table);
}

static StringMap<int> createIndexMap(const ArrayRef<StringRef> &names,
                                     const ArrayRef<StringRef> &altNames) {
  StringMap<int> map;
  assert(names.size() == altNames.size());
  for (unsigned i = 0; i < names.size(); ++i) {
    map.insert(std::make_pair(names[i], i));
    map.insert(std::make_pair(altNames[i], i));
  }
  return map;
}

static int get_amd_kernel_code_t_FieldIndex(StringRef name) {
  static const auto map = createIndexMap(get_amd_kernel_code_t_FldNames(),
                                         get_amd_kernel_code_t_FldAltNames());
  return map.lookup(name) - 1; // returns -1 if not found
}

static StringRef get_amd_kernel_code_t_FieldName(int index) {
  return get_amd_kernel_code_t_FldNames()[index + 1];
}

// Field printing

static raw_ostream &printName(raw_ostream &OS, StringRef Name) {
  return OS << Name << " = ";
}

template <typename T, T amd_kernel_code_t::*ptr>
static void printField(StringRef Name, const amd_kernel_code_t &C,
                       raw_ostream &OS) {
  printName(OS, Name) << (int)(C.*ptr);
}

template <typename T, T amd_kernel_code_t::*ptr, int shift, int width = 1>
static void printBitField(StringRef Name, const amd_kernel_code_t &c,
                          raw_ostream &OS) {
  const auto Mask = (static_cast<T>(1) << width) - 1;
  printName(OS, Name) << (int)((c.*ptr >> shift) & Mask);
}

using PrintFx = void(*)(StringRef, const amd_kernel_code_t &, raw_ostream &);

static ArrayRef<PrintFx> getPrinterTable() {
  static const PrintFx Table[] = {
#define RECORD(name, altName, print, parse) print
#include "AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return makeArrayRef(Table);
}

void llvm::printAmdKernelCodeField(const amd_kernel_code_t &C,
                                   int FldIndex,
                                   raw_ostream &OS) {
  auto Printer = getPrinterTable()[FldIndex];
  if (Printer)
    Printer(get_amd_kernel_code_t_FieldName(FldIndex), C, OS);
}

void llvm::dumpAmdKernelCode(const amd_kernel_code_t *C,
                             raw_ostream &OS,
                             const char *tab) {
  const int Size = getPrinterTable().size();
  for (int i = 0; i < Size; ++i) {
    OS << tab;
    printAmdKernelCodeField(*C, i, OS);
    OS << '\n';
  }
}

// Field parsing

static bool expectAbsExpression(MCAsmParser &MCParser, int64_t &Value, raw_ostream& Err) {

  if (MCParser.getLexer().isNot(AsmToken::Equal)) {
    Err << "expected '='";
    return false;
  }
  MCParser.getLexer().Lex();

  if (MCParser.parseAbsoluteExpression(Value)) {
    Err << "integer absolute expression expected";
    return false;
  }
  return true;
}

template <typename T, T amd_kernel_code_t::*ptr>
static bool parseField(amd_kernel_code_t &C, MCAsmParser &MCParser,
                       raw_ostream &Err) {
  int64_t Value = 0;
  if (!expectAbsExpression(MCParser, Value, Err))
    return false;
  C.*ptr = (T)Value;
  return true;
}

template <typename T, T amd_kernel_code_t::*ptr, int shift, int width = 1>
static bool parseBitField(amd_kernel_code_t &C, MCAsmParser &MCParser,
                          raw_ostream &Err) {
  int64_t Value = 0;
  if (!expectAbsExpression(MCParser, Value, Err))
    return false;
  const uint64_t Mask = ((UINT64_C(1)  << width) - 1) << shift;
  C.*ptr &= (T)~Mask;
  C.*ptr |= (T)((Value << shift) & Mask);
  return true;
}

using ParseFx = bool(*)(amd_kernel_code_t &, MCAsmParser &MCParser,
                        raw_ostream &Err);

static ArrayRef<ParseFx> getParserTable() {
  static const ParseFx Table[] = {
#define RECORD(name, altName, print, parse) parse
#include "AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return makeArrayRef(Table);
}

bool llvm::parseAmdKernelCodeField(StringRef ID,
                                   MCAsmParser &MCParser,
                                   amd_kernel_code_t &C,
                                   raw_ostream &Err) {
  const int Idx = get_amd_kernel_code_t_FieldIndex(ID);
  if (Idx < 0) {
    Err << "unexpected amd_kernel_code_t field name " << ID;
    return false;
  }
  auto Parser = getParserTable()[Idx];
  return Parser ? Parser(C, MCParser, Err) : false;
}
