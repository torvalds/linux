if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

sbin_PROGRAMS = auditd
auditd_LDADD = $(top_builddir)/libbsm/libbsm.la $(top_builddir)/libauditd/libauditd.la
man8_MANS = auditd.8

if USE_MACH_IPC
auditd_SOURCES = auditd_controlServer.c audit_triggersServer.c audit_warn.c auditd.c auditd_darwin.c
CLEANFILES = auditd_control_server.c auditd_controlServer.h audit_triggersServer.c audit_triggersServer.h

auditd_controlServer.c auditd_controlServer.h: auditd_control.defs
	$(MIG) -user /dev/null -header /dev/null -server auditd_controlServer.c -sheader auditd_controlServer.h $(top_srcdir)/bin/auditd/auditd_control.defs

audit_triggersServer.c audit_triggersServer.h: audit_triggers.defs
	$(MIG) -user /dev/null -header /dev/null -server audit_triggersServer.c -sheader audit_triggersServer.h $(top_srcdir)/bin/auditd/audit_triggers.defs
else
auditd_SOURCES = audit_warn.c auditd.c auditd_fbsd.c
endif
