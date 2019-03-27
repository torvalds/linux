#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;

namespace clang {
void EmitClangDataCollectors(RecordKeeper &RK, raw_ostream &OS) {
  const auto &Defs = RK.getClasses();
  for (const auto &Entry : Defs) {
    Record &R = *Entry.second;
    OS << "DEF_ADD_DATA(" << R.getName() << ", {\n";
    auto Code = R.getValue("Code")->getValue();
    OS << Code->getAsUnquotedString() << "}\n)";
    OS << "\n";
  }
  OS << "#undef DEF_ADD_DATA\n";
}
} // end namespace clang
