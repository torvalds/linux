/*
 * ---------------------------------------------------------------------------
 *  FILE:     firmware.c
 *
 *  PURPOSE:
 *      Implements the f/w related HIP core lib API.
 *      It is part of the porting exercise in Linux.
 *
 *      Also, it contains example code for reading the loader and f/w files
 *      from the userspace and starting the SME in Linux.
 *
 * Copyright (C) 2005-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <asm/uaccess.h>
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_unifi_udi.h"
#include "unifiio.h"
#include "unifi_priv.h"

/*
 * ---------------------------------------------------------------------------
 *
 *      F/W download. Part of the HIP core API
 *
 * ---------------------------------------------------------------------------
 */


/*
 * ---------------------------------------------------------------------------
 *  unifi_fw_read_start
 *
 *      Returns a structure to be passed in unifi_fw_read().
 *      This structure is an OS specific description of the f/w file.
 *      In the linux implementation it is a buffer with the f/w and its' length.
 *      The HIP driver calls this functions to request for the loader or
 *      the firmware file.
 *      The structure pointer can be freed when unifi_fw_read_stop() is called.
 *
 *  Arguments:
 *      ospriv          Pointer to driver context.
 *      is_fw           Type of firmware to retrieve
 *      info            Versions information. Can be used to determine
 *                      the appropriate f/w file to load.
 *
 *  Returns:
 *      O on success, non-zero otherwise.
 *
 * ---------------------------------------------------------------------------
 */
void*
unifi_fw_read_start(void *ospriv, s8 is_fw, const card_info_t *info)
{
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    CSR_UNUSED(info);

    if (is_fw == UNIFI_FW_STA) {
        /* F/w may have been released after a previous successful download. */
        if (priv->fw_sta.dl_data == NULL) {
            unifi_trace(priv, UDBG2, "Attempt reload of sta f/w\n");
            uf_request_firmware_files(priv, UNIFI_FW_STA);
        }
        /* Set up callback struct for readfunc() */
        if (priv->fw_sta.dl_data != NULL) {
            return &priv->fw_sta;
        }

    } else {
        unifi_error(priv, "downloading firmware... unknown request: %d\n", is_fw);
    }

    return NULL;
} /* unifi_fw_read_start() */



/*
 * ---------------------------------------------------------------------------
 *  unifi_fw_read_stop
 *
 *      Called when the HIP driver has finished using the loader or
 *      the firmware file.
 *      The firmware buffer may be released now.
 *
 *  Arguments:
 *      ospriv          Pointer to driver context.
 *      dlpriv          The pointer returned by unifi_fw_read_start()
 *
 * ---------------------------------------------------------------------------
 */
void
unifi_fw_read_stop(void *ospriv, void *dlpriv)
{
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    struct dlpriv *dl_struct = (struct dlpriv *)dlpriv;

    if (dl_struct != NULL) {
        if (dl_struct->dl_data != NULL) {
            unifi_trace(priv, UDBG2, "Release f/w buffer %p, %d bytes\n",
                        dl_struct->dl_data, dl_struct->dl_len);
        }
        uf_release_firmware(priv, dl_struct);
    }

} /* unifi_fw_read_stop() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_fw_open_buffer
 *
 *  Returns a handle for a buffer dynamically allocated by the driver,
 *  e.g. into which a firmware file may have been converted from another format
 *  which is the case with some production test images.
 *
 *  The handle may then be used by unifi_fw_read() to access the contents of
 *  the buffer.
 *
 *  Arguments:
 *      ospriv          Pointer to driver context.
 *      fwbuf           Buffer containing firmware image
 *      len             Length of buffer in bytes
 *
 *  Returns
 *      Handle for buffer, or NULL on error
 * ---------------------------------------------------------------------------
 */
void *
unifi_fw_open_buffer(void *ospriv, void *fwbuf, u32 len)
{
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;

    if (fwbuf == NULL) {
        return NULL;
    }
    priv->fw_conv.dl_data = fwbuf;
    priv->fw_conv.dl_len = len;
    priv->fw_conv.fw_desc = NULL;   /* No OS f/w resource is associated */

    return &priv->fw_conv;
}

/*
 * ---------------------------------------------------------------------------
 *  unifi_fw_close_buffer
 *
 *  Releases any handle for a buffer dynamically allocated by the driver,
 *  e.g. into which a firmware file may have been converted from another format
 *  which is the case with some production test images.
 *
 *
 *  Arguments:
 *      ospriv          Pointer to driver context.
 *      fwbuf           Buffer containing firmware image
 *
 *  Returns
 *      Handle for buffer, or NULL on error
 * ---------------------------------------------------------------------------
 */
void unifi_fw_close_buffer(void *ospriv, void *fwbuf)
{
}

/*
 * ---------------------------------------------------------------------------
 *  unifi_fw_read
 *
 *      The HIP driver calls this function to ask for a part of the loader or
 *      the firmware file.
 *
 *  Arguments:
 *      ospriv          Pointer to driver context.
 *      arg             The pointer returned by unifi_fw_read_start().
 *      offset          The offset in the file to return from.
 *      buf             A buffer to store the requested data.
 *      len             The size of the buf and the size of the requested data.
 *
 *  Returns
 *      The number of bytes read from the firmware image, or -ve on error
 * ---------------------------------------------------------------------------
 */
s32
unifi_fw_read(void *ospriv, void *arg, u32 offset, void *buf, u32 len)
{
    const struct dlpriv *dlpriv = arg;

    if (offset >= dlpriv->dl_len) {
        /* at end of file */
        return 0;
    }

    if ((offset + len) > dlpriv->dl_len) {
        /* attempt to read past end of file */
        return -1;
    }

    memcpy(buf, dlpriv->dl_data+offset, len);

    return len;

} /* unifi_fw_read() */




#define UNIFIHELPER_INIT_MODE_SMEUSER   2
#define UNIFIHELPER_INIT_MODE_NATIVE    1

/*
 * ---------------------------------------------------------------------------
 *  uf_run_unifihelper
 *
 *      Ask userspace to send us firmware for download by running
 *      '/usr/sbin/unififw'.
 *      The same script starts the SME userspace application.
 *      Derived from net_run_sbin_hotplug().
 *
 *  Arguments:
 *      priv            Pointer to OS private struct.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
int
uf_run_unifihelper(unifi_priv_t *priv)
{
#ifdef ANDROID_BUILD
    char *prog = "/system/bin/unififw";
#else
    char *prog = "/usr/sbin/unififw";
#endif /* ANDROID_BUILD */

    char *argv[6], *envp[4];
    char inst_str[8];
    char init_mode[8];
    int i, r;

#if (defined CSR_SME_USERSPACE) && (!defined CSR_SUPPORT_WEXT)
    unifi_trace(priv, UDBG1, "SME userspace build: run unifi_helper manually\n");
    return 0;
#endif

    unifi_trace(priv, UDBG1, "starting %s\n", prog);

    snprintf(inst_str,   8, "%d", priv->instance);
#if (defined CSR_SME_USERSPACE)
    snprintf(init_mode, 8, "%d", UNIFIHELPER_INIT_MODE_SMEUSER);
#else
    snprintf(init_mode, 8, "%d", UNIFIHELPER_INIT_MODE_NATIVE);
#endif /* CSR_SME_USERSPACE */

    i = 0;
    argv[i++] = prog;
    argv[i++] = inst_str;
    argv[i++] = init_mode;
    argv[i++] = 0;
    argv[i] = 0;
    /* Don't add more args without making argv bigger */

    /* minimal command environment */
    i = 0;
    envp[i++] = "HOME=/";
    envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
    envp[i] = 0;
    /* Don't add more without making envp bigger */

    unifi_trace(priv, UDBG2, "running %s %s %s\n", argv[0], argv[1], argv[2]);

    r = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);

    return r;
} /* uf_run_unifihelper() */

#ifdef CSR_WIFI_SPLIT_PATCH
static u8 is_ap_mode(unifi_priv_t *priv)
{
    if (priv == NULL || priv->interfacePriv[0] == NULL)
    {
        return FALSE;
    }

    /* Test for mode requiring AP patch */
    return(CSR_WIFI_HIP_IS_AP_FW(priv->interfacePriv[0]->interfaceMode));
}
#endif

/*
 * ---------------------------------------------------------------------------
 *  uf_request_firmware_files
 *
 *      Get the firmware files from userspace.
 *
 *  Arguments:
 *      priv            Pointer to OS private struct.
 *      is_fw           type of firmware to load (UNIFI_FW_STA/LOADER)
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
int uf_request_firmware_files(unifi_priv_t *priv, int is_fw)
{
    /* uses the default method to get the firmware */
    const struct firmware *fw_entry;
    int postfix;
#define UNIFI_MAX_FW_PATH_LEN       32
    char fw_name[UNIFI_MAX_FW_PATH_LEN];
    int r;

#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
    if (priv->mib_data.length) {
        vfree(priv->mib_data.data);
        priv->mib_data.data = NULL;
        priv->mib_data.length = 0;
    }
#endif /* CSR_SUPPORT_SME && CSR_SUPPORT_WEXT*/

    postfix = priv->instance;

    if (is_fw == UNIFI_FW_STA) {
        /* Free kernel buffer and reload */
        uf_release_firmware(priv, &priv->fw_sta);
#ifdef CSR_WIFI_SPLIT_PATCH
        scnprintf(fw_name, UNIFI_MAX_FW_PATH_LEN, "unifi-sdio-%d/%s",
                  postfix, (is_ap_mode(priv) ? "ap.xbv" : "staonly.xbv") );
#else
        scnprintf(fw_name, UNIFI_MAX_FW_PATH_LEN, "unifi-sdio-%d/%s",
                  postfix, "sta.xbv" );
#endif
        r = request_firmware(&fw_entry, fw_name, priv->unifi_device);
        if (r == 0) {
            priv->fw_sta.dl_data = fw_entry->data;
            priv->fw_sta.dl_len = fw_entry->size;
            priv->fw_sta.fw_desc = (void *)fw_entry;
        } else {
            unifi_trace(priv, UDBG2, "Firmware file not available\n");
        }
    }

    return 0;

} /* uf_request_firmware_files() */

/*
 * ---------------------------------------------------------------------------
 *  uf_release_firmware_files
 *
 *      Release all buffers used to store firmware files
 *
 *  Arguments:
 *      priv            Pointer to OS private struct.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
int uf_release_firmware_files(unifi_priv_t *priv)
{
    uf_release_firmware(priv, &priv->fw_sta);

    return 0;
}

/*
 * ---------------------------------------------------------------------------
 *  uf_release_firmware
 *
 *      Release specific buffer used to store firmware
 *
 *  Arguments:
 *      priv            Pointer to OS private struct.
 *      to_free         Pointer to specific buffer to release
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
int uf_release_firmware(unifi_priv_t *priv, struct dlpriv *to_free)
{
    if (to_free != NULL) {
        release_firmware((const struct firmware *)to_free->fw_desc);
        to_free->fw_desc = NULL;
        to_free->dl_data = NULL;
        to_free->dl_len = 0;
    }
    return 0;
}
