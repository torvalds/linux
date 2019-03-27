//===- Attributes.cpp - Generate attributes -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "attr-enum"

namespace {

class Attributes {
public:
  Attributes(RecordKeeper &R) : Records(R) {}
  void emit(raw_ostream &OS);

private:
  void emitTargetIndependentEnums(raw_ostream &OS);
  void emitConversionFn(raw_ostream &OS);
  void emitFnAttrCompatCheck(raw_ostream &OS, bool IsStringAttr);

  void printEnumAttrClasses(raw_ostream &OS,
                            const std::vector<Record *> &Records);
  void printStrBoolAttrClasses(raw_ostream &OS,
                               const std::vector<Record *> &Records);

  RecordKeeper &Records;
};

} // End anonymous namespace.

void Attributes::emitTargetIndependentEnums(raw_ostream &OS) {
  OS << "#ifdef GET_ATTR_ENUM\n";
  OS << "#undef GET_ATTR_ENUM\n";

  std::vector<Record*> Attrs =
      Records.getAllDerivedDefinitions("EnumAttr");

  for (auto A : Attrs)
    OS << A->getName() << ",\n";

  OS << "#endif\n";
}

void Attributes::emitConversionFn(raw_ostream &OS) {
  OS << "#ifdef GET_ATTR_KIND_FROM_NAME\n";
  OS << "#undef GET_ATTR_KIND_FROM_NAME\n";

  std::vector<Record*> Attrs =
      Records.getAllDerivedDefinitions("EnumAttr");

  OS << "static Attribute::AttrKind getAttrKindFromName(StringRef AttrName) {\n";
  OS << "  return StringSwitch<Attribute::AttrKind>(AttrName)\n";

  for (auto A : Attrs) {
    OS << "    .Case(\"" << A->getValueAsString("AttrString");
    OS << "\", Attribute::" << A->getName() << ")\n";
  }

  OS << "    .Default(Attribute::None);\n";
  OS << "}\n\n";

  OS << "#endif\n";
}

void Attributes::emitFnAttrCompatCheck(raw_ostream &OS, bool IsStringAttr) {
  OS << "#ifdef GET_ATTR_COMPAT_FUNC\n";
  OS << "#undef GET_ATTR_COMPAT_FUNC\n";

  OS << "struct EnumAttr {\n";
  OS << "  static bool isSet(const Function &Fn,\n";
  OS << "                    Attribute::AttrKind Kind) {\n";
  OS << "    return Fn.hasFnAttribute(Kind);\n";
  OS << "  }\n\n";
  OS << "  static void set(Function &Fn,\n";
  OS << "                  Attribute::AttrKind Kind, bool Val) {\n";
  OS << "    if (Val)\n";
  OS << "      Fn.addFnAttr(Kind);\n";
  OS << "    else\n";
  OS << "      Fn.removeFnAttr(Kind);\n";
  OS << "  }\n";
  OS << "};\n\n";

  OS << "struct StrBoolAttr {\n";
  OS << "  static bool isSet(const Function &Fn,\n";
  OS << "                    StringRef Kind) {\n";
  OS << "    auto A = Fn.getFnAttribute(Kind);\n";
  OS << "    return A.getValueAsString().equals(\"true\");\n";
  OS << "  }\n\n";
  OS << "  static void set(Function &Fn,\n";
  OS << "                  StringRef Kind, bool Val) {\n";
  OS << "    Fn.addFnAttr(Kind, Val ? \"true\" : \"false\");\n";
  OS << "  }\n";
  OS << "};\n\n";

  printEnumAttrClasses(OS ,Records.getAllDerivedDefinitions("EnumAttr"));
  printStrBoolAttrClasses(OS , Records.getAllDerivedDefinitions("StrBoolAttr"));

  OS << "static inline bool hasCompatibleFnAttrs(const Function &Caller,\n"
     << "                                        const Function &Callee) {\n";
  OS << "  bool Ret = true;\n\n";

  std::vector<Record *> CompatRules =
      Records.getAllDerivedDefinitions("CompatRule");

  for (auto *Rule : CompatRules) {
    StringRef FuncName = Rule->getValueAsString("CompatFunc");
    OS << "  Ret &= " << FuncName << "(Caller, Callee);\n";
  }

  OS << "\n";
  OS << "  return Ret;\n";
  OS << "}\n\n";

  std::vector<Record *> MergeRules =
      Records.getAllDerivedDefinitions("MergeRule");
  OS << "static inline void mergeFnAttrs(Function &Caller,\n"
     << "                                const Function &Callee) {\n";

  for (auto *Rule : MergeRules) {
    StringRef FuncName = Rule->getValueAsString("MergeFunc");
    OS << "  " << FuncName << "(Caller, Callee);\n";
  }

  OS << "}\n\n";

  OS << "#endif\n";
}

void Attributes::printEnumAttrClasses(raw_ostream &OS,
                                      const std::vector<Record *> &Records) {
  OS << "// EnumAttr classes\n";
  for (const auto *R : Records) {
    OS << "struct " << R->getName() << "Attr : EnumAttr {\n";
    OS << "  static enum Attribute::AttrKind getKind() {\n";
    OS << "    return llvm::Attribute::" << R->getName() << ";\n";
    OS << "  }\n";
    OS << "};\n";
  }
  OS << "\n";
}

void Attributes::printStrBoolAttrClasses(raw_ostream &OS,
                                         const std::vector<Record *> &Records) {
  OS << "// StrBoolAttr classes\n";
  for (const auto *R : Records) {
    OS << "struct " << R->getName() << "Attr : StrBoolAttr {\n";
    OS << "  static StringRef getKind() {\n";
    OS << "    return \"" << R->getValueAsString("AttrString") << "\";\n";
    OS << "  }\n";
    OS << "};\n";
  }
  OS << "\n";
}

void Attributes::emit(raw_ostream &OS) {
  emitTargetIndependentEnums(OS);
  emitConversionFn(OS);
  emitFnAttrCompatCheck(OS, false);
}

namespace llvm {

void EmitAttributes(RecordKeeper &RK, raw_ostream &OS) {
  Attributes(RK).emit(OS);
}

} // End llvm namespace.
