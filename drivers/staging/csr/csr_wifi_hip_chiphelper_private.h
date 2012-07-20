/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_HIP_CHIPHELPER_PRIVATE_H__
#define CSR_WIFI_HIP_CHIPHELPER_PRIVATE_H__


#include "csr_wifi_hip_chiphelper.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* This GP stuff should be somewhere else? */

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

#define MAKE_GP(R, O)  (((UNIFI_ ## R) << 24) | (O))
#define GP_OFFSET(GP)  ((GP) & 0xFFFFFF)
#define GP_SPACE(GP)   (((GP) >> 24) & 0xFF)


/* Address value pairs */
struct val_array_t
{
    CsrUint32                             len;
    const struct chip_helper_init_values *vals;
};

/* Just a (counted) CsrUint16 array */
struct data_array_t
{
    CsrUint32        len;
    const CsrUint16 *vals;
};

struct reset_prog_t
{
    CsrUint32                              len;
    const struct chip_helper_reset_values *vals;
};

/* The addresses of registers that are equivalent but on
   different host transports. */
struct chip_map_address_t
{
    CsrUint16 spi, host;
};

struct map_array_t
{
    CsrUint32                        len;
    const struct chip_map_address_t *vals;
};

struct chip_device_regs_per_transport_t
{
    CsrUint16 dbg_proc_select;
    CsrUint16 dbg_stop_status;
    CsrUint16 window1_page;    /* PROG_PMEM1 or GW1 */
    CsrUint16 window2_page;    /* PROG_PMEM2 or GW2 */
    CsrUint16 window3_page;    /* SHARED or GW3 */
    CsrUint16 io_log_addr;
};

struct chip_device_regs_t
{
    CsrUint16                               gbl_chip_version;
    CsrUint16                               gbl_misc_enables;
    CsrUint16                               dbg_emu_cmd;
    struct chip_device_regs_per_transport_t host;
    struct chip_device_regs_per_transport_t spi;
    CsrUint16                               dbg_reset;
    CsrUint16                               dbg_reset_value;
    CsrUint16                               dbg_reset_warn;
    CsrUint16                               dbg_reset_warn_value;
    CsrUint16                               dbg_reset_result;
    CsrUint16                               xap_pch;
    CsrUint16                               xap_pcl;
    CsrUint16                               proc_pc_snoop;
    CsrUint16                               watchdog_disable;
    CsrUint16                               mailbox0;
    CsrUint16                               mailbox1;
    CsrUint16                               mailbox2;
    CsrUint16                               mailbox3;
    CsrUint16                               sdio_host_int;
    CsrUint16                               shared_io_interrupt;
    CsrUint16                               sdio_hip_handshake;
    CsrUint16                               coex_status; /* Allows WAPI detection */
};

/* If allowed is false then this window does not provide this
   type of access.
   This describes how addresses should be shifted to make the
   "page" address.  The address is shifted left by 'page_shift'
   and then has 'page_offset' added.  This value should then be
   written to the page register. */
struct window_shift_info_t
{
    CsrInt32  allowed;
    CsrUint32 page_shift;
    CsrUint16 page_offset;
};

/* Each window has an address and size.  These are obvious.  It then
   has a description for each type of memory that might be accessed
   through it.  There might also be a start to the offset of the window.
   This means that that number of addresses at the start of the window
   are unusable. */
struct window_info_t
{
    CsrUint16                         address;
    CsrUint16                         size;
    CsrUint16                         blocked;
    const struct window_shift_info_t *mode;
};

/* If GBL_CHIP_VERSION and'ed with 'mask' and is equal to 'result'
   then this is the correct set of info.  If pre_bc7 is true then the
   address of GBL_CHIP_VERSION is FF9A, else its FE81. */
struct chip_version_t
{
    CsrInt32  pre_bc7;
    CsrUint16 mask;
    CsrUint16 result;
    u8  sdio;
};

struct chip_device_desc_t
{
    struct chip_version_t chip_version;

    /* This is a text string that a human might find useful (BC02, UF105x) */
    const CsrCharString *friendly_name;
    /* This is what we show to customers */
    const CsrCharString *marketing_name;

    /* Initialisation values to write following a reset */
    struct val_array_t init;

    /* Binary sequence for hard reset */
    struct reset_prog_t reset_prog;

    /* The register map */
    const struct chip_device_regs_t *regs;

    /* Some misc. info on the chip */
    struct
    {
        CsrUint32 has_flash     : 1;
        CsrUint32 has_ext_sram  : 1;
        CsrUint32 has_rom       : 1;
        CsrUint32 has_bt        : 1;
        CsrUint32 has_wlan      : 1;
    } bools;

    /* This table is used to remap register addresses depending on what
       host interface is used.  On the BC7 and later chips there are
       multiple sets of memory window registers, on for each host
       interafce (SDIO / SPI).  The correct one is needed. */
    struct map_array_t map;

    /* The offsets into the program address space of the different types of memory.
       The RAM offset is probably the most useful. */
    struct
    {
        CsrUint32 ram;
        CsrUint32 rom;
        CsrUint32 flash;
        CsrUint32 ext_sram;
    } prog_offset;

    /* The offsets into the data address space of interesting things. */
    struct
    {
        CsrUint16 ram;
        /* maybe add shared / page tables? */
    } data_offset;

    /* Information on the different windows */
    const struct window_info_t *windows[CHIP_HELPER_WINDOW_COUNT];
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CSR_WIFI_HIP_CHIPHELPER_PRIVATE_H__ */
