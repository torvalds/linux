//===- lib/Tooling/Execution.cpp - Standalone clang action execution. -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/StandaloneExecution.h"
#include "clang/Tooling/ToolExecutorPluginRegistry.h"

namespace clang {
namespace tooling {

static llvm::Error make_string_error(const llvm::Twine &Message) {
  return llvm::make_error<llvm::StringError>(Message,
                                             llvm::inconvertibleErrorCode());
}

const char *StandaloneToolExecutor::ExecutorName = "StandaloneToolExecutor";

static ArgumentsAdjuster getDefaultArgumentsAdjusters() {
  return combineAdjusters(
      getClangStripOutputAdjuster(),
      combineAdjusters(getClangSyntaxOnlyAdjuster(),
                       getClangStripDependencyFileAdjuster()));
}

StandaloneToolExecutor::StandaloneToolExecutor(
    const CompilationDatabase &Compilations,
    llvm::ArrayRef<std::string> SourcePaths,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : Tool(Compilations, SourcePaths, std::move(PCHContainerOps),
           std::move(BaseFS)),
      Context(&Results), ArgsAdjuster(getDefaultArgumentsAdjusters()) {
  // Use self-defined default argument adjusters instead of the default
  // adjusters that come with the old `ClangTool`.
  Tool.clearArgumentsAdjusters();
}

StandaloneToolExecutor::StandaloneToolExecutor(
    CommonOptionsParser Options,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : OptionsParser(std::move(Options)),
      Tool(OptionsParser->getCompilations(), OptionsParser->getSourcePathList(),
           std::move(PCHContainerOps)),
      Context(&Results), ArgsAdjuster(getDefaultArgumentsAdjusters()) {
  Tool.clearArgumentsAdjusters();
}

llvm::Error StandaloneToolExecutor::execute(
    llvm::ArrayRef<
        std::pair<std::unique_ptr<FrontendActionFactory>, ArgumentsAdjuster>>
        Actions) {
  if (Actions.empty())
    return make_string_error("No action to execute.");

  if (Actions.size() != 1)
    return make_string_error(
        "Only support executing exactly 1 action at this point.");

  auto &Action = Actions.front();
  Tool.appendArgumentsAdjuster(Action.second);
  Tool.appendArgumentsAdjuster(ArgsAdjuster);
  if (Tool.run(Action.first.get()))
    return make_string_error("Failed to run action.");

  return llvm::Error::success();
}

class StandaloneToolExecutorPlugin : public ToolExecutorPlugin {
public:
  llvm::Expected<std::unique_ptr<ToolExecutor>>
  create(CommonOptionsParser &OptionsParser) override {
    if (OptionsParser.getSourcePathList().empty())
      return make_string_error(
          "[StandaloneToolExecutorPlugin] No positional argument found.");
    return llvm::make_unique<StandaloneToolExecutor>(std::move(OptionsParser));
  }
};

static ToolExecutorPluginRegistry::Add<StandaloneToolExecutorPlugin>
    X("standalone", "Runs FrontendActions on a set of files provided "
                    "via positional arguments.");

// This anchor is used to force the linker to link in the generated object file
// and thus register the plugin.
volatile int StandaloneToolExecutorAnchorSource = 0;

} // end namespace tooling
} // end namespace clang
