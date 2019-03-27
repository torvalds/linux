# SYNOPSIS						-*- Autoconf -*-
#
#  NTP_CRYPTO_RAND
#
# DESCRIPTION
#
# AUTHOR
#
#  Harlan Stenn
#
# LICENSE
#
#  This file is Copyright (c) 2014 Network Time Foundation
# 
#  Copying and distribution of this file, with or without modification, are
#  permitted in any medium without royalty provided the copyright notice,
#  author attribution and this notice are preserved.  This file is offered
#  as-is, without any warranty.

AC_DEFUN([NTP_CRYPTO_RAND], [

dnl check for --disable-openssl-random
dnl if that's not specified:
dnl - Look for RAND_poll and RAND_bytes
dnl - if they exist, define USE_OPENSSL_CRYPTO_RAND

AC_MSG_CHECKING([if we want to use OpenSSL's crypto random (if available)])
AC_ARG_ENABLE(
    [openssl-random],
    [AS_HELP_STRING(
    	[--enable-openssl-random],
	[Use OpenSSL's crypto random number functions, if available (default is yes)]
    )],
    [ntp_use_openssl_random=$enableval],
    [ntp_use_openssl_random=yes]
)
AC_MSG_RESULT([$ntp_use_openssl_random])

# The following might need extra libraries
NTPO_SAVED_LIBS="$LIBS"
LIBS="$NTPO_SAVED_LIBS $LDADD_NTP"
dnl AC_MSG_NOTICE([LIBS is <$LIBS>])
AC_CHECK_FUNCS([RAND_bytes RAND_poll])
LIBS="$NTPO_SAVED_LIBS"
case "$ntp_use_openssl_random$ac_cv_func_RAND_bytes$ac_cv_func_RAND_poll" in
 yesyesyes)
     AC_DEFINE([USE_OPENSSL_CRYPTO_RAND], [1], [Use OpenSSL's crypto random functions])
     ;;
 *) ntp_use_openssl_random=no ;;
esac

]) dnl NTP_CRYPTO_RAND

