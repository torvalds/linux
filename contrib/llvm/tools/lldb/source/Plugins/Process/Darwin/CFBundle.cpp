//===-- CFBundle.cpp --------------------------------------------*- C++ -*-===//
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

#include "CFBundle.h"
#include "CFString.h"

//----------------------------------------------------------------------
// CFBundle constructor
//----------------------------------------------------------------------
CFBundle::CFBundle(const char *path)
    : CFReleaser<CFBundleRef>(), m_bundle_url() {
  if (path && path[0])
    SetPath(path);
}

//----------------------------------------------------------------------
// CFBundle copy constructor
//----------------------------------------------------------------------
CFBundle::CFBundle(const CFBundle &rhs)
    : CFReleaser<CFBundleRef>(rhs), m_bundle_url(rhs.m_bundle_url) {}

//----------------------------------------------------------------------
// CFBundle copy constructor
//----------------------------------------------------------------------
CFBundle &CFBundle::operator=(const CFBundle &rhs) {
  *this = rhs;
  return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
CFBundle::~CFBundle() {}

//----------------------------------------------------------------------
// Set the path for a bundle by supplying a
//----------------------------------------------------------------------
bool CFBundle::SetPath(const char *path) {
  CFAllocatorRef alloc = kCFAllocatorDefault;
  // Release our old bundle and ULR
  reset(); // This class is a CFReleaser<CFBundleRef>
  m_bundle_url.reset();
  // Make a CFStringRef from the supplied path
  CFString cf_path;
  cf_path.SetFileSystemRepresentation(path);
  if (cf_path.get()) {
    // Make our Bundle URL
    m_bundle_url.reset(::CFURLCreateWithFileSystemPath(
        alloc, cf_path.get(), kCFURLPOSIXPathStyle, true));
    if (m_bundle_url.get()) {
      reset(::CFBundleCreate(alloc, m_bundle_url.get()));
    }
  }
  return get() != NULL;
}

CFStringRef CFBundle::GetIdentifier() const {
  CFBundleRef bundle = get();
  if (bundle != NULL)
    return ::CFBundleGetIdentifier(bundle);
  return NULL;
}

CFURLRef CFBundle::CopyExecutableURL() const {
  CFBundleRef bundle = get();
  if (bundle != NULL)
    return CFBundleCopyExecutableURL(bundle);
  return NULL;
}
