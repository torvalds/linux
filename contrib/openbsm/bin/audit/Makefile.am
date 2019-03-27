if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

sbin_PROGRAMS = audit
audit_LDADD = $(top_builddir)/libbsm/libbsm.la
man8_MANS = audit.8

if USE_MACH_IPC
audit_SOURCES = auditd_controlUser.c audit.c
CLEANFILES = auditd_controlUser.c auditd_control.h

auditd_controlUser.c auditd_control.h: $(top_srcdir)/bin/auditd/auditd_control.defs 
	$(MIG)	-user auditd_controlUser.c -header auditd_control.h -server /dev/null -sheader /dev/null $(top_srcdir)/bin/auditd/auditd_control.defs
else
audit_SOURCES = audit.c
endif
