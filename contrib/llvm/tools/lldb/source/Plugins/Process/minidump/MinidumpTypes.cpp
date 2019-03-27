//===-- MinidumpTypes.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MinidumpTypes.h"

// C includes
// C++ includes

using namespace lldb_private;
using namespace minidump;

const MinidumpHeader *MinidumpHeader::Parse(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpHeader *header = nullptr;
  Status error = consumeObject(data, header);

  const MinidumpHeaderConstants signature =
      static_cast<const MinidumpHeaderConstants>(
          static_cast<const uint32_t>(header->signature));
  const MinidumpHeaderConstants version =
      static_cast<const MinidumpHeaderConstants>(
          static_cast<const uint32_t>(header->version) & 0x0000ffff);
  // the high 16 bits of the version field are implementation specific

  if (error.Fail() || signature != MinidumpHeaderConstants::Signature ||
      version != MinidumpHeaderConstants::Version)
    return nullptr;

  return header;
}

// Minidump string
llvm::Optional<std::string>
lldb_private::minidump::parseMinidumpString(llvm::ArrayRef<uint8_t> &data) {
  std::string result;

  const uint32_t *source_length_ptr;
  Status error = consumeObject(data, source_length_ptr);

  // Copy non-aligned source_length data into aligned memory.
  uint32_t source_length;
  std::memcpy(&source_length, source_length_ptr, sizeof(source_length));

  if (error.Fail() || source_length > data.size() || source_length % 2 != 0)
    return llvm::None;

  auto source_start = reinterpret_cast<const llvm::UTF16 *>(data.data());
  // source_length is the length of the string in bytes we need the length of
  // the string in UTF-16 characters/code points (16 bits per char) that's why
  // it's divided by 2
  const auto source_end = source_start + source_length / 2;
  // resize to worst case length
  result.resize(UNI_MAX_UTF8_BYTES_PER_CODE_POINT * source_length / 2);
  auto result_start = reinterpret_cast<llvm::UTF8 *>(&result[0]);
  const auto result_end = result_start + result.size();
  llvm::ConvertUTF16toUTF8(&source_start, source_end, &result_start, result_end,
                           llvm::strictConversion);
  const auto result_size =
      std::distance(reinterpret_cast<llvm::UTF8 *>(&result[0]), result_start);
  result.resize(result_size); // shrink to actual length

  return result;
}

// MinidumpThread
const MinidumpThread *MinidumpThread::Parse(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpThread *thread = nullptr;
  Status error = consumeObject(data, thread);
  if (error.Fail())
    return nullptr;

  return thread;
}

llvm::ArrayRef<MinidumpThread>
MinidumpThread::ParseThreadList(llvm::ArrayRef<uint8_t> &data) {
  const auto orig_size = data.size();
  const llvm::support::ulittle32_t *thread_count;
  Status error = consumeObject(data, thread_count);
  if (error.Fail() || *thread_count * sizeof(MinidumpThread) > data.size())
    return {};

  // Compilers might end up padding an extra 4 bytes depending on how the
  // structure is padded by the compiler and the #pragma pack settings.
  if (4 + *thread_count * sizeof(MinidumpThread) < orig_size)
    data = data.drop_front(4);

  return llvm::ArrayRef<MinidumpThread>(
      reinterpret_cast<const MinidumpThread *>(data.data()), *thread_count);
}

// MinidumpSystemInfo
const MinidumpSystemInfo *
MinidumpSystemInfo::Parse(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpSystemInfo *system_info;
  Status error = consumeObject(data, system_info);
  if (error.Fail())
    return nullptr;

  return system_info;
}

// MinidumpMiscInfo
const MinidumpMiscInfo *MinidumpMiscInfo::Parse(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpMiscInfo *misc_info;
  Status error = consumeObject(data, misc_info);
  if (error.Fail())
    return nullptr;

  return misc_info;
}

llvm::Optional<lldb::pid_t> MinidumpMiscInfo::GetPid() const {
  uint32_t pid_flag =
      static_cast<const uint32_t>(MinidumpMiscInfoFlags::ProcessID);
  if (flags1 & pid_flag)
    return llvm::Optional<lldb::pid_t>(process_id);

  return llvm::None;
}

// Linux Proc Status
// it's stored as an ascii string in the file
llvm::Optional<LinuxProcStatus>
LinuxProcStatus::Parse(llvm::ArrayRef<uint8_t> &data) {
  LinuxProcStatus result;
  result.proc_status =
      llvm::StringRef(reinterpret_cast<const char *>(data.data()), data.size());
  data = data.drop_front(data.size());

  llvm::SmallVector<llvm::StringRef, 0> lines;
  result.proc_status.split(lines, '\n', 42);
  // /proc/$pid/status has 41 lines, but why not use 42?
  for (auto line : lines) {
    if (line.consume_front("Pid:")) {
      line = line.trim();
      if (!line.getAsInteger(10, result.pid))
        return result;
    }
  }

  return llvm::None;
}

lldb::pid_t LinuxProcStatus::GetPid() const { return pid; }

// Module stuff
const MinidumpModule *MinidumpModule::Parse(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpModule *module = nullptr;
  Status error = consumeObject(data, module);
  if (error.Fail())
    return nullptr;

  return module;
}

llvm::ArrayRef<MinidumpModule>
MinidumpModule::ParseModuleList(llvm::ArrayRef<uint8_t> &data) {
  const auto orig_size = data.size();
  const llvm::support::ulittle32_t *modules_count;
  Status error = consumeObject(data, modules_count);
  if (error.Fail() || *modules_count * sizeof(MinidumpModule) > data.size())
    return {};
  
  // Compilers might end up padding an extra 4 bytes depending on how the
  // structure is padded by the compiler and the #pragma pack settings.
  if (4 + *modules_count * sizeof(MinidumpModule) < orig_size)
    data = data.drop_front(4);
  
  return llvm::ArrayRef<MinidumpModule>(
      reinterpret_cast<const MinidumpModule *>(data.data()), *modules_count);
}

// Exception stuff
const MinidumpExceptionStream *
MinidumpExceptionStream::Parse(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpExceptionStream *exception_stream = nullptr;
  Status error = consumeObject(data, exception_stream);
  if (error.Fail())
    return nullptr;

  return exception_stream;
}

llvm::ArrayRef<MinidumpMemoryDescriptor>
MinidumpMemoryDescriptor::ParseMemoryList(llvm::ArrayRef<uint8_t> &data) {
  const auto orig_size = data.size();
  const llvm::support::ulittle32_t *mem_ranges_count;
  Status error = consumeObject(data, mem_ranges_count);
  if (error.Fail() ||
      *mem_ranges_count * sizeof(MinidumpMemoryDescriptor) > data.size())
    return {};
  
  // Compilers might end up padding an extra 4 bytes depending on how the
  // structure is padded by the compiler and the #pragma pack settings.
  if (4 + *mem_ranges_count * sizeof(MinidumpMemoryDescriptor) < orig_size)
    data = data.drop_front(4);

  return llvm::makeArrayRef(
      reinterpret_cast<const MinidumpMemoryDescriptor *>(data.data()),
      *mem_ranges_count);
}

std::pair<llvm::ArrayRef<MinidumpMemoryDescriptor64>, uint64_t>
MinidumpMemoryDescriptor64::ParseMemory64List(llvm::ArrayRef<uint8_t> &data) {
  const llvm::support::ulittle64_t *mem_ranges_count;
  Status error = consumeObject(data, mem_ranges_count);
  if (error.Fail() ||
      *mem_ranges_count * sizeof(MinidumpMemoryDescriptor64) > data.size())
    return {};

  const llvm::support::ulittle64_t *base_rva;
  error = consumeObject(data, base_rva);
  if (error.Fail())
    return {};

  return std::make_pair(
      llvm::makeArrayRef(
          reinterpret_cast<const MinidumpMemoryDescriptor64 *>(data.data()),
          *mem_ranges_count),
      *base_rva);
}

std::vector<const MinidumpMemoryInfo *>
MinidumpMemoryInfo::ParseMemoryInfoList(llvm::ArrayRef<uint8_t> &data) {
  const MinidumpMemoryInfoListHeader *header;
  Status error = consumeObject(data, header);
  if (error.Fail() ||
      header->size_of_header < sizeof(MinidumpMemoryInfoListHeader) ||
      header->size_of_entry < sizeof(MinidumpMemoryInfo))
    return {};

  data = data.drop_front(header->size_of_header -
                         sizeof(MinidumpMemoryInfoListHeader));

  if (header->size_of_entry * header->num_of_entries > data.size())
    return {};

  std::vector<const MinidumpMemoryInfo *> result;
  result.reserve(header->num_of_entries);

  for (uint64_t i = 0; i < header->num_of_entries; ++i) {
    result.push_back(reinterpret_cast<const MinidumpMemoryInfo *>(
        data.data() + i * header->size_of_entry));
  }

  return result;
}
