#include "llvm/ExecutionEngine/Orc/OrcABISupport.h"
#include "llvm/ExecutionEngine/Orc/OrcRemoteTargetServer.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Process.h"
#include <sstream>

#include "../RemoteJITUtils.h"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::sys;

#ifdef __x86_64__
typedef OrcX86_64_SysV HostOrcArch;
#else
typedef OrcGenericABI HostOrcArch;
#endif

ExitOnError ExitOnErr;

int main(int argc, char *argv[]) {

  if (argc != 3) {
    errs() << "Usage: " << argv[0] << " <input fd> <output fd>\n";
    return 1;
  }

  ExitOnErr.setBanner(std::string(argv[0]) + ":");

  int InFD;
  int OutFD;
  {
    std::istringstream InFDStream(argv[1]), OutFDStream(argv[2]);
    InFDStream >> InFD;
    OutFDStream >> OutFD;
  }

  if (sys::DynamicLibrary::LoadLibraryPermanently(nullptr)) {
    errs() << "Error loading program symbols.\n";
    return 1;
  }

  auto SymbolLookup = [](const std::string &Name) {
    return RTDyldMemoryManager::getSymbolAddressInProcess(Name);
  };

  auto RegisterEHFrames = [](uint8_t *Addr, uint32_t Size) {
    RTDyldMemoryManager::registerEHFramesInProcess(Addr, Size);
  };

  auto DeregisterEHFrames = [](uint8_t *Addr, uint32_t Size) {
    RTDyldMemoryManager::deregisterEHFramesInProcess(Addr, Size);
  };

  FDRawChannel Channel(InFD, OutFD);
  typedef remote::OrcRemoteTargetServer<FDRawChannel, HostOrcArch> JITServer;
  JITServer Server(Channel, SymbolLookup, RegisterEHFrames, DeregisterEHFrames);

  while (!Server.receivedTerminate())
    ExitOnErr(Server.handleOne());

  close(InFD);
  close(OutFD);

  return 0;
}
