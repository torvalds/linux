//===--- Compression.cpp - Compression implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements compression functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compression.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#if LLVM_ENABLE_ZLIB == 1 && HAVE_ZLIB_H
#include <zlib.h>
#endif

using namespace llvm;

#if LLVM_ENABLE_ZLIB == 1 && HAVE_LIBZ
static Error createError(StringRef Err) {
  return make_error<StringError>(Err, inconvertibleErrorCode());
}

static StringRef convertZlibCodeToString(int Code) {
  switch (Code) {
  case Z_MEM_ERROR:
    return "zlib error: Z_MEM_ERROR";
  case Z_BUF_ERROR:
    return "zlib error: Z_BUF_ERROR";
  case Z_STREAM_ERROR:
    return "zlib error: Z_STREAM_ERROR";
  case Z_DATA_ERROR:
    return "zlib error: Z_DATA_ERROR";
  case Z_OK:
  default:
    llvm_unreachable("unknown or unexpected zlib status code");
  }
}

bool zlib::isAvailable() { return true; }

Error zlib::compress(StringRef InputBuffer,
                     SmallVectorImpl<char> &CompressedBuffer, int Level) {
  unsigned long CompressedSize = ::compressBound(InputBuffer.size());
  CompressedBuffer.reserve(CompressedSize);
  int Res =
      ::compress2((Bytef *)CompressedBuffer.data(), &CompressedSize,
                  (const Bytef *)InputBuffer.data(), InputBuffer.size(), Level);
  // Tell MemorySanitizer that zlib output buffer is fully initialized.
  // This avoids a false report when running LLVM with uninstrumented ZLib.
  __msan_unpoison(CompressedBuffer.data(), CompressedSize);
  CompressedBuffer.set_size(CompressedSize);
  return Res ? createError(convertZlibCodeToString(Res)) : Error::success();
}

Error zlib::uncompress(StringRef InputBuffer, char *UncompressedBuffer,
                       size_t &UncompressedSize) {
  int Res =
      ::uncompress((Bytef *)UncompressedBuffer, (uLongf *)&UncompressedSize,
                   (const Bytef *)InputBuffer.data(), InputBuffer.size());
  // Tell MemorySanitizer that zlib output buffer is fully initialized.
  // This avoids a false report when running LLVM with uninstrumented ZLib.
  __msan_unpoison(UncompressedBuffer, UncompressedSize);
  return Res ? createError(convertZlibCodeToString(Res)) : Error::success();
}

Error zlib::uncompress(StringRef InputBuffer,
                       SmallVectorImpl<char> &UncompressedBuffer,
                       size_t UncompressedSize) {
  UncompressedBuffer.resize(UncompressedSize);
  Error E =
      uncompress(InputBuffer, UncompressedBuffer.data(), UncompressedSize);
  UncompressedBuffer.resize(UncompressedSize);
  return E;
}

uint32_t zlib::crc32(StringRef Buffer) {
  return ::crc32(0, (const Bytef *)Buffer.data(), Buffer.size());
}

#else
bool zlib::isAvailable() { return false; }
Error zlib::compress(StringRef InputBuffer,
                     SmallVectorImpl<char> &CompressedBuffer, int Level) {
  llvm_unreachable("zlib::compress is unavailable");
}
Error zlib::uncompress(StringRef InputBuffer, char *UncompressedBuffer,
                       size_t &UncompressedSize) {
  llvm_unreachable("zlib::uncompress is unavailable");
}
Error zlib::uncompress(StringRef InputBuffer,
                       SmallVectorImpl<char> &UncompressedBuffer,
                       size_t UncompressedSize) {
  llvm_unreachable("zlib::uncompress is unavailable");
}
uint32_t zlib::crc32(StringRef Buffer) {
  llvm_unreachable("zlib::crc32 is unavailable");
}
#endif
