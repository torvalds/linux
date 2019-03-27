/*	$NetBSD: ftpcmd.y,v 1.6 1995/06/03 22:46:45 mycroft Exp $	*/

/*
 * Copyright (c) 1985, 1988, 1993, 1994
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
 *
 *	@(#)ftpcmd.y	8.3 (Berkeley) 4/6/94
 */

/*
 * Grammar for FTP commands.
 * See RFC 959.
 */

%{

#include "ftpd_locl.h"
RCSID("$Id$");

off_t	restart_point;

static	int hasyyerrored;


static	int cmd_type;
static	int cmd_form;
static	int cmd_bytesz;
char	cbuf[64*1024];
char	*fromname;

struct tab {
	char	*name;
	short	token;
	short	state;
	short	implemented;	/* 1 if command is implemented */
	char	*help;
};

extern struct tab cmdtab[];
extern struct tab sitetab[];

static char		*copy (char *);
static void		 help (struct tab *, char *);
static struct tab *
			 lookup (struct tab *, char *);
static void		 sizecmd (char *);
static RETSIGTYPE	 toolong (int);
static int		 yylex (void);

/* This is for bison */

#if !defined(alloca) && !defined(HAVE_ALLOCA)
#define alloca(x) malloc(x)
#endif

%}

%union {
	int	i;
	char   *s;
}

%token
	A	B	C	E	F	I
	L	N	P	R	S	T

	SP	CRLF	COMMA

	USER	PASS	ACCT	REIN	QUIT	PORT
	PASV	TYPE	STRU	MODE	RETR	STOR
	APPE	MLFL	MAIL	MSND	MSOM	MSAM
	MRSQ	MRCP	ALLO	REST	RNFR	RNTO
	ABOR	DELE	CWD	LIST	NLST	SITE
	sTAT	HELP	NOOP	MKD	RMD	PWD
	CDUP	STOU	SMNT	SYST	SIZE	MDTM
	EPRT	EPSV

	UMASK	IDLE	CHMOD

	AUTH	ADAT	PROT	PBSZ	CCC	MIC
	CONF	ENC

	KAUTH	KLIST	KDESTROY KRBTKFILE AFSLOG
	LOCATE	URL

	FEAT	OPTS

	LEXERR

%token	<s> STRING
%token	<i> NUMBER

%type	<i> check_login check_login_no_guest check_secure octal_number byte_size
%type	<i> struct_code mode_code type_code form_code
%type	<s> pathstring pathname password username

%start	cmd_list

%%

cmd_list
	: /* empty */
	| cmd_list cmd
		{
			fromname = (char *) 0;
			restart_point = (off_t) 0;
		}
	| cmd_list rcmd
	;

cmd
	: USER SP username CRLF check_secure
		{
		    if ($5)
			user($3);
		    free($3);
		}
	| PASS SP password CRLF check_secure
		{
		    if ($5)
			pass($3);
		    memset ($3, 0, strlen($3));
		    free($3);
		}

	| PORT SP host_port CRLF check_secure
		{
		    if ($5) {
			if (paranoid &&
			    (data_dest->sa_family != his_addr->sa_family ||
			     (socket_get_port(data_dest) < IPPORT_RESERVED) ||
			     memcmp(socket_get_address(data_dest),
				    socket_get_address(his_addr),
				    socket_addr_size(his_addr)) != 0)) {
			    usedefault = 1;
			    reply(500, "Illegal PORT range rejected.");
			} else {
			    usedefault = 0;
			    if (pdata >= 0) {
				close(pdata);
				pdata = -1;
			    }
			    reply(200, "PORT command successful.");
			}
		    }
		}
	| EPRT SP STRING CRLF check_secure
		{
		    if ($5)
			eprt ($3);
		    free ($3);
		}
	| PASV CRLF check_login
		{
		    if($3)
			pasv ();
		}
	| EPSV CRLF check_login
		{
		    if($3)
			epsv (NULL);
		}
	| EPSV SP STRING CRLF check_login
		{
		    if($5)
			epsv ($3);
		    free ($3);
		}
	| TYPE SP type_code CRLF check_secure
		{
		    if ($5) {
			switch (cmd_type) {

			case TYPE_A:
				if (cmd_form == FORM_N) {
					reply(200, "Type set to A.");
					type = cmd_type;
					form = cmd_form;
				} else
					reply(504, "Form must be N.");
				break;

			case TYPE_E:
				reply(504, "Type E not implemented.");
				break;

			case TYPE_I:
				reply(200, "Type set to I.");
				type = cmd_type;
				break;

			case TYPE_L:
#if NBBY == 8
				if (cmd_bytesz == 8) {
					reply(200,
					    "Type set to L (byte size 8).");
					type = cmd_type;
				} else
					reply(504, "Byte size must be 8.");
#else /* NBBY == 8 */
				UNIMPLEMENTED for NBBY != 8
#endif /* NBBY == 8 */
			}
		    }
		}
	| STRU SP struct_code CRLF check_secure
		{
		    if ($5) {
			switch ($3) {

			case STRU_F:
				reply(200, "STRU F ok.");
				break;

			default:
				reply(504, "Unimplemented STRU type.");
			}
		    }
		}
	| MODE SP mode_code CRLF check_secure
		{
		    if ($5) {
			switch ($3) {

			case MODE_S:
				reply(200, "MODE S ok.");
				break;

			default:
				reply(502, "Unimplemented MODE type.");
			}
		    }
		}
	| ALLO SP NUMBER CRLF check_secure
		{
		    if ($5) {
			reply(202, "ALLO command ignored.");
		    }
		}
	| ALLO SP NUMBER SP R SP NUMBER CRLF check_secure
		{
		    if ($9) {
			reply(202, "ALLO command ignored.");
		    }
		}
	| RETR SP pathname CRLF check_login
		{
			char *name = $3;

			if ($5 && name != NULL)
				retrieve(0, name);
			if (name != NULL)
				free(name);
		}
	| STOR SP pathname CRLF check_login
		{
			char *name = $3;

			if ($5 && name != NULL)
				do_store(name, "w", 0);
			if (name != NULL)
				free(name);
		}
	| APPE SP pathname CRLF check_login
		{
			char *name = $3;

			if ($5 && name != NULL)
				do_store(name, "a", 0);
			if (name != NULL)
				free(name);
		}
	| NLST CRLF check_login
		{
			if ($3)
				send_file_list(".");
		}
	| NLST SP STRING CRLF check_login
		{
			char *name = $3;

			if ($5 && name != NULL)
				send_file_list(name);
			if (name != NULL)
				free(name);
		}
	| LIST CRLF check_login
		{
		    if($3)
			list_file(".");
		}
	| LIST SP pathname CRLF check_login
		{
		    if($5)
			list_file($3);
		    free($3);
		}
	| sTAT SP pathname CRLF check_login
		{
			if ($5 && $3 != NULL)
				statfilecmd($3);
			if ($3 != NULL)
				free($3);
		}
	| sTAT CRLF check_secure
		{
		    if ($3)
			statcmd();
		}
	| DELE SP pathname CRLF check_login_no_guest
		{
			if ($5 && $3 != NULL)
				do_delete($3);
			if ($3 != NULL)
				free($3);
		}
	| RNTO SP pathname CRLF check_login_no_guest
		{
			if($5){
				if (fromname) {
					renamecmd(fromname, $3);
					free(fromname);
					fromname = (char *) 0;
				} else {
					reply(503, "Bad sequence of commands.");
				}
			}
			if ($3 != NULL)
				free($3);
		}
	| ABOR CRLF check_secure
		{
		    if ($3)
			reply(225, "ABOR command successful.");
		}
	| CWD CRLF check_login
		{
			if ($3) {
				const char *path = pw->pw_dir;
				if (dochroot || guest)
					path = "/";
				cwd(path);
			}
		}
	| CWD SP pathname CRLF check_login
		{
			if ($5 && $3 != NULL)
				cwd($3);
			if ($3 != NULL)
				free($3);
		}
	| HELP CRLF check_secure
		{
		    if ($3)
			help(cmdtab, (char *) 0);
		}
	| HELP SP STRING CRLF check_secure
		{
		    if ($5) {
			char *cp = $3;

			if (strncasecmp(cp, "SITE", 4) == 0) {
				cp = $3 + 4;
				if (*cp == ' ')
					cp++;
				if (*cp)
					help(sitetab, cp);
				else
					help(sitetab, (char *) 0);
			} else
				help(cmdtab, $3);
		    }
		}
	| NOOP CRLF check_secure
		{
		    if ($3)
			reply(200, "NOOP command successful.");
		}
	| MKD SP pathname CRLF check_login
		{
			if ($5 && $3 != NULL)
				makedir($3);
			if ($3 != NULL)
				free($3);
		}
	| RMD SP pathname CRLF check_login_no_guest
		{
			if ($5 && $3 != NULL)
				removedir($3);
			if ($3 != NULL)
				free($3);
		}
	| PWD CRLF check_login
		{
			if ($3)
				pwd();
		}
	| CDUP CRLF check_login
		{
			if ($3)
				cwd("..");
		}
	| FEAT CRLF check_secure
		{
		    if ($3) {
			lreply(211, "Supported features:");
			lreply(0, " MDTM");
			lreply(0, " REST STREAM");
			lreply(0, " SIZE");
			reply(211, "End");
		    }
		}
	| OPTS SP STRING CRLF check_secure
		{
		    if ($5)
			reply(501, "Bad options");
		    free ($3);
		}

	| SITE SP HELP CRLF check_secure
		{
		    if ($5)
			help(sitetab, (char *) 0);
		}
	| SITE SP HELP SP STRING CRLF check_secure
		{
		    if ($7)
			help(sitetab, $5);
		}
	| SITE SP UMASK CRLF check_login
		{
			if ($5) {
				int oldmask = umask(0);
				umask(oldmask);
				reply(200, "Current UMASK is %03o", oldmask);
			}
		}
	| SITE SP UMASK SP octal_number CRLF check_login_no_guest
		{
			if ($7) {
				if (($5 == -1) || ($5 > 0777)) {
					reply(501, "Bad UMASK value");
				} else {
					int oldmask = umask($5);
					reply(200,
					      "UMASK set to %03o (was %03o)",
					      $5, oldmask);
				}
			}
		}
	| SITE SP CHMOD SP octal_number SP pathname CRLF check_login_no_guest
		{
			if ($9 && $7 != NULL) {
				if ($5 > 0777)
					reply(501,
				"CHMOD: Mode value must be between 0 and 0777");
				else if (chmod($7, $5) < 0)
					perror_reply(550, $7);
				else
					reply(200, "CHMOD command successful.");
			}
			if ($7 != NULL)
				free($7);
		}
	| SITE SP IDLE CRLF check_secure
		{
		    if ($5)
			reply(200,
			    "Current IDLE time limit is %d seconds; max %d",
				ftpd_timeout, maxtimeout);
		}
	| SITE SP IDLE SP NUMBER CRLF check_secure
		{
		    if ($7) {
			if ($5 < 30 || $5 > maxtimeout) {
				reply(501,
			"Maximum IDLE time must be between 30 and %d seconds",
				    maxtimeout);
			} else {
				ftpd_timeout = $5;
				alarm((unsigned) ftpd_timeout);
				reply(200,
				    "Maximum IDLE time set to %d seconds",
				    ftpd_timeout);
			}
		    }
		}

	| SITE SP KAUTH SP STRING CRLF check_login
		{
			reply(500, "Command not implemented.");
		}
	| SITE SP KLIST CRLF check_login
		{
		    if($5)
			klist();
		}
	| SITE SP KDESTROY CRLF check_login
		{
		    reply(500, "Command not implemented.");
		}
	| SITE SP KRBTKFILE SP STRING CRLF check_login
		{
		    reply(500, "Command not implemented.");
		}
	| SITE SP AFSLOG CRLF check_login
		{
#if defined(KRB5)
		    if(guest)
			reply(500, "Can't be done as guest.");
		    else if($5)
			afslog(NULL, 0);
#else
		    reply(500, "Command not implemented.");
#endif
		}
	| SITE SP AFSLOG SP STRING CRLF check_login
		{
#if defined(KRB5)
		    if(guest)
			reply(500, "Can't be done as guest.");
		    else if($7)
			afslog($5, 0);
		    if($5)
			free($5);
#else
		    reply(500, "Command not implemented.");
#endif
		}
	| SITE SP LOCATE SP STRING CRLF check_login
		{
		    if($7 && $5 != NULL)
			find($5);
		    if($5 != NULL)
			free($5);
		}
	| SITE SP URL CRLF check_secure
		{
		    if ($5)
			reply(200, "http://www.pdc.kth.se/heimdal/");
		}
	| STOU SP pathname CRLF check_login
		{
			if ($5 && $3 != NULL)
				do_store($3, "w", 1);
			if ($3 != NULL)
				free($3);
		}
	| SYST CRLF check_secure
		{
		    if ($3) {
#if !defined(WIN32) && !defined(__EMX__) && !defined(__OS2__) && !defined(__CYGWIN32__)
			reply(215, "UNIX Type: L%d", NBBY);
#else
			reply(215, "UNKNOWN Type: L%d", NBBY);
#endif
		    }
		}

		/*
		 * SIZE is not in RFC959, but Postel has blessed it and
		 * it will be in the updated RFC.
		 *
		 * Return size of file in a format suitable for
		 * using with RESTART (we just count bytes).
		 */
	| SIZE SP pathname CRLF check_login
		{
			if ($5 && $3 != NULL)
				sizecmd($3);
			if ($3 != NULL)
				free($3);
		}

		/*
		 * MDTM is not in RFC959, but Postel has blessed it and
		 * it will be in the updated RFC.
		 *
		 * Return modification time of file as an ISO 3307
		 * style time. E.g. YYYYMMDDHHMMSS or YYYYMMDDHHMMSS.xxx
		 * where xxx is the fractional second (of any precision,
		 * not necessarily 3 digits)
		 */
	| MDTM SP pathname CRLF check_login
		{
			if ($5 && $3 != NULL) {
				struct stat stbuf;
				if (stat($3, &stbuf) < 0)
					reply(550, "%s: %s",
					    $3, strerror(errno));
				else if (!S_ISREG(stbuf.st_mode)) {
					reply(550,
					      "%s: not a plain file.", $3);
				} else {
					struct tm *t;
					time_t mtime = stbuf.st_mtime;

					t = gmtime(&mtime);
					reply(213,
					      "%04d%02d%02d%02d%02d%02d",
					      t->tm_year + 1900,
					      t->tm_mon + 1,
					      t->tm_mday,
					      t->tm_hour,
					      t->tm_min,
					      t->tm_sec);
				}
			}
			if ($3 != NULL)
				free($3);
		}
	| QUIT CRLF check_secure
		{
		    if ($3) {
			reply(221, "Goodbye.");
			dologout(0);
		    }
		}
	| error CRLF
		{
			yyerrok;
		}
	;
rcmd
	: RNFR SP pathname CRLF check_login_no_guest
		{
			restart_point = (off_t) 0;
			if ($5 && $3) {
				fromname = renamefrom($3);
				if (fromname == (char *) 0 && $3) {
					free($3);
				}
			}
		}
	| REST SP byte_size CRLF check_secure
		{
		    if ($5) {
			fromname = (char *) 0;
			restart_point = $3;	/* XXX $3 is only "int" */
			reply(350, "Restarting at %ld. %s",
			      (long)restart_point,
			      "Send STORE or RETRIEVE to initiate transfer.");
		    }
		}
	| AUTH SP STRING CRLF
		{
			auth($3);
			free($3);
		}
	| ADAT SP STRING CRLF
		{
			adat($3);
			free($3);
		}
	| PBSZ SP NUMBER CRLF check_secure
		{
		    if ($5)
			pbsz($3);
		}
	| PROT SP STRING CRLF check_secure
		{
		    if ($5)
			prot($3);
		}
	| CCC CRLF check_secure
		{
		    if ($3)
			ccc();
		}
	| MIC SP STRING CRLF
		{
			mec($3, prot_safe);
			free($3);
		}
	| CONF SP STRING CRLF
		{
			mec($3, prot_confidential);
			free($3);
		}
	| ENC SP STRING CRLF
		{
			mec($3, prot_private);
			free($3);
		}
	;

username
	: STRING
	;

password
	: /* empty */
		{
			$$ = (char *)calloc(1, sizeof(char));
		}
	| STRING
	;

byte_size
	: NUMBER
	;

host_port
	: NUMBER COMMA NUMBER COMMA NUMBER COMMA NUMBER COMMA
		NUMBER COMMA NUMBER
		{
			struct sockaddr_in *sin4 = (struct sockaddr_in *)data_dest;

			sin4->sin_family = AF_INET;
			sin4->sin_port = htons($9 * 256 + $11);
			sin4->sin_addr.s_addr =
			    htonl(($1 << 24) | ($3 << 16) | ($5 << 8) | $7);
		}
	;

form_code
	: N
		{
			$$ = FORM_N;
		}
	| T
		{
			$$ = FORM_T;
		}
	| C
		{
			$$ = FORM_C;
		}
	;

type_code
	: A
		{
			cmd_type = TYPE_A;
			cmd_form = FORM_N;
		}
	| A SP form_code
		{
			cmd_type = TYPE_A;
			cmd_form = $3;
		}
	| E
		{
			cmd_type = TYPE_E;
			cmd_form = FORM_N;
		}
	| E SP form_code
		{
			cmd_type = TYPE_E;
			cmd_form = $3;
		}
	| I
		{
			cmd_type = TYPE_I;
		}
	| L
		{
			cmd_type = TYPE_L;
			cmd_bytesz = NBBY;
		}
	| L SP byte_size
		{
			cmd_type = TYPE_L;
			cmd_bytesz = $3;
		}
		/* this is for a bug in the BBN ftp */
	| L byte_size
		{
			cmd_type = TYPE_L;
			cmd_bytesz = $2;
		}
	;

struct_code
	: F
		{
			$$ = STRU_F;
		}
	| R
		{
			$$ = STRU_R;
		}
	| P
		{
			$$ = STRU_P;
		}
	;

mode_code
	: S
		{
			$$ = MODE_S;
		}
	| B
		{
			$$ = MODE_B;
		}
	| C
		{
			$$ = MODE_C;
		}
	;

pathname
	: pathstring
		{
			/*
			 * Problem: this production is used for all pathname
			 * processing, but only gives a 550 error reply.
			 * This is a valid reply in some cases but not in others.
			 */
			if (logged_in && $1 && *$1 == '~') {
				glob_t gl;
				int flags =
				 GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE;

				memset(&gl, 0, sizeof(gl));
				if (glob($1, flags, NULL, &gl) ||
				    gl.gl_pathc == 0) {
					reply(550, "not found");
					$$ = NULL;
				} else {
					$$ = strdup(gl.gl_pathv[0]);
				}
				globfree(&gl);
				free($1);
			} else
				$$ = $1;
		}
	;

pathstring
	: STRING
	;

octal_number
	: NUMBER
		{
			int ret, dec, multby, digit;

			/*
			 * Convert a number that was read as decimal number
			 * to what it would be if it had been read as octal.
			 */
			dec = $1;
			multby = 1;
			ret = 0;
			while (dec) {
				digit = dec%10;
				if (digit > 7) {
					ret = -1;
					break;
				}
				ret += digit * multby;
				multby *= 8;
				dec /= 10;
			}
			$$ = ret;
		}
	;


check_login_no_guest : check_login
		{
			$$ = $1 && !guest;
			if($1 && !$$)
				reply(550, "Permission denied");
		}
	;

check_login : check_secure
		{
		    if($1) {
			if(($$ = logged_in) == 0)
			    reply(530, "Please login with USER and PASS.");
		    } else
			$$ = 0;
		}
	;

check_secure : /* empty */
		{
		    $$ = 1;
		    if(sec_complete && !ccc_passed && !secure_command()) {
			$$ = 0;
			reply(533, "Command protection level denied "
			      "for paranoid reasons.");
		    }
		}
	;

%%

#define	CMD	0	/* beginning of command */
#define	ARGS	1	/* expect miscellaneous arguments */
#define	STR1	2	/* expect SP followed by STRING */
#define	STR2	3	/* expect STRING */
#define	OSTR	4	/* optional SP then STRING */
#define	ZSTR1	5	/* SP then optional STRING */
#define	ZSTR2	6	/* optional STRING after SP */
#define	SITECMD	7	/* SITE command */
#define	NSTR	8	/* Number followed by a string */

struct tab cmdtab[] = {		/* In order defined in RFC 765 */
	{ "USER", USER, STR1, 1,	"<sp> username" },
	{ "PASS", PASS, ZSTR1, 1,	"<sp> password" },
	{ "ACCT", ACCT, STR1, 0,	"(specify account)" },
	{ "SMNT", SMNT, ARGS, 0,	"(structure mount)" },
	{ "REIN", REIN, ARGS, 0,	"(reinitialize server state)" },
	{ "QUIT", QUIT, ARGS, 1,	"(terminate service)", },
	{ "PORT", PORT, ARGS, 1,	"<sp> b0, b1, b2, b3, b4" },
	{ "EPRT", EPRT, STR1, 1,	"<sp> string" },
	{ "PASV", PASV, ARGS, 1,	"(set server in passive mode)" },
	{ "EPSV", EPSV, OSTR, 1,	"[<sp> foo]" },
	{ "TYPE", TYPE, ARGS, 1,	"<sp> [ A | E | I | L ]" },
	{ "STRU", STRU, ARGS, 1,	"(specify file structure)" },
	{ "MODE", MODE, ARGS, 1,	"(specify transfer mode)" },
	{ "RETR", RETR, STR1, 1,	"<sp> file-name" },
	{ "STOR", STOR, STR1, 1,	"<sp> file-name" },
	{ "APPE", APPE, STR1, 1,	"<sp> file-name" },
	{ "MLFL", MLFL, OSTR, 0,	"(mail file)" },
	{ "MAIL", MAIL, OSTR, 0,	"(mail to user)" },
	{ "MSND", MSND, OSTR, 0,	"(mail send to terminal)" },
	{ "MSOM", MSOM, OSTR, 0,	"(mail send to terminal or mailbox)" },
	{ "MSAM", MSAM, OSTR, 0,	"(mail send to terminal and mailbox)" },
	{ "MRSQ", MRSQ, OSTR, 0,	"(mail recipient scheme question)" },
	{ "MRCP", MRCP, STR1, 0,	"(mail recipient)" },
	{ "ALLO", ALLO, ARGS, 1,	"allocate storage (vacuously)" },
	{ "REST", REST, ARGS, 1,	"<sp> offset (restart command)" },
	{ "RNFR", RNFR, STR1, 1,	"<sp> file-name" },
	{ "RNTO", RNTO, STR1, 1,	"<sp> file-name" },
	{ "ABOR", ABOR, ARGS, 1,	"(abort operation)" },
	{ "DELE", DELE, STR1, 1,	"<sp> file-name" },
	{ "CWD",  CWD,  OSTR, 1,	"[ <sp> directory-name ]" },
	{ "XCWD", CWD,	OSTR, 1,	"[ <sp> directory-name ]" },
	{ "LIST", LIST, OSTR, 1,	"[ <sp> path-name ]" },
	{ "NLST", NLST, OSTR, 1,	"[ <sp> path-name ]" },
	{ "SITE", SITE, SITECMD, 1,	"site-cmd [ <sp> arguments ]" },
	{ "SYST", SYST, ARGS, 1,	"(get type of operating system)" },
	{ "STAT", sTAT, OSTR, 1,	"[ <sp> path-name ]" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },
	{ "NOOP", NOOP, ARGS, 1,	"" },
	{ "MKD",  MKD,  STR1, 1,	"<sp> path-name" },
	{ "XMKD", MKD,  STR1, 1,	"<sp> path-name" },
	{ "RMD",  RMD,  STR1, 1,	"<sp> path-name" },
	{ "XRMD", RMD,  STR1, 1,	"<sp> path-name" },
	{ "PWD",  PWD,  ARGS, 1,	"(return current directory)" },
	{ "XPWD", PWD,  ARGS, 1,	"(return current directory)" },
	{ "CDUP", CDUP, ARGS, 1,	"(change to parent directory)" },
	{ "XCUP", CDUP, ARGS, 1,	"(change to parent directory)" },
	{ "STOU", STOU, STR1, 1,	"<sp> file-name" },
	{ "SIZE", SIZE, OSTR, 1,	"<sp> path-name" },
	{ "MDTM", MDTM, OSTR, 1,	"<sp> path-name" },

	/* extensions from RFC2228 */
	{ "AUTH", AUTH,	STR1, 1,	"<sp> auth-type" },
	{ "ADAT", ADAT,	STR1, 1,	"<sp> auth-data" },
	{ "PBSZ", PBSZ,	ARGS, 1,	"<sp> buffer-size" },
	{ "PROT", PROT,	STR1, 1,	"<sp> prot-level" },
	{ "CCC",  CCC,	ARGS, 1,	"" },
	{ "MIC",  MIC,	STR1, 1,	"<sp> integrity command" },
	{ "CONF", CONF,	STR1, 1,	"<sp> confidentiality command" },
	{ "ENC",  ENC,	STR1, 1,	"<sp> privacy command" },

	/* RFC2389 */
	{ "FEAT", FEAT, ARGS, 1,	"" },
	{ "OPTS", OPTS, ARGS, 1,	"<sp> command [<sp> options]" },

	{ NULL,   0,    0,    0,	0 }
};

struct tab sitetab[] = {
	{ "UMASK", UMASK, ARGS, 1,	"[ <sp> umask ]" },
	{ "IDLE", IDLE, ARGS, 1,	"[ <sp> maximum-idle-time ]" },
	{ "CHMOD", CHMOD, NSTR, 1,	"<sp> mode <sp> file-name" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },

	{ "KAUTH", KAUTH, STR1, 1,	"<sp> principal [ <sp> ticket ]" },
	{ "KLIST", KLIST, ARGS, 1,	"(show ticket file)" },
	{ "KDESTROY", KDESTROY, ARGS, 1, "(destroy tickets)" },
	{ "KRBTKFILE", KRBTKFILE, STR1, 1, "<sp> ticket-file" },
	{ "AFSLOG", AFSLOG, OSTR, 1,	"[<sp> cell]" },

	{ "LOCATE", LOCATE, STR1, 1,	"<sp> globexpr" },
	{ "FIND", LOCATE, STR1, 1,	"<sp> globexpr" },

	{ "URL",  URL,  ARGS, 1,	"?" },

	{ NULL,   0,    0,    0,	0 }
};

static struct tab *
lookup(struct tab *p, char *cmd)
{

	for (; p->name != NULL; p++)
		if (strcmp(cmd, p->name) == 0)
			return (p);
	return (0);
}

/*
 * ftpd_getline - a hacked up version of fgets to ignore TELNET escape codes.
 */
char *
ftpd_getline(char *s, int n)
{
	int c;
	char *cs;

	cs = s;

	/* might still be data within the security MIC/CONF/ENC */
	if(ftp_command){
	    strlcpy(s, ftp_command, n);
	    if (debug)
		syslog(LOG_DEBUG, "command: %s", s);
	    return s;
	}
	while ((c = getc(stdin)) != EOF) {
		c &= 0377;
		if (c == IAC) {
		    if ((c = getc(stdin)) != EOF) {
			c &= 0377;
			switch (c) {
			case WILL:
			case WONT:
				c = getc(stdin);
				printf("%c%c%c", IAC, DONT, 0377&c);
				fflush(stdout);
				continue;
			case DO:
			case DONT:
				c = getc(stdin);
				printf("%c%c%c", IAC, WONT, 0377&c);
				fflush(stdout);
				continue;
			case IAC:
				break;
			default:
				continue;	/* ignore command */
			}
		    }
		}
		*cs++ = c;
		if (--n <= 0 || c == '\n')
			break;
	}
	if (c == EOF && cs == s)
		return (NULL);
	*cs++ = '\0';
	if (debug) {
		if (!guest && strncasecmp("pass ", s, 5) == 0) {
			/* Don't syslog passwords */
			syslog(LOG_DEBUG, "command: %.5s ???", s);
		} else {
			char *cp;
			int len;

			/* Don't syslog trailing CR-LF */
			len = strlen(s);
			cp = s + len - 1;
			while (cp >= s && (*cp == '\n' || *cp == '\r')) {
				--cp;
				--len;
			}
			syslog(LOG_DEBUG, "command: %.*s", len, s);
		}
	}
#ifdef XXX
	fprintf(stderr, "%s\n", s);
#endif
	return (s);
}

static RETSIGTYPE
toolong(int signo)
{

	reply(421,
	    "Timeout (%d seconds): closing control connection.",
	      ftpd_timeout);
	if (logging)
		syslog(LOG_INFO, "User %s timed out after %d seconds",
		    (pw ? pw -> pw_name : "unknown"), ftpd_timeout);
	dologout(1);
	SIGRETURN(0);
}

static int
yylex(void)
{
	static int cpos, state;
	char *cp, *cp2;
	struct tab *p;
	int n;
	char c;

	for (;;) {
		switch (state) {

		case CMD:
			hasyyerrored = 0;

			signal(SIGALRM, toolong);
			alarm((unsigned) ftpd_timeout);
			if (ftpd_getline(cbuf, sizeof(cbuf)-1) == NULL) {
				reply(221, "You could at least say goodbye.");
				dologout(0);
			}
			alarm(0);
#ifdef HAVE_SETPROCTITLE
			if (strncasecmp(cbuf, "PASS", 4) != 0)
				setproctitle("%s: %s", proctitle, cbuf);
#endif /* HAVE_SETPROCTITLE */
			if ((cp = strchr(cbuf, '\r'))) {
				*cp++ = '\n';
				*cp = '\0';
			}
			if ((cp = strpbrk(cbuf, " \n")))
				cpos = cp - cbuf;
			if (cpos == 0)
				cpos = 4;
			c = cbuf[cpos];
			cbuf[cpos] = '\0';
			strupr(cbuf);
			p = lookup(cmdtab, cbuf);
			cbuf[cpos] = c;
			if (p != 0) {
				if (p->implemented == 0) {
					nack(p->name);
					hasyyerrored = 1;
					break;
				}
				state = p->state;
				yylval.s = p->name;
				return (p->token);
			}
			break;

		case SITECMD:
			if (cbuf[cpos] == ' ') {
				cpos++;
				return (SP);
			}
			cp = &cbuf[cpos];
			if ((cp2 = strpbrk(cp, " \n")))
				cpos = cp2 - cbuf;
			c = cbuf[cpos];
			cbuf[cpos] = '\0';
			strupr(cp);
			p = lookup(sitetab, cp);
			cbuf[cpos] = c;
			if (p != 0) {
				if (p->implemented == 0) {
					state = CMD;
					nack(p->name);
					hasyyerrored = 1;
					break;
				}
				state = p->state;
				yylval.s = p->name;
				return (p->token);
			}
			state = CMD;
			break;

		case OSTR:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR1:
		case ZSTR1:
		dostr1:
			if (cbuf[cpos] == ' ') {
				cpos++;
				if(state == OSTR)
				    state = STR2;
				else
				    state++;
				return (SP);
			}
			break;

		case ZSTR2:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR2:
			cp = &cbuf[cpos];
			n = strlen(cp);
			cpos += n - 1;
			/*
			 * Make sure the string is nonempty and \n terminated.
			 */
			if (n > 1 && cbuf[cpos] == '\n') {
				cbuf[cpos] = '\0';
				yylval.s = copy(cp);
				cbuf[cpos] = '\n';
				state = ARGS;
				return (STRING);
			}
			break;

		case NSTR:
			if (cbuf[cpos] == ' ') {
				cpos++;
				return (SP);
			}
			if (isdigit((unsigned char)cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit((unsigned char)cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.i = atoi(cp);
				cbuf[cpos] = c;
				state = STR1;
				return (NUMBER);
			}
			state = STR1;
			goto dostr1;

		case ARGS:
			if (isdigit((unsigned char)cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit((unsigned char)cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.i = atoi(cp);
				cbuf[cpos] = c;
				return (NUMBER);
			}
			switch (cbuf[cpos++]) {

			case '\n':
				state = CMD;
				return (CRLF);

			case ' ':
				return (SP);

			case ',':
				return (COMMA);

			case 'A':
			case 'a':
				return (A);

			case 'B':
			case 'b':
				return (B);

			case 'C':
			case 'c':
				return (C);

			case 'E':
			case 'e':
				return (E);

			case 'F':
			case 'f':
				return (F);

			case 'I':
			case 'i':
				return (I);

			case 'L':
			case 'l':
				return (L);

			case 'N':
			case 'n':
				return (N);

			case 'P':
			case 'p':
				return (P);

			case 'R':
			case 'r':
				return (R);

			case 'S':
			case 's':
				return (S);

			case 'T':
			case 't':
				return (T);

			}
			break;

		default:
			fatal("Unknown state in scanner.");
		}
		yyerror(NULL);
		state = CMD;
		return (0);
	}
}

/* ARGSUSED */
void
yyerror(char *s)
{
	char *cp;

	if (hasyyerrored)
	    return;

	if ((cp = strchr(cbuf,'\n')))
		*cp = '\0';
	reply(500, "'%s': command not understood.", cbuf);
	hasyyerrored = 1;
}

static char *
copy(char *s)
{
	char *p;

	p = strdup(s);
	if (p == NULL)
		fatal("Ran out of memory.");
	return p;
}

static void
help(struct tab *ctab, char *s)
{
	struct tab *c;
	int width, NCMDS;
	char *t;
	char buf[1024];

	if (ctab == sitetab)
		t = "SITE ";
	else
		t = "";
	width = 0, NCMDS = 0;
	for (c = ctab; c->name != NULL; c++) {
		int len = strlen(c->name);

		if (len > width)
			width = len;
		NCMDS++;
	}
	width = (width + 8) &~ 7;
	if (s == 0) {
		int i, j, w;
		int columns, lines;

		lreply(214, "The following %scommands are recognized %s.",
		    t, "(* =>'s unimplemented)");
		columns = 76 / width;
		if (columns == 0)
			columns = 1;
		lines = (NCMDS + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
		    strlcpy (buf, "   ", sizeof(buf));
		    for (j = 0; j < columns; j++) {
			c = ctab + j * lines + i;
			snprintf (buf + strlen(buf),
				  sizeof(buf) - strlen(buf),
				  "%s%c",
				  c->name,
				  c->implemented ? ' ' : '*');
			if (c + lines >= &ctab[NCMDS])
			    break;
			w = strlen(c->name) + 1;
			while (w < width) {
			    strlcat (buf,
					     " ",
					     sizeof(buf));
			    w++;
			}
		    }
		    lreply(214, "%s", buf);
		}
		reply(214, "Direct comments to kth-krb-bugs@pdc.kth.se");
		return;
	}
	strupr(s);
	c = lookup(ctab, s);
	if (c == (struct tab *)0) {
		reply(502, "Unknown command %s.", s);
		return;
	}
	if (c->implemented)
		reply(214, "Syntax: %s%s %s", t, c->name, c->help);
	else
		reply(214, "%s%-*s\t%s; unimplemented.", t, width,
		    c->name, c->help);
}

static void
sizecmd(char *filename)
{
	switch (type) {
	case TYPE_L:
	case TYPE_I: {
		struct stat stbuf;
		if (stat(filename, &stbuf) < 0 || !S_ISREG(stbuf.st_mode))
			reply(550, "%s: not a plain file.", filename);
		else
			reply(213, "%lu", (unsigned long)stbuf.st_size);
		break;
	}
	case TYPE_A: {
		FILE *fin;
		int c;
		size_t count;
		struct stat stbuf;
		fin = fopen(filename, "r");
		if (fin == NULL) {
			perror_reply(550, filename);
			return;
		}
		if (fstat(fileno(fin), &stbuf) < 0 || !S_ISREG(stbuf.st_mode)) {
			reply(550, "%s: not a plain file.", filename);
			fclose(fin);
			return;
		}

		count = 0;
		while((c=getc(fin)) != EOF) {
			if (c == '\n')	/* will get expanded to \r\n */
				count++;
			count++;
		}
		fclose(fin);

		reply(213, "%lu", (unsigned long)count);
		break;
	}
	default:
		reply(504, "SIZE not implemented for Type %c.", "?AEIL"[type]);
	}
}
