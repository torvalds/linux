/*amlsd.h*/

#ifndef AMLSD_H
#define AMLSD_H


#define AML_MMC_MAJOR_VERSION   1
#define AML_MMC_MINOR_VERSION   07
#define AML_MMC_VERSION         ((AML_MMC_MAJOR_VERSION << 8) | AML_MMC_MINOR_VERSION)
#define AML_MMC_VER_MESSAGE     "2014-06-30: eMMC add hw reset function"

extern unsigned sdhc_debug;
extern unsigned sdio_debug;
#define DEBUG_SD_OF		1
//#define DEBUG_SD_OF			0

#define MODULE_NAME		"amlsd"

#define LDO4DAC_REG_ADDR        0x4f
#define LDO4DAC_REG_1_8_V       0x24
#define LDO4DAC_REG_2_8_V       0x4c
#define LDO4DAC_REG_3_3_V       0x60

#define AMLSD_DBG_COMMON	(1<<0)
#define AMLSD_DBG_REQ		(1<<1)
#define AMLSD_DBG_RESP		(1<<2)
#define AMLSD_DBG_REG		(1<<3)
#define AMLSD_DBG_RD_TIME	(1<<4)
#define AMLSD_DBG_WR_TIME	(1<<5)
#define AMLSD_DBG_BUSY_TIME	(1<<6)
#define AMLSD_DBG_RD_DATA	(1<<7)
#define AMLSD_DBG_WR_DATA	(1<<8)
#define AMLSD_DBG_IOS		(1<<9)
#define AMLSD_DBG_IRQ		(1<<10)
#define AMLSD_DBG_CLKC		(1<<11)
#define AMLSD_DBG_TUNING	(1<<12)

#define     DETECT_CARD_IN          1
#define     DETECT_CARD_OUT         2
#define     DETECT_CARD_JTAG_IN     3
#define     DETECT_CARD_JTAG_OUT    4
void aml_sd_uart_detect (struct amlsd_platform* pdata);
void aml_sd_uart_detect_clr (struct amlsd_platform* pdata);

#define EMMC_DAT3_PINMUX_CLR    0
#define EMMC_DAT3_PINMUX_SET    1

#define CHECK_RET(ret) \
    if (ret) \
        printk("\033[0;47;33m [%s] gpio op failed(%d) at line %d \033[0m\n", __FUNCTION__, ret, __LINE__)

#define sdhc_dbg(dbg_level, fmt, args...) do{\
	if(dbg_level & sdhc_debug)	\
		printk("[%s]" fmt , __FUNCTION__, ##args);	\
}while(0)

#define sdhc_err(fmt, args...) do{\
	printk("[%s]\033[0;40;32m " fmt "\033[0m", __FUNCTION__, ##args);  \
}while(0)

#define sdio_dbg(dbg_level, fmt, args...) do{\
	if(dbg_level & sdio_debug)	\
		printk("[%s]" fmt , __FUNCTION__, ##args);	\
}while(0)

#define sdio_err(fmt, args...) do{\
	printk("[%s]\033[0;40;33m " fmt "\033[0m", __FUNCTION__, ##args);	\
}while(0)

#define SD_PARSE_U32_PROP(node, prop_name, prop, value)      		\
    if (!of_property_read_u32(node, prop_name, &prop)){  			\
		value = prop;												\
		prop = 0;													\
	    if (DEBUG_SD_OF) {                                          \
	        printk("get property:%25s, value:0x%08x\n",    			\
	            prop_name, (unsigned int)value);                           		\
	    }															\
    }

#define SD_PARSE_GPIO_NUM_PROP(node, prop_name, str, gpio_pin)		\
	if(!of_property_read_string(node, prop_name, &str)) { 			\
		gpio_pin = amlogic_gpio_name_map_num(str);					\
		if (DEBUG_SD_OF) {                                     		\
	        printk("get property:%25s, str:%s\n",    				\
	            prop_name, str);    								\
		}															\
	}

#define SD_PARSE_STRING_PROP(node, prop_name, str, prop)      		\
	if (!of_property_read_string(node, prop_name, &str)){ 			\
		strcpy(prop, str);											\
		if (DEBUG_SD_OF) {											\
			printk("get property:%25s, str:%s\n", 					\
				prop_name, prop);									\
		}															\
	}

#define SD_CAPS(a, b) { .caps = a, .name = b }

struct sd_caps {
	unsigned caps;
	const char *name;
};

extern int storage_flag;

void aml_mmc_ver_msg_show (void);
extern void aml_sdhc_init_debugfs(struct mmc_host *mmc);
void aml_sdhc_print_reg_(u32 *buf);
extern void aml_sdhc_print_reg(struct amlsd_host* host);
extern void aml_sdio_init_debugfs(struct mmc_host *mmc);
extern void aml_sdio_print_reg(struct amlsd_host* host);

extern int add_part_table(struct mtd_partition * part, unsigned int nr_part);
extern int add_emmc_partition(struct gendisk * disk);
extern size_t aml_sg_copy_buffer(struct scatterlist *sgl, unsigned int nents,
			     void *buf, size_t buflen, int to_buffer);

int amlsd_get_platform_data(struct platform_device* pdev,
                struct amlsd_platform* pdata,
                struct mmc_host* mmc, u32 index);

int amlsd_get_reg_base(struct platform_device* pdev,
				struct amlsd_host* host);


// int of_amlsd_detect(struct amlsd_platform* pdata);
void of_amlsd_irq_init(struct amlsd_platform* pdata);
void of_amlsd_pwr_prepare(struct amlsd_platform* pdata);
void of_amlsd_pwr_on(struct amlsd_platform* pdata);
void of_amlsd_pwr_off(struct amlsd_platform* pdata);
int of_amlsd_init(struct amlsd_platform* pdata);
void of_amlsd_xfer_pre(struct amlsd_platform* pdata);
void of_amlsd_xfer_post(struct amlsd_platform* pdata);
int of_amlsd_ro (struct amlsd_platform* pdata);

void aml_sd_uart_detect (struct amlsd_platform* pdata);
irqreturn_t aml_sd_irq_cd(int irq, void *dev_id);
irqreturn_t aml_irq_cd_thread(int irq, void *data);
void aml_sduart_pre (struct amlsd_platform* pdata);
int aml_sd_voltage_switch (struct amlsd_platform* pdata, char signal_voltage);
int aml_check_unsupport_cmd(struct mmc_host* mmc, struct mmc_request* mrq);

void aml_cs_high (struct amlsd_platform * pdata); // chip select high
void aml_cs_dont_care (struct amlsd_platform * pdata); // chip select don't care
bool is_emmc_exist (struct amlsd_host* host); // is eMMC/tSD exist
void aml_devm_pinctrl_put (struct amlsd_host* host);
// void of_init_pins (struct amlsd_platform* pdata);
extern void aml_emmc_hw_reset(struct mmc_host *mmc);

void aml_snprint (char **pp, int *left_size,  const char *fmt, ...);

void aml_dbg_print_pinmux (void);
#ifdef      CONFIG_MMC_AML_DEBUG
void aml_dbg_verify_pull_up (struct amlsd_platform * pdata);
int aml_dbg_verify_pinmux (struct amlsd_platform * pdata);
#endif

#endif

