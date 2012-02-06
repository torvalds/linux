#ifdef CONFIG_ARCH_AT91RM9200
#include <mach/at91rm9200_mc.h>

/*
 * The AT91RM9200 goes into self-refresh mode with this command, and will
 * terminate self-refresh automatically on the next SDRAM access.
 *
 * Self-refresh mode is exited as soon as a memory access is made, but we don't
 * know for sure when that happens. However, we need to restore the low-power
 * mode if it was enabled before going idle. Restoring low-power mode while
 * still in self-refresh is "not recommended", but seems to work.
 */

static inline u32 sdram_selfrefresh_enable(void)
{
	u32 saved_lpr = at91_sys_read(AT91_SDRAMC_LPR);

	at91_sys_write(AT91_SDRAMC_LPR, 0);
	at91_sys_write(AT91_SDRAMC_SRR, 1);
	return saved_lpr;
}

#define sdram_selfrefresh_disable(saved_lpr)	at91_sys_write(AT91_SDRAMC_LPR, saved_lpr)
#define wait_for_interrupt_enable()		asm volatile ("mcr p15, 0, %0, c7, c0, 4" \
								: : "r" (0))

#elif defined(CONFIG_ARCH_AT91CAP9)
#include <mach/at91sam9_ddrsdr.h>


static inline u32 sdram_selfrefresh_enable(void)
{
	u32 saved_lpr, lpr;

	saved_lpr = at91_ramc_read(0, AT91CAP9_DDRSDRC_LPR);

	lpr = saved_lpr & ~AT91_DDRSDRC_LPCB;
	at91_ramc_write(0, AT91CAP9_DDRSDRC_LPR, lpr | AT91_DDRSDRC_LPCB_SELF_REFRESH);
	return saved_lpr;
}

#define sdram_selfrefresh_disable(saved_lpr)	at91_ramc_write(0, AT91CAP9_DDRSDRC_LPR, saved_lpr)
#define wait_for_interrupt_enable()		cpu_do_idle()

#elif defined(CONFIG_ARCH_AT91SAM9G45)
#include <mach/at91sam9_ddrsdr.h>

/* We manage both DDRAM/SDRAM controllers, we need more than one value to
 * remember.
 */
static u32 saved_lpr1;

static inline u32 sdram_selfrefresh_enable(void)
{
	/* Those tow values allow us to delay self-refresh activation
	 * to the maximum. */
	u32 lpr0, lpr1;
	u32 saved_lpr0;

	saved_lpr1 = at91_ramc_read(1, AT91_DDRSDRC_LPR);
	lpr1 = saved_lpr1 & ~AT91_DDRSDRC_LPCB;
	lpr1 |= AT91_DDRSDRC_LPCB_SELF_REFRESH;

	saved_lpr0 = at91_ramc_read(0, AT91_DDRSDRC_LPR);
	lpr0 = saved_lpr0 & ~AT91_DDRSDRC_LPCB;
	lpr0 |= AT91_DDRSDRC_LPCB_SELF_REFRESH;

	/* self-refresh mode now */
	at91_ramc_write(0, AT91_DDRSDRC_LPR, lpr0);
	at91_ramc_write(1, AT91_DDRSDRC_LPR, lpr1);

	return saved_lpr0;
}

#define sdram_selfrefresh_disable(saved_lpr0)	\
	do { \
		at91_ramc_write(0, AT91_DDRSDRC_LPR, saved_lpr0); \
		at91_ramc_write(1, AT91_DDRSDRC_LPR, saved_lpr1); \
	} while (0)
#define wait_for_interrupt_enable()		cpu_do_idle()

#else
#include <mach/at91sam9_sdramc.h>

#ifdef CONFIG_ARCH_AT91SAM9263
/*
 * FIXME either or both the SDRAM controllers (EB0, EB1) might be in use;
 * handle those cases both here and in the Suspend-To-RAM support.
 */
#warning Assuming EB1 SDRAM controller is *NOT* used
#endif

static inline u32 sdram_selfrefresh_enable(void)
{
	u32 saved_lpr, lpr;

	saved_lpr = at91_ramc_read(0, AT91_SDRAMC_LPR);

	lpr = saved_lpr & ~AT91_SDRAMC_LPCB;
	at91_ramc_write(0, AT91_SDRAMC_LPR, lpr | AT91_SDRAMC_LPCB_SELF_REFRESH);
	return saved_lpr;
}

#define sdram_selfrefresh_disable(saved_lpr)	at91_ramc_write(0, AT91_SDRAMC_LPR, saved_lpr)
#define wait_for_interrupt_enable()		cpu_do_idle()

#endif
