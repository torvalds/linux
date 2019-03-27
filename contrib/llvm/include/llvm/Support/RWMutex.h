//===- RWMutex.h - Reader/Writer Mutual Exclusion Lock ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::RWMutex class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RWMUTEX_H
#define LLVM_SUPPORT_RWMUTEX_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Threading.h"
#include <cassert>

namespace llvm {
namespace sys {

    /// Platform agnostic RWMutex class.
    class RWMutexImpl
    {
    /// @name Constructors
    /// @{
    public:

      /// Initializes the lock but doesn't acquire it.
      /// Default Constructor.
      explicit RWMutexImpl();

    /// @}
    /// @name Do Not Implement
    /// @{
      RWMutexImpl(const RWMutexImpl & original) = delete;
      RWMutexImpl &operator=(const RWMutexImpl &) = delete;
    /// @}

      /// Releases and removes the lock
      /// Destructor
      ~RWMutexImpl();

    /// @}
    /// @name Methods
    /// @{
    public:

      /// Attempts to unconditionally acquire the lock in reader mode. If the
      /// lock is held by a writer, this method will wait until it can acquire
      /// the lock.
      /// @returns false if any kind of error occurs, true otherwise.
      /// Unconditionally acquire the lock in reader mode.
      bool reader_acquire();

      /// Attempts to release the lock in reader mode.
      /// @returns false if any kind of error occurs, true otherwise.
      /// Unconditionally release the lock in reader mode.
      bool reader_release();

      /// Attempts to unconditionally acquire the lock in reader mode. If the
      /// lock is held by any readers, this method will wait until it can
      /// acquire the lock.
      /// @returns false if any kind of error occurs, true otherwise.
      /// Unconditionally acquire the lock in writer mode.
      bool writer_acquire();

      /// Attempts to release the lock in writer mode.
      /// @returns false if any kind of error occurs, true otherwise.
      /// Unconditionally release the lock in write mode.
      bool writer_release();

    //@}
    /// @name Platform Dependent Data
    /// @{
    private:
#if defined(LLVM_ENABLE_THREADS) && LLVM_ENABLE_THREADS != 0
      void* data_ = nullptr; ///< We don't know what the data will be
#endif
    };

    /// SmartMutex - An R/W mutex with a compile time constant parameter that
    /// indicates whether this mutex should become a no-op when we're not
    /// running in multithreaded mode.
    template<bool mt_only>
    class SmartRWMutex {
      RWMutexImpl impl;
      unsigned readers = 0;
      unsigned writers = 0;

    public:
      explicit SmartRWMutex() = default;
      SmartRWMutex(const SmartRWMutex<mt_only> & original) = delete;
      SmartRWMutex<mt_only> &operator=(const SmartRWMutex<mt_only> &) = delete;

      bool lock_shared() {
        if (!mt_only || llvm_is_multithreaded())
          return impl.reader_acquire();

        // Single-threaded debugging code.  This would be racy in multithreaded
        // mode, but provides not sanity checks in single threaded mode.
        ++readers;
        return true;
      }

      bool unlock_shared() {
        if (!mt_only || llvm_is_multithreaded())
          return impl.reader_release();

        // Single-threaded debugging code.  This would be racy in multithreaded
        // mode, but provides not sanity checks in single threaded mode.
        assert(readers > 0 && "Reader lock not acquired before release!");
        --readers;
        return true;
      }

      bool lock() {
        if (!mt_only || llvm_is_multithreaded())
          return impl.writer_acquire();

        // Single-threaded debugging code.  This would be racy in multithreaded
        // mode, but provides not sanity checks in single threaded mode.
        assert(writers == 0 && "Writer lock already acquired!");
        ++writers;
        return true;
      }

      bool unlock() {
        if (!mt_only || llvm_is_multithreaded())
          return impl.writer_release();

        // Single-threaded debugging code.  This would be racy in multithreaded
        // mode, but provides not sanity checks in single threaded mode.
        assert(writers == 1 && "Writer lock not acquired before release!");
        --writers;
        return true;
      }
    };

    typedef SmartRWMutex<false> RWMutex;

    /// ScopedReader - RAII acquisition of a reader lock
    template<bool mt_only>
    struct SmartScopedReader {
      SmartRWMutex<mt_only>& mutex;

      explicit SmartScopedReader(SmartRWMutex<mt_only>& m) : mutex(m) {
        mutex.lock_shared();
      }

      ~SmartScopedReader() {
        mutex.unlock_shared();
      }
    };

    typedef SmartScopedReader<false> ScopedReader;

    /// ScopedWriter - RAII acquisition of a writer lock
    template<bool mt_only>
    struct SmartScopedWriter {
      SmartRWMutex<mt_only>& mutex;

      explicit SmartScopedWriter(SmartRWMutex<mt_only>& m) : mutex(m) {
        mutex.lock();
      }

      ~SmartScopedWriter() {
        mutex.unlock();
      }
    };

    typedef SmartScopedWriter<false> ScopedWriter;

} // end namespace sys
} // end namespace llvm

#endif // LLVM_SUPPORT_RWMUTEX_H
