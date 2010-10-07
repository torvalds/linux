/*
 *  BRIEF MODULE DESCRIPTION
 *     Serial port initialisation.
 *
 *  Copyright 2004 IDT Inc. (rischelp@idt.com)
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

#include <linux/init.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/irq.h>

#include <asm/serial.h>
#include <asm/mach-rc32434/rb.h>

extern unsigned int idt_cpu_freq;

static struct uart_port rb532_uart = {
	.flags = UPF_BOOT_AUTOCONF,
	.line = 0,
	.irq = UART0_IRQ,
	.iotype = UPIO_MEM,
	.membase = (char *)KSEG1ADDR(REGBASE + UART0BASE),
	.regshift = 2
};

int __init setup_serial_port(void)
{
	rb532_uart.uartclk = idt_cpu_freq;

	return early_serial_setup(&rb532_uart);
}
arch_initcall(setup_serial_port);
