/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_HIP_CHIPHELPER_H__
#define CSR_WIFI_HIP_CHIPHELPER_H__


#include "csr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The age of the BlueCore chip.  This is probably not useful, if
   you know the age then you can probably work out the version directly. */
enum chip_helper_bluecore_age
{
    chip_helper_bluecore_pre_bc7,
    chip_helper_bluecore_bc7_or_later
};

/* We support up to three windowed regions at the moment.
   Don't reorder these - they're used to index into an array. */
enum chip_helper_window_index
{
    CHIP_HELPER_WINDOW_1        = 0,
    CHIP_HELPER_WINDOW_2        = 1,
    CHIP_HELPER_WINDOW_3        = 2,
    CHIP_HELPER_WINDOW_COUNT    = 3
};

/* These are the things that we can access through a window.
   Don't reorder these - they're used to index into an array. */
enum chip_helper_window_type
{
    CHIP_HELPER_WT_CODE_RAM = 0,
    CHIP_HELPER_WT_FLASH    = 1,
    CHIP_HELPER_WT_EXT_SRAM = 2,
    CHIP_HELPER_WT_ROM      = 3,
    CHIP_HELPER_WT_SHARED   = 4,
    CHIP_HELPER_WT_COUNT    = 5
};

/* Commands to stop and start the XAP */
enum chip_helper_dbg_emu_cmd_enum
{
    CHIP_HELPER_DBG_EMU_CMD_XAP_STEP_MASK   = 0x0001,
    CHIP_HELPER_DBG_EMU_CMD_XAP_RUN_B_MASK  = 0x0002,
    CHIP_HELPER_DBG_EMU_CMD_XAP_BRK_MASK    = 0x0004,
    CHIP_HELPER_DBG_EMU_CMD_XAP_WAKEUP_MASK = 0x0008
};

/* Bitmasks for Stop and sleep status: DBG_SPI_STOP_STATUS & DBG_HOST_STOP_STATUS */
enum chip_helper_dbg_stop_status_enum
{
    CHIP_HELPER_DBG_STOP_STATUS_NONE_MASK               = 0x0000,
    CHIP_HELPER_DBG_STOP_STATUS_P0_MASK                 = 0x0001,
    CHIP_HELPER_DBG_STOP_STATUS_P1_MASK                 = 0x0002,
    CHIP_HELPER_DBG_STOP_STATUS_P2_MASK                 = 0x0004,
    CHIP_HELPER_DBG_STOP_STATUS_SLEEP_STATUS_P0_MASK    = 0x0008,
    CHIP_HELPER_DBG_STOP_STATUS_SLEEP_STATUS_P1_MASK    = 0x0010,
    CHIP_HELPER_DBG_STOP_STATUS_SLEEP_STATUS_P2_MASK    = 0x0020,
    /* Legacy names/alias */
    CHIP_HELPER_DBG_STOP_STATUS_MAC_MASK                = 0x0001,
    CHIP_HELPER_DBG_STOP_STATUS_PHY_MASK                = 0x0002,
    CHIP_HELPER_DBG_STOP_STATUS_BT_MASK                 = 0x0004,
    CHIP_HELPER_DBG_STOP_STATUS_SLEEP_STATUS_MAC_MASK   = 0x0008,
    CHIP_HELPER_DBG_STOP_STATUS_SLEEP_STATUS_PHY_MASK   = 0x0010,
    CHIP_HELPER_DBG_STOP_STATUS_SLEEP_STATUS_BT_MASK    = 0x0020
};

/* Codes to disable the watchdog */
enum chip_helper_watchdog_disable_enum
{
    CHIP_HELPER_WATCHDOG_DISABLE_CODE1 = 0x6734,
    CHIP_HELPER_WATCHDOG_DISABLE_CODE2 = 0xD6BF,
    CHIP_HELPER_WATCHDOG_DISABLE_CODE3 = 0xC31E
};

/* Other bits have changed between versions */
enum chip_helper_gbl_misc_enum
{
    CHIP_HELPER_GBL_MISC_SPI_STOP_OUT_EN_MASK  = 0x0001,
    CHIP_HELPER_GBL_MISC_MMU_INIT_DONE_MASK    = 0x0004
};

/* Coex status register, contains interrupt status and reset pullup status.
 * CHIP_HELPER_COEX_STATUS_RST_PULLS_MSB_MASK can be used to check
 * for WAPI on R03 chips and later. */
enum chip_helper_coex_status_mask_enum
{
    CHIP_HELPER_COEX_STATUS_RST_PULLS_LSB_MASK   = 0x0001,
    CHIP_HELPER_COEX_STATUS_RST_PULLS_MSB_MASK   = 0x0008,
    CHIP_HELPER_COEX_STATUS_WL_FEC_PINS_LSB_MASK = 0x0010,
    CHIP_HELPER_COEX_STATUS_WL_FEC_PINS_MSB_MASK = 0x0080,
    CHIP_HELPER_COEX_STATUS_INT_UART_MASK        = 0x0100,
    CHIP_HELPER_COEX_STATUS_INT_BT_LEG_MASK      = 0x0200
};

/* How to select the different CPUs */
enum chip_helper_dbg_proc_sel_enum
{
    CHIP_HELPER_DBG_PROC_SEL_MAC  = 0,
    CHIP_HELPER_DBG_PROC_SEL_PHY  = 1,
    CHIP_HELPER_DBG_PROC_SEL_BT   = 2,
    CHIP_HELPER_DBG_PROC_SEL_NONE = 2,
    CHIP_HELPER_DBG_PROC_SEL_BOTH = 3
};

/* These are the only registers that we have to know the
   address of before we know the chip version. */
enum chip_helper_fixed_registers
{
    /* This is the address of GBL_CHIP_VERISON on BC7,
       UF105x, UF60xx and
       anything later than that. */
    CHIP_HELPER_UNIFI_GBL_CHIP_VERSION  = 0xFE81,

    CHIP_HELPER_OLD_BLUECORE_GBL_CHIP_VERSION = 0xFF9A

                                                /* This isn't used at the moment (but might be needed
                                                to distinguish the BlueCore sub version?) */
                                                /* CHIP_HELPER_OLD_BLUECORE_ANA_VERSION_ID = 0xFF7D */
};

/* Address-value pairs for defining initialisation values */
struct chip_helper_init_values
{
    u16 addr;
    u16 value;
};

/* A block of data that should be written to the device */
struct chip_helper_reset_values
{
    u32        gp_address;
    u32        len;
    const u16 *data;
};

/*
 * This is the C API.
 */

/* opaque type */
typedef const struct chip_device_desc_t ChipDescript;

/* Return a NULL descriptor */
ChipDescript* ChipHelper_Null(void);

/* This should get the correct version for any CSR chip.
   The two parameters are what is read from addresses
   0xFF9A and 0xFE81 (OLD_BLUECORE_GBL_CHIP_VERSION and
   UNIFI_GBL_CHIP_VERSION).  These should give a unique identity
   for most (all?) chips.

   FF9A is the old GBL_CHIP_VERSION register.  If the high
   eight bits are zero then the chip is a new (BC7 +) one
   and FE81 is the _new_ GBL_CHIP_VERSION register. */
ChipDescript* ChipHelper_GetVersionAny(u16 from_FF9A, u16 from_FE81);

/* The chip is a UniFi, but we don't know which type
   The parameter is the value of UNIFI_GBL_CHIP_VERSION (0xFE81) */
ChipDescript* ChipHelper_GetVersionUniFi(u16 version);

/* This gets the version from the SDIO device id.  This only
   gives quite a coarse grained version, so we should update once
   we hav access to the function N registers. */
ChipDescript* ChipHelper_GetVersionSdio(u8 sdio_version);

/* The chip is some sort of BlueCore.  If "age" is "pre_bc7" then
   "version" is what was read from FF9A.  If "age" is bc7_or_later
   then "version" is read from FE81.  If we don't know if we're pre
   or post BC7 then we should use "GetVersionAny". */
ChipDescript* ChipHelper_GetVersionBlueCore(enum chip_helper_bluecore_age age,
                                            u16                     version);

/* The main functions of this class are built with an X macro.  This
   means we can generate the C and C++ versions from the same source
   without the two diverging.

   The DEF0 functions are simple and take no parameters.  The first
   parameter to the macro is the return type.  The second parameter
   is the function name and the third parameter is where to get the
   info from (this is hidden from the user).

   The DEF1 functions take one parameter. This time the third macro
   parameter is the type of this parameter, and the fourth macro
   parameter is the name of the parameter. The bodies of these
   functions are hand written. */
#define CHIP_HELPER_LIST(m)                                             \
    CHIP_HELPER_DEF0(m, (const char *, FriendlyName, friendly_name))     \
    CHIP_HELPER_DEF0(m, (const char *, MarketingName, marketing_name))  \
    CHIP_HELPER_DEF0(m, (u16, DBG_EMU_CMD, regs->dbg_emu_cmd))       \
    CHIP_HELPER_DEF0(m, (u16, DBG_HOST_PROC_SELECT, regs->host.dbg_proc_select)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_HOST_STOP_STATUS, regs->host.dbg_stop_status)) \
    CHIP_HELPER_DEF0(m, (u16, HOST_WINDOW1_PAGE, regs->host.window1_page)) \
    CHIP_HELPER_DEF0(m, (u16, HOST_WINDOW2_PAGE, regs->host.window2_page)) \
    CHIP_HELPER_DEF0(m, (u16, HOST_WINDOW3_PAGE, regs->host.window3_page)) \
    CHIP_HELPER_DEF0(m, (u16, HOST_IO_LOG_ADDR, regs->host.io_log_addr)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_SPI_PROC_SELECT, regs->spi.dbg_proc_select)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_SPI_STOP_STATUS, regs->spi.dbg_stop_status)) \
    CHIP_HELPER_DEF0(m, (u16, SPI_WINDOW1_PAGE, regs->spi.window1_page)) \
    CHIP_HELPER_DEF0(m, (u16, SPI_WINDOW2_PAGE, regs->spi.window2_page)) \
    CHIP_HELPER_DEF0(m, (u16, SPI_WINDOW3_PAGE, regs->spi.window3_page)) \
    CHIP_HELPER_DEF0(m, (u16, SPI_IO_LOG_ADDR, regs->spi.io_log_addr)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_RESET, regs->dbg_reset))           \
    CHIP_HELPER_DEF0(m, (u16, DBG_RESET_VALUE, regs->dbg_reset_value)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_RESET_WARN, regs->dbg_reset_warn)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_RESET_WARN_VALUE, regs->dbg_reset_warn_value)) \
    CHIP_HELPER_DEF0(m, (u16, DBG_RESET_RESULT, regs->dbg_reset_result)) \
    CHIP_HELPER_DEF0(m, (u16, WATCHDOG_DISABLE, regs->watchdog_disable)) \
    CHIP_HELPER_DEF0(m, (u16, PROC_PC_SNOOP, regs->proc_pc_snoop))   \
    CHIP_HELPER_DEF0(m, (u16, GBL_CHIP_VERSION, regs->gbl_chip_version)) \
    CHIP_HELPER_DEF0(m, (u16, GBL_MISC_ENABLES, regs->gbl_misc_enables)) \
    CHIP_HELPER_DEF0(m, (u16, XAP_PCH, regs->xap_pch))               \
    CHIP_HELPER_DEF0(m, (u16, XAP_PCL, regs->xap_pcl))               \
    CHIP_HELPER_DEF0(m, (u16, MAILBOX0, regs->mailbox0))             \
    CHIP_HELPER_DEF0(m, (u16, MAILBOX1, regs->mailbox1))             \
    CHIP_HELPER_DEF0(m, (u16, MAILBOX2, regs->mailbox2))             \
    CHIP_HELPER_DEF0(m, (u16, MAILBOX3, regs->mailbox3))             \
    CHIP_HELPER_DEF0(m, (u16, SDIO_HIP_HANDSHAKE, regs->sdio_hip_handshake))   \
    CHIP_HELPER_DEF0(m, (u16, SDIO_HOST_INT, regs->sdio_host_int))   \
    CHIP_HELPER_DEF0(m, (u16, COEX_STATUS, regs->coex_status))       \
    CHIP_HELPER_DEF0(m, (u16, SHARED_IO_INTERRUPT, regs->shared_io_interrupt)) \
    CHIP_HELPER_DEF0(m, (u32, PROGRAM_MEMORY_RAM_OFFSET, prog_offset.ram)) \
    CHIP_HELPER_DEF0(m, (u32, PROGRAM_MEMORY_ROM_OFFSET, prog_offset.rom)) \
    CHIP_HELPER_DEF0(m, (u32, PROGRAM_MEMORY_FLASH_OFFSET, prog_offset.flash)) \
    CHIP_HELPER_DEF0(m, (u32, PROGRAM_MEMORY_EXT_SRAM_OFFSET, prog_offset.ext_sram)) \
    CHIP_HELPER_DEF0(m, (u16, DATA_MEMORY_RAM_OFFSET, data_offset.ram)) \
    CHIP_HELPER_DEF0(m, (s32, HasFlash, bools.has_flash))              \
    CHIP_HELPER_DEF0(m, (s32, HasExtSram, bools.has_ext_sram))         \
    CHIP_HELPER_DEF0(m, (s32, HasRom, bools.has_rom))                  \
    CHIP_HELPER_DEF0(m, (s32, HasBt, bools.has_bt))                    \
    CHIP_HELPER_DEF0(m, (s32, HasWLan, bools.has_wlan))                \
    CHIP_HELPER_DEF1(m, (u16, WINDOW_ADDRESS, enum chip_helper_window_index, window)) \
    CHIP_HELPER_DEF1(m, (u16, WINDOW_SIZE, enum chip_helper_window_index, window)) \
    CHIP_HELPER_DEF1(m, (u16, MapAddress_SPI2HOST, u16, addr))          \
    CHIP_HELPER_DEF1(m, (u16, MapAddress_HOST2SPI, u16, addr))          \
    CHIP_HELPER_DEF1(m, (u32, ClockStartupSequence, const struct chip_helper_init_values **, val)) \
    CHIP_HELPER_DEF1(m, (u32, HostResetSequence, const struct chip_helper_reset_values **, val))

/* Some magic to help the expansion */
#define CHIP_HELPER_DEF0(a, b) \
    CHIP_HELPER_DEF0_ ## a b
#define CHIP_HELPER_DEF1(a, b) \
    CHIP_HELPER_DEF1_ ## a b

/* Macros so that when we expand the list we get "C" function prototypes. */
#define CHIP_HELPER_DEF0_C_DEC(ret_type, name, info)    \
    ret_type ChipHelper_ ## name(ChipDescript * chip_help);
#define CHIP_HELPER_DEF1_C_DEC(ret_type, name, type1, name1)   \
    ret_type ChipHelper_ ## name(ChipDescript * chip_help, type1 name1);

CHIP_HELPER_LIST(C_DEC)

/* FriendlyName
   MarketingName

   These two functions return human readable strings that describe
   the chip.  FriendlyName returns something that a software engineer
   at CSR might understand.  MarketingName returns something more like
   an external name for a CSR chip.
*/
/* DBG_EMU_CMD
   WATCHDOG_DISABLE
   PROC_PC_SNOOP
   GBL_CHIP_VERSION
   XAP_PCH
   XAP_PCL

   These registers are used to control the XAPs.
*/
/* DBG_HOST_PROC_SELECT  DBG_HOST_STOP_STATUS
   HOST_WINDOW1_PAGE HOST_WINDOW2_PAGE HOST_WINDOW3_PAGE
   HOST_IO_LOG_ADDR
   DBG_SPI_PROC_SELECT  DBG_SPI_STOP_STATUS
   SPI_WINDOW1_PAGE SPI_WINDOW2_PAGE SPI_WINDOW3_PAGE
   SPI_IO_LOG_ADDR

   These register are used to control the XAPs and the memory
   windows, normally while debugging the code on chip.  There
   are two versons of these registers, one for access via SPI
   and another for access via the host interface.
*/
/*  DBG_RESET
    DBG_RESET_VALUE
    DBG_RESET_WARN
    DBG_RESET_WARN_VALUE
    DBG_RESET_RESULT

    These registers are used to reset the XAP.  This can be
    quite complex for some chips.  If DBG_RESET_WARN is non
    zero the DBG_RESET_WARN_VALUE should be written to address
    DBG_RESET_WARN before the reset is perfeormed.  DBG_RESET_VALUE
    should then be written to DBG_RESET to make the reset happen.
    The DBG_RESET_RESULT register should contain 0 if the reset
    was successful.
*/
/*  GBL_MISC_ENABLES

    This register controls some special chip features.  It
    should be used with care is it changes quite a lot between
    chip versions.
*/
/*  MAILBOX0
    MAILBOX1
    MAILBOX2
    MAILBOX3

    The mailbox registers are for communication between the host
    and the firmware.  There use is described in part by the host
    interface protcol specifcation.
*/
/*  SDIO_HIP_HANDSHAKE

    This is one of the more important SDIO HIP registers.  On some
    chips it has the same value as one of the mailbox registers
    and on other chips it is different.
*/
/*  SDIO_HOST_INT
    SHARED_IO_INTERRUPT

    These registers are used by some versions of the host interface
    protocol specification.  Their names should probably be changed
    to hide the registers and to expose the functions more.
*/
/*  COEX_STATUS

    Coex status register, contains interrupt status and reset
    pullup status.  The latter is used to detect WAPI.
*/
/*  PROGRAM_MEMORY_RAM_OFFSET
    PROGRAM_MEMORY_ROM_OFFSET
    PROGRAM_MEMORY_FLASH_OFFSET
    PROGRAM_MEMORY_EXT_SRAM_OFFSET
    DATA_MEMORY_RAM_OFFSET

    These are constants that describe the offset of the different
    memory types in the two different address spaces.
*/
/*  HasFlash HasExtSram HasRom
    HasBt HasWLan

    These are a set of bools describing the chip.
*/
/*  WINDOW_ADDRESS WINDOW_SIZE

    These two functions return the size and address of the windows.
    The address is the address of the lowest value in the address
    map that is part of the window and the size is the number of
    visible words.

    Some of the windows have thier lowest portion covered by
    registers.  For these windows address is the first address
    after the registers and size is the siave excluding the part
    covered by registers.
*/
/*  MapAddress_SPI2HOST
    MapAddress_HOST2SPI

    The debugging interface is duplicated on UniFi and later chips
    so that there are two versions - one over the SPI interaface and
    the other over the SDIO interface.  These functions map the
    registers between these two interfaces.
*/
/*  ClockStartupSequence

    This function returns the list of register value pairs that
    should be forced into UniFi to enable SPI communication.  This
    set of registers is not needed if the firmware is running, but
    will be needed if the device is being booted from cold.  These
    register writes enable the clocks and setup the PLL to a basic
    working state.  SPI access might be unreliable until these writes
    have occured (And they may take mulitple goes).
*/
/*  HostResetSequence

    This returns a number of chunks of data and generic pointers.
    All of the XAPs should be stopped.  The data should be written
    to the generic pointers.  The instruction pointer for the MAC
    should then be set to the start of program memory and then the
    MAC should be "go"d.  This will reset the chip in a reliable
    and orderly manner without resetting the SDIO interface.  It
    is therefore not needed if the chip is being accessed by the
    SPI interface (the DBG_RESET_ mechanism can be used instead).
*/

/* The Decode Window function is more complex.  For the window
   'window' it tries to return the address and page register
   value needed to see offset 'offset' of memory type 'type'.

   It return 1 on success and 0 on failure.  'page' is what
   should be written to the page register.  'addr' is the
   address in the XAPs 16 address map to read from.  'len'
   is the length that we can read without having to change
   the page registers. */
s32 ChipHelper_DecodeWindow(ChipDescript *chip_help,
                                 enum chip_helper_window_index window,
                                 enum chip_helper_window_type type,
                                 u32 offset,
                                 u16 *page, u16 *addr, u32 *len);

#ifdef __cplusplus
/* Close the extern "C" */
}

/*
 * This is the C++ API.
 */

class ChipHelper
{
public:
    /* If this constructor is used then a GetVersionXXX function
       should be called next. */
    ChipHelper();

    /* copy constructor */
    ChipHelper(ChipDescript * desc);

    /* The default constructor assume a BC7 / UF105x series chip
       and that the number given is the value of UNIFI_GBL_CHIP_VERSION
       (0xFE81) */
    ChipHelper(u16 version);

    /* This returns the C interface magic token from a C++ instance. */
    ChipDescript* GetDescript() const
    {
        return m_desc;
    }


    /* Clear out theis class (set it to the null token). */
    void ClearVersion();

    /* Load this class with data for a specific chip. */
    void GetVersionAny(u16 from_FF9A, u16 from_FE81);
    void GetVersionUniFi(u16 version);
    void GetVersionBlueCore(chip_helper_bluecore_age age, u16 version);
    void GetVersionSdio(u8 sdio_version);

    /* Helpers to build the definitions of the member functions. */
#define CHIP_HELPER_DEF0_CPP_DEC(ret_type, name, info)    \
    ret_type name() const;
#define CHIP_HELPER_DEF1_CPP_DEC(ret_type, name, type1, name1)   \
    ret_type name(type1 name1) const;

    CHIP_HELPER_LIST(CPP_DEC)


    /* The DecodeWindow function, see the description of the C version. */
    s32 DecodeWindow(chip_helper_window_index window,
                          chip_helper_window_type type,
                          u32 offset,
                          u16 &page, u16 &addr, u32 &len) const;

private:
    ChipDescript *m_desc;
};

#endif /* __cplusplus */

#endif
