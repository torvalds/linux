/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#ifndef __ARCH_ARM_MACH_BOARD_H
#define __ARCH_ARM_MACH_BOARD_H

#include <generated/autoconf.h>
#include <linux/pm.h>
/* --- chhung */
// #include <mach/mt6575.h>
// #include <board-custom.h>
/* end of chhung */

typedef void (*sdio_irq_handler_t)(void*);  /* external irq handler */
typedef void (*pm_callback_t)(pm_message_t state, void *data);

#define MSDC_CD_PIN_EN      (1 << 0)  /* card detection pin is wired   */
#define MSDC_WP_PIN_EN      (1 << 1)  /* write protection pin is wired */
#define MSDC_RST_PIN_EN     (1 << 2)  /* emmc reset pin is wired       */
#define MSDC_SDIO_IRQ       (1 << 3)  /* use internal sdio irq (bus)   */
#define MSDC_EXT_SDIO_IRQ   (1 << 4)  /* use external sdio irq         */
#define MSDC_REMOVABLE      (1 << 5)  /* removable slot                */
#define MSDC_SYS_SUSPEND    (1 << 6)  /* suspended by system           */
#define MSDC_HIGHSPEED      (1 << 7)  /* high-speed mode support       */
#define MSDC_UHS1           (1 << 8)  /* uhs-1 mode support            */
#define MSDC_DDR            (1 << 9)  /* ddr mode support              */


#define MSDC_SMPL_RISING    (0)
#define MSDC_SMPL_FALLING   (1)

#define MSDC_CMD_PIN        (0)
#define MSDC_DAT_PIN        (1)
#define MSDC_CD_PIN         (2)
#define MSDC_WP_PIN         (3)
#define MSDC_RST_PIN        (4)

enum {
    MSDC_CLKSRC_48MHZ = 0,
//    MSDC_CLKSRC_26MHZ = 0,
//    MSDC_CLKSRC_197MHZ = 1,
//    MSDC_CLKSRC_208MHZ = 2
};

struct msdc_hw {
    unsigned char  clk_src;          /* host clock source */
    unsigned char  cmd_edge;         /* command latch edge */
    unsigned char  data_edge;        /* data latch edge */
    unsigned char  clk_drv;          /* clock pad driving */
    unsigned char  cmd_drv;          /* command pad driving */
    unsigned char  dat_drv;          /* data pad driving */
    unsigned long  flags;            /* hardware capability flags */
    unsigned long  data_pins;        /* data pins */
    unsigned long  data_offset;      /* data address offset */

    /* config gpio pull mode */
    void (*config_gpio_pin)(int type, int pull);

    /* external power control for card */
    void (*ext_power_on)(void);
    void (*ext_power_off)(void);

    /* external sdio irq operations */
    void (*request_sdio_eirq)(sdio_irq_handler_t sdio_irq_handler, void *data);
    void (*enable_sdio_eirq)(void);
    void (*disable_sdio_eirq)(void);

    /* external cd irq operations */
    void (*request_cd_eirq)(sdio_irq_handler_t cd_irq_handler, void *data);
    void (*enable_cd_eirq)(void);
    void (*disable_cd_eirq)(void);
    int  (*get_cd_status)(void);
    
    /* power management callback for external module */
    void (*register_pm)(pm_callback_t pm_cb, void *data);
};

extern struct msdc_hw msdc0_hw;
extern struct msdc_hw msdc1_hw;
extern struct msdc_hw msdc2_hw;
extern struct msdc_hw msdc3_hw;

/*GPS driver*/
#define GPS_FLAG_FORCE_OFF  0x0001
struct mt3326_gps_hardware {
    int (*ext_power_on)(int);
    int (*ext_power_off)(int);
};
extern struct mt3326_gps_hardware mt3326_gps_hw;

/* NAND driver */
struct mt6575_nand_host_hw {
    unsigned int nfi_bus_width;		    /* NFI_BUS_WIDTH */ 
	unsigned int nfi_access_timing;		/* NFI_ACCESS_TIMING */  
	unsigned int nfi_cs_num;			/* NFI_CS_NUM */
	unsigned int nand_sec_size;			/* NAND_SECTOR_SIZE */
	unsigned int nand_sec_shift;		/* NAND_SECTOR_SHIFT */
	unsigned int nand_ecc_size;
	unsigned int nand_ecc_bytes;
	unsigned int nand_ecc_mode;
};
extern struct mt6575_nand_host_hw mt6575_nand_hw;

#endif /* __ARCH_ARM_MACH_BOARD_H */

