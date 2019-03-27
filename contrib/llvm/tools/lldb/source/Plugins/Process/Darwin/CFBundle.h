//===-- CFBundle.h ----------------------------------------------*- C++ -*-===//
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

#ifndef __CFBundle_h__
#define __CFBundle_h__

#include "CFUtils.h"

class CFBundle : public CFReleaser<CFBundleRef> {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CFBundle(const char *path = NULL);
  CFBundle(const CFBundle &rhs);
  CFBundle &operator=(const CFBundle &rhs);
  virtual ~CFBundle();
  bool SetPath(const char *path);

  CFStringRef GetIdentifier() const;

  CFURLRef CopyExecutableURL() const;

protected:
  CFReleaser<CFURLRef> m_bundle_url;
};

#endif // #ifndef __CFBundle_h__
