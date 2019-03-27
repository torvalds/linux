/*===-- llvm-c/OptRemarks.h - OptRemarks Public C Interface -------*- C -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a public interface to an opt-remark library.          *|
|* LLVM provides an implementation of this interface.                         *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_OPT_REMARKS_H
#define LLVM_C_OPT_REMARKS_H

#include "llvm-c/Core.h"
#include "llvm-c/Types.h"
#ifdef __cplusplus
#include <cstddef>
extern "C" {
#else
#include <stddef.h>
#endif /* !defined(__cplusplus) */

/**
 * @defgroup LLVMCOPTREMARKS OptRemarks
 * @ingroup LLVMC
 *
 * @{
 */

#define OPT_REMARKS_API_VERSION 0

/**
 * String containing a buffer and a length. The buffer is not guaranteed to be
 * zero-terminated.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
typedef struct {
  const char *Str;
  uint32_t Len;
} LLVMOptRemarkStringRef;

/**
 * DebugLoc containing File, Line and Column.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
typedef struct {
  // File:
  LLVMOptRemarkStringRef SourceFile;
  // Line:
  uint32_t SourceLineNumber;
  // Column:
  uint32_t SourceColumnNumber;
} LLVMOptRemarkDebugLoc;

/**
 * Element of the "Args" list. The key might give more information about what
 * are the semantics of the value, e.g. "Callee" will tell you that the value
 * is a symbol that names a function.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
typedef struct {
  // e.g. "Callee"
  LLVMOptRemarkStringRef Key;
  // e.g. "malloc"
  LLVMOptRemarkStringRef Value;

  // "DebugLoc": Optional
  LLVMOptRemarkDebugLoc DebugLoc;
} LLVMOptRemarkArg;

/**
 * One remark entry.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
typedef struct {
  // e.g. !Missed, !Passed
  LLVMOptRemarkStringRef RemarkType;
  // "Pass": Required
  LLVMOptRemarkStringRef PassName;
  // "Name": Required
  LLVMOptRemarkStringRef RemarkName;
  // "Function": Required
  LLVMOptRemarkStringRef FunctionName;

  // "DebugLoc": Optional
  LLVMOptRemarkDebugLoc DebugLoc;
  // "Hotness": Optional
  uint32_t Hotness;
  // "Args": Optional. It is an array of `num_args` elements.
  uint32_t NumArgs;
  LLVMOptRemarkArg *Args;
} LLVMOptRemarkEntry;

typedef struct LLVMOptRemarkOpaqueParser *LLVMOptRemarkParserRef;

/**
 * Creates a remark parser that can be used to read and parse the buffer located
 * in \p Buf of size \p Size.
 *
 * \p Buf cannot be NULL.
 *
 * This function should be paired with LLVMOptRemarkParserDispose() to avoid
 * leaking resources.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
extern LLVMOptRemarkParserRef LLVMOptRemarkParserCreate(const void *Buf,
                                                        uint64_t Size);

/**
 * Returns the next remark in the file.
 *
 * The value pointed to by the return value is invalidated by the next call to
 * LLVMOptRemarkParserGetNext().
 *
 * If the parser reaches the end of the buffer, the return value will be NULL.
 *
 * In the case of an error, the return value will be NULL, and:
 *
 * 1) LLVMOptRemarkParserHasError() will return `1`.
 *
 * 2) LLVMOptRemarkParserGetErrorMessage() will return a descriptive error
 *    message.
 *
 * An error may occur if:
 *
 * 1) An argument is invalid.
 *
 * 2) There is a YAML parsing error. This type of error aborts parsing
 *    immediately and returns `1`. It can occur on malformed YAML.
 *
 * 3) Remark parsing error. If this type of error occurs, the parser won't call
 *    the handler and will continue to the next one. It can occur on malformed
 *    remarks, like missing or extra fields in the file.
 *
 * Here is a quick example of the usage:
 *
 * ```
 *  LLVMOptRemarkParserRef Parser = LLVMOptRemarkParserCreate(Buf, Size);
 *  LLVMOptRemarkEntry *Remark = NULL;
 *  while ((Remark == LLVMOptRemarkParserGetNext(Parser))) {
 *    // use Remark
 *  }
 *  bool HasError = LLVMOptRemarkParserHasError(Parser);
 *  LLVMOptRemarkParserDispose(Parser);
 * ```
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
extern LLVMOptRemarkEntry *
LLVMOptRemarkParserGetNext(LLVMOptRemarkParserRef Parser);

/**
 * Returns `1` if the parser encountered an error while parsing the buffer.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
extern LLVMBool LLVMOptRemarkParserHasError(LLVMOptRemarkParserRef Parser);

/**
 * Returns a null-terminated string containing an error message.
 *
 * In case of no error, the result is `NULL`.
 *
 * The memory of the string is bound to the lifetime of \p Parser. If
 * LLVMOptRemarkParserDispose() is called, the memory of the string will be
 * released.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
extern const char *
LLVMOptRemarkParserGetErrorMessage(LLVMOptRemarkParserRef Parser);

/**
 * Releases all the resources used by \p Parser.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
extern void LLVMOptRemarkParserDispose(LLVMOptRemarkParserRef Parser);

/**
 * Returns the version of the opt-remarks dylib.
 *
 * \since OPT_REMARKS_API_VERSION=0
 */
extern uint32_t LLVMOptRemarkVersion(void);

/**
 * @} // endgoup LLVMCOPTREMARKS
 */

#ifdef __cplusplus
}
#endif /* !defined(__cplusplus) */

#endif /* LLVM_C_OPT_REMARKS_H */
