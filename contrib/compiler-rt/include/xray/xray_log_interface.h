//===-- xray_log_interface.h ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
// APIs for installing a new logging implementation.
//
//===----------------------------------------------------------------------===//
///
/// XRay allows users to implement their own logging handlers and install them
/// to replace the default runtime-controllable implementation that comes with
/// compiler-rt/xray. The "flight data recorder" (FDR) mode implementation uses
/// this API to install itself in an XRay-enabled binary. See
/// compiler-rt/lib/xray_fdr_logging.{h,cc} for details of that implementation.
///
/// The high-level usage pattern for these APIs look like the following:
///
///   // We choose the mode which we'd like to install, and check whether this
///   // has succeeded. Each mode will have their own set of flags they will
///   // support, outside of the global XRay configuration options that are
///   // defined in the XRAY_OPTIONS environment variable.
///   auto select_status = __xray_log_select_mode("xray-fdr");
///   if (select_status != XRayLogRegisterStatus::XRAY_REGISTRATION_OK) {
///     // This failed, we should not proceed with attempting to initialise
///     // the currently selected mode.
///     return;
///   }
///
///   // Once that's done, we can now attempt to configure the implementation.
///   // To do this, we provide the string flags configuration for the mode.
///   auto config_status = __xray_log_init_mode(
///       "xray-fdr", "verbosity=1 some_flag=1 another_flag=2");
///   if (config_status != XRayLogInitStatus::XRAY_LOG_INITIALIZED) {
///     // deal with the error here, if there is one.
///   }
///
///   // When the log implementation has had the chance to initialize, we can
///   // now patch the instrumentation points. Note that we could have patched
///   // the instrumentation points first, but there's no strict ordering to
///   // these operations.
///   auto patch_status = __xray_patch();
///   if (patch_status != XRayPatchingStatus::SUCCESS) {
///     // deal with the error here, if it is an error.
///   }
///
///   // If we want to stop the implementation, we can then finalize it (before
///   // optionally flushing the log).
///   auto fin_status = __xray_log_finalize();
///   if (fin_status != XRayLogInitStatus::XRAY_LOG_FINALIZED) {
///     // deal with the error here, if it is an error.
///   }
///
///   // We can optionally wait before flushing the log to give other threads a
///   // chance to see that the implementation is already finalized. Also, at
///   // this point we can optionally unpatch the instrumentation points to
///   // reduce overheads at runtime.
///   auto unpatch_status = __xray_unpatch();
///   if (unpatch_status != XRayPatchingStatus::SUCCESS) {
///     // deal with the error here, if it is an error.
///   }
///
///   // If there are logs or data to be flushed somewhere, we can do so only
///   // after we've finalized the log. Some implementations may not actually
///   // have anything to log (it might keep the data in memory, or periodically
///   // be logging the data anyway).
///   auto flush_status = __xray_log_flushLog();
///   if (flush_status != XRayLogFlushStatus::XRAY_LOG_FLUSHED) {
///     // deal with the error here, if it is an error.
///   }
///
///   // Alternatively, we can go through the buffers ourselves without
///   // relying on the implementations' flushing semantics (if the
///   // implementation supports exporting this data directly).
///   auto MyBufferProcessor = +[](const char* mode, XRayBuffer buffer) {
///     // Check the "mode" to see if it's something we know how to handle...
///     // and/or do something with an XRayBuffer instance.
///   };
///   auto process_status = __xray_log_process_buffers(MyBufferProcessor);
///   if (process_status != XRayLogFlushStatus::XRAY_LOG_FLUSHED) {
///     // deal with the error here, if it is an error.
///   }
///
/// NOTE: Before calling __xray_patch() again, consider re-initializing the
/// implementation first. Some implementations might stay in an "off" state when
/// they are finalized, while some might be in an invalid/unknown state.
///
#ifndef XRAY_XRAY_LOG_INTERFACE_H
#define XRAY_XRAY_LOG_INTERFACE_H

#include "xray/xray_interface.h"
#include <stddef.h>

extern "C" {

/// This enum defines the valid states in which the logging implementation can
/// be at.
enum XRayLogInitStatus {
  /// The default state is uninitialized, and in case there were errors in the
  /// initialization, the implementation MUST return XRAY_LOG_UNINITIALIZED.
  XRAY_LOG_UNINITIALIZED = 0,

  /// Some implementations support multi-stage init (or asynchronous init), and
  /// may return XRAY_LOG_INITIALIZING to signal callers of the API that
  /// there's an ongoing initialization routine running. This allows
  /// implementations to support concurrent threads attempting to initialize,
  /// while only signalling success in one.
  XRAY_LOG_INITIALIZING = 1,

  /// When an implementation is done initializing, it MUST return
  /// XRAY_LOG_INITIALIZED. When users call `__xray_patch()`, they are
  /// guaranteed that the implementation installed with
  /// `__xray_set_log_impl(...)` has been initialized.
  XRAY_LOG_INITIALIZED = 2,

  /// Some implementations might support multi-stage finalization (or
  /// asynchronous finalization), and may return XRAY_LOG_FINALIZING to signal
  /// callers of the API that there's an ongoing finalization routine running.
  /// This allows implementations to support concurrent threads attempting to
  /// finalize, while only signalling success/completion in one.
  XRAY_LOG_FINALIZING = 3,

  /// When an implementation is done finalizing, it MUST return
  /// XRAY_LOG_FINALIZED. It is up to the implementation to determine what the
  /// semantics of a finalized implementation is. Some implementations might
  /// allow re-initialization once the log is finalized, while some might always
  /// be on (and that finalization is a no-op).
  XRAY_LOG_FINALIZED = 4,
};

/// This enum allows an implementation to signal log flushing operations via
/// `__xray_log_flushLog()`, and the state of flushing the log.
enum XRayLogFlushStatus {
  XRAY_LOG_NOT_FLUSHING = 0,
  XRAY_LOG_FLUSHING = 1,
  XRAY_LOG_FLUSHED = 2,
};

/// This enum indicates the installation state of a logging implementation, when
/// associating a mode to a particular logging implementation through
/// `__xray_log_register_impl(...)` or through `__xray_log_select_mode(...`.
enum XRayLogRegisterStatus {
  XRAY_REGISTRATION_OK = 0,
  XRAY_DUPLICATE_MODE = 1,
  XRAY_MODE_NOT_FOUND = 2,
  XRAY_INCOMPLETE_IMPL = 3,
};

/// A valid XRay logging implementation MUST provide all of the function
/// pointers in XRayLogImpl when being installed through `__xray_set_log_impl`.
/// To be precise, ALL the functions pointers MUST NOT be nullptr.
struct XRayLogImpl {
  /// The log initialization routine provided by the implementation, always
  /// provided with the following parameters:
  ///
  ///   - buffer size (unused)
  ///   - maximum number of buffers (unused)
  ///   - a pointer to an argument struct that the implementation MUST handle
  ///   - the size of the argument struct
  ///
  /// See XRayLogInitStatus for details on what the implementation MUST return
  /// when called.
  ///
  /// If the implementation needs to install handlers aside from the 0-argument
  /// function call handler, it MUST do so in this initialization handler.
  ///
  /// See xray_interface.h for available handler installation routines.
  XRayLogInitStatus (*log_init)(size_t, size_t, void *, size_t);

  /// The log finalization routine provided by the implementation.
  ///
  /// See XRayLogInitStatus for details on what the implementation MUST return
  /// when called.
  XRayLogInitStatus (*log_finalize)();

  /// The 0-argument function call handler. XRay logging implementations MUST
  /// always have a handler for function entry and exit events. In case the
  /// implementation wants to support arg1 (or other future extensions to XRay
  /// logging) those MUST be installed by the installed 'log_init' handler.
  ///
  /// Because we didn't want to change the ABI of this struct, the arg1 handler
  /// may be silently overwritten during initialization as well.
  void (*handle_arg0)(int32_t, XRayEntryType);

  /// The log implementation provided routine for when __xray_log_flushLog() is
  /// called.
  ///
  /// See XRayLogFlushStatus for details on what the implementation MUST return
  /// when called.
  XRayLogFlushStatus (*flush_log)();
};

/// DEPRECATED: Use the mode registration workflow instead with
/// __xray_log_register_mode(...) and __xray_log_select_mode(...). See the
/// documentation for those function.
///
/// This function installs a new logging implementation that XRay will use. In
/// case there are any nullptr members in Impl, XRay will *uninstall any
/// existing implementations*. It does NOT patch the instrumentation points.
///
/// NOTE: This function does NOT attempt to finalize the currently installed
/// implementation. Use with caution.
///
/// It is guaranteed safe to call this function in the following states:
///
///   - When the implementation is UNINITIALIZED.
///   - When the implementation is FINALIZED.
///   - When there is no current implementation installed.
///
/// It is logging implementation defined what happens when this function is
/// called while in any other states.
void __xray_set_log_impl(XRayLogImpl Impl);

/// This function registers a logging implementation against a "mode"
/// identifier. This allows multiple modes to be registered, and chosen at
/// runtime using the same mode identifier through
/// `__xray_log_select_mode(...)`.
///
/// We treat the Mode identifier as a null-terminated byte string, as the
/// identifier used when retrieving the log impl.
///
/// Returns:
///   - XRAY_REGISTRATION_OK on success.
///   - XRAY_DUPLICATE_MODE when an implementation is already associated with
///     the provided Mode; does not update the already-registered
///     implementation.
XRayLogRegisterStatus __xray_log_register_mode(const char *Mode,
                                               XRayLogImpl Impl);

/// This function selects the implementation associated with Mode that has been
/// registered through __xray_log_register_mode(...) and installs that
/// implementation (as if through calling __xray_set_log_impl(...)). The same
/// caveats apply to __xray_log_select_mode(...) as with
/// __xray_log_set_log_impl(...).
///
/// Returns:
///   - XRAY_REGISTRATION_OK on success.
///   - XRAY_MODE_NOT_FOUND if there is no implementation associated with Mode;
///     does not update the currently installed implementation.
XRayLogRegisterStatus __xray_log_select_mode(const char *Mode);

/// Returns an identifier for the currently selected XRay mode chosen through
/// the __xray_log_select_mode(...) function call. Returns nullptr if there is
/// no currently installed mode.
const char *__xray_log_get_current_mode();

/// This function removes the currently installed implementation. It will also
/// uninstall any handlers that have been previously installed. It does NOT
/// unpatch the instrumentation points.
///
/// NOTE: This function does NOT attempt to finalize the currently installed
/// implementation. Use with caution.
///
/// It is guaranteed safe to call this function in the following states:
///
///   - When the implementation is UNINITIALIZED.
///   - When the implementation is FINALIZED.
///   - When there is no current implementation installed.
///
/// It is logging implementation defined what happens when this function is
/// called while in any other states.
void __xray_remove_log_impl();

/// DEPRECATED: Use __xray_log_init_mode() instead, and provide all the options
/// in string form.
/// Invokes the installed implementation initialization routine. See
/// XRayLogInitStatus for what the return values mean.
XRayLogInitStatus __xray_log_init(size_t BufferSize, size_t MaxBuffers,
                                  void *Args, size_t ArgsSize);

/// Invokes the installed initialization routine, which *must* support the
/// string based form.
///
/// NOTE: When this API is used, we still invoke the installed initialization
/// routine, but we will call it with the following convention to signal that we
/// are using the string form:
///
/// - BufferSize = 0
/// - MaxBuffers = 0
/// - ArgsSize = 0
/// - Args will be the pointer to the character buffer representing the
///   configuration.
///
/// FIXME: Updating the XRayLogImpl struct is an ABI breaking change. When we
/// are ready to make a breaking change, we should clean this up appropriately.
XRayLogInitStatus __xray_log_init_mode(const char *Mode, const char *Config);

/// Like __xray_log_init_mode(...) this version allows for providing
/// configurations that might have non-null-terminated strings. This will
/// operate similarly to __xray_log_init_mode, with the exception that
/// |ArgsSize| will be what |ConfigSize| is.
XRayLogInitStatus __xray_log_init_mode_bin(const char *Mode, const char *Config,
                                           size_t ConfigSize);

/// Invokes the installed implementation finalization routine. See
/// XRayLogInitStatus for what the return values mean.
XRayLogInitStatus __xray_log_finalize();

/// Invokes the install implementation log flushing routine. See
/// XRayLogFlushStatus for what the return values mean.
XRayLogFlushStatus __xray_log_flushLog();

/// An XRayBuffer represents a section of memory which can be treated by log
/// processing functions as bytes stored in the logging implementation's
/// buffers.
struct XRayBuffer {
  const void *Data;
  size_t Size;
};

/// Registers an iterator function which takes an XRayBuffer argument, then
/// returns another XRayBuffer function representing the next buffer. When the
/// Iterator function returns an empty XRayBuffer (Data = nullptr, Size = 0),
/// this signifies the end of the buffers.
///
/// The first invocation of this Iterator function will always take an empty
/// XRayBuffer (Data = nullptr, Size = 0).
void __xray_log_set_buffer_iterator(XRayBuffer (*Iterator)(XRayBuffer));

/// Removes the currently registered buffer iterator function.
void __xray_log_remove_buffer_iterator();

/// Invokes the provided handler to process data maintained by the logging
/// handler. This API will be provided raw access to the data available in
/// memory from the logging implementation. The callback function must:
///
/// 1) Not modify the data, to avoid running into undefined behaviour.
///
/// 2) Either know the data layout, or treat the data as raw bytes for later
///    interpretation.
///
/// This API is best used in place of the `__xray_log_flushLog()` implementation
/// above to enable the caller to provide an alternative means of extracting the
/// data from the XRay implementation.
///
/// Implementations MUST then provide:
///
/// 1) A function that will return an XRayBuffer. Functions that return an
///    "empty" XRayBuffer signifies that there are no more buffers to be
///    processed. This function should be registered through the
///    `__xray_log_set_buffer_iterator(...)` function.
///
/// 2) Its own means of converting data it holds in memory into an XRayBuffer
///    structure.
///
/// See XRayLogFlushStatus for what the return values mean.
///
XRayLogFlushStatus __xray_log_process_buffers(void (*Processor)(const char *,
                                                                XRayBuffer));

} // extern "C"

#endif // XRAY_XRAY_LOG_INTERFACE_H
