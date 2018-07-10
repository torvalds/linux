/* vi: set sw=4 ts=4: */
/*
 * popmaildir: a simple yet powerful POP3 client
 * Delivers contents of remote mailboxes to local Maildir
 *
 * Inspired by original utility by Nikola Vladov
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config POPMAILDIR
//config:	bool "popmaildir (10 kb)"
//config:	default y
//config:	help
//config:	Simple yet powerful POP3 mail popper. Delivers content
//config:	of remote mailboxes to local Maildir.
//config:
//config:config FEATURE_POPMAILDIR_DELIVERY
//config:	bool "Allow message filters and custom delivery program"
//config:	default y
//config:	depends on POPMAILDIR
//config:	help
//config:	Allow to use a custom program to filter the content
//config:	of the message before actual delivery (-F "prog [args...]").
//config:	Allow to use a custom program for message actual delivery
//config:	(-M "prog [args...]").

//applet:IF_POPMAILDIR(APPLET(popmaildir, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_POPMAILDIR) += popmaildir.o mail.o

//usage:#define popmaildir_trivial_usage
//usage:       "[OPTIONS] MAILDIR [CONN_HELPER ARGS]"
//usage:#define popmaildir_full_usage "\n\n"
//usage:       "Fetch content of remote mailbox to local maildir\n"
/* //usage:  "\n	-b		Binary mode. Ignored" */
/* //usage:  "\n	-d		Debug. Ignored" */
/* //usage:  "\n	-m		Show used memory. Ignored" */
/* //usage:  "\n	-V		Show version. Ignored" */
/* //usage:  "\n	-c		Use tcpclient. Ignored" */
/* //usage:  "\n	-a		Use APOP protocol. Implied. If server supports APOP -> use it" */
//usage:     "\n	-s		Skip authorization"
//usage:     "\n	-T		Get messages with TOP instead of RETR"
//usage:     "\n	-k		Keep retrieved messages on the server"
//usage:     "\n	-t SEC		Network timeout"
//usage:	IF_FEATURE_POPMAILDIR_DELIVERY(
//usage:     "\n	-F 'PROG ARGS'	Filter program (may be repeated)"
//usage:     "\n	-M 'PROG ARGS'	Delivery program"
//usage:	)
//usage:     "\n"
//usage:     "\nFetch from plain POP3 server:"
//usage:     "\npopmaildir -k DIR nc pop3.server.com 110 <user_and_pass.txt"
//usage:     "\nFetch from SSLed POP3 server and delete fetched emails:"
//usage:     "\npopmaildir DIR -- openssl s_client -quiet -connect pop3.server.com:995 <user_and_pass.txt"
/* //usage:  "\n	-R BYTES	Remove old messages on the server >= BYTES. Ignored" */
/* //usage:  "\n	-Z N1-N2	Remove messages from N1 to N2 (dangerous). Ignored" */
/* //usage:  "\n	-L BYTES	Don't retrieve new messages >= BYTES. Ignored" */
/* //usage:  "\n	-H LINES	Type first LINES of a message. Ignored" */
//usage:
//usage:#define popmaildir_example_usage
//usage:       "$ popmaildir -k ~/Maildir -- nc pop.drvv.ru 110 [<password_file]\n"
//usage:       "$ popmaildir ~/Maildir -- openssl s_client -quiet -connect pop.gmail.com:995 [<password_file]\n"

#include "libbb.h"
#include "mail.h"

static void pop3_checkr(const char *fmt, const char *param, char **ret)
{
	char *msg = send_mail_command(fmt, param);
	char *answer = xmalloc_fgetline(stdin);
	if (answer && '+' == answer[0]) {
		free(msg);
		if (timeout)
			alarm(0);
		if (ret) {
			// skip "+OK "
			memmove(answer, answer + 4, strlen(answer) - 4);
			*ret = answer;
		} else
			free(answer);
		return;
	}
	bb_error_msg_and_die("%s failed, reply was: %s", msg, answer);
}

static void pop3_check(const char *fmt, const char *param)
{
	pop3_checkr(fmt, param, NULL);
}

int popmaildir_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int popmaildir_main(int argc UNUSED_PARAM, char **argv)
{
	char *buf;
	unsigned nmsg;
	char *hostname;
	pid_t pid;
	const char *retr;
#if ENABLE_FEATURE_POPMAILDIR_DELIVERY
	const char *delivery;
#endif
	unsigned opt_nlines = 0;

	enum {
		OPT_b = 1 << 0,		// -b binary mode. Ignored
		OPT_d = 1 << 1,		// -d,-dd,-ddd debug. Ignored
		OPT_m = 1 << 2,		// -m show used memory. Ignored
		OPT_V = 1 << 3,		// -V version. Ignored
		OPT_c = 1 << 4,		// -c use tcpclient. Ignored
		OPT_a = 1 << 5,		// -a use APOP protocol
		OPT_s = 1 << 6,		// -s skip authorization
		OPT_T = 1 << 7,		// -T get messages with TOP instead with RETR
		OPT_k = 1 << 8,		// -k keep retrieved messages on the server
		OPT_t = 1 << 9,		// -t90 set timeout to 90 sec
		OPT_R = 1 << 10,	// -R20000 remove old messages on the server >= 20000 bytes (requires -k). Ignored
		OPT_Z = 1 << 11,	// -Z11-23 remove messages from 11 to 23 (dangerous). Ignored
		OPT_L = 1 << 12,	// -L50000 not retrieve new messages >= 50000 bytes. Ignored
		OPT_H = 1 << 13,	// -H30 type first 30 lines of a message; (-L12000 -H30). Ignored
		OPT_M = 1 << 14,	// -M\"program arg1 arg2 ...\"; deliver by program. Treated like -F
		OPT_F = 1 << 15,	// -F\"program arg1 arg2 ...\"; filter by program. Treated like -M
	};

	// init global variables
	INIT_G();

	// parse options
	opts = getopt32(argv, "^"
		"bdmVcasTkt:+" "R:+Z:L:+H:+" IF_FEATURE_POPMAILDIR_DELIVERY("M:F:")
		"\0" "-1:dd",
		&timeout, NULL, NULL, NULL, &opt_nlines
		IF_FEATURE_POPMAILDIR_DELIVERY(, &delivery, &delivery) // we treat -M and -F the same
	);
	//argc -= optind;
	argv += optind;

	// get auth info
	if (!(opts & OPT_s))
		get_cred_or_die(STDIN_FILENO);

	// goto maildir
	xchdir(*argv++);

	// launch connect helper, if any
	if (*argv)
		launch_helper((const char **)argv);

	// get server greeting
	pop3_checkr(NULL, NULL, &buf);

	// authenticate (if no -s given)
	if (!(opts & OPT_s)) {
		// server supports APOP and we want it?
		if ('<' == buf[0] && (opts & OPT_a)) {
			union { // save a bit of stack
				md5_ctx_t ctx;
				char hex[16 * 2 + 1];
			} md5;
			uint32_t res[16 / 4];

			char *s = strchr(buf, '>');
			if (s)
				s[1] = '\0';
			// get md5 sum of "<stamp>password" string
			md5_begin(&md5.ctx);
			md5_hash(&md5.ctx, buf, strlen(buf));
			md5_hash(&md5.ctx, G.pass, strlen(G.pass));
			md5_end(&md5.ctx, res);
			*bin2hex(md5.hex, (char*)res, 16) = '\0';
			// APOP
			s = xasprintf("%s %s", G.user, md5.hex);
			pop3_check("APOP %s", s);
			free(s);
			free(buf);
		// server ignores APOP -> use simple text authentication
		} else {
			// USER
			pop3_check("USER %s", G.user);
			// PASS
			pop3_check("PASS %s", G.pass);
		}
	}

	// get mailbox statistics
	pop3_checkr("STAT", NULL, &buf);

	// prepare message filename suffix
	hostname = safe_gethostname();
	pid = getpid();

	// get messages counter
	// NOTE: we don't use xatou(buf) since buf is "nmsg nbytes"
	// we only need nmsg and atoi is just exactly what we need
	// if atoi fails to convert buf into number it returns 0
	// in this case the following loop simply will not be executed
	nmsg = atoi(buf);
	free(buf);

	// loop through messages
	retr = (opts & OPT_T) ? xasprintf("TOP %%u %u", opt_nlines) : "RETR %u";
	for (; nmsg; nmsg--) {

		char *filename;
		char *target;
		char *answer;
		FILE *fp;
#if ENABLE_FEATURE_POPMAILDIR_DELIVERY
		int rc;
#endif
		// generate unique filename
		filename  = xasprintf("tmp/%llu.%u.%s",
			monotonic_us(), (unsigned)pid, hostname);

		// retrieve message in ./tmp/ unless filter is specified
		pop3_check(retr, (const char *)(ptrdiff_t)nmsg);

#if ENABLE_FEATURE_POPMAILDIR_DELIVERY
		// delivery helper ordered? -> setup pipe
		if (opts & (OPT_F|OPT_M)) {
			// helper will have $FILENAME set to filename
			xsetenv("FILENAME", filename);
			fp = popen(delivery, "w");
			unsetenv("FILENAME");
			if (!fp) {
				bb_perror_msg("delivery helper");
				break;
			}
		} else
#endif
		// create and open file filename
		fp = xfopen_for_write(filename);

		// copy stdin to fp (either filename or delivery helper)
		while ((answer = xmalloc_fgets_str(stdin, "\r\n")) != NULL) {
			char *s = answer;
			if ('.' == answer[0]) {
				if ('.' == answer[1])
					s++;
				else if ('\r' == answer[1] && '\n' == answer[2] && '\0' == answer[3])
					break;
			}
			//*strchrnul(s, '\r') = '\n';
			fputs(s, fp);
			free(answer);
		}

#if ENABLE_FEATURE_POPMAILDIR_DELIVERY
		// analyse delivery status
		if (opts & (OPT_F|OPT_M)) {
			rc = pclose(fp);
			if (99 == rc) // 99 means bail out
				break;
//			if (rc) // !0 means skip to the next message
				goto skip;
//			// 0 means continue
		} else {
			// close filename
			fclose(fp);
		}
#endif

		// delete message from server
		if (!(opts & OPT_k))
			pop3_check("DELE %u", (const char*)(ptrdiff_t)nmsg);

		// atomically move message to ./new/
		target = xstrdup(filename);
		memcpy(target, "new", 3);
		// ... or just stop receiving on failure
		if (rename_or_warn(filename, target))
			break;
		free(target);

#if ENABLE_FEATURE_POPMAILDIR_DELIVERY
 skip:
#endif
		free(filename);
	}

	// Bye
	pop3_check("QUIT", NULL);

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(G.user);
		free(G.pass);
	}

	return EXIT_SUCCESS;
}
