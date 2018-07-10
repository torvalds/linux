/* vi: set sw=4 ts=4: */
/*
 * helper routines
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <sys/prctl.h>
#include "libbb.h"
#include "mail.h"

// generic signal handler
static void signal_handler(int signo)
{
#define err signo
	if (SIGALRM == signo) {
		bb_error_msg_and_die("timed out");
	}

	// SIGCHLD. reap zombies
	if (safe_waitpid(G.helper_pid, &err, WNOHANG) > 0) {
		if (WIFSIGNALED(err))
			bb_error_msg_and_die("helper killed by signal %u", WTERMSIG(err));
		if (WIFEXITED(err)) {
			G.helper_pid = 0;
			if (WEXITSTATUS(err))
				bb_error_msg_and_die("helper exited (%u)", WEXITSTATUS(err));
		}
	}
#undef err
}

void FAST_FUNC launch_helper(const char **argv)
{
	// setup vanilla unidirectional pipes interchange
	int i;
	int pipes[4];

	xpipe(pipes);
	xpipe(pipes + 2);

	// NB: handler must be installed before vfork
	bb_signals(0
		+ (1 << SIGCHLD)
		+ (1 << SIGALRM)
		, signal_handler);

	G.helper_pid = xvfork();

	i = (!G.helper_pid) * 2; // for parent:0, for child:2
	close(pipes[i + 1]);     // 1 or 3 - closing one write end
	close(pipes[2 - i]);     // 2 or 0 - closing one read end
	xmove_fd(pipes[i], STDIN_FILENO);      // 0 or 2 - using other read end
	xmove_fd(pipes[3 - i], STDOUT_FILENO); // 3 or 1 - using other write end
	// End result:
	// parent stdout [3] -> child stdin [2]
	// child stdout [1] -> parent stdin [0]

	if (!G.helper_pid) {
		// child
		// if parent dies, get SIGTERM
		prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
		// try to execute connection helper
		// NB: SIGCHLD & SIGALRM revert to SIG_DFL on exec
		BB_EXECVP_or_die((char**)argv);
	}

	// parent goes on
}

char* FAST_FUNC send_mail_command(const char *fmt, const char *param)
{
	char *msg;
	if (timeout)
		alarm(timeout);
	msg = (char*)fmt;
	if (fmt) {
		msg = xasprintf(fmt, param);
		if (verbose)
			bb_error_msg("send:'%s'", msg);
		printf("%s\r\n", msg);
	}
	fflush_all();
	return msg;
}

// NB: parse_url can modify url[] (despite const), but only if '@' is there
/*
static char* FAST_FUNC parse_url(char *url, char **user, char **pass)
{
	// parse [user[:pass]@]host
	// return host
	char *s = strchr(url, '@');
	*user = *pass = NULL;
	if (s) {
		*s++ = '\0';
		*user = url;
		url = s;
		s = strchr(*user, ':');
		if (s) {
			*s++ = '\0';
			*pass = s;
		}
	}
	return url;
}
*/

void FAST_FUNC encode_base64(char *fname, const char *text, const char *eol)
{
	enum {
		SRC_BUF_SIZE = 57,  /* This *MUST* be a multiple of 3 */
		DST_BUF_SIZE = 4 * ((SRC_BUF_SIZE + 2) / 3),
	};
#define src_buf text
	char src[SRC_BUF_SIZE];
	FILE *fp = fp;
	ssize_t len = len;
	char dst_buf[DST_BUF_SIZE + 1];

	if (fname) {
		fp = (NOT_LONE_DASH(fname)) ? xfopen_for_read(fname) : (FILE *)text;
		src_buf = src;
	} else if (text) {
		// though we do not call uuencode(NULL, NULL) explicitly
		// still we do not want to break things suddenly
		len = strlen(text);
	} else
		return;

	while (1) {
		size_t size;
		if (fname) {
			size = fread((char *)src_buf, 1, SRC_BUF_SIZE, fp);
			if ((ssize_t)size < 0)
				bb_perror_msg_and_die(bb_msg_read_error);
		} else {
			size = len;
			if (len > SRC_BUF_SIZE)
				size = SRC_BUF_SIZE;
		}
		if (!size)
			break;
		// encode the buffer we just read in
		bb_uuencode(dst_buf, src_buf, size, bb_uuenc_tbl_base64);
		if (fname) {
			puts(eol);
		} else {
			src_buf += size;
			len -= size;
		}
		fwrite(dst_buf, 1, 4 * ((size + 2) / 3), stdout);
	}
	if (fname && NOT_LONE_DASH(fname))
		fclose(fp);
#undef src_buf
}

/*
 * get username and password from a file descriptor
 */
void FAST_FUNC get_cred_or_die(int fd)
{
	if (isatty(fd)) {
		G.user = bb_ask_noecho(fd, /* timeout: */ 0, "User: ");
		G.pass = bb_ask_noecho(fd, /* timeout: */ 0, "Password: ");
	} else {
		G.user = xmalloc_reads(fd, /* maxsize: */ NULL);
		G.pass = xmalloc_reads(fd, /* maxsize: */ NULL);
	}
	if (!G.user || !*G.user || !G.pass)
		bb_error_msg_and_die("no username or password");
}
