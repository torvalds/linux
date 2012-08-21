/******************************************************************************
*                         C O M P I L E R   F L A G S
*******************************************************************************
*/

/******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
*******************************************************************************
*/
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <asm/current.h>
#include <asm/uaccess.h>
//for mtk
//#include <mach/mt_combo.h>
//#include <mach/mt6575_gpio.h>
//#include <mach/board.h>
//end for mtk

//for Ingenic
#include <mach/chip-misc.h>
#include <mach/chip-gpio.h>
#include <mach/chip-pin.h>
#include <mach/chip-rtc.h>
//end for Ingenic

#include "hw_test.h"

/******************************************************************************
*                              C O N S T A N T S
*******************************************************************************
*/
#define GPIO_PMU_PIN GPD27
#define GPIO_RST_PIN GPD29
#define GPIO_RTC_PIN GPD14
#define GPIO_BGF_INT GPF20
#define GPIO_WIF_INT GPF21

#define HWTEST_DEV_MAJOR 194 // never used number
#define HWTEST_DEV_NUM 1

/* Linux char device */
static int HWTEST_major = HWTEST_DEV_MAJOR;
static struct cdev HWTEST_cdev;
static atomic_t HWTEST_ref_cnt = ATOMIC_INIT(0);

unsigned char** g_read_card_info_bysdio;
unsigned char** g_test;

UINT8 g_read_current_io_isready_from_sdio_card;
UINT16 g_read_function_blksize_from_sdio_card;
UINT32 g_read_int_from_sdio_card;

/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/
#define HWTEST_SUPPORT_PHONE 0
#define HWTEST_SUPPORT_SDIO  1


int porting_6620_sdio_card_detect()
{
	int ret = -1;
	//for MTK
	//ret = mt_combo_sdio_ctrl(2,1);
	//end for MTK
	//for Ingenic
	ret = sdio_card_detect();
	//end for Ingenic
	if(!ret){
		return 0;
	}else{
		HWTEST_INFO_FUNC("sdio card detect fail");
		return -1;
	}
}
int porting_6620_sdio_card_remove()
{
	int ret = -1;
	//for MTK
	//ret = mt_combo_sdio_ctrl(2,0);
	//end for MTK
	//for Ingenic
	ret = sdio_card_remove();
	//end for Ingenic
	if(!ret){
		return 0;
	}else{
		HWTEST_INFO_FUNC("sdio card remove fail");
		return -1;
	}
}
//end for Ingenic

int chip_power_on(void)
{
	//int ret = -1;
#define MT6620_OFF_TIME (10) /* in ms, workable value */
#define MT6620_RST_TIME (30) /* in ms, workable value */
#define MT6620_STABLE_TIME (30) /* in ms, workable value */
#define MT6620_EXT_INT_TIME (5) /* in ms, workable value */
#define MT6620_32K_STABLE_TIME (100) /* in ms, test value */

#if 0
#if (HWTEST_SUPPORT_PHONE)
	/* disable pull */    
    mt_set_gpio_pull_enable(GPIO_COMBO_6620_LDO_EN_PIN, GPIO_PULL_DISABLE);    
    /* set output */    
	mt_set_gpio_dir(GPIO_COMBO_6620_LDO_EN_PIN, GPIO_DIR_OUT);    
	/* set gpio mode */    
	mt_set_gpio_mode(GPIO_COMBO_6620_LDO_EN_PIN, GPIO_MODE_GPIO);    
	/* external LDO_EN high */    
	mt_set_gpio_out(GPIO_COMBO_6620_LDO_EN_PIN, GPIO_OUT_ONE);
#endif

#if (HWTEST_SUPPORT_SDIO) && (CONFIG_MTK_COMBO_SDIO_SLOT == 0)
	printk(KERN_INFO "[mt6620] pull up sd0 bus(gpio169~gpio175(exclude gpio174))\n");
    mt_set_gpio_pull_enable(GPIO172, GPIO_PULL_ENABLE);	//->CLK
    mt_set_gpio_pull_select(GPIO172, GPIO_PULL_UP);		
    mt_set_gpio_pull_enable(GPIO171, GPIO_PULL_ENABLE);	//->CMD
    mt_set_gpio_pull_select(GPIO171, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO175, GPIO_PULL_ENABLE);	//->DAT0
    mt_set_gpio_pull_select(GPIO175, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO173, GPIO_PULL_ENABLE);	//->DAT1
    mt_set_gpio_pull_select(GPIO173, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO169, GPIO_PULL_ENABLE);	//->DAT2
    mt_set_gpio_pull_select(GPIO169, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO170, GPIO_PULL_ENABLE);	//->DAT3
    mt_set_gpio_pull_select(GPIO170, GPIO_PULL_UP);
#elif (CONFIG_MTK_COMBO_SDIO_SLOT == 1)
	#error "error:MSDC1 is not reserved for MT6620 on MT6575EVB"
#elif (CONFIG_MTK_COMBO_SDIO_SLOT == 2)
    printk(KERN_INFO "[mt6620] pull up sd2 bus(gpio182~187)\n");
    mt_set_gpio_pull_enable(GPIO182, GPIO_PULL_ENABLE);	//->CLK
    mt_set_gpio_pull_select(GPIO182, GPIO_PULL_UP);		
    mt_set_gpio_pull_enable(GPIO184, GPIO_PULL_ENABLE);	//->CMD
    mt_set_gpio_pull_select(GPIO184, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO186, GPIO_PULL_ENABLE);	//->DAT0
    mt_set_gpio_pull_select(GPIO186, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO187, GPIO_PULL_ENABLE);	//->DAT1
    mt_set_gpio_pull_select(GPIO187, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO185, GPIO_PULL_ENABLE);	//->DAT2
    mt_set_gpio_pull_select(GPIO185, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO183, GPIO_PULL_ENABLE);	//->DAT3
    mt_set_gpio_pull_select(GPIO183, GPIO_PULL_UP);
#elif (CONFIG_MTK_COMBO_SDIO_SLOT == 3)
	printk(KERN_INFO "[mt6620] pull up sd3 bus (GPIO89~GPIO94)\n");
    mt_set_gpio_pull_enable(GPIO92, GPIO_PULL_ENABLE);	//->CLK
    mt_set_gpio_pull_select(GPIO92, GPIO_PULL_UP);		
    mt_set_gpio_pull_enable(GPIO91, GPIO_PULL_ENABLE);	//->CMD
    mt_set_gpio_pull_select(GPIO91, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO94, GPIO_PULL_ENABLE);	//->DAT0
    mt_set_gpio_pull_select(GPIO94, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO90, GPIO_PULL_ENABLE);	//->DAT1
    mt_set_gpio_pull_select(GPIO90, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO89, GPIO_PULL_ENABLE);	//->DAT2
    mt_set_gpio_pull_select(GPIO89, GPIO_PULL_UP);
    mt_set_gpio_pull_enable(GPIO93, GPIO_PULL_ENABLE);	//->DAT3
    mt_set_gpio_pull_select(GPIO93, GPIO_PULL_UP);
#else
	#error "error:unsupported CONFIG_MTK_COMBO_SDIO_SLOT" CONFIG_MTK_COMBO_SDIO_SLOT
#endif

    /* UART Mode */
    ret = mt_set_gpio_mode(GPIO_UART_URXD3_PIN, GPIO_UART_URXD3_PIN_M_URXD);//GPIO_MODE_01->GPIO_UART_URXD3_PIN_M_URXD
    ret += mt_set_gpio_mode(GPIO_UART_UTXD3_PIN, GPIO_UART_UTXD3_PIN_M_UTXD);//GPIO_MODE_01->GPIO_UART_UTXD3_PIN_M_UTXD
    //printk(KERN_INFO "[mt6620] set UART GPIO Mode [%d]\n", result);

    /* disable pull */  
#if (HWTEST_SUPPORT_PHONE)
	mt_set_gpio_pull_enable(GPIO25, GPIO_PULL_DISABLE);
#endif
    ret += mt_set_gpio_pull_enable(GPIO_COMBO_PMU_EN_PIN, GPIO_PULL_DISABLE);
    ret += mt_set_gpio_pull_enable(GPIO_COMBO_RST_PIN, GPIO_PULL_DISABLE);

	/* set output */
#if (HWTEST_SUPPORT_PHONE)
	mt_set_gpio_dir(GPIO25, GPIO_DIR_OUT);
#endif
	ret += mt_set_gpio_dir(GPIO_COMBO_PMU_EN_PIN, GPIO_DIR_OUT);
    ret += mt_set_gpio_dir(GPIO_COMBO_RST_PIN, GPIO_DIR_OUT);

	/* set gpio mode */
#if defined(HWTEST_SUPPORT_PHONE)
	mt_set_gpio_mode(GPIO25, GPIO_MODE_GPIO);
#endif
    ret += mt_set_gpio_mode(GPIO_COMBO_PMU_EN_PIN, GPIO_MODE_GPIO);
    ret += mt_set_gpio_mode(GPIO_COMBO_RST_PIN, GPIO_MODE_GPIO);

    /* SYSRST_B low */
    ret += mt_set_gpio_out(GPIO_COMBO_RST_PIN, GPIO_OUT_ZERO);
    /* PMU_EN low */
#if (HWTEST_SUPPORT_PHONE)
	mt_set_gpio_out(GPIO25, GPIO_OUT_ZERO);
#endif
    ret += mt_set_gpio_out(GPIO_COMBO_PMU_EN_PIN, GPIO_OUT_ZERO);

    msleep(MT6620_OFF_TIME);

    /* PMU_EN high, SYSRST_B low */
#if (HWTEST_SUPPORT_PHONE)
	mt_set_gpio_out(GPIO25, GPIO_OUT_ONE);
#endif
    ret += mt_set_gpio_out(GPIO_COMBO_PMU_EN_PIN, GPIO_OUT_ONE);
    msleep(MT6620_RST_TIME);

    /* SYSRST_B high */
    ret += mt_set_gpio_out(GPIO_COMBO_RST_PIN, GPIO_OUT_ONE);
    msleep(MT6620_STABLE_TIME);
#endif

	//for Ingenic
        rtc_enable_clk32k();
	msleep(MT6620_32K_STABLE_TIME);
	__gpio_as_uart2();
	msleep(30);

	//PMU_EN-->0
    __gpio_enable_pull(GPIO_PMU_PIN); 
	__gpio_as_output(GPIO_PMU_PIN);
	__gpio_clear_pin(GPIO_PMU_PIN);
    //RST-->0
    __gpio_enable_pull(GPIO_RST_PIN);        
	__gpio_as_output(GPIO_RST_PIN);
	__gpio_clear_pin(GPIO_RST_PIN);

    msleep(MT6620_OFF_TIME);
    //PMU_EN-->1
    __gpio_set_pin(GPIO_PMU);
    msleep(MT6620_RST_TIME);

    __gpio_enable_pull(GPIO_RST_PIN);        
	__gpio_as_output(GPIO_RST_PIN);
	__gpio_set_pin(GPIO_RST_PIN);

    msleep(MT6620_STABLE_TIME);
    printk(KERN_INFO "[mt6620] power on \n");

    return 0;
}


int chip_power_off(void)
{
    //int ret = -1;
    printk(KERN_INFO "[mt6620] power off\n");
#if 0
    /* SYSRST_B low */
    mt_set_gpio_out(GPIO_COMBO_RST_PIN, GPIO_OUT_ZERO);
    /* PMU_EN low */
#if (HWTEST_SUPPORT_PHONE)
	mt_set_gpio_out(GPIO25, GPIO_OUT_ZERO);
#endif
    mt_set_gpio_out(GPIO_COMBO_PMU_EN_PIN, GPIO_OUT_ZERO);

#if (HWTEST_SUPPORT_SDIO) && (CONFIG_MTK_COMBO_SDIO_SLOT == 0)
	printk(KERN_INFO "[mt6620] pull down sd0 bus(gpio169~gpio175(exclude gpio174))\n");
    mt_set_gpio_pull_enable(GPIO172, GPIO_PULL_DOWN);	//->CLK
    mt_set_gpio_pull_select(GPIO172, GPIO_PULL_ENABLE);		
    mt_set_gpio_pull_enable(GPIO171, GPIO_PULL_DOWN);	//->CMD
    mt_set_gpio_pull_select(GPIO171, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO175, GPIO_PULL_DOWN);	//->DAT0
    mt_set_gpio_pull_select(GPIO175, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO173, GPIO_PULL_DOWN);	//->DAT1
    mt_set_gpio_pull_select(GPIO173, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO169, GPIO_PULL_DOWN);	//->DAT2
    mt_set_gpio_pull_select(GPIO169, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO170, GPIO_PULL_DOWN);	//->DAT3
    mt_set_gpio_pull_select(GPIO170, GPIO_PULL_ENABLE);
#elif (CONFIG_MTK_COMBO_SDIO_SLOT == 1)
	#error "error:MSDC1 is not reserved for MT6620 on MT6575EVB"
#elif (CONFIG_MTK_COMBO_SDIO_SLOT == 2)
    printk(KERN_INFO "[mt6620] pull down sd2 bus(gpio182~187)\n");
    mt_set_gpio_pull_enable(GPIO182, GPIO_PULL_DOWN);	//->CLK
    mt_set_gpio_pull_select(GPIO182, GPIO_PULL_ENABLE);		
    mt_set_gpio_pull_enable(GPIO184, GPIO_PULL_DOWN);	//->CMD
    mt_set_gpio_pull_select(GPIO184, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO186, GPIO_PULL_DOWN);	//->DAT0
    mt_set_gpio_pull_select(GPIO186, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO187, GPIO_PULL_DOWN);	//->DAT1
    mt_set_gpio_pull_select(GPIO187, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO185, GPIO_PULL_DOWN);	//->DAT2
    mt_set_gpio_pull_select(GPIO185, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO183, GPIO_PULL_DOWN);	//->DAT3
    mt_set_gpio_pull_select(GPIO183, GPIO_PULL_ENABLE);
#elif (CONFIG_MTK_COMBO_SDIO_SLOT == 3)
	printk(KERN_INFO "[mt6620] pull down sd3 bus (GPIO89~GPIO94)\n");
    mt_set_gpio_pull_enable(GPIO92, GPIO_PULL_DOWN);	//->CLK
    mt_set_gpio_pull_select(GPIO92, GPIO_PULL_ENABLE);		
    mt_set_gpio_pull_enable(GPIO91, GPIO_PULL_DOWN);	//->CMD
    mt_set_gpio_pull_select(GPIO91, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO94, GPIO_PULL_DOWN);	//->DAT0
    mt_set_gpio_pull_select(GPIO94, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO90, GPIO_PULL_DOWN);	//->DAT1
    mt_set_gpio_pull_select(GPIO90, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO89, GPIO_PULL_DOWN);	//->DAT2
    mt_set_gpio_pull_select(GPIO89, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_enable(GPIO93, GPIO_PULL_DOWN);	//->DAT3
    mt_set_gpio_pull_select(GPIO93, GPIO_PULL_ENABLE);
#else
	#error "error:unsupported CONFIG_MTK_COMBO_SDIO_SLOT" CONFIG_MTK_COMBO_SDIO_SLOT
#endif


    //printk(KERN_INFO "[mt6620] set UART GPIO Mode output 0\n");
    ret = mt_set_gpio_mode(GPIO_UART_URXD3_PIN, GPIO_UART_URXD3_PIN_M_GPIO);
    ret += mt_set_gpio_dir(GPIO_UART_URXD3_PIN, GPIO_DIR_OUT);
    ret += mt_set_gpio_out(GPIO_UART_URXD3_PIN, GPIO_OUT_ZERO);

    ret += mt_set_gpio_mode(GPIO_UART_UTXD3_PIN, GPIO_UART_UTXD3_PIN_M_GPIO);
    ret += mt_set_gpio_dir(GPIO_UART_UTXD3_PIN, GPIO_DIR_OUT);
    ret += mt_set_gpio_out(GPIO_UART_UTXD3_PIN, GPIO_OUT_ZERO);

#if (HWTEST_SUPPORT_PHONE)
	 mt_set_gpio_out(GPIO_COMBO_6620_LDO_EN_PIN, GPIO_OUT_ZERO);
#endif
#endif
	//for Ingenic
	printk(KERN_INFO "[mt6620] power off\n");
    __gpio_clear_pin(GPIO_RST_PIN);
    __gpio_clear_pin(GPIO_PMU_PIN);
    rtc_disable_clk32k();
    return 0;
}

ssize_t HWTEST_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

ssize_t HWTEST_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	int i = 0;
	char local_buf[256] = {'\0'};
	
	HWTEST_INFO_FUNC("read card by sdio!\n");
	
	if(!g_sdio_init_count){
		HWTEST_INFO_FUNC("sdio card still not initiliaztion yet!\n");
		return -1;
	}
	if(!g_read_card_info_bysdio[0][0]){
		HWTEST_INFO_FUNC("read sdio card buffer is null\n");
		return -2;
	}
	HWTEST_INFO_FUNC("%s,%s",g_read_card_info_bysdio[0],g_read_card_info_bysdio[1]);
	sprintf(local_buf,"%s\n%s",g_read_card_info_bysdio[0],g_read_card_info_bysdio[1]);
	//strncpy(local_buf,g_read_card_info_bysdio[0],strlen(g_read_card_info_bysdio[0]));

	if(copy_to_user(buf,local_buf,strlen(g_read_card_info_bysdio[0]) + 1 + strlen(g_read_card_info_bysdio[1]))){

		HWTEST_INFO_FUNC("copy info to user error!\n");
		return -3;
	}
	return strlen(g_read_card_info_bysdio[0]) + strlen(g_read_card_info_bysdio[1])+1;
}

unsigned int HWTEST_poll(struct file *filp, poll_table *wait)
{
    return 0;
}
//retval:odd is exception,even is error!
int HWTEST_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	int ret;
	
	HWTEST_INFO_FUNC("cmd (0x%04x), arg (%d)\n", cmd, (int)arg);

	switch(cmd) {
		case HWTEST_IOCTL_POWER_ON:
			HWTEST_INFO_FUNC("power on!\n");
			if(g_state_flag){
				HWTEST_INFO_FUNC("already power on!\n");
				retval = 1;
			}else{
				ret = chip_power_on();
				if(!ret){
					g_state_flag = 1;
					HWTEST_INFO_FUNC("power on OK!\n");
				}else{
					HWTEST_INFO_FUNC("power on fail!\n");
					retval =  2;
				}		
			}
			break;
	
		case HWTEST_IOCTL_POWER_OFF: 
			HWTEST_INFO_FUNC("power off!\n");
			if(!g_state_flag){
				HWTEST_INFO_FUNC("already power off!\n");
				retval = 3;
			}else{
				ret = chip_power_off();
				if(!ret){
					g_state_flag = 0;
					HWTEST_INFO_FUNC("power off OK!\n");
				}else{
					HWTEST_INFO_FUNC("power off fail!\n");
					retval = 4;
				}
			}
			break;

		case HWTEST_IOCTL_CHIP_RESET:
			HWTEST_INFO_FUNC("chip reset!\n");
			if(g_state_flag){
				ret = chip_power_off();
				ret += chip_power_on();
				if(!ret){
					HWTEST_INFO_FUNC("chip reset OK!\n");
				}else{
					HWTEST_INFO_FUNC("chip rest fail!\n");
					retval = 6;
				}
			}else{
				HWTEST_INFO_FUNC("chip is power off now!\n");
				retval = 5;
			}
			break;
		case HWTEST_IOCTL_SDIO_INIT:
			HWTEST_INFO_FUNC("init sdio card!\n");
			if(g_sdio_init_count){	
				HWTEST_INFO_FUNC("SDIO card already exist!\n");
				retval = 7;
			}else{
				ret = porting_6620_sdio_card_detect();
				if(!ret){
					HWTEST_INFO_FUNC("hwtest sdio init finish!\n");
				}else{
					HWTEST_INFO_FUNC("hwtest sdio init fail!\n");
					retval = 8;
				}
				g_sdio_init_count = 1;
			}
			break;
		case HWTEST_IOCTL_SDIO_REMOVE:
			HWTEST_INFO_FUNC("remove sdio card!\n");
			if(!g_sdio_init_count){
				HWTEST_INFO_FUNC("sdio card not exist!\n");
				retval = 9;
			}else{
				ret = porting_6620_sdio_card_remove();
				if(!ret){
					HWTEST_INFO_FUNC("hwtest sdio card remove finish!\n");
				}else{
					HWTEST_INFO_FUNC("hwtest sdio card remove fail!\n");
					retval = 10;
				}
				g_sdio_init_count = 0;
			}
			break;
		case HWTEST_IOCTL_SDIO_READ_IOISREADY:
			if(!g_read_current_io_isready_from_sdio_card){
				HWTEST_INFO_FUNC("sdio card current io is not ready!\n");
				retval = 11;
			}else{
				if (copy_to_user((void *)arg, &g_read_current_io_isready_from_sdio_card, sizeof(g_read_current_io_isready_from_sdio_card)) < 0) {
					HWTEST_INFO_FUNC("copy to user space fail!\n");
					retval = 12;
                }
			}
			break;
		case HWTEST_IOCTL_SDIO_READ_BLKSIZE:
			if(!g_read_function_blksize_from_sdio_card){
				HWTEST_INFO_FUNC("sdio card current function blksize is not set!\n");
				retval = 13;
			}else{
				if (copy_to_user((void *)arg, &g_read_function_blksize_from_sdio_card, sizeof(g_read_function_blksize_from_sdio_card)) < 0) {
					HWTEST_INFO_FUNC("copy to user space fail!\n");
					retval = 14;
                }
			}
			break;
		case HWTEST_IOCTL_SDIO_READ_INT:
			if(!g_read_int_from_sdio_card){
				HWTEST_INFO_FUNC("sdio card current register has not valuse!\n");
				retval = 15;
			}else{
				if (copy_to_user((void *)arg, &g_read_int_from_sdio_card, sizeof(g_read_int_from_sdio_card)) < 0) {
					HWTEST_INFO_FUNC("copy to user space fail!\n");
					retval = 16;
                }
			}
			break;
		default:
			retval = -17;
			HWTEST_INFO_FUNC("unknown cmd (%d)\n", cmd);
			break;
	}

    HWTEST_INFO_FUNC("retval = (%d)\n",retval);
	return retval;
}



static int HWTEST_open(struct inode *inode, struct file *file)
{
    HWTEST_INFO_FUNC("major %d minor %d (pid %d)\n",
        imajor(inode),
        iminor(inode),
        current->pid
        );

    if (atomic_inc_return(&HWTEST_ref_cnt) == 1) {
        HWTEST_INFO_FUNC("1st call \n");
    }

    return 0;
}

static int HWTEST_close(struct inode *inode, struct file *file)
{
    HWTEST_INFO_FUNC("major %d minor %d (pid %d)\n",
        imajor(inode),
        iminor(inode),
        current->pid
        );

    if (atomic_dec_return(&HWTEST_ref_cnt) == 0) {
        HWTEST_INFO_FUNC("last call \n");
    }

    return 0;
}


struct file_operations HWTEST_fops = {
    .open = HWTEST_open,
    .release = HWTEST_close,
    .read = HWTEST_read,
    .write = HWTEST_write,
    .unlocked_ioctl = HWTEST_unlocked_ioctl,
    .poll = HWTEST_poll,
};


static int HWTEST_init(void)
{
    dev_t dev = MKDEV(HWTEST_major, 0);
    int cdev_err = -1;
    INT32 ret = -1;
    /* Prepare a char device */
    /*static allocate chrdev*/
    ret = register_chrdev_region(dev, HWTEST_DEV_NUM, HWTEST_DRIVER_NAME);
    if (ret) {
        HWTEST_ERR_FUNC("fail to register chrdev\n");
        return ret;
    }

    cdev_init(&HWTEST_cdev, &HWTEST_fops);
    HWTEST_cdev.owner = THIS_MODULE;

    cdev_err = cdev_add(&HWTEST_cdev, dev, HWTEST_DEV_NUM);
    if (cdev_err) {
        HWTEST_ERR_FUNC("cdev_add() fails (%d) \n", cdev_err);
        goto error;
    }

    HWTEST_INFO_FUNC("driver(major %d) installed \n", HWTEST_major);

    HWTEST_INFO_FUNC("success \n");
    return 0;

error:

    if (cdev_err == 0) {
        cdev_del(&HWTEST_cdev);
    }

    if (ret == 0) {
        unregister_chrdev_region(dev, HWTEST_DEV_NUM);
        HWTEST_major = -1;
    }
	g_state_flag = 0;
    HWTEST_ERR_FUNC("fail \n");

    return -1;
}

static void HWTEST_exit (void)
{
    dev_t dev = MKDEV(HWTEST_major, 0);
    cdev_del(&HWTEST_cdev);
    unregister_chrdev_region(dev, HWTEST_DEV_NUM);
    HWTEST_major = -1;
	g_state_flag = 0;
	g_sdio_init_count = 0;
    HWTEST_INFO_FUNC("done\n");
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL_GPL(g_read_card_info_bysdio);
EXPORT_SYMBOL_GPL(g_read_current_io_isready_from_sdio_card);
EXPORT_SYMBOL_GPL(g_read_function_blksize_from_sdio_card);
EXPORT_SYMBOL_GPL(g_read_int_from_sdio_card);


module_init(HWTEST_init);
module_exit(HWTEST_exit);

