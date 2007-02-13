/*
 * Copyright (C) 2003 PMC-Sierra Inc.
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 * Copyright (C) 2006 Ralf Baechle (ralf@linux-mips.org)
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
 * Second level Interrupt handlers for the PMC-Sierra Titan/Yosemite board
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/titan_dep.h>

/* Hypertransport specific */
#define IRQ_ACK_BITS            0x00000000	/* Ack bits */

#define HYPERTRANSPORT_INTA     0x78		/* INTA# */
#define HYPERTRANSPORT_INTB     0x79		/* INTB# */
#define HYPERTRANSPORT_INTC     0x7a		/* INTC# */
#define HYPERTRANSPORT_INTD     0x7b		/* INTD# */

/*
 * Handle hypertransport & SMP interrupts. The interrupt lines are scarce.
 * For interprocessor interrupts, the best thing to do is to use the INTMSG
 * register. We use the same external interrupt line, i.e. INTB3 and monitor
 * another status bit
 */
static void ll_ht_smp_irq_handler(int irq)
{
	u32 status = OCD_READ(RM9000x2_OCD_INTP0STATUS4);

	/* Ack all the bits that correspond to the interrupt sources */
	if (status != 0)
		OCD_WRITE(RM9000x2_OCD_INTP0STATUS4, IRQ_ACK_BITS);

	status = OCD_READ(RM9000x2_OCD_INTP1STATUS4);
	if (status != 0)
		OCD_WRITE(RM9000x2_OCD_INTP1STATUS4, IRQ_ACK_BITS);

#ifdef CONFIG_HT_LEVEL_TRIGGER
	/*
	 * Level Trigger Mode only. Send the HT EOI message back to the source.
	 */
	switch (status) {
	case 0x1000000:
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTA);
		break;
	case 0x2000000:
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTB);
		break;
	case 0x4000000:
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTC);
		break;
	case 0x8000000:
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTD);
		break;
	case 0x0000001:
		/* PLX */
		OCD_WRITE(RM9000x2_OCD_HTEOI, 0x20);
		OCD_WRITE(IRQ_CLEAR_REG, IRQ_ACK_BITS);
		break;
	case 0xf000000:
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTA);
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTB);
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTC);
		OCD_WRITE(RM9000x2_OCD_HTEOI, HYPERTRANSPORT_INTD);
		break;
	}
#endif /* CONFIG_HT_LEVEL_TRIGGER */

	do_IRQ(irq);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int cause = read_c0_cause();
	unsigned int status = read_c0_status();
	unsigned int pending = cause & status;

	if (pending & STATUSF_IP7) {
		do_IRQ(7);
	} else if (pending & STATUSF_IP2) {
#ifdef CONFIG_HYPERTRANSPORT
		ll_ht_smp_irq_handler(2);
#else
		do_IRQ(2);
#endif
	} else if (pending & STATUSF_IP3) {
		do_IRQ(3);
	} else if (pending & STATUSF_IP4) {
		do_IRQ(4);
	} else if (pending & STATUSF_IP5) {
#ifdef CONFIG_SMP
		titan_mailbox_irq();
#else
		do_IRQ(5);
#endif
	} else if (pending & STATUSF_IP6) {
		do_IRQ(4);
	}
}

#ifdef CONFIG_KGDB
extern void init_second_port(void);
#endif

/*
 * Initialize the next level interrupt handler
 */
void __init arch_init_irq(void)
{
	clear_c0_status(ST0_IM);

	mips_cpu_irq_init();
	rm7k_cpu_irq_init();
	rm9k_cpu_irq_init();

#ifdef CONFIG_KGDB
	/* At this point, initialize the second serial port */
	init_second_port();
#endif

#ifdef CONFIG_GDB_CONSOLE
	register_gdb_console();
#endif
}
