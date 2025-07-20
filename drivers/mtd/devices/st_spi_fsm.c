// SPDX-License-Identifier: GPL-2.0-only
/*
 * st_spi_fsm.c	- ST Fast Sequence Mode (FSM) Serial Flash Controller
 *
 * Author: Angus Clark <angus.clark@st.com>
 *
 * Copyright (C) 2010-2014 STMicroelectronics Limited
 *
 * JEDEC probe based on drivers/mtd/devices/m25p80.c
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>

#include "serial_flash_cmds.h"

/*
 * FSM SPI Controller Registers
 */
#define SPI_CLOCKDIV			0x0010
#define SPI_MODESELECT			0x0018
#define SPI_CONFIGDATA			0x0020
#define SPI_STA_MODE_CHANGE		0x0028
#define SPI_FAST_SEQ_TRANSFER_SIZE	0x0100
#define SPI_FAST_SEQ_ADD1		0x0104
#define SPI_FAST_SEQ_ADD2		0x0108
#define SPI_FAST_SEQ_ADD_CFG		0x010c
#define SPI_FAST_SEQ_OPC1		0x0110
#define SPI_FAST_SEQ_OPC2		0x0114
#define SPI_FAST_SEQ_OPC3		0x0118
#define SPI_FAST_SEQ_OPC4		0x011c
#define SPI_FAST_SEQ_OPC5		0x0120
#define SPI_MODE_BITS			0x0124
#define SPI_DUMMY_BITS			0x0128
#define SPI_FAST_SEQ_FLASH_STA_DATA	0x012c
#define SPI_FAST_SEQ_1			0x0130
#define SPI_FAST_SEQ_2			0x0134
#define SPI_FAST_SEQ_3			0x0138
#define SPI_FAST_SEQ_4			0x013c
#define SPI_FAST_SEQ_CFG		0x0140
#define SPI_FAST_SEQ_STA		0x0144
#define SPI_QUAD_BOOT_SEQ_INIT_1	0x0148
#define SPI_QUAD_BOOT_SEQ_INIT_2	0x014c
#define SPI_QUAD_BOOT_READ_SEQ_1	0x0150
#define SPI_QUAD_BOOT_READ_SEQ_2	0x0154
#define SPI_PROGRAM_ERASE_TIME		0x0158
#define SPI_MULT_PAGE_REPEAT_SEQ_1	0x015c
#define SPI_MULT_PAGE_REPEAT_SEQ_2	0x0160
#define SPI_STATUS_WR_TIME_REG		0x0164
#define SPI_FAST_SEQ_DATA_REG		0x0300

/*
 * Register: SPI_MODESELECT
 */
#define SPI_MODESELECT_CONTIG		0x01
#define SPI_MODESELECT_FASTREAD		0x02
#define SPI_MODESELECT_DUALIO		0x04
#define SPI_MODESELECT_FSM		0x08
#define SPI_MODESELECT_QUADBOOT		0x10

/*
 * Register: SPI_CONFIGDATA
 */
#define SPI_CFG_DEVICE_ST		0x1
#define SPI_CFG_DEVICE_ATMEL		0x4
#define SPI_CFG_MIN_CS_HIGH(x)		(((x) & 0xfff) << 4)
#define SPI_CFG_CS_SETUPHOLD(x)		(((x) & 0xff) << 16)
#define SPI_CFG_DATA_HOLD(x)		(((x) & 0xff) << 24)

#define SPI_CFG_DEFAULT_MIN_CS_HIGH    SPI_CFG_MIN_CS_HIGH(0x0AA)
#define SPI_CFG_DEFAULT_CS_SETUPHOLD   SPI_CFG_CS_SETUPHOLD(0xA0)
#define SPI_CFG_DEFAULT_DATA_HOLD      SPI_CFG_DATA_HOLD(0x00)

/*
 * Register: SPI_FAST_SEQ_TRANSFER_SIZE
 */
#define TRANSFER_SIZE(x)		((x) * 8)

/*
 * Register: SPI_FAST_SEQ_ADD_CFG
 */
#define ADR_CFG_CYCLES_ADD1(x)		((x) << 0)
#define ADR_CFG_PADS_1_ADD1		(0x0 << 6)
#define ADR_CFG_PADS_2_ADD1		(0x1 << 6)
#define ADR_CFG_PADS_4_ADD1		(0x3 << 6)
#define ADR_CFG_CSDEASSERT_ADD1		(1   << 8)
#define ADR_CFG_CYCLES_ADD2(x)		((x) << (0+16))
#define ADR_CFG_PADS_1_ADD2		(0x0 << (6+16))
#define ADR_CFG_PADS_2_ADD2		(0x1 << (6+16))
#define ADR_CFG_PADS_4_ADD2		(0x3 << (6+16))
#define ADR_CFG_CSDEASSERT_ADD2		(1   << (8+16))

/*
 * Register: SPI_FAST_SEQ_n
 */
#define SEQ_OPC_OPCODE(x)		((x) << 0)
#define SEQ_OPC_CYCLES(x)		((x) << 8)
#define SEQ_OPC_PADS_1			(0x0 << 14)
#define SEQ_OPC_PADS_2			(0x1 << 14)
#define SEQ_OPC_PADS_4			(0x3 << 14)
#define SEQ_OPC_CSDEASSERT		(1   << 16)

/*
 * Register: SPI_FAST_SEQ_CFG
 */
#define SEQ_CFG_STARTSEQ		(1 << 0)
#define SEQ_CFG_SWRESET			(1 << 5)
#define SEQ_CFG_CSDEASSERT		(1 << 6)
#define SEQ_CFG_READNOTWRITE		(1 << 7)
#define SEQ_CFG_ERASE			(1 << 8)
#define SEQ_CFG_PADS_1			(0x0 << 16)
#define SEQ_CFG_PADS_2			(0x1 << 16)
#define SEQ_CFG_PADS_4			(0x3 << 16)

/*
 * Register: SPI_MODE_BITS
 */
#define MODE_DATA(x)			(x & 0xff)
#define MODE_CYCLES(x)			((x & 0x3f) << 16)
#define MODE_PADS_1			(0x0 << 22)
#define MODE_PADS_2			(0x1 << 22)
#define MODE_PADS_4			(0x3 << 22)
#define DUMMY_CSDEASSERT		(1   << 24)

/*
 * Register: SPI_DUMMY_BITS
 */
#define DUMMY_CYCLES(x)			((x & 0x3f) << 16)
#define DUMMY_PADS_1			(0x0 << 22)
#define DUMMY_PADS_2			(0x1 << 22)
#define DUMMY_PADS_4			(0x3 << 22)
#define DUMMY_CSDEASSERT		(1   << 24)

/*
 * Register: SPI_FAST_SEQ_FLASH_STA_DATA
 */
#define STA_DATA_BYTE1(x)		((x & 0xff) << 0)
#define STA_DATA_BYTE2(x)		((x & 0xff) << 8)
#define STA_PADS_1			(0x0 << 16)
#define STA_PADS_2			(0x1 << 16)
#define STA_PADS_4			(0x3 << 16)
#define STA_CSDEASSERT			(0x1 << 20)
#define STA_RDNOTWR			(0x1 << 21)

/*
 * FSM SPI Instruction Opcodes
 */
#define STFSM_OPC_CMD			0x1
#define STFSM_OPC_ADD			0x2
#define STFSM_OPC_STA			0x3
#define STFSM_OPC_MODE			0x4
#define STFSM_OPC_DUMMY		0x5
#define STFSM_OPC_DATA			0x6
#define STFSM_OPC_WAIT			0x7
#define STFSM_OPC_JUMP			0x8
#define STFSM_OPC_GOTO			0x9
#define STFSM_OPC_STOP			0xF

/*
 * FSM SPI Instructions (== opcode + operand).
 */
#define STFSM_INSTR(cmd, op)		((cmd) | ((op) << 4))

#define STFSM_INST_CMD1			STFSM_INSTR(STFSM_OPC_CMD,	1)
#define STFSM_INST_CMD2			STFSM_INSTR(STFSM_OPC_CMD,	2)
#define STFSM_INST_CMD3			STFSM_INSTR(STFSM_OPC_CMD,	3)
#define STFSM_INST_CMD4			STFSM_INSTR(STFSM_OPC_CMD,	4)
#define STFSM_INST_CMD5			STFSM_INSTR(STFSM_OPC_CMD,	5)
#define STFSM_INST_ADD1			STFSM_INSTR(STFSM_OPC_ADD,	1)
#define STFSM_INST_ADD2			STFSM_INSTR(STFSM_OPC_ADD,	2)

#define STFSM_INST_DATA_WRITE		STFSM_INSTR(STFSM_OPC_DATA,	1)
#define STFSM_INST_DATA_READ		STFSM_INSTR(STFSM_OPC_DATA,	2)

#define STFSM_INST_STA_RD1		STFSM_INSTR(STFSM_OPC_STA,	0x1)
#define STFSM_INST_STA_WR1		STFSM_INSTR(STFSM_OPC_STA,	0x1)
#define STFSM_INST_STA_RD2		STFSM_INSTR(STFSM_OPC_STA,	0x2)
#define STFSM_INST_STA_WR1_2		STFSM_INSTR(STFSM_OPC_STA,	0x3)

#define STFSM_INST_MODE			STFSM_INSTR(STFSM_OPC_MODE,	0)
#define STFSM_INST_DUMMY		STFSM_INSTR(STFSM_OPC_DUMMY,	0)
#define STFSM_INST_WAIT			STFSM_INSTR(STFSM_OPC_WAIT,	0)
#define STFSM_INST_STOP			STFSM_INSTR(STFSM_OPC_STOP,	0)

#define STFSM_DEFAULT_EMI_FREQ 100000000UL                        /* 100 MHz */
#define STFSM_DEFAULT_WR_TIME  (STFSM_DEFAULT_EMI_FREQ * (15/1000)) /* 15ms */

#define STFSM_FLASH_SAFE_FREQ  10000000UL                         /* 10 MHz */

#define STFSM_MAX_WAIT_SEQ_MS  1000     /* FSM execution time */

/* S25FLxxxS commands */
#define S25FL_CMD_WRITE4_1_1_4 0x34
#define S25FL_CMD_SE4          0xdc
#define S25FL_CMD_CLSR         0x30
#define S25FL_CMD_DYBWR                0xe1
#define S25FL_CMD_DYBRD                0xe0
#define S25FL_CMD_WRITE4       0x12    /* Note, opcode clashes with
					* 'SPINOR_OP_WRITE_1_4_4'
					* as found on N25Qxxx devices! */

/* Status register */
#define FLASH_STATUS_BUSY      0x01
#define FLASH_STATUS_WEL       0x02
#define FLASH_STATUS_BP0       0x04
#define FLASH_STATUS_BP1       0x08
#define FLASH_STATUS_BP2       0x10
#define FLASH_STATUS_SRWP0     0x80
#define FLASH_STATUS_TIMEOUT   0xff
/* S25FL Error Flags */
#define S25FL_STATUS_E_ERR     0x20
#define S25FL_STATUS_P_ERR     0x40

#define N25Q_CMD_WRVCR         0x81
#define N25Q_CMD_RDVCR         0x85
#define N25Q_CMD_RDVECR        0x65
#define N25Q_CMD_RDNVCR        0xb5
#define N25Q_CMD_WRNVCR        0xb1

#define FLASH_PAGESIZE         256			/* In Bytes    */
#define FLASH_PAGESIZE_32      (FLASH_PAGESIZE / 4)	/* In uint32_t */
#define FLASH_MAX_BUSY_WAIT    (300 * HZ)	/* Maximum 'CHIPERASE' time */

/*
 * Flags to tweak operation of default read/write/erase routines
 */
#define CFG_READ_TOGGLE_32BIT_ADDR     0x00000001
#define CFG_WRITE_TOGGLE_32BIT_ADDR    0x00000002
#define CFG_ERASESEC_TOGGLE_32BIT_ADDR 0x00000008
#define CFG_S25FL_CHECK_ERROR_FLAGS    0x00000010

struct stfsm_seq {
	uint32_t data_size;
	uint32_t addr1;
	uint32_t addr2;
	uint32_t addr_cfg;
	uint32_t seq_opc[5];
	uint32_t mode;
	uint32_t dummy;
	uint32_t status;
	uint8_t  seq[16];
	uint32_t seq_cfg;
} __packed __aligned(4);

struct stfsm {
	struct device		*dev;
	void __iomem		*base;
	struct mtd_info		mtd;
	struct mutex		lock;
	struct flash_info       *info;
	struct clk              *clk;

	uint32_t                configuration;
	uint32_t                fifo_dir_delay;
	bool                    booted_from_spi;
	bool                    reset_signal;
	bool                    reset_por;

	struct stfsm_seq stfsm_seq_read;
	struct stfsm_seq stfsm_seq_write;
	struct stfsm_seq stfsm_seq_en_32bit_addr;
};

/* Parameters to configure a READ or WRITE FSM sequence */
struct seq_rw_config {
	uint32_t        flags;          /* flags to support config */
	uint8_t         cmd;            /* FLASH command */
	int             write;          /* Write Sequence */
	uint8_t         addr_pads;      /* No. of addr pads (MODE & DUMMY) */
	uint8_t         data_pads;      /* No. of data pads */
	uint8_t         mode_data;      /* MODE data */
	uint8_t         mode_cycles;    /* No. of MODE cycles */
	uint8_t         dummy_cycles;   /* No. of DUMMY cycles */
};

/* SPI Flash Device Table */
struct flash_info {
	char            *name;
	/*
	 * JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32             jedec_id;
	u16             ext_id;
	/*
	 * The size listed here is what works with SPINOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned        sector_size;
	u16             n_sectors;
	u32             flags;
	/*
	 * Note, where FAST_READ is supported, freq_max specifies the
	 * FAST_READ frequency, not the READ frequency.
	 */
	u32             max_freq;
	int             (*config)(struct stfsm *);
};

static int stfsm_n25q_config(struct stfsm *fsm);
static int stfsm_mx25_config(struct stfsm *fsm);
static int stfsm_s25fl_config(struct stfsm *fsm);
static int stfsm_w25q_config(struct stfsm *fsm);

static struct flash_info flash_types[] = {
	/*
	 * ST Microelectronics/Numonyx --
	 * (newer production versions may have feature updates
	 * (eg faster operating frequency)
	 */
#define M25P_FLAG (FLASH_FLAG_READ_WRITE | FLASH_FLAG_READ_FAST)
	{ "m25p40",  0x202013, 0,  64 * 1024,   8, M25P_FLAG, 25, NULL },
	{ "m25p80",  0x202014, 0,  64 * 1024,  16, M25P_FLAG, 25, NULL },
	{ "m25p16",  0x202015, 0,  64 * 1024,  32, M25P_FLAG, 25, NULL },
	{ "m25p32",  0x202016, 0,  64 * 1024,  64, M25P_FLAG, 50, NULL },
	{ "m25p64",  0x202017, 0,  64 * 1024, 128, M25P_FLAG, 50, NULL },
	{ "m25p128", 0x202018, 0, 256 * 1024,  64, M25P_FLAG, 50, NULL },

#define M25PX_FLAG (FLASH_FLAG_READ_WRITE      |	\
		    FLASH_FLAG_READ_FAST        |	\
		    FLASH_FLAG_READ_1_1_2       |	\
		    FLASH_FLAG_WRITE_1_1_2)
	{ "m25px32", 0x207116, 0,  64 * 1024,  64, M25PX_FLAG, 75, NULL },
	{ "m25px64", 0x207117, 0,  64 * 1024, 128, M25PX_FLAG, 75, NULL },

	/* Macronix MX25xxx
	 *     - Support for 'FLASH_FLAG_WRITE_1_4_4' is omitted for devices
	 *       where operating frequency must be reduced.
	 */
#define MX25_FLAG (FLASH_FLAG_READ_WRITE       |	\
		   FLASH_FLAG_READ_FAST         |	\
		   FLASH_FLAG_READ_1_1_2        |	\
		   FLASH_FLAG_READ_1_2_2        |	\
		   FLASH_FLAG_READ_1_1_4        |	\
		   FLASH_FLAG_SE_4K             |	\
		   FLASH_FLAG_SE_32K)
	{ "mx25l3255e",  0xc29e16, 0, 64 * 1024, 64,
	  (MX25_FLAG | FLASH_FLAG_WRITE_1_4_4), 86,
	  stfsm_mx25_config},
	{ "mx25l25635e", 0xc22019, 0, 64*1024, 512,
	  (MX25_FLAG | FLASH_FLAG_32BIT_ADDR | FLASH_FLAG_RESET), 70,
	  stfsm_mx25_config },
	{ "mx25l25655e", 0xc22619, 0, 64*1024, 512,
	  (MX25_FLAG | FLASH_FLAG_32BIT_ADDR | FLASH_FLAG_RESET), 70,
	  stfsm_mx25_config},

#define N25Q_FLAG (FLASH_FLAG_READ_WRITE       |	\
		   FLASH_FLAG_READ_FAST         |	\
		   FLASH_FLAG_READ_1_1_2        |	\
		   FLASH_FLAG_READ_1_2_2        |	\
		   FLASH_FLAG_READ_1_1_4        |	\
		   FLASH_FLAG_READ_1_4_4        |	\
		   FLASH_FLAG_WRITE_1_1_2       |	\
		   FLASH_FLAG_WRITE_1_2_2       |	\
		   FLASH_FLAG_WRITE_1_1_4       |	\
		   FLASH_FLAG_WRITE_1_4_4)
	{ "n25q128", 0x20ba18, 0, 64 * 1024,  256, N25Q_FLAG, 108,
	  stfsm_n25q_config },
	{ "n25q256", 0x20ba19, 0, 64 * 1024,  512,
	  N25Q_FLAG | FLASH_FLAG_32BIT_ADDR, 108, stfsm_n25q_config },

	/*
	 * Spansion S25FLxxxP
	 *     - 256KiB and 64KiB sector variants (identified by ext. JEDEC)
	 */
#define S25FLXXXP_FLAG (FLASH_FLAG_READ_WRITE  |	\
			FLASH_FLAG_READ_1_1_2   |	\
			FLASH_FLAG_READ_1_2_2   |	\
			FLASH_FLAG_READ_1_1_4   |	\
			FLASH_FLAG_READ_1_4_4   |	\
			FLASH_FLAG_WRITE_1_1_4  |	\
			FLASH_FLAG_READ_FAST)
	{ "s25fl032p",  0x010215, 0x4d00,  64 * 1024,  64, S25FLXXXP_FLAG, 80,
	  stfsm_s25fl_config},
	{ "s25fl129p0", 0x012018, 0x4d00, 256 * 1024,  64, S25FLXXXP_FLAG, 80,
	  stfsm_s25fl_config },
	{ "s25fl129p1", 0x012018, 0x4d01,  64 * 1024, 256, S25FLXXXP_FLAG, 80,
	  stfsm_s25fl_config },

	/*
	 * Spansion S25FLxxxS
	 *     - 256KiB and 64KiB sector variants (identified by ext. JEDEC)
	 *     - RESET# signal supported by die but not bristled out on all
	 *       package types.  The package type is a function of board design,
	 *       so this information is captured in the board's flags.
	 *     - Supports 'DYB' sector protection. Depending on variant, sectors
	 *       may default to locked state on power-on.
	 */
#define S25FLXXXS_FLAG (S25FLXXXP_FLAG         |	\
			FLASH_FLAG_RESET        |	\
			FLASH_FLAG_DYB_LOCKING)
	{ "s25fl128s0", 0x012018, 0x0300,  256 * 1024, 64, S25FLXXXS_FLAG, 80,
	  stfsm_s25fl_config },
	{ "s25fl128s1", 0x012018, 0x0301,  64 * 1024, 256, S25FLXXXS_FLAG, 80,
	  stfsm_s25fl_config },
	{ "s25fl256s0", 0x010219, 0x4d00, 256 * 1024, 128,
	  S25FLXXXS_FLAG | FLASH_FLAG_32BIT_ADDR, 80, stfsm_s25fl_config },
	{ "s25fl256s1", 0x010219, 0x4d01,  64 * 1024, 512,
	  S25FLXXXS_FLAG | FLASH_FLAG_32BIT_ADDR, 80, stfsm_s25fl_config },

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
#define W25X_FLAG (FLASH_FLAG_READ_WRITE       |	\
		   FLASH_FLAG_READ_FAST         |	\
		   FLASH_FLAG_READ_1_1_2        |	\
		   FLASH_FLAG_WRITE_1_1_2)
	{ "w25x40",  0xef3013, 0,  64 * 1024,   8, W25X_FLAG, 75, NULL },
	{ "w25x80",  0xef3014, 0,  64 * 1024,  16, W25X_FLAG, 75, NULL },
	{ "w25x16",  0xef3015, 0,  64 * 1024,  32, W25X_FLAG, 75, NULL },
	{ "w25x32",  0xef3016, 0,  64 * 1024,  64, W25X_FLAG, 75, NULL },
	{ "w25x64",  0xef3017, 0,  64 * 1024, 128, W25X_FLAG, 75, NULL },

	/* Winbond -- w25q "blocks" are 64K, "sectors" are 4KiB */
#define W25Q_FLAG (FLASH_FLAG_READ_WRITE       |	\
		   FLASH_FLAG_READ_FAST         |	\
		   FLASH_FLAG_READ_1_1_2        |	\
		   FLASH_FLAG_READ_1_2_2        |	\
		   FLASH_FLAG_READ_1_1_4        |	\
		   FLASH_FLAG_READ_1_4_4        |	\
		   FLASH_FLAG_WRITE_1_1_4)
	{ "w25q80",  0xef4014, 0,  64 * 1024,  16, W25Q_FLAG, 80,
	  stfsm_w25q_config },
	{ "w25q16",  0xef4015, 0,  64 * 1024,  32, W25Q_FLAG, 80,
	  stfsm_w25q_config },
	{ "w25q32",  0xef4016, 0,  64 * 1024,  64, W25Q_FLAG, 80,
	  stfsm_w25q_config },
	{ "w25q64",  0xef4017, 0,  64 * 1024, 128, W25Q_FLAG, 80,
	  stfsm_w25q_config },

	/* Sentinel */
	{ NULL, 0x000000, 0, 0, 0, 0, 0, NULL },
};

/*
 * FSM message sequence configurations:
 *
 * All configs are presented in order of preference
 */

/* Default READ configurations, in order of preference */
static struct seq_rw_config default_read_configs[] = {
	{FLASH_FLAG_READ_1_4_4, SPINOR_OP_READ_1_4_4,	0, 4, 4, 0x00, 2, 4},
	{FLASH_FLAG_READ_1_1_4, SPINOR_OP_READ_1_1_4,	0, 1, 4, 0x00, 4, 0},
	{FLASH_FLAG_READ_1_2_2, SPINOR_OP_READ_1_2_2,	0, 2, 2, 0x00, 4, 0},
	{FLASH_FLAG_READ_1_1_2, SPINOR_OP_READ_1_1_2,	0, 1, 2, 0x00, 0, 8},
	{FLASH_FLAG_READ_FAST,	SPINOR_OP_READ_FAST,	0, 1, 1, 0x00, 0, 8},
	{FLASH_FLAG_READ_WRITE, SPINOR_OP_READ,		0, 1, 1, 0x00, 0, 0},
	{0x00,			0,			0, 0, 0, 0x00, 0, 0},
};

/* Default WRITE configurations */
static struct seq_rw_config default_write_configs[] = {
	{FLASH_FLAG_WRITE_1_4_4, SPINOR_OP_WRITE_1_4_4, 1, 4, 4, 0x00, 0, 0},
	{FLASH_FLAG_WRITE_1_1_4, SPINOR_OP_WRITE_1_1_4, 1, 1, 4, 0x00, 0, 0},
	{FLASH_FLAG_WRITE_1_2_2, SPINOR_OP_WRITE_1_2_2, 1, 2, 2, 0x00, 0, 0},
	{FLASH_FLAG_WRITE_1_1_2, SPINOR_OP_WRITE_1_1_2, 1, 1, 2, 0x00, 0, 0},
	{FLASH_FLAG_READ_WRITE,  SPINOR_OP_WRITE,       1, 1, 1, 0x00, 0, 0},
	{0x00,			 0,			0, 0, 0, 0x00, 0, 0},
};

/*
 * [N25Qxxx] Configuration
 */
#define N25Q_VCR_DUMMY_CYCLES(x)	(((x) & 0xf) << 4)
#define N25Q_VCR_XIP_DISABLED		((uint8_t)0x1 << 3)
#define N25Q_VCR_WRAP_CONT		0x3

/* N25Q 3-byte Address READ configurations
 *	- 'FAST' variants configured for 8 dummy cycles.
 *
 * Note, the number of dummy cycles used for 'FAST' READ operations is
 * configurable and would normally be tuned according to the READ command and
 * operating frequency.  However, this applies universally to all 'FAST' READ
 * commands, including those used by the SPIBoot controller, and remains in
 * force until the device is power-cycled.  Since the SPIBoot controller is
 * hard-wired to use 8 dummy cycles, we must configure the device to also use 8
 * cycles.
 */
static struct seq_rw_config n25q_read3_configs[] = {
	{FLASH_FLAG_READ_1_4_4, SPINOR_OP_READ_1_4_4,	0, 4, 4, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_1_4, SPINOR_OP_READ_1_1_4,	0, 1, 4, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_2_2, SPINOR_OP_READ_1_2_2,	0, 2, 2, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_1_2, SPINOR_OP_READ_1_1_2,	0, 1, 2, 0x00, 0, 8},
	{FLASH_FLAG_READ_FAST,	SPINOR_OP_READ_FAST,	0, 1, 1, 0x00, 0, 8},
	{FLASH_FLAG_READ_WRITE, SPINOR_OP_READ,	        0, 1, 1, 0x00, 0, 0},
	{0x00,			0,			0, 0, 0, 0x00, 0, 0},
};

/* N25Q 4-byte Address READ configurations
 *	- use special 4-byte address READ commands (reduces overheads, and
 *        reduces risk of hitting watchdog reset issues).
 *	- 'FAST' variants configured for 8 dummy cycles (see note above.)
 */
static struct seq_rw_config n25q_read4_configs[] = {
	{FLASH_FLAG_READ_1_4_4, SPINOR_OP_READ_1_4_4_4B, 0, 4, 4, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_1_4, SPINOR_OP_READ_1_1_4_4B, 0, 1, 4, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_2_2, SPINOR_OP_READ_1_2_2_4B, 0, 2, 2, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_1_2, SPINOR_OP_READ_1_1_2_4B, 0, 1, 2, 0x00, 0, 8},
	{FLASH_FLAG_READ_FAST,	SPINOR_OP_READ_FAST_4B,  0, 1, 1, 0x00, 0, 8},
	{FLASH_FLAG_READ_WRITE, SPINOR_OP_READ_4B,       0, 1, 1, 0x00, 0, 0},
	{0x00,			0,                       0, 0, 0, 0x00, 0, 0},
};

/*
 * [MX25xxx] Configuration
 */
#define MX25_STATUS_QE			(0x1 << 6)

static int stfsm_mx25_en_32bit_addr_seq(struct stfsm_seq *seq)
{
	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(SPINOR_OP_EN4B) |
			   SEQ_OPC_CSDEASSERT);

	seq->seq[0] = STFSM_INST_CMD1;
	seq->seq[1] = STFSM_INST_WAIT;
	seq->seq[2] = STFSM_INST_STOP;

	seq->seq_cfg = (SEQ_CFG_PADS_1 |
			SEQ_CFG_ERASE |
			SEQ_CFG_READNOTWRITE |
			SEQ_CFG_CSDEASSERT |
			SEQ_CFG_STARTSEQ);

	return 0;
}

/*
 * [S25FLxxx] Configuration
 */
#define STFSM_S25FL_CONFIG_QE		(0x1 << 1)

/*
 * S25FLxxxS devices provide three ways of supporting 32-bit addressing: Bank
 * Register, Extended Address Modes, and a 32-bit address command set.  The
 * 32-bit address command set is used here, since it avoids any problems with
 * entering a state that is incompatible with the SPIBoot Controller.
 */
static struct seq_rw_config stfsm_s25fl_read4_configs[] = {
	{FLASH_FLAG_READ_1_4_4,  SPINOR_OP_READ_1_4_4_4B,  0, 4, 4, 0x00, 2, 4},
	{FLASH_FLAG_READ_1_1_4,  SPINOR_OP_READ_1_1_4_4B,  0, 1, 4, 0x00, 0, 8},
	{FLASH_FLAG_READ_1_2_2,  SPINOR_OP_READ_1_2_2_4B,  0, 2, 2, 0x00, 4, 0},
	{FLASH_FLAG_READ_1_1_2,  SPINOR_OP_READ_1_1_2_4B,  0, 1, 2, 0x00, 0, 8},
	{FLASH_FLAG_READ_FAST,   SPINOR_OP_READ_FAST_4B,   0, 1, 1, 0x00, 0, 8},
	{FLASH_FLAG_READ_WRITE,  SPINOR_OP_READ_4B,        0, 1, 1, 0x00, 0, 0},
	{0x00,                   0,                        0, 0, 0, 0x00, 0, 0},
};

static struct seq_rw_config stfsm_s25fl_write4_configs[] = {
	{FLASH_FLAG_WRITE_1_1_4, S25FL_CMD_WRITE4_1_1_4, 1, 1, 4, 0x00, 0, 0},
	{FLASH_FLAG_READ_WRITE,  S25FL_CMD_WRITE4,       1, 1, 1, 0x00, 0, 0},
	{0x00,                   0,                      0, 0, 0, 0x00, 0, 0},
};

/*
 * [W25Qxxx] Configuration
 */
#define W25Q_STATUS_QE			(0x1 << 1)

static struct stfsm_seq stfsm_seq_read_jedec = {
	.data_size = TRANSFER_SIZE(8),
	.seq_opc[0] = (SEQ_OPC_PADS_1 |
		       SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(SPINOR_OP_RDID)),
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_DATA_READ,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct stfsm_seq stfsm_seq_read_status_fifo = {
	.data_size = TRANSFER_SIZE(4),
	.seq_opc[0] = (SEQ_OPC_PADS_1 |
		       SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(SPINOR_OP_RDSR)),
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_DATA_READ,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct stfsm_seq stfsm_seq_erase_sector = {
	/* 'addr_cfg' configured during initialisation */
	.seq_opc = {
		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(SPINOR_OP_WREN) | SEQ_OPC_CSDEASSERT),

		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(SPINOR_OP_SE)),
	},
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_CMD2,
		STFSM_INST_ADD1,
		STFSM_INST_ADD2,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct stfsm_seq stfsm_seq_erase_chip = {
	.seq_opc = {
		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(SPINOR_OP_WREN) | SEQ_OPC_CSDEASSERT),

		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(SPINOR_OP_CHIP_ERASE) | SEQ_OPC_CSDEASSERT),
	},
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_CMD2,
		STFSM_INST_WAIT,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_ERASE |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct stfsm_seq stfsm_seq_write_status = {
	.seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(SPINOR_OP_WREN) | SEQ_OPC_CSDEASSERT),
	.seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(SPINOR_OP_WRSR)),
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_CMD2,
		STFSM_INST_STA_WR1,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

/* Dummy sequence to read one byte of data from flash into the FIFO */
static const struct stfsm_seq stfsm_seq_load_fifo_byte = {
	.data_size = TRANSFER_SIZE(1),
	.seq_opc[0] = (SEQ_OPC_PADS_1 |
		       SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(SPINOR_OP_RDID)),
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_DATA_READ,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static int stfsm_n25q_en_32bit_addr_seq(struct stfsm_seq *seq)
{
	seq->seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(SPINOR_OP_EN4B));
	seq->seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(SPINOR_OP_WREN) |
			   SEQ_OPC_CSDEASSERT);

	seq->seq[0] = STFSM_INST_CMD2;
	seq->seq[1] = STFSM_INST_CMD1;
	seq->seq[2] = STFSM_INST_WAIT;
	seq->seq[3] = STFSM_INST_STOP;

	seq->seq_cfg = (SEQ_CFG_PADS_1 |
			SEQ_CFG_ERASE |
			SEQ_CFG_READNOTWRITE |
			SEQ_CFG_CSDEASSERT |
			SEQ_CFG_STARTSEQ);

	return 0;
}

static inline int stfsm_is_idle(struct stfsm *fsm)
{
	return readl(fsm->base + SPI_FAST_SEQ_STA) & 0x10;
}

static inline uint32_t stfsm_fifo_available(struct stfsm *fsm)
{
	return (readl(fsm->base + SPI_FAST_SEQ_STA) >> 5) & 0x7f;
}

static inline void stfsm_load_seq(struct stfsm *fsm,
				  const struct stfsm_seq *seq)
{
	void __iomem *dst = fsm->base + SPI_FAST_SEQ_TRANSFER_SIZE;
	const uint32_t *src = (const uint32_t *)seq;
	int words = sizeof(*seq) / sizeof(*src);

	BUG_ON(!stfsm_is_idle(fsm));

	while (words--) {
		writel(*src, dst);
		src++;
		dst += 4;
	}
}

static void stfsm_wait_seq(struct stfsm *fsm)
{
	unsigned long deadline;
	int timeout = 0;

	deadline = jiffies + msecs_to_jiffies(STFSM_MAX_WAIT_SEQ_MS);

	while (!timeout) {
		if (time_after_eq(jiffies, deadline))
			timeout = 1;

		if (stfsm_is_idle(fsm))
			return;

		cond_resched();
	}

	dev_err(fsm->dev, "timeout on sequence completion\n");
}

static void stfsm_read_fifo(struct stfsm *fsm, uint32_t *buf, uint32_t size)
{
	uint32_t remaining = size >> 2;
	uint32_t avail;
	uint32_t words;

	dev_dbg(fsm->dev, "Reading %d bytes from FIFO\n", size);

	BUG_ON((((uintptr_t)buf) & 0x3) || (size & 0x3));

	while (remaining) {
		for (;;) {
			avail = stfsm_fifo_available(fsm);
			if (avail)
				break;
			udelay(1);
		}
		words = min(avail, remaining);
		remaining -= words;

		readsl(fsm->base + SPI_FAST_SEQ_DATA_REG, buf, words);
		buf += words;
	}
}

/*
 * Clear the data FIFO
 *
 * Typically, this is only required during driver initialisation, where no
 * assumptions can be made regarding the state of the FIFO.
 *
 * The process of clearing the FIFO is complicated by fact that while it is
 * possible for the FIFO to contain an arbitrary number of bytes [1], the
 * SPI_FAST_SEQ_STA register only reports the number of complete 32-bit words
 * present.  Furthermore, data can only be drained from the FIFO by reading
 * complete 32-bit words.
 *
 * With this in mind, a two stage process is used to the clear the FIFO:
 *
 *     1. Read any complete 32-bit words from the FIFO, as reported by the
 *        SPI_FAST_SEQ_STA register.
 *
 *     2. Mop up any remaining bytes.  At this point, it is not known if there
 *        are 0, 1, 2, or 3 bytes in the FIFO.  To handle all cases, a dummy FSM
 *        sequence is used to load one byte at a time, until a complete 32-bit
 *        word is formed; at most, 4 bytes will need to be loaded.
 *
 * [1] It is theoretically possible for the FIFO to contain an arbitrary number
 *     of bits.  However, since there are no known use-cases that leave
 *     incomplete bytes in the FIFO, only words and bytes are considered here.
 */
static void stfsm_clear_fifo(struct stfsm *fsm)
{
	const struct stfsm_seq *seq = &stfsm_seq_load_fifo_byte;
	uint32_t words, i;

	/* 1. Clear any 32-bit words */
	words = stfsm_fifo_available(fsm);
	if (words) {
		for (i = 0; i < words; i++)
			readl(fsm->base + SPI_FAST_SEQ_DATA_REG);
		dev_dbg(fsm->dev, "cleared %d words from FIFO\n", words);
	}

	/*
	 * 2. Clear any remaining bytes
	 *    - Load the FIFO, one byte at a time, until a complete 32-bit word
	 *      is available.
	 */
	for (i = 0, words = 0; i < 4 && !words; i++) {
		stfsm_load_seq(fsm, seq);
		stfsm_wait_seq(fsm);
		words = stfsm_fifo_available(fsm);
	}

	/*    - A single word must be available now */
	if (words != 1) {
		dev_err(fsm->dev, "failed to clear bytes from the data FIFO\n");
		return;
	}

	/*    - Read the 32-bit word */
	readl(fsm->base + SPI_FAST_SEQ_DATA_REG);

	dev_dbg(fsm->dev, "cleared %d byte(s) from the data FIFO\n", 4 - i);
}

static int stfsm_write_fifo(struct stfsm *fsm, const uint32_t *buf,
			    uint32_t size)
{
	uint32_t words = size >> 2;

	dev_dbg(fsm->dev, "writing %d bytes to FIFO\n", size);

	BUG_ON((((uintptr_t)buf) & 0x3) || (size & 0x3));

	writesl(fsm->base + SPI_FAST_SEQ_DATA_REG, buf, words);

	return size;
}

static int stfsm_enter_32bit_addr(struct stfsm *fsm, int enter)
{
	struct stfsm_seq *seq = &fsm->stfsm_seq_en_32bit_addr;
	uint32_t cmd = enter ? SPINOR_OP_EN4B : SPINOR_OP_EX4B;

	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(cmd) |
			   SEQ_OPC_CSDEASSERT);

	stfsm_load_seq(fsm, seq);

	stfsm_wait_seq(fsm);

	return 0;
}

static uint8_t stfsm_wait_busy(struct stfsm *fsm)
{
	struct stfsm_seq *seq = &stfsm_seq_read_status_fifo;
	unsigned long deadline;
	uint32_t status;
	int timeout = 0;

	/* Use RDRS1 */
	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(SPINOR_OP_RDSR));

	/* Load read_status sequence */
	stfsm_load_seq(fsm, seq);

	/*
	 * Repeat until busy bit is deasserted, or timeout, or error (S25FLxxxS)
	 */
	deadline = jiffies + FLASH_MAX_BUSY_WAIT;
	while (!timeout) {
		if (time_after_eq(jiffies, deadline))
			timeout = 1;

		stfsm_wait_seq(fsm);

		stfsm_read_fifo(fsm, &status, 4);

		if ((status & FLASH_STATUS_BUSY) == 0)
			return 0;

		if ((fsm->configuration & CFG_S25FL_CHECK_ERROR_FLAGS) &&
		    ((status & S25FL_STATUS_P_ERR) ||
		     (status & S25FL_STATUS_E_ERR)))
			return (uint8_t)(status & 0xff);

		if (!timeout)
			/* Restart */
			writel(seq->seq_cfg, fsm->base + SPI_FAST_SEQ_CFG);

		cond_resched();
	}

	dev_err(fsm->dev, "timeout on wait_busy\n");

	return FLASH_STATUS_TIMEOUT;
}

static int stfsm_read_status(struct stfsm *fsm, uint8_t cmd,
			     uint8_t *data, int bytes)
{
	struct stfsm_seq *seq = &stfsm_seq_read_status_fifo;
	uint32_t tmp;
	uint8_t *t = (uint8_t *)&tmp;
	int i;

	dev_dbg(fsm->dev, "read 'status' register [0x%02x], %d byte(s)\n",
		cmd, bytes);

	BUG_ON(bytes != 1 && bytes != 2);

	seq->seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(cmd));

	stfsm_load_seq(fsm, seq);

	stfsm_read_fifo(fsm, &tmp, 4);

	for (i = 0; i < bytes; i++)
		data[i] = t[i];

	stfsm_wait_seq(fsm);

	return 0;
}

static int stfsm_write_status(struct stfsm *fsm, uint8_t cmd,
			    uint16_t data, int bytes, int wait_busy)
{
	struct stfsm_seq *seq = &stfsm_seq_write_status;

	dev_dbg(fsm->dev,
		"write 'status' register [0x%02x], %d byte(s), 0x%04x\n"
		" %s wait-busy\n", cmd, bytes, data, wait_busy ? "with" : "no");

	BUG_ON(bytes != 1 && bytes != 2);

	seq->seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(cmd));

	seq->status = (uint32_t)data | STA_PADS_1 | STA_CSDEASSERT;
	seq->seq[2] = (bytes == 1) ? STFSM_INST_STA_WR1 : STFSM_INST_STA_WR1_2;

	stfsm_load_seq(fsm, seq);

	stfsm_wait_seq(fsm);

	if (wait_busy)
		stfsm_wait_busy(fsm);

	return 0;
}

/*
 * SoC reset on 'boot-from-spi' systems
 *
 * Certain modes of operation cause the Flash device to enter a particular state
 * for a period of time (e.g. 'Erase Sector', 'Quad Enable', and 'Enter 32-bit
 * Addr' commands).  On boot-from-spi systems, it is important to consider what
 * happens if a warm reset occurs during this period.  The SPIBoot controller
 * assumes that Flash device is in its default reset state, 24-bit address mode,
 * and ready to accept commands.  This can be achieved using some form of
 * on-board logic/controller to force a device POR in response to a SoC-level
 * reset or by making use of the device reset signal if available (limited
 * number of devices only).
 *
 * Failure to take such precautions can cause problems following a warm reset.
 * For some operations (e.g. ERASE), there is little that can be done.  For
 * other modes of operation (e.g. 32-bit addressing), options are often
 * available that can help minimise the window in which a reset could cause a
 * problem.
 *
 */
static bool stfsm_can_handle_soc_reset(struct stfsm *fsm)
{
	/* Reset signal is available on the board and supported by the device */
	if (fsm->reset_signal && fsm->info->flags & FLASH_FLAG_RESET)
		return true;

	/* Board-level logic forces a power-on-reset */
	if (fsm->reset_por)
		return true;

	/* Reset is not properly handled and may result in failure to reboot */
	return false;
}

/* Configure 'addr_cfg' according to addressing mode */
static void stfsm_prepare_erasesec_seq(struct stfsm *fsm,
				       struct stfsm_seq *seq)
{
	int addr1_cycles = fsm->info->flags & FLASH_FLAG_32BIT_ADDR ? 16 : 8;

	seq->addr_cfg = (ADR_CFG_CYCLES_ADD1(addr1_cycles) |
			 ADR_CFG_PADS_1_ADD1 |
			 ADR_CFG_CYCLES_ADD2(16) |
			 ADR_CFG_PADS_1_ADD2 |
			 ADR_CFG_CSDEASSERT_ADD2);
}

/* Search for preferred configuration based on available flags */
static struct seq_rw_config *
stfsm_search_seq_rw_configs(struct stfsm *fsm,
			    struct seq_rw_config cfgs[])
{
	struct seq_rw_config *config;
	int flags = fsm->info->flags;

	for (config = cfgs; config->cmd != 0; config++)
		if ((config->flags & flags) == config->flags)
			return config;

	return NULL;
}

/* Prepare a READ/WRITE sequence according to configuration parameters */
static void stfsm_prepare_rw_seq(struct stfsm *fsm,
				 struct stfsm_seq *seq,
				 struct seq_rw_config *cfg)
{
	int addr1_cycles, addr2_cycles;
	int i = 0;

	memset(seq, 0, sizeof(*seq));

	/* Add READ/WRITE OPC  */
	seq->seq_opc[i++] = (SEQ_OPC_PADS_1 |
			     SEQ_OPC_CYCLES(8) |
			     SEQ_OPC_OPCODE(cfg->cmd));

	/* Add WREN OPC for a WRITE sequence */
	if (cfg->write)
		seq->seq_opc[i++] = (SEQ_OPC_PADS_1 |
				     SEQ_OPC_CYCLES(8) |
				     SEQ_OPC_OPCODE(SPINOR_OP_WREN) |
				     SEQ_OPC_CSDEASSERT);

	/* Address configuration (24 or 32-bit addresses) */
	addr1_cycles  = (fsm->info->flags & FLASH_FLAG_32BIT_ADDR) ? 16 : 8;
	addr1_cycles /= cfg->addr_pads;
	addr2_cycles  = 16 / cfg->addr_pads;
	seq->addr_cfg = ((addr1_cycles & 0x3f) << 0 |	/* ADD1 cycles */
			 (cfg->addr_pads - 1) << 6 |	/* ADD1 pads */
			 (addr2_cycles & 0x3f) << 16 |	/* ADD2 cycles */
			 ((cfg->addr_pads - 1) << 22));	/* ADD2 pads */

	/* Data/Sequence configuration */
	seq->seq_cfg = ((cfg->data_pads - 1) << 16 |
			SEQ_CFG_STARTSEQ |
			SEQ_CFG_CSDEASSERT);
	if (!cfg->write)
		seq->seq_cfg |= SEQ_CFG_READNOTWRITE;

	/* Mode configuration (no. of pads taken from addr cfg) */
	seq->mode = ((cfg->mode_data & 0xff) << 0 |	/* data */
		     (cfg->mode_cycles & 0x3f) << 16 |	/* cycles */
		     (cfg->addr_pads - 1) << 22);	/* pads */

	/* Dummy configuration (no. of pads taken from addr cfg) */
	seq->dummy = ((cfg->dummy_cycles & 0x3f) << 16 |	/* cycles */
		      (cfg->addr_pads - 1) << 22);		/* pads */


	/* Instruction sequence */
	i = 0;
	if (cfg->write)
		seq->seq[i++] = STFSM_INST_CMD2;

	seq->seq[i++] = STFSM_INST_CMD1;

	seq->seq[i++] = STFSM_INST_ADD1;
	seq->seq[i++] = STFSM_INST_ADD2;

	if (cfg->mode_cycles)
		seq->seq[i++] = STFSM_INST_MODE;

	if (cfg->dummy_cycles)
		seq->seq[i++] = STFSM_INST_DUMMY;

	seq->seq[i++] =
		cfg->write ? STFSM_INST_DATA_WRITE : STFSM_INST_DATA_READ;
	seq->seq[i++] = STFSM_INST_STOP;
}

static int stfsm_search_prepare_rw_seq(struct stfsm *fsm,
				       struct stfsm_seq *seq,
				       struct seq_rw_config *cfgs)
{
	struct seq_rw_config *config;

	config = stfsm_search_seq_rw_configs(fsm, cfgs);
	if (!config) {
		dev_err(fsm->dev, "failed to find suitable config\n");
		return -EINVAL;
	}

	stfsm_prepare_rw_seq(fsm, seq, config);

	return 0;
}

/* Prepare a READ/WRITE/ERASE 'default' sequences */
static int stfsm_prepare_rwe_seqs_default(struct stfsm *fsm)
{
	uint32_t flags = fsm->info->flags;
	int ret;

	/* Configure 'READ' sequence */
	ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_read,
					  default_read_configs);
	if (ret) {
		dev_err(fsm->dev,
			"failed to prep READ sequence with flags [0x%08x]\n",
			flags);
		return ret;
	}

	/* Configure 'WRITE' sequence */
	ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_write,
					  default_write_configs);
	if (ret) {
		dev_err(fsm->dev,
			"failed to prep WRITE sequence with flags [0x%08x]\n",
			flags);
		return ret;
	}

	/* Configure 'ERASE_SECTOR' sequence */
	stfsm_prepare_erasesec_seq(fsm, &stfsm_seq_erase_sector);

	return 0;
}

static int stfsm_mx25_config(struct stfsm *fsm)
{
	uint32_t flags = fsm->info->flags;
	uint32_t data_pads;
	uint8_t sta;
	int ret;
	bool soc_reset;

	/*
	 * Use default READ/WRITE sequences
	 */
	ret = stfsm_prepare_rwe_seqs_default(fsm);
	if (ret)
		return ret;

	/*
	 * Configure 32-bit Address Support
	 */
	if (flags & FLASH_FLAG_32BIT_ADDR) {
		/* Configure 'enter_32bitaddr' FSM sequence */
		stfsm_mx25_en_32bit_addr_seq(&fsm->stfsm_seq_en_32bit_addr);

		soc_reset = stfsm_can_handle_soc_reset(fsm);
		if (soc_reset || !fsm->booted_from_spi)
			/* If we can handle SoC resets, we enable 32-bit address
			 * mode pervasively */
			stfsm_enter_32bit_addr(fsm, 1);

		else
			/* Else, enable/disable 32-bit addressing before/after
			 * each operation */
			fsm->configuration = (CFG_READ_TOGGLE_32BIT_ADDR |
					      CFG_WRITE_TOGGLE_32BIT_ADDR |
					      CFG_ERASESEC_TOGGLE_32BIT_ADDR);
	}

	/* Check status of 'QE' bit, update if required. */
	stfsm_read_status(fsm, SPINOR_OP_RDSR, &sta, 1);
	data_pads = ((fsm->stfsm_seq_read.seq_cfg >> 16) & 0x3) + 1;
	if (data_pads == 4) {
		if (!(sta & MX25_STATUS_QE)) {
			/* Set 'QE' */
			sta |= MX25_STATUS_QE;

			stfsm_write_status(fsm, SPINOR_OP_WRSR, sta, 1, 1);
		}
	} else {
		if (sta & MX25_STATUS_QE) {
			/* Clear 'QE' */
			sta &= ~MX25_STATUS_QE;

			stfsm_write_status(fsm, SPINOR_OP_WRSR, sta, 1, 1);
		}
	}

	return 0;
}

static int stfsm_n25q_config(struct stfsm *fsm)
{
	uint32_t flags = fsm->info->flags;
	uint8_t vcr;
	int ret = 0;
	bool soc_reset;

	/* Configure 'READ' sequence */
	if (flags & FLASH_FLAG_32BIT_ADDR)
		ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_read,
						  n25q_read4_configs);
	else
		ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_read,
						  n25q_read3_configs);
	if (ret) {
		dev_err(fsm->dev,
			"failed to prepare READ sequence with flags [0x%08x]\n",
			flags);
		return ret;
	}

	/* Configure 'WRITE' sequence (default configs) */
	ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_write,
					  default_write_configs);
	if (ret) {
		dev_err(fsm->dev,
			"preparing WRITE sequence using flags [0x%08x] failed\n",
			flags);
		return ret;
	}

	/* * Configure 'ERASE_SECTOR' sequence */
	stfsm_prepare_erasesec_seq(fsm, &stfsm_seq_erase_sector);

	/* Configure 32-bit address support */
	if (flags & FLASH_FLAG_32BIT_ADDR) {
		stfsm_n25q_en_32bit_addr_seq(&fsm->stfsm_seq_en_32bit_addr);

		soc_reset = stfsm_can_handle_soc_reset(fsm);
		if (soc_reset || !fsm->booted_from_spi) {
			/*
			 * If we can handle SoC resets, we enable 32-bit
			 * address mode pervasively
			 */
			stfsm_enter_32bit_addr(fsm, 1);
		} else {
			/*
			 * If not, enable/disable for WRITE and ERASE
			 * operations (READ uses special commands)
			 */
			fsm->configuration = (CFG_WRITE_TOGGLE_32BIT_ADDR |
					      CFG_ERASESEC_TOGGLE_32BIT_ADDR);
		}
	}

	/*
	 * Configure device to use 8 dummy cycles
	 */
	vcr = (N25Q_VCR_DUMMY_CYCLES(8) | N25Q_VCR_XIP_DISABLED |
	       N25Q_VCR_WRAP_CONT);
	stfsm_write_status(fsm, N25Q_CMD_WRVCR, vcr, 1, 0);

	return 0;
}

static void stfsm_s25fl_prepare_erasesec_seq_32(struct stfsm_seq *seq)
{
	seq->seq_opc[1] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(S25FL_CMD_SE4));

	seq->addr_cfg = (ADR_CFG_CYCLES_ADD1(16) |
			 ADR_CFG_PADS_1_ADD1 |
			 ADR_CFG_CYCLES_ADD2(16) |
			 ADR_CFG_PADS_1_ADD2 |
			 ADR_CFG_CSDEASSERT_ADD2);
}

static void stfsm_s25fl_read_dyb(struct stfsm *fsm, uint32_t offs, uint8_t *dby)
{
	uint32_t tmp;
	struct stfsm_seq seq = {
		.data_size = TRANSFER_SIZE(4),
		.seq_opc[0] = (SEQ_OPC_PADS_1 |
			       SEQ_OPC_CYCLES(8) |
			       SEQ_OPC_OPCODE(S25FL_CMD_DYBRD)),
		.addr_cfg = (ADR_CFG_CYCLES_ADD1(16) |
			     ADR_CFG_PADS_1_ADD1 |
			     ADR_CFG_CYCLES_ADD2(16) |
			     ADR_CFG_PADS_1_ADD2),
		.addr1 = (offs >> 16) & 0xffff,
		.addr2 = offs & 0xffff,
		.seq = {
			STFSM_INST_CMD1,
			STFSM_INST_ADD1,
			STFSM_INST_ADD2,
			STFSM_INST_DATA_READ,
			STFSM_INST_STOP,
		},
		.seq_cfg = (SEQ_CFG_PADS_1 |
			    SEQ_CFG_READNOTWRITE |
			    SEQ_CFG_CSDEASSERT |
			    SEQ_CFG_STARTSEQ),
	};

	stfsm_load_seq(fsm, &seq);

	stfsm_read_fifo(fsm, &tmp, 4);

	*dby = (uint8_t)(tmp >> 24);

	stfsm_wait_seq(fsm);
}

static void stfsm_s25fl_write_dyb(struct stfsm *fsm, uint32_t offs, uint8_t dby)
{
	struct stfsm_seq seq = {
		.seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			       SEQ_OPC_OPCODE(SPINOR_OP_WREN) |
			       SEQ_OPC_CSDEASSERT),
		.seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			       SEQ_OPC_OPCODE(S25FL_CMD_DYBWR)),
		.addr_cfg = (ADR_CFG_CYCLES_ADD1(16) |
			     ADR_CFG_PADS_1_ADD1 |
			     ADR_CFG_CYCLES_ADD2(16) |
			     ADR_CFG_PADS_1_ADD2),
		.status = (uint32_t)dby | STA_PADS_1 | STA_CSDEASSERT,
		.addr1 = (offs >> 16) & 0xffff,
		.addr2 = offs & 0xffff,
		.seq = {
			STFSM_INST_CMD1,
			STFSM_INST_CMD2,
			STFSM_INST_ADD1,
			STFSM_INST_ADD2,
			STFSM_INST_STA_WR1,
			STFSM_INST_STOP,
		},
		.seq_cfg = (SEQ_CFG_PADS_1 |
			    SEQ_CFG_READNOTWRITE |
			    SEQ_CFG_CSDEASSERT |
			    SEQ_CFG_STARTSEQ),
	};

	stfsm_load_seq(fsm, &seq);
	stfsm_wait_seq(fsm);

	stfsm_wait_busy(fsm);
}

static int stfsm_s25fl_clear_status_reg(struct stfsm *fsm)
{
	struct stfsm_seq seq = {
		.seq_opc[0] = (SEQ_OPC_PADS_1 |
			       SEQ_OPC_CYCLES(8) |
			       SEQ_OPC_OPCODE(S25FL_CMD_CLSR) |
			       SEQ_OPC_CSDEASSERT),
		.seq_opc[1] = (SEQ_OPC_PADS_1 |
			       SEQ_OPC_CYCLES(8) |
			       SEQ_OPC_OPCODE(SPINOR_OP_WRDI) |
			       SEQ_OPC_CSDEASSERT),
		.seq = {
			STFSM_INST_CMD1,
			STFSM_INST_CMD2,
			STFSM_INST_WAIT,
			STFSM_INST_STOP,
		},
		.seq_cfg = (SEQ_CFG_PADS_1 |
			    SEQ_CFG_ERASE |
			    SEQ_CFG_READNOTWRITE |
			    SEQ_CFG_CSDEASSERT |
			    SEQ_CFG_STARTSEQ),
	};

	stfsm_load_seq(fsm, &seq);

	stfsm_wait_seq(fsm);

	return 0;
}

static int stfsm_s25fl_config(struct stfsm *fsm)
{
	struct flash_info *info = fsm->info;
	uint32_t flags = info->flags;
	uint32_t data_pads;
	uint32_t offs;
	uint16_t sta_wr;
	uint8_t sr1, cr1, dyb;
	int update_sr = 0;
	int ret;

	if (flags & FLASH_FLAG_32BIT_ADDR) {
		/*
		 * Prepare Read/Write/Erase sequences according to S25FLxxx
		 * 32-bit address command set
		 */
		ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_read,
						  stfsm_s25fl_read4_configs);
		if (ret)
			return ret;

		ret = stfsm_search_prepare_rw_seq(fsm, &fsm->stfsm_seq_write,
						  stfsm_s25fl_write4_configs);
		if (ret)
			return ret;

		stfsm_s25fl_prepare_erasesec_seq_32(&stfsm_seq_erase_sector);

	} else {
		/* Use default configurations for 24-bit addressing */
		ret = stfsm_prepare_rwe_seqs_default(fsm);
		if (ret)
			return ret;
	}

	/*
	 * For devices that support 'DYB' sector locking, check lock status and
	 * unlock sectors if necessary (some variants power-on with sectors
	 * locked by default)
	 */
	if (flags & FLASH_FLAG_DYB_LOCKING) {
		offs = 0;
		for (offs = 0; offs < info->sector_size * info->n_sectors;) {
			stfsm_s25fl_read_dyb(fsm, offs, &dyb);
			if (dyb == 0x00)
				stfsm_s25fl_write_dyb(fsm, offs, 0xff);

			/* Handle bottom/top 4KiB parameter sectors */
			if ((offs < info->sector_size * 2) ||
			    (offs >= (info->sector_size - info->n_sectors * 4)))
				offs += 0x1000;
			else
				offs += 0x10000;
		}
	}

	/* Check status of 'QE' bit, update if required. */
	stfsm_read_status(fsm, SPINOR_OP_RDCR, &cr1, 1);
	data_pads = ((fsm->stfsm_seq_read.seq_cfg >> 16) & 0x3) + 1;
	if (data_pads == 4) {
		if (!(cr1 & STFSM_S25FL_CONFIG_QE)) {
			/* Set 'QE' */
			cr1 |= STFSM_S25FL_CONFIG_QE;

			update_sr = 1;
		}
	} else {
		if (cr1 & STFSM_S25FL_CONFIG_QE) {
			/* Clear 'QE' */
			cr1 &= ~STFSM_S25FL_CONFIG_QE;

			update_sr = 1;
		}
	}
	if (update_sr) {
		stfsm_read_status(fsm, SPINOR_OP_RDSR, &sr1, 1);
		sta_wr = ((uint16_t)cr1  << 8) | sr1;
		stfsm_write_status(fsm, SPINOR_OP_WRSR, sta_wr, 2, 1);
	}

	/*
	 * S25FLxxx devices support Program and Error error flags.
	 * Configure driver to check flags and clear if necessary.
	 */
	fsm->configuration |= CFG_S25FL_CHECK_ERROR_FLAGS;

	return 0;
}

static int stfsm_w25q_config(struct stfsm *fsm)
{
	uint32_t data_pads;
	uint8_t sr1, sr2;
	uint16_t sr_wr;
	int update_sr = 0;
	int ret;

	ret = stfsm_prepare_rwe_seqs_default(fsm);
	if (ret)
		return ret;

	/* Check status of 'QE' bit, update if required. */
	stfsm_read_status(fsm, SPINOR_OP_RDCR, &sr2, 1);
	data_pads = ((fsm->stfsm_seq_read.seq_cfg >> 16) & 0x3) + 1;
	if (data_pads == 4) {
		if (!(sr2 & W25Q_STATUS_QE)) {
			/* Set 'QE' */
			sr2 |= W25Q_STATUS_QE;
			update_sr = 1;
		}
	} else {
		if (sr2 & W25Q_STATUS_QE) {
			/* Clear 'QE' */
			sr2 &= ~W25Q_STATUS_QE;
			update_sr = 1;
		}
	}
	if (update_sr) {
		/* Write status register */
		stfsm_read_status(fsm, SPINOR_OP_RDSR, &sr1, 1);
		sr_wr = ((uint16_t)sr2 << 8) | sr1;
		stfsm_write_status(fsm, SPINOR_OP_WRSR, sr_wr, 2, 1);
	}

	return 0;
}

static int stfsm_read(struct stfsm *fsm, uint8_t *buf, uint32_t size,
		      uint32_t offset)
{
	struct stfsm_seq *seq = &fsm->stfsm_seq_read;
	uint32_t data_pads;
	uint32_t read_mask;
	uint32_t size_ub;
	uint32_t size_lb;
	uint32_t size_mop;
	uint32_t tmp[4];
	uint32_t page_buf[FLASH_PAGESIZE_32];
	uint8_t *p;

	dev_dbg(fsm->dev, "reading %d bytes from 0x%08x\n", size, offset);

	/* Enter 32-bit address mode, if required */
	if (fsm->configuration & CFG_READ_TOGGLE_32BIT_ADDR)
		stfsm_enter_32bit_addr(fsm, 1);

	/* Must read in multiples of 32 cycles (or 32*pads/8 Bytes) */
	data_pads = ((seq->seq_cfg >> 16) & 0x3) + 1;
	read_mask = (data_pads << 2) - 1;

	/* Handle non-aligned buf */
	p = ((uintptr_t)buf & 0x3) ? (uint8_t *)page_buf : buf;

	/* Handle non-aligned size */
	size_ub = (size + read_mask) & ~read_mask;
	size_lb = size & ~read_mask;
	size_mop = size & read_mask;

	seq->data_size = TRANSFER_SIZE(size_ub);
	seq->addr1 = (offset >> 16) & 0xffff;
	seq->addr2 = offset & 0xffff;

	stfsm_load_seq(fsm, seq);

	if (size_lb)
		stfsm_read_fifo(fsm, (uint32_t *)p, size_lb);

	if (size_mop) {
		stfsm_read_fifo(fsm, tmp, read_mask + 1);
		memcpy(p + size_lb, &tmp, size_mop);
	}

	/* Handle non-aligned buf */
	if ((uintptr_t)buf & 0x3)
		memcpy(buf, page_buf, size);

	/* Wait for sequence to finish */
	stfsm_wait_seq(fsm);

	stfsm_clear_fifo(fsm);

	/* Exit 32-bit address mode, if required */
	if (fsm->configuration & CFG_READ_TOGGLE_32BIT_ADDR)
		stfsm_enter_32bit_addr(fsm, 0);

	return 0;
}

static int stfsm_write(struct stfsm *fsm, const uint8_t *buf,
		       uint32_t size, uint32_t offset)
{
	struct stfsm_seq *seq = &fsm->stfsm_seq_write;
	uint32_t data_pads;
	uint32_t write_mask;
	uint32_t size_ub;
	uint32_t size_lb;
	uint32_t size_mop;
	uint32_t tmp[4];
	uint32_t i;
	uint32_t page_buf[FLASH_PAGESIZE_32];
	uint8_t *t = (uint8_t *)&tmp;
	const uint8_t *p;
	int ret;

	dev_dbg(fsm->dev, "writing %d bytes to 0x%08x\n", size, offset);

	/* Enter 32-bit address mode, if required */
	if (fsm->configuration & CFG_WRITE_TOGGLE_32BIT_ADDR)
		stfsm_enter_32bit_addr(fsm, 1);

	/* Must write in multiples of 32 cycles (or 32*pads/8 bytes) */
	data_pads = ((seq->seq_cfg >> 16) & 0x3) + 1;
	write_mask = (data_pads << 2) - 1;

	/* Handle non-aligned buf */
	if ((uintptr_t)buf & 0x3) {
		memcpy(page_buf, buf, size);
		p = (uint8_t *)page_buf;
	} else {
		p = buf;
	}

	/* Handle non-aligned size */
	size_ub = (size + write_mask) & ~write_mask;
	size_lb = size & ~write_mask;
	size_mop = size & write_mask;

	seq->data_size = TRANSFER_SIZE(size_ub);
	seq->addr1 = (offset >> 16) & 0xffff;
	seq->addr2 = offset & 0xffff;

	/* Need to set FIFO to write mode, before writing data to FIFO (see
	 * GNBvb79594)
	 */
	writel(0x00040000, fsm->base + SPI_FAST_SEQ_CFG);

	/*
	 * Before writing data to the FIFO, apply a small delay to allow a
	 * potential change of FIFO direction to complete.
	 */
	if (fsm->fifo_dir_delay == 0)
		readl(fsm->base + SPI_FAST_SEQ_CFG);
	else
		udelay(fsm->fifo_dir_delay);


	/* Write data to FIFO, before starting sequence (see GNBvd79593) */
	if (size_lb) {
		stfsm_write_fifo(fsm, (uint32_t *)p, size_lb);
		p += size_lb;
	}

	/* Handle non-aligned size */
	if (size_mop) {
		memset(t, 0xff, write_mask + 1);	/* fill with 0xff's */
		for (i = 0; i < size_mop; i++)
			t[i] = *p++;

		stfsm_write_fifo(fsm, tmp, write_mask + 1);
	}

	/* Start sequence */
	stfsm_load_seq(fsm, seq);

	/* Wait for sequence to finish */
	stfsm_wait_seq(fsm);

	/* Wait for completion */
	ret = stfsm_wait_busy(fsm);
	if (ret && fsm->configuration & CFG_S25FL_CHECK_ERROR_FLAGS)
		stfsm_s25fl_clear_status_reg(fsm);

	/* Exit 32-bit address mode, if required */
	if (fsm->configuration & CFG_WRITE_TOGGLE_32BIT_ADDR)
		stfsm_enter_32bit_addr(fsm, 0);

	return 0;
}

/*
 * Read an address range from the flash chip. The address range
 * may be any size provided it is within the physical boundaries.
 */
static int stfsm_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
			  size_t *retlen, u_char *buf)
{
	struct stfsm *fsm = dev_get_drvdata(mtd->dev.parent);
	uint32_t bytes;

	dev_dbg(fsm->dev, "%s from 0x%08x, len %zd\n",
		__func__, (u32)from, len);

	mutex_lock(&fsm->lock);

	while (len > 0) {
		bytes = min_t(size_t, len, FLASH_PAGESIZE);

		stfsm_read(fsm, buf, bytes, from);

		buf += bytes;
		from += bytes;
		len -= bytes;

		*retlen += bytes;
	}

	mutex_unlock(&fsm->lock);

	return 0;
}

static int stfsm_erase_sector(struct stfsm *fsm, uint32_t offset)
{
	struct stfsm_seq *seq = &stfsm_seq_erase_sector;
	int ret;

	dev_dbg(fsm->dev, "erasing sector at 0x%08x\n", offset);

	/* Enter 32-bit address mode, if required */
	if (fsm->configuration & CFG_ERASESEC_TOGGLE_32BIT_ADDR)
		stfsm_enter_32bit_addr(fsm, 1);

	seq->addr1 = (offset >> 16) & 0xffff;
	seq->addr2 = offset & 0xffff;

	stfsm_load_seq(fsm, seq);

	stfsm_wait_seq(fsm);

	/* Wait for completion */
	ret = stfsm_wait_busy(fsm);
	if (ret && fsm->configuration & CFG_S25FL_CHECK_ERROR_FLAGS)
		stfsm_s25fl_clear_status_reg(fsm);

	/* Exit 32-bit address mode, if required */
	if (fsm->configuration & CFG_ERASESEC_TOGGLE_32BIT_ADDR)
		stfsm_enter_32bit_addr(fsm, 0);

	return ret;
}

static int stfsm_erase_chip(struct stfsm *fsm)
{
	const struct stfsm_seq *seq = &stfsm_seq_erase_chip;

	dev_dbg(fsm->dev, "erasing chip\n");

	stfsm_load_seq(fsm, seq);

	stfsm_wait_seq(fsm);

	return stfsm_wait_busy(fsm);
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int stfsm_mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			   size_t *retlen, const u_char *buf)
{
	struct stfsm *fsm = dev_get_drvdata(mtd->dev.parent);

	u32 page_offs;
	u32 bytes;
	uint8_t *b = (uint8_t *)buf;
	int ret = 0;

	dev_dbg(fsm->dev, "%s to 0x%08x, len %zd\n", __func__, (u32)to, len);

	/* Offset within page */
	page_offs = to % FLASH_PAGESIZE;

	mutex_lock(&fsm->lock);

	while (len) {
		/* Write up to page boundary */
		bytes = min_t(size_t, FLASH_PAGESIZE - page_offs, len);

		ret = stfsm_write(fsm, b, bytes, to);
		if (ret)
			goto out1;

		b += bytes;
		len -= bytes;
		to += bytes;

		/* We are now page-aligned */
		page_offs = 0;

		*retlen += bytes;

	}

out1:
	mutex_unlock(&fsm->lock);

	return ret;
}

/*
 * Erase an address range on the flash chip. The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int stfsm_mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct stfsm *fsm = dev_get_drvdata(mtd->dev.parent);
	u32 addr, len;
	int ret;

	dev_dbg(fsm->dev, "%s at 0x%llx, len %lld\n", __func__,
		(long long)instr->addr, (long long)instr->len);

	addr = instr->addr;
	len = instr->len;

	mutex_lock(&fsm->lock);

	/* Whole-chip erase? */
	if (len == mtd->size) {
		ret = stfsm_erase_chip(fsm);
		if (ret)
			goto out1;
	} else {
		while (len) {
			ret = stfsm_erase_sector(fsm, addr);
			if (ret)
				goto out1;

			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&fsm->lock);

	return 0;

out1:
	mutex_unlock(&fsm->lock);

	return ret;
}

static void stfsm_read_jedec(struct stfsm *fsm, uint8_t *jedec)
{
	const struct stfsm_seq *seq = &stfsm_seq_read_jedec;
	uint32_t tmp[2];

	stfsm_load_seq(fsm, seq);

	stfsm_read_fifo(fsm, tmp, 8);

	memcpy(jedec, tmp, 5);

	stfsm_wait_seq(fsm);
}

static struct flash_info *stfsm_jedec_probe(struct stfsm *fsm)
{
	struct flash_info	*info;
	u16                     ext_jedec;
	u32			jedec;
	u8			id[5];

	stfsm_read_jedec(fsm, id);

	jedec     = id[0] << 16 | id[1] << 8 | id[2];
	/*
	 * JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here. Supporting some chips might require using it.
	 */
	ext_jedec = id[3] << 8  | id[4];

	dev_dbg(fsm->dev, "JEDEC =  0x%08x [%5ph]\n", jedec, id);

	for (info = flash_types; info->name; info++) {
		if (info->jedec_id == jedec) {
			if (info->ext_id && info->ext_id != ext_jedec)
				continue;
			return info;
		}
	}
	dev_err(fsm->dev, "Unrecognized JEDEC id %06x\n", jedec);

	return NULL;
}

static int stfsm_set_mode(struct stfsm *fsm, uint32_t mode)
{
	int ret, timeout = 10;

	/* Wait for controller to accept mode change */
	while (--timeout) {
		ret = readl(fsm->base + SPI_STA_MODE_CHANGE);
		if (ret & 0x1)
			break;
		udelay(1);
	}

	if (!timeout)
		return -EBUSY;

	writel(mode, fsm->base + SPI_MODESELECT);

	return 0;
}

static void stfsm_set_freq(struct stfsm *fsm, uint32_t spi_freq)
{
	uint32_t emi_freq;
	uint32_t clk_div;

	emi_freq = clk_get_rate(fsm->clk);

	/*
	 * Calculate clk_div - values between 2 and 128
	 * Multiple of 2, rounded up
	 */
	clk_div = 2 * DIV_ROUND_UP(emi_freq, 2 * spi_freq);
	if (clk_div < 2)
		clk_div = 2;
	else if (clk_div > 128)
		clk_div = 128;

	/*
	 * Determine a suitable delay for the IP to complete a change of
	 * direction of the FIFO. The required delay is related to the clock
	 * divider used. The following heuristics are based on empirical tests,
	 * using a 100MHz EMI clock.
	 */
	if (clk_div <= 4)
		fsm->fifo_dir_delay = 0;
	else if (clk_div <= 10)
		fsm->fifo_dir_delay = 1;
	else
		fsm->fifo_dir_delay = DIV_ROUND_UP(clk_div, 10);

	dev_dbg(fsm->dev, "emi_clk = %uHZ, spi_freq = %uHZ, clk_div = %u\n",
		emi_freq, spi_freq, clk_div);

	writel(clk_div, fsm->base + SPI_CLOCKDIV);
}

static int stfsm_init(struct stfsm *fsm)
{
	int ret;

	/* Perform a soft reset of the FSM controller */
	writel(SEQ_CFG_SWRESET, fsm->base + SPI_FAST_SEQ_CFG);
	udelay(1);
	writel(0, fsm->base + SPI_FAST_SEQ_CFG);

	/* Set clock to 'safe' frequency initially */
	stfsm_set_freq(fsm, STFSM_FLASH_SAFE_FREQ);

	/* Switch to FSM */
	ret = stfsm_set_mode(fsm, SPI_MODESELECT_FSM);
	if (ret)
		return ret;

	/* Set timing parameters */
	writel(SPI_CFG_DEVICE_ST            |
	       SPI_CFG_DEFAULT_MIN_CS_HIGH  |
	       SPI_CFG_DEFAULT_CS_SETUPHOLD |
	       SPI_CFG_DEFAULT_DATA_HOLD,
	       fsm->base + SPI_CONFIGDATA);
	writel(STFSM_DEFAULT_WR_TIME, fsm->base + SPI_STATUS_WR_TIME_REG);

	/*
	 * Set the FSM 'WAIT' delay to the minimum workable value.  Note, for
	 * our purposes, the WAIT instruction is used purely to achieve
	 * "sequence validity" rather than actually implement a delay.
	 */
	writel(0x00000001, fsm->base + SPI_PROGRAM_ERASE_TIME);

	/* Clear FIFO, just in case */
	stfsm_clear_fifo(fsm);

	return 0;
}

static void stfsm_fetch_platform_configs(struct platform_device *pdev)
{
	struct stfsm *fsm = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	struct regmap *regmap;
	uint32_t boot_device_reg;
	uint32_t boot_device_spi;
	uint32_t boot_device;     /* Value we read from *boot_device_reg */
	int ret;

	/* Booting from SPI NOR Flash is the default */
	fsm->booted_from_spi = true;

	regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(regmap))
		goto boot_device_fail;

	fsm->reset_signal = of_property_read_bool(np, "st,reset-signal");

	fsm->reset_por = of_property_read_bool(np, "st,reset-por");

	/* Where in the syscon the boot device information lives */
	ret = of_property_read_u32(np, "st,boot-device-reg", &boot_device_reg);
	if (ret)
		goto boot_device_fail;

	/* Boot device value when booted from SPI NOR */
	ret = of_property_read_u32(np, "st,boot-device-spi", &boot_device_spi);
	if (ret)
		goto boot_device_fail;

	ret = regmap_read(regmap, boot_device_reg, &boot_device);
	if (ret)
		goto boot_device_fail;

	if (boot_device != boot_device_spi)
		fsm->booted_from_spi = false;

	return;

boot_device_fail:
	dev_warn(&pdev->dev,
		 "failed to fetch boot device, assuming boot from SPI\n");
}

static int stfsm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct flash_info *info;
	struct stfsm *fsm;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	fsm = devm_kzalloc(&pdev->dev, sizeof(*fsm), GFP_KERNEL);
	if (!fsm)
		return -ENOMEM;

	fsm->dev = &pdev->dev;

	platform_set_drvdata(pdev, fsm);

	fsm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fsm->base))
		return PTR_ERR(fsm->base);

	fsm->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(fsm->clk)) {
		dev_err(fsm->dev, "Couldn't find EMI clock.\n");
		return PTR_ERR(fsm->clk);
	}

	mutex_init(&fsm->lock);

	ret = stfsm_init(fsm);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialise FSM Controller\n");
		return ret;
	}

	stfsm_fetch_platform_configs(pdev);

	/* Detect SPI FLASH device */
	info = stfsm_jedec_probe(fsm);
	if (!info)
		return -ENODEV;
	fsm->info = info;

	/* Use device size to determine address width */
	if (info->sector_size * info->n_sectors > 0x1000000)
		info->flags |= FLASH_FLAG_32BIT_ADDR;

	/*
	 * Configure READ/WRITE/ERASE sequences according to platform and
	 * device flags.
	 */
	if (info->config)
		ret = info->config(fsm);
	else
		ret = stfsm_prepare_rwe_seqs_default(fsm);
	if (ret)
		return ret;

	fsm->mtd.name		= info->name;
	fsm->mtd.dev.parent	= &pdev->dev;
	mtd_set_of_node(&fsm->mtd, np);
	fsm->mtd.type		= MTD_NORFLASH;
	fsm->mtd.writesize	= 4;
	fsm->mtd.writebufsize	= fsm->mtd.writesize;
	fsm->mtd.flags		= MTD_CAP_NORFLASH;
	fsm->mtd.size		= info->sector_size * info->n_sectors;
	fsm->mtd.erasesize	= info->sector_size;

	fsm->mtd._read  = stfsm_mtd_read;
	fsm->mtd._write = stfsm_mtd_write;
	fsm->mtd._erase = stfsm_mtd_erase;

	dev_info(&pdev->dev,
		"Found serial flash device: %s\n"
		" size = %llx (%lldMiB) erasesize = 0x%08x (%uKiB)\n",
		info->name,
		(long long)fsm->mtd.size, (long long)(fsm->mtd.size >> 20),
		fsm->mtd.erasesize, (fsm->mtd.erasesize >> 10));

	return mtd_device_register(&fsm->mtd, NULL, 0);
}

static void stfsm_remove(struct platform_device *pdev)
{
	struct stfsm *fsm = platform_get_drvdata(pdev);

	WARN_ON(mtd_device_unregister(&fsm->mtd));
}

static int stfsmfsm_suspend(struct device *dev)
{
	struct stfsm *fsm = dev_get_drvdata(dev);

	clk_disable_unprepare(fsm->clk);

	return 0;
}

static int stfsmfsm_resume(struct device *dev)
{
	struct stfsm *fsm = dev_get_drvdata(dev);

	return clk_prepare_enable(fsm->clk);
}

static DEFINE_SIMPLE_DEV_PM_OPS(stfsm_pm_ops, stfsmfsm_suspend, stfsmfsm_resume);

static const struct of_device_id stfsm_match[] = {
	{ .compatible = "st,spi-fsm", },
	{},
};
MODULE_DEVICE_TABLE(of, stfsm_match);

static struct platform_driver stfsm_driver = {
	.probe		= stfsm_probe,
	.remove		= stfsm_remove,
	.driver		= {
		.name	= "st-spi-fsm",
		.of_match_table = stfsm_match,
		.pm     = pm_sleep_ptr(&stfsm_pm_ops),
	},
};
module_platform_driver(stfsm_driver);

MODULE_AUTHOR("Angus Clark <angus.clark@st.com>");
MODULE_DESCRIPTION("ST SPI FSM driver");
MODULE_LICENSE("GPL");
