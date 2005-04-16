/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Low level uart routines to directly access a TX[34]927 SIO.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ahennessy@mvista.com or source@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * Based on arch/mips/au1000/common/puts.c
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

void
putch(const unsigned char c)
{
        int i = 0;

        do {
            slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (!(tx3927_sioptr(1)->cisr & TXx927_SICISR_TXALS));
	tx3927_sioptr(1)->tfifo = c;
	return;
}

unsigned char getch(void)
{
        int i = 0;
	int dicr;
	char c;

	/* diable RX int. */
	dicr = tx3927_sioptr(1)->dicr;
	tx3927_sioptr(1)->dicr = 0;

        do {
            slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (tx3927_sioptr(1)->disr & TXx927_SIDISR_UVALID)
		;
	c = tx3927_sioptr(1)->rfifo;

	/* clear RX int. status */
	tx3927_sioptr(1)->disr &= ~TXx927_SIDISR_RDIS;
	/* enable RX int. */
	tx3927_sioptr(1)->dicr = dicr;

	return c;
}
void
do_jmr3927_led_set(char n)
{
    /* and with current leds */
    jmr3927_led_and_set(n);
}

void
puts(unsigned char *cp)
{
    int i = 0;

    while (*cp) {
        do {
            slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (!(tx3927_sioptr(1)->cisr & TXx927_SICISR_TXALS));
	tx3927_sioptr(1)->tfifo = *cp++;
    }
    putch('\r');
    putch('\n');
}

void
fputs(unsigned char *cp)
{
    int i = 0;

    while (*cp) {
        do {
             slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (!(tx3927_sioptr(1)->cisr & TXx927_SICISR_TXALS));
	tx3927_sioptr(1)->tfifo = *cp++;
    }
}


void
put64(uint64_t ul)
{
    int cnt;
    unsigned ch;

    cnt = 16;            /* 16 nibbles in a 64 bit long */
    putch('0');
    putch('x');
    do {
        cnt--;
        ch = (unsigned char)(ul >> cnt * 4) & 0x0F;
                putch(digits[ch]);
    } while (cnt > 0);
}

void
put32(unsigned u)
{
    int cnt;
    unsigned ch;

    cnt = 8;            /* 8 nibbles in a 32 bit long */
    putch('0');
    putch('x');
    do {
        cnt--;
        ch = (unsigned char)(u >> cnt * 4) & 0x0F;
                putch(digits[ch]);
    } while (cnt > 0);
}
