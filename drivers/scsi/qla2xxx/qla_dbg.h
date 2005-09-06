/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2005 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

/*
 * Driver debug definitions.
 */
/* #define QL_DEBUG_LEVEL_1  */ /* Output register accesses to COM1 */
/* #define QL_DEBUG_LEVEL_2  */ /* Output error msgs to COM1 */
/* #define QL_DEBUG_LEVEL_3  */ /* Output function trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_4  */ /* Output NVRAM trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_5  */ /* Output ring trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_6  */ /* Output WATCHDOG timer trace to COM1 */
/* #define QL_DEBUG_LEVEL_7  */ /* Output RISC load trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_8  */ /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_9  */ /* Output IOCTL trace msgs */
/* #define QL_DEBUG_LEVEL_10 */ /* Output IOCTL error msgs */
/* #define QL_DEBUG_LEVEL_11 */ /* Output Mbx Cmd trace msgs */
/* #define QL_DEBUG_LEVEL_12 */ /* Output IP trace msgs */
/* #define QL_DEBUG_LEVEL_13 */ /* Output fdmi function trace msgs */
/* #define QL_DEBUG_LEVEL_14 */ /* Output RSCN trace msgs */
/*
 *  Local Macro Definitions.
 */
#if defined(QL_DEBUG_LEVEL_1)  || defined(QL_DEBUG_LEVEL_2) || \
    defined(QL_DEBUG_LEVEL_3)  || defined(QL_DEBUG_LEVEL_4) || \
    defined(QL_DEBUG_LEVEL_5)  || defined(QL_DEBUG_LEVEL_6) || \
    defined(QL_DEBUG_LEVEL_7)  || defined(QL_DEBUG_LEVEL_8) || \
    defined(QL_DEBUG_LEVEL_9)  || defined(QL_DEBUG_LEVEL_10) || \
    defined(QL_DEBUG_LEVEL_11) || defined(QL_DEBUG_LEVEL_12) || \
    defined(QL_DEBUG_LEVEL_13) || defined(QL_DEBUG_LEVEL_14)
    #define QL_DEBUG_ROUTINES
#endif

/*
* Macros use for debugging the driver.
*/
#undef ENTER_TRACE
#if defined(ENTER_TRACE)
#define ENTER(x)	do { printk("qla2100 : Entering %s()\n", x); } while (0)
#define LEAVE(x)	do { printk("qla2100 : Leaving %s()\n", x);  } while (0)
#define ENTER_INTR(x)	do { printk("qla2100 : Entering %s()\n", x); } while (0)
#define LEAVE_INTR(x)	do { printk("qla2100 : Leaving %s()\n", x);  } while (0)
#else
#define ENTER(x)	do {} while (0)
#define LEAVE(x)	do {} while (0)
#define ENTER_INTR(x) 	do {} while (0)
#define LEAVE_INTR(x)   do {} while (0)
#endif

#if  DEBUG_QLA2100
#define DEBUG(x)	do {x;} while (0);
#else
#define DEBUG(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_1)
#define DEBUG1(x)	do {x;} while (0);
#else
#define DEBUG1(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_2)
#define DEBUG2(x)       do {x;} while (0);
#define DEBUG2_3(x)     do {x;} while (0);
#define DEBUG2_3_11(x)  do {x;} while (0);
#define DEBUG2_9_10(x)    do {x;} while (0);
#define DEBUG2_11(x)    do {x;} while (0);
#define DEBUG2_13(x)    do {x;} while (0);
#else
#define DEBUG2(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_3)
#define DEBUG3(x)	do {x;} while (0);
#define DEBUG2_3(x)	do {x;} while (0);
#define DEBUG2_3_11(x)	do {x;} while (0);
#define DEBUG3_11(x)	do {x;} while (0);
#else
#define DEBUG3(x)	do {} while (0);
  #if !defined(QL_DEBUG_LEVEL_2)
  #define DEBUG2_3(x)	do {} while (0);
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_4)
#define DEBUG4(x)	do {x;} while (0);
#else
#define DEBUG4(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_5)
#define DEBUG5(x)          do {x;} while (0);
#else
#define DEBUG5(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_7)
#define DEBUG7(x)          do {x;} while (0);
#else
#define DEBUG7(x)	   do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_9)
#define DEBUG9(x)       do {x;} while (0);
#define DEBUG9_10(x)    do {x;} while (0);
#define DEBUG2_9_10(x)	do {x;} while (0);
#else
#define DEBUG9(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_10)
#define DEBUG10(x)      do {x;} while (0);
#define DEBUG2_9_10(x)	do {x;} while (0);
#define DEBUG9_10(x)	do {x;} while (0);
#else
#define DEBUG10(x)	do {} while (0);
  #if !defined(DEBUG2_9_10)
  #define DEBUG2_9_10(x)	do {} while (0);
  #endif
  #if !defined(DEBUG9_10)
  #define DEBUG9_10(x)	do {} while (0);
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_11)
#define DEBUG11(x)      do{x;} while(0);
#if !defined(DEBUG2_11)
#define DEBUG2_11(x)    do{x;} while(0);
#endif
#if !defined(DEBUG2_3_11)
#define DEBUG2_3_11(x)  do{x;} while(0);
#endif
#if !defined(DEBUG3_11)
#define DEBUG3_11(x)    do{x;} while(0);
#endif
#else
#define DEBUG11(x)	do{} while(0);
  #if !defined(QL_DEBUG_LEVEL_2)
  #define DEBUG2_11(x)	do{} while(0);
    #if !defined(QL_DEBUG_LEVEL_3)
    #define DEBUG2_3_11(x) do{} while(0);
    #endif
  #endif
  #if !defined(QL_DEBUG_LEVEL_3)
  #define DEBUG3_11(x)	do{} while(0);
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_12)
#define DEBUG12(x)      do {x;} while (0);
#else
#define DEBUG12(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_13)
#define DEBUG13(x)      do {x;} while (0)
#if !defined(DEBUG2_13)
#define DEBUG2_13(x)    do {x;} while(0)
#endif
#else
#define DEBUG13(x)	do {} while (0)
#if !defined(QL_DEBUG_LEVEL_2)
#define DEBUG2_13(x)	do {} while(0)
#endif
#endif

#if defined(QL_DEBUG_LEVEL_14)
#define DEBUG14(x)      do {x;} while (0)
#else
#define DEBUG14(x)	do {} while (0)
#endif

/*
 * Firmware Dump structure definition
 */
#define FW_DUMP_SIZE_128K	0xBC000
#define FW_DUMP_SIZE_512K	0x2FC000
#define FW_DUMP_SIZE_1M		0x5FC000

struct qla2300_fw_dump {
	uint16_t hccr;
	uint16_t pbiu_reg[8];
	uint16_t risc_host_reg[8];
	uint16_t mailbox_reg[32];
	uint16_t resp_dma_reg[32];
	uint16_t dma_reg[48];
	uint16_t risc_hdw_reg[16];
	uint16_t risc_gp0_reg[16];
	uint16_t risc_gp1_reg[16];
	uint16_t risc_gp2_reg[16];
	uint16_t risc_gp3_reg[16];
	uint16_t risc_gp4_reg[16];
	uint16_t risc_gp5_reg[16];
	uint16_t risc_gp6_reg[16];
	uint16_t risc_gp7_reg[16];
	uint16_t frame_buf_hdw_reg[64];
	uint16_t fpm_b0_reg[64];
	uint16_t fpm_b1_reg[64];
	uint16_t risc_ram[0xf800];
	uint16_t stack_ram[0x1000];
	uint16_t data_ram[1];
};

struct qla2100_fw_dump {
	uint16_t hccr;
	uint16_t pbiu_reg[8];
	uint16_t mailbox_reg[32];
	uint16_t dma_reg[48];
	uint16_t risc_hdw_reg[16];
	uint16_t risc_gp0_reg[16];
	uint16_t risc_gp1_reg[16];
	uint16_t risc_gp2_reg[16];
	uint16_t risc_gp3_reg[16];
	uint16_t risc_gp4_reg[16];
	uint16_t risc_gp5_reg[16];
	uint16_t risc_gp6_reg[16];
	uint16_t risc_gp7_reg[16];
	uint16_t frame_buf_hdw_reg[16];
	uint16_t fpm_b0_reg[64];
	uint16_t fpm_b1_reg[64];
	uint16_t risc_ram[0xf000];
};

#define FW_DUMP_SIZE_24XX	0x2B0000

struct qla24xx_fw_dump {
	uint32_t hccr;
	uint32_t host_reg[32];
	uint16_t mailbox_reg[32];
	uint32_t xseq_gp_reg[128];
	uint32_t xseq_0_reg[16];
	uint32_t xseq_1_reg[16];
	uint32_t rseq_gp_reg[128];
	uint32_t rseq_0_reg[16];
	uint32_t rseq_1_reg[16];
	uint32_t rseq_2_reg[16];
	uint32_t cmd_dma_reg[16];
	uint32_t req0_dma_reg[15];
	uint32_t resp0_dma_reg[15];
	uint32_t req1_dma_reg[15];
	uint32_t xmt0_dma_reg[32];
	uint32_t xmt1_dma_reg[32];
	uint32_t xmt2_dma_reg[32];
	uint32_t xmt3_dma_reg[32];
	uint32_t xmt4_dma_reg[32];
	uint32_t xmt_data_dma_reg[16];
	uint32_t rcvt0_data_dma_reg[32];
	uint32_t rcvt1_data_dma_reg[32];
	uint32_t risc_gp_reg[128];
	uint32_t shadow_reg[7];
	uint32_t lmc_reg[112];
	uint32_t fpm_hdw_reg[192];
	uint32_t fb_hdw_reg[176];
	uint32_t code_ram[0x2000];
	uint32_t ext_mem[1];
};
