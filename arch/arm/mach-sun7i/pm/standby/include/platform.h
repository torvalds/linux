/*
 * platform.h
 *
 *  Created on: 2012-4-25
 *      Author: Benn Huang (benn@allwinnertech.com)
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_


#define AW_SRAM_A1_BASE             0x00000000  /*32KB*/
#define AW_SRAM_A2_BASE             0x00040000  /*32KB*/
#define AW_SRAN_D_BASE              0x00010000  /*4K*/
#define AW_SRAM_B_BASE              0x00020000  /*64KB*/

#define AW_DMA_BASE                 0x01c02000
#define AW_NFC0_BASE                0x01c03000
#define AW_TS_BASE                  0x01c04000
#define AW_NFC1_BASE                0x01c05000
#define AW_LCD0_BASE                0x01c0c000
#define AW_LCD1_BASE                0x01c0d000
#define AW_VE_BASE                  0x01c0e000
#define AW_SDMMC0_BASE              0x01c0f000
#define AW_SDMMC1_BASE              0x01c10000
#define AW_SDMMC2_BASE              0x01c11000
#define AW_SDMMC3_BASE              0x01c12000

#define AW_SS_BASE                  0x01c15000
#define AW_HDMI_BASE                0x01c06000
#define AW_MSGBOX_BASE              0x01c17000
#define AW_SPINLOCK_BASE            0x01c18000
#define AW_TZASC_BASE               0x01c1e000
#define AW_CCM_BASE                 0x01c20000
#define AW_PIO_BASE                 0x01c20800
#define AW_TIMER_BASE               0x01c20c00
#define AW_SPDIF_BASE               0x01c21000
#define AW_PWM_BASE                 0x01c21400

#define AW_UART0_BASE               0x01c28000
#define AW_UART1_BASE               0x01c28400
#define AW_UART2_BASE               0x01c28800

#define AW_GMAC_BASE                0x01c30000
#define AW_GPU_BASE                 0x01c40000
#define AW_HSTMR_BASE               0x01c60000
#define AW_DRAMCOM_BASE             0x01c62000
#define AW_DRAMCTL0_BASE            0x01c63000
#define AW_DRAMCTL1_BASE            0x01c64000
#define AW_DRAMPHY0_BASE            0x01c65000
#define AW_DRAMPHY1_BASE            0x01c66000

#define AW_SCU_BASE                 0x01c80000
#define AW_GIC_BASE                 0x01c80100


/* The following register cannot access by cpu0 */
#define AW_G_TIMER_BASE             0x01c80200
#define AW_L_TIMER_BASE             0x01c80600
#define AW_INT_DIST_BASE            0x01c81000

#define AW_CPUCFG_BASE            	0x01c25C00

#define AW_DRAM_BASE                0x40000000
#define AW_BROM_BASE                0xffff0000    /*32KB*/


#endif /* PLATFORM_H_ */
