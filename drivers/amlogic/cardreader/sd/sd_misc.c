#include "sd_misc.h"
#include "sd_port.h"
#include "sd_protocol.h"

static unsigned long start_time,timeout_value,overflow_cnt,timeout_flag;
static unsigned long last_time_diff,time_diff,time_diff,cur_time;

unsigned int cal_crc(unsigned char *ptr, unsigned int len);
unsigned char cal_crc7(unsigned char *ptr, unsigned int len);
unsigned char very_crc7(unsigned char *ptr, unsigned int len);

void sd_start_timer(unsigned long time_value)
{
	timeout_value = time_value;
	overflow_cnt = 0;
	last_time_diff = 0;
	timeout_flag = 0;
	start_time = sd_get_timer_tick();
}

int sd_check_timer()
{
	cur_time = sd_get_timer_tick();
	
	if(cur_time < start_time)
	{
		time_diff = SD_MAX_TIMER_TICK - start_time + cur_time + 1;
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

int sd_check_timeout()
{
	return timeout_flag;
}

#define US 1000
#define MS (1000*1000)
#define FREQ_OP     5000000   //Max software clock freq: 3MHz, never will exceed this value
static unsigned time_unit[] = { 1,10,100,1*US,10*US, 100*US, 1*MS, 10*MS };
static unsigned ts_mul[] = { 0,    1000, 1200, 1300, 1500, 2000, 2500, 3000, 
   3500, 4000, 4500, 5000, 5500, 6000, 7000, 8000 };

unsigned long sd_mmc_TAAC( unsigned char ts )
{
	unsigned long rate = time_unit[(ts & 0x7)] *( ts_mul[(ts & 0x78) >> 3]/100)/10;

	return rate;
}

/*void sd_delay_us(unsigned long num_us)
{
	unsigned int i = 0, j = 0;
	for(i=0; i<num_us; i++)
		for(j=0; j<20; j++);
}*/

/*void sd_delay_ms(unsigned long num_ms)
{
	unsigned long i;
	for(i = 0; i < num_ms; i++)
	{
		sd_delay_us(1000);
	}
}*/

//calculate Nac cycles in clock
unsigned long sd_cal_clks_nac(unsigned char TAAC, unsigned char NSAC)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		return 3*(sd_mmc_TAAC(TAAC)/1000*FREQ_OP/1000000 + NSAC*100);
#endif
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		return 10*(sd_mmc_TAAC(TAAC)/1000*FREQ_OP/1000000 + NSAC*100);
#endif
	return 3*(sd_mmc_TAAC(TAAC)/1000*FREQ_OP/1000000 + NSAC*100);
}


#define	CRC7		0x89 //x7+x3+x0
#define CRCR		7
#define MAX_BIT		256

unsigned char cal_crc7(unsigned char *ptr, unsigned int len)
{
	unsigned char i,bitlen=0,j;
	unsigned char crc=0;
	//unsigned long data=0;
	unsigned char bitdata[MAX_BIT];
	
	if((len*8+CRCR)>MAX_BIT)
		return 1;
		
	bitlen=len*8+CRCR;
		//	data=(*ptr)<<24+(*ptr++)<<16+(*ptr++)<<8+(*ptr++);
	j=0;
	while(len--!=0) //translate to bitdata
	{
		for(i=0x80; i!=0; i/=2)
		{
			if((*ptr&i)!=0)
				bitdata[j]=1;
			else
				bitdata[j]=0;
			j++;
		}
		ptr++;
	}
	for(i=0;i<CRCR;i++)   //add r 0
	{
		bitdata[j]=0;
		j++;
	}
	j=0;
	while(bitlen--!=0) //translate to bitdata
	{
		if(crc&0x80)
			crc^=CRC7;
		crc*=2;
		crc|=(bitdata[j]);
		j++;

	}
	if(crc&0x80)
		crc^=CRC7;
	crc*=2;

	return(crc);
}
unsigned char very_crc7(unsigned char *ptr, unsigned int len)
{
	unsigned char i,bitlen=0,j;
	unsigned char crc=0;
	//unsigned long data=0;
	unsigned char bitdata[MAX_BIT];
	if((len*8)>MAX_BIT)
		return 1;
		
	bitlen=len*8-1;
       //	data=(*ptr)<<24+(*ptr++)<<16+(*ptr++)<<8+(*ptr++);
	j=0;
	while(len--!=0) //translate to bitdata
	{
		for(i=0x80; i!=0; i/=2)
		{
			if((*ptr&i)!=0)
				bitdata[j++]=1;
			else
				bitdata[j++]=0;
		}
		ptr++;
	}
	j=0;
	while(bitlen--!=0) //translate to bitdata
	{
		if(crc&0x80)
			crc^=CRC7;
		crc*=2;
		crc|=(bitdata[j]);
		j++;
	}
	if(crc&0x80)
		crc^=CRC7;

	return(crc);
}

unsigned int cal_crc(unsigned char *ptr, unsigned int len)
{
	unsigned char i;
	unsigned int crc=0;
	while(len--!=0)
	{
		for(i=0x80; i!=0; i/=2)
		{
			if((crc&0x8000)!=0) {crc*=2; crc^=0x1021;}   /* 余式CRC乘以2再求CRC  */
				else crc*=2;
			if((*ptr&i)!=0) crc^=0x1021;                /* 再加上本位的CRC */
		}
		ptr++;
	}
	return(crc);
}

unsigned int very_crc(unsigned char *ptr, unsigned int len)
{
	return 0;
}

unsigned char sd_verify_crc7(unsigned char *ptr, unsigned int len)
{
	return very_crc7(ptr, len);
}

unsigned short sd_verify_crc16(unsigned char *ptr, unsigned int len)
{
	//return very_crc(ptr, len);
	return 0;
}

unsigned char sd_cal_crc7(unsigned char *ptr, unsigned int len)
{
	return cal_crc7(ptr, len);
}

unsigned short sd_cal_crc16(unsigned char *ptr, unsigned int len)
{
	return cal_crc(ptr, len);
}

unsigned short sd_cal_crc_mode(unsigned char *ptr, unsigned int len, unsigned char *mode) 
{
	unsigned short crc=0;
	unsigned char i;
	while(len--!=0)
	{
		for(i = 0; mode[i] != 0 && i < 8; i++)
		{
			if((crc&0x8000) != 0)
			{
				crc <<= 1;
				crc ^= 0x1021;
			}   /* 余式CRC乘以2再求CRC  */
			else
				crc <<= 1;
		
			if((*ptr&mode[i]) != 0) crc ^= 0x1021;                /* 再加上本位的CRC */
		}
		ptr++;
	}
	return(crc);
}

const unsigned short sd_crc_table[256] = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
  0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
  0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
  0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
  0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
  
  0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
  0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
  0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
  0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
  0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
  0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
  0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
  
  0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
  0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
  0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
  0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
  0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
  0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
  0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
  
  0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
  0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
  0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
  0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
  0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
  0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
  0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
  0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};
