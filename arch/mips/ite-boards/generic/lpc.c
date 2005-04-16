/*
 *
 * BRIEF MODULE DESCRIPTION
 *	ITE Semi IT8712 Super I/O functions.
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

#include <asm/io.h>
#include <asm/types.h>
#include <asm/it8712.h>
#include <asm/it8172/it8172.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

void LPCEnterMBPnP(void)
{
	int i;
	unsigned char key[4] = {0x87, 0x01, 0x55, 0x55};

	for (i = 0; i<4; i++)
		outb(key[i], LPC_KEY_ADDR);

}

void LPCExitMBPnP(void)
{
	outb(0x02, LPC_KEY_ADDR);
	outb(0x02, LPC_DATA_ADDR);
}

void LPCSetConfig(char LdnNumber, char Index, char data)
{
	LPCEnterMBPnP();				// Enter IT8712 MB PnP mode
	outb(0x07, LPC_KEY_ADDR);
	outb(LdnNumber, LPC_DATA_ADDR);
	outb(Index, LPC_KEY_ADDR);
	outb(data, LPC_DATA_ADDR);
	LPCExitMBPnP();
}

char LPCGetConfig(char LdnNumber, char Index)
{
	char rtn;

	LPCEnterMBPnP();				// Enter IT8712 MB PnP mode
	outb(0x07, LPC_KEY_ADDR);
	outb(LdnNumber, LPC_DATA_ADDR);
	outb(Index, LPC_KEY_ADDR);
	rtn = inb(LPC_DATA_ADDR);
	LPCExitMBPnP();
	return rtn;
}

int SearchIT8712(void)
{
	unsigned char Id1, Id2;
	unsigned short Id;

	LPCEnterMBPnP();
	outb(0x20, LPC_KEY_ADDR); /* chip id byte 1 */
	Id1 = inb(LPC_DATA_ADDR);
	outb(0x21, LPC_KEY_ADDR); /* chip id byte 2 */
	Id2 = inb(LPC_DATA_ADDR);
	Id = (Id1 << 8) | Id2;
	LPCExitMBPnP();
	if (Id == 0x8712)
		return TRUE;
	else
		return FALSE;
}

void InitLPCInterface(void)
{
	unsigned char bus, dev_fn;
	unsigned long data;

	bus = 0;
	dev_fn = 1<<3 | 4;


	/* pci cmd, SERR# Enable */
	IT_WRITE(IT_CONFADDR,
		 (bus         << IT_BUSNUM_SHF)   |
		 (dev_fn      << IT_FUNCNUM_SHF) |
		 ((0x4 / 4) << IT_REGNUM_SHF));
	IT_READ(IT_CONFDATA, data);
	data |= 0x0100;
	IT_WRITE(IT_CONFADDR,
		 (bus         << IT_BUSNUM_SHF)   |
		 (dev_fn      << IT_FUNCNUM_SHF) |
		 ((0x4 / 4) << IT_REGNUM_SHF));
	IT_WRITE(IT_CONFDATA, data);

	/* setup serial irq control register */
	IT_WRITE(IT_CONFADDR,
		 (bus         << IT_BUSNUM_SHF)   |
		 (dev_fn      << IT_FUNCNUM_SHF) |
		 ((0x48 / 4) << IT_REGNUM_SHF));
	IT_READ(IT_CONFDATA, data);
	data  = (data & 0xffff00ff) | 0xc400;
	IT_WRITE(IT_CONFADDR,
		 (bus         << IT_BUSNUM_SHF)   |
		 (dev_fn      << IT_FUNCNUM_SHF) |
		 ((0x48 / 4) << IT_REGNUM_SHF));
	IT_WRITE(IT_CONFDATA, data);


	/* Enable I/O Space Subtractive Decode */
	/* default 0x4C is 0x3f220000 */
	IT_WRITE(IT_CONFADDR,
		 (bus         << IT_BUSNUM_SHF)   |
		 (dev_fn      << IT_FUNCNUM_SHF) |
		 ((0x4C / 4) << IT_REGNUM_SHF));
	IT_WRITE(IT_CONFDATA, 0x3f2200f3);
}
