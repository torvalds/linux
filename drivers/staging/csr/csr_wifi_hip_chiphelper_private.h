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
    u32                             len;
    const struct chip_helper_init_values *vals;
};

/* Just a (counted) u16 array */
struct data_array_t
{
    u32        len;
    const u16 *vals;
};

struct reset_prog_t
{
    u32                              len;
    const struct chip_helper_reset_values *vals;
};

/* The addresses of registers that are equivalent but on
   different host transports. */
struct chip_map_address_t
{
    u16 spi, host;
};

struct map_array_t
{
    u32                        len;
    const struct chip_map_address_t *vals;
};

struct chip_device_regs_per_transport_t
{
    u16 dbg_proc_select;
    u16 dbg_stop_status;
    u16 window1_page;    /* PROG_PMEM1 or GW1 */
    u16 window2_page;    /* PROG_PMEM2 or GW2 */
    u16 window3_page;    /* SHARED or GW3 */
    u16 io_log_addr;
};

struct chip_device_regs_t
{
    u16                               gbl_chip_version;
    u16                               gbl_misc_enables;
    u16                               dbg_emu_cmd;
    struct chip_device_regs_per_transport_t host;
    struct chip_device_regs_per_transport_t spi;
    u16                               dbg_reset;
    u16                               dbg_reset_value;
    u16                               dbg_reset_warn;
    u16                               dbg_reset_warn_value;
    u16                               dbg_reset_result;
    u16                               xap_pch;
    u16                               xap_pcl;
    u16                               proc_pc_snoop;
    u16                               watchdog_disable;
    u16                               mailbox0;
    u16                               mailbox1;
    u16                               mailbox2;
    u16                               mailbox3;
    u16                               sdio_host_int;
    u16                               shared_io_interrupt;
    u16                               sdio_hip_handshake;
    u16                               coex_status; /* Allows WAPI detection */
};

/* If allowed is false then this window does not provide this
   type of access.
   This describes how addresses should be shifted to make the
   "page" address.  The address is shifted left by 'page_shift'
   and then has 'page_offset' added.  This value should then be
   written to the page register. */
struct window_shift_info_t
{
    s32  allowed;
    u32 page_shift;
    u16 page_offset;
};

/* Each window has an address and size.  These are obvious.  It then
   has a description for each type of memory that might be accessed
   through it.  There might also be a start to the offset of the window.
   This means that that number of addresses at the start of the window
   are unusable. */
struct window_info_t
{
    u16                         address;
    u16                         size;
    u16                         blocked;
    const struct window_shift_info_t *mode;
};

/* If GBL_CHIP_VERSION and'ed with 'mask' and is equal to 'result'
   then this is the correct set of info.  If pre_bc7 is true then the
   address of GBL_CHIP_VERSION is FF9A, else its FE81. */
struct chip_version_t
{
    s32  pre_bc7;
    u16 mask;
    u16 result;
    u8  sdio;
};

struct chip_device_desc_t
{
    struct chip_version_t chip_version;

    /* This is a text string that a human might find useful (BC02, UF105x) */
    const char *friendly_name;
    /* This is what we show to customers */
    const char *marketing_name;

    /* Initialisation values to write following a reset */
    struct val_array_t init;

    /* Binary sequence for hard reset */
    struct reset_prog_t reset_prog;

    /* The register map */
    const struct chip_device_regs_t *regs;

    /* Some misc. info on the chip */
    struct
    {
        u32 has_flash     : 1;
        u32 has_ext_sram  : 1;
        u32 has_rom       : 1;
        u32 has_bt        : 1;
        u32 has_wlan      : 1;
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
        u32 ram;
        u32 rom;
        u32 flash;
        u32 ext_sram;
    } prog_offset;

    /* The offsets into the data address space of interesting things. */
    struct
    {
        u16 ram;
        /* maybe add shared / page tables? */
    } data_offset;

    /* Information on the different windows */
    const struct window_info_t *windows[CHIP_HELPER_WINDOW_COUNT];
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CSR_WIFI_HIP_CHIPHELPER_PRIVATE_H__ */
