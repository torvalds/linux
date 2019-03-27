//===- lib/Core/Reader.cpp ------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Core/Reader.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <memory>

using llvm::file_magic;
using llvm::identify_magic;

namespace lld {

YamlIOTaggedDocumentHandler::~YamlIOTaggedDocumentHandler() = default;

void Registry::add(std::unique_ptr<Reader> reader) {
  _readers.push_back(std::move(reader));
}

void Registry::add(std::unique_ptr<YamlIOTaggedDocumentHandler> handler) {
  _yamlHandlers.push_back(std::move(handler));
}

ErrorOr<std::unique_ptr<File>>
Registry::loadFile(std::unique_ptr<MemoryBuffer> mb) const {
  // Get file magic.
  StringRef content(mb->getBufferStart(), mb->getBufferSize());
  file_magic fileType = identify_magic(content);

  // Ask each registered reader if it can handle this file type or extension.
  for (const std::unique_ptr<Reader> &reader : _readers) {
    if (!reader->canParse(fileType, mb->getMemBufferRef()))
      continue;
    return reader->loadFile(std::move(mb), *this);
  }

  // No Reader could parse this file.
  return make_error_code(llvm::errc::executable_format_error);
}

static const Registry::KindStrings kindStrings[] = {
    {Reference::kindLayoutAfter, "layout-after"},
    {Reference::kindAssociate, "associate"},
    LLD_KIND_STRING_END};

Registry::Registry() {
  addKindTable(Reference::KindNamespace::all, Reference::KindArch::all,
               kindStrings);
}

bool Registry::handleTaggedDoc(llvm::yaml::IO &io,
                               const lld::File *&file) const {
  for (const std::unique_ptr<YamlIOTaggedDocumentHandler> &h : _yamlHandlers)
    if (h->handledDocTag(io, file))
      return true;
  return false;
}

void Registry::addKindTable(Reference::KindNamespace ns,
                            Reference::KindArch arch,
                            const KindStrings array[]) {
  KindEntry entry = { ns, arch, array };
  _kindEntries.push_back(entry);
}

bool Registry::referenceKindFromString(StringRef inputStr,
                                       Reference::KindNamespace &ns,
                                       Reference::KindArch &arch,
                                       Reference::KindValue &value) const {
  for (const KindEntry &entry : _kindEntries) {
    for (const KindStrings *pair = entry.array; !pair->name.empty(); ++pair) {
      if (!inputStr.equals(pair->name))
        continue;
      ns = entry.ns;
      arch = entry.arch;
      value = pair->value;
      return true;
    }
  }
  return false;
}

bool Registry::referenceKindToString(Reference::KindNamespace ns,
                                     Reference::KindArch arch,
                                     Reference::KindValue value,
                                     StringRef &str) const {
  for (const KindEntry &entry : _kindEntries) {
    if (entry.ns != ns)
      continue;
    if (entry.arch != arch)
      continue;
    for (const KindStrings *pair = entry.array; !pair->name.empty(); ++pair) {
      if (pair->value != value)
        continue;
      str = pair->name;
      return true;
    }
  }
  return false;
}

} // end namespace lld
