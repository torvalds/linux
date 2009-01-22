/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#include "qla_def.h"

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
/* #define QL_DEBUG_LEVEL_15 */ /* Output NPIV trace msgs */
/* #define QL_DEBUG_LEVEL_16 */ /* Output ISP84XX trace msgs */
/* #define QL_DEBUG_LEVEL_17 */ /* Output MULTI-Q trace messages */

/*
* Macros use for debugging the driver.
*/

#define DEBUG(x)	do { if (ql2xextended_error_logging) { x; } } while (0)

#if defined(QL_DEBUG_LEVEL_1)
#define DEBUG1(x)	do {x;} while (0)
#else
#define DEBUG1(x)	do {} while (0)
#endif

#define DEBUG2(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_3(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_3_11(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_9_10(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_11(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_13(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_16(x)	do { if (ql2xextended_error_logging) { x; } } while (0)
#define DEBUG2_17(x) 	do { if (ql2xextended_error_logging) { x; } } while (0)

#if defined(QL_DEBUG_LEVEL_3)
#define DEBUG3(x)	do {x;} while (0)
#define DEBUG3_11(x)	do {x;} while (0)
#else
#define DEBUG3(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_4)
#define DEBUG4(x)	do {x;} while (0)
#else
#define DEBUG4(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_5)
#define DEBUG5(x)          do {x;} while (0)
#else
#define DEBUG5(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_7)
#define DEBUG7(x)          do {x;} while (0)
#else
#define DEBUG7(x)	   do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_9)
#define DEBUG9(x)       do {x;} while (0)
#define DEBUG9_10(x)    do {x;} while (0)
#else
#define DEBUG9(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_10)
#define DEBUG10(x)      do {x;} while (0)
#define DEBUG9_10(x)	do {x;} while (0)
#else
#define DEBUG10(x)	do {} while (0)
  #if !defined(DEBUG9_10)
  #define DEBUG9_10(x)	do {} while (0)
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_11)
#define DEBUG11(x)      do{x;} while(0)
#if !defined(DEBUG3_11)
#define DEBUG3_11(x)    do{x;} while(0)
#endif
#else
#define DEBUG11(x)	do{} while(0)
  #if !defined(QL_DEBUG_LEVEL_3)
  #define DEBUG3_11(x)	do{} while(0)
  #endif
#endif

#if defined(QL_DEBUG_LEVEL_12)
#define DEBUG12(x)      do {x;} while (0)
#else
#define DEBUG12(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_13)
#define DEBUG13(x)      do {x;} while (0)
#else
#define DEBUG13(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_14)
#define DEBUG14(x)      do {x;} while (0)
#else
#define DEBUG14(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_15)
#define DEBUG15(x)      do {x;} while (0)
#else
#define DEBUG15(x)	do {} while (0)
#endif

#if defined(QL_DEBUG_LEVEL_16)
#define DEBUG16(x)	do {x;} while (0)
#else
#define DEBUG16(x)	do {} while (0)
#endif
/*
 * Firmware Dump structure definition
 */

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

struct qla24xx_fw_dump {
	uint32_t host_status;
	uint32_t host_reg[32];
	uint32_t shadow_reg[7];
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
	uint32_t lmc_reg[112];
	uint32_t fpm_hdw_reg[192];
	uint32_t fb_hdw_reg[176];
	uint32_t code_ram[0x2000];
	uint32_t ext_mem[1];
};

struct qla25xx_fw_dump {
	uint32_t host_status;
	uint32_t host_risc_reg[32];
	uint32_t pcie_regs[4];
	uint32_t host_reg[32];
	uint32_t shadow_reg[11];
	uint32_t risc_io_reg;
	uint16_t mailbox_reg[32];
	uint32_t xseq_gp_reg[128];
	uint32_t xseq_0_reg[48];
	uint32_t xseq_1_reg[16];
	uint32_t rseq_gp_reg[128];
	uint32_t rseq_0_reg[32];
	uint32_t rseq_1_reg[16];
	uint32_t rseq_2_reg[16];
	uint32_t aseq_gp_reg[128];
	uint32_t aseq_0_reg[32];
	uint32_t aseq_1_reg[16];
	uint32_t aseq_2_reg[16];
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
	uint32_t lmc_reg[128];
	uint32_t fpm_hdw_reg[192];
	uint32_t fb_hdw_reg[192];
	uint32_t code_ram[0x2000];
	uint32_t ext_mem[1];
};

struct qla81xx_fw_dump {
	uint32_t host_status;
	uint32_t host_risc_reg[32];
	uint32_t pcie_regs[4];
	uint32_t host_reg[32];
	uint32_t shadow_reg[11];
	uint32_t risc_io_reg;
	uint16_t mailbox_reg[32];
	uint32_t xseq_gp_reg[128];
	uint32_t xseq_0_reg[48];
	uint32_t xseq_1_reg[16];
	uint32_t rseq_gp_reg[128];
	uint32_t rseq_0_reg[32];
	uint32_t rseq_1_reg[16];
	uint32_t rseq_2_reg[16];
	uint32_t aseq_gp_reg[128];
	uint32_t aseq_0_reg[32];
	uint32_t aseq_1_reg[16];
	uint32_t aseq_2_reg[16];
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
	uint32_t lmc_reg[128];
	uint32_t fpm_hdw_reg[224];
	uint32_t fb_hdw_reg[208];
	uint32_t code_ram[0x2000];
	uint32_t ext_mem[1];
};

#define EFT_NUM_BUFFERS		4
#define EFT_BYTES_PER_BUFFER	0x4000
#define EFT_SIZE		((EFT_BYTES_PER_BUFFER) * (EFT_NUM_BUFFERS))

#define FCE_NUM_BUFFERS		64
#define FCE_BYTES_PER_BUFFER	0x400
#define FCE_SIZE		((FCE_BYTES_PER_BUFFER) * (FCE_NUM_BUFFERS))
#define fce_calc_size(b)	((FCE_BYTES_PER_BUFFER) * (b))

struct qla2xxx_fce_chain {
	uint32_t type;
	uint32_t chain_size;

	uint32_t size;
	uint32_t addr_l;
	uint32_t addr_h;
	uint32_t eregs[8];
};

struct qla2xxx_mq_chain {
	uint32_t type;
	uint32_t chain_size;

	uint32_t count;
	uint32_t qregs[4 * QLA_MQ_SIZE];
};

#define DUMP_CHAIN_VARIANT	0x80000000
#define DUMP_CHAIN_FCE		0x7FFFFAF0
#define DUMP_CHAIN_MQ		0x7FFFFAF1
#define DUMP_CHAIN_LAST		0x80000000

struct qla2xxx_fw_dump {
	uint8_t signature[4];
	uint32_t version;

	uint32_t fw_major_version;
	uint32_t fw_minor_version;
	uint32_t fw_subminor_version;
	uint32_t fw_attributes;

	uint32_t vendor;
	uint32_t device;
	uint32_t subsystem_vendor;
	uint32_t subsystem_device;

	uint32_t fixed_size;
	uint32_t mem_size;
	uint32_t req_q_size;
	uint32_t rsp_q_size;

	uint32_t eft_size;
	uint32_t eft_addr_l;
	uint32_t eft_addr_h;

	uint32_t header_size;

	union {
		struct qla2100_fw_dump isp21;
		struct qla2300_fw_dump isp23;
		struct qla24xx_fw_dump isp24;
		struct qla25xx_fw_dump isp25;
		struct qla81xx_fw_dump isp81;
	} isp;
};
