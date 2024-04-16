// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */
#include "qla_def.h"
#include <linux/delay.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/pci.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <scsi/scsi_tcq.h>

#define MASK(n)			((1ULL<<(n))-1)
#define MN_WIN(addr) (((addr & 0x1fc0000) >> 1) | \
	((addr >> 25) & 0x3ff))
#define OCM_WIN(addr) (((addr & 0x1ff0000) >> 1) | \
	((addr >> 25) & 0x3ff))
#define MS_WIN(addr) (addr & 0x0ffc0000)
#define QLA82XX_PCI_MN_2M   (0)
#define QLA82XX_PCI_MS_2M   (0x80000)
#define QLA82XX_PCI_OCM0_2M (0xc0000)
#define VALID_OCM_ADDR(addr) (((addr) & 0x3f800) != 0x3f800)
#define GET_MEM_OFFS_2M(addr) (addr & MASK(18))
#define BLOCK_PROTECT_BITS 0x0F

/* CRB window related */
#define CRB_BLK(off)	((off >> 20) & 0x3f)
#define CRB_SUBBLK(off)	((off >> 16) & 0xf)
#define CRB_WINDOW_2M	(0x130060)
#define QLA82XX_PCI_CAMQM_2M_END	(0x04800800UL)
#define CRB_HI(off)	((qla82xx_crb_hub_agt[CRB_BLK(off)] << 20) | \
			((off) & 0xf0000))
#define QLA82XX_PCI_CAMQM_2M_BASE	(0x000ff800UL)
#define CRB_INDIRECT_2M	(0x1e0000UL)

#define MAX_CRB_XFORM 60
static unsigned long crb_addr_xform[MAX_CRB_XFORM];
static int qla82xx_crb_table_initialized;

#define qla82xx_crb_addr_transform(name) \
	(crb_addr_xform[QLA82XX_HW_PX_MAP_CRB_##name] = \
	QLA82XX_HW_CRB_HUB_AGT_ADR_##name << 20)

const int MD_MIU_TEST_AGT_RDDATA[] = {
	0x410000A8, 0x410000AC,
	0x410000B8, 0x410000BC
};

static void qla82xx_crb_addr_transform_setup(void)
{
	qla82xx_crb_addr_transform(XDMA);
	qla82xx_crb_addr_transform(TIMR);
	qla82xx_crb_addr_transform(SRE);
	qla82xx_crb_addr_transform(SQN3);
	qla82xx_crb_addr_transform(SQN2);
	qla82xx_crb_addr_transform(SQN1);
	qla82xx_crb_addr_transform(SQN0);
	qla82xx_crb_addr_transform(SQS3);
	qla82xx_crb_addr_transform(SQS2);
	qla82xx_crb_addr_transform(SQS1);
	qla82xx_crb_addr_transform(SQS0);
	qla82xx_crb_addr_transform(RPMX7);
	qla82xx_crb_addr_transform(RPMX6);
	qla82xx_crb_addr_transform(RPMX5);
	qla82xx_crb_addr_transform(RPMX4);
	qla82xx_crb_addr_transform(RPMX3);
	qla82xx_crb_addr_transform(RPMX2);
	qla82xx_crb_addr_transform(RPMX1);
	qla82xx_crb_addr_transform(RPMX0);
	qla82xx_crb_addr_transform(ROMUSB);
	qla82xx_crb_addr_transform(SN);
	qla82xx_crb_addr_transform(QMN);
	qla82xx_crb_addr_transform(QMS);
	qla82xx_crb_addr_transform(PGNI);
	qla82xx_crb_addr_transform(PGND);
	qla82xx_crb_addr_transform(PGN3);
	qla82xx_crb_addr_transform(PGN2);
	qla82xx_crb_addr_transform(PGN1);
	qla82xx_crb_addr_transform(PGN0);
	qla82xx_crb_addr_transform(PGSI);
	qla82xx_crb_addr_transform(PGSD);
	qla82xx_crb_addr_transform(PGS3);
	qla82xx_crb_addr_transform(PGS2);
	qla82xx_crb_addr_transform(PGS1);
	qla82xx_crb_addr_transform(PGS0);
	qla82xx_crb_addr_transform(PS);
	qla82xx_crb_addr_transform(PH);
	qla82xx_crb_addr_transform(NIU);
	qla82xx_crb_addr_transform(I2Q);
	qla82xx_crb_addr_transform(EG);
	qla82xx_crb_addr_transform(MN);
	qla82xx_crb_addr_transform(MS);
	qla82xx_crb_addr_transform(CAS2);
	qla82xx_crb_addr_transform(CAS1);
	qla82xx_crb_addr_transform(CAS0);
	qla82xx_crb_addr_transform(CAM);
	qla82xx_crb_addr_transform(C2C1);
	qla82xx_crb_addr_transform(C2C0);
	qla82xx_crb_addr_transform(SMB);
	qla82xx_crb_addr_transform(OCM0);
	/*
	 * Used only in P3 just define it for P2 also.
	 */
	qla82xx_crb_addr_transform(I2C0);

	qla82xx_crb_table_initialized = 1;
}

static struct crb_128M_2M_block_map crb_128M_2M_map[64] = {
	{{{0, 0,         0,         0} } },
	{{{1, 0x0100000, 0x0102000, 0x120000},
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
	{0, 0x0000000, 0x0000000, 0x000000} } } ,
	{{{1, 0x0200000, 0x0210000, 0x180000} } },
	{{{0, 0,         0,         0} } },
	{{{1, 0x0400000, 0x0401000, 0x169000} } },
	{{{1, 0x0500000, 0x0510000, 0x140000} } },
	{{{1, 0x0600000, 0x0610000, 0x1c0000} } },
	{{{1, 0x0700000, 0x0704000, 0x1b8000} } },
	{{{1, 0x0800000, 0x0802000, 0x170000},
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
	{{{1, 0x0900000, 0x0902000, 0x174000},
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
	{{{0, 0x0a00000, 0x0a02000, 0x178000},
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
	{{{0, 0x0b00000, 0x0b02000, 0x17c000},
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
	{{{1, 0x0c00000, 0x0c04000, 0x1d4000} } },
	{{{1, 0x0d00000, 0x0d04000, 0x1a4000} } },
	{{{1, 0x0e00000, 0x0e04000, 0x1a0000} } },
	{{{1, 0x0f00000, 0x0f01000, 0x164000} } },
	{{{0, 0x1000000, 0x1004000, 0x1a8000} } },
	{{{1, 0x1100000, 0x1101000, 0x160000} } },
	{{{1, 0x1200000, 0x1201000, 0x161000} } },
	{{{1, 0x1300000, 0x1301000, 0x162000} } },
	{{{1, 0x1400000, 0x1401000, 0x163000} } },
	{{{1, 0x1500000, 0x1501000, 0x165000} } },
	{{{1, 0x1600000, 0x1601000, 0x166000} } },
	{{{0, 0,         0,         0} } },
	{{{0, 0,         0,         0} } },
	{{{0, 0,         0,         0} } },
	{{{0, 0,         0,         0} } },
	{{{0, 0,         0,         0} } },
	{{{0, 0,         0,         0} } },
	{{{1, 0x1d00000, 0x1d10000, 0x190000} } },
	{{{1, 0x1e00000, 0x1e01000, 0x16a000} } },
	{{{1, 0x1f00000, 0x1f10000, 0x150000} } },
	{{{0} } },
	{{{1, 0x2100000, 0x2102000, 0x120000},
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
	{{{1, 0x2200000, 0x2204000, 0x1b0000} } },
	{{{0} } },
	{{{0} } },
	{{{0} } },
	{{{0} } },
	{{{0} } },
	{{{1, 0x2800000, 0x2804000, 0x1a4000} } },
	{{{1, 0x2900000, 0x2901000, 0x16b000} } },
	{{{1, 0x2a00000, 0x2a00400, 0x1ac400} } },
	{{{1, 0x2b00000, 0x2b00400, 0x1ac800} } },
	{{{1, 0x2c00000, 0x2c00400, 0x1acc00} } },
	{{{1, 0x2d00000, 0x2d00400, 0x1ad000} } },
	{{{1, 0x2e00000, 0x2e00400, 0x1ad400} } },
	{{{1, 0x2f00000, 0x2f00400, 0x1ad800} } },
	{{{1, 0x3000000, 0x3000400, 0x1adc00} } },
	{{{0, 0x3100000, 0x3104000, 0x1a8000} } },
	{{{1, 0x3200000, 0x3204000, 0x1d4000} } },
	{{{1, 0x3300000, 0x3304000, 0x1a0000} } },
	{{{0} } },
	{{{1, 0x3500000, 0x3500400, 0x1ac000} } },
	{{{1, 0x3600000, 0x3600400, 0x1ae000} } },
	{{{1, 0x3700000, 0x3700400, 0x1ae400} } },
	{{{1, 0x3800000, 0x3804000, 0x1d0000} } },
	{{{1, 0x3900000, 0x3904000, 0x1b4000} } },
	{{{1, 0x3a00000, 0x3a04000, 0x1d8000} } },
	{{{0} } },
	{{{0} } },
	{{{1, 0x3d00000, 0x3d04000, 0x1dc000} } },
	{{{1, 0x3e00000, 0x3e01000, 0x167000} } },
	{{{1, 0x3f00000, 0x3f01000, 0x168000} } }
};

/*
 * top 12 bits of crb internal address (hub, agent)
 */
static unsigned qla82xx_crb_hub_agt[64] = {
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
static const char *const q_dev_state[] = {
	[QLA8XXX_DEV_UNKNOWN]		= "Unknown",
	[QLA8XXX_DEV_COLD]		= "Cold/Re-init",
	[QLA8XXX_DEV_INITIALIZING]	= "Initializing",
	[QLA8XXX_DEV_READY]		= "Ready",
	[QLA8XXX_DEV_NEED_RESET]	= "Need Reset",
	[QLA8XXX_DEV_NEED_QUIESCENT]	= "Need Quiescent",
	[QLA8XXX_DEV_FAILED]		= "Failed",
	[QLA8XXX_DEV_QUIESCENT]		= "Quiescent",
};

const char *qdev_state(uint32_t dev_state)
{
	return (dev_state < MAX_STATES) ? q_dev_state[dev_state] : "Unknown";
}

/*
 * In: 'off_in' is offset from CRB space in 128M pci map
 * Out: 'off_out' is 2M pci map addr
 * side effect: lock crb window
 */
static void
qla82xx_pci_set_crbwindow_2M(struct qla_hw_data *ha, ulong off_in,
			     void __iomem **off_out)
{
	u32 win_read;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	ha->crb_win = CRB_HI(off_in);
	writel(ha->crb_win, CRB_WINDOW_2M + ha->nx_pcibase);

	/* Read back value to make sure write has gone through before trying
	 * to use it.
	 */
	win_read = rd_reg_dword(CRB_WINDOW_2M + ha->nx_pcibase);
	if (win_read != ha->crb_win) {
		ql_dbg(ql_dbg_p3p, vha, 0xb000,
		    "%s: Written crbwin (0x%x) "
		    "!= Read crbwin (0x%x), off=0x%lx.\n",
		    __func__, ha->crb_win, win_read, off_in);
	}
	*off_out = (off_in & MASK(16)) + CRB_INDIRECT_2M + ha->nx_pcibase;
}

static int
qla82xx_pci_get_crb_addr_2M(struct qla_hw_data *ha, ulong off_in,
			    void __iomem **off_out)
{
	struct crb_128M_2M_sub_block_map *m;

	if (off_in >= QLA82XX_CRB_MAX)
		return -1;

	if (off_in >= QLA82XX_PCI_CAMQM && off_in < QLA82XX_PCI_CAMQM_2M_END) {
		*off_out = (off_in - QLA82XX_PCI_CAMQM) +
		    QLA82XX_PCI_CAMQM_2M_BASE + ha->nx_pcibase;
		return 0;
	}

	if (off_in < QLA82XX_PCI_CRBSPACE)
		return -1;

	off_in -= QLA82XX_PCI_CRBSPACE;

	/* Try direct map */
	m = &crb_128M_2M_map[CRB_BLK(off_in)].sub_block[CRB_SUBBLK(off_in)];

	if (m->valid && (m->start_128M <= off_in) && (m->end_128M > off_in)) {
		*off_out = off_in + m->start_2M - m->start_128M + ha->nx_pcibase;
		return 0;
	}
	/* Not in direct map, use crb window */
	*off_out = (void __iomem *)off_in;
	return 1;
}

#define CRB_WIN_LOCK_TIMEOUT 100000000
static int qla82xx_crb_win_lock(struct qla_hw_data *ha)
{
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore3 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_LOCK));
		if (done == 1)
			break;
		if (timeout >= CRB_WIN_LOCK_TIMEOUT)
			return -1;
		timeout++;
	}
	qla82xx_wr_32(ha, QLA82XX_CRB_WIN_LOCK_ID, ha->portnum);
	return 0;
}

int
qla82xx_wr_32(struct qla_hw_data *ha, ulong off_in, u32 data)
{
	void __iomem *off;
	unsigned long flags = 0;
	int rv;

	rv = qla82xx_pci_get_crb_addr_2M(ha, off_in, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
#ifndef __CHECKER__
		write_lock_irqsave(&ha->hw_lock, flags);
#endif
		qla82xx_crb_win_lock(ha);
		qla82xx_pci_set_crbwindow_2M(ha, off_in, &off);
	}

	writel(data, (void __iomem *)off);

	if (rv == 1) {
		qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_UNLOCK));
#ifndef __CHECKER__
		write_unlock_irqrestore(&ha->hw_lock, flags);
#endif
	}
	return 0;
}

int
qla82xx_rd_32(struct qla_hw_data *ha, ulong off_in)
{
	void __iomem *off;
	unsigned long flags = 0;
	int rv;
	u32 data;

	rv = qla82xx_pci_get_crb_addr_2M(ha, off_in, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
#ifndef __CHECKER__
		write_lock_irqsave(&ha->hw_lock, flags);
#endif
		qla82xx_crb_win_lock(ha);
		qla82xx_pci_set_crbwindow_2M(ha, off_in, &off);
	}
	data = rd_reg_dword(off);

	if (rv == 1) {
		qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_UNLOCK));
#ifndef __CHECKER__
		write_unlock_irqrestore(&ha->hw_lock, flags);
#endif
	}
	return data;
}

/*
 * Context: task, might sleep
 */
int qla82xx_idc_lock(struct qla_hw_data *ha)
{
	const int delay_ms = 100, timeout_ms = 2000;
	int done, total = 0;

	might_sleep();

	while (true) {
		/* acquire semaphore5 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_LOCK));
		if (done == 1)
			break;
		if (WARN_ON_ONCE(total >= timeout_ms))
			return -1;

		total += delay_ms;
		msleep(delay_ms);
	}

	return 0;
}

void qla82xx_idc_unlock(struct qla_hw_data *ha)
{
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_UNLOCK));
}

/*
 * check memory access boundary.
 * used by test agent. support ddr access only for now
 */
static unsigned long
qla82xx_pci_mem_bound_check(struct qla_hw_data *ha,
	unsigned long long addr, int size)
{
	if (!addr_in_range(addr, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX) ||
		!addr_in_range(addr + size - 1, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX) ||
		((size != 1) && (size != 2) && (size != 4) && (size != 8)))
			return 0;
	else
		return 1;
}

static int qla82xx_pci_set_window_warning_count;

static unsigned long
qla82xx_pci_set_window(struct qla_hw_data *ha, unsigned long long addr)
{
	int window;
	u32 win_read;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	if (addr_in_range(addr, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		window = MN_WIN(addr);
		ha->ddr_mn_window = window;
		qla82xx_wr_32(ha,
			ha->mn_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha,
			ha->mn_win_crb | QLA82XX_PCI_CRBSPACE);
		if ((win_read << 17) != window) {
			ql_dbg(ql_dbg_p3p, vha, 0xb003,
			    "%s: Written MNwin (0x%x) != Read MNwin (0x%x).\n",
			    __func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_DDR_NET;
	} else if (addr_in_range(addr, QLA82XX_ADDR_OCM0,
		QLA82XX_ADDR_OCM0_MAX)) {
		unsigned int temp1;

		if ((addr & 0x00ff800) == 0xff800) {
			ql_log(ql_log_warn, vha, 0xb004,
			    "%s: QM access not handled.\n", __func__);
			addr = -1UL;
		}
		window = OCM_WIN(addr);
		ha->ddr_mn_window = window;
		qla82xx_wr_32(ha,
			ha->mn_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha,
			ha->mn_win_crb | QLA82XX_PCI_CRBSPACE);
		temp1 = ((window & 0x1FF) << 7) |
		    ((window & 0x0FFFE0000) >> 17);
		if (win_read != temp1) {
			ql_log(ql_log_warn, vha, 0xb005,
			    "%s: Written OCMwin (0x%x) != Read OCMwin (0x%x).\n",
			    __func__, temp1, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_OCM0_2M;

	} else if (addr_in_range(addr, QLA82XX_ADDR_QDR_NET,
		QLA82XX_P3_ADDR_QDR_NET_MAX)) {
		/* QDR network side */
		window = MS_WIN(addr);
		ha->qdr_sn_window = window;
		qla82xx_wr_32(ha,
			ha->ms_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha,
			ha->ms_win_crb | QLA82XX_PCI_CRBSPACE);
		if (win_read != window) {
			ql_log(ql_log_warn, vha, 0xb006,
			    "%s: Written MSwin (0x%x) != Read MSwin (0x%x).\n",
			    __func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_QDR_NET;
	} else {
		/*
		 * peg gdb frequently accesses memory that doesn't exist,
		 * this limits the chit chat so debugging isn't slowed down.
		 */
		if ((qla82xx_pci_set_window_warning_count++ < 8) ||
		    (qla82xx_pci_set_window_warning_count%64 == 0)) {
			ql_log(ql_log_warn, vha, 0xb007,
			    "%s: Warning:%s Unknown address range!.\n",
			    __func__, QLA2XXX_DRIVER_NAME);
		}
		addr = -1UL;
	}
	return addr;
}

/* check if address is in the same windows as the previous access */
static int qla82xx_pci_is_same_window(struct qla_hw_data *ha,
	unsigned long long addr)
{
	int			window;
	unsigned long long	qdr_max;

	qdr_max = QLA82XX_P3_ADDR_QDR_NET_MAX;

	/* DDR network side */
	if (addr_in_range(addr, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX))
		BUG();
	else if (addr_in_range(addr, QLA82XX_ADDR_OCM0,
		QLA82XX_ADDR_OCM0_MAX))
		return 1;
	else if (addr_in_range(addr, QLA82XX_ADDR_OCM1,
		QLA82XX_ADDR_OCM1_MAX))
		return 1;
	else if (addr_in_range(addr, QLA82XX_ADDR_QDR_NET, qdr_max)) {
		/* QDR network side */
		window = ((addr - QLA82XX_ADDR_QDR_NET) >> 22) & 0x3f;
		if (ha->qdr_sn_window == window)
			return 1;
	}
	return 0;
}

static int qla82xx_pci_mem_read_direct(struct qla_hw_data *ha,
	u64 off, void *data, int size)
{
	unsigned long   flags;
	void __iomem *addr = NULL;
	int             ret = 0;
	u64             start;
	uint8_t __iomem  *mem_ptr = NULL;
	unsigned long   mem_base;
	unsigned long   mem_page;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla82xx_pci_set_window(ha, off);
	if ((start == -1UL) ||
		(qla82xx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		ql_log(ql_log_fatal, vha, 0xb008,
		    "%s out of bound pci memory "
		    "access, offset is 0x%llx.\n",
		    QLA2XXX_DRIVER_NAME, off);
		return -1;
	}

	write_unlock_irqrestore(&ha->hw_lock, flags);
	mem_base = pci_resource_start(ha->pdev, 0);
	mem_page = start & PAGE_MASK;
	/* Map two pages whenever user tries to access addresses in two
	* consecutive pages.
	*/
	if (mem_page != ((start + size - 1) & PAGE_MASK))
		mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE * 2);
	else
		mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);
	if (mem_ptr == NULL) {
		*(u8  *)data = 0;
		return -1;
	}
	addr = mem_ptr;
	addr += start & (PAGE_SIZE - 1);
	write_lock_irqsave(&ha->hw_lock, flags);

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
qla82xx_pci_mem_write_direct(struct qla_hw_data *ha,
	u64 off, void *data, int size)
{
	unsigned long   flags;
	void  __iomem *addr = NULL;
	int             ret = 0;
	u64             start;
	uint8_t __iomem *mem_ptr = NULL;
	unsigned long   mem_base;
	unsigned long   mem_page;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla82xx_pci_set_window(ha, off);
	if ((start == -1UL) ||
		(qla82xx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		ql_log(ql_log_fatal, vha, 0xb009,
		    "%s out of bound memory "
		    "access, offset is 0x%llx.\n",
		    QLA2XXX_DRIVER_NAME, off);
		return -1;
	}

	write_unlock_irqrestore(&ha->hw_lock, flags);
	mem_base = pci_resource_start(ha->pdev, 0);
	mem_page = start & PAGE_MASK;
	/* Map two pages whenever user tries to access addresses in two
	 * consecutive pages.
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

	switch (size) {
	case 1:
		writeb(*(u8  *)data, addr);
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
qla82xx_decode_crb_addr(unsigned long addr)
{
	int i;
	unsigned long base_addr, offset, pci_base;

	if (!qla82xx_crb_table_initialized)
		qla82xx_crb_addr_transform_setup();

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
	return pci_base + offset;
}

static long rom_max_timeout = 100;
static long qla82xx_rom_lock_timeout = 100;

static int
qla82xx_rom_lock(struct qla_hw_data *ha)
{
	int done = 0, timeout = 0;
	uint32_t lock_owner = 0;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	while (!done) {
		/* acquire semaphore2 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_LOCK));
		if (done == 1)
			break;
		if (timeout >= qla82xx_rom_lock_timeout) {
			lock_owner = qla82xx_rd_32(ha, QLA82XX_ROM_LOCK_ID);
			ql_dbg(ql_dbg_p3p, vha, 0xb157,
			    "%s: Simultaneous flash access by following ports, active port = %d: accessing port = %d",
			    __func__, ha->portnum, lock_owner);
			return -1;
		}
		timeout++;
	}
	qla82xx_wr_32(ha, QLA82XX_ROM_LOCK_ID, ha->portnum);
	return 0;
}

static void
qla82xx_rom_unlock(struct qla_hw_data *ha)
{
	qla82xx_wr_32(ha, QLA82XX_ROM_LOCK_ID, 0xffffffff);
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_UNLOCK));
}

static int
qla82xx_wait_rom_busy(struct qla_hw_data *ha)
{
	long timeout = 0;
	long done = 0 ;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	while (done == 0) {
		done = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 4;
		timeout++;
		if (timeout >= rom_max_timeout) {
			ql_dbg(ql_dbg_p3p, vha, 0xb00a,
			    "%s: Timeout reached waiting for rom busy.\n",
			    QLA2XXX_DRIVER_NAME);
			return -1;
		}
	}
	return 0;
}

static int
qla82xx_wait_rom_done(struct qla_hw_data *ha)
{
	long timeout = 0;
	long done = 0 ;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	while (done == 0) {
		done = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 2;
		timeout++;
		if (timeout >= rom_max_timeout) {
			ql_dbg(ql_dbg_p3p, vha, 0xb00b,
			    "%s: Timeout reached waiting for rom done.\n",
			    QLA2XXX_DRIVER_NAME);
			return -1;
		}
	}
	return 0;
}

static int
qla82xx_md_rw_32(struct qla_hw_data *ha, uint32_t off, u32 data, uint8_t flag)
{
	uint32_t  off_value, rval = 0;

	wrt_reg_dword(CRB_WINDOW_2M + ha->nx_pcibase, off & 0xFFFF0000);

	/* Read back value to make sure write has gone through */
	rd_reg_dword(CRB_WINDOW_2M + ha->nx_pcibase);
	off_value  = (off & 0x0000FFFF);

	if (flag)
		wrt_reg_dword(off_value + CRB_INDIRECT_2M + ha->nx_pcibase,
			      data);
	else
		rval = rd_reg_dword(off_value + CRB_INDIRECT_2M +
				    ha->nx_pcibase);

	return rval;
}

static int
qla82xx_do_rom_fast_read(struct qla_hw_data *ha, int addr, int *valp)
{
	/* Dword reads to flash. */
	qla82xx_md_rw_32(ha, MD_DIRECT_ROM_WINDOW, (addr & 0xFFFF0000), 1);
	*valp = qla82xx_md_rw_32(ha, MD_DIRECT_ROM_READ_BASE +
	    (addr & 0x0000FFFF), 0, 0);

	return 0;
}

static int
qla82xx_rom_fast_read(struct qla_hw_data *ha, int addr, int *valp)
{
	int ret, loops = 0;
	uint32_t lock_owner = 0;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	while ((qla82xx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		schedule();
		loops++;
	}
	if (loops >= 50000) {
		lock_owner = qla82xx_rd_32(ha, QLA82XX_ROM_LOCK_ID);
		ql_log(ql_log_fatal, vha, 0x00b9,
		    "Failed to acquire SEM2 lock, Lock Owner %u.\n",
		    lock_owner);
		return -1;
	}
	ret = qla82xx_do_rom_fast_read(ha, addr, valp);
	qla82xx_rom_unlock(ha);
	return ret;
}

static int
qla82xx_read_status_reg(struct qla_hw_data *ha, uint32_t *val)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_RDSR);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		ql_log(ql_log_warn, vha, 0xb00c,
		    "Error waiting for rom done.\n");
		return -1;
	}
	*val = qla82xx_rd_32(ha, QLA82XX_ROMUSB_ROM_RDATA);
	return 0;
}

static int
qla82xx_flash_wait_write_finish(struct qla_hw_data *ha)
{
	uint32_t val = 0;
	int i, ret;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);
	for (i = 0; i < 50000; i++) {
		ret = qla82xx_read_status_reg(ha, &val);
		if (ret < 0 || (val & 1) == 0)
			return ret;
		udelay(10);
		cond_resched();
	}
	ql_log(ql_log_warn, vha, 0xb00d,
	       "Timeout reached waiting for write finish.\n");
	return -1;
}

static int
qla82xx_flash_set_write_enable(struct qla_hw_data *ha)
{
	uint32_t val;

	qla82xx_wait_rom_busy(ha);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_WREN);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha))
		return -1;
	if (qla82xx_read_status_reg(ha, &val) != 0)
		return -1;
	if ((val & 2) != 2)
		return -1;
	return 0;
}

static int
qla82xx_write_status_reg(struct qla_hw_data *ha, uint32_t val)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	if (qla82xx_flash_set_write_enable(ha))
		return -1;
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_WDATA, val);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, 0x1);
	if (qla82xx_wait_rom_done(ha)) {
		ql_log(ql_log_warn, vha, 0xb00e,
		    "Error waiting for rom done.\n");
		return -1;
	}
	return qla82xx_flash_wait_write_finish(ha);
}

static int
qla82xx_write_disable_flash(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_WRDI);
	if (qla82xx_wait_rom_done(ha)) {
		ql_log(ql_log_warn, vha, 0xb00f,
		    "Error waiting for rom done.\n");
		return -1;
	}
	return 0;
}

static int
ql82xx_rom_lock_d(struct qla_hw_data *ha)
{
	int loops = 0;
	uint32_t lock_owner = 0;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	while ((qla82xx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		cond_resched();
		loops++;
	}
	if (loops >= 50000) {
		lock_owner = qla82xx_rd_32(ha, QLA82XX_ROM_LOCK_ID);
		ql_log(ql_log_warn, vha, 0xb010,
		    "ROM lock failed, Lock Owner %u.\n", lock_owner);
		return -1;
	}
	return 0;
}

static int
qla82xx_write_flash_dword(struct qla_hw_data *ha, uint32_t flashaddr,
	uint32_t data)
{
	int ret = 0;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		ql_log(ql_log_warn, vha, 0xb011,
		    "ROM lock failed.\n");
		return ret;
	}

	ret = qla82xx_flash_set_write_enable(ha);
	if (ret < 0)
		goto done_write;

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_WDATA, data);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, flashaddr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_PP);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		ql_log(ql_log_warn, vha, 0xb012,
		    "Error waiting for rom done.\n");
		ret = -1;
		goto done_write;
	}

	ret = qla82xx_flash_wait_write_finish(ha);

done_write:
	qla82xx_rom_unlock(ha);
	return ret;
}

/* This routine does CRB initialize sequence
 *  to put the ISP into operational state
 */
static int
qla82xx_pinit_from_rom(scsi_qla_host_t *vha)
{
	int addr, val;
	int i ;
	struct crb_addr_pair *buf;
	unsigned long off;
	unsigned offset, n;
	struct qla_hw_data *ha = vha->hw;

	struct crb_addr_pair {
		long addr;
		long data;
	};

	/* Halt all the individual PEGs and other blocks of the ISP */
	qla82xx_rom_lock(ha);

	/* disable all I2Q */
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x10, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x14, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x18, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x1c, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x20, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x24, 0x0);

	/* disable all niu interrupts */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x40, 0xff);
	/* disable xge rx/tx */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x70000, 0x00);
	/* disable xg1 rx/tx */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x80000, 0x00);
	/* disable sideband mac */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x90000, 0x00);
	/* disable ap0 mac */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0xa0000, 0x00);
	/* disable ap1 mac */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0xb0000, 0x00);

	/* halt sre */
	val = qla82xx_rd_32(ha, QLA82XX_CRB_SRE + 0x1000);
	qla82xx_wr_32(ha, QLA82XX_CRB_SRE + 0x1000, val & (~(0x1)));

	/* halt epg */
	qla82xx_wr_32(ha, QLA82XX_CRB_EPG + 0x1300, 0x1);

	/* halt timers */
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x0, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x8, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x10, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x18, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x100, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x200, 0x0);

	/* halt pegs */
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_1 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_2 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_3 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_4 + 0x3c, 1);
	msleep(20);

	/* big hammer */
	if (test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags))
		/* don't reset CAM block on reset */
		qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0xfeffffff);
	else
		qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0xffffffff);
	qla82xx_rom_unlock(ha);

	/* Read the signature value from the flash.
	 * Offset 0: Contain signature (0xcafecafe)
	 * Offset 4: Offset and number of addr/value pairs
	 * that present in CRB initialize sequence
	 */
	n = 0;
	if (qla82xx_rom_fast_read(ha, 0, &n) != 0 || n != 0xcafecafeUL ||
	    qla82xx_rom_fast_read(ha, 4, &n) != 0) {
		ql_log(ql_log_fatal, vha, 0x006e,
		    "Error Reading crb_init area: n: %08x.\n", n);
		return -1;
	}

	/* Offset in flash = lower 16 bits
	 * Number of entries = upper 16 bits
	 */
	offset = n & 0xffffU;
	n = (n >> 16) & 0xffffU;

	/* number of addr/value pair should not exceed 1024 entries */
	if (n  >= 1024) {
		ql_log(ql_log_fatal, vha, 0x0071,
		    "Card flash not initialized:n=0x%x.\n", n);
		return -1;
	}

	ql_log(ql_log_info, vha, 0x0072,
	    "%d CRB init values found in ROM.\n", n);

	buf = kmalloc_array(n, sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL) {
		ql_log(ql_log_fatal, vha, 0x010c,
		    "Unable to allocate memory.\n");
		return -ENOMEM;
	}

	for (i = 0; i < n; i++) {
		if (qla82xx_rom_fast_read(ha, 8*i + 4*offset, &val) != 0 ||
		    qla82xx_rom_fast_read(ha, 8*i + 4*offset + 4, &addr) != 0) {
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
		off = qla82xx_decode_crb_addr((unsigned long)buf[i].addr) +
		    QLA82XX_PCI_CRBSPACE;
		/* Not all CRB  addr/value pair to be written,
		 * some of them are skipped
		 */

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
			ql_log(ql_log_fatal, vha, 0x0116,
			    "Unknown addr: 0x%08lx.\n", buf[i].addr);
			continue;
		}

		qla82xx_wr_32(ha, off, buf[i].data);

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
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_D+0xec, 0x1e);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_D+0x4c, 8);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_I+0x4c, 8);

	/* Clear all protocol processing engines */
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0+0xc, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_1+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_1+0xc, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_2+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_2+0xc, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_3+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_3+0xc, 0);
	return 0;
}

static int
qla82xx_pci_mem_write_2M(struct qla_hw_data *ha,
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
		if (qla82xx_pci_mem_bound_check(ha, off, size) == 0)
			return qla82xx_pci_mem_write_direct(ha,
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
		if (qla82xx_pci_mem_read_2M(ha, off8 +
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

	if (sz[0] == 8) {
		word[startword] = tmpw;
	} else {
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
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_ADDR_LO, temp);
		temp = 0;
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_ADDR_HI, temp);
		temp = word[i * scale] & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_WRDATA_LO, temp);
		temp = (word[i * scale] >> 32) & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_WRDATA_HI, temp);
		temp = word[i*scale + 1] & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb +
		    MIU_TEST_AGT_WRDATA_UPPER_LO, temp);
		temp = (word[i*scale + 1] >> 32) & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb +
		    MIU_TEST_AGT_WRDATA_UPPER_HI, temp);

		temp = MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = qla82xx_rd_32(ha, mem_crb + MIU_TEST_AGT_CTRL);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&ha->pdev->dev,
				    "failed to write through agent.\n");
			ret = -1;
			break;
		}
	}

	return ret;
}

static int
qla82xx_fw_load_from_flash(struct qla_hw_data *ha)
{
	int  i;
	long size = 0;
	long flashaddr = ha->flt_region_bootload << 2;
	long memaddr = BOOTLD_START;
	u64 data;
	u32 high, low;

	size = (IMAGE_START - BOOTLD_START) / 8;

	for (i = 0; i < size; i++) {
		if ((qla82xx_rom_fast_read(ha, flashaddr, (int *)&low)) ||
		    (qla82xx_rom_fast_read(ha, flashaddr + 4, (int *)&high))) {
			return -1;
		}
		data = ((u64)high << 32) | low ;
		qla82xx_pci_mem_write_2M(ha, memaddr, &data, 8);
		flashaddr += 8;
		memaddr += 8;

		if (i % 0x1000 == 0)
			msleep(1);
	}
	udelay(100);
	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x18, 0x1020);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0x80001e);
	read_unlock(&ha->hw_lock);
	return 0;
}

int
qla82xx_pci_mem_read_2M(struct qla_hw_data *ha,
		u64 off, void *data, int size)
{
	int i, j = 0, k, start, end, loop, sz[2], off0[2];
	int	      shift_amount;
	uint32_t      temp;
	uint64_t      off8, val, mem_crb, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */

	if (off >= QLA82XX_ADDR_QDR_NET && off <= QLA82XX_P3_ADDR_QDR_NET_MAX)
		mem_crb = QLA82XX_CRB_QDR_NET;
	else {
		mem_crb = QLA82XX_CRB_DDR_NET;
		if (qla82xx_pci_mem_bound_check(ha, off, size) == 0)
			return qla82xx_pci_mem_read_direct(ha,
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
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_ADDR_LO, temp);
		temp = 0;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_ADDR_HI, temp);
		temp = MIU_TA_CTL_ENABLE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = qla82xx_rd_32(ha, mem_crb + MIU_TEST_AGT_CTRL);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&ha->pdev->dev,
				    "failed to read through agent.\n");
			break;
		}

		start = off0[i] >> 2;
		end   = (off0[i] + sz[i] - 1) >> 2;
		for (k = start; k <= end; k++) {
			temp = qla82xx_rd_32(ha,
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


static struct qla82xx_uri_table_desc *
qla82xx_get_table_desc(const u8 *unirom, int section)
{
	uint32_t i;
	struct qla82xx_uri_table_desc *directory =
		(struct qla82xx_uri_table_desc *)&unirom[0];
	uint32_t offset;
	uint32_t tab_type;
	uint32_t entries = le32_to_cpu(directory->num_entries);

	for (i = 0; i < entries; i++) {
		offset = le32_to_cpu(directory->findex) +
		    (i * le32_to_cpu(directory->entry_size));
		tab_type = get_unaligned_le32((u32 *)&unirom[offset] + 8);

		if (tab_type == section)
			return (struct qla82xx_uri_table_desc *)&unirom[offset];
	}

	return NULL;
}

static struct qla82xx_uri_data_desc *
qla82xx_get_data_desc(struct qla_hw_data *ha,
	u32 section, u32 idx_offset)
{
	const u8 *unirom = ha->hablob->fw->data;
	int idx = get_unaligned_le32((u32 *)&unirom[ha->file_prd_off] +
				     idx_offset);
	struct qla82xx_uri_table_desc *tab_desc = NULL;
	uint32_t offset;

	tab_desc = qla82xx_get_table_desc(unirom, section);
	if (!tab_desc)
		return NULL;

	offset = le32_to_cpu(tab_desc->findex) +
	    (le32_to_cpu(tab_desc->entry_size) * idx);

	return (struct qla82xx_uri_data_desc *)&unirom[offset];
}

static u8 *
qla82xx_get_bootld_offset(struct qla_hw_data *ha)
{
	u32 offset = BOOTLD_START;
	struct qla82xx_uri_data_desc *uri_desc = NULL;

	if (ha->fw_type == QLA82XX_UNIFIED_ROMIMAGE) {
		uri_desc = qla82xx_get_data_desc(ha,
		    QLA82XX_URI_DIR_SECT_BOOTLD, QLA82XX_URI_BOOTLD_IDX_OFF);
		if (uri_desc)
			offset = le32_to_cpu(uri_desc->findex);
	}

	return (u8 *)&ha->hablob->fw->data[offset];
}

static u32 qla82xx_get_fw_size(struct qla_hw_data *ha)
{
	struct qla82xx_uri_data_desc *uri_desc = NULL;

	if (ha->fw_type == QLA82XX_UNIFIED_ROMIMAGE) {
		uri_desc =  qla82xx_get_data_desc(ha, QLA82XX_URI_DIR_SECT_FW,
		    QLA82XX_URI_FIRMWARE_IDX_OFF);
		if (uri_desc)
			return le32_to_cpu(uri_desc->size);
	}

	return get_unaligned_le32(&ha->hablob->fw->data[FW_SIZE_OFFSET]);
}

static u8 *
qla82xx_get_fw_offs(struct qla_hw_data *ha)
{
	u32 offset = IMAGE_START;
	struct qla82xx_uri_data_desc *uri_desc = NULL;

	if (ha->fw_type == QLA82XX_UNIFIED_ROMIMAGE) {
		uri_desc = qla82xx_get_data_desc(ha, QLA82XX_URI_DIR_SECT_FW,
			QLA82XX_URI_FIRMWARE_IDX_OFF);
		if (uri_desc)
			offset = le32_to_cpu(uri_desc->findex);
	}

	return (u8 *)&ha->hablob->fw->data[offset];
}

/* PCI related functions */
int qla82xx_pci_region_offset(struct pci_dev *pdev, int region)
{
	unsigned long val = 0;
	u32 control;

	switch (region) {
	case 0:
		val = 0;
		break;
	case 1:
		pci_read_config_dword(pdev, QLA82XX_PCI_REG_MSIX_TBL, &control);
		val = control + QLA82XX_MSIX_TBL_SPACE;
		break;
	}
	return val;
}


int
qla82xx_iospace_config(struct qla_hw_data *ha)
{
	uint32_t len = 0;

	if (pci_request_regions(ha->pdev, QLA2XXX_DRIVER_NAME)) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x000c,
		    "Failed to reserver selected regions.\n");
		goto iospace_error_exit;
	}

	/* Use MMIO operations for all accesses. */
	if (!(pci_resource_flags(ha->pdev, 0) & IORESOURCE_MEM)) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x000d,
		    "Region #0 not an MMIO resource, aborting.\n");
		goto iospace_error_exit;
	}

	len = pci_resource_len(ha->pdev, 0);
	ha->nx_pcibase = ioremap(pci_resource_start(ha->pdev, 0), len);
	if (!ha->nx_pcibase) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x000e,
		    "Cannot remap pcibase MMIO, aborting.\n");
		goto iospace_error_exit;
	}

	/* Mapping of IO base pointer */
	if (IS_QLA8044(ha)) {
		ha->iobase = ha->nx_pcibase;
	} else if (IS_QLA82XX(ha)) {
		ha->iobase = ha->nx_pcibase + 0xbc000 + (ha->pdev->devfn << 11);
	}

	if (!ql2xdbwr) {
		ha->nxdb_wr_ptr = ioremap((pci_resource_start(ha->pdev, 4) +
		    (ha->pdev->devfn << 12)), 4);
		if (!ha->nxdb_wr_ptr) {
			ql_log_pci(ql_log_fatal, ha->pdev, 0x000f,
			    "Cannot remap MMIO, aborting.\n");
			goto iospace_error_exit;
		}

		/* Mapping of IO base pointer,
		 * door bell read and write pointer
		 */
		ha->nxdb_rd_ptr = ha->nx_pcibase + (512 * 1024) +
		    (ha->pdev->devfn * 8);
	} else {
		ha->nxdb_wr_ptr = (void __iomem *)(ha->pdev->devfn == 6 ?
			QLA82XX_CAMRAM_DB1 :
			QLA82XX_CAMRAM_DB2);
	}

	ha->max_req_queues = ha->max_rsp_queues = 1;
	ha->msix_count = ha->max_rsp_queues + 1;
	ql_dbg_pci(ql_dbg_multiq, ha->pdev, 0xc006,
	    "nx_pci_base=%p iobase=%p "
	    "max_req_queues=%d msix_count=%d.\n",
	    ha->nx_pcibase, ha->iobase,
	    ha->max_req_queues, ha->msix_count);
	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0010,
	    "nx_pci_base=%p iobase=%p "
	    "max_req_queues=%d msix_count=%d.\n",
	    ha->nx_pcibase, ha->iobase,
	    ha->max_req_queues, ha->msix_count);
	return 0;

iospace_error_exit:
	return -ENOMEM;
}

/* GS related functions */

/* Initialization related functions */

/**
 * qla82xx_pci_config() - Setup ISP82xx PCI configuration registers.
 * @vha: HA context
 *
 * Returns 0 on success.
*/
int
qla82xx_pci_config(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	int ret;

	pci_set_master(ha->pdev);
	ret = pci_set_mwi(ha->pdev);
	ha->chip_revision = ha->pdev->revision;
	ql_dbg(ql_dbg_init, vha, 0x0043,
	    "Chip revision:%d; pci_set_mwi() returned %d.\n",
	    ha->chip_revision, ret);
	return 0;
}

/**
 * qla82xx_reset_chip() - Setup ISP82xx PCI configuration registers.
 * @vha: HA context
 *
 * Returns 0 on success.
 */
int
qla82xx_reset_chip(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	ha->isp_ops->disable_intrs(ha);

	return QLA_SUCCESS;
}

void qla82xx_config_rings(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;
	struct init_cb_81xx *icb;
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];

	/* Setup ring parameters in initialization control block. */
	icb = (struct init_cb_81xx *)ha->init_cb;
	icb->request_q_outpointer = cpu_to_le16(0);
	icb->response_q_inpointer = cpu_to_le16(0);
	icb->request_q_length = cpu_to_le16(req->length);
	icb->response_q_length = cpu_to_le16(rsp->length);
	put_unaligned_le64(req->dma, &icb->request_q_address);
	put_unaligned_le64(rsp->dma, &icb->response_q_address);

	wrt_reg_dword(&reg->req_q_out[0], 0);
	wrt_reg_dword(&reg->rsp_q_in[0], 0);
	wrt_reg_dword(&reg->rsp_q_out[0], 0);
}

static int
qla82xx_fw_load_from_blob(struct qla_hw_data *ha)
{
	u64 *ptr64;
	u32 i, flashaddr, size;
	__le64 data;

	size = (IMAGE_START - BOOTLD_START) / 8;

	ptr64 = (u64 *)qla82xx_get_bootld_offset(ha);
	flashaddr = BOOTLD_START;

	for (i = 0; i < size; i++) {
		data = cpu_to_le64(ptr64[i]);
		if (qla82xx_pci_mem_write_2M(ha, flashaddr, &data, 8))
			return -EIO;
		flashaddr += 8;
	}

	flashaddr = FLASH_ADDR_START;
	size = qla82xx_get_fw_size(ha) / 8;
	ptr64 = (u64 *)qla82xx_get_fw_offs(ha);

	for (i = 0; i < size; i++) {
		data = cpu_to_le64(ptr64[i]);

		if (qla82xx_pci_mem_write_2M(ha, flashaddr, &data, 8))
			return -EIO;
		flashaddr += 8;
	}
	udelay(100);

	/* Write a magic value to CAMRAM register
	 * at a specified offset to indicate
	 * that all data is written and
	 * ready for firmware to initialize.
	 */
	qla82xx_wr_32(ha, QLA82XX_CAM_RAM(0x1fc), QLA82XX_BDINFO_MAGIC);

	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x18, 0x1020);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0x80001e);
	read_unlock(&ha->hw_lock);
	return 0;
}

static int
qla82xx_set_product_offset(struct qla_hw_data *ha)
{
	struct qla82xx_uri_table_desc *ptab_desc = NULL;
	const uint8_t *unirom = ha->hablob->fw->data;
	uint32_t i;
	uint32_t entries;
	uint32_t flags, file_chiprev, offset;
	uint8_t chiprev = ha->chip_revision;
	/* Hardcoding mn_present flag for P3P */
	int mn_present = 0;
	uint32_t flagbit;

	ptab_desc = qla82xx_get_table_desc(unirom,
		 QLA82XX_URI_DIR_SECT_PRODUCT_TBL);
	if (!ptab_desc)
		return -1;

	entries = le32_to_cpu(ptab_desc->num_entries);

	for (i = 0; i < entries; i++) {
		offset = le32_to_cpu(ptab_desc->findex) +
			(i * le32_to_cpu(ptab_desc->entry_size));
		flags = le32_to_cpu(*((__le32 *)&unirom[offset] +
			QLA82XX_URI_FLAGS_OFF));
		file_chiprev = le32_to_cpu(*((__le32 *)&unirom[offset] +
			QLA82XX_URI_CHIP_REV_OFF));

		flagbit = mn_present ? 1 : 2;

		if ((chiprev == file_chiprev) && ((1ULL << flagbit) & flags)) {
			ha->file_prd_off = offset;
			return 0;
		}
	}
	return -1;
}

static int
qla82xx_validate_firmware_blob(scsi_qla_host_t *vha, uint8_t fw_type)
{
	uint32_t val;
	uint32_t min_size;
	struct qla_hw_data *ha = vha->hw;
	const struct firmware *fw = ha->hablob->fw;

	ha->fw_type = fw_type;

	if (fw_type == QLA82XX_UNIFIED_ROMIMAGE) {
		if (qla82xx_set_product_offset(ha))
			return -EINVAL;

		min_size = QLA82XX_URI_FW_MIN_SIZE;
	} else {
		val = get_unaligned_le32(&fw->data[QLA82XX_FW_MAGIC_OFFSET]);
		if (val != QLA82XX_BDINFO_MAGIC)
			return -EINVAL;

		min_size = QLA82XX_FW_MIN_SIZE;
	}

	if (fw->size < min_size)
		return -EINVAL;
	return 0;
}

static int
qla82xx_check_cmdpeg_state(struct qla_hw_data *ha)
{
	u32 val = 0;
	int retries = 60;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	do {
		read_lock(&ha->hw_lock);
		val = qla82xx_rd_32(ha, CRB_CMDPEG_STATE);
		read_unlock(&ha->hw_lock);

		switch (val) {
		case PHAN_INITIALIZE_COMPLETE:
		case PHAN_INITIALIZE_ACK:
			return QLA_SUCCESS;
		case PHAN_INITIALIZE_FAILED:
			break;
		default:
			break;
		}
		ql_log(ql_log_info, vha, 0x00a8,
		    "CRB_CMDPEG_STATE: 0x%x and retries:0x%x.\n",
		    val, retries);

		msleep(500);

	} while (--retries);

	ql_log(ql_log_fatal, vha, 0x00a9,
	    "Cmd Peg initialization failed: 0x%x.\n", val);

	val = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_PEGTUNE_DONE);
	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, CRB_CMDPEG_STATE, PHAN_INITIALIZE_FAILED);
	read_unlock(&ha->hw_lock);
	return QLA_FUNCTION_FAILED;
}

static int
qla82xx_check_rcvpeg_state(struct qla_hw_data *ha)
{
	u32 val = 0;
	int retries = 60;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	do {
		read_lock(&ha->hw_lock);
		val = qla82xx_rd_32(ha, CRB_RCVPEG_STATE);
		read_unlock(&ha->hw_lock);

		switch (val) {
		case PHAN_INITIALIZE_COMPLETE:
		case PHAN_INITIALIZE_ACK:
			return QLA_SUCCESS;
		case PHAN_INITIALIZE_FAILED:
			break;
		default:
			break;
		}
		ql_log(ql_log_info, vha, 0x00ab,
		    "CRB_RCVPEG_STATE: 0x%x and retries: 0x%x.\n",
		    val, retries);

		msleep(500);

	} while (--retries);

	ql_log(ql_log_fatal, vha, 0x00ac,
	    "Rcv Peg initialization failed: 0x%x.\n", val);
	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, CRB_RCVPEG_STATE, PHAN_INITIALIZE_FAILED);
	read_unlock(&ha->hw_lock);
	return QLA_FUNCTION_FAILED;
}

/* ISR related functions */
static struct qla82xx_legacy_intr_set legacy_intr[] =
	QLA82XX_LEGACY_INTR_CONFIG;

/*
 * qla82xx_mbx_completion() - Process mailbox command completions.
 * @ha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
void
qla82xx_mbx_completion(scsi_qla_host_t *vha, uint16_t mb0)
{
	uint16_t	cnt;
	__le16 __iomem *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;

	wptr = &reg->mailbox_out[1];

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		ha->mailbox_out[cnt] = rd_reg_word(wptr);
		wptr++;
	}

	if (!ha->mcp)
		ql_dbg(ql_dbg_async, vha, 0x5053,
		    "MBX pointer ERROR.\n");
}

/**
 * qla82xx_intr_handler() - Process interrupts for the ISP23xx and ISP63xx.
 * @irq: interrupt number
 * @dev_id: SCSI driver HA context
 *
 * Called by system whenever the host adapter generates an interrupt.
 *
 * Returns handled flag.
 */
irqreturn_t
qla82xx_intr_handler(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;
	int status = 0, status1 = 0;
	unsigned long	flags;
	unsigned long	iter;
	uint32_t	stat = 0;
	uint16_t	mb[8];

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		ql_log(ql_log_info, NULL, 0xb053,
		    "%s: NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;

	if (!ha->flags.msi_enabled) {
		status = qla82xx_rd_32(ha, ISR_INT_VECTOR);
		if (!(status & ha->nx_legacy_intr.int_vec_bit))
			return IRQ_NONE;

		status1 = qla82xx_rd_32(ha, ISR_INT_STATE_REG);
		if (!ISR_IS_LEGACY_INTR_TRIGGERED(status1))
			return IRQ_NONE;
	}

	/* clear the interrupt */
	qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_status_reg, 0xffffffff);

	/* read twice to ensure write is flushed */
	qla82xx_rd_32(ha, ISR_INT_VECTOR);
	qla82xx_rd_32(ha, ISR_INT_VECTOR);

	reg = &ha->iobase->isp82;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	for (iter = 1; iter--; ) {

		if (rd_reg_dword(&reg->host_int)) {
			stat = rd_reg_dword(&reg->host_status);

			switch (stat & 0xff) {
			case 0x1:
			case 0x2:
			case 0x10:
			case 0x11:
				qla82xx_mbx_completion(vha, MSW(stat));
				status |= MBX_INTERRUPT;
				break;
			case 0x12:
				mb[0] = MSW(stat);
				mb[1] = rd_reg_word(&reg->mailbox_out[1]);
				mb[2] = rd_reg_word(&reg->mailbox_out[2]);
				mb[3] = rd_reg_word(&reg->mailbox_out[3]);
				qla2x00_async_event(vha, rsp, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(vha, rsp);
				break;
			default:
				ql_dbg(ql_dbg_async, vha, 0x5054,
				    "Unrecognized interrupt type (%d).\n",
				    stat & 0xff);
				break;
			}
		}
		wrt_reg_dword(&reg->host_int, 0);
	}

	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (!ha->flags.msi_enabled)
		qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);

	return IRQ_HANDLED;
}

irqreturn_t
qla82xx_msix_default(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;
	int status = 0;
	unsigned long flags;
	uint32_t stat = 0;
	uint32_t host_int = 0;
	uint16_t mb[8];

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;

	reg = &ha->iobase->isp82;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	do {
		host_int = rd_reg_dword(&reg->host_int);
		if (qla2x00_check_reg32_for_disconnect(vha, host_int))
			break;
		if (host_int) {
			stat = rd_reg_dword(&reg->host_status);

			switch (stat & 0xff) {
			case 0x1:
			case 0x2:
			case 0x10:
			case 0x11:
				qla82xx_mbx_completion(vha, MSW(stat));
				status |= MBX_INTERRUPT;
				break;
			case 0x12:
				mb[0] = MSW(stat);
				mb[1] = rd_reg_word(&reg->mailbox_out[1]);
				mb[2] = rd_reg_word(&reg->mailbox_out[2]);
				mb[3] = rd_reg_word(&reg->mailbox_out[3]);
				qla2x00_async_event(vha, rsp, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(vha, rsp);
				break;
			default:
				ql_dbg(ql_dbg_async, vha, 0x5041,
				    "Unrecognized interrupt type (%d).\n",
				    stat & 0xff);
				break;
			}
		}
		wrt_reg_dword(&reg->host_int, 0);
	} while (0);

	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
}

irqreturn_t
qla82xx_msix_rsp_q(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;
	unsigned long flags;
	uint32_t host_int = 0;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp82;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	host_int = rd_reg_dword(&reg->host_int);
	if (qla2x00_check_reg32_for_disconnect(vha, host_int))
		goto out;
	qla24xx_process_response_queue(vha, rsp);
	wrt_reg_dword(&reg->host_int, 0);
out:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return IRQ_HANDLED;
}

void
qla82xx_poll(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;
	uint32_t stat;
	uint32_t host_int = 0;
	uint16_t mb[8];
	unsigned long flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer.\n", __func__);
		return;
	}
	ha = rsp->hw;

	reg = &ha->iobase->isp82;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);

	host_int = rd_reg_dword(&reg->host_int);
	if (qla2x00_check_reg32_for_disconnect(vha, host_int))
		goto out;
	if (host_int) {
		stat = rd_reg_dword(&reg->host_status);
		switch (stat & 0xff) {
		case 0x1:
		case 0x2:
		case 0x10:
		case 0x11:
			qla82xx_mbx_completion(vha, MSW(stat));
			break;
		case 0x12:
			mb[0] = MSW(stat);
			mb[1] = rd_reg_word(&reg->mailbox_out[1]);
			mb[2] = rd_reg_word(&reg->mailbox_out[2]);
			mb[3] = rd_reg_word(&reg->mailbox_out[3]);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case 0x13:
			qla24xx_process_response_queue(vha, rsp);
			break;
		default:
			ql_dbg(ql_dbg_p3p, vha, 0xb013,
			    "Unrecognized interrupt type (%d).\n",
			    stat * 0xff);
			break;
		}
		wrt_reg_dword(&reg->host_int, 0);
	}
out:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla82xx_enable_intrs(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	qla82xx_mbx_intr_enable(vha);
	spin_lock_irq(&ha->hardware_lock);
	if (IS_QLA8044(ha))
		qla8044_wr_reg(ha, LEG_INTR_MASK_OFFSET, 0);
	else
		qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);
	spin_unlock_irq(&ha->hardware_lock);
	ha->interrupts_on = 1;
}

void
qla82xx_disable_intrs(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	if (ha->interrupts_on)
		qla82xx_mbx_intr_disable(vha);

	spin_lock_irq(&ha->hardware_lock);
	if (IS_QLA8044(ha))
		qla8044_wr_reg(ha, LEG_INTR_MASK_OFFSET, 1);
	else
		qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0x0400);
	spin_unlock_irq(&ha->hardware_lock);
	ha->interrupts_on = 0;
}

void qla82xx_init_flags(struct qla_hw_data *ha)
{
	struct qla82xx_legacy_intr_set *nx_legacy_intr;

	/* ISP 8021 initializations */
	rwlock_init(&ha->hw_lock);
	ha->qdr_sn_window = -1;
	ha->ddr_mn_window = -1;
	ha->curr_window = 255;
	ha->portnum = PCI_FUNC(ha->pdev->devfn);
	nx_legacy_intr = &legacy_intr[ha->portnum];
	ha->nx_legacy_intr.int_vec_bit = nx_legacy_intr->int_vec_bit;
	ha->nx_legacy_intr.tgt_status_reg = nx_legacy_intr->tgt_status_reg;
	ha->nx_legacy_intr.tgt_mask_reg = nx_legacy_intr->tgt_mask_reg;
	ha->nx_legacy_intr.pci_int_reg = nx_legacy_intr->pci_int_reg;
}

static inline void
qla82xx_set_idc_version(scsi_qla_host_t *vha)
{
	int idc_ver;
	uint32_t drv_active;
	struct qla_hw_data *ha = vha->hw;

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	if (drv_active == (QLA82XX_DRV_ACTIVE << (ha->portnum * 4))) {
		qla82xx_wr_32(ha, QLA82XX_CRB_DRV_IDC_VERSION,
		    QLA82XX_IDC_VERSION);
		ql_log(ql_log_info, vha, 0xb082,
		    "IDC version updated to %d\n", QLA82XX_IDC_VERSION);
	} else {
		idc_ver = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_IDC_VERSION);
		if (idc_ver != QLA82XX_IDC_VERSION)
			ql_log(ql_log_info, vha, 0xb083,
			    "qla2xxx driver IDC version %d is not compatible "
			    "with IDC version %d of the other drivers\n",
			    QLA82XX_IDC_VERSION, idc_ver);
	}
}

inline void
qla82xx_set_drv_active(scsi_qla_host_t *vha)
{
	uint32_t drv_active;
	struct qla_hw_data *ha = vha->hw;

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);

	/* If reset value is all FF's, initialize DRV_ACTIVE */
	if (drv_active == 0xffffffff) {
		qla82xx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE,
			QLA82XX_DRV_NOT_ACTIVE);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	}
	drv_active |= (QLA82XX_DRV_ACTIVE << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, drv_active);
}

inline void
qla82xx_clear_drv_active(struct qla_hw_data *ha)
{
	uint32_t drv_active;

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	drv_active &= ~(QLA82XX_DRV_ACTIVE << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, drv_active);
}

static inline int
qla82xx_need_reset(struct qla_hw_data *ha)
{
	uint32_t drv_state;
	int rval;

	if (ha->flags.nic_core_reset_owner)
		return 1;
	else {
		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		rval = drv_state & (QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
		return rval;
	}
}

static inline void
qla82xx_set_rst_ready(struct qla_hw_data *ha)
{
	uint32_t drv_state;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);

	/* If reset value is all FF's, initialize DRV_STATE */
	if (drv_state == 0xffffffff) {
		qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, QLA82XX_DRVST_NOT_RDY);
		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	}
	drv_state |= (QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
	ql_dbg(ql_dbg_init, vha, 0x00bb,
	    "drv_state = 0x%08x.\n", drv_state);
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, drv_state);
}

static inline void
qla82xx_clear_rst_ready(struct qla_hw_data *ha)
{
	uint32_t drv_state;

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_state &= ~(QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, drv_state);
}

static inline void
qla82xx_set_qsnt_ready(struct qla_hw_data *ha)
{
	uint32_t qsnt_state;

	qsnt_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	qsnt_state |= (QLA82XX_DRVST_QSNT_RDY << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, qsnt_state);
}

void
qla82xx_clear_qsnt_ready(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t qsnt_state;

	qsnt_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	qsnt_state &= ~(QLA82XX_DRVST_QSNT_RDY << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, qsnt_state);
}

static int
qla82xx_load_fw(scsi_qla_host_t *vha)
{
	int rst;
	struct fw_blob *blob;
	struct qla_hw_data *ha = vha->hw;

	if (qla82xx_pinit_from_rom(vha) != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0x009f,
		    "Error during CRB initialization.\n");
		return QLA_FUNCTION_FAILED;
	}
	udelay(500);

	/* Bring QM and CAMRAM out of reset */
	rst = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET);
	rst &= ~((1 << 28) | (1 << 24));
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, rst);

	/*
	 * FW Load priority:
	 * 1) Operational firmware residing in flash.
	 * 2) Firmware via request-firmware interface (.bin file).
	 */
	if (ql2xfwloadbin == 2)
		goto try_blob_fw;

	ql_log(ql_log_info, vha, 0x00a0,
	    "Attempting to load firmware from flash.\n");

	if (qla82xx_fw_load_from_flash(ha) == QLA_SUCCESS) {
		ql_log(ql_log_info, vha, 0x00a1,
		    "Firmware loaded successfully from flash.\n");
		return QLA_SUCCESS;
	} else {
		ql_log(ql_log_warn, vha, 0x0108,
		    "Firmware load from flash failed.\n");
	}

try_blob_fw:
	ql_log(ql_log_info, vha, 0x00a2,
	    "Attempting to load firmware from blob.\n");

	/* Load firmware blob. */
	blob = ha->hablob = qla2x00_request_firmware(vha);
	if (!blob) {
		ql_log(ql_log_fatal, vha, 0x00a3,
		    "Firmware image not present.\n");
		goto fw_load_failed;
	}

	/* Validating firmware blob */
	if (qla82xx_validate_firmware_blob(vha,
		QLA82XX_FLASH_ROMIMAGE)) {
		/* Fallback to URI format */
		if (qla82xx_validate_firmware_blob(vha,
			QLA82XX_UNIFIED_ROMIMAGE)) {
			ql_log(ql_log_fatal, vha, 0x00a4,
			    "No valid firmware image found.\n");
			return QLA_FUNCTION_FAILED;
		}
	}

	if (qla82xx_fw_load_from_blob(ha) == QLA_SUCCESS) {
		ql_log(ql_log_info, vha, 0x00a5,
		    "Firmware loaded successfully from binary blob.\n");
		return QLA_SUCCESS;
	}

	ql_log(ql_log_fatal, vha, 0x00a6,
	       "Firmware load failed for binary blob.\n");
	blob->fw = NULL;
	blob = NULL;

fw_load_failed:
	return QLA_FUNCTION_FAILED;
}

int
qla82xx_start_firmware(scsi_qla_host_t *vha)
{
	uint16_t      lnk;
	struct qla_hw_data *ha = vha->hw;

	/* scrub dma mask expansion register */
	qla82xx_wr_32(ha, CRB_DMA_SHIFT, QLA82XX_DMA_SHIFT_VALUE);

	/* Put both the PEG CMD and RCV PEG to default state
	 * of 0 before resetting the hardware
	 */
	qla82xx_wr_32(ha, CRB_CMDPEG_STATE, 0);
	qla82xx_wr_32(ha, CRB_RCVPEG_STATE, 0);

	/* Overwrite stale initialization register values */
	qla82xx_wr_32(ha, QLA82XX_PEG_HALT_STATUS1, 0);
	qla82xx_wr_32(ha, QLA82XX_PEG_HALT_STATUS2, 0);

	if (qla82xx_load_fw(vha) != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0x00a7,
		    "Error trying to start fw.\n");
		return QLA_FUNCTION_FAILED;
	}

	/* Handshake with the card before we register the devices. */
	if (qla82xx_check_cmdpeg_state(ha) != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0x00aa,
		    "Error during card handshake.\n");
		return QLA_FUNCTION_FAILED;
	}

	/* Negotiated Link width */
	pcie_capability_read_word(ha->pdev, PCI_EXP_LNKSTA, &lnk);
	ha->link_width = (lnk >> 4) & 0x3f;

	/* Synchronize with Receive peg */
	return qla82xx_check_rcvpeg_state(ha);
}

static __le32 *
qla82xx_read_flash_data(scsi_qla_host_t *vha, __le32 *dwptr, uint32_t faddr,
	uint32_t length)
{
	uint32_t i;
	uint32_t val;
	struct qla_hw_data *ha = vha->hw;

	/* Dword reads to flash. */
	for (i = 0; i < length/4; i++, faddr += 4) {
		if (qla82xx_rom_fast_read(ha, faddr, &val)) {
			ql_log(ql_log_warn, vha, 0x0106,
			    "Do ROM fast read failed.\n");
			goto done_read;
		}
		dwptr[i] = cpu_to_le32(val);
	}
done_read:
	return dwptr;
}

static int
qla82xx_unprotect_flash(struct qla_hw_data *ha)
{
	int ret;
	uint32_t val;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		ql_log(ql_log_warn, vha, 0xb014,
		    "ROM Lock failed.\n");
		return ret;
	}

	ret = qla82xx_read_status_reg(ha, &val);
	if (ret < 0)
		goto done_unprotect;

	val &= ~(BLOCK_PROTECT_BITS << 2);
	ret = qla82xx_write_status_reg(ha, val);
	if (ret < 0) {
		val |= (BLOCK_PROTECT_BITS << 2);
		qla82xx_write_status_reg(ha, val);
	}

	if (qla82xx_write_disable_flash(ha) != 0)
		ql_log(ql_log_warn, vha, 0xb015,
		    "Write disable failed.\n");

done_unprotect:
	qla82xx_rom_unlock(ha);
	return ret;
}

static int
qla82xx_protect_flash(struct qla_hw_data *ha)
{
	int ret;
	uint32_t val;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		ql_log(ql_log_warn, vha, 0xb016,
		    "ROM Lock failed.\n");
		return ret;
	}

	ret = qla82xx_read_status_reg(ha, &val);
	if (ret < 0)
		goto done_protect;

	val |= (BLOCK_PROTECT_BITS << 2);
	/* LOCK all sectors */
	ret = qla82xx_write_status_reg(ha, val);
	if (ret < 0)
		ql_log(ql_log_warn, vha, 0xb017,
		    "Write status register failed.\n");

	if (qla82xx_write_disable_flash(ha) != 0)
		ql_log(ql_log_warn, vha, 0xb018,
		    "Write disable failed.\n");
done_protect:
	qla82xx_rom_unlock(ha);
	return ret;
}

static int
qla82xx_erase_sector(struct qla_hw_data *ha, int addr)
{
	int ret = 0;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		ql_log(ql_log_warn, vha, 0xb019,
		    "ROM Lock failed.\n");
		return ret;
	}

	qla82xx_flash_set_write_enable(ha);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, addr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_SE);

	if (qla82xx_wait_rom_done(ha)) {
		ql_log(ql_log_warn, vha, 0xb01a,
		    "Error waiting for rom done.\n");
		ret = -1;
		goto done;
	}
	ret = qla82xx_flash_wait_write_finish(ha);
done:
	qla82xx_rom_unlock(ha);
	return ret;
}

/*
 * Address and length are byte address
 */
void *
qla82xx_read_optrom_data(struct scsi_qla_host *vha, void *buf,
	uint32_t offset, uint32_t length)
{
	scsi_block_requests(vha->host);
	qla82xx_read_flash_data(vha, buf, offset, length);
	scsi_unblock_requests(vha->host);
	return buf;
}

static int
qla82xx_write_flash_data(struct scsi_qla_host *vha, __le32 *dwptr,
	uint32_t faddr, uint32_t dwords)
{
	int ret;
	uint32_t liter;
	uint32_t rest_addr;
	dma_addr_t optrom_dma;
	void *optrom = NULL;
	int page_mode = 0;
	struct qla_hw_data *ha = vha->hw;

	ret = -1;

	/* Prepare burst-capable write on supported ISPs. */
	if (page_mode && !(faddr & 0xfff) &&
	    dwords > OPTROM_BURST_DWORDS) {
		optrom = dma_alloc_coherent(&ha->pdev->dev, OPTROM_BURST_SIZE,
		    &optrom_dma, GFP_KERNEL);
		if (!optrom) {
			ql_log(ql_log_warn, vha, 0xb01b,
			    "Unable to allocate memory "
			    "for optrom burst write (%x KB).\n",
			    OPTROM_BURST_SIZE / 1024);
		}
	}

	rest_addr = ha->fdt_block_size - 1;

	ret = qla82xx_unprotect_flash(ha);
	if (ret) {
		ql_log(ql_log_warn, vha, 0xb01c,
		    "Unable to unprotect flash for update.\n");
		goto write_done;
	}

	for (liter = 0; liter < dwords; liter++, faddr += 4, dwptr++) {
		/* Are we at the beginning of a sector? */
		if ((faddr & rest_addr) == 0) {

			ret = qla82xx_erase_sector(ha, faddr);
			if (ret) {
				ql_log(ql_log_warn, vha, 0xb01d,
				    "Unable to erase sector: address=%x.\n",
				    faddr);
				break;
			}
		}

		/* Go with burst-write. */
		if (optrom && (liter + OPTROM_BURST_DWORDS) <= dwords) {
			/* Copy data to DMA'ble buffer. */
			memcpy(optrom, dwptr, OPTROM_BURST_SIZE);

			ret = qla2x00_load_ram(vha, optrom_dma,
			    (ha->flash_data_off | faddr),
			    OPTROM_BURST_DWORDS);
			if (ret != QLA_SUCCESS) {
				ql_log(ql_log_warn, vha, 0xb01e,
				    "Unable to burst-write optrom segment "
				    "(%x/%x/%llx).\n", ret,
				    (ha->flash_data_off | faddr),
				    (unsigned long long)optrom_dma);
				ql_log(ql_log_warn, vha, 0xb01f,
				    "Reverting to slow-write.\n");

				dma_free_coherent(&ha->pdev->dev,
				    OPTROM_BURST_SIZE, optrom, optrom_dma);
				optrom = NULL;
			} else {
				liter += OPTROM_BURST_DWORDS - 1;
				faddr += OPTROM_BURST_DWORDS - 1;
				dwptr += OPTROM_BURST_DWORDS - 1;
				continue;
			}
		}

		ret = qla82xx_write_flash_dword(ha, faddr,
						le32_to_cpu(*dwptr));
		if (ret) {
			ql_dbg(ql_dbg_p3p, vha, 0xb020,
			    "Unable to program flash address=%x data=%x.\n",
			    faddr, *dwptr);
			break;
		}
	}

	ret = qla82xx_protect_flash(ha);
	if (ret)
		ql_log(ql_log_warn, vha, 0xb021,
		    "Unable to protect flash after update.\n");
write_done:
	if (optrom)
		dma_free_coherent(&ha->pdev->dev,
		    OPTROM_BURST_SIZE, optrom, optrom_dma);
	return ret;
}

int
qla82xx_write_optrom_data(struct scsi_qla_host *vha, void *buf,
	uint32_t offset, uint32_t length)
{
	int rval;

	/* Suspend HBA. */
	scsi_block_requests(vha->host);
	rval = qla82xx_write_flash_data(vha, buf, offset, length >> 2);
	scsi_unblock_requests(vha->host);

	/* Convert return ISP82xx to generic */
	if (rval)
		rval = QLA_FUNCTION_FAILED;
	else
		rval = QLA_SUCCESS;
	return rval;
}

void
qla82xx_start_iocbs(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];
	uint32_t dbval;

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	dbval = 0x04 | (ha->portnum << 5);

	dbval = dbval | (req->id << 8) | (req->ring_index << 16);
	if (ql2xdbwr)
		qla82xx_wr_32(ha, (unsigned long)ha->nxdb_wr_ptr, dbval);
	else {
		wrt_reg_dword(ha->nxdb_wr_ptr, dbval);
		wmb();
		while (rd_reg_dword(ha->nxdb_rd_ptr) != dbval) {
			wrt_reg_dword(ha->nxdb_wr_ptr, dbval);
			wmb();
		}
	}
}

static void
qla82xx_rom_lock_recovery(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	uint32_t lock_owner = 0;

	if (qla82xx_rom_lock(ha)) {
		lock_owner = qla82xx_rd_32(ha, QLA82XX_ROM_LOCK_ID);
		/* Someone else is holding the lock. */
		ql_log(ql_log_info, vha, 0xb022,
		    "Resetting rom_lock, Lock Owner %u.\n", lock_owner);
	}
	/*
	 * Either we got the lock, or someone
	 * else died while holding it.
	 * In either case, unlock.
	 */
	qla82xx_rom_unlock(ha);
}

/*
 * qla82xx_device_bootstrap
 *    Initialize device, set DEV_READY, start fw
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return:
 *    Success : 0
 *    Failed  : 1
 */
static int
qla82xx_device_bootstrap(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;
	int i;
	uint32_t old_count, count;
	struct qla_hw_data *ha = vha->hw;
	int need_reset = 0;

	need_reset = qla82xx_need_reset(ha);

	if (need_reset) {
		/* We are trying to perform a recovery here. */
		if (ha->flags.isp82xx_fw_hung)
			qla82xx_rom_lock_recovery(ha);
	} else  {
		old_count = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);
		for (i = 0; i < 10; i++) {
			msleep(200);
			count = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);
			if (count != old_count) {
				rval = QLA_SUCCESS;
				goto dev_ready;
			}
		}
		qla82xx_rom_lock_recovery(ha);
	}

	/* set to DEV_INITIALIZING */
	ql_log(ql_log_info, vha, 0x009e,
	    "HW State: INITIALIZING.\n");
	qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA8XXX_DEV_INITIALIZING);

	qla82xx_idc_unlock(ha);
	rval = qla82xx_start_firmware(vha);
	qla82xx_idc_lock(ha);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0x00ad,
		    "HW State: FAILED.\n");
		qla82xx_clear_drv_active(ha);
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA8XXX_DEV_FAILED);
		return rval;
	}

dev_ready:
	ql_log(ql_log_info, vha, 0x00ae,
	    "HW State: READY.\n");
	qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA8XXX_DEV_READY);

	return QLA_SUCCESS;
}

/*
* qla82xx_need_qsnt_handler
*    Code to start quiescence sequence
*
* Note:
*      IDC lock must be held upon entry
*
* Return: void
*/

static void
qla82xx_need_qsnt_handler(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t dev_state, drv_state, drv_active;
	unsigned long reset_timeout;

	if (vha->flags.online) {
		/*Block any further I/O and wait for pending cmnds to complete*/
		qla2x00_quiesce_io(vha);
	}

	/* Set the quiescence ready bit */
	qla82xx_set_qsnt_ready(ha);

	/*wait for 30 secs for other functions to ack */
	reset_timeout = jiffies + (30 * HZ);

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	/* Its 2 that is written when qsnt is acked, moving one bit */
	drv_active = drv_active << 0x01;

	while (drv_state != drv_active) {

		if (time_after_eq(jiffies, reset_timeout)) {
			/* quiescence timeout, other functions didn't ack
			 * changing the state to DEV_READY
			 */
			ql_log(ql_log_info, vha, 0xb023,
			    "%s : QUIESCENT TIMEOUT DRV_ACTIVE:%d "
			    "DRV_STATE:%d.\n", QLA2XXX_DRIVER_NAME,
			    drv_active, drv_state);
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			    QLA8XXX_DEV_READY);
			ql_log(ql_log_info, vha, 0xb025,
			    "HW State: DEV_READY.\n");
			qla82xx_idc_unlock(ha);
			qla2x00_perform_loop_resync(vha);
			qla82xx_idc_lock(ha);

			qla82xx_clear_qsnt_ready(vha);
			return;
		}

		qla82xx_idc_unlock(ha);
		msleep(1000);
		qla82xx_idc_lock(ha);

		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
		drv_active = drv_active << 0x01;
	}
	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	/* everyone acked so set the state to DEV_QUIESCENCE */
	if (dev_state == QLA8XXX_DEV_NEED_QUIESCENT) {
		ql_log(ql_log_info, vha, 0xb026,
		    "HW State: DEV_QUIESCENT.\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA8XXX_DEV_QUIESCENT);
	}
}

/*
* qla82xx_wait_for_state_change
*    Wait for device state to change from given current state
*
* Note:
*     IDC lock must not be held upon entry
*
* Return:
*    Changed device state.
*/
uint32_t
qla82xx_wait_for_state_change(scsi_qla_host_t *vha, uint32_t curr_state)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t dev_state;

	do {
		msleep(1000);
		qla82xx_idc_lock(ha);
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		qla82xx_idc_unlock(ha);
	} while (dev_state == curr_state);

	return dev_state;
}

void
qla8xxx_dev_failed_handler(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	/* Disable the board */
	ql_log(ql_log_fatal, vha, 0x00b8,
	    "Disabling the board.\n");

	if (IS_QLA82XX(ha)) {
		qla82xx_clear_drv_active(ha);
		qla82xx_idc_unlock(ha);
	} else if (IS_QLA8044(ha)) {
		qla8044_clear_drv_active(ha);
		qla8044_idc_unlock(ha);
	}

	/* Set DEV_FAILED flag to disable timer */
	vha->device_flags |= DFLG_DEV_FAILED;
	qla2x00_abort_all_cmds(vha, DID_NO_CONNECT << 16);
	qla2x00_mark_all_devices_lost(vha);
	vha->flags.online = 0;
	vha->flags.init_done = 0;
}

/*
 * qla82xx_need_reset_handler
 *    Code to start reset sequence
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return:
 *    Success : 0
 *    Failed  : 1
 */
static void
qla82xx_need_reset_handler(scsi_qla_host_t *vha)
{
	uint32_t dev_state, drv_state, drv_active;
	uint32_t active_mask = 0;
	unsigned long reset_timeout;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	if (vha->flags.online) {
		qla82xx_idc_unlock(ha);
		qla2x00_abort_isp_cleanup(vha);
		ha->isp_ops->get_flash_version(vha, req->ring);
		ha->isp_ops->nvram_config(vha);
		qla82xx_idc_lock(ha);
	}

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	if (!ha->flags.nic_core_reset_owner) {
		ql_dbg(ql_dbg_p3p, vha, 0xb028,
		    "reset_acknowledged by 0x%x\n", ha->portnum);
		qla82xx_set_rst_ready(ha);
	} else {
		active_mask = ~(QLA82XX_DRV_ACTIVE << (ha->portnum * 4));
		drv_active &= active_mask;
		ql_dbg(ql_dbg_p3p, vha, 0xb029,
		    "active_mask: 0x%08x\n", active_mask);
	}

	/* wait for 10 seconds for reset ack from all functions */
	reset_timeout = jiffies + (ha->fcoe_reset_timeout * HZ);

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);

	ql_dbg(ql_dbg_p3p, vha, 0xb02a,
	    "drv_state: 0x%08x, drv_active: 0x%08x, "
	    "dev_state: 0x%08x, active_mask: 0x%08x\n",
	    drv_state, drv_active, dev_state, active_mask);

	while (drv_state != drv_active &&
	    dev_state != QLA8XXX_DEV_INITIALIZING) {
		if (time_after_eq(jiffies, reset_timeout)) {
			ql_log(ql_log_warn, vha, 0x00b5,
			    "Reset timeout.\n");
			break;
		}
		qla82xx_idc_unlock(ha);
		msleep(1000);
		qla82xx_idc_lock(ha);
		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
		if (ha->flags.nic_core_reset_owner)
			drv_active &= active_mask;
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	}

	ql_dbg(ql_dbg_p3p, vha, 0xb02b,
	    "drv_state: 0x%08x, drv_active: 0x%08x, "
	    "dev_state: 0x%08x, active_mask: 0x%08x\n",
	    drv_state, drv_active, dev_state, active_mask);

	ql_log(ql_log_info, vha, 0x00b6,
	    "Device state is 0x%x = %s.\n",
	    dev_state, qdev_state(dev_state));

	/* Force to DEV_COLD unless someone else is starting a reset */
	if (dev_state != QLA8XXX_DEV_INITIALIZING &&
	    dev_state != QLA8XXX_DEV_COLD) {
		ql_log(ql_log_info, vha, 0x00b7,
		    "HW State: COLD/RE-INIT.\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA8XXX_DEV_COLD);
		qla82xx_set_rst_ready(ha);
		if (ql2xmdenable) {
			if (qla82xx_md_collect(vha))
				ql_log(ql_log_warn, vha, 0xb02c,
				    "Minidump not collected.\n");
		} else
			ql_log(ql_log_warn, vha, 0xb04f,
			    "Minidump disabled.\n");
	}
}

int
qla82xx_check_md_needed(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint16_t fw_major_version, fw_minor_version, fw_subminor_version;
	int rval = QLA_SUCCESS;

	fw_major_version = ha->fw_major_version;
	fw_minor_version = ha->fw_minor_version;
	fw_subminor_version = ha->fw_subminor_version;

	rval = qla2x00_get_fw_version(vha);
	if (rval != QLA_SUCCESS)
		return rval;

	if (ql2xmdenable) {
		if (!ha->fw_dumped) {
			if ((fw_major_version != ha->fw_major_version ||
			    fw_minor_version != ha->fw_minor_version ||
			    fw_subminor_version != ha->fw_subminor_version) ||
			    (ha->prev_minidump_failed)) {
				ql_dbg(ql_dbg_p3p, vha, 0xb02d,
				    "Firmware version differs Previous version: %d:%d:%d - New version: %d:%d:%d, prev_minidump_failed: %d.\n",
				    fw_major_version, fw_minor_version,
				    fw_subminor_version,
				    ha->fw_major_version,
				    ha->fw_minor_version,
				    ha->fw_subminor_version,
				    ha->prev_minidump_failed);
				/* Release MiniDump resources */
				qla82xx_md_free(vha);
				/* ALlocate MiniDump resources */
				qla82xx_md_prep(vha);
			}
		} else
			ql_log(ql_log_info, vha, 0xb02e,
			    "Firmware dump available to retrieve\n");
	}
	return rval;
}


static int
qla82xx_check_fw_alive(scsi_qla_host_t *vha)
{
	uint32_t fw_heartbeat_counter;
	int status = 0;

	fw_heartbeat_counter = qla82xx_rd_32(vha->hw,
		QLA82XX_PEG_ALIVE_COUNTER);
	/* all 0xff, assume AER/EEH in progress, ignore */
	if (fw_heartbeat_counter == 0xffffffff) {
		ql_dbg(ql_dbg_timer, vha, 0x6003,
		    "FW heartbeat counter is 0xffffffff, "
		    "returning status=%d.\n", status);
		return status;
	}
	if (vha->fw_heartbeat_counter == fw_heartbeat_counter) {
		vha->seconds_since_last_heartbeat++;
		/* FW not alive after 2 seconds */
		if (vha->seconds_since_last_heartbeat == 2) {
			vha->seconds_since_last_heartbeat = 0;
			status = 1;
		}
	} else
		vha->seconds_since_last_heartbeat = 0;
	vha->fw_heartbeat_counter = fw_heartbeat_counter;
	if (status)
		ql_dbg(ql_dbg_timer, vha, 0x6004,
		    "Returning status=%d.\n", status);
	return status;
}

/*
 * qla82xx_device_state_handler
 *	Main state handler
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return:
 *    Success : 0
 *    Failed  : 1
 */
int
qla82xx_device_state_handler(scsi_qla_host_t *vha)
{
	uint32_t dev_state;
	uint32_t old_dev_state;
	int rval = QLA_SUCCESS;
	unsigned long dev_init_timeout;
	struct qla_hw_data *ha = vha->hw;
	int loopcount = 0;

	qla82xx_idc_lock(ha);
	if (!vha->flags.init_done) {
		qla82xx_set_drv_active(vha);
		qla82xx_set_idc_version(vha);
	}

	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	old_dev_state = dev_state;
	ql_log(ql_log_info, vha, 0x009b,
	    "Device state is 0x%x = %s.\n",
	    dev_state, qdev_state(dev_state));

	/* wait for 30 seconds for device to go ready */
	dev_init_timeout = jiffies + (ha->fcoe_dev_init_timeout * HZ);

	while (1) {

		if (time_after_eq(jiffies, dev_init_timeout)) {
			ql_log(ql_log_fatal, vha, 0x009c,
			    "Device init failed.\n");
			rval = QLA_FUNCTION_FAILED;
			break;
		}
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		if (old_dev_state != dev_state) {
			loopcount = 0;
			old_dev_state = dev_state;
		}
		if (loopcount < 5) {
			ql_log(ql_log_info, vha, 0x009d,
			    "Device state is 0x%x = %s.\n",
			    dev_state, qdev_state(dev_state));
		}

		switch (dev_state) {
		case QLA8XXX_DEV_READY:
			ha->flags.nic_core_reset_owner = 0;
			goto rel_lock;
		case QLA8XXX_DEV_COLD:
			rval = qla82xx_device_bootstrap(vha);
			break;
		case QLA8XXX_DEV_INITIALIZING:
			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);
			break;
		case QLA8XXX_DEV_NEED_RESET:
			if (!ql2xdontresethba)
				qla82xx_need_reset_handler(vha);
			else {
				qla82xx_idc_unlock(ha);
				msleep(1000);
				qla82xx_idc_lock(ha);
			}
			dev_init_timeout = jiffies +
			    (ha->fcoe_dev_init_timeout * HZ);
			break;
		case QLA8XXX_DEV_NEED_QUIESCENT:
			qla82xx_need_qsnt_handler(vha);
			/* Reset timeout value after quiescence handler */
			dev_init_timeout = jiffies + (ha->fcoe_dev_init_timeout
							 * HZ);
			break;
		case QLA8XXX_DEV_QUIESCENT:
			/* Owner will exit and other will wait for the state
			 * to get changed
			 */
			if (ha->flags.quiesce_owner)
				goto rel_lock;

			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);

			/* Reset timeout value after quiescence handler */
			dev_init_timeout = jiffies + (ha->fcoe_dev_init_timeout
							 * HZ);
			break;
		case QLA8XXX_DEV_FAILED:
			qla8xxx_dev_failed_handler(vha);
			rval = QLA_FUNCTION_FAILED;
			goto exit;
		default:
			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);
		}
		loopcount++;
	}
rel_lock:
	qla82xx_idc_unlock(ha);
exit:
	return rval;
}

static int qla82xx_check_temp(scsi_qla_host_t *vha)
{
	uint32_t temp, temp_state, temp_val;
	struct qla_hw_data *ha = vha->hw;

	temp = qla82xx_rd_32(ha, CRB_TEMP_STATE);
	temp_state = qla82xx_get_temp_state(temp);
	temp_val = qla82xx_get_temp_val(temp);

	if (temp_state == QLA82XX_TEMP_PANIC) {
		ql_log(ql_log_warn, vha, 0x600e,
		    "Device temperature %d degrees C exceeds "
		    " maximum allowed. Hardware has been shut down.\n",
		    temp_val);
		return 1;
	} else if (temp_state == QLA82XX_TEMP_WARN) {
		ql_log(ql_log_warn, vha, 0x600f,
		    "Device temperature %d degrees C exceeds "
		    "operating range. Immediate action needed.\n",
		    temp_val);
	}
	return 0;
}

int qla82xx_read_temperature(scsi_qla_host_t *vha)
{
	uint32_t temp;

	temp = qla82xx_rd_32(vha->hw, CRB_TEMP_STATE);
	return qla82xx_get_temp_val(temp);
}

void qla82xx_clear_pending_mbx(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (ha->flags.mbox_busy) {
		ha->flags.mbox_int = 1;
		ha->flags.mbox_busy = 0;
		ql_log(ql_log_warn, vha, 0x6010,
		    "Doing premature completion of mbx command.\n");
		if (test_and_clear_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags))
			complete(&ha->mbx_intr_comp);
	}
}

void qla82xx_watchdog(scsi_qla_host_t *vha)
{
	uint32_t dev_state, halt_status;
	struct qla_hw_data *ha = vha->hw;

	/* don't poll if reset is going on */
	if (!ha->flags.nic_core_reset_hdlr_active) {
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		if (qla82xx_check_temp(vha)) {
			set_bit(ISP_UNRECOVERABLE, &vha->dpc_flags);
			ha->flags.isp82xx_fw_hung = 1;
			qla82xx_clear_pending_mbx(vha);
		} else if (dev_state == QLA8XXX_DEV_NEED_RESET &&
		    !test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags)) {
			ql_log(ql_log_warn, vha, 0x6001,
			    "Adapter reset needed.\n");
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		} else if (dev_state == QLA8XXX_DEV_NEED_QUIESCENT &&
			!test_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags)) {
			ql_log(ql_log_warn, vha, 0x6002,
			    "Quiescent needed.\n");
			set_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags);
		} else if (dev_state == QLA8XXX_DEV_FAILED &&
			!test_bit(ISP_UNRECOVERABLE, &vha->dpc_flags) &&
			vha->flags.online == 1) {
			ql_log(ql_log_warn, vha, 0xb055,
			    "Adapter state is failed. Offlining.\n");
			set_bit(ISP_UNRECOVERABLE, &vha->dpc_flags);
			ha->flags.isp82xx_fw_hung = 1;
			qla82xx_clear_pending_mbx(vha);
		} else {
			if (qla82xx_check_fw_alive(vha)) {
				ql_dbg(ql_dbg_timer, vha, 0x6011,
				    "disabling pause transmit on port 0 & 1.\n");
				qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x98,
				    CRB_NIU_XG_PAUSE_CTL_P0|CRB_NIU_XG_PAUSE_CTL_P1);
				halt_status = qla82xx_rd_32(ha,
				    QLA82XX_PEG_HALT_STATUS1);
				ql_log(ql_log_info, vha, 0x6005,
				    "dumping hw/fw registers:.\n "
				    " PEG_HALT_STATUS1: 0x%x, PEG_HALT_STATUS2: 0x%x,.\n "
				    " PEG_NET_0_PC: 0x%x, PEG_NET_1_PC: 0x%x,.\n "
				    " PEG_NET_2_PC: 0x%x, PEG_NET_3_PC: 0x%x,.\n "
				    " PEG_NET_4_PC: 0x%x.\n", halt_status,
				    qla82xx_rd_32(ha, QLA82XX_PEG_HALT_STATUS2),
				    qla82xx_rd_32(ha,
					    QLA82XX_CRB_PEG_NET_0 + 0x3c),
				    qla82xx_rd_32(ha,
					    QLA82XX_CRB_PEG_NET_1 + 0x3c),
				    qla82xx_rd_32(ha,
					    QLA82XX_CRB_PEG_NET_2 + 0x3c),
				    qla82xx_rd_32(ha,
					    QLA82XX_CRB_PEG_NET_3 + 0x3c),
				    qla82xx_rd_32(ha,
					    QLA82XX_CRB_PEG_NET_4 + 0x3c));
				if (((halt_status & 0x1fffff00) >> 8) == 0x67)
					ql_log(ql_log_warn, vha, 0xb052,
					    "Firmware aborted with "
					    "error code 0x00006700. Device is "
					    "being reset.\n");
				if (halt_status & HALT_STATUS_UNRECOVERABLE) {
					set_bit(ISP_UNRECOVERABLE,
					    &vha->dpc_flags);
				} else {
					ql_log(ql_log_info, vha, 0x6006,
					    "Detect abort  needed.\n");
					set_bit(ISP_ABORT_NEEDED,
					    &vha->dpc_flags);
				}
				ha->flags.isp82xx_fw_hung = 1;
				ql_log(ql_log_warn, vha, 0x6007, "Firmware hung.\n");
				qla82xx_clear_pending_mbx(vha);
			}
		}
	}
}

int qla82xx_load_risc(scsi_qla_host_t *vha, uint32_t *srisc_addr)
{
	int rval = -1;
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLA82XX(ha))
		rval = qla82xx_device_state_handler(vha);
	else if (IS_QLA8044(ha)) {
		qla8044_idc_lock(ha);
		/* Decide the reset ownership */
		qla83xx_reset_ownership(vha);
		qla8044_idc_unlock(ha);
		rval = qla8044_device_state_handler(vha);
	}
	return rval;
}

void
qla82xx_set_reset_owner(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t dev_state = 0;

	if (IS_QLA82XX(ha))
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	else if (IS_QLA8044(ha))
		dev_state = qla8044_rd_direct(vha, QLA8044_CRB_DEV_STATE_INDEX);

	if (dev_state == QLA8XXX_DEV_READY) {
		ql_log(ql_log_info, vha, 0xb02f,
		    "HW State: NEED RESET\n");
		if (IS_QLA82XX(ha)) {
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			    QLA8XXX_DEV_NEED_RESET);
			ha->flags.nic_core_reset_owner = 1;
			ql_dbg(ql_dbg_p3p, vha, 0xb030,
			    "reset_owner is 0x%x\n", ha->portnum);
		} else if (IS_QLA8044(ha))
			qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX,
			    QLA8XXX_DEV_NEED_RESET);
	} else
		ql_log(ql_log_info, vha, 0xb031,
		    "Device state is 0x%x = %s.\n",
		    dev_state, qdev_state(dev_state));
}

/*
 *  qla82xx_abort_isp
 *      Resets ISP and aborts all outstanding commands.
 *
 * Input:
 *      ha           = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
int
qla82xx_abort_isp(scsi_qla_host_t *vha)
{
	int rval = -1;
	struct qla_hw_data *ha = vha->hw;

	if (vha->device_flags & DFLG_DEV_FAILED) {
		ql_log(ql_log_warn, vha, 0x8024,
		    "Device in failed state, exiting.\n");
		return QLA_SUCCESS;
	}
	ha->flags.nic_core_reset_hdlr_active = 1;

	qla82xx_idc_lock(ha);
	qla82xx_set_reset_owner(vha);
	qla82xx_idc_unlock(ha);

	if (IS_QLA82XX(ha))
		rval = qla82xx_device_state_handler(vha);
	else if (IS_QLA8044(ha)) {
		qla8044_idc_lock(ha);
		/* Decide the reset ownership */
		qla83xx_reset_ownership(vha);
		qla8044_idc_unlock(ha);
		rval = qla8044_device_state_handler(vha);
	}

	qla82xx_idc_lock(ha);
	qla82xx_clear_rst_ready(ha);
	qla82xx_idc_unlock(ha);

	if (rval == QLA_SUCCESS) {
		ha->flags.isp82xx_fw_hung = 0;
		ha->flags.nic_core_reset_hdlr_active = 0;
		qla82xx_restart_isp(vha);
	}

	if (rval) {
		vha->flags.online = 1;
		if (test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
			if (ha->isp_abort_cnt == 0) {
				ql_log(ql_log_warn, vha, 0x8027,
				    "ISP error recover failed - board "
				    "disabled.\n");
				/*
				 * The next call disables the board
				 * completely.
				 */
				ha->isp_ops->reset_adapter(vha);
				vha->flags.online = 0;
				clear_bit(ISP_ABORT_RETRY,
				    &vha->dpc_flags);
				rval = QLA_SUCCESS;
			} else { /* schedule another ISP abort */
				ha->isp_abort_cnt--;
				ql_log(ql_log_warn, vha, 0x8036,
				    "ISP abort - retry remaining %d.\n",
				    ha->isp_abort_cnt);
				rval = QLA_FUNCTION_FAILED;
			}
		} else {
			ha->isp_abort_cnt = MAX_RETRIES_OF_ISP_ABORT;
			ql_dbg(ql_dbg_taskm, vha, 0x8029,
			    "ISP error recovery - retrying (%d) more times.\n",
			    ha->isp_abort_cnt);
			set_bit(ISP_ABORT_RETRY, &vha->dpc_flags);
			rval = QLA_FUNCTION_FAILED;
		}
	}
	return rval;
}

/*
 *  qla82xx_fcoe_ctx_reset
 *      Perform a quick reset and aborts all outstanding commands.
 *      This will only perform an FCoE context reset and avoids a full blown
 *      chip reset.
 *
 * Input:
 *      ha = adapter block pointer.
 *      is_reset_path = flag for identifying the reset path.
 *
 * Returns:
 *      0 = success
 */
int qla82xx_fcoe_ctx_reset(scsi_qla_host_t *vha)
{
	int rval = QLA_FUNCTION_FAILED;

	if (vha->flags.online) {
		/* Abort all outstanding commands, so as to be requeued later */
		qla2x00_abort_isp_cleanup(vha);
	}

	/* Stop currently executing firmware.
	 * This will destroy existing FCoE context at the F/W end.
	 */
	qla2x00_try_to_stop_firmware(vha);

	/* Restart. Creates a new FCoE context on INIT_FIRMWARE. */
	rval = qla82xx_restart_isp(vha);

	return rval;
}

/*
 * qla2x00_wait_for_fcoe_ctx_reset
 *    Wait till the FCoE context is reset.
 *
 * Note:
 *    Does context switching here.
 *    Release SPIN_LOCK (if any) before calling this routine.
 *
 * Return:
 *    Success (fcoe_ctx reset is done) : 0
 *    Failed  (fcoe_ctx reset not completed within max loop timout ) : 1
 */
int qla2x00_wait_for_fcoe_ctx_reset(scsi_qla_host_t *vha)
{
	int status = QLA_FUNCTION_FAILED;
	unsigned long wait_reset;

	wait_reset = jiffies + (MAX_LOOP_TIMEOUT * HZ);
	while ((test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags) ||
	    test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags))
	    && time_before(jiffies, wait_reset)) {

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);

		if (!test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags) &&
		    !test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags)) {
			status = QLA_SUCCESS;
			break;
		}
	}
	ql_dbg(ql_dbg_p3p, vha, 0xb027,
	       "%s: status=%d.\n", __func__, status);

	return status;
}

void
qla82xx_chip_reset_cleanup(scsi_qla_host_t *vha)
{
	int i, fw_state = 0;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;

	/* Check if 82XX firmware is alive or not
	 * We may have arrived here from NEED_RESET
	 * detection only
	 */
	if (!ha->flags.isp82xx_fw_hung) {
		for (i = 0; i < 2; i++) {
			msleep(1000);
			if (IS_QLA82XX(ha))
				fw_state = qla82xx_check_fw_alive(vha);
			else if (IS_QLA8044(ha))
				fw_state = qla8044_check_fw_alive(vha);
			if (fw_state) {
				ha->flags.isp82xx_fw_hung = 1;
				qla82xx_clear_pending_mbx(vha);
				break;
			}
		}
	}
	ql_dbg(ql_dbg_init, vha, 0x00b0,
	    "Entered %s fw_hung=%d.\n",
	    __func__, ha->flags.isp82xx_fw_hung);

	/* Abort all commands gracefully if fw NOT hung */
	if (!ha->flags.isp82xx_fw_hung) {
		int cnt, que;
		srb_t *sp;
		struct req_que *req;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		for (que = 0; que < ha->max_req_queues; que++) {
			req = ha->req_q_map[que];
			if (!req)
				continue;
			for (cnt = 1; cnt < req->num_outstanding_cmds; cnt++) {
				sp = req->outstanding_cmds[cnt];
				if (sp) {
					if ((!sp->u.scmd.crc_ctx ||
					    (sp->flags &
						SRB_FCP_CMND_DMA_VALID)) &&
						!ha->flags.isp82xx_fw_hung) {
						spin_unlock_irqrestore(
						    &ha->hardware_lock, flags);
						if (ha->isp_ops->abort_command(sp)) {
							ql_log(ql_log_info, vha,
							    0x00b1,
							    "mbx abort failed.\n");
						} else {
							ql_log(ql_log_info, vha,
							    0x00b2,
							    "mbx abort success.\n");
						}
						spin_lock_irqsave(&ha->hardware_lock, flags);
					}
				}
			}
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* Wait for pending cmds (physical and virtual) to complete */
		if (qla2x00_eh_wait_for_pending_commands(vha, 0, 0,
		    WAIT_HOST) == QLA_SUCCESS) {
			ql_dbg(ql_dbg_init, vha, 0x00b3,
			    "Done wait for "
			    "pending commands.\n");
		} else {
			WARN_ON_ONCE(true);
		}
	}
}

/* Minidump related functions */
static int
qla82xx_minidump_process_control(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla82xx_md_entry_crb *crb_entry;
	uint32_t read_value, opcode, poll_time;
	uint32_t addr, index, crb_addr;
	unsigned long wtime;
	struct qla82xx_md_template_hdr *tmplt_hdr;
	uint32_t rval = QLA_SUCCESS;
	int i;

	tmplt_hdr = (struct qla82xx_md_template_hdr *)ha->md_tmplt_hdr;
	crb_entry = (struct qla82xx_md_entry_crb *)entry_hdr;
	crb_addr = crb_entry->addr;

	for (i = 0; i < crb_entry->op_count; i++) {
		opcode = crb_entry->crb_ctrl.opcode;
		if (opcode & QLA82XX_DBG_OPCODE_WR) {
			qla82xx_md_rw_32(ha, crb_addr,
			    crb_entry->value_1, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_WR;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RW) {
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);
			qla82xx_md_rw_32(ha, crb_addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_RW;
		}

		if (opcode & QLA82XX_DBG_OPCODE_AND) {
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);
			read_value &= crb_entry->value_2;
			opcode &= ~QLA82XX_DBG_OPCODE_AND;
			if (opcode & QLA82XX_DBG_OPCODE_OR) {
				read_value |= crb_entry->value_3;
				opcode &= ~QLA82XX_DBG_OPCODE_OR;
			}
			qla82xx_md_rw_32(ha, crb_addr, read_value, 1);
		}

		if (opcode & QLA82XX_DBG_OPCODE_OR) {
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);
			read_value |= crb_entry->value_3;
			qla82xx_md_rw_32(ha, crb_addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_OR;
		}

		if (opcode & QLA82XX_DBG_OPCODE_POLL) {
			poll_time = crb_entry->crb_strd.poll_timeout;
			wtime = jiffies + poll_time;
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);

			do {
				if ((read_value & crb_entry->value_2)
				    == crb_entry->value_1)
					break;
				else if (time_after_eq(jiffies, wtime)) {
					/* capturing dump failed */
					rval = QLA_FUNCTION_FAILED;
					break;
				} else
					read_value = qla82xx_md_rw_32(ha,
					    crb_addr, 0, 0);
			} while (1);
			opcode &= ~QLA82XX_DBG_OPCODE_POLL;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RDSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else
				addr = crb_addr;

			read_value = qla82xx_md_rw_32(ha, addr, 0, 0);
			index = crb_entry->crb_ctrl.state_index_v;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_RDSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_WRSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else
				addr = crb_addr;

			if (crb_entry->crb_ctrl.state_index_v) {
				index = crb_entry->crb_ctrl.state_index_v;
				read_value =
				    tmplt_hdr->saved_state_array[index];
			} else
				read_value = crb_entry->value_1;

			qla82xx_md_rw_32(ha, addr, read_value, 1);
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
	return rval;
}

static void
qla82xx_minidump_process_rdocm(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla82xx_md_entry_rdocm *ocm_hdr;
	__le32 *data_ptr = *d_ptr;

	ocm_hdr = (struct qla82xx_md_entry_rdocm *)entry_hdr;
	r_addr = ocm_hdr->read_addr;
	r_stride = ocm_hdr->read_addr_stride;
	loop_cnt = ocm_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		r_value = rd_reg_dword(r_addr + ha->nx_pcibase);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += r_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_rdmux(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t r_addr, s_stride, s_addr, s_value, loop_cnt, i, r_value;
	struct qla82xx_md_entry_mux *mux_hdr;
	__le32 *data_ptr = *d_ptr;

	mux_hdr = (struct qla82xx_md_entry_mux *)entry_hdr;
	r_addr = mux_hdr->read_addr;
	s_addr = mux_hdr->select_addr;
	s_stride = mux_hdr->select_value_stride;
	s_value = mux_hdr->select_value;
	loop_cnt = mux_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, s_addr, s_value, 1);
		r_value = qla82xx_md_rw_32(ha, r_addr, 0, 0);
		*data_ptr++ = cpu_to_le32(s_value);
		*data_ptr++ = cpu_to_le32(r_value);
		s_value += s_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_rdcrb(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla82xx_md_entry_crb *crb_hdr;
	__le32 *data_ptr = *d_ptr;

	crb_hdr = (struct qla82xx_md_entry_crb *)entry_hdr;
	r_addr = crb_hdr->addr;
	r_stride = crb_hdr->crb_strd.addr_stride;
	loop_cnt = crb_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		r_value = qla82xx_md_rw_32(ha, r_addr, 0, 0);
		*data_ptr++ = cpu_to_le32(r_addr);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += r_stride;
	}
	*d_ptr = data_ptr;
}

static int
qla82xx_minidump_process_l2tag(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	unsigned long p_wait, w_time, p_mask;
	uint32_t c_value_w, c_value_r;
	struct qla82xx_md_entry_cache *cache_hdr;
	int rval = QLA_FUNCTION_FAILED;
	__le32 *data_ptr = *d_ptr;

	cache_hdr = (struct qla82xx_md_entry_cache *)entry_hdr;
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
		qla82xx_md_rw_32(ha, t_r_addr, t_value, 1);
		if (c_value_w)
			qla82xx_md_rw_32(ha, c_addr, c_value_w, 1);

		if (p_mask) {
			w_time = jiffies + p_wait;
			do {
				c_value_r = qla82xx_md_rw_32(ha, c_addr, 0, 0);
				if ((c_value_r & p_mask) == 0)
					break;
				else if (time_after_eq(jiffies, w_time)) {
					/* capturing dump failed */
					ql_dbg(ql_dbg_p3p, vha, 0xb032,
					    "c_value_r: 0x%x, poll_mask: 0x%lx, "
					    "w_time: 0x%lx\n",
					    c_value_r, p_mask, w_time);
					return rval;
				}
			} while (1);
		}

		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla82xx_md_rw_32(ha, addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

static void
qla82xx_minidump_process_l1cache(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	uint32_t c_value_w;
	struct qla82xx_md_entry_cache *cache_hdr;
	__le32 *data_ptr = *d_ptr;

	cache_hdr = (struct qla82xx_md_entry_cache *)entry_hdr;
	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;

	for (i = 0; i < loop_count; i++) {
		qla82xx_md_rw_32(ha, t_r_addr, t_value, 1);
		qla82xx_md_rw_32(ha, c_addr, c_value_w, 1);
		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla82xx_md_rw_32(ha, addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_queue(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t s_addr, r_addr;
	uint32_t r_stride, r_value, r_cnt, qid = 0;
	uint32_t i, k, loop_cnt;
	struct qla82xx_md_entry_queue *q_hdr;
	__le32 *data_ptr = *d_ptr;

	q_hdr = (struct qla82xx_md_entry_queue *)entry_hdr;
	s_addr = q_hdr->select_addr;
	r_cnt = q_hdr->rd_strd.read_addr_cnt;
	r_stride = q_hdr->rd_strd.read_addr_stride;
	loop_cnt = q_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, s_addr, qid, 1);
		r_addr = q_hdr->read_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla82xx_md_rw_32(ha, r_addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			r_addr += r_stride;
		}
		qid += q_hdr->q_strd.queue_id_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_rdrom(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t r_addr, r_value;
	uint32_t i, loop_cnt;
	struct qla82xx_md_entry_rdrom *rom_hdr;
	__le32 *data_ptr = *d_ptr;

	rom_hdr = (struct qla82xx_md_entry_rdrom *)entry_hdr;
	r_addr = rom_hdr->read_addr;
	loop_cnt = rom_hdr->read_data_size/sizeof(uint32_t);

	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, MD_DIRECT_ROM_WINDOW,
		    (r_addr & 0xFFFF0000), 1);
		r_value = qla82xx_md_rw_32(ha,
		    MD_DIRECT_ROM_READ_BASE +
		    (r_addr & 0x0000FFFF), 0, 0);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += sizeof(uint32_t);
	}
	*d_ptr = data_ptr;
}

static int
qla82xx_minidump_process_rdmem(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, __le32 **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t r_addr, r_value, r_data;
	uint32_t i, j, loop_cnt;
	struct qla82xx_md_entry_rdmem *m_hdr;
	unsigned long flags;
	int rval = QLA_FUNCTION_FAILED;
	__le32 *data_ptr = *d_ptr;

	m_hdr = (struct qla82xx_md_entry_rdmem *)entry_hdr;
	r_addr = m_hdr->read_addr;
	loop_cnt = m_hdr->read_data_size/16;

	if (r_addr & 0xf) {
		ql_log(ql_log_warn, vha, 0xb033,
		    "Read addr 0x%x not 16 bytes aligned\n", r_addr);
		return rval;
	}

	if (m_hdr->read_data_size % 16) {
		ql_log(ql_log_warn, vha, 0xb034,
		    "Read data[0x%x] not multiple of 16 bytes\n",
		    m_hdr->read_data_size);
		return rval;
	}

	ql_dbg(ql_dbg_p3p, vha, 0xb035,
	    "[%s]: rdmem_addr: 0x%x, read_data_size: 0x%x, loop_cnt: 0x%x\n",
	    __func__, r_addr, m_hdr->read_data_size, loop_cnt);

	write_lock_irqsave(&ha->hw_lock, flags);
	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_ADDR_LO, r_addr, 1);
		r_value = 0;
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_ADDR_HI, r_value, 1);
		r_value = MIU_TA_CTL_ENABLE;
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL, r_value, 1);
		r_value = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL, r_value, 1);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			r_value = qla82xx_md_rw_32(ha,
			    MD_MIU_TEST_AGT_CTRL, 0, 0);
			if ((r_value & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			printk_ratelimited(KERN_ERR
			    "failed to read through agent\n");
			write_unlock_irqrestore(&ha->hw_lock, flags);
			return rval;
		}

		for (j = 0; j < 4; j++) {
			r_data = qla82xx_md_rw_32(ha,
			    MD_MIU_TEST_AGT_RDDATA[j], 0, 0);
			*data_ptr++ = cpu_to_le32(r_data);
		}
		r_addr += 16;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);
	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

int
qla82xx_validate_template_chksum(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint64_t chksum = 0;
	uint32_t *d_ptr = (uint32_t *)ha->md_tmplt_hdr;
	int count = ha->md_template_size/sizeof(uint32_t);

	while (count-- > 0)
		chksum += *d_ptr++;
	while (chksum >> 32)
		chksum = (chksum & 0xFFFFFFFF) + (chksum >> 32);
	return ~chksum;
}

static void
qla82xx_mark_entry_skipped(scsi_qla_host_t *vha,
	qla82xx_md_entry_hdr_t *entry_hdr, int index)
{
	entry_hdr->d_ctrl.driver_flags |= QLA82XX_DBG_SKIPPED_FLAG;
	ql_dbg(ql_dbg_p3p, vha, 0xb036,
	    "Skipping entry[%d]: "
	    "ETYPE[0x%x]-ELEVEL[0x%x]\n",
	    index, entry_hdr->entry_type,
	    entry_hdr->d_ctrl.entry_capture_mask);
}

int
qla82xx_md_collect(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	int no_entry_hdr = 0;
	qla82xx_md_entry_hdr_t *entry_hdr;
	struct qla82xx_md_template_hdr *tmplt_hdr;
	__le32 *data_ptr;
	uint32_t total_data_size = 0, f_capture_mask, data_collected = 0;
	int i = 0, rval = QLA_FUNCTION_FAILED;

	tmplt_hdr = (struct qla82xx_md_template_hdr *)ha->md_tmplt_hdr;
	data_ptr = ha->md_dump;

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xb037,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n", ha->fw_dump);
		goto md_failed;
	}

	ha->fw_dumped = false;

	if (!ha->md_tmplt_hdr || !ha->md_dump) {
		ql_log(ql_log_warn, vha, 0xb038,
		    "Memory not allocated for minidump capture\n");
		goto md_failed;
	}

	if (ha->flags.isp82xx_no_md_cap) {
		ql_log(ql_log_warn, vha, 0xb054,
		    "Forced reset from application, "
		    "ignore minidump capture\n");
		ha->flags.isp82xx_no_md_cap = 0;
		goto md_failed;
	}

	if (qla82xx_validate_template_chksum(vha)) {
		ql_log(ql_log_info, vha, 0xb039,
		    "Template checksum validation error\n");
		goto md_failed;
	}

	no_entry_hdr = tmplt_hdr->num_of_entries;
	ql_dbg(ql_dbg_p3p, vha, 0xb03a,
	    "No of entry headers in Template: 0x%x\n", no_entry_hdr);

	ql_dbg(ql_dbg_p3p, vha, 0xb03b,
	    "Capture Mask obtained: 0x%x\n", tmplt_hdr->capture_debug_level);

	f_capture_mask = tmplt_hdr->capture_debug_level & 0xFF;

	/* Validate whether required debug level is set */
	if ((f_capture_mask & 0x3) != 0x3) {
		ql_log(ql_log_warn, vha, 0xb03c,
		    "Minimum required capture mask[0x%x] level not set\n",
		    f_capture_mask);
		goto md_failed;
	}
	tmplt_hdr->driver_capture_mask = ql2xmdcapmask;

	tmplt_hdr->driver_info[0] = vha->host_no;
	tmplt_hdr->driver_info[1] = (QLA_DRIVER_MAJOR_VER << 24) |
	    (QLA_DRIVER_MINOR_VER << 16) | (QLA_DRIVER_PATCH_VER << 8) |
	    QLA_DRIVER_BETA_VER;

	total_data_size = ha->md_dump_size;

	ql_dbg(ql_dbg_p3p, vha, 0xb03d,
	    "Total minidump data_size 0x%x to be captured\n", total_data_size);

	/* Check whether template obtained is valid */
	if (tmplt_hdr->entry_type != QLA82XX_TLHDR) {
		ql_log(ql_log_warn, vha, 0xb04e,
		    "Bad template header entry type: 0x%x obtained\n",
		    tmplt_hdr->entry_type);
		goto md_failed;
	}

	entry_hdr = (qla82xx_md_entry_hdr_t *)
	    (((uint8_t *)ha->md_tmplt_hdr) + tmplt_hdr->first_entry_offset);

	/* Walk through the entry headers */
	for (i = 0; i < no_entry_hdr; i++) {

		if (data_collected > total_data_size) {
			ql_log(ql_log_warn, vha, 0xb03e,
			    "More MiniDump data collected: [0x%x]\n",
			    data_collected);
			goto md_failed;
		}

		if (!(entry_hdr->d_ctrl.entry_capture_mask &
		    ql2xmdcapmask)) {
			entry_hdr->d_ctrl.driver_flags |=
			    QLA82XX_DBG_SKIPPED_FLAG;
			ql_dbg(ql_dbg_p3p, vha, 0xb03f,
			    "Skipping entry[%d]: "
			    "ETYPE[0x%x]-ELEVEL[0x%x]\n",
			    i, entry_hdr->entry_type,
			    entry_hdr->d_ctrl.entry_capture_mask);
			goto skip_nxt_entry;
		}

		ql_dbg(ql_dbg_p3p, vha, 0xb040,
		    "[%s]: data ptr[%d]: %p, entry_hdr: %p\n"
		    "entry_type: 0x%x, capture_mask: 0x%x\n",
		    __func__, i, data_ptr, entry_hdr,
		    entry_hdr->entry_type,
		    entry_hdr->d_ctrl.entry_capture_mask);

		ql_dbg(ql_dbg_p3p, vha, 0xb041,
		    "Data collected: [0x%x], Dump size left:[0x%x]\n",
		    data_collected, (ha->md_dump_size - data_collected));

		/* Decode the entry type and take
		 * required action to capture debug data */
		switch (entry_hdr->entry_type) {
		case QLA82XX_RDEND:
			qla82xx_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA82XX_CNTRL:
			rval = qla82xx_minidump_process_control(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla82xx_mark_entry_skipped(vha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_RDCRB:
			qla82xx_minidump_process_rdcrb(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDMEM:
			rval = qla82xx_minidump_process_rdmem(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla82xx_mark_entry_skipped(vha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_BOARD:
		case QLA82XX_RDROM:
			qla82xx_minidump_process_rdrom(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_L2DTG:
		case QLA82XX_L2ITG:
		case QLA82XX_L2DAT:
		case QLA82XX_L2INS:
			rval = qla82xx_minidump_process_l2tag(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla82xx_mark_entry_skipped(vha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_L1DAT:
		case QLA82XX_L1INS:
			qla82xx_minidump_process_l1cache(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDOCM:
			qla82xx_minidump_process_rdocm(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDMUX:
			qla82xx_minidump_process_rdmux(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_QUEUE:
			qla82xx_minidump_process_queue(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDNOP:
		default:
			qla82xx_mark_entry_skipped(vha, entry_hdr, i);
			break;
		}

		ql_dbg(ql_dbg_p3p, vha, 0xb042,
		    "[%s]: data ptr[%d]: %p\n", __func__, i, data_ptr);

		data_collected = (uint8_t *)data_ptr -
		    (uint8_t *)ha->md_dump;
skip_nxt_entry:
		entry_hdr = (qla82xx_md_entry_hdr_t *)
		    (((uint8_t *)entry_hdr) + entry_hdr->entry_size);
	}

	if (data_collected != total_data_size) {
		ql_dbg(ql_dbg_p3p, vha, 0xb043,
		    "MiniDump data mismatch: Data collected: [0x%x],"
		    "total_data_size:[0x%x]\n",
		    data_collected, total_data_size);
		goto md_failed;
	}

	ql_log(ql_log_info, vha, 0xb044,
	    "Firmware dump saved to temp buffer (%ld/%p %ld/%p).\n",
	    vha->host_no, ha->md_tmplt_hdr, vha->host_no, ha->md_dump);
	ha->fw_dumped = true;
	qla2x00_post_uevent_work(vha, QLA_UEVENT_CODE_FW_DUMP);

md_failed:
	return rval;
}

int
qla82xx_md_alloc(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	int i, k;
	struct qla82xx_md_template_hdr *tmplt_hdr;

	tmplt_hdr = (struct qla82xx_md_template_hdr *)ha->md_tmplt_hdr;

	if (ql2xmdcapmask < 0x3 || ql2xmdcapmask > 0x7F) {
		ql2xmdcapmask = tmplt_hdr->capture_debug_level & 0xFF;
		ql_log(ql_log_info, vha, 0xb045,
		    "Forcing driver capture mask to firmware default capture mask: 0x%x.\n",
		    ql2xmdcapmask);
	}

	for (i = 0x2, k = 1; (i & QLA82XX_DEFAULT_CAP_MASK); i <<= 1, k++) {
		if (i & ql2xmdcapmask)
			ha->md_dump_size += tmplt_hdr->capture_size_array[k];
	}

	if (ha->md_dump) {
		ql_log(ql_log_warn, vha, 0xb046,
		    "Firmware dump previously allocated.\n");
		return 1;
	}

	ha->md_dump = vmalloc(ha->md_dump_size);
	if (ha->md_dump == NULL) {
		ql_log(ql_log_warn, vha, 0xb047,
		    "Unable to allocate memory for Minidump size "
		    "(0x%x).\n", ha->md_dump_size);
		return 1;
	}
	return 0;
}

void
qla82xx_md_free(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	/* Release the template header allocated */
	if (ha->md_tmplt_hdr) {
		ql_log(ql_log_info, vha, 0xb048,
		    "Free MiniDump template: %p, size (%d KB)\n",
		    ha->md_tmplt_hdr, ha->md_template_size / 1024);
		dma_free_coherent(&ha->pdev->dev, ha->md_template_size,
		    ha->md_tmplt_hdr, ha->md_tmplt_hdr_dma);
		ha->md_tmplt_hdr = NULL;
	}

	/* Release the template data buffer allocated */
	if (ha->md_dump) {
		ql_log(ql_log_info, vha, 0xb049,
		    "Free MiniDump memory: %p, size (%d KB)\n",
		    ha->md_dump, ha->md_dump_size / 1024);
		vfree(ha->md_dump);
		ha->md_dump_size = 0;
		ha->md_dump = NULL;
	}
}

void
qla82xx_md_prep(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	int rval;

	/* Get Minidump template size */
	rval = qla82xx_md_get_template_size(vha);
	if (rval == QLA_SUCCESS) {
		ql_log(ql_log_info, vha, 0xb04a,
		    "MiniDump Template size obtained (%d KB)\n",
		    ha->md_template_size / 1024);

		/* Get Minidump template */
		if (IS_QLA8044(ha))
			rval = qla8044_md_get_template(vha);
		else
			rval = qla82xx_md_get_template(vha);

		if (rval == QLA_SUCCESS) {
			ql_dbg(ql_dbg_p3p, vha, 0xb04b,
			    "MiniDump Template obtained\n");

			/* Allocate memory for minidump */
			rval = qla82xx_md_alloc(vha);
			if (rval == QLA_SUCCESS)
				ql_log(ql_log_info, vha, 0xb04c,
				    "MiniDump memory allocated (%d KB)\n",
				    ha->md_dump_size / 1024);
			else {
				ql_log(ql_log_info, vha, 0xb04d,
				    "Free MiniDump template: %p, size: (%d KB)\n",
				    ha->md_tmplt_hdr,
				    ha->md_template_size / 1024);
				dma_free_coherent(&ha->pdev->dev,
				    ha->md_template_size,
				    ha->md_tmplt_hdr, ha->md_tmplt_hdr_dma);
				ha->md_tmplt_hdr = NULL;
			}

		}
	}
}

int
qla82xx_beacon_on(struct scsi_qla_host *vha)
{

	int rval;
	struct qla_hw_data *ha = vha->hw;

	qla82xx_idc_lock(ha);
	rval = qla82xx_mbx_beacon_ctl(vha, 1);

	if (rval) {
		ql_log(ql_log_warn, vha, 0xb050,
		    "mbx set led config failed in %s\n", __func__);
		goto exit;
	}
	ha->beacon_blink_led = 1;
exit:
	qla82xx_idc_unlock(ha);
	return rval;
}

int
qla82xx_beacon_off(struct scsi_qla_host *vha)
{

	int rval;
	struct qla_hw_data *ha = vha->hw;

	qla82xx_idc_lock(ha);
	rval = qla82xx_mbx_beacon_ctl(vha, 0);

	if (rval) {
		ql_log(ql_log_warn, vha, 0xb051,
		    "mbx set led config failed in %s\n", __func__);
		goto exit;
	}
	ha->beacon_blink_led = 0;
exit:
	qla82xx_idc_unlock(ha);
	return rval;
}

void
qla82xx_fw_dump(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!ha->allow_cna_fw_dump)
		return;

	scsi_block_requests(vha->host);
	ha->flags.isp82xx_no_md_cap = 1;
	qla82xx_idc_lock(ha);
	qla82xx_set_reset_owner(vha);
	qla82xx_idc_unlock(ha);
	qla2x00_wait_for_chip_reset(vha);
	scsi_unblock_requests(vha->host);
}
