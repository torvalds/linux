/*
 * Copyright (c) 1989, 1993
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
 *	@(#)ext.h	8.2 (Berkeley) 12/15/93
 */

/* $Id$ */

#ifndef __EXT_H__
#define __EXT_H__

/*
 * Telnet server variable declarations
 */
extern char	options[256];
extern char	do_dont_resp[256];
extern char	will_wont_resp[256];
extern int	flowmode;	/* current flow control state */
extern int	restartany;	/* restart output on any character state */
#ifdef DIAGNOSTICS
extern int	diagnostic;	/* telnet diagnostic capabilities */
#endif /* DIAGNOSTICS */
extern int	require_otp;
#ifdef AUTHENTICATION
extern int	auth_level;
#endif
extern const char *new_login;

extern slcfun	slctab[NSLC + 1];	/* slc mapping table */

extern char	terminaltype[41];

/*
 * I/O data buffers, pointers, and counters.
 */
extern char	ptyobuf[BUFSIZ+NETSLOP], *pfrontp, *pbackp;

extern char	netibuf[BUFSIZ], *netip;

extern char	netobuf[BUFSIZ+NETSLOP], *nfrontp, *nbackp;
extern char	*neturg;		/* one past last bye of urgent data */

extern int	pcc, ncc;

extern int	ourpty, net;
extern char	*line;
extern int	SYNCHing;		/* we are in TELNET SYNCH mode */

int telnet_net_write (unsigned char *str, int len);
void net_encrypt (void);
int telnet_spin (void);
char *telnet_getenv (const char *val);
char *telnet_gets (char *prompt, char *result, int length, int echo);
void get_slc_defaults (void);
void telrcv (void);
void send_do (int option, int init);
void willoption (int option);
void send_dont (int option, int init);
void wontoption (int option);
void send_will (int option, int init);
void dooption (int option);
void send_wont (int option, int init);
void dontoption (int option);
void suboption (void);
void doclientstat (void);
void send_status (void);
void init_termbuf (void);
void set_termbuf (void);
int spcset (int func, cc_t *valp, cc_t **valpp);
void set_utid (void);
int getpty (int *ptynum);
int tty_isecho (void);
int tty_flowmode (void);
int tty_restartany (void);
void tty_setecho (int on);
int tty_israw (void);
void tty_binaryin (int on);
void tty_binaryout (int on);
int tty_isbinaryin (void);
int tty_isbinaryout (void);
int tty_issofttab (void);
void tty_setsofttab (int on);
int tty_islitecho (void);
void tty_setlitecho (int on);
int tty_iscrnl (void);
void tty_tspeed (int val);
void tty_rspeed (int val);
void getptyslave (void);
int cleanopen (char *);
void startslave (const char *host, const char *, int autologin, char *autoname);
void init_env (void);
void start_login (const char *host, int autologin, char *name);
void cleanup (int sig);
int main (int argc, char **argv);
int getterminaltype (char *name, size_t);
void _gettermname (void);
int terminaltypeok (char *s);
void my_telnet (int f, int p, const char*, const char *, int, char*);
void interrupt (void);
void sendbrk (void);
void sendsusp (void);
void recv_ayt (void);
void doeof (void);
void flowstat (void);
void clientstat (int code, int parm1, int parm2);
int ttloop (void);
int stilloob (int s);
void ptyflush (void);
char *nextitem (char *current);
void netclear (void);
void netflush (void);
void writenet (const void *, size_t);
void fatal (int f, char *msg);
void fatalperror (int f, const char *msg);
void fatalperror_errno (int f, const char *msg, int error);
void edithost (char *pat, char *host);
void putstr (char *s);
void putchr (int cc);
void putf (char *cp, char *where);
void printoption (char *fmt, int option);
void printsub (int direction, unsigned char *pointer, size_t length);
void printdata (char *tag, char *ptr, size_t cnt);
int login_tty(int t);

#ifdef ENCRYPTION
extern void	(*encrypt_output) (unsigned char *, int);
extern int	(*decrypt_input) (int);
extern char	*nclearto;
#endif


/*
 * The following are some clocks used to decide how to interpret
 * the relationship between various variables.
 */

struct clocks_t{
    int
	system,			/* what the current time is */
	echotoggle,		/* last time user entered echo character */
	modenegotiated,		/* last time operating mode negotiated */
	didnetreceive,		/* last time we read data from network */
	ttypesubopt,		/* ttype subopt is received */
	tspeedsubopt,		/* tspeed subopt is received */
	environsubopt,		/* environ subopt is received */
	oenvironsubopt,		/* old environ subopt is received */
	xdisplocsubopt,		/* xdisploc subopt is received */
	baseline,		/* time started to do timed action */
	gotDM;			/* when did we last see a data mark */
};
extern struct clocks_t clocks;

extern int log_unauth;
extern int no_warn;

extern int def_tspeed, def_rspeed;
#ifdef	TIOCSWINSZ
extern int def_row, def_col;
#endif

#ifdef STREAMSPTY
extern int really_stream;
#endif

#ifndef USE_IM
# ifdef CRAY
#  define USE_IM "Cray UNICOS (%h) (%t)"
# endif
# ifdef _AIX
#  define USE_IM "%s %v.%r (%h) (%t)"
# endif
# ifndef USE_IM
#  define USE_IM "%s %r (%h) (%t)"
# endif
#endif

#define DEFAULT_IM "\r\n\r\n" USE_IM "\r\n\r\n\r\n"

#endif /* __EXT_H__ */
