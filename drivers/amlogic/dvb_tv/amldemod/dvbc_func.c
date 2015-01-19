#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "aml_demod.h"
#include "demod_func.h"
#include <linux/kthread.h>



static struct task_struct *cci_task;
int cciflag=0;
struct timer_list mytimer;
static void dvbc_cci_timer(unsigned long data)
{

#if 1
	int count;
	int maxCCI_p,re,im,j,i,times,maxCCI,sum,sum1,reg_0xf0,tmp1,tmp,tmp2,reg_0xa8,reg_0xac;
	int reg_0xa8_t, reg_0xac_t;
	count=100;
//	while(1){
		    // search cci((si2176_get_strength()-256)<(-85))
		if((((apb_read_reg(QAM_BASE+0x18))&0x1)==1)){
				printk("[cci]lock ");
				if(cciflag==0){
					  apb_write_reg(QAM_BASE+0xa8, 0);

					  cciflag=0;
				}
				 printk("\n");
				 mdelay(500);
			mod_timer(&mytimer, jiffies + 2*HZ);
			return 0;
		}
		 if(cciflag==1){
		 	printk("[cci]cciflag is 1,wait 20\n");
			mdelay(20000);
		}
	   		times = 300;
		    tmp = 0x2be2be3; //0x2ae4772;  IF = 6M, fs = 35M, dec2hex(round(8*IF/fs*2^25))
		    tmp2 = 0x2000;
		    tmp1 = 8;
		    reg_0xa8 = 0xc0000000; // bypass CCI
		    reg_0xac = 0xc0000000; // bypass CCI

		    maxCCI = 0;
    		maxCCI_p = 0;
			for(i = 0; i < times; i++) {
		         //reg_0xa8 = app_apb_read_reg(0xa8);
		         reg_0xa8_t = reg_0xa8 + tmp + i * tmp2;
	       		 apb_write_reg(QAM_BASE+0xa8, reg_0xa8_t);
		         reg_0xac_t = reg_0xac + tmp - i * tmp2;
	        	 apb_write_reg(QAM_BASE+0xac, reg_0xac_t);
	       		 sum = 0;
	        	 sum1 = 0;
		          for(j = 0; j < tmp1; j++) {
		              	// msleep(1);
		              	// mdelay(1);
			             reg_0xf0 = apb_read_reg(QAM_BASE+0xf0);
			             re = (reg_0xf0 >> 24) & 0xff;
			             im = (reg_0xf0 >> 16) & 0xff;
			             if(re > 127)
			                 //re = re - 256;
			                 re = 256 - re;
			             if(im > 127)
			                 //im = im - 256;
			                 im = 256 - im;

			             sum += re + im;
			             re = (reg_0xf0 >> 8) & 0xff;
			             im = (reg_0xf0 >> 0) & 0xff;
			             if(re > 127)
			                 //re = re - 256;
			                 re = 256 - re;
			             if(im > 127)
			                 //im = im - 256;
			                 im = 256 - im;

			             sum1 += re + im;

			     }
		         sum = sum / tmp1;
		         sum1 = sum1 /tmp1;
		         if(sum1 > sum) {
		             sum = sum1;
		             reg_0xa8_t = reg_0xac_t;
		         }
				// printk("0xa8 = %x, sum = %d i is %d\n", reg_0xa8_t, sum,i);
		         if(sum > maxCCI) {
		              maxCCI = sum;
		              if(maxCCI > 24) {
		                  maxCCI_p = reg_0xa8_t & 0x7fffffff;
		              }
		              printk("[cci]maxCCI = %d, maxCCI_p = %x i is %d, sum is %d\n", maxCCI, maxCCI_p,i,sum);
		         }
		         if((sum < 24) && (maxCCI_p > 0))
		             break;  // stop CCI detect.
	     }

	     if(maxCCI_p > 0) {
	         printk("[cci]--------- find CCI, loc = %x, value = %d ---------- \n", maxCCI_p, maxCCI);
	         apb_write_reg(QAM_BASE+0xa8, maxCCI_p & 0x7fffffff); // enable CCI
	         apb_write_reg(QAM_BASE+0xac, maxCCI_p & 0x7fffffff); // enable CCI
	    //     if(dvbc.mode == 4) // 256QAM
	             apb_write_reg(QAM_BASE+0x54, 0xa25705fa); //
	             cciflag=1;
	             mdelay(1000);
	     }
	     else{
	         printk("[cci] ------------  find NO CCI ------------------- \n");
			 cciflag = 0;
	     }


		printk("[cci][%s]--------------------------\n",__func__);
		mod_timer(&mytimer, jiffies + 2*HZ);
		return 0;
//	}
#endif
}
int  dvbc_timer_init(void)
{
	printk("%s\n",__func__);
	setup_timer(&mytimer, dvbc_cci_timer, (unsigned long)"Hello, world!");
	mytimer.expires = jiffies + 2*HZ;
	add_timer(&mytimer);
	return 0;
}
void  dvbc_timer_exit(void)
{
	printk("%s\n",__func__);
	del_timer(&mytimer);
}

void dvbc_cci_task(void)
{
	int count;
	int maxCCI_p,re,im,j,i,times,maxCCI,sum,sum1,reg_0xf0,tmp1,tmp,tmp2,reg_0xa8,reg_0xac;
	int reg_0xa8_t, reg_0xac_t;
	count=100;
	while(1){
			msleep(200);
		    // search cci((si2176_get_strength()-256)<(-85))
		if((((apb_read_reg(QAM_BASE+0x18))&0x1)==1)){
				printk("[cci]lock ");
				if(cciflag==0){
					  apb_write_reg(QAM_BASE+0xa8, 0);
					  apb_write_reg(QAM_BASE+0xac, 0);
					  printk("no cci ");
					  cciflag=0;
				}
				 printk("\n");
				 msleep(500);
			continue;
		}

		 if(cciflag==1){
		 	printk("[cci]cciflag is 1,wait 20\n");
			msleep(20000);
		}
	   		times = 300;
		    tmp = 0x2be2be3; //0x2ae4772;  IF = 6M, fs = 35M, dec2hex(round(8*IF/fs*2^25))
		    tmp2 = 0x2000;
		    tmp1 = 8;
		    reg_0xa8 = 0xc0000000; // bypass CCI
		    reg_0xac = 0xc0000000; // bypass CCI

		    maxCCI = 0;
    		maxCCI_p = 0;
			for(i = 0; i < times; i++) {
		         //reg_0xa8 = app_apb_read_reg(0xa8);
		         reg_0xa8_t = reg_0xa8 + tmp + i * tmp2;
	       		 apb_write_reg(QAM_BASE+0xa8, reg_0xa8_t);
		         reg_0xac_t = reg_0xac + tmp - i * tmp2;
	        	 apb_write_reg(QAM_BASE+0xac, reg_0xac_t);
	       		 sum = 0;
	        	 sum1 = 0;
		          for(j = 0; j < tmp1; j++) {
		              //	 msleep(1);
			             reg_0xf0 = apb_read_reg(QAM_BASE+0xf0);
			             re = (reg_0xf0 >> 24) & 0xff;
			             im = (reg_0xf0 >> 16) & 0xff;
			             if(re > 127)
			                 //re = re - 256;
			                 re = 256 - re;
			             if(im > 127)
			                 //im = im - 256;
			                 im = 256 - im;

			             sum += re + im;

			             re = (reg_0xf0 >> 8) & 0xff;
			             im = (reg_0xf0 >> 0) & 0xff;
			             if(re > 127)
			                 //re = re - 256;
			                 re = 256 - re;
			             if(im > 127)
			                 //im = im - 256;
			                 im = 256 - im;

			             sum1 += re + im;


			     }
		         sum = sum / tmp1;
		         sum1 = sum1 /tmp1;
		         if(sum1 > sum) {
		             sum = sum1;
		             reg_0xa8_t = reg_0xac_t;
		         }
				// printk("0xa8 = %x, sum = %d i is %d\n", reg_0xa8_t, sum,i);
		         if(sum > maxCCI) {
		              maxCCI = sum;
		              if(maxCCI > 24) {
		                  maxCCI_p = reg_0xa8_t & 0x7fffffff;
		              }
		              printk("[cci]maxCCI = %d, maxCCI_p = %x i is %d, sum is %d\n", maxCCI, maxCCI_p,i,sum);
		         }

		         if((sum < 24) && (maxCCI_p > 0))
		             break;  // stop CCI detect.
	     }

	     if(maxCCI_p > 0) {
	         printk("[cci]--------- find CCI, loc = %x, value = %d ---------- \n", maxCCI_p, maxCCI);
	         apb_write_reg(QAM_BASE+0xa8, maxCCI_p & 0x7fffffff); // enable CCI
	         apb_write_reg(QAM_BASE+0xac, maxCCI_p & 0x7fffffff); // enable CCI
	    //     if(dvbc.mode == 4) // 256QAM
	             apb_write_reg(QAM_BASE+0x54, 0xa25705fa); //
	             cciflag=1;
	             msleep(1000);
	     }
	     else{
	         printk("[cci] ------------  find NO CCI ------------------- \n");
			 cciflag = 0;
	     }


		printk("[cci][%s]--------------------------\n",__func__);
	}

}

int dvbc_get_cci_task(void)
{
	if(cci_task)
		return 0;
	else
		return 1;

}

void dvbc_create_cci_task(void)
{

	int ret;
	//apb_write_reg(QAM_BASE+0xa8, 0x42b2ebe3); // enable CCI
    // apb_write_reg(QAM_BASE+0xac, 0x42b2ebe3); // enable CCI
//     if(dvbc.mode == 4) // 256QAM
    // apb_write_reg(QAM_BASE+0x54, 0xa25705fa); //
    ret=0;
	cci_task=kthread_create(dvbc_cci_task,NULL,"cci_task");
	if(ret!=0)
	{
		printk ("[%s]Create cci kthread error!\n",__func__);
		cci_task=NULL;
		return 0;
	}
	wake_up_process(cci_task);
	printk ("[%s]Create cci kthread and wake up!\n",__func__);
}

void dvbc_kill_cci_task(void)
{
	if(cci_task){
                kthread_stop(cci_task);
                cci_task = NULL;
				printk ("[%s]kill cci kthread !\n",__func__);
     }
}




u32 dvbc_set_qam_mode(unsigned char mode)
{
	printk("auto change mode ,now mode is %d\n",mode);
	apb_write_reg(QAM_BASE+0x008, (mode&7));  // qam mode
	switch(mode){
		case 0 : // 16 QAM
		apb_write_reg(QAM_BASE+0x054, 0x23460224);	// EQ_FIR_CTL,
		apb_write_reg(QAM_BASE+0x068, 0x00c000c0);	// EQ_CRTH_SNR
		apb_write_reg(QAM_BASE+0x074,  0x50001a0);	// EQ_TH_LMS  40db	13db
		apb_write_reg(QAM_BASE+0x07c, 0x003001e9);	// EQ_NORM and EQ_TH_MMA
		//apb_write_reg(QAM_BASE+0x080, 0x000be1ff);  // EQ_TH_SMMA0
		apb_write_reg(QAM_BASE+0x080, 0x000e01fe);	// EQ_TH_SMMA0
		apb_write_reg(QAM_BASE+0x084, 0x00000000);	// EQ_TH_SMMA1
		apb_write_reg(QAM_BASE+0x088, 0x00000000);	// EQ_TH_SMMA2
		apb_write_reg(QAM_BASE+0x08c, 0x00000000);	// EQ_TH_SMMA3
		//apb_write_reg(QAM_BASE+0x094, 0x7f800d2b);  // AGC_CTRL  ALPS tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292b);  // Pilips Tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292d);  // Pilips Tuner
		apb_write_reg(QAM_BASE+0x094, 0x7f80092d);	// Pilips Tuner
		 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f66); // by raymond 20121213
		break;

		case 1 : // 32 QAM
			apb_write_reg(QAM_BASE+0x054, 0x24560506);	// EQ_FIR_CTL,
			apb_write_reg(QAM_BASE+0x068, 0x00c000c0);	// EQ_CRTH_SNR
			//apb_write_reg(QAM_BASE+0x074, 0x5000260);   // EQ_TH_LMS	40db  19db
			apb_write_reg(QAM_BASE+0x074,  0x50001f0);	// EQ_TH_LMS  40db	17.5db
			apb_write_reg(QAM_BASE+0x07c, 0x00500102);	// EQ_TH_MMA  0x000001cc
			apb_write_reg(QAM_BASE+0x080, 0x00077140);	// EQ_TH_SMMA0
			apb_write_reg(QAM_BASE+0x084, 0x001fb000);	// EQ_TH_SMMA1
			apb_write_reg(QAM_BASE+0x088, 0x00000000);	// EQ_TH_SMMA2
			apb_write_reg(QAM_BASE+0x08c, 0x00000000);	// EQ_TH_SMMA3
			//apb_write_reg(QAM_BASE+0x094, 0x7f800d2b);  // AGC_CTRL  ALPS tuner
			//apb_write_reg(QAM_BASE+0x094, 0x7f80292b);  // Pilips Tuner
			apb_write_reg(QAM_BASE+0x094, 0x7f80092b);	// Pilips Tuner
			 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f66); // by raymond 20121213
		break;

		case 2 : // 64 QAM
		//apb_write_reg(QAM_BASE+0x054, 0x2256033a);  // EQ_FIR_CTL,
		apb_write_reg(QAM_BASE+0x054, 0x2336043a);	// EQ_FIR_CTL, by raymond
		apb_write_reg(QAM_BASE+0x068, 0x00c000c0);	// EQ_CRTH_SNR
		//apb_write_reg(QAM_BASE+0x074, 0x5000260);  // EQ_TH_LMS  40db  19db
		apb_write_reg(QAM_BASE+0x074,  0x5000230);	// EQ_TH_LMS  40db	17.5db
		apb_write_reg(QAM_BASE+0x07c, 0x007001bd);	// EQ_TH_MMA
		apb_write_reg(QAM_BASE+0x080, 0x000580ed);	// EQ_TH_SMMA0
		apb_write_reg(QAM_BASE+0x084, 0x001771fb);	// EQ_TH_SMMA1
		apb_write_reg(QAM_BASE+0x088, 0x00000000);	// EQ_TH_SMMA2
		apb_write_reg(QAM_BASE+0x08c, 0x00000000);	// EQ_TH_SMMA3
		//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c); // AGC_CTRL  ALPS tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292c); // Pilips & maxlinear Tuner
		apb_write_reg(QAM_BASE+0x094, 0x7f802b3d);	// Pilips Tuner & maxlinear Tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f802b3a);  // Pilips Tuner & maxlinear Tuner
		 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f66); // by raymond 20121213
		break;

		case 3 : // 128 QAM
		//apb_write_reg(QAM_BASE+0x054, 0x2557046a);  // EQ_FIR_CTL,
		apb_write_reg(QAM_BASE+0x054, 0x2437067a);	// EQ_FIR_CTL, by raymond 20121213
		apb_write_reg(QAM_BASE+0x068, 0x00c000d0);	// EQ_CRTH_SNR
		// apb_write_reg(QAM_BASE+0x074, 0x02440240);  // EQ_TH_LMS  18.5db  18db
		// apb_write_reg(QAM_BASE+0x074, 0x04000400);  // EQ_TH_LMS  22db  22.5db
		apb_write_reg(QAM_BASE+0x074,  0x5000260);	// EQ_TH_LMS  40db	19db
		//apb_write_reg(QAM_BASE+0x07c, 0x00b000f2);  // EQ_TH_MMA0x000000b2
		apb_write_reg(QAM_BASE+0x07c, 0x00b00132);	// EQ_TH_MMA0x000000b2 by raymond 20121213
		apb_write_reg(QAM_BASE+0x080, 0x0003a09d);	// EQ_TH_SMMA0
		apb_write_reg(QAM_BASE+0x084, 0x000f8150);	// EQ_TH_SMMA1
		apb_write_reg(QAM_BASE+0x088, 0x001a51f8);	// EQ_TH_SMMA2
		apb_write_reg(QAM_BASE+0x08c, 0x00000000);	// EQ_TH_SMMA3
		//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c);  // AGC_CTRL  ALPS tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292c);  // Pilips Tuner
		apb_write_reg(QAM_BASE+0x094, 0x7f80092c);	// Pilips Tuner
		 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f66); // by raymond 20121213
		break;

		case 4 : // 256 QAM
		//apb_write_reg(QAM_BASE+0x054, 0xa2580588);  // EQ_FIR_CTL,
		apb_write_reg(QAM_BASE+0x054, 0xa25905f9);	// EQ_FIR_CTL, by raymond 20121213
		apb_write_reg(QAM_BASE+0x068, 0x01e00220);	// EQ_CRTH_SNR
		//apb_write_reg(QAM_BASE+0x074,  0x50002a0);  // EQ_TH_LMS	40db  19db
		apb_write_reg(QAM_BASE+0x074,  0x5000270);	// EQ_TH_LMS  40db	19db by raymond 201211213
		apb_write_reg(QAM_BASE+0x07c, 0x00f001a5);	// EQ_TH_MMA
		apb_write_reg(QAM_BASE+0x080, 0x0002c077);	// EQ_TH_SMMA0
		apb_write_reg(QAM_BASE+0x084, 0x000bc0fe);	// EQ_TH_SMMA1
		apb_write_reg(QAM_BASE+0x088, 0x0013f17e);	// EQ_TH_SMMA2
		apb_write_reg(QAM_BASE+0x08c, 0x01bc01f9);	// EQ_TH_SMMA3
		//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c);  // AGC_CTRL  ALPS tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292c);  // Pilips Tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292d);  // Maxlinear Tuner
		apb_write_reg(QAM_BASE+0x094, 0x7f80092d);	// Maxlinear Tuner
		 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213, when adc=35M,sys=70M, its better than 0x61f2f66
		break;
		default:   //64qam
			//apb_write_reg(QAM_BASE+0x054, 0x2256033a);  // EQ_FIR_CTL,
		apb_write_reg(QAM_BASE+0x054, 0x2336043a);	// EQ_FIR_CTL, by raymond
		apb_write_reg(QAM_BASE+0x068, 0x00c000c0);	// EQ_CRTH_SNR
		//apb_write_reg(QAM_BASE+0x074, 0x5000260);  // EQ_TH_LMS  40db  19db
		apb_write_reg(QAM_BASE+0x074,  0x5000230);	// EQ_TH_LMS  40db	17.5db
		apb_write_reg(QAM_BASE+0x07c, 0x007001bd);	// EQ_TH_MMA
		apb_write_reg(QAM_BASE+0x080, 0x000580ed);	// EQ_TH_SMMA0
		apb_write_reg(QAM_BASE+0x084, 0x001771fb);	// EQ_TH_SMMA1
		apb_write_reg(QAM_BASE+0x088, 0x00000000);	// EQ_TH_SMMA2
		apb_write_reg(QAM_BASE+0x08c, 0x00000000);	// EQ_TH_SMMA3
		//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c); // AGC_CTRL  ALPS tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f80292c); // Pilips & maxlinear Tuner
		apb_write_reg(QAM_BASE+0x094, 0x7f802b3d);	// Pilips Tuner & maxlinear Tuner
		//apb_write_reg(QAM_BASE+0x094, 0x7f802b3a);  // Pilips Tuner & maxlinear Tuner
		 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f66); // by raymond 20121213
		break;
		}
		return 0;
}


u32 dvbc_get_status(void)
{
//	printk("c4 is %x\n",apb_read_reg(QAM_BASE+0xc4));
	return apb_read_reg(QAM_BASE+0xc4)&0xf;
}
EXPORT_SYMBOL(dvbc_get_status);


static u32 dvbc_get_ch_power(void)
{
    u32 tmp;
    u32 ad_power;
    u32 agc_gain;
    u32 ch_power;

    tmp = apb_read_reg(QAM_BASE+0x09c);

    ad_power = (tmp>>22)&0x1ff;
    agc_gain = (tmp>>0)&0x7ff;

    ad_power = ad_power>>4;
    // ch_power = lookuptable(agc_gain) + ad_power; TODO
    ch_power = (ad_power & 0xffff) + ((agc_gain & 0xffff) << 16) ;

    return ch_power;
}

static u32 dvbc_get_snr(void)
{
    u32 tmp, snr;

    tmp = apb_read_reg(QAM_BASE+0x14)&0xfff;
    snr = tmp * 100 / 32;  // * 1e2

    return snr;
}

static u32 dvbc_get_ber(void)
{
    u32 rs_ber;
    u32 rs_packet_len;

    rs_packet_len = apb_read_reg(QAM_BASE+0x10)&0xffff;
    rs_ber = apb_read_reg(QAM_BASE+0x14)>>12&0xfffff;

    // rs_ber = rs_ber / 204.0 / 8.0 / rs_packet_len;
    if(rs_packet_len == 0)
	rs_ber = 1000000;
    else
	rs_ber = rs_ber * 613 / rs_packet_len;  // 1e-6

    return rs_ber;
}

static u32 dvbc_get_per(void)
{
    u32 rs_per;
    u32 rs_packet_len;
	u32 acc_rs_per_times;

    rs_packet_len = apb_read_reg(QAM_BASE+0x10)&0xffff;
    rs_per = apb_read_reg(QAM_BASE+0x18)>>16&0xffff;

    acc_rs_per_times = apb_read_reg(QAM_BASE+0xcc)&0xffff;
    //rs_per = rs_per / rs_packet_len;

    if(rs_packet_len == 0)
	rs_per = 10000;
    else
	rs_per = 10000 * rs_per / rs_packet_len;  // 1e-4

    //return rs_per;
    return acc_rs_per_times;
}

static u32 dvbc_get_symb_rate(void)
{
    u32 tmp;
    u32 adc_freq;
    u32 symb_rate;

    adc_freq = apb_read_reg(QAM_BASE+0x34)>>16&0xffff;
    tmp = apb_read_reg(QAM_BASE+0xb8);

    if((tmp>>15) == 0)
	symb_rate = 0;
    else
	symb_rate = 10 * (adc_freq<<12) / (tmp>>15); // 1e4

    return symb_rate;
}

static int dvbc_get_freq_off(void)
{
    int tmp;
    int symb_rate;
    int freq_off;

    symb_rate = dvbc_get_symb_rate();
    tmp = apb_read_reg(QAM_BASE+0xe0)&0x3fffffff;
    if (tmp>>29&1) tmp -= (1<<30);

    freq_off = ((tmp>>16) * 25 * (symb_rate>>10)) >> 3;

    return freq_off;
}

static void dvbc_set_test_bus(u8 sel)
{
    u32 tmp;

    tmp = apb_read_reg(QAM_BASE+0x08);
    tmp &= ~(0x1f<<4);
    tmp |= ((sel&0x1f)<<4) | (1<<3);
    apb_write_reg(QAM_BASE+0x08, tmp);
}

void dvbc_get_test_out(u8 sel, u32 len, u32 *buf)
{
    int i, cnt;

    dvbc_set_test_bus(sel);

    for (i=0, cnt=0; i<len-4 && cnt<1000000; i++) {
	buf[i] = apb_read_reg(QAM_BASE+0xb0);
	if (buf[i]>>11&1) {
	    buf[i++] = apb_read_reg(QAM_BASE+0xb0);
	    buf[i++] = apb_read_reg(QAM_BASE+0xb0);
	    buf[i++] = apb_read_reg(QAM_BASE+0xb0);
	    buf[i++] = apb_read_reg(QAM_BASE+0xb0);
	}
	else {
	    i--;
	}

	cnt++;
    }
}
#if 0
static void dvbc_sw_reset(int addr, int idx)
{
    u32 tmp;

    tmp = apb_read_reg(QAM_BASE+addr);

    tmp &= ~(1<<idx);
    apb_write_reg(QAM_BASE+addr, tmp);

    udelay(1);

    tmp |= (1<<idx);
    apb_write_reg(QAM_BASE+addr, tmp);
}

static void dvbc_reset(void)
{
    dvbc_sw_reset(0x04, 0);
}

static void dvbc_eq_reset(void)
{
    dvbc_sw_reset(0x50, 3);
}

static void dvbc_eq_smma_reset(void)
{
    dvbc_sw_reset(0xe8, 0);
}
#endif
static void dvbc_reg_initial(struct aml_demod_sta *demod_sta)
{
    u32 clk_freq;
    u32 adc_freq;
    u8  tuner;
    u8  ch_mode;
    u8  agc_mode;
    u32 ch_freq;
    u16 ch_if;
    u16 ch_bw;
    u16 symb_rate;
    u32 phs_cfg;
    int afifo_ctr;
    int max_frq_off, tmp;

    clk_freq  = demod_sta->clk_freq ; // kHz
    adc_freq  = demod_sta->adc_freq ; // kHz
//    adc_freq  = 25414;
    tuner     = demod_sta->tuner    ;
    ch_mode   = demod_sta->ch_mode  ;
    agc_mode  = demod_sta->agc_mode ;
    ch_freq   = demod_sta->ch_freq  ; // kHz
    ch_if     = demod_sta->ch_if    ; // kHz
    ch_bw     = demod_sta->ch_bw    ; // kHz
    symb_rate = demod_sta->symb_rate; // k/sec
//    ch_mode=4;
    printk("in dvbc_func, clk_freq is %d, adc_freq is %d,ch_mode is %d,ch_if is %d\n", clk_freq, adc_freq,ch_mode,ch_if);
//	apb_write_reg(DEMOD_CFG_BASE,0x00000007);
    // disable irq
    apb_write_reg(QAM_BASE+0xd0, 0);

    // reset
    //dvbc_reset();
    apb_write_reg(QAM_BASE+0x4, apb_read_reg(QAM_BASE+0x4) &~ (1 << 4)); // disable fsm_en
	  apb_write_reg(QAM_BASE+0x4, apb_read_reg(QAM_BASE+0x4) &~ (1 << 0)); // Sw disable demod
	  apb_write_reg(QAM_BASE+0x4, apb_read_reg(QAM_BASE+0x4) | (1 << 0)); // Sw enable demod

    apb_write_reg(QAM_BASE+0x000, 0x00000000);  // QAM_STATUS
    apb_write_reg(QAM_BASE+0x004, 0x00000f00);  // QAM_GCTL0
    apb_write_reg(QAM_BASE+0x008, (ch_mode&7));  // qam mode

    switch (ch_mode) {
    case 0 : // 16 QAM
       	apb_write_reg(QAM_BASE+0x054, 0x23460224);  // EQ_FIR_CTL,
       	apb_write_reg(QAM_BASE+0x068, 0x00c000c0);  // EQ_CRTH_SNR
       	apb_write_reg(QAM_BASE+0x074,  0x50001a0);  // EQ_TH_LMS  40db  13db
       	apb_write_reg(QAM_BASE+0x07c, 0x003001e9);  // EQ_NORM and EQ_TH_MMA
       	//apb_write_reg(QAM_BASE+0x080, 0x000be1ff);  // EQ_TH_SMMA0
       	apb_write_reg(QAM_BASE+0x080, 0x000e01fe);  // EQ_TH_SMMA0
       	apb_write_reg(QAM_BASE+0x084, 0x00000000);  // EQ_TH_SMMA1
       	apb_write_reg(QAM_BASE+0x088, 0x00000000);  // EQ_TH_SMMA2
       	apb_write_reg(QAM_BASE+0x08c, 0x00000000);  // EQ_TH_SMMA3
       	//apb_write_reg(QAM_BASE+0x094, 0x7f800d2b);  // AGC_CTRL  ALPS tuner
       	//apb_write_reg(QAM_BASE+0x094, 0x7f80292b);  // Pilips Tuner
       	//apb_write_reg(QAM_BASE+0x094, 0x7f80292d);  // Pilips Tuner
       	apb_write_reg(QAM_BASE+0x094, 0x7f80092d);  // Pilips Tuner
       	 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213
	break;

    case 1 : // 32 QAM
       	apb_write_reg(QAM_BASE+0x054, 0x24560506);  // EQ_FIR_CTL,
       	apb_write_reg(QAM_BASE+0x068, 0x00c000c0);  // EQ_CRTH_SNR
       	//apb_write_reg(QAM_BASE+0x074, 0x5000260);   // EQ_TH_LMS  40db  19db
       	apb_write_reg(QAM_BASE+0x074,  0x50001f0);  // EQ_TH_LMS  40db  17.5db
       	apb_write_reg(QAM_BASE+0x07c, 0x00500102);  // EQ_TH_MMA  0x000001cc
       	apb_write_reg(QAM_BASE+0x080, 0x00077140);  // EQ_TH_SMMA0
       	apb_write_reg(QAM_BASE+0x084, 0x001fb000);  // EQ_TH_SMMA1
       	apb_write_reg(QAM_BASE+0x088, 0x00000000);  // EQ_TH_SMMA2
       	apb_write_reg(QAM_BASE+0x08c, 0x00000000);  // EQ_TH_SMMA3
       	//apb_write_reg(QAM_BASE+0x094, 0x7f800d2b);  // AGC_CTRL  ALPS tuner
       	//apb_write_reg(QAM_BASE+0x094, 0x7f80292b);  // Pilips Tuner
       	apb_write_reg(QAM_BASE+0x094, 0x7f80092b);  // Pilips Tuner
       	 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213
	break;

    case 2 : // 64 QAM
	//apb_write_reg(QAM_BASE+0x054, 0x2256033a);  // EQ_FIR_CTL,
	apb_write_reg(QAM_BASE+0x054, 0x2336043a);  // EQ_FIR_CTL, by raymond
	apb_write_reg(QAM_BASE+0x068, 0x00c000c0);  // EQ_CRTH_SNR
	//apb_write_reg(QAM_BASE+0x074, 0x5000260);  // EQ_TH_LMS  40db  19db
	apb_write_reg(QAM_BASE+0x074,  0x5000230);  // EQ_TH_LMS  40db  17.5db
	apb_write_reg(QAM_BASE+0x07c, 0x007001bd);  // EQ_TH_MMA
	apb_write_reg(QAM_BASE+0x080, 0x000580ed);  // EQ_TH_SMMA0
	apb_write_reg(QAM_BASE+0x084, 0x001771fb);  // EQ_TH_SMMA1
	apb_write_reg(QAM_BASE+0x088, 0x00000000);  // EQ_TH_SMMA2
	apb_write_reg(QAM_BASE+0x08c, 0x00000000);  // EQ_TH_SMMA3
	//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c); // AGC_CTRL  ALPS tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f80292c); // Pilips & maxlinear Tuner
	apb_write_reg(QAM_BASE+0x094, 0x7f802b3d);  // Pilips Tuner & maxlinear Tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f802b3a);  // Pilips Tuner & maxlinear Tuner
	 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213
	break;

    case 3 : // 128 QAM
	//apb_write_reg(QAM_BASE+0x054, 0x2557046a);  // EQ_FIR_CTL,
	apb_write_reg(QAM_BASE+0x054, 0x2437067a);  // EQ_FIR_CTL, by raymond 20121213
	apb_write_reg(QAM_BASE+0x068, 0x00c000d0);  // EQ_CRTH_SNR
	// apb_write_reg(QAM_BASE+0x074, 0x02440240);  // EQ_TH_LMS  18.5db  18db
	// apb_write_reg(QAM_BASE+0x074, 0x04000400);  // EQ_TH_LMS  22db  22.5db
	apb_write_reg(QAM_BASE+0x074,  0x5000260);  // EQ_TH_LMS  40db  19db
	//apb_write_reg(QAM_BASE+0x07c, 0x00b000f2);  // EQ_TH_MMA0x000000b2
	apb_write_reg(QAM_BASE+0x07c, 0x00b00132);  // EQ_TH_MMA0x000000b2 by raymond 20121213
	apb_write_reg(QAM_BASE+0x080, 0x0003a09d);  // EQ_TH_SMMA0
	apb_write_reg(QAM_BASE+0x084, 0x000f8150);  // EQ_TH_SMMA1
	apb_write_reg(QAM_BASE+0x088, 0x001a51f8);  // EQ_TH_SMMA2
	apb_write_reg(QAM_BASE+0x08c, 0x00000000);  // EQ_TH_SMMA3
	//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c);  // AGC_CTRL  ALPS tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f80292c);  // Pilips Tuner
	apb_write_reg(QAM_BASE+0x094, 0x7f80092c);  // Pilips Tuner
	 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213
	break;

    case 4 : // 256 QAM
	//apb_write_reg(QAM_BASE+0x054, 0xa2580588);  // EQ_FIR_CTL,
	apb_write_reg(QAM_BASE+0x054, 0xa25905f9);  // EQ_FIR_CTL, by raymond 20121213
	apb_write_reg(QAM_BASE+0x068, 0x01e00220);  // EQ_CRTH_SNR
	//apb_write_reg(QAM_BASE+0x074,  0x50002a0);  // EQ_TH_LMS  40db  19db
	apb_write_reg(QAM_BASE+0x074,  0x5000270);  // EQ_TH_LMS  40db  19db by raymond 201211213
	apb_write_reg(QAM_BASE+0x07c, 0x00f001a5);  // EQ_TH_MMA
	apb_write_reg(QAM_BASE+0x080, 0x0002c077);  // EQ_TH_SMMA0
	apb_write_reg(QAM_BASE+0x084, 0x000bc0fe);  // EQ_TH_SMMA1
	apb_write_reg(QAM_BASE+0x088, 0x0013f17e);  // EQ_TH_SMMA2
	apb_write_reg(QAM_BASE+0x08c, 0x01bc01f9);  // EQ_TH_SMMA3
	//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c);  // AGC_CTRL  ALPS tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f80292c);  // Pilips Tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f80292d);  // Maxlinear Tuner
	apb_write_reg(QAM_BASE+0x094, 0x7f80092d);  // Maxlinear Tuner
	 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213, when adc=35M,sys=70M, its better than 0x61f2f66
	break;
	default:   //64qam
		//apb_write_reg(QAM_BASE+0x054, 0x2256033a);  // EQ_FIR_CTL,
	apb_write_reg(QAM_BASE+0x054, 0x2336043a);  // EQ_FIR_CTL, by raymond
	apb_write_reg(QAM_BASE+0x068, 0x00c000c0);  // EQ_CRTH_SNR
	//apb_write_reg(QAM_BASE+0x074, 0x5000260);  // EQ_TH_LMS  40db  19db
	apb_write_reg(QAM_BASE+0x074,  0x5000230);  // EQ_TH_LMS  40db  17.5db
	apb_write_reg(QAM_BASE+0x07c, 0x007001bd);  // EQ_TH_MMA
	apb_write_reg(QAM_BASE+0x080, 0x000580ed);  // EQ_TH_SMMA0
	apb_write_reg(QAM_BASE+0x084, 0x001771fb);  // EQ_TH_SMMA1
	apb_write_reg(QAM_BASE+0x088, 0x00000000);  // EQ_TH_SMMA2
	apb_write_reg(QAM_BASE+0x08c, 0x00000000);  // EQ_TH_SMMA3
	//apb_write_reg(QAM_BASE+0x094, 0x7f800d2c); // AGC_CTRL  ALPS tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f80292c); // Pilips & maxlinear Tuner
	apb_write_reg(QAM_BASE+0x094, 0x7f802b3d);  // Pilips Tuner & maxlinear Tuner
	//apb_write_reg(QAM_BASE+0x094, 0x7f802b3a);  // Pilips Tuner & maxlinear Tuner
	 apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121213
	break;
    }

    //apb_write_reg(QAM_BASE+0x00c, 0xfffffffe);  // adc_cnt, symb_cnt
    apb_write_reg(QAM_BASE+0x00c, 0xffff8ffe);  // adc_cnt, symb_cnt	by raymond 20121213
    if (clk_freq == 0)
        afifo_ctr = 0;
    else
        afifo_ctr = (adc_freq * 256 / clk_freq) + 2;
    if(afifo_ctr > 255) afifo_ctr = 255;
    apb_write_reg(QAM_BASE+0x010, (afifo_ctr<<16) | 8000); // afifo, rs_cnt_cfg

    //apb_write_reg(QAM_BASE+0x020, 0x21353e54);  // PHS_reset & TIM_CTRO_ACCURATE  sw_tim_select=0
	  //apb_write_reg(QAM_BASE+0x020, 0x21b53e54); //modified by qiancheng
	  apb_write_reg(QAM_BASE+0x020, 0x61b53e54); //modified by qiancheng by raymond 20121208  0x63b53e54 for cci
	//  apb_write_reg(QAM_BASE+0x020, 0x6192bfe2); //modifed by ligg 20130613 auto symb_rate scan
    if (adc_freq == 0)
        phs_cfg = 0;
    else
        phs_cfg = (1<<31) / adc_freq * ch_if / (1<<8);   //  8*fo/fs*2^20 fo=36.125, fs = 28.57114, = 21d775
    // printk("phs_cfg = %x\n", phs_cfg);
    apb_write_reg(QAM_BASE+0x024, 0x4c000000 | (phs_cfg&0x7fffff));  // PHS_OFFSET, IF offset,

    if(adc_freq == 0)
       max_frq_off = 0;
    else
    {
       max_frq_off = (1<<29) / symb_rate;   // max_frq_off = (400KHz * 2^29) / (AD=28571 * symbol_rate=6875)
       tmp = 40000000 / adc_freq;
       max_frq_off = tmp * max_frq_off;
    }
    printk("max_frq_off is %x,\n",max_frq_off);
    apb_write_reg(QAM_BASE+0x02c, max_frq_off&0x3fffffff);  // max frequency offset, by raymond 20121208

    //apb_write_reg(QAM_BASE+0x030, 0x011bf400);  // TIM_CTL0 start speed is 0,  when know symbol rate
	apb_write_reg(QAM_BASE+0x030, 0x245cf451); //MODIFIED BY QIANCHENG
//	apb_write_reg(QAM_BASE+0x030, 0x245bf451); //modified by ligg 20130613 --auto symb_rate scan
    apb_write_reg(QAM_BASE+0x034, ((adc_freq & 0xffff) << 16) | (symb_rate&0xffff) );

    apb_write_reg(QAM_BASE+0x038, 0x00400000);  // TIM_SWEEP_RANGE 16000

/************* hw state machine config **********/
    apb_write_reg(QAM_BASE+0x040, 0x003c); // configure symbol rate step step 0

    // modified 0x44 0x48
    apb_write_reg(QAM_BASE+0x044, (symb_rate&0xffff)*256); // blind search, configure max symbol_rate      for 7218  fb=3.6M
    //apb_write_reg(QAM_BASE+0x048, 3600*256); // configure min symbol_rate fb = 6.95M
    apb_write_reg(QAM_BASE+0x048, 3400*256); // configure min symbol_rate fb = 6.95M

    //apb_write_reg(QAM_BASE+0x0c0, 0xffffff68); // threshold
    //apb_write_reg(QAM_BASE+0x0c0, 0xffffff6f); // threshold
    //apb_write_reg(QAM_BASE+0x0c0, 0xfffffd68); // threshold
    //apb_write_reg(QAM_BASE+0x0c0, 0xffffff68); // threshold
    //apb_write_reg(QAM_BASE+0x0c0, 0xffffff68); // threshold
    //apb_write_reg(QAM_BASE+0x0c0, 0xffff2f67); // threshold for skyworth
   // apb_write_reg(QAM_BASE+0x0c0, 0x061f2f67); // by raymond 20121208
   // apb_write_reg(QAM_BASE+0x0c0, 0x061f2f66); // by raymond 20121213, remove it to every constellation
/************* hw state machine config **********/

    apb_write_reg(QAM_BASE+0x04c, 0x00008800);  // reserved

    //apb_write_reg(QAM_BASE+0x050, 0x00000002);  // EQ_CTL0
    apb_write_reg(QAM_BASE+0x050, 0x01472002);  // EQ_CTL0 by raymond 20121208

    //apb_write_reg(QAM_BASE+0x058, 0xff550e1e);  // EQ_FIR_INITPOS
    apb_write_reg(QAM_BASE+0x058, 0xff100e1e);  // EQ_FIR_INITPOS for skyworth

    apb_write_reg(QAM_BASE+0x05c, 0x019a0000);  // EQ_FIR_INITVAL0
    apb_write_reg(QAM_BASE+0x060, 0x019a0000);  // EQ_FIR_INITVAL1

    //apb_write_reg(QAM_BASE+0x064, 0x01101128);  // EQ_CRTH_TIMES
    apb_write_reg(QAM_BASE+0x064, 0x010a1128);  // EQ_CRTH_TIMES for skyworth
    apb_write_reg(QAM_BASE+0x06c, 0x00041a05);  // EQ_CRTH_PPM

    apb_write_reg(QAM_BASE+0x070, 0xffb9aa01);  // EQ_CRLP

    //apb_write_reg(QAM_BASE+0x090, 0x00020bd5); // agc control
    apb_write_reg(QAM_BASE+0x090, 0x00000bd5); // agc control

    // agc control
    // apb_write_reg(QAM_BASE+0x094, 0x7f800d2c);   // AGC_CTRL  ALPS tuner
    // apb_write_reg(QAM_BASE+0x094, 0x7f80292c);     // Pilips Tuner
    if ((agc_mode&1)==0)
	apb_write_reg(QAM_BASE+0x094, apb_read_reg(QAM_BASE+0x94) | (0x1 << 10));     // freeze if agc
    if ((agc_mode&2)==0) // IF control
	apb_write_reg(QAM_BASE+0x094, apb_read_reg(QAM_BASE+0x94) | (0x1 << 13));     // freeze rf agc

    //apb_write_reg(QAM_BASE+0x094, 0x7f80292d);     // Maxlinear Tuner

    apb_write_reg(QAM_BASE+0x098, 0x9fcc8190);  // AGC_IFGAIN_CTRL
    //apb_write_reg(QAM_BASE+0x0a0, 0x0e028c00);  // AGC_RFGAIN_CTRL 0x0e020800
    //apb_write_reg(QAM_BASE+0x0a0, 0x0e03cc00);  // AGC_RFGAIN_CTRL 0x0e020800
    //apb_write_reg(QAM_BASE+0x0a0, 0x0e028700);  // AGC_RFGAIN_CTRL 0x0e020800 now
    //apb_write_reg(QAM_BASE+0x0a0, 0x0e03cd00);  // AGC_RFGAIN_CTRL 0x0e020800
    //apb_write_reg(QAM_BASE+0x0a0, 0x0603cd11);  // AGC_RFGAIN_CTRL 0x0e020800 by raymond, if Adjcent channel test, maybe it need change.20121208 ad invert
    apb_write_reg(QAM_BASE+0x0a0, 0x0603cd10);  // AGC_RFGAIN_CTRL 0x0e020800 by raymond, if Adjcent channel test, maybe it need change.20121208 ad invert,20130221, suit for two path channel.

    apb_write_reg(QAM_BASE+0x004, apb_read_reg(QAM_BASE+0x004)|0x33);  // IMQ, QAM Enable

    // start hardware machine
    //dvbc_sw_reset(0x004, 4);
    apb_write_reg(QAM_BASE+0x4, apb_read_reg(QAM_BASE+0x4) | (1 << 4));
    apb_write_reg(QAM_BASE+0x0e8, (apb_read_reg(QAM_BASE+0x0e8)|(1<<2)));

    // clear irq status
    apb_read_reg(QAM_BASE+0xd4);

    // enable irq
    apb_write_reg(QAM_BASE+0xd0, 0x7fff<<3);


//auto track
	//	dvbc_set_auto_symtrack();
}

u32 dvbc_set_auto_symtrack()
{
	  apb_write_reg(QAM_BASE+0x030, 0x245bf45c);//open track
	  apb_write_reg(QAM_BASE+0x020, 0x61b2bf5c);
	  apb_write_reg(QAM_BASE+0x044, (7000&0xffff)*256);
	  apb_write_reg(QAM_BASE+0x038, 0x00220000);
	  apb_write_reg(QAM_BASE+0x4, apb_read_reg(QAM_BASE+0x4) &~ (1 << 0)); // Sw disable demod
	  apb_write_reg(QAM_BASE+0x4, apb_read_reg(QAM_BASE+0x4) | (1 << 0)); // Sw enable demod
	  return 0;
}

int dvbc_set_ch(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_dvbc *demod_dvbc)
{
    int ret = 0;
    u16 symb_rate;
    u8  mode;
    u32 ch_freq;

   printk("f=%d, s=%d, q=%d\n", demod_dvbc->ch_freq, demod_dvbc->symb_rate, demod_dvbc->mode);
   demod_i2c->tuner = 7;
    mode      = demod_dvbc->mode;
    symb_rate = demod_dvbc->symb_rate;
    ch_freq   = demod_dvbc->ch_freq;
    if (mode > 4) {
	printk("Error: Invalid QAM mode option %d\n", mode);
	mode = 2;
       	ret = -1;
    }

    if (symb_rate<1000 || symb_rate>7000) {
	printk("Error: Invalid Symbol Rate option %d\n", symb_rate);
	symb_rate = 6875;
	ret = -1;
    }

    if (ch_freq<1000 || ch_freq>900000) {
	printk("Error: Invalid Channel Freq option %d\n", ch_freq);
	ch_freq = 474000;
	ret = -1;
    }

    // if (ret != 0) return ret;
    demod_sta->dvb_mode  = 0;
    demod_sta->ch_mode   = mode; // 0:16, 1:32, 2:64, 3:128, 4:256
    demod_sta->agc_mode  = 1;    // 0:NULL, 1:IF, 2:RF, 3:both
    demod_sta->ch_freq   = ch_freq;
    demod_sta->tuner     = demod_i2c->tuner;

    if(demod_i2c->tuner == 1)
        demod_sta->ch_if     = 36130; // TODO  DCT tuner
    else if (demod_i2c->tuner == 2)
        demod_sta->ch_if     = 4570; // TODO  Maxlinear tuner
    else if (demod_i2c->tuner == 7)
     //   demod_sta->ch_if     = 5000; // TODO  Si2176 tuner

    demod_sta->ch_bw     = 8000;  // TODO
    demod_sta->symb_rate = symb_rate;
    dvbc_reg_initial(demod_sta);

    return ret;
}

int dvbc_status(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_sts *demod_sts)
{
	struct aml_fe_dev *dev;
	int ftmp,tmp;
	dev=NULL;
    demod_sts->ch_sts = apb_read_reg(QAM_BASE+0x18);
    demod_sts->ch_pow = dvbc_get_ch_power();
    demod_sts->ch_snr = dvbc_get_snr();
    demod_sts->ch_ber = dvbc_get_ber();
    demod_sts->ch_per = dvbc_get_per();
    demod_sts->symb_rate = dvbc_get_symb_rate();
    demod_sts->freq_off = dvbc_get_freq_off();
    //demod_sts->dat0 = apb_read_reg(QAM_BASE+0x28);
//    demod_sts->dat0 = tuner_get_ch_power(demod_i2c);
    demod_sts->dat1 = tuner_get_ch_power(dev);
#if 1

    ftmp = demod_sts->ch_sts;
	printk("[dvbc debug] ch_sts is %x\n",ftmp);
	ftmp = demod_sts->ch_snr;
    ftmp /= 100;
    printk("snr %d dB ", ftmp);
    ftmp = demod_sts->ch_ber;
    printk("ber %.d ", ftmp);
    tmp = demod_sts->ch_per;
    printk("per %d ", tmp);
    ftmp = demod_sts->symb_rate;
    printk("srate %.d ", ftmp);
    ftmp = demod_sts->freq_off;
    printk("freqoff %.d kHz ", ftmp);
    tmp = demod_sts->dat1;
    printk("strength %ddb  0xe0 status is %lu ,b4 status is %lu", tmp,(
	apb_read_reg(QAM_BASE+0xe0)&0xffff),(apb_read_reg(QAM_BASE+0xb4)&0xffff));
	printk("dagc_gain is %lu ", apb_read_reg(QAM_BASE+0xa4)&0x7f);
	tmp = demod_sts->ch_pow;
	printk("power is %ddb \n",(tmp & 0xffff));

#endif

    return 0;
}

void dvbc_enable_irq(int dvbc_irq)
{
    u32 mask;

    // clear status
    apb_read_reg(QAM_BASE+0xd4);
    // enable irq
    mask = apb_read_reg(QAM_BASE+0xd0);
    mask |= (1<<dvbc_irq);
    apb_write_reg(QAM_BASE+0xd0, mask);
}

void dvbc_disable_irq(int dvbc_irq)
{
    u32 mask;

    // disable irq
    mask = apb_read_reg(QAM_BASE+0xd0);
    mask &= ~(1<<dvbc_irq);
    apb_write_reg(QAM_BASE+0xd0, mask);
    // clear status
    apb_read_reg(QAM_BASE+0xd4);
}

char *dvbc_irq_name[] = {
    "      ADC",
    "   Symbol",
    "       RS",
    " In_Sync0",
    " In_Sync1",
    " In_Sync2",
    " In_Sync3",
    " In_Sync4",
    "Out_Sync0",
    "Out_Sync1",
    "Out_Sync2",
    "Out_Sync3",
    "Out_Sync4",
    "In_SyncCo",
    "OutSyncCo",
    "  In_Dagc",
    " Out_Dagc",
    "  Eq_Mode",
    "RS_Uncorr"};

void dvbc_isr(struct aml_demod_sta *demod_sta)
{
    u32 stat, mask;
    int dvbc_irq;

    stat = apb_read_reg(QAM_BASE+0xd4);
    mask = apb_read_reg(QAM_BASE+0xd0);
    stat &= mask;

    for (dvbc_irq=0; dvbc_irq<20; dvbc_irq++) {
	if (stat>>dvbc_irq&1) {
	    if (demod_sta->debug)
		printk("irq: dvbc %2d %s %8x\n",
		       dvbc_irq, dvbc_irq_name[dvbc_irq], stat);
	    // dvbc_disable_irq(dvbc_irq);
	}
    }
}

int dvbc_isr_islock(void)
{
#define IN_SYNC4_MASK (0x80)

    u32 stat, mask;

    stat = apb_read_reg(QAM_BASE+0xd4);
    apb_write_reg(QAM_BASE+0xd4, 0);
    mask = apb_read_reg(QAM_BASE+0xd0);
    stat &= mask;

    return ((stat&IN_SYNC4_MASK)==IN_SYNC4_MASK);
}

