//===-- LTOModule.cpp - LLVM Link Time Optimizer --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Link Time Optimization library. This library is
// intended to be used by linker to optimize code at link time.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/legacy/LTOModule.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"
#include <system_error>
using namespace llvm;
using namespace llvm::object;

LTOModule::LTOModule(std::unique_ptr<Module> M, MemoryBufferRef MBRef,
                     llvm::TargetMachine *TM)
    : Mod(std::move(M)), MBRef(MBRef), _target(TM) {
  SymTab.addModule(Mod.get());
}

LTOModule::~LTOModule() {}

/// isBitcodeFile - Returns 'true' if the file (or memory contents) is LLVM
/// bitcode.
bool LTOModule::isBitcodeFile(const void *Mem, size_t Length) {
  Expected<MemoryBufferRef> BCData = IRObjectFile::findBitcodeInMemBuffer(
      MemoryBufferRef(StringRef((const char *)Mem, Length), "<mem>"));
  return !errorToBool(BCData.takeError());
}

bool LTOModule::isBitcodeFile(StringRef Path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(Path);
  if (!BufferOrErr)
    return false;

  Expected<MemoryBufferRef> BCData = IRObjectFile::findBitcodeInMemBuffer(
      BufferOrErr.get()->getMemBufferRef());
  return !errorToBool(BCData.takeError());
}

bool LTOModule::isThinLTO() {
  Expected<BitcodeLTOInfo> Result = getBitcodeLTOInfo(MBRef);
  if (!Result) {
    logAllUnhandledErrors(Result.takeError(), errs());
    return false;
  }
  return Result->IsThinLTO;
}

bool LTOModule::isBitcodeForTarget(MemoryBuffer *Buffer,
                                   StringRef TriplePrefix) {
  Expected<MemoryBufferRef> BCOrErr =
      IRObjectFile::findBitcodeInMemBuffer(Buffer->getMemBufferRef());
  if (errorToBool(BCOrErr.takeError()))
    return false;
  LLVMContext Context;
  ErrorOr<std::string> TripleOrErr =
      expectedToErrorOrAndEmitErrors(Context, getBitcodeTargetTriple(*BCOrErr));
  if (!TripleOrErr)
    return false;
  return StringRef(*TripleOrErr).startswith(TriplePrefix);
}

std::string LTOModule::getProducerString(MemoryBuffer *Buffer) {
  Expected<MemoryBufferRef> BCOrErr =
      IRObjectFile::findBitcodeInMemBuffer(Buffer->getMemBufferRef());
  if (errorToBool(BCOrErr.takeError()))
    return "";
  LLVMContext Context;
  ErrorOr<std::string> ProducerOrErr = expectedToErrorOrAndEmitErrors(
      Context, getBitcodeProducerString(*BCOrErr));
  if (!ProducerOrErr)
    return "";
  return *ProducerOrErr;
}

ErrorOr<std::unique_ptr<LTOModule>>
LTOModule::createFromFile(LLVMContext &Context, StringRef path,
                          const TargetOptions &options) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(path);
  if (std::error_code EC = BufferOrErr.getError()) {
    Context.emitError(EC.message());
    return EC;
  }
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BufferOrErr.get());
  return makeLTOModule(Buffer->getMemBufferRef(), options, Context,
                       /* ShouldBeLazy*/ false);
}

ErrorOr<std::unique_ptr<LTOModule>>
LTOModule::createFromOpenFile(LLVMContext &Context, int fd, StringRef path,
                              size_t size, const TargetOptions &options) {
  return createFromOpenFileSlice(Context, fd, path, size, 0, options);
}

ErrorOr<std::unique_ptr<LTOModule>>
LTOModule::createFromOpenFileSlice(LLVMContext &Context, int fd, StringRef path,
                                   size_t map_size, off_t offset,
                                   const TargetOptions &options) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getOpenFileSlice(fd, path, map_size, offset);
  if (std::error_code EC = BufferOrErr.getError()) {
    Context.emitError(EC.message());
    return EC;
  }
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BufferOrErr.get());
  return makeLTOModule(Buffer->getMemBufferRef(), options, Context,
                       /* ShouldBeLazy */ false);
}

ErrorOr<std::unique_ptr<LTOModule>>
LTOModule::createFromBuffer(LLVMContext &Context, const void *mem,
                            size_t length, const TargetOptions &options,
                            StringRef path) {
  StringRef Data((const char *)mem, length);
  MemoryBufferRef Buffer(Data, path);
  return makeLTOModule(Buffer, options, Context, /* ShouldBeLazy */ false);
}

ErrorOr<std::unique_ptr<LTOModule>>
LTOModule::createInLocalContext(std::unique_ptr<LLVMContext> Context,
                                const void *mem, size_t length,
                                const TargetOptions &options, StringRef path) {
  StringRef Data((const char *)mem, length);
  MemoryBufferRef Buffer(Data, path);
  // If we own a context, we know this is being used only for symbol extraction,
  // not linking.  Be lazy in that case.
  ErrorOr<std::unique_ptr<LTOModule>> Ret =
      makeLTOModule(Buffer, options, *Context, /* ShouldBeLazy */ true);
  if (Ret)
    (*Ret)->OwnedContext = std::move(Context);
  return Ret;
}

static ErrorOr<std::unique_ptr<Module>>
parseBitcodeFileImpl(MemoryBufferRef Buffer, LLVMContext &Context,
                     bool ShouldBeLazy) {
  // Find the buffer.
  Expected<MemoryBufferRef> MBOrErr =
      IRObjectFile::findBitcodeInMemBuffer(Buffer);
  if (Error E = MBOrErr.takeError()) {
    std::error_code EC = errorToErrorCode(std::move(E));
    Context.emitError(EC.message());
    return EC;
  }

  if (!ShouldBeLazy) {
    // Parse the full file.
    return expectedToErrorOrAndEmitErrors(Context,
                                          parseBitcodeFile(*MBOrErr, Context));
  }

  // Parse lazily.
  return expectedToErrorOrAndEmitErrors(
      Context,
      getLazyBitcodeModule(*MBOrErr, Context, true /*ShouldLazyLoadMetadata*/));
}

ErrorOr<std::unique_ptr<LTOModule>>
LTOModule::makeLTOModule(MemoryBufferRef Buffer, const TargetOptions &options,
                         LLVMContext &Context, bool ShouldBeLazy) {
  ErrorOr<std::unique_ptr<Module>> MOrErr =
      parseBitcodeFileImpl(Buffer, Context, ShouldBeLazy);
  if (std::error_code EC = MOrErr.getError())
    return EC;
  std::unique_ptr<Module> &M = *MOrErr;

  std::string TripleStr = M->getTargetTriple();
  if (TripleStr.empty())
    TripleStr = sys::getDefaultTargetTriple();
  llvm::Triple Triple(TripleStr);

  // find machine architecture for this module
  std::string errMsg;
  const Target *march = TargetRegistry::lookupTarget(TripleStr, errMsg);
  if (!march)
    return make_error_code(object::object_error::arch_not_found);

  // construct LTOModule, hand over ownership of module and target
  SubtargetFeatures Features;
  Features.getDefaultSubtargetFeatures(Triple);
  std::string FeatureStr = Features.getString();
  // Set a default CPU for Darwin triples.
  std::string CPU;
  if (Triple.isOSDarwin()) {
    if (Triple.getArch() == llvm::Triple::x86_64)
      CPU = "core2";
    else if (Triple.getArch() == llvm::Triple::x86)
      CPU = "yonah";
    else if (Triple.getArch() == llvm::Triple::aarch64)
      CPU = "cyclone";
  }

  TargetMachine *target =
      march->createTargetMachine(TripleStr, CPU, FeatureStr, options, None);

  std::unique_ptr<LTOModule> Ret(new LTOModule(std::move(M), Buffer, target));
  Ret->parseSymbols();
  Ret->parseMetadata();

  return std::move(Ret);
}

/// Create a MemoryBuffer from a memory range with an optional name.
std::unique_ptr<MemoryBuffer>
LTOModule::makeBuffer(const void *mem, size_t length, StringRef name) {
  const char *startPtr = (const char*)mem;
  return MemoryBuffer::getMemBuffer(StringRef(startPtr, length), name, false);
}

/// objcClassNameFromExpression - Get string that the data pointer points to.
bool
LTOModule::objcClassNameFromExpression(const Constant *c, std::string &name) {
  if (const ConstantExpr *ce = dyn_cast<ConstantExpr>(c)) {
    Constant *op = ce->getOperand(0);
    if (GlobalVariable *gvn = dyn_cast<GlobalVariable>(op)) {
      Constant *cn = gvn->getInitializer();
      if (ConstantDataArray *ca = dyn_cast<ConstantDataArray>(cn)) {
        if (ca->isCString()) {
          name = (".objc_class_name_" + ca->getAsCString()).str();
          return true;
        }
      }
    }
  }
  return false;
}

/// addObjCClass - Parse i386/ppc ObjC class data structure.
void LTOModule::addObjCClass(const GlobalVariable *clgv) {
  const ConstantStruct *c = dyn_cast<ConstantStruct>(clgv->getInitializer());
  if (!c) return;

  // second slot in __OBJC,__class is pointer to superclass name
  std::string superclassName;
  if (objcClassNameFromExpression(c->getOperand(1), superclassName)) {
    auto IterBool =
        _undefines.insert(std::make_pair(superclassName, NameAndAttributes()));
    if (IterBool.second) {
      NameAndAttributes &info = IterBool.first->second;
      info.name = IterBool.first->first();
      info.attributes = LTO_SYMBOL_DEFINITION_UNDEFINED;
      info.isFunction = false;
      info.symbol = clgv;
    }
  }

  // third slot in __OBJC,__class is pointer to class name
  std::string className;
  if (objcClassNameFromExpression(c->getOperand(2), className)) {
    auto Iter = _defines.insert(className).first;

    NameAndAttributes info;
    info.name = Iter->first();
    info.attributes = LTO_SYMBOL_PERMISSIONS_DATA |
      LTO_SYMBOL_DEFINITION_REGULAR | LTO_SYMBOL_SCOPE_DEFAULT;
    info.isFunction = false;
    info.symbol = clgv;
    _symbols.push_back(info);
  }
}

/// addObjCCategory - Parse i386/ppc ObjC category data structure.
void LTOModule::addObjCCategory(const GlobalVariable *clgv) {
  const ConstantStruct *c = dyn_cast<ConstantStruct>(clgv->getInitializer());
  if (!c) return;

  // second slot in __OBJC,__category is pointer to target class name
  std::string targetclassName;
  if (!objcClassNameFromExpression(c->getOperand(1), targetclassName))
    return;

  auto IterBool =
      _undefines.insert(std::make_pair(targetclassName, NameAndAttributes()));

  if (!IterBool.second)
    return;

  NameAndAttributes &info = IterBool.first->second;
  info.name = IterBool.first->first();
  info.attributes = LTO_SYMBOL_DEFINITION_UNDEFINED;
  info.isFunction = false;
  info.symbol = clgv;
}

/// addObjCClassRef - Parse i386/ppc ObjC class list data structure.
void LTOModule::addObjCClassRef(const GlobalVariable *clgv) {
  std::string targetclassName;
  if (!objcClassNameFromExpression(clgv->getInitializer(), targetclassName))
    return;

  auto IterBool =
      _undefines.insert(std::make_pair(targetclassName, NameAndAttributes()));

  if (!IterBool.second)
    return;

  NameAndAttributes &info = IterBool.first->second;
  info.name = IterBool.first->first();
  info.attributes = LTO_SYMBOL_DEFINITION_UNDEFINED;
  info.isFunction = false;
  info.symbol = clgv;
}

void LTOModule::addDefinedDataSymbol(ModuleSymbolTable::Symbol Sym) {
  SmallString<64> Buffer;
  {
    raw_svector_ostream OS(Buffer);
    SymTab.printSymbolName(OS, Sym);
    Buffer.c_str();
  }

  const GlobalValue *V = Sym.get<GlobalValue *>();
  addDefinedDataSymbol(Buffer, V);
}

void LTOModule::addDefinedDataSymbol(StringRef Name, const GlobalValue *v) {
  // Add to list of defined symbols.
  addDefinedSymbol(Name, v, false);

  if (!v->hasSection() /* || !isTargetDarwin */)
    return;

  // Special case i386/ppc ObjC data structures in magic sections:
  // The issue is that the old ObjC object format did some strange
  // contortions to avoid real linker symbols.  For instance, the
  // ObjC class data structure is allocated statically in the executable
  // that defines that class.  That data structures contains a pointer to
  // its superclass.  But instead of just initializing that part of the
  // struct to the address of its superclass, and letting the static and
  // dynamic linkers do the rest, the runtime works by having that field
  // instead point to a C-string that is the name of the superclass.
  // At runtime the objc initialization updates that pointer and sets
  // it to point to the actual super class.  As far as the linker
  // knows it is just a pointer to a string.  But then someone wanted the
  // linker to issue errors at build time if the superclass was not found.
  // So they figured out a way in mach-o object format to use an absolute
  // symbols (.objc_class_name_Foo = 0) and a floating reference
  // (.reference .objc_class_name_Bar) to cause the linker into erroring when
  // a class was missing.
  // The following synthesizes the implicit .objc_* symbols for the linker
  // from the ObjC data structures generated by the front end.

  // special case if this data blob is an ObjC class definition
  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(v)) {
    StringRef Section = GV->getSection();
    if (Section.startswith("__OBJC,__class,")) {
      addObjCClass(GV);
    }

    // special case if this data blob is an ObjC category definition
    else if (Section.startswith("__OBJC,__category,")) {
      addObjCCategory(GV);
    }

    // special case if this data blob is the list of referenced classes
    else if (Section.startswith("__OBJC,__cls_refs,")) {
      addObjCClassRef(GV);
    }
  }
}

void LTOModule::addDefinedFunctionSymbol(ModuleSymbolTable::Symbol Sym) {
  SmallString<64> Buffer;
  {
    raw_svector_ostream OS(Buffer);
    SymTab.printSymbolName(OS, Sym);
    Buffer.c_str();
  }

  const Function *F = cast<Function>(Sym.get<GlobalValue *>());
  addDefinedFunctionSymbol(Buffer, F);
}

void LTOModule::addDefinedFunctionSymbol(StringRef Name, const Function *F) {
  // add to list of defined symbols
  addDefinedSymbol(Name, F, true);
}

void LTOModule::addDefinedSymbol(StringRef Name, const GlobalValue *def,
                                 bool isFunction) {
  // set alignment part log2() can have rounding errors
  uint32_t align = def->getAlignment();
  uint32_t attr = align ? countTrailingZeros(align) : 0;

  // set permissions part
  if (isFunction) {
    attr |= LTO_SYMBOL_PERMISSIONS_CODE;
  } else {
    const GlobalVariable *gv = dyn_cast<GlobalVariable>(def);
    if (gv && gv->isConstant())
      attr |= LTO_SYMBOL_PERMISSIONS_RODATA;
    else
      attr |= LTO_SYMBOL_PERMISSIONS_DATA;
  }

  // set definition part
  if (def->hasWeakLinkage() || def->hasLinkOnceLinkage())
    attr |= LTO_SYMBOL_DEFINITION_WEAK;
  else if (def->hasCommonLinkage())
    attr |= LTO_SYMBOL_DEFINITION_TENTATIVE;
  else
    attr |= LTO_SYMBOL_DEFINITION_REGULAR;

  // set scope part
  if (def->hasLocalLinkage())
    // Ignore visibility if linkage is local.
    attr |= LTO_SYMBOL_SCOPE_INTERNAL;
  else if (def->hasHiddenVisibility())
    attr |= LTO_SYMBOL_SCOPE_HIDDEN;
  else if (def->hasProtectedVisibility())
    attr |= LTO_SYMBOL_SCOPE_PROTECTED;
  else if (def->canBeOmittedFromSymbolTable())
    attr |= LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN;
  else
    attr |= LTO_SYMBOL_SCOPE_DEFAULT;

  if (def->hasComdat())
    attr |= LTO_SYMBOL_COMDAT;

  if (isa<GlobalAlias>(def))
    attr |= LTO_SYMBOL_ALIAS;

  auto Iter = _defines.insert(Name).first;

  // fill information structure
  NameAndAttributes info;
  StringRef NameRef = Iter->first();
  info.name = NameRef;
  assert(NameRef.data()[NameRef.size()] == '\0');
  info.attributes = attr;
  info.isFunction = isFunction;
  info.symbol = def;

  // add to table of symbols
  _symbols.push_back(info);
}

/// addAsmGlobalSymbol - Add a global symbol from module-level ASM to the
/// defined list.
void LTOModule::addAsmGlobalSymbol(StringRef name,
                                   lto_symbol_attributes scope) {
  auto IterBool = _defines.insert(name);

  // only add new define if not already defined
  if (!IterBool.second)
    return;

  NameAndAttributes &info = _undefines[IterBool.first->first()];

  if (info.symbol == nullptr) {
    // FIXME: This is trying to take care of module ASM like this:
    //
    //   module asm ".zerofill __FOO, __foo, _bar_baz_qux, 0"
    //
    // but is gross and its mother dresses it funny. Have the ASM parser give us
    // more details for this type of situation so that we're not guessing so
    // much.

    // fill information structure
    info.name = IterBool.first->first();
    info.attributes =
      LTO_SYMBOL_PERMISSIONS_DATA | LTO_SYMBOL_DEFINITION_REGULAR | scope;
    info.isFunction = false;
    info.symbol = nullptr;

    // add to table of symbols
    _symbols.push_back(info);
    return;
  }

  if (info.isFunction)
    addDefinedFunctionSymbol(info.name, cast<Function>(info.symbol));
  else
    addDefinedDataSymbol(info.name, info.symbol);

  _symbols.back().attributes &= ~LTO_SYMBOL_SCOPE_MASK;
  _symbols.back().attributes |= scope;
}

/// addAsmGlobalSymbolUndef - Add a global symbol from module-level ASM to the
/// undefined list.
void LTOModule::addAsmGlobalSymbolUndef(StringRef name) {
  auto IterBool = _undefines.insert(std::make_pair(name, NameAndAttributes()));

  _asm_undefines.push_back(IterBool.first->first());

  // we already have the symbol
  if (!IterBool.second)
    return;

  uint32_t attr = LTO_SYMBOL_DEFINITION_UNDEFINED;
  attr |= LTO_SYMBOL_SCOPE_DEFAULT;
  NameAndAttributes &info = IterBool.first->second;
  info.name = IterBool.first->first();
  info.attributes = attr;
  info.isFunction = false;
  info.symbol = nullptr;
}

/// Add a symbol which isn't defined just yet to a list to be resolved later.
void LTOModule::addPotentialUndefinedSymbol(ModuleSymbolTable::Symbol Sym,
                                            bool isFunc) {
  SmallString<64> name;
  {
    raw_svector_ostream OS(name);
    SymTab.printSymbolName(OS, Sym);
    name.c_str();
  }

  auto IterBool = _undefines.insert(std::make_pair(name, NameAndAttributes()));

  // we already have the symbol
  if (!IterBool.second)
    return;

  NameAndAttributes &info = IterBool.first->second;

  info.name = IterBool.first->first();

  const GlobalValue *decl = Sym.dyn_cast<GlobalValue *>();

  if (decl->hasExternalWeakLinkage())
    info.attributes = LTO_SYMBOL_DEFINITION_WEAKUNDEF;
  else
    info.attributes = LTO_SYMBOL_DEFINITION_UNDEFINED;

  info.isFunction = isFunc;
  info.symbol = decl;
}

void LTOModule::parseSymbols() {
  for (auto Sym : SymTab.symbols()) {
    auto *GV = Sym.dyn_cast<GlobalValue *>();
    uint32_t Flags = SymTab.getSymbolFlags(Sym);
    if (Flags & object::BasicSymbolRef::SF_FormatSpecific)
      continue;

    bool IsUndefined = Flags & object::BasicSymbolRef::SF_Undefined;

    if (!GV) {
      SmallString<64> Buffer;
      {
        raw_svector_ostream OS(Buffer);
        SymTab.printSymbolName(OS, Sym);
        Buffer.c_str();
      }
      StringRef Name(Buffer);

      if (IsUndefined)
        addAsmGlobalSymbolUndef(Name);
      else if (Flags & object::BasicSymbolRef::SF_Global)
        addAsmGlobalSymbol(Name, LTO_SYMBOL_SCOPE_DEFAULT);
      else
        addAsmGlobalSymbol(Name, LTO_SYMBOL_SCOPE_INTERNAL);
      continue;
    }

    auto *F = dyn_cast<Function>(GV);
    if (IsUndefined) {
      addPotentialUndefinedSymbol(Sym, F != nullptr);
      continue;
    }

    if (F) {
      addDefinedFunctionSymbol(Sym);
      continue;
    }

    if (isa<GlobalVariable>(GV)) {
      addDefinedDataSymbol(Sym);
      continue;
    }

    assert(isa<GlobalAlias>(GV));
    addDefinedDataSymbol(Sym);
  }

  // make symbols for all undefines
  for (StringMap<NameAndAttributes>::iterator u =_undefines.begin(),
         e = _undefines.end(); u != e; ++u) {
    // If this symbol also has a definition, then don't make an undefine because
    // it is a tentative definition.
    if (_defines.count(u->getKey())) continue;
    NameAndAttributes info = u->getValue();
    _symbols.push_back(info);
  }
}

/// parseMetadata - Parse metadata from the module
void LTOModule::parseMetadata() {
  raw_string_ostream OS(LinkerOpts);

  // Linker Options
  if (NamedMDNode *LinkerOptions =
          getModule().getNamedMetadata("llvm.linker.options")) {
    for (unsigned i = 0, e = LinkerOptions->getNumOperands(); i != e; ++i) {
      MDNode *MDOptions = LinkerOptions->getOperand(i);
      for (unsigned ii = 0, ie = MDOptions->getNumOperands(); ii != ie; ++ii) {
        MDString *MDOption = cast<MDString>(MDOptions->getOperand(ii));
        OS << " " << MDOption->getString();
      }
    }
  }

  // Globals - we only need to do this for COFF.
  const Triple TT(_target->getTargetTriple());
  if (!TT.isOSBinFormatCOFF())
    return;
  Mangler M;
  for (const NameAndAttributes &Sym : _symbols) {
    if (!Sym.symbol)
      continue;
    emitLinkerFlagsForGlobalCOFF(OS, Sym.symbol, TT, M);
  }

  // Add other interesting metadata here.
}
