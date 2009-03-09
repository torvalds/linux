/*
 * BRIEF MODULE DESCRIPTION
 *	Au1xx0 Power Management routines.
 *
 * Copyright 2001, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 *  Some of the routines are right out of init/main.c, whose
 *  copyrights apply here.
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
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/jiffies.h>

#include <asm/uaccess.h>
#include <asm/mach-au1x00/au1000.h>
#if defined(CONFIG_SOC_AU1550) || defined(CONFIG_SOC_AU1200)
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#endif

#ifdef CONFIG_PM

/*
 * We need to save/restore a bunch of core registers that are
 * either volatile or reset to some state across a processor sleep.
 * If reading a register doesn't provide a proper result for a
 * later restore, we have to provide a function for loading that
 * register and save a copy.
 *
 * We only have to save/restore registers that aren't otherwise
 * done as part of a driver pm_* function.
 */
static unsigned int sleep_uart0_inten;
static unsigned int sleep_uart0_fifoctl;
static unsigned int sleep_uart0_linectl;
static unsigned int sleep_uart0_clkdiv;
static unsigned int sleep_uart0_enable;
static unsigned int sleep_usb[2];
static unsigned int sleep_sys_clocks[5];
static unsigned int sleep_sys_pinfunc;
static unsigned int sleep_static_memctlr[4][3];


static void save_core_regs(void)
{
	extern void save_au1xxx_intctl(void);
	extern void pm_eth0_shutdown(void);

	/*
	 * Do the serial ports.....these really should be a pm_*
	 * registered function by the driver......but of course the
	 * standard serial driver doesn't understand our Au1xxx
	 * unique registers.
	 */
	sleep_uart0_inten = au_readl(UART0_ADDR + UART_IER);
	sleep_uart0_fifoctl = au_readl(UART0_ADDR + UART_FCR);
	sleep_uart0_linectl = au_readl(UART0_ADDR + UART_LCR);
	sleep_uart0_clkdiv = au_readl(UART0_ADDR + UART_CLK);
	sleep_uart0_enable = au_readl(UART0_ADDR + UART_MOD_CNTRL);
	au_sync();

#ifndef CONFIG_SOC_AU1200
	/* Shutdown USB host/device. */
	sleep_usb[0] = au_readl(USB_HOST_CONFIG);

	/* There appears to be some undocumented reset register.... */
	au_writel(0, 0xb0100004);
	au_sync();
	au_writel(0, USB_HOST_CONFIG);
	au_sync();

	sleep_usb[1] = au_readl(USBD_ENABLE);
	au_writel(0, USBD_ENABLE);
	au_sync();

#else	/* AU1200 */

	/* enable access to OTG mmio so we can save OTG CAP/MUX.
	 * FIXME: write an OTG driver and move this stuff there!
	 */
	au_writel(au_readl(USB_MSR_BASE + 4) | (1 << 6), USB_MSR_BASE + 4);
	au_sync();
	sleep_usb[0] = au_readl(0xb4020020);	/* OTG_CAP */
	sleep_usb[1] = au_readl(0xb4020024);	/* OTG_MUX */
#endif

	/* Save interrupt controller state. */
	save_au1xxx_intctl();

	/* Clocks and PLLs. */
	sleep_sys_clocks[0] = au_readl(SYS_FREQCTRL0);
	sleep_sys_clocks[1] = au_readl(SYS_FREQCTRL1);
	sleep_sys_clocks[2] = au_readl(SYS_CLKSRC);
	sleep_sys_clocks[3] = au_readl(SYS_CPUPLL);
	sleep_sys_clocks[4] = au_readl(SYS_AUXPLL);

	/* pin mux config */
	sleep_sys_pinfunc = au_readl(SYS_PINFUNC);

	/* Save the static memory controller configuration. */
	sleep_static_memctlr[0][0] = au_readl(MEM_STCFG0);
	sleep_static_memctlr[0][1] = au_readl(MEM_STTIME0);
	sleep_static_memctlr[0][2] = au_readl(MEM_STADDR0);
	sleep_static_memctlr[1][0] = au_readl(MEM_STCFG1);
	sleep_static_memctlr[1][1] = au_readl(MEM_STTIME1);
	sleep_static_memctlr[1][2] = au_readl(MEM_STADDR1);
	sleep_static_memctlr[2][0] = au_readl(MEM_STCFG2);
	sleep_static_memctlr[2][1] = au_readl(MEM_STTIME2);
	sleep_static_memctlr[2][2] = au_readl(MEM_STADDR2);
	sleep_static_memctlr[3][0] = au_readl(MEM_STCFG3);
	sleep_static_memctlr[3][1] = au_readl(MEM_STTIME3);
	sleep_static_memctlr[3][2] = au_readl(MEM_STADDR3);

#if defined(CONFIG_SOC_AU1550) || defined(CONFIG_SOC_AU1200)
	au1xxx_dbdma_suspend();
#endif
}

static void restore_core_regs(void)
{
	/* restore clock configuration.  Writing CPUPLL last will
	 * stall a bit and stabilize other clocks (unless this is
	 * one of those Au1000 with a write-only PLL, where we dont
	 * have a valid value)
	 */
	au_writel(sleep_sys_clocks[0], SYS_FREQCTRL0);
	au_writel(sleep_sys_clocks[1], SYS_FREQCTRL1);
	au_writel(sleep_sys_clocks[2], SYS_CLKSRC);
	au_writel(sleep_sys_clocks[4], SYS_AUXPLL);
	if (!au1xxx_cpu_has_pll_wo())
		au_writel(sleep_sys_clocks[3], SYS_CPUPLL);
	au_sync();

	au_writel(sleep_sys_pinfunc, SYS_PINFUNC);
	au_sync();

#ifndef CONFIG_SOC_AU1200
	au_writel(sleep_usb[0], USB_HOST_CONFIG);
	au_writel(sleep_usb[1], USBD_ENABLE);
	au_sync();
#else
	/* enable accces to OTG memory */
	au_writel(au_readl(USB_MSR_BASE + 4) | (1 << 6), USB_MSR_BASE + 4);
	au_sync();

	/* restore OTG caps and port mux. */
	au_writel(sleep_usb[0], 0xb4020020 + 0);	/* OTG_CAP */
	au_sync();
	au_writel(sleep_usb[1], 0xb4020020 + 4);	/* OTG_MUX */
	au_sync();
#endif

	/* Restore the static memory controller configuration. */
	au_writel(sleep_static_memctlr[0][0], MEM_STCFG0);
	au_writel(sleep_static_memctlr[0][1], MEM_STTIME0);
	au_writel(sleep_static_memctlr[0][2], MEM_STADDR0);
	au_writel(sleep_static_memctlr[1][0], MEM_STCFG1);
	au_writel(sleep_static_memctlr[1][1], MEM_STTIME1);
	au_writel(sleep_static_memctlr[1][2], MEM_STADDR1);
	au_writel(sleep_static_memctlr[2][0], MEM_STCFG2);
	au_writel(sleep_static_memctlr[2][1], MEM_STTIME2);
	au_writel(sleep_static_memctlr[2][2], MEM_STADDR2);
	au_writel(sleep_static_memctlr[3][0], MEM_STCFG3);
	au_writel(sleep_static_memctlr[3][1], MEM_STTIME3);
	au_writel(sleep_static_memctlr[3][2], MEM_STADDR3);

	/*
	 * Enable the UART if it was enabled before sleep.
	 * I guess I should define module control bits........
	 */
	if (sleep_uart0_enable & 0x02) {
		au_writel(0, UART0_ADDR + UART_MOD_CNTRL); au_sync();
		au_writel(1, UART0_ADDR + UART_MOD_CNTRL); au_sync();
		au_writel(3, UART0_ADDR + UART_MOD_CNTRL); au_sync();
		au_writel(sleep_uart0_inten, UART0_ADDR + UART_IER); au_sync();
		au_writel(sleep_uart0_fifoctl, UART0_ADDR + UART_FCR); au_sync();
		au_writel(sleep_uart0_linectl, UART0_ADDR + UART_LCR); au_sync();
		au_writel(sleep_uart0_clkdiv, UART0_ADDR + UART_CLK); au_sync();
	}

	restore_au1xxx_intctl();

#if defined(CONFIG_SOC_AU1550) || defined(CONFIG_SOC_AU1200)
	au1xxx_dbdma_resume();
#endif
}

void au_sleep(void)
{
	save_core_regs();
	au1xxx_save_and_sleep();
	restore_core_regs();
}

#endif	/* CONFIG_PM */
