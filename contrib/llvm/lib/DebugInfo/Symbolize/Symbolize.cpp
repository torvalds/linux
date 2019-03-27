//===-- LLVMSymbolize.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation for LLVM symbolization library.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/Symbolize.h"

#include "SymbolizableObjectFile.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/DebugInfo/PDB/PDBContext.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <Windows.h>

// This must be included after windows.h.
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")

// Windows.h conflicts with our COFF header definitions.
#ifdef IMAGE_FILE_MACHINE_I386
#undef IMAGE_FILE_MACHINE_I386
#endif
#endif

namespace llvm {
namespace symbolize {

Expected<DILineInfo>
LLVMSymbolizer::symbolizeCode(const std::string &ModuleName,
                              uint64_t ModuleOffset, StringRef DWPName) {
  SymbolizableModule *Info;
  if (auto InfoOrErr = getOrCreateModuleInfo(ModuleName, DWPName))
    Info = InfoOrErr.get();
  else
    return InfoOrErr.takeError();

  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return DILineInfo();

  // If the user is giving us relative addresses, add the preferred base of the
  // object to the offset before we do the query. It's what DIContext expects.
  if (Opts.RelativeAddresses)
    ModuleOffset += Info->getModulePreferredBase();

  DILineInfo LineInfo = Info->symbolizeCode(ModuleOffset, Opts.PrintFunctions,
                                            Opts.UseSymbolTable);
  if (Opts.Demangle)
    LineInfo.FunctionName = DemangleName(LineInfo.FunctionName, Info);
  return LineInfo;
}

Expected<DIInliningInfo>
LLVMSymbolizer::symbolizeInlinedCode(const std::string &ModuleName,
                                     uint64_t ModuleOffset, StringRef DWPName) {
  SymbolizableModule *Info;
  if (auto InfoOrErr = getOrCreateModuleInfo(ModuleName, DWPName))
    Info = InfoOrErr.get();
  else
    return InfoOrErr.takeError();

  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return DIInliningInfo();

  // If the user is giving us relative addresses, add the preferred base of the
  // object to the offset before we do the query. It's what DIContext expects.
  if (Opts.RelativeAddresses)
    ModuleOffset += Info->getModulePreferredBase();

  DIInliningInfo InlinedContext = Info->symbolizeInlinedCode(
      ModuleOffset, Opts.PrintFunctions, Opts.UseSymbolTable);
  if (Opts.Demangle) {
    for (int i = 0, n = InlinedContext.getNumberOfFrames(); i < n; i++) {
      auto *Frame = InlinedContext.getMutableFrame(i);
      Frame->FunctionName = DemangleName(Frame->FunctionName, Info);
    }
  }
  return InlinedContext;
}

Expected<DIGlobal> LLVMSymbolizer::symbolizeData(const std::string &ModuleName,
                                                 uint64_t ModuleOffset) {
  SymbolizableModule *Info;
  if (auto InfoOrErr = getOrCreateModuleInfo(ModuleName))
    Info = InfoOrErr.get();
  else
    return InfoOrErr.takeError();

  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return DIGlobal();

  // If the user is giving us relative addresses, add the preferred base of
  // the object to the offset before we do the query. It's what DIContext
  // expects.
  if (Opts.RelativeAddresses)
    ModuleOffset += Info->getModulePreferredBase();

  DIGlobal Global = Info->symbolizeData(ModuleOffset);
  if (Opts.Demangle)
    Global.Name = DemangleName(Global.Name, Info);
  return Global;
}

void LLVMSymbolizer::flush() {
  ObjectForUBPathAndArch.clear();
  BinaryForPath.clear();
  ObjectPairForPathArch.clear();
  Modules.clear();
}

namespace {

// For Path="/path/to/foo" and Basename="foo" assume that debug info is in
// /path/to/foo.dSYM/Contents/Resources/DWARF/foo.
// For Path="/path/to/bar.dSYM" and Basename="foo" assume that debug info is in
// /path/to/bar.dSYM/Contents/Resources/DWARF/foo.
std::string getDarwinDWARFResourceForPath(
    const std::string &Path, const std::string &Basename) {
  SmallString<16> ResourceName = StringRef(Path);
  if (sys::path::extension(Path) != ".dSYM") {
    ResourceName += ".dSYM";
  }
  sys::path::append(ResourceName, "Contents", "Resources", "DWARF");
  sys::path::append(ResourceName, Basename);
  return ResourceName.str();
}

bool checkFileCRC(StringRef Path, uint32_t CRCHash) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> MB =
      MemoryBuffer::getFileOrSTDIN(Path);
  if (!MB)
    return false;
  return !zlib::isAvailable() || CRCHash == zlib::crc32(MB.get()->getBuffer());
}

bool findDebugBinary(const std::string &OrigPath,
                     const std::string &DebuglinkName, uint32_t CRCHash,
                     std::string &Result) {
  std::string OrigRealPath = OrigPath;
#if defined(HAVE_REALPATH)
  if (char *RP = realpath(OrigPath.c_str(), nullptr)) {
    OrigRealPath = RP;
    free(RP);
  }
#endif
  SmallString<16> OrigDir(OrigRealPath);
  llvm::sys::path::remove_filename(OrigDir);
  SmallString<16> DebugPath = OrigDir;
  // Try /path/to/original_binary/debuglink_name
  llvm::sys::path::append(DebugPath, DebuglinkName);
  if (checkFileCRC(DebugPath, CRCHash)) {
    Result = DebugPath.str();
    return true;
  }
  // Try /path/to/original_binary/.debug/debuglink_name
  DebugPath = OrigDir;
  llvm::sys::path::append(DebugPath, ".debug", DebuglinkName);
  if (checkFileCRC(DebugPath, CRCHash)) {
    Result = DebugPath.str();
    return true;
  }
#if defined(__NetBSD__)
  // Try /usr/libdata/debug/path/to/original_binary/debuglink_name
  DebugPath = "/usr/libdata/debug";
#else
  // Try /usr/lib/debug/path/to/original_binary/debuglink_name
  DebugPath = "/usr/lib/debug";
#endif
  llvm::sys::path::append(DebugPath, llvm::sys::path::relative_path(OrigDir),
                          DebuglinkName);
  if (checkFileCRC(DebugPath, CRCHash)) {
    Result = DebugPath.str();
    return true;
  }
  return false;
}

bool getGNUDebuglinkContents(const ObjectFile *Obj, std::string &DebugName,
                             uint32_t &CRCHash) {
  if (!Obj)
    return false;
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Name;
    Section.getName(Name);
    Name = Name.substr(Name.find_first_not_of("._"));
    if (Name == "gnu_debuglink") {
      StringRef Data;
      Section.getContents(Data);
      DataExtractor DE(Data, Obj->isLittleEndian(), 0);
      uint32_t Offset = 0;
      if (const char *DebugNameStr = DE.getCStr(&Offset)) {
        // 4-byte align the offset.
        Offset = (Offset + 3) & ~0x3;
        if (DE.isValidOffsetForDataOfSize(Offset, 4)) {
          DebugName = DebugNameStr;
          CRCHash = DE.getU32(&Offset);
          return true;
        }
      }
      break;
    }
  }
  return false;
}

bool darwinDsymMatchesBinary(const MachOObjectFile *DbgObj,
                             const MachOObjectFile *Obj) {
  ArrayRef<uint8_t> dbg_uuid = DbgObj->getUuid();
  ArrayRef<uint8_t> bin_uuid = Obj->getUuid();
  if (dbg_uuid.empty() || bin_uuid.empty())
    return false;
  return !memcmp(dbg_uuid.data(), bin_uuid.data(), dbg_uuid.size());
}

} // end anonymous namespace

ObjectFile *LLVMSymbolizer::lookUpDsymFile(const std::string &ExePath,
    const MachOObjectFile *MachExeObj, const std::string &ArchName) {
  // On Darwin we may find DWARF in separate object file in
  // resource directory.
  std::vector<std::string> DsymPaths;
  StringRef Filename = sys::path::filename(ExePath);
  DsymPaths.push_back(getDarwinDWARFResourceForPath(ExePath, Filename));
  for (const auto &Path : Opts.DsymHints) {
    DsymPaths.push_back(getDarwinDWARFResourceForPath(Path, Filename));
  }
  for (const auto &Path : DsymPaths) {
    auto DbgObjOrErr = getOrCreateObject(Path, ArchName);
    if (!DbgObjOrErr) {
      // Ignore errors, the file might not exist.
      consumeError(DbgObjOrErr.takeError());
      continue;
    }
    ObjectFile *DbgObj = DbgObjOrErr.get();
    if (!DbgObj)
      continue;
    const MachOObjectFile *MachDbgObj = dyn_cast<const MachOObjectFile>(DbgObj);
    if (!MachDbgObj)
      continue;
    if (darwinDsymMatchesBinary(MachDbgObj, MachExeObj))
      return DbgObj;
  }
  return nullptr;
}

ObjectFile *LLVMSymbolizer::lookUpDebuglinkObject(const std::string &Path,
                                                  const ObjectFile *Obj,
                                                  const std::string &ArchName) {
  std::string DebuglinkName;
  uint32_t CRCHash;
  std::string DebugBinaryPath;
  if (!getGNUDebuglinkContents(Obj, DebuglinkName, CRCHash))
    return nullptr;
  if (!findDebugBinary(Path, DebuglinkName, CRCHash, DebugBinaryPath))
    return nullptr;
  auto DbgObjOrErr = getOrCreateObject(DebugBinaryPath, ArchName);
  if (!DbgObjOrErr) {
    // Ignore errors, the file might not exist.
    consumeError(DbgObjOrErr.takeError());
    return nullptr;
  }
  return DbgObjOrErr.get();
}

Expected<LLVMSymbolizer::ObjectPair>
LLVMSymbolizer::getOrCreateObjectPair(const std::string &Path,
                                      const std::string &ArchName) {
  const auto &I = ObjectPairForPathArch.find(std::make_pair(Path, ArchName));
  if (I != ObjectPairForPathArch.end()) {
    return I->second;
  }

  auto ObjOrErr = getOrCreateObject(Path, ArchName);
  if (!ObjOrErr) {
    ObjectPairForPathArch.insert(std::make_pair(std::make_pair(Path, ArchName),
                                                ObjectPair(nullptr, nullptr)));
    return ObjOrErr.takeError();
  }

  ObjectFile *Obj = ObjOrErr.get();
  assert(Obj != nullptr);
  ObjectFile *DbgObj = nullptr;

  if (auto MachObj = dyn_cast<const MachOObjectFile>(Obj))
    DbgObj = lookUpDsymFile(Path, MachObj, ArchName);
  if (!DbgObj)
    DbgObj = lookUpDebuglinkObject(Path, Obj, ArchName);
  if (!DbgObj)
    DbgObj = Obj;
  ObjectPair Res = std::make_pair(Obj, DbgObj);
  ObjectPairForPathArch.insert(
      std::make_pair(std::make_pair(Path, ArchName), Res));
  return Res;
}

Expected<ObjectFile *>
LLVMSymbolizer::getOrCreateObject(const std::string &Path,
                                  const std::string &ArchName) {
  const auto &I = BinaryForPath.find(Path);
  Binary *Bin = nullptr;
  if (I == BinaryForPath.end()) {
    Expected<OwningBinary<Binary>> BinOrErr = createBinary(Path);
    if (!BinOrErr) {
      BinaryForPath.insert(std::make_pair(Path, OwningBinary<Binary>()));
      return BinOrErr.takeError();
    }
    Bin = BinOrErr->getBinary();
    BinaryForPath.insert(std::make_pair(Path, std::move(BinOrErr.get())));
  } else {
    Bin = I->second.getBinary();
  }

  if (!Bin)
    return static_cast<ObjectFile *>(nullptr);

  if (MachOUniversalBinary *UB = dyn_cast_or_null<MachOUniversalBinary>(Bin)) {
    const auto &I = ObjectForUBPathAndArch.find(std::make_pair(Path, ArchName));
    if (I != ObjectForUBPathAndArch.end()) {
      return I->second.get();
    }
    Expected<std::unique_ptr<ObjectFile>> ObjOrErr =
        UB->getObjectForArch(ArchName);
    if (!ObjOrErr) {
      ObjectForUBPathAndArch.insert(std::make_pair(
          std::make_pair(Path, ArchName), std::unique_ptr<ObjectFile>()));
      return ObjOrErr.takeError();
    }
    ObjectFile *Res = ObjOrErr->get();
    ObjectForUBPathAndArch.insert(std::make_pair(std::make_pair(Path, ArchName),
                                                 std::move(ObjOrErr.get())));
    return Res;
  }
  if (Bin->isObject()) {
    return cast<ObjectFile>(Bin);
  }
  return errorCodeToError(object_error::arch_not_found);
}

Expected<SymbolizableModule *>
LLVMSymbolizer::getOrCreateModuleInfo(const std::string &ModuleName,
                                      StringRef DWPName) {
  const auto &I = Modules.find(ModuleName);
  if (I != Modules.end()) {
    return I->second.get();
  }
  std::string BinaryName = ModuleName;
  std::string ArchName = Opts.DefaultArch;
  size_t ColonPos = ModuleName.find_last_of(':');
  // Verify that substring after colon form a valid arch name.
  if (ColonPos != std::string::npos) {
    std::string ArchStr = ModuleName.substr(ColonPos + 1);
    if (Triple(ArchStr).getArch() != Triple::UnknownArch) {
      BinaryName = ModuleName.substr(0, ColonPos);
      ArchName = ArchStr;
    }
  }
  auto ObjectsOrErr = getOrCreateObjectPair(BinaryName, ArchName);
  if (!ObjectsOrErr) {
    // Failed to find valid object file.
    Modules.insert(
        std::make_pair(ModuleName, std::unique_ptr<SymbolizableModule>()));
    return ObjectsOrErr.takeError();
  }
  ObjectPair Objects = ObjectsOrErr.get();

  std::unique_ptr<DIContext> Context;
  // If this is a COFF object containing PDB info, use a PDBContext to
  // symbolize. Otherwise, use DWARF.
  if (auto CoffObject = dyn_cast<COFFObjectFile>(Objects.first)) {
    const codeview::DebugInfo *DebugInfo;
    StringRef PDBFileName;
    auto EC = CoffObject->getDebugPDBInfo(DebugInfo, PDBFileName);
    if (!EC && DebugInfo != nullptr && !PDBFileName.empty()) {
#if 0
      using namespace pdb;
      std::unique_ptr<IPDBSession> Session;
      if (auto Err = loadDataForEXE(PDB_ReaderType::DIA,
                                    Objects.first->getFileName(), Session)) {
        Modules.insert(
            std::make_pair(ModuleName, std::unique_ptr<SymbolizableModule>()));
        // Return along the PDB filename to provide more context
        return createFileError(PDBFileName, std::move(Err));
      }
      Context.reset(new PDBContext(*CoffObject, std::move(Session)));
#else
      return make_error<StringError>(
          "PDB support not compiled in",
          std::make_error_code(std::errc::not_supported));
#endif
    }
  }
  if (!Context)
    Context = DWARFContext::create(*Objects.second, nullptr,
                                   DWARFContext::defaultErrorHandler, DWPName);
  assert(Context);
  auto InfoOrErr =
      SymbolizableObjectFile::create(Objects.first, std::move(Context));
  std::unique_ptr<SymbolizableModule> SymMod;
  if (InfoOrErr)
    SymMod = std::move(InfoOrErr.get());
  auto InsertResult =
      Modules.insert(std::make_pair(ModuleName, std::move(SymMod)));
  assert(InsertResult.second);
  if (auto EC = InfoOrErr.getError())
    return errorCodeToError(EC);
  return InsertResult.first->second.get();
}

namespace {

// Undo these various manglings for Win32 extern "C" functions:
// cdecl       - _foo
// stdcall     - _foo@12
// fastcall    - @foo@12
// vectorcall  - foo@@12
// These are all different linkage names for 'foo'.
StringRef demanglePE32ExternCFunc(StringRef SymbolName) {
  // Remove any '_' or '@' prefix.
  char Front = SymbolName.empty() ? '\0' : SymbolName[0];
  if (Front == '_' || Front == '@')
    SymbolName = SymbolName.drop_front();

  // Remove any '@[0-9]+' suffix.
  if (Front != '?') {
    size_t AtPos = SymbolName.rfind('@');
    if (AtPos != StringRef::npos &&
        std::all_of(SymbolName.begin() + AtPos + 1, SymbolName.end(),
                    [](char C) { return C >= '0' && C <= '9'; })) {
      SymbolName = SymbolName.substr(0, AtPos);
    }
  }

  // Remove any ending '@' for vectorcall.
  if (SymbolName.endswith("@"))
    SymbolName = SymbolName.drop_back();

  return SymbolName;
}

} // end anonymous namespace

std::string
LLVMSymbolizer::DemangleName(const std::string &Name,
                             const SymbolizableModule *DbiModuleDescriptor) {
  // We can spoil names of symbols with C linkage, so use an heuristic
  // approach to check if the name should be demangled.
  if (Name.substr(0, 2) == "_Z") {
    int status = 0;
    char *DemangledName = itaniumDemangle(Name.c_str(), nullptr, nullptr, &status);
    if (status != 0)
      return Name;
    std::string Result = DemangledName;
    free(DemangledName);
    return Result;
  }

#if defined(_MSC_VER)
  if (!Name.empty() && Name.front() == '?') {
    // Only do MSVC C++ demangling on symbols starting with '?'.
    char DemangledName[1024] = {0};
    DWORD result = ::UnDecorateSymbolName(
        Name.c_str(), DemangledName, 1023,
        UNDNAME_NO_ACCESS_SPECIFIERS |       // Strip public, private, protected
            UNDNAME_NO_ALLOCATION_LANGUAGE | // Strip __thiscall, __stdcall, etc
            UNDNAME_NO_THROW_SIGNATURES |    // Strip throw() specifications
            UNDNAME_NO_MEMBER_TYPE | // Strip virtual, static, etc specifiers
            UNDNAME_NO_MS_KEYWORDS | // Strip all MS extension keywords
            UNDNAME_NO_FUNCTION_RETURNS); // Strip function return types
    return (result == 0) ? Name : std::string(DemangledName);
  }
#endif
  if (DbiModuleDescriptor && DbiModuleDescriptor->isWin32Module())
    return std::string(demanglePE32ExternCFunc(Name));
  return Name;
}

} // namespace symbolize
} // namespace llvm
