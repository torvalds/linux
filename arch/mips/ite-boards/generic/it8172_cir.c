/*
 *
 * BRIEF MODULE DESCRIPTION
 *	IT8172 Consumer IR port generic routines.
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

#include <linux/config.h>

#ifdef CONFIG_IT8172_CIR

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_cir.h>


volatile struct it8172_cir_regs *cir_regs[NUM_CIR_PORTS] = {
	(volatile struct it8172_cir_regs *)(KSEG1ADDR(IT8172_PCI_IO_BASE + IT_CIR0_BASE)),
	(volatile struct it8172_cir_regs *)(KSEG1ADDR(IT8172_PCI_IO_BASE + IT_CIR1_BASE))};


/*
 * Initialize Consumer IR Port.
 */
int cir_port_init(struct cir_port *cir)
{
	int port = cir->port;
	unsigned char data;

	/* set baud rate */
	cir_regs[port]->bdlr = cir->baud_rate & 0xff;
	cir_regs[port]->bdhr = (cir->baud_rate >> 8) & 0xff;

	/* set receiver control register */
	cir_regs[port]->rcr = (CIR_SET_RDWOS(cir->rdwos) | CIR_SET_RXDCR(cir->rxdcr));

	/* set carrier frequency register */
	cir_regs[port]->cfr = (CIR_SET_CF(cir->cfq) | CIR_SET_HS(cir->hcfs));

	/* set fifo threshold */
	data = cir_regs[port]->mstcr & 0xf3;
	data |= CIR_SET_FIFO_TL(cir->fifo_tl);
	cir_regs[port]->mstcr = data;

	clear_fifo(cir);
	enable_receiver(cir);
	disable_rx_demodulation(cir);

	set_rx_active(cir);
	int_enable(cir);
	rx_int_enable(cir);

	return 0;
}


void clear_fifo(struct cir_port *cir)
{
	cir_regs[cir->port]->mstcr |= CIR_FIFO_CLEAR;
}

void enable_receiver(struct cir_port *cir)
{
	cir_regs[cir->port]->rcr |= CIR_RXEN;
}

void disable_receiver(struct cir_port *cir)
{
	cir_regs[cir->port]->rcr &= ~CIR_RXEN;
}

void enable_rx_demodulation(struct cir_port *cir)
{
	cir_regs[cir->port]->rcr |= CIR_RXEND;
}

void disable_rx_demodulation(struct cir_port *cir)
{
	cir_regs[cir->port]->rcr &= ~CIR_RXEND;
}

void set_rx_active(struct cir_port *cir)
{
	cir_regs[cir->port]->rcr |= CIR_RXACT;
}

void int_enable(struct cir_port *cir)
{
	cir_regs[cir->port]->ier |= CIR_IEC;
}

void rx_int_enable(struct cir_port *cir)
{
	cir_regs[cir->port]->ier |= CIR_RDAIE;
}

void dump_regs(struct cir_port *cir)
{
	printk("mstcr %x ier %x iir %x cfr %x rcr %x tcr %x tfsr %x rfsr %x\n",
	cir_regs[cir->port]->mstcr,
	cir_regs[cir->port]->ier,
	cir_regs[cir->port]->iir,
	cir_regs[cir->port]->cfr,
	cir_regs[cir->port]->rcr,
	cir_regs[cir->port]->tcr,
	cir_regs[cir->port]->tfsr,
	cir_regs[cir->port]->rfsr);

	while (cir_regs[cir->port]->iir & CIR_RDAI) {
		printk("data %x\n", cir_regs[cir->port]->dr);
	}
}

void dump_reg_addr(struct cir_port *cir)
{
	printk("dr %x mstcr %x ier %x iir %x cfr %x rcr %x tcr %x bdlr %x bdhr %x tfsr %x rfsr %x\n",
	(unsigned)&cir_regs[cir->port]->dr,
	(unsigned)&cir_regs[cir->port]->mstcr,
	(unsigned)&cir_regs[cir->port]->ier,
	(unsigned)&cir_regs[cir->port]->iir,
	(unsigned)&cir_regs[cir->port]->cfr,
	(unsigned)&cir_regs[cir->port]->rcr,
	(unsigned)&cir_regs[cir->port]->tcr,
	(unsigned)&cir_regs[cir->port]->bdlr,
	(unsigned)&cir_regs[cir->port]->bdhr,
	(unsigned)&cir_regs[cir->port]->tfsr,
	(unsigned)&cir_regs[cir->port]->rfsr);
}

int cir_get_rx_count(struct cir_port *cir)
{
	return cir_regs[cir->port]->rfsr & CIR_RXFBC_MASK;
}

char cir_read_data(struct cir_port *cir)
{
	return cir_regs[cir->port]->dr;
}

char get_int_status(struct cir_port *cir)
{
	return cir_regs[cir->port]->iir;
}
#endif
