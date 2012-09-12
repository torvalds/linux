/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2010 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/ratelimit.h>
#include "ql4_def.h"
#include "ql4_glbl.h"

#include <asm-generic/io-64-nonatomic-lo-hi.h>

#define MASK(n)		DMA_BIT_MASK(n)
#define MN_WIN(addr)	(((addr & 0x1fc0000) >> 1) | ((addr >> 25) & 0x3ff))
#define OCM_WIN(addr)	(((addr & 0x1ff0000) >> 1) | ((addr >> 25) & 0x3ff))
#define MS_WIN(addr)	(addr & 0x0ffc0000)
#define QLA82XX_PCI_MN_2M	(0)
#define QLA82XX_PCI_MS_2M	(0x80000)
#define QLA82XX_PCI_OCM0_2M	(0xc0000)
#define VALID_OCM_ADDR(addr)	(((addr) & 0x3f800) != 0x3f800)
#define GET_MEM_OFFS_2M(addr)	(addr & MASK(18))

/* CRB window related */
#define CRB_BLK(off)	((off >> 20) & 0x3f)
#define CRB_SUBBLK(off)	((off >> 16) & 0xf)
#define CRB_WINDOW_2M	(0x130060)
#define CRB_HI(off)	((qla4_8xxx_crb_hub_agt[CRB_BLK(off)] << 20) | \
			((off) & 0xf0000))
#define QLA82XX_PCI_CAMQM_2M_END	(0x04800800UL)
#define QLA82XX_PCI_CAMQM_2M_BASE	(0x000ff800UL)
#define CRB_INDIRECT_2M			(0x1e0000UL)

static inline void __iomem *
qla4_8xxx_pci_base_offsetfset(struct scsi_qla_host *ha, unsigned long off)
{
	if ((off < ha->first_page_group_end) &&
	    (off >= ha->first_page_group_start))
		return (void __iomem *)(ha->nx_pcibase + off);

	return NULL;
}

#define MAX_CRB_XFORM 60
static unsigned long crb_addr_xform[MAX_CRB_XFORM];
static int qla4_8xxx_crb_table_initialized;

#define qla4_8xxx_crb_addr_transform(name) \
	(crb_addr_xform[QLA82XX_HW_PX_MAP_CRB_##name] = \
	 QLA82XX_HW_CRB_HUB_AGT_ADR_##name << 20)
static void
qla4_8xxx_crb_addr_transform_setup(void)
{
	qla4_8xxx_crb_addr_transform(XDMA);
	qla4_8xxx_crb_addr_transform(TIMR);
	qla4_8xxx_crb_addr_transform(SRE);
	qla4_8xxx_crb_addr_transform(SQN3);
	qla4_8xxx_crb_addr_transform(SQN2);
	qla4_8xxx_crb_addr_transform(SQN1);
	qla4_8xxx_crb_addr_transform(SQN0);
	qla4_8xxx_crb_addr_transform(SQS3);
	qla4_8xxx_crb_addr_transform(SQS2);
	qla4_8xxx_crb_addr_transform(SQS1);
	qla4_8xxx_crb_addr_transform(SQS0);
	qla4_8xxx_crb_addr_transform(RPMX7);
	qla4_8xxx_crb_addr_transform(RPMX6);
	qla4_8xxx_crb_addr_transform(RPMX5);
	qla4_8xxx_crb_addr_transform(RPMX4);
	qla4_8xxx_crb_addr_transform(RPMX3);
	qla4_8xxx_crb_addr_transform(RPMX2);
	qla4_8xxx_crb_addr_transform(RPMX1);
	qla4_8xxx_crb_addr_transform(RPMX0);
	qla4_8xxx_crb_addr_transform(ROMUSB);
	qla4_8xxx_crb_addr_transform(SN);
	qla4_8xxx_crb_addr_transform(QMN);
	qla4_8xxx_crb_addr_transform(QMS);
	qla4_8xxx_crb_addr_transform(PGNI);
	qla4_8xxx_crb_addr_transform(PGND);
	qla4_8xxx_crb_addr_transform(PGN3);
	qla4_8xxx_crb_addr_transform(PGN2);
	qla4_8xxx_crb_addr_transform(PGN1);
	qla4_8xxx_crb_addr_transform(PGN0);
	qla4_8xxx_crb_addr_transform(PGSI);
	qla4_8xxx_crb_addr_transform(PGSD);
	qla4_8xxx_crb_addr_transform(PGS3);
	qla4_8xxx_crb_addr_transform(PGS2);
	qla4_8xxx_crb_addr_transform(PGS1);
	qla4_8xxx_crb_addr_transform(PGS0);
	qla4_8xxx_crb_addr_transform(PS);
	qla4_8xxx_crb_addr_transform(PH);
	qla4_8xxx_crb_addr_transform(NIU);
	qla4_8xxx_crb_addr_transform(I2Q);
	qla4_8xxx_crb_addr_transform(EG);
	qla4_8xxx_crb_addr_transform(MN);
	qla4_8xxx_crb_addr_transform(MS);
	qla4_8xxx_crb_addr_transform(CAS2);
	qla4_8xxx_crb_addr_transform(CAS1);
	qla4_8xxx_crb_addr_transform(CAS0);
	qla4_8xxx_crb_addr_transform(CAM);
	qla4_8xxx_crb_addr_transform(C2C1);
	qla4_8xxx_crb_addr_transform(C2C0);
	qla4_8xxx_crb_addr_transform(SMB);
	qla4_8xxx_crb_addr_transform(OCM0);
	qla4_8xxx_crb_addr_transform(I2C0);

	qla4_8xxx_crb_table_initialized = 1;
}

static struct crb_128M_2M_block_map crb_128M_2M_map[64] = {
	{{{0, 0,         0,         0} } },		/* 0: PCI */
	{{{1, 0x0100000, 0x0102000, 0x120000},	/* 1: PCIE */
		{1, 0x0110000, 0x0120000, 0x130000},
		{1, 0x0120000, 0x0122000, 0x124000},
		{1, 0x0130000, 0x0132000, 0x126000},
		{1, 0x0140000, 0x0142000, 0x128000},
		{1, 0x0150000, 0x0152000, 0x12a000},
		{1, 0x0160000, 0x0170000, 0x110000},
		{1, 0x0170000, 0x0172000, 0x12e000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{1, 0x01e0000, 0x01e0800, 0x122000},
		{0, 0x0000000, 0x0000000, 0x000000} } },
	{{{1, 0x0200000, 0x0210000, 0x180000} } },/* 2: MN */
	{{{0, 0,         0,         0} } },	    /* 3: */
	{{{1, 0x0400000, 0x0401000, 0x169000} } },/* 4: P2NR1 */
	{{{1, 0x0500000, 0x0510000, 0x140000} } },/* 5: SRE   */
	{{{1, 0x0600000, 0x0610000, 0x1c0000} } },/* 6: NIU   */
	{{{1, 0x0700000, 0x0704000, 0x1b8000} } },/* 7: QM    */
	{{{1, 0x0800000, 0x0802000, 0x170000},  /* 8: SQM0  */
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{1, 0x08f0000, 0x08f2000, 0x172000} } },
	{{{1, 0x0900000, 0x0902000, 0x174000},	/* 9: SQM1*/
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{1, 0x09f0000, 0x09f2000, 0x176000} } },
	{{{0, 0x0a00000, 0x0a02000, 0x178000},	/* 10: SQM2*/
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{1, 0x0af0000, 0x0af2000, 0x17a000} } },
	{{{0, 0x0b00000, 0x0b02000, 0x17c000},	/* 11: SQM3*/
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{1, 0x0bf0000, 0x0bf2000, 0x17e000} } },
	{{{1, 0x0c00000, 0x0c04000, 0x1d4000} } },/* 12: I2Q */
	{{{1, 0x0d00000, 0x0d04000, 0x1a4000} } },/* 13: TMR */
	{{{1, 0x0e00000, 0x0e04000, 0x1a0000} } },/* 14: ROMUSB */
	{{{1, 0x0f00000, 0x0f01000, 0x164000} } },/* 15: PEG4 */
	{{{0, 0x1000000, 0x1004000, 0x1a8000} } },/* 16: XDMA */
	{{{1, 0x1100000, 0x1101000, 0x160000} } },/* 17: PEG0 */
	{{{1, 0x1200000, 0x1201000, 0x161000} } },/* 18: PEG1 */
	{{{1, 0x1300000, 0x1301000, 0x162000} } },/* 19: PEG2 */
	{{{1, 0x1400000, 0x1401000, 0x163000} } },/* 20: PEG3 */
	{{{1, 0x1500000, 0x1501000, 0x165000} } },/* 21: P2ND */
	{{{1, 0x1600000, 0x1601000, 0x166000} } },/* 22: P2NI */
	{{{0, 0,         0,         0} } },	/* 23: */
	{{{0, 0,         0,         0} } },	/* 24: */
	{{{0, 0,         0,         0} } },	/* 25: */
	{{{0, 0,         0,         0} } },	/* 26: */
	{{{0, 0,         0,         0} } },	/* 27: */
	{{{0, 0,         0,         0} } },	/* 28: */
	{{{1, 0x1d00000, 0x1d10000, 0x190000} } },/* 29: MS */
	{{{1, 0x1e00000, 0x1e01000, 0x16a000} } },/* 30: P2NR2 */
	{{{1, 0x1f00000, 0x1f10000, 0x150000} } },/* 31: EPG */
	{{{0} } },				/* 32: PCI */
	{{{1, 0x2100000, 0x2102000, 0x120000},	/* 33: PCIE */
		{1, 0x2110000, 0x2120000, 0x130000},
		{1, 0x2120000, 0x2122000, 0x124000},
		{1, 0x2130000, 0x2132000, 0x126000},
		{1, 0x2140000, 0x2142000, 0x128000},
		{1, 0x2150000, 0x2152000, 0x12a000},
		{1, 0x2160000, 0x2170000, 0x110000},
		{1, 0x2170000, 0x2172000, 0x12e000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000},
		{0, 0x0000000, 0x0000000, 0x000000} } },
	{{{1, 0x2200000, 0x2204000, 0x1b0000} } },/* 34: CAM */
	{{{0} } },				/* 35: */
	{{{0} } },				/* 36: */
	{{{0} } },				/* 37: */
	{{{0} } },				/* 38: */
	{{{0} } },				/* 39: */
	{{{1, 0x2800000, 0x2804000, 0x1a4000} } },/* 40: TMR */
	{{{1, 0x2900000, 0x2901000, 0x16b000} } },/* 41: P2NR3 */
	{{{1, 0x2a00000, 0x2a00400, 0x1ac400} } },/* 42: RPMX1 */
	{{{1, 0x2b00000, 0x2b00400, 0x1ac800} } },/* 43: RPMX2 */
	{{{1, 0x2c00000, 0x2c00400, 0x1acc00} } },/* 44: RPMX3 */
	{{{1, 0x2d00000, 0x2d00400, 0x1ad000} } },/* 45: RPMX4 */
	{{{1, 0x2e00000, 0x2e00400, 0x1ad400} } },/* 46: RPMX5 */
	{{{1, 0x2f00000, 0x2f00400, 0x1ad800} } },/* 47: RPMX6 */
	{{{1, 0x3000000, 0x3000400, 0x1adc00} } },/* 48: RPMX7 */
	{{{0, 0x3100000, 0x3104000, 0x1a8000} } },/* 49: XDMA */
	{{{1, 0x3200000, 0x3204000, 0x1d4000} } },/* 50: I2Q */
	{{{1, 0x3300000, 0x3304000, 0x1a0000} } },/* 51: ROMUSB */
	{{{0} } },				/* 52: */
	{{{1, 0x3500000, 0x3500400, 0x1ac000} } },/* 53: RPMX0 */
	{{{1, 0x3600000, 0x3600400, 0x1ae000} } },/* 54: RPMX8 */
	{{{1, 0x3700000, 0x3700400, 0x1ae400} } },/* 55: RPMX9 */
	{{{1, 0x3800000, 0x3804000, 0x1d0000} } },/* 56: OCM0 */
	{{{1, 0x3900000, 0x3904000, 0x1b4000} } },/* 57: CRYPTO */
	{{{1, 0x3a00000, 0x3a04000, 0x1d8000} } },/* 58: SMB */
	{{{0} } },				/* 59: I2C0 */
	{{{0} } },				/* 60: I2C1 */
	{{{1, 0x3d00000, 0x3d04000, 0x1dc000} } },/* 61: LPC */
	{{{1, 0x3e00000, 0x3e01000, 0x167000} } },/* 62: P2NC */
	{{{1, 0x3f00000, 0x3f01000, 0x168000} } }	/* 63: P2NR0 */
};

/*
 * top 12 bits of crb internal address (hub, agent)
 */
static unsigned qla4_8xxx_crb_hub_agt[64] = {
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PS,
	QLA82XX_HW_CRB_HUB_AGT_ADR_MN,
	QLA82XX_HW_CRB_HUB_AGT_ADR_MS,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SRE,
	QLA82XX_HW_CRB_HUB_AGT_ADR_NIU,
	QLA82XX_HW_CRB_HUB_AGT_ADR_QMN,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN3,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2Q,
	QLA82XX_HW_CRB_HUB_AGT_ADR_TIMR,
	QLA82XX_HW_CRB_HUB_AGT_ADR_ROMUSB,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN4,
	QLA82XX_HW_CRB_HUB_AGT_ADR_XDMA,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN3,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGND,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGNI,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS3,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGSI,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SN,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_EG,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PS,
	QLA82XX_HW_CRB_HUB_AGT_ADR_CAM,
	0,
	0,
	0,
	0,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_TIMR,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX3,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX4,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX5,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX6,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX7,
	QLA82XX_HW_CRB_HUB_AGT_ADR_XDMA,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2Q,
	QLA82XX_HW_CRB_HUB_AGT_ADR_ROMUSB,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX8,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX9,
	QLA82XX_HW_CRB_HUB_AGT_ADR_OCM0,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SMB,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2C0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2C1,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGNC,
	0,
};

/* Device states */
static char *qdev_state[] = {
	"Unknown",
	"Cold",
	"Initializing",
	"Ready",
	"Need Reset",
	"Need Quiescent",
	"Failed",
	"Quiescent",
};

/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static void
qla4_8xxx_pci_set_crbwindow_2M(struct scsi_qla_host *ha, ulong *off)
{
	u32 win_read;

	ha->crb_win = CRB_HI(*off);
	writel(ha->crb_win,
		(void __iomem *)(CRB_WINDOW_2M + ha->nx_pcibase));

	/* Read back value to make sure write has gone through before trying
	* to use it. */
	win_read = readl((void __iomem *)(CRB_WINDOW_2M + ha->nx_pcibase));
	if (win_read != ha->crb_win) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "%s: Written crbwin (0x%x) != Read crbwin (0x%x),"
		    " off=0x%lx\n", __func__, ha->crb_win, win_read, *off));
	}
	*off = (*off & MASK(16)) + CRB_INDIRECT_2M + ha->nx_pcibase;
}

void
qla4_8xxx_wr_32(struct scsi_qla_host *ha, ulong off, u32 data)
{
	unsigned long flags = 0;
	int rv;

	rv = qla4_8xxx_pci_get_crb_addr_2M(ha, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
		write_lock_irqsave(&ha->hw_lock, flags);
		qla4_8xxx_crb_win_lock(ha);
		qla4_8xxx_pci_set_crbwindow_2M(ha, &off);
	}

	writel(data, (void __iomem *)off);

	if (rv == 1) {
		qla4_8xxx_crb_win_unlock(ha);
		write_unlock_irqrestore(&ha->hw_lock, flags);
	}
}

int
qla4_8xxx_rd_32(struct scsi_qla_host *ha, ulong off)
{
	unsigned long flags = 0;
	int rv;
	u32 data;

	rv = qla4_8xxx_pci_get_crb_addr_2M(ha, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
		write_lock_irqsave(&ha->hw_lock, flags);
		qla4_8xxx_crb_win_lock(ha);
		qla4_8xxx_pci_set_crbwindow_2M(ha, &off);
	}
	data = readl((void __iomem *)off);

	if (rv == 1) {
		qla4_8xxx_crb_win_unlock(ha);
		write_unlock_irqrestore(&ha->hw_lock, flags);
	}
	return data;
}

/* Minidump related functions */
static int qla4_8xxx_md_rw_32(struct scsi_qla_host *ha, uint32_t off,
			      u32 data, uint8_t flag)
{
	uint32_t win_read, off_value, rval = QLA_SUCCESS;

	off_value  = off & 0xFFFF0000;
	writel(off_value, (void __iomem *)(CRB_WINDOW_2M + ha->nx_pcibase));

	/* Read back value to make sure write has gone through before trying
	 * to use it.
	 */
	win_read = readl((void __iomem *)(CRB_WINDOW_2M + ha->nx_pcibase));
	if (win_read != off_value) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: Written (0x%x) != Read (0x%x), off=0x%x\n",
				   __func__, off_value, win_read, off));
		return QLA_ERROR;
	}

	off_value  = off & 0x0000FFFF;

	if (flag)
		writel(data, (void __iomem *)(off_value + CRB_INDIRECT_2M +
					      ha->nx_pcibase));
	else
		rval = readl((void __iomem *)(off_value + CRB_INDIRECT_2M +
					      ha->nx_pcibase));

	return rval;
}

#define CRB_WIN_LOCK_TIMEOUT 100000000

int qla4_8xxx_crb_win_lock(struct scsi_qla_host *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore3 from PCI HW block */
		done = qla4_8xxx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_LOCK));
		if (done == 1)
			break;
		if (timeout >= CRB_WIN_LOCK_TIMEOUT)
			return -1;

		timeout++;

		/* Yield CPU */
		if (!in_interrupt())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();    /*This a nop instr on i386*/
		}
	}
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_WIN_LOCK_ID, ha->func_num);
	return 0;
}

void qla4_8xxx_crb_win_unlock(struct scsi_qla_host *ha)
{
	qla4_8xxx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_UNLOCK));
}

#define IDC_LOCK_TIMEOUT 100000000

/**
 * qla4_8xxx_idc_lock - hw_lock
 * @ha: pointer to adapter structure
 *
 * General purpose lock used to synchronize access to
 * CRB_DEV_STATE, CRB_DEV_REF_COUNT, etc.
 **/
int qla4_8xxx_idc_lock(struct scsi_qla_host *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore5 from PCI HW block */
		done = qla4_8xxx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_LOCK));
		if (done == 1)
			break;
		if (timeout >= IDC_LOCK_TIMEOUT)
			return -1;

		timeout++;

		/* Yield CPU */
		if (!in_interrupt())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();    /*This a nop instr on i386*/
		}
	}
	return 0;
}

void qla4_8xxx_idc_unlock(struct scsi_qla_host *ha)
{
	qla4_8xxx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_UNLOCK));
}

int
qla4_8xxx_pci_get_crb_addr_2M(struct scsi_qla_host *ha, ulong *off)
{
	struct crb_128M_2M_sub_block_map *m;

	if (*off >= QLA82XX_CRB_MAX)
		return -1;

	if (*off >= QLA82XX_PCI_CAMQM && (*off < QLA82XX_PCI_CAMQM_2M_END)) {
		*off = (*off - QLA82XX_PCI_CAMQM) +
		    QLA82XX_PCI_CAMQM_2M_BASE + ha->nx_pcibase;
		return 0;
	}

	if (*off < QLA82XX_PCI_CRBSPACE)
		return -1;

	*off -= QLA82XX_PCI_CRBSPACE;
	/*
	 * Try direct map
	 */

	m = &crb_128M_2M_map[CRB_BLK(*off)].sub_block[CRB_SUBBLK(*off)];

	if (m->valid && (m->start_128M <= *off) && (m->end_128M > *off)) {
		*off = *off + m->start_2M - m->start_128M + ha->nx_pcibase;
		return 0;
	}

	/*
	 * Not in direct map, use crb window
	 */
	return 1;
}

/*  PCI Windowing for DDR regions.  */
#define QLA82XX_ADDR_IN_RANGE(addr, low, high)            \
	(((addr) <= (high)) && ((addr) >= (low)))

/*
* check memory access boundary.
* used by test agent. support ddr access only for now
*/
static unsigned long
qla4_8xxx_pci_mem_bound_check(struct scsi_qla_host *ha,
		unsigned long long addr, int size)
{
	if (!QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX) ||
	    !QLA82XX_ADDR_IN_RANGE(addr + size - 1,
	    QLA82XX_ADDR_DDR_NET, QLA82XX_ADDR_DDR_NET_MAX) ||
	    ((size != 1) && (size != 2) && (size != 4) && (size != 8))) {
		return 0;
	}
	return 1;
}

static int qla4_8xxx_pci_set_window_warning_count;

static unsigned long
qla4_8xxx_pci_set_window(struct scsi_qla_host *ha, unsigned long long addr)
{
	int window;
	u32 win_read;

	if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		window = MN_WIN(addr);
		ha->ddr_mn_window = window;
		qla4_8xxx_wr_32(ha, ha->mn_win_crb |
		    QLA82XX_PCI_CRBSPACE, window);
		win_read = qla4_8xxx_rd_32(ha, ha->mn_win_crb |
		    QLA82XX_PCI_CRBSPACE);
		if ((win_read << 17) != window) {
			ql4_printk(KERN_WARNING, ha,
			"%s: Written MNwin (0x%x) != Read MNwin (0x%x)\n",
			__func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_DDR_NET;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM0,
				QLA82XX_ADDR_OCM0_MAX)) {
		unsigned int temp1;
		/* if bits 19:18&17:11 are on */
		if ((addr & 0x00ff800) == 0xff800) {
			printk("%s: QM access not handled.\n", __func__);
			addr = -1UL;
		}

		window = OCM_WIN(addr);
		ha->ddr_mn_window = window;
		qla4_8xxx_wr_32(ha, ha->mn_win_crb |
		    QLA82XX_PCI_CRBSPACE, window);
		win_read = qla4_8xxx_rd_32(ha, ha->mn_win_crb |
		    QLA82XX_PCI_CRBSPACE);
		temp1 = ((window & 0x1FF) << 7) |
		    ((window & 0x0FFFE0000) >> 17);
		if (win_read != temp1) {
			printk("%s: Written OCMwin (0x%x) != Read"
			    " OCMwin (0x%x)\n", __func__, temp1, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_OCM0_2M;

	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_QDR_NET,
				QLA82XX_P3_ADDR_QDR_NET_MAX)) {
		/* QDR network side */
		window = MS_WIN(addr);
		ha->qdr_sn_window = window;
		qla4_8xxx_wr_32(ha, ha->ms_win_crb |
		    QLA82XX_PCI_CRBSPACE, window);
		win_read = qla4_8xxx_rd_32(ha,
		     ha->ms_win_crb | QLA82XX_PCI_CRBSPACE);
		if (win_read != window) {
			printk("%s: Written MSwin (0x%x) != Read "
			    "MSwin (0x%x)\n", __func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_QDR_NET;

	} else {
		/*
		 * peg gdb frequently accesses memory that doesn't exist,
		 * this limits the chit chat so debugging isn't slowed down.
		 */
		if ((qla4_8xxx_pci_set_window_warning_count++ < 8) ||
		    (qla4_8xxx_pci_set_window_warning_count%64 == 0)) {
			printk("%s: Warning:%s Unknown address range!\n",
			    __func__, DRIVER_NAME);
		}
		addr = -1UL;
	}
	return addr;
}

/* check if address is in the same windows as the previous access */
static int qla4_8xxx_pci_is_same_window(struct scsi_qla_host *ha,
		unsigned long long addr)
{
	int window;
	unsigned long long qdr_max;

	qdr_max = QLA82XX_P3_ADDR_QDR_NET_MAX;

	if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		BUG();	/* MN access can not come here */
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM0,
	     QLA82XX_ADDR_OCM0_MAX)) {
		return 1;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM1,
	     QLA82XX_ADDR_OCM1_MAX)) {
		return 1;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_QDR_NET,
	    qdr_max)) {
		/* QDR network side */
		window = ((addr - QLA82XX_ADDR_QDR_NET) >> 22) & 0x3f;
		if (ha->qdr_sn_window == window)
			return 1;
	}

	return 0;
}

static int qla4_8xxx_pci_mem_read_direct(struct scsi_qla_host *ha,
		u64 off, void *data, int size)
{
	unsigned long flags;
	void __iomem *addr;
	int ret = 0;
	u64 start;
	void __iomem *mem_ptr = NULL;
	unsigned long mem_base;
	unsigned long mem_page;

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla4_8xxx_pci_set_window(ha, off);
	if ((start == -1UL) ||
	    (qla4_8xxx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		printk(KERN_ERR"%s out of bound pci memory access. "
				"offset is 0x%llx\n", DRIVER_NAME, off);
		return -1;
	}

	addr = qla4_8xxx_pci_base_offsetfset(ha, start);
	if (!addr) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		mem_base = pci_resource_start(ha->pdev, 0);
		mem_page = start & PAGE_MASK;
		/* Map two pages whenever user tries to access addresses in two
		   consecutive pages.
		 */
		if (mem_page != ((start + size - 1) & PAGE_MASK))
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE * 2);
		else
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);

		if (mem_ptr == NULL) {
			*(u8 *)data = 0;
			return -1;
		}
		addr = mem_ptr;
		addr += start & (PAGE_SIZE - 1);
		write_lock_irqsave(&ha->hw_lock, flags);
	}

	switch (size) {
	case 1:
		*(u8  *)data = readb(addr);
		break;
	case 2:
		*(u16 *)data = readw(addr);
		break;
	case 4:
		*(u32 *)data = readl(addr);
		break;
	case 8:
		*(u64 *)data = readq(addr);
		break;
	default:
		ret = -1;
		break;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);

	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

static int
qla4_8xxx_pci_mem_write_direct(struct scsi_qla_host *ha, u64 off,
		void *data, int size)
{
	unsigned long flags;
	void __iomem *addr;
	int ret = 0;
	u64 start;
	void __iomem *mem_ptr = NULL;
	unsigned long mem_base;
	unsigned long mem_page;

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla4_8xxx_pci_set_window(ha, off);
	if ((start == -1UL) ||
	    (qla4_8xxx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		printk(KERN_ERR"%s out of bound pci memory access. "
				"offset is 0x%llx\n", DRIVER_NAME, off);
		return -1;
	}

	addr = qla4_8xxx_pci_base_offsetfset(ha, start);
	if (!addr) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		mem_base = pci_resource_start(ha->pdev, 0);
		mem_page = start & PAGE_MASK;
		/* Map two pages whenever user tries to access addresses in two
		   consecutive pages.
		 */
		if (mem_page != ((start + size - 1) & PAGE_MASK))
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE*2);
		else
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);
		if (mem_ptr == NULL)
			return -1;

		addr = mem_ptr;
		addr += start & (PAGE_SIZE - 1);
		write_lock_irqsave(&ha->hw_lock, flags);
	}

	switch (size) {
	case 1:
		writeb(*(u8 *)data, addr);
		break;
	case 2:
		writew(*(u16 *)data, addr);
		break;
	case 4:
		writel(*(u32 *)data, addr);
		break;
	case 8:
		writeq(*(u64 *)data, addr);
		break;
	default:
		ret = -1;
		break;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);
	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

#define MTU_FUDGE_FACTOR 100

static unsigned long
qla4_8xxx_decode_crb_addr(unsigned long addr)
{
	int i;
	unsigned long base_addr, offset, pci_base;

	if (!qla4_8xxx_crb_table_initialized)
		qla4_8xxx_crb_addr_transform_setup();

	pci_base = ADDR_ERROR;
	base_addr = addr & 0xfff00000;
	offset = addr & 0x000fffff;

	for (i = 0; i < MAX_CRB_XFORM; i++) {
		if (crb_addr_xform[i] == base_addr) {
			pci_base = i << 20;
			break;
		}
	}
	if (pci_base == ADDR_ERROR)
		return pci_base;
	else
		return pci_base + offset;
}

static long rom_max_timeout = 100;
static long qla4_8xxx_rom_lock_timeout = 100;

static int
qla4_8xxx_rom_lock(struct scsi_qla_host *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore2 from PCI HW block */

		done = qla4_8xxx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_LOCK));
		if (done == 1)
			break;
		if (timeout >= qla4_8xxx_rom_lock_timeout)
			return -1;

		timeout++;

		/* Yield CPU */
		if (!in_interrupt())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();    /*This a nop instr on i386*/
		}
	}
	qla4_8xxx_wr_32(ha, QLA82XX_ROM_LOCK_ID, ROM_LOCK_DRIVER);
	return 0;
}

static void
qla4_8xxx_rom_unlock(struct scsi_qla_host *ha)
{
	qla4_8xxx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_UNLOCK));
}

static int
qla4_8xxx_wait_rom_done(struct scsi_qla_host *ha)
{
	long timeout = 0;
	long done = 0 ;

	while (done == 0) {
		done = qla4_8xxx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 2;
		timeout++;
		if (timeout >= rom_max_timeout) {
			printk("%s: Timeout reached  waiting for rom done",
					DRIVER_NAME);
			return -1;
		}
	}
	return 0;
}

static int
qla4_8xxx_do_rom_fast_read(struct scsi_qla_host *ha, int addr, int *valp)
{
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, addr);
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	if (qla4_8xxx_wait_rom_done(ha)) {
		printk("%s: Error waiting for rom done\n", DRIVER_NAME);
		return -1;
	}
	/* reset abyte_cnt and dummy_byte_cnt */
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	udelay(10);
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);

	*valp = qla4_8xxx_rd_32(ha, QLA82XX_ROMUSB_ROM_RDATA);
	return 0;
}

static int
qla4_8xxx_rom_fast_read(struct scsi_qla_host *ha, int addr, int *valp)
{
	int ret, loops = 0;

	while ((qla4_8xxx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		loops++;
	}
	if (loops >= 50000) {
		printk("%s: qla4_8xxx_rom_lock failed\n", DRIVER_NAME);
		return -1;
	}
	ret = qla4_8xxx_do_rom_fast_read(ha, addr, valp);
	qla4_8xxx_rom_unlock(ha);
	return ret;
}

/**
 * This routine does CRB initialize sequence
 * to put the ISP into operational state
 **/
static int
qla4_8xxx_pinit_from_rom(struct scsi_qla_host *ha, int verbose)
{
	int addr, val;
	int i ;
	struct crb_addr_pair *buf;
	unsigned long off;
	unsigned offset, n;

	struct crb_addr_pair {
		long addr;
		long data;
	};

	/* Halt all the indiviual PEGs and other blocks of the ISP */
	qla4_8xxx_rom_lock(ha);

	/* disable all I2Q */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_I2Q + 0x10, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_I2Q + 0x14, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_I2Q + 0x18, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_I2Q + 0x1c, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_I2Q + 0x20, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_I2Q + 0x24, 0x0);

	/* disable all niu interrupts */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_NIU + 0x40, 0xff);
	/* disable xge rx/tx */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_NIU + 0x70000, 0x00);
	/* disable xg1 rx/tx */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_NIU + 0x80000, 0x00);
	/* disable sideband mac */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_NIU + 0x90000, 0x00);
	/* disable ap0 mac */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_NIU + 0xa0000, 0x00);
	/* disable ap1 mac */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_NIU + 0xb0000, 0x00);

	/* halt sre */
	val = qla4_8xxx_rd_32(ha, QLA82XX_CRB_SRE + 0x1000);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_SRE + 0x1000, val & (~(0x1)));

	/* halt epg */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_EPG + 0x1300, 0x1);

	/* halt timers */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_TIMER + 0x0, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_TIMER + 0x8, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_TIMER + 0x10, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_TIMER + 0x18, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_TIMER + 0x100, 0x0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_TIMER + 0x200, 0x0);

	/* halt pegs */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x3c, 1);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_1 + 0x3c, 1);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_2 + 0x3c, 1);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_3 + 0x3c, 1);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_4 + 0x3c, 1);
	msleep(5);

	/* big hammer */
	if (test_bit(DPC_RESET_HA, &ha->dpc_flags))
		/* don't reset CAM block on reset */
		qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0xfeffffff);
	else
		qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0xffffffff);

	qla4_8xxx_rom_unlock(ha);

	/* Read the signature value from the flash.
	 * Offset 0: Contain signature (0xcafecafe)
	 * Offset 4: Offset and number of addr/value pairs
	 * that present in CRB initialize sequence
	 */
	if (qla4_8xxx_rom_fast_read(ha, 0, &n) != 0 || n != 0xcafecafeUL ||
	    qla4_8xxx_rom_fast_read(ha, 4, &n) != 0) {
		ql4_printk(KERN_WARNING, ha,
			"[ERROR] Reading crb_init area: n: %08x\n", n);
		return -1;
	}

	/* Offset in flash = lower 16 bits
	 * Number of enteries = upper 16 bits
	 */
	offset = n & 0xffffU;
	n = (n >> 16) & 0xffffU;

	/* number of addr/value pair should not exceed 1024 enteries */
	if (n  >= 1024) {
		ql4_printk(KERN_WARNING, ha,
		    "%s: %s:n=0x%x [ERROR] Card flash not initialized.\n",
		    DRIVER_NAME, __func__, n);
		return -1;
	}

	ql4_printk(KERN_INFO, ha,
		"%s: %d CRB init values found in ROM.\n", DRIVER_NAME, n);

	buf = kmalloc(n * sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL) {
		ql4_printk(KERN_WARNING, ha,
		    "%s: [ERROR] Unable to malloc memory.\n", DRIVER_NAME);
		return -1;
	}

	for (i = 0; i < n; i++) {
		if (qla4_8xxx_rom_fast_read(ha, 8*i + 4*offset, &val) != 0 ||
		    qla4_8xxx_rom_fast_read(ha, 8*i + 4*offset + 4, &addr) !=
		    0) {
			kfree(buf);
			return -1;
		}

		buf[i].addr = addr;
		buf[i].data = val;
	}

	for (i = 0; i < n; i++) {
		/* Translate internal CRB initialization
		 * address to PCI bus address
		 */
		off = qla4_8xxx_decode_crb_addr((unsigned long)buf[i].addr) +
		    QLA82XX_PCI_CRBSPACE;
		/* Not all CRB  addr/value pair to be written,
		 * some of them are skipped
		 */

		/* skip if LS bit is set*/
		if (off & 0x1) {
			DEBUG2(ql4_printk(KERN_WARNING, ha,
			    "Skip CRB init replay for offset = 0x%lx\n", off));
			continue;
		}

		/* skipping cold reboot MAGIC */
		if (off == QLA82XX_CAM_RAM(0x1fc))
			continue;

		/* do not reset PCI */
		if (off == (ROMUSB_GLB + 0xbc))
			continue;

		/* skip core clock, so that firmware can increase the clock */
		if (off == (ROMUSB_GLB + 0xc8))
			continue;

		/* skip the function enable register */
		if (off == QLA82XX_PCIE_REG(PCIE_SETUP_FUNCTION))
			continue;

		if (off == QLA82XX_PCIE_REG(PCIE_SETUP_FUNCTION2))
			continue;

		if ((off & 0x0ff00000) == QLA82XX_CRB_SMB)
			continue;

		if ((off & 0x0ff00000) == QLA82XX_CRB_DDR_NET)
			continue;

		if (off == ADDR_ERROR) {
			ql4_printk(KERN_WARNING, ha,
			    "%s: [ERROR] Unknown addr: 0x%08lx\n",
			    DRIVER_NAME, buf[i].addr);
			continue;
		}

		qla4_8xxx_wr_32(ha, off, buf[i].data);

		/* ISP requires much bigger delay to settle down,
		 * else crb_window returns 0xffffffff
		 */
		if (off == QLA82XX_ROMUSB_GLB_SW_RESET)
			msleep(1000);

		/* ISP requires millisec delay between
		 * successive CRB register updation
		 */
		msleep(1);
	}

	kfree(buf);

	/* Resetting the data and instruction cache */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_D+0xec, 0x1e);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_D+0x4c, 8);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_I+0x4c, 8);

	/* Clear all protocol processing engines */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_0+0x8, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_0+0xc, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_1+0x8, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_1+0xc, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_2+0x8, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_2+0xc, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_3+0x8, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_3+0xc, 0);

	return 0;
}

static int
qla4_8xxx_load_from_flash(struct scsi_qla_host *ha, uint32_t image_start)
{
	int  i, rval = 0;
	long size = 0;
	long flashaddr, memaddr;
	u64 data;
	u32 high, low;

	flashaddr = memaddr = ha->hw.flt_region_bootload;
	size = (image_start - flashaddr) / 8;

	DEBUG2(printk("scsi%ld: %s: bootldr=0x%lx, fw_image=0x%x\n",
	    ha->host_no, __func__, flashaddr, image_start));

	for (i = 0; i < size; i++) {
		if ((qla4_8xxx_rom_fast_read(ha, flashaddr, (int *)&low)) ||
		    (qla4_8xxx_rom_fast_read(ha, flashaddr + 4,
		    (int *)&high))) {
			rval = -1;
			goto exit_load_from_flash;
		}
		data = ((u64)high << 32) | low ;
		rval = qla4_8xxx_pci_mem_write_2M(ha, memaddr, &data, 8);
		if (rval)
			goto exit_load_from_flash;

		flashaddr += 8;
		memaddr   += 8;

		if (i % 0x1000 == 0)
			msleep(1);

	}

	udelay(100);

	read_lock(&ha->hw_lock);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x18, 0x1020);
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0x80001e);
	read_unlock(&ha->hw_lock);

exit_load_from_flash:
	return rval;
}

static int qla4_8xxx_load_fw(struct scsi_qla_host *ha, uint32_t image_start)
{
	u32 rst;

	qla4_8xxx_wr_32(ha, CRB_CMDPEG_STATE, 0);
	if (qla4_8xxx_pinit_from_rom(ha, 0) != QLA_SUCCESS) {
		printk(KERN_WARNING "%s: Error during CRB Initialization\n",
		    __func__);
		return QLA_ERROR;
	}

	udelay(500);

	/* at this point, QM is in reset. This could be a problem if there are
	 * incoming d* transition queue messages. QM/PCIE could wedge.
	 * To get around this, QM is brought out of reset.
	 */

	rst = qla4_8xxx_rd_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET);
	/* unreset qm */
	rst &= ~(1 << 28);
	qla4_8xxx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, rst);

	if (qla4_8xxx_load_from_flash(ha, image_start)) {
		printk("%s: Error trying to load fw from flash!\n", __func__);
		return QLA_ERROR;
	}

	return QLA_SUCCESS;
}

int
qla4_8xxx_pci_mem_read_2M(struct scsi_qla_host *ha,
		u64 off, void *data, int size)
{
	int i, j = 0, k, start, end, loop, sz[2], off0[2];
	int shift_amount;
	uint32_t temp;
	uint64_t off8, val, mem_crb, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */

	if (off >= QLA82XX_ADDR_QDR_NET && off <= QLA82XX_P3_ADDR_QDR_NET_MAX)
		mem_crb = QLA82XX_CRB_QDR_NET;
	else {
		mem_crb = QLA82XX_CRB_DDR_NET;
		if (qla4_8xxx_pci_mem_bound_check(ha, off, size) == 0)
			return qla4_8xxx_pci_mem_read_direct(ha,
					off, data, size);
	}


	off8 = off & 0xfffffff0;
	off0[0] = off & 0xf;
	sz[0] = (size < (16 - off0[0])) ? size : (16 - off0[0]);
	shift_amount = 4;

	loop = ((off0[0] + size - 1) >> shift_amount) + 1;
	off0[1] = 0;
	sz[1] = size - sz[0];

	for (i = 0; i < loop; i++) {
		temp = off8 + (i << shift_amount);
		qla4_8xxx_wr_32(ha, mem_crb + MIU_TEST_AGT_ADDR_LO, temp);
		temp = 0;
		qla4_8xxx_wr_32(ha, mem_crb + MIU_TEST_AGT_ADDR_HI, temp);
		temp = MIU_TA_CTL_ENABLE;
		qla4_8xxx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		qla4_8xxx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = qla4_8xxx_rd_32(ha, mem_crb + MIU_TEST_AGT_CTRL);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			printk_ratelimited(KERN_ERR
					   "%s: failed to read through agent\n",
					   __func__);
			break;
		}

		start = off0[i] >> 2;
		end   = (off0[i] + sz[i] - 1) >> 2;
		for (k = start; k <= end; k++) {
			temp = qla4_8xxx_rd_32(ha,
				mem_crb + MIU_TEST_AGT_RDDATA(k));
			word[i] |= ((uint64_t)temp << (32 * (k & 1)));
		}
	}

	if (j >= MAX_CTL_CHECK)
		return -1;

	if ((off0[0] & 7) == 0) {
		val = word[0];
	} else {
		val = ((word[0] >> (off0[0] * 8)) & (~(~0ULL << (sz[0] * 8)))) |
		((word[1] & (~(~0ULL << (sz[1] * 8)))) << (sz[0] * 8));
	}

	switch (size) {
	case 1:
		*(uint8_t  *)data = val;
		break;
	case 2:
		*(uint16_t *)data = val;
		break;
	case 4:
		*(uint32_t *)data = val;
		break;
	case 8:
		*(uint64_t *)data = val;
		break;
	}
	return 0;
}

int
qla4_8xxx_pci_mem_write_2M(struct scsi_qla_host *ha,
		u64 off, void *data, int size)
{
	int i, j, ret = 0, loop, sz[2], off0;
	int scale, shift_amount, startword;
	uint32_t temp;
	uint64_t off8, mem_crb, tmpw, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */
	if (off >= QLA82XX_ADDR_QDR_NET && off <= QLA82XX_P3_ADDR_QDR_NET_MAX)
		mem_crb = QLA82XX_CRB_QDR_NET;
	else {
		mem_crb = QLA82XX_CRB_DDR_NET;
		if (qla4_8xxx_pci_mem_bound_check(ha, off, size) == 0)
			return qla4_8xxx_pci_mem_write_direct(ha,
					off, data, size);
	}

	off0 = off & 0x7;
	sz[0] = (size < (8 - off0)) ? size : (8 - off0);
	sz[1] = size - sz[0];

	off8 = off & 0xfffffff0;
	loop = (((off & 0xf) + size - 1) >> 4) + 1;
	shift_amount = 4;
	scale = 2;
	startword = (off & 0xf)/8;

	for (i = 0; i < loop; i++) {
		if (qla4_8xxx_pci_mem_read_2M(ha, off8 +
		    (i << shift_amount), &word[i * scale], 8))
			return -1;
	}

	switch (size) {
	case 1:
		tmpw = *((uint8_t *)data);
		break;
	case 2:
		tmpw = *((uint16_t *)data);
		break;
	case 4:
		tmpw = *((uint32_t *)data);
		break;
	case 8:
	default:
		tmpw = *((uint64_t *)data);
		break;
	}

	if (sz[0] == 8)
		word[startword] = tmpw;
	else {
		word[startword] &=
		    ~((~(~0ULL << (sz[0] * 8))) << (off0 * 8));
		word[startword] |= tmpw << (off0 * 8);
	}

	if (sz[1] != 0) {
		word[startword+1] &= ~(~0ULL << (sz[1] * 8));
		word[startword+1] |= tmpw >> (sz[0] * 8);
	}

	for (i = 0; i < loop; i++) {
		temp = off8 + (i << shift_amount);
		qla4_8xxx_wr_32(ha, mem_crb+MIU_TEST_AGT_ADDR_LO, temp);
		temp = 0;
		qla4_8xxx_wr_32(ha, mem_crb+MIU_TEST_AGT_ADDR_HI, temp);
		temp = word[i * scale] & 0xffffffff;
		qla4_8xxx_wr_32(ha, mem_crb+MIU_TEST_AGT_WRDATA_LO, temp);
		temp = (word[i * scale] >> 32) & 0xffffffff;
		qla4_8xxx_wr_32(ha, mem_crb+MIU_TEST_AGT_WRDATA_HI, temp);
		temp = word[i*scale + 1] & 0xffffffff;
		qla4_8xxx_wr_32(ha, mem_crb + MIU_TEST_AGT_WRDATA_UPPER_LO,
		    temp);
		temp = (word[i*scale + 1] >> 32) & 0xffffffff;
		qla4_8xxx_wr_32(ha, mem_crb + MIU_TEST_AGT_WRDATA_UPPER_HI,
		    temp);

		temp = MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		qla4_8xxx_wr_32(ha, mem_crb+MIU_TEST_AGT_CTRL, temp);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		qla4_8xxx_wr_32(ha, mem_crb+MIU_TEST_AGT_CTRL, temp);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = qla4_8xxx_rd_32(ha, mem_crb + MIU_TEST_AGT_CTRL);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				ql4_printk(KERN_ERR, ha,
					   "%s: failed to read through agent\n",
					   __func__);
			ret = -1;
			break;
		}
	}

	return ret;
}

static int qla4_8xxx_cmdpeg_ready(struct scsi_qla_host *ha, int pegtune_val)
{
	u32 val = 0;
	int retries = 60;

	if (!pegtune_val) {
		do {
			val = qla4_8xxx_rd_32(ha, CRB_CMDPEG_STATE);
			if ((val == PHAN_INITIALIZE_COMPLETE) ||
			    (val == PHAN_INITIALIZE_ACK))
				return 0;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(500);

		} while (--retries);

		if (!retries) {
			pegtune_val = qla4_8xxx_rd_32(ha,
				QLA82XX_ROMUSB_GLB_PEGTUNE_DONE);
			printk(KERN_WARNING "%s: init failed, "
				"pegtune_val = %x\n", __func__, pegtune_val);
			return -1;
		}
	}
	return 0;
}

static int qla4_8xxx_rcvpeg_ready(struct scsi_qla_host *ha)
{
	uint32_t state = 0;
	int loops = 0;

	/* Window 1 call */
	read_lock(&ha->hw_lock);
	state = qla4_8xxx_rd_32(ha, CRB_RCVPEG_STATE);
	read_unlock(&ha->hw_lock);

	while ((state != PHAN_PEG_RCV_INITIALIZED) && (loops < 30000)) {
		udelay(100);
		/* Window 1 call */
		read_lock(&ha->hw_lock);
		state = qla4_8xxx_rd_32(ha, CRB_RCVPEG_STATE);
		read_unlock(&ha->hw_lock);

		loops++;
	}

	if (loops >= 30000) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "Receive Peg initialization not complete: 0x%x.\n", state));
		return QLA_ERROR;
	}

	return QLA_SUCCESS;
}

void
qla4_8xxx_set_drv_active(struct scsi_qla_host *ha)
{
	uint32_t drv_active;

	drv_active = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	drv_active |= (1 << (ha->func_num * 4));
	ql4_printk(KERN_INFO, ha, "%s(%ld): drv_active: 0x%08x\n",
		   __func__, ha->host_no, drv_active);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, drv_active);
}

void
qla4_8xxx_clear_drv_active(struct scsi_qla_host *ha)
{
	uint32_t drv_active;

	drv_active = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	drv_active &= ~(1 << (ha->func_num * 4));
	ql4_printk(KERN_INFO, ha, "%s(%ld): drv_active: 0x%08x\n",
		   __func__, ha->host_no, drv_active);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, drv_active);
}

static inline int
qla4_8xxx_need_reset(struct scsi_qla_host *ha)
{
	uint32_t drv_state, drv_active;
	int rval;

	drv_active = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	drv_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	rval = drv_state & (1 << (ha->func_num * 4));
	if ((test_bit(AF_EEH_BUSY, &ha->flags)) && drv_active)
		rval = 1;

	return rval;
}

static inline void
qla4_8xxx_set_rst_ready(struct scsi_qla_host *ha)
{
	uint32_t drv_state;

	drv_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_state |= (1 << (ha->func_num * 4));
	ql4_printk(KERN_INFO, ha, "%s(%ld): drv_state: 0x%08x\n",
		   __func__, ha->host_no, drv_state);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DRV_STATE, drv_state);
}

static inline void
qla4_8xxx_clear_rst_ready(struct scsi_qla_host *ha)
{
	uint32_t drv_state;

	drv_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_state &= ~(1 << (ha->func_num * 4));
	ql4_printk(KERN_INFO, ha, "%s(%ld): drv_state: 0x%08x\n",
		   __func__, ha->host_no, drv_state);
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DRV_STATE, drv_state);
}

static inline void
qla4_8xxx_set_qsnt_ready(struct scsi_qla_host *ha)
{
	uint32_t qsnt_state;

	qsnt_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	qsnt_state |= (2 << (ha->func_num * 4));
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DRV_STATE, qsnt_state);
}


static int
qla4_8xxx_start_firmware(struct scsi_qla_host *ha, uint32_t image_start)
{
	int pcie_cap;
	uint16_t lnk;

	/* scrub dma mask expansion register */
	qla4_8xxx_wr_32(ha, CRB_DMA_SHIFT, 0x55555555);

	/* Overwrite stale initialization register values */
	qla4_8xxx_wr_32(ha, CRB_CMDPEG_STATE, 0);
	qla4_8xxx_wr_32(ha, CRB_RCVPEG_STATE, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_PEG_HALT_STATUS1, 0);
	qla4_8xxx_wr_32(ha, QLA82XX_PEG_HALT_STATUS2, 0);

	if (qla4_8xxx_load_fw(ha, image_start) != QLA_SUCCESS) {
		printk("%s: Error trying to start fw!\n", __func__);
		return QLA_ERROR;
	}

	/* Handshake with the card before we register the devices. */
	if (qla4_8xxx_cmdpeg_ready(ha, 0) != QLA_SUCCESS) {
		printk("%s: Error during card handshake!\n", __func__);
		return QLA_ERROR;
	}

	/* Negotiated Link width */
	pcie_cap = pci_pcie_cap(ha->pdev);
	pci_read_config_word(ha->pdev, pcie_cap + PCI_EXP_LNKSTA, &lnk);
	ha->link_width = (lnk >> 4) & 0x3f;

	/* Synchronize with Receive peg */
	return qla4_8xxx_rcvpeg_ready(ha);
}

static int
qla4_8xxx_try_start_fw(struct scsi_qla_host *ha)
{
	int rval = QLA_ERROR;

	/*
	 * FW Load priority:
	 * 1) Operational firmware residing in flash.
	 * 2) Fail
	 */

	ql4_printk(KERN_INFO, ha,
	    "FW: Retrieving flash offsets from FLT/FDT ...\n");
	rval = qla4_8xxx_get_flash_info(ha);
	if (rval != QLA_SUCCESS)
		return rval;

	ql4_printk(KERN_INFO, ha,
	    "FW: Attempting to load firmware from flash...\n");
	rval = qla4_8xxx_start_firmware(ha, ha->hw.flt_region_fw);

	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "FW: Load firmware from flash"
		    " FAILED...\n");
		return rval;
	}

	return rval;
}

static void qla4_8xxx_rom_lock_recovery(struct scsi_qla_host *ha)
{
	if (qla4_8xxx_rom_lock(ha)) {
		/* Someone else is holding the lock. */
		dev_info(&ha->pdev->dev, "Resetting rom_lock\n");
	}

	/*
	 * Either we got the lock, or someone
	 * else died while holding it.
	 * In either case, unlock.
	 */
	qla4_8xxx_rom_unlock(ha);
}

static void qla4_8xxx_minidump_process_rdcrb(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla82xx_minidump_entry_crb *crb_hdr;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	crb_hdr = (struct qla82xx_minidump_entry_crb *)entry_hdr;
	r_addr = crb_hdr->addr;
	r_stride = crb_hdr->crb_strd.addr_stride;
	loop_cnt = crb_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		r_value = qla4_8xxx_md_rw_32(ha, r_addr, 0, 0);
		*data_ptr++ = cpu_to_le32(r_addr);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += r_stride;
	}
	*d_ptr = data_ptr;
}

static int qla4_8xxx_minidump_process_l2tag(struct scsi_qla_host *ha,
				 struct qla82xx_minidump_entry_hdr *entry_hdr,
				 uint32_t **d_ptr)
{
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	unsigned long p_wait, w_time, p_mask;
	uint32_t c_value_w, c_value_r;
	struct qla82xx_minidump_entry_cache *cache_hdr;
	int rval = QLA_ERROR;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	cache_hdr = (struct qla82xx_minidump_entry_cache *)entry_hdr;

	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;
	p_wait = cache_hdr->cache_ctrl.poll_wait;
	p_mask = cache_hdr->cache_ctrl.poll_mask;

	for (i = 0; i < loop_count; i++) {
		qla4_8xxx_md_rw_32(ha, t_r_addr, t_value, 1);

		if (c_value_w)
			qla4_8xxx_md_rw_32(ha, c_addr, c_value_w, 1);

		if (p_mask) {
			w_time = jiffies + p_wait;
			do {
				c_value_r = qla4_8xxx_md_rw_32(ha, c_addr,
								0, 0);
				if ((c_value_r & p_mask) == 0) {
					break;
				} else if (time_after_eq(jiffies, w_time)) {
					/* capturing dump failed */
					return rval;
				}
			} while (1);
		}

		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla4_8xxx_md_rw_32(ha, addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}

		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

static int qla4_8xxx_minidump_process_control(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr)
{
	struct qla82xx_minidump_entry_crb *crb_entry;
	uint32_t read_value, opcode, poll_time, addr, index, rval = QLA_SUCCESS;
	uint32_t crb_addr;
	unsigned long wtime;
	struct qla4_8xxx_minidump_template_hdr *tmplt_hdr;
	int i;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	tmplt_hdr = (struct qla4_8xxx_minidump_template_hdr *)
						ha->fw_dump_tmplt_hdr;
	crb_entry = (struct qla82xx_minidump_entry_crb *)entry_hdr;

	crb_addr = crb_entry->addr;
	for (i = 0; i < crb_entry->op_count; i++) {
		opcode = crb_entry->crb_ctrl.opcode;
		if (opcode & QLA82XX_DBG_OPCODE_WR) {
			qla4_8xxx_md_rw_32(ha, crb_addr,
					   crb_entry->value_1, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_WR;
		}
		if (opcode & QLA82XX_DBG_OPCODE_RW) {
			read_value = qla4_8xxx_md_rw_32(ha, crb_addr, 0, 0);
			qla4_8xxx_md_rw_32(ha, crb_addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_RW;
		}
		if (opcode & QLA82XX_DBG_OPCODE_AND) {
			read_value = qla4_8xxx_md_rw_32(ha, crb_addr, 0, 0);
			read_value &= crb_entry->value_2;
			opcode &= ~QLA82XX_DBG_OPCODE_AND;
			if (opcode & QLA82XX_DBG_OPCODE_OR) {
				read_value |= crb_entry->value_3;
				opcode &= ~QLA82XX_DBG_OPCODE_OR;
			}
			qla4_8xxx_md_rw_32(ha, crb_addr, read_value, 1);
		}
		if (opcode & QLA82XX_DBG_OPCODE_OR) {
			read_value = qla4_8xxx_md_rw_32(ha, crb_addr, 0, 0);
			read_value |= crb_entry->value_3;
			qla4_8xxx_md_rw_32(ha, crb_addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_OR;
		}
		if (opcode & QLA82XX_DBG_OPCODE_POLL) {
			poll_time = crb_entry->crb_strd.poll_timeout;
			wtime = jiffies + poll_time;
			read_value = qla4_8xxx_md_rw_32(ha, crb_addr, 0, 0);

			do {
				if ((read_value & crb_entry->value_2) ==
				    crb_entry->value_1)
					break;
				else if (time_after_eq(jiffies, wtime)) {
					/* capturing dump failed */
					rval = QLA_ERROR;
					break;
				} else
					read_value = qla4_8xxx_md_rw_32(ha,
								crb_addr, 0, 0);
			} while (1);
			opcode &= ~QLA82XX_DBG_OPCODE_POLL;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RDSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else {
				addr = crb_addr;
			}

			read_value = qla4_8xxx_md_rw_32(ha, addr, 0, 0);
			index = crb_entry->crb_ctrl.state_index_v;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_RDSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_WRSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else {
				addr = crb_addr;
			}

			if (crb_entry->crb_ctrl.state_index_v) {
				index = crb_entry->crb_ctrl.state_index_v;
				read_value =
					tmplt_hdr->saved_state_array[index];
			} else {
				read_value = crb_entry->value_1;
			}

			qla4_8xxx_md_rw_32(ha, addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_WRSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_MDSTATE) {
			index = crb_entry->crb_ctrl.state_index_v;
			read_value = tmplt_hdr->saved_state_array[index];
			read_value <<= crb_entry->crb_ctrl.shl;
			read_value >>= crb_entry->crb_ctrl.shr;
			if (crb_entry->value_2)
				read_value &= crb_entry->value_2;
			read_value |= crb_entry->value_3;
			read_value += crb_entry->value_1;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_MDSTATE;
		}
		crb_addr += crb_entry->crb_strd.addr_stride;
	}
	DEBUG2(ql4_printk(KERN_INFO, ha, "Leaving fn: %s\n", __func__));
	return rval;
}

static void qla4_8xxx_minidump_process_rdocm(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla82xx_minidump_entry_rdocm *ocm_hdr;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	ocm_hdr = (struct qla82xx_minidump_entry_rdocm *)entry_hdr;
	r_addr = ocm_hdr->read_addr;
	r_stride = ocm_hdr->read_addr_stride;
	loop_cnt = ocm_hdr->op_count;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "[%s]: r_addr: 0x%x, r_stride: 0x%x, loop_cnt: 0x%x\n",
			  __func__, r_addr, r_stride, loop_cnt));

	for (i = 0; i < loop_cnt; i++) {
		r_value = readl((void __iomem *)(r_addr + ha->nx_pcibase));
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += r_stride;
	}
	DEBUG2(ql4_printk(KERN_INFO, ha, "Leaving fn: %s datacount: 0x%lx\n",
			  __func__, (loop_cnt * sizeof(uint32_t))));
	*d_ptr = data_ptr;
}

static void qla4_8xxx_minidump_process_rdmux(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t r_addr, s_stride, s_addr, s_value, loop_cnt, i, r_value;
	struct qla82xx_minidump_entry_mux *mux_hdr;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	mux_hdr = (struct qla82xx_minidump_entry_mux *)entry_hdr;
	r_addr = mux_hdr->read_addr;
	s_addr = mux_hdr->select_addr;
	s_stride = mux_hdr->select_value_stride;
	s_value = mux_hdr->select_value;
	loop_cnt = mux_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla4_8xxx_md_rw_32(ha, s_addr, s_value, 1);
		r_value = qla4_8xxx_md_rw_32(ha, r_addr, 0, 0);
		*data_ptr++ = cpu_to_le32(s_value);
		*data_ptr++ = cpu_to_le32(r_value);
		s_value += s_stride;
	}
	*d_ptr = data_ptr;
}

static void qla4_8xxx_minidump_process_l1cache(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	uint32_t c_value_w;
	struct qla82xx_minidump_entry_cache *cache_hdr;
	uint32_t *data_ptr = *d_ptr;

	cache_hdr = (struct qla82xx_minidump_entry_cache *)entry_hdr;
	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;

	for (i = 0; i < loop_count; i++) {
		qla4_8xxx_md_rw_32(ha, t_r_addr, t_value, 1);
		qla4_8xxx_md_rw_32(ha, c_addr, c_value_w, 1);
		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla4_8xxx_md_rw_32(ha, addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
}

static void qla4_8xxx_minidump_process_queue(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t s_addr, r_addr;
	uint32_t r_stride, r_value, r_cnt, qid = 0;
	uint32_t i, k, loop_cnt;
	struct qla82xx_minidump_entry_queue *q_hdr;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	q_hdr = (struct qla82xx_minidump_entry_queue *)entry_hdr;
	s_addr = q_hdr->select_addr;
	r_cnt = q_hdr->rd_strd.read_addr_cnt;
	r_stride = q_hdr->rd_strd.read_addr_stride;
	loop_cnt = q_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla4_8xxx_md_rw_32(ha, s_addr, qid, 1);
		r_addr = q_hdr->read_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla4_8xxx_md_rw_32(ha, r_addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			r_addr += r_stride;
		}
		qid += q_hdr->q_strd.queue_id_stride;
	}
	*d_ptr = data_ptr;
}

#define MD_DIRECT_ROM_WINDOW		0x42110030
#define MD_DIRECT_ROM_READ_BASE		0x42150000

static void qla4_8xxx_minidump_process_rdrom(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t r_addr, r_value;
	uint32_t i, loop_cnt;
	struct qla82xx_minidump_entry_rdrom *rom_hdr;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	rom_hdr = (struct qla82xx_minidump_entry_rdrom *)entry_hdr;
	r_addr = rom_hdr->read_addr;
	loop_cnt = rom_hdr->read_data_size/sizeof(uint32_t);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "[%s]: flash_addr: 0x%x, read_data_size: 0x%x\n",
			   __func__, r_addr, loop_cnt));

	for (i = 0; i < loop_cnt; i++) {
		qla4_8xxx_md_rw_32(ha, MD_DIRECT_ROM_WINDOW,
				   (r_addr & 0xFFFF0000), 1);
		r_value = qla4_8xxx_md_rw_32(ha,
					     MD_DIRECT_ROM_READ_BASE +
					     (r_addr & 0x0000FFFF), 0, 0);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += sizeof(uint32_t);
	}
	*d_ptr = data_ptr;
}

#define MD_MIU_TEST_AGT_CTRL		0x41000090
#define MD_MIU_TEST_AGT_ADDR_LO		0x41000094
#define MD_MIU_TEST_AGT_ADDR_HI		0x41000098

static int qla4_8xxx_minidump_process_rdmem(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				uint32_t **d_ptr)
{
	uint32_t r_addr, r_value, r_data;
	uint32_t i, j, loop_cnt;
	struct qla82xx_minidump_entry_rdmem *m_hdr;
	unsigned long flags;
	uint32_t *data_ptr = *d_ptr;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Entering fn: %s\n", __func__));
	m_hdr = (struct qla82xx_minidump_entry_rdmem *)entry_hdr;
	r_addr = m_hdr->read_addr;
	loop_cnt = m_hdr->read_data_size/16;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "[%s]: Read addr: 0x%x, read_data_size: 0x%x\n",
			  __func__, r_addr, m_hdr->read_data_size));

	if (r_addr & 0xf) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "[%s]: Read addr 0x%x not 16 bytes alligned\n",
				  __func__, r_addr));
		return QLA_ERROR;
	}

	if (m_hdr->read_data_size % 16) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "[%s]: Read data[0x%x] not multiple of 16 bytes\n",
				  __func__, m_hdr->read_data_size));
		return QLA_ERROR;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "[%s]: rdmem_addr: 0x%x, read_data_size: 0x%x, loop_cnt: 0x%x\n",
			  __func__, r_addr, m_hdr->read_data_size, loop_cnt));

	write_lock_irqsave(&ha->hw_lock, flags);
	for (i = 0; i < loop_cnt; i++) {
		qla4_8xxx_md_rw_32(ha, MD_MIU_TEST_AGT_ADDR_LO, r_addr, 1);
		r_value = 0;
		qla4_8xxx_md_rw_32(ha, MD_MIU_TEST_AGT_ADDR_HI, r_value, 1);
		r_value = MIU_TA_CTL_ENABLE;
		qla4_8xxx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL, r_value, 1);
		r_value = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		qla4_8xxx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL, r_value, 1);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			r_value = qla4_8xxx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL,
						     0, 0);
			if ((r_value & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			printk_ratelimited(KERN_ERR
					   "%s: failed to read through agent\n",
					    __func__);
			write_unlock_irqrestore(&ha->hw_lock, flags);
			return QLA_SUCCESS;
		}

		for (j = 0; j < 4; j++) {
			r_data = qla4_8xxx_md_rw_32(ha,
						    MD_MIU_TEST_AGT_RDDATA[j],
						    0, 0);
			*data_ptr++ = cpu_to_le32(r_data);
		}

		r_addr += 16;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);

	DEBUG2(ql4_printk(KERN_INFO, ha, "Leaving fn: %s datacount: 0x%x\n",
			  __func__, (loop_cnt * 16)));

	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

static void ql4_8xxx_mark_entry_skipped(struct scsi_qla_host *ha,
				struct qla82xx_minidump_entry_hdr *entry_hdr,
				int index)
{
	entry_hdr->d_ctrl.driver_flags |= QLA82XX_DBG_SKIPPED_FLAG;
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "scsi(%ld): Skipping entry[%d]: ETYPE[0x%x]-ELEVEL[0x%x]\n",
			  ha->host_no, index, entry_hdr->entry_type,
			  entry_hdr->d_ctrl.entry_capture_mask));
}

/**
 * qla82xx_collect_md_data - Retrieve firmware minidump data.
 * @ha: pointer to adapter structure
 **/
static int qla4_8xxx_collect_md_data(struct scsi_qla_host *ha)
{
	int num_entry_hdr = 0;
	struct qla82xx_minidump_entry_hdr *entry_hdr;
	struct qla4_8xxx_minidump_template_hdr *tmplt_hdr;
	uint32_t *data_ptr;
	uint32_t data_collected = 0;
	int i, rval = QLA_ERROR;
	uint64_t now;
	uint32_t timestamp;

	if (!ha->fw_dump) {
		ql4_printk(KERN_INFO, ha, "%s(%ld) No buffer to dump\n",
			   __func__, ha->host_no);
		return rval;
	}

	tmplt_hdr = (struct qla4_8xxx_minidump_template_hdr *)
						ha->fw_dump_tmplt_hdr;
	data_ptr = (uint32_t *)((uint8_t *)ha->fw_dump +
						ha->fw_dump_tmplt_size);
	data_collected += ha->fw_dump_tmplt_size;

	num_entry_hdr = tmplt_hdr->num_of_entries;
	ql4_printk(KERN_INFO, ha, "[%s]: starting data ptr: %p\n",
		   __func__, data_ptr);
	ql4_printk(KERN_INFO, ha,
		   "[%s]: no of entry headers in Template: 0x%x\n",
		   __func__, num_entry_hdr);
	ql4_printk(KERN_INFO, ha, "[%s]: Capture Mask obtained: 0x%x\n",
		   __func__, ha->fw_dump_capture_mask);
	ql4_printk(KERN_INFO, ha, "[%s]: Total_data_size 0x%x, %d obtained\n",
		   __func__, ha->fw_dump_size, ha->fw_dump_size);

	/* Update current timestamp before taking dump */
	now = get_jiffies_64();
	timestamp = (u32)(jiffies_to_msecs(now) / 1000);
	tmplt_hdr->driver_timestamp = timestamp;

	entry_hdr = (struct qla82xx_minidump_entry_hdr *)
					(((uint8_t *)ha->fw_dump_tmplt_hdr) +
					 tmplt_hdr->first_entry_offset);

	/* Walk through the entry headers - validate/perform required action */
	for (i = 0; i < num_entry_hdr; i++) {
		if (data_collected >= ha->fw_dump_size) {
			ql4_printk(KERN_INFO, ha,
				   "Data collected: [0x%x], Total Dump size: [0x%x]\n",
				   data_collected, ha->fw_dump_size);
			return rval;
		}

		if (!(entry_hdr->d_ctrl.entry_capture_mask &
		      ha->fw_dump_capture_mask)) {
			entry_hdr->d_ctrl.driver_flags |=
						QLA82XX_DBG_SKIPPED_FLAG;
			goto skip_nxt_entry;
		}

		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "Data collected: [0x%x], Dump size left:[0x%x]\n",
				  data_collected,
				  (ha->fw_dump_size - data_collected)));

		/* Decode the entry type and take required action to capture
		 * debug data
		 */
		switch (entry_hdr->entry_type) {
		case QLA82XX_RDEND:
			ql4_8xxx_mark_entry_skipped(ha, entry_hdr, i);
			break;
		case QLA82XX_CNTRL:
			rval = qla4_8xxx_minidump_process_control(ha,
								  entry_hdr);
			if (rval != QLA_SUCCESS) {
				ql4_8xxx_mark_entry_skipped(ha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_RDCRB:
			qla4_8xxx_minidump_process_rdcrb(ha, entry_hdr,
							 &data_ptr);
			break;
		case QLA82XX_RDMEM:
			rval = qla4_8xxx_minidump_process_rdmem(ha, entry_hdr,
								&data_ptr);
			if (rval != QLA_SUCCESS) {
				ql4_8xxx_mark_entry_skipped(ha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_BOARD:
		case QLA82XX_RDROM:
			qla4_8xxx_minidump_process_rdrom(ha, entry_hdr,
							 &data_ptr);
			break;
		case QLA82XX_L2DTG:
		case QLA82XX_L2ITG:
		case QLA82XX_L2DAT:
		case QLA82XX_L2INS:
			rval = qla4_8xxx_minidump_process_l2tag(ha, entry_hdr,
								&data_ptr);
			if (rval != QLA_SUCCESS) {
				ql4_8xxx_mark_entry_skipped(ha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_L1DAT:
		case QLA82XX_L1INS:
			qla4_8xxx_minidump_process_l1cache(ha, entry_hdr,
							   &data_ptr);
			break;
		case QLA82XX_RDOCM:
			qla4_8xxx_minidump_process_rdocm(ha, entry_hdr,
							 &data_ptr);
			break;
		case QLA82XX_RDMUX:
			qla4_8xxx_minidump_process_rdmux(ha, entry_hdr,
							 &data_ptr);
			break;
		case QLA82XX_QUEUE:
			qla4_8xxx_minidump_process_queue(ha, entry_hdr,
							 &data_ptr);
			break;
		case QLA82XX_RDNOP:
		default:
			ql4_8xxx_mark_entry_skipped(ha, entry_hdr, i);
			break;
		}

		data_collected = (uint8_t *)data_ptr -
				 ((uint8_t *)((uint8_t *)ha->fw_dump +
						ha->fw_dump_tmplt_size));
skip_nxt_entry:
		/*  next entry in the template */
		entry_hdr = (struct qla82xx_minidump_entry_hdr *)
				(((uint8_t *)entry_hdr) +
				 entry_hdr->entry_size);
	}

	if ((data_collected + ha->fw_dump_tmplt_size) != ha->fw_dump_size) {
		ql4_printk(KERN_INFO, ha,
			   "Dump data mismatch: Data collected: [0x%x], total_data_size:[0x%x]\n",
			   data_collected, ha->fw_dump_size);
		goto md_failed;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "Leaving fn: %s Last entry: 0x%x\n",
			  __func__, i));
md_failed:
	return rval;
}

/**
 * qla4_8xxx_uevent_emit - Send uevent when the firmware dump is ready.
 * @ha: pointer to adapter structure
 **/
static void qla4_8xxx_uevent_emit(struct scsi_qla_host *ha, u32 code)
{
	char event_string[40];
	char *envp[] = { event_string, NULL };

	switch (code) {
	case QL4_UEVENT_CODE_FW_DUMP:
		snprintf(event_string, sizeof(event_string), "FW_DUMP=%ld",
			 ha->host_no);
		break;
	default:
		/*do nothing*/
		break;
	}

	kobject_uevent_env(&(&ha->pdev->dev)->kobj, KOBJ_CHANGE, envp);
}

/**
 * qla4_8xxx_device_bootstrap - Initialize device, set DEV_READY, start fw
 * @ha: pointer to adapter structure
 *
 * Note: IDC lock must be held upon entry
 **/
static int
qla4_8xxx_device_bootstrap(struct scsi_qla_host *ha)
{
	int rval = QLA_ERROR;
	int i, timeout;
	uint32_t old_count, count;
	int need_reset = 0, peg_stuck = 1;

	need_reset = qla4_8xxx_need_reset(ha);

	old_count = qla4_8xxx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);

	for (i = 0; i < 10; i++) {
		timeout = msleep_interruptible(200);
		if (timeout) {
			qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			   QLA82XX_DEV_FAILED);
			return rval;
		}

		count = qla4_8xxx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);
		if (count != old_count)
			peg_stuck = 0;
	}

	if (need_reset) {
		/* We are trying to perform a recovery here. */
		if (peg_stuck)
			qla4_8xxx_rom_lock_recovery(ha);
		goto dev_initialize;
	} else  {
		/* Start of day for this ha context. */
		if (peg_stuck) {
			/* Either we are the first or recovery in progress. */
			qla4_8xxx_rom_lock_recovery(ha);
			goto dev_initialize;
		} else {
			/* Firmware already running. */
			rval = QLA_SUCCESS;
			goto dev_ready;
		}
	}

dev_initialize:
	/* set to DEV_INITIALIZING */
	ql4_printk(KERN_INFO, ha, "HW State: INITIALIZING\n");
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_INITIALIZING);

	/* Driver that sets device state to initializating sets IDC version */
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DRV_IDC_VERSION, QLA82XX_IDC_VERSION);

	qla4_8xxx_idc_unlock(ha);
	if (ql4xenablemd && test_bit(AF_FW_RECOVERY, &ha->flags) &&
	    !test_and_set_bit(AF_82XX_FW_DUMPED, &ha->flags)) {
		if (!qla4_8xxx_collect_md_data(ha)) {
			qla4_8xxx_uevent_emit(ha, QL4_UEVENT_CODE_FW_DUMP);
		} else {
			ql4_printk(KERN_INFO, ha, "Unable to collect minidump\n");
			clear_bit(AF_82XX_FW_DUMPED, &ha->flags);
		}
	}
	rval = qla4_8xxx_try_start_fw(ha);
	qla4_8xxx_idc_lock(ha);

	if (rval != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "HW State: FAILED\n");
		qla4_8xxx_clear_drv_active(ha);
		qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_FAILED);
		return rval;
	}

dev_ready:
	ql4_printk(KERN_INFO, ha, "HW State: READY\n");
	qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_READY);

	return rval;
}

/**
 * qla4_8xxx_need_reset_handler - Code to start reset sequence
 * @ha: pointer to adapter structure
 *
 * Note: IDC lock must be held upon entry
 **/
static void
qla4_8xxx_need_reset_handler(struct scsi_qla_host *ha)
{
	uint32_t dev_state, drv_state, drv_active;
	uint32_t active_mask = 0xFFFFFFFF;
	unsigned long reset_timeout;

	ql4_printk(KERN_INFO, ha,
		"Performing ISP error recovery\n");

	if (test_and_clear_bit(AF_ONLINE, &ha->flags)) {
		qla4_8xxx_idc_unlock(ha);
		ha->isp_ops->disable_intrs(ha);
		qla4_8xxx_idc_lock(ha);
	}

	if (!test_bit(AF_82XX_RST_OWNER, &ha->flags)) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s(%ld): reset acknowledged\n",
				  __func__, ha->host_no));
		qla4_8xxx_set_rst_ready(ha);
	} else {
		active_mask = (~(1 << (ha->func_num * 4)));
	}

	/* wait for 10 seconds for reset ack from all functions */
	reset_timeout = jiffies + (ha->nx_reset_timeout * HZ);

	drv_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_active = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);

	ql4_printk(KERN_INFO, ha,
		"%s(%ld): drv_state = 0x%x, drv_active = 0x%x\n",
		__func__, ha->host_no, drv_state, drv_active);

	while (drv_state != (drv_active & active_mask)) {
		if (time_after_eq(jiffies, reset_timeout)) {
			ql4_printk(KERN_INFO, ha,
				   "%s: RESET TIMEOUT! drv_state: 0x%08x, drv_active: 0x%08x\n",
				   DRIVER_NAME, drv_state, drv_active);
			break;
		}

		/*
		 * When reset_owner times out, check which functions
		 * acked/did not ack
		 */
		if (test_bit(AF_82XX_RST_OWNER, &ha->flags)) {
			ql4_printk(KERN_INFO, ha,
				   "%s(%ld): drv_state = 0x%x, drv_active = 0x%x\n",
				   __func__, ha->host_no, drv_state,
				   drv_active);
		}
		qla4_8xxx_idc_unlock(ha);
		msleep(1000);
		qla4_8xxx_idc_lock(ha);

		drv_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		drv_active = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	}

	/* Clear RESET OWNER as we are not going to use it any further */
	clear_bit(AF_82XX_RST_OWNER, &ha->flags);

	dev_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	ql4_printk(KERN_INFO, ha, "Device state is 0x%x = %s\n", dev_state,
		   dev_state < MAX_STATES ? qdev_state[dev_state] : "Unknown");

	/* Force to DEV_COLD unless someone else is starting a reset */
	if (dev_state != QLA82XX_DEV_INITIALIZING) {
		ql4_printk(KERN_INFO, ha, "HW State: COLD/RE-INIT\n");
		qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_COLD);
		qla4_8xxx_set_rst_ready(ha);
	}
}

/**
 * qla4_8xxx_need_qsnt_handler - Code to start qsnt
 * @ha: pointer to adapter structure
 **/
void
qla4_8xxx_need_qsnt_handler(struct scsi_qla_host *ha)
{
	qla4_8xxx_idc_lock(ha);
	qla4_8xxx_set_qsnt_ready(ha);
	qla4_8xxx_idc_unlock(ha);
}

/**
 * qla4_8xxx_device_state_handler - Adapter state machine
 * @ha: pointer to host adapter structure.
 *
 * Note: IDC lock must be UNLOCKED upon entry
 **/
int qla4_8xxx_device_state_handler(struct scsi_qla_host *ha)
{
	uint32_t dev_state;
	int rval = QLA_SUCCESS;
	unsigned long dev_init_timeout;

	if (!test_bit(AF_INIT_DONE, &ha->flags)) {
		qla4_8xxx_idc_lock(ha);
		qla4_8xxx_set_drv_active(ha);
		qla4_8xxx_idc_unlock(ha);
	}

	dev_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	DEBUG2(ql4_printk(KERN_INFO, ha, "Device state is 0x%x = %s\n",
			  dev_state, dev_state < MAX_STATES ?
			  qdev_state[dev_state] : "Unknown"));

	/* wait for 30 seconds for device to go ready */
	dev_init_timeout = jiffies + (ha->nx_dev_init_timeout * HZ);

	qla4_8xxx_idc_lock(ha);
	while (1) {

		if (time_after_eq(jiffies, dev_init_timeout)) {
			ql4_printk(KERN_WARNING, ha,
				   "%s: Device Init Failed 0x%x = %s\n",
				   DRIVER_NAME,
				   dev_state, dev_state < MAX_STATES ?
				   qdev_state[dev_state] : "Unknown");
			qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
				QLA82XX_DEV_FAILED);
		}

		dev_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		ql4_printk(KERN_INFO, ha, "Device state is 0x%x = %s\n",
			   dev_state, dev_state < MAX_STATES ?
			   qdev_state[dev_state] : "Unknown");

		/* NOTE: Make sure idc unlocked upon exit of switch statement */
		switch (dev_state) {
		case QLA82XX_DEV_READY:
			goto exit;
		case QLA82XX_DEV_COLD:
			rval = qla4_8xxx_device_bootstrap(ha);
			goto exit;
		case QLA82XX_DEV_INITIALIZING:
			qla4_8xxx_idc_unlock(ha);
			msleep(1000);
			qla4_8xxx_idc_lock(ha);
			break;
		case QLA82XX_DEV_NEED_RESET:
			if (!ql4xdontresethba) {
				qla4_8xxx_need_reset_handler(ha);
				/* Update timeout value after need
				 * reset handler */
				dev_init_timeout = jiffies +
					(ha->nx_dev_init_timeout * HZ);
			} else {
				qla4_8xxx_idc_unlock(ha);
				msleep(1000);
				qla4_8xxx_idc_lock(ha);
			}
			break;
		case QLA82XX_DEV_NEED_QUIESCENT:
			/* idc locked/unlocked in handler */
			qla4_8xxx_need_qsnt_handler(ha);
			break;
		case QLA82XX_DEV_QUIESCENT:
			qla4_8xxx_idc_unlock(ha);
			msleep(1000);
			qla4_8xxx_idc_lock(ha);
			break;
		case QLA82XX_DEV_FAILED:
			qla4_8xxx_idc_unlock(ha);
			qla4xxx_dead_adapter_cleanup(ha);
			rval = QLA_ERROR;
			qla4_8xxx_idc_lock(ha);
			goto exit;
		default:
			qla4_8xxx_idc_unlock(ha);
			qla4xxx_dead_adapter_cleanup(ha);
			rval = QLA_ERROR;
			qla4_8xxx_idc_lock(ha);
			goto exit;
		}
	}
exit:
	qla4_8xxx_idc_unlock(ha);
	return rval;
}

int qla4_8xxx_load_risc(struct scsi_qla_host *ha)
{
	int retval;

	/* clear the interrupt */
	writel(0, &ha->qla4_8xxx_reg->host_int);
	readl(&ha->qla4_8xxx_reg->host_int);

	retval = qla4_8xxx_device_state_handler(ha);

	if (retval == QLA_SUCCESS && !test_bit(AF_INIT_DONE, &ha->flags))
		retval = qla4xxx_request_irqs(ha);

	return retval;
}

/*****************************************************************************/
/* Flash Manipulation Routines                                               */
/*****************************************************************************/

#define OPTROM_BURST_SIZE       0x1000
#define OPTROM_BURST_DWORDS     (OPTROM_BURST_SIZE / 4)

#define FARX_DATA_FLAG	BIT_31
#define FARX_ACCESS_FLASH_CONF	0x7FFD0000
#define FARX_ACCESS_FLASH_DATA	0x7FF00000

static inline uint32_t
flash_conf_addr(struct ql82xx_hw_data *hw, uint32_t faddr)
{
	return hw->flash_conf_off | faddr;
}

static inline uint32_t
flash_data_addr(struct ql82xx_hw_data *hw, uint32_t faddr)
{
	return hw->flash_data_off | faddr;
}

static uint32_t *
qla4_8xxx_read_flash_data(struct scsi_qla_host *ha, uint32_t *dwptr,
    uint32_t faddr, uint32_t length)
{
	uint32_t i;
	uint32_t val;
	int loops = 0;
	while ((qla4_8xxx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		cond_resched();
		loops++;
	}
	if (loops >= 50000) {
		ql4_printk(KERN_WARNING, ha, "ROM lock failed\n");
		return dwptr;
	}

	/* Dword reads to flash. */
	for (i = 0; i < length/4; i++, faddr += 4) {
		if (qla4_8xxx_do_rom_fast_read(ha, faddr, &val)) {
			ql4_printk(KERN_WARNING, ha,
			    "Do ROM fast read failed\n");
			goto done_read;
		}
		dwptr[i] = __constant_cpu_to_le32(val);
	}

done_read:
	qla4_8xxx_rom_unlock(ha);
	return dwptr;
}

/**
 * Address and length are byte address
 **/
static uint8_t *
qla4_8xxx_read_optrom_data(struct scsi_qla_host *ha, uint8_t *buf,
		uint32_t offset, uint32_t length)
{
	qla4_8xxx_read_flash_data(ha, (uint32_t *)buf, offset, length);
	return buf;
}

static int
qla4_8xxx_find_flt_start(struct scsi_qla_host *ha, uint32_t *start)
{
	const char *loc, *locations[] = { "DEF", "PCI" };

	/*
	 * FLT-location structure resides after the last PCI region.
	 */

	/* Begin with sane defaults. */
	loc = locations[0];
	*start = FA_FLASH_LAYOUT_ADDR_82;

	DEBUG2(ql4_printk(KERN_INFO, ha, "FLTL[%s] = 0x%x.\n", loc, *start));
	return QLA_SUCCESS;
}

static void
qla4_8xxx_get_flt_info(struct scsi_qla_host *ha, uint32_t flt_addr)
{
	const char *loc, *locations[] = { "DEF", "FLT" };
	uint16_t *wptr;
	uint16_t cnt, chksum;
	uint32_t start;
	struct qla_flt_header *flt;
	struct qla_flt_region *region;
	struct ql82xx_hw_data *hw = &ha->hw;

	hw->flt_region_flt = flt_addr;
	wptr = (uint16_t *)ha->request_ring;
	flt = (struct qla_flt_header *)ha->request_ring;
	region = (struct qla_flt_region *)&flt[1];
	qla4_8xxx_read_optrom_data(ha, (uint8_t *)ha->request_ring,
			flt_addr << 2, OPTROM_BURST_SIZE);
	if (*wptr == __constant_cpu_to_le16(0xffff))
		goto no_flash_data;
	if (flt->version != __constant_cpu_to_le16(1)) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "Unsupported FLT detected: "
			"version=0x%x length=0x%x checksum=0x%x.\n",
			le16_to_cpu(flt->version), le16_to_cpu(flt->length),
			le16_to_cpu(flt->checksum)));
		goto no_flash_data;
	}

	cnt = (sizeof(struct qla_flt_header) + le16_to_cpu(flt->length)) >> 1;
	for (chksum = 0; cnt; cnt--)
		chksum += le16_to_cpu(*wptr++);
	if (chksum) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "Inconsistent FLT detected: "
			"version=0x%x length=0x%x checksum=0x%x.\n",
			le16_to_cpu(flt->version), le16_to_cpu(flt->length),
			chksum));
		goto no_flash_data;
	}

	loc = locations[1];
	cnt = le16_to_cpu(flt->length) / sizeof(struct qla_flt_region);
	for ( ; cnt; cnt--, region++) {
		/* Store addresses as DWORD offsets. */
		start = le32_to_cpu(region->start) >> 2;

		DEBUG3(ql4_printk(KERN_DEBUG, ha, "FLT[%02x]: start=0x%x "
		    "end=0x%x size=0x%x.\n", le32_to_cpu(region->code), start,
		    le32_to_cpu(region->end) >> 2, le32_to_cpu(region->size)));

		switch (le32_to_cpu(region->code) & 0xff) {
		case FLT_REG_FDT:
			hw->flt_region_fdt = start;
			break;
		case FLT_REG_BOOT_CODE_82:
			hw->flt_region_boot = start;
			break;
		case FLT_REG_FW_82:
		case FLT_REG_FW_82_1:
			hw->flt_region_fw = start;
			break;
		case FLT_REG_BOOTLOAD_82:
			hw->flt_region_bootload = start;
			break;
		case FLT_REG_ISCSI_PARAM:
			hw->flt_iscsi_param =  start;
			break;
		case FLT_REG_ISCSI_CHAP:
			hw->flt_region_chap =  start;
			hw->flt_chap_size =  le32_to_cpu(region->size);
			break;
		}
	}
	goto done;

no_flash_data:
	/* Use hardcoded defaults. */
	loc = locations[0];

	hw->flt_region_fdt      = FA_FLASH_DESCR_ADDR_82;
	hw->flt_region_boot     = FA_BOOT_CODE_ADDR_82;
	hw->flt_region_bootload = FA_BOOT_LOAD_ADDR_82;
	hw->flt_region_fw       = FA_RISC_CODE_ADDR_82;
	hw->flt_region_chap	= FA_FLASH_ISCSI_CHAP;
	hw->flt_chap_size	= FA_FLASH_CHAP_SIZE;

done:
	DEBUG2(ql4_printk(KERN_INFO, ha, "FLT[%s]: flt=0x%x fdt=0x%x "
	    "boot=0x%x bootload=0x%x fw=0x%x\n", loc, hw->flt_region_flt,
	    hw->flt_region_fdt,	hw->flt_region_boot, hw->flt_region_bootload,
	    hw->flt_region_fw));
}

static void
qla4_8xxx_get_fdt_info(struct scsi_qla_host *ha)
{
#define FLASH_BLK_SIZE_4K       0x1000
#define FLASH_BLK_SIZE_32K      0x8000
#define FLASH_BLK_SIZE_64K      0x10000
	const char *loc, *locations[] = { "MID", "FDT" };
	uint16_t cnt, chksum;
	uint16_t *wptr;
	struct qla_fdt_layout *fdt;
	uint16_t mid = 0;
	uint16_t fid = 0;
	struct ql82xx_hw_data *hw = &ha->hw;

	hw->flash_conf_off = FARX_ACCESS_FLASH_CONF;
	hw->flash_data_off = FARX_ACCESS_FLASH_DATA;

	wptr = (uint16_t *)ha->request_ring;
	fdt = (struct qla_fdt_layout *)ha->request_ring;
	qla4_8xxx_read_optrom_data(ha, (uint8_t *)ha->request_ring,
	    hw->flt_region_fdt << 2, OPTROM_BURST_SIZE);

	if (*wptr == __constant_cpu_to_le16(0xffff))
		goto no_flash_data;

	if (fdt->sig[0] != 'Q' || fdt->sig[1] != 'L' || fdt->sig[2] != 'I' ||
	    fdt->sig[3] != 'D')
		goto no_flash_data;

	for (cnt = 0, chksum = 0; cnt < sizeof(struct qla_fdt_layout) >> 1;
	    cnt++)
		chksum += le16_to_cpu(*wptr++);

	if (chksum) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "Inconsistent FDT detected: "
		    "checksum=0x%x id=%c version=0x%x.\n", chksum, fdt->sig[0],
		    le16_to_cpu(fdt->version)));
		goto no_flash_data;
	}

	loc = locations[1];
	mid = le16_to_cpu(fdt->man_id);
	fid = le16_to_cpu(fdt->id);
	hw->fdt_wrt_disable = fdt->wrt_disable_bits;
	hw->fdt_erase_cmd = flash_conf_addr(hw, 0x0300 | fdt->erase_cmd);
	hw->fdt_block_size = le32_to_cpu(fdt->block_size);

	if (fdt->unprotect_sec_cmd) {
		hw->fdt_unprotect_sec_cmd = flash_conf_addr(hw, 0x0300 |
		    fdt->unprotect_sec_cmd);
		hw->fdt_protect_sec_cmd = fdt->protect_sec_cmd ?
		    flash_conf_addr(hw, 0x0300 | fdt->protect_sec_cmd) :
		    flash_conf_addr(hw, 0x0336);
	}
	goto done;

no_flash_data:
	loc = locations[0];
	hw->fdt_block_size = FLASH_BLK_SIZE_64K;
done:
	DEBUG2(ql4_printk(KERN_INFO, ha, "FDT[%s]: (0x%x/0x%x) erase=0x%x "
		"pro=%x upro=%x wrtd=0x%x blk=0x%x.\n", loc, mid, fid,
		hw->fdt_erase_cmd, hw->fdt_protect_sec_cmd,
		hw->fdt_unprotect_sec_cmd, hw->fdt_wrt_disable,
		hw->fdt_block_size));
}

static void
qla4_8xxx_get_idc_param(struct scsi_qla_host *ha)
{
#define QLA82XX_IDC_PARAM_ADDR      0x003e885c
	uint32_t *wptr;

	if (!is_qla8022(ha))
		return;
	wptr = (uint32_t *)ha->request_ring;
	qla4_8xxx_read_optrom_data(ha, (uint8_t *)ha->request_ring,
			QLA82XX_IDC_PARAM_ADDR , 8);

	if (*wptr == __constant_cpu_to_le32(0xffffffff)) {
		ha->nx_dev_init_timeout = ROM_DEV_INIT_TIMEOUT;
		ha->nx_reset_timeout = ROM_DRV_RESET_ACK_TIMEOUT;
	} else {
		ha->nx_dev_init_timeout = le32_to_cpu(*wptr++);
		ha->nx_reset_timeout = le32_to_cpu(*wptr);
	}

	DEBUG2(ql4_printk(KERN_DEBUG, ha,
		"ha->nx_dev_init_timeout = %d\n", ha->nx_dev_init_timeout));
	DEBUG2(ql4_printk(KERN_DEBUG, ha,
		"ha->nx_reset_timeout = %d\n", ha->nx_reset_timeout));
	return;
}

int
qla4_8xxx_get_flash_info(struct scsi_qla_host *ha)
{
	int ret;
	uint32_t flt_addr;

	ret = qla4_8xxx_find_flt_start(ha, &flt_addr);
	if (ret != QLA_SUCCESS)
		return ret;

	qla4_8xxx_get_flt_info(ha, flt_addr);
	qla4_8xxx_get_fdt_info(ha);
	qla4_8xxx_get_idc_param(ha);

	return QLA_SUCCESS;
}

/**
 * qla4_8xxx_stop_firmware - stops firmware on specified adapter instance
 * @ha: pointer to host adapter structure.
 *
 * Remarks:
 * For iSCSI, throws away all I/O and AENs into bit bucket, so they will
 * not be available after successful return.  Driver must cleanup potential
 * outstanding I/O's after calling this funcion.
 **/
int
qla4_8xxx_stop_firmware(struct scsi_qla_host *ha)
{
	int status;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_STOP_FW;
	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1,
	    &mbox_cmd[0], &mbox_sts[0]);

	DEBUG2(printk("scsi%ld: %s: status = %d\n", ha->host_no,
	    __func__, status));
	return status;
}

/**
 * qla4_8xxx_isp_reset - Resets ISP and aborts all outstanding commands.
 * @ha: pointer to host adapter structure.
 **/
int
qla4_8xxx_isp_reset(struct scsi_qla_host *ha)
{
	int rval;
	uint32_t dev_state;

	qla4_8xxx_idc_lock(ha);
	dev_state = qla4_8xxx_rd_32(ha, QLA82XX_CRB_DEV_STATE);

	if (dev_state == QLA82XX_DEV_READY) {
		ql4_printk(KERN_INFO, ha, "HW State: NEED RESET\n");
		qla4_8xxx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
		    QLA82XX_DEV_NEED_RESET);
		set_bit(AF_82XX_RST_OWNER, &ha->flags);
	} else
		ql4_printk(KERN_INFO, ha, "HW State: DEVICE INITIALIZING\n");

	qla4_8xxx_idc_unlock(ha);

	rval = qla4_8xxx_device_state_handler(ha);

	qla4_8xxx_idc_lock(ha);
	qla4_8xxx_clear_rst_ready(ha);
	qla4_8xxx_idc_unlock(ha);

	if (rval == QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "Clearing AF_RECOVERY in qla4_8xxx_isp_reset\n");
		clear_bit(AF_FW_RECOVERY, &ha->flags);
	}

	return rval;
}

/**
 * qla4_8xxx_get_sys_info - get adapter MAC address(es) and serial number
 * @ha: pointer to host adapter structure.
 *
 **/
int qla4_8xxx_get_sys_info(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	struct mbx_sys_info *sys_info;
	dma_addr_t sys_info_dma;
	int status = QLA_ERROR;

	sys_info = dma_alloc_coherent(&ha->pdev->dev, sizeof(*sys_info),
				      &sys_info_dma, GFP_KERNEL);
	if (sys_info == NULL) {
		DEBUG2(printk("scsi%ld: %s: Unable to allocate dma buffer.\n",
		    ha->host_no, __func__));
		return status;
	}

	memset(sys_info, 0, sizeof(*sys_info));
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_SYS_INFO;
	mbox_cmd[1] = LSDW(sys_info_dma);
	mbox_cmd[2] = MSDW(sys_info_dma);
	mbox_cmd[4] = sizeof(*sys_info);

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 6, &mbox_cmd[0],
	    &mbox_sts[0]) != QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: GET_SYS_INFO failed\n",
		    ha->host_no, __func__));
		goto exit_validate_mac82;
	}

	/* Make sure we receive the minimum required data to cache internally */
	if (mbox_sts[4] < offsetof(struct mbx_sys_info, reserved)) {
		DEBUG2(printk("scsi%ld: %s: GET_SYS_INFO data receive"
		    " error (%x)\n", ha->host_no, __func__, mbox_sts[4]));
		goto exit_validate_mac82;

	}

	/* Save M.A.C. address & serial_number */
	ha->port_num = sys_info->port_num;
	memcpy(ha->my_mac, &sys_info->mac_addr[0],
	    min(sizeof(ha->my_mac), sizeof(sys_info->mac_addr)));
	memcpy(ha->serial_number, &sys_info->serial_number,
	    min(sizeof(ha->serial_number), sizeof(sys_info->serial_number)));
	memcpy(ha->model_name, &sys_info->board_id_str,
	       min(sizeof(ha->model_name), sizeof(sys_info->board_id_str)));
	ha->phy_port_cnt = sys_info->phys_port_cnt;
	ha->phy_port_num = sys_info->port_num;
	ha->iscsi_pci_func_cnt = sys_info->iscsi_pci_func_cnt;

	DEBUG2(printk("scsi%ld: %s: "
	    "mac %02x:%02x:%02x:%02x:%02x:%02x "
	    "serial %s\n", ha->host_no, __func__,
	    ha->my_mac[0], ha->my_mac[1], ha->my_mac[2],
	    ha->my_mac[3], ha->my_mac[4], ha->my_mac[5],
	    ha->serial_number));

	status = QLA_SUCCESS;

exit_validate_mac82:
	dma_free_coherent(&ha->pdev->dev, sizeof(*sys_info), sys_info,
			  sys_info_dma);
	return status;
}

/* Interrupt handling helpers. */

static int
qla4_8xxx_mbx_intr_enable(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s\n", __func__));

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_ENABLE_INTRS;
	mbox_cmd[1] = INTR_ENABLE;
	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0],
		&mbox_sts[0]) != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "%s: MBOX_CMD_ENABLE_INTRS failed (0x%04x)\n",
		    __func__, mbox_sts[0]));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

static int
qla4_8xxx_mbx_intr_disable(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s\n", __func__));

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_ENABLE_INTRS;
	mbox_cmd[1] = INTR_DISABLE;
	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0],
	    &mbox_sts[0]) != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
			"%s: MBOX_CMD_ENABLE_INTRS failed (0x%04x)\n",
			__func__, mbox_sts[0]));
		return QLA_ERROR;
	}

	return QLA_SUCCESS;
}

void
qla4_8xxx_enable_intrs(struct scsi_qla_host *ha)
{
	qla4_8xxx_mbx_intr_enable(ha);

	spin_lock_irq(&ha->hardware_lock);
	/* BIT 10 - reset */
	qla4_8xxx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);
	spin_unlock_irq(&ha->hardware_lock);
	set_bit(AF_INTERRUPTS_ON, &ha->flags);
}

void
qla4_8xxx_disable_intrs(struct scsi_qla_host *ha)
{
	if (test_and_clear_bit(AF_INTERRUPTS_ON, &ha->flags))
		qla4_8xxx_mbx_intr_disable(ha);

	spin_lock_irq(&ha->hardware_lock);
	/* BIT 10 - set */
	qla4_8xxx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0x0400);
	spin_unlock_irq(&ha->hardware_lock);
}

struct ql4_init_msix_entry {
	uint16_t entry;
	uint16_t index;
	const char *name;
	irq_handler_t handler;
};

static struct ql4_init_msix_entry qla4_8xxx_msix_entries[QLA_MSIX_ENTRIES] = {
	{ QLA_MSIX_DEFAULT, QLA_MIDX_DEFAULT,
	    "qla4xxx (default)",
	    (irq_handler_t)qla4_8xxx_default_intr_handler },
	{ QLA_MSIX_RSP_Q, QLA_MIDX_RSP_Q,
	    "qla4xxx (rsp_q)", (irq_handler_t)qla4_8xxx_msix_rsp_q },
};

void
qla4_8xxx_disable_msix(struct scsi_qla_host *ha)
{
	int i;
	struct ql4_msix_entry *qentry;

	for (i = 0; i < QLA_MSIX_ENTRIES; i++) {
		qentry = &ha->msix_entries[qla4_8xxx_msix_entries[i].index];
		if (qentry->have_irq) {
			free_irq(qentry->msix_vector, ha);
			DEBUG2(ql4_printk(KERN_INFO, ha, "%s: %s\n",
				__func__, qla4_8xxx_msix_entries[i].name));
		}
	}
	pci_disable_msix(ha->pdev);
	clear_bit(AF_MSIX_ENABLED, &ha->flags);
}

int
qla4_8xxx_enable_msix(struct scsi_qla_host *ha)
{
	int i, ret;
	struct msix_entry entries[QLA_MSIX_ENTRIES];
	struct ql4_msix_entry *qentry;

	for (i = 0; i < QLA_MSIX_ENTRIES; i++)
		entries[i].entry = qla4_8xxx_msix_entries[i].entry;

	ret = pci_enable_msix(ha->pdev, entries, ARRAY_SIZE(entries));
	if (ret) {
		ql4_printk(KERN_WARNING, ha,
		    "MSI-X: Failed to enable support -- %d/%d\n",
		    QLA_MSIX_ENTRIES, ret);
		goto msix_out;
	}
	set_bit(AF_MSIX_ENABLED, &ha->flags);

	for (i = 0; i < QLA_MSIX_ENTRIES; i++) {
		qentry = &ha->msix_entries[qla4_8xxx_msix_entries[i].index];
		qentry->msix_vector = entries[i].vector;
		qentry->msix_entry = entries[i].entry;
		qentry->have_irq = 0;
		ret = request_irq(qentry->msix_vector,
		    qla4_8xxx_msix_entries[i].handler, 0,
		    qla4_8xxx_msix_entries[i].name, ha);
		if (ret) {
			ql4_printk(KERN_WARNING, ha,
			    "MSI-X: Unable to register handler -- %x/%d.\n",
			    qla4_8xxx_msix_entries[i].index, ret);
			qla4_8xxx_disable_msix(ha);
			goto msix_out;
		}
		qentry->have_irq = 1;
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: %s\n",
			__func__, qla4_8xxx_msix_entries[i].name));
	}
msix_out:
	return ret;
}
