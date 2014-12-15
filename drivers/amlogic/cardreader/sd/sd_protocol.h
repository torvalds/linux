#ifndef _H_SD_PROTOCOL
#define _H_SD_PROTOCOL

#include <linux/slab.h>
#include <linux/cardreader/cardreader.h>
#include <linux/cardreader/card_block.h>
#include <linux/cardreader/sdio_hw.h>

#include <mach/am_regs.h>
#include <mach/irqs.h>
#include <mach/card_io.h>

typedef enum _SDIO_CMD_ERR {
	SDIO_NONE_ERR,
	SDIO_PACK_CRC_ERR,
	SDIO_PACK_TIMEOUT_ERR,
	SDIO_RES_CRC_ERR,
	SDIO_RES_TIMEOUT_ERR
} SDIO_CMD_ERR;

//Never change any sequence of following data variables
#pragma pack(1)


/**********************
	SDXC regs (9)
***********************/

#define SDHC_BASE 0xC1108E00

#define SDHC_ARGU 0x2380
#define SDHC_SEND 0x2381
#define SDHC_CTRL 0x2382
#define SDHC_STAT 0x2383
#define SDHC_CLKC 0x2384
#define SDHC_ADDR 0x2385
#define SDHC_PDMA 0x2386
#define SDHC_MISC 0x2387
#define SDHC_DATA 0x2388
#define SDHC_ICTL 0x2389
#define SDHC_ISTA 0x238A
#define SDHC_SRST 0x238B

extern int using_sdxc_controller;

// 0 : SDHC_ARGU
typedef struct SDXC_Arg_Reg {
	unsigned long command;
} SDXC_Arg_Reg_t;

// 1 : SDHC_SEND
typedef struct SDXC_Send_Reg {
	unsigned command_index : 6;
	unsigned command_has_resp : 1;
	unsigned command_has_data : 1;
	unsigned response_length : 1;
	unsigned response_no_crc : 1;
	unsigned data_direction : 1;
	unsigned data_stop : 1;
	unsigned total_pack : 20;
} SDXC_Send_Reg_t;

/*
//SDHC_CTRL
typedef struct SDXC_Ctrl_Reg {
	unsigned dat_width : 2;	//dat_type
	unsigned ddr_mode : 1;
	unsigned sdxc_soft_reset : 1;
	unsigned pack_len : 9;		//0:512, 1:1, ..., 511:511
	unsigned rx_timeout : 7;
	
	////unsigned sdxc_irq_en : 12;		//detail
	unsigned dma_done_int_en 	: 1;
	unsigned sdio_dat1_int_en 	: 1;		//@@
	unsigned rx_fifo_int_en 	: 1;
	unsigned tx_fifo_int_en 	: 1;
	
	unsigned data_complete_int_en 	: 1;	//@@
	unsigned pack_crc_err_int_en 	: 1;
	unsigned pack_timeout_int_en 	: 1;
	unsigned pack_complete_int_en 	: 1;
	
	unsigned dat0_ready_int_en 		: 1;
	
	unsigned res_crc_err_int_en 	: 1;
	unsigned res_timeout_int_en 	: 1;
	unsigned res_ok_int_en 			: 1;	//@@
} SDXC_Ctrl_Reg_t;
*/
// 2 : SDHC_CTRL
typedef struct SDXC_Ctrl_Reg {
	unsigned dat_width : 2;	//dat_type
	unsigned ddr_mode : 1;
	unsigned reserved1 : 1;
	unsigned pack_len : 9;		//0:512, 1:1, ..., 511:511
	unsigned rx_timeout : 7;
	unsigned rc_period : 4;
	unsigned endian : 2;		
	unsigned reserved2 : 6;		
} SDXC_Ctrl_Reg_t;

/*
//SDHC_STAT
typedef struct SDXC_Status_Reg {
	unsigned busy : 1;
	unsigned dat_state : 4;
	unsigned cmd_state : 1;
	unsigned request_rx : 1;	//???	set when rx_count >= rx_threshold
	unsigned request_tx : 1;	//???	set when tx_count <= tx_threshold
	unsigned tx_count : 6;
	unsigned rx_count : 6;
	
	////unsigned sdxc_irq_state : 12;	//detail
	unsigned dma_done_int 		: 1;
	unsigned sdio_dat1_int 		: 1;		//@@
	unsigned rx_fifo_int 		: 1;
	unsigned tx_fifo_int 		: 1;
	
	unsigned data_complete_int 	: 1;		//@@
	unsigned pack_crc_err_int 	: 1;
	unsigned pack_timeout_int 	: 1;
	unsigned pack_complete_int 	: 1;
	
	unsigned dat0_ready_int 	: 1;
	
	unsigned res_crc_err_int 	: 1;
	unsigned res_timeout_int 	: 1;
	unsigned res_ok_int 		: 1;		//@@
} SDXC_Status_Reg_t;
*/
// 3 : SDHC_STAT
typedef struct SDXC_Status_Reg {
	unsigned busy : 1;
	unsigned dat_3_0 : 4;
	unsigned cmd_state : 1;
	unsigned reserved1 : 1;	
	unsigned reserved2 : 1;	
	unsigned rx_count : 6;
	unsigned tx_count : 6;
	unsigned dat_7_4 : 4;
	
	unsigned reserved3 : 8;	
} SDXC_Status_Reg_t;

/*
//SDHC_CLKC
typedef struct SDXC_Clk_Reg {
	unsigned clk_div : 16;
	unsigned clk_in_sel : 3;
	unsigned clk_en : 1;
	unsigned phase_sel : 2;
	unsigned reserved1 : 1;
	unsigned clk_ctl_en : 1;	//???  write 0,  change, write 1
	unsigned sdio_dat1_irq2_en : 1;
	unsigned sdio_dat1_irq2_delay : 2;
	unsigned reserved2 : 5;
} SDXC_Clk_Reg_t;
*/
// 4 : SDHC_CLKC
typedef struct SDXC_Clk_Reg {
	unsigned clk_div : 16;
	unsigned clk_in_sel : 3;
	unsigned clk_en : 1;
	unsigned rx_clk_phase_sel : 2;
	unsigned rx_clk_feedback_en : 1;
	unsigned clk_ctl_en : 1;	//???  write 0,  change, write 1
	unsigned clk_jic_control : 1;	//???
	unsigned reserved1 : 7;
} SDXC_Clk_Reg_t;

// 5 : SDHC_ADDR
typedef struct SDXC_Addr_Reg {
	unsigned long dma_addr;
} SDXC_Addr_Reg_t;

// 6 : SDHC_PDMA
typedef struct SDXC_PDMA_Reg {
	unsigned dma_mode : 1;		//0:PIO, 1:DMA
	unsigned pio_rd_resp : 3;	//0:[39:8],  1:1st long,  2:2nd long, ...,  6 or 7:cmd arg
	unsigned dma_urgent : 1;
	unsigned wr_burst : 5;
	unsigned rd_burst : 5;
	unsigned rx_threshold : 6;
	unsigned tx_threshold : 6;
	unsigned rx_manual_flush : 1;	//???
	unsigned reserved1 : 4;
} SDXC_PDMA_Reg_t;

/*
//SDHC_MISC
typedef struct SDXC_Misc_Reg {
	unsigned cmd_line_delay : 2;	//for tuning
	unsigned dat_line_delay : 2;	//for tuning
	unsigned manual_stop : 1;
	unsigned ext_ctrl : 1;
	unsigned burst_num : 6;
	unsigned thread_id : 6;
	unsigned pio_rd_flag : 1;
	unsigned pio_rd_mode : 1;		//0:one time mode,  1:burst mode
	unsigned rx_limit : 6;
	unsigned tx_limit : 6;
} SDXC_Misc_Reg_t;
*/
// 7 : SDHC_MISC
typedef struct SDXC_Misc_Reg {
	unsigned cmd_line_delay : 2;	//for tuning
	unsigned dat_line_delay : 2;	//for tuning
	unsigned rx_full_threshold : 6;		//default : 30
	unsigned tx_empty_threshold : 6;	//default : 0
	unsigned burst_num : 6;
	unsigned thread_id : 6;
	unsigned manual_stop : 1;
	unsigned reserved1 : 3;
} SDXC_Misc_Reg_t;

// 8 : SDHC_DATA
typedef struct SDXC_Data_Reg {
	unsigned long value;
} SDXC_Data_Reg_t;

// 9 : SDHC_ICTL
typedef struct SDXC_Ictl_Reg {	//interrupt control
	unsigned res_ok_int_en 			: 1;	//@@
	unsigned res_timeout_int_en 	: 1;
	unsigned res_crc_err_int_en 	: 1;
	unsigned dat0_ready_int_en 		: 1;

	unsigned pack_complete_int_en 	: 1;
	unsigned pack_timeout_int_en 	: 1;
	unsigned pack_crc_err_int_en 	: 1;
	unsigned data_complete_int_en 	: 1;	//@@

	unsigned rx_fifo_int_en 	: 1;
	unsigned tx_fifo_int_en 	: 1;
	unsigned sdio_dat1_int_en 	: 1;		//@@
	unsigned dma_done_int_en 	: 1;

	unsigned rx_fifo_full_int_en 			: 1;
	unsigned tx_fifo_empty_int_en 			: 1;
	unsigned additional_sdio_dat1_int_en 	: 1;		//???

	unsigned reserved1 : 1;	
	unsigned sdio_dat1_mask_delay : 2;	
	unsigned reserved2 : 14;
} SDXC_Ictl_Reg_t;

// A : SDHC_ISTA
typedef struct SDXC_Ista_Reg {	//interrupt status
	unsigned res_ok_int 		: 1;		//@@
	unsigned res_timeout_int 	: 1;
	unsigned res_crc_err_int 	: 1;
	unsigned dat0_ready_int 	: 1;

	unsigned pack_complete_int 	: 1;
	unsigned pack_timeout_int 	: 1;
	unsigned pack_crc_err_int 	: 1;
	unsigned data_complete_int 	: 1;		//@@

	unsigned rx_fifo_int 		: 1;
	unsigned tx_fifo_int 		: 1;
	unsigned sdio_dat1_int 		: 1;		//@@
	unsigned dma_done_int 		: 1;

	unsigned rx_fifo_full_int 	: 1;
	unsigned tx_fifo_empty_int 	: 1;
	unsigned additional_sdio_dat1_int 	: 1;		//???

	unsigned reserved1 : 17;
} SDXC_Ista_Reg_t;

// B : SDHC_SRST
typedef struct SDXC_Srst_Reg {	//soft reset
	unsigned main_ctrl_srst : 1;
	unsigned rx_fifo_srst : 1;
	unsigned tx_fifo_srst : 1;
	unsigned rx_dphy_srst : 1;	//manual clear
	unsigned tx_dphy_srst : 1;	//manual clear
	unsigned dma_srst : 1;
	unsigned reserved1 : 26;
} SDXC_Srst_Reg_t;


typedef struct _SD_REG_SSR {
	unsigned Reserved1_1 : 5;
	unsigned SECURE_MODE : 1;
	unsigned DAT_BUS_WIDTH : 2;
	
	unsigned Reserved2 : 6;
	unsigned Reserved1_2 : 2;
	
	unsigned short SD_CARD_TYPE;			// 16
	
	unsigned int SIZE_OF_PROTECTED_AREA;	// 32
	unsigned char SPEED_CLASS;				//8
	unsigned char PERPORMANCE_MOVE;			//8
	
	unsigned Reserved3 : 4;
	unsigned AU_SIZE : 4;
	
	unsigned short ERASE_SIZE;				// 16

	unsigned ERASE_OFFSET : 2;
	unsigned ERASE_TIMEOUT : 6;
	
	unsigned char Reserved4[11];			// 88
	unsigned char Reserved5[39];			// 312
} SD_REG_SSR_t;
    
//MSB->LSB, structure for Operation Conditions Register
typedef struct _SD_REG_OCR {

	//unsigned Reserved0:6;	
	unsigned S18A : 1;
	unsigned Reserved0 : 5;
	unsigned Card_Capacity_Status:1;	//Card_High_capacity
	unsigned Card_Busy:1;	//Card power up status bit (busy)

	unsigned VDD_28_29:1;	/* VDD voltage 2.8 ~ 2.9 */
	unsigned VDD_29_30:1;	/* VDD voltage 2.9 ~ 3.0 */
	unsigned VDD_30_31:1;	/* VDD voltage 3.0 ~ 3.1 */
	unsigned VDD_31_32:1;	/* VDD voltage 3.1 ~ 3.2 */	
	unsigned VDD_32_33:1;	/* VDD voltage 3.2 ~ 3.3 */	
	unsigned VDD_33_34:1;	/* VDD voltage 3.3 ~ 3.4 */	
	unsigned VDD_34_35:1;	/* VDD voltage 3.4 ~ 3.5 */	
	unsigned VDD_35_36:1;	/* VDD voltage 3.5 ~ 3.6 */	

	unsigned VDD_20_21:1;	/* VDD voltage 2.0 ~ 2.1 */	
	unsigned VDD_21_22:1;	/* VDD voltage 2.1 ~ 2.2 */	
	unsigned VDD_22_23:1;	/* VDD voltage 2.2 ~ 2.3 */	
	unsigned VDD_23_24:1;	/* VDD voltage 2.3 ~ 2.4 */
	unsigned VDD_24_25:1;	/* VDD voltage 2.4 ~ 2.5 */	
	unsigned VDD_25_26:1;	/* VDD voltage 2.5 ~ 2.6 */	
	unsigned VDD_26_27:1;	/* VDD voltage 2.6 ~ 2.7 */	
	unsigned VDD_27_28:1;	/* VDD voltage 2.7 ~ 2.8 */

	unsigned Reserved4:1;	/* MMC VDD voltage 1.45 ~ 1.50 */
	unsigned Reserved3:1;	/* MMC VDD voltage 1.50 ~ 1.55 */	
	unsigned Reserved2:1;	/* MMC VDD voltage 1.55 ~ 1.60 */	
	unsigned Reserved1:1;	/* MMC VDD voltage 1.60 ~ 1.65 */	
	unsigned VDD_16_17:1;	/* VDD voltage 1.6 ~ 1.7 */	
	unsigned VDD_17_18:1;	/* VDD voltage 1.7 ~ 1.8 */	
	unsigned VDD_18_19:1;	/* VDD voltage 1.8 ~ 1.9 */	
	unsigned VDD_19_20:1;	/* VDD voltage 1.9 ~ 2.0 */
} SD_REG_OCR_t;


typedef struct _SDIO_REG_OCR {

	unsigned VDD_28_29:1;	/* VDD voltage 2.8 ~ 2.9 */
	unsigned VDD_29_30:1;	/* VDD voltage 2.9 ~ 3.0 */
	unsigned VDD_30_31:1;	/* VDD voltage 3.0 ~ 3.1 */	
	unsigned VDD_31_32:1;	/* VDD voltage 3.1 ~ 3.2 */
	unsigned VDD_32_33:1;	/* VDD voltage 3.2 ~ 3.3 */	
	unsigned VDD_33_34:1;	/* VDD voltage 3.3 ~ 3.4 */	
	unsigned VDD_34_35:1;	/* VDD voltage 3.4 ~ 3.5 */	
	unsigned VDD_35_36:1;	/* VDD voltage 3.5 ~ 3.6 */
	
	unsigned VDD_20_21:1;	/* VDD voltage 2.0 ~ 2.1 */
	unsigned VDD_21_22:1;	/* VDD voltage 2.1 ~ 2.2 */
	unsigned VDD_22_23:1;	/* VDD voltage 2.2 ~ 2.3 */
	unsigned VDD_23_24:1;	/* VDD voltage 2.3 ~ 2.4 */
	unsigned VDD_24_25:1;	/* VDD voltage 2.4 ~ 2.5 */
	unsigned VDD_25_26:1;	/* VDD voltage 2.5 ~ 2.6 */
	unsigned VDD_26_27:1;	/* VDD voltage 2.6 ~ 2.7 */
	unsigned VDD_27_28:1;	/* VDD voltage 2.7 ~ 2.8 */	

	unsigned Reserved:8;	/* reserved */
} SDIO_REG_OCR_t;

//MSB->LSB, structure for Card_Identification Register
typedef struct _SD_REG_CID {

	unsigned char MID;	//Manufacturer ID
	char OID[2];		//OEM/Application ID	
	char PNM[5];		//Product Name
	unsigned char PRV;	//Product Revision
	unsigned long PSN;	//Serial Number
	unsigned MDT_high:4;	//Manufacture Date Code
	unsigned Reserved:4;	
	unsigned MDT_low:8;	// MDT = (MDT_high << 8) | MDT_low
	unsigned NotUsed:1;
	unsigned CRC:7;	//CRC7 checksum
} SD_REG_CID_t;

//MSB->LSB, structure for Card-Specific Data Register
typedef struct _SD_REG_CSD {

	unsigned Reserved1:2;
	unsigned MMC_SPEC_VERS:4;	//MMC Spec_vers
	unsigned CSD_STRUCTURE:2;	//CSD structure	

	unsigned TAAC:8;	//data read access-time-1
	unsigned NSAC:8;	//data read access-time-2 in CLK cycles(NSAC*100)
	unsigned TRAN_SPEED:8;	//max. data transfer rate
	unsigned CCC_high:8;	//card command classes	

	unsigned READ_BL_LEN:4;	//max. read data block length
	unsigned CCC_low:4;	// CCC = (CCC_high << 4) | CCC_low

	unsigned C_SIZE_high:2;	//device size
	unsigned Reserved2:2;
	unsigned DSR_IMP:1;	//DSR implemented
	unsigned READ_BLK_MISALIGN:1;	//read block misalignment
	unsigned WRITE_BLK_MISALIGN:1;	//write block misalignment
	unsigned READ_BL_PARTIAL:1;	//partial blocks for read allowed
	
	unsigned C_SIZE_mid:8;

	unsigned VDD_R_CURR_MAX:3;	//max. read current @VDD max
	unsigned VDD_R_CURR_MIN:3;	//max. read current @VDD min
	unsigned C_SIZE_low:2;	// C_SIZE = (C_SIZE_high << 10) | (C_SIZE_mid << 2) | C_SIZE_low

	unsigned C_SIZE_MULT_high:2;	
	unsigned VDD_W_CURR_MAX:3;	//max. write current @VDD max
	unsigned VDD_W_CURR_MIN:3;	//max. write current @VDD min
	
	unsigned SECTOR_SIZE_high:6;	//erase sector size
	unsigned ERASE_BLK_EN:1;	//erase single block enable
	unsigned C_SIZE_MULT_low:1;	// C_SIZE_MULT = (C_SIZE_MULT_high << 1) | C_SIZE_MULT_low
	
	unsigned WP_GRP_SIZE:7;	//write protect group size
	unsigned SECTOR_SIZE_low:1;	// SECTOR_SIZE = (SECTOR_SIZE_high << 1) | SECTOR_SIZE_low
	
	unsigned WRITE_BL_LEN_high:2;	//max. write data block length
	unsigned R2W_FACTOR:3;	//write speed factor
	unsigned Reserved3:2;
	
	unsigned WP_GRP_ENABLE:1;	//write protect group enable
	unsigned Reserved4:5;	
	unsigned WRITE_BL_PARTIAL:1;	//partial blocks for write allowed
	unsigned WRITE_BL_LEN_low:2;	//WRITE_BL_LEN = (WRITE_BL_LEN_high << 2) | WRITE_BL_LEN_low
	
	unsigned Reserved5:2;
	unsigned FILE_FORMAT:2;	//File format
	unsigned TMP_WRITE_PROTECT:1;	//temporary write protection
	unsigned PERM_WRITE_PROTECT:1;	//permanent write protection
	unsigned COPY:1;	//copy flag (OTP)
	unsigned FILE_FORMAT_GRP:1;	//File format group
	
	unsigned NotUsed:1;	
	unsigned CRC:7;	//CRC checksum
} SD_REG_CSD_t;


typedef struct _SDHC_REG_CSD {
	
	unsigned Reserved1:6;
	unsigned CSD_STRUCTURE:2;	//CSD structure
	
	unsigned TAAC:8;	//data read access-time-1
	unsigned NSAC:8;	//data read access-time-2 in CLK cycles(NSAC*100)
	unsigned TRAN_SPEED:8;	//max. data transfer rate
	
	unsigned CCC_high:8;	//card command classes
	unsigned READ_BL_LEN:4;	//max. read data block length
	unsigned CCC_low:4;	// CCC = (CCC_high << 4) | CCC_low
	
	unsigned Reserved2:4;
	unsigned DSR_IMP:1;	//DSR implemented
	unsigned READ_BLK_MISALIGN:1;	//read block misalignment
	unsigned WRITE_BLK_MISALIGN:1;	//write block misalignment
	unsigned READ_BL_PARTIAL:1;	//partial blocks for read allowed
	
	unsigned C_SIZE_high:6;	//device size       
	unsigned Reserved3:2;

	unsigned C_SIZE_mid:8;
	unsigned C_SIZE_low:8;
	
	unsigned SECTOR_SIZE_high:6;	//erase sector size
	unsigned ERASE_BLK_EN:1;	//erase single block enable
	unsigned Reserved4:1;
	
	unsigned WP_GRP_SIZE:7;	//write protect group size
	unsigned SECTOR_SIZE_low:1;	// SECTOR_SIZE = (SECTOR_SIZE_high << 1) | SECTOR_SIZE_low
	
	unsigned WRITE_BL_LEN_high:2;	//max. write data block length
	unsigned R2W_FACTOR:3;	//write speed factor
	unsigned Reserved5:2;
	unsigned WP_GRP_ENABLE:1;	//write protect group enable
		
	unsigned Reserved6:5;
	unsigned WRITE_BL_PARTIAL:1;	//partial blocks for write allowed
	unsigned WRITE_BL_LEN_low:2;	//WRITE_BL_LEN = (WRITE_BL_LEN_high << 2) | WRITE_BL_LEN_low

	unsigned Reserved7:2;
	unsigned FILE_FORMAT:2;	//File format
	unsigned TMP_WRITE_PROTECT:1;	//temporary write protection
	unsigned PERM_WRITE_PROTECT:1;	//permanent write protection
	unsigned COPY:1;	//copy flag (OTP)
	unsigned FILE_FORMAT_GRP:1;	//File format group

	unsigned NotUsed:1;	
	unsigned CRC:7;	//CRC checksum
} SDHC_REG_CSD_t;

/*typedef struct _MMC_REG_EXT_CSD
{
    unsigned char Reserved1[7];
    unsigned char S_CMD_SET;
    unsigned char Reserved2[300];
    unsigned char PWR_CL_26_360;
    unsigned char PWR_CL_52_360;
    unsigned char PWR_CL_26_195;
    unsigned char PWR_CL_52_195;
    unsigned char Reserved3[3];
    unsigned char CARD_TYPE;
    unsigned char Reserved4;
    unsigned char CSD_STRUCTURE;
    unsigned char Reserved5;
    unsigned char EXT_CSD_REV;
    unsigned char CMD_SET;
    unsigned char Reserved6;
    unsigned char CMD_SET_REV;
    unsigned char Reserved7;
    unsigned char POWER_CLASS;
    unsigned char Reserved8;
    unsigned char HS_TIMING;
    unsigned char Reserved9;
    unsigned char BUS_WIDTH;
    unsigned char Reserved10[183];
            } MMC_REG_EXT_CSD_t;*/// reserved for future use
typedef struct _MMC_REG_EXT_CSD
{
 	unsigned char Reserved26[134];                 //133
 	unsigned char SEC_BAD_BLK_MGMNT;               //134
 	unsigned char Reserved25;                      //135
 	unsigned char ENH_START_ADDR[4];               //136
 	unsigned char ENH_SIZE_MULT[3];                //140
 	unsigned char GP_SIZE_MULT[12];                //143
 	unsigned char PARTITION_SETTING_COMPLETED;     //155
 	unsigned char PARTITIONS_ATTRIBUTE;            //156
 	unsigned char MAX_ENH_SIZE_MULT[3];            //157
 	unsigned char PARTITIONING_SUPPORT;            //160
 	unsigned char HPI_MGMT;                        //161
 	unsigned char RST_n_FUNCTION;                  //162
 	unsigned char BKOPS_EN;                        //163
 	unsigned char BKOPS_START;                     //164
 	unsigned char Reserved24;                      //165
 	unsigned char WR_REL_PARAM;                    //166
 	unsigned char WR_REL_SET;                      //167
 	unsigned char RPMB_SIZE_MULT;                  //168
 	unsigned char FW_CONFIG;                       //169
 	unsigned char Reserved23; 	                   //170
 	unsigned char USER_WP;                         //171
 	unsigned char Reserved22;                      //172
 	unsigned char BOOT_WP;                         //173
 	unsigned char Reserved21; 	                   //174
 	unsigned char ERASE_GROUP_DEF;                 //175
 	unsigned char Reserved20; 	                   //176
 	unsigned char BOOT_BUS_WIDTH;                  //177
 	unsigned char BOOT_CONFIG_PROT;                //178
 	unsigned char PARTITION_CONFIG;                //179
 	unsigned char Reserved19;                      //180
 	unsigned char ERASED_MEM_CONT;                 //181
 	unsigned char Reserved18;                      //182
 	unsigned char BUS_WIDTH;                       //183
 	unsigned char Reserved17;                      //184
 	unsigned char HS_TIMING;                       //185
 	unsigned char Reserved16;                      //186
 	unsigned char POWER_CLASS;                     //187
 	unsigned char Reserved15;                      //188
 	unsigned char CMD_SET_REV;                     //189
 	unsigned char Reserved14;                      //190 
 	unsigned char CMD_SET;                         //191
 	unsigned char EXT_CSD_REV;                     //192
 	unsigned char Reserved13;                      //193
 	unsigned char CSD_STRUCTURE;                   //194
 	unsigned char Reserved12;                      //195
 	unsigned char CARD_TYPE;                       //196
 	unsigned char Reserved11;                      //197
 	unsigned char OUT_OF_INTERRUPT_TIME;           //198
 	unsigned char PARTITION_SWITCH_TIME;           //199
 	unsigned char PWR_CL_52_195;                   //200
 	unsigned char PWR_CL_26_195;                   //201
 	unsigned char PWR_CL_52_360;                   //202
 	unsigned char PWR_CL_26_360;                   //203
 	unsigned char Reserved10;                      //204
 	unsigned char MIN_PERF_R_4_26;                 //205
 	unsigned char MIN_PERF_W_4_26;                 //206
 	unsigned char MIN_PERF_R_8_26_4_52;            //207
 	unsigned char MIN_PERF_W_8_26_4_52;            //208
 	unsigned char MIN_PERF_R_8_52;                 //209
 	unsigned char MIN_PERF_W_8_52;                 //210
 	unsigned char Reserved9;                       //211
 	unsigned char SEC_COUNT[4];                    //212
 	unsigned char Reserved8;                       //216
 	unsigned char S_A_TIMEOUT;                     //217
 	unsigned char Reserved7;                       //218
 	unsigned char S_C_VCCQ;                        //219
 	unsigned char S_C_VCC;                         //220
 	unsigned char HC_WP_GRP_SIZE;                  //221
 	unsigned char REL_WR_SEC_C;                    //222
 	unsigned char ERASE_TIMEOUT_MULT;              //223
 	unsigned char HC_ERASE_GRP_SIZE;               //224
 	unsigned char ACC_SIZE;                        //225
 	unsigned char BOOT_SIZE_MULTI;                 //226
 	unsigned char Reserved6;                       //227
 	unsigned char BOOT_INFO;                       //228
 	unsigned char SEC_TRIM_MULT;                   //229
 	unsigned char SEC_ERASE_MULT;                  //230
 	unsigned char SEC_FEATURE_SUPPORT;             //231
 	unsigned char TRIM_MULT;                       //232
 	unsigned char Reserved5;                       //233
 	unsigned char MIN_PERF_DDR_R_8_52;             //234
 	unsigned char MIN_PERF_DDR_W_8_52;             //235
 	unsigned char Reserved4[2];                    //236
 	unsigned char PWR_CL_DDR_52_195;               //238
 	unsigned char PWR_CL_DDR_52_360;               //239
 	unsigned char Reserved3;                       //240
 	unsigned char INI_TIMEOUT_AP;                  //241
 	unsigned char CORRECTLY_PRG_SECTORS_NUM[4];    //242
 	unsigned char BKOPS_STATUS;                    //246
 	unsigned char Reserved2[255];                  //247
 	unsigned char BKOPS_SUPPORT;                   //502
 	unsigned char HPI_FEATURES;                    //503
 	unsigned char S_CMD_SET;                       //504
    unsigned char Reserved1[7];                    //505
} MMC_REG_EXT_CSD_t;
    
//MSB->LSB, structure for SD CARD Configuration Register
typedef struct _SD_REG_SCR {
	
	unsigned SD_SPEC:4;	//SD Card¡ªSpec. Version
	unsigned SCR_STRUCTURE:4;	//SCR Structure
	
	unsigned SD_BUS_WIDTHS:4;	//DAT Bus widths supported
	unsigned SD_SECURITY:3;	//SD Security Support
	unsigned DATA_STAT_AFTER_ERASE:1;	//data_status_after erases
	
	//unsigned Reserved1:16;	//for alignment
	unsigned CMD_SUPPORT : 2;
	unsigned Reserved1 : 13;
	unsigned SD_SPEC3 : 1;
	
	unsigned long Reserved2;
} SD_REG_SCR_t;


typedef struct _SDIO_REG_CCCR {

	unsigned char CCCR_SDIO_SPEC;	//cccr and sdio spec verion low four bits(cccr) high four bits(sdio)
	unsigned char SD_SPEC;	//SD Card¡ªSpec. Version low four bits
	unsigned char IO_ENABLE;	//
	unsigned char IO_READY;	//
	unsigned char INT_ENABLE;	//
	unsigned char INT_PENDING;
	unsigned char INT_ABORT;
	unsigned char BUS_Interface_Control;
	unsigned char Card_Capability;	
	unsigned short Common_CIS_Pointer1;	
	unsigned char Common_CIS_Pointer2;	
	unsigned char BUS_Suspend;	
	unsigned char Function_Select;	
	unsigned char Exec_Flags;	
	unsigned short FN0_Block_Size;	
	unsigned char Power_Control;	
	unsigned char High_Speed;	
	unsigned char RFU[220];	//
	unsigned char Reserved[16];
} SDIO_REG_CCCR_t;


typedef struct _SD_REG_DSR {

} SD_REG_DSR_t;

//MSB->LSB, structrue for SD Card Status
typedef struct _SD_Card_Status {
	
	unsigned LOCK_UNLOCK_FAILED:1;	//Set when a sequence or password error has been detected in lock/ unlock card command or if there was an attempt to access a locked card
	unsigned CARD_IS_LOCKED:1;	//When set, signals that the card is locked by the host
	unsigned WP_VIOLATION:1;	//Attempt to program a write-protected block.
	unsigned ERASE_PARAM:1;	//An invalid selection of write-blocks for erase occurred.
	unsigned ERASE_SEQ_ERROR:1;	//An error in the sequence of erase commands occurred.
	unsigned BLOCK_LEN_ERROR:1;	//The transferred block length is not allowed for this card, or the number of transferred bytes does not match the block length.
	unsigned ADDRESS_ERROR:1;	//A misaligned address that did not match the block length was used in the command.
	unsigned OUT_OF_RANGE:1;	//The command¡¯s argument was out of the allowed range for this card.
	
	unsigned CID_CSD_OVERWRITE:1;	//Can be either one of the following errors:
	unsigned Reserved1:1;
	unsigned Reserved2:1;
	unsigned ERROR:1;	//A general or an unknown error occurred during the operation.
	unsigned CC_ERROR:1;	//Internal card controller error
	unsigned CARD_ECC_FAILED:1;	//Card internal ECC was applied but failed to correct the data.
	unsigned ILLEGAL_COMMAND:1;	//Command not legal for the card state
	unsigned COM_CRC_ERROR:1;	//The CRC check of the previous command failed.
	
	unsigned READY_FOR_DATA:1;	//Corresponds to buffer empty signalling on the bus.
	unsigned CURRENT_STATE:4;	//The state of the card when receiving the command. 
	unsigned ERASE_RESET:1;	//An erase sequence was cleared beforem executing because an out of erase sequence command was received.
	unsigned CARD_ECC_DISABLED:1;	//The command has been executed without using the internal ECC.
	unsigned WP_ERASE_SKIP:1;	//Only partial address space was erased due to existing write protected blocks.
	
	unsigned Reserved3:2;
	unsigned Reserved4:1;
	unsigned AKE_SEQ_ERROR:1;	//Error in the sequence of authentication process.
	unsigned Reserved5:1;
	
	unsigned APP_CMD:1;	//The card will expect ACMD, or indication that the command has been interpreted as ACMD.
	unsigned NotUsed:2;
} SD_Card_Status_t;

//MSB->LSB, structure for SD SD_Status
typedef struct _SD_SD_Status {
	
	unsigned Reserved1:5;
	unsigned SECURED_MODE:1;	//Card is in Secured Mode of operation (refer to the SD Security Specifications document).
	unsigned DAT_BUS_WIDTH:2;	//Shows the currently defined data bus width that was defined by the SET_sd_info.bus_width command.
	
	unsigned Reserved2:8;

	unsigned SIZE_OF_PROTECTED_AREA:2;	//Shows the size of the protected area. The actual area = (SIZE_OF_PROTECTED_AREA) * MULT * BLOCK_LEN.
	unsigned SD_CARD_TYPE:6;	//In the future, the 8 LSBs will be used to define different variations of an SD Card (each bit will define different SD types).
	
	unsigned Reserved3:8;	//just for bit structure alignment	
	unsigned char Reserved4[16];	
	unsigned char Reserved5[39];
} SD_SD_Status_t;


typedef struct _SD_Switch_Function__Status {
	
	unsigned short Max_Current_Consumption;
	unsigned short Function_Group[6];
	
	unsigned Function_Group_Status5:4;
	unsigned Function_Group_Status6:4;
	unsigned Function_Group_Status3:4;	
	unsigned Function_Group_Status4:4;	
	unsigned Function_Group_Status1:4;	
	unsigned Function_Group_Status2:4;	
	
	unsigned char Data_Struction_Verion;
	unsigned short Function_Status_In_Group[6];
	unsigned char Reserved[34];
} SD_Switch_Function_Status_t;


//structure for response
typedef struct _SD_Response_R1 {

	unsigned char command;	//command index = bit 6:0
	SD_Card_Status_t card_status;	//card status
	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SD_Response_R1_t;


typedef struct _SD_Response_R2_CID {

	unsigned char reserved;	//should be 0x3F
	SD_REG_CID_t cid;	//response CID	
	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SD_Response_R2_CID_t;


typedef struct _SD_Response_R2_CSD {
	
	unsigned char reserved;	//should be 0x3F
	SD_REG_CSD_t csd;	//response CSD
	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SD_Response_R2_CSD_t;


typedef struct _SDHC_Response_R2_CSD {
	
	unsigned char reserved;	//should be 0x3F
	SDHC_REG_CSD_t csd;	//response CSD	
	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SDHC_Response_R2_CSD_t;


typedef struct _SD_Response_R3 {
	
	unsigned char reserved1;	//should be 0x3F
	SD_REG_OCR_t ocr;	//OCR register	
	unsigned end_bit:1;	//end bit = bit 0
	unsigned reserved2:7;	//should be 0x7F
} SD_Response_R3_t;


typedef struct _SDIO_Response_R4 {
	
	unsigned char reserved1;	//should be 0x3F
	unsigned Stuff_bits:3;	
	unsigned Memory_Present:1;	
	unsigned IO_Function_No:3;
	unsigned Card_Ready:1;
	SDIO_REG_OCR_t ocr;	//OCR register
	unsigned end_bit:1;	//end bit = bit 0
	unsigned reserved2:7;	//should be 0x7F
} SDIO_Response_R4_t;


typedef struct _SDIO_RW_CMD_Response_R5 {

	unsigned command:6;	//command index = bit 6:0
	unsigned direct_bit:1;
	unsigned start_bit:1;
	
	unsigned short stuff;	//not used
	
	unsigned Out_Of_Range:1;	//status of sdio card
	unsigned Function_Number:1;
	unsigned RFU:1;
	unsigned Error:1;
	unsigned IO_Current_State:2;
	unsigned Illegal_CMD:1;
	unsigned CMD_CRC_Error:1;

	unsigned char read_or_write_data;	//read back data

	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SDIO_RW_CMD_Response_R5_t;


typedef struct _SD_Response_R6 {
	
	unsigned char command;	//command index = bit 6:0
	unsigned char rca_high;	//New published RCA [31:16] of the card
	unsigned char rca_low;
	unsigned short part_card_status;	//[15:0] card status bits: 23,22,19,12:0
	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SD_Response_R6_t;


typedef struct _SD_Response_R7 {

	unsigned char command;	//command index = bit 6:0

	unsigned reserved1:4;
	unsigned cmd_version:4;	//0:voltage check
	
	unsigned char reserved;

	unsigned voltage_accept:4;	//0001b:2.7v-3.6v  0010b:1.65v-1.95v
	unsigned reserved2:4;
	
	unsigned char check_pattern;

	unsigned end_bit:1;	//end bit = bit 0
	unsigned crc7:7;	//CRC7 = bit 7:1
} SD_Response_R7_t;


typedef struct _SDIO_IO_RW_CMD_ARG {

	unsigned char write_data_bytes;	//write data bytes count

	unsigned stuff1:1;	//not used
	unsigned Register_Address:17;	//byte address of select fuction to read
	unsigned stuff2:1;	//not used  
	unsigned RAW_Flag:1;	//read after write flag
	unsigned Function_No:3;	//function number of wish to read or write
	unsigned R_W_Flag:1;	//the direction of I/O operation
} SDIO_IO_RW_CMD_ARG_t;


typedef struct _SDIO_IO_RW_EXTENDED_ARG {
	
	unsigned Byte_Block_Count:9;	//bytes or block count
	unsigned Register_Address:17;	//start address of I/O register
	unsigned OP_Code:1;	//define the read write operation  
	unsigned Block_Mode:1;	//Block or byte mode of read and write
	unsigned Function_No:3;	//function number of wish to read or write
	unsigned R_W_Flag:1;	//the direction of I/O operation
} SDIO_IO_RW_EXTENDED_ARG;

#pragma pack()


typedef enum _SD_Card_State { 

	STATE_UNKNOWN = -1, 
	STATE_INACTIVE = 0, 
	STATE_IDLE, 
	STATE_READY, 
	STATE_IDENTIFICATION, 
	STATE_STAND_BY, 
	STATE_TRANSFER,
	STATE_SENDING_DATA, 
	STATE_RECEIVE_DATA, 
	STATE_PROGRAMMING,
	STATE_DISCONNECT 
} SD_Card_State_t;


typedef enum _SD_Response_Type { 

	RESPONSE_NONE = -1, 
	RESPONSE_R1 = 0, 
	RESPONSE_R1B, 
	RESPONSE_R2_CID, 
	RESPONSE_R2_CSD, 
	RESPONSE_R3, 
	RESPONSE_R4,	//SD, responses are not supported.
	RESPONSE_R5,		//SD, responses are not supported.
	RESPONSE_R6, 
	RESPONSE_R7 
} SD_Response_Type_t;

/* Error codes */ 
typedef enum _SD_Error_Status_t {

	SD_MMC_NO_ERROR = 0, 
	SD_MMC_ERROR_OUT_OF_RANGE,	//Bit 31
	SD_MMC_ERROR_ADDRESS,	//Bit 30 
	SD_MMC_ERROR_BLOCK_LEN,	//Bit 29
	SD_MMC_ERROR_ERASE_SEQ,	//Bit 28
	SD_MMC_ERROR_ERASE_PARAM,	//Bit 27
	SD_MMC_ERROR_WP_VIOLATION,	//Bit 26
	SD_ERROR_CARD_IS_LOCKED,	//Bit 25
	SD_ERROR_LOCK_UNLOCK_FAILED,	//Bit 24
	SD_MMC_ERROR_COM_CRC,	//Bit 23
	SD_MMC_ERROR_ILLEGAL_COMMAND,	//Bit 22
	SD_ERROR_CARD_ECC_FAILED,	//Bit 21
	SD_ERROR_CC,		//Bit 20
	SD_MMC_ERROR_GENERAL,	//Bit 19
	SD_ERROR_Reserved1,	//Bit 18
	SD_ERROR_Reserved2,	//Bit 17
	SD_MMC_ERROR_CID_CSD_OVERWRITE,	//Bit 16
	SD_ERROR_AKE_SEQ,	//Bit 03
	SD_MMC_ERROR_STATE_MISMATCH, 
	SD_MMC_ERROR_HEADER_MISMATCH,
	SD_MMC_ERROR_DATA_CRC, 
	SD_MMC_ERROR_TIMEOUT,	
	SD_MMC_ERROR_DRIVER_FAILURE, 
	SD_MMC_ERROR_WRITE_PROTECTED,	
	SD_MMC_ERROR_NO_MEMORY, 
	SD_ERROR_SWITCH_FUNCTION_COMUNICATION,
	SD_ERROR_NO_FUNCTION_SWITCH, 
	SD_MMC_ERROR_NO_CARD_INS,	
	SD_MMC_ERROR_READ_DATA_FAILED, 
	SD_SDIO_ERROR_NO_FUNCTION,
        SD_WAIT_FOR_COMPLETION_TIMEOUT
} SD_Error_Status_t;


typedef enum _SD_Operation_Mode { 

	CARD_INDENTIFICATION_MODE = 0,	//fod = 100 ~ 400 Khz, OHz stops the clock. The given minimum frequency range is for cases where a continuous clock is required.
	DATA_TRANSFER_MODE = 1	//fpp = 0 ~ 25 Mhz
} SD_Operation_Mode_t;


typedef enum _SD_Bus_Width { 

	SD_BUS_SINGLE = 1,	//only DAT0
	SD_BUS_WIDE = 4		//use DAT0-4
} SD_Bus_Width_t;


typedef enum SD_Card_Type { 

	CARD_TYPE_NONE = 0, 
	CARD_TYPE_SD, 
	CARD_TYPE_SDHC, 
	CARD_TYPE_MMC, 
	CARD_TYPE_EMMC, 
	CARD_TYPE_SDIO 
} SD_Card_Type_t;


typedef enum SDIO_Card_Type { 

	CARD_TYPE_NONE_SDIO =0, 
	CARD_TYPE_SDIO_STD_UART, 
	CARD_TYPE_SDIO_BT_TYPEA, 
	CARD_TYPE_SDIO_BT_TYPEB,	
	CARD_TYPE_SDIO_GPS, 
	CARD_TYPE_SDIO_CAMERA, 
	CARD_TYPE_SDIO_PHS,	
	CARD_TYPE_SDIO_WLAN,
	CARD_TYPE_SDIO_OTHER_IF 
} SDIO_Card_Type_t;


typedef enum SD_SPEC_VERSION { 

	SPEC_VERSION_10_101, 
	SPEC_VERSION_110, 
	SPEC_VERSION_20,
	SPEC_VERSION_30 
} SD_SPEC_VERSION_t;


typedef enum MMC_SPEC_VERSION { 

	SPEC_VERSION_10_12, 
	SPEC_VERSION_14, 
	SPEC_VERSION_20_22,	
	SPEC_VERSION_30_33, 
	SPEC_VERSION_40_41 
} MMC_SPEC_VERSION_t;


typedef enum SD_SPEED_CLASS { 

	NORMAL_SPEED, 
	HIGH_SPEED 
} SD_SPEED_CLASS_t;

//function group 4
typedef enum SD_CURRENT_LIMIT {
	CURRENT_200mA = 0x01,
	CURRENT_400mA = 0x02,
	CURRENT_600mA = 0x04,
	CURRENT_800mA = 0x08
} SD_CURRENT_LIMIT_t;

//function group 3
typedef enum SD_DRIVER_STRENGTH {
	TYPE_B = 0x01,
	TYPE_A = 0x02,
	TYPE_C = 0x04,
	TYPE_D = 0x08
} SD_DRIVER_STRENGTH_t;

//function group 1
typedef enum SD_UHS_I_MODE {
	SDR12 = 0x01,
	SDR25 = 0x02,
	SDR50 = 0x04,
	SDR104 = 0x08,
	DDR50 = 0x10
} SD_UHS_I_MODE_t;

typedef struct SD_MMC_Card_Info {
	
	SD_Card_Type_t card_type;	
	SDIO_Card_Type_t sdio_card_type[8];	
	SD_Operation_Mode_t operation_mode;	
	SD_Bus_Width_t bus_width;	
	SD_SPEC_VERSION_t spec_version;	
	MMC_SPEC_VERSION_t mmc_spec_version;	
	SD_SPEED_CLASS_t speed_class;	
	SD_UHS_I_MODE_t uhs_mode;
	SD_REG_CID_t raw_cid;	
	SDIO_Pad_Type_t  io_pad_type;	/* hw io pin pad */

	unsigned short card_rca;
	unsigned char sdio_function_nums;
	unsigned sdio_clk_unit;
	unsigned sdio_clk;
	unsigned blk_len;
	unsigned sdio_blk_len[8];
	unsigned sdio_cis_addr[8];
	unsigned blk_nums;	
	unsigned clks_nac;
	int write_protected_flag;
	int inited_flag;
	int removed_flag;
	int init_retry;	
	int single_blk_failed;
	int sdio_init_flag;	

	unsigned emmc_boot_support;
	unsigned emmc_boot_partition_size[2];
	unsigned sd_save_hw_io_flag;
	unsigned sd_save_hw_io_config;
	unsigned sd_save_hw_io_mult_config;
	unsigned read_multi_block_failed;
	unsigned write_multi_block_failed;
	unsigned sdio_read_crc_close;
	unsigned sd_mmc_power_delay ;
	unsigned disable_high_speed;
	unsigned disable_wide_bus;
	unsigned max_blk_count;

	unsigned sdxc_save_hw_io_flag;
	unsigned sdxc_save_hw_io_ctrl;
	unsigned sdxc_save_hw_io_clk;

	unsigned support_uhs_mode;

	unsigned char *sd_mmc_buf;
	unsigned char *sd_mmc_phy_buf;

	void (*sd_mmc_power) (int power_on);
	int (*sd_mmc_get_ins) (void);	
	int (*sd_get_wp) (void);	
	void (*sd_mmc_io_release) (void);
} SD_MMC_Card_Info_t;

#define EMMC_BOOT_SIZE_UNIT (128*1024)  // for emmc boot size
    //SDIO_REG_DEFINE
#define CCCR_SDIO_SPEC_REG               0x00
#define SD_SPEC_REG                      0x01
#define IO_ENABLE_REG                    0x02
#define IO_READY_REG                     0x03
#define INT_ENABLE_REG                   0x04
#define INT_PENDING_REG				  	 0x05
#define IO_ABORT_REG					 0x06
#define BUS_Interface_Control_REG		 0x07
#define Card_Capability_REG			  	 0x08
#define Common_CIS_Pointer1_REG		  	 0x09
#define Common_CIS_Pointer2_REG		  	 0x0a
#define Common_CIS_Pointer3_REG		  	 0x0b
#define BUS_Suspend_REG				  	 0x0c
#define Function_Select_REG			  	 0x0d
#define Exec_Flags_REG					 0x0e
#define Ready_Flags_REG 				 0x0f
#define FN0_Block_Size_Low_REG			 0x10
#define FN0_Block_Size_High_REG		  	 0x11
#define Power_Control_REG				 0x12
#define High_Speed_REG					 0x13
#define FN1_Block_Size_Low_REG			 0x110
#define FN1_Block_Size_High_REG			 0x111
    
#define SDIO_Read_Data				  0
#define SDIO_Write_Data				  1
#define SDIO_DONT_Read_After_Write	  0
#ifdef CONFIG_SDIO_MARVELL_NH387_WIFI
#define SDIO_Read_After_Write		  0	//MARVELL_NH387 can't support verify check
#else
#define SDIO_Read_After_Write		  1
#endif
#define SDIO_Block_MODE			  	  1
#define SDIO_Byte_MODE		  	  	  0
    
#define SDIO_Wide_bus_Bit			  0x02
#define SDIO_Single_bus_Bit			  0x00
#define SDIO_Support_High_Speed		  0x01
#define SDIO_Enable_High_Speed		  0x02
#define SDIO_Support_Multi_Block	  0x02
#define SDIO_INT_EN_MASK			  0x01
#define SDIO_E4MI_EN_MASK			  0x20
#define SDIO_RES_bit				  0x08
    
#define SDIO_BLOCK_SIZE				  512
    
//SD/MMC Card bus commands          CMD     type    argument                response
    
//Broadcast Commands (bc), no response
//Broadcast Commands with Response (bcr)
//Addressed (point-to-point) Commands (ac)¡ªno data transfer on DAT
//Addressed (point-to-point) Data Transfer Commands (adtc)¡ªdata transfer on DAT.
    
#define APP_SPECIFIC	0x0

    /* Class 0 and 1, Basic Commands */ 
#define SD_MMC_GO_IDLE_STATE            0	//---   [31:0] don¡¯t care       --------
#define MMC_SEND_OP_COND                1	//bcr   [31:0] OCR w/out busy   R3
#define SD_MMC_ALL_SEND_CID             2	//bcr   [31:0] don¡¯t care       R2
#define SD_MMC_SEND_RELATIVE_ADDR       3	//bcr   [31:0] don¡¯t care       R6 for SD and R1 for MMC
    //  Reserved                4       -----   ----------              --------
#define IO_SEND_OP_COND                 5	//bcr   [23:0] OCR w/out busy  R4
#define SD_SET_BUS_WIDTHS               (6 | APP_SPECIFIC)	//ac    [31:2]stuff,[1:0]B/W    R1  ,Application Specific Commands Used
#define MMC_SWITCH_FUNTION              6	//MMC_c mmc switch power clock bus_width cmd
#define SD_SWITCH_FUNCTION				46 //46	//bcr   [31:23]mode,[22:8]default bit,[7:0]function
#define SD_MMC_SELECT_DESELECT_CARD     7	//ac    [31:16] RCA             R1
#define SD_SEND_IF_COND                 8	//      [11:8]supply voltage    R7
#define MMC_SEND_EXT_CSD                48	//ac    [31:0]stuff             R1
#define SD_MMC_SEND_CSD                 9	//ac    [31:16] RCA             R2
#define SD_MMC_SEND_CID                 10	//ac    [31:16] RCA             R2
////#define SD_READ_DAT_UNTIL_STOP          11	//adtc  [31:0] data address     R1
#define VOLTAGE_SWITCH					11
#define SD_MMC_STOP_TRANSMISSION        12	//ac    [31:0] don¡¯t care       R1b
#define SD_MMC_SEND_STATUS              (13 | APP_SPECIFIC)	//ac    [31:16] RCA             R1, Application Specific Commands Used
    //  Reserved                14      -----   ----------              --------
#define SD_MMC_GO_INACTIVE_STATE        15	//ac    [31:16] RCA             --------
    
    /* Class 2, Block Read Commands */ 
#define SD_MMC_SET_BLOCKLEN             16	//ac    [31:0] block length     R1
#define SD_MMC_READ_SINGLE_BLOCK        17	//adtc  [31:0] data address     R1
#define SD_MMC_READ_MULTIPLE_BLOCK      18	//adtc  [31:0] data address     R1
#define SD_SEND_TUNNING_PATTERN			19	//adtc	[31:0] data address		R1
    //  Reserved                20      -----   ----------              --------
    //  Reserved                21      -----   ----------              --------
#define SD_SEND_NUM_WR_BLOCKS           (22	| APP_SPECIFIC)//adtc  [31:0] stuff bits       R1  ,Application Specific Commands Used
#define SD_SET_WR_BLK_ERASE_COUNT       (23	| APP_SPECIFIC)//ac    [31:23]stuff,[22:0]B/N  R1  ,Application Specific Commands Used
    
    /* Class 4, Block Write Commands */ 
#define SD_MMC_WRITE_BLOCK              24	//adtc  [31:0] data address     R1
#define SD_MMC_WRITE_MULTIPLE_BLOCK     25	//adtc  [31:0] data address     R1
    //  Reserved                26      -----   ----------              --------
#define SD_MMC_PROGRAM_CSD              27	//adtc  [31:0] don¡¯t care*      R1
    
    /* Class 6, Write Protection */ 
#define SD_MMC_SET_WRITE_PROT           28	//ac    [31:0] data address     R1b
#define SD_MMC_CLR_WRITE_PROT           29	//ac    [31:0] data address     R1b
#define SD_MMC_SEND_WRITE_PROT          30	//adtc  [31:0] WP data address  R1
    //  Reserved                31      -----   ----------              --------
    
    /* Class 5, Erase Commands */ 
#define SD_ERASE_WR_BLK_START           32	//ac    [31:0] data address     R1
#define MMC_TAG_SECTOR_START            32	//ac    [31:0] data address     R1
#define SD_ERASE_WR_BLK_END             33	//ac    [31:0] data address     R1
#define MMC_TAG_SECTOR_END              33	//ac    [31:0] data address     R1
#define MMC_UNTAG_SECTOR                34	//ac    [31:0] data address     R1
#define MMC_TAG_ERASE_GROUP_START       35	//ac    [31:0] data address     R1
#define MMC_TAG_ERASE_GROUP_END         36	//ac    [31:0] data address     R1
#define MMC_UNTAG_ERASE_GROUP           37	//ac    [31:0] data address     R1
#define SD_MMC_ERASE                    38	//ac    [31:0] don¡¯t care       R1b
    //  Reserved                39      -----   ----------              --------
    //  Reserved                40      -----   ----------              --------
#define SD_APP_OP_COND                  (41	| APP_SPECIFIC) //bcr   [31:0]OCR without busy  R3  ,Application Specific Commands Used
    
    /* Class 7, Lock Card Commands */ 
#define SD_SET_CLR_CARD_DETECT          (42 | APP_SPECIFIC)	//ac    [31:1]stuff,[0]set_cd   R1, Application Specific Commands Used
#define MMC_LOCK_UNLOCK                 42	//adtc  [31:0] stuff bits       R1b
    //  SDA Optional Commands           43      -----   ----------              --------
    //  SDA Optional Commands           44      -----   ----------              --------
    //  SDA Optional Commands           45      -----   ----------              --------
    //  SDA Optional Commands           46      -----   ----------              --------
    //  SDA Optional Commands           47      -----   ----------              --------
    //  SDA Optional Commands           48      -----   ----------              --------
    //  SDA Optional Commands           49      -----   ----------              --------
    //  SDA Optional Commands           50      -----   ----------              --------
#define SD_SEND_SCR                     (51 | APP_SPECIFIC)	//adtc  [31:0] staff bits       R1  ,Application Specific Commands Used
#define IO_RW_DIRECT                    52	//R5
#define IO_RW_EXTENDED                  53	//R5-----   ----------              --------
    //  SDA Optional Commands           54      -----   ----------              --------
    
    /* Class 8, Application Specific Commands */ 
#define SD_APP_CMD                      55	//ac    [31:16]RCA,[15:0]stuff  R1
#define SD_GEN_CMD                      56	//adtc  [31:1]stuff,[0]RD/WR    R1
    //  Reserved                57      -----   ----------              --------
    //  Reserved                58      -----   ----------              --------
    //  Reserved                59      -----   ----------              --------
    //  Rsserved for Manufacturer       60      -----   ----------              --------
    //  Rsserved for Manufacturer       61      -----   ----------              --------
    //  Rsserved for Manufacturer       62      -----   ----------              --------
    //  Rsserved for Manufacturer       63      -----   ----------              --------
    
//All timing values definition for NAND MMC and SD-based Products
#define SD_MMC_TIME_NCR_MIN             2           /* min. of Number of cycles
													   between command and response */
#define SD_MMC_TIME_NCR_MAX             (128*10)    /* max. of Number of cycles
													   between command and response */
#define SD_MMC_TIME_NID                 5           /* Number of cycles
													   between card identification or
													   card operation conditions command
													   and the corresponding response */
#define SD_MMC_TIME_NAC_MIN             2           /* min. of Number of cycles
													   between command and 
													   the start of a related data block */
#define SD_MMC_TIME_NRC_MIN             8           /* min. of Number of cycles
													   between the last reponse and
													   a new command */
#define SD_MMC_TIME_NCC_MIN             8           /* min. of Number of cycles
													   between two commands, if no reponse
													   will be send after the first command
													   (e.g. broadcast) */
#define SD_MMC_TIME_NWR_MIN             2           /* min. of Number of cycles
													   between a write command and
													   the start of a related data block */
#define SD_MMC_TIME_NRC_NCC             16          /* actual NRC/NCC time used in source code */
    
#define SD_MMC_TIME_NWR                 8          /* actual Nwr time used in source code */
    
#define SD_MMC_Z_CMD_TO_RES             2          	/* number of Z cycles
													   allowing time for direction switching on the bus) */
#define SD_MMC_TIME_NAC_DEFAULT         25500
    
//Misc definitions
#define MAX_RESPONSE_BYTES              20
    
#define RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH     6
#define RESPONSE_R2_CID_CSD_LENGTH            17
#define RESPONSE_NONE_LENGTH                  0
    
#define SD_MMC_IDENTIFY_TIMEOUT			1500	// ms, SD=1000, MMC=500
#define MAX_CHECK_INSERT_RETRY          3
    
#define SD_IDENTIFICATION_TIMEOUT       (250*TIMER_1MS)	// ms
#define SD_PROGRAMMING_TIMEOUT          (1500*TIMER_1MS)	// ms
    
#define SDIO_FUNCTION_TIMEOUT			1000
    
#define SD_MMC_INIT_RETRY				3
    
#define SD_MMC_IDENTIFY_CLK							300	//K HZ
#define SD_MMC_TRANSFER_SLOWER_CLK					12	//M HZ
#define SD_MMC_TRANSFER_CLK							18	//M HZ
#define SD_MMC_TRANSFER_HIGHSPEED_CLK				25	//M HZ
    
//Following functions are the API used for outside routine
    
//SD Initialization...
int sd_mmc_init(SD_MMC_Card_Info_t *sd_mmc_info);

void sd_mmc_exit(SD_MMC_Card_Info_t *sd_mmc_info);

void sd_mmc_prepare_init(SD_MMC_Card_Info_t *sd_mmc_info);

//get sd_mmc card information
//void sd_mmc_get_info(blkdev_stat_t *info);
//Check if any card is connected to adapter
SD_Card_Type_t sd_mmc_check_present(SD_MMC_Card_Info_t *sd_mmc_info);

//Check if any card is inserted according to pull up resistor
int sd_mmc_check_insert(SD_MMC_Card_Info_t *sd_mmc_info);

//Read data from SD/MMC card
int sd_mmc_read_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long byte_cnt,
		     unsigned char *data_buf);

//Write data to SD/MMC card
int sd_mmc_write_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long byte_cnt,
		      unsigned char *data_buf);


int sdio_read_data_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo,
			      unsigned long sdio_addr,
			      unsigned long block_count,
			      unsigned char *data_buf);

int sdio_read_data_byte_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo,
			    unsigned long sdio_addr, unsigned long byte_count,
			    unsigned char *data_buf);

int sdio_write_data_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo,
			      unsigned long sdio_addr,
			      unsigned long block_count,
			      unsigned char *data_buf);

int sdio_write_data_byte_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo,
			     unsigned long sdio_addr, unsigned long byte_count,
			     unsigned char *data_buf);

int sdio_read_reg(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, unsigned long sdio_register,
		   unsigned char *reg_data);

int sdio_write_reg(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, unsigned int sdio_register,
		    unsigned char *reg_data, unsigned read_after_write_flag);

int sdio_read_data(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr,
		    unsigned long byte_count, unsigned char *data_buf);

int sdio_write_data(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr,
		     unsigned long byte_count, unsigned char *data_buf);

int sdio_close_target_interrupt(SD_MMC_Card_Info_t *sd_mmc_info, int function_no);

int sdio_open_target_interrupt(SD_MMC_Card_Info_t *sd_mmc_info, int function_no);

//SD Power on/off
void sd_mmc_power_on(SD_MMC_Card_Info_t *sd_mmc_info);

void sd_mmc_power_off(SD_MMC_Card_Info_t *sd_mmc_info);

void sd_mmc_prepare_init(SD_MMC_Card_Info_t *sd_mmc_info);

int sd_mmc_staff_init(SD_MMC_Card_Info_t *sd_mmc_info);
#endif				//_H_SD_PROTOCOL
    
