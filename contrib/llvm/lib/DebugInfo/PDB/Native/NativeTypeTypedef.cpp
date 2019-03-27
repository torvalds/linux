#include "llvm/DebugInfo/PDB/Native/NativeTypeTypedef.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeTypeTypedef::NativeTypeTypedef(NativeSession &Session, SymIndexId Id,
                                     codeview::UDTSym Typedef)
    : NativeRawSymbol(Session, PDB_SymType::Typedef, Id),
      Record(std::move(Typedef)) {}

NativeTypeTypedef::~NativeTypeTypedef() {}

void NativeTypeTypedef::dump(raw_ostream &OS, int Indent,
                             PdbSymbolIdField ShowIdFields,
                             PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);
  dumpSymbolField(OS, "name", getName(), Indent);
  dumpSymbolIdField(OS, "typeId", getTypeId(), Indent, Session,
                    PdbSymbolIdField::Type, ShowIdFields, RecurseIdFields);
}

std::string NativeTypeTypedef::getName() const { return Record.Name; }

SymIndexId NativeTypeTypedef::getTypeId() const {
  return Session.getSymbolCache().findSymbolByTypeIndex(Record.Type);
}
