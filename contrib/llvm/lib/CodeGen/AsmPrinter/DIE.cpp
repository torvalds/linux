//===--- lib/CodeGen/DIE.cpp - DWARF Info Entries -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Data structures for DWARF info entries.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DIE.h"
#include "DwarfCompileUnit.h"
#include "DwarfDebug.h"
#include "DwarfUnit.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

//===----------------------------------------------------------------------===//
// DIEAbbrevData Implementation
//===----------------------------------------------------------------------===//

/// Profile - Used to gather unique data for the abbreviation folding set.
///
void DIEAbbrevData::Profile(FoldingSetNodeID &ID) const {
  // Explicitly cast to an integer type for which FoldingSetNodeID has
  // overloads.  Otherwise MSVC 2010 thinks this call is ambiguous.
  ID.AddInteger(unsigned(Attribute));
  ID.AddInteger(unsigned(Form));
  if (Form == dwarf::DW_FORM_implicit_const)
    ID.AddInteger(Value);
}

//===----------------------------------------------------------------------===//
// DIEAbbrev Implementation
//===----------------------------------------------------------------------===//

/// Profile - Used to gather unique data for the abbreviation folding set.
///
void DIEAbbrev::Profile(FoldingSetNodeID &ID) const {
  ID.AddInteger(unsigned(Tag));
  ID.AddInteger(unsigned(Children));

  // For each attribute description.
  for (unsigned i = 0, N = Data.size(); i < N; ++i)
    Data[i].Profile(ID);
}

/// Emit - Print the abbreviation using the specified asm printer.
///
void DIEAbbrev::Emit(const AsmPrinter *AP) const {
  // Emit its Dwarf tag type.
  AP->EmitULEB128(Tag, dwarf::TagString(Tag).data());

  // Emit whether it has children DIEs.
  AP->EmitULEB128((unsigned)Children, dwarf::ChildrenString(Children).data());

  // For each attribute description.
  for (unsigned i = 0, N = Data.size(); i < N; ++i) {
    const DIEAbbrevData &AttrData = Data[i];

    // Emit attribute type.
    AP->EmitULEB128(AttrData.getAttribute(),
                    dwarf::AttributeString(AttrData.getAttribute()).data());

    // Emit form type.
#ifndef NDEBUG
    // Could be an assertion, but this way we can see the failing form code
    // easily, which helps track down where it came from.
    if (!dwarf::isValidFormForVersion(AttrData.getForm(),
                                      AP->getDwarfVersion())) {
      LLVM_DEBUG(dbgs() << "Invalid form " << format("0x%x", AttrData.getForm())
                        << " for DWARF version " << AP->getDwarfVersion()
                        << "\n");
      llvm_unreachable("Invalid form for specified DWARF version");
    }
#endif
    AP->EmitULEB128(AttrData.getForm(),
                    dwarf::FormEncodingString(AttrData.getForm()).data());

    // Emit value for DW_FORM_implicit_const.
    if (AttrData.getForm() == dwarf::DW_FORM_implicit_const)
      AP->EmitSLEB128(AttrData.getValue());
  }

  // Mark end of abbreviation.
  AP->EmitULEB128(0, "EOM(1)");
  AP->EmitULEB128(0, "EOM(2)");
}

LLVM_DUMP_METHOD
void DIEAbbrev::print(raw_ostream &O) const {
  O << "Abbreviation @"
    << format("0x%lx", (long)(intptr_t)this)
    << "  "
    << dwarf::TagString(Tag)
    << " "
    << dwarf::ChildrenString(Children)
    << '\n';

  for (unsigned i = 0, N = Data.size(); i < N; ++i) {
    O << "  "
      << dwarf::AttributeString(Data[i].getAttribute())
      << "  "
      << dwarf::FormEncodingString(Data[i].getForm());

    if (Data[i].getForm() == dwarf::DW_FORM_implicit_const)
      O << " " << Data[i].getValue();

    O << '\n';
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DIEAbbrev::dump() const {
  print(dbgs());
}
#endif

//===----------------------------------------------------------------------===//
// DIEAbbrevSet Implementation
//===----------------------------------------------------------------------===//

DIEAbbrevSet::~DIEAbbrevSet() {
  for (DIEAbbrev *Abbrev : Abbreviations)
    Abbrev->~DIEAbbrev();
}

DIEAbbrev &DIEAbbrevSet::uniqueAbbreviation(DIE &Die) {

  FoldingSetNodeID ID;
  DIEAbbrev Abbrev = Die.generateAbbrev();
  Abbrev.Profile(ID);

  void *InsertPos;
  if (DIEAbbrev *Existing =
          AbbreviationsSet.FindNodeOrInsertPos(ID, InsertPos)) {
    Die.setAbbrevNumber(Existing->getNumber());
    return *Existing;
  }

  // Move the abbreviation to the heap and assign a number.
  DIEAbbrev *New = new (Alloc) DIEAbbrev(std::move(Abbrev));
  Abbreviations.push_back(New);
  New->setNumber(Abbreviations.size());
  Die.setAbbrevNumber(Abbreviations.size());

  // Store it for lookup.
  AbbreviationsSet.InsertNode(New, InsertPos);
  return *New;
}

void DIEAbbrevSet::Emit(const AsmPrinter *AP, MCSection *Section) const {
  if (!Abbreviations.empty()) {
    // Start the debug abbrev section.
    AP->OutStreamer->SwitchSection(Section);
    AP->emitDwarfAbbrevs(Abbreviations);
  }
}

//===----------------------------------------------------------------------===//
// DIE Implementation
//===----------------------------------------------------------------------===//

DIE *DIE::getParent() const {
  return Owner.dyn_cast<DIE*>();
}

DIEAbbrev DIE::generateAbbrev() const {
  DIEAbbrev Abbrev(Tag, hasChildren());
  for (const DIEValue &V : values())
    if (V.getForm() == dwarf::DW_FORM_implicit_const)
      Abbrev.AddImplicitConstAttribute(V.getAttribute(),
                                       V.getDIEInteger().getValue());
    else
      Abbrev.AddAttribute(V.getAttribute(), V.getForm());
  return Abbrev;
}

unsigned DIE::getDebugSectionOffset() const {
  const DIEUnit *Unit = getUnit();
  assert(Unit && "DIE must be owned by a DIEUnit to get its absolute offset");
  return Unit->getDebugSectionOffset() + getOffset();
}

const DIE *DIE::getUnitDie() const {
  const DIE *p = this;
  while (p) {
    if (p->getTag() == dwarf::DW_TAG_compile_unit ||
        p->getTag() == dwarf::DW_TAG_type_unit)
      return p;
    p = p->getParent();
  }
  return nullptr;
}

const DIEUnit *DIE::getUnit() const {
  const DIE *UnitDie = getUnitDie();
  if (UnitDie)
    return UnitDie->Owner.dyn_cast<DIEUnit*>();
  return nullptr;
}

DIEValue DIE::findAttribute(dwarf::Attribute Attribute) const {
  // Iterate through all the attributes until we find the one we're
  // looking for, if we can't find it return NULL.
  for (const auto &V : values())
    if (V.getAttribute() == Attribute)
      return V;
  return DIEValue();
}

LLVM_DUMP_METHOD
static void printValues(raw_ostream &O, const DIEValueList &Values,
                        StringRef Type, unsigned Size, unsigned IndentCount) {
  O << Type << ": Size: " << Size << "\n";

  unsigned I = 0;
  const std::string Indent(IndentCount, ' ');
  for (const auto &V : Values.values()) {
    O << Indent;
    O << "Blk[" << I++ << "]";
    O << "  " << dwarf::FormEncodingString(V.getForm()) << " ";
    V.print(O);
    O << "\n";
  }
}

LLVM_DUMP_METHOD
void DIE::print(raw_ostream &O, unsigned IndentCount) const {
  const std::string Indent(IndentCount, ' ');
  O << Indent << "Die: " << format("0x%lx", (long)(intptr_t) this)
    << ", Offset: " << Offset << ", Size: " << Size << "\n";

  O << Indent << dwarf::TagString(getTag()) << " "
    << dwarf::ChildrenString(hasChildren()) << "\n";

  IndentCount += 2;
  for (const auto &V : values()) {
    O << Indent;
    O << dwarf::AttributeString(V.getAttribute());
    O << "  " << dwarf::FormEncodingString(V.getForm()) << " ";
    V.print(O);
    O << "\n";
  }
  IndentCount -= 2;

  for (const auto &Child : children())
    Child.print(O, IndentCount + 4);

  O << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DIE::dump() const {
  print(dbgs());
}
#endif

unsigned DIE::computeOffsetsAndAbbrevs(const AsmPrinter *AP,
                                       DIEAbbrevSet &AbbrevSet,
                                       unsigned CUOffset) {
  // Unique the abbreviation and fill in the abbreviation number so this DIE
  // can be emitted.
  const DIEAbbrev &Abbrev = AbbrevSet.uniqueAbbreviation(*this);

  // Set compile/type unit relative offset of this DIE.
  setOffset(CUOffset);

  // Add the byte size of the abbreviation code.
  CUOffset += getULEB128Size(getAbbrevNumber());

  // Add the byte size of all the DIE attribute values.
  for (const auto &V : values())
    CUOffset += V.SizeOf(AP);

  // Let the children compute their offsets and abbreviation numbers.
  if (hasChildren()) {
    (void)Abbrev;
    assert(Abbrev.hasChildren() && "Children flag not set");

    for (auto &Child : children())
      CUOffset = Child.computeOffsetsAndAbbrevs(AP, AbbrevSet, CUOffset);

    // Each child chain is terminated with a zero byte, adjust the offset.
    CUOffset += sizeof(int8_t);
  }

  // Compute the byte size of this DIE and all of its children correctly. This
  // is needed so that top level DIE can help the compile unit set its length
  // correctly.
  setSize(CUOffset - getOffset());
  return CUOffset;
}

//===----------------------------------------------------------------------===//
// DIEUnit Implementation
//===----------------------------------------------------------------------===//
DIEUnit::DIEUnit(uint16_t V, uint8_t A, dwarf::Tag UnitTag)
    : Die(UnitTag), Section(nullptr), Offset(0), Length(0), Version(V),
      AddrSize(A)
{
  Die.Owner = this;
  assert((UnitTag == dwarf::DW_TAG_compile_unit ||
          UnitTag == dwarf::DW_TAG_type_unit ||
          UnitTag == dwarf::DW_TAG_partial_unit) && "expected a unit TAG");
}

void DIEValue::EmitValue(const AsmPrinter *AP) const {
  switch (Ty) {
  case isNone:
    llvm_unreachable("Expected valid DIEValue");
#define HANDLE_DIEVALUE(T)                                                     \
  case is##T:                                                                  \
    getDIE##T().EmitValue(AP, Form);                                           \
    break;
#include "llvm/CodeGen/DIEValue.def"
  }
}

unsigned DIEValue::SizeOf(const AsmPrinter *AP) const {
  switch (Ty) {
  case isNone:
    llvm_unreachable("Expected valid DIEValue");
#define HANDLE_DIEVALUE(T)                                                     \
  case is##T:                                                                  \
    return getDIE##T().SizeOf(AP, Form);
#include "llvm/CodeGen/DIEValue.def"
  }
  llvm_unreachable("Unknown DIE kind");
}

LLVM_DUMP_METHOD
void DIEValue::print(raw_ostream &O) const {
  switch (Ty) {
  case isNone:
    llvm_unreachable("Expected valid DIEValue");
#define HANDLE_DIEVALUE(T)                                                     \
  case is##T:                                                                  \
    getDIE##T().print(O);                                                      \
    break;
#include "llvm/CodeGen/DIEValue.def"
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DIEValue::dump() const {
  print(dbgs());
}
#endif

//===----------------------------------------------------------------------===//
// DIEInteger Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit integer of appropriate size.
///
void DIEInteger::EmitValue(const AsmPrinter *Asm, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_implicit_const:
  case dwarf::DW_FORM_flag_present:
    // Emit something to keep the lines and comments in sync.
    // FIXME: Is there a better way to do this?
    Asm->OutStreamer->AddBlankLine();
    return;
  case dwarf::DW_FORM_flag:
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_data1:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_addrx1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_data2:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_addrx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_strp:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_data4:
  case dwarf::DW_FORM_ref_sup4:
  case dwarf::DW_FORM_strx4:
  case dwarf::DW_FORM_addrx4:
  case dwarf::DW_FORM_ref8:
  case dwarf::DW_FORM_ref_sig8:
  case dwarf::DW_FORM_data8:
  case dwarf::DW_FORM_ref_sup8:
  case dwarf::DW_FORM_GNU_ref_alt:
  case dwarf::DW_FORM_GNU_strp_alt:
  case dwarf::DW_FORM_line_strp:
  case dwarf::DW_FORM_sec_offset:
  case dwarf::DW_FORM_strp_sup:
  case dwarf::DW_FORM_addr:
  case dwarf::DW_FORM_ref_addr:
    Asm->OutStreamer->EmitIntValue(Integer, SizeOf(Asm, Form));
    return;
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_GNU_addr_index:
  case dwarf::DW_FORM_ref_udata:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_addrx:
  case dwarf::DW_FORM_rnglistx:
  case dwarf::DW_FORM_udata:
    Asm->EmitULEB128(Integer);
    return;
  case dwarf::DW_FORM_sdata:
    Asm->EmitSLEB128(Integer);
    return;
  default: llvm_unreachable("DIE Value form not supported yet");
  }
}

/// SizeOf - Determine size of integer value in bytes.
///
unsigned DIEInteger::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  dwarf::FormParams Params = {0, 0, dwarf::DWARF32};
  if (AP)
    Params = {AP->getDwarfVersion(), uint8_t(AP->getPointerSize()),
              AP->OutStreamer->getContext().getDwarfFormat()};

  if (Optional<uint8_t> FixedSize = dwarf::getFixedFormByteSize(Form, Params))
    return *FixedSize;

  switch (Form) {
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_GNU_addr_index:
  case dwarf::DW_FORM_ref_udata:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_addrx:
  case dwarf::DW_FORM_rnglistx:
  case dwarf::DW_FORM_udata:
    return getULEB128Size(Integer);
  case dwarf::DW_FORM_sdata:
    return getSLEB128Size(Integer);
  default: llvm_unreachable("DIE Value form not supported yet");
  }
}

LLVM_DUMP_METHOD
void DIEInteger::print(raw_ostream &O) const {
  O << "Int: " << (int64_t)Integer << "  0x";
  O.write_hex(Integer);
}

//===----------------------------------------------------------------------===//
// DIEExpr Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit expression value.
///
void DIEExpr::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  AP->EmitDebugValue(Expr, SizeOf(AP, Form));
}

/// SizeOf - Determine size of expression value in bytes.
///
unsigned DIEExpr::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_data4) return 4;
  if (Form == dwarf::DW_FORM_sec_offset) return 4;
  if (Form == dwarf::DW_FORM_strp) return 4;
  return AP->getPointerSize();
}

LLVM_DUMP_METHOD
void DIEExpr::print(raw_ostream &O) const { O << "Expr: " << *Expr; }

//===----------------------------------------------------------------------===//
// DIELabel Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit label value.
///
void DIELabel::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  AP->EmitLabelReference(Label, SizeOf(AP, Form),
                         Form == dwarf::DW_FORM_strp ||
                             Form == dwarf::DW_FORM_sec_offset ||
                             Form == dwarf::DW_FORM_ref_addr ||
                             Form == dwarf::DW_FORM_data4);
}

/// SizeOf - Determine size of label value in bytes.
///
unsigned DIELabel::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_data4) return 4;
  if (Form == dwarf::DW_FORM_sec_offset) return 4;
  if (Form == dwarf::DW_FORM_strp) return 4;
  return AP->MAI->getCodePointerSize();
}

LLVM_DUMP_METHOD
void DIELabel::print(raw_ostream &O) const { O << "Lbl: " << Label->getName(); }

//===----------------------------------------------------------------------===//
// DIEDelta Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit delta value.
///
void DIEDelta::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  AP->EmitLabelDifference(LabelHi, LabelLo, SizeOf(AP, Form));
}

/// SizeOf - Determine size of delta value in bytes.
///
unsigned DIEDelta::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_data4) return 4;
  if (Form == dwarf::DW_FORM_sec_offset) return 4;
  if (Form == dwarf::DW_FORM_strp) return 4;
  return AP->MAI->getCodePointerSize();
}

LLVM_DUMP_METHOD
void DIEDelta::print(raw_ostream &O) const {
  O << "Del: " << LabelHi->getName() << "-" << LabelLo->getName();
}

//===----------------------------------------------------------------------===//
// DIEString Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit string value.
///
void DIEString::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  // Index of string in symbol table.
  switch (Form) {
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_strx4:
    DIEInteger(S.getIndex()).EmitValue(AP, Form);
    return;
  case dwarf::DW_FORM_strp:
    if (AP->MAI->doesDwarfUseRelocationsAcrossSections())
      DIELabel(S.getSymbol()).EmitValue(AP, Form);
    else
      DIEInteger(S.getOffset()).EmitValue(AP, Form);
    return;
  default:
    llvm_unreachable("Expected valid string form");
  }
}

/// SizeOf - Determine size of delta value in bytes.
///
unsigned DIEString::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  // Index of string in symbol table.
  switch (Form) {
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_strx4:
    return DIEInteger(S.getIndex()).SizeOf(AP, Form);
  case dwarf::DW_FORM_strp:
    if (AP->MAI->doesDwarfUseRelocationsAcrossSections())
      return DIELabel(S.getSymbol()).SizeOf(AP, Form);
    return DIEInteger(S.getOffset()).SizeOf(AP, Form);
  default:
    llvm_unreachable("Expected valid string form");
  }
}

LLVM_DUMP_METHOD
void DIEString::print(raw_ostream &O) const {
  O << "String: " << S.getString();
}

//===----------------------------------------------------------------------===//
// DIEInlineString Implementation
//===----------------------------------------------------------------------===//
void DIEInlineString::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_string) {
    AP->OutStreamer->EmitBytes(S);
    AP->emitInt8(0);
    return;
  }
  llvm_unreachable("Expected valid string form");
}

unsigned DIEInlineString::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  // Emit string bytes + NULL byte.
  return S.size() + 1;
}

LLVM_DUMP_METHOD
void DIEInlineString::print(raw_ostream &O) const {
  O << "InlineString: " << S;
}

//===----------------------------------------------------------------------===//
// DIEEntry Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit debug information entry offset.
///
void DIEEntry::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {

  switch (Form) {
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_ref8:
    AP->OutStreamer->EmitIntValue(Entry->getOffset(), SizeOf(AP, Form));
    return;

  case dwarf::DW_FORM_ref_udata:
    AP->EmitULEB128(Entry->getOffset());
    return;

  case dwarf::DW_FORM_ref_addr: {
    // Get the absolute offset for this DIE within the debug info/types section.
    unsigned Addr = Entry->getDebugSectionOffset();
    if (const MCSymbol *SectionSym =
            Entry->getUnit()->getCrossSectionRelativeBaseAddress()) {
      AP->EmitLabelPlusOffset(SectionSym, Addr, SizeOf(AP, Form), true);
      return;
    }

    AP->OutStreamer->EmitIntValue(Addr, SizeOf(AP, Form));
    return;
  }
  default:
    llvm_unreachable("Improper form for DIE reference");
  }
}

unsigned DIEEntry::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_ref1:
    return 1;
  case dwarf::DW_FORM_ref2:
    return 2;
  case dwarf::DW_FORM_ref4:
    return 4;
  case dwarf::DW_FORM_ref8:
    return 8;
  case dwarf::DW_FORM_ref_udata:
    return getULEB128Size(Entry->getOffset());
  case dwarf::DW_FORM_ref_addr:
    if (AP->getDwarfVersion() == 2)
      return AP->MAI->getCodePointerSize();
    switch (AP->OutStreamer->getContext().getDwarfFormat()) {
    case dwarf::DWARF32:
      return 4;
    case dwarf::DWARF64:
      return 8;
    }
    llvm_unreachable("Invalid DWARF format");

  default:
    llvm_unreachable("Improper form for DIE reference");
  }
}

LLVM_DUMP_METHOD
void DIEEntry::print(raw_ostream &O) const {
  O << format("Die: 0x%lx", (long)(intptr_t)&Entry);
}

//===----------------------------------------------------------------------===//
// DIELoc Implementation
//===----------------------------------------------------------------------===//

/// ComputeSize - calculate the size of the location expression.
///
unsigned DIELoc::ComputeSize(const AsmPrinter *AP) const {
  if (!Size) {
    for (const auto &V : values())
      Size += V.SizeOf(AP);
  }

  return Size;
}

/// EmitValue - Emit location data.
///
void DIELoc::EmitValue(const AsmPrinter *Asm, dwarf::Form Form) const {
  switch (Form) {
  default: llvm_unreachable("Improper form for block");
  case dwarf::DW_FORM_block1: Asm->emitInt8(Size);    break;
  case dwarf::DW_FORM_block2: Asm->emitInt16(Size);   break;
  case dwarf::DW_FORM_block4: Asm->emitInt32(Size);   break;
  case dwarf::DW_FORM_block:
  case dwarf::DW_FORM_exprloc:
    Asm->EmitULEB128(Size); break;
  }

  for (const auto &V : values())
    V.EmitValue(Asm);
}

/// SizeOf - Determine size of location data in bytes.
///
unsigned DIELoc::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_block1: return Size + sizeof(int8_t);
  case dwarf::DW_FORM_block2: return Size + sizeof(int16_t);
  case dwarf::DW_FORM_block4: return Size + sizeof(int32_t);
  case dwarf::DW_FORM_block:
  case dwarf::DW_FORM_exprloc:
    return Size + getULEB128Size(Size);
  default: llvm_unreachable("Improper form for block");
  }
}

LLVM_DUMP_METHOD
void DIELoc::print(raw_ostream &O) const {
  printValues(O, *this, "ExprLoc", Size, 5);
}

//===----------------------------------------------------------------------===//
// DIEBlock Implementation
//===----------------------------------------------------------------------===//

/// ComputeSize - calculate the size of the block.
///
unsigned DIEBlock::ComputeSize(const AsmPrinter *AP) const {
  if (!Size) {
    for (const auto &V : values())
      Size += V.SizeOf(AP);
  }

  return Size;
}

/// EmitValue - Emit block data.
///
void DIEBlock::EmitValue(const AsmPrinter *Asm, dwarf::Form Form) const {
  switch (Form) {
  default: llvm_unreachable("Improper form for block");
  case dwarf::DW_FORM_block1: Asm->emitInt8(Size);    break;
  case dwarf::DW_FORM_block2: Asm->emitInt16(Size);   break;
  case dwarf::DW_FORM_block4: Asm->emitInt32(Size);   break;
  case dwarf::DW_FORM_block:  Asm->EmitULEB128(Size); break;
  case dwarf::DW_FORM_string: break;
  case dwarf::DW_FORM_data16: break;
  }

  for (const auto &V : values())
    V.EmitValue(Asm);
}

/// SizeOf - Determine size of block data in bytes.
///
unsigned DIEBlock::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_block1: return Size + sizeof(int8_t);
  case dwarf::DW_FORM_block2: return Size + sizeof(int16_t);
  case dwarf::DW_FORM_block4: return Size + sizeof(int32_t);
  case dwarf::DW_FORM_block:  return Size + getULEB128Size(Size);
  case dwarf::DW_FORM_data16: return 16;
  default: llvm_unreachable("Improper form for block");
  }
}

LLVM_DUMP_METHOD
void DIEBlock::print(raw_ostream &O) const {
  printValues(O, *this, "Blk", Size, 5);
}

//===----------------------------------------------------------------------===//
// DIELocList Implementation
//===----------------------------------------------------------------------===//

unsigned DIELocList::SizeOf(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_data4)
    return 4;
  if (Form == dwarf::DW_FORM_sec_offset)
    return 4;
  return AP->MAI->getCodePointerSize();
}

/// EmitValue - Emit label value.
///
void DIELocList::EmitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  DwarfDebug *DD = AP->getDwarfDebug();
  MCSymbol *Label = DD->getDebugLocs().getList(Index).Label;
  AP->emitDwarfSymbolReference(Label, /*ForceOffset*/ DD->useSplitDwarf());
}

LLVM_DUMP_METHOD
void DIELocList::print(raw_ostream &O) const { O << "LocList: " << Index; }
