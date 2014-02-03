/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/setup.h>
#include <asm/irq_cpu.h>
#include <bcm47xx.h>

asmlinkage void plat_irq_dispatch(void)
{
	u32 cause;

	cause = read_c0_cause() & read_c0_status() & CAUSEF_IP;

	clear_c0_status(cause);

	if (cause & CAUSEF_IP7)
		do_IRQ(7);
	if (cause & CAUSEF_IP2)
		do_IRQ(2);
	if (cause & CAUSEF_IP3)
		do_IRQ(3);
	if (cause & CAUSEF_IP4)
		do_IRQ(4);
	if (cause & CAUSEF_IP5)
		do_IRQ(5);
	if (cause & CAUSEF_IP6)
		do_IRQ(6);
}

#define DEFINE_HWx_IRQDISPATCH(x)					\
	static void bcm47xx_hw ## x ## _irqdispatch(void)		\
	{								\
		do_IRQ(x);						\
	}
DEFINE_HWx_IRQDISPATCH(2)
DEFINE_HWx_IRQDISPATCH(3)
DEFINE_HWx_IRQDISPATCH(4)
DEFINE_HWx_IRQDISPATCH(5)
DEFINE_HWx_IRQDISPATCH(6)
DEFINE_HWx_IRQDISPATCH(7)

void __init arch_init_irq(void)
{
#ifdef CONFIG_BCM47XX_BCMA
	if (bcm47xx_bus_type == BCM47XX_BUS_TYPE_BCMA) {
		bcma_write32(bcm47xx_bus.bcma.bus.drv_mips.core,
			     BCMA_MIPS_MIPS74K_INTMASK(5), 1 << 31);
		/*
		 * the kernel reads the timer irq from some register and thinks
		 * it's #5, but we offset it by 2 and route to #7
		 */
		cp0_compare_irq = 7;
	}
#endif
	mips_cpu_irq_init();

	if (cpu_has_vint) {
		pr_info("Setting up vectored interrupts\n");
		set_vi_handler(2, bcm47xx_hw2_irqdispatch);
		set_vi_handler(3, bcm47xx_hw3_irqdispatch);
		set_vi_handler(4, bcm47xx_hw4_irqdispatch);
		set_vi_handler(5, bcm47xx_hw5_irqdispatch);
		set_vi_handler(6, bcm47xx_hw6_irqdispatch);
		set_vi_handler(7, bcm47xx_hw7_irqdispatch);
	}
}
