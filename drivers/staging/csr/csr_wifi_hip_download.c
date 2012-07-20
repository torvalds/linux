/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 * FILE: csr_wifi_hip_download.c
 *
 * PURPOSE:
 *      Routines for downloading firmware to UniFi.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_unifiversion.h"
#include "csr_wifi_hip_card.h"
#include "csr_wifi_hip_xbv.h"

#undef CSR_WIFI_IGNORE_PATCH_VERSION_MISMATCH

static CsrResult do_patch_download(card_t *card, void *dlpriv,
                                   xbv1_t *pfwinfo, CsrUint32 boot_ctrl_addr);

static CsrResult do_patch_convert_download(card_t *card,
                                           void *dlpriv, xbv1_t *pfwinfo);

/*
 * ---------------------------------------------------------------------------
 *  _find_in_slut
 *
 *      Find the offset of the appropriate object in the SLUT of a card
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      psym            Pointer to symbol object.
 *                         id set up by caller
 *                         obj will be set up by this function
 *      pslut           Pointer to SLUT address, if 0xffffffff then it must be
 *                         read from the chip.
 *  Returns:
 *      CSR_RESULT_SUCCESS on success
 *      Non-zero on error,
 *      CSR_WIFI_HIP_RESULT_NOT_FOUND if not found
 * ---------------------------------------------------------------------------
 */
static CsrResult _find_in_slut(card_t *card, symbol_t *psym, CsrUint32 *pslut)
{
    CsrUint32 slut_address;
    CsrUint16 finger_print;
    CsrResult r;
    CsrResult csrResult;

    /* Get SLUT address */
    if (*pslut == 0xffffffff)
    {
        r = card_wait_for_firmware_to_start(card, &slut_address);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Firmware hasn't started\n");
            func_exit_r(r);
            return r;
        }
        *pslut = slut_address;

        /*
         * Firmware has started so set the SDIO bus clock to the initial speed,
         * faster than UNIFI_SDIO_CLOCK_SAFE_HZ, to speed up the f/w download.
         */
        csrResult = CsrSdioMaxBusClockFrequencySet(card->sdio_if, UNIFI_SDIO_CLOCK_INIT_HZ);
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            r = ConvertCsrSdioToCsrHipResult(card, csrResult);
            func_exit_r(r);
            return r;
        }
        card->sdio_clock_speed = UNIFI_SDIO_CLOCK_INIT_HZ;
    }
    else
    {
        slut_address = *pslut;  /* Use previously discovered address */
    }
    unifi_trace(card->ospriv, UDBG4, "SLUT addr: 0x%lX\n", slut_address);

    /*
     * Check the SLUT fingerprint.
     * The slut_address is a generic pointer so we must use unifi_card_read16().
     */
    unifi_trace(card->ospriv, UDBG4, "Looking for SLUT finger print\n");
    finger_print = 0;
    r = unifi_card_read16(card, slut_address, &finger_print);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to read SLUT finger print\n");
        func_exit_r(r);
        return r;
    }

    if (finger_print != SLUT_FINGERPRINT)
    {
        unifi_error(card->ospriv, "Failed to find SLUT fingerprint\n");
        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
    }

    /* Symbol table starts imedately after the fingerprint */
    slut_address += 2;

    while (1)
    {
        CsrUint16 id;
        CsrUint32 obj;

        r = unifi_card_read16(card, slut_address, &id);
        if (r != CSR_RESULT_SUCCESS)
        {
            func_exit_r(r);
            return r;
        }
        slut_address += 2;

        if (id == CSR_SLT_END)
        {
            /* End of table reached: not found */
            r = CSR_WIFI_HIP_RESULT_RANGE;
            break;
        }

        r = unifi_read32(card, slut_address, &obj);
        if (r != CSR_RESULT_SUCCESS)
        {
            func_exit_r(r);
            return r;
        }
        slut_address += 4;

        unifi_trace(card->ospriv, UDBG3, "  found SLUT id %02d.%08lx\n", id, obj);

        r = CSR_WIFI_HIP_RESULT_NOT_FOUND;
        /* Found search term? */
        if (id == psym->id)
        {
            unifi_trace(card->ospriv, UDBG1, " matched SLUT id %02d.%08lx\n", id, obj);
            psym->obj = obj;
            r = CSR_RESULT_SUCCESS;
            break;
        }
    }

    func_exit_r(r);
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  do_patch_convert_download
 *
 *      Download the given firmware image to the UniFi, converting from FWDL
 *      to PTDL XBV format.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      dlpriv          Pointer to source firmware image
 *      fwinfo          Pointer to source firmware info struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on error
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
static CsrResult do_patch_convert_download(card_t *card, void *dlpriv, xbv1_t *pfwinfo)
{
    CsrResult r;
    CsrUint32 slut_base = 0xffffffff;
    void *pfw;
    CsrUint32 psize;
    symbol_t sym;

    /* Reset the chip to guarantee that the ROM loader is running */
    r = unifi_init(card);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv,
                    "do_patch_convert_download: failed to re-init UniFi\n");
        return r;
    }

    /* If no unifi_helper is running, the firmware version must be read */
    if (card->build_id == 0)
    {
        CsrUint32 ver = 0;
        sym.id = CSR_SLT_BUILD_ID_NUMBER;
        sym.obj = 0; /* To be updated by _find_in_slut() */

        unifi_trace(card->ospriv, UDBG1, "Need f/w version\n");

        /* Find chip build id entry in SLUT */
        r = _find_in_slut(card, &sym, &slut_base);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to find CSR_SLT_BUILD_ID_NUMBER\n");
            return CSR_RESULT_FAILURE;
        }

        /* Read running f/w version */
        r = unifi_read32(card, sym.obj, &ver);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to read f/w id\n");
            return CSR_RESULT_FAILURE;
        }
        card->build_id = ver;
    }

    /* Convert the ptest firmware to a patch against the running firmware */
    pfw = xbv_to_patch(card, unifi_fw_read, dlpriv, pfwinfo, &psize);
    if (!pfw)
    {
        unifi_error(card->ospriv, "Failed to convert f/w to patch");
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }
    else
    {
        void *desc;
        sym.id = CSR_SLT_BOOT_LOADER_CONTROL;
        sym.obj = 0; /* To be updated by _find_in_slut() */

        /* Find boot loader control entry in SLUT */
        r = _find_in_slut(card, &sym, &slut_base);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to find BOOT_LOADER_CONTROL\n");
            return CSR_RESULT_FAILURE;
        }

        r = unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to wake UniFi\n");
        }

        /* Get a dlpriv for the patch buffer so that unifi_fw_read() can
         * access it.
         */
        desc = unifi_fw_open_buffer(card->ospriv, pfw, psize);
        if (!desc)
        {
            return CSR_WIFI_HIP_RESULT_NO_MEMORY;
        }

        /* Download the patch */
        unifi_info(card->ospriv, "Downloading converted f/w as patch\n");
        r = unifi_dl_patch(card, desc, sym.obj);
        CsrMemFree(pfw);
        unifi_fw_close_buffer(card->ospriv, desc);

        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Converted patch download failed\n");
            func_exit_r(r);
            return r;
        }
        else
        {
            unifi_trace(card->ospriv, UDBG1, "Converted patch downloaded\n");
        }

        /* This command starts the firmware */
        r = unifi_do_loader_op(card, sym.obj + 6, UNIFI_BOOT_LOADER_RESTART);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to write loader restart cmd\n");
        }

        func_exit_r(r);
        return r;
    }
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_dl_firmware
 *
 *      Download the given firmware image to the UniFi.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      dlpriv          A context pointer from the calling function to be
 *                      passed when calling unifi_fw_read().
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success,
 *      CSR_WIFI_HIP_RESULT_NO_MEMORY         memory allocation failed
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         error in XBV file
 *      CSR_RESULT_FAILURE            SDIO error
 *
 *  Notes:
 *      Stops and resets the chip, does the download and runs the new
 *      firmware.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_dl_firmware(card_t *card, void *dlpriv)
{
    xbv1_t *fwinfo;
    CsrResult r;

    func_enter();

    fwinfo = CsrMemAlloc(sizeof(xbv1_t));
    if (fwinfo == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for firmware\n");
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }

    /*
     * Scan the firmware file to find the TLVs we are interested in.
     * These are:
     *   - check we support the file format version in VERF
     *   - SLTP Symbol Lookup Table Pointer
     *   - FWDL firmware download segments
     *   - FWOV firmware overlay segment
     *   - VMEQ Register probe tests to verify matching h/w
     */
    r = xbv1_parse(card, unifi_fw_read, dlpriv, fwinfo);
    if (r != CSR_RESULT_SUCCESS || fwinfo->mode != xbv_firmware)
    {
        unifi_error(card->ospriv, "File type is %s, expected firmware.\n",
                    fwinfo->mode == xbv_patch?"patch" : "unknown");
        CsrMemFree(fwinfo);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    /* UF6xxx doesn't accept firmware, only patches. Therefore we convert
     * the file to patch format with version numbers matching the current
     * running firmware, and then download via the patch mechanism.
     * The sole purpose of this is to support production test firmware across
     * different ROM releases, the test firmware being provided in non-patch
     * format.
     */
    if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
    {
        unifi_info(card->ospriv, "Must convert f/w to patch format\n");
        r = do_patch_convert_download(card, dlpriv, fwinfo);
    }
    else
    {
        /* Older UniFi chips allowed firmware to be directly loaded onto the
         * chip, which is no longer supported.
         */
        unifi_error(card->ospriv, "Only patch downloading supported\n");
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    CsrMemFree(fwinfo);
    func_exit_r(r);
    return r;
} /* unifi_dl_firmware() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_dl_patch
 *
 *      Load the given patch set into UniFi.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      dlpriv          The os specific handle to the firmware file.
 *      boot_ctrl       The address of the boot loader control structure.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success,
 *      CSR_WIFI_HIP_RESULT_NO_MEMORY         memory allocation failed
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         error in XBV file
 *      CSR_RESULT_FAILURE            SDIO error
 *
 *  Notes:
 *      This ends up telling UniFi to restart.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_dl_patch(card_t *card, void *dlpriv, CsrUint32 boot_ctrl)
{
    xbv1_t *fwinfo;
    CsrResult r;

    func_enter();

    unifi_info(card->ospriv, "unifi_dl_patch %p %08x\n", dlpriv, boot_ctrl);

    fwinfo = CsrMemAlloc(sizeof(xbv1_t));
    if (fwinfo == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for patches\n");
        func_exit();
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }

    /*
     * Scan the firmware file to find the TLVs we are interested in.
     * These are:
     *   - check we support the file format version in VERF
     *   - FWID The build ID of the ROM that we can patch
     *   - PTDL patch download segments
     */
    r = xbv1_parse(card, unifi_fw_read, dlpriv, fwinfo);
    if (r != CSR_RESULT_SUCCESS || fwinfo->mode != xbv_patch)
    {
        CsrMemFree(fwinfo);
        unifi_error(card->ospriv, "Failed to read in patch file\n");
        func_exit();
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    /*
     * We have to check the build id read from the SLUT against that
     * for the patch file.  They have to match exactly.
     *    "card->build_id" == XBV1.PTCH.FWID
     */
    if (card->build_id != fwinfo->build_id)
    {
        unifi_error(card->ospriv, "Wrong patch file for chip (chip = %lu, file = %lu)\n",
                    card->build_id, fwinfo->build_id);
        CsrMemFree(fwinfo);
#ifndef CSR_WIFI_IGNORE_PATCH_VERSION_MISMATCH
        func_exit();
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
#else
        fwinfo = NULL;
        dlpriv = NULL;
        return CSR_RESULT_SUCCESS;
#endif
    }

    r = do_patch_download(card, dlpriv, fwinfo, boot_ctrl);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to patch image\n");
    }

    CsrMemFree(fwinfo);

    func_exit_r(r);
    return r;
} /* unifi_dl_patch() */


void* unifi_dl_fw_read_start(card_t *card, s8 is_fw)
{
    card_info_t card_info;

    unifi_card_info(card, &card_info);
    unifi_trace(card->ospriv, UDBG5,
                "id=%d, ver=0x%x, fw_build=%u, fw_hip=0x%x, block_size=%d\n",
                card_info.chip_id, card_info.chip_version,
                card_info.fw_build, card_info.fw_hip_version,
                card_info.sdio_block_size);

    return unifi_fw_read_start(card->ospriv, is_fw, &card_info);
}


/*
 * ---------------------------------------------------------------------------
 *  safe_read_shared_location
 *
 *      Read a shared memory location repeatedly until we get two readings
 *      the same.
 *
 *  Arguments:
 *      card            Pointer to card context struct.
 *      unifi_addr      UniFi shared-data-memory address to access.
 *      pdata           Pointer to a byte variable for the value read.
 *
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on failure
 * ---------------------------------------------------------------------------
 */
static CsrResult safe_read_shared_location(card_t *card, CsrUint32 address, u8 *pdata)
{
    CsrResult r;
    CsrUint16 limit = 1000;
    u8 b, b2;

    *pdata = 0;

    r = unifi_read_8_or_16(card, address, &b);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    while (limit--)
    {
        r = unifi_read_8_or_16(card, address, &b2);
        if (r != CSR_RESULT_SUCCESS)
        {
            return r;
        }

        /* When we have a stable value, return it */
        if (b == b2)
        {
            *pdata = b;
            return CSR_RESULT_SUCCESS;
        }

        b = b2;
    }

    return CSR_RESULT_FAILURE;
} /* safe_read_shared_location() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_do_loader_op
 *
 *      Send a loader / boot_loader command to the UniFi and wait for
 *      it to complete.
 *
 *  Arguments:
 *      card            Pointer to card context struct.
 *      op_addr         The address of the loader operation control word.
 *      opcode          The operation to perform.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS    on success
 *      CSR_RESULT_FAILURE    SDIO error or SDIO/XAP timeout
 * ---------------------------------------------------------------------------
 */

/*
 * Ideally instead of sleeping, we want to busy wait.
 * Currently there is no framework API to do this. When it becomes available,
 * we can use it to busy wait using usecs
 */
#define OPERATION_TIMEOUT_LOOPS (100)  /* when OPERATION_TIMEOUT_DELAY==1, (500) otherwise */
#define OPERATION_TIMEOUT_DELAY 1      /* msec, or 200usecs */

CsrResult unifi_do_loader_op(card_t *card, CsrUint32 op_addr, u8 opcode)
{
    CsrResult r;
    CsrInt16 op_retries;

    unifi_trace(card->ospriv, UDBG4, "Loader cmd 0x%0x -> 0x%08x\n", opcode, op_addr);

    /* Set the Operation command byte to the opcode */
    r = unifi_write_8_or_16(card, op_addr, opcode);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write loader copy command\n");
        return r;
    }

    /* Wait for Operation command byte to be Idle */
    /* Typically takes ~100us */
    op_retries = 0;
    r = CSR_RESULT_SUCCESS;
    while (1)
    {
        u8 op;

        /*
         * Read the memory location until two successive reads give
         * the same value.
         * Then handle it.
         */
        r = safe_read_shared_location(card, op_addr, &op);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to read loader status\n");
            break;
        }

        if (op == UNIFI_LOADER_IDLE)
        {
            /* Success */
            break;
        }

        if (op != opcode)
        {
            unifi_error(card->ospriv, "Error reported by loader: 0x%X\n", op);
            r = CSR_RESULT_FAILURE;
            break;
        }

        /* Allow 500us timeout */
        if (++op_retries >= OPERATION_TIMEOUT_LOOPS)
        {
            unifi_error(card->ospriv, "Timeout waiting for loader to ack transfer\n");
            /* Stop XAPs to aid post-mortem */
            r = unifi_card_stop_processor(card, UNIFI_PROC_BOTH);
            if (r != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "Failed to stop UniFi processors\n");
            }
            else
            {
                r = CSR_RESULT_FAILURE;
            }
            break;
        }
        CsrThreadSleep(OPERATION_TIMEOUT_DELAY);
    } /* Loop exits with r != CSR_RESULT_SUCCESS on error */

    return r;
}     /* unifi_do_loader_op() */


/*
 * ---------------------------------------------------------------------------
 *  send_ptdl_to_unifi
 *
 *      Copy a patch block from userland to the UniFi.
 *      This function reads data, 2K at a time, from userland and writes
 *      it to the UniFi.
 *
 *  Arguments:
 *      card            A pointer to the card structure
 *      dlpriv          The os specific handle for the firmware file
 *      ptdl            A pointer ot the PTDL block
 *      handle          The buffer handle to use for the xfer
 *      op_addr         The address of the loader operation control word
 *
 *  Returns:
 *      Number of bytes sent (Positive) or negative value indicating
 *      error code:
 *      CSR_WIFI_HIP_RESULT_NO_MEMORY         memory allocation failed
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         error in XBV file
 *      CSR_RESULT_FAILURE            SDIO error
 * ---------------------------------------------------------------------------
 */
static CsrResult send_ptdl_to_unifi(card_t *card, void *dlpriv,
                                    const struct PTDL *ptdl, CsrUint32 handle,
                                    CsrUint32 op_addr)
{
    CsrUint32 offset;
    u8 *buf;
    CsrInt32 data_len;
    CsrUint32 write_len;
    CsrResult r;
    const CsrUint16 buf_size = 2 * 1024;

    offset = ptdl->dl_offset;
    data_len = ptdl->dl_size;

    if (data_len > buf_size)
    {
        unifi_error(card->ospriv, "PTDL block is too large (%u)\n",
                    ptdl->dl_size);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    buf = CsrMemAllocDma(buf_size);
    if (buf == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate transfer buffer for firmware download\n");
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }

    r = CSR_RESULT_SUCCESS;

    if (unifi_fw_read(card->ospriv, dlpriv, offset, buf, data_len) != data_len)
    {
        unifi_error(card->ospriv, "Failed to read from file\n");
    }
    else
    {
        /* We can always round these if the host wants to */
        if (card->sdio_io_block_pad)
        {
            write_len = (data_len + (card->sdio_io_block_size - 1)) &
                        ~(card->sdio_io_block_size - 1);

            /* Zero out the rest of the buffer (This isn't needed, but it
             * makes debugging things later much easier). */
            CsrMemSet(buf + data_len, 0, write_len - data_len);
        }
        else
        {
            write_len = data_len;
        }

        r = unifi_bulk_rw_noretry(card, handle, buf, write_len, UNIFI_SDIO_WRITE);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "CMD53 failed writing %d bytes to handle %ld\n",
                        data_len, handle);
        }
        else
        {
            /*
             * Can change the order of things to overlap read from file
             * with copy to unifi
             */
            r = unifi_do_loader_op(card, op_addr, UNIFI_BOOT_LOADER_PATCH);
        }
    }

    CsrMemFreeDma(buf);

    if (r != CSR_RESULT_SUCCESS && r != CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        unifi_error(card->ospriv, "Failed to copy block of %u bytes to UniFi\n",
                    ptdl->dl_size);
    }

    return r;
} /* send_ptdl_to_unifi() */


/*
 * ---------------------------------------------------------------------------
 *  do_patch_download
 *
 *      This function downloads a set of patches to UniFi and then
 *      causes it to restart.
 *
 *  Arguments:
 *      card            Pointer to card struct.
 *      dlpriv          A context pointer from the calling function to be
 *                      used when reading the XBV file.  This can be NULL
 *                      in which case not patches are applied.
 *      pfwinfo         Pointer to a fwinfo struct describing the f/w
 *                      XBV file.
 *      boot_ctrl_addr  The address of the boot loader control structure.
 *
 *  Returns:
 *      0 on success, or an error code
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE for a bad laoader version number
 * ---------------------------------------------------------------------------
 */
static CsrResult do_patch_download(card_t *card, void *dlpriv, xbv1_t *pfwinfo, CsrUint32 boot_ctrl_addr)
{
    CsrResult r;
    CsrInt32 i;
    CsrUint16 loader_version;
    CsrUint16 handle;
    CsrUint32 total_bytes;

    /*
     * Read info from the SDIO Loader Control Data Structure
     */
    /* Check the loader version */
    r = unifi_card_read16(card, boot_ctrl_addr, &loader_version);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Patch download: Failed to read loader version\n");
        return r;
    }
    unifi_trace(card->ospriv, UDBG2, "Patch download: boot loader version 0x%04X\n", loader_version);
    switch (loader_version)
    {
        case 0x0000:
            break;

        default:
            unifi_error(card->ospriv, "Patch loader version (0x%04X) is not supported by this driver\n",
                        loader_version);
            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    /* Retrieve the handle to use with CMD53 */
    r = unifi_card_read16(card, boot_ctrl_addr + 4, &handle);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Patch download: Failed to read loader handle\n");
        return r;
    }

    /* Set the mask of LEDs to flash */
    if (card->loader_led_mask)
    {
        r = unifi_card_write16(card, boot_ctrl_addr + 2,
                               (CsrUint16)card->loader_led_mask);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Patch download: Failed to write LED mask\n");
            return r;
        }
    }

    total_bytes = 0;

    /* Copy download data to UniFi memory */
    for (i = 0; i < pfwinfo->num_ptdl; i++)
    {
        unifi_trace(card->ospriv, UDBG3, "Patch download: %d Downloading for %d from offset %d\n",
                    i,
                    pfwinfo->ptdl[i].dl_size,
                    pfwinfo->ptdl[i].dl_offset);

        r = send_ptdl_to_unifi(card, dlpriv, &pfwinfo->ptdl[i],
                               handle, boot_ctrl_addr + 6);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Patch failed after %u bytes\n",
                        total_bytes);
            return r;
        }
        total_bytes += pfwinfo->ptdl[i].dl_size;
    }

    return CSR_RESULT_SUCCESS;
} /* do_patch_download() */


