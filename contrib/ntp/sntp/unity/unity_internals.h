/* ==========================================
    Unity Project - A Test Framework for C
    Copyright (c) 2007-14 Mike Karlesky, Mark VanderVoord, Greg Williams
    [Released under MIT License. Please refer to license.txt for details]
========================================== */

#ifndef UNITY_INTERNALS_H
#define UNITY_INTERNALS_H

#ifdef UNITY_INCLUDE_CONFIG_H
#include "unity_config.h"
#endif

#include <setjmp.h>

// Unity Attempts to Auto-Detect Integer Types
// Attempt 1: UINT_MAX, ULONG_MAX, etc in <stdint.h>
// Attempt 2: UINT_MAX, ULONG_MAX, etc in <limits.h>
// Attempt 3: Deduced from sizeof() macros
#ifndef UNITY_EXCLUDE_STDINT_H
#include <stdint.h>
#endif

#ifndef UNITY_EXCLUDE_LIMITS_H
#include <limits.h>
#endif

#ifndef UNITY_EXCLUDE_SIZEOF
#ifndef UINT_MAX
#define UINT_MAX     (sizeof(unsigned int) * 256 - 1)
#endif
#ifndef ULONG_MAX
#define ULONG_MAX    (sizeof(unsigned long) * 256 - 1)
#endif
#ifndef UINTPTR_MAX
//apparently this is not a constant expression: (sizeof(unsigned int *) * 256 - 1) so we have to just let this fall through
#endif
#endif
//-------------------------------------------------------
// Guess Widths If Not Specified
//-------------------------------------------------------

// Determine the size of an int, if not already specificied.
// We cannot use sizeof(int), because it is not yet defined
// at this stage in the trnslation of the C program.
// Therefore, infer it from UINT_MAX if possible.
#ifndef UNITY_INT_WIDTH
  #ifdef UINT_MAX
    #if (UINT_MAX == 0xFFFF)
      #define UNITY_INT_WIDTH (16)
    #elif (UINT_MAX == 0xFFFFFFFF)
      #define UNITY_INT_WIDTH (32)
    #elif (UINT_MAX == 0xFFFFFFFFFFFFFFFF)
      #define UNITY_INT_WIDTH (64)
    #endif
  #endif
#endif
#ifndef UNITY_INT_WIDTH
  #define UNITY_INT_WIDTH (32)
#endif

// Determine the size of a long, if not already specified,
// by following the process used above to define
// UNITY_INT_WIDTH.
#ifndef UNITY_LONG_WIDTH
  #ifdef ULONG_MAX
    #if (ULONG_MAX == 0xFFFF)
      #define UNITY_LONG_WIDTH (16)
    #elif (ULONG_MAX == 0xFFFFFFFF)
      #define UNITY_LONG_WIDTH (32)
    #elif (ULONG_MAX == 0xFFFFFFFFFFFFFFFF)
      #define UNITY_LONG_WIDTH (64)
    #endif
  #endif
#endif
#ifndef UNITY_LONG_WIDTH
  #define UNITY_LONG_WIDTH (32)
#endif

// Determine the size of a pointer, if not already specified,
// by following the process used above to define
// UNITY_INT_WIDTH.
#ifndef UNITY_POINTER_WIDTH
  #ifdef UINTPTR_MAX
    #if (UINTPTR_MAX+0 <= 0xFFFF)
      #define UNITY_POINTER_WIDTH (16)
    #elif (UINTPTR_MAX+0 <= 0xFFFFFFFF)
      #define UNITY_POINTER_WIDTH (32)
    #elif (UINTPTR_MAX+0 <= 0xFFFFFFFFFFFFFFFF)
      #define UNITY_POINTER_WIDTH (64)
    #endif
  #endif
#endif
#ifndef UNITY_POINTER_WIDTH
  #ifdef INTPTR_MAX
    #if (INTPTR_MAX+0 <= 0x7FFF)
      #define UNITY_POINTER_WIDTH (16)
    #elif (INTPTR_MAX+0 <= 0x7FFFFFFF)
      #define UNITY_POINTER_WIDTH (32)
    #elif (INTPTR_MAX+0 <= 0x7FFFFFFFFFFFFFFF)
      #define UNITY_POINTER_WIDTH (64)
    #endif
  #endif
#endif
#ifndef UNITY_POINTER_WIDTH
  #define UNITY_POINTER_WIDTH UNITY_LONG_WIDTH
#endif

//-------------------------------------------------------
// Int Support (Define types based on detected sizes)
//-------------------------------------------------------

#if (UNITY_INT_WIDTH == 32)
    typedef unsigned char   _UU8;
    typedef unsigned short  _UU16;
    typedef unsigned int    _UU32;
    typedef signed char     _US8;
    typedef signed short    _US16;
    typedef signed int      _US32;
#elif (UNITY_INT_WIDTH == 16)
    typedef unsigned char   _UU8;
    typedef unsigned int    _UU16;
    typedef unsigned long   _UU32;
    typedef signed char     _US8;
    typedef signed int      _US16;
    typedef signed long     _US32;
#else
    #error Invalid UNITY_INT_WIDTH specified! (16 or 32 are supported)
#endif

//-------------------------------------------------------
// 64-bit Support
//-------------------------------------------------------

#ifndef UNITY_SUPPORT_64
#if UNITY_LONG_WIDTH > 32
#define UNITY_SUPPORT_64
#endif
#endif
#ifndef UNITY_SUPPORT_64
#if UNITY_POINTER_WIDTH > 32
#define UNITY_SUPPORT_64
#endif
#endif

#ifndef UNITY_SUPPORT_64

//No 64-bit Support
typedef _UU32 _U_UINT;
typedef _US32 _U_SINT;

#else

//64-bit Support
#if (UNITY_LONG_WIDTH == 32)
    typedef unsigned long long _UU64;
    typedef signed long long   _US64;
#elif (UNITY_LONG_WIDTH == 64)
    typedef unsigned long      _UU64;
    typedef signed long        _US64;
#else
    #error Invalid UNITY_LONG_WIDTH specified! (32 or 64 are supported)
#endif
typedef _UU64 _U_UINT;
typedef _US64 _U_SINT;

#endif

//-------------------------------------------------------
// Pointer Support
//-------------------------------------------------------

#if (UNITY_POINTER_WIDTH == 32)
    typedef _UU32 _UP;
#define UNITY_DISPLAY_STYLE_POINTER UNITY_DISPLAY_STYLE_HEX32
#elif (UNITY_POINTER_WIDTH == 64)
    typedef _UU64 _UP;
#define UNITY_DISPLAY_STYLE_POINTER UNITY_DISPLAY_STYLE_HEX64
#elif (UNITY_POINTER_WIDTH == 16)
    typedef _UU16 _UP;
#define UNITY_DISPLAY_STYLE_POINTER UNITY_DISPLAY_STYLE_HEX16
#else
    #error Invalid UNITY_POINTER_WIDTH specified! (16, 32 or 64 are supported)
#endif

#ifndef UNITY_PTR_ATTRIBUTE
  #define UNITY_PTR_ATTRIBUTE
#endif

//-------------------------------------------------------
// Float Support
//-------------------------------------------------------

#ifdef UNITY_EXCLUDE_FLOAT

//No Floating Point Support
#undef UNITY_INCLUDE_FLOAT
#undef UNITY_FLOAT_PRECISION
#undef UNITY_FLOAT_TYPE
#undef UNITY_FLOAT_VERBOSE

#else

#ifndef UNITY_INCLUDE_FLOAT
#define UNITY_INCLUDE_FLOAT
#endif

//Floating Point Support
#ifndef UNITY_FLOAT_PRECISION
#define UNITY_FLOAT_PRECISION (0.00001f)
#endif
#ifndef UNITY_FLOAT_TYPE
#define UNITY_FLOAT_TYPE float
#endif
typedef UNITY_FLOAT_TYPE _UF;

#endif

//-------------------------------------------------------
// Double Float Support
//-------------------------------------------------------

//unlike FLOAT, we DON'T include by default
#ifndef UNITY_EXCLUDE_DOUBLE
#ifndef UNITY_INCLUDE_DOUBLE
#define UNITY_EXCLUDE_DOUBLE
#endif
#endif

#ifdef UNITY_EXCLUDE_DOUBLE

//No Floating Point Support
#undef UNITY_DOUBLE_PRECISION
#undef UNITY_DOUBLE_TYPE
#undef UNITY_DOUBLE_VERBOSE

#ifdef UNITY_INCLUDE_DOUBLE
#undef UNITY_INCLUDE_DOUBLE
#endif

#else

//Double Floating Point Support
#ifndef UNITY_DOUBLE_PRECISION
#define UNITY_DOUBLE_PRECISION (1e-12f)
#endif
#ifndef UNITY_DOUBLE_TYPE
#define UNITY_DOUBLE_TYPE double
#endif
typedef UNITY_DOUBLE_TYPE _UD;

#endif

#ifdef UNITY_DOUBLE_VERBOSE
#ifndef UNITY_FLOAT_VERBOSE
#define UNITY_FLOAT_VERBOSE
#endif
#endif

//-------------------------------------------------------
// Output Method: stdout (DEFAULT)
//-------------------------------------------------------
#ifndef UNITY_OUTPUT_CHAR
//Default to using putchar, which is defined in stdio.h
#include <stdio.h>
#define UNITY_OUTPUT_CHAR(a) putchar(a)
#else
//If defined as something else, make sure we declare it here so it's ready for use
extern int UNITY_OUTPUT_CHAR(int);
#endif

#ifndef UNITY_OUTPUT_START
#define UNITY_OUTPUT_START()
#endif

#ifndef UNITY_OUTPUT_COMPLETE
#define UNITY_OUTPUT_COMPLETE()
#endif

//-------------------------------------------------------
// Footprint
//-------------------------------------------------------

#ifndef UNITY_LINE_TYPE
#define UNITY_LINE_TYPE _U_UINT
#endif

#ifndef UNITY_COUNTER_TYPE
#define UNITY_COUNTER_TYPE _U_UINT
#endif

//-------------------------------------------------------
// Language Features Available
//-------------------------------------------------------
#if !defined(UNITY_WEAK_ATTRIBUTE) && !defined(UNITY_WEAK_PRAGMA)
#   ifdef __GNUC__ // includes clang
#       if !(defined(__WIN32__) && defined(__clang__))
#           define UNITY_WEAK_ATTRIBUTE __attribute__((weak))
#       endif
#   endif
#endif

#ifdef UNITY_NO_WEAK
#   undef UNITY_WEAK_ATTRIBUTE
#   undef UNITY_WEAK_PRAGMA
#endif

#if !defined(UNITY_NORETURN_ATTRIBUTE)
#   ifdef __GNUC__ // includes clang
#       if !(defined(__WIN32__) && defined(__clang__))
#           define UNITY_NORETURN_ATTRIBUTE __attribute__((noreturn))
#       endif
#   endif
#endif

#ifndef UNITY_NORETURN_ATTRIBUTE
#   define UNITY_NORETURN_ATTRIBUTE
#endif


//-------------------------------------------------------
// Internal Structs Needed
//-------------------------------------------------------

typedef void (*UnityTestFunction)(void);

#define UNITY_DISPLAY_RANGE_INT  (0x10)
#define UNITY_DISPLAY_RANGE_UINT (0x20)
#define UNITY_DISPLAY_RANGE_HEX  (0x40)
#define UNITY_DISPLAY_RANGE_AUTO (0x80)

typedef enum
{
#if (UNITY_INT_WIDTH == 16)
    UNITY_DISPLAY_STYLE_INT      = 2 + UNITY_DISPLAY_RANGE_INT + UNITY_DISPLAY_RANGE_AUTO,
#elif (UNITY_INT_WIDTH  == 32)
    UNITY_DISPLAY_STYLE_INT      = 4 + UNITY_DISPLAY_RANGE_INT + UNITY_DISPLAY_RANGE_AUTO,
#elif (UNITY_INT_WIDTH  == 64)
    UNITY_DISPLAY_STYLE_INT      = 8 + UNITY_DISPLAY_RANGE_INT + UNITY_DISPLAY_RANGE_AUTO,
#endif
    UNITY_DISPLAY_STYLE_INT8     = 1 + UNITY_DISPLAY_RANGE_INT,
    UNITY_DISPLAY_STYLE_INT16    = 2 + UNITY_DISPLAY_RANGE_INT,
    UNITY_DISPLAY_STYLE_INT32    = 4 + UNITY_DISPLAY_RANGE_INT,
#ifdef UNITY_SUPPORT_64
    UNITY_DISPLAY_STYLE_INT64    = 8 + UNITY_DISPLAY_RANGE_INT,
#endif

#if (UNITY_INT_WIDTH == 16)
    UNITY_DISPLAY_STYLE_UINT     = 2 + UNITY_DISPLAY_RANGE_UINT + UNITY_DISPLAY_RANGE_AUTO,
#elif (UNITY_INT_WIDTH  == 32)
    UNITY_DISPLAY_STYLE_UINT     = 4 + UNITY_DISPLAY_RANGE_UINT + UNITY_DISPLAY_RANGE_AUTO,
#elif (UNITY_INT_WIDTH  == 64)
    UNITY_DISPLAY_STYLE_UINT     = 8 + UNITY_DISPLAY_RANGE_UINT + UNITY_DISPLAY_RANGE_AUTO,
#endif
    UNITY_DISPLAY_STYLE_UINT8    = 1 + UNITY_DISPLAY_RANGE_UINT,
    UNITY_DISPLAY_STYLE_UINT16   = 2 + UNITY_DISPLAY_RANGE_UINT,
    UNITY_DISPLAY_STYLE_UINT32   = 4 + UNITY_DISPLAY_RANGE_UINT,
#ifdef UNITY_SUPPORT_64
    UNITY_DISPLAY_STYLE_UINT64   = 8 + UNITY_DISPLAY_RANGE_UINT,
#endif
    UNITY_DISPLAY_STYLE_HEX8     = 1 + UNITY_DISPLAY_RANGE_HEX,
    UNITY_DISPLAY_STYLE_HEX16    = 2 + UNITY_DISPLAY_RANGE_HEX,
    UNITY_DISPLAY_STYLE_HEX32    = 4 + UNITY_DISPLAY_RANGE_HEX,
#ifdef UNITY_SUPPORT_64
    UNITY_DISPLAY_STYLE_HEX64    = 8 + UNITY_DISPLAY_RANGE_HEX,
#endif
    UNITY_DISPLAY_STYLE_UNKNOWN
} UNITY_DISPLAY_STYLE_T;

#ifndef UNITY_EXCLUDE_FLOAT
typedef enum _UNITY_FLOAT_TRAIT_T
{
    UNITY_FLOAT_IS_NOT_INF       = 0,
    UNITY_FLOAT_IS_INF,
    UNITY_FLOAT_IS_NOT_NEG_INF,
    UNITY_FLOAT_IS_NEG_INF,
    UNITY_FLOAT_IS_NOT_NAN,
    UNITY_FLOAT_IS_NAN,
    UNITY_FLOAT_IS_NOT_DET,
    UNITY_FLOAT_IS_DET,
} UNITY_FLOAT_TRAIT_T;
#endif

struct _Unity
{
    const char* TestFile;
    const char* CurrentTestName;
    UNITY_LINE_TYPE CurrentTestLineNumber;
    UNITY_COUNTER_TYPE NumberOfTests;
    UNITY_COUNTER_TYPE TestFailures;
    UNITY_COUNTER_TYPE TestIgnores;
    UNITY_COUNTER_TYPE CurrentTestFailed;
    UNITY_COUNTER_TYPE CurrentTestIgnored;
    jmp_buf AbortFrame;
	int isExpectingFail;
	UNITY_COUNTER_TYPE TestXFAILS;
	UNITY_COUNTER_TYPE TestPasses;
	UNITY_COUNTER_TYPE TestXPASSES;
	const char* XFAILMessage;
};

extern struct _Unity Unity;

//-------------------------------------------------------
// Test Suite Management
//-------------------------------------------------------

void UnityBegin(const char* filename);
int  UnityEnd(void);
void UnityConcludeTest(void);
void UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, const int FuncLineNum);

//-------------------------------------------------------
// Test Output
//-------------------------------------------------------

void UnityPrint(const char* string);
void UnityPrintMask(const _U_UINT mask, const _U_UINT number);
void UnityPrintNumberByStyle(const _U_SINT number, const UNITY_DISPLAY_STYLE_T style);
void UnityPrintNumber(const _U_SINT number);
void UnityPrintNumberUnsigned(const _U_UINT number);
void UnityPrintNumberHex(const _U_UINT number, const char nibbles);

#ifdef UNITY_FLOAT_VERBOSE
void UnityPrintFloat(const _UF number);
#endif

//-------------------------------------------------------
// Test Assertion Fuctions
//-------------------------------------------------------
//  Use the macros below this section instead of calling
//  these directly. The macros have a consistent naming
//  convention and will pull in file and line information
//  for you.

void UnityAssertEqualNumber(const _U_SINT expected,
                            const _U_SINT actual,
                            const char* msg,
                            const UNITY_LINE_TYPE lineNumber,
                            const UNITY_DISPLAY_STYLE_T style);

void UnityAssertEqualIntArray(UNITY_PTR_ATTRIBUTE const void* expected,
                              UNITY_PTR_ATTRIBUTE const void* actual,
                              const _UU32 num_elements,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber,
                              const UNITY_DISPLAY_STYLE_T style);

void UnityAssertBits(const _U_SINT mask,
                     const _U_SINT expected,
                     const _U_SINT actual,
                     const char* msg,
                     const UNITY_LINE_TYPE lineNumber);

void UnityAssertEqualString(const char* expected,
                            const char* actual,
                            const char* msg,
                            const UNITY_LINE_TYPE lineNumber);

void UnityAssertEqualStringArray( const char** expected,
                                  const char** actual,
                                  const _UU32 num_elements,
                                  const char* msg,
                                  const UNITY_LINE_TYPE lineNumber);

void UnityAssertEqualMemory( UNITY_PTR_ATTRIBUTE const void* expected,
                             UNITY_PTR_ATTRIBUTE const void* actual,
                             const _UU32 length,
                             const _UU32 num_elements,
                             const char* msg,
                             const UNITY_LINE_TYPE lineNumber);

void UnityAssertNumbersWithin(const _U_SINT delta,
                              const _U_SINT expected,
                              const _U_SINT actual,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber,
                              const UNITY_DISPLAY_STYLE_T style);

void UnityFail(const char* message, const UNITY_LINE_TYPE line) UNITY_NORETURN_ATTRIBUTE;

void UnityIgnore(const char* message, const UNITY_LINE_TYPE line);

#ifndef UNITY_EXCLUDE_FLOAT
void UnityAssertFloatsWithin(const _UF delta,
                             const _UF expected,
                             const _UF actual,
                             const char* msg,
                             const UNITY_LINE_TYPE lineNumber);

void UnityAssertEqualFloatArray(UNITY_PTR_ATTRIBUTE const _UF* expected,
                                UNITY_PTR_ATTRIBUTE const _UF* actual,
                                const _UU32 num_elements,
                                const char* msg,
                                const UNITY_LINE_TYPE lineNumber);

void UnityAssertFloatSpecial(const _UF actual,
                             const char* msg,
                             const UNITY_LINE_TYPE lineNumber,
                             const UNITY_FLOAT_TRAIT_T style);
#endif

#ifndef UNITY_EXCLUDE_DOUBLE
void UnityAssertDoublesWithin(const _UD delta,
                              const _UD expected,
                              const _UD actual,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber);

void UnityAssertEqualDoubleArray(UNITY_PTR_ATTRIBUTE const _UD* expected,
                                 UNITY_PTR_ATTRIBUTE const _UD* actual,
                                 const _UU32 num_elements,
                                 const char* msg,
                                 const UNITY_LINE_TYPE lineNumber);

void UnityAssertDoubleSpecial(const _UD actual,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber,
                              const UNITY_FLOAT_TRAIT_T style);
#endif

//-------------------------------------------------------
// Error Strings We Might Need
//-------------------------------------------------------

extern const char UnityStrErrFloat[];
extern const char UnityStrErrDouble[];
extern const char UnityStrErr64[];

//-------------------------------------------------------
// Test Running Macros
//-------------------------------------------------------

#define TEST_PROTECT() (setjmp(Unity.AbortFrame) == 0)

#define TEST_ABORT() {longjmp(Unity.AbortFrame, 1);}

//This tricky series of macros gives us an optional line argument to treat it as RUN_TEST(func, num=__LINE__)
#ifndef RUN_TEST
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 199901L
#define RUN_TEST(...) UnityDefaultTestRun(RUN_TEST_FIRST(__VA_ARGS__), RUN_TEST_SECOND(__VA_ARGS__))
#define RUN_TEST_FIRST(...) RUN_TEST_FIRST_HELPER(__VA_ARGS__, throwaway)
#define RUN_TEST_FIRST_HELPER(first,...) first, #first
#define RUN_TEST_SECOND(...) RUN_TEST_SECOND_HELPER(__VA_ARGS__, __LINE__, throwaway)
#define RUN_TEST_SECOND_HELPER(first,second,...) second
#endif
#endif
#endif

//If we can't do the tricky version, we'll just have to require them to always include the line number
#ifndef RUN_TEST
#ifdef CMOCK
#define RUN_TEST(func, num) UnityDefaultTestRun(func, #func, num)
#else
#define RUN_TEST(func) UnityDefaultTestRun(func, #func, __LINE__)
#endif
#endif

#define TEST_LINE_NUM (Unity.CurrentTestLineNumber)
#define TEST_IS_IGNORED (Unity.CurrentTestIgnored)
#define UNITY_NEW_TEST(a) \
    Unity.CurrentTestName = a; \
    Unity.CurrentTestLineNumber = (UNITY_LINE_TYPE)(__LINE__); \
    Unity.NumberOfTests++;

#ifndef UNITY_BEGIN
#define UNITY_BEGIN() UnityBegin(__FILE__)
#endif

#ifndef UNITY_END
#define UNITY_END() UnityEnd()
#endif

//-------------------------------------------------------
// Basic Fail and Ignore
//-------------------------------------------------------

#define UNITY_TEST_FAIL(line, message)   UnityFail(   (message), (UNITY_LINE_TYPE)line);
#define UNITY_TEST_IGNORE(line, message) UnityIgnore( (message), (UNITY_LINE_TYPE)line);

//-------------------------------------------------------
// Test Asserts
//-------------------------------------------------------

#define UNITY_TEST_ASSERT(condition, line, message)                                              if (condition) {} else {UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, message);}
#define UNITY_TEST_ASSERT_NULL(pointer, line, message)                                           UNITY_TEST_ASSERT(((pointer) == NULL),  (UNITY_LINE_TYPE)line, message)
#define UNITY_TEST_ASSERT_NOT_NULL(pointer, line, message)                                       UNITY_TEST_ASSERT(((pointer) != NULL),  (UNITY_LINE_TYPE)line, message)

#define UNITY_TEST_ASSERT_EQUAL_INT(expected, actual, line, message)                             UnityAssertEqualNumber((_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT)
#define UNITY_TEST_ASSERT_EQUAL_INT8(expected, actual, line, message)                            UnityAssertEqualNumber((_U_SINT)(_US8 )(expected), (_U_SINT)(_US8 )(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT8)
#define UNITY_TEST_ASSERT_EQUAL_INT16(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(_US16)(expected), (_U_SINT)(_US16)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT16)
#define UNITY_TEST_ASSERT_EQUAL_INT32(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(_US32)(expected), (_U_SINT)(_US32)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT32)
#define UNITY_TEST_ASSERT_EQUAL_UINT(expected, actual, line, message)                            UnityAssertEqualNumber((_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT)
#define UNITY_TEST_ASSERT_EQUAL_UINT8(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(_UU8 )(expected), (_U_SINT)(_UU8 )(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT8)
#define UNITY_TEST_ASSERT_EQUAL_UINT16(expected, actual, line, message)                          UnityAssertEqualNumber((_U_SINT)(_UU16)(expected), (_U_SINT)(_UU16)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT16)
#define UNITY_TEST_ASSERT_EQUAL_UINT32(expected, actual, line, message)                          UnityAssertEqualNumber((_U_SINT)(_UU32)(expected), (_U_SINT)(_UU32)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT32)
#define UNITY_TEST_ASSERT_EQUAL_HEX8(expected, actual, line, message)                            UnityAssertEqualNumber((_U_SINT)(_US8 )(expected), (_U_SINT)(_US8 )(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX8)
#define UNITY_TEST_ASSERT_EQUAL_HEX16(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(_US16)(expected), (_U_SINT)(_US16)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX16)
#define UNITY_TEST_ASSERT_EQUAL_HEX32(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(_US32)(expected), (_U_SINT)(_US32)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX32)
#define UNITY_TEST_ASSERT_BITS(mask, expected, actual, line, message)                            UnityAssertBits((_U_SINT)(mask), (_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line)

#define UNITY_TEST_ASSERT_INT_WITHIN(delta, expected, actual, line, message)                     UnityAssertNumbersWithin((_U_SINT)(delta), (_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT)
#define UNITY_TEST_ASSERT_INT8_WITHIN(delta, expected, actual, line, message)                    UnityAssertNumbersWithin((_U_SINT)(_US8 )(delta), (_U_SINT)(_US8 )(expected), (_U_SINT)(_US8 )(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT8)
#define UNITY_TEST_ASSERT_INT16_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(_US16)(delta), (_U_SINT)(_US16)(expected), (_U_SINT)(_US16)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT16)
#define UNITY_TEST_ASSERT_INT32_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(_US32)(delta), (_U_SINT)(_US32)(expected), (_U_SINT)(_US32)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT32)
#define UNITY_TEST_ASSERT_UINT_WITHIN(delta, expected, actual, line, message)                    UnityAssertNumbersWithin((_U_SINT)(delta), (_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT)
#define UNITY_TEST_ASSERT_UINT8_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(_U_UINT)(_UU8 )(delta), (_U_SINT)(_U_UINT)(_UU8 )(expected), (_U_SINT)(_U_UINT)(_UU8 )(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT8)
#define UNITY_TEST_ASSERT_UINT16_WITHIN(delta, expected, actual, line, message)                  UnityAssertNumbersWithin((_U_SINT)(_U_UINT)(_UU16)(delta), (_U_SINT)(_U_UINT)(_UU16)(expected), (_U_SINT)(_U_UINT)(_UU16)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT16)
#define UNITY_TEST_ASSERT_UINT32_WITHIN(delta, expected, actual, line, message)                  UnityAssertNumbersWithin((_U_SINT)(_U_UINT)(_UU32)(delta), (_U_SINT)(_U_UINT)(_UU32)(expected), (_U_SINT)(_U_UINT)(_UU32)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT32)
#define UNITY_TEST_ASSERT_HEX8_WITHIN(delta, expected, actual, line, message)                    UnityAssertNumbersWithin((_U_SINT)(_U_UINT)(_UU8 )(delta), (_U_SINT)(_U_UINT)(_UU8 )(expected), (_U_SINT)(_U_UINT)(_UU8 )(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX8)
#define UNITY_TEST_ASSERT_HEX16_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(_U_UINT)(_UU16)(delta), (_U_SINT)(_U_UINT)(_UU16)(expected), (_U_SINT)(_U_UINT)(_UU16)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX16)
#define UNITY_TEST_ASSERT_HEX32_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(_U_UINT)(_UU32)(delta), (_U_SINT)(_U_UINT)(_UU32)(expected), (_U_SINT)(_U_UINT)(_UU32)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX32)

#define UNITY_TEST_ASSERT_EQUAL_PTR(expected, actual, line, message)                             UnityAssertEqualNumber((_U_SINT)(_UP)(expected), (_U_SINT)(_UP)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_POINTER)
#define UNITY_TEST_ASSERT_EQUAL_STRING(expected, actual, line, message)                          UnityAssertEqualString((const char*)(expected), (const char*)(actual), (message), (UNITY_LINE_TYPE)line)
#define UNITY_TEST_ASSERT_EQUAL_MEMORY(expected, actual, len, line, message)                     UnityAssertEqualMemory((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(len), 1, (message), (UNITY_LINE_TYPE)line)

#define UNITY_TEST_ASSERT_EQUAL_INT_ARRAY(expected, actual, num_elements, line, message)         UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT)
#define UNITY_TEST_ASSERT_EQUAL_INT8_ARRAY(expected, actual, num_elements, line, message)        UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT8)
#define UNITY_TEST_ASSERT_EQUAL_INT16_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT16)
#define UNITY_TEST_ASSERT_EQUAL_INT32_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT32)
#define UNITY_TEST_ASSERT_EQUAL_UINT_ARRAY(expected, actual, num_elements, line, message)        UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT)
#define UNITY_TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT8)
#define UNITY_TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, actual, num_elements, line, message)      UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT16)
#define UNITY_TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, actual, num_elements, line, message)      UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT32)
#define UNITY_TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, num_elements, line, message)        UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX8)
#define UNITY_TEST_ASSERT_EQUAL_HEX16_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX16)
#define UNITY_TEST_ASSERT_EQUAL_HEX32_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(expected), (UNITY_PTR_ATTRIBUTE const void*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX32)
#define UNITY_TEST_ASSERT_EQUAL_PTR_ARRAY(expected, actual, num_elements, line, message)         UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const void*)(_UP*)(expected), (const void*)(_UP*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_POINTER)
#define UNITY_TEST_ASSERT_EQUAL_STRING_ARRAY(expected, actual, num_elements, line, message)      UnityAssertEqualStringArray((const char**)(expected), (const char**)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line)
#define UNITY_TEST_ASSERT_EQUAL_MEMORY_ARRAY(expected, actual, len, num_elements, line, message) UnityAssertEqualMemory((UNITY_PTR_ATTRIBUTE void*)(expected), (UNITY_PTR_ATTRIBUTE void*)(actual), (_UU32)(len), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line)

#ifdef UNITY_SUPPORT_64
#define UNITY_TEST_ASSERT_EQUAL_INT64(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT64)
#define UNITY_TEST_ASSERT_EQUAL_UINT64(expected, actual, line, message)                          UnityAssertEqualNumber((_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT64)
#define UNITY_TEST_ASSERT_EQUAL_HEX64(expected, actual, line, message)                           UnityAssertEqualNumber((_U_SINT)(expected), (_U_SINT)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX64)
#define UNITY_TEST_ASSERT_EQUAL_INT64_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const _U_SINT*)(expected), (UNITY_PTR_ATTRIBUTE const _U_SINT*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT64)
#define UNITY_TEST_ASSERT_EQUAL_UINT64_ARRAY(expected, actual, num_elements, line, message)      UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const _U_SINT*)(expected), (UNITY_PTR_ATTRIBUTE const _U_SINT*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT64)
#define UNITY_TEST_ASSERT_EQUAL_HEX64_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualIntArray((UNITY_PTR_ATTRIBUTE const _U_SINT*)(expected), (UNITY_PTR_ATTRIBUTE const _U_SINT*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX64)
#define UNITY_TEST_ASSERT_INT64_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(delta), (_U_SINT)(expected), (_U_SINT)(actual), NULL, (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_INT64)
#define UNITY_TEST_ASSERT_UINT64_WITHIN(delta, expected, actual, line, message)                  UnityAssertNumbersWithin((_U_SINT)(delta), (_U_SINT)(expected), (_U_SINT)(actual), NULL, (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_UINT64)
#define UNITY_TEST_ASSERT_HEX64_WITHIN(delta, expected, actual, line, message)                   UnityAssertNumbersWithin((_U_SINT)(delta), (_U_SINT)(expected), (_U_SINT)(actual), NULL, (UNITY_LINE_TYPE)line, UNITY_DISPLAY_STYLE_HEX64)
#else
#define UNITY_TEST_ASSERT_EQUAL_INT64(expected, actual, line, message)                           UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_EQUAL_UINT64(expected, actual, line, message)                          UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_EQUAL_HEX64(expected, actual, line, message)                           UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_EQUAL_INT64_ARRAY(expected, actual, num_elements, line, message)       UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_EQUAL_UINT64_ARRAY(expected, actual, num_elements, line, message)      UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_EQUAL_HEX64_ARRAY(expected, actual, num_elements, line, message)       UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_INT64_WITHIN(delta, expected, actual, line, message)                   UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_UINT64_WITHIN(delta, expected, actual, line, message)                  UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#define UNITY_TEST_ASSERT_HEX64_WITHIN(delta, expected, actual, line, message)                   UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErr64)
#endif

#ifdef UNITY_EXCLUDE_FLOAT
#define UNITY_TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual, line, message)                   UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_EQUAL_FLOAT(expected, actual, line, message)                           UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_EQUAL_FLOAT_ARRAY(expected, actual, num_elements, line, message)       UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_INF(actual, line, message)                                    UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_NEG_INF(actual, line, message)                                UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_NAN(actual, line, message)                                    UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_DETERMINATE(actual, line, message)                            UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_INF(actual, line, message)                                UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_NEG_INF(actual, line, message)                            UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_NAN(actual, line, message)                                UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_DETERMINATE(actual, line, message)                        UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrFloat)
#else
#define UNITY_TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual, line, message)                   UnityAssertFloatsWithin((_UF)(delta), (_UF)(expected), (_UF)(actual), (message), (UNITY_LINE_TYPE)line)
#define UNITY_TEST_ASSERT_EQUAL_FLOAT(expected, actual, line, message)                           UNITY_TEST_ASSERT_FLOAT_WITHIN((_UF)(expected) * (_UF)UNITY_FLOAT_PRECISION, (_UF)expected, (_UF)actual, (UNITY_LINE_TYPE)line, message)
#define UNITY_TEST_ASSERT_EQUAL_FLOAT_ARRAY(expected, actual, num_elements, line, message)       UnityAssertEqualFloatArray((_UF*)(expected), (_UF*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line)
#define UNITY_TEST_ASSERT_FLOAT_IS_INF(actual, line, message)                                    UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_INF)
#define UNITY_TEST_ASSERT_FLOAT_IS_NEG_INF(actual, line, message)                                UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NEG_INF)
#define UNITY_TEST_ASSERT_FLOAT_IS_NAN(actual, line, message)                                    UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NAN)
#define UNITY_TEST_ASSERT_FLOAT_IS_DETERMINATE(actual, line, message)                            UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_DET)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_INF(actual, line, message)                                UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_INF)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_NEG_INF(actual, line, message)                            UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_NEG_INF)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_NAN(actual, line, message)                                UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_NAN)
#define UNITY_TEST_ASSERT_FLOAT_IS_NOT_DETERMINATE(actual, line, message)                        UnityAssertFloatSpecial((_UF)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_DET)
#endif

#ifdef UNITY_EXCLUDE_DOUBLE
#define UNITY_TEST_ASSERT_DOUBLE_WITHIN(delta, expected, actual, line, message)                  UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_EQUAL_DOUBLE(expected, actual, line, message)                          UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_EQUAL_DOUBLE_ARRAY(expected, actual, num_elements, line, message)      UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_INF(actual, line, message)                                   UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NEG_INF(actual, line, message)                               UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NAN(actual, line, message)                                   UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_DETERMINATE(actual, line, message)                           UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_INF(actual, line, message)                               UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_NEG_INF(actual, line, message)                           UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_NAN(actual, line, message)                               UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_DETERMINATE(actual, line, message)                       UNITY_TEST_FAIL((UNITY_LINE_TYPE)line, UnityStrErrDouble)
#else
#define UNITY_TEST_ASSERT_DOUBLE_WITHIN(delta, expected, actual, line, message)                  UnityAssertDoublesWithin((_UD)(delta), (_UD)(expected), (_UD)(actual), (message), (UNITY_LINE_TYPE)line)
#define UNITY_TEST_ASSERT_EQUAL_DOUBLE(expected, actual, line, message)                          UNITY_TEST_ASSERT_DOUBLE_WITHIN((_UD)(expected) * (_UD)UNITY_DOUBLE_PRECISION, (_UD)expected, (_UD)actual, (UNITY_LINE_TYPE)line, message)
#define UNITY_TEST_ASSERT_EQUAL_DOUBLE_ARRAY(expected, actual, num_elements, line, message)      UnityAssertEqualDoubleArray((_UD*)(expected), (_UD*)(actual), (_UU32)(num_elements), (message), (UNITY_LINE_TYPE)line)
#define UNITY_TEST_ASSERT_DOUBLE_IS_INF(actual, line, message)                                   UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_INF)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NEG_INF(actual, line, message)                               UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NEG_INF)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NAN(actual, line, message)                                   UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NAN)
#define UNITY_TEST_ASSERT_DOUBLE_IS_DETERMINATE(actual, line, message)                           UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_DET)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_INF(actual, line, message)                               UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_INF)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_NEG_INF(actual, line, message)                           UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_NEG_INF)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_NAN(actual, line, message)                               UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_NAN)
#define UNITY_TEST_ASSERT_DOUBLE_IS_NOT_DETERMINATE(actual, line, message)                       UnityAssertDoubleSpecial((_UD)(actual), (message), (UNITY_LINE_TYPE)line, UNITY_FLOAT_IS_NOT_DET)
#endif

//End of UNITY_INTERNALS_H
#endif

//#define TEST_EXPECT_FAIL()																		Unity.isExpectingFail = 1;
//#define TEST_EXPECT_FAIL_MESSAGE(message)														Unity.isExpectingFail = 1; Unity.XFAILMessage = message; //PROBLEM : does this work on all compilers?

#define TEST_EXPECT_FAIL()																		UnityExpectFail();
#define TEST_EXPECT_FAIL_MESSAGE(message)														UnityExpectFailMessage( (message) );
