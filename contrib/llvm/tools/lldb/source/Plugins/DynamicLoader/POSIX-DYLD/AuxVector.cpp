//===-- AuxVector.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AuxVector.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

static bool GetMaxU64(DataExtractor &data, lldb::offset_t *offset_ptr,
                      uint64_t *value, unsigned int byte_size) {
  lldb::offset_t saved_offset = *offset_ptr;
  *value = data.GetMaxU64(offset_ptr, byte_size);
  return *offset_ptr != saved_offset;
}

static bool ParseAuxvEntry(DataExtractor &data, AuxVector::Entry &entry,
                           lldb::offset_t *offset_ptr, unsigned int byte_size) {
  if (!GetMaxU64(data, offset_ptr, &entry.type, byte_size))
    return false;

  if (!GetMaxU64(data, offset_ptr, &entry.value, byte_size))
    return false;

  return true;
}

DataBufferSP AuxVector::GetAuxvData() {
  if (m_process)
    return m_process->GetAuxvData();
  else
    return DataBufferSP();
}

void AuxVector::ParseAuxv(DataExtractor &data) {
  const unsigned int byte_size = m_process->GetAddressByteSize();
  lldb::offset_t offset = 0;

  for (;;) {
    Entry entry;

    if (!ParseAuxvEntry(data, entry, &offset, byte_size))
      break;

    if (entry.type == AUXV_AT_NULL)
      break;

    if (entry.type == AUXV_AT_IGNORE)
      continue;

    m_auxv.push_back(entry);
  }
}

AuxVector::AuxVector(Process *process) : m_process(process) {
  DataExtractor data;
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

  data.SetData(GetAuxvData());
  data.SetByteOrder(m_process->GetByteOrder());
  data.SetAddressByteSize(m_process->GetAddressByteSize());

  ParseAuxv(data);

  if (log)
    DumpToLog(log);
}

AuxVector::iterator AuxVector::FindEntry(EntryType type) const {
  for (iterator I = begin(); I != end(); ++I) {
    if (I->type == static_cast<uint64_t>(type))
      return I;
  }

  return end();
}

void AuxVector::DumpToLog(Log *log) const {
  if (!log)
    return;

  log->PutCString("AuxVector: ");
  for (iterator I = begin(); I != end(); ++I) {
    log->Printf("   %s [%" PRIu64 "]: %" PRIx64, GetEntryName(*I), I->type,
                I->value);
  }
}

const char *AuxVector::GetEntryName(EntryType type) {
  const char *name = "AT_???";

#define ENTRY_NAME(_type) \
  _type:                  \
  name = &#_type[5]
  switch (type) {
    case ENTRY_NAME(AUXV_AT_NULL);           break;
    case ENTRY_NAME(AUXV_AT_IGNORE);         break;
    case ENTRY_NAME(AUXV_AT_EXECFD);         break;
    case ENTRY_NAME(AUXV_AT_PHDR);           break;
    case ENTRY_NAME(AUXV_AT_PHENT);          break;
    case ENTRY_NAME(AUXV_AT_PHNUM);          break;
    case ENTRY_NAME(AUXV_AT_PAGESZ);         break;
    case ENTRY_NAME(AUXV_AT_BASE);           break;
    case ENTRY_NAME(AUXV_AT_FLAGS);          break;
    case ENTRY_NAME(AUXV_AT_ENTRY);          break;
    case ENTRY_NAME(AUXV_AT_NOTELF);         break;
    case ENTRY_NAME(AUXV_AT_UID);            break;
    case ENTRY_NAME(AUXV_AT_EUID);           break;
    case ENTRY_NAME(AUXV_AT_GID);            break;
    case ENTRY_NAME(AUXV_AT_EGID);           break;
    case ENTRY_NAME(AUXV_AT_CLKTCK);         break;
    case ENTRY_NAME(AUXV_AT_PLATFORM);       break;
    case ENTRY_NAME(AUXV_AT_HWCAP);          break;
    case ENTRY_NAME(AUXV_AT_FPUCW);          break;
    case ENTRY_NAME(AUXV_AT_DCACHEBSIZE);    break;
    case ENTRY_NAME(AUXV_AT_ICACHEBSIZE);    break;
    case ENTRY_NAME(AUXV_AT_UCACHEBSIZE);    break;
    case ENTRY_NAME(AUXV_AT_IGNOREPPC);      break;
    case ENTRY_NAME(AUXV_AT_SECURE);         break;
    case ENTRY_NAME(AUXV_AT_BASE_PLATFORM);  break;
    case ENTRY_NAME(AUXV_AT_RANDOM);         break;
    case ENTRY_NAME(AUXV_AT_EXECFN);         break;
    case ENTRY_NAME(AUXV_AT_SYSINFO);        break;
    case ENTRY_NAME(AUXV_AT_SYSINFO_EHDR);   break;
    case ENTRY_NAME(AUXV_AT_L1I_CACHESHAPE); break;
    case ENTRY_NAME(AUXV_AT_L1D_CACHESHAPE); break;
    case ENTRY_NAME(AUXV_AT_L2_CACHESHAPE);  break;
    case ENTRY_NAME(AUXV_AT_L3_CACHESHAPE);  break;
    }
#undef ENTRY_NAME

    return name;
}
