#include "llvm/DebugInfo/PDB/Native/NativeTypeVTShape.h"

using namespace llvm;
using namespace llvm::pdb;

// Create a pointer record for a non-simple type.
NativeTypeVTShape::NativeTypeVTShape(NativeSession &Session, SymIndexId Id,
                                     codeview::TypeIndex TI,
                                     codeview::VFTableShapeRecord SR)
    : NativeRawSymbol(Session, PDB_SymType::VTableShape, Id), TI(TI),
      Record(std::move(SR)) {}

NativeTypeVTShape::~NativeTypeVTShape() {}

void NativeTypeVTShape::dump(raw_ostream &OS, int Indent,
                             PdbSymbolIdField ShowIdFields,
                             PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);

  dumpSymbolIdField(OS, "lexicalParentId", 0, Indent, Session,
                    PdbSymbolIdField::LexicalParent, ShowIdFields,
                    RecurseIdFields);
  dumpSymbolField(OS, "count", getCount(), Indent);
  dumpSymbolField(OS, "constType", isConstType(), Indent);
  dumpSymbolField(OS, "unalignedType", isUnalignedType(), Indent);
  dumpSymbolField(OS, "volatileType", isVolatileType(), Indent);
}

bool NativeTypeVTShape::isConstType() const { return false; }

bool NativeTypeVTShape::isVolatileType() const { return false; }

bool NativeTypeVTShape::isUnalignedType() const { return false; }

uint32_t NativeTypeVTShape::getCount() const { return Record.Slots.size(); }
