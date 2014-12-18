
#include <linux/amlogic/efuse.h>
#include <linux/amlogic/saradc.h>
#include <mach/thermal.h>
#include <linux/slab.h>

#define  NOT_WRITE_EFUSE 0x0
#define EFUSE_MIGHT_WRONG 0x8
#define EFUEE_PRIVATE 0x4
#define EFUSE_OPS 0xa
struct temp_sensor{
	int flag;
	int trimming;
	int adc_efuse;
	int efuse_flag;
};
struct temp_sensor *temps;
int thermal_firmware_init(void)
{
	int err;
	char buf[4]={0};
	int temp=-1,TS_C=-1,flag=0;
	err=efuse_read_intlItem("temper_cvbs",buf,4);
	if(err>=0){
		printk("buf[0]=%x,buf[1]=%x,err=%d\n",buf[0],buf[1],err);
		temps=kzalloc(sizeof(*temps),GFP_KERNEL);
		temp=0;TS_C=0;
		temp=buf[1];
		temp=(temp<<8)|buf[0];
		TS_C=temp&0x1f;
		flag=0;
		flag=(temp&0x8000)>>15;
		temp=(temp&0x7fff)>>5;
		printk("adc=%d,TS_C=%d,flag=%d\n",temp,TS_C,flag);
		temps->flag=flag;
		temps->trimming=TS_C;
		temps->adc_efuse=temp;
		temps->efuse_flag=buf[3]>>4;
		printk("efuse_flag=%x\n",temps->efuse_flag);
		if((temps->efuse_flag == EFUEE_PRIVATE) ||(temps->efuse_flag == EFUSE_OPS)){
			if(temps->flag){
				temps->flag=1;
			}
		}else{
			temps->flag=0;
		}
	}
	else{
		temps->flag=flag;
		temps->trimming=TS_C;
		temps->adc_efuse=temp;
		temps->efuse_flag=-1;
	}
	if(temps->flag){
		temp_sensor_adc_init(temps->trimming);
		return 0;
	}
	else
		return -1;
	
}
int get_cpu_temp(void)
{
	int ret=-1,tempa;
	if(temps->flag){
		ret=get_adc_sample(6);
		if(ret>=0){
			tempa=(10*(ret-temps->adc_efuse))/32+27;
			ret=tempa;
		}
	}
	return ret;
}

