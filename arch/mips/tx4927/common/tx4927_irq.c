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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/tx4927/tx4927.h>
#ifdef CONFIG_TOSHIBA_RBTX4927
#include <asm/tx4927/toshiba_rbtx4927.h>
#endif

void __init tx4927_irq_init(void)
{
	mips_cpu_irq_init();
	txx9_irq_init(TX4927_IRC_REG);
	set_irq_chained_handler(TX4927_IRQ_NEST_PIC_ON_CP0, handle_simple_irq);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)			/* cpu timer */
		do_IRQ(TX4927_IRQ_CPU_TIMER);
	else if (pending & STATUSF_IP2) {		/* tx4927 pic */
		int irq = txx9_irq();
#ifdef CONFIG_TOSHIBA_RBTX4927
		if (irq == TX4927_IRQ_NEST_EXT_ON_PIC)
			irq = toshiba_rbtx4927_irq_nested(irq);
#endif
		if (unlikely(irq < 0)) {
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
