#include <mach/power_gate.h>
#include <mach/mod_gate.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <mach/am_regs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hardirq.h>

short GCLK_ref[GCLK_IDX_MAX];
EXPORT_SYMBOL(GCLK_ref);

//#define PRINT_DEBUG_INFO
#ifdef PRINT_DEBUG_INFO
#define PRINT_INFO(...)		printk(__VA_ARGS__)
#else
#define PRINT_INFO(...)	
#endif

typedef struct{
	const char* name;
	const mod_type_t type;
	int ref;
	int flag;
	int dc_en;
	int no_share;
}mod_record_t;

DEFINE_SPINLOCK(gate_lock);

static mod_record_t mod_records[MOD_MAX_NUM + 1] = {
	{
		.name = "vdec",
		.type = MOD_VDEC,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "audio",
		.type = MOD_AUDIO,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "hdmi",
		.type = MOD_HDMI,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "venc",
		.type = MOD_VENC,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "tcon",
		.type = MOD_TCON,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
		.no_share = 1,
	},{
		.name = "lcd",
		.type = MOD_LCD,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
		.no_share = 1,
	},{
		.name = "spi",
		.type = MOD_SPI,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "uart0",
		.type = MOD_UART0,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "uart1",
		.type = MOD_UART1,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "uart2",
		.type = MOD_UART2,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "sana",
		.type = MOD_SANA,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "rom",
		.type = MOD_ROM,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "efuse",
		.type = MOD_EFUSE,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "random_num_gen",
		.type = MOD_RANDOM_NUM_GEN,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "ethernet",
		.type = MOD_ETHERNET,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "media_cpu",
		.type = MOD_MEDIA_CPU,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "ge2d",
		.type = MOD_GE2D,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "ahb",
		.type = MOD_AHB,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "demux",
		.type = MOD_DEMUX,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "smart_card",
		.type = MOD_SMART_CARD,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "sdhc",
		.type = MOD_SDHC,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "stream",
		.type = MOD_STREAM,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "blk_mov",
		.type = MOD_BLK_MOV,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "dvin",
		.type = MOD_MISC_DVIN,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "usb0",
		.type = MOD_USB0,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "usb1",
		.type = MOD_USB1,
		.ref = 0,
		.flag = 1,
		.dc_en = 0,
	},{
		.name = "sdio",
		.type = MOD_SDIO,
		.ref = 0,
		.flag = 1,
		.dc_en = 1,
	},{
		.name = NULL,
		.type = -1,
		.ref = -1,
		.flag = -1,
		.dc_en = -1,
	}, //end of the record array
};


static int _switch_gate(mod_type_t type, int flag)
{
	int ret = 0;
	switch(type) {
	case MOD_VDEC:
		PRINT_INFO("turn %s vdec module\n", flag?"on":"off");
		if (flag) {			   
			//__CLK_GATE_ON(DOS);
			//aml_set_reg32_mask(P_HHI_VDEC_CLK_CNTL, 1 << 8);
		} else {
			//__CLK_GATE_OFF(DOS);
			//aml_clr_reg32_mask(P_HHI_VDEC_CLK_CNTL, 1 << 8);
		}
		break;
	case MOD_AUDIO:
		PRINT_INFO("turn %s audio module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(AIU_AI_TOP_GLUE);
			__CLK_GATE_ON(AIU_IEC958);
			__CLK_GATE_ON(AIU_I2S_OUT);
			__CLK_GATE_ON(AIU_AMCLK_MEASURE);
			__CLK_GATE_ON(AIU_AIFIFO2);
			__CLK_GATE_ON(AIU_AUD_MIXER);
			__CLK_GATE_ON(AIU_MIXER_REG);
			__CLK_GATE_ON(AIU_ADC);
			__CLK_GATE_ON(AIU_TOP_LEVEL);
			//__CLK_GATE_ON(AIU_PCLK);
			__CLK_GATE_ON(AIU_AOCLK);
			__CLK_GATE_ON(AIU_ICE958_AMCLK);
		} else { 
			__CLK_GATE_OFF(AIU_AI_TOP_GLUE);
			__CLK_GATE_OFF(AIU_IEC958);
			__CLK_GATE_OFF(AIU_I2S_OUT);
			__CLK_GATE_OFF(AIU_AMCLK_MEASURE);
			__CLK_GATE_OFF(AIU_AIFIFO2);
			__CLK_GATE_OFF(AIU_AUD_MIXER);
			__CLK_GATE_OFF(AIU_MIXER_REG);
			__CLK_GATE_OFF(AIU_ADC);
			__CLK_GATE_OFF(AIU_TOP_LEVEL);
			//__CLK_GATE_OFF(AIU_PCLK);
			__CLK_GATE_OFF(AIU_AOCLK);
			__CLK_GATE_OFF(AIU_ICE958_AMCLK);
	  
		}
		break;
	#if 0
	case MOD_HDMI:
		PRINT_INFO("turn %s hdmi module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(HDMI_INTR_SYNC);
			//__CLK_GATE_ON(HDMI_RX);
			__CLK_GATE_ON(HDMI_PCLK);
		} else {
			__CLK_GATE_OFF(HDMI_INTR_SYNC);
			//__CLK_GATE_OFF(HDMI_RX);
			__CLK_GATE_OFF(HDMI_PCLK);
		}			
		break;
	case MOD_VENC:
		PRINT_INFO("turn %s venc module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(VCLK2_VENCI);
			__CLK_GATE_ON(VCLK2_VENCI1);
			__CLK_GATE_ON(VCLK2_VENCP);
			__CLK_GATE_ON(VCLK2_VENCP1);
			__CLK_GATE_ON(VCLK2_ENCI);
			__CLK_GATE_ON(VCLK2_ENCP);
			__CLK_GATE_ON(VCLK2_VENCT);
			__CLK_GATE_ON(VCLK2_VENCT1);
			__CLK_GATE_ON(VCLK2_OTHER);
			__CLK_GATE_ON(VCLK2_OTHER1);
			__CLK_GATE_ON(ENC480P);
			//__CLK_GATE_ON(VENC_DAC);
			__CLK_GATE_ON(DAC_CLK);
		} else {
			__CLK_GATE_OFF(VCLK2_VENCI);
			__CLK_GATE_OFF(VCLK2_VENCI1);
			__CLK_GATE_OFF(VCLK2_VENCP);
		#ifndef CONFIG_MACH_MESON6_G02_DONGLE
			__CLK_GATE_OFF(VCLK2_VENCP1);
		#endif	 
		
			__CLK_GATE_OFF(VCLK2_ENCI);
		#ifndef CONFIG_MACH_MESON6_G02_DONGLE	  
			__CLK_GATE_OFF(VCLK2_ENCP);
		#endif
			__CLK_GATE_OFF(VCLK2_VENCT);
			__CLK_GATE_OFF(VCLK2_VENCT1);
			__CLK_GATE_OFF(VCLK2_OTHER);
			__CLK_GATE_OFF(VCLK2_OTHER1);
			__CLK_GATE_OFF(ENC480P);
			__CLK_GATE_OFF(DAC_CLK);
		}
		break;
	case MOD_TCON:
		//PRINT_INFO("turn %s tcon module\n", flag?"on":"off");
		/*if (flag) {
			//__CLK_GATE_ON(VCLK2_ENCT);
		} else {
			//__CLK_GATE_OFF(VCLK2_ENCT);
		}*/
		break;
	case MOD_LCD:
		PRINT_INFO("turn %s lcd module\n", flag?"on":"off");
		if (flag) {
			//__CLK_GATE_ON(VCLK2_ENCL);
			__CLK_GATE_ON(VCLK2_VENCL);
			__CLK_GATE_ON(EDP_CLK);
		} else {
			__CLK_GATE_OFF(EDP_CLK);
			__CLK_GATE_OFF(VCLK2_VENCL);
			//__CLK_GATE_OFF(VCLK2_ENCL);
		}
		break;
	#endif
	case MOD_SPI:
		PRINT_INFO("turn %s spi module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(SPICC);
			__CLK_GATE_ON(SPI);
		} else {
			__CLK_GATE_OFF(SPICC);
			__CLK_GATE_OFF(SPI);
		}
		break;
	case MOD_UART0:
		PRINT_INFO("turn %s uart0 module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(UART0);
		} else {
			__CLK_GATE_OFF(UART0);
		}
		break;
	case MOD_UART1:
		PRINT_INFO("turn %s uart1 module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(UART1);
		} else {
			__CLK_GATE_OFF(UART1);
		}
		break;
	case MOD_UART2:
		PRINT_INFO("turn %s uart2 module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(UART2);
		} else {
			__CLK_GATE_OFF(UART2);
		}
		break;
	case MOD_SANA:
		PRINT_INFO("turn %s sana module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(SANA);
		} else {
			__CLK_GATE_OFF(SANA);
		}
		break;
	case MOD_ROM:
		PRINT_INFO("turn %s rom module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(ROM_CLK);
		} else {
			__CLK_GATE_OFF(ROM_CLK);
		}
		break;
	case MOD_EFUSE:
		PRINT_INFO("turn %s efuse module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(EFUSE);
		} else {
			__CLK_GATE_OFF(EFUSE);
		}
		break;
	case MOD_RANDOM_NUM_GEN:
		PRINT_INFO("turn %s random_num_gen module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(RANDOM_NUM_GEN);
		} else {
			__CLK_GATE_OFF(RANDOM_NUM_GEN);
		}
		break;
	case MOD_ETHERNET:
		PRINT_INFO("turn %s ethernet module\n", flag?"on":"off");
		if (flag) {
			__CLK_GATE_ON(ETHERNET);
		} else {
			__CLK_GATE_OFF(ETHERNET);
		}
		break;
	case MOD_MEDIA_CPU:
		PRINT_INFO("trun %s Audio DSP\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(MEDIA_CPU);
		}else{
			 __CLK_GATE_OFF(MEDIA_CPU);
		}
		break;
	case MOD_GE2D:
		PRINT_INFO("trun %s GE2D\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(GE2D);
		}else{
			__CLK_GATE_OFF(GE2D);
		}
		break;
	case MOD_AHB:
		PRINT_INFO("trun %s ahb\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(AHB_ARB0);
			__CLK_GATE_ON(AHB_BRIDGE);
			__CLK_GATE_ON(AHB_DATA_BUS);
			__CLK_GATE_ON(AHB_CONTROL_BUS);
		}else{
			__CLK_GATE_OFF(AHB_ARB0);
			__CLK_GATE_OFF(AHB_BRIDGE);
			__CLK_GATE_OFF(AHB_DATA_BUS);
			__CLK_GATE_OFF(AHB_CONTROL_BUS);
		}
		break;
	case MOD_DEMUX:
		PRINT_INFO("trun %s demux\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(DEMUX);
		}else{
			__CLK_GATE_OFF(DEMUX);
		}
		break;
	case MOD_SMART_CARD:
		PRINT_INFO("trun %s smart card\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(SMART_CARD_MPEG_DOMAIN);
		}else{
			__CLK_GATE_OFF(SMART_CARD_MPEG_DOMAIN);
		}
		break;
	case MOD_SDHC:
		PRINT_INFO("trun %s sdhc\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(SDHC);
		}else{
			__CLK_GATE_OFF(SDHC);
		}
		break;
	case MOD_STREAM:
		PRINT_INFO("trun %s stream\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(STREAM);
		}else{
			__CLK_GATE_OFF(STREAM);
		}
		break;
	case MOD_BLK_MOV:
		PRINT_INFO("trun %s blk_mov\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(BLK_MOV);
		}else{
			__CLK_GATE_OFF(BLK_MOV);
		}
		break;
	case MOD_MISC_DVIN:
		PRINT_INFO("trun %s dvin\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(MISC_DVIN);
		}else{
			__CLK_GATE_OFF(MISC_DVIN);
		}
		break;
	case MOD_USB0:
		PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(USB_GENERAL);
			__CLK_GATE_ON(USB0);
			__CLK_GATE_ON(MISC_USB0_TO_DDR);
		}else{
			__CLK_GATE_OFF(USB0);
			__CLK_GATE_OFF(MISC_USB0_TO_DDR);
			__CLK_GATE_OFF(USB_GENERAL);
		}
		break;
	case MOD_USB1:
		PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(USB_GENERAL);
			__CLK_GATE_ON(USB1);
			__CLK_GATE_ON(MISC_USB1_TO_DDR);
		}else{
			__CLK_GATE_OFF(USB1);
			__CLK_GATE_ON(MISC_USB1_TO_DDR);
			__CLK_GATE_OFF(USB_GENERAL);
		}
		break;
	case MOD_SDIO:
		PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
		if(flag){
			__CLK_GATE_ON(SDIO);
		}else{
			__CLK_GATE_OFF(SDIO);
		}
		break;
	default:
		PRINT_INFO("mod type not support\n");
		ret = -1;
		break;
	}
	return ret;
}

static int get_mod(mod_record_t* mod_record)
{
	int ret = 0;
	unsigned long flags;
	PRINT_INFO("get mod  %s\n", mod_record->name);
	spin_lock_irqsave(&gate_lock, flags);
	ret = _switch_gate(mod_record->type, 1);
	spin_unlock_irqrestore(&gate_lock, flags);
	return ret;
}

static int put_mod(mod_record_t* mod_record)
{
	int ret = 0;
	unsigned long flags;
	PRINT_INFO("put mod  %s\n", mod_record->name);
	spin_lock_irqsave(&gate_lock, flags);
	ret = _switch_gate(mod_record->type, 0); 
	spin_unlock_irqrestore(&gate_lock, flags);
	return ret;
}

void switch_mod_gate_by_type(mod_type_t type, int flag)
{
	if (flag)
		get_mod(&mod_records[type]);
	else
		put_mod(&mod_records[type]);
}
EXPORT_SYMBOL(switch_mod_gate_by_type);

void switch_mod_gate_by_name(const char* mod_name, int flag)
{
	int i = 0;
	//PRINT_INFO("arg mod_name is %s\n", mod_name);
	while(mod_records[i].name && i < MOD_MAX_NUM) {
		//PRINT_INFO("mod%d name is %s\n", i, mod_records[i].name);
		if (!strncmp(mod_name, mod_records[i].name, strlen(mod_name))) {
			if (flag)
				get_mod(&mod_records[i]);
			else
				put_mod(&mod_records[i]);
			break;
		}
		i++;
	}
}
EXPORT_SYMBOL(switch_mod_gate_by_name);

void switch_lcd_mod_gate(int flag)
{
	unsigned long flags;
	
	spin_lock_irqsave(&gate_lock, flags);
	PRINT_INFO("turn %s lcd module\n", flag?"on":"off");
	if (flag) {
		//__CLK_GATE_ON(VCLK2_ENCL);
		__CLK_GATE_ON(VCLK2_VENCL);
		__CLK_GATE_ON(EDP_CLK);
	} else {
		__CLK_GATE_OFF(EDP_CLK);
		__CLK_GATE_OFF(VCLK2_VENCL);
		//__CLK_GATE_OFF(VCLK2_ENCL);
	}
	spin_unlock_irqrestore(&gate_lock, flags);
}
EXPORT_SYMBOL(switch_lcd_mod_gate);

void power_gate_init(void)
{
	GATE_INIT(DDR);
	GATE_INIT(DOS);
	GATE_INIT(AHB_BRIDGE);
	GATE_INIT(ISA);
	GATE_INIT(PL310_CBUS);
	GATE_INIT(PERIPHS_TOP);
	GATE_INIT(SPICC);
	GATE_INIT(I2C);
	GATE_INIT(SAR_ADC);
	GATE_INIT(SMART_CARD_MPEG_DOMAIN);
	GATE_INIT(RANDOM_NUM_GEN);
	GATE_INIT(UART0);
	GATE_INIT(SDHC);
	GATE_INIT(STREAM);
	GATE_INIT(ASYNC_FIFO);
	GATE_INIT(SDIO);
	GATE_INIT(AUD_BUF);
	GATE_INIT(HIU_PARSER);
	//GATE_INIT(HDMI_RX);
	GATE_INIT(ASSIST_MISC);
	GATE_INIT(SPI);
	GATE_INIT(AUD_IN);
	GATE_INIT(ETHERNET);
	GATE_INIT(DEMUX);
	GATE_INIT(AIU_AI_TOP_GLUE);
	GATE_INIT(AIU_IEC958);
	GATE_INIT(AIU_I2S_OUT);
	GATE_INIT(AIU_AMCLK_MEASURE);
	GATE_INIT(AIU_AIFIFO2);
	GATE_INIT(AIU_AUD_MIXER);
	GATE_INIT(AIU_MIXER_REG);
	GATE_INIT(AIU_ADC);
	GATE_INIT(BLK_MOV);
	GATE_INIT(AIU_TOP_LEVEL);
	GATE_INIT(UART1);
	//GATE_INIT(CSI_DIG_CLKIN);
	GATE_INIT(GE2D);
	GATE_INIT(USB0);
	GATE_INIT(USB1);
	GATE_INIT(RESET);
	GATE_INIT(NAND);
	GATE_INIT(HIU_PARSER_TOP);
	GATE_INIT(USB_GENERAL);
	GATE_INIT(VDIN1);
	GATE_INIT(AHB_ARB0);
	GATE_INIT(EFUSE);
	GATE_INIT(ROM_CLK);
	GATE_INIT(AHB_DATA_BUS);
	GATE_INIT(AHB_CONTROL_BUS);
	GATE_INIT(HDMI_INTR_SYNC);
	GATE_INIT(HDMI_PCLK);
	GATE_INIT(MISC_USB1_TO_DDR);
	GATE_INIT(MISC_USB0_TO_DDR);
	//GATE_INIT(AIU_PCLK);
	GATE_INIT(MMC_PCLK);
	GATE_INIT(MISC_DVIN);
	GATE_INIT(UART2);
	GATE_INIT(SANA);
	GATE_INIT(VPU_INTR);
	GATE_INIT(SECURE_AHP_APB3);
	GATE_INIT(CLK81_TO_A9);
	GATE_INIT(VCLK2_VENCI);
	GATE_INIT(VCLK2_VENCI1);
	GATE_INIT(VCLK2_VENCP);
	GATE_INIT(VCLK2_VENCP1);
	GATE_INIT(VCLK2_VENCT);
	GATE_INIT(VCLK2_VENCT1);
	GATE_INIT(VCLK2_OTHER);
	GATE_INIT(VCLK2_ENCI);
	GATE_INIT(VCLK2_ENCP);
	GATE_INIT(DAC_CLK);
	GATE_INIT(AIU_AOCLK);
	GATE_INIT(AIU_ICE958_AMCLK);
	GATE_INIT(ENC480P);
	GATE_INIT(RANDOM_NUM_GEN1);
	GATE_INIT(GCLK_VENCL_INT);
	//GATE_INIT(VCLK2_ENCL);
	GATE_INIT(MMC_CLK);
	GATE_INIT(VCLK2_VENCL);
	GATE_INIT(VCLK2_OTHER1);
	GATE_INIT(EDP_CLK);
	GATE_INIT(MEDIA_CPU);
}

#if 1 //disable sysfs interface.

static struct class* mod_gate_clsp;

static ssize_t store_mod_on(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	#if 0
	char tmp_str[32];
	memset(tmp_str, 0, 32);
	strncpy(tmp_str, buf, 32);
	while(tmp_str[0] && tmp_str[strlen(tmp_str)-1] < 33 )
	tmp_str[strlen(tmp_str)-1] = 0;
	//switch_mod_gate_by_name(tmp_str, 1);
	#endif
	return count;
}

static ssize_t store_mod_off(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	#if 0
	char tmp_str[32];
	memset(tmp_str, 0, 32);
	strncpy(tmp_str, buf, 32);
	while(tmp_str[0] && tmp_str[strlen(tmp_str)-1] < 33 )
		tmp_str[strlen(tmp_str)-1] = 0;
	switch_mod_gate_by_name(tmp_str, 0);
	#endif
	return count;
}

static struct class_attribute aml_mod_attrs[]={
	__ATTR(mod_on,  S_IRUGO | S_IWUSR, NULL, store_mod_on),
	__ATTR(mod_off,  S_IRUGO | S_IWUSR, NULL, store_mod_off),
	__ATTR_NULL,
};

static int __init mode_gate_mgr_init(void)
{
	int ret = 0, i = 0;
	power_gate_init();
	mod_gate_clsp = class_create(THIS_MODULE, "aml_mod");
	if(IS_ERR(mod_gate_clsp)){
		ret = PTR_ERR(mod_gate_clsp);
		return ret;
	}
	for(i = 0; aml_mod_attrs[i].attr.name; i++){
		if(class_create_file(mod_gate_clsp, &aml_mod_attrs[i]) < 0)
			goto err;
	}
	return 0;
err:
	for(i=0; aml_mod_attrs[i].attr.name; i++){
		class_remove_file(mod_gate_clsp, &aml_mod_attrs[i]);
	}
	class_destroy(mod_gate_clsp); 
	return -1;  
}
arch_initcall(mode_gate_mgr_init);
#endif

int  video_dac_enable(unsigned char enable_mask)
{
    //switch_mod_gate_by_name("venc", 1);
    //CLEAR_CBUS_REG_MASK(VENC_VDAC_SETTING, enable_mask & 0x1f);
    return 0;
}
EXPORT_SYMBOL(video_dac_enable);

int  video_dac_disable()
{
    //SET_CBUS_REG_MASK(VENC_VDAC_SETTING, 0x1f);
    //switch_mod_gate_by_name("venc", 0);
  
    return 0;
}
EXPORT_SYMBOL(video_dac_disable);


