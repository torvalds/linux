/*
 * A hack for platforms which require specially built syslog facilities
 */

#ifndef NTP_SYSLOG_H
#define NTP_SYSLOG_H

#include <ntp_types.h>		/* u_int32 type */

#ifdef VMS
extern void msyslog();
extern void mvsyslog();
#else
# ifndef SYS_VXWORKS
#  include <syslog.h>
# endif
#endif /* VMS */
#include <stdio.h>

extern int	syslogit;
extern int	msyslog_term;	/* duplicate to stdout/err */
extern int	msyslog_term_pid;
extern int	msyslog_include_timestamp;
extern FILE *	syslog_file;	/* if syslogit is FALSE, log to 
				   this file and not syslog */
extern char *	syslog_fname;
extern char *	syslog_abs_fname;

#if defined(VMS) || defined (SYS_VXWORKS)
#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but signification condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */
#endif /* VMS || VXWORKS */

/*
 * syslog output control
 */
#define NLOG_INFO		0x00000001
#define NLOG_EVENT		0x00000002
#define NLOG_STATUS		0x00000004
#define NLOG_STATIST		0x00000008

#define NLOG_OSYS			 0 /* offset for system flags */
#define NLOG_SYSMASK		0x0000000F /* system log events */
#define NLOG_SYSINFO		0x00000001 /* system info log events */
#define NLOG_SYSEVENT		0x00000002 /* system events */
#define NLOG_SYSSTATUS		0x00000004 /* system status (sync/unsync) */
#define NLOG_SYSSTATIST		0x00000008 /* system statistics output */

#define NLOG_OPEER			 4 /* offset for peer flags */
#define NLOG_PEERMASK		0x000000F0 /* peer log events */
#define NLOG_PEERINFO		0x00000010 /* peer info log events */
#define NLOG_PEEREVENT		0x00000020 /* peer events */
#define NLOG_PEERSTATUS		0x00000040 /* peer status (sync/unsync) */
#define NLOG_PEERSTATIST	0x00000080 /* peer statistics output */

#define NLOG_OCLOCK			 8 /* offset for clock flags */
#define NLOG_CLOCKMASK		0x00000F00 /* clock log events */
#define NLOG_CLOCKINFO		0x00000100 /* clock info log events */
#define NLOG_CLOCKEVENT		0x00000200 /* clock events */
#define NLOG_CLOCKSTATUS	0x00000400 /* clock status (sync/unsync) */
#define NLOG_CLOCKSTATIST	0x00000800 /* clock statistics output */

#define NLOG_OSYNC			12 /* offset for sync flags */
#define NLOG_SYNCMASK		0x0000F000 /* sync log events */
#define NLOG_SYNCINFO		0x00001000 /* sync info log events */
#define NLOG_SYNCEVENT		0x00002000 /* sync events */
#define NLOG_SYNCSTATUS		0x00004000 /* sync status (sync/unsync) */
#define NLOG_SYNCSTATIST	0x00008000 /* sync statistics output */

extern u_int32 ntp_syslogmask;

#define NLOG(bits)	if (ntp_syslogmask & (bits))

#define LOGIF(nlog_suffix, msl_args)				\
do {								\
	NLOG(NLOG_##nlog_suffix)	/* like "if (...) */	\
		msyslog msl_args;				\
} while (FALSE)

#endif /* NTP_SYSLOG_H */
