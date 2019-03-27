//===--- Execution.h - Executing clang frontend actions -*- C++ ---------*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines framework for executing clang frontend actions.
//
//  The framework can be extended to support different execution plans including
//  standalone execution on the given TUs or parallel execution on all TUs in
//  the codebase.
//
//  In order to enable multiprocessing execution, tool actions are expected to
//  output result into the ToolResults provided by the executor. The
//  `ToolResults` is an interface that abstracts how results are stored e.g.
//  in-memory for standalone execution or on-disk for large-scale execution.
//
//  New executors can be registered as ToolExecutorPlugins via the
//  `ToolExecutorPluginRegistry`. CLI tools can use
//  `createExecutorFromCommandLineArgs` to create a specific registered executor
//  according to the command-line arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_EXECUTION_H
#define LLVM_CLANG_TOOLING_EXECUTION_H

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Registry.h"
#include "llvm/Support/StringSaver.h"

namespace clang {
namespace tooling {

extern llvm::cl::opt<std::string> ExecutorName;

/// An abstraction for the result of a tool execution. For example, the
/// underlying result can be in-memory or on-disk.
///
/// Results should be string key-value pairs. For example, a refactoring tool
/// can use source location as key and a replacement in YAML format as value.
class ToolResults {
public:
  virtual ~ToolResults() = default;
  virtual void addResult(StringRef Key, StringRef Value) = 0;
  virtual std::vector<std::pair<llvm::StringRef, llvm::StringRef>>
  AllKVResults() = 0;
  virtual void forEachResult(
      llvm::function_ref<void(StringRef Key, StringRef Value)> Callback) = 0;
};

/// Stores the key-value results in memory. It maintains the lifetime of
/// the result. Clang tools using this class are expected to generate a small
/// set of different results, or a large set of duplicated results.
class InMemoryToolResults : public ToolResults {
public:
  InMemoryToolResults() : Strings(Arena) {}
  void addResult(StringRef Key, StringRef Value) override;
  std::vector<std::pair<llvm::StringRef, llvm::StringRef>>
  AllKVResults() override;
  void forEachResult(llvm::function_ref<void(StringRef Key, StringRef Value)>
                         Callback) override;

private:
  llvm::BumpPtrAllocator Arena;
  llvm::UniqueStringSaver Strings;

  std::vector<std::pair<llvm::StringRef, llvm::StringRef>> KVResults;
};

/// The context of an execution, including the information about
/// compilation and results.
class ExecutionContext {
public:
  virtual ~ExecutionContext() {}

  /// Initializes a context. This does not take ownership of `Results`.
  explicit ExecutionContext(ToolResults *Results) : Results(Results) {}

  /// Adds a KV pair to the result container of this execution.
  void reportResult(StringRef Key, StringRef Value);

  // Returns the source control system's revision number if applicable.
  // Otherwise returns an empty string.
  virtual std::string getRevision() { return ""; }

  // Returns the corpus being analyzed, e.g. "llvm" for the LLVM codebase, if
  // applicable.
  virtual std::string getCorpus() { return ""; }

  // Returns the currently processed compilation unit if available.
  virtual std::string getCurrentCompilationUnit() { return ""; }

private:
  ToolResults *Results;
};

/// Interface for executing clang frontend actions.
///
/// This can be extended to support running tool actions in different
/// execution mode, e.g. on a specific set of TUs or many TUs in parallel.
///
///  New executors can be registered as ToolExecutorPlugins via the
///  `ToolExecutorPluginRegistry`. CLI tools can use
///  `createExecutorFromCommandLineArgs` to create a specific registered
///  executor according to the command-line arguments.
class ToolExecutor {
public:
  virtual ~ToolExecutor() {}

  /// Returns the name of a specific executor.
  virtual StringRef getExecutorName() const = 0;

  /// Should return true iff executor runs all actions in a single process.
  /// Clients can use this signal to find out if they can collect results
  /// in-memory (e.g. to avoid serialization costs of using ToolResults).
  /// The single-process executors can still run multiple threads, but all
  /// executions are guaranteed to share the same memory.
  virtual bool isSingleProcess() const = 0;

  /// Executes each action with a corresponding arguments adjuster.
  virtual llvm::Error
  execute(llvm::ArrayRef<
          std::pair<std::unique_ptr<FrontendActionFactory>, ArgumentsAdjuster>>
              Actions) = 0;

  /// Convenient functions for the above `execute`.
  llvm::Error execute(std::unique_ptr<FrontendActionFactory> Action);
  /// Executes an action with an argument adjuster.
  llvm::Error execute(std::unique_ptr<FrontendActionFactory> Action,
                      ArgumentsAdjuster Adjuster);

  /// Returns a reference to the execution context.
  ///
  /// This should be passed to tool callbacks, and tool callbacks should report
  /// results via the returned context.
  virtual ExecutionContext *getExecutionContext() = 0;

  /// Returns a reference to the result container.
  ///
  /// NOTE: This should only be used after the execution finishes. Tool
  /// callbacks should report results via `ExecutionContext` instead.
  virtual ToolResults *getToolResults() = 0;

  /// Map a virtual file to be used while running the tool.
  ///
  /// \param FilePath The path at which the content will be mapped.
  /// \param Content A buffer of the file's content.
  virtual void mapVirtualFile(StringRef FilePath, StringRef Content) = 0;
};

/// Interface for factories that create specific executors. This is also
/// used as a plugin to be registered into ToolExecutorPluginRegistry.
class ToolExecutorPlugin {
public:
  virtual ~ToolExecutorPlugin() {}

  /// Create an `ToolExecutor`.
  ///
  /// `OptionsParser` can be consumed (e.g. moved) if the creation succeeds.
  virtual llvm::Expected<std::unique_ptr<ToolExecutor>>
  create(CommonOptionsParser &OptionsParser) = 0;
};

/// This creates a ToolExecutor that is in the global registry based on
/// commandline arguments.
///
/// This picks the right executor based on the `--executor` option. This parses
/// the commandline arguments with `CommonOptionsParser`, so caller does not
/// need to parse again.
///
/// By default, this creates a `StandaloneToolExecutor` ("standalone") if
/// `--executor` is not provided.
llvm::Expected<std::unique_ptr<ToolExecutor>>
createExecutorFromCommandLineArgs(int &argc, const char **argv,
                                  llvm::cl::OptionCategory &Category,
                                  const char *Overview = nullptr);

namespace internal {
llvm::Expected<std::unique_ptr<ToolExecutor>>
createExecutorFromCommandLineArgsImpl(int &argc, const char **argv,
                                      llvm::cl::OptionCategory &Category,
                                      const char *Overview = nullptr);
} // end namespace internal

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_EXECUTION_H
