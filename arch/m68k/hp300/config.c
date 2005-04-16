/*
 *  linux/arch/m68k/hp300/config.c
 *
 *  Copyright (C) 1998 Philip Blundell <philb@gnu.org>
 *
 *  This file contains the HP300-specific initialisation code.  It gets
 *  called by setup.c.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/console.h>

#include <asm/bootinfo.h>
#include <asm/machdep.h>
#include <asm/blinken.h>
#include <asm/io.h>                               /* readb() and writeb() */
#include <asm/hp300hw.h>
#include <asm/rtc.h>

#include "ints.h"
#include "time.h"

unsigned long hp300_model;
unsigned long hp300_uart_scode = -1;
unsigned char ledstate;

static char s_hp330[] __initdata = "330";
static char s_hp340[] __initdata = "340";
static char s_hp345[] __initdata = "345";
static char s_hp360[] __initdata = "360";
static char s_hp370[] __initdata = "370";
static char s_hp375[] __initdata = "375";
static char s_hp380[] __initdata = "380";
static char s_hp385[] __initdata = "385";
static char s_hp400[] __initdata = "400";
static char s_hp425t[] __initdata = "425t";
static char s_hp425s[] __initdata = "425s";
static char s_hp425e[] __initdata = "425e";
static char s_hp433t[] __initdata = "433t";
static char s_hp433s[] __initdata = "433s";
static char *hp300_models[] __initdata = {
	[HP_320]	= NULL,
	[HP_330]	= s_hp330,
	[HP_340]	= s_hp340,
	[HP_345]	= s_hp345,
	[HP_350]	= NULL,
	[HP_360]	= s_hp360,
	[HP_370]	= s_hp370,
	[HP_375]	= s_hp375,
	[HP_380]	= s_hp380,
	[HP_385]	= s_hp385,
	[HP_400]	= s_hp400,
	[HP_425T]	= s_hp425t,
	[HP_425S]	= s_hp425s,
	[HP_425E]	= s_hp425e,
	[HP_433T]	= s_hp433t,
	[HP_433S]	= s_hp433s,
};

static char hp300_model_name[13] = "HP9000/";

extern void hp300_reset(void);
extern irqreturn_t (*hp300_default_handler[])(int, void *, struct pt_regs *);
extern int show_hp300_interrupts(struct seq_file *, void *);
#ifdef CONFIG_SERIAL_8250_CONSOLE
extern int hp300_setup_serial_console(void) __init;
#endif

int __init hp300_parse_bootinfo(const struct bi_record *record)
{
	int unknown = 0;
	const unsigned long *data = record->data;

	switch (record->tag) {
	case BI_HP300_MODEL:
		hp300_model = *data;
		break;

	case BI_HP300_UART_SCODE:
		hp300_uart_scode = *data;
		break;

	case BI_HP300_UART_ADDR:
		/* serial port address: ignored here */
		break;

        default:
		unknown = 1;
	}

	return unknown;
}

#ifdef CONFIG_HEARTBEAT
static void hp300_pulse(int x)
{
	if (x)
		blinken_leds(0x10, 0);
	else
		blinken_leds(0, 0x10);
}
#endif

static void hp300_get_model(char *model)
{
	strcpy(model, hp300_model_name);
}

#define RTCBASE			0xf0420000
#define RTC_DATA		0x1
#define RTC_CMD			0x3

#define	RTC_BUSY		0x02
#define	RTC_DATA_RDY		0x01

#define rtc_busy()		(in_8(RTCBASE + RTC_CMD) & RTC_BUSY)
#define rtc_data_available()	(in_8(RTCBASE + RTC_CMD) & RTC_DATA_RDY)
#define rtc_status()		(in_8(RTCBASE + RTC_CMD))
#define rtc_command(x)		out_8(RTCBASE + RTC_CMD, (x))
#define rtc_read_data()		(in_8(RTCBASE + RTC_DATA))
#define rtc_write_data(x)	out_8(RTCBASE + RTC_DATA, (x))

#define RTC_SETREG	0xe0
#define RTC_WRITEREG	0xc2
#define RTC_READREG	0xc3

#define RTC_REG_SEC2	0
#define RTC_REG_SEC1	1
#define RTC_REG_MIN2	2
#define RTC_REG_MIN1	3
#define RTC_REG_HOUR2	4
#define RTC_REG_HOUR1	5
#define RTC_REG_WDAY	6
#define RTC_REG_DAY2	7
#define RTC_REG_DAY1	8
#define RTC_REG_MON2	9
#define RTC_REG_MON1	10
#define RTC_REG_YEAR2	11
#define RTC_REG_YEAR1	12

#define RTC_HOUR1_24HMODE 0x8

#define RTC_STAT_MASK	0xf0
#define RTC_STAT_RDY	0x40

static inline unsigned char hp300_rtc_read(unsigned char reg)
{
	unsigned char s, ret;
	unsigned long flags;

	local_irq_save(flags);

	while (rtc_busy());
	rtc_command(RTC_SETREG);
	while (rtc_busy());
	rtc_write_data(reg);
	while (rtc_busy());
	rtc_command(RTC_READREG);

	do {
		while (!rtc_data_available());
		s = rtc_status();
		ret = rtc_read_data();
	} while ((s & RTC_STAT_MASK) != RTC_STAT_RDY);

	local_irq_restore(flags);

	return ret;
}

static inline unsigned char hp300_rtc_write(unsigned char reg,
					    unsigned char val)
{
	unsigned char s, ret;
	unsigned long flags;

	local_irq_save(flags);

	while (rtc_busy());
	rtc_command(RTC_SETREG);
	while (rtc_busy());
	rtc_write_data((val << 4) | reg);
	while (rtc_busy());
	rtc_command(RTC_WRITEREG);
	while (rtc_busy());
	rtc_command(RTC_READREG);

	do {
		while (!rtc_data_available());
		s = rtc_status();
		ret = rtc_read_data();
	} while ((s & RTC_STAT_MASK) != RTC_STAT_RDY);

	local_irq_restore(flags);

	return ret;
}

static int hp300_hwclk(int op, struct rtc_time *t)
{
	if (!op) { /* read */
		t->tm_sec  = hp300_rtc_read(RTC_REG_SEC1) * 10 +
			hp300_rtc_read(RTC_REG_SEC2);
		t->tm_min  = hp300_rtc_read(RTC_REG_MIN1) * 10 +
			hp300_rtc_read(RTC_REG_MIN2);
		t->tm_hour = (hp300_rtc_read(RTC_REG_HOUR1) & 3) * 10 +
			hp300_rtc_read(RTC_REG_HOUR2);
		t->tm_wday = -1;
		t->tm_mday = hp300_rtc_read(RTC_REG_DAY1) * 10 +
			hp300_rtc_read(RTC_REG_DAY2);
		t->tm_mon  = hp300_rtc_read(RTC_REG_MON1) * 10 +
			hp300_rtc_read(RTC_REG_MON2) - 1;
		t->tm_year = hp300_rtc_read(RTC_REG_YEAR1) * 10 +
			hp300_rtc_read(RTC_REG_YEAR2);
		if (t->tm_year <= 69)
			t->tm_year += 100;
	} else {
		hp300_rtc_write(RTC_REG_SEC1, t->tm_sec / 10);
		hp300_rtc_write(RTC_REG_SEC2, t->tm_sec % 10);
		hp300_rtc_write(RTC_REG_MIN1, t->tm_min / 10);
		hp300_rtc_write(RTC_REG_MIN2, t->tm_min % 10);
		hp300_rtc_write(RTC_REG_HOUR1,
				((t->tm_hour / 10) & 3) | RTC_HOUR1_24HMODE);
		hp300_rtc_write(RTC_REG_HOUR2, t->tm_hour % 10);
		hp300_rtc_write(RTC_REG_DAY1, t->tm_mday / 10);
		hp300_rtc_write(RTC_REG_DAY2, t->tm_mday % 10);
		hp300_rtc_write(RTC_REG_MON1, (t->tm_mon + 1) / 10);
		hp300_rtc_write(RTC_REG_MON2, (t->tm_mon + 1) % 10);
		if (t->tm_year >= 100)
			t->tm_year -= 100;
		hp300_rtc_write(RTC_REG_YEAR1, t->tm_year / 10);
		hp300_rtc_write(RTC_REG_YEAR2, t->tm_year % 10);
	}

	return 0;
}

static unsigned int hp300_get_ss(void)
{
	return hp300_rtc_read(RTC_REG_SEC1) * 10 +
		hp300_rtc_read(RTC_REG_SEC2);
}

void __init config_hp300(void)
{
	mach_sched_init      = hp300_sched_init;
	mach_init_IRQ        = hp300_init_IRQ;
	mach_request_irq     = hp300_request_irq;
	mach_free_irq        = hp300_free_irq;
	mach_get_model       = hp300_get_model;
	mach_get_irq_list    = show_hp300_interrupts;
	mach_gettimeoffset   = hp300_gettimeoffset;
	mach_default_handler = &hp300_default_handler;
	mach_hwclk	     = hp300_hwclk;
	mach_get_ss	     = hp300_get_ss;
	mach_reset           = hp300_reset;
#ifdef CONFIG_HEARTBEAT
	mach_heartbeat       = hp300_pulse;
#endif
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp	     = &dummy_con;
#endif
	mach_max_dma_address = 0xffffffff;

	if (hp300_model >= HP_330 && hp300_model <= HP_433S && hp300_model != HP_350) {
		printk(KERN_INFO "Detected HP9000 model %s\n", hp300_models[hp300_model-HP_320]);
		strcat(hp300_model_name, hp300_models[hp300_model-HP_320]);
	}
	else {
		panic("Unknown HP9000 Model");
	}
#ifdef CONFIG_SERIAL_8250_CONSOLE
	hp300_setup_serial_console();
#endif
}
