/*
 * {- join("\n * ", @autowarntext) -}
 *
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslv.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef OPENSSL_ALGORITHM_DEFINES
# error OPENSSL_ALGORITHM_DEFINES no longer supported
#endif

/*
 * OpenSSL was configured with the following options:
 */

{- if (@{$config{openssl_sys_defines}}) {
      foreach (@{$config{openssl_sys_defines}}) {
	$OUT .= "#ifndef $_\n";
	$OUT .= "# define $_ 1\n";
	$OUT .= "#endif\n";
      }
    }
    foreach (@{$config{openssl_api_defines}}) {
        (my $macro, my $value) = $_ =~ /^(.*?)=(.*?)$/;
        $OUT .= "#define $macro $value\n";
    }
    if (@{$config{openssl_algorithm_defines}}) {
      foreach (@{$config{openssl_algorithm_defines}}) {
	$OUT .= "#ifndef $_\n";
	$OUT .= "# define $_\n";
	$OUT .= "#endif\n";
      }
    }
    if (@{$config{openssl_thread_defines}}) {
      foreach (@{$config{openssl_thread_defines}}) {
	$OUT .= "#ifndef $_\n";
	$OUT .= "# define $_\n";
	$OUT .= "#endif\n";
      }
    }
    if (@{$config{openssl_other_defines}}) {
      foreach (@{$config{openssl_other_defines}}) {
	$OUT .= "#ifndef $_\n";
	$OUT .= "# define $_\n";
	$OUT .= "#endif\n";
      }
    }
    "";
-}

/*
 * Sometimes OPENSSSL_NO_xxx ends up with an empty file and some compilers
 * don't like that.  This will hopefully silence them.
 */
#define NON_EMPTY_TRANSLATION_UNIT static void *dummy = &dummy;

/*
 * Applications should use -DOPENSSL_API_COMPAT=<version> to suppress the
 * declarations of functions deprecated in or before <version>. Otherwise, they
 * still won't see them if the library has been built to disable deprecated
 * functions.
 */
#ifndef DECLARE_DEPRECATED
# define DECLARE_DEPRECATED(f)   f;
# ifdef __GNUC__
#  if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 0)
#   undef DECLARE_DEPRECATED
#   define DECLARE_DEPRECATED(f)    f __attribute__ ((deprecated));
#  endif
# endif
#endif

#ifndef OPENSSL_FILE
# ifdef OPENSSL_NO_FILENAMES
#  define OPENSSL_FILE ""
#  define OPENSSL_LINE 0
# else
#  define OPENSSL_FILE __FILE__
#  define OPENSSL_LINE __LINE__
# endif
#endif

#ifndef OPENSSL_MIN_API
# define OPENSSL_MIN_API 0
#endif

#if !defined(OPENSSL_API_COMPAT) || OPENSSL_API_COMPAT < OPENSSL_MIN_API
# undef OPENSSL_API_COMPAT
# define OPENSSL_API_COMPAT OPENSSL_MIN_API
#endif

/*
 * Do not deprecate things to be deprecated in version 1.2.0 before the
 * OpenSSL version number matches.
 */
#if OPENSSL_VERSION_NUMBER < 0x10200000L
# define DEPRECATEDIN_1_2_0(f)   f;
#elif OPENSSL_API_COMPAT < 0x10200000L
# define DEPRECATEDIN_1_2_0(f)   DECLARE_DEPRECATED(f)
#else
# define DEPRECATEDIN_1_2_0(f)
#endif

#if OPENSSL_API_COMPAT < 0x10100000L
# define DEPRECATEDIN_1_1_0(f)   DECLARE_DEPRECATED(f)
#else
# define DEPRECATEDIN_1_1_0(f)
#endif

#if OPENSSL_API_COMPAT < 0x10000000L
# define DEPRECATEDIN_1_0_0(f)   DECLARE_DEPRECATED(f)
#else
# define DEPRECATEDIN_1_0_0(f)
#endif

#if OPENSSL_API_COMPAT < 0x00908000L
# define DEPRECATEDIN_0_9_8(f)   DECLARE_DEPRECATED(f)
#else
# define DEPRECATEDIN_0_9_8(f)
#endif

/* Generate 80386 code? */
{- $config{processor} eq "386" ? "#define" : "#undef" -} I386_ONLY

#undef OPENSSL_UNISTD
#define OPENSSL_UNISTD {- $target{unistd} -}

{- $config{export_var_as_fn} ? "#define" : "#undef" -} OPENSSL_EXPORT_VAR_AS_FUNCTION

/*
 * The following are cipher-specific, but are part of the public API.
 */
#if !defined(OPENSSL_SYS_UEFI)
{- $config{bn_ll} ? "# define" : "# undef" -} BN_LLONG
/* Only one for the following should be defined */
{- $config{b64l} ? "# define" : "# undef" -} SIXTY_FOUR_BIT_LONG
{- $config{b64}  ? "# define" : "# undef" -} SIXTY_FOUR_BIT
{- $config{b32}  ? "# define" : "# undef" -} THIRTY_TWO_BIT
#endif

#define RC4_INT {- $config{rc4_int} -}

#ifdef  __cplusplus
}
#endif
