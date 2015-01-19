#include <mach/power_gate.h>
#include <mach/mod_gate.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <mach/am_regs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <linux/amlogic/vout/vout_notify.h>

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

DEFINE_SPINLOCK(mod_lock);

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
        .name = "lvds",
        .type = MOD_LVDS,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
        .no_share = 1,
    },{
        .name = "mipi",
        .type = MOD_MIPI,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
    },{
        .name = "bt656",
        .type = MOD_BT656,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
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
        .name = "uart3",
        .type = MOD_UART3,
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
        .name = "vdin",
        .type = MOD_VIDEO_IN,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
    },{
        .name = "viu2",
        .type = MOD_VIU2,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
    },{
        .name = "audio_in",
        .type = MOD_AUD_IN,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
    },{
        .name = "audio_out",
        .type = MOD_AUD_OUT,
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
        .name = "rdma",
        .type = MOD_MISC_RDMA,
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
        .name = "vi_core",
        .type = MOD_VI_CORE,
        .ref = 0,
        .flag = 1,
        .dc_en = 0,
    },{
        .name = "led_pwm",
        .type = MOD_LED_PWM,
        .ref = 0,
        .flag = 1,
        .dc_en = 1,
    },{
        .name = "vdac",
        .type = MOD_VDAC,
        .ref = 0,
        .flag = 1,
        .dc_en = 1,
        .no_share = 1,
    },
    
    {
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
                GATE_ON(DOS);
                aml_set_reg32_mask(P_HHI_VDEC_CLK_CNTL, 1 << 8);
            } else {
                GATE_OFF(DOS);
                aml_clr_reg32_mask(P_HHI_VDEC_CLK_CNTL, 1 << 8);
            }
            break;
        case MOD_AUDIO:
            PRINT_INFO("turn %s audio module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(AIU_AMCLK_MEASURE);
                GATE_ON(AIU_AIFIFO2);
                GATE_ON(AIU_AUD_MIXER);
                GATE_ON(AIU_MIXER_REG);
                
                GATE_ON(AIU_IEC958);
                GATE_ON(AIU_AI_TOP_GLUE);
                GATE_ON(AUD_BUF);
                GATE_ON(AIU_I2S_OUT);
                GATE_ON(AIU_AMCLK); //this gate should not be turned off
                GATE_ON(AIU_ICE958_AMCLK);
                GATE_ON(AIU_AOCLK);   
                //GATE_ON(AUD_IN);
                GATE_ON(AIU_ADC);
                GATE_ON(AIU_AUDIN_SCLK);
            } else {   
            	  GATE_OFF(AIU_AMCLK_MEASURE);
                GATE_OFF(AIU_AIFIFO2);
                GATE_OFF(AIU_AUD_MIXER);
                GATE_OFF(AIU_MIXER_REG);
                
                GATE_OFF(AIU_IEC958);
                GATE_OFF(AIU_AI_TOP_GLUE);
                GATE_OFF(AUD_BUF);
                GATE_OFF(AIU_I2S_OUT);         
                GATE_OFF(AIU_AMCLK); //this gate should not be turned off
                GATE_OFF(AIU_ICE958_AMCLK);
                GATE_OFF(AIU_AOCLK);
                //GATE_OFF(AUD_IN);
                GATE_OFF(AIU_ADC);
                GATE_OFF(AIU_AUDIN_SCLK);
            }
            break;
        case MOD_HDMI:
            PRINT_INFO("turn %s hdmi module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(HDMI_INTR_SYNC);
                GATE_ON(HDMI_PCLK);
                GATE_ON(VCLK1_HDMI);
            } else {
                GATE_OFF(HDMI_INTR_SYNC);
                GATE_OFF(HDMI_PCLK);
                GATE_OFF(VCLK1_HDMI);
            }            
            break;
        case MOD_VENC:
            PRINT_INFO("turn %s venc module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(VCLK2_VENCI);
                GATE_ON(VCLK2_VENCI1);
                GATE_ON(VCLK2_VENCP);
                GATE_ON(VCLK2_VENCP1);
                GATE_ON(VENC_P_TOP);
                GATE_ON(VENC_I_TOP);
                GATE_ON(VENCI_INT);
                GATE_ON(VENCP_INT);
                GATE_ON(VCLK2_ENCI);
                GATE_ON(VCLK2_ENCP);
                GATE_ON(VCLK2_VENCT);
                GATE_ON(VCLK2_VENCT1);
                GATE_ON(VCLK2_OTHER);
                GATE_ON(VCLK2_OTHER1);
                GATE_ON(ENC480P);
                GATE_ON(VENC_DAC);
                GATE_ON(DAC_CLK);
            } else {
                GATE_OFF(VCLK2_VENCI);
                GATE_OFF(VCLK2_VENCI1);
                GATE_OFF(VCLK2_VENCP);  
                GATE_OFF(VENC_P_TOP);
                GATE_OFF(VENC_I_TOP);
                GATE_OFF(VENCI_INT);
                GATE_OFF(VCLK2_ENCI);
                if(get_power_level() == 0) {
                    GATE_OFF(VCLK2_VENCP1);
                    GATE_OFF(VENCP_INT);
                    GATE_OFF(VCLK2_ENCP);
                }
                GATE_OFF(VCLK2_VENCT);
                GATE_OFF(VCLK2_VENCT1);
                GATE_OFF(VCLK2_OTHER);
                GATE_OFF(VCLK2_OTHER1);
                GATE_OFF(ENC480P);
                GATE_OFF(VENC_DAC);
                GATE_OFF(DAC_CLK);
            }
            break;
        case MOD_TCON:
            PRINT_INFO("turn %s tcon module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(VENC_T_TOP);
                GATE_ON(VENCT_INT);
                GATE_ON(VCLK2_ENCT);
            } else {
                GATE_OFF(VENC_T_TOP);
                GATE_OFF(VENCT_INT);
                GATE_OFF(VCLK2_ENCT);
            }
            break;
        case MOD_LVDS:
            PRINT_INFO("turn %s lvds module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(VENC_L_TOP);
                GATE_ON(VENCL_INT);
                GATE_ON(VCLK2_ENCL);
            } else {
                GATE_OFF(VENC_L_TOP);
                GATE_OFF(VENCL_INT);
                GATE_OFF(VCLK2_ENCL);
            }
            break;
        case MOD_MIPI:
            PRINT_INFO("turn %s mipi module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(MIPI_APB_CLK);
                GATE_ON(MIPI_SYS_CLK);
                GATE_ON(MIPI_PHY);
            } else {
                GATE_OFF(MIPI_APB_CLK);
                GATE_OFF(MIPI_SYS_CLK);
                GATE_OFF(MIPI_PHY);
            }
            break;
        case MOD_BT656:
            PRINT_INFO("turn %s bt656 module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(BT656_IN);
            } else {
                GATE_OFF(BT656_IN);
            }
            break;
        case MOD_SPI:
            PRINT_INFO("turn %s spi module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(SPICC);
                GATE_ON(SPI1);
                GATE_ON(SPI2);
            } else {
                GATE_OFF(SPICC);
                GATE_OFF(SPI1);
                GATE_OFF(SPI2);
            }
            break;
        case MOD_UART0:
            PRINT_INFO("turn %s uart0 module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(UART0);
            } else {
                GATE_OFF(UART0);
            }
            break;
        case MOD_UART1:
            PRINT_INFO("turn %s uart1 module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(UART1);
            } else {
                GATE_OFF(UART1);
            }
            break;
        case MOD_UART2:
            PRINT_INFO("turn %s uart2 module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(UART2);
            } else {
                GATE_OFF(UART2);
            }
            break;
        case MOD_UART3:
            PRINT_INFO("turn %s uart3 module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(UART3);
            } else {
                GATE_OFF(UART3);
            }
            break;
        case MOD_ROM:
            PRINT_INFO("turn %s rom module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(ROM_CLK);
            } else {
                GATE_OFF(ROM_CLK);
            }
            break;
        case MOD_EFUSE:
            PRINT_INFO("turn %s efuse module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(EFUSE);
            } else {
                GATE_OFF(EFUSE);
            }
            break;
        case MOD_RANDOM_NUM_GEN:
            PRINT_INFO("turn %s random_num_gen module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(RANDOM_NUM_GEN);
            } else {
                GATE_OFF(RANDOM_NUM_GEN);
            }
            break;
        case MOD_ETHERNET:
            PRINT_INFO("turn %s ethernet module\n", flag?"on":"off");
            if (flag) {
                GATE_ON(ETHERNET);
            } else {
                GATE_OFF(ETHERNET);
            }
            break;
        case MOD_MEDIA_CPU:
            PRINT_INFO("trun %s Audio DSP\n", flag? " on" : "off");
            if(flag){
                GATE_ON(MEDIA_CPU);
            }else{
                 GATE_OFF(MEDIA_CPU);
            }
            break;
        case MOD_GE2D:
            PRINT_INFO("trun %s GE2D\n", flag? " on" : "off");
            if(flag){
                GATE_ON(GE2D);
            }else{
                GATE_OFF(GE2D);
            }
            break;
        case MOD_VIDEO_IN:
            PRINT_INFO("trun %s video_in\n", flag? " on" : "off");
            if(flag){
                GATE_ON(VIDEO_IN);
            }else{
                GATE_OFF(VIDEO_IN);
            }
            break;
        case MOD_VIU2:
            PRINT_INFO("trun %s viu2\n", flag? " on" : "off");
            if(flag){
                GATE_ON(VIU2);
            }else{
                GATE_OFF(VIU2);
            }
            break;
        case MOD_AUD_IN:
            PRINT_INFO("trun %s audio_in\n", flag? " on" : "off");
            #if 0
            if(flag){
                GATE_ON(AIU_ADC);
                GATE_ON(AIU_AUDIN_SCLK);
            }else{
                GATE_OFF(AIU_ADC);
                GATE_OFF(AIU_AUDIN_SCLK);
            }
            #endif
            break;
        case MOD_AUD_OUT:
            PRINT_INFO("trun %s audio_out\n", flag? " on" : "off");
            if(flag){

            }else{

            }
            break;
        case MOD_AHB:
            PRINT_INFO("trun %s ahb\n", flag? " on" : "off");
            if(flag){
                GATE_ON(AHB_ARB0);
                GATE_ON(AHB_BRIDGE);
                GATE_ON(AHB_DATA_BUS);
                GATE_ON(AHB_CONTROL_BUS);
            }else{
                GATE_OFF(AHB_ARB0);
                GATE_OFF(AHB_BRIDGE);
                GATE_OFF(AHB_DATA_BUS);
                GATE_OFF(AHB_CONTROL_BUS);
            }
            break;
        case MOD_DEMUX:
            PRINT_INFO("trun %s demux\n", flag? " on" : "off");
            if(flag){
                GATE_ON(DEMUX);
            }else{
                GATE_OFF(DEMUX);
            }
            break;
        case MOD_SMART_CARD:
            PRINT_INFO("trun %s smart card\n", flag? " on" : "off");
            if(flag){
                GATE_ON(SMART_CARD_MPEG_DOMAIN);
            }else{
                GATE_OFF(SMART_CARD_MPEG_DOMAIN);
            }
            break;
        case MOD_SDHC:
            PRINT_INFO("trun %s sdhc\n", flag? " on" : "off");
            if(flag){
                GATE_ON(SDHC);
            }else{
                GATE_OFF(SDHC);
            }
            break;
        case MOD_STREAM:
            PRINT_INFO("trun %s stream\n", flag? " on" : "off");
            if(flag){
                GATE_ON(STREAM);
            }else{
                GATE_OFF(STREAM);
            }
            break;
        case MOD_BLK_MOV:
            PRINT_INFO("trun %s blk_mov\n", flag? " on" : "off");
            if(flag){
                GATE_ON(BLK_MOV);
            }else{
                GATE_OFF(BLK_MOV);
            }
            break;
        case MOD_MISC_DVIN:
            PRINT_INFO("trun %s dvin\n", flag? " on" : "off");
            if(flag){
                GATE_ON(MISC_DVIN);
            }else{
                GATE_OFF(MISC_DVIN);
            }
            break;
        case MOD_MISC_RDMA:
            PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
            if(flag){
                GATE_ON(MISC_RDMA);
            }else{
                GATE_OFF(MISC_RDMA);
            }
            break;
        case MOD_USB0:
            PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
            if(flag){
                GATE_ON(USB0);
                GATE_ON(MISC_USB0_TO_DDR);
            }else{
                GATE_OFF(USB0);
                GATE_ON(MISC_USB0_TO_DDR);
            }
            break;
        case MOD_USB1:
            PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
            if(flag){
                GATE_ON(USB1);
                GATE_ON(MISC_USB1_TO_DDR);
            }else{
                GATE_OFF(USB1);
                GATE_ON(MISC_USB1_TO_DDR);
            }
            break;
        case MOD_SDIO:
            PRINT_INFO("trun %s rdma\n", flag? " on" : "off");
            if(flag){
                GATE_ON(SDIO);
            }else{
                GATE_OFF(SDIO);
            }
            break;
        case MOD_VI_CORE:
            PRINT_INFO("trun %s vi core\n", flag? " on" : "off");
            if(flag){
                GATE_ON(VI_CORE);
            }else{
                GATE_OFF(VI_CORE);
            }
            break;
        case MOD_LED_PWM:
            PRINT_INFO("trun %s led pwm\n", flag? " on" : "off");
            if(flag){
                GATE_ON(LED_PWM);
            }else{
                GATE_OFF(LED_PWM);
            }
            break;
        case MOD_VDAC:
            printk("trun %s vdac\n", flag? " on" : "off");
            if(flag){
                aml_write_reg32(P_VENC_VDAC_SETTING, 0x0);
            }else{
                aml_write_reg32(P_VENC_VDAC_SETTING, 0xffffffff);
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
    spin_lock_irqsave(&mod_lock, flags);
    if (mod_record->no_share)
    	ret = _switch_gate(mod_record->type, 1);
    else {
    if(mod_record->ref > 0)
        mod_record->ref++;
    else {
        mod_record->ref = 1;
        mod_record->flag = 1;
        ret = _switch_gate(mod_record->type, 1);
    }  
    }
    spin_unlock_irqrestore(&mod_lock, flags);
    return ret;
}

static int put_mod(mod_record_t* mod_record)
{
    int ret = 0;
    unsigned long flags;
    PRINT_INFO("put mod  %s\n", mod_record->name);
    spin_lock_irqsave(&mod_lock, flags);
    if (mod_record->no_share) 
    	ret = _switch_gate(mod_record->type, 0); 
    else {
    mod_record->ref--;
    if(mod_record->ref <= 0) {
        ret = _switch_gate(mod_record->type, 0); 
        mod_record->ref = 0;
        mod_record->flag = 0;
        }else
            printk("ref value is %d\n", mod_record->ref);
    }
    spin_unlock_irqrestore(&mod_lock, flags);
    return ret;
}

static void _switch_mod_gate_by_type(mod_type_t type, int flag, int dc_protect)
{
    if (mod_records[type].dc_en <= 0 && dc_protect)
        return;
    else {
        if (flag)
            get_mod(&mod_records[type]);
        else
            put_mod(&mod_records[type]);
    }
}

void switch_mod_gate_by_type(mod_type_t type, int flag)
{
    _switch_mod_gate_by_type(type, flag, 1);
}
EXPORT_SYMBOL(switch_mod_gate_by_type);

static void _switch_mod_gate_by_name(const char* mod_name, int flag, int dc_protect)
{
    int i = 0;
    //PRINT_INFO("arg mod_name is %s\n", mod_name);
    while(mod_records[i].name && i < MOD_MAX_NUM) {
        //PRINT_INFO("mod%d name is %s\n", i, mod_records[i].name);
        if (!strncmp(mod_name, mod_records[i].name, strlen(mod_name))) {
            if(mod_records[i].dc_en <= 0 && dc_protect)
                break;
            else {
                if (flag)
                    get_mod(&mod_records[i]);
                else
                    put_mod(&mod_records[i]);
                break;
            }
        }
        i++;
    }
}

void switch_mod_gate_by_name(const char* mod_name, int flag)
{
    _switch_mod_gate_by_name(mod_name, flag, 1);
}
EXPORT_SYMBOL(switch_mod_gate_by_name);

void power_gate_init(void)
{
    GATE_INIT(DDR);
    GATE_INIT(DOS);
    GATE_INIT(MIPI_APB_CLK);
    GATE_INIT(MIPI_SYS_CLK);
    GATE_INIT(AHB_BRIDGE);
    GATE_INIT(ISA);
    GATE_INIT(APB_CBUS);
    GATE_INIT(_1200XXX);
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
    GATE_INIT(BT656_IN);
    GATE_INIT(ASSIST_MISC);
    GATE_INIT(VENC_I_TOP);
    GATE_INIT(VENC_P_TOP);
    GATE_INIT(VENC_T_TOP);
    GATE_INIT(VENC_DAC);
    GATE_INIT(VI_CORE);
    GATE_INIT(SPI2);
    GATE_INIT(SPI1);
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
    GATE_INIT(UART1);
    GATE_INIT(LED_PWM);
    GATE_INIT(VGHL_PWM);
    GATE_INIT(GE2D);
    GATE_INIT(USB0);
    GATE_INIT(USB1);
    GATE_INIT(RESET);
    GATE_INIT(NAND);
    GATE_INIT(HIU_PARSER_TOP);
    GATE_INIT(MIPI_PHY);
    GATE_INIT(VIDEO_IN);
    GATE_INIT(AHB_ARB0);
    GATE_INIT(EFUSE);
    GATE_INIT(ROM_CLK);
    GATE_INIT(AHB_DATA_BUS);
    GATE_INIT(AHB_CONTROL_BUS);
    GATE_INIT(HDMI_INTR_SYNC);
    GATE_INIT(HDMI_PCLK);
    GATE_INIT(MISC_USB1_TO_DDR);
    GATE_INIT(MISC_USB0_TO_DDR);
    GATE_INIT(MMC_PCLK);
    GATE_INIT(MISC_DVIN);
    GATE_INIT(MISC_RDMA);
    GATE_INIT(UART2);
    GATE_INIT(VENCI_INT);
    GATE_INIT(VIU2);
    GATE_INIT(VENCP_INT);
    GATE_INIT(VENCT_INT);
    GATE_INIT(VENCL_INT);
    GATE_INIT(VENC_L_TOP);
    GATE_INIT(UART3);
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
    GATE_INIT(AIU_AMCLK);
    GATE_INIT(AIU_ICE958_AMCLK);
    GATE_INIT(VCLK1_HDMI);
    GATE_INIT(AIU_AUDIN_SCLK);
    GATE_INIT(ENC480P);
    GATE_INIT(VCLK2_ENCT);
    GATE_INIT(VCLK2_ENCL);
    GATE_INIT(MMC_CLK);
    GATE_INIT(VCLK2_VENCL);
    GATE_INIT(VCLK2_OTHER1);
    GATE_INIT(MEDIA_CPU);
}

static struct class* mod_gate_clsp;

static ssize_t show_mod_on(struct class* class, struct class_attribute* attr,
    char* buf)
{
    ssize_t size = 0;
    int i = 0;
    while(mod_records[i].name && i < MOD_MAX_NUM) {
        if (mod_records[i].flag > 0)
            size += sprintf(buf + size, "%s\n", mod_records[i].name);
        i++;
    }
    return size;
}

static ssize_t store_mod_on(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	  char tmp_str[32];
	  memset(tmp_str, 0, 32);
    strncpy(tmp_str, buf, 32);
    while(tmp_str[0] && tmp_str[strlen(tmp_str)-1] < 33 )
      tmp_str[strlen(tmp_str)-1] = 0;
    _switch_mod_gate_by_name(tmp_str, 1, 0);
    return count;
}

static ssize_t show_mod_off(struct class* class, struct class_attribute* attr,
    char* buf)
{
    ssize_t size = 0;
    int i = 0;
    while(mod_records[i].name && i < MOD_MAX_NUM) {
        if (mod_records[i].flag <= 0)
            size += sprintf(buf + size, "%s\n", mod_records[i].name);
        i++;
    }
    return size;
}

static ssize_t store_mod_off(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
    char tmp_str[32];
	  memset(tmp_str, 0, 32);
    strncpy(tmp_str, buf, 32);
    while(tmp_str[0] && tmp_str[strlen(tmp_str)-1] < 33 )
      tmp_str[strlen(tmp_str)-1] = 0;
    _switch_mod_gate_by_name(tmp_str, 0, 0);
    return count;
}

static ssize_t show_dynamical_control(struct class* class, struct class_attribute* attr,
    char* buf)
{
    ssize_t size = 0;
    int i = 0;
    while(mod_records[i].name && i < MOD_MAX_NUM) {
        if (mod_records[i].dc_en > 0)
            size += sprintf(buf + size, "%s\n", mod_records[i].name);
        i++;
    }
    return size;
}

static ssize_t store_dynamical_control(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
    int i = 0;
    char tmp_str[32];
	  
    PRINT_INFO("arg mod_name is %s\n", buf);
    while(mod_records[i].name && i < MOD_MAX_NUM) {
    	  memset(tmp_str, 0, 32);
        strncpy(tmp_str, buf, 32);
        while(tmp_str[0] && tmp_str[strlen(tmp_str)-1] < 33 )
           tmp_str[strlen(tmp_str)-1] = 0;
        //PRINT_INFO("mod%d name is %s\n", i, mod_records[i].name);
        if (!strncmp(tmp_str, mod_records[i].name, strlen(tmp_str))) {
            mod_records[i].dc_en = 1;
            break;
        }
        i++;
    }
    return count;
}

static struct class_attribute aml_mod_attrs[]={
    __ATTR(mod_on,  S_IRUGO | S_IWUSR, show_mod_on, store_mod_on),
    __ATTR(mod_off,  S_IRUGO | S_IWUSR, show_mod_off, store_mod_off),
    __ATTR(dynamical_control,  S_IRUGO | S_IWUSR, show_dynamical_control, store_dynamical_control),
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
