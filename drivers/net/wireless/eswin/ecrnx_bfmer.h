/**
 ******************************************************************************
 *
 * @file ecrnx_bfmer.h
 *
 * @brief VHT Beamformer function declarations
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_BFMER_H_
#define _ECRNX_BFMER_H_

/**
 * INCLUDE FILES
 ******************************************************************************
 */

#include "ecrnx_defs.h"

/**
 * DEFINES
 ******************************************************************************
 */

/// Maximal supported report length (in bytes)
#define ECRNX_BFMER_REPORT_MAX_LEN     2048

/// Size of the allocated report space (twice the maximum report length)
#define ECRNX_BFMER_REPORT_SPACE_SIZE  (ECRNX_BFMER_REPORT_MAX_LEN * 2)

/**
 * TYPE DEFINITIONS
 ******************************************************************************
 */

/*
 * Structure used to store a beamforming report.
 */
struct ecrnx_bfmer_report {
    dma_addr_t dma_addr;    /* Virtual address provided to MAC for
                               DMA transfer of the Beamforming Report */
    unsigned int length;    /* Report Length */
    u8 report[1];           /* Report to be used for VHT TX Beamforming */
};

/**
 * FUNCTION DECLARATIONS
 ******************************************************************************
 */

/**
 ******************************************************************************
 * @brief Allocate memory aiming to contains the Beamforming Report received
 * from a Beamformee capable capable.
 * The providing length shall be large enough to contain the VHT Compressed
 * Beaforming Report and the MU Exclusive part.
 * It also perform a DMA Mapping providing an address to be provided to the HW
 * responsible for the DMA transfer of the report.
 * If successful a struct ecrnx_bfmer_report object is allocated, it's address
 * is stored in ecrnx_sta->bfm_report.
 *
 * @param[in] ecrnx_hw   PHY Information
 * @param[in] ecrnx_sta  Peer STA Information
 * @param[in] length    Memory size to be allocated
 *
 * @return 0 if operation is successful, else -1.
 ******************************************************************************
 */
int ecrnx_bfmer_report_add(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *ecrnx_sta,
                          unsigned int length);

/**
 ******************************************************************************
 * @brief Free a previously allocated memory intended to be used for
 * Beamforming Reports.
 *
 * @param[in] ecrnx_hw   PHY Information
 * @param[in] ecrnx_sta  Peer STA Information
 *
 ******************************************************************************
 */
void ecrnx_bfmer_report_del(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *ecrnx_sta);

#ifdef CONFIG_ECRNX_FULLMAC
/**
 ******************************************************************************
 * @brief Parse a Rx VHT-MCS map in order to deduce the maximum number of
 * Spatial Streams supported by a beamformee.
 *
 * @param[in] vht_capa  Received VHT Capability field.
 *
 ******************************************************************************
 */
u8 ecrnx_bfmer_get_rx_nss(const struct ieee80211_vht_cap *vht_capa);
#endif /* CONFIG_ECRNX_FULLMAC */

#endif /* _ECRNX_BFMER_H_ */
