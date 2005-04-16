/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Low level uart routines to directly access a 16550 uart.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
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
#include <asm/mach-au1x00/au1000.h>

#define SERIAL_BASE   UART_BASE
#define SER_CMD       0x7
#define SER_DATA      0x1
#define TX_BUSY       0x20

#define TIMEOUT       0xffffff
#define SLOW_DOWN

static const char digits[16] = "0123456789abcdef";
static volatile unsigned long * const com1 = (unsigned long *)SERIAL_BASE;


#ifdef SLOW_DOWN
static inline void slow_down(void)
{
    int k;
    for (k=0; k<10000; k++);
}
#else
#define slow_down()
#endif

void
putch(const unsigned char c)
{
    unsigned char ch;
    int i = 0;

    do {
        ch = com1[SER_CMD];
        slow_down();
        i++;
        if (i>TIMEOUT) {
            break;
        }
    } while (0 == (ch & TX_BUSY));
    com1[SER_DATA] = c;
}

void
puts(unsigned char *cp)
{
    unsigned char ch;
    int i = 0;

    while (*cp) {
        do {
             ch = com1[SER_CMD];
            slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (0 == (ch & TX_BUSY));
        com1[SER_DATA] = *cp++;
    }
    putch('\r');
    putch('\n');
}

void
fputs(const char *cp)
{
    unsigned char ch;
    int i = 0;

    while (*cp) {

        do {
             ch = com1[SER_CMD];
             slow_down();
            i++;
            if (i>TIMEOUT) {
                break;
            }
        } while (0 == (ch & TX_BUSY));
        com1[SER_DATA] = *cp++;
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
