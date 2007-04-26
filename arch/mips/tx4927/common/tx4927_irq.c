/*
 * Common tx4927 irq handler
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
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
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/tx4927/tx4927.h>

/*
 * DEBUG
 */

#undef TX4927_IRQ_DEBUG

#ifdef TX4927_IRQ_DEBUG
#define TX4927_IRQ_NONE        0x00000000

#define TX4927_IRQ_INFO        ( 1 <<  0 )
#define TX4927_IRQ_WARN        ( 1 <<  1 )
#define TX4927_IRQ_EROR        ( 1 <<  2 )

#define TX4927_IRQ_INIT        ( 1 <<  5 )
#define TX4927_IRQ_NEST1       ( 1 <<  6 )
#define TX4927_IRQ_NEST2       ( 1 <<  7 )
#define TX4927_IRQ_NEST3       ( 1 <<  8 )
#define TX4927_IRQ_NEST4       ( 1 <<  9 )

#define TX4927_IRQ_CP0_INIT     ( 1 << 10 )
#define TX4927_IRQ_CP0_ENABLE   ( 1 << 13 )
#define TX4927_IRQ_CP0_DISABLE  ( 1 << 14 )

#define TX4927_IRQ_PIC_INIT     ( 1 << 20 )
#define TX4927_IRQ_PIC_ENABLE   ( 1 << 23 )
#define TX4927_IRQ_PIC_DISABLE  ( 1 << 24 )

#define TX4927_IRQ_ALL         0xffffffff
#endif

#ifdef TX4927_IRQ_DEBUG
static const u32 tx4927_irq_debug_flag = (TX4927_IRQ_NONE
					  | TX4927_IRQ_INFO
					  | TX4927_IRQ_WARN | TX4927_IRQ_EROR
//                                       | TX4927_IRQ_CP0_INIT
//                                       | TX4927_IRQ_CP0_ENABLE
//                                       | TX4927_IRQ_CP0_ENDIRQ
//                                       | TX4927_IRQ_PIC_INIT
//                                       | TX4927_IRQ_PIC_ENABLE
//                                       | TX4927_IRQ_PIC_DISABLE
//                                       | TX4927_IRQ_INIT
//                                       | TX4927_IRQ_NEST1
//                                       | TX4927_IRQ_NEST2
//                                       | TX4927_IRQ_NEST3
//                                       | TX4927_IRQ_NEST4
    );
#endif

#ifdef TX4927_IRQ_DEBUG
#define TX4927_IRQ_DPRINTK(flag,str...) \
        if ( (tx4927_irq_debug_flag) & (flag) ) \
        { \
           char tmp[100]; \
           sprintf( tmp, str ); \
           printk( "%s(%s:%u)::%s", __FUNCTION__, __FILE__, __LINE__, tmp ); \
        }
#else
#define TX4927_IRQ_DPRINTK(flag,str...)
#endif

/*
 * Forwad definitions for all pic's
 */

static void tx4927_irq_cp0_enable(unsigned int irq);
static void tx4927_irq_cp0_disable(unsigned int irq);

static void tx4927_irq_pic_enable(unsigned int irq);
static void tx4927_irq_pic_disable(unsigned int irq);

/*
 * Kernel structs for all pic's
 */

#define TX4927_CP0_NAME "TX4927-CP0"
static struct irq_chip tx4927_irq_cp0_type = {
	.name		= TX4927_CP0_NAME,
	.ack		= tx4927_irq_cp0_disable,
	.mask		= tx4927_irq_cp0_disable,
	.mask_ack	= tx4927_irq_cp0_disable,
	.unmask		= tx4927_irq_cp0_enable,
};

#define TX4927_PIC_NAME "TX4927-PIC"
static struct irq_chip tx4927_irq_pic_type = {
	.name		= TX4927_PIC_NAME,
	.ack		= tx4927_irq_pic_disable,
	.mask		= tx4927_irq_pic_disable,
	.mask_ack	= tx4927_irq_pic_disable,
	.unmask		= tx4927_irq_pic_enable,
};

#define TX4927_PIC_ACTION(s) { no_action, 0, CPU_MASK_NONE, s, NULL, NULL }
static struct irqaction tx4927_irq_pic_action =
TX4927_PIC_ACTION(TX4927_PIC_NAME);

#define CCP0_STATUS 12
#define CCP0_CAUSE 13

/*
 * Functions for cp0
 */

#define tx4927_irq_cp0_mask(irq) ( 1 << ( irq-TX4927_IRQ_CP0_BEG+8 ) )

static void
tx4927_irq_cp0_modify(unsigned cp0_reg, unsigned clr_bits, unsigned set_bits)
{
	unsigned long val = 0;

	switch (cp0_reg) {
	case CCP0_STATUS:
		val = read_c0_status();
		break;

	case CCP0_CAUSE:
		val = read_c0_cause();
		break;

	}

	val &= (~clr_bits);
	val |= (set_bits);

	switch (cp0_reg) {
	case CCP0_STATUS:{
			write_c0_status(val);
			break;
		}
	case CCP0_CAUSE:{
			write_c0_cause(val);
			break;
		}
	}
}

static void __init tx4927_irq_cp0_init(void)
{
	int i;

	TX4927_IRQ_DPRINTK(TX4927_IRQ_CP0_INIT, "beg=%d end=%d\n",
			   TX4927_IRQ_CP0_BEG, TX4927_IRQ_CP0_END);

	for (i = TX4927_IRQ_CP0_BEG; i <= TX4927_IRQ_CP0_END; i++)
		set_irq_chip_and_handler(i, &tx4927_irq_cp0_type,
					 handle_level_irq);
}

static void tx4927_irq_cp0_enable(unsigned int irq)
{
	TX4927_IRQ_DPRINTK(TX4927_IRQ_CP0_ENABLE, "irq=%d \n", irq);

	tx4927_irq_cp0_modify(CCP0_STATUS, 0, tx4927_irq_cp0_mask(irq));
}

static void tx4927_irq_cp0_disable(unsigned int irq)
{
	TX4927_IRQ_DPRINTK(TX4927_IRQ_CP0_DISABLE, "irq=%d \n", irq);

	tx4927_irq_cp0_modify(CCP0_STATUS, tx4927_irq_cp0_mask(irq), 0);
}

/*
 * Functions for pic
 */
u32 tx4927_irq_pic_addr(int irq)
{
	/* MVMCP -- need to formulize this */
	irq -= TX4927_IRQ_PIC_BEG;
	switch (irq) {
	case 17:
	case 16:
	case 1:
	case 0:
		return (0xff1ff610);

	case 19:
	case 18:
	case 3:
	case 2:
		return (0xff1ff614);

	case 21:
	case 20:
	case 5:
	case 4:
		return (0xff1ff618);

	case 23:
	case 22:
	case 7:
	case 6:
		return (0xff1ff61c);

	case 25:
	case 24:
	case 9:
	case 8:
		return (0xff1ff620);

	case 27:
	case 26:
	case 11:
	case 10:
		return (0xff1ff624);

	case 29:
	case 28:
	case 13:
	case 12:
		return (0xff1ff628);

	case 31:
	case 30:
	case 15:
	case 14:
		return (0xff1ff62c);

	}
	return (0);
}

u32 tx4927_irq_pic_mask(int irq)
{
	/* MVMCP -- need to formulize this */
	irq -= TX4927_IRQ_PIC_BEG;
	switch (irq) {
	case 31:
	case 29:
	case 27:
	case 25:
	case 23:
	case 21:
	case 19:
	case 17:{
			return (0x07000000);
		}
	case 30:
	case 28:
	case 26:
	case 24:
	case 22:
	case 20:
	case 18:
	case 16:{
			return (0x00070000);
		}
	case 15:
	case 13:
	case 11:
	case 9:
	case 7:
	case 5:
	case 3:
	case 1:{
			return (0x00000700);
		}
	case 14:
	case 12:
	case 10:
	case 8:
	case 6:
	case 4:
	case 2:
	case 0:{
			return (0x00000007);
		}
	}
	return (0x00000000);
}

static void tx4927_irq_pic_modify(unsigned pic_reg, unsigned clr_bits,
	unsigned set_bits)
{
	unsigned long val = 0;

	val = TX4927_RD(pic_reg);
	val &= (~clr_bits);
	val |= (set_bits);
	TX4927_WR(pic_reg, val);
}

static void __init tx4927_irq_pic_init(void)
{
	int i;

	TX4927_IRQ_DPRINTK(TX4927_IRQ_PIC_INIT, "beg=%d end=%d\n",
			   TX4927_IRQ_PIC_BEG, TX4927_IRQ_PIC_END);

	for (i = TX4927_IRQ_PIC_BEG; i <= TX4927_IRQ_PIC_END; i++)
		set_irq_chip_and_handler(i, &tx4927_irq_pic_type,
					 handle_level_irq);

	setup_irq(TX4927_IRQ_NEST_PIC_ON_CP0, &tx4927_irq_pic_action);

	TX4927_WR(0xff1ff640, 0x6);	/* irq level mask -- only accept hightest */
	TX4927_WR(0xff1ff600, TX4927_RD(0xff1ff600) | 0x1);	/* irq enable */
}

static void tx4927_irq_pic_enable(unsigned int irq)
{
	TX4927_IRQ_DPRINTK(TX4927_IRQ_PIC_ENABLE, "irq=%d\n", irq);

	tx4927_irq_pic_modify(tx4927_irq_pic_addr(irq), 0,
			      tx4927_irq_pic_mask(irq));
}

static void tx4927_irq_pic_disable(unsigned int irq)
{
	TX4927_IRQ_DPRINTK(TX4927_IRQ_PIC_DISABLE, "irq=%d\n", irq);

	tx4927_irq_pic_modify(tx4927_irq_pic_addr(irq),
			      tx4927_irq_pic_mask(irq), 0);
}

/*
 * Main init functions
 */
void __init tx4927_irq_init(void)
{
	TX4927_IRQ_DPRINTK(TX4927_IRQ_INIT, "-\n");

	TX4927_IRQ_DPRINTK(TX4927_IRQ_INIT, "=Calling tx4927_irq_cp0_init()\n");
	tx4927_irq_cp0_init();

	TX4927_IRQ_DPRINTK(TX4927_IRQ_INIT, "=Calling tx4927_irq_pic_init()\n");
	tx4927_irq_pic_init();

	TX4927_IRQ_DPRINTK(TX4927_IRQ_INIT, "+\n");
}

static int tx4927_irq_nested(void)
{
	int sw_irq = 0;
	u32 level2;

	TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST1, "-\n");

	level2 = TX4927_RD(0xff1ff6a0);
	TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST2, "=level2a=0x%x\n", level2);

	if ((level2 & 0x10000) == 0) {
		level2 &= 0x1f;
		TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST3, "=level2b=0x%x\n", level2);

		sw_irq = TX4927_IRQ_PIC_BEG + level2;
		TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST3, "=sw_irq=%d\n", sw_irq);

		if (sw_irq == 27) {
			TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST4, "=irq-%d\n",
					   sw_irq);

#ifdef CONFIG_TOSHIBA_RBTX4927
			{
				sw_irq = toshiba_rbtx4927_irq_nested(sw_irq);
			}
#endif

			TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST4, "=irq+%d\n",
					   sw_irq);
		}
	}

	TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST2, "=sw_irq=%d\n", sw_irq);

	TX4927_IRQ_DPRINTK(TX4927_IRQ_NEST1, "+\n");

	return (sw_irq);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)			/* cpu timer */
		do_IRQ(TX4927_IRQ_CPU_TIMER);
	else if (pending & STATUSF_IP2) {		/* tx4927 pic */
		unsigned int irq = tx4927_irq_nested();

		if (unlikely(irq == 0)) {
			spurious_interrupt();
			return;
		}
		do_IRQ(irq);
	} else if (pending & STATUSF_IP0)		/* user line 0 */
		do_IRQ(TX4927_IRQ_USER0);
	else if (pending & STATUSF_IP1)			/* user line 1 */
		do_IRQ(TX4927_IRQ_USER1);
	else
		spurious_interrupt();
}
