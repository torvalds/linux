//===- lld/Core/LinkingContext.h - Linker Target Info Interface -*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_LINKING_CONTEXT_H
#define LLD_CORE_LINKING_CONTEXT_H

#include "lld/Core/Node.h"
#include "lld/Core/Reader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lld {

class PassManager;
class File;
class Writer;
class Node;
class SharedLibraryFile;

/// The LinkingContext class encapsulates "what and how" to link.
///
/// The base class LinkingContext contains the options needed by core linking.
/// Subclasses of LinkingContext have additional options needed by specific
/// Writers.
class LinkingContext {
public:
  virtual ~LinkingContext();

  /// \name Methods needed by core linking
  /// @{

  /// Name of symbol linker should use as "entry point" to program,
  /// usually "main" or "start".
  virtual StringRef entrySymbolName() const { return _entrySymbolName; }

  /// Whether core linking should remove Atoms not reachable by following
  /// References from the entry point Atom or from all global scope Atoms
  /// if globalsAreDeadStripRoots() is true.
  bool deadStrip() const { return _deadStrip; }

  /// Only used if deadStrip() returns true.  Means all global scope Atoms
  /// should be marked live (along with all Atoms they reference).  Usually
  /// this method returns false for main executables, but true for dynamic
  /// shared libraries.
  bool globalsAreDeadStripRoots() const { return _globalsAreDeadStripRoots; }

  /// Only used if deadStrip() returns true.  This method returns the names
  /// of DefinedAtoms that should be marked live (along with all Atoms they
  /// reference). Only Atoms with scope scopeLinkageUnit or scopeGlobal can
  /// be kept live using this method.
  ArrayRef<StringRef> deadStripRoots() const {
    return _deadStripRoots;
  }

  /// Add the given symbol name to the dead strip root set. Only used if
  /// deadStrip() returns true.
  void addDeadStripRoot(StringRef symbolName) {
    assert(!symbolName.empty() && "Empty symbol cannot be a dead strip root");
    _deadStripRoots.push_back(symbolName);
  }

  /// Normally, every UndefinedAtom must be replaced by a DefinedAtom or a
  /// SharedLibraryAtom for the link to be successful.  This method controls
  /// whether core linking prints out a list of remaining UndefinedAtoms.
  ///
  /// \todo This should be a method core linking calls with a list of the
  /// UndefinedAtoms so that different drivers can format the error message
  /// as needed.
  bool printRemainingUndefines() const { return _printRemainingUndefines; }

  /// Normally, every UndefinedAtom must be replaced by a DefinedAtom or a
  /// SharedLibraryAtom for the link to be successful.  This method controls
  /// whether core linking considers remaining undefines to be an error.
  bool allowRemainingUndefines() const { return _allowRemainingUndefines; }

  /// Normally, every UndefinedAtom must be replaced by a DefinedAtom or a
  /// SharedLibraryAtom for the link to be successful.  This method controls
  /// whether core linking considers remaining undefines from the shared library
  /// to be an error.
  bool allowShlibUndefines() const { return _allowShlibUndefines; }

  /// If true, core linking will write the path to each input file to stdout
  /// (i.e. llvm::outs()) as it is used.  This is used to implement the -t
  /// linker option.
  ///
  /// \todo This should be a method core linking calls so that drivers can
  /// format the line as needed.
  bool logInputFiles() const { return _logInputFiles; }

  /// Parts of LLVM use global variables which are bound to command line
  /// options (see llvm::cl::Options). This method returns "command line"
  /// options which are used to configure LLVM's command line settings.
  /// For instance the -debug-only XXX option can be used to dynamically
  /// trace different parts of LLVM and lld.
  ArrayRef<const char *> llvmOptions() const { return _llvmOptions; }

  /// \name Methods used by Drivers to configure TargetInfo
  /// @{
  void setOutputPath(StringRef str) { _outputPath = str; }

  // Set the entry symbol name. You may also need to call addDeadStripRoot() for
  // the symbol if your platform supports dead-stripping, so that the symbol
  // will not be removed from the output.
  void setEntrySymbolName(StringRef name) {
    _entrySymbolName = name;
  }

  void setDeadStripping(bool enable) { _deadStrip = enable; }
  void setGlobalsAreDeadStripRoots(bool v) { _globalsAreDeadStripRoots = v; }

  void setPrintRemainingUndefines(bool print) {
    _printRemainingUndefines = print;
  }

  void setAllowRemainingUndefines(bool allow) {
    _allowRemainingUndefines = allow;
  }

  void setAllowShlibUndefines(bool allow) { _allowShlibUndefines = allow; }
  void setLogInputFiles(bool log) { _logInputFiles = log; }

  void appendLLVMOption(const char *opt) { _llvmOptions.push_back(opt); }

  std::vector<std::unique_ptr<Node>> &getNodes() { return _nodes; }
  const std::vector<std::unique_ptr<Node>> &getNodes() const { return _nodes; }

  /// This method adds undefined symbols specified by the -u option to the to
  /// the list of undefined symbols known to the linker. This option essentially
  /// forces an undefined symbol to be created. You may also need to call
  /// addDeadStripRoot() for the symbol if your platform supports dead
  /// stripping, so that the symbol will not be removed from the output.
  void addInitialUndefinedSymbol(StringRef symbolName) {
    _initialUndefinedSymbols.push_back(symbolName);
  }

  /// Iterators for symbols that appear on the command line.
  typedef std::vector<StringRef> StringRefVector;
  typedef StringRefVector::iterator StringRefVectorIter;
  typedef StringRefVector::const_iterator StringRefVectorConstIter;

  /// Create linker internal files containing atoms for the linker to include
  /// during link. Flavors can override this function in their LinkingContext
  /// to add more internal files. These internal files are positioned before
  /// the actual input files.
  virtual void createInternalFiles(std::vector<std::unique_ptr<File>> &) const;

  /// Return the list of undefined symbols that are specified in the
  /// linker command line, using the -u option.
  ArrayRef<StringRef> initialUndefinedSymbols() const {
    return _initialUndefinedSymbols;
  }

  /// After all set* methods are called, the Driver calls this method
  /// to validate that there are no missing options or invalid combinations
  /// of options.  If there is a problem, a description of the problem
  /// is written to the global error handler.
  ///
  /// \returns true if there is an error with the current settings.
  bool validate();

  /// Formats symbol name for use in error messages.
  virtual std::string demangle(StringRef symbolName) const = 0;

  /// @}
  /// \name Methods used by Driver::link()
  /// @{

  /// Returns the file system path to which the linked output should be written.
  ///
  /// \todo To support in-memory linking, we need an abstraction that allows
  /// the linker to write to an in-memory buffer.
  StringRef outputPath() const { return _outputPath; }

  /// Accessor for Register object embedded in LinkingContext.
  const Registry &registry() const { return _registry; }
  Registry &registry() { return _registry; }

  /// This method is called by core linking to give the Writer a chance
  /// to add file format specific "files" to set of files to be linked. This is
  /// how file format specific atoms can be added to the link.
  virtual void createImplicitFiles(std::vector<std::unique_ptr<File>> &) = 0;

  /// This method is called by core linking to build the list of Passes to be
  /// run on the merged/linked graph of all input files.
  virtual void addPasses(PassManager &pm) = 0;

  /// Calls through to the writeFile() method on the specified Writer.
  ///
  /// \param linkedFile This is the merged/linked graph of all input file Atoms.
  virtual llvm::Error writeFile(const File &linkedFile) const;

  /// Return the next ordinal and Increment it.
  virtual uint64_t getNextOrdinalAndIncrement() const { return _nextOrdinal++; }

  // This function is called just before the Resolver kicks in.
  // Derived classes may use it to change the list of input files.
  virtual void finalizeInputFiles() = 0;

  /// Callback invoked for each file the Resolver decides we are going to load.
  /// This can be used to update context state based on the file, and emit
  /// errors for any differences between the context state and a loaded file.
  /// For example, we can error if we try to load a file which is a different
  /// arch from that being linked.
  virtual llvm::Error handleLoadedFile(File &file) = 0;

  /// @}
protected:
  LinkingContext(); // Must be subclassed

  /// Abstract method to lazily instantiate the Writer.
  virtual Writer &writer() const = 0;

  /// Method to create an internal file for the entry symbol
  virtual std::unique_ptr<File> createEntrySymbolFile() const;
  std::unique_ptr<File> createEntrySymbolFile(StringRef filename) const;

  /// Method to create an internal file for an undefined symbol
  virtual std::unique_ptr<File> createUndefinedSymbolFile() const;
  std::unique_ptr<File> createUndefinedSymbolFile(StringRef filename) const;

  StringRef _outputPath;
  StringRef _entrySymbolName;
  bool _deadStrip = false;
  bool _globalsAreDeadStripRoots = false;
  bool _printRemainingUndefines = true;
  bool _allowRemainingUndefines = false;
  bool _logInputFiles = false;
  bool _allowShlibUndefines = false;
  std::vector<StringRef> _deadStripRoots;
  std::vector<const char *> _llvmOptions;
  StringRefVector _initialUndefinedSymbols;
  std::vector<std::unique_ptr<Node>> _nodes;
  mutable llvm::BumpPtrAllocator _allocator;
  mutable uint64_t _nextOrdinal = 0;
  Registry _registry;

private:
  /// Validate the subclass bits. Only called by validate.
  virtual bool validateImpl() = 0;
};

} // end namespace lld

#endif // LLD_CORE_LINKING_CONTEXT_H
