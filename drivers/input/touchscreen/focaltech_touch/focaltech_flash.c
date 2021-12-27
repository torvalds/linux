/*
 *
 * FocalTech fts TouchScreen driver.
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

/*****************************************************************************
*
* File Name: focaltech_flash.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "focaltech_core.h"
#include "focaltech_flash.h"

#define vfs_read(fp, buf, len, pos) kernel_read(fp, buf, len, pos)
/*****************************************************************************
* Static variables
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
/* Upgrade FW/PRAMBOOT/LCD CFG */
u8 fw_file[] = {
#include FTS_UPGRADE_FW_FILE
};

u8 fw_file2[] = {
#include FTS_UPGRADE_FW2_FILE
};

u8 fw_file3[] = {
#include FTS_UPGRADE_FW3_FILE
};

struct upgrade_fw fw_list[] = {
    {FTS_VENDOR_ID, fw_file, sizeof(fw_file)},
    {FTS_VENDOR_ID2, fw_file2, sizeof(fw_file2)},
    {FTS_VENDOR_ID3, fw_file3, sizeof(fw_file3)},
};

struct upgrade_func *upgrade_func_list[] = {
    &upgrade_func_ft8006m,
};

struct fts_upgrade *fwupgrade = NULL;

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
u16 fts_crc16_calc_host(u8 *pbuf, u16 length)
{
    u16 ecc = 0;
    u16 i = 0;
    u16 j = 0;

    for ( i = 0; i < length; i += 2 ) {
        ecc ^= ((pbuf[i] << 8) | (pbuf[i + 1]));
        for (j = 0; j < 16; j ++) {
            if (ecc & 0x01)
                ecc = (u16)((ecc >> 1) ^ AL2_FCS_COEF);
            else
                ecc >>= 1;
        }
    }

    return ecc;
}

static u16 fts_pram_ecc_calc_host(u8 *pbuf, u16 length)
{
    return fts_crc16_calc_host(pbuf, length);
}


/************************************************************************
 * fts_pram_ecc_cal - Calculate and get pramboot ecc
 *
 * return pramboot ecc of tp if success, otherwise return error code
 ***********************************************************************/
static int fts_pram_ecc_cal_algo(
    struct i2c_client *client,
    u32 start_addr,
    u32 ecc_length)
{
    int ret = 0;
    int i = 0;
    int ecc = 0;
    u8 val[2] = { 0 };
    u8 cmd[FTS_ROMBOOT_CMD_ECC_NEW_LEN] = { 0 };

    FTS_INFO("read out pramboot checksum");
    cmd[0] = FTS_ROMBOOT_CMD_ECC;
    cmd[1] = BYTE_OFF_16(start_addr);
    cmd[2] = BYTE_OFF_8(start_addr);
    cmd[3] = BYTE_OFF_0(start_addr);
    cmd[4] = BYTE_OFF_16(ecc_length);
    cmd[5] = BYTE_OFF_8(ecc_length);
    cmd[6] = BYTE_OFF_0(ecc_length);
    ret = fts_i2c_write(client, cmd, FTS_ROMBOOT_CMD_ECC_NEW_LEN);
    if (ret < 0) {
        FTS_ERROR("write pramboot ecc cal cmd fail");
        return ret;
    }

    cmd[0] = FTS_ROMBOOT_CMD_ECC_FINISH;
    for (i = 0; i < 100; i++) {
        msleep(1);
        ret = fts_i2c_read(client, cmd, 1, val, 1);
        if (ret < 0) {
            FTS_ERROR("ecc_finish read cmd fail");
            return ret;
        }
        if (0 == val[0])
            break;
    }
    if (i >= 100) {
        FTS_ERROR("wait ecc finish fail");
        return -EIO;
    }

    cmd[0] = FTS_CMD_READ_ECC;
    ret = fts_i2c_read(client, cmd, 1, val, 2);
    if (ret < 0) {
        FTS_ERROR("read pramboot ecc fail");
        return ret;
    }

    ecc = ((u16)(val[0] << 8) + val[1]) & 0x0000FFFF;
    return ecc;
}

static int fts_pram_ecc_cal_xor(struct i2c_client *client)
{
    int ret = 0;
    u8 reg_val = 0;

    FTS_INFO("read out pramboot checksum");

    ret = fts_i2c_read_reg(client, FTS_ROMBOOT_CMD_ECC, &reg_val);
    if (ret < 0) {
        FTS_ERROR("read pramboot ecc fail");
        return ret;
    }

    return (int)reg_val;
}

static int fts_pram_ecc_cal(struct i2c_client *client, u32 saddr, u32 len)
{
    if ((NULL == fwupgrade) || (NULL == fwupgrade->func)) {
        FTS_ERROR("fwupgrade/func is null");
        return -EINVAL;
    }

    if (fwupgrade->func->newmode) {
        return fts_pram_ecc_cal_algo(client, saddr, len);
    } else {
        return fts_pram_ecc_cal_xor(client);
    }
}

/************************************************************************
 * fts_pram_write_buf - write pramboot data and calculate ecc
 *
 * return pramboot ecc of host if success, otherwise return error code
 ***********************************************************************/
static int fts_pram_write_buf(struct i2c_client *client, u8 *buf, u32 len)
{
    int ret = 0;
    u32 i = 0;
    u32 j = 0;
    u32 offset = 0;
    u32 remainder = 0;
    u32 packet_number;
    u32 packet_len = 0;
    u8 packet_buf[FTS_FLASH_PACKET_LENGTH + FTS_CMD_WRITE_LEN] = { 0 };
    u8 ecc_tmp = 0;
    int ecc_in_host = 0;

    FTS_INFO("write pramboot to pram");
    if ((NULL == fwupgrade) || (NULL == fwupgrade->func)) {
        FTS_ERROR("fwupgrade/func is null");
        return -EINVAL;
    }

    if (NULL == buf) {
        FTS_ERROR("pramboot buf is null");
        return -EINVAL;
    }

    FTS_INFO("pramboot len=%d", len);
    if ((len < PRAMBOOT_MIN_SIZE) || (len > PRAMBOOT_MAX_SIZE)) {
        FTS_ERROR("pramboot length(%d) fail", len);
        return -EINVAL;
    }

    packet_number = len / FTS_FLASH_PACKET_LENGTH;
    remainder = len % FTS_FLASH_PACKET_LENGTH;
    if (remainder > 0)
        packet_number++;
    packet_len = FTS_FLASH_PACKET_LENGTH;

    packet_buf[0] = FTS_ROMBOOT_CMD_WRITE;
    for (i = 0; i < packet_number; i++) {
        offset = i * FTS_FLASH_PACKET_LENGTH;
        packet_buf[1] = BYTE_OFF_16(offset);
        packet_buf[2] = BYTE_OFF_8(offset);
        packet_buf[3] = BYTE_OFF_0(offset);

        /* last packet */
        if ((i == (packet_number - 1)) && remainder)
            packet_len = remainder;

        packet_buf[4] = BYTE_OFF_8(packet_len);
        packet_buf[5] = BYTE_OFF_0(packet_len);

        for (j = 0; j < packet_len; j++) {
            packet_buf[FTS_CMD_WRITE_LEN + j] = buf[offset + j];
            if (!fwupgrade->func->newmode) {
                ecc_tmp ^= packet_buf[FTS_CMD_WRITE_LEN + j];
            }
        }

        ret = fts_i2c_write(client, packet_buf, packet_len + FTS_CMD_WRITE_LEN);
        if (ret < 0) {
            FTS_ERROR("pramboot write data(%d) fail", i);
            return ret;
        }
    }

    if (fwupgrade->func->newmode) {
        ecc_in_host = (int)fts_pram_ecc_calc_host(buf, len);
    } else {
        ecc_in_host = (int)ecc_tmp;
    }

    return ecc_in_host;
}

/************************************************************************
 * fts_pram_start - remap to start pramboot
 *
 * return 0 if success, otherwise return error code
 ***********************************************************************/
static int fts_pram_start(struct i2c_client *client)
{
    u8 cmd = FTS_ROMBOOT_CMD_START_APP;
    int ret = 0;

    FTS_INFO("remap to start pramboot");

    ret = fts_i2c_write(client, &cmd, 1);
    if (ret < 0) {
        FTS_ERROR("write start pram cmd fail");
        return ret;
    }
    msleep(FTS_DELAY_PRAMBOOT_START);

    return 0;
}

/************************************************************************
 * fts_pram_write_remap - write pramboot to pram and start pramboot
 *
 * return 0 if success, otherwise return error code
***********************************************************************/
static int fts_pram_write_remap(struct i2c_client *client)
{
    int ret = 0;
    int ecc_in_host = 0;
    int ecc_in_tp = 0;
    u8 *pb_buf = NULL;
    u32 pb_len = 0;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("write pram and remap");

    if (!upg || !upg->func || !upg->func->pramboot) {
        FTS_ERROR("upgrade/pramboot is null");
        return -EINVAL;
    }

    if (upg->func->pb_length < FTS_MIN_LEN) {
        FTS_ERROR("pramboot length(%d) fail", upg->func->pb_length);
        return -EINVAL;
    }

    pb_buf = upg->func->pramboot;
    pb_len = upg->func->pb_length;

    /* write pramboot to pram */
    ecc_in_host = fts_pram_write_buf(client, pb_buf, pb_len);
    if (ecc_in_host < 0) {
        FTS_ERROR( "write pramboot fail");
        return ecc_in_host;
    }

    /* read out checksum */
    ecc_in_tp = fts_pram_ecc_cal(client, 0, pb_len);
    if (ecc_in_tp < 0) {
        FTS_ERROR( "read pramboot ecc fail");
        return ecc_in_tp;
    }

    FTS_INFO("pram ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
    /*  pramboot checksum != fw checksum, upgrade fail */
    if (ecc_in_host != ecc_in_tp) {
        FTS_ERROR("pramboot ecc check fail");
        return -EIO;
    }

    /*start pram*/
    ret = fts_pram_start(client);
    if (ret < 0) {
        FTS_ERROR("pram start fail");
        return ret;
    }

    return 0;
}

/************************************************************************
 * fts_pram_init - initialize pramboot
 *
 * return 0 if success, otherwise return error code
***********************************************************************/
static int fts_pram_init(struct i2c_client *client)
{
    int ret = 0;
    u8 reg_val = 0;
    u8 wbuf[3] = { 0 };

    FTS_INFO("pramboot initialization");

    /* read flash ID */
    wbuf[0] = FTS_CMD_FLASH_TYPE;
    ret = fts_i2c_read(client, wbuf, 1, &reg_val, 1);
    if (ret < 0) {
        FTS_ERROR("read flash type fail");
        return ret;
    }

    /* set flash clk */
    wbuf[0] = FTS_CMD_FLASH_TYPE;
    wbuf[1] = reg_val;
    wbuf[2] = 0x00;
    ret = fts_i2c_write(client, wbuf, 3);
    if (ret < 0) {
        FTS_ERROR("write flash type fail");
        return ret;
    }

    return 0;
}

/************************************************************************
* Name: fts_pram_write_init
* Brief: wirte pramboot to pram and initialize
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
static int fts_pram_write_init(struct i2c_client *client)
{
    int ret = 0;
    bool state = 0;
    enum FW_STATUS status = FTS_RUN_IN_ERROR;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("**********pram write and init**********");
    if ((NULL == upg) || (NULL == upg->func)) {
        FTS_ERROR("upgrade/func is null");
        return -EINVAL;
    }

    if (!upg->func->pramboot_supported) {
        FTS_ERROR("ic not support pram");
        return -EINVAL;
    }

    FTS_DEBUG("check whether tp is in romboot or not ");
    /* need reset to romboot when non-romboot state */
    fts_fwupg_get_boot_state(client, &status);
    if (status != FTS_RUN_IN_ROM) {
        if (FTS_RUN_IN_PRAM == status) {
            FTS_INFO("tp is in pramboot, need send reset cmd before upgrade");
            ret = fts_pram_init(client);
            if (ret < 0) {
                FTS_ERROR("pramboot(before) init fail");
                return ret;
            }
        }

        FTS_INFO("tp isn't in romboot, need send reset to romboot");
        ret = fts_fwupg_reset_to_romboot(client);
        if (ret < 0) {
            FTS_ERROR("reset to romboot fail");
            return ret;
        }
    }

    /* check the length of the pramboot */
    ret = fts_pram_write_remap(client);
    if (ret < 0) {
        FTS_ERROR("pram write fail, ret=%d", ret);
        return ret;
    }

    FTS_DEBUG("after write pramboot, confirm run in pramboot");
    state = fts_fwupg_check_state(client, FTS_RUN_IN_PRAM);
    if (!state) {
        FTS_ERROR("not in pramboot");
        return -EIO;
    }

    ret = fts_pram_init(client);
    if (ret < 0) {
        FTS_ERROR("pramboot init fail");
        return ret;
    }

    return 0;
}

/************************************************************************
* Name: fts_fwupg_check_fw_valid
* Brief: check fw in tp is valid or not
* Input:
* Output:
* Return: return true if fw is valid, otherwise return false
***********************************************************************/
bool fts_fwupg_check_fw_valid(struct i2c_client *client)
{
    int ret = 0;

    ret = fts_wait_tp_to_valid(client);
    if (ret < 0) {
        FTS_INFO("tp fw invaild");
        return false;
    }

    FTS_INFO("tp fw vaild");
    return true;
}

/************************************************************************
* Name: fts_fwupg_get_boot_state
* Brief: read boot id(rom/pram/bootloader), confirm boot environment
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
int fts_fwupg_get_boot_state(struct i2c_client *client, enum FW_STATUS *fw_sts)
{
    int ret = 0;
    u8 cmd[4] = { 0 };
    u32 cmd_len = 0;
    u8 val[2] = { 0 };
    struct ft_chip_t ids = fts_data->ic_info.ids;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("**********read boot id**********");
    if ((NULL == fw_sts) || (NULL == upg) || (NULL == upg->func)) {
        FTS_ERROR("upgrade/func/fw_sts is null");
        return -EINVAL;
    }

    if (upg->func->hid_supported)
        fts_i2c_hid2std(client);

    cmd[0] = FTS_CMD_START1;
    cmd[1] = FTS_CMD_START2;
    ret = fts_i2c_write(client, cmd, 2);
    if (ret < 0) {
        FTS_ERROR("write 55 aa cmd fail");
        return ret;
    }

    msleep(FTS_CMD_START_DELAY);
    cmd[0] = FTS_CMD_READ_ID;
    cmd[1] = cmd[2] = cmd[3] = 0x00;
    if (fts_data->ic_info.is_incell)
        cmd_len = FTS_CMD_READ_ID_LEN_INCELL;
    else
        cmd_len = FTS_CMD_READ_ID_LEN;
    ret = fts_i2c_read(client, cmd, cmd_len, val, 2);
    if (ret < 0) {
        FTS_ERROR("write 90 cmd fail");
        return ret;
    }

    FTS_INFO("read boot id:0x%02x%02x", val[0], val[1]);
    if ((val[0] == ids.rom_idh) && (val[1] == ids.rom_idl)) {
        FTS_INFO("tp run in romboot");
        *fw_sts = FTS_RUN_IN_ROM;
    } else if ((val[0] == ids.pb_idh) && (val[1] == ids.pb_idl)) {
        FTS_INFO("tp run in pramboot");
        *fw_sts = FTS_RUN_IN_PRAM;
    } else if ((val[0] == ids.bl_idh) && (val[1] == ids.bl_idl)) {
        FTS_INFO("tp run in bootloader");
        *fw_sts = FTS_RUN_IN_BOOTLOADER;
    }

    return 0;
}

/************************************************************************
* Name: fts_fwupg_check_state
* Brief: confirm tp run in romboot/pramboot/bootloader
* Input:
* Output:
* Return: return true if state is match, otherwise return false
***********************************************************************/
bool fts_fwupg_check_state(struct i2c_client *client, enum FW_STATUS rstate)
{
    int i = 0;
    enum FW_STATUS cstate = FTS_RUN_IN_ERROR;

    for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
        fts_fwupg_get_boot_state(client, &cstate);
        /*         FTS_DEBUG("fw state=%d, retries=%d", cstate, i); */
        if (cstate == rstate)
            return true;
        msleep(FTS_DELAY_READ_ID);
    }

    return false;
}

/************************************************************************
* Name: fts_fwupg_reset_in_boot
* Brief: RST CMD(07), reset to romboot(bootloader) in boot environment
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
int fts_fwupg_reset_in_boot(struct i2c_client *client)
{
    int ret = 0;
    u8 cmd = FTS_CMD_RESET;

    FTS_INFO("reset in boot environment");
    ret = fts_i2c_write(client, &cmd, 1);
    if (ret < 0) {
        FTS_ERROR("pram/rom/bootloader reset cmd write fail");
        return ret;
    }

    msleep(FTS_DELAY_UPGRADE_RESET);
    return 0;
}

/************************************************************************
* Name: fts_fwupg_reset_to_boot
* Brief: reset to boot environment
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
int fts_fwupg_reset_to_boot(struct i2c_client *client)
{
    int ret = 0;

    FTS_INFO("send 0xAA and 0x55 to FW, reset to boot environment");

    ret = fts_i2c_write_reg(client, FTS_REG_UPGRADE, FTS_UPGRADE_AA);
    if (ret < 0) {
        FTS_ERROR("write FC=0xAA fail");
        return ret;
    }
    msleep(FTS_DELAY_FC_AA);

    ret = fts_i2c_write_reg(client, FTS_REG_UPGRADE, FTS_UPGRADE_55);
    if (ret < 0) {
        FTS_ERROR("write FC=0x55 fail");
        return ret;
    }

    msleep(FTS_DELAY_UPGRADE_RESET);
    return 0;
}

/************************************************************************
* Name: fts_fwupg_reset_to_romboot
* Brief: reset to romboot, to load pramboot
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
int fts_fwupg_reset_to_romboot(struct i2c_client *client)
{
    int ret = 0;
    int i = 0;
    u8 cmd = FTS_CMD_RESET;
    enum FW_STATUS state = FTS_RUN_IN_ERROR;

    ret = fts_i2c_write(client, &cmd, 1);
    if (ret < 0) {
        FTS_ERROR("pram/rom/bootloader reset cmd write fail");
        return ret;
    }
    mdelay(10);

    for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
        fts_fwupg_get_boot_state(client, &state);
        if (FTS_RUN_IN_ROM == state)
            break;
        mdelay(5);
    }
    if (i >= FTS_UPGRADE_LOOP) {
        FTS_ERROR("reset to romboot fail");
        return -EIO;
    }

    return 0;
}

/************************************************************************
* Name: fts_fwupg_enter_into_boot
* Brief: enter into boot environment, ready for upgrade
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
int fts_fwupg_enter_into_boot(struct i2c_client *client)
{
    int ret = 0;
    bool fwvalid = false;
    bool state = false;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("***********enter into pramboot/bootloader***********");
    if ((NULL == upg) || (NULL == upg->func)) {
        FTS_ERROR("upgrade/func is null");
        return -EINVAL;
    }

    fwvalid = fts_fwupg_check_fw_valid(client);
    if (fwvalid) {
        ret = fts_fwupg_reset_to_boot(client);
        if (ret < 0) {
            FTS_ERROR("enter into romboot/bootloader fail");
            return ret;
        }
    } else if (upg->func->read_boot_id_need_reset) {
        ret = fts_fwupg_reset_in_boot(client);
        if (ret < 0) {
            FTS_ERROR("reset before read boot id when fw invalid fail");
            return ret;
        }
    }

    if (upg->func->pramboot_supported) {
        FTS_INFO("pram supported, write pramboot and init");
        /* pramboot */
        ret = fts_pram_write_init(client);
        if (ret < 0) {
            FTS_ERROR("pram write_init fail");
            return ret;
        }
    } else {
        FTS_DEBUG("pram not supported, confirm in bootloader");
        /* bootloader */
        state = fts_fwupg_check_state(client, FTS_RUN_IN_BOOTLOADER);
        if (!state) {
            FTS_ERROR("fw not in bootloader, fail");
            return -EIO;
        }
    }

    return 0;
}

/************************************************************************
 * Name: fts_fwupg_check_flash_status
 * Brief: read status from tp
 * Input: flash_status: correct value from tp
 *        retries: read retry times
 *        retries_delay: retry delay
 * Output:
 * Return: return true if flash status check pass, otherwise return false
***********************************************************************/
static bool fts_fwupg_check_flash_status(
    struct i2c_client *client,
    u16 flash_status,
    int retries,
    int retries_delay)
{
    int i = 0;
    u8 cmd = 0;
    u8 val[FTS_CMD_FLASH_STATUS_LEN] = { 0 };
    u16 read_status = 0;

    for (i = 0; i < retries; i++) {
        cmd = FTS_CMD_FLASH_STATUS;
        fts_i2c_read(client, &cmd , 1, val, FTS_CMD_FLASH_STATUS_LEN);
        read_status = (((u16)val[0]) << 8) + val[1];
        if (flash_status == read_status) {
            /* FTS_DEBUG("[UPGRADE]flash status ok"); */
            return true;
        }
        /* FTS_DEBUG("flash status fail,ok:%04x read:%04x, retries:%d", flash_status, read_status, i); */
        msleep(retries_delay);
    }

    return false;
}

/************************************************************************
 * Name: fts_fwupg_erase
 * Brief: erase flash area
 * Input: delay - delay after erase
 * Output:
 * Return: return 0 if success, otherwise return error code
 ***********************************************************************/
int fts_fwupg_erase(struct i2c_client *client, u32 delay)
{
    int ret = 0;
    u8 cmd = 0;
    bool flag = false;

    FTS_INFO("**********erase now**********");

    /*send to erase flash*/
    cmd = FTS_CMD_ERASE_APP;
    ret = fts_i2c_write(client, &cmd, 1);
    if (ret < 0) {
        FTS_ERROR("erase cmd fail");
        return ret;
    }
    msleep(delay);

    /* read status 0xF0AA: success */
    flag = fts_fwupg_check_flash_status(client, FTS_CMD_FLASH_STATUS_ERASE_OK,
                                        FTS_RETRIES_REASE, FTS_RETRIES_DELAY_REASE);
    if (!flag) {
        FTS_ERROR("ecc flash status check fail");
        return -EIO;
    }

    return 0;
}

/************************************************************************
 * Name: fts_fwupg_ecc_cal
 * Brief: calculate and get ecc from tp
 * Input: saddr - start address need calculate ecc
 *        len - length need calculate ecc
 * Output:
 * Return: return data ecc of tp if success, otherwise return error code
 ***********************************************************************/
int fts_fwupg_ecc_cal(struct i2c_client *client, u32 saddr, u32 len)
{
    int ret = 0;
    u32 i = 0;
    u8 wbuf[FTS_CMD_ECC_CAL_LEN] = { 0 };
    u8 val[FTS_CMD_FLASH_STATUS_LEN] = { 0 };
    int ecc = 0;
    int ecc_len = 0;
    u32 packet_num = 0;
    u32 packet_len = 0;
    u32 remainder = 0;
    u32 addr = 0;
    u32 offset = 0;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO( "**********read out checksum**********");
    if ((NULL == upg) || (NULL == upg->func)) {
        FTS_ERROR("upgrade/func is null");
        return -EINVAL;
    }

    /* check sum init */
    wbuf[0] = FTS_CMD_ECC_INIT;
    ret = fts_i2c_write(client, wbuf, 1);
    if (ret < 0) {
        FTS_ERROR("ecc init cmd write fail");
        return ret;
    }

    packet_num = len / FTS_MAX_LEN_ECC_CALC;
    remainder = len % FTS_MAX_LEN_ECC_CALC;
    if (remainder)
        packet_num++;
    packet_len = FTS_MAX_LEN_ECC_CALC;
    FTS_INFO("ecc calc num:%d, remainder:%d", packet_num, remainder);

    /* send commond to start checksum */
    wbuf[0] = FTS_CMD_ECC_CAL;
    for (i = 0; i < packet_num; i++) {
        offset = FTS_MAX_LEN_ECC_CALC * i;
        addr = saddr + offset;
        wbuf[1] = BYTE_OFF_16(addr);
        wbuf[2] = BYTE_OFF_8(addr);
        wbuf[3] = BYTE_OFF_0(addr);

        if ((i == (packet_num - 1)) && remainder)
            packet_len = remainder;
        wbuf[4] = BYTE_OFF_8(packet_len);
        wbuf[5] = BYTE_OFF_0(packet_len);

        FTS_DEBUG("ecc calc startaddr:0x%04x, len:%d", addr, packet_len);
        ret = fts_i2c_write(client, wbuf, FTS_CMD_ECC_CAL_LEN);
        if (ret < 0) {
            FTS_ERROR("ecc calc cmd write fail");
            return ret;
        }

        msleep(packet_len / 256);

        /* read status if check sum is finished */
        ret = fts_fwupg_check_flash_status(client, FTS_CMD_FLASH_STATUS_ECC_OK,
                                           FTS_RETRIES_ECC_CAL, FTS_RETRIES_DELAY_ECC_CAL);
        if (ret < 0) {
            FTS_ERROR("ecc flash status read fail");
            return ret;
        }
    }

    ecc_len = 1;
    if (ECC_CHECK_MODE_CRC16 == upg->func->ecc_check_mode) {
        ecc_len = 2;
    }

    /* read out check sum */
    wbuf[0] = FTS_CMD_ECC_READ;
    ret = fts_i2c_read(client, wbuf, 1, val, ecc_len);
    if (ret < 0) {
        FTS_ERROR( "ecc read cmd write fail");
        return ret;
    }

    if (ECC_CHECK_MODE_CRC16 == upg->func->ecc_check_mode) {
        ecc = (int)((u16)(val[0] << 8) + val[1]);
    } else {
        ecc = (int)val[0];
    }

    return ecc;
}

/************************************************************************
 * Name: fts_flash_write_buf
 * Brief: write buf data to flash address
 * Input: saddr - start address data write to flash
 *        buf - data buffer
 *        len - data length
 *        delay - delay after write
 * Output:
 * Return: return data ecc of host if success, otherwise return error code
 ***********************************************************************/
int fts_flash_write_buf(
    struct i2c_client *client,
    u32 saddr,
    u8 *buf,
    u32 len,
    u32 delay)
{
    int ret = 0;
    u32 i = 0;
    u32 j = 0;
    u32 packet_number = 0;
    u32 packet_len = 0;
    u32 addr = 0;
    u32 offset = 0;
    u32 remainder = 0;
    u8 packet_buf[FTS_FLASH_PACKET_LENGTH + FTS_CMD_WRITE_LEN] = { 0 };
    u8 ecc_in_host = 0;
    u8 cmd = 0;
    u8 val[FTS_CMD_FLASH_STATUS_LEN] = { 0 };
    u16 read_status = 0;
    u16 wr_ok = 0;

    FTS_INFO( "**********write data to flash**********");

    if ((NULL == buf) || (0 == len)) {
        FTS_ERROR("buf is NULL or len is 0");
        return -EINVAL;
    }

    FTS_INFO("data buf start addr=0x%x, len=0x%x", saddr, len);
    packet_number = len / FTS_FLASH_PACKET_LENGTH;
    remainder = len % FTS_FLASH_PACKET_LENGTH;
    if (remainder > 0)
        packet_number++;
    packet_len = FTS_FLASH_PACKET_LENGTH;
    FTS_INFO("write data, num:%d remainder:%d", packet_number, remainder);

    packet_buf[0] = FTS_CMD_WRITE;
    for (i = 0; i < packet_number; i++) {
        offset = i * FTS_FLASH_PACKET_LENGTH;
        addr = saddr + offset;
        packet_buf[1] = BYTE_OFF_16(addr);
        packet_buf[2] = BYTE_OFF_8(addr);
        packet_buf[3] = BYTE_OFF_0(addr);

        /* last packet */
        if ((i == (packet_number - 1)) && remainder)
            packet_len = remainder;

        packet_buf[4] = BYTE_OFF_8(packet_len);
        packet_buf[5] = BYTE_OFF_0(packet_len);

        for (j = 0; j < packet_len; j++) {
            packet_buf[FTS_CMD_WRITE_LEN + j] = buf[offset + j];
            ecc_in_host ^= packet_buf[FTS_CMD_WRITE_LEN + j];
        }

        ret = fts_i2c_write(client, packet_buf, packet_len + FTS_CMD_WRITE_LEN);
        if (ret < 0) {
            FTS_ERROR("app write fail");
            return ret;
        }
        mdelay(delay);

        /* read status */
        wr_ok = FTS_CMD_FLASH_STATUS_WRITE_OK + addr / packet_len;
        for (j = 0; j < FTS_RETRIES_WRITE; j++) {
            cmd = FTS_CMD_FLASH_STATUS;
            fts_i2c_read(client, &cmd , 1, val, FTS_CMD_FLASH_STATUS_LEN);
            read_status = (((u16)val[0]) << 8) + val[1];
            /*  FTS_INFO("%x %x", wr_ok, read_status); */
            if (wr_ok == read_status) {
                break;
            }
            mdelay(FTS_RETRIES_DELAY_WRITE);
        }
    }

    return (int)ecc_in_host;
}

#if FTS_AUTO_UPGRADE_EN
/************************************************************************
 * Name: fts_flash_read_buf
 * Brief: read data from flash
 * Input: saddr - start address data write to flash
 *        buf - buffer to store data read from flash
 *        len - read length
 * Output:
 * Return: return 0 if success, otherwise return error code
 *
 * Warning: can't call this function directly, need call in boot environment
 ***********************************************************************/
static int fts_flash_read_buf(struct i2c_client *client, u32 saddr, u8 *buf, u32 len)
{
    int ret = 0;
    u32 i = 0;
    u32 packet_number = 0;
    u32 packet_len = 0;
    u32 addr = 0;
    u32 offset = 0;
    u32 remainder = 0;
    u8 wbuf[FTS_CMD_READ_LEN];

    if ((NULL == buf) || (0 == len)) {
        FTS_ERROR("buf is NULL or len is 0");
        return -EINVAL;
    }

    packet_number = len / FTS_FLASH_PACKET_LENGTH;
    remainder = len % FTS_FLASH_PACKET_LENGTH;
    if (remainder > 0) {
        packet_number++;
    }
    packet_len = FTS_FLASH_PACKET_LENGTH;
    FTS_INFO("read packet_number:%d, remainder:%d", packet_number, remainder);

    wbuf[0] = FTS_CMD_READ;
    for (i = 0; i < packet_number; i++) {
        offset = i * FTS_FLASH_PACKET_LENGTH;
        addr = saddr + offset;
        wbuf[1] = BYTE_OFF_16(addr);
        wbuf[2] = BYTE_OFF_8(addr);
        wbuf[3] = BYTE_OFF_0(addr);

        /* last packet */
        if ((i == (packet_number - 1)) && remainder)
            packet_len = remainder;

        ret = fts_i2c_write(client, wbuf, FTS_CMD_READ_LEN);
        if (ret < 0) {
            FTS_ERROR("pram/bootloader write 03 command fail");
            return ret;
        }

        msleep(FTS_CMD_READ_DELAY); /* must wait, otherwise read wrong data */
        ret = fts_i2c_read(client, NULL, 0, buf + offset, packet_len);
        if (ret < 0) {
            FTS_ERROR("pram/bootloader read 03 command fail");
            return ret;
        }
    }

    return 0;
}

/************************************************************************
 * Name: fts_flash_read
 * Brief:
 * Input:  addr  - address of flash
 *         len   - length of read
 * Output: buf   - data read from flash
 * Return: return 0 if success, otherwise return error code
 ***********************************************************************/
static int fts_flash_read(struct i2c_client *client, u32 addr, u8 *buf, u32 len)
{
    int ret = 0;

    FTS_INFO("***********read flash***********");

    if ((NULL == buf) || (0 == len)) {
        FTS_ERROR("buf is NULL or len is 0");
        return -EINVAL;
    }

    ret = fts_fwupg_enter_into_boot(client);
    if (ret < 0) {
        FTS_ERROR("enter into pramboot/bootloader fail");
        goto read_flash_err;
    }

    ret = fts_flash_read_buf(client, addr, buf, len);
    if (ret < 0) {
        FTS_ERROR("read flash fail");
        goto read_flash_err;
    }

read_flash_err:
    /* reset to normal boot */
    ret = fts_fwupg_reset_in_boot(client);
    if (ret < 0) {
        FTS_ERROR("reset to normal boot fail");
    }
    return ret;
}
#endif /* FTS_AUTO_UPGRADE_EN */

/************************************************************************
 * Name: fts_read_file
 * Brief:  read file
 * Input: file name
 * Output:
 * Return: return file len if succuss, otherwise return error code
 ***********************************************************************/
static int fts_read_file(char *file_name, u8 **file_buf)
{
    int ret = 0;
    char file_path[FILE_NAME_LENGTH] = { 0 };
    struct file *filp = NULL;
    struct inode *inode;
    mm_segment_t old_fs;
    loff_t pos;
    loff_t file_len = 0;

    if ((NULL == file_name) || (NULL == file_buf)) {
        FTS_ERROR("filename/filebuf is NULL");
        return -EINVAL;
    }

    snprintf(file_path, FILE_NAME_LENGTH, "%s%s", FTS_FW_BIN_FILEPATH, file_name);
    filp = filp_open(file_path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        FTS_ERROR("open %s file fail", file_path);
        return -ENOENT;
    }

#if 1
    inode = filp->f_inode;
#else
    /* reserved for linux earlier verion */
    inode = filp->f_dentry->d_inode;
#endif

    file_len = inode->i_size;
    *file_buf = (u8 *)vmalloc(file_len);
    if (NULL == *file_buf) {
        FTS_ERROR("file buf malloc fail");
        filp_close(filp, NULL);
        return -ENOMEM;
    }
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    ret = vfs_read(filp, *file_buf, file_len , &pos);
    if (ret < 0)
        FTS_ERROR("read file fail");
    FTS_INFO("file len:%d read len:%d pos:%d", (u32)file_len, ret, (u32)pos);
    filp_close(filp, NULL);
    set_fs(old_fs);

    return ret;
}

/************************************************************************
* Name: fts_upgrade_bin
* Brief:
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
int fts_upgrade_bin(struct i2c_client *client, char *fw_name, bool force)
{
    int ret = 0;
    u32 fw_file_len = 0;
    u8 *fw_file_buf = NULL;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("start upgrade with fw bin");
    if ((NULL == upg) || (NULL == upg->func)) {
        FTS_ERROR("upgrade/func is null");
        return -EINVAL;
    }

    ret = fts_read_file(fw_name, &fw_file_buf);
    if ((ret < 0) || (ret < FTS_MIN_LEN) || (ret > FTS_MAX_LEN_FILE)) {
        FTS_ERROR("read fw bin file(sdcard) fail, len:%d", ret);
        goto err_bin;
    }

    fw_file_len = ret;
    FTS_INFO("fw bin file len:%d", fw_file_len);
    if (force) {
        if (upg->func->force_upgrade) {
            ret = upg->func->force_upgrade(client, fw_file_buf, fw_file_len);
        } else {
            FTS_INFO("force_upgrade function is null, no upgrade");
            goto err_bin;
        }
    } else {
#if FTS_AUTO_LIC_UPGRADE_EN
        if (upg->func->lic_upgrade) {
            ret = upg->func->lic_upgrade(client, fw_file_buf, fw_file_len);
        } else {
            FTS_INFO("lic_upgrade function is null, no upgrade");
        }
#endif
        if (upg->func->upgrade) {
            ret = upg->func->upgrade(client, fw_file_buf, fw_file_len);
        } else {
            FTS_INFO("upgrade function is null, no upgrade");
        }
    }

    if (ret < 0) {
        FTS_ERROR("upgrade fw bin failed");
        fts_fwupg_reset_in_boot(client);
        goto err_bin;
    }

    FTS_INFO("upgrade fw bin success");

err_bin:
    if (fw_file_buf) {
        vfree(fw_file_buf);
        fw_file_buf = NULL;
    }
    return ret;
}

#if (FTS_AUTO_LIC_UPGRADE_EN && FTS_AUTO_UPGRADE_EN)
static int fts_lic_get_vid_in_tp(struct i2c_client *client, u16 *vid)
{
    int ret = 0;
    u8 val[2] = { 0 };

    if (NULL == vid) {
        FTS_ERROR("vid is NULL");
        return -EINVAL;
    }

    ret = fts_i2c_read_reg(client, FTS_REG_VENDOR_ID, &val[0]);
    if (fts_data->ic_info.is_incell)
        ret = fts_i2c_read_reg(client, FTS_REG_MODULE_ID, &val[1]);
    if (ret < 0) {
        FTS_ERROR("read vid from tp fail");
        return ret;
    }

    *vid = *(u16 *)val;
    return 0;
}

static int fts_lic_get_vid_in_host(u16 *vid)
{
    u8 val[2] = { 0 };
    u8 *licbuf = NULL;
    u32 conf_saddr = 0;
    struct fts_upgrade *upg = fwupgrade;

    if (!upg || !upg->func || !upg->lic || !vid) {
        FTS_ERROR("upgrade/func/get_hlic_ver/lic/vid is null");
        return -EINVAL;
    }

    if (upg->lic_length < FTS_MAX_LEN_SECTOR) {
        FTS_ERROR("lic length(%x) fail", upg->lic_length);
        return -EINVAL;
    }

    licbuf  = upg->lic;
    conf_saddr = upg->func->fwcfgoff;
    val[0] = licbuf[conf_saddr + FTS_CONIFG_VENDORID_OFF];
    if (fts_data->ic_info.is_incell)
        val[1] = licbuf[conf_saddr + FTS_CONIFG_MODULEID_OFF];

    *vid = *(u16 *)val;
    return 0;
}

static int fts_lic_get_ver_in_tp(struct i2c_client *client, u8 *ver)
{
    int ret = 0;

    if (NULL == ver) {
        FTS_ERROR("ver is NULL");
        return -EINVAL;
    }

    ret = fts_i2c_read_reg(client, FTS_REG_LIC_VER, ver);
    if (ret < 0) {
        FTS_ERROR("read lcd initcode ver from tp fail");
        return ret;
    }

    return 0;
}

static int fts_lic_get_ver_in_host(u8 *ver)
{
    int ret = 0;
    struct fts_upgrade *upg = fwupgrade;

    if (!upg || !upg->func || !upg->func->get_hlic_ver || !upg->lic) {
        FTS_ERROR("upgrade/func/get_hlic_ver/lic is null");
        return -EINVAL;
    }

    ret = upg->func->get_hlic_ver(upg->lic);
    if (ret < 0) {
        FTS_ERROR("get host lcd initial code version fail");
        return ret;
    }

    *ver = (u8)ret;
    return ret;
}

/* check if lcd init code need upgrade
* true-need  false-no need
*/
static bool fts_lic_need_upgrade(struct i2c_client *client)
{
    int ret = 0;
    u8 initcode_ver_in_tp = 0;
    u8 initcode_ver_in_host = 0;
    u16 vid_in_tp = 0;
    u16 vid_in_host = 0;
    bool fwvalid = false;

    fwvalid = fts_fwupg_check_fw_valid(client);
    if ( !fwvalid) {
        FTS_INFO("fw is invalid, no upgrade lcd init code");
        return false;
    }

    ret = fts_lic_get_vid_in_host(&vid_in_host);
    if (ret < 0) {
        FTS_ERROR("vendor id in host invalid");
        return false;
    }

    ret = fts_lic_get_vid_in_tp(client, &vid_in_tp);
    if (ret < 0) {
        FTS_ERROR("vendor id in tp invalid");
        return false;
    }

    FTS_DEBUG("vid in tp:0x%04x, host:0x%04x", vid_in_tp, vid_in_host);
    if (vid_in_tp != vid_in_host) {
        FTS_INFO("vendor id in tp&host are different, no upgrade lic");
        return false;
    }

    ret = fts_lic_get_ver_in_host(&initcode_ver_in_host);
    if (ret < 0) {
        FTS_ERROR("init code in host invalid");
        return false;
    }

    ret = fts_lic_get_ver_in_tp(client, &initcode_ver_in_tp);
    if (ret < 0) {
        FTS_ERROR("read reg0xE4 fail");
        return false;
    }

    FTS_DEBUG("lcd initial code version in tp:%x, host:%x",
              initcode_ver_in_tp, initcode_ver_in_host);
    if (0xA5 == initcode_ver_in_tp) {
        FTS_INFO("lcd init code ver is 0xA5, don't upgade init code");
        return false;
    } else if (0xFF == initcode_ver_in_tp) {
        FTS_DEBUG("lcd init code in tp is invalid, need upgrade init code");
        return true;
    } else if (initcode_ver_in_tp < initcode_ver_in_host)
        return true;
    else
        return false;
}

static int fts_lic_upgrade(struct i2c_client *client, struct fts_upgrade *upg)
{
    int ret = 0;
    bool hlic_upgrade = false;
    int upgrade_count = 0;
    u8 ver = 0;

    FTS_INFO("lcd initial code auto upgrade function");
    if ((!upg) || (!upg->func) || (!upg->func->lic_upgrade)) {
        FTS_ERROR("lcd upgrade function is null");
        return -EINVAL;
    }

    hlic_upgrade = fts_lic_need_upgrade(client);
    FTS_INFO("lcd init code upgrade flag:%d", hlic_upgrade);
    if (hlic_upgrade) {
        FTS_INFO("lcd initial code need upgrade, upgrade begin...");
        do {
            FTS_INFO("lcd initial code upgrade times:%d", upgrade_count);
            upgrade_count++;

            ret = upg->func->lic_upgrade(client, upg->lic, upg->lic_length);
            if (ret < 0) {
                fts_fwupg_reset_in_boot(client);
            } else {
                fts_lic_get_ver_in_tp(client, &ver);
                FTS_INFO("success upgrade to lcd initcode ver:%02x", ver);
                break;
            }
        } while (upgrade_count < 2);
    } else {
        FTS_INFO("lcd initial code don't need upgrade");
    }

    return ret;
}
#endif /* FTS_AUTO_LIC_UPGRADE_EN && FTS_AUTO_UPGRADE_EN */


static int fts_param_get_ver_in_tp(struct i2c_client *client, u8 *ver)
{
    int ret = 0;

    if (NULL == ver) {
        FTS_ERROR("ver is NULL");
        return -EINVAL;
    }

    ret = fts_i2c_read_reg(client, FTS_REG_IDE_PARA_VER_ID, ver);
    if (ret < 0) {
        FTS_ERROR("read fw param ver from tp fail");
        return ret;
    }

    if ((0x00 == *ver) || (0xFF == *ver)) {
        FTS_INFO("param version in tp invalid");
        return -EIO;
    }

    return 0;
}

static int fts_param_get_ver_in_host(u8 *ver)
{
    struct fts_upgrade *upg = fwupgrade;

    if ((!upg) || (!upg->func) || (!upg->fw) || (!ver)) {
        FTS_ERROR("fts_data/upgrade/func/fw/ver is NULL");
        return -EINVAL;
    }

    if (upg->fw_length < upg->func->paramcfgveroff) {
        FTS_ERROR("fw len(%x) < paramcfg ver offset(%x)",
                  upg->fw_length, upg->func->paramcfgveroff);
        return -EINVAL;
    }

    FTS_INFO("fw paramcfg version offset:%x", upg->func->paramcfgveroff);
    *ver = upg->fw[upg->func->paramcfgveroff];

    if ((0x00 == *ver) || (0xFF == *ver)) {
        FTS_INFO("param version in host invalid");
        return -EIO;
    }

    return 0;
}

/*
 * return: < 0 : error
 *         == 0: no ide
 *         == 1: ide
 */
static int fts_param_ide_in_host(void)
{
    u32 off = 0;
    struct fts_upgrade *upg = fwupgrade;

    if ((!upg) || (!upg->func) || (!upg->fw)) {
        FTS_ERROR("fts_data/upgrade/func/fw is NULL");
        return -EINVAL;
    }

    if (upg->fw_length < upg->func->paramcfgoff + FTS_FW_IDE_SIG_LEN) {
        FTS_INFO("fw len(%x) < paramcfg offset(%x), no IDE",
                 upg->fw_length, upg->func->paramcfgoff + FTS_FW_IDE_SIG_LEN);
        return 0;
    }

    off = upg->func->paramcfgoff;
    if (0 == memcmp(&upg->fw[off], FTS_FW_IDE_SIG, FTS_FW_IDE_SIG_LEN)) {
        FTS_INFO("fw in host is IDE version");
        return 1;
    }

    FTS_INFO("fw in host isn't IDE version");
    return 0;
}

/*
 * return: < 0 : error
 *         0   : no ide
 *         1   : ide
 */
static int fts_param_ide_in_tp(struct i2c_client *client, u8 *val)
{
    int ret = 0;

    ret = fts_i2c_read_reg(client, FTS_REG_IDE_PARA_STATUS, val);
    if (ret < 0) {
        FTS_ERROR("read IDE PARAM STATUS in tp fail");
        return ret;
    }

    if ((*val != 0xFF) && ((*val & 0x80) == 0x80)) {
        FTS_INFO("fw in tp is IDE version");
        return 1;
    }

    FTS_INFO("fw in tp isn't IDE version");
    return 0;
}

/************************************************************************
 * fts_param_need_upgrade - check fw paramcfg need upgrade or not
 *
 * Return:  < 0 : error if paramcfg need upgrade
 *          0   : no need upgrade
 *          1   : need upgrade app + param
 *          2   : need upgrade param
 ***********************************************************************/
static int fts_param_need_upgrade(struct i2c_client *client)
{
    int ret = 0;
    u8 val = 0;
    int ide_in_host = 0;
    int ide_in_tp = 0;
    u8 ver_in_host = 0;
    u8 ver_in_tp = 0;
    bool fwvalid = false;

    fwvalid = fts_fwupg_check_fw_valid(client);
    if ( !fwvalid) {
        FTS_INFO("fw is invalid, upgrade app+param");
        return 1;
    }

    ide_in_host = fts_param_ide_in_host();
    if (ide_in_host < 0) {
        FTS_INFO("fts_param_ide_in_host fail");
        return ide_in_host;
    }

    ide_in_tp = fts_param_ide_in_tp(client, &val);
    if (ide_in_tp < 0) {
        FTS_INFO("fts_param_ide_in_tp fail");
        return ide_in_tp;
    }

    if ((0 == ide_in_host) && (0 == ide_in_tp)) {
        FTS_INFO("fw in host&tp are both no ide");
        return 0;
    } else if (ide_in_host != ide_in_tp) {
        FTS_INFO("fw in host&tp not equal, need upgrade app+param");
        return 1;
    } else if ((1 == ide_in_host) && (1 == ide_in_tp)) {
        FTS_INFO("fw in host&tp are both ide");
        if ((val & 0x7F) != 0x00) {
            FTS_INFO("param invalid, need upgrade param");
            return 2;
        }

        ret = fts_param_get_ver_in_host(&ver_in_host);
        if (ret < 0) {
            FTS_ERROR("param version in host invalid");
            return ret;
        }

        ret = fts_param_get_ver_in_tp(client, &ver_in_tp);
        if (ret < 0) {
            FTS_ERROR("get IDE param ver in tp fail");
            return ret;
        }

        FTS_INFO("fw paramcfg version in tp:%x, host:%x", ver_in_tp, ver_in_host);
        if (ver_in_tp != ver_in_host) {
            return 2;
        }
    }

    return 0;
}

/************************************************************************
 * fts_fwupg_get_ver_in_tp - read fw ver from tp register
 *
 * return 0 if success, otherwise return error code
 ***********************************************************************/
static int fts_fwupg_get_ver_in_tp(struct i2c_client *client, u8 *ver)
{
    int ret = 0;

    if (NULL == ver) {
        FTS_ERROR("ver is NULL");
        return -EINVAL;
    }

    ret = fts_i2c_read_reg(client, FTS_REG_FW_VER, ver);
    if (ret < 0) {
        FTS_ERROR("read fw ver from tp fail");
        return ret;
    }

    return 0;
}

/************************************************************************
 * fts_fwupg_get_ver_in_host - read fw ver in host fw image
 *
 * return 0 if success, otherwise return error code
 ***********************************************************************/
static int fts_fwupg_get_ver_in_host(u8 *ver)
{
    struct fts_upgrade *upg = fwupgrade;

    if ((!upg) || (!upg->func) || (!upg->fw) || (!ver)) {
        FTS_ERROR("fts_data/upgrade/func/fw/ver is NULL");
        return -EINVAL;
    }

    if (upg->fw_length < upg->func->fwveroff) {
        FTS_ERROR("fw len(0x%0x) < fw ver offset(0x%x)",
                  upg->fw_length, upg->func->fwveroff);
        return -EINVAL;
    }

    FTS_INFO("fw version offset:0x%x", upg->func->fwveroff);
    *ver = upg->fw[upg->func->fwveroff];
    return 0;
}

/************************************************************************
 * fts_fwupg_need_upgrade - check fw need upgrade or not
 *
 * Return: return true if fw need upgrade
 ***********************************************************************/
static bool fts_fwupg_need_upgrade(struct i2c_client *client)
{
    int ret = 0;
    bool fwvalid = false;
    u8 fw_ver_in_host = 0;
    u8 fw_ver_in_tp = 0;

    fwvalid = fts_fwupg_check_fw_valid(client);
    if (fwvalid) {
        ret = fts_fwupg_get_ver_in_host(&fw_ver_in_host);
        if (ret < 0) {
            FTS_ERROR("get fw ver in host fail");
            return false;
        }

        ret = fts_fwupg_get_ver_in_tp(client, &fw_ver_in_tp);
        if (ret < 0) {
            FTS_ERROR("get fw ver in tp fail");
            return false;
        }

        FTS_INFO("fw version in tp:%x, host:%x", fw_ver_in_tp, fw_ver_in_host);
        if (fw_ver_in_tp != fw_ver_in_host) {
            return true;
        }
    } else {
        FTS_INFO("fw invalid, need upgrade fw");
        return true;
    }

    return false;
}

/************************************************************************
 * Name: fts_fw_upgrade
 * Brief: fw upgrade main entry
 * Input:
 * Output:
 * Return: return 0 if success, otherwise return error code
 ***********************************************************************/
int fts_fwupg_upgrade(struct i2c_client *client, struct fts_upgrade *upg)
{
    int ret = 0;
    bool upgrade_flag = false;
    int upgrade_count = 0;
    u8 ver = 0;

    FTS_INFO("fw auto upgrade function");
    if ((NULL == upg) || (NULL == upg->func)) {
        FTS_ERROR("upg/upg->func is null");
        return -EINVAL;
    }

    upgrade_flag = fts_fwupg_need_upgrade(client);
    FTS_INFO("fw upgrade flag:%d", upgrade_flag);
    do {
        upgrade_count++;
        if (upgrade_flag) {
            FTS_INFO("upgrade fw app(times:%d)", upgrade_count);
            if (upg->func->upgrade) {
                ret = upg->func->upgrade(client, upg->fw, upg->fw_length);
                if (ret < 0) {
                    fts_fwupg_reset_in_boot(client);
                } else {
                    fts_fwupg_get_ver_in_tp(client, &ver);
                    FTS_INFO("success upgrade to fw version %02x", ver);
                    break;
                }
            } else {
                FTS_ERROR("upgrade func/upgrade is null, return immediately");
                ret = -ENODATA;
                break;
            }
        } else {
            if (upg->func->param_upgrade) {
                ret = fts_param_need_upgrade(client);
                if (ret <= 0) {
                    FTS_INFO("param don't need upgrade");
                    break;
                } else if (1 == ret) {
                    FTS_INFO("force upgrade fw app(times:%d)", upgrade_count);
                    if (upg->func->upgrade) {
                        ret = upg->func->upgrade(client, upg->fw, upg->fw_length);
                        if (ret < 0) {
                            fts_fwupg_reset_in_boot(client);
                        } else {
                            break;
                        }
                    }
                } else if (2 == ret) {
                    FTS_INFO("upgrade param area(times:%d)", upgrade_count);
                    ret = upg->func->param_upgrade(client, upg->fw, upg->fw_length);
                    if (ret < 0) {
                        fts_fwupg_reset_in_boot(client);
                    } else {
                        fts_param_get_ver_in_tp(client, &ver);
                        FTS_INFO("success upgrade to fw param version %02x", ver);
                        break;
                    }
                } else
                    break;
            } else {
                break;
            }
        }
    } while (upgrade_count < 2);

    return ret;
}

#if FTS_AUTO_UPGRADE_EN
/************************************************************************
 * fts_fwupg_auto_upgrade - upgrade main entry
 ***********************************************************************/
void fts_fwupg_auto_upgrade(struct fts_ts_data *ts_data)
{
    int ret = 0;
    struct i2c_client *client = ts_data->client;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("********************FTS enter upgrade********************");

    ret = fts_fwupg_upgrade(client, upg);
    if (ret < 0)
        FTS_ERROR("**********tp fw(app/param) upgrade failed**********");
    else
        FTS_INFO("**********tp fw(app/param) no upgrade/upgrade success**********");

#if FTS_AUTO_LIC_UPGRADE_EN
    ret = fts_lic_upgrade(client, upg);
    if (ret < 0)
        FTS_ERROR("**********lcd init code upgrade failed**********");
    else
        FTS_INFO("**********lcd init code no upgrade/upgrade success**********");
#endif

    FTS_INFO("********************FTS exit upgrade********************");
}

/*
 * fts_fwupg_work - fw upgrade work function, handle upgrade
 * @vid - u16 consit of module id and panel id
 *
 * return 0 if success, otherwise return error code
 */
int fts_fwupg_get_vendorid(struct fts_ts_data *ts_data, u16 *vid)
{
    int ret = 0;
    bool fwvalid = false;
    u8 vendor_id = 0;
    u8 module_id = 0;
    u32 fwcfg_addr = 0;
    u8 cfgbuf[FTS_HEADER_LEN] = { 0 };
    struct i2c_client *client = ts_data->client;
    struct fts_upgrade *upg = fwupgrade;

    FTS_INFO("read vendor id from tp");
    if ((!upg) || (!upg->func) || (!vid)) {
        FTS_ERROR("upgrade/func/vid is null");
        return -EINVAL;
    }

    fwvalid = fts_fwupg_check_fw_valid(client);
    if (fwvalid) {
        ret = fts_i2c_read_reg(client, FTS_REG_VENDOR_ID, &vendor_id);
        if (ts_data->ic_info.is_incell)
            ret = fts_i2c_read_reg(client, FTS_REG_MODULE_ID, &module_id);
    } else {
        fwcfg_addr =  upg->func->fwcfgoff;
        ret = fts_flash_read(client, fwcfg_addr, cfgbuf, FTS_HEADER_LEN);
        vendor_id = cfgbuf[FTS_CONIFG_VENDORID_OFF];
        if (ts_data->ic_info.is_incell) {
            if ((cfgbuf[FTS_CONIFG_MODULEID_OFF] +
                 cfgbuf[FTS_CONIFG_MODULEID_OFF + 1]) == 0xFF)
                module_id = cfgbuf[FTS_CONIFG_MODULEID_OFF];
        }
    }

    if (ret < 0) {
        FTS_ERROR("fail to get vendor id from tp");
        return ret;
    }

    *vid = ((u16)module_id << 8) + vendor_id;
    return 0;
}

/*
 * fts_fwupg_get_fw_file - get upgrade fw file in host driver
 *
 * return 0 if success, otherwise return error code
 *
 * call it to get upgrade file which include in host driver
 * warning:
 *   1. if more fw files, please modify FTS_GET_VENDOR_ID_NUM&
 * FTS_VENDOR_ID
 *   2. For incell ICs, vendor id = module id << 8 + panel id
 *      For others, vendor id = 0x0000 + panel id
 *   3. get fw file from reques_firmware(), this function unactive
 */
static int fts_fwupg_get_fw_file(struct fts_ts_data *ts_data)
{
    struct upgrade_fw *fw = &fw_list[0];
    struct fts_upgrade *upg = fwupgrade;

#if (FTS_GET_VENDOR_ID_NUM > 1)
    int ret = 0;
    int i = 0;
    u16 vendor_id = 0;

    /* support multi vendor, must read correct vendor id */
    ret = fts_fwupg_get_vendorid(ts_data, &vendor_id);
    if (ret < 0) {
        FTS_ERROR("get vendor id failed");
        return ret;
    }
    FTS_INFO("success to read vendor id:%04x", vendor_id);
    for (i = 0; i < FTS_GET_VENDOR_ID_NUM; i++) {
        fw = &fw_list[i];
        if (vendor_id == fw->vendor_id) {
            FTS_INFO("vendor id match, get fw file successfully");
            break;
        }
    }
    if (i >= FTS_GET_VENDOR_ID_NUM) {
        FTS_ERROR("no vendor id match, don't get file");
        return -ENODATA;
    }
#endif

    if (upg) {
        upg->fw = fw->fw_file;
        upg->fw_length = fw->fw_len;
        upg->lic = fw->fw_file;
        upg->lic_length = fw->fw_len;

        FTS_INFO("upgrade fw file len:%d", upg->fw_length);
        if ((upg->fw_length < FTS_MIN_LEN)
            || (upg->fw_length > FTS_MAX_LEN_FILE)) {
            FTS_ERROR("fw file len(%d) fail", upg->fw_length);
            return -ENODATA;
        }
    }
    return 0;
}

/*
 * fts_fwupg_init_ic_detail - for ic detail initialaztion
 */
static void fts_fwupg_init_ic_detail(void)
{
    struct fts_upgrade *upg = fwupgrade;

    if (upg && upg->func && upg->func->init) {
        upg->func->init();
    }
}

/*
 * fts_fwupg_work - fw upgrade work function
 * 1. get fw image/file
 * 2. call upgrade main function(fts_fwupg_auto_upgrade)
 */
static void fts_fwupg_work(struct work_struct *work)
{
    int ret = 0;
    struct fts_ts_data *ts_data = container_of(work,
                                  struct fts_ts_data, fwupg_work);

    FTS_INFO("fw upgrade work function");
    ts_data->fw_loading = 1;
    fts_irq_disable();
#if FTS_ESDCHECK_EN
    fts_esdcheck_switch(DISABLE);
#endif

    FTS_INFO("get upgrade fw file");
    ret = fts_fwupg_get_fw_file(ts_data);
    fts_fwupg_init_ic_detail();
    if (ret < 0) {
        FTS_ERROR("get file fail, can't upgrade");
    } else {
        /* run auto upgrade */
        fts_fwupg_auto_upgrade(ts_data);
    }

#if FTS_ESDCHECK_EN
    fts_esdcheck_switch(ENABLE);
#endif
    fts_irq_enable();
    ts_data->fw_loading = 0;
}

/*****************************************************************************
 *  Name: fts_fwupg_init
 *  Brief: upgrade function initialization
 *  Input:
 *  Output:
 *  Return: return 0 if success, otherwise return error code
 *****************************************************************************/
int fts_fwupg_init(struct fts_ts_data *ts_data)
{
    int i = 0;
    int j = 0;
    int ic_stype = ts_data->ic_info.ids.type;
    struct upgrade_func *func = upgrade_func_list[0];
    int func_count = sizeof(upgrade_func_list) / sizeof(upgrade_func_list[0]);

    FTS_INFO("fw upgrade init function");

    if (NULL == ts_data->ts_workqueue) {
        FTS_ERROR("fts workqueue is NULL, can't run upgrade function");
        return -EINVAL;
    }

    if (0 == func_count) {
        FTS_ERROR("no upgrade function in tp driver");
        return -ENODATA;
    }

    fwupgrade = (struct fts_upgrade *)kzalloc(sizeof(*fwupgrade), GFP_KERNEL);
    if (NULL == fwupgrade) {
        FTS_ERROR("malloc memory for upgrade fail");
        return -ENOMEM;
    }

    if (1 == func_count) {
        fwupgrade->func = func;
    } else {
        for (i = 0; i < func_count; i++) {
            func = upgrade_func_list[i];
            for (j = 0; j < FTX_MAX_COMPATIBLE_TYPE; j++) {
                if (0 == func->ctype[j])
                    break;
                else if (func->ctype[j] == ic_stype) {
                    FTS_INFO("match upgrade function,type:%x", (int)func->ctype[j]);
                    fwupgrade->func = func;
                }
            }
        }
    }

    if (NULL == fwupgrade->func) {
        FTS_ERROR("no upgrade function match, can't upgrade");
        return -ENODATA;
    }

    INIT_WORK(&ts_data->fwupg_work, fts_fwupg_work);
    queue_work(ts_data->ts_workqueue, &ts_data->fwupg_work);

    return 0;
}

/*****************************************************************************
 *  Name: fts_fwupg_exit
 *  Brief:
 *  Input:
 *  Output:
 *  Return:
 *****************************************************************************/
int fts_fwupg_exit(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();
    if (fwupgrade) {
        kfree(fwupgrade);
    }
    FTS_FUNC_EXIT();

    return 0;
}

#endif  /* #if FTS_AUTO_UPGRADE_EN */
