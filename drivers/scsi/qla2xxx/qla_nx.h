/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef __QLA_NX_H
#define __QLA_NX_H

/*
 * Following are the states of the Phantom. Phantom will set them and
 * Host will read to check if the fields are correct.
*/
#define PHAN_INITIALIZE_FAILED	      0xffff
#define PHAN_INITIALIZE_COMPLETE      0xff01

/* Host writes the following to notify that it has done the init-handshake */
#define PHAN_INITIALIZE_ACK	      0xf00f
#define PHAN_PEG_RCV_INITIALIZED      0xff01

/*CRB_RELATED*/
#define QLA82XX_CRB_BASE	QLA82XX_CAM_RAM(0x200)
#define QLA82XX_REG(X)		(QLA82XX_CRB_BASE+(X))

#define CRB_CMDPEG_STATE		QLA82XX_REG(0x50)
#define CRB_RCVPEG_STATE		QLA82XX_REG(0x13c)
#define BOOT_LOADER_DIMM_STATUS		QLA82XX_REG(0x54)
#define CRB_DMA_SHIFT			QLA82XX_REG(0xcc)
#define CRB_TEMP_STATE			QLA82XX_REG(0x1b4)
#define QLA82XX_DMA_SHIFT_VALUE		0x55555555

#define QLA82XX_HW_H0_CH_HUB_ADR    0x05
#define QLA82XX_HW_H1_CH_HUB_ADR    0x0E
#define QLA82XX_HW_H2_CH_HUB_ADR    0x03
#define QLA82XX_HW_H3_CH_HUB_ADR    0x01
#define QLA82XX_HW_H4_CH_HUB_ADR    0x06
#define QLA82XX_HW_H5_CH_HUB_ADR    0x07
#define QLA82XX_HW_H6_CH_HUB_ADR    0x08

/*  Hub 0 */
#define QLA82XX_HW_MN_CRB_AGT_ADR   0x15
#define QLA82XX_HW_MS_CRB_AGT_ADR   0x25

/*  Hub 1 */
#define QLA82XX_HW_PS_CRB_AGT_ADR	0x73
#define QLA82XX_HW_QMS_CRB_AGT_ADR	0x00
#define QLA82XX_HW_RPMX3_CRB_AGT_ADR	0x0b
#define QLA82XX_HW_SQGS0_CRB_AGT_ADR	0x01
#define QLA82XX_HW_SQGS1_CRB_AGT_ADR	0x02
#define QLA82XX_HW_SQGS2_CRB_AGT_ADR	0x03
#define QLA82XX_HW_SQGS3_CRB_AGT_ADR	0x04
#define QLA82XX_HW_C2C0_CRB_AGT_ADR	0x58
#define QLA82XX_HW_C2C1_CRB_AGT_ADR	0x59
#define QLA82XX_HW_C2C2_CRB_AGT_ADR	0x5a
#define QLA82XX_HW_RPMX2_CRB_AGT_ADR	0x0a
#define QLA82XX_HW_RPMX4_CRB_AGT_ADR	0x0c
#define QLA82XX_HW_RPMX7_CRB_AGT_ADR	0x0f
#define QLA82XX_HW_RPMX9_CRB_AGT_ADR	0x12
#define QLA82XX_HW_SMB_CRB_AGT_ADR	0x18

/*  Hub 2 */
#define QLA82XX_HW_NIU_CRB_AGT_ADR	0x31
#define QLA82XX_HW_I2C0_CRB_AGT_ADR	0x19
#define QLA82XX_HW_I2C1_CRB_AGT_ADR	0x29

#define QLA82XX_HW_SN_CRB_AGT_ADR	0x10
#define QLA82XX_HW_I2Q_CRB_AGT_ADR	0x20
#define QLA82XX_HW_LPC_CRB_AGT_ADR	0x22
#define QLA82XX_HW_ROMUSB_CRB_AGT_ADR	0x21
#define QLA82XX_HW_QM_CRB_AGT_ADR	0x66
#define QLA82XX_HW_SQG0_CRB_AGT_ADR	0x60
#define QLA82XX_HW_SQG1_CRB_AGT_ADR	0x61
#define QLA82XX_HW_SQG2_CRB_AGT_ADR	0x62
#define QLA82XX_HW_SQG3_CRB_AGT_ADR	0x63
#define QLA82XX_HW_RPMX1_CRB_AGT_ADR	0x09
#define QLA82XX_HW_RPMX5_CRB_AGT_ADR	0x0d
#define QLA82XX_HW_RPMX6_CRB_AGT_ADR	0x0e
#define QLA82XX_HW_RPMX8_CRB_AGT_ADR	0x11

/*  Hub 3 */
#define QLA82XX_HW_PH_CRB_AGT_ADR	0x1A
#define QLA82XX_HW_SRE_CRB_AGT_ADR	0x50
#define QLA82XX_HW_EG_CRB_AGT_ADR	0x51
#define QLA82XX_HW_RPMX0_CRB_AGT_ADR	0x08

/*  Hub 4 */
#define QLA82XX_HW_PEGN0_CRB_AGT_ADR	0x40
#define QLA82XX_HW_PEGN1_CRB_AGT_ADR	0x41
#define QLA82XX_HW_PEGN2_CRB_AGT_ADR	0x42
#define QLA82XX_HW_PEGN3_CRB_AGT_ADR	0x43
#define QLA82XX_HW_PEGNI_CRB_AGT_ADR	0x44
#define QLA82XX_HW_PEGND_CRB_AGT_ADR	0x45
#define QLA82XX_HW_PEGNC_CRB_AGT_ADR	0x46
#define QLA82XX_HW_PEGR0_CRB_AGT_ADR	0x47
#define QLA82XX_HW_PEGR1_CRB_AGT_ADR	0x48
#define QLA82XX_HW_PEGR2_CRB_AGT_ADR	0x49
#define QLA82XX_HW_PEGR3_CRB_AGT_ADR	0x4a
#define QLA82XX_HW_PEGN4_CRB_AGT_ADR	0x4b

/*  Hub 5 */
#define QLA82XX_HW_PEGS0_CRB_AGT_ADR	0x40
#define QLA82XX_HW_PEGS1_CRB_AGT_ADR	0x41
#define QLA82XX_HW_PEGS2_CRB_AGT_ADR	0x42
#define QLA82XX_HW_PEGS3_CRB_AGT_ADR	0x43
#define QLA82XX_HW_PEGSI_CRB_AGT_ADR	0x44
#define QLA82XX_HW_PEGSD_CRB_AGT_ADR	0x45
#define QLA82XX_HW_PEGSC_CRB_AGT_ADR	0x46

/*  Hub 6 */
#define QLA82XX_HW_CAS0_CRB_AGT_ADR	0x46
#define QLA82XX_HW_CAS1_CRB_AGT_ADR	0x47
#define QLA82XX_HW_CAS2_CRB_AGT_ADR	0x48
#define QLA82XX_HW_CAS3_CRB_AGT_ADR	0x49
#define QLA82XX_HW_NCM_CRB_AGT_ADR	0x16
#define QLA82XX_HW_TMR_CRB_AGT_ADR	0x17
#define QLA82XX_HW_XDMA_CRB_AGT_ADR	0x05
#define QLA82XX_HW_OCM0_CRB_AGT_ADR	0x06
#define QLA82XX_HW_OCM1_CRB_AGT_ADR	0x07

/*  This field defines PCI/X adr [25:20] of agents on the CRB */
/*  */
#define QLA82XX_HW_PX_MAP_CRB_PH	0
#define QLA82XX_HW_PX_MAP_CRB_PS	1
#define QLA82XX_HW_PX_MAP_CRB_MN	2
#define QLA82XX_HW_PX_MAP_CRB_MS	3
#define QLA82XX_HW_PX_MAP_CRB_SRE	5
#define QLA82XX_HW_PX_MAP_CRB_NIU	6
#define QLA82XX_HW_PX_MAP_CRB_QMN	7
#define QLA82XX_HW_PX_MAP_CRB_SQN0	8
#define QLA82XX_HW_PX_MAP_CRB_SQN1	9
#define QLA82XX_HW_PX_MAP_CRB_SQN2	10
#define QLA82XX_HW_PX_MAP_CRB_SQN3	11
#define QLA82XX_HW_PX_MAP_CRB_QMS	12
#define QLA82XX_HW_PX_MAP_CRB_SQS0	13
#define QLA82XX_HW_PX_MAP_CRB_SQS1	14
#define QLA82XX_HW_PX_MAP_CRB_SQS2	15
#define QLA82XX_HW_PX_MAP_CRB_SQS3	16
#define QLA82XX_HW_PX_MAP_CRB_PGN0	17
#define QLA82XX_HW_PX_MAP_CRB_PGN1	18
#define QLA82XX_HW_PX_MAP_CRB_PGN2	19
#define QLA82XX_HW_PX_MAP_CRB_PGN3	20
#define QLA82XX_HW_PX_MAP_CRB_PGN4	QLA82XX_HW_PX_MAP_CRB_SQS2
#define QLA82XX_HW_PX_MAP_CRB_PGND	21
#define QLA82XX_HW_PX_MAP_CRB_PGNI	22
#define QLA82XX_HW_PX_MAP_CRB_PGS0	23
#define QLA82XX_HW_PX_MAP_CRB_PGS1	24
#define QLA82XX_HW_PX_MAP_CRB_PGS2	25
#define QLA82XX_HW_PX_MAP_CRB_PGS3	26
#define QLA82XX_HW_PX_MAP_CRB_PGSD	27
#define QLA82XX_HW_PX_MAP_CRB_PGSI	28
#define QLA82XX_HW_PX_MAP_CRB_SN	29
#define QLA82XX_HW_PX_MAP_CRB_EG	31
#define QLA82XX_HW_PX_MAP_CRB_PH2	32
#define QLA82XX_HW_PX_MAP_CRB_PS2	33
#define QLA82XX_HW_PX_MAP_CRB_CAM	34
#define QLA82XX_HW_PX_MAP_CRB_CAS0	35
#define QLA82XX_HW_PX_MAP_CRB_CAS1	36
#define QLA82XX_HW_PX_MAP_CRB_CAS2	37
#define QLA82XX_HW_PX_MAP_CRB_C2C0	38
#define QLA82XX_HW_PX_MAP_CRB_C2C1	39
#define QLA82XX_HW_PX_MAP_CRB_TIMR	40
#define QLA82XX_HW_PX_MAP_CRB_RPMX1	42
#define QLA82XX_HW_PX_MAP_CRB_RPMX2	43
#define QLA82XX_HW_PX_MAP_CRB_RPMX3	44
#define QLA82XX_HW_PX_MAP_CRB_RPMX4	45
#define QLA82XX_HW_PX_MAP_CRB_RPMX5	46
#define QLA82XX_HW_PX_MAP_CRB_RPMX6	47
#define QLA82XX_HW_PX_MAP_CRB_RPMX7	48
#define QLA82XX_HW_PX_MAP_CRB_XDMA	49
#define QLA82XX_HW_PX_MAP_CRB_I2Q	50
#define QLA82XX_HW_PX_MAP_CRB_ROMUSB	51
#define QLA82XX_HW_PX_MAP_CRB_CAS3	52
#define QLA82XX_HW_PX_MAP_CRB_RPMX0	53
#define QLA82XX_HW_PX_MAP_CRB_RPMX8	54
#define QLA82XX_HW_PX_MAP_CRB_RPMX9	55
#define QLA82XX_HW_PX_MAP_CRB_OCM0	56
#define QLA82XX_HW_PX_MAP_CRB_OCM1	57
#define QLA82XX_HW_PX_MAP_CRB_SMB	58
#define QLA82XX_HW_PX_MAP_CRB_I2C0	59
#define QLA82XX_HW_PX_MAP_CRB_I2C1	60
#define QLA82XX_HW_PX_MAP_CRB_LPC	61
#define QLA82XX_HW_PX_MAP_CRB_PGNC	62
#define QLA82XX_HW_PX_MAP_CRB_PGR0	63
#define QLA82XX_HW_PX_MAP_CRB_PGR1	4
#define QLA82XX_HW_PX_MAP_CRB_PGR2	30
#define QLA82XX_HW_PX_MAP_CRB_PGR3	41

/*  This field defines CRB adr [31:20] of the agents */
/*  */

#define QLA82XX_HW_CRB_HUB_AGT_ADR_MN	    ((QLA82XX_HW_H0_CH_HUB_ADR << 7) | \
	QLA82XX_HW_MN_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PH	    ((QLA82XX_HW_H0_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PH_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_MS	    ((QLA82XX_HW_H0_CH_HUB_ADR << 7) | \
	QLA82XX_HW_MS_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PS	    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PS_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SS	    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SS_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX3    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_QMS	    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_QMS_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQS0     ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQGS0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQS1     ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQGS1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQS2     ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQGS2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQS3     ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQGS3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_C2C0     ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_C2C0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_C2C1     ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_C2C1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX2    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX4    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX4_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX7    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX7_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX9    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX9_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SMB	    ((QLA82XX_HW_H1_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SMB_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_NIU	    ((QLA82XX_HW_H2_CH_HUB_ADR << 7) | \
	QLA82XX_HW_NIU_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_I2C0     ((QLA82XX_HW_H2_CH_HUB_ADR << 7) | \
	QLA82XX_HW_I2C0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_I2C1     ((QLA82XX_HW_H2_CH_HUB_ADR << 7) | \
	QLA82XX_HW_I2C1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SRE	    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SRE_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_EG	    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_EG_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX0    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_QMN	    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_QM_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQN0     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQG0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQN1     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQG1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQN2     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQG2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SQN3     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SQG3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX1    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX5    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX5_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX6    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX6_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX8    ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_RPMX8_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_CAS0     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_CAS0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_CAS1     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_CAS1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_CAS2     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_CAS2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_CAS3     ((QLA82XX_HW_H3_CH_HUB_ADR << 7) | \
	QLA82XX_HW_CAS3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGNI     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGNI_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGND     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGND_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGN0     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGN0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGN1     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGN1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGN2     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGN2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGN3     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGN3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGN4	   ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGN4_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGNC     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGNC_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGR0     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGR0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGR1     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGR1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGR2     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGR2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGR3     ((QLA82XX_HW_H4_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGR3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGSI     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGSI_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGSD     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGSD_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGS0     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGS0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGS1     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGS1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGS2     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGS2_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGS3     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGS3_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_PGSC     ((QLA82XX_HW_H5_CH_HUB_ADR << 7) | \
	QLA82XX_HW_PEGSC_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_CAM	    ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_NCM_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_TIMR     ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_TMR_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_XDMA     ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_XDMA_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_SN	    ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_SN_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_I2Q	    ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_I2Q_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_ROMUSB   ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_ROMUSB_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_OCM0     ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_OCM0_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_OCM1     ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_OCM1_CRB_AGT_ADR)
#define QLA82XX_HW_CRB_HUB_AGT_ADR_LPC	    ((QLA82XX_HW_H6_CH_HUB_ADR << 7) | \
	QLA82XX_HW_LPC_CRB_AGT_ADR)

#define ROMUSB_GLB				(QLA82XX_CRB_ROMUSB + 0x00000)
#define QLA82XX_ROMUSB_GLB_PEGTUNE_DONE		(ROMUSB_GLB + 0x005c)
#define QLA82XX_ROMUSB_GLB_STATUS		(ROMUSB_GLB + 0x0004)
#define QLA82XX_ROMUSB_GLB_SW_RESET		(ROMUSB_GLB + 0x0008)
#define QLA82XX_ROMUSB_ROM_ADDRESS		(ROMUSB_ROM + 0x0008)
#define QLA82XX_ROMUSB_ROM_WDATA		(ROMUSB_ROM + 0x000c)
#define QLA82XX_ROMUSB_ROM_ABYTE_CNT		(ROMUSB_ROM + 0x0010)
#define QLA82XX_ROMUSB_ROM_DUMMY_BYTE_CNT	(ROMUSB_ROM + 0x0014)
#define QLA82XX_ROMUSB_ROM_RDATA		(ROMUSB_ROM + 0x0018)

#define ROMUSB_ROM				(QLA82XX_CRB_ROMUSB + 0x10000)
#define QLA82XX_ROMUSB_ROM_INSTR_OPCODE		(ROMUSB_ROM + 0x0004)
#define QLA82XX_ROMUSB_GLB_CAS_RST		(ROMUSB_GLB + 0x0038)

/* Lock IDs for ROM lock */
#define ROM_LOCK_DRIVER       0x0d417340

#define QLA82XX_PCI_CRB_WINDOWSIZE 0x00100000	 /* all are 1MB windows */
#define QLA82XX_PCI_CRB_WINDOW(A) \
	(QLA82XX_PCI_CRBSPACE + (A)*QLA82XX_PCI_CRB_WINDOWSIZE)
#define QLA82XX_CRB_C2C_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_C2C0)
#define QLA82XX_CRB_C2C_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_C2C1)
#define QLA82XX_CRB_C2C_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_C2C2)
#define QLA82XX_CRB_CAM \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_CAM)
#define QLA82XX_CRB_CASPER \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_CAS)
#define QLA82XX_CRB_CASPER_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_CAS0)
#define QLA82XX_CRB_CASPER_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_CAS1)
#define QLA82XX_CRB_CASPER_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_CAS2)
#define QLA82XX_CRB_DDR_MD \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_MS)
#define QLA82XX_CRB_DDR_NET \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_MN)
#define QLA82XX_CRB_EPG \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_EG)
#define QLA82XX_CRB_I2Q \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_I2Q)
#define QLA82XX_CRB_NIU \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_NIU)

#define QLA82XX_CRB_PCIX_HOST \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PH)
#define QLA82XX_CRB_PCIX_HOST2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PH2)
#define QLA82XX_CRB_PCIX_MD \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PS)
#define QLA82XX_CRB_PCIE \
	QLA82XX_CRB_PCIX_MD

/* window 1 pcie slot */
#define QLA82XX_CRB_PCIE2	 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PS2)
#define QLA82XX_CRB_PEG_MD_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGS0)
#define QLA82XX_CRB_PEG_MD_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGS1)
#define QLA82XX_CRB_PEG_MD_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGS2)
#define QLA82XX_CRB_PEG_MD_3 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGS3)
#define QLA82XX_CRB_PEG_MD_3 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGS3)
#define QLA82XX_CRB_PEG_MD_D \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGSD)
#define QLA82XX_CRB_PEG_MD_I \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGSI)
#define QLA82XX_CRB_PEG_NET_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGN0)
#define QLA82XX_CRB_PEG_NET_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGN1)
#define QLA82XX_CRB_PEG_NET_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGN2)
#define QLA82XX_CRB_PEG_NET_3 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGN3)
#define QLA82XX_CRB_PEG_NET_4 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGN4)
#define QLA82XX_CRB_PEG_NET_D \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGND)
#define QLA82XX_CRB_PEG_NET_I \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_PGNI)
#define QLA82XX_CRB_PQM_MD \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_QMS)
#define QLA82XX_CRB_PQM_NET \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_QMN)
#define QLA82XX_CRB_QDR_MD \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SS)
#define QLA82XX_CRB_QDR_NET \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SN)
#define QLA82XX_CRB_ROMUSB \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_ROMUSB)
#define QLA82XX_CRB_RPMX_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX0)
#define QLA82XX_CRB_RPMX_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX1)
#define QLA82XX_CRB_RPMX_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX2)
#define QLA82XX_CRB_RPMX_3 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX3)
#define QLA82XX_CRB_RPMX_4 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX4)
#define QLA82XX_CRB_RPMX_5 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX5)
#define QLA82XX_CRB_RPMX_6 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX6)
#define QLA82XX_CRB_RPMX_7 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_RPMX7)
#define QLA82XX_CRB_SQM_MD_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQS0)
#define QLA82XX_CRB_SQM_MD_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQS1)
#define QLA82XX_CRB_SQM_MD_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQS2)
#define QLA82XX_CRB_SQM_MD_3 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQS3)
#define QLA82XX_CRB_SQM_NET_0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQN0)
#define QLA82XX_CRB_SQM_NET_1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQN1)
#define QLA82XX_CRB_SQM_NET_2 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQN2)
#define QLA82XX_CRB_SQM_NET_3 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SQN3)
#define QLA82XX_CRB_SRE \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SRE)
#define QLA82XX_CRB_TIMER \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_TIMR)
#define QLA82XX_CRB_XDMA \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_XDMA)
#define QLA82XX_CRB_I2C0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_I2C0)
#define QLA82XX_CRB_I2C1 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_I2C1)
#define QLA82XX_CRB_OCM0 \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_OCM0)
#define QLA82XX_CRB_SMB \
	QLA82XX_PCI_CRB_WINDOW(QLA82XX_HW_PX_MAP_CRB_SMB)
#define QLA82XX_CRB_MAX \
	QLA82XX_PCI_CRB_WINDOW(64)

/*
 * ====================== BASE ADDRESSES ON-CHIP ======================
 * Base addresses of major components on-chip.
 * ====================== BASE ADDRESSES ON-CHIP ======================
 */
#define QLA82XX_ADDR_DDR_NET		(0x0000000000000000ULL)
#define QLA82XX_ADDR_DDR_NET_MAX	(0x000000000fffffffULL)

/* Imbus address bit used to indicate a host address. This bit is
 * eliminated by the pcie bar and bar select before presentation
 * over pcie. */
/* host memory via IMBUS */
#define QLA82XX_P2_ADDR_PCIE		(0x0000000800000000ULL)
#define QLA82XX_P3_ADDR_PCIE		(0x0000008000000000ULL)
#define QLA82XX_ADDR_PCIE_MAX		(0x0000000FFFFFFFFFULL)
#define QLA82XX_ADDR_OCM0		(0x0000000200000000ULL)
#define QLA82XX_ADDR_OCM0_MAX		(0x00000002000fffffULL)
#define QLA82XX_ADDR_OCM1		(0x0000000200400000ULL)
#define QLA82XX_ADDR_OCM1_MAX		(0x00000002004fffffULL)
#define QLA82XX_ADDR_QDR_NET		(0x0000000300000000ULL)
#define QLA82XX_P3_ADDR_QDR_NET_MAX	(0x0000000303ffffffULL)

#define QLA82XX_PCI_CRBSPACE		(unsigned long)0x06000000
#define QLA82XX_PCI_DIRECT_CRB		(unsigned long)0x04400000
#define QLA82XX_PCI_CAMQM		(unsigned long)0x04800000
#define QLA82XX_PCI_CAMQM_MAX		(unsigned long)0x04ffffff
#define QLA82XX_PCI_DDR_NET		(unsigned long)0x00000000
#define QLA82XX_PCI_QDR_NET		(unsigned long)0x04000000
#define QLA82XX_PCI_QDR_NET_MAX		(unsigned long)0x043fffff

/*
 *   Register offsets for MN
 */
#define MIU_CONTROL			(0x000)
#define MIU_TAG				(0x004)
#define MIU_TEST_AGT_CTRL		(0x090)
#define MIU_TEST_AGT_ADDR_LO		(0x094)
#define MIU_TEST_AGT_ADDR_HI		(0x098)
#define MIU_TEST_AGT_WRDATA_LO		(0x0a0)
#define MIU_TEST_AGT_WRDATA_HI		(0x0a4)
#define MIU_TEST_AGT_WRDATA(i)		(0x0a0+(4*(i)))
#define MIU_TEST_AGT_RDDATA_LO		(0x0a8)
#define MIU_TEST_AGT_RDDATA_HI		(0x0ac)
#define MIU_TEST_AGT_RDDATA(i)		(0x0a8+(4*(i)))
#define MIU_TEST_AGT_ADDR_MASK		0xfffffff8
#define MIU_TEST_AGT_UPPER_ADDR(off)	(0)

/* MIU_TEST_AGT_CTRL flags. work for SIU as well */
#define MIU_TA_CTL_START	1
#define MIU_TA_CTL_ENABLE	2
#define MIU_TA_CTL_WRITE	4
#define MIU_TA_CTL_BUSY		8

/*CAM RAM */
# define QLA82XX_CAM_RAM_BASE		(QLA82XX_CRB_CAM + 0x02000)
# define QLA82XX_CAM_RAM(reg)		(QLA82XX_CAM_RAM_BASE + (reg))

#define QLA82XX_PORT_MODE_ADDR		(QLA82XX_CAM_RAM(0x24))
#define QLA82XX_PEG_HALT_STATUS1	(QLA82XX_CAM_RAM(0xa8))
#define QLA82XX_PEG_HALT_STATUS2	(QLA82XX_CAM_RAM(0xac))
#define QLA82XX_PEG_ALIVE_COUNTER	(QLA82XX_CAM_RAM(0xb0))

#define QLA82XX_CAMRAM_DB1		(QLA82XX_CAM_RAM(0x1b8))
#define QLA82XX_CAMRAM_DB2		(QLA82XX_CAM_RAM(0x1bc))

#define HALT_STATUS_UNRECOVERABLE	0x80000000
#define HALT_STATUS_RECOVERABLE		0x40000000

/* Driver Coexistence Defines */
#define QLA82XX_CRB_DRV_ACTIVE	     (QLA82XX_CAM_RAM(0x138))
#define QLA82XX_CRB_DEV_STATE	     (QLA82XX_CAM_RAM(0x140))
#define QLA82XX_CRB_DRV_STATE	     (QLA82XX_CAM_RAM(0x144))
#define QLA82XX_CRB_DRV_SCRATCH      (QLA82XX_CAM_RAM(0x148))
#define QLA82XX_CRB_DEV_PART_INFO    (QLA82XX_CAM_RAM(0x14c))
#define QLA82XX_CRB_DRV_IDC_VERSION  (QLA82XX_CAM_RAM(0x174))

/* Every driver should use these Device State */
#define QLA8XXX_DEV_COLD		1
#define QLA8XXX_DEV_INITIALIZING	2
#define QLA8XXX_DEV_READY		3
#define QLA8XXX_DEV_NEED_RESET		4
#define QLA8XXX_DEV_NEED_QUIESCENT	5
#define QLA8XXX_DEV_FAILED		6
#define QLA8XXX_DEV_QUIESCENT		7
#define	MAX_STATES			8 /* Increment if new state added */
#define QLA8XXX_BAD_VALUE		0xbad0bad0

#define QLA82XX_IDC_VERSION			1
#define QLA82XX_ROM_DEV_INIT_TIMEOUT		30
#define QLA82XX_ROM_DRV_RESET_ACK_TIMEOUT	10

#define QLA82XX_ROM_LOCK_ID		(QLA82XX_CAM_RAM(0x100))
#define QLA82XX_CRB_WIN_LOCK_ID		(QLA82XX_CAM_RAM(0x124))
#define QLA82XX_FW_VERSION_MAJOR	(QLA82XX_CAM_RAM(0x150))
#define QLA82XX_FW_VERSION_MINOR	(QLA82XX_CAM_RAM(0x154))
#define QLA82XX_FW_VERSION_SUB		(QLA82XX_CAM_RAM(0x158))
#define QLA82XX_PCIE_REG(reg)		(QLA82XX_CRB_PCIE + (reg))

#define PCIE_SETUP_FUNCTION		(0x12040)
#define PCIE_SETUP_FUNCTION2		(0x12048)

#define QLA82XX_PCIX_PS_REG(reg)	(QLA82XX_CRB_PCIX_MD + (reg))
#define QLA82XX_PCIX_PS2_REG(reg)	(QLA82XX_CRB_PCIE2 + (reg))

#define PCIE_SEM2_LOCK	     (0x1c010)	/* Flash lock	*/
#define PCIE_SEM2_UNLOCK     (0x1c014)	/* Flash unlock */
#define PCIE_SEM5_LOCK	     (0x1c028)	/* Coexistence lock   */
#define PCIE_SEM5_UNLOCK     (0x1c02c)	/* Coexistence unlock */
#define PCIE_SEM7_LOCK	     (0x1c038)	/* crb win lock */
#define PCIE_SEM7_UNLOCK     (0x1c03c)	/* crbwin unlock*/

/* Different drive state */
#define QLA82XX_DRVST_NOT_RDY		0
#define	QLA82XX_DRVST_RST_RDY		1
#define QLA82XX_DRVST_QSNT_RDY		2

/* Different drive active state */
#define QLA82XX_DRV_NOT_ACTIVE		0
#define QLA82XX_DRV_ACTIVE		1

/*
 * The PCI VendorID and DeviceID for our board.
 */
#define PCI_DEVICE_ID_QLOGIC_ISP8021		0x8021

#define QLA82XX_MSIX_TBL_SPACE			8192
#define QLA82XX_PCI_REG_MSIX_TBL		0x44
#define QLA82XX_PCI_MSIX_CONTROL		0x40

struct crb_128M_2M_sub_block_map {
	unsigned valid;
	unsigned start_128M;
	unsigned end_128M;
	unsigned start_2M;
};

struct crb_128M_2M_block_map {
	struct crb_128M_2M_sub_block_map sub_block[16];
};

struct crb_addr_pair {
	long addr;
	long data;
};

#define ADDR_ERROR ((unsigned long) 0xffffffff)
#define MAX_CTL_CHECK	1000

/***************************************************************************
 *		PCI related defines.
 **************************************************************************/

/*
 * Interrupt related defines.
 */
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

/*
 * Message Signaled Interrupts
 */
#define PCIX_MSI_F0		(0x13000)
#define PCIX_MSI_F1		(0x13004)
#define PCIX_MSI_F2		(0x13008)
#define PCIX_MSI_F3		(0x1300c)
#define PCIX_MSI_F4		(0x13010)
#define PCIX_MSI_F5		(0x13014)
#define PCIX_MSI_F6		(0x13018)
#define PCIX_MSI_F7		(0x1301c)
#define PCIX_MSI_F(FUNC)	(0x13000 + ((FUNC) * 4))
#define PCIX_INT_VECTOR		(0x10100)
#define PCIX_INT_MASK		(0x10104)

/*
 * Interrupt state machine and other bits.
 */
#define PCIE_MISCCFG_RC		(0x1206c)

#define ISR_INT_TARGET_STATUS \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS))
#define ISR_INT_TARGET_STATUS_F1 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F1))
#define ISR_INT_TARGET_STATUS_F2 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F2))
#define ISR_INT_TARGET_STATUS_F3 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F3))
#define ISR_INT_TARGET_STATUS_F4 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F4))
#define ISR_INT_TARGET_STATUS_F5 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F5))
#define ISR_INT_TARGET_STATUS_F6 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F6))
#define ISR_INT_TARGET_STATUS_F7 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_STATUS_F7))

#define ISR_INT_TARGET_MASK \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK))
#define ISR_INT_TARGET_MASK_F1 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F1))
#define ISR_INT_TARGET_MASK_F2 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F2))
#define ISR_INT_TARGET_MASK_F3 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F3))
#define ISR_INT_TARGET_MASK_F4 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F4))
#define ISR_INT_TARGET_MASK_F5 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F5))
#define ISR_INT_TARGET_MASK_F6 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F6))
#define ISR_INT_TARGET_MASK_F7 \
	(QLA82XX_PCIX_PS_REG(PCIX_TARGET_MASK_F7))

#define ISR_INT_VECTOR \
	(QLA82XX_PCIX_PS_REG(PCIX_INT_VECTOR))
#define ISR_INT_MASK \
	(QLA82XX_PCIX_PS_REG(PCIX_INT_MASK))
#define ISR_INT_STATE_REG \
	(QLA82XX_PCIX_PS_REG(PCIE_MISCCFG_RC))

#define	ISR_MSI_INT_TRIGGER(FUNC) \
	(QLA82XX_PCIX_PS_REG(PCIX_MSI_F(FUNC)))

#define	ISR_IS_LEGACY_INTR_IDLE(VAL)		(((VAL) & 0x300) == 0)
#define	ISR_IS_LEGACY_INTR_TRIGGERED(VAL)	(((VAL) & 0x300) == 0x200)

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

struct qla82xx_legacy_intr_set {
	uint32_t	int_vec_bit;
	uint32_t	tgt_status_reg;
	uint32_t	tgt_mask_reg;
	uint32_t	pci_int_reg;
};

#define QLA82XX_LEGACY_INTR_CONFIG					\
{									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F0,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS,		\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(0) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F1,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F1,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F1,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(1) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F2,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F2,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F2,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(2) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F3,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F3,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F3,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(3) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F4,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F4,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F4,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(4) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F5,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F5,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F5,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(5) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F6,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F6,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F6,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(6) },	\
									\
	{								\
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F7,		\
		.tgt_status_reg =	ISR_INT_TARGET_STATUS_F7,	\
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F7,		\
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(7) },	\
}

#define BRDCFG_START		0x4000
#define	BOOTLD_START		0x10000
#define	IMAGE_START		0x100000
#define FLASH_ADDR_START	0x43000

/* Magic number to let user know flash is programmed */
#define QLA82XX_BDINFO_MAGIC	0x12345678
#define QLA82XX_FW_MAGIC_OFFSET	(BRDCFG_START + 0x128)
#define FW_SIZE_OFFSET		(0x3e840c)
#define QLA82XX_FW_MIN_SIZE	0x3fffff

/* UNIFIED ROMIMAGE START */
#define QLA82XX_URI_FW_MIN_SIZE			0xc8000
#define QLA82XX_URI_DIR_SECT_PRODUCT_TBL	0x0
#define QLA82XX_URI_DIR_SECT_BOOTLD		0x6
#define QLA82XX_URI_DIR_SECT_FW			0x7

/* Offsets */
#define QLA82XX_URI_CHIP_REV_OFF	10
#define QLA82XX_URI_FLAGS_OFF		11
#define QLA82XX_URI_BIOS_VERSION_OFF	12
#define QLA82XX_URI_BOOTLD_IDX_OFF	27
#define QLA82XX_URI_FIRMWARE_IDX_OFF	29

struct qla82xx_uri_table_desc{
	uint32_t	findex;
	uint32_t	num_entries;
	uint32_t	entry_size;
	uint32_t	reserved[5];
};

struct qla82xx_uri_data_desc{
	uint32_t	findex;
	uint32_t	size;
	uint32_t	reserved[5];
};

/* UNIFIED ROMIMAGE END */

#define QLA82XX_UNIFIED_ROMIMAGE	3
#define QLA82XX_FLASH_ROMIMAGE		4
#define QLA82XX_UNKNOWN_ROMIMAGE	0xff

#define MIU_TEST_AGT_WRDATA_UPPER_LO		(0x0b0)
#define	MIU_TEST_AGT_WRDATA_UPPER_HI		(0x0b4)

#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	return readl(addr) | (((u64) readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	writel(((u32) (val)), (addr));
	writel(((u32) (val >> 32)), (addr + 4));
}
#endif

/* Request and response queue size */
#define REQUEST_ENTRY_CNT_82XX		128	/* Number of request entries. */
#define RESPONSE_ENTRY_CNT_82XX		128	/* Number of response entries.*/

/*
 * ISP 8021 I/O Register Set structure definitions.
 */
struct device_reg_82xx {
	uint32_t req_q_out[64];		/* Request Queue out-Pointer (64 * 4) */
	uint32_t rsp_q_in[64];		/* Response Queue In-Pointer. */
	uint32_t rsp_q_out[64];		/* Response Queue Out-Pointer. */

	uint16_t mailbox_in[32];	/* Mail box In registers */
	uint16_t unused_1[32];
	uint32_t hint;			/* Host interrupt register */
#define	HINT_MBX_INT_PENDING	BIT_0
	uint16_t unused_2[62];
	uint16_t mailbox_out[32];	/* Mail box Out registers */
	uint32_t unused_3[48];

	uint32_t host_status;		/* host status */
#define HSRX_RISC_INT		BIT_15	/* RISC to Host interrupt. */
#define HSRX_RISC_PAUSED	BIT_8	/* RISC Paused. */
	uint32_t host_int;		/* Interrupt status. */
#define ISRX_NX_RISC_INT	BIT_0	/* RISC interrupt. */
};

struct fcp_cmnd {
	struct scsi_lun lun;
	uint8_t crn;
	uint8_t task_attribute;
	uint8_t task_management;
	uint8_t additional_cdb_len;
	uint8_t cdb[260]; /* 256 for CDB len and 4 for FCP_DL */
};

struct dsd_dma {
	struct list_head list;
	dma_addr_t dsd_list_dma;
	void *dsd_addr;
};

#define QLA_DSDS_PER_IOCB	37
#define QLA_DSD_SIZE		12
struct ct6_dsd {
	uint16_t fcp_cmnd_len;
	dma_addr_t fcp_cmnd_dma;
	struct fcp_cmnd *fcp_cmnd;
	int dsd_use_cnt;
	struct list_head dsd_list;
};

#define MBC_TOGGLE_INTERRUPT	0x10
#define MBC_SET_LED_CONFIG	0x125	/* FCoE specific LED control */
#define MBC_GET_LED_CONFIG	0x126	/* FCoE specific LED control */

/* Flash  offset */
#define FLT_REG_BOOTLOAD_82XX	0x72
#define FLT_REG_BOOT_CODE_82XX	0x78
#define FLT_REG_FW_82XX		0x74
#define FLT_REG_GOLD_FW_82XX	0x75
#define FLT_REG_VPD_8XXX	0x81

#define	FA_VPD_SIZE_82XX	0x400

#define FA_FLASH_LAYOUT_ADDR_82	0xFC400

/******************************************************************************
*
*    Definitions specific to M25P flash
*
*******************************************************************************
*   Instructions
*/
#define M25P_INSTR_WREN		0x06
#define M25P_INSTR_WRDI		0x04
#define M25P_INSTR_RDID		0x9f
#define M25P_INSTR_RDSR		0x05
#define M25P_INSTR_WRSR		0x01
#define M25P_INSTR_READ		0x03
#define M25P_INSTR_FAST_READ	0x0b
#define M25P_INSTR_PP		0x02
#define M25P_INSTR_SE		0xd8
#define M25P_INSTR_BE		0xc7
#define M25P_INSTR_DP		0xb9
#define M25P_INSTR_RES		0xab

/* Minidump related */

/*
 * Version of the template
 * 4 Bytes
 * X.Major.Minor.RELEASE
 */
#define QLA82XX_MINIDUMP_VERSION         0x10101

/*
 * Entry Type Defines
 */
#define QLA82XX_RDNOP                   0
#define QLA82XX_RDCRB                   1
#define QLA82XX_RDMUX                   2
#define QLA82XX_QUEUE                   3
#define QLA82XX_BOARD                   4
#define QLA82XX_RDSRE                   5
#define QLA82XX_RDOCM                   6
#define QLA82XX_CACHE                  10
#define QLA82XX_L1DAT                  11
#define QLA82XX_L1INS                  12
#define QLA82XX_L2DTG                  21
#define QLA82XX_L2ITG                  22
#define QLA82XX_L2DAT                  23
#define QLA82XX_L2INS                  24
#define QLA82XX_RDROM                  71
#define QLA82XX_RDMEM                  72
#define QLA82XX_CNTRL                  98
#define QLA82XX_TLHDR                  99
#define QLA82XX_RDEND                  255

/*
 * Opcodes for Control Entries.
 * These Flags are bit fields.
 */
#define QLA82XX_DBG_OPCODE_WR        0x01
#define QLA82XX_DBG_OPCODE_RW        0x02
#define QLA82XX_DBG_OPCODE_AND       0x04
#define QLA82XX_DBG_OPCODE_OR        0x08
#define QLA82XX_DBG_OPCODE_POLL      0x10
#define QLA82XX_DBG_OPCODE_RDSTATE   0x20
#define QLA82XX_DBG_OPCODE_WRSTATE   0x40
#define QLA82XX_DBG_OPCODE_MDSTATE   0x80

/*
 * Template Header and Entry Header definitions start here.
 */

/*
 * Template Header
 * Parts of the template header can be modified by the driver.
 * These include the saved_state_array, capture_debug_level, driver_timestamp
 */

#define QLA82XX_DBG_STATE_ARRAY_LEN        16
#define QLA82XX_DBG_CAP_SIZE_ARRAY_LEN     8
#define QLA82XX_DBG_RSVD_ARRAY_LEN         8

/*
 * Driver Flags
 */
#define QLA82XX_DBG_SKIPPED_FLAG	0x80	/* driver skipped this entry */
#define	QLA82XX_DEFAULT_CAP_MASK	0xFF	/* default capture mask */

struct qla82xx_md_template_hdr {
	uint32_t entry_type;
	uint32_t first_entry_offset;
	uint32_t size_of_template;
	uint32_t capture_debug_level;

	uint32_t num_of_entries;
	uint32_t version;
	uint32_t driver_timestamp;
	uint32_t template_checksum;

	uint32_t driver_capture_mask;
	uint32_t driver_info[3];

	uint32_t saved_state_array[QLA82XX_DBG_STATE_ARRAY_LEN];
	uint32_t capture_size_array[QLA82XX_DBG_CAP_SIZE_ARRAY_LEN];

	/*  markers_array used to capture some special locations on board */
	uint32_t markers_array[QLA82XX_DBG_RSVD_ARRAY_LEN];
	uint32_t num_of_free_entries;	/* For internal use */
	uint32_t free_entry_offset;	/* For internal use */
	uint32_t total_table_size;	/*  For internal use */
	uint32_t bkup_table_offset;	/*  For internal use */
} __packed;

/*
 * Entry Header:  Common to All Entry Types
 */

/*
 * Driver Code is for driver to write some info about the entry.
 * Currently not used.
 */
typedef struct qla82xx_md_entry_hdr {
	uint32_t entry_type;
	uint32_t entry_size;
	uint32_t entry_capture_size;
	struct {
		uint8_t entry_capture_mask;
		uint8_t entry_code;
		uint8_t driver_code;
		uint8_t driver_flags;
	} d_ctrl;
} __packed qla82xx_md_entry_hdr_t;

/*
 *  Read CRB entry header
 */
struct qla82xx_md_entry_crb {
	qla82xx_md_entry_hdr_t h;
	uint32_t addr;
	struct {
		uint8_t addr_stride;
		uint8_t state_index_a;
		uint16_t poll_timeout;
	} crb_strd;

	uint32_t data_size;
	uint32_t op_count;

	struct {
		uint8_t opcode;
		uint8_t state_index_v;
		uint8_t shl;
		uint8_t shr;
	} crb_ctrl;

	uint32_t value_1;
	uint32_t value_2;
	uint32_t value_3;
} __packed;

/*
 * Cache entry header
 */
struct qla82xx_md_entry_cache {
	qla82xx_md_entry_hdr_t h;

	uint32_t tag_reg_addr;
	struct {
		uint16_t tag_value_stride;
		uint16_t init_tag_value;
	} addr_ctrl;

	uint32_t data_size;
	uint32_t op_count;

	uint32_t control_addr;
	struct {
		uint16_t write_value;
		uint8_t poll_mask;
		uint8_t poll_wait;
	} cache_ctrl;

	uint32_t read_addr;
	struct {
		uint8_t read_addr_stride;
		uint8_t read_addr_cnt;
		uint16_t rsvd_1;
	} read_ctrl;
} __packed;

/*
 * Read OCM
 */
struct qla82xx_md_entry_rdocm {
	qla82xx_md_entry_hdr_t h;

	uint32_t rsvd_0;
	uint32_t rsvd_1;
	uint32_t data_size;
	uint32_t op_count;

	uint32_t rsvd_2;
	uint32_t rsvd_3;
	uint32_t read_addr;
	uint32_t read_addr_stride;
	uint32_t read_addr_cntrl;
} __packed;

/*
 * Read Memory
 */
struct qla82xx_md_entry_rdmem {
	qla82xx_md_entry_hdr_t h;
	uint32_t rsvd[6];
	uint32_t read_addr;
	uint32_t read_data_size;
} __packed;

/*
 * Read ROM
 */
struct qla82xx_md_entry_rdrom {
	qla82xx_md_entry_hdr_t h;
	uint32_t rsvd[6];
	uint32_t read_addr;
	uint32_t read_data_size;
} __packed;

struct qla82xx_md_entry_mux {
	qla82xx_md_entry_hdr_t h;

	uint32_t select_addr;
	uint32_t rsvd_0;
	uint32_t data_size;
	uint32_t op_count;

	uint32_t select_value;
	uint32_t select_value_stride;
	uint32_t read_addr;
	uint32_t rsvd_1;
} __packed;

struct qla82xx_md_entry_queue {
	qla82xx_md_entry_hdr_t h;

	uint32_t select_addr;
	struct {
		uint16_t queue_id_stride;
		uint16_t rsvd_0;
	} q_strd;

	uint32_t data_size;
	uint32_t op_count;
	uint32_t rsvd_1;
	uint32_t rsvd_2;

	uint32_t read_addr;
	struct {
		uint8_t read_addr_stride;
		uint8_t read_addr_cnt;
		uint16_t rsvd_3;
	} rd_strd;
} __packed;

#define MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE 0x129
#define RQST_TMPLT_SIZE	0x0
#define RQST_TMPLT 0x1
#define MD_DIRECT_ROM_WINDOW	0x42110030
#define MD_DIRECT_ROM_READ_BASE	0x42150000
#define MD_MIU_TEST_AGT_CTRL		0x41000090
#define MD_MIU_TEST_AGT_ADDR_LO		0x41000094
#define MD_MIU_TEST_AGT_ADDR_HI		0x41000098

static const int MD_MIU_TEST_AGT_RDDATA[] = { 0x410000A8, 0x410000AC,
	0x410000B8, 0x410000BC };

#define CRB_NIU_XG_PAUSE_CTL_P0        0x1
#define CRB_NIU_XG_PAUSE_CTL_P1        0x8

#define qla82xx_get_temp_val(x)          ((x) >> 16)
#define qla82xx_get_temp_state(x)        ((x) & 0xffff)
#define qla82xx_encode_temp(val, state)  (((val) << 16) | (state))

/*
 * Temperature control.
 */
enum {
	QLA82XX_TEMP_NORMAL = 0x1, /* Normal operating range */
	QLA82XX_TEMP_WARN,	   /* Sound alert, temperature getting high */
	QLA82XX_TEMP_PANIC	   /* Fatal error, hardware has shut down. */
};
#endif
