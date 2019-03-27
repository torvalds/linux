//===-- RegisterCheckpoint.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterCheckpoint_h_
#define liblldb_RegisterCheckpoint_h_

#include "lldb/Target/StackID.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// Inherit from UserID in case pushing/popping all register values can be done
// using a 64 bit integer that holds a baton/cookie instead of actually having
// to read all register values into a buffer
class RegisterCheckpoint : public UserID {
public:
  enum class Reason {
    // An expression is about to be run on the thread if the protocol that
    // talks to the debuggee supports checkpointing the registers using a
    // push/pop then the UserID base class in the RegisterCheckpoint can be
    // used to store the baton/cookie that refers to the remote saved state.
    eExpression,
    // The register checkpoint wants the raw register bytes, so they must be
    // read into m_data_sp, or the save/restore checkpoint should fail.
    eDataBackup
  };

  RegisterCheckpoint(Reason reason)
      : UserID(0), m_data_sp(), m_reason(reason) {}

  ~RegisterCheckpoint() {}

  lldb::DataBufferSP &GetData() { return m_data_sp; }

  const lldb::DataBufferSP &GetData() const { return m_data_sp; }

protected:
  lldb::DataBufferSP m_data_sp;
  Reason m_reason;

  // Make RegisterCheckpointSP if you wish to share the data in this class.
  DISALLOW_COPY_AND_ASSIGN(RegisterCheckpoint);
};

} // namespace lldb_private

#endif // liblldb_RegisterCheckpoint_h_
