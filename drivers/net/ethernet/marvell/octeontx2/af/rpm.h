/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell CN10K RPM driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef RPM_H
#define RPM_H

#include <linux/bits.h>

/* PCI device IDs */
#define PCI_DEVID_CN10K_RPM		0xA060
#define PCI_SUBSYS_DEVID_CNF10KB_RPM	0xBC00
#define PCI_DEVID_CN10KB_RPM		0xA09F

/* Registers */
#define RPMX_CMRX_CFG			0x00
#define RPMX_RX_TS_PREPEND              BIT_ULL(22)
#define RPMX_TX_PTP_1S_SUPPORT          BIT_ULL(17)
#define RPMX_CMRX_RX_ID_MAP		0x80
#define RPMX_CMRX_SW_INT                0x180
#define RPMX_CMRX_SW_INT_W1S            0x188
#define RPMX_CMRX_SW_INT_ENA_W1S        0x198
#define RPMX_CMRX_LINK_CFG		0x1070
#define RPMX_MTI_PCS100X_CONTROL1       0x20000
#define RPMX_MTI_PCS_LBK                BIT_ULL(14)
#define RPMX_MTI_LPCSX_CONTROL(id)     (0x30000 | ((id) * 0x100))

#define RPMX_CMRX_LINK_RANGE_MASK	GENMASK_ULL(19, 16)
#define RPMX_CMRX_LINK_BASE_MASK	GENMASK_ULL(11, 0)
#define RPMX_MTI_MAC100X_COMMAND_CONFIG	0x8010
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_RX_P_DISABLE	BIT_ULL(29)
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_P_DISABLE	BIT_ULL(28)
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE	BIT_ULL(8)
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_PFC_MODE	BIT_ULL(19)
#define RPMX_MTI_MAC100X_CL01_PAUSE_QUANTA		0x80A8
#define RPMX_MTI_MAC100X_CL23_PAUSE_QUANTA		0x80B0
#define RPMX_MTI_MAC100X_CL45_PAUSE_QUANTA		0x80B8
#define RPMX_MTI_MAC100X_CL67_PAUSE_QUANTA		0x80C0
#define RPMX_MTI_MAC100X_CL01_QUANTA_THRESH		0x80C8
#define RPMX_MTI_MAC100X_CL23_QUANTA_THRESH		0x80D0
#define RPMX_MTI_MAC100X_CL45_QUANTA_THRESH		0x80D8
#define RPMX_MTI_MAC100X_CL67_QUANTA_THRESH		0x80E0
#define RPMX_MTI_MAC100X_CL89_PAUSE_QUANTA		0x8108
#define RPMX_MTI_MAC100X_CL1011_PAUSE_QUANTA		0x8110
#define RPMX_MTI_MAC100X_CL1213_PAUSE_QUANTA		0x8118
#define RPMX_MTI_MAC100X_CL1415_PAUSE_QUANTA		0x8120
#define RPMX_MTI_MAC100X_CL89_QUANTA_THRESH		0x8128
#define RPMX_MTI_MAC100X_CL1011_QUANTA_THRESH		0x8130
#define RPMX_MTI_MAC100X_CL1213_QUANTA_THRESH		0x8138
#define RPMX_MTI_MAC100X_CL1415_QUANTA_THRESH		0x8140
#define RPMX_CMR_RX_OVR_BP		0x4120
#define RPMX_CMR_RX_OVR_BP_EN(x)	BIT_ULL((x) + 8)
#define RPMX_CMR_RX_OVR_BP_BP(x)	BIT_ULL((x) + 4)
#define RPMX_CMR_CHAN_MSK_OR            0x4118
#define RPMX_MTI_STAT_RX_STAT_PAGES_COUNTERX 0x12000
#define RPMX_MTI_STAT_TX_STAT_PAGES_COUNTERX 0x13000
#define RPMX_MTI_STAT_DATA_HI_CDC            0x10038

#define RPM_LMAC_FWI			0xa
#define RPM_TX_EN			BIT_ULL(0)
#define RPM_RX_EN			BIT_ULL(1)
#define RPMX_CMRX_PRT_CBFC_CTL                         0x5B08
#define RPMX_CMRX_PRT_CBFC_CTL_LOGL_EN_RX_SHIFT        33
#define RPMX_CMRX_PRT_CBFC_CTL_PHYS_BP_SHIFT           16
#define RPMX_CMRX_PRT_CBFC_CTL_LOGL_EN_TX_SHIFT        0
#define RPM_PFC_CLASS_MASK			       GENMASK_ULL(48, 33)
#define RPMX_MTI_MAC100X_CL89_QUANTA_THRESH		0x8128
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_TX_PAD_EN              BIT_ULL(11)
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_IGNORE           BIT_ULL(8)
#define RPMX_MTI_MAC100X_COMMAND_CONFIG_PAUSE_FWD              BIT_ULL(7)
#define RPMX_MTI_MAC100X_CL01_PAUSE_QUANTA              0x80A8
#define RPMX_MTI_MAC100X_CL89_PAUSE_QUANTA		0x8108
#define RPM_DEFAULT_PAUSE_TIME                          0x7FF
#define RPMX_CMRX_RX_LOGL_XON				0x4100

#define RPMX_MTI_MAC100X_XIF_MODE		        0x8100
#define RPMX_ONESTEP_ENABLE				BIT_ULL(5)
#define RPMX_TS_BINARY_MODE				BIT_ULL(11)
#define RPMX_CONST1					0x2008

/* FEC stats */
#define RPMX_MTI_STAT_STATN_CONTROL			0x10018
#define RPMX_MTI_STAT_DATA_HI_CDC			0x10038
#define RPMX_RSFEC_RX_CAPTURE				BIT_ULL(27)
#define RPMX_CMD_CLEAR_RX				BIT_ULL(30)
#define RPMX_CMD_CLEAR_TX				BIT_ULL(31)
#define RPMX_MTI_RSFEC_STAT_COUNTER_CAPTURE_2		0x40050
#define RPMX_MTI_RSFEC_STAT_COUNTER_CAPTURE_3		0x40058
#define RPMX_MTI_FCFECX_VL0_CCW_LO			0x38618
#define RPMX_MTI_FCFECX_VL0_NCCW_LO			0x38620
#define RPMX_MTI_FCFECX_VL1_CCW_LO			0x38628
#define RPMX_MTI_FCFECX_VL1_NCCW_LO			0x38630
#define RPMX_MTI_FCFECX_CW_HI				0x38638

/* CN10KB CSR Declaration */
#define  RPM2_CMRX_SW_INT				0x1b0
#define  RPM2_CMRX_SW_INT_ENA_W1S			0x1c8
#define  RPM2_LMAC_FWI					0x12
#define  RPM2_CMR_CHAN_MSK_OR				0x3120
#define  RPM2_CMR_RX_OVR_BP_EN				BIT_ULL(2)
#define  RPM2_CMR_RX_OVR_BP_BP				BIT_ULL(1)
#define  RPM2_CMR_RX_OVR_BP				0x3130
#define  RPM2_CSR_OFFSET				0x3e00
#define  RPM2_CMRX_PRT_CBFC_CTL				0x6510
#define  RPM2_CMRX_RX_LMACS				0x100
#define  RPM2_CMRX_RX_LOGL_XON				0x3100
#define  RPM2_CMRX_RX_STAT2				0x3010
#define  RPM2_USX_PCSX_CONTROL1				0x80000
#define  RPM2_USX_PCS_LBK				BIT_ULL(14)

/* Function Declarations */
int rpm_get_nr_lmacs(void *rpmd);
u8 rpm_get_lmac_type(void *rpmd, int lmac_id);
u32 rpm_get_lmac_fifo_len(void *rpmd, int lmac_id);
u32 rpm2_get_lmac_fifo_len(void *rpmd, int lmac_id);
int rpm_lmac_internal_loopback(void *rpmd, int lmac_id, bool enable);
void rpm_lmac_enadis_rx_pause_fwding(void *rpmd, int lmac_id, bool enable);
int rpm_lmac_get_pause_frm_status(void *cgxd, int lmac_id, u8 *tx_pause,
				  u8 *rx_pause);
void rpm_lmac_pause_frm_config(void *rpmd, int lmac_id, bool enable);
int rpm_lmac_enadis_pause_frm(void *rpmd, int lmac_id, u8 tx_pause,
			      u8 rx_pause);
int rpm_get_tx_stats(void *rpmd, int lmac_id, int idx, u64 *tx_stat);
int rpm_get_rx_stats(void *rpmd, int lmac_id, int idx, u64 *rx_stat);
void rpm_lmac_ptp_config(void *rpmd, int lmac_id, bool enable);
int rpm_lmac_rx_tx_enable(void *rpmd, int lmac_id, bool enable);
int rpm_lmac_tx_enable(void *rpmd, int lmac_id, bool enable);
int rpm_lmac_pfc_config(void *rpmd, int lmac_id, u8 tx_pause, u8 rx_pause,
			u16 pfc_en);
int rpm_lmac_get_pfc_frm_cfg(void *rpmd, int lmac_id, u8 *tx_pause,
			     u8 *rx_pause);
int rpm2_get_nr_lmacs(void *rpmd);
bool is_dev_rpm2(void *rpmd);
int rpm_get_fec_stats(void *cgxd, int lmac_id, struct cgx_fec_stats_rsp *rsp);
int rpm_lmac_reset(void *rpmd, int lmac_id, u8 pf_req_flr);
int rpm_stats_reset(void *rpmd, int lmac_id);
#endif /* RPM_H */
