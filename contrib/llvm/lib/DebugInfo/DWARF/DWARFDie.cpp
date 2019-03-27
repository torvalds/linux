//===- DWARFDie.cpp -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <utility>

using namespace llvm;
using namespace dwarf;
using namespace object;

static void dumpApplePropertyAttribute(raw_ostream &OS, uint64_t Val) {
  OS << " (";
  do {
    uint64_t Shift = countTrailingZeros(Val);
    assert(Shift < 64 && "undefined behavior");
    uint64_t Bit = 1ULL << Shift;
    auto PropName = ApplePropertyString(Bit);
    if (!PropName.empty())
      OS << PropName;
    else
      OS << format("DW_APPLE_PROPERTY_0x%" PRIx64, Bit);
    if (!(Val ^= Bit))
      break;
    OS << ", ";
  } while (true);
  OS << ")";
}

static void dumpRanges(const DWARFObject &Obj, raw_ostream &OS,
                       const DWARFAddressRangesVector &Ranges,
                       unsigned AddressSize, unsigned Indent,
                       const DIDumpOptions &DumpOpts) {
  if (!DumpOpts.ShowAddresses)
    return;

  ArrayRef<SectionName> SectionNames;
  if (DumpOpts.Verbose)
    SectionNames = Obj.getSectionNames();

  for (const DWARFAddressRange &R : Ranges) {
    OS << '\n';
    OS.indent(Indent);
    R.dump(OS, AddressSize);

    DWARFFormValue::dumpAddressSection(Obj, OS, DumpOpts, R.SectionIndex);
  }
}

static void dumpLocation(raw_ostream &OS, DWARFFormValue &FormValue,
                         DWARFUnit *U, unsigned Indent,
                         DIDumpOptions DumpOpts) {
  DWARFContext &Ctx = U->getContext();
  const DWARFObject &Obj = Ctx.getDWARFObj();
  const MCRegisterInfo *MRI = Ctx.getRegisterInfo();
  if (FormValue.isFormClass(DWARFFormValue::FC_Block) ||
      FormValue.isFormClass(DWARFFormValue::FC_Exprloc)) {
    ArrayRef<uint8_t> Expr = *FormValue.getAsBlock();
    DataExtractor Data(StringRef((const char *)Expr.data(), Expr.size()),
                       Ctx.isLittleEndian(), 0);
    DWARFExpression(Data, U->getVersion(), U->getAddressByteSize())
        .print(OS, MRI);
    return;
  }

  FormValue.dump(OS, DumpOpts);
  if (FormValue.isFormClass(DWARFFormValue::FC_SectionOffset)) {
    uint32_t Offset = *FormValue.getAsSectionOffset();
    if (!U->isDWOUnit() && !U->getLocSection()->Data.empty()) {
      DWARFDebugLoc DebugLoc;
      DWARFDataExtractor Data(Obj, *U->getLocSection(), Ctx.isLittleEndian(),
                              Obj.getAddressSize());
      auto LL = DebugLoc.parseOneLocationList(Data, &Offset);
      if (LL) {
        uint64_t BaseAddr = 0;
        if (Optional<SectionedAddress> BA = U->getBaseAddress())
          BaseAddr = BA->Address;
        LL->dump(OS, Ctx.isLittleEndian(), Obj.getAddressSize(), MRI, BaseAddr,
                 Indent);
      } else
        OS << "error extracting location list.";
      return;
    }

    bool UseLocLists = !U->isDWOUnit();
    StringRef LoclistsSectionData =
        UseLocLists ? Obj.getLoclistsSection().Data : U->getLocSectionData();

    if (!LoclistsSectionData.empty()) {
      DataExtractor Data(LoclistsSectionData, Ctx.isLittleEndian(),
                         Obj.getAddressSize());

      // Old-style location list were used in DWARF v4 (.debug_loc.dwo section).
      // Modern locations list (.debug_loclists) are used starting from v5.
      // Ideally we should take the version from the .debug_loclists section
      // header, but using CU's version for simplicity.
      auto LL = DWARFDebugLoclists::parseOneLocationList(
          Data, &Offset, UseLocLists ? U->getVersion() : 4);

      uint64_t BaseAddr = 0;
      if (Optional<SectionedAddress> BA = U->getBaseAddress())
        BaseAddr = BA->Address;

      if (LL)
        LL->dump(OS, BaseAddr, Ctx.isLittleEndian(), Obj.getAddressSize(), MRI,
                 Indent);
      else
        OS << "error extracting location list.";
    }
  }
}

/// Dump the name encoded in the type tag.
static void dumpTypeTagName(raw_ostream &OS, dwarf::Tag T) {
  StringRef TagStr = TagString(T);
  if (!TagStr.startswith("DW_TAG_") || !TagStr.endswith("_type"))
    return;
  OS << TagStr.substr(7, TagStr.size() - 12) << " ";
}

static void dumpArrayType(raw_ostream &OS, const DWARFDie &D) {
  Optional<uint64_t> Bound;
  for (const DWARFDie &C : D.children())
    if (C.getTag() == DW_TAG_subrange_type) {
      Optional<uint64_t> LB;
      Optional<uint64_t> Count;
      Optional<uint64_t> UB;
      Optional<unsigned> DefaultLB;
      if (Optional<DWARFFormValue> L = C.find(DW_AT_lower_bound))
        LB = L->getAsUnsignedConstant();
      if (Optional<DWARFFormValue> CountV = C.find(DW_AT_count))
        Count = CountV->getAsUnsignedConstant();
      if (Optional<DWARFFormValue> UpperV = C.find(DW_AT_upper_bound))
        UB = UpperV->getAsUnsignedConstant();
      if (Optional<DWARFFormValue> LV =
              D.getDwarfUnit()->getUnitDIE().find(DW_AT_language))
        if (Optional<uint64_t> LC = LV->getAsUnsignedConstant())
          if ((DefaultLB =
                   LanguageLowerBound(static_cast<dwarf::SourceLanguage>(*LC))))
            if (LB && *LB == *DefaultLB)
              LB = None;
      if (!LB && !Count && !UB)
        OS << "[]";
      else if (!LB && (Count || UB) && DefaultLB)
        OS << '[' << (Count ? *Count : *UB - *DefaultLB + 1) << ']';
      else {
        OS << "[[";
        if (LB)
          OS << *LB;
        else
          OS << '?';
        OS << ", ";
        if (Count)
          if (LB)
            OS << *LB + *Count;
          else
            OS << "? + " << *Count;
        else if (UB)
          OS << *UB + 1;
        else
          OS << '?';
        OS << ")]";
      }
    }
}

/// Recursively dump the DIE type name when applicable.
static void dumpTypeName(raw_ostream &OS, const DWARFDie &D) {
  if (!D.isValid())
    return;

  if (const char *Name = D.getName(DINameKind::LinkageName)) {
    OS << Name;
    return;
  }

  // FIXME: We should have pretty printers per language. Currently we print
  // everything as if it was C++ and fall back to the TAG type name.
  const dwarf::Tag T = D.getTag();
  switch (T) {
  case DW_TAG_array_type:
  case DW_TAG_pointer_type:
  case DW_TAG_ptr_to_member_type:
  case DW_TAG_reference_type:
  case DW_TAG_rvalue_reference_type:
  case DW_TAG_subroutine_type:
    break;
  default:
    dumpTypeTagName(OS, T);
  }

  // Follow the DW_AT_type if possible.
  DWARFDie TypeDie = D.getAttributeValueAsReferencedDie(DW_AT_type);
  dumpTypeName(OS, TypeDie);

  switch (T) {
  case DW_TAG_subroutine_type: {
    if (!TypeDie)
      OS << "void";
    OS << '(';
    bool First = true;
    for (const DWARFDie &C : D.children()) {
      if (C.getTag() == DW_TAG_formal_parameter) {
        if (!First)
          OS << ", ";
        First = false;
        dumpTypeName(OS, C.getAttributeValueAsReferencedDie(DW_AT_type));
      }
    }
    OS << ')';
    break;
  }
  case DW_TAG_array_type: {
    dumpArrayType(OS, D);
    break;
  }
  case DW_TAG_pointer_type:
    OS << '*';
    break;
  case DW_TAG_ptr_to_member_type:
    if (DWARFDie Cont =
            D.getAttributeValueAsReferencedDie(DW_AT_containing_type)) {
      dumpTypeName(OS << ' ', Cont);
      OS << "::";
    }
    OS << '*';
    break;
  case DW_TAG_reference_type:
    OS << '&';
    break;
  case DW_TAG_rvalue_reference_type:
    OS << "&&";
    break;
  default:
    break;
  }
}

static void dumpAttribute(raw_ostream &OS, const DWARFDie &Die,
                          uint32_t *OffsetPtr, dwarf::Attribute Attr,
                          dwarf::Form Form, unsigned Indent,
                          DIDumpOptions DumpOpts) {
  if (!Die.isValid())
    return;
  const char BaseIndent[] = "            ";
  OS << BaseIndent;
  OS.indent(Indent + 2);
  WithColor(OS, HighlightColor::Attribute) << formatv("{0}", Attr);

  if (DumpOpts.Verbose || DumpOpts.ShowForm)
    OS << formatv(" [{0}]", Form);

  DWARFUnit *U = Die.getDwarfUnit();
  DWARFFormValue formValue(Form);

  if (!formValue.extractValue(U->getDebugInfoExtractor(), OffsetPtr,
                              U->getFormParams(), U))
    return;

  OS << "\t(";

  StringRef Name;
  std::string File;
  auto Color = HighlightColor::Enumerator;
  if (Attr == DW_AT_decl_file || Attr == DW_AT_call_file) {
    Color = HighlightColor::String;
    if (const auto *LT = U->getContext().getLineTableForUnit(U))
      if (LT->getFileNameByIndex(
              formValue.getAsUnsignedConstant().getValue(),
              U->getCompilationDir(),
              DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, File)) {
        File = '"' + File + '"';
        Name = File;
      }
  } else if (Optional<uint64_t> Val = formValue.getAsUnsignedConstant())
    Name = AttributeValueString(Attr, *Val);

  if (!Name.empty())
    WithColor(OS, Color) << Name;
  else if (Attr == DW_AT_decl_line || Attr == DW_AT_call_line)
    OS << *formValue.getAsUnsignedConstant();
  else if (Attr == DW_AT_high_pc && !DumpOpts.ShowForm && !DumpOpts.Verbose &&
           formValue.getAsUnsignedConstant()) {
    if (DumpOpts.ShowAddresses) {
      // Print the actual address rather than the offset.
      uint64_t LowPC, HighPC, Index;
      if (Die.getLowAndHighPC(LowPC, HighPC, Index))
        OS << format("0x%016" PRIx64, HighPC);
      else
        formValue.dump(OS, DumpOpts);
    }
  } else if (Attr == DW_AT_location || Attr == DW_AT_frame_base ||
             Attr == DW_AT_data_member_location ||
             Attr == DW_AT_GNU_call_site_value)
    dumpLocation(OS, formValue, U, sizeof(BaseIndent) + Indent + 4, DumpOpts);
  else
    formValue.dump(OS, DumpOpts);

  std::string Space = DumpOpts.ShowAddresses ? " " : "";

  // We have dumped the attribute raw value. For some attributes
  // having both the raw value and the pretty-printed value is
  // interesting. These attributes are handled below.
  if (Attr == DW_AT_specification || Attr == DW_AT_abstract_origin) {
    if (const char *Name =
            Die.getAttributeValueAsReferencedDie(formValue).getName(
                DINameKind::LinkageName))
      OS << Space << "\"" << Name << '\"';
  } else if (Attr == DW_AT_type) {
    OS << Space << "\"";
    dumpTypeName(OS, Die.getAttributeValueAsReferencedDie(formValue));
    OS << '"';
  } else if (Attr == DW_AT_APPLE_property_attribute) {
    if (Optional<uint64_t> OptVal = formValue.getAsUnsignedConstant())
      dumpApplePropertyAttribute(OS, *OptVal);
  } else if (Attr == DW_AT_ranges) {
    const DWARFObject &Obj = Die.getDwarfUnit()->getContext().getDWARFObj();
    // For DW_FORM_rnglistx we need to dump the offset separately, since
    // we have only dumped the index so far.
    if (formValue.getForm() == DW_FORM_rnglistx)
      if (auto RangeListOffset =
              U->getRnglistOffset(*formValue.getAsSectionOffset())) {
        DWARFFormValue FV(dwarf::DW_FORM_sec_offset);
        FV.setUValue(*RangeListOffset);
        FV.dump(OS, DumpOpts);
      }
    if (auto RangesOrError = Die.getAddressRanges())
      dumpRanges(Obj, OS, RangesOrError.get(), U->getAddressByteSize(),
                 sizeof(BaseIndent) + Indent + 4, DumpOpts);
    else
      WithColor::error() << "decoding address ranges: "
                         << toString(RangesOrError.takeError()) << '\n';
  }

  OS << ")\n";
}

bool DWARFDie::isSubprogramDIE() const { return getTag() == DW_TAG_subprogram; }

bool DWARFDie::isSubroutineDIE() const {
  auto Tag = getTag();
  return Tag == DW_TAG_subprogram || Tag == DW_TAG_inlined_subroutine;
}

Optional<DWARFFormValue> DWARFDie::find(dwarf::Attribute Attr) const {
  if (!isValid())
    return None;
  auto AbbrevDecl = getAbbreviationDeclarationPtr();
  if (AbbrevDecl)
    return AbbrevDecl->getAttributeValue(getOffset(), Attr, *U);
  return None;
}

Optional<DWARFFormValue>
DWARFDie::find(ArrayRef<dwarf::Attribute> Attrs) const {
  if (!isValid())
    return None;
  auto AbbrevDecl = getAbbreviationDeclarationPtr();
  if (AbbrevDecl) {
    for (auto Attr : Attrs) {
      if (auto Value = AbbrevDecl->getAttributeValue(getOffset(), Attr, *U))
        return Value;
    }
  }
  return None;
}

Optional<DWARFFormValue>
DWARFDie::findRecursively(ArrayRef<dwarf::Attribute> Attrs) const {
  std::vector<DWARFDie> Worklist;
  Worklist.push_back(*this);

  // Keep track if DIEs already seen to prevent infinite recursion.
  // Empirically we rarely see a depth of more than 3 when dealing with valid
  // DWARF. This corresponds to following the DW_AT_abstract_origin and
  // DW_AT_specification just once.
  SmallSet<DWARFDie, 3> Seen;

  while (!Worklist.empty()) {
    DWARFDie Die = Worklist.back();
    Worklist.pop_back();

    if (!Die.isValid())
      continue;

    if (Seen.count(Die))
      continue;

    Seen.insert(Die);

    if (auto Value = Die.find(Attrs))
      return Value;

    if (auto D = Die.getAttributeValueAsReferencedDie(DW_AT_abstract_origin))
      Worklist.push_back(D);

    if (auto D = Die.getAttributeValueAsReferencedDie(DW_AT_specification))
      Worklist.push_back(D);
  }

  return None;
}

DWARFDie
DWARFDie::getAttributeValueAsReferencedDie(dwarf::Attribute Attr) const {
  if (Optional<DWARFFormValue> F = find(Attr))
    return getAttributeValueAsReferencedDie(*F);
  return DWARFDie();
}

DWARFDie
DWARFDie::getAttributeValueAsReferencedDie(const DWARFFormValue &V) const {
  if (auto SpecRef = toReference(V)) {
    if (auto SpecUnit = U->getUnitVector().getUnitForOffset(*SpecRef))
      return SpecUnit->getDIEForOffset(*SpecRef);
  }
  return DWARFDie();
}

Optional<uint64_t> DWARFDie::getRangesBaseAttribute() const {
  return toSectionOffset(find({DW_AT_rnglists_base, DW_AT_GNU_ranges_base}));
}

Optional<uint64_t> DWARFDie::getHighPC(uint64_t LowPC) const {
  if (auto FormValue = find(DW_AT_high_pc)) {
    if (auto Address = FormValue->getAsAddress()) {
      // High PC is an address.
      return Address;
    }
    if (auto Offset = FormValue->getAsUnsignedConstant()) {
      // High PC is an offset from LowPC.
      return LowPC + *Offset;
    }
  }
  return None;
}

bool DWARFDie::getLowAndHighPC(uint64_t &LowPC, uint64_t &HighPC,
                               uint64_t &SectionIndex) const {
  auto F = find(DW_AT_low_pc);
  auto LowPcAddr = toSectionedAddress(F);
  if (!LowPcAddr)
    return false;
  if (auto HighPcAddr = getHighPC(LowPcAddr->Address)) {
    LowPC = LowPcAddr->Address;
    HighPC = *HighPcAddr;
    SectionIndex = LowPcAddr->SectionIndex;
    return true;
  }
  return false;
}

Expected<DWARFAddressRangesVector> DWARFDie::getAddressRanges() const {
  if (isNULL())
    return DWARFAddressRangesVector();
  // Single range specified by low/high PC.
  uint64_t LowPC, HighPC, Index;
  if (getLowAndHighPC(LowPC, HighPC, Index))
    return DWARFAddressRangesVector{{LowPC, HighPC, Index}};

  Optional<DWARFFormValue> Value = find(DW_AT_ranges);
  if (Value) {
    if (Value->getForm() == DW_FORM_rnglistx)
      return U->findRnglistFromIndex(*Value->getAsSectionOffset());
    return U->findRnglistFromOffset(*Value->getAsSectionOffset());
  }
  return DWARFAddressRangesVector();
}

void DWARFDie::collectChildrenAddressRanges(
    DWARFAddressRangesVector &Ranges) const {
  if (isNULL())
    return;
  if (isSubprogramDIE()) {
    if (auto DIERangesOrError = getAddressRanges())
      Ranges.insert(Ranges.end(), DIERangesOrError.get().begin(),
                    DIERangesOrError.get().end());
    else
      llvm::consumeError(DIERangesOrError.takeError());
  }

  for (auto Child : children())
    Child.collectChildrenAddressRanges(Ranges);
}

bool DWARFDie::addressRangeContainsAddress(const uint64_t Address) const {
  auto RangesOrError = getAddressRanges();
  if (!RangesOrError) {
    llvm::consumeError(RangesOrError.takeError());
    return false;
  }

  for (const auto &R : RangesOrError.get())
    if (R.LowPC <= Address && Address < R.HighPC)
      return true;
  return false;
}

const char *DWARFDie::getSubroutineName(DINameKind Kind) const {
  if (!isSubroutineDIE())
    return nullptr;
  return getName(Kind);
}

const char *DWARFDie::getName(DINameKind Kind) const {
  if (!isValid() || Kind == DINameKind::None)
    return nullptr;
  // Try to get mangled name only if it was asked for.
  if (Kind == DINameKind::LinkageName) {
    if (auto Name = dwarf::toString(
            findRecursively({DW_AT_MIPS_linkage_name, DW_AT_linkage_name}),
            nullptr))
      return Name;
  }
  if (auto Name = dwarf::toString(findRecursively(DW_AT_name), nullptr))
    return Name;
  return nullptr;
}

uint64_t DWARFDie::getDeclLine() const {
  return toUnsigned(findRecursively(DW_AT_decl_line), 0);
}

void DWARFDie::getCallerFrame(uint32_t &CallFile, uint32_t &CallLine,
                              uint32_t &CallColumn,
                              uint32_t &CallDiscriminator) const {
  CallFile = toUnsigned(find(DW_AT_call_file), 0);
  CallLine = toUnsigned(find(DW_AT_call_line), 0);
  CallColumn = toUnsigned(find(DW_AT_call_column), 0);
  CallDiscriminator = toUnsigned(find(DW_AT_GNU_discriminator), 0);
}

/// Helper to dump a DIE with all of its parents, but no siblings.
static unsigned dumpParentChain(DWARFDie Die, raw_ostream &OS, unsigned Indent,
                                DIDumpOptions DumpOpts) {
  if (!Die)
    return Indent;
  Indent = dumpParentChain(Die.getParent(), OS, Indent, DumpOpts);
  Die.dump(OS, Indent, DumpOpts);
  return Indent + 2;
}

void DWARFDie::dump(raw_ostream &OS, unsigned Indent,
                    DIDumpOptions DumpOpts) const {
  if (!isValid())
    return;
  DWARFDataExtractor debug_info_data = U->getDebugInfoExtractor();
  const uint32_t Offset = getOffset();
  uint32_t offset = Offset;
  if (DumpOpts.ShowParents) {
    DIDumpOptions ParentDumpOpts = DumpOpts;
    ParentDumpOpts.ShowParents = false;
    ParentDumpOpts.ShowChildren = false;
    Indent = dumpParentChain(getParent(), OS, Indent, ParentDumpOpts);
  }

  if (debug_info_data.isValidOffset(offset)) {
    uint32_t abbrCode = debug_info_data.getULEB128(&offset);
    if (DumpOpts.ShowAddresses)
      WithColor(OS, HighlightColor::Address).get()
          << format("\n0x%8.8x: ", Offset);

    if (abbrCode) {
      auto AbbrevDecl = getAbbreviationDeclarationPtr();
      if (AbbrevDecl) {
        WithColor(OS, HighlightColor::Tag).get().indent(Indent)
            << formatv("{0}", getTag());
        if (DumpOpts.Verbose)
          OS << format(" [%u] %c", abbrCode,
                       AbbrevDecl->hasChildren() ? '*' : ' ');
        OS << '\n';

        // Dump all data in the DIE for the attributes.
        for (const auto &AttrSpec : AbbrevDecl->attributes()) {
          if (AttrSpec.Form == DW_FORM_implicit_const) {
            // We are dumping .debug_info section ,
            // implicit_const attribute values are not really stored here,
            // but in .debug_abbrev section. So we just skip such attrs.
            continue;
          }
          dumpAttribute(OS, *this, &offset, AttrSpec.Attr, AttrSpec.Form,
                        Indent, DumpOpts);
        }

        DWARFDie child = getFirstChild();
        if (DumpOpts.ShowChildren && DumpOpts.RecurseDepth > 0 && child) {
          DumpOpts.RecurseDepth--;
          DIDumpOptions ChildDumpOpts = DumpOpts;
          ChildDumpOpts.ShowParents = false;
          while (child) {
            child.dump(OS, Indent + 2, ChildDumpOpts);
            child = child.getSibling();
          }
        }
      } else {
        OS << "Abbreviation code not found in 'debug_abbrev' class for code: "
           << abbrCode << '\n';
      }
    } else {
      OS.indent(Indent) << "NULL\n";
    }
  }
}

LLVM_DUMP_METHOD void DWARFDie::dump() const { dump(llvm::errs(), 0); }

DWARFDie DWARFDie::getParent() const {
  if (isValid())
    return U->getParent(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getSibling() const {
  if (isValid())
    return U->getSibling(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getPreviousSibling() const {
  if (isValid())
    return U->getPreviousSibling(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getFirstChild() const {
  if (isValid())
    return U->getFirstChild(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getLastChild() const {
  if (isValid())
    return U->getLastChild(Die);
  return DWARFDie();
}

iterator_range<DWARFDie::attribute_iterator> DWARFDie::attributes() const {
  return make_range(attribute_iterator(*this, false),
                    attribute_iterator(*this, true));
}

DWARFDie::attribute_iterator::attribute_iterator(DWARFDie D, bool End)
    : Die(D), AttrValue(0), Index(0) {
  auto AbbrDecl = Die.getAbbreviationDeclarationPtr();
  assert(AbbrDecl && "Must have abbreviation declaration");
  if (End) {
    // This is the end iterator so we set the index to the attribute count.
    Index = AbbrDecl->getNumAttributes();
  } else {
    // This is the begin iterator so we extract the value for this->Index.
    AttrValue.Offset = D.getOffset() + AbbrDecl->getCodeByteSize();
    updateForIndex(*AbbrDecl, 0);
  }
}

void DWARFDie::attribute_iterator::updateForIndex(
    const DWARFAbbreviationDeclaration &AbbrDecl, uint32_t I) {
  Index = I;
  // AbbrDecl must be valid before calling this function.
  auto NumAttrs = AbbrDecl.getNumAttributes();
  if (Index < NumAttrs) {
    AttrValue.Attr = AbbrDecl.getAttrByIndex(Index);
    // Add the previous byte size of any previous attribute value.
    AttrValue.Offset += AttrValue.ByteSize;
    AttrValue.Value.setForm(AbbrDecl.getFormByIndex(Index));
    uint32_t ParseOffset = AttrValue.Offset;
    auto U = Die.getDwarfUnit();
    assert(U && "Die must have valid DWARF unit");
    bool b = AttrValue.Value.extractValue(U->getDebugInfoExtractor(),
                                          &ParseOffset, U->getFormParams(), U);
    (void)b;
    assert(b && "extractValue cannot fail on fully parsed DWARF");
    AttrValue.ByteSize = ParseOffset - AttrValue.Offset;
  } else {
    assert(Index == NumAttrs && "Indexes should be [0, NumAttrs) only");
    AttrValue.clear();
  }
}

DWARFDie::attribute_iterator &DWARFDie::attribute_iterator::operator++() {
  if (auto AbbrDecl = Die.getAbbreviationDeclarationPtr())
    updateForIndex(*AbbrDecl, Index + 1);
  return *this;
}
