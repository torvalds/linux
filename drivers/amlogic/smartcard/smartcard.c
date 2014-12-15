/*
 * AMLOGIC Smart card driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#ifdef CONFIG_ARCH_ARC700
#include <asm/arch/am_regs.h>
#else
#include "linux/clk.h"
#include <mach/am_regs.h>
#endif
#include <mach/devio_aml.h>
#include <mach/gpio.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/amlogic/amsmc.h>
#include <linux/platform_device.h>
#include <linux/time.h>

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#include "smc_reg.h"

#define DRIVER_NAME "amsmc"
#define MODULE_NAME "amsmc"
#define DEVICE_NAME "amsmc"
#define CLASS_NAME  "amsmc-class"

#if 0
#define pr_dbg(fmt, args...) printk("Smartcard: " fmt, ## args)
#else
#define pr_dbg(fmt, args...)
#endif

#define pr_error(fmt, args...) printk(KERN_ERR "Smartcard: " fmt, ## args)

MODULE_PARM_DESC(smc0_irq, "\n\t\t Irq number of smartcard0");
static int smc0_irq = -1;
module_param(smc0_irq, int, S_IRUGO);

MODULE_PARM_DESC(smc0_reset, "\n\t\t Reset GPIO pin of smartcard0");
static int smc0_reset = -1;
module_param(smc0_reset, int, S_IRUGO);

#define NO_HOT_RESET
//#define DISABLE_RECV_INT
#define ATR_FROM_INT

#define RECV_BUF_SIZE     512
#define SEND_BUF_SIZE     512

#define RESET_ENABLE      (smc->reset_level)
#define RESET_DISABLE     (!smc->reset_level)

#define VBUS_SMC_RESET_GPIO_OWNER  "SMARTCARD"

typedef struct {
	int                id;
	struct device     *dev;
	struct platform_device *pdev;
	int                init;
	int                used;
	int                cardin;
	int                active;
	struct mutex       lock;
	spinlock_t         slock;
	wait_queue_head_t  rd_wq;
	wait_queue_head_t  wr_wq;
	int                recv_start;
	int                recv_count;
	int                send_start;
	int                send_count;
	char               recv_buf[RECV_BUF_SIZE];
	char               send_buf[SEND_BUF_SIZE];
	struct am_smc_param param;
	struct am_smc_atr   atr;
	u32                reset_pin;
	int 			(*reset)(void *, int);
	u32                irq_num;
	int                reset_level;
	struct pinctrl     *pinctrl;
} smc_dev_t;

#define SMC_DEV_NAME     "smc"
#define SMC_CLASS_NAME   "smc-class"
#define SMC_DEV_COUNT    1

#define SMC_READ_REG(a)           READ_MPEG_REG(SMARTCARD_##a)
#define SMC_WRITE_REG(a,b)        WRITE_MPEG_REG(SMARTCARD_##a,b)
#define SUSPEND_CARD_STATUS         (0xFF)
/** to enable this in
*** System Tyep-->Amlogic Meson platform-->ARM Generic Interrupt Controller FIQ
*** by make menuconfig if default not be set to y
*** which is used to solve IRQ not responsed sometimes
**/
#ifdef  CONFIG_MESON_ARM_GIC_FIQ
#define SMC_FIQ
#endif
static struct mutex smc_lock;
static int          smc_major;
static smc_dev_t    smc_dev[SMC_DEV_COUNT];
static int ENA_GPIO_PULL = 1;
static int smc_status = -1;

#ifdef SMC_FIQ
#include <plat/fiq_bridge.h>
#define SMC_CNT     (1024)
static char smc_send_data[SMC_CNT];
static char smc_recv_data[SMC_CNT];
static char smc_recv_data_bak[SMC_CNT];//for debug only and only be reset if another serial data be sent to smartcard
//the smc_write_idx always >= smc_write_fiq_idx
static int smc_write_idx = -1;
static int smc_write_fiq_idx = -1;
//the smc_read_idx always <= smc_read_fiq_idx
static int smc_read_fiq_idx = -1;
static int smc_recv_bak_idx = -1;
static int smc_read_idx = -1;
static int smc_cardin = -1;
static int smc_debug_flag = 0;
//volatile
wait_queue_head_t  *smc_rd_wq;
//volatile
wait_queue_head_t  *smc_wr_wq;
static struct timer_list smc_wakeup_wq_timer;
static bridge_item_t smc_fiq_bridge;
static void smc_print_data(char buf[], int cnt);
#endif
static ssize_t show_gpio_pull(struct class *class, struct class_attribute *attr,	char *buf)
{
	if(ENA_GPIO_PULL > 0)
		return sprintf(buf, "%s\n","enable GPIO pull low");
	else
		return sprintf(buf, "%s\n","disable GPIO pull low");
}

static ssize_t set_gpio_pull(struct class *class, struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int dbg;
    ssize_t r;

    r = sscanf(buf, "%d", &dbg);
    if (r != 1)
    return -EINVAL;

    ENA_GPIO_PULL = dbg;
    printk("Smartcard the ENA_GPIO_PULL is:%d.\n", ENA_GPIO_PULL);
    return count;
}

#ifdef SMC_FIQ
static ssize_t smc_show_communication_state(struct class *class, struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int dbg;
    ssize_t r;

    r = sscanf(buf, "%d", &dbg);

    if(smc_debug_flag > 0){
        if(dbg ==0)
        {
            printk(KERN_ALERT"communication OK......\n");
        }else{
            printk(KERN_ALERT"communication failed dbg:%d, smc_recv_bak_idx:%d:\n", dbg, smc_recv_bak_idx);
            smc_print_data(smc_recv_data_bak, smc_recv_bak_idx);
        }
    }
    return count;
}

static ssize_t smc_show_debug(struct class *class, struct class_attribute *attr,	char *buf)
{
    printk(KERN_ALERT"the smc debug flag is:0x%x.\n", smc_debug_flag);

    if(smc_debug_flag > 0)
        return sprintf(buf, "%s\n","smc debug enabled.");
    else
        return sprintf(buf, "%s\n","smc debug disabled.");
}

static ssize_t smc_store_debug(struct class *class, struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int dbg;
    ssize_t r;

    r = sscanf(buf, "%d", &dbg);

    printk(KERN_ALERT"set the smc debug flag to:0x%x.\n", dbg);

    if(dbg > 0)
        smc_debug_flag = dbg;
    else
        smc_debug_flag = 0;

    return count;
}
#endif

static struct class_attribute smc_class_attrs[] = {
    __ATTR(smc_gpio_pull,  S_IRUGO | S_IWUSR, show_gpio_pull,    set_gpio_pull),
#ifdef SMC_FIQ
    __ATTR(smc_com_state,  S_IRUGO | S_IWUSR, NULL,    smc_show_communication_state),
    __ATTR(smc_debug,  S_IRUGO | S_IWUSR, smc_show_debug,    smc_store_debug),
#endif
    __ATTR_NULL
};

static struct class smc_class = {
    .name = SMC_CLASS_NAME,
    .class_attrs = smc_class_attrs,
};

static unsigned long get_clk(char *name)
{
	struct clk *clk=NULL;
	clk = clk_get_sys(name, NULL);
	if(clk)
		return clk_get_rate(clk);
	return 0;
}

static unsigned long get_module_clk(int sel)
{
#ifdef CONFIG_ARCH_ARC700
	extern unsigned long get_mpeg_clk(void);
	return get_mpeg_clk();
#else

	unsigned long clk=0;

#ifdef CONFIG_ARCH_MESON6/*M6*/
	/*sel = [0:clk81, 1:ddr-pll, 2:fclk-div5, 3:XTAL]*/
	switch(sel)
	{
		case 0: clk = get_clk("clk81"); break;
		case 1: clk = get_clk("pll_ddr"); break;
		case 2: clk = get_clk("fixed")/5; break;
		case 3: clk = get_clk("xtal"); break;
	}
#else /*M6TV*/
	/*sel = [0:fclk-div2, 1:fclk-div3, 2:fclk-div5, 3:XTAL]*/
	switch(sel)
	{
		case 0: clk = 1000000000; break;
		//case 0: clk = get_clk("fixed")/2; break;
		case 1: clk = get_clk("fixed")/3; break;
		case 2: clk = get_clk("fixed")/5; break;
		case 3: clk = get_clk("xtal"); break;
	}
#endif /*M6*/

	if(!clk)
		printk("fail: unknown clk source");

	return clk;

#endif
}

static int inline smc_write_end(smc_dev_t *smc)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&smc->slock, flags);
	ret = (!smc->cardin || !smc->send_count);
	spin_unlock_irqrestore(&smc->slock, flags);

	return ret;
}


static int inline smc_can_read(smc_dev_t *smc)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&smc->slock, flags);
	ret = (!smc->cardin || smc->recv_count);
	spin_unlock_irqrestore(&smc->slock, flags);

	return ret;
}

static int inline smc_can_write(smc_dev_t *smc)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&smc->slock, flags);
	ret = (!smc->cardin || (smc->send_count!=SEND_BUF_SIZE));
	spin_unlock_irqrestore(&smc->slock, flags);

	return ret;
}

static int smc_hw_set_param(smc_dev_t *smc)
{
	unsigned long v=0;
	SMCCARD_HW_Reg0_t *reg0;
	//SMC_ANSWER_TO_RST_t *reg1;
	//SMCCARD_HW_Reg2_t *reg2;
	//SMC_INTERRUPT_Reg_t *reg_int;
	//SMCCARD_HW_Reg5_t *reg5;
	SMCCARD_HW_Reg6_t *reg6;

	v = SMC_READ_REG(REG0);
	reg0 = (SMCCARD_HW_Reg0_t*)&v;
	reg0->etu_divider = ETU_DIVIDER_CLOCK_HZ*smc->param.f/(smc->param.d*smc->param.freq)-1;
	SMC_WRITE_REG(REG0, v);

	v = SMC_READ_REG(REG6);
	reg6 = (SMCCARD_HW_Reg6_t*)&v;
	reg6->N_parameter = smc->param.n;
	reg6->cwi_value = smc->param.cwi;
	reg6->bgt = smc->param.bgt;
	reg6->bwi = smc->param.bwi;
	SMC_WRITE_REG(REG6, v);

	return 0;
}

static int smc_hw_setup(smc_dev_t *smc)
{
	unsigned long v=0;
	SMCCARD_HW_Reg0_t *reg0;
	SMC_ANSWER_TO_RST_t *reg1;
	SMCCARD_HW_Reg2_t *reg2;
	SMC_INTERRUPT_Reg_t *reg_int;
	SMCCARD_HW_Reg5_t *reg5;
	SMCCARD_HW_Reg6_t *reg6;

	unsigned sys_clk_rate = get_module_clk(CLK_SRC_DEFAULT);

	unsigned long freq_cpu = sys_clk_rate/1000;

	printk("SMC CLK SOURCE - %luKHz\n", freq_cpu);

	v = SMC_READ_REG(REG0);
	reg0 = (SMCCARD_HW_Reg0_t*)&v;
	reg0->enable = 1;
	reg0->clk_en = 0;
	reg0->clk_oen = 0;
	reg0->card_detect = 0;
	reg0->start_atr = 0;
	reg0->start_atr_en = 0;
	reg0->rst_level = RESET_DISABLE;
	reg0->io_level = 0;
	reg0->recv_fifo_threshold = FIFO_THRESHOLD_DEFAULT;
	reg0->etu_divider = ETU_DIVIDER_CLOCK_HZ*smc->param.f/(smc->param.d*smc->param.freq)-1;
	SMC_WRITE_REG(REG0, v);

	v = SMC_READ_REG(REG1);
	reg1 = (SMC_ANSWER_TO_RST_t*)&v;
	reg1->atr_final_tcnt = ATR_FINAL_TCNT_DEFAULT;
	reg1->atr_holdoff_tcnt = ATR_HOLDOFF_TCNT_DEFAULT;
	reg1->atr_clk_mux = ATR_CLK_MUX_DEFAULT;
	reg1->atr_holdoff_en = ATR_HOLDOFF_EN;
	SMC_WRITE_REG(REG1, v);

	v = SMC_READ_REG(REG2);
	reg2 = (SMCCARD_HW_Reg2_t*)&v;
	reg2->recv_invert = smc->param.recv_invert;
	reg2->recv_lsb_msb = smc->param.recv_lsb_msb;
	reg2->xmit_invert = smc->param.xmit_invert;
	reg2->xmit_lsb_msb = smc->param.xmit_lsb_msb;
	reg2->xmit_retries = smc->param.xmit_retries;
	reg2->xmit_repeat_dis = smc->param.xmit_repeat_dis;
	reg2->recv_no_parity = smc->param.recv_no_parity;
	reg2->clk_tcnt = freq_cpu/smc->param.freq - 1;
	reg2->det_filter_sel = DET_FILTER_SEL_DEFAULT;
	reg2->io_filter_sel = IO_FILTER_SEL_DEFAULT;
	reg2->clk_sel = CLK_SRC_DEFAULT;
	//reg2->pulse_irq = 0;
	SMC_WRITE_REG(REG2, v);

	v = SMC_READ_REG(INTR);
	reg_int = (SMC_INTERRUPT_Reg_t*)&v;
	reg_int->recv_fifo_bytes_threshold_int_mask = 0;
	reg_int->send_fifo_last_byte_int_mask = 1;
	reg_int->cwt_expeired_int_mask = 1;
	reg_int->bwt_expeired_int_mask = 1;
	reg_int->write_full_fifo_int_mask = 1;
	reg_int->send_and_recv_confilt_int_mask = 1;
	reg_int->recv_error_int_mask = 1;
	reg_int->send_error_int_mask = 1;
	reg_int->rst_expired_int_mask = 1;
	reg_int->card_detect_int_mask = 0;
	SMC_WRITE_REG(INTR,v|0x03FF);

	v = SMC_READ_REG(REG5);
	reg5 = (SMCCARD_HW_Reg5_t*)&v;
	reg5->cwt_detect_en = 1;
	reg5->bwt_base_time_gnt = BWT_BASE_DEFAULT;
	SMC_WRITE_REG(REG5, v);


	v = SMC_READ_REG(REG6);
	reg6 = (SMCCARD_HW_Reg6_t*)&v;
	reg6->N_parameter = smc->param.n;
	reg6->cwi_value = smc->param.cwi;
	reg6->bgt = smc->param.bgt;
	reg6->bwi = smc->param.bwi;
	SMC_WRITE_REG(REG6, v);

	return 0;
}

static void enable_smc_clk(void){
    unsigned int _value = READ_CBUS_REG(0x2030);
    _value |= 0x100000;
    WRITE_CBUS_REG(0x2030, _value);
    _value = READ_CBUS_REG(0x2030);
}

static void disable_smc_clk(void){
    unsigned int _value = READ_CBUS_REG(0x2030);
    _value &= 0xFFEFFFFF;
    WRITE_CBUS_REG(0x2030, _value);
    _value = READ_CBUS_REG(0x2030);
    return;
}

static int smc_hw_active2(smc_dev_t *smc)
{
	if(smc->reset_pin != -1) {
		amlogic_gpio_direction_output(smc->reset_pin,0,VBUS_SMC_RESET_GPIO_OWNER);
	}
    return 0;
}

static int smc_hw_deactive2(smc_dev_t *smc)
{
	if(smc->reset_pin != -1) {
		amlogic_gpio_direction_output(smc->reset_pin,1,VBUS_SMC_RESET_GPIO_OWNER);
	}
    return 0;
}

static int smc_hw_active(smc_dev_t *smc)
{
    if(ENA_GPIO_PULL > 0){
        enable_smc_clk();
        udelay(200);
    }
	if(!smc->active) {
		if(smc->reset) {
			smc->reset(NULL, 0);
		} else {
			if(smc->reset_pin != -1) {
				amlogic_gpio_direction_output(smc->reset_pin,0,VBUS_SMC_RESET_GPIO_OWNER);
			}
		}

		udelay(200);
		smc_hw_setup(smc);

		smc->active = 1;
	}

	return 0;
}

static int smc_hw_deactive(smc_dev_t *smc)
{
	if(smc->active) {
		unsigned long sc_reg0 = SMC_READ_REG(REG0);
		SMCCARD_HW_Reg0_t *sc_reg0_reg = (void *)&sc_reg0;
		sc_reg0_reg->rst_level = RESET_DISABLE;
		sc_reg0_reg->enable= 1;
		sc_reg0_reg->start_atr = 0;
		sc_reg0_reg->start_atr_en = 0;
		sc_reg0_reg->clk_en=0;
		SMC_WRITE_REG(REG0,sc_reg0);

		udelay(200);

		if(smc->reset) {
			smc->reset(NULL, 1);
		} else {
			if(smc->reset_pin != -1) {
				amlogic_gpio_direction_output(smc->reset_pin,1,VBUS_SMC_RESET_GPIO_OWNER);
			}
		}
            if(ENA_GPIO_PULL > 0){
                 disable_smc_clk();
                 udelay(1000);
            }
		smc->active = 0;
	}

	return 0;
}

#if 0
static int smc_hw_get(smc_dev_t *smc, int cnt, int timeout)
{

	unsigned long sc_status;
	int times = timeout*100;
	SMC_STATUS_Reg_t *sc_status_reg = (SMC_STATUS_Reg_t*)&sc_status;

	while((times > 0) && (cnt > 0)) {
		sc_status = SMC_READ_REG(STATUS);

		//pr_dbg("read atr status %08x\n", sc_status);

		if(sc_status_reg->rst_expired_status)
		{
			//pr_error("atr timeout\n");
		}

		if(sc_status_reg->cwt_expeired_status)
		{
			//pr_error("cwt timeout when get atr, but maybe it is natural!\n");
		}

		if(sc_status_reg->recv_fifo_empty_status)
		{
			udelay(10);
			times--;
		}
		else
		{
			while(sc_status_reg->recv_fifo_bytes_number > 0)
			{
				smc->atr.atr[smc->atr.atr_len++] = (SMC_READ_REG(FIFO))&0xff;
				sc_status_reg->recv_fifo_bytes_number--;
				cnt--;
				if(cnt==0){
					pr_dbg("read atr bytes ok\n");
					return 0;
				}
			}
		}
	}

	pr_error("read atr failed\n");
	return -1;
}
#endif

#ifdef SMC_FIQ
static void smc_print_data(char buf[], int cnt)
{
    int temp = cnt/8,idx = 0,left_cnt = 0;

    for(idx = 0; idx < temp*8;){
        printk(KERN_ALERT"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
        buf[idx+0],  buf[idx+1],buf[idx+2],  buf[idx+3],buf[idx+4],  buf[idx+5],buf[idx+6],  buf[idx+7]);
        idx+=8;
    }

    left_cnt = cnt - temp * 8;

    if(left_cnt > 0){
        switch(left_cnt){
            case 7:
                printk(KERN_ALERT"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
                buf[temp * 8+0],  buf[temp * 8+1],buf[temp * 8+2],  buf[temp * 8+3],
                buf[temp * 8+4],  buf[temp * 8+5],buf[temp * 8+6]);
            break;

            case 6:
                printk(KERN_ALERT"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
                buf[temp * 8+0],  buf[temp * 8+1],buf[temp * 8+2],  buf[temp * 8+3],
                buf[temp * 8+4],  buf[temp * 8+5]);
            break;

            case 5:
                printk(KERN_ALERT"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
                buf[temp * 8+0],  buf[temp * 8+1],buf[temp * 8+2],  buf[temp * 8+3],
                buf[temp * 8+4]);
            break;

            case 4:
                printk(KERN_ALERT"0x%02x 0x%02x 0x%02x 0x%02x\n",
                buf[temp * 8+0],  buf[temp * 8+1],buf[temp * 8+2],  buf[temp * 8+3]);
            break;

            case 3:
                printk(KERN_ALERT"0x%02x 0x%02x 0x%02x\n",
                buf[temp * 8+0],  buf[temp * 8+1],buf[temp * 8+2]);
            break;

            case 2:
                printk(KERN_ALERT"0x%02x 0x%02x\n",
                buf[temp * 8+0],  buf[temp * 8+1]);
            break;

            case 1:
                printk(KERN_ALERT"0x%02x\n",
                buf[temp * 8+0]);
            break;
        }
    }

}

/*==============================================================
*  Function Name:
*  Function Description:to check wheter api like smc_read can read/write data from/to the fiq array
*  Function Parameters:
*===============================================================
*/
static bool smc_read_write_check(bool r_flag, int r_fiq_idx, int w_fiq_idx)
{
    //to check read flag
    if(r_flag == true){
        if(r_fiq_idx < 0)
            r_fiq_idx = smc_read_fiq_idx;
        //printk(KERN_ALERT"smc read write check r_fiq_idx:%d, smc_read_idx:%d.\n",r_fiq_idx, smc_read_idx);
        if((r_fiq_idx > 0)&&(r_fiq_idx != smc_read_idx))
            return true;
        else if((r_fiq_idx == 0)&&(smc_read_idx > 0))
            return true;
        else
            return false;
    }else if(r_flag == false){
        if(w_fiq_idx < 0)
            w_fiq_idx = smc_write_fiq_idx;
        //printk(KERN_ALERT"smc read write check w_fiq_idx:%d, smc_write_idx:%d.\n",w_fiq_idx, smc_write_idx);
        //if not equal that means FIQ need more time to sent the data to smartcard and API should poll again
        if(w_fiq_idx == smc_write_idx)
            return true;
        else
            return false;
    }

    return false;
}

/*==============================================================
*  Function Name:
*  Function Description:to check wheter api like smc_read can read/write data from/to the fiq array
*                               if there are more data avaible
*  Function Parameters:
*===============================================================
*/
static int smc_read_write_data(bool r_flag, char temp_buf[], int size)
{
    int cnt = 0, smc_read_fiq_idx_temp, smc_write_fiq_idx_temp;

    //if read and read avaible
    if(r_flag){
        smc_read_fiq_idx_temp = smc_read_fiq_idx;//this can be read only only since it will be updated by FIQ
        //printk(KERN_ALERT"before the size is:%d, the smc_read_fiq_idx_temp:%d, the smc_read_idx:%d.\n", size, smc_read_fiq_idx_temp, smc_read_idx);
        if(smc_read_fiq_idx_temp > smc_read_idx){//this case: |||||smc_read_idx||||||smc_read_fiq_idx|||||
            if((smc_read_fiq_idx_temp - smc_read_idx) < size){
                //printk(KERN_ALERT"(smc_read_fiq_idx > smc_read_idx)");
                return -1;
            }
        }else if(smc_read_fiq_idx_temp < smc_read_idx){//this case: |||||smc_read_fiq_idx||||||smc_read_idx|||||
            cnt = SMC_CNT - smc_read_idx + smc_read_fiq_idx_temp;
            if(size > cnt){
                //printk(KERN_ALERT"(smc_read_fiq_idx_temp < smc_read_idx)");
                return -1;
            }
        }else if(smc_read_fiq_idx_temp == smc_read_idx){//this case: |||||smc_read_fiq_idx smc_read_idx|||||
            //printk(KERN_ALERT"(smc_read_fiq_idx == smc_read_idx)");
            return -1;
        }else{
            //printk(KERN_ALERT"(read else return -1 which should not be happened.\n)");
            return -1;
        }

        cnt = 0;
        while(smc_read_write_check(true, smc_read_fiq_idx_temp, -1)){
            if(cnt == size){
                break;
            }
            //and the smc_read_idx can be updated here only
            smc_read_idx%=SMC_CNT;
            temp_buf[cnt++] = smc_recv_data[smc_read_idx++];
        }
        //printk(KERN_ALERT"after the size is:%d, the smc_read_fiq_idx_temp:%d, the smc_read_idx:%d.\n", size, smc_read_fiq_idx_temp, smc_read_idx);
        return cnt;
    }else if(r_flag == false){
        //assume there are always enought space for API to write fiq array because
        //first, write data to smartcard is active and indicate
        //second, FIQ will send the data to smartcard at a very high speed
        smc_write_fiq_idx_temp = smc_write_fiq_idx;
        cnt = 0;
        //this means the data has been send out and here we should not set FIQ send data enable trigger
        if(smc_write_fiq_idx_temp == smc_write_idx){
            while(cnt != size){
                smc_write_idx%=SMC_CNT;
                smc_send_data[smc_write_idx++] = temp_buf[cnt++];
            }
            return size;
        }else{
            return -1;
        }
    }else{
        ;//this will not happen
    }

    return -1;
}

/*==============================================================
*  Function Name:
*  Function Description: this can be called only during smartcard reset and module init
*  Function Parameters:
*===============================================================
*/
static irqreturn_t smc_bridge_isr(int irq, void *dev_id)
{
    //smc_dev_t *smc = (smc_dev_t*)dev_id;

    bool can_read = false, can_write = false;
    int temp_r_idx = smc_read_fiq_idx, temp_w_idx = smc_write_fiq_idx;

    can_read  = smc_read_write_check(true, temp_r_idx, temp_w_idx);
    can_write = smc_read_write_check(false, temp_r_idx, temp_w_idx);

    if(can_read) wake_up_interruptible(smc_rd_wq);
    if(can_write) wake_up_interruptible(smc_wr_wq);

    //if(can_read && can_write)printk(KERN_ALERT"smc bridge isr func be called.......and can rw.\n");
    return IRQ_HANDLED;
}

/*==============================================================
*  Function Name:
*  Function Description: this can be called only during smartcard reset and module init
*  Function Parameters:
*===============================================================
*/
static void smc_wakeup_wq_timer_fun(unsigned long arg)
{
#if 0
    bool can_read = false, can_write = false;
    int temp_r_idx = smc_read_fiq_idx, temp_w_idx = smc_write_fiq_idx;

    can_read  = smc_read_write_check(true, temp_r_idx, temp_w_idx);
    can_write = smc_read_write_check(false, temp_r_idx, temp_w_idx);

    if(can_read) wake_up_interruptible(smc_rd_wq);
    if(can_write) wake_up_interruptible(smc_wr_wq);

    if(can_read && can_write)
    printk(KERN_ALERT"wake up func be called.......\n");
    mod_timer(&smc_wakeup_wq_timer,jiffies + 20);
#endif
}

/*==============================================================
*  Function Name:
*  Function Description: this can be called only during smartcard reset and module init
*  Function Parameters:
*===============================================================
*/
static void smc_reset_fiq_varible(void)
{
    int idx = 0;

    for(idx = 0; idx < SMC_CNT; idx++){
        smc_send_data[idx] = 0;
        smc_recv_data[idx] = 0;
    }

    smc_write_idx = 0;
    smc_write_fiq_idx = 0;
    smc_read_fiq_idx = 0;
    smc_read_idx = 0;
}

/*==============================================================
*  Function Name:
*  Function Description: this can be called only during smartcard reset and module init
*  Function Parameters:
*===============================================================
*/
static int smc_fiq_get(smc_dev_t *smc, int size, int timeout)
{
    int times = timeout/10, ret = 0, idx = 0;
    char data_buf[512];

    if(!times)
        times = 1;

    if(!(size > 0)){
        printk(KERN_ALERT"smc fiq get size not large 0.\n");
        return -1;
    }

    if(!smc){
        printk(KERN_ALERT"smc fiq get smc NULL.\n");
        return -1;
    }

    while(times > 0){
        ret = smc_read_write_data(true,data_buf,size);
        //printk(KERN_ALERT"to read atr ret is:%d.\n", ret);
        if(ret < 0){
            msleep(10);
            times--;
        }else if(ret == size){
            for(idx = 0; idx < size; idx++){
                smc->atr.atr[smc->atr.atr_len+idx] = data_buf[idx];
            }
            smc->atr.atr_len+=size;
            return 0;
        }
    }

    return -1;

}
#endif

static int smc_get(smc_dev_t *smc, int size, int timeout)
{
	unsigned long flags;
	int pos = 0, ret=0;
	int times = timeout/10;

	if(!times)
		times = 1;

	while((times>0) && (size>0)) {

		spin_lock_irqsave(&smc->slock, flags);

		if(!smc->cardin) {
			ret = -ENODEV;
		} else if (!smc->recv_count) {
			ret = -EAGAIN;
		} else {
			ret = smc->recv_count;
			if(ret>size) ret = size;

			pos = smc->recv_start;
			smc->recv_start += ret;
			smc->recv_count -= ret;
			smc->recv_start %= RECV_BUF_SIZE;
		}

		spin_unlock_irqrestore(&smc->slock, flags);

		if(ret>0) {
			int cnt = RECV_BUF_SIZE-pos;
			int i;
			unsigned char byte = 0xff;

			pr_dbg("read %d bytes\n", ret);

			if(cnt>=ret) {
				for(i=0; i<ret; i++){
					byte = smc->recv_buf[pos+i];
//					if(SC_DIRECT != smcTpye)
//						byte = inv_table[byte&0xff];
					smc->atr.atr[smc->atr.atr_len+i] = byte;
				}
				//memcpy(&smc->atr.atr[smc->atr.atr_len], smc->recv_buf+pos, ret);
				smc->atr.atr_len+=ret;
			} else {
				int cnt1 = ret-cnt;

				for(i=0; i<cnt; i++){
					byte = smc->recv_buf[pos+i];
//					if(SC_DIRECT != smcTpye)
//						byte = inv_table[byte&0xff];
					smc->atr.atr[smc->atr.atr_len+i] = byte;
				}
				//memcpy(&smc->atr.atr[smc->atr.atr_len], smc->recv_buf+pos, cnt);
				smc->atr.atr_len+=cnt;

				for(i=0; i<cnt1; i++){
					byte = smc->recv_buf[i];
//					if(SC_DIRECT != smcTpye)
//						byte = inv_table[byte&0xff];
					smc->atr.atr[smc->atr.atr_len+i] = byte;
				}
				//memcpy(&smc->atr.atr[smc->atr.atr_len], smc->recv_buf, cnt1);
				smc->atr.atr_len+=cnt1;
			}
			size-=ret;
		} else {
			msleep(10);
			times--;
		}
	}

	if(size>0)
		ret = -ETIMEDOUT;

	return ret;
}

static int smc_hw_read_atr(smc_dev_t *smc)
{
	char *ptr = smc->atr.atr;
	int his_len, t, tnext = 0, only_t0 = 1, loop_cnt=0;
	int i;

	printk(KERN_ALERT"read atr\n");

#ifdef ATR_FROM_INT
#ifdef SMC_FIQ
#define smc_hw_get smc_fiq_get
#else
#define smc_hw_get smc_get
#endif
#endif

	smc->atr.atr_len = 0;
#if 0
	if(smc_hw_get(smc, 2, 2000)<0){
		goto end;
	}
#else
	if(smc_hw_get(smc, 1, 2000)<0)
		goto end;

	if(smc_hw_get(smc, 1, 2000)<0)
		goto end;
#endif

	if(ptr[0] == 0){
		smc->atr.atr[0] = smc->atr.atr[1];
		smc->atr.atr_len = 1;
		if(smc_hw_get(smc, 1, 2000)<0){
			goto end;
		}
	}

	ptr++;
	his_len = ptr[0]&0x0F;

	do {
		tnext = 0;
		loop_cnt++;
		if(ptr[0]&0x10) {
			if(smc_hw_get(smc, 1, 1000)<0)
				goto end;
		}
		if(ptr[0]&0x20) {
			if(smc_hw_get(smc, 1, 1000)<0)
				goto end;
		}
		if(ptr[0]&0x40) {
			if(smc_hw_get(smc, 1, 1000)<0)
				goto end;
		}
		if(ptr[0]&0x80) {
			if(smc_hw_get(smc, 1, 1000)<0)
				goto end;
			ptr = &smc->atr.atr[smc->atr.atr_len-1];
			t = ptr[0]&0x0F;
			if(t) {
				only_t0 = 0;
			}
			if(ptr[0]&0xF0) {
				tnext = 1;
			}
		}
	} while(tnext && loop_cnt<4);

	if(!only_t0) his_len++;
	smc_hw_get(smc, his_len, 1000);

	printk(KERN_ALERT"get atr len:%d data: ", smc->atr.atr_len);
	for(i=0; i<smc->atr.atr_len; i++){
		printk(KERN_ALERT"%02x ", smc->atr.atr[i]);
	}
	printk(KERN_ALERT"\n");

	return 0;

end:
	pr_error("read atr failed\n");
	return -EIO;

#ifdef ATR_FROM_INT
#undef smc_hw_get
#endif
}

static int smc_hw_reset(smc_dev_t *smc)
{
	unsigned long flags;
	int ret;
	unsigned long sc_reg0 = SMC_READ_REG(REG0);
	SMCCARD_HW_Reg0_t *sc_reg0_reg = (void *)&sc_reg0;
	unsigned long sc_int;
	SMC_INTERRUPT_Reg_t *sc_int_reg = (void *)&sc_int;

	spin_lock_irqsave(&smc->slock, flags);
	if(smc->cardin) {
		ret = 0;
              //smc_status = smc->cardin;
              //printk(KERN_ALERT"smc_hw_reset card status is:%d.\n", smc_status);
	} else {
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(&smc->slock, flags);

	if(ret>=0) {
		/*Reset*/
#ifdef NO_HOT_RESET
		smc->active = 0;
#endif
		if(smc->active) {
			sc_reg0_reg->rst_level = RESET_DISABLE;
			sc_reg0_reg->clk_en = 1;
			sc_reg0_reg->etu_divider = ETU_DIVIDER_CLOCK_HZ*smc->param.f/(smc->param.d*smc->param.freq)-1;

			pr_dbg("hot reset\n");

			SMC_WRITE_REG(REG0, sc_reg0);

			udelay(800/smc->param.freq); // >= 400/f ;

			/* disable receive interrupt*/
			sc_int = SMC_READ_REG(INTR);
			sc_int_reg->recv_fifo_bytes_threshold_int_mask = 0;
			SMC_WRITE_REG(INTR, sc_int|0x3FF);

			sc_reg0_reg->rst_level = RESET_ENABLE;
			sc_reg0_reg->start_atr = 1;
			SMC_WRITE_REG(REG0, sc_reg0);
		} else {
			printk(KERN_ALERT"cold reset build at %s %s.\n",__DATE__,__TIME__);
#ifdef SMC_FIQ
                    smc_reset_fiq_varible();
                    smc_rd_wq = &smc->rd_wq;
                    smc_wr_wq = &smc->wr_wq;
#endif
			smc_hw_deactive(smc);
			udelay(200);
			smc_hw_active(smc);
			udelay(200);
			{
				int idx = 0;
				sc_reg0_reg->rst_level = RESET_ENABLE;
				sc_reg0_reg->enable = 1;
				SMC_WRITE_REG(REG0, sc_reg0);

				for(idx = 0; idx < 50; idx++){
					udelay(2000);
				}

				smc_hw_deactive2(smc);

				for(idx = 0; idx < 40; idx++){
					if(idx == 35){
						sc_reg0_reg->enable = 0;
						sc_reg0_reg->clk_en =1 ;
						sc_reg0_reg->rst_level = RESET_DISABLE;
						SMC_WRITE_REG(REG0, sc_reg0);
					}
					udelay(2000);
				}
				smc_hw_active2(smc);
				sc_reg0_reg->rst_level = RESET_ENABLE;
				sc_reg0_reg->enable = 1;
				SMC_WRITE_REG(REG0, sc_reg0);
				for(idx = 0; idx < 10; idx++)
					udelay(2000);
			}

			sc_reg0_reg->clk_en =1 ;
			sc_reg0_reg->enable = 0;
			sc_reg0_reg->rst_level = RESET_DISABLE;
			SMC_WRITE_REG(REG0, sc_reg0);

			udelay(2000); // >= 400/f ;

			/* disable receive interrupt*/
			sc_int = SMC_READ_REG(INTR);
			sc_int_reg->recv_fifo_bytes_threshold_int_mask = 0;
			SMC_WRITE_REG(INTR, sc_int|0x3FF);

			sc_reg0_reg->rst_level = RESET_ENABLE;
			sc_reg0_reg->start_atr_en = 1;
			sc_reg0_reg->start_atr = 1;
			sc_reg0_reg->enable = 1;
			SMC_WRITE_REG(REG0, sc_reg0);
		}

		/*reset recv&send buf*/
		smc->send_start = 0;
		smc->send_count = 0;
		smc->recv_start = 0;
		smc->recv_count = 0;

		/*Read ATR*/
		smc->atr.atr_len = 0;
		smc->recv_count = 0;
		smc->send_count = 0;

#ifdef ATR_FROM_INT
		printk(KERN_ALERT"ATR from INT\n");
		/* enable receive interrupt*/
		sc_int = SMC_READ_REG(INTR);
		sc_int_reg->recv_fifo_bytes_threshold_int_mask = 1;
		SMC_WRITE_REG(INTR, sc_int|0x3FF);
#endif

		ret = smc_hw_read_atr(smc);
#ifdef SMC_FIQ
		smc_reset_fiq_varible();
#endif

#ifdef ATR_FROM_INT
		/* disable receive interrupt*/
		sc_int = SMC_READ_REG(INTR);
		sc_int_reg->recv_fifo_bytes_threshold_int_mask = 0;
		SMC_WRITE_REG(INTR, sc_int|0x3FF);
#endif

		/*Disable ATR*/
		sc_reg0 = SMC_READ_REG(REG0);
		sc_reg0_reg->start_atr_en = 0;
		sc_reg0_reg->start_atr = 0;
		SMC_WRITE_REG(REG0,sc_reg0);

#ifndef DISABLE_RECV_INT
		sc_int_reg->recv_fifo_bytes_threshold_int_mask = 1;
#endif
		SMC_WRITE_REG(INTR, sc_int|0x3FF);
	}

	return ret;
}

static int smc_hw_get_status(smc_dev_t *smc, int *sret)
{
    unsigned long flags;
    unsigned int  reg_val;
    SMCCARD_HW_Reg0_t *reg = (SMCCARD_HW_Reg0_t*)&reg_val;
#ifdef SMC_FIQ
    //bool can_read = false, can_write = false;
    //int temp_r_idx = smc_read_fiq_idx, temp_w_idx = smc_write_fiq_idx;
#endif
    spin_lock_irqsave(&smc->slock, flags);

#ifdef SMC_FIQ
    //can_read  = smc_read_write_check(true, temp_r_idx, temp_w_idx);
    //can_write = smc_read_write_check(false, temp_r_idx, temp_w_idx);
    //printk(KERN_ALERT"read/write:%d/%d.\n", can_read, can_write);
    //if(can_read)
        //wake_up_interruptible(&smc->rd_wq);
    //if(can_write)
        //wake_up_interruptible(&smc->wr_wq);
#endif

	reg_val = SMC_READ_REG(REG0);

	smc->cardin = reg->card_detect;

	//pr_dbg("get_status: smc reg0 %08x, card detect: %d\n", reg_val, smc->cardin);
#if 0
	if(smc_status != smc->cardin){
            //printk(KERN_ALERT"smc_hw_get_status card status changed from %d to %d.", smc_status, smc->cardin);
            if(smc_status == SUSPEND_CARD_STATUS)
                smc->cardin = 0;//to pass user API an artificial status of card removed to let API reset card again to enable communication ok while suspend/resume

            smc_status = smc->cardin;
       }
#endif
	*sret = smc->cardin;

	spin_unlock_irqrestore(&smc->slock, flags);

	return 0;
}

#ifdef SMC_FIQ
static int smc_hw_start_send(smc_dev_t *smc)
{
    unsigned int sc_status;
    SMC_STATUS_Reg_t *sc_status_reg = (SMC_STATUS_Reg_t*)&sc_status;
    u8 byte;
    int cnt = 0, smc_write_idx_temp;

    while(1) {
        smc_write_idx_temp = smc_write_idx;
        sc_status = SMC_READ_REG(STATUS);
        if ((smc_write_idx_temp == smc_write_fiq_idx) || sc_status_reg->send_fifo_full_status){
            break;
        }
        smc_write_fiq_idx%=SMC_CNT;
        byte = smc_send_data[smc_write_fiq_idx++];
        SMC_WRITE_REG(FIFO, byte);
        cnt++;
    }
    //printk(KERN_ALERT"smc_hw_start_send %d byte data to smartcard.\n", cnt);

    return 0;
}
#else
static int smc_hw_start_send(smc_dev_t *smc)
{
	unsigned long flags;
	unsigned int sc_status;
	SMC_STATUS_Reg_t *sc_status_reg = (SMC_STATUS_Reg_t*)&sc_status;
	u8 byte;
	int cnt = 0;

	while(1) {
		spin_lock_irqsave(&smc->slock, flags);

		sc_status = SMC_READ_REG(STATUS);
		if (!smc->send_count || sc_status_reg->send_fifo_full_status) {
			spin_unlock_irqrestore(&smc->slock, flags);
			break;
		}

		pr_dbg("s i f [%d], [%d]\n", smc->send_count, cnt);
		byte = smc->send_buf[smc->send_start++];
		SMC_WRITE_REG(FIFO, byte);
		smc->send_start %= SEND_BUF_SIZE;
		smc->send_count--;
		cnt++;

		spin_unlock_irqrestore(&smc->slock, flags);
	}

	pr_dbg("send %d bytes to hw\n", cnt);

	return 0;
}
#endif

#ifdef SMC_FIQ
/*==============================================================
*  Function Name: FIQ hander
*  Function Description: to solve sometimes irq will not response, which will result in CA data lost
*  Function Parameters:
*===============================================================
*/
static void smc_irq_handler(void)
{
    unsigned int sc_status;
    unsigned int sc_reg0;
    unsigned int sc_int;
    SMC_STATUS_Reg_t *sc_status_reg = (SMC_STATUS_Reg_t*)&sc_status;
    SMC_INTERRUPT_Reg_t *sc_int_reg = (SMC_INTERRUPT_Reg_t*)&sc_int;
    SMCCARD_HW_Reg0_t *sc_reg0_reg = (void *)&sc_reg0;

    sc_int = SMC_READ_REG(INTR);

    //if data avaible to read
    if(sc_int_reg->recv_fifo_bytes_threshold_int){
        int total_data = 0;
        sc_status = SMC_READ_REG(STATUS);
        total_data = sc_status_reg->recv_fifo_bytes_number ;
        while(total_data > 0){
            smc_read_fiq_idx%=SMC_CNT;
            smc_recv_data[smc_read_fiq_idx++] = SMC_READ_REG(FIFO);
            smc_recv_bak_idx%=SMC_CNT;
            smc_recv_data_bak[smc_recv_bak_idx++] = smc_recv_data[smc_read_fiq_idx-1];
            total_data--;
            sc_status = SMC_READ_REG(STATUS);
        }
        sc_int_reg->recv_fifo_bytes_threshold_int = 0;
        //no system call like kmalloc spinlock wait queue mutex_lock.. can be call in FIQ
        //wake_up_interruptible(&smc_rd_wq);
        //mod_timer(&smc_wakeup_wq_timer,jiffies + 20);
        fiq_bridge_pulse_trigger(&smc_fiq_bridge);
    }

    //if more data need to be sent to the smartcard
    if(sc_int_reg->send_fifo_last_byte_int) {
        int cnt = 0,smc_write_idx_temp = -1;
        char byte;

        while(1) {
            smc_write_idx_temp = smc_write_idx;//need update here everytime?
            sc_status = SMC_READ_REG(STATUS);
            if ((smc_write_idx_temp == smc_write_fiq_idx) || sc_status_reg->send_fifo_full_status){
                break;
            }
            smc_write_fiq_idx%=SMC_CNT;
            byte = smc_send_data[smc_write_fiq_idx++];
            SMC_WRITE_REG(FIFO, byte);
            cnt++;
        }

        //if no more data be sent, which means we are going to receive data from smartcard
        if(smc_write_idx_temp == smc_write_fiq_idx){
            sc_int_reg->send_fifo_last_byte_int_mask = 0;
            sc_int_reg->recv_fifo_bytes_threshold_int_mask = 1;
            //no system call like kmalloc spinlock wait queue mutex_lock.. can be call in FIQ
            //wake_up_interruptible(&smc_rd_wq);
            //mod_timer(&smc_wakeup_wq_timer,jiffies + 20);
            fiq_bridge_pulse_trigger(&smc_fiq_bridge);
        }
        sc_int_reg->send_fifo_last_byte_int = 0;
        //wake_up_interruptible(&smc_wr_wq);
    }

    //should be update here
    sc_reg0 = SMC_READ_REG(REG0);
    smc_cardin = sc_reg0_reg->card_detect;
    //smc->cardin = sc_reg0_reg->card_detect;

    SMC_WRITE_REG(INTR, sc_int|0x3FF);

    return;
}
#else
static irqreturn_t smc_irq_handler(int irq, void *data)
{
	smc_dev_t *smc = (smc_dev_t*)data;
	unsigned int sc_status;
	unsigned int sc_reg0;
	unsigned int sc_int;
	SMC_STATUS_Reg_t *sc_status_reg = (SMC_STATUS_Reg_t*)&sc_status;
	SMC_INTERRUPT_Reg_t *sc_int_reg = (SMC_INTERRUPT_Reg_t*)&sc_int;
	SMCCARD_HW_Reg0_t *sc_reg0_reg = (void *)&sc_reg0;
	unsigned long flags;

	spin_lock_irqsave(&smc->slock, flags);

	sc_int = SMC_READ_REG(INTR);
	pr_dbg("smc intr:0x%x\n", sc_int);
#if 1
        if(sc_int_reg->recv_fifo_bytes_threshold_int) {
            if(smc->recv_count==RECV_BUF_SIZE) {
                pr_error("receive buffer overflow\n");
            } else {
                int total_data;
                sc_status = SMC_READ_REG(STATUS);
                total_data = sc_status_reg->recv_fifo_bytes_number ;
                while(total_data > 0){
                    int pos = smc->recv_start+smc->recv_count;

                    pos %= RECV_BUF_SIZE;
                    smc->recv_buf[pos] = SMC_READ_REG(FIFO);
                    smc->recv_count++;
                    total_data--;
                    sc_status = SMC_READ_REG(STATUS);
                    pr_dbg("irq: recv 1 byte=:%x.......\n", smc->recv_buf[pos]);
                }
            }
            sc_int_reg->recv_fifo_bytes_threshold_int = 0;

            wake_up_interruptible(&smc->rd_wq);
        }
#else
	if(sc_int_reg->recv_fifo_bytes_threshold_int) {

		if(smc->recv_count==RECV_BUF_SIZE) {
			pr_error("receive buffer overflow\n");
		} else {
			int pos = smc->recv_start+smc->recv_count;

			pos %= RECV_BUF_SIZE;
			smc->recv_buf[pos] = SMC_READ_REG(FIFO);
			smc->recv_count++;

			pr_dbg("irq: recv 1 byte [0x%x]\n", smc->recv_buf[pos]);
		}

		sc_int_reg->recv_fifo_bytes_threshold_int = 0;

		wake_up_interruptible(&smc->rd_wq);
	}
#endif
	if(sc_int_reg->send_fifo_last_byte_int) {
		int cnt = 0;

		while(1) {
			u8 byte;

			sc_status = SMC_READ_REG(STATUS);
			if (!smc->send_count || sc_status_reg->send_fifo_full_status) {
				break;
			}

			byte = smc->send_buf[smc->send_start++];
			SMC_WRITE_REG(FIFO, byte);
			smc->send_start %= SEND_BUF_SIZE;
			smc->send_count--;
			cnt++;
		}

		pr_dbg("irq: send %d bytes to hw\n", cnt);

		if(!smc->send_count) {
			sc_int_reg->send_fifo_last_byte_int_mask = 0;
			sc_int_reg->recv_fifo_bytes_threshold_int_mask = 1;
		}

		sc_int_reg->send_fifo_last_byte_int = 0;

		wake_up_interruptible(&smc->wr_wq);
	}

	sc_reg0 = SMC_READ_REG(REG0);
	smc->cardin = sc_reg0_reg->card_detect;

	if(smc_status != smc->cardin){
            //not to set here and let user API to call smc_hw_get_status to update the card status
            //printk(KERN_ALERT"smc_irq_handler card status changed from %d to %d.", smc_status, smc->cardin);
            //smc_status = smc->cardin;
       }

	SMC_WRITE_REG(INTR, sc_int|0x3FF);

	spin_unlock_irqrestore(&smc->slock, flags);
	return IRQ_HANDLED;
}
#endif

static void smc_dev_deinit(smc_dev_t *smc)
{
	struct devio_aml_platform_data *pd_smc;

#ifdef SMC_FIQ
        free_fiq(INT_SMART_CARD, &smc_irq_handler);
        del_timer(&smc_wakeup_wq_timer);
        unregister_fiq_bridge_handle(&smc_fiq_bridge);
#else
        if(smc->irq_num!=-1) {
            free_irq(smc->irq_num, &smc);
        }
#endif

	if(smc->reset_pin != -1) {
		amlogic_gpio_free(smc->reset_pin,VBUS_SMC_RESET_GPIO_OWNER);
	}

	if(smc->dev) {
		device_destroy(&smc_class, MKDEV(smc_major, smc->id));
	}

	mutex_destroy(&smc->lock);

	pd_smc =  (struct devio_aml_platform_data*)smc->pdev->dev.platform_data;
	if(pd_smc && pd_smc->io_cleanup)
		pd_smc->io_cleanup(NULL);

	smc->init = 0;

}

static int smc_dev_init(smc_dev_t *smc, int id)
{
	char buf[32];
	struct devio_aml_platform_data *pd_smc;
	const char *gpio_name = NULL;
	unsigned int smc_reset_pin = -1;
	int smc_reset_level = -1;
	int ret;

	smc->id = id;

	smc->pinctrl = devm_pinctrl_get_select_default(&smc->pdev->dev);

	smc->reset_pin = smc0_reset;
	if(smc->reset_pin==-1) {
		gpio_name = of_get_property(smc->pdev->dev.of_node, "smc_reset", NULL);
		if(gpio_name) {
			smc_reset_pin = amlogic_gpio_name_map_num(gpio_name);
			amlogic_gpio_request(smc_reset_pin,VBUS_SMC_RESET_GPIO_OWNER);
			smc->reset_pin = smc_reset_pin;
		} else {
			pr_error("don't find to match \"smc_reset\" \n");
		}
	}

	smc->reset_level = 1;
	if(1) {
		ret = of_property_read_u32(smc->pdev->dev.of_node, "smc_reset_level",&smc_reset_level);
		if(ret){
			pr_error("don't find to match \"smc_reset_level\"\n");
			return -1;
		}
		smc->reset_level = smc_reset_level;
	}

#ifdef SMC_FIQ
    //since we using register_fiq_bridge_handle to handle callback, reserved only
    init_timer(&smc_wakeup_wq_timer);
    smc_wakeup_wq_timer.function = smc_wakeup_wq_timer_fun;
    smc_wakeup_wq_timer.expires = jiffies + HZ;
    add_timer(&smc_wakeup_wq_timer);

    smc_reset_fiq_varible();
    {
        int r = -1;
        smc_fiq_bridge.handle = smc_bridge_isr;
        smc_fiq_bridge.key=(u32)smc_bridge_isr;
        smc_fiq_bridge.name="smc_bridge_isr";
        r = register_fiq_bridge_handle(&smc_fiq_bridge);
        if(r) {
            pr_error( "smc fiq bridge register error.\n");
            return -1;
        }
    }
    request_fiq(INT_SMART_CARD, &smc_irq_handler);
#else
        smc->irq_num = INT_SMART_CARD;
#endif

	init_waitqueue_head(&smc->rd_wq);
	init_waitqueue_head(&smc->wr_wq);

#ifdef SMC_FIQ
        smc_rd_wq = &smc->rd_wq;
        smc_wr_wq = &smc->wr_wq;
#endif

	spin_lock_init(&smc->slock);
	mutex_init(&smc->lock);

	pd_smc =  (struct devio_aml_platform_data*)smc->pdev->dev.platform_data;
	if(pd_smc) {
		if(pd_smc->io_setup)
			pd_smc->io_setup(NULL);

		smc->reset = pd_smc->io_reset;
	}

#ifdef SMC_FIQ
        request_fiq(INT_SMART_CARD, &smc_irq_handler);
#else
        smc->irq_num=request_irq(smc->irq_num,(irq_handler_t)smc_irq_handler,IRQF_SHARED,"smc",smc);
        if(smc->irq_num<0) {
            pr_error("request irq error!\n");
            smc_dev_deinit(smc);
            return -1;
        }
#endif

	snprintf(buf, sizeof(buf), "smc%d", smc->id);
	smc->dev=device_create(&smc_class, NULL, MKDEV(smc_major, smc->id), smc, buf);
	if(!smc->dev) {
		pr_error("create device error!\n");
		smc_dev_deinit(smc);
		return -1;
	}

	smc->param.f = F_DEFAULT;
	smc->param.d = D_DEFAULT;
	smc->param.n = N_DEFAULT;
	smc->param.bwi = BWI_DEFAULT;
	smc->param.cwi = CWI_DEFAULT;
	smc->param.bgt = BGT_DEFAULT;
	smc->param.freq = FREQ_DEFAULT;
	smc->param.recv_invert = 0;
	smc->param.recv_lsb_msb = 0;
	smc->param.recv_no_parity = 1;
	smc->param.xmit_invert = 0;
	smc->param.xmit_lsb_msb = 0;
	smc->param.xmit_retries = 1;
	smc->param.xmit_repeat_dis = 1;
	smc->init = 1;

	smc_hw_setup(smc);

	return 0;
}

static int smc_open(struct inode *inode, struct file *filp)
{
	int id = iminor(inode);
	smc_dev_t *smc = &smc_dev[id];

	mutex_lock(&smc->lock);

	if(smc->used) {
		mutex_unlock(&smc->lock);
		pr_error("smartcard %d already openned!", id);
		return -EBUSY;
	}

	smc->used = 1;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("smart_card", 1);
#endif

	mutex_unlock(&smc->lock);

	filp->private_data = smc;
	return 0;
}

static int smc_close(struct inode *inode, struct file *filp)
{
	smc_dev_t *smc = (smc_dev_t*)filp->private_data;

	mutex_lock(&smc->lock);
	smc_hw_deactive(smc);
	smc->used = 0;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name("smart_card", 0);
#endif

	mutex_unlock(&smc->lock);

	return 0;
}

#ifdef SMC_FIQ
static int smc_read(struct file *filp,char __user *buff, size_t size, loff_t *ppos)
{
    smc_dev_t *smc = (smc_dev_t*)filp->private_data;
    int ret;
    char temp_buf[512];
    unsigned long flags;

    ret = mutex_lock_interruptible(&smc->lock);
    if(ret) return ret;

    if(!smc->cardin) {
        mutex_unlock(&smc->lock);
        return -ENODEV;
    }

    //printk(KERN_ALERT"<<<<==== to read %d data  from smartcard and received:%d.\n", size, smc_recv_bak_idx);
    //if(!(filp->f_flags&O_NONBLOCK))
    {
        //ret = wait_event_interruptible(smc->rd_wq, smc_read_write_check(true, -1, -1));
    }
    //printk(KERN_ALERT"ret is:0x%x.\n", ret);

    if(ret ==0){
        spin_lock_irqsave(&smc->slock, flags);
        ret = smc_read_write_data(true, temp_buf, size);
        spin_unlock_irqrestore(&smc->slock, flags);
        if(ret == size){
            if(smc_debug_flag > 0){
                if(8==size)
                    printk(KERN_ALERT"smc_read......success data 7 is:%d.\n", temp_buf[7]);
                else
                    printk(KERN_ALERT"<<<<==== to read %d data  from smartcard and received:%d.\n", size, smc_recv_bak_idx);
                smc_print_data(temp_buf, size);
            }
            ret = copy_to_user(buff, temp_buf, size);
		if(ret<0) {
			printk("ret =%d \n",ret);
        		return ret;
    		}
        }else{
            //printk(KERN_ALERT"smc_read.......failed.\n");
            ret = 0;
        }
    }

    mutex_unlock(&smc->lock);
    //printk(KERN_ALERT"smc_read.......ret:%d..\n", ret);
    return ret;
}

static void smc_reset_recv_bak_data(void){
    int idx = 0;

    smc_recv_bak_idx = 0;
    for(idx = 0; idx < SMC_CNT; idx++)
        smc_recv_data_bak[idx] = 0x0;
}

static int smc_write(struct file *filp, const char __user *buff, size_t size, loff_t *offp)
{
    smc_dev_t *smc = (smc_dev_t*)filp->private_data;
    int ret;
    unsigned long sc_int;
    SMC_INTERRUPT_Reg_t *sc_int_reg = (void *)&sc_int;
    char temp_buf[512];
    unsigned long flags;

    ret = mutex_lock_interruptible(&smc->lock);
    if(ret) return ret;

    if(!smc->cardin) {
        mutex_unlock(&smc->lock);
        return -ENODEV;
    }

    //printk(KERN_ALERT"===>>>> to write %d byte data to smartcard. -->\n", size);
    //ret = wait_event_interruptible(smc->wr_wq, smc_read_write_check(false, -1, -1));
    //printk(KERN_ALERT"ret is:0x%x.\n", ret);

    if(ret == 0){
        ret = copy_from_user(temp_buf, buff, size);
	if(ret<0) {
		printk("ret =%d \n",ret);
        	return ret;
    	}

        spin_lock_irqsave(&smc->slock, flags);
        if(smc_read_write_check(true, smc_read_fiq_idx, -1)) ret = -1;
        else ret = smc_read_write_data(false, temp_buf, size);
        spin_unlock_irqrestore(&smc->slock, flags);

        smc_reset_recv_bak_data();

        if(ret == size){
            //smc_print_data(temp_buf, size);
            if(smc_debug_flag > 0){
                printk(KERN_ALERT"smc_write %d byte success.\n", size);
                smc_print_data(temp_buf, size);
            }
            smc_hw_start_send(smc);
            sc_int = SMC_READ_REG(INTR);
            sc_int_reg->send_fifo_last_byte_int_mask = 1;
            SMC_WRITE_REG(INTR, sc_int|0x3FF);
        }else{
            //printk(KERN_ALERT"smc_write.....failed.\n");
            ret = 0;
        }
    }
    mutex_unlock(&smc->lock);
    //printk(KERN_ALERT"smc_write.....ret:%d..\n", ret);

    return ret;
}
#else
static int smc_read(struct file *filp,char __user *buff, size_t size, loff_t *ppos)
{
	smc_dev_t *smc = (smc_dev_t*)filp->private_data;
	unsigned long flags;
	int pos = 0, ret;
	//unsigned long sc_int;
	//SMC_INTERRUPT_Reg_t *sc_int_reg = (void *)&sc_int;

	ret = mutex_lock_interruptible(&smc->lock);
	if(ret) return ret;

#ifdef DISABLE_RECV_INT
	pr_dbg("wait write end\n");
	if(!(filp->f_flags&O_NONBLOCK)) {
		ret = wait_event_interruptible(smc->wr_wq, smc_write_end(smc));
	}

	if(ret==0) {
		pr_dbg("wait read buffer\n");

		sc_int = SMC_READ_REG(INTR);
		sc_int_reg->recv_fifo_bytes_threshold_int_mask = 1;
		sc_int_reg->send_fifo_last_byte_int_mask = 0;
		SMC_WRITE_REG(INTR, sc_int|0x3FF);

		if(!(filp->f_flags&O_NONBLOCK)) {
			ret = wait_event_interruptible(smc->rd_wq, smc_can_read(smc));
		}
	}
#endif

	if(ret==0) {

		spin_lock_irqsave(&smc->slock, flags);

		if(!smc->cardin) {
			ret = -ENODEV;
		} else if (!smc->recv_count) {
			ret = -EAGAIN;
		} else {
			ret = smc->recv_count;
			if(ret>size) ret = size;

			pos = smc->recv_start;
			smc->recv_start += ret;
			smc->recv_count -= ret;
			smc->recv_start %= RECV_BUF_SIZE;
		}
		spin_unlock_irqrestore(&smc->slock, flags);
	}

	if(ret>0) {
		int cnt = RECV_BUF_SIZE-pos;
		pr_dbg("read %d bytes\n", ret);

		if(cnt>=ret) {
			ret = copy_to_user(buff, smc->recv_buf+pos, ret);
			if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
		} else {
			int cnt1 = ret-cnt;
			ret = copy_to_user(buff, smc->recv_buf+pos, cnt);
			if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
			ret = copy_to_user(buff+cnt, smc->recv_buf, cnt1);
			if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
		}
	}

	mutex_unlock(&smc->lock);

	return ret;
}

static int smc_write(struct file *filp, const char __user *buff, size_t size, loff_t *offp)
{
	smc_dev_t *smc = (smc_dev_t*)filp->private_data;
	unsigned long flags;
	int pos = 0, ret;
	unsigned long sc_int;
	SMC_INTERRUPT_Reg_t *sc_int_reg = (void *)&sc_int;

	ret = mutex_lock_interruptible(&smc->lock);
	if(ret) return ret;

	pr_dbg("wait write buffer\n");

	if(!(filp->f_flags&O_NONBLOCK)) {
		ret = wait_event_interruptible(smc->wr_wq, smc_can_write(smc));
	}

	if(ret==0) {
		spin_lock_irqsave(&smc->slock, flags);

		if(!smc->cardin) {
			ret = -ENODEV;
		} else if (smc->send_count==SEND_BUF_SIZE) {
			ret = -EAGAIN;
		} else {
			ret = SEND_BUF_SIZE-smc->send_count;
			if(ret>size) ret = size;

			pos = smc->send_start+smc->send_count;
			pos %= SEND_BUF_SIZE;
			smc->send_count += ret;
		}

		spin_unlock_irqrestore(&smc->slock, flags);
	}

	if(ret>0) {
		int cnt = SEND_BUF_SIZE-pos;

		if(cnt>=ret) {
			ret = copy_from_user(smc->send_buf+pos, buff, ret);
			 if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
		} else {
			int cnt1 = ret-cnt;
			ret = copy_from_user(smc->send_buf+pos, buff, cnt);
			 if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
			ret = copy_from_user(smc->send_buf, buff+cnt, cnt1);
			 if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
		}

		sc_int = SMC_READ_REG(INTR);
#ifdef DISABLE_RECV_INT
		sc_int_reg->recv_fifo_bytes_threshold_int_mask = 0;
#endif
		sc_int_reg->send_fifo_last_byte_int_mask = 1;
		SMC_WRITE_REG(INTR, sc_int|0x3FF);

		pr_dbg("write %d bytes\n", ret);

		smc_hw_start_send(smc);
	}


	mutex_unlock(&smc->lock);

	return ret;
}
#endif

static long smc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	smc_dev_t *smc = (smc_dev_t*)file->private_data;
	int ret = 0;

	switch(cmd) {
		case AMSMC_IOC_RESET:
		{
			ret = mutex_lock_interruptible(&smc->lock);
			if(ret) return ret;

			ret = smc_hw_reset(smc);
			if(ret>=0) {
				ret = copy_to_user((void*)arg, &smc->atr, sizeof(struct am_smc_atr));
				if(ret<0) {
					printk("ret =%d \n",ret);
        				return ret;
    				}
			}

			mutex_unlock(&smc->lock);
		}
		break;
		case AMSMC_IOC_GET_STATUS:
		{
			int status;
			ret = smc_hw_get_status(smc, &status);
			if(ret>=0) {
				ret = copy_to_user((void*)arg, &status, sizeof(int));
				if(ret<0) {
					printk("ret =%d \n",ret);
        				return ret;
    				}				
			}
		}
		break;
		case AMSMC_IOC_ACTIVE:
		{
			ret = mutex_lock_interruptible(&smc->lock);
			if(ret) return ret;

			ret = smc_hw_active(smc);

			mutex_unlock(&smc->lock);
		}
		break;
		case AMSMC_IOC_DEACTIVE:
		{
			ret = mutex_lock_interruptible(&smc->lock);
			if(ret) return ret;

			ret = smc_hw_deactive(smc);

			mutex_unlock(&smc->lock);
		}
		break;
		case AMSMC_IOC_GET_PARAM:
		{
			ret = mutex_lock_interruptible(&smc->lock);
			if(ret) return ret;

			ret = copy_to_user((void*)arg, &smc->param, sizeof(struct am_smc_param));
			if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}
			mutex_unlock(&smc->lock);
		}
		break;
		case AMSMC_IOC_SET_PARAM:
		{
			ret = mutex_lock_interruptible(&smc->lock);
			if(ret) return ret;

			ret = copy_from_user(&smc->param, (void*)arg, sizeof(struct am_smc_param));
			if(ret<0) {
				printk("ret =%d \n",ret);
        			return ret;
    			}			
			ret = smc_hw_set_param(smc);

			mutex_unlock(&smc->lock);
		}
		break;
		default:
			ret = -EINVAL;
		break;
	}

	return ret;
}

static unsigned int smc_poll(struct file *filp, struct poll_table_struct *wait)
{
    smc_dev_t *smc = (smc_dev_t*)filp->private_data;
    unsigned int ret = 0;
    unsigned long flags;

#ifdef SMC_FIQ
    bool can_read = false, can_write = false;
    int temp_r_idx = smc_read_fiq_idx, temp_w_idx = smc_write_fiq_idx;
    //printk(KERN_ALERT"11 going to poll and can_read:%d, can write:%d.\n", can_read, can_write);

    poll_wait(filp, &smc->rd_wq, wait);
    poll_wait(filp, &smc->wr_wq, wait);

    can_read  = smc_read_write_check(true, temp_r_idx, temp_w_idx);
    can_write = smc_read_write_check(false, temp_r_idx, temp_w_idx);

    //printk(KERN_ALERT"22 going to poll and can_read:%d, can write:%d.\n", can_read, can_write);

    //if(can_read) wake_up_interruptible(&smc->rd_wq);
    //if(can_write) wake_up_interruptible(&smc->wr_wq);

#else
    poll_wait(filp, &smc->rd_wq, wait);
    poll_wait(filp, &smc->wr_wq, wait);
#endif

    spin_lock_irqsave(&smc->slock, flags);

#ifdef SMC_FIQ
    if(can_read) ret |= POLLIN|POLLRDNORM; //0x41
    if(can_write) ret |= POLLOUT|POLLWRNORM; //0x104
    if(!smc_cardin) ret |= POLLERR;
    smc->cardin = smc_cardin;//may be not a good idea to update here
    //printk(KERN_ALERT"smc poll the ret is:0x%x,r/fiq_r:%d/%d, w/fiq_w:%d/%d.\n", ret,  smc_read_idx,temp_r_idx,smc_write_idx, temp_w_idx);
#else
    if(smc->recv_count) ret |= POLLIN|POLLRDNORM;
    if(smc->send_count!=SEND_BUF_SIZE) ret |= POLLOUT|POLLWRNORM;
    if(!smc->cardin) ret |= POLLERR;
#endif
    spin_unlock_irqrestore(&smc->slock, flags);

    return ret;
}

static struct file_operations smc_fops =
{
	.owner = THIS_MODULE,
	.open  = smc_open,
	.write = smc_write,
	.read  = smc_read,
	.release = smc_close,
	.unlocked_ioctl = smc_ioctl,
	.poll  = smc_poll
};

static int smc_probe(struct platform_device *pdev)
{
	smc_dev_t *smc = NULL;
	int i, ret;

	mutex_lock(&smc_lock);

	for (i=0; i<SMC_DEV_COUNT; i++) {
		if (!smc_dev[i].init) {
			smc = &smc_dev[i];
			break;
		}
	}

	if(smc) {
		smc->init = 1;
		smc->pdev = pdev;
		dev_set_drvdata(&pdev->dev, smc);

		if ((ret=smc_dev_init(smc, i))<0) {
			smc = NULL;
		}
	}

	mutex_unlock(&smc_lock);

	return smc ? 0 : -1;
}

static int smc_remove(struct platform_device *pdev)
{
	smc_dev_t *smc = (smc_dev_t*)dev_get_drvdata(&pdev->dev);

	mutex_lock(&smc_lock);

	smc_dev_deinit(smc);

	if(smc->pinctrl)
		devm_pinctrl_put(smc->pinctrl);

	mutex_unlock(&smc_lock);

	return 0;
}

static int smc_suspend(struct platform_device *dev, pm_message_t state)
{
    unsigned long flags;
    smc_dev_t *smc = (smc_dev_t*)dev_get_drvdata(&dev->dev);

    spin_lock_irqsave(&smc->slock, flags);
    //printk(KERN_ALERT"The smc_suspend be called and card status is:%d.\n", smc_status);
    //smc_status = SUSPEND_CARD_STATUS; //no use to set here
    spin_unlock_irqrestore(&smc->slock, flags);

    return 0;
}

static int smc_resume(struct platform_device *dev)
{
    unsigned long flags;
    smc_dev_t *smc = (smc_dev_t*)dev_get_drvdata(&dev->dev);

    spin_lock_irqsave(&smc->slock, flags);
    //smc_status = SUSPEND_CARD_STATUS;
    //printk(KERN_ALERT"The smc_resume be called and card status is:%d.\n", smc_status);
    spin_unlock_irqrestore(&smc->slock, flags);

    return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id meson6tv_smc_dt_match[]={
	{	.compatible = "amlogic,smartcard",
	},
	{},
};
#else
#define meson6tv_smc_dt_match NULL
#endif

static struct platform_driver smc_driver = {
        .resume = smc_resume,
        .suspend = smc_suspend,
        .probe		= smc_probe,
        .remove		= smc_remove,
        .driver		= {
	        .name	= "amlogic-smc",
	        .owner	= THIS_MODULE,
		.of_match_table = meson6tv_smc_dt_match,
        }
};

static int __init smc_mod_init(void)
{
	int ret = -1;

	mutex_init(&smc_lock);

	smc_major = register_chrdev(0, SMC_DEV_NAME, &smc_fops);
	if(smc_major<=0) {
		mutex_destroy(&smc_lock);
		pr_error("register chrdev error\n");
		goto error_register_chrdev;
	}

	if(class_register(&smc_class)<0) {
		pr_error("register class error\n");
		goto error_class_register;
	}

	if(platform_driver_register(&smc_driver)<0) {
		pr_error("register platform driver error\n");
		goto error_platform_drv_register;
	}

	return 0;
error_platform_drv_register:
	class_unregister(&smc_class);
error_class_register:
	unregister_chrdev(smc_major, SMC_DEV_NAME);
error_register_chrdev:
	mutex_destroy(&smc_lock);
	return ret;
}

static void __exit smc_mod_exit(void)
{
	platform_driver_unregister(&smc_driver);
	class_unregister(&smc_class);
	unregister_chrdev(smc_major, SMC_DEV_NAME);
	mutex_destroy(&smc_lock);
}

module_init(smc_mod_init);

module_exit(smc_mod_exit);

MODULE_AUTHOR("AMLOGIC");

MODULE_DESCRIPTION("AMLOGIC smart card driver");

MODULE_LICENSE("GPL");

