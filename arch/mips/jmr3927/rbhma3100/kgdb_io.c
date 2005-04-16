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

#include <linux/types.h>
#include <asm/jmr3927/txx927.h>
#include <asm/jmr3927/tx3927.h>
#include <asm/jmr3927/jmr3927.h>

#define TIMEOUT       0xffffff
#define SLOW_DOWN

static const char digits[16] = "0123456789abcdef";

#ifdef SLOW_DOWN
#define slow_down() { int k; for (k=0; k<10000; k++); }
#else
#define slow_down()
#endif

static int remoteDebugInitialized = 0;

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

void debugInit(int baud)
{
	/*
	volatile unsigned long lcr;
	volatile unsigned long dicr;
	volatile unsigned long disr;
	volatile unsigned long cisr;
	volatile unsigned long fcr;
	volatile unsigned long flcr;
	volatile unsigned long bgr;
	volatile unsigned long tfifo;
	volatile unsigned long rfifo;
	*/

	tx3927_sioptr(0)->lcr = 0x020;
	tx3927_sioptr(0)->dicr = 0;
	tx3927_sioptr(0)->disr = 0x4100;
	tx3927_sioptr(0)->cisr = 0x014;
	tx3927_sioptr(0)->fcr = 0;
	tx3927_sioptr(0)->flcr = 0x02;
	tx3927_sioptr(0)->bgr = ((JMR3927_BASE_BAUD + baud / 2) / baud) |
		TXx927_SIBGR_BCLK_T0;
#if 0
	/*
	 * Reset the UART.
	 */
	tx3927_sioptr(0)->fcr = TXx927_SIFCR_SWRST;
	while (tx3927_sioptr(0)->fcr & TXx927_SIFCR_SWRST)
		;

	/*
	 * and set the speed of the serial port
	 * (currently hardwired to 9600 8N1
	 */

	tx3927_sioptr(0)->lcr = TXx927_SILCR_UMODE_8BIT |
		TXx927_SILCR_USBL_1BIT |
		TXx927_SILCR_SCS_IMCLK_BG;
	tx3927_sioptr(0)->bgr =
		((JMR3927_BASE_BAUD + baud / 2) / baud) |
		TXx927_SIBGR_BCLK_T0;

	/* HW RTS/CTS control */
	if (ser->flags & ASYNC_HAVE_CTS_LINE)
		tx3927_sioptr(0)->flcr = TXx927_SIFLCR_RCS | TXx927_SIFLCR_TES |
			TXx927_SIFLCR_RTSTL_MAX /* 15 */;
	/* Enable RX/TX */
	tx3927_sioptr(0)->flcr &= ~(TXx927_SIFLCR_RSDE | TXx927_SIFLCR_TSDE);
#endif
}
