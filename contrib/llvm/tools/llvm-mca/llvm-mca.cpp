//===-- llvm-mca.cpp - Machine Code Analyzer -------------------*- C++ -* -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility is a simple driver that allows static performance analysis on
// machine code similarly to how IACA (Intel Architecture Code Analyzer) works.
//
//   llvm-mca [options] <file-name>
//      -march <type>
//      -mcpu <cpu>
//      -o <file>
//
// The target defaults to the host target.
// The cpu defaults to the 'native' host cpu.
// The output defaults to standard output.
//
//===----------------------------------------------------------------------===//

#include "CodeRegion.h"
#include "CodeRegionGenerator.h"
#include "PipelinePrinter.h"
#include "Views/DispatchStatistics.h"
#include "Views/InstructionInfoView.h"
#include "Views/RegisterFileStatistics.h"
#include "Views/ResourcePressureView.h"
#include "Views/RetireControlUnitStatistics.h"
#include "Views/SchedulerStatistics.h"
#include "Views/SummaryView.h"
#include "Views/TimelineView.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Stages/InstructionTables.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;

static cl::OptionCategory ToolOptions("Tool Options");
static cl::OptionCategory ViewOptions("View Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::cat(ToolOptions), cl::init("-"));

static cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"),
                                           cl::init("-"), cl::cat(ToolOptions),
                                           cl::value_desc("filename"));

static cl::opt<std::string>
    ArchName("march", cl::desc("Target architecture. "
                               "See -version for available targets"),
             cl::cat(ToolOptions));

static cl::opt<std::string>
    TripleName("mtriple",
               cl::desc("Target triple. See -version for available targets"),
               cl::cat(ToolOptions));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::cat(ToolOptions), cl::init("native"));

static cl::opt<int>
    OutputAsmVariant("output-asm-variant",
                     cl::desc("Syntax variant to use for output printing"),
                     cl::cat(ToolOptions), cl::init(-1));

static cl::opt<unsigned> Iterations("iterations",
                                    cl::desc("Number of iterations to run"),
                                    cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    DispatchWidth("dispatch", cl::desc("Override the processor dispatch width"),
                  cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    RegisterFileSize("register-file-size",
                     cl::desc("Maximum number of physical registers which can "
                              "be used for register mappings"),
                     cl::cat(ToolOptions), cl::init(0));

static cl::opt<bool>
    PrintRegisterFileStats("register-file-stats",
                           cl::desc("Print register file statistics"),
                           cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> PrintDispatchStats("dispatch-stats",
                                        cl::desc("Print dispatch statistics"),
                                        cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool>
    PrintSummaryView("summary-view", cl::Hidden,
                     cl::desc("Print summary view (enabled by default)"),
                     cl::cat(ViewOptions), cl::init(true));

static cl::opt<bool> PrintSchedulerStats("scheduler-stats",
                                         cl::desc("Print scheduler statistics"),
                                         cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool>
    PrintRetireStats("retire-stats",
                     cl::desc("Print retire control unit statistics"),
                     cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> PrintResourcePressureView(
    "resource-pressure",
    cl::desc("Print the resource pressure view (enabled by default)"),
    cl::cat(ViewOptions), cl::init(true));

static cl::opt<bool> PrintTimelineView("timeline",
                                       cl::desc("Print the timeline view"),
                                       cl::cat(ViewOptions), cl::init(false));

static cl::opt<unsigned> TimelineMaxIterations(
    "timeline-max-iterations",
    cl::desc("Maximum number of iterations to print in timeline view"),
    cl::cat(ViewOptions), cl::init(0));

static cl::opt<unsigned> TimelineMaxCycles(
    "timeline-max-cycles",
    cl::desc(
        "Maximum number of cycles in the timeline view. Defaults to 80 cycles"),
    cl::cat(ViewOptions), cl::init(80));

static cl::opt<bool>
    AssumeNoAlias("noalias",
                  cl::desc("If set, assume that loads and stores do not alias"),
                  cl::cat(ToolOptions), cl::init(true));

static cl::opt<unsigned> LoadQueueSize("lqueue",
                                       cl::desc("Size of the load queue"),
                                       cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned> StoreQueueSize("squeue",
                                        cl::desc("Size of the store queue"),
                                        cl::cat(ToolOptions), cl::init(0));

static cl::opt<bool>
    PrintInstructionTables("instruction-tables",
                           cl::desc("Print instruction tables"),
                           cl::cat(ToolOptions), cl::init(false));

static cl::opt<bool> PrintInstructionInfoView(
    "instruction-info",
    cl::desc("Print the instruction info view (enabled by default)"),
    cl::cat(ViewOptions), cl::init(true));

static cl::opt<bool> EnableAllStats("all-stats",
                                    cl::desc("Print all hardware statistics"),
                                    cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool>
    EnableAllViews("all-views",
                   cl::desc("Print all views including hardware statistics"),
                   cl::cat(ViewOptions), cl::init(false));

namespace {

const Target *getTarget(const char *ProgName) {
  if (TripleName.empty())
    TripleName = Triple::normalize(sys::getDefaultTargetTriple());
  Triple TheTriple(TripleName);

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(ArchName, TheTriple, Error);
  if (!TheTarget) {
    errs() << ProgName << ": " << Error;
    return nullptr;
  }

  // Return the found target.
  return TheTarget;
}

ErrorOr<std::unique_ptr<ToolOutputFile>> getOutputStream() {
  if (OutputFilename == "")
    OutputFilename = "-";
  std::error_code EC;
  auto Out =
      llvm::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::F_None);
  if (!EC)
    return std::move(Out);
  return EC;
}
} // end of anonymous namespace

static void processOptionImpl(cl::opt<bool> &O, const cl::opt<bool> &Default) {
  if (!O.getNumOccurrences() || O.getPosition() < Default.getPosition())
    O = Default.getValue();
}

static void processViewOptions() {
  if (!EnableAllViews.getNumOccurrences() &&
      !EnableAllStats.getNumOccurrences())
    return;

  if (EnableAllViews.getNumOccurrences()) {
    processOptionImpl(PrintSummaryView, EnableAllViews);
    processOptionImpl(PrintResourcePressureView, EnableAllViews);
    processOptionImpl(PrintTimelineView, EnableAllViews);
    processOptionImpl(PrintInstructionInfoView, EnableAllViews);
  }

  const cl::opt<bool> &Default =
      EnableAllViews.getPosition() < EnableAllStats.getPosition()
          ? EnableAllStats
          : EnableAllViews;
  processOptionImpl(PrintRegisterFileStats, Default);
  processOptionImpl(PrintDispatchStats, Default);
  processOptionImpl(PrintSchedulerStats, Default);
  processOptionImpl(PrintRetireStats, Default);
}

// Returns true on success.
static bool runPipeline(mca::Pipeline &P) {
  // Handle pipeline errors here.
  Expected<unsigned> Cycles = P.run();
  if (!Cycles) {
    WithColor::error() << toString(Cycles.takeError());
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();

  // Enable printing of available targets when flag --version is specified.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  cl::HideUnrelatedOptions({&ToolOptions, &ViewOptions});

  // Parse flags and initialize target options.
  cl::ParseCommandLineOptions(argc, argv,
                              "llvm machine code performance analyzer.\n");

  // Get the target from the triple. If a triple is not specified, then select
  // the default triple for the host. If the triple doesn't correspond to any
  // registered target, then exit with an error message.
  const char *ProgName = argv[0];
  const Target *TheTarget = getTarget(ProgName);
  if (!TheTarget)
    return 1;

  // GetTarget() may replaced TripleName with a default triple.
  // For safety, reconstruct the Triple object.
  Triple TheTriple(TripleName);

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferPtr =
      MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = BufferPtr.getError()) {
    WithColor::error() << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  // Apply overrides to llvm-mca specific options.
  processViewOptions();

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(*BufferPtr), SMLoc());

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  assert(MRI && "Unable to create target register info!");

  std::unique_ptr<MCAsmInfo> MAI(TheTarget->createMCAsmInfo(*MRI, TripleName));
  assert(MAI && "Unable to create target asm info!");

  MCObjectFileInfo MOFI;
  MCContext Ctx(MAI.get(), MRI.get(), &MOFI, &SrcMgr);
  MOFI.InitMCObjectFileInfo(TheTriple, /* PIC= */ false, Ctx);

  std::unique_ptr<buffer_ostream> BOS;

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());

  std::unique_ptr<MCInstrAnalysis> MCIA(
      TheTarget->createMCInstrAnalysis(MCII.get()));

  if (!MCPU.compare("native"))
    MCPU = llvm::sys::getHostCPUName();

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, MCPU, /* FeaturesStr */ ""));
  if (!STI->isCPUStringValid(MCPU))
    return 1;

  if (!PrintInstructionTables && !STI->getSchedModel().isOutOfOrder()) {
    WithColor::error() << "please specify an out-of-order cpu. '" << MCPU
                       << "' is an in-order cpu.\n";
    return 1;
  }

  if (!STI->getSchedModel().hasInstrSchedModel()) {
    WithColor::error()
        << "unable to find instruction-level scheduling information for"
        << " target triple '" << TheTriple.normalize() << "' and cpu '" << MCPU
        << "'.\n";

    if (STI->getSchedModel().InstrItineraries)
      WithColor::note()
          << "cpu '" << MCPU << "' provides itineraries. However, "
          << "instruction itineraries are currently unsupported.\n";
    return 1;
  }

  // Parse the input and create CodeRegions that llvm-mca can analyze.
  mca::AsmCodeRegionGenerator CRG(*TheTarget, SrcMgr, Ctx, *MAI, *STI, *MCII);
  Expected<const mca::CodeRegions &> RegionsOrErr = CRG.parseCodeRegions();
  if (!RegionsOrErr) {
    if (auto Err =
            handleErrors(RegionsOrErr.takeError(), [](const StringError &E) {
              WithColor::error() << E.getMessage() << '\n';
            })) {
      // Default case.
      WithColor::error() << toString(std::move(Err)) << '\n';
    }
    return 1;
  }
  const mca::CodeRegions &Regions = *RegionsOrErr;
  if (Regions.empty()) {
    WithColor::error() << "no assembly instructions found.\n";
    return 1;
  }

  // Now initialize the output file.
  auto OF = getOutputStream();
  if (std::error_code EC = OF.getError()) {
    WithColor::error() << EC.message() << '\n';
    return 1;
  }

  unsigned AssemblerDialect = CRG.getAssemblerDialect();
  if (OutputAsmVariant >= 0)
    AssemblerDialect = static_cast<unsigned>(OutputAsmVariant);
  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      Triple(TripleName), AssemblerDialect, *MAI, *MCII, *MRI));
  if (!IP) {
    WithColor::error()
        << "unable to create instruction printer for target triple '"
        << TheTriple.normalize() << "' with assembly variant "
        << AssemblerDialect << ".\n";
    return 1;
  }

  std::unique_ptr<ToolOutputFile> TOF = std::move(*OF);

  const MCSchedModel &SM = STI->getSchedModel();

  unsigned Width = SM.IssueWidth;
  if (DispatchWidth)
    Width = DispatchWidth;

  // Create an instruction builder.
  mca::InstrBuilder IB(*STI, *MCII, *MRI, MCIA.get());

  // Create a context to control ownership of the pipeline hardware.
  mca::Context MCA(*MRI, *STI);

  mca::PipelineOptions PO(Width, RegisterFileSize, LoadQueueSize,
                          StoreQueueSize, AssumeNoAlias);

  // Number each region in the sequence.
  unsigned RegionIdx = 0;

  for (const std::unique_ptr<mca::CodeRegion> &Region : Regions) {
    // Skip empty code regions.
    if (Region->empty())
      continue;

    // Don't print the header of this region if it is the default region, and
    // it doesn't have an end location.
    if (Region->startLoc().isValid() || Region->endLoc().isValid()) {
      TOF->os() << "\n[" << RegionIdx++ << "] Code Region";
      StringRef Desc = Region->getDescription();
      if (!Desc.empty())
        TOF->os() << " - " << Desc;
      TOF->os() << "\n\n";
    }

    // Lower the MCInst sequence into an mca::Instruction sequence.
    ArrayRef<MCInst> Insts = Region->getInstructions();
    std::vector<std::unique_ptr<mca::Instruction>> LoweredSequence;
    for (const MCInst &MCI : Insts) {
      Expected<std::unique_ptr<mca::Instruction>> Inst =
          IB.createInstruction(MCI);
      if (!Inst) {
        if (auto NewE = handleErrors(
                Inst.takeError(),
                [&IP, &STI](const mca::InstructionError<MCInst> &IE) {
                  std::string InstructionStr;
                  raw_string_ostream SS(InstructionStr);
                  WithColor::error() << IE.Message << '\n';
                  IP->printInst(&IE.Inst, SS, "", *STI);
                  SS.flush();
                  WithColor::note() << "instruction: " << InstructionStr
                                    << '\n';
                })) {
          // Default case.
          WithColor::error() << toString(std::move(NewE));
        }
        return 1;
      }

      LoweredSequence.emplace_back(std::move(Inst.get()));
    }

    mca::SourceMgr S(LoweredSequence, PrintInstructionTables ? 1 : Iterations);

    if (PrintInstructionTables) {
      //  Create a pipeline, stages, and a printer.
      auto P = llvm::make_unique<mca::Pipeline>();
      P->appendStage(llvm::make_unique<mca::EntryStage>(S));
      P->appendStage(llvm::make_unique<mca::InstructionTables>(SM));
      mca::PipelinePrinter Printer(*P);

      // Create the views for this pipeline, execute, and emit a report.
      if (PrintInstructionInfoView) {
        Printer.addView(llvm::make_unique<mca::InstructionInfoView>(
            *STI, *MCII, Insts, *IP));
      }
      Printer.addView(
          llvm::make_unique<mca::ResourcePressureView>(*STI, *IP, Insts));

      if (!runPipeline(*P))
        return 1;

      Printer.printReport(TOF->os());
      continue;
    }

    // Create a basic pipeline simulating an out-of-order backend.
    auto P = MCA.createDefaultPipeline(PO, IB, S);
    mca::PipelinePrinter Printer(*P);

    if (PrintSummaryView)
      Printer.addView(llvm::make_unique<mca::SummaryView>(SM, Insts, Width));

    if (PrintInstructionInfoView)
      Printer.addView(
          llvm::make_unique<mca::InstructionInfoView>(*STI, *MCII, Insts, *IP));

    if (PrintDispatchStats)
      Printer.addView(llvm::make_unique<mca::DispatchStatistics>());

    if (PrintSchedulerStats)
      Printer.addView(llvm::make_unique<mca::SchedulerStatistics>(*STI));

    if (PrintRetireStats)
      Printer.addView(llvm::make_unique<mca::RetireControlUnitStatistics>(SM));

    if (PrintRegisterFileStats)
      Printer.addView(llvm::make_unique<mca::RegisterFileStatistics>(*STI));

    if (PrintResourcePressureView)
      Printer.addView(
          llvm::make_unique<mca::ResourcePressureView>(*STI, *IP, Insts));

    if (PrintTimelineView) {
      unsigned TimelineIterations =
          TimelineMaxIterations ? TimelineMaxIterations : 10;
      Printer.addView(llvm::make_unique<mca::TimelineView>(
          *STI, *IP, Insts, std::min(TimelineIterations, S.getNumIterations()),
          TimelineMaxCycles));
    }

    if (!runPipeline(*P))
      return 1;

    Printer.printReport(TOF->os());

    // Clear the InstrBuilder internal state in preparation for another round.
    IB.clear();
  }

  TOF->keep();
  return 0;
}
