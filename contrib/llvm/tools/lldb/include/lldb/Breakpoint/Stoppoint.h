//===-- Stoppoint.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Stoppoint_h_
#define liblldb_Stoppoint_h_

#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class Stoppoint {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  Stoppoint();

  virtual ~Stoppoint();

  //------------------------------------------------------------------
  // Methods
  //------------------------------------------------------------------
  virtual void Dump(Stream *) = 0;

  virtual bool IsEnabled() = 0;

  virtual void SetEnabled(bool enable) = 0;

  lldb::break_id_t GetID() const;

  void SetID(lldb::break_id_t bid);

protected:
  lldb::break_id_t m_bid;

private:
  //------------------------------------------------------------------
  // For Stoppoint only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(Stoppoint);
};

} // namespace lldb_private

#endif // liblldb_Stoppoint_h_
