if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

sbin_PROGRAMS = auditdistd
man5_MANS = auditdistd.conf.5
man8_MANS = auditdistd.8
YFLAGS = -d
auditdistd_CFLAGS = -Wno-format
auditdistd_LDFLAGS = -lcrypto

auditdistd_SOURCES =		\
	auditdistd.c		\
	parse.y			\
	pjdlog.c		\
	proto.c			\
	proto_common.c		\
	proto_socketpair.c	\
	proto_tcp.c		\
	proto_tls.c		\
	proto_uds.c		\
	receiver.c		\
	sandbox.c		\
	sender.c		\
	subr.c			\
	token.l			\
	trail.c
