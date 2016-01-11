/*
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/cdev.h>
#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genalloc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mxc_mlb.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "mxc_mlb150"

/*
 * MLB module memory map registers define
 */
#define REG_MLBC0		0x0
#define MLBC0_MLBEN		(0x1)
#define MLBC0_MLBCLK_MASK	(0x7 << 2)
#define MLBC0_MLBCLK_SHIFT	(2)
#define MLBC0_MLBPEN		(0x1 << 5)
#define MLBC0_MLBLK		(0x1 << 7)
#define MLBC0_ASYRETRY		(0x1 << 12)
#define MLBC0_CTLRETRY		(0x1 << 12)
#define MLBC0_FCNT_MASK		(0x7 << 15)
#define MLBC0_FCNT_SHIFT	(15)

#define REG_MLBPC0		0x8
#define MLBPC0_MCLKHYS		(0x1 << 11)

#define REG_MS0			0xC
#define REG_MS1			0x14

#define REG_MSS			0x20
#define MSS_RSTSYSCMD		(0x1)
#define MSS_LKSYSCMD		(0x1 << 1)
#define MSS_ULKSYSCMD		(0x1 << 2)
#define MSS_CSSYSCMD		(0x1 << 3)
#define MSS_SWSYSCMD		(0x1 << 4)
#define MSS_SERVREQ		(0x1 << 5)

#define REG_MSD			0x24

#define REG_MIEN		0x2C
#define MIEN_ISOC_PE		(0x1)
#define MIEN_ISOC_BUFO		(0x1 << 1)
#define MIEN_SYNC_PE		(0x1 << 16)
#define MIEN_ARX_DONE		(0x1 << 17)
#define MIEN_ARX_PE		(0x1 << 18)
#define MIEN_ARX_BREAK		(0x1 << 19)
#define MIEN_ATX_DONE		(0x1 << 20)
#define MIEN_ATX_PE		(0x1 << 21)
#define MIEN_ATX_BREAK		(0x1 << 22)
#define MIEN_CRX_DONE		(0x1 << 24)
#define MIEN_CRX_PE		(0x1 << 25)
#define MIEN_CRX_BREAK		(0x1 << 26)
#define MIEN_CTX_DONE		(0x1 << 27)
#define MIEN_CTX_PE		(0x1 << 28)
#define MIEN_CTX_BREAK		(0x1 << 29)

#define REG_MLBPC2		0x34
#define REG_MLBPC1		0x38
#define MLBPC1_VAL		(0x00000888)

#define REG_MLBC1		0x3C
#define MLBC1_LOCK		(0x1 << 6)
#define MLBC1_CLKM		(0x1 << 7)
#define MLBC1_NDA_MASK		(0xFF << 8)
#define MLBC1_NDA_SHIFT		(8)

#define REG_HCTL		0x80
#define HCTL_RST0		(0x1)
#define HCTL_RST1		(0x1 << 1)
#define HCTL_EN			(0x1 << 15)

#define REG_HCMR0		0x88
#define REG_HCMR1		0x8C
#define REG_HCER0		0x90
#define REG_HCER1		0x94
#define REG_HCBR0		0x98
#define REG_HCBR1		0x9C

#define REG_MDAT0		0xC0
#define REG_MDAT1		0xC4
#define REG_MDAT2		0xC8
#define REG_MDAT3		0xCC

#define REG_MDWE0		0xD0
#define REG_MDWE1		0xD4
#define REG_MDWE2		0xD8
#define REG_MDWE3		0xDC

#define REG_MCTL		0xE0
#define MCTL_XCMP		(0x1)

#define REG_MADR		0xE4
#define MADR_WNR		(0x1 << 31)
#define MADR_TB			(0x1 << 30)
#define MADR_ADDR_MASK		(0x7f << 8)
#define MADR_ADDR_SHIFT		(0)

#define REG_ACTL		0x3C0
#define ACTL_MPB		(0x1 << 4)
#define ACTL_DMAMODE		(0x1 << 2)
#define ACTL_SMX		(0x1 << 1)
#define ACTL_SCE		(0x1)

#define REG_ACSR0		0x3D0
#define REG_ACSR1		0x3D4
#define REG_ACMR0		0x3D8
#define REG_ACMR1		0x3DC

#define REG_CAT_MDATn(ch) (REG_MDAT0 + ((ch % 8) >> 1) * 4)
#define REG_CAT_MDWEn(ch) (REG_MDWE0 + ((ch % 8) >> 1) * 4)

#define INT_AHB0_CH_START	(0)
#define INT_AHB1_CH_START	(32)

#define LOGIC_CH_NUM		(64)
#define BUF_CDT_OFFSET		(0x0)
#define BUF_ADT_OFFSET		(0x40)
#define BUF_CAT_MLB_OFFSET	(0x80)
#define BUF_CAT_HBI_OFFSET	(0x88)
#define BUF_CTR_END_OFFSET	(0x8F)

#define CAT_MODE_RX		(0x1 << 0)
#define CAT_MODE_TX		(0x1 << 1)
#define CAT_MODE_INBOUND_DMA	(0x1 << 8)
#define CAT_MODE_OUTBOUND_DMA	(0x1 << 9)

#define CH_SYNC_DEFAULT_QUAD	(1)
#define CH_SYNC_MAX_QUAD	(15)
#define CH_SYNC_CDT_BUF_DEP	(CH_SYNC_DEFAULT_QUAD * 4 * 4)
#define CH_SYNC_ADT_BUF_MULTI	(4)
#define CH_SYNC_ADT_BUF_DEP	(CH_SYNC_CDT_BUF_DEP * CH_SYNC_ADT_BUF_MULTI)
#define CH_SYNC_BUF_SZ		(CH_SYNC_MAX_QUAD * 4 * 4 * \
				CH_SYNC_ADT_BUF_MULTI)
#define CH_CTRL_CDT_BUF_DEP	(64)
#define CH_CTRL_ADT_BUF_DEP	(CH_CTRL_CDT_BUF_DEP)
#define CH_CTRL_BUF_SZ		(CH_CTRL_ADT_BUF_DEP)
#define CH_ASYNC_MDP_PACKET_LEN	(1024)
#define CH_ASYNC_MEP_PACKET_LEN	(1536)
#define CH_ASYNC_CDT_BUF_DEP	(CH_ASYNC_MEP_PACKET_LEN)
#define CH_ASYNC_ADT_BUF_DEP	(CH_ASYNC_CDT_BUF_DEP)
#define CH_ASYNC_BUF_SZ		(CH_ASYNC_ADT_BUF_DEP)
#define CH_ISOC_BLK_SIZE_188	(188)
#define CH_ISOC_BLK_SIZE_196	(196)
#define CH_ISOC_BLK_SIZE	(CH_ISOC_BLK_SIZE_188)
#define CH_ISOC_BLK_NUM		(1)
#define CH_ISOC_CDT_BUF_DEP	(CH_ISOC_BLK_SIZE * CH_ISOC_BLK_NUM)
#define CH_ISOC_ADT_BUF_DEP	(CH_ISOC_CDT_BUF_DEP)
#define CH_ISOC_BUF_SZ		(1024)

#define CH_SYNC_DBR_BUF_OFFSET	(0x0)
#define CH_CTRL_DBR_BUF_OFFSET	(CH_SYNC_DBR_BUF_OFFSET + \
				2 * (CH_SYNC_MAX_QUAD * 4 * 4))
#define CH_ASYNC_DBR_BUF_OFFSET	(CH_CTRL_DBR_BUF_OFFSET + \
				2 * CH_CTRL_CDT_BUF_DEP)
#define CH_ISOC_DBR_BUF_OFFSET	(CH_ASYNC_DBR_BUF_OFFSET + \
				2 * CH_ASYNC_CDT_BUF_DEP)

#define DBR_BUF_START 0x00000

#define CDT_LEN			(16)
#define ADT_LEN			(16)
#define CAT_LEN			(2)

#define CDT_SZ			(CDT_LEN * LOGIC_CH_NUM)
#define ADT_SZ			(ADT_LEN * LOGIC_CH_NUM)
#define CAT_SZ			(CAT_LEN * LOGIC_CH_NUM * 2)

#define CDT_BASE(base)		(base + BUF_CDT_OFFSET)
#define ADT_BASE(base)		(base + BUF_ADT_OFFSET)
#define CAT_MLB_BASE(base)	(base + BUF_CAT_MLB_OFFSET)
#define CAT_HBI_BASE(base)	(base + BUF_CAT_HBI_OFFSET)

#define CDTn_ADDR(base, n)	(base + BUF_CDT_OFFSET + n * CDT_LEN)
#define ADTn_ADDR(base, n)	(base + BUF_ADT_OFFSET + n * ADT_LEN)
#define CATn_MLB_ADDR(base, n)	(base + BUF_CAT_MLB_OFFSET + n * CAT_LEN)
#define CATn_HBI_ADDR(base, n)	(base + BUF_CAT_HBI_OFFSET + n * CAT_LEN)

#define CAT_CL_SHIFT		(0x0)
#define CAT_CT_SHIFT		(8)
#define CAT_CE			(0x1 << 11)
#define CAT_RNW			(0x1 << 12)
#define CAT_MT			(0x1 << 13)
#define CAT_FCE			(0x1 << 14)
#define CAT_MFE			(0x1 << 14)

#define CDT_WSBC_SHIFT		(14)
#define CDT_WPC_SHIFT		(11)
#define CDT_RSBC_SHIFT		(30)
#define CDT_RPC_SHIFT		(27)
#define CDT_WPC_1_SHIFT		(12)
#define CDT_RPC_1_SHIFT		(28)
#define CDT_WPTR_SHIFT		(0)
#define CDT_SYNC_WSTS_MASK	(0x0000f000)
#define CDT_SYNC_WSTS_SHIFT	(12)
#define CDT_CTRL_ASYNC_WSTS_MASK	(0x0000f000)
#define CDT_CTRL_ASYNC_WSTS_SHIFT	(12)
#define CDT_ISOC_WSTS_MASK	(0x0000e000)
#define CDT_ISOC_WSTS_SHIFT	(13)
#define CDT_RPTR_SHIFT		(16)
#define CDT_SYNC_RSTS_MASK	(0xf0000000)
#define CDT_SYNC_RSTS_SHIFT	(28)
#define CDT_CTRL_ASYNC_RSTS_MASK	(0xf0000000)
#define CDT_CTRL_ASYNC_RSTS_SHIFT	(28)
#define CDT_ISOC_RSTS_MASK	(0xe0000000)
#define CDT_ISOC_RSTS_SHIFT	(29)
#define CDT_CTRL_ASYNC_WSTS_1	(0x1 << 14)
#define CDT_CTRL_ASYNC_RSTS_1	(0x1 << 15)
#define CDT_BD_SHIFT		(0)
#define CDT_BA_SHIFT		(16)
#define CDT_BS_SHIFT		(0)
#define CDT_BF_SHIFT		(31)

#define ADT_PG			(0x1 << 13)
#define ADT_LE			(0x1 << 14)
#define ADT_CE			(0x1 << 15)
#define ADT_BD1_SHIFT		(0)
#define ADT_ERR1		(0x1 << 13)
#define ADT_DNE1		(0x1 << 14)
#define ADT_RDY1		(0x1 << 15)
#define ADT_BD2_SHIFT		(16)
#define ADT_ERR2		(0x1 << 29)
#define ADT_DNE2		(0x1 << 30)
#define ADT_RDY2		(0x1 << 31)
#define ADT_BA1_SHIFT		(0x0)
#define ADT_BA2_SHIFT		(0x0)
#define ADT_PS1			(0x1 << 12)
#define ADT_PS2			(0x1 << 28)
#define ADT_MEP1		(0x1 << 11)
#define ADT_MEP2		(0x1 << 27)

#define MLB_MINOR_DEVICES	4
#define MLB_CONTROL_DEV_NAME	"ctrl"
#define MLB_ASYNC_DEV_NAME	"async"
#define MLB_SYNC_DEV_NAME	"sync"
#define MLB_ISOC_DEV_NAME	"isoc"

#define TX_CHANNEL		0
#define RX_CHANNEL		1

#define TRANS_RING_NODES	(1 << 3)
#define MLB_QUIRK_MLB150	(1 << 0)

enum MLB_CTYPE {
	MLB_CTYPE_SYNC,
	MLB_CTYPE_CTRL,
	MLB_CTYPE_ASYNC,
	MLB_CTYPE_ISOC,
};

enum CLK_SPEED {
	CLK_256FS,
	CLK_512FS,
	CLK_1024FS,
	CLK_2048FS,
	CLK_3072FS,
	CLK_4096FS,
	CLK_6144FS,
	CLK_8192FS,
};

enum MLB_INDEX {
	IMX6Q_MLB = 0,
	IMX6SX_MLB,
};

struct mlb_ringbuf {
	s8 *virt_bufs[TRANS_RING_NODES];
	u32 phy_addrs[TRANS_RING_NODES];
	s32 head;
	s32 tail;
	s32 unit_size;
	s32 total_size;
	rwlock_t rb_lock ____cacheline_aligned; /* ring index lock */
};

struct mlb_channel_info {
	/* Input MLB channel address */
	u32 address;
	/* Internal AHB channel label */
	u32 cl;
	/* DBR buf head */
	u32 dbr_buf_head;
};

struct mlb_dev_info {
	/* device node name */
	const char dev_name[20];
	/* channel type */
	const unsigned int channel_type;
	/* ch fps */
	enum CLK_SPEED fps;
	/* channel info for tx/rx */
	struct mlb_channel_info channels[2];
	/* ring buffer */
	u8 *rbuf_base_virt;
	u32 rbuf_base_phy;
	struct mlb_ringbuf rx_rbuf;
	struct mlb_ringbuf tx_rbuf;
	/* exception event */
	unsigned long ex_event;
	/* tx busy indicator */
	unsigned long tx_busy;
	/* channel started up or not */
	atomic_t on;
	/* device open count */
	atomic_t opencnt;
	/* wait queue head for channel */
	wait_queue_head_t rx_wq;
	wait_queue_head_t tx_wq;
	/* TX OK */
	s32 tx_ok;
	/* spinlock for event access */
	spinlock_t event_lock;
	/*
	 * Block size for isoc mode
	 * This variable can be configured in ioctl
	 */
	u32 isoc_blksz;
	/*
	 * Quads number for sync mode
	 * This variable can be confifured in ioctl
	 */
	u32 sync_quad;
	/* Buffer depth in cdt */
	u32 cdt_buf_dep;
	/* Buffer depth in adt */
	u32 adt_buf_dep;
	/* Buffer size to hold data */
	u32 buf_size;
};

struct mlb_data {
	struct mlb_dev_info *devinfo;
	struct clk *clk_mlb3p;
	struct clk *clk_mlb6p;
	struct cdev cdev;
	struct class *class;	/* device class */
	dev_t firstdev;
#ifdef CONFIG_REGULATOR
	struct regulator *nvcc;
#endif
	void __iomem *membase;	/* mlb module base address */
	struct gen_pool *iram_pool;
	u32 iram_size;
	int irq_ahb0;
	int irq_ahb1;
	int irq_mlb;
	u32 quirk_flag;
};

/*
 * For optimization, we use fixed channel label for
 * input channels of each mode
 * SYNC: CL = 0 for RX, CL = 64 for TX
 * CTRL: CL = 1 for RX, CL = 65 for TX
 * ASYNC: CL = 2 for RX, CL = 66 for TX
 * ISOC: CL = 3 for RX, CL = 67 for TX
 */
#define SYNC_RX_CL_AHB0		0
#define CTRL_RX_CL_AHB0		1
#define ASYNC_RX_CL_AHB0	2
#define ISOC_RX_CL_AHB0		3
#define SYNC_TX_CL_AHB0		4
#define CTRL_TX_CL_AHB0		5
#define ASYNC_TX_CL_AHB0	6
#define ISOC_TX_CL_AHB0		7

#define SYNC_RX_CL_AHB1		32
#define CTRL_RX_CL_AHB1		33
#define ASYNC_RX_CL_AHB1	34
#define ISOC_RX_CL_AHB1		35
#define SYNC_TX_CL_AHB1		36
#define CTRL_TX_CL_AHB1		37
#define ASYNC_TX_CL_AHB1	38
#define ISOC_TX_CL_AHB1		39

#define SYNC_RX_CL	SYNC_RX_CL_AHB0
#define CTRL_RX_CL	CTRL_RX_CL_AHB0
#define ASYNC_RX_CL	ASYNC_RX_CL_AHB0
#define ISOC_RX_CL	ISOC_RX_CL_AHB0

#define SYNC_TX_CL	SYNC_TX_CL_AHB0
#define CTRL_TX_CL	CTRL_TX_CL_AHB0
#define ASYNC_TX_CL	ASYNC_TX_CL_AHB0
#define ISOC_TX_CL	ISOC_TX_CL_AHB0

static struct mlb_dev_info mlb_devinfo[MLB_MINOR_DEVICES] = {
	{
	.dev_name = MLB_SYNC_DEV_NAME,
	.channel_type = MLB_CTYPE_SYNC,
	.channels = {
		[0] = {
			.cl = SYNC_TX_CL,
			.dbr_buf_head = CH_SYNC_DBR_BUF_OFFSET,
		},
		[1] = {
			.cl = SYNC_RX_CL,
			.dbr_buf_head = CH_SYNC_DBR_BUF_OFFSET
					+ CH_SYNC_BUF_SZ,
		},
	},
	.rx_rbuf = {
		.unit_size = CH_SYNC_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[0].rx_rbuf.rb_lock),
	},
	.tx_rbuf = {
		.unit_size = CH_SYNC_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[0].tx_rbuf.rb_lock),
	},
	.cdt_buf_dep = CH_SYNC_CDT_BUF_DEP,
	.adt_buf_dep = CH_SYNC_ADT_BUF_DEP,
	.buf_size = CH_SYNC_BUF_SZ,
	.on = ATOMIC_INIT(0),
	.opencnt = ATOMIC_INIT(0),
	.event_lock = __SPIN_LOCK_UNLOCKED(mlb_devinfo[0].event_lock),
	},
	{
	.dev_name = MLB_CONTROL_DEV_NAME,
	.channel_type = MLB_CTYPE_CTRL,
	.channels = {
		[0] = {
			.cl = CTRL_TX_CL,
			.dbr_buf_head = CH_CTRL_DBR_BUF_OFFSET,
		},
		[1] = {
			.cl = CTRL_RX_CL,
			.dbr_buf_head = CH_CTRL_DBR_BUF_OFFSET
					+ CH_CTRL_BUF_SZ,
		},
	},
	.rx_rbuf = {
		.unit_size = CH_CTRL_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[1].rx_rbuf.rb_lock),
	},
	.tx_rbuf = {
		.unit_size = CH_CTRL_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[1].tx_rbuf.rb_lock),
	},
	.cdt_buf_dep = CH_CTRL_CDT_BUF_DEP,
	.adt_buf_dep = CH_CTRL_ADT_BUF_DEP,
	.buf_size = CH_CTRL_BUF_SZ,
	.on = ATOMIC_INIT(0),
	.opencnt = ATOMIC_INIT(0),
	.event_lock = __SPIN_LOCK_UNLOCKED(mlb_devinfo[1].event_lock),
	},
	{
	.dev_name = MLB_ASYNC_DEV_NAME,
	.channel_type = MLB_CTYPE_ASYNC,
	.channels = {
		[0] = {
			.cl = ASYNC_TX_CL,
			.dbr_buf_head = CH_ASYNC_DBR_BUF_OFFSET,
		},
		[1] = {
			.cl = ASYNC_RX_CL,
			.dbr_buf_head = CH_ASYNC_DBR_BUF_OFFSET
					+ CH_ASYNC_BUF_SZ,
		},
	},
	.rx_rbuf = {
		.unit_size = CH_ASYNC_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[2].rx_rbuf.rb_lock),
	},
	.tx_rbuf = {
		.unit_size = CH_ASYNC_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[2].tx_rbuf.rb_lock),
	},
	.cdt_buf_dep = CH_ASYNC_CDT_BUF_DEP,
	.adt_buf_dep = CH_ASYNC_ADT_BUF_DEP,
	.buf_size = CH_ASYNC_BUF_SZ,
	.on = ATOMIC_INIT(0),
	.opencnt = ATOMIC_INIT(0),
	.event_lock = __SPIN_LOCK_UNLOCKED(mlb_devinfo[2].event_lock),
	},
	{
	.dev_name = MLB_ISOC_DEV_NAME,
	.channel_type = MLB_CTYPE_ISOC,
	.channels = {
		[0] = {
			.cl = ISOC_TX_CL,
			.dbr_buf_head = CH_ISOC_DBR_BUF_OFFSET,
		},
		[1] = {
			.cl = ISOC_RX_CL,
			.dbr_buf_head = CH_ISOC_DBR_BUF_OFFSET
					+ CH_ISOC_BUF_SZ,
		},
	},
	.rx_rbuf = {
		.unit_size = CH_ISOC_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[3].rx_rbuf.rb_lock),
	},
	.tx_rbuf = {
		.unit_size = CH_ISOC_BUF_SZ,
		.rb_lock =
			__RW_LOCK_UNLOCKED(mlb_devinfo[3].tx_rbuf.rb_lock),
	},
	.cdt_buf_dep = CH_ISOC_CDT_BUF_DEP,
	.adt_buf_dep = CH_ISOC_ADT_BUF_DEP,
	.buf_size = CH_ISOC_BUF_SZ,
	.on = ATOMIC_INIT(0),
	.opencnt = ATOMIC_INIT(0),
	.event_lock = __SPIN_LOCK_UNLOCKED(mlb_devinfo[3].event_lock),
	.isoc_blksz = CH_ISOC_BLK_SIZE_188,
	},
};

static void __iomem *mlb_base;

DEFINE_SPINLOCK(ctr_lock);

#ifdef DEBUG
#define DUMP_REG(reg) pr_debug(#reg": 0x%08x\n", __raw_readl(mlb_base + reg))

static void mlb150_dev_dump_reg(void)
{
	pr_debug("mxc_mlb150: Dump registers:\n");
	DUMP_REG(REG_MLBC0);
	DUMP_REG(REG_MLBPC0);
	DUMP_REG(REG_MS0);
	DUMP_REG(REG_MS1);
	DUMP_REG(REG_MSS);
	DUMP_REG(REG_MSD);
	DUMP_REG(REG_MIEN);
	DUMP_REG(REG_MLBPC2);
	DUMP_REG(REG_MLBPC1);
	DUMP_REG(REG_MLBC1);
	DUMP_REG(REG_HCTL);
	DUMP_REG(REG_HCMR0);
	DUMP_REG(REG_HCMR1);
	DUMP_REG(REG_HCER0);
	DUMP_REG(REG_HCER1);
	DUMP_REG(REG_HCBR0);
	DUMP_REG(REG_HCBR1);
	DUMP_REG(REG_MDAT0);
	DUMP_REG(REG_MDAT1);
	DUMP_REG(REG_MDAT2);
	DUMP_REG(REG_MDAT3);
	DUMP_REG(REG_MDWE0);
	DUMP_REG(REG_MDWE1);
	DUMP_REG(REG_MDWE2);
	DUMP_REG(REG_MDWE3);
	DUMP_REG(REG_MCTL);
	DUMP_REG(REG_MADR);
	DUMP_REG(REG_ACTL);
	DUMP_REG(REG_ACSR0);
	DUMP_REG(REG_ACSR1);
	DUMP_REG(REG_ACMR0);
	DUMP_REG(REG_ACMR1);
}

static void mlb150_dev_dump_hex(const u8 *buf, u32 len)
{
	print_hex_dump(KERN_DEBUG, "CTR DUMP:",
			DUMP_PREFIX_OFFSET, 8, 1, buf, len, 0);
}
#endif

static inline void mlb150_dev_enable_ctr_write(u32 mdat0_bits_en,
		u32 mdat1_bits_en, u32 mdat2_bits_en, u32 mdat3_bits_en)
{
	__raw_writel(mdat0_bits_en, mlb_base + REG_MDWE0);
	__raw_writel(mdat1_bits_en, mlb_base + REG_MDWE1);
	__raw_writel(mdat2_bits_en, mlb_base + REG_MDWE2);
	__raw_writel(mdat3_bits_en, mlb_base + REG_MDWE3);
}

#ifdef DEBUG
static inline u8 mlb150_dev_dbr_read(u32 dbr_addr)
{
	s32 timeout = 1000;
	u8  dbr_val = 0;
	unsigned long flags;

	spin_lock_irqsave(&ctr_lock, flags);
	__raw_writel(MADR_TB | dbr_addr,
		mlb_base + REG_MADR);

	while ((!(__raw_readl(mlb_base + REG_MCTL)
			& MCTL_XCMP)) &&
			timeout--)
		;

	if (0 == timeout) {
		spin_unlock_irqrestore(&ctr_lock, flags);
		return -ETIME;
	}

	dbr_val = __raw_readl(mlb_base + REG_MDAT0) & 0x000000ff;

	__raw_writel(0, mlb_base + REG_MCTL);
	spin_unlock_irqrestore(&ctr_lock, flags);

	return dbr_val;
}

static inline s32 mlb150_dev_dbr_write(u32 dbr_addr, u8 dbr_val)
{
	s32 timeout = 1000;
	u32 mdat0 = dbr_val & 0x000000ff;
	unsigned long flags;

	spin_lock_irqsave(&ctr_lock, flags);
	__raw_writel(mdat0, mlb_base + REG_MDAT0);

	__raw_writel(MADR_WNR | MADR_TB | dbr_addr,
			mlb_base + REG_MADR);

	while ((!(__raw_readl(mlb_base + REG_MCTL)
			& MCTL_XCMP)) &&
			timeout--)
		;

	if (timeout <= 0) {
		spin_unlock_irqrestore(&ctr_lock, flags);
		return -ETIME;
	}

	__raw_writel(0, mlb_base + REG_MCTL);
	spin_unlock_irqrestore(&ctr_lock, flags);

	return 0;
}

static inline s32 mlb150_dev_dbr_dump(u32 addr, u32 size)
{
	u8 *dump_buf = NULL;
	u8 *buf_ptr = NULL;
	s32 i;

	dump_buf = kzalloc(size, GFP_KERNEL);
	if (!dump_buf) {
		pr_err("can't allocate enough memory\n");
		return -ENOMEM;
	}

	for (i = 0, buf_ptr = dump_buf;
			i < size; ++i, ++buf_ptr)
		*buf_ptr = mlb150_dev_dbr_read(addr + i);

	mlb150_dev_dump_hex(dump_buf, size);

	kfree(dump_buf);

	return 0;
}
#endif

static s32 mlb150_dev_ctr_read(u32 ctr_offset, u32 *ctr_val)
{
	s32 timeout = 1000;
	unsigned long flags;

	spin_lock_irqsave(&ctr_lock, flags);
	__raw_writel(ctr_offset, mlb_base + REG_MADR);

	while ((!(__raw_readl(mlb_base + REG_MCTL)
			& MCTL_XCMP)) &&
			timeout--)
		;

	if (timeout <= 0) {
		spin_unlock_irqrestore(&ctr_lock, flags);
		pr_debug("mxc_mlb150: Read CTR timeout\n");
		return -ETIME;
	}

	ctr_val[0] = __raw_readl(mlb_base + REG_MDAT0);
	ctr_val[1] = __raw_readl(mlb_base + REG_MDAT1);
	ctr_val[2] = __raw_readl(mlb_base + REG_MDAT2);
	ctr_val[3] = __raw_readl(mlb_base + REG_MDAT3);

	__raw_writel(0, mlb_base + REG_MCTL);

	spin_unlock_irqrestore(&ctr_lock, flags);

	return 0;
}

static s32 mlb150_dev_ctr_write(u32 ctr_offset, const u32 *ctr_val)
{
	s32 timeout = 1000;
	unsigned long flags;

	spin_lock_irqsave(&ctr_lock, flags);

	__raw_writel(ctr_val[0], mlb_base + REG_MDAT0);
	__raw_writel(ctr_val[1], mlb_base + REG_MDAT1);
	__raw_writel(ctr_val[2], mlb_base + REG_MDAT2);
	__raw_writel(ctr_val[3], mlb_base + REG_MDAT3);

	__raw_writel(MADR_WNR | ctr_offset,
			mlb_base + REG_MADR);

	while ((!(__raw_readl(mlb_base + REG_MCTL)
			& MCTL_XCMP)) &&
			timeout--)
		;

	if (timeout <= 0) {
		spin_unlock_irqrestore(&ctr_lock, flags);
		pr_debug("mxc_mlb150: Write CTR timeout\n");
		return -ETIME;
	}

	__raw_writel(0, mlb_base + REG_MCTL);

	spin_unlock_irqrestore(&ctr_lock, flags);

#ifdef DEBUG_CTR
	{
		u32 ctr_rd[4] = { 0 };

		if (!mlb150_dev_ctr_read(ctr_offset, ctr_rd)) {
			if (ctr_val[0] == ctr_rd[0] &&
				ctr_val[1] == ctr_rd[1] &&
				ctr_val[2] == ctr_rd[2] &&
				ctr_val[3] == ctr_rd[3])
				return 0;
			else {
				pr_debug("mxc_mlb150: ctr write failed\n");
				pr_debug("offset: 0x%x\n", ctr_offset);
				pr_debug("Write: 0x%x 0x%x 0x%x 0x%x\n",
						ctr_val[3], ctr_val[2],
						ctr_val[1], ctr_val[0]);
				pr_debug("Read: 0x%x 0x%x 0x%x 0x%x\n",
						ctr_rd[3], ctr_rd[2],
						ctr_rd[1], ctr_rd[0]);
				return -EBADE;
			}
		} else {
			pr_debug("mxc_mlb150: ctr read failed\n");
			return -EBADE;
		}
	}
#endif

	return 0;
}

#ifdef DEBUG
static s32 mlb150_dev_cat_read(u32 ctr_offset, u32 ch, u16 *cat_val)
{
	u16 ctr_val[8] = { 0 };

	if (mlb150_dev_ctr_read(ctr_offset, (u32 *)ctr_val))
		return -ETIME;

	/*
	 * Use u16 array to get u32 array value,
	 * need to convert
	 */
	cat_val = ctr_val[ch % 8];

	 return 0;
}
#endif

static s32 mlb150_dev_cat_write(u32 ctr_offset, u32 ch, const u16 cat_val)
{
	u16 ctr_val[8] = { 0 };

	if (mlb150_dev_ctr_read(ctr_offset, (u32 *)ctr_val))
		return -ETIME;

	ctr_val[ch % 8] = cat_val;
	if (mlb150_dev_ctr_write(ctr_offset, (u32 *)ctr_val))
		return -ETIME;

	return 0;
}

#define mlb150_dev_cat_mlb_read(ch, cat_val)	\
	mlb150_dev_cat_read(BUF_CAT_MLB_OFFSET + (ch >> 3), ch, cat_val)
#define mlb150_dev_cat_mlb_write(ch, cat_val)	\
	mlb150_dev_cat_write(BUF_CAT_MLB_OFFSET + (ch >> 3), ch, cat_val)
#define mlb150_dev_cat_hbi_read(ch, cat_val)	\
	mlb150_dev_cat_read(BUF_CAT_HBI_OFFSET + (ch >> 3), ch, cat_val)
#define mlb150_dev_cat_hbi_write(ch, cat_val)	\
	mlb150_dev_cat_write(BUF_CAT_HBI_OFFSET + (ch >> 3), ch, cat_val)

#define mlb150_dev_cdt_read(ch, cdt_val)	\
	mlb150_dev_ctr_read(BUF_CDT_OFFSET + ch, cdt_val)
#define mlb150_dev_cdt_write(ch, cdt_val)	\
	mlb150_dev_ctr_write(BUF_CDT_OFFSET + ch, cdt_val)
#define mlb150_dev_adt_read(ch, adt_val)	\
	mlb150_dev_ctr_read(BUF_ADT_OFFSET + ch, adt_val)
#define mlb150_dev_adt_write(ch, adt_val)	\
	mlb150_dev_ctr_write(BUF_ADT_OFFSET + ch, adt_val)

static s32 mlb150_dev_get_adt_sts(u32 ch)
{
	s32 timeout = 1000;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&ctr_lock, flags);
	__raw_writel(BUF_ADT_OFFSET + ch,
			mlb_base + REG_MADR);

	while ((!(__raw_readl(mlb_base + REG_MCTL)
			& MCTL_XCMP)) &&
			timeout--)
		;

	if (timeout <= 0) {
		spin_unlock_irqrestore(&ctr_lock, flags);
		pr_debug("mxc_mlb150: Read CTR timeout\n");
		return -ETIME;
	}

	reg = __raw_readl(mlb_base + REG_MDAT1);

	__raw_writel(0, mlb_base + REG_MCTL);
	spin_unlock_irqrestore(&ctr_lock, flags);

#ifdef DEBUG_ADT
	pr_debug("mxc_mlb150: Get ch %d adt sts: 0x%08x\n", ch, reg);
#endif

	return reg;
}

#ifdef DEBUG
static void mlb150_dev_dump_ctr_tbl(u32 ch_start, u32 ch_end)
{
	u32 i = 0;
	u32 ctr_val[4] = { 0 };

	pr_debug("mxc_mlb150: CDT Table");
	for (i = BUF_CDT_OFFSET + ch_start;
			i < BUF_CDT_OFFSET + ch_end;
			++i) {
		mlb150_dev_ctr_read(i, ctr_val);
		pr_debug("CTR 0x%02x: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			i, ctr_val[3], ctr_val[2], ctr_val[1], ctr_val[0]);
	}

	pr_debug("mxc_mlb150: ADT Table");
	for (i = BUF_ADT_OFFSET + ch_start;
			i < BUF_ADT_OFFSET + ch_end;
			++i) {
		mlb150_dev_ctr_read(i, ctr_val);
		pr_debug("CTR 0x%02x: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			i, ctr_val[3], ctr_val[2], ctr_val[1], ctr_val[0]);
	}

	pr_debug("mxc_mlb150: CAT MLB Table");
	for (i = BUF_CAT_MLB_OFFSET + (ch_start >> 3);
			i <= BUF_CAT_MLB_OFFSET + ((ch_end + 8) >> 3);
			++i) {
		mlb150_dev_ctr_read(i, ctr_val);
		pr_debug("CTR 0x%02x: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			i, ctr_val[3], ctr_val[2], ctr_val[1], ctr_val[0]);
	}

	pr_debug("mxc_mlb150: CAT HBI Table");
	for (i = BUF_CAT_HBI_OFFSET + (ch_start >> 3);
			i <= BUF_CAT_HBI_OFFSET + ((ch_end + 8) >> 3);
			++i) {
		mlb150_dev_ctr_read(i, ctr_val);
		pr_debug("CTR 0x%02x: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			i, ctr_val[3], ctr_val[2], ctr_val[1], ctr_val[0]);
	}
}
#endif

/*
 * Initial the MLB module device
 */
static inline void  mlb150_dev_enable_dma_irq(u32 enable)
{
	u32 ch_rx_mask = (1 << SYNC_RX_CL_AHB0) | (1 << CTRL_RX_CL_AHB0)
			| (1 << ASYNC_RX_CL_AHB0) | (1 << ISOC_RX_CL_AHB0)
			| (1 << SYNC_TX_CL_AHB0) | (1 << CTRL_TX_CL_AHB0)
			| (1 << ASYNC_TX_CL_AHB0) | (1 << ISOC_TX_CL_AHB0);
	u32 ch_tx_mask = (1 << (SYNC_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (CTRL_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ASYNC_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ISOC_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (SYNC_TX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (CTRL_TX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ASYNC_TX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ISOC_TX_CL_AHB1 - INT_AHB1_CH_START));

	if (enable) {
		__raw_writel(ch_rx_mask, mlb_base + REG_ACMR0);
		__raw_writel(ch_tx_mask, mlb_base + REG_ACMR1);
	} else {
		__raw_writel(0x0, mlb_base + REG_ACMR0);
		__raw_writel(0x0, mlb_base + REG_ACMR1);
	}
}


static void mlb150_dev_init_ir_amba_ahb(void)
{
	u32 reg = 0;

	/*
	 * Step 1. Program the ACMRn registers to enable interrupts from all
	 * active DMA channels
	 */
	mlb150_dev_enable_dma_irq(1);

	/*
	 * Step 2. Select the status clear method:
	 * ACTL.SCE = 0, hardware clears on read
	 * ACTL.SCE = 1, software writes a '1' to clear
	 * We only support DMA MODE 1
	 */
	reg = __raw_readl(mlb_base + REG_ACTL);
	reg |= ACTL_DMAMODE;
#ifdef MULTIPLE_PACKAGE_MODE
	reg |= REG_ACTL_MPB;
#endif

	/*
	 *  Step 3. Select 1 or 2 interrupt signals:
	 * ACTL.SMX = 0: one interrupt for channels 0 - 31 on ahb_init[0]
	 *	and another interrupt for channels 32 - 63 on ahb_init[1]
	 * ACTL.SMX = 1: singel interrupt all channels on ahb_init[0]
	 */
	reg &= ~ACTL_SMX;

	__raw_writel(reg, mlb_base + REG_ACTL);
}

static inline void mlb150_dev_enable_ir_mlb(u32 enable)
{
	/*
	 * Step 1, Select the MSn to be cleared by software,
	 * writing a '0' to the appropriate bits
	 */
	__raw_writel(0, mlb_base + REG_MS0);
	__raw_writel(0, mlb_base + REG_MS1);

	/*
	 * Step 1, Program MIEN to enable protocol error
	 * interrupts for all active MLB channels
	 */
	if (enable)
		__raw_writel(MIEN_CTX_PE |
			MIEN_CRX_PE | MIEN_ATX_PE |
			MIEN_ARX_PE | MIEN_SYNC_PE |
			MIEN_ISOC_PE,
			mlb_base + REG_MIEN);
	else
		__raw_writel(0, mlb_base + REG_MIEN);
}

static inline void mlb150_enable_pll(struct mlb_data *drvdata)
{
	u32 c0_val;

	__raw_writel(MLBPC1_VAL,
			drvdata->membase + REG_MLBPC1);

	c0_val = __raw_readl(drvdata->membase + REG_MLBC0);
	if (c0_val & MLBC0_MLBPEN) {
		c0_val &= ~MLBC0_MLBPEN;
		__raw_writel(c0_val,
				drvdata->membase + REG_MLBC0);
	}

	clk_prepare_enable(drvdata->clk_mlb6p);

	c0_val |= (MLBC0_MLBPEN);
	__raw_writel(c0_val, drvdata->membase + REG_MLBC0);
}

static inline void mlb150_disable_pll(struct mlb_data *drvdata)
{
	u32 c0_val;

	clk_disable_unprepare(drvdata->clk_mlb6p);

	c0_val = __raw_readl(drvdata->membase + REG_MLBC0);

	__raw_writel(0x0, drvdata->membase + REG_MLBPC1);

	c0_val &= ~MLBC0_MLBPEN;
	__raw_writel(c0_val, drvdata->membase + REG_MLBC0);
}

static void mlb150_dev_reset_cdt(void)
{
	int i = 0;
	u32 ctr_val[4] = { 0 };

	mlb150_dev_enable_ctr_write(0xffffffff, 0xffffffff,
			0xffffffff, 0xffffffff);

	for (i = 0; i < (LOGIC_CH_NUM); ++i)
		mlb150_dev_ctr_write(BUF_CDT_OFFSET + i, ctr_val);
}

static s32 mlb150_dev_init_ch_cdt(struct mlb_dev_info *pdevinfo, u32 ch,
		enum MLB_CTYPE ctype, u32 ch_func)
{
	u32 cdt_val[4] = { 0 };

	/* a. Set the 14-bit base address (BA) */
	pr_debug("mxc_mlb150: ctype: %d, ch: %d, dbr_buf_head: 0x%08x",
		ctype, ch, pdevinfo->channels[ch_func].dbr_buf_head);
	cdt_val[3] = (pdevinfo->channels[ch_func].dbr_buf_head)
			<< CDT_BA_SHIFT;
	/*
	 * b. Set the 12-bit or 13-bit buffer depth (BD)
	 * BD = buffer depth in bytes - 1
	 * For synchronous channels: (BD + 1) = 4 * m * bpf
	 * For control channels: (BD + 1) >= max packet length (64)
	 * For asynchronous channels: (BD + 1) >= max packet length
	 * 1024 for a MOST Data packet (MDP);
	 * 1536 for a MOST Ethernet Packet (MEP)
	 * For isochronous channels: (BD + 1) mod (BS + 1) = 0
	 * BS
	 */
	if (MLB_CTYPE_ISOC == ctype)
		cdt_val[1] |= (pdevinfo->isoc_blksz - 1);
	/* BD */
	cdt_val[3] |= (pdevinfo->cdt_buf_dep - 1) << CDT_BD_SHIFT;

	pr_debug("mxc_mlb150: Set CDT val of channel %d, type: %d: "
		"0x%08x 0x%08x 0x%08x 0x%08x\n",
		ch, ctype, cdt_val[3], cdt_val[2], cdt_val[1], cdt_val[0]);

	if (mlb150_dev_cdt_write(ch, cdt_val))
		return -ETIME;

#ifdef DEBUG_CTR
	{
		u32 cdt_rd[4] = { 0 };
		if (!mlb150_dev_cdt_read(ch, cdt_rd)) {
			pr_debug("mxc_mlb150: CDT val of channel %d: "
				"0x%08x 0x%08x 0x%08x 0x%08x\n",
				ch, cdt_rd[3], cdt_rd[2], cdt_rd[1], cdt_rd[0]);
			if (cdt_rd[3] == cdt_val[3] &&
				cdt_rd[2] == cdt_val[2] &&
				cdt_rd[1] == cdt_val[1] &&
				cdt_rd[0] == cdt_val[0]) {
				pr_debug("mxc_mlb150: set cdt succeed!\n");
				return 0;
			} else {
				pr_debug("mxc_mlb150: set cdt failed!\n");
				return -EBADE;
			}
		} else {
			pr_debug("mxc_mlb150: Read CDT val of channel %d failed\n",
					ch);
			return -EBADE;
		}
	}
#endif

	return 0;
}

static s32 mlb150_dev_init_ch_cat(u32 ch, u32 cl,
		u32 cat_mode, enum MLB_CTYPE ctype)
{
	u16 cat_val = 0;
#ifdef DEBUG_CTR
	u16 cat_rd = 0;
#endif

	cat_val = CAT_CE | (ctype << CAT_CT_SHIFT) | cl;

	if (cat_mode & CAT_MODE_OUTBOUND_DMA)
		cat_val |= CAT_RNW;

	if (MLB_CTYPE_SYNC == ctype)
		cat_val |= CAT_MT;

	switch (cat_mode) {
	case CAT_MODE_RX | CAT_MODE_INBOUND_DMA:
	case CAT_MODE_TX | CAT_MODE_OUTBOUND_DMA:
		pr_debug("mxc_mlb150: set CAT val of channel %d, type: %d: 0x%04x\n",
			ch, ctype, cat_val);

		if (mlb150_dev_cat_mlb_write(ch, cat_val))
			return -ETIME;
#ifdef DEBUG_CTR
		if (!mlb150_dev_cat_mlb_read(ch, &cat_rd))
			pr_debug("mxc_mlb150: CAT val of mlb channel %d: 0x%04x",
					ch, cat_rd);
		else {
			pr_debug("mxc_mlb150: Read CAT of mlb channel %d failed\n",
					ch);
				return -EBADE;
		}
#endif
		break;
	case CAT_MODE_TX | CAT_MODE_INBOUND_DMA:
	case CAT_MODE_RX | CAT_MODE_OUTBOUND_DMA:
		pr_debug("mxc_mlb150: set CAT val of channel %d, type: %d: 0x%04x\n",
			cl, ctype, cat_val);

		if (mlb150_dev_cat_hbi_write(cl, cat_val))
			return -ETIME;
#ifdef DEBUG_CTR
		if (!mlb150_dev_cat_hbi_read(cl, &cat_rd))
			pr_debug("mxc_mlb150: CAT val of hbi channel %d: 0x%04x",
					cl, cat_rd);
		else {
			pr_debug("mxc_mlb150: Read CAT of hbi channel %d failed\n",
					cl);
				return -EBADE;
		}
#endif
		break;
	default:
		return EBADRQC;
	}

#ifdef DEBUG_CTR
	{
		if (cat_val == cat_rd) {
			pr_debug("mxc_mlb150: set cat succeed!\n");
			return 0;
		} else {
			pr_debug("mxc_mlb150: set cat failed!\n");
			return -EBADE;
		}
	}
#endif
	return 0;
}

static void mlb150_dev_reset_cat(void)
{
	int i = 0;
	u32 ctr_val[4] = { 0 };

	mlb150_dev_enable_ctr_write(0xffffffff, 0xffffffff,
			0xffffffff, 0xffffffff);

	for (i = 0; i < (LOGIC_CH_NUM >> 3); ++i) {
		mlb150_dev_ctr_write(BUF_CAT_MLB_OFFSET + i, ctr_val);
		mlb150_dev_ctr_write(BUF_CAT_HBI_OFFSET + i, ctr_val);
	}
}

static void mlb150_dev_init_rfb(struct mlb_dev_info *pdevinfo, u32 rx_ch,
		u32 tx_ch, enum MLB_CTYPE ctype)
{
	u32 rx_cl = pdevinfo->channels[RX_CHANNEL].cl;
	u32 tx_cl = pdevinfo->channels[TX_CHANNEL].cl;
	/* Step 1, Initialize all bits of CAT to '0' */
	mlb150_dev_reset_cat();
	mlb150_dev_reset_cdt();
	/*
	 * Step 2, Initialize logical channel
	 * Step 3, Program the CDT for channel N
	 */
	mlb150_dev_init_ch_cdt(pdevinfo, rx_cl, ctype, RX_CHANNEL);
	mlb150_dev_init_ch_cdt(pdevinfo, tx_cl, ctype, TX_CHANNEL);

	/* Step 4&5, Program the CAT for the inbound and outbound DMA */
	mlb150_dev_init_ch_cat(rx_ch, rx_cl,
			CAT_MODE_RX | CAT_MODE_INBOUND_DMA,
			ctype);
	mlb150_dev_init_ch_cat(rx_ch, rx_cl,
			CAT_MODE_RX | CAT_MODE_OUTBOUND_DMA,
			ctype);
	mlb150_dev_init_ch_cat(tx_ch, tx_cl,
			CAT_MODE_TX | CAT_MODE_INBOUND_DMA,
			ctype);
	mlb150_dev_init_ch_cat(tx_ch, tx_cl,
			CAT_MODE_TX | CAT_MODE_OUTBOUND_DMA,
			ctype);
}

static void mlb150_dev_reset_adt(void)
{
	int i = 0;
	u32 ctr_val[4] = { 0 };

	mlb150_dev_enable_ctr_write(0xffffffff, 0xffffffff,
			0xffffffff, 0xffffffff);

	for (i = 0; i < (LOGIC_CH_NUM); ++i)
		mlb150_dev_ctr_write(BUF_ADT_OFFSET + i, ctr_val);
}

static void mlb150_dev_reset_whole_ctr(void)
{
	mlb150_dev_enable_ctr_write(0xffffffff, 0xffffffff,
			0xffffffff, 0xffffffff);
	mlb150_dev_reset_cdt();
	mlb150_dev_reset_adt();
	mlb150_dev_reset_cat();
}

#define CLR_REG(reg)  __raw_writel(0x0, mlb_base + reg)

static void mlb150_dev_reset_all_regs(void)
{
	CLR_REG(REG_MLBC0);
	CLR_REG(REG_MLBPC0);
	CLR_REG(REG_MS0);
	CLR_REG(REG_MS1);
	CLR_REG(REG_MSS);
	CLR_REG(REG_MSD);
	CLR_REG(REG_MIEN);
	CLR_REG(REG_MLBPC2);
	CLR_REG(REG_MLBPC1);
	CLR_REG(REG_MLBC1);
	CLR_REG(REG_HCTL);
	CLR_REG(REG_HCMR0);
	CLR_REG(REG_HCMR1);
	CLR_REG(REG_HCER0);
	CLR_REG(REG_HCER1);
	CLR_REG(REG_HCBR0);
	CLR_REG(REG_HCBR1);
	CLR_REG(REG_MDAT0);
	CLR_REG(REG_MDAT1);
	CLR_REG(REG_MDAT2);
	CLR_REG(REG_MDAT3);
	CLR_REG(REG_MDWE0);
	CLR_REG(REG_MDWE1);
	CLR_REG(REG_MDWE2);
	CLR_REG(REG_MDWE3);
	CLR_REG(REG_MCTL);
	CLR_REG(REG_MADR);
	CLR_REG(REG_ACTL);
	CLR_REG(REG_ACSR0);
	CLR_REG(REG_ACSR1);
	CLR_REG(REG_ACMR0);
	CLR_REG(REG_ACMR1);
}

static inline s32 mlb150_dev_pipo_start(struct mlb_ringbuf *rbuf,
						u32 ahb_ch, u32 buf_addr)
{
	u32 ctr_val[4] = { 0 };

	ctr_val[1] |= ADT_RDY1;
	ctr_val[2] = buf_addr;

	if (mlb150_dev_adt_write(ahb_ch, ctr_val))
		return -ETIME;

	return 0;
}

static inline s32 mlb150_dev_pipo_next(u32 ahb_ch, enum MLB_CTYPE ctype,
				u32 dne_sts, u32 buf_addr)
{
	u32 ctr_val[4] = { 0 };

	if (MLB_CTYPE_ASYNC == ctype ||
		MLB_CTYPE_CTRL == ctype) {
		ctr_val[1] |= ADT_PS1;
		ctr_val[1] |= ADT_PS2;
	}

	/*
	 * Clear DNE1 and ERR1
	 * Set the page ready bit (RDY1)
	 */
	if (dne_sts & ADT_DNE1) {
		ctr_val[1] |= ADT_RDY2;
		ctr_val[3] = buf_addr;
	} else {
		ctr_val[1] |= ADT_RDY1;
		ctr_val[2] = buf_addr;
	}

	if (mlb150_dev_adt_write(ahb_ch, ctr_val))
		return -ETIME;

	return 0;
}

static inline s32 mlb150_dev_pipo_stop(struct mlb_ringbuf *rbuf, u32 ahb_ch)
{
	u32 ctr_val[4] = { 0 };
	unsigned long flags;

	write_lock_irqsave(&rbuf->rb_lock, flags);
	rbuf->head = rbuf->tail = 0;
	write_unlock_irqrestore(&rbuf->rb_lock, flags);

	if (mlb150_dev_adt_write(ahb_ch, ctr_val))
		return -ETIME;

	return 0;
}

static s32 mlb150_dev_init_ch_amba_ahb(struct mlb_dev_info *pdevinfo,
					struct mlb_channel_info *chinfo,
					enum MLB_CTYPE ctype)
{
	u32 ctr_val[4] = { 0 };

	/* a. Set the 32-bit base address (BA1) */
	ctr_val[3] = 0;
	ctr_val[2] = 0;
	ctr_val[1] = (pdevinfo->adt_buf_dep - 1) << ADT_BD1_SHIFT;
	ctr_val[1] |= (pdevinfo->adt_buf_dep - 1) << ADT_BD2_SHIFT;
	if (MLB_CTYPE_ASYNC == ctype ||
		MLB_CTYPE_CTRL == ctype) {
		ctr_val[1] |= ADT_PS1;
		ctr_val[1] |= ADT_PS2;
	}

	ctr_val[0] |= (ADT_LE | ADT_CE);

	pr_debug("mxc_mlb150: Set ADT val of channel %d, ctype: %d: "
		"0x%08x 0x%08x 0x%08x 0x%08x\n",
		chinfo->cl, ctype, ctr_val[3], ctr_val[2],
		ctr_val[1], ctr_val[0]);

	if (mlb150_dev_adt_write(chinfo->cl, ctr_val))
		return -ETIME;

#ifdef DEBUG_CTR
	{
		u32 ctr_rd[4] = { 0 };
		if (!mlb150_dev_adt_read(chinfo->cl, ctr_rd)) {
			pr_debug("mxc_mlb150: ADT val of channel %d: "
				"0x%08x 0x%08x 0x%08x 0x%08x\n",
				chinfo->cl, ctr_rd[3], ctr_rd[2],
				ctr_rd[1], ctr_rd[0]);
			if (ctr_rd[3] == ctr_val[3] &&
				ctr_rd[2] == ctr_val[2] &&
				ctr_rd[1] == ctr_val[1] &&
				ctr_rd[0] == ctr_val[0]) {
				pr_debug("mxc_mlb150: set adt succeed!\n");
				return 0;
			} else {
				pr_debug("mxc_mlb150: set adt failed!\n");
				return -EBADE;
			}
		} else {
			pr_debug("mxc_mlb150: Read ADT val of channel %d failed\n",
					chinfo->cl);
			return -EBADE;
		}
	}
#endif

	return 0;
}

static void mlb150_dev_init_amba_ahb(struct mlb_dev_info *pdevinfo,
					enum MLB_CTYPE ctype)
{
	struct mlb_channel_info *tx_chinfo = &pdevinfo->channels[TX_CHANNEL];
	struct mlb_channel_info *rx_chinfo = &pdevinfo->channels[RX_CHANNEL];

	/* Step 1, Initialize all bits of the ADT to '0' */
	mlb150_dev_reset_adt();

	/*
	 * Step 2, Select a logic channel
	 * Step 3, Program the AMBA AHB block ping page for channel N
	 * Step 4, Program the AMBA AHB block pong page for channel N
	 */
	mlb150_dev_init_ch_amba_ahb(pdevinfo, rx_chinfo, ctype);
	mlb150_dev_init_ch_amba_ahb(pdevinfo, tx_chinfo, ctype);
}

static void mlb150_dev_exit(void)
{
	u32 c0_val, hctl_val;

	/* Disable EN bits */
	c0_val = __raw_readl(mlb_base + REG_MLBC0);
	c0_val &= ~(MLBC0_MLBEN | MLBC0_MLBPEN);
	__raw_writel(c0_val, mlb_base + REG_MLBC0);

	hctl_val = __raw_readl(mlb_base + REG_HCTL);
	hctl_val &= ~HCTL_EN;
	__raw_writel(hctl_val, mlb_base + REG_HCTL);

	__raw_writel(0x0, mlb_base + REG_HCMR0);
	__raw_writel(0x0, mlb_base + REG_HCMR1);

	mlb150_dev_enable_dma_irq(0);
	mlb150_dev_enable_ir_mlb(0);
}

static void mlb150_dev_init(void)
{
	u32 c0_val;
	u32 ch_rx_mask = (1 << SYNC_RX_CL_AHB0) | (1 << CTRL_RX_CL_AHB0)
			| (1 << ASYNC_RX_CL_AHB0) | (1 << ISOC_RX_CL_AHB0)
			| (1 << SYNC_TX_CL_AHB0) | (1 << CTRL_TX_CL_AHB0)
			| (1 << ASYNC_TX_CL_AHB0) | (1 << ISOC_TX_CL_AHB0);
	u32 ch_tx_mask = (1 << (SYNC_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (CTRL_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ASYNC_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ISOC_RX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (SYNC_TX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (CTRL_TX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ASYNC_TX_CL_AHB1 - INT_AHB1_CH_START)) |
			(1 << (ISOC_TX_CL_AHB1 - INT_AHB1_CH_START));

	/* Disable EN bits */
	mlb150_dev_exit();

	/*
	 * Step 1. Initialize CTR and registers
	 * a. Set all bit of the CTR (CAT, CDT, and ADT) to 0.
	 */
	mlb150_dev_reset_whole_ctr();

	/* a. Set all bit of the CTR (CAT, CDT, and ADT) to 0. */
	mlb150_dev_reset_all_regs();

	/*
	 * Step 2, Configure the MediaLB interface
	 * Select pin mode and clock, 3-pin and 256fs
	 */
	c0_val = __raw_readl(mlb_base + REG_MLBC0);
	c0_val &= ~(MLBC0_MLBPEN | MLBC0_MLBCLK_MASK);
	__raw_writel(c0_val, mlb_base + REG_MLBC0);

	c0_val |= MLBC0_MLBEN;
	__raw_writel(c0_val, mlb_base + REG_MLBC0);

	/* Step 3, Configure the HBI interface */
	__raw_writel(ch_rx_mask, mlb_base + REG_HCMR0);
	__raw_writel(ch_tx_mask, mlb_base + REG_HCMR1);
	__raw_writel(HCTL_EN, mlb_base + REG_HCTL);

	mlb150_dev_init_ir_amba_ahb();

	mlb150_dev_enable_ir_mlb(1);
}

static s32 mlb150_dev_unmute_syn_ch(u32 rx_ch, u32 rx_cl, u32 tx_ch, u32 tx_cl)
{
	u32 timeout = 10000;

	/*
	 * Check that MediaLB clock is running (MLBC1.CLKM = 0)
	 * If MLBC1.CLKM = 1, clear the register bit, wait one
	 * APB or I/O clock cycle and repeat the check
	 */
	while ((__raw_readl(mlb_base + REG_MLBC1) & MLBC1_CLKM)
			&& --timeout)
		__raw_writel(~MLBC1_CLKM, mlb_base + REG_MLBC1);

	if (0 == timeout)
		return -ETIME;

	timeout = 10000;
	/* Poll for MLB lock (MLBC0.MLBLK = 1) */
	while (!(__raw_readl(mlb_base + REG_MLBC0) & MLBC0_MLBLK)
			&& --timeout)
		;

	if (0 == timeout)
		return -ETIME;

	/* Unmute synchronous channel(s) */
	mlb150_dev_cat_mlb_write(rx_ch, CAT_CE | rx_cl);
	mlb150_dev_cat_mlb_write(tx_ch,
			CAT_CE | tx_cl | CAT_RNW);
	mlb150_dev_cat_hbi_write(rx_cl,
			CAT_CE | rx_cl | CAT_RNW);
	mlb150_dev_cat_hbi_write(tx_cl, CAT_CE | tx_cl);

	return 0;
}

/* In case the user calls channel shutdown, but rx or tx is not completed yet */
static s32 mlb150_trans_complete_check(struct mlb_dev_info *pdevinfo)
{
	struct mlb_ringbuf *rx_rbuf = &pdevinfo->rx_rbuf;
	struct mlb_ringbuf *tx_rbuf = &pdevinfo->tx_rbuf;
	s32 timeout = 1024;

	while (timeout--) {
		read_lock(&tx_rbuf->rb_lock);
		if (!CIRC_CNT(tx_rbuf->head, tx_rbuf->tail, TRANS_RING_NODES)) {
			read_unlock(&tx_rbuf->rb_lock);
			break;
		} else
			read_unlock(&tx_rbuf->rb_lock);
	}

	if (timeout <= 0) {
		pr_debug("TX complete check timeout!\n");
		return -ETIME;
	}

	timeout = 1024;
	while (timeout--) {
		read_lock(&rx_rbuf->rb_lock);
		if (!CIRC_CNT(rx_rbuf->head, rx_rbuf->tail, TRANS_RING_NODES)) {
			read_unlock(&rx_rbuf->rb_lock);
			break;
		} else
			read_unlock(&rx_rbuf->rb_lock);
	}

	if (timeout <= 0) {
		pr_debug("RX complete check timeout!\n");
		return -ETIME;
	}

	/*
	 * Interrupt from TX can only inform that the data is sent
	 * to AHB bus, not mean that it is sent to MITB. Thus we add
	 * a delay here for data to be completed sent.
	 */
	udelay(1000);

	return 0;
}

/*
 * Enable/Disable the MLB IRQ
 */
static void mxc_mlb150_irq_enable(struct mlb_data *drvdata, u8 enable)
{
	if (enable) {
		enable_irq(drvdata->irq_ahb0);
		enable_irq(drvdata->irq_ahb1);
		enable_irq(drvdata->irq_mlb);
	} else {
		disable_irq(drvdata->irq_ahb0);
		disable_irq(drvdata->irq_ahb1);
		disable_irq(drvdata->irq_mlb);
	}
}

/*
 * Enable the MLB channel
 */
static s32 mlb_channel_enable(struct mlb_data *drvdata,
				int chan_dev_id, int on)
{
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;
	struct mlb_channel_info *tx_chinfo = &pdevinfo->channels[TX_CHANNEL];
	struct mlb_channel_info *rx_chinfo = &pdevinfo->channels[RX_CHANNEL];
	u32 tx_ch = tx_chinfo->address;
	u32 rx_ch = rx_chinfo->address;
	u32 tx_cl = tx_chinfo->cl;
	u32 rx_cl = rx_chinfo->cl;
	s32 ret = 0;

	/*
	 * setup the direction, enable, channel type,
	 * mode select, channel address and mask buf start
	 */
	if (on) {
		u32 ctype = pdevinfo->channel_type;

		mlb150_dev_enable_ctr_write(0xffffffff, 0xffffffff,
				0xffffffff, 0xffffffff);
		mlb150_dev_init_rfb(pdevinfo, rx_ch, tx_ch, ctype);

		mlb150_dev_init_amba_ahb(pdevinfo, ctype);

#ifdef DEBUG
		mlb150_dev_dump_ctr_tbl(0, tx_chinfo->cl + 1);
#endif
		/* Synchronize and unmute synchrouous channel */
		if (MLB_CTYPE_SYNC == ctype) {
			ret = mlb150_dev_unmute_syn_ch(rx_ch, rx_cl,
							tx_ch, tx_cl);
			if (ret)
				return ret;
		}

		mlb150_dev_enable_ctr_write(0x0, ADT_RDY1 | ADT_DNE1 |
				ADT_ERR1 | ADT_PS1 |
				ADT_RDY2 | ADT_DNE2 | ADT_ERR2 | ADT_PS2,
				0xffffffff, 0xffffffff);

		if (pdevinfo->fps >= CLK_2048FS)
			mlb150_enable_pll(drvdata);

		atomic_set(&pdevinfo->on, 1);

#ifdef DEBUG
		mlb150_dev_dump_reg();
		mlb150_dev_dump_ctr_tbl(0, tx_chinfo->cl + 1);
#endif
		/* Init RX ADT */
		mlb150_dev_pipo_start(&pdevinfo->rx_rbuf, rx_cl,
					pdevinfo->rx_rbuf.phy_addrs[0]);
	} else {
		mlb150_dev_pipo_stop(&pdevinfo->rx_rbuf, rx_cl);

		mlb150_dev_enable_dma_irq(0);
		mlb150_dev_enable_ir_mlb(0);

		mlb150_dev_reset_cat();

		atomic_set(&pdevinfo->on, 0);

		if (pdevinfo->fps >= CLK_2048FS)
			mlb150_disable_pll(drvdata);
	}

	return 0;
}

/*
 * MLB interrupt handler
 */
static void mlb_rx_isr(s32 ctype, u32 ahb_ch, struct mlb_dev_info *pdevinfo)
{
	struct mlb_ringbuf *rx_rbuf = &pdevinfo->rx_rbuf;
	s32 head, tail, adt_sts;
	u32 rx_buf_ptr;

#ifdef DEBUG_RX
	pr_debug("mxc_mlb150: mlb_rx_isr\n");
#endif

	read_lock(&rx_rbuf->rb_lock);

	head = (rx_rbuf->head + 1) & (TRANS_RING_NODES - 1);
	tail = ACCESS_ONCE(rx_rbuf->tail);
	read_unlock(&rx_rbuf->rb_lock);

	if (CIRC_SPACE(head, tail, TRANS_RING_NODES) >= 1) {
		rx_buf_ptr = rx_rbuf->phy_addrs[head];

		/* commit the item before incrementing the head */
		smp_wmb();

		write_lock(&rx_rbuf->rb_lock);
		rx_rbuf->head = head;
		write_unlock(&rx_rbuf->rb_lock);

		/* wake up the reader */
		wake_up_interruptible(&pdevinfo->rx_wq);
	} else {
		rx_buf_ptr = rx_rbuf->phy_addrs[head];
		pr_debug("drop RX package, due to no space, (%d,%d)\n",
				head, tail);
	}

	adt_sts = mlb150_dev_get_adt_sts(ahb_ch);
	/*  Set ADT for RX */
	mlb150_dev_pipo_next(ahb_ch, ctype, adt_sts, rx_buf_ptr);
}

static void mlb_tx_isr(s32 ctype, u32 ahb_ch, struct mlb_dev_info *pdevinfo)
{
	struct mlb_ringbuf *tx_rbuf = &pdevinfo->tx_rbuf;
	s32 head, tail, adt_sts;
	u32 tx_buf_ptr;

	read_lock(&tx_rbuf->rb_lock);

	head = ACCESS_ONCE(tx_rbuf->head);
	tail = (tx_rbuf->tail + 1) & (TRANS_RING_NODES - 1);
	read_unlock(&tx_rbuf->rb_lock);

	smp_mb();
	write_lock(&tx_rbuf->rb_lock);
	tx_rbuf->tail = tail;
	write_unlock(&tx_rbuf->rb_lock);

	/* check the current tx buffer is available or not */
	if (CIRC_CNT(head, tail, TRANS_RING_NODES) >= 1) {
		/* read index before reading contents at that index */
		smp_read_barrier_depends();

		tx_buf_ptr = tx_rbuf->phy_addrs[tail];

		wake_up_interruptible(&pdevinfo->tx_wq);

		adt_sts = mlb150_dev_get_adt_sts(ahb_ch);
		/*  Set ADT for TX */
		mlb150_dev_pipo_next(ahb_ch, ctype, adt_sts, tx_buf_ptr);
	}
}

static irqreturn_t mlb_ahb_isr(int irq, void *dev_id)
{
	u32 acsr0, hcer0;
	u32 ch_mask = (1 << SYNC_RX_CL) | (1 << CTRL_RX_CL)
			| (1 << ASYNC_RX_CL) | (1 << ISOC_RX_CL)
			| (1 << SYNC_TX_CL) | (1 << CTRL_TX_CL)
			| (1 << ASYNC_TX_CL) | (1 << ISOC_TX_CL);

	/*
	 * Step 5, Read the ACSRn registers to determine which channel or
	 * channels are causing the interrupt
	 */
	acsr0 = __raw_readl(mlb_base + REG_ACSR0);

	hcer0 = __raw_readl(mlb_base + REG_HCER0);

	/*
	 * Step 6, If ACTL.SCE = 1, write the result of step 5 back to ACSR0
	 * and ACSR1 to clear the interrupt
	 * We'll not set ACTL_SCE
	 */

	if (ch_mask & hcer0)
		pr_err("CH encounters an AHB error: 0x%x\n", hcer0);

	if ((1 << SYNC_RX_CL) & acsr0)
		mlb_rx_isr(MLB_CTYPE_SYNC, SYNC_RX_CL,
				&mlb_devinfo[MLB_CTYPE_SYNC]);

	if ((1 << CTRL_RX_CL) & acsr0)
		mlb_rx_isr(MLB_CTYPE_CTRL, CTRL_RX_CL,
				&mlb_devinfo[MLB_CTYPE_CTRL]);

	if ((1 << ASYNC_RX_CL) & acsr0)
		mlb_rx_isr(MLB_CTYPE_ASYNC, ASYNC_RX_CL,
				&mlb_devinfo[MLB_CTYPE_ASYNC]);

	if ((1 << ISOC_RX_CL) & acsr0)
		mlb_rx_isr(MLB_CTYPE_ISOC, ISOC_RX_CL,
				&mlb_devinfo[MLB_CTYPE_ISOC]);

	if ((1 << SYNC_TX_CL) & acsr0)
		mlb_tx_isr(MLB_CTYPE_SYNC, SYNC_TX_CL,
				&mlb_devinfo[MLB_CTYPE_SYNC]);

	if ((1 << CTRL_TX_CL) & acsr0)
		mlb_tx_isr(MLB_CTYPE_CTRL, CTRL_TX_CL,
				&mlb_devinfo[MLB_CTYPE_CTRL]);

	if ((1 << ASYNC_TX_CL) & acsr0)
		mlb_tx_isr(MLB_CTYPE_ASYNC, ASYNC_TX_CL,
				&mlb_devinfo[MLB_CTYPE_ASYNC]);

	if ((1 << ISOC_TX_CL) & acsr0)
		mlb_tx_isr(MLB_CTYPE_ASYNC, ISOC_TX_CL,
				&mlb_devinfo[MLB_CTYPE_ISOC]);

	return IRQ_HANDLED;
}

static irqreturn_t mlb_isr(int irq, void *dev_id)
{
	u32 rx_int_sts, tx_int_sts, ms0,
		ms1, tx_cis, rx_cis, ctype;
	int minor;
	u32 cdt_val[4] = { 0 };

	/*
	 * Step 4, Read the MSn register to determine which channel(s)
	 * are causing the interrupt
	 */
	ms0 = __raw_readl(mlb_base + REG_MS0);
	ms1 = __raw_readl(mlb_base + REG_MS1);

	/*
	 * The MLB150_MS0, MLB150_MS1 registers need to be cleared. In
	 * the spec description, the registers should  be cleared when
	 * enabling interrupt. In fact, we also should clear it in ISR.
	 */
	__raw_writel(0, mlb_base + REG_MS0);
	__raw_writel(0, mlb_base + REG_MS1);

	pr_debug("mxc_mlb150: mlb interrupt:0x%08x 0x%08x\n",
			(u32)ms0, (u32)ms1);

	for (minor = 0; minor < MLB_MINOR_DEVICES; minor++) {
		struct mlb_dev_info *pdevinfo = &mlb_devinfo[minor];
		u32 rx_mlb_ch = pdevinfo->channels[RX_CHANNEL].address;
		u32 tx_mlb_ch = pdevinfo->channels[TX_CHANNEL].address;
		u32 rx_mlb_cl = pdevinfo->channels[RX_CHANNEL].cl;
		u32 tx_mlb_cl = pdevinfo->channels[TX_CHANNEL].cl;

		tx_cis = rx_cis = 0;

		ctype = pdevinfo->channel_type;
		rx_int_sts = (rx_mlb_ch < 31) ? ms0 : ms1;
		tx_int_sts = (tx_mlb_ch < 31) ? ms0 : ms1;

		pr_debug("mxc_mlb150: channel interrupt: "
				"tx %d: 0x%08x, rx %d: 0x%08x\n",
			tx_mlb_ch, (u32)tx_int_sts, rx_mlb_ch, (u32)rx_int_sts);

		/* Get tx channel interrupt status */
		if (tx_int_sts & (1 << (tx_mlb_ch % 32))) {
			mlb150_dev_cdt_read(tx_mlb_cl, cdt_val);
			pr_debug("mxc_mlb150: TX_CH: %d, cdt_val[3]: 0x%08x, "
					"cdt_val[2]: 0x%08x, "
					"cdt_val[1]: 0x%08x, "
					"cdt_val[0]: 0x%08x\n",
					tx_mlb_ch, cdt_val[3], cdt_val[2],
					cdt_val[1], cdt_val[0]);
			switch (ctype) {
			case MLB_CTYPE_SYNC:
				tx_cis = (cdt_val[2] & CDT_SYNC_WSTS_MASK)
					>> CDT_SYNC_WSTS_SHIFT;
				/*
				 * Clear RSTS/WSTS errors to resume
				 * channel operation
				 * a. For synchronous channels: WSTS[3] = 0
				 */
				cdt_val[2] &= ~(0x8 << CDT_SYNC_WSTS_SHIFT);
				break;
			case MLB_CTYPE_CTRL:
			case MLB_CTYPE_ASYNC:
				tx_cis = (cdt_val[2] &
					CDT_CTRL_ASYNC_WSTS_MASK)
					>> CDT_CTRL_ASYNC_WSTS_SHIFT;
				tx_cis = (cdt_val[3] & CDT_CTRL_ASYNC_WSTS_1) ?
					(tx_cis | (0x1 << 4)) : tx_cis;
				/*
				 * b. For async and ctrl channels:
				 * RSTS[4]/WSTS[4] = 0
				 * and RSTS[2]/WSTS[2] = 0
				 */
				cdt_val[3] &= ~CDT_CTRL_ASYNC_WSTS_1;
				cdt_val[2] &=
					~(0x4 << CDT_CTRL_ASYNC_WSTS_SHIFT);
				break;
			case MLB_CTYPE_ISOC:
				tx_cis = (cdt_val[2] & CDT_ISOC_WSTS_MASK)
					>> CDT_ISOC_WSTS_SHIFT;
				/* c. For isoc channels: WSTS[2:1] = 0x00 */
				cdt_val[2] &= ~(0x6 << CDT_ISOC_WSTS_SHIFT);
				break;
			default:
				break;
			}
			mlb150_dev_cdt_write(tx_mlb_ch, cdt_val);
		}

		/* Get rx channel interrupt status */
		if (rx_int_sts & (1 << (rx_mlb_ch % 32))) {
			mlb150_dev_cdt_read(rx_mlb_cl, cdt_val);
			pr_debug("mxc_mlb150: RX_CH: %d, cdt_val[3]: 0x%08x, "
					"cdt_val[2]: 0x%08x, "
					"cdt_val[1]: 0x%08x, "
					"cdt_val[0]: 0x%08x\n",
					rx_mlb_ch, cdt_val[3], cdt_val[2],
					cdt_val[1], cdt_val[0]);
			switch (ctype) {
			case MLB_CTYPE_SYNC:
				rx_cis = (cdt_val[2] & CDT_SYNC_RSTS_MASK)
					>> CDT_SYNC_RSTS_SHIFT;
				cdt_val[2] &= ~(0x8 << CDT_SYNC_WSTS_SHIFT);
				break;
			case MLB_CTYPE_CTRL:
			case MLB_CTYPE_ASYNC:
				rx_cis =
					(cdt_val[2] & CDT_CTRL_ASYNC_RSTS_MASK)
					>> CDT_CTRL_ASYNC_RSTS_SHIFT;
				rx_cis = (cdt_val[3] & CDT_CTRL_ASYNC_RSTS_1) ?
					(rx_cis | (0x1 << 4)) : rx_cis;
				cdt_val[3] &= ~CDT_CTRL_ASYNC_RSTS_1;
				cdt_val[2] &=
					~(0x4 << CDT_CTRL_ASYNC_RSTS_SHIFT);
				break;
			case MLB_CTYPE_ISOC:
				rx_cis = (cdt_val[2] & CDT_ISOC_RSTS_MASK)
					>> CDT_ISOC_RSTS_SHIFT;
				cdt_val[2] &= ~(0x6 << CDT_ISOC_WSTS_SHIFT);
				break;
			default:
				break;
			}
			mlb150_dev_cdt_write(rx_mlb_ch, cdt_val);
		}

		if (!tx_cis && !rx_cis)
			continue;

		/* fill exception event */
		spin_lock(&pdevinfo->event_lock);
		pdevinfo->ex_event |= (rx_cis << 16) | tx_cis;
		spin_unlock(&pdevinfo->event_lock);
	}

	return IRQ_HANDLED;
}

static int mxc_mlb150_open(struct inode *inode, struct file *filp)
{
	int minor, ring_buf_size, buf_size, j, ret;
	void __iomem *buf_addr;
	ulong phy_addr;
	struct mlb_dev_info *pdevinfo = NULL;
	struct mlb_channel_info *pchinfo = NULL;
	struct mlb_data *drvdata;

	minor = MINOR(inode->i_rdev);
	drvdata = container_of(inode->i_cdev, struct mlb_data, cdev);

	if (minor < 0 || minor >= MLB_MINOR_DEVICES) {
		pr_err("no device\n");
		return -ENODEV;
	}

	/* open for each channel device */
	if (atomic_cmpxchg(&mlb_devinfo[minor].opencnt, 0, 1) != 0) {
		pr_err("busy\n");
		return -EBUSY;
	}

	clk_prepare_enable(drvdata->clk_mlb3p);

	/* initial MLB module */
	mlb150_dev_init();

	pdevinfo = &mlb_devinfo[minor];
	pchinfo = &pdevinfo->channels[TX_CHANNEL];

	ring_buf_size = pdevinfo->buf_size;
	buf_size = ring_buf_size * (TRANS_RING_NODES * 2);
	buf_addr = (void __iomem *)gen_pool_alloc(drvdata->iram_pool, buf_size);
	if (buf_addr == NULL) {
		ret = -ENOMEM;
		pr_err("can not alloc rx/tx buffers: %d\n", buf_size);
		return ret;
	}
	phy_addr = gen_pool_virt_to_phys(drvdata->iram_pool, (ulong)buf_addr);
	pr_debug("IRAM Range: Virt 0x%p - 0x%p, Phys 0x%x - 0x%x, size: 0x%x\n",
			buf_addr, (buf_addr + buf_size - 1), (u32)phy_addr,
			(u32)(phy_addr + buf_size - 1), buf_size);
	pdevinfo->rbuf_base_virt = buf_addr;
	pdevinfo->rbuf_base_phy = phy_addr;
	drvdata->iram_size = buf_size;

	memset(buf_addr, 0, buf_size);

	for (j = 0; j < (TRANS_RING_NODES);
		++j, buf_addr += ring_buf_size, phy_addr += ring_buf_size) {
		pdevinfo->rx_rbuf.virt_bufs[j] = buf_addr;
		pdevinfo->rx_rbuf.phy_addrs[j] = phy_addr;
		pr_debug("RX Ringbuf[%d]: 0x%p 0x%x\n",
			j, buf_addr, (u32)phy_addr);
	}
	pdevinfo->rx_rbuf.unit_size = ring_buf_size;
	pdevinfo->rx_rbuf.total_size = buf_size;
	for (j = 0; j < (TRANS_RING_NODES);
		++j, buf_addr += ring_buf_size, phy_addr += ring_buf_size) {
		pdevinfo->tx_rbuf.virt_bufs[j] = buf_addr;
		pdevinfo->tx_rbuf.phy_addrs[j] = phy_addr;
		pr_debug("TX Ringbuf[%d]: 0x%p 0x%x\n",
			j, buf_addr, (u32)phy_addr);
	}

	pdevinfo->tx_rbuf.unit_size = ring_buf_size;
	pdevinfo->tx_rbuf.total_size = buf_size;

	/* reset the buffer read/write ptr */
	pdevinfo->rx_rbuf.head = pdevinfo->rx_rbuf.tail = 0;
	pdevinfo->tx_rbuf.head = pdevinfo->tx_rbuf.tail = 0;
	pdevinfo->ex_event = 0;
	pdevinfo->tx_ok = 0;

	init_waitqueue_head(&pdevinfo->rx_wq);
	init_waitqueue_head(&pdevinfo->tx_wq);

	drvdata = container_of(inode->i_cdev, struct mlb_data, cdev);
	drvdata->devinfo = pdevinfo;
	mxc_mlb150_irq_enable(drvdata, 1);
	filp->private_data = drvdata;

	return 0;
}

static int mxc_mlb150_release(struct inode *inode, struct file *filp)
{
	int minor;
	struct mlb_data *drvdata = filp->private_data;
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;

	minor = MINOR(inode->i_rdev);
	mxc_mlb150_irq_enable(drvdata, 0);

#ifdef DEBUG
	mlb150_dev_dump_reg();
	mlb150_dev_dump_ctr_tbl(0, pdevinfo->channels[TX_CHANNEL].cl + 1);
#endif

	gen_pool_free(drvdata->iram_pool,
			(ulong)pdevinfo->rbuf_base_virt, drvdata->iram_size);

	mlb150_dev_exit();

	if (pdevinfo && atomic_read(&pdevinfo->on)
		&& (pdevinfo->fps >= CLK_2048FS))
		clk_disable_unprepare(drvdata->clk_mlb6p);

	atomic_set(&pdevinfo->on, 0);

	clk_disable_unprepare(drvdata->clk_mlb3p);
	/* decrease the open count */
	atomic_set(&pdevinfo->opencnt, 0);

	drvdata->devinfo = NULL;

	return 0;
}

static long mxc_mlb150_ioctl(struct file *filp,
			 unsigned int cmd, unsigned long arg)
{
	//struct inode *inode = filp->f_dentry->d_inode;
	struct inode *inode = file_inode(filp);
	struct mlb_data *drvdata = filp->private_data;
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;
	void __user *argp = (void __user *)arg;
	unsigned long flags, event;
	int minor;

	minor = MINOR(inode->i_rdev);

	switch (cmd) {
	case MLB_CHAN_SETADDR:
		{
			unsigned int caddr;
			/* get channel address from user space */
			if (copy_from_user(&caddr, argp, sizeof(caddr))) {
				pr_err("mxc_mlb150: copy from user failed\n");
				return -EFAULT;
			}
			pdevinfo->channels[TX_CHANNEL].address =
							(caddr >> 16) & 0xFFFF;
			pdevinfo->channels[RX_CHANNEL].address = caddr & 0xFFFF;
			pr_debug("mxc_mlb150: set ch addr, tx: %d, rx: %d\n",
					pdevinfo->channels[TX_CHANNEL].address,
					pdevinfo->channels[RX_CHANNEL].address);
			break;
		}

	case MLB_CHAN_STARTUP:
		if (atomic_read(&pdevinfo->on)) {
			pr_debug("mxc_mlb150: channel alreadly startup\n");
			break;
		}
		if (mlb_channel_enable(drvdata, minor, 1))
			return -EFAULT;
		break;
	case MLB_CHAN_SHUTDOWN:
		if (atomic_read(&pdevinfo->on) == 0) {
			pr_debug("mxc_mlb150: channel areadly shutdown\n");
			break;
		}
		mlb150_trans_complete_check(pdevinfo);
		mlb_channel_enable(drvdata, minor, 0);
		break;
	case MLB_CHAN_GETEVENT:
		/* get and clear the ex_event */
		spin_lock_irqsave(&pdevinfo->event_lock, flags);
		event = pdevinfo->ex_event;
		pdevinfo->ex_event = 0;
		spin_unlock_irqrestore(&pdevinfo->event_lock, flags);

		if (event) {
			if (copy_to_user(argp, &event, sizeof(event))) {
				pr_err("mxc_mlb150: copy to user failed\n");
				return -EFAULT;
			}
		} else
			return -EAGAIN;
		break;
	case MLB_SET_ISOC_BLKSIZE_188:
		pdevinfo->isoc_blksz = 188;
		pdevinfo->cdt_buf_dep = pdevinfo->adt_buf_dep =
					pdevinfo->isoc_blksz * CH_ISOC_BLK_NUM;
		break;
	case MLB_SET_ISOC_BLKSIZE_196:
		pdevinfo->isoc_blksz = 196;
		pdevinfo->cdt_buf_dep = pdevinfo->adt_buf_dep =
					pdevinfo->isoc_blksz * CH_ISOC_BLK_NUM;
		break;
	case MLB_SET_SYNC_QUAD:
		{
			u32 quad;

			if (copy_from_user(&quad, argp, sizeof(quad))) {
				pr_err("mxc_mlb150: get quad number "
						"from user failed\n");
				return -EFAULT;
			}
			if (quad <= 0 || quad > 3) {
				pr_err("mxc_mlb150: Invalid Quadlets!"
					"Quadlets in Sync mode can "
					"only be 1, 2, 3\n");
				return -EINVAL;
			}
			pdevinfo->sync_quad = quad;
			/* Each quadlets is 4 bytes */
			pdevinfo->cdt_buf_dep = quad * 4 * 4;
			pdevinfo->adt_buf_dep =
				pdevinfo->cdt_buf_dep * CH_SYNC_ADT_BUF_MULTI;
		}
		break;
	case MLB_SET_FPS:
		{
			u32 fps, c0_val;

			/* get fps from user space */
			if (copy_from_user(&fps, argp, sizeof(fps))) {
				pr_err("mxc_mlb150: copy from user failed\n");
				return -EFAULT;
			}

			if ((fps > 1024) &&
				!(drvdata->quirk_flag & MLB_QUIRK_MLB150)) {
				pr_err("mxc_mlb150: not support fps %d\n", fps);
				return -EINVAL;
			}

			c0_val = __raw_readl(mlb_base + REG_MLBC0);
			c0_val &= ~MLBC0_MLBCLK_MASK;

			/* check fps value */
			switch (fps) {
			case 256:
			case 512:
			case 1024:
				pdevinfo->fps = fps >> 9;
				c0_val &= ~MLBC0_MLBPEN;
				c0_val |= (fps >> 9)
					<< MLBC0_MLBCLK_SHIFT;

				if (1024 == fps) {
					/*
					 * Invert output clock phase
					 * in 1024 fps
					 */
					__raw_writel(0x1,
						mlb_base + REG_MLBPC2);
				}
				break;
			case 2048:
			case 3072:
			case 4096:
				pdevinfo->fps = (fps >> 10) + 1;
				c0_val |= ((fps >> 10) + 1)
					<< MLBC0_MLBCLK_SHIFT;
				break;
			case 6144:
				pdevinfo->fps = fps >> 10;
				c0_val |= ((fps >> 10) + 1)
					<< MLBC0_MLBCLK_SHIFT;
				break;
			case 8192:
				pdevinfo->fps = (fps >> 10) - 1;
				c0_val |= ((fps >> 10) - 1)
						<< MLBC0_MLBCLK_SHIFT;
				break;
			default:
				pr_debug("mxc_mlb150: invalid fps argument: %d\n",
						fps);
				return -EINVAL;
			}

			__raw_writel(c0_val, mlb_base + REG_MLBC0);

			pr_debug("mxc_mlb150: set fps to %d, MLBC0: 0x%08x\n",
				fps,
				(u32)__raw_readl(mlb_base + REG_MLBC0));

			break;
		}

	case MLB_GET_VER:
		{
			u32 version;

			/* get MLB device module version */
			version = 0x03030003;

			pr_debug("mxc_mlb150: get version: 0x%08x\n",
					version);

			if (copy_to_user(argp, &version, sizeof(version))) {
				pr_err("mxc_mlb150: copy to user failed\n");
				return -EFAULT;
			}
			break;
		}

	case MLB_SET_DEVADDR:
		{
			u32 c1_val;
			u8 devaddr;

			/* get MLB device address from user space */
			if (copy_from_user
				(&devaddr, argp, sizeof(unsigned char))) {
				pr_err("mxc_mlb150: copy from user failed\n");
				return -EFAULT;
			}

			c1_val = __raw_readl(mlb_base + REG_MLBC1);
			c1_val &= ~MLBC1_NDA_MASK;
			c1_val |= devaddr << MLBC1_NDA_SHIFT;
			__raw_writel(c1_val, mlb_base + REG_MLBC1);
			pr_debug("mxc_mlb150: set dev addr, dev addr: %d, "
				"MLBC1: 0x%08x\n", devaddr,
				(u32)__raw_readl(mlb_base + REG_MLBC1));

			break;
		}

	case MLB_IRQ_DISABLE:
		{
			disable_irq(drvdata->irq_mlb);
			break;
		}

	case MLB_IRQ_ENABLE:
		{
			enable_irq(drvdata->irq_mlb);
			break;
		}
	default:
		pr_info("mxc_mlb150: Invalid ioctl command\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * MLB read routine
 * Read the current received data from queued buffer,
 * and free this buffer for hw to fill ingress data.
 */
static ssize_t mxc_mlb150_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *f_pos)
{
	int size;
	struct mlb_data *drvdata = filp->private_data;
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;
	struct mlb_ringbuf *rx_rbuf = &pdevinfo->rx_rbuf;
	int head, tail;
	unsigned long flags;

	read_lock_irqsave(&rx_rbuf->rb_lock, flags);

	head = ACCESS_ONCE(rx_rbuf->head);
	tail = rx_rbuf->tail;

	read_unlock_irqrestore(&rx_rbuf->rb_lock, flags);

	/* check the current rx buffer is available or not */
	if (0 == CIRC_CNT(head, tail, TRANS_RING_NODES)) {

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		do {
			DEFINE_WAIT(__wait);

			for (;;) {
				prepare_to_wait(&pdevinfo->rx_wq,
						&__wait, TASK_INTERRUPTIBLE);

				read_lock_irqsave(&rx_rbuf->rb_lock, flags);
				if (CIRC_CNT(rx_rbuf->head, rx_rbuf->tail,
						TRANS_RING_NODES) > 0) {
					read_unlock_irqrestore(&rx_rbuf->rb_lock,
								flags);
					break;
				}
				read_unlock_irqrestore(&rx_rbuf->rb_lock,
							flags);

				if (!signal_pending(current)) {
					schedule();
					continue;
				}
				return -ERESTARTSYS;
			}
			finish_wait(&pdevinfo->rx_wq, &__wait);
		} while (0);
	}

	/* read index before reading contents at that index */
	smp_read_barrier_depends();

	size = pdevinfo->adt_buf_dep;
	if (size > count) {
		/* the user buffer is too small */
		pr_warning
			("mxc_mlb150: received data size is bigger than "
			"size: %d, count: %d\n", size, count);
		return -EINVAL;
	}

	/* extract one item from the buffer */
	if (copy_to_user(buf, rx_rbuf->virt_bufs[tail], size)) {
		pr_err("mxc_mlb150: copy from user failed\n");
		return -EFAULT;
	}

	/* finish reading descriptor before incrementing tail */
	smp_mb();

	write_lock_irqsave(&rx_rbuf->rb_lock, flags);
	rx_rbuf->tail = (tail + 1) & (TRANS_RING_NODES - 1);
	write_unlock_irqrestore(&rx_rbuf->rb_lock, flags);

	*f_pos = 0;

	return size;
}

/*
 * MLB write routine
 * Copy the user data to tx channel buffer,
 * and prepare the channel current/next buffer ptr.
 */
static ssize_t mxc_mlb150_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *f_pos)
{
	s32 ret = 0;
	struct mlb_channel_info *pchinfo = NULL;
	struct mlb_data *drvdata = filp->private_data;
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;
	struct mlb_ringbuf *tx_rbuf = &pdevinfo->tx_rbuf;
	int head, tail;
	unsigned long flags;

	/*
	 * minor = MINOR(filp->f_dentry->d_inode->i_rdev);
	 */
	pchinfo = &pdevinfo->channels[TX_CHANNEL];

	if (count > pdevinfo->buf_size) {
		/* too many data to write */
		pr_warning("mxc_mlb150: overflow write data\n");
		return -EFBIG;
	}

	*f_pos = 0;

	read_lock_irqsave(&tx_rbuf->rb_lock, flags);

	head = tx_rbuf->head;
	tail = ACCESS_ONCE(tx_rbuf->tail);
	read_unlock_irqrestore(&tx_rbuf->rb_lock, flags);

	if (0 == CIRC_SPACE(head, tail, TRANS_RING_NODES)) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		do {
			DEFINE_WAIT(__wait);

			for (;;) {
				prepare_to_wait(&pdevinfo->tx_wq,
						&__wait, TASK_INTERRUPTIBLE);

				read_lock_irqsave(&tx_rbuf->rb_lock, flags);
				if (CIRC_SPACE(tx_rbuf->head, tx_rbuf->tail,
							TRANS_RING_NODES) > 0) {
					read_unlock_irqrestore(&tx_rbuf->rb_lock,
							flags);
					break;
				}
				read_unlock_irqrestore(&tx_rbuf->rb_lock,
								flags);

				if (!signal_pending(current)) {
					schedule();
					continue;
				}
				return -ERESTARTSYS;
			}
			finish_wait(&pdevinfo->tx_wq, &__wait);
		} while (0);
	}

	if (copy_from_user((void *)tx_rbuf->virt_bufs[head], buf, count)) {
		read_unlock_irqrestore(&tx_rbuf->rb_lock, flags);
		pr_err("mxc_mlb: copy from user failed\n");
		ret = -EFAULT;
		goto out;
	}

	write_lock_irqsave(&tx_rbuf->rb_lock, flags);
	smp_wmb();
	tx_rbuf->head = (head + 1) & (TRANS_RING_NODES - 1);
	write_unlock_irqrestore(&tx_rbuf->rb_lock, flags);

	if (0 == CIRC_CNT(head, tail, TRANS_RING_NODES)) {
		u32 tx_buf_ptr, ahb_ch;
		s32 adt_sts;
		u32 ctype = pdevinfo->channel_type;

		/* read index before reading contents at that index */
		smp_read_barrier_depends();

		tx_buf_ptr = tx_rbuf->phy_addrs[tail];

		ahb_ch = pdevinfo->channels[TX_CHANNEL].cl;
		adt_sts = mlb150_dev_get_adt_sts(ahb_ch);

		/*  Set ADT for TX */
		mlb150_dev_pipo_next(ahb_ch, ctype, adt_sts, tx_buf_ptr);
	}

	ret = count;
out:
	return ret;
}

static unsigned int mxc_mlb150_poll(struct file *filp,
				 struct poll_table_struct *wait)
{
	int minor;
	unsigned int ret = 0;
	struct mlb_data *drvdata = filp->private_data;
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;
	struct mlb_ringbuf *tx_rbuf = &pdevinfo->tx_rbuf;
	struct mlb_ringbuf *rx_rbuf = &pdevinfo->rx_rbuf;
	int head, tail;
	unsigned long flags;


	minor = MINOR(file_inode(filp)->i_rdev);

	poll_wait(filp, &pdevinfo->rx_wq, wait);
	poll_wait(filp, &pdevinfo->tx_wq, wait);

	read_lock_irqsave(&tx_rbuf->rb_lock, flags);
	head = tx_rbuf->head;
	tail = tx_rbuf->tail;
	read_unlock_irqrestore(&tx_rbuf->rb_lock, flags);

	/* check the tx buffer is avaiable or not */
	if (CIRC_SPACE(head, tail, TRANS_RING_NODES) >= 1)
		ret |= POLLOUT | POLLWRNORM;

	read_lock_irqsave(&rx_rbuf->rb_lock, flags);
	head = rx_rbuf->head;
	tail = rx_rbuf->tail;
	read_unlock_irqrestore(&rx_rbuf->rb_lock, flags);

	/* check the rx buffer filled or not */
	if (CIRC_CNT(head, tail, TRANS_RING_NODES) >= 1)
		ret |= POLLIN | POLLRDNORM;


	/* check the exception event */
	if (pdevinfo->ex_event)
		ret |= POLLIN | POLLRDNORM;

	return ret;
}

/*
 * char dev file operations structure
 */
static const struct file_operations mxc_mlb150_fops = {

	.owner = THIS_MODULE,
	.open = mxc_mlb150_open,
	.release = mxc_mlb150_release,
	.unlocked_ioctl = mxc_mlb150_ioctl,
	.poll = mxc_mlb150_poll,
	.read = mxc_mlb150_read,
	.write = mxc_mlb150_write,
};

static struct platform_device_id imx_mlb150_devtype[] = {
	{
		.name = "imx6q-mlb150",
		.driver_data = MLB_QUIRK_MLB150,
	}, {
		.name = "imx6sx-mlb50",
		.driver_data = 0,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, imx_mlb150_devtype);

static const struct of_device_id mlb150_imx_dt_ids[] = {
	{ .compatible = "fsl,imx6q-mlb150",
		.data = &imx_mlb150_devtype[IMX6Q_MLB], },
	{ .compatible = "fsl,imx6sx-mlb50",
		.data = &imx_mlb150_devtype[IMX6SX_MLB], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mlb150_imx_dt_ids);

/*
 * This function is called whenever the MLB device is detected.
 */
static int mxc_mlb150_probe(struct platform_device *pdev)
{
	int ret, mlb_major, i;
	struct mlb_data *drvdata;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct mlb_data),
				GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "can't allocate enough memory\n");
		return -ENOMEM;
	}

	of_id = of_match_device(mlb150_imx_dt_ids, &pdev->dev);
	if (of_id)
		pdev->id_entry = of_id->data;
	else
		return -EINVAL;
	/*
	 * Register MLB lld as four character devices
	 */
	ret = alloc_chrdev_region(&drvdata->firstdev, 0,
			MLB_MINOR_DEVICES, "mxc_mlb150");
	if (ret < 0) {
		dev_err(&pdev->dev, "alloc region error\n");
		goto err_reg;
	}
	mlb_major = MAJOR(drvdata->firstdev);
	dev_dbg(&pdev->dev, "MLB device major: %d\n", mlb_major);

	cdev_init(&drvdata->cdev, &mxc_mlb150_fops);
	drvdata->cdev.owner = THIS_MODULE;

	ret = cdev_add(&drvdata->cdev, drvdata->firstdev, MLB_MINOR_DEVICES);
	if (ret) {
		dev_err(&pdev->dev, "can't add cdev\n");
		goto err_reg;
	}

	/* create class and device for udev information */
	drvdata->class = class_create(THIS_MODULE, "mlb150");
	if (IS_ERR(drvdata->class)) {
		dev_err(&pdev->dev, "failed to create device class\n");
		ret = -ENOMEM;
		goto err_class;
	}

	for (i = 0; i < MLB_MINOR_DEVICES; i++) {
		struct device *class_dev;

		class_dev = device_create(drvdata->class, NULL,
				MKDEV(mlb_major, i),
				NULL, mlb_devinfo[i].dev_name);
		if (IS_ERR(class_dev)) {
			dev_err(&pdev->dev, "failed to create mlb150 %s"
				" class device\n", mlb_devinfo[i].dev_name);
			ret = -ENOMEM;
			goto err_dev;
		}
	}

	drvdata->quirk_flag = pdev->id_entry->driver_data;

	/* ahb0 irq */
	drvdata->irq_ahb0 = platform_get_irq(pdev,  1);
	if (drvdata->irq_ahb0 < 0) {
		dev_err(&pdev->dev, "No ahb0 irq line provided\n");
		goto err_dev;
	}
	dev_dbg(&pdev->dev, "ahb0_irq: %d\n", drvdata->irq_ahb0);
	if (devm_request_irq(&pdev->dev, drvdata->irq_ahb0, mlb_ahb_isr,
				0, "mlb_ahb0", NULL)) {
		dev_err(&pdev->dev, "can't claim irq %d\n", drvdata->irq_ahb0);
		goto err_dev;
	}

	/* ahb1 irq */
	drvdata->irq_ahb1 = platform_get_irq(pdev,  2);
	if (drvdata->irq_ahb1 < 0) {
		dev_err(&pdev->dev, "No ahb1 irq line provided\n");
		goto err_dev;
	}
	dev_dbg(&pdev->dev, "ahb1_irq: %d\n", drvdata->irq_ahb1);
	if (devm_request_irq(&pdev->dev, drvdata->irq_ahb1, mlb_ahb_isr,
				0, "mlb_ahb1", NULL)) {
		dev_err(&pdev->dev, "can't claim irq %d\n", drvdata->irq_ahb1);
		goto err_dev;
	}

	/* mlb irq */
	drvdata->irq_mlb  = platform_get_irq(pdev,  0);
	if (drvdata->irq_mlb < 0) {
		dev_err(&pdev->dev, "No mlb irq line provided\n");
		goto err_dev;
	}
	dev_dbg(&pdev->dev, "mlb_irq: %d\n", drvdata->irq_mlb);
	if (devm_request_irq(&pdev->dev, drvdata->irq_mlb, mlb_isr,
				0, "mlb", NULL)) {
		dev_err(&pdev->dev, "can't claim irq %d\n", drvdata->irq_mlb);
		goto err_dev;
	}

	/* ioremap from phy mlb to kernel space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get device resources\n");
		ret = -ENOENT;
		goto err_dev;
	}
	mlb_base = devm_ioremap_resource(&pdev->dev, res);
	dev_dbg(&pdev->dev, "mapped base address: 0x%08x\n", (u32)mlb_base);
	if (IS_ERR(mlb_base)) {
		dev_err(&pdev->dev,
			"failed to get ioremap base\n");
		ret = PTR_ERR(mlb_base);
		goto err_dev;
	}
	drvdata->membase = mlb_base;

#ifdef CONFIG_REGULATOR
	drvdata->nvcc = devm_regulator_get(&pdev->dev, "reg_nvcc");
	if (!IS_ERR(drvdata->nvcc)) {
		regulator_set_voltage(drvdata->nvcc, 2500000, 2500000);
		dev_err(&pdev->dev, "enalbe regulator\n");
		ret = regulator_enable(drvdata->nvcc);
		if (ret) {
			dev_err(&pdev->dev, "vdd set voltage error\n");
			goto err_dev;
		}
	}
#endif

	/* enable clock */
	drvdata->clk_mlb3p = devm_clk_get(&pdev->dev, "mlb");
	if (IS_ERR(drvdata->clk_mlb3p)) {
		dev_err(&pdev->dev, "unable to get mlb clock\n");
		ret = PTR_ERR(drvdata->clk_mlb3p);
		goto err_dev;
	}

	if (drvdata->quirk_flag & MLB_QUIRK_MLB150) {
		drvdata->clk_mlb6p = devm_clk_get(&pdev->dev, "pll8_mlb");
		if (IS_ERR(drvdata->clk_mlb6p)) {
			dev_err(&pdev->dev, "unable to get mlb pll clock\n");
			ret = PTR_ERR(drvdata->clk_mlb6p);
			goto err_dev;
		}
	}

	drvdata->iram_pool = of_get_named_gen_pool(np, "iram", 0);
	if (!drvdata->iram_pool) {
		dev_err(&pdev->dev, "iram pool not available\n");
		ret = -ENOMEM;
		goto err_dev;
	}

	drvdata->devinfo = NULL;
	mxc_mlb150_irq_enable(drvdata, 0);
	platform_set_drvdata(pdev, drvdata);
	return 0;

err_dev:
	for (--i; i >= 0; i--)
		device_destroy(drvdata->class, MKDEV(mlb_major, i));

	class_destroy(drvdata->class);
err_class:
	cdev_del(&drvdata->cdev);
err_reg:
	unregister_chrdev_region(drvdata->firstdev, MLB_MINOR_DEVICES);

	return ret;
}

static int mxc_mlb150_remove(struct platform_device *pdev)
{
	int i;
	struct mlb_data *drvdata = platform_get_drvdata(pdev);
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;

	if (pdevinfo && atomic_read(&pdevinfo->on)
		&& (pdevinfo->fps >= CLK_2048FS))
		clk_disable_unprepare(drvdata->clk_mlb6p);

	if (pdevinfo && atomic_read(&pdevinfo->opencnt))
		clk_disable_unprepare(drvdata->clk_mlb3p);

	/* disable mlb power */
#ifdef CONFIG_REGULATOR
	if (!IS_ERR(drvdata->nvcc))
		regulator_disable(drvdata->nvcc);
#endif

	/* destroy mlb device class */
	for (i = MLB_MINOR_DEVICES - 1; i >= 0; i--)
		device_destroy(drvdata->class,
				MKDEV(MAJOR(drvdata->firstdev), i));
	class_destroy(drvdata->class);

	cdev_del(&drvdata->cdev);

	/* Unregister the two MLB devices */
	unregister_chrdev_region(drvdata->firstdev, MLB_MINOR_DEVICES);

	return 0;
}

#ifdef CONFIG_PM
static int mxc_mlb150_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mlb_data *drvdata = platform_get_drvdata(pdev);
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;

	if (pdevinfo && atomic_read(&pdevinfo->on)
		&& (pdevinfo->fps >= CLK_2048FS))
		clk_disable_unprepare(drvdata->clk_mlb6p);

	if (pdevinfo && atomic_read(&pdevinfo->opencnt)) {
		mlb150_dev_exit();
		clk_disable_unprepare(drvdata->clk_mlb3p);
	}

	return 0;
}

static int mxc_mlb150_resume(struct platform_device *pdev)
{
	struct mlb_data *drvdata = platform_get_drvdata(pdev);
	struct mlb_dev_info *pdevinfo = drvdata->devinfo;

	if (pdevinfo && atomic_read(&pdevinfo->opencnt)) {
		clk_prepare_enable(drvdata->clk_mlb3p);
		mlb150_dev_init();
	}

	if (pdevinfo && atomic_read(&pdevinfo->on) &&
		(pdevinfo->fps >= CLK_2048FS))
		clk_prepare_enable(drvdata->clk_mlb6p);

	return 0;
}
#else
#define mxc_mlb150_suspend NULL
#define mxc_mlb150_resume NULL
#endif

/*
 * platform driver structure for MLB
 */
static struct platform_driver mxc_mlb150_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = mlb150_imx_dt_ids,
	},
	.probe = mxc_mlb150_probe,
	.remove = mxc_mlb150_remove,
	.suspend = mxc_mlb150_suspend,
	.resume = mxc_mlb150_resume,
	.id_table = imx_mlb150_devtype,
};

static int __init mxc_mlb150_init(void)
{
	return platform_driver_register(&mxc_mlb150_driver);
}

static void __exit mxc_mlb150_exit(void)
{
	platform_driver_unregister(&mxc_mlb150_driver);
}

module_init(mxc_mlb150_init);
module_exit(mxc_mlb150_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MLB150 low level driver");
MODULE_LICENSE("GPL");
