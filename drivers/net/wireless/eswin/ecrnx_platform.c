/**
 ******************************************************************************
 *
 * @file ecrnx_platform.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include "ecrnx_platform.h"
#include "reg_access.h"
#include "hal_desc.h"
#include "ecrnx_main.h"

#ifndef CONFIG_ECRNX_ESWIN
#include "ecrnx_pci.h"
#ifndef CONFIG_ECRNX_FHOST
#include "ipc_host.h"
#endif /* !CONFIG_ECRNX_FHOST */
#endif /* CONFIG_ECRNX_ESWIN */

#if defined(CONFIG_ECRNX_ESWIN_SDIO)
#include "sdio.h"
#include "ecrnx_sdio.h"
#elif defined(CONFIG_ECRNX_ESWIN_USB)
#include "usb.h"
#include "ecrnx_usb.h"
#endif
#ifdef CONFIG_ECRNX_WIFO_CAIL
#include "core.h"
#include "ecrnx_amt.h"
#endif

#ifdef CONFIG_ECRNX_TL4
/**
 * ecrnx_plat_tl4_fw_upload() - Load the requested FW into embedded side.
 *
 * @ecrnx_plat: pointer to platform structure
 * @fw_addr: Virtual address where the fw must be loaded
 * @filename: Name of the fw.
 *
 * Load a fw, stored as a hex file, into the specified address
 */
static int ecrnx_plat_tl4_fw_upload(struct ecrnx_plat *ecrnx_plat, u8* fw_addr,
                                   char *filename)
{
    struct device *dev = ecrnx_platform_get_dev(ecrnx_plat);
    const struct firmware *fw;
    int err = 0;
    u32 *dst;
    u8 const *file_data;
    char typ0, typ1;
    u32 addr0, addr1;
    u32 dat0, dat1;
    int remain;

    err = request_firmware(&fw, filename, dev);
    if (err) {
        return err;
    }
    file_data = fw->data;
    remain = fw->size;

    /* Copy the file on the Embedded side */
    dev_dbg(dev, "\n### Now copy %s firmware, @ = %p\n", filename, fw_addr);

    /* Walk through all the lines of the configuration file */
    while (remain >= 16) {
        u32 data, offset;

        if (sscanf(file_data, "%c:%08X %04X", &typ0, &addr0, &dat0) != 3)
            break;
        if ((addr0 & 0x01) != 0) {
            addr0 = addr0 - 1;
            dat0 = 0;
        } else {
            file_data += 16;
            remain -= 16;
        }
        if ((remain < 16) ||
            (sscanf(file_data, "%c:%08X %04X", &typ1, &addr1, &dat1) != 3) ||
            (typ1 != typ0) || (addr1 != (addr0 + 1))) {
            typ1 = typ0;
            addr1 = addr0 + 1;
            dat1 = 0;
        } else {
            file_data += 16;
            remain -= 16;
        }

        if (typ0 == 'C') {
            offset = 0x00200000;
            if ((addr1 % 4) == 3)
                offset += 2*(addr1 - 3);
            else
                offset += 2*(addr1 + 1);

            data = dat1 | (dat0 << 16);
        } else {
            offset = 2*(addr1 - 1);
            data = dat0 | (dat1 << 16);
        }
        dst = (u32 *)(fw_addr + offset);
        *dst = data;
    }

    release_firmware(fw);

    return err;
}
#endif

/**
 * ecrnx_plat_bin_fw_upload() - Load the requested binary FW into embedded side.
 *
 * @ecrnx_plat: pointer to platform structure
 * @fw_addr: Virtual address where the fw must be loaded
 * @filename: Name of the fw.
 *
 * Load a fw, stored as a binary file, into the specified address
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_plat_bin_fw_upload(struct ecrnx_plat *ecrnx_plat, u8* fw_addr,
                               char *filename)
{
    const struct firmware *fw;
    struct device *dev = ecrnx_platform_get_dev(ecrnx_plat);
    int err = 0;
    unsigned int i, size;
    u32 *src, *dst;

    err = request_firmware(&fw, filename, dev);
    if (err) {
        return err;
    }

    /* Copy the file on the Embedded side */
    dev_dbg(dev, "\n### Now copy %s firmware, @ = %p\n", filename, fw_addr);

    src = (u32 *)fw->data;
    dst = (u32 *)fw_addr;
    size = (unsigned int)fw->size;

    /* check potential platform bug on multiple stores vs memcpy */
    for (i = 0; i < size; i += 4) {
        *dst++ = *src++;
    }

    release_firmware(fw);

    return err;
}
#endif

#ifndef CONFIG_ECRNX_TL4
#define IHEX_REC_DATA           0
#define IHEX_REC_EOF            1
#define IHEX_REC_EXT_SEG_ADD    2
#define IHEX_REC_START_SEG_ADD  3
#define IHEX_REC_EXT_LIN_ADD    4
#define IHEX_REC_START_LIN_ADD  5

/**
 * ecrnx_plat_ihex_fw_upload() - Load the requested intel hex 8 FW into embedded side.
 *
 * @ecrnx_plat: pointer to platform structure
 * @fw_addr: Virtual address where the fw must be loaded
 * @filename: Name of the fw.
 *
 * Load a fw, stored as a ihex file, into the specified address.
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_plat_ihex_fw_upload(struct ecrnx_plat *ecrnx_plat, u8* fw_addr,
                                    char *filename)
{
    const struct firmware *fw;
    struct device *dev = ecrnx_platform_get_dev(ecrnx_plat);
    u8 const *src, *end;
    u32 *dst;
    u16 haddr, segaddr, addr;
    u32 hwaddr;
    u8 load_fw, byte_count, checksum, csum, rec_type;
    int err, rec_idx;
    char hex_buff[9];

    err = request_firmware(&fw, filename, dev);
    if (err) {
        return err;
    }

    /* Copy the file on the Embedded side */
    dev_dbg(dev, "\n### Now copy %s firmware, @ = %p\n", filename, fw_addr);

    src = fw->data;
    end = src + (unsigned int)fw->size;
    haddr = 0;
    segaddr = 0;
    load_fw = 1;
    err = -EINVAL;
    rec_idx = 0;
    hwaddr = 0;

#define IHEX_READ8(_val, _cs) {                  \
        hex_buff[2] = 0;                         \
        strncpy(hex_buff, src, 2);               \
        if (kstrtou8(hex_buff, 16, &_val))       \
            goto end;                            \
        src += 2;                                \
        if (_cs)                                 \
            csum += _val;                        \
    }

#define IHEX_READ16(_val) {                        \
        hex_buff[4] = 0;                           \
        strncpy(hex_buff, src, 4);                 \
        if (kstrtou16(hex_buff, 16, &_val))        \
            goto end;                              \
        src += 4;                                  \
        csum += (_val & 0xff) + (_val >> 8);       \
    }

#define IHEX_READ32(_val) {                              \
        hex_buff[8] = 0;                                 \
        strncpy(hex_buff, src, 8);                       \
        if (kstrtouint(hex_buff, 16, &_val))             \
            goto end;                                    \
        src += 8;                                        \
        csum += (_val & 0xff) + ((_val >> 8) & 0xff) +   \
            ((_val >> 16) & 0xff) + (_val >> 24);        \
    }

#define IHEX_READ32_PAD(_val, _nb) {                    \
        memset(hex_buff, '0', 8);                       \
        hex_buff[8] = 0;                                \
        strncpy(hex_buff, src, (2 * _nb));              \
        if (kstrtouint(hex_buff, 16, &_val))            \
            goto end;                                   \
        src += (2 * _nb);                               \
        csum += (_val & 0xff) + ((_val >> 8) & 0xff) +  \
            ((_val >> 16) & 0xff) + (_val >> 24);       \
}

    /* loop until end of file is read*/
    while (load_fw) {
        rec_idx++;
        csum = 0;

        /* Find next colon start code */
        while (*src != ':') {
            src++;
            if ((src + 3) >= end) /* 3 = : + rec_len */
                goto end;
        }
        src++;

        /* Read record len */
        IHEX_READ8(byte_count, 1);
        if ((src + (byte_count * 2) + 8) >= end) /* 8 = rec_addr + rec_type + chksum */
            goto end;

        /* Read record addr */
        IHEX_READ16(addr);

        /* Read record type */
        IHEX_READ8(rec_type, 1);

        switch(rec_type) {
            case IHEX_REC_DATA:
            {
                /* Update destination address */
                dst = (u32 *) (fw_addr + hwaddr + addr);

                while (byte_count) {
                    u32 val;
                    if (byte_count >= 4) {
                        IHEX_READ32(val);
                        byte_count -= 4;
                    } else {
                        IHEX_READ32_PAD(val, byte_count);
                        byte_count = 0;
                    }
                    *dst++ = __swab32(val);
                }
                break;
            }
            case IHEX_REC_EOF:
            {
                load_fw = 0;
                err = 0;
                break;
            }
            case IHEX_REC_EXT_SEG_ADD: /* Extended Segment Address */
            {
                IHEX_READ16(segaddr);
                hwaddr = (haddr << 16) + (segaddr << 4);
                break;
            }
            case IHEX_REC_EXT_LIN_ADD: /* Extended Linear Address */
            {
                IHEX_READ16(haddr);
                hwaddr = (haddr << 16) + (segaddr << 4);
                break;
            }
            case IHEX_REC_START_LIN_ADD: /* Start Linear Address */
            {
                u32 val;
                IHEX_READ32(val); /* need to read for checksum */
                break;
            }
            case IHEX_REC_START_SEG_ADD:
            default:
            {
                dev_err(dev, "ihex: record type %d not supported\n", rec_type);
                load_fw = 0;
            }
        }

        /* Read and compare checksum */
        IHEX_READ8(checksum, 0);
        if (checksum != (u8)(~csum + 1))
            goto end;
    }

#undef IHEX_READ8
#undef IHEX_READ16
#undef IHEX_READ32
#undef IHEX_READ32_PAD

  end:
    release_firmware(fw);

    if (err)
        dev_err(dev, "%s: Invalid ihex record around line %d\n", filename, rec_idx);

    return err;
}
#endif /* CONFIG_ECRNX_TL4 */
#endif

#ifndef CONFIG_ECRNX_ESWIN
#ifndef CONFIG_ECRNX_SDM
/**
 * ecrnx_plat_get_rf() - Retrun the RF used in the platform
 *
 * @ecrnx_plat: pointer to platform structure
 */
static u32 ecrnx_plat_get_rf(struct ecrnx_plat *ecrnx_plat)
{
    u32 ver;
    ver = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, MDM_HDMCONFIG_ADDR);

    ver = __MDM_PHYCFG_FROM_VERS(ver);
    WARN(((ver != MDM_PHY_CONFIG_TRIDENT) &&
          (ver != MDM_PHY_CONFIG_CATAXIA) &&
          (ver != MDM_PHY_CONFIG_KARST)),
         "Unknown PHY version 0x%08x\n", ver);

    return ver;
}
#endif

/**
 * ecrnx_plat_get_clkctrl_addr() - Return the clock control register address
 *
 * @ecrnx_plat: platform data
 */
#ifndef CONFIG_ECRNX_SDM
#ifndef CONFIG_ECRNX_ESWIN
static u32 ecrnx_plat_get_clkctrl_addr(struct ecrnx_plat *ecrnx_plat)
{
    u32 regval;
    if (ecrnx_plat_get_rf(ecrnx_plat) ==  MDM_PHY_CONFIG_TRIDENT)
        return MDM_MEMCLKCTRL0_ADDR;
    regval = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, SYSCTRL_SIGNATURE_ADDR);
    if (__FPGA_TYPE(regval) == 0xC0CA)
        return CRM_CLKGATEFCTRL0_ADDR;
    else
        return MDM_CLKGATEFCTRL0_ADDR;
}
#endif /* CONFIG_ECRNX_SDM */
#endif

/**
 * ecrnx_plat_stop_agcfsm() - Stop a AGC state machine
 *
 * @ecrnx_plat: pointer to platform structure
 * @agg_reg: Address of the agccntl register (within ECRNX_ADDR_SYSTEM)
 * @agcctl: Updated with value of the agccntl rgister before stop
 * @memclk: Updated with value of the clock register before stop
 * @agc_ver: Version of the AGC load procedure
 * @clkctrladdr: Indicates which AGC clock register should be accessed
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_plat_stop_agcfsm(struct ecrnx_plat *ecrnx_plat, int agc_reg,
                                  u32 *agcctl, u32 *memclk, u8 agc_ver,
                                  u32 clkctrladdr)
{
    /* First read agcctnl and clock registers */
    *memclk = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, clkctrladdr);

    /* Stop state machine : xxAGCCNTL0[AGCFSMRESET]=1 */
    *agcctl = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, agc_reg);
    ECRNX_REG_WRITE((*agcctl) | BIT(12), ecrnx_plat, ECRNX_ADDR_SYSTEM, agc_reg);

    /* Force clock */
    if (agc_ver > 0) {
        /* CLKGATEFCTRL0[AGCCLKFORCE]=1 */
        ECRNX_REG_WRITE((*memclk) | BIT(29), ecrnx_plat, ECRNX_ADDR_SYSTEM,
                       clkctrladdr);
    } else {
        /* MEMCLKCTRL0[AGCMEMCLKCTRL]=0 */
        ECRNX_REG_WRITE((*memclk) & ~BIT(3), ecrnx_plat, ECRNX_ADDR_SYSTEM,
                       clkctrladdr);
    }
}
#endif

/**
 * ecrnx_plat_start_agcfsm() - Restart a AGC state machine
 *
 * @ecrnx_plat: pointer to platform structure
 * @agg_reg: Address of the agccntl register (within ECRNX_ADDR_SYSTEM)
 * @agcctl: value of the agccntl register to restore
 * @memclk: value of the clock register to restore
 * @agc_ver: Version of the AGC load procedure
 * @clkctrladdr: Indicates which AGC clock register should be accessed
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_plat_start_agcfsm(struct ecrnx_plat *ecrnx_plat, int agc_reg,
                                   u32 agcctl, u32 memclk, u8 agc_ver,
                                   u32 clkctrladdr)
{

    /* Release clock */
    if (agc_ver > 0)
        /* CLKGATEFCTRL0[AGCCLKFORCE]=0 */
        ECRNX_REG_WRITE(memclk & ~BIT(29), ecrnx_plat, ECRNX_ADDR_SYSTEM,
                       clkctrladdr);
    else
        /* MEMCLKCTRL0[AGCMEMCLKCTRL]=1 */
        ECRNX_REG_WRITE(memclk | BIT(3), ecrnx_plat, ECRNX_ADDR_SYSTEM,
                       clkctrladdr);

    /* Restart state machine: xxAGCCNTL0[AGCFSMRESET]=0 */
    ECRNX_REG_WRITE(agcctl & ~BIT(12), ecrnx_plat, ECRNX_ADDR_SYSTEM, agc_reg);
}
#endif
#endif

/**
 * ecrnx_plat_get_agc_load_version() - Return the agc load protocol version and the
 * address of the clock control register
 *
 * @ecrnx_plat: platform data
 * @rf: rf in used
 * @clkctrladdr: returned clock control register address
 *
 */
#ifndef CONFIG_ECRNX_ESWIN
#ifndef CONFIG_ECRNX_SDM
static u8 ecrnx_plat_get_agc_load_version(struct ecrnx_plat *ecrnx_plat, u32 rf,
                                         u32 *clkctrladdr)
{
    u8 agc_load_ver = 0;
    u32 agc_ver;
    u32 regval;

    *clkctrladdr = ecrnx_plat_get_clkctrl_addr(ecrnx_plat);
    /* Trident and Elma PHY use old method */
    if (rf ==  MDM_PHY_CONFIG_TRIDENT)
        return 0;

    /* Get the FPGA signature */
    regval = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, SYSCTRL_SIGNATURE_ADDR);


    /* Read RIU version register */
    agc_ver = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, RIU_ECRNXVERSION_ADDR);
    agc_load_ver = __RIU_AGCLOAD_FROM_VERS(agc_ver);

    return agc_load_ver;
}
#endif /* CONFIG_ECRNX_SDM */
#endif

/**
 * ecrnx_plat_agc_load() - Load AGC ucode
 *
 * @ecrnx_plat: platform data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_plat_agc_load(struct ecrnx_plat *ecrnx_plat)
{
    int ret = 0;
#ifndef CONFIG_ECRNX_SDM
    u32 agc = 0, agcctl, memclk;
    u32 clkctrladdr;
    u32 rf = ecrnx_plat_get_rf(ecrnx_plat);
    u8 agc_ver;

    switch (rf) {
        case MDM_PHY_CONFIG_TRIDENT:
            agc = AGC_ECRNXAGCCNTL_ADDR;
            break;
        case MDM_PHY_CONFIG_CATAXIA:
        case MDM_PHY_CONFIG_KARST:
            agc = RIU_ECRNXAGCCNTL_ADDR;
            break;
        default:
            return -1;
    }

    agc_ver = ecrnx_plat_get_agc_load_version(ecrnx_plat, rf, &clkctrladdr);

    ecrnx_plat_stop_agcfsm(ecrnx_plat, agc, &agcctl, &memclk, agc_ver, clkctrladdr);

    ret = ecrnx_plat_bin_fw_upload(ecrnx_plat,
                              ECRNX_ADDR(ecrnx_plat, ECRNX_ADDR_SYSTEM, PHY_AGC_UCODE_ADDR),
                              ECRNX_AGC_FW_NAME);

    if (!ret && (agc_ver == 1)) {
        /* Run BIST to ensure that the AGC RAM was correctly loaded */
        ECRNX_REG_WRITE(BIT(28), ecrnx_plat, ECRNX_ADDR_SYSTEM,
                       RIU_ECRNXDYNAMICCONFIG_ADDR);
        while (ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM,
                             RIU_ECRNXDYNAMICCONFIG_ADDR) & BIT(28));

        if (!(ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM,
                            RIU_AGCMEMBISTSTAT_ADDR) & BIT(0))) {
            dev_err(ecrnx_platform_get_dev(ecrnx_plat),
                    "AGC RAM not loaded correctly 0x%08x\n",
                    ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM,
                                  RIU_AGCMEMSIGNATURESTAT_ADDR));
            ret = -EIO;
        }
    }

    ecrnx_plat_start_agcfsm(ecrnx_plat, agc, agcctl, memclk, agc_ver, clkctrladdr);

#endif
    return ret;
}
#endif

/**
 * ecrnx_ldpc_load() - Load LDPC RAM
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_ldpc_load(struct ecrnx_hw *ecrnx_hw)
{
#ifndef CONFIG_ECRNX_SDM
    struct ecrnx_plat *ecrnx_plat = ecrnx_hw->plat;
    u32 rf = ecrnx_plat_get_rf(ecrnx_plat);
    u32 phy_feat = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, MDM_HDMCONFIG_ADDR);
    u32 phy_vers = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, MDM_HDMVERSION_ADDR);

    if (((rf !=  MDM_PHY_CONFIG_KARST) && (rf !=  MDM_PHY_CONFIG_CATAXIA)) ||
        (phy_feat & (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT)) !=
        (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT)) {
        goto disable_ldpc;
    }
    if (__MDM_VERSION(phy_vers) > 30) {
        return 0;
    }

    if (ecrnx_plat_bin_fw_upload(ecrnx_plat,
                            ECRNX_ADDR(ecrnx_plat, ECRNX_ADDR_SYSTEM, PHY_LDPC_RAM_ADDR),
                            ECRNX_LDPC_RAM_NAME)) {
        goto disable_ldpc;
    }

    return 0;

  disable_ldpc:
    ecrnx_hw->mod_params->ldpc_on = false;

#endif /* CONFIG_ECRNX_SDM */
    return 0;
}
#endif
/**
 * ecrnx_plat_lmac_load() - Load FW code
 *
 * @ecrnx_plat: platform data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_plat_lmac_load(struct ecrnx_plat *ecrnx_plat)
{
    int ret;

    #ifdef CONFIG_ECRNX_TL4
    ret = ecrnx_plat_tl4_fw_upload(ecrnx_plat,
                                  ECRNX_ADDR(ecrnx_plat, ECRNX_ADDR_CPU, RAM_LMAC_FW_ADDR),
                                  ECRNX_MAC_FW_NAME);
    #else
    ret = ecrnx_plat_ihex_fw_upload(ecrnx_plat,
                                   ECRNX_ADDR(ecrnx_plat, ECRNX_ADDR_CPU, RAM_LMAC_FW_ADDR),
                                   ECRNX_MAC_FW_NAME);
    if (ret == -ENOENT)
    {
        ret = ecrnx_plat_bin_fw_upload(ecrnx_plat,
                                      ECRNX_ADDR(ecrnx_plat, ECRNX_ADDR_CPU, RAM_LMAC_FW_ADDR),
                                      ECRNX_MAC_FW_NAME2);
    }
    #endif

    return ret;
}
#endif

/**
 * ecrnx_rf_fw_load() - Load RF FW if any
 *
 * @ecrnx_hw: Main driver data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_plat_rf_fw_load(struct ecrnx_hw *ecrnx_hw)
{
#ifndef CONFIG_ECRNX_SDM
    struct ecrnx_plat *ecrnx_plat = ecrnx_hw->plat;
    u32 rf = ecrnx_plat_get_rf(ecrnx_plat);
    struct device *dev = ecrnx_platform_get_dev(ecrnx_plat);
    const struct firmware *fw;
    int err = 0;
    u8 const *file_data;
    int remain;
    u32 clkforce;
    u32 clkctrladdr;

    // Today only Cataxia has a FW to load
    if (rf !=  MDM_PHY_CONFIG_CATAXIA)
        return 0;

    err = request_firmware(&fw, ECRNX_CATAXIA_FW_NAME, dev);
    if (err)
    {
        dev_err(dev, "Make sure your board has up-to-date packages.");
        dev_err(dev, "Run \"sudo smart update\" \"sudo smart upgrade\" commands.\n");
        return err;
    }

    file_data = fw->data;
    remain = fw->size;

    // Get address of clock control register
    clkctrladdr = ecrnx_plat_get_clkctrl_addr(ecrnx_plat);

    // Force RC clock
    clkforce = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, clkctrladdr);
    ECRNX_REG_WRITE(clkforce | BIT(27), ecrnx_plat, ECRNX_ADDR_SYSTEM, clkctrladdr);
    mdelay(1);

    // Reset RC
    ECRNX_REG_WRITE(0x00003100, ecrnx_plat, ECRNX_ADDR_SYSTEM, RC_SYSTEM_CONFIGURATION_ADDR);
    mdelay(20);

    // Reset RF
    ECRNX_REG_WRITE(0x00133100, ecrnx_plat, ECRNX_ADDR_SYSTEM, RC_SYSTEM_CONFIGURATION_ADDR);
    mdelay(20);

    // Select trx 2 HB
    ECRNX_REG_WRITE(0x00103100, ecrnx_plat, ECRNX_ADDR_SYSTEM, RC_SYSTEM_CONFIGURATION_ADDR);
    mdelay(1);

    // Set ASP freeze
    ECRNX_REG_WRITE(0xC1010001, ecrnx_plat, ECRNX_ADDR_SYSTEM, RC_ACCES_TO_CATAXIA_REG_ADDR);
    mdelay(1);

    /* Walk through all the lines of the FW file */
    while (remain >= 10) {
        u32 data;

        if (sscanf(file_data, "0x%08X", &data) != 1)
        {
            // Corrupted FW file
            err = -1;
            break;
        }
        file_data += 11;
        remain -= 11;

        ECRNX_REG_WRITE(data, ecrnx_plat, ECRNX_ADDR_SYSTEM, RC_ACCES_TO_CATAXIA_REG_ADDR);
        udelay(50);
    }

    // Clear ASP freeze
    ECRNX_REG_WRITE(0xE0010011, ecrnx_plat, ECRNX_ADDR_SYSTEM, RC_ACCES_TO_CATAXIA_REG_ADDR);
    mdelay(1);

    // Unforce RC clock
    ECRNX_REG_WRITE(clkforce, ecrnx_plat, ECRNX_ADDR_SYSTEM, clkctrladdr);

    release_firmware(fw);

#endif /* CONFIG_ECRNX_SDM */
    return err;
}
#endif

/**
 * ecrnx_plat_mpif_sel() - Select the MPIF according to the FPGA signature
 *
 * @ecrnx_plat: platform data
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_plat_mpif_sel(struct ecrnx_plat *ecrnx_plat)
{
#ifndef CONFIG_ECRNX_SDM
    u32 regval;
    u32 type;

    /* Get the FPGA signature */
    regval = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, SYSCTRL_SIGNATURE_ADDR);
    type = __FPGA_TYPE(regval);

    /* Check if we need to switch to the old MPIF or not */
    if ((type != 0xCAFE) && (type != 0XC0CA) && (regval & 0xF) < 0x3)
    {
        /* A old FPGA A is used, so configure the FPGA B to use the old MPIF */
        ECRNX_REG_WRITE(0x3, ecrnx_plat, ECRNX_ADDR_SYSTEM, FPGAB_MPIF_SEL_ADDR);
    }
#endif
}
#endif

/**
 * ecrnx_platform_reset() - Reset the platform
 *
 * @ecrnx_plat: platform data
 */
#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_platform_reset(struct ecrnx_plat *ecrnx_plat)
{
    u32 regval;

    /* the doc states that SOFT implies FPGA_B_RESET
     * adding FPGA_B_RESET is clearer */
    ECRNX_REG_WRITE(SOFT_RESET | FPGA_B_RESET, ecrnx_plat,
                   ECRNX_ADDR_SYSTEM, SYSCTRL_MISC_CNTL_ADDR);
    msleep(100);

    regval = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM, SYSCTRL_MISC_CNTL_ADDR);

    if (regval & SOFT_RESET) {
        dev_err(ecrnx_platform_get_dev(ecrnx_plat), "reset: failed\n");
        return -EIO;
    }

    ECRNX_REG_WRITE(regval & ~FPGA_B_RESET, ecrnx_plat,
                   ECRNX_ADDR_SYSTEM, SYSCTRL_MISC_CNTL_ADDR);
    msleep(100);
    return 0;
}
#endif

/**
 * rwmx_platform_save_config() - Save hardware config before reload
 *
 * @ecrnx_plat: Pointer to platform data
 *
 * Return configuration registers values.
 */
#ifndef CONFIG_ECRNX_ESWIN
static void* ecrnx_term_save_config(struct ecrnx_plat *ecrnx_plat)
{
    const u32 *reg_list;
    u32 *reg_value, *res;
    int i, size = 0;

    if (ecrnx_plat->get_config_reg) {
        size = ecrnx_plat->get_config_reg(ecrnx_plat, &reg_list);
    }

    if (size <= 0)
        return NULL;

    res = kmalloc(sizeof(u32) * size, GFP_KERNEL);
    if (!res)
        return NULL;

    reg_value = res;
    for (i = 0; i < size; i++) {
        *reg_value++ = ECRNX_REG_READ(ecrnx_plat, ECRNX_ADDR_SYSTEM,
                                     *reg_list++);
    }

    return res;
}
#endif

/**
 * rwmx_platform_restore_config() - Restore hardware config after reload
 *
 * @ecrnx_plat: Pointer to platform data
 * @reg_value: Pointer of value to restore
 * (obtained with rwmx_platform_save_config())
 *
 * Restore configuration registers value.
 */
#ifndef CONFIG_ECRNX_ESWIN
static void ecrnx_term_restore_config(struct ecrnx_plat *ecrnx_plat,
                                     u32 *reg_value)
{
    const u32 *reg_list;
    int i, size = 0;

    if (!reg_value || !ecrnx_plat->get_config_reg)
        return;

    size = ecrnx_plat->get_config_reg(ecrnx_plat, &reg_list);

    for (i = 0; i < size; i++) {
        ECRNX_REG_WRITE(*reg_value++, ecrnx_plat, ECRNX_ADDR_SYSTEM,
                       *reg_list++);
    }
}
#endif

#ifndef CONFIG_ECRNX_ESWIN
static int ecrnx_check_fw_compatibility(struct ecrnx_hw *ecrnx_hw)
{
    int res = 0;

    struct ipc_shared_env_tag *shared = ecrnx_hw->ipc_env->shared;
    #ifdef CONFIG_ECRNX_SOFTMAC
    struct wiphy *wiphy = ecrnx_hw->hw->wiphy;
    #else //CONFIG_ECRNX_SOFTMAC
    struct wiphy *wiphy = ecrnx_hw->wiphy;
    #endif //CONFIG_ECRNX_SOFTMAC
    #ifdef CONFIG_ECRNX_OLD_IPC
    int ipc_shared_version = 10;
    #else //CONFIG_ECRNX_OLD_IPC
    int ipc_shared_version = 11;
    #endif //CONFIG_ECRNX_OLD_IPC

    if(shared->comp_info.ipc_shared_version != ipc_shared_version)
    {
        wiphy_err(wiphy, "Different versions of IPC shared version between driver and FW (%d != %d)\n ",
                  ipc_shared_version, shared->comp_info.ipc_shared_version);
        res = -1;
    }

    if(shared->comp_info.radarbuf_cnt != IPC_RADARBUF_CNT)
    {
        wiphy_err(wiphy, "Different number of host buffers available for Radar events handling "\
                  "between driver and FW (%d != %d)\n", IPC_RADARBUF_CNT,
                  shared->comp_info.radarbuf_cnt);
        res = -1;
    }

    if(shared->comp_info.unsuprxvecbuf_cnt != IPC_UNSUPRXVECBUF_CNT)
    {
        wiphy_err(wiphy, "Different number of host buffers available for unsupported Rx vectors "\
                  "handling between driver and FW (%d != %d)\n", IPC_UNSUPRXVECBUF_CNT,
                  shared->comp_info.unsuprxvecbuf_cnt);
        res = -1;
    }

    #ifdef CONFIG_ECRNX_FULLMAC
    if(shared->comp_info.rxdesc_cnt != IPC_RXDESC_CNT)
    {
        wiphy_err(wiphy, "Different number of shared descriptors available for Data RX handling "\
                  "between driver and FW (%d != %d)\n", IPC_RXDESC_CNT,
                  shared->comp_info.rxdesc_cnt);
        res = -1;
    }
    #endif /* CONFIG_ECRNX_FULLMAC */

    if(shared->comp_info.rxbuf_cnt != IPC_RXBUF_CNT)
    {
        wiphy_err(wiphy, "Different number of host buffers available for Data Rx handling "\
                  "between driver and FW (%d != %d)\n", IPC_RXBUF_CNT,
                  shared->comp_info.rxbuf_cnt);
        res = -1;
    }

    if(shared->comp_info.msge2a_buf_cnt != IPC_MSGE2A_BUF_CNT)
    {
        wiphy_err(wiphy, "Different number of host buffers available for Emb->App MSGs "\
                  "sending between driver and FW (%d != %d)\n", IPC_MSGE2A_BUF_CNT,
                  shared->comp_info.msge2a_buf_cnt);
        res = -1;
    }

    if(shared->comp_info.dbgbuf_cnt != IPC_DBGBUF_CNT)
    {
        wiphy_err(wiphy, "Different number of host buffers available for debug messages "\
                  "sending between driver and FW (%d != %d)\n", IPC_DBGBUF_CNT,
                  shared->comp_info.dbgbuf_cnt);
        res = -1;
    }

    if(shared->comp_info.bk_txq != NX_TXDESC_CNT0)
    {
        wiphy_err(wiphy, "Driver and FW have different sizes of BK TX queue (%d != %d)\n",
                  NX_TXDESC_CNT0, shared->comp_info.bk_txq);
        res = -1;
    }

    if(shared->comp_info.be_txq != NX_TXDESC_CNT1)
    {
        wiphy_err(wiphy, "Driver and FW have different sizes of BE TX queue (%d != %d)\n",
                  NX_TXDESC_CNT1, shared->comp_info.be_txq);
        res = -1;
    }

    if(shared->comp_info.vi_txq != NX_TXDESC_CNT2)
    {
        wiphy_err(wiphy, "Driver and FW have different sizes of VI TX queue (%d != %d)\n",
                  NX_TXDESC_CNT2, shared->comp_info.vi_txq);
        res = -1;
    }

    if(shared->comp_info.vo_txq != NX_TXDESC_CNT3)
    {
        wiphy_err(wiphy, "Driver and FW have different sizes of VO TX queue (%d != %d)\n",
                  NX_TXDESC_CNT3, shared->comp_info.vo_txq);
        res = -1;
    }

    #if NX_TXQ_CNT == 5
    if(shared->comp_info.bcn_txq != NX_TXDESC_CNT4)
    {
        wiphy_err(wiphy, "Driver and FW have different sizes of BCN TX queue (%d != %d)\n",
                NX_TXDESC_CNT4, shared->comp_info.bcn_txq);
        res = -1;
    }
    #else
    if (shared->comp_info.bcn_txq > 0)
    {
        wiphy_err(wiphy, "BCMC enabled in firmware but disabled in driver\n");
        res = -1;
    }
    #endif /* NX_TXQ_CNT == 5 */

    if(shared->comp_info.ipc_shared_size != sizeof(ipc_shared_env))
    {
        wiphy_err(wiphy, "Different sizes of IPC shared between driver and FW (%zd != %d)\n",
                  sizeof(ipc_shared_env), shared->comp_info.ipc_shared_size);
        res = -1;
    }

    if(shared->comp_info.msg_api != MSG_API_VER)
    {
        wiphy_err(wiphy, "Different supported message API versions between "\
                   "driver and FW (%d != %d)\n", MSG_API_VER, shared->comp_info.msg_api);
        res = -1;
    }

    return res;
}
#endif /* !CONFIG_ECRNX_ESWIN */

/**
 * ecrnx_platform_on() - Start the platform
 *
 * @ecrnx_hw: Main driver data
 * @config: Config to restore (NULL if nothing to restore)
 *
 * It starts the platform :
 * - load fw and ucodes
 * - initialize IPC
 * - boot the fw
 * - enable link communication/IRQ
 *
 * Called by 802.11 part
 */
int ecrnx_platform_on(struct ecrnx_hw *ecrnx_hw, void *config)
{
    u8 *shared_ram;
    int ret;
    
    ECRNX_DBG("%s entry!!", __func__);
    shared_ram = kzalloc(sizeof(struct ipc_shared_env_tag), GFP_KERNEL);
    if (!shared_ram)
        return -ENOMEM;

    if ((ret = ecrnx_ipc_init(ecrnx_hw, shared_ram)))
       return ret;

    ECRNX_DBG("%s exit!!", __func__);
    return 0;
}

/**
 * ecrnx_platform_off() - Stop the platform
 *
 * @ecrnx_hw: Main driver data
 * @config: Updated with pointer to config, to be able to restore it with
 * ecrnx_platform_on(). It's up to the caller to free the config. Set to NULL
 * if configuration is not needed.
 *
 * Called by 802.11 part
 */
void ecrnx_platform_off(struct ecrnx_hw *ecrnx_hw, void **config)
{
    ecrnx_ipc_deinit(ecrnx_hw);
#if defined(CONFIG_ECRNX_ESWIN_SDIO)
     ecrnx_sdio_deinit(ecrnx_hw);
#elif defined(CONFIG_ECRNX_ESWIN_USB)
    ecrnx_usb_deinit(ecrnx_hw);
#else
   #error "config error drv";
#endif
}

/**
 * ecrnx_platform_init() - Initialize the platform
 *
 * @ecrnx_plat: platform data (already updated by platform driver)
 * @platform_data: Pointer to store the main driver data pointer (aka ecrnx_hw)
 *                That will be set as driver data for the platform driver
 * Return: 0 on success, < 0 otherwise
 *
 * Called by the platform driver after it has been probed
 */
int ecrnx_platform_init(void *ecrnx_plat, void **platform_data)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
#if defined CONFIG_ECRNX_SOFTMAC
    return ecrnx_mac80211_init(ecrnx_plat, platform_data);
#elif defined CONFIG_ECRNX_FULLMAC
#ifdef CONFIG_ECRNX_WIFO_CAIL
	if (amt_mode == true) {
		return ecrnx_amt_init();
	}
	else
#endif
    return ecrnx_cfg80211_init(ecrnx_plat, platform_data);
#elif defined CONFIG_ECRNX_FHOST
    return ecrnx_fhost_init(ecrnx_plat, platform_data);
#endif
}

/**
 * ecrnx_platform_deinit() - Deinitialize the platform
 *
 * @ecrnx_hw: main driver data
 *
 * Called by the platform driver after it is removed
 */
void ecrnx_platform_deinit(struct ecrnx_hw *ecrnx_hw)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

#if defined CONFIG_ECRNX_SOFTMAC
    ecrnx_mac80211_deinit(ecrnx_hw);
#elif defined CONFIG_ECRNX_FULLMAC
#ifdef CONFIG_ECRNX_WIFO_CAIL
	if (amt_mode == true) {
		ecrnx_amt_deinit();
	}
	else
#endif
    ecrnx_cfg80211_deinit(ecrnx_hw);
#elif defined CONFIG_ECRNX_FHOST
    ecrnx_fhost_deinit(ecrnx_hw);
#endif
}


/**
 * ecrnx_platform_register_drv() - Register all possible platform drivers
 */
int ecrnx_platform_register_drv(void)
{
#if defined(CONFIG_ECRNX_ESWIN_SDIO)
    return ecrnx_sdio_register_drv();
#elif defined(CONFIG_ECRNX_ESWIN_USB)
    return ecrnx_usb_register_drv();
#else
    #error "config error drv"
#endif
}


/**
 * ecrnx_platform_unregister_drv() - Unegister all platform drivers
 */
void ecrnx_platform_unregister_drv(void)
{
#if defined(CONFIG_ECRNX_ESWIN_SDIO)
    return ecrnx_sdio_unregister_drv();
#elif defined(CONFIG_ECRNX_ESWIN_USB)
    return ecrnx_usb_unregister_drv();
#else
    #error "config error drv"
#endif
}


#ifndef CONFIG_ECRNX_SDM
MODULE_FIRMWARE(ECRNX_AGC_FW_NAME);
MODULE_FIRMWARE(ECRNX_FCU_FW_NAME);
MODULE_FIRMWARE(ECRNX_LDPC_RAM_NAME);
#endif
MODULE_FIRMWARE(ECRNX_MAC_FW_NAME);
#ifndef CONFIG_ECRNX_TL4
MODULE_FIRMWARE(ECRNX_MAC_FW_NAME2);
#endif
