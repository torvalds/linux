include $(top_srcdir)/Makefile.am.common

noinst_LTLIBRARIES = libheim-ipcc.la libheim-ipcs.la

dist_libheim_ipcc_la_SOURCES = hi_locl.h heim_ipc_types.h client.c common.c
dist_libheim_ipcs_la_SOURCES = hi_locl.h heim_ipc_types.h server.c common.c

include_HEADERS = heim-ipc.h

##
## Enable when this is not a noinst_ library
##
#libheim_ipcc_la_LDFLAGS = -version-info 0:0:0
#libheim_ipcs_la_LDFLAGS = -version-info 0:0:0
#
#if versionscript
#libheim_ipcc_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-scriptc.map
#libheim_ipcs_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-scripts.map
#endif

libheim_ipcc_la_LIBADD = \
	$(LIB_heimbase) \
	$(LIB_roken) \
	$(PTHREAD_LIBADD)

libheim_ipcs_la_LIBADD = $(libheim_ipcc_la_LIBADD)

TESTS =	$(check_PROGRAMS)

noinst_PROGRAMS = tc ts ts-http

ts_LDADD = libheim-ipcs.la $(LIB_roken)
ts_http_LDADD = $(ts_LDADD)
tc_LDADD = libheim-ipcc.la $(LIB_roken)

if have_gcd

EXTRA_DIST = heim_ipc.defs heim_ipc_async.defs heim_ipc_reply.defs

heim_ipc.h heim_ipcUser.c heim_ipcServer.c heim_ipcServer.h: heim_ipc.defs
	mig -header heim_ipc.h -user heim_ipcUser.c -sheader heim_ipcServer.h -server heim_ipcServer.c -I$(srcdir) $(srcdir)/heim_ipc.defs

heim_ipc_async.h heim_ipc_asyncUser.c heim_ipc_asyncServer.c heim_ipc_asyncServer.h: heim_ipc_async.defs
	mig -header heim_ipc_async.h -user heim_ipc_asyncUser.c -sheader heim_ipc_asyncServer.h -server heim_ipc_asyncServer.c -I$(srcdir) $(srcdir)/heim_ipc_async.defs

heim_ipc_reply.h heim_ipc_replyUser.c: heim_ipc_reply.defs
	mig -header heim_ipc_reply.h -user heim_ipc_replyUser.c -sheader /dev/null -server /dev/null -I$(srcdir) $(srcdir)/heim_ipc_reply.defs

built_ipcc = heim_ipc.h heim_ipcUser.c
built_ipcc += heim_ipc_asyncServer.c heim_ipc_asyncServer.h

nodist_libheim_ipcc_la_SOURCES = $(built_ipcc)

built_ipcs = heim_ipcServer.c heim_ipcServer.h
built_ipcs += heim_ipc_asyncUser.c heim_ipc_async.h
built_ipcs += heim_ipc_reply.h heim_ipc_replyUser.c

nodist_libheim_ipcs_la_SOURCES = $(built_ipcs)

libheim_ipcs_la_LIBADD += -lbsm

CLEANFILES = $(built_ipcc) $(built_ipcs)

$(srcdir)/client.c: $(built_ipcc)
$(srcdir)/server.c: $(built_ipcs)

endif
