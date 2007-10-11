/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * This is the interface to the remote debugger stub.
 */
#include <asm/io.h>
#include <asm/mips-boards/atlas.h>
#include <asm/mips-boards/saa9730_uart.h>

#define INB(a)     inb((unsigned long)a)
#define OUTB(x, a)  outb(x, (unsigned long)a)

/*
 * This is the interface to the remote debugger stub
 * if the Philips part is used for the debug port,
 * called from the platform setup code.
 */
void *saa9730_base = (void *)ATLAS_SAA9730_REG;

static int saa9730_kgdb_active = 0;

#define SAA9730_BAUDCLOCK(baud) (((ATLAS_SAA9730_BAUDCLOCK/(baud))/16)-1)

int saa9730_kgdb_hook(int speed)
{
	int baudclock;
	t_uart_saa9730_regmap *kgdb_uart = (t_uart_saa9730_regmap *)(saa9730_base + SAA9730_UART_REGS_ADDR);

        /*
         * Clear all interrupts
         */
	(void) INB(&kgdb_uart->Lsr);
	(void) INB(&kgdb_uart->Msr);
	(void) INB(&kgdb_uart->Thr_Rbr);
	(void) INB(&kgdb_uart->Iir_Fcr);

        /*
         * Now, initialize the UART
         */
	/* 8 data bits, one stop bit, no parity */
	OUTB(SAA9730_LCR_DATA8, &kgdb_uart->Lcr);

	baudclock = SAA9730_BAUDCLOCK(speed);

	OUTB((baudclock >> 16) & 0xff, &kgdb_uart->BaudDivMsb);
	OUTB( baudclock        & 0xff, &kgdb_uart->BaudDivLsb);

	/* Set RTS/DTR active */
	OUTB(SAA9730_MCR_DTR | SAA9730_MCR_RTS, &kgdb_uart->Mcr);
	saa9730_kgdb_active = 1;

	return speed;
}

int saa9730_putDebugChar(char c)
{
	t_uart_saa9730_regmap *kgdb_uart = (t_uart_saa9730_regmap *)(saa9730_base + SAA9730_UART_REGS_ADDR);

        if (!saa9730_kgdb_active) {     /* need to init device first */
                return 0;
        }

        while (!(INB(&kgdb_uart->Lsr) & SAA9730_LSR_THRE))
                ;
	OUTB(c, &kgdb_uart->Thr_Rbr);

        return 1;
}

char saa9730_getDebugChar(void)
{
	t_uart_saa9730_regmap *kgdb_uart = (t_uart_saa9730_regmap *)(saa9730_base + SAA9730_UART_REGS_ADDR);
	char c;

        if (!saa9730_kgdb_active) {     /* need to init device first */
                return 0;
        }
        while (!(INB(&kgdb_uart->Lsr) & SAA9730_LSR_DR))
                ;

	c = INB(&kgdb_uart->Thr_Rbr);
	return(c);
}
