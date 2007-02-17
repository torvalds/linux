/*
 * BRIEF MODULE DESCRIPTION
 *	Au1000 Power Management routines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
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
#include <linux/pm_legacy.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/jiffies.h>

#include <asm/string.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/cacheflush.h>
#include <asm/mach-au1x00/au1000.h>

#ifdef CONFIG_PM

#define DEBUG 1
#ifdef DEBUG
#  define DPRINTK(fmt, args...)	printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

static void au1000_calibrate_delay(void);

extern void set_au1x00_speed(unsigned int new_freq);
extern unsigned int get_au1x00_speed(void);
extern unsigned long get_au1x00_uart_baud_base(void);
extern void set_au1x00_uart_baud_base(unsigned long new_baud_base);
extern unsigned long save_local_and_disable(int controller);
extern void restore_local_and_enable(int controller, unsigned long mask);
extern void local_enable_irq(unsigned int irq_nr);

static DEFINE_SPINLOCK(pm_lock);

/* We need to save/restore a bunch of core registers that are
 * either volatile or reset to some state across a processor sleep.
 * If reading a register doesn't provide a proper result for a
 * later restore, we have to provide a function for loading that
 * register and save a copy.
 *
 * We only have to save/restore registers that aren't otherwise
 * done as part of a driver pm_* function.
 */
static unsigned int	sleep_aux_pll_cntrl;
static unsigned int	sleep_cpu_pll_cntrl;
static unsigned int	sleep_pin_function;
static unsigned int	sleep_uart0_inten;
static unsigned int	sleep_uart0_fifoctl;
static unsigned int	sleep_uart0_linectl;
static unsigned int	sleep_uart0_clkdiv;
static unsigned int	sleep_uart0_enable;
static unsigned int	sleep_usbhost_enable;
static unsigned int	sleep_usbdev_enable;
static unsigned int	sleep_static_memctlr[4][3];

/* Define this to cause the value you write to /proc/sys/pm/sleep to
 * set the TOY timer for the amount of time you want to sleep.
 * This is done mainly for testing, but may be useful in other cases.
 * The value is number of 32KHz ticks to sleep.
 */
#define SLEEP_TEST_TIMEOUT 1
#ifdef SLEEP_TEST_TIMEOUT
static	int	sleep_ticks;
void wakeup_counter0_set(int ticks);
#endif

static void
save_core_regs(void)
{
	extern void save_au1xxx_intctl(void);
	extern void pm_eth0_shutdown(void);

	/* Do the serial ports.....these really should be a pm_*
	 * registered function by the driver......but of course the
	 * standard serial driver doesn't understand our Au1xxx
	 * unique registers.
	 */
	sleep_uart0_inten = au_readl(UART0_ADDR + UART_IER);
	sleep_uart0_fifoctl = au_readl(UART0_ADDR + UART_FCR);
	sleep_uart0_linectl = au_readl(UART0_ADDR + UART_LCR);
	sleep_uart0_clkdiv = au_readl(UART0_ADDR + UART_CLK);
	sleep_uart0_enable = au_readl(UART0_ADDR + UART_MOD_CNTRL);

	/* Shutdown USB host/device.
	*/
	sleep_usbhost_enable = au_readl(USB_HOST_CONFIG);

	/* There appears to be some undocumented reset register....
	*/
	au_writel(0, 0xb0100004); au_sync();
	au_writel(0, USB_HOST_CONFIG); au_sync();

	sleep_usbdev_enable = au_readl(USBD_ENABLE);
	au_writel(0, USBD_ENABLE); au_sync();

	/* Save interrupt controller state.
	*/
	save_au1xxx_intctl();

	/* Clocks and PLLs.
	*/
	sleep_aux_pll_cntrl = au_readl(SYS_AUXPLL);

	/* We don't really need to do this one, but unless we
	 * write it again it won't have a valid value if we
	 * happen to read it.
	 */
	sleep_cpu_pll_cntrl = au_readl(SYS_CPUPLL);

	sleep_pin_function = au_readl(SYS_PINFUNC);

	/* Save the static memory controller configuration.
	*/
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
}

static void
restore_core_regs(void)
{
	extern void restore_au1xxx_intctl(void);
	extern void wakeup_counter0_adjust(void);

	au_writel(sleep_aux_pll_cntrl, SYS_AUXPLL); au_sync();
	au_writel(sleep_cpu_pll_cntrl, SYS_CPUPLL); au_sync();
	au_writel(sleep_pin_function, SYS_PINFUNC); au_sync();

	/* Restore the static memory controller configuration.
	*/
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

	/* Enable the UART if it was enabled before sleep.
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
	wakeup_counter0_adjust();
}

unsigned long suspend_mode;

void wakeup_from_suspend(void)
{
	suspend_mode = 0;
}

int au_sleep(void)
{
	unsigned long wakeup, flags;
	extern	void	save_and_sleep(void);

	spin_lock_irqsave(&pm_lock,flags);

	save_core_regs();

	flush_cache_all();

	/** The code below is all system dependent and we should probably
	 ** have a function call out of here to set this up.  You need
	 ** to configure the GPIO or timer interrupts that will bring
	 ** you out of sleep.
	 ** For testing, the TOY counter wakeup is useful.
	 **/

#if 0
	au_writel(au_readl(SYS_PINSTATERD) & ~(1 << 11), SYS_PINSTATERD);

	/* gpio 6 can cause a wake up event */
	wakeup = au_readl(SYS_WAKEMSK);
	wakeup &= ~(1 << 8);	/* turn off match20 wakeup */
	wakeup |= 1 << 6;	/* turn on gpio 6 wakeup   */
#else
	/* For testing, allow match20 to wake us up.
	*/
#ifdef SLEEP_TEST_TIMEOUT
	wakeup_counter0_set(sleep_ticks);
#endif
	wakeup = 1 << 8;	/* turn on match20 wakeup   */
	wakeup = 0;
#endif
	au_writel(1, SYS_WAKESRC);	/* clear cause */
	au_sync();
	au_writel(wakeup, SYS_WAKEMSK);
	au_sync();

	save_and_sleep();

	/* after a wakeup, the cpu vectors back to 0x1fc00000 so
	 * it's up to the boot code to get us back here.
	 */
	restore_core_regs();
	spin_unlock_irqrestore(&pm_lock, flags);
	return 0;
}

static int pm_do_sleep(ctl_table * ctl, int write, struct file *file,
		       void __user *buffer, size_t * len, loff_t *ppos)
{
	int retval = 0;
#ifdef SLEEP_TEST_TIMEOUT
#define TMPBUFLEN2 16
	char buf[TMPBUFLEN2], *p;
#endif

	if (!write) {
		*len = 0;
	} else {
#ifdef SLEEP_TEST_TIMEOUT
		if (*len > TMPBUFLEN2 - 1) {
			return -EFAULT;
		}
		if (copy_from_user(buf, buffer, *len)) {
			return -EFAULT;
		}
		buf[*len] = 0;
		p = buf;
		sleep_ticks = simple_strtoul(p, &p, 0);
#endif
		retval = pm_send_all(PM_SUSPEND, (void *) 2);

		if (retval)
			return retval;

		au_sleep();
		retval = pm_send_all(PM_RESUME, (void *) 0);
	}
	return retval;
}

static int pm_do_suspend(ctl_table * ctl, int write, struct file *file,
			 void __user *buffer, size_t * len, loff_t *ppos)
{
	int retval = 0;

	if (!write) {
		*len = 0;
	} else {
		retval = pm_send_all(PM_SUSPEND, (void *) 2);
		if (retval)
			return retval;
		suspend_mode = 1;

		retval = pm_send_all(PM_RESUME, (void *) 0);
	}
	return retval;
}


static int pm_do_freq(ctl_table * ctl, int write, struct file *file,
		      void __user *buffer, size_t * len, loff_t *ppos)
{
	int retval = 0, i;
	unsigned long val, pll;
#define TMPBUFLEN 64
#define MAX_CPU_FREQ 396
	char buf[TMPBUFLEN], *p;
	unsigned long flags, intc0_mask, intc1_mask;
	unsigned long old_baud_base, old_cpu_freq, baud_rate, old_clk,
	    old_refresh;
	unsigned long new_baud_base, new_cpu_freq, new_clk, new_refresh;

	spin_lock_irqsave(&pm_lock, flags);
	if (!write) {
		*len = 0;
	} else {
		/* Parse the new frequency */
		if (*len > TMPBUFLEN - 1) {
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}
		if (copy_from_user(buf, buffer, *len)) {
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}
		buf[*len] = 0;
		p = buf;
		val = simple_strtoul(p, &p, 0);
		if (val > MAX_CPU_FREQ) {
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}

		pll = val / 12;
		if ((pll > 33) || (pll < 7)) {	/* 396 MHz max, 84 MHz min */
			/* revisit this for higher speed cpus */
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}

		old_baud_base = get_au1x00_uart_baud_base();
		old_cpu_freq = get_au1x00_speed();

		new_cpu_freq = pll * 12 * 1000000;
	        new_baud_base =  (new_cpu_freq / (2 * ((int)(au_readl(SYS_POWERCTRL)&0x03) + 2) * 16));
		set_au1x00_speed(new_cpu_freq);
		set_au1x00_uart_baud_base(new_baud_base);

		old_refresh = au_readl(MEM_SDREFCFG) & 0x1ffffff;
		new_refresh =
		    ((old_refresh * new_cpu_freq) /
		     old_cpu_freq) | (au_readl(MEM_SDREFCFG) & ~0x1ffffff);

		au_writel(pll, SYS_CPUPLL);
		au_sync_delay(1);
		au_writel(new_refresh, MEM_SDREFCFG);
		au_sync_delay(1);

		for (i = 0; i < 4; i++) {
			if (au_readl
			    (UART_BASE + UART_MOD_CNTRL +
			     i * 0x00100000) == 3) {
				old_clk =
				    au_readl(UART_BASE + UART_CLK +
					  i * 0x00100000);
				// baud_rate = baud_base/clk
				baud_rate = old_baud_base / old_clk;
				/* we won't get an exact baud rate and the error
				 * could be significant enough that our new
				 * calculation will result in a clock that will
				 * give us a baud rate that's too far off from
				 * what we really want.
				 */
				if (baud_rate > 100000)
					baud_rate = 115200;
				else if (baud_rate > 50000)
					baud_rate = 57600;
				else if (baud_rate > 30000)
					baud_rate = 38400;
				else if (baud_rate > 17000)
					baud_rate = 19200;
				else
					(baud_rate = 9600);
				// new_clk = new_baud_base/baud_rate
				new_clk = new_baud_base / baud_rate;
				au_writel(new_clk,
				       UART_BASE + UART_CLK +
				       i * 0x00100000);
				au_sync_delay(10);
			}
		}
	}


	/* We don't want _any_ interrupts other than
	 * match20. Otherwise our au1000_calibrate_delay()
	 * calculation will be off, potentially a lot.
	 */
	intc0_mask = save_local_and_disable(0);
	intc1_mask = save_local_and_disable(1);
	local_enable_irq(AU1000_TOY_MATCH2_INT);
	spin_unlock_irqrestore(&pm_lock, flags);
	au1000_calibrate_delay();
	restore_local_and_enable(0, intc0_mask);
	restore_local_and_enable(1, intc1_mask);
	return retval;
}


static struct ctl_table pm_table[] = {
	{
		.ctl_name 	= CTL_UNNUMBERED,
		.procname	= "suspend",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0600,
		.proc_handler	= &pm_do_suspend
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sleep",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0600,
		.proc_handler	= &pm_do_sleep
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "freq",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0600,
		.proc_handler	= &pm_do_freq
	},
	{}
};

static struct ctl_table pm_dir_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "pm",
		.mode		= 0555,
		.child		= pm_table
	},
	{}
};

/*
 * Initialize power interface
 */
static int __init pm_init(void)
{
	register_sysctl_table(pm_dir_table);
	return 0;
}

__initcall(pm_init);


/*
 * This is right out of init/main.c
 */

/* This is the number of bits of precision for the loops_per_jiffy.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

static void au1000_calibrate_delay(void)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	loops_per_jiffy = (1 << 12);

	while (loops_per_jiffy <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */ ;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_jiffy);
		ticks = jiffies - ticks;
		if (ticks)
			break;
	}

/* Do a binary approximation to get loops_per_jiffy set to equal one clock
   (up to lps_precision bits) */
	loops_per_jiffy >>= 1;
	loopbit = loops_per_jiffy;
	while (lps_precision-- && (loopbit >>= 1)) {
		loops_per_jiffy |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_jiffy);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_jiffy &= ~loopbit;
	}
}
#endif				/* CONFIG_PM */
