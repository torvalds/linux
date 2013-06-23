#include "pm_types.h"
#include "pm.h"

//for io-measure time
#define PORT_E_CONFIG (SW_VA_PORTC_IO_BASE + 0x90)
#define PORT_E_DATA (SW_VA_PORTC_IO_BASE + 0xa0)
#define PORT_CONFIG PORT_E_CONFIG
#define PORT_DATA PORT_E_DATA

volatile int  print_flag = 0;

void busy_waiting(void)
{
#if 1
	volatile __u32 loop_flag = 1;
	while(1 == loop_flag);
	
#endif
	return;
}

void fake_busy_waiting(void)
{
#if 1
	volatile __u32 loop_flag = 2;
	while(1 == loop_flag);
	
#endif
	return;
}


#define RTC_DATA_REG(x)				(SW_VA_TIMERC_IO_BASE + 0x120 + 4*(x))
#define lread(n)                    (*((volatile unsigned int *)(n)))
#define lwrite(addr,value)   (*((volatile unsigned int *)(addr)) = (value))

/*flag: 0 -- save to rtc; 1 -- check crc*/
void standby_dram_crc(int flag)
{
    int i, j;
    int *tmp = (int *)0x40000000;
    int crc = 0;
    int crc_offset_words = 0;
    crc_offset_words = lread(RTC_DATA_REG(7));
    crc_offset_words = crc_offset_words & 7;
    tmp += crc_offset_words;
    if (flag == 1)
    {
        lwrite(RTC_DATA_REG(7), crc_offset_words + 1);
    }
    for(i = 0; i < 8; i++)
    {
        crc = 0;
        for(j = 0; j < 128 * 1024 * 1024; j+=32)
        {
            crc += *tmp;
            tmp += 8;
        }
        if (flag == 0)
        {
            lwrite(RTC_DATA_REG(8 + i), crc);
        }
        else
        {
            if (crc != lread(RTC_DATA_REG(8 + i)))
            {
                printk("[%dM - %dM) dram crc err!\n", i*128, (i + 1)*128);
            }
            else
            {
                printk("[%dM - %dM) dram crc ok!\n", i*128, (i + 1)*128);
            }
        }
    }
}


/*
 * notice: dependant with perf counter to delay.
 */
void io_init(void)
{
	//config port output
	*(volatile unsigned int *)(PORT_CONFIG)  = 0x111111;
	
	return;
}

void io_init_high(void)
{
	__u32 data;
	
	//set port to high
	data = *(volatile unsigned int *)(PORT_DATA);
	data |= 0x3f;
	*(volatile unsigned int *)(PORT_DATA) = data;

	return;
}

void io_init_low(void)
{
	__u32 data;

	data = *(volatile unsigned int *)(PORT_DATA);
	//set port to low
	data &= 0xffffffc0;
	*(volatile unsigned int *)(PORT_DATA) = data;

	return;
}

/*
 * set pa port to high, num range is 0-7;	
 */
void io_high(int num)
{
	__u32 data;
	data = *(volatile unsigned int *)(PORT_DATA);
	//pull low 10ms
	data &= (~(1<<num));
	*(volatile unsigned int *)(PORT_DATA) = data;
	delay_us(10000);
	//pull high
	data |= (1<<num);
	*(volatile unsigned int *)(PORT_DATA) = data;

	return;
}
