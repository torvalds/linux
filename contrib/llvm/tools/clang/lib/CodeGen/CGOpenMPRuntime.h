//===----- CGOpenMPRuntime.h - Interface to OpenMP Runtimes -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides a class for OpenMP runtime code generation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGOPENMPRUNTIME_H
#define LLVM_CLANG_LIB_CODEGEN_CGOPENMPRUNTIME_H

#include "CGValue.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/Type.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {
class ArrayType;
class Constant;
class FunctionType;
class GlobalVariable;
class StructType;
class Type;
class Value;
} // namespace llvm

namespace clang {
class Expr;
class GlobalDecl;
class OMPDependClause;
class OMPExecutableDirective;
class OMPLoopDirective;
class VarDecl;
class OMPDeclareReductionDecl;
class IdentifierInfo;

namespace CodeGen {
class Address;
class CodeGenFunction;
class CodeGenModule;

/// A basic class for pre|post-action for advanced codegen sequence for OpenMP
/// region.
class PrePostActionTy {
public:
  explicit PrePostActionTy() {}
  virtual void Enter(CodeGenFunction &CGF) {}
  virtual void Exit(CodeGenFunction &CGF) {}
  virtual ~PrePostActionTy() {}
};

/// Class provides a way to call simple version of codegen for OpenMP region, or
/// an advanced with possible pre|post-actions in codegen.
class RegionCodeGenTy final {
  intptr_t CodeGen;
  typedef void (*CodeGenTy)(intptr_t, CodeGenFunction &, PrePostActionTy &);
  CodeGenTy Callback;
  mutable PrePostActionTy *PrePostAction;
  RegionCodeGenTy() = delete;
  RegionCodeGenTy &operator=(const RegionCodeGenTy &) = delete;
  template <typename Callable>
  static void CallbackFn(intptr_t CodeGen, CodeGenFunction &CGF,
                         PrePostActionTy &Action) {
    return (*reinterpret_cast<Callable *>(CodeGen))(CGF, Action);
  }

public:
  template <typename Callable>
  RegionCodeGenTy(
      Callable &&CodeGen,
      typename std::enable_if<
          !std::is_same<typename std::remove_reference<Callable>::type,
                        RegionCodeGenTy>::value>::type * = nullptr)
      : CodeGen(reinterpret_cast<intptr_t>(&CodeGen)),
        Callback(CallbackFn<typename std::remove_reference<Callable>::type>),
        PrePostAction(nullptr) {}
  void setAction(PrePostActionTy &Action) const { PrePostAction = &Action; }
  void operator()(CodeGenFunction &CGF) const;
};

struct OMPTaskDataTy final {
  SmallVector<const Expr *, 4> PrivateVars;
  SmallVector<const Expr *, 4> PrivateCopies;
  SmallVector<const Expr *, 4> FirstprivateVars;
  SmallVector<const Expr *, 4> FirstprivateCopies;
  SmallVector<const Expr *, 4> FirstprivateInits;
  SmallVector<const Expr *, 4> LastprivateVars;
  SmallVector<const Expr *, 4> LastprivateCopies;
  SmallVector<const Expr *, 4> ReductionVars;
  SmallVector<const Expr *, 4> ReductionCopies;
  SmallVector<const Expr *, 4> ReductionOps;
  SmallVector<std::pair<OpenMPDependClauseKind, const Expr *>, 4> Dependences;
  llvm::PointerIntPair<llvm::Value *, 1, bool> Final;
  llvm::PointerIntPair<llvm::Value *, 1, bool> Schedule;
  llvm::PointerIntPair<llvm::Value *, 1, bool> Priority;
  llvm::Value *Reductions = nullptr;
  unsigned NumberOfParts = 0;
  bool Tied = true;
  bool Nogroup = false;
};

/// Class intended to support codegen of all kind of the reduction clauses.
class ReductionCodeGen {
private:
  /// Data required for codegen of reduction clauses.
  struct ReductionData {
    /// Reference to the original shared item.
    const Expr *Ref = nullptr;
    /// Helper expression for generation of private copy.
    const Expr *Private = nullptr;
    /// Helper expression for generation reduction operation.
    const Expr *ReductionOp = nullptr;
    ReductionData(const Expr *Ref, const Expr *Private, const Expr *ReductionOp)
        : Ref(Ref), Private(Private), ReductionOp(ReductionOp) {}
  };
  /// List of reduction-based clauses.
  SmallVector<ReductionData, 4> ClausesData;

  /// List of addresses of original shared variables/expressions.
  SmallVector<std::pair<LValue, LValue>, 4> SharedAddresses;
  /// Sizes of the reduction items in chars.
  SmallVector<std::pair<llvm::Value *, llvm::Value *>, 4> Sizes;
  /// Base declarations for the reduction items.
  SmallVector<const VarDecl *, 4> BaseDecls;

  /// Emits lvalue for shared expression.
  LValue emitSharedLValue(CodeGenFunction &CGF, const Expr *E);
  /// Emits upper bound for shared expression (if array section).
  LValue emitSharedLValueUB(CodeGenFunction &CGF, const Expr *E);
  /// Performs aggregate initialization.
  /// \param N Number of reduction item in the common list.
  /// \param PrivateAddr Address of the corresponding private item.
  /// \param SharedLVal Address of the original shared variable.
  /// \param DRD Declare reduction construct used for reduction item.
  void emitAggregateInitialization(CodeGenFunction &CGF, unsigned N,
                                   Address PrivateAddr, LValue SharedLVal,
                                   const OMPDeclareReductionDecl *DRD);

public:
  ReductionCodeGen(ArrayRef<const Expr *> Shareds,
                   ArrayRef<const Expr *> Privates,
                   ArrayRef<const Expr *> ReductionOps);
  /// Emits lvalue for a reduction item.
  /// \param N Number of the reduction item.
  void emitSharedLValue(CodeGenFunction &CGF, unsigned N);
  /// Emits the code for the variable-modified type, if required.
  /// \param N Number of the reduction item.
  void emitAggregateType(CodeGenFunction &CGF, unsigned N);
  /// Emits the code for the variable-modified type, if required.
  /// \param N Number of the reduction item.
  /// \param Size Size of the type in chars.
  void emitAggregateType(CodeGenFunction &CGF, unsigned N, llvm::Value *Size);
  /// Performs initialization of the private copy for the reduction item.
  /// \param N Number of the reduction item.
  /// \param PrivateAddr Address of the corresponding private item.
  /// \param DefaultInit Default initialization sequence that should be
  /// performed if no reduction specific initialization is found.
  /// \param SharedLVal Address of the original shared variable.
  void
  emitInitialization(CodeGenFunction &CGF, unsigned N, Address PrivateAddr,
                     LValue SharedLVal,
                     llvm::function_ref<bool(CodeGenFunction &)> DefaultInit);
  /// Returns true if the private copy requires cleanups.
  bool needCleanups(unsigned N);
  /// Emits cleanup code for the reduction item.
  /// \param N Number of the reduction item.
  /// \param PrivateAddr Address of the corresponding private item.
  void emitCleanups(CodeGenFunction &CGF, unsigned N, Address PrivateAddr);
  /// Adjusts \p PrivatedAddr for using instead of the original variable
  /// address in normal operations.
  /// \param N Number of the reduction item.
  /// \param PrivateAddr Address of the corresponding private item.
  Address adjustPrivateAddress(CodeGenFunction &CGF, unsigned N,
                               Address PrivateAddr);
  /// Returns LValue for the reduction item.
  LValue getSharedLValue(unsigned N) const { return SharedAddresses[N].first; }
  /// Returns the size of the reduction item (in chars and total number of
  /// elements in the item), or nullptr, if the size is a constant.
  std::pair<llvm::Value *, llvm::Value *> getSizes(unsigned N) const {
    return Sizes[N];
  }
  /// Returns the base declaration of the reduction item.
  const VarDecl *getBaseDecl(unsigned N) const { return BaseDecls[N]; }
  /// Returns the base declaration of the reduction item.
  const Expr *getRefExpr(unsigned N) const { return ClausesData[N].Ref; }
  /// Returns true if the initialization of the reduction item uses initializer
  /// from declare reduction construct.
  bool usesReductionInitializer(unsigned N) const;
};

class CGOpenMPRuntime {
public:
  /// Allows to disable automatic handling of functions used in target regions
  /// as those marked as `omp declare target`.
  class DisableAutoDeclareTargetRAII {
    CodeGenModule &CGM;
    bool SavedShouldMarkAsGlobal;

  public:
    DisableAutoDeclareTargetRAII(CodeGenModule &CGM);
    ~DisableAutoDeclareTargetRAII();
  };

protected:
  CodeGenModule &CGM;
  StringRef FirstSeparator, Separator;

  /// Constructor allowing to redefine the name separator for the variables.
  explicit CGOpenMPRuntime(CodeGenModule &CGM, StringRef FirstSeparator,
                           StringRef Separator);

  /// Creates offloading entry for the provided entry ID \a ID,
  /// address \a Addr, size \a Size, and flags \a Flags.
  virtual void createOffloadEntry(llvm::Constant *ID, llvm::Constant *Addr,
                                  uint64_t Size, int32_t Flags,
                                  llvm::GlobalValue::LinkageTypes Linkage);

  /// Helper to emit outlined function for 'target' directive.
  /// \param D Directive to emit.
  /// \param ParentName Name of the function that encloses the target region.
  /// \param OutlinedFn Outlined function value to be defined by this call.
  /// \param OutlinedFnID Outlined function ID value to be defined by this call.
  /// \param IsOffloadEntry True if the outlined function is an offload entry.
  /// \param CodeGen Lambda codegen specific to an accelerator device.
  /// An outlined function may not be an entry if, e.g. the if clause always
  /// evaluates to false.
  virtual void emitTargetOutlinedFunctionHelper(const OMPExecutableDirective &D,
                                                StringRef ParentName,
                                                llvm::Function *&OutlinedFn,
                                                llvm::Constant *&OutlinedFnID,
                                                bool IsOffloadEntry,
                                                const RegionCodeGenTy &CodeGen);

  /// Emits code for OpenMP 'if' clause using specified \a CodeGen
  /// function. Here is the logic:
  /// if (Cond) {
  ///   ThenGen();
  /// } else {
  ///   ElseGen();
  /// }
  void emitOMPIfClause(CodeGenFunction &CGF, const Expr *Cond,
                       const RegionCodeGenTy &ThenGen,
                       const RegionCodeGenTy &ElseGen);

  /// Emits object of ident_t type with info for source location.
  /// \param Flags Flags for OpenMP location.
  ///
  llvm::Value *emitUpdateLocation(CodeGenFunction &CGF, SourceLocation Loc,
                                  unsigned Flags = 0);

  /// Returns pointer to ident_t type.
  llvm::Type *getIdentTyPointerTy();

  /// Gets thread id value for the current thread.
  ///
  llvm::Value *getThreadID(CodeGenFunction &CGF, SourceLocation Loc);

  /// Get the function name of an outlined region.
  //  The name can be customized depending on the target.
  //
  virtual StringRef getOutlinedHelperName() const { return ".omp_outlined."; }

  /// Emits \p Callee function call with arguments \p Args with location \p Loc.
  void emitCall(CodeGenFunction &CGF, SourceLocation Loc, llvm::Value *Callee,
                ArrayRef<llvm::Value *> Args = llvm::None) const;

  /// Emits address of the word in a memory where current thread id is
  /// stored.
  virtual Address emitThreadIDAddress(CodeGenFunction &CGF, SourceLocation Loc);

  void setLocThreadIdInsertPt(CodeGenFunction &CGF,
                              bool AtCurrentPoint = false);
  void clearLocThreadIdInsertPt(CodeGenFunction &CGF);

  /// Check if the default location must be constant.
  /// Default is false to support OMPT/OMPD.
  virtual bool isDefaultLocationConstant() const { return false; }

  /// Returns additional flags that can be stored in reserved_2 field of the
  /// default location.
  virtual unsigned getDefaultLocationReserved2Flags() const { return 0; }

  /// Returns default flags for the barriers depending on the directive, for
  /// which this barier is going to be emitted.
  static unsigned getDefaultFlagsForBarriers(OpenMPDirectiveKind Kind);

  /// Get the LLVM type for the critical name.
  llvm::ArrayType *getKmpCriticalNameTy() const {return KmpCriticalNameTy;}

  /// Returns corresponding lock object for the specified critical region
  /// name. If the lock object does not exist it is created, otherwise the
  /// reference to the existing copy is returned.
  /// \param CriticalName Name of the critical region.
  ///
  llvm::Value *getCriticalRegionLock(StringRef CriticalName);

private:
  /// Default const ident_t object used for initialization of all other
  /// ident_t objects.
  llvm::Constant *DefaultOpenMPPSource = nullptr;
  using FlagsTy = std::pair<unsigned, unsigned>;
  /// Map of flags and corresponding default locations.
  using OpenMPDefaultLocMapTy = llvm::DenseMap<FlagsTy, llvm::Value *>;
  OpenMPDefaultLocMapTy OpenMPDefaultLocMap;
  Address getOrCreateDefaultLocation(unsigned Flags);

  QualType IdentQTy;
  llvm::StructType *IdentTy = nullptr;
  /// Map for SourceLocation and OpenMP runtime library debug locations.
  typedef llvm::DenseMap<unsigned, llvm::Value *> OpenMPDebugLocMapTy;
  OpenMPDebugLocMapTy OpenMPDebugLocMap;
  /// The type for a microtask which gets passed to __kmpc_fork_call().
  /// Original representation is:
  /// typedef void (kmpc_micro)(kmp_int32 global_tid, kmp_int32 bound_tid,...);
  llvm::FunctionType *Kmpc_MicroTy = nullptr;
  /// Stores debug location and ThreadID for the function.
  struct DebugLocThreadIdTy {
    llvm::Value *DebugLoc;
    llvm::Value *ThreadID;
    /// Insert point for the service instructions.
    llvm::AssertingVH<llvm::Instruction> ServiceInsertPt = nullptr;
  };
  /// Map of local debug location, ThreadId and functions.
  typedef llvm::DenseMap<llvm::Function *, DebugLocThreadIdTy>
      OpenMPLocThreadIDMapTy;
  OpenMPLocThreadIDMapTy OpenMPLocThreadIDMap;
  /// Map of UDRs and corresponding combiner/initializer.
  typedef llvm::DenseMap<const OMPDeclareReductionDecl *,
                         std::pair<llvm::Function *, llvm::Function *>>
      UDRMapTy;
  UDRMapTy UDRMap;
  /// Map of functions and locally defined UDRs.
  typedef llvm::DenseMap<llvm::Function *,
                         SmallVector<const OMPDeclareReductionDecl *, 4>>
      FunctionUDRMapTy;
  FunctionUDRMapTy FunctionUDRMap;
  /// Type kmp_critical_name, originally defined as typedef kmp_int32
  /// kmp_critical_name[8];
  llvm::ArrayType *KmpCriticalNameTy;
  /// An ordered map of auto-generated variables to their unique names.
  /// It stores variables with the following names: 1) ".gomp_critical_user_" +
  /// <critical_section_name> + ".var" for "omp critical" directives; 2)
  /// <mangled_name_for_global_var> + ".cache." for cache for threadprivate
  /// variables.
  llvm::StringMap<llvm::AssertingVH<llvm::Constant>, llvm::BumpPtrAllocator>
      InternalVars;
  /// Type typedef kmp_int32 (* kmp_routine_entry_t)(kmp_int32, void *);
  llvm::Type *KmpRoutineEntryPtrTy = nullptr;
  QualType KmpRoutineEntryPtrQTy;
  /// Type typedef struct kmp_task {
  ///    void *              shareds; /**< pointer to block of pointers to
  ///    shared vars   */
  ///    kmp_routine_entry_t routine; /**< pointer to routine to call for
  ///    executing task */
  ///    kmp_int32           part_id; /**< part id for the task */
  ///    kmp_routine_entry_t destructors; /* pointer to function to invoke
  ///    deconstructors of firstprivate C++ objects */
  /// } kmp_task_t;
  QualType KmpTaskTQTy;
  /// Saved kmp_task_t for task directive.
  QualType SavedKmpTaskTQTy;
  /// Saved kmp_task_t for taskloop-based directive.
  QualType SavedKmpTaskloopTQTy;
  /// Type typedef struct kmp_depend_info {
  ///    kmp_intptr_t               base_addr;
  ///    size_t                     len;
  ///    struct {
  ///             bool                   in:1;
  ///             bool                   out:1;
  ///    } flags;
  /// } kmp_depend_info_t;
  QualType KmpDependInfoTy;
  /// struct kmp_dim {  // loop bounds info casted to kmp_int64
  ///  kmp_int64 lo; // lower
  ///  kmp_int64 up; // upper
  ///  kmp_int64 st; // stride
  /// };
  QualType KmpDimTy;
  /// Type struct __tgt_offload_entry{
  ///   void      *addr;       // Pointer to the offload entry info.
  ///                          // (function or global)
  ///   char      *name;       // Name of the function or global.
  ///   size_t     size;       // Size of the entry info (0 if it a function).
  /// };
  QualType TgtOffloadEntryQTy;
  /// struct __tgt_device_image{
  /// void   *ImageStart;       // Pointer to the target code start.
  /// void   *ImageEnd;         // Pointer to the target code end.
  /// // We also add the host entries to the device image, as it may be useful
  /// // for the target runtime to have access to that information.
  /// __tgt_offload_entry  *EntriesBegin;   // Begin of the table with all
  ///                                       // the entries.
  /// __tgt_offload_entry  *EntriesEnd;     // End of the table with all the
  ///                                       // entries (non inclusive).
  /// };
  QualType TgtDeviceImageQTy;
  /// struct __tgt_bin_desc{
  ///   int32_t              NumDevices;      // Number of devices supported.
  ///   __tgt_device_image   *DeviceImages;   // Arrays of device images
  ///                                         // (one per device).
  ///   __tgt_offload_entry  *EntriesBegin;   // Begin of the table with all the
  ///                                         // entries.
  ///   __tgt_offload_entry  *EntriesEnd;     // End of the table with all the
  ///                                         // entries (non inclusive).
  /// };
  QualType TgtBinaryDescriptorQTy;
  /// Entity that registers the offloading constants that were emitted so
  /// far.
  class OffloadEntriesInfoManagerTy {
    CodeGenModule &CGM;

    /// Number of entries registered so far.
    unsigned OffloadingEntriesNum = 0;

  public:
    /// Base class of the entries info.
    class OffloadEntryInfo {
    public:
      /// Kind of a given entry.
      enum OffloadingEntryInfoKinds : unsigned {
        /// Entry is a target region.
        OffloadingEntryInfoTargetRegion = 0,
        /// Entry is a declare target variable.
        OffloadingEntryInfoDeviceGlobalVar = 1,
        /// Invalid entry info.
        OffloadingEntryInfoInvalid = ~0u
      };

    protected:
      OffloadEntryInfo() = delete;
      explicit OffloadEntryInfo(OffloadingEntryInfoKinds Kind) : Kind(Kind) {}
      explicit OffloadEntryInfo(OffloadingEntryInfoKinds Kind, unsigned Order,
                                uint32_t Flags)
          : Flags(Flags), Order(Order), Kind(Kind) {}
      ~OffloadEntryInfo() = default;

    public:
      bool isValid() const { return Order != ~0u; }
      unsigned getOrder() const { return Order; }
      OffloadingEntryInfoKinds getKind() const { return Kind; }
      uint32_t getFlags() const { return Flags; }
      void setFlags(uint32_t NewFlags) { Flags = NewFlags; }
      llvm::Constant *getAddress() const {
        return cast_or_null<llvm::Constant>(Addr);
      }
      void setAddress(llvm::Constant *V) {
        assert(!Addr.pointsToAliveValue() && "Address has been set before!");
        Addr = V;
      }
      static bool classof(const OffloadEntryInfo *Info) { return true; }

    private:
      /// Address of the entity that has to be mapped for offloading.
      llvm::WeakTrackingVH Addr;

      /// Flags associated with the device global.
      uint32_t Flags = 0u;

      /// Order this entry was emitted.
      unsigned Order = ~0u;

      OffloadingEntryInfoKinds Kind = OffloadingEntryInfoInvalid;
    };

    /// Return true if a there are no entries defined.
    bool empty() const;
    /// Return number of entries defined so far.
    unsigned size() const { return OffloadingEntriesNum; }
    OffloadEntriesInfoManagerTy(CodeGenModule &CGM) : CGM(CGM) {}

    //
    // Target region entries related.
    //

    /// Kind of the target registry entry.
    enum OMPTargetRegionEntryKind : uint32_t {
      /// Mark the entry as target region.
      OMPTargetRegionEntryTargetRegion = 0x0,
      /// Mark the entry as a global constructor.
      OMPTargetRegionEntryCtor = 0x02,
      /// Mark the entry as a global destructor.
      OMPTargetRegionEntryDtor = 0x04,
    };

    /// Target region entries info.
    class OffloadEntryInfoTargetRegion final : public OffloadEntryInfo {
      /// Address that can be used as the ID of the entry.
      llvm::Constant *ID = nullptr;

    public:
      OffloadEntryInfoTargetRegion()
          : OffloadEntryInfo(OffloadingEntryInfoTargetRegion) {}
      explicit OffloadEntryInfoTargetRegion(unsigned Order,
                                            llvm::Constant *Addr,
                                            llvm::Constant *ID,
                                            OMPTargetRegionEntryKind Flags)
          : OffloadEntryInfo(OffloadingEntryInfoTargetRegion, Order, Flags),
            ID(ID) {
        setAddress(Addr);
      }

      llvm::Constant *getID() const { return ID; }
      void setID(llvm::Constant *V) {
        assert(!ID && "ID has been set before!");
        ID = V;
      }
      static bool classof(const OffloadEntryInfo *Info) {
        return Info->getKind() == OffloadingEntryInfoTargetRegion;
      }
    };

    /// Initialize target region entry.
    void initializeTargetRegionEntryInfo(unsigned DeviceID, unsigned FileID,
                                         StringRef ParentName, unsigned LineNum,
                                         unsigned Order);
    /// Register target region entry.
    void registerTargetRegionEntryInfo(unsigned DeviceID, unsigned FileID,
                                       StringRef ParentName, unsigned LineNum,
                                       llvm::Constant *Addr, llvm::Constant *ID,
                                       OMPTargetRegionEntryKind Flags);
    /// Return true if a target region entry with the provided information
    /// exists.
    bool hasTargetRegionEntryInfo(unsigned DeviceID, unsigned FileID,
                                  StringRef ParentName, unsigned LineNum) const;
    /// brief Applies action \a Action on all registered entries.
    typedef llvm::function_ref<void(unsigned, unsigned, StringRef, unsigned,
                                    const OffloadEntryInfoTargetRegion &)>
        OffloadTargetRegionEntryInfoActTy;
    void actOnTargetRegionEntriesInfo(
        const OffloadTargetRegionEntryInfoActTy &Action);

    //
    // Device global variable entries related.
    //

    /// Kind of the global variable entry..
    enum OMPTargetGlobalVarEntryKind : uint32_t {
      /// Mark the entry as a to declare target.
      OMPTargetGlobalVarEntryTo = 0x0,
      /// Mark the entry as a to declare target link.
      OMPTargetGlobalVarEntryLink = 0x1,
    };

    /// Device global variable entries info.
    class OffloadEntryInfoDeviceGlobalVar final : public OffloadEntryInfo {
      /// Type of the global variable.
     CharUnits VarSize;
     llvm::GlobalValue::LinkageTypes Linkage;

   public:
     OffloadEntryInfoDeviceGlobalVar()
         : OffloadEntryInfo(OffloadingEntryInfoDeviceGlobalVar) {}
     explicit OffloadEntryInfoDeviceGlobalVar(unsigned Order,
                                              OMPTargetGlobalVarEntryKind Flags)
         : OffloadEntryInfo(OffloadingEntryInfoDeviceGlobalVar, Order, Flags) {}
     explicit OffloadEntryInfoDeviceGlobalVar(
         unsigned Order, llvm::Constant *Addr, CharUnits VarSize,
         OMPTargetGlobalVarEntryKind Flags,
         llvm::GlobalValue::LinkageTypes Linkage)
         : OffloadEntryInfo(OffloadingEntryInfoDeviceGlobalVar, Order, Flags),
           VarSize(VarSize), Linkage(Linkage) {
       setAddress(Addr);
      }

      CharUnits getVarSize() const { return VarSize; }
      void setVarSize(CharUnits Size) { VarSize = Size; }
      llvm::GlobalValue::LinkageTypes getLinkage() const { return Linkage; }
      void setLinkage(llvm::GlobalValue::LinkageTypes LT) { Linkage = LT; }
      static bool classof(const OffloadEntryInfo *Info) {
        return Info->getKind() == OffloadingEntryInfoDeviceGlobalVar;
      }
    };

    /// Initialize device global variable entry.
    void initializeDeviceGlobalVarEntryInfo(StringRef Name,
                                            OMPTargetGlobalVarEntryKind Flags,
                                            unsigned Order);

    /// Register device global variable entry.
    void
    registerDeviceGlobalVarEntryInfo(StringRef VarName, llvm::Constant *Addr,
                                     CharUnits VarSize,
                                     OMPTargetGlobalVarEntryKind Flags,
                                     llvm::GlobalValue::LinkageTypes Linkage);
    /// Checks if the variable with the given name has been registered already.
    bool hasDeviceGlobalVarEntryInfo(StringRef VarName) const {
      return OffloadEntriesDeviceGlobalVar.count(VarName) > 0;
    }
    /// Applies action \a Action on all registered entries.
    typedef llvm::function_ref<void(StringRef,
                                    const OffloadEntryInfoDeviceGlobalVar &)>
        OffloadDeviceGlobalVarEntryInfoActTy;
    void actOnDeviceGlobalVarEntriesInfo(
        const OffloadDeviceGlobalVarEntryInfoActTy &Action);

  private:
    // Storage for target region entries kind. The storage is to be indexed by
    // file ID, device ID, parent function name and line number.
    typedef llvm::DenseMap<unsigned, OffloadEntryInfoTargetRegion>
        OffloadEntriesTargetRegionPerLine;
    typedef llvm::StringMap<OffloadEntriesTargetRegionPerLine>
        OffloadEntriesTargetRegionPerParentName;
    typedef llvm::DenseMap<unsigned, OffloadEntriesTargetRegionPerParentName>
        OffloadEntriesTargetRegionPerFile;
    typedef llvm::DenseMap<unsigned, OffloadEntriesTargetRegionPerFile>
        OffloadEntriesTargetRegionPerDevice;
    typedef OffloadEntriesTargetRegionPerDevice OffloadEntriesTargetRegionTy;
    OffloadEntriesTargetRegionTy OffloadEntriesTargetRegion;
    /// Storage for device global variable entries kind. The storage is to be
    /// indexed by mangled name.
    typedef llvm::StringMap<OffloadEntryInfoDeviceGlobalVar>
        OffloadEntriesDeviceGlobalVarTy;
    OffloadEntriesDeviceGlobalVarTy OffloadEntriesDeviceGlobalVar;
  };
  OffloadEntriesInfoManagerTy OffloadEntriesInfoManager;

  bool ShouldMarkAsGlobal = true;
  /// List of the emitted functions.
  llvm::StringSet<> AlreadyEmittedTargetFunctions;
  /// List of the global variables with their addresses that should not be
  /// emitted for the target.
  llvm::StringMap<llvm::WeakTrackingVH> EmittedNonTargetVariables;

  /// List of variables that can become declare target implicitly and, thus,
  /// must be emitted.
  llvm::SmallDenseSet<const VarDecl *> DeferredGlobalVariables;

  /// Creates and registers offloading binary descriptor for the current
  /// compilation unit. The function that does the registration is returned.
  llvm::Function *createOffloadingBinaryDescriptorRegistration();

  /// Creates all the offload entries in the current compilation unit
  /// along with the associated metadata.
  void createOffloadEntriesAndInfoMetadata();

  /// Loads all the offload entries information from the host IR
  /// metadata.
  void loadOffloadInfoMetadata();

  /// Returns __tgt_offload_entry type.
  QualType getTgtOffloadEntryQTy();

  /// Returns __tgt_device_image type.
  QualType getTgtDeviceImageQTy();

  /// Returns __tgt_bin_desc type.
  QualType getTgtBinaryDescriptorQTy();

  /// Start scanning from statement \a S and and emit all target regions
  /// found along the way.
  /// \param S Starting statement.
  /// \param ParentName Name of the function declaration that is being scanned.
  void scanForTargetRegionsFunctions(const Stmt *S, StringRef ParentName);

  /// Build type kmp_routine_entry_t (if not built yet).
  void emitKmpRoutineEntryT(QualType KmpInt32Ty);

  /// Returns pointer to kmpc_micro type.
  llvm::Type *getKmpc_MicroPointerTy();

  /// Returns specified OpenMP runtime function.
  /// \param Function OpenMP runtime function.
  /// \return Specified function.
  llvm::Constant *createRuntimeFunction(unsigned Function);

  /// Returns __kmpc_for_static_init_* runtime function for the specified
  /// size \a IVSize and sign \a IVSigned.
  llvm::Constant *createForStaticInitFunction(unsigned IVSize, bool IVSigned);

  /// Returns __kmpc_dispatch_init_* runtime function for the specified
  /// size \a IVSize and sign \a IVSigned.
  llvm::Constant *createDispatchInitFunction(unsigned IVSize, bool IVSigned);

  /// Returns __kmpc_dispatch_next_* runtime function for the specified
  /// size \a IVSize and sign \a IVSigned.
  llvm::Constant *createDispatchNextFunction(unsigned IVSize, bool IVSigned);

  /// Returns __kmpc_dispatch_fini_* runtime function for the specified
  /// size \a IVSize and sign \a IVSigned.
  llvm::Constant *createDispatchFiniFunction(unsigned IVSize, bool IVSigned);

  /// If the specified mangled name is not in the module, create and
  /// return threadprivate cache object. This object is a pointer's worth of
  /// storage that's reserved for use by the OpenMP runtime.
  /// \param VD Threadprivate variable.
  /// \return Cache variable for the specified threadprivate.
  llvm::Constant *getOrCreateThreadPrivateCache(const VarDecl *VD);

  /// Gets (if variable with the given name already exist) or creates
  /// internal global variable with the specified Name. The created variable has
  /// linkage CommonLinkage by default and is initialized by null value.
  /// \param Ty Type of the global variable. If it is exist already the type
  /// must be the same.
  /// \param Name Name of the variable.
  llvm::Constant *getOrCreateInternalVariable(llvm::Type *Ty,
                                              const llvm::Twine &Name);

  /// Set of threadprivate variables with the generated initializer.
  llvm::StringSet<> ThreadPrivateWithDefinition;

  /// Set of declare target variables with the generated initializer.
  llvm::StringSet<> DeclareTargetWithDefinition;

  /// Emits initialization code for the threadprivate variables.
  /// \param VDAddr Address of the global variable \a VD.
  /// \param Ctor Pointer to a global init function for \a VD.
  /// \param CopyCtor Pointer to a global copy function for \a VD.
  /// \param Dtor Pointer to a global destructor function for \a VD.
  /// \param Loc Location of threadprivate declaration.
  void emitThreadPrivateVarInit(CodeGenFunction &CGF, Address VDAddr,
                                llvm::Value *Ctor, llvm::Value *CopyCtor,
                                llvm::Value *Dtor, SourceLocation Loc);

  struct TaskResultTy {
    llvm::Value *NewTask = nullptr;
    llvm::Value *TaskEntry = nullptr;
    llvm::Value *NewTaskNewTaskTTy = nullptr;
    LValue TDBase;
    const RecordDecl *KmpTaskTQTyRD = nullptr;
    llvm::Value *TaskDupFn = nullptr;
  };
  /// Emit task region for the task directive. The task region is emitted in
  /// several steps:
  /// 1. Emit a call to kmp_task_t *__kmpc_omp_task_alloc(ident_t *, kmp_int32
  /// gtid, kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  /// kmp_routine_entry_t *task_entry). Here task_entry is a pointer to the
  /// function:
  /// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
  ///   TaskFunction(gtid, tt->part_id, tt->shareds);
  ///   return 0;
  /// }
  /// 2. Copy a list of shared variables to field shareds of the resulting
  /// structure kmp_task_t returned by the previous call (if any).
  /// 3. Copy a pointer to destructions function to field destructions of the
  /// resulting structure kmp_task_t.
  /// \param D Current task directive.
  /// \param TaskFunction An LLVM function with type void (*)(i32 /*gtid*/, i32
  /// /*part_id*/, captured_struct */*__context*/);
  /// \param SharedsTy A type which contains references the shared variables.
  /// \param Shareds Context with the list of shared variables from the \p
  /// TaskFunction.
  /// \param Data Additional data for task generation like tiednsee, final
  /// state, list of privates etc.
  TaskResultTy emitTaskInit(CodeGenFunction &CGF, SourceLocation Loc,
                            const OMPExecutableDirective &D,
                            llvm::Value *TaskFunction, QualType SharedsTy,
                            Address Shareds, const OMPTaskDataTy &Data);

public:
  explicit CGOpenMPRuntime(CodeGenModule &CGM)
      : CGOpenMPRuntime(CGM, ".", ".") {}
  virtual ~CGOpenMPRuntime() {}
  virtual void clear();

  /// Get the platform-specific name separator.
  std::string getName(ArrayRef<StringRef> Parts) const;

  /// Emit code for the specified user defined reduction construct.
  virtual void emitUserDefinedReduction(CodeGenFunction *CGF,
                                        const OMPDeclareReductionDecl *D);
  /// Get combiner/initializer for the specified user-defined reduction, if any.
  virtual std::pair<llvm::Function *, llvm::Function *>
  getUserDefinedReduction(const OMPDeclareReductionDecl *D);

  /// Emits outlined function for the specified OpenMP parallel directive
  /// \a D. This outlined function has type void(*)(kmp_int32 *ThreadID,
  /// kmp_int32 BoundID, struct context_vars*).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  virtual llvm::Value *emitParallelOutlinedFunction(
      const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
      OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen);

  /// Emits outlined function for the specified OpenMP teams directive
  /// \a D. This outlined function has type void(*)(kmp_int32 *ThreadID,
  /// kmp_int32 BoundID, struct context_vars*).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  virtual llvm::Value *emitTeamsOutlinedFunction(
      const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
      OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen);

  /// Emits outlined function for the OpenMP task directive \a D. This
  /// outlined function has type void(*)(kmp_int32 ThreadID, struct task_t*
  /// TaskT).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param PartIDVar Variable for partition id in the current OpenMP untied
  /// task region.
  /// \param TaskTVar Variable for task_t argument.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  /// \param Tied true if task is generated for tied task, false otherwise.
  /// \param NumberOfParts Number of parts in untied task. Ignored for tied
  /// tasks.
  ///
  virtual llvm::Value *emitTaskOutlinedFunction(
      const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
      const VarDecl *PartIDVar, const VarDecl *TaskTVar,
      OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
      bool Tied, unsigned &NumberOfParts);

  /// Cleans up references to the objects in finished function.
  ///
  virtual void functionFinished(CodeGenFunction &CGF);

  /// Emits code for parallel or serial call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// \param OutlinedFn Outlined function to be run in parallel threads. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  ///
  virtual void emitParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                                llvm::Value *OutlinedFn,
                                ArrayRef<llvm::Value *> CapturedVars,
                                const Expr *IfCond);

  /// Emits a critical region.
  /// \param CriticalName Name of the critical region.
  /// \param CriticalOpGen Generator for the statement associated with the given
  /// critical region.
  /// \param Hint Value of the 'hint' clause (optional).
  virtual void emitCriticalRegion(CodeGenFunction &CGF, StringRef CriticalName,
                                  const RegionCodeGenTy &CriticalOpGen,
                                  SourceLocation Loc,
                                  const Expr *Hint = nullptr);

  /// Emits a master region.
  /// \param MasterOpGen Generator for the statement associated with the given
  /// master region.
  virtual void emitMasterRegion(CodeGenFunction &CGF,
                                const RegionCodeGenTy &MasterOpGen,
                                SourceLocation Loc);

  /// Emits code for a taskyield directive.
  virtual void emitTaskyieldCall(CodeGenFunction &CGF, SourceLocation Loc);

  /// Emit a taskgroup region.
  /// \param TaskgroupOpGen Generator for the statement associated with the
  /// given taskgroup region.
  virtual void emitTaskgroupRegion(CodeGenFunction &CGF,
                                   const RegionCodeGenTy &TaskgroupOpGen,
                                   SourceLocation Loc);

  /// Emits a single region.
  /// \param SingleOpGen Generator for the statement associated with the given
  /// single region.
  virtual void emitSingleRegion(CodeGenFunction &CGF,
                                const RegionCodeGenTy &SingleOpGen,
                                SourceLocation Loc,
                                ArrayRef<const Expr *> CopyprivateVars,
                                ArrayRef<const Expr *> DestExprs,
                                ArrayRef<const Expr *> SrcExprs,
                                ArrayRef<const Expr *> AssignmentOps);

  /// Emit an ordered region.
  /// \param OrderedOpGen Generator for the statement associated with the given
  /// ordered region.
  virtual void emitOrderedRegion(CodeGenFunction &CGF,
                                 const RegionCodeGenTy &OrderedOpGen,
                                 SourceLocation Loc, bool IsThreads);

  /// Emit an implicit/explicit barrier for OpenMP threads.
  /// \param Kind Directive for which this implicit barrier call must be
  /// generated. Must be OMPD_barrier for explicit barrier generation.
  /// \param EmitChecks true if need to emit checks for cancellation barriers.
  /// \param ForceSimpleCall true simple barrier call must be emitted, false if
  /// runtime class decides which one to emit (simple or with cancellation
  /// checks).
  ///
  virtual void emitBarrierCall(CodeGenFunction &CGF, SourceLocation Loc,
                               OpenMPDirectiveKind Kind,
                               bool EmitChecks = true,
                               bool ForceSimpleCall = false);

  /// Check if the specified \a ScheduleKind is static non-chunked.
  /// This kind of worksharing directive is emitted without outer loop.
  /// \param ScheduleKind Schedule kind specified in the 'schedule' clause.
  /// \param Chunked True if chunk is specified in the clause.
  ///
  virtual bool isStaticNonchunked(OpenMPScheduleClauseKind ScheduleKind,
                                  bool Chunked) const;

  /// Check if the specified \a ScheduleKind is static non-chunked.
  /// This kind of distribute directive is emitted without outer loop.
  /// \param ScheduleKind Schedule kind specified in the 'dist_schedule' clause.
  /// \param Chunked True if chunk is specified in the clause.
  ///
  virtual bool isStaticNonchunked(OpenMPDistScheduleClauseKind ScheduleKind,
                                  bool Chunked) const;

  /// Check if the specified \a ScheduleKind is static chunked.
  /// \param ScheduleKind Schedule kind specified in the 'schedule' clause.
  /// \param Chunked True if chunk is specified in the clause.
  ///
  virtual bool isStaticChunked(OpenMPScheduleClauseKind ScheduleKind,
                               bool Chunked) const;

  /// Check if the specified \a ScheduleKind is static non-chunked.
  /// \param ScheduleKind Schedule kind specified in the 'dist_schedule' clause.
  /// \param Chunked True if chunk is specified in the clause.
  ///
  virtual bool isStaticChunked(OpenMPDistScheduleClauseKind ScheduleKind,
                               bool Chunked) const;

  /// Check if the specified \a ScheduleKind is dynamic.
  /// This kind of worksharing directive is emitted without outer loop.
  /// \param ScheduleKind Schedule Kind specified in the 'schedule' clause.
  ///
  virtual bool isDynamic(OpenMPScheduleClauseKind ScheduleKind) const;

  /// struct with the values to be passed to the dispatch runtime function
  struct DispatchRTInput {
    /// Loop lower bound
    llvm::Value *LB = nullptr;
    /// Loop upper bound
    llvm::Value *UB = nullptr;
    /// Chunk size specified using 'schedule' clause (nullptr if chunk
    /// was not specified)
    llvm::Value *Chunk = nullptr;
    DispatchRTInput() = default;
    DispatchRTInput(llvm::Value *LB, llvm::Value *UB, llvm::Value *Chunk)
        : LB(LB), UB(UB), Chunk(Chunk) {}
  };

  /// Call the appropriate runtime routine to initialize it before start
  /// of loop.

  /// This is used for non static scheduled types and when the ordered
  /// clause is present on the loop construct.
  /// Depending on the loop schedule, it is necessary to call some runtime
  /// routine before start of the OpenMP loop to get the loop upper / lower
  /// bounds \a LB and \a UB and stride \a ST.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param ScheduleKind Schedule kind, specified by the 'schedule' clause.
  /// \param IVSize Size of the iteration variable in bits.
  /// \param IVSigned Sign of the iteration variable.
  /// \param Ordered true if loop is ordered, false otherwise.
  /// \param DispatchValues struct containing llvm values for lower bound, upper
  /// bound, and chunk expression.
  /// For the default (nullptr) value, the chunk 1 will be used.
  ///
  virtual void emitForDispatchInit(CodeGenFunction &CGF, SourceLocation Loc,
                                   const OpenMPScheduleTy &ScheduleKind,
                                   unsigned IVSize, bool IVSigned, bool Ordered,
                                   const DispatchRTInput &DispatchValues);

  /// Struct with the values to be passed to the static runtime function
  struct StaticRTInput {
    /// Size of the iteration variable in bits.
    unsigned IVSize = 0;
    /// Sign of the iteration variable.
    bool IVSigned = false;
    /// true if loop is ordered, false otherwise.
    bool Ordered = false;
    /// Address of the output variable in which the flag of the last iteration
    /// is returned.
    Address IL = Address::invalid();
    /// Address of the output variable in which the lower iteration number is
    /// returned.
    Address LB = Address::invalid();
    /// Address of the output variable in which the upper iteration number is
    /// returned.
    Address UB = Address::invalid();
    /// Address of the output variable in which the stride value is returned
    /// necessary to generated the static_chunked scheduled loop.
    Address ST = Address::invalid();
    /// Value of the chunk for the static_chunked scheduled loop. For the
    /// default (nullptr) value, the chunk 1 will be used.
    llvm::Value *Chunk = nullptr;
    StaticRTInput(unsigned IVSize, bool IVSigned, bool Ordered, Address IL,
                  Address LB, Address UB, Address ST,
                  llvm::Value *Chunk = nullptr)
        : IVSize(IVSize), IVSigned(IVSigned), Ordered(Ordered), IL(IL), LB(LB),
          UB(UB), ST(ST), Chunk(Chunk) {}
  };
  /// Call the appropriate runtime routine to initialize it before start
  /// of loop.
  ///
  /// This is used only in case of static schedule, when the user did not
  /// specify a ordered clause on the loop construct.
  /// Depending on the loop schedule, it is necessary to call some runtime
  /// routine before start of the OpenMP loop to get the loop upper / lower
  /// bounds LB and UB and stride ST.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param DKind Kind of the directive.
  /// \param ScheduleKind Schedule kind, specified by the 'schedule' clause.
  /// \param Values Input arguments for the construct.
  ///
  virtual void emitForStaticInit(CodeGenFunction &CGF, SourceLocation Loc,
                                 OpenMPDirectiveKind DKind,
                                 const OpenMPScheduleTy &ScheduleKind,
                                 const StaticRTInput &Values);

  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param SchedKind Schedule kind, specified by the 'dist_schedule' clause.
  /// \param Values Input arguments for the construct.
  ///
  virtual void emitDistributeStaticInit(CodeGenFunction &CGF,
                                        SourceLocation Loc,
                                        OpenMPDistScheduleClauseKind SchedKind,
                                        const StaticRTInput &Values);

  /// Call the appropriate runtime routine to notify that we finished
  /// iteration of the ordered loop with the dynamic scheduling.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param IVSize Size of the iteration variable in bits.
  /// \param IVSigned Sign of the iteration variable.
  ///
  virtual void emitForOrderedIterationEnd(CodeGenFunction &CGF,
                                          SourceLocation Loc, unsigned IVSize,
                                          bool IVSigned);

  /// Call the appropriate runtime routine to notify that we finished
  /// all the work with current loop.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param DKind Kind of the directive for which the static finish is emitted.
  ///
  virtual void emitForStaticFinish(CodeGenFunction &CGF, SourceLocation Loc,
                                   OpenMPDirectiveKind DKind);

  /// Call __kmpc_dispatch_next(
  ///          ident_t *loc, kmp_int32 tid, kmp_int32 *p_lastiter,
  ///          kmp_int[32|64] *p_lower, kmp_int[32|64] *p_upper,
  ///          kmp_int[32|64] *p_stride);
  /// \param IVSize Size of the iteration variable in bits.
  /// \param IVSigned Sign of the iteration variable.
  /// \param IL Address of the output variable in which the flag of the
  /// last iteration is returned.
  /// \param LB Address of the output variable in which the lower iteration
  /// number is returned.
  /// \param UB Address of the output variable in which the upper iteration
  /// number is returned.
  /// \param ST Address of the output variable in which the stride value is
  /// returned.
  virtual llvm::Value *emitForNext(CodeGenFunction &CGF, SourceLocation Loc,
                                   unsigned IVSize, bool IVSigned,
                                   Address IL, Address LB,
                                   Address UB, Address ST);

  /// Emits call to void __kmpc_push_num_threads(ident_t *loc, kmp_int32
  /// global_tid, kmp_int32 num_threads) to generate code for 'num_threads'
  /// clause.
  /// \param NumThreads An integer value of threads.
  virtual void emitNumThreadsClause(CodeGenFunction &CGF,
                                    llvm::Value *NumThreads,
                                    SourceLocation Loc);

  /// Emit call to void __kmpc_push_proc_bind(ident_t *loc, kmp_int32
  /// global_tid, int proc_bind) to generate code for 'proc_bind' clause.
  virtual void emitProcBindClause(CodeGenFunction &CGF,
                                  OpenMPProcBindClauseKind ProcBind,
                                  SourceLocation Loc);

  /// Returns address of the threadprivate variable for the current
  /// thread.
  /// \param VD Threadprivate variable.
  /// \param VDAddr Address of the global variable \a VD.
  /// \param Loc Location of the reference to threadprivate var.
  /// \return Address of the threadprivate variable for the current thread.
  virtual Address getAddrOfThreadPrivate(CodeGenFunction &CGF,
                                         const VarDecl *VD,
                                         Address VDAddr,
                                         SourceLocation Loc);

  /// Returns the address of the variable marked as declare target with link
  /// clause.
  virtual Address getAddrOfDeclareTargetLink(const VarDecl *VD);

  /// Emit a code for initialization of threadprivate variable. It emits
  /// a call to runtime library which adds initial value to the newly created
  /// threadprivate variable (if it is not constant) and registers destructor
  /// for the variable (if any).
  /// \param VD Threadprivate variable.
  /// \param VDAddr Address of the global variable \a VD.
  /// \param Loc Location of threadprivate declaration.
  /// \param PerformInit true if initialization expression is not constant.
  virtual llvm::Function *
  emitThreadPrivateVarDefinition(const VarDecl *VD, Address VDAddr,
                                 SourceLocation Loc, bool PerformInit,
                                 CodeGenFunction *CGF = nullptr);

  /// Emit a code for initialization of declare target variable.
  /// \param VD Declare target variable.
  /// \param Addr Address of the global variable \a VD.
  /// \param PerformInit true if initialization expression is not constant.
  virtual bool emitDeclareTargetVarDefinition(const VarDecl *VD,
                                              llvm::GlobalVariable *Addr,
                                              bool PerformInit);

  /// Creates artificial threadprivate variable with name \p Name and type \p
  /// VarType.
  /// \param VarType Type of the artificial threadprivate variable.
  /// \param Name Name of the artificial threadprivate variable.
  virtual Address getAddrOfArtificialThreadPrivate(CodeGenFunction &CGF,
                                                   QualType VarType,
                                                   StringRef Name);

  /// Emit flush of the variables specified in 'omp flush' directive.
  /// \param Vars List of variables to flush.
  virtual void emitFlush(CodeGenFunction &CGF, ArrayRef<const Expr *> Vars,
                         SourceLocation Loc);

  /// Emit task region for the task directive. The task region is
  /// emitted in several steps:
  /// 1. Emit a call to kmp_task_t *__kmpc_omp_task_alloc(ident_t *, kmp_int32
  /// gtid, kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  /// kmp_routine_entry_t *task_entry). Here task_entry is a pointer to the
  /// function:
  /// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
  ///   TaskFunction(gtid, tt->part_id, tt->shareds);
  ///   return 0;
  /// }
  /// 2. Copy a list of shared variables to field shareds of the resulting
  /// structure kmp_task_t returned by the previous call (if any).
  /// 3. Copy a pointer to destructions function to field destructions of the
  /// resulting structure kmp_task_t.
  /// 4. Emit a call to kmp_int32 __kmpc_omp_task(ident_t *, kmp_int32 gtid,
  /// kmp_task_t *new_task), where new_task is a resulting structure from
  /// previous items.
  /// \param D Current task directive.
  /// \param TaskFunction An LLVM function with type void (*)(i32 /*gtid*/, i32
  /// /*part_id*/, captured_struct */*__context*/);
  /// \param SharedsTy A type which contains references the shared variables.
  /// \param Shareds Context with the list of shared variables from the \p
  /// TaskFunction.
  /// \param IfCond Not a nullptr if 'if' clause was specified, nullptr
  /// otherwise.
  /// \param Data Additional data for task generation like tiednsee, final
  /// state, list of privates etc.
  virtual void emitTaskCall(CodeGenFunction &CGF, SourceLocation Loc,
                            const OMPExecutableDirective &D,
                            llvm::Value *TaskFunction, QualType SharedsTy,
                            Address Shareds, const Expr *IfCond,
                            const OMPTaskDataTy &Data);

  /// Emit task region for the taskloop directive. The taskloop region is
  /// emitted in several steps:
  /// 1. Emit a call to kmp_task_t *__kmpc_omp_task_alloc(ident_t *, kmp_int32
  /// gtid, kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  /// kmp_routine_entry_t *task_entry). Here task_entry is a pointer to the
  /// function:
  /// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
  ///   TaskFunction(gtid, tt->part_id, tt->shareds);
  ///   return 0;
  /// }
  /// 2. Copy a list of shared variables to field shareds of the resulting
  /// structure kmp_task_t returned by the previous call (if any).
  /// 3. Copy a pointer to destructions function to field destructions of the
  /// resulting structure kmp_task_t.
  /// 4. Emit a call to void __kmpc_taskloop(ident_t *loc, int gtid, kmp_task_t
  /// *task, int if_val, kmp_uint64 *lb, kmp_uint64 *ub, kmp_int64 st, int
  /// nogroup, int sched, kmp_uint64 grainsize, void *task_dup ), where new_task
  /// is a resulting structure from
  /// previous items.
  /// \param D Current task directive.
  /// \param TaskFunction An LLVM function with type void (*)(i32 /*gtid*/, i32
  /// /*part_id*/, captured_struct */*__context*/);
  /// \param SharedsTy A type which contains references the shared variables.
  /// \param Shareds Context with the list of shared variables from the \p
  /// TaskFunction.
  /// \param IfCond Not a nullptr if 'if' clause was specified, nullptr
  /// otherwise.
  /// \param Data Additional data for task generation like tiednsee, final
  /// state, list of privates etc.
  virtual void emitTaskLoopCall(
      CodeGenFunction &CGF, SourceLocation Loc, const OMPLoopDirective &D,
      llvm::Value *TaskFunction, QualType SharedsTy, Address Shareds,
      const Expr *IfCond, const OMPTaskDataTy &Data);

  /// Emit code for the directive that does not require outlining.
  ///
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  /// \param HasCancel true if region has inner cancel directive, false
  /// otherwise.
  virtual void emitInlinedDirective(CodeGenFunction &CGF,
                                    OpenMPDirectiveKind InnermostKind,
                                    const RegionCodeGenTy &CodeGen,
                                    bool HasCancel = false);

  /// Emits reduction function.
  /// \param ArgsType Array type containing pointers to reduction variables.
  /// \param Privates List of private copies for original reduction arguments.
  /// \param LHSExprs List of LHS in \a ReductionOps reduction operations.
  /// \param RHSExprs List of RHS in \a ReductionOps reduction operations.
  /// \param ReductionOps List of reduction operations in form 'LHS binop RHS'
  /// or 'operator binop(LHS, RHS)'.
  llvm::Value *emitReductionFunction(CodeGenModule &CGM, SourceLocation Loc,
                                     llvm::Type *ArgsType,
                                     ArrayRef<const Expr *> Privates,
                                     ArrayRef<const Expr *> LHSExprs,
                                     ArrayRef<const Expr *> RHSExprs,
                                     ArrayRef<const Expr *> ReductionOps);

  /// Emits single reduction combiner
  void emitSingleReductionCombiner(CodeGenFunction &CGF,
                                   const Expr *ReductionOp,
                                   const Expr *PrivateRef,
                                   const DeclRefExpr *LHS,
                                   const DeclRefExpr *RHS);

  struct ReductionOptionsTy {
    bool WithNowait;
    bool SimpleReduction;
    OpenMPDirectiveKind ReductionKind;
  };
  /// Emit a code for reduction clause. Next code should be emitted for
  /// reduction:
  /// \code
  ///
  /// static kmp_critical_name lock = { 0 };
  ///
  /// void reduce_func(void *lhs[<n>], void *rhs[<n>]) {
  ///  ...
  ///  *(Type<i>*)lhs[i] = RedOp<i>(*(Type<i>*)lhs[i], *(Type<i>*)rhs[i]);
  ///  ...
  /// }
  ///
  /// ...
  /// void *RedList[<n>] = {&<RHSExprs>[0], ..., &<RHSExprs>[<n>-1]};
  /// switch (__kmpc_reduce{_nowait}(<loc>, <gtid>, <n>, sizeof(RedList),
  /// RedList, reduce_func, &<lock>)) {
  /// case 1:
  ///  ...
  ///  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  ///  ...
  /// __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  /// break;
  /// case 2:
  ///  ...
  ///  Atomic(<LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]));
  ///  ...
  /// break;
  /// default:;
  /// }
  /// \endcode
  ///
  /// \param Privates List of private copies for original reduction arguments.
  /// \param LHSExprs List of LHS in \a ReductionOps reduction operations.
  /// \param RHSExprs List of RHS in \a ReductionOps reduction operations.
  /// \param ReductionOps List of reduction operations in form 'LHS binop RHS'
  /// or 'operator binop(LHS, RHS)'.
  /// \param Options List of options for reduction codegen:
  ///     WithNowait true if parent directive has also nowait clause, false
  ///     otherwise.
  ///     SimpleReduction Emit reduction operation only. Used for omp simd
  ///     directive on the host.
  ///     ReductionKind The kind of reduction to perform.
  virtual void emitReduction(CodeGenFunction &CGF, SourceLocation Loc,
                             ArrayRef<const Expr *> Privates,
                             ArrayRef<const Expr *> LHSExprs,
                             ArrayRef<const Expr *> RHSExprs,
                             ArrayRef<const Expr *> ReductionOps,
                             ReductionOptionsTy Options);

  /// Emit a code for initialization of task reduction clause. Next code
  /// should be emitted for reduction:
  /// \code
  ///
  /// _task_red_item_t red_data[n];
  /// ...
  /// red_data[i].shar = &origs[i];
  /// red_data[i].size = sizeof(origs[i]);
  /// red_data[i].f_init = (void*)RedInit<i>;
  /// red_data[i].f_fini = (void*)RedDest<i>;
  /// red_data[i].f_comb = (void*)RedOp<i>;
  /// red_data[i].flags = <Flag_i>;
  /// ...
  /// void* tg1 = __kmpc_task_reduction_init(gtid, n, red_data);
  /// \endcode
  ///
  /// \param LHSExprs List of LHS in \a Data.ReductionOps reduction operations.
  /// \param RHSExprs List of RHS in \a Data.ReductionOps reduction operations.
  /// \param Data Additional data for task generation like tiedness, final
  /// state, list of privates, reductions etc.
  virtual llvm::Value *emitTaskReductionInit(CodeGenFunction &CGF,
                                             SourceLocation Loc,
                                             ArrayRef<const Expr *> LHSExprs,
                                             ArrayRef<const Expr *> RHSExprs,
                                             const OMPTaskDataTy &Data);

  /// Required to resolve existing problems in the runtime. Emits threadprivate
  /// variables to store the size of the VLAs/array sections for
  /// initializer/combiner/finalizer functions + emits threadprivate variable to
  /// store the pointer to the original reduction item for the custom
  /// initializer defined by declare reduction construct.
  /// \param RCG Allows to reuse an existing data for the reductions.
  /// \param N Reduction item for which fixups must be emitted.
  virtual void emitTaskReductionFixups(CodeGenFunction &CGF, SourceLocation Loc,
                                       ReductionCodeGen &RCG, unsigned N);

  /// Get the address of `void *` type of the privatue copy of the reduction
  /// item specified by the \p SharedLVal.
  /// \param ReductionsPtr Pointer to the reduction data returned by the
  /// emitTaskReductionInit function.
  /// \param SharedLVal Address of the original reduction item.
  virtual Address getTaskReductionItem(CodeGenFunction &CGF, SourceLocation Loc,
                                       llvm::Value *ReductionsPtr,
                                       LValue SharedLVal);

  /// Emit code for 'taskwait' directive.
  virtual void emitTaskwaitCall(CodeGenFunction &CGF, SourceLocation Loc);

  /// Emit code for 'cancellation point' construct.
  /// \param CancelRegion Region kind for which the cancellation point must be
  /// emitted.
  ///
  virtual void emitCancellationPointCall(CodeGenFunction &CGF,
                                         SourceLocation Loc,
                                         OpenMPDirectiveKind CancelRegion);

  /// Emit code for 'cancel' construct.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  /// \param CancelRegion Region kind for which the cancel must be emitted.
  ///
  virtual void emitCancelCall(CodeGenFunction &CGF, SourceLocation Loc,
                              const Expr *IfCond,
                              OpenMPDirectiveKind CancelRegion);

  /// Emit outilined function for 'target' directive.
  /// \param D Directive to emit.
  /// \param ParentName Name of the function that encloses the target region.
  /// \param OutlinedFn Outlined function value to be defined by this call.
  /// \param OutlinedFnID Outlined function ID value to be defined by this call.
  /// \param IsOffloadEntry True if the outlined function is an offload entry.
  /// \param CodeGen Code generation sequence for the \a D directive.
  /// An outlined function may not be an entry if, e.g. the if clause always
  /// evaluates to false.
  virtual void emitTargetOutlinedFunction(const OMPExecutableDirective &D,
                                          StringRef ParentName,
                                          llvm::Function *&OutlinedFn,
                                          llvm::Constant *&OutlinedFnID,
                                          bool IsOffloadEntry,
                                          const RegionCodeGenTy &CodeGen);

  /// Emit code that pushes the trip count of loops associated with constructs
  /// 'target teams distribute' and 'teams distribute parallel for'.
  /// \param SizeEmitter Emits the int64 value for the number of iterations of
  /// the associated loop.
  virtual void emitTargetNumIterationsCall(
      CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *Device,
      const llvm::function_ref<llvm::Value *(
          CodeGenFunction &CGF, const OMPLoopDirective &D)> &SizeEmitter);

  /// Emit the target offloading code associated with \a D. The emitted
  /// code attempts offloading the execution to the device, an the event of
  /// a failure it executes the host version outlined in \a OutlinedFn.
  /// \param D Directive to emit.
  /// \param OutlinedFn Host version of the code to be offloaded.
  /// \param OutlinedFnID ID of host version of the code to be offloaded.
  /// \param IfCond Expression evaluated in if clause associated with the target
  /// directive, or null if no if clause is used.
  /// \param Device Expression evaluated in device clause associated with the
  /// target directive, or null if no device clause is used.
  virtual void emitTargetCall(CodeGenFunction &CGF,
                              const OMPExecutableDirective &D,
                              llvm::Value *OutlinedFn,
                              llvm::Value *OutlinedFnID, const Expr *IfCond,
                              const Expr *Device);

  /// Emit the target regions enclosed in \a GD function definition or
  /// the function itself in case it is a valid device function. Returns true if
  /// \a GD was dealt with successfully.
  /// \param GD Function to scan.
  virtual bool emitTargetFunctions(GlobalDecl GD);

  /// Emit the global variable if it is a valid device global variable.
  /// Returns true if \a GD was dealt with successfully.
  /// \param GD Variable declaration to emit.
  virtual bool emitTargetGlobalVariable(GlobalDecl GD);

  /// Checks if the provided global decl \a GD is a declare target variable and
  /// registers it when emitting code for the host.
  virtual void registerTargetGlobalVariable(const VarDecl *VD,
                                            llvm::Constant *Addr);

  /// Emit the global \a GD if it is meaningful for the target. Returns
  /// if it was emitted successfully.
  /// \param GD Global to scan.
  virtual bool emitTargetGlobal(GlobalDecl GD);

  /// Creates the offloading descriptor in the event any target region
  /// was emitted in the current module and return the function that registers
  /// it.
  virtual llvm::Function *emitRegistrationFunction();

  /// Emits code for teams call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// \param OutlinedFn Outlined function to be run by team masters. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  ///
  virtual void emitTeamsCall(CodeGenFunction &CGF,
                             const OMPExecutableDirective &D,
                             SourceLocation Loc, llvm::Value *OutlinedFn,
                             ArrayRef<llvm::Value *> CapturedVars);

  /// Emits call to void __kmpc_push_num_teams(ident_t *loc, kmp_int32
  /// global_tid, kmp_int32 num_teams, kmp_int32 thread_limit) to generate code
  /// for num_teams clause.
  /// \param NumTeams An integer expression of teams.
  /// \param ThreadLimit An integer expression of threads.
  virtual void emitNumTeamsClause(CodeGenFunction &CGF, const Expr *NumTeams,
                                  const Expr *ThreadLimit, SourceLocation Loc);

  /// Struct that keeps all the relevant information that should be kept
  /// throughout a 'target data' region.
  class TargetDataInfo {
    /// Set to true if device pointer information have to be obtained.
    bool RequiresDevicePointerInfo = false;

  public:
    /// The array of base pointer passed to the runtime library.
    llvm::Value *BasePointersArray = nullptr;
    /// The array of section pointers passed to the runtime library.
    llvm::Value *PointersArray = nullptr;
    /// The array of sizes passed to the runtime library.
    llvm::Value *SizesArray = nullptr;
    /// The array of map types passed to the runtime library.
    llvm::Value *MapTypesArray = nullptr;
    /// The total number of pointers passed to the runtime library.
    unsigned NumberOfPtrs = 0u;
    /// Map between the a declaration of a capture and the corresponding base
    /// pointer address where the runtime returns the device pointers.
    llvm::DenseMap<const ValueDecl *, Address> CaptureDeviceAddrMap;

    explicit TargetDataInfo() {}
    explicit TargetDataInfo(bool RequiresDevicePointerInfo)
        : RequiresDevicePointerInfo(RequiresDevicePointerInfo) {}
    /// Clear information about the data arrays.
    void clearArrayInfo() {
      BasePointersArray = nullptr;
      PointersArray = nullptr;
      SizesArray = nullptr;
      MapTypesArray = nullptr;
      NumberOfPtrs = 0u;
    }
    /// Return true if the current target data information has valid arrays.
    bool isValid() {
      return BasePointersArray && PointersArray && SizesArray &&
             MapTypesArray && NumberOfPtrs;
    }
    bool requiresDevicePointerInfo() { return RequiresDevicePointerInfo; }
  };

  /// Emit the target data mapping code associated with \a D.
  /// \param D Directive to emit.
  /// \param IfCond Expression evaluated in if clause associated with the
  /// target directive, or null if no device clause is used.
  /// \param Device Expression evaluated in device clause associated with the
  /// target directive, or null if no device clause is used.
  /// \param Info A record used to store information that needs to be preserved
  /// until the region is closed.
  virtual void emitTargetDataCalls(CodeGenFunction &CGF,
                                   const OMPExecutableDirective &D,
                                   const Expr *IfCond, const Expr *Device,
                                   const RegionCodeGenTy &CodeGen,
                                   TargetDataInfo &Info);

  /// Emit the data mapping/movement code associated with the directive
  /// \a D that should be of the form 'target [{enter|exit} data | update]'.
  /// \param D Directive to emit.
  /// \param IfCond Expression evaluated in if clause associated with the target
  /// directive, or null if no if clause is used.
  /// \param Device Expression evaluated in device clause associated with the
  /// target directive, or null if no device clause is used.
  virtual void emitTargetDataStandAloneCall(CodeGenFunction &CGF,
                                            const OMPExecutableDirective &D,
                                            const Expr *IfCond,
                                            const Expr *Device);

  /// Marks function \a Fn with properly mangled versions of vector functions.
  /// \param FD Function marked as 'declare simd'.
  /// \param Fn LLVM function that must be marked with 'declare simd'
  /// attributes.
  virtual void emitDeclareSimdFunction(const FunctionDecl *FD,
                                       llvm::Function *Fn);

  /// Emit initialization for doacross loop nesting support.
  /// \param D Loop-based construct used in doacross nesting construct.
  virtual void emitDoacrossInit(CodeGenFunction &CGF, const OMPLoopDirective &D,
                                ArrayRef<Expr *> NumIterations);

  /// Emit code for doacross ordered directive with 'depend' clause.
  /// \param C 'depend' clause with 'sink|source' dependency kind.
  virtual void emitDoacrossOrdered(CodeGenFunction &CGF,
                                   const OMPDependClause *C);

  /// Translates the native parameter of outlined function if this is required
  /// for target.
  /// \param FD Field decl from captured record for the parameter.
  /// \param NativeParam Parameter itself.
  virtual const VarDecl *translateParameter(const FieldDecl *FD,
                                            const VarDecl *NativeParam) const {
    return NativeParam;
  }

  /// Gets the address of the native argument basing on the address of the
  /// target-specific parameter.
  /// \param NativeParam Parameter itself.
  /// \param TargetParam Corresponding target-specific parameter.
  virtual Address getParameterAddress(CodeGenFunction &CGF,
                                      const VarDecl *NativeParam,
                                      const VarDecl *TargetParam) const;

  /// Choose default schedule type and chunk value for the
  /// dist_schedule clause.
  virtual void getDefaultDistScheduleAndChunk(CodeGenFunction &CGF,
      const OMPLoopDirective &S, OpenMPDistScheduleClauseKind &ScheduleKind,
      llvm::Value *&Chunk) const {}

  /// Choose default schedule type and chunk value for the
  /// schedule clause.
  virtual void getDefaultScheduleAndChunk(CodeGenFunction &CGF,
      const OMPLoopDirective &S, OpenMPScheduleClauseKind &ScheduleKind,
      const Expr *&ChunkExpr) const {}

  /// Emits call of the outlined function with the provided arguments,
  /// translating these arguments to correct target-specific arguments.
  virtual void
  emitOutlinedFunctionCall(CodeGenFunction &CGF, SourceLocation Loc,
                           llvm::Value *OutlinedFn,
                           ArrayRef<llvm::Value *> Args = llvm::None) const;

  /// Emits OpenMP-specific function prolog.
  /// Required for device constructs.
  virtual void emitFunctionProlog(CodeGenFunction &CGF, const Decl *D) {}

  /// Gets the OpenMP-specific address of the local variable.
  virtual Address getAddressOfLocalVariable(CodeGenFunction &CGF,
                                            const VarDecl *VD);

  /// Marks the declaration as already emitted for the device code and returns
  /// true, if it was marked already, and false, otherwise.
  bool markAsGlobalTarget(GlobalDecl GD);

  /// Emit deferred declare target variables marked for deferred emission.
  void emitDeferredTargetDecls() const;

  /// Adjust some parameters for the target-based directives, like addresses of
  /// the variables captured by reference in lambdas.
  virtual void
  adjustTargetSpecificDataForLambdas(CodeGenFunction &CGF,
                                     const OMPExecutableDirective &D) const;

  /// Perform check on requires decl to ensure that target architecture
  /// supports unified addressing
  virtual void checkArchForUnifiedAddressing(CodeGenModule &CGM,
                                             const OMPRequiresDecl *D) const {}
};

/// Class supports emissionof SIMD-only code.
class CGOpenMPSIMDRuntime final : public CGOpenMPRuntime {
public:
  explicit CGOpenMPSIMDRuntime(CodeGenModule &CGM) : CGOpenMPRuntime(CGM) {}
  ~CGOpenMPSIMDRuntime() override {}

  /// Emits outlined function for the specified OpenMP parallel directive
  /// \a D. This outlined function has type void(*)(kmp_int32 *ThreadID,
  /// kmp_int32 BoundID, struct context_vars*).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  llvm::Value *
  emitParallelOutlinedFunction(const OMPExecutableDirective &D,
                               const VarDecl *ThreadIDVar,
                               OpenMPDirectiveKind InnermostKind,
                               const RegionCodeGenTy &CodeGen) override;

  /// Emits outlined function for the specified OpenMP teams directive
  /// \a D. This outlined function has type void(*)(kmp_int32 *ThreadID,
  /// kmp_int32 BoundID, struct context_vars*).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  llvm::Value *
  emitTeamsOutlinedFunction(const OMPExecutableDirective &D,
                            const VarDecl *ThreadIDVar,
                            OpenMPDirectiveKind InnermostKind,
                            const RegionCodeGenTy &CodeGen) override;

  /// Emits outlined function for the OpenMP task directive \a D. This
  /// outlined function has type void(*)(kmp_int32 ThreadID, struct task_t*
  /// TaskT).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param PartIDVar Variable for partition id in the current OpenMP untied
  /// task region.
  /// \param TaskTVar Variable for task_t argument.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  /// \param Tied true if task is generated for tied task, false otherwise.
  /// \param NumberOfParts Number of parts in untied task. Ignored for tied
  /// tasks.
  ///
  llvm::Value *emitTaskOutlinedFunction(
      const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
      const VarDecl *PartIDVar, const VarDecl *TaskTVar,
      OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
      bool Tied, unsigned &NumberOfParts) override;

  /// Emits code for parallel or serial call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// \param OutlinedFn Outlined function to be run in parallel threads. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  ///
  void emitParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                        llvm::Value *OutlinedFn,
                        ArrayRef<llvm::Value *> CapturedVars,
                        const Expr *IfCond) override;

  /// Emits a critical region.
  /// \param CriticalName Name of the critical region.
  /// \param CriticalOpGen Generator for the statement associated with the given
  /// critical region.
  /// \param Hint Value of the 'hint' clause (optional).
  void emitCriticalRegion(CodeGenFunction &CGF, StringRef CriticalName,
                          const RegionCodeGenTy &CriticalOpGen,
                          SourceLocation Loc,
                          const Expr *Hint = nullptr) override;

  /// Emits a master region.
  /// \param MasterOpGen Generator for the statement associated with the given
  /// master region.
  void emitMasterRegion(CodeGenFunction &CGF,
                        const RegionCodeGenTy &MasterOpGen,
                        SourceLocation Loc) override;

  /// Emits code for a taskyield directive.
  void emitTaskyieldCall(CodeGenFunction &CGF, SourceLocation Loc) override;

  /// Emit a taskgroup region.
  /// \param TaskgroupOpGen Generator for the statement associated with the
  /// given taskgroup region.
  void emitTaskgroupRegion(CodeGenFunction &CGF,
                           const RegionCodeGenTy &TaskgroupOpGen,
                           SourceLocation Loc) override;

  /// Emits a single region.
  /// \param SingleOpGen Generator for the statement associated with the given
  /// single region.
  void emitSingleRegion(CodeGenFunction &CGF,
                        const RegionCodeGenTy &SingleOpGen, SourceLocation Loc,
                        ArrayRef<const Expr *> CopyprivateVars,
                        ArrayRef<const Expr *> DestExprs,
                        ArrayRef<const Expr *> SrcExprs,
                        ArrayRef<const Expr *> AssignmentOps) override;

  /// Emit an ordered region.
  /// \param OrderedOpGen Generator for the statement associated with the given
  /// ordered region.
  void emitOrderedRegion(CodeGenFunction &CGF,
                         const RegionCodeGenTy &OrderedOpGen,
                         SourceLocation Loc, bool IsThreads) override;

  /// Emit an implicit/explicit barrier for OpenMP threads.
  /// \param Kind Directive for which this implicit barrier call must be
  /// generated. Must be OMPD_barrier for explicit barrier generation.
  /// \param EmitChecks true if need to emit checks for cancellation barriers.
  /// \param ForceSimpleCall true simple barrier call must be emitted, false if
  /// runtime class decides which one to emit (simple or with cancellation
  /// checks).
  ///
  void emitBarrierCall(CodeGenFunction &CGF, SourceLocation Loc,
                       OpenMPDirectiveKind Kind, bool EmitChecks = true,
                       bool ForceSimpleCall = false) override;

  /// This is used for non static scheduled types and when the ordered
  /// clause is present on the loop construct.
  /// Depending on the loop schedule, it is necessary to call some runtime
  /// routine before start of the OpenMP loop to get the loop upper / lower
  /// bounds \a LB and \a UB and stride \a ST.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param ScheduleKind Schedule kind, specified by the 'schedule' clause.
  /// \param IVSize Size of the iteration variable in bits.
  /// \param IVSigned Sign of the iteration variable.
  /// \param Ordered true if loop is ordered, false otherwise.
  /// \param DispatchValues struct containing llvm values for lower bound, upper
  /// bound, and chunk expression.
  /// For the default (nullptr) value, the chunk 1 will be used.
  ///
  void emitForDispatchInit(CodeGenFunction &CGF, SourceLocation Loc,
                           const OpenMPScheduleTy &ScheduleKind,
                           unsigned IVSize, bool IVSigned, bool Ordered,
                           const DispatchRTInput &DispatchValues) override;

  /// Call the appropriate runtime routine to initialize it before start
  /// of loop.
  ///
  /// This is used only in case of static schedule, when the user did not
  /// specify a ordered clause on the loop construct.
  /// Depending on the loop schedule, it is necessary to call some runtime
  /// routine before start of the OpenMP loop to get the loop upper / lower
  /// bounds LB and UB and stride ST.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param DKind Kind of the directive.
  /// \param ScheduleKind Schedule kind, specified by the 'schedule' clause.
  /// \param Values Input arguments for the construct.
  ///
  void emitForStaticInit(CodeGenFunction &CGF, SourceLocation Loc,
                         OpenMPDirectiveKind DKind,
                         const OpenMPScheduleTy &ScheduleKind,
                         const StaticRTInput &Values) override;

  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param SchedKind Schedule kind, specified by the 'dist_schedule' clause.
  /// \param Values Input arguments for the construct.
  ///
  void emitDistributeStaticInit(CodeGenFunction &CGF, SourceLocation Loc,
                                OpenMPDistScheduleClauseKind SchedKind,
                                const StaticRTInput &Values) override;

  /// Call the appropriate runtime routine to notify that we finished
  /// iteration of the ordered loop with the dynamic scheduling.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param IVSize Size of the iteration variable in bits.
  /// \param IVSigned Sign of the iteration variable.
  ///
  void emitForOrderedIterationEnd(CodeGenFunction &CGF, SourceLocation Loc,
                                  unsigned IVSize, bool IVSigned) override;

  /// Call the appropriate runtime routine to notify that we finished
  /// all the work with current loop.
  ///
  /// \param CGF Reference to current CodeGenFunction.
  /// \param Loc Clang source location.
  /// \param DKind Kind of the directive for which the static finish is emitted.
  ///
  void emitForStaticFinish(CodeGenFunction &CGF, SourceLocation Loc,
                           OpenMPDirectiveKind DKind) override;

  /// Call __kmpc_dispatch_next(
  ///          ident_t *loc, kmp_int32 tid, kmp_int32 *p_lastiter,
  ///          kmp_int[32|64] *p_lower, kmp_int[32|64] *p_upper,
  ///          kmp_int[32|64] *p_stride);
  /// \param IVSize Size of the iteration variable in bits.
  /// \param IVSigned Sign of the iteration variable.
  /// \param IL Address of the output variable in which the flag of the
  /// last iteration is returned.
  /// \param LB Address of the output variable in which the lower iteration
  /// number is returned.
  /// \param UB Address of the output variable in which the upper iteration
  /// number is returned.
  /// \param ST Address of the output variable in which the stride value is
  /// returned.
  llvm::Value *emitForNext(CodeGenFunction &CGF, SourceLocation Loc,
                           unsigned IVSize, bool IVSigned, Address IL,
                           Address LB, Address UB, Address ST) override;

  /// Emits call to void __kmpc_push_num_threads(ident_t *loc, kmp_int32
  /// global_tid, kmp_int32 num_threads) to generate code for 'num_threads'
  /// clause.
  /// \param NumThreads An integer value of threads.
  void emitNumThreadsClause(CodeGenFunction &CGF, llvm::Value *NumThreads,
                            SourceLocation Loc) override;

  /// Emit call to void __kmpc_push_proc_bind(ident_t *loc, kmp_int32
  /// global_tid, int proc_bind) to generate code for 'proc_bind' clause.
  void emitProcBindClause(CodeGenFunction &CGF,
                          OpenMPProcBindClauseKind ProcBind,
                          SourceLocation Loc) override;

  /// Returns address of the threadprivate variable for the current
  /// thread.
  /// \param VD Threadprivate variable.
  /// \param VDAddr Address of the global variable \a VD.
  /// \param Loc Location of the reference to threadprivate var.
  /// \return Address of the threadprivate variable for the current thread.
  Address getAddrOfThreadPrivate(CodeGenFunction &CGF, const VarDecl *VD,
                                 Address VDAddr, SourceLocation Loc) override;

  /// Emit a code for initialization of threadprivate variable. It emits
  /// a call to runtime library which adds initial value to the newly created
  /// threadprivate variable (if it is not constant) and registers destructor
  /// for the variable (if any).
  /// \param VD Threadprivate variable.
  /// \param VDAddr Address of the global variable \a VD.
  /// \param Loc Location of threadprivate declaration.
  /// \param PerformInit true if initialization expression is not constant.
  llvm::Function *
  emitThreadPrivateVarDefinition(const VarDecl *VD, Address VDAddr,
                                 SourceLocation Loc, bool PerformInit,
                                 CodeGenFunction *CGF = nullptr) override;

  /// Creates artificial threadprivate variable with name \p Name and type \p
  /// VarType.
  /// \param VarType Type of the artificial threadprivate variable.
  /// \param Name Name of the artificial threadprivate variable.
  Address getAddrOfArtificialThreadPrivate(CodeGenFunction &CGF,
                                           QualType VarType,
                                           StringRef Name) override;

  /// Emit flush of the variables specified in 'omp flush' directive.
  /// \param Vars List of variables to flush.
  void emitFlush(CodeGenFunction &CGF, ArrayRef<const Expr *> Vars,
                 SourceLocation Loc) override;

  /// Emit task region for the task directive. The task region is
  /// emitted in several steps:
  /// 1. Emit a call to kmp_task_t *__kmpc_omp_task_alloc(ident_t *, kmp_int32
  /// gtid, kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  /// kmp_routine_entry_t *task_entry). Here task_entry is a pointer to the
  /// function:
  /// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
  ///   TaskFunction(gtid, tt->part_id, tt->shareds);
  ///   return 0;
  /// }
  /// 2. Copy a list of shared variables to field shareds of the resulting
  /// structure kmp_task_t returned by the previous call (if any).
  /// 3. Copy a pointer to destructions function to field destructions of the
  /// resulting structure kmp_task_t.
  /// 4. Emit a call to kmp_int32 __kmpc_omp_task(ident_t *, kmp_int32 gtid,
  /// kmp_task_t *new_task), where new_task is a resulting structure from
  /// previous items.
  /// \param D Current task directive.
  /// \param TaskFunction An LLVM function with type void (*)(i32 /*gtid*/, i32
  /// /*part_id*/, captured_struct */*__context*/);
  /// \param SharedsTy A type which contains references the shared variables.
  /// \param Shareds Context with the list of shared variables from the \p
  /// TaskFunction.
  /// \param IfCond Not a nullptr if 'if' clause was specified, nullptr
  /// otherwise.
  /// \param Data Additional data for task generation like tiednsee, final
  /// state, list of privates etc.
  void emitTaskCall(CodeGenFunction &CGF, SourceLocation Loc,
                    const OMPExecutableDirective &D, llvm::Value *TaskFunction,
                    QualType SharedsTy, Address Shareds, const Expr *IfCond,
                    const OMPTaskDataTy &Data) override;

  /// Emit task region for the taskloop directive. The taskloop region is
  /// emitted in several steps:
  /// 1. Emit a call to kmp_task_t *__kmpc_omp_task_alloc(ident_t *, kmp_int32
  /// gtid, kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  /// kmp_routine_entry_t *task_entry). Here task_entry is a pointer to the
  /// function:
  /// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
  ///   TaskFunction(gtid, tt->part_id, tt->shareds);
  ///   return 0;
  /// }
  /// 2. Copy a list of shared variables to field shareds of the resulting
  /// structure kmp_task_t returned by the previous call (if any).
  /// 3. Copy a pointer to destructions function to field destructions of the
  /// resulting structure kmp_task_t.
  /// 4. Emit a call to void __kmpc_taskloop(ident_t *loc, int gtid, kmp_task_t
  /// *task, int if_val, kmp_uint64 *lb, kmp_uint64 *ub, kmp_int64 st, int
  /// nogroup, int sched, kmp_uint64 grainsize, void *task_dup ), where new_task
  /// is a resulting structure from
  /// previous items.
  /// \param D Current task directive.
  /// \param TaskFunction An LLVM function with type void (*)(i32 /*gtid*/, i32
  /// /*part_id*/, captured_struct */*__context*/);
  /// \param SharedsTy A type which contains references the shared variables.
  /// \param Shareds Context with the list of shared variables from the \p
  /// TaskFunction.
  /// \param IfCond Not a nullptr if 'if' clause was specified, nullptr
  /// otherwise.
  /// \param Data Additional data for task generation like tiednsee, final
  /// state, list of privates etc.
  void emitTaskLoopCall(CodeGenFunction &CGF, SourceLocation Loc,
                        const OMPLoopDirective &D, llvm::Value *TaskFunction,
                        QualType SharedsTy, Address Shareds, const Expr *IfCond,
                        const OMPTaskDataTy &Data) override;

  /// Emit a code for reduction clause. Next code should be emitted for
  /// reduction:
  /// \code
  ///
  /// static kmp_critical_name lock = { 0 };
  ///
  /// void reduce_func(void *lhs[<n>], void *rhs[<n>]) {
  ///  ...
  ///  *(Type<i>*)lhs[i] = RedOp<i>(*(Type<i>*)lhs[i], *(Type<i>*)rhs[i]);
  ///  ...
  /// }
  ///
  /// ...
  /// void *RedList[<n>] = {&<RHSExprs>[0], ..., &<RHSExprs>[<n>-1]};
  /// switch (__kmpc_reduce{_nowait}(<loc>, <gtid>, <n>, sizeof(RedList),
  /// RedList, reduce_func, &<lock>)) {
  /// case 1:
  ///  ...
  ///  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  ///  ...
  /// __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  /// break;
  /// case 2:
  ///  ...
  ///  Atomic(<LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]));
  ///  ...
  /// break;
  /// default:;
  /// }
  /// \endcode
  ///
  /// \param Privates List of private copies for original reduction arguments.
  /// \param LHSExprs List of LHS in \a ReductionOps reduction operations.
  /// \param RHSExprs List of RHS in \a ReductionOps reduction operations.
  /// \param ReductionOps List of reduction operations in form 'LHS binop RHS'
  /// or 'operator binop(LHS, RHS)'.
  /// \param Options List of options for reduction codegen:
  ///     WithNowait true if parent directive has also nowait clause, false
  ///     otherwise.
  ///     SimpleReduction Emit reduction operation only. Used for omp simd
  ///     directive on the host.
  ///     ReductionKind The kind of reduction to perform.
  void emitReduction(CodeGenFunction &CGF, SourceLocation Loc,
                     ArrayRef<const Expr *> Privates,
                     ArrayRef<const Expr *> LHSExprs,
                     ArrayRef<const Expr *> RHSExprs,
                     ArrayRef<const Expr *> ReductionOps,
                     ReductionOptionsTy Options) override;

  /// Emit a code for initialization of task reduction clause. Next code
  /// should be emitted for reduction:
  /// \code
  ///
  /// _task_red_item_t red_data[n];
  /// ...
  /// red_data[i].shar = &origs[i];
  /// red_data[i].size = sizeof(origs[i]);
  /// red_data[i].f_init = (void*)RedInit<i>;
  /// red_data[i].f_fini = (void*)RedDest<i>;
  /// red_data[i].f_comb = (void*)RedOp<i>;
  /// red_data[i].flags = <Flag_i>;
  /// ...
  /// void* tg1 = __kmpc_task_reduction_init(gtid, n, red_data);
  /// \endcode
  ///
  /// \param LHSExprs List of LHS in \a Data.ReductionOps reduction operations.
  /// \param RHSExprs List of RHS in \a Data.ReductionOps reduction operations.
  /// \param Data Additional data for task generation like tiedness, final
  /// state, list of privates, reductions etc.
  llvm::Value *emitTaskReductionInit(CodeGenFunction &CGF, SourceLocation Loc,
                                     ArrayRef<const Expr *> LHSExprs,
                                     ArrayRef<const Expr *> RHSExprs,
                                     const OMPTaskDataTy &Data) override;

  /// Required to resolve existing problems in the runtime. Emits threadprivate
  /// variables to store the size of the VLAs/array sections for
  /// initializer/combiner/finalizer functions + emits threadprivate variable to
  /// store the pointer to the original reduction item for the custom
  /// initializer defined by declare reduction construct.
  /// \param RCG Allows to reuse an existing data for the reductions.
  /// \param N Reduction item for which fixups must be emitted.
  void emitTaskReductionFixups(CodeGenFunction &CGF, SourceLocation Loc,
                               ReductionCodeGen &RCG, unsigned N) override;

  /// Get the address of `void *` type of the privatue copy of the reduction
  /// item specified by the \p SharedLVal.
  /// \param ReductionsPtr Pointer to the reduction data returned by the
  /// emitTaskReductionInit function.
  /// \param SharedLVal Address of the original reduction item.
  Address getTaskReductionItem(CodeGenFunction &CGF, SourceLocation Loc,
                               llvm::Value *ReductionsPtr,
                               LValue SharedLVal) override;

  /// Emit code for 'taskwait' directive.
  void emitTaskwaitCall(CodeGenFunction &CGF, SourceLocation Loc) override;

  /// Emit code for 'cancellation point' construct.
  /// \param CancelRegion Region kind for which the cancellation point must be
  /// emitted.
  ///
  void emitCancellationPointCall(CodeGenFunction &CGF, SourceLocation Loc,
                                 OpenMPDirectiveKind CancelRegion) override;

  /// Emit code for 'cancel' construct.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  /// \param CancelRegion Region kind for which the cancel must be emitted.
  ///
  void emitCancelCall(CodeGenFunction &CGF, SourceLocation Loc,
                      const Expr *IfCond,
                      OpenMPDirectiveKind CancelRegion) override;

  /// Emit outilined function for 'target' directive.
  /// \param D Directive to emit.
  /// \param ParentName Name of the function that encloses the target region.
  /// \param OutlinedFn Outlined function value to be defined by this call.
  /// \param OutlinedFnID Outlined function ID value to be defined by this call.
  /// \param IsOffloadEntry True if the outlined function is an offload entry.
  /// \param CodeGen Code generation sequence for the \a D directive.
  /// An outlined function may not be an entry if, e.g. the if clause always
  /// evaluates to false.
  void emitTargetOutlinedFunction(const OMPExecutableDirective &D,
                                  StringRef ParentName,
                                  llvm::Function *&OutlinedFn,
                                  llvm::Constant *&OutlinedFnID,
                                  bool IsOffloadEntry,
                                  const RegionCodeGenTy &CodeGen) override;

  /// Emit the target offloading code associated with \a D. The emitted
  /// code attempts offloading the execution to the device, an the event of
  /// a failure it executes the host version outlined in \a OutlinedFn.
  /// \param D Directive to emit.
  /// \param OutlinedFn Host version of the code to be offloaded.
  /// \param OutlinedFnID ID of host version of the code to be offloaded.
  /// \param IfCond Expression evaluated in if clause associated with the target
  /// directive, or null if no if clause is used.
  /// \param Device Expression evaluated in device clause associated with the
  /// target directive, or null if no device clause is used.
  void emitTargetCall(CodeGenFunction &CGF, const OMPExecutableDirective &D,
                      llvm::Value *OutlinedFn, llvm::Value *OutlinedFnID,
                      const Expr *IfCond, const Expr *Device) override;

  /// Emit the target regions enclosed in \a GD function definition or
  /// the function itself in case it is a valid device function. Returns true if
  /// \a GD was dealt with successfully.
  /// \param GD Function to scan.
  bool emitTargetFunctions(GlobalDecl GD) override;

  /// Emit the global variable if it is a valid device global variable.
  /// Returns true if \a GD was dealt with successfully.
  /// \param GD Variable declaration to emit.
  bool emitTargetGlobalVariable(GlobalDecl GD) override;

  /// Emit the global \a GD if it is meaningful for the target. Returns
  /// if it was emitted successfully.
  /// \param GD Global to scan.
  bool emitTargetGlobal(GlobalDecl GD) override;

  /// Creates the offloading descriptor in the event any target region
  /// was emitted in the current module and return the function that registers
  /// it.
  llvm::Function *emitRegistrationFunction() override;

  /// Emits code for teams call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// \param OutlinedFn Outlined function to be run by team masters. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  ///
  void emitTeamsCall(CodeGenFunction &CGF, const OMPExecutableDirective &D,
                     SourceLocation Loc, llvm::Value *OutlinedFn,
                     ArrayRef<llvm::Value *> CapturedVars) override;

  /// Emits call to void __kmpc_push_num_teams(ident_t *loc, kmp_int32
  /// global_tid, kmp_int32 num_teams, kmp_int32 thread_limit) to generate code
  /// for num_teams clause.
  /// \param NumTeams An integer expression of teams.
  /// \param ThreadLimit An integer expression of threads.
  void emitNumTeamsClause(CodeGenFunction &CGF, const Expr *NumTeams,
                          const Expr *ThreadLimit, SourceLocation Loc) override;

  /// Emit the target data mapping code associated with \a D.
  /// \param D Directive to emit.
  /// \param IfCond Expression evaluated in if clause associated with the
  /// target directive, or null if no device clause is used.
  /// \param Device Expression evaluated in device clause associated with the
  /// target directive, or null if no device clause is used.
  /// \param Info A record used to store information that needs to be preserved
  /// until the region is closed.
  void emitTargetDataCalls(CodeGenFunction &CGF,
                           const OMPExecutableDirective &D, const Expr *IfCond,
                           const Expr *Device, const RegionCodeGenTy &CodeGen,
                           TargetDataInfo &Info) override;

  /// Emit the data mapping/movement code associated with the directive
  /// \a D that should be of the form 'target [{enter|exit} data | update]'.
  /// \param D Directive to emit.
  /// \param IfCond Expression evaluated in if clause associated with the target
  /// directive, or null if no if clause is used.
  /// \param Device Expression evaluated in device clause associated with the
  /// target directive, or null if no device clause is used.
  void emitTargetDataStandAloneCall(CodeGenFunction &CGF,
                                    const OMPExecutableDirective &D,
                                    const Expr *IfCond,
                                    const Expr *Device) override;

  /// Emit initialization for doacross loop nesting support.
  /// \param D Loop-based construct used in doacross nesting construct.
  void emitDoacrossInit(CodeGenFunction &CGF, const OMPLoopDirective &D,
                        ArrayRef<Expr *> NumIterations) override;

  /// Emit code for doacross ordered directive with 'depend' clause.
  /// \param C 'depend' clause with 'sink|source' dependency kind.
  void emitDoacrossOrdered(CodeGenFunction &CGF,
                           const OMPDependClause *C) override;

  /// Translates the native parameter of outlined function if this is required
  /// for target.
  /// \param FD Field decl from captured record for the parameter.
  /// \param NativeParam Parameter itself.
  const VarDecl *translateParameter(const FieldDecl *FD,
                                    const VarDecl *NativeParam) const override;

  /// Gets the address of the native argument basing on the address of the
  /// target-specific parameter.
  /// \param NativeParam Parameter itself.
  /// \param TargetParam Corresponding target-specific parameter.
  Address getParameterAddress(CodeGenFunction &CGF, const VarDecl *NativeParam,
                              const VarDecl *TargetParam) const override;
};

} // namespace CodeGen
} // namespace clang

#endif
