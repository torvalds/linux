/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_macro.h"
#include "csr_wifi_hip_chiphelper_private.h"

#ifndef nelem
#define nelem(a) (sizeof(a) / sizeof(a[0]))
#endif

#define counted(foo) { nelem(foo), foo }
#define null_counted()  { 0, NULL }

/* The init values are a set of register writes that we must
   perform when we first connect to the chip to get it working.
   They swicth on the correct clocks and possibly set the host
   interface as a wkaeup source.  They should not be used if
   proper HIP opperation is required, but are useful before we
   do a code download. */
static const struct chip_helper_init_values init_vals_v1[] = {
    { 0xFDBB, 0xFFFF },
    { 0xFDB6, 0x03FF },
    { 0xFDB1, 0x01E3 },
    { 0xFDB3, 0x0FFF },
    { 0xFEE3, 0x08F0 },
    { 0xFEE7, 0x3C3F },
    { 0xFEE6, 0x0050 },
    { 0xFDBA, 0x0000 }
};

static const struct chip_helper_init_values init_vals_v2[] = {
    { 0xFDB6, 0x0FFF },
    { 0xF023, 0x3F3F },
    { 0xFDB1, 0x01E3 },
    { 0xFDB3, 0x0FFF },
    { 0xF003, 0x08F0 },
    { 0xF007, 0x3C3F },
    { 0xF006, 0x0050 }
};


static const struct chip_helper_init_values init_vals_v22_v23[] = {
    { 0xF81C, 0x00FF },
    /*{ 0x????, 0x???? }, */
    { 0xF80C, 0x1FFF },
    { 0xFA25, 0x001F },
    { 0xF804, 0x00FF },
    { 0xF802, 0x0FFF },
    /*{ 0x????, 0x???? },
      { 0x????, 0x???? },
      { 0x????, 0x???? }*/
};

static const u16 reset_program_a_v1_or_v2[] = {
    0x0000
};
static const u16 reset_program_b_v1_or_v2[] = {
    0x0010, 0xFE00, 0xA021, 0xFF00, 0x8111, 0x0009, 0x0CA4, 0x0114,
    0x0280, 0x04F8, 0xFE00, 0x6F25, 0x06E0, 0x0010, 0xFC00, 0x0121,
    0xFC00, 0x0225, 0xFE00, 0x7125, 0xFE00, 0x6D11, 0x03F0, 0xFE00,
    0x6E25, 0x0008, 0x00E0
};

static const struct chip_helper_reset_values reset_program_v1_or_v2[] =
{
    {
        MAKE_GP(REGISTERS, 0x000C),
        nelem(reset_program_a_v1_or_v2),
        reset_program_a_v1_or_v2
    },
    {
        MAKE_GP(MAC_PMEM, 0x000000),
        nelem(reset_program_b_v1_or_v2),
        reset_program_b_v1_or_v2
    }
};

static const struct chip_map_address_t unifi_map_address_v1_v2[] =
{
    { 0xFE9F, 0xFE7B },     /* PM1_BANK_SELECT */
    { 0xFE9E, 0xFE78 },     /* PM2_BANK_SELECT */
    { 0xFE9D, 0xFE7E },     /* SHARED_DMEM_PAGE */
    { 0xFE91, 0xFE90 },     /* PROC_SELECT */
    { 0xFE8D, 0xFE8C },     /* STOP_STATUS */
};

static const struct chip_map_address_t unifi_map_address_v22_v23[] =
{
    { 0xF8F9, 0xF8AC },     /* GW1_CONFIG */
    { 0xF8FA, 0xF8AD },     /* GW2_CONFIG */
    { 0xF8FB, 0xF8AE },     /* GW3_CONFIG */
    { 0xF830, 0xF81E },     /* PROC_SELECT */
    { 0xF831, 0xF81F },     /* STOP_STATUS */
    { 0xF8FC, 0xF8AF },     /* IO_LOG_ADDRESS */
};

static const struct chip_device_regs_t unifi_device_regs_null =
{
    0xFE81,                     /* GBL_CHIP_VERSION */
    0x0000,                     /* GBL_MISC_ENABLES */
    0x0000,                     /* DBG_EMU_CMD */
    {
        0x0000,                 /* HOST.DBG_PROC_SELECT */
        0x0000,                 /* HOST.DBG_STOP_STATUS */
        0x0000,                 /* HOST.WINDOW1_PAGE */
        0x0000,                 /* HOST.WINDOW2_PAGE */
        0x0000,                 /* HOST.WINDOW3_PAGE */
        0x0000                  /* HOST.IO_LOG_ADDR */
    },
    {
        0x0000,                 /* SPI.DBG_PROC_SELECT */
        0x0000,                 /* SPI.DBG_STOP_STATUS */
        0x0000,                 /* SPI.WINDOW1_PAGE */
        0x0000,                 /* SPI.WINDOW2_PAGE */
        0x0000,                 /* SPI.WINDOW3_PAGE */
        0x0000                  /* SPI.IO_LOG_ADDR */
    },
    0x0000,                     /* DBG_RESET */
    0x0000,                     /* > DBG_RESET_VALUE */
    0x0000,                     /* DBG_RESET_WARN */
    0x0000,                     /* DBG_RESET_WARN_VALUE */
    0x0000,                     /* DBG_RESET_RESULT */
    0xFFE9,                     /* XAP_PCH */
    0xFFEA,                     /* XAP_PCL */
    0x0000,                     /* PROC_PC_SNOOP */
    0x0000,                     /* WATCHDOG_DISABLE */
    0x0000,                     /* MAILBOX0 */
    0x0000,                     /* MAILBOX1 */
    0x0000,                     /* MAILBOX2 */
    0x0000,                     /* MAILBOX3 */
    0x0000,                     /* SDIO_HOST_INT */
    0x0000,                     /* SHARED_IO_INTERRUPT */
    0x0000,                     /* SDIO HIP HANDSHAKE */
    0x0000                      /* COEX_STATUS */
};

/* UF105x */
static const struct chip_device_regs_t unifi_device_regs_v1 =
{
    0xFE81,                     /* GBL_CHIP_VERSION */
    0xFE87,                     /* GBL_MISC_ENABLES */
    0xFE9C,                     /* DBG_EMU_CMD */
    {
        0xFE90,                 /* HOST.DBG_PROC_SELECT */
        0xFE8C,                 /* HOST.DBG_STOP_STATUS */
        0xFE7B,                 /* HOST.WINDOW1_PAGE */
        0xFE78,                 /* HOST.WINDOW2_PAGE */
        0xFE7E,                 /* HOST.WINDOW3_PAGE */
        0x0000                  /* HOST.IO_LOG_ADDR */
    },
    {
        0xFE91,                 /* SPI.DBG_PROC_SELECT */
        0xFE8D,                 /* SPI.DBG_STOP_STATUS */
        0xFE9F,                 /* SPI.WINDOW1_PAGE */
        0xFE9E,                 /* SPI.WINDOW2_PAGE */
        0xFE9D,                 /* SPI.WINDOW3_PAGE */
        0x0000                  /* SPI.IO_LOG_ADDR */
    },
    0xFE92,                     /* DBG_RESET */
    0x0001,                     /* > DBG_RESET_VALUE */
    0xFDA0,                     /* DBG_RESET_WARN (HOST_SELECT) */
    0x0000,                     /* DBG_RESET_WARN_VALUE */
    0xFE92,                     /* DBG_RESET_RESULT */
    0xFFE9,                     /* XAP_PCH */
    0xFFEA,                     /* XAP_PCL */
    0x0051,                     /* PROC_PC_SNOOP */
    0xFE70,                     /* WATCHDOG_DISABLE */
    0xFE6B,                     /* MAILBOX0 */
    0xFE6A,                     /* MAILBOX1 */
    0xFE69,                     /* MAILBOX2 */
    0xFE68,                     /* MAILBOX3 */
    0xFE67,                     /* SDIO_HOST_INT */
    0xFE65,                     /* SHARED_IO_INTERRUPT */
    0xFDE9,                     /* SDIO HIP HANDSHAKE */
    0x0000                      /* COEX_STATUS */
};

/* UF2... */
static const struct chip_device_regs_t unifi_device_regs_v2 =
{
    0xFE81,                     /* GBL_CHIP_VERSION */
    0xFE87,                     /* GBL_MISC_ENABLES */
    0xFE9C,                     /* DBG_EMU_CMD */
    {
        0xFE90,                 /* HOST.DBG_PROC_SELECT */
        0xFE8C,                 /* HOST.DBG_STOP_STATUS */
        0xFE7B,                 /* HOST.WINDOW1_PAGE */
        0xFE78,                 /* HOST.WINDOW2_PAGE */
        0xFE7E,                 /* HOST.WINDOW3_PAGE */
        0x0000                  /* HOST.IO_LOG_ADDR */
    },
    {
        0xFE91,                 /* SPI.DBG_PROC_SELECT */
        0xFE8D,                 /* SPI.DBG_STOP_STATUS */
        0xFE9F,                 /* SPI.WINDOW1_PAGE */
        0xFE9E,                 /* SPI.WINDOW2_PAGE */
        0xFE9D,                 /* SPI.WINDOW3_PAGE */
        0x0000                  /* SPI.IO_LOG_ADDR */
    },
    0xFE92,                     /* DBG_RESET */
    0x0000,                     /* > DBG_RESET_VALUE */
    0xFDE9,                     /* DBG_RESET_WARN (TEST_FLASH_DATA - SHARED_MAILBOX2B) */
    0xFFFF,                     /* DBG_RESET_WARN_VALUE */
    0xFDE9,                     /* DBG_RESET_RESULT (TEST_FLASH_DATA) */
    0xFFE9,                     /* XAP_PCH */
    0xFFEA,                     /* XAP_PCL */
    0x0051,                     /* PROC_PC_SNOOP */
    0xFE70,                     /* WATCHDOG_DISABLE */
    0xFE6B,                     /* MAILBOX0 */
    0xFE6A,                     /* MAILBOX1 */
    0xFE69,                     /* MAILBOX2 */
    0xFE68,                     /* MAILBOX3 */
    0xFE67,                     /* SDIO_HOST_INT */
    0xFE65,                     /* SHARED_IO_INTERRUPT */
    0xFE69,                     /* SDIO HIP HANDSHAKE */
    0x0000                      /* COEX_STATUS */
};

/* UF60xx */
static const struct chip_device_regs_t unifi_device_regs_v22_v23 =
{
    0xFE81,                     /* GBL_CHIP_VERSION */
    0xF84F,                     /* GBL_MISC_ENABLES */
    0xF81D,                     /* DBG_EMU_CMD */
    {
        0xF81E,                 /* HOST.DBG_PROC_SELECT */
        0xF81F,                 /* HOST.DBG_STOP_STATUS */
        0xF8AC,                 /* HOST.WINDOW1_PAGE */
        0xF8AD,                 /* HOST.WINDOW2_PAGE */
        0xF8AE,                 /* HOST.WINDOW3_PAGE */
        0xF8AF                  /* HOST.IO_LOG_ADDR */
    },
    {
        0xF830,                 /* SPI.DBG_PROC_SELECT */
        0xF831,                 /* SPI.DBG_STOP_STATUS */
        0xF8F9,                 /* SPI.WINDOW1_PAGE */
        0xF8FA,                 /* SPI.WINDOW2_PAGE */
        0xF8FB,                 /* SPI.WINDOW3_PAGE */
        0xF8FC                  /* SPI.IO_LOG_ADDR */
    },
    0xF82F,                     /* DBG_RESET */
    0x0001,                     /* > DBG_RESET_VALUE */
    0x0000,                     /* DBG_RESET_WARN */
    0x0000,                     /* DBG_RESET_WARN_VALUE */
    0xF82F,                     /* DBG_RESET_RESULT */
    0xFFE9,                     /* XAP_PCH */
    0xFFEA,                     /* XAP_PCL */
    0x001B,                     /* PROC_PC_SNOOP */
    0x0055,                     /* WATCHDOG_DISABLE */
    0xF84B,                     /* MAILBOX0 */
    0xF84C,                     /* MAILBOX1 */
    0xF84D,                     /* MAILBOX2 */
    0xF84E,                     /* MAILBOX3 */
    0xF92F,                     /* SDIO_HOST_INT */
    0xF92B,                     /* SDIO_FROMHOST_SCRTACH0 / SHARED_IO_INTERRUPT */
    0xF84D,                     /* SDIO HIP HANDSHAKE (MAILBOX2) */
    0xF9FB                      /* COEX_STATUS */
};

/* Program memory window on UF105x. */
static const struct window_shift_info_t prog_window_array_unifi_v1_v2[CHIP_HELPER_WT_COUNT] =
{
    { TRUE, 11, 0x0200 }, /* CODE RAM */
    { TRUE, 11, 0x0000 }, /* FLASH */
    { TRUE, 11, 0x0400 }, /* External SRAM */
    { FALSE, 0, 0 },      /* ROM */
    { FALSE, 0, 0 }       /* SHARED */
};

/* Shared memory window on UF105x. */
static const struct window_shift_info_t shared_window_array_unifi_v1_v2[CHIP_HELPER_WT_COUNT] =
{
    { FALSE, 0, 0 },      /* CODE RAM */
    { FALSE, 0, 0 },      /* FLASH */
    { FALSE, 0, 0 },      /* External SRAM */
    { FALSE, 0, 0 },      /* ROM */
    { TRUE, 11, 0x0000 }  /* SHARED */
};

/* One of the Generic Windows on UF60xx and later. */
static const struct window_shift_info_t generic_window_array_unifi_v22_v23[CHIP_HELPER_WT_COUNT] =
{
    { TRUE, 11, 0x3800 }, /* CODE RAM */
    { FALSE, 0, 0 },      /* FLASH */
    { FALSE, 0, 0 },      /* External SRAM */
    { TRUE, 11, 0x2000 }, /* ROM */
    { TRUE, 11, 0x0000 }  /* SHARED */
};

/* The three windows on UF105x. */
static const struct window_info_t prog1_window_unifi_v1_v2  = { 0x0000, 0x2000, 0x0080, prog_window_array_unifi_v1_v2 };
static const struct window_info_t prog2_window_unifi_v1_v2  = { 0x2000, 0x2000, 0x0000, prog_window_array_unifi_v1_v2 };
static const struct window_info_t shared_window_unifi_v1_v2 = { 0x4000, 0x2000, 0x0000, shared_window_array_unifi_v1_v2 };

/* The three windows on UF60xx and later. */
static const struct window_info_t generic1_window_unifi_v22_v23 = { 0x0000, 0x2000, 0x0080, generic_window_array_unifi_v22_v23 };
static const struct window_info_t generic2_window_unifi_v22_v23 = { 0x2000, 0x2000, 0x0000, generic_window_array_unifi_v22_v23 };
static const struct window_info_t generic3_window_unifi_v22_v23 = { 0x4000, 0x2000, 0x0000, generic_window_array_unifi_v22_v23 };

static const struct chip_device_desc_t chip_device_desc_null =
{
    { FALSE, 0x0000, 0x0000, 0x00 },
    "",
    "",
    null_counted(),                         /* init */
    null_counted(),                         /* reset_prog */
    &unifi_device_regs_null,                /* regs */
    {
        FALSE,                              /* has_flash */
        FALSE,                              /* has_ext_sram */
        FALSE,                              /* has_rom */
        FALSE,                              /* has_bt */
        FALSE,                              /* has_wlan */
    },
    null_counted(),
    /* prog_offset */
    {
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000
    },
    /* data_offset */
    {
        0x0000                              /* ram */
    },
    /* windows */
    {
        NULL,
        NULL,
        NULL
    }
};

static const struct chip_device_desc_t unifi_device_desc_v1 =
{
    { FALSE, 0xf0ff, 0x1001, 0x01 },        /* UF105x R01 */
    "UF105x",
    "UniFi-1",
    counted(init_vals_v1),                  /* init */
    counted(reset_program_v1_or_v2),        /* reset_prog */
    &unifi_device_regs_v1,                  /* regs */
    {
        TRUE,                               /* has_flash    */
        TRUE,                               /* has_ext_sram */
        FALSE,                              /* has_rom      */
        FALSE,                              /* has_bt       */
        TRUE,                               /* has_wlan */
    },
    counted(unifi_map_address_v1_v2),       /* map */
    /* prog_offset */
    {
        0x00100000,                         /* ram */
        0x00000000,                         /* rom (invalid) */
        0x00000000,                         /* flash */
        0x00200000,                         /* ext_ram */
    },
    /* data_offset */
    {
        0x8000                              /* ram */
    },
    /* windows */
    {
        &prog1_window_unifi_v1_v2,
        &prog2_window_unifi_v1_v2,
        &shared_window_unifi_v1_v2
    }
};

static const struct chip_device_desc_t unifi_device_desc_v2 =
{
    { FALSE, 0xf0ff, 0x2001, 0x02 },        /* UF2... R02 */
    "UF2...",
    "UniFi-2",
    counted(init_vals_v2),                  /* init */
    counted(reset_program_v1_or_v2),        /* reset_prog */
    &unifi_device_regs_v2,                  /* regs */
    {
        TRUE,                               /* has_flash    */
        TRUE,                               /* has_ext_sram */
        FALSE,                              /* has_rom      */
        FALSE,                              /* has_bt      */
        TRUE,                               /* has_wlan */
    },
    counted(unifi_map_address_v1_v2),       /* map */
    /* prog_offset */
    {
        0x00100000,                         /* ram */
        0x00000000,                         /* rom (invalid) */
        0x00000000,                         /* flash */
        0x00200000,                         /* ext_ram */
    },
    /* data_offset */
    {
        0x8000                              /* ram */
    },
    /* windows */
    {
        &prog1_window_unifi_v1_v2,
        &prog2_window_unifi_v1_v2,
        &shared_window_unifi_v1_v2
    }
};

static const struct chip_device_desc_t unifi_device_desc_v3 =
{
    { FALSE, 0xf0ff, 0x3001, 0x02 },        /* UF2... R03 */
    "UF2...",
    "UniFi-3",
    counted(init_vals_v2),                  /* init */
    counted(reset_program_v1_or_v2),        /* reset_prog */
    &unifi_device_regs_v2,                  /* regs */
    {
        TRUE,                               /* has_flash    */
        TRUE,                               /* has_ext_sram */
        FALSE,                              /* has_rom      */
        FALSE,                              /* has_bt      */
        TRUE,                               /* has_wlan */
    },
    counted(unifi_map_address_v1_v2),       /* map */
    /* prog_offset */
    {
        0x00100000,                         /* ram */
        0x00000000,                         /* rom (invalid) */
        0x00000000,                         /* flash */
        0x00200000,                         /* ext_ram */
    },
    /* data_offset */
    {
        0x8000                              /* ram */
    },
    /* windows */
    {
        &prog1_window_unifi_v1_v2,
        &prog2_window_unifi_v1_v2,
        &shared_window_unifi_v1_v2
    }
};

static const struct chip_device_desc_t unifi_device_desc_v22 =
{
    { FALSE, 0x00ff, 0x0022, 0x07 },        /* UF60xx */
    "UF60xx",
    "UniFi-4",
    counted(init_vals_v22_v23),             /* init */
    null_counted(),                         /* reset_prog */
    &unifi_device_regs_v22_v23,             /* regs */
    {
        FALSE,                              /* has_flash    */
        FALSE,                              /* has_ext_sram */
        TRUE,                               /* has_rom      */
        FALSE,                              /* has_bt       */
        TRUE,                               /* has_wlan */
    },
    counted(unifi_map_address_v22_v23),     /* map */
    /* prog_offset */
    {
        0x00C00000,                         /* ram */
        0x00000000,                         /* rom */
        0x00000000,                         /* flash (invalid) */
        0x00000000,                         /* ext_ram (invalid) */
    },
    /* data_offset */
    {
        0x8000                              /* ram */
    },
    /* windows */
    {
        &generic1_window_unifi_v22_v23,
        &generic2_window_unifi_v22_v23,
        &generic3_window_unifi_v22_v23
    }
};

static const struct chip_device_desc_t unifi_device_desc_v23 =
{
    { FALSE, 0x00ff, 0x0023, 0x08 },        /* UF.... */
    "UF....",
    "UF.... (5)",
    counted(init_vals_v22_v23),             /* init */
    null_counted(),                         /* reset_prog */
    &unifi_device_regs_v22_v23,             /* regs */
    {
        FALSE,                              /* has_flash    */
        FALSE,                              /* has_ext_sram */
        TRUE,                               /* has_rom      */
        TRUE,                               /* has_bt       */
        TRUE,                               /* has_wlan */
    },
    counted(unifi_map_address_v22_v23),
    /* prog_offset */
    {
        0x00C00000,                         /* ram */
        0x00000000,                         /* rom */
        0x00000000,                         /* flash (invalid) */
        0x00000000,                         /* ext_sram (invalid) */
    },
    /* data_offset */
    {
        0x8000                              /* ram */
    },
    /* windows */
    {
        &generic1_window_unifi_v22_v23,
        &generic2_window_unifi_v22_v23,
        &generic3_window_unifi_v22_v23
    }
};

static const struct chip_device_desc_t hyd_wlan_subsys_desc_v1 =
{
    { FALSE, 0x00ff, 0x0044, 0x00 },        /* UF.... */
    "HYD...",
    "HYD...    ",
    counted(init_vals_v22_v23),             /* init */
    null_counted(),                         /* reset_prog */
    &unifi_device_regs_v22_v23,             /* regs */
    {
        FALSE,                              /* has_flash    */
        FALSE,                              /* has_ext_sram */
        TRUE,                               /* has_rom      */
        FALSE,                              /* has_bt       */
        TRUE,                               /* has_wlan */
    },
    counted(unifi_map_address_v22_v23),
    /* prog_offset */
    {
        0x00C00000,                         /* ram */
        0x00000000,                         /* rom */
        0x00000000,                         /* flash (invalid) */
        0x00000000,                         /* ext_sram (invalid) */
    },
    /* data_offset */
    {
        0x8000                              /* ram */
    },
    /* windows */
    {
        &generic1_window_unifi_v22_v23,
        &generic2_window_unifi_v22_v23,
        &generic3_window_unifi_v22_v23
    }
};


/* This is the list of all chips that we know about.  I'm
   assuming that the order here will be important - we
   might have multiple entries witrh the same SDIO id for
   instance.  The first one in this list will be the one
   that is returned if a search is done on only that id.
   The client will then have to call GetVersionXXX again
   but with more detailed info.

   I don't know if we need to signal this up to the client
   in some way?

   (We get the SDIO id before we know anything else about
   the chip.  We might not be able to read any of the other
   registers at first, but we still need to know about the
   chip). */
static const struct chip_device_desc_t *chip_ver_to_desc[] =
{
    &unifi_device_desc_v1,      /* UF105x R01 */
    &unifi_device_desc_v2,      /* UF2... R02 */
    &unifi_device_desc_v3,      /* UF2... R03 */
    &unifi_device_desc_v22,     /* UF60xx */
    &unifi_device_desc_v23,     /* UF.... */
    &hyd_wlan_subsys_desc_v1
};

ChipDescript* ChipHelper_GetVersionSdio(u8 sdio_ver)
{
    u32 i;

    for (i = 0; i < nelem(chip_ver_to_desc); i++)
    {
        if (chip_ver_to_desc[i]->chip_version.sdio == sdio_ver)
        {
            return chip_ver_to_desc[i];
        }
    }

    return &chip_device_desc_null;
}


ChipDescript* ChipHelper_GetVersionAny(u16 from_FF9A, u16 from_FE81)
{
    u32 i;

    if ((from_FF9A & 0xFF00) != 0)
    {
        for (i = 0; i < nelem(chip_ver_to_desc); i++)
        {
            if (chip_ver_to_desc[i]->chip_version.pre_bc7 &&
                ((from_FF9A & chip_ver_to_desc[i]->chip_version.mask) ==
                 chip_ver_to_desc[i]->chip_version.result))
            {
                return chip_ver_to_desc[i];
            }
        }
    }
    else
    {
        for (i = 0; i < nelem(chip_ver_to_desc); i++)
        {
            if (!chip_ver_to_desc[i]->chip_version.pre_bc7 &&
                ((from_FE81 & chip_ver_to_desc[i]->chip_version.mask) ==
                 chip_ver_to_desc[i]->chip_version.result))
            {
                return chip_ver_to_desc[i];
            }
        }
    }

    return &chip_device_desc_null;
}


ChipDescript* ChipHelper_GetVersionUniFi(u16 ver)
{
    return ChipHelper_GetVersionAny(0x0000, ver);
}


ChipDescript *ChipHelper_Null(void)
{
    return &chip_device_desc_null;
}


ChipDescript* ChipHelper_GetVersionBlueCore(enum chip_helper_bluecore_age bc_age, u16 version)
{
    if (bc_age == chip_helper_bluecore_pre_bc7)
    {
        return ChipHelper_GetVersionAny(version, 0x0000);
    }
    else
    {
        return ChipHelper_GetVersionAny(0x0000, version);
    }
}


/* Expand the DEF0 functions into simple code to return the
   correct thing.  The DEF1 functions expand to nothing in
   this X macro expansion. */
#define CHIP_HELPER_DEF0_C_DEF(ret_type, name, info)            \
    ret_type ChipHelper_ ## name(ChipDescript * chip_help)           \
    {                                                               \
        return chip_help->info;                                     \
    }
#define CHIP_HELPER_DEF1_C_DEF(ret_type, name, type1, name1)

CHIP_HELPER_LIST(C_DEF)

/*
 * Map register addresses between HOST and SPI access.
 */
u16 ChipHelper_MapAddress_SPI2HOST(ChipDescript *chip_help, u16 addr)
{
    u32 i;
    for (i = 0; i < chip_help->map.len; i++)
    {
        if (chip_help->map.vals[i].spi == addr)
        {
            return chip_help->map.vals[i].host;
        }
    }
    return addr;
}


u16 ChipHelper_MapAddress_HOST2SPI(ChipDescript *chip_help, u16 addr)
{
    u32 i;
    for (i = 0; i < chip_help->map.len; i++)
    {
        if (chip_help->map.vals[i].host == addr)
        {
            return chip_help->map.vals[i].spi;
        }
    }
    return addr;
}


/* The address returned by this function is the start of the
   window in the address space, that is where we can start
   accessing data from.  If a section of the window at the
   start is unusable because something else is cluttering up
   the address map then that is taken into account and this
   function returns that address justt past that. */
u16 ChipHelper_WINDOW_ADDRESS(ChipDescript                 *chip_help,
                                    enum chip_helper_window_index window)
{
    if (window < CHIP_HELPER_WINDOW_COUNT &&
        chip_help->windows[window] != NULL)
    {
        return chip_help->windows[window]->address + chip_help->windows[window]->blocked;
    }
    return 0;
}


/* This returns the size of the window minus any blocked section */
u16 ChipHelper_WINDOW_SIZE(ChipDescript                 *chip_help,
                                 enum chip_helper_window_index window)
{
    if (window < CHIP_HELPER_WINDOW_COUNT &&
        chip_help->windows[window] != NULL)
    {
        return chip_help->windows[window]->size - chip_help->windows[window]->blocked;
    }
    return 0;
}


/* Get the register writes we should do to make sure that
   the chip is running with most clocks on. */
u32 ChipHelper_ClockStartupSequence(ChipDescript                          *chip_help,
                                          const struct chip_helper_init_values **val)
{
    *val = chip_help->init.vals;
    return chip_help->init.len;
}


/* Get the set of values tat we should write to the chip to perform a reset. */
u32 ChipHelper_HostResetSequence(ChipDescript                           *chip_help,
                                       const struct chip_helper_reset_values **val)
{
    *val = chip_help->reset_prog.vals;
    return chip_help->reset_prog.len;
}


/* Decode a windowed access to the chip. */
s32 ChipHelper_DecodeWindow(ChipDescript *chip_help,
                                 enum chip_helper_window_index window,
                                 enum chip_helper_window_type type,
                                 u32 offset,
                                 u16 *page, u16 *addr, u32 *len)
{
    const struct window_info_t *win;
    const struct window_shift_info_t *mode;
    u16 of, pg;

    if (window >= CHIP_HELPER_WINDOW_COUNT)
    {
        return FALSE;
    }
    if ((win = chip_help->windows[window]) == NULL)
    {
        return FALSE;
    }
    if (type >= CHIP_HELPER_WT_COUNT)
    {
        return FALSE;
    }
    if ((mode = &win->mode[type]) == NULL)
    {
        return FALSE;
    }
    if (!mode->allowed)
    {
        return FALSE;
    }

    pg = (u16)(offset >> mode->page_shift) + mode->page_offset;
    of = (u16)(offset & ((1 << mode->page_shift) - 1));
    /* If 'blocked' is zero this does nothing, else decrease
       the page register and increase the offset until we aren't
       in the blocked region of the window. */
    while (of < win->blocked)
    {
        of += 1 << mode->page_shift;
        pg--;
    }
    *page = pg;
    *addr = win->address + of;
    *len = win->size - of;
    return TRUE;
}


