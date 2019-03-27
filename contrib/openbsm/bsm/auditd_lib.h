/*-
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _BSM_AUDITD_LIB_H_
#define	_BSM_AUDITD_LIB_H_

/*
 * Lengths for audit trail file components.
 */
#define	NOT_TERMINATED		"not_terminated"
#define	CRASH_RECOVERY		"crash_recovery"
#define	PREFIX_LEN	(sizeof("YYYYMMDDhhmmss") - 1)
#define	POSTFIX_LEN	PREFIX_LEN
#define	FILENAME_LEN	(PREFIX_LEN + 1 + POSTFIX_LEN)
#define	TIMESTAMP_LEN	POSTFIX_LEN

/*
 * Macro to generate the timestamp string for trail file.
 */
#define	getTSstr(t, b, l)						\
	( (((t) = time(0)) == (time_t)-1 ) ||				\
	    !strftime((b), (l), "%Y%m%d%H%M%S", gmtime(&(t)) ) ) ? -1 : 0

/*
 * The symbolic link to the currently active audit trail file.
 */
#define	AUDIT_CURRENT_LINK	"/var/audit/current"

/*
 * Path of auditd plist file for launchd.
 */ 
#define	AUDITD_PLIST_FILE 	\
	    "/System/Library/LaunchDaemons/com.apple.auditd.plist"

/*
 * Error return codes for auditd_lib functions.
 */
#define	ADE_NOERR	  0	/* No Error or Success. */
#define	ADE_PARSE	 -1	/* Error parsing audit_control(5). */
#define	ADE_AUDITON	 -2	/* auditon(2) call failed. */
#define	ADE_NOMEM	 -3	/* Error allocating memory. */
#define	ADE_SOFTLIM	 -4	/* All audit log directories over soft limit. */
#define	ADE_HARDLIM	 -5	/* All audit log directories over hard limit. */
#define	ADE_STRERR	 -6	/* Error creating file name string. */
#define	ADE_AU_OPEN	 -7	/* au_open(3) failed. */
#define	ADE_AU_CLOSE	 -8	/* au_close(3) failed. */
#define	ADE_SETAUDIT	 -9	/* setaudit(2) or setaudit_addr(2) failed. */
#define	ADE_ACTL	-10	/* "Soft" error with auditctl(2). */ 
#define	ADE_ACTLERR	-11	/* "Hard" error with auditctl(2). */
#define	ADE_SWAPERR	-12	/* The audit trail file could not be swap. */ 
#define	ADE_RENAME	-13	/* Error renaming crash recovery file. */
#define	ADE_READLINK	-14	/* Error reading 'current' link. */	
#define	ADE_SYMLINK	-15	/* Error creating 'current' link. */
#define	ADE_INVAL	-16	/* Invalid argument. */
#define	ADE_GETADDR	-17	/* Error resolving address from hostname. */
#define	ADE_ADDRFAM	-18	/* Address family not supported. */
#define	ADE_EXPIRE	-19	/* Error expiring audit trail files. */

/*
 * auditd_lib functions.
 */
const char *auditd_strerror(int errcode);
int auditd_set_minfree(void);
int auditd_expire_trails(int (*warn_expired)(char *));
int auditd_read_dirs(int (*warn_soft)(char *), int (*warn_hard)(char *));
void auditd_close_dirs(void);
int auditd_set_dist(void);
int auditd_set_evcmap(void);
int auditd_set_namask(void);
int auditd_set_policy(void);
int auditd_set_fsize(void);
int auditd_set_qsize(void);
int auditd_set_host(void);
int auditd_swap_trail(char *TS, char **newfile, gid_t gid,
    int (*warn_getacdir)(char *));
int auditd_prevent_audit(void);
int auditd_gen_record(int event, char *path);
int auditd_new_curlink(char *curfile);
int auditd_rename(const char *fromname, const char *toname);
int audit_quick_start(void);
int audit_quick_stop(void);

#endif /* !_BSM_AUDITD_LIB_H_ */
