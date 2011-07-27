/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include <linux/delay.h>
#include <linux/pci.h>
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
int qla82xx_crb_table_initialized;

#define qla82xx_crb_addr_transform(name) \
	(crb_addr_xform[QLA82XX_HW_PX_MAP_CRB_##name] = \
	QLA82XX_HW_CRB_HUB_AGT_ADR_##name << 20)

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

struct crb_128M_2M_block_map crb_128M_2M_map[64] = {
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
unsigned qla82xx_crb_hub_agt[64] = {
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
char *qdev_state[] = {
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
qla82xx_pci_set_crbwindow_2M(struct qla_hw_data *ha, ulong *off)
{
	u32 win_read;

	ha->crb_win = CRB_HI(*off);
	writel(ha->crb_win,
		(void *)(CRB_WINDOW_2M + ha->nx_pcibase));

	/* Read back value to make sure write has gone through before trying
	 * to use it.
	 */
	win_read = RD_REG_DWORD((void *)(CRB_WINDOW_2M + ha->nx_pcibase));
	if (win_read != ha->crb_win) {
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "%s: Written crbwin (0x%x) != Read crbwin (0x%x), "
		    "off=0x%lx\n", __func__, ha->crb_win, win_read, *off));
	}
	*off = (*off & MASK(16)) + CRB_INDIRECT_2M + ha->nx_pcibase;
}

static inline unsigned long
qla82xx_pci_set_crbwindow(struct qla_hw_data *ha, u64 off)
{
	/* See if we are currently pointing to the region we want to use next */
	if ((off >= QLA82XX_CRB_PCIX_HOST) && (off < QLA82XX_CRB_DDR_NET)) {
		/* No need to change window. PCIX and PCIEregs are in both
		 * regs are in both windows.
		 */
		return off;
	}

	if ((off >= QLA82XX_CRB_PCIX_HOST) && (off < QLA82XX_CRB_PCIX_HOST2)) {
		/* We are in first CRB window */
		if (ha->curr_window != 0)
			WARN_ON(1);
		return off;
	}

	if ((off > QLA82XX_CRB_PCIX_HOST2) && (off < QLA82XX_CRB_MAX)) {
		/* We are in second CRB window */
		off = off - QLA82XX_CRB_PCIX_HOST2 + QLA82XX_CRB_PCIX_HOST;

		if (ha->curr_window != 1)
			return off;

		/* We are in the QM or direct access
		 * register region - do nothing
		 */
		if ((off >= QLA82XX_PCI_DIRECT_CRB) &&
			(off < QLA82XX_PCI_CAMQM_MAX))
			return off;
	}
	/* strange address given */
	qla_printk(KERN_WARNING, ha,
		"%s: Warning: unm_nic_pci_set_crbwindow called with"
		" an unknown address(%llx)\n", QLA2XXX_DRIVER_NAME, off);
	return off;
}

static int
qla82xx_pci_get_crb_addr_2M(struct qla_hw_data *ha, ulong *off)
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

	/* Try direct map */
	m = &crb_128M_2M_map[CRB_BLK(*off)].sub_block[CRB_SUBBLK(*off)];

	if (m->valid && (m->start_128M <= *off) && (m->end_128M > *off)) {
		*off = *off + m->start_2M - m->start_128M + ha->nx_pcibase;
		return 0;
	}
	/* Not in direct map, use crb window */
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
qla82xx_wr_32(struct qla_hw_data *ha, ulong off, u32 data)
{
	unsigned long flags = 0;
	int rv;

	rv = qla82xx_pci_get_crb_addr_2M(ha, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
		write_lock_irqsave(&ha->hw_lock, flags);
		qla82xx_crb_win_lock(ha);
		qla82xx_pci_set_crbwindow_2M(ha, &off);
	}

	writel(data, (void __iomem *)off);

	if (rv == 1) {
		qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_UNLOCK));
		write_unlock_irqrestore(&ha->hw_lock, flags);
	}
	return 0;
}

int
qla82xx_rd_32(struct qla_hw_data *ha, ulong off)
{
	unsigned long flags = 0;
	int rv;
	u32 data;

	rv = qla82xx_pci_get_crb_addr_2M(ha, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
		write_lock_irqsave(&ha->hw_lock, flags);
		qla82xx_crb_win_lock(ha);
		qla82xx_pci_set_crbwindow_2M(ha, &off);
	}
	data = RD_REG_DWORD((void __iomem *)off);

	if (rv == 1) {
		qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_UNLOCK));
		write_unlock_irqrestore(&ha->hw_lock, flags);
	}
	return data;
}

#define IDC_LOCK_TIMEOUT 100000000
int qla82xx_idc_lock(struct qla_hw_data *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore5 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_LOCK));
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
				cpu_relax();
		}
	}

	return 0;
}

void qla82xx_idc_unlock(struct qla_hw_data *ha)
{
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_UNLOCK));
}

/*  PCI Windowing for DDR regions.  */
#define QLA82XX_ADDR_IN_RANGE(addr, low, high) \
	(((addr) <= (high)) && ((addr) >= (low)))
/*
 * check memory access boundary.
 * used by test agent. support ddr access only for now
 */
static unsigned long
qla82xx_pci_mem_bound_check(struct qla_hw_data *ha,
	unsigned long long addr, int size)
{
	if (!QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX) ||
		!QLA82XX_ADDR_IN_RANGE(addr + size - 1, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX) ||
		((size != 1) && (size != 2) && (size != 4) && (size != 8)))
			return 0;
	else
		return 1;
}

int qla82xx_pci_set_window_warning_count;

static unsigned long
qla82xx_pci_set_window(struct qla_hw_data *ha, unsigned long long addr)
{
	int window;
	u32 win_read;

	if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		window = MN_WIN(addr);
		ha->ddr_mn_window = window;
		qla82xx_wr_32(ha,
			ha->mn_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha,
			ha->mn_win_crb | QLA82XX_PCI_CRBSPACE);
		if ((win_read << 17) != window) {
			qla_printk(KERN_WARNING, ha,
			    "%s: Written MNwin (0x%x) != Read MNwin (0x%x)\n",
			    __func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_DDR_NET;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM0,
		QLA82XX_ADDR_OCM0_MAX)) {
		unsigned int temp1;
		if ((addr & 0x00ff800) == 0xff800) {
			qla_printk(KERN_WARNING, ha,
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
			qla_printk(KERN_WARNING, ha,
			    "%s: Written OCMwin (0x%x) != Read OCMwin (0x%x)\n",
			    __func__, temp1, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_OCM0_2M;

	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_QDR_NET,
		QLA82XX_P3_ADDR_QDR_NET_MAX)) {
		/* QDR network side */
		window = MS_WIN(addr);
		ha->qdr_sn_window = window;
		qla82xx_wr_32(ha,
			ha->ms_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha,
			ha->ms_win_crb | QLA82XX_PCI_CRBSPACE);
		if (win_read != window) {
			qla_printk(KERN_WARNING, ha,
			    "%s: Written MSwin (0x%x) != Read MSwin (0x%x)\n",
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
			qla_printk(KERN_WARNING, ha,
			    "%s: Warning:%s Unknown address range!\n", __func__,
			    QLA2XXX_DRIVER_NAME);
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
	if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
		QLA82XX_ADDR_DDR_NET_MAX))
		BUG();
	else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM0,
		QLA82XX_ADDR_OCM0_MAX))
		return 1;
	else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM1,
		QLA82XX_ADDR_OCM1_MAX))
		return 1;
	else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_QDR_NET, qdr_max)) {
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
	void           *addr = NULL;
	int             ret = 0;
	u64             start;
	uint8_t         *mem_ptr = NULL;
	unsigned long   mem_base;
	unsigned long   mem_page;

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla82xx_pci_set_window(ha, off);
	if ((start == -1UL) ||
		(qla82xx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		qla_printk(KERN_ERR, ha,
			"%s out of bound pci memory access. "
			"offset is 0x%llx\n", QLA2XXX_DRIVER_NAME, off);
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
	if (mem_ptr == 0UL) {
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
	void           *addr = NULL;
	int             ret = 0;
	u64             start;
	uint8_t         *mem_ptr = NULL;
	unsigned long   mem_base;
	unsigned long   mem_page;

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla82xx_pci_set_window(ha, off);
	if ((start == -1UL) ||
		(qla82xx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		qla_printk(KERN_ERR, ha,
			"%s out of bound pci memory access. "
			"offset is 0x%llx\n", QLA2XXX_DRIVER_NAME, off);
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
	if (mem_ptr == 0UL)
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

	while (!done) {
		/* acquire semaphore2 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_LOCK));
		if (done == 1)
			break;
		if (timeout >= qla82xx_rom_lock_timeout)
			return -1;
		timeout++;
	}
	qla82xx_wr_32(ha, QLA82XX_ROM_LOCK_ID, ROM_LOCK_DRIVER);
	return 0;
}

static void
qla82xx_rom_unlock(struct qla_hw_data *ha)
{
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_UNLOCK));
}

static int
qla82xx_wait_rom_busy(struct qla_hw_data *ha)
{
	long timeout = 0;
	long done = 0 ;

	while (done == 0) {
		done = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 4;
		timeout++;
		if (timeout >= rom_max_timeout) {
			DEBUG(qla_printk(KERN_INFO, ha,
				"%s: Timeout reached waiting for rom busy",
				QLA2XXX_DRIVER_NAME));
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

	while (done == 0) {
		done = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 2;
		timeout++;
		if (timeout >= rom_max_timeout) {
			DEBUG(qla_printk(KERN_INFO, ha,
				"%s: Timeout reached  waiting for rom done",
				QLA2XXX_DRIVER_NAME));
			return -1;
		}
	}
	return 0;
}

static int
qla82xx_do_rom_fast_read(struct qla_hw_data *ha, int addr, int *valp)
{
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, addr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
			"%s: Error waiting for rom done\n",
			QLA2XXX_DRIVER_NAME);
		return -1;
	}
	/* Reset abyte_cnt and dummy_byte_cnt */
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	udelay(10);
	cond_resched();
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);
	*valp = qla82xx_rd_32(ha, QLA82XX_ROMUSB_ROM_RDATA);
	return 0;
}

static int
qla82xx_rom_fast_read(struct qla_hw_data *ha, int addr, int *valp)
{
	int ret, loops = 0;

	while ((qla82xx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		schedule();
		loops++;
	}
	if (loops >= 50000) {
		qla_printk(KERN_INFO, ha,
			"%s: qla82xx_rom_lock failed\n",
			QLA2XXX_DRIVER_NAME);
		return -1;
	}
	ret = qla82xx_do_rom_fast_read(ha, addr, valp);
	qla82xx_rom_unlock(ha);
	return ret;
}

static int
qla82xx_read_status_reg(struct qla_hw_data *ha, uint32_t *val)
{
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_RDSR);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		return -1;
	}
	*val = qla82xx_rd_32(ha, QLA82XX_ROMUSB_ROM_RDATA);
	return 0;
}

static int
qla82xx_flash_wait_write_finish(struct qla_hw_data *ha)
{
	long timeout = 0;
	uint32_t done = 1 ;
	uint32_t val;
	int ret = 0;

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);
	while ((done != 0) && (ret == 0)) {
		ret = qla82xx_read_status_reg(ha, &val);
		done = val & 1;
		timeout++;
		udelay(10);
		cond_resched();
		if (timeout >= 50000) {
			qla_printk(KERN_WARNING, ha,
			    "Timeout reached  waiting for write finish");
			return -1;
		}
	}
	return ret;
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
	if (qla82xx_flash_set_write_enable(ha))
		return -1;
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_WDATA, val);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, 0x1);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		return -1;
	}
	return qla82xx_flash_wait_write_finish(ha);
}

static int
qla82xx_write_disable_flash(struct qla_hw_data *ha)
{
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_WRDI);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		return -1;
	}
	return 0;
}

static int
ql82xx_rom_lock_d(struct qla_hw_data *ha)
{
	int loops = 0;
	while ((qla82xx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		cond_resched();
		loops++;
	}
	if (loops >= 50000) {
		qla_printk(KERN_WARNING, ha, "ROM lock failed\n");
		return -1;
	}
	return 0;;
}

static int
qla82xx_write_flash_dword(struct qla_hw_data *ha, uint32_t flashaddr,
	uint32_t data)
{
	int ret = 0;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_WARNING, ha, "ROM Lock failed\n");
		return ret;
	}

	if (qla82xx_flash_set_write_enable(ha))
		goto done_write;

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_WDATA, data);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, flashaddr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_PP);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
			"Error waiting for rom done\n");
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

	/* Halt all the indiviual PEGs and other blocks of the ISP */
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

	/* reset ms */
	val = qla82xx_rd_32(ha, QLA82XX_CRB_QDR_NET + 0xe4);
	val |= (1 << 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_QDR_NET + 0xe4, val);
	msleep(20);

	/* unreset ms */
	val = qla82xx_rd_32(ha, QLA82XX_CRB_QDR_NET + 0xe4);
	val &= ~(1 << 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_QDR_NET + 0xe4, val);
	msleep(20);

	qla82xx_rom_unlock(ha);

	/* Read the signature value from the flash.
	 * Offset 0: Contain signature (0xcafecafe)
	 * Offset 4: Offset and number of addr/value pairs
	 * that present in CRB initialize sequence
	 */
	if (qla82xx_rom_fast_read(ha, 0, &n) != 0 || n != 0xcafecafeUL ||
	    qla82xx_rom_fast_read(ha, 4, &n) != 0) {
		qla_printk(KERN_WARNING, ha,
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
		qla_printk(KERN_WARNING, ha,
		    "%s: %s:n=0x%x [ERROR] Card flash not initialized.\n",
		    QLA2XXX_DRIVER_NAME, __func__, n);
		return -1;
	}

	qla_printk(KERN_INFO, ha,
	    "%s: %d CRB init values found in ROM.\n", QLA2XXX_DRIVER_NAME, n);

	buf = kmalloc(n * sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL) {
		qla_printk(KERN_WARNING, ha,
		    "%s: [ERROR] Unable to malloc memory.\n",
		    QLA2XXX_DRIVER_NAME);
		return -1;
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
			qla_printk(KERN_WARNING, ha,
			    "%s: [ERROR] Unknown addr: 0x%08lx\n",
			    QLA2XXX_DRIVER_NAME, buf[i].addr);
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
				    "failed to write through agent\n");
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
				    "failed to read through agent\n");
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
	__le32 offset;
	__le32 tab_type;
	__le32 entries = cpu_to_le32(directory->num_entries);

	for (i = 0; i < entries; i++) {
		offset = cpu_to_le32(directory->findex) +
		    (i * cpu_to_le32(directory->entry_size));
		tab_type = cpu_to_le32(*((u32 *)&unirom[offset] + 8));

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
	int idx = cpu_to_le32(*((int *)&unirom[ha->file_prd_off] + idx_offset));
	struct qla82xx_uri_table_desc *tab_desc = NULL;
	__le32 offset;

	tab_desc = qla82xx_get_table_desc(unirom, section);
	if (!tab_desc)
		return NULL;

	offset = cpu_to_le32(tab_desc->findex) +
	    (cpu_to_le32(tab_desc->entry_size) * idx);

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
			offset = cpu_to_le32(uri_desc->findex);
	}

	return (u8 *)&ha->hablob->fw->data[offset];
}

static __le32
qla82xx_get_fw_size(struct qla_hw_data *ha)
{
	struct qla82xx_uri_data_desc *uri_desc = NULL;

	if (ha->fw_type == QLA82XX_UNIFIED_ROMIMAGE) {
		uri_desc =  qla82xx_get_data_desc(ha, QLA82XX_URI_DIR_SECT_FW,
		    QLA82XX_URI_FIRMWARE_IDX_OFF);
		if (uri_desc)
			return cpu_to_le32(uri_desc->size);
	}

	return cpu_to_le32(*(u32 *)&ha->hablob->fw->data[FW_SIZE_OFFSET]);
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
			offset = cpu_to_le32(uri_desc->findex);
	}

	return (u8 *)&ha->hablob->fw->data[offset];
}

/* PCI related functions */
char *
qla82xx_pci_info_str(struct scsi_qla_host *vha, char *str)
{
	int pcie_reg;
	struct qla_hw_data *ha = vha->hw;
	char lwstr[6];
	uint16_t lnk;

	pcie_reg = pci_find_capability(ha->pdev, PCI_CAP_ID_EXP);
	pci_read_config_word(ha->pdev, pcie_reg + PCI_EXP_LNKSTA, &lnk);
	ha->link_width = (lnk >> 4) & 0x3f;

	strcpy(str, "PCIe (");
	strcat(str, "2.5Gb/s ");
	snprintf(lwstr, sizeof(lwstr), "x%d)", ha->link_width);
	strcat(str, lwstr);
	return str;
}

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
		qla_printk(KERN_WARNING, ha,
			"Failed to reserve selected regions (%s)\n",
			pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	/* Use MMIO operations for all accesses. */
	if (!(pci_resource_flags(ha->pdev, 0) & IORESOURCE_MEM)) {
		qla_printk(KERN_ERR, ha,
			"region #0 not an MMIO resource (%s), aborting\n",
			pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	len = pci_resource_len(ha->pdev, 0);
	ha->nx_pcibase =
	    (unsigned long)ioremap(pci_resource_start(ha->pdev, 0), len);
	if (!ha->nx_pcibase) {
		qla_printk(KERN_ERR, ha,
		    "cannot remap pcibase MMIO (%s), aborting\n",
		    pci_name(ha->pdev));
		pci_release_regions(ha->pdev);
		goto iospace_error_exit;
	}

	/* Mapping of IO base pointer */
	ha->iobase = (device_reg_t __iomem *)((uint8_t *)ha->nx_pcibase +
	    0xbc000 + (ha->pdev->devfn << 11));

	if (!ql2xdbwr) {
		ha->nxdb_wr_ptr =
		    (unsigned long)ioremap((pci_resource_start(ha->pdev, 4) +
		    (ha->pdev->devfn << 12)), 4);
		if (!ha->nxdb_wr_ptr) {
			qla_printk(KERN_ERR, ha,
			    "cannot remap MMIO (%s), aborting\n",
			    pci_name(ha->pdev));
			pci_release_regions(ha->pdev);
			goto iospace_error_exit;
		}

		/* Mapping of IO base pointer,
		 * door bell read and write pointer
		 */
		ha->nxdb_rd_ptr = (uint8_t *) ha->nx_pcibase + (512 * 1024) +
		    (ha->pdev->devfn * 8);
	} else {
		ha->nxdb_wr_ptr = (ha->pdev->devfn == 6 ?
			QLA82XX_CAMRAM_DB1 :
			QLA82XX_CAMRAM_DB2);
	}

	ha->max_req_queues = ha->max_rsp_queues = 1;
	ha->msix_count = ha->max_rsp_queues + 1;
	return 0;

iospace_error_exit:
	return -ENOMEM;
}

/* GS related functions */

/* Initialization related functions */

/**
 * qla82xx_pci_config() - Setup ISP82xx PCI configuration registers.
 * @ha: HA context
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
	return 0;
}

/**
 * qla82xx_reset_chip() - Setup ISP82xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
void
qla82xx_reset_chip(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	ha->isp_ops->disable_intrs(ha);
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
	icb->request_q_outpointer = __constant_cpu_to_le16(0);
	icb->response_q_inpointer = __constant_cpu_to_le16(0);
	icb->request_q_length = cpu_to_le16(req->length);
	icb->response_q_length = cpu_to_le16(rsp->length);
	icb->request_q_address[0] = cpu_to_le32(LSD(req->dma));
	icb->request_q_address[1] = cpu_to_le32(MSD(req->dma));
	icb->response_q_address[0] = cpu_to_le32(LSD(rsp->dma));
	icb->response_q_address[1] = cpu_to_le32(MSD(rsp->dma));

	WRT_REG_DWORD((unsigned long  __iomem *)&reg->req_q_out[0], 0);
	WRT_REG_DWORD((unsigned long  __iomem *)&reg->rsp_q_in[0], 0);
	WRT_REG_DWORD((unsigned long  __iomem *)&reg->rsp_q_out[0], 0);
}

void qla82xx_reset_adapter(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	vha->flags.online = 0;
	qla2x00_try_to_stop_firmware(vha);
	ha->isp_ops->disable_intrs(ha);
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
	size = (__force u32)qla82xx_get_fw_size(ha) / 8;
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
	__le32 entries;
	__le32 flags, file_chiprev, offset;
	uint8_t chiprev = ha->chip_revision;
	/* Hardcoding mn_present flag for P3P */
	int mn_present = 0;
	uint32_t flagbit;

	ptab_desc = qla82xx_get_table_desc(unirom,
		 QLA82XX_URI_DIR_SECT_PRODUCT_TBL);
       if (!ptab_desc)
		return -1;

	entries = cpu_to_le32(ptab_desc->num_entries);

	for (i = 0; i < entries; i++) {
		offset = cpu_to_le32(ptab_desc->findex) +
			(i * cpu_to_le32(ptab_desc->entry_size));
		flags = cpu_to_le32(*((int *)&unirom[offset] +
			QLA82XX_URI_FLAGS_OFF));
		file_chiprev = cpu_to_le32(*((int *)&unirom[offset] +
			QLA82XX_URI_CHIP_REV_OFF));

		flagbit = mn_present ? 1 : 2;

		if ((chiprev == file_chiprev) && ((1ULL << flagbit) & flags)) {
			ha->file_prd_off = offset;
			return 0;
		}
	}
	return -1;
}

int
qla82xx_validate_firmware_blob(scsi_qla_host_t *vha, uint8_t fw_type)
{
	__le32 val;
	uint32_t min_size;
	struct qla_hw_data *ha = vha->hw;
	const struct firmware *fw = ha->hablob->fw;

	ha->fw_type = fw_type;

	if (fw_type == QLA82XX_UNIFIED_ROMIMAGE) {
		if (qla82xx_set_product_offset(ha))
			return -EINVAL;

		min_size = QLA82XX_URI_FW_MIN_SIZE;
	} else {
		val = cpu_to_le32(*(u32 *)&fw->data[QLA82XX_FW_MAGIC_OFFSET]);
		if ((__force u32)val != QLA82XX_BDINFO_MAGIC)
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
		qla_printk(KERN_WARNING, ha,
			"CRB_CMDPEG_STATE: 0x%x and retries: 0x%x\n",
			val, retries);

		msleep(500);

	} while (--retries);

	qla_printk(KERN_INFO, ha,
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

		qla_printk(KERN_WARNING, ha,
			"CRB_RCVPEG_STATE: 0x%x and retries: 0x%x\n",
			val, retries);

		msleep(500);

	} while (--retries);

	qla_printk(KERN_INFO, ha,
		"Rcv Peg initialization failed: 0x%x.\n", val);
	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, CRB_RCVPEG_STATE, PHAN_INITIALIZE_FAILED);
	read_unlock(&ha->hw_lock);
	return QLA_FUNCTION_FAILED;
}

/* ISR related functions */
uint32_t qla82xx_isr_int_target_mask_enable[8] = {
	ISR_INT_TARGET_MASK, ISR_INT_TARGET_MASK_F1,
	ISR_INT_TARGET_MASK_F2, ISR_INT_TARGET_MASK_F3,
	ISR_INT_TARGET_MASK_F4, ISR_INT_TARGET_MASK_F5,
	ISR_INT_TARGET_MASK_F7, ISR_INT_TARGET_MASK_F7
};

uint32_t qla82xx_isr_int_target_status[8] = {
	ISR_INT_TARGET_STATUS, ISR_INT_TARGET_STATUS_F1,
	ISR_INT_TARGET_STATUS_F2, ISR_INT_TARGET_STATUS_F3,
	ISR_INT_TARGET_STATUS_F4, ISR_INT_TARGET_STATUS_F5,
	ISR_INT_TARGET_STATUS_F7, ISR_INT_TARGET_STATUS_F7
};

static struct qla82xx_legacy_intr_set legacy_intr[] = \
	QLA82XX_LEGACY_INTR_CONFIG;

/*
 * qla82xx_mbx_completion() - Process mailbox command completions.
 * @ha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
static void
qla82xx_mbx_completion(scsi_qla_host_t *vha, uint16_t mb0)
{
	uint16_t	cnt;
	uint16_t __iomem *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;
	wptr = (uint16_t __iomem *)&reg->mailbox_out[1];

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		ha->mailbox_out[cnt] = RD_REG_WORD(wptr);
		wptr++;
	}

	if (ha->mcp) {
		DEBUG3_11(printk(KERN_INFO "%s(%ld): "
			"Got mailbox completion. cmd=%x.\n",
			__func__, vha->host_no, ha->mcp->mb[0]));
	} else {
		qla_printk(KERN_INFO, ha,
			"%s(%ld): MBX pointer ERROR!\n",
			__func__, vha->host_no);
	}
}

/*
 * qla82xx_intr_handler() - Process interrupts for the ISP23xx and ISP63xx.
 * @irq:
 * @dev_id: SCSI driver HA context
 * @regs:
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
	uint32_t	stat;
	uint16_t	mb[4];

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer\n", __func__);
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

		if (RD_REG_DWORD(&reg->host_int)) {
			stat = RD_REG_DWORD(&reg->host_status);

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
				mb[1] = RD_REG_WORD(&reg->mailbox_out[1]);
				mb[2] = RD_REG_WORD(&reg->mailbox_out[2]);
				mb[3] = RD_REG_WORD(&reg->mailbox_out[3]);
				qla2x00_async_event(vha, rsp, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(vha, rsp);
				break;
			default:
				DEBUG2(printk("scsi(%ld): "
					" Unrecognized interrupt type (%d).\n",
					vha->host_no, stat & 0xff));
				break;
			}
		}
		WRT_REG_DWORD(&reg->host_int, 0);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (!ha->flags.msi_enabled)
		qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);

#ifdef QL_DEBUG_LEVEL_17
	if (!irq && ha->flags.eeh_busy)
		qla_printk(KERN_WARNING, ha,
		    "isr: status %x, cmd_flags %lx, mbox_int %x, stat %x\n",
		    status, ha->mbx_cmd_flags, ha->flags.mbox_int, stat);
#endif

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}
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
	uint32_t stat;
	uint16_t mb[4];

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;

	reg = &ha->iobase->isp82;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	do {
		if (RD_REG_DWORD(&reg->host_int)) {
			stat = RD_REG_DWORD(&reg->host_status);

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
				mb[1] = RD_REG_WORD(&reg->mailbox_out[1]);
				mb[2] = RD_REG_WORD(&reg->mailbox_out[2]);
				mb[3] = RD_REG_WORD(&reg->mailbox_out[3]);
				qla2x00_async_event(vha, rsp, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(vha, rsp);
				break;
			default:
				DEBUG2(printk("scsi(%ld): "
					" Unrecognized interrupt type (%d).\n",
					vha->host_no, stat & 0xff));
				break;
			}
		}
		WRT_REG_DWORD(&reg->host_int, 0);
	} while (0);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

#ifdef QL_DEBUG_LEVEL_17
	if (!irq && ha->flags.eeh_busy)
		qla_printk(KERN_WARNING, ha,
			"isr: status %x, cmd_flags %lx, mbox_int %x, stat %x\n",
			status, ha->mbx_cmd_flags, ha->flags.mbox_int, stat);
#endif

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
		(status & MBX_INTERRUPT) && ha->flags.mbox_int) {
			set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
			complete(&ha->mbx_intr_comp);
	}
	return IRQ_HANDLED;
}

irqreturn_t
qla82xx_msix_rsp_q(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer\n", __func__);
		return IRQ_NONE;
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp82;
	spin_lock_irq(&ha->hardware_lock);
	vha = pci_get_drvdata(ha->pdev);
	qla24xx_process_response_queue(vha, rsp);
	WRT_REG_DWORD(&reg->host_int, 0);
	spin_unlock_irq(&ha->hardware_lock);
	return IRQ_HANDLED;
}

void
qla82xx_poll(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;
	int status = 0;
	uint32_t stat;
	uint16_t mb[4];
	unsigned long flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer\n", __func__);
		return;
	}
	ha = rsp->hw;

	reg = &ha->iobase->isp82;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);

	if (RD_REG_DWORD(&reg->host_int)) {
		stat = RD_REG_DWORD(&reg->host_status);
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
			mb[1] = RD_REG_WORD(&reg->mailbox_out[1]);
			mb[2] = RD_REG_WORD(&reg->mailbox_out[2]);
			mb[3] = RD_REG_WORD(&reg->mailbox_out[3]);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case 0x13:
			qla24xx_process_response_queue(vha, rsp);
			break;
		default:
			DEBUG2(printk("scsi(%ld): Unrecognized interrupt type "
				"(%d).\n",
				vha->host_no, stat & 0xff));
			break;
		}
	}
	WRT_REG_DWORD(&reg->host_int, 0);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla82xx_enable_intrs(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	qla82xx_mbx_intr_enable(vha);
	spin_lock_irq(&ha->hardware_lock);
	qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);
	spin_unlock_irq(&ha->hardware_lock);
	ha->interrupts_on = 1;
}

void
qla82xx_disable_intrs(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	qla82xx_mbx_intr_disable(vha);
	spin_lock_irq(&ha->hardware_lock);
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

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	rval = drv_state & (QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
	return rval;
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
	qla_printk(KERN_INFO, ha,
		"%s(%ld):drv_state = 0x%x\n",
		__func__, vha->host_no, drv_state);
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
		qla_printk(KERN_ERR, ha,
			"%s: Error during CRB Initialization\n", __func__);
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

	qla_printk(KERN_INFO, ha,
		"Attempting to load firmware from flash\n");

	if (qla82xx_fw_load_from_flash(ha) == QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
		    "Firmware loaded successfully from flash\n");
		return QLA_SUCCESS;
	} else {
		qla_printk(KERN_ERR, ha,
		    "Firmware load from flash failed\n");
	}

try_blob_fw:
	qla_printk(KERN_INFO, ha,
	    "Attempting to load firmware from blob\n");

	/* Load firmware blob. */
	blob = ha->hablob = qla2x00_request_firmware(vha);
	if (!blob) {
		qla_printk(KERN_ERR, ha,
			"Firmware image not present.\n");
		goto fw_load_failed;
	}

	/* Validating firmware blob */
	if (qla82xx_validate_firmware_blob(vha,
		QLA82XX_FLASH_ROMIMAGE)) {
		/* Fallback to URI format */
		if (qla82xx_validate_firmware_blob(vha,
			QLA82XX_UNIFIED_ROMIMAGE)) {
			qla_printk(KERN_ERR, ha,
				"No valid firmware image found!!!");
			return QLA_FUNCTION_FAILED;
		}
	}

	if (qla82xx_fw_load_from_blob(ha) == QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
			"%s: Firmware loaded successfully "
			" from binary blob\n", __func__);
		return QLA_SUCCESS;
	} else {
		qla_printk(KERN_ERR, ha,
		    "Firmware load failed from binary blob\n");
		blob->fw = NULL;
		blob = NULL;
		goto fw_load_failed;
	}
	return QLA_SUCCESS;

fw_load_failed:
	return QLA_FUNCTION_FAILED;
}

int
qla82xx_start_firmware(scsi_qla_host_t *vha)
{
	int           pcie_cap;
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
		qla_printk(KERN_INFO, ha,
			"%s: Error trying to start fw!\n", __func__);
		return QLA_FUNCTION_FAILED;
	}

	/* Handshake with the card before we register the devices. */
	if (qla82xx_check_cmdpeg_state(ha) != QLA_SUCCESS) {
		qla_printk(KERN_INFO, ha,
			"%s: Error during card handshake!\n", __func__);
		return QLA_FUNCTION_FAILED;
	}

	/* Negotiated Link width */
	pcie_cap = pci_find_capability(ha->pdev, PCI_CAP_ID_EXP);
	pci_read_config_word(ha->pdev, pcie_cap + PCI_EXP_LNKSTA, &lnk);
	ha->link_width = (lnk >> 4) & 0x3f;

	/* Synchronize with Receive peg */
	return qla82xx_check_rcvpeg_state(ha);
}

static inline int
qla2xx_build_scsi_type_6_iocbs(srb_t *sp, struct cmd_type_6 *cmd_pkt,
	uint16_t tot_dsds)
{
	uint32_t *cur_dsd = NULL;
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct scsi_cmnd *cmd;
	struct	scatterlist *cur_seg;
	uint32_t *dsd_seg;
	void *next_dsd;
	uint8_t avail_dsds;
	uint8_t first_iocb = 1;
	uint32_t dsd_list_len;
	struct dsd_dma *dsd_ptr;
	struct ct6_dsd *ctx;

	cmd = sp->cmd;

	/* Update entry type to indicate Command Type 3 IOCB */
	*((uint32_t *)(&cmd_pkt->entry_type)) =
		__constant_cpu_to_le32(COMMAND_TYPE_6);

	/* No data transfer */
	if (!scsi_bufflen(cmd) || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		return 0;
	}

	vha = sp->fcport->vha;
	ha = vha->hw;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		cmd_pkt->control_flags =
		    __constant_cpu_to_le16(CF_WRITE_DATA);
		ha->qla_stats.output_bytes += scsi_bufflen(cmd);
	} else if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		cmd_pkt->control_flags =
		    __constant_cpu_to_le16(CF_READ_DATA);
		ha->qla_stats.input_bytes += scsi_bufflen(cmd);
	}

	cur_seg = scsi_sglist(cmd);
	ctx = sp->ctx;

	while (tot_dsds) {
		avail_dsds = (tot_dsds > QLA_DSDS_PER_IOCB) ?
		    QLA_DSDS_PER_IOCB : tot_dsds;
		tot_dsds -= avail_dsds;
		dsd_list_len = (avail_dsds + 1) * QLA_DSD_SIZE;

		dsd_ptr = list_first_entry(&ha->gbl_dsd_list,
		    struct dsd_dma, list);
		next_dsd = dsd_ptr->dsd_addr;
		list_del(&dsd_ptr->list);
		ha->gbl_dsd_avail--;
		list_add_tail(&dsd_ptr->list, &ctx->dsd_list);
		ctx->dsd_use_cnt++;
		ha->gbl_dsd_inuse++;

		if (first_iocb) {
			first_iocb = 0;
			dsd_seg = (uint32_t *)&cmd_pkt->fcp_data_dseg_address;
			*dsd_seg++ = cpu_to_le32(LSD(dsd_ptr->dsd_list_dma));
			*dsd_seg++ = cpu_to_le32(MSD(dsd_ptr->dsd_list_dma));
			*dsd_seg++ = cpu_to_le32(dsd_list_len);
		} else {
			*cur_dsd++ = cpu_to_le32(LSD(dsd_ptr->dsd_list_dma));
			*cur_dsd++ = cpu_to_le32(MSD(dsd_ptr->dsd_list_dma));
			*cur_dsd++ = cpu_to_le32(dsd_list_len);
		}
		cur_dsd = (uint32_t *)next_dsd;
		while (avail_dsds) {
			dma_addr_t	sle_dma;

			sle_dma = sg_dma_address(cur_seg);
			*cur_dsd++ = cpu_to_le32(LSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(MSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(sg_dma_len(cur_seg));
			cur_seg = sg_next(cur_seg);
			avail_dsds--;
		}
	}

	/* Null termination */
	*cur_dsd++ =  0;
	*cur_dsd++ = 0;
	*cur_dsd++ = 0;
	cmd_pkt->control_flags |= CF_DATA_SEG_DESCR_ENABLE;
	return 0;
}

/*
 * qla82xx_calc_dsd_lists() - Determine number of DSD list required
 * for Command Type 6.
 *
 * @dsds: number of data segment decriptors needed
 *
 * Returns the number of dsd list needed to store @dsds.
 */
inline uint16_t
qla82xx_calc_dsd_lists(uint16_t dsds)
{
	uint16_t dsd_lists = 0;

	dsd_lists = (dsds/QLA_DSDS_PER_IOCB);
	if (dsds % QLA_DSDS_PER_IOCB)
		dsd_lists++;
	return dsd_lists;
}

/*
 * qla82xx_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
int
qla82xx_start_scsi(srb_t *sp)
{
	int		ret, nseg;
	unsigned long   flags;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t        index;
	uint32_t	handle;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct device_reg_82xx __iomem *reg;
	uint32_t dbval;
	uint32_t *fcp_dl;
	uint8_t additional_cdb_len;
	struct ct6_dsd *ctx;
	struct scsi_qla_host *vha = sp->fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = NULL;
	struct rsp_que *rsp = NULL;
	char		tag[2];

	/* Setup device pointers. */
	ret = 0;
	reg = &ha->iobase->isp82;
	cmd = sp->cmd;
	req = vha->req;
	rsp = ha->rsp_q_map[0];

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	dbval = 0x04 | (ha->portnum << 5);

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, req,
			rsp, 0, 0, MK_SYNC_ALL) != QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for room in outstanding command list. */
	handle = req->current_outstanding_cmd;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		handle++;
		if (handle == MAX_OUTSTANDING_COMMANDS)
			handle = 1;
		if (!req->outstanding_cmds[handle])
			break;
	}
	if (index == MAX_OUTSTANDING_COMMANDS)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
	} else
		nseg = 0;

	tot_dsds = nseg;

	if (tot_dsds > ql2xshiftctondsd) {
		struct cmd_type_6 *cmd_pkt;
		uint16_t more_dsd_lists = 0;
		struct dsd_dma *dsd_ptr;
		uint16_t i;

		more_dsd_lists = qla82xx_calc_dsd_lists(tot_dsds);
		if ((more_dsd_lists + ha->gbl_dsd_inuse) >= NUM_DSD_CHAIN)
			goto queuing_error;

		if (more_dsd_lists <= ha->gbl_dsd_avail)
			goto sufficient_dsds;
		else
			more_dsd_lists -= ha->gbl_dsd_avail;

		for (i = 0; i < more_dsd_lists; i++) {
			dsd_ptr = kzalloc(sizeof(struct dsd_dma), GFP_ATOMIC);
			if (!dsd_ptr)
				goto queuing_error;

			dsd_ptr->dsd_addr = dma_pool_alloc(ha->dl_dma_pool,
				GFP_ATOMIC, &dsd_ptr->dsd_list_dma);
			if (!dsd_ptr->dsd_addr) {
				kfree(dsd_ptr);
				goto queuing_error;
			}
			list_add_tail(&dsd_ptr->list, &ha->gbl_dsd_list);
			ha->gbl_dsd_avail++;
		}

sufficient_dsds:
		req_cnt = 1;

		if (req->cnt < (req_cnt + 2)) {
			cnt = (uint16_t)RD_REG_DWORD_RELAXED(
				&reg->req_q_out[0]);
			if (req->ring_index < cnt)
				req->cnt = cnt - req->ring_index;
			else
				req->cnt = req->length -
					(req->ring_index - cnt);
		}

		if (req->cnt < (req_cnt + 2))
			goto queuing_error;

		ctx = sp->ctx = mempool_alloc(ha->ctx_mempool, GFP_ATOMIC);
		if (!sp->ctx) {
			DEBUG(printk(KERN_INFO
				"%s(%ld): failed to allocate"
				" ctx.\n", __func__, vha->host_no));
			goto queuing_error;
		}
		memset(ctx, 0, sizeof(struct ct6_dsd));
		ctx->fcp_cmnd = dma_pool_alloc(ha->fcp_cmnd_dma_pool,
			GFP_ATOMIC, &ctx->fcp_cmnd_dma);
		if (!ctx->fcp_cmnd) {
			DEBUG2_3(printk("%s(%ld): failed to allocate"
				" fcp_cmnd.\n", __func__, vha->host_no));
			goto queuing_error_fcp_cmnd;
		}

		/* Initialize the DSD list and dma handle */
		INIT_LIST_HEAD(&ctx->dsd_list);
		ctx->dsd_use_cnt = 0;

		if (cmd->cmd_len > 16) {
			additional_cdb_len = cmd->cmd_len - 16;
			if ((cmd->cmd_len % 4) != 0) {
				/* SCSI command bigger than 16 bytes must be
				 * multiple of 4
				 */
				goto queuing_error_fcp_cmnd;
			}
			ctx->fcp_cmnd_len = 12 + cmd->cmd_len + 4;
		} else {
			additional_cdb_len = 0;
			ctx->fcp_cmnd_len = 12 + 16 + 4;
		}

		cmd_pkt = (struct cmd_type_6 *)req->ring_ptr;
		cmd_pkt->handle = MAKE_HANDLE(req->id, handle);

		/* Zero out remaining portion of packet. */
		/*    tagged queuing modifier -- default is TSK_SIMPLE (0). */
		clr_ptr = (uint32_t *)cmd_pkt + 2;
		memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
		cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

		/* Set NPORT-ID and LUN number*/
		cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
		cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
		cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
		cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
		cmd_pkt->vp_index = sp->fcport->vp_idx;

		/* Build IOCB segments */
		if (qla2xx_build_scsi_type_6_iocbs(sp, cmd_pkt, tot_dsds))
			goto queuing_error_fcp_cmnd;

		int_to_scsilun(sp->cmd->device->lun, &cmd_pkt->lun);
		host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

		/*
		 * Update tagged queuing modifier -- default is TSK_SIMPLE (0).
		 */
		if (scsi_populate_tag_msg(cmd, tag)) {
			switch (tag[0]) {
			case HEAD_OF_QUEUE_TAG:
				ctx->fcp_cmnd->task_attribute =
				    TSK_HEAD_OF_QUEUE;
				break;
			case ORDERED_QUEUE_TAG:
				ctx->fcp_cmnd->task_attribute =
				    TSK_ORDERED;
				break;
			}
		}

		/* build FCP_CMND IU */
		memset(ctx->fcp_cmnd, 0, sizeof(struct fcp_cmnd));
		int_to_scsilun(sp->cmd->device->lun, &ctx->fcp_cmnd->lun);
		ctx->fcp_cmnd->additional_cdb_len = additional_cdb_len;

		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			ctx->fcp_cmnd->additional_cdb_len |= 1;
		else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
			ctx->fcp_cmnd->additional_cdb_len |= 2;

		memcpy(ctx->fcp_cmnd->cdb, cmd->cmnd, cmd->cmd_len);

		fcp_dl = (uint32_t *)(ctx->fcp_cmnd->cdb + 16 +
		    additional_cdb_len);
		*fcp_dl = htonl((uint32_t)scsi_bufflen(cmd));

		cmd_pkt->fcp_cmnd_dseg_len = cpu_to_le16(ctx->fcp_cmnd_len);
		cmd_pkt->fcp_cmnd_dseg_address[0] =
		    cpu_to_le32(LSD(ctx->fcp_cmnd_dma));
		cmd_pkt->fcp_cmnd_dseg_address[1] =
		    cpu_to_le32(MSD(ctx->fcp_cmnd_dma));

		sp->flags |= SRB_FCP_CMND_DMA_VALID;
		cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));
		/* Set total data segment count. */
		cmd_pkt->entry_count = (uint8_t)req_cnt;
		/* Specify response queue number where
		 * completion should happen
		 */
		cmd_pkt->entry_status = (uint8_t) rsp->id;
	} else {
		struct cmd_type_7 *cmd_pkt;
		req_cnt = qla24xx_calc_iocbs(tot_dsds);
		if (req->cnt < (req_cnt + 2)) {
			cnt = (uint16_t)RD_REG_DWORD_RELAXED(
			    &reg->req_q_out[0]);
			if (req->ring_index < cnt)
				req->cnt = cnt - req->ring_index;
			else
				req->cnt = req->length -
					(req->ring_index - cnt);
		}
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;

		cmd_pkt = (struct cmd_type_7 *)req->ring_ptr;
		cmd_pkt->handle = MAKE_HANDLE(req->id, handle);

		/* Zero out remaining portion of packet. */
		/* tagged queuing modifier -- default is TSK_SIMPLE (0).*/
		clr_ptr = (uint32_t *)cmd_pkt + 2;
		memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
		cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

		/* Set NPORT-ID and LUN number*/
		cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
		cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
		cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
		cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
		cmd_pkt->vp_index = sp->fcport->vp_idx;

		int_to_scsilun(sp->cmd->device->lun, &cmd_pkt->lun);
		host_to_fcp_swap((uint8_t *)&cmd_pkt->lun,
			sizeof(cmd_pkt->lun));

		/*
		 * Update tagged queuing modifier -- default is TSK_SIMPLE (0).
		 */
		if (scsi_populate_tag_msg(cmd, tag)) {
			switch (tag[0]) {
			case HEAD_OF_QUEUE_TAG:
				cmd_pkt->task = TSK_HEAD_OF_QUEUE;
				break;
			case ORDERED_QUEUE_TAG:
				cmd_pkt->task = TSK_ORDERED;
				break;
			}
		}

		/* Load SCSI command packet. */
		memcpy(cmd_pkt->fcp_cdb, cmd->cmnd, cmd->cmd_len);
		host_to_fcp_swap(cmd_pkt->fcp_cdb, sizeof(cmd_pkt->fcp_cdb));

		cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));

		/* Build IOCB segments */
		qla24xx_build_scsi_iocbs(sp, cmd_pkt, tot_dsds);

		/* Set total data segment count. */
		cmd_pkt->entry_count = (uint8_t)req_cnt;
		/* Specify response queue number where
		 * completion should happen.
		 */
		cmd_pkt->entry_status = (uint8_t) rsp->id;

	}
	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	sp->cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;
	wmb();

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->flags |= SRB_DMA_VALID;

	/* Set chip new ring index. */
	/* write, read and verify logic */
	dbval = dbval | (req->id << 8) | (req->ring_index << 16);
	if (ql2xdbwr)
		qla82xx_wr_32(ha, ha->nxdb_wr_ptr, dbval);
	else {
		WRT_REG_DWORD(
			(unsigned long __iomem *)ha->nxdb_wr_ptr,
			dbval);
		wmb();
		while (RD_REG_DWORD(ha->nxdb_rd_ptr) != dbval) {
			WRT_REG_DWORD(
				(unsigned long __iomem *)ha->nxdb_wr_ptr,
				dbval);
			wmb();
		}
	}

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return QLA_SUCCESS;

queuing_error_fcp_cmnd:
	dma_pool_free(ha->fcp_cmnd_dma_pool, ctx->fcp_cmnd, ctx->fcp_cmnd_dma);
queuing_error:
	if (tot_dsds)
		scsi_dma_unmap(cmd);

	if (sp->ctx) {
		mempool_free(sp->ctx, ha->ctx_mempool);
		sp->ctx = NULL;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_FUNCTION_FAILED;
}

static uint32_t *
qla82xx_read_flash_data(scsi_qla_host_t *vha, uint32_t *dwptr, uint32_t faddr,
	uint32_t length)
{
	uint32_t i;
	uint32_t val;
	struct qla_hw_data *ha = vha->hw;

	/* Dword reads to flash. */
	for (i = 0; i < length/4; i++, faddr += 4) {
		if (qla82xx_rom_fast_read(ha, faddr, &val)) {
			qla_printk(KERN_WARNING, ha,
			    "Do ROM fast read failed\n");
			goto done_read;
		}
		dwptr[i] = __constant_cpu_to_le32(val);
	}
done_read:
	return dwptr;
}

static int
qla82xx_unprotect_flash(struct qla_hw_data *ha)
{
	int ret;
	uint32_t val;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_WARNING, ha, "ROM Lock failed\n");
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
		qla_printk(KERN_WARNING, ha, "Write disable failed\n");

done_unprotect:
	qla82xx_rom_unlock(ha);
	return ret;
}

static int
qla82xx_protect_flash(struct qla_hw_data *ha)
{
	int ret;
	uint32_t val;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_WARNING, ha, "ROM Lock failed\n");
		return ret;
	}

	ret = qla82xx_read_status_reg(ha, &val);
	if (ret < 0)
		goto done_protect;

	val |= (BLOCK_PROTECT_BITS << 2);
	/* LOCK all sectors */
	ret = qla82xx_write_status_reg(ha, val);
	if (ret < 0)
		qla_printk(KERN_WARNING, ha, "Write status register failed\n");

	if (qla82xx_write_disable_flash(ha) != 0)
		qla_printk(KERN_WARNING, ha, "Write disable failed\n");
done_protect:
	qla82xx_rom_unlock(ha);
	return ret;
}

static int
qla82xx_erase_sector(struct qla_hw_data *ha, int addr)
{
	int ret = 0;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_WARNING, ha, "ROM Lock failed\n");
		return ret;
	}

	qla82xx_flash_set_write_enable(ha);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, addr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_SE);

	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
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
uint8_t *
qla82xx_read_optrom_data(struct scsi_qla_host *vha, uint8_t *buf,
	uint32_t offset, uint32_t length)
{
	scsi_block_requests(vha->host);
	qla82xx_read_flash_data(vha, (uint32_t *)buf, offset, length);
	scsi_unblock_requests(vha->host);
	return buf;
}

static int
qla82xx_write_flash_data(struct scsi_qla_host *vha, uint32_t *dwptr,
	uint32_t faddr, uint32_t dwords)
{
	int ret;
	uint32_t liter;
	uint32_t sec_mask, rest_addr;
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
			qla_printk(KERN_DEBUG, ha,
				"Unable to allocate memory for optrom "
				"burst write (%x KB).\n",
				OPTROM_BURST_SIZE / 1024);
		}
	}

	rest_addr = ha->fdt_block_size - 1;
	sec_mask = ~rest_addr;

	ret = qla82xx_unprotect_flash(ha);
	if (ret) {
		qla_printk(KERN_WARNING, ha,
			"Unable to unprotect flash for update.\n");
		goto write_done;
	}

	for (liter = 0; liter < dwords; liter++, faddr += 4, dwptr++) {
		/* Are we at the beginning of a sector? */
		if ((faddr & rest_addr) == 0) {

			ret = qla82xx_erase_sector(ha, faddr);
			if (ret) {
				DEBUG9(qla_printk(KERN_ERR, ha,
				    "Unable to erase sector: "
				    "address=%x.\n", faddr));
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
				qla_printk(KERN_WARNING, ha,
				    "Unable to burst-write optrom segment "
				    "(%x/%x/%llx).\n", ret,
				    (ha->flash_data_off | faddr),
				    (unsigned long long)optrom_dma);
				qla_printk(KERN_WARNING, ha,
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
		    cpu_to_le32(*dwptr));
		if (ret) {
			DEBUG9(printk(KERN_DEBUG "%s(%ld) Unable to program"
			    "flash address=%x data=%x.\n", __func__,
			    ha->host_no, faddr, *dwptr));
			break;
		}
	}

	ret = qla82xx_protect_flash(ha);
	if (ret)
		qla_printk(KERN_WARNING, ha,
		    "Unable to protect flash after update.\n");
write_done:
	if (optrom)
		dma_free_coherent(&ha->pdev->dev,
		    OPTROM_BURST_SIZE, optrom, optrom_dma);
	return ret;
}

int
qla82xx_write_optrom_data(struct scsi_qla_host *vha, uint8_t *buf,
	uint32_t offset, uint32_t length)
{
	int rval;

	/* Suspend HBA. */
	scsi_block_requests(vha->host);
	rval = qla82xx_write_flash_data(vha, (uint32_t *)buf, offset,
		length >> 2);
	scsi_unblock_requests(vha->host);

	/* Convert return ISP82xx to generic */
	if (rval)
		rval = QLA_FUNCTION_FAILED;
	else
		rval = QLA_SUCCESS;
	return rval;
}

void
qla82xx_start_iocbs(srb_t *sp)
{
	struct qla_hw_data *ha = sp->fcport->vha->hw;
	struct req_que *req = ha->req_q_map[0];
	struct device_reg_82xx __iomem *reg;
	uint32_t dbval;

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	reg = &ha->iobase->isp82;
	dbval = 0x04 | (ha->portnum << 5);

	dbval = dbval | (req->id << 8) | (req->ring_index << 16);
	if (ql2xdbwr)
		qla82xx_wr_32(ha, ha->nxdb_wr_ptr, dbval);
	else {
		WRT_REG_DWORD((unsigned long __iomem *)ha->nxdb_wr_ptr, dbval);
		wmb();
		while (RD_REG_DWORD(ha->nxdb_rd_ptr) != dbval) {
			WRT_REG_DWORD((unsigned long  __iomem *)ha->nxdb_wr_ptr,
				dbval);
			wmb();
		}
	}
}

void qla82xx_rom_lock_recovery(struct qla_hw_data *ha)
{
	if (qla82xx_rom_lock(ha))
		/* Someone else is holding the lock. */
		qla_printk(KERN_INFO, ha, "Resetting rom_lock\n");

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
	int i, timeout;
	uint32_t old_count, count;
	struct qla_hw_data *ha = vha->hw;
	int need_reset = 0, peg_stuck = 1;

	need_reset = qla82xx_need_reset(ha);

	old_count = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);

	for (i = 0; i < 10; i++) {
		timeout = msleep_interruptible(200);
		if (timeout) {
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
				QLA82XX_DEV_FAILED);
			return QLA_FUNCTION_FAILED;
		}

		count = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);
		if (count != old_count)
			peg_stuck = 0;
	}

	if (need_reset) {
		/* We are trying to perform a recovery here. */
		if (peg_stuck)
			qla82xx_rom_lock_recovery(ha);
		goto dev_initialize;
	} else  {
		/* Start of day for this ha context. */
		if (peg_stuck) {
			/* Either we are the first or recovery in progress. */
			qla82xx_rom_lock_recovery(ha);
			goto dev_initialize;
		} else
			/* Firmware already running. */
			goto dev_ready;
	}

	return rval;

dev_initialize:
	/* set to DEV_INITIALIZING */
	qla_printk(KERN_INFO, ha, "HW State: INITIALIZING\n");
	qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_INITIALIZING);

	/* Driver that sets device state to initializating sets IDC version */
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_IDC_VERSION, QLA82XX_IDC_VERSION);

	qla82xx_idc_unlock(ha);
	rval = qla82xx_start_firmware(vha);
	qla82xx_idc_lock(ha);

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_INFO, ha, "HW State: FAILED\n");
		qla82xx_clear_drv_active(ha);
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_FAILED);
		return rval;
	}

dev_ready:
	qla_printk(KERN_INFO, ha, "HW State: READY\n");
	qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_READY);

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
		qla82xx_quiescent_state_cleanup(vha);
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
			qla_printk(KERN_INFO, ha,
			    "%s: QUIESCENT TIMEOUT\n", QLA2XXX_DRIVER_NAME);
			qla_printk(KERN_INFO, ha,
			    "DRV_ACTIVE:%d DRV_STATE:%d\n", drv_active,
			    drv_state);
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
						QLA82XX_DEV_READY);
			qla_printk(KERN_INFO, ha,
			    "HW State: DEV_READY\n");
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
	if (dev_state == QLA82XX_DEV_NEED_QUIESCENT) {
		qla_printk(KERN_INFO, ha, "HW State: DEV_QUIESCENT\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_QUIESCENT);
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

static void
qla82xx_dev_failed_handler(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	/* Disable the board */
	qla_printk(KERN_INFO, ha, "Disabling the board\n");

	qla82xx_idc_lock(ha);
	qla82xx_clear_drv_active(ha);
	qla82xx_idc_unlock(ha);

	/* Set DEV_FAILED flag to disable timer */
	vha->device_flags |= DFLG_DEV_FAILED;
	qla2x00_abort_all_cmds(vha, DID_NO_CONNECT << 16);
	qla2x00_mark_all_devices_lost(vha, 0);
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

	qla82xx_set_rst_ready(ha);

	/* wait for 10 seconds for reset ack from all functions */
	reset_timeout = jiffies + (ha->nx_reset_timeout * HZ);

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);

	while (drv_state != drv_active) {
		if (time_after_eq(jiffies, reset_timeout)) {
			qla_printk(KERN_INFO, ha,
				"%s: RESET TIMEOUT!\n", QLA2XXX_DRIVER_NAME);
			break;
		}
		qla82xx_idc_unlock(ha);
		msleep(1000);
		qla82xx_idc_lock(ha);
		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	}

	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	qla_printk(KERN_INFO, ha, "3:Device state is 0x%x = %s\n", dev_state,
		dev_state < MAX_STATES ? qdev_state[dev_state] : "Unknown");

	/* Force to DEV_COLD unless someone else is starting a reset */
	if (dev_state != QLA82XX_DEV_INITIALIZING) {
		qla_printk(KERN_INFO, ha, "HW State: COLD/RE-INIT\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_COLD);
	}
}

int
qla82xx_check_fw_alive(scsi_qla_host_t *vha)
{
	uint32_t fw_heartbeat_counter;
	int status = 0;

	fw_heartbeat_counter = qla82xx_rd_32(vha->hw,
		QLA82XX_PEG_ALIVE_COUNTER);
	/* all 0xff, assume AER/EEH in progress, ignore */
	if (fw_heartbeat_counter == 0xffffffff)
		return status;
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
	if (!vha->flags.init_done)
		qla82xx_set_drv_active(vha);

	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	old_dev_state = dev_state;
	qla_printk(KERN_INFO, ha, "1:Device state is 0x%x = %s\n", dev_state,
		dev_state < MAX_STATES ? qdev_state[dev_state] : "Unknown");

	/* wait for 30 seconds for device to go ready */
	dev_init_timeout = jiffies + (ha->nx_dev_init_timeout * HZ);

	while (1) {

		if (time_after_eq(jiffies, dev_init_timeout)) {
			DEBUG(qla_printk(KERN_INFO, ha,
				"%s: device init failed!\n",
				QLA2XXX_DRIVER_NAME));
			rval = QLA_FUNCTION_FAILED;
			break;
		}
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		if (old_dev_state != dev_state) {
			loopcount = 0;
			old_dev_state = dev_state;
		}
		if (loopcount < 5) {
			qla_printk(KERN_INFO, ha,
			    "2:Device state is 0x%x = %s\n", dev_state,
			    dev_state < MAX_STATES ?
			    qdev_state[dev_state] : "Unknown");
		}

		switch (dev_state) {
		case QLA82XX_DEV_READY:
			goto exit;
		case QLA82XX_DEV_COLD:
			rval = qla82xx_device_bootstrap(vha);
			goto exit;
		case QLA82XX_DEV_INITIALIZING:
			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);
			break;
		case QLA82XX_DEV_NEED_RESET:
		    if (!ql2xdontresethba)
			qla82xx_need_reset_handler(vha);
			dev_init_timeout = jiffies +
				(ha->nx_dev_init_timeout * HZ);
			break;
		case QLA82XX_DEV_NEED_QUIESCENT:
			qla82xx_need_qsnt_handler(vha);
			/* Reset timeout value after quiescence handler */
			dev_init_timeout = jiffies + (ha->nx_dev_init_timeout\
							 * HZ);
			break;
		case QLA82XX_DEV_QUIESCENT:
			/* Owner will exit and other will wait for the state
			 * to get changed
			 */
			if (ha->flags.quiesce_owner)
				goto exit;

			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);

			/* Reset timeout value after quiescence handler */
			dev_init_timeout = jiffies + (ha->nx_dev_init_timeout\
							 * HZ);
			break;
		case QLA82XX_DEV_FAILED:
			qla82xx_dev_failed_handler(vha);
			rval = QLA_FUNCTION_FAILED;
			goto exit;
		default:
			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);
		}
		loopcount++;
	}
exit:
	qla82xx_idc_unlock(ha);
	return rval;
}

void qla82xx_watchdog(scsi_qla_host_t *vha)
{
	uint32_t dev_state, halt_status;
	struct qla_hw_data *ha = vha->hw;

	/* don't poll if reset is going on */
	if (!ha->flags.isp82xx_reset_hdlr_active) {
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		if (dev_state == QLA82XX_DEV_NEED_RESET &&
		    !test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags)) {
			qla_printk(KERN_WARNING, ha,
			    "scsi(%ld) %s: Adapter reset needed!\n",
				vha->host_no, __func__);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		} else if (dev_state == QLA82XX_DEV_NEED_QUIESCENT &&
			!test_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags)) {
			DEBUG(qla_printk(KERN_INFO, ha,
				"scsi(%ld) %s - detected quiescence needed\n",
				vha->host_no, __func__));
			set_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		} else {
			if (qla82xx_check_fw_alive(vha)) {
				halt_status = qla82xx_rd_32(ha,
				    QLA82XX_PEG_HALT_STATUS1);
				qla_printk(KERN_INFO, ha,
				    "scsi(%ld): %s, Dumping hw/fw registers:\n "
				    " PEG_HALT_STATUS1: 0x%x, PEG_HALT_STATUS2: 0x%x,\n "
				    " PEG_NET_0_PC: 0x%x, PEG_NET_1_PC: 0x%x,\n "
				    " PEG_NET_2_PC: 0x%x, PEG_NET_3_PC: 0x%x,\n "
				    " PEG_NET_4_PC: 0x%x\n",
				    vha->host_no, __func__, halt_status,
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
				if (halt_status & HALT_STATUS_UNRECOVERABLE) {
					set_bit(ISP_UNRECOVERABLE,
					    &vha->dpc_flags);
				} else {
					qla_printk(KERN_INFO, ha,
					    "scsi(%ld): %s - detect abort needed\n",
					    vha->host_no, __func__);
					set_bit(ISP_ABORT_NEEDED,
					    &vha->dpc_flags);
				}
				qla2xxx_wake_dpc(vha);
				ha->flags.isp82xx_fw_hung = 1;
				if (ha->flags.mbox_busy) {
					ha->flags.mbox_int = 1;
					DEBUG2(qla_printk(KERN_ERR, ha,
					    "scsi(%ld) Due to fw hung, doing "
					    "premature completion of mbx "
					    "command\n", vha->host_no));
					if (test_bit(MBX_INTR_WAIT,
					    &ha->mbx_cmd_flags))
						complete(&ha->mbx_intr_comp);
				}
			}
		}
	}
}

int qla82xx_load_risc(scsi_qla_host_t *vha, uint32_t *srisc_addr)
{
	int rval;
	rval = qla82xx_device_state_handler(vha);
	return rval;
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
	int rval;
	struct qla_hw_data *ha = vha->hw;
	uint32_t dev_state;

	if (vha->device_flags & DFLG_DEV_FAILED) {
		qla_printk(KERN_WARNING, ha,
			"%s(%ld): Device in failed state, "
			"Exiting.\n", __func__, vha->host_no);
		return QLA_SUCCESS;
	}
	ha->flags.isp82xx_reset_hdlr_active = 1;

	qla82xx_idc_lock(ha);
	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	if (dev_state == QLA82XX_DEV_READY) {
		qla_printk(KERN_INFO, ha, "HW State: NEED RESET\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			QLA82XX_DEV_NEED_RESET);
	} else
		qla_printk(KERN_INFO, ha, "HW State: %s\n",
			dev_state < MAX_STATES ?
			qdev_state[dev_state] : "Unknown");
	qla82xx_idc_unlock(ha);

	rval = qla82xx_device_state_handler(vha);

	qla82xx_idc_lock(ha);
	qla82xx_clear_rst_ready(ha);
	qla82xx_idc_unlock(ha);

	if (rval == QLA_SUCCESS) {
		ha->flags.isp82xx_fw_hung = 0;
		ha->flags.isp82xx_reset_hdlr_active = 0;
		qla82xx_restart_isp(vha);
	}

	if (rval) {
		vha->flags.online = 1;
		if (test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
			if (ha->isp_abort_cnt == 0) {
				qla_printk(KERN_WARNING, ha,
				    "ISP error recovery failed - "
				    "board disabled\n");
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
				DEBUG(qla_printk(KERN_INFO, ha,
				    "qla%ld: ISP abort - retry remaining %d\n",
				    vha->host_no, ha->isp_abort_cnt));
				rval = QLA_FUNCTION_FAILED;
			}
		} else {
			ha->isp_abort_cnt = MAX_RETRIES_OF_ISP_ABORT;
			DEBUG(qla_printk(KERN_INFO, ha,
			    "(%ld): ISP error recovery - retrying (%d) "
			    "more times\n", vha->host_no, ha->isp_abort_cnt));
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
	DEBUG2(printk(KERN_INFO
	    "%s status=%d\n", __func__, status));

	return status;
}

void
qla82xx_chip_reset_cleanup(scsi_qla_host_t *vha)
{
	int i;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;

	/* Check if 82XX firmware is alive or not
	 * We may have arrived here from NEED_RESET
	 * detection only
	 */
	if (!ha->flags.isp82xx_fw_hung) {
		for (i = 0; i < 2; i++) {
			msleep(1000);
			if (qla82xx_check_fw_alive(vha)) {
				ha->flags.isp82xx_fw_hung = 1;
				if (ha->flags.mbox_busy) {
					ha->flags.mbox_int = 1;
					complete(&ha->mbx_intr_comp);
				}
				break;
			}
		}
	}

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
			for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
				sp = req->outstanding_cmds[cnt];
				if (sp) {
					if (!sp->ctx ||
					    (sp->flags & SRB_FCP_CMND_DMA_VALID)) {
						spin_unlock_irqrestore(
						    &ha->hardware_lock, flags);
						if (ha->isp_ops->abort_command(sp)) {
							qla_printk(KERN_INFO, ha,
							    "scsi(%ld): mbx abort command failed in %s\n",
							    vha->host_no, __func__);
						} else {
							qla_printk(KERN_INFO, ha,
							    "scsi(%ld): mbx abort command success in %s\n",
							    vha->host_no, __func__);
						}
						spin_lock_irqsave(&ha->hardware_lock, flags);
					}
				}
			}
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* Wait for pending cmds (physical and virtual) to complete */
		if (!qla2x00_eh_wait_for_pending_commands(vha, 0, 0,
		    WAIT_HOST) == QLA_SUCCESS) {
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "Done wait for pending commands\n"));
		}
	}
}
