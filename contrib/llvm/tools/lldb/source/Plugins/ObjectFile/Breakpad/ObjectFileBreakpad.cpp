//===-- ObjectFileBreakpad.cpp -------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/ObjectFile/Breakpad/ObjectFileBreakpad.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Utility/DataBuffer.h"
#include "llvm/ADT/StringExtras.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::breakpad;

namespace {
struct Header {
  ArchSpec arch;
  UUID uuid;
  static llvm::Optional<Header> parse(llvm::StringRef text);
};

enum class Token { Unknown, Module, Info, File, Func, Public, Stack };
} // namespace

static Token toToken(llvm::StringRef str) {
  return llvm::StringSwitch<Token>(str)
      .Case("MODULE", Token::Module)
      .Case("INFO", Token::Info)
      .Case("FILE", Token::File)
      .Case("FUNC", Token::Func)
      .Case("PUBLIC", Token::Public)
      .Case("STACK", Token::Stack)
      .Default(Token::Unknown);
}

static llvm::StringRef toString(Token t) {
  switch (t) {
  case Token::Unknown:
    return "";
  case Token::Module:
    return "MODULE";
  case Token::Info:
    return "INFO";
  case Token::File:
    return "FILE";
  case Token::Func:
    return "FUNC";
  case Token::Public:
    return "PUBLIC";
  case Token::Stack:
    return "STACK";
  }
  llvm_unreachable("Unknown token!");
}

static llvm::Triple::OSType toOS(llvm::StringRef str) {
  using llvm::Triple;
  return llvm::StringSwitch<Triple::OSType>(str)
      .Case("Linux", Triple::Linux)
      .Case("mac", Triple::MacOSX)
      .Case("windows", Triple::Win32)
      .Default(Triple::UnknownOS);
}

static llvm::Triple::ArchType toArch(llvm::StringRef str) {
  using llvm::Triple;
  return llvm::StringSwitch<Triple::ArchType>(str)
      .Case("arm", Triple::arm)
      .Case("arm64", Triple::aarch64)
      .Case("mips", Triple::mips)
      .Case("ppc", Triple::ppc)
      .Case("ppc64", Triple::ppc64)
      .Case("s390", Triple::systemz)
      .Case("sparc", Triple::sparc)
      .Case("sparcv9", Triple::sparcv9)
      .Case("x86", Triple::x86)
      .Case("x86_64", Triple::x86_64)
      .Default(Triple::UnknownArch);
}

static llvm::StringRef consume_front(llvm::StringRef &str, size_t n) {
  llvm::StringRef result = str.take_front(n);
  str = str.drop_front(n);
  return result;
}

static UUID parseModuleId(llvm::Triple::OSType os, llvm::StringRef str) {
  struct uuid_data {
    llvm::support::ulittle32_t uuid1;
    llvm::support::ulittle16_t uuid2[2];
    uint8_t uuid3[8];
    llvm::support::ulittle32_t age;
  } data;
  static_assert(sizeof(data) == 20, "");
  // The textual module id encoding should be between 33 and 40 bytes long,
  // depending on the size of the age field, which is of variable length.
  // The first three chunks of the id are encoded in big endian, so we need to
  // byte-swap those.
  if (str.size() < 33 || str.size() > 40)
    return UUID();
  uint32_t t;
  if (to_integer(consume_front(str, 8), t, 16))
    data.uuid1 = t;
  else
    return UUID();
  for (int i = 0; i < 2; ++i) {
    if (to_integer(consume_front(str, 4), t, 16))
      data.uuid2[i] = t;
    else
      return UUID();
  }
  for (int i = 0; i < 8; ++i) {
    if (!to_integer(consume_front(str, 2), data.uuid3[i], 16))
      return UUID();
  }
  if (to_integer(str, t, 16))
    data.age = t;
  else
    return UUID();

  // On non-windows, the age field should always be zero, so we don't include to
  // match the native uuid format of these platforms.
  return UUID::fromData(&data, os == llvm::Triple::Win32 ? 20 : 16);
}

llvm::Optional<Header> Header::parse(llvm::StringRef text) {
  // A valid module should start with something like:
  // MODULE Linux x86_64 E5894855C35DCCCCCCCCCCCCCCCCCCCC0 a.out
  // optionally followed by
  // INFO CODE_ID 554889E55DC3CCCCCCCCCCCCCCCCCCCC [a.exe]
  llvm::StringRef token, line;
  std::tie(line, text) = text.split('\n');
  std::tie(token, line) = getToken(line);
  if (toToken(token) != Token::Module)
    return llvm::None;

  std::tie(token, line) = getToken(line);
  llvm::Triple triple;
  triple.setOS(toOS(token));
  if (triple.getOS() == llvm::Triple::UnknownOS)
    return llvm::None;

  std::tie(token, line) = getToken(line);
  triple.setArch(toArch(token));
  if (triple.getArch() == llvm::Triple::UnknownArch)
    return llvm::None;

  llvm::StringRef module_id;
  std::tie(module_id, line) = getToken(line);

  std::tie(line, text) = text.split('\n');
  std::tie(token, line) = getToken(line);
  if (token == "INFO") {
    std::tie(token, line) = getToken(line);
    if (token != "CODE_ID")
      return llvm::None;

    std::tie(token, line) = getToken(line);
    // If we don't have any text following the code id (e.g. on linux), we
    // should use the module id as UUID. Otherwise, we revert back to the module
    // id.
    if (line.trim().empty()) {
      UUID uuid;
      if (uuid.SetFromStringRef(token, token.size() / 2) != token.size())
        return llvm::None;

      return Header{ArchSpec(triple), uuid};
    }
  }

  // We reach here if we don't have a INFO CODE_ID section, or we chose not to
  // use it. In either case, we need to properly decode the module id, whose
  // fields are encoded in big-endian.
  UUID uuid = parseModuleId(triple.getOS(), module_id);
  if (!uuid)
    return llvm::None;

  return Header{ArchSpec(triple), uuid};
}

void ObjectFileBreakpad::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileBreakpad::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString ObjectFileBreakpad::GetPluginNameStatic() {
  static ConstString g_name("breakpad");
  return g_name;
}

ObjectFile *ObjectFileBreakpad::CreateInstance(
    const ModuleSP &module_sp, DataBufferSP &data_sp, offset_t data_offset,
    const FileSpec *file, offset_t file_offset, offset_t length) {
  if (!data_sp) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
  }
  auto text = toStringRef(data_sp->GetData());
  llvm::Optional<Header> header = Header::parse(text);
  if (!header)
    return nullptr;

  // Update the data to contain the entire file if it doesn't already
  if (data_sp->GetByteSize() < length) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
  }

  return new ObjectFileBreakpad(module_sp, data_sp, data_offset, file,
                                file_offset, length, std::move(header->arch),
                                std::move(header->uuid));
}

ObjectFile *ObjectFileBreakpad::CreateMemoryInstance(
    const ModuleSP &module_sp, DataBufferSP &data_sp,
    const ProcessSP &process_sp, addr_t header_addr) {
  return nullptr;
}

size_t ObjectFileBreakpad::GetModuleSpecifications(
    const FileSpec &file, DataBufferSP &data_sp, offset_t data_offset,
    offset_t file_offset, offset_t length, ModuleSpecList &specs) {
  auto text = toStringRef(data_sp->GetData());
  llvm::Optional<Header> header = Header::parse(text);
  if (!header)
    return 0;
  ModuleSpec spec(file, std::move(header->arch));
  spec.GetUUID() = std::move(header->uuid);
  specs.Append(spec);
  return 1;
}

ObjectFileBreakpad::ObjectFileBreakpad(const ModuleSP &module_sp,
                                       DataBufferSP &data_sp,
                                       offset_t data_offset,
                                       const FileSpec *file, offset_t offset,
                                       offset_t length, ArchSpec arch,
                                       UUID uuid)
    : ObjectFile(module_sp, file, offset, length, data_sp, data_offset),
      m_arch(std::move(arch)), m_uuid(std::move(uuid)) {}

bool ObjectFileBreakpad::ParseHeader() {
  // We already parsed the header during initialization.
  return true;
}

Symtab *ObjectFileBreakpad::GetSymtab() {
  // TODO
  return nullptr;
}

bool ObjectFileBreakpad::GetUUID(UUID *uuid) {
  *uuid = m_uuid;
  return true;
}

void ObjectFileBreakpad::CreateSections(SectionList &unified_section_list) {
  if (m_sections_ap)
    return;
  m_sections_ap = llvm::make_unique<SectionList>();

  Token current_section = Token::Unknown;
  offset_t section_start;
  llvm::StringRef text = toStringRef(m_data.GetData());
  uint32_t next_section_id = 1;
  auto maybe_add_section = [&](const uint8_t *end_ptr) {
    if (current_section == Token::Unknown)
      return; // We have been called before parsing the first line.

    offset_t end_offset = end_ptr - m_data.GetDataStart();
    auto section_sp = std::make_shared<Section>(
        GetModule(), this, next_section_id++,
        ConstString(toString(current_section)), eSectionTypeOther,
        /*file_vm_addr*/ 0, /*vm_size*/ 0, section_start,
        end_offset - section_start, /*log2align*/ 0, /*flags*/ 0);
    m_sections_ap->AddSection(section_sp);
    unified_section_list.AddSection(section_sp);
  };
  while (!text.empty()) {
    llvm::StringRef line;
    std::tie(line, text) = text.split('\n');

    Token token = toToken(getToken(line).first);
    if (token == Token::Unknown) {
      // We assume this is a line record, which logically belongs to the Func
      // section. Errors will be handled when parsing the Func section.
      token = Token::Func;
    }
    if (token == current_section)
      continue;

    // Changing sections, finish off the previous one, if there was any.
    maybe_add_section(line.bytes_begin());
    // And start a new one.
    current_section = token;
    section_start = line.bytes_begin() - m_data.GetDataStart();
  }
  // Finally, add the last section.
  maybe_add_section(m_data.GetDataEnd());
}
