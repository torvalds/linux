/*
 * BRIEF MODULE DESCRIPTION
 *	Au1xxx irq map table
 *
 * Copyright 2003 Embedded Edge, LLC
 *		dan@embeddededge.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/mach-au1x00/au1000.h>

#ifdef CONFIG_MIPS_DB1500
char irq_tab_alchemy[][5] __initdata = {
	[12] = { -1, INTA, INTX, INTX, INTX }, /* IDSEL 12 - HPT371   */
	[13] = { -1, INTA, INTB, INTC, INTD }, /* IDSEL 13 - PCI slot */
};
#endif

#ifdef CONFIG_MIPS_BOSPORUS
char irq_tab_alchemy[][5] __initdata = {
	[11] = { -1, INTA, INTB, INTX, INTX }, /* IDSEL 11 - miniPCI  */
	[12] = { -1, INTA, INTX, INTX, INTX }, /* IDSEL 12 - SN1741   */
	[13] = { -1, INTA, INTB, INTC, INTD }, /* IDSEL 13 - PCI slot */
};
#endif

#ifdef CONFIG_MIPS_MIRAGE
char irq_tab_alchemy[][5] __initdata = {
	[11] = { -1, INTD, INTX, INTX, INTX }, /* IDSEL 11 - SMI VGX */
	[12] = { -1, INTX, INTX, INTC, INTX }, /* IDSEL 12 - PNX1300 */
	[13] = { -1, INTA, INTB, INTX, INTX }, /* IDSEL 13 - miniPCI */
};
#endif

#ifdef CONFIG_MIPS_DB1550
char irq_tab_alchemy[][5] __initdata = {
	[11] = { -1, INTC, INTX, INTX, INTX }, /* IDSEL 11 - on-board HPT371 */
	[12] = { -1, INTB, INTC, INTD, INTA }, /* IDSEL 12 - PCI slot 2 (left) */
	[13] = { -1, INTA, INTB, INTC, INTD }, /* IDSEL 13 - PCI slot 1 (right) */
};
#endif


struct au1xxx_irqmap __initdata au1xxx_irq_map[] = {

#ifndef CONFIG_MIPS_MIRAGE
#ifdef CONFIG_MIPS_DB1550
	{ AU1000_GPIO_3, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 0 IRQ# */
	{ AU1000_GPIO_5, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 1 IRQ# */
#else
	{ AU1000_GPIO_0, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 0 Fully_Interted# */
	{ AU1000_GPIO_1, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 0 STSCHG# */
	{ AU1000_GPIO_2, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 0 IRQ# */

	{ AU1000_GPIO_3, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 1 Fully_Interted# */
	{ AU1000_GPIO_4, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 1 STSCHG# */
	{ AU1000_GPIO_5, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card 1 IRQ# */
#endif
#else
	{ AU1000_GPIO_7, IRQF_TRIGGER_RISING, 0 }, /* touchscreen pen down */
#endif

};

void __init board_init_irq(void)
{
	au1xxx_setup_irqmap(au1xxx_irq_map, ARRAY_SIZE(au1xxx_irq_map));
}
