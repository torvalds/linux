/*
 * kmp_version.h -- version number for this release
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_VERSION_H
#define KMP_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifndef KMP_VERSION_MAJOR
#error KMP_VERSION_MAJOR macro is not defined.
#endif
#define KMP_VERSION_MINOR 0
/* Using "magic" prefix in all the version strings is rather convenient to get
   static version info from binaries by using standard utilities "strings" and
   "grep", e. g.:
        $ strings libomp.so | grep "@(#)"
   gives clean list of all version strings in the library. Leading zero helps
   to keep version string separate from printable characters which may occurs
   just before version string. */
#define KMP_VERSION_MAGIC_STR "\x00@(#) "
#define KMP_VERSION_MAGIC_LEN 6 // Length of KMP_VERSION_MAGIC_STR.
#define KMP_VERSION_PREF_STR "Intel(R) OMP "
#define KMP_VERSION_PREFIX KMP_VERSION_MAGIC_STR KMP_VERSION_PREF_STR

/* declare all the version string constants for KMP_VERSION env. variable */
extern int const __kmp_version_major;
extern int const __kmp_version_minor;
extern int const __kmp_version_build;
extern int const __kmp_openmp_version;
extern char const
    __kmp_copyright[]; // Old variable, kept for compatibility with ITC and ITP.
extern char const __kmp_version_copyright[];
extern char const __kmp_version_lib_ver[];
extern char const __kmp_version_lib_type[];
extern char const __kmp_version_link_type[];
extern char const __kmp_version_build_time[];
extern char const __kmp_version_target_env[];
extern char const __kmp_version_build_compiler[];
extern char const __kmp_version_alt_comp[];
extern char const __kmp_version_omp_api[];
// ??? extern char const __kmp_version_debug[];
extern char const __kmp_version_lock[];
extern char const __kmp_version_nested_stats_reporting[];
extern char const __kmp_version_ftnstdcall[];
extern char const __kmp_version_ftncdecl[];
extern char const __kmp_version_ftnextra[];

void __kmp_print_version_1(void);
void __kmp_print_version_2(void);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif /* KMP_VERSION_H */
