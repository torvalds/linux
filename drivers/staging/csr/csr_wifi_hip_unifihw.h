/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 *
 * File: csr_wifi_hip_unifihw.h
 *
 *      Definitions of various chip registers, addresses, values etc.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __UNIFIHW_H__
#define __UNIFIHW_H__ 1

/* Symbol Look Up Table fingerprint. IDs are in sigs.h */
#define SLUT_FINGERPRINT        0xD397


/* Values of LoaderOperation */
#define UNIFI_LOADER_IDLE       0x00
#define UNIFI_LOADER_COPY       0x01
#define UNIFI_LOADER_ERROR_MASK 0xF0

/* Values of BootLoaderOperation */
#define UNIFI_BOOT_LOADER_IDLE       0x00
#define UNIFI_BOOT_LOADER_RESTART    0x01
#define UNIFI_BOOT_LOADER_PATCH      0x02
#define UNIFI_BOOT_LOADER_LOAD_STA   0x10
#define UNIFI_BOOT_LOADER_LOAD_PTEST 0x11


/* Memory spaces encoded in top byte of Generic Pointer type */
#define UNIFI_SH_DMEM   0x01    /* Shared Data Memory */
#define UNIFI_EXT_FLASH 0x02    /* External FLASH */
#define UNIFI_EXT_SRAM  0x03    /* External SRAM */
#define UNIFI_REGISTERS 0x04    /* Registers */
#define UNIFI_PHY_DMEM  0x10    /* PHY Data Memory */
#define UNIFI_PHY_PMEM  0x11    /* PHY Program Memory */
#define UNIFI_PHY_ROM   0x12    /* PHY ROM */
#define UNIFI_MAC_DMEM  0x20    /* MAC Data Memory */
#define UNIFI_MAC_PMEM  0x21    /* MAC Program Memory */
#define UNIFI_MAC_ROM   0x22    /* MAC ROM */
#define UNIFI_BT_DMEM   0x30    /* BT Data Memory */
#define UNIFI_BT_PMEM   0x31    /* BT Program Memory */
#define UNIFI_BT_ROM    0x32    /* BT ROM */

#define UNIFI_MAKE_GP(R, O)  (((UNIFI_ ## R) << 24) | (O))
#define UNIFI_GP_OFFSET(GP)  ((GP) & 0xFFFFFF)
#define UNIFI_GP_SPACE(GP)   (((GP) >> 24) & 0xFF)

#endif /* __UNIFIHW_H__ */
