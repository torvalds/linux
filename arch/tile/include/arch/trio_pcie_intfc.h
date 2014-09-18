/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/* Machine-generated file; do not edit. */

#ifndef __ARCH_TRIO_PCIE_INTFC_H__
#define __ARCH_TRIO_PCIE_INTFC_H__

#include <arch/abi.h>
#include <arch/trio_pcie_intfc_def.h>

#ifndef __ASSEMBLER__

/*
 * Port Configuration.
 * Configuration of the PCIe Port
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Provides the state of the strapping pins for this port. */
    uint_reg_t strap_state      : 3;
    /* Reserved. */
    uint_reg_t __reserved_0     : 1;
    /*
     * When 1, the device type will be overridden using OVD_DEV_TYPE_VAL.
     * When 0, the device type is determined based on the STRAP_STATE.
     */
    uint_reg_t ovd_dev_type     : 1;
    /* Provides the device type when OVD_DEV_TYPE is 1. */
    uint_reg_t ovd_dev_type_val : 4;
    /* Determines how link is trained. */
    uint_reg_t train_mode       : 2;
    /* Reserved. */
    uint_reg_t __reserved_1     : 1;
    /*
     * For PCIe, used to flip physical RX lanes that were not properly wired.
     *  This is not the same as lane reversal which is handled automatically
     * during link training.  When 0, RX Lane0 must be wired to the link
     * partner (either to its Lane0 or it's LaneN).  When RX_LANE_FLIP is 1,
     * the highest numbered lane for this port becomes Lane0 and Lane0 does
     * NOT have to be wired to the link partner.
     */
    uint_reg_t rx_lane_flip     : 1;
    /*
     * For PCIe, used to flip physical TX lanes that were not properly wired.
     *  This is not the same as lane reversal which is handled automatically
     * during link training.  When 0, TX Lane0 must be wired to the link
     * partner (either to its Lane0 or it's LaneN).  When TX_LANE_FLIP is 1,
     * the highest numbered lane for this port becomes Lane0 and Lane0 does
     * NOT have to be wired to the link partner.
     */
    uint_reg_t tx_lane_flip     : 1;
    /*
     * For StreamIO port, configures the width of the port when TRAIN_MODE is
     * not STRAP.
     */
    uint_reg_t stream_width     : 2;
    /*
     * For StreamIO port, configures the rate of the port when TRAIN_MODE is
     * not STRAP.
     */
    uint_reg_t stream_rate      : 2;
    /* Reserved. */
    uint_reg_t __reserved_2     : 46;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_2     : 46;
    uint_reg_t stream_rate      : 2;
    uint_reg_t stream_width     : 2;
    uint_reg_t tx_lane_flip     : 1;
    uint_reg_t rx_lane_flip     : 1;
    uint_reg_t __reserved_1     : 1;
    uint_reg_t train_mode       : 2;
    uint_reg_t ovd_dev_type_val : 4;
    uint_reg_t ovd_dev_type     : 1;
    uint_reg_t __reserved_0     : 1;
    uint_reg_t strap_state      : 3;
#endif
  };

  uint_reg_t word;
} TRIO_PCIE_INTFC_PORT_CONFIG_t;

/*
 * Port Status.
 * Status of the PCIe Port.  This register applies to the StreamIO port when
 * StreamIO is enabled.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * Indicates the DL state of the port.  When 1, the port is up and ready
     * to receive traffic.
     */
    uint_reg_t dl_up        : 1;
    /*
     * Indicates the number of times the link has gone down.  Clears on read.
     */
    uint_reg_t dl_down_cnt  : 7;
    /* Indicates the SERDES PLL has spun up and is providing a valid clock. */
    uint_reg_t clock_ready  : 1;
    /* Reserved. */
    uint_reg_t __reserved_0 : 7;
    /* Device revision ID. */
    uint_reg_t device_rev   : 8;
    /* Link state (PCIe). */
    uint_reg_t ltssm_state  : 6;
    /* Link power management state (PCIe). */
    uint_reg_t pm_state     : 3;
    /* Reserved. */
    uint_reg_t __reserved_1 : 31;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_1 : 31;
    uint_reg_t pm_state     : 3;
    uint_reg_t ltssm_state  : 6;
    uint_reg_t device_rev   : 8;
    uint_reg_t __reserved_0 : 7;
    uint_reg_t clock_ready  : 1;
    uint_reg_t dl_down_cnt  : 7;
    uint_reg_t dl_up        : 1;
#endif
  };

  uint_reg_t word;
} TRIO_PCIE_INTFC_PORT_STATUS_t;

/*
 * Transmit FIFO Control.
 * Contains TX FIFO thresholds.  These registers are for diagnostics purposes
 * only.  Changing these values causes undefined behavior.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * Almost-Empty level for TX0 data.  Typically set to at least
     * roundup(38.0*M/N) where N=tclk frequency and M=MAC symbol rate in MHz
     * for a x4 port (250MHz).
     */
    uint_reg_t tx0_data_ae_lvl : 7;
    /* Reserved. */
    uint_reg_t __reserved_0    : 1;
    /* Almost-Empty level for TX1 data. */
    uint_reg_t tx1_data_ae_lvl : 7;
    /* Reserved. */
    uint_reg_t __reserved_1    : 1;
    /* Almost-Full level for TX0 data. */
    uint_reg_t tx0_data_af_lvl : 7;
    /* Reserved. */
    uint_reg_t __reserved_2    : 1;
    /* Almost-Full level for TX1 data. */
    uint_reg_t tx1_data_af_lvl : 7;
    /* Reserved. */
    uint_reg_t __reserved_3    : 1;
    /* Almost-Full level for TX0 info. */
    uint_reg_t tx0_info_af_lvl : 5;
    /* Reserved. */
    uint_reg_t __reserved_4    : 3;
    /* Almost-Full level for TX1 info. */
    uint_reg_t tx1_info_af_lvl : 5;
    /* Reserved. */
    uint_reg_t __reserved_5    : 3;
    /*
     * This register provides performance adjustment for high bandwidth
     * flows.  The MAC will assert almost-full to TRIO if non-posted credits
     * fall below this level.  Note that setting this larger than the initial
     * PORT_CREDIT.NPH value will cause READS to never be sent.  If the
     * initial credit value from the link partner is smaller than this value
     * when the link comes up, the value will be reset to the initial credit
     * value to prevent lockup.
     */
    uint_reg_t min_np_credits  : 8;
    /*
     * This register provides performance adjustment for high bandwidth
     * flows.  The MAC will assert almost-full to TRIO if posted credits fall
     * below this level.  Note that setting this larger than the initial
     * PORT_CREDIT.PH value will cause WRITES to never be sent.  If the
     * initial credit value from the link partner is smaller than this value
     * when the link comes up, the value will be reset to the initial credit
     * value to prevent lockup.
     */
    uint_reg_t min_p_credits   : 8;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t min_p_credits   : 8;
    uint_reg_t min_np_credits  : 8;
    uint_reg_t __reserved_5    : 3;
    uint_reg_t tx1_info_af_lvl : 5;
    uint_reg_t __reserved_4    : 3;
    uint_reg_t tx0_info_af_lvl : 5;
    uint_reg_t __reserved_3    : 1;
    uint_reg_t tx1_data_af_lvl : 7;
    uint_reg_t __reserved_2    : 1;
    uint_reg_t tx0_data_af_lvl : 7;
    uint_reg_t __reserved_1    : 1;
    uint_reg_t tx1_data_ae_lvl : 7;
    uint_reg_t __reserved_0    : 1;
    uint_reg_t tx0_data_ae_lvl : 7;
#endif
  };

  uint_reg_t word;
} TRIO_PCIE_INTFC_TX_FIFO_CTL_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_TRIO_PCIE_INTFC_H__) */
