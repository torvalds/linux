//===-- llvm-rtdyld.cpp - MCJIT Testing Tool ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a testing tool for use with the MC-JIT LLVM components.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/ExecutionEngine/RuntimeDyldChecker.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <list>

using namespace llvm;
using namespace llvm::object;

static cl::list<std::string>
InputFileList(cl::Positional, cl::ZeroOrMore,
              cl::desc("<input files>"));

enum ActionType {
  AC_Execute,
  AC_PrintObjectLineInfo,
  AC_PrintLineInfo,
  AC_PrintDebugLineInfo,
  AC_Verify
};

static cl::opt<ActionType>
Action(cl::desc("Action to perform:"),
       cl::init(AC_Execute),
       cl::values(clEnumValN(AC_Execute, "execute",
                             "Load, link, and execute the inputs."),
                  clEnumValN(AC_PrintLineInfo, "printline",
                             "Load, link, and print line information for each function."),
                  clEnumValN(AC_PrintDebugLineInfo, "printdebugline",
                             "Load, link, and print line information for each function using the debug object"),
                  clEnumValN(AC_PrintObjectLineInfo, "printobjline",
                             "Like -printlineinfo but does not load the object first"),
                  clEnumValN(AC_Verify, "verify",
                             "Load, link and verify the resulting memory image.")));

static cl::opt<std::string>
EntryPoint("entry",
           cl::desc("Function to call as entry point."),
           cl::init("_main"));

static cl::list<std::string>
Dylibs("dylib",
       cl::desc("Add library."),
       cl::ZeroOrMore);

static cl::opt<std::string>
TripleName("triple", cl::desc("Target triple for disassembler"));

static cl::opt<std::string>
MCPU("mcpu",
     cl::desc("Target a specific cpu type (-mcpu=help for details)"),
     cl::value_desc("cpu-name"),
     cl::init(""));

static cl::list<std::string>
CheckFiles("check",
           cl::desc("File containing RuntimeDyld verifier checks."),
           cl::ZeroOrMore);

// Tracking BUG: 19665
// http://llvm.org/bugs/show_bug.cgi?id=19665
//
// Do not change these options to cl::opt<uint64_t> since this silently breaks
// argument parsing.
static cl::opt<unsigned long long>
PreallocMemory("preallocate",
              cl::desc("Allocate memory upfront rather than on-demand"),
              cl::init(0));

static cl::opt<unsigned long long>
TargetAddrStart("target-addr-start",
                cl::desc("For -verify only: start of phony target address "
                         "range."),
                cl::init(4096), // Start at "page 1" - no allocating at "null".
                cl::Hidden);

static cl::opt<unsigned long long>
TargetAddrEnd("target-addr-end",
              cl::desc("For -verify only: end of phony target address range."),
              cl::init(~0ULL),
              cl::Hidden);

static cl::opt<unsigned long long>
TargetSectionSep("target-section-sep",
                 cl::desc("For -verify only: Separation between sections in "
                          "phony target address space."),
                 cl::init(0),
                 cl::Hidden);

static cl::list<std::string>
SpecificSectionMappings("map-section",
                        cl::desc("For -verify only: Map a section to a "
                                 "specific address."),
                        cl::ZeroOrMore,
                        cl::Hidden);

static cl::list<std::string>
DummySymbolMappings("dummy-extern",
                    cl::desc("For -verify only: Inject a symbol into the extern "
                             "symbol table."),
                    cl::ZeroOrMore,
                    cl::Hidden);

static cl::opt<bool>
PrintAllocationRequests("print-alloc-requests",
                        cl::desc("Print allocation requests made to the memory "
                                 "manager by RuntimeDyld"),
                        cl::Hidden);

/* *** */

// A trivial memory manager that doesn't do anything fancy, just uses the
// support library allocation routines directly.
class TrivialMemoryManager : public RTDyldMemoryManager {
public:
  SmallVector<sys::MemoryBlock, 16> FunctionMemory;
  SmallVector<sys::MemoryBlock, 16> DataMemory;

  uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID,
                               StringRef SectionName) override;
  uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID, StringRef SectionName,
                               bool IsReadOnly) override;

  void *getPointerToNamedFunction(const std::string &Name,
                                  bool AbortOnFailure = true) override {
    return nullptr;
  }

  bool finalizeMemory(std::string *ErrMsg) override { return false; }

  void addDummySymbol(const std::string &Name, uint64_t Addr) {
    DummyExterns[Name] = Addr;
  }

  JITSymbol findSymbol(const std::string &Name) override {
    auto I = DummyExterns.find(Name);

    if (I != DummyExterns.end())
      return JITSymbol(I->second, JITSymbolFlags::Exported);

    return RTDyldMemoryManager::findSymbol(Name);
  }

  void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                        size_t Size) override {}
  void deregisterEHFrames() override {}

  void preallocateSlab(uint64_t Size) {
    std::error_code EC;
    sys::MemoryBlock MB =
      sys::Memory::allocateMappedMemory(Size, nullptr,
                                        sys::Memory::MF_READ |
                                        sys::Memory::MF_WRITE,
                                        EC);
    if (!MB.base())
      report_fatal_error("Can't allocate enough memory: " + EC.message());

    PreallocSlab = MB;
    UsePreallocation = true;
    SlabSize = Size;
  }

  uint8_t *allocateFromSlab(uintptr_t Size, unsigned Alignment, bool isCode) {
    Size = alignTo(Size, Alignment);
    if (CurrentSlabOffset + Size > SlabSize)
      report_fatal_error("Can't allocate enough memory. Tune --preallocate");

    uintptr_t OldSlabOffset = CurrentSlabOffset;
    sys::MemoryBlock MB((void *)OldSlabOffset, Size);
    if (isCode)
      FunctionMemory.push_back(MB);
    else
      DataMemory.push_back(MB);
    CurrentSlabOffset += Size;
    return (uint8_t*)OldSlabOffset;
  }

private:
  std::map<std::string, uint64_t> DummyExterns;
  sys::MemoryBlock PreallocSlab;
  bool UsePreallocation = false;
  uintptr_t SlabSize = 0;
  uintptr_t CurrentSlabOffset = 0;
};

uint8_t *TrivialMemoryManager::allocateCodeSection(uintptr_t Size,
                                                   unsigned Alignment,
                                                   unsigned SectionID,
                                                   StringRef SectionName) {
  if (PrintAllocationRequests)
    outs() << "allocateCodeSection(Size = " << Size << ", Alignment = "
           << Alignment << ", SectionName = " << SectionName << ")\n";

  if (UsePreallocation)
    return allocateFromSlab(Size, Alignment, true /* isCode */);

  std::error_code EC;
  sys::MemoryBlock MB =
    sys::Memory::allocateMappedMemory(Size, nullptr,
                                      sys::Memory::MF_READ |
                                      sys::Memory::MF_WRITE,
                                      EC);
  if (!MB.base())
    report_fatal_error("MemoryManager allocation failed: " + EC.message());
  FunctionMemory.push_back(MB);
  return (uint8_t*)MB.base();
}

uint8_t *TrivialMemoryManager::allocateDataSection(uintptr_t Size,
                                                   unsigned Alignment,
                                                   unsigned SectionID,
                                                   StringRef SectionName,
                                                   bool IsReadOnly) {
  if (PrintAllocationRequests)
    outs() << "allocateDataSection(Size = " << Size << ", Alignment = "
           << Alignment << ", SectionName = " << SectionName << ")\n";

  if (UsePreallocation)
    return allocateFromSlab(Size, Alignment, false /* isCode */);

  std::error_code EC;
  sys::MemoryBlock MB =
    sys::Memory::allocateMappedMemory(Size, nullptr,
                                      sys::Memory::MF_READ |
                                      sys::Memory::MF_WRITE,
                                      EC);
  if (!MB.base())
    report_fatal_error("MemoryManager allocation failed: " + EC.message());
  DataMemory.push_back(MB);
  return (uint8_t*)MB.base();
}

static const char *ProgramName;

static void ErrorAndExit(const Twine &Msg) {
  errs() << ProgramName << ": error: " << Msg << "\n";
  exit(1);
}

static void loadDylibs() {
  for (const std::string &Dylib : Dylibs) {
    if (!sys::fs::is_regular_file(Dylib))
      report_fatal_error("Dylib not found: '" + Dylib + "'.");
    std::string ErrMsg;
    if (sys::DynamicLibrary::LoadLibraryPermanently(Dylib.c_str(), &ErrMsg))
      report_fatal_error("Error loading '" + Dylib + "': " + ErrMsg);
  }
}

/* *** */

static int printLineInfoForInput(bool LoadObjects, bool UseDebugObj) {
  assert(LoadObjects || !UseDebugObj);

  // Load any dylibs requested on the command line.
  loadDylibs();

  // If we don't have any input files, read from stdin.
  if (!InputFileList.size())
    InputFileList.push_back("-");
  for (auto &File : InputFileList) {
    // Instantiate a dynamic linker.
    TrivialMemoryManager MemMgr;
    RuntimeDyld Dyld(MemMgr, MemMgr);

    // Load the input memory buffer.

    ErrorOr<std::unique_ptr<MemoryBuffer>> InputBuffer =
        MemoryBuffer::getFileOrSTDIN(File);
    if (std::error_code EC = InputBuffer.getError())
      ErrorAndExit("unable to read input: '" + EC.message() + "'");

    Expected<std::unique_ptr<ObjectFile>> MaybeObj(
      ObjectFile::createObjectFile((*InputBuffer)->getMemBufferRef()));

    if (!MaybeObj) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      logAllUnhandledErrors(MaybeObj.takeError(), OS);
      OS.flush();
      ErrorAndExit("unable to create object file: '" + Buf + "'");
    }

    ObjectFile &Obj = **MaybeObj;

    OwningBinary<ObjectFile> DebugObj;
    std::unique_ptr<RuntimeDyld::LoadedObjectInfo> LoadedObjInfo = nullptr;
    ObjectFile *SymbolObj = &Obj;
    if (LoadObjects) {
      // Load the object file
      LoadedObjInfo =
        Dyld.loadObject(Obj);

      if (Dyld.hasError())
        ErrorAndExit(Dyld.getErrorString());

      // Resolve all the relocations we can.
      Dyld.resolveRelocations();

      if (UseDebugObj) {
        DebugObj = LoadedObjInfo->getObjectForDebug(Obj);
        SymbolObj = DebugObj.getBinary();
        LoadedObjInfo.reset();
      }
    }

    std::unique_ptr<DIContext> Context =
        DWARFContext::create(*SymbolObj, LoadedObjInfo.get());

    std::vector<std::pair<SymbolRef, uint64_t>> SymAddr =
        object::computeSymbolSizes(*SymbolObj);

    // Use symbol info to iterate functions in the object.
    for (const auto &P : SymAddr) {
      object::SymbolRef Sym = P.first;
      Expected<SymbolRef::Type> TypeOrErr = Sym.getType();
      if (!TypeOrErr) {
        // TODO: Actually report errors helpfully.
        consumeError(TypeOrErr.takeError());
        continue;
      }
      SymbolRef::Type Type = *TypeOrErr;
      if (Type == object::SymbolRef::ST_Function) {
        Expected<StringRef> Name = Sym.getName();
        if (!Name) {
          // TODO: Actually report errors helpfully.
          consumeError(Name.takeError());
          continue;
        }
        Expected<uint64_t> AddrOrErr = Sym.getAddress();
        if (!AddrOrErr) {
          // TODO: Actually report errors helpfully.
          consumeError(AddrOrErr.takeError());
          continue;
        }
        uint64_t Addr = *AddrOrErr;

        uint64_t Size = P.second;
        // If we're not using the debug object, compute the address of the
        // symbol in memory (rather than that in the unrelocated object file)
        // and use that to query the DWARFContext.
        if (!UseDebugObj && LoadObjects) {
          auto SecOrErr = Sym.getSection();
          if (!SecOrErr) {
            // TODO: Actually report errors helpfully.
            consumeError(SecOrErr.takeError());
            continue;
          }
          object::section_iterator Sec = *SecOrErr;
          StringRef SecName;
          Sec->getName(SecName);
          uint64_t SectionLoadAddress =
            LoadedObjInfo->getSectionLoadAddress(*Sec);
          if (SectionLoadAddress != 0)
            Addr += SectionLoadAddress - Sec->getAddress();
        }

        outs() << "Function: " << *Name << ", Size = " << Size
               << ", Addr = " << Addr << "\n";

        DILineInfoTable Lines = Context->getLineInfoForAddressRange(Addr, Size);
        for (auto &D : Lines) {
          outs() << "  Line info @ " << D.first - Addr << ": "
                 << D.second.FileName << ", line:" << D.second.Line << "\n";
        }
      }
    }
  }

  return 0;
}

static void doPreallocation(TrivialMemoryManager &MemMgr) {
  // Allocate a slab of memory upfront, if required. This is used if
  // we want to test small code models.
  if (static_cast<intptr_t>(PreallocMemory) < 0)
    report_fatal_error("Pre-allocated bytes of memory must be a positive integer.");

  // FIXME: Limit the amount of memory that can be preallocated?
  if (PreallocMemory != 0)
    MemMgr.preallocateSlab(PreallocMemory);
}

static int executeInput() {
  // Load any dylibs requested on the command line.
  loadDylibs();

  // Instantiate a dynamic linker.
  TrivialMemoryManager MemMgr;
  doPreallocation(MemMgr);
  RuntimeDyld Dyld(MemMgr, MemMgr);

  // If we don't have any input files, read from stdin.
  if (!InputFileList.size())
    InputFileList.push_back("-");
  for (auto &File : InputFileList) {
    // Load the input memory buffer.
    ErrorOr<std::unique_ptr<MemoryBuffer>> InputBuffer =
        MemoryBuffer::getFileOrSTDIN(File);
    if (std::error_code EC = InputBuffer.getError())
      ErrorAndExit("unable to read input: '" + EC.message() + "'");
    Expected<std::unique_ptr<ObjectFile>> MaybeObj(
      ObjectFile::createObjectFile((*InputBuffer)->getMemBufferRef()));

    if (!MaybeObj) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      logAllUnhandledErrors(MaybeObj.takeError(), OS);
      OS.flush();
      ErrorAndExit("unable to create object file: '" + Buf + "'");
    }

    ObjectFile &Obj = **MaybeObj;

    // Load the object file
    Dyld.loadObject(Obj);
    if (Dyld.hasError()) {
      ErrorAndExit(Dyld.getErrorString());
    }
  }

  // Resove all the relocations we can.
  // FIXME: Error out if there are unresolved relocations.
  Dyld.resolveRelocations();

  // Get the address of the entry point (_main by default).
  void *MainAddress = Dyld.getSymbolLocalAddress(EntryPoint);
  if (!MainAddress)
    ErrorAndExit("no definition for '" + EntryPoint + "'");

  // Invalidate the instruction cache for each loaded function.
  for (auto &FM : MemMgr.FunctionMemory) {

    // Make sure the memory is executable.
    // setExecutable will call InvalidateInstructionCache.
    if (auto EC = sys::Memory::protectMappedMemory(FM,
                                                   sys::Memory::MF_READ |
                                                   sys::Memory::MF_EXEC))
      ErrorAndExit("unable to mark function executable: '" + EC.message() +
                   "'");
  }

  // Dispatch to _main().
  errs() << "loaded '" << EntryPoint << "' at: " << (void*)MainAddress << "\n";

  int (*Main)(int, const char**) =
    (int(*)(int,const char**)) uintptr_t(MainAddress);
  const char **Argv = new const char*[2];
  // Use the name of the first input object module as argv[0] for the target.
  Argv[0] = InputFileList[0].c_str();
  Argv[1] = nullptr;
  return Main(1, Argv);
}

static int checkAllExpressions(RuntimeDyldChecker &Checker) {
  for (const auto& CheckerFileName : CheckFiles) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> CheckerFileBuf =
        MemoryBuffer::getFileOrSTDIN(CheckerFileName);
    if (std::error_code EC = CheckerFileBuf.getError())
      ErrorAndExit("unable to read input '" + CheckerFileName + "': " +
                   EC.message());

    if (!Checker.checkAllRulesInBuffer("# rtdyld-check:",
                                       CheckerFileBuf.get().get()))
      ErrorAndExit("some checks in '" + CheckerFileName + "' failed");
  }
  return 0;
}

void applySpecificSectionMappings(RuntimeDyldChecker &Checker) {

  for (StringRef Mapping : SpecificSectionMappings) {

    size_t EqualsIdx = Mapping.find_first_of("=");
    std::string SectionIDStr = Mapping.substr(0, EqualsIdx);
    size_t ComaIdx = Mapping.find_first_of(",");

    if (ComaIdx == StringRef::npos)
      report_fatal_error("Invalid section specification '" + Mapping +
                         "'. Should be '<file name>,<section name>=<addr>'");

    std::string FileName = SectionIDStr.substr(0, ComaIdx);
    std::string SectionName = SectionIDStr.substr(ComaIdx + 1);

    uint64_t OldAddrInt;
    std::string ErrorMsg;
    std::tie(OldAddrInt, ErrorMsg) =
      Checker.getSectionAddr(FileName, SectionName, true);

    if (ErrorMsg != "")
      report_fatal_error(ErrorMsg);

    void* OldAddr = reinterpret_cast<void*>(static_cast<uintptr_t>(OldAddrInt));

    std::string NewAddrStr = Mapping.substr(EqualsIdx + 1);
    uint64_t NewAddr;

    if (StringRef(NewAddrStr).getAsInteger(0, NewAddr))
      report_fatal_error("Invalid section address in mapping '" + Mapping +
                         "'.");

    Checker.getRTDyld().mapSectionAddress(OldAddr, NewAddr);
  }
}

// Scatter sections in all directions!
// Remaps section addresses for -verify mode. The following command line options
// can be used to customize the layout of the memory within the phony target's
// address space:
// -target-addr-start <s> -- Specify where the phony target address range starts.
// -target-addr-end   <e> -- Specify where the phony target address range ends.
// -target-section-sep <d> -- Specify how big a gap should be left between the
//                            end of one section and the start of the next.
//                            Defaults to zero. Set to something big
//                            (e.g. 1 << 32) to stress-test stubs, GOTs, etc.
//
static void remapSectionsAndSymbols(const llvm::Triple &TargetTriple,
                                    TrivialMemoryManager &MemMgr,
                                    RuntimeDyldChecker &Checker) {

  // Set up a work list (section addr/size pairs).
  typedef std::list<std::pair<void*, uint64_t>> WorklistT;
  WorklistT Worklist;

  for (const auto& CodeSection : MemMgr.FunctionMemory)
    Worklist.push_back(std::make_pair(CodeSection.base(), CodeSection.size()));
  for (const auto& DataSection : MemMgr.DataMemory)
    Worklist.push_back(std::make_pair(DataSection.base(), DataSection.size()));

  // Apply any section-specific mappings that were requested on the command
  // line.
  applySpecificSectionMappings(Checker);

  // Keep an "already allocated" mapping of section target addresses to sizes.
  // Sections whose address mappings aren't specified on the command line will
  // allocated around the explicitly mapped sections while maintaining the
  // minimum separation.
  std::map<uint64_t, uint64_t> AlreadyAllocated;

  // Move the previously applied mappings (whether explicitly specified on the
  // command line, or implicitly set by RuntimeDyld) into the already-allocated
  // map.
  for (WorklistT::iterator I = Worklist.begin(), E = Worklist.end();
       I != E;) {
    WorklistT::iterator Tmp = I;
    ++I;
    auto LoadAddr = Checker.getSectionLoadAddress(Tmp->first);

    if (LoadAddr &&
        *LoadAddr != static_cast<uint64_t>(
                       reinterpret_cast<uintptr_t>(Tmp->first))) {
      // A section will have a LoadAddr of 0 if it wasn't loaded for whatever
      // reason (e.g. zero byte COFF sections). Don't include those sections in
      // the allocation map.
      if (*LoadAddr != 0)
        AlreadyAllocated[*LoadAddr] = Tmp->second;
      Worklist.erase(Tmp);
    }
  }

  // If the -target-addr-end option wasn't explicitly passed, then set it to a
  // sensible default based on the target triple.
  if (TargetAddrEnd.getNumOccurrences() == 0) {
    if (TargetTriple.isArch16Bit())
      TargetAddrEnd = (1ULL << 16) - 1;
    else if (TargetTriple.isArch32Bit())
      TargetAddrEnd = (1ULL << 32) - 1;
    // TargetAddrEnd already has a sensible default for 64-bit systems, so
    // there's nothing to do in the 64-bit case.
  }

  // Process any elements remaining in the worklist.
  while (!Worklist.empty()) {
    std::pair<void*, uint64_t> CurEntry = Worklist.front();
    Worklist.pop_front();

    uint64_t NextSectionAddr = TargetAddrStart;

    for (const auto &Alloc : AlreadyAllocated)
      if (NextSectionAddr + CurEntry.second + TargetSectionSep <= Alloc.first)
        break;
      else
        NextSectionAddr = Alloc.first + Alloc.second + TargetSectionSep;

    AlreadyAllocated[NextSectionAddr] = CurEntry.second;
    Checker.getRTDyld().mapSectionAddress(CurEntry.first, NextSectionAddr);
  }

  // Add dummy symbols to the memory manager.
  for (const auto &Mapping : DummySymbolMappings) {
    size_t EqualsIdx = Mapping.find_first_of('=');

    if (EqualsIdx == StringRef::npos)
      report_fatal_error("Invalid dummy symbol specification '" + Mapping +
                         "'. Should be '<symbol name>=<addr>'");

    std::string Symbol = Mapping.substr(0, EqualsIdx);
    std::string AddrStr = Mapping.substr(EqualsIdx + 1);

    uint64_t Addr;
    if (StringRef(AddrStr).getAsInteger(0, Addr))
      report_fatal_error("Invalid symbol mapping '" + Mapping + "'.");

    MemMgr.addDummySymbol(Symbol, Addr);
  }
}

// Load and link the objects specified on the command line, but do not execute
// anything. Instead, attach a RuntimeDyldChecker instance and call it to
// verify the correctness of the linked memory.
static int linkAndVerify() {

  // Check for missing triple.
  if (TripleName == "")
    ErrorAndExit("-triple required when running in -verify mode.");

  // Look up the target and build the disassembler.
  Triple TheTriple(Triple::normalize(TripleName));
  std::string ErrorStr;
  const Target *TheTarget =
    TargetRegistry::lookupTarget("", TheTriple, ErrorStr);
  if (!TheTarget)
    ErrorAndExit("Error accessing target '" + TripleName + "': " + ErrorStr);

  TripleName = TheTriple.getTriple();

  std::unique_ptr<MCSubtargetInfo> STI(
    TheTarget->createMCSubtargetInfo(TripleName, MCPU, ""));
  if (!STI)
    ErrorAndExit("Unable to create subtarget info!");

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    ErrorAndExit("Unable to create target register info!");

  std::unique_ptr<MCAsmInfo> MAI(TheTarget->createMCAsmInfo(*MRI, TripleName));
  if (!MAI)
    ErrorAndExit("Unable to create target asm info!");

  MCContext Ctx(MAI.get(), MRI.get(), nullptr);

  std::unique_ptr<MCDisassembler> Disassembler(
    TheTarget->createMCDisassembler(*STI, Ctx));
  if (!Disassembler)
    ErrorAndExit("Unable to create disassembler!");

  std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());

  std::unique_ptr<MCInstPrinter> InstPrinter(
      TheTarget->createMCInstPrinter(Triple(TripleName), 0, *MAI, *MII, *MRI));

  // Load any dylibs requested on the command line.
  loadDylibs();

  // Instantiate a dynamic linker.
  TrivialMemoryManager MemMgr;
  doPreallocation(MemMgr);
  RuntimeDyld Dyld(MemMgr, MemMgr);
  Dyld.setProcessAllSections(true);
  RuntimeDyldChecker Checker(Dyld, Disassembler.get(), InstPrinter.get(),
                             llvm::dbgs());

  // If we don't have any input files, read from stdin.
  if (!InputFileList.size())
    InputFileList.push_back("-");
  for (auto &Filename : InputFileList) {
    // Load the input memory buffer.
    ErrorOr<std::unique_ptr<MemoryBuffer>> InputBuffer =
        MemoryBuffer::getFileOrSTDIN(Filename);

    if (std::error_code EC = InputBuffer.getError())
      ErrorAndExit("unable to read input: '" + EC.message() + "'");

    Expected<std::unique_ptr<ObjectFile>> MaybeObj(
      ObjectFile::createObjectFile((*InputBuffer)->getMemBufferRef()));

    if (!MaybeObj) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      logAllUnhandledErrors(MaybeObj.takeError(), OS);
      OS.flush();
      ErrorAndExit("unable to create object file: '" + Buf + "'");
    }

    ObjectFile &Obj = **MaybeObj;

    // Load the object file
    Dyld.loadObject(Obj);
    if (Dyld.hasError()) {
      ErrorAndExit(Dyld.getErrorString());
    }
  }

  // Re-map the section addresses into the phony target address space and add
  // dummy symbols.
  remapSectionsAndSymbols(TheTriple, MemMgr, Checker);

  // Resolve all the relocations we can.
  Dyld.resolveRelocations();

  // Register EH frames.
  Dyld.registerEHFrames();

  int ErrorCode = checkAllExpressions(Checker);
  if (Dyld.hasError())
    ErrorAndExit("RTDyld reported an error applying relocations:\n  " +
                 Dyld.getErrorString());

  return ErrorCode;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  ProgramName = argv[0];

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllDisassemblers();

  cl::ParseCommandLineOptions(argc, argv, "llvm MC-JIT tool\n");

  switch (Action) {
  case AC_Execute:
    return executeInput();
  case AC_PrintDebugLineInfo:
    return printLineInfoForInput(/* LoadObjects */ true,/* UseDebugObj */ true);
  case AC_PrintLineInfo:
    return printLineInfoForInput(/* LoadObjects */ true,/* UseDebugObj */false);
  case AC_PrintObjectLineInfo:
    return printLineInfoForInput(/* LoadObjects */false,/* UseDebugObj */false);
  case AC_Verify:
    return linkAndVerify();
  }
}
