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

#elif defined(CONFIG_ARCH_AT91CAP9)
#include <mach/at91cap9_ddrsdr.h>


static inline u32 sdram_selfrefresh_enable(void)
{
	u32 saved_lpr, lpr;

	saved_lpr = at91_sys_read(AT91_DDRSDRC_LPR);

	lpr = saved_lpr & ~AT91_DDRSDRC_LPCB;
	at91_sys_write(AT91_DDRSDRC_LPR, lpr | AT91_DDRSDRC_LPCB_SELF_REFRESH);
	return saved_lpr;
}

#define sdram_selfrefresh_disable(saved_lpr)	at91_sys_write(AT91_DDRSDRC_LPR, saved_lpr)

#else
#include <mach/at91sam9_sdramc.h>

#ifdef CONFIG_ARCH_AT91SAM9263
/*
 * FIXME either or both the SDRAM controllers (EB0, EB1) might be in use;
 * handle those cases both here and in the Suspend-To-RAM support.
 */
#define	AT91_SDRAMC	AT91_SDRAMC0
#warning Assuming EB1 SDRAM controller is *NOT* used
#endif

static inline u32 sdram_selfrefresh_enable(void)
{
	u32 saved_lpr, lpr;

	saved_lpr = at91_sys_read(AT91_SDRAMC_LPR);

	lpr = saved_lpr & ~AT91_SDRAMC_LPCB;
	at91_sys_write(AT91_SDRAMC_LPR, lpr | AT91_SDRAMC_LPCB_SELF_REFRESH);
	return saved_lpr;
}

#define sdram_selfrefresh_disable(saved_lpr)	at91_sys_write(AT91_SDRAMC_LPR, saved_lpr)

#endif
