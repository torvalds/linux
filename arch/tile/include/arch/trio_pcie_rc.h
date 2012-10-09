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

#ifndef __ARCH_TRIO_PCIE_RC_H__
#define __ARCH_TRIO_PCIE_RC_H__

#include <arch/abi.h>
#include <arch/trio_pcie_rc_def.h>

#ifndef __ASSEMBLER__

/* Device Capabilities Register. */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * Max_Payload_Size Supported, writablethrough the MAC_STANDARD interface
     */
    uint_reg_t mps_sup                    : 3;
    /*
     * This field is writable through the MAC_STANDARD interface.  However,
     * Phantom Function is not  supported. Therefore, the application must
     * not write any value other than 0x0 to this  field.
     */
    uint_reg_t phantom_function_supported : 2;
    /* This bit is writable through the MAC_STANDARD interface. */
    uint_reg_t ext_tag_field_supported    : 1;
    /* Reserved. */
    uint_reg_t __reserved_0               : 3;
    /* Endpoint L1 Acceptable Latency Must be 0x0 for non-Endpoint devices. */
    uint_reg_t l1_lat                     : 3;
    /*
     * Undefined since PCI Express 1.1 (Was Attention Button Present for PCI
     * Express 1.0a)
     */
    uint_reg_t r1                         : 1;
    /*
     * Undefined since PCI Express 1.1 (Was Attention Indicator Present for
     * PCI  Express 1.0a)
     */
    uint_reg_t r2                         : 1;
    /*
     * Undefined since PCI Express 1.1 (Was Power Indicator Present for PCI
     * Express 1.0a)
     */
    uint_reg_t r3                         : 1;
    /*
     * Role-Based Error Reporting, writable through the MAC_STANDARD
     * interface.  Required to be set for device compliant to 1.1  spec and
     * later.
     */
    uint_reg_t rer                        : 1;
    /* Reserved. */
    uint_reg_t __reserved_1               : 2;
    /* Captured Slot Power Limit Value Upstream port only. */
    uint_reg_t slot_pwr_lim               : 8;
    /* Captured Slot Power Limit Scale Upstream port only. */
    uint_reg_t slot_pwr_scale             : 2;
    /* Reserved. */
    uint_reg_t __reserved_2               : 4;
    /* Endpoint L0s Acceptable LatencyMust be 0x0 for non-Endpoint devices. */
    uint_reg_t l0s_lat                    : 1;
    /* Reserved. */
    uint_reg_t __reserved_3               : 31;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_3               : 31;
    uint_reg_t l0s_lat                    : 1;
    uint_reg_t __reserved_2               : 4;
    uint_reg_t slot_pwr_scale             : 2;
    uint_reg_t slot_pwr_lim               : 8;
    uint_reg_t __reserved_1               : 2;
    uint_reg_t rer                        : 1;
    uint_reg_t r3                         : 1;
    uint_reg_t r2                         : 1;
    uint_reg_t r1                         : 1;
    uint_reg_t l1_lat                     : 3;
    uint_reg_t __reserved_0               : 3;
    uint_reg_t ext_tag_field_supported    : 1;
    uint_reg_t phantom_function_supported : 2;
    uint_reg_t mps_sup                    : 3;
#endif
  };

  uint_reg_t word;
} TRIO_PCIE_RC_DEVICE_CAP_t;

/* Device Control Register. */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Correctable Error Reporting Enable */
    uint_reg_t cor_err_ena      : 1;
    /* Non-Fatal Error Reporting Enable */
    uint_reg_t nf_err_ena       : 1;
    /* Fatal Error Reporting Enable */
    uint_reg_t fatal_err_ena    : 1;
    /* Unsupported Request Reporting Enable */
    uint_reg_t ur_ena           : 1;
    /* Relaxed orderring enable */
    uint_reg_t ro_ena           : 1;
    /* Max Payload Size */
    uint_reg_t max_payload_size : 3;
    /* Extended Tag Field Enable */
    uint_reg_t ext_tag          : 1;
    /* Phantom Function Enable */
    uint_reg_t ph_fn_ena        : 1;
    /* AUX Power PM Enable */
    uint_reg_t aux_pm_ena       : 1;
    /* Enable NoSnoop */
    uint_reg_t no_snoop         : 1;
    /* Max read request size */
    uint_reg_t max_read_req_sz  : 3;
    /* Reserved. */
    uint_reg_t __reserved       : 49;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved       : 49;
    uint_reg_t max_read_req_sz  : 3;
    uint_reg_t no_snoop         : 1;
    uint_reg_t aux_pm_ena       : 1;
    uint_reg_t ph_fn_ena        : 1;
    uint_reg_t ext_tag          : 1;
    uint_reg_t max_payload_size : 3;
    uint_reg_t ro_ena           : 1;
    uint_reg_t ur_ena           : 1;
    uint_reg_t fatal_err_ena    : 1;
    uint_reg_t nf_err_ena       : 1;
    uint_reg_t cor_err_ena      : 1;
#endif
  };

  uint_reg_t word;
} TRIO_PCIE_RC_DEVICE_CONTROL_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_TRIO_PCIE_RC_H__) */
