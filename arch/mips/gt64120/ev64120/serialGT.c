/*
 * serialGT.c
 *
 * BRIEF MODULE DESCRIPTION
 *  Low Level Serial Port control for use
 *  with the Galileo EVB64120A MIPS eval board and
 *  its on board two channel 16552 Uart.
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
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
 *
 */

// Note:
//   Serial CHANNELS - 0 is the bottom connector of evb64120A.
//                       (The one that maps to the "B" channel of the
//                       board's uart)
//                     1 is the top connector of evb64120A.
//                       (The one that maps to the "A" channel of the
//                       board's uart)
int DEBUG_CHANNEL = 0;		// See Note Above
int CONSOLE_CHANNEL = 1;	// See Note Above

#define DUART 0xBD000000	/* Base address of Uart. */
#define CHANNELOFFSET 0x20	/* DUART+CHANNELOFFSET gets you to the ChanA
				   register set of the 16552 Uart device.
				   DUART+0 gets you to the ChanB register set.
				 */
#define DUART_DELTA 0x4
#define FIFO_ENABLE 0x07
#define INT_ENABLE  0x04	/* default interrupt mask */

#define RBR 0x00
#define THR 0x00
#define DLL 0x00
#define IER 0x01
#define DLM 0x01
#define IIR 0x02
#define FCR 0x02
#define LCR 0x03
#define MCR 0x04
#define LSR 0x05
#define MSR 0x06
#define SCR 0x07

#define LCR_DLAB 0x80
#define XTAL 1843200
#define LSR_THRE 0x20
#define LSR_BI   0x10
#define LSR_DR   0x01
#define MCR_LOOP 0x10
#define ACCESS_DELAY 0x10000

/******************************
 Routine:
 Description:
 ******************************/
int inreg(int channel, int reg)
{
	int val;
	val =
	    *((volatile unsigned char *) DUART +
	      (channel * CHANNELOFFSET) + (reg * DUART_DELTA));
	return val;
}

/******************************
 Routine:
 Description:
 ******************************/
void outreg(int channel, int reg, unsigned char val)
{
	*((volatile unsigned char *) DUART + (channel * CHANNELOFFSET)
	  + (reg * DUART_DELTA)) = val;
}

/******************************
 Routine:
 Description:
   Initialize the device driver.
 ******************************/
void serial_init(int channel)
{
	/*
	 * Configure active port, (CHANNELOFFSET already set.)
	 *
	 * Set 8 bits, 1 stop bit, no parity.
	 *
	 * LCR<7>       0       divisor latch access bit
	 * LCR<6>       0       break control (1=send break)
	 * LCR<5>       0       stick parity (0=space, 1=mark)
	 * LCR<4>       0       parity even (0=odd, 1=even)
	 * LCR<3>       0       parity enable (1=enabled)
	 * LCR<2>       0       # stop bits (0=1, 1=1.5)
	 * LCR<1:0>     11      bits per character(00=5, 01=6, 10=7, 11=8)
	 */
	outreg(channel, LCR, 0x3);

	outreg(channel, FCR, FIFO_ENABLE);	/* Enable the FIFO */

	outreg(channel, IER, INT_ENABLE);	/* Enable appropriate interrupts */
}

/******************************
 Routine:
 Description:
   Set the baud rate.
 ******************************/
void serial_set(int channel, unsigned long baud)
{
	unsigned char sav_lcr;

	/*
	 * Enable access to the divisor latches by setting DLAB in LCR.
	 *
	 */
	sav_lcr = inreg(channel, LCR);

#if 0
	/*
	 * Set baud rate
	 */
	outreg(channel, LCR, LCR_DLAB | sav_lcr);
	//  outreg(DLL,(XTAL/(16*2*(baud))-2));
	outreg(channel, DLL, XTAL / (16 * baud));
	//  outreg(DLM,(XTAL/(16*2*(baud))-2)>>8);
	outreg(channel, DLM, (XTAL / (16 * baud)) >> 8);
#else
	/*
	 * Note: Set baud rate, hardcoded here for rate of 115200
	 * since became unsure of above "buad rate" algorithm (??).
	 */
	outreg(channel, LCR, 0x83);
	outreg(channel, DLM, 0x00);	// See note above
	outreg(channel, DLL, 0x02);	// See note above.
	outreg(channel, LCR, 0x03);
#endif

	/*
	 * Restore line control register
	 */
	outreg(channel, LCR, sav_lcr);
}


/******************************
 Routine:
 Description:
   Transmit a character.
 ******************************/
void serial_putc(int channel, int c)
{
	while ((inreg(channel, LSR) & LSR_THRE) == 0);
	outreg(channel, THR, c);
}

/******************************
 Routine:
 Description:
    Read a received character if one is
    available.  Return -1 otherwise.
 ******************************/
int serial_getc(int channel)
{
	if (inreg(channel, LSR) & LSR_DR) {
		return inreg(channel, RBR);
	}
	return -1;
}

/******************************
 Routine:
 Description:
   Used by embedded gdb client. (example; gdb-stub.c)
 ******************************/
char getDebugChar()
{
	int val;
	while ((val = serial_getc(DEBUG_CHANNEL)) == -1);	// loop until we get a character in.
	return (char) val;
}

/******************************
 Routine:
 Description:
   Used by embedded gdb target. (example; gdb-stub.c)
 ******************************/
void putDebugChar(char c)
{
	serial_putc(DEBUG_CHANNEL, (int) c);
}
