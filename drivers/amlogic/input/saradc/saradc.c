#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/amlogic/saradc.h>
#include <linux/delay.h>
#include "saradc_reg.h"
#ifdef CONFIG_MESON_CPU_TEMP_SENSOR
#include <mach/cpu.h>
#endif

#define ENABLE_CALIBRATION
#ifndef CONFIG_OF
#define CONFIG_OF
#endif
struct saradc {
	spinlock_t lock;
#ifdef ENABLE_CALIBRATION
	int ref_val;
	int ref_nominal;
	int coef;
#endif

};

static struct saradc *gp_saradc;

#define CHAN_XP	CHAN_0
#define CHAN_YP	CHAN_1
#define CHAN_XN	CHAN_2
#define CHAN_YN	CHAN_3

#define INTERNAL_CAL_NUM	5

static u8 chan_mux[SARADC_CHAN_NUM] = {0,1,2,3,4,5,6,7};


static void saradc_reset(void)
{
	int i;

	//set adc clock as 1.28Mhz
	set_clock_divider(20);
	enable_clock();
	enable_adc();
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	enable_bandgap();
#endif
	set_sample_mode(DIFF_MODE);
	set_tempsen(0);
	disable_fifo_irq();
	disable_continuous_sample();
	disable_chan0_delta();
	disable_chan1_delta();

	set_input_delay(10, INPUT_DELAY_TB_1US);
	set_sample_delay(10, SAMPLE_DELAY_TB_1US);
	set_block_delay(10, BLOCK_DELAY_TB_1US);
	
	// channels sampling mode setting
	for(i=0; i<SARADC_CHAN_NUM; i++) {
		set_sample_sw(i, IDLE_SW);
		set_sample_mux(i, chan_mux[i]);
	}
	
	// idle mode setting
	set_idle_sw(IDLE_SW);
	set_idle_mux(chan_mux[CHAN_0]);
	
	// detect mode setting
	set_detect_sw(DETECT_SW);
	set_detect_mux(chan_mux[CHAN_0]);
	disable_detect_sw();
	disable_detect_pullup();
	set_detect_irq_pol(0);
	disable_detect_irq();

//	set_sc_phase();
	enable_sample_engine();

//	printk("ADCREG reg0 =%x\n", get_reg(SAR_ADC_REG0));
//	printk("ADCREG ch list =%x\n", get_reg(SAR_ADC_CHAN_LIST));
//	printk("ADCREG avg =%x\n", get_reg(SAR_ADC_AVG_CNTL));
//	printk("ADCREG reg3=%x\n", get_reg(SAR_ADC_REG3));
//	printk("ADCREG ch72 sw=%x\n", get_reg(SAR_ADC_AUX_SW));
//	printk("ADCREG ch10 sw=%x\n", get_reg(SAR_ADC_CHAN_10_SW));
//	printk("ADCREG detect&idle=%x\n", get_reg(SAR_ADC_DETECT_IDLE_SW));
}

#ifdef ENABLE_CALIBRATION
static int  saradc_internal_cal(struct saradc *saradc)
{
	int i;
	int voltage[] = {CAL_VOLTAGE_1, CAL_VOLTAGE_2, CAL_VOLTAGE_3, CAL_VOLTAGE_4, CAL_VOLTAGE_5};
	int nominal[INTERNAL_CAL_NUM] = {0, 256, 512, 768, 1023};
	int val[INTERNAL_CAL_NUM];
	
//	set_cal_mux(MUX_CAL);
//	enable_cal_res_array();	
	for (i=0; i<INTERNAL_CAL_NUM; i++) {
		set_cal_voltage(voltage[i]);
		msleep(20);
		val[i] = get_adc_sample(CHAN_7);
		if (val[i] < 0) {
			return -1;
		}
	}
	saradc->ref_val = val[2];	
	saradc->ref_nominal = nominal[2];
	saradc->coef = (nominal[3] - nominal[1]) << 12;
	saradc->coef /= val[3] - val[1];
	printk("saradc calibration: ref_val = %d\n", saradc->ref_val);
	printk("saradc calibration: ref_nominal = %d\n", saradc->ref_nominal);
	printk("saradc calibration: coef = %d\n", saradc->coef);

	return 0;
}

static int saradc_get_cal_value(struct saradc *saradc, int val)
{
  int nominal;
/*  
  ((nominal - ref_nominal) << 10) / (val - ref_val) = coef
  ==> nominal = ((val - ref_val) * coef >> 10) + ref_nominal
*/
  nominal = val;
  if ((saradc->coef > 0) && ( val > 0)) {
    nominal = (val - saradc->ref_val) * saradc->coef;
    nominal >>= 12;
    nominal += saradc->ref_nominal;
  }
  if (nominal < 0) nominal = 0;
  if (nominal > 1023) nominal = 1023;
 	return nominal;
}
#endif

static u8 print_flag = 0; //(1<<CHAN_4)
#ifdef CONFIG_AMLOGIC_THERMAL
void temp_sensor_adc_init(int triming)
{
	select_temp();
	set_trimming(triming&0xf);
	if(!IS_MESON_M8_CPU)
		set_trimming1(triming>>4);
	enable_temp();
	enable_temp__();
}
#endif
int get_adc_sample(int chan)
{
	int count;
	int value=-1;
	int sum;
	unsigned long flags;
	if (!gp_saradc)
		return -1;
	
	spin_lock_irqsave(&gp_saradc->lock,flags);

	set_chan_list(chan, 1);
	set_avg_mode(chan, NO_AVG_MODE, SAMPLE_NUM_8);
	set_sample_mux(chan, chan_mux[chan]);
	set_detect_mux(chan_mux[chan]);
	set_idle_mux(chan_mux[chan]); // for revb
	enable_sample_engine();
	start_sample();

	// Read any CBUS register to delay one clock cycle after starting the sampling engine
	// The bus is really fast and we may miss that it started
	{ count = get_reg(ISA_TIMERE); }

	count = 0;
	while (delta_busy() || sample_busy() || avg_busy()) {
		if (++count > 10000) {
			printk(KERN_ERR "ADC busy error=%x.\n", READ_CBUS_REG(SAR_ADC_REG0));
			goto end;
		}
	}
    stop_sample();
    
    sum = 0;
    count = 0;
    value = get_fifo_sample();
	while (get_fifo_cnt()) {
        value = get_fifo_sample() & 0x3ff;
        //if ((value != 0x1fe) && (value != 0x1ff)) {
			sum += value & 0x3ff;
            count++;
        //}
	}
	value = (count) ? (sum / count) : (-1);

end:
	if ((print_flag>>chan)&1) {
		printk("before cal: ch%d = %d\n", chan, value);
	}
#ifdef ENABLE_CALIBRATION
  value = saradc_get_cal_value(gp_saradc, value);
  if ((print_flag>>chan)&1) {
			printk("after cal: ch%d = %d\n\n", chan, value);
	}
#endif
	disable_sample_engine();
//	set_sc_phase();
	spin_unlock_irqrestore(&gp_saradc->lock,flags);
	return value;
}

int saradc_ts_service(int cmd)
{
	int value = -1;
	
	switch (cmd) {
	case CMD_GET_X:
		//set_sample_sw(CHAN_YP, X_SW);
		value = get_adc_sample(CHAN_YP);
		set_sample_sw(CHAN_XP, Y_SW); // preset for y
		break;

	case CMD_GET_Y:
		//set_sample_sw(CHAN_XP, Y_SW);
		value = get_adc_sample(CHAN_XP);
		break;

	case CMD_GET_Z1:
		set_sample_sw(CHAN_XP, Z1_SW);
		value = get_adc_sample(CHAN_XP);
		break;

	case CMD_GET_Z2:
		set_sample_sw(CHAN_YN, Z2_SW);
		value = get_adc_sample(CHAN_YN);
		break;

	case CMD_GET_PENDOWN:
		value = !detect_level();
		set_sample_sw(CHAN_YP, X_SW); // preset for x
		break;
	
	case CMD_INIT_PENIRQ:
		enable_detect_pullup();
		enable_detect_sw();
		value = 0;
		printk(KERN_INFO "init penirq ok\n");
		break;

	case CMD_SET_PENIRQ:
		enable_detect_pullup();
		enable_detect_sw();
		value = 0;
		break;
		
	case CMD_CLEAR_PENIRQ:
		disable_detect_pullup();
		disable_detect_sw();
		value = 0;
		break;

	default:
		break;		
	}
	
	return value;
}

static ssize_t saradc_ch0_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(0));
}
static ssize_t saradc_ch1_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(1));
}
static ssize_t saradc_ch2_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(2));
}
static ssize_t saradc_ch3_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(3));
}
static ssize_t saradc_ch4_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(4));
}
static ssize_t saradc_ch5_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(5));
}
static ssize_t saradc_ch6_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(6));
}
static ssize_t saradc_ch7_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(7));
}
static ssize_t saradc_print_flag_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
		sscanf(buf, "%x", (int*)&print_flag);
    printk("print_flag=%d\n", print_flag);
    return count;
}
#ifndef CONFIG_MESON_CPU_TEMP_SENSOR
static ssize_t saradc_temperature_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
		u8 tempsen;
		sscanf(buf, "%d", (int*)&tempsen);
		if (tempsen) {
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
      select_temp();
      set_trimming((tempsen-1)&0xf);
      enable_temp();
      enable_temp__();
#else
    	temp_sens_sel(1);
    	set_tempsen(2);
#endif
    	printk("enter temperature mode(trimming=%d),please get the value from chan6\n",(tempsen-1)&0xf);
		}
		else {
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
      disable_temp__();
      disable_temp();
      unselect_temp();
#else
     	temp_sens_sel(0);
   		set_tempsen(0);
#endif
    	printk("exit temperature mode\n");
  	}
    return count;
}


#else

static int get_celius(void)
{
    int x,y;
    unsigned div=100000;
    /**
     * .x=0.304991,.y=-87.883549
.x=0.304991,.y=-87.578558
.x=0.305556,.y=-87.055556
.x=0.230769,.y=-87.769231
.x=0.230769,.y=-87.538462
.x=0.231092,.y=-86.911765
.x=0.288967,.y=-99.527145
.x=0.288967,.y=-99.238179
.x=0.289982,.y=-98.866432
     *
     */
    ///@todo fix it later
    if(aml_read_reg32(P_SAR_ADC_REG3)&(1<<29))
    {
        x=23077;
        y=-88;

    }else{
        x=28897;
        y=-100;

    }
    return (int)((get_adc_sample(6))*x/div + y);
}
static unsigned get_saradc_ch6(void)
{
    int val=get_adc_sample(6);
    return  (unsigned)(val<0?0:val);
}
static ssize_t temperature_raw_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_adc_sample(6));
}
static ssize_t temperature_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", get_cpu_temperature());
}
static ssize_t temperature_mode_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", aml_read_reg32(P_SAR_ADC_REG3)&(1<<29)?1:0);
}
static ssize_t temperature_mode_store(struct class *cla, struct class_attribute *attr, const char *buf, ssize_t count)
{
    u8 tempsen;
    sscanf(buf, "%x", (int*)&tempsen);
    if (tempsen==0) {
        set_tempsen(0);
    }
    else if(tempsen==1) {
        set_tempsen(2);
    }else{
        printk("only support 1 or 0\n");
    }
    return count;
}
#endif
static struct class_attribute saradc_class_attrs[] = {
    __ATTR_RO(saradc_ch0),
    __ATTR_RO(saradc_ch1),
    __ATTR_RO(saradc_ch2),
    __ATTR_RO(saradc_ch3),
    __ATTR_RO(saradc_ch4),
    __ATTR_RO(saradc_ch5),
#ifndef CONFIG_MESON_CPU_TEMP_SENSOR
    __ATTR_RO(saradc_ch6),
    __ATTR(saradc_temperature, S_IRUGO | S_IWUSR, NULL, saradc_temperature_store),
#else
    __ATTR_RO(temperature_raw),
    __ATTR_RO(temperature),
    __ATTR(temperature_mode, S_IRUGO | S_IWUSR, temperature_mode_show, temperature_mode_store),
#endif
    __ATTR_RO(saradc_ch7),    
    __ATTR(saradc_print_flag, S_IRUGO | S_IWUSR,NULL, saradc_print_flag_store),
    __ATTR_NULL
};
static struct class saradc_class = {
    .name = "saradc",
    .class_attrs = saradc_class_attrs,
};

static int saradc_probe(struct platform_device *pdev)
{
	int err;
	struct saradc *saradc;

	printk("__%s__\n",__func__);
	saradc = kzalloc(sizeof(struct saradc), GFP_KERNEL);
	if (!saradc) {
		err = -ENOMEM;
		goto err_free_mem;
	}
	saradc_reset();
	gp_saradc = saradc;
#ifdef ENABLE_CALIBRATION
	saradc->coef = 0;
  saradc_internal_cal(saradc);
#endif
	set_cal_voltage(7);
	spin_lock_init(&saradc->lock);	
#ifdef	CONFIG_MESON_CPU_TEMP_SENSOR
	temp_sens_sel(1);
	get_cpu_temperature_celius=get_celius;
#endif
	return 0;

err_free_mem:
	kfree(saradc);
	printk(KERN_INFO "saradc probe error\n");	
	return err;
}

static int saradc_suspend(struct platform_device *pdev,pm_message_t state)
{
	printk("%s: disable SARADC\n", __func__);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	disable_bandgap();
#endif
	disable_adc();
	disable_clock();
	return 0;
}

static int saradc_resume(struct platform_device *pdev)
{
	printk("%s: enable SARADC\n", __func__);
	enable_clock();
	enable_adc();
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	enable_bandgap();
#endif
	return 0;
}

static int __exit saradc_remove(struct platform_device *pdev)
{
	struct saradc *saradc = platform_get_drvdata(pdev);
	disable_adc();
	disable_sample_engine();
	gp_saradc = 0;
	kfree(saradc);
	return 0;
}
#ifdef CONFIG_OF
static const struct of_device_id saradc_dt_match[]={
	{	.compatible = "amlogic,saradc",
	},
	{},
};
#else
#define saradc_dt_match NULL
#endif
static struct platform_driver saradc_driver = {
	.probe      = saradc_probe,
	.remove     = saradc_remove,
	.suspend    = saradc_suspend,
	.resume     = saradc_resume,
	.driver     = {
		.name   = "saradc",
		.of_match_table = saradc_dt_match,
	},
};

static int __init saradc_init(void)
{
	printk(KERN_INFO "SARADC Driver init.\n");
	class_register(&saradc_class);
	return platform_driver_register(&saradc_driver);
}

static void __exit saradc_exit(void)
{
	printk(KERN_INFO "SARADC Driver exit.\n");
	platform_driver_unregister(&saradc_driver);
	class_unregister(&saradc_class);
}

module_init(saradc_init);
module_exit(saradc_exit);

MODULE_AUTHOR("aml");
MODULE_DESCRIPTION("SARADC Driver");
MODULE_LICENSE("GPL");
