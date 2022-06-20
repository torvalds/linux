// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020, Linaro Limited
 */

#include <dt-bindings/dma/qcom-gpi.h>
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/dma/qcom-gpi-dma.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include "../dmaengine.h"
#include "../virt-dma.h"

#define TRE_TYPE_DMA		0x10
#define TRE_TYPE_GO		0x20
#define TRE_TYPE_CONFIG0	0x22

/* TRE flags */
#define TRE_FLAGS_CHAIN		BIT(0)
#define TRE_FLAGS_IEOB		BIT(8)
#define TRE_FLAGS_IEOT		BIT(9)
#define TRE_FLAGS_BEI		BIT(10)
#define TRE_FLAGS_LINK		BIT(11)
#define TRE_FLAGS_TYPE		GENMASK(23, 16)

/* SPI CONFIG0 WD0 */
#define TRE_SPI_C0_WORD_SZ	GENMASK(4, 0)
#define TRE_SPI_C0_LOOPBACK	BIT(8)
#define TRE_SPI_C0_CS		BIT(11)
#define TRE_SPI_C0_CPHA		BIT(12)
#define TRE_SPI_C0_CPOL		BIT(13)
#define TRE_SPI_C0_TX_PACK	BIT(24)
#define TRE_SPI_C0_RX_PACK	BIT(25)

/* CONFIG0 WD2 */
#define TRE_C0_CLK_DIV		GENMASK(11, 0)
#define TRE_C0_CLK_SRC		GENMASK(19, 16)

/* SPI GO WD0 */
#define TRE_SPI_GO_CMD		GENMASK(4, 0)
#define TRE_SPI_GO_CS		GENMASK(10, 8)
#define TRE_SPI_GO_FRAG		BIT(26)

/* GO WD2 */
#define TRE_RX_LEN		GENMASK(23, 0)

/* I2C Config0 WD0 */
#define TRE_I2C_C0_TLOW		GENMASK(7, 0)
#define TRE_I2C_C0_THIGH	GENMASK(15, 8)
#define TRE_I2C_C0_TCYL		GENMASK(23, 16)
#define TRE_I2C_C0_TX_PACK	BIT(24)
#define TRE_I2C_C0_RX_PACK      BIT(25)

/* I2C GO WD0 */
#define TRE_I2C_GO_CMD          GENMASK(4, 0)
#define TRE_I2C_GO_ADDR		GENMASK(14, 8)
#define TRE_I2C_GO_STRETCH	BIT(26)

/* DMA TRE */
#define TRE_DMA_LEN		GENMASK(23, 0)

/* Register offsets from gpi-top */
#define GPII_n_CH_k_CNTXT_0_OFFS(n, k)	(0x20000 + (0x4000 * (n)) + (0x80 * (k)))
#define GPII_n_CH_k_CNTXT_0_EL_SIZE	GENMASK(31, 24)
#define GPII_n_CH_k_CNTXT_0_CHSTATE	GENMASK(23, 20)
#define GPII_n_CH_k_CNTXT_0_ERIDX	GENMASK(18, 14)
#define GPII_n_CH_k_CNTXT_0_DIR		BIT(3)
#define GPII_n_CH_k_CNTXT_0_PROTO	GENMASK(2, 0)

#define GPII_n_CH_k_CNTXT_0(el_size, erindex, dir, chtype_proto)  \
	(FIELD_PREP(GPII_n_CH_k_CNTXT_0_EL_SIZE, el_size)	| \
	 FIELD_PREP(GPII_n_CH_k_CNTXT_0_ERIDX, erindex)		| \
	 FIELD_PREP(GPII_n_CH_k_CNTXT_0_DIR, dir)		| \
	 FIELD_PREP(GPII_n_CH_k_CNTXT_0_PROTO, chtype_proto))

#define GPI_CHTYPE_DIR_IN	(0)
#define GPI_CHTYPE_DIR_OUT	(1)

#define GPI_CHTYPE_PROTO_GPI	(0x2)

#define GPII_n_CH_k_DOORBELL_0_OFFS(n, k)	(0x22000 + (0x4000 * (n)) + (0x8 * (k)))
#define GPII_n_CH_CMD_OFFS(n)			(0x23008 + (0x4000 * (n)))
#define GPII_n_CH_CMD_OPCODE			GENMASK(31, 24)
#define GPII_n_CH_CMD_CHID			GENMASK(7, 0)
#define GPII_n_CH_CMD(opcode, chid)				 \
		     (FIELD_PREP(GPII_n_CH_CMD_OPCODE, opcode) | \
		      FIELD_PREP(GPII_n_CH_CMD_CHID, chid))

#define GPII_n_CH_CMD_ALLOCATE		(0)
#define GPII_n_CH_CMD_START		(1)
#define GPII_n_CH_CMD_STOP		(2)
#define GPII_n_CH_CMD_RESET		(9)
#define GPII_n_CH_CMD_DE_ALLOC		(10)
#define GPII_n_CH_CMD_UART_SW_STALE	(32)
#define GPII_n_CH_CMD_UART_RFR_READY	(33)
#define GPII_n_CH_CMD_UART_RFR_NOT_READY (34)

/* EV Context Array */
#define GPII_n_EV_CH_k_CNTXT_0_OFFS(n, k) (0x21000 + (0x4000 * (n)) + (0x80 * (k)))
#define GPII_n_EV_k_CNTXT_0_EL_SIZE	GENMASK(31, 24)
#define GPII_n_EV_k_CNTXT_0_CHSTATE	GENMASK(23, 20)
#define GPII_n_EV_k_CNTXT_0_INTYPE	BIT(16)
#define GPII_n_EV_k_CNTXT_0_CHTYPE	GENMASK(3, 0)

#define GPII_n_EV_k_CNTXT_0(el_size, inttype, chtype)		\
	(FIELD_PREP(GPII_n_EV_k_CNTXT_0_EL_SIZE, el_size) |	\
	 FIELD_PREP(GPII_n_EV_k_CNTXT_0_INTYPE, inttype)  |	\
	 FIELD_PREP(GPII_n_EV_k_CNTXT_0_CHTYPE, chtype))

#define GPI_INTTYPE_IRQ		(1)
#define GPI_CHTYPE_GPI_EV	(0x2)

enum CNTXT_OFFS {
	CNTXT_0_CONFIG = 0x0,
	CNTXT_1_R_LENGTH = 0x4,
	CNTXT_2_RING_BASE_LSB = 0x8,
	CNTXT_3_RING_BASE_MSB = 0xC,
	CNTXT_4_RING_RP_LSB = 0x10,
	CNTXT_5_RING_RP_MSB = 0x14,
	CNTXT_6_RING_WP_LSB = 0x18,
	CNTXT_7_RING_WP_MSB = 0x1C,
	CNTXT_8_RING_INT_MOD = 0x20,
	CNTXT_9_RING_INTVEC = 0x24,
	CNTXT_10_RING_MSI_LSB = 0x28,
	CNTXT_11_RING_MSI_MSB = 0x2C,
	CNTXT_12_RING_RP_UPDATE_LSB = 0x30,
	CNTXT_13_RING_RP_UPDATE_MSB = 0x34,
};

#define GPII_n_EV_CH_k_DOORBELL_0_OFFS(n, k)	(0x22100 + (0x4000 * (n)) + (0x8 * (k)))
#define GPII_n_EV_CH_CMD_OFFS(n)		(0x23010 + (0x4000 * (n)))
#define GPII_n_EV_CMD_OPCODE			GENMASK(31, 24)
#define GPII_n_EV_CMD_CHID			GENMASK(7, 0)
#define GPII_n_EV_CMD(opcode, chid)				 \
		     (FIELD_PREP(GPII_n_EV_CMD_OPCODE, opcode) | \
		      FIELD_PREP(GPII_n_EV_CMD_CHID, chid))

#define GPII_n_EV_CH_CMD_ALLOCATE		(0x00)
#define GPII_n_EV_CH_CMD_RESET			(0x09)
#define GPII_n_EV_CH_CMD_DE_ALLOC		(0x0A)

#define GPII_n_CNTXT_TYPE_IRQ_OFFS(n)		(0x23080 + (0x4000 * (n)))

/* mask type register */
#define GPII_n_CNTXT_TYPE_IRQ_MSK_OFFS(n)	(0x23088 + (0x4000 * (n)))
#define GPII_n_CNTXT_TYPE_IRQ_MSK_BMSK		GENMASK(6, 0)
#define GPII_n_CNTXT_TYPE_IRQ_MSK_GENERAL	BIT(6)
#define GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB		BIT(3)
#define GPII_n_CNTXT_TYPE_IRQ_MSK_GLOB		BIT(2)
#define GPII_n_CNTXT_TYPE_IRQ_MSK_EV_CTRL	BIT(1)
#define GPII_n_CNTXT_TYPE_IRQ_MSK_CH_CTRL	BIT(0)

#define GPII_n_CNTXT_SRC_GPII_CH_IRQ_OFFS(n)	(0x23090 + (0x4000 * (n)))
#define GPII_n_CNTXT_SRC_EV_CH_IRQ_OFFS(n)	(0x23094 + (0x4000 * (n)))

/* Mask channel control interrupt register */
#define GPII_n_CNTXT_SRC_CH_IRQ_MSK_OFFS(n)	(0x23098 + (0x4000 * (n)))
#define GPII_n_CNTXT_SRC_CH_IRQ_MSK_BMSK	GENMASK(1, 0)

/* Mask event control interrupt register */
#define GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_OFFS(n)	(0x2309C + (0x4000 * (n)))
#define GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_BMSK	BIT(0)

#define GPII_n_CNTXT_SRC_CH_IRQ_CLR_OFFS(n)	(0x230A0 + (0x4000 * (n)))
#define GPII_n_CNTXT_SRC_EV_CH_IRQ_CLR_OFFS(n)	(0x230A4 + (0x4000 * (n)))

/* Mask event interrupt register */
#define GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_OFFS(n)	(0x230B8 + (0x4000 * (n)))
#define GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_BMSK	BIT(0)

#define GPII_n_CNTXT_SRC_IEOB_IRQ_CLR_OFFS(n)	(0x230C0 + (0x4000 * (n)))
#define GPII_n_CNTXT_GLOB_IRQ_STTS_OFFS(n)	(0x23100 + (0x4000 * (n)))
#define GPI_GLOB_IRQ_ERROR_INT_MSK		BIT(0)

/* GPII specific Global - Enable bit register */
#define GPII_n_CNTXT_GLOB_IRQ_EN_OFFS(n)	(0x23108 + (0x4000 * (n)))
#define GPII_n_CNTXT_GLOB_IRQ_CLR_OFFS(n)	(0x23110 + (0x4000 * (n)))
#define GPII_n_CNTXT_GPII_IRQ_STTS_OFFS(n)	(0x23118 + (0x4000 * (n)))

/* GPII general interrupt - Enable bit register */
#define GPII_n_CNTXT_GPII_IRQ_EN_OFFS(n)	(0x23120 + (0x4000 * (n)))
#define GPII_n_CNTXT_GPII_IRQ_EN_BMSK		GENMASK(3, 0)

#define GPII_n_CNTXT_GPII_IRQ_CLR_OFFS(n)	(0x23128 + (0x4000 * (n)))

/* GPII Interrupt Type register */
#define GPII_n_CNTXT_INTSET_OFFS(n)		(0x23180 + (0x4000 * (n)))
#define GPII_n_CNTXT_INTSET_BMSK		BIT(0)

#define GPII_n_CNTXT_MSI_BASE_LSB_OFFS(n)	(0x23188 + (0x4000 * (n)))
#define GPII_n_CNTXT_MSI_BASE_MSB_OFFS(n)	(0x2318C + (0x4000 * (n)))
#define GPII_n_CNTXT_SCRATCH_0_OFFS(n)		(0x23400 + (0x4000 * (n)))
#define GPII_n_CNTXT_SCRATCH_1_OFFS(n)		(0x23404 + (0x4000 * (n)))

#define GPII_n_ERROR_LOG_OFFS(n)		(0x23200 + (0x4000 * (n)))

/* QOS Registers */
#define GPII_n_CH_k_QOS_OFFS(n, k)		(0x2005C + (0x4000 * (n)) + (0x80 * (k)))

/* Scratch registers */
#define GPII_n_CH_k_SCRATCH_0_OFFS(n, k)	(0x20060 + (0x4000 * (n)) + (0x80 * (k)))
#define GPII_n_CH_k_SCRATCH_0_SEID		GENMASK(2, 0)
#define GPII_n_CH_k_SCRATCH_0_PROTO		GENMASK(7, 4)
#define GPII_n_CH_k_SCRATCH_0_PAIR		GENMASK(20, 16)
#define GPII_n_CH_k_SCRATCH_0(pair, proto, seid)		\
			     (FIELD_PREP(GPII_n_CH_k_SCRATCH_0_PAIR, pair)	| \
			      FIELD_PREP(GPII_n_CH_k_SCRATCH_0_PROTO, proto)	| \
			      FIELD_PREP(GPII_n_CH_k_SCRATCH_0_SEID, seid))
#define GPII_n_CH_k_SCRATCH_1_OFFS(n, k)	(0x20064 + (0x4000 * (n)) + (0x80 * (k)))
#define GPII_n_CH_k_SCRATCH_2_OFFS(n, k)	(0x20068 + (0x4000 * (n)) + (0x80 * (k)))
#define GPII_n_CH_k_SCRATCH_3_OFFS(n, k)	(0x2006C + (0x4000 * (n)) + (0x80 * (k)))

struct __packed gpi_tre {
	u32 dword[4];
};

enum msm_gpi_tce_code {
	MSM_GPI_TCE_SUCCESS = 1,
	MSM_GPI_TCE_EOT = 2,
	MSM_GPI_TCE_EOB = 4,
	MSM_GPI_TCE_UNEXP_ERR = 16,
};

#define CMD_TIMEOUT_MS		(250)

#define MAX_CHANNELS_PER_GPII	(2)
#define GPI_TX_CHAN		(0)
#define GPI_RX_CHAN		(1)
#define STATE_IGNORE		(U32_MAX)
#define EV_FACTOR		(2)
#define REQ_OF_DMA_ARGS		(5) /* # of arguments required from client */
#define CHAN_TRES		64

struct __packed xfer_compl_event {
	u64 ptr;
	u32 length:24;
	u8 code;
	u16 status;
	u8 type;
	u8 chid;
};

struct __packed immediate_data_event {
	u8 data_bytes[8];
	u8 length:4;
	u8 resvd:4;
	u16 tre_index;
	u8 code;
	u16 status;
	u8 type;
	u8 chid;
};

struct __packed qup_notif_event {
	u32 status;
	u32 time;
	u32 count:24;
	u8 resvd;
	u16 resvd1;
	u8 type;
	u8 chid;
};

struct __packed gpi_ere {
	u32 dword[4];
};

enum GPI_EV_TYPE {
	XFER_COMPLETE_EV_TYPE = 0x22,
	IMMEDIATE_DATA_EV_TYPE = 0x30,
	QUP_NOTIF_EV_TYPE = 0x31,
	STALE_EV_TYPE = 0xFF,
};

union __packed gpi_event {
	struct __packed xfer_compl_event xfer_compl_event;
	struct __packed immediate_data_event immediate_data_event;
	struct __packed qup_notif_event qup_notif_event;
	struct __packed gpi_ere gpi_ere;
};

enum gpii_irq_settings {
	DEFAULT_IRQ_SETTINGS,
	MASK_IEOB_SETTINGS,
};

enum gpi_ev_state {
	DEFAULT_EV_CH_STATE = 0,
	EV_STATE_NOT_ALLOCATED = DEFAULT_EV_CH_STATE,
	EV_STATE_ALLOCATED,
	MAX_EV_STATES
};

static const char *const gpi_ev_state_str[MAX_EV_STATES] = {
	[EV_STATE_NOT_ALLOCATED] = "NOT ALLOCATED",
	[EV_STATE_ALLOCATED] = "ALLOCATED",
};

#define TO_GPI_EV_STATE_STR(_state) (((_state) >= MAX_EV_STATES) ? \
				    "INVALID" : gpi_ev_state_str[(_state)])

enum gpi_ch_state {
	DEFAULT_CH_STATE = 0x0,
	CH_STATE_NOT_ALLOCATED = DEFAULT_CH_STATE,
	CH_STATE_ALLOCATED = 0x1,
	CH_STATE_STARTED = 0x2,
	CH_STATE_STOPPED = 0x3,
	CH_STATE_STOP_IN_PROC = 0x4,
	CH_STATE_ERROR = 0xf,
	MAX_CH_STATES
};

enum gpi_cmd {
	GPI_CH_CMD_BEGIN,
	GPI_CH_CMD_ALLOCATE = GPI_CH_CMD_BEGIN,
	GPI_CH_CMD_START,
	GPI_CH_CMD_STOP,
	GPI_CH_CMD_RESET,
	GPI_CH_CMD_DE_ALLOC,
	GPI_CH_CMD_UART_SW_STALE,
	GPI_CH_CMD_UART_RFR_READY,
	GPI_CH_CMD_UART_RFR_NOT_READY,
	GPI_CH_CMD_END = GPI_CH_CMD_UART_RFR_NOT_READY,
	GPI_EV_CMD_BEGIN,
	GPI_EV_CMD_ALLOCATE = GPI_EV_CMD_BEGIN,
	GPI_EV_CMD_RESET,
	GPI_EV_CMD_DEALLOC,
	GPI_EV_CMD_END = GPI_EV_CMD_DEALLOC,
	GPI_MAX_CMD,
};

#define IS_CHAN_CMD(_cmd) ((_cmd) <= GPI_CH_CMD_END)

static const char *const gpi_cmd_str[GPI_MAX_CMD] = {
	[GPI_CH_CMD_ALLOCATE] = "CH ALLOCATE",
	[GPI_CH_CMD_START] = "CH START",
	[GPI_CH_CMD_STOP] = "CH STOP",
	[GPI_CH_CMD_RESET] = "CH_RESET",
	[GPI_CH_CMD_DE_ALLOC] = "DE ALLOC",
	[GPI_CH_CMD_UART_SW_STALE] = "UART SW STALE",
	[GPI_CH_CMD_UART_RFR_READY] = "UART RFR READY",
	[GPI_CH_CMD_UART_RFR_NOT_READY] = "UART RFR NOT READY",
	[GPI_EV_CMD_ALLOCATE] = "EV ALLOCATE",
	[GPI_EV_CMD_RESET] = "EV RESET",
	[GPI_EV_CMD_DEALLOC] = "EV DEALLOC",
};

#define TO_GPI_CMD_STR(_cmd) (((_cmd) >= GPI_MAX_CMD) ? "INVALID" : \
				  gpi_cmd_str[(_cmd)])

/*
 * @DISABLE_STATE: no register access allowed
 * @CONFIG_STATE:  client has configured the channel
 * @PREP_HARDWARE: register access is allowed
 *		   however, no processing EVENTS
 * @ACTIVE_STATE: channels are fully operational
 * @PREPARE_TERMINATE: graceful termination of channels
 *		       register access is allowed
 * @PAUSE_STATE: channels are active, but not processing any events
 */
enum gpi_pm_state {
	DISABLE_STATE,
	CONFIG_STATE,
	PREPARE_HARDWARE,
	ACTIVE_STATE,
	PREPARE_TERMINATE,
	PAUSE_STATE,
	MAX_PM_STATE
};

#define REG_ACCESS_VALID(_pm_state) ((_pm_state) >= PREPARE_HARDWARE)

static const char *const gpi_pm_state_str[MAX_PM_STATE] = {
	[DISABLE_STATE] = "DISABLE",
	[CONFIG_STATE] = "CONFIG",
	[PREPARE_HARDWARE] = "PREPARE HARDWARE",
	[ACTIVE_STATE] = "ACTIVE",
	[PREPARE_TERMINATE] = "PREPARE TERMINATE",
	[PAUSE_STATE] = "PAUSE",
};

#define TO_GPI_PM_STR(_state) (((_state) >= MAX_PM_STATE) ? \
			      "INVALID" : gpi_pm_state_str[(_state)])

static const struct {
	enum gpi_cmd gpi_cmd;
	u32 opcode;
	u32 state;
} gpi_cmd_info[GPI_MAX_CMD] = {
	{
		GPI_CH_CMD_ALLOCATE,
		GPII_n_CH_CMD_ALLOCATE,
		CH_STATE_ALLOCATED,
	},
	{
		GPI_CH_CMD_START,
		GPII_n_CH_CMD_START,
		CH_STATE_STARTED,
	},
	{
		GPI_CH_CMD_STOP,
		GPII_n_CH_CMD_STOP,
		CH_STATE_STOPPED,
	},
	{
		GPI_CH_CMD_RESET,
		GPII_n_CH_CMD_RESET,
		CH_STATE_ALLOCATED,
	},
	{
		GPI_CH_CMD_DE_ALLOC,
		GPII_n_CH_CMD_DE_ALLOC,
		CH_STATE_NOT_ALLOCATED,
	},
	{
		GPI_CH_CMD_UART_SW_STALE,
		GPII_n_CH_CMD_UART_SW_STALE,
		STATE_IGNORE,
	},
	{
		GPI_CH_CMD_UART_RFR_READY,
		GPII_n_CH_CMD_UART_RFR_READY,
		STATE_IGNORE,
	},
	{
		GPI_CH_CMD_UART_RFR_NOT_READY,
		GPII_n_CH_CMD_UART_RFR_NOT_READY,
		STATE_IGNORE,
	},
	{
		GPI_EV_CMD_ALLOCATE,
		GPII_n_EV_CH_CMD_ALLOCATE,
		EV_STATE_ALLOCATED,
	},
	{
		GPI_EV_CMD_RESET,
		GPII_n_EV_CH_CMD_RESET,
		EV_STATE_ALLOCATED,
	},
	{
		GPI_EV_CMD_DEALLOC,
		GPII_n_EV_CH_CMD_DE_ALLOC,
		EV_STATE_NOT_ALLOCATED,
	},
};

struct gpi_ring {
	void *pre_aligned;
	size_t alloc_size;
	phys_addr_t phys_addr;
	dma_addr_t dma_handle;
	void *base;
	void *wp;
	void *rp;
	u32 len;
	u32 el_size;
	u32 elements;
	bool configured;
};

struct gpi_dev {
	struct dma_device dma_device;
	struct device *dev;
	struct resource *res;
	void __iomem *regs;
	void __iomem *ee_base; /*ee register base address*/
	u32 max_gpii; /* maximum # of gpii instances available per gpi block */
	u32 gpii_mask; /* gpii instances available for apps */
	u32 ev_factor; /* ev ring length factor */
	struct gpii *gpiis;
};

struct reg_info {
	char *name;
	u32 offset;
	u32 val;
};

struct gchan {
	struct virt_dma_chan vc;
	u32 chid;
	u32 seid;
	u32 protocol;
	struct gpii *gpii;
	enum gpi_ch_state ch_state;
	enum gpi_pm_state pm_state;
	void __iomem *ch_cntxt_base_reg;
	void __iomem *ch_cntxt_db_reg;
	void __iomem *ch_cmd_reg;
	u32 dir;
	struct gpi_ring ch_ring;
	void *config;
};

struct gpii {
	u32 gpii_id;
	struct gchan gchan[MAX_CHANNELS_PER_GPII];
	struct gpi_dev *gpi_dev;
	int irq;
	void __iomem *regs; /* points to gpi top */
	void __iomem *ev_cntxt_base_reg;
	void __iomem *ev_cntxt_db_reg;
	void __iomem *ev_ring_rp_lsb_reg;
	void __iomem *ev_cmd_reg;
	void __iomem *ieob_clr_reg;
	struct mutex ctrl_lock;
	enum gpi_ev_state ev_state;
	bool configured_irq;
	enum gpi_pm_state pm_state;
	rwlock_t pm_lock;
	struct gpi_ring ev_ring;
	struct tasklet_struct ev_task; /* event processing tasklet */
	struct completion cmd_completion;
	enum gpi_cmd gpi_cmd;
	u32 cntxt_type_irq_msk;
	bool ieob_set;
};

#define MAX_TRE 3

struct gpi_desc {
	struct virt_dma_desc vd;
	size_t len;
	void *db; /* DB register to program */
	struct gchan *gchan;
	struct gpi_tre tre[MAX_TRE];
	u32 num_tre;
};

static const u32 GPII_CHAN_DIR[MAX_CHANNELS_PER_GPII] = {
	GPI_CHTYPE_DIR_OUT, GPI_CHTYPE_DIR_IN
};

static irqreturn_t gpi_handle_irq(int irq, void *data);
static void gpi_ring_recycle_ev_element(struct gpi_ring *ring);
static int gpi_ring_add_element(struct gpi_ring *ring, void **wp);
static void gpi_process_events(struct gpii *gpii);

static inline struct gchan *to_gchan(struct dma_chan *dma_chan)
{
	return container_of(dma_chan, struct gchan, vc.chan);
}

static inline struct gpi_desc *to_gpi_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct gpi_desc, vd);
}

static inline phys_addr_t to_physical(const struct gpi_ring *const ring,
				      void *addr)
{
	return ring->phys_addr + (addr - ring->base);
}

static inline void *to_virtual(const struct gpi_ring *const ring, phys_addr_t addr)
{
	return ring->base + (addr - ring->phys_addr);
}

static inline u32 gpi_read_reg(struct gpii *gpii, void __iomem *addr)
{
	return readl_relaxed(addr);
}

static inline void gpi_write_reg(struct gpii *gpii, void __iomem *addr, u32 val)
{
	writel_relaxed(val, addr);
}

/* gpi_write_reg_field - write to specific bit field */
static inline void gpi_write_reg_field(struct gpii *gpii, void __iomem *addr,
				       u32 mask, u32 shift, u32 val)
{
	u32 tmp = gpi_read_reg(gpii, addr);

	tmp &= ~mask;
	val = tmp | ((val << shift) & mask);
	gpi_write_reg(gpii, addr, val);
}

static __always_inline void
gpi_update_reg(struct gpii *gpii, u32 offset, u32 mask, u32 val)
{
	void __iomem *addr = gpii->regs + offset;
	u32 tmp = gpi_read_reg(gpii, addr);

	tmp &= ~mask;
	tmp |= u32_encode_bits(val, mask);

	gpi_write_reg(gpii, addr, tmp);
}

static void gpi_disable_interrupts(struct gpii *gpii)
{
	gpi_update_reg(gpii, GPII_n_CNTXT_TYPE_IRQ_MSK_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_TYPE_IRQ_MSK_BMSK, 0);
	gpi_update_reg(gpii, GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_BMSK, 0);
	gpi_update_reg(gpii, GPII_n_CNTXT_SRC_CH_IRQ_MSK_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_SRC_CH_IRQ_MSK_BMSK, 0);
	gpi_update_reg(gpii, GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_BMSK, 0);
	gpi_update_reg(gpii, GPII_n_CNTXT_GLOB_IRQ_EN_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_GPII_IRQ_EN_BMSK, 0);
	gpi_update_reg(gpii, GPII_n_CNTXT_GPII_IRQ_EN_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_GPII_IRQ_EN_BMSK, 0);
	gpi_update_reg(gpii, GPII_n_CNTXT_INTSET_OFFS(gpii->gpii_id),
		       GPII_n_CNTXT_INTSET_BMSK, 0);

	gpii->cntxt_type_irq_msk = 0;
	devm_free_irq(gpii->gpi_dev->dev, gpii->irq, gpii);
	gpii->configured_irq = false;
}

/* configure and enable interrupts */
static int gpi_config_interrupts(struct gpii *gpii, enum gpii_irq_settings settings, bool mask)
{
	const u32 enable = (GPII_n_CNTXT_TYPE_IRQ_MSK_GENERAL |
			      GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB |
			      GPII_n_CNTXT_TYPE_IRQ_MSK_GLOB |
			      GPII_n_CNTXT_TYPE_IRQ_MSK_EV_CTRL |
			      GPII_n_CNTXT_TYPE_IRQ_MSK_CH_CTRL);
	int ret;

	if (!gpii->configured_irq) {
		ret = devm_request_irq(gpii->gpi_dev->dev, gpii->irq,
				       gpi_handle_irq, IRQF_TRIGGER_HIGH,
				       "gpi-dma", gpii);
		if (ret < 0) {
			dev_err(gpii->gpi_dev->dev, "error request irq:%d ret:%d\n",
				gpii->irq, ret);
			return ret;
		}
	}

	if (settings == MASK_IEOB_SETTINGS) {
		/*
		 * GPII only uses one EV ring per gpii so we can globally
		 * enable/disable IEOB interrupt
		 */
		if (mask)
			gpii->cntxt_type_irq_msk |= GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB;
		else
			gpii->cntxt_type_irq_msk &= ~(GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB);
		gpi_update_reg(gpii, GPII_n_CNTXT_TYPE_IRQ_MSK_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_TYPE_IRQ_MSK_BMSK, gpii->cntxt_type_irq_msk);
	} else {
		gpi_update_reg(gpii, GPII_n_CNTXT_TYPE_IRQ_MSK_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_TYPE_IRQ_MSK_BMSK, enable);
		gpi_update_reg(gpii, GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_BMSK,
			       GPII_n_CNTXT_SRC_IEOB_IRQ_MSK_BMSK);
		gpi_update_reg(gpii, GPII_n_CNTXT_SRC_CH_IRQ_MSK_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_SRC_CH_IRQ_MSK_BMSK,
			       GPII_n_CNTXT_SRC_CH_IRQ_MSK_BMSK);
		gpi_update_reg(gpii, GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_BMSK,
			       GPII_n_CNTXT_SRC_EV_CH_IRQ_MSK_BMSK);
		gpi_update_reg(gpii, GPII_n_CNTXT_GLOB_IRQ_EN_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_GPII_IRQ_EN_BMSK,
			       GPII_n_CNTXT_GPII_IRQ_EN_BMSK);
		gpi_update_reg(gpii, GPII_n_CNTXT_GPII_IRQ_EN_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_GPII_IRQ_EN_BMSK, GPII_n_CNTXT_GPII_IRQ_EN_BMSK);
		gpi_update_reg(gpii, GPII_n_CNTXT_MSI_BASE_LSB_OFFS(gpii->gpii_id), U32_MAX, 0);
		gpi_update_reg(gpii, GPII_n_CNTXT_MSI_BASE_MSB_OFFS(gpii->gpii_id), U32_MAX, 0);
		gpi_update_reg(gpii, GPII_n_CNTXT_SCRATCH_0_OFFS(gpii->gpii_id), U32_MAX, 0);
		gpi_update_reg(gpii, GPII_n_CNTXT_SCRATCH_1_OFFS(gpii->gpii_id), U32_MAX, 0);
		gpi_update_reg(gpii, GPII_n_CNTXT_INTSET_OFFS(gpii->gpii_id),
			       GPII_n_CNTXT_INTSET_BMSK, 1);
		gpi_update_reg(gpii, GPII_n_ERROR_LOG_OFFS(gpii->gpii_id), U32_MAX, 0);

		gpii->cntxt_type_irq_msk = enable;
	}

	gpii->configured_irq = true;
	return 0;
}

/* Sends gpii event or channel command */
static int gpi_send_cmd(struct gpii *gpii, struct gchan *gchan,
			enum gpi_cmd gpi_cmd)
{
	u32 chid = MAX_CHANNELS_PER_GPII;
	unsigned long timeout;
	void __iomem *cmd_reg;
	u32 cmd;

	if (gpi_cmd >= GPI_MAX_CMD)
		return -EINVAL;
	if (IS_CHAN_CMD(gpi_cmd))
		chid = gchan->chid;

	dev_dbg(gpii->gpi_dev->dev,
		"sending cmd: %s:%u\n", TO_GPI_CMD_STR(gpi_cmd), chid);

	/* send opcode and wait for completion */
	reinit_completion(&gpii->cmd_completion);
	gpii->gpi_cmd = gpi_cmd;

	cmd_reg = IS_CHAN_CMD(gpi_cmd) ? gchan->ch_cmd_reg : gpii->ev_cmd_reg;
	cmd = IS_CHAN_CMD(gpi_cmd) ? GPII_n_CH_CMD(gpi_cmd_info[gpi_cmd].opcode, chid) :
				     GPII_n_EV_CMD(gpi_cmd_info[gpi_cmd].opcode, 0);
	gpi_write_reg(gpii, cmd_reg, cmd);
	timeout = wait_for_completion_timeout(&gpii->cmd_completion,
					      msecs_to_jiffies(CMD_TIMEOUT_MS));
	if (!timeout) {
		dev_err(gpii->gpi_dev->dev, "cmd: %s completion timeout:%u\n",
			TO_GPI_CMD_STR(gpi_cmd), chid);
		return -EIO;
	}

	/* confirm new ch state is correct , if the cmd is a state change cmd */
	if (gpi_cmd_info[gpi_cmd].state == STATE_IGNORE)
		return 0;

	if (IS_CHAN_CMD(gpi_cmd) && gchan->ch_state == gpi_cmd_info[gpi_cmd].state)
		return 0;

	if (!IS_CHAN_CMD(gpi_cmd) && gpii->ev_state == gpi_cmd_info[gpi_cmd].state)
		return 0;

	return -EIO;
}

/* program transfer ring DB register */
static inline void gpi_write_ch_db(struct gchan *gchan,
				   struct gpi_ring *ring, void *wp)
{
	struct gpii *gpii = gchan->gpii;
	phys_addr_t p_wp;

	p_wp = to_physical(ring, wp);
	gpi_write_reg(gpii, gchan->ch_cntxt_db_reg, p_wp);
}

/* program event ring DB register */
static inline void gpi_write_ev_db(struct gpii *gpii,
				   struct gpi_ring *ring, void *wp)
{
	phys_addr_t p_wp;

	p_wp = ring->phys_addr + (wp - ring->base);
	gpi_write_reg(gpii, gpii->ev_cntxt_db_reg, p_wp);
}

/* process transfer completion interrupt */
static void gpi_process_ieob(struct gpii *gpii)
{
	gpi_write_reg(gpii, gpii->ieob_clr_reg, BIT(0));

	gpi_config_interrupts(gpii, MASK_IEOB_SETTINGS, 0);
	tasklet_hi_schedule(&gpii->ev_task);
}

/* process channel control interrupt */
static void gpi_process_ch_ctrl_irq(struct gpii *gpii)
{
	u32 gpii_id = gpii->gpii_id;
	u32 offset = GPII_n_CNTXT_SRC_GPII_CH_IRQ_OFFS(gpii_id);
	u32 ch_irq = gpi_read_reg(gpii, gpii->regs + offset);
	struct gchan *gchan;
	u32 chid, state;

	/* clear the status */
	offset = GPII_n_CNTXT_SRC_CH_IRQ_CLR_OFFS(gpii_id);
	gpi_write_reg(gpii, gpii->regs + offset, (u32)ch_irq);

	for (chid = 0; chid < MAX_CHANNELS_PER_GPII; chid++) {
		if (!(BIT(chid) & ch_irq))
			continue;

		gchan = &gpii->gchan[chid];
		state = gpi_read_reg(gpii, gchan->ch_cntxt_base_reg +
				     CNTXT_0_CONFIG);
		state = FIELD_GET(GPII_n_CH_k_CNTXT_0_CHSTATE, state);

		/*
		 * CH_CMD_DEALLOC cmd always successful. However cmd does
		 * not change hardware status. So overwriting software state
		 * to default state.
		 */
		if (gpii->gpi_cmd == GPI_CH_CMD_DE_ALLOC)
			state = DEFAULT_CH_STATE;
		gchan->ch_state = state;

		/*
		 * Triggering complete all if ch_state is not a stop in process.
		 * Stop in process is a transition state and we will wait for
		 * stop interrupt before notifying.
		 */
		if (gchan->ch_state != CH_STATE_STOP_IN_PROC)
			complete_all(&gpii->cmd_completion);
	}
}

/* processing gpi general error interrupts */
static void gpi_process_gen_err_irq(struct gpii *gpii)
{
	u32 gpii_id = gpii->gpii_id;
	u32 offset = GPII_n_CNTXT_GPII_IRQ_STTS_OFFS(gpii_id);
	u32 irq_stts = gpi_read_reg(gpii, gpii->regs + offset);

	/* clear the status */
	dev_dbg(gpii->gpi_dev->dev, "irq_stts:0x%x\n", irq_stts);

	/* Clear the register */
	offset = GPII_n_CNTXT_GPII_IRQ_CLR_OFFS(gpii_id);
	gpi_write_reg(gpii, gpii->regs + offset, irq_stts);
}

/* processing gpi level error interrupts */
static void gpi_process_glob_err_irq(struct gpii *gpii)
{
	u32 gpii_id = gpii->gpii_id;
	u32 offset = GPII_n_CNTXT_GLOB_IRQ_STTS_OFFS(gpii_id);
	u32 irq_stts = gpi_read_reg(gpii, gpii->regs + offset);

	offset = GPII_n_CNTXT_GLOB_IRQ_CLR_OFFS(gpii_id);
	gpi_write_reg(gpii, gpii->regs + offset, irq_stts);

	/* only error interrupt should be set */
	if (irq_stts & ~GPI_GLOB_IRQ_ERROR_INT_MSK) {
		dev_err(gpii->gpi_dev->dev, "invalid error status:0x%x\n", irq_stts);
		return;
	}

	offset = GPII_n_ERROR_LOG_OFFS(gpii_id);
	gpi_write_reg(gpii, gpii->regs + offset, 0);
}

/* gpii interrupt handler */
static irqreturn_t gpi_handle_irq(int irq, void *data)
{
	struct gpii *gpii = data;
	u32 gpii_id = gpii->gpii_id;
	u32 type, offset;
	unsigned long flags;

	read_lock_irqsave(&gpii->pm_lock, flags);

	/*
	 * States are out of sync to receive interrupt
	 * while software state is in DISABLE state, bailing out.
	 */
	if (!REG_ACCESS_VALID(gpii->pm_state)) {
		dev_err(gpii->gpi_dev->dev, "receive interrupt while in %s state\n",
			TO_GPI_PM_STR(gpii->pm_state));
		goto exit_irq;
	}

	offset = GPII_n_CNTXT_TYPE_IRQ_OFFS(gpii->gpii_id);
	type = gpi_read_reg(gpii, gpii->regs + offset);

	do {
		/* global gpii error */
		if (type & GPII_n_CNTXT_TYPE_IRQ_MSK_GLOB) {
			gpi_process_glob_err_irq(gpii);
			type &= ~(GPII_n_CNTXT_TYPE_IRQ_MSK_GLOB);
		}

		/* transfer complete interrupt */
		if (type & GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB) {
			gpi_process_ieob(gpii);
			type &= ~GPII_n_CNTXT_TYPE_IRQ_MSK_IEOB;
		}

		/* event control irq */
		if (type & GPII_n_CNTXT_TYPE_IRQ_MSK_EV_CTRL) {
			u32 ev_state;
			u32 ev_ch_irq;

			dev_dbg(gpii->gpi_dev->dev,
				"processing EV CTRL interrupt\n");
			offset = GPII_n_CNTXT_SRC_EV_CH_IRQ_OFFS(gpii_id);
			ev_ch_irq = gpi_read_reg(gpii, gpii->regs + offset);

			offset = GPII_n_CNTXT_SRC_EV_CH_IRQ_CLR_OFFS
				(gpii_id);
			gpi_write_reg(gpii, gpii->regs + offset, ev_ch_irq);
			ev_state = gpi_read_reg(gpii, gpii->ev_cntxt_base_reg +
						CNTXT_0_CONFIG);
			ev_state = FIELD_GET(GPII_n_EV_k_CNTXT_0_CHSTATE, ev_state);

			/*
			 * CMD EV_CMD_DEALLOC is always successful. However
			 * cmd does not change hardware status. So overwriting
			 * software state to default state.
			 */
			if (gpii->gpi_cmd == GPI_EV_CMD_DEALLOC)
				ev_state = DEFAULT_EV_CH_STATE;

			gpii->ev_state = ev_state;
			dev_dbg(gpii->gpi_dev->dev, "setting EV state to %s\n",
				TO_GPI_EV_STATE_STR(gpii->ev_state));
			complete_all(&gpii->cmd_completion);
			type &= ~(GPII_n_CNTXT_TYPE_IRQ_MSK_EV_CTRL);
		}

		/* channel control irq */
		if (type & GPII_n_CNTXT_TYPE_IRQ_MSK_CH_CTRL) {
			dev_dbg(gpii->gpi_dev->dev, "process CH CTRL interrupts\n");
			gpi_process_ch_ctrl_irq(gpii);
			type &= ~(GPII_n_CNTXT_TYPE_IRQ_MSK_CH_CTRL);
		}

		if (type) {
			dev_err(gpii->gpi_dev->dev, "Unhandled interrupt status:0x%x\n", type);
			gpi_process_gen_err_irq(gpii);
			goto exit_irq;
		}

		offset = GPII_n_CNTXT_TYPE_IRQ_OFFS(gpii->gpii_id);
		type = gpi_read_reg(gpii, gpii->regs + offset);
	} while (type);

exit_irq:
	read_unlock_irqrestore(&gpii->pm_lock, flags);

	return IRQ_HANDLED;
}

/* process DMA Immediate completion data events */
static void gpi_process_imed_data_event(struct gchan *gchan,
					struct immediate_data_event *imed_event)
{
	struct gpii *gpii = gchan->gpii;
	struct gpi_ring *ch_ring = &gchan->ch_ring;
	void *tre = ch_ring->base + (ch_ring->el_size * imed_event->tre_index);
	struct dmaengine_result result;
	struct gpi_desc *gpi_desc;
	struct virt_dma_desc *vd;
	unsigned long flags;
	u32 chid;

	/*
	 * If channel not active don't process event
	 */
	if (gchan->pm_state != ACTIVE_STATE) {
		dev_err(gpii->gpi_dev->dev, "skipping processing event because ch @ %s state\n",
			TO_GPI_PM_STR(gchan->pm_state));
		return;
	}

	spin_lock_irqsave(&gchan->vc.lock, flags);
	vd = vchan_next_desc(&gchan->vc);
	if (!vd) {
		struct gpi_ere *gpi_ere;
		struct gpi_tre *gpi_tre;

		spin_unlock_irqrestore(&gchan->vc.lock, flags);
		dev_dbg(gpii->gpi_dev->dev, "event without a pending descriptor!\n");
		gpi_ere = (struct gpi_ere *)imed_event;
		dev_dbg(gpii->gpi_dev->dev,
			"Event: %08x %08x %08x %08x\n",
			gpi_ere->dword[0], gpi_ere->dword[1],
			gpi_ere->dword[2], gpi_ere->dword[3]);
		gpi_tre = tre;
		dev_dbg(gpii->gpi_dev->dev,
			"Pending TRE: %08x %08x %08x %08x\n",
			gpi_tre->dword[0], gpi_tre->dword[1],
			gpi_tre->dword[2], gpi_tre->dword[3]);
		return;
	}
	gpi_desc = to_gpi_desc(vd);
	spin_unlock_irqrestore(&gchan->vc.lock, flags);

	/*
	 * RP pointed by Event is to last TRE processed,
	 * we need to update ring rp to tre + 1
	 */
	tre += ch_ring->el_size;
	if (tre >= (ch_ring->base + ch_ring->len))
		tre = ch_ring->base;
	ch_ring->rp = tre;

	/* make sure rp updates are immediately visible to all cores */
	smp_wmb();

	chid = imed_event->chid;
	if (imed_event->code == MSM_GPI_TCE_EOT && gpii->ieob_set) {
		if (chid == GPI_RX_CHAN)
			goto gpi_free_desc;
		else
			return;
	}

	if (imed_event->code == MSM_GPI_TCE_UNEXP_ERR)
		result.result = DMA_TRANS_ABORTED;
	else
		result.result = DMA_TRANS_NOERROR;
	result.residue = gpi_desc->len - imed_event->length;

	dma_cookie_complete(&vd->tx);
	dmaengine_desc_get_callback_invoke(&vd->tx, &result);

gpi_free_desc:
	spin_lock_irqsave(&gchan->vc.lock, flags);
	list_del(&vd->node);
	spin_unlock_irqrestore(&gchan->vc.lock, flags);
	kfree(gpi_desc);
	gpi_desc = NULL;
}

/* processing transfer completion events */
static void gpi_process_xfer_compl_event(struct gchan *gchan,
					 struct xfer_compl_event *compl_event)
{
	struct gpii *gpii = gchan->gpii;
	struct gpi_ring *ch_ring = &gchan->ch_ring;
	void *ev_rp = to_virtual(ch_ring, compl_event->ptr);
	struct virt_dma_desc *vd;
	struct gpi_desc *gpi_desc;
	struct dmaengine_result result;
	unsigned long flags;
	u32 chid;

	/* only process events on active channel */
	if (unlikely(gchan->pm_state != ACTIVE_STATE)) {
		dev_err(gpii->gpi_dev->dev, "skipping processing event because ch @ %s state\n",
			TO_GPI_PM_STR(gchan->pm_state));
		return;
	}

	spin_lock_irqsave(&gchan->vc.lock, flags);
	vd = vchan_next_desc(&gchan->vc);
	if (!vd) {
		struct gpi_ere *gpi_ere;

		spin_unlock_irqrestore(&gchan->vc.lock, flags);
		dev_err(gpii->gpi_dev->dev, "Event without a pending descriptor!\n");
		gpi_ere = (struct gpi_ere *)compl_event;
		dev_err(gpii->gpi_dev->dev,
			"Event: %08x %08x %08x %08x\n",
			gpi_ere->dword[0], gpi_ere->dword[1],
			gpi_ere->dword[2], gpi_ere->dword[3]);
		return;
	}

	gpi_desc = to_gpi_desc(vd);
	spin_unlock_irqrestore(&gchan->vc.lock, flags);

	/*
	 * RP pointed by Event is to last TRE processed,
	 * we need to update ring rp to ev_rp + 1
	 */
	ev_rp += ch_ring->el_size;
	if (ev_rp >= (ch_ring->base + ch_ring->len))
		ev_rp = ch_ring->base;
	ch_ring->rp = ev_rp;

	/* update must be visible to other cores */
	smp_wmb();

	chid = compl_event->chid;
	if (compl_event->code == MSM_GPI_TCE_EOT && gpii->ieob_set) {
		if (chid == GPI_RX_CHAN)
			goto gpi_free_desc;
		else
			return;
	}

	if (compl_event->code == MSM_GPI_TCE_UNEXP_ERR) {
		dev_err(gpii->gpi_dev->dev, "Error in Transaction\n");
		result.result = DMA_TRANS_ABORTED;
	} else {
		dev_dbg(gpii->gpi_dev->dev, "Transaction Success\n");
		result.result = DMA_TRANS_NOERROR;
	}
	result.residue = gpi_desc->len - compl_event->length;
	dev_dbg(gpii->gpi_dev->dev, "Residue %d\n", result.residue);

	dma_cookie_complete(&vd->tx);
	dmaengine_desc_get_callback_invoke(&vd->tx, &result);

gpi_free_desc:
	spin_lock_irqsave(&gchan->vc.lock, flags);
	list_del(&vd->node);
	spin_unlock_irqrestore(&gchan->vc.lock, flags);
	kfree(gpi_desc);
	gpi_desc = NULL;
}

/* process all events */
static void gpi_process_events(struct gpii *gpii)
{
	struct gpi_ring *ev_ring = &gpii->ev_ring;
	phys_addr_t cntxt_rp;
	void *rp;
	union gpi_event *gpi_event;
	struct gchan *gchan;
	u32 chid, type;

	cntxt_rp = gpi_read_reg(gpii, gpii->ev_ring_rp_lsb_reg);
	rp = to_virtual(ev_ring, cntxt_rp);

	do {
		while (rp != ev_ring->rp) {
			gpi_event = ev_ring->rp;
			chid = gpi_event->xfer_compl_event.chid;
			type = gpi_event->xfer_compl_event.type;

			dev_dbg(gpii->gpi_dev->dev,
				"Event: CHID:%u, type:%x %08x %08x %08x %08x\n",
				chid, type, gpi_event->gpi_ere.dword[0],
				gpi_event->gpi_ere.dword[1], gpi_event->gpi_ere.dword[2],
				gpi_event->gpi_ere.dword[3]);

			switch (type) {
			case XFER_COMPLETE_EV_TYPE:
				gchan = &gpii->gchan[chid];
				gpi_process_xfer_compl_event(gchan,
							     &gpi_event->xfer_compl_event);
				break;
			case STALE_EV_TYPE:
				dev_dbg(gpii->gpi_dev->dev, "stale event, not processing\n");
				break;
			case IMMEDIATE_DATA_EV_TYPE:
				gchan = &gpii->gchan[chid];
				gpi_process_imed_data_event(gchan,
							    &gpi_event->immediate_data_event);
				break;
			case QUP_NOTIF_EV_TYPE:
				dev_dbg(gpii->gpi_dev->dev, "QUP_NOTIF_EV_TYPE\n");
				break;
			default:
				dev_dbg(gpii->gpi_dev->dev,
					"not supported event type:0x%x\n", type);
			}
			gpi_ring_recycle_ev_element(ev_ring);
		}
		gpi_write_ev_db(gpii, ev_ring, ev_ring->wp);

		/* clear pending IEOB events */
		gpi_write_reg(gpii, gpii->ieob_clr_reg, BIT(0));

		cntxt_rp = gpi_read_reg(gpii, gpii->ev_ring_rp_lsb_reg);
		rp = to_virtual(ev_ring, cntxt_rp);

	} while (rp != ev_ring->rp);
}

/* processing events using tasklet */
static void gpi_ev_tasklet(unsigned long data)
{
	struct gpii *gpii = (struct gpii *)data;

	read_lock_bh(&gpii->pm_lock);
	if (!REG_ACCESS_VALID(gpii->pm_state)) {
		read_unlock_bh(&gpii->pm_lock);
		dev_err(gpii->gpi_dev->dev, "not processing any events, pm_state:%s\n",
			TO_GPI_PM_STR(gpii->pm_state));
		return;
	}

	/* process the events */
	gpi_process_events(gpii);

	/* enable IEOB, switching back to interrupts */
	gpi_config_interrupts(gpii, MASK_IEOB_SETTINGS, 1);
	read_unlock_bh(&gpii->pm_lock);
}

/* marks all pending events for the channel as stale */
static void gpi_mark_stale_events(struct gchan *gchan)
{
	struct gpii *gpii = gchan->gpii;
	struct gpi_ring *ev_ring = &gpii->ev_ring;
	u32 cntxt_rp, local_rp;
	void *ev_rp;

	cntxt_rp = gpi_read_reg(gpii, gpii->ev_ring_rp_lsb_reg);

	ev_rp = ev_ring->rp;
	local_rp = (u32)to_physical(ev_ring, ev_rp);
	while (local_rp != cntxt_rp) {
		union gpi_event *gpi_event = ev_rp;
		u32 chid = gpi_event->xfer_compl_event.chid;

		if (chid == gchan->chid)
			gpi_event->xfer_compl_event.type = STALE_EV_TYPE;
		ev_rp += ev_ring->el_size;
		if (ev_rp >= (ev_ring->base + ev_ring->len))
			ev_rp = ev_ring->base;
		cntxt_rp = gpi_read_reg(gpii, gpii->ev_ring_rp_lsb_reg);
		local_rp = (u32)to_physical(ev_ring, ev_rp);
	}
}

/* reset sw state and issue channel reset or de-alloc */
static int gpi_reset_chan(struct gchan *gchan, enum gpi_cmd gpi_cmd)
{
	struct gpii *gpii = gchan->gpii;
	struct gpi_ring *ch_ring = &gchan->ch_ring;
	unsigned long flags;
	LIST_HEAD(list);
	int ret;

	ret = gpi_send_cmd(gpii, gchan, gpi_cmd);
	if (ret) {
		dev_err(gpii->gpi_dev->dev, "Error with cmd:%s ret:%d\n",
			TO_GPI_CMD_STR(gpi_cmd), ret);
		return ret;
	}

	/* initialize the local ring ptrs */
	ch_ring->rp = ch_ring->base;
	ch_ring->wp = ch_ring->base;

	/* visible to other cores */
	smp_wmb();

	/* check event ring for any stale events */
	write_lock_irq(&gpii->pm_lock);
	gpi_mark_stale_events(gchan);

	/* remove all async descriptors */
	spin_lock_irqsave(&gchan->vc.lock, flags);
	vchan_get_all_descriptors(&gchan->vc, &list);
	spin_unlock_irqrestore(&gchan->vc.lock, flags);
	write_unlock_irq(&gpii->pm_lock);
	vchan_dma_desc_free_list(&gchan->vc, &list);

	return 0;
}

static int gpi_start_chan(struct gchan *gchan)
{
	struct gpii *gpii = gchan->gpii;
	int ret;

	ret = gpi_send_cmd(gpii, gchan, GPI_CH_CMD_START);
	if (ret) {
		dev_err(gpii->gpi_dev->dev, "Error with cmd:%s ret:%d\n",
			TO_GPI_CMD_STR(GPI_CH_CMD_START), ret);
		return ret;
	}

	/* gpii CH is active now */
	write_lock_irq(&gpii->pm_lock);
	gchan->pm_state = ACTIVE_STATE;
	write_unlock_irq(&gpii->pm_lock);

	return 0;
}

static int gpi_stop_chan(struct gchan *gchan)
{
	struct gpii *gpii = gchan->gpii;
	int ret;

	ret = gpi_send_cmd(gpii, gchan, GPI_CH_CMD_STOP);
	if (ret) {
		dev_err(gpii->gpi_dev->dev, "Error with cmd:%s ret:%d\n",
			TO_GPI_CMD_STR(GPI_CH_CMD_STOP), ret);
		return ret;
	}

	return 0;
}

/* allocate and configure the transfer channel */
static int gpi_alloc_chan(struct gchan *chan, bool send_alloc_cmd)
{
	struct gpii *gpii = chan->gpii;
	struct gpi_ring *ring = &chan->ch_ring;
	int ret;
	u32 id = gpii->gpii_id;
	u32 chid = chan->chid;
	u32 pair_chid = !chid;

	if (send_alloc_cmd) {
		ret = gpi_send_cmd(gpii, chan, GPI_CH_CMD_ALLOCATE);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error with cmd:%s ret:%d\n",
				TO_GPI_CMD_STR(GPI_CH_CMD_ALLOCATE), ret);
			return ret;
		}
	}

	gpi_write_reg(gpii, chan->ch_cntxt_base_reg + CNTXT_0_CONFIG,
		      GPII_n_CH_k_CNTXT_0(ring->el_size, 0, chan->dir, GPI_CHTYPE_PROTO_GPI));
	gpi_write_reg(gpii, chan->ch_cntxt_base_reg + CNTXT_1_R_LENGTH, ring->len);
	gpi_write_reg(gpii, chan->ch_cntxt_base_reg + CNTXT_2_RING_BASE_LSB, ring->phys_addr);
	gpi_write_reg(gpii, chan->ch_cntxt_base_reg + CNTXT_3_RING_BASE_MSB,
		      upper_32_bits(ring->phys_addr));
	gpi_write_reg(gpii, chan->ch_cntxt_db_reg + CNTXT_5_RING_RP_MSB - CNTXT_4_RING_RP_LSB,
		      upper_32_bits(ring->phys_addr));
	gpi_write_reg(gpii, gpii->regs + GPII_n_CH_k_SCRATCH_0_OFFS(id, chid),
		      GPII_n_CH_k_SCRATCH_0(pair_chid, chan->protocol, chan->seid));
	gpi_write_reg(gpii, gpii->regs + GPII_n_CH_k_SCRATCH_1_OFFS(id, chid), 0);
	gpi_write_reg(gpii, gpii->regs + GPII_n_CH_k_SCRATCH_2_OFFS(id, chid), 0);
	gpi_write_reg(gpii, gpii->regs + GPII_n_CH_k_SCRATCH_3_OFFS(id, chid), 0);
	gpi_write_reg(gpii, gpii->regs + GPII_n_CH_k_QOS_OFFS(id, chid), 1);

	/* flush all the writes */
	wmb();
	return 0;
}

/* allocate and configure event ring */
static int gpi_alloc_ev_chan(struct gpii *gpii)
{
	struct gpi_ring *ring = &gpii->ev_ring;
	void __iomem *base = gpii->ev_cntxt_base_reg;
	int ret;

	ret = gpi_send_cmd(gpii, NULL, GPI_EV_CMD_ALLOCATE);
	if (ret) {
		dev_err(gpii->gpi_dev->dev, "error with cmd:%s ret:%d\n",
			TO_GPI_CMD_STR(GPI_EV_CMD_ALLOCATE), ret);
		return ret;
	}

	/* program event context */
	gpi_write_reg(gpii, base + CNTXT_0_CONFIG,
		      GPII_n_EV_k_CNTXT_0(ring->el_size, GPI_INTTYPE_IRQ, GPI_CHTYPE_GPI_EV));
	gpi_write_reg(gpii, base + CNTXT_1_R_LENGTH, ring->len);
	gpi_write_reg(gpii, base + CNTXT_2_RING_BASE_LSB, lower_32_bits(ring->phys_addr));
	gpi_write_reg(gpii, base + CNTXT_3_RING_BASE_MSB, upper_32_bits(ring->phys_addr));
	gpi_write_reg(gpii, gpii->ev_cntxt_db_reg + CNTXT_5_RING_RP_MSB - CNTXT_4_RING_RP_LSB,
		      upper_32_bits(ring->phys_addr));
	gpi_write_reg(gpii, base + CNTXT_8_RING_INT_MOD, 0);
	gpi_write_reg(gpii, base + CNTXT_10_RING_MSI_LSB, 0);
	gpi_write_reg(gpii, base + CNTXT_11_RING_MSI_MSB, 0);
	gpi_write_reg(gpii, base + CNTXT_8_RING_INT_MOD, 0);
	gpi_write_reg(gpii, base + CNTXT_12_RING_RP_UPDATE_LSB, 0);
	gpi_write_reg(gpii, base + CNTXT_13_RING_RP_UPDATE_MSB, 0);

	/* add events to ring */
	ring->wp = (ring->base + ring->len - ring->el_size);

	/* flush all the writes */
	wmb();

	/* gpii is active now */
	write_lock_irq(&gpii->pm_lock);
	gpii->pm_state = ACTIVE_STATE;
	write_unlock_irq(&gpii->pm_lock);
	gpi_write_ev_db(gpii, ring, ring->wp);

	return 0;
}

/* calculate # of ERE/TRE available to queue */
static int gpi_ring_num_elements_avail(const struct gpi_ring * const ring)
{
	int elements = 0;

	if (ring->wp < ring->rp) {
		elements = ((ring->rp - ring->wp) / ring->el_size) - 1;
	} else {
		elements = (ring->rp - ring->base) / ring->el_size;
		elements += ((ring->base + ring->len - ring->wp) / ring->el_size) - 1;
	}

	return elements;
}

static int gpi_ring_add_element(struct gpi_ring *ring, void **wp)
{
	if (gpi_ring_num_elements_avail(ring) <= 0)
		return -ENOMEM;

	*wp = ring->wp;
	ring->wp += ring->el_size;
	if (ring->wp  >= (ring->base + ring->len))
		ring->wp = ring->base;

	/* visible to other cores */
	smp_wmb();

	return 0;
}

static void gpi_ring_recycle_ev_element(struct gpi_ring *ring)
{
	/* Update the WP */
	ring->wp += ring->el_size;
	if (ring->wp  >= (ring->base + ring->len))
		ring->wp = ring->base;

	/* Update the RP */
	ring->rp += ring->el_size;
	if (ring->rp  >= (ring->base + ring->len))
		ring->rp = ring->base;

	/* visible to other cores */
	smp_wmb();
}

static void gpi_free_ring(struct gpi_ring *ring,
			  struct gpii *gpii)
{
	dma_free_coherent(gpii->gpi_dev->dev, ring->alloc_size,
			  ring->pre_aligned, ring->dma_handle);
	memset(ring, 0, sizeof(*ring));
}

/* allocate memory for transfer and event rings */
static int gpi_alloc_ring(struct gpi_ring *ring, u32 elements,
			  u32 el_size, struct gpii *gpii)
{
	u64 len = elements * el_size;
	int bit;

	/* ring len must be power of 2 */
	bit = find_last_bit((unsigned long *)&len, 32);
	if (((1 << bit) - 1) & len)
		bit++;
	len = 1 << bit;
	ring->alloc_size = (len + (len - 1));
	dev_dbg(gpii->gpi_dev->dev,
		"#el:%u el_size:%u len:%u actual_len:%llu alloc_size:%zu\n",
		  elements, el_size, (elements * el_size), len,
		  ring->alloc_size);

	ring->pre_aligned = dma_alloc_coherent(gpii->gpi_dev->dev,
					       ring->alloc_size,
					       &ring->dma_handle, GFP_KERNEL);
	if (!ring->pre_aligned) {
		dev_err(gpii->gpi_dev->dev, "could not alloc size:%zu mem for ring\n",
			ring->alloc_size);
		return -ENOMEM;
	}

	/* align the physical mem */
	ring->phys_addr = (ring->dma_handle + (len - 1)) & ~(len - 1);
	ring->base = ring->pre_aligned + (ring->phys_addr - ring->dma_handle);
	ring->rp = ring->base;
	ring->wp = ring->base;
	ring->len = len;
	ring->el_size = el_size;
	ring->elements = ring->len / ring->el_size;
	memset(ring->base, 0, ring->len);
	ring->configured = true;

	/* update to other cores */
	smp_wmb();

	dev_dbg(gpii->gpi_dev->dev,
		"phy_pre:%pad phy_alig:%pa len:%u el_size:%u elements:%u\n",
		&ring->dma_handle, &ring->phys_addr, ring->len,
		ring->el_size, ring->elements);

	return 0;
}

/* copy tre into transfer ring */
static void gpi_queue_xfer(struct gpii *gpii, struct gchan *gchan,
			   struct gpi_tre *gpi_tre, void **wp)
{
	struct gpi_tre *ch_tre;
	int ret;

	/* get next tre location we can copy */
	ret = gpi_ring_add_element(&gchan->ch_ring, (void **)&ch_tre);
	if (unlikely(ret)) {
		dev_err(gpii->gpi_dev->dev, "Error adding ring element to xfer ring\n");
		return;
	}

	/* copy the tre info */
	memcpy(ch_tre, gpi_tre, sizeof(*ch_tre));
	*wp = ch_tre;
}

/* reset and restart transfer channel */
static int gpi_terminate_all(struct dma_chan *chan)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	int schid, echid, i;
	int ret = 0;

	mutex_lock(&gpii->ctrl_lock);

	/*
	 * treat both channels as a group if its protocol is not UART
	 * STOP, RESET, or START needs to be in lockstep
	 */
	schid = (gchan->protocol == QCOM_GPI_UART) ? gchan->chid : 0;
	echid = (gchan->protocol == QCOM_GPI_UART) ? schid + 1 : MAX_CHANNELS_PER_GPII;

	/* stop the channel */
	for (i = schid; i < echid; i++) {
		gchan = &gpii->gchan[i];

		/* disable ch state so no more TRE processing */
		write_lock_irq(&gpii->pm_lock);
		gchan->pm_state = PREPARE_TERMINATE;
		write_unlock_irq(&gpii->pm_lock);

		/* send command to Stop the channel */
		ret = gpi_stop_chan(gchan);
	}

	/* reset the channels (clears any pending tre) */
	for (i = schid; i < echid; i++) {
		gchan = &gpii->gchan[i];

		ret = gpi_reset_chan(gchan, GPI_CH_CMD_RESET);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error resetting channel ret:%d\n", ret);
			goto terminate_exit;
		}

		/* reprogram channel CNTXT */
		ret = gpi_alloc_chan(gchan, false);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error alloc_channel ret:%d\n", ret);
			goto terminate_exit;
		}
	}

	/* restart the channels */
	for (i = schid; i < echid; i++) {
		gchan = &gpii->gchan[i];

		ret = gpi_start_chan(gchan);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error Starting Channel ret:%d\n", ret);
			goto terminate_exit;
		}
	}

terminate_exit:
	mutex_unlock(&gpii->ctrl_lock);
	return ret;
}

/* pause dma transfer for all channels */
static int gpi_pause(struct dma_chan *chan)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	int i, ret;

	mutex_lock(&gpii->ctrl_lock);

	/*
	 * pause/resume are per gpii not per channel, so
	 * client needs to call pause only once
	 */
	if (gpii->pm_state == PAUSE_STATE) {
		dev_dbg(gpii->gpi_dev->dev, "channel is already paused\n");
		mutex_unlock(&gpii->ctrl_lock);
		return 0;
	}

	/* send stop command to stop the channels */
	for (i = 0; i < MAX_CHANNELS_PER_GPII; i++) {
		ret = gpi_stop_chan(&gpii->gchan[i]);
		if (ret) {
			mutex_unlock(&gpii->ctrl_lock);
			return ret;
		}
	}

	disable_irq(gpii->irq);

	/* Wait for threads to complete out */
	tasklet_kill(&gpii->ev_task);

	write_lock_irq(&gpii->pm_lock);
	gpii->pm_state = PAUSE_STATE;
	write_unlock_irq(&gpii->pm_lock);
	mutex_unlock(&gpii->ctrl_lock);

	return 0;
}

/* resume dma transfer */
static int gpi_resume(struct dma_chan *chan)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	int i, ret;

	mutex_lock(&gpii->ctrl_lock);
	if (gpii->pm_state == ACTIVE_STATE) {
		dev_dbg(gpii->gpi_dev->dev, "channel is already active\n");
		mutex_unlock(&gpii->ctrl_lock);
		return 0;
	}

	enable_irq(gpii->irq);

	/* send start command to start the channels */
	for (i = 0; i < MAX_CHANNELS_PER_GPII; i++) {
		ret = gpi_send_cmd(gpii, &gpii->gchan[i], GPI_CH_CMD_START);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error starting chan, ret:%d\n", ret);
			mutex_unlock(&gpii->ctrl_lock);
			return ret;
		}
	}

	write_lock_irq(&gpii->pm_lock);
	gpii->pm_state = ACTIVE_STATE;
	write_unlock_irq(&gpii->pm_lock);
	mutex_unlock(&gpii->ctrl_lock);

	return 0;
}

static void gpi_desc_free(struct virt_dma_desc *vd)
{
	struct gpi_desc *gpi_desc = to_gpi_desc(vd);

	kfree(gpi_desc);
	gpi_desc = NULL;
}

static int
gpi_peripheral_config(struct dma_chan *chan, struct dma_slave_config *config)
{
	struct gchan *gchan = to_gchan(chan);

	if (!config->peripheral_config)
		return -EINVAL;

	gchan->config = krealloc(gchan->config, config->peripheral_size, GFP_NOWAIT);
	if (!gchan->config)
		return -ENOMEM;

	memcpy(gchan->config, config->peripheral_config, config->peripheral_size);

	return 0;
}

static int gpi_create_i2c_tre(struct gchan *chan, struct gpi_desc *desc,
			      struct scatterlist *sgl, enum dma_transfer_direction direction)
{
	struct gpi_i2c_config *i2c = chan->config;
	struct device *dev = chan->gpii->gpi_dev->dev;
	unsigned int tre_idx = 0;
	dma_addr_t address;
	struct gpi_tre *tre;
	unsigned int i;

	/* first create config tre if applicable */
	if (i2c->set_config) {
		tre = &desc->tre[tre_idx];
		tre_idx++;

		tre->dword[0] = u32_encode_bits(i2c->low_count, TRE_I2C_C0_TLOW);
		tre->dword[0] |= u32_encode_bits(i2c->high_count, TRE_I2C_C0_THIGH);
		tre->dword[0] |= u32_encode_bits(i2c->cycle_count, TRE_I2C_C0_TCYL);
		tre->dword[0] |= u32_encode_bits(i2c->pack_enable, TRE_I2C_C0_TX_PACK);
		tre->dword[0] |= u32_encode_bits(i2c->pack_enable, TRE_I2C_C0_RX_PACK);

		tre->dword[1] = 0;

		tre->dword[2] = u32_encode_bits(i2c->clk_div, TRE_C0_CLK_DIV);

		tre->dword[3] = u32_encode_bits(TRE_TYPE_CONFIG0, TRE_FLAGS_TYPE);
		tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_CHAIN);
	}

	/* create the GO tre for Tx */
	if (i2c->op == I2C_WRITE) {
		tre = &desc->tre[tre_idx];
		tre_idx++;

		if (i2c->multi_msg)
			tre->dword[0] = u32_encode_bits(I2C_READ, TRE_I2C_GO_CMD);
		else
			tre->dword[0] = u32_encode_bits(i2c->op, TRE_I2C_GO_CMD);

		tre->dword[0] |= u32_encode_bits(i2c->addr, TRE_I2C_GO_ADDR);
		tre->dword[0] |= u32_encode_bits(i2c->stretch, TRE_I2C_GO_STRETCH);

		tre->dword[1] = 0;
		tre->dword[2] = u32_encode_bits(i2c->rx_len, TRE_RX_LEN);

		tre->dword[3] = u32_encode_bits(TRE_TYPE_GO, TRE_FLAGS_TYPE);

		if (i2c->multi_msg)
			tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_LINK);
		else
			tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_CHAIN);
	}

	if (i2c->op == I2C_READ || i2c->multi_msg == false) {
		/* create the DMA TRE */
		tre = &desc->tre[tre_idx];
		tre_idx++;

		address = sg_dma_address(sgl);
		tre->dword[0] = lower_32_bits(address);
		tre->dword[1] = upper_32_bits(address);

		tre->dword[2] = u32_encode_bits(sg_dma_len(sgl), TRE_DMA_LEN);

		tre->dword[3] = u32_encode_bits(TRE_TYPE_DMA, TRE_FLAGS_TYPE);
		tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_IEOT);
	}

	for (i = 0; i < tre_idx; i++)
		dev_dbg(dev, "TRE:%d %x:%x:%x:%x\n", i, desc->tre[i].dword[0],
			desc->tre[i].dword[1], desc->tre[i].dword[2], desc->tre[i].dword[3]);

	return tre_idx;
}

static int gpi_create_spi_tre(struct gchan *chan, struct gpi_desc *desc,
			      struct scatterlist *sgl, enum dma_transfer_direction direction)
{
	struct gpi_spi_config *spi = chan->config;
	struct device *dev = chan->gpii->gpi_dev->dev;
	unsigned int tre_idx = 0;
	dma_addr_t address;
	struct gpi_tre *tre;
	unsigned int i;

	/* first create config tre if applicable */
	if (direction == DMA_MEM_TO_DEV && spi->set_config) {
		tre = &desc->tre[tre_idx];
		tre_idx++;

		tre->dword[0] = u32_encode_bits(spi->word_len, TRE_SPI_C0_WORD_SZ);
		tre->dword[0] |= u32_encode_bits(spi->loopback_en, TRE_SPI_C0_LOOPBACK);
		tre->dword[0] |= u32_encode_bits(spi->clock_pol_high, TRE_SPI_C0_CPOL);
		tre->dword[0] |= u32_encode_bits(spi->data_pol_high, TRE_SPI_C0_CPHA);
		tre->dword[0] |= u32_encode_bits(spi->pack_en, TRE_SPI_C0_TX_PACK);
		tre->dword[0] |= u32_encode_bits(spi->pack_en, TRE_SPI_C0_RX_PACK);

		tre->dword[1] = 0;

		tre->dword[2] = u32_encode_bits(spi->clk_div, TRE_C0_CLK_DIV);
		tre->dword[2] |= u32_encode_bits(spi->clk_src, TRE_C0_CLK_SRC);

		tre->dword[3] = u32_encode_bits(TRE_TYPE_CONFIG0, TRE_FLAGS_TYPE);
		tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_CHAIN);
	}

	/* create the GO tre for Tx */
	if (direction == DMA_MEM_TO_DEV) {
		tre = &desc->tre[tre_idx];
		tre_idx++;

		tre->dword[0] = u32_encode_bits(spi->fragmentation, TRE_SPI_GO_FRAG);
		tre->dword[0] |= u32_encode_bits(spi->cs, TRE_SPI_GO_CS);
		tre->dword[0] |= u32_encode_bits(spi->cmd, TRE_SPI_GO_CMD);

		tre->dword[1] = 0;

		tre->dword[2] = u32_encode_bits(spi->rx_len, TRE_RX_LEN);

		tre->dword[3] = u32_encode_bits(TRE_TYPE_GO, TRE_FLAGS_TYPE);
		if (spi->cmd == SPI_RX) {
			tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_IEOB);
		} else if (spi->cmd == SPI_TX) {
			tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_CHAIN);
		} else { /* SPI_DUPLEX */
			tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_CHAIN);
			tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_LINK);
		}
	}

	/* create the dma tre */
	tre = &desc->tre[tre_idx];
	tre_idx++;

	address = sg_dma_address(sgl);
	tre->dword[0] = lower_32_bits(address);
	tre->dword[1] = upper_32_bits(address);

	tre->dword[2] = u32_encode_bits(sg_dma_len(sgl), TRE_DMA_LEN);

	tre->dword[3] = u32_encode_bits(TRE_TYPE_DMA, TRE_FLAGS_TYPE);
	if (direction == DMA_MEM_TO_DEV)
		tre->dword[3] |= u32_encode_bits(1, TRE_FLAGS_IEOT);

	for (i = 0; i < tre_idx; i++)
		dev_dbg(dev, "TRE:%d %x:%x:%x:%x\n", i, desc->tre[i].dword[0],
			desc->tre[i].dword[1], desc->tre[i].dword[2], desc->tre[i].dword[3]);

	return tre_idx;
}

/* copy tre into transfer ring */
static struct dma_async_tx_descriptor *
gpi_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		  unsigned int sg_len, enum dma_transfer_direction direction,
		  unsigned long flags, void *context)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	struct device *dev = gpii->gpi_dev->dev;
	struct gpi_ring *ch_ring = &gchan->ch_ring;
	struct gpi_desc *gpi_desc;
	u32 nr, nr_tre = 0;
	u8 set_config;
	int i;

	gpii->ieob_set = false;
	if (!is_slave_direction(direction)) {
		dev_err(gpii->gpi_dev->dev, "invalid dma direction: %d\n", direction);
		return NULL;
	}

	if (sg_len > 1) {
		dev_err(dev, "Multi sg sent, we support only one atm: %d\n", sg_len);
		return NULL;
	}

	nr_tre = 3;
	set_config = *(u32 *)gchan->config;
	if (!set_config)
		nr_tre = 2;
	if (direction == DMA_DEV_TO_MEM) /* rx */
		nr_tre = 1;

	/* calculate # of elements required & available */
	nr = gpi_ring_num_elements_avail(ch_ring);
	if (nr < nr_tre) {
		dev_err(dev, "not enough space in ring, avail:%u required:%u\n", nr, nr_tre);
		return NULL;
	}

	gpi_desc = kzalloc(sizeof(*gpi_desc), GFP_NOWAIT);
	if (!gpi_desc)
		return NULL;

	/* create TREs for xfer */
	if (gchan->protocol == QCOM_GPI_SPI) {
		i = gpi_create_spi_tre(gchan, gpi_desc, sgl, direction);
	} else if (gchan->protocol == QCOM_GPI_I2C) {
		i = gpi_create_i2c_tre(gchan, gpi_desc, sgl, direction);
	} else {
		dev_err(dev, "invalid peripheral: %d\n", gchan->protocol);
		kfree(gpi_desc);
		return NULL;
	}

	/* set up the descriptor */
	gpi_desc->gchan = gchan;
	gpi_desc->len = sg_dma_len(sgl);
	gpi_desc->num_tre  = i;

	return vchan_tx_prep(&gchan->vc, &gpi_desc->vd, flags);
}

/* rings transfer ring db to being transfer */
static void gpi_issue_pending(struct dma_chan *chan)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	unsigned long flags, pm_lock_flags;
	struct virt_dma_desc *vd = NULL;
	struct gpi_desc *gpi_desc;
	struct gpi_ring *ch_ring = &gchan->ch_ring;
	void *tre, *wp = NULL;
	int i;

	read_lock_irqsave(&gpii->pm_lock, pm_lock_flags);

	/* move all submitted discriptors to issued list */
	spin_lock_irqsave(&gchan->vc.lock, flags);
	if (vchan_issue_pending(&gchan->vc))
		vd = list_last_entry(&gchan->vc.desc_issued,
				     struct virt_dma_desc, node);
	spin_unlock_irqrestore(&gchan->vc.lock, flags);

	/* nothing to do list is empty */
	if (!vd) {
		read_unlock_irqrestore(&gpii->pm_lock, pm_lock_flags);
		return;
	}

	gpi_desc = to_gpi_desc(vd);
	for (i = 0; i < gpi_desc->num_tre; i++) {
		tre = &gpi_desc->tre[i];
		gpi_queue_xfer(gpii, gchan, tre, &wp);
	}

	gpi_desc->db = ch_ring->wp;
	gpi_write_ch_db(gchan, &gchan->ch_ring, gpi_desc->db);
	read_unlock_irqrestore(&gpii->pm_lock, pm_lock_flags);
}

static int gpi_ch_init(struct gchan *gchan)
{
	struct gpii *gpii = gchan->gpii;
	const int ev_factor = gpii->gpi_dev->ev_factor;
	u32 elements;
	int i = 0, ret = 0;

	gchan->pm_state = CONFIG_STATE;

	/* check if both channels are configured before continue */
	for (i = 0; i < MAX_CHANNELS_PER_GPII; i++)
		if (gpii->gchan[i].pm_state != CONFIG_STATE)
			goto exit_gpi_init;

	/* protocol must be same for both channels */
	if (gpii->gchan[0].protocol != gpii->gchan[1].protocol) {
		dev_err(gpii->gpi_dev->dev, "protocol did not match protocol %u != %u\n",
			gpii->gchan[0].protocol, gpii->gchan[1].protocol);
		ret = -EINVAL;
		goto exit_gpi_init;
	}

	/* allocate memory for event ring */
	elements = CHAN_TRES << ev_factor;
	ret = gpi_alloc_ring(&gpii->ev_ring, elements,
			     sizeof(union gpi_event), gpii);
	if (ret)
		goto exit_gpi_init;

	/* configure interrupts */
	write_lock_irq(&gpii->pm_lock);
	gpii->pm_state = PREPARE_HARDWARE;
	write_unlock_irq(&gpii->pm_lock);
	ret = gpi_config_interrupts(gpii, DEFAULT_IRQ_SETTINGS, 0);
	if (ret) {
		dev_err(gpii->gpi_dev->dev, "error config. interrupts, ret:%d\n", ret);
		goto error_config_int;
	}

	/* allocate event rings */
	ret = gpi_alloc_ev_chan(gpii);
	if (ret) {
		dev_err(gpii->gpi_dev->dev, "error alloc_ev_chan:%d\n", ret);
		goto error_alloc_ev_ring;
	}

	/* Allocate all channels */
	for (i = 0; i < MAX_CHANNELS_PER_GPII; i++) {
		ret = gpi_alloc_chan(&gpii->gchan[i], true);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error allocating chan:%d\n", ret);
			goto error_alloc_chan;
		}
	}

	/* start channels  */
	for (i = 0; i < MAX_CHANNELS_PER_GPII; i++) {
		ret = gpi_start_chan(&gpii->gchan[i]);
		if (ret) {
			dev_err(gpii->gpi_dev->dev, "Error start chan:%d\n", ret);
			goto error_start_chan;
		}
	}
	return ret;

error_start_chan:
	for (i = i - 1; i >= 0; i--) {
		gpi_stop_chan(&gpii->gchan[i]);
		gpi_send_cmd(gpii, gchan, GPI_CH_CMD_RESET);
	}
	i = 2;
error_alloc_chan:
	for (i = i - 1; i >= 0; i--)
		gpi_reset_chan(gchan, GPI_CH_CMD_DE_ALLOC);
error_alloc_ev_ring:
	gpi_disable_interrupts(gpii);
error_config_int:
	gpi_free_ring(&gpii->ev_ring, gpii);
exit_gpi_init:
	mutex_unlock(&gpii->ctrl_lock);
	return ret;
}

/* release all channel resources */
static void gpi_free_chan_resources(struct dma_chan *chan)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	enum gpi_pm_state cur_state;
	int ret, i;

	mutex_lock(&gpii->ctrl_lock);

	cur_state = gchan->pm_state;

	/* disable ch state so no more TRE processing for this channel */
	write_lock_irq(&gpii->pm_lock);
	gchan->pm_state = PREPARE_TERMINATE;
	write_unlock_irq(&gpii->pm_lock);

	/* attempt to do graceful hardware shutdown */
	if (cur_state == ACTIVE_STATE) {
		gpi_stop_chan(gchan);

		ret = gpi_send_cmd(gpii, gchan, GPI_CH_CMD_RESET);
		if (ret)
			dev_err(gpii->gpi_dev->dev, "error resetting channel:%d\n", ret);

		gpi_reset_chan(gchan, GPI_CH_CMD_DE_ALLOC);
	}

	/* free all allocated memory */
	gpi_free_ring(&gchan->ch_ring, gpii);
	vchan_free_chan_resources(&gchan->vc);
	kfree(gchan->config);

	write_lock_irq(&gpii->pm_lock);
	gchan->pm_state = DISABLE_STATE;
	write_unlock_irq(&gpii->pm_lock);

	/* if other rings are still active exit */
	for (i = 0; i < MAX_CHANNELS_PER_GPII; i++)
		if (gpii->gchan[i].ch_ring.configured)
			goto exit_free;

	/* deallocate EV Ring */
	cur_state = gpii->pm_state;
	write_lock_irq(&gpii->pm_lock);
	gpii->pm_state = PREPARE_TERMINATE;
	write_unlock_irq(&gpii->pm_lock);

	/* wait for threads to complete out */
	tasklet_kill(&gpii->ev_task);

	/* send command to de allocate event ring */
	if (cur_state == ACTIVE_STATE)
		gpi_send_cmd(gpii, NULL, GPI_EV_CMD_DEALLOC);

	gpi_free_ring(&gpii->ev_ring, gpii);

	/* disable interrupts */
	if (cur_state == ACTIVE_STATE)
		gpi_disable_interrupts(gpii);

	/* set final state to disable */
	write_lock_irq(&gpii->pm_lock);
	gpii->pm_state = DISABLE_STATE;
	write_unlock_irq(&gpii->pm_lock);

exit_free:
	mutex_unlock(&gpii->ctrl_lock);
}

/* allocate channel resources */
static int gpi_alloc_chan_resources(struct dma_chan *chan)
{
	struct gchan *gchan = to_gchan(chan);
	struct gpii *gpii = gchan->gpii;
	int ret;

	mutex_lock(&gpii->ctrl_lock);

	/* allocate memory for transfer ring */
	ret = gpi_alloc_ring(&gchan->ch_ring, CHAN_TRES,
			     sizeof(struct gpi_tre), gpii);
	if (ret)
		goto xfer_alloc_err;

	ret = gpi_ch_init(gchan);

	mutex_unlock(&gpii->ctrl_lock);

	return ret;
xfer_alloc_err:
	mutex_unlock(&gpii->ctrl_lock);

	return ret;
}

static int gpi_find_avail_gpii(struct gpi_dev *gpi_dev, u32 seid)
{
	struct gchan *tx_chan, *rx_chan;
	unsigned int gpii;

	/* check if same seid is already configured for another chid */
	for (gpii = 0; gpii < gpi_dev->max_gpii; gpii++) {
		if (!((1 << gpii) & gpi_dev->gpii_mask))
			continue;

		tx_chan = &gpi_dev->gpiis[gpii].gchan[GPI_TX_CHAN];
		rx_chan = &gpi_dev->gpiis[gpii].gchan[GPI_RX_CHAN];

		if (rx_chan->vc.chan.client_count && rx_chan->seid == seid)
			return gpii;
		if (tx_chan->vc.chan.client_count && tx_chan->seid == seid)
			return gpii;
	}

	/* no channels configured with same seid, return next avail gpii */
	for (gpii = 0; gpii < gpi_dev->max_gpii; gpii++) {
		if (!((1 << gpii) & gpi_dev->gpii_mask))
			continue;

		tx_chan = &gpi_dev->gpiis[gpii].gchan[GPI_TX_CHAN];
		rx_chan = &gpi_dev->gpiis[gpii].gchan[GPI_RX_CHAN];

		/* check if gpii is configured */
		if (tx_chan->vc.chan.client_count ||
		    rx_chan->vc.chan.client_count)
			continue;

		/* found a free gpii */
		return gpii;
	}

	/* no gpii instance available to use */
	return -EIO;
}

/* gpi_of_dma_xlate: open client requested channel */
static struct dma_chan *gpi_of_dma_xlate(struct of_phandle_args *args,
					 struct of_dma *of_dma)
{
	struct gpi_dev *gpi_dev = (struct gpi_dev *)of_dma->of_dma_data;
	u32 seid, chid;
	int gpii;
	struct gchan *gchan;

	if (args->args_count < 3) {
		dev_err(gpi_dev->dev, "gpii require minimum 2 args, client passed:%d args\n",
			args->args_count);
		return NULL;
	}

	chid = args->args[0];
	if (chid >= MAX_CHANNELS_PER_GPII) {
		dev_err(gpi_dev->dev, "gpii channel:%d not valid\n", chid);
		return NULL;
	}

	seid = args->args[1];

	/* find next available gpii to use */
	gpii = gpi_find_avail_gpii(gpi_dev, seid);
	if (gpii < 0) {
		dev_err(gpi_dev->dev, "no available gpii instances\n");
		return NULL;
	}

	gchan = &gpi_dev->gpiis[gpii].gchan[chid];
	if (gchan->vc.chan.client_count) {
		dev_err(gpi_dev->dev, "gpii:%d chid:%d seid:%d already configured\n",
			gpii, chid, gchan->seid);
		return NULL;
	}

	gchan->seid = seid;
	gchan->protocol = args->args[2];

	return dma_get_slave_channel(&gchan->vc.chan);
}

static int gpi_probe(struct platform_device *pdev)
{
	struct gpi_dev *gpi_dev;
	unsigned int i;
	u32 ee_offset;
	int ret;

	gpi_dev = devm_kzalloc(&pdev->dev, sizeof(*gpi_dev), GFP_KERNEL);
	if (!gpi_dev)
		return -ENOMEM;

	gpi_dev->dev = &pdev->dev;
	gpi_dev->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpi_dev->regs = devm_ioremap_resource(gpi_dev->dev, gpi_dev->res);
	if (IS_ERR(gpi_dev->regs))
		return PTR_ERR(gpi_dev->regs);
	gpi_dev->ee_base = gpi_dev->regs;

	ret = of_property_read_u32(gpi_dev->dev->of_node, "dma-channels",
				   &gpi_dev->max_gpii);
	if (ret) {
		dev_err(gpi_dev->dev, "missing 'max-no-gpii' DT node\n");
		return ret;
	}

	ret = of_property_read_u32(gpi_dev->dev->of_node, "dma-channel-mask",
				   &gpi_dev->gpii_mask);
	if (ret) {
		dev_err(gpi_dev->dev, "missing 'gpii-mask' DT node\n");
		return ret;
	}

	ee_offset = (uintptr_t)device_get_match_data(gpi_dev->dev);
	gpi_dev->ee_base = gpi_dev->ee_base - ee_offset;

	gpi_dev->ev_factor = EV_FACTOR;

	ret = dma_set_mask(gpi_dev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(gpi_dev->dev, "Error setting dma_mask to 64, ret:%d\n", ret);
		return ret;
	}

	gpi_dev->gpiis = devm_kzalloc(gpi_dev->dev, sizeof(*gpi_dev->gpiis) *
				      gpi_dev->max_gpii, GFP_KERNEL);
	if (!gpi_dev->gpiis)
		return -ENOMEM;

	/* setup all the supported gpii */
	INIT_LIST_HEAD(&gpi_dev->dma_device.channels);
	for (i = 0; i < gpi_dev->max_gpii; i++) {
		struct gpii *gpii = &gpi_dev->gpiis[i];
		int chan;

		if (!((1 << i) & gpi_dev->gpii_mask))
			continue;

		/* set up ev cntxt register map */
		gpii->ev_cntxt_base_reg = gpi_dev->ee_base + GPII_n_EV_CH_k_CNTXT_0_OFFS(i, 0);
		gpii->ev_cntxt_db_reg = gpi_dev->ee_base + GPII_n_EV_CH_k_DOORBELL_0_OFFS(i, 0);
		gpii->ev_ring_rp_lsb_reg = gpii->ev_cntxt_base_reg + CNTXT_4_RING_RP_LSB;
		gpii->ev_cmd_reg = gpi_dev->ee_base + GPII_n_EV_CH_CMD_OFFS(i);
		gpii->ieob_clr_reg = gpi_dev->ee_base + GPII_n_CNTXT_SRC_IEOB_IRQ_CLR_OFFS(i);

		/* set up irq */
		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			return ret;
		gpii->irq = ret;

		/* set up channel specific register info */
		for (chan = 0; chan < MAX_CHANNELS_PER_GPII; chan++) {
			struct gchan *gchan = &gpii->gchan[chan];

			/* set up ch cntxt register map */
			gchan->ch_cntxt_base_reg = gpi_dev->ee_base +
				GPII_n_CH_k_CNTXT_0_OFFS(i, chan);
			gchan->ch_cntxt_db_reg = gpi_dev->ee_base +
				GPII_n_CH_k_DOORBELL_0_OFFS(i, chan);
			gchan->ch_cmd_reg = gpi_dev->ee_base + GPII_n_CH_CMD_OFFS(i);

			/* vchan setup */
			vchan_init(&gchan->vc, &gpi_dev->dma_device);
			gchan->vc.desc_free = gpi_desc_free;
			gchan->chid = chan;
			gchan->gpii = gpii;
			gchan->dir = GPII_CHAN_DIR[chan];
		}
		mutex_init(&gpii->ctrl_lock);
		rwlock_init(&gpii->pm_lock);
		tasklet_init(&gpii->ev_task, gpi_ev_tasklet,
			     (unsigned long)gpii);
		init_completion(&gpii->cmd_completion);
		gpii->gpii_id = i;
		gpii->regs = gpi_dev->ee_base;
		gpii->gpi_dev = gpi_dev;
	}

	platform_set_drvdata(pdev, gpi_dev);

	/* clear and Set capabilities */
	dma_cap_zero(gpi_dev->dma_device.cap_mask);
	dma_cap_set(DMA_SLAVE, gpi_dev->dma_device.cap_mask);

	/* configure dmaengine apis */
	gpi_dev->dma_device.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	gpi_dev->dma_device.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	gpi_dev->dma_device.src_addr_widths = DMA_SLAVE_BUSWIDTH_8_BYTES;
	gpi_dev->dma_device.dst_addr_widths = DMA_SLAVE_BUSWIDTH_8_BYTES;
	gpi_dev->dma_device.device_alloc_chan_resources = gpi_alloc_chan_resources;
	gpi_dev->dma_device.device_free_chan_resources = gpi_free_chan_resources;
	gpi_dev->dma_device.device_tx_status = dma_cookie_status;
	gpi_dev->dma_device.device_issue_pending = gpi_issue_pending;
	gpi_dev->dma_device.device_prep_slave_sg = gpi_prep_slave_sg;
	gpi_dev->dma_device.device_config = gpi_peripheral_config;
	gpi_dev->dma_device.device_terminate_all = gpi_terminate_all;
	gpi_dev->dma_device.dev = gpi_dev->dev;
	gpi_dev->dma_device.device_pause = gpi_pause;
	gpi_dev->dma_device.device_resume = gpi_resume;

	/* register with dmaengine framework */
	ret = dma_async_device_register(&gpi_dev->dma_device);
	if (ret) {
		dev_err(gpi_dev->dev, "async_device_register failed ret:%d", ret);
		return ret;
	}

	ret = of_dma_controller_register(gpi_dev->dev->of_node,
					 gpi_of_dma_xlate, gpi_dev);
	if (ret) {
		dev_err(gpi_dev->dev, "of_dma_controller_reg failed ret:%d", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id gpi_of_match[] = {
	{ .compatible = "qcom,sc7280-gpi-dma", .data = (void *)0x10000 },
	{ .compatible = "qcom,sdm845-gpi-dma", .data = (void *)0x0 },
	{ .compatible = "qcom,sm8150-gpi-dma", .data = (void *)0x0 },
	{ .compatible = "qcom,sm8250-gpi-dma", .data = (void *)0x0 },
	{ .compatible = "qcom,sm8350-gpi-dma", .data = (void *)0x10000 },
	{ .compatible = "qcom,sm8450-gpi-dma", .data = (void *)0x10000 },
	{ },
};
MODULE_DEVICE_TABLE(of, gpi_of_match);

static struct platform_driver gpi_driver = {
	.probe = gpi_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = gpi_of_match,
	},
};

static int __init gpi_init(void)
{
	return platform_driver_register(&gpi_driver);
}
subsys_initcall(gpi_init)

MODULE_DESCRIPTION("QCOM GPI DMA engine driver");
MODULE_LICENSE("GPL v2");
