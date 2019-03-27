# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(AFS_EXTRA_DEFS) $(ROKEN_RENAME)

if KRB5
DEPLIB_krb5 = ../krb5/libkrb5.la 
krb5_am_workaround = $(INCLUDE_hcrypto) -I$(top_srcdir)/lib/krb5
else
DEPLIB_krb5  =
krb5_am_workaround = 
endif # KRB5
AM_CPPFLAGS += $(krb5_am_workaround)


if AIX
AFSL_EXP = $(srcdir)/afsl.exp

if AIX4
AFS_EXTRA_LD = -bnoentry
else
AFS_EXTRA_LD = -e _nostart
endif

if AIX_DYNAMIC_AFS
AIX_SRC = 
AFS_EXTRA_LIBS = afslib.so
AFS_EXTRA_DEFS =
else
AIX_SRC = afslib.c
AFS_EXTRA_LIBS = 
AFS_EXTRA_DEFS = -DSTATIC_AFS
endif

else
AFSL_EXP =
AIX_SRC =
endif # AIX

libkafs_la_LIBADD = $(DEPLIB_krb5) $(LIBADD_roken)

lib_LTLIBRARIES = libkafs.la
libkafs_la_LDFLAGS = -version-info 5:1:5
foodir = $(libdir)
foo_DATA = $(AFS_EXTRA_LIBS)
# EXTRA_DATA = afslib.so

CLEANFILES= $(AFS_EXTRA_LIBS) $(ROKEN_SRCS)

include_HEADERS = kafs.h

if KRB5
afskrb5_c = 
endif

if do_roken_rename
ROKEN_SRCS = resolve.c strtok_r.c strlcpy.c strsep.c
endif

dist_libkafs_la_SOURCES =			\
	afssys.c				\
	afskrb5.c				\
	common.c				\
	$(AIX_SRC)				\
	kafs_locl.h				\
	afssysdefs.h				\
	roken_rename.h

nodist_libkafs_la_SOURCES = $(ROKEN_SRCS)

EXTRA_libkafs_la_SOURCES = afskrb5.c afslib.c

EXTRA_DIST = NTMakefile afsl.exp afslib.exp $(man_MANS)

man_MANS = kafs.3

# AIX: this almost works with gcc, but somehow it fails to use the
# correct ld, use ld instead
afslib.so: afslib.o
	ld -o $@ -bM:SRE -bI:$(srcdir)/afsl.exp -bE:$(srcdir)/afslib.exp $(AFS_EXTRA_LD) afslib.o -lc

resolve.c:
	$(LN_S) $(srcdir)/../roken/resolve.c .

strtok_r.c:
	$(LN_S) $(srcdir)/../roken/strtok_r.c .

strlcpy.c:
	$(LN_S) $(srcdir)/../roken/strlcpy.c .

strsep.c:
	$(LN_S) $(srcdir)/../roken/strsep.c .
