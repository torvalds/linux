/*
 *
 * FocalTech ftxxxx TouchScreen driver.
 *
 * Copyright (c) 2012-2018, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_FOCALTECH_FLASH_H__
#define __LINUX_FOCALTECH_FLASH_H__

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "focaltech_core.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_CMD_RESET                               0x07
#define FTS_ROMBOOT_CMD_WRITE                       0xAE
#define FTS_ROMBOOT_CMD_START_APP                   0x08
#define FTS_DELAY_PRAMBOOT_START                    10
#define FTS_ROMBOOT_CMD_ECC                         0xCC

#define FTS_CMD_READ                                0x03
#define FTS_CMD_READ_DELAY                          1
#define FTS_CMD_READ_LEN                            4
#define FTS_CMD_FLASH_TYPE                          0x05
#define FTS_CMD_FLASH_MODE                          0x09
#define FLASH_MODE_WRITE_FLASH_VALUE                0x0A
#define FLASH_MODE_UPGRADE_VALUE                    0x0B
#define FLASH_MODE_LIC_VALUE                        0x0C
#define FLASH_MODE_PARAM_VALUE                      0x0D
#define FTS_CMD_ERASE_APP                           0x61
#define FTS_REASE_APP_DELAY                         1350
#define FTS_ERASE_SECTOR_DELAY                      60
#define FTS_RETRIES_REASE                           50
#define FTS_RETRIES_DELAY_REASE                     200
#define FTS_CMD_FLASH_STATUS                        0x6A
#define FTS_CMD_FLASH_STATUS_LEN                    2
#define FTS_CMD_FLASH_STATUS_NOP                    0x0000
#define FTS_CMD_FLASH_STATUS_ECC_OK                 0xF055
#define FTS_CMD_FLASH_STATUS_ERASE_OK               0xF0AA
#define FTS_CMD_FLASH_STATUS_WRITE_OK               0x1000
#define FTS_CMD_ECC_INIT                            0x64
#define FTS_CMD_ECC_CAL                             0x65
#define FTS_CMD_ECC_CAL_LEN                         6
#define FTS_RETRIES_ECC_CAL                         10
#define FTS_RETRIES_DELAY_ECC_CAL                   50
#define FTS_CMD_ECC_READ                            0x66
#define FTS_CMD_DATA_LEN                            0xB0
#define FTS_CMD_APP_DATA_LEN_INCELL                 0x7A
#define FTS_CMD_DATA_LEN_LEN                        4
#define FTS_CMD_WRITE                               0xBF
#define FTS_RETRIES_WRITE                           100
#define FTS_RETRIES_DELAY_WRITE                     1
#define FTS_CMD_WRITE_LEN                           6
#define FTS_DELAY_READ_ID                           20
#define FTS_DELAY_UPGRADE_RESET                     80
#define PRAMBOOT_MIN_SIZE                           0x120
#define PRAMBOOT_MAX_SIZE                           (64*1024)
#define FTS_FLASH_PACKET_LENGTH                     32     /* max=128 */
#define FTS_MAX_LEN_ECC_CALC                        0xFFFE /* must be even */
#define FTS_MIN_LEN                                 0x120
#define FTS_MAX_LEN_FILE                            (128 * 1024)
#define FTS_MAX_LEN_APP                             (64 * 1024)
#define FTS_MAX_LEN_SECTOR                          (4 * 1024)
#define FTS_CONIFG_VENDORID_OFF                     0x04
#define FTS_CONIFG_MODULEID_OFF                     0x1E
#define FTS_CONIFG_PROJECTID_OFF                    0x20
#define FTS_APPINFO_OFF                             0x100
#define FTS_APPINFO_APPLEN_OFF                      0x00
#define FTS_APPINFO_APPLEN2_OFF                     0x12
#define FTS_REG_UPGRADE                             0xFC
#define FTS_UPGRADE_AA                              0xAA
#define FTS_UPGRADE_55                              0x55
#define FTS_DELAY_FC_AA                             10
#define FTS_UPGRADE_LOOP                            30
#define FTS_HEADER_LEN                              32
#define FTS_FW_BIN_FILEPATH                         "/sdcard/"
#define FTS_FW_IDE_SIG                              "IDE_"
#define FTS_FW_IDE_SIG_LEN                          4

#define FTS_ROMBOOT_CMD_ECC_NEW_LEN                 7
#define FTS_ROMBOOT_CMD_ECC_FINISH                  0xCE
#define FTS_CMD_READ_ECC                            0xCD
#define AL2_FCS_COEF                ((1 << 15) + (1 << 10) + (1 << 3))

enum FW_STATUS {
    FTS_RUN_IN_ERROR,
    FTS_RUN_IN_APP,
    FTS_RUN_IN_ROM,
    FTS_RUN_IN_PRAM,
    FTS_RUN_IN_BOOTLOADER,
};

enum FW_FLASH_MODE {
    FLASH_MODE_APP,
    FLASH_MODE_LIC,
    FLASH_MODE_PARAM,
    FLASH_MODE_ALL,
};

enum ECC_CHECK_MODE {
    ECC_CHECK_MODE_XOR,
    ECC_CHECK_MODE_CRC16,
};

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
/* IC info */
struct upgrade_func {
    u64 ctype[FTX_MAX_COMPATIBLE_TYPE];
    int newmode;
    u32 fwveroff;
    u32 fwcfgoff;
    u32 appoff;
    u32 licoff;
    u32 paramcfgoff;
    u32 paramcfgveroff;
    u32 paramcfg2off;
    int ecc_check_mode;
    bool read_boot_id_need_reset;
    bool hid_supported;
    bool pramboot_supported;
    u8 *pramboot;
    u32 pb_length;
    int (*init)(void);
    int (*upgrade)(struct i2c_client *, u8 *, u32);
    int (*get_hlic_ver)(u8 *);
    int (*lic_upgrade)(struct i2c_client *, u8 *, u32);
    int (*param_upgrade)(struct i2c_client *, u8 *, u32);
    int (*force_upgrade)(struct i2c_client *, u8 *, u32);
};

struct fts_upgrade {
    u8 *fw;
    u32 fw_length;
    u8 *lic;
    u32 lic_length;
    struct upgrade_func *func;
};

struct upgrade_fw {
    u16 vendor_id;
    u8 *fw_file;
    u32 fw_len;
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
extern struct fts_upgrade *fwupgrade;
extern struct upgrade_func upgrade_func_ft8006m;

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
bool fts_fwupg_check_fw_valid(struct i2c_client *client);
int fts_fwupg_get_boot_state(struct i2c_client *client, enum FW_STATUS *fw_sts);
bool fts_fwupg_check_state(struct i2c_client *client, enum FW_STATUS rstate);
int fts_fwupg_reset_in_boot(struct i2c_client *client);
int fts_fwupg_reset_to_boot(struct i2c_client *client);
int fts_fwupg_reset_to_romboot(struct i2c_client *client);
int fts_fwupg_enter_into_boot(struct i2c_client *client);
int fts_fwupg_erase(struct i2c_client *client, u32 delay);
int fts_fwupg_ecc_cal(struct i2c_client *client, u32 saddr, u32 len);
int fts_flash_write_buf(struct i2c_client *client, u32 saddr, u8 *buf, u32 len, u32 delay);
int fts_fwupg_upgrade(struct i2c_client *client, struct fts_upgrade *upg);
u16 fts_crc16_calc_host(u8 *pbuf, u16 length);
#endif
