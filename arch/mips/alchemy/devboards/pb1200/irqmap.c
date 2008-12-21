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

#include <linux/init.h>
#include <linux/interrupt.h>

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
	/* This is external interrupt cascade */
	{ AU1000_GPIO_7, INTC_INT_LOW_LEVEL, 0 },
};

int __initdata au1xxx_nr_irqs = ARRAY_SIZE(au1xxx_irq_map);

/*
 * Support for External interrupts on the Pb1200 Development platform.
 */
static volatile int pb1200_cascade_en;

irqreturn_t pb1200_cascade_handler(int irq, void *dev_id)
{
	unsigned short bisr = bcsr->int_status;
	int extirq_nr = 0;

	/* Clear all the edge interrupts. This has no effect on level. */
	bcsr->int_status = bisr;
	for ( ; bisr; bisr &= bisr - 1) {
		extirq_nr = PB1200_INT_BEGIN + __ffs(bisr);
		/* Ack and dispatch IRQ */
		do_IRQ(extirq_nr);
	}

	return IRQ_RETVAL(1);
}

inline void pb1200_enable_irq(unsigned int irq_nr)
{
	bcsr->intset_mask = 1 << (irq_nr - PB1200_INT_BEGIN);
	bcsr->intset = 1 << (irq_nr - PB1200_INT_BEGIN);
}

inline void pb1200_disable_irq(unsigned int irq_nr)
{
	bcsr->intclr_mask = 1 << (irq_nr - PB1200_INT_BEGIN);
	bcsr->intclr = 1 << (irq_nr - PB1200_INT_BEGIN);
}

static unsigned int pb1200_setup_cascade(void)
{
	return request_irq(AU1000_GPIO_7, &pb1200_cascade_handler,
			   0, "Pb1200 Cascade", &pb1200_cascade_handler);
}

static unsigned int pb1200_startup_irq(unsigned int irq)
{
	if (++pb1200_cascade_en == 1) {
		int res;

		res = pb1200_setup_cascade();
		if (res)
			return res;
	}

	pb1200_enable_irq(irq);

	return 0;
}

static void pb1200_shutdown_irq(unsigned int irq)
{
	pb1200_disable_irq(irq);
	if (--pb1200_cascade_en == 0)
		free_irq(AU1000_GPIO_7, &pb1200_cascade_handler);
}

static struct irq_chip external_irq_type = {
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
	unsigned int irq;

#ifdef CONFIG_MIPS_PB1200
	/* We have a problem with CPLD rev 3. */
	if (((bcsr->whoami & BCSR_WHOAMI_CPLD) >> 4) <= 3) {
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "Pb1200 must be at CPLD rev 4. Please have Pb1200\n");
		printk(KERN_ERR "updated to latest revision. This software will\n");
		printk(KERN_ERR "not work on anything less than CPLD rev 4.\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		panic("Game over.  Your score is 0.");
	}
#endif

	for (irq = PB1200_INT_BEGIN; irq <= PB1200_INT_END; irq++) {
		set_irq_chip_and_handler(irq, &external_irq_type,
					 handle_level_irq);
		pb1200_disable_irq(irq);
	}

	/*
	 * GPIO_7 can not be hooked here, so it is hooked upon first
	 * request of any source attached to the cascade.
	 */
}
