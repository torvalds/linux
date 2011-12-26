/****************************************************************
*
*VERSION 1.0 Inital Version
*note: when swith to real ic, need to define SYS_CLK_CFG_EN & undef FPGA_SIM_CONFIG, vice versa.
*****************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/keyboard.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/slab.h>
#include <linux/timer.h> 
#include <mach/clock.h>

#include <mach/irqs.h>
#include <mach/system.h>
#include <mach/hardware.h>
#include <mach/sys_config.h>

#include <linux/clk.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
    #include <linux/pm.h>
    #include <linux/earlysuspend.h>
#endif

#include "ir-keymap.h"

static struct clk *apb_ir_clk;
static struct clk *ir_clk;
static u32 ir_gpio_hdle;

#ifdef CONFIG_SUN5I_FPGA
#define FPGA_SIM_CONFIG            //input clk is 24M
//#define SYS_CLK_CFG_EN
#else
//#define FPGA_SIM_CONFIG            //input clk is 24M
#define SYS_CLK_CFG_EN
#endif

#define SYS_GPIO_CFG_EN
//#define DEBUG_IR
#define PRINT_SUSPEND_INFO

#ifdef DEBUG_IR
#define DEBUG_IR_LEVEL0
#define DEBUG_IR_LEVEL2
#define DEBUG_IR_LEVEL1
#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG fmt , ## arg)
#else
#undef DEBUG_IR_LEVEL0
#undef DEBUG_IR_LEVEL2
#undef DEBUG_IR_LEVEL1
#define dprintk(level, fmt, arg...)	//
#endif

//Registers
#define IR_REG(x) 	(x)
#define IR0_BASE		(0xf1c21800)
#define IR_BASE		IR0_BASE
#define IR_IRQNO	(SW_INT_IRQNO_IR0)

//CCM register
#define CCM_BASE	0xf1c20000
//PIO register
#define PI_BASE		0xf1c20800
                                    
#define IR_CTRL_REG	IR_REG(0x00) //IR Control
#define IR_RXCFG_REG	IR_REG(0x10) //Rx Config
#define IR_RXDAT_REG	IR_REG(0x20) //Rx Data
#define IR_RXINTE_REG	IR_REG(0x2C) //Rx Interrupt Enable
#define IR_RXINTS_REG	IR_REG(0x30) //Rx Interrupt Status
#define IR_SPLCFG_REG	IR_REG(0x34) //IR Sample Config

//Bit Definition of IR_RXINTS_REG Register
#define IR_RXINTS_RXOF		(0x1<<0)		//Rx FIFO Overflow
#define IR_RXINTS_RXPE		(0x1<<1)		//Rx Packet End
#define IR_RXINTS_RXDA		(0x1<<4)		//Rx FIFO Data Available

#define IR_FIFO_SIZE		       (64)    //64Bytes
//Frequency of Sample Clock = 23437.5Hz, Cycle is 42.7us
//Pulse of NEC Remote >560us
#ifdef FPGA_SIM_CONFIG
#define IR_RXFILT_VAL         (16)   //Filter Threshold = 8*42.7 = ~341us < 500us 
#define IR_RXIDLE_VAL         (5)    //Idle Threshold = (2+1)*128*42.7 = ~16.4ms > 9ms

#define IR_L1_MIN     (160)  //80*42.7 = ~3.4ms, Lead1(4.5ms) > IR_L1_MIN 
#define IR_L0_MIN     (80)  //40*42.7 = ~1.7ms, Lead0(4.5ms) Lead0R(2.25ms)> IR_L0_MIN 
#define IR_PMAX      (52)  //26*42.7 = ~1109us ~= 561*2, Pluse < IR_PMAX 
#define IR_DMID      (52)  //26*42.7 = ~1109us ~= 561*2, D1 > IR_DMID, D0 =< IR_DMID
#define IR_DMAX      (106)  //53*42.7 = ~2263us ~= 561*4, D < IR_DMAX

#else
#define IR_RXFILT_VAL		(8)			//Filter Threshold = 8*42.7 = ~341us	< 500us	
#define IR_RXIDLE_VAL		(2)   	//Idle Threshold = (2+1)*128*42.7 = ~16.4ms > 9ms

#define IR_L1_MIN	(80)		//80*42.7 = ~3.4ms, Lead1(4.5ms) > IR_L1_MIN 
#define IR_L0_MIN	(40)		//40*42.7 = ~1.7ms, Lead0(4.5ms) Lead0R(2.25ms)> IR_L0_MIN 
#define IR_PMAX		(26)		//26*42.7 = ~1109us ~= 561*2, Pluse < IR_PMAX 
#define IR_DMID		(26)		//26*42.7 = ~1109us ~= 561*2, D1 > IR_DMID, D0 =< IR_DMID
#define IR_DMAX		(53)		//53*42.7 = ~2263us ~= 561*4, D < IR_DMAX
#endif

#define IR_ERROR_CODE		(0xffffffff)
#define IR_REPEAT_CODE		(0x00000000)
#define DRV_VERSION		"1.00"


#ifdef CONFIG_HAS_EARLYSUSPEND	
struct sun4i_ir_data {
	struct early_suspend early_suspend;
};
#endif

struct ir_raw_buffer	
{
	unsigned long dcnt;                  /*Packet Count*/
	#define	IR_RAW_BUF_SIZE		128
	unsigned char buf[IR_RAW_BUF_SIZE];	
};

#ifdef DEBUG_IR
static int debug = 8;
#endif

static unsigned int ir_cnt = 0;
static struct input_dev *ir_dev;
static struct timer_list *s_timer; 
static unsigned long ir_code=0;
static int timer_used=0;
static struct ir_raw_buffer	ir_rawbuf;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct sun4i_ir_data *ir_data;
#endif


static inline void ir_reset_rawbuffer(void)
{
	ir_rawbuf.dcnt = 0;
}

static inline void ir_write_rawbuffer(unsigned char data)
{
	if(ir_rawbuf.dcnt < IR_RAW_BUF_SIZE) 	
	{
		ir_rawbuf.buf[ir_rawbuf.dcnt++] = data;
	}
	else
	{
		printk("ir_write_rawbuffer: IR Rx Buffer Full!!\n");
	}
}

static inline unsigned char ir_read_rawbuffer(void)
{
	unsigned char data = 0x00;
	
	if(ir_rawbuf.dcnt > 0)
	{
		data = ir_rawbuf.buf[--ir_rawbuf.dcnt];
	}
		
	return data;
}

static inline int ir_rawbuffer_empty(void)
{
	return (ir_rawbuf.dcnt == 0);
}

static inline int ir_rawbuffer_full(void)
{
	return (ir_rawbuf.dcnt >= IR_RAW_BUF_SIZE);	
}

static void ir_clk_cfg(void)
{
#ifdef SYS_CLK_CFG_EN
        unsigned long rate = 3000000; //3Mhz     
#else
        unsigned long tmp = 0;
#endif
	
#ifdef SYS_CLK_CFG_EN
	apb_ir_clk = clk_get(NULL, "apb_ir0");		
	if(!apb_ir_clk) {
			printk("try to get apb_ir0 clock failed!\n");
			return;
	}

	ir_clk = clk_get(NULL, "ir0");		
	if(!ir_clk) {
			printk("try to get ir0 clock failed!\n");
			return;
	}

	if(clk_set_rate(ir_clk, rate)) {
			printk("set ir0 clock freq to 3M failed!\n");
	}
		
	if(clk_enable(apb_ir_clk)) {
		printk("try to enable apb_ir_clk failed!\n");	
		}
	if(clk_enable(ir_clk)) {
		printk("try to enable apb_ir_clk failed!\n");	
	}
		
#else	
	//Enable APB Clock for IR	
	tmp = readl(CCM_BASE + 0x10);
	tmp |= 0x1<<10;  //IR	
	writel(tmp, CCM_BASE + 0x10);	

	//config Special Clock for IR	(24/8=3MHz)
	tmp = readl(CCM_BASE + 0x34);
	tmp &= ~(0x3<<8);  	
	tmp |= (0x1<<8);   	//Select 24MHz
	tmp |= (0x1<<7);   	//Open Clock
	tmp &= ~(0x3f<<0);  
	tmp |= (7<<0);	 	//Divisor = 8 
	writel(tmp, CCM_BASE + 0x34);
#endif

    return;
}

static void ir_clk_uncfg(void)
{
#ifdef SYS_CLK_CFG_EN
	clk_put(apb_ir_clk);
	clk_put(ir_clk);
#else	
#endif
 
	return;
}
static void ir_sys_cfg(void)
{
	//unsigned long tmp;

#ifdef SYS_GPIO_CFG_EN
        if(0 == (ir_gpio_hdle = gpio_request_ex("ir_para", "ir0_rx"))){
	printk("try to request ir_para gpio failed. \n");
        }
        
#else
	//config IO: PIOB4 to IR_Rx
	tmp = readl(PI_BASE + 0x24); //PIOB_CFG0_REG
	tmp &= ~(0xf<<16);
	tmp |= (0x2<<16);
	writel(tmp, PI_BASE + 0x24);
#endif

	ir_clk_cfg();

    return;	
}

static void ir_sys_uncfg(void)
{
	//unsigned long tmp;
	

	#ifdef SYS_GPIO_CFG_EN
        gpio_release(ir_gpio_hdle, 2);
	#else
	#endif

    ir_clk_uncfg();

	return;	
}

static void ir_reg_cfg(void)
{
	unsigned long tmp = 0;
	/*Enable IR Mode*/
	tmp = 0x3<<4;
	writel(tmp, IR_BASE+IR_CTRL_REG);
	
	/*Config IR Smaple Register*/
#ifdef FPGA_SIM_CONFIG
    tmp = 0x3<<0;  //Fsample = 24MHz/512 = 46875Hz (21.33us)
#else
    tmp = 0x1<<0;  //Fsample = 3MHz/128 =23437.5Hz (42.7us)
#endif
 
        
	tmp |= (IR_RXFILT_VAL&0x3f)<<2;	//Set Filter Threshold
	tmp |= (IR_RXIDLE_VAL&0xff)<<8; //Set Idle Threshold 
	writel(tmp, IR_BASE+IR_SPLCFG_REG);
	
	/*Invert Input Signal*/
	writel(0x1<<2, IR_BASE+IR_RXCFG_REG);
	
	/*Clear All Rx Interrupt Status*/
	writel(0xff, IR_BASE+IR_RXINTS_REG);
	
	/*Set Rx Interrupt Enable*/
	tmp = (0x1<<4)|0x3;
	//tmp |= ((IR_FIFO_SIZE>>1)-1)<<8; //Rx FIFO Threshold = FIFOsz/2;
	tmp |= ((IR_FIFO_SIZE>>2)-1)<<8; //Rx FIFO Threshold = FIFOsz/4;
	writel(tmp, IR_BASE+IR_RXINTE_REG);
	
	/*Enable IR Module*/
	tmp = readl(IR_BASE+IR_CTRL_REG);
	tmp |= 0x3;
	writel(tmp, IR_BASE+IR_CTRL_REG);

	return;
}

static void ir_setup(void)
{	
	dprintk(2, "ir_setup: ir setup start!!\n");

	ir_code = 0;
	timer_used = 0;
	ir_reset_rawbuffer();	
	ir_sys_cfg();	
	ir_reg_cfg();
	
	dprintk(2, "ir_setup: ir setup end!!\n");

	return;
}

static inline unsigned char ir_get_data(void)
{
	return (unsigned char)(readl(IR_BASE + IR_RXDAT_REG));
}

static inline unsigned long ir_get_intsta(void)
{
	return (readl(IR_BASE + IR_RXINTS_REG));
}

static inline void ir_clr_intsta(unsigned long bitmap)
{
	unsigned long tmp = readl(IR_BASE + IR_RXINTS_REG);

	tmp &= ~0xff;
	tmp |= bitmap&0xff;
	writel(tmp, IR_BASE + IR_RXINTS_REG);
}

static unsigned long ir_packet_handler(unsigned char *buf, unsigned long dcnt)
{
	unsigned long len;
	unsigned char val = 0x00;
	unsigned char last = 0x00;
	unsigned long code = 0;
	int bitCnt = 0;
	unsigned long i=0;
	
	//print_hex_dump_bytes("--- ", DUMP_PREFIX_NONE, buf, dcnt);

	dprintk(2, "dcnt = %d \n", (int)dcnt);
	
	/*Find Lead '1'*/
	len = 0;
	for(i=0; i<dcnt; i++)
	{
		val = buf[i];
		if(val & 0x80)
		{
			len += val & 0x7f;
		}
		else 
		{
			if(len > IR_L1_MIN)
				break;
			
			len = 0;
		}
	}

	if((val&0x80) || (len<=IR_L1_MIN))
		return IR_ERROR_CODE; /*Invalid Code*/
		
	/*Find Lead '0'*/
	len = 0;
	for(; i<dcnt; i++)
	{
		val = buf[i];		
		if(val & 0x80)
		{
			if(len > IR_L0_MIN)
				break;
			
			len = 0;
		}
		else 
		{
			len += val & 0x7f;
		}		
	}
	
	if((!(val&0x80)) || (len<=IR_L0_MIN))
		return IR_ERROR_CODE; /*Invalid Code*/
	
	/*go decoding*/
	code = 0;  /*0 for Repeat Code*/
	bitCnt = 0;
	last = 1;
	len = 0;
	for(; i<dcnt; i++)
	{
		val = buf[i];		
		if(last)
		{
			if(val & 0x80)
			{
				len += val & 0x7f;
			}
			else
			{
				if(len > IR_PMAX) /*Error Pulse*/
				{
					return IR_ERROR_CODE;
				}
				last = 0;
				len = val & 0x7f;
			}
		}
		else
		{
			if(val & 0x80)
			{
				if(len > IR_DMAX) /*Error Distant*/
				{
					return IR_ERROR_CODE;
				}
				else
				{
					if(len > IR_DMID)  
					{
						/*data '1'*/
						code |= 1<<bitCnt;
					}
					bitCnt ++;
					if(bitCnt == 32)
						break;  /*decode over*/
				}	
				last = 1;
				len = val & 0x7f;
			}
			else
			{
				len += val & 0x7f;
			}
		}
	}
	
	return code;
}

static int ir_code_valid(unsigned long code)
{
	unsigned long tmp1, tmp2;

#ifdef IR_CHECK_ADDR_CODE
	/*Check Address Value*/
	if((code&0xffff) != (IR_ADDR_CODE&0xffff))
		return 0; /*Address Error*/
	
	tmp1 = code & 0x00ff0000;
	tmp2 = (code & 0xff000000)>>8;
	
	return ((tmp1^tmp2)==0x00ff0000);  /*Check User Code*/
#else	
	/*Do Not Check Address Value*/
	tmp1 = code & 0x00ff00ff;
	tmp2 = (code & 0xff00ff00)>>8;
	
	//return ((tmp1^tmp2)==0x00ff00ff);
	return (((tmp1^tmp2) & 0x00ff0000)==0x00ff0000 );
#endif /*#ifdef IR_CHECK_ADDR_CODE*/
}

static irqreturn_t ir_irq_service(int irqno, void *dev_id)
{
	unsigned long intsta = ir_get_intsta();
	
#ifdef DEBUG_IR_LEVEL2
	printk("IR IRQ Serve\n");
#endif
	ir_clr_intsta(intsta);
	
	//if(intsta & (IR_RXINTS_RXDA|IR_RXINTS_RXPE))  /*FIFO Data Valid*/
	/*Read Data Every Time Enter this Routine*/
	{
		//unsigned long dcnt =  (ir_get_intsta()>>8) & 0x1f;
		unsigned long dcnt =  (ir_get_intsta()>>8) & 0x3f;
		unsigned long i = 0;
		
		/*Read FIFO*/
		for(i=0; i<dcnt; i++)
		{
			if(ir_rawbuffer_full())
			{
				#ifdef DEBUG_IR_LEVEL0
				printk("ir_irq_service: Raw Buffer Full!!\n");
				#endif
				break;
			}
			else
			{
				ir_write_rawbuffer(ir_get_data());
			}			
		}		
	}
	
	if(intsta & IR_RXINTS_RXPE)	 /*Packet End*/
	{
		unsigned long code;
		int code_valid;
		
		code = ir_packet_handler(ir_rawbuf.buf, ir_rawbuf.dcnt);
		ir_rawbuf.dcnt = 0;
		code_valid = ir_code_valid(code);
					
		if(timer_used)
		{
			if(code_valid)  //the pre-key is released 
			{
				input_report_key(ir_dev, ir_keycodes[(ir_code>>16)&0xff], 0);
				input_sync(ir_dev);	
				#ifdef DEBUG_IR_LEVEL1
				printk("IR KEY UP\n");
				#endif
				ir_cnt = 0;
			}
			if((code==IR_REPEAT_CODE)||(code_valid))  //Error, may interfere from other sources
			{
				mod_timer(s_timer, jiffies + (HZ/5));
			}
		}
		else  		
		{
			if(code_valid)
			{
				s_timer->expires = jiffies + (HZ/5);  //200ms timeout
				add_timer(s_timer);
				timer_used = 1;	
			}
		}
		
		if(timer_used)
		{
			ir_cnt++;
			if(ir_cnt == 1)
			{
			if(code_valid)	ir_code = code;  /*update saved code with a new valid code*/
			#ifdef DEBUG_IR_LEVEL0
			printk("IR RAW CODE : %lu\n",(ir_code>>16)&0xff);
			#endif
			input_report_key(ir_dev, ir_keycodes[(ir_code>>16)&0xff], 1);
			#ifdef DEBUG_IR_LEVEL0
			printk("IR CODE : %d\n",ir_keycodes[(ir_code>>16)&0xff]);
			#endif
			input_sync(ir_dev);			
			#ifdef DEBUG_IR_LEVEL1
			printk("IR KEY VALE %d\n",ir_keycodes[(ir_code>>16)&0xff]);
			#endif
		  }

			
		}
		
		dprintk(1, "ir_irq_service: Rx Packet End, code=0x%x, ir_code=0x%x, timer_used=%d \n", (int)code, (int)ir_code, timer_used);
	}	
	
	if(intsta & IR_RXINTS_RXOF)  /*FIFO Overflow*/
	{
		/*flush raw buffer*/
		ir_reset_rawbuffer();		
#ifdef DEBUG_IR_LEVEL0
		printk("ir_irq_service: Rx FIFO Overflow!!\n");
#endif
	}	
	
	return IRQ_HANDLED;
}

static void ir_timer_handle(unsigned long arg)
{
	del_timer(s_timer);
	timer_used = 0;
	/*Time Out, means that the key is up*/
	input_report_key(ir_dev, ir_keycodes[(ir_code>>16)&0xff], 0);
	input_sync(ir_dev);	
#ifdef DEBUG_IR_LEVEL1
	printk("IR KEY TIMER OUT UP\n");
#endif
	ir_cnt = 0;
	
	dprintk(2, "ir_timer_handle: timeout \n");	
}

//停用设备
#ifdef CONFIG_HAS_EARLYSUSPEND
static void sun4i_ir_suspend(struct early_suspend *h)
{
//    unsigned long tmp = 0;
	/*int ret;
	struct sun4i_ir_data *ts = container_of(h, struct sun4i_ir_data, early_suspend);
      */
#ifdef PRINT_SUSPEND_INFO
	printk("enter earlysuspend: sun4i_ir_suspend. \n");
#endif
/*
	tmp = readl(IR_BASE+IR_CTRL_REG);
	tmp &= 0xfffffffc;
    writel(tmp, IR_BASE+IR_CTRL_REG);
*/
	clk_disable(ir_clk);
	clk_disable(apb_ir_clk);

	return ;
}

//重新唤醒
static void sun4i_ir_resume(struct early_suspend *h)
{
    //unsigned long tmp = 0;
	/*int ret;
	struct sun4i_ir_data *ts = container_of(h, struct sun4i_ir_data, early_suspend);
    */
#ifdef PRINT_SUSPEND_INFO
	printk("enter laterresume: sun4i_ir_resume. \n");
#endif

	ir_code = 0;
	timer_used = 0;	
	ir_reset_rawbuffer();	
	ir_clk_cfg();	
	ir_reg_cfg();

	return ; 
}
#else
#endif

static int __init ir_init(void)
{
	int i,ret;
	int err =0;

	ir_dev = input_allocate_device();
	if (!ir_dev) 
	{
		printk(KERN_ERR "ir_dev: not enough memory for input device\n");
		err = -ENOMEM;
		goto fail1;
	}

	ir_dev->name = "sun4i-ir";  
	ir_dev->phys = "RemoteIR/input1"; 
	ir_dev->id.bustype = BUS_HOST;      
	ir_dev->id.vendor = 0x0001;
	ir_dev->id.product = 0x0001;
	ir_dev->id.version = 0x0100;

    #ifdef REPORT_REPEAT_KEY_VALUE
	ir_dev->evbit[0] = BIT_MASK(EV_KEY)|BIT_MASK(EV_REP) ;
    #else
    	ir_dev->evbit[0] = BIT_MASK(EV_KEY);
    #endif
    
	for (i = 0; i < 256; i++)
		set_bit(ir_keycodes[i], ir_dev->keybit);
		
	if (request_irq(IR_IRQNO, ir_irq_service, 0, "RemoteIR",
			ir_dev)) {
		err = -EBUSY;
		goto fail2;
	}

	ir_setup();
	  
	s_timer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (!s_timer)   
	{
	ret =  - ENOMEM;
	printk(" IR FAIL TO  Request Time\n");
	goto fail3;
	}
	init_timer(s_timer);
	s_timer->function = &ir_timer_handle;

	err = input_register_device(ir_dev);
	if (err)
		goto fail4;
	printk("IR Initial OK\n");

#ifdef CONFIG_HAS_EARLYSUSPEND	
	printk("==register_early_suspend =\n");
	ir_data = kzalloc(sizeof(*ir_data), GFP_KERNEL);
	if (ir_data == NULL) {
		err = -ENOMEM;
		goto err_alloc_data_failed;
	}

	ir_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;	
	ir_data->early_suspend.suspend = sun4i_ir_suspend;
	ir_data->early_suspend.resume	= sun4i_ir_resume;	
	register_early_suspend(&ir_data->early_suspend);
#endif

	return 0;

#ifdef CONFIG_HAS_EARLYSUSPEND
 err_alloc_data_failed:
#endif
 fail4:	
 	kfree(s_timer);
 fail3:	
 	free_irq(IR_IRQNO, ir_dev);
 fail2:	
 	input_free_device(ir_dev);
 fail1:	
 	return err;
}

static void __exit ir_exit(void)
{
	#ifdef CONFIG_HAS_EARLYSUSPEND	
	    unregister_early_suspend(&ir_data->early_suspend);	
	#endif

	free_irq(IR_IRQNO, ir_dev);
	input_unregister_device(ir_dev);
	ir_sys_uncfg();
 	kfree(s_timer);
}

module_init(ir_init);
module_exit(ir_exit);

MODULE_DESCRIPTION("Remote IR driver");
MODULE_AUTHOR("DanielWang");
MODULE_LICENSE("GPL");

