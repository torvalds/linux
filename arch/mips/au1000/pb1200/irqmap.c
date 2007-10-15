/*
 * BRIEF MODULE DESCRIPTION
 *	Au1xxx irq map table
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/delay.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/mach-au1x00/au1000.h>

#ifdef CONFIG_MIPS_PB1200
#include <asm/mach-pb1x00/pb1200.h>
#endif

#ifdef CONFIG_MIPS_DB1200
#include <asm/mach-db1x00/db1200.h>
#define PB1200_INT_BEGIN DB1200_INT_BEGIN
#define PB1200_INT_END DB1200_INT_END
#endif

struct au1xxx_irqmap __initdata au1xxx_irq_map[] = {
	{ AU1000_GPIO_7, INTC_INT_LOW_LEVEL, 0 }, // This is exteranl interrupt cascade
};

int __initdata au1xxx_nr_irqs = ARRAY_SIZE(au1xxx_irq_map);

/*
 *	Support for External interrupts on the PbAu1200 Development platform.
 */
static volatile int pb1200_cascade_en=0;

irqreturn_t pb1200_cascade_handler( int irq, void *dev_id)
{
	unsigned short bisr = bcsr->int_status;
	int extirq_nr = 0;

	/* Clear all the edge interrupts. This has no effect on level */
	bcsr->int_status = bisr;
	for( ; bisr; bisr &= (bisr-1) )
	{
		extirq_nr = PB1200_INT_BEGIN + au_ffs(bisr);
		/* Ack and dispatch IRQ */
		do_IRQ(extirq_nr);
	}

	return IRQ_RETVAL(1);
}

inline void pb1200_enable_irq(unsigned int irq_nr)
{
	bcsr->intset_mask = 1<<(irq_nr - PB1200_INT_BEGIN);
	bcsr->intset = 1<<(irq_nr - PB1200_INT_BEGIN);
}

inline void pb1200_disable_irq(unsigned int irq_nr)
{
	bcsr->intclr_mask = 1<<(irq_nr - PB1200_INT_BEGIN);
	bcsr->intclr = 1<<(irq_nr - PB1200_INT_BEGIN);
}

static unsigned int pb1200_startup_irq( unsigned int irq_nr )
{
	if (++pb1200_cascade_en == 1)
	{
		request_irq(AU1000_GPIO_7, &pb1200_cascade_handler,
			0, "Pb1200 Cascade", (void *)&pb1200_cascade_handler );
#ifdef CONFIG_MIPS_PB1200
    /* We have a problem with CPLD rev3. Enable a workaround */
	if( ((bcsr->whoami & BCSR_WHOAMI_CPLD)>>4) <= 3)
	{
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("Pb1200 must be at CPLD rev4. Please have Pb1200\n");
		printk("updated to latest revision. This software will not\n");
		printk("work on anything less than CPLD rev4\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		printk("\nWARNING!!!\n");
		while(1);
	}
#endif
	}
	pb1200_enable_irq(irq_nr);
	return 0;
}

static void pb1200_shutdown_irq( unsigned int irq_nr )
{
	pb1200_disable_irq(irq_nr);
	if (--pb1200_cascade_en == 0)
	{
		free_irq(AU1000_GPIO_7, &pb1200_cascade_handler );
	}
	return;
}

static struct irq_chip external_irq_type =
{
#ifdef CONFIG_MIPS_PB1200
	.name = "Pb1200 Ext",
#endif
#ifdef CONFIG_MIPS_DB1200
	.name = "Db1200 Ext",
#endif
	.startup  = pb1200_startup_irq,
	.shutdown = pb1200_shutdown_irq,
	.ack      = pb1200_disable_irq,
	.mask     = pb1200_disable_irq,
	.mask_ack = pb1200_disable_irq,
	.unmask   = pb1200_enable_irq,
};

void _board_init_irq(void)
{
	int irq_nr;

	for (irq_nr = PB1200_INT_BEGIN; irq_nr <= PB1200_INT_END; irq_nr++)
	{
		set_irq_chip_and_handler(irq_nr, &external_irq_type,
					 handle_level_irq);
		pb1200_disable_irq(irq_nr);
	}

	/* GPIO_7 can not be hooked here, so it is hooked upon first
	request of any source attached to the cascade */
}

