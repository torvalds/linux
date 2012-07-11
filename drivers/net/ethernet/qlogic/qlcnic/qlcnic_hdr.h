/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2010 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#ifndef __QLCNIC_HDR_H_
#define __QLCNIC_HDR_H_

#include <linux/kernel.h>
#include <linux/types.h>

/*
 * The basic unit of access when reading/writing control registers.
 */

enum {
	QLCNIC_HW_H0_CH_HUB_ADR = 0x05,
	QLCNIC_HW_H1_CH_HUB_ADR = 0x0E,
	QLCNIC_HW_H2_CH_HUB_ADR = 0x03,
	QLCNIC_HW_H3_CH_HUB_ADR = 0x01,
	QLCNIC_HW_H4_CH_HUB_ADR = 0x06,
	QLCNIC_HW_H5_CH_HUB_ADR = 0x07,
	QLCNIC_HW_H6_CH_HUB_ADR = 0x08
};

/*  Hub 0 */
enum {
	QLCNIC_HW_MN_CRB_AGT_ADR = 0x15,
	QLCNIC_HW_MS_CRB_AGT_ADR = 0x25
};

/*  Hub 1 */
enum {
	QLCNIC_HW_PS_CRB_AGT_ADR = 0x73,
	QLCNIC_HW_SS_CRB_AGT_ADR = 0x20,
	QLCNIC_HW_RPMX3_CRB_AGT_ADR = 0x0b,
	QLCNIC_HW_QMS_CRB_AGT_ADR = 0x00,
	QLCNIC_HW_SQGS0_CRB_AGT_ADR = 0x01,
	QLCNIC_HW_SQGS1_CRB_AGT_ADR = 0x02,
	QLCNIC_HW_SQGS2_CRB_AGT_ADR = 0x03,
	QLCNIC_HW_SQGS3_CRB_AGT_ADR = 0x04,
	QLCNIC_HW_C2C0_CRB_AGT_ADR = 0x58,
	QLCNIC_HW_C2C1_CRB_AGT_ADR = 0x59,
	QLCNIC_HW_C2C2_CRB_AGT_ADR = 0x5a,
	QLCNIC_HW_RPMX2_CRB_AGT_ADR = 0x0a,
	QLCNIC_HW_RPMX4_CRB_AGT_ADR = 0x0c,
	QLCNIC_HW_RPMX7_CRB_AGT_ADR = 0x0f,
	QLCNIC_HW_RPMX9_CRB_AGT_ADR = 0x12,
	QLCNIC_HW_SMB_CRB_AGT_ADR = 0x18
};

/*  Hub 2 */
enum {
	QLCNIC_HW_NIU_CRB_AGT_ADR = 0x31,
	QLCNIC_HW_I2C0_CRB_AGT_ADR = 0x19,
	QLCNIC_HW_I2C1_CRB_AGT_ADR = 0x29,

	QLCNIC_HW_SN_CRB_AGT_ADR = 0x10,
	QLCNIC_HW_I2Q_CRB_AGT_ADR = 0x20,
	QLCNIC_HW_LPC_CRB_AGT_ADR = 0x22,
	QLCNIC_HW_ROMUSB_CRB_AGT_ADR = 0x21,
	QLCNIC_HW_QM_CRB_AGT_ADR = 0x66,
	QLCNIC_HW_SQG0_CRB_AGT_ADR = 0x60,
	QLCNIC_HW_SQG1_CRB_AGT_ADR = 0x61,
	QLCNIC_HW_SQG2_CRB_AGT_ADR = 0x62,
	QLCNIC_HW_SQG3_CRB_AGT_ADR = 0x63,
	QLCNIC_HW_RPMX1_CRB_AGT_ADR = 0x09,
	QLCNIC_HW_RPMX5_CRB_AGT_ADR = 0x0d,
	QLCNIC_HW_RPMX6_CRB_AGT_ADR = 0x0e,
	QLCNIC_HW_RPMX8_CRB_AGT_ADR = 0x11
};

/*  Hub 3 */
enum {
	QLCNIC_HW_PH_CRB_AGT_ADR = 0x1A,
	QLCNIC_HW_SRE_CRB_AGT_ADR = 0x50,
	QLCNIC_HW_EG_CRB_AGT_ADR = 0x51,
	QLCNIC_HW_RPMX0_CRB_AGT_ADR = 0x08
};

/*  Hub 4 */
enum {
	QLCNIC_HW_PEGN0_CRB_AGT_ADR = 0x40,
	QLCNIC_HW_PEGN1_CRB_AGT_ADR,
	QLCNIC_HW_PEGN2_CRB_AGT_ADR,
	QLCNIC_HW_PEGN3_CRB_AGT_ADR,
	QLCNIC_HW_PEGNI_CRB_AGT_ADR,
	QLCNIC_HW_PEGND_CRB_AGT_ADR,
	QLCNIC_HW_PEGNC_CRB_AGT_ADR,
	QLCNIC_HW_PEGR0_CRB_AGT_ADR,
	QLCNIC_HW_PEGR1_CRB_AGT_ADR,
	QLCNIC_HW_PEGR2_CRB_AGT_ADR,
	QLCNIC_HW_PEGR3_CRB_AGT_ADR,
	QLCNIC_HW_PEGN4_CRB_AGT_ADR
};

/*  Hub 5 */
enum {
	QLCNIC_HW_PEGS0_CRB_AGT_ADR = 0x40,
	QLCNIC_HW_PEGS1_CRB_AGT_ADR,
	QLCNIC_HW_PEGS2_CRB_AGT_ADR,
	QLCNIC_HW_PEGS3_CRB_AGT_ADR,
	QLCNIC_HW_PEGSI_CRB_AGT_ADR,
	QLCNIC_HW_PEGSD_CRB_AGT_ADR,
	QLCNIC_HW_PEGSC_CRB_AGT_ADR
};

/*  Hub 6 */
enum {
	QLCNIC_HW_CAS0_CRB_AGT_ADR = 0x46,
	QLCNIC_HW_CAS1_CRB_AGT_ADR = 0x47,
	QLCNIC_HW_CAS2_CRB_AGT_ADR = 0x48,
	QLCNIC_HW_CAS3_CRB_AGT_ADR = 0x49,
	QLCNIC_HW_NCM_CRB_AGT_ADR = 0x16,
	QLCNIC_HW_TMR_CRB_AGT_ADR = 0x17,
	QLCNIC_HW_XDMA_CRB_AGT_ADR = 0x05,
	QLCNIC_HW_OCM0_CRB_AGT_ADR = 0x06,
	QLCNIC_HW_OCM1_CRB_AGT_ADR = 0x07
};

/*  Floaters - non existent modules */
#define QLCNIC_HW_EFC_RPMX0_CRB_AGT_ADR	0x67

/*  This field defines PCI/X adr [25:20] of agents on the CRB */
enum {
	QLCNIC_HW_PX_MAP_CRB_PH = 0,
	QLCNIC_HW_PX_MAP_CRB_PS,
	QLCNIC_HW_PX_MAP_CRB_MN,
	QLCNIC_HW_PX_MAP_CRB_MS,
	QLCNIC_HW_PX_MAP_CRB_PGR1,
	QLCNIC_HW_PX_MAP_CRB_SRE,
	QLCNIC_HW_PX_MAP_CRB_NIU,
	QLCNIC_HW_PX_MAP_CRB_QMN,
	QLCNIC_HW_PX_MAP_CRB_SQN0,
	QLCNIC_HW_PX_MAP_CRB_SQN1,
	QLCNIC_HW_PX_MAP_CRB_SQN2,
	QLCNIC_HW_PX_MAP_CRB_SQN3,
	QLCNIC_HW_PX_MAP_CRB_QMS,
	QLCNIC_HW_PX_MAP_CRB_SQS0,
	QLCNIC_HW_PX_MAP_CRB_SQS1,
	QLCNIC_HW_PX_MAP_CRB_SQS2,
	QLCNIC_HW_PX_MAP_CRB_SQS3,
	QLCNIC_HW_PX_MAP_CRB_PGN0,
	QLCNIC_HW_PX_MAP_CRB_PGN1,
	QLCNIC_HW_PX_MAP_CRB_PGN2,
	QLCNIC_HW_PX_MAP_CRB_PGN3,
	QLCNIC_HW_PX_MAP_CRB_PGND,
	QLCNIC_HW_PX_MAP_CRB_PGNI,
	QLCNIC_HW_PX_MAP_CRB_PGS0,
	QLCNIC_HW_PX_MAP_CRB_PGS1,
	QLCNIC_HW_PX_MAP_CRB_PGS2,
	QLCNIC_HW_PX_MAP_CRB_PGS3,
	QLCNIC_HW_PX_MAP_CRB_PGSD,
	QLCNIC_HW_PX_MAP_CRB_PGSI,
	QLCNIC_HW_PX_MAP_CRB_SN,
	QLCNIC_HW_PX_MAP_CRB_PGR2,
	QLCNIC_HW_PX_MAP_CRB_EG,
	QLCNIC_HW_PX_MAP_CRB_PH2,
	QLCNIC_HW_PX_MAP_CRB_PS2,
	QLCNIC_HW_PX_MAP_CRB_CAM,
	QLCNIC_HW_PX_MAP_CRB_CAS0,
	QLCNIC_HW_PX_MAP_CRB_CAS1,
	QLCNIC_HW_PX_MAP_CRB_CAS2,
	QLCNIC_HW_PX_MAP_CRB_C2C0,
	QLCNIC_HW_PX_MAP_CRB_C2C1,
	QLCNIC_HW_PX_MAP_CRB_TIMR,
	QLCNIC_HW_PX_MAP_CRB_PGR3,
	QLCNIC_HW_PX_MAP_CRB_RPMX1,
	QLCNIC_HW_PX_MAP_CRB_RPMX2,
	QLCNIC_HW_PX_MAP_CRB_RPMX3,
	QLCNIC_HW_PX_MAP_CRB_RPMX4,
	QLCNIC_HW_PX_MAP_CRB_RPMX5,
	QLCNIC_HW_PX_MAP_CRB_RPMX6,
	QLCNIC_HW_PX_MAP_CRB_RPMX7,
	QLCNIC_HW_PX_MAP_CRB_XDMA,
	QLCNIC_HW_PX_MAP_CRB_I2Q,
	QLCNIC_HW_PX_MAP_CRB_ROMUSB,
	QLCNIC_HW_PX_MAP_CRB_CAS3,
	QLCNIC_HW_PX_MAP_CRB_RPMX0,
	QLCNIC_HW_PX_MAP_CRB_RPMX8,
	QLCNIC_HW_PX_MAP_CRB_RPMX9,
	QLCNIC_HW_PX_MAP_CRB_OCM0,
	QLCNIC_HW_PX_MAP_CRB_OCM1,
	QLCNIC_HW_PX_MAP_CRB_SMB,
	QLCNIC_HW_PX_MAP_CRB_I2C0,
	QLCNIC_HW_PX_MAP_CRB_I2C1,
	QLCNIC_HW_PX_MAP_CRB_LPC,
	QLCNIC_HW_PX_MAP_CRB_PGNC,
	QLCNIC_HW_PX_MAP_CRB_PGR0
};

#define	BIT_0	0x1
#define	BIT_1	0x2
#define	BIT_2	0x4
#define	BIT_3	0x8
#define	BIT_4	0x10
#define	BIT_5	0x20
#define	BIT_6	0x40
#define	BIT_7	0x80
#define	BIT_8	0x100
#define	BIT_9	0x200
#define	BIT_10	0x400
#define	BIT_11	0x800
#define	BIT_12	0x1000
#define	BIT_13	0x2000
#define	BIT_14	0x4000
#define	BIT_15	0x8000
#define	BIT_16	0x10000
#define	BIT_17	0x20000
#define	BIT_18	0x40000
#define	BIT_19	0x80000
#define	BIT_20	0x100000
#define	BIT_21	0x200000
#define	BIT_22	0x400000
#define	BIT_23	0x800000
#define	BIT_24	0x1000000
#define	BIT_25	0x2000000
#define	BIT_26	0x4000000
#define	BIT_27	0x8000000
#define	BIT_28	0x10000000
#define	BIT_29	0x20000000
#define	BIT_30	0x40000000
#define	BIT_31	0x80000000

/*  This field defines CRB adr [31:20] of the agents */

#define QLCNIC_HW_CRB_HUB_AGT_ADR_MN	\
	((QLCNIC_HW_H0_CH_HUB_ADR << 7) | QLCNIC_HW_MN_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PH	\
	((QLCNIC_HW_H0_CH_HUB_ADR << 7) | QLCNIC_HW_PH_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_MS	\
	((QLCNIC_HW_H0_CH_HUB_ADR << 7) | QLCNIC_HW_MS_CRB_AGT_ADR)

#define QLCNIC_HW_CRB_HUB_AGT_ADR_PS	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_PS_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SS	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_SS_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX3	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX3_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_QMS	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_QMS_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQS0	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_SQGS0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQS1	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_SQGS1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQS2	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_SQGS2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQS3	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_SQGS3_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_C2C0	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_C2C0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_C2C1	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_C2C1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX2	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX4	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX4_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX7	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX7_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX9	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX9_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SMB	\
	((QLCNIC_HW_H1_CH_HUB_ADR << 7) | QLCNIC_HW_SMB_CRB_AGT_ADR)

#define QLCNIC_HW_CRB_HUB_AGT_ADR_NIU	\
	((QLCNIC_HW_H2_CH_HUB_ADR << 7) | QLCNIC_HW_NIU_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_I2C0	\
	((QLCNIC_HW_H2_CH_HUB_ADR << 7) | QLCNIC_HW_I2C0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_I2C1	\
	((QLCNIC_HW_H2_CH_HUB_ADR << 7) | QLCNIC_HW_I2C1_CRB_AGT_ADR)

#define QLCNIC_HW_CRB_HUB_AGT_ADR_SRE	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_SRE_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_EG	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_EG_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX0	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_QMN	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_QM_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQN0	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_SQG0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQN1	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_SQG1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQN2	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_SQG2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SQN3	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_SQG3_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX1	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX5	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX5_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX6	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX6_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX8	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_RPMX8_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_CAS0	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_CAS0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_CAS1	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_CAS1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_CAS2	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_CAS2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_CAS3	\
	((QLCNIC_HW_H3_CH_HUB_ADR << 7) | QLCNIC_HW_CAS3_CRB_AGT_ADR)

#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGNI	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGNI_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGND	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGND_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGN0	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGN0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGN1	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGN1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGN2	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGN2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGN3	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGN3_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGN4	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGN4_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGNC	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGNC_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGR0	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGR0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGR1	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGR1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGR2	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGR2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGR3	\
	((QLCNIC_HW_H4_CH_HUB_ADR << 7) | QLCNIC_HW_PEGR3_CRB_AGT_ADR)

#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGSI	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGSI_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGSD	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGSD_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGS0	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGS0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGS1	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGS1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGS2	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGS2_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGS3	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGS3_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_PGSC	\
	((QLCNIC_HW_H5_CH_HUB_ADR << 7) | QLCNIC_HW_PEGSC_CRB_AGT_ADR)

#define QLCNIC_HW_CRB_HUB_AGT_ADR_CAM	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_NCM_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_TIMR	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_TMR_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_XDMA	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_XDMA_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_SN	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_SN_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_I2Q	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_I2Q_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_ROMUSB	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_ROMUSB_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_OCM0	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_OCM0_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_OCM1	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_OCM1_CRB_AGT_ADR)
#define QLCNIC_HW_CRB_HUB_AGT_ADR_LPC	\
	((QLCNIC_HW_H6_CH_HUB_ADR << 7) | QLCNIC_HW_LPC_CRB_AGT_ADR)

#define QLCNIC_SRE_MISC		(QLCNIC_CRB_SRE + 0x0002c)

#define QLCNIC_I2Q_CLR_PCI_HI	(QLCNIC_CRB_I2Q + 0x00034)

#define ROMUSB_GLB		(QLCNIC_CRB_ROMUSB + 0x00000)
#define ROMUSB_ROM		(QLCNIC_CRB_ROMUSB + 0x10000)

#define QLCNIC_ROMUSB_GLB_STATUS	(ROMUSB_GLB + 0x0004)
#define QLCNIC_ROMUSB_GLB_SW_RESET	(ROMUSB_GLB + 0x0008)
#define QLCNIC_ROMUSB_GLB_PAD_GPIO_I	(ROMUSB_GLB + 0x000c)
#define QLCNIC_ROMUSB_GLB_CAS_RST	(ROMUSB_GLB + 0x0038)
#define QLCNIC_ROMUSB_GLB_TEST_MUX_SEL	(ROMUSB_GLB + 0x0044)
#define QLCNIC_ROMUSB_GLB_PEGTUNE_DONE	(ROMUSB_GLB + 0x005c)
#define QLCNIC_ROMUSB_GLB_CHIP_CLK_CTRL	(ROMUSB_GLB + 0x00A8)

#define QLCNIC_ROMUSB_GPIO(n)		(ROMUSB_GLB + 0x60 + (4 * (n)))

#define QLCNIC_ROMUSB_ROM_INSTR_OPCODE	(ROMUSB_ROM + 0x0004)
#define QLCNIC_ROMUSB_ROM_ADDRESS	(ROMUSB_ROM + 0x0008)
#define QLCNIC_ROMUSB_ROM_WDATA		(ROMUSB_ROM + 0x000c)
#define QLCNIC_ROMUSB_ROM_ABYTE_CNT	(ROMUSB_ROM + 0x0010)
#define QLCNIC_ROMUSB_ROM_DUMMY_BYTE_CNT (ROMUSB_ROM + 0x0014)
#define QLCNIC_ROMUSB_ROM_RDATA		(ROMUSB_ROM + 0x0018)

/* Lock IDs for ROM lock */
#define ROM_LOCK_DRIVER	0x0d417340

/******************************************************************************
*
*    Definitions specific to M25P flash
*
*******************************************************************************
*/

/* all are 1MB windows */

#define QLCNIC_PCI_CRB_WINDOWSIZE	0x00100000
#define QLCNIC_PCI_CRB_WINDOW(A)	\
	(QLCNIC_PCI_CRBSPACE + (A)*QLCNIC_PCI_CRB_WINDOWSIZE)

#define QLCNIC_CRB_NIU		QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_NIU)
#define QLCNIC_CRB_SRE		QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_SRE)
#define QLCNIC_CRB_ROMUSB	\
	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_ROMUSB)
#define QLCNIC_CRB_EPG		QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_EG)
#define QLCNIC_CRB_I2Q		QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_I2Q)
#define QLCNIC_CRB_TIMER	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_TIMR)
#define QLCNIC_CRB_I2C0 	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_I2C0)
#define QLCNIC_CRB_SMB		QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_SMB)
#define QLCNIC_CRB_MAX		QLCNIC_PCI_CRB_WINDOW(64)

#define QLCNIC_CRB_PCIX_HOST	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PH)
#define QLCNIC_CRB_PCIX_HOST2	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PH2)
#define QLCNIC_CRB_PEG_NET_0	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PGN0)
#define QLCNIC_CRB_PEG_NET_1	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PGN1)
#define QLCNIC_CRB_PEG_NET_2	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PGN2)
#define QLCNIC_CRB_PEG_NET_3	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PGN3)
#define QLCNIC_CRB_PEG_NET_4	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_SQS2)
#define QLCNIC_CRB_PEG_NET_D	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PGND)
#define QLCNIC_CRB_PEG_NET_I	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PGNI)
#define QLCNIC_CRB_DDR_NET	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_MN)
#define QLCNIC_CRB_QDR_NET	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_SN)

#define QLCNIC_CRB_PCIX_MD	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_PS)
#define QLCNIC_CRB_PCIE 	QLCNIC_CRB_PCIX_MD

#define ISR_INT_VECTOR		(QLCNIC_PCIX_PS_REG(PCIX_INT_VECTOR))
#define ISR_INT_MASK		(QLCNIC_PCIX_PS_REG(PCIX_INT_MASK))
#define ISR_INT_MASK_SLOW	(QLCNIC_PCIX_PS_REG(PCIX_INT_MASK))
#define ISR_INT_TARGET_STATUS	(QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS))
#define ISR_INT_TARGET_MASK	(QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK))
#define ISR_INT_TARGET_STATUS_F1   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F1))
#define ISR_INT_TARGET_MASK_F1     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F1))
#define ISR_INT_TARGET_STATUS_F2   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F2))
#define ISR_INT_TARGET_MASK_F2     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F2))
#define ISR_INT_TARGET_STATUS_F3   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F3))
#define ISR_INT_TARGET_MASK_F3     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F3))
#define ISR_INT_TARGET_STATUS_F4   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F4))
#define ISR_INT_TARGET_MASK_F4     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F4))
#define ISR_INT_TARGET_STATUS_F5   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F5))
#define ISR_INT_TARGET_MASK_F5     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F5))
#define ISR_INT_TARGET_STATUS_F6   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F6))
#define ISR_INT_TARGET_MASK_F6     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F6))
#define ISR_INT_TARGET_STATUS_F7   (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F7))
#define ISR_INT_TARGET_MASK_F7     (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F7))

#define QLCNIC_PCI_MN_2M	(0)
#define QLCNIC_PCI_MS_2M	(0x80000)
#define QLCNIC_PCI_OCM0_2M	(0x000c0000UL)
#define QLCNIC_PCI_CRBSPACE	(0x06000000UL)
#define QLCNIC_PCI_CAMQM	(0x04800000UL)
#define QLCNIC_PCI_CAMQM_END	(0x04800800UL)
#define QLCNIC_PCI_2MB_SIZE	(0x00200000UL)
#define QLCNIC_PCI_CAMQM_2M_BASE	(0x000ff800UL)

#define QLCNIC_CRB_CAM	QLCNIC_PCI_CRB_WINDOW(QLCNIC_HW_PX_MAP_CRB_CAM)

#define QLCNIC_ADDR_DDR_NET	(0x0000000000000000ULL)
#define QLCNIC_ADDR_DDR_NET_MAX (0x000000000fffffffULL)
#define QLCNIC_ADDR_OCM0	(0x0000000200000000ULL)
#define QLCNIC_ADDR_OCM0_MAX	(0x00000002000fffffULL)
#define QLCNIC_ADDR_OCM1	(0x0000000200400000ULL)
#define QLCNIC_ADDR_OCM1_MAX	(0x00000002004fffffULL)
#define QLCNIC_ADDR_QDR_NET	(0x0000000300000000ULL)
#define QLCNIC_ADDR_QDR_NET_MAX (0x0000000307ffffffULL)

/*
 *   Register offsets for MN
 */
#define QLCNIC_MIU_CONTROL	(0x000)
#define QLCNIC_MIU_MN_CONTROL	(QLCNIC_CRB_DDR_NET+QLCNIC_MIU_CONTROL)

/* 200ms delay in each loop */
#define QLCNIC_NIU_PHY_WAITLEN		200000
/* 10 seconds before we give up */
#define QLCNIC_NIU_PHY_WAITMAX		50
#define QLCNIC_NIU_MAX_GBE_PORTS	4
#define QLCNIC_NIU_MAX_XG_PORTS		2

#define QLCNIC_NIU_MODE			(QLCNIC_CRB_NIU + 0x00000)
#define QLCNIC_NIU_GB_PAUSE_CTL		(QLCNIC_CRB_NIU + 0x0030c)
#define QLCNIC_NIU_XG_PAUSE_CTL		(QLCNIC_CRB_NIU + 0x00098)

#define QLCNIC_NIU_GB_MAC_CONFIG_0(I)		\
		(QLCNIC_CRB_NIU + 0x30000 + (I)*0x10000)
#define QLCNIC_NIU_GB_MAC_CONFIG_1(I)		\
		(QLCNIC_CRB_NIU + 0x30004 + (I)*0x10000)


#define TEST_AGT_CTRL	(0x00)

#define TA_CTL_START	BIT_0
#define TA_CTL_ENABLE	BIT_1
#define TA_CTL_WRITE	BIT_2
#define TA_CTL_BUSY	BIT_3

/*
 *   Register offsets for MN
 */
#define MIU_TEST_AGT_BASE		(0x90)

#define MIU_TEST_AGT_ADDR_LO		(0x04)
#define MIU_TEST_AGT_ADDR_HI		(0x08)
#define MIU_TEST_AGT_WRDATA_LO		(0x10)
#define MIU_TEST_AGT_WRDATA_HI		(0x14)
#define MIU_TEST_AGT_WRDATA_UPPER_LO	(0x20)
#define MIU_TEST_AGT_WRDATA_UPPER_HI	(0x24)
#define MIU_TEST_AGT_WRDATA(i)		(0x10+(0x10*((i)>>1))+(4*((i)&1)))
#define MIU_TEST_AGT_RDDATA_LO		(0x18)
#define MIU_TEST_AGT_RDDATA_HI		(0x1c)
#define MIU_TEST_AGT_RDDATA_UPPER_LO	(0x28)
#define MIU_TEST_AGT_RDDATA_UPPER_HI	(0x2c)
#define MIU_TEST_AGT_RDDATA(i)		(0x18+(0x10*((i)>>1))+(4*((i)&1)))

#define MIU_TEST_AGT_ADDR_MASK		0xfffffff8
#define MIU_TEST_AGT_UPPER_ADDR(off)	(0)

/*
 *   Register offsets for MS
 */
#define SIU_TEST_AGT_BASE		(0x60)

#define SIU_TEST_AGT_ADDR_LO		(0x04)
#define SIU_TEST_AGT_ADDR_HI		(0x18)
#define SIU_TEST_AGT_WRDATA_LO		(0x08)
#define SIU_TEST_AGT_WRDATA_HI		(0x0c)
#define SIU_TEST_AGT_WRDATA(i)		(0x08+(4*(i)))
#define SIU_TEST_AGT_RDDATA_LO		(0x10)
#define SIU_TEST_AGT_RDDATA_HI		(0x14)
#define SIU_TEST_AGT_RDDATA(i)		(0x10+(4*(i)))

#define SIU_TEST_AGT_ADDR_MASK		0x3ffff8
#define SIU_TEST_AGT_UPPER_ADDR(off)	((off)>>22)

/* XG Link status */
#define XG_LINK_UP	0x10
#define XG_LINK_DOWN	0x20

#define XG_LINK_UP_P3P	0x01
#define XG_LINK_DOWN_P3P	0x02
#define XG_LINK_STATE_P3P_MASK 0xf
#define XG_LINK_STATE_P3P(pcifn, val) \
	(((val) >> ((pcifn) * 4)) & XG_LINK_STATE_P3P_MASK)

#define P3P_LINK_SPEED_MHZ	100
#define P3P_LINK_SPEED_MASK	0xff
#define P3P_LINK_SPEED_REG(pcifn)	\
	(CRB_PF_LINK_SPEED_1 + (((pcifn) / 4) * 4))
#define P3P_LINK_SPEED_VAL(pcifn, reg)	\
	(((reg) >> (8 * ((pcifn) & 0x3))) & P3P_LINK_SPEED_MASK)

#define QLCNIC_CAM_RAM_BASE	(QLCNIC_CRB_CAM + 0x02000)
#define QLCNIC_CAM_RAM(reg)	(QLCNIC_CAM_RAM_BASE + (reg))
#define QLCNIC_FW_VERSION_MAJOR (QLCNIC_CAM_RAM(0x150))
#define QLCNIC_FW_VERSION_MINOR (QLCNIC_CAM_RAM(0x154))
#define QLCNIC_FW_VERSION_SUB	(QLCNIC_CAM_RAM(0x158))
#define QLCNIC_ROM_LOCK_ID	(QLCNIC_CAM_RAM(0x100))
#define QLCNIC_PHY_LOCK_ID	(QLCNIC_CAM_RAM(0x120))
#define QLCNIC_CRB_WIN_LOCK_ID	(QLCNIC_CAM_RAM(0x124))

#define NIC_CRB_BASE		(QLCNIC_CAM_RAM(0x200))
#define NIC_CRB_BASE_2		(QLCNIC_CAM_RAM(0x700))
#define QLCNIC_REG(X)		(NIC_CRB_BASE+(X))
#define QLCNIC_REG_2(X) 	(NIC_CRB_BASE_2+(X))

#define QLCNIC_CDRP_CRB_OFFSET		(QLCNIC_REG(0x18))
#define QLCNIC_ARG1_CRB_OFFSET		(QLCNIC_REG(0x1c))
#define QLCNIC_ARG2_CRB_OFFSET		(QLCNIC_REG(0x20))
#define QLCNIC_ARG3_CRB_OFFSET		(QLCNIC_REG(0x24))
#define QLCNIC_SIGN_CRB_OFFSET		(QLCNIC_REG(0x28))

#define CRB_CMDPEG_STATE		(QLCNIC_REG(0x50))
#define CRB_RCVPEG_STATE		(QLCNIC_REG(0x13c))

#define CRB_XG_STATE_P3P		(QLCNIC_REG(0x98))
#define CRB_PF_LINK_SPEED_1		(QLCNIC_REG(0xe8))
#define CRB_PF_LINK_SPEED_2		(QLCNIC_REG(0xec))

#define CRB_TEMP_STATE			(QLCNIC_REG(0x1b4))

#define CRB_V2P_0			(QLCNIC_REG(0x290))
#define CRB_V2P(port)			(CRB_V2P_0+((port)*4))
#define CRB_DRIVER_VERSION		(QLCNIC_REG(0x2a0))

#define CRB_FW_CAPABILITIES_1		(QLCNIC_CAM_RAM(0x128))
#define CRB_MAC_BLOCK_START		(QLCNIC_CAM_RAM(0x1c0))

/*
 * CrbPortPhanCntrHi/Lo is used to pass the address of HostPhantomIndex address
 * which can be read by the Phantom host to get producer/consumer indexes from
 * Phantom/Casper. If it is not HOST_SHARED_MEMORY, then the following
 * registers will be used for the addresses of the ring's shared memory
 * on the Phantom.
 */

#define qlcnic_get_temp_val(x)		((x) >> 16)
#define qlcnic_get_temp_state(x)	((x) & 0xffff)
#define qlcnic_encode_temp(val, state)	(((val) << 16) | (state))

/*
 * Temperature control.
 */
enum {
	QLCNIC_TEMP_NORMAL = 0x1,	/* Normal operating range */
	QLCNIC_TEMP_WARN,	/* Sound alert, temperature getting high */
	QLCNIC_TEMP_PANIC	/* Fatal error, hardware has shut down. */
};


/* Lock IDs for PHY lock */
#define PHY_LOCK_DRIVER		0x44524956

/* Used for PS PCI Memory access */
#define PCIX_PS_OP_ADDR_LO	(0x10000)
/*   via CRB  (PS side only)     */
#define PCIX_PS_OP_ADDR_HI	(0x10004)

#define PCIX_INT_VECTOR 	(0x10100)
#define PCIX_INT_MASK		(0x10104)

#define PCIX_OCM_WINDOW		(0x10800)
#define PCIX_OCM_WINDOW_REG(func)	(PCIX_OCM_WINDOW + 0x4 * (func))

#define PCIX_TARGET_STATUS	(0x10118)
#define PCIX_TARGET_STATUS_F1	(0x10160)
#define PCIX_TARGET_STATUS_F2	(0x10164)
#define PCIX_TARGET_STATUS_F3	(0x10168)
#define PCIX_TARGET_STATUS_F4	(0x10360)
#define PCIX_TARGET_STATUS_F5	(0x10364)
#define PCIX_TARGET_STATUS_F6	(0x10368)
#define PCIX_TARGET_STATUS_F7	(0x1036c)

#define PCIX_TARGET_MASK	(0x10128)
#define PCIX_TARGET_MASK_F1	(0x10170)
#define PCIX_TARGET_MASK_F2	(0x10174)
#define PCIX_TARGET_MASK_F3	(0x10178)
#define PCIX_TARGET_MASK_F4	(0x10370)
#define PCIX_TARGET_MASK_F5	(0x10374)
#define PCIX_TARGET_MASK_F6	(0x10378)
#define PCIX_TARGET_MASK_F7	(0x1037c)

#define PCIX_MSI_F(i)		(0x13000+((i)*4))

#define QLCNIC_PCIX_PH_REG(reg)	(QLCNIC_CRB_PCIE + (reg))
#define QLCNIC_PCIX_PS_REG(reg)	(QLCNIC_CRB_PCIX_MD + (reg))
#define QLCNIC_PCIE_REG(reg)	(QLCNIC_CRB_PCIE + (reg))

#define PCIE_SEM0_LOCK		(0x1c000)
#define PCIE_SEM0_UNLOCK	(0x1c004)
#define PCIE_SEM_LOCK(N)	(PCIE_SEM0_LOCK + 8*(N))
#define PCIE_SEM_UNLOCK(N)	(PCIE_SEM0_UNLOCK + 8*(N))

#define PCIE_SETUP_FUNCTION	(0x12040)
#define PCIE_SETUP_FUNCTION2	(0x12048)
#define PCIE_MISCCFG_RC         (0x1206c)
#define PCIE_TGT_SPLIT_CHICKEN	(0x12080)
#define PCIE_CHICKEN3		(0x120c8)

#define ISR_INT_STATE_REG       (QLCNIC_PCIX_PS_REG(PCIE_MISCCFG_RC))
#define PCIE_MAX_MASTER_SPLIT	(0x14048)

#define QLCNIC_PORT_MODE_NONE		0
#define QLCNIC_PORT_MODE_XG		1
#define QLCNIC_PORT_MODE_GB		2
#define QLCNIC_PORT_MODE_802_3_AP	3
#define QLCNIC_PORT_MODE_AUTO_NEG	4
#define QLCNIC_PORT_MODE_AUTO_NEG_1G	5
#define QLCNIC_PORT_MODE_AUTO_NEG_XG	6
#define QLCNIC_PORT_MODE_ADDR		(QLCNIC_CAM_RAM(0x24))
#define QLCNIC_WOL_PORT_MODE		(QLCNIC_CAM_RAM(0x198))

#define QLCNIC_WOL_CONFIG_NV		(QLCNIC_CAM_RAM(0x184))
#define QLCNIC_WOL_CONFIG		(QLCNIC_CAM_RAM(0x188))

#define QLCNIC_PEG_TUNE_MN_PRESENT	0x1
#define QLCNIC_PEG_TUNE_CAPABILITY	(QLCNIC_CAM_RAM(0x02c))

#define QLCNIC_DMA_WATCHDOG_CTRL	(QLCNIC_CAM_RAM(0x14))
#define QLCNIC_PEG_ALIVE_COUNTER	(QLCNIC_CAM_RAM(0xb0))
#define QLCNIC_PEG_HALT_STATUS1 	(QLCNIC_CAM_RAM(0xa8))
#define QLCNIC_PEG_HALT_STATUS2 	(QLCNIC_CAM_RAM(0xac))
#define QLCNIC_CRB_DRV_ACTIVE	(QLCNIC_CAM_RAM(0x138))
#define QLCNIC_CRB_DEV_STATE		(QLCNIC_CAM_RAM(0x140))

#define QLCNIC_CRB_DRV_STATE		(QLCNIC_CAM_RAM(0x144))
#define QLCNIC_CRB_DRV_SCRATCH		(QLCNIC_CAM_RAM(0x148))
#define QLCNIC_CRB_DEV_PARTITION_INFO	(QLCNIC_CAM_RAM(0x14c))
#define QLCNIC_CRB_DRV_IDC_VER		(QLCNIC_CAM_RAM(0x174))
#define QLCNIC_CRB_DEV_NPAR_STATE	(QLCNIC_CAM_RAM(0x19c))
#define QLCNIC_ROM_DEV_INIT_TIMEOUT	(0x3e885c)
#define QLCNIC_ROM_DRV_RESET_TIMEOUT	(0x3e8860)

/* Device State */
#define QLCNIC_DEV_COLD			0x1
#define QLCNIC_DEV_INITIALIZING		0x2
#define QLCNIC_DEV_READY		0x3
#define QLCNIC_DEV_NEED_RESET		0x4
#define QLCNIC_DEV_NEED_QUISCENT	0x5
#define QLCNIC_DEV_FAILED		0x6
#define QLCNIC_DEV_QUISCENT		0x7

#define QLCNIC_DEV_BADBAD		0xbad0bad0

#define QLCNIC_DEV_NPAR_NON_OPER	0 /* NON Operational */
#define QLCNIC_DEV_NPAR_OPER		1 /* NPAR Operational */
#define QLCNIC_DEV_NPAR_OPER_TIMEO	30 /* Operational time out */

#define QLC_DEV_CHECK_ACTIVE(VAL, FN)		((VAL) & (1 << (FN * 4)))
#define QLC_DEV_SET_REF_CNT(VAL, FN)		((VAL) |= (1 << (FN * 4)))
#define QLC_DEV_CLR_REF_CNT(VAL, FN)		((VAL) &= ~(1 << (FN * 4)))
#define QLC_DEV_SET_RST_RDY(VAL, FN)		((VAL) |= (1 << (FN * 4)))
#define QLC_DEV_SET_QSCNT_RDY(VAL, FN)		((VAL) |= (2 << (FN * 4)))
#define QLC_DEV_CLR_RST_QSCNT(VAL, FN)		((VAL) &= ~(3 << (FN * 4)))

#define QLC_DEV_GET_DRV(VAL, FN)		(0xf & ((VAL) >> (FN * 4)))
#define QLC_DEV_SET_DRV(VAL, FN)		((VAL) << (FN * 4))

#define QLCNIC_TYPE_NIC		1
#define QLCNIC_TYPE_FCOE		2
#define QLCNIC_TYPE_ISCSI		3

#define QLCNIC_RCODE_DRIVER_INFO		0x20000000
#define QLCNIC_RCODE_DRIVER_CAN_RELOAD		BIT_30
#define QLCNIC_RCODE_FATAL_ERROR		BIT_31
#define QLCNIC_FWERROR_PEGNUM(code)		((code) & 0xff)
#define QLCNIC_FWERROR_CODE(code)		((code >> 8) & 0x1fffff)
#define QLCNIC_FWERROR_FAN_FAILURE		0x16

#define FW_POLL_DELAY		(1 * HZ)
#define FW_FAIL_THRESH		2

#define QLCNIC_RESET_TIMEOUT_SECS	10
#define QLCNIC_INIT_TIMEOUT_SECS	30
#define QLCNIC_RCVPEG_CHECK_RETRY_COUNT	2000
#define QLCNIC_RCVPEG_CHECK_DELAY	10
#define QLCNIC_CMDPEG_CHECK_RETRY_COUNT	60
#define QLCNIC_CMDPEG_CHECK_DELAY	500
#define QLCNIC_HEARTBEAT_PERIOD_MSECS	200
#define QLCNIC_HEARTBEAT_CHECK_RETRY_COUNT	45

#define	ISR_MSI_INT_TRIGGER(FUNC) (QLCNIC_PCIX_PS_REG(PCIX_MSI_F(FUNC)))
#define ISR_LEGACY_INT_TRIGGERED(VAL)	(((VAL) & 0x300) == 0x200)

/*
 * PCI Interrupt Vector Values.
 */
#define	PCIX_INT_VECTOR_BIT_F0	0x0080
#define	PCIX_INT_VECTOR_BIT_F1	0x0100
#define	PCIX_INT_VECTOR_BIT_F2	0x0200
#define	PCIX_INT_VECTOR_BIT_F3	0x0400
#define	PCIX_INT_VECTOR_BIT_F4	0x0800
#define	PCIX_INT_VECTOR_BIT_F5	0x1000
#define	PCIX_INT_VECTOR_BIT_F6	0x2000
#define	PCIX_INT_VECTOR_BIT_F7	0x4000

struct qlcnic_legacy_intr_set {
	u32	int_vec_bit;
	u32	tgt_status_reg;
	u32	tgt_mask_reg;
	u32	pci_int_reg;
};

#define QLCNIC_FW_API		0x1b216c
#define QLCNIC_DRV_OP_MODE	0x1b2170
#define QLCNIC_MSIX_BASE	0x132110
#define QLCNIC_MAX_PCI_FUNC	8
#define QLCNIC_MAX_VLAN_FILTERS	64

/* FW dump defines */
#define MIU_TEST_CTR		0x41000090
#define MIU_TEST_ADDR_LO	0x41000094
#define MIU_TEST_ADDR_HI	0x41000098
#define FLASH_ROM_WINDOW	0x42110030
#define FLASH_ROM_DATA		0x42150000


static const u32 FW_DUMP_LEVELS[] = {
	0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff };

static const u32 MIU_TEST_READ_DATA[] = {
	0x410000A8, 0x410000AC, 0x410000B8, 0x410000BC, };

#define QLCNIC_FW_DUMP_REG1	0x00130060
#define QLCNIC_FW_DUMP_REG2	0x001e0000
#define QLCNIC_FLASH_SEM2_LK	0x0013C010
#define QLCNIC_FLASH_SEM2_ULK	0x0013C014
#define QLCNIC_FLASH_LOCK_ID	0x001B2100

#define QLCNIC_RD_DUMP_REG(addr, bar0, data) do {			\
	writel((addr & 0xFFFF0000), (void *) (bar0 +			\
		QLCNIC_FW_DUMP_REG1));					\
	readl((void *) (bar0 + QLCNIC_FW_DUMP_REG1));			\
	*data = readl((void *) (bar0 + QLCNIC_FW_DUMP_REG2 +		\
		LSW(addr)));						\
} while (0)

#define QLCNIC_WR_DUMP_REG(addr, bar0, data) do {			\
	writel((addr & 0xFFFF0000), (void *) (bar0 +			\
		QLCNIC_FW_DUMP_REG1));					\
	readl((void *) (bar0 + QLCNIC_FW_DUMP_REG1));			\
	writel(data, (void *) (bar0 + QLCNIC_FW_DUMP_REG2 + LSW(addr)));\
	readl((void *) (bar0 + QLCNIC_FW_DUMP_REG2 + LSW(addr)));	\
} while (0)

/* PCI function operational mode */
enum {
	QLCNIC_MGMT_FUNC	= 0,
	QLCNIC_PRIV_FUNC	= 1,
	QLCNIC_NON_PRIV_FUNC	= 2
};

enum {
	QLCNIC_PORT_DEFAULTS	= 0,
	QLCNIC_ADD_VLAN	= 1,
	QLCNIC_DEL_VLAN	= 2
};

#define QLC_DEV_DRV_DEFAULT 0x11111111

#define LSB(x)	((uint8_t)(x))
#define MSB(x)	((uint8_t)((uint16_t)(x) >> 8))

#define LSW(x)  ((uint16_t)((uint32_t)(x)))
#define MSW(x)  ((uint16_t)((uint32_t)(x) >> 16))

#define LSD(x)  ((uint32_t)((uint64_t)(x)))
#define MSD(x)  ((uint32_t)((((uint64_t)(x)) >> 16) >> 16))

#define	QLCNIC_LEGACY_INTR_CONFIG					\
{									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F0,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS,		\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(0) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F1,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F1,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F1,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(1) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F2,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F2,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F2,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(2) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F3,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F3,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F3,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(3) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F4,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F4,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F4,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(4) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F5,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F5,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F5,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(5) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F6,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F6,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F6,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(6) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F7,		\
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F7,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F7,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(7) },	\
}

/* NIU REGS */

#define _qlcnic_crb_get_bit(var, bit)  ((var >> bit) & 0x1)

/*
 * NIU GB MAC Config Register 0 (applies to GB0, GB1, GB2, GB3)
 *
 *	Bit 0 : enable_tx => 1:enable frame xmit, 0:disable
 *	Bit 1 : tx_synced => R/O: xmit enable synched to xmit stream
 *	Bit 2 : enable_rx => 1:enable frame recv, 0:disable
 *	Bit 3 : rx_synced => R/O: recv enable synched to recv stream
 *	Bit 4 : tx_flowctl => 1:enable pause frame generation, 0:disable
 *	Bit 5 : rx_flowctl => 1:act on recv'd pause frames, 0:ignore
 *	Bit 8 : loopback => 1:loop MAC xmits to MAC recvs, 0:normal
 *	Bit 16: tx_reset_pb => 1:reset frame xmit protocol blk, 0:no-op
 *	Bit 17: rx_reset_pb => 1:reset frame recv protocol blk, 0:no-op
 *	Bit 18: tx_reset_mac => 1:reset data/ctl multiplexer blk, 0:no-op
 *	Bit 19: rx_reset_mac => 1:reset ctl frames & timers blk, 0:no-op
 *	Bit 31: soft_reset => 1:reset the MAC and the SERDES, 0:no-op
 */
#define qlcnic_gb_rx_flowctl(config_word)	\
	((config_word) |= 1 << 5)
#define qlcnic_gb_get_rx_flowctl(config_word)	\
	_qlcnic_crb_get_bit((config_word), 5)
#define qlcnic_gb_unset_rx_flowctl(config_word)	\
	((config_word) &= ~(1 << 5))

/*
 * NIU GB Pause Ctl Register
 */

#define qlcnic_gb_set_gb0_mask(config_word)    \
	((config_word) |= 1 << 0)
#define qlcnic_gb_set_gb1_mask(config_word)    \
	((config_word) |= 1 << 2)
#define qlcnic_gb_set_gb2_mask(config_word)    \
	((config_word) |= 1 << 4)
#define qlcnic_gb_set_gb3_mask(config_word)    \
	((config_word) |= 1 << 6)

#define qlcnic_gb_get_gb0_mask(config_word)    \
	_qlcnic_crb_get_bit((config_word), 0)
#define qlcnic_gb_get_gb1_mask(config_word)    \
	_qlcnic_crb_get_bit((config_word), 2)
#define qlcnic_gb_get_gb2_mask(config_word)    \
	_qlcnic_crb_get_bit((config_word), 4)
#define qlcnic_gb_get_gb3_mask(config_word)    \
	_qlcnic_crb_get_bit((config_word), 6)

#define qlcnic_gb_unset_gb0_mask(config_word)  \
	((config_word) &= ~(1 << 0))
#define qlcnic_gb_unset_gb1_mask(config_word)  \
	((config_word) &= ~(1 << 2))
#define qlcnic_gb_unset_gb2_mask(config_word)  \
	((config_word) &= ~(1 << 4))
#define qlcnic_gb_unset_gb3_mask(config_word)  \
	((config_word) &= ~(1 << 6))

/*
 * NIU XG Pause Ctl Register
 *
 *      Bit 0       : xg0_mask => 1:disable tx pause frames
 *      Bit 1       : xg0_request => 1:request single pause frame
 *      Bit 2       : xg0_on_off => 1:request is pause on, 0:off
 *      Bit 3       : xg1_mask => 1:disable tx pause frames
 *      Bit 4       : xg1_request => 1:request single pause frame
 *      Bit 5       : xg1_on_off => 1:request is pause on, 0:off
 */

#define qlcnic_xg_set_xg0_mask(config_word)    \
	((config_word) |= 1 << 0)
#define qlcnic_xg_set_xg1_mask(config_word)    \
	((config_word) |= 1 << 3)

#define qlcnic_xg_get_xg0_mask(config_word)    \
	_qlcnic_crb_get_bit((config_word), 0)
#define qlcnic_xg_get_xg1_mask(config_word)    \
	_qlcnic_crb_get_bit((config_word), 3)

#define qlcnic_xg_unset_xg0_mask(config_word)  \
	((config_word) &= ~(1 << 0))
#define qlcnic_xg_unset_xg1_mask(config_word)  \
	((config_word) &= ~(1 << 3))

/*
 * NIU XG Pause Ctl Register
 *
 *      Bit 0       : xg0_mask => 1:disable tx pause frames
 *      Bit 1       : xg0_request => 1:request single pause frame
 *      Bit 2       : xg0_on_off => 1:request is pause on, 0:off
 *      Bit 3       : xg1_mask => 1:disable tx pause frames
 *      Bit 4       : xg1_request => 1:request single pause frame
 *      Bit 5       : xg1_on_off => 1:request is pause on, 0:off
 */

/*
 * PHY-Specific MII control/status registers.
 */
#define QLCNIC_NIU_GB_MII_MGMT_ADDR_AUTONEG		4
#define QLCNIC_NIU_GB_MII_MGMT_ADDR_PHY_STATUS		17

/*
 * PHY-Specific Status Register (reg 17).
 *
 * Bit 0      : jabber => 1:jabber detected, 0:not
 * Bit 1      : polarity => 1:polarity reversed, 0:normal
 * Bit 2      : recvpause => 1:receive pause enabled, 0:disabled
 * Bit 3      : xmitpause => 1:transmit pause enabled, 0:disabled
 * Bit 4      : energydetect => 1:sleep, 0:active
 * Bit 5      : downshift => 1:downshift, 0:no downshift
 * Bit 6      : crossover => 1:MDIX (crossover), 0:MDI (no crossover)
 * Bits 7-9   : cablelen => not valid in 10Mb/s mode
 *			0:<50m, 1:50-80m, 2:80-110m, 3:110-140m, 4:>140m
 * Bit 10     : link => 1:link up, 0:link down
 * Bit 11     : resolved => 1:speed and duplex resolved, 0:not yet
 * Bit 12     : pagercvd => 1:page received, 0:page not received
 * Bit 13     : duplex => 1:full duplex, 0:half duplex
 * Bits 14-15 : speed => 0:10Mb/s, 1:100Mb/s, 2:1000Mb/s, 3:rsvd
 */

#define qlcnic_get_phy_speed(config_word) (((config_word) >> 14) & 0x03)

#define qlcnic_set_phy_speed(config_word, val)	\
		((config_word) |= ((val & 0x03) << 14))
#define qlcnic_set_phy_duplex(config_word)	\
		((config_word) |= 1 << 13)
#define qlcnic_clear_phy_duplex(config_word)	\
		((config_word) &= ~(1 << 13))

#define qlcnic_get_phy_link(config_word)	\
		_qlcnic_crb_get_bit(config_word, 10)
#define qlcnic_get_phy_duplex(config_word)	\
		_qlcnic_crb_get_bit(config_word, 13)

#define QLCNIC_NIU_NON_PROMISC_MODE	0
#define QLCNIC_NIU_PROMISC_MODE		1
#define QLCNIC_NIU_ALLMULTI_MODE	2

struct crb_128M_2M_sub_block_map {
	unsigned valid;
	unsigned start_128M;
	unsigned end_128M;
	unsigned start_2M;
};

struct crb_128M_2M_block_map{
	struct crb_128M_2M_sub_block_map sub_block[16];
};
#endif				/* __QLCNIC_HDR_H_ */
