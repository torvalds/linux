## $NetBSD: Makefile.am,v 1.2 2010/01/04 06:24:20 lukem Exp $

bin_PROGRAMS = tnftp

tnftp_SOURCES = \
	cmds.c \
	cmdtab.c \
	complete.c \
	domacro.c \
	fetch.c \
	ftp.c \
	main.c \
	progressbar.c \
	ruserpass.c \
	util.c

tnftp_CPPFLAGS = \
	-DHAVE_TNFTPD_H=1 \
	-D_DEFAULT_CONFDIR=\"${sysconfdir}\" \
	-I$(srcdir) \
	-I$(top_srcdir)/libnetbsd \
	-I$(top_srcdir) \
	-I$(top_builddir)

tnftp_LDADD = \
	../libnetbsd/libnetbsd.la 


if USE_LIBEDIT
tnftp_CPPFLAGS += \
	-I$(top_srcdir)/libedit

tnftp_LDADD += \
	../libedit/libedit.la
endif


man1_MANS = \
	tnftp.1

tnftp.1: ftp.1
	cp $(srcdir)/ftp.1 tnftp.1

CLEANFILES = \
	tnftp.1

EXTRA_DIST = \
	extern.h \
	ftp.1 \
	ftp_var.h \
	progressbar.h \
	version.h
