//===-- xray-graph.cpp: XRay Function Call Graph Renderer -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Generate a DOT file to represent the function call graph encountered in
// the trace.
//
//===----------------------------------------------------------------------===//

#include "xray-graph.h"
#include "xray-registry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"

using namespace llvm;
using namespace llvm::xray;

// Setup llvm-xray graph subcommand and its options.
static cl::SubCommand GraphC("graph", "Generate function-call graph");
static cl::opt<std::string> GraphInput(cl::Positional,
                                       cl::desc("<xray log file>"),
                                       cl::Required, cl::sub(GraphC));

static cl::opt<bool>
    GraphKeepGoing("keep-going", cl::desc("Keep going on errors encountered"),
                   cl::sub(GraphC), cl::init(false));
static cl::alias GraphKeepGoing2("k", cl::aliasopt(GraphKeepGoing),
                                 cl::desc("Alias for -keep-going"),
                                 cl::sub(GraphC));

static cl::opt<std::string>
    GraphOutput("output", cl::value_desc("Output file"), cl::init("-"),
                cl::desc("output file; use '-' for stdout"), cl::sub(GraphC));
static cl::alias GraphOutput2("o", cl::aliasopt(GraphOutput),
                              cl::desc("Alias for -output"), cl::sub(GraphC));

static cl::opt<std::string>
    GraphInstrMap("instr_map",
                  cl::desc("binary with the instrumrntation map, or "
                           "a separate instrumentation map"),
                  cl::value_desc("binary with xray_instr_map"), cl::sub(GraphC),
                  cl::init(""));
static cl::alias GraphInstrMap2("m", cl::aliasopt(GraphInstrMap),
                                cl::desc("alias for -instr_map"),
                                cl::sub(GraphC));

static cl::opt<bool> GraphDeduceSiblingCalls(
    "deduce-sibling-calls",
    cl::desc("Deduce sibling calls when unrolling function call stacks"),
    cl::sub(GraphC), cl::init(false));
static cl::alias
    GraphDeduceSiblingCalls2("d", cl::aliasopt(GraphDeduceSiblingCalls),
                             cl::desc("Alias for -deduce-sibling-calls"),
                             cl::sub(GraphC));

static cl::opt<GraphRenderer::StatType>
    GraphEdgeLabel("edge-label",
                   cl::desc("Output graphs with edges labeled with this field"),
                   cl::value_desc("field"), cl::sub(GraphC),
                   cl::init(GraphRenderer::StatType::NONE),
                   cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                                         "Do not label Edges"),
                              clEnumValN(GraphRenderer::StatType::COUNT,
                                         "count", "function call counts"),
                              clEnumValN(GraphRenderer::StatType::MIN, "min",
                                         "minimum function durations"),
                              clEnumValN(GraphRenderer::StatType::MED, "med",
                                         "median function durations"),
                              clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                                         "90th percentile durations"),
                              clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                                         "99th percentile durations"),
                              clEnumValN(GraphRenderer::StatType::MAX, "max",
                                         "maximum function durations"),
                              clEnumValN(GraphRenderer::StatType::SUM, "sum",
                                         "sum of call durations")));
static cl::alias GraphEdgeLabel2("e", cl::aliasopt(GraphEdgeLabel),
                                 cl::desc("Alias for -edge-label"),
                                 cl::sub(GraphC));

static cl::opt<GraphRenderer::StatType> GraphVertexLabel(
    "vertex-label",
    cl::desc("Output graphs with vertices labeled with this field"),
    cl::value_desc("field"), cl::sub(GraphC),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not label Vertices"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphVertexLabel2("v", cl::aliasopt(GraphVertexLabel),
                                   cl::desc("Alias for -edge-label"),
                                   cl::sub(GraphC));

static cl::opt<GraphRenderer::StatType> GraphEdgeColorType(
    "color-edges",
    cl::desc("Output graphs with edge colors determined by this field"),
    cl::value_desc("field"), cl::sub(GraphC),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not color Edges"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphEdgeColorType2("c", cl::aliasopt(GraphEdgeColorType),
                                     cl::desc("Alias for -color-edges"),
                                     cl::sub(GraphC));

static cl::opt<GraphRenderer::StatType> GraphVertexColorType(
    "color-vertices",
    cl::desc("Output graphs with vertex colors determined by this field"),
    cl::value_desc("field"), cl::sub(GraphC),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not color vertices"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphVertexColorType2("b", cl::aliasopt(GraphVertexColorType),
                                       cl::desc("Alias for -edge-label"),
                                       cl::sub(GraphC));

template <class T> T diff(T L, T R) { return std::max(L, R) - std::min(L, R); }

// Updates the statistics for a GraphRenderer::TimeStat
static void updateStat(GraphRenderer::TimeStat &S, int64_t L) {
  S.Count++;
  if (S.Min > L || S.Min == 0)
    S.Min = L;
  if (S.Max < L)
    S.Max = L;
  S.Sum += L;
}

// Evaluates an XRay record and performs accounting on it.
//
// If the record is an ENTER record it pushes the FuncID and TSC onto a
// structure representing the call stack for that function.
// If the record is an EXIT record it checks computes computes the ammount of
// time the function took to complete and then stores that information in an
// edge of the graph. If there is no matching ENTER record the function tries
// to recover by assuming that there were EXIT records which were missed, for
// example caused by tail call elimination and if the option is enabled then
// then tries to recover from this.
//
// This funciton will also error if the records are out of order, as the trace
// is expected to be sorted.
//
// The graph generated has an immaginary root for functions called by no-one at
// FuncId 0.
//
// FIXME: Refactor this and account subcommand to reduce code duplication.
Error GraphRenderer::accountRecord(const XRayRecord &Record) {
  using std::make_error_code;
  using std::errc;
  if (CurrentMaxTSC == 0)
    CurrentMaxTSC = Record.TSC;

  if (Record.TSC < CurrentMaxTSC)
    return make_error<StringError>("Records not in order",
                                   make_error_code(errc::invalid_argument));

  auto &ThreadStack = PerThreadFunctionStack[Record.TId];
  switch (Record.Type) {
  case RecordTypes::ENTER:
  case RecordTypes::ENTER_ARG: {
    if (Record.FuncId != 0 && G.count(Record.FuncId) == 0)
      G[Record.FuncId].SymbolName = FuncIdHelper.SymbolOrNumber(Record.FuncId);
    ThreadStack.push_back({Record.FuncId, Record.TSC});
    break;
  }
  case RecordTypes::EXIT:
  case RecordTypes::TAIL_EXIT: {
    // FIXME: Refactor this and the account subcommand to reduce code
    // duplication
    if (ThreadStack.size() == 0 || ThreadStack.back().FuncId != Record.FuncId) {
      if (!DeduceSiblingCalls)
        return make_error<StringError>("No matching ENTRY record",
                                       make_error_code(errc::invalid_argument));
      auto Parent = std::find_if(
          ThreadStack.rbegin(), ThreadStack.rend(),
          [&](const FunctionAttr &A) { return A.FuncId == Record.FuncId; });
      if (Parent == ThreadStack.rend())
        return make_error<StringError>(
            "No matching Entry record in stack",
            make_error_code(errc::invalid_argument)); // There is no matching
                                                      // Function for this exit.
      while (ThreadStack.back().FuncId != Record.FuncId) {
        TimestampT D = diff(ThreadStack.back().TSC, Record.TSC);
        VertexIdentifier TopFuncId = ThreadStack.back().FuncId;
        ThreadStack.pop_back();
        assert(ThreadStack.size() != 0);
        EdgeIdentifier EI(ThreadStack.back().FuncId, TopFuncId);
        auto &EA = G[EI];
        EA.Timings.push_back(D);
        updateStat(EA.S, D);
        updateStat(G[TopFuncId].S, D);
      }
    }
    uint64_t D = diff(ThreadStack.back().TSC, Record.TSC);
    ThreadStack.pop_back();
    VertexIdentifier VI = ThreadStack.empty() ? 0 : ThreadStack.back().FuncId;
    EdgeIdentifier EI(VI, Record.FuncId);
    auto &EA = G[EI];
    EA.Timings.push_back(D);
    updateStat(EA.S, D);
    updateStat(G[Record.FuncId].S, D);
    break;
  }
  case RecordTypes::CUSTOM_EVENT:
  case RecordTypes::TYPED_EVENT:
    // TODO: Support custom and typed events in the graph processing?
    break;
  }

  return Error::success();
}

template <typename U>
void GraphRenderer::getStats(U begin, U end, GraphRenderer::TimeStat &S) {
  if (begin == end) return;
  std::ptrdiff_t MedianOff = S.Count / 2;
  std::nth_element(begin, begin + MedianOff, end);
  S.Median = *(begin + MedianOff);
  std::ptrdiff_t Pct90Off = (S.Count * 9) / 10;
  std::nth_element(begin, begin + Pct90Off, end);
  S.Pct90 = *(begin + Pct90Off);
  std::ptrdiff_t Pct99Off = (S.Count * 99) / 100;
  std::nth_element(begin, begin + Pct99Off, end);
  S.Pct99 = *(begin + Pct99Off);
}

void GraphRenderer::updateMaxStats(const GraphRenderer::TimeStat &S,
                                   GraphRenderer::TimeStat &M) {
  M.Count = std::max(M.Count, S.Count);
  M.Min = std::max(M.Min, S.Min);
  M.Median = std::max(M.Median, S.Median);
  M.Pct90 = std::max(M.Pct90, S.Pct90);
  M.Pct99 = std::max(M.Pct99, S.Pct99);
  M.Max = std::max(M.Max, S.Max);
  M.Sum = std::max(M.Sum, S.Sum);
}

void GraphRenderer::calculateEdgeStatistics() {
  assert(!G.edges().empty());
  for (auto &E : G.edges()) {
    auto &A = E.second;
    assert(!A.Timings.empty());
    getStats(A.Timings.begin(), A.Timings.end(), A.S);
    updateMaxStats(A.S, G.GraphEdgeMax);
  }
}

void GraphRenderer::calculateVertexStatistics() {
  std::vector<uint64_t> TempTimings;
  for (auto &V : G.vertices()) {
    if (V.first != 0) {
      for (auto &E : G.inEdges(V.first)) {
        auto &A = E.second;
        TempTimings.insert(TempTimings.end(), A.Timings.begin(),
                           A.Timings.end());
      }
      getStats(TempTimings.begin(), TempTimings.end(), G[V.first].S);
      updateMaxStats(G[V.first].S, G.GraphVertexMax);
      TempTimings.clear();
    }
  }
}

// A Helper function for normalizeStatistics which normalises a single
// TimeStat element.
static void normalizeTimeStat(GraphRenderer::TimeStat &S,
                              double CycleFrequency) {
  int64_t OldCount = S.Count;
  S = S / CycleFrequency;
  S.Count = OldCount;
}

// Normalises the statistics in the graph for a given TSC frequency.
void GraphRenderer::normalizeStatistics(double CycleFrequency) {
  for (auto &E : G.edges()) {
    auto &S = E.second.S;
    normalizeTimeStat(S, CycleFrequency);
  }
  for (auto &V : G.vertices()) {
    auto &S = V.second.S;
    normalizeTimeStat(S, CycleFrequency);
  }

  normalizeTimeStat(G.GraphEdgeMax, CycleFrequency);
  normalizeTimeStat(G.GraphVertexMax, CycleFrequency);
}

// Returns a string containing the value of statistic field T
std::string
GraphRenderer::TimeStat::getString(GraphRenderer::StatType T) const {
  std::string St;
  raw_string_ostream S{St};
  double TimeStat::*DoubleStatPtrs[] = {&TimeStat::Min,   &TimeStat::Median,
                                        &TimeStat::Pct90, &TimeStat::Pct99,
                                        &TimeStat::Max,   &TimeStat::Sum};
  switch (T) {
  case GraphRenderer::StatType::NONE:
    break;
  case GraphRenderer::StatType::COUNT:
    S << Count;
    break;
  default:
    S << (*this).*
             DoubleStatPtrs[static_cast<int>(T) -
                            static_cast<int>(GraphRenderer::StatType::MIN)];
    break;
  }
  return S.str();
}

// Returns the quotient between the property T of this and another TimeStat as
// a double
double GraphRenderer::TimeStat::getDouble(StatType T) const {
  double retval = 0;
  double TimeStat::*DoubleStatPtrs[] = {&TimeStat::Min,   &TimeStat::Median,
                                        &TimeStat::Pct90, &TimeStat::Pct99,
                                        &TimeStat::Max,   &TimeStat::Sum};
  switch (T) {
  case GraphRenderer::StatType::NONE:
    retval = 0.0;
    break;
  case GraphRenderer::StatType::COUNT:
    retval = static_cast<double>(Count);
    break;
  default:
    retval =
        (*this).*DoubleStatPtrs[static_cast<int>(T) -
                                static_cast<int>(GraphRenderer::StatType::MIN)];
    break;
  }
  return retval;
}

// Outputs a DOT format version of the Graph embedded in the GraphRenderer
// object on OS. It does this in the expected way by itterating
// through all edges then vertices and then outputting them and their
// annotations.
//
// FIXME: output more information, better presented.
void GraphRenderer::exportGraphAsDOT(raw_ostream &OS, StatType ET, StatType EC,
                                     StatType VT, StatType VC) {
  OS << "digraph xray {\n";

  if (VT != StatType::NONE)
    OS << "node [shape=record];\n";

  for (const auto &E : G.edges()) {
    const auto &S = E.second.S;
    OS << "F" << E.first.first << " -> "
       << "F" << E.first.second << " [label=\"" << S.getString(ET) << "\"";
    if (EC != StatType::NONE)
      OS << " color=\""
         << CHelper.getColorString(
                std::sqrt(S.getDouble(EC) / G.GraphEdgeMax.getDouble(EC)))
         << "\"";
    OS << "];\n";
  }

  for (const auto &V : G.vertices()) {
    const auto &VA = V.second;
    if (V.first == 0)
      continue;
    OS << "F" << V.first << " [label=\"" << (VT != StatType::NONE ? "{" : "")
       << (VA.SymbolName.size() > 40 ? VA.SymbolName.substr(0, 40) + "..."
                                     : VA.SymbolName);
    if (VT != StatType::NONE)
      OS << "|" << VA.S.getString(VT) << "}\"";
    else
      OS << "\"";
    if (VC != StatType::NONE)
      OS << " color=\""
         << CHelper.getColorString(
                std::sqrt(VA.S.getDouble(VC) / G.GraphVertexMax.getDouble(VC)))
         << "\"";
    OS << "];\n";
  }
  OS << "}\n";
}

Expected<GraphRenderer> GraphRenderer::Factory::getGraphRenderer() {
  InstrumentationMap Map;
  if (!GraphInstrMap.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(GraphInstrMap);
    if (!InstrumentationMapOrError)
      return joinErrors(
          make_error<StringError>(
              Twine("Cannot open instrumentation map '") + GraphInstrMap + "'",
              std::make_error_code(std::errc::invalid_argument)),
          InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  const auto &FunctionAddresses = Map.getFunctionAddresses();

  symbolize::LLVMSymbolizer::Options Opts(
      symbolize::FunctionNameKind::LinkageName, true, true, false, "");
  symbolize::LLVMSymbolizer Symbolizer(Opts);
  const auto &Header = Trace.getFileHeader();

  llvm::xray::FuncIdConversionHelper FuncIdHelper(InstrMap, Symbolizer,
                                                  FunctionAddresses);

  xray::GraphRenderer GR(FuncIdHelper, DeduceSiblingCalls);
  for (const auto &Record : Trace) {
    auto E = GR.accountRecord(Record);
    if (!E)
      continue;

    for (const auto &ThreadStack : GR.getPerThreadFunctionStack()) {
      errs() << "Thread ID: " << ThreadStack.first << "\n";
      auto Level = ThreadStack.second.size();
      for (const auto &Entry : llvm::reverse(ThreadStack.second))
        errs() << "#" << Level-- << "\t"
               << FuncIdHelper.SymbolOrNumber(Entry.FuncId) << '\n';
    }

    if (!GraphKeepGoing)
      return joinErrors(make_error<StringError>(
                            "Error encountered generating the call graph.",
                            std::make_error_code(std::errc::invalid_argument)),
                        std::move(E));

    handleAllErrors(std::move(E),
                    [&](const ErrorInfoBase &E) { E.log(errs()); });
  }

  GR.G.GraphEdgeMax = {};
  GR.G.GraphVertexMax = {};
  GR.calculateEdgeStatistics();
  GR.calculateVertexStatistics();

  if (Header.CycleFrequency)
    GR.normalizeStatistics(Header.CycleFrequency);

  return GR;
}

// Here we register and implement the llvm-xray graph subcommand.
// The bulk of this code reads in the options, opens the required files, uses
// those files to create a context for analysing the xray trace, then there is a
// short loop which actually analyses the trace, generates the graph and then
// outputs it as a DOT.
//
// FIXME: include additional filtering and annalysis passes to provide more
// specific useful information.
static CommandRegistration Unused(&GraphC, []() -> Error {
  GraphRenderer::Factory F;

  F.KeepGoing = GraphKeepGoing;
  F.DeduceSiblingCalls = GraphDeduceSiblingCalls;
  F.InstrMap = GraphInstrMap;

  auto TraceOrErr = loadTraceFile(GraphInput, true);

  if (!TraceOrErr)
    return make_error<StringError>(
        Twine("Failed loading input file '") + GraphInput + "'",
        make_error_code(llvm::errc::invalid_argument));

  F.Trace = std::move(*TraceOrErr);
  auto GROrError = F.getGraphRenderer();
  if (!GROrError)
    return GROrError.takeError();
  auto &GR = *GROrError;

  std::error_code EC;
  raw_fd_ostream OS(GraphOutput, EC, sys::fs::OpenFlags::F_Text);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + GraphOutput + "' for writing.", EC);

  GR.exportGraphAsDOT(OS, GraphEdgeLabel, GraphEdgeColorType, GraphVertexLabel,
                      GraphVertexColorType);
  return Error::success();
});
