# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I$(srcdir)/.. $(INCLUDE_hcrypto)

noinst_LIBRARIES = libtelnet.a

libtelnet_a_SOURCES = \
	auth-proto.h	\
	auth.c		\
	auth.h		\
	enc-proto.h	\
	enc_des.c	\
	encrypt.c	\
	encrypt.h	\
	genget.c	\
	kerberos5.c	\
	misc-proto.h	\
	misc.c		\
	misc.h

EXTRA_DIST = NTMakefile rsaencpwd.c spx.c
