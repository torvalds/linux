/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _HIVE_ISP_CSS_IRQ_TYPES_HRT_H_
#define _HIVE_ISP_CSS_IRQ_TYPES_HRT_H_

/*
 * These are the indices of each interrupt in the interrupt
 * controller's registers. these can be used as the irq_id
 * argument to the hrt functions irq_controller.h.
 *
 * The definitions are taken from <system>_defs.h
 */
typedef enum hrt_isp_css_irq {
  hrt_isp_css_irq_gpio_pin_0           = HIVE_GP_DEV_IRQ_GPIO_PIN_0_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_1           = HIVE_GP_DEV_IRQ_GPIO_PIN_1_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_2           = HIVE_GP_DEV_IRQ_GPIO_PIN_2_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_3           = HIVE_GP_DEV_IRQ_GPIO_PIN_3_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_4           = HIVE_GP_DEV_IRQ_GPIO_PIN_4_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_5           = HIVE_GP_DEV_IRQ_GPIO_PIN_5_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_6           = HIVE_GP_DEV_IRQ_GPIO_PIN_6_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_7           = HIVE_GP_DEV_IRQ_GPIO_PIN_7_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_8           = HIVE_GP_DEV_IRQ_GPIO_PIN_8_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_9           = HIVE_GP_DEV_IRQ_GPIO_PIN_9_BIT_ID          ,               
  hrt_isp_css_irq_gpio_pin_10          = HIVE_GP_DEV_IRQ_GPIO_PIN_10_BIT_ID         ,              
  hrt_isp_css_irq_gpio_pin_11          = HIVE_GP_DEV_IRQ_GPIO_PIN_11_BIT_ID         ,              
  hrt_isp_css_irq_sp                   = HIVE_GP_DEV_IRQ_SP_BIT_ID                  ,                       
  hrt_isp_css_irq_isp                  = HIVE_GP_DEV_IRQ_ISP_BIT_ID                 ,                      
  hrt_isp_css_irq_isys                 = HIVE_GP_DEV_IRQ_ISYS_BIT_ID                ,                     
  hrt_isp_css_irq_isel                 = HIVE_GP_DEV_IRQ_ISEL_BIT_ID                ,                     
  hrt_isp_css_irq_ifmt                 = HIVE_GP_DEV_IRQ_IFMT_BIT_ID                ,                     
  hrt_isp_css_irq_sp_stream_mon        = HIVE_GP_DEV_IRQ_SP_STREAM_MON_BIT_ID       ,            
  hrt_isp_css_irq_isp_stream_mon       = HIVE_GP_DEV_IRQ_ISP_STREAM_MON_BIT_ID      ,           
  hrt_isp_css_irq_mod_stream_mon       = HIVE_GP_DEV_IRQ_MOD_STREAM_MON_BIT_ID      ,
#ifdef _HIVE_ISP_CSS_2401_SYSTEM
  hrt_isp_css_irq_is2401               = HIVE_GP_DEV_IRQ_IS2401_BIT_ID              ,           
#else
  hrt_isp_css_irq_isp_pmem_error       = HIVE_GP_DEV_IRQ_ISP_PMEM_ERROR_BIT_ID      ,           
#endif
  hrt_isp_css_irq_isp_bamem_error      = HIVE_GP_DEV_IRQ_ISP_BAMEM_ERROR_BIT_ID     ,          
  hrt_isp_css_irq_isp_dmem_error       = HIVE_GP_DEV_IRQ_ISP_DMEM_ERROR_BIT_ID      ,           
  hrt_isp_css_irq_sp_icache_mem_error  = HIVE_GP_DEV_IRQ_SP_ICACHE_MEM_ERROR_BIT_ID ,      
  hrt_isp_css_irq_sp_dmem_error        = HIVE_GP_DEV_IRQ_SP_DMEM_ERROR_BIT_ID       ,            
  hrt_isp_css_irq_mmu_cache_mem_error  = HIVE_GP_DEV_IRQ_MMU_CACHE_MEM_ERROR_BIT_ID ,      
  hrt_isp_css_irq_gp_timer_0           = HIVE_GP_DEV_IRQ_GP_TIMER_0_BIT_ID          ,               
  hrt_isp_css_irq_gp_timer_1           = HIVE_GP_DEV_IRQ_GP_TIMER_1_BIT_ID          ,               
  hrt_isp_css_irq_sw_pin_0             = HIVE_GP_DEV_IRQ_SW_PIN_0_BIT_ID            ,                 
  hrt_isp_css_irq_sw_pin_1             = HIVE_GP_DEV_IRQ_SW_PIN_1_BIT_ID            ,                 
  hrt_isp_css_irq_dma                  = HIVE_GP_DEV_IRQ_DMA_BIT_ID                 ,
  hrt_isp_css_irq_sp_stream_mon_b      = HIVE_GP_DEV_IRQ_SP_STREAM_MON_B_BIT_ID     ,
  /* this must (obviously) be the last on in the enum */
  hrt_isp_css_irq_num_irqs
} hrt_isp_css_irq_t;

typedef enum hrt_isp_css_irq_status {
  hrt_isp_css_irq_status_error,
  hrt_isp_css_irq_status_more_irqs,
  hrt_isp_css_irq_status_success
} hrt_isp_css_irq_status_t;

#endif /* _HIVE_ISP_CSS_IRQ_TYPES_HRT_H_ */
