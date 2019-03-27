//===-- RegisterContextDarwinConstants.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_REGISTERCONTEXTDARWINCONSTANTS_H
#define LLDB_REGISTERCONTEXTDARWINCONSTANTS_H

namespace lldb_private {

/// Constants returned by various RegisterContextDarwin_*** functions.
#ifndef KERN_SUCCESS
#define KERN_SUCCESS 0
#endif

#ifndef KERN_INVALID_ARGUMENT
#define KERN_INVALID_ARGUMENT 4
#endif

} // namespace lldb_private

#endif // LLDB_REGISTERCONTEXTDARWINCONSTANTS_H
