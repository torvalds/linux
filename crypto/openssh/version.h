/* $OpenBSD: version.h,v 1.82 2018/07/03 11:42:12 djm Exp $ */
/* $FreeBSD$ */

#define SSH_VERSION	"OpenSSH_7.8"

#define SSH_PORTABLE	"p1"
#define SSH_RELEASE	SSH_VERSION SSH_PORTABLE

#define SSH_VERSION_FREEBSD	"FreeBSD-20180909"

#ifdef WITH_OPENSSL
#define OPENSSL_VERSION_STRING	SSLeay_version(SSLEAY_VERSION)
#else
#define OPENSSL_VERSION_STRING	"without OpenSSL"
#endif
