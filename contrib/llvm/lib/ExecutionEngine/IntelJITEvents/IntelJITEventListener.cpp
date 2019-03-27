//===-- IntelJITEventListener.cpp - Tell Intel profiler about JITed code --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a JITEventListener object to tell Intel(R) VTune(TM)
// Amplifier XE 2011 about JITted functions.
//
//===----------------------------------------------------------------------===//

#include "IntelJITEventsWrapper.h"
#include "llvm-c/ExecutionEngine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "amplifier-jit-event-listener"

namespace {

class IntelJITEventListener : public JITEventListener {
  typedef DenseMap<void*, unsigned int> MethodIDMap;

  std::unique_ptr<IntelJITEventsWrapper> Wrapper;
  MethodIDMap MethodIDs;

  typedef SmallVector<const void *, 64> MethodAddressVector;
  typedef DenseMap<const void *, MethodAddressVector>  ObjectMap;

  ObjectMap  LoadedObjectMap;
  std::map<ObjectKey, OwningBinary<ObjectFile>> DebugObjects;

public:
  IntelJITEventListener(IntelJITEventsWrapper* libraryWrapper) {
      Wrapper.reset(libraryWrapper);
  }

  ~IntelJITEventListener() {
  }

  void notifyObjectLoaded(ObjectKey Key, const ObjectFile &Obj,
                          const RuntimeDyld::LoadedObjectInfo &L) override;

  void notifyFreeingObject(ObjectKey Key) override;
};

static LineNumberInfo DILineInfoToIntelJITFormat(uintptr_t StartAddress,
                                                 uintptr_t Address,
                                                 DILineInfo Line) {
  LineNumberInfo Result;

  Result.Offset = Address - StartAddress;
  Result.LineNumber = Line.Line;

  return Result;
}

static iJIT_Method_Load FunctionDescToIntelJITFormat(
    IntelJITEventsWrapper& Wrapper,
    const char* FnName,
    uintptr_t FnStart,
    size_t FnSize) {
  iJIT_Method_Load Result;
  memset(&Result, 0, sizeof(iJIT_Method_Load));

  Result.method_id = Wrapper.iJIT_GetNewMethodID();
  Result.method_name = const_cast<char*>(FnName);
  Result.method_load_address = reinterpret_cast<void*>(FnStart);
  Result.method_size = FnSize;

  Result.class_id = 0;
  Result.class_file_name = NULL;
  Result.user_data = NULL;
  Result.user_data_size = 0;
  Result.env = iJDE_JittingAPI;

  return Result;
}

void IntelJITEventListener::notifyObjectLoaded(
    ObjectKey Key, const ObjectFile &Obj,
    const RuntimeDyld::LoadedObjectInfo &L) {

  OwningBinary<ObjectFile> DebugObjOwner = L.getObjectForDebug(Obj);
  const ObjectFile *DebugObj = DebugObjOwner.getBinary();
  if (!DebugObj)
    return;

  // Get the address of the object image for use as a unique identifier
  const void* ObjData = DebugObj->getData().data();
  std::unique_ptr<DIContext> Context = DWARFContext::create(*DebugObj);
  MethodAddressVector Functions;

  // Use symbol info to iterate functions in the object.
  for (const std::pair<SymbolRef, uint64_t> &P : computeSymbolSizes(*DebugObj)) {
    SymbolRef Sym = P.first;
    std::vector<LineNumberInfo> LineInfo;
    std::string SourceFileName;

    Expected<SymbolRef::Type> SymTypeOrErr = Sym.getType();
    if (!SymTypeOrErr) {
      // TODO: Actually report errors helpfully.
      consumeError(SymTypeOrErr.takeError());
      continue;
    }
    SymbolRef::Type SymType = *SymTypeOrErr;
    if (SymType != SymbolRef::ST_Function)
      continue;

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

    // Record this address in a local vector
    Functions.push_back((void*)Addr);

    // Build the function loaded notification message
    iJIT_Method_Load FunctionMessage =
      FunctionDescToIntelJITFormat(*Wrapper, Name->data(), Addr, Size);
    DILineInfoTable Lines = Context->getLineInfoForAddressRange(Addr, Size);
    DILineInfoTable::iterator Begin = Lines.begin();
    DILineInfoTable::iterator End = Lines.end();
    for (DILineInfoTable::iterator It = Begin; It != End; ++It) {
      LineInfo.push_back(
          DILineInfoToIntelJITFormat((uintptr_t)Addr, It->first, It->second));
    }
    if (LineInfo.size() == 0) {
      FunctionMessage.source_file_name = 0;
      FunctionMessage.line_number_size = 0;
      FunctionMessage.line_number_table = 0;
    } else {
      // Source line information for the address range is provided as
      // a code offset for the start of the corresponding sub-range and
      // a source line. JIT API treats offsets in LineNumberInfo structures
      // as the end of the corresponding code region. The start of the code
      // is taken from the previous element. Need to shift the elements.

      LineNumberInfo last = LineInfo.back();
      last.Offset = FunctionMessage.method_size;
      LineInfo.push_back(last);
      for (size_t i = LineInfo.size() - 2; i > 0; --i)
        LineInfo[i].LineNumber = LineInfo[i - 1].LineNumber;

      SourceFileName = Lines.front().second.FileName;
      FunctionMessage.source_file_name =
        const_cast<char *>(SourceFileName.c_str());
      FunctionMessage.line_number_size = LineInfo.size();
      FunctionMessage.line_number_table = &*LineInfo.begin();
    }

    Wrapper->iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED,
                              &FunctionMessage);
    MethodIDs[(void*)Addr] = FunctionMessage.method_id;
  }

  // To support object unload notification, we need to keep a list of
  // registered function addresses for each loaded object.  We will
  // use the MethodIDs map to get the registered ID for each function.
  LoadedObjectMap[ObjData] = Functions;
  DebugObjects[Key] = std::move(DebugObjOwner);
}

void IntelJITEventListener::notifyFreeingObject(ObjectKey Key) {
  // This object may not have been registered with the listener. If it wasn't,
  // bail out.
  if (DebugObjects.find(Key) == DebugObjects.end())
    return;

  // Get the address of the object image for use as a unique identifier
  const ObjectFile &DebugObj = *DebugObjects[Key].getBinary();
  const void* ObjData = DebugObj.getData().data();

  // Get the object's function list from LoadedObjectMap
  ObjectMap::iterator OI = LoadedObjectMap.find(ObjData);
  if (OI == LoadedObjectMap.end())
    return;
  MethodAddressVector& Functions = OI->second;

  // Walk the function list, unregistering each function
  for (MethodAddressVector::iterator FI = Functions.begin(),
                                     FE = Functions.end();
       FI != FE;
       ++FI) {
    void* FnStart = const_cast<void*>(*FI);
    MethodIDMap::iterator MI = MethodIDs.find(FnStart);
    if (MI != MethodIDs.end()) {
      Wrapper->iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_UNLOAD_START,
                                &MI->second);
      MethodIDs.erase(MI);
    }
  }

  // Erase the object from LoadedObjectMap
  LoadedObjectMap.erase(OI);
  DebugObjects.erase(Key);
}

}  // anonymous namespace.

namespace llvm {
JITEventListener *JITEventListener::createIntelJITEventListener() {
  return new IntelJITEventListener(new IntelJITEventsWrapper);
}

// for testing
JITEventListener *JITEventListener::createIntelJITEventListener(
                                      IntelJITEventsWrapper* TestImpl) {
  return new IntelJITEventListener(TestImpl);
}

} // namespace llvm

LLVMJITEventListenerRef LLVMCreateIntelJITEventListener(void)
{
  return wrap(JITEventListener::createIntelJITEventListener());
}
