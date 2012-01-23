/**
 * arch/arm/mach-softwinner/pm/standby/sun3i_sram_serial.c
 *
 *This application can only run in sram for allwin chips
 *It provides easy uart functions for debug
 *
 *author: yekai
 *date:2011-03-23
 *version:0.1
 */
#include <mach/platform.h>
#include <sun3i_standby.h>

#if __SRAM_DEBUG__
void aw_put_char(char val)
{
	while ((SW_UART0_USR & 0x2) == 0);
	SW_UART0_THR = val;
}

void easy_print_string(char *buf)
{
#if 0
	int len = strlen(buf);
	int i;

	for (i=0; i<len; i++) {
		my_put_char(buf[i]);
	}

	my_put_char('\r');
	my_put_char('\n');
#else
	char cc;
	int  i=0;

    	do{
		cc = buf[i++];
        	if(cc == 0)
	      		break;

		if (cc ==0x0a)
			aw_put_char(0x0d);
	 	aw_put_char(cc);
    	}while(1);
#endif
}

void easy_print_reg(char* buf, unsigned int reg)
{
	char cc,tmp;
	int i=0,j=0;

	do{
		cc = buf[i];

		if(cc == 0)
	      		break;

		if (cc == '\n'){
			aw_put_char(0x0d);
			aw_put_char(cc);
		}else if(cc == '%'){
			for(j=7;j>=0;j--){
				cc = (reg>>(4*j))&0xf;

				if(cc>=0&&cc<10)   // 0~9
					cc += 0x30;
                            	else
					cc += 87;
                    	 	aw_put_char(cc);
                        }
			i++;
		}else
			aw_put_char(cc);
		i ++;
    }while(1);
}
#endif  //__SRAM_DEBUG__

