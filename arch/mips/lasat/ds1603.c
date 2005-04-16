/* 
 * Dallas Semiconductors 1603 RTC driver 
 *
 * Brian Murphy <brian@murphy.dk> 
 *
 */
#include <linux/kernel.h>
#include <asm/lasat/lasat.h>
#include <linux/delay.h>
#include <asm/lasat/ds1603.h>

#include "ds1603.h"

#define READ_TIME_CMD 0x81
#define SET_TIME_CMD 0x80
#define TRIMMER_SET_CMD 0xC0
#define TRIMMER_VALUE_MASK 0x38
#define TRIMMER_SHIFT 3

struct ds_defs *ds1603 = NULL;

/* HW specific register functions */
static void rtc_reg_write(unsigned long val) 
{
	*ds1603->reg = val;
}

static unsigned long rtc_reg_read(void) 
{
	unsigned long tmp = *ds1603->reg;
	return tmp;
}

static unsigned long rtc_datareg_read(void)
{
	unsigned long tmp = *ds1603->data_reg;
	return tmp;
}

static void rtc_nrst_high(void)
{
	rtc_reg_write(rtc_reg_read() | ds1603->rst);
}

static void rtc_nrst_low(void)
{
	rtc_reg_write(rtc_reg_read() & ~ds1603->rst);
}

static void rtc_cycle_clock(unsigned long data)
{
	data |= ds1603->clk;
	rtc_reg_write(data);
	lasat_ndelay(250);
	if (ds1603->data_reversed)
		data &= ~ds1603->data;
	else
		data |= ds1603->data;
	data &= ~ds1603->clk;
	rtc_reg_write(data);
	lasat_ndelay(250 + ds1603->huge_delay);
}

static void rtc_write_databit(unsigned int bit)
{
	unsigned long data = rtc_reg_read();
	if (ds1603->data_reversed)
		bit = !bit;
	if (bit)
		data |= ds1603->data;
	else
		data &= ~ds1603->data;

	rtc_reg_write(data);
	lasat_ndelay(50 + ds1603->huge_delay);
	rtc_cycle_clock(data);
}

static unsigned int rtc_read_databit(void)
{
	unsigned int data;

	data = (rtc_datareg_read() & (1 << ds1603->data_read_shift)) 
		>> ds1603->data_read_shift;
	rtc_cycle_clock(rtc_reg_read());
	return data;
}

static void rtc_write_byte(unsigned int byte)
{
	int i;

	for (i = 0; i<=7; i++) {
		rtc_write_databit(byte & 1L);
		byte >>= 1;
	}
}

static void rtc_write_word(unsigned long word)
{
	int i;

	for (i = 0; i<=31; i++) {
		rtc_write_databit(word & 1L);
		word >>= 1;
	}
}

static unsigned long rtc_read_word(void)
{
	int i;
	unsigned long word = 0;
	unsigned long shift = 0;

	for (i = 0; i<=31; i++) {
		word |= rtc_read_databit() << shift;
		shift++;
	}
	return word;
}

static void rtc_init_op(void)
{
	rtc_nrst_high();

	rtc_reg_write(rtc_reg_read() & ~ds1603->clk);

	lasat_ndelay(50);
}

static void rtc_end_op(void)
{
	rtc_nrst_low();
	lasat_ndelay(1000);
}

/* interface */
unsigned long ds1603_read(void)
{
	unsigned long word;
	rtc_init_op();
	rtc_write_byte(READ_TIME_CMD);
	word = rtc_read_word();
	rtc_end_op();
	return word;
}

int ds1603_set(unsigned long time)
{
	rtc_init_op();
	rtc_write_byte(SET_TIME_CMD);
	rtc_write_word(time);
	rtc_end_op();

	return 0;
}

void ds1603_set_trimmer(unsigned int trimval)
{
	rtc_init_op();
	rtc_write_byte(((trimval << TRIMMER_SHIFT) & TRIMMER_VALUE_MASK)
			| (TRIMMER_SET_CMD));
	rtc_end_op();
}

void ds1603_disable(void)
{
	ds1603_set_trimmer(TRIMMER_DISABLE_RTC);
}

void ds1603_enable(void)
{
	ds1603_set_trimmer(TRIMMER_DEFAULT);
}
