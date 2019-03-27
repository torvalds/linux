/*
 * Copyright (c) 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

static char *copyright[] = {
    "@(#) Copyright (c) 1988, 1990, 1993\n"
    "\tThe Regents of the University of California.  All rights reserved.\n",
    (char*)copyright
};

#include "telnet_locl.h"
RCSID("$Id$");

#if KRB5
#define FORWARD
#endif

/*
 * Initialize variables.
 */
void
tninit(void)
{
    init_terminal();

    init_network();

    init_telnet();

    init_sys();
}

static void
usage(int exit_code)
{
  fprintf(stderr, "Usage: %s %s%s%s%s\n", prompt,
#ifdef	AUTHENTICATION
	  "[-8] [-E] [-K] [-L] [-G] [-S tos] [-X atype] [-a] [-c] [-d] [-e char]",
	  "\n\t[-k realm] [-l user] [-f/-F] [-n tracefile] ",
#else
	  "[-8] [-E] [-L] [-S tos] [-a] [-c] [-d] [-e char] [-l user]",
	  "\n\t[-n tracefile]",
#endif
	  "[-r] ",
#ifdef	ENCRYPTION
	  "[-x] [host-name [port]]"
#else
	  "[host-name [port]]"
#endif
    );
  exit(exit_code);
}

/*
 * main.  Parse arguments, invoke the protocol or command parser.
 */


#ifdef	FORWARD
int forward_option = 0; /* forward flags set from command line */
#endif	/* FORWARD */
void
set_forward_options(void)
{
#ifdef FORWARD
	switch(forward_option) {
	case 'f':
		kerberos5_set_forward(1);
		kerberos5_set_forwardable(0);
		break;
	case 'F':
		kerberos5_set_forward(1);
		kerberos5_set_forwardable(1);
		break;
	case 'G':
		kerberos5_set_forward(0);
		kerberos5_set_forwardable(0);
		break;
	default:
		break;
	}
#endif
}

#ifdef KRB5
#define Authenticator asn1_Authenticator
#include <krb5.h>
static void
krb5_init(void)
{
    krb5_context context;
    krb5_error_code ret;
    krb5_boolean ret_val;

    ret = krb5_init_context(&context);
    if (ret)
	return;

#if defined(AUTHENTICATION) && defined(FORWARD)
    krb5_appdefault_boolean(context, NULL,
			    NULL, "forward",
			    0, &ret_val);
    if (ret_val)
	    kerberos5_set_forward(1);
    krb5_appdefault_boolean(context, NULL,
			    NULL, "forwardable",
			    0, &ret_val);
    if (ret_val)
	    kerberos5_set_forwardable(1);
#endif
#ifdef  ENCRYPTION
    krb5_appdefault_boolean(context, NULL,
			    NULL, "encrypt",
			    0, &ret_val);
    if (ret_val) {
          encrypt_auto(1);
          decrypt_auto(1);
	  wantencryption = 1;
          EncryptVerbose(1);
        }
#endif

    krb5_free_context(context);
}
#endif

int
main(int argc, char **argv)
{
	int ch;
	char *user;

	setprogname(argv[0]);

#ifdef KRB5
	krb5_init();
#endif

	tninit();		/* Clear out things */

	TerminalSaveState();

	if ((prompt = strrchr(argv[0], '/')))
		++prompt;
	else
		prompt = argv[0];

	user = NULL;

	rlogin = (strncmp(prompt, "rlog", 4) == 0) ? '~' : _POSIX_VDISABLE;

	/*
	 * if AUTHENTICATION and ENCRYPTION is set autologin will be
	 * se to true after the getopt switch; unless the -K option is
	 * passed
	 */
	autologin = -1;

	if (argc == 2 && strcmp(argv[1], "--version") == 0) {
	    print_version(NULL);
	    exit(0);
	}
	if (argc == 2 && strcmp(argv[1], "--help") == 0)
	    usage(0);


	while((ch = getopt(argc, argv,
			   "78DEKLS:X:abcde:fFk:l:n:rxG")) != -1) {
		switch(ch) {
		case '8':
			eight = 3;	/* binary output and input */
			break;
		case '7':
			eight = 0;
			break;
		case 'b':
		    binary = 3;
		    break;
		case 'D': {
		    /* sometimes we don't want a mangled display */
		    char *p;
		    if((p = getenv("DISPLAY")))
			env_define((unsigned char*)"DISPLAY", (unsigned char*)p);
		    break;
		}
		case 'E':
			rlogin = escape = _POSIX_VDISABLE;
			break;
		case 'K':
#ifdef	AUTHENTICATION
			autologin = 0;
#endif
			break;
		case 'L':
			eight |= 2;	/* binary output only */
			break;
		case 'S':
		    {
#ifdef	HAVE_PARSETOS
			extern int tos;

			if ((tos = parsetos(optarg, "tcp")) < 0)
				fprintf(stderr, "%s%s%s%s\n",
					prompt, ": Bad TOS argument '",
					optarg,
					"; will try to use default TOS");
#else
			fprintf(stderr,
			   "%s: Warning: -S ignored, no parsetos() support.\n",
								prompt);
#endif
		    }
			break;
		case 'X':
#ifdef	AUTHENTICATION
			auth_disable_name(optarg);
#endif
			break;
		case 'a':
			autologin = 1;
			break;
		case 'c':
			skiprc = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'e':
			set_escape_char(optarg);
			break;
		case 'f':
		case 'F':
		case 'G':
#if defined(AUTHENTICATION) && defined(KRB5) && defined(FORWARD)
			if (forward_option) {
			    fprintf(stderr,
				    "%s: Only one of -f, -F and -G allowed.\n",
				    prompt);
			    usage(1);
			}
			forward_option = ch;
#else
			fprintf(stderr,
			 "%s: Warning: -%c ignored, no Kerberos V5 support.\n",
				prompt, ch);
#endif
			break;
		case 'k':
		    fprintf(stderr,
			    "%s: Warning: -k ignored, no Kerberos V4 support.\n",
			    prompt);
		    break;
		case 'l':
		  if(autologin == 0){
		    fprintf(stderr, "%s: Warning: -K ignored\n", prompt);
		    autologin = -1;
		  }
			user = optarg;
			break;
		case 'n':
				SetNetTrace(optarg);
			break;
		case 'r':
			rlogin = '~';
			break;
		case 'x':
#ifdef	ENCRYPTION
			encrypt_auto(1);
			decrypt_auto(1);
			wantencryption = 1;
			EncryptVerbose(1);
#else
			fprintf(stderr,
			    "%s: Warning: -x ignored, no ENCRYPT support.\n",
								prompt);
#endif
			break;

		case '?':
		default:
			usage(1);
			/* NOTREACHED */
		}
	}

	if (autologin == -1) {		/* esc@magic.fi; force  */
#if defined(AUTHENTICATION)
		autologin = 1;
#endif
#if defined(ENCRYPTION)
		encrypt_auto(1);
		decrypt_auto(1);
		wantencryption = -1;
#endif
	}

	if (autologin == -1)
		autologin = (rlogin == _POSIX_VDISABLE) ? 0 : 1;

	argc -= optind;
	argv += optind;

	if (argc) {
		char *args[7], **argp = args;

		if (argc > 2)
			usage(1);
		*argp++ = prompt;
		if (user) {
			*argp++ = "-l";
			*argp++ = user;
		}
		*argp++ = argv[0];		/* host */
		if (argc > 1)
			*argp++ = argv[1];	/* port */
		*argp = 0;

		if (setjmp(toplevel) != 0)
			Exit(0);
		if (tn(argp - args, args) == 1)
			return (0);
		else
			return (1);
	}
	setjmp(toplevel);
	for (;;) {
			command(1, 0, 0);
	}
}
