//===-- UserID.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_UserID_h_
#define liblldb_UserID_h_

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"
namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class UserID UserID.h "lldb/Core/UserID.h"
/// A mix in class that contains a generic user ID.
///
/// UserID is designed as a mix in class that can contain an integer based
/// unique identifier for a variety of objects in lldb.
///
/// The value for this identifier is chosen by each parser plug-in. A value
/// should be chosen that makes sense for each kind of object and should allow
/// quick access to further and more in depth parsing.
///
/// Symbol table entries can use this to store the original symbol table
/// index, functions can use it to store the symbol table index or the
/// DWARF offset.
//----------------------------------------------------------------------
struct UserID {
  //------------------------------------------------------------------
  /// Construct with optional user ID.
  //------------------------------------------------------------------
  UserID(lldb::user_id_t uid = LLDB_INVALID_UID) : m_uid(uid) {}

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~UserID() {}

  //------------------------------------------------------------------
  /// Clears the object state.
  ///
  /// Clears the object contents back to a default invalid state.
  //------------------------------------------------------------------
  void Clear() { m_uid = LLDB_INVALID_UID; }

  //------------------------------------------------------------------
  /// Get accessor for the user ID.
  ///
  /// @return
  ///     The user ID.
  //------------------------------------------------------------------
  lldb::user_id_t GetID() const { return m_uid; }

  //------------------------------------------------------------------
  /// Set accessor for the user ID.
  ///
  /// @param[in] uid
  ///     The new user ID.
  //------------------------------------------------------------------
  void SetID(lldb::user_id_t uid) { m_uid = uid; }

  //------------------------------------------------------------------
  /// Unary predicate function object that can search for a matching user ID.
  ///
  /// Function object that can be used on any class that inherits from UserID:
  /// \code
  /// iterator pos;
  /// pos = std::find_if (coll.begin(), coll.end(), UserID::IDMatches(blockID));
  /// \endcode
  //------------------------------------------------------------------
  class IDMatches {
  public:
    //--------------------------------------------------------------
    /// Construct with the user ID to look for.
    //--------------------------------------------------------------
    IDMatches(lldb::user_id_t uid) : m_uid(uid) {}

    //--------------------------------------------------------------
    /// Unary predicate function object callback.
    //--------------------------------------------------------------
    bool operator()(const UserID &rhs) const { return m_uid == rhs.GetID(); }

  private:
    //--------------------------------------------------------------
    // Member variables.
    //--------------------------------------------------------------
    const lldb::user_id_t m_uid; ///< The user ID we are looking for
  };

protected:
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  lldb::user_id_t m_uid; ///< The user ID that uniquely identifies an object.
};

inline bool operator==(const UserID &lhs, const UserID &rhs) {
  return lhs.GetID() == rhs.GetID();
}

inline bool operator!=(const UserID &lhs, const UserID &rhs) {
  return lhs.GetID() != rhs.GetID();
}

//--------------------------------------------------------------
/// Stream the UserID object to a Stream.
//--------------------------------------------------------------
Stream &operator<<(Stream &strm, const UserID &uid);

} // namespace lldb_private

#endif // liblldb_UserID_h_
