//===- xray-extract.cpp: XRay Instrumentation Map Extraction --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the xray-extract.h interface.
//
// FIXME: Support other XRay-instrumented binary formats other than ELF.
//
//===----------------------------------------------------------------------===//


#include "func-id-helper.h"
#include "xray-registry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/InstrumentationMap.h"

using namespace llvm;
using namespace llvm::xray;
using namespace llvm::yaml;

// llvm-xray extract
// ----------------------------------------------------------------------------
static cl::SubCommand Extract("extract", "Extract instrumentation maps");
static cl::opt<std::string> ExtractInput(cl::Positional,
                                         cl::desc("<input file>"), cl::Required,
                                         cl::sub(Extract));
static cl::opt<std::string>
    ExtractOutput("output", cl::value_desc("output file"), cl::init("-"),
                  cl::desc("output file; use '-' for stdout"),
                  cl::sub(Extract));
static cl::alias ExtractOutput2("o", cl::aliasopt(ExtractOutput),
                                cl::desc("Alias for -output"),
                                cl::sub(Extract));
static cl::opt<bool> ExtractSymbolize("symbolize", cl::value_desc("symbolize"),
                                      cl::init(false),
                                      cl::desc("symbolize functions"),
                                      cl::sub(Extract));
static cl::alias ExtractSymbolize2("s", cl::aliasopt(ExtractSymbolize),
                                   cl::desc("alias for -symbolize"),
                                   cl::sub(Extract));

namespace {

void exportAsYAML(const InstrumentationMap &Map, raw_ostream &OS,
                  FuncIdConversionHelper &FH) {
  // First we translate the sleds into the YAMLXRaySledEntry objects in a deque.
  std::vector<YAMLXRaySledEntry> YAMLSleds;
  auto Sleds = Map.sleds();
  YAMLSleds.reserve(std::distance(Sleds.begin(), Sleds.end()));
  for (const auto &Sled : Sleds) {
    auto FuncId = Map.getFunctionId(Sled.Function);
    if (!FuncId)
      return;
    YAMLSleds.push_back({*FuncId, Sled.Address, Sled.Function, Sled.Kind,
                         Sled.AlwaysInstrument,
                         ExtractSymbolize ? FH.SymbolOrNumber(*FuncId) : ""});
  }
  Output Out(OS, nullptr, 0);
  Out << YAMLSleds;
}

} // namespace

static CommandRegistration Unused(&Extract, []() -> Error {
  auto InstrumentationMapOrError = loadInstrumentationMap(ExtractInput);
  if (!InstrumentationMapOrError)
    return joinErrors(make_error<StringError>(
                          Twine("Cannot extract instrumentation map from '") +
                              ExtractInput + "'.",
                          std::make_error_code(std::errc::invalid_argument)),
                      InstrumentationMapOrError.takeError());

  std::error_code EC;
  raw_fd_ostream OS(ExtractOutput, EC, sys::fs::OpenFlags::F_Text);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + ExtractOutput + "' for writing.", EC);
  const auto &FunctionAddresses =
      InstrumentationMapOrError->getFunctionAddresses();
  symbolize::LLVMSymbolizer::Options Opts(
      symbolize::FunctionNameKind::LinkageName, true, true, false, "");
  symbolize::LLVMSymbolizer Symbolizer(Opts);
  llvm::xray::FuncIdConversionHelper FuncIdHelper(ExtractInput, Symbolizer,
                                                  FunctionAddresses);
  exportAsYAML(*InstrumentationMapOrError, OS, FuncIdHelper);
  return Error::success();
});
