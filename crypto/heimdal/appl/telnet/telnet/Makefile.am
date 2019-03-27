# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I$(srcdir)/.. $(INCLUDE_hcrypto)

bin_PROGRAMS = telnet

CHECK_LOCAL = 

telnet_SOURCES  = authenc.c commands.c main.c network.c ring.c \
		  sys_bsd.c telnet.c terminal.c \
		  utilities.c defines.h externs.h ring.h telnet_locl.h types.h

man_MANS = telnet.1

LDADD = ../libtelnet/libtelnet.a \
	$(LIB_krb5) \
	$(LIB_hcrypto) \
	$(LIB_tgetent) \
	$(LIB_kdfs) \
	$(LIB_roken)

EXTRA_DIST = NTMakefile $(man_MANS)
