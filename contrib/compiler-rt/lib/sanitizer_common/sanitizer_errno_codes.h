//===-- sanitizer_errno_codes.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizers run-time libraries.
//
// Defines errno codes to avoid including errno.h and its dependencies into
// sensitive files (e.g. interceptors are not supposed to include any system
// headers).
// It's ok to use errno.h directly when your file already depend on other system
// includes though.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ERRNO_CODES_H
#define SANITIZER_ERRNO_CODES_H

namespace __sanitizer {

#define errno_ENOMEM 12
#define errno_EBUSY 16
#define errno_EINVAL 22

// Those might not present or their value differ on different platforms.
extern const int errno_EOWNERDEAD;

}  // namespace __sanitizer

#endif  // SANITIZER_ERRNO_CODES_H
