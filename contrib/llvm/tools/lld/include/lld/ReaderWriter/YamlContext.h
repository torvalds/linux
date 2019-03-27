//===- lld/ReaderWriter/YamlContext.h - object used in YAML I/O context ---===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_YAML_CONTEXT_H
#define LLD_READER_WRITER_YAML_CONTEXT_H

#include "lld/Common/LLVM.h"
#include <functional>
#include <memory>
#include <vector>

namespace lld {
class File;
class LinkingContext;
class Registry;
namespace mach_o {
namespace normalized {
struct NormalizedFile;
}
}

using lld::mach_o::normalized::NormalizedFile;

/// When YAML I/O is used in lld, the yaml context always holds a YamlContext
/// object.  We need to support hetergenous yaml documents which each require
/// different context info.  This struct supports all clients.
struct YamlContext {
  const LinkingContext *_ctx = nullptr;
  const Registry *_registry = nullptr;
  File *_file = nullptr;
  NormalizedFile *_normalizeMachOFile = nullptr;
  StringRef _path;
};

} // end namespace lld

#endif // LLD_READER_WRITER_YAML_CONTEXT_H
