//===-- ExecutionContext.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ExecutionContext_h_
#define liblldb_ExecutionContext_h_

#include <mutex>

#include "lldb/Target/StackID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//===----------------------------------------------------------------------===//
/// Execution context objects refer to objects in the execution of the program
/// that is being debugged. The consist of one or more of the following
/// objects: target, process, thread, and frame. Many objects in the debugger
/// need to track different executions contexts. For example, a local function
/// variable might have an execution context that refers to a stack frame. A
/// global or static variable might refer to a target since a stack frame
/// isn't required in order to evaluate a global or static variable (a process
/// isn't necessarily needed for a global variable since we might be able to
/// read the variable value from a data section in one of the object files in
/// a target). There are two types of objects that hold onto execution
/// contexts: ExecutionContextRef and ExecutionContext. Both of these objects
/// are described below.
///
/// Not all objects in an ExecutionContext objects will be valid. If you want
/// to refer strongly (ExecutionContext) or weakly (ExecutionContextRef) to a
/// process, then only the process and target references will be valid. For
/// threads, only the thread, process and target references will be filled in.
/// For frames, all of the objects will be filled in.
///
/// These classes are designed to be used as baton objects that get passed to
/// a wide variety of functions that require execution contexts.
//===----------------------------------------------------------------------===//

//----------------------------------------------------------------------
/// @class ExecutionContextRef ExecutionContext.h
/// "lldb/Target/ExecutionContext.h"
/// A class that holds a weak reference to an execution context.
///
/// ExecutionContextRef objects are designed to hold onto an execution context
/// that might change over time. For example, if an object wants to refer to a
/// stack frame, it should hold onto an ExecutionContextRef to a frame object.
/// The backing object that represents the stack frame might change over time
/// and instances of this object can track the logical object that refers to a
/// frame even if it does change.
///
/// These objects also don't keep execution objects around longer than they
/// should since they use weak pointers. For example if an object refers to a
/// stack frame and a stack frame is no longer in a thread, then a
/// ExecutionContextRef object that refers to that frame will not be able to
/// get a shared pointer to those objects since they are no longer around.
///
/// ExecutionContextRef objects can also be used as objects in classes that
/// want to track a "previous execution context". Since the weak references to
/// the execution objects (target, process, thread and frame) don't keep these
/// objects around, they are safe to keep around.
///
/// The general rule of thumb is all long lived objects that want to refer to
/// execution contexts should use ExecutionContextRef objects. The
/// ExecutionContext class is used to temporarily get shared pointers to any
/// execution context objects that are still around so they are guaranteed to
/// exist during a function that requires the objects. ExecutionContext
/// objects should NOT be used for long term storage since they will keep
/// objects alive with extra shared pointer references to these  objects.
//----------------------------------------------------------------------
class ExecutionContextRef {
public:
  //------------------------------------------------------------------
  /// Default Constructor.
  //------------------------------------------------------------------
  ExecutionContextRef();

  //------------------------------------------------------------------
  /// Copy Constructor.
  //------------------------------------------------------------------
  ExecutionContextRef(const ExecutionContextRef &rhs);

  //------------------------------------------------------------------
  /// Construct using an ExecutionContext object that might be nullptr.
  ///
  /// If \a exe_ctx_ptr is valid, then make weak references to any valid
  /// objects in the ExecutionContext, otherwise no weak references to any
  /// execution context objects will be made.
  //------------------------------------------------------------------
  ExecutionContextRef(const ExecutionContext *exe_ctx_ptr);

  //------------------------------------------------------------------
  /// Construct using an ExecutionContext object.
  ///
  /// Make weak references to any valid objects in the ExecutionContext.
  //------------------------------------------------------------------
  ExecutionContextRef(const ExecutionContext &exe_ctx);

  //------------------------------------------------------------------
  /// Construct using the target and all the selected items inside of it (the
  /// process and its selected thread, and the thread's selected frame). If
  /// there is no selected thread, default to the first thread If there is no
  /// selected frame, default to the first frame.
  //------------------------------------------------------------------
  ExecutionContextRef(Target *target, bool adopt_selected);

  //------------------------------------------------------------------
  /// Construct using an execution context scope.
  ///
  /// If the ExecutionContextScope object is valid and refers to a frame, make
  /// weak references too the frame, thread, process and target. If the
  /// ExecutionContextScope object is valid and refers to a thread, make weak
  /// references too the thread, process and target. If the
  /// ExecutionContextScope object is valid and refers to a process, make weak
  /// references too the process and target. If the ExecutionContextScope
  /// object is valid and refers to a target, make weak references too the
  /// target.
  //------------------------------------------------------------------
  ExecutionContextRef(ExecutionContextScope *exe_scope);

  //------------------------------------------------------------------
  /// Construct using an execution context scope.
  ///
  /// If the ExecutionContextScope object refers to a frame, make weak
  /// references too the frame, thread, process and target. If the
  /// ExecutionContextScope object refers to a thread, make weak references
  /// too the thread, process and target. If the ExecutionContextScope object
  /// refers to a process, make weak references too the process and target. If
  /// the ExecutionContextScope object refers to a target, make weak
  /// references too the target.
  //------------------------------------------------------------------
  ExecutionContextRef(ExecutionContextScope &exe_scope);

  ~ExecutionContextRef();

  //------------------------------------------------------------------
  /// Assignment operator
  ///
  /// Copy all weak references in \a rhs.
  //------------------------------------------------------------------
  ExecutionContextRef &operator=(const ExecutionContextRef &rhs);

  //------------------------------------------------------------------
  /// Assignment operator from a ExecutionContext
  ///
  /// Make weak references to any strongly referenced objects in \a exe_ctx.
  //------------------------------------------------------------------
  ExecutionContextRef &operator=(const ExecutionContext &exe_ctx);

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Sets the process and thread to nullptr, and the frame index to an
  /// invalid value.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Set accessor that creates a weak reference to the target referenced in
  /// \a target_sp.
  ///
  /// If \a target_sp is valid this object will create a weak reference to
  /// that object, otherwise any previous target weak reference contained in
  /// this object will be reset.
  ///
  /// Only the weak reference to the target will be updated, no other weak
  /// references will be modified. If you want this execution context to make
  /// a weak reference to the target's process, use the
  /// ExecutionContextRef::SetContext() functions.
  ///
  /// @see ExecutionContextRef::SetContext(const lldb::TargetSP &, bool)
  //------------------------------------------------------------------
  void SetTargetSP(const lldb::TargetSP &target_sp);

  //------------------------------------------------------------------
  /// Set accessor that creates a weak reference to the process referenced in
  /// \a process_sp.
  ///
  /// If \a process_sp is valid this object will create a weak reference to
  /// that object, otherwise any previous process weak reference contained in
  /// this object will be reset.
  ///
  /// Only the weak reference to the process will be updated, no other weak
  /// references will be modified. If you want this execution context to make
  /// a weak reference to the target, use the
  /// ExecutionContextRef::SetContext() functions.
  ///
  /// @see ExecutionContextRef::SetContext(const lldb::ProcessSP &)
  //------------------------------------------------------------------
  void SetProcessSP(const lldb::ProcessSP &process_sp);

  //------------------------------------------------------------------
  /// Set accessor that creates a weak reference to the thread referenced in
  /// \a thread_sp.
  ///
  /// If \a thread_sp is valid this object will create a weak reference to
  /// that object, otherwise any previous thread weak reference contained in
  /// this object will be reset.
  ///
  /// Only the weak reference to the thread will be updated, no other weak
  /// references will be modified. If you want this execution context to make
  /// a weak reference to the thread's process and target, use the
  /// ExecutionContextRef::SetContext() functions.
  ///
  /// @see ExecutionContextRef::SetContext(const lldb::ThreadSP &)
  //------------------------------------------------------------------
  void SetThreadSP(const lldb::ThreadSP &thread_sp);

  //------------------------------------------------------------------
  /// Set accessor that creates a weak reference to the frame referenced in \a
  /// frame_sp.
  ///
  /// If \a frame_sp is valid this object will create a weak reference to that
  /// object, otherwise any previous frame weak reference contained in this
  /// object will be reset.
  ///
  /// Only the weak reference to the frame will be updated, no other weak
  /// references will be modified. If you want this execution context to make
  /// a weak reference to the frame's thread, process and target, use the
  /// ExecutionContextRef::SetContext() functions.
  ///
  /// @see ExecutionContextRef::SetContext(const lldb::StackFrameSP &)
  //------------------------------------------------------------------
  void SetFrameSP(const lldb::StackFrameSP &frame_sp);

  void SetTargetPtr(Target *target, bool adopt_selected);

  void SetProcessPtr(Process *process);

  void SetThreadPtr(Thread *thread);

  void SetFramePtr(StackFrame *frame);

  //------------------------------------------------------------------
  /// Get accessor that creates a strong reference from the weak target
  /// reference contained in this object.
  ///
  /// @returns
  ///     A shared pointer to a target that is not guaranteed to be valid.
  //------------------------------------------------------------------
  lldb::TargetSP GetTargetSP() const;

  //------------------------------------------------------------------
  /// Get accessor that creates a strong reference from the weak process
  /// reference contained in this object.
  ///
  /// @returns
  ///     A shared pointer to a process that is not guaranteed to be valid.
  //------------------------------------------------------------------
  lldb::ProcessSP GetProcessSP() const;

  //------------------------------------------------------------------
  /// Get accessor that creates a strong reference from the weak thread
  /// reference contained in this object.
  ///
  /// @returns
  ///     A shared pointer to a thread that is not guaranteed to be valid.
  //------------------------------------------------------------------
  lldb::ThreadSP GetThreadSP() const;

  //------------------------------------------------------------------
  /// Get accessor that creates a strong reference from the weak frame
  /// reference contained in this object.
  ///
  /// @returns
  ///     A shared pointer to a frame that is not guaranteed to be valid.
  //------------------------------------------------------------------
  lldb::StackFrameSP GetFrameSP() const;

  //------------------------------------------------------------------
  /// Create an ExecutionContext object from this object.
  ///
  /// Create strong references to any execution context objects that are still
  /// valid. Any of the returned shared pointers in the ExecutionContext
  /// objects is not guaranteed to be valid. @returns
  ///     An execution context object that has strong references to
  ///     any valid weak references in this object.
  //------------------------------------------------------------------
  ExecutionContext Lock(bool thread_and_frame_only_if_stopped) const;

  //------------------------------------------------------------------
  /// Returns true if this object has a weak reference to a thread. The return
  /// value is only an indication of whether this object has a weak reference
  /// and does not indicate whether the weak reference is valid or not.
  //------------------------------------------------------------------
  bool HasThreadRef() const { return m_tid != LLDB_INVALID_THREAD_ID; }

  //------------------------------------------------------------------
  /// Returns true if this object has a weak reference to a frame. The return
  /// value is only an indication of whether this object has a weak reference
  /// and does not indicate whether the weak reference is valid or not.
  //------------------------------------------------------------------
  bool HasFrameRef() const { return m_stack_id.IsValid(); }

  void ClearThread() {
    m_thread_wp.reset();
    m_tid = LLDB_INVALID_THREAD_ID;
  }

  void ClearFrame() { m_stack_id.Clear(); }

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  lldb::TargetWP m_target_wp;         ///< A weak reference to a target
  lldb::ProcessWP m_process_wp;       ///< A weak reference to a process
  mutable lldb::ThreadWP m_thread_wp; ///< A weak reference to a thread
  lldb::tid_t m_tid;  ///< The thread ID that this object refers to in case the
                      ///backing object changes
  StackID m_stack_id; ///< The stack ID that this object refers to in case the
                      ///backing object changes
};

//----------------------------------------------------------------------
/// @class ExecutionContext ExecutionContext.h
/// "lldb/Target/ExecutionContext.h"
/// A class that contains an execution context.
///
/// This baton object can be passed into any function that requires a context
/// that specifies a target, process, thread and frame. These objects are
/// designed to be used for short term execution context object storage while
/// a function might be trying to evaluate something that requires a thread or
/// frame. ExecutionContextRef objects can be used to initialize one of these
/// objects to turn the weak execution context object references to the
/// target, process, thread and frame into strong references (shared pointers)
/// so that functions can guarantee that these objects won't go away in the
/// middle of a function.
///
/// ExecutionContext objects should be used as short lived objects (typically
/// on the stack) in order to lock down an execution context for local use and
/// for passing down to other functions that also require specific contexts.
/// They should NOT be used for long term storage, for long term storage use
/// ExecutionContextRef objects.
//----------------------------------------------------------------------
class ExecutionContext {
public:
  //------------------------------------------------------------------
  /// Default Constructor.
  //------------------------------------------------------------------
  ExecutionContext();

  //------------------------------------------------------------------
  // Copy constructor
  //------------------------------------------------------------------
  ExecutionContext(const ExecutionContext &rhs);

  //------------------------------------------------------------------
  // Adopt the target and optionally its current context.
  //------------------------------------------------------------------
  ExecutionContext(Target *t, bool fill_current_process_thread_frame = true);

  //------------------------------------------------------------------
  // Create execution contexts from shared pointers
  //------------------------------------------------------------------
  ExecutionContext(const lldb::TargetSP &target_sp, bool get_process);
  ExecutionContext(const lldb::ProcessSP &process_sp);
  ExecutionContext(const lldb::ThreadSP &thread_sp);
  ExecutionContext(const lldb::StackFrameSP &frame_sp);

  //------------------------------------------------------------------
  // Create execution contexts from weak pointers
  //------------------------------------------------------------------
  ExecutionContext(const lldb::TargetWP &target_wp, bool get_process);
  ExecutionContext(const lldb::ProcessWP &process_wp);
  ExecutionContext(const lldb::ThreadWP &thread_wp);
  ExecutionContext(const lldb::StackFrameWP &frame_wp);
  ExecutionContext(const ExecutionContextRef &exe_ctx_ref);
  ExecutionContext(const ExecutionContextRef *exe_ctx_ref,
                   bool thread_and_frame_only_if_stopped = false);

  // These two variants take in a locker, and grab the target, lock the API
  // mutex into locker, then fill in the rest of the shared pointers.
  ExecutionContext(const ExecutionContextRef &exe_ctx_ref,
                   std::unique_lock<std::recursive_mutex> &locker);
  ExecutionContext(const ExecutionContextRef *exe_ctx_ref,
                   std::unique_lock<std::recursive_mutex> &locker);
  //------------------------------------------------------------------
  // Create execution contexts from execution context scopes
  //------------------------------------------------------------------
  ExecutionContext(ExecutionContextScope *exe_scope);
  ExecutionContext(ExecutionContextScope &exe_scope);

  //------------------------------------------------------------------
  /// Construct with process, thread, and frame index.
  ///
  /// Initialize with process \a p, thread \a t, and frame index \a f.
  ///
  /// @param[in] process
  ///     The process for this execution context.
  ///
  /// @param[in] thread
  ///     The thread for this execution context.
  ///
  /// @param[in] frame
  ///     The frame index for this execution context.
  //------------------------------------------------------------------
  ExecutionContext(Process *process, Thread *thread = nullptr,
                   StackFrame *frame = nullptr);

  ~ExecutionContext();

  ExecutionContext &operator=(const ExecutionContext &rhs);

  bool operator==(const ExecutionContext &rhs) const;

  bool operator!=(const ExecutionContext &rhs) const;

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Sets the process and thread to nullptr, and the frame index to an
  /// invalid value.
  //------------------------------------------------------------------
  void Clear();

  RegisterContext *GetRegisterContext() const;

  ExecutionContextScope *GetBestExecutionContextScope() const;

  uint32_t GetAddressByteSize() const;

  lldb::ByteOrder GetByteOrder() const;

  //------------------------------------------------------------------
  /// Returns a pointer to the target object.
  ///
  /// The returned pointer might be nullptr. Calling HasTargetScope(),
  /// HasProcessScope(), HasThreadScope(), or HasFrameScope() can help to pre-
  /// validate this pointer so that this accessor can freely be used without
  /// having to check for nullptr each time.
  ///
  /// @see ExecutionContext::HasTargetScope() const @see
  /// ExecutionContext::HasProcessScope() const @see
  /// ExecutionContext::HasThreadScope() const @see
  /// ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  Target *GetTargetPtr() const;

  //------------------------------------------------------------------
  /// Returns a pointer to the process object.
  ///
  /// The returned pointer might be nullptr. Calling HasProcessScope(),
  /// HasThreadScope(), or HasFrameScope()  can help to pre-validate this
  /// pointer so that this accessor can freely be used without having to check
  /// for nullptr each time.
  ///
  /// @see ExecutionContext::HasProcessScope() const @see
  /// ExecutionContext::HasThreadScope() const @see
  /// ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  Process *GetProcessPtr() const;

  //------------------------------------------------------------------
  /// Returns a pointer to the thread object.
  ///
  /// The returned pointer might be nullptr. Calling HasThreadScope() or
  /// HasFrameScope() can help to pre-validate this pointer so that this
  /// accessor can freely be used without having to check for nullptr each
  /// time.
  ///
  /// @see ExecutionContext::HasThreadScope() const @see
  /// ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  Thread *GetThreadPtr() const { return m_thread_sp.get(); }

  //------------------------------------------------------------------
  /// Returns a pointer to the frame object.
  ///
  /// The returned pointer might be nullptr. Calling HasFrameScope(), can help
  /// to pre-validate this pointer so that this accessor can freely be used
  /// without having to check for nullptr each time.
  ///
  /// @see ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  StackFrame *GetFramePtr() const { return m_frame_sp.get(); }

  //------------------------------------------------------------------
  /// Returns a reference to the target object.
  ///
  /// Clients should call HasTargetScope(), HasProcessScope(),
  /// HasThreadScope(), or HasFrameScope() prior to calling this function to
  /// ensure that this ExecutionContext object contains a valid target.
  ///
  /// @see ExecutionContext::HasTargetScope() const @see
  /// ExecutionContext::HasProcessScope() const @see
  /// ExecutionContext::HasThreadScope() const @see
  /// ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  Target &GetTargetRef() const;

  //------------------------------------------------------------------
  /// Returns a reference to the process object.
  ///
  /// Clients should call HasProcessScope(), HasThreadScope(), or
  /// HasFrameScope() prior to calling this  function to ensure that this
  /// ExecutionContext object contains a valid target.
  ///
  /// @see ExecutionContext::HasProcessScope() const @see
  /// ExecutionContext::HasThreadScope() const @see
  /// ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  Process &GetProcessRef() const;

  //------------------------------------------------------------------
  /// Returns a reference to the thread object.
  ///
  /// Clients should call HasThreadScope(), or  HasFrameScope() prior to
  /// calling this  function to ensure that  this ExecutionContext object
  /// contains a valid target.
  ///
  /// @see ExecutionContext::HasThreadScope() const @see
  /// ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  Thread &GetThreadRef() const;

  //------------------------------------------------------------------
  /// Returns a reference to the thread object.
  ///
  /// Clients should call HasFrameScope() prior to calling this function to
  /// ensure that  this ExecutionContext object contains a valid target.
  ///
  /// @see ExecutionContext::HasFrameScope() const
  //------------------------------------------------------------------
  StackFrame &GetFrameRef() const;

  //------------------------------------------------------------------
  /// Get accessor to get the target shared pointer.
  ///
  /// The returned shared pointer is not guaranteed to be valid.
  //------------------------------------------------------------------
  const lldb::TargetSP &GetTargetSP() const { return m_target_sp; }

  //------------------------------------------------------------------
  /// Get accessor to get the process shared pointer.
  ///
  /// The returned shared pointer is not guaranteed to be valid.
  //------------------------------------------------------------------
  const lldb::ProcessSP &GetProcessSP() const { return m_process_sp; }

  //------------------------------------------------------------------
  /// Get accessor to get the thread shared pointer.
  ///
  /// The returned shared pointer is not guaranteed to be valid.
  //------------------------------------------------------------------
  const lldb::ThreadSP &GetThreadSP() const { return m_thread_sp; }

  //------------------------------------------------------------------
  /// Get accessor to get the frame shared pointer.
  ///
  /// The returned shared pointer is not guaranteed to be valid.
  //------------------------------------------------------------------
  const lldb::StackFrameSP &GetFrameSP() const { return m_frame_sp; }

  //------------------------------------------------------------------
  /// Set accessor to set only the target shared pointer.
  //------------------------------------------------------------------
  void SetTargetSP(const lldb::TargetSP &target_sp);

  //------------------------------------------------------------------
  /// Set accessor to set only the process shared pointer.
  //------------------------------------------------------------------
  void SetProcessSP(const lldb::ProcessSP &process_sp);

  //------------------------------------------------------------------
  /// Set accessor to set only the thread shared pointer.
  //------------------------------------------------------------------
  void SetThreadSP(const lldb::ThreadSP &thread_sp);

  //------------------------------------------------------------------
  /// Set accessor to set only the frame shared pointer.
  //------------------------------------------------------------------
  void SetFrameSP(const lldb::StackFrameSP &frame_sp);

  //------------------------------------------------------------------
  /// Set accessor to set only the target shared pointer from a target
  /// pointer.
  //------------------------------------------------------------------
  void SetTargetPtr(Target *target);

  //------------------------------------------------------------------
  /// Set accessor to set only the process shared pointer from a process
  /// pointer.
  //------------------------------------------------------------------
  void SetProcessPtr(Process *process);

  //------------------------------------------------------------------
  /// Set accessor to set only the thread shared pointer from a thread
  /// pointer.
  //------------------------------------------------------------------
  void SetThreadPtr(Thread *thread);

  //------------------------------------------------------------------
  /// Set accessor to set only the frame shared pointer from a frame pointer.
  //------------------------------------------------------------------
  void SetFramePtr(StackFrame *frame);

  //------------------------------------------------------------------
  // Set the execution context using a target shared pointer.
  //
  // If "target_sp" is valid, sets the target context to match and if
  // "get_process" is true, sets the process shared pointer if the target
  // currently has a process.
  //------------------------------------------------------------------
  void SetContext(const lldb::TargetSP &target_sp, bool get_process);

  //------------------------------------------------------------------
  // Set the execution context using a process shared pointer.
  //
  // If "process_sp" is valid, then set the process and target in this context.
  // Thread and frame contexts will be cleared. If "process_sp" is not valid,
  // all shared pointers are reset.
  //------------------------------------------------------------------
  void SetContext(const lldb::ProcessSP &process_sp);

  //------------------------------------------------------------------
  // Set the execution context using a thread shared pointer.
  //
  // If "thread_sp" is valid, then set the thread, process and target in this
  // context. The frame context will be cleared. If "thread_sp" is not valid,
  // all shared pointers are reset.
  //------------------------------------------------------------------
  void SetContext(const lldb::ThreadSP &thread_sp);

  //------------------------------------------------------------------
  // Set the execution context using a frame shared pointer.
  //
  // If "frame_sp" is valid, then set the frame, thread, process and target in
  // this context If "frame_sp" is not valid, all shared pointers are reset.
  //------------------------------------------------------------------
  void SetContext(const lldb::StackFrameSP &frame_sp);

  //------------------------------------------------------------------
  /// Returns true the ExecutionContext object contains a valid target.
  ///
  /// This function can be called after initializing an ExecutionContext
  /// object, and if it returns true, calls to GetTargetPtr() and
  /// GetTargetRef() do not need to be checked for validity.
  //------------------------------------------------------------------
  bool HasTargetScope() const;

  //------------------------------------------------------------------
  /// Returns true the ExecutionContext object contains a valid target and
  /// process.
  ///
  /// This function can be called after initializing an ExecutionContext
  /// object, and if it returns true, calls to GetTargetPtr() and
  /// GetTargetRef(), GetProcessPtr(), and GetProcessRef(), do not need to be
  /// checked for validity.
  //------------------------------------------------------------------
  bool HasProcessScope() const;

  //------------------------------------------------------------------
  /// Returns true the ExecutionContext object contains a valid target,
  /// process, and thread.
  ///
  /// This function can be called after initializing an ExecutionContext
  /// object, and if it returns true, calls to GetTargetPtr(), GetTargetRef(),
  /// GetProcessPtr(), GetProcessRef(), GetThreadPtr(), and GetThreadRef() do
  /// not need to be checked for validity.
  //------------------------------------------------------------------
  bool HasThreadScope() const;

  //------------------------------------------------------------------
  /// Returns true the ExecutionContext object contains a valid target,
  /// process, thread and frame.
  ///
  /// This function can be called after initializing an ExecutionContext
  /// object, and if it returns true, calls to GetTargetPtr(), GetTargetRef(),
  /// GetProcessPtr(), GetProcessRef(), GetThreadPtr(), GetThreadRef(),
  /// GetFramePtr(), and GetFrameRef() do not need to be checked for validity.
  //------------------------------------------------------------------
  bool HasFrameScope() const;

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  lldb::TargetSP m_target_sp; ///< The target that owns the process/thread/frame
  lldb::ProcessSP m_process_sp;  ///< The process that owns the thread/frame
  lldb::ThreadSP m_thread_sp;    ///< The thread that owns the frame
  lldb::StackFrameSP m_frame_sp; ///< The stack frame in thread.
};

} // namespace lldb_private

#endif // liblldb_ExecutionContext_h_
