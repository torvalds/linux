//===- lib/ReaderWriter/FileArchive.cpp -----------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/LLVM.h"
#include "lld/Core/ArchiveLibraryFile.h"
#include "lld/Core/File.h"
#include "lld/Core/Reader.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

using llvm::object::Archive;
using llvm::file_magic;
using llvm::identify_magic;

namespace lld {

namespace {

/// The FileArchive class represents an Archive Library file
class FileArchive : public lld::ArchiveLibraryFile {
public:
  FileArchive(std::unique_ptr<MemoryBuffer> mb, const Registry &reg,
              StringRef path, bool logLoading)
      : ArchiveLibraryFile(path), _mb(std::shared_ptr<MemoryBuffer>(mb.release())),
        _registry(reg), _logLoading(logLoading) {}

  /// Check if any member of the archive contains an Atom with the
  /// specified name and return the File object for that member, or nullptr.
  File *find(StringRef name) override {
    auto member = _symbolMemberMap.find(name);
    if (member == _symbolMemberMap.end())
      return nullptr;
    Archive::Child c = member->second;

    // Don't return a member already returned
    Expected<StringRef> buf = c.getBuffer();
    if (!buf) {
      // TODO: Actually report errors helpfully.
      consumeError(buf.takeError());
      return nullptr;
    }
    const char *memberStart = buf->data();
    if (_membersInstantiated.count(memberStart))
      return nullptr;
    _membersInstantiated.insert(memberStart);

    std::unique_ptr<File> result;
    if (instantiateMember(c, result))
      return nullptr;

    File *file = result.get();
    _filesReturned.push_back(std::move(result));

    // Give up the file pointer. It was stored and will be destroyed with destruction of FileArchive
    return file;
  }

  /// parse each member
  std::error_code
  parseAllMembers(std::vector<std::unique_ptr<File>> &result) override {
    if (std::error_code ec = parse())
      return ec;
    llvm::Error err = llvm::Error::success();
    for (auto mf = _archive->child_begin(err), me = _archive->child_end();
         mf != me; ++mf) {
      std::unique_ptr<File> file;
      if (std::error_code ec = instantiateMember(*mf, file)) {
        // err is Success (or we wouldn't be in the loop body) but we can't
        // return without testing or consuming it.
        consumeError(std::move(err));
        return ec;
      }
      result.push_back(std::move(file));
    }
    if (err)
      return errorToErrorCode(std::move(err));
    return std::error_code();
  }

  const AtomRange<DefinedAtom> defined() const override {
    return _noDefinedAtoms;
  }

  const AtomRange<UndefinedAtom> undefined() const override {
    return _noUndefinedAtoms;
  }

  const AtomRange<SharedLibraryAtom> sharedLibrary() const override {
    return _noSharedLibraryAtoms;
  }

  const AtomRange<AbsoluteAtom> absolute() const override {
    return _noAbsoluteAtoms;
  }

  void clearAtoms() override {
    _noDefinedAtoms.clear();
    _noUndefinedAtoms.clear();
    _noSharedLibraryAtoms.clear();
    _noAbsoluteAtoms.clear();
  }

protected:
  std::error_code doParse() override {
    // Make Archive object which will be owned by FileArchive object.
    llvm::Error Err = llvm::Error::success();
    _archive.reset(new Archive(_mb->getMemBufferRef(), Err));
    if (Err)
      return errorToErrorCode(std::move(Err));
    std::error_code ec;
    if ((ec = buildTableOfContents()))
      return ec;
    return std::error_code();
  }

private:
  std::error_code instantiateMember(Archive::Child member,
                                    std::unique_ptr<File> &result) const {
    Expected<llvm::MemoryBufferRef> mbOrErr = member.getMemoryBufferRef();
    if (!mbOrErr)
      return errorToErrorCode(mbOrErr.takeError());
    llvm::MemoryBufferRef mb = mbOrErr.get();
    std::string memberPath = (_archive->getFileName() + "("
                           + mb.getBufferIdentifier() + ")").str();

    if (_logLoading)
      llvm::errs() << memberPath << "\n";

    std::unique_ptr<MemoryBuffer> memberMB(MemoryBuffer::getMemBuffer(
        mb.getBuffer(), mb.getBufferIdentifier(), false));

    ErrorOr<std::unique_ptr<File>> fileOrErr =
        _registry.loadFile(std::move(memberMB));
    if (std::error_code ec = fileOrErr.getError())
      return ec;
    result = std::move(fileOrErr.get());
    if (std::error_code ec = result->parse())
      return ec;
    result->setArchivePath(_archive->getFileName());

    // The memory buffer is co-owned by the archive file and the children,
    // so that the bufffer is deallocated when all the members are destructed.
    result->setSharedMemoryBuffer(_mb);
    return std::error_code();
  }

  std::error_code buildTableOfContents() {
    DEBUG_WITH_TYPE("FileArchive", llvm::dbgs()
                                       << "Table of contents for archive '"
                                       << _archive->getFileName() << "':\n");
    for (const Archive::Symbol &sym : _archive->symbols()) {
      StringRef name = sym.getName();
      Expected<Archive::Child> memberOrErr = sym.getMember();
      if (!memberOrErr)
        return errorToErrorCode(memberOrErr.takeError());
      Archive::Child member = memberOrErr.get();
      DEBUG_WITH_TYPE("FileArchive",
                      llvm::dbgs()
                          << llvm::format("0x%08llX ",
                                          member.getBuffer()->data())
                          << "'" << name << "'\n");
      _symbolMemberMap.insert(std::make_pair(name, member));
    }
    return std::error_code();
  }

  typedef std::unordered_map<StringRef, Archive::Child> MemberMap;
  typedef std::set<const char *> InstantiatedSet;

  std::shared_ptr<MemoryBuffer> _mb;
  const Registry &_registry;
  std::unique_ptr<Archive> _archive;
  MemberMap _symbolMemberMap;
  InstantiatedSet _membersInstantiated;
  bool _logLoading;
  std::vector<std::unique_ptr<MemoryBuffer>> _memberBuffers;
  std::vector<std::unique_ptr<File>> _filesReturned;
};

class ArchiveReader : public Reader {
public:
  ArchiveReader(bool logLoading) : _logLoading(logLoading) {}

  bool canParse(file_magic magic, MemoryBufferRef) const override {
    return magic == file_magic::archive;
  }

  ErrorOr<std::unique_ptr<File>> loadFile(std::unique_ptr<MemoryBuffer> mb,
                                          const Registry &reg) const override {
    StringRef path = mb->getBufferIdentifier();
    std::unique_ptr<File> ret =
        llvm::make_unique<FileArchive>(std::move(mb), reg, path, _logLoading);
    return std::move(ret);
  }

private:
  bool _logLoading;
};

} // anonymous namespace

void Registry::addSupportArchives(bool logLoading) {
  add(std::unique_ptr<Reader>(new ArchiveReader(logLoading)));
}

} // namespace lld
