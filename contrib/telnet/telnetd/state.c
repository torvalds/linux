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
 */

#if 0
#ifndef lint
static const char sccsid[] = "@(#)state.c	8.5 (Berkeley) 5/30/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdarg.h>
#include "telnetd.h"
#ifdef	AUTHENTICATION
#include <libtelnet/auth.h>
#endif
#ifdef	ENCRYPTION
#include <libtelnet/encrypt.h>
#endif

unsigned char	doopt[] = { IAC, DO, '%', 'c', 0 };
unsigned char	dont[] = { IAC, DONT, '%', 'c', 0 };
unsigned char	will[] = { IAC, WILL, '%', 'c', 0 };
unsigned char	wont[] = { IAC, WONT, '%', 'c', 0 };
int	not42 = 1;

/*
 * Buffer for sub-options, and macros
 * for suboptions buffer manipulations
 */
unsigned char subbuffer[512], *subpointer= subbuffer, *subend= subbuffer;

#define	SB_CLEAR()	subpointer = subbuffer
#define	SB_TERM()	{ subend = subpointer; SB_CLEAR(); }
#define	SB_ACCUM(c)	if (subpointer < (subbuffer+sizeof subbuffer)) { \
				*subpointer++ = (c); \
			}
#define	SB_GET()	((*subpointer++)&0xff)
#define	SB_EOF()	(subpointer >= subend)
#define	SB_LEN()	(subend - subpointer)

#ifdef	ENV_HACK
unsigned char *subsave;
#define SB_SAVE()	subsave = subpointer;
#define	SB_RESTORE()	subpointer = subsave;
#endif


/*
 * State for recv fsm
 */
#define	TS_DATA		0	/* base state */
#define	TS_IAC		1	/* look for double IAC's */
#define	TS_CR		2	/* CR-LF ->'s CR */
#define	TS_SB		3	/* throw away begin's... */
#define	TS_SE		4	/* ...end's (suboption negotiation) */
#define	TS_WILL		5	/* will option negotiation */
#define	TS_WONT		6	/* wont " */
#define	TS_DO		7	/* do " */
#define	TS_DONT		8	/* dont " */

static void doclientstat(void);

void
telrcv(void)
{
	int c;
	static int state = TS_DATA;

	while (ncc > 0) {
		if ((&ptyobuf[BUFSIZ] - pfrontp) < 2)
			break;
		c = *netip++ & 0377, ncc--;
#ifdef	ENCRYPTION
		if (decrypt_input)
			c = (*decrypt_input)(c);
#endif	/* ENCRYPTION */
		switch (state) {

		case TS_CR:
			state = TS_DATA;
			/* Strip off \n or \0 after a \r */
			if ((c == 0) || (c == '\n')) {
				break;
			}
			/* FALLTHROUGH */

		case TS_DATA:
			if (c == IAC) {
				state = TS_IAC;
				break;
			}
			/*
			 * We now map \r\n ==> \r for pragmatic reasons.
			 * Many client implementations send \r\n when
			 * the user hits the CarriageReturn key.
			 *
			 * We USED to map \r\n ==> \n, since \r\n says
			 * that we want to be in column 1 of the next
			 * printable line, and \n is the standard
			 * unix way of saying that (\r is only good
			 * if CRMOD is set, which it normally is).
			 */
			if ((c == '\r') && his_state_is_wont(TELOPT_BINARY)) {
				int nc = *netip;
#ifdef	ENCRYPTION
				if (decrypt_input)
					nc = (*decrypt_input)(nc & 0xff);
#endif	/* ENCRYPTION */
#ifdef	LINEMODE
				/*
				 * If we are operating in linemode,
				 * convert to local end-of-line.
				 */
				if (linemode && (ncc > 0) && (('\n' == nc) ||
					 ((0 == nc) && tty_iscrnl())) ) {
					netip++; ncc--;
					c = '\n';
				} else
#endif
				{
#ifdef	ENCRYPTION
					if (decrypt_input)
						(void)(*decrypt_input)(-1);
#endif	/* ENCRYPTION */
					state = TS_CR;
				}
			}
			*pfrontp++ = c;
			break;

		case TS_IAC:
gotiac:			switch (c) {

			/*
			 * Send the process on the pty side an
			 * interrupt.  Do this with a NULL or
			 * interrupt char; depending on the tty mode.
			 */
			case IP:
				DIAG(TD_OPTIONS,
					printoption("td: recv IAC", c));
				interrupt();
				break;

			case BREAK:
				DIAG(TD_OPTIONS,
					printoption("td: recv IAC", c));
				sendbrk();
				break;

			/*
			 * Are You There?
			 */
			case AYT:
				DIAG(TD_OPTIONS,
					printoption("td: recv IAC", c));
				recv_ayt();
				break;

			/*
			 * Abort Output
			 */
			case AO:
			    {
				DIAG(TD_OPTIONS,
					printoption("td: recv IAC", c));
				ptyflush();	/* half-hearted */
				init_termbuf();

				if (slctab[SLC_AO].sptr &&
				    *slctab[SLC_AO].sptr != (cc_t)(_POSIX_VDISABLE)) {
				    *pfrontp++ =
					(unsigned char)*slctab[SLC_AO].sptr;
				}

				netclear();	/* clear buffer back */
				output_data("%c%c", IAC, DM);
				neturg = nfrontp-1; /* off by one XXX */
				DIAG(TD_OPTIONS,
					printoption("td: send IAC", DM));
				break;
			    }

			/*
			 * Erase Character and
			 * Erase Line
			 */
			case EC:
			case EL:
			    {
				cc_t ch;

				DIAG(TD_OPTIONS,
					printoption("td: recv IAC", c));
				ptyflush();	/* half-hearted */
				init_termbuf();
				if (c == EC)
					ch = *slctab[SLC_EC].sptr;
				else
					ch = *slctab[SLC_EL].sptr;
				if (ch != (cc_t)(_POSIX_VDISABLE))
					*pfrontp++ = (unsigned char)ch;
				break;
			    }

			/*
			 * Check for urgent data...
			 */
			case DM:
				DIAG(TD_OPTIONS,
					printoption("td: recv IAC", c));
				SYNCHing = stilloob(net);
				settimer(gotDM);
				break;


			/*
			 * Begin option subnegotiation...
			 */
			case SB:
				state = TS_SB;
				SB_CLEAR();
				continue;

			case WILL:
				state = TS_WILL;
				continue;

			case WONT:
				state = TS_WONT;
				continue;

			case DO:
				state = TS_DO;
				continue;

			case DONT:
				state = TS_DONT;
				continue;
			case EOR:
				if (his_state_is_will(TELOPT_EOR))
					doeof();
				break;

			/*
			 * Handle RFC 10xx Telnet linemode option additions
			 * to command stream (EOF, SUSP, ABORT).
			 */
			case xEOF:
				doeof();
				break;

			case SUSP:
				sendsusp();
				break;

			case ABORT:
				sendbrk();
				break;

			case IAC:
				*pfrontp++ = c;
				break;
			}
			state = TS_DATA;
			break;

		case TS_SB:
			if (c == IAC) {
				state = TS_SE;
			} else {
				SB_ACCUM(c);
			}
			break;

		case TS_SE:
			if (c != SE) {
				if (c != IAC) {
					/*
					 * bad form of suboption negotiation.
					 * handle it in such a way as to avoid
					 * damage to local state.  Parse
					 * suboption buffer found so far,
					 * then treat remaining stream as
					 * another command sequence.
					 */

					/* for DIAGNOSTICS */
					SB_ACCUM(IAC);
					SB_ACCUM(c);
					subpointer -= 2;

					SB_TERM();
					suboption();
					state = TS_IAC;
					goto gotiac;
				}
				SB_ACCUM(c);
				state = TS_SB;
			} else {
				/* for DIAGNOSTICS */
				SB_ACCUM(IAC);
				SB_ACCUM(SE);
				subpointer -= 2;

				SB_TERM();
				suboption();	/* handle sub-option */
				state = TS_DATA;
			}
			break;

		case TS_WILL:
			willoption(c);
			state = TS_DATA;
			continue;

		case TS_WONT:
			wontoption(c);
			state = TS_DATA;
			continue;

		case TS_DO:
			dooption(c);
			state = TS_DATA;
			continue;

		case TS_DONT:
			dontoption(c);
			state = TS_DATA;
			continue;

		default:
			syslog(LOG_ERR, "panic state=%d", state);
			printf("telnetd: panic state=%d\n", state);
			exit(1);
		}
	}
}  /* end of telrcv */

/*
 * The will/wont/do/dont state machines are based on Dave Borman's
 * Telnet option processing state machine.
 *
 * These correspond to the following states:
 *	my_state = the last negotiated state
 *	want_state = what I want the state to go to
 *	want_resp = how many requests I have sent
 * All state defaults are negative, and resp defaults to 0.
 *
 * When initiating a request to change state to new_state:
 *
 * if ((want_resp == 0 && new_state == my_state) || want_state == new_state) {
 *	do nothing;
 * } else {
 *	want_state = new_state;
 *	send new_state;
 *	want_resp++;
 * }
 *
 * When receiving new_state:
 *
 * if (want_resp) {
 *	want_resp--;
 *	if (want_resp && (new_state == my_state))
 *		want_resp--;
 * }
 * if ((want_resp == 0) && (new_state != want_state)) {
 *	if (ok_to_switch_to new_state)
 *		want_state = new_state;
 *	else
 *		want_resp++;
 *	send want_state;
 * }
 * my_state = new_state;
 *
 * Note that new_state is implied in these functions by the function itself.
 * will and do imply positive new_state, wont and dont imply negative.
 *
 * Finally, there is one catch.  If we send a negative response to a
 * positive request, my_state will be the positive while want_state will
 * remain negative.  my_state will revert to negative when the negative
 * acknowlegment arrives from the peer.  Thus, my_state generally tells
 * us not only the last negotiated state, but also tells us what the peer
 * wants to be doing as well.  It is important to understand this difference
 * as we may wish to be processing data streams based on our desired state
 * (want_state) or based on what the peer thinks the state is (my_state).
 *
 * This all works fine because if the peer sends a positive request, the data
 * that we receive prior to negative acknowlegment will probably be affected
 * by the positive state, and we can process it as such (if we can; if we
 * can't then it really doesn't matter).  If it is that important, then the
 * peer probably should be buffering until this option state negotiation
 * is complete.
 *
 */
void
send_do(int option, int init)
{
	if (init) {
		if ((do_dont_resp[option] == 0 && his_state_is_will(option)) ||
		    his_want_state_is_will(option))
			return;
		/*
		 * Special case for TELOPT_TM:  We send a DO, but pretend
		 * that we sent a DONT, so that we can send more DOs if
		 * we want to.
		 */
		if (option == TELOPT_TM)
			set_his_want_state_wont(option);
		else
			set_his_want_state_will(option);
		do_dont_resp[option]++;
	}
	output_data((const char *)doopt, option);

	DIAG(TD_OPTIONS, printoption("td: send do", option));
}

void
willoption(int option)
{
	int changeok = 0;
	void (*func)(void) = 0;

	/*
	 * process input from peer.
	 */

	DIAG(TD_OPTIONS, printoption("td: recv will", option));

	if (do_dont_resp[option]) {
		do_dont_resp[option]--;
		if (do_dont_resp[option] && his_state_is_will(option))
			do_dont_resp[option]--;
	}
	if (do_dont_resp[option] == 0) {
	    if (his_want_state_is_wont(option)) {
		switch (option) {

		case TELOPT_BINARY:
			init_termbuf();
			tty_binaryin(1);
			set_termbuf();
			changeok++;
			break;

		case TELOPT_ECHO:
			/*
			 * See comments below for more info.
			 */
			not42 = 0;	/* looks like a 4.2 system */
			break;

		case TELOPT_TM:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
			/*
			 * This telnetd implementation does not really
			 * support timing marks, it just uses them to
			 * support the kludge linemode stuff.  If we
			 * receive a will or wont TM in response to our
			 * do TM request that may have been sent to
			 * determine kludge linemode support, process
			 * it, otherwise TM should get a negative
			 * response back.
			 */
			/*
			 * Handle the linemode kludge stuff.
			 * If we are not currently supporting any
			 * linemode at all, then we assume that this
			 * is the client telling us to use kludge
			 * linemode in response to our query.  Set the
			 * linemode type that is to be supported, note
			 * that the client wishes to use linemode, and
			 * eat the will TM as though it never arrived.
			 */
			if (lmodetype < KLUDGE_LINEMODE) {
				lmodetype = KLUDGE_LINEMODE;
				clientstat(TELOPT_LINEMODE, WILL, 0);
				send_wont(TELOPT_SGA, 1);
			} else if (lmodetype == NO_AUTOKLUDGE) {
				lmodetype = KLUDGE_OK;
			}
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
			/*
			 * We never respond to a WILL TM, and
			 * we leave the state WONT.
			 */
			return;

		case TELOPT_LFLOW:
			/*
			 * If we are going to support flow control
			 * option, then don't worry peer that we can't
			 * change the flow control characters.
			 */
			slctab[SLC_XON].defset.flag &= ~SLC_LEVELBITS;
			slctab[SLC_XON].defset.flag |= SLC_DEFAULT;
			slctab[SLC_XOFF].defset.flag &= ~SLC_LEVELBITS;
			slctab[SLC_XOFF].defset.flag |= SLC_DEFAULT;
		case TELOPT_TTYPE:
		case TELOPT_SGA:
		case TELOPT_NAWS:
		case TELOPT_TSPEED:
		case TELOPT_XDISPLOC:
		case TELOPT_NEW_ENVIRON:
		case TELOPT_OLD_ENVIRON:
			changeok++;
			break;

#ifdef	LINEMODE
		case TELOPT_LINEMODE:
# ifdef	KLUDGELINEMODE
			/*
			 * Note client's desire to use linemode.
			 */
			lmodetype = REAL_LINEMODE;
# endif	/* KLUDGELINEMODE */
			func = doclientstat;
			changeok++;
			break;
#endif	/* LINEMODE */

#ifdef	AUTHENTICATION
		case TELOPT_AUTHENTICATION:
			if (auth_level >= 0) {
				func = auth_request;
				changeok++;
			}
			break;
#endif

#ifdef	ENCRYPTION
		case TELOPT_ENCRYPT:
			func = encrypt_send_support;
			changeok++;
			break;
#endif	/* ENCRYPTION */

		default:
			break;
		}
		if (changeok) {
			set_his_want_state_will(option);
			send_do(option, 0);
		} else {
			do_dont_resp[option]++;
			send_dont(option, 0);
		}
	    } else {
		/*
		 * Option processing that should happen when
		 * we receive conformation of a change in
		 * state that we had requested.
		 */
		switch (option) {
		case TELOPT_ECHO:
			not42 = 0;	/* looks like a 4.2 system */
			/*
			 * Egads, he responded "WILL ECHO".  Turn
			 * it off right now!
			 */
			send_dont(option, 1);
			/*
			 * "WILL ECHO".  Kludge upon kludge!
			 * A 4.2 client is now echoing user input at
			 * the tty.  This is probably undesireable and
			 * it should be stopped.  The client will
			 * respond WONT TM to the DO TM that we send to
			 * check for kludge linemode.  When the WONT TM
			 * arrives, linemode will be turned off and a
			 * change propogated to the pty.  This change
			 * will cause us to process the new pty state
			 * in localstat(), which will notice that
			 * linemode is off and send a WILL ECHO
			 * so that we are properly in character mode and
			 * all is well.
			 */
			break;
#ifdef	LINEMODE
		case TELOPT_LINEMODE:
# ifdef	KLUDGELINEMODE
			/*
			 * Note client's desire to use linemode.
			 */
			lmodetype = REAL_LINEMODE;
# endif	/* KLUDGELINEMODE */
			func = doclientstat;
			break;
#endif	/* LINEMODE */

#ifdef	AUTHENTICATION
		case TELOPT_AUTHENTICATION:
			func = auth_request;
			break;
#endif

#ifdef	ENCRYPTION
		case TELOPT_ENCRYPT:
			func = encrypt_send_support;
			break;
#endif	/* ENCRYPTION */
		case TELOPT_LFLOW:
			func = flowstat;
			break;
		}
	    }
	}
	set_his_state_will(option);
	if (func)
		(*func)();
}  /* end of willoption */

void
send_dont(int option, int init)
{
	if (init) {
		if ((do_dont_resp[option] == 0 && his_state_is_wont(option)) ||
		    his_want_state_is_wont(option))
			return;
		set_his_want_state_wont(option);
		do_dont_resp[option]++;
	}
	output_data((const char *)dont, option);

	DIAG(TD_OPTIONS, printoption("td: send dont", option));
}

void
wontoption(int option)
{
	/*
	 * Process client input.
	 */

	DIAG(TD_OPTIONS, printoption("td: recv wont", option));

	if (do_dont_resp[option]) {
		do_dont_resp[option]--;
		if (do_dont_resp[option] && his_state_is_wont(option))
			do_dont_resp[option]--;
	}
	if (do_dont_resp[option] == 0) {
	    if (his_want_state_is_will(option)) {
		/* it is always ok to change to negative state */
		switch (option) {
		case TELOPT_ECHO:
			not42 = 1; /* doesn't seem to be a 4.2 system */
			break;

		case TELOPT_BINARY:
			init_termbuf();
			tty_binaryin(0);
			set_termbuf();
			break;

#ifdef	LINEMODE
		case TELOPT_LINEMODE:
# ifdef	KLUDGELINEMODE
			/*
			 * If real linemode is supported, then client is
			 * asking to turn linemode off.
			 */
			if (lmodetype != REAL_LINEMODE)
				break;
			lmodetype = KLUDGE_LINEMODE;
# endif	/* KLUDGELINEMODE */
			clientstat(TELOPT_LINEMODE, WONT, 0);
			break;
#endif	/* LINEMODE */

		case TELOPT_TM:
			/*
			 * If we get a WONT TM, and had sent a DO TM,
			 * don't respond with a DONT TM, just leave it
			 * as is.  Short circut the state machine to
			 * achive this.
			 */
			set_his_want_state_wont(TELOPT_TM);
			return;

		case TELOPT_LFLOW:
			/*
			 * If we are not going to support flow control
			 * option, then let peer know that we can't
			 * change the flow control characters.
			 */
			slctab[SLC_XON].defset.flag &= ~SLC_LEVELBITS;
			slctab[SLC_XON].defset.flag |= SLC_CANTCHANGE;
			slctab[SLC_XOFF].defset.flag &= ~SLC_LEVELBITS;
			slctab[SLC_XOFF].defset.flag |= SLC_CANTCHANGE;
			break;

#ifdef	AUTHENTICATION
		case TELOPT_AUTHENTICATION:
			auth_finished(0, AUTH_REJECT);
			break;
#endif

		/*
		 * For options that we might spin waiting for
		 * sub-negotiation, if the client turns off the
		 * option rather than responding to the request,
		 * we have to treat it here as if we got a response
		 * to the sub-negotiation, (by updating the timers)
		 * so that we'll break out of the loop.
		 */
		case TELOPT_TTYPE:
			settimer(ttypesubopt);
			break;

		case TELOPT_TSPEED:
			settimer(tspeedsubopt);
			break;

		case TELOPT_XDISPLOC:
			settimer(xdisplocsubopt);
			break;

		case TELOPT_OLD_ENVIRON:
			settimer(oenvironsubopt);
			break;

		case TELOPT_NEW_ENVIRON:
			settimer(environsubopt);
			break;

		default:
			break;
		}
		set_his_want_state_wont(option);
		if (his_state_is_will(option))
			send_dont(option, 0);
	    } else {
		switch (option) {
		case TELOPT_TM:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
			if (lmodetype < NO_AUTOKLUDGE) {
				lmodetype = NO_LINEMODE;
				clientstat(TELOPT_LINEMODE, WONT, 0);
				send_will(TELOPT_SGA, 1);
				send_will(TELOPT_ECHO, 1);
			}
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
			break;

#ifdef AUTHENTICATION
		case TELOPT_AUTHENTICATION:
			auth_finished(0, AUTH_REJECT);
			break;
#endif
		default:
			break;
		}
	    }
	}
	set_his_state_wont(option);

}  /* end of wontoption */

void
send_will(int option, int init)
{
	if (init) {
		if ((will_wont_resp[option] == 0 && my_state_is_will(option))||
		    my_want_state_is_will(option))
			return;
		set_my_want_state_will(option);
		will_wont_resp[option]++;
	}
	output_data((const char *)will, option);

	DIAG(TD_OPTIONS, printoption("td: send will", option));
}

#if	!defined(LINEMODE) || !defined(KLUDGELINEMODE)
/*
 * When we get a DONT SGA, we will try once to turn it
 * back on.  If the other side responds DONT SGA, we
 * leave it at that.  This is so that when we talk to
 * clients that understand KLUDGELINEMODE but not LINEMODE,
 * we'll keep them in char-at-a-time mode.
 */
int turn_on_sga = 0;
#endif

void
dooption(int option)
{
	int changeok = 0;

	/*
	 * Process client input.
	 */

	DIAG(TD_OPTIONS, printoption("td: recv do", option));

	if (will_wont_resp[option]) {
		will_wont_resp[option]--;
		if (will_wont_resp[option] && my_state_is_will(option))
			will_wont_resp[option]--;
	}
	if ((will_wont_resp[option] == 0) && (my_want_state_is_wont(option))) {
		switch (option) {
		case TELOPT_ECHO:
#ifdef	LINEMODE
# ifdef	KLUDGELINEMODE
			if (lmodetype == NO_LINEMODE)
# else
			if (his_state_is_wont(TELOPT_LINEMODE))
# endif
#endif
			{
				init_termbuf();
				tty_setecho(1);
				set_termbuf();
			}
			changeok++;
			break;

		case TELOPT_BINARY:
			init_termbuf();
			tty_binaryout(1);
			set_termbuf();
			changeok++;
			break;

		case TELOPT_SGA:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
			/*
			 * If kludge linemode is in use, then we must
			 * process an incoming do SGA for linemode
			 * purposes.
			 */
			if (lmodetype == KLUDGE_LINEMODE) {
				/*
				 * Receipt of "do SGA" in kludge
				 * linemode is the peer asking us to
				 * turn off linemode.  Make note of
				 * the request.
				 */
				clientstat(TELOPT_LINEMODE, WONT, 0);
				/*
				 * If linemode did not get turned off
				 * then don't tell peer that we did.
				 * Breaking here forces a wont SGA to
				 * be returned.
				 */
				if (linemode)
					break;
			}
#else
			turn_on_sga = 0;
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
			changeok++;
			break;

		case TELOPT_STATUS:
			changeok++;
			break;

		case TELOPT_TM:
			/*
			 * Special case for TM.  We send a WILL, but
			 * pretend we sent a WONT.
			 */
			send_will(option, 0);
			set_my_want_state_wont(option);
			set_my_state_wont(option);
			return;

		case TELOPT_LOGOUT:
			/*
			 * When we get a LOGOUT option, respond
			 * with a WILL LOGOUT, make sure that
			 * it gets written out to the network,
			 * and then just go away...
			 */
			set_my_want_state_will(TELOPT_LOGOUT);
			send_will(TELOPT_LOGOUT, 0);
			set_my_state_will(TELOPT_LOGOUT);
			(void)netflush();
			cleanup(0);
			/* NOT REACHED */
			break;

#ifdef	ENCRYPTION
		case TELOPT_ENCRYPT:
			changeok++;
			break;
#endif	/* ENCRYPTION */
		case TELOPT_LINEMODE:
		case TELOPT_TTYPE:
		case TELOPT_NAWS:
		case TELOPT_TSPEED:
		case TELOPT_LFLOW:
		case TELOPT_XDISPLOC:
#ifdef	TELOPT_ENVIRON
		case TELOPT_NEW_ENVIRON:
#endif
		case TELOPT_OLD_ENVIRON:
		default:
			break;
		}
		if (changeok) {
			set_my_want_state_will(option);
			send_will(option, 0);
		} else {
			will_wont_resp[option]++;
			send_wont(option, 0);
		}
	}
	set_my_state_will(option);

}  /* end of dooption */

void
send_wont(int option, int init)
{
	if (init) {
		if ((will_wont_resp[option] == 0 && my_state_is_wont(option)) ||
		    my_want_state_is_wont(option))
			return;
		set_my_want_state_wont(option);
		will_wont_resp[option]++;
	}
	output_data((const char *)wont, option);

	DIAG(TD_OPTIONS, printoption("td: send wont", option));
}

void
dontoption(int option)
{
	/*
	 * Process client input.
	 */


	DIAG(TD_OPTIONS, printoption("td: recv dont", option));

	if (will_wont_resp[option]) {
		will_wont_resp[option]--;
		if (will_wont_resp[option] && my_state_is_wont(option))
			will_wont_resp[option]--;
	}
	if ((will_wont_resp[option] == 0) && (my_want_state_is_will(option))) {
		switch (option) {
		case TELOPT_BINARY:
			init_termbuf();
			tty_binaryout(0);
			set_termbuf();
			break;

		case TELOPT_ECHO:	/* we should stop echoing */
#ifdef	LINEMODE
# ifdef	KLUDGELINEMODE
			if ((lmodetype != REAL_LINEMODE) &&
			    (lmodetype != KLUDGE_LINEMODE))
# else
			if (his_state_is_wont(TELOPT_LINEMODE))
# endif
#endif
			{
				init_termbuf();
				tty_setecho(0);
				set_termbuf();
			}
			break;

		case TELOPT_SGA:
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
			/*
			 * If kludge linemode is in use, then we
			 * must process an incoming do SGA for
			 * linemode purposes.
			 */
			if ((lmodetype == KLUDGE_LINEMODE) ||
			    (lmodetype == KLUDGE_OK)) {
				/*
				 * The client is asking us to turn
				 * linemode on.
				 */
				lmodetype = KLUDGE_LINEMODE;
				clientstat(TELOPT_LINEMODE, WILL, 0);
				/*
				 * If we did not turn line mode on,
				 * then what do we say?  Will SGA?
				 * This violates design of telnet.
				 * Gross.  Very Gross.
				 */
			}
			break;
#else
			set_my_want_state_wont(option);
			if (my_state_is_will(option))
				send_wont(option, 0);
			set_my_state_wont(option);
			if (turn_on_sga ^= 1)
				send_will(option, 1);
			return;
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */

		default:
			break;
		}

		set_my_want_state_wont(option);
		if (my_state_is_will(option))
			send_wont(option, 0);
	}
	set_my_state_wont(option);

}  /* end of dontoption */

#ifdef	ENV_HACK
int env_ovar = -1;
int env_ovalue = -1;
#else	/* ENV_HACK */
# define env_ovar OLD_ENV_VAR
# define env_ovalue OLD_ENV_VALUE
#endif	/* ENV_HACK */

/*
 * suboption()
 *
 *	Look at the sub-option buffer, and try to be helpful to the other
 * side.
 *
 *	Currently we recognize:
 *
 *	Terminal type is
 *	Linemode
 *	Window size
 *	Terminal speed
 */
void
suboption(void)
{
    int subchar;

    DIAG(TD_OPTIONS, {netflush(); printsub('<', subpointer, SB_LEN()+2);});

    subchar = SB_GET();
    switch (subchar) {
    case TELOPT_TSPEED: {
	int xspeed, rspeed;

	if (his_state_is_wont(TELOPT_TSPEED))	/* Ignore if option disabled */
		break;

	settimer(tspeedsubopt);

	if (SB_EOF() || SB_GET() != TELQUAL_IS)
		return;

	xspeed = atoi((char *)subpointer);

	while (SB_GET() != ',' && !SB_EOF());
	if (SB_EOF())
		return;

	rspeed = atoi((char *)subpointer);
	clientstat(TELOPT_TSPEED, xspeed, rspeed);

	break;

    }  /* end of case TELOPT_TSPEED */

    case TELOPT_TTYPE: {		/* Yaaaay! */
	static char terminalname[41];

	if (his_state_is_wont(TELOPT_TTYPE))	/* Ignore if option disabled */
		break;
	settimer(ttypesubopt);

	if (SB_EOF() || SB_GET() != TELQUAL_IS) {
	    return;		/* ??? XXX but, this is the most robust */
	}

	terminaltype = terminalname;

	while ((terminaltype < (terminalname + sizeof terminalname-1)) &&
								    !SB_EOF()) {
	    int c;

	    c = SB_GET();
	    if (isupper(c)) {
		c = tolower(c);
	    }
	    *terminaltype++ = c;    /* accumulate name */
	}
	*terminaltype = 0;
	terminaltype = terminalname;
	break;
    }  /* end of case TELOPT_TTYPE */

    case TELOPT_NAWS: {
	int xwinsize, ywinsize;

	if (his_state_is_wont(TELOPT_NAWS))	/* Ignore if option disabled */
		break;

	if (SB_EOF())
		return;
	xwinsize = SB_GET() << 8;
	if (SB_EOF())
		return;
	xwinsize |= SB_GET();
	if (SB_EOF())
		return;
	ywinsize = SB_GET() << 8;
	if (SB_EOF())
		return;
	ywinsize |= SB_GET();
	clientstat(TELOPT_NAWS, xwinsize, ywinsize);

	break;

    }  /* end of case TELOPT_NAWS */

#ifdef	LINEMODE
    case TELOPT_LINEMODE: {
	int request;

	if (his_state_is_wont(TELOPT_LINEMODE))	/* Ignore if option disabled */
		break;
	/*
	 * Process linemode suboptions.
	 */
	if (SB_EOF())
	    break;		/* garbage was sent */
	request = SB_GET();	/* get will/wont */

	if (SB_EOF())
	    break;		/* another garbage check */

	if (request == LM_SLC) {  /* SLC is not preceeded by WILL or WONT */
		/*
		 * Process suboption buffer of slc's
		 */
		start_slc(1);
		do_opt_slc(subpointer, subend - subpointer);
		(void) end_slc(0);
		break;
	} else if (request == LM_MODE) {
		if (SB_EOF())
		    return;
		useeditmode = SB_GET();  /* get mode flag */
		clientstat(LM_MODE, 0, 0);
		break;
	}

	if (SB_EOF())
	    break;
	switch (SB_GET()) {  /* what suboption? */
	case LM_FORWARDMASK:
		/*
		 * According to spec, only server can send request for
		 * forwardmask, and client can only return a positive response.
		 * So don't worry about it.
		 */

	default:
		break;
	}
	break;
    }  /* end of case TELOPT_LINEMODE */
#endif
    case TELOPT_STATUS: {
	int mode;

	if (SB_EOF())
	    break;
	mode = SB_GET();
	switch (mode) {
	case TELQUAL_SEND:
	    if (my_state_is_will(TELOPT_STATUS))
		send_status();
	    break;

	case TELQUAL_IS:
	    break;

	default:
	    break;
	}
	break;
    }  /* end of case TELOPT_STATUS */

    case TELOPT_XDISPLOC: {
	if (SB_EOF() || SB_GET() != TELQUAL_IS)
		return;
	settimer(xdisplocsubopt);
	subpointer[SB_LEN()] = '\0';
	(void)setenv("DISPLAY", (char *)subpointer, 1);
	break;
    }  /* end of case TELOPT_XDISPLOC */

#ifdef	TELOPT_NEW_ENVIRON
    case TELOPT_NEW_ENVIRON:
#endif
    case TELOPT_OLD_ENVIRON: {
	int c;
	char *cp, *varp, *valp;

	if (SB_EOF())
		return;
	c = SB_GET();
	if (c == TELQUAL_IS) {
		if (subchar == TELOPT_OLD_ENVIRON)
			settimer(oenvironsubopt);
		else
			settimer(environsubopt);
	} else if (c != TELQUAL_INFO) {
		return;
	}

#ifdef	TELOPT_NEW_ENVIRON
	if (subchar == TELOPT_NEW_ENVIRON) {
	    while (!SB_EOF()) {
		c = SB_GET();
		if ((c == NEW_ENV_VAR) || (c == ENV_USERVAR))
			break;
	    }
	} else
#endif
	{
#ifdef	ENV_HACK
	    /*
	     * We only want to do this if we haven't already decided
	     * whether or not the other side has its VALUE and VAR
	     * reversed.
	     */
	    if (env_ovar < 0) {
		int last = -1;		/* invalid value */
		int empty = 0;
		int got_var = 0, got_value = 0, got_uservar = 0;

		/*
		 * The other side might have its VALUE and VAR values
		 * reversed.  To be interoperable, we need to determine
		 * which way it is.  If the first recognized character
		 * is a VAR or VALUE, then that will tell us what
		 * type of client it is.  If the fist recognized
		 * character is a USERVAR, then we continue scanning
		 * the suboption looking for two consecutive
		 * VAR or VALUE fields.  We should not get two
		 * consecutive VALUE fields, so finding two
		 * consecutive VALUE or VAR fields will tell us
		 * what the client is.
		 */
		SB_SAVE();
		while (!SB_EOF()) {
			c = SB_GET();
			switch(c) {
			case OLD_ENV_VAR:
				if (last < 0 || last == OLD_ENV_VAR
				    || (empty && (last == OLD_ENV_VALUE)))
					goto env_ovar_ok;
				got_var++;
				last = OLD_ENV_VAR;
				break;
			case OLD_ENV_VALUE:
				if (last < 0 || last == OLD_ENV_VALUE
				    || (empty && (last == OLD_ENV_VAR)))
					goto env_ovar_wrong;
				got_value++;
				last = OLD_ENV_VALUE;
				break;
			case ENV_USERVAR:
				/* count strings of USERVAR as one */
				if (last != ENV_USERVAR)
					got_uservar++;
				if (empty) {
					if (last == OLD_ENV_VALUE)
						goto env_ovar_ok;
					if (last == OLD_ENV_VAR)
						goto env_ovar_wrong;
				}
				last = ENV_USERVAR;
				break;
			case ENV_ESC:
				if (!SB_EOF())
					c = SB_GET();
				/* FALLTHROUGH */
			default:
				empty = 0;
				continue;
			}
			empty = 1;
		}
		if (empty) {
			if (last == OLD_ENV_VALUE)
				goto env_ovar_ok;
			if (last == OLD_ENV_VAR)
				goto env_ovar_wrong;
		}
		/*
		 * Ok, the first thing was a USERVAR, and there
		 * are not two consecutive VAR or VALUE commands,
		 * and none of the VAR or VALUE commands are empty.
		 * If the client has sent us a well-formed option,
		 * then the number of VALUEs received should always
		 * be less than or equal to the number of VARs and
		 * USERVARs received.
		 *
		 * If we got exactly as many VALUEs as VARs and
		 * USERVARs, the client has the same definitions.
		 *
		 * If we got exactly as many VARs as VALUEs and
		 * USERVARS, the client has reversed definitions.
		 */
		if (got_uservar + got_var == got_value) {
	    env_ovar_ok:
			env_ovar = OLD_ENV_VAR;
			env_ovalue = OLD_ENV_VALUE;
		} else if (got_uservar + got_value == got_var) {
	    env_ovar_wrong:
			env_ovar = OLD_ENV_VALUE;
			env_ovalue = OLD_ENV_VAR;
			DIAG(TD_OPTIONS,
			    output_data("ENVIRON VALUE and VAR are reversed!\r\n"));

		}
	    }
	    SB_RESTORE();
#endif

	    while (!SB_EOF()) {
		c = SB_GET();
		if ((c == env_ovar) || (c == ENV_USERVAR))
			break;
	    }
	}

	if (SB_EOF())
		return;

	cp = varp = (char *)subpointer;
	valp = 0;

	while (!SB_EOF()) {
		c = SB_GET();
		if (subchar == TELOPT_OLD_ENVIRON) {
			if (c == env_ovar)
				c = NEW_ENV_VAR;
			else if (c == env_ovalue)
				c = NEW_ENV_VALUE;
		}
		switch (c) {

		case NEW_ENV_VALUE:
			*cp = '\0';
			cp = valp = (char *)subpointer;
			break;

		case NEW_ENV_VAR:
		case ENV_USERVAR:
			*cp = '\0';
			if (valp)
				(void)setenv(varp, valp, 1);
			else
				unsetenv(varp);
			cp = varp = (char *)subpointer;
			valp = 0;
			break;

		case ENV_ESC:
			if (SB_EOF())
				break;
			c = SB_GET();
			/* FALLTHROUGH */
		default:
			*cp++ = c;
			break;
		}
	}
	*cp = '\0';
	if (valp)
		(void)setenv(varp, valp, 1);
	else
		unsetenv(varp);
	break;
    }  /* end of case TELOPT_NEW_ENVIRON */
#ifdef	AUTHENTICATION
    case TELOPT_AUTHENTICATION:
	if (SB_EOF())
		break;
	switch(SB_GET()) {
	case TELQUAL_SEND:
	case TELQUAL_REPLY:
		/*
		 * These are sent by us and cannot be sent by
		 * the client.
		 */
		break;
	case TELQUAL_IS:
		auth_is(subpointer, SB_LEN());
		break;
	case TELQUAL_NAME:
		auth_name(subpointer, SB_LEN());
		break;
	}
	break;
#endif
#ifdef	ENCRYPTION
    case TELOPT_ENCRYPT:
	if (SB_EOF())
		break;
	switch(SB_GET()) {
	case ENCRYPT_SUPPORT:
		encrypt_support(subpointer, SB_LEN());
		break;
	case ENCRYPT_IS:
		encrypt_is(subpointer, SB_LEN());
		break;
	case ENCRYPT_REPLY:
		encrypt_reply(subpointer, SB_LEN());
		break;
	case ENCRYPT_START:
		encrypt_start(subpointer, SB_LEN());
		break;
	case ENCRYPT_END:
		encrypt_end();
		break;
	case ENCRYPT_REQSTART:
		encrypt_request_start(subpointer, SB_LEN());
		break;
	case ENCRYPT_REQEND:
		/*
		 * We can always send an REQEND so that we cannot
		 * get stuck encrypting.  We should only get this
		 * if we have been able to get in the correct mode
		 * anyhow.
		 */
		encrypt_request_end();
		break;
	case ENCRYPT_ENC_KEYID:
		encrypt_enc_keyid(subpointer, SB_LEN());
		break;
	case ENCRYPT_DEC_KEYID:
		encrypt_dec_keyid(subpointer, SB_LEN());
		break;
	default:
		break;
	}
	break;
#endif	/* ENCRYPTION */

    default:
	break;
    }  /* end of switch */

}  /* end of suboption */

static void
doclientstat(void)
{
	clientstat(TELOPT_LINEMODE, WILL, 0);
}

#define	ADD(c)	 *ncp++ = c
#define	ADD_DATA(c) { *ncp++ = c; if (c == SE || c == IAC) *ncp++ = c; }
void
send_status(void)
{
	unsigned char statusbuf[256];
	unsigned char *ncp;
	unsigned char i;

	ncp = statusbuf;

	netflush();	/* get rid of anything waiting to go out */

	ADD(IAC);
	ADD(SB);
	ADD(TELOPT_STATUS);
	ADD(TELQUAL_IS);

	/*
	 * We check the want_state rather than the current state,
	 * because if we received a DO/WILL for an option that we
	 * don't support, and the other side didn't send a DONT/WONT
	 * in response to our WONT/DONT, then the "state" will be
	 * WILL/DO, and the "want_state" will be WONT/DONT.  We
	 * need to go by the latter.
	 */
	for (i = 0; i < (unsigned char)NTELOPTS; i++) {
		if (my_want_state_is_will(i)) {
			ADD(WILL);
			ADD_DATA(i);
			if (i == IAC)
				ADD(IAC);
		}
		if (his_want_state_is_will(i)) {
			ADD(DO);
			ADD_DATA(i);
			if (i == IAC)
				ADD(IAC);
		}
	}

	if (his_want_state_is_will(TELOPT_LFLOW)) {
		ADD(SB);
		ADD(TELOPT_LFLOW);
		if (flowmode) {
			ADD(LFLOW_ON);
		} else {
			ADD(LFLOW_OFF);
		}
		ADD(SE);

		if (restartany >= 0) {
			ADD(SB);
			ADD(TELOPT_LFLOW);
			if (restartany) {
				ADD(LFLOW_RESTART_ANY);
			} else {
				ADD(LFLOW_RESTART_XON);
			}
			ADD(SE);
		}
	}

#ifdef	LINEMODE
	if (his_want_state_is_will(TELOPT_LINEMODE)) {
		unsigned char *cp, *cpe;
		int len;

		ADD(SB);
		ADD(TELOPT_LINEMODE);
		ADD(LM_MODE);
		ADD_DATA(editmode);
		ADD(SE);

		ADD(SB);
		ADD(TELOPT_LINEMODE);
		ADD(LM_SLC);
		start_slc(0);
		send_slc();
		len = end_slc(&cp);
		for (cpe = cp + len; cp < cpe; cp++)
			ADD_DATA(*cp);
		ADD(SE);
	}
#endif	/* LINEMODE */

	ADD(IAC);
	ADD(SE);

	output_datalen(statusbuf, ncp - statusbuf);
	netflush();	/* Send it on its way */

	DIAG(TD_OPTIONS,
		{printsub('>', statusbuf, ncp - statusbuf); netflush();});
}

/*
 * This function appends data to nfrontp and advances nfrontp.
 * Returns the number of characters written altogether (the
 * buffer may have been flushed in the process).
 */

int
output_data(const char *format, ...)
{
	va_list args;
	int len;
	char *buf;

	va_start(args, format);
	if ((len = vasprintf(&buf, format, args)) == -1) {
		va_end(args);
		return -1;
	}
	output_datalen(buf, len);
	va_end(args);
	free(buf);
	return (len);
}

void
output_datalen(const char *buf, int len)
{
	int remaining, copied;
	
	remaining = BUFSIZ - (nfrontp - netobuf);
	while (len > 0) {
		/* Free up enough space if the room is too low*/
		if ((len > BUFSIZ ? BUFSIZ : len) > remaining) {
			netflush();
			remaining = BUFSIZ - (nfrontp - netobuf);
		}

		/* Copy out as much as will fit */
		copied = remaining > len ? len : remaining;
		memmove(nfrontp, buf, copied);
		nfrontp += copied;
		len -= copied;
		remaining -= copied;
		buf += copied;
	}
	return;
}
