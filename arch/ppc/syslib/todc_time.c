/*
 * arch/ppc/syslib/todc_time.c
 *
 * Time of Day Clock support for the M48T35, M48T37, M48T59, and MC146818
 * Real Time Clocks/Timekeepers.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/bcd.h>
#include <linux/mc146818rtc.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/todc.h>

/*
 * Depending on the hardware on your board and your board design, the
 * RTC/NVRAM may be accessed either directly (like normal memory) or via
 * address/data registers.  If your board uses the direct method, set
 * 'nvram_data' to the base address of your nvram and leave 'nvram_as0' and
 * 'nvram_as1' NULL.  If your board uses address/data regs to access nvram,
 * set 'nvram_as0' to the address of the lower byte, set 'nvram_as1' to the
 * address of the upper byte (leave NULL if using mc146818), and set
 * 'nvram_data' to the address of the 8-bit data register.
 *
 * In order to break the assumption that the RTC and NVRAM are accessed by
 * the same mechanism, you need to explicitly set 'ppc_md.rtc_read_val' and
 * 'ppc_md.rtc_write_val', otherwise the values of 'ppc_md.rtc_read_val'
 * and 'ppc_md.rtc_write_val' will be used.
 *
 * Note: Even though the documentation for the various RTC chips say that it
 * 	 take up to a second before it starts updating once the 'R' bit is
 * 	 cleared, they always seem to update even though we bang on it many
 * 	 times a second.  This is true, except for the Dallas Semi 1746/1747
 * 	 (possibly others).  Those chips seem to have a real problem whenever
 * 	 we set the 'R' bit before reading them, they basically stop counting.
 * 	 					--MAG
 */

/*
 * 'todc_info' should be initialized in your *_setup.c file to
 * point to a fully initialized 'todc_info_t' structure.
 * This structure holds all the register offsets for your particular
 * TODC/RTC chip.
 * TODC_ALLOC()/TODC_INIT() will allocate and initialize this table for you.
 */

#ifdef	RTC_FREQ_SELECT
#undef	RTC_FREQ_SELECT
#define	RTC_FREQ_SELECT		control_b	/* Register A */
#endif

#ifdef	RTC_CONTROL
#undef	RTC_CONTROL
#define	RTC_CONTROL		control_a	/* Register B */
#endif

#ifdef	RTC_INTR_FLAGS
#undef	RTC_INTR_FLAGS
#define	RTC_INTR_FLAGS		watchdog	/* Register C */
#endif

#ifdef	RTC_VALID
#undef	RTC_VALID
#define	RTC_VALID		interrupts	/* Register D */
#endif

/* Access routines when RTC accessed directly (like normal memory) */
u_char
todc_direct_read_val(int addr)
{
	return readb((void __iomem *)(todc_info->nvram_data + addr));
}

void
todc_direct_write_val(int addr, unsigned char val)
{
	writeb(val, (void __iomem *)(todc_info->nvram_data + addr));
	return;
}

/* Access routines for accessing m48txx type chips via addr/data regs */
u_char
todc_m48txx_read_val(int addr)
{
	outb(addr, todc_info->nvram_as0);
	outb(addr>>todc_info->as0_bits, todc_info->nvram_as1);
	return inb(todc_info->nvram_data);
}

void
todc_m48txx_write_val(int addr, unsigned char val)
{
	outb(addr, todc_info->nvram_as0);
	outb(addr>>todc_info->as0_bits, todc_info->nvram_as1);
   	outb(val, todc_info->nvram_data);
	return;
}

/* Access routines for accessing mc146818 type chips via addr/data regs */
u_char
todc_mc146818_read_val(int addr)
{
	outb_p(addr, todc_info->nvram_as0);
	return inb_p(todc_info->nvram_data);
}

void
todc_mc146818_write_val(int addr, unsigned char val)
{
	outb_p(addr, todc_info->nvram_as0);
   	outb_p(val, todc_info->nvram_data);
}


/*
 * Routines to make RTC chips with NVRAM buried behind an addr/data pair
 * have the NVRAM and clock regs appear at the same level.
 * The NVRAM will appear to start at addr 0 and the clock regs will appear
 * to start immediately after the NVRAM (actually, start at offset
 * todc_info->nvram_size).
 */
static inline u_char
todc_read_val(int addr)
{
	u_char	val;

	if (todc_info->sw_flags & TODC_FLAG_2_LEVEL_NVRAM) {
		if (addr < todc_info->nvram_size) { /* NVRAM */
			ppc_md.rtc_write_val(todc_info->nvram_addr_reg, addr);
			val = ppc_md.rtc_read_val(todc_info->nvram_data_reg);
		}
		else { /* Clock Reg */
			addr -= todc_info->nvram_size;
			val = ppc_md.rtc_read_val(addr);
		}
	}
	else {
		val = ppc_md.rtc_read_val(addr);
	}

	return val;
}

static inline void
todc_write_val(int addr, u_char val)
{
	if (todc_info->sw_flags & TODC_FLAG_2_LEVEL_NVRAM) {
		if (addr < todc_info->nvram_size) { /* NVRAM */
			ppc_md.rtc_write_val(todc_info->nvram_addr_reg, addr);
			ppc_md.rtc_write_val(todc_info->nvram_data_reg, val);
		}
		else { /* Clock Reg */
			addr -= todc_info->nvram_size;
			ppc_md.rtc_write_val(addr, val);
		}
	}
	else {
		ppc_md.rtc_write_val(addr, val);
	}
}

/*
 * TODC routines
 *
 * There is some ugly stuff in that there are assumptions for the mc146818.
 *
 * Assumptions:
 *	- todc_info->control_a has the offset as mc146818 Register B reg
 *	- todc_info->control_b has the offset as mc146818 Register A reg
 *	- m48txx control reg's write enable or 'W' bit is same as
 *	  mc146818 Register B 'SET' bit (i.e., 0x80)
 *
 * These assumptions were made to make the code simpler.
 */
long __init
todc_time_init(void)
{
	u_char	cntl_b;

	if (!ppc_md.rtc_read_val)
		ppc_md.rtc_read_val = ppc_md.nvram_read_val;
	if (!ppc_md.rtc_write_val)
		ppc_md.rtc_write_val = ppc_md.nvram_write_val;
	
	cntl_b = todc_read_val(todc_info->control_b);

	if (todc_info->rtc_type == TODC_TYPE_MC146818) {
		if ((cntl_b & 0x70) != 0x20) {
			printk(KERN_INFO "TODC %s %s\n",
				"real-time-clock was stopped.",
				"Now starting...");
			cntl_b &= ~0x70;
			cntl_b |= 0x20;
		}

		todc_write_val(todc_info->control_b, cntl_b);
	} else if (todc_info->rtc_type == TODC_TYPE_DS17285) {
		u_char mode;

		mode = todc_read_val(TODC_TYPE_DS17285_CNTL_A);
		/* Make sure countdown clear is not set */
		mode &= ~0x40;
		/* Enable oscillator, extended register set */
		mode |= 0x30;
		todc_write_val(TODC_TYPE_DS17285_CNTL_A, mode);

	} else if (todc_info->rtc_type == TODC_TYPE_DS1501) {
		u_char	month;

		todc_info->enable_read = TODC_DS1501_CNTL_B_TE;
		todc_info->enable_write = TODC_DS1501_CNTL_B_TE;

		month = todc_read_val(todc_info->month);

		if ((month & 0x80) == 0x80) {
			printk(KERN_INFO "TODC %s %s\n",
				"real-time-clock was stopped.",
				"Now starting...");
			month &= ~0x80;
			todc_write_val(todc_info->month, month);
		}

		cntl_b &= ~TODC_DS1501_CNTL_B_TE;
		todc_write_val(todc_info->control_b, cntl_b);
	} else { /* must be a m48txx type */
		u_char	cntl_a;

		todc_info->enable_read = TODC_MK48TXX_CNTL_A_R;
		todc_info->enable_write = TODC_MK48TXX_CNTL_A_W;

		cntl_a = todc_read_val(todc_info->control_a);

		/* Check & clear STOP bit in control B register */
		if (cntl_b & TODC_MK48TXX_DAY_CB) {
			printk(KERN_INFO "TODC %s %s\n",
				"real-time-clock was stopped.",
				"Now starting...");

			cntl_a |= todc_info->enable_write;
			cntl_b &= ~TODC_MK48TXX_DAY_CB;/* Start Oscil */

			todc_write_val(todc_info->control_a, cntl_a);
			todc_write_val(todc_info->control_b, cntl_b);
		}

		/* Make sure READ & WRITE bits are cleared. */
		cntl_a &= ~(todc_info->enable_write |
			    todc_info->enable_read);
		todc_write_val(todc_info->control_a, cntl_a);
	}

	return 0;
}

/*
 * There is some ugly stuff in that there are assumptions that for a mc146818,
 * the todc_info->control_a has the offset of the mc146818 Register B reg and
 * that the register'ss 'SET' bit is the same as the m48txx's write enable
 * bit in the control register of the m48txx (i.e., 0x80).
 *
 * It was done to make the code look simpler.
 */
ulong
todc_get_rtc_time(void)
{
	uint	year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
	uint	limit, i;
	u_char	save_control, uip = 0;

	spin_lock(&rtc_lock);
	save_control = todc_read_val(todc_info->control_a);

	if (todc_info->rtc_type != TODC_TYPE_MC146818) {
		limit = 1;

		switch (todc_info->rtc_type) {
			case TODC_TYPE_DS1553:
			case TODC_TYPE_DS1557:
			case TODC_TYPE_DS1743:
			case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
			case TODC_TYPE_DS1747:
			case TODC_TYPE_DS17285:
				break;
			default:
				todc_write_val(todc_info->control_a,
				       (save_control | todc_info->enable_read));
		}
	}
	else {
		limit = 100000000;
	}

	for (i=0; i<limit; i++) {
		if (todc_info->rtc_type == TODC_TYPE_MC146818) {
			uip = todc_read_val(todc_info->RTC_FREQ_SELECT);
		}

		sec = todc_read_val(todc_info->seconds) & 0x7f;
		min = todc_read_val(todc_info->minutes) & 0x7f;
		hour = todc_read_val(todc_info->hours) & 0x3f;
		day = todc_read_val(todc_info->day_of_month) & 0x3f;
		mon = todc_read_val(todc_info->month) & 0x1f;
		year = todc_read_val(todc_info->year) & 0xff;

		if (todc_info->rtc_type == TODC_TYPE_MC146818) {
			uip |= todc_read_val(todc_info->RTC_FREQ_SELECT);
			if ((uip & RTC_UIP) == 0) break;
		}
	}

	if (todc_info->rtc_type != TODC_TYPE_MC146818) {
		switch (todc_info->rtc_type) {
			case TODC_TYPE_DS1553:
			case TODC_TYPE_DS1557:
			case TODC_TYPE_DS1743:
			case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
			case TODC_TYPE_DS1747:
			case TODC_TYPE_DS17285:
				break;
			default:
				save_control &= ~(todc_info->enable_read);
				todc_write_val(todc_info->control_a,
						       save_control);
		}
	}
	spin_unlock(&rtc_lock);

	if ((todc_info->rtc_type != TODC_TYPE_MC146818) ||
	    ((save_control & RTC_DM_BINARY) == 0) ||
	    RTC_ALWAYS_BCD) {

		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}

	year = year + 1900;
	if (year < 1970) {
		year += 100;
	}

	return mktime(year, mon, day, hour, min, sec);
}

int
todc_set_rtc_time(unsigned long nowtime)
{
	struct rtc_time	tm;
	u_char		save_control, save_freq_select = 0;

	spin_lock(&rtc_lock);
	to_tm(nowtime, &tm);

	save_control = todc_read_val(todc_info->control_a);

	/* Assuming MK48T59_RTC_CA_WRITE & RTC_SET are equal */
	todc_write_val(todc_info->control_a,
			       (save_control | todc_info->enable_write));
	save_control &= ~(todc_info->enable_write); /* in case it was set */

	if (todc_info->rtc_type == TODC_TYPE_MC146818) {
		save_freq_select = todc_read_val(todc_info->RTC_FREQ_SELECT);
		todc_write_val(todc_info->RTC_FREQ_SELECT,
				       save_freq_select | RTC_DIV_RESET2);
	}


        tm.tm_year = (tm.tm_year - 1900) % 100;

	if ((todc_info->rtc_type != TODC_TYPE_MC146818) ||
	    ((save_control & RTC_DM_BINARY) == 0) ||
	    RTC_ALWAYS_BCD) {

		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}

	todc_write_val(todc_info->seconds,      tm.tm_sec);
	todc_write_val(todc_info->minutes,      tm.tm_min);
	todc_write_val(todc_info->hours,        tm.tm_hour);
	todc_write_val(todc_info->month,        tm.tm_mon);
	todc_write_val(todc_info->day_of_month, tm.tm_mday);
	todc_write_val(todc_info->year,         tm.tm_year);

	todc_write_val(todc_info->control_a, save_control);

	if (todc_info->rtc_type == TODC_TYPE_MC146818) {
		todc_write_val(todc_info->RTC_FREQ_SELECT, save_freq_select);
	}
	spin_unlock(&rtc_lock);

	return 0;
}

/*
 * Manipulates read bit to reliably read seconds at a high rate.
 */
static unsigned char __init todc_read_timereg(int addr)
{
	unsigned char save_control = 0, val;

	switch (todc_info->rtc_type) {
		case TODC_TYPE_DS1553:
		case TODC_TYPE_DS1557:
		case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
		case TODC_TYPE_DS1747:
		case TODC_TYPE_DS17285:
		case TODC_TYPE_MC146818:
			break;
		default:
			save_control = todc_read_val(todc_info->control_a);
			todc_write_val(todc_info->control_a,
				       (save_control | todc_info->enable_read));
	}
	val = todc_read_val(addr);

	switch (todc_info->rtc_type) {
		case TODC_TYPE_DS1553:
		case TODC_TYPE_DS1557:
		case TODC_TYPE_DS1746:	/* XXXX BAD HACK -> FIX */
		case TODC_TYPE_DS1747:
		case TODC_TYPE_DS17285:
		case TODC_TYPE_MC146818:
			break;
		default:
			save_control &= ~(todc_info->enable_read);
			todc_write_val(todc_info->control_a, save_control);
	}

	return val;
}

/*
 * This was taken from prep_setup.c
 * Use the NVRAM RTC to time a second to calibrate the decrementer.
 */
void __init
todc_calibrate_decr(void)
{
	ulong	freq;
	ulong	tbl, tbu;
        long	i, loop_count;
        u_char	sec;

	todc_time_init();

	/*
	 * Actually this is bad for precision, we should have a loop in
	 * which we only read the seconds counter. todc_read_val writes
	 * the address bytes on every call and this takes a lot of time.
	 * Perhaps an nvram_wait_change method returning a time
	 * stamp with a loop count as parameter would be the solution.
	 */
	/*
	 * Need to make sure the tbl doesn't roll over so if tbu increments
	 * during this test, we need to do it again.
	 */
	loop_count = 0;

	sec = todc_read_timereg(todc_info->seconds) & 0x7f;

	do {
		tbu = get_tbu();

		for (i = 0 ; i < 10000000 ; i++) {/* may take up to 1 second */
		   tbl = get_tbl();

		   if ((todc_read_timereg(todc_info->seconds) & 0x7f) != sec) {
		      break;
		   }
		}

		sec = todc_read_timereg(todc_info->seconds) & 0x7f;

		for (i = 0 ; i < 10000000 ; i++) { /* Should take 1 second */
		   freq = get_tbl();

		   if ((todc_read_timereg(todc_info->seconds) & 0x7f) != sec) {
		      break;
		   }
		}

		freq -= tbl;
	} while ((get_tbu() != tbu) && (++loop_count < 2));

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}
