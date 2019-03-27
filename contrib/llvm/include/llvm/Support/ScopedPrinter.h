//===-- ScopedPrinter.h ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SCOPEDPRINTER_H
#define LLVM_SUPPORT_SCOPEDPRINTER_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

namespace llvm {

template <typename T> struct EnumEntry {
  StringRef Name;
  // While Name suffices in most of the cases, in certain cases
  // GNU style and LLVM style of ELFDumper do not
  // display same string for same enum. The AltName if initialized appropriately
  // will hold the string that GNU style emits.
  // Example:
  // "EM_X86_64" string on LLVM style for Elf_Ehdr->e_machine corresponds to
  // "Advanced Micro Devices X86-64" on GNU style
  StringRef AltName;
  T Value;
  EnumEntry(StringRef N, StringRef A, T V) : Name(N), AltName(A), Value(V) {}
  EnumEntry(StringRef N, T V) : Name(N), AltName(N), Value(V) {}
};

struct HexNumber {
  // To avoid sign-extension we have to explicitly cast to the appropriate
  // unsigned type. The overloads are here so that every type that is implicitly
  // convertible to an integer (including enums and endian helpers) can be used
  // without requiring type traits or call-site changes.
  HexNumber(char Value) : Value(static_cast<unsigned char>(Value)) {}
  HexNumber(signed char Value) : Value(static_cast<unsigned char>(Value)) {}
  HexNumber(signed short Value) : Value(static_cast<unsigned short>(Value)) {}
  HexNumber(signed int Value) : Value(static_cast<unsigned int>(Value)) {}
  HexNumber(signed long Value) : Value(static_cast<unsigned long>(Value)) {}
  HexNumber(signed long long Value)
      : Value(static_cast<unsigned long long>(Value)) {}
  HexNumber(unsigned char Value) : Value(Value) {}
  HexNumber(unsigned short Value) : Value(Value) {}
  HexNumber(unsigned int Value) : Value(Value) {}
  HexNumber(unsigned long Value) : Value(Value) {}
  HexNumber(unsigned long long Value) : Value(Value) {}
  uint64_t Value;
};

raw_ostream &operator<<(raw_ostream &OS, const HexNumber &Value);
const std::string to_hexString(uint64_t Value, bool UpperCase = true);

template <class T> const std::string to_string(const T &Value) {
  std::string number;
  llvm::raw_string_ostream stream(number);
  stream << Value;
  return stream.str();
}

class ScopedPrinter {
public:
  ScopedPrinter(raw_ostream &OS) : OS(OS), IndentLevel(0) {}

  void flush() { OS.flush(); }

  void indent(int Levels = 1) { IndentLevel += Levels; }

  void unindent(int Levels = 1) {
    IndentLevel = std::max(0, IndentLevel - Levels);
  }

  void resetIndent() { IndentLevel = 0; }

  int getIndentLevel() { return IndentLevel; }

  void setPrefix(StringRef P) { Prefix = P; }

  void printIndent() {
    OS << Prefix;
    for (int i = 0; i < IndentLevel; ++i)
      OS << "  ";
  }

  template <typename T> HexNumber hex(T Value) { return HexNumber(Value); }

  template <typename T, typename TEnum>
  void printEnum(StringRef Label, T Value,
                 ArrayRef<EnumEntry<TEnum>> EnumValues) {
    StringRef Name;
    bool Found = false;
    for (const auto &EnumItem : EnumValues) {
      if (EnumItem.Value == Value) {
        Name = EnumItem.Name;
        Found = true;
        break;
      }
    }

    if (Found) {
      startLine() << Label << ": " << Name << " (" << hex(Value) << ")\n";
    } else {
      startLine() << Label << ": " << hex(Value) << "\n";
    }
  }

  template <typename T, typename TFlag>
  void printFlags(StringRef Label, T Value, ArrayRef<EnumEntry<TFlag>> Flags,
                  TFlag EnumMask1 = {}, TFlag EnumMask2 = {},
                  TFlag EnumMask3 = {}) {
    typedef EnumEntry<TFlag> FlagEntry;
    typedef SmallVector<FlagEntry, 10> FlagVector;
    FlagVector SetFlags;

    for (const auto &Flag : Flags) {
      if (Flag.Value == 0)
        continue;

      TFlag EnumMask{};
      if (Flag.Value & EnumMask1)
        EnumMask = EnumMask1;
      else if (Flag.Value & EnumMask2)
        EnumMask = EnumMask2;
      else if (Flag.Value & EnumMask3)
        EnumMask = EnumMask3;
      bool IsEnum = (Flag.Value & EnumMask) != 0;
      if ((!IsEnum && (Value & Flag.Value) == Flag.Value) ||
          (IsEnum && (Value & EnumMask) == Flag.Value)) {
        SetFlags.push_back(Flag);
      }
    }

    llvm::sort(SetFlags, &flagName<TFlag>);

    startLine() << Label << " [ (" << hex(Value) << ")\n";
    for (const auto &Flag : SetFlags) {
      startLine() << "  " << Flag.Name << " (" << hex(Flag.Value) << ")\n";
    }
    startLine() << "]\n";
  }

  template <typename T> void printFlags(StringRef Label, T Value) {
    startLine() << Label << " [ (" << hex(Value) << ")\n";
    uint64_t Flag = 1;
    uint64_t Curr = Value;
    while (Curr > 0) {
      if (Curr & 1)
        startLine() << "  " << hex(Flag) << "\n";
      Curr >>= 1;
      Flag <<= 1;
    }
    startLine() << "]\n";
  }

  void printNumber(StringRef Label, uint64_t Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printNumber(StringRef Label, uint32_t Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printNumber(StringRef Label, uint16_t Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printNumber(StringRef Label, uint8_t Value) {
    startLine() << Label << ": " << unsigned(Value) << "\n";
  }

  void printNumber(StringRef Label, int64_t Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printNumber(StringRef Label, int32_t Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printNumber(StringRef Label, int16_t Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printNumber(StringRef Label, int8_t Value) {
    startLine() << Label << ": " << int(Value) << "\n";
  }

  void printNumber(StringRef Label, const APSInt &Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printBoolean(StringRef Label, bool Value) {
    startLine() << Label << ": " << (Value ? "Yes" : "No") << '\n';
  }

  template <typename... T> void printVersion(StringRef Label, T... Version) {
    startLine() << Label << ": ";
    printVersionInternal(Version...);
    getOStream() << "\n";
  }

  template <typename T> void printList(StringRef Label, const T &List) {
    startLine() << Label << ": [";
    bool Comma = false;
    for (const auto &Item : List) {
      if (Comma)
        OS << ", ";
      OS << Item;
      Comma = true;
    }
    OS << "]\n";
  }

  template <typename T, typename U>
  void printList(StringRef Label, const T &List, const U &Printer) {
    startLine() << Label << ": [";
    bool Comma = false;
    for (const auto &Item : List) {
      if (Comma)
        OS << ", ";
      Printer(OS, Item);
      Comma = true;
    }
    OS << "]\n";
  }

  template <typename T> void printHexList(StringRef Label, const T &List) {
    startLine() << Label << ": [";
    bool Comma = false;
    for (const auto &Item : List) {
      if (Comma)
        OS << ", ";
      OS << hex(Item);
      Comma = true;
    }
    OS << "]\n";
  }

  template <typename T> void printHex(StringRef Label, T Value) {
    startLine() << Label << ": " << hex(Value) << "\n";
  }

  template <typename T> void printHex(StringRef Label, StringRef Str, T Value) {
    startLine() << Label << ": " << Str << " (" << hex(Value) << ")\n";
  }

  template <typename T>
  void printSymbolOffset(StringRef Label, StringRef Symbol, T Value) {
    startLine() << Label << ": " << Symbol << '+' << hex(Value) << '\n';
  }

  void printString(StringRef Value) { startLine() << Value << "\n"; }

  void printString(StringRef Label, StringRef Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printString(StringRef Label, const std::string &Value) {
    printString(Label, StringRef(Value));
  }

  void printString(StringRef Label, const char* Value) {
    printString(Label, StringRef(Value));
  }

  template <typename T>
  void printNumber(StringRef Label, StringRef Str, T Value) {
    startLine() << Label << ": " << Str << " (" << Value << ")\n";
  }

  void printBinary(StringRef Label, StringRef Str, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, Str, Value, false);
  }

  void printBinary(StringRef Label, StringRef Str, ArrayRef<char> Value) {
    auto V = makeArrayRef(reinterpret_cast<const uint8_t *>(Value.data()),
                          Value.size());
    printBinaryImpl(Label, Str, V, false);
  }

  void printBinary(StringRef Label, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, StringRef(), Value, false);
  }

  void printBinary(StringRef Label, ArrayRef<char> Value) {
    auto V = makeArrayRef(reinterpret_cast<const uint8_t *>(Value.data()),
                          Value.size());
    printBinaryImpl(Label, StringRef(), V, false);
  }

  void printBinary(StringRef Label, StringRef Value) {
    auto V = makeArrayRef(reinterpret_cast<const uint8_t *>(Value.data()),
                          Value.size());
    printBinaryImpl(Label, StringRef(), V, false);
  }

  void printBinaryBlock(StringRef Label, ArrayRef<uint8_t> Value,
                        uint32_t StartOffset) {
    printBinaryImpl(Label, StringRef(), Value, true, StartOffset);
  }

  void printBinaryBlock(StringRef Label, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, StringRef(), Value, true);
  }

  void printBinaryBlock(StringRef Label, StringRef Value) {
    auto V = makeArrayRef(reinterpret_cast<const uint8_t *>(Value.data()),
                          Value.size());
    printBinaryImpl(Label, StringRef(), V, true);
  }

  template <typename T> void printObject(StringRef Label, const T &Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  raw_ostream &startLine() {
    printIndent();
    return OS;
  }

  raw_ostream &getOStream() { return OS; }

private:
  template <typename T> void printVersionInternal(T Value) {
    getOStream() << Value;
  }

  template <typename S, typename T, typename... TArgs>
  void printVersionInternal(S Value, T Value2, TArgs... Args) {
    getOStream() << Value << ".";
    printVersionInternal(Value2, Args...);
  }

  template <typename T>
  static bool flagName(const EnumEntry<T> &lhs, const EnumEntry<T> &rhs) {
    return lhs.Name < rhs.Name;
  }

  void printBinaryImpl(StringRef Label, StringRef Str, ArrayRef<uint8_t> Value,
                       bool Block, uint32_t StartOffset = 0);

  raw_ostream &OS;
  int IndentLevel;
  StringRef Prefix;
};

template <>
inline void
ScopedPrinter::printHex<support::ulittle16_t>(StringRef Label,
                                              support::ulittle16_t Value) {
  startLine() << Label << ": " << hex(Value) << "\n";
}

template<char Open, char Close>
struct DelimitedScope {
  explicit DelimitedScope(ScopedPrinter &W) : W(W) {
    W.startLine() << Open << '\n';
    W.indent();
  }

  DelimitedScope(ScopedPrinter &W, StringRef N) : W(W) {
    W.startLine() << N;
    if (!N.empty())
      W.getOStream() << ' ';
    W.getOStream() << Open << '\n';
    W.indent();
  }

  ~DelimitedScope() {
    W.unindent();
    W.startLine() << Close << '\n';
  }

  ScopedPrinter &W;
};

using DictScope = DelimitedScope<'{', '}'>;
using ListScope = DelimitedScope<'[', ']'>;

} // namespace llvm

#endif
