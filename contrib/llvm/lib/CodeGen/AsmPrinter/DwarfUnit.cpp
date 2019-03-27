//===-- llvm/CodeGen/DwarfUnit.cpp - Dwarf Type and Compile Units ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for constructing a dwarf compile unit.
//
//===----------------------------------------------------------------------===//

#include "DwarfUnit.h"
#include "AddressPool.h"
#include "DwarfCompileUnit.h"
#include "DwarfDebug.h"
#include "DwarfExpression.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Metadata.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

DIEDwarfExpression::DIEDwarfExpression(const AsmPrinter &AP, DwarfUnit &DU,
                                       DIELoc &DIE)
    : DwarfExpression(AP.getDwarfVersion()), AP(AP), DU(DU),
      DIE(DIE) {}

void DIEDwarfExpression::emitOp(uint8_t Op, const char* Comment) {
  DU.addUInt(DIE, dwarf::DW_FORM_data1, Op);
}

void DIEDwarfExpression::emitSigned(int64_t Value) {
  DU.addSInt(DIE, dwarf::DW_FORM_sdata, Value);
}

void DIEDwarfExpression::emitUnsigned(uint64_t Value) {
  DU.addUInt(DIE, dwarf::DW_FORM_udata, Value);
}

bool DIEDwarfExpression::isFrameRegister(const TargetRegisterInfo &TRI,
                                         unsigned MachineReg) {
  return MachineReg == TRI.getFrameRegister(*AP.MF);
}

DwarfUnit::DwarfUnit(dwarf::Tag UnitTag, const DICompileUnit *Node,
                     AsmPrinter *A, DwarfDebug *DW, DwarfFile *DWU)
    : DIEUnit(A->getDwarfVersion(), A->MAI->getCodePointerSize(), UnitTag),
      CUNode(Node), Asm(A), DD(DW), DU(DWU), IndexTyDie(nullptr) {
}

DwarfTypeUnit::DwarfTypeUnit(DwarfCompileUnit &CU, AsmPrinter *A,
                             DwarfDebug *DW, DwarfFile *DWU,
                             MCDwarfDwoLineTable *SplitLineTable)
    : DwarfUnit(dwarf::DW_TAG_type_unit, CU.getCUNode(), A, DW, DWU), CU(CU),
      SplitLineTable(SplitLineTable) {
}

DwarfUnit::~DwarfUnit() {
  for (unsigned j = 0, M = DIEBlocks.size(); j < M; ++j)
    DIEBlocks[j]->~DIEBlock();
  for (unsigned j = 0, M = DIELocs.size(); j < M; ++j)
    DIELocs[j]->~DIELoc();
}

int64_t DwarfUnit::getDefaultLowerBound() const {
  switch (getLanguage()) {
  default:
    break;

  // The languages below have valid values in all DWARF versions.
  case dwarf::DW_LANG_C:
  case dwarf::DW_LANG_C89:
  case dwarf::DW_LANG_C_plus_plus:
    return 0;

  case dwarf::DW_LANG_Fortran77:
  case dwarf::DW_LANG_Fortran90:
    return 1;

  // The languages below have valid values only if the DWARF version >= 3.
  case dwarf::DW_LANG_C99:
  case dwarf::DW_LANG_ObjC:
  case dwarf::DW_LANG_ObjC_plus_plus:
    if (DD->getDwarfVersion() >= 3)
      return 0;
    break;

  case dwarf::DW_LANG_Fortran95:
    if (DD->getDwarfVersion() >= 3)
      return 1;
    break;

  // Starting with DWARF v4, all defined languages have valid values.
  case dwarf::DW_LANG_D:
  case dwarf::DW_LANG_Java:
  case dwarf::DW_LANG_Python:
  case dwarf::DW_LANG_UPC:
    if (DD->getDwarfVersion() >= 4)
      return 0;
    break;

  case dwarf::DW_LANG_Ada83:
  case dwarf::DW_LANG_Ada95:
  case dwarf::DW_LANG_Cobol74:
  case dwarf::DW_LANG_Cobol85:
  case dwarf::DW_LANG_Modula2:
  case dwarf::DW_LANG_Pascal83:
  case dwarf::DW_LANG_PLI:
    if (DD->getDwarfVersion() >= 4)
      return 1;
    break;

  // The languages below are new in DWARF v5.
  case dwarf::DW_LANG_BLISS:
  case dwarf::DW_LANG_C11:
  case dwarf::DW_LANG_C_plus_plus_03:
  case dwarf::DW_LANG_C_plus_plus_11:
  case dwarf::DW_LANG_C_plus_plus_14:
  case dwarf::DW_LANG_Dylan:
  case dwarf::DW_LANG_Go:
  case dwarf::DW_LANG_Haskell:
  case dwarf::DW_LANG_OCaml:
  case dwarf::DW_LANG_OpenCL:
  case dwarf::DW_LANG_RenderScript:
  case dwarf::DW_LANG_Rust:
  case dwarf::DW_LANG_Swift:
    if (DD->getDwarfVersion() >= 5)
      return 0;
    break;

  case dwarf::DW_LANG_Fortran03:
  case dwarf::DW_LANG_Fortran08:
  case dwarf::DW_LANG_Julia:
  case dwarf::DW_LANG_Modula3:
    if (DD->getDwarfVersion() >= 5)
      return 1;
    break;
  }

  return -1;
}

/// Check whether the DIE for this MDNode can be shared across CUs.
bool DwarfUnit::isShareableAcrossCUs(const DINode *D) const {
  // When the MDNode can be part of the type system, the DIE can be shared
  // across CUs.
  // Combining type units and cross-CU DIE sharing is lower value (since
  // cross-CU DIE sharing is used in LTO and removes type redundancy at that
  // level already) but may be implementable for some value in projects
  // building multiple independent libraries with LTO and then linking those
  // together.
  if (isDwoUnit() && !DD->shareAcrossDWOCUs())
    return false;
  return (isa<DIType>(D) ||
          (isa<DISubprogram>(D) && !cast<DISubprogram>(D)->isDefinition())) &&
         !DD->generateTypeUnits();
}

DIE *DwarfUnit::getDIE(const DINode *D) const {
  if (isShareableAcrossCUs(D))
    return DU->getDIE(D);
  return MDNodeToDieMap.lookup(D);
}

void DwarfUnit::insertDIE(const DINode *Desc, DIE *D) {
  if (isShareableAcrossCUs(Desc)) {
    DU->insertDIE(Desc, D);
    return;
  }
  MDNodeToDieMap.insert(std::make_pair(Desc, D));
}

void DwarfUnit::addFlag(DIE &Die, dwarf::Attribute Attribute) {
  if (DD->getDwarfVersion() >= 4)
    Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_flag_present,
                 DIEInteger(1));
  else
    Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_flag,
                 DIEInteger(1));
}

void DwarfUnit::addUInt(DIEValueList &Die, dwarf::Attribute Attribute,
                        Optional<dwarf::Form> Form, uint64_t Integer) {
  if (!Form)
    Form = DIEInteger::BestForm(false, Integer);
  assert(Form != dwarf::DW_FORM_implicit_const &&
         "DW_FORM_implicit_const is used only for signed integers");
  Die.addValue(DIEValueAllocator, Attribute, *Form, DIEInteger(Integer));
}

void DwarfUnit::addUInt(DIEValueList &Block, dwarf::Form Form,
                        uint64_t Integer) {
  addUInt(Block, (dwarf::Attribute)0, Form, Integer);
}

void DwarfUnit::addSInt(DIEValueList &Die, dwarf::Attribute Attribute,
                        Optional<dwarf::Form> Form, int64_t Integer) {
  if (!Form)
    Form = DIEInteger::BestForm(true, Integer);
  Die.addValue(DIEValueAllocator, Attribute, *Form, DIEInteger(Integer));
}

void DwarfUnit::addSInt(DIELoc &Die, Optional<dwarf::Form> Form,
                        int64_t Integer) {
  addSInt(Die, (dwarf::Attribute)0, Form, Integer);
}

void DwarfUnit::addString(DIE &Die, dwarf::Attribute Attribute,
                          StringRef String) {
  if (CUNode->isDebugDirectivesOnly())
    return;

  if (DD->useInlineStrings()) {
    Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_string,
                 new (DIEValueAllocator)
                     DIEInlineString(String, DIEValueAllocator));
    return;
  }
  dwarf::Form IxForm =
      isDwoUnit() ? dwarf::DW_FORM_GNU_str_index : dwarf::DW_FORM_strp;

  auto StringPoolEntry =
      useSegmentedStringOffsetsTable() || IxForm == dwarf::DW_FORM_GNU_str_index
          ? DU->getStringPool().getIndexedEntry(*Asm, String)
          : DU->getStringPool().getEntry(*Asm, String);

  // For DWARF v5 and beyond, use the smallest strx? form possible.
  if (useSegmentedStringOffsetsTable()) {
    IxForm = dwarf::DW_FORM_strx1;
    unsigned Index = StringPoolEntry.getIndex();
    if (Index > 0xffffff)
      IxForm = dwarf::DW_FORM_strx4;
    else if (Index > 0xffff)
      IxForm = dwarf::DW_FORM_strx3;
    else if (Index > 0xff)
      IxForm = dwarf::DW_FORM_strx2;
  }
  Die.addValue(DIEValueAllocator, Attribute, IxForm,
               DIEString(StringPoolEntry));
}

DIEValueList::value_iterator DwarfUnit::addLabel(DIEValueList &Die,
                                                 dwarf::Attribute Attribute,
                                                 dwarf::Form Form,
                                                 const MCSymbol *Label) {
  return Die.addValue(DIEValueAllocator, Attribute, Form, DIELabel(Label));
}

void DwarfUnit::addLabel(DIELoc &Die, dwarf::Form Form, const MCSymbol *Label) {
  addLabel(Die, (dwarf::Attribute)0, Form, Label);
}

void DwarfUnit::addSectionOffset(DIE &Die, dwarf::Attribute Attribute,
                                 uint64_t Integer) {
  if (DD->getDwarfVersion() >= 4)
    addUInt(Die, Attribute, dwarf::DW_FORM_sec_offset, Integer);
  else
    addUInt(Die, Attribute, dwarf::DW_FORM_data4, Integer);
}

MD5::MD5Result *DwarfUnit::getMD5AsBytes(const DIFile *File) const {
  assert(File);
  if (DD->getDwarfVersion() < 5)
    return nullptr;
  Optional<DIFile::ChecksumInfo<StringRef>> Checksum = File->getChecksum();
  if (!Checksum || Checksum->Kind != DIFile::CSK_MD5)
    return nullptr;

  // Convert the string checksum to an MD5Result for the streamer.
  // The verifier validates the checksum so we assume it's okay.
  // An MD5 checksum is 16 bytes.
  std::string ChecksumString = fromHex(Checksum->Value);
  void *CKMem = Asm->OutStreamer->getContext().allocate(16, 1);
  memcpy(CKMem, ChecksumString.data(), 16);
  return reinterpret_cast<MD5::MD5Result *>(CKMem);
}

unsigned DwarfTypeUnit::getOrCreateSourceID(const DIFile *File) {
  if (!SplitLineTable)
    return getCU().getOrCreateSourceID(File);
  if (!UsedLineTable) {
    UsedLineTable = true;
    // This is a split type unit that needs a line table.
    addSectionOffset(getUnitDie(), dwarf::DW_AT_stmt_list, 0);
  }
  return SplitLineTable->getFile(File->getDirectory(), File->getFilename(),
                                 getMD5AsBytes(File), File->getSource());
}

void DwarfUnit::addOpAddress(DIELoc &Die, const MCSymbol *Sym) {
  if (DD->getDwarfVersion() >= 5) {
    addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_addrx);
    addUInt(Die, dwarf::DW_FORM_addrx, DD->getAddressPool().getIndex(Sym));
    return;
  }

  if (DD->useSplitDwarf()) {
    addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_GNU_addr_index);
    addUInt(Die, dwarf::DW_FORM_GNU_addr_index,
            DD->getAddressPool().getIndex(Sym));
    return;
  }

  addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_addr);
  addLabel(Die, dwarf::DW_FORM_udata, Sym);
}

void DwarfUnit::addLabelDelta(DIE &Die, dwarf::Attribute Attribute,
                              const MCSymbol *Hi, const MCSymbol *Lo) {
  Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_data4,
               new (DIEValueAllocator) DIEDelta(Hi, Lo));
}

void DwarfUnit::addDIEEntry(DIE &Die, dwarf::Attribute Attribute, DIE &Entry) {
  addDIEEntry(Die, Attribute, DIEEntry(Entry));
}

void DwarfUnit::addDIETypeSignature(DIE &Die, uint64_t Signature) {
  // Flag the type unit reference as a declaration so that if it contains
  // members (implicit special members, static data member definitions, member
  // declarations for definitions in this CU, etc) consumers don't get confused
  // and think this is a full definition.
  addFlag(Die, dwarf::DW_AT_declaration);

  Die.addValue(DIEValueAllocator, dwarf::DW_AT_signature,
               dwarf::DW_FORM_ref_sig8, DIEInteger(Signature));
}

void DwarfUnit::addDIEEntry(DIE &Die, dwarf::Attribute Attribute,
                            DIEEntry Entry) {
  const DIEUnit *CU = Die.getUnit();
  const DIEUnit *EntryCU = Entry.getEntry().getUnit();
  if (!CU)
    // We assume that Die belongs to this CU, if it is not linked to any CU yet.
    CU = getUnitDie().getUnit();
  if (!EntryCU)
    EntryCU = getUnitDie().getUnit();
  Die.addValue(DIEValueAllocator, Attribute,
               EntryCU == CU ? dwarf::DW_FORM_ref4 : dwarf::DW_FORM_ref_addr,
               Entry);
}

DIE &DwarfUnit::createAndAddDIE(unsigned Tag, DIE &Parent, const DINode *N) {
  DIE &Die = Parent.addChild(DIE::get(DIEValueAllocator, (dwarf::Tag)Tag));
  if (N)
    insertDIE(N, &Die);
  return Die;
}

void DwarfUnit::addBlock(DIE &Die, dwarf::Attribute Attribute, DIELoc *Loc) {
  Loc->ComputeSize(Asm);
  DIELocs.push_back(Loc); // Memoize so we can call the destructor later on.
  Die.addValue(DIEValueAllocator, Attribute,
               Loc->BestForm(DD->getDwarfVersion()), Loc);
}

void DwarfUnit::addBlock(DIE &Die, dwarf::Attribute Attribute,
                         DIEBlock *Block) {
  Block->ComputeSize(Asm);
  DIEBlocks.push_back(Block); // Memoize so we can call the destructor later on.
  Die.addValue(DIEValueAllocator, Attribute, Block->BestForm(), Block);
}

void DwarfUnit::addSourceLine(DIE &Die, unsigned Line, const DIFile *File) {
  if (Line == 0)
    return;

  unsigned FileID = getOrCreateSourceID(File);
  assert(FileID && "Invalid file id");
  addUInt(Die, dwarf::DW_AT_decl_file, None, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, None, Line);
}

void DwarfUnit::addSourceLine(DIE &Die, const DILocalVariable *V) {
  assert(V);

  addSourceLine(Die, V->getLine(), V->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DIGlobalVariable *G) {
  assert(G);

  addSourceLine(Die, G->getLine(), G->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DISubprogram *SP) {
  assert(SP);

  addSourceLine(Die, SP->getLine(), SP->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DILabel *L) {
  assert(L);

  addSourceLine(Die, L->getLine(), L->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DIType *Ty) {
  assert(Ty);

  addSourceLine(Die, Ty->getLine(), Ty->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DIObjCProperty *Ty) {
  assert(Ty);

  addSourceLine(Die, Ty->getLine(), Ty->getFile());
}

/// Return true if type encoding is unsigned.
static bool isUnsignedDIType(DwarfDebug *DD, const DIType *Ty) {
  if (auto *CTy = dyn_cast<DICompositeType>(Ty)) {
    // FIXME: Enums without a fixed underlying type have unknown signedness
    // here, leading to incorrectly emitted constants.
    if (CTy->getTag() == dwarf::DW_TAG_enumeration_type)
      return false;

    // (Pieces of) aggregate types that get hacked apart by SROA may be
    // represented by a constant. Encode them as unsigned bytes.
    return true;
  }

  if (auto *DTy = dyn_cast<DIDerivedType>(Ty)) {
    dwarf::Tag T = (dwarf::Tag)Ty->getTag();
    // Encode pointer constants as unsigned bytes. This is used at least for
    // null pointer constant emission.
    // FIXME: reference and rvalue_reference /probably/ shouldn't be allowed
    // here, but accept them for now due to a bug in SROA producing bogus
    // dbg.values.
    if (T == dwarf::DW_TAG_pointer_type ||
        T == dwarf::DW_TAG_ptr_to_member_type ||
        T == dwarf::DW_TAG_reference_type ||
        T == dwarf::DW_TAG_rvalue_reference_type)
      return true;
    assert(T == dwarf::DW_TAG_typedef || T == dwarf::DW_TAG_const_type ||
           T == dwarf::DW_TAG_volatile_type ||
           T == dwarf::DW_TAG_restrict_type || T == dwarf::DW_TAG_atomic_type);
    DITypeRef Deriv = DTy->getBaseType();
    assert(Deriv && "Expected valid base type");
    return isUnsignedDIType(DD, DD->resolve(Deriv));
  }

  auto *BTy = cast<DIBasicType>(Ty);
  unsigned Encoding = BTy->getEncoding();
  assert((Encoding == dwarf::DW_ATE_unsigned ||
          Encoding == dwarf::DW_ATE_unsigned_char ||
          Encoding == dwarf::DW_ATE_signed ||
          Encoding == dwarf::DW_ATE_signed_char ||
          Encoding == dwarf::DW_ATE_float || Encoding == dwarf::DW_ATE_UTF ||
          Encoding == dwarf::DW_ATE_boolean ||
          (Ty->getTag() == dwarf::DW_TAG_unspecified_type &&
           Ty->getName() == "decltype(nullptr)")) &&
         "Unsupported encoding");
  return Encoding == dwarf::DW_ATE_unsigned ||
         Encoding == dwarf::DW_ATE_unsigned_char ||
         Encoding == dwarf::DW_ATE_UTF || Encoding == dwarf::DW_ATE_boolean ||
         Ty->getTag() == dwarf::DW_TAG_unspecified_type;
}

void DwarfUnit::addConstantFPValue(DIE &Die, const MachineOperand &MO) {
  assert(MO.isFPImm() && "Invalid machine operand!");
  DIEBlock *Block = new (DIEValueAllocator) DIEBlock;
  APFloat FPImm = MO.getFPImm()->getValueAPF();

  // Get the raw data form of the floating point.
  const APInt FltVal = FPImm.bitcastToAPInt();
  const char *FltPtr = (const char *)FltVal.getRawData();

  int NumBytes = FltVal.getBitWidth() / 8; // 8 bits per byte.
  bool LittleEndian = Asm->getDataLayout().isLittleEndian();
  int Incr = (LittleEndian ? 1 : -1);
  int Start = (LittleEndian ? 0 : NumBytes - 1);
  int Stop = (LittleEndian ? NumBytes : -1);

  // Output the constant to DWARF one byte at a time.
  for (; Start != Stop; Start += Incr)
    addUInt(*Block, dwarf::DW_FORM_data1, (unsigned char)0xFF & FltPtr[Start]);

  addBlock(Die, dwarf::DW_AT_const_value, Block);
}

void DwarfUnit::addConstantFPValue(DIE &Die, const ConstantFP *CFP) {
  // Pass this down to addConstantValue as an unsigned bag of bits.
  addConstantValue(Die, CFP->getValueAPF().bitcastToAPInt(), true);
}

void DwarfUnit::addConstantValue(DIE &Die, const ConstantInt *CI,
                                 const DIType *Ty) {
  addConstantValue(Die, CI->getValue(), Ty);
}

void DwarfUnit::addConstantValue(DIE &Die, const MachineOperand &MO,
                                 const DIType *Ty) {
  assert(MO.isImm() && "Invalid machine operand!");

  addConstantValue(Die, isUnsignedDIType(DD, Ty), MO.getImm());
}

void DwarfUnit::addConstantValue(DIE &Die, bool Unsigned, uint64_t Val) {
  // FIXME: This is a bit conservative/simple - it emits negative values always
  // sign extended to 64 bits rather than minimizing the number of bytes.
  addUInt(Die, dwarf::DW_AT_const_value,
          Unsigned ? dwarf::DW_FORM_udata : dwarf::DW_FORM_sdata, Val);
}

void DwarfUnit::addConstantValue(DIE &Die, const APInt &Val, const DIType *Ty) {
  addConstantValue(Die, Val, isUnsignedDIType(DD, Ty));
}

void DwarfUnit::addConstantValue(DIE &Die, const APInt &Val, bool Unsigned) {
  unsigned CIBitWidth = Val.getBitWidth();
  if (CIBitWidth <= 64) {
    addConstantValue(Die, Unsigned,
                     Unsigned ? Val.getZExtValue() : Val.getSExtValue());
    return;
  }

  DIEBlock *Block = new (DIEValueAllocator) DIEBlock;

  // Get the raw data form of the large APInt.
  const uint64_t *Ptr64 = Val.getRawData();

  int NumBytes = Val.getBitWidth() / 8; // 8 bits per byte.
  bool LittleEndian = Asm->getDataLayout().isLittleEndian();

  // Output the constant to DWARF one byte at a time.
  for (int i = 0; i < NumBytes; i++) {
    uint8_t c;
    if (LittleEndian)
      c = Ptr64[i / 8] >> (8 * (i & 7));
    else
      c = Ptr64[(NumBytes - 1 - i) / 8] >> (8 * ((NumBytes - 1 - i) & 7));
    addUInt(*Block, dwarf::DW_FORM_data1, c);
  }

  addBlock(Die, dwarf::DW_AT_const_value, Block);
}

void DwarfUnit::addLinkageName(DIE &Die, StringRef LinkageName) {
  if (!LinkageName.empty())
    addString(Die,
              DD->getDwarfVersion() >= 4 ? dwarf::DW_AT_linkage_name
                                         : dwarf::DW_AT_MIPS_linkage_name,
              GlobalValue::dropLLVMManglingEscape(LinkageName));
}

void DwarfUnit::addTemplateParams(DIE &Buffer, DINodeArray TParams) {
  // Add template parameters.
  for (const auto *Element : TParams) {
    if (auto *TTP = dyn_cast<DITemplateTypeParameter>(Element))
      constructTemplateTypeParameterDIE(Buffer, TTP);
    else if (auto *TVP = dyn_cast<DITemplateValueParameter>(Element))
      constructTemplateValueParameterDIE(Buffer, TVP);
  }
}

/// Add thrown types.
void DwarfUnit::addThrownTypes(DIE &Die, DINodeArray ThrownTypes) {
  for (const auto *Ty : ThrownTypes) {
    DIE &TT = createAndAddDIE(dwarf::DW_TAG_thrown_type, Die);
    addType(TT, cast<DIType>(Ty));
  }
}

DIE *DwarfUnit::getOrCreateContextDIE(const DIScope *Context) {
  if (!Context || isa<DIFile>(Context))
    return &getUnitDie();
  if (auto *T = dyn_cast<DIType>(Context))
    return getOrCreateTypeDIE(T);
  if (auto *NS = dyn_cast<DINamespace>(Context))
    return getOrCreateNameSpace(NS);
  if (auto *SP = dyn_cast<DISubprogram>(Context))
    return getOrCreateSubprogramDIE(SP);
  if (auto *M = dyn_cast<DIModule>(Context))
    return getOrCreateModule(M);
  return getDIE(Context);
}

DIE *DwarfTypeUnit::createTypeDIE(const DICompositeType *Ty) {
  auto *Context = resolve(Ty->getScope());
  DIE *ContextDIE = getOrCreateContextDIE(Context);

  if (DIE *TyDIE = getDIE(Ty))
    return TyDIE;

  // Create new type.
  DIE &TyDIE = createAndAddDIE(Ty->getTag(), *ContextDIE, Ty);

  constructTypeDIE(TyDIE, cast<DICompositeType>(Ty));

  updateAcceleratorTables(Context, Ty, TyDIE);
  return &TyDIE;
}

DIE *DwarfUnit::getOrCreateTypeDIE(const MDNode *TyNode) {
  if (!TyNode)
    return nullptr;

  auto *Ty = cast<DIType>(TyNode);

  // DW_TAG_restrict_type is not supported in DWARF2
  if (Ty->getTag() == dwarf::DW_TAG_restrict_type && DD->getDwarfVersion() <= 2)
    return getOrCreateTypeDIE(resolve(cast<DIDerivedType>(Ty)->getBaseType()));

  // DW_TAG_atomic_type is not supported in DWARF < 5
  if (Ty->getTag() == dwarf::DW_TAG_atomic_type && DD->getDwarfVersion() < 5)
    return getOrCreateTypeDIE(resolve(cast<DIDerivedType>(Ty)->getBaseType()));

  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  auto *Context = resolve(Ty->getScope());
  DIE *ContextDIE = getOrCreateContextDIE(Context);
  assert(ContextDIE);

  if (DIE *TyDIE = getDIE(Ty))
    return TyDIE;

  // Create new type.
  DIE &TyDIE = createAndAddDIE(Ty->getTag(), *ContextDIE, Ty);

  updateAcceleratorTables(Context, Ty, TyDIE);

  if (auto *BT = dyn_cast<DIBasicType>(Ty))
    constructTypeDIE(TyDIE, BT);
  else if (auto *STy = dyn_cast<DISubroutineType>(Ty))
    constructTypeDIE(TyDIE, STy);
  else if (auto *CTy = dyn_cast<DICompositeType>(Ty)) {
    if (DD->generateTypeUnits() && !Ty->isForwardDecl())
      if (MDString *TypeId = CTy->getRawIdentifier()) {
        DD->addDwarfTypeUnitType(getCU(), TypeId->getString(), TyDIE, CTy);
        // Skip updating the accelerator tables since this is not the full type.
        return &TyDIE;
      }
    constructTypeDIE(TyDIE, CTy);
  } else {
    constructTypeDIE(TyDIE, cast<DIDerivedType>(Ty));
  }

  return &TyDIE;
}

void DwarfUnit::updateAcceleratorTables(const DIScope *Context,
                                        const DIType *Ty, const DIE &TyDIE) {
  if (!Ty->getName().empty() && !Ty->isForwardDecl()) {
    bool IsImplementation = false;
    if (auto *CT = dyn_cast<DICompositeType>(Ty)) {
      // A runtime language of 0 actually means C/C++ and that any
      // non-negative value is some version of Objective-C/C++.
      IsImplementation = CT->getRuntimeLang() == 0 || CT->isObjcClassComplete();
    }
    unsigned Flags = IsImplementation ? dwarf::DW_FLAG_type_implementation : 0;
    DD->addAccelType(*CUNode, Ty->getName(), TyDIE, Flags);

    if (!Context || isa<DICompileUnit>(Context) || isa<DIFile>(Context) ||
        isa<DINamespace>(Context))
      addGlobalType(Ty, TyDIE, Context);
  }
}

void DwarfUnit::addType(DIE &Entity, const DIType *Ty,
                        dwarf::Attribute Attribute) {
  assert(Ty && "Trying to add a type that doesn't exist?");
  addDIEEntry(Entity, Attribute, DIEEntry(*getOrCreateTypeDIE(Ty)));
}

std::string DwarfUnit::getParentContextString(const DIScope *Context) const {
  if (!Context)
    return "";

  // FIXME: Decide whether to implement this for non-C++ languages.
  if (getLanguage() != dwarf::DW_LANG_C_plus_plus)
    return "";

  std::string CS;
  SmallVector<const DIScope *, 1> Parents;
  while (!isa<DICompileUnit>(Context)) {
    Parents.push_back(Context);
    if (Context->getScope())
      Context = resolve(Context->getScope());
    else
      // Structure, etc types will have a NULL context if they're at the top
      // level.
      break;
  }

  // Reverse iterate over our list to go from the outermost construct to the
  // innermost.
  for (const DIScope *Ctx : make_range(Parents.rbegin(), Parents.rend())) {
    StringRef Name = Ctx->getName();
    if (Name.empty() && isa<DINamespace>(Ctx))
      Name = "(anonymous namespace)";
    if (!Name.empty()) {
      CS += Name;
      CS += "::";
    }
  }
  return CS;
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DIBasicType *BTy) {
  // Get core information.
  StringRef Name = BTy->getName();
  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  // An unspecified type only has a name attribute.
  if (BTy->getTag() == dwarf::DW_TAG_unspecified_type)
    return;

  addUInt(Buffer, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1,
          BTy->getEncoding());

  uint64_t Size = BTy->getSizeInBits() >> 3;
  addUInt(Buffer, dwarf::DW_AT_byte_size, None, Size);

  if (BTy->isBigEndian())
    addUInt(Buffer, dwarf::DW_AT_endianity, None, dwarf::DW_END_big);
  else if (BTy->isLittleEndian())
    addUInt(Buffer, dwarf::DW_AT_endianity, None, dwarf::DW_END_little);
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DIDerivedType *DTy) {
  // Get core information.
  StringRef Name = DTy->getName();
  uint64_t Size = DTy->getSizeInBits() >> 3;
  uint16_t Tag = Buffer.getTag();

  // Map to main type, void will not have a type.
  const DIType *FromTy = resolve(DTy->getBaseType());
  if (FromTy)
    addType(Buffer, FromTy);

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  // Add size if non-zero (derived types might be zero-sized.)
  if (Size && Tag != dwarf::DW_TAG_pointer_type
           && Tag != dwarf::DW_TAG_ptr_to_member_type
           && Tag != dwarf::DW_TAG_reference_type
           && Tag != dwarf::DW_TAG_rvalue_reference_type)
    addUInt(Buffer, dwarf::DW_AT_byte_size, None, Size);

  if (Tag == dwarf::DW_TAG_ptr_to_member_type)
    addDIEEntry(
        Buffer, dwarf::DW_AT_containing_type,
        *getOrCreateTypeDIE(resolve(cast<DIDerivedType>(DTy)->getClassType())));
  // Add source line info if available and TyDesc is not a forward declaration.
  if (!DTy->isForwardDecl())
    addSourceLine(Buffer, DTy);

  // If DWARF address space value is other than None, add it for pointer and
  // reference types as DW_AT_address_class.
  if (DTy->getDWARFAddressSpace() && (Tag == dwarf::DW_TAG_pointer_type ||
                                      Tag == dwarf::DW_TAG_reference_type))
    addUInt(Buffer, dwarf::DW_AT_address_class, dwarf::DW_FORM_data4,
            DTy->getDWARFAddressSpace().getValue());
}

void DwarfUnit::constructSubprogramArguments(DIE &Buffer, DITypeRefArray Args) {
  for (unsigned i = 1, N = Args.size(); i < N; ++i) {
    const DIType *Ty = resolve(Args[i]);
    if (!Ty) {
      assert(i == N-1 && "Unspecified parameter must be the last argument");
      createAndAddDIE(dwarf::DW_TAG_unspecified_parameters, Buffer);
    } else {
      DIE &Arg = createAndAddDIE(dwarf::DW_TAG_formal_parameter, Buffer);
      addType(Arg, Ty);
      if (Ty->isArtificial())
        addFlag(Arg, dwarf::DW_AT_artificial);
    }
  }
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DISubroutineType *CTy) {
  // Add return type.  A void return won't have a type.
  auto Elements = cast<DISubroutineType>(CTy)->getTypeArray();
  if (Elements.size())
    if (auto RTy = resolve(Elements[0]))
      addType(Buffer, RTy);

  bool isPrototyped = true;
  if (Elements.size() == 2 && !Elements[1])
    isPrototyped = false;

  constructSubprogramArguments(Buffer, Elements);

  // Add prototype flag if we're dealing with a C language and the function has
  // been prototyped.
  uint16_t Language = getLanguage();
  if (isPrototyped &&
      (Language == dwarf::DW_LANG_C89 || Language == dwarf::DW_LANG_C99 ||
       Language == dwarf::DW_LANG_ObjC))
    addFlag(Buffer, dwarf::DW_AT_prototyped);

  // Add a DW_AT_calling_convention if this has an explicit convention.
  if (CTy->getCC() && CTy->getCC() != dwarf::DW_CC_normal)
    addUInt(Buffer, dwarf::DW_AT_calling_convention, dwarf::DW_FORM_data1,
            CTy->getCC());

  if (CTy->isLValueReference())
    addFlag(Buffer, dwarf::DW_AT_reference);

  if (CTy->isRValueReference())
    addFlag(Buffer, dwarf::DW_AT_rvalue_reference);
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DICompositeType *CTy) {
  // Add name if not anonymous or intermediate type.
  StringRef Name = CTy->getName();

  uint64_t Size = CTy->getSizeInBits() >> 3;
  uint16_t Tag = Buffer.getTag();

  switch (Tag) {
  case dwarf::DW_TAG_array_type:
    constructArrayTypeDIE(Buffer, CTy);
    break;
  case dwarf::DW_TAG_enumeration_type:
    constructEnumTypeDIE(Buffer, CTy);
    break;
  case dwarf::DW_TAG_variant_part:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_class_type: {
    // Emit the discriminator for a variant part.
    DIDerivedType *Discriminator = nullptr;
    if (Tag == dwarf::DW_TAG_variant_part) {
      Discriminator = CTy->getDiscriminator();
      if (Discriminator) {
        // DWARF says:
        //    If the variant part has a discriminant, the discriminant is
        //    represented by a separate debugging information entry which is
        //    a child of the variant part entry.
        DIE &DiscMember = constructMemberDIE(Buffer, Discriminator);
        addDIEEntry(Buffer, dwarf::DW_AT_discr, DiscMember);
      }
    }

    // Add elements to structure type.
    DINodeArray Elements = CTy->getElements();
    for (const auto *Element : Elements) {
      if (!Element)
        continue;
      if (auto *SP = dyn_cast<DISubprogram>(Element))
        getOrCreateSubprogramDIE(SP);
      else if (auto *DDTy = dyn_cast<DIDerivedType>(Element)) {
        if (DDTy->getTag() == dwarf::DW_TAG_friend) {
          DIE &ElemDie = createAndAddDIE(dwarf::DW_TAG_friend, Buffer);
          addType(ElemDie, resolve(DDTy->getBaseType()), dwarf::DW_AT_friend);
        } else if (DDTy->isStaticMember()) {
          getOrCreateStaticMemberDIE(DDTy);
        } else if (Tag == dwarf::DW_TAG_variant_part) {
          // When emitting a variant part, wrap each member in
          // DW_TAG_variant.
          DIE &Variant = createAndAddDIE(dwarf::DW_TAG_variant, Buffer);
          if (const ConstantInt *CI =
              dyn_cast_or_null<ConstantInt>(DDTy->getDiscriminantValue())) {
            if (isUnsignedDIType(DD, resolve(Discriminator->getBaseType())))
              addUInt(Variant, dwarf::DW_AT_discr_value, None, CI->getZExtValue());
            else
              addSInt(Variant, dwarf::DW_AT_discr_value, None, CI->getSExtValue());
          }
          constructMemberDIE(Variant, DDTy);
        } else {
          constructMemberDIE(Buffer, DDTy);
        }
      } else if (auto *Property = dyn_cast<DIObjCProperty>(Element)) {
        DIE &ElemDie = createAndAddDIE(Property->getTag(), Buffer);
        StringRef PropertyName = Property->getName();
        addString(ElemDie, dwarf::DW_AT_APPLE_property_name, PropertyName);
        if (Property->getType())
          addType(ElemDie, resolve(Property->getType()));
        addSourceLine(ElemDie, Property);
        StringRef GetterName = Property->getGetterName();
        if (!GetterName.empty())
          addString(ElemDie, dwarf::DW_AT_APPLE_property_getter, GetterName);
        StringRef SetterName = Property->getSetterName();
        if (!SetterName.empty())
          addString(ElemDie, dwarf::DW_AT_APPLE_property_setter, SetterName);
        if (unsigned PropertyAttributes = Property->getAttributes())
          addUInt(ElemDie, dwarf::DW_AT_APPLE_property_attribute, None,
                  PropertyAttributes);
      } else if (auto *Composite = dyn_cast<DICompositeType>(Element)) {
        if (Composite->getTag() == dwarf::DW_TAG_variant_part) {
          DIE &VariantPart = createAndAddDIE(Composite->getTag(), Buffer);
          constructTypeDIE(VariantPart, Composite);
        }
      }
    }

    if (CTy->isAppleBlockExtension())
      addFlag(Buffer, dwarf::DW_AT_APPLE_block);

    // This is outside the DWARF spec, but GDB expects a DW_AT_containing_type
    // inside C++ composite types to point to the base class with the vtable.
    // Rust uses DW_AT_containing_type to link a vtable to the type
    // for which it was created.
    if (auto *ContainingType = resolve(CTy->getVTableHolder()))
      addDIEEntry(Buffer, dwarf::DW_AT_containing_type,
                  *getOrCreateTypeDIE(ContainingType));

    if (CTy->isObjcClassComplete())
      addFlag(Buffer, dwarf::DW_AT_APPLE_objc_complete_type);

    // Add template parameters to a class, structure or union types.
    // FIXME: The support isn't in the metadata for this yet.
    if (Tag == dwarf::DW_TAG_class_type ||
        Tag == dwarf::DW_TAG_structure_type || Tag == dwarf::DW_TAG_union_type)
      addTemplateParams(Buffer, CTy->getTemplateParams());

    // Add the type's non-standard calling convention.
    uint8_t CC = 0;
    if (CTy->isTypePassByValue())
      CC = dwarf::DW_CC_pass_by_value;
    else if (CTy->isTypePassByReference())
      CC = dwarf::DW_CC_pass_by_reference;
    if (CC)
      addUInt(Buffer, dwarf::DW_AT_calling_convention, dwarf::DW_FORM_data1,
              CC);
    break;
  }
  default:
    break;
  }

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  if (Tag == dwarf::DW_TAG_enumeration_type ||
      Tag == dwarf::DW_TAG_class_type || Tag == dwarf::DW_TAG_structure_type ||
      Tag == dwarf::DW_TAG_union_type) {
    // Add size if non-zero (derived types might be zero-sized.)
    // TODO: Do we care about size for enum forward declarations?
    if (Size)
      addUInt(Buffer, dwarf::DW_AT_byte_size, None, Size);
    else if (!CTy->isForwardDecl())
      // Add zero size if it is not a forward declaration.
      addUInt(Buffer, dwarf::DW_AT_byte_size, None, 0);

    // If we're a forward decl, say so.
    if (CTy->isForwardDecl())
      addFlag(Buffer, dwarf::DW_AT_declaration);

    // Add source line info if available.
    if (!CTy->isForwardDecl())
      addSourceLine(Buffer, CTy);

    // No harm in adding the runtime language to the declaration.
    unsigned RLang = CTy->getRuntimeLang();
    if (RLang)
      addUInt(Buffer, dwarf::DW_AT_APPLE_runtime_class, dwarf::DW_FORM_data1,
              RLang);

    // Add align info if available.
    if (uint32_t AlignInBytes = CTy->getAlignInBytes())
      addUInt(Buffer, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
              AlignInBytes);
  }
}

void DwarfUnit::constructTemplateTypeParameterDIE(
    DIE &Buffer, const DITemplateTypeParameter *TP) {
  DIE &ParamDIE =
      createAndAddDIE(dwarf::DW_TAG_template_type_parameter, Buffer);
  // Add the type if it exists, it could be void and therefore no type.
  if (TP->getType())
    addType(ParamDIE, resolve(TP->getType()));
  if (!TP->getName().empty())
    addString(ParamDIE, dwarf::DW_AT_name, TP->getName());
}

void DwarfUnit::constructTemplateValueParameterDIE(
    DIE &Buffer, const DITemplateValueParameter *VP) {
  DIE &ParamDIE = createAndAddDIE(VP->getTag(), Buffer);

  // Add the type if there is one, template template and template parameter
  // packs will not have a type.
  if (VP->getTag() == dwarf::DW_TAG_template_value_parameter)
    addType(ParamDIE, resolve(VP->getType()));
  if (!VP->getName().empty())
    addString(ParamDIE, dwarf::DW_AT_name, VP->getName());
  if (Metadata *Val = VP->getValue()) {
    if (ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(Val))
      addConstantValue(ParamDIE, CI, resolve(VP->getType()));
    else if (GlobalValue *GV = mdconst::dyn_extract<GlobalValue>(Val)) {
      // We cannot describe the location of dllimport'd entities: the
      // computation of their address requires loads from the IAT.
      if (!GV->hasDLLImportStorageClass()) {
        // For declaration non-type template parameters (such as global values
        // and functions)
        DIELoc *Loc = new (DIEValueAllocator) DIELoc;
        addOpAddress(*Loc, Asm->getSymbol(GV));
        // Emit DW_OP_stack_value to use the address as the immediate value of
        // the parameter, rather than a pointer to it.
        addUInt(*Loc, dwarf::DW_FORM_data1, dwarf::DW_OP_stack_value);
        addBlock(ParamDIE, dwarf::DW_AT_location, Loc);
      }
    } else if (VP->getTag() == dwarf::DW_TAG_GNU_template_template_param) {
      assert(isa<MDString>(Val));
      addString(ParamDIE, dwarf::DW_AT_GNU_template_name,
                cast<MDString>(Val)->getString());
    } else if (VP->getTag() == dwarf::DW_TAG_GNU_template_parameter_pack) {
      addTemplateParams(ParamDIE, cast<MDTuple>(Val));
    }
  }
}

DIE *DwarfUnit::getOrCreateNameSpace(const DINamespace *NS) {
  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(NS->getScope());

  if (DIE *NDie = getDIE(NS))
    return NDie;
  DIE &NDie = createAndAddDIE(dwarf::DW_TAG_namespace, *ContextDIE, NS);

  StringRef Name = NS->getName();
  if (!Name.empty())
    addString(NDie, dwarf::DW_AT_name, NS->getName());
  else
    Name = "(anonymous namespace)";
  DD->addAccelNamespace(*CUNode, Name, NDie);
  addGlobalName(Name, NDie, NS->getScope());
  if (NS->getExportSymbols())
    addFlag(NDie, dwarf::DW_AT_export_symbols);
  return &NDie;
}

DIE *DwarfUnit::getOrCreateModule(const DIModule *M) {
  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(M->getScope());

  if (DIE *MDie = getDIE(M))
    return MDie;
  DIE &MDie = createAndAddDIE(dwarf::DW_TAG_module, *ContextDIE, M);

  if (!M->getName().empty()) {
    addString(MDie, dwarf::DW_AT_name, M->getName());
    addGlobalName(M->getName(), MDie, M->getScope());
  }
  if (!M->getConfigurationMacros().empty())
    addString(MDie, dwarf::DW_AT_LLVM_config_macros,
              M->getConfigurationMacros());
  if (!M->getIncludePath().empty())
    addString(MDie, dwarf::DW_AT_LLVM_include_path, M->getIncludePath());
  if (!M->getISysRoot().empty())
    addString(MDie, dwarf::DW_AT_LLVM_isysroot, M->getISysRoot());

  return &MDie;
}

DIE *DwarfUnit::getOrCreateSubprogramDIE(const DISubprogram *SP, bool Minimal) {
  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE (as is the case for member function
  // declarations).
  DIE *ContextDIE =
      Minimal ? &getUnitDie() : getOrCreateContextDIE(resolve(SP->getScope()));

  if (DIE *SPDie = getDIE(SP))
    return SPDie;

  if (auto *SPDecl = SP->getDeclaration()) {
    if (!Minimal) {
      // Add subprogram definitions to the CU die directly.
      ContextDIE = &getUnitDie();
      // Build the decl now to ensure it precedes the definition.
      getOrCreateSubprogramDIE(SPDecl);
    }
  }

  // DW_TAG_inlined_subroutine may refer to this DIE.
  DIE &SPDie = createAndAddDIE(dwarf::DW_TAG_subprogram, *ContextDIE, SP);

  // Stop here and fill this in later, depending on whether or not this
  // subprogram turns out to have inlined instances or not.
  if (SP->isDefinition())
    return &SPDie;

  applySubprogramAttributes(SP, SPDie);
  return &SPDie;
}

bool DwarfUnit::applySubprogramDefinitionAttributes(const DISubprogram *SP,
                                                    DIE &SPDie) {
  DIE *DeclDie = nullptr;
  StringRef DeclLinkageName;
  if (auto *SPDecl = SP->getDeclaration()) {
    DeclDie = getDIE(SPDecl);
    assert(DeclDie && "This DIE should've already been constructed when the "
                      "definition DIE was created in "
                      "getOrCreateSubprogramDIE");
    // Look at the Decl's linkage name only if we emitted it.
    if (DD->useAllLinkageNames())
      DeclLinkageName = SPDecl->getLinkageName();
    unsigned DeclID = getOrCreateSourceID(SPDecl->getFile());
    unsigned DefID = getOrCreateSourceID(SP->getFile());
    if (DeclID != DefID)
      addUInt(SPDie, dwarf::DW_AT_decl_file, None, DefID);

    if (SP->getLine() != SPDecl->getLine())
      addUInt(SPDie, dwarf::DW_AT_decl_line, None, SP->getLine());
  }

  // Add function template parameters.
  addTemplateParams(SPDie, SP->getTemplateParams());

  // Add the linkage name if we have one and it isn't in the Decl.
  StringRef LinkageName = SP->getLinkageName();
  assert(((LinkageName.empty() || DeclLinkageName.empty()) ||
          LinkageName == DeclLinkageName) &&
         "decl has a linkage name and it is different");
  if (DeclLinkageName.empty() &&
      // Always emit it for abstract subprograms.
      (DD->useAllLinkageNames() || DU->getAbstractSPDies().lookup(SP)))
    addLinkageName(SPDie, LinkageName);

  if (!DeclDie)
    return false;

  // Refer to the function declaration where all the other attributes will be
  // found.
  addDIEEntry(SPDie, dwarf::DW_AT_specification, *DeclDie);
  return true;
}

void DwarfUnit::applySubprogramAttributes(const DISubprogram *SP, DIE &SPDie,
                                          bool SkipSPAttributes) {
  // If -fdebug-info-for-profiling is enabled, need to emit the subprogram
  // and its source location.
  bool SkipSPSourceLocation = SkipSPAttributes &&
                              !CUNode->getDebugInfoForProfiling();
  if (!SkipSPSourceLocation)
    if (applySubprogramDefinitionAttributes(SP, SPDie))
      return;

  // Constructors and operators for anonymous aggregates do not have names.
  if (!SP->getName().empty())
    addString(SPDie, dwarf::DW_AT_name, SP->getName());

  if (!SkipSPSourceLocation)
    addSourceLine(SPDie, SP);

  // Skip the rest of the attributes under -gmlt to save space.
  if (SkipSPAttributes)
    return;

  // Add the prototype if we have a prototype and we have a C like
  // language.
  uint16_t Language = getLanguage();
  if (SP->isPrototyped() &&
      (Language == dwarf::DW_LANG_C89 || Language == dwarf::DW_LANG_C99 ||
       Language == dwarf::DW_LANG_ObjC))
    addFlag(SPDie, dwarf::DW_AT_prototyped);

  unsigned CC = 0;
  DITypeRefArray Args;
  if (const DISubroutineType *SPTy = SP->getType()) {
    Args = SPTy->getTypeArray();
    CC = SPTy->getCC();
  }

  // Add a DW_AT_calling_convention if this has an explicit convention.
  if (CC && CC != dwarf::DW_CC_normal)
    addUInt(SPDie, dwarf::DW_AT_calling_convention, dwarf::DW_FORM_data1, CC);

  // Add a return type. If this is a type like a C/C++ void type we don't add a
  // return type.
  if (Args.size())
    if (auto Ty = resolve(Args[0]))
      addType(SPDie, Ty);

  unsigned VK = SP->getVirtuality();
  if (VK) {
    addUInt(SPDie, dwarf::DW_AT_virtuality, dwarf::DW_FORM_data1, VK);
    if (SP->getVirtualIndex() != -1u) {
      DIELoc *Block = getDIELoc();
      addUInt(*Block, dwarf::DW_FORM_data1, dwarf::DW_OP_constu);
      addUInt(*Block, dwarf::DW_FORM_udata, SP->getVirtualIndex());
      addBlock(SPDie, dwarf::DW_AT_vtable_elem_location, Block);
    }
    ContainingTypeMap.insert(
        std::make_pair(&SPDie, resolve(SP->getContainingType())));
  }

  if (!SP->isDefinition()) {
    addFlag(SPDie, dwarf::DW_AT_declaration);

    // Add arguments. Do not add arguments for subprogram definition. They will
    // be handled while processing variables.
    constructSubprogramArguments(SPDie, Args);
  }

  addThrownTypes(SPDie, SP->getThrownTypes());

  if (SP->isArtificial())
    addFlag(SPDie, dwarf::DW_AT_artificial);

  if (!SP->isLocalToUnit())
    addFlag(SPDie, dwarf::DW_AT_external);

  if (DD->useAppleExtensionAttributes()) {
    if (SP->isOptimized())
      addFlag(SPDie, dwarf::DW_AT_APPLE_optimized);

    if (unsigned isa = Asm->getISAEncoding())
      addUInt(SPDie, dwarf::DW_AT_APPLE_isa, dwarf::DW_FORM_flag, isa);
  }

  if (SP->isLValueReference())
    addFlag(SPDie, dwarf::DW_AT_reference);

  if (SP->isRValueReference())
    addFlag(SPDie, dwarf::DW_AT_rvalue_reference);

  if (SP->isNoReturn())
    addFlag(SPDie, dwarf::DW_AT_noreturn);

  if (SP->isProtected())
    addUInt(SPDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_protected);
  else if (SP->isPrivate())
    addUInt(SPDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_private);
  else if (SP->isPublic())
    addUInt(SPDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_public);

  if (SP->isExplicit())
    addFlag(SPDie, dwarf::DW_AT_explicit);

  if (SP->isMainSubprogram())
    addFlag(SPDie, dwarf::DW_AT_main_subprogram);
}

void DwarfUnit::constructSubrangeDIE(DIE &Buffer, const DISubrange *SR,
                                     DIE *IndexTy) {
  DIE &DW_Subrange = createAndAddDIE(dwarf::DW_TAG_subrange_type, Buffer);
  addDIEEntry(DW_Subrange, dwarf::DW_AT_type, *IndexTy);

  // The LowerBound value defines the lower bounds which is typically zero for
  // C/C++. The Count value is the number of elements.  Values are 64 bit. If
  // Count == -1 then the array is unbounded and we do not emit
  // DW_AT_lower_bound and DW_AT_count attributes.
  int64_t LowerBound = SR->getLowerBound();
  int64_t DefaultLowerBound = getDefaultLowerBound();
  int64_t Count = -1;
  if (auto *CI = SR->getCount().dyn_cast<ConstantInt*>())
    Count = CI->getSExtValue();

  if (DefaultLowerBound == -1 || LowerBound != DefaultLowerBound)
    addUInt(DW_Subrange, dwarf::DW_AT_lower_bound, None, LowerBound);

  if (auto *CV = SR->getCount().dyn_cast<DIVariable*>()) {
    if (auto *CountVarDIE = getDIE(CV))
      addDIEEntry(DW_Subrange, dwarf::DW_AT_count, *CountVarDIE);
  } else if (Count != -1)
    addUInt(DW_Subrange, dwarf::DW_AT_count, None, Count);
}

DIE *DwarfUnit::getIndexTyDie() {
  if (IndexTyDie)
    return IndexTyDie;
  // Construct an integer type to use for indexes.
  IndexTyDie = &createAndAddDIE(dwarf::DW_TAG_base_type, getUnitDie());
  StringRef Name = "__ARRAY_SIZE_TYPE__";
  addString(*IndexTyDie, dwarf::DW_AT_name, Name);
  addUInt(*IndexTyDie, dwarf::DW_AT_byte_size, None, sizeof(int64_t));
  addUInt(*IndexTyDie, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1,
          dwarf::DW_ATE_unsigned);
  DD->addAccelType(*CUNode, Name, *IndexTyDie, /*Flags*/ 0);
  return IndexTyDie;
}

/// Returns true if the vector's size differs from the sum of sizes of elements
/// the user specified.  This can occur if the vector has been rounded up to
/// fit memory alignment constraints.
static bool hasVectorBeenPadded(const DICompositeType *CTy) {
  assert(CTy && CTy->isVector() && "Composite type is not a vector");
  const uint64_t ActualSize = CTy->getSizeInBits();

  // Obtain the size of each element in the vector.
  DIType *BaseTy = CTy->getBaseType().resolve();
  assert(BaseTy && "Unknown vector element type.");
  const uint64_t ElementSize = BaseTy->getSizeInBits();

  // Locate the number of elements in the vector.
  const DINodeArray Elements = CTy->getElements();
  assert(Elements.size() == 1 &&
         Elements[0]->getTag() == dwarf::DW_TAG_subrange_type &&
         "Invalid vector element array, expected one element of type subrange");
  const auto Subrange = cast<DISubrange>(Elements[0]);
  const auto CI = Subrange->getCount().get<ConstantInt *>();
  const int32_t NumVecElements = CI->getSExtValue();

  // Ensure we found the element count and that the actual size is wide
  // enough to contain the requested size.
  assert(ActualSize >= (NumVecElements * ElementSize) && "Invalid vector size");
  return ActualSize != (NumVecElements * ElementSize);
}

void DwarfUnit::constructArrayTypeDIE(DIE &Buffer, const DICompositeType *CTy) {
  if (CTy->isVector()) {
    addFlag(Buffer, dwarf::DW_AT_GNU_vector);
    if (hasVectorBeenPadded(CTy))
      addUInt(Buffer, dwarf::DW_AT_byte_size, None,
              CTy->getSizeInBits() / CHAR_BIT);
  }

  // Emit the element type.
  addType(Buffer, resolve(CTy->getBaseType()));

  // Get an anonymous type for index type.
  // FIXME: This type should be passed down from the front end
  // as different languages may have different sizes for indexes.
  DIE *IdxTy = getIndexTyDie();

  // Add subranges to array type.
  DINodeArray Elements = CTy->getElements();
  for (unsigned i = 0, N = Elements.size(); i < N; ++i) {
    // FIXME: Should this really be such a loose cast?
    if (auto *Element = dyn_cast_or_null<DINode>(Elements[i]))
      if (Element->getTag() == dwarf::DW_TAG_subrange_type)
        constructSubrangeDIE(Buffer, cast<DISubrange>(Element), IdxTy);
  }
}

void DwarfUnit::constructEnumTypeDIE(DIE &Buffer, const DICompositeType *CTy) {
  const DIType *DTy = resolve(CTy->getBaseType());
  bool IsUnsigned = DTy && isUnsignedDIType(DD, DTy);
  if (DTy) {
    if (DD->getDwarfVersion() >= 3)
      addType(Buffer, DTy);
    if (DD->getDwarfVersion() >= 4 && (CTy->getFlags() & DINode::FlagEnumClass))
      addFlag(Buffer, dwarf::DW_AT_enum_class);
  }

  DINodeArray Elements = CTy->getElements();

  // Add enumerators to enumeration type.
  for (unsigned i = 0, N = Elements.size(); i < N; ++i) {
    auto *Enum = dyn_cast_or_null<DIEnumerator>(Elements[i]);
    if (Enum) {
      DIE &Enumerator = createAndAddDIE(dwarf::DW_TAG_enumerator, Buffer);
      StringRef Name = Enum->getName();
      addString(Enumerator, dwarf::DW_AT_name, Name);
      auto Value = static_cast<uint64_t>(Enum->getValue());
      addConstantValue(Enumerator, IsUnsigned, Value);
    }
  }
}

void DwarfUnit::constructContainingTypeDIEs() {
  for (auto CI = ContainingTypeMap.begin(), CE = ContainingTypeMap.end();
       CI != CE; ++CI) {
    DIE &SPDie = *CI->first;
    const DINode *D = CI->second;
    if (!D)
      continue;
    DIE *NDie = getDIE(D);
    if (!NDie)
      continue;
    addDIEEntry(SPDie, dwarf::DW_AT_containing_type, *NDie);
  }
}

DIE &DwarfUnit::constructMemberDIE(DIE &Buffer, const DIDerivedType *DT) {
  DIE &MemberDie = createAndAddDIE(DT->getTag(), Buffer);
  StringRef Name = DT->getName();
  if (!Name.empty())
    addString(MemberDie, dwarf::DW_AT_name, Name);

  if (DIType *Resolved = resolve(DT->getBaseType()))
    addType(MemberDie, Resolved);

  addSourceLine(MemberDie, DT);

  if (DT->getTag() == dwarf::DW_TAG_inheritance && DT->isVirtual()) {

    // For C++, virtual base classes are not at fixed offset. Use following
    // expression to extract appropriate offset from vtable.
    // BaseAddr = ObAddr + *((*ObAddr) - Offset)

    DIELoc *VBaseLocationDie = new (DIEValueAllocator) DIELoc;
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_dup);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_constu);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_udata, DT->getOffsetInBits());
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_minus);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_plus);

    addBlock(MemberDie, dwarf::DW_AT_data_member_location, VBaseLocationDie);
  } else {
    uint64_t Size = DT->getSizeInBits();
    uint64_t FieldSize = DD->getBaseTypeSize(DT);
    uint32_t AlignInBytes = DT->getAlignInBytes();
    uint64_t OffsetInBytes;

    bool IsBitfield = FieldSize && Size != FieldSize;
    if (IsBitfield) {
      // Handle bitfield, assume bytes are 8 bits.
      if (DD->useDWARF2Bitfields())
        addUInt(MemberDie, dwarf::DW_AT_byte_size, None, FieldSize/8);
      addUInt(MemberDie, dwarf::DW_AT_bit_size, None, Size);

      uint64_t Offset = DT->getOffsetInBits();
      // We can't use DT->getAlignInBits() here: AlignInBits for member type
      // is non-zero if and only if alignment was forced (e.g. _Alignas()),
      // which can't be done with bitfields. Thus we use FieldSize here.
      uint32_t AlignInBits = FieldSize;
      uint32_t AlignMask = ~(AlignInBits - 1);
      // The bits from the start of the storage unit to the start of the field.
      uint64_t StartBitOffset = Offset - (Offset & AlignMask);
      // The byte offset of the field's aligned storage unit inside the struct.
      OffsetInBytes = (Offset - StartBitOffset) / 8;

      if (DD->useDWARF2Bitfields()) {
        uint64_t HiMark = (Offset + FieldSize) & AlignMask;
        uint64_t FieldOffset = (HiMark - FieldSize);
        Offset -= FieldOffset;

        // Maybe we need to work from the other end.
        if (Asm->getDataLayout().isLittleEndian())
          Offset = FieldSize - (Offset + Size);

        addUInt(MemberDie, dwarf::DW_AT_bit_offset, None, Offset);
        OffsetInBytes = FieldOffset >> 3;
      } else {
        addUInt(MemberDie, dwarf::DW_AT_data_bit_offset, None, Offset);
      }
    } else {
      // This is not a bitfield.
      OffsetInBytes = DT->getOffsetInBits() / 8;
      if (AlignInBytes)
        addUInt(MemberDie, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
                AlignInBytes);
    }

    if (DD->getDwarfVersion() <= 2) {
      DIELoc *MemLocationDie = new (DIEValueAllocator) DIELoc;
      addUInt(*MemLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_plus_uconst);
      addUInt(*MemLocationDie, dwarf::DW_FORM_udata, OffsetInBytes);
      addBlock(MemberDie, dwarf::DW_AT_data_member_location, MemLocationDie);
    } else if (!IsBitfield || DD->useDWARF2Bitfields())
      addUInt(MemberDie, dwarf::DW_AT_data_member_location, None,
              OffsetInBytes);
  }

  if (DT->isProtected())
    addUInt(MemberDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_protected);
  else if (DT->isPrivate())
    addUInt(MemberDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_private);
  // Otherwise C++ member and base classes are considered public.
  else if (DT->isPublic())
    addUInt(MemberDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_public);
  if (DT->isVirtual())
    addUInt(MemberDie, dwarf::DW_AT_virtuality, dwarf::DW_FORM_data1,
            dwarf::DW_VIRTUALITY_virtual);

  // Objective-C properties.
  if (DINode *PNode = DT->getObjCProperty())
    if (DIE *PDie = getDIE(PNode))
      MemberDie.addValue(DIEValueAllocator, dwarf::DW_AT_APPLE_property,
                         dwarf::DW_FORM_ref4, DIEEntry(*PDie));

  if (DT->isArtificial())
    addFlag(MemberDie, dwarf::DW_AT_artificial);

  return MemberDie;
}

DIE *DwarfUnit::getOrCreateStaticMemberDIE(const DIDerivedType *DT) {
  if (!DT)
    return nullptr;

  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(resolve(DT->getScope()));
  assert(dwarf::isType(ContextDIE->getTag()) &&
         "Static member should belong to a type.");

  if (DIE *StaticMemberDIE = getDIE(DT))
    return StaticMemberDIE;

  DIE &StaticMemberDIE = createAndAddDIE(DT->getTag(), *ContextDIE, DT);

  const DIType *Ty = resolve(DT->getBaseType());

  addString(StaticMemberDIE, dwarf::DW_AT_name, DT->getName());
  addType(StaticMemberDIE, Ty);
  addSourceLine(StaticMemberDIE, DT);
  addFlag(StaticMemberDIE, dwarf::DW_AT_external);
  addFlag(StaticMemberDIE, dwarf::DW_AT_declaration);

  // FIXME: We could omit private if the parent is a class_type, and
  // public if the parent is something else.
  if (DT->isProtected())
    addUInt(StaticMemberDIE, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_protected);
  else if (DT->isPrivate())
    addUInt(StaticMemberDIE, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_private);
  else if (DT->isPublic())
    addUInt(StaticMemberDIE, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_public);

  if (const ConstantInt *CI = dyn_cast_or_null<ConstantInt>(DT->getConstant()))
    addConstantValue(StaticMemberDIE, CI, Ty);
  if (const ConstantFP *CFP = dyn_cast_or_null<ConstantFP>(DT->getConstant()))
    addConstantFPValue(StaticMemberDIE, CFP);

  if (uint32_t AlignInBytes = DT->getAlignInBytes())
    addUInt(StaticMemberDIE, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
            AlignInBytes);

  return &StaticMemberDIE;
}

void DwarfUnit::emitCommonHeader(bool UseOffsets, dwarf::UnitType UT) {
  // Emit size of content not including length itself
  Asm->OutStreamer->AddComment("Length of Unit");
  if (!DD->useSectionsAsReferences()) {
    StringRef Prefix = isDwoUnit() ? "debug_info_dwo_" : "debug_info_";
    MCSymbol *BeginLabel = Asm->createTempSymbol(Prefix + "start");
    EndLabel = Asm->createTempSymbol(Prefix + "end");
    Asm->EmitLabelDifference(EndLabel, BeginLabel, 4);
    Asm->OutStreamer->EmitLabel(BeginLabel);
  } else
    Asm->emitInt32(getHeaderSize() + getUnitDie().getSize());

  Asm->OutStreamer->AddComment("DWARF version number");
  unsigned Version = DD->getDwarfVersion();
  Asm->emitInt16(Version);

  // DWARF v5 reorders the address size and adds a unit type.
  if (Version >= 5) {
    Asm->OutStreamer->AddComment("DWARF Unit Type");
    Asm->emitInt8(UT);
    Asm->OutStreamer->AddComment("Address Size (in bytes)");
    Asm->emitInt8(Asm->MAI->getCodePointerSize());
  }

  // We share one abbreviations table across all units so it's always at the
  // start of the section. Use a relocatable offset where needed to ensure
  // linking doesn't invalidate that offset.
  Asm->OutStreamer->AddComment("Offset Into Abbrev. Section");
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  if (UseOffsets)
    Asm->emitInt32(0);
  else
    Asm->emitDwarfSymbolReference(
        TLOF.getDwarfAbbrevSection()->getBeginSymbol(), false);

  if (Version <= 4) {
    Asm->OutStreamer->AddComment("Address Size (in bytes)");
    Asm->emitInt8(Asm->MAI->getCodePointerSize());
  }
}

void DwarfTypeUnit::emitHeader(bool UseOffsets) {
  DwarfUnit::emitCommonHeader(UseOffsets,
                              DD->useSplitDwarf() ? dwarf::DW_UT_split_type
                                                  : dwarf::DW_UT_type);
  Asm->OutStreamer->AddComment("Type Signature");
  Asm->OutStreamer->EmitIntValue(TypeSignature, sizeof(TypeSignature));
  Asm->OutStreamer->AddComment("Type DIE Offset");
  // In a skeleton type unit there is no type DIE so emit a zero offset.
  Asm->OutStreamer->EmitIntValue(Ty ? Ty->getOffset() : 0,
                                 sizeof(Ty->getOffset()));
}

DIE::value_iterator
DwarfUnit::addSectionDelta(DIE &Die, dwarf::Attribute Attribute,
                           const MCSymbol *Hi, const MCSymbol *Lo) {
  return Die.addValue(DIEValueAllocator, Attribute,
                      DD->getDwarfVersion() >= 4 ? dwarf::DW_FORM_sec_offset
                                                 : dwarf::DW_FORM_data4,
                      new (DIEValueAllocator) DIEDelta(Hi, Lo));
}

DIE::value_iterator
DwarfUnit::addSectionLabel(DIE &Die, dwarf::Attribute Attribute,
                           const MCSymbol *Label, const MCSymbol *Sec) {
  if (Asm->MAI->doesDwarfUseRelocationsAcrossSections())
    return addLabel(Die, Attribute,
                    DD->getDwarfVersion() >= 4 ? dwarf::DW_FORM_sec_offset
                                               : dwarf::DW_FORM_data4,
                    Label);
  return addSectionDelta(Die, Attribute, Label, Sec);
}

bool DwarfTypeUnit::isDwoUnit() const {
  // Since there are no skeleton type units, all type units are dwo type units
  // when split DWARF is being used.
  return DD->useSplitDwarf();
}

void DwarfTypeUnit::addGlobalName(StringRef Name, const DIE &Die,
                                  const DIScope *Context) {
  getCU().addGlobalNameForTypeUnit(Name, Context);
}

void DwarfTypeUnit::addGlobalType(const DIType *Ty, const DIE &Die,
                                  const DIScope *Context) {
  getCU().addGlobalTypeUnitType(Ty, Context);
}

const MCSymbol *DwarfUnit::getCrossSectionRelativeBaseAddress() const {
  if (!Asm->MAI->doesDwarfUseRelocationsAcrossSections())
    return nullptr;
  if (isDwoUnit())
    return nullptr;
  return getSection()->getBeginSymbol();
}

void DwarfUnit::addStringOffsetsStart() {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  addSectionLabel(getUnitDie(), dwarf::DW_AT_str_offsets_base,
                  DU->getStringOffsetsStartSym(),
                  TLOF.getDwarfStrOffSection()->getBeginSymbol());
}

void DwarfUnit::addRnglistsBase() {
  assert(DD->getDwarfVersion() >= 5 &&
         "DW_AT_rnglists_base requires DWARF version 5 or later");
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  addSectionLabel(getUnitDie(), dwarf::DW_AT_rnglists_base,
                  DU->getRnglistsTableBaseSym(),
                  TLOF.getDwarfRnglistsSection()->getBeginSymbol());
}

void DwarfUnit::addLoclistsBase() {
  assert(DD->getDwarfVersion() >= 5 &&
         "DW_AT_loclists_base requires DWARF version 5 or later");
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  addSectionLabel(getUnitDie(), dwarf::DW_AT_loclists_base,
                  DU->getLoclistsTableBaseSym(),
                  TLOF.getDwarfLoclistsSection()->getBeginSymbol());
}
