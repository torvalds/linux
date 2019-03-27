//===------ Core.h -- Core ORC APIs (Layer, JITDylib, etc.) -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains core ORC APIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_CORE_H
#define LLVM_EXECUTIONENGINE_ORC_CORE_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"

#include <memory>
#include <vector>

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {

// Forward declare some classes.
class AsynchronousSymbolQuery;
class ExecutionSession;
class MaterializationUnit;
class MaterializationResponsibility;
class JITDylib;

/// VModuleKey provides a unique identifier (allocated and managed by
/// ExecutionSessions) for a module added to the JIT.
using VModuleKey = uint64_t;

/// A set of symbol names (represented by SymbolStringPtrs for
//         efficiency).
using SymbolNameSet = DenseSet<SymbolStringPtr>;

/// A map from symbol names (as SymbolStringPtrs) to JITSymbols
///        (address/flags pairs).
using SymbolMap = DenseMap<SymbolStringPtr, JITEvaluatedSymbol>;

/// A map from symbol names (as SymbolStringPtrs) to JITSymbolFlags.
using SymbolFlagsMap = DenseMap<SymbolStringPtr, JITSymbolFlags>;

/// A base class for materialization failures that allows the failing
///        symbols to be obtained for logging.
using SymbolDependenceMap = DenseMap<JITDylib *, SymbolNameSet>;

/// A list of (JITDylib*, bool) pairs.
using JITDylibSearchList = std::vector<std::pair<JITDylib *, bool>>;

/// Render a SymbolStringPtr.
raw_ostream &operator<<(raw_ostream &OS, const SymbolStringPtr &Sym);

/// Render a SymbolNameSet.
raw_ostream &operator<<(raw_ostream &OS, const SymbolNameSet &Symbols);

/// Render a SymbolFlagsMap entry.
raw_ostream &operator<<(raw_ostream &OS, const SymbolFlagsMap::value_type &KV);

/// Render a SymbolMap entry.
raw_ostream &operator<<(raw_ostream &OS, const SymbolMap::value_type &KV);

/// Render a SymbolFlagsMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolFlagsMap &SymbolFlags);

/// Render a SymbolMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolMap &Symbols);

/// Render a SymbolDependenceMap entry.
raw_ostream &operator<<(raw_ostream &OS,
                        const SymbolDependenceMap::value_type &KV);

/// Render a SymbolDependendeMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolDependenceMap &Deps);

/// Render a MaterializationUnit.
raw_ostream &operator<<(raw_ostream &OS, const MaterializationUnit &MU);

/// Render a JITDylibSearchList.
raw_ostream &operator<<(raw_ostream &OS, const JITDylibSearchList &JDs);

/// Callback to notify client that symbols have been resolved.
using SymbolsResolvedCallback = std::function<void(Expected<SymbolMap>)>;

/// Callback to notify client that symbols are ready for execution.
using SymbolsReadyCallback = std::function<void(Error)>;

/// Callback to register the dependencies for a given query.
using RegisterDependenciesFunction =
    std::function<void(const SymbolDependenceMap &)>;

/// This can be used as the value for a RegisterDependenciesFunction if there
/// are no dependants to register with.
extern RegisterDependenciesFunction NoDependenciesToRegister;

/// Used to notify a JITDylib that the given set of symbols failed to
/// materialize.
class FailedToMaterialize : public ErrorInfo<FailedToMaterialize> {
public:
  static char ID;

  FailedToMaterialize(SymbolNameSet Symbols);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  const SymbolNameSet &getSymbols() const { return Symbols; }

private:
  SymbolNameSet Symbols;
};

/// Used to notify clients when symbols can not be found during a lookup.
class SymbolsNotFound : public ErrorInfo<SymbolsNotFound> {
public:
  static char ID;

  SymbolsNotFound(SymbolNameSet Symbols);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  const SymbolNameSet &getSymbols() const { return Symbols; }

private:
  SymbolNameSet Symbols;
};

/// Used to notify clients that a set of symbols could not be removed.
class SymbolsCouldNotBeRemoved : public ErrorInfo<SymbolsCouldNotBeRemoved> {
public:
  static char ID;

  SymbolsCouldNotBeRemoved(SymbolNameSet Symbols);
  std::error_code convertToErrorCode() const override;
  void log(raw_ostream &OS) const override;
  const SymbolNameSet &getSymbols() const { return Symbols; }

private:
  SymbolNameSet Symbols;
};

/// Tracks responsibility for materialization, and mediates interactions between
/// MaterializationUnits and JDs.
///
/// An instance of this class is passed to MaterializationUnits when their
/// materialize method is called. It allows MaterializationUnits to resolve and
/// emit symbols, or abandon materialization by notifying any unmaterialized
/// symbols of an error.
class MaterializationResponsibility {
  friend class MaterializationUnit;
public:
  MaterializationResponsibility(MaterializationResponsibility &&) = default;
  MaterializationResponsibility &
  operator=(MaterializationResponsibility &&) = delete;

  /// Destruct a MaterializationResponsibility instance. In debug mode
  ///        this asserts that all symbols being tracked have been either
  ///        emitted or notified of an error.
  ~MaterializationResponsibility();

  /// Returns the target JITDylib that these symbols are being materialized
  ///        into.
  JITDylib &getTargetJITDylib() const { return JD; }

  /// Returns the VModuleKey for this instance.
  VModuleKey getVModuleKey() const { return K; }

  /// Returns the symbol flags map for this responsibility instance.
  /// Note: The returned flags may have transient flags (Lazy, Materializing)
  /// set. These should be stripped with JITSymbolFlags::stripTransientFlags
  /// before using.
  const SymbolFlagsMap &getSymbols() { return SymbolFlags; }

  /// Returns the names of any symbols covered by this
  /// MaterializationResponsibility object that have queries pending. This
  /// information can be used to return responsibility for unrequested symbols
  /// back to the JITDylib via the delegate method.
  SymbolNameSet getRequestedSymbols() const;

  /// Notifies the target JITDylib that the given symbols have been resolved.
  /// This will update the given symbols' addresses in the JITDylib, and notify
  /// any pending queries on the given symbols of their resolution. The given
  /// symbols must be ones covered by this MaterializationResponsibility
  /// instance. Individual calls to this method may resolve a subset of the
  /// symbols, but all symbols must have been resolved prior to calling emit.
  void resolve(const SymbolMap &Symbols);

  /// Notifies the target JITDylib (and any pending queries on that JITDylib)
  /// that all symbols covered by this MaterializationResponsibility instance
  /// have been emitted.
  void emit();

  /// Adds new symbols to the JITDylib and this responsibility instance.
  ///        JITDylib entries start out in the materializing state.
  ///
  ///   This method can be used by materialization units that want to add
  /// additional symbols at materialization time (e.g. stubs, compile
  /// callbacks, metadata).
  Error defineMaterializing(const SymbolFlagsMap &SymbolFlags);

  /// Notify all not-yet-emitted covered by this MaterializationResponsibility
  /// instance that an error has occurred.
  /// This will remove all symbols covered by this MaterializationResponsibilty
  /// from the target JITDylib, and send an error to any queries waiting on
  /// these symbols.
  void failMaterialization();

  /// Transfers responsibility to the given MaterializationUnit for all
  /// symbols defined by that MaterializationUnit. This allows
  /// materializers to break up work based on run-time information (e.g.
  /// by introspecting which symbols have actually been looked up and
  /// materializing only those).
  void replace(std::unique_ptr<MaterializationUnit> MU);

  /// Delegates responsibility for the given symbols to the returned
  /// materialization responsibility. Useful for breaking up work between
  /// threads, or different kinds of materialization processes.
  MaterializationResponsibility delegate(const SymbolNameSet &Symbols,
                                         VModuleKey NewKey = VModuleKey());

  void addDependencies(const SymbolStringPtr &Name,
                       const SymbolDependenceMap &Dependencies);

  /// Add dependencies that apply to all symbols covered by this instance.
  void addDependenciesForAll(const SymbolDependenceMap &Dependencies);

private:
  /// Create a MaterializationResponsibility for the given JITDylib and
  ///        initial symbols.
  MaterializationResponsibility(JITDylib &JD, SymbolFlagsMap SymbolFlags,
                                VModuleKey K);

  JITDylib &JD;
  SymbolFlagsMap SymbolFlags;
  VModuleKey K;
};

/// A MaterializationUnit represents a set of symbol definitions that can
///        be materialized as a group, or individually discarded (when
///        overriding definitions are encountered).
///
/// MaterializationUnits are used when providing lazy definitions of symbols to
/// JITDylibs. The JITDylib will call materialize when the address of a symbol
/// is requested via the lookup method. The JITDylib will call discard if a
/// stronger definition is added or already present.
class MaterializationUnit {
public:
  MaterializationUnit(SymbolFlagsMap InitalSymbolFlags, VModuleKey K)
      : SymbolFlags(std::move(InitalSymbolFlags)), K(std::move(K)) {}

  virtual ~MaterializationUnit() {}

  /// Return the name of this materialization unit. Useful for debugging
  /// output.
  virtual StringRef getName() const = 0;

  /// Return the set of symbols that this source provides.
  const SymbolFlagsMap &getSymbols() const { return SymbolFlags; }

  /// Called by materialization dispatchers (see
  /// ExecutionSession::DispatchMaterializationFunction) to trigger
  /// materialization of this MaterializationUnit.
  void doMaterialize(JITDylib &JD) {
    materialize(MaterializationResponsibility(JD, std::move(SymbolFlags),
                                              std::move(K)));
  }

  /// Called by JITDylibs to notify MaterializationUnits that the given symbol
  /// has been overridden.
  void doDiscard(const JITDylib &JD, const SymbolStringPtr &Name) {
    SymbolFlags.erase(Name);
    discard(JD, std::move(Name));
  }

protected:
  SymbolFlagsMap SymbolFlags;
  VModuleKey K;

private:
  virtual void anchor();

  /// Implementations of this method should materialize all symbols
  ///        in the materialzation unit, except for those that have been
  ///        previously discarded.
  virtual void materialize(MaterializationResponsibility R) = 0;

  /// Implementations of this method should discard the given symbol
  ///        from the source (e.g. if the source is an LLVM IR Module and the
  ///        symbol is a function, delete the function body or mark it available
  ///        externally).
  virtual void discard(const JITDylib &JD, const SymbolStringPtr &Name) = 0;
};

using MaterializationUnitList =
    std::vector<std::unique_ptr<MaterializationUnit>>;

/// A MaterializationUnit implementation for pre-existing absolute symbols.
///
/// All symbols will be resolved and marked ready as soon as the unit is
/// materialized.
class AbsoluteSymbolsMaterializationUnit : public MaterializationUnit {
public:
  AbsoluteSymbolsMaterializationUnit(SymbolMap Symbols, VModuleKey K);

  StringRef getName() const override;

private:
  void materialize(MaterializationResponsibility R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static SymbolFlagsMap extractFlags(const SymbolMap &Symbols);

  SymbolMap Symbols;
};

/// Create an AbsoluteSymbolsMaterializationUnit with the given symbols.
/// Useful for inserting absolute symbols into a JITDylib. E.g.:
/// \code{.cpp}
///   JITDylib &JD = ...;
///   SymbolStringPtr Foo = ...;
///   JITEvaluatedSymbol FooSym = ...;
///   if (auto Err = JD.define(absoluteSymbols({{Foo, FooSym}})))
///     return Err;
/// \endcode
///
inline std::unique_ptr<AbsoluteSymbolsMaterializationUnit>
absoluteSymbols(SymbolMap Symbols, VModuleKey K = VModuleKey()) {
  return llvm::make_unique<AbsoluteSymbolsMaterializationUnit>(
      std::move(Symbols), std::move(K));
}

struct SymbolAliasMapEntry {
  SymbolAliasMapEntry() = default;
  SymbolAliasMapEntry(SymbolStringPtr Aliasee, JITSymbolFlags AliasFlags)
      : Aliasee(std::move(Aliasee)), AliasFlags(AliasFlags) {}

  SymbolStringPtr Aliasee;
  JITSymbolFlags AliasFlags;
};

/// A map of Symbols to (Symbol, Flags) pairs.
using SymbolAliasMap = DenseMap<SymbolStringPtr, SymbolAliasMapEntry>;

/// A materialization unit for symbol aliases. Allows existing symbols to be
/// aliased with alternate flags.
class ReExportsMaterializationUnit : public MaterializationUnit {
public:
  /// SourceJD is allowed to be nullptr, in which case the source JITDylib is
  /// taken to be whatever JITDylib these definitions are materialized in (and
  /// MatchNonExported has no effect). This is useful for defining aliases
  /// within a JITDylib.
  ///
  /// Note: Care must be taken that no sets of aliases form a cycle, as such
  ///       a cycle will result in a deadlock when any symbol in the cycle is
  ///       resolved.
  ReExportsMaterializationUnit(JITDylib *SourceJD, bool MatchNonExported,
                               SymbolAliasMap Aliases, VModuleKey K);

  StringRef getName() const override;

private:
  void materialize(MaterializationResponsibility R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static SymbolFlagsMap extractFlags(const SymbolAliasMap &Aliases);

  JITDylib *SourceJD = nullptr;
  bool MatchNonExported = false;
  SymbolAliasMap Aliases;
};

/// Create a ReExportsMaterializationUnit with the given aliases.
/// Useful for defining symbol aliases.: E.g., given a JITDylib JD containing
/// symbols "foo" and "bar", we can define aliases "baz" (for "foo") and "qux"
/// (for "bar") with: \code{.cpp}
///   SymbolStringPtr Baz = ...;
///   SymbolStringPtr Qux = ...;
///   if (auto Err = JD.define(symbolAliases({
///       {Baz, { Foo, JITSymbolFlags::Exported }},
///       {Qux, { Bar, JITSymbolFlags::Weak }}}))
///     return Err;
/// \endcode
inline std::unique_ptr<ReExportsMaterializationUnit>
symbolAliases(SymbolAliasMap Aliases, VModuleKey K = VModuleKey()) {
  return llvm::make_unique<ReExportsMaterializationUnit>(
      nullptr, true, std::move(Aliases), std::move(K));
}

/// Create a materialization unit for re-exporting symbols from another JITDylib
/// with alternative names/flags.
/// If MatchNonExported is true then non-exported symbols from SourceJD can be
/// re-exported. If it is false, attempts to re-export a non-exported symbol
/// will result in a "symbol not found" error.
inline std::unique_ptr<ReExportsMaterializationUnit>
reexports(JITDylib &SourceJD, SymbolAliasMap Aliases,
          bool MatchNonExported = false, VModuleKey K = VModuleKey()) {
  return llvm::make_unique<ReExportsMaterializationUnit>(
      &SourceJD, MatchNonExported, std::move(Aliases), std::move(K));
}

/// Build a SymbolAliasMap for the common case where you want to re-export
/// symbols from another JITDylib with the same linkage/flags.
Expected<SymbolAliasMap>
buildSimpleReexportsAliasMap(JITDylib &SourceJD, const SymbolNameSet &Symbols);

/// ReexportsGenerator can be used with JITDylib::setGenerator to automatically
/// re-export a subset of the source JITDylib's symbols in the target.
class ReexportsGenerator {
public:
  using SymbolPredicate = std::function<bool(SymbolStringPtr)>;

  /// Create a reexports generator. If an Allow predicate is passed, only
  /// symbols for which the predicate returns true will be reexported. If no
  /// Allow predicate is passed, all symbols will be exported.
  ReexportsGenerator(JITDylib &SourceJD, bool MatchNonExported = false,
                     SymbolPredicate Allow = SymbolPredicate());

  SymbolNameSet operator()(JITDylib &JD, const SymbolNameSet &Names);

private:
  JITDylib &SourceJD;
  bool MatchNonExported = false;
  SymbolPredicate Allow;
};

/// A symbol query that returns results via a callback when results are
///        ready.
///
/// makes a callback when all symbols are available.
class AsynchronousSymbolQuery {
  friend class ExecutionSession;
  friend class JITDylib;
  friend class JITSymbolResolverAdapter;

public:

  /// Create a query for the given symbols, notify-resolved and
  ///        notify-ready callbacks.
  AsynchronousSymbolQuery(const SymbolNameSet &Symbols,
                          SymbolsResolvedCallback NotifySymbolsResolved,
                          SymbolsReadyCallback NotifySymbolsReady);

  /// Set the resolved symbol information for the given symbol name.
  void resolve(const SymbolStringPtr &Name, JITEvaluatedSymbol Sym);

  /// Returns true if all symbols covered by this query have been
  ///        resolved.
  bool isFullyResolved() const { return NotYetResolvedCount == 0; }

  /// Call the NotifySymbolsResolved callback.
  ///
  /// This should only be called if all symbols covered by the query have been
  /// resolved.
  void handleFullyResolved();

  /// Notify the query that a requested symbol is ready for execution.
  void notifySymbolReady();

  /// Returns true if all symbols covered by this query are ready.
  bool isFullyReady() const { return NotYetReadyCount == 0; }

  /// Calls the NotifySymbolsReady callback.
  ///
  /// This should only be called if all symbols covered by this query are ready.
  void handleFullyReady();

private:
  void addQueryDependence(JITDylib &JD, SymbolStringPtr Name);

  void removeQueryDependence(JITDylib &JD, const SymbolStringPtr &Name);

  bool canStillFail();

  void handleFailed(Error Err);

  void detach();

  SymbolsResolvedCallback NotifySymbolsResolved;
  SymbolsReadyCallback NotifySymbolsReady;
  SymbolDependenceMap QueryRegistrations;
  SymbolMap ResolvedSymbols;
  size_t NotYetResolvedCount;
  size_t NotYetReadyCount;
};

/// A symbol table that supports asynchoronous symbol queries.
///
/// Represents a virtual shared object. Instances can not be copied or moved, so
/// their addresses may be used as keys for resource management.
/// JITDylib state changes must be made via an ExecutionSession to guarantee
/// that they are synchronized with respect to other JITDylib operations.
class JITDylib {
  friend class AsynchronousSymbolQuery;
  friend class ExecutionSession;
  friend class MaterializationResponsibility;
public:
  using GeneratorFunction = std::function<SymbolNameSet(
      JITDylib &Parent, const SymbolNameSet &Names)>;

  using AsynchronousSymbolQuerySet =
    std::set<std::shared_ptr<AsynchronousSymbolQuery>>;

  JITDylib(const JITDylib &) = delete;
  JITDylib &operator=(const JITDylib &) = delete;
  JITDylib(JITDylib &&) = delete;
  JITDylib &operator=(JITDylib &&) = delete;

  /// Get the name for this JITDylib.
  const std::string &getName() const { return JITDylibName; }

  /// Get a reference to the ExecutionSession for this JITDylib.
  ExecutionSession &getExecutionSession() const { return ES; }

  /// Set a definition generator. If set, whenever a symbol fails to resolve
  /// within this JITDylib, lookup and lookupFlags will pass the unresolved
  /// symbols set to the definition generator. The generator can optionally
  /// add a definition for the unresolved symbols to the dylib.
  void setGenerator(GeneratorFunction DefGenerator) {
    this->DefGenerator = std::move(DefGenerator);
  }

  /// Set the search order to be used when fixing up definitions in JITDylib.
  /// This will replace the previous search order, and apply to any symbol
  /// resolutions made for definitions in this JITDylib after the call to
  /// setSearchOrder (even if the definition itself was added before the
  /// call).
  ///
  /// If SearchThisJITDylibFirst is set, which by default it is, then this
  /// JITDylib will add itself to the beginning of the SearchOrder (Clients
  /// should *not* put this JITDylib in the list in this case, to avoid
  /// redundant lookups).
  ///
  /// If SearchThisJITDylibFirst is false then the search order will be used as
  /// given. The main motivation for this feature is to support deliberate
  /// shadowing of symbols in this JITDylib by a facade JITDylib. For example,
  /// the facade may resolve function names to stubs, and the stubs may compile
  /// lazily by looking up symbols in this dylib. Adding the facade dylib
  /// as the first in the search order (instead of this dylib) ensures that
  /// definitions within this dylib resolve to the lazy-compiling stubs,
  /// rather than immediately materializing the definitions in this dylib.
  void setSearchOrder(JITDylibSearchList NewSearchOrder,
                      bool SearchThisJITDylibFirst = true,
                      bool MatchNonExportedInThisDylib = true);

  /// Add the given JITDylib to the search order for definitions in this
  /// JITDylib.
  void addToSearchOrder(JITDylib &JD, bool MatcNonExported = false);

  /// Replace OldJD with NewJD in the search order if OldJD is present.
  /// Otherwise this operation is a no-op.
  void replaceInSearchOrder(JITDylib &OldJD, JITDylib &NewJD,
                            bool MatchNonExported = false);

  /// Remove the given JITDylib from the search order for this JITDylib if it is
  /// present. Otherwise this operation is a no-op.
  void removeFromSearchOrder(JITDylib &JD);

  /// Do something with the search order (run under the session lock).
  template <typename Func>
  auto withSearchOrderDo(Func &&F)
      -> decltype(F(std::declval<const JITDylibSearchList &>()));

  /// Define all symbols provided by the materialization unit to be part of this
  /// JITDylib.
  ///
  /// This overload always takes ownership of the MaterializationUnit. If any
  /// errors occur, the MaterializationUnit consumed.
  template <typename MaterializationUnitType>
  Error define(std::unique_ptr<MaterializationUnitType> &&MU);

  /// Define all symbols provided by the materialization unit to be part of this
  /// JITDylib.
  ///
  /// This overload only takes ownership of the MaterializationUnit no error is
  /// generated. If an error occurs, ownership remains with the caller. This
  /// may allow the caller to modify the MaterializationUnit to correct the
  /// issue, then re-call define.
  template <typename MaterializationUnitType>
  Error define(std::unique_ptr<MaterializationUnitType> &MU);

  /// Tries to remove the given symbols.
  ///
  /// If any symbols are not defined in this JITDylib this method will return
  /// a SymbolsNotFound error covering the missing symbols.
  ///
  /// If all symbols are found but some symbols are in the process of being
  /// materialized this method will return a SymbolsCouldNotBeRemoved error.
  ///
  /// On success, all symbols are removed. On failure, the JITDylib state is
  /// left unmodified (no symbols are removed).
  Error remove(const SymbolNameSet &Names);

  /// Search the given JITDylib for the symbols in Symbols. If found, store
  ///        the flags for each symbol in Flags. Returns any unresolved symbols.
  SymbolFlagsMap lookupFlags(const SymbolNameSet &Names);

  /// Dump current JITDylib state to OS.
  void dump(raw_ostream &OS);

  /// FIXME: Remove this when we remove the old ORC layers.
  /// Search the given JITDylibs in order for the symbols in Symbols. Results
  ///        (once they become available) will be returned via the given Query.
  ///
  /// If any symbol is not found then the unresolved symbols will be returned,
  /// and the query will not be applied. The Query is not failed and can be
  /// re-used in a subsequent lookup once the symbols have been added, or
  /// manually failed.
  SymbolNameSet legacyLookup(std::shared_ptr<AsynchronousSymbolQuery> Q,
                             SymbolNameSet Names);

private:
  using AsynchronousSymbolQueryList =
      std::vector<std::shared_ptr<AsynchronousSymbolQuery>>;

  struct UnmaterializedInfo {
    UnmaterializedInfo(std::unique_ptr<MaterializationUnit> MU)
        : MU(std::move(MU)) {}

    std::unique_ptr<MaterializationUnit> MU;
  };

  using UnmaterializedInfosMap =
      DenseMap<SymbolStringPtr, std::shared_ptr<UnmaterializedInfo>>;

  struct MaterializingInfo {
    AsynchronousSymbolQueryList PendingQueries;
    SymbolDependenceMap Dependants;
    SymbolDependenceMap UnemittedDependencies;
    bool IsEmitted = false;
  };

  using MaterializingInfosMap = DenseMap<SymbolStringPtr, MaterializingInfo>;

  using LookupImplActionFlags = enum {
    None = 0,
    NotifyFullyResolved = 1 << 0U,
    NotifyFullyReady = 1 << 1U,
    LLVM_MARK_AS_BITMASK_ENUM(NotifyFullyReady)
  };

  JITDylib(ExecutionSession &ES, std::string Name);

  Error defineImpl(MaterializationUnit &MU);

  SymbolNameSet lookupFlagsImpl(SymbolFlagsMap &Flags,
                                const SymbolNameSet &Names);

  void lodgeQuery(std::shared_ptr<AsynchronousSymbolQuery> &Q,
                  SymbolNameSet &Unresolved, bool MatchNonExported,
                  MaterializationUnitList &MUs);

  void lodgeQueryImpl(std::shared_ptr<AsynchronousSymbolQuery> &Q,
                      SymbolNameSet &Unresolved, bool MatchNonExported,
                      MaterializationUnitList &MUs);

  LookupImplActionFlags
  lookupImpl(std::shared_ptr<AsynchronousSymbolQuery> &Q,
             std::vector<std::unique_ptr<MaterializationUnit>> &MUs,
             SymbolNameSet &Unresolved);

  void detachQueryHelper(AsynchronousSymbolQuery &Q,
                         const SymbolNameSet &QuerySymbols);

  void transferEmittedNodeDependencies(MaterializingInfo &DependantMI,
                                       const SymbolStringPtr &DependantName,
                                       MaterializingInfo &EmittedMI);

  Error defineMaterializing(const SymbolFlagsMap &SymbolFlags);

  void replace(std::unique_ptr<MaterializationUnit> MU);

  SymbolNameSet getRequestedSymbols(const SymbolFlagsMap &SymbolFlags) const;

  void addDependencies(const SymbolStringPtr &Name,
                       const SymbolDependenceMap &Dependants);

  void resolve(const SymbolMap &Resolved);

  void emit(const SymbolFlagsMap &Emitted);

  void notifyFailed(const SymbolNameSet &FailedSymbols);

  ExecutionSession &ES;
  std::string JITDylibName;
  SymbolMap Symbols;
  UnmaterializedInfosMap UnmaterializedInfos;
  MaterializingInfosMap MaterializingInfos;
  GeneratorFunction DefGenerator;
  JITDylibSearchList SearchOrder;
};

/// An ExecutionSession represents a running JIT program.
class ExecutionSession {
  // FIXME: Remove this when we remove the old ORC layers.
  friend class JITDylib;

public:
  /// For reporting errors.
  using ErrorReporter = std::function<void(Error)>;

  /// For dispatching MaterializationUnit::materialize calls.
  using DispatchMaterializationFunction = std::function<void(
      JITDylib &JD, std::unique_ptr<MaterializationUnit> MU)>;

  /// Construct an ExecutionSession.
  ///
  /// SymbolStringPools may be shared between ExecutionSessions.
  ExecutionSession(std::shared_ptr<SymbolStringPool> SSP = nullptr);

  /// Add a symbol name to the SymbolStringPool and return a pointer to it.
  SymbolStringPtr intern(StringRef SymName) { return SSP->intern(SymName); }

  /// Returns a shared_ptr to the SymbolStringPool for this ExecutionSession.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() const { return SSP; }

  /// Run the given lambda with the session mutex locked.
  template <typename Func> auto runSessionLocked(Func &&F) -> decltype(F()) {
    std::lock_guard<std::recursive_mutex> Lock(SessionMutex);
    return F();
  }

  /// Get the "main" JITDylib, which is created automatically on construction of
  /// the ExecutionSession.
  JITDylib &getMainJITDylib();

  /// Add a new JITDylib to this ExecutionSession.
  JITDylib &createJITDylib(std::string Name,
                           bool AddToMainDylibSearchOrder = true);

  /// Allocate a module key for a new module to add to the JIT.
  VModuleKey allocateVModule() {
    return runSessionLocked([this]() { return ++LastKey; });
  }

  /// Return a module key to the ExecutionSession so that it can be
  ///        re-used. This should only be done once all resources associated
  ///        with the original key have been released.
  void releaseVModule(VModuleKey Key) { /* FIXME: Recycle keys */
  }

  /// Set the error reporter function.
  ExecutionSession &setErrorReporter(ErrorReporter ReportError) {
    this->ReportError = std::move(ReportError);
    return *this;
  }

  /// Report a error for this execution session.
  ///
  /// Unhandled errors can be sent here to log them.
  void reportError(Error Err) { ReportError(std::move(Err)); }

  /// Set the materialization dispatch function.
  ExecutionSession &setDispatchMaterialization(
      DispatchMaterializationFunction DispatchMaterialization) {
    this->DispatchMaterialization = std::move(DispatchMaterialization);
    return *this;
  }

  void legacyFailQuery(AsynchronousSymbolQuery &Q, Error Err);

  using LegacyAsyncLookupFunction = std::function<SymbolNameSet(
      std::shared_ptr<AsynchronousSymbolQuery> Q, SymbolNameSet Names)>;

  /// A legacy lookup function for JITSymbolResolverAdapter.
  /// Do not use -- this will be removed soon.
  Expected<SymbolMap>
  legacyLookup(LegacyAsyncLookupFunction AsyncLookup, SymbolNameSet Names,
               bool WaiUntilReady,
               RegisterDependenciesFunction RegisterDependencies);

  /// Search the given JITDylib list for the given symbols.
  ///
  /// SearchOrder lists the JITDylibs to search. For each dylib, the associated
  /// boolean indicates whether the search should match against non-exported
  /// (hidden visibility) symbols in that dylib (true means match against
  /// non-exported symbols, false means do not match).
  ///
  /// The OnResolve callback will be called once all requested symbols are
  /// resolved, or if an error occurs prior to resolution.
  ///
  /// The OnReady callback will be called once all requested symbols are ready,
  /// or if an error occurs after resolution but before all symbols are ready.
  ///
  /// If all symbols are found, the RegisterDependencies function will be called
  /// while the session lock is held. This gives clients a chance to register
  /// dependencies for on the queried symbols for any symbols they are
  /// materializing (if a MaterializationResponsibility instance is present,
  /// this can be implemented by calling
  /// MaterializationResponsibility::addDependencies). If there are no
  /// dependenant symbols for this query (e.g. it is being made by a top level
  /// client to get an address to call) then the value NoDependenciesToRegister
  /// can be used.
  void lookup(const JITDylibSearchList &SearchOrder, SymbolNameSet Symbols,
              SymbolsResolvedCallback OnResolve, SymbolsReadyCallback OnReady,
              RegisterDependenciesFunction RegisterDependencies);

  /// Blocking version of lookup above. Returns the resolved symbol map.
  /// If WaitUntilReady is true (the default), will not return until all
  /// requested symbols are ready (or an error occurs). If WaitUntilReady is
  /// false, will return as soon as all requested symbols are resolved,
  /// or an error occurs. If WaitUntilReady is false and an error occurs
  /// after resolution, the function will return a success value, but the
  /// error will be reported via reportErrors.
  Expected<SymbolMap> lookup(const JITDylibSearchList &SearchOrder,
                             const SymbolNameSet &Symbols,
                             RegisterDependenciesFunction RegisterDependencies =
                                 NoDependenciesToRegister,
                             bool WaitUntilReady = true);

  /// Convenience version of blocking lookup.
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol.
  Expected<JITEvaluatedSymbol> lookup(const JITDylibSearchList &SearchOrder,
                                      SymbolStringPtr Symbol);

  /// Convenience version of blocking lookup.
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol. The search will not find non-exported symbols.
  Expected<JITEvaluatedSymbol> lookup(ArrayRef<JITDylib *> SearchOrder,
                                      SymbolStringPtr Symbol);

  /// Convenience version of blocking lookup.
  /// Searches each of the JITDylibs in the search order in turn for the given
  /// symbol. The search will not find non-exported symbols.
  Expected<JITEvaluatedSymbol> lookup(ArrayRef<JITDylib *> SearchOrder,
                                      StringRef Symbol);

  /// Materialize the given unit.
  void dispatchMaterialization(JITDylib &JD,
                               std::unique_ptr<MaterializationUnit> MU) {
    LLVM_DEBUG(runSessionLocked([&]() {
                 dbgs() << "Compiling, for " << JD.getName() << ", " << *MU
                        << "\n";
               }););
    DispatchMaterialization(JD, std::move(MU));
  }

  /// Dump the state of all the JITDylibs in this session.
  void dump(raw_ostream &OS);

private:
  static void logErrorsToStdErr(Error Err) {
    logAllUnhandledErrors(std::move(Err), errs(), "JIT session error: ");
  }

  static void
  materializeOnCurrentThread(JITDylib &JD,
                             std::unique_ptr<MaterializationUnit> MU) {
    MU->doMaterialize(JD);
  }

  void runOutstandingMUs();

  mutable std::recursive_mutex SessionMutex;
  std::shared_ptr<SymbolStringPool> SSP;
  VModuleKey LastKey = 0;
  ErrorReporter ReportError = logErrorsToStdErr;
  DispatchMaterializationFunction DispatchMaterialization =
      materializeOnCurrentThread;

  std::vector<std::unique_ptr<JITDylib>> JDs;

  // FIXME: Remove this (and runOutstandingMUs) once the linking layer works
  //        with callbacks from asynchronous queries.
  mutable std::recursive_mutex OutstandingMUsMutex;
  std::vector<std::pair<JITDylib *, std::unique_ptr<MaterializationUnit>>>
      OutstandingMUs;
};

template <typename Func>
auto JITDylib::withSearchOrderDo(Func &&F)
    -> decltype(F(std::declval<const JITDylibSearchList &>())) {
  return ES.runSessionLocked([&]() { return F(SearchOrder); });
}

template <typename MaterializationUnitType>
Error JITDylib::define(std::unique_ptr<MaterializationUnitType> &&MU) {
  assert(MU && "Can not define with a null MU");
  return ES.runSessionLocked([&, this]() -> Error {
    if (auto Err = defineImpl(*MU))
      return Err;

    /// defineImpl succeeded.
    auto UMI = std::make_shared<UnmaterializedInfo>(std::move(MU));
    for (auto &KV : UMI->MU->getSymbols())
      UnmaterializedInfos[KV.first] = UMI;

    return Error::success();
  });
}

template <typename MaterializationUnitType>
Error JITDylib::define(std::unique_ptr<MaterializationUnitType> &MU) {
  assert(MU && "Can not define with a null MU");

  return ES.runSessionLocked([&, this]() -> Error {
    if (auto Err = defineImpl(*MU))
      return Err;

    /// defineImpl succeeded.
    auto UMI = std::make_shared<UnmaterializedInfo>(std::move(MU));
    for (auto &KV : UMI->MU->getSymbols())
      UnmaterializedInfos[KV.first] = UMI;

    return Error::success();
  });
}

/// Mangles symbol names then uniques them in the context of an
/// ExecutionSession.
class MangleAndInterner {
public:
  MangleAndInterner(ExecutionSession &ES, const DataLayout &DL);
  SymbolStringPtr operator()(StringRef Name);

private:
  ExecutionSession &ES;
  const DataLayout &DL;
};

} // End namespace orc
} // End namespace llvm

#undef DEBUG_TYPE // "orc"

#endif // LLVM_EXECUTIONENGINE_ORC_CORE_H
