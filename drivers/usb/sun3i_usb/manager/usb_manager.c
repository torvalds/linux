/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: usb_manager.c
*
* Author 		: javen
*
* Description 	: USB π‹¿Ì≥Ã–Ú
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2011-4-14            1.0          create this file
*
*************************************************************************************
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/kthread.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <mach/irqs.h>

#include  "../include/sw_usb_config.h"
#include  "usb_manager.h"
#include  "usbc_platform.h"
#include  "usb_hw_scan.h"
#include  "usb_msg_center.h"


static struct usb_cfg g_usb_cfg;

#ifdef CONFIG_USB_SW_SUN3I_USB0_OTG
static __u32 thread_run_flag = 1;
static __u32 thread_stopped_flag = 1;
#endif

/*
*******************************************************************************
*                     usb_hardware_scan_thread
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
#ifdef CONFIG_USB_SW_SUN3I_USB0_OTG

static int usb_hardware_scan_thread(void * pArg)
{
	struct usb_cfg *cfg = pArg;

	while(thread_run_flag){
		DMSG_DBG_MANAGER("\n\n");

		usb_hw_scan(cfg);
		usb_msg_center(cfg);

		DMSG_DBG_MANAGER("\n\n");

		msleep(1000);  /* 1s */
	}

	thread_stopped_flag = 1;

	return 0;
}

#endif

/*
*******************************************************************************
*                     usb_manager_init
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static __s32 usb_script_parse(struct usb_cfg *cfg)
{
	s32 ret = 0;
	u32 i = 0;
	char *set_usbc = NULL;

	/* usb_global_enable */
	ret = script_parser_fetch(SET_USB_PARA, KEY_USB_GLOBAL_ENABLE, (int *)&(cfg->usb_global_enable), 64);
	if(ret != 0){
		DMSG_PANIC("ERR: get usbc0(usb_global_enable) id failed\n");
	}

	/* usbc_num */
	ret = script_parser_fetch(SET_USB_PARA, KEY_USBC_NUM, (int *)&(cfg->usbc_num), 64);
	if(ret != 0){
		DMSG_PANIC("ERR: get usbc_num failed\n");
	}

	for(i = 0; i < cfg->usbc_num; i++){
		if(i == 0){
			set_usbc = SET_USB0;
		}else if(i == 1){
			set_usbc = SET_USB1;
		}else{
			set_usbc = SET_USB2;
		}

		/* usbc enable */
		ret = script_parser_fetch(set_usbc, KEY_USB_ENABLE, (int *)&(cfg->port[i].enable), 64);
		if(ret != 0){
			DMSG_PANIC("ERR: get usbc(%d) enable failed\n", i);
		}

		/* usbc port type */
		ret = script_parser_fetch(set_usbc, KEY_USB_PORT_TYPE, (int *)&(cfg->port[i].port_type), 64);
		if(ret != 0){
			DMSG_PANIC("ERR: get usbc(%d) port type failed\n", i);
		}

		/* usbc detect type */
		ret = script_parser_fetch(set_usbc, KEY_USB_DETECT_TYPE, (int *)&(cfg->port[i].detect_type), 64);
		if(ret != 0){
			DMSG_PANIC("ERR: get usbc(%d) detect type failed\n", i);
		}

		/* usbc id */
		ret = script_parser_fetch(set_usbc, KEY_USB_ID_GPIO, (int *)&(cfg->port[i].id.gpio_set), 64);
		if(ret != 0){
			DMSG_PANIC("ERR: get usbc(%d) id failed\n", i);
		}

		/* usbc det_vbus */
		ret = script_parser_fetch(set_usbc, KEY_USB_DETVBUS_GPIO, (int *)&(cfg->port[i].det_vbus.gpio_set), 64);
		if(ret != 0){
			DMSG_PANIC("ERR: get usbc(%d) det_vbus failed\n", i);
		}

		/* usbc drv_vbus */
		ret = script_parser_fetch(set_usbc, KEY_USB_DRVVBUS_GPIO, (int *)&(cfg->port[i].drv_vbus.gpio_set), 64);
		if(ret != 0){
			DMSG_PANIC("ERR: get usbc(%d) drv_vbus failed\n", i);
		}
	}

	return 0;
}

/*
*******************************************************************************
*                     modify_usb_borad_info
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void modify_usb_borad_info(struct usb_cfg *cfg)
{
    u32 i = 0;

	for(i = 0; i < cfg->usbc_num; i++){
		if(cfg->port[i].id.gpio_set.port){
			cfg->port[i].id.valid = 1;

			if(cfg->port[i].id.gpio_set.port == 0xffff){
				cfg->port[i].id.group_type = GPIO_GROUP_TYPE_POWER;
			}else{
				cfg->port[i].id.group_type = GPIO_GROUP_TYPE_PIO;
			}
		}

		if(cfg->port[i].det_vbus.gpio_set.port){
			cfg->port[i].det_vbus.valid = 1;

			if(cfg->port[i].det_vbus.gpio_set.port == 0xffff){
				cfg->port[i].det_vbus.group_type = GPIO_GROUP_TYPE_POWER;
			}else{
				cfg->port[i].det_vbus.group_type = GPIO_GROUP_TYPE_PIO;
			}
		}

		if(cfg->port[i].drv_vbus.gpio_set.port){
			cfg->port[i].drv_vbus.valid = 1;

			if(cfg->port[i].drv_vbus.gpio_set.port == 0xffff){
				cfg->port[i].drv_vbus.group_type = GPIO_GROUP_TYPE_POWER;
			}else{
				cfg->port[i].drv_vbus.group_type = GPIO_GROUP_TYPE_PIO;
			}
		}
	}

	return;
}

static void print_gpio_set(user_gpio_set_t *gpio_set)
{
	DMSG_DBG_MANAGER("gpio_name            = %s\n", gpio_set->gpio_name);
	DMSG_DBG_MANAGER("port                 = %x\n", gpio_set->port);
	DMSG_DBG_MANAGER("port_num             = %x\n", gpio_set->port_num);
	DMSG_DBG_MANAGER("mul_sel              = %x\n", gpio_set->mul_sel);
	DMSG_DBG_MANAGER("pull                 = %x\n", gpio_set->pull);
	DMSG_DBG_MANAGER("drv_level            = %x\n", gpio_set->drv_level);
	DMSG_DBG_MANAGER("data                 = %x\n", gpio_set->data);
}

/*
*******************************************************************************
*                     print_usb_cfg
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void print_usb_cfg(struct usb_cfg *cfg)
{
    u32 i = 0;

	DMSG_DBG_MANAGER("\n-----------usb config information--------------\n");

	DMSG_DBG_MANAGER("controller number    = %x\n", (u32)USBC_MAX_CTL_NUM);

	DMSG_DBG_MANAGER("usb_global_enable    = %x\n", cfg->usb_global_enable);
	DMSG_DBG_MANAGER("usbc_num             = %x\n", cfg->usbc_num);

    for(i = 0; i < USBC_MAX_CTL_NUM; i++){
		DMSG_DBG_MANAGER("\n");
		DMSG_DBG_MANAGER("port[%d]:\n", i);
		DMSG_DBG_MANAGER("enable               = %x\n", cfg->port[i].enable);
		DMSG_DBG_MANAGER("port_no              = %x\n", cfg->port[i].port_no);
		DMSG_DBG_MANAGER("port_type            = %x\n", cfg->port[i].port_type);
		DMSG_DBG_MANAGER("detect_type          = %x\n", cfg->port[i].detect_type);

		DMSG_DBG_MANAGER("id.valid             = %x\n", cfg->port[i].id.valid);
		DMSG_DBG_MANAGER("id.group_type        = %x\n", cfg->port[i].id.group_type);
		print_gpio_set(&cfg->port[i].id.gpio_set);

		DMSG_DBG_MANAGER("vbus.valid           = %x\n", cfg->port[i].det_vbus.valid);
		DMSG_DBG_MANAGER("vbus.group_type      = %x\n", cfg->port[i].det_vbus.group_type);
		print_gpio_set(&cfg->port[i].det_vbus.gpio_set);

		DMSG_DBG_MANAGER("drv_vbus.valid       = %x\n", cfg->port[i].drv_vbus.valid);
		DMSG_DBG_MANAGER("drv_vbus.group_type  = %x\n", cfg->port[i].drv_vbus.group_type);
		print_gpio_set(&cfg->port[i].drv_vbus.gpio_set);

		DMSG_DBG_MANAGER("\n");
    }

	DMSG_DBG_MANAGER("-------------------------------------------------\n");
}

/*
*******************************************************************************
*                     get_usb_cfg
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static __s32 get_usb_cfg(struct usb_cfg *cfg)
{
	__s32 ret = 0;

	/* parse script */
	ret = usb_script_parse(cfg);
	if(ret != 0){
		DMSG_PANIC("ERR: usb_script_parse failed\n");
		return -1;
	}

	modify_usb_borad_info(cfg);

	print_usb_cfg(cfg);

	return 0;
}

/*
*******************************************************************************
*                     usb_manager_init
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int __init usb_manager_init(void)
{
	__s32 ret = 0;
	bsp_usbc_t usbc;
	u32 i = 0;

#ifdef CONFIG_USB_SW_SUN3I_USB0_OTG
	struct task_struct *th = NULL;
#endif

    DMSG_DBG_MANAGER("[sw usb]: usb_manager_init\n");

#if defined(CONFIG_USB_SW_SUN3I_USB0_DEVICE_ONLY)
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_DEVICE_ONLY\n");
#elif defined(CONFIG_USB_SW_SUN3I_USB0_HOST_ONLY)
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_HOST_ONLY\n");
#elif defined(CONFIG_USB_SW_SUN3I_USB0_OTG)
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_OTG\n");
#else
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_NULL\n");
	return 0;
#endif

	memset(&g_usb_cfg, 0, sizeof(struct usb_cfg));

	ret = get_usb_cfg(&g_usb_cfg);
	if(ret != 0){
		DMSG_PANIC("ERR: get_usb_cfg failed\n");
		return -1;
	}

    memset(&usbc, 0, sizeof(bsp_usbc_t));
   	for(i = 0; i < USBC_MAX_CTL_NUM; i++){
		usbc.usbc_info[i].num = i;

		switch(i){
            case 0:
                usbc.usbc_info[i].base = SW_VA_USB0_IO_BASE;
            break;

            case 1:
                usbc.usbc_info[i].base = SW_VA_USB1_IO_BASE;
            break;

			case 2:
                usbc.usbc_info[i].base = SW_VA_USB2_IO_BASE;
            break;

            default:
                DMSG_PANIC("ERR: unkown cnt(%d)\n", i);
                usbc.usbc_info[i].base = 0;
        }
	}
	usbc.sram_base = SW_VA_SRAM_IO_BASE;
	USBC_init(&usbc);

    usbc0_platform_device_init();
    usbc1_platform_device_init();
    usbc2_platform_device_init();

#ifdef CONFIG_USB_SW_SUN3I_USB0_OTG
	usb_hw_scan_init(&g_usb_cfg);

	thread_run_flag = 1;
	thread_stopped_flag = 0;
	th = kthread_create(usb_hardware_scan_thread, &g_usb_cfg, "usb-hardware-scan");
	if(IS_ERR(th)){
		DMSG_PANIC("ERR: kthread_create failed\n");
		return -1;
	}

	wake_up_process(th);
#endif

    DMSG_DBG_MANAGER("[sw usb]: usb_manager_init end\n");

    return 0;
}

/*
*******************************************************************************
*                     usb_manager_exit
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void __exit usb_manager_exit(void)
{
	bsp_usbc_t usbc;

    DMSG_DBG_MANAGER("[sw usb]: usb_manager_exit\n");

#if defined(CONFIG_USB_SW_SUN3I_USB0_DEVICE_ONLY)
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_DEVICE_ONLY\n");
#elif defined(CONFIG_USB_SW_SUN3I_USB0_HOST_ONLY)
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_HOST_ONLY\n");
#elif defined(CONFIG_USB_SW_SUN3I_USB0_OTG)
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_OTG\n");
#else
	DMSG_INFO("CONFIG_USB_SW_SUN3I_USB0_NULL\n");
	return;
#endif

    memset(&usbc, 0, sizeof(bsp_usbc_t));
	USBC_exit(&usbc);

#ifdef CONFIG_USB_SW_SUN3I_USB0_OTG
	thread_run_flag = 0;
	while(!thread_stopped_flag){
		DMSG_INFO("waitting for usb_hardware_scan_thread stop\n");
	}

	usb_hw_scan_exit(&g_usb_cfg);
#endif

    usbc0_platform_device_exit();
    usbc1_platform_device_exit();
    usbc2_platform_device_exit();

    return;
}

module_init(usb_manager_init);
module_exit(usb_manager_exit);

