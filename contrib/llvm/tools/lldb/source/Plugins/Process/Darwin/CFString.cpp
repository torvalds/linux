//===-- CFString.cpp --------------------------------------------*- C++ -*-===//
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

#include "CFString.h"
#include <glob.h>
#include <string>

//----------------------------------------------------------------------
// CFString constructor
//----------------------------------------------------------------------
CFString::CFString(CFStringRef s) : CFReleaser<CFStringRef>(s) {}

//----------------------------------------------------------------------
// CFString copy constructor
//----------------------------------------------------------------------
CFString::CFString(const CFString &rhs) : CFReleaser<CFStringRef>(rhs) {}

//----------------------------------------------------------------------
// CFString copy constructor
//----------------------------------------------------------------------
CFString &CFString::operator=(const CFString &rhs) {
  if (this != &rhs)
    *this = rhs;
  return *this;
}

CFString::CFString(const char *cstr, CFStringEncoding cstr_encoding)
    : CFReleaser<CFStringRef>() {
  if (cstr && cstr[0]) {
    reset(
        ::CFStringCreateWithCString(kCFAllocatorDefault, cstr, cstr_encoding));
  }
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
CFString::~CFString() {}

const char *CFString::GetFileSystemRepresentation(std::string &s) {
  return CFString::FileSystemRepresentation(get(), s);
}

CFStringRef CFString::SetFileSystemRepresentation(const char *path) {
  CFStringRef new_value = NULL;
  if (path && path[0])
    new_value =
        ::CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, path);
  reset(new_value);
  return get();
}

CFStringRef CFString::SetFileSystemRepresentationFromCFType(CFTypeRef cf_type) {
  CFStringRef new_value = NULL;
  if (cf_type != NULL) {
    CFTypeID cf_type_id = ::CFGetTypeID(cf_type);

    if (cf_type_id == ::CFStringGetTypeID()) {
      // Retain since we are using the existing object
      new_value = (CFStringRef)::CFRetain(cf_type);
    } else if (cf_type_id == ::CFURLGetTypeID()) {
      new_value =
          ::CFURLCopyFileSystemPath((CFURLRef)cf_type, kCFURLPOSIXPathStyle);
    }
  }
  reset(new_value);
  return get();
}

CFStringRef
CFString::SetFileSystemRepresentationAndExpandTilde(const char *path) {
  std::string expanded_path;
  if (CFString::GlobPath(path, expanded_path))
    SetFileSystemRepresentation(expanded_path.c_str());
  else
    reset();
  return get();
}

const char *CFString::UTF8(std::string &str) {
  return CFString::UTF8(get(), str);
}

// Static function that puts a copy of the UTF8 contents of CF_STR into STR and
// returns the C string pointer that is contained in STR when successful, else
// NULL is returned. This allows the std::string parameter to own the extracted
// string,
// and also allows that string to be returned as a C string pointer that can be
// used.

const char *CFString::UTF8(CFStringRef cf_str, std::string &str) {
  if (cf_str) {
    const CFStringEncoding encoding = kCFStringEncodingUTF8;
    CFIndex max_utf8_str_len = CFStringGetLength(cf_str);
    max_utf8_str_len =
        CFStringGetMaximumSizeForEncoding(max_utf8_str_len, encoding);
    if (max_utf8_str_len > 0) {
      str.resize(max_utf8_str_len);
      if (!str.empty()) {
        if (CFStringGetCString(cf_str, &str[0], str.size(), encoding)) {
          str.resize(strlen(str.c_str()));
          return str.c_str();
        }
      }
    }
  }
  return NULL;
}

// Static function that puts a copy of the file system representation of CF_STR
// into STR and returns the C string pointer that is contained in STR when
// successful, else NULL is returned. This allows the std::string parameter to
// own the extracted string, and also allows that string to be returned as a C
// string pointer that can be used.

const char *CFString::FileSystemRepresentation(CFStringRef cf_str,
                                               std::string &str) {
  if (cf_str) {
    CFIndex max_length =
        ::CFStringGetMaximumSizeOfFileSystemRepresentation(cf_str);
    if (max_length > 0) {
      str.resize(max_length);
      if (!str.empty()) {
        if (::CFStringGetFileSystemRepresentation(cf_str, &str[0],
                                                  str.size())) {
          str.erase(::strlen(str.c_str()));
          return str.c_str();
        }
      }
    }
  }
  str.erase();
  return NULL;
}

CFIndex CFString::GetLength() const {
  CFStringRef str = get();
  if (str)
    return CFStringGetLength(str);
  return 0;
}

const char *CFString::GlobPath(const char *path, std::string &expanded_path) {
  glob_t globbuf;
  if (::glob(path, GLOB_TILDE, NULL, &globbuf) == 0) {
    expanded_path = globbuf.gl_pathv[0];
    ::globfree(&globbuf);
  } else
    expanded_path.clear();

  return expanded_path.c_str();
}
