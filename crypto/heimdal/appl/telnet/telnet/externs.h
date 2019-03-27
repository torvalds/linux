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
 *
 *	@(#)externs.h	8.3 (Berkeley) 5/30/95
 */

/* $Id$ */

#ifndef	BSD
# define BSD 43
#endif

#ifndef	_POSIX_VDISABLE
# ifdef sun
#  include <sys/param.h>	/* pick up VDISABLE definition, mayby */
# endif
# ifdef VDISABLE
#  define _POSIX_VDISABLE VDISABLE
# else
#  define _POSIX_VDISABLE ((cc_t)'\377')
# endif
#endif

#define	SUBBUFSIZE	256

extern int
    autologin,		/* Autologin enabled */
    skiprc,		/* Don't process the ~/.telnetrc file */
    eight,		/* use eight bit mode (binary in and/or out */
    binary,
    flushout,		/* flush output */
    connected,		/* Are we connected to the other side? */
    globalmode,		/* Mode tty should be in */
    telnetport,		/* Are we connected to the telnet port? */
    localflow,		/* Flow control handled locally */
    restartany,		/* If flow control, restart output on any character */
    localchars,		/* we recognize interrupt/quit */
    donelclchars,	/* the user has set "localchars" */
    showoptions,
    wantencryption,	/* User has requested encryption */
    net,		/* Network file descriptor */
    tin,		/* Terminal input file descriptor */
    tout,		/* Terminal output file descriptor */
    crlf,		/* Should '\r' be mapped to <CR><LF> (or <CR><NUL>)? */
    autoflush,		/* flush output when interrupting? */
    autosynch,		/* send interrupt characters with SYNCH? */
    SYNCHing,		/* Is the stream in telnet SYNCH mode? */
    donebinarytoggle,	/* the user has put us in binary */
    dontlecho,		/* do we suppress local echoing right now? */
    crmod,
    netdata,		/* Print out network data flow */
    prettydump,		/* Print "netdata" output in user readable format */
    termdata,		/* Print out terminal data flow */
    debug;		/* Debug level */

extern int intr_happened, intr_waiting;	/* for interrupt handling */

extern cc_t escape;	/* Escape to command mode */
extern cc_t rlogin;	/* Rlogin mode escape character */
#ifdef	KLUDGELINEMODE
extern cc_t echoc;	/* Toggle local echoing */
#endif

extern char
    *prompt;		/* Prompt for command. */

extern char
    doopt[],
    dont[],
    will[],
    wont[],
    do_dont_resp[],
    will_wont_resp[],
    options[],		/* All the little options */
    *hostname;		/* Who are we connected to? */
#if	defined(ENCRYPTION)
extern void (*encrypt_output) (unsigned char *, int);
extern int (*decrypt_input) (int);
#endif

/*
 * We keep track of each side of the option negotiation.
 */

#define	MY_STATE_WILL		0x01
#define	MY_WANT_STATE_WILL	0x02
#define	MY_STATE_DO		0x04
#define	MY_WANT_STATE_DO	0x08

/*
 * Macros to check the current state of things
 */

#define	my_state_is_do(opt)		(options[opt]&MY_STATE_DO)
#define	my_state_is_will(opt)		(options[opt]&MY_STATE_WILL)
#define my_want_state_is_do(opt)	(options[opt]&MY_WANT_STATE_DO)
#define my_want_state_is_will(opt)	(options[opt]&MY_WANT_STATE_WILL)

#define	my_state_is_dont(opt)		(!my_state_is_do(opt))
#define	my_state_is_wont(opt)		(!my_state_is_will(opt))
#define my_want_state_is_dont(opt)	(!my_want_state_is_do(opt))
#define my_want_state_is_wont(opt)	(!my_want_state_is_will(opt))

#define	set_my_state_do(opt)		{options[opt] |= MY_STATE_DO;}
#define	set_my_state_will(opt)		{options[opt] |= MY_STATE_WILL;}
#define	set_my_want_state_do(opt)	{options[opt] |= MY_WANT_STATE_DO;}
#define	set_my_want_state_will(opt)	{options[opt] |= MY_WANT_STATE_WILL;}

#define	set_my_state_dont(opt)		{options[opt] &= ~MY_STATE_DO;}
#define	set_my_state_wont(opt)		{options[opt] &= ~MY_STATE_WILL;}
#define	set_my_want_state_dont(opt)	{options[opt] &= ~MY_WANT_STATE_DO;}
#define	set_my_want_state_wont(opt)	{options[opt] &= ~MY_WANT_STATE_WILL;}

/*
 * Make everything symmetrical
 */

#define	HIS_STATE_WILL			MY_STATE_DO
#define	HIS_WANT_STATE_WILL		MY_WANT_STATE_DO
#define HIS_STATE_DO			MY_STATE_WILL
#define HIS_WANT_STATE_DO		MY_WANT_STATE_WILL

#define	his_state_is_do			my_state_is_will
#define	his_state_is_will		my_state_is_do
#define his_want_state_is_do		my_want_state_is_will
#define his_want_state_is_will		my_want_state_is_do

#define	his_state_is_dont		my_state_is_wont
#define	his_state_is_wont		my_state_is_dont
#define his_want_state_is_dont		my_want_state_is_wont
#define his_want_state_is_wont		my_want_state_is_dont

#define	set_his_state_do		set_my_state_will
#define	set_his_state_will		set_my_state_do
#define	set_his_want_state_do		set_my_want_state_will
#define	set_his_want_state_will		set_my_want_state_do

#define	set_his_state_dont		set_my_state_wont
#define	set_his_state_wont		set_my_state_dont
#define	set_his_want_state_dont		set_my_want_state_wont
#define	set_his_want_state_wont		set_my_want_state_dont


extern FILE
    *NetTrace;		/* Where debugging output goes */
extern char
    NetTraceFile[];	/* Name of file where debugging output goes */
extern void
    SetNetTrace (char *);	/* Function to change where debugging goes */

extern jmp_buf
    peerdied,
    toplevel;		/* For error conditions. */

int Scheduler(int);
extern int scheduler_lockout_tty;


/* authenc.c */

#if	defined(AUTHENTICATION) || defined(ENCRYPTION)
int telnet_net_write(unsigned char *str, int len);
void net_encrypt(void);
int telnet_spin(void);
char *telnet_getenv(const char *val);
char *telnet_gets(char *prompt, char *result, int length, int echo);
#endif

/* commands.c */

struct env_lst *env_define (unsigned char *, unsigned char *);
struct env_lst *env_find(unsigned char *var);
void env_init (void);
void env_undefine (unsigned char *);
void env_export (unsigned char *);
void env_unexport (unsigned char *);
void env_send (unsigned char *);
void env_list (void);
unsigned char * env_default(int init, int welldefined);
unsigned char * env_getvalue(unsigned char *var);

void set_escape_char(char *s);
int sourceroute(struct addrinfo *ai, char *arg, char **cpp,
		int *prototp, int *optp);

#if	defined(AUTHENTICATION)
int auth_enable (char *);
int auth_disable (char *);
int auth_status (void);
#endif

#if defined(ENCRYPTION)
int 	EncryptEnable (char *, char *);
int 	EncryptDisable (char *, char *);
int 	EncryptType (char *, char *);
int 	EncryptStart (char *);
int 	EncryptStartInput (void);
int 	EncryptStartOutput (void);
int 	EncryptStop (char *);
int 	EncryptStopInput (void);
int 	EncryptStopOutput (void);
int 	EncryptStatus (void);
#endif

#ifdef SIGINFO
RETSIGTYPE ayt_status(int);
#endif
int tn(int argc, char **argv);
void command(int top, char *tbuf, int cnt);

/* main.c */

void tninit(void);
void set_forward_options(void);

/* network.c */

void init_network(void);
int stilloob(void);
void setneturg(void);
int netflush(void);

/* sys_bsd.c */

void init_sys(void);
int TerminalWrite(char *buf, int n);
int TerminalRead(unsigned char *buf, int n);
int TerminalAutoFlush(void);
int TerminalSpecialChars(int c);
void TerminalFlushOutput(void);
void TerminalSaveState(void);
void TerminalDefaultChars(void);
void TerminalNewMode(int f);
cc_t *tcval(int func);
void TerminalSpeeds(long *input_speed, long *output_speed);
int TerminalWindowSize(long *rows, long *cols);
int NetClose(int fd);
void NetNonblockingIO(int fd, int onoff);
int process_rings(int netin, int netout, int netex, int ttyin, int ttyout,
		  int poll);

/* telnet.c */

void init_telnet(void);

void tel_leave_binary(int rw);
void tel_enter_binary(int rw);
int opt_welldefined(char *ep);
int telrcv(void);
int rlogin_susp(void);
void intp(void);
void sendbrk(void);
void sendabort(void);
void sendsusp(void);
void sendeof(void);
void sendayt(void);

void xmitAO(void);
void xmitEL(void);
void xmitEC(void);


void     Dump (char, unsigned char *, int);
void     printoption (char *, int, int);
void     sendnaws (void);
void     setconnmode (int);
void     setcommandmode (void);
void     setneturg (void);
void     sys_telnet_init (void);
void     my_telnet (char *);
void     tel_enter_binary (int);
void     TerminalFlushOutput (void);
void     TerminalNewMode (int);
void     TerminalRestoreState (void);
void     TerminalSaveState (void);
void     willoption (int);
void     wontoption (int);


void     send_do (int, int);
void     send_dont (int, int);
void     send_will (int, int);
void     send_wont (int, int);

void     lm_will (unsigned char *, int);
void     lm_wont (unsigned char *, int);
void     lm_do (unsigned char *, int);
void     lm_dont (unsigned char *, int);
void     lm_mode (unsigned char *, int, int);

void     slc_init (void);
void     slcstate (void);
void     slc_mode_export (void);
void     slc_mode_import (int);
void     slc_import (int);
void     slc_export (void);
void     slc (unsigned char *, int);
void     slc_check (void);
void     slc_start_reply (void);
void     slc_add_reply (unsigned char, unsigned char, cc_t);
void     slc_end_reply (void);
int	 slc_update (void);

void     env_opt (unsigned char *, int);
void     env_opt_start (void);
void     env_opt_start_info (void);
void     env_opt_add (unsigned char *);
void     env_opt_end (int);

unsigned char     *env_default (int, int);
unsigned char     *env_getvalue (unsigned char *);

int get_status (void);
int dosynch (void);

cc_t *tcval (int);

int quit (void);

/* terminal.c */

void init_terminal(void);
int ttyflush(int drop);
int getconnmode(void);

/* utilities.c */

int SetSockOpt(int fd, int level, int option, int yesno);
void SetNetTrace(char *file);
void Dump(char direction, unsigned char *buffer, int length);
void printoption(char *direction, int cmd, int option);
void optionstatus(void);
void printsub(int direction, unsigned char *pointer, size_t length);
void EmptyTerminal(void);
void SetForExit(void);
void Exit(int returnCode);
void ExitString(char *string, int returnCode);

extern struct	termios new_tc;

# define termEofChar		new_tc.c_cc[VEOF]
# define termEraseChar		new_tc.c_cc[VERASE]
# define termIntChar		new_tc.c_cc[VINTR]
# define termKillChar		new_tc.c_cc[VKILL]
# define termQuitChar		new_tc.c_cc[VQUIT]

# ifndef	VSUSP
extern cc_t termSuspChar;
# else
#  define termSuspChar		new_tc.c_cc[VSUSP]
# endif
# if	defined(VFLUSHO) && !defined(VDISCARD)
#  define VDISCARD VFLUSHO
# endif
# ifndef	VDISCARD
extern cc_t termFlushChar;
# else
#  define termFlushChar		new_tc.c_cc[VDISCARD]
# endif
# ifndef VWERASE
extern cc_t termWerasChar;
# else
#  define termWerasChar		new_tc.c_cc[VWERASE]
# endif
# ifndef	VREPRINT
extern cc_t termRprntChar;
# else
#  define termRprntChar		new_tc.c_cc[VREPRINT]
# endif
# ifndef	VLNEXT
extern cc_t termLiteralNextChar;
# else
#  define termLiteralNextChar	new_tc.c_cc[VLNEXT]
# endif
# ifndef	VSTART
extern cc_t termStartChar;
# else
#  define termStartChar		new_tc.c_cc[VSTART]
# endif
# ifndef	VSTOP
extern cc_t termStopChar;
# else
#  define termStopChar		new_tc.c_cc[VSTOP]
# endif
# ifndef	VEOL
extern cc_t termForw1Char;
# else
#  define termForw1Char		new_tc.c_cc[VEOL]
# endif
# ifndef	VEOL2
extern cc_t termForw2Char;
# else
#  define termForw2Char		new_tc.c_cc[VEOL]
# endif
# ifndef	VSTATUS
extern cc_t termAytChar;
#else
#  define termAytChar		new_tc.c_cc[VSTATUS]
#endif

/* Ring buffer structures which are shared */

extern Ring
    netoring,
    netiring,
    ttyoring,
    ttyiring;

extern int resettermname;
extern int linemode;
#ifdef KLUDGELINEMODE
extern int kludgelinemode;
#endif
extern int want_status_response;
