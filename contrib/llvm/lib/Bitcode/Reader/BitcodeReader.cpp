//===- BitcodeReader.cpp - Internal BitcodeReader implementation ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "MetadataLoader.h"
#include "ValueList.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GVMaterializer.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalIndirectSymbol.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<bool> PrintSummaryGUIDs(
    "print-summary-global-ids", cl::init(false), cl::Hidden,
    cl::desc(
        "Print the global id for each value when reading the module summary"));

namespace {

enum {
  SWITCH_INST_MAGIC = 0x4B5 // May 2012 => 1205 => Hex
};

} // end anonymous namespace

static Error error(const Twine &Message) {
  return make_error<StringError>(
      Message, make_error_code(BitcodeError::CorruptedBitcode));
}

/// Helper to read the header common to all bitcode files.
static bool hasValidBitcodeHeader(BitstreamCursor &Stream) {
  // Sniff for the signature.
  if (!Stream.canSkipToPos(4) ||
      Stream.Read(8) != 'B' ||
      Stream.Read(8) != 'C' ||
      Stream.Read(4) != 0x0 ||
      Stream.Read(4) != 0xC ||
      Stream.Read(4) != 0xE ||
      Stream.Read(4) != 0xD)
    return false;
  return true;
}

static Expected<BitstreamCursor> initStream(MemoryBufferRef Buffer) {
  const unsigned char *BufPtr = (const unsigned char *)Buffer.getBufferStart();
  const unsigned char *BufEnd = BufPtr + Buffer.getBufferSize();

  if (Buffer.getBufferSize() & 3)
    return error("Invalid bitcode signature");

  // If we have a wrapper header, parse it and ignore the non-bc file contents.
  // The magic number is 0x0B17C0DE stored in little endian.
  if (isBitcodeWrapper(BufPtr, BufEnd))
    if (SkipBitcodeWrapperHeader(BufPtr, BufEnd, true))
      return error("Invalid bitcode wrapper header");

  BitstreamCursor Stream(ArrayRef<uint8_t>(BufPtr, BufEnd));
  if (!hasValidBitcodeHeader(Stream))
    return error("Invalid bitcode signature");

  return std::move(Stream);
}

/// Convert a string from a record into an std::string, return true on failure.
template <typename StrTy>
static bool convertToString(ArrayRef<uint64_t> Record, unsigned Idx,
                            StrTy &Result) {
  if (Idx > Record.size())
    return true;

  for (unsigned i = Idx, e = Record.size(); i != e; ++i)
    Result += (char)Record[i];
  return false;
}

// Strip all the TBAA attachment for the module.
static void stripTBAA(Module *M) {
  for (auto &F : *M) {
    if (F.isMaterializable())
      continue;
    for (auto &I : instructions(F))
      I.setMetadata(LLVMContext::MD_tbaa, nullptr);
  }
}

/// Read the "IDENTIFICATION_BLOCK_ID" block, do some basic enforcement on the
/// "epoch" encoded in the bitcode, and return the producer name if any.
static Expected<std::string> readIdentificationBlock(BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(bitc::IDENTIFICATION_BLOCK_ID))
    return error("Invalid record");

  // Read all the records.
  SmallVector<uint64_t, 64> Record;

  std::string ProducerIdentification;

  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    default:
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return ProducerIdentification;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: reject
      return error("Invalid value");
    case bitc::IDENTIFICATION_CODE_STRING: // IDENTIFICATION: [strchr x N]
      convertToString(Record, 0, ProducerIdentification);
      break;
    case bitc::IDENTIFICATION_CODE_EPOCH: { // EPOCH: [epoch#]
      unsigned epoch = (unsigned)Record[0];
      if (epoch != bitc::BITCODE_CURRENT_EPOCH) {
        return error(
          Twine("Incompatible epoch: Bitcode '") + Twine(epoch) +
          "' vs current: '" + Twine(bitc::BITCODE_CURRENT_EPOCH) + "'");
      }
    }
    }
  }
}

static Expected<std::string> readIdentificationCode(BitstreamCursor &Stream) {
  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (true) {
    if (Stream.AtEndOfStream())
      return "";

    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    case BitstreamEntry::EndBlock:
    case BitstreamEntry::Error:
      return error("Malformed block");

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::IDENTIFICATION_BLOCK_ID)
        return readIdentificationBlock(Stream);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;
    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

static Expected<bool> hasObjCCategoryInModule(BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  // Read all the records for this module.

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:
      break; // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_SECTIONNAME: { // SECTIONNAME: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      // Check for the i386 and other (x86_64, ARM) conventions
      if (S.find("__DATA,__objc_catlist") != std::string::npos ||
          S.find("__OBJC,__category") != std::string::npos)
        return true;
      break;
    }
    }
    Record.clear();
  }
  llvm_unreachable("Exit infinite loop");
}

static Expected<bool> hasObjCCategory(BitstreamCursor &Stream) {
  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return false;

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::MODULE_BLOCK_ID)
        return hasObjCCategoryInModule(Stream);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

static Expected<std::string> readModuleTriple(BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  std::string Triple;

  // Read all the records for this module.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Triple;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: break;  // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_TRIPLE: {  // TRIPLE: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      Triple = S;
      break;
    }
    }
    Record.clear();
  }
  llvm_unreachable("Exit infinite loop");
}

static Expected<std::string> readTriple(BitstreamCursor &Stream) {
  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return "";

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::MODULE_BLOCK_ID)
        return readModuleTriple(Stream);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

namespace {

class BitcodeReaderBase {
protected:
  BitcodeReaderBase(BitstreamCursor Stream, StringRef Strtab)
      : Stream(std::move(Stream)), Strtab(Strtab) {
    this->Stream.setBlockInfo(&BlockInfo);
  }

  BitstreamBlockInfo BlockInfo;
  BitstreamCursor Stream;
  StringRef Strtab;

  /// In version 2 of the bitcode we store names of global values and comdats in
  /// a string table rather than in the VST.
  bool UseStrtab = false;

  Expected<unsigned> parseVersionRecord(ArrayRef<uint64_t> Record);

  /// If this module uses a string table, pop the reference to the string table
  /// and return the referenced string and the rest of the record. Otherwise
  /// just return the record itself.
  std::pair<StringRef, ArrayRef<uint64_t>>
  readNameFromStrtab(ArrayRef<uint64_t> Record);

  bool readBlockInfo();

  // Contains an arbitrary and optional string identifying the bitcode producer
  std::string ProducerIdentification;

  Error error(const Twine &Message);
};

} // end anonymous namespace

Error BitcodeReaderBase::error(const Twine &Message) {
  std::string FullMsg = Message.str();
  if (!ProducerIdentification.empty())
    FullMsg += " (Producer: '" + ProducerIdentification + "' Reader: 'LLVM " +
               LLVM_VERSION_STRING "')";
  return ::error(FullMsg);
}

Expected<unsigned>
BitcodeReaderBase::parseVersionRecord(ArrayRef<uint64_t> Record) {
  if (Record.empty())
    return error("Invalid record");
  unsigned ModuleVersion = Record[0];
  if (ModuleVersion > 2)
    return error("Invalid value");
  UseStrtab = ModuleVersion >= 2;
  return ModuleVersion;
}

std::pair<StringRef, ArrayRef<uint64_t>>
BitcodeReaderBase::readNameFromStrtab(ArrayRef<uint64_t> Record) {
  if (!UseStrtab)
    return {"", Record};
  // Invalid reference. Let the caller complain about the record being empty.
  if (Record[0] + Record[1] > Strtab.size())
    return {"", {}};
  return {StringRef(Strtab.data() + Record[0], Record[1]), Record.slice(2)};
}

namespace {

class BitcodeReader : public BitcodeReaderBase, public GVMaterializer {
  LLVMContext &Context;
  Module *TheModule = nullptr;
  // Next offset to start scanning for lazy parsing of function bodies.
  uint64_t NextUnreadBit = 0;
  // Last function offset found in the VST.
  uint64_t LastFunctionBlockBit = 0;
  bool SeenValueSymbolTable = false;
  uint64_t VSTOffset = 0;

  std::vector<std::string> SectionTable;
  std::vector<std::string> GCTable;

  std::vector<Type*> TypeList;
  BitcodeReaderValueList ValueList;
  Optional<MetadataLoader> MDLoader;
  std::vector<Comdat *> ComdatList;
  SmallVector<Instruction *, 64> InstructionList;

  std::vector<std::pair<GlobalVariable *, unsigned>> GlobalInits;
  std::vector<std::pair<GlobalIndirectSymbol *, unsigned>> IndirectSymbolInits;
  std::vector<std::pair<Function *, unsigned>> FunctionPrefixes;
  std::vector<std::pair<Function *, unsigned>> FunctionPrologues;
  std::vector<std::pair<Function *, unsigned>> FunctionPersonalityFns;

  /// The set of attributes by index.  Index zero in the file is for null, and
  /// is thus not represented here.  As such all indices are off by one.
  std::vector<AttributeList> MAttributes;

  /// The set of attribute groups.
  std::map<unsigned, AttributeList> MAttributeGroups;

  /// While parsing a function body, this is a list of the basic blocks for the
  /// function.
  std::vector<BasicBlock*> FunctionBBs;

  // When reading the module header, this list is populated with functions that
  // have bodies later in the file.
  std::vector<Function*> FunctionsWithBodies;

  // When intrinsic functions are encountered which require upgrading they are
  // stored here with their replacement function.
  using UpdatedIntrinsicMap = DenseMap<Function *, Function *>;
  UpdatedIntrinsicMap UpgradedIntrinsics;
  // Intrinsics which were remangled because of types rename
  UpdatedIntrinsicMap RemangledIntrinsics;

  // Several operations happen after the module header has been read, but
  // before function bodies are processed. This keeps track of whether
  // we've done this yet.
  bool SeenFirstFunctionBody = false;

  /// When function bodies are initially scanned, this map contains info about
  /// where to find deferred function body in the stream.
  DenseMap<Function*, uint64_t> DeferredFunctionInfo;

  /// When Metadata block is initially scanned when parsing the module, we may
  /// choose to defer parsing of the metadata. This vector contains info about
  /// which Metadata blocks are deferred.
  std::vector<uint64_t> DeferredMetadataInfo;

  /// These are basic blocks forward-referenced by block addresses.  They are
  /// inserted lazily into functions when they're loaded.  The basic block ID is
  /// its index into the vector.
  DenseMap<Function *, std::vector<BasicBlock *>> BasicBlockFwdRefs;
  std::deque<Function *> BasicBlockFwdRefQueue;

  /// Indicates that we are using a new encoding for instruction operands where
  /// most operands in the current FUNCTION_BLOCK are encoded relative to the
  /// instruction number, for a more compact encoding.  Some instruction
  /// operands are not relative to the instruction ID: basic block numbers, and
  /// types. Once the old style function blocks have been phased out, we would
  /// not need this flag.
  bool UseRelativeIDs = false;

  /// True if all functions will be materialized, negating the need to process
  /// (e.g.) blockaddress forward references.
  bool WillMaterializeAllForwardRefs = false;

  bool StripDebugInfo = false;
  TBAAVerifier TBAAVerifyHelper;

  std::vector<std::string> BundleTags;
  SmallVector<SyncScope::ID, 8> SSIDs;

public:
  BitcodeReader(BitstreamCursor Stream, StringRef Strtab,
                StringRef ProducerIdentification, LLVMContext &Context);

  Error materializeForwardReferencedFunctions();

  Error materialize(GlobalValue *GV) override;
  Error materializeModule() override;
  std::vector<StructType *> getIdentifiedStructTypes() const override;

  /// Main interface to parsing a bitcode buffer.
  /// \returns true if an error occurred.
  Error parseBitcodeInto(Module *M, bool ShouldLazyLoadMetadata = false,
                         bool IsImporting = false);

  static uint64_t decodeSignRotatedValue(uint64_t V);

  /// Materialize any deferred Metadata block.
  Error materializeMetadata() override;

  void setStripDebugInfo() override;

private:
  std::vector<StructType *> IdentifiedStructTypes;
  StructType *createIdentifiedStructType(LLVMContext &Context, StringRef Name);
  StructType *createIdentifiedStructType(LLVMContext &Context);

  Type *getTypeByID(unsigned ID);

  Value *getFnValueByID(unsigned ID, Type *Ty) {
    if (Ty && Ty->isMetadataTy())
      return MetadataAsValue::get(Ty->getContext(), getFnMetadataByID(ID));
    return ValueList.getValueFwdRef(ID, Ty);
  }

  Metadata *getFnMetadataByID(unsigned ID) {
    return MDLoader->getMetadataFwdRefOrLoad(ID);
  }

  BasicBlock *getBasicBlock(unsigned ID) const {
    if (ID >= FunctionBBs.size()) return nullptr; // Invalid ID
    return FunctionBBs[ID];
  }

  AttributeList getAttributes(unsigned i) const {
    if (i-1 < MAttributes.size())
      return MAttributes[i-1];
    return AttributeList();
  }

  /// Read a value/type pair out of the specified record from slot 'Slot'.
  /// Increment Slot past the number of slots used in the record. Return true on
  /// failure.
  bool getValueTypePair(SmallVectorImpl<uint64_t> &Record, unsigned &Slot,
                        unsigned InstNum, Value *&ResVal) {
    if (Slot == Record.size()) return true;
    unsigned ValNo = (unsigned)Record[Slot++];
    // Adjust the ValNo, if it was encoded relative to the InstNum.
    if (UseRelativeIDs)
      ValNo = InstNum - ValNo;
    if (ValNo < InstNum) {
      // If this is not a forward reference, just return the value we already
      // have.
      ResVal = getFnValueByID(ValNo, nullptr);
      return ResVal == nullptr;
    }
    if (Slot == Record.size())
      return true;

    unsigned TypeNo = (unsigned)Record[Slot++];
    ResVal = getFnValueByID(ValNo, getTypeByID(TypeNo));
    return ResVal == nullptr;
  }

  /// Read a value out of the specified record from slot 'Slot'. Increment Slot
  /// past the number of slots used by the value in the record. Return true if
  /// there is an error.
  bool popValue(SmallVectorImpl<uint64_t> &Record, unsigned &Slot,
                unsigned InstNum, Type *Ty, Value *&ResVal) {
    if (getValue(Record, Slot, InstNum, Ty, ResVal))
      return true;
    // All values currently take a single record slot.
    ++Slot;
    return false;
  }

  /// Like popValue, but does not increment the Slot number.
  bool getValue(SmallVectorImpl<uint64_t> &Record, unsigned Slot,
                unsigned InstNum, Type *Ty, Value *&ResVal) {
    ResVal = getValue(Record, Slot, InstNum, Ty);
    return ResVal == nullptr;
  }

  /// Version of getValue that returns ResVal directly, or 0 if there is an
  /// error.
  Value *getValue(SmallVectorImpl<uint64_t> &Record, unsigned Slot,
                  unsigned InstNum, Type *Ty) {
    if (Slot == Record.size()) return nullptr;
    unsigned ValNo = (unsigned)Record[Slot];
    // Adjust the ValNo, if it was encoded relative to the InstNum.
    if (UseRelativeIDs)
      ValNo = InstNum - ValNo;
    return getFnValueByID(ValNo, Ty);
  }

  /// Like getValue, but decodes signed VBRs.
  Value *getValueSigned(SmallVectorImpl<uint64_t> &Record, unsigned Slot,
                        unsigned InstNum, Type *Ty) {
    if (Slot == Record.size()) return nullptr;
    unsigned ValNo = (unsigned)decodeSignRotatedValue(Record[Slot]);
    // Adjust the ValNo, if it was encoded relative to the InstNum.
    if (UseRelativeIDs)
      ValNo = InstNum - ValNo;
    return getFnValueByID(ValNo, Ty);
  }

  /// Converts alignment exponent (i.e. power of two (or zero)) to the
  /// corresponding alignment to use. If alignment is too large, returns
  /// a corresponding error code.
  Error parseAlignmentValue(uint64_t Exponent, unsigned &Alignment);
  Error parseAttrKind(uint64_t Code, Attribute::AttrKind *Kind);
  Error parseModule(uint64_t ResumeBit, bool ShouldLazyLoadMetadata = false);

  Error parseComdatRecord(ArrayRef<uint64_t> Record);
  Error parseGlobalVarRecord(ArrayRef<uint64_t> Record);
  Error parseFunctionRecord(ArrayRef<uint64_t> Record);
  Error parseGlobalIndirectSymbolRecord(unsigned BitCode,
                                        ArrayRef<uint64_t> Record);

  Error parseAttributeBlock();
  Error parseAttributeGroupBlock();
  Error parseTypeTable();
  Error parseTypeTableBody();
  Error parseOperandBundleTags();
  Error parseSyncScopeNames();

  Expected<Value *> recordValue(SmallVectorImpl<uint64_t> &Record,
                                unsigned NameIndex, Triple &TT);
  void setDeferredFunctionInfo(unsigned FuncBitcodeOffsetDelta, Function *F,
                               ArrayRef<uint64_t> Record);
  Error parseValueSymbolTable(uint64_t Offset = 0);
  Error parseGlobalValueSymbolTable();
  Error parseConstants();
  Error rememberAndSkipFunctionBodies();
  Error rememberAndSkipFunctionBody();
  /// Save the positions of the Metadata blocks and skip parsing the blocks.
  Error rememberAndSkipMetadata();
  Error typeCheckLoadStoreInst(Type *ValType, Type *PtrType);
  Error parseFunctionBody(Function *F);
  Error globalCleanup();
  Error resolveGlobalAndIndirectSymbolInits();
  Error parseUseLists();
  Error findFunctionInStream(
      Function *F,
      DenseMap<Function *, uint64_t>::iterator DeferredFunctionInfoIterator);

  SyncScope::ID getDecodedSyncScopeID(unsigned Val);
};

/// Class to manage reading and parsing function summary index bitcode
/// files/sections.
class ModuleSummaryIndexBitcodeReader : public BitcodeReaderBase {
  /// The module index built during parsing.
  ModuleSummaryIndex &TheIndex;

  /// Indicates whether we have encountered a global value summary section
  /// yet during parsing.
  bool SeenGlobalValSummary = false;

  /// Indicates whether we have already parsed the VST, used for error checking.
  bool SeenValueSymbolTable = false;

  /// Set to the offset of the VST recorded in the MODULE_CODE_VSTOFFSET record.
  /// Used to enable on-demand parsing of the VST.
  uint64_t VSTOffset = 0;

  // Map to save ValueId to ValueInfo association that was recorded in the
  // ValueSymbolTable. It is used after the VST is parsed to convert
  // call graph edges read from the function summary from referencing
  // callees by their ValueId to using the ValueInfo instead, which is how
  // they are recorded in the summary index being built.
  // We save a GUID which refers to the same global as the ValueInfo, but
  // ignoring the linkage, i.e. for values other than local linkage they are
  // identical.
  DenseMap<unsigned, std::pair<ValueInfo, GlobalValue::GUID>>
      ValueIdToValueInfoMap;

  /// Map populated during module path string table parsing, from the
  /// module ID to a string reference owned by the index's module
  /// path string table, used to correlate with combined index
  /// summary records.
  DenseMap<uint64_t, StringRef> ModuleIdMap;

  /// Original source file name recorded in a bitcode record.
  std::string SourceFileName;

  /// The string identifier given to this module by the client, normally the
  /// path to the bitcode file.
  StringRef ModulePath;

  /// For per-module summary indexes, the unique numerical identifier given to
  /// this module by the client.
  unsigned ModuleId;

public:
  ModuleSummaryIndexBitcodeReader(BitstreamCursor Stream, StringRef Strtab,
                                  ModuleSummaryIndex &TheIndex,
                                  StringRef ModulePath, unsigned ModuleId);

  Error parseModule();

private:
  void setValueGUID(uint64_t ValueID, StringRef ValueName,
                    GlobalValue::LinkageTypes Linkage,
                    StringRef SourceFileName);
  Error parseValueSymbolTable(
      uint64_t Offset,
      DenseMap<unsigned, GlobalValue::LinkageTypes> &ValueIdToLinkageMap);
  std::vector<ValueInfo> makeRefList(ArrayRef<uint64_t> Record);
  std::vector<FunctionSummary::EdgeTy> makeCallList(ArrayRef<uint64_t> Record,
                                                    bool IsOldProfileFormat,
                                                    bool HasProfile,
                                                    bool HasRelBF);
  Error parseEntireSummary(unsigned ID);
  Error parseModuleStringTable();

  std::pair<ValueInfo, GlobalValue::GUID>
  getValueInfoFromValueId(unsigned ValueId);

  void addThisModule();
  ModuleSummaryIndex::ModuleInfo *getThisModule();
};

} // end anonymous namespace

std::error_code llvm::errorToErrorCodeAndEmitErrors(LLVMContext &Ctx,
                                                    Error Err) {
  if (Err) {
    std::error_code EC;
    handleAllErrors(std::move(Err), [&](ErrorInfoBase &EIB) {
      EC = EIB.convertToErrorCode();
      Ctx.emitError(EIB.message());
    });
    return EC;
  }
  return std::error_code();
}

BitcodeReader::BitcodeReader(BitstreamCursor Stream, StringRef Strtab,
                             StringRef ProducerIdentification,
                             LLVMContext &Context)
    : BitcodeReaderBase(std::move(Stream), Strtab), Context(Context),
      ValueList(Context) {
  this->ProducerIdentification = ProducerIdentification;
}

Error BitcodeReader::materializeForwardReferencedFunctions() {
  if (WillMaterializeAllForwardRefs)
    return Error::success();

  // Prevent recursion.
  WillMaterializeAllForwardRefs = true;

  while (!BasicBlockFwdRefQueue.empty()) {
    Function *F = BasicBlockFwdRefQueue.front();
    BasicBlockFwdRefQueue.pop_front();
    assert(F && "Expected valid function");
    if (!BasicBlockFwdRefs.count(F))
      // Already materialized.
      continue;

    // Check for a function that isn't materializable to prevent an infinite
    // loop.  When parsing a blockaddress stored in a global variable, there
    // isn't a trivial way to check if a function will have a body without a
    // linear search through FunctionsWithBodies, so just check it here.
    if (!F->isMaterializable())
      return error("Never resolved function from blockaddress");

    // Try to materialize F.
    if (Error Err = materialize(F))
      return Err;
  }
  assert(BasicBlockFwdRefs.empty() && "Function missing from queue");

  // Reset state.
  WillMaterializeAllForwardRefs = false;
  return Error::success();
}

//===----------------------------------------------------------------------===//
//  Helper functions to implement forward reference resolution, etc.
//===----------------------------------------------------------------------===//

static bool hasImplicitComdat(size_t Val) {
  switch (Val) {
  default:
    return false;
  case 1:  // Old WeakAnyLinkage
  case 4:  // Old LinkOnceAnyLinkage
  case 10: // Old WeakODRLinkage
  case 11: // Old LinkOnceODRLinkage
    return true;
  }
}

static GlobalValue::LinkageTypes getDecodedLinkage(unsigned Val) {
  switch (Val) {
  default: // Map unknown/new linkages to external
  case 0:
    return GlobalValue::ExternalLinkage;
  case 2:
    return GlobalValue::AppendingLinkage;
  case 3:
    return GlobalValue::InternalLinkage;
  case 5:
    return GlobalValue::ExternalLinkage; // Obsolete DLLImportLinkage
  case 6:
    return GlobalValue::ExternalLinkage; // Obsolete DLLExportLinkage
  case 7:
    return GlobalValue::ExternalWeakLinkage;
  case 8:
    return GlobalValue::CommonLinkage;
  case 9:
    return GlobalValue::PrivateLinkage;
  case 12:
    return GlobalValue::AvailableExternallyLinkage;
  case 13:
    return GlobalValue::PrivateLinkage; // Obsolete LinkerPrivateLinkage
  case 14:
    return GlobalValue::PrivateLinkage; // Obsolete LinkerPrivateWeakLinkage
  case 15:
    return GlobalValue::ExternalLinkage; // Obsolete LinkOnceODRAutoHideLinkage
  case 1: // Old value with implicit comdat.
  case 16:
    return GlobalValue::WeakAnyLinkage;
  case 10: // Old value with implicit comdat.
  case 17:
    return GlobalValue::WeakODRLinkage;
  case 4: // Old value with implicit comdat.
  case 18:
    return GlobalValue::LinkOnceAnyLinkage;
  case 11: // Old value with implicit comdat.
  case 19:
    return GlobalValue::LinkOnceODRLinkage;
  }
}

static FunctionSummary::FFlags getDecodedFFlags(uint64_t RawFlags) {
  FunctionSummary::FFlags Flags;
  Flags.ReadNone = RawFlags & 0x1;
  Flags.ReadOnly = (RawFlags >> 1) & 0x1;
  Flags.NoRecurse = (RawFlags >> 2) & 0x1;
  Flags.ReturnDoesNotAlias = (RawFlags >> 3) & 0x1;
  Flags.NoInline = (RawFlags >> 4) & 0x1;
  return Flags;
}

/// Decode the flags for GlobalValue in the summary.
static GlobalValueSummary::GVFlags getDecodedGVSummaryFlags(uint64_t RawFlags,
                                                            uint64_t Version) {
  // Summary were not emitted before LLVM 3.9, we don't need to upgrade Linkage
  // like getDecodedLinkage() above. Any future change to the linkage enum and
  // to getDecodedLinkage() will need to be taken into account here as above.
  auto Linkage = GlobalValue::LinkageTypes(RawFlags & 0xF); // 4 bits
  RawFlags = RawFlags >> 4;
  bool NotEligibleToImport = (RawFlags & 0x1) || Version < 3;
  // The Live flag wasn't introduced until version 3. For dead stripping
  // to work correctly on earlier versions, we must conservatively treat all
  // values as live.
  bool Live = (RawFlags & 0x2) || Version < 3;
  bool Local = (RawFlags & 0x4);

  return GlobalValueSummary::GVFlags(Linkage, NotEligibleToImport, Live, Local);
}

// Decode the flags for GlobalVariable in the summary
static GlobalVarSummary::GVarFlags getDecodedGVarFlags(uint64_t RawFlags) {
  return GlobalVarSummary::GVarFlags((RawFlags & 0x1) ? true : false);
}

static GlobalValue::VisibilityTypes getDecodedVisibility(unsigned Val) {
  switch (Val) {
  default: // Map unknown visibilities to default.
  case 0: return GlobalValue::DefaultVisibility;
  case 1: return GlobalValue::HiddenVisibility;
  case 2: return GlobalValue::ProtectedVisibility;
  }
}

static GlobalValue::DLLStorageClassTypes
getDecodedDLLStorageClass(unsigned Val) {
  switch (Val) {
  default: // Map unknown values to default.
  case 0: return GlobalValue::DefaultStorageClass;
  case 1: return GlobalValue::DLLImportStorageClass;
  case 2: return GlobalValue::DLLExportStorageClass;
  }
}

static bool getDecodedDSOLocal(unsigned Val) {
  switch(Val) {
  default: // Map unknown values to preemptable.
  case 0:  return false;
  case 1:  return true;
  }
}

static GlobalVariable::ThreadLocalMode getDecodedThreadLocalMode(unsigned Val) {
  switch (Val) {
    case 0: return GlobalVariable::NotThreadLocal;
    default: // Map unknown non-zero value to general dynamic.
    case 1: return GlobalVariable::GeneralDynamicTLSModel;
    case 2: return GlobalVariable::LocalDynamicTLSModel;
    case 3: return GlobalVariable::InitialExecTLSModel;
    case 4: return GlobalVariable::LocalExecTLSModel;
  }
}

static GlobalVariable::UnnamedAddr getDecodedUnnamedAddrType(unsigned Val) {
  switch (Val) {
    default: // Map unknown to UnnamedAddr::None.
    case 0: return GlobalVariable::UnnamedAddr::None;
    case 1: return GlobalVariable::UnnamedAddr::Global;
    case 2: return GlobalVariable::UnnamedAddr::Local;
  }
}

static int getDecodedCastOpcode(unsigned Val) {
  switch (Val) {
  default: return -1;
  case bitc::CAST_TRUNC   : return Instruction::Trunc;
  case bitc::CAST_ZEXT    : return Instruction::ZExt;
  case bitc::CAST_SEXT    : return Instruction::SExt;
  case bitc::CAST_FPTOUI  : return Instruction::FPToUI;
  case bitc::CAST_FPTOSI  : return Instruction::FPToSI;
  case bitc::CAST_UITOFP  : return Instruction::UIToFP;
  case bitc::CAST_SITOFP  : return Instruction::SIToFP;
  case bitc::CAST_FPTRUNC : return Instruction::FPTrunc;
  case bitc::CAST_FPEXT   : return Instruction::FPExt;
  case bitc::CAST_PTRTOINT: return Instruction::PtrToInt;
  case bitc::CAST_INTTOPTR: return Instruction::IntToPtr;
  case bitc::CAST_BITCAST : return Instruction::BitCast;
  case bitc::CAST_ADDRSPACECAST: return Instruction::AddrSpaceCast;
  }
}

static int getDecodedUnaryOpcode(unsigned Val, Type *Ty) {
  bool IsFP = Ty->isFPOrFPVectorTy();
  // UnOps are only valid for int/fp or vector of int/fp types
  if (!IsFP && !Ty->isIntOrIntVectorTy())
    return -1;

  switch (Val) {
  default:
    return -1;
  case bitc::UNOP_NEG:
    return IsFP ? Instruction::FNeg : -1;
  }
}

static int getDecodedBinaryOpcode(unsigned Val, Type *Ty) {
  bool IsFP = Ty->isFPOrFPVectorTy();
  // BinOps are only valid for int/fp or vector of int/fp types
  if (!IsFP && !Ty->isIntOrIntVectorTy())
    return -1;

  switch (Val) {
  default:
    return -1;
  case bitc::BINOP_ADD:
    return IsFP ? Instruction::FAdd : Instruction::Add;
  case bitc::BINOP_SUB:
    return IsFP ? Instruction::FSub : Instruction::Sub;
  case bitc::BINOP_MUL:
    return IsFP ? Instruction::FMul : Instruction::Mul;
  case bitc::BINOP_UDIV:
    return IsFP ? -1 : Instruction::UDiv;
  case bitc::BINOP_SDIV:
    return IsFP ? Instruction::FDiv : Instruction::SDiv;
  case bitc::BINOP_UREM:
    return IsFP ? -1 : Instruction::URem;
  case bitc::BINOP_SREM:
    return IsFP ? Instruction::FRem : Instruction::SRem;
  case bitc::BINOP_SHL:
    return IsFP ? -1 : Instruction::Shl;
  case bitc::BINOP_LSHR:
    return IsFP ? -1 : Instruction::LShr;
  case bitc::BINOP_ASHR:
    return IsFP ? -1 : Instruction::AShr;
  case bitc::BINOP_AND:
    return IsFP ? -1 : Instruction::And;
  case bitc::BINOP_OR:
    return IsFP ? -1 : Instruction::Or;
  case bitc::BINOP_XOR:
    return IsFP ? -1 : Instruction::Xor;
  }
}

static AtomicRMWInst::BinOp getDecodedRMWOperation(unsigned Val) {
  switch (Val) {
  default: return AtomicRMWInst::BAD_BINOP;
  case bitc::RMW_XCHG: return AtomicRMWInst::Xchg;
  case bitc::RMW_ADD: return AtomicRMWInst::Add;
  case bitc::RMW_SUB: return AtomicRMWInst::Sub;
  case bitc::RMW_AND: return AtomicRMWInst::And;
  case bitc::RMW_NAND: return AtomicRMWInst::Nand;
  case bitc::RMW_OR: return AtomicRMWInst::Or;
  case bitc::RMW_XOR: return AtomicRMWInst::Xor;
  case bitc::RMW_MAX: return AtomicRMWInst::Max;
  case bitc::RMW_MIN: return AtomicRMWInst::Min;
  case bitc::RMW_UMAX: return AtomicRMWInst::UMax;
  case bitc::RMW_UMIN: return AtomicRMWInst::UMin;
  }
}

static AtomicOrdering getDecodedOrdering(unsigned Val) {
  switch (Val) {
  case bitc::ORDERING_NOTATOMIC: return AtomicOrdering::NotAtomic;
  case bitc::ORDERING_UNORDERED: return AtomicOrdering::Unordered;
  case bitc::ORDERING_MONOTONIC: return AtomicOrdering::Monotonic;
  case bitc::ORDERING_ACQUIRE: return AtomicOrdering::Acquire;
  case bitc::ORDERING_RELEASE: return AtomicOrdering::Release;
  case bitc::ORDERING_ACQREL: return AtomicOrdering::AcquireRelease;
  default: // Map unknown orderings to sequentially-consistent.
  case bitc::ORDERING_SEQCST: return AtomicOrdering::SequentiallyConsistent;
  }
}

static Comdat::SelectionKind getDecodedComdatSelectionKind(unsigned Val) {
  switch (Val) {
  default: // Map unknown selection kinds to any.
  case bitc::COMDAT_SELECTION_KIND_ANY:
    return Comdat::Any;
  case bitc::COMDAT_SELECTION_KIND_EXACT_MATCH:
    return Comdat::ExactMatch;
  case bitc::COMDAT_SELECTION_KIND_LARGEST:
    return Comdat::Largest;
  case bitc::COMDAT_SELECTION_KIND_NO_DUPLICATES:
    return Comdat::NoDuplicates;
  case bitc::COMDAT_SELECTION_KIND_SAME_SIZE:
    return Comdat::SameSize;
  }
}

static FastMathFlags getDecodedFastMathFlags(unsigned Val) {
  FastMathFlags FMF;
  if (0 != (Val & bitc::UnsafeAlgebra))
    FMF.setFast();
  if (0 != (Val & bitc::AllowReassoc))
    FMF.setAllowReassoc();
  if (0 != (Val & bitc::NoNaNs))
    FMF.setNoNaNs();
  if (0 != (Val & bitc::NoInfs))
    FMF.setNoInfs();
  if (0 != (Val & bitc::NoSignedZeros))
    FMF.setNoSignedZeros();
  if (0 != (Val & bitc::AllowReciprocal))
    FMF.setAllowReciprocal();
  if (0 != (Val & bitc::AllowContract))
    FMF.setAllowContract(true);
  if (0 != (Val & bitc::ApproxFunc))
    FMF.setApproxFunc();
  return FMF;
}

static void upgradeDLLImportExportLinkage(GlobalValue *GV, unsigned Val) {
  switch (Val) {
  case 5: GV->setDLLStorageClass(GlobalValue::DLLImportStorageClass); break;
  case 6: GV->setDLLStorageClass(GlobalValue::DLLExportStorageClass); break;
  }
}

Type *BitcodeReader::getTypeByID(unsigned ID) {
  // The type table size is always specified correctly.
  if (ID >= TypeList.size())
    return nullptr;

  if (Type *Ty = TypeList[ID])
    return Ty;

  // If we have a forward reference, the only possible case is when it is to a
  // named struct.  Just create a placeholder for now.
  return TypeList[ID] = createIdentifiedStructType(Context);
}

StructType *BitcodeReader::createIdentifiedStructType(LLVMContext &Context,
                                                      StringRef Name) {
  auto *Ret = StructType::create(Context, Name);
  IdentifiedStructTypes.push_back(Ret);
  return Ret;
}

StructType *BitcodeReader::createIdentifiedStructType(LLVMContext &Context) {
  auto *Ret = StructType::create(Context);
  IdentifiedStructTypes.push_back(Ret);
  return Ret;
}

//===----------------------------------------------------------------------===//
//  Functions for parsing blocks from the bitcode file
//===----------------------------------------------------------------------===//

static uint64_t getRawAttributeMask(Attribute::AttrKind Val) {
  switch (Val) {
  case Attribute::EndAttrKinds:
    llvm_unreachable("Synthetic enumerators which should never get here");

  case Attribute::None:            return 0;
  case Attribute::ZExt:            return 1 << 0;
  case Attribute::SExt:            return 1 << 1;
  case Attribute::NoReturn:        return 1 << 2;
  case Attribute::InReg:           return 1 << 3;
  case Attribute::StructRet:       return 1 << 4;
  case Attribute::NoUnwind:        return 1 << 5;
  case Attribute::NoAlias:         return 1 << 6;
  case Attribute::ByVal:           return 1 << 7;
  case Attribute::Nest:            return 1 << 8;
  case Attribute::ReadNone:        return 1 << 9;
  case Attribute::ReadOnly:        return 1 << 10;
  case Attribute::NoInline:        return 1 << 11;
  case Attribute::AlwaysInline:    return 1 << 12;
  case Attribute::OptimizeForSize: return 1 << 13;
  case Attribute::StackProtect:    return 1 << 14;
  case Attribute::StackProtectReq: return 1 << 15;
  case Attribute::Alignment:       return 31 << 16;
  case Attribute::NoCapture:       return 1 << 21;
  case Attribute::NoRedZone:       return 1 << 22;
  case Attribute::NoImplicitFloat: return 1 << 23;
  case Attribute::Naked:           return 1 << 24;
  case Attribute::InlineHint:      return 1 << 25;
  case Attribute::StackAlignment:  return 7 << 26;
  case Attribute::ReturnsTwice:    return 1 << 29;
  case Attribute::UWTable:         return 1 << 30;
  case Attribute::NonLazyBind:     return 1U << 31;
  case Attribute::SanitizeAddress: return 1ULL << 32;
  case Attribute::MinSize:         return 1ULL << 33;
  case Attribute::NoDuplicate:     return 1ULL << 34;
  case Attribute::StackProtectStrong: return 1ULL << 35;
  case Attribute::SanitizeThread:  return 1ULL << 36;
  case Attribute::SanitizeMemory:  return 1ULL << 37;
  case Attribute::NoBuiltin:       return 1ULL << 38;
  case Attribute::Returned:        return 1ULL << 39;
  case Attribute::Cold:            return 1ULL << 40;
  case Attribute::Builtin:         return 1ULL << 41;
  case Attribute::OptimizeNone:    return 1ULL << 42;
  case Attribute::InAlloca:        return 1ULL << 43;
  case Attribute::NonNull:         return 1ULL << 44;
  case Attribute::JumpTable:       return 1ULL << 45;
  case Attribute::Convergent:      return 1ULL << 46;
  case Attribute::SafeStack:       return 1ULL << 47;
  case Attribute::NoRecurse:       return 1ULL << 48;
  case Attribute::InaccessibleMemOnly:         return 1ULL << 49;
  case Attribute::InaccessibleMemOrArgMemOnly: return 1ULL << 50;
  case Attribute::SwiftSelf:       return 1ULL << 51;
  case Attribute::SwiftError:      return 1ULL << 52;
  case Attribute::WriteOnly:       return 1ULL << 53;
  case Attribute::Speculatable:    return 1ULL << 54;
  case Attribute::StrictFP:        return 1ULL << 55;
  case Attribute::SanitizeHWAddress: return 1ULL << 56;
  case Attribute::NoCfCheck:       return 1ULL << 57;
  case Attribute::OptForFuzzing:   return 1ULL << 58;
  case Attribute::ShadowCallStack: return 1ULL << 59;
  case Attribute::SpeculativeLoadHardening:
    return 1ULL << 60;
  case Attribute::Dereferenceable:
    llvm_unreachable("dereferenceable attribute not supported in raw format");
    break;
  case Attribute::DereferenceableOrNull:
    llvm_unreachable("dereferenceable_or_null attribute not supported in raw "
                     "format");
    break;
  case Attribute::ArgMemOnly:
    llvm_unreachable("argmemonly attribute not supported in raw format");
    break;
  case Attribute::AllocSize:
    llvm_unreachable("allocsize not supported in raw format");
    break;
  }
  llvm_unreachable("Unsupported attribute type");
}

static void addRawAttributeValue(AttrBuilder &B, uint64_t Val) {
  if (!Val) return;

  for (Attribute::AttrKind I = Attribute::None; I != Attribute::EndAttrKinds;
       I = Attribute::AttrKind(I + 1)) {
    if (I == Attribute::Dereferenceable ||
        I == Attribute::DereferenceableOrNull ||
        I == Attribute::ArgMemOnly ||
        I == Attribute::AllocSize)
      continue;
    if (uint64_t A = (Val & getRawAttributeMask(I))) {
      if (I == Attribute::Alignment)
        B.addAlignmentAttr(1ULL << ((A >> 16) - 1));
      else if (I == Attribute::StackAlignment)
        B.addStackAlignmentAttr(1ULL << ((A >> 26)-1));
      else
        B.addAttribute(I);
    }
  }
}

/// This fills an AttrBuilder object with the LLVM attributes that have
/// been decoded from the given integer. This function must stay in sync with
/// 'encodeLLVMAttributesForBitcode'.
static void decodeLLVMAttributesForBitcode(AttrBuilder &B,
                                           uint64_t EncodedAttrs) {
  // FIXME: Remove in 4.0.

  // The alignment is stored as a 16-bit raw value from bits 31--16.  We shift
  // the bits above 31 down by 11 bits.
  unsigned Alignment = (EncodedAttrs & (0xffffULL << 16)) >> 16;
  assert((!Alignment || isPowerOf2_32(Alignment)) &&
         "Alignment must be a power of two.");

  if (Alignment)
    B.addAlignmentAttr(Alignment);
  addRawAttributeValue(B, ((EncodedAttrs & (0xfffffULL << 32)) >> 11) |
                          (EncodedAttrs & 0xffff));
}

Error BitcodeReader::parseAttributeBlock() {
  if (Stream.EnterSubBlock(bitc::PARAMATTR_BLOCK_ID))
    return error("Invalid record");

  if (!MAttributes.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;

  SmallVector<AttributeList, 8> Attrs;

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::PARAMATTR_CODE_ENTRY_OLD: // ENTRY: [paramidx0, attr0, ...]
      // FIXME: Remove in 4.0.
      if (Record.size() & 1)
        return error("Invalid record");

      for (unsigned i = 0, e = Record.size(); i != e; i += 2) {
        AttrBuilder B;
        decodeLLVMAttributesForBitcode(B, Record[i+1]);
        Attrs.push_back(AttributeList::get(Context, Record[i], B));
      }

      MAttributes.push_back(AttributeList::get(Context, Attrs));
      Attrs.clear();
      break;
    case bitc::PARAMATTR_CODE_ENTRY: // ENTRY: [attrgrp0, attrgrp1, ...]
      for (unsigned i = 0, e = Record.size(); i != e; ++i)
        Attrs.push_back(MAttributeGroups[Record[i]]);

      MAttributes.push_back(AttributeList::get(Context, Attrs));
      Attrs.clear();
      break;
    }
  }
}

// Returns Attribute::None on unrecognized codes.
static Attribute::AttrKind getAttrFromCode(uint64_t Code) {
  switch (Code) {
  default:
    return Attribute::None;
  case bitc::ATTR_KIND_ALIGNMENT:
    return Attribute::Alignment;
  case bitc::ATTR_KIND_ALWAYS_INLINE:
    return Attribute::AlwaysInline;
  case bitc::ATTR_KIND_ARGMEMONLY:
    return Attribute::ArgMemOnly;
  case bitc::ATTR_KIND_BUILTIN:
    return Attribute::Builtin;
  case bitc::ATTR_KIND_BY_VAL:
    return Attribute::ByVal;
  case bitc::ATTR_KIND_IN_ALLOCA:
    return Attribute::InAlloca;
  case bitc::ATTR_KIND_COLD:
    return Attribute::Cold;
  case bitc::ATTR_KIND_CONVERGENT:
    return Attribute::Convergent;
  case bitc::ATTR_KIND_INACCESSIBLEMEM_ONLY:
    return Attribute::InaccessibleMemOnly;
  case bitc::ATTR_KIND_INACCESSIBLEMEM_OR_ARGMEMONLY:
    return Attribute::InaccessibleMemOrArgMemOnly;
  case bitc::ATTR_KIND_INLINE_HINT:
    return Attribute::InlineHint;
  case bitc::ATTR_KIND_IN_REG:
    return Attribute::InReg;
  case bitc::ATTR_KIND_JUMP_TABLE:
    return Attribute::JumpTable;
  case bitc::ATTR_KIND_MIN_SIZE:
    return Attribute::MinSize;
  case bitc::ATTR_KIND_NAKED:
    return Attribute::Naked;
  case bitc::ATTR_KIND_NEST:
    return Attribute::Nest;
  case bitc::ATTR_KIND_NO_ALIAS:
    return Attribute::NoAlias;
  case bitc::ATTR_KIND_NO_BUILTIN:
    return Attribute::NoBuiltin;
  case bitc::ATTR_KIND_NO_CAPTURE:
    return Attribute::NoCapture;
  case bitc::ATTR_KIND_NO_DUPLICATE:
    return Attribute::NoDuplicate;
  case bitc::ATTR_KIND_NO_IMPLICIT_FLOAT:
    return Attribute::NoImplicitFloat;
  case bitc::ATTR_KIND_NO_INLINE:
    return Attribute::NoInline;
  case bitc::ATTR_KIND_NO_RECURSE:
    return Attribute::NoRecurse;
  case bitc::ATTR_KIND_NON_LAZY_BIND:
    return Attribute::NonLazyBind;
  case bitc::ATTR_KIND_NON_NULL:
    return Attribute::NonNull;
  case bitc::ATTR_KIND_DEREFERENCEABLE:
    return Attribute::Dereferenceable;
  case bitc::ATTR_KIND_DEREFERENCEABLE_OR_NULL:
    return Attribute::DereferenceableOrNull;
  case bitc::ATTR_KIND_ALLOC_SIZE:
    return Attribute::AllocSize;
  case bitc::ATTR_KIND_NO_RED_ZONE:
    return Attribute::NoRedZone;
  case bitc::ATTR_KIND_NO_RETURN:
    return Attribute::NoReturn;
  case bitc::ATTR_KIND_NOCF_CHECK:
    return Attribute::NoCfCheck;
  case bitc::ATTR_KIND_NO_UNWIND:
    return Attribute::NoUnwind;
  case bitc::ATTR_KIND_OPT_FOR_FUZZING:
    return Attribute::OptForFuzzing;
  case bitc::ATTR_KIND_OPTIMIZE_FOR_SIZE:
    return Attribute::OptimizeForSize;
  case bitc::ATTR_KIND_OPTIMIZE_NONE:
    return Attribute::OptimizeNone;
  case bitc::ATTR_KIND_READ_NONE:
    return Attribute::ReadNone;
  case bitc::ATTR_KIND_READ_ONLY:
    return Attribute::ReadOnly;
  case bitc::ATTR_KIND_RETURNED:
    return Attribute::Returned;
  case bitc::ATTR_KIND_RETURNS_TWICE:
    return Attribute::ReturnsTwice;
  case bitc::ATTR_KIND_S_EXT:
    return Attribute::SExt;
  case bitc::ATTR_KIND_SPECULATABLE:
    return Attribute::Speculatable;
  case bitc::ATTR_KIND_STACK_ALIGNMENT:
    return Attribute::StackAlignment;
  case bitc::ATTR_KIND_STACK_PROTECT:
    return Attribute::StackProtect;
  case bitc::ATTR_KIND_STACK_PROTECT_REQ:
    return Attribute::StackProtectReq;
  case bitc::ATTR_KIND_STACK_PROTECT_STRONG:
    return Attribute::StackProtectStrong;
  case bitc::ATTR_KIND_SAFESTACK:
    return Attribute::SafeStack;
  case bitc::ATTR_KIND_SHADOWCALLSTACK:
    return Attribute::ShadowCallStack;
  case bitc::ATTR_KIND_STRICT_FP:
    return Attribute::StrictFP;
  case bitc::ATTR_KIND_STRUCT_RET:
    return Attribute::StructRet;
  case bitc::ATTR_KIND_SANITIZE_ADDRESS:
    return Attribute::SanitizeAddress;
  case bitc::ATTR_KIND_SANITIZE_HWADDRESS:
    return Attribute::SanitizeHWAddress;
  case bitc::ATTR_KIND_SANITIZE_THREAD:
    return Attribute::SanitizeThread;
  case bitc::ATTR_KIND_SANITIZE_MEMORY:
    return Attribute::SanitizeMemory;
  case bitc::ATTR_KIND_SPECULATIVE_LOAD_HARDENING:
    return Attribute::SpeculativeLoadHardening;
  case bitc::ATTR_KIND_SWIFT_ERROR:
    return Attribute::SwiftError;
  case bitc::ATTR_KIND_SWIFT_SELF:
    return Attribute::SwiftSelf;
  case bitc::ATTR_KIND_UW_TABLE:
    return Attribute::UWTable;
  case bitc::ATTR_KIND_WRITEONLY:
    return Attribute::WriteOnly;
  case bitc::ATTR_KIND_Z_EXT:
    return Attribute::ZExt;
  }
}

Error BitcodeReader::parseAlignmentValue(uint64_t Exponent,
                                         unsigned &Alignment) {
  // Note: Alignment in bitcode files is incremented by 1, so that zero
  // can be used for default alignment.
  if (Exponent > Value::MaxAlignmentExponent + 1)
    return error("Invalid alignment value");
  Alignment = (1 << static_cast<unsigned>(Exponent)) >> 1;
  return Error::success();
}

Error BitcodeReader::parseAttrKind(uint64_t Code, Attribute::AttrKind *Kind) {
  *Kind = getAttrFromCode(Code);
  if (*Kind == Attribute::None)
    return error("Unknown attribute kind (" + Twine(Code) + ")");
  return Error::success();
}

Error BitcodeReader::parseAttributeGroupBlock() {
  if (Stream.EnterSubBlock(bitc::PARAMATTR_GROUP_BLOCK_ID))
    return error("Invalid record");

  if (!MAttributeGroups.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::PARAMATTR_GRP_CODE_ENTRY: { // ENTRY: [grpid, idx, a0, a1, ...]
      if (Record.size() < 3)
        return error("Invalid record");

      uint64_t GrpID = Record[0];
      uint64_t Idx = Record[1]; // Index of the object this attribute refers to.

      AttrBuilder B;
      for (unsigned i = 2, e = Record.size(); i != e; ++i) {
        if (Record[i] == 0) {        // Enum attribute
          Attribute::AttrKind Kind;
          if (Error Err = parseAttrKind(Record[++i], &Kind))
            return Err;

          B.addAttribute(Kind);
        } else if (Record[i] == 1) { // Integer attribute
          Attribute::AttrKind Kind;
          if (Error Err = parseAttrKind(Record[++i], &Kind))
            return Err;
          if (Kind == Attribute::Alignment)
            B.addAlignmentAttr(Record[++i]);
          else if (Kind == Attribute::StackAlignment)
            B.addStackAlignmentAttr(Record[++i]);
          else if (Kind == Attribute::Dereferenceable)
            B.addDereferenceableAttr(Record[++i]);
          else if (Kind == Attribute::DereferenceableOrNull)
            B.addDereferenceableOrNullAttr(Record[++i]);
          else if (Kind == Attribute::AllocSize)
            B.addAllocSizeAttrFromRawRepr(Record[++i]);
        } else {                     // String attribute
          assert((Record[i] == 3 || Record[i] == 4) &&
                 "Invalid attribute group entry");
          bool HasValue = (Record[i++] == 4);
          SmallString<64> KindStr;
          SmallString<64> ValStr;

          while (Record[i] != 0 && i != e)
            KindStr += Record[i++];
          assert(Record[i] == 0 && "Kind string not null terminated");

          if (HasValue) {
            // Has a value associated with it.
            ++i; // Skip the '0' that terminates the "kind" string.
            while (Record[i] != 0 && i != e)
              ValStr += Record[i++];
            assert(Record[i] == 0 && "Value string not null terminated");
          }

          B.addAttribute(KindStr.str(), ValStr.str());
        }
      }

      MAttributeGroups[GrpID] = AttributeList::get(Context, Idx, B);
      break;
    }
    }
  }
}

Error BitcodeReader::parseTypeTable() {
  if (Stream.EnterSubBlock(bitc::TYPE_BLOCK_ID_NEW))
    return error("Invalid record");

  return parseTypeTableBody();
}

Error BitcodeReader::parseTypeTableBody() {
  if (!TypeList.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;
  unsigned NumRecords = 0;

  SmallString<64> TypeName;

  // Read all the records for this type table.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (NumRecords != TypeList.size())
        return error("Malformed block");
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Type *ResultTy = nullptr;
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:
      return error("Invalid value");
    case bitc::TYPE_CODE_NUMENTRY: // TYPE_CODE_NUMENTRY: [numentries]
      // TYPE_CODE_NUMENTRY contains a count of the number of types in the
      // type list.  This allows us to reserve space.
      if (Record.size() < 1)
        return error("Invalid record");
      TypeList.resize(Record[0]);
      continue;
    case bitc::TYPE_CODE_VOID:      // VOID
      ResultTy = Type::getVoidTy(Context);
      break;
    case bitc::TYPE_CODE_HALF:     // HALF
      ResultTy = Type::getHalfTy(Context);
      break;
    case bitc::TYPE_CODE_FLOAT:     // FLOAT
      ResultTy = Type::getFloatTy(Context);
      break;
    case bitc::TYPE_CODE_DOUBLE:    // DOUBLE
      ResultTy = Type::getDoubleTy(Context);
      break;
    case bitc::TYPE_CODE_X86_FP80:  // X86_FP80
      ResultTy = Type::getX86_FP80Ty(Context);
      break;
    case bitc::TYPE_CODE_FP128:     // FP128
      ResultTy = Type::getFP128Ty(Context);
      break;
    case bitc::TYPE_CODE_PPC_FP128: // PPC_FP128
      ResultTy = Type::getPPC_FP128Ty(Context);
      break;
    case bitc::TYPE_CODE_LABEL:     // LABEL
      ResultTy = Type::getLabelTy(Context);
      break;
    case bitc::TYPE_CODE_METADATA:  // METADATA
      ResultTy = Type::getMetadataTy(Context);
      break;
    case bitc::TYPE_CODE_X86_MMX:   // X86_MMX
      ResultTy = Type::getX86_MMXTy(Context);
      break;
    case bitc::TYPE_CODE_TOKEN:     // TOKEN
      ResultTy = Type::getTokenTy(Context);
      break;
    case bitc::TYPE_CODE_INTEGER: { // INTEGER: [width]
      if (Record.size() < 1)
        return error("Invalid record");

      uint64_t NumBits = Record[0];
      if (NumBits < IntegerType::MIN_INT_BITS ||
          NumBits > IntegerType::MAX_INT_BITS)
        return error("Bitwidth for integer type out of range");
      ResultTy = IntegerType::get(Context, NumBits);
      break;
    }
    case bitc::TYPE_CODE_POINTER: { // POINTER: [pointee type] or
                                    //          [pointee type, address space]
      if (Record.size() < 1)
        return error("Invalid record");
      unsigned AddressSpace = 0;
      if (Record.size() == 2)
        AddressSpace = Record[1];
      ResultTy = getTypeByID(Record[0]);
      if (!ResultTy ||
          !PointerType::isValidElementType(ResultTy))
        return error("Invalid type");
      ResultTy = PointerType::get(ResultTy, AddressSpace);
      break;
    }
    case bitc::TYPE_CODE_FUNCTION_OLD: {
      // FIXME: attrid is dead, remove it in LLVM 4.0
      // FUNCTION: [vararg, attrid, retty, paramty x N]
      if (Record.size() < 3)
        return error("Invalid record");
      SmallVector<Type*, 8> ArgTys;
      for (unsigned i = 3, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          ArgTys.push_back(T);
        else
          break;
      }

      ResultTy = getTypeByID(Record[2]);
      if (!ResultTy || ArgTys.size() < Record.size()-3)
        return error("Invalid type");

      ResultTy = FunctionType::get(ResultTy, ArgTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_FUNCTION: {
      // FUNCTION: [vararg, retty, paramty x N]
      if (Record.size() < 2)
        return error("Invalid record");
      SmallVector<Type*, 8> ArgTys;
      for (unsigned i = 2, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i])) {
          if (!FunctionType::isValidArgumentType(T))
            return error("Invalid function argument type");
          ArgTys.push_back(T);
        }
        else
          break;
      }

      ResultTy = getTypeByID(Record[1]);
      if (!ResultTy || ArgTys.size() < Record.size()-2)
        return error("Invalid type");

      ResultTy = FunctionType::get(ResultTy, ArgTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_STRUCT_ANON: {  // STRUCT: [ispacked, eltty x N]
      if (Record.size() < 1)
        return error("Invalid record");
      SmallVector<Type*, 8> EltTys;
      for (unsigned i = 1, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          EltTys.push_back(T);
        else
          break;
      }
      if (EltTys.size() != Record.size()-1)
        return error("Invalid type");
      ResultTy = StructType::get(Context, EltTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_STRUCT_NAME:   // STRUCT_NAME: [strchr x N]
      if (convertToString(Record, 0, TypeName))
        return error("Invalid record");
      continue;

    case bitc::TYPE_CODE_STRUCT_NAMED: { // STRUCT: [ispacked, eltty x N]
      if (Record.size() < 1)
        return error("Invalid record");

      if (NumRecords >= TypeList.size())
        return error("Invalid TYPE table");

      // Check to see if this was forward referenced, if so fill in the temp.
      StructType *Res = cast_or_null<StructType>(TypeList[NumRecords]);
      if (Res) {
        Res->setName(TypeName);
        TypeList[NumRecords] = nullptr;
      } else  // Otherwise, create a new struct.
        Res = createIdentifiedStructType(Context, TypeName);
      TypeName.clear();

      SmallVector<Type*, 8> EltTys;
      for (unsigned i = 1, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          EltTys.push_back(T);
        else
          break;
      }
      if (EltTys.size() != Record.size()-1)
        return error("Invalid record");
      Res->setBody(EltTys, Record[0]);
      ResultTy = Res;
      break;
    }
    case bitc::TYPE_CODE_OPAQUE: {       // OPAQUE: []
      if (Record.size() != 1)
        return error("Invalid record");

      if (NumRecords >= TypeList.size())
        return error("Invalid TYPE table");

      // Check to see if this was forward referenced, if so fill in the temp.
      StructType *Res = cast_or_null<StructType>(TypeList[NumRecords]);
      if (Res) {
        Res->setName(TypeName);
        TypeList[NumRecords] = nullptr;
      } else  // Otherwise, create a new struct with no body.
        Res = createIdentifiedStructType(Context, TypeName);
      TypeName.clear();
      ResultTy = Res;
      break;
    }
    case bitc::TYPE_CODE_ARRAY:     // ARRAY: [numelts, eltty]
      if (Record.size() < 2)
        return error("Invalid record");
      ResultTy = getTypeByID(Record[1]);
      if (!ResultTy || !ArrayType::isValidElementType(ResultTy))
        return error("Invalid type");
      ResultTy = ArrayType::get(ResultTy, Record[0]);
      break;
    case bitc::TYPE_CODE_VECTOR:    // VECTOR: [numelts, eltty]
      if (Record.size() < 2)
        return error("Invalid record");
      if (Record[0] == 0)
        return error("Invalid vector length");
      ResultTy = getTypeByID(Record[1]);
      if (!ResultTy || !StructType::isValidElementType(ResultTy))
        return error("Invalid type");
      ResultTy = VectorType::get(ResultTy, Record[0]);
      break;
    }

    if (NumRecords >= TypeList.size())
      return error("Invalid TYPE table");
    if (TypeList[NumRecords])
      return error(
          "Invalid TYPE table: Only named structs can be forward referenced");
    assert(ResultTy && "Didn't read a type?");
    TypeList[NumRecords++] = ResultTy;
  }
}

Error BitcodeReader::parseOperandBundleTags() {
  if (Stream.EnterSubBlock(bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID))
    return error("Invalid record");

  if (!BundleTags.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Tags are implicitly mapped to integers by their order.

    if (Stream.readRecord(Entry.ID, Record) != bitc::OPERAND_BUNDLE_TAG)
      return error("Invalid record");

    // OPERAND_BUNDLE_TAG: [strchr x N]
    BundleTags.emplace_back();
    if (convertToString(Record, 0, BundleTags.back()))
      return error("Invalid record");
    Record.clear();
  }
}

Error BitcodeReader::parseSyncScopeNames() {
  if (Stream.EnterSubBlock(bitc::SYNC_SCOPE_NAMES_BLOCK_ID))
    return error("Invalid record");

  if (!SSIDs.empty())
    return error("Invalid multiple synchronization scope names blocks");

  SmallVector<uint64_t, 64> Record;
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();
    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (SSIDs.empty())
        return error("Invalid empty synchronization scope names block");
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Synchronization scope names are implicitly mapped to synchronization
    // scope IDs by their order.

    if (Stream.readRecord(Entry.ID, Record) != bitc::SYNC_SCOPE_NAME)
      return error("Invalid record");

    SmallString<16> SSN;
    if (convertToString(Record, 0, SSN))
      return error("Invalid record");

    SSIDs.push_back(Context.getOrInsertSyncScopeID(SSN));
    Record.clear();
  }
}

/// Associate a value with its name from the given index in the provided record.
Expected<Value *> BitcodeReader::recordValue(SmallVectorImpl<uint64_t> &Record,
                                             unsigned NameIndex, Triple &TT) {
  SmallString<128> ValueName;
  if (convertToString(Record, NameIndex, ValueName))
    return error("Invalid record");
  unsigned ValueID = Record[0];
  if (ValueID >= ValueList.size() || !ValueList[ValueID])
    return error("Invalid record");
  Value *V = ValueList[ValueID];

  StringRef NameStr(ValueName.data(), ValueName.size());
  if (NameStr.find_first_of(0) != StringRef::npos)
    return error("Invalid value name");
  V->setName(NameStr);
  auto *GO = dyn_cast<GlobalObject>(V);
  if (GO) {
    if (GO->getComdat() == reinterpret_cast<Comdat *>(1)) {
      if (TT.supportsCOMDAT())
        GO->setComdat(TheModule->getOrInsertComdat(V->getName()));
      else
        GO->setComdat(nullptr);
    }
  }
  return V;
}

/// Helper to note and return the current location, and jump to the given
/// offset.
static uint64_t jumpToValueSymbolTable(uint64_t Offset,
                                       BitstreamCursor &Stream) {
  // Save the current parsing location so we can jump back at the end
  // of the VST read.
  uint64_t CurrentBit = Stream.GetCurrentBitNo();
  Stream.JumpToBit(Offset * 32);
#ifndef NDEBUG
  // Do some checking if we are in debug mode.
  BitstreamEntry Entry = Stream.advance();
  assert(Entry.Kind == BitstreamEntry::SubBlock);
  assert(Entry.ID == bitc::VALUE_SYMTAB_BLOCK_ID);
#else
  // In NDEBUG mode ignore the output so we don't get an unused variable
  // warning.
  Stream.advance();
#endif
  return CurrentBit;
}

void BitcodeReader::setDeferredFunctionInfo(unsigned FuncBitcodeOffsetDelta,
                                            Function *F,
                                            ArrayRef<uint64_t> Record) {
  // Note that we subtract 1 here because the offset is relative to one word
  // before the start of the identification or module block, which was
  // historically always the start of the regular bitcode header.
  uint64_t FuncWordOffset = Record[1] - 1;
  uint64_t FuncBitOffset = FuncWordOffset * 32;
  DeferredFunctionInfo[F] = FuncBitOffset + FuncBitcodeOffsetDelta;
  // Set the LastFunctionBlockBit to point to the last function block.
  // Later when parsing is resumed after function materialization,
  // we can simply skip that last function block.
  if (FuncBitOffset > LastFunctionBlockBit)
    LastFunctionBlockBit = FuncBitOffset;
}

/// Read a new-style GlobalValue symbol table.
Error BitcodeReader::parseGlobalValueSymbolTable() {
  unsigned FuncBitcodeOffsetDelta =
      Stream.getAbbrevIDWidth() + bitc::BlockIDWidth;

  if (Stream.EnterSubBlock(bitc::VALUE_SYMTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock:
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      break;
    }

    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    case bitc::VST_CODE_FNENTRY: // [valueid, offset]
      setDeferredFunctionInfo(FuncBitcodeOffsetDelta,
                              cast<Function>(ValueList[Record[0]]), Record);
      break;
    }
  }
}

/// Parse the value symbol table at either the current parsing location or
/// at the given bit offset if provided.
Error BitcodeReader::parseValueSymbolTable(uint64_t Offset) {
  uint64_t CurrentBit;
  // Pass in the Offset to distinguish between calling for the module-level
  // VST (where we want to jump to the VST offset) and the function-level
  // VST (where we don't).
  if (Offset > 0) {
    CurrentBit = jumpToValueSymbolTable(Offset, Stream);
    // If this module uses a string table, read this as a module-level VST.
    if (UseStrtab) {
      if (Error Err = parseGlobalValueSymbolTable())
        return Err;
      Stream.JumpToBit(CurrentBit);
      return Error::success();
    }
    // Otherwise, the VST will be in a similar format to a function-level VST,
    // and will contain symbol names.
  }

  // Compute the delta between the bitcode indices in the VST (the word offset
  // to the word-aligned ENTER_SUBBLOCK for the function block, and that
  // expected by the lazy reader. The reader's EnterSubBlock expects to have
  // already read the ENTER_SUBBLOCK code (size getAbbrevIDWidth) and BlockID
  // (size BlockIDWidth). Note that we access the stream's AbbrevID width here
  // just before entering the VST subblock because: 1) the EnterSubBlock
  // changes the AbbrevID width; 2) the VST block is nested within the same
  // outer MODULE_BLOCK as the FUNCTION_BLOCKs and therefore have the same
  // AbbrevID width before calling EnterSubBlock; and 3) when we want to
  // jump to the FUNCTION_BLOCK using this offset later, we don't want
  // to rely on the stream's AbbrevID width being that of the MODULE_BLOCK.
  unsigned FuncBitcodeOffsetDelta =
      Stream.getAbbrevIDWidth() + bitc::BlockIDWidth;

  if (Stream.EnterSubBlock(bitc::VALUE_SYMTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  Triple TT(TheModule->getTargetTriple());

  // Read all the records for this value table.
  SmallString<128> ValueName;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (Offset > 0)
        Stream.JumpToBit(CurrentBit);
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: unknown type.
      break;
    case bitc::VST_CODE_ENTRY: {  // VST_CODE_ENTRY: [valueid, namechar x N]
      Expected<Value *> ValOrErr = recordValue(Record, 1, TT);
      if (Error Err = ValOrErr.takeError())
        return Err;
      ValOrErr.get();
      break;
    }
    case bitc::VST_CODE_FNENTRY: {
      // VST_CODE_FNENTRY: [valueid, offset, namechar x N]
      Expected<Value *> ValOrErr = recordValue(Record, 2, TT);
      if (Error Err = ValOrErr.takeError())
        return Err;
      Value *V = ValOrErr.get();

      // Ignore function offsets emitted for aliases of functions in older
      // versions of LLVM.
      if (auto *F = dyn_cast<Function>(V))
        setDeferredFunctionInfo(FuncBitcodeOffsetDelta, F, Record);
      break;
    }
    case bitc::VST_CODE_BBENTRY: {
      if (convertToString(Record, 1, ValueName))
        return error("Invalid record");
      BasicBlock *BB = getBasicBlock(Record[0]);
      if (!BB)
        return error("Invalid record");

      BB->setName(StringRef(ValueName.data(), ValueName.size()));
      ValueName.clear();
      break;
    }
    }
  }
}

/// Decode a signed value stored with the sign bit in the LSB for dense VBR
/// encoding.
uint64_t BitcodeReader::decodeSignRotatedValue(uint64_t V) {
  if ((V & 1) == 0)
    return V >> 1;
  if (V != 1)
    return -(V >> 1);
  // There is no such thing as -0 with integers.  "-0" really means MININT.
  return 1ULL << 63;
}

/// Resolve all of the initializers for global values and aliases that we can.
Error BitcodeReader::resolveGlobalAndIndirectSymbolInits() {
  std::vector<std::pair<GlobalVariable *, unsigned>> GlobalInitWorklist;
  std::vector<std::pair<GlobalIndirectSymbol *, unsigned>>
      IndirectSymbolInitWorklist;
  std::vector<std::pair<Function *, unsigned>> FunctionPrefixWorklist;
  std::vector<std::pair<Function *, unsigned>> FunctionPrologueWorklist;
  std::vector<std::pair<Function *, unsigned>> FunctionPersonalityFnWorklist;

  GlobalInitWorklist.swap(GlobalInits);
  IndirectSymbolInitWorklist.swap(IndirectSymbolInits);
  FunctionPrefixWorklist.swap(FunctionPrefixes);
  FunctionPrologueWorklist.swap(FunctionPrologues);
  FunctionPersonalityFnWorklist.swap(FunctionPersonalityFns);

  while (!GlobalInitWorklist.empty()) {
    unsigned ValID = GlobalInitWorklist.back().second;
    if (ValID >= ValueList.size()) {
      // Not ready to resolve this yet, it requires something later in the file.
      GlobalInits.push_back(GlobalInitWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        GlobalInitWorklist.back().first->setInitializer(C);
      else
        return error("Expected a constant");
    }
    GlobalInitWorklist.pop_back();
  }

  while (!IndirectSymbolInitWorklist.empty()) {
    unsigned ValID = IndirectSymbolInitWorklist.back().second;
    if (ValID >= ValueList.size()) {
      IndirectSymbolInits.push_back(IndirectSymbolInitWorklist.back());
    } else {
      Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]);
      if (!C)
        return error("Expected a constant");
      GlobalIndirectSymbol *GIS = IndirectSymbolInitWorklist.back().first;
      if (isa<GlobalAlias>(GIS) && C->getType() != GIS->getType())
        return error("Alias and aliasee types don't match");
      GIS->setIndirectSymbol(C);
    }
    IndirectSymbolInitWorklist.pop_back();
  }

  while (!FunctionPrefixWorklist.empty()) {
    unsigned ValID = FunctionPrefixWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPrefixes.push_back(FunctionPrefixWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        FunctionPrefixWorklist.back().first->setPrefixData(C);
      else
        return error("Expected a constant");
    }
    FunctionPrefixWorklist.pop_back();
  }

  while (!FunctionPrologueWorklist.empty()) {
    unsigned ValID = FunctionPrologueWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPrologues.push_back(FunctionPrologueWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        FunctionPrologueWorklist.back().first->setPrologueData(C);
      else
        return error("Expected a constant");
    }
    FunctionPrologueWorklist.pop_back();
  }

  while (!FunctionPersonalityFnWorklist.empty()) {
    unsigned ValID = FunctionPersonalityFnWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPersonalityFns.push_back(FunctionPersonalityFnWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        FunctionPersonalityFnWorklist.back().first->setPersonalityFn(C);
      else
        return error("Expected a constant");
    }
    FunctionPersonalityFnWorklist.pop_back();
  }

  return Error::success();
}

static APInt readWideAPInt(ArrayRef<uint64_t> Vals, unsigned TypeBits) {
  SmallVector<uint64_t, 8> Words(Vals.size());
  transform(Vals, Words.begin(),
                 BitcodeReader::decodeSignRotatedValue);

  return APInt(TypeBits, Words);
}

Error BitcodeReader::parseConstants() {
  if (Stream.EnterSubBlock(bitc::CONSTANTS_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this value table.
  Type *CurTy = Type::getInt32Ty(Context);
  unsigned NextCstNo = ValueList.size();

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (NextCstNo != ValueList.size())
        return error("Invalid constant reference");

      // Once all the constants have been read, go through and resolve forward
      // references.
      ValueList.resolveConstantForwardRefs();
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Type *VoidType = Type::getVoidTy(Context);
    Value *V = nullptr;
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default:  // Default behavior: unknown constant
    case bitc::CST_CODE_UNDEF:     // UNDEF
      V = UndefValue::get(CurTy);
      break;
    case bitc::CST_CODE_SETTYPE:   // SETTYPE: [typeid]
      if (Record.empty())
        return error("Invalid record");
      if (Record[0] >= TypeList.size() || !TypeList[Record[0]])
        return error("Invalid record");
      if (TypeList[Record[0]] == VoidType)
        return error("Invalid constant type");
      CurTy = TypeList[Record[0]];
      continue;  // Skip the ValueList manipulation.
    case bitc::CST_CODE_NULL:      // NULL
      V = Constant::getNullValue(CurTy);
      break;
    case bitc::CST_CODE_INTEGER:   // INTEGER: [intval]
      if (!CurTy->isIntegerTy() || Record.empty())
        return error("Invalid record");
      V = ConstantInt::get(CurTy, decodeSignRotatedValue(Record[0]));
      break;
    case bitc::CST_CODE_WIDE_INTEGER: {// WIDE_INTEGER: [n x intval]
      if (!CurTy->isIntegerTy() || Record.empty())
        return error("Invalid record");

      APInt VInt =
          readWideAPInt(Record, cast<IntegerType>(CurTy)->getBitWidth());
      V = ConstantInt::get(Context, VInt);

      break;
    }
    case bitc::CST_CODE_FLOAT: {    // FLOAT: [fpval]
      if (Record.empty())
        return error("Invalid record");
      if (CurTy->isHalfTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEhalf(),
                                             APInt(16, (uint16_t)Record[0])));
      else if (CurTy->isFloatTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEsingle(),
                                             APInt(32, (uint32_t)Record[0])));
      else if (CurTy->isDoubleTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEdouble(),
                                             APInt(64, Record[0])));
      else if (CurTy->isX86_FP80Ty()) {
        // Bits are not stored the same way as a normal i80 APInt, compensate.
        uint64_t Rearrange[2];
        Rearrange[0] = (Record[1] & 0xffffLL) | (Record[0] << 16);
        Rearrange[1] = Record[0] >> 48;
        V = ConstantFP::get(Context, APFloat(APFloat::x87DoubleExtended(),
                                             APInt(80, Rearrange)));
      } else if (CurTy->isFP128Ty())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEquad(),
                                             APInt(128, Record)));
      else if (CurTy->isPPC_FP128Ty())
        V = ConstantFP::get(Context, APFloat(APFloat::PPCDoubleDouble(),
                                             APInt(128, Record)));
      else
        V = UndefValue::get(CurTy);
      break;
    }

    case bitc::CST_CODE_AGGREGATE: {// AGGREGATE: [n x value number]
      if (Record.empty())
        return error("Invalid record");

      unsigned Size = Record.size();
      SmallVector<Constant*, 16> Elts;

      if (StructType *STy = dyn_cast<StructType>(CurTy)) {
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i],
                                                     STy->getElementType(i)));
        V = ConstantStruct::get(STy, Elts);
      } else if (ArrayType *ATy = dyn_cast<ArrayType>(CurTy)) {
        Type *EltTy = ATy->getElementType();
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i], EltTy));
        V = ConstantArray::get(ATy, Elts);
      } else if (VectorType *VTy = dyn_cast<VectorType>(CurTy)) {
        Type *EltTy = VTy->getElementType();
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i], EltTy));
        V = ConstantVector::get(Elts);
      } else {
        V = UndefValue::get(CurTy);
      }
      break;
    }
    case bitc::CST_CODE_STRING:    // STRING: [values]
    case bitc::CST_CODE_CSTRING: { // CSTRING: [values]
      if (Record.empty())
        return error("Invalid record");

      SmallString<16> Elts(Record.begin(), Record.end());
      V = ConstantDataArray::getString(Context, Elts,
                                       BitCode == bitc::CST_CODE_CSTRING);
      break;
    }
    case bitc::CST_CODE_DATA: {// DATA: [n x value]
      if (Record.empty())
        return error("Invalid record");

      Type *EltTy = cast<SequentialType>(CurTy)->getElementType();
      if (EltTy->isIntegerTy(8)) {
        SmallVector<uint8_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(16)) {
        SmallVector<uint16_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(32)) {
        SmallVector<uint32_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(64)) {
        SmallVector<uint64_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isHalfTy()) {
        SmallVector<uint16_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::getFP(Context, Elts);
        else
          V = ConstantDataArray::getFP(Context, Elts);
      } else if (EltTy->isFloatTy()) {
        SmallVector<uint32_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::getFP(Context, Elts);
        else
          V = ConstantDataArray::getFP(Context, Elts);
      } else if (EltTy->isDoubleTy()) {
        SmallVector<uint64_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::getFP(Context, Elts);
        else
          V = ConstantDataArray::getFP(Context, Elts);
      } else {
        return error("Invalid type for value");
      }
      break;
    }
    case bitc::CST_CODE_CE_UNOP: {  // CE_UNOP: [opcode, opval]
      if (Record.size() < 2)
        return error("Invalid record");
      int Opc = getDecodedUnaryOpcode(Record[0], CurTy);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown unop.
      } else {
        Constant *LHS = ValueList.getConstantFwdRef(Record[1], CurTy);
        unsigned Flags = 0;
        V = ConstantExpr::get(Opc, LHS, Flags);
      }
      break;
    }
    case bitc::CST_CODE_CE_BINOP: {  // CE_BINOP: [opcode, opval, opval]
      if (Record.size() < 3)
        return error("Invalid record");
      int Opc = getDecodedBinaryOpcode(Record[0], CurTy);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown binop.
      } else {
        Constant *LHS = ValueList.getConstantFwdRef(Record[1], CurTy);
        Constant *RHS = ValueList.getConstantFwdRef(Record[2], CurTy);
        unsigned Flags = 0;
        if (Record.size() >= 4) {
          if (Opc == Instruction::Add ||
              Opc == Instruction::Sub ||
              Opc == Instruction::Mul ||
              Opc == Instruction::Shl) {
            if (Record[3] & (1 << bitc::OBO_NO_SIGNED_WRAP))
              Flags |= OverflowingBinaryOperator::NoSignedWrap;
            if (Record[3] & (1 << bitc::OBO_NO_UNSIGNED_WRAP))
              Flags |= OverflowingBinaryOperator::NoUnsignedWrap;
          } else if (Opc == Instruction::SDiv ||
                     Opc == Instruction::UDiv ||
                     Opc == Instruction::LShr ||
                     Opc == Instruction::AShr) {
            if (Record[3] & (1 << bitc::PEO_EXACT))
              Flags |= SDivOperator::IsExact;
          }
        }
        V = ConstantExpr::get(Opc, LHS, RHS, Flags);
      }
      break;
    }
    case bitc::CST_CODE_CE_CAST: {  // CE_CAST: [opcode, opty, opval]
      if (Record.size() < 3)
        return error("Invalid record");
      int Opc = getDecodedCastOpcode(Record[0]);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown cast.
      } else {
        Type *OpTy = getTypeByID(Record[1]);
        if (!OpTy)
          return error("Invalid record");
        Constant *Op = ValueList.getConstantFwdRef(Record[2], OpTy);
        V = UpgradeBitCastExpr(Opc, Op, CurTy);
        if (!V) V = ConstantExpr::getCast(Opc, Op, CurTy);
      }
      break;
    }
    case bitc::CST_CODE_CE_INBOUNDS_GEP: // [ty, n x operands]
    case bitc::CST_CODE_CE_GEP: // [ty, n x operands]
    case bitc::CST_CODE_CE_GEP_WITH_INRANGE_INDEX: { // [ty, flags, n x
                                                     // operands]
      unsigned OpNum = 0;
      Type *PointeeType = nullptr;
      if (BitCode == bitc::CST_CODE_CE_GEP_WITH_INRANGE_INDEX ||
          Record.size() % 2)
        PointeeType = getTypeByID(Record[OpNum++]);

      bool InBounds = false;
      Optional<unsigned> InRangeIndex;
      if (BitCode == bitc::CST_CODE_CE_GEP_WITH_INRANGE_INDEX) {
        uint64_t Op = Record[OpNum++];
        InBounds = Op & 1;
        InRangeIndex = Op >> 1;
      } else if (BitCode == bitc::CST_CODE_CE_INBOUNDS_GEP)
        InBounds = true;

      SmallVector<Constant*, 16> Elts;
      while (OpNum != Record.size()) {
        Type *ElTy = getTypeByID(Record[OpNum++]);
        if (!ElTy)
          return error("Invalid record");
        Elts.push_back(ValueList.getConstantFwdRef(Record[OpNum++], ElTy));
      }

      if (PointeeType &&
          PointeeType !=
              cast<PointerType>(Elts[0]->getType()->getScalarType())
                  ->getElementType())
        return error("Explicit gep operator type does not match pointee type "
                     "of pointer operand");

      if (Elts.size() < 1)
        return error("Invalid gep with no operands");

      ArrayRef<Constant *> Indices(Elts.begin() + 1, Elts.end());
      V = ConstantExpr::getGetElementPtr(PointeeType, Elts[0], Indices,
                                         InBounds, InRangeIndex);
      break;
    }
    case bitc::CST_CODE_CE_SELECT: {  // CE_SELECT: [opval#, opval#, opval#]
      if (Record.size() < 3)
        return error("Invalid record");

      Type *SelectorTy = Type::getInt1Ty(Context);

      // The selector might be an i1 or an <n x i1>
      // Get the type from the ValueList before getting a forward ref.
      if (VectorType *VTy = dyn_cast<VectorType>(CurTy))
        if (Value *V = ValueList[Record[0]])
          if (SelectorTy != V->getType())
            SelectorTy = VectorType::get(SelectorTy, VTy->getNumElements());

      V = ConstantExpr::getSelect(ValueList.getConstantFwdRef(Record[0],
                                                              SelectorTy),
                                  ValueList.getConstantFwdRef(Record[1],CurTy),
                                  ValueList.getConstantFwdRef(Record[2],CurTy));
      break;
    }
    case bitc::CST_CODE_CE_EXTRACTELT
        : { // CE_EXTRACTELT: [opty, opval, opty, opval]
      if (Record.size() < 3)
        return error("Invalid record");
      VectorType *OpTy =
        dyn_cast_or_null<VectorType>(getTypeByID(Record[0]));
      if (!OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = nullptr;
      if (Record.size() == 4) {
        Type *IdxTy = getTypeByID(Record[2]);
        if (!IdxTy)
          return error("Invalid record");
        Op1 = ValueList.getConstantFwdRef(Record[3], IdxTy);
      } else // TODO: Remove with llvm 4.0
        Op1 = ValueList.getConstantFwdRef(Record[2], Type::getInt32Ty(Context));
      if (!Op1)
        return error("Invalid record");
      V = ConstantExpr::getExtractElement(Op0, Op1);
      break;
    }
    case bitc::CST_CODE_CE_INSERTELT
        : { // CE_INSERTELT: [opval, opval, opty, opval]
      VectorType *OpTy = dyn_cast<VectorType>(CurTy);
      if (Record.size() < 3 || !OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[0], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[1],
                                                  OpTy->getElementType());
      Constant *Op2 = nullptr;
      if (Record.size() == 4) {
        Type *IdxTy = getTypeByID(Record[2]);
        if (!IdxTy)
          return error("Invalid record");
        Op2 = ValueList.getConstantFwdRef(Record[3], IdxTy);
      } else // TODO: Remove with llvm 4.0
        Op2 = ValueList.getConstantFwdRef(Record[2], Type::getInt32Ty(Context));
      if (!Op2)
        return error("Invalid record");
      V = ConstantExpr::getInsertElement(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_SHUFFLEVEC: { // CE_SHUFFLEVEC: [opval, opval, opval]
      VectorType *OpTy = dyn_cast<VectorType>(CurTy);
      if (Record.size() < 3 || !OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[0], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Type *ShufTy = VectorType::get(Type::getInt32Ty(Context),
                                                 OpTy->getNumElements());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[2], ShufTy);
      V = ConstantExpr::getShuffleVector(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_SHUFVEC_EX: { // [opty, opval, opval, opval]
      VectorType *RTy = dyn_cast<VectorType>(CurTy);
      VectorType *OpTy =
        dyn_cast_or_null<VectorType>(getTypeByID(Record[0]));
      if (Record.size() < 4 || !RTy || !OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2], OpTy);
      Type *ShufTy = VectorType::get(Type::getInt32Ty(Context),
                                                 RTy->getNumElements());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[3], ShufTy);
      V = ConstantExpr::getShuffleVector(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_CMP: {     // CE_CMP: [opty, opval, opval, pred]
      if (Record.size() < 4)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      if (!OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2], OpTy);

      if (OpTy->isFPOrFPVectorTy())
        V = ConstantExpr::getFCmp(Record[3], Op0, Op1);
      else
        V = ConstantExpr::getICmp(Record[3], Op0, Op1);
      break;
    }
    // This maintains backward compatibility, pre-asm dialect keywords.
    // FIXME: Remove with the 4.0 release.
    case bitc::CST_CODE_INLINEASM_OLD: {
      if (Record.size() < 2)
        return error("Invalid record");
      std::string AsmStr, ConstrStr;
      bool HasSideEffects = Record[0] & 1;
      bool IsAlignStack = Record[0] >> 1;
      unsigned AsmStrSize = Record[1];
      if (2+AsmStrSize >= Record.size())
        return error("Invalid record");
      unsigned ConstStrSize = Record[2+AsmStrSize];
      if (3+AsmStrSize+ConstStrSize > Record.size())
        return error("Invalid record");

      for (unsigned i = 0; i != AsmStrSize; ++i)
        AsmStr += (char)Record[2+i];
      for (unsigned i = 0; i != ConstStrSize; ++i)
        ConstrStr += (char)Record[3+AsmStrSize+i];
      PointerType *PTy = cast<PointerType>(CurTy);
      UpgradeInlineAsmString(&AsmStr);
      V = InlineAsm::get(cast<FunctionType>(PTy->getElementType()),
                         AsmStr, ConstrStr, HasSideEffects, IsAlignStack);
      break;
    }
    // This version adds support for the asm dialect keywords (e.g.,
    // inteldialect).
    case bitc::CST_CODE_INLINEASM: {
      if (Record.size() < 2)
        return error("Invalid record");
      std::string AsmStr, ConstrStr;
      bool HasSideEffects = Record[0] & 1;
      bool IsAlignStack = (Record[0] >> 1) & 1;
      unsigned AsmDialect = Record[0] >> 2;
      unsigned AsmStrSize = Record[1];
      if (2+AsmStrSize >= Record.size())
        return error("Invalid record");
      unsigned ConstStrSize = Record[2+AsmStrSize];
      if (3+AsmStrSize+ConstStrSize > Record.size())
        return error("Invalid record");

      for (unsigned i = 0; i != AsmStrSize; ++i)
        AsmStr += (char)Record[2+i];
      for (unsigned i = 0; i != ConstStrSize; ++i)
        ConstrStr += (char)Record[3+AsmStrSize+i];
      PointerType *PTy = cast<PointerType>(CurTy);
      UpgradeInlineAsmString(&AsmStr);
      V = InlineAsm::get(cast<FunctionType>(PTy->getElementType()),
                         AsmStr, ConstrStr, HasSideEffects, IsAlignStack,
                         InlineAsm::AsmDialect(AsmDialect));
      break;
    }
    case bitc::CST_CODE_BLOCKADDRESS:{
      if (Record.size() < 3)
        return error("Invalid record");
      Type *FnTy = getTypeByID(Record[0]);
      if (!FnTy)
        return error("Invalid record");
      Function *Fn =
        dyn_cast_or_null<Function>(ValueList.getConstantFwdRef(Record[1],FnTy));
      if (!Fn)
        return error("Invalid record");

      // If the function is already parsed we can insert the block address right
      // away.
      BasicBlock *BB;
      unsigned BBID = Record[2];
      if (!BBID)
        // Invalid reference to entry block.
        return error("Invalid ID");
      if (!Fn->empty()) {
        Function::iterator BBI = Fn->begin(), BBE = Fn->end();
        for (size_t I = 0, E = BBID; I != E; ++I) {
          if (BBI == BBE)
            return error("Invalid ID");
          ++BBI;
        }
        BB = &*BBI;
      } else {
        // Otherwise insert a placeholder and remember it so it can be inserted
        // when the function is parsed.
        auto &FwdBBs = BasicBlockFwdRefs[Fn];
        if (FwdBBs.empty())
          BasicBlockFwdRefQueue.push_back(Fn);
        if (FwdBBs.size() < BBID + 1)
          FwdBBs.resize(BBID + 1);
        if (!FwdBBs[BBID])
          FwdBBs[BBID] = BasicBlock::Create(Context);
        BB = FwdBBs[BBID];
      }
      V = BlockAddress::get(Fn, BB);
      break;
    }
    }

    ValueList.assignValue(V, NextCstNo);
    ++NextCstNo;
  }
}

Error BitcodeReader::parseUseLists() {
  if (Stream.EnterSubBlock(bitc::USELIST_BLOCK_ID))
    return error("Invalid record");

  // Read all the records.
  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a use list record.
    Record.clear();
    bool IsBB = false;
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: unknown type.
      break;
    case bitc::USELIST_CODE_BB:
      IsBB = true;
      LLVM_FALLTHROUGH;
    case bitc::USELIST_CODE_DEFAULT: {
      unsigned RecordLength = Record.size();
      if (RecordLength < 3)
        // Records should have at least an ID and two indexes.
        return error("Invalid record");
      unsigned ID = Record.back();
      Record.pop_back();

      Value *V;
      if (IsBB) {
        assert(ID < FunctionBBs.size() && "Basic block not found");
        V = FunctionBBs[ID];
      } else
        V = ValueList[ID];
      unsigned NumUses = 0;
      SmallDenseMap<const Use *, unsigned, 16> Order;
      for (const Use &U : V->materialized_uses()) {
        if (++NumUses > Record.size())
          break;
        Order[&U] = Record[NumUses - 1];
      }
      if (Order.size() != Record.size() || NumUses > Record.size())
        // Mismatches can happen if the functions are being materialized lazily
        // (out-of-order), or a value has been upgraded.
        break;

      V->sortUseList([&](const Use &L, const Use &R) {
        return Order.lookup(&L) < Order.lookup(&R);
      });
      break;
    }
    }
  }
}

/// When we see the block for metadata, remember where it is and then skip it.
/// This lets us lazily deserialize the metadata.
Error BitcodeReader::rememberAndSkipMetadata() {
  // Save the current stream state.
  uint64_t CurBit = Stream.GetCurrentBitNo();
  DeferredMetadataInfo.push_back(CurBit);

  // Skip over the block for now.
  if (Stream.SkipBlock())
    return error("Invalid record");
  return Error::success();
}

Error BitcodeReader::materializeMetadata() {
  for (uint64_t BitPos : DeferredMetadataInfo) {
    // Move the bit stream to the saved position.
    Stream.JumpToBit(BitPos);
    if (Error Err = MDLoader->parseModuleMetadata())
      return Err;
  }

  // Upgrade "Linker Options" module flag to "llvm.linker.options" module-level
  // metadata.
  if (Metadata *Val = TheModule->getModuleFlag("Linker Options")) {
    NamedMDNode *LinkerOpts =
        TheModule->getOrInsertNamedMetadata("llvm.linker.options");
    for (const MDOperand &MDOptions : cast<MDNode>(Val)->operands())
      LinkerOpts->addOperand(cast<MDNode>(MDOptions));
  }

  DeferredMetadataInfo.clear();
  return Error::success();
}

void BitcodeReader::setStripDebugInfo() { StripDebugInfo = true; }

/// When we see the block for a function body, remember where it is and then
/// skip it.  This lets us lazily deserialize the functions.
Error BitcodeReader::rememberAndSkipFunctionBody() {
  // Get the function we are talking about.
  if (FunctionsWithBodies.empty())
    return error("Insufficient function protos");

  Function *Fn = FunctionsWithBodies.back();
  FunctionsWithBodies.pop_back();

  // Save the current stream state.
  uint64_t CurBit = Stream.GetCurrentBitNo();
  assert(
      (DeferredFunctionInfo[Fn] == 0 || DeferredFunctionInfo[Fn] == CurBit) &&
      "Mismatch between VST and scanned function offsets");
  DeferredFunctionInfo[Fn] = CurBit;

  // Skip over the function block for now.
  if (Stream.SkipBlock())
    return error("Invalid record");
  return Error::success();
}

Error BitcodeReader::globalCleanup() {
  // Patch the initializers for globals and aliases up.
  if (Error Err = resolveGlobalAndIndirectSymbolInits())
    return Err;
  if (!GlobalInits.empty() || !IndirectSymbolInits.empty())
    return error("Malformed global initializer set");

  // Look for intrinsic functions which need to be upgraded at some point
  for (Function &F : *TheModule) {
    MDLoader->upgradeDebugIntrinsics(F);
    Function *NewFn;
    if (UpgradeIntrinsicFunction(&F, NewFn))
      UpgradedIntrinsics[&F] = NewFn;
    else if (auto Remangled = Intrinsic::remangleIntrinsicFunction(&F))
      // Some types could be renamed during loading if several modules are
      // loaded in the same LLVMContext (LTO scenario). In this case we should
      // remangle intrinsics names as well.
      RemangledIntrinsics[&F] = Remangled.getValue();
  }

  // Look for global variables which need to be renamed.
  for (GlobalVariable &GV : TheModule->globals())
    UpgradeGlobalVariable(&GV);

  // Force deallocation of memory for these vectors to favor the client that
  // want lazy deserialization.
  std::vector<std::pair<GlobalVariable *, unsigned>>().swap(GlobalInits);
  std::vector<std::pair<GlobalIndirectSymbol *, unsigned>>().swap(
      IndirectSymbolInits);
  return Error::success();
}

/// Support for lazy parsing of function bodies. This is required if we
/// either have an old bitcode file without a VST forward declaration record,
/// or if we have an anonymous function being materialized, since anonymous
/// functions do not have a name and are therefore not in the VST.
Error BitcodeReader::rememberAndSkipFunctionBodies() {
  Stream.JumpToBit(NextUnreadBit);

  if (Stream.AtEndOfStream())
    return error("Could not find function in stream");

  if (!SeenFirstFunctionBody)
    return error("Trying to materialize functions before seeing function blocks");

  // An old bitcode file with the symbol table at the end would have
  // finished the parse greedily.
  assert(SeenValueSymbolTable);

  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    default:
      return error("Expect SubBlock");
    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:
        return error("Expect function block");
      case bitc::FUNCTION_BLOCK_ID:
        if (Error Err = rememberAndSkipFunctionBody())
          return Err;
        NextUnreadBit = Stream.GetCurrentBitNo();
        return Error::success();
      }
    }
  }
}

bool BitcodeReaderBase::readBlockInfo() {
  Optional<BitstreamBlockInfo> NewBlockInfo = Stream.ReadBlockInfoBlock();
  if (!NewBlockInfo)
    return true;
  BlockInfo = std::move(*NewBlockInfo);
  return false;
}

Error BitcodeReader::parseComdatRecord(ArrayRef<uint64_t> Record) {
  // v1: [selection_kind, name]
  // v2: [strtab_offset, strtab_size, selection_kind]
  StringRef Name;
  std::tie(Name, Record) = readNameFromStrtab(Record);

  if (Record.empty())
    return error("Invalid record");
  Comdat::SelectionKind SK = getDecodedComdatSelectionKind(Record[0]);
  std::string OldFormatName;
  if (!UseStrtab) {
    if (Record.size() < 2)
      return error("Invalid record");
    unsigned ComdatNameSize = Record[1];
    OldFormatName.reserve(ComdatNameSize);
    for (unsigned i = 0; i != ComdatNameSize; ++i)
      OldFormatName += (char)Record[2 + i];
    Name = OldFormatName;
  }
  Comdat *C = TheModule->getOrInsertComdat(Name);
  C->setSelectionKind(SK);
  ComdatList.push_back(C);
  return Error::success();
}

static void inferDSOLocal(GlobalValue *GV) {
  // infer dso_local from linkage and visibility if it is not encoded.
  if (GV->hasLocalLinkage() ||
      (!GV->hasDefaultVisibility() && !GV->hasExternalWeakLinkage()))
    GV->setDSOLocal(true);
}

Error BitcodeReader::parseGlobalVarRecord(ArrayRef<uint64_t> Record) {
  // v1: [pointer type, isconst, initid, linkage, alignment, section,
  // visibility, threadlocal, unnamed_addr, externally_initialized,
  // dllstorageclass, comdat, attributes, preemption specifier] (name in VST)
  // v2: [strtab_offset, strtab_size, v1]
  StringRef Name;
  std::tie(Name, Record) = readNameFromStrtab(Record);

  if (Record.size() < 6)
    return error("Invalid record");
  Type *Ty = getTypeByID(Record[0]);
  if (!Ty)
    return error("Invalid record");
  bool isConstant = Record[1] & 1;
  bool explicitType = Record[1] & 2;
  unsigned AddressSpace;
  if (explicitType) {
    AddressSpace = Record[1] >> 2;
  } else {
    if (!Ty->isPointerTy())
      return error("Invalid type for value");
    AddressSpace = cast<PointerType>(Ty)->getAddressSpace();
    Ty = cast<PointerType>(Ty)->getElementType();
  }

  uint64_t RawLinkage = Record[3];
  GlobalValue::LinkageTypes Linkage = getDecodedLinkage(RawLinkage);
  unsigned Alignment;
  if (Error Err = parseAlignmentValue(Record[4], Alignment))
    return Err;
  std::string Section;
  if (Record[5]) {
    if (Record[5] - 1 >= SectionTable.size())
      return error("Invalid ID");
    Section = SectionTable[Record[5] - 1];
  }
  GlobalValue::VisibilityTypes Visibility = GlobalValue::DefaultVisibility;
  // Local linkage must have default visibility.
  if (Record.size() > 6 && !GlobalValue::isLocalLinkage(Linkage))
    // FIXME: Change to an error if non-default in 4.0.
    Visibility = getDecodedVisibility(Record[6]);

  GlobalVariable::ThreadLocalMode TLM = GlobalVariable::NotThreadLocal;
  if (Record.size() > 7)
    TLM = getDecodedThreadLocalMode(Record[7]);

  GlobalValue::UnnamedAddr UnnamedAddr = GlobalValue::UnnamedAddr::None;
  if (Record.size() > 8)
    UnnamedAddr = getDecodedUnnamedAddrType(Record[8]);

  bool ExternallyInitialized = false;
  if (Record.size() > 9)
    ExternallyInitialized = Record[9];

  GlobalVariable *NewGV =
      new GlobalVariable(*TheModule, Ty, isConstant, Linkage, nullptr, Name,
                         nullptr, TLM, AddressSpace, ExternallyInitialized);
  NewGV->setAlignment(Alignment);
  if (!Section.empty())
    NewGV->setSection(Section);
  NewGV->setVisibility(Visibility);
  NewGV->setUnnamedAddr(UnnamedAddr);

  if (Record.size() > 10)
    NewGV->setDLLStorageClass(getDecodedDLLStorageClass(Record[10]));
  else
    upgradeDLLImportExportLinkage(NewGV, RawLinkage);

  ValueList.push_back(NewGV);

  // Remember which value to use for the global initializer.
  if (unsigned InitID = Record[2])
    GlobalInits.push_back(std::make_pair(NewGV, InitID - 1));

  if (Record.size() > 11) {
    if (unsigned ComdatID = Record[11]) {
      if (ComdatID > ComdatList.size())
        return error("Invalid global variable comdat ID");
      NewGV->setComdat(ComdatList[ComdatID - 1]);
    }
  } else if (hasImplicitComdat(RawLinkage)) {
    NewGV->setComdat(reinterpret_cast<Comdat *>(1));
  }

  if (Record.size() > 12) {
    auto AS = getAttributes(Record[12]).getFnAttributes();
    NewGV->setAttributes(AS);
  }

  if (Record.size() > 13) {
    NewGV->setDSOLocal(getDecodedDSOLocal(Record[13]));
  }
  inferDSOLocal(NewGV);

  return Error::success();
}

Error BitcodeReader::parseFunctionRecord(ArrayRef<uint64_t> Record) {
  // v1: [type, callingconv, isproto, linkage, paramattr, alignment, section,
  // visibility, gc, unnamed_addr, prologuedata, dllstorageclass, comdat,
  // prefixdata,  personalityfn, preemption specifier, addrspace] (name in VST)
  // v2: [strtab_offset, strtab_size, v1]
  StringRef Name;
  std::tie(Name, Record) = readNameFromStrtab(Record);

  if (Record.size() < 8)
    return error("Invalid record");
  Type *Ty = getTypeByID(Record[0]);
  if (!Ty)
    return error("Invalid record");
  if (auto *PTy = dyn_cast<PointerType>(Ty))
    Ty = PTy->getElementType();
  auto *FTy = dyn_cast<FunctionType>(Ty);
  if (!FTy)
    return error("Invalid type for value");
  auto CC = static_cast<CallingConv::ID>(Record[1]);
  if (CC & ~CallingConv::MaxID)
    return error("Invalid calling convention ID");

  unsigned AddrSpace = TheModule->getDataLayout().getProgramAddressSpace();
  if (Record.size() > 16)
    AddrSpace = Record[16];

  Function *Func = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                    AddrSpace, Name, TheModule);

  Func->setCallingConv(CC);
  bool isProto = Record[2];
  uint64_t RawLinkage = Record[3];
  Func->setLinkage(getDecodedLinkage(RawLinkage));
  Func->setAttributes(getAttributes(Record[4]));

  unsigned Alignment;
  if (Error Err = parseAlignmentValue(Record[5], Alignment))
    return Err;
  Func->setAlignment(Alignment);
  if (Record[6]) {
    if (Record[6] - 1 >= SectionTable.size())
      return error("Invalid ID");
    Func->setSection(SectionTable[Record[6] - 1]);
  }
  // Local linkage must have default visibility.
  if (!Func->hasLocalLinkage())
    // FIXME: Change to an error if non-default in 4.0.
    Func->setVisibility(getDecodedVisibility(Record[7]));
  if (Record.size() > 8 && Record[8]) {
    if (Record[8] - 1 >= GCTable.size())
      return error("Invalid ID");
    Func->setGC(GCTable[Record[8] - 1]);
  }
  GlobalValue::UnnamedAddr UnnamedAddr = GlobalValue::UnnamedAddr::None;
  if (Record.size() > 9)
    UnnamedAddr = getDecodedUnnamedAddrType(Record[9]);
  Func->setUnnamedAddr(UnnamedAddr);
  if (Record.size() > 10 && Record[10] != 0)
    FunctionPrologues.push_back(std::make_pair(Func, Record[10] - 1));

  if (Record.size() > 11)
    Func->setDLLStorageClass(getDecodedDLLStorageClass(Record[11]));
  else
    upgradeDLLImportExportLinkage(Func, RawLinkage);

  if (Record.size() > 12) {
    if (unsigned ComdatID = Record[12]) {
      if (ComdatID > ComdatList.size())
        return error("Invalid function comdat ID");
      Func->setComdat(ComdatList[ComdatID - 1]);
    }
  } else if (hasImplicitComdat(RawLinkage)) {
    Func->setComdat(reinterpret_cast<Comdat *>(1));
  }

  if (Record.size() > 13 && Record[13] != 0)
    FunctionPrefixes.push_back(std::make_pair(Func, Record[13] - 1));

  if (Record.size() > 14 && Record[14] != 0)
    FunctionPersonalityFns.push_back(std::make_pair(Func, Record[14] - 1));

  if (Record.size() > 15) {
    Func->setDSOLocal(getDecodedDSOLocal(Record[15]));
  }
  inferDSOLocal(Func);

  ValueList.push_back(Func);

  // If this is a function with a body, remember the prototype we are
  // creating now, so that we can match up the body with them later.
  if (!isProto) {
    Func->setIsMaterializable(true);
    FunctionsWithBodies.push_back(Func);
    DeferredFunctionInfo[Func] = 0;
  }
  return Error::success();
}

Error BitcodeReader::parseGlobalIndirectSymbolRecord(
    unsigned BitCode, ArrayRef<uint64_t> Record) {
  // v1 ALIAS_OLD: [alias type, aliasee val#, linkage] (name in VST)
  // v1 ALIAS: [alias type, addrspace, aliasee val#, linkage, visibility,
  // dllstorageclass, threadlocal, unnamed_addr,
  // preemption specifier] (name in VST)
  // v1 IFUNC: [alias type, addrspace, aliasee val#, linkage,
  // visibility, dllstorageclass, threadlocal, unnamed_addr,
  // preemption specifier] (name in VST)
  // v2: [strtab_offset, strtab_size, v1]
  StringRef Name;
  std::tie(Name, Record) = readNameFromStrtab(Record);

  bool NewRecord = BitCode != bitc::MODULE_CODE_ALIAS_OLD;
  if (Record.size() < (3 + (unsigned)NewRecord))
    return error("Invalid record");
  unsigned OpNum = 0;
  Type *Ty = getTypeByID(Record[OpNum++]);
  if (!Ty)
    return error("Invalid record");

  unsigned AddrSpace;
  if (!NewRecord) {
    auto *PTy = dyn_cast<PointerType>(Ty);
    if (!PTy)
      return error("Invalid type for value");
    Ty = PTy->getElementType();
    AddrSpace = PTy->getAddressSpace();
  } else {
    AddrSpace = Record[OpNum++];
  }

  auto Val = Record[OpNum++];
  auto Linkage = Record[OpNum++];
  GlobalIndirectSymbol *NewGA;
  if (BitCode == bitc::MODULE_CODE_ALIAS ||
      BitCode == bitc::MODULE_CODE_ALIAS_OLD)
    NewGA = GlobalAlias::create(Ty, AddrSpace, getDecodedLinkage(Linkage), Name,
                                TheModule);
  else
    NewGA = GlobalIFunc::create(Ty, AddrSpace, getDecodedLinkage(Linkage), Name,
                                nullptr, TheModule);
  // Old bitcode files didn't have visibility field.
  // Local linkage must have default visibility.
  if (OpNum != Record.size()) {
    auto VisInd = OpNum++;
    if (!NewGA->hasLocalLinkage())
      // FIXME: Change to an error if non-default in 4.0.
      NewGA->setVisibility(getDecodedVisibility(Record[VisInd]));
  }
  if (BitCode == bitc::MODULE_CODE_ALIAS ||
      BitCode == bitc::MODULE_CODE_ALIAS_OLD) {
    if (OpNum != Record.size())
      NewGA->setDLLStorageClass(getDecodedDLLStorageClass(Record[OpNum++]));
    else
      upgradeDLLImportExportLinkage(NewGA, Linkage);
    if (OpNum != Record.size())
      NewGA->setThreadLocalMode(getDecodedThreadLocalMode(Record[OpNum++]));
    if (OpNum != Record.size())
      NewGA->setUnnamedAddr(getDecodedUnnamedAddrType(Record[OpNum++]));
  }
  if (OpNum != Record.size())
    NewGA->setDSOLocal(getDecodedDSOLocal(Record[OpNum++]));
  inferDSOLocal(NewGA);

  ValueList.push_back(NewGA);
  IndirectSymbolInits.push_back(std::make_pair(NewGA, Val));
  return Error::success();
}

Error BitcodeReader::parseModule(uint64_t ResumeBit,
                                 bool ShouldLazyLoadMetadata) {
  if (ResumeBit)
    Stream.JumpToBit(ResumeBit);
  else if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this module.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return globalCleanup();

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:  // Skip unknown content.
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::BLOCKINFO_BLOCK_ID:
        if (readBlockInfo())
          return error("Malformed block");
        break;
      case bitc::PARAMATTR_BLOCK_ID:
        if (Error Err = parseAttributeBlock())
          return Err;
        break;
      case bitc::PARAMATTR_GROUP_BLOCK_ID:
        if (Error Err = parseAttributeGroupBlock())
          return Err;
        break;
      case bitc::TYPE_BLOCK_ID_NEW:
        if (Error Err = parseTypeTable())
          return Err;
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        if (!SeenValueSymbolTable) {
          // Either this is an old form VST without function index and an
          // associated VST forward declaration record (which would have caused
          // the VST to be jumped to and parsed before it was encountered
          // normally in the stream), or there were no function blocks to
          // trigger an earlier parsing of the VST.
          assert(VSTOffset == 0 || FunctionsWithBodies.empty());
          if (Error Err = parseValueSymbolTable())
            return Err;
          SeenValueSymbolTable = true;
        } else {
          // We must have had a VST forward declaration record, which caused
          // the parser to jump to and parse the VST earlier.
          assert(VSTOffset > 0);
          if (Stream.SkipBlock())
            return error("Invalid record");
        }
        break;
      case bitc::CONSTANTS_BLOCK_ID:
        if (Error Err = parseConstants())
          return Err;
        if (Error Err = resolveGlobalAndIndirectSymbolInits())
          return Err;
        break;
      case bitc::METADATA_BLOCK_ID:
        if (ShouldLazyLoadMetadata) {
          if (Error Err = rememberAndSkipMetadata())
            return Err;
          break;
        }
        assert(DeferredMetadataInfo.empty() && "Unexpected deferred metadata");
        if (Error Err = MDLoader->parseModuleMetadata())
          return Err;
        break;
      case bitc::METADATA_KIND_BLOCK_ID:
        if (Error Err = MDLoader->parseMetadataKinds())
          return Err;
        break;
      case bitc::FUNCTION_BLOCK_ID:
        // If this is the first function body we've seen, reverse the
        // FunctionsWithBodies list.
        if (!SeenFirstFunctionBody) {
          std::reverse(FunctionsWithBodies.begin(), FunctionsWithBodies.end());
          if (Error Err = globalCleanup())
            return Err;
          SeenFirstFunctionBody = true;
        }

        if (VSTOffset > 0) {
          // If we have a VST forward declaration record, make sure we
          // parse the VST now if we haven't already. It is needed to
          // set up the DeferredFunctionInfo vector for lazy reading.
          if (!SeenValueSymbolTable) {
            if (Error Err = BitcodeReader::parseValueSymbolTable(VSTOffset))
              return Err;
            SeenValueSymbolTable = true;
            // Fall through so that we record the NextUnreadBit below.
            // This is necessary in case we have an anonymous function that
            // is later materialized. Since it will not have a VST entry we
            // need to fall back to the lazy parse to find its offset.
          } else {
            // If we have a VST forward declaration record, but have already
            // parsed the VST (just above, when the first function body was
            // encountered here), then we are resuming the parse after
            // materializing functions. The ResumeBit points to the
            // start of the last function block recorded in the
            // DeferredFunctionInfo map. Skip it.
            if (Stream.SkipBlock())
              return error("Invalid record");
            continue;
          }
        }

        // Support older bitcode files that did not have the function
        // index in the VST, nor a VST forward declaration record, as
        // well as anonymous functions that do not have VST entries.
        // Build the DeferredFunctionInfo vector on the fly.
        if (Error Err = rememberAndSkipFunctionBody())
          return Err;

        // Suspend parsing when we reach the function bodies. Subsequent
        // materialization calls will resume it when necessary. If the bitcode
        // file is old, the symbol table will be at the end instead and will not
        // have been seen yet. In this case, just finish the parse now.
        if (SeenValueSymbolTable) {
          NextUnreadBit = Stream.GetCurrentBitNo();
          // After the VST has been parsed, we need to make sure intrinsic name
          // are auto-upgraded.
          return globalCleanup();
        }
        break;
      case bitc::USELIST_BLOCK_ID:
        if (Error Err = parseUseLists())
          return Err;
        break;
      case bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID:
        if (Error Err = parseOperandBundleTags())
          return Err;
        break;
      case bitc::SYNC_SCOPE_NAMES_BLOCK_ID:
        if (Error Err = parseSyncScopeNames())
          return Err;
        break;
      }
      continue;

    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    auto BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: break;  // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_VERSION: {
      Expected<unsigned> VersionOrErr = parseVersionRecord(Record);
      if (!VersionOrErr)
        return VersionOrErr.takeError();
      UseRelativeIDs = *VersionOrErr >= 1;
      break;
    }
    case bitc::MODULE_CODE_TRIPLE: {  // TRIPLE: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      TheModule->setTargetTriple(S);
      break;
    }
    case bitc::MODULE_CODE_DATALAYOUT: {  // DATALAYOUT: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      TheModule->setDataLayout(S);
      break;
    }
    case bitc::MODULE_CODE_ASM: {  // ASM: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      TheModule->setModuleInlineAsm(S);
      break;
    }
    case bitc::MODULE_CODE_DEPLIB: {  // DEPLIB: [strchr x N]
      // FIXME: Remove in 4.0.
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      // Ignore value.
      break;
    }
    case bitc::MODULE_CODE_SECTIONNAME: {  // SECTIONNAME: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      SectionTable.push_back(S);
      break;
    }
    case bitc::MODULE_CODE_GCNAME: {  // SECTIONNAME: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      GCTable.push_back(S);
      break;
    }
    case bitc::MODULE_CODE_COMDAT:
      if (Error Err = parseComdatRecord(Record))
        return Err;
      break;
    case bitc::MODULE_CODE_GLOBALVAR:
      if (Error Err = parseGlobalVarRecord(Record))
        return Err;
      break;
    case bitc::MODULE_CODE_FUNCTION:
      if (Error Err = parseFunctionRecord(Record))
        return Err;
      break;
    case bitc::MODULE_CODE_IFUNC:
    case bitc::MODULE_CODE_ALIAS:
    case bitc::MODULE_CODE_ALIAS_OLD:
      if (Error Err = parseGlobalIndirectSymbolRecord(BitCode, Record))
        return Err;
      break;
    /// MODULE_CODE_VSTOFFSET: [offset]
    case bitc::MODULE_CODE_VSTOFFSET:
      if (Record.size() < 1)
        return error("Invalid record");
      // Note that we subtract 1 here because the offset is relative to one word
      // before the start of the identification or module block, which was
      // historically always the start of the regular bitcode header.
      VSTOffset = Record[0] - 1;
      break;
    /// MODULE_CODE_SOURCE_FILENAME: [namechar x N]
    case bitc::MODULE_CODE_SOURCE_FILENAME:
      SmallString<128> ValueName;
      if (convertToString(Record, 0, ValueName))
        return error("Invalid record");
      TheModule->setSourceFileName(ValueName);
      break;
    }
    Record.clear();
  }
}

Error BitcodeReader::parseBitcodeInto(Module *M, bool ShouldLazyLoadMetadata,
                                      bool IsImporting) {
  TheModule = M;
  MDLoader = MetadataLoader(Stream, *M, ValueList, IsImporting,
                            [&](unsigned ID) { return getTypeByID(ID); });
  return parseModule(0, ShouldLazyLoadMetadata);
}

Error BitcodeReader::typeCheckLoadStoreInst(Type *ValType, Type *PtrType) {
  if (!isa<PointerType>(PtrType))
    return error("Load/Store operand is not a pointer type");
  Type *ElemType = cast<PointerType>(PtrType)->getElementType();

  if (ValType && ValType != ElemType)
    return error("Explicit load/store type does not match pointee "
                 "type of pointer operand");
  if (!PointerType::isLoadableOrStorableType(ElemType))
    return error("Cannot load/store from pointer");
  return Error::success();
}

/// Lazily parse the specified function body block.
Error BitcodeReader::parseFunctionBody(Function *F) {
  if (Stream.EnterSubBlock(bitc::FUNCTION_BLOCK_ID))
    return error("Invalid record");

  // Unexpected unresolved metadata when parsing function.
  if (MDLoader->hasFwdRefs())
    return error("Invalid function metadata: incoming forward references");

  InstructionList.clear();
  unsigned ModuleValueListSize = ValueList.size();
  unsigned ModuleMDLoaderSize = MDLoader->size();

  // Add all the function arguments to the value table.
  for (Argument &I : F->args())
    ValueList.push_back(&I);

  unsigned NextValueNo = ValueList.size();
  BasicBlock *CurBB = nullptr;
  unsigned CurBBNo = 0;

  DebugLoc LastLoc;
  auto getLastInstruction = [&]() -> Instruction * {
    if (CurBB && !CurBB->empty())
      return &CurBB->back();
    else if (CurBBNo && FunctionBBs[CurBBNo - 1] &&
             !FunctionBBs[CurBBNo - 1]->empty())
      return &FunctionBBs[CurBBNo - 1]->back();
    return nullptr;
  };

  std::vector<OperandBundleDef> OperandBundles;

  // Read all the records.
  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      goto OutOfRecordLoop;

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:  // Skip unknown content.
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::CONSTANTS_BLOCK_ID:
        if (Error Err = parseConstants())
          return Err;
        NextValueNo = ValueList.size();
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        if (Error Err = parseValueSymbolTable())
          return Err;
        break;
      case bitc::METADATA_ATTACHMENT_ID:
        if (Error Err = MDLoader->parseMetadataAttachment(*F, InstructionList))
          return Err;
        break;
      case bitc::METADATA_BLOCK_ID:
        assert(DeferredMetadataInfo.empty() &&
               "Must read all module-level metadata before function-level");
        if (Error Err = MDLoader->parseFunctionMetadata())
          return Err;
        break;
      case bitc::USELIST_BLOCK_ID:
        if (Error Err = parseUseLists())
          return Err;
        break;
      }
      continue;

    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Instruction *I = nullptr;
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: reject
      return error("Invalid value");
    case bitc::FUNC_CODE_DECLAREBLOCKS: {   // DECLAREBLOCKS: [nblocks]
      if (Record.size() < 1 || Record[0] == 0)
        return error("Invalid record");
      // Create all the basic blocks for the function.
      FunctionBBs.resize(Record[0]);

      // See if anything took the address of blocks in this function.
      auto BBFRI = BasicBlockFwdRefs.find(F);
      if (BBFRI == BasicBlockFwdRefs.end()) {
        for (unsigned i = 0, e = FunctionBBs.size(); i != e; ++i)
          FunctionBBs[i] = BasicBlock::Create(Context, "", F);
      } else {
        auto &BBRefs = BBFRI->second;
        // Check for invalid basic block references.
        if (BBRefs.size() > FunctionBBs.size())
          return error("Invalid ID");
        assert(!BBRefs.empty() && "Unexpected empty array");
        assert(!BBRefs.front() && "Invalid reference to entry block");
        for (unsigned I = 0, E = FunctionBBs.size(), RE = BBRefs.size(); I != E;
             ++I)
          if (I < RE && BBRefs[I]) {
            BBRefs[I]->insertInto(F);
            FunctionBBs[I] = BBRefs[I];
          } else {
            FunctionBBs[I] = BasicBlock::Create(Context, "", F);
          }

        // Erase from the table.
        BasicBlockFwdRefs.erase(BBFRI);
      }

      CurBB = FunctionBBs[0];
      continue;
    }

    case bitc::FUNC_CODE_DEBUG_LOC_AGAIN:  // DEBUG_LOC_AGAIN
      // This record indicates that the last instruction is at the same
      // location as the previous instruction with a location.
      I = getLastInstruction();

      if (!I)
        return error("Invalid record");
      I->setDebugLoc(LastLoc);
      I = nullptr;
      continue;

    case bitc::FUNC_CODE_DEBUG_LOC: {      // DEBUG_LOC: [line, col, scope, ia]
      I = getLastInstruction();
      if (!I || Record.size() < 4)
        return error("Invalid record");

      unsigned Line = Record[0], Col = Record[1];
      unsigned ScopeID = Record[2], IAID = Record[3];
      bool isImplicitCode = Record.size() == 5 && Record[4];

      MDNode *Scope = nullptr, *IA = nullptr;
      if (ScopeID) {
        Scope = dyn_cast_or_null<MDNode>(
            MDLoader->getMetadataFwdRefOrLoad(ScopeID - 1));
        if (!Scope)
          return error("Invalid record");
      }
      if (IAID) {
        IA = dyn_cast_or_null<MDNode>(
            MDLoader->getMetadataFwdRefOrLoad(IAID - 1));
        if (!IA)
          return error("Invalid record");
      }
      LastLoc = DebugLoc::get(Line, Col, Scope, IA, isImplicitCode);
      I->setDebugLoc(LastLoc);
      I = nullptr;
      continue;
    }
    case bitc::FUNC_CODE_INST_UNOP: {    // UNOP: [opval, ty, opcode]
      unsigned OpNum = 0;
      Value *LHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          OpNum+1 > Record.size())
        return error("Invalid record");

      int Opc = getDecodedUnaryOpcode(Record[OpNum++], LHS->getType());
      if (Opc == -1)
        return error("Invalid record");
      I = UnaryOperator::Create((Instruction::UnaryOps)Opc, LHS);
      InstructionList.push_back(I);
      if (OpNum < Record.size()) {
        if (isa<FPMathOperator>(I)) {
          FastMathFlags FMF = getDecodedFastMathFlags(Record[OpNum]);
          if (FMF.any())
            I->setFastMathFlags(FMF);
        }
      }
      break;
    }
    case bitc::FUNC_CODE_INST_BINOP: {    // BINOP: [opval, ty, opval, opcode]
      unsigned OpNum = 0;
      Value *LHS, *RHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          popValue(Record, OpNum, NextValueNo, LHS->getType(), RHS) ||
          OpNum+1 > Record.size())
        return error("Invalid record");

      int Opc = getDecodedBinaryOpcode(Record[OpNum++], LHS->getType());
      if (Opc == -1)
        return error("Invalid record");
      I = BinaryOperator::Create((Instruction::BinaryOps)Opc, LHS, RHS);
      InstructionList.push_back(I);
      if (OpNum < Record.size()) {
        if (Opc == Instruction::Add ||
            Opc == Instruction::Sub ||
            Opc == Instruction::Mul ||
            Opc == Instruction::Shl) {
          if (Record[OpNum] & (1 << bitc::OBO_NO_SIGNED_WRAP))
            cast<BinaryOperator>(I)->setHasNoSignedWrap(true);
          if (Record[OpNum] & (1 << bitc::OBO_NO_UNSIGNED_WRAP))
            cast<BinaryOperator>(I)->setHasNoUnsignedWrap(true);
        } else if (Opc == Instruction::SDiv ||
                   Opc == Instruction::UDiv ||
                   Opc == Instruction::LShr ||
                   Opc == Instruction::AShr) {
          if (Record[OpNum] & (1 << bitc::PEO_EXACT))
            cast<BinaryOperator>(I)->setIsExact(true);
        } else if (isa<FPMathOperator>(I)) {
          FastMathFlags FMF = getDecodedFastMathFlags(Record[OpNum]);
          if (FMF.any())
            I->setFastMathFlags(FMF);
        }

      }
      break;
    }
    case bitc::FUNC_CODE_INST_CAST: {    // CAST: [opval, opty, destty, castopc]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          OpNum+2 != Record.size())
        return error("Invalid record");

      Type *ResTy = getTypeByID(Record[OpNum]);
      int Opc = getDecodedCastOpcode(Record[OpNum + 1]);
      if (Opc == -1 || !ResTy)
        return error("Invalid record");
      Instruction *Temp = nullptr;
      if ((I = UpgradeBitCastInst(Opc, Op, ResTy, Temp))) {
        if (Temp) {
          InstructionList.push_back(Temp);
          CurBB->getInstList().push_back(Temp);
        }
      } else {
        auto CastOp = (Instruction::CastOps)Opc;
        if (!CastInst::castIsValid(CastOp, Op, ResTy))
          return error("Invalid cast");
        I = CastInst::Create(CastOp, Op, ResTy);
      }
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_INBOUNDS_GEP_OLD:
    case bitc::FUNC_CODE_INST_GEP_OLD:
    case bitc::FUNC_CODE_INST_GEP: { // GEP: type, [n x operands]
      unsigned OpNum = 0;

      Type *Ty;
      bool InBounds;

      if (BitCode == bitc::FUNC_CODE_INST_GEP) {
        InBounds = Record[OpNum++];
        Ty = getTypeByID(Record[OpNum++]);
      } else {
        InBounds = BitCode == bitc::FUNC_CODE_INST_INBOUNDS_GEP_OLD;
        Ty = nullptr;
      }

      Value *BasePtr;
      if (getValueTypePair(Record, OpNum, NextValueNo, BasePtr))
        return error("Invalid record");

      if (!Ty)
        Ty = cast<PointerType>(BasePtr->getType()->getScalarType())
                 ->getElementType();
      else if (Ty !=
               cast<PointerType>(BasePtr->getType()->getScalarType())
                   ->getElementType())
        return error(
            "Explicit gep type does not match pointee type of pointer operand");

      SmallVector<Value*, 16> GEPIdx;
      while (OpNum != Record.size()) {
        Value *Op;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return error("Invalid record");
        GEPIdx.push_back(Op);
      }

      I = GetElementPtrInst::Create(Ty, BasePtr, GEPIdx);

      InstructionList.push_back(I);
      if (InBounds)
        cast<GetElementPtrInst>(I)->setIsInBounds(true);
      break;
    }

    case bitc::FUNC_CODE_INST_EXTRACTVAL: {
                                       // EXTRACTVAL: [opty, opval, n x indices]
      unsigned OpNum = 0;
      Value *Agg;
      if (getValueTypePair(Record, OpNum, NextValueNo, Agg))
        return error("Invalid record");

      unsigned RecSize = Record.size();
      if (OpNum == RecSize)
        return error("EXTRACTVAL: Invalid instruction with 0 indices");

      SmallVector<unsigned, 4> EXTRACTVALIdx;
      Type *CurTy = Agg->getType();
      for (; OpNum != RecSize; ++OpNum) {
        bool IsArray = CurTy->isArrayTy();
        bool IsStruct = CurTy->isStructTy();
        uint64_t Index = Record[OpNum];

        if (!IsStruct && !IsArray)
          return error("EXTRACTVAL: Invalid type");
        if ((unsigned)Index != Index)
          return error("Invalid value");
        if (IsStruct && Index >= CurTy->getStructNumElements())
          return error("EXTRACTVAL: Invalid struct index");
        if (IsArray && Index >= CurTy->getArrayNumElements())
          return error("EXTRACTVAL: Invalid array index");
        EXTRACTVALIdx.push_back((unsigned)Index);

        if (IsStruct)
          CurTy = CurTy->getStructElementType(Index);
        else
          CurTy = CurTy->getArrayElementType();
      }

      I = ExtractValueInst::Create(Agg, EXTRACTVALIdx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_INSERTVAL: {
                           // INSERTVAL: [opty, opval, opty, opval, n x indices]
      unsigned OpNum = 0;
      Value *Agg;
      if (getValueTypePair(Record, OpNum, NextValueNo, Agg))
        return error("Invalid record");
      Value *Val;
      if (getValueTypePair(Record, OpNum, NextValueNo, Val))
        return error("Invalid record");

      unsigned RecSize = Record.size();
      if (OpNum == RecSize)
        return error("INSERTVAL: Invalid instruction with 0 indices");

      SmallVector<unsigned, 4> INSERTVALIdx;
      Type *CurTy = Agg->getType();
      for (; OpNum != RecSize; ++OpNum) {
        bool IsArray = CurTy->isArrayTy();
        bool IsStruct = CurTy->isStructTy();
        uint64_t Index = Record[OpNum];

        if (!IsStruct && !IsArray)
          return error("INSERTVAL: Invalid type");
        if ((unsigned)Index != Index)
          return error("Invalid value");
        if (IsStruct && Index >= CurTy->getStructNumElements())
          return error("INSERTVAL: Invalid struct index");
        if (IsArray && Index >= CurTy->getArrayNumElements())
          return error("INSERTVAL: Invalid array index");

        INSERTVALIdx.push_back((unsigned)Index);
        if (IsStruct)
          CurTy = CurTy->getStructElementType(Index);
        else
          CurTy = CurTy->getArrayElementType();
      }

      if (CurTy != Val->getType())
        return error("Inserted value type doesn't match aggregate type");

      I = InsertValueInst::Create(Agg, Val, INSERTVALIdx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_SELECT: { // SELECT: [opval, ty, opval, opval]
      // obsolete form of select
      // handles select i1 ... in old bitcode
      unsigned OpNum = 0;
      Value *TrueVal, *FalseVal, *Cond;
      if (getValueTypePair(Record, OpNum, NextValueNo, TrueVal) ||
          popValue(Record, OpNum, NextValueNo, TrueVal->getType(), FalseVal) ||
          popValue(Record, OpNum, NextValueNo, Type::getInt1Ty(Context), Cond))
        return error("Invalid record");

      I = SelectInst::Create(Cond, TrueVal, FalseVal);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_VSELECT: {// VSELECT: [ty,opval,opval,predty,pred]
      // new form of select
      // handles select i1 or select [N x i1]
      unsigned OpNum = 0;
      Value *TrueVal, *FalseVal, *Cond;
      if (getValueTypePair(Record, OpNum, NextValueNo, TrueVal) ||
          popValue(Record, OpNum, NextValueNo, TrueVal->getType(), FalseVal) ||
          getValueTypePair(Record, OpNum, NextValueNo, Cond))
        return error("Invalid record");

      // select condition can be either i1 or [N x i1]
      if (VectorType* vector_type =
          dyn_cast<VectorType>(Cond->getType())) {
        // expect <n x i1>
        if (vector_type->getElementType() != Type::getInt1Ty(Context))
          return error("Invalid type for value");
      } else {
        // expect i1
        if (Cond->getType() != Type::getInt1Ty(Context))
          return error("Invalid type for value");
      }

      I = SelectInst::Create(Cond, TrueVal, FalseVal);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_EXTRACTELT: { // EXTRACTELT: [opty, opval, opval]
      unsigned OpNum = 0;
      Value *Vec, *Idx;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec) ||
          getValueTypePair(Record, OpNum, NextValueNo, Idx))
        return error("Invalid record");
      if (!Vec->getType()->isVectorTy())
        return error("Invalid type for value");
      I = ExtractElementInst::Create(Vec, Idx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_INSERTELT: { // INSERTELT: [ty, opval,opval,opval]
      unsigned OpNum = 0;
      Value *Vec, *Elt, *Idx;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec))
        return error("Invalid record");
      if (!Vec->getType()->isVectorTy())
        return error("Invalid type for value");
      if (popValue(Record, OpNum, NextValueNo,
                   cast<VectorType>(Vec->getType())->getElementType(), Elt) ||
          getValueTypePair(Record, OpNum, NextValueNo, Idx))
        return error("Invalid record");
      I = InsertElementInst::Create(Vec, Elt, Idx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_SHUFFLEVEC: {// SHUFFLEVEC: [opval,ty,opval,opval]
      unsigned OpNum = 0;
      Value *Vec1, *Vec2, *Mask;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec1) ||
          popValue(Record, OpNum, NextValueNo, Vec1->getType(), Vec2))
        return error("Invalid record");

      if (getValueTypePair(Record, OpNum, NextValueNo, Mask))
        return error("Invalid record");
      if (!Vec1->getType()->isVectorTy() || !Vec2->getType()->isVectorTy())
        return error("Invalid type for value");
      I = new ShuffleVectorInst(Vec1, Vec2, Mask);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_CMP:   // CMP: [opty, opval, opval, pred]
      // Old form of ICmp/FCmp returning bool
      // Existed to differentiate between icmp/fcmp and vicmp/vfcmp which were
      // both legal on vectors but had different behaviour.
    case bitc::FUNC_CODE_INST_CMP2: { // CMP2: [opty, opval, opval, pred]
      // FCmp/ICmp returning bool or vector of bool

      unsigned OpNum = 0;
      Value *LHS, *RHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          popValue(Record, OpNum, NextValueNo, LHS->getType(), RHS))
        return error("Invalid record");

      unsigned PredVal = Record[OpNum];
      bool IsFP = LHS->getType()->isFPOrFPVectorTy();
      FastMathFlags FMF;
      if (IsFP && Record.size() > OpNum+1)
        FMF = getDecodedFastMathFlags(Record[++OpNum]);

      if (OpNum+1 != Record.size())
        return error("Invalid record");

      if (LHS->getType()->isFPOrFPVectorTy())
        I = new FCmpInst((FCmpInst::Predicate)PredVal, LHS, RHS);
      else
        I = new ICmpInst((ICmpInst::Predicate)PredVal, LHS, RHS);

      if (FMF.any())
        I->setFastMathFlags(FMF);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_RET: // RET: [opty,opval<optional>]
      {
        unsigned Size = Record.size();
        if (Size == 0) {
          I = ReturnInst::Create(Context);
          InstructionList.push_back(I);
          break;
        }

        unsigned OpNum = 0;
        Value *Op = nullptr;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return error("Invalid record");
        if (OpNum != Record.size())
          return error("Invalid record");

        I = ReturnInst::Create(Context, Op);
        InstructionList.push_back(I);
        break;
      }
    case bitc::FUNC_CODE_INST_BR: { // BR: [bb#, bb#, opval] or [bb#]
      if (Record.size() != 1 && Record.size() != 3)
        return error("Invalid record");
      BasicBlock *TrueDest = getBasicBlock(Record[0]);
      if (!TrueDest)
        return error("Invalid record");

      if (Record.size() == 1) {
        I = BranchInst::Create(TrueDest);
        InstructionList.push_back(I);
      }
      else {
        BasicBlock *FalseDest = getBasicBlock(Record[1]);
        Value *Cond = getValue(Record, 2, NextValueNo,
                               Type::getInt1Ty(Context));
        if (!FalseDest || !Cond)
          return error("Invalid record");
        I = BranchInst::Create(TrueDest, FalseDest, Cond);
        InstructionList.push_back(I);
      }
      break;
    }
    case bitc::FUNC_CODE_INST_CLEANUPRET: { // CLEANUPRET: [val] or [val,bb#]
      if (Record.size() != 1 && Record.size() != 2)
        return error("Invalid record");
      unsigned Idx = 0;
      Value *CleanupPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));
      if (!CleanupPad)
        return error("Invalid record");
      BasicBlock *UnwindDest = nullptr;
      if (Record.size() == 2) {
        UnwindDest = getBasicBlock(Record[Idx++]);
        if (!UnwindDest)
          return error("Invalid record");
      }

      I = CleanupReturnInst::Create(CleanupPad, UnwindDest);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CATCHRET: { // CATCHRET: [val,bb#]
      if (Record.size() != 2)
        return error("Invalid record");
      unsigned Idx = 0;
      Value *CatchPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));
      if (!CatchPad)
        return error("Invalid record");
      BasicBlock *BB = getBasicBlock(Record[Idx++]);
      if (!BB)
        return error("Invalid record");

      I = CatchReturnInst::Create(CatchPad, BB);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CATCHSWITCH: { // CATCHSWITCH: [tok,num,(bb)*,bb?]
      // We must have, at minimum, the outer scope and the number of arguments.
      if (Record.size() < 2)
        return error("Invalid record");

      unsigned Idx = 0;

      Value *ParentPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));

      unsigned NumHandlers = Record[Idx++];

      SmallVector<BasicBlock *, 2> Handlers;
      for (unsigned Op = 0; Op != NumHandlers; ++Op) {
        BasicBlock *BB = getBasicBlock(Record[Idx++]);
        if (!BB)
          return error("Invalid record");
        Handlers.push_back(BB);
      }

      BasicBlock *UnwindDest = nullptr;
      if (Idx + 1 == Record.size()) {
        UnwindDest = getBasicBlock(Record[Idx++]);
        if (!UnwindDest)
          return error("Invalid record");
      }

      if (Record.size() != Idx)
        return error("Invalid record");

      auto *CatchSwitch =
          CatchSwitchInst::Create(ParentPad, UnwindDest, NumHandlers);
      for (BasicBlock *Handler : Handlers)
        CatchSwitch->addHandler(Handler);
      I = CatchSwitch;
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CATCHPAD:
    case bitc::FUNC_CODE_INST_CLEANUPPAD: { // [tok,num,(ty,val)*]
      // We must have, at minimum, the outer scope and the number of arguments.
      if (Record.size() < 2)
        return error("Invalid record");

      unsigned Idx = 0;

      Value *ParentPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));

      unsigned NumArgOperands = Record[Idx++];

      SmallVector<Value *, 2> Args;
      for (unsigned Op = 0; Op != NumArgOperands; ++Op) {
        Value *Val;
        if (getValueTypePair(Record, Idx, NextValueNo, Val))
          return error("Invalid record");
        Args.push_back(Val);
      }

      if (Record.size() != Idx)
        return error("Invalid record");

      if (BitCode == bitc::FUNC_CODE_INST_CLEANUPPAD)
        I = CleanupPadInst::Create(ParentPad, Args);
      else
        I = CatchPadInst::Create(ParentPad, Args);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_SWITCH: { // SWITCH: [opty, op0, op1, ...]
      // Check magic
      if ((Record[0] >> 16) == SWITCH_INST_MAGIC) {
        // "New" SwitchInst format with case ranges. The changes to write this
        // format were reverted but we still recognize bitcode that uses it.
        // Hopefully someday we will have support for case ranges and can use
        // this format again.

        Type *OpTy = getTypeByID(Record[1]);
        unsigned ValueBitWidth = cast<IntegerType>(OpTy)->getBitWidth();

        Value *Cond = getValue(Record, 2, NextValueNo, OpTy);
        BasicBlock *Default = getBasicBlock(Record[3]);
        if (!OpTy || !Cond || !Default)
          return error("Invalid record");

        unsigned NumCases = Record[4];

        SwitchInst *SI = SwitchInst::Create(Cond, Default, NumCases);
        InstructionList.push_back(SI);

        unsigned CurIdx = 5;
        for (unsigned i = 0; i != NumCases; ++i) {
          SmallVector<ConstantInt*, 1> CaseVals;
          unsigned NumItems = Record[CurIdx++];
          for (unsigned ci = 0; ci != NumItems; ++ci) {
            bool isSingleNumber = Record[CurIdx++];

            APInt Low;
            unsigned ActiveWords = 1;
            if (ValueBitWidth > 64)
              ActiveWords = Record[CurIdx++];
            Low = readWideAPInt(makeArrayRef(&Record[CurIdx], ActiveWords),
                                ValueBitWidth);
            CurIdx += ActiveWords;

            if (!isSingleNumber) {
              ActiveWords = 1;
              if (ValueBitWidth > 64)
                ActiveWords = Record[CurIdx++];
              APInt High = readWideAPInt(
                  makeArrayRef(&Record[CurIdx], ActiveWords), ValueBitWidth);
              CurIdx += ActiveWords;

              // FIXME: It is not clear whether values in the range should be
              // compared as signed or unsigned values. The partially
              // implemented changes that used this format in the past used
              // unsigned comparisons.
              for ( ; Low.ule(High); ++Low)
                CaseVals.push_back(ConstantInt::get(Context, Low));
            } else
              CaseVals.push_back(ConstantInt::get(Context, Low));
          }
          BasicBlock *DestBB = getBasicBlock(Record[CurIdx++]);
          for (SmallVector<ConstantInt*, 1>::iterator cvi = CaseVals.begin(),
                 cve = CaseVals.end(); cvi != cve; ++cvi)
            SI->addCase(*cvi, DestBB);
        }
        I = SI;
        break;
      }

      // Old SwitchInst format without case ranges.

      if (Record.size() < 3 || (Record.size() & 1) == 0)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Cond = getValue(Record, 1, NextValueNo, OpTy);
      BasicBlock *Default = getBasicBlock(Record[2]);
      if (!OpTy || !Cond || !Default)
        return error("Invalid record");
      unsigned NumCases = (Record.size()-3)/2;
      SwitchInst *SI = SwitchInst::Create(Cond, Default, NumCases);
      InstructionList.push_back(SI);
      for (unsigned i = 0, e = NumCases; i != e; ++i) {
        ConstantInt *CaseVal =
          dyn_cast_or_null<ConstantInt>(getFnValueByID(Record[3+i*2], OpTy));
        BasicBlock *DestBB = getBasicBlock(Record[1+3+i*2]);
        if (!CaseVal || !DestBB) {
          delete SI;
          return error("Invalid record");
        }
        SI->addCase(CaseVal, DestBB);
      }
      I = SI;
      break;
    }
    case bitc::FUNC_CODE_INST_INDIRECTBR: { // INDIRECTBR: [opty, op0, op1, ...]
      if (Record.size() < 2)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Address = getValue(Record, 1, NextValueNo, OpTy);
      if (!OpTy || !Address)
        return error("Invalid record");
      unsigned NumDests = Record.size()-2;
      IndirectBrInst *IBI = IndirectBrInst::Create(Address, NumDests);
      InstructionList.push_back(IBI);
      for (unsigned i = 0, e = NumDests; i != e; ++i) {
        if (BasicBlock *DestBB = getBasicBlock(Record[2+i])) {
          IBI->addDestination(DestBB);
        } else {
          delete IBI;
          return error("Invalid record");
        }
      }
      I = IBI;
      break;
    }

    case bitc::FUNC_CODE_INST_INVOKE: {
      // INVOKE: [attrs, cc, normBB, unwindBB, fnty, op0,op1,op2, ...]
      if (Record.size() < 4)
        return error("Invalid record");
      unsigned OpNum = 0;
      AttributeList PAL = getAttributes(Record[OpNum++]);
      unsigned CCInfo = Record[OpNum++];
      BasicBlock *NormalBB = getBasicBlock(Record[OpNum++]);
      BasicBlock *UnwindBB = getBasicBlock(Record[OpNum++]);

      FunctionType *FTy = nullptr;
      if (CCInfo >> 13 & 1 &&
          !(FTy = dyn_cast<FunctionType>(getTypeByID(Record[OpNum++]))))
        return error("Explicit invoke type is not a function type");

      Value *Callee;
      if (getValueTypePair(Record, OpNum, NextValueNo, Callee))
        return error("Invalid record");

      PointerType *CalleeTy = dyn_cast<PointerType>(Callee->getType());
      if (!CalleeTy)
        return error("Callee is not a pointer");
      if (!FTy) {
        FTy = dyn_cast<FunctionType>(CalleeTy->getElementType());
        if (!FTy)
          return error("Callee is not of pointer to function type");
      } else if (CalleeTy->getElementType() != FTy)
        return error("Explicit invoke type does not match pointee type of "
                     "callee operand");
      if (Record.size() < FTy->getNumParams() + OpNum)
        return error("Insufficient operands to call");

      SmallVector<Value*, 16> Ops;
      for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i, ++OpNum) {
        Ops.push_back(getValue(Record, OpNum, NextValueNo,
                               FTy->getParamType(i)));
        if (!Ops.back())
          return error("Invalid record");
      }

      if (!FTy->isVarArg()) {
        if (Record.size() != OpNum)
          return error("Invalid record");
      } else {
        // Read type/value pairs for varargs params.
        while (OpNum != Record.size()) {
          Value *Op;
          if (getValueTypePair(Record, OpNum, NextValueNo, Op))
            return error("Invalid record");
          Ops.push_back(Op);
        }
      }

      I = InvokeInst::Create(Callee, NormalBB, UnwindBB, Ops, OperandBundles);
      OperandBundles.clear();
      InstructionList.push_back(I);
      cast<InvokeInst>(I)->setCallingConv(
          static_cast<CallingConv::ID>(CallingConv::MaxID & CCInfo));
      cast<InvokeInst>(I)->setAttributes(PAL);
      break;
    }
    case bitc::FUNC_CODE_INST_RESUME: { // RESUME: [opval]
      unsigned Idx = 0;
      Value *Val = nullptr;
      if (getValueTypePair(Record, Idx, NextValueNo, Val))
        return error("Invalid record");
      I = ResumeInst::Create(Val);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_UNREACHABLE: // UNREACHABLE
      I = new UnreachableInst(Context);
      InstructionList.push_back(I);
      break;
    case bitc::FUNC_CODE_INST_PHI: { // PHI: [ty, val0,bb0, ...]
      if (Record.size() < 1 || ((Record.size()-1)&1))
        return error("Invalid record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty)
        return error("Invalid record");

      PHINode *PN = PHINode::Create(Ty, (Record.size()-1)/2);
      InstructionList.push_back(PN);

      for (unsigned i = 0, e = Record.size()-1; i != e; i += 2) {
        Value *V;
        // With the new function encoding, it is possible that operands have
        // negative IDs (for forward references).  Use a signed VBR
        // representation to keep the encoding small.
        if (UseRelativeIDs)
          V = getValueSigned(Record, 1+i, NextValueNo, Ty);
        else
          V = getValue(Record, 1+i, NextValueNo, Ty);
        BasicBlock *BB = getBasicBlock(Record[2+i]);
        if (!V || !BB)
          return error("Invalid record");
        PN->addIncoming(V, BB);
      }
      I = PN;
      break;
    }

    case bitc::FUNC_CODE_INST_LANDINGPAD:
    case bitc::FUNC_CODE_INST_LANDINGPAD_OLD: {
      // LANDINGPAD: [ty, val, val, num, (id0,val0 ...)?]
      unsigned Idx = 0;
      if (BitCode == bitc::FUNC_CODE_INST_LANDINGPAD) {
        if (Record.size() < 3)
          return error("Invalid record");
      } else {
        assert(BitCode == bitc::FUNC_CODE_INST_LANDINGPAD_OLD);
        if (Record.size() < 4)
          return error("Invalid record");
      }
      Type *Ty = getTypeByID(Record[Idx++]);
      if (!Ty)
        return error("Invalid record");
      if (BitCode == bitc::FUNC_CODE_INST_LANDINGPAD_OLD) {
        Value *PersFn = nullptr;
        if (getValueTypePair(Record, Idx, NextValueNo, PersFn))
          return error("Invalid record");

        if (!F->hasPersonalityFn())
          F->setPersonalityFn(cast<Constant>(PersFn));
        else if (F->getPersonalityFn() != cast<Constant>(PersFn))
          return error("Personality function mismatch");
      }

      bool IsCleanup = !!Record[Idx++];
      unsigned NumClauses = Record[Idx++];
      LandingPadInst *LP = LandingPadInst::Create(Ty, NumClauses);
      LP->setCleanup(IsCleanup);
      for (unsigned J = 0; J != NumClauses; ++J) {
        LandingPadInst::ClauseType CT =
          LandingPadInst::ClauseType(Record[Idx++]); (void)CT;
        Value *Val;

        if (getValueTypePair(Record, Idx, NextValueNo, Val)) {
          delete LP;
          return error("Invalid record");
        }

        assert((CT != LandingPadInst::Catch ||
                !isa<ArrayType>(Val->getType())) &&
               "Catch clause has a invalid type!");
        assert((CT != LandingPadInst::Filter ||
                isa<ArrayType>(Val->getType())) &&
               "Filter clause has invalid type!");
        LP->addClause(cast<Constant>(Val));
      }

      I = LP;
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_ALLOCA: { // ALLOCA: [instty, opty, op, align]
      if (Record.size() != 4)
        return error("Invalid record");
      uint64_t AlignRecord = Record[3];
      const uint64_t InAllocaMask = uint64_t(1) << 5;
      const uint64_t ExplicitTypeMask = uint64_t(1) << 6;
      const uint64_t SwiftErrorMask = uint64_t(1) << 7;
      const uint64_t FlagMask = InAllocaMask | ExplicitTypeMask |
                                SwiftErrorMask;
      bool InAlloca = AlignRecord & InAllocaMask;
      bool SwiftError = AlignRecord & SwiftErrorMask;
      Type *Ty = getTypeByID(Record[0]);
      if ((AlignRecord & ExplicitTypeMask) == 0) {
        auto *PTy = dyn_cast_or_null<PointerType>(Ty);
        if (!PTy)
          return error("Old-style alloca with a non-pointer type");
        Ty = PTy->getElementType();
      }
      Type *OpTy = getTypeByID(Record[1]);
      Value *Size = getFnValueByID(Record[2], OpTy);
      unsigned Align;
      if (Error Err = parseAlignmentValue(AlignRecord & ~FlagMask, Align)) {
        return Err;
      }
      if (!Ty || !Size)
        return error("Invalid record");

      // FIXME: Make this an optional field.
      const DataLayout &DL = TheModule->getDataLayout();
      unsigned AS = DL.getAllocaAddrSpace();

      AllocaInst *AI = new AllocaInst(Ty, AS, Size, Align);
      AI->setUsedWithInAlloca(InAlloca);
      AI->setSwiftError(SwiftError);
      I = AI;
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_LOAD: { // LOAD: [opty, op, align, vol]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          (OpNum + 2 != Record.size() && OpNum + 3 != Record.size()))
        return error("Invalid record");

      Type *Ty = nullptr;
      if (OpNum + 3 == Record.size())
        Ty = getTypeByID(Record[OpNum++]);
      if (Error Err = typeCheckLoadStoreInst(Ty, Op->getType()))
        return Err;
      if (!Ty)
        Ty = cast<PointerType>(Op->getType())->getElementType();

      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new LoadInst(Ty, Op, "", Record[OpNum + 1], Align);

      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_LOADATOMIC: {
       // LOADATOMIC: [opty, op, align, vol, ordering, ssid]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          (OpNum + 4 != Record.size() && OpNum + 5 != Record.size()))
        return error("Invalid record");

      Type *Ty = nullptr;
      if (OpNum + 5 == Record.size())
        Ty = getTypeByID(Record[OpNum++]);
      if (Error Err = typeCheckLoadStoreInst(Ty, Op->getType()))
        return Err;
      if (!Ty)
        Ty = cast<PointerType>(Op->getType())->getElementType();

      AtomicOrdering Ordering = getDecodedOrdering(Record[OpNum + 2]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Release ||
          Ordering == AtomicOrdering::AcquireRelease)
        return error("Invalid record");
      if (Ordering != AtomicOrdering::NotAtomic && Record[OpNum] == 0)
        return error("Invalid record");
      SyncScope::ID SSID = getDecodedSyncScopeID(Record[OpNum + 3]);

      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new LoadInst(Op, "", Record[OpNum+1], Align, Ordering, SSID);

      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_STORE:
    case bitc::FUNC_CODE_INST_STORE_OLD: { // STORE2:[ptrty, ptr, val, align, vol]
      unsigned OpNum = 0;
      Value *Val, *Ptr;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          (BitCode == bitc::FUNC_CODE_INST_STORE
               ? getValueTypePair(Record, OpNum, NextValueNo, Val)
               : popValue(Record, OpNum, NextValueNo,
                          cast<PointerType>(Ptr->getType())->getElementType(),
                          Val)) ||
          OpNum + 2 != Record.size())
        return error("Invalid record");

      if (Error Err = typeCheckLoadStoreInst(Val->getType(), Ptr->getType()))
        return Err;
      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new StoreInst(Val, Ptr, Record[OpNum+1], Align);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_STOREATOMIC:
    case bitc::FUNC_CODE_INST_STOREATOMIC_OLD: {
      // STOREATOMIC: [ptrty, ptr, val, align, vol, ordering, ssid]
      unsigned OpNum = 0;
      Value *Val, *Ptr;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          !isa<PointerType>(Ptr->getType()) ||
          (BitCode == bitc::FUNC_CODE_INST_STOREATOMIC
               ? getValueTypePair(Record, OpNum, NextValueNo, Val)
               : popValue(Record, OpNum, NextValueNo,
                          cast<PointerType>(Ptr->getType())->getElementType(),
                          Val)) ||
          OpNum + 4 != Record.size())
        return error("Invalid record");

      if (Error Err = typeCheckLoadStoreInst(Val->getType(), Ptr->getType()))
        return Err;
      AtomicOrdering Ordering = getDecodedOrdering(Record[OpNum + 2]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Acquire ||
          Ordering == AtomicOrdering::AcquireRelease)
        return error("Invalid record");
      SyncScope::ID SSID = getDecodedSyncScopeID(Record[OpNum + 3]);
      if (Ordering != AtomicOrdering::NotAtomic && Record[OpNum] == 0)
        return error("Invalid record");

      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new StoreInst(Val, Ptr, Record[OpNum+1], Align, Ordering, SSID);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CMPXCHG_OLD:
    case bitc::FUNC_CODE_INST_CMPXCHG: {
      // CMPXCHG:[ptrty, ptr, cmp, new, vol, successordering, ssid,
      //          failureordering?, isweak?]
      unsigned OpNum = 0;
      Value *Ptr, *Cmp, *New;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          (BitCode == bitc::FUNC_CODE_INST_CMPXCHG
               ? getValueTypePair(Record, OpNum, NextValueNo, Cmp)
               : popValue(Record, OpNum, NextValueNo,
                          cast<PointerType>(Ptr->getType())->getElementType(),
                          Cmp)) ||
          popValue(Record, OpNum, NextValueNo, Cmp->getType(), New) ||
          Record.size() < OpNum + 3 || Record.size() > OpNum + 5)
        return error("Invalid record");
      AtomicOrdering SuccessOrdering = getDecodedOrdering(Record[OpNum + 1]);
      if (SuccessOrdering == AtomicOrdering::NotAtomic ||
          SuccessOrdering == AtomicOrdering::Unordered)
        return error("Invalid record");
      SyncScope::ID SSID = getDecodedSyncScopeID(Record[OpNum + 2]);

      if (Error Err = typeCheckLoadStoreInst(Cmp->getType(), Ptr->getType()))
        return Err;
      AtomicOrdering FailureOrdering;
      if (Record.size() < 7)
        FailureOrdering =
            AtomicCmpXchgInst::getStrongestFailureOrdering(SuccessOrdering);
      else
        FailureOrdering = getDecodedOrdering(Record[OpNum + 3]);

      I = new AtomicCmpXchgInst(Ptr, Cmp, New, SuccessOrdering, FailureOrdering,
                                SSID);
      cast<AtomicCmpXchgInst>(I)->setVolatile(Record[OpNum]);

      if (Record.size() < 8) {
        // Before weak cmpxchgs existed, the instruction simply returned the
        // value loaded from memory, so bitcode files from that era will be
        // expecting the first component of a modern cmpxchg.
        CurBB->getInstList().push_back(I);
        I = ExtractValueInst::Create(I, 0);
      } else {
        cast<AtomicCmpXchgInst>(I)->setWeak(Record[OpNum+4]);
      }

      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_ATOMICRMW: {
      // ATOMICRMW:[ptrty, ptr, val, op, vol, ordering, ssid]
      unsigned OpNum = 0;
      Value *Ptr, *Val;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          !isa<PointerType>(Ptr->getType()) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), Val) ||
          OpNum+4 != Record.size())
        return error("Invalid record");
      AtomicRMWInst::BinOp Operation = getDecodedRMWOperation(Record[OpNum]);
      if (Operation < AtomicRMWInst::FIRST_BINOP ||
          Operation > AtomicRMWInst::LAST_BINOP)
        return error("Invalid record");
      AtomicOrdering Ordering = getDecodedOrdering(Record[OpNum + 2]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Unordered)
        return error("Invalid record");
      SyncScope::ID SSID = getDecodedSyncScopeID(Record[OpNum + 3]);
      I = new AtomicRMWInst(Operation, Ptr, Val, Ordering, SSID);
      cast<AtomicRMWInst>(I)->setVolatile(Record[OpNum+1]);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_FENCE: { // FENCE:[ordering, ssid]
      if (2 != Record.size())
        return error("Invalid record");
      AtomicOrdering Ordering = getDecodedOrdering(Record[0]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Unordered ||
          Ordering == AtomicOrdering::Monotonic)
        return error("Invalid record");
      SyncScope::ID SSID = getDecodedSyncScopeID(Record[1]);
      I = new FenceInst(Context, Ordering, SSID);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CALL: {
      // CALL: [paramattrs, cc, fmf, fnty, fnid, arg0, arg1...]
      if (Record.size() < 3)
        return error("Invalid record");

      unsigned OpNum = 0;
      AttributeList PAL = getAttributes(Record[OpNum++]);
      unsigned CCInfo = Record[OpNum++];

      FastMathFlags FMF;
      if ((CCInfo >> bitc::CALL_FMF) & 1) {
        FMF = getDecodedFastMathFlags(Record[OpNum++]);
        if (!FMF.any())
          return error("Fast math flags indicator set for call with no FMF");
      }

      FunctionType *FTy = nullptr;
      if (CCInfo >> bitc::CALL_EXPLICIT_TYPE & 1 &&
          !(FTy = dyn_cast<FunctionType>(getTypeByID(Record[OpNum++]))))
        return error("Explicit call type is not a function type");

      Value *Callee;
      if (getValueTypePair(Record, OpNum, NextValueNo, Callee))
        return error("Invalid record");

      PointerType *OpTy = dyn_cast<PointerType>(Callee->getType());
      if (!OpTy)
        return error("Callee is not a pointer type");
      if (!FTy) {
        FTy = dyn_cast<FunctionType>(OpTy->getElementType());
        if (!FTy)
          return error("Callee is not of pointer to function type");
      } else if (OpTy->getElementType() != FTy)
        return error("Explicit call type does not match pointee type of "
                     "callee operand");
      if (Record.size() < FTy->getNumParams() + OpNum)
        return error("Insufficient operands to call");

      SmallVector<Value*, 16> Args;
      // Read the fixed params.
      for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i, ++OpNum) {
        if (FTy->getParamType(i)->isLabelTy())
          Args.push_back(getBasicBlock(Record[OpNum]));
        else
          Args.push_back(getValue(Record, OpNum, NextValueNo,
                                  FTy->getParamType(i)));
        if (!Args.back())
          return error("Invalid record");
      }

      // Read type/value pairs for varargs params.
      if (!FTy->isVarArg()) {
        if (OpNum != Record.size())
          return error("Invalid record");
      } else {
        while (OpNum != Record.size()) {
          Value *Op;
          if (getValueTypePair(Record, OpNum, NextValueNo, Op))
            return error("Invalid record");
          Args.push_back(Op);
        }
      }

      I = CallInst::Create(FTy, Callee, Args, OperandBundles);
      OperandBundles.clear();
      InstructionList.push_back(I);
      cast<CallInst>(I)->setCallingConv(
          static_cast<CallingConv::ID>((0x7ff & CCInfo) >> bitc::CALL_CCONV));
      CallInst::TailCallKind TCK = CallInst::TCK_None;
      if (CCInfo & 1 << bitc::CALL_TAIL)
        TCK = CallInst::TCK_Tail;
      if (CCInfo & (1 << bitc::CALL_MUSTTAIL))
        TCK = CallInst::TCK_MustTail;
      if (CCInfo & (1 << bitc::CALL_NOTAIL))
        TCK = CallInst::TCK_NoTail;
      cast<CallInst>(I)->setTailCallKind(TCK);
      cast<CallInst>(I)->setAttributes(PAL);
      if (FMF.any()) {
        if (!isa<FPMathOperator>(I))
          return error("Fast-math-flags specified for call without "
                       "floating-point scalar or vector return type");
        I->setFastMathFlags(FMF);
      }
      break;
    }
    case bitc::FUNC_CODE_INST_VAARG: { // VAARG: [valistty, valist, instty]
      if (Record.size() < 3)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Op = getValue(Record, 1, NextValueNo, OpTy);
      Type *ResTy = getTypeByID(Record[2]);
      if (!OpTy || !Op || !ResTy)
        return error("Invalid record");
      I = new VAArgInst(Op, ResTy);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_OPERAND_BUNDLE: {
      // A call or an invoke can be optionally prefixed with some variable
      // number of operand bundle blocks.  These blocks are read into
      // OperandBundles and consumed at the next call or invoke instruction.

      if (Record.size() < 1 || Record[0] >= BundleTags.size())
        return error("Invalid record");

      std::vector<Value *> Inputs;

      unsigned OpNum = 1;
      while (OpNum != Record.size()) {
        Value *Op;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return error("Invalid record");
        Inputs.push_back(Op);
      }

      OperandBundles.emplace_back(BundleTags[Record[0]], std::move(Inputs));
      continue;
    }
    }

    // Add instruction to end of current BB.  If there is no current BB, reject
    // this file.
    if (!CurBB) {
      I->deleteValue();
      return error("Invalid instruction with no BB");
    }
    if (!OperandBundles.empty()) {
      I->deleteValue();
      return error("Operand bundles found with no consumer");
    }
    CurBB->getInstList().push_back(I);

    // If this was a terminator instruction, move to the next block.
    if (I->isTerminator()) {
      ++CurBBNo;
      CurBB = CurBBNo < FunctionBBs.size() ? FunctionBBs[CurBBNo] : nullptr;
    }

    // Non-void values get registered in the value table for future use.
    if (I && !I->getType()->isVoidTy())
      ValueList.assignValue(I, NextValueNo++);
  }

OutOfRecordLoop:

  if (!OperandBundles.empty())
    return error("Operand bundles found with no consumer");

  // Check the function list for unresolved values.
  if (Argument *A = dyn_cast<Argument>(ValueList.back())) {
    if (!A->getParent()) {
      // We found at least one unresolved value.  Nuke them all to avoid leaks.
      for (unsigned i = ModuleValueListSize, e = ValueList.size(); i != e; ++i){
        if ((A = dyn_cast_or_null<Argument>(ValueList[i])) && !A->getParent()) {
          A->replaceAllUsesWith(UndefValue::get(A->getType()));
          delete A;
        }
      }
      return error("Never resolved value found in function");
    }
  }

  // Unexpected unresolved metadata about to be dropped.
  if (MDLoader->hasFwdRefs())
    return error("Invalid function metadata: outgoing forward refs");

  // Trim the value list down to the size it was before we parsed this function.
  ValueList.shrinkTo(ModuleValueListSize);
  MDLoader->shrinkTo(ModuleMDLoaderSize);
  std::vector<BasicBlock*>().swap(FunctionBBs);
  return Error::success();
}

/// Find the function body in the bitcode stream
Error BitcodeReader::findFunctionInStream(
    Function *F,
    DenseMap<Function *, uint64_t>::iterator DeferredFunctionInfoIterator) {
  while (DeferredFunctionInfoIterator->second == 0) {
    // This is the fallback handling for the old format bitcode that
    // didn't contain the function index in the VST, or when we have
    // an anonymous function which would not have a VST entry.
    // Assert that we have one of those two cases.
    assert(VSTOffset == 0 || !F->hasName());
    // Parse the next body in the stream and set its position in the
    // DeferredFunctionInfo map.
    if (Error Err = rememberAndSkipFunctionBodies())
      return Err;
  }
  return Error::success();
}

SyncScope::ID BitcodeReader::getDecodedSyncScopeID(unsigned Val) {
  if (Val == SyncScope::SingleThread || Val == SyncScope::System)
    return SyncScope::ID(Val);
  if (Val >= SSIDs.size())
    return SyncScope::System; // Map unknown synchronization scopes to system.
  return SSIDs[Val];
}

//===----------------------------------------------------------------------===//
// GVMaterializer implementation
//===----------------------------------------------------------------------===//

Error BitcodeReader::materialize(GlobalValue *GV) {
  Function *F = dyn_cast<Function>(GV);
  // If it's not a function or is already material, ignore the request.
  if (!F || !F->isMaterializable())
    return Error::success();

  DenseMap<Function*, uint64_t>::iterator DFII = DeferredFunctionInfo.find(F);
  assert(DFII != DeferredFunctionInfo.end() && "Deferred function not found!");
  // If its position is recorded as 0, its body is somewhere in the stream
  // but we haven't seen it yet.
  if (DFII->second == 0)
    if (Error Err = findFunctionInStream(F, DFII))
      return Err;

  // Materialize metadata before parsing any function bodies.
  if (Error Err = materializeMetadata())
    return Err;

  // Move the bit stream to the saved position of the deferred function body.
  Stream.JumpToBit(DFII->second);

  if (Error Err = parseFunctionBody(F))
    return Err;
  F->setIsMaterializable(false);

  if (StripDebugInfo)
    stripDebugInfo(*F);

  // Upgrade any old intrinsic calls in the function.
  for (auto &I : UpgradedIntrinsics) {
    for (auto UI = I.first->materialized_user_begin(), UE = I.first->user_end();
         UI != UE;) {
      User *U = *UI;
      ++UI;
      if (CallInst *CI = dyn_cast<CallInst>(U))
        UpgradeIntrinsicCall(CI, I.second);
    }
  }

  // Update calls to the remangled intrinsics
  for (auto &I : RemangledIntrinsics)
    for (auto UI = I.first->materialized_user_begin(), UE = I.first->user_end();
         UI != UE;)
      // Don't expect any other users than call sites
      CallSite(*UI++).setCalledFunction(I.second);

  // Finish fn->subprogram upgrade for materialized functions.
  if (DISubprogram *SP = MDLoader->lookupSubprogramForFunction(F))
    F->setSubprogram(SP);

  // Check if the TBAA Metadata are valid, otherwise we will need to strip them.
  if (!MDLoader->isStrippingTBAA()) {
    for (auto &I : instructions(F)) {
      MDNode *TBAA = I.getMetadata(LLVMContext::MD_tbaa);
      if (!TBAA || TBAAVerifyHelper.visitTBAAMetadata(I, TBAA))
        continue;
      MDLoader->setStripTBAA(true);
      stripTBAA(F->getParent());
    }
  }

  // Bring in any functions that this function forward-referenced via
  // blockaddresses.
  return materializeForwardReferencedFunctions();
}

Error BitcodeReader::materializeModule() {
  if (Error Err = materializeMetadata())
    return Err;

  // Promise to materialize all forward references.
  WillMaterializeAllForwardRefs = true;

  // Iterate over the module, deserializing any functions that are still on
  // disk.
  for (Function &F : *TheModule) {
    if (Error Err = materialize(&F))
      return Err;
  }
  // At this point, if there are any function bodies, parse the rest of
  // the bits in the module past the last function block we have recorded
  // through either lazy scanning or the VST.
  if (LastFunctionBlockBit || NextUnreadBit)
    if (Error Err = parseModule(LastFunctionBlockBit > NextUnreadBit
                                    ? LastFunctionBlockBit
                                    : NextUnreadBit))
      return Err;

  // Check that all block address forward references got resolved (as we
  // promised above).
  if (!BasicBlockFwdRefs.empty())
    return error("Never resolved function from blockaddress");

  // Upgrade any intrinsic calls that slipped through (should not happen!) and
  // delete the old functions to clean up. We can't do this unless the entire
  // module is materialized because there could always be another function body
  // with calls to the old function.
  for (auto &I : UpgradedIntrinsics) {
    for (auto *U : I.first->users()) {
      if (CallInst *CI = dyn_cast<CallInst>(U))
        UpgradeIntrinsicCall(CI, I.second);
    }
    if (!I.first->use_empty())
      I.first->replaceAllUsesWith(I.second);
    I.first->eraseFromParent();
  }
  UpgradedIntrinsics.clear();
  // Do the same for remangled intrinsics
  for (auto &I : RemangledIntrinsics) {
    I.first->replaceAllUsesWith(I.second);
    I.first->eraseFromParent();
  }
  RemangledIntrinsics.clear();

  UpgradeDebugInfo(*TheModule);

  UpgradeModuleFlags(*TheModule);

  UpgradeRetainReleaseMarker(*TheModule);

  return Error::success();
}

std::vector<StructType *> BitcodeReader::getIdentifiedStructTypes() const {
  return IdentifiedStructTypes;
}

ModuleSummaryIndexBitcodeReader::ModuleSummaryIndexBitcodeReader(
    BitstreamCursor Cursor, StringRef Strtab, ModuleSummaryIndex &TheIndex,
    StringRef ModulePath, unsigned ModuleId)
    : BitcodeReaderBase(std::move(Cursor), Strtab), TheIndex(TheIndex),
      ModulePath(ModulePath), ModuleId(ModuleId) {}

void ModuleSummaryIndexBitcodeReader::addThisModule() {
  TheIndex.addModule(ModulePath, ModuleId);
}

ModuleSummaryIndex::ModuleInfo *
ModuleSummaryIndexBitcodeReader::getThisModule() {
  return TheIndex.getModule(ModulePath);
}

std::pair<ValueInfo, GlobalValue::GUID>
ModuleSummaryIndexBitcodeReader::getValueInfoFromValueId(unsigned ValueId) {
  auto VGI = ValueIdToValueInfoMap[ValueId];
  assert(VGI.first);
  return VGI;
}

void ModuleSummaryIndexBitcodeReader::setValueGUID(
    uint64_t ValueID, StringRef ValueName, GlobalValue::LinkageTypes Linkage,
    StringRef SourceFileName) {
  std::string GlobalId =
      GlobalValue::getGlobalIdentifier(ValueName, Linkage, SourceFileName);
  auto ValueGUID = GlobalValue::getGUID(GlobalId);
  auto OriginalNameID = ValueGUID;
  if (GlobalValue::isLocalLinkage(Linkage))
    OriginalNameID = GlobalValue::getGUID(ValueName);
  if (PrintSummaryGUIDs)
    dbgs() << "GUID " << ValueGUID << "(" << OriginalNameID << ") is "
           << ValueName << "\n";

  // UseStrtab is false for legacy summary formats and value names are
  // created on stack. In that case we save the name in a string saver in
  // the index so that the value name can be recorded.
  ValueIdToValueInfoMap[ValueID] = std::make_pair(
      TheIndex.getOrInsertValueInfo(
          ValueGUID,
          UseStrtab ? ValueName : TheIndex.saveString(ValueName)),
      OriginalNameID);
}

// Specialized value symbol table parser used when reading module index
// blocks where we don't actually create global values. The parsed information
// is saved in the bitcode reader for use when later parsing summaries.
Error ModuleSummaryIndexBitcodeReader::parseValueSymbolTable(
    uint64_t Offset,
    DenseMap<unsigned, GlobalValue::LinkageTypes> &ValueIdToLinkageMap) {
  // With a strtab the VST is not required to parse the summary.
  if (UseStrtab)
    return Error::success();

  assert(Offset > 0 && "Expected non-zero VST offset");
  uint64_t CurrentBit = jumpToValueSymbolTable(Offset, Stream);

  if (Stream.EnterSubBlock(bitc::VALUE_SYMTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this value table.
  SmallString<128> ValueName;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      // Done parsing VST, jump back to wherever we came from.
      Stream.JumpToBit(CurrentBit);
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: // Default behavior: ignore (e.g. VST_CODE_BBENTRY records).
      break;
    case bitc::VST_CODE_ENTRY: { // VST_CODE_ENTRY: [valueid, namechar x N]
      if (convertToString(Record, 1, ValueName))
        return error("Invalid record");
      unsigned ValueID = Record[0];
      assert(!SourceFileName.empty());
      auto VLI = ValueIdToLinkageMap.find(ValueID);
      assert(VLI != ValueIdToLinkageMap.end() &&
             "No linkage found for VST entry?");
      auto Linkage = VLI->second;
      setValueGUID(ValueID, ValueName, Linkage, SourceFileName);
      ValueName.clear();
      break;
    }
    case bitc::VST_CODE_FNENTRY: {
      // VST_CODE_FNENTRY: [valueid, offset, namechar x N]
      if (convertToString(Record, 2, ValueName))
        return error("Invalid record");
      unsigned ValueID = Record[0];
      assert(!SourceFileName.empty());
      auto VLI = ValueIdToLinkageMap.find(ValueID);
      assert(VLI != ValueIdToLinkageMap.end() &&
             "No linkage found for VST entry?");
      auto Linkage = VLI->second;
      setValueGUID(ValueID, ValueName, Linkage, SourceFileName);
      ValueName.clear();
      break;
    }
    case bitc::VST_CODE_COMBINED_ENTRY: {
      // VST_CODE_COMBINED_ENTRY: [valueid, refguid]
      unsigned ValueID = Record[0];
      GlobalValue::GUID RefGUID = Record[1];
      // The "original name", which is the second value of the pair will be
      // overriden later by a FS_COMBINED_ORIGINAL_NAME in the combined index.
      ValueIdToValueInfoMap[ValueID] =
          std::make_pair(TheIndex.getOrInsertValueInfo(RefGUID), RefGUID);
      break;
    }
    }
  }
}

// Parse just the blocks needed for building the index out of the module.
// At the end of this routine the module Index is populated with a map
// from global value id to GlobalValueSummary objects.
Error ModuleSummaryIndexBitcodeReader::parseModule() {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  DenseMap<unsigned, GlobalValue::LinkageTypes> ValueIdToLinkageMap;
  unsigned ValueId = 0;

  // Read the index for this module.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default: // Skip unknown content.
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::BLOCKINFO_BLOCK_ID:
        // Need to parse these to get abbrev ids (e.g. for VST)
        if (readBlockInfo())
          return error("Malformed block");
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        // Should have been parsed earlier via VSTOffset, unless there
        // is no summary section.
        assert(((SeenValueSymbolTable && VSTOffset > 0) ||
                !SeenGlobalValSummary) &&
               "Expected early VST parse via VSTOffset record");
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::GLOBALVAL_SUMMARY_BLOCK_ID:
      case bitc::FULL_LTO_GLOBALVAL_SUMMARY_BLOCK_ID:
        // Add the module if it is a per-module index (has a source file name).
        if (!SourceFileName.empty())
          addThisModule();
        assert(!SeenValueSymbolTable &&
               "Already read VST when parsing summary block?");
        // We might not have a VST if there were no values in the
        // summary. An empty summary block generated when we are
        // performing ThinLTO compiles so we don't later invoke
        // the regular LTO process on them.
        if (VSTOffset > 0) {
          if (Error Err = parseValueSymbolTable(VSTOffset, ValueIdToLinkageMap))
            return Err;
          SeenValueSymbolTable = true;
        }
        SeenGlobalValSummary = true;
        if (Error Err = parseEntireSummary(Entry.ID))
          return Err;
        break;
      case bitc::MODULE_STRTAB_BLOCK_ID:
        if (Error Err = parseModuleStringTable())
          return Err;
        break;
      }
      continue;

    case BitstreamEntry::Record: {
        Record.clear();
        auto BitCode = Stream.readRecord(Entry.ID, Record);
        switch (BitCode) {
        default:
          break; // Default behavior, ignore unknown content.
        case bitc::MODULE_CODE_VERSION: {
          if (Error Err = parseVersionRecord(Record).takeError())
            return Err;
          break;
        }
        /// MODULE_CODE_SOURCE_FILENAME: [namechar x N]
        case bitc::MODULE_CODE_SOURCE_FILENAME: {
          SmallString<128> ValueName;
          if (convertToString(Record, 0, ValueName))
            return error("Invalid record");
          SourceFileName = ValueName.c_str();
          break;
        }
        /// MODULE_CODE_HASH: [5*i32]
        case bitc::MODULE_CODE_HASH: {
          if (Record.size() != 5)
            return error("Invalid hash length " + Twine(Record.size()).str());
          auto &Hash = getThisModule()->second.second;
          int Pos = 0;
          for (auto &Val : Record) {
            assert(!(Val >> 32) && "Unexpected high bits set");
            Hash[Pos++] = Val;
          }
          break;
        }
        /// MODULE_CODE_VSTOFFSET: [offset]
        case bitc::MODULE_CODE_VSTOFFSET:
          if (Record.size() < 1)
            return error("Invalid record");
          // Note that we subtract 1 here because the offset is relative to one
          // word before the start of the identification or module block, which
          // was historically always the start of the regular bitcode header.
          VSTOffset = Record[0] - 1;
          break;
        // v1 GLOBALVAR: [pointer type, isconst,     initid,       linkage, ...]
        // v1 FUNCTION:  [type,         callingconv, isproto,      linkage, ...]
        // v1 ALIAS:     [alias type,   addrspace,   aliasee val#, linkage, ...]
        // v2: [strtab offset, strtab size, v1]
        case bitc::MODULE_CODE_GLOBALVAR:
        case bitc::MODULE_CODE_FUNCTION:
        case bitc::MODULE_CODE_ALIAS: {
          StringRef Name;
          ArrayRef<uint64_t> GVRecord;
          std::tie(Name, GVRecord) = readNameFromStrtab(Record);
          if (GVRecord.size() <= 3)
            return error("Invalid record");
          uint64_t RawLinkage = GVRecord[3];
          GlobalValue::LinkageTypes Linkage = getDecodedLinkage(RawLinkage);
          if (!UseStrtab) {
            ValueIdToLinkageMap[ValueId++] = Linkage;
            break;
          }

          setValueGUID(ValueId++, Name, Linkage, SourceFileName);
          break;
        }
        }
      }
      continue;
    }
  }
}

std::vector<ValueInfo>
ModuleSummaryIndexBitcodeReader::makeRefList(ArrayRef<uint64_t> Record) {
  std::vector<ValueInfo> Ret;
  Ret.reserve(Record.size());
  for (uint64_t RefValueId : Record)
    Ret.push_back(getValueInfoFromValueId(RefValueId).first);
  return Ret;
}

std::vector<FunctionSummary::EdgeTy>
ModuleSummaryIndexBitcodeReader::makeCallList(ArrayRef<uint64_t> Record,
                                              bool IsOldProfileFormat,
                                              bool HasProfile, bool HasRelBF) {
  std::vector<FunctionSummary::EdgeTy> Ret;
  Ret.reserve(Record.size());
  for (unsigned I = 0, E = Record.size(); I != E; ++I) {
    CalleeInfo::HotnessType Hotness = CalleeInfo::HotnessType::Unknown;
    uint64_t RelBF = 0;
    ValueInfo Callee = getValueInfoFromValueId(Record[I]).first;
    if (IsOldProfileFormat) {
      I += 1; // Skip old callsitecount field
      if (HasProfile)
        I += 1; // Skip old profilecount field
    } else if (HasProfile)
      Hotness = static_cast<CalleeInfo::HotnessType>(Record[++I]);
    else if (HasRelBF)
      RelBF = Record[++I];
    Ret.push_back(FunctionSummary::EdgeTy{Callee, CalleeInfo(Hotness, RelBF)});
  }
  return Ret;
}

static void
parseWholeProgramDevirtResolutionByArg(ArrayRef<uint64_t> Record, size_t &Slot,
                                       WholeProgramDevirtResolution &Wpd) {
  uint64_t ArgNum = Record[Slot++];
  WholeProgramDevirtResolution::ByArg &B =
      Wpd.ResByArg[{Record.begin() + Slot, Record.begin() + Slot + ArgNum}];
  Slot += ArgNum;

  B.TheKind =
      static_cast<WholeProgramDevirtResolution::ByArg::Kind>(Record[Slot++]);
  B.Info = Record[Slot++];
  B.Byte = Record[Slot++];
  B.Bit = Record[Slot++];
}

static void parseWholeProgramDevirtResolution(ArrayRef<uint64_t> Record,
                                              StringRef Strtab, size_t &Slot,
                                              TypeIdSummary &TypeId) {
  uint64_t Id = Record[Slot++];
  WholeProgramDevirtResolution &Wpd = TypeId.WPDRes[Id];

  Wpd.TheKind = static_cast<WholeProgramDevirtResolution::Kind>(Record[Slot++]);
  Wpd.SingleImplName = {Strtab.data() + Record[Slot],
                        static_cast<size_t>(Record[Slot + 1])};
  Slot += 2;

  uint64_t ResByArgNum = Record[Slot++];
  for (uint64_t I = 0; I != ResByArgNum; ++I)
    parseWholeProgramDevirtResolutionByArg(Record, Slot, Wpd);
}

static void parseTypeIdSummaryRecord(ArrayRef<uint64_t> Record,
                                     StringRef Strtab,
                                     ModuleSummaryIndex &TheIndex) {
  size_t Slot = 0;
  TypeIdSummary &TypeId = TheIndex.getOrInsertTypeIdSummary(
      {Strtab.data() + Record[Slot], static_cast<size_t>(Record[Slot + 1])});
  Slot += 2;

  TypeId.TTRes.TheKind = static_cast<TypeTestResolution::Kind>(Record[Slot++]);
  TypeId.TTRes.SizeM1BitWidth = Record[Slot++];
  TypeId.TTRes.AlignLog2 = Record[Slot++];
  TypeId.TTRes.SizeM1 = Record[Slot++];
  TypeId.TTRes.BitMask = Record[Slot++];
  TypeId.TTRes.InlineBits = Record[Slot++];

  while (Slot < Record.size())
    parseWholeProgramDevirtResolution(Record, Strtab, Slot, TypeId);
}

static void setImmutableRefs(std::vector<ValueInfo> &Refs, unsigned Count) {
  // Read-only refs are in the end of the refs list.
  for (unsigned RefNo = Refs.size() - Count; RefNo < Refs.size(); ++RefNo)
    Refs[RefNo].setReadOnly();
}

// Eagerly parse the entire summary block. This populates the GlobalValueSummary
// objects in the index.
Error ModuleSummaryIndexBitcodeReader::parseEntireSummary(unsigned ID) {
  if (Stream.EnterSubBlock(ID))
    return error("Invalid record");
  SmallVector<uint64_t, 64> Record;

  // Parse version
  {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();
    if (Entry.Kind != BitstreamEntry::Record)
      return error("Invalid Summary Block: record for version expected");
    if (Stream.readRecord(Entry.ID, Record) != bitc::FS_VERSION)
      return error("Invalid Summary Block: version expected");
  }
  const uint64_t Version = Record[0];
  const bool IsOldProfileFormat = Version == 1;
  if (Version < 1 || Version > 6)
    return error("Invalid summary version " + Twine(Version) +
                 ". Version should be in the range [1-6].");
  Record.clear();

  // Keep around the last seen summary to be used when we see an optional
  // "OriginalName" attachement.
  GlobalValueSummary *LastSeenSummary = nullptr;
  GlobalValue::GUID LastSeenGUID = 0;

  // We can expect to see any number of type ID information records before
  // each function summary records; these variables store the information
  // collected so far so that it can be used to create the summary object.
  std::vector<GlobalValue::GUID> PendingTypeTests;
  std::vector<FunctionSummary::VFuncId> PendingTypeTestAssumeVCalls,
      PendingTypeCheckedLoadVCalls;
  std::vector<FunctionSummary::ConstVCall> PendingTypeTestAssumeConstVCalls,
      PendingTypeCheckedLoadConstVCalls;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record. The record format depends on whether this
    // is a per-module index or a combined index file. In the per-module
    // case the records contain the associated value's ID for correlation
    // with VST entries. In the combined index the correlation is done
    // via the bitcode offset of the summary records (which were saved
    // in the combined index VST entries). The records also contain
    // information used for ThinLTO renaming and importing.
    Record.clear();
    auto BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: ignore.
      break;
    case bitc::FS_FLAGS: {  // [flags]
      uint64_t Flags = Record[0];
      // Scan flags.
      assert(Flags <= 0x1f && "Unexpected bits in flag");

      // 1 bit: WithGlobalValueDeadStripping flag.
      // Set on combined index only.
      if (Flags & 0x1)
        TheIndex.setWithGlobalValueDeadStripping();
      // 1 bit: SkipModuleByDistributedBackend flag.
      // Set on combined index only.
      if (Flags & 0x2)
        TheIndex.setSkipModuleByDistributedBackend();
      // 1 bit: HasSyntheticEntryCounts flag.
      // Set on combined index only.
      if (Flags & 0x4)
        TheIndex.setHasSyntheticEntryCounts();
      // 1 bit: DisableSplitLTOUnit flag.
      // Set on per module indexes. It is up to the client to validate
      // the consistency of this flag across modules being linked.
      if (Flags & 0x8)
        TheIndex.setEnableSplitLTOUnit();
      // 1 bit: PartiallySplitLTOUnits flag.
      // Set on combined index only.
      if (Flags & 0x10)
        TheIndex.setPartiallySplitLTOUnits();
      break;
    }
    case bitc::FS_VALUE_GUID: { // [valueid, refguid]
      uint64_t ValueID = Record[0];
      GlobalValue::GUID RefGUID = Record[1];
      ValueIdToValueInfoMap[ValueID] =
          std::make_pair(TheIndex.getOrInsertValueInfo(RefGUID), RefGUID);
      break;
    }
    // FS_PERMODULE: [valueid, flags, instcount, fflags, numrefs,
    //                numrefs x valueid, n x (valueid)]
    // FS_PERMODULE_PROFILE: [valueid, flags, instcount, fflags, numrefs,
    //                        numrefs x valueid,
    //                        n x (valueid, hotness)]
    // FS_PERMODULE_RELBF: [valueid, flags, instcount, fflags, numrefs,
    //                      numrefs x valueid,
    //                      n x (valueid, relblockfreq)]
    case bitc::FS_PERMODULE:
    case bitc::FS_PERMODULE_RELBF:
    case bitc::FS_PERMODULE_PROFILE: {
      unsigned ValueID = Record[0];
      uint64_t RawFlags = Record[1];
      unsigned InstCount = Record[2];
      uint64_t RawFunFlags = 0;
      unsigned NumRefs = Record[3];
      unsigned NumImmutableRefs = 0;
      int RefListStartIndex = 4;
      if (Version >= 4) {
        RawFunFlags = Record[3];
        NumRefs = Record[4];
        RefListStartIndex = 5;
        if (Version >= 5) {
          NumImmutableRefs = Record[5];
          RefListStartIndex = 6;
        }
      }

      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      // The module path string ref set in the summary must be owned by the
      // index's module string table. Since we don't have a module path
      // string table section in the per-module index, we create a single
      // module path string table entry with an empty (0) ID to take
      // ownership.
      int CallGraphEdgeStartIndex = RefListStartIndex + NumRefs;
      assert(Record.size() >= RefListStartIndex + NumRefs &&
             "Record size inconsistent with number of references");
      std::vector<ValueInfo> Refs = makeRefList(
          ArrayRef<uint64_t>(Record).slice(RefListStartIndex, NumRefs));
      bool HasProfile = (BitCode == bitc::FS_PERMODULE_PROFILE);
      bool HasRelBF = (BitCode == bitc::FS_PERMODULE_RELBF);
      std::vector<FunctionSummary::EdgeTy> Calls = makeCallList(
          ArrayRef<uint64_t>(Record).slice(CallGraphEdgeStartIndex),
          IsOldProfileFormat, HasProfile, HasRelBF);
      setImmutableRefs(Refs, NumImmutableRefs);
      auto FS = llvm::make_unique<FunctionSummary>(
          Flags, InstCount, getDecodedFFlags(RawFunFlags), /*EntryCount=*/0,
          std::move(Refs), std::move(Calls), std::move(PendingTypeTests),
          std::move(PendingTypeTestAssumeVCalls),
          std::move(PendingTypeCheckedLoadVCalls),
          std::move(PendingTypeTestAssumeConstVCalls),
          std::move(PendingTypeCheckedLoadConstVCalls));
      PendingTypeTests.clear();
      PendingTypeTestAssumeVCalls.clear();
      PendingTypeCheckedLoadVCalls.clear();
      PendingTypeTestAssumeConstVCalls.clear();
      PendingTypeCheckedLoadConstVCalls.clear();
      auto VIAndOriginalGUID = getValueInfoFromValueId(ValueID);
      FS->setModulePath(getThisModule()->first());
      FS->setOriginalName(VIAndOriginalGUID.second);
      TheIndex.addGlobalValueSummary(VIAndOriginalGUID.first, std::move(FS));
      break;
    }
    // FS_ALIAS: [valueid, flags, valueid]
    // Aliases must be emitted (and parsed) after all FS_PERMODULE entries, as
    // they expect all aliasee summaries to be available.
    case bitc::FS_ALIAS: {
      unsigned ValueID = Record[0];
      uint64_t RawFlags = Record[1];
      unsigned AliaseeID = Record[2];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      auto AS = llvm::make_unique<AliasSummary>(Flags);
      // The module path string ref set in the summary must be owned by the
      // index's module string table. Since we don't have a module path
      // string table section in the per-module index, we create a single
      // module path string table entry with an empty (0) ID to take
      // ownership.
      AS->setModulePath(getThisModule()->first());

      GlobalValue::GUID AliaseeGUID =
          getValueInfoFromValueId(AliaseeID).first.getGUID();
      auto AliaseeInModule =
          TheIndex.findSummaryInModule(AliaseeGUID, ModulePath);
      if (!AliaseeInModule)
        return error("Alias expects aliasee summary to be parsed");
      AS->setAliasee(AliaseeInModule);
      AS->setAliaseeGUID(AliaseeGUID);

      auto GUID = getValueInfoFromValueId(ValueID);
      AS->setOriginalName(GUID.second);
      TheIndex.addGlobalValueSummary(GUID.first, std::move(AS));
      break;
    }
    // FS_PERMODULE_GLOBALVAR_INIT_REFS: [valueid, flags, varflags, n x valueid]
    case bitc::FS_PERMODULE_GLOBALVAR_INIT_REFS: {
      unsigned ValueID = Record[0];
      uint64_t RawFlags = Record[1];
      unsigned RefArrayStart = 2;
      GlobalVarSummary::GVarFlags GVF;
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      if (Version >= 5) {
        GVF = getDecodedGVarFlags(Record[2]);
        RefArrayStart = 3;
      }
      std::vector<ValueInfo> Refs =
          makeRefList(ArrayRef<uint64_t>(Record).slice(RefArrayStart));
      auto FS =
          llvm::make_unique<GlobalVarSummary>(Flags, GVF, std::move(Refs));
      FS->setModulePath(getThisModule()->first());
      auto GUID = getValueInfoFromValueId(ValueID);
      FS->setOriginalName(GUID.second);
      TheIndex.addGlobalValueSummary(GUID.first, std::move(FS));
      break;
    }
    // FS_COMBINED: [valueid, modid, flags, instcount, fflags, numrefs,
    //               numrefs x valueid, n x (valueid)]
    // FS_COMBINED_PROFILE: [valueid, modid, flags, instcount, fflags, numrefs,
    //                       numrefs x valueid, n x (valueid, hotness)]
    case bitc::FS_COMBINED:
    case bitc::FS_COMBINED_PROFILE: {
      unsigned ValueID = Record[0];
      uint64_t ModuleId = Record[1];
      uint64_t RawFlags = Record[2];
      unsigned InstCount = Record[3];
      uint64_t RawFunFlags = 0;
      uint64_t EntryCount = 0;
      unsigned NumRefs = Record[4];
      unsigned NumImmutableRefs = 0;
      int RefListStartIndex = 5;

      if (Version >= 4) {
        RawFunFlags = Record[4];
        RefListStartIndex = 6;
        size_t NumRefsIndex = 5;
        if (Version >= 5) {
          RefListStartIndex = 7;
          if (Version >= 6) {
            NumRefsIndex = 6;
            EntryCount = Record[5];
            RefListStartIndex = 8;
          }
          NumImmutableRefs = Record[RefListStartIndex - 1];
        }
        NumRefs = Record[NumRefsIndex];
      }

      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      int CallGraphEdgeStartIndex = RefListStartIndex + NumRefs;
      assert(Record.size() >= RefListStartIndex + NumRefs &&
             "Record size inconsistent with number of references");
      std::vector<ValueInfo> Refs = makeRefList(
          ArrayRef<uint64_t>(Record).slice(RefListStartIndex, NumRefs));
      bool HasProfile = (BitCode == bitc::FS_COMBINED_PROFILE);
      std::vector<FunctionSummary::EdgeTy> Edges = makeCallList(
          ArrayRef<uint64_t>(Record).slice(CallGraphEdgeStartIndex),
          IsOldProfileFormat, HasProfile, false);
      ValueInfo VI = getValueInfoFromValueId(ValueID).first;
      setImmutableRefs(Refs, NumImmutableRefs);
      auto FS = llvm::make_unique<FunctionSummary>(
          Flags, InstCount, getDecodedFFlags(RawFunFlags), EntryCount,
          std::move(Refs), std::move(Edges), std::move(PendingTypeTests),
          std::move(PendingTypeTestAssumeVCalls),
          std::move(PendingTypeCheckedLoadVCalls),
          std::move(PendingTypeTestAssumeConstVCalls),
          std::move(PendingTypeCheckedLoadConstVCalls));
      PendingTypeTests.clear();
      PendingTypeTestAssumeVCalls.clear();
      PendingTypeCheckedLoadVCalls.clear();
      PendingTypeTestAssumeConstVCalls.clear();
      PendingTypeCheckedLoadConstVCalls.clear();
      LastSeenSummary = FS.get();
      LastSeenGUID = VI.getGUID();
      FS->setModulePath(ModuleIdMap[ModuleId]);
      TheIndex.addGlobalValueSummary(VI, std::move(FS));
      break;
    }
    // FS_COMBINED_ALIAS: [valueid, modid, flags, valueid]
    // Aliases must be emitted (and parsed) after all FS_COMBINED entries, as
    // they expect all aliasee summaries to be available.
    case bitc::FS_COMBINED_ALIAS: {
      unsigned ValueID = Record[0];
      uint64_t ModuleId = Record[1];
      uint64_t RawFlags = Record[2];
      unsigned AliaseeValueId = Record[3];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      auto AS = llvm::make_unique<AliasSummary>(Flags);
      LastSeenSummary = AS.get();
      AS->setModulePath(ModuleIdMap[ModuleId]);

      auto AliaseeGUID =
          getValueInfoFromValueId(AliaseeValueId).first.getGUID();
      auto AliaseeInModule =
          TheIndex.findSummaryInModule(AliaseeGUID, AS->modulePath());
      AS->setAliasee(AliaseeInModule);
      AS->setAliaseeGUID(AliaseeGUID);

      ValueInfo VI = getValueInfoFromValueId(ValueID).first;
      LastSeenGUID = VI.getGUID();
      TheIndex.addGlobalValueSummary(VI, std::move(AS));
      break;
    }
    // FS_COMBINED_GLOBALVAR_INIT_REFS: [valueid, modid, flags, n x valueid]
    case bitc::FS_COMBINED_GLOBALVAR_INIT_REFS: {
      unsigned ValueID = Record[0];
      uint64_t ModuleId = Record[1];
      uint64_t RawFlags = Record[2];
      unsigned RefArrayStart = 3;
      GlobalVarSummary::GVarFlags GVF;
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      if (Version >= 5) {
        GVF = getDecodedGVarFlags(Record[3]);
        RefArrayStart = 4;
      }
      std::vector<ValueInfo> Refs =
          makeRefList(ArrayRef<uint64_t>(Record).slice(RefArrayStart));
      auto FS =
          llvm::make_unique<GlobalVarSummary>(Flags, GVF, std::move(Refs));
      LastSeenSummary = FS.get();
      FS->setModulePath(ModuleIdMap[ModuleId]);
      ValueInfo VI = getValueInfoFromValueId(ValueID).first;
      LastSeenGUID = VI.getGUID();
      TheIndex.addGlobalValueSummary(VI, std::move(FS));
      break;
    }
    // FS_COMBINED_ORIGINAL_NAME: [original_name]
    case bitc::FS_COMBINED_ORIGINAL_NAME: {
      uint64_t OriginalName = Record[0];
      if (!LastSeenSummary)
        return error("Name attachment that does not follow a combined record");
      LastSeenSummary->setOriginalName(OriginalName);
      TheIndex.addOriginalName(LastSeenGUID, OriginalName);
      // Reset the LastSeenSummary
      LastSeenSummary = nullptr;
      LastSeenGUID = 0;
      break;
    }
    case bitc::FS_TYPE_TESTS:
      assert(PendingTypeTests.empty());
      PendingTypeTests.insert(PendingTypeTests.end(), Record.begin(),
                              Record.end());
      break;

    case bitc::FS_TYPE_TEST_ASSUME_VCALLS:
      assert(PendingTypeTestAssumeVCalls.empty());
      for (unsigned I = 0; I != Record.size(); I += 2)
        PendingTypeTestAssumeVCalls.push_back({Record[I], Record[I+1]});
      break;

    case bitc::FS_TYPE_CHECKED_LOAD_VCALLS:
      assert(PendingTypeCheckedLoadVCalls.empty());
      for (unsigned I = 0; I != Record.size(); I += 2)
        PendingTypeCheckedLoadVCalls.push_back({Record[I], Record[I+1]});
      break;

    case bitc::FS_TYPE_TEST_ASSUME_CONST_VCALL:
      PendingTypeTestAssumeConstVCalls.push_back(
          {{Record[0], Record[1]}, {Record.begin() + 2, Record.end()}});
      break;

    case bitc::FS_TYPE_CHECKED_LOAD_CONST_VCALL:
      PendingTypeCheckedLoadConstVCalls.push_back(
          {{Record[0], Record[1]}, {Record.begin() + 2, Record.end()}});
      break;

    case bitc::FS_CFI_FUNCTION_DEFS: {
      std::set<std::string> &CfiFunctionDefs = TheIndex.cfiFunctionDefs();
      for (unsigned I = 0; I != Record.size(); I += 2)
        CfiFunctionDefs.insert(
            {Strtab.data() + Record[I], static_cast<size_t>(Record[I + 1])});
      break;
    }

    case bitc::FS_CFI_FUNCTION_DECLS: {
      std::set<std::string> &CfiFunctionDecls = TheIndex.cfiFunctionDecls();
      for (unsigned I = 0; I != Record.size(); I += 2)
        CfiFunctionDecls.insert(
            {Strtab.data() + Record[I], static_cast<size_t>(Record[I + 1])});
      break;
    }

    case bitc::FS_TYPE_ID:
      parseTypeIdSummaryRecord(Record, Strtab, TheIndex);
      break;
    }
  }
  llvm_unreachable("Exit infinite loop");
}

// Parse the  module string table block into the Index.
// This populates the ModulePathStringTable map in the index.
Error ModuleSummaryIndexBitcodeReader::parseModuleStringTable() {
  if (Stream.EnterSubBlock(bitc::MODULE_STRTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  SmallString<128> ModulePath;
  ModuleSummaryIndex::ModuleInfo *LastSeenModule = nullptr;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: // Default behavior: ignore.
      break;
    case bitc::MST_CODE_ENTRY: {
      // MST_ENTRY: [modid, namechar x N]
      uint64_t ModuleId = Record[0];

      if (convertToString(Record, 1, ModulePath))
        return error("Invalid record");

      LastSeenModule = TheIndex.addModule(ModulePath, ModuleId);
      ModuleIdMap[ModuleId] = LastSeenModule->first();

      ModulePath.clear();
      break;
    }
    /// MST_CODE_HASH: [5*i32]
    case bitc::MST_CODE_HASH: {
      if (Record.size() != 5)
        return error("Invalid hash length " + Twine(Record.size()).str());
      if (!LastSeenModule)
        return error("Invalid hash that does not follow a module path");
      int Pos = 0;
      for (auto &Val : Record) {
        assert(!(Val >> 32) && "Unexpected high bits set");
        LastSeenModule->second.second[Pos++] = Val;
      }
      // Reset LastSeenModule to avoid overriding the hash unexpectedly.
      LastSeenModule = nullptr;
      break;
    }
    }
  }
  llvm_unreachable("Exit infinite loop");
}

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class BitcodeErrorCategoryType : public std::error_category {
  const char *name() const noexcept override {
    return "llvm.bitcode";
  }

  std::string message(int IE) const override {
    BitcodeError E = static_cast<BitcodeError>(IE);
    switch (E) {
    case BitcodeError::CorruptedBitcode:
      return "Corrupted bitcode";
    }
    llvm_unreachable("Unknown error type!");
  }
};

} // end anonymous namespace

static ManagedStatic<BitcodeErrorCategoryType> ErrorCategory;

const std::error_category &llvm::BitcodeErrorCategory() {
  return *ErrorCategory;
}

static Expected<StringRef> readBlobInRecord(BitstreamCursor &Stream,
                                            unsigned Block, unsigned RecordID) {
  if (Stream.EnterSubBlock(Block))
    return error("Invalid record");

  StringRef Strtab;
  while (true) {
    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    case BitstreamEntry::EndBlock:
      return Strtab;

    case BitstreamEntry::Error:
      return error("Malformed block");

    case BitstreamEntry::SubBlock:
      if (Stream.SkipBlock())
        return error("Malformed block");
      break;

    case BitstreamEntry::Record:
      StringRef Blob;
      SmallVector<uint64_t, 1> Record;
      if (Stream.readRecord(Entry.ID, Record, &Blob) == RecordID)
        Strtab = Blob;
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// External interface
//===----------------------------------------------------------------------===//

Expected<std::vector<BitcodeModule>>
llvm::getBitcodeModuleList(MemoryBufferRef Buffer) {
  auto FOrErr = getBitcodeFileContents(Buffer);
  if (!FOrErr)
    return FOrErr.takeError();
  return std::move(FOrErr->Mods);
}

Expected<BitcodeFileContents>
llvm::getBitcodeFileContents(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();
  BitstreamCursor &Stream = *StreamOrErr;

  BitcodeFileContents F;
  while (true) {
    uint64_t BCBegin = Stream.getCurrentByteNo();

    // We may be consuming bitcode from a client that leaves garbage at the end
    // of the bitcode stream (e.g. Apple's ar tool). If we are close enough to
    // the end that there cannot possibly be another module, stop looking.
    if (BCBegin + 8 >= Stream.getBitcodeBytes().size())
      return F;

    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    case BitstreamEntry::EndBlock:
    case BitstreamEntry::Error:
      return error("Malformed block");

    case BitstreamEntry::SubBlock: {
      uint64_t IdentificationBit = -1ull;
      if (Entry.ID == bitc::IDENTIFICATION_BLOCK_ID) {
        IdentificationBit = Stream.GetCurrentBitNo() - BCBegin * 8;
        if (Stream.SkipBlock())
          return error("Malformed block");

        Entry = Stream.advance();
        if (Entry.Kind != BitstreamEntry::SubBlock ||
            Entry.ID != bitc::MODULE_BLOCK_ID)
          return error("Malformed block");
      }

      if (Entry.ID == bitc::MODULE_BLOCK_ID) {
        uint64_t ModuleBit = Stream.GetCurrentBitNo() - BCBegin * 8;
        if (Stream.SkipBlock())
          return error("Malformed block");

        F.Mods.push_back({Stream.getBitcodeBytes().slice(
                              BCBegin, Stream.getCurrentByteNo() - BCBegin),
                          Buffer.getBufferIdentifier(), IdentificationBit,
                          ModuleBit});
        continue;
      }

      if (Entry.ID == bitc::STRTAB_BLOCK_ID) {
        Expected<StringRef> Strtab =
            readBlobInRecord(Stream, bitc::STRTAB_BLOCK_ID, bitc::STRTAB_BLOB);
        if (!Strtab)
          return Strtab.takeError();
        // This string table is used by every preceding bitcode module that does
        // not have its own string table. A bitcode file may have multiple
        // string tables if it was created by binary concatenation, for example
        // with "llvm-cat -b".
        for (auto I = F.Mods.rbegin(), E = F.Mods.rend(); I != E; ++I) {
          if (!I->Strtab.empty())
            break;
          I->Strtab = *Strtab;
        }
        // Similarly, the string table is used by every preceding symbol table;
        // normally there will be just one unless the bitcode file was created
        // by binary concatenation.
        if (!F.Symtab.empty() && F.StrtabForSymtab.empty())
          F.StrtabForSymtab = *Strtab;
        continue;
      }

      if (Entry.ID == bitc::SYMTAB_BLOCK_ID) {
        Expected<StringRef> SymtabOrErr =
            readBlobInRecord(Stream, bitc::SYMTAB_BLOCK_ID, bitc::SYMTAB_BLOB);
        if (!SymtabOrErr)
          return SymtabOrErr.takeError();

        // We can expect the bitcode file to have multiple symbol tables if it
        // was created by binary concatenation. In that case we silently
        // ignore any subsequent symbol tables, which is fine because this is a
        // low level function. The client is expected to notice that the number
        // of modules in the symbol table does not match the number of modules
        // in the input file and regenerate the symbol table.
        if (F.Symtab.empty())
          F.Symtab = *SymtabOrErr;
        continue;
      }

      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;
    }
    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

/// Get a lazy one-at-time loading module from bitcode.
///
/// This isn't always used in a lazy context.  In particular, it's also used by
/// \a parseModule().  If this is truly lazy, then we need to eagerly pull
/// in forward-referenced functions from block address references.
///
/// \param[in] MaterializeAll Set to \c true if we should materialize
/// everything.
Expected<std::unique_ptr<Module>>
BitcodeModule::getModuleImpl(LLVMContext &Context, bool MaterializeAll,
                             bool ShouldLazyLoadMetadata, bool IsImporting) {
  BitstreamCursor Stream(Buffer);

  std::string ProducerIdentification;
  if (IdentificationBit != -1ull) {
    Stream.JumpToBit(IdentificationBit);
    Expected<std::string> ProducerIdentificationOrErr =
        readIdentificationBlock(Stream);
    if (!ProducerIdentificationOrErr)
      return ProducerIdentificationOrErr.takeError();

    ProducerIdentification = *ProducerIdentificationOrErr;
  }

  Stream.JumpToBit(ModuleBit);
  auto *R = new BitcodeReader(std::move(Stream), Strtab, ProducerIdentification,
                              Context);

  std::unique_ptr<Module> M =
      llvm::make_unique<Module>(ModuleIdentifier, Context);
  M->setMaterializer(R);

  // Delay parsing Metadata if ShouldLazyLoadMetadata is true.
  if (Error Err =
          R->parseBitcodeInto(M.get(), ShouldLazyLoadMetadata, IsImporting))
    return std::move(Err);

  if (MaterializeAll) {
    // Read in the entire module, and destroy the BitcodeReader.
    if (Error Err = M->materializeAll())
      return std::move(Err);
  } else {
    // Resolve forward references from blockaddresses.
    if (Error Err = R->materializeForwardReferencedFunctions())
      return std::move(Err);
  }
  return std::move(M);
}

Expected<std::unique_ptr<Module>>
BitcodeModule::getLazyModule(LLVMContext &Context, bool ShouldLazyLoadMetadata,
                             bool IsImporting) {
  return getModuleImpl(Context, false, ShouldLazyLoadMetadata, IsImporting);
}

// Parse the specified bitcode buffer and merge the index into CombinedIndex.
// We don't use ModuleIdentifier here because the client may need to control the
// module path used in the combined summary (e.g. when reading summaries for
// regular LTO modules).
Error BitcodeModule::readSummary(ModuleSummaryIndex &CombinedIndex,
                                 StringRef ModulePath, uint64_t ModuleId) {
  BitstreamCursor Stream(Buffer);
  Stream.JumpToBit(ModuleBit);

  ModuleSummaryIndexBitcodeReader R(std::move(Stream), Strtab, CombinedIndex,
                                    ModulePath, ModuleId);
  return R.parseModule();
}

// Parse the specified bitcode buffer, returning the function info index.
Expected<std::unique_ptr<ModuleSummaryIndex>> BitcodeModule::getSummary() {
  BitstreamCursor Stream(Buffer);
  Stream.JumpToBit(ModuleBit);

  auto Index = llvm::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);
  ModuleSummaryIndexBitcodeReader R(std::move(Stream), Strtab, *Index,
                                    ModuleIdentifier, 0);

  if (Error Err = R.parseModule())
    return std::move(Err);

  return std::move(Index);
}

static Expected<bool> getEnableSplitLTOUnitFlag(BitstreamCursor &Stream,
                                                unsigned ID) {
  if (Stream.EnterSubBlock(ID))
    return error("Invalid record");
  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      // If no flags record found, conservatively return true to mimic
      // behavior before this flag was added.
      return true;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Look for the FS_FLAGS record.
    Record.clear();
    auto BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: ignore.
      break;
    case bitc::FS_FLAGS: { // [flags]
      uint64_t Flags = Record[0];
      // Scan flags.
      assert(Flags <= 0x1f && "Unexpected bits in flag");

      return Flags & 0x8;
    }
    }
  }
  llvm_unreachable("Exit infinite loop");
}

// Check if the given bitcode buffer contains a global value summary block.
Expected<BitcodeLTOInfo> BitcodeModule::getLTOInfo() {
  BitstreamCursor Stream(Buffer);
  Stream.JumpToBit(ModuleBit);

  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return BitcodeLTOInfo{/*IsThinLTO=*/false, /*HasSummary=*/false,
                            /*EnableSplitLTOUnit=*/false};

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::GLOBALVAL_SUMMARY_BLOCK_ID) {
        Expected<bool> EnableSplitLTOUnit =
            getEnableSplitLTOUnitFlag(Stream, Entry.ID);
        if (!EnableSplitLTOUnit)
          return EnableSplitLTOUnit.takeError();
        return BitcodeLTOInfo{/*IsThinLTO=*/true, /*HasSummary=*/true,
                              *EnableSplitLTOUnit};
      }

      if (Entry.ID == bitc::FULL_LTO_GLOBALVAL_SUMMARY_BLOCK_ID) {
        Expected<bool> EnableSplitLTOUnit =
            getEnableSplitLTOUnitFlag(Stream, Entry.ID);
        if (!EnableSplitLTOUnit)
          return EnableSplitLTOUnit.takeError();
        return BitcodeLTOInfo{/*IsThinLTO=*/false, /*HasSummary=*/true,
                              *EnableSplitLTOUnit};
      }

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

static Expected<BitcodeModule> getSingleModule(MemoryBufferRef Buffer) {
  Expected<std::vector<BitcodeModule>> MsOrErr = getBitcodeModuleList(Buffer);
  if (!MsOrErr)
    return MsOrErr.takeError();

  if (MsOrErr->size() != 1)
    return error("Expected a single module");

  return (*MsOrErr)[0];
}

Expected<std::unique_ptr<Module>>
llvm::getLazyBitcodeModule(MemoryBufferRef Buffer, LLVMContext &Context,
                           bool ShouldLazyLoadMetadata, bool IsImporting) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->getLazyModule(Context, ShouldLazyLoadMetadata, IsImporting);
}

Expected<std::unique_ptr<Module>> llvm::getOwningLazyBitcodeModule(
    std::unique_ptr<MemoryBuffer> &&Buffer, LLVMContext &Context,
    bool ShouldLazyLoadMetadata, bool IsImporting) {
  auto MOrErr = getLazyBitcodeModule(*Buffer, Context, ShouldLazyLoadMetadata,
                                     IsImporting);
  if (MOrErr)
    (*MOrErr)->setOwnedMemoryBuffer(std::move(Buffer));
  return MOrErr;
}

Expected<std::unique_ptr<Module>>
BitcodeModule::parseModule(LLVMContext &Context) {
  return getModuleImpl(Context, true, false, false);
  // TODO: Restore the use-lists to the in-memory state when the bitcode was
  // written.  We must defer until the Module has been fully materialized.
}

Expected<std::unique_ptr<Module>> llvm::parseBitcodeFile(MemoryBufferRef Buffer,
                                                         LLVMContext &Context) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->parseModule(Context);
}

Expected<std::string> llvm::getBitcodeTargetTriple(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();

  return readTriple(*StreamOrErr);
}

Expected<bool> llvm::isBitcodeContainingObjCCategory(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();

  return hasObjCCategory(*StreamOrErr);
}

Expected<std::string> llvm::getBitcodeProducerString(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();

  return readIdentificationCode(*StreamOrErr);
}

Error llvm::readModuleSummaryIndex(MemoryBufferRef Buffer,
                                   ModuleSummaryIndex &CombinedIndex,
                                   uint64_t ModuleId) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->readSummary(CombinedIndex, BM->getModuleIdentifier(), ModuleId);
}

Expected<std::unique_ptr<ModuleSummaryIndex>>
llvm::getModuleSummaryIndex(MemoryBufferRef Buffer) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->getSummary();
}

Expected<BitcodeLTOInfo> llvm::getBitcodeLTOInfo(MemoryBufferRef Buffer) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->getLTOInfo();
}

Expected<std::unique_ptr<ModuleSummaryIndex>>
llvm::getModuleSummaryIndexForFile(StringRef Path,
                                   bool IgnoreEmptyThinLTOIndexFile) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(Path);
  if (!FileOrErr)
    return errorCodeToError(FileOrErr.getError());
  if (IgnoreEmptyThinLTOIndexFile && !(*FileOrErr)->getBufferSize())
    return nullptr;
  return getModuleSummaryIndex(**FileOrErr);
}
