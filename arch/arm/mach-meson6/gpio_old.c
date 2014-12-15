/*
Linux PINMUX.C

*/
#include <linux/module.h>
#include <stdarg.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <plat/io.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#include <mach/am_regs.h>
#include "gpio_data.c"
//#define DEBUG_PINMUX
#ifndef DEBUG_PINMUX
#define debug(a...)
#else
#define debug(a...) printk(KERN_INFO  a)
#endif

#define set_pin_mux_reg(a,b)   if(b!=NOT_EXIST){  a[(b>>5)&0xf]|=(1<<(b&0x1f))  ;}
static int32_t single_pin_pad(uint32_t  reg_en[P_PIN_MUX_REG_NUM], uint32_t  reg_dis[P_PIN_MUX_REG_NUM],uint32_t pad, uint32_t sig)
{

	uint32_t enable,disable;
	int32_t ret=-1;
	foreach_pad_sig_start(pad,sig)
		case_pad_equal(enable,disable);
			set_pin_mux_reg(reg_dis,enable);
			set_pin_mux_reg(reg_en,disable);
		case_end;
		case_sig_equal(enable,disable);
			set_pin_mux_reg(reg_dis,enable);
			set_pin_mux_reg(reg_en,disable);
		case_end;
		case_both_equal(enable,disable);
			set_pin_mux_reg(reg_en,enable);
			set_pin_mux_reg(reg_dis,disable);
			ret=0;
		case_end;
	foreach_pad_sig_end;
	if(ret==-1&&sig!=SIG_GPIOIN&&sig!=SIG_GPIOOUT)
		return -1;
	return 0;
}
#if 0
static uint32_t caculate_pinmux_set_size(uint32_t  reg_en[P_PIN_MUX_REG_NUM], uint32_t  reg_dis[P_PIN_MUX_REG_NUM])
{
	uint32_t ret=0;
	int i;
	for(i=0;i<P_PIN_MUX_REG_NUM;i++)
	{
		if(reg_en[i]||reg_dis[i])
			ret++;
	}
	return ret;
}
#endif
static int32_t caculate_single_pinmux_set(pinmux_item_t pinmux[P_PIN_MUX_REG_NUM+1],uint32_t pad,uint32_t sig)
{
	uint32_t  reg_en[P_PIN_MUX_REG_NUM];
	uint32_t  reg_dis[P_PIN_MUX_REG_NUM];

	int32_t i,j;
	pinmux_item_t end=PINMUX_END_ITEM;

	memset(reg_en,0,sizeof(reg_en));
	memset(reg_dis,0,sizeof(reg_dis));
	if(single_pin_pad(reg_en,reg_dis,pad,sig)<0)
		return -1;
	for(j=0,i=0;i<P_PIN_MUX_REG_NUM;i++)
	{
		if(reg_en[i]==0&&reg_dis[i]==0)
			continue;
		pinmux[j].setmask=reg_en[i];
		pinmux[j].clrmask=reg_dis[i];
		pinmux[j].reg=i;
		j++;
	}
	pinmux[j]=end;
	return 0;
}
/**
 * UTIL interface
 * these function can be implement in a tools
 */
 /**
  * @return NULL is fail
  * 		errno NOTAVAILABLE ,
  * 			  SOMEPIN IS LOCKED
  */
static DEFINE_SPINLOCK(pinmux_set_lock);
static uint32_t pimux_locktable[P_PIN_MUX_REG_NUM];
pinmux_set_t* pinmux_cacl_str(char * pad,char * sig ,...)
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(pinmux_cacl_str);
pinmux_set_t* pinmux_cacl_int(uint32_t pad,uint32_t sig ,...)
{
#if 0
	va_list ap;
           int d;
           char c, *s;

           va_start(ap, fmt);
           while (*fmt)
               switch (*fmt++) {
               case 's':              /* string */
                   s = va_arg(ap, char *);
                   printf("string %s\n", s);
                   break;
               case 'd':              /* int */
                   d = va_arg(ap, int);
                   printf("int %d\n", d);
                   break;
               case 'c':              /* char */
                   /* need a cast here since va_arg only
                      takes fully promoted types */
                   c = (char) va_arg(ap, int);
                   printf("char %c\n", c);
                   break;
               }
           va_end(ap);
#endif

	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();


	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(pinmux_cacl_int);
pinmux_set_t* pinmux_cacl(char * str)///formate is "pad=sig pad=sig "
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(pinmux_cacl);

char ** pin_get_list(void)
{

	 return (char **)&pad_name[0];
}
EXPORT_SYMBOL(pin_get_list);

char ** sig_get_list(void)
{
	 return (char **)&sig_name[0];
}
EXPORT_SYMBOL(sig_get_list);
char * pin_getname(uint32_t pin)
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(pin_getname);
char * sig_getname(uint32_t sig)
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(sig_getname);
uint32_t pins_num(void)
{
	 return PAD_MAX_PADS;
}
EXPORT_SYMBOL(pins_num);
/**
 * Util Get status function
 */
uint32_t pin_sig(uint32_t pin)
{
	return SIG_MAX_SIGS;
}
EXPORT_SYMBOL(pin_sig);
uint32_t sig_pin(uint32_t sig)
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return 0-1;
}
EXPORT_SYMBOL(sig_pin);
/**
 * pinmux set function
 * @return 0, success ,
 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
 * 		   NOTAVAILABLE, not available .
 */

static DECLARE_WAIT_QUEUE_HEAD(pinmux_wait_queue);
int32_t pinmux_set(pinmux_set_t* pinmux )
{
	uint32_t locallock[P_PIN_MUX_REG_NUM];
    uint32_t reg,value,conflict,dest_value;
	int i;
	DECLARE_WAITQUEUE(wait, current);
	if(pinmux==NULL)
	{
	    BUG();
		return -4;
	}
    debug( " pinmux addr %p \n",(pinmux->pinmux));
retry:
	memset(locallock,0,sizeof(locallock));
	spin_lock(&pinmux_set_lock);
	///check lock table
	for(i=0;pinmux->pinmux[i].reg!=0xffffffff;i++)
	{
        reg=pinmux->pinmux[i].reg;
        locallock[reg]=pinmux->pinmux[i].clrmask|pinmux->pinmux[i].setmask;
        dest_value=pinmux->pinmux[i].setmask;

        conflict=locallock[reg]&pimux_locktable[reg];
		if(conflict)
        {
            value=readl(p_pin_mux_reg_addr[reg])&conflict;
            dest_value&=conflict;
            if(value!=dest_value)
            {

                printk("set fail , detect locktable conflict,retry");
                set_current_state(TASK_UNINTERRUPTIBLE);
                add_wait_queue(&pinmux_wait_queue, &wait);
                spin_unlock(&pinmux_set_lock);
                schedule();
                remove_wait_queue(&pinmux_wait_queue, &wait);
                goto retry;
            }
        }
	}
	if(pinmux->chip_select!=NULL )
	{
		if(pinmux->chip_select(true)==false){
            debug("error return -3");
            spin_unlock(&pinmux_set_lock);
            BUG();
			return -3;///@select chip fail;
        }
	}

	for(i=0;pinmux->pinmux[i].reg!=0xffffffff;i++)
	{

        debug( "clrsetbits %08x %08x %08x \n",p_pin_mux_reg_addr[pinmux->pinmux[i].reg],pinmux->pinmux[i].clrmask,pinmux->pinmux[i].setmask);
    	pimux_locktable[pinmux->pinmux[i].reg]|=locallock[pinmux->pinmux[i].reg];
        clrsetbits_le32(p_pin_mux_reg_addr[pinmux->pinmux[i].reg],pinmux->pinmux[i].clrmask,pinmux->pinmux[i].setmask);
	}
	spin_unlock(&pinmux_set_lock);
	return 0;
}
EXPORT_SYMBOL(pinmux_set);
int32_t pinmux_clr(pinmux_set_t* pinmux)
{
	int i;

	if(pinmux==NULL)
	{
	    BUG();
		return -4;
	}

	if(pinmux->chip_select==NULL)///non share device , we should put the pins in same status always
		return 0;
	spin_lock(&pinmux_set_lock);

	pinmux->chip_select(false);
	debug("pinmux_clr : %p" ,pinmux->pinmux);
    for(i=0;pinmux->pinmux[i].reg!=0xffffffff;i++)
	{
		pimux_locktable[pinmux->pinmux[i].reg]&=~(pinmux->pinmux[i].clrmask|pinmux->pinmux[i].setmask);
	}


	for(i=0;pinmux->pinmux[i].reg!=0xffffffff;i++)
	{
        debug("clrsetbits %x %x %x",p_pin_mux_reg_addr[pinmux->pinmux[i].reg],pinmux->pinmux[i].setmask|pinmux->pinmux[i].clrmask,pinmux->pinmux[i].clrmask);
		clrsetbits_le32(p_pin_mux_reg_addr[pinmux->pinmux[i].reg],pinmux->pinmux[i].setmask|pinmux->pinmux[i].clrmask,pinmux->pinmux[i].clrmask);
	}
	wake_up(&pinmux_wait_queue);
	spin_unlock(&pinmux_set_lock);

	return 0;
}
EXPORT_SYMBOL(pinmux_clr);
int32_t pinmux_set_locktable(pinmux_set_t* pinmux )
{
	ulong flags;
	int i;
	if(pinmux==NULL)
		return -4;
	spin_lock_irqsave(&pinmux_set_lock, flags);
	for(i=0;pinmux->pinmux[i].reg!=0xffffffff;i++)
	{
		pimux_locktable[pinmux->pinmux[i].reg]|=pinmux->pinmux[i].clrmask|pinmux->pinmux[i].setmask;
		clrsetbits_le32(p_pin_mux_reg_addr[pinmux->pinmux[i].reg],pinmux->pinmux[i].clrmask,pinmux->pinmux[i].setmask);
	}
	spin_unlock_irqrestore(&pinmux_set_lock, flags);
	return 0;
}
EXPORT_SYMBOL(pinmux_set_locktable);

/**
 * @return 0, success ,
 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
 * 		   NOTAVAILABLE, not available .
 */
static int32_t pad_to_gpio(uint32_t pad)
{
	pinmux_item_t pinmux[P_PIN_MUX_REG_NUM];
	pinmux_set_t dummy;
	memset(&dummy,0,sizeof(dummy));
	if(caculate_single_pinmux_set(pinmux,pad,SIG_GPIOIN)<0)
		return -1;
	dummy.pinmux=&pinmux[0];
	return pinmux_set(&dummy);
}
int32_t gpio_set_status(uint32_t pin,bool gpio_in)
{
	unsigned bit,reg;
	if(pad_to_gpio(pin)<0)
		return -1;
	reg=(pad_gpio_bit[pin]>>5)&0xf;
	bit=(pad_gpio_bit[pin])&0x1f;
	clrsetbits_le32(p_gpio_oen_addr[reg],1<<bit,gpio_in<<bit);
	return 0;
}
EXPORT_SYMBOL(gpio_set_status);

bool gpio_get_status(uint32_t pin)
{
	unsigned bit,reg;

	reg=(pad_gpio_bit[pin]>>5)&0xf;
	bit=(pad_gpio_bit[pin])&0x1f;

	return ((aml_get_reg32_bits(p_gpio_oen_addr[reg],bit, 1))?(gpio_status_in):(gpio_status_out));
}
EXPORT_SYMBOL(gpio_get_status);

int32_t gpio_get_val(uint32_t pin)
{
	unsigned bit,reg;

	reg=(pad_gpio_bit[pin]>>5)&0xf;
	bit=(pad_gpio_bit[pin])&0x1f;

	return aml_get_reg32_bits(p_gpio_in_addr[reg],bit, 1);
}
EXPORT_SYMBOL(gpio_get_val);


/**
 * GPIO out function
 */
int32_t gpio_out(uint32_t pin,bool high)
{
	unsigned bit,reg;
	if(gpio_set_status(pin,false)==0)
	{
		reg=(pad_gpio_bit[pin]>>5)&0xf;
		bit=(pad_gpio_bit[pin])&0x1f;
		if((p_gpio_out_addr[reg]&3)==2)
		{
			reg=p_gpio_out_addr[reg]&(~3);
			bit+=16;
		}else{
		   reg=p_gpio_out_addr[reg];
		}
		clrsetbits_le32(reg,1<<bit,high<<bit);
		return 0;
	};
	return -1;
}
EXPORT_SYMBOL(gpio_out);

/**
 * GPIO out function
 */
int32_t gpio_out_directly(uint32_t pin,bool high)
{
	unsigned bit,reg;
		reg=(pad_gpio_bit[pin]>>5)&0xf;
		bit=(pad_gpio_bit[pin])&0x1f;
		if((p_gpio_out_addr[reg]&3)==2)
		{
			reg=p_gpio_out_addr[reg]&(~3);
			bit+=16;
		}else{
		   reg=p_gpio_out_addr[reg];
		}
		clrsetbits_le32(reg,1<<bit,high<<bit);
		return 0;

}
EXPORT_SYMBOL(gpio_out_directly);
/**
 * GPIO in function .ls
 */
int32_t gpio_in_get(uint32_t pin)
{
	unsigned bit,reg;
	if(gpio_set_status(pin,true)<0)
	{
		printk(" %s , Set gpio to input fail\n",__func__);
		BUG();
	}

	reg=(pad_gpio_bit[pin]>>5)&0xf;
	bit=(pad_gpio_bit[pin])&0x1f;


	return (readl(p_gpio_in_addr[reg])>>bit)&1;

}
EXPORT_SYMBOL(gpio_in_get);
/**
 * Multi pin operation
 * @return 0, success ,
 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
 * 		   NOTAVAILABLE, not available .
 *
 */

gpio_set_t * gpio_out_group_cacl(uint32_t pin,uint32_t bits, ... )
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(gpio_out_group_cacl);
int32_t gpio_out_group_set(gpio_set_t * set,uint32_t high_low )
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return -1;
}
EXPORT_SYMBOL(gpio_out_group_set);

	/**
	 * Multi pin operation
	 */
	/**
	 * Multi pin operation
	 * @return 0, success ,
	 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
	 * 		   NOTAVAILABLE, not available .
	 *
	 */
gpio_set_t * gpio_in_group_cacl(uint32_t pin,uint32_t bits, ... )
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return NULL;
}
EXPORT_SYMBOL(gpio_in_group_cacl);

gpio_in_t gpio_in_group(gpio_in_set_t *grp)
{
	printk(" %s , NOT IMPLENMENT\n",__func__);
    BUG();

	/**
	 * @todo NOT implement;
	 */
	 return 0;
}
EXPORT_SYMBOL(gpio_in_group);

//~ typedef struct gpio_irq_s{
    //~ int8_t    filter;
    //~ uint8_t    irq;///
    //~ uint16_t   pad;
//~ }gpio_irq_t;
static gpio_irq_t gpio_irqs[8]={

};

int32_t gpio_irq_set_lock(int32_t pad, uint32_t irq/*GPIO_IRQ(irq,type)*/,int32_t filter,bool lock)
{
    if(pad>=PAD_MAX_PADS)
        return -1;
    gpio_irqs[(irq>>2)].irq=irq&3;
    gpio_irqs[(irq>>2)].pad=pad;
    gpio_irqs[(irq>>2)].filter=filter&0x7;
    return 0;
}
EXPORT_SYMBOL(gpio_irq_set_lock);
void gpio_irq_enable(uint32_t irq)
{
    int idx=(irq>>2)&7;
    unsigned reg,start_bit;
    unsigned type[]={0x0, ///GPIO_IRQ_HIGH
                    0x10000, ///GPIO_IRQ_LOW
                    0x1,  ///GPIO_IRQ_RISING
                    0x10001, ///GPIO_IRQ_FALLING
                    };
    debug("write reg %p clr=%x set=%x",P_GPIO_INTR_EDGE_POL,0x10001<<idx,type[gpio_irqs[idx].irq]<<idx);
    /// set trigger type
    clrsetbits_le32(P_GPIO_INTR_EDGE_POL,0x10001<<idx,type[gpio_irqs[idx].irq]<<idx);

    ///select pad
    reg=idx<4?P_GPIO_INTR_GPIO_SEL0:P_GPIO_INTR_GPIO_SEL1;
    start_bit=(idx&3)*8;
    clrsetbits_le32(reg,0xff<<start_bit,gpio_irqs[idx].pad<<start_bit);
    debug("write reg %p clr=%x set=%x",reg,0xff<<start_bit,gpio_irqs[idx].pad<<start_bit);
    ///set filter
    start_bit=(idx)*4;
    clrsetbits_le32(P_GPIO_INTR_FILTER_SEL0,0x7<<start_bit,gpio_irqs[idx].filter<<start_bit);
    debug("write reg %p clr=%x set=%x",P_GPIO_INTR_FILTER_SEL0,0x7<<start_bit,gpio_irqs[idx].filter<<start_bit);

}
EXPORT_SYMBOL(gpio_irq_enable);



//#if defined(CONFIG_CARDREADER)
struct gpio_addr {
    unsigned long mode_addr;
    unsigned long out_addr;
    unsigned long in_addr;
};
static struct gpio_addr gpio_addrs[] = {
    [PREG_PAD_GPIO0] = {P_PREG_PAD_GPIO0_EN_N, P_PREG_PAD_GPIO0_O, P_PREG_PAD_GPIO0_I},
    [PREG_PAD_GPIO1] = {P_PREG_PAD_GPIO1_EN_N, P_PREG_PAD_GPIO1_O, P_PREG_PAD_GPIO1_I},
    [PREG_PAD_GPIO2] = {P_PREG_PAD_GPIO2_EN_N, P_PREG_PAD_GPIO2_O, P_PREG_PAD_GPIO2_I},
    [PREG_PAD_GPIO3] = {P_PREG_PAD_GPIO3_EN_N, P_PREG_PAD_GPIO3_O, P_PREG_PAD_GPIO3_I},
    [PREG_PAD_GPIO4] = {P_PREG_PAD_GPIO4_EN_N, P_PREG_PAD_GPIO4_O, P_PREG_PAD_GPIO4_I},
    [PREG_PAD_GPIO5] = {P_PREG_PAD_GPIO5_EN_N, P_PREG_PAD_GPIO5_O, P_PREG_PAD_GPIO5_I},
    [PREG_PAD_GPIOAO] = {P_AO_GPIO_O_EN_N,     P_AO_GPIO_O_EN_N,   P_AO_GPIO_I},
};
#if 0
int gpio_direction_input(unsigned gpio)
{
    gpio_bank_t bank = (gpio_bank_t)(gpio >> 16);
    int bit = gpio & 0xFFFF;
    set_gpio_mode(bank, bit, GPIO_INPUT_MODE);
    //printk("set gpio%d.%d input\n", bank, bit);
    return (get_gpio_val(bank, bit));
}
#endif
void gpio_enable_level_int(int pin , int flag, int group)
{
        group &= 7;

  			aml_set_reg32_bits(P_GPIO_INTR_GPIO_SEL0+(group>>2), pin, (group&3)*8, 8);

        aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, 0, group, 1);
        aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, flag, group+16, 1);
}
int gpio_to_idx(unsigned gpio)
{
    gpio_bank_t bank = (gpio_bank_t)(gpio >> 16);
    int bit = gpio & 0xFFFF;
    int idx = -1;

    switch(bank) {
    case PREG_PAD_GPIO0:
        idx = GPIOA_IDX + bit;
                break;
    case PREG_PAD_GPIO1:
        idx = GPIOB_IDX + bit;
                break;
    case PREG_PAD_GPIO2:
        idx = GPIOC_IDX + bit;
                break;
    case PREG_PAD_GPIO3:
                if( bit < 20 ) {
            idx = GPIO_BOOT_IDX + bit;
                } else {
            idx = GPIOX_IDX + (bit + 12);
                }
                break;
    case PREG_PAD_GPIO4:
        idx = GPIOX_IDX + bit;
                break;
    case PREG_PAD_GPIO5:
                if( bit < 23 ) {
            idx = GPIOY_IDX + bit;
                } else {
                idx = GPIO_CARD_IDX + (bit - 23) ;
                }
                break;
    case PREG_PAD_GPIOAO:
        idx = GPIOAO_IDX + bit;
                break;
        }

    return idx;
}

void gpio_enable_edge_int(int pin , int flag, int group)
{
        group &= 7;

        aml_set_reg32_bits(P_GPIO_INTR_GPIO_SEL0+(group>>2), pin, (group&3)*8, 8);
        aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, 1, group, 1);
        aml_set_reg32_bits(P_GPIO_INTR_EDGE_POL, flag, group+16, 1);
}

int set_gpio_mode(gpio_bank_t bank, int bit, gpio_mode_t mode)
{
    unsigned long addr = gpio_addrs[bank].mode_addr;
#ifdef CONFIG_EXGPIO
    if (bank >= EXGPIO_BANK0) {
        set_exgpio_mode(bank - EXGPIO_BANK0, bit, mode);
        return 0;
    }
#endif
		aml_set_reg32_bits(addr, mode, bit, 1);
    return 0;
}
unsigned long  get_gpio_val(gpio_bank_t bank, int bit)
{
    unsigned long addr = gpio_addrs[bank].in_addr;
#ifdef CONFIG_EXGPIO
    if (bank >= EXGPIO_BANK0) {
        return get_exgpio_val(bank - EXGPIO_BANK0, bit);
    }
#endif
		return aml_get_reg32_bits(addr,bit,1);
}
#if 0
void gpio_free(unsigned gpio)
{
	return;
}

int gpio_request(unsigned gpio, const char *label)
{
	return 0;
}
#endif

//#endif
