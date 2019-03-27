/* ===-- assembly.h - compiler-rt assembler support macros -----------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file defines macros for use in compiler-rt assembler source.
 * This file is not part of the interface of this library.
 *
 * ===----------------------------------------------------------------------===
 */

#ifndef COMPILERRT_ASSEMBLY_H
#define COMPILERRT_ASSEMBLY_H

#if defined(__POWERPC__) || defined(__powerpc__) || defined(__ppc__)
#define SEPARATOR @
#else
#define SEPARATOR ;
#endif

#if defined(__APPLE__)
#define HIDDEN(name) .private_extern name
#define LOCAL_LABEL(name) L_##name
// tell linker it can break up file at label boundaries
#define FILE_LEVEL_DIRECTIVE .subsections_via_symbols
#define SYMBOL_IS_FUNC(name)
#define CONST_SECTION .const

#define NO_EXEC_STACK_DIRECTIVE

#elif defined(__ELF__)

#define HIDDEN(name) .hidden name
#define LOCAL_LABEL(name) .L_##name
#define FILE_LEVEL_DIRECTIVE
#if defined(__arm__)
#define SYMBOL_IS_FUNC(name) .type name,%function
#else
#define SYMBOL_IS_FUNC(name) .type name,@function
#endif
#define CONST_SECTION .section .rodata

#if defined(__GNU__) || defined(__FreeBSD__) || defined(__Fuchsia__) || \
    defined(__linux__)
#define NO_EXEC_STACK_DIRECTIVE .section .note.GNU-stack,"",%progbits
#else
#define NO_EXEC_STACK_DIRECTIVE
#endif

#else // !__APPLE__ && !__ELF__

#define HIDDEN(name)
#define LOCAL_LABEL(name) .L ## name
#define FILE_LEVEL_DIRECTIVE
#define SYMBOL_IS_FUNC(name)                                                   \
  .def name SEPARATOR                                                          \
    .scl 2 SEPARATOR                                                           \
    .type 32 SEPARATOR                                                         \
  .endef
#define CONST_SECTION .section .rdata,"rd"

#define NO_EXEC_STACK_DIRECTIVE

#endif

#if defined(__arm__)

/*
 * Determine actual [ARM][THUMB[1][2]] ISA using compiler predefined macros:
 * - for '-mthumb -march=armv6' compiler defines '__thumb__'
 * - for '-mthumb -march=armv7' compiler defines '__thumb__' and '__thumb2__'
 */
#if defined(__thumb2__) || defined(__thumb__)
#define DEFINE_CODE_STATE .thumb SEPARATOR
#define DECLARE_FUNC_ENCODING    .thumb_func SEPARATOR
#if defined(__thumb2__)
#define USE_THUMB_2
#define IT(cond)  it cond
#define ITT(cond) itt cond
#define ITE(cond) ite cond
#else
#define USE_THUMB_1
#define IT(cond)
#define ITT(cond)
#define ITE(cond)
#endif // defined(__thumb__2)
#else // !defined(__thumb2__) && !defined(__thumb__)
#define DEFINE_CODE_STATE .arm SEPARATOR
#define DECLARE_FUNC_ENCODING
#define IT(cond)
#define ITT(cond)
#define ITE(cond)
#endif

#if defined(USE_THUMB_1) && defined(USE_THUMB_2)
#error "USE_THUMB_1 and USE_THUMB_2 can't be defined together."
#endif

#if defined(__ARM_ARCH_4T__) || __ARM_ARCH >= 5
#define ARM_HAS_BX
#endif
#if !defined(__ARM_FEATURE_CLZ) && !defined(USE_THUMB_1) &&  \
    (__ARM_ARCH >= 6 || (__ARM_ARCH == 5 && !defined(__ARM_ARCH_5__)))
#define __ARM_FEATURE_CLZ
#endif

#ifdef ARM_HAS_BX
#define JMP(r) bx r
#define JMPc(r, c) bx##c r
#else
#define JMP(r) mov pc, r
#define JMPc(r, c) mov##c pc, r
#endif

// pop {pc} can't switch Thumb mode on ARMv4T
#if __ARM_ARCH >= 5
#define POP_PC() pop {pc}
#else
#define POP_PC()                                                               \
  pop {ip};                                                                    \
  JMP(ip)
#endif

#if defined(USE_THUMB_2)
#define WIDE(op) op.w
#else
#define WIDE(op) op
#endif
#else // !defined(__arm)
#define DECLARE_FUNC_ENCODING
#define DEFINE_CODE_STATE
#endif

#define GLUE2(a, b) a##b
#define GLUE(a, b) GLUE2(a, b)
#define SYMBOL_NAME(name) GLUE(__USER_LABEL_PREFIX__, name)

#ifdef VISIBILITY_HIDDEN
#define DECLARE_SYMBOL_VISIBILITY(name)                                        \
  HIDDEN(SYMBOL_NAME(name)) SEPARATOR
#else
#define DECLARE_SYMBOL_VISIBILITY(name)
#endif

#define DEFINE_COMPILERRT_FUNCTION(name)                                       \
  DEFINE_CODE_STATE                                                            \
  FILE_LEVEL_DIRECTIVE SEPARATOR                                               \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  DECLARE_SYMBOL_VISIBILITY(name)                                              \
  DECLARE_FUNC_ENCODING                                                        \
  SYMBOL_NAME(name):

#define DEFINE_COMPILERRT_THUMB_FUNCTION(name)                                 \
  DEFINE_CODE_STATE                                                            \
  FILE_LEVEL_DIRECTIVE SEPARATOR                                               \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  DECLARE_SYMBOL_VISIBILITY(name) SEPARATOR                                    \
  .thumb_func SEPARATOR                                                        \
  SYMBOL_NAME(name):

#define DEFINE_COMPILERRT_PRIVATE_FUNCTION(name)                               \
  DEFINE_CODE_STATE                                                            \
  FILE_LEVEL_DIRECTIVE SEPARATOR                                               \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  HIDDEN(SYMBOL_NAME(name)) SEPARATOR                                          \
  DECLARE_FUNC_ENCODING                                                        \
  SYMBOL_NAME(name):

#define DEFINE_COMPILERRT_PRIVATE_FUNCTION_UNMANGLED(name)                     \
  DEFINE_CODE_STATE                                                            \
  .globl name SEPARATOR                                                        \
  SYMBOL_IS_FUNC(name) SEPARATOR                                               \
  HIDDEN(name) SEPARATOR                                                       \
  DECLARE_FUNC_ENCODING                                                        \
  name:

#define DEFINE_COMPILERRT_FUNCTION_ALIAS(name, target)                         \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  DECLARE_SYMBOL_VISIBILITY(SYMBOL_NAME(name)) SEPARATOR                       \
  .set SYMBOL_NAME(name), SYMBOL_NAME(target) SEPARATOR

#if defined(__ARM_EABI__)
#define DEFINE_AEABI_FUNCTION_ALIAS(aeabi_name, name)                          \
  DEFINE_COMPILERRT_FUNCTION_ALIAS(aeabi_name, name)
#else
#define DEFINE_AEABI_FUNCTION_ALIAS(aeabi_name, name)
#endif

#ifdef __ELF__
#define END_COMPILERRT_FUNCTION(name)                                          \
  .size SYMBOL_NAME(name), . - SYMBOL_NAME(name)
#else
#define END_COMPILERRT_FUNCTION(name)
#endif

#endif /* COMPILERRT_ASSEMBLY_H */
