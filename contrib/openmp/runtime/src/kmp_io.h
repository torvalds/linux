/*
 * kmp_io.h -- RTL IO header file.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_IO_H
#define KMP_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */

enum kmp_io { kmp_out = 0, kmp_err };

extern kmp_bootstrap_lock_t __kmp_stdio_lock; /* Control stdio functions */
extern kmp_bootstrap_lock_t
    __kmp_console_lock; /* Control console initialization */

extern void __kmp_vprintf(enum kmp_io stream, char const *format, va_list ap);
extern void __kmp_printf(char const *format, ...);
extern void __kmp_printf_no_lock(char const *format, ...);
extern void __kmp_fprintf(enum kmp_io stream, char const *format, ...);
extern void __kmp_close_console(void);

#ifdef __cplusplus
}
#endif

#endif /* KMP_IO_H */
