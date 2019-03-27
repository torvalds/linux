//===-- sanitizer_asm.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Various support for assemebler.
//
//===----------------------------------------------------------------------===//

// Some toolchains do not support .cfi asm directives, so we have to hide
// them inside macros.
#if defined(__clang__) ||                                                      \
    (defined(__GNUC__) && defined(__GCC_HAVE_DWARF2_CFI_ASM))
  // GCC defined __GCC_HAVE_DWARF2_CFI_ASM if it supports CFI.
  // Clang seems to support CFI by default (or not?).
  // We need two versions of macros: for inline asm and standalone asm files.
# define CFI_INL_ADJUST_CFA_OFFSET(n) ".cfi_adjust_cfa_offset " #n ";"

# define CFI_STARTPROC .cfi_startproc
# define CFI_ENDPROC .cfi_endproc
# define CFI_ADJUST_CFA_OFFSET(n) .cfi_adjust_cfa_offset n
# define CFI_DEF_CFA_OFFSET(n) .cfi_def_cfa_offset n
# define CFI_REL_OFFSET(reg, n) .cfi_rel_offset reg, n
# define CFI_OFFSET(reg, n) .cfi_offset reg, n
# define CFI_DEF_CFA_REGISTER(reg) .cfi_def_cfa_register reg
# define CFI_DEF_CFA(reg, n) .cfi_def_cfa reg, n
# define CFI_RESTORE(reg) .cfi_restore reg

#else  // No CFI
# define CFI_INL_ADJUST_CFA_OFFSET(n)
# define CFI_STARTPROC
# define CFI_ENDPROC
# define CFI_ADJUST_CFA_OFFSET(n)
# define CFI_DEF_CFA_OFFSET(n)
# define CFI_REL_OFFSET(reg, n)
# define CFI_OFFSET(reg, n)
# define CFI_DEF_CFA_REGISTER(reg)
# define CFI_DEF_CFA(reg, n)
# define CFI_RESTORE(reg)
#endif

#if !defined(__APPLE__)
# define ASM_HIDDEN(symbol) .hidden symbol
# define ASM_TYPE_FUNCTION(symbol) .type symbol, @function
# define ASM_SIZE(symbol) .size symbol, .-symbol
# define ASM_SYMBOL(symbol) symbol
# define ASM_SYMBOL_INTERCEPTOR(symbol) symbol
#else
# define ASM_HIDDEN(symbol)
# define ASM_TYPE_FUNCTION(symbol)
# define ASM_SIZE(symbol)
# define ASM_SYMBOL(symbol) _##symbol
# define ASM_SYMBOL_INTERCEPTOR(symbol) _wrap_##symbol
#endif
