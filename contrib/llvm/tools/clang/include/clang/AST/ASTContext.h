//===- ASTContext.h - Context to hold long-lived AST nodes ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::ASTContext interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONTEXT_H
#define LLVM_CLANG_AST_ASTCONTEXT_H

#include "clang/AST/ASTContextAllocate.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SanitizerBlacklist.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/XRayLists.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {

struct fltSemantics;

} // namespace llvm

namespace clang {

class APFixedPoint;
class APValue;
class ASTMutationListener;
class ASTRecordLayout;
class AtomicExpr;
class BlockExpr;
class BuiltinTemplateDecl;
class CharUnits;
class CXXABI;
class CXXConstructorDecl;
class CXXMethodDecl;
class CXXRecordDecl;
class DiagnosticsEngine;
class Expr;
class FixedPointSemantics;
class MangleContext;
class MangleNumberingContext;
class MaterializeTemporaryExpr;
class MemberSpecializationInfo;
class Module;
class ObjCCategoryDecl;
class ObjCCategoryImplDecl;
class ObjCContainerDecl;
class ObjCImplDecl;
class ObjCImplementationDecl;
class ObjCInterfaceDecl;
class ObjCIvarDecl;
class ObjCMethodDecl;
class ObjCPropertyDecl;
class ObjCPropertyImplDecl;
class ObjCProtocolDecl;
class ObjCTypeParamDecl;
class Preprocessor;
class Stmt;
class StoredDeclsMap;
class TemplateDecl;
class TemplateParameterList;
class TemplateTemplateParmDecl;
class TemplateTypeParmDecl;
class UnresolvedSetIterator;
class UsingShadowDecl;
class VarTemplateDecl;
class VTableContextBase;

namespace Builtin {

class Context;

} // namespace Builtin

enum BuiltinTemplateKind : int;

namespace comments {

class FullComment;

} // namespace comments

struct TypeInfo {
  uint64_t Width = 0;
  unsigned Align = 0;
  bool AlignIsRequired : 1;

  TypeInfo() : AlignIsRequired(false) {}
  TypeInfo(uint64_t Width, unsigned Align, bool AlignIsRequired)
      : Width(Width), Align(Align), AlignIsRequired(AlignIsRequired) {}
};

/// Holds long-lived AST nodes (such as types and decls) that can be
/// referred to throughout the semantic analysis of a file.
class ASTContext : public RefCountedBase<ASTContext> {
public:
  /// Copy initialization expr of a __block variable and a boolean flag that
  /// indicates whether the expression can throw.
  struct BlockVarCopyInit {
    BlockVarCopyInit() = default;
    BlockVarCopyInit(Expr *CopyExpr, bool CanThrow)
        : ExprAndFlag(CopyExpr, CanThrow) {}
    void setExprAndFlag(Expr *CopyExpr, bool CanThrow) {
      ExprAndFlag.setPointerAndInt(CopyExpr, CanThrow);
    }
    Expr *getCopyExpr() const { return ExprAndFlag.getPointer(); }
    bool canThrow() const { return ExprAndFlag.getInt(); }
    llvm::PointerIntPair<Expr *, 1, bool> ExprAndFlag;
  };

private:
  friend class NestedNameSpecifier;

  mutable SmallVector<Type *, 0> Types;
  mutable llvm::FoldingSet<ExtQuals> ExtQualNodes;
  mutable llvm::FoldingSet<ComplexType> ComplexTypes;
  mutable llvm::FoldingSet<PointerType> PointerTypes;
  mutable llvm::FoldingSet<AdjustedType> AdjustedTypes;
  mutable llvm::FoldingSet<BlockPointerType> BlockPointerTypes;
  mutable llvm::FoldingSet<LValueReferenceType> LValueReferenceTypes;
  mutable llvm::FoldingSet<RValueReferenceType> RValueReferenceTypes;
  mutable llvm::FoldingSet<MemberPointerType> MemberPointerTypes;
  mutable llvm::FoldingSet<ConstantArrayType> ConstantArrayTypes;
  mutable llvm::FoldingSet<IncompleteArrayType> IncompleteArrayTypes;
  mutable std::vector<VariableArrayType*> VariableArrayTypes;
  mutable llvm::FoldingSet<DependentSizedArrayType> DependentSizedArrayTypes;
  mutable llvm::FoldingSet<DependentSizedExtVectorType>
    DependentSizedExtVectorTypes;
  mutable llvm::FoldingSet<DependentAddressSpaceType>
      DependentAddressSpaceTypes;
  mutable llvm::FoldingSet<VectorType> VectorTypes;
  mutable llvm::FoldingSet<DependentVectorType> DependentVectorTypes;
  mutable llvm::FoldingSet<FunctionNoProtoType> FunctionNoProtoTypes;
  mutable llvm::ContextualFoldingSet<FunctionProtoType, ASTContext&>
    FunctionProtoTypes;
  mutable llvm::FoldingSet<DependentTypeOfExprType> DependentTypeOfExprTypes;
  mutable llvm::FoldingSet<DependentDecltypeType> DependentDecltypeTypes;
  mutable llvm::FoldingSet<TemplateTypeParmType> TemplateTypeParmTypes;
  mutable llvm::FoldingSet<ObjCTypeParamType> ObjCTypeParamTypes;
  mutable llvm::FoldingSet<SubstTemplateTypeParmType>
    SubstTemplateTypeParmTypes;
  mutable llvm::FoldingSet<SubstTemplateTypeParmPackType>
    SubstTemplateTypeParmPackTypes;
  mutable llvm::ContextualFoldingSet<TemplateSpecializationType, ASTContext&>
    TemplateSpecializationTypes;
  mutable llvm::FoldingSet<ParenType> ParenTypes;
  mutable llvm::FoldingSet<ElaboratedType> ElaboratedTypes;
  mutable llvm::FoldingSet<DependentNameType> DependentNameTypes;
  mutable llvm::ContextualFoldingSet<DependentTemplateSpecializationType,
                                     ASTContext&>
    DependentTemplateSpecializationTypes;
  llvm::FoldingSet<PackExpansionType> PackExpansionTypes;
  mutable llvm::FoldingSet<ObjCObjectTypeImpl> ObjCObjectTypes;
  mutable llvm::FoldingSet<ObjCObjectPointerType> ObjCObjectPointerTypes;
  mutable llvm::FoldingSet<DependentUnaryTransformType>
    DependentUnaryTransformTypes;
  mutable llvm::FoldingSet<AutoType> AutoTypes;
  mutable llvm::FoldingSet<DeducedTemplateSpecializationType>
    DeducedTemplateSpecializationTypes;
  mutable llvm::FoldingSet<AtomicType> AtomicTypes;
  llvm::FoldingSet<AttributedType> AttributedTypes;
  mutable llvm::FoldingSet<PipeType> PipeTypes;

  mutable llvm::FoldingSet<QualifiedTemplateName> QualifiedTemplateNames;
  mutable llvm::FoldingSet<DependentTemplateName> DependentTemplateNames;
  mutable llvm::FoldingSet<SubstTemplateTemplateParmStorage>
    SubstTemplateTemplateParms;
  mutable llvm::ContextualFoldingSet<SubstTemplateTemplateParmPackStorage,
                                     ASTContext&>
    SubstTemplateTemplateParmPacks;

  /// The set of nested name specifiers.
  ///
  /// This set is managed by the NestedNameSpecifier class.
  mutable llvm::FoldingSet<NestedNameSpecifier> NestedNameSpecifiers;
  mutable NestedNameSpecifier *GlobalNestedNameSpecifier = nullptr;

  /// A cache mapping from RecordDecls to ASTRecordLayouts.
  ///
  /// This is lazily created.  This is intentionally not serialized.
  mutable llvm::DenseMap<const RecordDecl*, const ASTRecordLayout*>
    ASTRecordLayouts;
  mutable llvm::DenseMap<const ObjCContainerDecl*, const ASTRecordLayout*>
    ObjCLayouts;

  /// A cache from types to size and alignment information.
  using TypeInfoMap = llvm::DenseMap<const Type *, struct TypeInfo>;
  mutable TypeInfoMap MemoizedTypeInfo;

  /// A cache from types to unadjusted alignment information. Only ARM and
  /// AArch64 targets need this information, keeping it separate prevents
  /// imposing overhead on TypeInfo size.
  using UnadjustedAlignMap = llvm::DenseMap<const Type *, unsigned>;
  mutable UnadjustedAlignMap MemoizedUnadjustedAlign;

  /// A cache mapping from CXXRecordDecls to key functions.
  llvm::DenseMap<const CXXRecordDecl*, LazyDeclPtr> KeyFunctions;

  /// Mapping from ObjCContainers to their ObjCImplementations.
  llvm::DenseMap<ObjCContainerDecl*, ObjCImplDecl*> ObjCImpls;

  /// Mapping from ObjCMethod to its duplicate declaration in the same
  /// interface.
  llvm::DenseMap<const ObjCMethodDecl*,const ObjCMethodDecl*> ObjCMethodRedecls;

  /// Mapping from __block VarDecls to BlockVarCopyInit.
  llvm::DenseMap<const VarDecl *, BlockVarCopyInit> BlockVarCopyInits;

  /// Mapping from class scope functions specialization to their
  /// template patterns.
  llvm::DenseMap<const FunctionDecl*, FunctionDecl*>
    ClassScopeSpecializationPattern;

  /// Mapping from materialized temporaries with static storage duration
  /// that appear in constant initializers to their evaluated values.  These are
  /// allocated in a std::map because their address must be stable.
  llvm::DenseMap<const MaterializeTemporaryExpr *, APValue *>
    MaterializedTemporaryValues;

  /// Representation of a "canonical" template template parameter that
  /// is used in canonical template names.
  class CanonicalTemplateTemplateParm : public llvm::FoldingSetNode {
    TemplateTemplateParmDecl *Parm;

  public:
    CanonicalTemplateTemplateParm(TemplateTemplateParmDecl *Parm)
        : Parm(Parm) {}

    TemplateTemplateParmDecl *getParam() const { return Parm; }

    void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, Parm); }

    static void Profile(llvm::FoldingSetNodeID &ID,
                        TemplateTemplateParmDecl *Parm);
  };
  mutable llvm::FoldingSet<CanonicalTemplateTemplateParm>
    CanonTemplateTemplateParms;

  TemplateTemplateParmDecl *
    getCanonicalTemplateTemplateParmDecl(TemplateTemplateParmDecl *TTP) const;

  /// The typedef for the __int128_t type.
  mutable TypedefDecl *Int128Decl = nullptr;

  /// The typedef for the __uint128_t type.
  mutable TypedefDecl *UInt128Decl = nullptr;

  /// The typedef for the target specific predefined
  /// __builtin_va_list type.
  mutable TypedefDecl *BuiltinVaListDecl = nullptr;

  /// The typedef for the predefined \c __builtin_ms_va_list type.
  mutable TypedefDecl *BuiltinMSVaListDecl = nullptr;

  /// The typedef for the predefined \c id type.
  mutable TypedefDecl *ObjCIdDecl = nullptr;

  /// The typedef for the predefined \c SEL type.
  mutable TypedefDecl *ObjCSelDecl = nullptr;

  /// The typedef for the predefined \c Class type.
  mutable TypedefDecl *ObjCClassDecl = nullptr;

  /// The typedef for the predefined \c Protocol class in Objective-C.
  mutable ObjCInterfaceDecl *ObjCProtocolClassDecl = nullptr;

  /// The typedef for the predefined 'BOOL' type.
  mutable TypedefDecl *BOOLDecl = nullptr;

  // Typedefs which may be provided defining the structure of Objective-C
  // pseudo-builtins
  QualType ObjCIdRedefinitionType;
  QualType ObjCClassRedefinitionType;
  QualType ObjCSelRedefinitionType;

  /// The identifier 'bool'.
  mutable IdentifierInfo *BoolName = nullptr;

  /// The identifier 'NSObject'.
  mutable IdentifierInfo *NSObjectName = nullptr;

  /// The identifier 'NSCopying'.
  IdentifierInfo *NSCopyingName = nullptr;

  /// The identifier '__make_integer_seq'.
  mutable IdentifierInfo *MakeIntegerSeqName = nullptr;

  /// The identifier '__type_pack_element'.
  mutable IdentifierInfo *TypePackElementName = nullptr;

  QualType ObjCConstantStringType;
  mutable RecordDecl *CFConstantStringTagDecl = nullptr;
  mutable TypedefDecl *CFConstantStringTypeDecl = nullptr;

  mutable QualType ObjCSuperType;

  QualType ObjCNSStringType;

  /// The typedef declaration for the Objective-C "instancetype" type.
  TypedefDecl *ObjCInstanceTypeDecl = nullptr;

  /// The type for the C FILE type.
  TypeDecl *FILEDecl = nullptr;

  /// The type for the C jmp_buf type.
  TypeDecl *jmp_bufDecl = nullptr;

  /// The type for the C sigjmp_buf type.
  TypeDecl *sigjmp_bufDecl = nullptr;

  /// The type for the C ucontext_t type.
  TypeDecl *ucontext_tDecl = nullptr;

  /// Type for the Block descriptor for Blocks CodeGen.
  ///
  /// Since this is only used for generation of debug info, it is not
  /// serialized.
  mutable RecordDecl *BlockDescriptorType = nullptr;

  /// Type for the Block descriptor for Blocks CodeGen.
  ///
  /// Since this is only used for generation of debug info, it is not
  /// serialized.
  mutable RecordDecl *BlockDescriptorExtendedType = nullptr;

  /// Declaration for the CUDA cudaConfigureCall function.
  FunctionDecl *cudaConfigureCallDecl = nullptr;

  /// Keeps track of all declaration attributes.
  ///
  /// Since so few decls have attrs, we keep them in a hash map instead of
  /// wasting space in the Decl class.
  llvm::DenseMap<const Decl*, AttrVec*> DeclAttrs;

  /// A mapping from non-redeclarable declarations in modules that were
  /// merged with other declarations to the canonical declaration that they were
  /// merged into.
  llvm::DenseMap<Decl*, Decl*> MergedDecls;

  /// A mapping from a defining declaration to a list of modules (other
  /// than the owning module of the declaration) that contain merged
  /// definitions of that entity.
  llvm::DenseMap<NamedDecl*, llvm::TinyPtrVector<Module*>> MergedDefModules;

  /// Initializers for a module, in order. Each Decl will be either
  /// something that has a semantic effect on startup (such as a variable with
  /// a non-constant initializer), or an ImportDecl (which recursively triggers
  /// initialization of another module).
  struct PerModuleInitializers {
    llvm::SmallVector<Decl*, 4> Initializers;
    llvm::SmallVector<uint32_t, 4> LazyInitializers;

    void resolve(ASTContext &Ctx);
  };
  llvm::DenseMap<Module*, PerModuleInitializers*> ModuleInitializers;

  ASTContext &this_() { return *this; }

public:
  /// A type synonym for the TemplateOrInstantiation mapping.
  using TemplateOrSpecializationInfo =
      llvm::PointerUnion<VarTemplateDecl *, MemberSpecializationInfo *>;

private:
  friend class ASTDeclReader;
  friend class ASTReader;
  friend class ASTWriter;
  friend class CXXRecordDecl;

  /// A mapping to contain the template or declaration that
  /// a variable declaration describes or was instantiated from,
  /// respectively.
  ///
  /// For non-templates, this value will be NULL. For variable
  /// declarations that describe a variable template, this will be a
  /// pointer to a VarTemplateDecl. For static data members
  /// of class template specializations, this will be the
  /// MemberSpecializationInfo referring to the member variable that was
  /// instantiated or specialized. Thus, the mapping will keep track of
  /// the static data member templates from which static data members of
  /// class template specializations were instantiated.
  ///
  /// Given the following example:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   static T value;
  /// };
  ///
  /// template<typename T>
  ///   T X<T>::value = T(17);
  ///
  /// int *x = &X<int>::value;
  /// \endcode
  ///
  /// This mapping will contain an entry that maps from the VarDecl for
  /// X<int>::value to the corresponding VarDecl for X<T>::value (within the
  /// class template X) and will be marked TSK_ImplicitInstantiation.
  llvm::DenseMap<const VarDecl *, TemplateOrSpecializationInfo>
  TemplateOrInstantiation;

  /// Keeps track of the declaration from which a using declaration was
  /// created during instantiation.
  ///
  /// The source and target declarations are always a UsingDecl, an
  /// UnresolvedUsingValueDecl, or an UnresolvedUsingTypenameDecl.
  ///
  /// For example:
  /// \code
  /// template<typename T>
  /// struct A {
  ///   void f();
  /// };
  ///
  /// template<typename T>
  /// struct B : A<T> {
  ///   using A<T>::f;
  /// };
  ///
  /// template struct B<int>;
  /// \endcode
  ///
  /// This mapping will contain an entry that maps from the UsingDecl in
  /// B<int> to the UnresolvedUsingDecl in B<T>.
  llvm::DenseMap<NamedDecl *, NamedDecl *> InstantiatedFromUsingDecl;

  llvm::DenseMap<UsingShadowDecl*, UsingShadowDecl*>
    InstantiatedFromUsingShadowDecl;

  llvm::DenseMap<FieldDecl *, FieldDecl *> InstantiatedFromUnnamedFieldDecl;

  /// Mapping that stores the methods overridden by a given C++
  /// member function.
  ///
  /// Since most C++ member functions aren't virtual and therefore
  /// don't override anything, we store the overridden functions in
  /// this map on the side rather than within the CXXMethodDecl structure.
  using CXXMethodVector = llvm::TinyPtrVector<const CXXMethodDecl *>;
  llvm::DenseMap<const CXXMethodDecl *, CXXMethodVector> OverriddenMethods;

  /// Mapping from each declaration context to its corresponding
  /// mangling numbering context (used for constructs like lambdas which
  /// need to be consistently numbered for the mangler).
  llvm::DenseMap<const DeclContext *, std::unique_ptr<MangleNumberingContext>>
      MangleNumberingContexts;

  /// Side-table of mangling numbers for declarations which rarely
  /// need them (like static local vars).
  llvm::MapVector<const NamedDecl *, unsigned> MangleNumbers;
  llvm::MapVector<const VarDecl *, unsigned> StaticLocalNumbers;

  /// Mapping that stores parameterIndex values for ParmVarDecls when
  /// that value exceeds the bitfield size of ParmVarDeclBits.ParameterIndex.
  using ParameterIndexTable = llvm::DenseMap<const VarDecl *, unsigned>;
  ParameterIndexTable ParamIndices;

  ImportDecl *FirstLocalImport = nullptr;
  ImportDecl *LastLocalImport = nullptr;

  TranslationUnitDecl *TUDecl;
  mutable ExternCContextDecl *ExternCContext = nullptr;
  mutable BuiltinTemplateDecl *MakeIntegerSeqDecl = nullptr;
  mutable BuiltinTemplateDecl *TypePackElementDecl = nullptr;

  /// The associated SourceManager object.
  SourceManager &SourceMgr;

  /// The language options used to create the AST associated with
  ///  this ASTContext object.
  LangOptions &LangOpts;

  /// Blacklist object that is used by sanitizers to decide which
  /// entities should not be instrumented.
  std::unique_ptr<SanitizerBlacklist> SanitizerBL;

  /// Function filtering mechanism to determine whether a given function
  /// should be imbued with the XRay "always" or "never" attributes.
  std::unique_ptr<XRayFunctionFilter> XRayFilter;

  /// The allocator used to create AST objects.
  ///
  /// AST objects are never destructed; rather, all memory associated with the
  /// AST objects will be released when the ASTContext itself is destroyed.
  mutable llvm::BumpPtrAllocator BumpAlloc;

  /// Allocator for partial diagnostics.
  PartialDiagnostic::StorageAllocator DiagAllocator;

  /// The current C++ ABI.
  std::unique_ptr<CXXABI> ABI;
  CXXABI *createCXXABI(const TargetInfo &T);

  /// The logical -> physical address space map.
  const LangASMap *AddrSpaceMap = nullptr;

  /// Address space map mangling must be used with language specific
  /// address spaces (e.g. OpenCL/CUDA)
  bool AddrSpaceMapMangling;

  const TargetInfo *Target = nullptr;
  const TargetInfo *AuxTarget = nullptr;
  clang::PrintingPolicy PrintingPolicy;

public:
  IdentifierTable &Idents;
  SelectorTable &Selectors;
  Builtin::Context &BuiltinInfo;
  mutable DeclarationNameTable DeclarationNames;
  IntrusiveRefCntPtr<ExternalASTSource> ExternalSource;
  ASTMutationListener *Listener = nullptr;

  /// Container for either a single DynTypedNode or for an ArrayRef to
  /// DynTypedNode. For use with ParentMap.
  class DynTypedNodeList {
    using DynTypedNode = ast_type_traits::DynTypedNode;

    llvm::AlignedCharArrayUnion<ast_type_traits::DynTypedNode,
                                ArrayRef<DynTypedNode>> Storage;
    bool IsSingleNode;

  public:
    DynTypedNodeList(const DynTypedNode &N) : IsSingleNode(true) {
      new (Storage.buffer) DynTypedNode(N);
    }

    DynTypedNodeList(ArrayRef<DynTypedNode> A) : IsSingleNode(false) {
      new (Storage.buffer) ArrayRef<DynTypedNode>(A);
    }

    const ast_type_traits::DynTypedNode *begin() const {
      if (!IsSingleNode)
        return reinterpret_cast<const ArrayRef<DynTypedNode> *>(Storage.buffer)
            ->begin();
      return reinterpret_cast<const DynTypedNode *>(Storage.buffer);
    }

    const ast_type_traits::DynTypedNode *end() const {
      if (!IsSingleNode)
        return reinterpret_cast<const ArrayRef<DynTypedNode> *>(Storage.buffer)
            ->end();
      return reinterpret_cast<const DynTypedNode *>(Storage.buffer) + 1;
    }

    size_t size() const { return end() - begin(); }
    bool empty() const { return begin() == end(); }

    const DynTypedNode &operator[](size_t N) const {
      assert(N < size() && "Out of bounds!");
      return *(begin() + N);
    }
  };

  // A traversal scope limits the parts of the AST visible to certain analyses.
  // RecursiveASTVisitor::TraverseAST will only visit reachable nodes, and
  // getParents() will only observe reachable parent edges.
  //
  // The scope is defined by a set of "top-level" declarations.
  // Initially, it is the entire TU: {getTranslationUnitDecl()}.
  // Changing the scope clears the parent cache, which is expensive to rebuild.
  std::vector<Decl *> getTraversalScope() const { return TraversalScope; }
  void setTraversalScope(const std::vector<Decl *> &);

  /// Returns the parents of the given node (within the traversal scope).
  ///
  /// Note that this will lazily compute the parents of all nodes
  /// and store them for later retrieval. Thus, the first call is O(n)
  /// in the number of AST nodes.
  ///
  /// Caveats and FIXMEs:
  /// Calculating the parent map over all AST nodes will need to load the
  /// full AST. This can be undesirable in the case where the full AST is
  /// expensive to create (for example, when using precompiled header
  /// preambles). Thus, there are good opportunities for optimization here.
  /// One idea is to walk the given node downwards, looking for references
  /// to declaration contexts - once a declaration context is found, compute
  /// the parent map for the declaration context; if that can satisfy the
  /// request, loading the whole AST can be avoided. Note that this is made
  /// more complex by statements in templates having multiple parents - those
  /// problems can be solved by building closure over the templated parts of
  /// the AST, which also avoids touching large parts of the AST.
  /// Additionally, we will want to add an interface to already give a hint
  /// where to search for the parents, for example when looking at a statement
  /// inside a certain function.
  ///
  /// 'NodeT' can be one of Decl, Stmt, Type, TypeLoc,
  /// NestedNameSpecifier or NestedNameSpecifierLoc.
  template <typename NodeT> DynTypedNodeList getParents(const NodeT &Node) {
    return getParents(ast_type_traits::DynTypedNode::create(Node));
  }

  DynTypedNodeList getParents(const ast_type_traits::DynTypedNode &Node);

  const clang::PrintingPolicy &getPrintingPolicy() const {
    return PrintingPolicy;
  }

  void setPrintingPolicy(const clang::PrintingPolicy &Policy) {
    PrintingPolicy = Policy;
  }

  SourceManager& getSourceManager() { return SourceMgr; }
  const SourceManager& getSourceManager() const { return SourceMgr; }

  llvm::BumpPtrAllocator &getAllocator() const {
    return BumpAlloc;
  }

  void *Allocate(size_t Size, unsigned Align = 8) const {
    return BumpAlloc.Allocate(Size, Align);
  }
  template <typename T> T *Allocate(size_t Num = 1) const {
    return static_cast<T *>(Allocate(Num * sizeof(T), alignof(T)));
  }
  void Deallocate(void *Ptr) const {}

  /// Return the total amount of physical memory allocated for representing
  /// AST nodes and type information.
  size_t getASTAllocatedMemory() const {
    return BumpAlloc.getTotalMemory();
  }

  /// Return the total memory used for various side tables.
  size_t getSideTableAllocatedMemory() const;

  PartialDiagnostic::StorageAllocator &getDiagAllocator() {
    return DiagAllocator;
  }

  const TargetInfo &getTargetInfo() const { return *Target; }
  const TargetInfo *getAuxTargetInfo() const { return AuxTarget; }

  /// getIntTypeForBitwidth -
  /// sets integer QualTy according to specified details:
  /// bitwidth, signed/unsigned.
  /// Returns empty type if there is no appropriate target types.
  QualType getIntTypeForBitwidth(unsigned DestWidth,
                                 unsigned Signed) const;

  /// getRealTypeForBitwidth -
  /// sets floating point QualTy according to specified bitwidth.
  /// Returns empty type if there is no appropriate target types.
  QualType getRealTypeForBitwidth(unsigned DestWidth) const;

  bool AtomicUsesUnsupportedLibcall(const AtomicExpr *E) const;

  const LangOptions& getLangOpts() const { return LangOpts; }

  const SanitizerBlacklist &getSanitizerBlacklist() const {
    return *SanitizerBL;
  }

  const XRayFunctionFilter &getXRayFilter() const {
    return *XRayFilter;
  }

  DiagnosticsEngine &getDiagnostics() const;

  FullSourceLoc getFullLoc(SourceLocation Loc) const {
    return FullSourceLoc(Loc,SourceMgr);
  }

  /// All comments in this translation unit.
  RawCommentList Comments;

  /// True if comments are already loaded from ExternalASTSource.
  mutable bool CommentsLoaded = false;

  class RawCommentAndCacheFlags {
  public:
    enum Kind {
      /// We searched for a comment attached to the particular declaration, but
      /// didn't find any.
      ///
      /// getRaw() == 0.
      NoCommentInDecl = 0,

      /// We have found a comment attached to this particular declaration.
      ///
      /// getRaw() != 0.
      FromDecl,

      /// This declaration does not have an attached comment, and we have
      /// searched the redeclaration chain.
      ///
      /// If getRaw() == 0, the whole redeclaration chain does not have any
      /// comments.
      ///
      /// If getRaw() != 0, it is a comment propagated from other
      /// redeclaration.
      FromRedecl
    };

    Kind getKind() const LLVM_READONLY {
      return Data.getInt();
    }

    void setKind(Kind K) {
      Data.setInt(K);
    }

    const RawComment *getRaw() const LLVM_READONLY {
      return Data.getPointer();
    }

    void setRaw(const RawComment *RC) {
      Data.setPointer(RC);
    }

    const Decl *getOriginalDecl() const LLVM_READONLY {
      return OriginalDecl;
    }

    void setOriginalDecl(const Decl *Orig) {
      OriginalDecl = Orig;
    }

  private:
    llvm::PointerIntPair<const RawComment *, 2, Kind> Data;
    const Decl *OriginalDecl;
  };

  /// Mapping from declarations to comments attached to any
  /// redeclaration.
  ///
  /// Raw comments are owned by Comments list.  This mapping is populated
  /// lazily.
  mutable llvm::DenseMap<const Decl *, RawCommentAndCacheFlags> RedeclComments;

  /// Mapping from declarations to parsed comments attached to any
  /// redeclaration.
  mutable llvm::DenseMap<const Decl *, comments::FullComment *> ParsedComments;

  /// Return the documentation comment attached to a given declaration,
  /// without looking into cache.
  RawComment *getRawCommentForDeclNoCache(const Decl *D) const;

public:
  RawCommentList &getRawCommentList() {
    return Comments;
  }

  void addComment(const RawComment &RC) {
    assert(LangOpts.RetainCommentsFromSystemHeaders ||
           !SourceMgr.isInSystemHeader(RC.getSourceRange().getBegin()));
    Comments.addComment(RC, LangOpts.CommentOpts, BumpAlloc);
  }

  /// Return the documentation comment attached to a given declaration.
  /// Returns nullptr if no comment is attached.
  ///
  /// \param OriginalDecl if not nullptr, is set to declaration AST node that
  /// had the comment, if the comment we found comes from a redeclaration.
  const RawComment *
  getRawCommentForAnyRedecl(const Decl *D,
                            const Decl **OriginalDecl = nullptr) const;

  /// Return parsed documentation comment attached to a given declaration.
  /// Returns nullptr if no comment is attached.
  ///
  /// \param PP the Preprocessor used with this TU.  Could be nullptr if
  /// preprocessor is not available.
  comments::FullComment *getCommentForDecl(const Decl *D,
                                           const Preprocessor *PP) const;

  /// Return parsed documentation comment attached to a given declaration.
  /// Returns nullptr if no comment is attached. Does not look at any
  /// redeclarations of the declaration.
  comments::FullComment *getLocalCommentForDeclUncached(const Decl *D) const;

  comments::FullComment *cloneFullComment(comments::FullComment *FC,
                                         const Decl *D) const;

private:
  mutable comments::CommandTraits CommentCommandTraits;

  /// Iterator that visits import declarations.
  class import_iterator {
    ImportDecl *Import = nullptr;

  public:
    using value_type = ImportDecl *;
    using reference = ImportDecl *;
    using pointer = ImportDecl *;
    using difference_type = int;
    using iterator_category = std::forward_iterator_tag;

    import_iterator() = default;
    explicit import_iterator(ImportDecl *Import) : Import(Import) {}

    reference operator*() const { return Import; }
    pointer operator->() const { return Import; }

    import_iterator &operator++() {
      Import = ASTContext::getNextLocalImport(Import);
      return *this;
    }

    import_iterator operator++(int) {
      import_iterator Other(*this);
      ++(*this);
      return Other;
    }

    friend bool operator==(import_iterator X, import_iterator Y) {
      return X.Import == Y.Import;
    }

    friend bool operator!=(import_iterator X, import_iterator Y) {
      return X.Import != Y.Import;
    }
  };

public:
  comments::CommandTraits &getCommentCommandTraits() const {
    return CommentCommandTraits;
  }

  /// Retrieve the attributes for the given declaration.
  AttrVec& getDeclAttrs(const Decl *D);

  /// Erase the attributes corresponding to the given declaration.
  void eraseDeclAttrs(const Decl *D);

  /// If this variable is an instantiated static data member of a
  /// class template specialization, returns the templated static data member
  /// from which it was instantiated.
  // FIXME: Remove ?
  MemberSpecializationInfo *getInstantiatedFromStaticDataMember(
                                                           const VarDecl *Var);

  TemplateOrSpecializationInfo
  getTemplateOrSpecializationInfo(const VarDecl *Var);

  FunctionDecl *getClassScopeSpecializationPattern(const FunctionDecl *FD);

  void setClassScopeSpecializationPattern(FunctionDecl *FD,
                                          FunctionDecl *Pattern);

  /// Note that the static data member \p Inst is an instantiation of
  /// the static data member template \p Tmpl of a class template.
  void setInstantiatedFromStaticDataMember(VarDecl *Inst, VarDecl *Tmpl,
                                           TemplateSpecializationKind TSK,
                        SourceLocation PointOfInstantiation = SourceLocation());

  void setTemplateOrSpecializationInfo(VarDecl *Inst,
                                       TemplateOrSpecializationInfo TSI);

  /// If the given using decl \p Inst is an instantiation of a
  /// (possibly unresolved) using decl from a template instantiation,
  /// return it.
  NamedDecl *getInstantiatedFromUsingDecl(NamedDecl *Inst);

  /// Remember that the using decl \p Inst is an instantiation
  /// of the using decl \p Pattern of a class template.
  void setInstantiatedFromUsingDecl(NamedDecl *Inst, NamedDecl *Pattern);

  void setInstantiatedFromUsingShadowDecl(UsingShadowDecl *Inst,
                                          UsingShadowDecl *Pattern);
  UsingShadowDecl *getInstantiatedFromUsingShadowDecl(UsingShadowDecl *Inst);

  FieldDecl *getInstantiatedFromUnnamedFieldDecl(FieldDecl *Field);

  void setInstantiatedFromUnnamedFieldDecl(FieldDecl *Inst, FieldDecl *Tmpl);

  // Access to the set of methods overridden by the given C++ method.
  using overridden_cxx_method_iterator = CXXMethodVector::const_iterator;
  overridden_cxx_method_iterator
  overridden_methods_begin(const CXXMethodDecl *Method) const;

  overridden_cxx_method_iterator
  overridden_methods_end(const CXXMethodDecl *Method) const;

  unsigned overridden_methods_size(const CXXMethodDecl *Method) const;

  using overridden_method_range =
      llvm::iterator_range<overridden_cxx_method_iterator>;

  overridden_method_range overridden_methods(const CXXMethodDecl *Method) const;

  /// Note that the given C++ \p Method overrides the given \p
  /// Overridden method.
  void addOverriddenMethod(const CXXMethodDecl *Method,
                           const CXXMethodDecl *Overridden);

  /// Return C++ or ObjC overridden methods for the given \p Method.
  ///
  /// An ObjC method is considered to override any method in the class's
  /// base classes, its protocols, or its categories' protocols, that has
  /// the same selector and is of the same kind (class or instance).
  /// A method in an implementation is not considered as overriding the same
  /// method in the interface or its categories.
  void getOverriddenMethods(
                        const NamedDecl *Method,
                        SmallVectorImpl<const NamedDecl *> &Overridden) const;

  /// Notify the AST context that a new import declaration has been
  /// parsed or implicitly created within this translation unit.
  void addedLocalImportDecl(ImportDecl *Import);

  static ImportDecl *getNextLocalImport(ImportDecl *Import) {
    return Import->NextLocalImport;
  }

  using import_range = llvm::iterator_range<import_iterator>;

  import_range local_imports() const {
    return import_range(import_iterator(FirstLocalImport), import_iterator());
  }

  Decl *getPrimaryMergedDecl(Decl *D) {
    Decl *Result = MergedDecls.lookup(D);
    return Result ? Result : D;
  }
  void setPrimaryMergedDecl(Decl *D, Decl *Primary) {
    MergedDecls[D] = Primary;
  }

  /// Note that the definition \p ND has been merged into module \p M,
  /// and should be visible whenever \p M is visible.
  void mergeDefinitionIntoModule(NamedDecl *ND, Module *M,
                                 bool NotifyListeners = true);

  /// Clean up the merged definition list. Call this if you might have
  /// added duplicates into the list.
  void deduplicateMergedDefinitonsFor(NamedDecl *ND);

  /// Get the additional modules in which the definition \p Def has
  /// been merged.
  ArrayRef<Module*> getModulesWithMergedDefinition(const NamedDecl *Def) {
    auto MergedIt =
        MergedDefModules.find(cast<NamedDecl>(Def->getCanonicalDecl()));
    if (MergedIt == MergedDefModules.end())
      return None;
    return MergedIt->second;
  }

  /// Add a declaration to the list of declarations that are initialized
  /// for a module. This will typically be a global variable (with internal
  /// linkage) that runs module initializers, such as the iostream initializer,
  /// or an ImportDecl nominating another module that has initializers.
  void addModuleInitializer(Module *M, Decl *Init);

  void addLazyModuleInitializers(Module *M, ArrayRef<uint32_t> IDs);

  /// Get the initializations to perform when importing a module, if any.
  ArrayRef<Decl*> getModuleInitializers(Module *M);

  TranslationUnitDecl *getTranslationUnitDecl() const { return TUDecl; }

  ExternCContextDecl *getExternCContextDecl() const;
  BuiltinTemplateDecl *getMakeIntegerSeqDecl() const;
  BuiltinTemplateDecl *getTypePackElementDecl() const;

  // Builtin Types.
  CanQualType VoidTy;
  CanQualType BoolTy;
  CanQualType CharTy;
  CanQualType WCharTy;  // [C++ 3.9.1p5].
  CanQualType WideCharTy; // Same as WCharTy in C++, integer type in C99.
  CanQualType WIntTy;   // [C99 7.24.1], integer type unchanged by default promotions.
  CanQualType Char8Ty;  // [C++20 proposal]
  CanQualType Char16Ty; // [C++0x 3.9.1p5], integer type in C99.
  CanQualType Char32Ty; // [C++0x 3.9.1p5], integer type in C99.
  CanQualType SignedCharTy, ShortTy, IntTy, LongTy, LongLongTy, Int128Ty;
  CanQualType UnsignedCharTy, UnsignedShortTy, UnsignedIntTy, UnsignedLongTy;
  CanQualType UnsignedLongLongTy, UnsignedInt128Ty;
  CanQualType FloatTy, DoubleTy, LongDoubleTy, Float128Ty;
  CanQualType ShortAccumTy, AccumTy,
      LongAccumTy;  // ISO/IEC JTC1 SC22 WG14 N1169 Extension
  CanQualType UnsignedShortAccumTy, UnsignedAccumTy, UnsignedLongAccumTy;
  CanQualType ShortFractTy, FractTy, LongFractTy;
  CanQualType UnsignedShortFractTy, UnsignedFractTy, UnsignedLongFractTy;
  CanQualType SatShortAccumTy, SatAccumTy, SatLongAccumTy;
  CanQualType SatUnsignedShortAccumTy, SatUnsignedAccumTy,
      SatUnsignedLongAccumTy;
  CanQualType SatShortFractTy, SatFractTy, SatLongFractTy;
  CanQualType SatUnsignedShortFractTy, SatUnsignedFractTy,
      SatUnsignedLongFractTy;
  CanQualType HalfTy; // [OpenCL 6.1.1.1], ARM NEON
  CanQualType Float16Ty; // C11 extension ISO/IEC TS 18661-3
  CanQualType FloatComplexTy, DoubleComplexTy, LongDoubleComplexTy;
  CanQualType Float128ComplexTy;
  CanQualType VoidPtrTy, NullPtrTy;
  CanQualType DependentTy, OverloadTy, BoundMemberTy, UnknownAnyTy;
  CanQualType BuiltinFnTy;
  CanQualType PseudoObjectTy, ARCUnbridgedCastTy;
  CanQualType ObjCBuiltinIdTy, ObjCBuiltinClassTy, ObjCBuiltinSelTy;
  CanQualType ObjCBuiltinBoolTy;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  CanQualType SingletonId;
#include "clang/Basic/OpenCLImageTypes.def"
  CanQualType OCLSamplerTy, OCLEventTy, OCLClkEventTy;
  CanQualType OCLQueueTy, OCLReserveIDTy;
  CanQualType OMPArraySectionTy;
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  CanQualType Id##Ty;
#include "clang/Basic/OpenCLExtensionTypes.def"

  // Types for deductions in C++0x [stmt.ranged]'s desugaring. Built on demand.
  mutable QualType AutoDeductTy;     // Deduction against 'auto'.
  mutable QualType AutoRRefDeductTy; // Deduction against 'auto &&'.

  // Decl used to help define __builtin_va_list for some targets.
  // The decl is built when constructing 'BuiltinVaListDecl'.
  mutable Decl *VaListTagDecl;

  ASTContext(LangOptions &LOpts, SourceManager &SM, IdentifierTable &idents,
             SelectorTable &sels, Builtin::Context &builtins);
  ASTContext(const ASTContext &) = delete;
  ASTContext &operator=(const ASTContext &) = delete;
  ~ASTContext();

  /// Attach an external AST source to the AST context.
  ///
  /// The external AST source provides the ability to load parts of
  /// the abstract syntax tree as needed from some external storage,
  /// e.g., a precompiled header.
  void setExternalSource(IntrusiveRefCntPtr<ExternalASTSource> Source);

  /// Retrieve a pointer to the external AST source associated
  /// with this AST context, if any.
  ExternalASTSource *getExternalSource() const {
    return ExternalSource.get();
  }

  /// Attach an AST mutation listener to the AST context.
  ///
  /// The AST mutation listener provides the ability to track modifications to
  /// the abstract syntax tree entities committed after they were initially
  /// created.
  void setASTMutationListener(ASTMutationListener *Listener) {
    this->Listener = Listener;
  }

  /// Retrieve a pointer to the AST mutation listener associated
  /// with this AST context, if any.
  ASTMutationListener *getASTMutationListener() const { return Listener; }

  void PrintStats() const;
  const SmallVectorImpl<Type *>& getTypes() const { return Types; }

  BuiltinTemplateDecl *buildBuiltinTemplateDecl(BuiltinTemplateKind BTK,
                                                const IdentifierInfo *II) const;

  /// Create a new implicit TU-level CXXRecordDecl or RecordDecl
  /// declaration.
  RecordDecl *buildImplicitRecord(StringRef Name,
                                  RecordDecl::TagKind TK = TTK_Struct) const;

  /// Create a new implicit TU-level typedef declaration.
  TypedefDecl *buildImplicitTypedef(QualType T, StringRef Name) const;

  /// Retrieve the declaration for the 128-bit signed integer type.
  TypedefDecl *getInt128Decl() const;

  /// Retrieve the declaration for the 128-bit unsigned integer type.
  TypedefDecl *getUInt128Decl() const;

  //===--------------------------------------------------------------------===//
  //                           Type Constructors
  //===--------------------------------------------------------------------===//

private:
  /// Return a type with extended qualifiers.
  QualType getExtQualType(const Type *Base, Qualifiers Quals) const;

  QualType getTypeDeclTypeSlow(const TypeDecl *Decl) const;

  QualType getPipeType(QualType T, bool ReadOnly) const;

public:
  /// Return the uniqued reference to the type for an address space
  /// qualified type with the specified type and address space.
  ///
  /// The resulting type has a union of the qualifiers from T and the address
  /// space. If T already has an address space specifier, it is silently
  /// replaced.
  QualType getAddrSpaceQualType(QualType T, LangAS AddressSpace) const;

  /// Remove any existing address space on the type and returns the type
  /// with qualifiers intact (or that's the idea anyway)
  ///
  /// The return type should be T with all prior qualifiers minus the address
  /// space.
  QualType removeAddrSpaceQualType(QualType T) const;

  /// Apply Objective-C protocol qualifiers to the given type.
  /// \param allowOnPointerType specifies if we can apply protocol
  /// qualifiers on ObjCObjectPointerType. It can be set to true when
  /// constructing the canonical type of a Objective-C type parameter.
  QualType applyObjCProtocolQualifiers(QualType type,
      ArrayRef<ObjCProtocolDecl *> protocols, bool &hasError,
      bool allowOnPointerType = false) const;

  /// Return the uniqued reference to the type for an Objective-C
  /// gc-qualified type.
  ///
  /// The resulting type has a union of the qualifiers from T and the gc
  /// attribute.
  QualType getObjCGCQualType(QualType T, Qualifiers::GC gcAttr) const;

  /// Return the uniqued reference to the type for a \c restrict
  /// qualified type.
  ///
  /// The resulting type has a union of the qualifiers from \p T and
  /// \c restrict.
  QualType getRestrictType(QualType T) const {
    return T.withFastQualifiers(Qualifiers::Restrict);
  }

  /// Return the uniqued reference to the type for a \c volatile
  /// qualified type.
  ///
  /// The resulting type has a union of the qualifiers from \p T and
  /// \c volatile.
  QualType getVolatileType(QualType T) const {
    return T.withFastQualifiers(Qualifiers::Volatile);
  }

  /// Return the uniqued reference to the type for a \c const
  /// qualified type.
  ///
  /// The resulting type has a union of the qualifiers from \p T and \c const.
  ///
  /// It can be reasonably expected that this will always be equivalent to
  /// calling T.withConst().
  QualType getConstType(QualType T) const { return T.withConst(); }

  /// Change the ExtInfo on a function type.
  const FunctionType *adjustFunctionType(const FunctionType *Fn,
                                         FunctionType::ExtInfo EInfo);

  /// Adjust the given function result type.
  CanQualType getCanonicalFunctionResultType(QualType ResultType) const;

  /// Change the result type of a function type once it is deduced.
  void adjustDeducedFunctionResultType(FunctionDecl *FD, QualType ResultType);

  /// Get a function type and produce the equivalent function type with the
  /// specified exception specification. Type sugar that can be present on a
  /// declaration of a function with an exception specification is permitted
  /// and preserved. Other type sugar (for instance, typedefs) is not.
  QualType getFunctionTypeWithExceptionSpec(
      QualType Orig, const FunctionProtoType::ExceptionSpecInfo &ESI);

  /// Determine whether two function types are the same, ignoring
  /// exception specifications in cases where they're part of the type.
  bool hasSameFunctionTypeIgnoringExceptionSpec(QualType T, QualType U);

  /// Change the exception specification on a function once it is
  /// delay-parsed, instantiated, or computed.
  void adjustExceptionSpec(FunctionDecl *FD,
                           const FunctionProtoType::ExceptionSpecInfo &ESI,
                           bool AsWritten = false);

  /// Return the uniqued reference to the type for a complex
  /// number with the specified element type.
  QualType getComplexType(QualType T) const;
  CanQualType getComplexType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getComplexType((QualType) T));
  }

  /// Return the uniqued reference to the type for a pointer to
  /// the specified type.
  QualType getPointerType(QualType T) const;
  CanQualType getPointerType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getPointerType((QualType) T));
  }

  /// Return the uniqued reference to a type adjusted from the original
  /// type to a new type.
  QualType getAdjustedType(QualType Orig, QualType New) const;
  CanQualType getAdjustedType(CanQualType Orig, CanQualType New) const {
    return CanQualType::CreateUnsafe(
        getAdjustedType((QualType)Orig, (QualType)New));
  }

  /// Return the uniqued reference to the decayed version of the given
  /// type.  Can only be called on array and function types which decay to
  /// pointer types.
  QualType getDecayedType(QualType T) const;
  CanQualType getDecayedType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getDecayedType((QualType) T));
  }

  /// Return the uniqued reference to the atomic type for the specified
  /// type.
  QualType getAtomicType(QualType T) const;

  /// Return the uniqued reference to the type for a block of the
  /// specified type.
  QualType getBlockPointerType(QualType T) const;

  /// Gets the struct used to keep track of the descriptor for pointer to
  /// blocks.
  QualType getBlockDescriptorType() const;

  /// Return a read_only pipe type for the specified type.
  QualType getReadPipeType(QualType T) const;

  /// Return a write_only pipe type for the specified type.
  QualType getWritePipeType(QualType T) const;

  /// Gets the struct used to keep track of the extended descriptor for
  /// pointer to blocks.
  QualType getBlockDescriptorExtendedType() const;

  /// Map an AST Type to an OpenCLTypeKind enum value.
  TargetInfo::OpenCLTypeKind getOpenCLTypeKind(const Type *T) const;

  /// Get address space for OpenCL type.
  LangAS getOpenCLTypeAddrSpace(const Type *T) const;

  void setcudaConfigureCallDecl(FunctionDecl *FD) {
    cudaConfigureCallDecl = FD;
  }

  FunctionDecl *getcudaConfigureCallDecl() {
    return cudaConfigureCallDecl;
  }

  /// Returns true iff we need copy/dispose helpers for the given type.
  bool BlockRequiresCopying(QualType Ty, const VarDecl *D);

  /// Returns true, if given type has a known lifetime. HasByrefExtendedLayout
  /// is set to false in this case. If HasByrefExtendedLayout returns true,
  /// byref variable has extended lifetime.
  bool getByrefLifetime(QualType Ty,
                        Qualifiers::ObjCLifetime &Lifetime,
                        bool &HasByrefExtendedLayout) const;

  /// Return the uniqued reference to the type for an lvalue reference
  /// to the specified type.
  QualType getLValueReferenceType(QualType T, bool SpelledAsLValue = true)
    const;

  /// Return the uniqued reference to the type for an rvalue reference
  /// to the specified type.
  QualType getRValueReferenceType(QualType T) const;

  /// Return the uniqued reference to the type for a member pointer to
  /// the specified type in the specified class.
  ///
  /// The class \p Cls is a \c Type because it could be a dependent name.
  QualType getMemberPointerType(QualType T, const Type *Cls) const;

  /// Return a non-unique reference to the type for a variable array of
  /// the specified element type.
  QualType getVariableArrayType(QualType EltTy, Expr *NumElts,
                                ArrayType::ArraySizeModifier ASM,
                                unsigned IndexTypeQuals,
                                SourceRange Brackets) const;

  /// Return a non-unique reference to the type for a dependently-sized
  /// array of the specified element type.
  ///
  /// FIXME: We will need these to be uniqued, or at least comparable, at some
  /// point.
  QualType getDependentSizedArrayType(QualType EltTy, Expr *NumElts,
                                      ArrayType::ArraySizeModifier ASM,
                                      unsigned IndexTypeQuals,
                                      SourceRange Brackets) const;

  /// Return a unique reference to the type for an incomplete array of
  /// the specified element type.
  QualType getIncompleteArrayType(QualType EltTy,
                                  ArrayType::ArraySizeModifier ASM,
                                  unsigned IndexTypeQuals) const;

  /// Return the unique reference to the type for a constant array of
  /// the specified element type.
  QualType getConstantArrayType(QualType EltTy, const llvm::APInt &ArySize,
                                ArrayType::ArraySizeModifier ASM,
                                unsigned IndexTypeQuals) const;

  /// Returns a vla type where known sizes are replaced with [*].
  QualType getVariableArrayDecayedType(QualType Ty) const;

  /// Return the unique reference to a vector type of the specified
  /// element type and size.
  ///
  /// \pre \p VectorType must be a built-in type.
  QualType getVectorType(QualType VectorType, unsigned NumElts,
                         VectorType::VectorKind VecKind) const;
  /// Return the unique reference to the type for a dependently sized vector of
  /// the specified element type.
  QualType getDependentVectorType(QualType VectorType, Expr *SizeExpr,
                                  SourceLocation AttrLoc,
                                  VectorType::VectorKind VecKind) const;

  /// Return the unique reference to an extended vector type
  /// of the specified element type and size.
  ///
  /// \pre \p VectorType must be a built-in type.
  QualType getExtVectorType(QualType VectorType, unsigned NumElts) const;

  /// \pre Return a non-unique reference to the type for a dependently-sized
  /// vector of the specified element type.
  ///
  /// FIXME: We will need these to be uniqued, or at least comparable, at some
  /// point.
  QualType getDependentSizedExtVectorType(QualType VectorType,
                                          Expr *SizeExpr,
                                          SourceLocation AttrLoc) const;

  QualType getDependentAddressSpaceType(QualType PointeeType,
                                        Expr *AddrSpaceExpr,
                                        SourceLocation AttrLoc) const;

  /// Return a K&R style C function type like 'int()'.
  QualType getFunctionNoProtoType(QualType ResultTy,
                                  const FunctionType::ExtInfo &Info) const;

  QualType getFunctionNoProtoType(QualType ResultTy) const {
    return getFunctionNoProtoType(ResultTy, FunctionType::ExtInfo());
  }

  /// Return a normal function type with a typed argument list.
  QualType getFunctionType(QualType ResultTy, ArrayRef<QualType> Args,
                           const FunctionProtoType::ExtProtoInfo &EPI) const {
    return getFunctionTypeInternal(ResultTy, Args, EPI, false);
  }

  QualType adjustStringLiteralBaseType(QualType StrLTy) const;

private:
  /// Return a normal function type with a typed argument list.
  QualType getFunctionTypeInternal(QualType ResultTy, ArrayRef<QualType> Args,
                                   const FunctionProtoType::ExtProtoInfo &EPI,
                                   bool OnlyWantCanonical) const;

public:
  /// Return the unique reference to the type for the specified type
  /// declaration.
  QualType getTypeDeclType(const TypeDecl *Decl,
                           const TypeDecl *PrevDecl = nullptr) const {
    assert(Decl && "Passed null for Decl param");
    if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);

    if (PrevDecl) {
      assert(PrevDecl->TypeForDecl && "previous decl has no TypeForDecl");
      Decl->TypeForDecl = PrevDecl->TypeForDecl;
      return QualType(PrevDecl->TypeForDecl, 0);
    }

    return getTypeDeclTypeSlow(Decl);
  }

  /// Return the unique reference to the type for the specified
  /// typedef-name decl.
  QualType getTypedefType(const TypedefNameDecl *Decl,
                          QualType Canon = QualType()) const;

  QualType getRecordType(const RecordDecl *Decl) const;

  QualType getEnumType(const EnumDecl *Decl) const;

  QualType getInjectedClassNameType(CXXRecordDecl *Decl, QualType TST) const;

  QualType getAttributedType(attr::Kind attrKind,
                             QualType modifiedType,
                             QualType equivalentType);

  QualType getSubstTemplateTypeParmType(const TemplateTypeParmType *Replaced,
                                        QualType Replacement) const;
  QualType getSubstTemplateTypeParmPackType(
                                          const TemplateTypeParmType *Replaced,
                                            const TemplateArgument &ArgPack);

  QualType
  getTemplateTypeParmType(unsigned Depth, unsigned Index,
                          bool ParameterPack,
                          TemplateTypeParmDecl *ParmDecl = nullptr) const;

  QualType getTemplateSpecializationType(TemplateName T,
                                         ArrayRef<TemplateArgument> Args,
                                         QualType Canon = QualType()) const;

  QualType
  getCanonicalTemplateSpecializationType(TemplateName T,
                                         ArrayRef<TemplateArgument> Args) const;

  QualType getTemplateSpecializationType(TemplateName T,
                                         const TemplateArgumentListInfo &Args,
                                         QualType Canon = QualType()) const;

  TypeSourceInfo *
  getTemplateSpecializationTypeInfo(TemplateName T, SourceLocation TLoc,
                                    const TemplateArgumentListInfo &Args,
                                    QualType Canon = QualType()) const;

  QualType getParenType(QualType NamedType) const;

  QualType getElaboratedType(ElaboratedTypeKeyword Keyword,
                             NestedNameSpecifier *NNS, QualType NamedType,
                             TagDecl *OwnedTagDecl = nullptr) const;
  QualType getDependentNameType(ElaboratedTypeKeyword Keyword,
                                NestedNameSpecifier *NNS,
                                const IdentifierInfo *Name,
                                QualType Canon = QualType()) const;

  QualType getDependentTemplateSpecializationType(ElaboratedTypeKeyword Keyword,
                                                  NestedNameSpecifier *NNS,
                                                  const IdentifierInfo *Name,
                                    const TemplateArgumentListInfo &Args) const;
  QualType getDependentTemplateSpecializationType(
      ElaboratedTypeKeyword Keyword, NestedNameSpecifier *NNS,
      const IdentifierInfo *Name, ArrayRef<TemplateArgument> Args) const;

  TemplateArgument getInjectedTemplateArg(NamedDecl *ParamDecl);

  /// Get a template argument list with one argument per template parameter
  /// in a template parameter list, such as for the injected class name of
  /// a class template.
  void getInjectedTemplateArgs(const TemplateParameterList *Params,
                               SmallVectorImpl<TemplateArgument> &Args);

  QualType getPackExpansionType(QualType Pattern,
                                Optional<unsigned> NumExpansions);

  QualType getObjCInterfaceType(const ObjCInterfaceDecl *Decl,
                                ObjCInterfaceDecl *PrevDecl = nullptr) const;

  /// Legacy interface: cannot provide type arguments or __kindof.
  QualType getObjCObjectType(QualType Base,
                             ObjCProtocolDecl * const *Protocols,
                             unsigned NumProtocols) const;

  QualType getObjCObjectType(QualType Base,
                             ArrayRef<QualType> typeArgs,
                             ArrayRef<ObjCProtocolDecl *> protocols,
                             bool isKindOf) const;

  QualType getObjCTypeParamType(const ObjCTypeParamDecl *Decl,
                                ArrayRef<ObjCProtocolDecl *> protocols,
                                QualType Canonical = QualType()) const;

  bool ObjCObjectAdoptsQTypeProtocols(QualType QT, ObjCInterfaceDecl *Decl);

  /// QIdProtocolsAdoptObjCObjectProtocols - Checks that protocols in
  /// QT's qualified-id protocol list adopt all protocols in IDecl's list
  /// of protocols.
  bool QIdProtocolsAdoptObjCObjectProtocols(QualType QT,
                                            ObjCInterfaceDecl *IDecl);

  /// Return a ObjCObjectPointerType type for the given ObjCObjectType.
  QualType getObjCObjectPointerType(QualType OIT) const;

  /// GCC extension.
  QualType getTypeOfExprType(Expr *e) const;
  QualType getTypeOfType(QualType t) const;

  /// C++11 decltype.
  QualType getDecltypeType(Expr *e, QualType UnderlyingType) const;

  /// Unary type transforms
  QualType getUnaryTransformType(QualType BaseType, QualType UnderlyingType,
                                 UnaryTransformType::UTTKind UKind) const;

  /// C++11 deduced auto type.
  QualType getAutoType(QualType DeducedType, AutoTypeKeyword Keyword,
                       bool IsDependent) const;

  /// C++11 deduction pattern for 'auto' type.
  QualType getAutoDeductType() const;

  /// C++11 deduction pattern for 'auto &&' type.
  QualType getAutoRRefDeductType() const;

  /// C++17 deduced class template specialization type.
  QualType getDeducedTemplateSpecializationType(TemplateName Template,
                                                QualType DeducedType,
                                                bool IsDependent) const;

  /// Return the unique reference to the type for the specified TagDecl
  /// (struct/union/class/enum) decl.
  QualType getTagDeclType(const TagDecl *Decl) const;

  /// Return the unique type for "size_t" (C99 7.17), defined in
  /// <stddef.h>.
  ///
  /// The sizeof operator requires this (C99 6.5.3.4p4).
  CanQualType getSizeType() const;

  /// Return the unique signed counterpart of
  /// the integer type corresponding to size_t.
  CanQualType getSignedSizeType() const;

  /// Return the unique type for "intmax_t" (C99 7.18.1.5), defined in
  /// <stdint.h>.
  CanQualType getIntMaxType() const;

  /// Return the unique type for "uintmax_t" (C99 7.18.1.5), defined in
  /// <stdint.h>.
  CanQualType getUIntMaxType() const;

  /// Return the unique wchar_t type available in C++ (and available as
  /// __wchar_t as a Microsoft extension).
  QualType getWCharType() const { return WCharTy; }

  /// Return the type of wide characters. In C++, this returns the
  /// unique wchar_t type. In C99, this returns a type compatible with the type
  /// defined in <stddef.h> as defined by the target.
  QualType getWideCharType() const { return WideCharTy; }

  /// Return the type of "signed wchar_t".
  ///
  /// Used when in C++, as a GCC extension.
  QualType getSignedWCharType() const;

  /// Return the type of "unsigned wchar_t".
  ///
  /// Used when in C++, as a GCC extension.
  QualType getUnsignedWCharType() const;

  /// In C99, this returns a type compatible with the type
  /// defined in <stddef.h> as defined by the target.
  QualType getWIntType() const { return WIntTy; }

  /// Return a type compatible with "intptr_t" (C99 7.18.1.4),
  /// as defined by the target.
  QualType getIntPtrType() const;

  /// Return a type compatible with "uintptr_t" (C99 7.18.1.4),
  /// as defined by the target.
  QualType getUIntPtrType() const;

  /// Return the unique type for "ptrdiff_t" (C99 7.17) defined in
  /// <stddef.h>. Pointer - pointer requires this (C99 6.5.6p9).
  QualType getPointerDiffType() const;

  /// Return the unique unsigned counterpart of "ptrdiff_t"
  /// integer type. The standard (C11 7.21.6.1p7) refers to this type
  /// in the definition of %tu format specifier.
  QualType getUnsignedPointerDiffType() const;

  /// Return the unique type for "pid_t" defined in
  /// <sys/types.h>. We need this to compute the correct type for vfork().
  QualType getProcessIDType() const;

  /// Return the C structure type used to represent constant CFStrings.
  QualType getCFConstantStringType() const;

  /// Returns the C struct type for objc_super
  QualType getObjCSuperType() const;
  void setObjCSuperType(QualType ST) { ObjCSuperType = ST; }

  /// Get the structure type used to representation CFStrings, or NULL
  /// if it hasn't yet been built.
  QualType getRawCFConstantStringType() const {
    if (CFConstantStringTypeDecl)
      return getTypedefType(CFConstantStringTypeDecl);
    return QualType();
  }
  void setCFConstantStringType(QualType T);
  TypedefDecl *getCFConstantStringDecl() const;
  RecordDecl *getCFConstantStringTagDecl() const;

  // This setter/getter represents the ObjC type for an NSConstantString.
  void setObjCConstantStringInterface(ObjCInterfaceDecl *Decl);
  QualType getObjCConstantStringInterface() const {
    return ObjCConstantStringType;
  }

  QualType getObjCNSStringType() const {
    return ObjCNSStringType;
  }

  void setObjCNSStringType(QualType T) {
    ObjCNSStringType = T;
  }

  /// Retrieve the type that \c id has been defined to, which may be
  /// different from the built-in \c id if \c id has been typedef'd.
  QualType getObjCIdRedefinitionType() const {
    if (ObjCIdRedefinitionType.isNull())
      return getObjCIdType();
    return ObjCIdRedefinitionType;
  }

  /// Set the user-written type that redefines \c id.
  void setObjCIdRedefinitionType(QualType RedefType) {
    ObjCIdRedefinitionType = RedefType;
  }

  /// Retrieve the type that \c Class has been defined to, which may be
  /// different from the built-in \c Class if \c Class has been typedef'd.
  QualType getObjCClassRedefinitionType() const {
    if (ObjCClassRedefinitionType.isNull())
      return getObjCClassType();
    return ObjCClassRedefinitionType;
  }

  /// Set the user-written type that redefines 'SEL'.
  void setObjCClassRedefinitionType(QualType RedefType) {
    ObjCClassRedefinitionType = RedefType;
  }

  /// Retrieve the type that 'SEL' has been defined to, which may be
  /// different from the built-in 'SEL' if 'SEL' has been typedef'd.
  QualType getObjCSelRedefinitionType() const {
    if (ObjCSelRedefinitionType.isNull())
      return getObjCSelType();
    return ObjCSelRedefinitionType;
  }

  /// Set the user-written type that redefines 'SEL'.
  void setObjCSelRedefinitionType(QualType RedefType) {
    ObjCSelRedefinitionType = RedefType;
  }

  /// Retrieve the identifier 'NSObject'.
  IdentifierInfo *getNSObjectName() const {
    if (!NSObjectName) {
      NSObjectName = &Idents.get("NSObject");
    }

    return NSObjectName;
  }

  /// Retrieve the identifier 'NSCopying'.
  IdentifierInfo *getNSCopyingName() {
    if (!NSCopyingName) {
      NSCopyingName = &Idents.get("NSCopying");
    }

    return NSCopyingName;
  }

  CanQualType getNSUIntegerType() const {
    assert(Target && "Expected target to be initialized");
    const llvm::Triple &T = Target->getTriple();
    // Windows is LLP64 rather than LP64
    if (T.isOSWindows() && T.isArch64Bit())
      return UnsignedLongLongTy;
    return UnsignedLongTy;
  }

  CanQualType getNSIntegerType() const {
    assert(Target && "Expected target to be initialized");
    const llvm::Triple &T = Target->getTriple();
    // Windows is LLP64 rather than LP64
    if (T.isOSWindows() && T.isArch64Bit())
      return LongLongTy;
    return LongTy;
  }

  /// Retrieve the identifier 'bool'.
  IdentifierInfo *getBoolName() const {
    if (!BoolName)
      BoolName = &Idents.get("bool");
    return BoolName;
  }

  IdentifierInfo *getMakeIntegerSeqName() const {
    if (!MakeIntegerSeqName)
      MakeIntegerSeqName = &Idents.get("__make_integer_seq");
    return MakeIntegerSeqName;
  }

  IdentifierInfo *getTypePackElementName() const {
    if (!TypePackElementName)
      TypePackElementName = &Idents.get("__type_pack_element");
    return TypePackElementName;
  }

  /// Retrieve the Objective-C "instancetype" type, if already known;
  /// otherwise, returns a NULL type;
  QualType getObjCInstanceType() {
    return getTypeDeclType(getObjCInstanceTypeDecl());
  }

  /// Retrieve the typedef declaration corresponding to the Objective-C
  /// "instancetype" type.
  TypedefDecl *getObjCInstanceTypeDecl();

  /// Set the type for the C FILE type.
  void setFILEDecl(TypeDecl *FILEDecl) { this->FILEDecl = FILEDecl; }

  /// Retrieve the C FILE type.
  QualType getFILEType() const {
    if (FILEDecl)
      return getTypeDeclType(FILEDecl);
    return QualType();
  }

  /// Set the type for the C jmp_buf type.
  void setjmp_bufDecl(TypeDecl *jmp_bufDecl) {
    this->jmp_bufDecl = jmp_bufDecl;
  }

  /// Retrieve the C jmp_buf type.
  QualType getjmp_bufType() const {
    if (jmp_bufDecl)
      return getTypeDeclType(jmp_bufDecl);
    return QualType();
  }

  /// Set the type for the C sigjmp_buf type.
  void setsigjmp_bufDecl(TypeDecl *sigjmp_bufDecl) {
    this->sigjmp_bufDecl = sigjmp_bufDecl;
  }

  /// Retrieve the C sigjmp_buf type.
  QualType getsigjmp_bufType() const {
    if (sigjmp_bufDecl)
      return getTypeDeclType(sigjmp_bufDecl);
    return QualType();
  }

  /// Set the type for the C ucontext_t type.
  void setucontext_tDecl(TypeDecl *ucontext_tDecl) {
    this->ucontext_tDecl = ucontext_tDecl;
  }

  /// Retrieve the C ucontext_t type.
  QualType getucontext_tType() const {
    if (ucontext_tDecl)
      return getTypeDeclType(ucontext_tDecl);
    return QualType();
  }

  /// The result type of logical operations, '<', '>', '!=', etc.
  QualType getLogicalOperationType() const {
    return getLangOpts().CPlusPlus ? BoolTy : IntTy;
  }

  /// Emit the Objective-CC type encoding for the given type \p T into
  /// \p S.
  ///
  /// If \p Field is specified then record field names are also encoded.
  void getObjCEncodingForType(QualType T, std::string &S,
                              const FieldDecl *Field=nullptr,
                              QualType *NotEncodedT=nullptr) const;

  /// Emit the Objective-C property type encoding for the given
  /// type \p T into \p S.
  void getObjCEncodingForPropertyType(QualType T, std::string &S) const;

  void getLegacyIntegralTypeEncoding(QualType &t) const;

  /// Put the string version of the type qualifiers \p QT into \p S.
  void getObjCEncodingForTypeQualifier(Decl::ObjCDeclQualifier QT,
                                       std::string &S) const;

  /// Emit the encoded type for the function \p Decl into \p S.
  ///
  /// This is in the same format as Objective-C method encodings.
  ///
  /// \returns true if an error occurred (e.g., because one of the parameter
  /// types is incomplete), false otherwise.
  std::string getObjCEncodingForFunctionDecl(const FunctionDecl *Decl) const;

  /// Emit the encoded type for the method declaration \p Decl into
  /// \p S.
  std::string getObjCEncodingForMethodDecl(const ObjCMethodDecl *Decl,
                                           bool Extended = false) const;

  /// Return the encoded type for this block declaration.
  std::string getObjCEncodingForBlock(const BlockExpr *blockExpr) const;

  /// getObjCEncodingForPropertyDecl - Return the encoded type for
  /// this method declaration. If non-NULL, Container must be either
  /// an ObjCCategoryImplDecl or ObjCImplementationDecl; it should
  /// only be NULL when getting encodings for protocol properties.
  std::string getObjCEncodingForPropertyDecl(const ObjCPropertyDecl *PD,
                                             const Decl *Container) const;

  bool ProtocolCompatibleWithProtocol(ObjCProtocolDecl *lProto,
                                      ObjCProtocolDecl *rProto) const;

  ObjCPropertyImplDecl *getObjCPropertyImplDeclForPropertyDecl(
                                                  const ObjCPropertyDecl *PD,
                                                  const Decl *Container) const;

  /// Return the size of type \p T for Objective-C encoding purpose,
  /// in characters.
  CharUnits getObjCEncodingTypeSize(QualType T) const;

  /// Retrieve the typedef corresponding to the predefined \c id type
  /// in Objective-C.
  TypedefDecl *getObjCIdDecl() const;

  /// Represents the Objective-CC \c id type.
  ///
  /// This is set up lazily, by Sema.  \c id is always a (typedef for a)
  /// pointer type, a pointer to a struct.
  QualType getObjCIdType() const {
    return getTypeDeclType(getObjCIdDecl());
  }

  /// Retrieve the typedef corresponding to the predefined 'SEL' type
  /// in Objective-C.
  TypedefDecl *getObjCSelDecl() const;

  /// Retrieve the type that corresponds to the predefined Objective-C
  /// 'SEL' type.
  QualType getObjCSelType() const {
    return getTypeDeclType(getObjCSelDecl());
  }

  /// Retrieve the typedef declaration corresponding to the predefined
  /// Objective-C 'Class' type.
  TypedefDecl *getObjCClassDecl() const;

  /// Represents the Objective-C \c Class type.
  ///
  /// This is set up lazily, by Sema.  \c Class is always a (typedef for a)
  /// pointer type, a pointer to a struct.
  QualType getObjCClassType() const {
    return getTypeDeclType(getObjCClassDecl());
  }

  /// Retrieve the Objective-C class declaration corresponding to
  /// the predefined \c Protocol class.
  ObjCInterfaceDecl *getObjCProtocolDecl() const;

  /// Retrieve declaration of 'BOOL' typedef
  TypedefDecl *getBOOLDecl() const {
    return BOOLDecl;
  }

  /// Save declaration of 'BOOL' typedef
  void setBOOLDecl(TypedefDecl *TD) {
    BOOLDecl = TD;
  }

  /// type of 'BOOL' type.
  QualType getBOOLType() const {
    return getTypeDeclType(getBOOLDecl());
  }

  /// Retrieve the type of the Objective-C \c Protocol class.
  QualType getObjCProtoType() const {
    return getObjCInterfaceType(getObjCProtocolDecl());
  }

  /// Retrieve the C type declaration corresponding to the predefined
  /// \c __builtin_va_list type.
  TypedefDecl *getBuiltinVaListDecl() const;

  /// Retrieve the type of the \c __builtin_va_list type.
  QualType getBuiltinVaListType() const {
    return getTypeDeclType(getBuiltinVaListDecl());
  }

  /// Retrieve the C type declaration corresponding to the predefined
  /// \c __va_list_tag type used to help define the \c __builtin_va_list type
  /// for some targets.
  Decl *getVaListTagDecl() const;

  /// Retrieve the C type declaration corresponding to the predefined
  /// \c __builtin_ms_va_list type.
  TypedefDecl *getBuiltinMSVaListDecl() const;

  /// Retrieve the type of the \c __builtin_ms_va_list type.
  QualType getBuiltinMSVaListType() const {
    return getTypeDeclType(getBuiltinMSVaListDecl());
  }

  /// Return whether a declaration to a builtin is allowed to be
  /// overloaded/redeclared.
  bool canBuiltinBeRedeclared(const FunctionDecl *) const;

  /// Return a type with additional \c const, \c volatile, or
  /// \c restrict qualifiers.
  QualType getCVRQualifiedType(QualType T, unsigned CVR) const {
    return getQualifiedType(T, Qualifiers::fromCVRMask(CVR));
  }

  /// Un-split a SplitQualType.
  QualType getQualifiedType(SplitQualType split) const {
    return getQualifiedType(split.Ty, split.Quals);
  }

  /// Return a type with additional qualifiers.
  QualType getQualifiedType(QualType T, Qualifiers Qs) const {
    if (!Qs.hasNonFastQualifiers())
      return T.withFastQualifiers(Qs.getFastQualifiers());
    QualifierCollector Qc(Qs);
    const Type *Ptr = Qc.strip(T);
    return getExtQualType(Ptr, Qc);
  }

  /// Return a type with additional qualifiers.
  QualType getQualifiedType(const Type *T, Qualifiers Qs) const {
    if (!Qs.hasNonFastQualifiers())
      return QualType(T, Qs.getFastQualifiers());
    return getExtQualType(T, Qs);
  }

  /// Return a type with the given lifetime qualifier.
  ///
  /// \pre Neither type.ObjCLifetime() nor \p lifetime may be \c OCL_None.
  QualType getLifetimeQualifiedType(QualType type,
                                    Qualifiers::ObjCLifetime lifetime) {
    assert(type.getObjCLifetime() == Qualifiers::OCL_None);
    assert(lifetime != Qualifiers::OCL_None);

    Qualifiers qs;
    qs.addObjCLifetime(lifetime);
    return getQualifiedType(type, qs);
  }

  /// getUnqualifiedObjCPointerType - Returns version of
  /// Objective-C pointer type with lifetime qualifier removed.
  QualType getUnqualifiedObjCPointerType(QualType type) const {
    if (!type.getTypePtr()->isObjCObjectPointerType() ||
        !type.getQualifiers().hasObjCLifetime())
      return type;
    Qualifiers Qs = type.getQualifiers();
    Qs.removeObjCLifetime();
    return getQualifiedType(type.getUnqualifiedType(), Qs);
  }

  unsigned char getFixedPointScale(QualType Ty) const;
  unsigned char getFixedPointIBits(QualType Ty) const;
  FixedPointSemantics getFixedPointSemantics(QualType Ty) const;
  APFixedPoint getFixedPointMax(QualType Ty) const;
  APFixedPoint getFixedPointMin(QualType Ty) const;

  DeclarationNameInfo getNameForTemplate(TemplateName Name,
                                         SourceLocation NameLoc) const;

  TemplateName getOverloadedTemplateName(UnresolvedSetIterator Begin,
                                         UnresolvedSetIterator End) const;

  TemplateName getQualifiedTemplateName(NestedNameSpecifier *NNS,
                                        bool TemplateKeyword,
                                        TemplateDecl *Template) const;

  TemplateName getDependentTemplateName(NestedNameSpecifier *NNS,
                                        const IdentifierInfo *Name) const;
  TemplateName getDependentTemplateName(NestedNameSpecifier *NNS,
                                        OverloadedOperatorKind Operator) const;
  TemplateName getSubstTemplateTemplateParm(TemplateTemplateParmDecl *param,
                                            TemplateName replacement) const;
  TemplateName getSubstTemplateTemplateParmPack(TemplateTemplateParmDecl *Param,
                                        const TemplateArgument &ArgPack) const;

  enum GetBuiltinTypeError {
    /// No error
    GE_None,

    /// Missing a type from <stdio.h>
    GE_Missing_stdio,

    /// Missing a type from <setjmp.h>
    GE_Missing_setjmp,

    /// Missing a type from <ucontext.h>
    GE_Missing_ucontext
  };

  /// Return the type for the specified builtin.
  ///
  /// If \p IntegerConstantArgs is non-null, it is filled in with a bitmask of
  /// arguments to the builtin that are required to be integer constant
  /// expressions.
  QualType GetBuiltinType(unsigned ID, GetBuiltinTypeError &Error,
                          unsigned *IntegerConstantArgs = nullptr) const;

  /// Types and expressions required to build C++2a three-way comparisons
  /// using operator<=>, including the values return by builtin <=> operators.
  ComparisonCategories CompCategories;

private:
  CanQualType getFromTargetType(unsigned Type) const;
  TypeInfo getTypeInfoImpl(const Type *T) const;

  //===--------------------------------------------------------------------===//
  //                         Type Predicates.
  //===--------------------------------------------------------------------===//

public:
  /// Return one of the GCNone, Weak or Strong Objective-C garbage
  /// collection attributes.
  Qualifiers::GC getObjCGCAttrKind(QualType Ty) const;

  /// Return true if the given vector types are of the same unqualified
  /// type or if they are equivalent to the same GCC vector type.
  ///
  /// \note This ignores whether they are target-specific (AltiVec or Neon)
  /// types.
  bool areCompatibleVectorTypes(QualType FirstVec, QualType SecondVec);

  /// Return true if this is an \c NSObject object with its \c NSObject
  /// attribute set.
  static bool isObjCNSObjectType(QualType Ty) {
    return Ty->isObjCNSObjectType();
  }

  //===--------------------------------------------------------------------===//
  //                         Type Sizing and Analysis
  //===--------------------------------------------------------------------===//

  /// Return the APFloat 'semantics' for the specified scalar floating
  /// point type.
  const llvm::fltSemantics &getFloatTypeSemantics(QualType T) const;

  /// Get the size and alignment of the specified complete type in bits.
  TypeInfo getTypeInfo(const Type *T) const;
  TypeInfo getTypeInfo(QualType T) const { return getTypeInfo(T.getTypePtr()); }

  /// Get default simd alignment of the specified complete type in bits.
  unsigned getOpenMPDefaultSimdAlign(QualType T) const;

  /// Return the size of the specified (complete) type \p T, in bits.
  uint64_t getTypeSize(QualType T) const { return getTypeInfo(T).Width; }
  uint64_t getTypeSize(const Type *T) const { return getTypeInfo(T).Width; }

  /// Return the size of the character type, in bits.
  uint64_t getCharWidth() const {
    return getTypeSize(CharTy);
  }

  /// Convert a size in bits to a size in characters.
  CharUnits toCharUnitsFromBits(int64_t BitSize) const;

  /// Convert a size in characters to a size in bits.
  int64_t toBits(CharUnits CharSize) const;

  /// Return the size of the specified (complete) type \p T, in
  /// characters.
  CharUnits getTypeSizeInChars(QualType T) const;
  CharUnits getTypeSizeInChars(const Type *T) const;

  /// Return the ABI-specified alignment of a (complete) type \p T, in
  /// bits.
  unsigned getTypeAlign(QualType T) const { return getTypeInfo(T).Align; }
  unsigned getTypeAlign(const Type *T) const { return getTypeInfo(T).Align; }

  /// Return the ABI-specified natural alignment of a (complete) type \p T,
  /// before alignment adjustments, in bits.
  ///
  /// This alignment is curently used only by ARM and AArch64 when passing
  /// arguments of a composite type.
  unsigned getTypeUnadjustedAlign(QualType T) const {
    return getTypeUnadjustedAlign(T.getTypePtr());
  }
  unsigned getTypeUnadjustedAlign(const Type *T) const;

  /// Return the ABI-specified alignment of a type, in bits, or 0 if
  /// the type is incomplete and we cannot determine the alignment (for
  /// example, from alignment attributes).
  unsigned getTypeAlignIfKnown(QualType T) const;

  /// Return the ABI-specified alignment of a (complete) type \p T, in
  /// characters.
  CharUnits getTypeAlignInChars(QualType T) const;
  CharUnits getTypeAlignInChars(const Type *T) const;

  /// getTypeUnadjustedAlignInChars - Return the ABI-specified alignment of a type,
  /// in characters, before alignment adjustments. This method does not work on
  /// incomplete types.
  CharUnits getTypeUnadjustedAlignInChars(QualType T) const;
  CharUnits getTypeUnadjustedAlignInChars(const Type *T) const;

  // getTypeInfoDataSizeInChars - Return the size of a type, in chars. If the
  // type is a record, its data size is returned.
  std::pair<CharUnits, CharUnits> getTypeInfoDataSizeInChars(QualType T) const;

  std::pair<CharUnits, CharUnits> getTypeInfoInChars(const Type *T) const;
  std::pair<CharUnits, CharUnits> getTypeInfoInChars(QualType T) const;

  /// Determine if the alignment the type has was required using an
  /// alignment attribute.
  bool isAlignmentRequired(const Type *T) const;
  bool isAlignmentRequired(QualType T) const;

  /// Return the "preferred" alignment of the specified type \p T for
  /// the current target, in bits.
  ///
  /// This can be different than the ABI alignment in cases where it is
  /// beneficial for performance to overalign a data type.
  unsigned getPreferredTypeAlign(const Type *T) const;

  /// Return the default alignment for __attribute__((aligned)) on
  /// this target, to be used if no alignment value is specified.
  unsigned getTargetDefaultAlignForAttributeAligned() const;

  /// Return the alignment in bits that should be given to a
  /// global variable with type \p T.
  unsigned getAlignOfGlobalVar(QualType T) const;

  /// Return the alignment in characters that should be given to a
  /// global variable with type \p T.
  CharUnits getAlignOfGlobalVarInChars(QualType T) const;

  /// Return a conservative estimate of the alignment of the specified
  /// decl \p D.
  ///
  /// \pre \p D must not be a bitfield type, as bitfields do not have a valid
  /// alignment.
  ///
  /// If \p ForAlignof, references are treated like their underlying type
  /// and  large arrays don't get any special treatment. If not \p ForAlignof
  /// it computes the value expected by CodeGen: references are treated like
  /// pointers and large arrays get extra alignment.
  CharUnits getDeclAlign(const Decl *D, bool ForAlignof = false) const;

  /// Get or compute information about the layout of the specified
  /// record (struct/union/class) \p D, which indicates its size and field
  /// position information.
  const ASTRecordLayout &getASTRecordLayout(const RecordDecl *D) const;

  /// Get or compute information about the layout of the specified
  /// Objective-C interface.
  const ASTRecordLayout &getASTObjCInterfaceLayout(const ObjCInterfaceDecl *D)
    const;

  void DumpRecordLayout(const RecordDecl *RD, raw_ostream &OS,
                        bool Simple = false) const;

  /// Get or compute information about the layout of the specified
  /// Objective-C implementation.
  ///
  /// This may differ from the interface if synthesized ivars are present.
  const ASTRecordLayout &
  getASTObjCImplementationLayout(const ObjCImplementationDecl *D) const;

  /// Get our current best idea for the key function of the
  /// given record decl, or nullptr if there isn't one.
  ///
  /// The key function is, according to the Itanium C++ ABI section 5.2.3:
  ///   ...the first non-pure virtual function that is not inline at the
  ///   point of class definition.
  ///
  /// Other ABIs use the same idea.  However, the ARM C++ ABI ignores
  /// virtual functions that are defined 'inline', which means that
  /// the result of this computation can change.
  const CXXMethodDecl *getCurrentKeyFunction(const CXXRecordDecl *RD);

  /// Observe that the given method cannot be a key function.
  /// Checks the key-function cache for the method's class and clears it
  /// if matches the given declaration.
  ///
  /// This is used in ABIs where out-of-line definitions marked
  /// inline are not considered to be key functions.
  ///
  /// \param method should be the declaration from the class definition
  void setNonKeyFunction(const CXXMethodDecl *method);

  /// Loading virtual member pointers using the virtual inheritance model
  /// always results in an adjustment using the vbtable even if the index is
  /// zero.
  ///
  /// This is usually OK because the first slot in the vbtable points
  /// backwards to the top of the MDC.  However, the MDC might be reusing a
  /// vbptr from an nv-base.  In this case, the first slot in the vbtable
  /// points to the start of the nv-base which introduced the vbptr and *not*
  /// the MDC.  Modify the NonVirtualBaseAdjustment to account for this.
  CharUnits getOffsetOfBaseWithVBPtr(const CXXRecordDecl *RD) const;

  /// Get the offset of a FieldDecl or IndirectFieldDecl, in bits.
  uint64_t getFieldOffset(const ValueDecl *FD) const;

  /// Get the offset of an ObjCIvarDecl in bits.
  uint64_t lookupFieldBitOffset(const ObjCInterfaceDecl *OID,
                                const ObjCImplementationDecl *ID,
                                const ObjCIvarDecl *Ivar) const;

  bool isNearlyEmpty(const CXXRecordDecl *RD) const;

  VTableContextBase *getVTableContext();

  MangleContext *createMangleContext();

  void DeepCollectObjCIvars(const ObjCInterfaceDecl *OI, bool leafClass,
                            SmallVectorImpl<const ObjCIvarDecl*> &Ivars) const;

  unsigned CountNonClassIvars(const ObjCInterfaceDecl *OI) const;
  void CollectInheritedProtocols(const Decl *CDecl,
                          llvm::SmallPtrSet<ObjCProtocolDecl*, 8> &Protocols);

  /// Return true if the specified type has unique object representations
  /// according to (C++17 [meta.unary.prop]p9)
  bool hasUniqueObjectRepresentations(QualType Ty) const;

  //===--------------------------------------------------------------------===//
  //                            Type Operators
  //===--------------------------------------------------------------------===//

  /// Return the canonical (structural) type corresponding to the
  /// specified potentially non-canonical type \p T.
  ///
  /// The non-canonical version of a type may have many "decorated" versions of
  /// types.  Decorators can include typedefs, 'typeof' operators, etc. The
  /// returned type is guaranteed to be free of any of these, allowing two
  /// canonical types to be compared for exact equality with a simple pointer
  /// comparison.
  CanQualType getCanonicalType(QualType T) const {
    return CanQualType::CreateUnsafe(T.getCanonicalType());
  }

  const Type *getCanonicalType(const Type *T) const {
    return T->getCanonicalTypeInternal().getTypePtr();
  }

  /// Return the canonical parameter type corresponding to the specific
  /// potentially non-canonical one.
  ///
  /// Qualifiers are stripped off, functions are turned into function
  /// pointers, and arrays decay one level into pointers.
  CanQualType getCanonicalParamType(QualType T) const;

  /// Determine whether the given types \p T1 and \p T2 are equivalent.
  bool hasSameType(QualType T1, QualType T2) const {
    return getCanonicalType(T1) == getCanonicalType(T2);
  }
  bool hasSameType(const Type *T1, const Type *T2) const {
    return getCanonicalType(T1) == getCanonicalType(T2);
  }

  /// Return this type as a completely-unqualified array type,
  /// capturing the qualifiers in \p Quals.
  ///
  /// This will remove the minimal amount of sugaring from the types, similar
  /// to the behavior of QualType::getUnqualifiedType().
  ///
  /// \param T is the qualified type, which may be an ArrayType
  ///
  /// \param Quals will receive the full set of qualifiers that were
  /// applied to the array.
  ///
  /// \returns if this is an array type, the completely unqualified array type
  /// that corresponds to it. Otherwise, returns T.getUnqualifiedType().
  QualType getUnqualifiedArrayType(QualType T, Qualifiers &Quals);

  /// Determine whether the given types are equivalent after
  /// cvr-qualifiers have been removed.
  bool hasSameUnqualifiedType(QualType T1, QualType T2) const {
    return getCanonicalType(T1).getTypePtr() ==
           getCanonicalType(T2).getTypePtr();
  }

  bool hasSameNullabilityTypeQualifier(QualType SubT, QualType SuperT,
                                       bool IsParam) const {
    auto SubTnullability = SubT->getNullability(*this);
    auto SuperTnullability = SuperT->getNullability(*this);
    if (SubTnullability.hasValue() == SuperTnullability.hasValue()) {
      // Neither has nullability; return true
      if (!SubTnullability)
        return true;
      // Both have nullability qualifier.
      if (*SubTnullability == *SuperTnullability ||
          *SubTnullability == NullabilityKind::Unspecified ||
          *SuperTnullability == NullabilityKind::Unspecified)
        return true;

      if (IsParam) {
        // Ok for the superclass method parameter to be "nonnull" and the subclass
        // method parameter to be "nullable"
        return (*SuperTnullability == NullabilityKind::NonNull &&
                *SubTnullability == NullabilityKind::Nullable);
      }
      else {
        // For the return type, it's okay for the superclass method to specify
        // "nullable" and the subclass method specify "nonnull"
        return (*SuperTnullability == NullabilityKind::Nullable &&
                *SubTnullability == NullabilityKind::NonNull);
      }
    }
    return true;
  }

  bool ObjCMethodsAreEqual(const ObjCMethodDecl *MethodDecl,
                           const ObjCMethodDecl *MethodImp);

  bool UnwrapSimilarTypes(QualType &T1, QualType &T2);
  bool UnwrapSimilarArrayTypes(QualType &T1, QualType &T2);

  /// Determine if two types are similar, according to the C++ rules. That is,
  /// determine if they are the same other than qualifiers on the initial
  /// sequence of pointer / pointer-to-member / array (and in Clang, object
  /// pointer) types and their element types.
  ///
  /// Clang offers a number of qualifiers in addition to the C++ qualifiers;
  /// those qualifiers are also ignored in the 'similarity' check.
  bool hasSimilarType(QualType T1, QualType T2);

  /// Determine if two types are similar, ignoring only CVR qualifiers.
  bool hasCvrSimilarType(QualType T1, QualType T2);

  /// Retrieves the "canonical" nested name specifier for a
  /// given nested name specifier.
  ///
  /// The canonical nested name specifier is a nested name specifier
  /// that uniquely identifies a type or namespace within the type
  /// system. For example, given:
  ///
  /// \code
  /// namespace N {
  ///   struct S {
  ///     template<typename T> struct X { typename T* type; };
  ///   };
  /// }
  ///
  /// template<typename T> struct Y {
  ///   typename N::S::X<T>::type member;
  /// };
  /// \endcode
  ///
  /// Here, the nested-name-specifier for N::S::X<T>:: will be
  /// S::X<template-param-0-0>, since 'S' and 'X' are uniquely defined
  /// by declarations in the type system and the canonical type for
  /// the template type parameter 'T' is template-param-0-0.
  NestedNameSpecifier *
  getCanonicalNestedNameSpecifier(NestedNameSpecifier *NNS) const;

  /// Retrieves the default calling convention for the current target.
  CallingConv getDefaultCallingConvention(bool IsVariadic,
                                          bool IsCXXMethod) const;

  /// Retrieves the "canonical" template name that refers to a
  /// given template.
  ///
  /// The canonical template name is the simplest expression that can
  /// be used to refer to a given template. For most templates, this
  /// expression is just the template declaration itself. For example,
  /// the template std::vector can be referred to via a variety of
  /// names---std::vector, \::std::vector, vector (if vector is in
  /// scope), etc.---but all of these names map down to the same
  /// TemplateDecl, which is used to form the canonical template name.
  ///
  /// Dependent template names are more interesting. Here, the
  /// template name could be something like T::template apply or
  /// std::allocator<T>::template rebind, where the nested name
  /// specifier itself is dependent. In this case, the canonical
  /// template name uses the shortest form of the dependent
  /// nested-name-specifier, which itself contains all canonical
  /// types, values, and templates.
  TemplateName getCanonicalTemplateName(TemplateName Name) const;

  /// Determine whether the given template names refer to the same
  /// template.
  bool hasSameTemplateName(TemplateName X, TemplateName Y);

  /// Retrieve the "canonical" template argument.
  ///
  /// The canonical template argument is the simplest template argument
  /// (which may be a type, value, expression, or declaration) that
  /// expresses the value of the argument.
  TemplateArgument getCanonicalTemplateArgument(const TemplateArgument &Arg)
    const;

  /// Type Query functions.  If the type is an instance of the specified class,
  /// return the Type pointer for the underlying maximally pretty type.  This
  /// is a member of ASTContext because this may need to do some amount of
  /// canonicalization, e.g. to move type qualifiers into the element type.
  const ArrayType *getAsArrayType(QualType T) const;
  const ConstantArrayType *getAsConstantArrayType(QualType T) const {
    return dyn_cast_or_null<ConstantArrayType>(getAsArrayType(T));
  }
  const VariableArrayType *getAsVariableArrayType(QualType T) const {
    return dyn_cast_or_null<VariableArrayType>(getAsArrayType(T));
  }
  const IncompleteArrayType *getAsIncompleteArrayType(QualType T) const {
    return dyn_cast_or_null<IncompleteArrayType>(getAsArrayType(T));
  }
  const DependentSizedArrayType *getAsDependentSizedArrayType(QualType T)
    const {
    return dyn_cast_or_null<DependentSizedArrayType>(getAsArrayType(T));
  }

  /// Return the innermost element type of an array type.
  ///
  /// For example, will return "int" for int[m][n]
  QualType getBaseElementType(const ArrayType *VAT) const;

  /// Return the innermost element type of a type (which needn't
  /// actually be an array type).
  QualType getBaseElementType(QualType QT) const;

  /// Return number of constant array elements.
  uint64_t getConstantArrayElementCount(const ConstantArrayType *CA) const;

  /// Perform adjustment on the parameter type of a function.
  ///
  /// This routine adjusts the given parameter type @p T to the actual
  /// parameter type used by semantic analysis (C99 6.7.5.3p[7,8],
  /// C++ [dcl.fct]p3). The adjusted parameter type is returned.
  QualType getAdjustedParameterType(QualType T) const;

  /// Retrieve the parameter type as adjusted for use in the signature
  /// of a function, decaying array and function types and removing top-level
  /// cv-qualifiers.
  QualType getSignatureParameterType(QualType T) const;

  QualType getExceptionObjectType(QualType T) const;

  /// Return the properly qualified result of decaying the specified
  /// array type to a pointer.
  ///
  /// This operation is non-trivial when handling typedefs etc.  The canonical
  /// type of \p T must be an array type, this returns a pointer to a properly
  /// qualified element of the array.
  ///
  /// See C99 6.7.5.3p7 and C99 6.3.2.1p3.
  QualType getArrayDecayedType(QualType T) const;

  /// Return the type that \p PromotableType will promote to: C99
  /// 6.3.1.1p2, assuming that \p PromotableType is a promotable integer type.
  QualType getPromotedIntegerType(QualType PromotableType) const;

  /// Recurses in pointer/array types until it finds an Objective-C
  /// retainable type and returns its ownership.
  Qualifiers::ObjCLifetime getInnerObjCOwnership(QualType T) const;

  /// Whether this is a promotable bitfield reference according
  /// to C99 6.3.1.1p2, bullet 2 (and GCC extensions).
  ///
  /// \returns the type this bit-field will promote to, or NULL if no
  /// promotion occurs.
  QualType isPromotableBitField(Expr *E) const;

  /// Return the highest ranked integer type, see C99 6.3.1.8p1.
  ///
  /// If \p LHS > \p RHS, returns 1.  If \p LHS == \p RHS, returns 0.  If
  /// \p LHS < \p RHS, return -1.
  int getIntegerTypeOrder(QualType LHS, QualType RHS) const;

  /// Compare the rank of the two specified floating point types,
  /// ignoring the domain of the type (i.e. 'double' == '_Complex double').
  ///
  /// If \p LHS > \p RHS, returns 1.  If \p LHS == \p RHS, returns 0.  If
  /// \p LHS < \p RHS, return -1.
  int getFloatingTypeOrder(QualType LHS, QualType RHS) const;

  /// Return a real floating point or a complex type (based on
  /// \p typeDomain/\p typeSize).
  ///
  /// \param typeDomain a real floating point or complex type.
  /// \param typeSize a real floating point or complex type.
  QualType getFloatingTypeOfSizeWithinDomain(QualType typeSize,
                                             QualType typeDomain) const;

  unsigned getTargetAddressSpace(QualType T) const {
    return getTargetAddressSpace(T.getQualifiers());
  }

  unsigned getTargetAddressSpace(Qualifiers Q) const {
    return getTargetAddressSpace(Q.getAddressSpace());
  }

  unsigned getTargetAddressSpace(LangAS AS) const;

  LangAS getLangASForBuiltinAddressSpace(unsigned AS) const;

  /// Get target-dependent integer value for null pointer which is used for
  /// constant folding.
  uint64_t getTargetNullPointerValue(QualType QT) const;

  bool addressSpaceMapManglingFor(LangAS AS) const {
    return AddrSpaceMapMangling || isTargetAddressSpace(AS);
  }

private:
  // Helper for integer ordering
  unsigned getIntegerRank(const Type *T) const;

public:
  //===--------------------------------------------------------------------===//
  //                    Type Compatibility Predicates
  //===--------------------------------------------------------------------===//

  /// Compatibility predicates used to check assignment expressions.
  bool typesAreCompatible(QualType T1, QualType T2,
                          bool CompareUnqualified = false); // C99 6.2.7p1

  bool propertyTypesAreCompatible(QualType, QualType);
  bool typesAreBlockPointerCompatible(QualType, QualType);

  bool isObjCIdType(QualType T) const {
    return T == getObjCIdType();
  }

  bool isObjCClassType(QualType T) const {
    return T == getObjCClassType();
  }

  bool isObjCSelType(QualType T) const {
    return T == getObjCSelType();
  }

  bool ObjCQualifiedIdTypesAreCompatible(QualType LHS, QualType RHS,
                                         bool ForCompare);

  bool ObjCQualifiedClassTypesAreCompatible(QualType LHS, QualType RHS);

  // Check the safety of assignment from LHS to RHS
  bool canAssignObjCInterfaces(const ObjCObjectPointerType *LHSOPT,
                               const ObjCObjectPointerType *RHSOPT);
  bool canAssignObjCInterfaces(const ObjCObjectType *LHS,
                               const ObjCObjectType *RHS);
  bool canAssignObjCInterfacesInBlockPointer(
                                          const ObjCObjectPointerType *LHSOPT,
                                          const ObjCObjectPointerType *RHSOPT,
                                          bool BlockReturnType);
  bool areComparableObjCPointerTypes(QualType LHS, QualType RHS);
  QualType areCommonBaseCompatible(const ObjCObjectPointerType *LHSOPT,
                                   const ObjCObjectPointerType *RHSOPT);
  bool canBindObjCObjectType(QualType To, QualType From);

  // Functions for calculating composite types
  QualType mergeTypes(QualType, QualType, bool OfBlockPointer=false,
                      bool Unqualified = false, bool BlockReturnType = false);
  QualType mergeFunctionTypes(QualType, QualType, bool OfBlockPointer=false,
                              bool Unqualified = false);
  QualType mergeFunctionParameterTypes(QualType, QualType,
                                       bool OfBlockPointer = false,
                                       bool Unqualified = false);
  QualType mergeTransparentUnionType(QualType, QualType,
                                     bool OfBlockPointer=false,
                                     bool Unqualified = false);

  QualType mergeObjCGCQualifiers(QualType, QualType);

  /// This function merges the ExtParameterInfo lists of two functions. It
  /// returns true if the lists are compatible. The merged list is returned in
  /// NewParamInfos.
  ///
  /// \param FirstFnType The type of the first function.
  ///
  /// \param SecondFnType The type of the second function.
  ///
  /// \param CanUseFirst This flag is set to true if the first function's
  /// ExtParameterInfo list can be used as the composite list of
  /// ExtParameterInfo.
  ///
  /// \param CanUseSecond This flag is set to true if the second function's
  /// ExtParameterInfo list can be used as the composite list of
  /// ExtParameterInfo.
  ///
  /// \param NewParamInfos The composite list of ExtParameterInfo. The list is
  /// empty if none of the flags are set.
  ///
  bool mergeExtParameterInfo(
      const FunctionProtoType *FirstFnType,
      const FunctionProtoType *SecondFnType,
      bool &CanUseFirst, bool &CanUseSecond,
      SmallVectorImpl<FunctionProtoType::ExtParameterInfo> &NewParamInfos);

  void ResetObjCLayout(const ObjCContainerDecl *CD);

  //===--------------------------------------------------------------------===//
  //                    Integer Predicates
  //===--------------------------------------------------------------------===//

  // The width of an integer, as defined in C99 6.2.6.2. This is the number
  // of bits in an integer type excluding any padding bits.
  unsigned getIntWidth(QualType T) const;

  // Per C99 6.2.5p6, for every signed integer type, there is a corresponding
  // unsigned integer type.  This method takes a signed type, and returns the
  // corresponding unsigned integer type.
  // With the introduction of fixed point types in ISO N1169, this method also
  // accepts fixed point types and returns the corresponding unsigned type for
  // a given fixed point type.
  QualType getCorrespondingUnsignedType(QualType T) const;

  // Per ISO N1169, this method accepts fixed point types and returns the
  // corresponding saturated type for a given fixed point type.
  QualType getCorrespondingSaturatedType(QualType Ty) const;

  //===--------------------------------------------------------------------===//
  //                    Integer Values
  //===--------------------------------------------------------------------===//

  /// Make an APSInt of the appropriate width and signedness for the
  /// given \p Value and integer \p Type.
  llvm::APSInt MakeIntValue(uint64_t Value, QualType Type) const {
    // If Type is a signed integer type larger than 64 bits, we need to be sure
    // to sign extend Res appropriately.
    llvm::APSInt Res(64, !Type->isSignedIntegerOrEnumerationType());
    Res = Value;
    unsigned Width = getIntWidth(Type);
    if (Width != Res.getBitWidth())
      return Res.extOrTrunc(Width);
    return Res;
  }

  bool isSentinelNullExpr(const Expr *E);

  /// Get the implementation of the ObjCInterfaceDecl \p D, or nullptr if
  /// none exists.
  ObjCImplementationDecl *getObjCImplementation(ObjCInterfaceDecl *D);

  /// Get the implementation of the ObjCCategoryDecl \p D, or nullptr if
  /// none exists.
  ObjCCategoryImplDecl *getObjCImplementation(ObjCCategoryDecl *D);

  /// Return true if there is at least one \@implementation in the TU.
  bool AnyObjCImplementation() {
    return !ObjCImpls.empty();
  }

  /// Set the implementation of ObjCInterfaceDecl.
  void setObjCImplementation(ObjCInterfaceDecl *IFaceD,
                             ObjCImplementationDecl *ImplD);

  /// Set the implementation of ObjCCategoryDecl.
  void setObjCImplementation(ObjCCategoryDecl *CatD,
                             ObjCCategoryImplDecl *ImplD);

  /// Get the duplicate declaration of a ObjCMethod in the same
  /// interface, or null if none exists.
  const ObjCMethodDecl *
  getObjCMethodRedeclaration(const ObjCMethodDecl *MD) const;

  void setObjCMethodRedeclaration(const ObjCMethodDecl *MD,
                                  const ObjCMethodDecl *Redecl);

  /// Returns the Objective-C interface that \p ND belongs to if it is
  /// an Objective-C method/property/ivar etc. that is part of an interface,
  /// otherwise returns null.
  const ObjCInterfaceDecl *getObjContainingInterface(const NamedDecl *ND) const;

  /// Set the copy inialization expression of a block var decl. \p CanThrow
  /// indicates whether the copy expression can throw or not.
  void setBlockVarCopyInit(const VarDecl* VD, Expr *CopyExpr, bool CanThrow);

  /// Get the copy initialization expression of the VarDecl \p VD, or
  /// nullptr if none exists.
  BlockVarCopyInit getBlockVarCopyInit(const VarDecl* VD) const;

  /// Allocate an uninitialized TypeSourceInfo.
  ///
  /// The caller should initialize the memory held by TypeSourceInfo using
  /// the TypeLoc wrappers.
  ///
  /// \param T the type that will be the basis for type source info. This type
  /// should refer to how the declarator was written in source code, not to
  /// what type semantic analysis resolved the declarator to.
  ///
  /// \param Size the size of the type info to create, or 0 if the size
  /// should be calculated based on the type.
  TypeSourceInfo *CreateTypeSourceInfo(QualType T, unsigned Size = 0) const;

  /// Allocate a TypeSourceInfo where all locations have been
  /// initialized to a given location, which defaults to the empty
  /// location.
  TypeSourceInfo *
  getTrivialTypeSourceInfo(QualType T,
                           SourceLocation Loc = SourceLocation()) const;

  /// Add a deallocation callback that will be invoked when the
  /// ASTContext is destroyed.
  ///
  /// \param Callback A callback function that will be invoked on destruction.
  ///
  /// \param Data Pointer data that will be provided to the callback function
  /// when it is called.
  void AddDeallocation(void (*Callback)(void*), void *Data);

  /// If T isn't trivially destructible, calls AddDeallocation to register it
  /// for destruction.
  template <typename T>
  void addDestruction(T *Ptr) {
    if (!std::is_trivially_destructible<T>::value) {
      auto DestroyPtr = [](void *V) { static_cast<T *>(V)->~T(); };
      AddDeallocation(DestroyPtr, Ptr);
    }
  }

  GVALinkage GetGVALinkageForFunction(const FunctionDecl *FD) const;
  GVALinkage GetGVALinkageForVariable(const VarDecl *VD);

  /// Determines if the decl can be CodeGen'ed or deserialized from PCH
  /// lazily, only when used; this is only relevant for function or file scoped
  /// var definitions.
  ///
  /// \returns true if the function/var must be CodeGen'ed/deserialized even if
  /// it is not used.
  bool DeclMustBeEmitted(const Decl *D);

  /// Visits all versions of a multiversioned function with the passed
  /// predicate.
  void forEachMultiversionedFunctionVersion(
      const FunctionDecl *FD,
      llvm::function_ref<void(FunctionDecl *)> Pred) const;

  const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *RD);

  void addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                            CXXConstructorDecl *CD);

  void addTypedefNameForUnnamedTagDecl(TagDecl *TD, TypedefNameDecl *TND);

  TypedefNameDecl *getTypedefNameForUnnamedTagDecl(const TagDecl *TD);

  void addDeclaratorForUnnamedTagDecl(TagDecl *TD, DeclaratorDecl *DD);

  DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD);

  void setManglingNumber(const NamedDecl *ND, unsigned Number);
  unsigned getManglingNumber(const NamedDecl *ND) const;

  void setStaticLocalNumber(const VarDecl *VD, unsigned Number);
  unsigned getStaticLocalNumber(const VarDecl *VD) const;

  /// Retrieve the context for computing mangling numbers in the given
  /// DeclContext.
  MangleNumberingContext &getManglingNumberContext(const DeclContext *DC);

  std::unique_ptr<MangleNumberingContext> createMangleNumberingContext() const;

  /// Used by ParmVarDecl to store on the side the
  /// index of the parameter when it exceeds the size of the normal bitfield.
  void setParameterIndex(const ParmVarDecl *D, unsigned index);

  /// Used by ParmVarDecl to retrieve on the side the
  /// index of the parameter when it exceeds the size of the normal bitfield.
  unsigned getParameterIndex(const ParmVarDecl *D) const;

  /// Get the storage for the constant value of a materialized temporary
  /// of static storage duration.
  APValue *getMaterializedTemporaryValue(const MaterializeTemporaryExpr *E,
                                         bool MayCreate);

  //===--------------------------------------------------------------------===//
  //                    Statistics
  //===--------------------------------------------------------------------===//

  /// The number of implicitly-declared default constructors.
  static unsigned NumImplicitDefaultConstructors;

  /// The number of implicitly-declared default constructors for
  /// which declarations were built.
  static unsigned NumImplicitDefaultConstructorsDeclared;

  /// The number of implicitly-declared copy constructors.
  static unsigned NumImplicitCopyConstructors;

  /// The number of implicitly-declared copy constructors for
  /// which declarations were built.
  static unsigned NumImplicitCopyConstructorsDeclared;

  /// The number of implicitly-declared move constructors.
  static unsigned NumImplicitMoveConstructors;

  /// The number of implicitly-declared move constructors for
  /// which declarations were built.
  static unsigned NumImplicitMoveConstructorsDeclared;

  /// The number of implicitly-declared copy assignment operators.
  static unsigned NumImplicitCopyAssignmentOperators;

  /// The number of implicitly-declared copy assignment operators for
  /// which declarations were built.
  static unsigned NumImplicitCopyAssignmentOperatorsDeclared;

  /// The number of implicitly-declared move assignment operators.
  static unsigned NumImplicitMoveAssignmentOperators;

  /// The number of implicitly-declared move assignment operators for
  /// which declarations were built.
  static unsigned NumImplicitMoveAssignmentOperatorsDeclared;

  /// The number of implicitly-declared destructors.
  static unsigned NumImplicitDestructors;

  /// The number of implicitly-declared destructors for which
  /// declarations were built.
  static unsigned NumImplicitDestructorsDeclared;

public:
  /// Initialize built-in types.
  ///
  /// This routine may only be invoked once for a given ASTContext object.
  /// It is normally invoked after ASTContext construction.
  ///
  /// \param Target The target
  void InitBuiltinTypes(const TargetInfo &Target,
                        const TargetInfo *AuxTarget = nullptr);

private:
  void InitBuiltinType(CanQualType &R, BuiltinType::Kind K);

  // Return the Objective-C type encoding for a given type.
  void getObjCEncodingForTypeImpl(QualType t, std::string &S,
                                  bool ExpandPointedToStructures,
                                  bool ExpandStructures,
                                  const FieldDecl *Field,
                                  bool OutermostType = false,
                                  bool EncodingProperty = false,
                                  bool StructField = false,
                                  bool EncodeBlockParameters = false,
                                  bool EncodeClassNames = false,
                                  bool EncodePointerToObjCTypedef = false,
                                  QualType *NotEncodedT=nullptr) const;

  // Adds the encoding of the structure's members.
  void getObjCEncodingForStructureImpl(RecordDecl *RD, std::string &S,
                                       const FieldDecl *Field,
                                       bool includeVBases = true,
                                       QualType *NotEncodedT=nullptr) const;

public:
  // Adds the encoding of a method parameter or return type.
  void getObjCEncodingForMethodParameter(Decl::ObjCDeclQualifier QT,
                                         QualType T, std::string& S,
                                         bool Extended) const;

  /// Returns true if this is an inline-initialized static data member
  /// which is treated as a definition for MSVC compatibility.
  bool isMSStaticDataMemberInlineDefinition(const VarDecl *VD) const;

  enum class InlineVariableDefinitionKind {
    /// Not an inline variable.
    None,

    /// Weak definition of inline variable.
    Weak,

    /// Weak for now, might become strong later in this TU.
    WeakUnknown,

    /// Strong definition.
    Strong
  };

  /// Determine whether a definition of this inline variable should
  /// be treated as a weak or strong definition. For compatibility with
  /// C++14 and before, for a constexpr static data member, if there is an
  /// out-of-line declaration of the member, we may promote it from weak to
  /// strong.
  InlineVariableDefinitionKind
  getInlineVariableDefinitionKind(const VarDecl *VD) const;

private:
  friend class DeclarationNameTable;
  friend class DeclContext;

  const ASTRecordLayout &
  getObjCLayout(const ObjCInterfaceDecl *D,
                const ObjCImplementationDecl *Impl) const;

  /// A set of deallocations that should be performed when the
  /// ASTContext is destroyed.
  // FIXME: We really should have a better mechanism in the ASTContext to
  // manage running destructors for types which do variable sized allocation
  // within the AST. In some places we thread the AST bump pointer allocator
  // into the datastructures which avoids this mess during deallocation but is
  // wasteful of memory, and here we require a lot of error prone book keeping
  // in order to track and run destructors while we're tearing things down.
  using DeallocationFunctionsAndArguments =
      llvm::SmallVector<std::pair<void (*)(void *), void *>, 16>;
  DeallocationFunctionsAndArguments Deallocations;

  // FIXME: This currently contains the set of StoredDeclMaps used
  // by DeclContext objects.  This probably should not be in ASTContext,
  // but we include it here so that ASTContext can quickly deallocate them.
  llvm::PointerIntPair<StoredDeclsMap *, 1> LastSDM;

  std::vector<Decl *> TraversalScope;
  class ParentMap;
  std::unique_ptr<ParentMap> Parents;

  std::unique_ptr<VTableContextBase> VTContext;

  void ReleaseDeclContextMaps();

public:
  enum PragmaSectionFlag : unsigned {
    PSF_None = 0,
    PSF_Read = 0x1,
    PSF_Write = 0x2,
    PSF_Execute = 0x4,
    PSF_Implicit = 0x8,
    PSF_Invalid = 0x80000000U,
  };

  struct SectionInfo {
    DeclaratorDecl *Decl;
    SourceLocation PragmaSectionLocation;
    int SectionFlags;

    SectionInfo() = default;
    SectionInfo(DeclaratorDecl *Decl,
                SourceLocation PragmaSectionLocation,
                int SectionFlags)
        : Decl(Decl), PragmaSectionLocation(PragmaSectionLocation),
          SectionFlags(SectionFlags) {}
  };

  llvm::StringMap<SectionInfo> SectionInfos;
};

/// Utility function for constructing a nullary selector.
inline Selector GetNullarySelector(StringRef name, ASTContext &Ctx) {
  IdentifierInfo* II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(0, &II);
}

/// Utility function for constructing an unary selector.
inline Selector GetUnarySelector(StringRef name, ASTContext &Ctx) {
  IdentifierInfo* II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(1, &II);
}

} // namespace clang

// operator new and delete aren't allowed inside namespaces.

/// Placement new for using the ASTContext's allocator.
///
/// This placement form of operator new uses the ASTContext's allocator for
/// obtaining memory.
///
/// IMPORTANT: These are also declared in clang/AST/ASTContextAllocate.h!
/// Any changes here need to also be made there.
///
/// We intentionally avoid using a nothrow specification here so that the calls
/// to this operator will not perform a null check on the result -- the
/// underlying allocator never returns null pointers.
///
/// Usage looks like this (assuming there's an ASTContext 'Context' in scope):
/// @code
/// // Default alignment (8)
/// IntegerLiteral *Ex = new (Context) IntegerLiteral(arguments);
/// // Specific alignment
/// IntegerLiteral *Ex2 = new (Context, 4) IntegerLiteral(arguments);
/// @endcode
/// Memory allocated through this placement new operator does not need to be
/// explicitly freed, as ASTContext will free all of this memory when it gets
/// destroyed. Please note that you cannot use delete on the pointer.
///
/// @param Bytes The number of bytes to allocate. Calculated by the compiler.
/// @param C The ASTContext that provides the allocator.
/// @param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// @return The allocated memory. Could be nullptr.
inline void *operator new(size_t Bytes, const clang::ASTContext &C,
                          size_t Alignment /* = 8 */) {
  return C.Allocate(Bytes, Alignment);
}

/// Placement delete companion to the new above.
///
/// This operator is just a companion to the new above. There is no way of
/// invoking it directly; see the new operator for more details. This operator
/// is called implicitly by the compiler if a placement new expression using
/// the ASTContext throws in the object constructor.
inline void operator delete(void *Ptr, const clang::ASTContext &C, size_t) {
  C.Deallocate(Ptr);
}

/// This placement form of operator new[] uses the ASTContext's allocator for
/// obtaining memory.
///
/// We intentionally avoid using a nothrow specification here so that the calls
/// to this operator will not perform a null check on the result -- the
/// underlying allocator never returns null pointers.
///
/// Usage looks like this (assuming there's an ASTContext 'Context' in scope):
/// @code
/// // Default alignment (8)
/// char *data = new (Context) char[10];
/// // Specific alignment
/// char *data = new (Context, 4) char[10];
/// @endcode
/// Memory allocated through this placement new[] operator does not need to be
/// explicitly freed, as ASTContext will free all of this memory when it gets
/// destroyed. Please note that you cannot use delete on the pointer.
///
/// @param Bytes The number of bytes to allocate. Calculated by the compiler.
/// @param C The ASTContext that provides the allocator.
/// @param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// @return The allocated memory. Could be nullptr.
inline void *operator new[](size_t Bytes, const clang::ASTContext& C,
                            size_t Alignment /* = 8 */) {
  return C.Allocate(Bytes, Alignment);
}

/// Placement delete[] companion to the new[] above.
///
/// This operator is just a companion to the new[] above. There is no way of
/// invoking it directly; see the new[] operator for more details. This operator
/// is called implicitly by the compiler if a placement new[] expression using
/// the ASTContext throws in the object constructor.
inline void operator delete[](void *Ptr, const clang::ASTContext &C, size_t) {
  C.Deallocate(Ptr);
}

/// Create the representation of a LazyGenerationalUpdatePtr.
template <typename Owner, typename T,
          void (clang::ExternalASTSource::*Update)(Owner)>
typename clang::LazyGenerationalUpdatePtr<Owner, T, Update>::ValueType
    clang::LazyGenerationalUpdatePtr<Owner, T, Update>::makeValue(
        const clang::ASTContext &Ctx, T Value) {
  // Note, this is implemented here so that ExternalASTSource.h doesn't need to
  // include ASTContext.h. We explicitly instantiate it for all relevant types
  // in ASTContext.cpp.
  if (auto *Source = Ctx.getExternalSource())
    return new (Ctx) LazyData(Source, Value);
  return Value;
}

#endif // LLVM_CLANG_AST_ASTCONTEXT_H
