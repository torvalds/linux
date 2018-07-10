/* vi: set sw=4 ts=4: */
/*
 * Simple FTP daemon, based on vsftpd 2.0.7 (written by Chris Evans)
 *
 * Author: Adam Tkac <vonsch@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * Only subset of FTP protocol is implemented but vast majority of clients
 * should not have any problem.
 *
 * You have to run this daemon via inetd.
 */
//config:config FTPD
//config:	bool "ftpd (30 kb)"
//config:	default y
//config:	help
//config:	Simple FTP daemon. You have to run it via inetd.
//config:
//config:config FEATURE_FTPD_WRITE
//config:	bool "Enable -w (upload commands)"
//config:	default y
//config:	depends on FTPD
//config:	help
//config:	Enable -w option. "ftpd -w" will accept upload commands
//config:	such as STOR, STOU, APPE, DELE, MKD, RMD, rename commands.
//config:
//config:config FEATURE_FTPD_ACCEPT_BROKEN_LIST
//config:	bool "Enable workaround for RFC-violating clients"
//config:	default y
//config:	depends on FTPD
//config:	help
//config:	Some ftp clients (among them KDE's Konqueror) issue illegal
//config:	"LIST -l" requests. This option works around such problems.
//config:	It might prevent you from listing files starting with "-" and
//config:	it increases the code size by ~40 bytes.
//config:	Most other ftp servers seem to behave similar to this.
//config:
//config:config FEATURE_FTPD_AUTHENTICATION
//config:	bool "Enable authentication"
//config:	default y
//config:	depends on FTPD
//config:	help
//config:	Require login, and change to logged in user's UID:GID before
//config:	accessing any files. Option "-a USER" allows "anonymous"
//config:	logins (treats them as if USER logged in).
//config:
//config:	If this option is not selected, ftpd runs with the rights
//config:	of the user it was started under, and does not require login.
//config:	Take care to not launch it under root.

//applet:IF_FTPD(APPLET(ftpd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FTPD) += ftpd.o

//usage:#define ftpd_trivial_usage
//usage:       "[-wvS]"IF_FEATURE_FTPD_AUTHENTICATION(" [-a USER]")" [-t N] [-T N] [DIR]"
//usage:#define ftpd_full_usage "\n\n"
//usage:	IF_NOT_FEATURE_FTPD_AUTHENTICATION(
//usage:       "Anonymous FTP server. Client access occurs under ftpd's UID.\n"
//usage:	)
//usage:	IF_FEATURE_FTPD_AUTHENTICATION(
//usage:       "FTP server. "
//usage:	)
//usage:       "Chroots to DIR, if this fails (run by non-root), cds to it.\n"
//usage:       "Should be used as inetd service, inetd.conf line:\n"
//usage:       "	21 stream tcp nowait root ftpd ftpd /files/to/serve\n"
//usage:       "Can be run from tcpsvd:\n"
//usage:       "	tcpsvd -vE 0.0.0.0 21 ftpd /files/to/serve"
//usage:     "\n"
//usage:     "\n	-w	Allow upload"
//usage:	IF_FEATURE_FTPD_AUTHENTICATION(
//usage:     "\n	-A	No login required, client access occurs under ftpd's UID"
//
// if !FTPD_AUTHENTICATION, -A is accepted too, but not shown in --help
// since it's the only supported mode in that configuration
//
//usage:     "\n	-a USER	Enable 'anonymous' login and map it to USER"
//usage:	)
//usage:     "\n	-v	Log errors to stderr. -vv: verbose log"
//usage:     "\n	-S	Log errors to syslog. -SS: verbose log"
//usage:     "\n	-t,-T N	Idle and absolute timeout"

#include "libbb.h"
#include "common_bufsiz.h"
#include <syslog.h>
#include <netinet/tcp.h>

#define FTP_DATACONN            150
#define FTP_NOOPOK              200
#define FTP_TYPEOK              200
#define FTP_PORTOK              200
#define FTP_STRUOK              200
#define FTP_MODEOK              200
#define FTP_ALLOOK              202
#define FTP_STATOK              211
#define FTP_STATFILE_OK         213
#define FTP_HELP                214
#define FTP_SYSTOK              215
#define FTP_GREET               220
#define FTP_GOODBYE             221
#define FTP_TRANSFEROK          226
#define FTP_PASVOK              227
/*#define FTP_EPRTOK              228*/
#define FTP_EPSVOK              229
#define FTP_LOGINOK             230
#define FTP_CWDOK               250
#define FTP_RMDIROK             250
#define FTP_DELEOK              250
#define FTP_RENAMEOK            250
#define FTP_PWDOK               257
#define FTP_MKDIROK             257
#define FTP_GIVEPWORD           331
#define FTP_RESTOK              350
#define FTP_RNFROK              350
#define FTP_TIMEOUT             421
#define FTP_BADSENDCONN         425
#define FTP_BADSENDNET          426
#define FTP_BADSENDFILE         451
#define FTP_BADCMD              500
#define FTP_COMMANDNOTIMPL      502
#define FTP_NEEDUSER            503
#define FTP_NEEDRNFR            503
#define FTP_BADSTRU             504
#define FTP_BADMODE             504
#define FTP_LOGINERR            530
#define FTP_FILEFAIL            550
#define FTP_NOPERM              550
#define FTP_UPLOADFAIL          553

#define STR1(s) #s
#define STR(s) STR1(s)

/* Convert a constant to 3-digit string, packed into uint32_t */
enum {
	/* Shift for Nth decimal digit */
	SHIFT2  =  0 * BB_LITTLE_ENDIAN + 24 * BB_BIG_ENDIAN,
	SHIFT1  =  8 * BB_LITTLE_ENDIAN + 16 * BB_BIG_ENDIAN,
	SHIFT0  = 16 * BB_LITTLE_ENDIAN + 8 * BB_BIG_ENDIAN,
	/* And for 4th position (space) */
	SHIFTsp = 24 * BB_LITTLE_ENDIAN + 0 * BB_BIG_ENDIAN,
};
#define STRNUM32(s) (uint32_t)(0 \
	| (('0' + ((s) / 1 % 10)) << SHIFT0) \
	| (('0' + ((s) / 10 % 10)) << SHIFT1) \
	| (('0' + ((s) / 100 % 10)) << SHIFT2) \
)
#define STRNUM32sp(s) (uint32_t)(0 \
	| (' ' << SHIFTsp) \
	| (('0' + ((s) / 1 % 10)) << SHIFT0) \
	| (('0' + ((s) / 10 % 10)) << SHIFT1) \
	| (('0' + ((s) / 100 % 10)) << SHIFT2) \
)

#define MSG_OK "Operation successful\r\n"
#define MSG_ERR "Error\r\n"

struct globals {
	int pasv_listen_fd;
#if !BB_MMU
	int root_fd;
#endif
	int local_file_fd;
	unsigned end_time;
	unsigned timeout;
	unsigned verbose;
	off_t local_file_pos;
	off_t restart_pos;
	len_and_sockaddr *local_addr;
	len_and_sockaddr *port_addr;
	char *ftp_cmd;
	char *ftp_arg;
#if ENABLE_FEATURE_FTPD_WRITE
	char *rnfr_filename;
#endif
	/* We need these aligned to uint32_t */
	char msg_ok [(sizeof("NNN " MSG_OK ) + 3) & 0xfffc];
	char msg_err[(sizeof("NNN " MSG_ERR) + 3) & 0xfffc];
} FIX_ALIASING;
#define G (*ptr_to_globals)
/* ^^^ about 75 bytes smaller code than this: */
//#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	/*setup_common_bufsiz();*/ \
	\
	/* Moved to main */ \
	/*strcpy(G.msg_ok  + 4, MSG_OK );*/ \
	/*strcpy(G.msg_err + 4, MSG_ERR);*/ \
} while (0)


static char *
escape_text(const char *prepend, const char *str, unsigned escapee)
{
	unsigned retlen, remainlen, chunklen;
	char *ret, *found;
	char append;

	append = (char)escapee;
	escapee >>= 8;

	remainlen = strlen(str);
	retlen = strlen(prepend);
	ret = xmalloc(retlen + remainlen * 2 + 1 + 1);
	strcpy(ret, prepend);

	for (;;) {
		found = strchrnul(str, escapee);
		chunklen = found - str + 1;

		/* Copy chunk up to and including escapee (or NUL) to ret */
		memcpy(ret + retlen, str, chunklen);
		retlen += chunklen;

		if (*found == '\0') {
			/* It wasn't escapee, it was NUL! */
			ret[retlen - 1] = append; /* replace NUL */
			ret[retlen] = '\0'; /* add NUL */
			break;
		}
		ret[retlen++] = escapee; /* duplicate escapee */
		str = found + 1;
	}
	return ret;
}

/* Returns strlen as a bonus */
static unsigned
replace_char(char *str, char from, char to)
{
	char *p = str;
	while (*p) {
		if (*p == from)
			*p = to;
		p++;
	}
	return p - str;
}

static void
verbose_log(const char *str)
{
	bb_error_msg("%.*s", (int)strcspn(str, "\r\n"), str);
}

/* NB: status_str is char[4] packed into uint32_t */
static void
cmdio_write(uint32_t status_str, const char *str)
{
	char *response;
	int len;

	/* FTP uses telnet protocol for command link.
	 * In telnet, 0xff is an escape char, and needs to be escaped: */
	response = escape_text((char *) &status_str, str, (0xff << 8) + '\r');

	/* FTP sends embedded LFs as NULs */
	len = replace_char(response, '\n', '\0');

	response[len++] = '\n'; /* tack on trailing '\n' */
	xwrite(STDOUT_FILENO, response, len);
	if (G.verbose > 1)
		verbose_log(response);
	free(response);
}

static void
cmdio_write_ok(unsigned status)
{
	*(bb__aliased_uint32_t *) G.msg_ok = status;
	xwrite(STDOUT_FILENO, G.msg_ok, sizeof("NNN " MSG_OK) - 1);
	if (G.verbose > 1)
		verbose_log(G.msg_ok);
}
#define WRITE_OK(a) cmdio_write_ok(STRNUM32sp(a))

/* TODO: output strerr(errno) if errno != 0? */
static void
cmdio_write_error(unsigned status)
{
	*(bb__aliased_uint32_t *) G.msg_err = status;
	xwrite(STDOUT_FILENO, G.msg_err, sizeof("NNN " MSG_ERR) - 1);
	if (G.verbose > 0)
		verbose_log(G.msg_err);
}
#define WRITE_ERR(a) cmdio_write_error(STRNUM32sp(a))

static void
cmdio_write_raw(const char *p_text)
{
	xwrite_str(STDOUT_FILENO, p_text);
	if (G.verbose > 1)
		verbose_log(p_text);
}

static void
timeout_handler(int sig UNUSED_PARAM)
{
	off_t pos;
	int sv_errno = errno;

	if ((int)(monotonic_sec() - G.end_time) >= 0)
		goto timed_out;

	if (!G.local_file_fd)
		goto timed_out;

	pos = xlseek(G.local_file_fd, 0, SEEK_CUR);
	if (pos == G.local_file_pos)
		goto timed_out;
	G.local_file_pos = pos;

	alarm(G.timeout);
	errno = sv_errno;
	return;

 timed_out:
	cmdio_write_raw(STR(FTP_TIMEOUT)" Timeout\r\n");
/* TODO: do we need to abort (as opposed to usual shutdown) data transfer? */
	exit(1);
}

/* Simple commands */

static void
handle_pwd(void)
{
	char *cwd, *response;

	cwd = xrealloc_getcwd_or_warn(NULL);
	if (cwd == NULL)
		cwd = xstrdup("");

	/* We have to promote each " to "" */
	response = escape_text(" \"", cwd, ('"' << 8) + '"');
	free(cwd);
	cmdio_write(STRNUM32(FTP_PWDOK), response);
	free(response);
}

static void
handle_cwd(void)
{
	if (!G.ftp_arg || chdir(G.ftp_arg) != 0) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}
	WRITE_OK(FTP_CWDOK);
}

static void
handle_cdup(void)
{
	G.ftp_arg = (char*)"..";
	handle_cwd();
}

static void
handle_stat(void)
{
	cmdio_write_raw(STR(FTP_STATOK)"-Server status:\r\n"
			" TYPE: BINARY\r\n"
			STR(FTP_STATOK)" Ok\r\n");
}

/* Examples of HELP and FEAT:
# nc -vvv ftp.kernel.org 21
ftp.kernel.org (130.239.17.4:21) open
220 Welcome to ftp.kernel.org.
FEAT
211-Features:
 EPRT
 EPSV
 MDTM
 PASV
 REST STREAM
 SIZE
 TVFS
 UTF8
211 End
HELP
214-The following commands are recognized.
 ABOR ACCT ALLO APPE CDUP CWD  DELE EPRT EPSV FEAT HELP LIST MDTM MKD
 MODE NLST NOOP OPTS PASS PASV PORT PWD  QUIT REIN REST RETR RMD  RNFR
 RNTO SITE SIZE SMNT STAT STOR STOU STRU SYST TYPE USER XCUP XCWD XMKD
 XPWD XRMD
214 Help OK.
*/
static void
handle_feat(unsigned status)
{
	cmdio_write(status, "-Features:");
	cmdio_write_raw(" EPSV\r\n"
			" PASV\r\n"
			" REST STREAM\r\n"
			" MDTM\r\n"
			" SIZE\r\n");
	cmdio_write(status, " Ok");
}

/* Download commands */

static inline int
port_active(void)
{
	return (G.port_addr != NULL);
}

static inline int
pasv_active(void)
{
	return (G.pasv_listen_fd > STDOUT_FILENO);
}

static void
port_pasv_cleanup(void)
{
	free(G.port_addr);
	G.port_addr = NULL;
	if (G.pasv_listen_fd > STDOUT_FILENO)
		close(G.pasv_listen_fd);
	G.pasv_listen_fd = -1;
}

/* On error, emits error code to the peer */
static int
ftpdataio_get_pasv_fd(void)
{
	int remote_fd;

	remote_fd = accept(G.pasv_listen_fd, NULL, 0);

	if (remote_fd < 0) {
		WRITE_ERR(FTP_BADSENDCONN);
		return remote_fd;
	}

	setsockopt_keepalive(remote_fd);
	return remote_fd;
}

/* Clears port/pasv data.
 * This means we dont waste resources, for example, keeping
 * PASV listening socket open when it is no longer needed.
 * On error, emits error code to the peer (or exits).
 * On success, emits p_status_msg to the peer.
 */
static int
get_remote_transfer_fd(const char *p_status_msg)
{
	int remote_fd;

	if (pasv_active())
		/* On error, emits error code to the peer */
		remote_fd = ftpdataio_get_pasv_fd();
	else
		/* Exits on error */
		remote_fd = xconnect_stream(G.port_addr);

	port_pasv_cleanup();

	if (remote_fd < 0)
		return remote_fd;

	cmdio_write(STRNUM32(FTP_DATACONN), p_status_msg);
	return remote_fd;
}

/* If there were neither PASV nor PORT, emits error code to the peer */
static int
port_or_pasv_was_seen(void)
{
	if (!pasv_active() && !port_active()) {
		cmdio_write_raw(STR(FTP_BADSENDCONN)" Use PORT/PASV first\r\n");
		return 0;
	}

	return 1;
}

/* Exits on error */
static unsigned
bind_for_passive_mode(void)
{
	int fd;
	unsigned port;

	port_pasv_cleanup();

	G.pasv_listen_fd = fd = xsocket(G.local_addr->u.sa.sa_family, SOCK_STREAM, 0);
	setsockopt_reuseaddr(fd);

	set_nport(&G.local_addr->u.sa, 0);
	xbind(fd, &G.local_addr->u.sa, G.local_addr->len);
	xlisten(fd, 1);
	getsockname(fd, &G.local_addr->u.sa, &G.local_addr->len);

	port = get_nport(&G.local_addr->u.sa);
	port = ntohs(port);
	return port;
}

/* Exits on error */
static void
handle_pasv(void)
{
	unsigned port;
	char *addr, *response;

	port = bind_for_passive_mode();

	if (G.local_addr->u.sa.sa_family == AF_INET)
		addr = xmalloc_sockaddr2dotted_noport(&G.local_addr->u.sa);
	else /* seen this in the wild done by other ftp servers: */
		addr = xstrdup("0.0.0.0");
	replace_char(addr, '.', ',');

	response = xasprintf(STR(FTP_PASVOK)" PASV ok (%s,%u,%u)\r\n",
			addr, (int)(port >> 8), (int)(port & 255));
	free(addr);
	cmdio_write_raw(response);
	free(response);
}

/* Exits on error */
static void
handle_epsv(void)
{
	unsigned port;
	char *response;

	port = bind_for_passive_mode();
	response = xasprintf(STR(FTP_EPSVOK)" EPSV ok (|||%u|)\r\n", port);
	cmdio_write_raw(response);
	free(response);
}

static void
handle_port(void)
{
	unsigned port, port_hi;
	char *raw, *comma;
#ifdef WHY_BOTHER_WE_CAN_ASSUME_IP_MATCHES
	socklen_t peer_ipv4_len;
	struct sockaddr_in peer_ipv4;
	struct in_addr port_ipv4_sin_addr;
#endif

	port_pasv_cleanup();

	raw = G.ftp_arg;

	/* PORT command format makes sense only over IPv4 */
	if (!raw
#ifdef WHY_BOTHER_WE_CAN_ASSUME_IP_MATCHES
	 || G.local_addr->u.sa.sa_family != AF_INET
#endif
	) {
 bail:
		WRITE_ERR(FTP_BADCMD);
		return;
	}

	comma = strrchr(raw, ',');
	if (comma == NULL)
		goto bail;
	*comma = '\0';
	port = bb_strtou(&comma[1], NULL, 10);
	if (errno || port > 0xff)
		goto bail;

	comma = strrchr(raw, ',');
	if (comma == NULL)
		goto bail;
	*comma = '\0';
	port_hi = bb_strtou(&comma[1], NULL, 10);
	if (errno || port_hi > 0xff)
		goto bail;
	port |= port_hi << 8;

#ifdef WHY_BOTHER_WE_CAN_ASSUME_IP_MATCHES
	replace_char(raw, ',', '.');

	/* We are verifying that PORT's IP matches getpeername().
	 * Otherwise peer can make us open data connections
	 * to other hosts (security problem!)
	 * This code would be too simplistic:
	 * lsa = xdotted2sockaddr(raw, port);
	 * if (lsa == NULL) goto bail;
	 */
	if (!inet_aton(raw, &port_ipv4_sin_addr))
		goto bail;
	peer_ipv4_len = sizeof(peer_ipv4);
	if (getpeername(STDIN_FILENO, &peer_ipv4, &peer_ipv4_len) != 0)
		goto bail;
	if (memcmp(&port_ipv4_sin_addr, &peer_ipv4.sin_addr, sizeof(struct in_addr)) != 0)
		goto bail;

	G.port_addr = xdotted2sockaddr(raw, port);
#else
	G.port_addr = get_peer_lsa(STDIN_FILENO);
	set_nport(&G.port_addr->u.sa, htons(port));
#endif
	WRITE_OK(FTP_PORTOK);
}

static void
handle_rest(void)
{
	/* When ftp_arg == NULL simply restart from beginning */
	G.restart_pos = G.ftp_arg ? XATOOFF(G.ftp_arg) : 0;
	WRITE_OK(FTP_RESTOK);
}

static void
handle_retr(void)
{
	struct stat statbuf;
	off_t bytes_transferred;
	int remote_fd;
	int local_file_fd;
	off_t offset = G.restart_pos;
	char *response;

	G.restart_pos = 0;

	if (!port_or_pasv_was_seen())
		return; /* port_or_pasv_was_seen emitted error response */

	/* O_NONBLOCK is useful if file happens to be a device node */
	local_file_fd = G.ftp_arg ? open(G.ftp_arg, O_RDONLY | O_NONBLOCK) : -1;
	if (local_file_fd < 0) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}

	if (fstat(local_file_fd, &statbuf) != 0 || !S_ISREG(statbuf.st_mode)) {
		/* Note - pretend open failed */
		WRITE_ERR(FTP_FILEFAIL);
		goto file_close_out;
	}
	G.local_file_fd = local_file_fd;

	/* Now deactive O_NONBLOCK, otherwise we have a problem
	 * on DMAPI filesystems such as XFS DMAPI.
	 */
	ndelay_off(local_file_fd);

	/* Set the download offset (from REST) if any */
	if (offset != 0)
		xlseek(local_file_fd, offset, SEEK_SET);

	response = xasprintf(
		" Opening BINARY connection for %s (%"OFF_FMT"u bytes)",
		G.ftp_arg, statbuf.st_size);
	remote_fd = get_remote_transfer_fd(response);
	free(response);
	if (remote_fd < 0)
		goto file_close_out;

	bytes_transferred = bb_copyfd_eof(local_file_fd, remote_fd);
	close(remote_fd);
	if (bytes_transferred < 0)
		WRITE_ERR(FTP_BADSENDFILE);
	else
		WRITE_OK(FTP_TRANSFEROK);

 file_close_out:
	close(local_file_fd);
	G.local_file_fd = 0;
}

/* List commands */

static int
popen_ls(const char *opt)
{
	const char *argv[5];
	struct fd_pair outfd;
	pid_t pid;

	argv[0] = "ftpd";
	argv[1] = opt; /* "-lA" or "-1A" */
	argv[2] = "--";
	argv[3] = G.ftp_arg;
	argv[4] = NULL;

	/* Improve compatibility with non-RFC conforming FTP clients
	 * which send e.g. "LIST -l", "LIST -la", "LIST -aL".
	 * See https://bugs.kde.org/show_bug.cgi?id=195578 */
	if (ENABLE_FEATURE_FTPD_ACCEPT_BROKEN_LIST
	 && G.ftp_arg && G.ftp_arg[0] == '-'
	) {
		const char *tmp = strchr(G.ftp_arg, ' ');
		if (tmp) /* skip the space */
			tmp++;
		argv[3] = tmp;
	}

	xpiped_pair(outfd);

	/*fflush_all(); - so far we dont use stdio on output */
	pid = BB_MMU ? xfork() : xvfork();
	if (pid == 0) {
#if !BB_MMU
		int cur_fd;
#endif
		/* child */
		/* NB: close _first_, then move fd! */
		close(outfd.rd);
		xmove_fd(outfd.wr, STDOUT_FILENO);
		/* Opening /dev/null in chroot is hard.
		 * Just making sure STDIN_FILENO is opened
		 * to something harmless. Paranoia,
		 * ls won't read it anyway */
		close(STDIN_FILENO);
		dup(STDOUT_FILENO); /* copy will become STDIN_FILENO */
#if BB_MMU
		/* memset(&G, 0, sizeof(G)); - ls_main does it */
		exit(ls_main(/*argc_unused*/ 0, (char**) argv));
#else
		cur_fd = xopen(".", O_RDONLY | O_DIRECTORY);
		/* On NOMMU, we want to execute a child - copy of ourself
		 * in order to unblock parent after vfork.
		 * In chroot we usually can't re-exec. Thus we escape
		 * out of the chroot back to original root.
		 */
		if (G.root_fd >= 0) {
			if (fchdir(G.root_fd) != 0 || chroot(".") != 0)
				_exit(127);
			/*close(G.root_fd); - close_on_exec_on() took care of this */
		}
		/* Child expects directory to list on fd #3 */
		xmove_fd(cur_fd, 3);
		execv(bb_busybox_exec_path, (char**) argv);
		_exit(127);
#endif
	}

	/* parent */
	close(outfd.wr);
	return outfd.rd;
}

enum {
	USE_CTRL_CONN = 1,
	LONG_LISTING = 2,
};

static void
handle_dir_common(int opts)
{
	FILE *ls_fp;
	char *line;
	int ls_fd;

	if (!(opts & USE_CTRL_CONN) && !port_or_pasv_was_seen())
		return; /* port_or_pasv_was_seen emitted error response */

	ls_fd = popen_ls((opts & LONG_LISTING) ? "-lA" : "-1A");
	ls_fp = xfdopen_for_read(ls_fd);
/* FIXME: filenames with embedded newlines are mishandled */

	if (opts & USE_CTRL_CONN) {
		/* STAT <filename> */
		cmdio_write_raw(STR(FTP_STATFILE_OK)"-File status:\r\n");
		while (1) {
			line = xmalloc_fgetline(ls_fp);
			if (!line)
				break;
			/* Hack: 0 results in no status at all */
			/* Note: it's ok that we don't prepend space,
			 * ftp.kernel.org doesn't do that too */
			cmdio_write(0, line);
			free(line);
		}
		WRITE_OK(FTP_STATFILE_OK);
	} else {
		/* LIST/NLST [<filename>] */
		int remote_fd = get_remote_transfer_fd(" Directory listing");
		if (remote_fd >= 0) {
			while (1) {
				unsigned len;

				line = xmalloc_fgets(ls_fp);
				if (!line)
					break;
				/* I've seen clients complaining when they
				 * are fed with ls output with bare '\n'.
				 * Replace trailing "\n\0" with "\r\n".
				 */
				len = strlen(line);
				if (len != 0) /* paranoia check */
					line[len - 1] = '\r';
				line[len] = '\n';
				xwrite(remote_fd, line, len + 1);
				free(line);
			}
		}
		close(remote_fd);
		WRITE_OK(FTP_TRANSFEROK);
	}
	fclose(ls_fp); /* closes ls_fd too */
}
static void
handle_list(void)
{
	handle_dir_common(LONG_LISTING);
}
static void
handle_nlst(void)
{
	/* NLST returns list of names, "\r\n" terminated without regard
	 * to the current binary flag. Names may start with "/",
	 * then they represent full names (we don't produce such names),
	 * otherwise names are relative to current directory.
	 * Embedded "\n" are replaced by NULs. This is safe since names
	 * can never contain NUL.
	 */
	handle_dir_common(0);
}
static void
handle_stat_file(void)
{
	handle_dir_common(LONG_LISTING + USE_CTRL_CONN);
}

/* This can be extended to handle MLST, as all info is available
 * in struct stat for that:
 * MLST file_name
 * 250-Listing file_name
 *  type=file;size=4161;modify=19970214165800; /dir/dir/file_name
 * 250 End
 * Nano-doc:
 * MLST [<file or dir name, "." assumed if not given>]
 * Returned name should be either the same as requested, or fully qualified.
 * If there was no parameter, return "" or (preferred) fully-qualified name.
 * Returned "facts" (case is not important):
 *  size    - size in octets
 *  modify  - last modification time
 *  type    - entry type (file,dir,OS.unix=block)
 *            (+ cdir and pdir types for MLSD)
 *  unique  - unique id of file/directory (inode#)
 *  perm    -
 *      a: can be appended to (APPE)
 *      d: can be deleted (RMD/DELE)
 *      f: can be renamed (RNFR)
 *      r: can be read (RETR)
 *      w: can be written (STOR)
 *      e: can CWD into this dir
 *      l: this dir can be listed (dir only!)
 *      c: can create files in this dir
 *      m: can create dirs in this dir (MKD)
 *      p: can delete files in this dir
 *  UNIX.mode - unix file mode
 */
static void
handle_size_or_mdtm(int need_size)
{
	struct stat statbuf;
	struct tm broken_out;
	char buf[(sizeof("NNN %"OFF_FMT"u\r\n") + sizeof(off_t) * 3)
		| sizeof("NNN YYYYMMDDhhmmss\r\n")
	];

	if (!G.ftp_arg
	 || stat(G.ftp_arg, &statbuf) != 0
	 || !S_ISREG(statbuf.st_mode)
	) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}
	if (need_size) {
		sprintf(buf, STR(FTP_STATFILE_OK)" %"OFF_FMT"u\r\n", statbuf.st_size);
	} else {
		gmtime_r(&statbuf.st_mtime, &broken_out);
		sprintf(buf, STR(FTP_STATFILE_OK)" %04u%02u%02u%02u%02u%02u\r\n",
			broken_out.tm_year + 1900,
			broken_out.tm_mon + 1,
			broken_out.tm_mday,
			broken_out.tm_hour,
			broken_out.tm_min,
			broken_out.tm_sec);
	}
	cmdio_write_raw(buf);
}

/* Upload commands */

#if ENABLE_FEATURE_FTPD_WRITE
static void
handle_mkd(void)
{
	if (!G.ftp_arg || mkdir(G.ftp_arg, 0777) != 0) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}
	WRITE_OK(FTP_MKDIROK);
}

static void
handle_rmd(void)
{
	if (!G.ftp_arg || rmdir(G.ftp_arg) != 0) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}
	WRITE_OK(FTP_RMDIROK);
}

static void
handle_dele(void)
{
	if (!G.ftp_arg || unlink(G.ftp_arg) != 0) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}
	WRITE_OK(FTP_DELEOK);
}

static void
handle_rnfr(void)
{
	free(G.rnfr_filename);
	G.rnfr_filename = xstrdup(G.ftp_arg);
	WRITE_OK(FTP_RNFROK);
}

static void
handle_rnto(void)
{
	int retval;

	/* If we didn't get a RNFR, throw a wobbly */
	if (G.rnfr_filename == NULL || G.ftp_arg == NULL) {
		cmdio_write_raw(STR(FTP_NEEDRNFR)" Use RNFR first\r\n");
		return;
	}

	retval = rename(G.rnfr_filename, G.ftp_arg);
	free(G.rnfr_filename);
	G.rnfr_filename = NULL;

	if (retval) {
		WRITE_ERR(FTP_FILEFAIL);
		return;
	}
	WRITE_OK(FTP_RENAMEOK);
}

static void
handle_upload_common(int is_append, int is_unique)
{
	struct stat statbuf;
	char *tempname;
	off_t bytes_transferred;
	off_t offset;
	int local_file_fd;
	int remote_fd;

	offset = G.restart_pos;
	G.restart_pos = 0;

	if (!port_or_pasv_was_seen())
		return; /* port_or_pasv_was_seen emitted error response */

	tempname = NULL;
	local_file_fd = -1;
	if (is_unique) {
		tempname = xstrdup(" FILE: uniq.XXXXXX");
		local_file_fd = mkstemp(tempname + 7);
	} else if (G.ftp_arg) {
		int flags = O_WRONLY | O_CREAT | O_TRUNC;
		if (is_append)
			flags = O_WRONLY | O_CREAT | O_APPEND;
		if (offset)
			flags = O_WRONLY | O_CREAT;
		local_file_fd = open(G.ftp_arg, flags, 0666);
	}

	if (local_file_fd < 0
	 || fstat(local_file_fd, &statbuf) != 0
	 || !S_ISREG(statbuf.st_mode)
	) {
		free(tempname);
		WRITE_ERR(FTP_UPLOADFAIL);
		if (local_file_fd >= 0)
			goto close_local_and_bail;
		return;
	}
	G.local_file_fd = local_file_fd;

	if (offset)
		xlseek(local_file_fd, offset, SEEK_SET);

	remote_fd = get_remote_transfer_fd(tempname ? tempname : " Ok to send data");
	free(tempname);

	if (remote_fd < 0)
		goto close_local_and_bail;

	bytes_transferred = bb_copyfd_eof(remote_fd, local_file_fd);
	close(remote_fd);
	if (bytes_transferred < 0)
		WRITE_ERR(FTP_BADSENDFILE);
	else
		WRITE_OK(FTP_TRANSFEROK);

 close_local_and_bail:
	close(local_file_fd);
	G.local_file_fd = 0;
}

static void
handle_stor(void)
{
	handle_upload_common(0, 0);
}

static void
handle_appe(void)
{
	G.restart_pos = 0;
	handle_upload_common(1, 0);
}

static void
handle_stou(void)
{
	G.restart_pos = 0;
	handle_upload_common(0, 1);
}
#endif /* ENABLE_FEATURE_FTPD_WRITE */

static uint32_t
cmdio_get_cmd_and_arg(void)
{
	int len;
	uint32_t cmdval;
	char *cmd;

	alarm(G.timeout);

	free(G.ftp_cmd);
	{
		/* Paranoia. Peer may send 1 gigabyte long cmd... */
		/* Using separate len_on_stk instead of len optimizes
		 * code size (allows len to be in CPU register) */
		size_t len_on_stk = 8 * 1024;
		G.ftp_cmd = cmd = xmalloc_fgets_str_len(stdin, "\r\n", &len_on_stk);
		if (!cmd)
			exit(0);
		len = len_on_stk;
	}

	/* De-escape telnet: 0xff,0xff => 0xff */
	/* RFC959 says that ABOR, STAT, QUIT may be sent even during
	 * data transfer, and may be preceded by telnet's "Interrupt Process"
	 * code (two-byte sequence 255,244) and then by telnet "Synch" code
	 * 255,242 (byte 242 is sent with TCP URG bit using send(MSG_OOB)
	 * and may generate SIGURG on our side. See RFC854).
	 * So far we don't support that (may install SIGURG handler if we'd want to),
	 * but we need to at least remove 255,xxx pairs. lftp sends those. */
	/* Then de-escape FTP: NUL => '\n' */
	/* Testing for \xff:
	 * Create file named '\xff': echo Hello >`echo -ne "\xff"`
	 * Try to get it:            ftpget -v 127.0.0.1 Eff `echo -ne "\xff\xff"`
	 * (need "\xff\xff" until ftpget applet is fixed to do escaping :)
	 * Testing for embedded LF:
	 * LF_HERE=`echo -ne "LF\nHERE"`
	 * echo Hello >"$LF_HERE"
	 * ftpget -v 127.0.0.1 LF_HERE "$LF_HERE"
	 */
	{
		int dst, src;

		/* Strip "\r\n" if it is there */
		if (len != 0 && cmd[len - 1] == '\n') {
			len--;
			if (len != 0 && cmd[len - 1] == '\r')
				len--;
			cmd[len] = '\0';
		}
		src = strchrnul(cmd, 0xff) - cmd;
		/* 99,99% there are neither NULs nor 255s and src == len */
		if (src < len) {
			dst = src;
			do {
				if ((unsigned char)(cmd[src]) == 255) {
					src++;
					/* 255,xxx - skip 255 */
					if ((unsigned char)(cmd[src]) != 255) {
						/* 255,!255 - skip both */
						src++;
						continue;
					}
					/* 255,255 - retain one 255 */
				}
				/* NUL => '\n' */
				cmd[dst++] = cmd[src] ? cmd[src] : '\n';
				src++;
			} while (src < len);
			cmd[dst] = '\0';
		}
	}

	if (G.verbose > 1)
		verbose_log(cmd);

	G.ftp_arg = strchr(cmd, ' ');
	if (G.ftp_arg != NULL)
		*G.ftp_arg++ = '\0';

	/* Uppercase and pack into uint32_t first word of the command */
	cmdval = 0;
	while (*cmd)
		cmdval = (cmdval << 8) + ((unsigned char)*cmd++ & (unsigned char)~0x20);

	return cmdval;
}

#define mk_const4(a,b,c,d) (((a * 0x100 + b) * 0x100 + c) * 0x100 + d)
#define mk_const3(a,b,c)    ((a * 0x100 + b) * 0x100 + c)
enum {
	const_ALLO = mk_const4('A', 'L', 'L', 'O'),
	const_APPE = mk_const4('A', 'P', 'P', 'E'),
	const_CDUP = mk_const4('C', 'D', 'U', 'P'),
	const_CWD  = mk_const3('C', 'W', 'D'),
	const_DELE = mk_const4('D', 'E', 'L', 'E'),
	const_EPSV = mk_const4('E', 'P', 'S', 'V'),
	const_FEAT = mk_const4('F', 'E', 'A', 'T'),
	const_HELP = mk_const4('H', 'E', 'L', 'P'),
	const_LIST = mk_const4('L', 'I', 'S', 'T'),
	const_MDTM = mk_const4('M', 'D', 'T', 'M'),
	const_MKD  = mk_const3('M', 'K', 'D'),
	const_MODE = mk_const4('M', 'O', 'D', 'E'),
	const_NLST = mk_const4('N', 'L', 'S', 'T'),
	const_NOOP = mk_const4('N', 'O', 'O', 'P'),
	const_PASS = mk_const4('P', 'A', 'S', 'S'),
	const_PASV = mk_const4('P', 'A', 'S', 'V'),
	const_PORT = mk_const4('P', 'O', 'R', 'T'),
	const_PWD  = mk_const3('P', 'W', 'D'),
	/* Same as PWD. Reportedly used by windows ftp client */
	const_XPWD = mk_const4('X', 'P', 'W', 'D'),
	const_QUIT = mk_const4('Q', 'U', 'I', 'T'),
	const_REST = mk_const4('R', 'E', 'S', 'T'),
	const_RETR = mk_const4('R', 'E', 'T', 'R'),
	const_RMD  = mk_const3('R', 'M', 'D'),
	const_RNFR = mk_const4('R', 'N', 'F', 'R'),
	const_RNTO = mk_const4('R', 'N', 'T', 'O'),
	const_SIZE = mk_const4('S', 'I', 'Z', 'E'),
	const_STAT = mk_const4('S', 'T', 'A', 'T'),
	const_STOR = mk_const4('S', 'T', 'O', 'R'),
	const_STOU = mk_const4('S', 'T', 'O', 'U'),
	const_STRU = mk_const4('S', 'T', 'R', 'U'),
	const_SYST = mk_const4('S', 'Y', 'S', 'T'),
	const_TYPE = mk_const4('T', 'Y', 'P', 'E'),
	const_USER = mk_const4('U', 'S', 'E', 'R'),

#if !BB_MMU
	OPT_l = (1 << 0),
	OPT_1 = (1 << 1),
#endif
	BIT_A =        (!BB_MMU) * 2,
	OPT_A = (1 << (BIT_A + 0)),
	OPT_v = (1 << (BIT_A + 1)),
	OPT_S = (1 << (BIT_A + 2)),
	OPT_w = (1 << (BIT_A + 3)) * ENABLE_FEATURE_FTPD_WRITE,
};

int ftpd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ftpd_main(int argc UNUSED_PARAM, char **argv)
{
#if ENABLE_FEATURE_FTPD_AUTHENTICATION
	struct passwd *pw = NULL;
	char *anon_opt = NULL;
#endif
	unsigned abs_timeout;
	unsigned verbose_S;
	smallint opts;

	INIT_G();

	abs_timeout = 1 * 60 * 60;
	verbose_S = 0;
	G.timeout = 2 * 60;
#if BB_MMU
	opts = getopt32(argv, "^"   "AvS" IF_FEATURE_FTPD_WRITE("w")
		"t:+T:+" IF_FEATURE_FTPD_AUTHENTICATION("a:")
		"\0" "vv:SS",
		&G.timeout, &abs_timeout, IF_FEATURE_FTPD_AUTHENTICATION(&anon_opt,)
		&G.verbose, &verbose_S
	);
#else
	opts = getopt32(argv, "^" "l1AvS" IF_FEATURE_FTPD_WRITE("w")
		"t:+T:+" IF_FEATURE_FTPD_AUTHENTICATION("a:")
		"\0" "vv:SS",
		&G.timeout, &abs_timeout, IF_FEATURE_FTPD_AUTHENTICATION(&anon_opt,)
		&G.verbose, &verbose_S
	);
	if (opts & (OPT_l|OPT_1)) {
		/* Our secret backdoor to ls: see popen_ls() */
		if (fchdir(3) != 0)
			_exit(127);
		/* memset(&G, 0, sizeof(G)); - ls_main does it */
		/* NB: in this case -A has a different meaning: like "ls -A" */
		return ls_main(/*argc_unused*/ 0, argv);
	}
#endif
	if (G.verbose < verbose_S)
		G.verbose = verbose_S;
	if (abs_timeout | G.timeout) {
		if (abs_timeout == 0)
			abs_timeout = INT_MAX;
		G.end_time = monotonic_sec() + abs_timeout;
		if (G.timeout > abs_timeout)
			G.timeout = abs_timeout;
	}
	strcpy(G.msg_ok  + 4, MSG_OK );
	strcpy(G.msg_err + 4, MSG_ERR);

	G.local_addr = get_sock_lsa(STDIN_FILENO);
	if (!G.local_addr) {
		/* This is confusing:
		 * bb_error_msg_and_die("stdin is not a socket");
		 * Better: */
		bb_show_usage();
		/* Help text says that ftpd must be used as inetd service,
		 * which is by far the most usual cause of get_sock_lsa
		 * failure */
	}

	if (!(opts & OPT_v))
		logmode = LOGMODE_NONE;
	if (opts & OPT_S) {
		/* LOG_NDELAY is needed since we may chroot later */
		openlog(applet_name, LOG_PID | LOG_NDELAY, LOG_DAEMON);
		logmode |= LOGMODE_SYSLOG;
	}
	if (logmode)
		applet_name = xasprintf("%s[%u]", applet_name, (int)getpid());

	//umask(077); - admin can set umask before starting us

	/* Signals */
	bb_signals(0
		/* We'll always take EPIPE rather than a rude signal, thanks */
		+ (1 << SIGPIPE)
		/* LIST command spawns chilren. Prevent zombies */
		+ (1 << SIGCHLD)
		, SIG_IGN);

	/* Set up options on the command socket (do we need these all? why?) */
	setsockopt_1(STDIN_FILENO, IPPROTO_TCP, TCP_NODELAY);
	setsockopt_keepalive(STDIN_FILENO);
	/* Telnet protocol over command link may send "urgent" data,
	 * we prefer it to be received in the "normal" data stream: */
	setsockopt_1(STDIN_FILENO, SOL_SOCKET, SO_OOBINLINE);

	WRITE_OK(FTP_GREET);
	signal(SIGALRM, timeout_handler);

#if ENABLE_FEATURE_FTPD_AUTHENTICATION
	if (!(opts & OPT_A)) {
		while (1) {
			uint32_t cmdval = cmdio_get_cmd_and_arg();
			if (cmdval == const_USER) {
				if (anon_opt && strcmp(G.ftp_arg, "anonymous") == 0) {
					pw = getpwnam(anon_opt);
					if (pw)
						break; /* does not even ask for password */
				}
				pw = getpwnam(G.ftp_arg);
				cmdio_write_raw(STR(FTP_GIVEPWORD)" Specify password\r\n");
			} else if (cmdval == const_PASS) {
				if (check_password(pw, G.ftp_arg) > 0) {
					break;	/* login success */
				}
				cmdio_write_raw(STR(FTP_LOGINERR)" Login failed\r\n");
				pw = NULL;
			} else if (cmdval == const_QUIT) {
				WRITE_OK(FTP_GOODBYE);
				return 0;
			} else {
				cmdio_write_raw(STR(FTP_LOGINERR)" Login with USER+PASS\r\n");
			}
		}
		WRITE_OK(FTP_LOGINOK);
	}
#endif

	/* Do this after auth, else /etc/passwd is not accessible */
#if !BB_MMU
	G.root_fd = -1;
#endif
	argv += optind;
	if (argv[0]) {
		const char *basedir = argv[0];
#if !BB_MMU
		G.root_fd = xopen("/", O_RDONLY | O_DIRECTORY);
		close_on_exec_on(G.root_fd);
#endif
		if (chroot(basedir) == 0)
			basedir = "/";
#if !BB_MMU
		else {
			close(G.root_fd);
			G.root_fd = -1;
		}
#endif
		/*
		 * If chroot failed, assume that we aren't root,
		 * and at least chdir to the specified DIR
		 * (older versions were dying with error message).
		 * If chroot worked, move current dir to new "/":
		 */
		xchdir(basedir);
	}

#if ENABLE_FEATURE_FTPD_AUTHENTICATION
	if (pw)
		change_identity(pw);
	/* else: -A is in effect */
#endif

	/* RFC-959 Section 5.1
	 * The following commands and options MUST be supported by every
	 * server-FTP and user-FTP, except in cases where the underlying
	 * file system or operating system does not allow or support
	 * a particular command.
	 * Type: ASCII Non-print, IMAGE, LOCAL 8
	 * Mode: Stream
	 * Structure: File, Record*
	 * (Record structure is REQUIRED only for hosts whose file
	 *  systems support record structure).
	 * Commands:
	 * USER, PASS, ACCT, [bbox: ACCT not supported]
	 * PORT, PASV,
	 * TYPE, MODE, STRU,
	 * RETR, STOR, APPE,
	 * RNFR, RNTO, DELE,
	 * CWD,  CDUP, RMD,  MKD,  PWD,
	 * LIST, NLST,
	 * SYST, STAT,
	 * HELP, NOOP, QUIT.
	 */
	/* ACCOUNT (ACCT)
	 * "The argument field is a Telnet string identifying the user's account.
	 * The command is not necessarily related to the USER command, as some
	 * sites may require an account for login and others only for specific
	 * access, such as storing files. In the latter case the command may
	 * arrive at any time.
	 * There are reply codes to differentiate these cases for the automation:
	 * when account information is required for login, the response to
	 * a successful PASSword command is reply code 332. On the other hand,
	 * if account information is NOT required for login, the reply to
	 * a successful PASSword command is 230; and if the account information
	 * is needed for a command issued later in the dialogue, the server
	 * should return a 332 or 532 reply depending on whether it stores
	 * (pending receipt of the ACCounT command) or discards the command,
	 * respectively."
	 */

	while (1) {
		uint32_t cmdval = cmdio_get_cmd_and_arg();

		if (cmdval == const_QUIT) {
			WRITE_OK(FTP_GOODBYE);
			return 0;
		}
		else if (cmdval == const_USER)
			/* This would mean "ok, now give me PASS". */
			/*WRITE_OK(FTP_GIVEPWORD);*/
			/* vsftpd can be configured to not require that,
			 * and this also saves one roundtrip:
			 */
			WRITE_OK(FTP_LOGINOK);
		else if (cmdval == const_PASS)
			WRITE_OK(FTP_LOGINOK);
		else if (cmdval == const_NOOP)
			WRITE_OK(FTP_NOOPOK);
		else if (cmdval == const_TYPE)
			WRITE_OK(FTP_TYPEOK);
		else if (cmdval == const_STRU)
			WRITE_OK(FTP_STRUOK);
		else if (cmdval == const_MODE)
			WRITE_OK(FTP_MODEOK);
		else if (cmdval == const_ALLO)
			WRITE_OK(FTP_ALLOOK);
		else if (cmdval == const_SYST)
			cmdio_write_raw(STR(FTP_SYSTOK)" UNIX Type: L8\r\n");
		else if (cmdval == const_PWD || cmdval == const_XPWD)
			handle_pwd();
		else if (cmdval == const_CWD)
			handle_cwd();
		else if (cmdval == const_CDUP) /* cd .. */
			handle_cdup();
		/* HELP is nearly useless, but we can reuse FEAT for it */
		/* lftp uses FEAT */
		else if (cmdval == const_HELP || cmdval == const_FEAT)
			handle_feat(cmdval == const_HELP
					? STRNUM32(FTP_HELP)
					: STRNUM32(FTP_STATOK)
			);
		else if (cmdval == const_LIST) /* ls -l */
			handle_list();
		else if (cmdval == const_NLST) /* "name list", bare ls */
			handle_nlst();
		/* SIZE is crucial for wget's download indicator etc */
		/* Mozilla, lftp use MDTM (presumably for caching) */
		else if (cmdval == const_SIZE || cmdval == const_MDTM)
			handle_size_or_mdtm(cmdval == const_SIZE);
		else if (cmdval == const_STAT) {
			if (G.ftp_arg == NULL)
				handle_stat();
			else
				handle_stat_file();
		}
		else if (cmdval == const_PASV)
			handle_pasv();
		else if (cmdval == const_EPSV)
			handle_epsv();
		else if (cmdval == const_RETR)
			handle_retr();
		else if (cmdval == const_PORT)
			handle_port();
		else if (cmdval == const_REST)
			handle_rest();
#if ENABLE_FEATURE_FTPD_WRITE
		else if (opts & OPT_w) {
			if (cmdval == const_STOR)
				handle_stor();
			else if (cmdval == const_MKD)
				handle_mkd();
			else if (cmdval == const_RMD)
				handle_rmd();
			else if (cmdval == const_DELE)
				handle_dele();
			else if (cmdval == const_RNFR) /* "rename from" */
				handle_rnfr();
			else if (cmdval == const_RNTO) /* "rename to" */
				handle_rnto();
			else if (cmdval == const_APPE)
				handle_appe();
			else if (cmdval == const_STOU) /* "store unique" */
				handle_stou();
			else
				goto bad_cmd;
		}
#endif
#if 0
		else if (cmdval == const_STOR
		 || cmdval == const_MKD
		 || cmdval == const_RMD
		 || cmdval == const_DELE
		 || cmdval == const_RNFR
		 || cmdval == const_RNTO
		 || cmdval == const_APPE
		 || cmdval == const_STOU
		) {
			cmdio_write_raw(STR(FTP_NOPERM)" Permission denied\r\n");
		}
#endif
		else {
			/* Which unsupported commands were seen in the wild?
			 * (doesn't necessarily mean "we must support them")
			 * foo 1.2.3: XXXX - comment
			 */
#if ENABLE_FEATURE_FTPD_WRITE
 bad_cmd:
#endif
			cmdio_write_raw(STR(FTP_BADCMD)" Unknown command\r\n");
		}
	}
}
