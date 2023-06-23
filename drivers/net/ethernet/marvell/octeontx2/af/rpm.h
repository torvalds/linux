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

/* Registers */
#define RPMX_CMRX_CFG			0x00
#define RPMX_RX_TS_PREPEND              BIT_ULL(22)
#define RPMX_CMRX_SW_INT                0x180
#define RPMX_CMRX_SW_INT_W1S            0x188
#define RPMX_CMRX_SW_INT_ENA_W1S        0x198
#define RPMX_CMRX_LINK_CFG		0x1070
#define RPMX_MTI_PCS100X_CONTROL1       0x20000
#define RPMX_MTI_LPCSX_CONTROL1         0x30000
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

/* Function Declarations */
int rpm_get_nr_lmacs(void *rpmd);
u8 rpm_get_lmac_type(void *rpmd, int lmac_id);
u32 rpm_get_lmac_fifo_len(void *rpmd, int lmac_id);
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
#endif /* RPM_H */
