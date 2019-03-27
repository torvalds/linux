//===-- StreamFile.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/StreamFile.h"
#include "lldb/Host/FileSystem.h"

#include <stdio.h>

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// StreamFile constructor
//----------------------------------------------------------------------
StreamFile::StreamFile() : Stream(), m_file() {}

StreamFile::StreamFile(uint32_t flags, uint32_t addr_size, ByteOrder byte_order)
    : Stream(flags, addr_size, byte_order), m_file() {}

StreamFile::StreamFile(int fd, bool transfer_ownership)
    : Stream(), m_file(fd, transfer_ownership) {}

StreamFile::StreamFile(FILE *fh, bool transfer_ownership)
    : Stream(), m_file(fh, transfer_ownership) {}

StreamFile::StreamFile(const char *path) : Stream(), m_file() {
  FileSystem::Instance().Open(m_file, FileSpec(path),
                              File::eOpenOptionWrite |
                                  File::eOpenOptionCanCreate |
                                  File::eOpenOptionCloseOnExec);
}

StreamFile::StreamFile(const char *path, uint32_t options, uint32_t permissions)
    : Stream(), m_file() {

  FileSystem::Instance().Open(m_file, FileSpec(path), options, permissions);
}

StreamFile::~StreamFile() {}

void StreamFile::Flush() { m_file.Flush(); }

size_t StreamFile::WriteImpl(const void *s, size_t length) {
  m_file.Write(s, length);
  return length;
}
