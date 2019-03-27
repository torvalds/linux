//===-- Predicate.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Predicate_h_
#define liblldb_Predicate_h_

#include <stdint.h>
#include <time.h>

#include <condition_variable>
#include <mutex>

#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-defines.h"

//#define DB_PTHREAD_LOG_EVENTS

//----------------------------------------------------------------------
/// Enumerations for broadcasting.
//----------------------------------------------------------------------
namespace lldb_private {

typedef enum {
  eBroadcastNever,   ///< No broadcast will be sent when the value is modified.
  eBroadcastAlways,  ///< Always send a broadcast when the value is modified.
  eBroadcastOnChange ///< Only broadcast if the value changes when the value is
                     /// modified.
} PredicateBroadcastType;

//----------------------------------------------------------------------
/// @class Predicate Predicate.h "lldb/Utility/Predicate.h"
/// A C++ wrapper class for providing threaded access to a value of
/// type T.
///
/// A templatized class that provides multi-threaded access to a value
/// of type T. Threads can efficiently wait for bits within T to be set
/// or reset, or wait for T to be set to be equal/not equal to a
/// specified values.
//----------------------------------------------------------------------
template <class T> class Predicate {
public:
  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initializes the mutex, condition and value with their default
  /// constructors.
  //------------------------------------------------------------------
  Predicate() : m_value(), m_mutex(), m_condition() {}

  //------------------------------------------------------------------
  /// Construct with initial T value \a initial_value.
  ///
  /// Initializes the mutex and condition with their default
  /// constructors, and initializes the value with \a initial_value.
  ///
  /// @param[in] initial_value
  ///     The initial value for our T object.
  //------------------------------------------------------------------
  Predicate(T initial_value)
      : m_value(initial_value), m_mutex(), m_condition() {}

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// Destroy the condition, mutex, and T objects.
  //------------------------------------------------------------------
  ~Predicate() = default;

  //------------------------------------------------------------------
  /// Value get accessor.
  ///
  /// Copies the current \a m_value in a thread safe manor and returns
  /// the copied value.
  ///
  /// @return
  ///     A copy of the current value.
  //------------------------------------------------------------------
  T GetValue() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    T value = m_value;
    return value;
  }

  //------------------------------------------------------------------
  /// Value set accessor.
  ///
  /// Set the contained \a m_value to \a new_value in a thread safe
  /// way and broadcast if needed.
  ///
  /// @param[in] value
  ///     The new value to set.
  ///
  /// @param[in] broadcast_type
  ///     A value indicating when and if to broadcast. See the
  ///     PredicateBroadcastType enumeration for details.
  ///
  /// @see Predicate::Broadcast()
  //------------------------------------------------------------------
  void SetValue(T value, PredicateBroadcastType broadcast_type) {
    std::lock_guard<std::mutex> guard(m_mutex);
#ifdef DB_PTHREAD_LOG_EVENTS
    printf("%s (value = 0x%8.8x, broadcast_type = %i)\n", __FUNCTION__, value,
           broadcast_type);
#endif
    const T old_value = m_value;
    m_value = value;

    Broadcast(old_value, broadcast_type);
  }

  //------------------------------------------------------------------
  /// Wait for Cond(m_value) to be true.
  ///
  /// Waits in a thread safe way for Cond(m_value) to be true. If Cond(m_value)
  /// is already true, this function will return without waiting.
  ///
  /// It is possible for the value to be changed between the time the value is
  /// set and the time the waiting thread wakes up. If the value no longer
  /// satisfies the condition when the waiting thread wakes up, it will go back
  /// into a wait state. It may be necessary for the calling code to use
  /// additional thread synchronization methods to detect transitory states.
  ///
  /// @param[in] Cond
  ///     The condition we want \a m_value satisfy.
  ///
  /// @param[in] timeout
  ///     How long to wait for the condition to hold.
  ///
  /// @return
  ///     @li m_value if Cond(m_value) is true.
  ///     @li None otherwise (timeout occurred).
  //------------------------------------------------------------------
  template <typename C>
  llvm::Optional<T> WaitFor(C Cond, const Timeout<std::micro> &timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto RealCond = [&] { return Cond(m_value); };
    if (!timeout) {
      m_condition.wait(lock, RealCond);
      return m_value;
    }
    if (m_condition.wait_for(lock, *timeout, RealCond))
      return m_value;
    return llvm::None;
  }
  //------------------------------------------------------------------
  /// Wait for \a m_value to be equal to \a value.
  ///
  /// Waits in a thread safe way for \a m_value to be equal to \a
  /// value. If \a m_value is already equal to \a value, this
  /// function will return without waiting.
  ///
  /// It is possible for the value to be changed between the time
  /// the value is set and the time the waiting thread wakes up.
  /// If the value no longer matches the requested value when the
  /// waiting thread wakes up, it will go back into a wait state.  It
  /// may be necessary for the calling code to use additional thread
  /// synchronization methods to detect transitory states.
  ///
  /// @param[in] value
  ///     The value we want \a m_value to be equal to.
  ///
  /// @param[in] timeout
  ///     How long to wait for the condition to hold.
  ///
  /// @return
  ///     @li \b true if the \a m_value is equal to \a value
  ///     @li \b false otherwise (timeout occurred)
  //------------------------------------------------------------------
  bool WaitForValueEqualTo(T value,
                           const Timeout<std::micro> &timeout = llvm::None) {
    return WaitFor([&value](T current) { return value == current; }, timeout) !=
           llvm::None;
  }

  //------------------------------------------------------------------
  /// Wait for \a m_value to not be equal to \a value.
  ///
  /// Waits in a thread safe way for \a m_value to not be equal to \a
  /// value. If \a m_value is already not equal to \a value, this
  /// function will return without waiting.
  ///
  /// It is possible for the value to be changed between the time
  /// the value is set and the time the waiting thread wakes up.
  /// If the value is equal to the test value when the waiting thread
  /// wakes up, it will go back into a wait state.  It may be
  /// necessary for the calling code to use additional thread
  /// synchronization methods to detect transitory states.
  ///
  /// @param[in] value
  ///     The value we want \a m_value to not be equal to.
  ///
  /// @param[in] timeout
  ///     How long to wait for the condition to hold.
  ///
  /// @return
  ///     @li m_value if m_value != value
  ///     @li None otherwise (timeout occurred).
  //------------------------------------------------------------------
  llvm::Optional<T>
  WaitForValueNotEqualTo(T value,
                         const Timeout<std::micro> &timeout = llvm::None) {
    return WaitFor([&value](T current) { return value != current; }, timeout);
  }

protected:
  //----------------------------------------------------------------------
  // pthread condition and mutex variable to control access and allow blocking
  // between the main thread and the spotlight index thread.
  //----------------------------------------------------------------------
  T m_value; ///< The templatized value T that we are protecting access to
  mutable std::mutex m_mutex; ///< The mutex to use when accessing the data
  std::condition_variable m_condition; ///< The pthread condition variable to
                                       /// use for signaling that data available
                                       /// or changed.

private:
  //------------------------------------------------------------------
  /// Broadcast if needed.
  ///
  /// Check to see if we need to broadcast to our condition variable
  /// depending on the \a old_value and on the \a broadcast_type.
  ///
  /// If \a broadcast_type is eBroadcastNever, no broadcast will be
  /// sent.
  ///
  /// If \a broadcast_type is eBroadcastAlways, the condition variable
  /// will always be broadcast.
  ///
  /// If \a broadcast_type is eBroadcastOnChange, the condition
  /// variable be broadcast if the owned value changes.
  //------------------------------------------------------------------
  void Broadcast(T old_value, PredicateBroadcastType broadcast_type) {
    bool broadcast =
        (broadcast_type == eBroadcastAlways) ||
        ((broadcast_type == eBroadcastOnChange) && old_value != m_value);
#ifdef DB_PTHREAD_LOG_EVENTS
    printf("%s (old_value = 0x%8.8x, broadcast_type = %i) m_value = 0x%8.8x, "
           "broadcast = %u\n",
           __FUNCTION__, old_value, broadcast_type, m_value, broadcast);
#endif
    if (broadcast)
      m_condition.notify_all();
  }

  DISALLOW_COPY_AND_ASSIGN(Predicate);
};

} // namespace lldb_private

#endif // liblldb_Predicate_h_
