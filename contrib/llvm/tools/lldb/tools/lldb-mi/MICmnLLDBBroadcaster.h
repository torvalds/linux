//===-- MICmnLLDBBroadcaster.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmnBase.h"
#include "MIUtilSingletonBase.h"
#include "lldb/API/SBBroadcaster.h"

//++
//============================================================================
// Details: MI derived class from LLDB SBBroadcaster API.
//
//          *** This class (files) is a place holder until we know we need it or
//          *** not
//
//          A singleton class.
//--
class CMICmnLLDBBroadcaster : public CMICmnBase,
                              public lldb::SBBroadcaster,
                              public MI::ISingleton<CMICmnLLDBBroadcaster> {
  friend MI::ISingleton<CMICmnLLDBBroadcaster>;

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;
  // Methods:
private:
  /* ctor */ CMICmnLLDBBroadcaster();
  /* ctor */ CMICmnLLDBBroadcaster(const CMICmnLLDBBroadcaster &);
  void operator=(const CMICmnLLDBBroadcaster &);

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmnLLDBBroadcaster() override;
};
