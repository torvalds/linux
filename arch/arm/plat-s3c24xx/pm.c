/* linux/arch/arm/plat-s3c24xx/pm.c
 *
 * Copyright (c) 2004,2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX Power Manager (Suspend-To-RAM) support
 *
 * See Documentation/arm/Samsung-S3C24XX/Suspend.txt for more information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Parts based on arch/arm/mach-pxa/pm.c
 *
 * Thanks to Dimitry Andric for debugging
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/crc32.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/serial_core.h>

#include <asm/cacheflush.h>
#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-mem.h>
#include <asm/arch/regs-irq.h>

#include <asm/mach/time.h>

#include <asm/plat-s3c24xx/pm.h>

/* for external use */

unsigned long s3c_pm_flags;

#define PFX "s3c24xx-pm: "

static struct sleep_save core_save[] = {
	SAVE_ITEM(S3C2410_LOCKTIME),
	SAVE_ITEM(S3C2410_CLKCON),

	/* we restore the timings here, with the proviso that the board
	 * brings the system up in an slower, or equal frequency setting
	 * to the original system.
	 *
	 * if we cannot guarantee this, then things are going to go very
	 * wrong here, as we modify the refresh and both pll settings.
	 */

	SAVE_ITEM(S3C2410_BWSCON),
	SAVE_ITEM(S3C2410_BANKCON0),
	SAVE_ITEM(S3C2410_BANKCON1),
	SAVE_ITEM(S3C2410_BANKCON2),
	SAVE_ITEM(S3C2410_BANKCON3),
	SAVE_ITEM(S3C2410_BANKCON4),
	SAVE_ITEM(S3C2410_BANKCON5),

	SAVE_ITEM(S3C2410_CLKDIVN),
	SAVE_ITEM(S3C2410_MPLLCON),
	SAVE_ITEM(S3C2410_UPLLCON),
	SAVE_ITEM(S3C2410_CLKSLOW),
	SAVE_ITEM(S3C2410_REFRESH),
};

static struct sleep_save gpio_save[] = {
	SAVE_ITEM(S3C2410_GPACON),
	SAVE_ITEM(S3C2410_GPADAT),

	SAVE_ITEM(S3C2410_GPBCON),
	SAVE_ITEM(S3C2410_GPBDAT),
	SAVE_ITEM(S3C2410_GPBUP),

	SAVE_ITEM(S3C2410_GPCCON),
	SAVE_ITEM(S3C2410_GPCDAT),
	SAVE_ITEM(S3C2410_GPCUP),

	SAVE_ITEM(S3C2410_GPDCON),
	SAVE_ITEM(S3C2410_GPDDAT),
	SAVE_ITEM(S3C2410_GPDUP),

	SAVE_ITEM(S3C2410_GPECON),
	SAVE_ITEM(S3C2410_GPEDAT),
	SAVE_ITEM(S3C2410_GPEUP),

	SAVE_ITEM(S3C2410_GPFCON),
	SAVE_ITEM(S3C2410_GPFDAT),
	SAVE_ITEM(S3C2410_GPFUP),

	SAVE_ITEM(S3C2410_GPGCON),
	SAVE_ITEM(S3C2410_GPGDAT),
	SAVE_ITEM(S3C2410_GPGUP),

	SAVE_ITEM(S3C2410_GPHCON),
	SAVE_ITEM(S3C2410_GPHDAT),
	SAVE_ITEM(S3C2410_GPHUP),

	SAVE_ITEM(S3C2410_DCLKCON),
};

#ifdef CONFIG_S3C2410_PM_DEBUG

#define SAVE_UART(va) \
	SAVE_ITEM((va) + S3C2410_ULCON), \
	SAVE_ITEM((va) + S3C2410_UCON), \
	SAVE_ITEM((va) + S3C2410_UFCON), \
	SAVE_ITEM((va) + S3C2410_UMCON), \
	SAVE_ITEM((va) + S3C2410_UBRDIV)

static struct sleep_save uart_save[] = {
	SAVE_UART(S3C24XX_VA_UART0),
	SAVE_UART(S3C24XX_VA_UART1),
#ifndef CONFIG_CPU_S3C2400
	SAVE_UART(S3C24XX_VA_UART2),
#endif
};

/* debug
 *
 * we send the debug to printascii() to allow it to be seen if the
 * system never wakes up from the sleep
*/

extern void printascii(const char *);

void pm_dbg(const char *fmt, ...)
{
	va_list va;
	char buff[256];

	va_start(va, fmt);
	vsprintf(buff, fmt, va);
	va_end(va);

	printascii(buff);
}

static void s3c2410_pm_debug_init(void)
{
	unsigned long tmp = __raw_readl(S3C2410_CLKCON);

	/* re-start uart clocks */
	tmp |= S3C2410_CLKCON_UART0;
	tmp |= S3C2410_CLKCON_UART1;
	tmp |= S3C2410_CLKCON_UART2;

	__raw_writel(tmp, S3C2410_CLKCON);
	udelay(10);
}

#define DBG(fmt...) pm_dbg(fmt)
#else
#define DBG(fmt...) printk(KERN_DEBUG fmt)

#define s3c2410_pm_debug_init() do { } while(0)

static struct sleep_save uart_save[] = {};
#endif

#if defined(CONFIG_S3C2410_PM_CHECK) && CONFIG_S3C2410_PM_CHECK_CHUNKSIZE != 0

/* suspend checking code...
 *
 * this next area does a set of crc checks over all the installed
 * memory, so the system can verify if the resume was ok.
 *
 * CONFIG_S3C2410_PM_CHECK_CHUNKSIZE defines the block-size for the CRC,
 * increasing it will mean that the area corrupted will be less easy to spot,
 * and reducing the size will cause the CRC save area to grow
*/

#define CHECK_CHUNKSIZE (CONFIG_S3C2410_PM_CHECK_CHUNKSIZE * 1024)

static u32 crc_size;	/* size needed for the crc block */
static u32 *crcs;	/* allocated over suspend/resume */

typedef u32 *(run_fn_t)(struct resource *ptr, u32 *arg);

/* s3c2410_pm_run_res
 *
 * go thorugh the given resource list, and look for system ram
*/

static void s3c2410_pm_run_res(struct resource *ptr, run_fn_t fn, u32 *arg)
{
	while (ptr != NULL) {
		if (ptr->child != NULL)
			s3c2410_pm_run_res(ptr->child, fn, arg);

		if ((ptr->flags & IORESOURCE_MEM) &&
		    strcmp(ptr->name, "System RAM") == 0) {
			DBG("Found system RAM at %08lx..%08lx\n",
			    ptr->start, ptr->end);
			arg = (fn)(ptr, arg);
		}

		ptr = ptr->sibling;
	}
}

static void s3c2410_pm_run_sysram(run_fn_t fn, u32 *arg)
{
	s3c2410_pm_run_res(&iomem_resource, fn, arg);
}

static u32 *s3c2410_pm_countram(struct resource *res, u32 *val)
{
	u32 size = (u32)(res->end - res->start)+1;

	size += CHECK_CHUNKSIZE-1;
	size /= CHECK_CHUNKSIZE;

	DBG("Area %08lx..%08lx, %d blocks\n", res->start, res->end, size);

	*val += size * sizeof(u32);
	return val;
}

/* s3c2410_pm_prepare_check
 *
 * prepare the necessary information for creating the CRCs. This
 * must be done before the final save, as it will require memory
 * allocating, and thus touching bits of the kernel we do not
 * know about.
*/

static void s3c2410_pm_check_prepare(void)
{
	crc_size = 0;

	s3c2410_pm_run_sysram(s3c2410_pm_countram, &crc_size);

	DBG("s3c2410_pm_prepare_check: %u checks needed\n", crc_size);

	crcs = kmalloc(crc_size+4, GFP_KERNEL);
	if (crcs == NULL)
		printk(KERN_ERR "Cannot allocated CRC save area\n");
}

static u32 *s3c2410_pm_makecheck(struct resource *res, u32 *val)
{
	unsigned long addr, left;

	for (addr = res->start; addr < res->end;
	     addr += CHECK_CHUNKSIZE) {
		left = res->end - addr;

		if (left > CHECK_CHUNKSIZE)
			left = CHECK_CHUNKSIZE;

		*val = crc32_le(~0, phys_to_virt(addr), left);
		val++;
	}

	return val;
}

/* s3c2410_pm_check_store
 *
 * compute the CRC values for the memory blocks before the final
 * sleep.
*/

static void s3c2410_pm_check_store(void)
{
	if (crcs != NULL)
		s3c2410_pm_run_sysram(s3c2410_pm_makecheck, crcs);
}

/* in_region
 *
 * return TRUE if the area defined by ptr..ptr+size contatins the
 * what..what+whatsz
*/

static inline int in_region(void *ptr, int size, void *what, size_t whatsz)
{
	if ((what+whatsz) < ptr)
		return 0;

	if (what > (ptr+size))
		return 0;

	return 1;
}

static u32 *s3c2410_pm_runcheck(struct resource *res, u32 *val)
{
	void *save_at = phys_to_virt(s3c2410_sleep_save_phys);
	unsigned long addr;
	unsigned long left;
	void *ptr;
	u32 calc;

	for (addr = res->start; addr < res->end;
	     addr += CHECK_CHUNKSIZE) {
		left = res->end - addr;

		if (left > CHECK_CHUNKSIZE)
			left = CHECK_CHUNKSIZE;

		ptr = phys_to_virt(addr);

		if (in_region(ptr, left, crcs, crc_size)) {
			DBG("skipping %08lx, has crc block in\n", addr);
			goto skip_check;
		}

		if (in_region(ptr, left, save_at, 32*4 )) {
			DBG("skipping %08lx, has save block in\n", addr);
			goto skip_check;
		}

		/* calculate and check the checksum */

		calc = crc32_le(~0, ptr, left);
		if (calc != *val) {
			printk(KERN_ERR PFX "Restore CRC error at "
			       "%08lx (%08x vs %08x)\n", addr, calc, *val);

			DBG("Restore CRC error at %08lx (%08x vs %08x)\n",
			    addr, calc, *val);
		}

	skip_check:
		val++;
	}

	return val;
}

/* s3c2410_pm_check_restore
 *
 * check the CRCs after the restore event and free the memory used
 * to hold them
*/

static void s3c2410_pm_check_restore(void)
{
	if (crcs != NULL) {
		s3c2410_pm_run_sysram(s3c2410_pm_runcheck, crcs);
		kfree(crcs);
		crcs = NULL;
	}
}

#else

#define s3c2410_pm_check_prepare() do { } while(0)
#define s3c2410_pm_check_restore() do { } while(0)
#define s3c2410_pm_check_store()   do { } while(0)
#endif

/* helper functions to save and restore register state */

void s3c2410_pm_do_save(struct sleep_save *ptr, int count)
{
	for (; count > 0; count--, ptr++) {
		ptr->val = __raw_readl(ptr->reg);
		DBG("saved %p value %08lx\n", ptr->reg, ptr->val);
	}
}

/* s3c2410_pm_do_restore
 *
 * restore the system from the given list of saved registers
 *
 * Note, we do not use DBG() in here, as the system may not have
 * restore the UARTs state yet
*/

void s3c2410_pm_do_restore(struct sleep_save *ptr, int count)
{
	for (; count > 0; count--, ptr++) {
		printk(KERN_DEBUG "restore %p (restore %08lx, was %08x)\n",
		       ptr->reg, ptr->val, __raw_readl(ptr->reg));

		__raw_writel(ptr->val, ptr->reg);
	}
}

/* s3c2410_pm_do_restore_core
 *
 * similar to s3c2410_pm_do_restore_core
 *
 * WARNING: Do not put any debug in here that may effect memory or use
 * peripherals, as things may be changing!
*/

static void s3c2410_pm_do_restore_core(struct sleep_save *ptr, int count)
{
	for (; count > 0; count--, ptr++) {
		__raw_writel(ptr->val, ptr->reg);
	}
}

/* s3c2410_pm_show_resume_irqs
 *
 * print any IRQs asserted at resume time (ie, we woke from)
*/

static void s3c2410_pm_show_resume_irqs(int start, unsigned long which,
					unsigned long mask)
{
	int i;

	which &= ~mask;

	for (i = 0; i <= 31; i++) {
		if ((which) & (1L<<i)) {
			DBG("IRQ %d asserted at resume\n", start+i);
		}
	}
}

/* s3c2410_pm_check_resume_pin
 *
 * check to see if the pin is configured correctly for sleep mode, and
 * make any necessary adjustments if it is not
*/

static void s3c2410_pm_check_resume_pin(unsigned int pin, unsigned int irqoffs)
{
	unsigned long irqstate;
	unsigned long pinstate;
	int irq = s3c2410_gpio_getirq(pin);

	if (irqoffs < 4)
		irqstate = s3c_irqwake_intmask & (1L<<irqoffs);
	else
		irqstate = s3c_irqwake_eintmask & (1L<<irqoffs);

	pinstate = s3c2410_gpio_getcfg(pin);

	if (!irqstate) {
		if (pinstate == S3C2410_GPIO_IRQ)
			DBG("Leaving IRQ %d (pin %d) enabled\n", irq, pin);
	} else {
		if (pinstate == S3C2410_GPIO_IRQ) {
			DBG("Disabling IRQ %d (pin %d)\n", irq, pin);
			s3c2410_gpio_cfgpin(pin, S3C2410_GPIO_INPUT);
		}
	}
}

/* s3c2410_pm_configure_extint
 *
 * configure all external interrupt pins
*/

static void s3c2410_pm_configure_extint(void)
{
	int pin;

	/* for each of the external interrupts (EINT0..EINT15) we
	 * need to check wether it is an external interrupt source,
	 * and then configure it as an input if it is not
	*/

	for (pin = S3C2410_GPF0; pin <= S3C2410_GPF7; pin++) {
		s3c2410_pm_check_resume_pin(pin, pin - S3C2410_GPF0);
	}

	for (pin = S3C2410_GPG0; pin <= S3C2410_GPG7; pin++) {
		s3c2410_pm_check_resume_pin(pin, (pin - S3C2410_GPG0)+8);
	}
}

void (*pm_cpu_prep)(void);
void (*pm_cpu_sleep)(void);

#define any_allowed(mask, allow) (((mask) & (allow)) != (allow))

/* s3c2410_pm_enter
 *
 * central control for sleep/resume process
*/

static int s3c2410_pm_enter(suspend_state_t state)
{
	unsigned long regs_save[16];

	/* ensure the debug is initialised (if enabled) */

	s3c2410_pm_debug_init();

	DBG("s3c2410_pm_enter(%d)\n", state);

	if (pm_cpu_prep == NULL || pm_cpu_sleep == NULL) {
		printk(KERN_ERR PFX "error: no cpu sleep functions set\n");
		return -EINVAL;
	}

	/* check if we have anything to wake-up with... bad things seem
	 * to happen if you suspend with no wakeup (system will often
	 * require a full power-cycle)
	*/

	if (!any_allowed(s3c_irqwake_intmask, s3c_irqwake_intallow) &&
	    !any_allowed(s3c_irqwake_eintmask, s3c_irqwake_eintallow)) {
		printk(KERN_ERR PFX "No sources enabled for wake-up!\n");
		printk(KERN_ERR PFX "Aborting sleep\n");
		return -EINVAL;
	}

	/* prepare check area if configured */

	s3c2410_pm_check_prepare();

	/* store the physical address of the register recovery block */

	s3c2410_sleep_save_phys = virt_to_phys(regs_save);

	DBG("s3c2410_sleep_save_phys=0x%08lx\n", s3c2410_sleep_save_phys);

	/* save all necessary core registers not covered by the drivers */

	s3c2410_pm_do_save(gpio_save, ARRAY_SIZE(gpio_save));
	s3c2410_pm_do_save(core_save, ARRAY_SIZE(core_save));
	s3c2410_pm_do_save(uart_save, ARRAY_SIZE(uart_save));

	/* set the irq configuration for wake */

	s3c2410_pm_configure_extint();

	DBG("sleep: irq wakeup masks: %08lx,%08lx\n",
	    s3c_irqwake_intmask, s3c_irqwake_eintmask);

	__raw_writel(s3c_irqwake_intmask, S3C2410_INTMSK);
	__raw_writel(s3c_irqwake_eintmask, S3C2410_EINTMASK);

	/* ack any outstanding external interrupts before we go to sleep */

	__raw_writel(__raw_readl(S3C2410_EINTPEND), S3C2410_EINTPEND);
	__raw_writel(__raw_readl(S3C2410_INTPND), S3C2410_INTPND);
	__raw_writel(__raw_readl(S3C2410_SRCPND), S3C2410_SRCPND);

	/* call cpu specific preperation */

	pm_cpu_prep();

	/* flush cache back to ram */

	flush_cache_all();

	s3c2410_pm_check_store();

	/* send the cpu to sleep... */

	__raw_writel(0x00, S3C2410_CLKCON);  /* turn off clocks over sleep */

	/* s3c2410_cpu_save will also act as our return point from when
	 * we resume as it saves its own register state, so use the return
	 * code to differentiate return from save and return from sleep */

	if (s3c2410_cpu_save(regs_save) == 0) {
		flush_cache_all();
		pm_cpu_sleep();
	}

	/* restore the cpu state */

	cpu_init();

	/* restore the system state */

	s3c2410_pm_do_restore_core(core_save, ARRAY_SIZE(core_save));
	s3c2410_pm_do_restore(gpio_save, ARRAY_SIZE(gpio_save));
	s3c2410_pm_do_restore(uart_save, ARRAY_SIZE(uart_save));

	s3c2410_pm_debug_init();

	/* check what irq (if any) restored the system */

	DBG("post sleep: IRQs 0x%08x, 0x%08x\n",
	    __raw_readl(S3C2410_SRCPND),
	    __raw_readl(S3C2410_EINTPEND));

	s3c2410_pm_show_resume_irqs(IRQ_EINT0, __raw_readl(S3C2410_SRCPND),
				    s3c_irqwake_intmask);

	s3c2410_pm_show_resume_irqs(IRQ_EINT4-4, __raw_readl(S3C2410_EINTPEND),
				    s3c_irqwake_eintmask);

	DBG("post sleep, preparing to return\n");

	s3c2410_pm_check_restore();

	/* ok, let's return from sleep */

	DBG("S3C2410 PM Resume (post-restore)\n");
	return 0;
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
static int s3c2410_pm_prepare(suspend_state_t state)
{
	return 0;
}

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
static int s3c2410_pm_finish(suspend_state_t state)
{
	return 0;
}

static struct pm_ops s3c2410_pm_ops = {
	.prepare	= s3c2410_pm_prepare,
	.enter		= s3c2410_pm_enter,
	.finish		= s3c2410_pm_finish,
};

/* s3c2410_pm_init
 *
 * Attach the power management functions. This should be called
 * from the board specific initialisation if the board supports
 * it.
*/

int __init s3c2410_pm_init(void)
{
	printk("S3C2410 Power Management, (c) 2004 Simtec Electronics\n");

	pm_set_ops(&s3c2410_pm_ops);
	return 0;
}
