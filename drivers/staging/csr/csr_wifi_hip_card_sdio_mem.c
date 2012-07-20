/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 * FILE: csr_wifi_hip_card_sdio_mem.c
 *
 * PURPOSE: Implementation of the Card API for SDIO.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_card.h"

#define SDIO_RETRIES    3
#define CSR_WIFI_HIP_SDIO_TRACE_DATA_LENGTH 16


#define retryable_sdio_error(_csrResult) (((_csrResult) == CSR_SDIO_RESULT_CRC_ERROR) || ((_csrResult) == CSR_SDIO_RESULT_TIMEOUT))


/*
 * ---------------------------------------------------------------------------
 *  retrying_read8
 *  retrying_write8
 *
 *      These functions provide the first level of retry for SDIO operations.
 *      If an SDIO command fails for reason of a response timeout or CRC
 *      error, it is retried immediately. If three attempts fail we report a
 *      failure.
 *      If the command failed for any other reason, the failure is reported
 *      immediately.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      funcnum         The SDIO function to access.
 *                      Function 0 is the Card Configuration Register space,
 *                      function 1/2 is the UniFi register space.
 *      addr            Address to access
 *      pdata           Pointer in which to return the value read.
 *      data            Value to write.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS  on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
static CsrResult retrying_read8(card_t *card, CsrInt16 funcnum, CsrUint32 addr, u8 *pdata)
{
    CsrSdioFunction *sdio = card->sdio_if;
    CsrResult r = CSR_RESULT_SUCCESS;
    CsrInt16 retries;
    CsrResult csrResult = CSR_RESULT_SUCCESS;

    retries = 0;
    while (retries++ < SDIO_RETRIES)
    {
        if (funcnum == 0)
        {
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
            unifi_debug_log_to_buf("r0@%02X", addr);
#endif
            csrResult = CsrSdioF0Read8(sdio, addr, pdata);
        }
        else
        {
#ifdef CSR_WIFI_TRANSPORT_CSPI
            unifi_error(card->ospriv,
                        "retrying_read_f0_8: F1 8-bit reads are not allowed.\n");
            return CSR_RESULT_FAILURE;
#else
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
            unifi_debug_log_to_buf("r@%02X", addr);
#endif
            csrResult = CsrSdioRead8(sdio, addr, pdata);
#endif
        }
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            unifi_debug_log_to_buf("error=%X\n", csrResult);
        }
        else
        {
            unifi_debug_log_to_buf("=%X\n", *pdata);
        }
#endif
        if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
        {
            return CSR_WIFI_HIP_RESULT_NO_DEVICE;
        }
        /*
         * Try again for retryable (CRC or TIMEOUT) errors,
         * break on success or fatal error
         */
        if (!retryable_sdio_error(csrResult))
        {
#ifdef CSR_WIFI_HIP_DATA_PLANE_PROFILE
            card->cmd_prof.cmd52_count++;
#endif
            break;
        }
        unifi_trace(card->ospriv, UDBG2, "retryable SDIO error reading F%d 0x%lX\n", funcnum, addr);
    }

    if ((csrResult == CSR_RESULT_SUCCESS) && (retries > 1))
    {
        unifi_warning(card->ospriv, "Read succeeded after %d attempts\n", retries);
    }

    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to read from UniFi (addr 0x%lX) after %d tries\n",
                    addr, retries - 1);
        /* Report any SDIO error as a general i/o error */
        r = CSR_RESULT_FAILURE;
    }

    return r;
} /* retrying_read8() */


static CsrResult retrying_write8(card_t *card, CsrInt16 funcnum, CsrUint32 addr, u8 data)
{
    CsrSdioFunction *sdio = card->sdio_if;
    CsrResult r = CSR_RESULT_SUCCESS;
    CsrInt16 retries;
    CsrResult csrResult = CSR_RESULT_SUCCESS;

    retries = 0;
    while (retries++ < SDIO_RETRIES)
    {
        if (funcnum == 0)
        {
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
            unifi_debug_log_to_buf("w0@%02X=%X", addr, data);
#endif
            csrResult = CsrSdioF0Write8(sdio, addr, data);
        }
        else
        {
#ifdef CSR_WIFI_TRANSPORT_CSPI
            unifi_error(card->ospriv,
                        "retrying_write_f0_8: F1 8-bit writes are not allowed.\n");
            return CSR_RESULT_FAILURE;
#else
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
            unifi_debug_log_to_buf("w@%02X=%X", addr, data);
#endif
            csrResult = CsrSdioWrite8(sdio, addr, data);
#endif
        }
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            unifi_debug_log_to_buf(",error=%X", csrResult);
        }
        unifi_debug_string_to_buf("\n");
#endif
        if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
        {
            return CSR_WIFI_HIP_RESULT_NO_DEVICE;
        }
        /*
         * Try again for retryable (CRC or TIMEOUT) errors,
         * break on success or fatal error
         */
        if (!retryable_sdio_error(csrResult))
        {
#ifdef CSR_WIFI_HIP_DATA_PLANE_PROFILE
            card->cmd_prof.cmd52_count++;
#endif
            break;
        }
        unifi_trace(card->ospriv, UDBG2, "retryable SDIO error writing %02X to F%d 0x%lX\n",
                    data, funcnum, addr);
    }

    if ((csrResult == CSR_RESULT_SUCCESS) && (retries > 1))
    {
        unifi_warning(card->ospriv, "Write succeeded after %d attempts\n", retries);
    }

    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write to UniFi (addr 0x%lX) after %d tries\n",
                    addr, retries - 1);
        /* Report any SDIO error as a general i/o error */
        r = CSR_RESULT_FAILURE;
    }

    return r;
} /* retrying_write8() */


static CsrResult retrying_read16(card_t *card, CsrInt16 funcnum,
                                 CsrUint32 addr, u16 *pdata)
{
    CsrSdioFunction *sdio = card->sdio_if;
    CsrResult r = CSR_RESULT_SUCCESS;
    CsrInt16 retries;
    CsrResult csrResult = CSR_RESULT_SUCCESS;

    retries = 0;
    while (retries++ < SDIO_RETRIES)
    {
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        unifi_debug_log_to_buf("r@%02X", addr);
#endif
        csrResult = CsrSdioRead16(sdio, addr, pdata);
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            unifi_debug_log_to_buf("error=%X\n", csrResult);
        }
        else
        {
            unifi_debug_log_to_buf("=%X\n", *pdata);
        }
#endif
        if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
        {
            return CSR_WIFI_HIP_RESULT_NO_DEVICE;
        }

        /*
         * Try again for retryable (CRC or TIMEOUT) errors,
         * break on success or fatal error
         */
        if (!retryable_sdio_error(csrResult))
        {
#ifdef CSR_WIFI_HIP_DATA_PLANE_PROFILE
            card->cmd_prof.cmd52_count++;
#endif
            break;
        }
        unifi_trace(card->ospriv, UDBG2, "retryable SDIO error reading F%d 0x%lX\n", funcnum, addr);
    }

    if ((csrResult == CSR_RESULT_SUCCESS) && (retries > 1))
    {
        unifi_warning(card->ospriv, "Read succeeded after %d attempts\n", retries);
    }

    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to read from UniFi (addr 0x%lX) after %d tries\n",
                    addr, retries - 1);
        /* Report any SDIO error as a general i/o error */
        r = CSR_RESULT_FAILURE;
    }

    return r;
} /* retrying_read16() */


static CsrResult retrying_write16(card_t *card, CsrInt16 funcnum,
                                  CsrUint32 addr, u16 data)
{
    CsrSdioFunction *sdio = card->sdio_if;
    CsrResult r = CSR_RESULT_SUCCESS;
    CsrInt16 retries;
    CsrResult csrResult = CSR_RESULT_SUCCESS;

    retries = 0;
    while (retries++ < SDIO_RETRIES)
    {
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        unifi_debug_log_to_buf("w@%02X=%X", addr, data);
#endif
        csrResult = CsrSdioWrite16(sdio, addr, data);
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            unifi_debug_log_to_buf(",error=%X", csrResult);
        }
        unifi_debug_string_to_buf("\n");
#endif
        if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
        {
            return CSR_WIFI_HIP_RESULT_NO_DEVICE;
        }

        /*
         * Try again for retryable (CRC or TIMEOUT) errors,
         * break on success or fatal error
         */
        if (!retryable_sdio_error(csrResult))
        {
#ifdef CSR_WIFI_HIP_DATA_PLANE_PROFILE
            card->cmd_prof.cmd52_count++;
#endif
            break;
        }
        unifi_trace(card->ospriv, UDBG2, "retryable SDIO error writing %02X to F%d 0x%lX\n",
                    data, funcnum, addr);
    }

    if ((csrResult == CSR_RESULT_SUCCESS) && (retries > 1))
    {
        unifi_warning(card->ospriv, "Write succeeded after %d attempts\n", retries);
    }

    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write to UniFi (addr 0x%lX) after %d tries\n",
                    addr, retries - 1);
        /* Report any SDIO error as a general i/o error */
        r = CSR_RESULT_FAILURE;
    }

    return r;
} /* retrying_write16() */


/*
 * ---------------------------------------------------------------------------
 *  sdio_read_f0
 *
 *      Reads a byte value from the CCCR (func 0) area of UniFi.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to read from
 *      pdata   Pointer in which to store the read value.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
CsrResult sdio_read_f0(card_t *card, CsrUint32 addr, u8 *pdata)
{
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_f0_r_count++;
#endif
    return retrying_read8(card, 0, addr, pdata);
} /* sdio_read_f0() */


/*
 * ---------------------------------------------------------------------------
 *  sdio_write_f0
 *
 *      Writes a byte value to the CCCR (func 0) area of UniFi.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to read from
 *      data    Data value to write.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
CsrResult sdio_write_f0(card_t *card, CsrUint32 addr, u8 data)
{
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_f0_w_count++;
#endif
    return retrying_write8(card, 0, addr, data);
} /* sdio_write_f0() */


/*
 * ---------------------------------------------------------------------------
 * unifi_read_direct_8_or_16
 *
 *      Read a 8-bit value from the UniFi SDIO interface.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to read from
 *      pdata   Pointer in which to return data.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_read_direct_8_or_16(card_t *card, CsrUint32 addr, u8 *pdata)
{
#ifdef CSR_WIFI_TRANSPORT_CSPI
    u16 w;
    CsrResult r;

    r = retrying_read16(card, card->function, addr, &w);
    *pdata = (u8)(w & 0xFF);
    return r;
#else
    return retrying_read8(card, card->function, addr, pdata);
#endif
} /* unifi_read_direct_8_or_16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_write_direct_8_or_16
 *
 *      Write a byte value to the UniFi SDIO interface.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to write to
 *      data    Value to write.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error
 *
 *  Notes:
 *      If 8-bit write is used, the even address *must* be written second.
 *      This is because writes to odd bytes are cached and not committed
 *      to memory until the preceding even address is written.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_write_direct_8_or_16(card_t *card, CsrUint32 addr, u8 data)
{
    if (addr & 1)
    {
        unifi_warning(card->ospriv,
                      "Warning: Byte write to an odd address (0x%lX) is dangerous\n",
                      addr);
    }

#ifdef CSR_WIFI_TRANSPORT_CSPI
    return retrying_write16(card, card->function, addr, (u16)data);
#else
    return retrying_write8(card, card->function, addr, data);
#endif
} /* unifi_write_direct_8_or_16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_read_direct16
 *
 *      Read a 16-bit value from the UniFi SDIO interface.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to read from
 *      pdata   Pointer in which to return data.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      The even address *must* be read first. This is because reads from
 *      odd bytes are cached and read from memory when the preceding
 *      even address is read.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_read_direct16(card_t *card, CsrUint32 addr, u16 *pdata)
{
    return retrying_read16(card, card->function, addr, pdata);
} /* unifi_read_direct16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_write_direct16
 *
 *      Write a 16-bit value to the UniFi SDIO interface.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to write to
 *      data    Value to write.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      The even address *must* be written second. This is because writes to
 *      odd bytes are cached and not committed to memory until the preceding
 *      even address is written.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_write_direct16(card_t *card, CsrUint32 addr, u16 data)
{
    return retrying_write16(card, card->function, addr, data);
} /* unifi_write_direct16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_read_direct32
 *
 *      Read a 32-bit value from the UniFi SDIO interface.
 *
 *  Arguments:
 *      card    Pointer to card structure.
 *      addr    Address to read from
 *      pdata   Pointer in which to return data.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_read_direct32(card_t *card, CsrUint32 addr, CsrUint32 *pdata)
{
    CsrResult r;
    u16 w0, w1;

    r = retrying_read16(card, card->function, addr, &w0);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    r = retrying_read16(card, card->function, addr + 2, &w1);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    *pdata = ((CsrUint32)w1 << 16) | (CsrUint32)w0;

    return CSR_RESULT_SUCCESS;
} /* unifi_read_direct32() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_read_directn_match
 *
 *      Read multiple 8-bit values from the UniFi SDIO interface,
 *      stopping when either we have read 'len' bytes or we have read
 *      a octet equal to 'match'.  If 'match' is not a valid octet
 *      then this function is the same as 'unifi_read_directn'.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      addr            Start address to read from.
 *      pdata           Pointer to which to write data.
 *      len             Maximum umber of bytes to read
 *      match           The value to stop reading at.
 *      num             Pointer to buffer to write number of bytes read
 *
 *  Returns:
 *      number of octets read on success, negative error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      The even address *must* be read first. This is because reads from
 *      odd bytes are cached and read from memory when the preceding
 *      even address is read.
 * ---------------------------------------------------------------------------
 */
static CsrResult unifi_read_directn_match(card_t *card, CsrUint32 addr, void *pdata, u16 len, s8 m, CsrUint32 *num)
{
    CsrResult r;
    CsrUint32 i;
    u8 *cptr;
    u16 w;

    *num = 0;

    cptr = (u8 *)pdata;
    for (i = 0; i < len; i += 2)
    {
        r = retrying_read16(card, card->function, addr, &w);
        if (r != CSR_RESULT_SUCCESS)
        {
            return r;
        }

        *cptr++ = ((u8)w & 0xFF);
        if ((m >= 0) && (((s8)w & 0xFF) == m))
        {
            break;
        }

        if (i + 1 == len)
        {
            /* The len is odd. Ignore the last high byte */
            break;
        }

        *cptr++ = ((u8)(w >> 8) & 0xFF);
        if ((m >= 0) && (((s8)(w >> 8) & 0xFF) == m))
        {
            break;
        }

        addr += 2;
    }

    *num = (CsrInt32)(cptr - (u8 *)pdata);
    return CSR_RESULT_SUCCESS;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_read_directn
 *
 *      Read multiple 8-bit values from the UniFi SDIO interface.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      addr            Start address to read from.
 *      pdata           Pointer to which to write data.
 *      len             Number of bytes to read
 *
 *  Returns:
 *      0 on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      The even address *must* be read first. This is because reads from
 *      odd bytes are cached and read from memory when the preceding
 *      even address is read.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_read_directn(card_t *card, CsrUint32 addr, void *pdata, u16 len)
{
    CsrUint32 num;

    return unifi_read_directn_match(card, addr, pdata, len, -1, &num);
} /* unifi_read_directn() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_write_directn
 *
 *      Write multiple 8-bit values to the UniFi SDIO interface.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      addr            Start address to write to.
 *      pdata           Source data pointer.
 *      len             Number of bytes to write, must be even.
 *
 *  Returns:
 *      0 on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      The UniFi has a peculiar 16-bit bus architecture. Writes are only
 *      committed to memory when an even address is accessed. Writes to
 *      odd addresses are cached and only committed if the next write is
 *      to the preceding address.
 *      This means we must write data as pairs of bytes in reverse order.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_write_directn(card_t *card, CsrUint32 addr, void *pdata, u16 len)
{
    CsrResult r;
    u8 *cptr;
    CsrInt16 signed_len;

    cptr = (u8 *)pdata;
    signed_len = (CsrInt16)len;
    while (signed_len > 0)
    {
        /* This is UniFi-1 specific code. CSPI not supported so 8-bit write allowed */
        r = retrying_write16(card, card->function, addr, *cptr);
        if (r != CSR_RESULT_SUCCESS)
        {
            return r;
        }

        cptr += 2;
        addr += 2;
        signed_len -= 2;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_write_directn() */


/*
 * ---------------------------------------------------------------------------
 *  set_dmem_page
 *  set_pmem_page
 *
 *      Set up the page register for the shared data memory window or program
 *      memory window.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      dmem_addr       UniFi shared-data-memory address to access.
 *      pmem_addr       UniFi program memory address to access. This includes
 *                        External FLASH memory at    0x000000
 *                        Processor program memory at 0x200000
 *                        External SRAM at memory     0x400000
 *      paddr           Location to write an SDIO address (24-bit) for
 *                       use in a unifi_read_direct or unifi_write_direct call.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE card was ejected
 *      CSR_RESULT_FAILURE an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
static CsrResult set_dmem_page(card_t *card, CsrUint32 dmem_addr, CsrUint32 *paddr)
{
    u16 page, addr;
    CsrUint32 len;
    CsrResult r;

    *paddr = 0;

    if (!ChipHelper_DecodeWindow(card->helper,
                                 CHIP_HELPER_WINDOW_3,
                                 CHIP_HELPER_WT_SHARED,
                                 dmem_addr / 2,
                                 &page, &addr, &len))
    {
        unifi_error(card->ospriv, "Failed to decode SHARED_DMEM_PAGE %08lx\n", dmem_addr);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    if (page != card->dmem_page)
    {
        unifi_trace(card->ospriv, UDBG6, "setting dmem page=0x%X, addr=0x%lX\n", page, addr);

        /* change page register */
        r = unifi_write_direct16(card, ChipHelper_HOST_WINDOW3_PAGE(card->helper) * 2, page);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to write SHARED_DMEM_PAGE\n");
            return r;
        }

        card->dmem_page = page;
    }

    *paddr = ((CsrInt32)addr * 2) + (dmem_addr & 1);

    return CSR_RESULT_SUCCESS;
} /* set_dmem_page() */


static CsrResult set_pmem_page(card_t *card, CsrUint32 pmem_addr,
                               enum chip_helper_window_type mem_type, CsrUint32 *paddr)
{
    u16 page, addr;
    CsrUint32 len;
    CsrResult r;

    *paddr = 0;

    if (!ChipHelper_DecodeWindow(card->helper,
                                 CHIP_HELPER_WINDOW_2,
                                 mem_type,
                                 pmem_addr / 2,
                                 &page, &addr, &len))
    {
        unifi_error(card->ospriv, "Failed to decode PROG MEM PAGE %08lx %d\n", pmem_addr, mem_type);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    if (page != card->pmem_page)
    {
        unifi_trace(card->ospriv, UDBG6, "setting pmem page=0x%X, addr=0x%lX\n", page, addr);

        /* change page register */
        r = unifi_write_direct16(card, ChipHelper_HOST_WINDOW2_PAGE(card->helper) * 2, page);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to write PROG MEM PAGE\n");
            return r;
        }

        card->pmem_page = page;
    }

    *paddr = ((CsrInt32)addr * 2) + (pmem_addr & 1);

    return CSR_RESULT_SUCCESS;
} /* set_pmem_page() */


/*
 * ---------------------------------------------------------------------------
 *  set_page
 *
 *      Sets up the appropriate page register to access the given address.
 *      Returns the sdio address at which the unifi address can be accessed.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      generic_addr    UniFi internal address to access, in Generic Pointer
 *                      format, i.e. top byte is space indicator.
 *      paddr           Location to write page address
 *                          SDIO address (24-bit) for use in a unifi_read_direct or
 *                          unifi_write_direct call
 *
 *  Returns:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE  the address is invalid
 * ---------------------------------------------------------------------------
 */
static CsrResult set_page(card_t *card, CsrUint32 generic_addr, CsrUint32 *paddr)
{
    CsrInt32 space;
    CsrUint32 addr;
    CsrResult r = CSR_RESULT_SUCCESS;

    if (!paddr)
    {
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }
    *paddr = 0;
    space = UNIFI_GP_SPACE(generic_addr);
    addr = UNIFI_GP_OFFSET(generic_addr);
    switch (space)
    {
        case UNIFI_SH_DMEM:
            /* Shared Data Memory is accessed via the Shared Data Memory window */
            r = set_dmem_page(card, addr, paddr);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            break;

        case UNIFI_EXT_FLASH:
            if (!ChipHelper_HasFlash(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08lX (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            /* External FLASH is accessed via the Program Memory window */
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_FLASH, paddr);
            break;

        case UNIFI_EXT_SRAM:
            if (!ChipHelper_HasExtSram(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08l (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            /* External SRAM is accessed via the Program Memory window */
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_EXT_SRAM, paddr);
            break;

        case UNIFI_REGISTERS:
            /* Registers are accessed directly */
            *paddr = addr;
            break;

        case UNIFI_PHY_DMEM:
            r = unifi_set_proc_select(card, UNIFI_PROC_PHY);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            *paddr = ChipHelper_DATA_MEMORY_RAM_OFFSET(card->helper) * 2 + addr;
            break;

        case UNIFI_MAC_DMEM:
            r = unifi_set_proc_select(card, UNIFI_PROC_MAC);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            *paddr = ChipHelper_DATA_MEMORY_RAM_OFFSET(card->helper) * 2 + addr;
            break;

        case UNIFI_BT_DMEM:
            if (!ChipHelper_HasBt(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08lX (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            r = unifi_set_proc_select(card, UNIFI_PROC_BT);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            *paddr = ChipHelper_DATA_MEMORY_RAM_OFFSET(card->helper) * 2 + addr;
            break;

        case UNIFI_PHY_PMEM:
            r = unifi_set_proc_select(card, UNIFI_PROC_PHY);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_CODE_RAM, paddr);
            break;

        case UNIFI_MAC_PMEM:
            r = unifi_set_proc_select(card, UNIFI_PROC_MAC);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_CODE_RAM, paddr);
            break;

        case UNIFI_BT_PMEM:
            if (!ChipHelper_HasBt(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08lX (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            r = unifi_set_proc_select(card, UNIFI_PROC_BT);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_CODE_RAM, paddr);
            break;

        case UNIFI_PHY_ROM:
            if (!ChipHelper_HasRom(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08lX (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            r = unifi_set_proc_select(card, UNIFI_PROC_PHY);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_ROM, paddr);
            break;

        case UNIFI_MAC_ROM:
            if (!ChipHelper_HasRom(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08lX (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            r = unifi_set_proc_select(card, UNIFI_PROC_MAC);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_ROM, paddr);
            break;

        case UNIFI_BT_ROM:
            if (!ChipHelper_HasRom(card->helper) || !ChipHelper_HasBt(card->helper))
            {
                unifi_error(card->ospriv, "Bad address space for chip in generic pointer 0x%08lX (helper=0x%x)\n",
                            generic_addr, card->helper);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            r = unifi_set_proc_select(card, UNIFI_PROC_BT);
            if (r != CSR_RESULT_SUCCESS)
            {
                return r;
            }
            r = set_pmem_page(card, addr, CHIP_HELPER_WT_ROM, paddr);
            break;

        default:
            unifi_error(card->ospriv, "Bad address space %d in generic pointer 0x%08lX (helper=0x%x)\n",
                        space, generic_addr, card->helper);
            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    return r;
} /* set_page() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_set_proc_select
 *
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      select          Which XAP core to select
 *
 *  Returns:
 *      0 on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_set_proc_select(card_t *card, enum unifi_dbg_processors_select select)
{
    CsrResult r;

    /* Verify the the select value is allowed. */
    switch (select)
    {
        case UNIFI_PROC_MAC:
        case UNIFI_PROC_PHY:
        case UNIFI_PROC_BOTH:
            break;


        default:
            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    if (card->proc_select != (CsrUint32)select)
    {
        r = unifi_write_direct16(card,
                                 ChipHelper_DBG_HOST_PROC_SELECT(card->helper) * 2,
                                 (u8)select);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to write to Proc Select register\n");
            return r;
        }

        card->proc_select = (CsrUint32)select;
    }

    return CSR_RESULT_SUCCESS;
}


/*
 * ---------------------------------------------------------------------------
 * unifi_read_8_or_16
 *
 * Performs a byte read of the given address in shared data memory.
 * Set up the shared data memory page register as required.
 *
 * Arguments:
 * card Pointer to card structure.
 * unifi_addr UniFi shared-data-memory address to access.
 * pdata Pointer to a byte variable for the value read.
 *
 * Returns:
 * CSR_RESULT_SUCCESS on success, non-zero error code on error:
 * CSR_WIFI_HIP_RESULT_NO_DEVICE card was ejected
 * CSR_RESULT_FAILURE an SDIO error occurred
 * CSR_WIFI_HIP_RESULT_INVALID_VALUE a bad generic pointer was specified
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_read_8_or_16(card_t *card, CsrUint32 unifi_addr, u8 *pdata)
{
    CsrUint32 sdio_addr;
    CsrResult r;
#ifdef CSR_WIFI_TRANSPORT_CSPI
    u16 w;
#endif

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_r8or16_count++;
#endif
#ifdef CSR_WIFI_TRANSPORT_CSPI
    r = retrying_read16(card, card->function, sdio_addr, &w);
    *pdata = (u8)(w & 0xFF);
    return r;
#else
    return retrying_read8(card, card->function, sdio_addr, pdata);
#endif
} /* unifi_read_8_or_16() */


/*
 * ---------------------------------------------------------------------------
 * unifi_write_8_or_16
 *
 * Performs a byte write of the given address in shared data memory.
 * Set up the shared data memory page register as required.
 *
 * Arguments:
 * card Pointer to card context struct.
 * unifi_addr UniFi shared-data-memory address to access.
 * data Value to write.
 *
 * Returns:
 * CSR_RESULT_SUCCESS on success, non-zero error code on error:
 * CSR_WIFI_HIP_RESULT_NO_DEVICE card was ejected
 * CSR_RESULT_FAILURE an SDIO error occurred
 * CSR_WIFI_HIP_RESULT_INVALID_VALUE a bad generic pointer was specified
 *
 * Notes:
 * Beware using unifi_write8() because byte writes are not safe on UniFi.
 * Writes to odd bytes are cached, writes to even bytes perform a 16-bit
 * write with the previously cached odd byte.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_write_8_or_16(card_t *card, CsrUint32 unifi_addr, u8 data)
{
    CsrUint32 sdio_addr;
    CsrResult r;
#ifdef CSR_WIFI_TRANSPORT_CSPI
    u16 w;
#endif

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    if (sdio_addr & 1)
    {
        unifi_warning(card->ospriv,
                      "Warning: Byte write to an odd address (0x%lX) is dangerous\n",
                      sdio_addr);
    }

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_w8or16_count++;
#endif
#ifdef CSR_WIFI_TRANSPORT_CSPI
    w = data;
    return retrying_write16(card, card->function, sdio_addr, w);
#else
    return retrying_write8(card, card->function, sdio_addr, data);
#endif
} /* unifi_write_8_or_16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_card_read16
 *
 *      Performs a 16-bit read of the given address in shared data memory.
 *      Set up the shared data memory page register as required.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      unifi_addr      UniFi shared-data-memory address to access.
 *      pdata           Pointer to a 16-bit int variable for the value read.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE  a bad generic pointer was specified
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_card_read16(card_t *card, CsrUint32 unifi_addr, u16 *pdata)
{
    CsrUint32 sdio_addr;
    CsrResult r;

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_r16_count++;
#endif
    return unifi_read_direct16(card, sdio_addr, pdata);
} /* unifi_card_read16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_card_write16
 *
 *      Performs a 16-bit write of the given address in shared data memory.
 *      Set up the shared data memory page register as required.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      unifi_addr      UniFi shared-data-memory address to access.
 *      pdata           Pointer to a byte variable for the value write.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE  a bad generic pointer was specified
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_card_write16(card_t *card, CsrUint32 unifi_addr, u16 data)
{
    CsrUint32 sdio_addr;
    CsrResult r;

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_w16_count++;
#endif
    return unifi_write_direct16(card, sdio_addr, data);
} /* unifi_card_write16() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_read32
 *
 *      Performs a 32-bit read of the given address in shared data memory.
 *      Set up the shared data memory page register as required.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      unifi_addr      UniFi shared-data-memory address to access.
 *      pdata           Pointer to a int variable for the value read.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE  a bad generic pointer was specified
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_read32(card_t *card, CsrUint32 unifi_addr, CsrUint32 *pdata)
{
    CsrUint32 sdio_addr;
    CsrResult r;

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
    card->cmd_prof.cmd52_r32_count++;
#endif
    return unifi_read_direct32(card, sdio_addr, pdata);
} /* unifi_read32() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_card_readn
 *  unifi_readnz
 *
 *      Read multiple 8-bit values from the UniFi SDIO interface.
 *      This function interprets the address as a GenericPointer as
 *      defined in the UniFi Host Interface Protocol Specification.
 *      The readnz version of this function will stop when it reads a
 *      zero octet.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      unifi_addr      UniFi shared-data-memory address to access.
 *      pdata           Pointer to which to write data.
 *      len             Number of bytes to read
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE  a bad generic pointer was specified
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_readn_match(card_t *card, CsrUint32 unifi_addr, void *pdata, u16 len, s8 match)
{
    CsrUint32 sdio_addr;
    CsrResult r;
    CsrUint32 num;

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    r = unifi_read_directn_match(card, sdio_addr, pdata, len, match, &num);
    return r;
} /* unifi_readn_match() */


CsrResult unifi_card_readn(card_t *card, CsrUint32 unifi_addr, void *pdata, u16 len)
{
    return unifi_readn_match(card, unifi_addr, pdata, len, -1);
} /* unifi_card_readn() */


CsrResult unifi_readnz(card_t *card, CsrUint32 unifi_addr, void *pdata, u16 len)
{
    return unifi_readn_match(card, unifi_addr, pdata, len, 0);
} /* unifi_readnz() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_read_shared_count
 *
 *      Read signal count locations, checking for an SDIO error.  The
 *      signal count locations only contain a valid number if the
 *      highest bit isn't set.
 *
 *  Arguments:
 *      card            Pointer to card context structure.
 *      addr            Shared-memory address to read.
 *
 *  Returns:
 *      Value read from memory (0-127) or -1 on error
 * ---------------------------------------------------------------------------
 */
CsrInt32 unifi_read_shared_count(card_t *card, CsrUint32 addr)
{
    u8 b;
    /* I've increased this count, because I have seen cases where
     * there were three reads in a row with the top bit set.  I'm not
     * sure why this might have happened, but I can't see a problem
     * with increasing this limit.  It's better to take a while to
     * recover than to fail. */
#define SHARED_READ_RETRY_LIMIT 10
    CsrInt32 i;

    /*
     * Get the to-host-signals-written count.
     * The top-bit will be set if the firmware was in the process of
     * changing the value, in which case we read again.
     */
    /* Limit the number of repeats so we don't freeze */
    for (i = 0; i < SHARED_READ_RETRY_LIMIT; i++)
    {
        CsrResult r;
        r = unifi_read_8_or_16(card, addr, &b);
        if (r != CSR_RESULT_SUCCESS)
        {
            return -1;
        }
        if (!(b & 0x80))
        {
            /* There is a chance that the MSB may have contained invalid data
             * (overflow) at the time it was read. Therefore mask off the MSB.
             * This avoids a race between driver read and firmware write of the
             * word, the value we need is in the lower 8 bits anway.
             */
            return (CsrInt32)(b & 0xff);
        }
    }

    return -1;                  /* this function has changed in WMM mods */
} /* unifi_read_shared_count() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_writen
 *
 *      Write multiple 8-bit values to the UniFi SDIO interface using CMD52
 *      This function interprets the address as a GenericPointer as
 *      defined in the UniFi Host Interface Protocol Specification.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      unifi_addr      UniFi shared-data-memory address to access.
 *      pdata           Pointer to which to write data.
 *      len             Number of bytes to write
 *
 *  Returns:
 *      0 on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE    an odd length or length too big.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_writen(card_t *card, CsrUint32 unifi_addr, void *pdata, u16 len)
{
    CsrUint32 sdio_addr;
    CsrResult r;

    r = set_page(card, unifi_addr, &sdio_addr);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    return unifi_write_directn(card, sdio_addr, pdata, len);
} /* unifi_writen() */


static CsrResult csr_sdio_block_rw(card_t *card, CsrInt16 funcnum,
                                   CsrUint32 addr, u8 *pdata,
                                   u16 count, CsrInt16 dir_is_write)
{
    CsrResult csrResult;

    if (dir_is_write == UNIFI_SDIO_READ)
    {
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        unifi_debug_log_to_buf("r@%02X#%X=", addr, count);
#endif
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
        unifi_debug_log_to_buf("R");
#endif
        csrResult = CsrSdioRead(card->sdio_if, addr, pdata, count);
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
        unifi_debug_log_to_buf("<");
#endif
    }
    else
    {
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        unifi_debug_log_to_buf("w@%02X#%X=", addr, count);
        unifi_debug_hex_to_buf(pdata, count > CSR_WIFI_HIP_SDIO_TRACE_DATA_LENGTH?CSR_WIFI_HIP_SDIO_TRACE_DATA_LENGTH : count);
#endif
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
        unifi_debug_log_to_buf("W");
#endif
        csrResult = CsrSdioWrite(card->sdio_if, addr, pdata, count);
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
        unifi_debug_log_to_buf(">");
#endif
    }
#ifdef CSR_WIFI_HIP_DATA_PLANE_PROFILE
    card->cmd_prof.cmd53_count++;
#endif
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_debug_log_to_buf("error=%X", csrResult);
    }
    else if (dir_is_write == UNIFI_SDIO_READ)
    {
        unifi_debug_hex_to_buf(pdata, count > CSR_WIFI_HIP_SDIO_TRACE_DATA_LENGTH?CSR_WIFI_HIP_SDIO_TRACE_DATA_LENGTH : count);
    }
    unifi_debug_string_to_buf("\n");
#endif
    return csrResult;  /* CSR SDIO (not HIP) error code */
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_bulk_rw
 *
 *      Transfer bulk data to or from the UniFi SDIO interface.
 *      This function is used to read or write signals and bulk data.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      handle          Value to put in the Register Address field of the CMD53 req.
 *      data            Pointer to data to write.
 *      direction       One of UNIFI_SDIO_READ or UNIFI_SDIO_WRITE
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      This function uses SDIO CMD53, which is the block transfer mode.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_bulk_rw(card_t *card, CsrUint32 handle, void *pdata,
                        CsrUint32 len, CsrInt16 direction)
{
#define CMD53_RETRIES 3
    /*
     * Ideally instead of sleeping, we want to busy wait.
     * Currently there is no framework API to do this. When it becomes available,
     * we can use it to busy wait using usecs
     */
#define REWIND_RETRIES          15    /* when REWIND_DELAY==1msec, or 250 when REWIND_DELAY==50usecs */
#define REWIND_POLLING_RETRIES  5
#define REWIND_DELAY            1     /* msec or 50usecs */
    CsrResult csrResult;              /* SDIO error code */
    CsrResult r = CSR_RESULT_SUCCESS; /* HIP error code */
    CsrInt16 retries = CMD53_RETRIES;
    CsrInt16 stat_retries;
    u8 stat;
    CsrInt16 dump_read;
#ifdef UNIFI_DEBUG
    u8 *pdata_lsb = ((u8 *)&pdata) + card->lsb;
#endif
#ifdef CSR_WIFI_MAKE_FAKE_CMD53_ERRORS
    static CsrInt16 fake_error;
#endif

    dump_read = 0;
#ifdef UNIFI_DEBUG
    if (*pdata_lsb & 1)
    {
        unifi_notice(card->ospriv, "CD53 request on a unaligned buffer (addr: 0x%X) dir %s-Host\n",
                     pdata, (direction == UNIFI_SDIO_READ)?"To" : "From");
        if (direction == UNIFI_SDIO_WRITE)
        {
            dump(pdata, (u16)len);
        }
        else
        {
            dump_read = 1;
        }
    }
#endif

    /* Defensive checks */
    if (!pdata)
    {
        unifi_error(card->ospriv, "Null pdata for unifi_bulk_rw() len: %d\n", len);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }
    if ((len & 1) || (len > 0xffff))
    {
        unifi_error(card->ospriv, "Impossible CMD53 length requested: %d\n", len);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    while (1)
    {
        csrResult = csr_sdio_block_rw(card, card->function, handle,
                                      (u8 *)pdata, (u16)len,
                                      direction);
        if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
        {
            return CSR_WIFI_HIP_RESULT_NO_DEVICE;
        }
#ifdef CSR_WIFI_MAKE_FAKE_CMD53_ERRORS
        if (++fake_error > 100)
        {
            fake_error = 90;
            unifi_warning(card->ospriv, "Faking a CMD53 error,\n");
            if (csrResult == CSR_RESULT_SUCCESS)
            {
                csrResult = CSR_RESULT_FAILURE;
            }
        }
#endif
        if (csrResult == CSR_RESULT_SUCCESS)
        {
            if (dump_read)
            {
                dump(pdata, (u16)len);
            }
            break;
        }

        /*
         * At this point the SDIO driver should have written the I/O Abort
         * register to notify UniFi that the command has failed.
         * UniFi-1 and UniFi-2 (not UF6xxx) use the same register to store the
         * Deep Sleep State. This means we have to restore the Deep Sleep
         * State (AWAKE in any case since we can not perform a CD53 in any other
         * state) by rewriting the I/O Abort register to its previous value.
         */
        if (card->chip_id <= SDIO_CARD_ID_UNIFI_2)
        {
            (void)unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
        }

        /* If csr_sdio_block_rw() failed in a non-retryable way, or retries exhausted
         * then stop retrying
         */
        if (!retryable_sdio_error(csrResult))
        {
            unifi_error(card->ospriv, "Fatal error in a CMD53 transfer\n");
            break;
        }

        /*
         * These happen from time to time, try again
         */
        if (--retries == 0)
        {
            break;
        }

        unifi_trace(card->ospriv, UDBG4,
                    "Error in a CMD53 transfer, retrying (h:%d,l:%u)...\n",
                    (CsrInt16)handle & 0xff, len);

        /* The transfer failed, rewind and try again */
        r = unifi_write_8_or_16(card, card->sdio_ctrl_addr + 8,
                                (u8)(handle & 0xff));
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            /*
             * If we can't even do CMD52 (register read/write) then
             * stop here.
             */
            unifi_error(card->ospriv, "Failed to write REWIND cmd\n");
            return r;
        }

        /* Signal the UniFi to look for the rewind request. */
        r = CardGenInt(card);
        if (r != CSR_RESULT_SUCCESS)
        {
            return r;
        }

        /* Wait for UniFi to acknowledge the rewind */
        stat_retries = REWIND_RETRIES;
        while (1)
        {
            r = unifi_read_8_or_16(card, card->sdio_ctrl_addr + 8, &stat);
            if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                return r;
            }
            if (r != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "Failed to read REWIND status\n");
                return CSR_RESULT_FAILURE;
            }

            if (stat == 0)
            {
                break;
            }
            if (--stat_retries == 0)
            {
                unifi_error(card->ospriv, "Timeout waiting for REWIND ready\n");
                return CSR_RESULT_FAILURE;
            }

            /* Poll for the ack a few times */
            if (stat_retries < REWIND_RETRIES - REWIND_POLLING_RETRIES)
            {
                CsrThreadSleep(REWIND_DELAY);
            }
        }
    }

    /* The call to csr_sdio_block_rw() still failed after retrying */
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Block %s failed after %d retries\n",
                    (direction == UNIFI_SDIO_READ)?"read" : "write",
                    CMD53_RETRIES - retries);
        /* Report any SDIO error as a general i/o error */
        return CSR_RESULT_FAILURE;
    }

    /* Collect some stats */
    if (direction == UNIFI_SDIO_READ)
    {
        card->sdio_bytes_read += len;
    }
    else
    {
        card->sdio_bytes_written += len;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_bulk_rw() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_bulk_rw_noretry
 *
 *      Transfer bulk data to or from the UniFi SDIO interface.
 *      This function is used to read or write signals and bulk data.
 *
 *  Arguments:
 *      card            Pointer to card structure.
 *      handle          Value to put in the Register Address field of
 *                      the CMD53 req.
 *      data            Pointer to data to write.
 *      direction       One of UNIFI_SDIO_READ or UNIFI_SDIO_WRITE
 *
 *  Returns:
 *      0 on success, non-zero error code on error:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE  card was ejected
 *      CSR_RESULT_FAILURE     an SDIO error occurred
 *
 *  Notes:
 *      This function uses SDIO CMD53, which is the block transfer mode.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_bulk_rw_noretry(card_t *card, CsrUint32 handle, void *pdata,
                                CsrUint32 len, CsrInt16 direction)
{
    CsrResult csrResult;

    csrResult = csr_sdio_block_rw(card, card->function, handle,
                                  (u8 *)pdata, (u16)len, direction);
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Block %s failed\n",
                    (direction == UNIFI_SDIO_READ)?"read" : "write");
        return csrResult;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_bulk_rw_noretry() */


