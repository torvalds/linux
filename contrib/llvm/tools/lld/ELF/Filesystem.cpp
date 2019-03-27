//===- Filesystem.cpp -----------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a few utility functions to handle files.
//
//===----------------------------------------------------------------------===//

#include "Filesystem.h"
#include "Config.h"
#include "lld/Common/Threads.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#if LLVM_ON_UNIX
#include <unistd.h>
#endif
#include <thread>

using namespace llvm;

using namespace lld;
using namespace lld::elf;

// Removes a given file asynchronously. This is a performance hack,
// so remove this when operating systems are improved.
//
// On Linux (and probably on other Unix-like systems), unlink(2) is a
// noticeably slow system call. As of 2016, unlink takes 250
// milliseconds to remove a 1 GB file on ext4 filesystem on my machine.
//
// To create a new result file, we first remove existing file. So, if
// you repeatedly link a 1 GB program in a regular compile-link-debug
// cycle, every cycle wastes 250 milliseconds only to remove a file.
// Since LLD can link a 1 GB binary in about 5 seconds, that waste
// actually counts.
//
// This function spawns a background thread to remove the file.
// The calling thread returns almost immediately.
void elf::unlinkAsync(StringRef Path) {
// Removing a file is async on windows.
#if defined(_WIN32)
  sys::fs::remove(Path);
#else
  if (!ThreadsEnabled || !sys::fs::exists(Path) ||
      !sys::fs::is_regular_file(Path))
    return;

  // We cannot just remove path from a different thread because we are now going
  // to create path as a new file.
  // Instead we open the file and unlink it on this thread. The unlink is fast
  // since the open fd guarantees that it is not removing the last reference.
  int FD;
  std::error_code EC = sys::fs::openFileForRead(Path, FD);
  sys::fs::remove(Path);

  // close and therefore remove TempPath in background.
  if (!EC)
    std::thread([=] { ::close(FD); }).detach();
#endif
}

// Simulate file creation to see if Path is writable.
//
// Determining whether a file is writable or not is amazingly hard,
// and after all the only reliable way of doing that is to actually
// create a file. But we don't want to do that in this function
// because LLD shouldn't update any file if it will end in a failure.
// We also don't want to reimplement heuristics to determine if a
// file is writable. So we'll let FileOutputBuffer do the work.
//
// FileOutputBuffer doesn't touch a desitnation file until commit()
// is called. We use that class without calling commit() to predict
// if the given file is writable.
std::error_code elf::tryCreateFile(StringRef Path) {
  if (Path.empty())
    return std::error_code();
  if (Path == "-")
    return std::error_code();
  return errorToErrorCode(FileOutputBuffer::create(Path, 1).takeError());
}
