/*
 * ***************************************************************************
 *  FILE:     putest.c
 *
 *  PURPOSE:    putest related functions.
 *
 *  Copyright (C) 2008-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ***************************************************************************
 */

#include <linux/vmalloc.h>
#include <linux/firmware.h>

#include "unifi_priv.h"
#include "csr_wifi_hip_chiphelper.h"

#define UNIFI_PROC_BOTH 3


int unifi_putest_cmd52_read(unifi_priv_t *priv, unsigned char *arg)
{
    struct unifi_putest_cmd52 cmd52_params;
    u8 *arg_pos;
    unsigned int cmd_param_size;
    int r;
    CsrResult csrResult;
    unsigned char ret_buffer[32];
    u8 *ret_buffer_pos;
    u8 retries;

    arg_pos = (u8*)(((unifi_putest_command_t*)arg) + 1);
    if (get_user(cmd_param_size, (int*)arg_pos)) {
        unifi_error(priv,
                    "unifi_putest_cmd52_read: Failed to get the argument\n");
        return -EFAULT;
    }

    if (cmd_param_size != sizeof(struct unifi_putest_cmd52)) {
        unifi_error(priv,
                    "unifi_putest_cmd52_read: cmd52 struct mismatch\n");
        return -EINVAL;
    }

    arg_pos += sizeof(unsigned int);
    if (copy_from_user(&cmd52_params,
                       (void*)arg_pos,
                       sizeof(struct unifi_putest_cmd52))) {
        unifi_error(priv,
                    "unifi_putest_cmd52_read: Failed to get the cmd52 params\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "cmd52r: func=%d addr=0x%x ",
                cmd52_params.funcnum, cmd52_params.addr);

    retries = 3;
    CsrSdioClaim(priv->sdio);
    do {
        if (cmd52_params.funcnum == 0) {
            csrResult = CsrSdioF0Read8(priv->sdio, cmd52_params.addr, &cmd52_params.data);
        } else {
            csrResult = CsrSdioRead8(priv->sdio, cmd52_params.addr, &cmd52_params.data);
        }
    } while (--retries && ((csrResult == CSR_SDIO_RESULT_CRC_ERROR) || (csrResult == CSR_SDIO_RESULT_TIMEOUT)));
    CsrSdioRelease(priv->sdio);

    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "\nunifi_putest_cmd52_read: Read8() failed (csrResult=0x%x)\n", csrResult);
        return -EFAULT;
    }
    unifi_trace(priv, UDBG2, "data=%d\n", cmd52_params.data);

    /* Copy the info to the out buffer */
    *(unifi_putest_command_t*)ret_buffer = UNIFI_PUTEST_CMD52_READ;
    ret_buffer_pos = (u8*)(((unifi_putest_command_t*)ret_buffer) + 1);
    *(unsigned int*)ret_buffer_pos = sizeof(struct unifi_putest_cmd52);
    ret_buffer_pos += sizeof(unsigned int);
    memcpy(ret_buffer_pos, &cmd52_params, sizeof(struct unifi_putest_cmd52));
    ret_buffer_pos += sizeof(struct unifi_putest_cmd52);

    r = copy_to_user((void*)arg,
                     ret_buffer,
                     ret_buffer_pos - ret_buffer);
    if (r) {
        unifi_error(priv,
                    "unifi_putest_cmd52_read: Failed to return the data\n");
        return -EFAULT;
    }

    return 0;
}


int unifi_putest_cmd52_write(unifi_priv_t *priv, unsigned char *arg)
{
    struct unifi_putest_cmd52 cmd52_params;
    u8 *arg_pos;
    unsigned int cmd_param_size;
    CsrResult csrResult;
    u8 retries;

    arg_pos = (u8*)(((unifi_putest_command_t*)arg) + 1);
    if (get_user(cmd_param_size, (int*)arg_pos)) {
        unifi_error(priv,
                    "unifi_putest_cmd52_write: Failed to get the argument\n");
        return -EFAULT;
    }

    if (cmd_param_size != sizeof(struct unifi_putest_cmd52)) {
        unifi_error(priv,
                    "unifi_putest_cmd52_write: cmd52 struct mismatch\n");
        return -EINVAL;
    }

    arg_pos += sizeof(unsigned int);
    if (copy_from_user(&cmd52_params,
                       (void*)(arg_pos),
                       sizeof(struct unifi_putest_cmd52))) {
        unifi_error(priv,
                    "unifi_putest_cmd52_write: Failed to get the cmd52 params\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "cmd52w: func=%d addr=0x%x data=%d\n",
                cmd52_params.funcnum, cmd52_params.addr, cmd52_params.data);

    retries = 3;
    CsrSdioClaim(priv->sdio);
    do {
        if (cmd52_params.funcnum == 0) {
            csrResult = CsrSdioF0Write8(priv->sdio, cmd52_params.addr, cmd52_params.data);
        } else {
            csrResult = CsrSdioWrite8(priv->sdio, cmd52_params.addr, cmd52_params.data);
        }
    } while (--retries && ((csrResult == CSR_SDIO_RESULT_CRC_ERROR) || (csrResult == CSR_SDIO_RESULT_TIMEOUT)));
    CsrSdioRelease(priv->sdio);

    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_cmd52_write: Write8() failed (csrResult=0x%x)\n", csrResult);
        return -EFAULT;
    }

    return 0;
}

int unifi_putest_gp_read16(unifi_priv_t *priv, unsigned char *arg)
{
    struct unifi_putest_gp_rw16 gp_r16_params;
    u8 *arg_pos;
    unsigned int cmd_param_size;
    int r;
    CsrResult csrResult;
    unsigned char ret_buffer[32];
    u8 *ret_buffer_pos;

    arg_pos = (u8*)(((unifi_putest_command_t*)arg) + 1);
    if (get_user(cmd_param_size, (int*)arg_pos)) {
        unifi_error(priv,
                    "unifi_putest_gp_read16: Failed to get the argument\n");
        return -EFAULT;
    }

    if (cmd_param_size != sizeof(struct unifi_putest_gp_rw16)) {
        unifi_error(priv,
                    "unifi_putest_gp_read16: struct mismatch\n");
        return -EINVAL;
    }

    arg_pos += sizeof(unsigned int);
    if (copy_from_user(&gp_r16_params,
                       (void*)arg_pos,
                       sizeof(struct unifi_putest_gp_rw16))) {
        unifi_error(priv,
                    "unifi_putest_gp_read16: Failed to get the params\n");
        return -EFAULT;
    }
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_card_read16(priv->card, gp_r16_params.addr, &gp_r16_params.data);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_gp_read16: unifi_card_read16() GP=0x%x failed (csrResult=0x%x)\n", gp_r16_params.addr, csrResult);
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "gp_r16: GP=0x%08x, data=0x%04x\n", gp_r16_params.addr, gp_r16_params.data);

    /* Copy the info to the out buffer */
    *(unifi_putest_command_t*)ret_buffer = UNIFI_PUTEST_GP_READ16;
    ret_buffer_pos = (u8*)(((unifi_putest_command_t*)ret_buffer) + 1);
    *(unsigned int*)ret_buffer_pos = sizeof(struct unifi_putest_gp_rw16);
    ret_buffer_pos += sizeof(unsigned int);
    memcpy(ret_buffer_pos, &gp_r16_params, sizeof(struct unifi_putest_gp_rw16));
    ret_buffer_pos += sizeof(struct unifi_putest_gp_rw16);

    r = copy_to_user((void*)arg,
                     ret_buffer,
                     ret_buffer_pos - ret_buffer);
    if (r) {
        unifi_error(priv,
                    "unifi_putest_gp_read16: Failed to return the data\n");
        return -EFAULT;
    }

    return 0;
}

int unifi_putest_gp_write16(unifi_priv_t *priv, unsigned char *arg)
{
    struct unifi_putest_gp_rw16 gp_w16_params;
    u8 *arg_pos;
    unsigned int cmd_param_size;
    CsrResult csrResult;

    arg_pos = (u8*)(((unifi_putest_command_t*)arg) + 1);
    if (get_user(cmd_param_size, (int*)arg_pos)) {
        unifi_error(priv,
                    "unifi_putest_gp_write16: Failed to get the argument\n");
        return -EFAULT;
    }

    if (cmd_param_size != sizeof(struct unifi_putest_gp_rw16)) {
        unifi_error(priv,
                    "unifi_putest_gp_write16: struct mismatch\n");
        return -EINVAL;
    }

    arg_pos += sizeof(unsigned int);
    if (copy_from_user(&gp_w16_params,
                       (void*)(arg_pos),
                       sizeof(struct unifi_putest_gp_rw16))) {
        unifi_error(priv,
                    "unifi_putest_gp_write16: Failed to get the params\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "gp_w16: GP=0x%08x, data=0x%04x\n", gp_w16_params.addr, gp_w16_params.data);
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_card_write16(priv->card, gp_w16_params.addr, gp_w16_params.data);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_gp_write16: unifi_card_write16() GP=%x failed (csrResult=0x%x)\n", gp_w16_params.addr, csrResult);
        return -EFAULT;
    }

    return 0;
}

int unifi_putest_set_sdio_clock(unifi_priv_t *priv, unsigned char *arg)
{
    int sdio_clock_speed;
    CsrResult csrResult;

    if (get_user(sdio_clock_speed, (int*)(((unifi_putest_command_t*)arg) + 1))) {
        unifi_error(priv,
                    "unifi_putest_set_sdio_clock: Failed to get the argument\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "set sdio clock: %d KHz\n", sdio_clock_speed);

    CsrSdioClaim(priv->sdio);
    csrResult = CsrSdioMaxBusClockFrequencySet(priv->sdio, sdio_clock_speed * 1000);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_set_sdio_clock: Set clock failed (csrResult=0x%x)\n", csrResult);
        return -EFAULT;
    }

    return 0;
}


int unifi_putest_start(unifi_priv_t *priv, unsigned char *arg)
{
    int r;
    CsrResult csrResult;
    int already_in_test = priv->ptest_mode;

    /* Ensure that sme_sys_suspend() doesn't power down the chip because:
     *  1) Power is needed anyway for ptest.
     *  2) The app code uses the START ioctl as a reset, so it gets called
     *     multiple times. If the app stops the XAPs, but the power_down/up
     *     sequence doesn't actually power down the chip, there can be problems
     *     resetting, because part of the power_up sequence disables function 1
     */
    priv->ptest_mode = 1;

    /* Suspend the SME and UniFi */
    if (priv->sme_cli) {
        r = sme_sys_suspend(priv);
        if (r) {
            unifi_error(priv,
                        "unifi_putest_start: failed to suspend UniFi\n");
            return r;
        }
    }

    /* Application may have stopped the XAPs, but they are needed for reset */
    if (already_in_test) {
        CsrSdioClaim(priv->sdio);
        csrResult = unifi_start_processors(priv->card);
        CsrSdioRelease(priv->sdio);
        if (csrResult != CSR_RESULT_SUCCESS) {
            unifi_error(priv, "Failed to start XAPs. Hard reset required.\n");
        }
    } else {
        /* Ensure chip is powered for the case where there's no unifi_helper */
        CsrSdioClaim(priv->sdio);
        csrResult = CsrSdioPowerOn(priv->sdio);
        CsrSdioRelease(priv->sdio);
        if (csrResult != CSR_RESULT_SUCCESS) {
            unifi_error(priv, "CsrSdioPowerOn csrResult = %d\n", csrResult);
        }
    }
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_init(priv->card);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_start: failed to init UniFi\n");
        return CsrHipResultToStatus(csrResult);
    }

    return 0;
}


int unifi_putest_stop(unifi_priv_t *priv, unsigned char *arg)
{
    int r = 0;
    CsrResult csrResult;

    /* Application may have stopped the XAPs, but they are needed for reset */
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_start_processors(priv->card);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "Failed to start XAPs. Hard reset required.\n");
    }

    /* PUTEST_STOP is also used to resume the XAPs after SME coredump.
     * Don't power off the chip, leave that to the normal wifi-off which is
     * about to carry on. No need to resume the SME either, as it wasn't suspended.
     */
    if (priv->coredump_mode) {
        priv->coredump_mode = 0;
        return 0;
    }

    /* At this point function 1 is enabled and the XAPs are running, so it is
     * safe to let the card power down. Power is restored later, asynchronously,
     * during the wifi_on requested by the SME.
     */
    CsrSdioClaim(priv->sdio);
    CsrSdioPowerOff(priv->sdio);
    CsrSdioRelease(priv->sdio);

    /* Resume the SME and UniFi */
    if (priv->sme_cli) {
        r = sme_sys_resume(priv);
        if (r) {
            unifi_error(priv,
                        "unifi_putest_stop: failed to resume SME\n");
        }
    }
    priv->ptest_mode = 0;

    return r;
}


int unifi_putest_dl_fw(unifi_priv_t *priv, unsigned char *arg)
{
#define UF_PUTEST_MAX_FW_FILE_NAME      16
#define UNIFI_MAX_FW_PATH_LEN           32
    unsigned int fw_name_length;
    unsigned char fw_name[UF_PUTEST_MAX_FW_FILE_NAME+1];
    unsigned char *name_buffer;
    int postfix;
    char fw_path[UNIFI_MAX_FW_PATH_LEN];
    const struct firmware *fw_entry;
    struct dlpriv temp_fw_sta;
    int r;
    CsrResult csrResult;

    /* Get the f/w file name length */
    if (get_user(fw_name_length, (unsigned int*)(((unifi_putest_command_t*)arg) + 1))) {
        unifi_error(priv,
                    "unifi_putest_dl_fw: Failed to get the length argument\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "unifi_putest_dl_fw: file name size = %d\n", fw_name_length);

    /* Sanity check for the f/w file name length */
    if (fw_name_length > UF_PUTEST_MAX_FW_FILE_NAME) {
        unifi_error(priv,
                    "unifi_putest_dl_fw: F/W file name is too long\n");
        return -EINVAL;
    }

    /* Get the f/w file name */
    name_buffer = ((unsigned char*)arg) + sizeof(unifi_putest_command_t) + sizeof(unsigned int);
    if (copy_from_user(fw_name, (void*)name_buffer, fw_name_length)) {
        unifi_error(priv, "unifi_putest_dl_fw: Failed to get the file name\n");
        return -EFAULT;
    }
    fw_name[fw_name_length] = '\0';
    unifi_trace(priv, UDBG2, "unifi_putest_dl_fw: file = %s\n", fw_name);

    /* Keep the existing f/w to a temp, we need to restore it later */
    temp_fw_sta = priv->fw_sta;

    /* Get the putest f/w */
    postfix = priv->instance;
    scnprintf(fw_path, UNIFI_MAX_FW_PATH_LEN, "unifi-sdio-%d/%s",
              postfix, fw_name);
    r = request_firmware(&fw_entry, fw_path, priv->unifi_device);
    if (r == 0) {
        priv->fw_sta.fw_desc = (void *)fw_entry;
        priv->fw_sta.dl_data = fw_entry->data;
        priv->fw_sta.dl_len = fw_entry->size;
    } else {
        unifi_error(priv, "Firmware file not available\n");
        return -EINVAL;
    }

    /* Application may have stopped the XAPs, but they are needed for reset */
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_start_processors(priv->card);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "Failed to start XAPs. Hard reset required.\n");
    }

    /* Download the f/w. On UF6xxx this will cause the f/w file to convert
     * into patch format and download via the ROM boot loader
     */
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_download(priv->card, 0x0c00);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_dl_fw: failed to download the f/w\n");
        goto free_fw;
    }

    /* Free the putest f/w... */
free_fw:
    uf_release_firmware(priv, &priv->fw_sta);
    /* ... and restore the original f/w */
    priv->fw_sta = temp_fw_sta;

    return CsrHipResultToStatus(csrResult);
}


int unifi_putest_dl_fw_buff(unifi_priv_t *priv, unsigned char *arg)
{
    unsigned int fw_length;
    unsigned char *fw_buf = NULL;
    unsigned char *fw_user_ptr;
    struct dlpriv temp_fw_sta;
    CsrResult csrResult;

    /* Get the f/w buffer length */
    if (get_user(fw_length, (unsigned int*)(((unifi_putest_command_t*)arg) + 1))) {
        unifi_error(priv,
                    "unifi_putest_dl_fw_buff: Failed to get the length arg\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "unifi_putest_dl_fw_buff: size = %d\n", fw_length);

    /* Sanity check for the buffer length */
    if (fw_length == 0 || fw_length > 0xfffffff) {
        unifi_error(priv,
                    "unifi_putest_dl_fw_buff: buffer length bad %u\n", fw_length);
        return -EINVAL;
    }

    /* Buffer for kernel copy of the f/w image */
    fw_buf = CsrPmemAlloc(fw_length);
    if (!fw_buf) {
        unifi_error(priv, "unifi_putest_dl_fw_buff: malloc fail\n");
        return -ENOMEM;
    }

    /* Get the f/w image */
    fw_user_ptr = ((unsigned char*)arg) + sizeof(unifi_putest_command_t) + sizeof(unsigned int);
    if (copy_from_user(fw_buf, (void*)fw_user_ptr, fw_length)) {
        unifi_error(priv, "unifi_putest_dl_fw_buff: Failed to get the buffer\n");
        CsrPmemFree(fw_buf);
        return -EFAULT;
    }

    /* Save the existing f/w to a temp, we need to restore it later */
    temp_fw_sta = priv->fw_sta;

    /* Setting fw_desc NULL indicates to the core that no f/w file was loaded
     * via the kernel request_firmware() mechanism. This indicates to the core
     * that it shouldn't call release_firmware() after the download is done.
     */
    priv->fw_sta.fw_desc = NULL;            /* No OS f/w resource */
    priv->fw_sta.dl_data = fw_buf;
    priv->fw_sta.dl_len = fw_length;

    /* Application may have stopped the XAPs, but they are needed for reset */
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_start_processors(priv->card);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "Failed to start XAPs. Hard reset required.\n");
    }

    /* Download the f/w. On UF6xxx this will cause the f/w file to convert
     * into patch format and download via the ROM boot loader
     */
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_download(priv->card, 0x0c00);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv,
                    "unifi_putest_dl_fw_buff: failed to download the f/w\n");
        goto free_fw;
    }

free_fw:
    /* Finished with the putest f/w, so restore the station f/w */
    priv->fw_sta = temp_fw_sta;
    CsrPmemFree(fw_buf);

    return CsrHipResultToStatus(csrResult);
}


int unifi_putest_coredump_prepare(unifi_priv_t *priv, unsigned char *arg)
{
    u16 data_u16;
    s32 i;
    CsrResult r;

    unifi_info(priv, "Preparing for SDIO coredump\n");
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE)
    unifi_debug_buf_dump();
#endif

    /* Sanity check that userspace hasn't called a PUTEST_START, because that
     * would have reset UniFi, potentially power cycling it and losing context
     */
    if (priv->ptest_mode) {
        unifi_error(priv, "PUTEST_START shouldn't be used before a coredump\n");
    }

    /* Flag that the userspace has requested coredump. Even if this preparation
     * fails, the SME will call PUTEST_STOP to tidy up.
     */
    priv->coredump_mode = 1;

    for (i = 0; i < 3; i++) {
        CsrSdioClaim(priv->sdio);
        r = CsrSdioRead16(priv->sdio, CHIP_HELPER_UNIFI_GBL_CHIP_VERSION*2, &data_u16);
        CsrSdioRelease(priv->sdio);
        if (r != CSR_RESULT_SUCCESS) {
            unifi_info(priv, "Failed to read chip version! Try %d\n", i);

            /* First try, re-enable function which may have been disabled by f/w panic */
            if (i == 0) {
                unifi_info(priv, "Try function enable\n");
                CsrSdioClaim(priv->sdio);
                r = CsrSdioFunctionEnable(priv->sdio);
                CsrSdioRelease(priv->sdio);
                if (r != CSR_RESULT_SUCCESS) {
                    unifi_error(priv, "CsrSdioFunctionEnable failed %d\n", r);
                }
                continue;
            }

            /* Subsequent tries, reset */

            /* Set clock speed low */
            CsrSdioClaim(priv->sdio);
            r = CsrSdioMaxBusClockFrequencySet(priv->sdio, UNIFI_SDIO_CLOCK_SAFE_HZ);
            CsrSdioRelease(priv->sdio);
            if (r != CSR_RESULT_SUCCESS) {
                unifi_error(priv, "CsrSdioMaxBusClockFrequencySet() failed %d\n", r);
            }

            /* Card software reset */
            CsrSdioClaim(priv->sdio);
            r = unifi_card_hard_reset(priv->card);
            CsrSdioRelease(priv->sdio);
            if (r != CSR_RESULT_SUCCESS) {
                unifi_error(priv, "unifi_card_hard_reset() failed %d\n", r);
            }
        } else {
            unifi_info(priv, "Read chip version of 0x%04x\n", data_u16);
            break;
        }
    }

    if (r != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "Failed to prepare chip\n");
        return -EIO;
    }

    /* Stop the XAPs for coredump. The PUTEST_STOP must be called, e.g. at
     * Raw SDIO deinit, to resume them.
     */
    CsrSdioClaim(priv->sdio);
    r = unifi_card_stop_processor(priv->card, UNIFI_PROC_BOTH);
    CsrSdioRelease(priv->sdio);
    if (r != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "Failed to stop processors\n");
    }

    return 0;
}

int unifi_putest_cmd52_block_read(unifi_priv_t *priv, unsigned char *arg)
{
    struct unifi_putest_block_cmd52_r block_cmd52;
    u8 *arg_pos;
    unsigned int cmd_param_size;
    CsrResult r;
    u8 *block_local_buffer;

    arg_pos = (u8*)(((unifi_putest_command_t*)arg) + 1);
    if (get_user(cmd_param_size, (int*)arg_pos)) {
        unifi_error(priv,
                    "cmd52r_block: Failed to get the argument\n");
        return -EFAULT;
    }

    if (cmd_param_size != sizeof(struct unifi_putest_block_cmd52_r)) {
        unifi_error(priv,
                    "cmd52r_block: cmd52 struct mismatch\n");
        return -EINVAL;
    }

    arg_pos += sizeof(unsigned int);
    if (copy_from_user(&block_cmd52,
                       (void*)arg_pos,
                       sizeof(struct unifi_putest_block_cmd52_r))) {
        unifi_error(priv,
                    "cmd52r_block: Failed to get the cmd52 params\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG2, "cmd52r_block: func=%d addr=0x%x len=0x%x ",
                block_cmd52.funcnum, block_cmd52.addr, block_cmd52.length);

    block_local_buffer = vmalloc(block_cmd52.length);
    if (block_local_buffer == NULL) {
        unifi_error(priv, "cmd52r_block: Failed to allocate buffer\n");
        return -ENOMEM;
    }

    CsrSdioClaim(priv->sdio);
    r = unifi_card_readn(priv->card, block_cmd52.addr, block_local_buffer, block_cmd52.length);
    CsrSdioRelease(priv->sdio);
    if (r != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "cmd52r_block: unifi_readn failed\n");
        return -EIO;
    }

    if (copy_to_user((void*)block_cmd52.data,
                     block_local_buffer,
                     block_cmd52.length)) {
        unifi_error(priv,
                    "cmd52r_block: Failed to return the data\n");
        return -EFAULT;
    }

    return 0;
}
