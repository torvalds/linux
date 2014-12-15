/*
 * SDHC definitions
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __AML_SDHC_H__
#define __AML_SDHC_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/earlysuspend.h>

#define     AML_ERROR_RETRY_COUNTER         5
#define     AML_TIMEOUT_RETRY_COUNTER       2

#define AML_SDHC_MAGIC			 "amlsdhc"
#define AML_SDIO_MAGIC			 "amlsdio"
enum aml_mmc_waitfor {
	XFER_INIT,
	XFER_START,				/* 1 */
	XFER_IRQ_OCCUR,			/* 2 */
	XFER_IRQ_FIFO_ERR,		/* 3 */
	XFER_IRQ_CRC_ERR,		/* 4 */
	XFER_IRQ_TIMEOUT_ERR,	/* 5 */
	XFER_IRQ_TASKLET_CMD,	/* 6 */
	XFER_IRQ_TASKLET_DATA,	/* 7 */
	XFER_IRQ_TASKLET_BUSY,	/* 8 */
	XFER_IRQ_UNKNOWN_IRQ,	/* 9 */
	XFER_TIMER_TIMEOUT,		/* 10 */
	XFER_TASKLET_CMD,		/* 11 */
	XFER_TASKLET_DATA,		/* 12 */
	XFER_TASKLET_BUSY,		/* 13 */
	XFER_TIMEDOUT,			/* 14 */
	XFER_FINISHED,			/* 15 */
	XFER_AFTER_START,		/* 16 */
};

enum aml_host_status { /* Host controller status */
	HOST_INVALID = 0,       /* 0, invalid value used for initialization */
	HOST_RX_FIFO_FULL = 1,  /* 1, start with 1 */
	HOST_TX_FIFO_EMPTY,	    /* 2 */
	HOST_RSP_CRC_ERR,	    /* 3 */
	HOST_DAT_CRC_ERR,	    /* 4 */
	HOST_RSP_TIMEOUT_ERR,   /* 5 */
	HOST_DAT_TIMEOUT_ERR,   /* 6 */
    HOST_ERR_END,	        /* 7, end of errors */
	HOST_TASKLET_CMD,	    /* 8 */
	HOST_TASKLET_DATA,	    /* 9 */
};
struct amlsd_host;
struct amlsd_platform {
	struct amlsd_host* host;
	struct mmc_host *mmc;
	struct list_head sibling;
	unsigned int ocr_avail;
	unsigned int port;
#define     PORT_SDIO_A     0
#define     PORT_SDIO_B     1
#define     PORT_SDIO_C     2
#define     PORT_SDHC_A     3
#define     PORT_SDHC_B     4
#define     PORT_SDHC_C     5

	unsigned int width;
	unsigned int caps;
	unsigned int caps2;
    unsigned int card_capacity;

	unsigned int f_min;
	unsigned int f_max;
	unsigned int f_max_w;
	unsigned int clkc;
	unsigned int clk2;
	unsigned int clkc_w;
	unsigned int ctrl;
	unsigned int clock;
	unsigned int tune_phase;            /* store tuning result */
	unsigned char signal_voltage;		/* signalling voltage (1.8V or 3.3V) */

	unsigned int low_burst;
	unsigned int irq_in;
	unsigned int irq_in_edge;
	unsigned int irq_out;
	unsigned int irq_out_edge;
	unsigned int gpio_cd;
	unsigned int gpio_cd_level;
	unsigned int gpio_power;
	unsigned int power_level;
	char pinname[32];
	unsigned int gpio_ro;
    unsigned int gpio_dat3;
    unsigned int jtag_pin;
    int is_sduart;
    bool is_in;
    bool is_tuned;                      /* if card has been tuning */
    bool need_retuning;
	struct delayed_work	retuning;

    /* we used this flag to filter some unnecessary cmd before initialized flow */
    bool is_fir_init; // has been initialized for the first time
    unsigned int card_type; /* 0:unknown, 1:mmc card(include eMMC), 2:sd card(include tSD), 3:sdio device(ie:sdio-wifi), 4:SD combo (IO+mem) card, 5:NON sdio device(means sd/mmc card), other:reserved */
#define CARD_TYPE_UNKNOWN           0        /* unknown */
#define CARD_TYPE_MMC               1        /* MMC card */
#define CARD_TYPE_SD                2        /* SD card */
#define CARD_TYPE_SDIO              3        /* SDIO card */
#define CARD_TYPE_SD_COMBO          4        /* SD combo (IO+mem) card */
#define CARD_TYPE_NON_SDIO          5        /* NON sdio device (means SD/MMC card) */
#define aml_card_type_unknown(c)    ((c)->card_type == CARD_TYPE_UNKNOWN)
#define aml_card_type_mmc(c)        ((c)->card_type == CARD_TYPE_MMC)
#define aml_card_type_sd(c)         ((c)->card_type == CARD_TYPE_SD)
#define aml_card_type_sdio(c)       ((c)->card_type == CARD_TYPE_SDIO)
#define aml_card_type_non_sdio(c)   ((c)->card_type == CARD_TYPE_NON_SDIO)

    // struct pinctrl *uart_ao_pinctrl;
	void (*irq_init)(struct amlsd_platform* pdata);

	unsigned int max_blk_count;
	unsigned int max_blk_size;
	unsigned int max_req_size;
	unsigned int max_seg_size;

	/*for inand partition: struct mtd_partition, easy porting from nand*/
	struct mtd_partition *parts;
	unsigned int nr_parts;

	struct resource* resource;
	void (*xfer_pre)(struct amlsd_platform* pdata);
	void (*xfer_post)(struct amlsd_platform* pdata);

	int (*port_init)(struct amlsd_platform* pdata);
	int (*cd)(struct amlsd_platform* pdata);
	int (*ro)(struct amlsd_platform* pdata);
	void (*pwr_pre)(struct amlsd_platform* pdata);
	void (*pwr_on)(struct amlsd_platform* pdata);
	void (*pwr_off)(struct amlsd_platform* pdata);

};

struct amlsd_host {
	/* back-link to device */
	struct device *dev;
	struct list_head sibling;
    struct platform_device *pdev;
	struct amlsd_platform * pdata;
	struct mmc_host		*mmc;
	struct mmc_request	*request;
	struct resource		*mem;
	void __iomem		*base;
	int			dma;
	char*		bn_buf;
	dma_addr_t		bn_dma_buf;
	unsigned int f_max;
	unsigned int f_max_w;
	unsigned int f_min;
	// struct tasklet_struct cmd_tlet;
	// struct tasklet_struct data_tlet;
	// struct tasklet_struct busy_tlet;
	// struct tasklet_struct to_tlet;
    // struct timer_list timeout_tlist;
	struct delayed_work	timeout;
	// struct early_suspend amlsd_early_suspend;

    struct class            debug;
	unsigned int send;
	unsigned int ctrl;
	unsigned int clkc;
	// unsigned int clkc_w;
	// unsigned int pdma;
	// unsigned int pdma_s;
	// unsigned int pdma_low;
	unsigned int misc;
	unsigned int ictl;
	unsigned int ista;
	unsigned int dma_addr;

	unsigned long		clk_rate;

	struct  mmc_request	*mrq;
	struct  mmc_request	*mrq2;
	spinlock_t	mrq_lock;
	int			cmd_is_stop;
	enum aml_mmc_waitfor	xfer_step;
	enum aml_mmc_waitfor	xfer_step_prev;

	int			bus_width;
	int     port;
	int     locked;
    bool    is_gated;
	// unsigned int		ccnt, dcnt;

	int     status; // host status: xx_error/ok
	int init_flag;

    char    *msg_buf;
#define MESSAGE_BUF_SIZE            512

#ifdef CONFIG_DEBUG_FS
	struct dentry		*debug_root;
	struct dentry		*debug_state;
	struct dentry		*debug_regs;
#endif

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

    u32			opcode; // add by gch for debug
	u32			arg; // add by gch for debug
    u32         cmd25_cnt;

#ifdef      CONFIG_MMC_AML_DEBUG
    u32         req_cnt;
    u32         trans_size;
    u32         time_req_sta; // request start time
    u32         reg_buf[16];
#endif
    u32         time_req_sta; // request start time
    
    struct pinctrl *pinctrl;
    char pinctrl_name[30];

    int storage_flag; // used for judging if there is a tsd/emmc

    int         version; // bit[7-0]--minor version, bit[31-8]--major version
};

/*-sdio-*/

#define SDIO_ARGU       (0x0)
#define SDIO_SEND       (0x4)
#define SDIO_CONF       (0x8)
#define SDIO_IRQS       (0xc)
#define SDIO_IRQC       (0x10)
#define SDIO_MULT       (0x14)
#define SDIO_ADDR       (0x18)
#define SDIO_EXT        (0x1c)
#define SDIO_CCTL       (0x40)
#define SDIO_CDAT       (0x44)

#define CLK_DIV         (0x1f4)

struct cmd_send{
    u32 cmd_command:8; /*[7:0] Command Index*/
    u32 cmd_response_bits:8; /*[15:8]
        * 00 means no response
        * others: Response bit number(cmd bits+response bits+crc bits-1)*/
    u32 response_do_not_have_crc7:1; /*[16]
        * 0:Response need check CRC7, 1: dont need check*/
    u32 response_have_data:1; /*[17]
        * 0:Receiving Response without data, 1:Receiving response with data*/
    u32 response_crc7_from_8:1; /*[18]
        * 0:Normal CRC7, Calculating CRC7 will be from bit0 of all response bits,
        * 1:Calculating CRC7 will be from bit8 of all response bits*/
    u32 check_busy_on_dat0:1; /*[19]
        * used for R1b response 0: dont check busy on dat0, 1:need check*/
    u32 cmd_send_data:1; /*[20]
        * 0:This command is not for transmitting data,
        * 1:This command is for transmitting data*/
    u32 use_int_window:1; /*[21]
        * 0:SDIO DAT1 interrupt window disabled, 1:Enabled*/
    u32 reserved:2;/*[23:22]*/
    u32 repeat_package_times:8; /*[31:24] Total packages to be sent*/
};

struct sdio_config{
    u32 cmd_clk_divide:10; /*[9:0] Clock rate setting,
        * Frequency of SD equals to Fsystem/((cmd_clk_divide+1)*2)*/
    u32 cmd_disable_crc:1; /*[10]
        * 0:CRC employed, 1:dont send CRC during command being sent*/
    u32 cmd_out_at_posedge:1; /*[11]
        * Command out at negedge normally, 1:at posedge*/
    u32 cmd_argument_bits:6; /*[17:12] before CRC added, normally 39*/
    u32 do_not_delay_data:1; /*[18]
        *0:Delay one clock normally, 1:dont delay*/
    u32 data_latch_at_negedge:1; /*[19]
        * 0:Data caught at posedge normally, 1:negedge*/
    u32 bus_width:1; /*[20] 0:1bit, 1:4bit*/
    u32 m_endian:2; /*[22:21]
        * Change ENDIAN(bytes order) from DMA data (e.g. dma_din[31:0]).
        * (00: ENDIAN no change, data output equals to original dma_din[31:0];
        * 01: data output equals to {dma_din[23:16],dma_din[31:24],
        * dma_din[7:0],dma_din[15:8]};10: data output equals to
        * {dma_din[15:0],dma_din[31:16]};11: data output equals to
        * {dma_din[7:0],dma_din[15:8],dma_din[23:16],dma_din[31:24]})*/
    u32 sdio_write_nwr:6; /*[28:23]
        * Number of clock cycles waiting before writing data*/
    u32 sdio_write_crc_ok_status:3; /*[31:29] if CRC status
        * equals this register, sdio write can be consider as correct*/
};

struct sdio_status_irq{
    u32 sdio_status:4; /*[3:0] Read Only
        * SDIO State Machine Current State, just for debug*/
    u32 sdio_cmd_busy:1; /*[4] Read Only
        * SDIO Command Busy, 1:Busy State*/
    u32 sdio_response_crc7_ok:1; /*[5] Read Only
        * SDIO Response CRC7 status, 1:OK*/
    u32 sdio_data_read_crc16_ok:1; /*[6] Read Only
        * SDIO Data Read CRC16 status, 1:OK*/
    u32 sdio_data_write_crc16_ok:1; /*[7] Read Only
        * SDIO Data Write CRC16 status, 1:OK*/
    u32 sdio_if_int:1; /*[8] write 1 clear this int bit
        * SDIO DAT1 Interrupt Status*/
    u32 sdio_cmd_int:1; /*[9] write 1 clear this int bit
        * Command Done Interrupt Status*/
    u32 sdio_soft_int:1; /*[10] write 1 clear this int bit
        * Soft Interrupt Status*/
    u32 sdio_set_soft_int:1; /*[11] write 1 to this bit
        * will set Soft Interrupt, read out is m_req_sdio, just for debug*/
    u32 sdio_status_info:4; /*[15:12]
        * used for change information between ARC and Amrisc */
    u32 sdio_timing_out_int:1; /*[16] write 1 clear this int bit
        * Timeout Counter Interrupt Status*/
    u32 amrisc_timing_out_int_en:1; /*[17]
        * Timeout Counter Interrupt Enable for AMRISC*/
    u32 arc_timing_out_int_en:1; /*[18]
        * Timeout Counter Interrupt Enable for ARC/ARM*/
    u32 sdio_timing_out_count:13; /*[31:19]
        * Timeout Counter Preload Setting and Present Status*/
};

struct sdio_irq_config{
    u32 amrisc_if_int_en:1; /*[0]
        * 1:SDIO DAT1 Interrupt Enable for AMRISC*/
    u32 amrisc_cmd_int_en:1; /*[1]
        * 1:Command Done Interrupt Enable for AMRISC*/
    u32 amrisc_soft_int_en:1; /*[2]
        * 1:Soft Interrupt Enable for AMRISC*/
    u32 arc_if_int_en:1; /*[3]
        * 1:SDIO DAT1 Interrupt Enable for ARM/ARC*/
    u32 arc_cmd_int_en:1; /*[4]
        * 1:Command Done Interrupt Enable for ARM/ARC*/
    u32 arc_soft_int_en:1; /*[5]
        * 1:Soft Interrupt Enable for ARM/ARC*/
    u32 sdio_if_int_config:2; /*[7:6]
        * 00:sdio_if_interrupt window will reset after data Tx/Rx or command
        * done, others: only after command done*/
    u32 sdio_force_data:6; /*[13:8]
        * Write operation: Data forced by software
        * Read operation: {CLK,CMD,DAT[3:0]}*/
    u32 sdio_force_enable:1; /*[14] Software Force Enable
        * This is the software force mode, Software can directly
        * write to sdio 6 ports (cmd, clk, dat0..3) if force_output_en
        * is enabled. and hardware outputs will be bypassed.*/
    u32 soft_reset:1; /*[15]
        * Write 1 Soft Reset, Don't need to clear it*/
    u32 sdio_force_output_en:6; /*[21:16]
        * Force Data Output Enable,{CLK,CMD,DAT[3:0]}*/
    u32 disable_mem_halt:2; /*[23:22] write and read
        * 23:Disable write memory halt, 22:Disable read memory halt*/
    u32 sdio_force_data_read:6; /*[29:24] Read Only
        * Data read out which have been forced by software*/
    u32 force_halt:1; /*[30] 1:Force halt SDIO by software
        * Halt in this sdio host controller means stop to transmit or
        * receive data from sd card. and then sd card clock will be shutdown.
        * Software can force to halt anytime, and hardware will automatically
        * halt the sdio when reading fifo is full or writing fifo is empty*/
    u32 halt_hole:1; /*[31]
        * 0: SDIO halt for 8bit mode, 1:SDIO halt for 16bit mode*/
};

struct sdio_mult_config{
    u32 sdio_port_sel:2; /*[1:0] 0:sdio_a, 1:sdio_b, 2:sdio_c*/
    u32 ms_enable:1; /*[2] 1:Memory Stick Enable*/
    u32 ms_sclk_always:1; /*[3] 1: Always send ms_sclk*/
    u32 stream_enable:1; /*[4] 1:Stream Enable*/
    u32 stream_8_bits_mode:1; /*[5] Stream 8bits mode*/
    u32 data_catch_level:2; /*[7:6] Level of data catch*/
    u32 write_read_out_index:1; /*[8] Write response index Enable
        * [31:16], [11:10], [7:0] is set only when  bit8 of this register is not set.
        * And other bits are set only when bit8 of this register is also set.*/
    u32 data_catch_readout_en:1; /*[9] Data catch readout Enable*/
    u32 sdio_0_data_on_1:1; /*[10] 1:dat0 is on dat1*/
    u32 sdio_1_data_swap01:1; /*[11] 1:dat1 and dat0 swapped*/
    u32 response_read_index:4; /*[15:12] Index of internal read response*/
    u32 data_catch_finish_point:12; /*[27:16] If internal data
        * catch counter equals this register, it indicates data catching is finished*/
    u32 reserved:4; /*[31:28]*/
};

struct sdio_extension{
    u32 cmd_argument_ext:16; /*[15:0] for future use*/
    u32 data_rw_number:14; /*[29:16]
        * Data Read/Write Number in one packet, include CRC16 if has CRC16*/
    u32 data_rw_do_not_have_crc16:1; /*[30]
        * 0:data Read/Write has crc16, 1:without crc16*/
    u32 crc_status_4line:1; /*[31] 1:4Lines check CRC Status*/
};

struct sdio_reg{
    u32 argument; /*2308*/
    struct cmd_send send; /*2309*/
    struct sdio_config config; /*230a*/
    struct sdio_status_irq status; /*230b*/
    struct sdio_irq_config irqc; /*230c*/
    struct sdio_mult_config mult; /*230d*/
    u32 m_addr; /*230e*/
    struct sdio_extension ext;/*230f*/
};

/*-sdhc-*/

#define SDHC_ARGU				(0x00)
#define SDHC_SEND				(0x04)
#define SDHC_CTRL				(0x08)
#define SDHC_STAT				(0x0C)
#define SDHC_CLKC				(0x10)
#define SDHC_ADDR				(0x14)
#define SDHC_PDMA				(0x18)
#define SDHC_MISC				(0x1C)
#define SDHC_DATA				(0x20)
#define SDHC_ICTL				(0x24)
#define SDHC_ISTA				(0x28)
#define SDHC_SRST				(0x2C)
#define SDHC_ESTA				(0x30)
#define SDHC_ENHC				(0x34)
#define SDHC_CLK2				(0x38)

struct sdhc_send{
	u32 cmd_index:6; /*[5:0] command index*/
	u32 cmd_has_resp:1; /*[6] 0:no resp 1:has resp*/
	u32 cmd_has_data:1; /*[7] 0:no data 1:has data*/
	u32 resp_len:1; /*[8] 0:48bit 1:136bit*/
	u32 resp_no_crc:1; /*[9] 0:check crc7 1:don't check crc7*/
	u32 data_dir:1; /*[10] 0:data rx, 1:data tx*/
	u32 data_stop:1; /*[11] 0:rx or tx, 1:data stop,ATTN:will give rx a softreset*/
	u32 r1b:1; /*[12] 0: resp with no busy, 1:R1B*/
	u32 reserved:3; /*[15:13] reserved*/
	u32 total_pack:16; /*[31:16] total package number for writing or reading*/
};

struct sdhc_ctrl{
	u32 dat_type:2; /*[1:0] 0:1bit, 1:4bits, 2:8bits, 3:reserved*/
	u32 ddr_mode:1; /*[2] 0:SDR mode, 1:Don't set it*/
	u32 tx_crc_nocheck:1; /*[3] 0:check sd write crc result, 1:disable tx crc check*/
	u32 pack_len:9; /*[12:4] 0:512Bytes, 1:1, 2:2, ..., 511:511Bytes*/
	u32 rx_timeout:7; /*[19:13] cmd or wcrc Receiving Timeout, default 64*/
	u32 rx_period:4; /*[23:20]Period between response/cmd and next cmd, default 8*/
	u32 rx_endian:3; /*[26:24] Rx Endian Control*/
	u32 sdio_irq_mode:1; /*[27]0:Normal mode, 1: support data block gap
			(need turn off clock gating)*/
	u32 dat0_irq_sel:1; /*[28] Dat0 Interrupt selection,
			0:busy check after response, 1:any rising edge of dat0*/
	u32 tx_endian:3; /*[31:29] Tx Endian Control*/
};

struct sdhc_stat{
	u32 cmd_busy:1; /*[0] 0:Ready for command, 1:busy*/
	u32 dat3_0:4; /*[4:1] DAT[3:0]*/
	u32 cmd:1; /*[5] CMD*/
	u32 rxfifo_cnt:7; /*[12:6] RxFIFO count*/
	u32 txfifo_cnt:7; /*[19:13] TxFIFO count*/
	u32 dat7_4:4; /*[23:20] DAT[7:4]*/
	u32 reserved:8; /*[31:24] Reserved*/
};

/*
* to avoid glitch issue,
* 1. clk_switch_on better be set after cfg_en be set to 1'b1
* 2. clk_switch_off shall be set before cfg_en be set to 1'b0
* 3. rx_clk/sd_clk phase diff please see SD_REGE_CLK2.
*/
struct sdhc_clkc{
	u32 clk_div:12; /*[11:0] clk_div for TX_CLK 0: don't set it,
			1:div2, 2:div3, 3:div4 ...*/
	u32 tx_clk_on:1; /*[12] TX_CLK 0:switch off, 1:switch on*/
	u32 rx_clk_on:1; /*[13] RX_CLK 0:switch off, 1:switch on*/
	u32 sd_clk_on:1; /*[14] SD_CLK 0:switch off, 1:switch on*/
	u32 mod_clk_on:1; /*[15] Clock Module Enable, Should
			set before bit[14:12] switch on, and after bit[14:12] switch off*/
	u32 clk_src_sel:2; /*[17:16] 0:osc, 1:fclk_div4, 2:fclk_div3, 3:fclk_div5*/
	u32 reserved:6; /*[23:18] Reserved*/
	u32 clk_jic:1; /*[24] Clock JIC for clock gating control
			1: will turn off clock gating*/
	u32 mem_pwr_off:2; /*[26:25] 00:Memory Power Up, 11:Memory Power Off*/
	u32 reserved2:5; /*[31:27] Reserved*/
};

/*
* Note1: dma_urgent is just set when bandwidth is very tight
* Note2: pio_rdresp need to be combined with REG0_ARGU;
* For R0, when 0, reading REG0 will get the normal 32bit response;
* For R2, when 1, reading REG0 will get CID[31:0], when 2, get CID[63:32],
* and so on; 6 or 7, will get original command argument.
*/
struct sdhc_pdma{
	u32 dma_mode:1; /*[0] 0:PIO mode, 1:DMA mode*/
	u32 pio_rdresp:3; /*[3:1] 0:[39:8] 1:1st 32bits, 2:2nd ...,
			6 or 7:command argument*/
	u32 dma_urgent:1; /*[4] 0:not urgent, 1:urgent*/
	u32 wr_burst:5; /*[9:5] Number in one Write request burst(0:1,1:2...)*/
	u32 rd_burst:5; /*[14:10] Number in one Read request burst(0:1, 1:2...)*/
	u32 rxfifo_th:7; /*[21:15] RxFIFO threshold, >=rxth, will request write*/
	u32 txfifo_th:7; /*[28:22] TxFIFO threshold, <=txth, will request read*/
	u32 rxfifo_manual_flush:2; /*[30:29] [30]self-clear-flush,
			[29] mode: 0:hw, 1:sw*/
	u32 txfifo_fill:1; /*[31] self-clear-fill, recommand to write before sd send*/
};

struct sdhc_misc{
	u32 reserved:4; /*[3:0] reserved*/
	u32 wcrc_err_patt:3; /*[6:4] WCRC Error Pattern*/
	u32 wcrc_ok_patt:3; /*[9:7] WCRC OK Pattern*/
	u32 reserved1:6; /*[15:10] reserved*/
	u32 burst_num:6; /*[21:16] Burst Number*/
	u32 thread_id:6; /*[27:22] Thread ID*/
	u32 manual_stop:1; /*[28] 0:auto stop mode, 1:manual stop mode*/
	u32 reserved2:3; /*[31:29] reserved*/
};

struct sdhc_ictl{
	u32 resp_ok:1; /*[0] Response is received OK*/
	u32 resp_timeout:1; /*[1] Response Timeout Error*/
	u32 resp_err_crc:1; /*[2] Response CRC Error*/
	u32 resp_ok_noclear:1; /*[3] Response is received OK(always no self reset)*/
	u32 data_1pack_ok:1; /*[4] One Package Data Completed ok*/
	u32 data_timeout:1; /*[5] One Package Data Failed (Timeout Error)*/
	u32 data_err_crc:1; /*[6] One Package Data Failed (CRC Error)*/
	u32 data_xfer_ok:1; /*[7] Data Transfer Completed ok*/
	u32 rx_higher:1; /*[8] RxFIFO count > threshold*/
	u32 tx_lower:1; /*[9] TxFIFO count < threshold*/
	u32 dat1_irq:1; /*[10] SDIO DAT1 Interrupt*/
	u32 dma_done:1; /*[11] DMA Done*/
	u32 rxfifo_full:1; /*[12] RxFIFO Full*/
	u32 txfifo_empty:1; /*[13] TxFIFO Empty*/
	u32 addi_dat1_irq:1; /*[14] Additional SDIO DAT1 Interrupt*/
	u32 reserved:1; /*[15] reserved*/
	u32 dat1_irq_delay:2; /*[17:16] sdio dat1 interrupt mask windows clear
			delay control,0:2cycle 1:1cycles*/
	u32 reserved1:14; /*[31:18] reserved*/
};

/*Note1: W1C is write one clear.*/
struct sdhc_ista{
	u32 resp_ok:1; /*[0] Response is received OK (W1C)*/
	u32 resp_timeout:1; /*[1] Response is received Failed (Timeout Error) (W1C)*/
	u32 resp_err_crc:1; /*[2] Response is received Failed (CRC Error) (W1C)*/
	u32 resp_ok_noclear:1; /*[3] Response is Received OK (always no self reset)*/
	u32 data_1pack_ok:1; /*[4] One Package Data Completed ok (W1C)*/
	u32 data_timeout:1; /*[5] One Package Data Failed (Timeout Error) (W1C)*/
	u32 data_err_crc:1; /*[6] One Package Data Failed (CRC Error) (W1C)*/
	u32 data_xfer_ok:1; /*[7] Data Transfer Completed ok (W1C)*/
	u32 rx_higher:1; /*[8] RxFIFO count > threshold (W1C)*/
	u32 tx_lower:1; /*[9] TxFIFO count < threshold (W1C)*/
	u32 dat1_irq:1; /*[10] SDIO DAT1 Interrupt (W1C)*/
	u32 dma_done:1; /*[11] DMA Done (W1C)*/
	u32 rxfifo_full:1; /*[12] RxFIFO Full(W1C)*/
	u32 txfifo_empty:1; /*[13] TxFIFO Empty(W1C)*/
	u32 addi_dat1_irq:1; /*[14] Additional SDIO DAT1 Interrupt*/
	u32 reserved:19; /*[31:13] reserved*/
};

/*
* Note1: Soft reset for DPHY TX/RX needs programmer to set it
* and then clear it manually.*/
struct sdhc_srst{
	u32 main_ctrl:1; /*[0] Soft reset for MAIN CTRL(self clear)*/
	u32 rxfifo:1; /*[1] Soft reset for RX FIFO(self clear)*/
	u32 txfifo:1; /*[2] Soft reset for TX FIFO(self clear)*/
	u32 dphy_rx:1; /*[3] Soft reset for DPHY RX*/
	u32 dphy_tx:1; /*[4] Soft reset for DPHY TX*/
	u32 dma_if:1; /*[5] Soft reset for DMA IF(self clear)*/
	u32 reserved:26; /*[31:6] reserved*/
};

struct sdhc_enhc{
	u32 rx_timeout:8; /*[7:0] Data Rx Timeout Setting*/
	u32 sdio_irq_period:8; /*[15:8] SDIO IRQ Period Setting
			(IRQ checking window length)*/
	u32 dma_rd_resp:1; /*[16] No Read DMA Response Check*/
	u32 dma_wr_resp:1; /*[16] No Write DMA Response Check*/
	u32 rxfifo_th:7; /*[24:18] RXFIFO Full Threshold,default 60*/
	u32 txfifo_th:7; /*[31:25] TXFIFO Empty Threshold,default 0*/
};

struct sdhc_clk2{
	u32 rx_clk_phase:12; /*[11:0] rx_clk phase diff(default 0:no diff,
			1:one input clock cycle ...)*/
	u32 sd_clk_phase:12; /*[23:12] sd_clk phase diff(default 0:half(180 degree),
			1:half+one input clock cycle, 2:half+2 input clock cycles, ...)*/
	u32 reserved:8; /*[31:24] reserved*/
};

#define SDHC_CLOCK_SRC_OSC              0 // 24MHz
#define SDHC_CLOCK_SRC_FCLK_DIV4        1
#define SDHC_CLOCK_SRC_FCLK_DIV3        2
#define SDHC_CLOCK_SRC_FCLK_DIV5        3
#define SDHC_ISTA_W1C_ALL               0x7fff
#define SDHC_SRST_ALL                   0x3f
#define SDHC_ICTL_ALL	                    0x7fff

#define STAT_POLL_TIMEOUT				0xfffff

#define MMC_RSP_136_NUM					4
#define MMC_MAX_DEVICE					3
#define MMC_TIMEOUT						5000

//#define printk(a...)
#define DBG_LINE_INFO()  printk(KERN_WARNING "[%s] : %s\n",__func__,__FILE__);
//#define DBG_LINE_INFO()
// #define dev_err(a,s) printk(KERN_INFO s);


#define AML_MMC_DISABLED_TIMEOUT	100
#define AML_MMC_SLEEP_TIMEOUT		1000
#define AML_MMC_OFF_TIMEOUT 8000

#define SDHC_BOUNCE_REQ_SIZE		(512*1024)
#define SDIO_BOUNCE_REQ_SIZE		(128*1024)
#define MMC_TIMEOUT_MS		20

#define MESON_SDIO_PORT_A 0
#define MESON_SDIO_PORT_B 1
#define MESON_SDIO_PORT_C 2
#define MESON_SDIO_PORT_XC_A 3
#define MESON_SDIO_PORT_XC_B 4
#define MESON_SDIO_PORT_XC_C 5

void aml_sdhc_request(struct mmc_host *mmc, struct mmc_request *mrq);
int aml_sdhc_get_cd(struct mmc_host *mmc);
extern void amlsd_init_debugfs(struct mmc_host *host);

extern struct mmc_host *sdio_host;

#define     SPI_BOOT_FLAG                   0
#define     NAND_BOOT_FLAG                  1
#define     EMMC_BOOT_FLAG                  2
#define     CARD_BOOT_FLAG                  3
#define     SPI_NAND_FLAG                   4
#define     SPI_EMMC_FLAG                   5

#define R_BOOT_DEVICE_FLAG  READ_CBUS_REG(ASSIST_POR_CONFIG)

#if defined (CONFIG_ARCH_MESON8) || defined (CONFIG_ARCH_MESON6TVD)
#define POR_BOOT_VALUE ((((R_BOOT_DEVICE_FLAG>>9)&1)<<2)|((R_BOOT_DEVICE_FLAG>>6)&3)) // {poc[9],poc[7:6]}
#else
#define POR_BOOT_VALUE (R_BOOT_DEVICE_FLAG & 7)
#endif

#define POR_NAND_BOOT() ((POR_BOOT_VALUE == 7) || (POR_BOOT_VALUE == 6))
#define POR_SPI_BOOT() ((POR_BOOT_VALUE == 5) || (POR_BOOT_VALUE == 4))
#define POR_EMMC_BOOT() (POR_BOOT_VALUE == 3)
#define POR_CARD_BOOT() (POR_BOOT_VALUE == 0)

#define print_tmp(fmt, args...) do{\
		printk("[%s] " fmt, __FUNCTION__, ##args);	\
}while(0)

// P_AO_SECURE_REG1 is "Secure Register 1" in <M8-Secure-AHB-Registers.doc>
#define aml_jtag_gpioao() do{\
    writel(0x102, (u32 *)P_AO_SECURE_REG1); \
}while(0)

#define aml_jtag_sd() do{\
    writel(0x220, (u32 *)P_AO_SECURE_REG1); \
}while(0)

#define aml_uart_pinctrl() do {\
    \
}while(0)

#endif

