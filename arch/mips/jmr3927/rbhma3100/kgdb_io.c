/*
 * BRIEF MODULE DESCRIPTION
 *	Low level uart routines to directly access a TX[34]927 SIO.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ahennessy@mvista.com or source@mvista.com
 *
 * Based on arch/mips/ddb5xxx/ddb5477/kgdb_io.c
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/jmr3927/jmr3927.h>

#define TIMEOUT       0xffffff

static int remoteDebugInitialized = 0;
static void debugInit(int baud);

int putDebugChar(unsigned char c)
{
        int i = 0;

	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(38400);
	}

        do {
            slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (!(tx3927_sioptr(0)->cisr & TXx927_SICISR_TXALS));
	tx3927_sioptr(0)->tfifo = c;

	return 1;
}

unsigned char getDebugChar(void)
{
        int i = 0;
	int dicr;
	char c;

	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(38400);
	}

	/* diable RX int. */
	dicr = tx3927_sioptr(0)->dicr;
	tx3927_sioptr(0)->dicr = 0;

        do {
            slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (tx3927_sioptr(0)->disr & TXx927_SIDISR_UVALID)
		;
	c = tx3927_sioptr(0)->rfifo;

	/* clear RX int. status */
	tx3927_sioptr(0)->disr &= ~TXx927_SIDISR_RDIS;
	/* enable RX int. */
	tx3927_sioptr(0)->dicr = dicr;

	return c;
}

static void debugInit(int baud)
{
	tx3927_sioptr(0)->lcr = 0x020;
	tx3927_sioptr(0)->dicr = 0;
	tx3927_sioptr(0)->disr = 0x4100;
	tx3927_sioptr(0)->cisr = 0x014;
	tx3927_sioptr(0)->fcr = 0;
	tx3927_sioptr(0)->flcr = 0x02;
	tx3927_sioptr(0)->bgr = ((JMR3927_BASE_BAUD + baud / 2) / baud) |
		TXx927_SIBGR_BCLK_T0;
}
