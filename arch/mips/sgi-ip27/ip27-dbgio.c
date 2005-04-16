/*
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
 * Copyright 2004 Ralf Baechle <ralf@linux-mips.org>
 */
#include <asm/sn/addrs.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/sn_private.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#define IOC3_CLK        (22000000 / 3)
#define IOC3_FLAGS      (0)

static inline struct ioc3_uartregs *console_uart(void)
{
	struct ioc3 *ioc3;

	ioc3 = (struct ioc3 *)KL_CONFIG_CH_CONS_INFO(get_nasid())->memory_base;

	return &ioc3->sregs.uarta;
}

unsigned char getDebugChar(void)
{
	struct ioc3_uartregs *uart = console_uart();

	while ((uart->iu_lsr & UART_LSR_DR) == 0);
	return uart->iu_rbr;
}

void putDebugChar(unsigned char c)
{
	struct ioc3_uartregs *uart = console_uart();

	while ((uart->iu_lsr & UART_LSR_THRE) == 0);
	uart->iu_thr = c;
}
