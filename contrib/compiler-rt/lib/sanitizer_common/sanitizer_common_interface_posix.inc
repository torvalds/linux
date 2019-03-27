//===-- sanitizer_common_interface_posix.inc ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Sanitizer Common interface list only available for Posix systems.
//===----------------------------------------------------------------------===//
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_code)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_data)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_demangle)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_flush)
