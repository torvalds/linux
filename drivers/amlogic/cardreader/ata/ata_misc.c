#include "ata_misc.h"

static unsigned long start_time,timeout_value,overflow_cnt,timeout_flag;
static unsigned long last_time_diff,time_diff,time_diff,cur_time;

void ata_start_timer(unsigned long time_value)
{
	timeout_value = time_value;
	overflow_cnt = 0;
	last_time_diff = 0;
	timeout_flag = 0;
	start_time = ata_get_timer_tick();
}

int ata_check_timer()
{
	cur_time = ata_get_timer_tick();
	
	if(cur_time < start_time)
	{
		time_diff = ATA_MAX_TIMER_TICK - start_time + cur_time + 1;
	}
	else
	{
		time_diff = cur_time - start_time;
	}
	
	if(last_time_diff > time_diff)
	{
		overflow_cnt++;
	}
	last_time_diff = time_diff;

	time_diff += (overflow_cnt << 24);
	
	if(time_diff >= timeout_value)
	{
		timeout_flag = 1;
		return 1;
	}
	else
	{
		return 0;
	}
}

int ata_check_timeout()
{
	return timeout_flag;
}

/*void ata_delay_100ns(unsigned long num_100ns)
{
	unsigned long i, j;
	for(i = 0; i < num_100ns; i++)
       for(j = 0; j < card_100ns_circle_num; j++);
}*/

unsigned char read_pio_8(unsigned reg)
{
	volatile unsigned *addr;

	reg = ATABASE + (reg << 2);
	addr = (unsigned *)reg;
	
	return IO_READ32(addr);
}

void write_pio_8(unsigned reg, unsigned char val)
{
	volatile unsigned *addr;
	unsigned data = val;
	
	reg = ATABASE + (reg << 2);
	addr = (unsigned *)reg;
	
	*addr = data;
}

unsigned short read_pio_16(unsigned reg)
{
	volatile unsigned *addr;
	
	reg = ATABASE + (reg << 2);
	addr = (unsigned *)reg;
	
	return IO_READ32(addr);
}

void write_pio_16(unsigned reg, unsigned short val)
{
	volatile unsigned *addr;
	unsigned data = val;
	
	reg = ATABASE + (reg << 2);
	addr = (unsigned *)reg;
	
	*addr = data;
}

