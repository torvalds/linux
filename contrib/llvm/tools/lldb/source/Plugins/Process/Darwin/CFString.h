//===-- CFString.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 1/16/08.
//
//===----------------------------------------------------------------------===//

#ifndef __CFString_h__
#define __CFString_h__

#include "CFUtils.h"
#include <iosfwd>

class CFString : public CFReleaser<CFStringRef> {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CFString(CFStringRef cf_str = NULL);
  CFString(const char *s, CFStringEncoding encoding = kCFStringEncodingUTF8);
  CFString(const CFString &rhs);
  CFString &operator=(const CFString &rhs);
  virtual ~CFString();

  const char *GetFileSystemRepresentation(std::string &str);
  CFStringRef SetFileSystemRepresentation(const char *path);
  CFStringRef SetFileSystemRepresentationFromCFType(CFTypeRef cf_type);
  CFStringRef SetFileSystemRepresentationAndExpandTilde(const char *path);
  const char *UTF8(std::string &str);
  CFIndex GetLength() const;
  static const char *UTF8(CFStringRef cf_str, std::string &str);
  static const char *FileSystemRepresentation(CFStringRef cf_str,
                                              std::string &str);
  static const char *GlobPath(const char *path, std::string &expanded_path);
};

#endif // #ifndef __CFString_h__
