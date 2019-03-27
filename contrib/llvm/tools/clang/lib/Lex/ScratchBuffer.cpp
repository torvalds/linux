//===--- ScratchBuffer.cpp - Scratch space for forming tokens -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ScratchBuffer interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/ScratchBuffer.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstring>
using namespace clang;

// ScratchBufSize - The size of each chunk of scratch memory.  Slightly less
//than a page, almost certainly enough for anything. :)
static const unsigned ScratchBufSize = 4060;

ScratchBuffer::ScratchBuffer(SourceManager &SM)
    : SourceMgr(SM), CurBuffer(nullptr) {
  // Set BytesUsed so that the first call to getToken will require an alloc.
  BytesUsed = ScratchBufSize;
}

/// getToken - Splat the specified text into a temporary MemoryBuffer and
/// return a SourceLocation that refers to the token.  This is just like the
/// method below, but returns a location that indicates the physloc of the
/// token.
SourceLocation ScratchBuffer::getToken(const char *Buf, unsigned Len,
                                       const char *&DestPtr) {
  if (BytesUsed+Len+2 > ScratchBufSize)
    AllocScratchBuffer(Len+2);
  else {
    // Clear out the source line cache if it's already been computed.
    // FIXME: Allow this to be incrementally extended.
    auto *ContentCache = const_cast<SrcMgr::ContentCache *>(
        SourceMgr.getSLocEntry(SourceMgr.getFileID(BufferStartLoc))
                 .getFile().getContentCache());
    ContentCache->SourceLineCache = nullptr;
  }

  // Prefix the token with a \n, so that it looks like it is the first thing on
  // its own virtual line in caret diagnostics.
  CurBuffer[BytesUsed++] = '\n';

  // Return a pointer to the character data.
  DestPtr = CurBuffer+BytesUsed;

  // Copy the token data into the buffer.
  memcpy(CurBuffer+BytesUsed, Buf, Len);

  // Remember that we used these bytes.
  BytesUsed += Len+1;

  // Add a NUL terminator to the token.  This keeps the tokens separated, in
  // case they get relexed, and puts them on their own virtual lines in case a
  // diagnostic points to one.
  CurBuffer[BytesUsed-1] = '\0';

  return BufferStartLoc.getLocWithOffset(BytesUsed-Len-1);
}

void ScratchBuffer::AllocScratchBuffer(unsigned RequestLen) {
  // Only pay attention to the requested length if it is larger than our default
  // page size.  If it is, we allocate an entire chunk for it.  This is to
  // support gigantic tokens, which almost certainly won't happen. :)
  if (RequestLen < ScratchBufSize)
    RequestLen = ScratchBufSize;

  // Get scratch buffer. Zero-initialize it so it can be dumped into a PCH file
  // deterministically.
  std::unique_ptr<llvm::WritableMemoryBuffer> OwnBuf =
      llvm::WritableMemoryBuffer::getNewMemBuffer(RequestLen,
                                                  "<scratch space>");
  CurBuffer = OwnBuf->getBufferStart();
  FileID FID = SourceMgr.createFileID(std::move(OwnBuf));
  BufferStartLoc = SourceMgr.getLocForStartOfFile(FID);
  BytesUsed = 0;
}
