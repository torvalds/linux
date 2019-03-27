//===--- CodeGenModule.h - Per-Module state for LLVM CodeGen ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-translation-unit state used for llvm translation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENMODULE_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENMODULE_H

#include "CGVTables.h"
#include "CodeGenTypeCache.h"
#include "CodeGenTypes.h"
#include "SanitizerMetadata.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/Basic/ABI.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SanitizerBlacklist.h"
#include "clang/Basic/XRayLists.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Transforms/Utils/SanitizerStats.h"

namespace llvm {
class Module;
class Constant;
class ConstantInt;
class Function;
class GlobalValue;
class DataLayout;
class FunctionType;
class LLVMContext;
class IndexedInstrProfReader;
}

namespace clang {
class ASTContext;
class AtomicType;
class FunctionDecl;
class IdentifierInfo;
class ObjCMethodDecl;
class ObjCImplementationDecl;
class ObjCCategoryImplDecl;
class ObjCProtocolDecl;
class ObjCEncodeExpr;
class BlockExpr;
class CharUnits;
class Decl;
class Expr;
class Stmt;
class InitListExpr;
class StringLiteral;
class NamedDecl;
class ValueDecl;
class VarDecl;
class LangOptions;
class CodeGenOptions;
class HeaderSearchOptions;
class PreprocessorOptions;
class DiagnosticsEngine;
class AnnotateAttr;
class CXXDestructorDecl;
class Module;
class CoverageSourceInfo;

namespace CodeGen {

class CallArgList;
class CodeGenFunction;
class CodeGenTBAA;
class CGCXXABI;
class CGDebugInfo;
class CGObjCRuntime;
class CGOpenCLRuntime;
class CGOpenMPRuntime;
class CGCUDARuntime;
class BlockFieldFlags;
class FunctionArgList;
class CoverageMappingModuleGen;
class TargetCodeGenInfo;

enum ForDefinition_t : bool {
  NotForDefinition = false,
  ForDefinition = true
};

struct OrderGlobalInits {
  unsigned int priority;
  unsigned int lex_order;
  OrderGlobalInits(unsigned int p, unsigned int l)
      : priority(p), lex_order(l) {}

  bool operator==(const OrderGlobalInits &RHS) const {
    return priority == RHS.priority && lex_order == RHS.lex_order;
  }

  bool operator<(const OrderGlobalInits &RHS) const {
    return std::tie(priority, lex_order) <
           std::tie(RHS.priority, RHS.lex_order);
  }
};

struct ObjCEntrypoints {
  ObjCEntrypoints() { memset(this, 0, sizeof(*this)); }

  /// void objc_alloc(id);
  llvm::Constant *objc_alloc;

  /// void objc_allocWithZone(id);
  llvm::Constant *objc_allocWithZone;

  /// void objc_autoreleasePoolPop(void*);
  llvm::Constant *objc_autoreleasePoolPop;

  /// void objc_autoreleasePoolPop(void*);
  /// Note this method is used when we are using exception handling
  llvm::Constant *objc_autoreleasePoolPopInvoke;

  /// void *objc_autoreleasePoolPush(void);
  llvm::Constant *objc_autoreleasePoolPush;

  /// id objc_autorelease(id);
  llvm::Constant *objc_autorelease;

  /// id objc_autorelease(id);
  /// Note this is the runtime method not the intrinsic.
  llvm::Constant *objc_autoreleaseRuntimeFunction;

  /// id objc_autoreleaseReturnValue(id);
  llvm::Constant *objc_autoreleaseReturnValue;

  /// void objc_copyWeak(id *dest, id *src);
  llvm::Constant *objc_copyWeak;

  /// void objc_destroyWeak(id*);
  llvm::Constant *objc_destroyWeak;

  /// id objc_initWeak(id*, id);
  llvm::Constant *objc_initWeak;

  /// id objc_loadWeak(id*);
  llvm::Constant *objc_loadWeak;

  /// id objc_loadWeakRetained(id*);
  llvm::Constant *objc_loadWeakRetained;

  /// void objc_moveWeak(id *dest, id *src);
  llvm::Constant *objc_moveWeak;

  /// id objc_retain(id);
  llvm::Constant *objc_retain;

  /// id objc_retain(id);
  /// Note this is the runtime method not the intrinsic.
  llvm::Constant *objc_retainRuntimeFunction;

  /// id objc_retainAutorelease(id);
  llvm::Constant *objc_retainAutorelease;

  /// id objc_retainAutoreleaseReturnValue(id);
  llvm::Constant *objc_retainAutoreleaseReturnValue;

  /// id objc_retainAutoreleasedReturnValue(id);
  llvm::Constant *objc_retainAutoreleasedReturnValue;

  /// id objc_retainBlock(id);
  llvm::Constant *objc_retainBlock;

  /// void objc_release(id);
  llvm::Constant *objc_release;

  /// void objc_release(id);
  /// Note this is the runtime method not the intrinsic.
  llvm::Constant *objc_releaseRuntimeFunction;

  /// void objc_storeStrong(id*, id);
  llvm::Constant *objc_storeStrong;

  /// id objc_storeWeak(id*, id);
  llvm::Constant *objc_storeWeak;

  /// id objc_unsafeClaimAutoreleasedReturnValue(id);
  llvm::Constant *objc_unsafeClaimAutoreleasedReturnValue;

  /// A void(void) inline asm to use to mark that the return value of
  /// a call will be immediately retain.
  llvm::InlineAsm *retainAutoreleasedReturnValueMarker;

  /// void clang.arc.use(...);
  llvm::Constant *clang_arc_use;
};

/// This class records statistics on instrumentation based profiling.
class InstrProfStats {
  uint32_t VisitedInMainFile;
  uint32_t MissingInMainFile;
  uint32_t Visited;
  uint32_t Missing;
  uint32_t Mismatched;

public:
  InstrProfStats()
      : VisitedInMainFile(0), MissingInMainFile(0), Visited(0), Missing(0),
        Mismatched(0) {}
  /// Record that we've visited a function and whether or not that function was
  /// in the main source file.
  void addVisited(bool MainFile) {
    if (MainFile)
      ++VisitedInMainFile;
    ++Visited;
  }
  /// Record that a function we've visited has no profile data.
  void addMissing(bool MainFile) {
    if (MainFile)
      ++MissingInMainFile;
    ++Missing;
  }
  /// Record that a function we've visited has mismatched profile data.
  void addMismatched(bool MainFile) { ++Mismatched; }
  /// Whether or not the stats we've gathered indicate any potential problems.
  bool hasDiagnostics() { return Missing || Mismatched; }
  /// Report potential problems we've found to \c Diags.
  void reportDiagnostics(DiagnosticsEngine &Diags, StringRef MainFile);
};

/// A pair of helper functions for a __block variable.
class BlockByrefHelpers : public llvm::FoldingSetNode {
  // MSVC requires this type to be complete in order to process this
  // header.
public:
  llvm::Constant *CopyHelper;
  llvm::Constant *DisposeHelper;

  /// The alignment of the field.  This is important because
  /// different offsets to the field within the byref struct need to
  /// have different helper functions.
  CharUnits Alignment;

  BlockByrefHelpers(CharUnits alignment) : Alignment(alignment) {}
  BlockByrefHelpers(const BlockByrefHelpers &) = default;
  virtual ~BlockByrefHelpers();

  void Profile(llvm::FoldingSetNodeID &id) const {
    id.AddInteger(Alignment.getQuantity());
    profileImpl(id);
  }
  virtual void profileImpl(llvm::FoldingSetNodeID &id) const = 0;

  virtual bool needsCopy() const { return true; }
  virtual void emitCopy(CodeGenFunction &CGF, Address dest, Address src) = 0;

  virtual bool needsDispose() const { return true; }
  virtual void emitDispose(CodeGenFunction &CGF, Address field) = 0;
};

/// This class organizes the cross-function state that is used while generating
/// LLVM code.
class CodeGenModule : public CodeGenTypeCache {
  CodeGenModule(const CodeGenModule &) = delete;
  void operator=(const CodeGenModule &) = delete;

public:
  struct Structor {
    Structor() : Priority(0), Initializer(nullptr), AssociatedData(nullptr) {}
    Structor(int Priority, llvm::Constant *Initializer,
             llvm::Constant *AssociatedData)
        : Priority(Priority), Initializer(Initializer),
          AssociatedData(AssociatedData) {}
    int Priority;
    llvm::Constant *Initializer;
    llvm::Constant *AssociatedData;
  };

  typedef std::vector<Structor> CtorList;

private:
  ASTContext &Context;
  const LangOptions &LangOpts;
  const HeaderSearchOptions &HeaderSearchOpts; // Only used for debug info.
  const PreprocessorOptions &PreprocessorOpts; // Only used for debug info.
  const CodeGenOptions &CodeGenOpts;
  llvm::Module &TheModule;
  DiagnosticsEngine &Diags;
  const TargetInfo &Target;
  std::unique_ptr<CGCXXABI> ABI;
  llvm::LLVMContext &VMContext;

  std::unique_ptr<CodeGenTBAA> TBAA;

  mutable std::unique_ptr<TargetCodeGenInfo> TheTargetCodeGenInfo;

  // This should not be moved earlier, since its initialization depends on some
  // of the previous reference members being already initialized and also checks
  // if TheTargetCodeGenInfo is NULL
  CodeGenTypes Types;

  /// Holds information about C++ vtables.
  CodeGenVTables VTables;

  std::unique_ptr<CGObjCRuntime> ObjCRuntime;
  std::unique_ptr<CGOpenCLRuntime> OpenCLRuntime;
  std::unique_ptr<CGOpenMPRuntime> OpenMPRuntime;
  std::unique_ptr<CGCUDARuntime> CUDARuntime;
  std::unique_ptr<CGDebugInfo> DebugInfo;
  std::unique_ptr<ObjCEntrypoints> ObjCData;
  llvm::MDNode *NoObjCARCExceptionsMetadata = nullptr;
  std::unique_ptr<llvm::IndexedInstrProfReader> PGOReader;
  InstrProfStats PGOStats;
  std::unique_ptr<llvm::SanitizerStatReport> SanStats;

  // A set of references that have only been seen via a weakref so far. This is
  // used to remove the weak of the reference if we ever see a direct reference
  // or a definition.
  llvm::SmallPtrSet<llvm::GlobalValue*, 10> WeakRefReferences;

  /// This contains all the decls which have definitions but/ which are deferred
  /// for emission and therefore should only be output if they are actually
  /// used. If a decl is in this, then it is known to have not been referenced
  /// yet.
  std::map<StringRef, GlobalDecl> DeferredDecls;

  /// This is a list of deferred decls which we have seen that *are* actually
  /// referenced. These get code generated when the module is done.
  std::vector<GlobalDecl> DeferredDeclsToEmit;
  void addDeferredDeclToEmit(GlobalDecl GD) {
    DeferredDeclsToEmit.emplace_back(GD);
  }

  /// List of alias we have emitted. Used to make sure that what they point to
  /// is defined once we get to the end of the of the translation unit.
  std::vector<GlobalDecl> Aliases;

  /// List of multiversion functions that have to be emitted.  Used to make sure
  /// we properly emit the iFunc.
  std::vector<GlobalDecl> MultiVersionFuncs;

  typedef llvm::StringMap<llvm::TrackingVH<llvm::Constant> > ReplacementsTy;
  ReplacementsTy Replacements;

  /// List of global values to be replaced with something else. Used when we
  /// want to replace a GlobalValue but can't identify it by its mangled name
  /// anymore (because the name is already taken).
  llvm::SmallVector<std::pair<llvm::GlobalValue *, llvm::Constant *>, 8>
    GlobalValReplacements;

  /// Set of global decls for which we already diagnosed mangled name conflict.
  /// Required to not issue a warning (on a mangling conflict) multiple times
  /// for the same decl.
  llvm::DenseSet<GlobalDecl> DiagnosedConflictingDefinitions;

  /// A queue of (optional) vtables to consider emitting.
  std::vector<const CXXRecordDecl*> DeferredVTables;

  /// A queue of (optional) vtables that may be emitted opportunistically.
  std::vector<const CXXRecordDecl *> OpportunisticVTables;

  /// List of global values which are required to be present in the object file;
  /// bitcast to i8*. This is used for forcing visibility of symbols which may
  /// otherwise be optimized out.
  std::vector<llvm::WeakTrackingVH> LLVMUsed;
  std::vector<llvm::WeakTrackingVH> LLVMCompilerUsed;

  /// Store the list of global constructors and their respective priorities to
  /// be emitted when the translation unit is complete.
  CtorList GlobalCtors;

  /// Store the list of global destructors and their respective priorities to be
  /// emitted when the translation unit is complete.
  CtorList GlobalDtors;

  /// An ordered map of canonical GlobalDecls to their mangled names.
  llvm::MapVector<GlobalDecl, StringRef> MangledDeclNames;
  llvm::StringMap<GlobalDecl, llvm::BumpPtrAllocator> Manglings;

  // An ordered map of canonical GlobalDecls paired with the cpu-index for
  // cpu-specific name manglings.
  llvm::MapVector<std::pair<GlobalDecl, unsigned>, StringRef>
      CPUSpecificMangledDeclNames;
  llvm::StringMap<std::pair<GlobalDecl, unsigned>, llvm::BumpPtrAllocator>
      CPUSpecificManglings;

  /// Global annotations.
  std::vector<llvm::Constant*> Annotations;

  /// Map used to get unique annotation strings.
  llvm::StringMap<llvm::Constant*> AnnotationStrings;

  llvm::StringMap<llvm::GlobalVariable *> CFConstantStringMap;

  llvm::DenseMap<llvm::Constant *, llvm::GlobalVariable *> ConstantStringMap;
  llvm::DenseMap<const Decl*, llvm::Constant *> StaticLocalDeclMap;
  llvm::DenseMap<const Decl*, llvm::GlobalVariable*> StaticLocalDeclGuardMap;
  llvm::DenseMap<const Expr*, llvm::Constant *> MaterializedGlobalTemporaryMap;

  llvm::DenseMap<QualType, llvm::Constant *> AtomicSetterHelperFnMap;
  llvm::DenseMap<QualType, llvm::Constant *> AtomicGetterHelperFnMap;

  /// Map used to get unique type descriptor constants for sanitizers.
  llvm::DenseMap<QualType, llvm::Constant *> TypeDescriptorMap;

  /// Map used to track internal linkage functions declared within
  /// extern "C" regions.
  typedef llvm::MapVector<IdentifierInfo *,
                          llvm::GlobalValue *> StaticExternCMap;
  StaticExternCMap StaticExternCValues;

  /// thread_local variables defined or used in this TU.
  std::vector<const VarDecl *> CXXThreadLocals;

  /// thread_local variables with initializers that need to run
  /// before any thread_local variable in this TU is odr-used.
  std::vector<llvm::Function *> CXXThreadLocalInits;
  std::vector<const VarDecl *> CXXThreadLocalInitVars;

  /// Global variables with initializers that need to run before main.
  std::vector<llvm::Function *> CXXGlobalInits;

  /// When a C++ decl with an initializer is deferred, null is
  /// appended to CXXGlobalInits, and the index of that null is placed
  /// here so that the initializer will be performed in the correct
  /// order. Once the decl is emitted, the index is replaced with ~0U to ensure
  /// that we don't re-emit the initializer.
  llvm::DenseMap<const Decl*, unsigned> DelayedCXXInitPosition;

  typedef std::pair<OrderGlobalInits, llvm::Function*> GlobalInitData;

  struct GlobalInitPriorityCmp {
    bool operator()(const GlobalInitData &LHS,
                    const GlobalInitData &RHS) const {
      return LHS.first.priority < RHS.first.priority;
    }
  };

  /// Global variables with initializers whose order of initialization is set by
  /// init_priority attribute.
  SmallVector<GlobalInitData, 8> PrioritizedCXXGlobalInits;

  /// Global destructor functions and arguments that need to run on termination.
  std::vector<std::pair<llvm::WeakTrackingVH, llvm::Constant *>> CXXGlobalDtors;

  /// The complete set of modules that has been imported.
  llvm::SetVector<clang::Module *> ImportedModules;

  /// The set of modules for which the module initializers
  /// have been emitted.
  llvm::SmallPtrSet<clang::Module *, 16> EmittedModuleInitializers;

  /// A vector of metadata strings.
  SmallVector<llvm::MDNode *, 16> LinkerOptionsMetadata;

  /// @name Cache for Objective-C runtime types
  /// @{

  /// Cached reference to the class for constant strings. This value has type
  /// int * but is actually an Obj-C class pointer.
  llvm::WeakTrackingVH CFConstantStringClassRef;

  /// The type used to describe the state of a fast enumeration in
  /// Objective-C's for..in loop.
  QualType ObjCFastEnumerationStateType;

  /// @}

  /// Lazily create the Objective-C runtime
  void createObjCRuntime();

  void createOpenCLRuntime();
  void createOpenMPRuntime();
  void createCUDARuntime();

  bool isTriviallyRecursive(const FunctionDecl *F);
  bool shouldEmitFunction(GlobalDecl GD);
  bool shouldOpportunisticallyEmitVTables();
  /// Map used to be sure we don't emit the same CompoundLiteral twice.
  llvm::DenseMap<const CompoundLiteralExpr *, llvm::GlobalVariable *>
      EmittedCompoundLiterals;

  /// Map of the global blocks we've emitted, so that we don't have to re-emit
  /// them if the constexpr evaluator gets aggressive.
  llvm::DenseMap<const BlockExpr *, llvm::Constant *> EmittedGlobalBlocks;

  /// @name Cache for Blocks Runtime Globals
  /// @{

  llvm::Constant *NSConcreteGlobalBlock = nullptr;
  llvm::Constant *NSConcreteStackBlock = nullptr;

  llvm::Constant *BlockObjectAssign = nullptr;
  llvm::Constant *BlockObjectDispose = nullptr;

  llvm::Type *BlockDescriptorType = nullptr;
  llvm::Type *GenericBlockLiteralType = nullptr;

  struct {
    int GlobalUniqueCount;
  } Block;

  /// void @llvm.lifetime.start(i64 %size, i8* nocapture <ptr>)
  llvm::Constant *LifetimeStartFn = nullptr;

  /// void @llvm.lifetime.end(i64 %size, i8* nocapture <ptr>)
  llvm::Constant *LifetimeEndFn = nullptr;

  GlobalDecl initializedGlobalDecl;

  std::unique_ptr<SanitizerMetadata> SanitizerMD;

  /// @}

  llvm::MapVector<const Decl *, bool> DeferredEmptyCoverageMappingDecls;

  std::unique_ptr<CoverageMappingModuleGen> CoverageMapping;

  /// Mapping from canonical types to their metadata identifiers. We need to
  /// maintain this mapping because identifiers may be formed from distinct
  /// MDNodes.
  typedef llvm::DenseMap<QualType, llvm::Metadata *> MetadataTypeMap;
  MetadataTypeMap MetadataIdMap;
  MetadataTypeMap VirtualMetadataIdMap;
  MetadataTypeMap GeneralizedMetadataIdMap;

public:
  CodeGenModule(ASTContext &C, const HeaderSearchOptions &headersearchopts,
                const PreprocessorOptions &ppopts,
                const CodeGenOptions &CodeGenOpts, llvm::Module &M,
                DiagnosticsEngine &Diags,
                CoverageSourceInfo *CoverageInfo = nullptr);

  ~CodeGenModule();

  void clear();

  /// Finalize LLVM code generation.
  void Release();

  /// Return true if we should emit location information for expressions.
  bool getExpressionLocationsEnabled() const;

  /// Return a reference to the configured Objective-C runtime.
  CGObjCRuntime &getObjCRuntime() {
    if (!ObjCRuntime) createObjCRuntime();
    return *ObjCRuntime;
  }

  /// Return true iff an Objective-C runtime has been configured.
  bool hasObjCRuntime() { return !!ObjCRuntime; }

  /// Return a reference to the configured OpenCL runtime.
  CGOpenCLRuntime &getOpenCLRuntime() {
    assert(OpenCLRuntime != nullptr);
    return *OpenCLRuntime;
  }

  /// Return a reference to the configured OpenMP runtime.
  CGOpenMPRuntime &getOpenMPRuntime() {
    assert(OpenMPRuntime != nullptr);
    return *OpenMPRuntime;
  }

  /// Return a reference to the configured CUDA runtime.
  CGCUDARuntime &getCUDARuntime() {
    assert(CUDARuntime != nullptr);
    return *CUDARuntime;
  }

  ObjCEntrypoints &getObjCEntrypoints() const {
    assert(ObjCData != nullptr);
    return *ObjCData;
  }

  // Version checking function, used to implement ObjC's @available:
  // i32 @__isOSVersionAtLeast(i32, i32, i32)
  llvm::Constant *IsOSVersionAtLeastFn = nullptr;

  InstrProfStats &getPGOStats() { return PGOStats; }
  llvm::IndexedInstrProfReader *getPGOReader() const { return PGOReader.get(); }

  CoverageMappingModuleGen *getCoverageMapping() const {
    return CoverageMapping.get();
  }

  llvm::Constant *getStaticLocalDeclAddress(const VarDecl *D) {
    return StaticLocalDeclMap[D];
  }
  void setStaticLocalDeclAddress(const VarDecl *D,
                                 llvm::Constant *C) {
    StaticLocalDeclMap[D] = C;
  }

  llvm::Constant *
  getOrCreateStaticVarDecl(const VarDecl &D,
                           llvm::GlobalValue::LinkageTypes Linkage);

  llvm::GlobalVariable *getStaticLocalDeclGuardAddress(const VarDecl *D) {
    return StaticLocalDeclGuardMap[D];
  }
  void setStaticLocalDeclGuardAddress(const VarDecl *D,
                                      llvm::GlobalVariable *C) {
    StaticLocalDeclGuardMap[D] = C;
  }

  bool lookupRepresentativeDecl(StringRef MangledName,
                                GlobalDecl &Result) const;

  llvm::Constant *getAtomicSetterHelperFnMap(QualType Ty) {
    return AtomicSetterHelperFnMap[Ty];
  }
  void setAtomicSetterHelperFnMap(QualType Ty,
                            llvm::Constant *Fn) {
    AtomicSetterHelperFnMap[Ty] = Fn;
  }

  llvm::Constant *getAtomicGetterHelperFnMap(QualType Ty) {
    return AtomicGetterHelperFnMap[Ty];
  }
  void setAtomicGetterHelperFnMap(QualType Ty,
                            llvm::Constant *Fn) {
    AtomicGetterHelperFnMap[Ty] = Fn;
  }

  llvm::Constant *getTypeDescriptorFromMap(QualType Ty) {
    return TypeDescriptorMap[Ty];
  }
  void setTypeDescriptorInMap(QualType Ty, llvm::Constant *C) {
    TypeDescriptorMap[Ty] = C;
  }

  CGDebugInfo *getModuleDebugInfo() { return DebugInfo.get(); }

  llvm::MDNode *getNoObjCARCExceptionsMetadata() {
    if (!NoObjCARCExceptionsMetadata)
      NoObjCARCExceptionsMetadata = llvm::MDNode::get(getLLVMContext(), None);
    return NoObjCARCExceptionsMetadata;
  }

  ASTContext &getContext() const { return Context; }
  const LangOptions &getLangOpts() const { return LangOpts; }
  const HeaderSearchOptions &getHeaderSearchOpts()
    const { return HeaderSearchOpts; }
  const PreprocessorOptions &getPreprocessorOpts()
    const { return PreprocessorOpts; }
  const CodeGenOptions &getCodeGenOpts() const { return CodeGenOpts; }
  llvm::Module &getModule() const { return TheModule; }
  DiagnosticsEngine &getDiags() const { return Diags; }
  const llvm::DataLayout &getDataLayout() const {
    return TheModule.getDataLayout();
  }
  const TargetInfo &getTarget() const { return Target; }
  const llvm::Triple &getTriple() const { return Target.getTriple(); }
  bool supportsCOMDAT() const;
  void maybeSetTrivialComdat(const Decl &D, llvm::GlobalObject &GO);

  CGCXXABI &getCXXABI() const { return *ABI; }
  llvm::LLVMContext &getLLVMContext() { return VMContext; }

  bool shouldUseTBAA() const { return TBAA != nullptr; }

  const TargetCodeGenInfo &getTargetCodeGenInfo();

  CodeGenTypes &getTypes() { return Types; }

  CodeGenVTables &getVTables() { return VTables; }

  ItaniumVTableContext &getItaniumVTableContext() {
    return VTables.getItaniumVTableContext();
  }

  MicrosoftVTableContext &getMicrosoftVTableContext() {
    return VTables.getMicrosoftVTableContext();
  }

  CtorList &getGlobalCtors() { return GlobalCtors; }
  CtorList &getGlobalDtors() { return GlobalDtors; }

  /// getTBAATypeInfo - Get metadata used to describe accesses to objects of
  /// the given type.
  llvm::MDNode *getTBAATypeInfo(QualType QTy);

  /// getTBAAAccessInfo - Get TBAA information that describes an access to
  /// an object of the given type.
  TBAAAccessInfo getTBAAAccessInfo(QualType AccessType);

  /// getTBAAVTablePtrAccessInfo - Get the TBAA information that describes an
  /// access to a virtual table pointer.
  TBAAAccessInfo getTBAAVTablePtrAccessInfo(llvm::Type *VTablePtrType);

  llvm::MDNode *getTBAAStructInfo(QualType QTy);

  /// getTBAABaseTypeInfo - Get metadata that describes the given base access
  /// type. Return null if the type is not suitable for use in TBAA access tags.
  llvm::MDNode *getTBAABaseTypeInfo(QualType QTy);

  /// getTBAAAccessTagInfo - Get TBAA tag for a given memory access.
  llvm::MDNode *getTBAAAccessTagInfo(TBAAAccessInfo Info);

  /// mergeTBAAInfoForCast - Get merged TBAA information for the purposes of
  /// type casts.
  TBAAAccessInfo mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                      TBAAAccessInfo TargetInfo);

  /// mergeTBAAInfoForConditionalOperator - Get merged TBAA information for the
  /// purposes of conditional operator.
  TBAAAccessInfo mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                     TBAAAccessInfo InfoB);

  /// mergeTBAAInfoForMemoryTransfer - Get merged TBAA information for the
  /// purposes of memory transfer calls.
  TBAAAccessInfo mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                                TBAAAccessInfo SrcInfo);

  /// getTBAAInfoForSubobject - Get TBAA information for an access with a given
  /// base lvalue.
  TBAAAccessInfo getTBAAInfoForSubobject(LValue Base, QualType AccessType) {
    if (Base.getTBAAInfo().isMayAlias())
      return TBAAAccessInfo::getMayAliasInfo();
    return getTBAAAccessInfo(AccessType);
  }

  bool isTypeConstant(QualType QTy, bool ExcludeCtorDtor);

  bool isPaddedAtomicType(QualType type);
  bool isPaddedAtomicType(const AtomicType *type);

  /// DecorateInstructionWithTBAA - Decorate the instruction with a TBAA tag.
  void DecorateInstructionWithTBAA(llvm::Instruction *Inst,
                                   TBAAAccessInfo TBAAInfo);

  /// Adds !invariant.barrier !tag to instruction
  void DecorateInstructionWithInvariantGroup(llvm::Instruction *I,
                                             const CXXRecordDecl *RD);

  /// Emit the given number of characters as a value of type size_t.
  llvm::ConstantInt *getSize(CharUnits numChars);

  /// Set the visibility for the given LLVM GlobalValue.
  void setGlobalVisibility(llvm::GlobalValue *GV, const NamedDecl *D) const;

  void setGlobalVisibilityAndLocal(llvm::GlobalValue *GV,
                                   const NamedDecl *D) const;

  void setDSOLocal(llvm::GlobalValue *GV) const;

  void setDLLImportDLLExport(llvm::GlobalValue *GV, GlobalDecl D) const;
  void setDLLImportDLLExport(llvm::GlobalValue *GV, const NamedDecl *D) const;
  /// Set visibility, dllimport/dllexport and dso_local.
  /// This must be called after dllimport/dllexport is set.
  void setGVProperties(llvm::GlobalValue *GV, GlobalDecl GD) const;
  void setGVProperties(llvm::GlobalValue *GV, const NamedDecl *D) const;

  /// Set the TLS mode for the given LLVM GlobalValue for the thread-local
  /// variable declaration D.
  void setTLSMode(llvm::GlobalValue *GV, const VarDecl &D) const;

  static llvm::GlobalValue::VisibilityTypes GetLLVMVisibility(Visibility V) {
    switch (V) {
    case DefaultVisibility:   return llvm::GlobalValue::DefaultVisibility;
    case HiddenVisibility:    return llvm::GlobalValue::HiddenVisibility;
    case ProtectedVisibility: return llvm::GlobalValue::ProtectedVisibility;
    }
    llvm_unreachable("unknown visibility!");
  }

  llvm::Constant *GetAddrOfGlobal(GlobalDecl GD,
                                  ForDefinition_t IsForDefinition
                                    = NotForDefinition);

  /// Will return a global variable of the given type. If a variable with a
  /// different type already exists then a new  variable with the right type
  /// will be created and all uses of the old variable will be replaced with a
  /// bitcast to the new variable.
  llvm::GlobalVariable *
  CreateOrReplaceCXXRuntimeVariable(StringRef Name, llvm::Type *Ty,
                                    llvm::GlobalValue::LinkageTypes Linkage,
                                    unsigned Alignment);

  llvm::Function *
  CreateGlobalInitOrDestructFunction(llvm::FunctionType *ty, const Twine &name,
                                     const CGFunctionInfo &FI,
                                     SourceLocation Loc = SourceLocation(),
                                     bool TLS = false);

  /// Return the AST address space of the underlying global variable for D, as
  /// determined by its declaration. Normally this is the same as the address
  /// space of D's type, but in CUDA, address spaces are associated with
  /// declarations, not types. If D is nullptr, return the default address
  /// space for global variable.
  ///
  /// For languages without explicit address spaces, if D has default address
  /// space, target-specific global or constant address space may be returned.
  LangAS GetGlobalVarAddressSpace(const VarDecl *D);

  /// Return the llvm::Constant for the address of the given global variable.
  /// If Ty is non-null and if the global doesn't exist, then it will be created
  /// with the specified type instead of whatever the normal requested type
  /// would be. If IsForDefinition is true, it is guaranteed that an actual
  /// global with type Ty will be returned, not conversion of a variable with
  /// the same mangled name but some other type.
  llvm::Constant *GetAddrOfGlobalVar(const VarDecl *D,
                                     llvm::Type *Ty = nullptr,
                                     ForDefinition_t IsForDefinition
                                       = NotForDefinition);

  /// Return the AST address space of string literal, which is used to emit
  /// the string literal as global variable in LLVM IR.
  /// Note: This is not necessarily the address space of the string literal
  /// in AST. For address space agnostic language, e.g. C++, string literal
  /// in AST is always in default address space.
  LangAS getStringLiteralAddressSpace() const;

  /// Return the address of the given function. If Ty is non-null, then this
  /// function will use the specified type if it has to create it.
  llvm::Constant *GetAddrOfFunction(GlobalDecl GD, llvm::Type *Ty = nullptr,
                                    bool ForVTable = false,
                                    bool DontDefer = false,
                                    ForDefinition_t IsForDefinition
                                      = NotForDefinition);

  /// Get the address of the RTTI descriptor for the given type.
  llvm::Constant *GetAddrOfRTTIDescriptor(QualType Ty, bool ForEH = false);

  /// Get the address of a uuid descriptor .
  ConstantAddress GetAddrOfUuidDescriptor(const CXXUuidofExpr* E);

  /// Get the address of the thunk for the given global decl.
  llvm::Constant *GetAddrOfThunk(StringRef Name, llvm::Type *FnTy,
                                 GlobalDecl GD);

  /// Get a reference to the target of VD.
  ConstantAddress GetWeakRefReference(const ValueDecl *VD);

  /// Returns the assumed alignment of an opaque pointer to the given class.
  CharUnits getClassPointerAlignment(const CXXRecordDecl *CD);

  /// Returns the assumed alignment of a virtual base of a class.
  CharUnits getVBaseAlignment(CharUnits DerivedAlign,
                              const CXXRecordDecl *Derived,
                              const CXXRecordDecl *VBase);

  /// Given a class pointer with an actual known alignment, and the
  /// expected alignment of an object at a dynamic offset w.r.t that
  /// pointer, return the alignment to assume at the offset.
  CharUnits getDynamicOffsetAlignment(CharUnits ActualAlign,
                                      const CXXRecordDecl *Class,
                                      CharUnits ExpectedTargetAlign);

  CharUnits
  computeNonVirtualBaseClassOffset(const CXXRecordDecl *DerivedClass,
                                   CastExpr::path_const_iterator Start,
                                   CastExpr::path_const_iterator End);

  /// Returns the offset from a derived class to  a class. Returns null if the
  /// offset is 0.
  llvm::Constant *
  GetNonVirtualBaseClassOffset(const CXXRecordDecl *ClassDecl,
                               CastExpr::path_const_iterator PathBegin,
                               CastExpr::path_const_iterator PathEnd);

  llvm::FoldingSet<BlockByrefHelpers> ByrefHelpersCache;

  /// Fetches the global unique block count.
  int getUniqueBlockCount() { return ++Block.GlobalUniqueCount; }

  /// Fetches the type of a generic block descriptor.
  llvm::Type *getBlockDescriptorType();

  /// The type of a generic block literal.
  llvm::Type *getGenericBlockLiteralType();

  /// Gets the address of a block which requires no captures.
  llvm::Constant *GetAddrOfGlobalBlock(const BlockExpr *BE, StringRef Name);

  /// Returns the address of a block which requires no caputres, or null if
  /// we've yet to emit the block for BE.
  llvm::Constant *getAddrOfGlobalBlockIfEmitted(const BlockExpr *BE) {
    return EmittedGlobalBlocks.lookup(BE);
  }

  /// Notes that BE's global block is available via Addr. Asserts that BE
  /// isn't already emitted.
  void setAddrOfGlobalBlock(const BlockExpr *BE, llvm::Constant *Addr);

  /// Return a pointer to a constant CFString object for the given string.
  ConstantAddress GetAddrOfConstantCFString(const StringLiteral *Literal);

  /// Return a pointer to a constant NSString object for the given string. Or a
  /// user defined String object as defined via
  /// -fconstant-string-class=class_name option.
  ConstantAddress GetAddrOfConstantString(const StringLiteral *Literal);

  /// Return a constant array for the given string.
  llvm::Constant *GetConstantArrayFromStringLiteral(const StringLiteral *E);

  /// Return a pointer to a constant array for the given string literal.
  ConstantAddress
  GetAddrOfConstantStringFromLiteral(const StringLiteral *S,
                                     StringRef Name = ".str");

  /// Return a pointer to a constant array for the given ObjCEncodeExpr node.
  ConstantAddress
  GetAddrOfConstantStringFromObjCEncode(const ObjCEncodeExpr *);

  /// Returns a pointer to a character array containing the literal and a
  /// terminating '\0' character. The result has pointer to array type.
  ///
  /// \param GlobalName If provided, the name to use for the global (if one is
  /// created).
  ConstantAddress
  GetAddrOfConstantCString(const std::string &Str,
                           const char *GlobalName = nullptr);

  /// Returns a pointer to a constant global variable for the given file-scope
  /// compound literal expression.
  ConstantAddress GetAddrOfConstantCompoundLiteral(const CompoundLiteralExpr*E);

  /// If it's been emitted already, returns the GlobalVariable corresponding to
  /// a compound literal. Otherwise, returns null.
  llvm::GlobalVariable *
  getAddrOfConstantCompoundLiteralIfEmitted(const CompoundLiteralExpr *E);

  /// Notes that CLE's GlobalVariable is GV. Asserts that CLE isn't already
  /// emitted.
  void setAddrOfConstantCompoundLiteral(const CompoundLiteralExpr *CLE,
                                        llvm::GlobalVariable *GV);

  /// Returns a pointer to a global variable representing a temporary
  /// with static or thread storage duration.
  ConstantAddress GetAddrOfGlobalTemporary(const MaterializeTemporaryExpr *E,
                                           const Expr *Inner);

  /// Retrieve the record type that describes the state of an
  /// Objective-C fast enumeration loop (for..in).
  QualType getObjCFastEnumerationStateType();

  // Produce code for this constructor/destructor. This method doesn't try
  // to apply any ABI rules about which other constructors/destructors
  // are needed or if they are alias to each other.
  llvm::Function *codegenCXXStructor(const CXXMethodDecl *MD,
                                     StructorType Type);

  /// Return the address of the constructor/destructor of the given type.
  llvm::Constant *
  getAddrOfCXXStructor(const CXXMethodDecl *MD, StructorType Type,
                       const CGFunctionInfo *FnInfo = nullptr,
                       llvm::FunctionType *FnType = nullptr,
                       bool DontDefer = false,
                       ForDefinition_t IsForDefinition = NotForDefinition);

  /// Given a builtin id for a function like "__builtin_fabsf", return a
  /// Function* for "fabsf".
  llvm::Constant *getBuiltinLibFunction(const FunctionDecl *FD,
                                        unsigned BuiltinID);

  llvm::Function *getIntrinsic(unsigned IID, ArrayRef<llvm::Type*> Tys = None);

  /// Emit code for a single top level declaration.
  void EmitTopLevelDecl(Decl *D);

  /// Stored a deferred empty coverage mapping for an unused
  /// and thus uninstrumented top level declaration.
  void AddDeferredUnusedCoverageMapping(Decl *D);

  /// Remove the deferred empty coverage mapping as this
  /// declaration is actually instrumented.
  void ClearUnusedCoverageMapping(const Decl *D);

  /// Emit all the deferred coverage mappings
  /// for the uninstrumented functions.
  void EmitDeferredUnusedCoverageMappings();

  /// Tell the consumer that this variable has been instantiated.
  void HandleCXXStaticMemberVarInstantiation(VarDecl *VD);

  /// If the declaration has internal linkage but is inside an
  /// extern "C" linkage specification, prepare to emit an alias for it
  /// to the expected name.
  template<typename SomeDecl>
  void MaybeHandleStaticInExternC(const SomeDecl *D, llvm::GlobalValue *GV);

  /// Add a global to a list to be added to the llvm.used metadata.
  void addUsedGlobal(llvm::GlobalValue *GV);

  /// Add a global to a list to be added to the llvm.compiler.used metadata.
  void addCompilerUsedGlobal(llvm::GlobalValue *GV);

  /// Add a destructor and object to add to the C++ global destructor function.
  void AddCXXDtorEntry(llvm::Constant *DtorFn, llvm::Constant *Object) {
    CXXGlobalDtors.emplace_back(DtorFn, Object);
  }

  /// Create a new runtime function with the specified type and name.
  llvm::Constant *
  CreateRuntimeFunction(llvm::FunctionType *Ty, StringRef Name,
                        llvm::AttributeList ExtraAttrs = llvm::AttributeList(),
                        bool Local = false);

  /// Create a new compiler builtin function with the specified type and name.
  llvm::Constant *
  CreateBuiltinFunction(llvm::FunctionType *Ty, StringRef Name,
                        llvm::AttributeList ExtraAttrs = llvm::AttributeList());
  /// Create a new runtime global variable with the specified type and name.
  llvm::Constant *CreateRuntimeVariable(llvm::Type *Ty,
                                        StringRef Name);

  ///@name Custom Blocks Runtime Interfaces
  ///@{

  llvm::Constant *getNSConcreteGlobalBlock();
  llvm::Constant *getNSConcreteStackBlock();
  llvm::Constant *getBlockObjectAssign();
  llvm::Constant *getBlockObjectDispose();

  ///@}

  llvm::Constant *getLLVMLifetimeStartFn();
  llvm::Constant *getLLVMLifetimeEndFn();

  // Make sure that this type is translated.
  void UpdateCompletedType(const TagDecl *TD);

  llvm::Constant *getMemberPointerConstant(const UnaryOperator *e);

  /// Emit type info if type of an expression is a variably modified
  /// type. Also emit proper debug info for cast types.
  void EmitExplicitCastExprType(const ExplicitCastExpr *E,
                                CodeGenFunction *CGF = nullptr);

  /// Return the result of value-initializing the given type, i.e. a null
  /// expression of the given type.  This is usually, but not always, an LLVM
  /// null constant.
  llvm::Constant *EmitNullConstant(QualType T);

  /// Return a null constant appropriate for zero-initializing a base class with
  /// the given type. This is usually, but not always, an LLVM null constant.
  llvm::Constant *EmitNullConstantForBase(const CXXRecordDecl *Record);

  /// Emit a general error that something can't be done.
  void Error(SourceLocation loc, StringRef error);

  /// Print out an error that codegen doesn't support the specified stmt yet.
  void ErrorUnsupported(const Stmt *S, const char *Type);

  /// Print out an error that codegen doesn't support the specified decl yet.
  void ErrorUnsupported(const Decl *D, const char *Type);

  /// Set the attributes on the LLVM function for the given decl and function
  /// info. This applies attributes necessary for handling the ABI as well as
  /// user specified attributes like section.
  void SetInternalFunctionAttributes(GlobalDecl GD, llvm::Function *F,
                                     const CGFunctionInfo &FI);

  /// Set the LLVM function attributes (sext, zext, etc).
  void SetLLVMFunctionAttributes(GlobalDecl GD, const CGFunctionInfo &Info,
                                 llvm::Function *F);

  /// Set the LLVM function attributes which only apply to a function
  /// definition.
  void SetLLVMFunctionAttributesForDefinition(const Decl *D, llvm::Function *F);

  /// Return true iff the given type uses 'sret' when used as a return type.
  bool ReturnTypeUsesSRet(const CGFunctionInfo &FI);

  /// Return true iff the given type uses an argument slot when 'sret' is used
  /// as a return type.
  bool ReturnSlotInterferesWithArgs(const CGFunctionInfo &FI);

  /// Return true iff the given type uses 'fpret' when used as a return type.
  bool ReturnTypeUsesFPRet(QualType ResultType);

  /// Return true iff the given type uses 'fp2ret' when used as a return type.
  bool ReturnTypeUsesFP2Ret(QualType ResultType);

  /// Get the LLVM attributes and calling convention to use for a particular
  /// function type.
  ///
  /// \param Name - The function name.
  /// \param Info - The function type information.
  /// \param CalleeInfo - The callee information these attributes are being
  /// constructed for. If valid, the attributes applied to this decl may
  /// contribute to the function attributes and calling convention.
  /// \param Attrs [out] - On return, the attribute list to use.
  /// \param CallingConv [out] - On return, the LLVM calling convention to use.
  void ConstructAttributeList(StringRef Name, const CGFunctionInfo &Info,
                              CGCalleeInfo CalleeInfo,
                              llvm::AttributeList &Attrs, unsigned &CallingConv,
                              bool AttrOnCallSite);

  /// Adds attributes to F according to our CodeGenOptions and LangOptions, as
  /// though we had emitted it ourselves.  We remove any attributes on F that
  /// conflict with the attributes we add here.
  ///
  /// This is useful for adding attrs to bitcode modules that you want to link
  /// with but don't control, such as CUDA's libdevice.  When linking with such
  /// a bitcode library, you might want to set e.g. its functions'
  /// "unsafe-fp-math" attribute to match the attr of the functions you're
  /// codegen'ing.  Otherwise, LLVM will interpret the bitcode module's lack of
  /// unsafe-fp-math attrs as tantamount to unsafe-fp-math=false, and then LLVM
  /// will propagate unsafe-fp-math=false up to every transitive caller of a
  /// function in the bitcode library!
  ///
  /// With the exception of fast-math attrs, this will only make the attributes
  /// on the function more conservative.  But it's unsafe to call this on a
  /// function which relies on particular fast-math attributes for correctness.
  /// It's up to you to ensure that this is safe.
  void AddDefaultFnAttrs(llvm::Function &F);

  /// Parses the target attributes passed in, and returns only the ones that are
  /// valid feature names.
  TargetAttr::ParsedTargetAttr filterFunctionTargetAttrs(const TargetAttr *TD);

  // Fills in the supplied string map with the set of target features for the
  // passed in function.
  void getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap, GlobalDecl GD);

  StringRef getMangledName(GlobalDecl GD);
  StringRef getBlockMangledName(GlobalDecl GD, const BlockDecl *BD);

  void EmitTentativeDefinition(const VarDecl *D);

  void EmitVTable(CXXRecordDecl *Class);

  void RefreshTypeCacheForClass(const CXXRecordDecl *Class);

  /// Appends Opts to the "llvm.linker.options" metadata value.
  void AppendLinkerOptions(StringRef Opts);

  /// Appends a detect mismatch command to the linker options.
  void AddDetectMismatch(StringRef Name, StringRef Value);

  /// Appends a dependent lib to the "llvm.linker.options" metadata
  /// value.
  void AddDependentLib(StringRef Lib);

  void AddELFLibDirective(StringRef Lib);

  llvm::GlobalVariable::LinkageTypes getFunctionLinkage(GlobalDecl GD);

  void setFunctionLinkage(GlobalDecl GD, llvm::Function *F) {
    F->setLinkage(getFunctionLinkage(GD));
  }

  /// Return the appropriate linkage for the vtable, VTT, and type information
  /// of the given class.
  llvm::GlobalVariable::LinkageTypes getVTableLinkage(const CXXRecordDecl *RD);

  /// Return the store size, in character units, of the given LLVM type.
  CharUnits GetTargetTypeStoreSize(llvm::Type *Ty) const;

  /// Returns LLVM linkage for a declarator.
  llvm::GlobalValue::LinkageTypes
  getLLVMLinkageForDeclarator(const DeclaratorDecl *D, GVALinkage Linkage,
                              bool IsConstantVariable);

  /// Returns LLVM linkage for a declarator.
  llvm::GlobalValue::LinkageTypes
  getLLVMLinkageVarDefinition(const VarDecl *VD, bool IsConstant);

  /// Emit all the global annotations.
  void EmitGlobalAnnotations();

  /// Emit an annotation string.
  llvm::Constant *EmitAnnotationString(StringRef Str);

  /// Emit the annotation's translation unit.
  llvm::Constant *EmitAnnotationUnit(SourceLocation Loc);

  /// Emit the annotation line number.
  llvm::Constant *EmitAnnotationLineNo(SourceLocation L);

  /// Generate the llvm::ConstantStruct which contains the annotation
  /// information for a given GlobalValue. The annotation struct is
  /// {i8 *, i8 *, i8 *, i32}. The first field is a constant expression, the
  /// GlobalValue being annotated. The second field is the constant string
  /// created from the AnnotateAttr's annotation. The third field is a constant
  /// string containing the name of the translation unit. The fourth field is
  /// the line number in the file of the annotated value declaration.
  llvm::Constant *EmitAnnotateAttr(llvm::GlobalValue *GV,
                                   const AnnotateAttr *AA,
                                   SourceLocation L);

  /// Add global annotations that are set on D, for the global GV. Those
  /// annotations are emitted during finalization of the LLVM code.
  void AddGlobalAnnotations(const ValueDecl *D, llvm::GlobalValue *GV);

  bool isInSanitizerBlacklist(SanitizerMask Kind, llvm::Function *Fn,
                              SourceLocation Loc) const;

  bool isInSanitizerBlacklist(llvm::GlobalVariable *GV, SourceLocation Loc,
                              QualType Ty,
                              StringRef Category = StringRef()) const;

  /// Imbue XRay attributes to a function, applying the always/never attribute
  /// lists in the process. Returns true if we did imbue attributes this way,
  /// false otherwise.
  bool imbueXRayAttrs(llvm::Function *Fn, SourceLocation Loc,
                      StringRef Category = StringRef()) const;

  SanitizerMetadata *getSanitizerMetadata() {
    return SanitizerMD.get();
  }

  void addDeferredVTable(const CXXRecordDecl *RD) {
    DeferredVTables.push_back(RD);
  }

  /// Emit code for a single global function or var decl. Forward declarations
  /// are emitted lazily.
  void EmitGlobal(GlobalDecl D);

  bool TryEmitBaseDestructorAsAlias(const CXXDestructorDecl *D);

  llvm::GlobalValue *GetGlobalValue(StringRef Ref);

  /// Set attributes which are common to any form of a global definition (alias,
  /// Objective-C method, function, global variable).
  ///
  /// NOTE: This should only be called for definitions.
  void SetCommonAttributes(GlobalDecl GD, llvm::GlobalValue *GV);

  void addReplacement(StringRef Name, llvm::Constant *C);

  void addGlobalValReplacement(llvm::GlobalValue *GV, llvm::Constant *C);

  /// Emit a code for threadprivate directive.
  /// \param D Threadprivate declaration.
  void EmitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *D);

  /// Emit a code for declare reduction construct.
  void EmitOMPDeclareReduction(const OMPDeclareReductionDecl *D,
                               CodeGenFunction *CGF = nullptr);

  /// Emit a code for requires directive.
  /// \param D Requires declaration
  void EmitOMPRequiresDecl(const OMPRequiresDecl *D);

  /// Returns whether the given record has hidden LTO visibility and therefore
  /// may participate in (single-module) CFI and whole-program vtable
  /// optimization.
  bool HasHiddenLTOVisibility(const CXXRecordDecl *RD);

  /// Emit type metadata for the given vtable using the given layout.
  void EmitVTableTypeMetadata(llvm::GlobalVariable *VTable,
                              const VTableLayout &VTLayout);

  /// Generate a cross-DSO type identifier for MD.
  llvm::ConstantInt *CreateCrossDsoCfiTypeId(llvm::Metadata *MD);

  /// Create a metadata identifier for the given type. This may either be an
  /// MDString (for external identifiers) or a distinct unnamed MDNode (for
  /// internal identifiers).
  llvm::Metadata *CreateMetadataIdentifierForType(QualType T);

  /// Create a metadata identifier that is intended to be used to check virtual
  /// calls via a member function pointer.
  llvm::Metadata *CreateMetadataIdentifierForVirtualMemPtrType(QualType T);

  /// Create a metadata identifier for the generalization of the given type.
  /// This may either be an MDString (for external identifiers) or a distinct
  /// unnamed MDNode (for internal identifiers).
  llvm::Metadata *CreateMetadataIdentifierGeneralized(QualType T);

  /// Create and attach type metadata to the given function.
  void CreateFunctionTypeMetadataForIcall(const FunctionDecl *FD,
                                          llvm::Function *F);

  /// Returns whether this module needs the "all-vtables" type identifier.
  bool NeedAllVtablesTypeId() const;

  /// Create and attach type metadata for the given vtable.
  void AddVTableTypeMetadata(llvm::GlobalVariable *VTable, CharUnits Offset,
                             const CXXRecordDecl *RD);

  /// Return a vector of most-base classes for RD. This is used to implement
  /// control flow integrity checks for member function pointers.
  ///
  /// A most-base class of a class C is defined as a recursive base class of C,
  /// including C itself, that does not have any bases.
  std::vector<const CXXRecordDecl *>
  getMostBaseClasses(const CXXRecordDecl *RD);

  /// Get the declaration of std::terminate for the platform.
  llvm::Constant *getTerminateFn();

  llvm::SanitizerStatReport &getSanStats();

  llvm::Value *
  createOpenCLIntToSamplerConversion(const Expr *E, CodeGenFunction &CGF);

  /// Get target specific null pointer.
  /// \param T is the LLVM type of the null pointer.
  /// \param QT is the clang QualType of the null pointer.
  llvm::Constant *getNullPointer(llvm::PointerType *T, QualType QT);

private:
  llvm::Constant *GetOrCreateLLVMFunction(
      StringRef MangledName, llvm::Type *Ty, GlobalDecl D, bool ForVTable,
      bool DontDefer = false, bool IsThunk = false,
      llvm::AttributeList ExtraAttrs = llvm::AttributeList(),
      ForDefinition_t IsForDefinition = NotForDefinition);

  llvm::Constant *GetOrCreateMultiVersionResolver(GlobalDecl GD,
                                                  llvm::Type *DeclTy,
                                                  const FunctionDecl *FD);
  void UpdateMultiVersionNames(GlobalDecl GD, const FunctionDecl *FD);

  llvm::Constant *GetOrCreateLLVMGlobal(StringRef MangledName,
                                        llvm::PointerType *PTy,
                                        const VarDecl *D,
                                        ForDefinition_t IsForDefinition
                                          = NotForDefinition);

  bool GetCPUAndFeaturesAttributes(GlobalDecl GD,
                                   llvm::AttrBuilder &AttrBuilder);
  void setNonAliasAttributes(GlobalDecl GD, llvm::GlobalObject *GO);

  /// Set function attributes for a function declaration.
  void SetFunctionAttributes(GlobalDecl GD, llvm::Function *F,
                             bool IsIncompleteFunction, bool IsThunk);

  void EmitGlobalDefinition(GlobalDecl D, llvm::GlobalValue *GV = nullptr);

  void EmitGlobalFunctionDefinition(GlobalDecl GD, llvm::GlobalValue *GV);
  void EmitMultiVersionFunctionDefinition(GlobalDecl GD, llvm::GlobalValue *GV);

  void EmitGlobalVarDefinition(const VarDecl *D, bool IsTentative = false);
  void EmitAliasDefinition(GlobalDecl GD);
  void emitIFuncDefinition(GlobalDecl GD);
  void emitCPUDispatchDefinition(GlobalDecl GD);
  void EmitObjCPropertyImplementations(const ObjCImplementationDecl *D);
  void EmitObjCIvarInitializations(ObjCImplementationDecl *D);

  // C++ related functions.

  void EmitDeclContext(const DeclContext *DC);
  void EmitLinkageSpec(const LinkageSpecDecl *D);

  /// Emit the function that initializes C++ thread_local variables.
  void EmitCXXThreadLocalInitFunc();

  /// Emit the function that initializes C++ globals.
  void EmitCXXGlobalInitFunc();

  /// Emit the function that destroys C++ globals.
  void EmitCXXGlobalDtorFunc();

  /// Emit the function that initializes the specified global (if PerformInit is
  /// true) and registers its destructor.
  void EmitCXXGlobalVarDeclInitFunc(const VarDecl *D,
                                    llvm::GlobalVariable *Addr,
                                    bool PerformInit);

  void EmitPointerToInitFunc(const VarDecl *VD, llvm::GlobalVariable *Addr,
                             llvm::Function *InitFunc, InitSegAttr *ISA);

  // FIXME: Hardcoding priority here is gross.
  void AddGlobalCtor(llvm::Function *Ctor, int Priority = 65535,
                     llvm::Constant *AssociatedData = nullptr);
  void AddGlobalDtor(llvm::Function *Dtor, int Priority = 65535);

  /// EmitCtorList - Generates a global array of functions and priorities using
  /// the given list and name. This array will have appending linkage and is
  /// suitable for use as a LLVM constructor or destructor array. Clears Fns.
  void EmitCtorList(CtorList &Fns, const char *GlobalName);

  /// Emit any needed decls for which code generation was deferred.
  void EmitDeferred();

  /// Try to emit external vtables as available_externally if they have emitted
  /// all inlined virtual functions.  It runs after EmitDeferred() and therefore
  /// is not allowed to create new references to things that need to be emitted
  /// lazily.
  void EmitVTablesOpportunistically();

  /// Call replaceAllUsesWith on all pairs in Replacements.
  void applyReplacements();

  /// Call replaceAllUsesWith on all pairs in GlobalValReplacements.
  void applyGlobalValReplacements();

  void checkAliases();

  std::map<int, llvm::TinyPtrVector<llvm::Function *>> DtorsUsingAtExit;

  /// Register functions annotated with __attribute__((destructor)) using
  /// __cxa_atexit, if it is available, or atexit otherwise.
  void registerGlobalDtorsWithAtExit();

  void emitMultiVersionFunctions();

  /// Emit any vtables which we deferred and still have a use for.
  void EmitDeferredVTables();

  /// Emit a dummy function that reference a CoreFoundation symbol when
  /// @available is used on Darwin.
  void emitAtAvailableLinkGuard();

  /// Emit the llvm.used and llvm.compiler.used metadata.
  void emitLLVMUsed();

  /// Emit the link options introduced by imported modules.
  void EmitModuleLinkOptions();

  /// Emit aliases for internal-linkage declarations inside "C" language
  /// linkage specifications, giving them the "expected" name where possible.
  void EmitStaticExternCAliases();

  void EmitDeclMetadata();

  /// Emit the Clang version as llvm.ident metadata.
  void EmitVersionIdentMetadata();

  /// Emit the Clang commandline as llvm.commandline metadata.
  void EmitCommandLineMetadata();

  /// Emits target specific Metadata for global declarations.
  void EmitTargetMetadata();

  /// Emits OpenCL specific Metadata e.g. OpenCL version.
  void EmitOpenCLMetadata();

  /// Emit the llvm.gcov metadata used to tell LLVM where to emit the .gcno and
  /// .gcda files in a way that persists in .bc files.
  void EmitCoverageFile();

  /// Emits the initializer for a uuidof string.
  llvm::Constant *EmitUuidofInitializer(StringRef uuidstr);

  /// Determine whether the definition must be emitted; if this returns \c
  /// false, the definition can be emitted lazily if it's used.
  bool MustBeEmitted(const ValueDecl *D);

  /// Determine whether the definition can be emitted eagerly, or should be
  /// delayed until the end of the translation unit. This is relevant for
  /// definitions whose linkage can change, e.g. implicit function instantions
  /// which may later be explicitly instantiated.
  bool MayBeEmittedEagerly(const ValueDecl *D);

  /// Check whether we can use a "simpler", more core exceptions personality
  /// function.
  void SimplifyPersonality();

  /// Helper function for ConstructAttributeList and AddDefaultFnAttrs.
  /// Constructs an AttrList for a function with the given properties.
  void ConstructDefaultFnAttrList(StringRef Name, bool HasOptnone,
                                  bool AttrOnCallSite,
                                  llvm::AttrBuilder &FuncAttrs);

  llvm::Metadata *CreateMetadataIdentifierImpl(QualType T, MetadataTypeMap &Map,
                                               StringRef Suffix);
};

}  // end namespace CodeGen
}  // end namespace clang

#endif // LLVM_CLANG_LIB_CODEGEN_CODEGENMODULE_H
