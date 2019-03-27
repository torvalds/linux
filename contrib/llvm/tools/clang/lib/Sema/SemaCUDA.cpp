//===--- SemaCUDA.cpp - Semantic Analysis for CUDA constructs -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements semantic analysis for CUDA constructs.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;

void Sema::PushForceCUDAHostDevice() {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  ForceCUDAHostDeviceDepth++;
}

bool Sema::PopForceCUDAHostDevice() {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  if (ForceCUDAHostDeviceDepth == 0)
    return false;
  ForceCUDAHostDeviceDepth--;
  return true;
}

ExprResult Sema::ActOnCUDAExecConfigExpr(Scope *S, SourceLocation LLLLoc,
                                         MultiExprArg ExecConfig,
                                         SourceLocation GGGLoc) {
  FunctionDecl *ConfigDecl = Context.getcudaConfigureCallDecl();
  if (!ConfigDecl)
    return ExprError(
        Diag(LLLLoc, diag::err_undeclared_var_use)
        << (getLangOpts().HIP ? "hipConfigureCall" : "cudaConfigureCall"));
  QualType ConfigQTy = ConfigDecl->getType();

  DeclRefExpr *ConfigDR = new (Context)
      DeclRefExpr(Context, ConfigDecl, false, ConfigQTy, VK_LValue, LLLLoc);
  MarkFunctionReferenced(LLLLoc, ConfigDecl);

  return ActOnCallExpr(S, ConfigDR, LLLLoc, ExecConfig, GGGLoc, nullptr,
                       /*IsExecConfig=*/true);
}

Sema::CUDAFunctionTarget
Sema::IdentifyCUDATarget(const ParsedAttributesView &Attrs) {
  bool HasHostAttr = false;
  bool HasDeviceAttr = false;
  bool HasGlobalAttr = false;
  bool HasInvalidTargetAttr = false;
  for (const ParsedAttr &AL : Attrs) {
    switch (AL.getKind()) {
    case ParsedAttr::AT_CUDAGlobal:
      HasGlobalAttr = true;
      break;
    case ParsedAttr::AT_CUDAHost:
      HasHostAttr = true;
      break;
    case ParsedAttr::AT_CUDADevice:
      HasDeviceAttr = true;
      break;
    case ParsedAttr::AT_CUDAInvalidTarget:
      HasInvalidTargetAttr = true;
      break;
    default:
      break;
    }
  }

  if (HasInvalidTargetAttr)
    return CFT_InvalidTarget;

  if (HasGlobalAttr)
    return CFT_Global;

  if (HasHostAttr && HasDeviceAttr)
    return CFT_HostDevice;

  if (HasDeviceAttr)
    return CFT_Device;

  return CFT_Host;
}

template <typename A>
static bool hasAttr(const FunctionDecl *D, bool IgnoreImplicitAttr) {
  return D->hasAttrs() && llvm::any_of(D->getAttrs(), [&](Attr *Attribute) {
           return isa<A>(Attribute) &&
                  !(IgnoreImplicitAttr && Attribute->isImplicit());
         });
}

/// IdentifyCUDATarget - Determine the CUDA compilation target for this function
Sema::CUDAFunctionTarget Sema::IdentifyCUDATarget(const FunctionDecl *D,
                                                  bool IgnoreImplicitHDAttr) {
  // Code that lives outside a function is run on the host.
  if (D == nullptr)
    return CFT_Host;

  if (D->hasAttr<CUDAInvalidTargetAttr>())
    return CFT_InvalidTarget;

  if (D->hasAttr<CUDAGlobalAttr>())
    return CFT_Global;

  if (hasAttr<CUDADeviceAttr>(D, IgnoreImplicitHDAttr)) {
    if (hasAttr<CUDAHostAttr>(D, IgnoreImplicitHDAttr))
      return CFT_HostDevice;
    return CFT_Device;
  } else if (hasAttr<CUDAHostAttr>(D, IgnoreImplicitHDAttr)) {
    return CFT_Host;
  } else if (D->isImplicit() && !IgnoreImplicitHDAttr) {
    // Some implicit declarations (like intrinsic functions) are not marked.
    // Set the most lenient target on them for maximal flexibility.
    return CFT_HostDevice;
  }

  return CFT_Host;
}

// * CUDA Call preference table
//
// F - from,
// T - to
// Ph - preference in host mode
// Pd - preference in device mode
// H  - handled in (x)
// Preferences: N:native, SS:same side, HD:host-device, WS:wrong side, --:never.
//
// | F  | T  | Ph  | Pd  |  H  |
// |----+----+-----+-----+-----+
// | d  | d  | N   | N   | (c) |
// | d  | g  | --  | --  | (a) |
// | d  | h  | --  | --  | (e) |
// | d  | hd | HD  | HD  | (b) |
// | g  | d  | N   | N   | (c) |
// | g  | g  | --  | --  | (a) |
// | g  | h  | --  | --  | (e) |
// | g  | hd | HD  | HD  | (b) |
// | h  | d  | --  | --  | (e) |
// | h  | g  | N   | N   | (c) |
// | h  | h  | N   | N   | (c) |
// | h  | hd | HD  | HD  | (b) |
// | hd | d  | WS  | SS  | (d) |
// | hd | g  | SS  | --  |(d/a)|
// | hd | h  | SS  | WS  | (d) |
// | hd | hd | HD  | HD  | (b) |

Sema::CUDAFunctionPreference
Sema::IdentifyCUDAPreference(const FunctionDecl *Caller,
                             const FunctionDecl *Callee) {
  assert(Callee && "Callee must be valid.");
  CUDAFunctionTarget CallerTarget = IdentifyCUDATarget(Caller);
  CUDAFunctionTarget CalleeTarget = IdentifyCUDATarget(Callee);

  // If one of the targets is invalid, the check always fails, no matter what
  // the other target is.
  if (CallerTarget == CFT_InvalidTarget || CalleeTarget == CFT_InvalidTarget)
    return CFP_Never;

  // (a) Can't call global from some contexts until we support CUDA's
  // dynamic parallelism.
  if (CalleeTarget == CFT_Global &&
      (CallerTarget == CFT_Global || CallerTarget == CFT_Device))
    return CFP_Never;

  // (b) Calling HostDevice is OK for everyone.
  if (CalleeTarget == CFT_HostDevice)
    return CFP_HostDevice;

  // (c) Best case scenarios
  if (CalleeTarget == CallerTarget ||
      (CallerTarget == CFT_Host && CalleeTarget == CFT_Global) ||
      (CallerTarget == CFT_Global && CalleeTarget == CFT_Device))
    return CFP_Native;

  // (d) HostDevice behavior depends on compilation mode.
  if (CallerTarget == CFT_HostDevice) {
    // It's OK to call a compilation-mode matching function from an HD one.
    if ((getLangOpts().CUDAIsDevice && CalleeTarget == CFT_Device) ||
        (!getLangOpts().CUDAIsDevice &&
         (CalleeTarget == CFT_Host || CalleeTarget == CFT_Global)))
      return CFP_SameSide;

    // Calls from HD to non-mode-matching functions (i.e., to host functions
    // when compiling in device mode or to device functions when compiling in
    // host mode) are allowed at the sema level, but eventually rejected if
    // they're ever codegened.  TODO: Reject said calls earlier.
    return CFP_WrongSide;
  }

  // (e) Calling across device/host boundary is not something you should do.
  if ((CallerTarget == CFT_Host && CalleeTarget == CFT_Device) ||
      (CallerTarget == CFT_Device && CalleeTarget == CFT_Host) ||
      (CallerTarget == CFT_Global && CalleeTarget == CFT_Host))
    return CFP_Never;

  llvm_unreachable("All cases should've been handled by now.");
}

void Sema::EraseUnwantedCUDAMatches(
    const FunctionDecl *Caller,
    SmallVectorImpl<std::pair<DeclAccessPair, FunctionDecl *>> &Matches) {
  if (Matches.size() <= 1)
    return;

  using Pair = std::pair<DeclAccessPair, FunctionDecl*>;

  // Gets the CUDA function preference for a call from Caller to Match.
  auto GetCFP = [&](const Pair &Match) {
    return IdentifyCUDAPreference(Caller, Match.second);
  };

  // Find the best call preference among the functions in Matches.
  CUDAFunctionPreference BestCFP = GetCFP(*std::max_element(
      Matches.begin(), Matches.end(),
      [&](const Pair &M1, const Pair &M2) { return GetCFP(M1) < GetCFP(M2); }));

  // Erase all functions with lower priority.
  llvm::erase_if(Matches,
                 [&](const Pair &Match) { return GetCFP(Match) < BestCFP; });
}

/// When an implicitly-declared special member has to invoke more than one
/// base/field special member, conflicts may occur in the targets of these
/// members. For example, if one base's member __host__ and another's is
/// __device__, it's a conflict.
/// This function figures out if the given targets \param Target1 and
/// \param Target2 conflict, and if they do not it fills in
/// \param ResolvedTarget with a target that resolves for both calls.
/// \return true if there's a conflict, false otherwise.
static bool
resolveCalleeCUDATargetConflict(Sema::CUDAFunctionTarget Target1,
                                Sema::CUDAFunctionTarget Target2,
                                Sema::CUDAFunctionTarget *ResolvedTarget) {
  // Only free functions and static member functions may be global.
  assert(Target1 != Sema::CFT_Global);
  assert(Target2 != Sema::CFT_Global);

  if (Target1 == Sema::CFT_HostDevice) {
    *ResolvedTarget = Target2;
  } else if (Target2 == Sema::CFT_HostDevice) {
    *ResolvedTarget = Target1;
  } else if (Target1 != Target2) {
    return true;
  } else {
    *ResolvedTarget = Target1;
  }

  return false;
}

bool Sema::inferCUDATargetForImplicitSpecialMember(CXXRecordDecl *ClassDecl,
                                                   CXXSpecialMember CSM,
                                                   CXXMethodDecl *MemberDecl,
                                                   bool ConstRHS,
                                                   bool Diagnose) {
  llvm::Optional<CUDAFunctionTarget> InferredTarget;

  // We're going to invoke special member lookup; mark that these special
  // members are called from this one, and not from its caller.
  ContextRAII MethodContext(*this, MemberDecl);

  // Look for special members in base classes that should be invoked from here.
  // Infer the target of this member base on the ones it should call.
  // Skip direct and indirect virtual bases for abstract classes.
  llvm::SmallVector<const CXXBaseSpecifier *, 16> Bases;
  for (const auto &B : ClassDecl->bases()) {
    if (!B.isVirtual()) {
      Bases.push_back(&B);
    }
  }

  if (!ClassDecl->isAbstract()) {
    for (const auto &VB : ClassDecl->vbases()) {
      Bases.push_back(&VB);
    }
  }

  for (const auto *B : Bases) {
    const RecordType *BaseType = B->getType()->getAs<RecordType>();
    if (!BaseType) {
      continue;
    }

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
    Sema::SpecialMemberOverloadResult SMOR =
        LookupSpecialMember(BaseClassDecl, CSM,
                            /* ConstArg */ ConstRHS,
                            /* VolatileArg */ false,
                            /* RValueThis */ false,
                            /* ConstThis */ false,
                            /* VolatileThis */ false);

    if (!SMOR.getMethod())
      continue;

    CUDAFunctionTarget BaseMethodTarget = IdentifyCUDATarget(SMOR.getMethod());
    if (!InferredTarget.hasValue()) {
      InferredTarget = BaseMethodTarget;
    } else {
      bool ResolutionError = resolveCalleeCUDATargetConflict(
          InferredTarget.getValue(), BaseMethodTarget,
          InferredTarget.getPointer());
      if (ResolutionError) {
        if (Diagnose) {
          Diag(ClassDecl->getLocation(),
               diag::note_implicit_member_target_infer_collision)
              << (unsigned)CSM << InferredTarget.getValue() << BaseMethodTarget;
        }
        MemberDecl->addAttr(CUDAInvalidTargetAttr::CreateImplicit(Context));
        return true;
      }
    }
  }

  // Same as for bases, but now for special members of fields.
  for (const auto *F : ClassDecl->fields()) {
    if (F->isInvalidDecl()) {
      continue;
    }

    const RecordType *FieldType =
        Context.getBaseElementType(F->getType())->getAs<RecordType>();
    if (!FieldType) {
      continue;
    }

    CXXRecordDecl *FieldRecDecl = cast<CXXRecordDecl>(FieldType->getDecl());
    Sema::SpecialMemberOverloadResult SMOR =
        LookupSpecialMember(FieldRecDecl, CSM,
                            /* ConstArg */ ConstRHS && !F->isMutable(),
                            /* VolatileArg */ false,
                            /* RValueThis */ false,
                            /* ConstThis */ false,
                            /* VolatileThis */ false);

    if (!SMOR.getMethod())
      continue;

    CUDAFunctionTarget FieldMethodTarget =
        IdentifyCUDATarget(SMOR.getMethod());
    if (!InferredTarget.hasValue()) {
      InferredTarget = FieldMethodTarget;
    } else {
      bool ResolutionError = resolveCalleeCUDATargetConflict(
          InferredTarget.getValue(), FieldMethodTarget,
          InferredTarget.getPointer());
      if (ResolutionError) {
        if (Diagnose) {
          Diag(ClassDecl->getLocation(),
               diag::note_implicit_member_target_infer_collision)
              << (unsigned)CSM << InferredTarget.getValue()
              << FieldMethodTarget;
        }
        MemberDecl->addAttr(CUDAInvalidTargetAttr::CreateImplicit(Context));
        return true;
      }
    }
  }

  if (InferredTarget.hasValue()) {
    if (InferredTarget.getValue() == CFT_Device) {
      MemberDecl->addAttr(CUDADeviceAttr::CreateImplicit(Context));
    } else if (InferredTarget.getValue() == CFT_Host) {
      MemberDecl->addAttr(CUDAHostAttr::CreateImplicit(Context));
    } else {
      MemberDecl->addAttr(CUDADeviceAttr::CreateImplicit(Context));
      MemberDecl->addAttr(CUDAHostAttr::CreateImplicit(Context));
    }
  } else {
    // If no target was inferred, mark this member as __host__ __device__;
    // it's the least restrictive option that can be invoked from any target.
    MemberDecl->addAttr(CUDADeviceAttr::CreateImplicit(Context));
    MemberDecl->addAttr(CUDAHostAttr::CreateImplicit(Context));
  }

  return false;
}

bool Sema::isEmptyCudaConstructor(SourceLocation Loc, CXXConstructorDecl *CD) {
  if (!CD->isDefined() && CD->isTemplateInstantiation())
    InstantiateFunctionDefinition(Loc, CD->getFirstDecl());

  // (E.2.3.1, CUDA 7.5) A constructor for a class type is considered
  // empty at a point in the translation unit, if it is either a
  // trivial constructor
  if (CD->isTrivial())
    return true;

  // ... or it satisfies all of the following conditions:
  // The constructor function has been defined.
  // The constructor function has no parameters,
  // and the function body is an empty compound statement.
  if (!(CD->hasTrivialBody() && CD->getNumParams() == 0))
    return false;

  // Its class has no virtual functions and no virtual base classes.
  if (CD->getParent()->isDynamicClass())
    return false;

  // The only form of initializer allowed is an empty constructor.
  // This will recursively check all base classes and member initializers
  if (!llvm::all_of(CD->inits(), [&](const CXXCtorInitializer *CI) {
        if (const CXXConstructExpr *CE =
                dyn_cast<CXXConstructExpr>(CI->getInit()))
          return isEmptyCudaConstructor(Loc, CE->getConstructor());
        return false;
      }))
    return false;

  return true;
}

bool Sema::isEmptyCudaDestructor(SourceLocation Loc, CXXDestructorDecl *DD) {
  // No destructor -> no problem.
  if (!DD)
    return true;

  if (!DD->isDefined() && DD->isTemplateInstantiation())
    InstantiateFunctionDefinition(Loc, DD->getFirstDecl());

  // (E.2.3.1, CUDA 7.5) A destructor for a class type is considered
  // empty at a point in the translation unit, if it is either a
  // trivial constructor
  if (DD->isTrivial())
    return true;

  // ... or it satisfies all of the following conditions:
  // The destructor function has been defined.
  // and the function body is an empty compound statement.
  if (!DD->hasTrivialBody())
    return false;

  const CXXRecordDecl *ClassDecl = DD->getParent();

  // Its class has no virtual functions and no virtual base classes.
  if (ClassDecl->isDynamicClass())
    return false;

  // Only empty destructors are allowed. This will recursively check
  // destructors for all base classes...
  if (!llvm::all_of(ClassDecl->bases(), [&](const CXXBaseSpecifier &BS) {
        if (CXXRecordDecl *RD = BS.getType()->getAsCXXRecordDecl())
          return isEmptyCudaDestructor(Loc, RD->getDestructor());
        return true;
      }))
    return false;

  // ... and member fields.
  if (!llvm::all_of(ClassDecl->fields(), [&](const FieldDecl *Field) {
        if (CXXRecordDecl *RD = Field->getType()
                                    ->getBaseElementTypeUnsafe()
                                    ->getAsCXXRecordDecl())
          return isEmptyCudaDestructor(Loc, RD->getDestructor());
        return true;
      }))
    return false;

  return true;
}

void Sema::checkAllowedCUDAInitializer(VarDecl *VD) {
  if (VD->isInvalidDecl() || !VD->hasInit() || !VD->hasGlobalStorage())
    return;
  const Expr *Init = VD->getInit();
  if (VD->hasAttr<CUDADeviceAttr>() || VD->hasAttr<CUDAConstantAttr>() ||
      VD->hasAttr<CUDASharedAttr>()) {
    assert(!VD->isStaticLocal() || VD->hasAttr<CUDASharedAttr>());
    bool AllowedInit = false;
    if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(Init))
      AllowedInit =
          isEmptyCudaConstructor(VD->getLocation(), CE->getConstructor());
    // We'll allow constant initializers even if it's a non-empty
    // constructor according to CUDA rules. This deviates from NVCC,
    // but allows us to handle things like constexpr constructors.
    if (!AllowedInit &&
        (VD->hasAttr<CUDADeviceAttr>() || VD->hasAttr<CUDAConstantAttr>()))
      AllowedInit = VD->getInit()->isConstantInitializer(
          Context, VD->getType()->isReferenceType());

    // Also make sure that destructor, if there is one, is empty.
    if (AllowedInit)
      if (CXXRecordDecl *RD = VD->getType()->getAsCXXRecordDecl())
        AllowedInit =
            isEmptyCudaDestructor(VD->getLocation(), RD->getDestructor());

    if (!AllowedInit) {
      Diag(VD->getLocation(), VD->hasAttr<CUDASharedAttr>()
                                  ? diag::err_shared_var_init
                                  : diag::err_dynamic_var_init)
          << Init->getSourceRange();
      VD->setInvalidDecl();
    }
  } else {
    // This is a host-side global variable.  Check that the initializer is
    // callable from the host side.
    const FunctionDecl *InitFn = nullptr;
    if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(Init)) {
      InitFn = CE->getConstructor();
    } else if (const CallExpr *CE = dyn_cast<CallExpr>(Init)) {
      InitFn = CE->getDirectCallee();
    }
    if (InitFn) {
      CUDAFunctionTarget InitFnTarget = IdentifyCUDATarget(InitFn);
      if (InitFnTarget != CFT_Host && InitFnTarget != CFT_HostDevice) {
        Diag(VD->getLocation(), diag::err_ref_bad_target_global_initializer)
            << InitFnTarget << InitFn;
        Diag(InitFn->getLocation(), diag::note_previous_decl) << InitFn;
        VD->setInvalidDecl();
      }
    }
  }
}

// With -fcuda-host-device-constexpr, an unattributed constexpr function is
// treated as implicitly __host__ __device__, unless:
//  * it is a variadic function (device-side variadic functions are not
//    allowed), or
//  * a __device__ function with this signature was already declared, in which
//    case in which case we output an error, unless the __device__ decl is in a
//    system header, in which case we leave the constexpr function unattributed.
//
// In addition, all function decls are treated as __host__ __device__ when
// ForceCUDAHostDeviceDepth > 0 (corresponding to code within a
//   #pragma clang force_cuda_host_device_begin/end
// pair).
void Sema::maybeAddCUDAHostDeviceAttrs(FunctionDecl *NewD,
                                       const LookupResult &Previous) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");

  if (ForceCUDAHostDeviceDepth > 0) {
    if (!NewD->hasAttr<CUDAHostAttr>())
      NewD->addAttr(CUDAHostAttr::CreateImplicit(Context));
    if (!NewD->hasAttr<CUDADeviceAttr>())
      NewD->addAttr(CUDADeviceAttr::CreateImplicit(Context));
    return;
  }

  if (!getLangOpts().CUDAHostDeviceConstexpr || !NewD->isConstexpr() ||
      NewD->isVariadic() || NewD->hasAttr<CUDAHostAttr>() ||
      NewD->hasAttr<CUDADeviceAttr>() || NewD->hasAttr<CUDAGlobalAttr>())
    return;

  // Is D a __device__ function with the same signature as NewD, ignoring CUDA
  // attributes?
  auto IsMatchingDeviceFn = [&](NamedDecl *D) {
    if (UsingShadowDecl *Using = dyn_cast<UsingShadowDecl>(D))
      D = Using->getTargetDecl();
    FunctionDecl *OldD = D->getAsFunction();
    return OldD && OldD->hasAttr<CUDADeviceAttr>() &&
           !OldD->hasAttr<CUDAHostAttr>() &&
           !IsOverload(NewD, OldD, /* UseMemberUsingDeclRules = */ false,
                       /* ConsiderCudaAttrs = */ false);
  };
  auto It = llvm::find_if(Previous, IsMatchingDeviceFn);
  if (It != Previous.end()) {
    // We found a __device__ function with the same name and signature as NewD
    // (ignoring CUDA attrs).  This is an error unless that function is defined
    // in a system header, in which case we simply return without making NewD
    // host+device.
    NamedDecl *Match = *It;
    if (!getSourceManager().isInSystemHeader(Match->getLocation())) {
      Diag(NewD->getLocation(),
           diag::err_cuda_unattributed_constexpr_cannot_overload_device)
          << NewD;
      Diag(Match->getLocation(),
           diag::note_cuda_conflicting_device_function_declared_here);
    }
    return;
  }

  NewD->addAttr(CUDAHostAttr::CreateImplicit(Context));
  NewD->addAttr(CUDADeviceAttr::CreateImplicit(Context));
}

// In CUDA, there are some constructs which may appear in semantically-valid
// code, but trigger errors if we ever generate code for the function in which
// they appear.  Essentially every construct you're not allowed to use on the
// device falls into this category, because you are allowed to use these
// constructs in a __host__ __device__ function, but only if that function is
// never codegen'ed on the device.
//
// To handle semantic checking for these constructs, we keep track of the set of
// functions we know will be emitted, either because we could tell a priori that
// they would be emitted, or because they were transitively called by a
// known-emitted function.
//
// We also keep a partial call graph of which not-known-emitted functions call
// which other not-known-emitted functions.
//
// When we see something which is illegal if the current function is emitted
// (usually by way of CUDADiagIfDeviceCode, CUDADiagIfHostCode, or
// CheckCUDACall), we first check if the current function is known-emitted.  If
// so, we immediately output the diagnostic.
//
// Otherwise, we "defer" the diagnostic.  It sits in Sema::CUDADeferredDiags
// until we discover that the function is known-emitted, at which point we take
// it out of this map and emit the diagnostic.

Sema::CUDADiagBuilder::CUDADiagBuilder(Kind K, SourceLocation Loc,
                                       unsigned DiagID, FunctionDecl *Fn,
                                       Sema &S)
    : S(S), Loc(Loc), DiagID(DiagID), Fn(Fn),
      ShowCallStack(K == K_ImmediateWithCallStack || K == K_Deferred) {
  switch (K) {
  case K_Nop:
    break;
  case K_Immediate:
  case K_ImmediateWithCallStack:
    ImmediateDiag.emplace(S.Diag(Loc, DiagID));
    break;
  case K_Deferred:
    assert(Fn && "Must have a function to attach the deferred diag to.");
    PartialDiag.emplace(S.PDiag(DiagID));
    break;
  }
}

// Print notes showing how we can reach FD starting from an a priori
// known-callable function.
static void EmitCallStackNotes(Sema &S, FunctionDecl *FD) {
  auto FnIt = S.CUDAKnownEmittedFns.find(FD);
  while (FnIt != S.CUDAKnownEmittedFns.end()) {
    DiagnosticBuilder Builder(
        S.Diags.Report(FnIt->second.Loc, diag::note_called_by));
    Builder << FnIt->second.FD;
    Builder.setForceEmit();

    FnIt = S.CUDAKnownEmittedFns.find(FnIt->second.FD);
  }
}

Sema::CUDADiagBuilder::~CUDADiagBuilder() {
  if (ImmediateDiag) {
    // Emit our diagnostic and, if it was a warning or error, output a callstack
    // if Fn isn't a priori known-emitted.
    bool IsWarningOrError = S.getDiagnostics().getDiagnosticLevel(
                                DiagID, Loc) >= DiagnosticsEngine::Warning;
    ImmediateDiag.reset(); // Emit the immediate diag.
    if (IsWarningOrError && ShowCallStack)
      EmitCallStackNotes(S, Fn);
  } else if (PartialDiag) {
    assert(ShowCallStack && "Must always show call stack for deferred diags.");
    S.CUDADeferredDiags[Fn].push_back({Loc, std::move(*PartialDiag)});
  }
}

// Do we know that we will eventually codegen the given function?
static bool IsKnownEmitted(Sema &S, FunctionDecl *FD) {
  // Templates are emitted when they're instantiated.
  if (FD->isDependentContext())
    return false;

  // When compiling for device, host functions are never emitted.  Similarly,
  // when compiling for host, device and global functions are never emitted.
  // (Technically, we do emit a host-side stub for global functions, but this
  // doesn't count for our purposes here.)
  Sema::CUDAFunctionTarget T = S.IdentifyCUDATarget(FD);
  if (S.getLangOpts().CUDAIsDevice && T == Sema::CFT_Host)
    return false;
  if (!S.getLangOpts().CUDAIsDevice &&
      (T == Sema::CFT_Device || T == Sema::CFT_Global))
    return false;

  // Check whether this function is externally visible -- if so, it's
  // known-emitted.
  //
  // We have to check the GVA linkage of the function's *definition* -- if we
  // only have a declaration, we don't know whether or not the function will be
  // emitted, because (say) the definition could include "inline".
  FunctionDecl *Def = FD->getDefinition();

  if (Def &&
      !isDiscardableGVALinkage(S.getASTContext().GetGVALinkageForFunction(Def)))
    return true;

  // Otherwise, the function is known-emitted if it's in our set of
  // known-emitted functions.
  return S.CUDAKnownEmittedFns.count(FD) > 0;
}

Sema::CUDADiagBuilder Sema::CUDADiagIfDeviceCode(SourceLocation Loc,
                                                 unsigned DiagID) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  CUDADiagBuilder::Kind DiagKind = [&] {
    switch (CurrentCUDATarget()) {
    case CFT_Global:
    case CFT_Device:
      return CUDADiagBuilder::K_Immediate;
    case CFT_HostDevice:
      // An HD function counts as host code if we're compiling for host, and
      // device code if we're compiling for device.  Defer any errors in device
      // mode until the function is known-emitted.
      if (getLangOpts().CUDAIsDevice) {
        return IsKnownEmitted(*this, dyn_cast<FunctionDecl>(CurContext))
                   ? CUDADiagBuilder::K_ImmediateWithCallStack
                   : CUDADiagBuilder::K_Deferred;
      }
      return CUDADiagBuilder::K_Nop;

    default:
      return CUDADiagBuilder::K_Nop;
    }
  }();
  return CUDADiagBuilder(DiagKind, Loc, DiagID,
                         dyn_cast<FunctionDecl>(CurContext), *this);
}

Sema::CUDADiagBuilder Sema::CUDADiagIfHostCode(SourceLocation Loc,
                                               unsigned DiagID) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  CUDADiagBuilder::Kind DiagKind = [&] {
    switch (CurrentCUDATarget()) {
    case CFT_Host:
      return CUDADiagBuilder::K_Immediate;
    case CFT_HostDevice:
      // An HD function counts as host code if we're compiling for host, and
      // device code if we're compiling for device.  Defer any errors in device
      // mode until the function is known-emitted.
      if (getLangOpts().CUDAIsDevice)
        return CUDADiagBuilder::K_Nop;

      return IsKnownEmitted(*this, dyn_cast<FunctionDecl>(CurContext))
                 ? CUDADiagBuilder::K_ImmediateWithCallStack
                 : CUDADiagBuilder::K_Deferred;
    default:
      return CUDADiagBuilder::K_Nop;
    }
  }();
  return CUDADiagBuilder(DiagKind, Loc, DiagID,
                         dyn_cast<FunctionDecl>(CurContext), *this);
}

// Emit any deferred diagnostics for FD and erase them from the map in which
// they're stored.
static void EmitDeferredDiags(Sema &S, FunctionDecl *FD) {
  auto It = S.CUDADeferredDiags.find(FD);
  if (It == S.CUDADeferredDiags.end())
    return;
  bool HasWarningOrError = false;
  for (PartialDiagnosticAt &PDAt : It->second) {
    const SourceLocation &Loc = PDAt.first;
    const PartialDiagnostic &PD = PDAt.second;
    HasWarningOrError |= S.getDiagnostics().getDiagnosticLevel(
                             PD.getDiagID(), Loc) >= DiagnosticsEngine::Warning;
    DiagnosticBuilder Builder(S.Diags.Report(Loc, PD.getDiagID()));
    Builder.setForceEmit();
    PD.Emit(Builder);
  }
  S.CUDADeferredDiags.erase(It);

  // FIXME: Should this be called after every warning/error emitted in the loop
  // above, instead of just once per function?  That would be consistent with
  // how we handle immediate errors, but it also seems like a bit much.
  if (HasWarningOrError)
    EmitCallStackNotes(S, FD);
}

// Indicate that this function (and thus everything it transtively calls) will
// be codegen'ed, and emit any deferred diagnostics on this function and its
// (transitive) callees.
static void MarkKnownEmitted(Sema &S, FunctionDecl *OrigCaller,
                             FunctionDecl *OrigCallee, SourceLocation OrigLoc) {
  // Nothing to do if we already know that FD is emitted.
  if (IsKnownEmitted(S, OrigCallee)) {
    assert(!S.CUDACallGraph.count(OrigCallee));
    return;
  }

  // We've just discovered that OrigCallee is known-emitted.  Walk our call
  // graph to see what else we can now discover also must be emitted.

  struct CallInfo {
    FunctionDecl *Caller;
    FunctionDecl *Callee;
    SourceLocation Loc;
  };
  llvm::SmallVector<CallInfo, 4> Worklist = {{OrigCaller, OrigCallee, OrigLoc}};
  llvm::SmallSet<CanonicalDeclPtr<FunctionDecl>, 4> Seen;
  Seen.insert(OrigCallee);
  while (!Worklist.empty()) {
    CallInfo C = Worklist.pop_back_val();
    assert(!IsKnownEmitted(S, C.Callee) &&
           "Worklist should not contain known-emitted functions.");
    S.CUDAKnownEmittedFns[C.Callee] = {C.Caller, C.Loc};
    EmitDeferredDiags(S, C.Callee);

    // If this is a template instantiation, explore its callgraph as well:
    // Non-dependent calls are part of the template's callgraph, while dependent
    // calls are part of to the instantiation's call graph.
    if (auto *Templ = C.Callee->getPrimaryTemplate()) {
      FunctionDecl *TemplFD = Templ->getAsFunction();
      if (!Seen.count(TemplFD) && !S.CUDAKnownEmittedFns.count(TemplFD)) {
        Seen.insert(TemplFD);
        Worklist.push_back(
            {/* Caller = */ C.Caller, /* Callee = */ TemplFD, C.Loc});
      }
    }

    // Add all functions called by Callee to our worklist.
    auto CGIt = S.CUDACallGraph.find(C.Callee);
    if (CGIt == S.CUDACallGraph.end())
      continue;

    for (std::pair<CanonicalDeclPtr<FunctionDecl>, SourceLocation> FDLoc :
         CGIt->second) {
      FunctionDecl *NewCallee = FDLoc.first;
      SourceLocation CallLoc = FDLoc.second;
      if (Seen.count(NewCallee) || IsKnownEmitted(S, NewCallee))
        continue;
      Seen.insert(NewCallee);
      Worklist.push_back(
          {/* Caller = */ C.Callee, /* Callee = */ NewCallee, CallLoc});
    }

    // C.Callee is now known-emitted, so we no longer need to maintain its list
    // of callees in CUDACallGraph.
    S.CUDACallGraph.erase(CGIt);
  }
}

bool Sema::CheckCUDACall(SourceLocation Loc, FunctionDecl *Callee) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  assert(Callee && "Callee may not be null.");
  // FIXME: Is bailing out early correct here?  Should we instead assume that
  // the caller is a global initializer?
  FunctionDecl *Caller = dyn_cast<FunctionDecl>(CurContext);
  if (!Caller)
    return true;

  // If the caller is known-emitted, mark the callee as known-emitted.
  // Otherwise, mark the call in our call graph so we can traverse it later.
  bool CallerKnownEmitted = IsKnownEmitted(*this, Caller);
  if (CallerKnownEmitted) {
    // Host-side references to a __global__ function refer to the stub, so the
    // function itself is never emitted and therefore should not be marked.
    if (getLangOpts().CUDAIsDevice || IdentifyCUDATarget(Callee) != CFT_Global)
      MarkKnownEmitted(*this, Caller, Callee, Loc);
  } else {
    // If we have
    //   host fn calls kernel fn calls host+device,
    // the HD function does not get instantiated on the host.  We model this by
    // omitting at the call to the kernel from the callgraph.  This ensures
    // that, when compiling for host, only HD functions actually called from the
    // host get marked as known-emitted.
    if (getLangOpts().CUDAIsDevice || IdentifyCUDATarget(Callee) != CFT_Global)
      CUDACallGraph[Caller].insert({Callee, Loc});
  }

  CUDADiagBuilder::Kind DiagKind = [&] {
    switch (IdentifyCUDAPreference(Caller, Callee)) {
    case CFP_Never:
      return CUDADiagBuilder::K_Immediate;
    case CFP_WrongSide:
      assert(Caller && "WrongSide calls require a non-null caller");
      // If we know the caller will be emitted, we know this wrong-side call
      // will be emitted, so it's an immediate error.  Otherwise, defer the
      // error until we know the caller is emitted.
      return CallerKnownEmitted ? CUDADiagBuilder::K_ImmediateWithCallStack
                                : CUDADiagBuilder::K_Deferred;
    default:
      return CUDADiagBuilder::K_Nop;
    }
  }();

  if (DiagKind == CUDADiagBuilder::K_Nop)
    return true;

  // Avoid emitting this error twice for the same location.  Using a hashtable
  // like this is unfortunate, but because we must continue parsing as normal
  // after encountering a deferred error, it's otherwise very tricky for us to
  // ensure that we only emit this deferred error once.
  if (!LocsWithCUDACallDiags.insert({Caller, Loc}).second)
    return true;

  CUDADiagBuilder(DiagKind, Loc, diag::err_ref_bad_target, Caller, *this)
      << IdentifyCUDATarget(Callee) << Callee << IdentifyCUDATarget(Caller);
  CUDADiagBuilder(DiagKind, Callee->getLocation(), diag::note_previous_decl,
                  Caller, *this)
      << Callee;
  return DiagKind != CUDADiagBuilder::K_Immediate &&
         DiagKind != CUDADiagBuilder::K_ImmediateWithCallStack;
}

void Sema::CUDASetLambdaAttrs(CXXMethodDecl *Method) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  if (Method->hasAttr<CUDAHostAttr>() || Method->hasAttr<CUDADeviceAttr>())
    return;
  FunctionDecl *CurFn = dyn_cast<FunctionDecl>(CurContext);
  if (!CurFn)
    return;
  CUDAFunctionTarget Target = IdentifyCUDATarget(CurFn);
  if (Target == CFT_Global || Target == CFT_Device) {
    Method->addAttr(CUDADeviceAttr::CreateImplicit(Context));
  } else if (Target == CFT_HostDevice) {
    Method->addAttr(CUDADeviceAttr::CreateImplicit(Context));
    Method->addAttr(CUDAHostAttr::CreateImplicit(Context));
  }
}

void Sema::checkCUDATargetOverload(FunctionDecl *NewFD,
                                   const LookupResult &Previous) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  CUDAFunctionTarget NewTarget = IdentifyCUDATarget(NewFD);
  for (NamedDecl *OldND : Previous) {
    FunctionDecl *OldFD = OldND->getAsFunction();
    if (!OldFD)
      continue;

    CUDAFunctionTarget OldTarget = IdentifyCUDATarget(OldFD);
    // Don't allow HD and global functions to overload other functions with the
    // same signature.  We allow overloading based on CUDA attributes so that
    // functions can have different implementations on the host and device, but
    // HD/global functions "exist" in some sense on both the host and device, so
    // should have the same implementation on both sides.
    if (NewTarget != OldTarget &&
        ((NewTarget == CFT_HostDevice) || (OldTarget == CFT_HostDevice) ||
         (NewTarget == CFT_Global) || (OldTarget == CFT_Global)) &&
        !IsOverload(NewFD, OldFD, /* UseMemberUsingDeclRules = */ false,
                    /* ConsiderCudaAttrs = */ false)) {
      Diag(NewFD->getLocation(), diag::err_cuda_ovl_target)
          << NewTarget << NewFD->getDeclName() << OldTarget << OldFD;
      Diag(OldFD->getLocation(), diag::note_previous_declaration);
      NewFD->setInvalidDecl();
      break;
    }
  }
}

template <typename AttrTy>
static void copyAttrIfPresent(Sema &S, FunctionDecl *FD,
                              const FunctionDecl &TemplateFD) {
  if (AttrTy *Attribute = TemplateFD.getAttr<AttrTy>()) {
    AttrTy *Clone = Attribute->clone(S.Context);
    Clone->setInherited(true);
    FD->addAttr(Clone);
  }
}

void Sema::inheritCUDATargetAttrs(FunctionDecl *FD,
                                  const FunctionTemplateDecl &TD) {
  const FunctionDecl &TemplateFD = *TD.getTemplatedDecl();
  copyAttrIfPresent<CUDAGlobalAttr>(*this, FD, TemplateFD);
  copyAttrIfPresent<CUDAHostAttr>(*this, FD, TemplateFD);
  copyAttrIfPresent<CUDADeviceAttr>(*this, FD, TemplateFD);
}
