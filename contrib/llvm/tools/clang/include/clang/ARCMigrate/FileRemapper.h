//===-- FileRemapper.h - File Remapping Helper ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ARCMIGRATE_FILEREMAPPER_H
#define LLVM_CLANG_ARCMIGRATE_FILEREMAPPER_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace llvm {
  class MemoryBuffer;
}

namespace clang {
  class FileManager;
  class FileEntry;
  class DiagnosticsEngine;
  class PreprocessorOptions;

namespace arcmt {

class FileRemapper {
  // FIXME: Reuse the same FileManager for multiple ASTContexts.
  std::unique_ptr<FileManager> FileMgr;

  typedef llvm::PointerUnion<const FileEntry *, llvm::MemoryBuffer *> Target;
  typedef llvm::DenseMap<const FileEntry *, Target> MappingsTy;
  MappingsTy FromToMappings;

  llvm::DenseMap<const FileEntry *, const FileEntry *> ToFromMappings;

public:
  FileRemapper();
  ~FileRemapper();

  bool initFromDisk(StringRef outputDir, DiagnosticsEngine &Diag,
                    bool ignoreIfFilesChanged);
  bool initFromFile(StringRef filePath, DiagnosticsEngine &Diag,
                    bool ignoreIfFilesChanged);
  bool flushToDisk(StringRef outputDir, DiagnosticsEngine &Diag);
  bool flushToFile(StringRef outputPath, DiagnosticsEngine &Diag);

  bool overwriteOriginal(DiagnosticsEngine &Diag,
                         StringRef outputDir = StringRef());

  void remap(StringRef filePath, std::unique_ptr<llvm::MemoryBuffer> memBuf);

  void applyMappings(PreprocessorOptions &PPOpts) const;

  void clear(StringRef outputDir = StringRef());

private:
  void remap(const FileEntry *file, std::unique_ptr<llvm::MemoryBuffer> memBuf);
  void remap(const FileEntry *file, const FileEntry *newfile);

  const FileEntry *getOriginalFile(StringRef filePath);
  void resetTarget(Target &targ);

  bool report(const Twine &err, DiagnosticsEngine &Diag);

  std::string getRemapInfoFile(StringRef outputDir);
};

} // end namespace arcmt

}  // end namespace clang

#endif
