/*-
 * Copyright (c) 2004-2009 Apple Inc.
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

#ifndef _LIBBSM_H_
#define	_LIBBSM_H_

/*
 * NB: definitions, etc., marked with "OpenSSH compatibility" were introduced
 * solely to allow OpenSSH to compile; Darwin/Apple code should not use them.
 */

#include <sys/types.h>
#include <sys/cdefs.h>

#include <inttypes.h>		/* Required for audit.h. */
#include <time.h>		/* Required for clock_t on Linux. */

#include <bsm/audit.h>
#include <bsm/audit_record.h>

#include <stdio.h>

#ifdef __APPLE__
#include <mach/mach.h>		/* audit_token_t */
#endif

/*
 * Size parsed token vectors for execve(2) arguments and environmental
 * variables.  Note: changing these sizes affects the ABI of the token
 * structure, and as the token structure is often placed in the caller stack,
 * this is undesirable.
 */
#define	AUDIT_MAX_ARGS	128
#define	AUDIT_MAX_ENV	128

/*
 * Arguments to au_preselect(3).
 */
#define	AU_PRS_USECACHE	0
#define	AU_PRS_REREAD	1

#define	AU_PRS_SUCCESS	1
#define	AU_PRS_FAILURE	2
#define	AU_PRS_BOTH	(AU_PRS_SUCCESS|AU_PRS_FAILURE)

#define	AUDIT_EVENT_FILE	"/etc/security/audit_event"
#define	AUDIT_CLASS_FILE	"/etc/security/audit_class"
#define	AUDIT_CONTROL_FILE	"/etc/security/audit_control"
#define	AUDIT_USER_FILE		"/etc/security/audit_user"

#define	DIR_CONTROL_ENTRY		"dir"
#define	DIST_CONTROL_ENTRY		"dist"
#define	FILESZ_CONTROL_ENTRY		"filesz"
#define	FLAGS_CONTROL_ENTRY		"flags"
#define	HOST_CONTROL_ENTRY		"host"
#define	MINFREE_CONTROL_ENTRY		"minfree"
#define	NA_CONTROL_ENTRY		"naflags"
#define	POLICY_CONTROL_ENTRY		"policy"
#define	EXPIRE_AFTER_CONTROL_ENTRY	"expire-after"
#define	QSZ_CONTROL_ENTRY		"qsize"

#define	AU_CLASS_NAME_MAX	8
#define	AU_CLASS_DESC_MAX	72
#define	AU_EVENT_NAME_MAX	30
#define	AU_EVENT_DESC_MAX	50
#define	AU_USER_NAME_MAX	50
#define	AU_LINE_MAX		256
#define	MAX_AUDITSTRING_LEN	256
#define	BSM_TEXTBUFSZ		MAX_AUDITSTRING_LEN	/* OpenSSH compatibility */

#define USE_DEFAULT_QSZ		-1	/* Use system default queue size */

/*
 * Arguments to au_close(3).
 */
#define	AU_TO_NO_WRITE		0	/* Abandon audit record. */
#define	AU_TO_WRITE		1	/* Commit audit record. */

/*
 * Output format flags for au_print_flags_tok().
 */
#define	AU_OFLAG_NONE		0x0000	/* Default form. */
#define	AU_OFLAG_RAW		0x0001	/* Raw, numeric form. */
#define	AU_OFLAG_SHORT		0x0002	/* Short form. */
#define	AU_OFLAG_XML		0x0004	/* XML form. */
#define	AU_OFLAG_NORESOLVE	0x0008	/* No user/group name resolution. */

__BEGIN_DECLS
struct au_event_ent {
	au_event_t	 ae_number;
	char		*ae_name;
	char		*ae_desc;
	au_class_t	 ae_class;
};
typedef struct au_event_ent au_event_ent_t;

struct au_class_ent {
	char		*ac_name;
	au_class_t	 ac_class;
	char		*ac_desc;
};
typedef struct au_class_ent au_class_ent_t;

struct au_user_ent {
	char		*au_name;
	au_mask_t	 au_always;
	au_mask_t	 au_never;
};
typedef struct au_user_ent au_user_ent_t;
__END_DECLS

#define	ADD_TO_MASK(m, c, sel) do {					\
	if (sel & AU_PRS_SUCCESS)					\
		(m)->am_success |= c;					\
	if (sel & AU_PRS_FAILURE)					\
		(m)->am_failure |= c;					\
} while (0)

#define	SUB_FROM_MASK(m, c, sel) do {					\
	if (sel & AU_PRS_SUCCESS)					\
		(m)->am_success &= ((m)->am_success ^ c);		\
	if (sel & AU_PRS_FAILURE)					\
		(m)->am_failure &= ((m)->am_failure ^ c);		\
} while (0)

#define	ADDMASK(m, v) do {						\
	(m)->am_success |= (v)->am_success;				\
	(m)->am_failure |= (v)->am_failure;				\
} while(0)

#define	SUBMASK(m, v) do {						\
	(m)->am_success &= ((m)->am_success ^ (v)->am_success);		\
	(m)->am_failure &= ((m)->am_failure ^ (v)->am_failure);		\
} while(0)

__BEGIN_DECLS

typedef struct au_tid32 {
	u_int32_t	port;
	u_int32_t	addr;
} au_tid32_t;

typedef struct au_tid64 {
	u_int64_t	port;
	u_int32_t	addr;
} au_tid64_t;

typedef struct au_tidaddr32 {
	u_int32_t	port;
	u_int32_t	type;
	u_int32_t	addr[4];
} au_tidaddr32_t;

typedef struct au_tidaddr64 {
	u_int64_t	port;
	u_int32_t	type;
	u_int32_t	addr[4];
} au_tidaddr64_t;

/*
 * argument #              1 byte
 * argument value          4 bytes/8 bytes (32-bit/64-bit value)
 * text length             2 bytes
 * text                    N bytes + 1 terminating NULL byte
 */
typedef struct {
	u_char		 no;
	u_int32_t	 val;
	u_int16_t	 len;
	char		*text;
} au_arg32_t;

typedef struct {
	u_char		 no;
	u_int64_t	 val;
	u_int16_t	 len;
	char		*text;
} au_arg64_t;

/*
 * how to print            1 byte
 * basic unit              1 byte
 * unit count              1 byte
 * data items              (depends on basic unit)
 */
typedef struct {
	u_char	 howtopr;
	u_char	 bu;
	u_char	 uc;
	u_char	*data;
} au_arb_t;

/*
 * file access mode        4 bytes
 * owner user ID           4 bytes
 * owner group ID          4 bytes
 * file system ID          4 bytes
 * node ID                 8 bytes
 * device                  4 bytes/8 bytes (32-bit/64-bit)
 */
typedef struct {
	u_int32_t	mode;
	u_int32_t	uid;
	u_int32_t	gid;
	u_int32_t	fsid;
	u_int64_t	nid;
	u_int32_t	dev;
} au_attr32_t;

typedef struct {
	u_int32_t	mode;
	u_int32_t	uid;
	u_int32_t	gid;
	u_int32_t	fsid;
	u_int64_t	nid;
	u_int64_t	dev;
} au_attr64_t;

/*
 * count                   4 bytes
 * text                    count null-terminated string(s)
 */
typedef struct {
	u_int32_t	 count;
	char		*text[AUDIT_MAX_ARGS];
} au_execarg_t;

/*
 * count                   4 bytes
 * text                    count null-terminated string(s)
 */
typedef struct {
	u_int32_t	 count;
	char		*text[AUDIT_MAX_ENV];
} au_execenv_t;

/*
 * status                  4 bytes
 * return value            4 bytes
 */
typedef struct {
	u_int32_t	status;
	u_int32_t	ret;
} au_exit_t;

/*
 * seconds of time         4 bytes
 * milliseconds of time    4 bytes
 * file name length        2 bytes
 * file pathname           N bytes + 1 terminating NULL byte
 */
typedef struct {
	u_int32_t	 s;
	u_int32_t	 ms;
	u_int16_t	 len;
	char		*name;
} au_file_t;


/*
 * number groups           2 bytes
 * group list              N * 4 bytes
 */
typedef struct {
	u_int16_t	no;
	u_int32_t	list[AUDIT_MAX_GROUPS];
} au_groups_t;

/*
 * record byte count       4 bytes
 * version #               1 byte    [2]
 * event type              2 bytes
 * event modifier          2 bytes
 * seconds of time         4 bytes/8 bytes (32-bit/64-bit value)
 * milliseconds of time    4 bytes/8 bytes (32-bit/64-bit value)
 */
typedef struct {
	u_int32_t	size;
	u_char		version;
	u_int16_t	e_type;
	u_int16_t	e_mod;
	u_int32_t	s;
	u_int32_t	ms;
} au_header32_t;

/*
 * record byte count       4 bytes
 * version #               1 byte     [2]
 * event type              2 bytes
 * event modifier          2 bytes
 * address type/length     1 byte (XXX: actually, 4 bytes)
 * machine address         4 bytes/16 bytes (IPv4/IPv6 address)
 * seconds of time         4 bytes/8 bytes  (32/64-bits)
 * nanoseconds of time     4 bytes/8 bytes  (32/64-bits)
 */
typedef struct {
	u_int32_t	size;
	u_char		version;
	u_int16_t	e_type;
	u_int16_t	e_mod;
	u_int32_t	ad_type;
	u_int32_t	addr[4];
	u_int32_t	s;
	u_int32_t	ms;
} au_header32_ex_t;

typedef struct {
	u_int32_t	size;
	u_char		version;
	u_int16_t	e_type;
	u_int16_t	e_mod;
	u_int64_t	s;
	u_int64_t	ms;
} au_header64_t;

typedef struct {
	u_int32_t	size;
	u_char		version;
	u_int16_t	e_type;
	u_int16_t	e_mod;
	u_int32_t	ad_type;
	u_int32_t	addr[4];
	u_int64_t	s;
	u_int64_t	ms;
} au_header64_ex_t;

/*
 * internet address        4 bytes
 */
typedef struct {
	u_int32_t	addr;
} au_inaddr_t;

/*
 * type                 4 bytes
 * internet address     16 bytes
 */
typedef struct {
	u_int32_t	type;
	u_int32_t	addr[4];
} au_inaddr_ex_t;

/*
 * version and ihl         1 byte
 * type of service         1 byte
 * length                  2 bytes
 * id                      2 bytes
 * offset                  2 bytes
 * ttl                     1 byte
 * protocol                1 byte
 * checksum                2 bytes
 * source address          4 bytes
 * destination address     4 bytes
 */
typedef struct {
	u_char		version;
	u_char		tos;
	u_int16_t	len;
	u_int16_t	id;
	u_int16_t	offset;
	u_char		ttl;
	u_char		prot;
	u_int16_t	chksm;
	u_int32_t	src;
	u_int32_t	dest;
} au_ip_t;

/*
 * object ID type          1 byte
 * object ID               4 bytes
 */
typedef struct {
	u_char		type;
	u_int32_t	id;
} au_ipc_t;

/*
 * owner user ID           4 bytes
 * owner group ID          4 bytes
 * creator user ID         4 bytes
 * creator group ID        4 bytes
 * access mode             4 bytes
 * slot sequence #         4 bytes
 * key                     4 bytes
 */
typedef struct {
	u_int32_t	uid;
	u_int32_t	gid;
	u_int32_t	puid;
	u_int32_t	pgid;
	u_int32_t	mode;
	u_int32_t	seq;
	u_int32_t	key;
} au_ipcperm_t;

/*
 * port IP address         2 bytes
 */
typedef struct {
	u_int16_t	port;
} au_iport_t;

/*
 * length		2 bytes
 * data			length bytes
 */
typedef struct {
	u_int16_t	 size;
	char		*data;
} au_opaque_t;

/*
 * path length             2 bytes
 * path                    N bytes + 1 terminating NULL byte
 */
typedef struct {
	u_int16_t	 len;
	char		*path;
} au_path_t;

/*
 * audit ID                4 bytes
 * effective user ID       4 bytes
 * effective group ID      4 bytes
 * real user ID            4 bytes
 * real group ID           4 bytes
 * process ID              4 bytes
 * session ID              4 bytes
 * terminal ID
 * port ID               4 bytes/8 bytes (32-bit/64-bit value)
 * machine address       4 bytes
 */
typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tid32_t	tid;
} au_proc32_t;

typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tid64_t	tid;
} au_proc64_t;

/*
 * audit ID                4 bytes
 * effective user ID       4 bytes
 * effective group ID      4 bytes
 * real user ID            4 bytes
 * real group ID           4 bytes
 * process ID              4 bytes
 * session ID              4 bytes
 * terminal ID
 * port ID               4 bytes/8 bytes (32-bit/64-bit value)
 * type                  4 bytes
 * machine address       16 bytes
 */
typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tidaddr32_t	tid;
} au_proc32ex_t;

typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tidaddr64_t	tid;
} au_proc64ex_t;

/*
 * error status            1 byte
 * return value            4 bytes/8 bytes (32-bit/64-bit value)
 */
typedef struct {
	u_char		status;
	u_int32_t	ret;
} au_ret32_t;

typedef struct {
	u_char		err;
	u_int64_t	val;
} au_ret64_t;

/*
 * sequence number         4 bytes
 */
typedef struct {
	u_int32_t	seqno;
} au_seq_t;

/*
 * socket type             2 bytes
 * local port              2 bytes
 * local Internet address  4 bytes
 * remote port             2 bytes
 * remote Internet address 4 bytes
 */
typedef struct {
	u_int16_t	type;
	u_int16_t	l_port;
	u_int32_t	l_addr;
	u_int16_t	r_port;
	u_int32_t	r_addr;
} au_socket_t;

/*
 * socket type             2 bytes
 * local port              2 bytes
 * address type/length     4 bytes
 * local Internet address  4 bytes/16 bytes (IPv4/IPv6 address)
 * remote port             4 bytes
 * address type/length     4 bytes
 * remote Internet address 4 bytes/16 bytes (IPv4/IPv6 address)
 */
typedef struct {
	u_int16_t	domain;
	u_int16_t	type;
	u_int16_t	atype;
	u_int16_t	l_port;
	u_int32_t	l_addr[4];
	u_int32_t	r_port;
	u_int32_t	r_addr[4];
} au_socket_ex32_t;

/*
 * socket family           2 bytes
 * local port              2 bytes
 * socket address          4 bytes/16 bytes (IPv4/IPv6 address)
 */
typedef struct {
	u_int16_t	family;
	u_int16_t	port;
	u_int32_t	addr[4];
} au_socketinet_ex32_t;

typedef struct {
	u_int16_t	family;
	u_int16_t	port;
	u_int32_t	addr;
} au_socketinet32_t;

/*
 * socket family           2 bytes
 * path                    104 bytes
 */
typedef struct {
	u_int16_t	family;
	char		path[104];
} au_socketunix_t;

/*
 * audit ID                4 bytes
 * effective user ID       4 bytes
 * effective group ID      4 bytes
 * real user ID            4 bytes
 * real group ID           4 bytes
 * process ID              4 bytes
 * session ID              4 bytes
 * terminal ID
 * 	port ID               4 bytes/8 bytes (32-bit/64-bit value)
 * 	machine address       4 bytes
 */
typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tid32_t	tid;
} au_subject32_t;

typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tid64_t	tid;
} au_subject64_t;

/*
 * audit ID                4 bytes
 * effective user ID       4 bytes
 * effective group ID      4 bytes
 * real user ID            4 bytes
 * real group ID           4 bytes
 * process ID              4 bytes
 * session ID              4 bytes
 * terminal ID
 * port ID               4 bytes/8 bytes (32-bit/64-bit value)
 * type                  4 bytes
 * machine address       16 bytes
 */
typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tidaddr32_t	tid;
} au_subject32ex_t;

typedef struct {
	u_int32_t	auid;
	u_int32_t	euid;
	u_int32_t	egid;
	u_int32_t	ruid;
	u_int32_t	rgid;
	u_int32_t	pid;
	u_int32_t	sid;
	au_tidaddr64_t	tid;
} au_subject64ex_t;

/*
 * text length             2 bytes
 * text                    N bytes + 1 terminating NULL byte
 */
typedef struct {
	u_int16_t	 len;
	char		*text;
} au_text_t;

/*
 * upriv status         1 byte
 * privstr len          2 bytes
 * privstr              N bytes + 1 (\0 byte)
 */
typedef struct {
	u_int8_t	 sorf;
	u_int16_t	 privstrlen;
	char		*priv;
} au_priv_t;

/*
* privset
* privtstrlen		2 bytes
* privtstr		N Bytes + 1
* privstrlen		2 bytes
* privstr		N Bytes + 1
*/
typedef struct {
	u_int16_t	 privtstrlen;
	char		*privtstr;
	u_int16_t	 privstrlen;
	char		*privstr;
} au_privset_t;

/*
 * zonename length	2 bytes
 * zonename text	N bytes + 1 NULL terminator
 */
typedef struct {
	u_int16_t	 len;
	char		*zonename;
} au_zonename_t;

typedef struct {
	u_int32_t	ident;
	u_int16_t	filter;
	u_int16_t	flags;
	u_int32_t	fflags;
	u_int32_t	data;
} au_kevent_t;

typedef struct {
	u_int16_t	 length;
	char		*data;
} au_invalid_t;

/*
 * trailer magic number    2 bytes
 * record byte count       4 bytes
 */
typedef struct {
	u_int16_t	magic;
	u_int32_t	count;
} au_trailer_t;

struct tokenstr {
	u_char	 id;
	u_char	*data;
	size_t	 len;
	union {
		au_arg32_t		arg32;
		au_arg64_t		arg64;
		au_arb_t		arb;
		au_attr32_t		attr32;
		au_attr64_t		attr64;
		au_execarg_t		execarg;
		au_execenv_t		execenv;
		au_exit_t		exit;
		au_file_t		file;
		au_groups_t		grps;
		au_header32_t		hdr32;
		au_header32_ex_t	hdr32_ex;
		au_header64_t		hdr64;
		au_header64_ex_t	hdr64_ex;
		au_inaddr_t		inaddr;
		au_inaddr_ex_t		inaddr_ex;
		au_ip_t			ip;
		au_ipc_t		ipc;
		au_ipcperm_t		ipcperm;
		au_iport_t		iport;
		au_opaque_t		opaque;
		au_path_t		path;
		au_proc32_t		proc32;
		au_proc32ex_t		proc32_ex;
		au_proc64_t		proc64;
		au_proc64ex_t		proc64_ex;
		au_ret32_t		ret32;
		au_ret64_t		ret64;
		au_seq_t		seq;
		au_socket_t		socket;
		au_socket_ex32_t	socket_ex32;
		au_socketinet_ex32_t	sockinet_ex32;
		au_socketunix_t		sockunix;
		au_subject32_t		subj32;
		au_subject32ex_t	subj32_ex;
		au_subject64_t		subj64;
		au_subject64ex_t	subj64_ex;
		au_text_t		text;
		au_kevent_t		kevent;
		au_invalid_t		invalid;
		au_trailer_t		trail;
		au_zonename_t		zonename;
		au_priv_t		priv;
		au_privset_t		privset;
	} tt; /* The token is one of the above types */
};

typedef struct tokenstr tokenstr_t;

int			 audit_submit(short au_event, au_id_t auid,
			    char status, int reterr, const char *fmt, ...);

/*
 * Functions relating to querying audit class information.
 */
void			 setauclass(void);
void			 endauclass(void);
struct au_class_ent	*getauclassent(void);
struct au_class_ent	*getauclassent_r(au_class_ent_t *class_int);
struct au_class_ent	*getauclassnam(const char *name);
struct au_class_ent	*getauclassnam_r(au_class_ent_t *class_int,
			    const char *name);
struct au_class_ent	*getauclassnum(au_class_t class_number);
struct au_class_ent	*getauclassnum_r(au_class_ent_t *class_int,
			    au_class_t class_number);

/*
 * Functions relating to querying audit control information.
 */
void			 setac(void);
void			 endac(void);
int			 getacdir(char *name, int len);
int			 getacdist(void);
int			 getacexpire(int *andflg, time_t *age, size_t *size);
int			 getacfilesz(size_t *size_val);
int			 getacqsize(int *size_val);
int			 getacflg(char *auditstr, int len);
int			 getachost(char *auditstr, size_t len);
int			 getacmin(int *min_val);
int			 getacna(char *auditstr, int len);
int			 getacpol(char *auditstr, size_t len);
int			 getauditflagsbin(char *auditstr, au_mask_t *masks);
int			 getauditflagschar(char *auditstr, au_mask_t *masks,
			    int verbose);
int			 au_preselect(au_event_t event, au_mask_t *mask_p,
			    int sorf, int flag);
ssize_t			 au_poltostr(int policy, size_t maxsize, char *buf);
int			 au_strtopol(const char *polstr, int *policy);

/*
 * Functions relating to querying audit event information.
 */
void			 setauevent(void);
void			 endauevent(void);
struct au_event_ent	*getauevent(void);
struct au_event_ent	*getauevent_r(struct au_event_ent *e);
struct au_event_ent	*getauevnam(const char *name);
struct au_event_ent	*getauevnam_r(struct au_event_ent *e,
			    const char *name);
struct au_event_ent	*getauevnum(au_event_t event_number);
struct au_event_ent	*getauevnum_r(struct au_event_ent *e,
			    au_event_t event_number);
au_event_t		*getauevnonam(const char *event_name);
au_event_t		*getauevnonam_r(au_event_t *ev,
			    const char *event_name);

/*
 * Functions relating to querying audit user information.
 */
void			 setauuser(void);
void			 endauuser(void);
struct au_user_ent	*getauuserent(void);
struct au_user_ent	*getauuserent_r(struct au_user_ent *u);
struct au_user_ent	*getauusernam(const char *name);
struct au_user_ent	*getauusernam_r(struct au_user_ent *u,
			    const char *name);
int			 au_user_mask(char *username, au_mask_t *mask_p);
int			 getfauditflags(au_mask_t *usremask,
			    au_mask_t *usrdmask, au_mask_t *lastmask);

/*
 * Functions for reading and printing records and tokens from audit trails.
 */
int			 au_read_rec(FILE *fp, u_char **buf);
int			 au_fetch_tok(tokenstr_t *tok, u_char *buf, int len);
//XXX The following interface has different prototype from BSM
void			 au_print_tok(FILE *outfp, tokenstr_t *tok,
			    char *del, char raw, char sfrm);
void			 au_print_flags_tok(FILE *outfp, tokenstr_t *tok,
			    char *del, int oflags);
void			 au_print_tok_xml(FILE *outfp, tokenstr_t *tok,
			    char *del, char raw, char sfrm);

/* 
 * Functions relating to XML output.
 */
void			 au_print_xml_header(FILE *outfp);
void			 au_print_xml_footer(FILE *outfp);

const char	 *au_strerror(u_char bsm_error);
__END_DECLS

/*
 * The remaining APIs are associated with Apple's BSM implementation, in
 * particular as relates to Mach IPC auditing and triggers passed via Mach
 * IPC.
 */
#ifdef __APPLE__
#include <sys/appleapiopts.h>

/**************************************************************************
 **************************************************************************
 ** The following definitions, functions, etc., are NOT officially
 ** supported: they may be changed or removed in the future.  Do not use
 ** them unless you are prepared to cope with that eventuality.
 **************************************************************************
 **************************************************************************/

#ifdef __APPLE_API_PRIVATE
#define	__BSM_INTERNAL_NOTIFY_KEY	"com.apple.audit.change"
#endif /* __APPLE_API_PRIVATE */

/*
 * au_get_state() return values
 * XXX  use AUC_* values directly instead (<bsm/audit.h>); AUDIT_OFF and
 * AUDIT_ON are deprecated and WILL be removed.
 */
#ifdef __APPLE_API_PRIVATE
#define	AUDIT_OFF	AUC_NOAUDIT
#define	AUDIT_ON	AUC_AUDITING
#endif /* __APPLE_API_PRIVATE */
#endif /* !__APPLE__ */

/*
 * Error return codes for audit_set_terminal_id(), audit_write() and its
 * brethren.  We have 255 (not including kAUNoErr) to play with.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
enum {
	kAUNoErr			= 0,
	kAUBadParamErr			= -66049,
	kAUStatErr,
	kAUSysctlErr,
	kAUOpenErr,
	kAUMakeSubjectTokErr,
	kAUWriteSubjectTokErr,
	kAUWriteCallerTokErr,
	kAUMakeReturnTokErr,
	kAUWriteReturnTokErr,
	kAUCloseErr,
	kAUMakeTextTokErr,
	kAULastErr
};

#ifdef __APPLE__
/*
 * Error return codes for au_get_state() and/or its private support
 * functions.  These codes are designed to be compatible with the
 * NOTIFY_STATUS_* codes defined in <notify.h> but non-overlapping.
 * Any changes to notify(3) may cause these values to change in future.
 *
 * AU_UNIMPL should never happen unless you've changed your system software
 * without rebooting.  Shame on you.
 */
#ifdef __APPLE_API_PRIVATE
#define	AU_UNIMPL	NOTIFY_STATUS_FAILED + 1	/* audit unimplemented */
#endif /* __APPLE_API_PRIVATE */
#endif /* !__APPLE__ */

__BEGIN_DECLS
/*
 * XXX  This prototype should be in audit_record.h
 *
 * au_free_token()
 *
 * @summary - au_free_token() deallocates a token_t created by any of
 * the au_to_*() BSM API functions.
 *
 * The BSM API generally manages deallocation of token_t objects.  However,
 * if au_write() is passed a bad audit descriptor, the token_t * parameter
 * will be left untouched.  In that case, the caller can deallocate the
 * token_t using au_free_token() if desired.  This is, in fact, what
 * audit_write() does, in keeping with the existing memory management model
 * of the BSM API.
 *
 * @param tok - A token_t * generated by one of the au_to_*() BSM API
 * calls.  For convenience, tok may be NULL, in which case
 * au_free_token() returns immediately.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
void	au_free_token(token_t *tok);

/*
 * Lightweight check to determine if auditing is enabled.  If a client
 * wants to use this to govern whether an entire series of audit calls
 * should be made--as in the common case of a caller building a set of
 * tokens, then writing them--it should cache the audit status in a local
 * variable.  This call always returns the current state of auditing.
 *
 * @return - AUC_AUDITING or AUC_NOAUDIT if no error occurred.
 * Otherwise the function can return any of the errno values defined for
 * setaudit(2), or AU_UNIMPL if audit does not appear to be supported by
 * the system.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	au_get_state(void);

/*
 * Initialize the audit notification.  If it has not already been initialized
 * it will automatically on the first call of au_get_state().
 */
uint32_t	au_notify_initialize(void);

/*
 * Cancel audit notification and free the resources associated with it.
 * Responsible code that no longer needs to use au_get_state() should call
 * this.
 */
int		au_notify_terminate(void);
__END_DECLS

/* OpenSSH compatibility */
int	cannot_audit(int);

__BEGIN_DECLS
/*
 * audit_set_terminal_id()
 *
 * @summary - audit_set_terminal_id() fills in an au_tid_t struct, which is
 * used in audit session initialization by processes like /usr/bin/login.
 *
 * @param tid - A pointer to an au_tid_t struct.
 *
 * @return - kAUNoErr on success; kAUBadParamErr if tid is NULL, kAUStatErr
 * or kAUSysctlErr if one of the underlying system calls fails (a message
 * is sent to the system log in those cases).
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_set_terminal_id(au_tid_t *tid);

/*
 * BEGIN au_write() WRAPPERS
 *
 * The following calls all wrap the existing BSM API.  They use the
 * provided subject information, if any, to construct the subject token
 * required for every log message.  They use the provided return/error
 * value(s), if any, to construct the success/failure indication required
 * for every log message.  They only permit one "miscellaneous" token,
 * which should contain the event-specific logging information mandated by
 * CAPP.
 *
 * All these calls assume the caller has previously determined that
 * auditing is enabled by calling au_get_state().
 */

/*
 * audit_write()
 *
 * @summary - audit_write() is the basis for the other audit_write_*()
 * calls.  Performs a basic write of an audit record (subject, additional
 * info, success/failure).  Note that this call only permits logging one
 * caller-specified token; clients needing to log more flexibly must use
 * the existing BSM API (au_open(), et al.) directly.
 *
 * Note on memory management: audit_write() guarantees that the token_t *s
 * passed to it will be deallocated whether or not the underlying write to
 * the audit log succeeded.  This addresses an inconsistency in the
 * underlying BSM API in which token_t *s are usually but not always
 * deallocated.
 *
 * @param event_code - The code for the event being logged.  This should
 * be one of the AUE_ values in /usr/include/bsm/audit_uevents.h.
 *
 * @param subject - A token_t * generated by au_to_subject(),
 * au_to_subject32(), au_to_subject64(), or au_to_me().  If no subject is
 * required, subject should be NULL.
 *
 * @param misctok - A token_t * generated by one of the au_to_*() BSM API
 * calls.  This should correspond to the additional information required by
 * CAPP for the event being audited.  If no additional information is
 * required, misctok should be NULL.
 *
 * @param retval - The return value to be logged for this event.  This
 * should be 0 (zero) for success, otherwise the value is event-specific.
 *
 * @param errcode - Any error code associated with the return value (e.g.,
 * errno or h_errno).  If there was no error, errcode should be 0 (zero).
 *
 * @return - The status of the call: 0 (zero) on success, else one of the
 * kAU*Err values defined above.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_write(short event_code, token_t *subject, token_t *misctok,
	    char retval, int errcode);

/*
 * audit_write_success()
 *
 * @summary - audit_write_success() records an auditable event that did not
 * encounter an error.  The interface is designed to require as little
 * direct use of the au_to_*() API as possible.  It builds a subject token
 * from the information passed in and uses that to invoke audit_write().
 * A subject, as defined by CAPP, is a process acting on the user's behalf.
 *
 * If the subject information is the same as the current process, use
 * au_write_success_self().
 *
 * @param event_code - The code for the event being logged.  This should
 * be one of the AUE_ values in /usr/include/bsm/audit_uevents.h.
 *
 * @param misctok - A token_t * generated by one of the au_to_*() BSM API
 * calls.  This should correspond to the additional information required by
 * CAPP for the event being audited.  If no additional information is
 * required, misctok should be NULL.
 *
 * @param auid - The subject's audit ID.
 *
 * @param euid - The subject's effective user ID.
 *
 * @param egid - The subject's effective group ID.
 *
 * @param ruid - The subject's real user ID.
 *
 * @param rgid - The subject's real group ID.
 *
 * @param pid - The subject's process ID.
 *
 * @param sid - The subject's session ID.
 *
 * @param tid - The subject's terminal ID.
 *
 * @return - The status of the call: 0 (zero) on success, else one of the
 * kAU*Err values defined above.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_write_success(short event_code, token_t *misctok, au_id_t auid,
	    uid_t euid, gid_t egid, uid_t ruid, gid_t rgid, pid_t pid,
	    au_asid_t sid, au_tid_t *tid);

/*
 * audit_write_success_self()
 *
 * @summary - Similar to audit_write_success(), but used when the subject
 * (process) is owned and operated by the auditable user him/herself.
 *
 * @param event_code - The code for the event being logged.  This should
 * be one of the AUE_ values in /usr/include/bsm/audit_uevents.h.
 *
 * @param misctok - A token_t * generated by one of the au_to_*() BSM API
 * calls.  This should correspond to the additional information required by
 * CAPP for the event being audited.  If no additional information is
 * required, misctok should be NULL.
 *
 * @return - The status of the call: 0 (zero) on success, else one of the
 * kAU*Err values defined above.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_write_success_self(short event_code, token_t *misctok);

/*
 * audit_write_failure()
 *
 * @summary - audit_write_failure() records an auditable event that
 * encountered an error.  The interface is designed to require as little
 * direct use of the au_to_*() API as possible.  It builds a subject token
 * from the information passed in and uses that to invoke audit_write().
 * A subject, as defined by CAPP, is a process acting on the user's behalf.
 *
 * If the subject information is the same as the current process, use
 * au_write_failure_self().
 *
 * @param event_code - The code for the event being logged.  This should
 * be one of the AUE_ values in /usr/include/bsm/audit_uevents.h.
 *
 * @param errmsg - A text message providing additional information about
 * the event being audited.
 *
 * @param errret - A numerical value providing additional information about
 * the error.  This is intended to store the value of errno or h_errno if
 * it's relevant.  This can be 0 (zero) if no additional information is
 * available.
 *
 * @param auid - The subject's audit ID.
 *
 * @param euid - The subject's effective user ID.
 *
 * @param egid - The subject's effective group ID.
 *
 * @param ruid - The subject's real user ID.
 *
 * @param rgid - The subject's real group ID.
 *
 * @param pid - The subject's process ID.
 *
 * @param sid - The subject's session ID.
 *
 * @param tid - The subject's terminal ID.
 *
 * @return - The status of the call: 0 (zero) on success, else one of the
 * kAU*Err values defined above.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_write_failure(short event_code, char *errmsg, int errret,
	    au_id_t auid, uid_t euid, gid_t egid, uid_t ruid, gid_t rgid,
	    pid_t pid, au_asid_t sid, au_tid_t *tid);

/*
 * audit_write_failure_self()
 *
 * @summary - Similar to audit_write_failure(), but used when the subject
 * (process) is owned and operated by the auditable user him/herself.
 *
 * @param event_code - The code for the event being logged.  This should
 * be one of the AUE_ values in /usr/include/bsm/audit_uevents.h.
 *
 * @param errmsg - A text message providing additional information about
 * the event being audited.
 *
 * @param errret - A numerical value providing additional information about
 * the error.  This is intended to store the value of errno or h_errno if
 * it's relevant.  This can be 0 (zero) if no additional information is
 * available.
 *
 * @return - The status of the call: 0 (zero) on success, else one of the
 * kAU*Err values defined above.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_write_failure_self(short event_code, char *errmsg, int errret);

/*
 * audit_write_failure_na()
 *
 * @summary - audit_write_failure_na() records errors during login.  Such
 * errors are implicitly non-attributable (i.e., not ascribable to any user).
 *
 * @param event_code - The code for the event being logged.  This should
 * be one of the AUE_ values in /usr/include/bsm/audit_uevents.h.
 *
 * @param errmsg - A text message providing additional information about
 * the event being audited.
 *
 * @param errret - A numerical value providing additional information about
 * the error.  This is intended to store the value of errno or h_errno if
 * it's relevant.  This can be 0 (zero) if no additional information is
 * available.
 *
 * @param euid - The subject's effective user ID.
 *
 * @param egid - The subject's effective group ID.
 *
 * @param pid - The subject's process ID.
 *
 * @param tid - The subject's terminal ID.
 *
 * @return - The status of the call: 0 (zero) on success, else one of the
 * kAU*Err values defined above.
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
int	audit_write_failure_na(short event_code, char *errmsg, int errret,
	    uid_t euid, gid_t egid, pid_t pid, au_tid_t *tid);

/* END au_write() WRAPPERS */

#ifdef  __APPLE__
/*
 * audit_token_to_au32()
 *
 * @summary - Extract information from an audit_token_t, used to identify
 * Mach tasks and senders of Mach messages as subjects to the audit system.
 * audit_tokent_to_au32() is the only method that should be used to parse
 * an audit_token_t, since its internal representation may change over
 * time.  A pointer parameter may be NULL if that information is not
 * needed.
 *
 * @param atoken - the audit token containing the desired information
 *
 * @param auidp - Pointer to a uid_t; on return will be set to the task or
 * sender's audit user ID
 *
 * @param euidp - Pointer to a uid_t; on return will be set to the task or
 * sender's effective user ID
 *
 * @param egidp - Pointer to a gid_t; on return will be set to the task or
 * sender's effective group ID
 *
 * @param ruidp - Pointer to a uid_t; on return will be set to the task or
 * sender's real user ID
 *
 * @param rgidp - Pointer to a gid_t; on return will be set to the task or
 * sender's real group ID
 *
 * @param pidp - Pointer to a pid_t; on return will be set to the task or
 * sender's process ID
 *
 * @param asidp - Pointer to an au_asid_t; on return will be set to the
 * task or sender's audit session ID
 *
 * @param tidp - Pointer to an au_tid_t; on return will be set to the task
 * or sender's terminal ID
 *
 * XXXRW: In Apple's bsm-8, these are marked __APPLE_API_PRIVATE.
 */
void audit_token_to_au32(
	audit_token_t	 atoken,
	uid_t		*auidp,
	uid_t		*euidp,
	gid_t		*egidp,
	uid_t		*ruidp,
	gid_t		*rgidp,
	pid_t		*pidp,
	au_asid_t	*asidp,
	au_tid_t	*tidp);
#endif /* !__APPLE__ */

/*
 * Wrapper functions to auditon(2).
 */
int audit_get_car(char *path, size_t sz);
int audit_get_class(au_evclass_map_t *evc_map, size_t sz);
int audit_set_class(au_evclass_map_t *evc_map, size_t sz);
int audit_get_event(au_evname_map_t *evn_map, size_t sz);
int audit_set_event(au_evname_map_t *evn_map, size_t sz);
int audit_get_cond(int *cond);
int audit_set_cond(int *cond);
int audit_get_cwd(char *path, size_t sz);
int audit_get_fsize(au_fstat_t *fstat, size_t sz);
int audit_set_fsize(au_fstat_t *fstat, size_t sz);
int audit_get_kmask(au_mask_t *kmask, size_t sz);
int audit_set_kmask(au_mask_t *kmask, size_t sz);
int audit_get_kaudit(auditinfo_addr_t *aia, size_t sz);
int audit_set_kaudit(auditinfo_addr_t *aia, size_t sz);
int audit_set_pmask(auditpinfo_t *api, size_t sz);
int audit_get_pinfo(auditpinfo_t *api, size_t sz);
int audit_get_pinfo_addr(auditpinfo_addr_t *apia, size_t sz);
int audit_get_policy(int *policy);
int audit_set_policy(int *policy);
int audit_get_qctrl(au_qctrl_t *qctrl, size_t sz);
int audit_set_qctrl(au_qctrl_t *qctrl, size_t sz);
int audit_get_sinfo_addr(auditinfo_addr_t *aia, size_t sz);
int audit_get_stat(au_stat_t *stats, size_t sz);
int audit_set_stat(au_stat_t *stats, size_t sz);
int audit_send_trigger(int *trigger);

__END_DECLS

#endif /* !_LIBBSM_H_ */
