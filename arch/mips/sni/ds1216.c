
#include <linux/bcd.h>
#include <linux/time.h>

#include <asm/ds1216.h>

volatile unsigned char *ds1216_base;

/*
 * Read the 64 bit we'd like to have - It a series
 * of 64 bits showing up in the LSB of the base register.
 *
 */
static unsigned char *ds1216_read(void)
{
	static unsigned char	rdbuf[8];
	unsigned char		c;
	int			i, j;

	for (i = 0; i < 8; i++) {
		c = 0x0;
		for (j = 0; j < 8; j++) {
			c |= (*ds1216_base & 0x1) << j;
		}
		rdbuf[i] = c;
	}

	return rdbuf;
}

static void ds1216_switch_ds_to_clock(void)
{
	unsigned char magic[] = {
		0xc5, 0x3a, 0xa3, 0x5c, 0xc5, 0x3a, 0xa3, 0x5c
	};
	int i,j,c;

	/* Reset magic pointer */
	c = *ds1216_base;

	/* Write 64 bit magic to DS1216 */
	for (i = 0; i < 8; i++) {
		c = magic[i];
		for (j = 0; j < 8; j++) {
			*ds1216_base = c;
			c = c >> 1;
		}
	}
}

unsigned long ds1216_get_cmos_time(void)
{
	unsigned char	*rdbuf;
	unsigned int	year, month, date, hour, min, sec;

	ds1216_switch_ds_to_clock();
	rdbuf = ds1216_read();

	sec = BCD2BIN(DS1216_SEC(rdbuf));
	min = BCD2BIN(DS1216_MIN(rdbuf));
	hour = BCD2BIN(DS1216_HOUR(rdbuf));
	date = BCD2BIN(DS1216_DATE(rdbuf));
	month = BCD2BIN(DS1216_MONTH(rdbuf));
	year = BCD2BIN(DS1216_YEAR(rdbuf));

	if (DS1216_1224(rdbuf) && DS1216_AMPM(rdbuf))
		hour+=12;

	if (year < 70)
		year += 2000;
	else
		year += 1900;

	return mktime(year, month, date, hour, min, sec);
}

int ds1216_set_rtc_mmss(unsigned long nowtime)
{
	printk("ds1216_set_rtc_mmss called but not implemented\n");
	return -1;
}
