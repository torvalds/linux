/*
 * Tsi721 PCIExpress-to-SRIO bridge definitions
 *
 * Copyright 2011, Integrated Device Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __TSI721_H
#define __TSI721_H

/* Debug output filtering masks */
enum {
	DBG_NONE	= 0,
	DBG_INIT	= BIT(0), /* driver init */
	DBG_EXIT	= BIT(1), /* driver exit */
	DBG_MPORT	= BIT(2), /* mport add/remove */
	DBG_MAINT	= BIT(3), /* maintenance ops messages */
	DBG_DMA		= BIT(4), /* DMA transfer messages */
	DBG_DMAV	= BIT(5), /* verbose DMA transfer messages */
	DBG_IBW		= BIT(6), /* inbound window */
	DBG_EVENT	= BIT(7), /* event handling messages */
	DBG_OBW		= BIT(8), /* outbound window messages */
	DBG_DBELL	= BIT(9), /* doorbell messages */
	DBG_OMSG	= BIT(10), /* doorbell messages */
	DBG_IMSG	= BIT(11), /* doorbell messages */
	DBG_ALL		= ~0,
};

#ifdef DEBUG
extern u32 dbg_level;

#define tsi_debug(level, dev, fmt, arg...)				\
	do {								\
		if (DBG_##level & dbg_level)				\
			dev_dbg(dev, "%s: " fmt "\n", __func__, ##arg);	\
	} while (0)
#else
#define tsi_debug(level, dev, fmt, arg...) \
		no_printk(KERN_DEBUG "%s: " fmt "\n", __func__, ##arg)
#endif

#define tsi_info(dev, fmt, arg...) \
	dev_info(dev, "%s: " fmt "\n", __func__, ##arg)

#define tsi_warn(dev, fmt, arg...) \
	dev_warn(dev, "%s: WARNING " fmt "\n", __func__, ##arg)

#define tsi_err(dev, fmt, arg...) \
	dev_err(dev, "%s: ERROR " fmt "\n", __func__, ##arg)

#define DRV_NAME	"tsi721"

#define DEFAULT_HOPCOUNT	0xff
#define DEFAULT_DESTID		0xff

/* PCI device ID */
#define PCI_DEVICE_ID_TSI721		0x80ab

#define BAR_0	0
#define BAR_1	1
#define BAR_2	2
#define BAR_4	4

#define TSI721_PC2SR_BARS	2
#define TSI721_PC2SR_WINS	8
#define TSI721_PC2SR_ZONES	8
#define TSI721_MAINT_WIN	0 /* Window for outbound maintenance requests */
#define IDB_QUEUE		0 /* Inbound Doorbell Queue to use */
#define IDB_QSIZE		512 /* Inbound Doorbell Queue size */

/* Memory space sizes */
#define TSI721_REG_SPACE_SIZE		(512 * 1024) /* 512K */
#define TSI721_DB_WIN_SIZE		(16 * 1024 * 1024) /* 16MB */

#define  RIO_TT_CODE_8		0x00000000
#define  RIO_TT_CODE_16		0x00000001

#define TSI721_DMA_MAXCH	8
#define TSI721_DMA_MINSTSSZ	32
#define TSI721_DMA_STSBLKSZ	8

#define TSI721_SRIO_MAXCH	8

#define DBELL_SID(buf)		(((u8)buf[2] << 8) | (u8)buf[3])
#define DBELL_TID(buf)		(((u8)buf[4] << 8) | (u8)buf[5])
#define DBELL_INF(buf)		(((u8)buf[0] << 8) | (u8)buf[1])

#define TSI721_RIO_PW_MSG_SIZE	16  /* Tsi721 saves only 16 bytes of PW msg */

/* Register definitions */

/*
 * Registers in PCIe configuration space
 */

#define TSI721_PCIECFG_MSIXTBL	0x0a4
#define TSI721_MSIXTBL_OFFSET	0x2c000
#define TSI721_PCIECFG_MSIXPBA	0x0a8
#define TSI721_MSIXPBA_OFFSET	0x2a000
#define TSI721_PCIECFG_EPCTL	0x400

/*
 * Event Management Registers
 */

#define TSI721_RIO_EM_INT_STAT		0x10910
#define TSI721_RIO_EM_INT_STAT_PW_RX	0x00010000

#define TSI721_RIO_EM_INT_ENABLE	0x10914
#define TSI721_RIO_EM_INT_ENABLE_PW_RX	0x00010000

#define TSI721_RIO_EM_DEV_INT_EN	0x10930
#define TSI721_RIO_EM_DEV_INT_EN_INT	0x00000001

/*
 * Port-Write Block Registers
 */

#define TSI721_RIO_PW_CTL		0x10a04
#define TSI721_RIO_PW_CTL_PW_TIMER	0xf0000000
#define TSI721_RIO_PW_CTL_PWT_DIS	(0 << 28)
#define TSI721_RIO_PW_CTL_PWT_103	(1 << 28)
#define TSI721_RIO_PW_CTL_PWT_205	(1 << 29)
#define TSI721_RIO_PW_CTL_PWT_410	(1 << 30)
#define TSI721_RIO_PW_CTL_PWT_820	(1 << 31)
#define TSI721_RIO_PW_CTL_PWC_MODE	0x01000000
#define TSI721_RIO_PW_CTL_PWC_CONT	0x00000000
#define TSI721_RIO_PW_CTL_PWC_REL	0x01000000

#define TSI721_RIO_PW_RX_STAT		0x10a10
#define TSI721_RIO_PW_RX_STAT_WR_SIZE	0x0000f000
#define TSI_RIO_PW_RX_STAT_WDPTR	0x00000100
#define TSI721_RIO_PW_RX_STAT_PW_SHORT	0x00000008
#define TSI721_RIO_PW_RX_STAT_PW_TRUNC	0x00000004
#define TSI721_RIO_PW_RX_STAT_PW_DISC	0x00000002
#define TSI721_RIO_PW_RX_STAT_PW_VAL	0x00000001

#define TSI721_RIO_PW_RX_CAPT(x)	(0x10a20 + (x)*4)

/*
 * Inbound Doorbells
 */

#define TSI721_IDB_ENTRY_SIZE	64

#define TSI721_IDQ_CTL(x)	(0x20000 + (x) * 0x1000)
#define TSI721_IDQ_SUSPEND	0x00000002
#define TSI721_IDQ_INIT		0x00000001

#define TSI721_IDQ_STS(x)	(0x20004 + (x) * 0x1000)
#define TSI721_IDQ_RUN		0x00200000

#define TSI721_IDQ_MASK(x)	(0x20008 + (x) * 0x1000)
#define TSI721_IDQ_MASK_MASK	0xffff0000
#define TSI721_IDQ_MASK_PATT	0x0000ffff

#define TSI721_IDQ_RP(x)	(0x2000c + (x) * 0x1000)
#define TSI721_IDQ_RP_PTR	0x0007ffff

#define TSI721_IDQ_WP(x)	(0x20010 + (x) * 0x1000)
#define TSI721_IDQ_WP_PTR	0x0007ffff

#define TSI721_IDQ_BASEL(x)	(0x20014 + (x) * 0x1000)
#define TSI721_IDQ_BASEL_ADDR	0xffffffc0
#define TSI721_IDQ_BASEU(x)	(0x20018 + (x) * 0x1000)
#define TSI721_IDQ_SIZE(x)	(0x2001c + (x) * 0x1000)
#define TSI721_IDQ_SIZE_VAL(size)	(__fls(size) - 4)
#define TSI721_IDQ_SIZE_MIN	512
#define TSI721_IDQ_SIZE_MAX	(512 * 1024)

#define TSI721_SR_CHINT(x)	(0x20040 + (x) * 0x1000)
#define TSI721_SR_CHINTE(x)	(0x20044 + (x) * 0x1000)
#define TSI721_SR_CHINTSET(x)	(0x20048 + (x) * 0x1000)
#define TSI721_SR_CHINT_ODBOK	0x00000020
#define TSI721_SR_CHINT_IDBQRCV	0x00000010
#define TSI721_SR_CHINT_SUSP	0x00000008
#define TSI721_SR_CHINT_ODBTO	0x00000004
#define TSI721_SR_CHINT_ODBRTRY	0x00000002
#define TSI721_SR_CHINT_ODBERR	0x00000001
#define TSI721_SR_CHINT_ALL	0x0000003f

#define TSI721_IBWIN_NUM	8

#define TSI721_IBWIN_LB(x)	(0x29000 + (x) * 0x20)
#define TSI721_IBWIN_LB_BA	0xfffff000
#define TSI721_IBWIN_LB_WEN	0x00000001

#define TSI721_IBWIN_UB(x)	(0x29004 + (x) * 0x20)
#define TSI721_IBWIN_SZ(x)	(0x29008 + (x) * 0x20)
#define TSI721_IBWIN_SZ_SIZE	0x00001f00
#define TSI721_IBWIN_SIZE(size)	(__fls(size) - 12)

#define TSI721_IBWIN_TLA(x)	(0x2900c + (x) * 0x20)
#define TSI721_IBWIN_TLA_ADD	0xfffff000
#define TSI721_IBWIN_TUA(x)	(0x29010 + (x) * 0x20)

#define TSI721_SR2PC_GEN_INTE	0x29800
#define TSI721_SR2PC_PWE	0x29804
#define TSI721_SR2PC_GEN_INT	0x29808

#define TSI721_DEV_INTE		0x29840
#define TSI721_DEV_INT		0x29844
#define TSI721_DEV_INTSET	0x29848
#define TSI721_DEV_INT_BDMA_CH	0x00002000
#define TSI721_DEV_INT_BDMA_NCH	0x00001000
#define TSI721_DEV_INT_SMSG_CH	0x00000800
#define TSI721_DEV_INT_SMSG_NCH	0x00000400
#define TSI721_DEV_INT_SR2PC_CH	0x00000200
#define TSI721_DEV_INT_SRIO	0x00000020

#define TSI721_DEV_CHAN_INTE	0x2984c
#define TSI721_DEV_CHAN_INT	0x29850

#define TSI721_INT_SR2PC_CHAN_M	0xff000000
#define TSI721_INT_SR2PC_CHAN(x) (1 << (24 + (x)))
#define TSI721_INT_IMSG_CHAN_M	0x00ff0000
#define TSI721_INT_IMSG_CHAN(x)	(1 << (16 + (x)))
#define TSI721_INT_OMSG_CHAN_M	0x0000ff00
#define TSI721_INT_OMSG_CHAN(x)	(1 << (8 + (x)))
#define TSI721_INT_BDMA_CHAN_M	0x000000ff
#define TSI721_INT_BDMA_CHAN(x)	(1 << (x))

/*
 * PC2SR block registers
 */
#define TSI721_OBWIN_NUM	TSI721_PC2SR_WINS

#define TSI721_OBWINLB(x)	(0x40000 + (x) * 0x20)
#define TSI721_OBWINLB_BA	0xffff8000
#define TSI721_OBWINLB_WEN	0x00000001

#define TSI721_OBWINUB(x)	(0x40004 + (x) * 0x20)

#define TSI721_OBWINSZ(x)	(0x40008 + (x) * 0x20)
#define TSI721_OBWINSZ_SIZE	0x00001f00
#define TSI721_OBWIN_SIZE(size)	(__fls(size) - 15)

#define TSI721_ZONE_SEL		0x41300
#define TSI721_ZONE_SEL_RD_WRB	0x00020000
#define TSI721_ZONE_SEL_GO	0x00010000
#define TSI721_ZONE_SEL_WIN	0x00000038
#define TSI721_ZONE_SEL_ZONE	0x00000007

#define TSI721_LUT_DATA0	0x41304
#define TSI721_LUT_DATA0_ADD	0xfffff000
#define TSI721_LUT_DATA0_RDTYPE	0x00000f00
#define TSI721_LUT_DATA0_NREAD	0x00000100
#define TSI721_LUT_DATA0_MNTRD	0x00000200
#define TSI721_LUT_DATA0_RDCRF	0x00000020
#define TSI721_LUT_DATA0_WRCRF	0x00000010
#define TSI721_LUT_DATA0_WRTYPE	0x0000000f
#define TSI721_LUT_DATA0_NWR	0x00000001
#define TSI721_LUT_DATA0_MNTWR	0x00000002
#define TSI721_LUT_DATA0_NWR_R	0x00000004

#define TSI721_LUT_DATA1	0x41308

#define TSI721_LUT_DATA2	0x4130c
#define TSI721_LUT_DATA2_HC	0xff000000
#define TSI721_LUT_DATA2_ADD65	0x000c0000
#define TSI721_LUT_DATA2_TT	0x00030000
#define TSI721_LUT_DATA2_DSTID	0x0000ffff

#define TSI721_PC2SR_INTE	0x41310

#define TSI721_DEVCTL		0x48004
#define TSI721_DEVCTL_SRBOOT_CMPL	0x00000004

#define TSI721_I2C_INT_ENABLE	0x49120

/*
 * Block DMA Engine Registers
 *   x = 0..7
 */

#define TSI721_DMAC_BASE(x)	(0x51000 + (x) * 0x1000)

#define TSI721_DMAC_DWRCNT	0x000
#define TSI721_DMAC_DRDCNT	0x004

#define TSI721_DMAC_CTL		0x008
#define TSI721_DMAC_CTL_SUSP	0x00000002
#define TSI721_DMAC_CTL_INIT	0x00000001

#define TSI721_DMAC_INT		0x00c
#define TSI721_DMAC_INT_STFULL	0x00000010
#define TSI721_DMAC_INT_DONE	0x00000008
#define TSI721_DMAC_INT_SUSP	0x00000004
#define TSI721_DMAC_INT_ERR	0x00000002
#define TSI721_DMAC_INT_IOFDONE	0x00000001
#define TSI721_DMAC_INT_ALL	0x0000001f

#define TSI721_DMAC_INTSET	0x010

#define TSI721_DMAC_STS		0x014
#define TSI721_DMAC_STS_ABORT	0x00400000
#define TSI721_DMAC_STS_RUN	0x00200000
#define TSI721_DMAC_STS_CS	0x001f0000

#define TSI721_DMAC_INTE	0x018

#define TSI721_DMAC_DPTRL	0x024
#define TSI721_DMAC_DPTRL_MASK	0xffffffe0

#define TSI721_DMAC_DPTRH	0x028

#define TSI721_DMAC_DSBL	0x02c
#define TSI721_DMAC_DSBL_MASK	0xffffffc0

#define TSI721_DMAC_DSBH	0x030

#define TSI721_DMAC_DSSZ	0x034
#define TSI721_DMAC_DSSZ_SIZE_M	0x0000000f
#define TSI721_DMAC_DSSZ_SIZE(size)	(__fls(size) - 4)

#define TSI721_DMAC_DSRP	0x038
#define TSI721_DMAC_DSRP_MASK	0x0007ffff

#define TSI721_DMAC_DSWP	0x03c
#define TSI721_DMAC_DSWP_MASK	0x0007ffff

#define TSI721_BDMA_INTE	0x5f000

/*
 * Messaging definitions
 */
#define TSI721_MSG_BUFFER_SIZE		RIO_MAX_MSG_SIZE
#define TSI721_MSG_MAX_SIZE		RIO_MAX_MSG_SIZE
#define TSI721_IMSG_MAXCH		8
#define TSI721_IMSG_CHNUM		TSI721_IMSG_MAXCH
#define TSI721_IMSGD_MIN_RING_SIZE	32
#define TSI721_IMSGD_RING_SIZE		512

#define TSI721_OMSG_CHNUM		4 /* One channel per MBOX */
#define TSI721_OMSGD_MIN_RING_SIZE	32
#define TSI721_OMSGD_RING_SIZE		512

/*
 * Outbound Messaging Engine Registers
 *   x = 0..7
 */

#define TSI721_OBDMAC_DWRCNT(x)		(0x61000 + (x) * 0x1000)

#define TSI721_OBDMAC_DRDCNT(x)		(0x61004 + (x) * 0x1000)

#define TSI721_OBDMAC_CTL(x)		(0x61008 + (x) * 0x1000)
#define TSI721_OBDMAC_CTL_MASK		0x00000007
#define TSI721_OBDMAC_CTL_RETRY_THR	0x00000004
#define TSI721_OBDMAC_CTL_SUSPEND	0x00000002
#define TSI721_OBDMAC_CTL_INIT		0x00000001

#define TSI721_OBDMAC_INT(x)		(0x6100c + (x) * 0x1000)
#define TSI721_OBDMAC_INTSET(x)		(0x61010 + (x) * 0x1000)
#define TSI721_OBDMAC_INTE(x)		(0x61018 + (x) * 0x1000)
#define TSI721_OBDMAC_INT_MASK		0x0000001F
#define TSI721_OBDMAC_INT_ST_FULL	0x00000010
#define TSI721_OBDMAC_INT_DONE		0x00000008
#define TSI721_OBDMAC_INT_SUSPENDED	0x00000004
#define TSI721_OBDMAC_INT_ERROR		0x00000002
#define TSI721_OBDMAC_INT_IOF_DONE	0x00000001
#define TSI721_OBDMAC_INT_ALL		TSI721_OBDMAC_INT_MASK

#define TSI721_OBDMAC_STS(x)		(0x61014 + (x) * 0x1000)
#define TSI721_OBDMAC_STS_MASK		0x007f0000
#define TSI721_OBDMAC_STS_ABORT		0x00400000
#define TSI721_OBDMAC_STS_RUN		0x00200000
#define TSI721_OBDMAC_STS_CS		0x001f0000

#define TSI721_OBDMAC_PWE(x)		(0x6101c + (x) * 0x1000)
#define TSI721_OBDMAC_PWE_MASK		0x00000002
#define TSI721_OBDMAC_PWE_ERROR_EN	0x00000002

#define TSI721_OBDMAC_DPTRL(x)		(0x61020 + (x) * 0x1000)
#define TSI721_OBDMAC_DPTRL_MASK	0xfffffff0

#define TSI721_OBDMAC_DPTRH(x)		(0x61024 + (x) * 0x1000)
#define TSI721_OBDMAC_DPTRH_MASK	0xffffffff

#define TSI721_OBDMAC_DSBL(x)		(0x61040 + (x) * 0x1000)
#define TSI721_OBDMAC_DSBL_MASK		0xffffffc0

#define TSI721_OBDMAC_DSBH(x)		(0x61044 + (x) * 0x1000)
#define TSI721_OBDMAC_DSBH_MASK		0xffffffff

#define TSI721_OBDMAC_DSSZ(x)		(0x61048 + (x) * 0x1000)
#define TSI721_OBDMAC_DSSZ_MASK		0x0000000f

#define TSI721_OBDMAC_DSRP(x)		(0x6104c + (x) * 0x1000)
#define TSI721_OBDMAC_DSRP_MASK		0x0007ffff

#define TSI721_OBDMAC_DSWP(x)		(0x61050 + (x) * 0x1000)
#define TSI721_OBDMAC_DSWP_MASK		0x0007ffff

#define TSI721_RQRPTO			0x60010
#define TSI721_RQRPTO_MASK		0x00ffffff
#define TSI721_RQRPTO_VAL		400	/* Response TO value */

/*
 * Inbound Messaging Engine Registers
 *   x = 0..7
 */

#define TSI721_IB_DEVID_GLOBAL		0xffff
#define TSI721_IBDMAC_FQBL(x)		(0x61200 + (x) * 0x1000)
#define TSI721_IBDMAC_FQBL_MASK		0xffffffc0

#define TSI721_IBDMAC_FQBH(x)		(0x61204 + (x) * 0x1000)
#define TSI721_IBDMAC_FQBH_MASK		0xffffffff

#define TSI721_IBDMAC_FQSZ_ENTRY_INX	TSI721_IMSGD_RING_SIZE
#define TSI721_IBDMAC_FQSZ(x)		(0x61208 + (x) * 0x1000)
#define TSI721_IBDMAC_FQSZ_MASK		0x0000000f

#define TSI721_IBDMAC_FQRP(x)		(0x6120c + (x) * 0x1000)
#define TSI721_IBDMAC_FQRP_MASK		0x0007ffff

#define TSI721_IBDMAC_FQWP(x)		(0x61210 + (x) * 0x1000)
#define TSI721_IBDMAC_FQWP_MASK		0x0007ffff

#define TSI721_IBDMAC_FQTH(x)		(0x61214 + (x) * 0x1000)
#define TSI721_IBDMAC_FQTH_MASK		0x0007ffff

#define TSI721_IB_DEVID			0x60020
#define TSI721_IB_DEVID_MASK		0x0000ffff

#define TSI721_IBDMAC_CTL(x)		(0x61240 + (x) * 0x1000)
#define TSI721_IBDMAC_CTL_MASK		0x00000003
#define TSI721_IBDMAC_CTL_SUSPEND	0x00000002
#define TSI721_IBDMAC_CTL_INIT		0x00000001

#define TSI721_IBDMAC_STS(x)		(0x61244 + (x) * 0x1000)
#define TSI721_IBDMAC_STS_MASK		0x007f0000
#define TSI721_IBSMAC_STS_ABORT		0x00400000
#define TSI721_IBSMAC_STS_RUN		0x00200000
#define TSI721_IBSMAC_STS_CS		0x001f0000

#define TSI721_IBDMAC_INT(x)		(0x61248 + (x) * 0x1000)
#define TSI721_IBDMAC_INTSET(x)		(0x6124c + (x) * 0x1000)
#define TSI721_IBDMAC_INTE(x)		(0x61250 + (x) * 0x1000)
#define TSI721_IBDMAC_INT_MASK		0x0000100f
#define TSI721_IBDMAC_INT_SRTO		0x00001000
#define TSI721_IBDMAC_INT_SUSPENDED	0x00000008
#define TSI721_IBDMAC_INT_PC_ERROR	0x00000004
#define TSI721_IBDMAC_INT_FQ_LOW	0x00000002
#define TSI721_IBDMAC_INT_DQ_RCV	0x00000001
#define TSI721_IBDMAC_INT_ALL		TSI721_IBDMAC_INT_MASK

#define TSI721_IBDMAC_PWE(x)		(0x61254 + (x) * 0x1000)
#define TSI721_IBDMAC_PWE_MASK		0x00001700
#define TSI721_IBDMAC_PWE_SRTO		0x00001000
#define TSI721_IBDMAC_PWE_ILL_FMT	0x00000400
#define TSI721_IBDMAC_PWE_ILL_DEC	0x00000200
#define TSI721_IBDMAC_PWE_IMP_SP	0x00000100

#define TSI721_IBDMAC_DQBL(x)		(0x61300 + (x) * 0x1000)
#define TSI721_IBDMAC_DQBL_MASK		0xffffffc0
#define TSI721_IBDMAC_DQBL_ADDR		0xffffffc0

#define TSI721_IBDMAC_DQBH(x)		(0x61304 + (x) * 0x1000)
#define TSI721_IBDMAC_DQBH_MASK		0xffffffff

#define TSI721_IBDMAC_DQRP(x)		(0x61308 + (x) * 0x1000)
#define TSI721_IBDMAC_DQRP_MASK		0x0007ffff

#define TSI721_IBDMAC_DQWR(x)		(0x6130c + (x) * 0x1000)
#define TSI721_IBDMAC_DQWR_MASK		0x0007ffff

#define TSI721_IBDMAC_DQSZ(x)		(0x61314 + (x) * 0x1000)
#define TSI721_IBDMAC_DQSZ_MASK		0x0000000f

/*
 * Messaging Engine Interrupts
 */

#define TSI721_SMSG_PWE			0x6a004

#define TSI721_SMSG_INTE		0x6a000
#define TSI721_SMSG_INT			0x6a008
#define TSI721_SMSG_INTSET		0x6a010
#define TSI721_SMSG_INT_MASK		0x0086ffff
#define TSI721_SMSG_INT_UNS_RSP		0x00800000
#define TSI721_SMSG_INT_ECC_NCOR	0x00040000
#define TSI721_SMSG_INT_ECC_COR		0x00020000
#define TSI721_SMSG_INT_ECC_NCOR_CH	0x0000ff00
#define TSI721_SMSG_INT_ECC_COR_CH	0x000000ff

#define TSI721_SMSG_ECC_LOG		0x6a014
#define TSI721_SMSG_ECC_LOG_MASK	0x00070007
#define TSI721_SMSG_ECC_LOG_ECC_NCOR_M	0x00070000
#define TSI721_SMSG_ECC_LOG_ECC_COR_M	0x00000007

#define TSI721_RETRY_GEN_CNT		0x6a100
#define TSI721_RETRY_GEN_CNT_MASK	0xffffffff

#define TSI721_RETRY_RX_CNT		0x6a104
#define TSI721_RETRY_RX_CNT_MASK	0xffffffff

#define TSI721_SMSG_ECC_COR_LOG(x)	(0x6a300 + (x) * 4)
#define TSI721_SMSG_ECC_COR_LOG_MASK	0x000000ff

#define TSI721_SMSG_ECC_NCOR(x)		(0x6a340 + (x) * 4)
#define TSI721_SMSG_ECC_NCOR_MASK	0x000000ff

/*
 * Block DMA Descriptors
 */

struct tsi721_dma_desc {
	__le32 type_id;

#define TSI721_DMAD_DEVID	0x0000ffff
#define TSI721_DMAD_CRF		0x00010000
#define TSI721_DMAD_PRIO	0x00060000
#define TSI721_DMAD_RTYPE	0x00780000
#define TSI721_DMAD_IOF		0x08000000
#define TSI721_DMAD_DTYPE	0xe0000000

	__le32 bcount;

#define TSI721_DMAD_BCOUNT1	0x03ffffff /* if DTYPE == 1 */
#define TSI721_DMAD_BCOUNT2	0x0000000f /* if DTYPE == 2 */
#define TSI721_DMAD_TT		0x0c000000
#define TSI721_DMAD_RADDR0	0xc0000000

	union {
		__le32 raddr_lo;	   /* if DTYPE == (1 || 2) */
		__le32 next_lo;		   /* if DTYPE == 3 */
	};

#define TSI721_DMAD_CFGOFF	0x00ffffff
#define TSI721_DMAD_HOPCNT	0xff000000

	union {
		__le32 raddr_hi;	   /* if DTYPE == (1 || 2) */
		__le32 next_hi;		   /* if DTYPE == 3 */
	};

	union {
		struct {		   /* if DTYPE == 1 */
			__le32 bufptr_lo;
			__le32 bufptr_hi;
			__le32 s_dist;
			__le32 s_size;
		} t1;
		__le32 data[4];		   /* if DTYPE == 2 */
		u32    reserved[4];	   /* if DTYPE == 3 */
	};
} __aligned(32);

/*
 * Inbound Messaging Descriptor
 */
struct tsi721_imsg_desc {
	__le32 type_id;

#define TSI721_IMD_DEVID	0x0000ffff
#define TSI721_IMD_CRF		0x00010000
#define TSI721_IMD_PRIO		0x00060000
#define TSI721_IMD_TT		0x00180000
#define TSI721_IMD_DTYPE	0xe0000000

	__le32 msg_info;

#define TSI721_IMD_BCOUNT	0x00000ff8
#define TSI721_IMD_SSIZE	0x0000f000
#define TSI721_IMD_LETER	0x00030000
#define TSI721_IMD_XMBOX	0x003c0000
#define TSI721_IMD_MBOX		0x00c00000
#define TSI721_IMD_CS		0x78000000
#define TSI721_IMD_HO		0x80000000

	__le32 bufptr_lo;
	__le32 bufptr_hi;
	u32    reserved[12];

} __aligned(64);

/*
 * Outbound Messaging Descriptor
 */
struct tsi721_omsg_desc {
	__le32 type_id;

#define TSI721_OMD_DEVID	0x0000ffff
#define TSI721_OMD_CRF		0x00010000
#define TSI721_OMD_PRIO		0x00060000
#define TSI721_OMD_IOF		0x08000000
#define TSI721_OMD_DTYPE	0xe0000000
#define TSI721_OMD_RSRVD	0x17f80000

	__le32 msg_info;

#define TSI721_OMD_BCOUNT	0x00000ff8
#define TSI721_OMD_SSIZE	0x0000f000
#define TSI721_OMD_LETER	0x00030000
#define TSI721_OMD_XMBOX	0x003c0000
#define TSI721_OMD_MBOX		0x00c00000
#define TSI721_OMD_TT		0x0c000000

	union {
		__le32 bufptr_lo;	/* if DTYPE == 4 */
		__le32 next_lo;		/* if DTYPE == 5 */
	};

	union {
		__le32 bufptr_hi;	/* if DTYPE == 4 */
		__le32 next_hi;		/* if DTYPE == 5 */
	};

} __aligned(16);

struct tsi721_dma_sts {
	__le64	desc_sts[8];
} __aligned(64);

struct tsi721_desc_sts_fifo {
	union {
		__le64	da64;
		struct {
			__le32	lo;
			__le32	hi;
		} da32;
	} stat[8];
} __aligned(64);

/* Descriptor types for BDMA and Messaging blocks */
enum dma_dtype {
	DTYPE1 = 1, /* Data Transfer DMA Descriptor */
	DTYPE2 = 2, /* Immediate Data Transfer DMA Descriptor */
	DTYPE3 = 3, /* Block Pointer DMA Descriptor */
	DTYPE4 = 4, /* Outbound Msg DMA Descriptor */
	DTYPE5 = 5, /* OB Messaging Block Pointer Descriptor */
	DTYPE6 = 6  /* Inbound Messaging Descriptor */
};

enum dma_rtype {
	NREAD = 0,
	LAST_NWRITE_R = 1,
	ALL_NWRITE = 2,
	ALL_NWRITE_R = 3,
	MAINT_RD = 4,
	MAINT_WR = 5
};

/*
 * mport Driver Definitions
 */
#define TSI721_DMA_CHNUM	TSI721_DMA_MAXCH

#define TSI721_DMACH_MAINT	7	/* DMA channel for maint requests */
#define TSI721_DMACH_MAINT_NBD	32	/* Number of BDs for maint requests */

#define TSI721_DMACH_DMA	1	/* DMA channel for data transfers */

#define MSG_DMA_ENTRY_INX_TO_SIZE(x)	((0x10 << (x)) & 0xFFFF0)

enum tsi721_smsg_int_flag {
	SMSG_INT_NONE		= 0x00000000,
	SMSG_INT_ECC_COR_CH	= 0x000000ff,
	SMSG_INT_ECC_NCOR_CH	= 0x0000ff00,
	SMSG_INT_ECC_COR	= 0x00020000,
	SMSG_INT_ECC_NCOR	= 0x00040000,
	SMSG_INT_UNS_RSP	= 0x00800000,
	SMSG_INT_ALL		= 0x0006ffff
};

/* Structures */

#ifdef CONFIG_RAPIDIO_DMA_ENGINE

#define TSI721_BDMA_MAX_BCOUNT	(TSI721_DMAD_BCOUNT1 + 1)

struct tsi721_tx_desc {
	struct dma_async_tx_descriptor	txd;
	u16				destid;
	/* low 64-bits of 66-bit RIO address */
	u64				rio_addr;
	/* upper 2-bits of 66-bit RIO address */
	u8				rio_addr_u;
	enum dma_rtype			rtype;
	struct list_head		desc_node;
	struct scatterlist		*sg;
	unsigned int			sg_len;
	enum dma_status			status;
};

struct tsi721_bdma_chan {
	int		id;
	void __iomem	*regs;
	int		bd_num;		/* number of HW buffer descriptors */
	void		*bd_base;	/* start of DMA descriptors */
	dma_addr_t	bd_phys;
	void		*sts_base;	/* start of DMA BD status FIFO */
	dma_addr_t	sts_phys;
	int		sts_size;
	u32		sts_rdptr;
	u32		wr_count;
	u32		wr_count_next;

	struct dma_chan		dchan;
	struct tsi721_tx_desc	*tx_desc;
	spinlock_t		lock;
	struct tsi721_tx_desc	*active_tx;
	struct list_head	queue;
	struct list_head	free_list;
	struct tasklet_struct	tasklet;
	bool			active;
};

#endif /* CONFIG_RAPIDIO_DMA_ENGINE */

struct tsi721_bdma_maint {
	int		ch_id;		/* BDMA channel number */
	int		bd_num;		/* number of buffer descriptors */
	void		*bd_base;	/* start of DMA descriptors */
	dma_addr_t	bd_phys;
	void		*sts_base;	/* start of DMA BD status FIFO */
	dma_addr_t	sts_phys;
	int		sts_size;
};

struct tsi721_imsg_ring {
	u32		size;
	/* VA/PA of data buffers for incoming messages */
	void		*buf_base;
	dma_addr_t	buf_phys;
	/* VA/PA of circular free buffer list */
	void		*imfq_base;
	dma_addr_t	imfq_phys;
	/* VA/PA of Inbound message descriptors */
	void		*imd_base;
	dma_addr_t	imd_phys;
	 /* Inbound Queue buffer pointers */
	void		*imq_base[TSI721_IMSGD_RING_SIZE];

	u32		rx_slot;
	void		*dev_id;
	u32		fq_wrptr;
	u32		desc_rdptr;
	spinlock_t	lock;
};

struct tsi721_omsg_ring {
	u32		size;
	/* VA/PA of OB Msg descriptors */
	void		*omd_base;
	dma_addr_t	omd_phys;
	/* VA/PA of OB Msg data buffers */
	void		*omq_base[TSI721_OMSGD_RING_SIZE];
	dma_addr_t	omq_phys[TSI721_OMSGD_RING_SIZE];
	/* VA/PA of OB Msg descriptor status FIFO */
	void		*sts_base;
	dma_addr_t	sts_phys;
	u32		sts_size; /* # of allocated status entries */
	u32		sts_rdptr;

	u32		tx_slot;
	void		*dev_id;
	u32		wr_count;
	spinlock_t	lock;
};

enum tsi721_flags {
	TSI721_USING_MSI	= (1 << 0),
	TSI721_USING_MSIX	= (1 << 1),
	TSI721_IMSGID_SET	= (1 << 2),
};

#ifdef CONFIG_PCI_MSI
/*
 * MSI-X Table Entries (0 ... 69)
 */
#define TSI721_MSIX_DMACH_DONE(x)	(0 + (x))
#define TSI721_MSIX_DMACH_INT(x)	(8 + (x))
#define TSI721_MSIX_BDMA_INT		16
#define TSI721_MSIX_OMSG_DONE(x)	(17 + (x))
#define TSI721_MSIX_OMSG_INT(x)		(25 + (x))
#define TSI721_MSIX_IMSG_DQ_RCV(x)	(33 + (x))
#define TSI721_MSIX_IMSG_INT(x)		(41 + (x))
#define TSI721_MSIX_MSG_INT		49
#define TSI721_MSIX_SR2PC_IDBQ_RCV(x)	(50 + (x))
#define TSI721_MSIX_SR2PC_CH_INT(x)	(58 + (x))
#define TSI721_MSIX_SR2PC_INT		66
#define TSI721_MSIX_PC2SR_INT		67
#define TSI721_MSIX_SRIO_MAC_INT	68
#define TSI721_MSIX_I2C_INT		69

/* MSI-X vector and init table entry indexes */
enum tsi721_msix_vect {
	TSI721_VECT_IDB,
	TSI721_VECT_PWRX, /* PW_RX is part of SRIO MAC Interrupt reporting */
	TSI721_VECT_OMB0_DONE,
	TSI721_VECT_OMB1_DONE,
	TSI721_VECT_OMB2_DONE,
	TSI721_VECT_OMB3_DONE,
	TSI721_VECT_OMB0_INT,
	TSI721_VECT_OMB1_INT,
	TSI721_VECT_OMB2_INT,
	TSI721_VECT_OMB3_INT,
	TSI721_VECT_IMB0_RCV,
	TSI721_VECT_IMB1_RCV,
	TSI721_VECT_IMB2_RCV,
	TSI721_VECT_IMB3_RCV,
	TSI721_VECT_IMB0_INT,
	TSI721_VECT_IMB1_INT,
	TSI721_VECT_IMB2_INT,
	TSI721_VECT_IMB3_INT,
#ifdef CONFIG_RAPIDIO_DMA_ENGINE
	TSI721_VECT_DMA0_DONE,
	TSI721_VECT_DMA1_DONE,
	TSI721_VECT_DMA2_DONE,
	TSI721_VECT_DMA3_DONE,
	TSI721_VECT_DMA4_DONE,
	TSI721_VECT_DMA5_DONE,
	TSI721_VECT_DMA6_DONE,
	TSI721_VECT_DMA7_DONE,
	TSI721_VECT_DMA0_INT,
	TSI721_VECT_DMA1_INT,
	TSI721_VECT_DMA2_INT,
	TSI721_VECT_DMA3_INT,
	TSI721_VECT_DMA4_INT,
	TSI721_VECT_DMA5_INT,
	TSI721_VECT_DMA6_INT,
	TSI721_VECT_DMA7_INT,
#endif /* CONFIG_RAPIDIO_DMA_ENGINE */
	TSI721_VECT_MAX
};

#define IRQ_DEVICE_NAME_MAX	64

struct msix_irq {
	u16	vector;
	char	irq_name[IRQ_DEVICE_NAME_MAX];
};
#endif /* CONFIG_PCI_MSI */

struct tsi721_ib_win_mapping {
	struct list_head node;
	dma_addr_t	lstart;
};

struct tsi721_ib_win {
	u64		rstart;
	u32		size;
	dma_addr_t	lstart;
	bool		active;
	bool		xlat;
	struct list_head mappings;
};

struct tsi721_obw_bar {
	u64		base;
	u64		size;
	u64		free;
};

struct tsi721_ob_win {
	u64		base;
	u32		size;
	u16		destid;
	u64		rstart;
	bool		active;
	struct tsi721_obw_bar *pbar;
};

struct tsi721_device {
	struct pci_dev	*pdev;
	struct rio_mport mport;
	u32		flags;
	void __iomem	*regs;
#ifdef CONFIG_PCI_MSI
	struct msix_irq	msix[TSI721_VECT_MAX];
#endif
	/* Doorbells */
	void __iomem	*odb_base;
	void		*idb_base;
	dma_addr_t	idb_dma;
	struct work_struct idb_work;
	u32		db_discard_count;

	/* Inbound Port-Write */
	struct work_struct pw_work;
	struct kfifo	pw_fifo;
	spinlock_t	pw_fifo_lock;
	u32		pw_discard_count;

	/* BDMA Engine */
	struct tsi721_bdma_maint mdma; /* Maintenance rd/wr request channel */

#ifdef CONFIG_RAPIDIO_DMA_ENGINE
	struct tsi721_bdma_chan bdma[TSI721_DMA_CHNUM];
#endif

	/* Inbound Messaging */
	int		imsg_init[TSI721_IMSG_CHNUM];
	struct tsi721_imsg_ring imsg_ring[TSI721_IMSG_CHNUM];

	/* Outbound Messaging */
	int		omsg_init[TSI721_OMSG_CHNUM];
	struct tsi721_omsg_ring	omsg_ring[TSI721_OMSG_CHNUM];

	/* Inbound Mapping Windows */
	struct tsi721_ib_win ib_win[TSI721_IBWIN_NUM];
	int		ibwin_cnt;

	/* Outbound Mapping Windows */
	struct tsi721_obw_bar p2r_bar[2];
	struct tsi721_ob_win  ob_win[TSI721_OBWIN_NUM];
	int		obwin_cnt;
};

#ifdef CONFIG_RAPIDIO_DMA_ENGINE
extern void tsi721_bdma_handler(struct tsi721_bdma_chan *bdma_chan);
extern int tsi721_register_dma(struct tsi721_device *priv);
extern void tsi721_unregister_dma(struct tsi721_device *priv);
extern void tsi721_dma_stop_all(struct tsi721_device *priv);
#else
#define tsi721_dma_stop_all(priv) do {} while (0)
#define tsi721_unregister_dma(priv) do {} while (0)
#endif

#endif
