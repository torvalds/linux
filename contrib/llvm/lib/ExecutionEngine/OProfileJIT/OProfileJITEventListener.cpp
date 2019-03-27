//===-- OProfileJITEventListener.cpp - Tell OProfile about JITted code ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a JITEventListener object that uses OProfileWrapper to tell
// oprofile about JITted functions, including source line information.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/ExecutionEngine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/OProfileWrapper.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/raw_ostream.h"
#include <dirent.h>
#include <fcntl.h>

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "oprofile-jit-event-listener"

namespace {

class OProfileJITEventListener : public JITEventListener {
  std::unique_ptr<OProfileWrapper> Wrapper;

  void initialize();
  std::map<ObjectKey, OwningBinary<ObjectFile>> DebugObjects;

public:
  OProfileJITEventListener(std::unique_ptr<OProfileWrapper> LibraryWrapper)
    : Wrapper(std::move(LibraryWrapper)) {
    initialize();
  }

  ~OProfileJITEventListener();

  void notifyObjectLoaded(ObjectKey Key, const ObjectFile &Obj,
                          const RuntimeDyld::LoadedObjectInfo &L) override;

  void notifyFreeingObject(ObjectKey Key) override;
};

void OProfileJITEventListener::initialize() {
  if (!Wrapper->op_open_agent()) {
    const std::string err_str = sys::StrError();
    LLVM_DEBUG(dbgs() << "Failed to connect to OProfile agent: " << err_str
                      << "\n");
  } else {
    LLVM_DEBUG(dbgs() << "Connected to OProfile agent.\n");
  }
}

OProfileJITEventListener::~OProfileJITEventListener() {
  if (Wrapper->isAgentAvailable()) {
    if (Wrapper->op_close_agent() == -1) {
      const std::string err_str = sys::StrError();
      LLVM_DEBUG(dbgs() << "Failed to disconnect from OProfile agent: "
                        << err_str << "\n");
    } else {
      LLVM_DEBUG(dbgs() << "Disconnected from OProfile agent.\n");
    }
  }
}

void OProfileJITEventListener::notifyObjectLoaded(
    ObjectKey Key, const ObjectFile &Obj,
    const RuntimeDyld::LoadedObjectInfo &L) {
  if (!Wrapper->isAgentAvailable()) {
    return;
  }

  OwningBinary<ObjectFile> DebugObjOwner = L.getObjectForDebug(Obj);
  const ObjectFile &DebugObj = *DebugObjOwner.getBinary();
  std::unique_ptr<DIContext> Context = DWARFContext::create(DebugObj);

  // Use symbol info to iterate functions in the object.
  for (const std::pair<SymbolRef, uint64_t> &P : computeSymbolSizes(DebugObj)) {
    SymbolRef Sym = P.first;
    if (!Sym.getType() || *Sym.getType() != SymbolRef::ST_Function)
      continue;

    Expected<StringRef> NameOrErr = Sym.getName();
    if (!NameOrErr)
      continue;
    StringRef Name = *NameOrErr;
    Expected<uint64_t> AddrOrErr = Sym.getAddress();
    if (!AddrOrErr)
      continue;
    uint64_t Addr = *AddrOrErr;
    uint64_t Size = P.second;

    if (Wrapper->op_write_native_code(Name.data(), Addr, (void *)Addr, Size) ==
        -1) {
      LLVM_DEBUG(dbgs() << "Failed to tell OProfile about native function "
                        << Name << " at [" << (void *)Addr << "-"
                        << ((char *)Addr + Size) << "]\n");
      continue;
    }

    DILineInfoTable Lines = Context->getLineInfoForAddressRange(Addr, Size);
    size_t i = 0;
    size_t num_entries = Lines.size();
    struct debug_line_info *debug_line;
    debug_line = (struct debug_line_info *)calloc(
        num_entries, sizeof(struct debug_line_info));

    for (auto& It : Lines) {
      debug_line[i].vma = (unsigned long)It.first;
      debug_line[i].lineno = It.second.Line;
      debug_line[i].filename =
          const_cast<char *>(Lines.front().second.FileName.c_str());
      ++i;
    }

    if (Wrapper->op_write_debug_line_info((void *)Addr, num_entries,
                                          debug_line) == -1) {
      LLVM_DEBUG(dbgs() << "Failed to tell OProfiler about debug object at ["
                        << (void *)Addr << "-" << ((char *)Addr + Size)
                        << "]\n");
      continue;
    }
  }

  DebugObjects[Key] = std::move(DebugObjOwner);
}

void OProfileJITEventListener::notifyFreeingObject(ObjectKey Key) {
  if (Wrapper->isAgentAvailable()) {

    // If there was no agent registered when the original object was loaded then
    // we won't have created a debug object for it, so bail out.
    if (DebugObjects.find(Key) == DebugObjects.end())
      return;

    const ObjectFile &DebugObj = *DebugObjects[Key].getBinary();

    // Use symbol info to iterate functions in the object.
    for (symbol_iterator I = DebugObj.symbol_begin(),
                         E = DebugObj.symbol_end();
         I != E; ++I) {
      if (I->getType() && *I->getType() == SymbolRef::ST_Function) {
        Expected<uint64_t> AddrOrErr = I->getAddress();
        if (!AddrOrErr)
          continue;
        uint64_t Addr = *AddrOrErr;

        if (Wrapper->op_unload_native_code(Addr) == -1) {
          LLVM_DEBUG(
              dbgs()
              << "Failed to tell OProfile about unload of native function at "
              << (void *)Addr << "\n");
          continue;
        }
      }
    }
  }

  DebugObjects.erase(Key);
}

}  // anonymous namespace.

namespace llvm {
JITEventListener *JITEventListener::createOProfileJITEventListener() {
  return new OProfileJITEventListener(llvm::make_unique<OProfileWrapper>());
}

} // namespace llvm

LLVMJITEventListenerRef LLVMCreateOProfileJITEventListener(void)
{
  return wrap(JITEventListener::createOProfileJITEventListener());
}
