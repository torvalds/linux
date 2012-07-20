/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 * FILE: csr_wifi_hip_card_sdio.c
 *
 * PURPOSE: Implementation of the Card API for SDIO.
 *
 * NOTES:
 *      CardInit() is called from the SDIO probe callback when a card is
 *      inserted. This performs the basic SDIO initialisation, enabling i/o
 *      etc.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"
#include "csr_wifi_hip_unifiversion.h"
#include "csr_wifi_hip_card.h"
#include "csr_wifi_hip_card_sdio.h"
#include "csr_wifi_hip_chiphelper.h"


/* Time to wait between attempts to read MAILBOX0 */
#define MAILBOX1_TIMEOUT                10  /* in millisecs */
#define MAILBOX1_ATTEMPTS               200 /* 2 seconds */

#define MAILBOX2_TIMEOUT                5   /* in millisecs */
#define MAILBOX2_ATTEMPTS               10  /* 50ms */

#define RESET_SETTLE_DELAY              25  /* in millisecs */

static CsrResult card_init_slots(card_t *card);
static CsrResult card_hw_init(card_t *card);
static CsrResult firmware_present_in_flash(card_t *card);
static void bootstrap_chip_hw(card_t *card);
static CsrResult unifi_reset_hardware(card_t *card);
static CsrResult unifi_hip_init(card_t *card);
static CsrResult card_access_panic(card_t *card);
static CsrResult unifi_read_chip_version(card_t *card);

/*
 * ---------------------------------------------------------------------------
 *  unifi_alloc_card
 *
 *      Allocate and initialise the card context structure.
 *
 *  Arguments:
 *      sdio            Pointer to SDIO context pointer to pass to low
 *                      level i/o functions.
 *      ospriv          Pointer to O/S private struct to pass when calling
 *                      callbacks to the higher level system.
 *
 *  Returns:
 *      Pointer to card struct, which represents the driver context or
 *      NULL if the allocation failed.
 * ---------------------------------------------------------------------------
 */
card_t* unifi_alloc_card(CsrSdioFunction *sdio, void *ospriv)
{
    card_t *card;
    u32 i;

    func_enter();


    card = (card_t *)CsrMemAlloc(sizeof(card_t));
    if (card == NULL)
    {
        return NULL;
    }
    CsrMemSet(card, 0, sizeof(card_t));


    card->sdio_if = sdio;
    card->ospriv  = ospriv;

    card->unifi_interrupt_seq = 1;

    /* Make these invalid. */
    card->proc_select = (u32)(-1);
    card->dmem_page = (u32)(-1);
    card->pmem_page = (u32)(-1);

    card->bh_reason_host = 0;
    card->bh_reason_unifi = 0;

    for (i = 0; i < sizeof(card->tx_q_paused_flag) / sizeof(card->tx_q_paused_flag[0]); i++)
    {
        card->tx_q_paused_flag[i] = 0;
    }
    card->memory_resources_allocated = 0;

    card->low_power_mode = UNIFI_LOW_POWER_DISABLED;
    card->periodic_wake_mode = UNIFI_PERIODIC_WAKE_HOST_DISABLED;

    card->host_state = UNIFI_HOST_STATE_AWAKE;
    card->intmode = CSR_WIFI_INTMODE_DEFAULT;

    /*
     * Memory resources for buffers are allocated when the chip is initialised
     * because we need configuration information from the firmware.
     */

    /*
     * Initialise wait queues and lists
     */
    card->fh_command_queue.q_body = card->fh_command_q_body;
    card->fh_command_queue.q_length = UNIFI_SOFT_COMMAND_Q_LENGTH;

    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        card->fh_traffic_queue[i].q_body = card->fh_traffic_q_body[i];
        card->fh_traffic_queue[i].q_length = UNIFI_SOFT_TRAFFIC_Q_LENGTH;
    }


    /* Initialise mini-coredump pointers in case no coredump buffers
     * are requested by the OS layer.
     */
    card->request_coredump_on_reset = 0;
    card->dump_next_write = NULL;
    card->dump_cur_read = NULL;
    card->dump_buf = NULL;

#ifdef UNIFI_DEBUG
    /* Determine offset of LSB in pointer for later alignment sanity check.
     * Synergy integer types have specific widths, which cause compiler
     * warnings when casting pointer types, e.g. on 64-bit systems.
     */
    {
        u32 val = 0x01234567;

        if (*((u8 *)&val) == 0x01)
        {
            card->lsb = sizeof(void *) - 1;     /* BE */
        }
        else
        {
            card->lsb = 0;                      /* LE */
        }
    }
#endif
    func_exit();
    return card;
} /* unifi_alloc_card() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_init_card
 *
 *      Reset the hardware and perform HIP initialization
 *
 *  Arguments:
 *      card        Pointer to card struct
 *
 *  Returns:
 *      CsrResult code
 *      CSR_RESULT_SUCCESS if successful
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_init_card(card_t *card, s32 led_mask)
{
    CsrResult r;

    func_enter();

    if (card == NULL)
    {
        func_exit_r(CSR_WIFI_HIP_RESULT_INVALID_VALUE);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    r = unifi_init(card);
    if (r != CSR_RESULT_SUCCESS)
    {
        func_exit_r(r);
        return r;
    }

    r = unifi_hip_init(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        func_exit_r(r);
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to start host protocol.\n");
        func_exit_r(r);
        return r;
    }

    func_exit();
    return CSR_RESULT_SUCCESS;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_init
 *
 *      Init the hardware.
 *
 *  Arguments:
 *      card        Pointer to card struct
 *
 *  Returns:
 *      CsrResult code
 *      CSR_RESULT_SUCCESS if successful
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_init(card_t *card)
{
    CsrResult r;
    CsrResult csrResult;

    func_enter();

    if (card == NULL)
    {
        func_exit_r(CSR_WIFI_HIP_RESULT_INVALID_VALUE);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    /*
     * Disable the SDIO interrupts while initialising UniFi.
     * Re-enable them when f/w is running.
     */
    csrResult = CsrSdioInterruptDisable(card->sdio_if);
    if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
    {
        return CSR_WIFI_HIP_RESULT_NO_DEVICE;
    }

    /*
     * UniFi's PLL may start with a slow clock (~ 1 MHz) so initially
     * set the SDIO bus clock to a similar value or SDIO accesses may
     * fail.
     */
    csrResult = CsrSdioMaxBusClockFrequencySet(card->sdio_if, UNIFI_SDIO_CLOCK_SAFE_HZ);
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        r = ConvertCsrSdioToCsrHipResult(card, csrResult);
        func_exit_r(r);
        return r;
    }
    card->sdio_clock_speed = UNIFI_SDIO_CLOCK_SAFE_HZ;

    /*
     * Reset UniFi. Note, this only resets the WLAN function part of the chip,
     * the SDIO interface is not reset.
     */
    unifi_trace(card->ospriv, UDBG1, "Resetting UniFi\n");
    r = unifi_reset_hardware(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to reset UniFi\n");
        func_exit_r(r);
        return r;
    }

    /* Reset the power save mode, to be active until the MLME-reset is complete */
    r = unifi_configure_low_power_mode(card,
                                       UNIFI_LOW_POWER_DISABLED, UNIFI_PERIODIC_WAKE_HOST_DISABLED);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to set power save mode\n");
        func_exit_r(r);
        return r;
    }

    /*
     * Set initial value of page registers.
     * The page registers will be maintained by unifi_read...() and
     * unifi_write...().
     */
    card->proc_select = (u32)(-1);
    card->dmem_page = (u32)(-1);
    card->pmem_page = (u32)(-1);
    r = unifi_write_direct16(card, ChipHelper_HOST_WINDOW3_PAGE(card->helper) * 2, 0);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write SHARED_DMEM_PAGE\n");
        func_exit_r(r);
        return r;
    }
    r = unifi_write_direct16(card, ChipHelper_HOST_WINDOW2_PAGE(card->helper) * 2, 0);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write PROG_MEM2_PAGE\n");
        func_exit_r(r);
        return r;
    }

    /*
     * If the driver has reset UniFi due to previous SDIO failure, this may
     * have been due to a chip watchdog reset. In this case, the driver may
     * have requested a mini-coredump which needs to be captured now the
     * SDIO interface is alive.
     */
    (void)unifi_coredump_handle_request(card);

    /*
     * Probe to see if the UniFi has ROM/flash to boot from. CSR6xxx should do.
     */
    r = firmware_present_in_flash(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r == CSR_WIFI_HIP_RESULT_NOT_FOUND)
    {
        unifi_error(card->ospriv, "No firmware found\n");
    }
    else if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Probe for Flash failed\n");
    }

    func_exit_r(r);
    return r;
} /* unifi_init() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_download
 *
 *      Load the firmware.
 *
 *  Arguments:
 *      card        Pointer to card struct
 *      led_mask    Loader LED mask
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success
 *      CsrResult error code on failure.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_download(card_t *card, s32 led_mask)
{
    CsrResult r;
    void *dlpriv;

    func_enter();

    if (card == NULL)
    {
        func_exit_r(CSR_WIFI_HIP_RESULT_INVALID_VALUE);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    /* Set the loader led mask */
    card->loader_led_mask = led_mask;

    /* Get the firmware file information */
    unifi_trace(card->ospriv, UDBG1, "downloading firmware...\n");

    dlpriv = unifi_dl_fw_read_start(card, UNIFI_FW_STA);
    if (dlpriv == NULL)
    {
        func_exit_r(CSR_WIFI_HIP_RESULT_NOT_FOUND);
        return CSR_WIFI_HIP_RESULT_NOT_FOUND;
    }

    /* Download the firmware. */
    r = unifi_dl_firmware(card, dlpriv);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to download firmware\n");
        func_exit_r(r);
        return r;
    }

    /* Free the firmware file information. */
    unifi_fw_read_stop(card->ospriv, dlpriv);

    func_exit();

    return CSR_RESULT_SUCCESS;
} /* unifi_download() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_hip_init
 *
 *      This function performs the f/w initialisation sequence as described
 *      in the Unifi Host Interface Protocol Specification.
 *      It allocates memory for host-side slot data and signal queues.
 *
 *  Arguments:
 *      card        Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success or else a CSR error code
 *
 *  Notes:
 *      The firmware must have been downloaded.
 * ---------------------------------------------------------------------------
 */
static CsrResult unifi_hip_init(card_t *card)
{
    CsrResult r;
    CsrResult csrResult;

    func_enter();

    r = card_hw_init(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to establish communication with UniFi\n");
        func_exit_r(r);
        return r;
    }
#ifdef CSR_PRE_ALLOC_NET_DATA
    /* if there is any preallocated netdata left from the prev session free it now */
    prealloc_netdata_free(card);
#endif
    /*
     * Allocate memory for host-side slot data and signal queues.
     * We need the config info read from the firmware to know how much
     * memory to allocate.
     */
    r = card_init_slots(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Init slots failed: %d\n", r);
        func_exit_r(r);
        return r;
    }

    unifi_trace(card->ospriv, UDBG2, "Sending first UniFi interrupt\n");

    r = unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
    if (r != CSR_RESULT_SUCCESS)
    {
        func_exit_r(r);
        return r;
    }

    /* Enable the SDIO interrupts now that the f/w is running. */
    csrResult = CsrSdioInterruptEnable(card->sdio_if);
    if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
    {
        return CSR_WIFI_HIP_RESULT_NO_DEVICE;
    }

    /* Signal the UniFi to start handling messages */
    r = CardGenInt(card);
    if (r != CSR_RESULT_SUCCESS)
    {
        func_exit_r(r);
        return r;
    }

    func_exit();

    return CSR_RESULT_SUCCESS;
} /* unifi_hip_init() */


/*
 * ---------------------------------------------------------------------------
 *  _build_sdio_config_data
 *
 *      Unpack the SDIO configuration information from a buffer read from
 *      UniFi into a host structure.
 *      The data is byte-swapped for a big-endian host if necessary by the
 *      UNPACK... macros.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      cfg_data        Destination structure to unpack into.
 *      cfg_data_buf    Source buffer to read from. This should be the raw
 *                      data read from UniFi.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void _build_sdio_config_data(sdio_config_data_t *cfg_data,
                                    const u8     *cfg_data_buf)
{
    s16 offset = 0;

    cfg_data->version = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->sdio_ctrl_offset = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->fromhost_sigbuf_handle = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->tohost_sigbuf_handle = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->num_fromhost_sig_frags = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->num_tohost_sig_frags = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->num_fromhost_data_slots = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->num_tohost_data_slots = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->data_slot_size = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->initialised = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->overlay_size = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT32;

    cfg_data->data_slot_round = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->sig_frag_size = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
    offset += SIZEOF_UINT16;

    cfg_data->tohost_signal_padding = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(cfg_data_buf + offset);
} /* _build_sdio_config_data() */


/*
 * - Function ----------------------------------------------------------------
 * card_hw_init()
 *
 *      Perform the initialisation procedure described in the UniFi Host
 *      Interface Protocol document (section 3.3.8) and read the run-time
 *      configuration information from the UniFi. This is stuff like number
 *      of bulk data slots etc.
 *
 *      The card enumeration and SD initialisation has already been done by
 *      the SDIO library, see card_sdio_init().
 *
 *      The initialisation is done when firmware is ready, i.e. this may need
 *      to be called after a f/w download operation.
 *
 *      The initialisation procedure goes like this:
 *       - Wait for UniFi to start-up by polling SHARED_MAILBOX1
 *       - Find the symbol table and look up SLT_SDIO_SLOT_CONFIG
 *       - Read the config structure
 *       - Check the "SDIO initialised" flag, if not zero do a h/w reset and
 *         start again
 *       - Decide the number of bulk data slots to allocate, allocate them and
 *         set "SDIO initialised" flag (and generate an interrupt) to say so.
 *
 * Arguments:
 *      card        Pointer to card struct
 *
 * Returns:
 *      CSR_RESULT_SUCEESS on success,
 *      a CSR error code on failure
 *
 * Notes:
 *      All data in the f/w is stored in a little endian format, without any
 *      padding bytes. Every read from this memory has to be transformed in
 *      host (cpu specific) format, before it is stored in driver's parameters
 *      or/and structures. Athough unifi_card_read16() and unifi_read32() do perform
 *      the convertion internally, unifi_readn() does not.
 * ---------------------------------------------------------------------------
 */
static CsrResult card_hw_init(card_t *card)
{
    u32 slut_address;
    u16 initialised;
    u16 finger_print;
    symbol_t slut;
    sdio_config_data_t *cfg_data;
    u8 cfg_data_buf[SDIO_CONFIG_DATA_SIZE];
    CsrResult r;
    void *dlpriv;
    s16 major, minor;
    s16 search_4slut_again;
    CsrResult csrResult;

    func_enter();

    /*
     * The device revision from the TPLMID_MANF and TPLMID_CARD fields
     * of the CIS are available as
     *   card->sdio_if->pDevice->ManfID
     *   card->sdio_if->pDevice->AppID
     */

    /*
     * Run in a loop so we can patch.
     */
    do
    {
        /* Reset these each time around the loop. */
        search_4slut_again = 0;
        cfg_data = NULL;

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
        unifi_trace(card->ospriv, UDBG4, "SLUT addr 0x%lX\n", slut_address);

        /*
         * Firmware has started, but doesn't know full clock configuration yet
         * as some of the information may be in the MIB. Therefore we set an
         * initial SDIO clock speed, faster than UNIFI_SDIO_CLOCK_SAFE_HZ, for
         * the patch download and subsequent firmware initialisation, and
         * full speed UNIFI_SDIO_CLOCK_MAX_HZ will be set once the f/w tells us
         * that it is ready.
         */
        csrResult = CsrSdioMaxBusClockFrequencySet(card->sdio_if, UNIFI_SDIO_CLOCK_INIT_HZ);
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            r = ConvertCsrSdioToCsrHipResult(card, csrResult);
            func_exit_r(r);
            return r;
        }
        card->sdio_clock_speed = UNIFI_SDIO_CLOCK_INIT_HZ;

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
            unifi_error(card->ospriv, "Failed to find Symbol lookup table fingerprint\n");
            func_exit_r(CSR_RESULT_FAILURE);
            return CSR_RESULT_FAILURE;
        }

        /* Symbol table starts imedately after the fingerprint */
        slut_address += 2;

        /* Search the table until either the end marker is found, or the
         * loading of patch firmware invalidates the current table.
         */
        while (!search_4slut_again)
        {
            u16 s;
            u32 l;

            r = unifi_card_read16(card, slut_address, &s);
            if (r != CSR_RESULT_SUCCESS)
            {
                func_exit_r(r);
                return r;
            }
            slut_address += 2;

            if (s == CSR_SLT_END)
            {
                unifi_trace(card->ospriv, UDBG3, "  found CSR_SLT_END\n");
                break;
            }

            r = unifi_read32(card, slut_address, &l);
            if (r != CSR_RESULT_SUCCESS)
            {
                func_exit_r(r);
                return r;
            }
            slut_address += 4;

            slut.id = s;
            slut.obj = l;

            unifi_trace(card->ospriv, UDBG3, "  found SLUT id %02d.%08lx\n", slut.id, slut.obj);
            switch (slut.id)
            {
                case CSR_SLT_SDIO_SLOT_CONFIG:
                    cfg_data = &card->config_data;
                    /*
                     * unifi_card_readn reads n bytes from the card, where data is stored
                     * in a little endian format, without any padding bytes. So, we
                     * can not just pass the cfg_data pointer or use the
                     * sizeof(sdio_config_data_t) since the structure in the host can
                     * be big endian formatted or have padding bytes for alignment.
                     * We use a char buffer to read the data from the card.
                     */
                    r = unifi_card_readn(card, slut.obj, cfg_data_buf, SDIO_CONFIG_DATA_SIZE);
                    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
                    {
                        return r;
                    }
                    if (r != CSR_RESULT_SUCCESS)
                    {
                        unifi_error(card->ospriv, "Failed to read config data\n");
                        func_exit_r(r);
                        return r;
                    }
                    /* .. and then we copy the data to the host structure */
                    _build_sdio_config_data(cfg_data, cfg_data_buf);

                    /* Make sure the from host data slots are what we expect
                        we reserve 2 for commands and there should be at least
                        1 left for each access category */
                    if ((cfg_data->num_fromhost_data_slots < UNIFI_RESERVED_COMMAND_SLOTS)
                        || (cfg_data->num_fromhost_data_slots - UNIFI_RESERVED_COMMAND_SLOTS) / UNIFI_NO_OF_TX_QS == 0)
                    {
                        unifi_error(card->ospriv, "From host data slots %d\n", cfg_data->num_fromhost_data_slots);
                        unifi_error(card->ospriv, "need to be (queues * x + 2) (UNIFI_RESERVED_COMMAND_SLOTS for commands)\n");
                        func_exit_r(CSR_RESULT_FAILURE);
                        return CSR_RESULT_FAILURE;
                    }

                    /* Configure SDIO to-block-size padding */
                    if (card->sdio_io_block_pad)
                    {
                    /*
                     * Firmware limits the maximum padding size via data_slot_round.
                     * Therefore when padding to whole block sizes, the block size
                     * must be configured correctly by adjusting CSR_WIFI_HIP_SDIO_BLOCK_SIZE.
                     */
                        if (cfg_data->data_slot_round < card->sdio_io_block_size)
                        {
                            unifi_error(card->ospriv,
                                        "Configuration error: Block size of %d exceeds f/w data_slot_round of %d\n",
                                        card->sdio_io_block_size, cfg_data->data_slot_round);
                            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
                        }

                        /*
                         * To force the To-Host signals to be rounded up to the SDIO block
                         * size, we need to write the To-Host Signal Padding Fragments
                         * field of the SDIO configuration in UniFi.
                         */
                        if ((card->sdio_io_block_size % cfg_data->sig_frag_size) != 0)
                        {
                            unifi_error(card->ospriv, "Configuration error: Can not pad to-host signals.\n");
                            func_exit_r(CSR_WIFI_HIP_RESULT_INVALID_VALUE);
                            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
                        }
                        cfg_data->tohost_signal_padding = (u16) (card->sdio_io_block_size / cfg_data->sig_frag_size);
                        unifi_info(card->ospriv, "SDIO block size %d requires %d padding chunks\n",
                                   card->sdio_io_block_size, cfg_data->tohost_signal_padding);
                        r = unifi_card_write16(card, slut.obj + SDIO_TO_HOST_SIG_PADDING_OFFSET, cfg_data->tohost_signal_padding);
                        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
                        {
                            return r;
                        }
                        if (r != CSR_RESULT_SUCCESS)
                        {
                            unifi_error(card->ospriv, "Failed to write To-Host Signal Padding Fragments\n");
                            func_exit_r(r);
                            return r;
                        }
                    }

                    /* Reconstruct the Generic Pointer address of the
                     * SDIO Control Data Struct.
                     */
                    card->sdio_ctrl_addr = cfg_data->sdio_ctrl_offset | (UNIFI_SH_DMEM << 24);
                    card->init_flag_addr = slut.obj + SDIO_INIT_FLAG_OFFSET;
                    break;

                case CSR_SLT_BUILD_ID_NUMBER:
                {
                    u32 n;
                    r = unifi_read32(card, slut.obj, &n);
                    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
                    {
                        return r;
                    }
                    if (r != CSR_RESULT_SUCCESS)
                    {
                        unifi_error(card->ospriv, "Failed to read build id\n");
                        func_exit_r(r);
                        return r;
                    }
                    card->build_id = n;
                }
                break;

                case CSR_SLT_BUILD_ID_STRING:
                    r = unifi_readnz(card, slut.obj, card->build_id_string,
                                     sizeof(card->build_id_string));
                    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
                    {
                        return r;
                    }
                    if (r != CSR_RESULT_SUCCESS)
                    {
                        unifi_error(card->ospriv, "Failed to read build string\n");
                        func_exit_r(r);
                        return r;
                    }
                    break;

                case CSR_SLT_PERSISTENT_STORE_DB:
                    break;

                case CSR_SLT_BOOT_LOADER_CONTROL:

                    /* This command copies most of the station firmware
                     * image from ROM into program RAM.  It also clears
                     * out the zerod data and sets up the initialised
                     * data. */
                    r = unifi_do_loader_op(card, slut.obj + 6, UNIFI_BOOT_LOADER_LOAD_STA);
                    if (r != CSR_RESULT_SUCCESS)
                    {
                        unifi_error(card->ospriv, "Failed to write loader load image command\n");
                        func_exit_r(r);
                        return r;
                    }

                    dlpriv = unifi_dl_fw_read_start(card, UNIFI_FW_STA);

                    /* dlpriv might be NULL, we still need to do the do_loader_op step. */
                    if (dlpriv != NULL)
                    {
                    /* Download the firmware. */
                        r = unifi_dl_patch(card, dlpriv, slut.obj);

                    /* Free the firmware file information. */
                        unifi_fw_read_stop(card->ospriv, dlpriv);

                        if (r != CSR_RESULT_SUCCESS)
                        {
                            unifi_error(card->ospriv, "Failed to patch firmware\n");
                            func_exit_r(r);
                            return r;
                        }
                    }

                    /* This command starts the firmware image that we want (the
                    * station by default) with any patches required applied. */
                    r = unifi_do_loader_op(card, slut.obj + 6, UNIFI_BOOT_LOADER_RESTART);
                    if (r != CSR_RESULT_SUCCESS)
                    {
                        unifi_error(card->ospriv, "Failed to write loader restart command\n");
                        func_exit_r(r);
                        return r;
                    }

                    /* The now running patch f/w defines a new SLUT data structure -
                     * the current one is no longer valid. We must drop out of the
                     * processing loop and enumerate the new SLUT (which may appear
                     * at a different offset).
                     */
                    search_4slut_again = 1;
                    break;

                case CSR_SLT_PANIC_DATA_PHY:
                    card->panic_data_phy_addr = slut.obj;
                    break;

                case CSR_SLT_PANIC_DATA_MAC:
                    card->panic_data_mac_addr = slut.obj;
                    break;

                default:
                    /* do nothing */
                    break;
            }
        } /* while */
    } while (search_4slut_again);

    /* Did we find the Config Data ? */
    if (cfg_data == NULL)
    {
        unifi_error(card->ospriv, "Failed to find SDIO_SLOT_CONFIG Symbol\n");
        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
    }

    /*
     * Has ths card already been initialised?
     * If so, return an error so we do a h/w reset and start again.
     */
    r = unifi_card_read16(card, card->init_flag_addr, &initialised);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to read init flag at %08lx\n",
                    card->init_flag_addr);
        func_exit_r(r);
        return r;
    }
    if (initialised != 0)
    {
        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
    }


    /*
     * Now check the UniFi firmware version
     */
    major = (cfg_data->version >> 8) & 0xFF;
    minor = cfg_data->version & 0xFF;
    unifi_info(card->ospriv, "UniFi f/w protocol version %d.%d (driver %d.%d)\n",
               major, minor,
               UNIFI_HIP_MAJOR_VERSION, UNIFI_HIP_MINOR_VERSION);

    unifi_info(card->ospriv, "Firmware build %u: %s\n",
               card->build_id, card->build_id_string);

    if (major != UNIFI_HIP_MAJOR_VERSION)
    {
        unifi_error(card->ospriv, "UniFi f/w protocol major version (%d) is different from driver (v%d.%d)\n",
                    major, UNIFI_HIP_MAJOR_VERSION, UNIFI_HIP_MINOR_VERSION);
#ifndef CSR_WIFI_DISABLE_HIP_VERSION_CHECK
        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
#endif
    }
    if (minor < UNIFI_HIP_MINOR_VERSION)
    {
        unifi_error(card->ospriv, "UniFi f/w protocol version (v%d.%d) is older than minimum required by driver (v%d.%d).\n",
                    major, minor,
                    UNIFI_HIP_MAJOR_VERSION, UNIFI_HIP_MINOR_VERSION);
#ifndef CSR_WIFI_DISABLE_HIP_VERSION_CHECK
        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
#endif
    }

    /* Read panic codes from a previous firmware panic. If the firmware has
     * not panicked since power was applied (e.g. power-off hard reset)
     * the stored panic codes will not be updated.
     */
    unifi_read_panic(card);

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* card_hw_init() */


/*
 * ---------------------------------------------------------------------------
 *  card_wait_for_unifi_to_reset
 *
 *      Waits for a reset to complete by polling the WLAN function enable
 *      bit (which is cleared on reset).
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on failure.
 * ---------------------------------------------------------------------------
 */
static CsrResult card_wait_for_unifi_to_reset(card_t *card)
{
    s16 i;
    CsrResult r;
    u8 io_enable;
    CsrResult csrResult;

    func_enter();

    r = CSR_RESULT_SUCCESS;
    for (i = 0; i < MAILBOX2_ATTEMPTS; i++)
    {
        unifi_trace(card->ospriv, UDBG1, "waiting for reset to complete, attempt %d\n", i);
        if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
        {
            /* It's quite likely that this read will timeout for the
             * first few tries - especially if we have reset via
             * DBG_RESET.
             */
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
            unifi_debug_log_to_buf("m0@%02X=", SDIO_IO_READY);
#endif
            csrResult = CsrSdioF0Read8(card->sdio_if, SDIO_IO_READY, &io_enable);
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
            if (csrResult != CSR_RESULT_SUCCESS)
            {
                unifi_debug_log_to_buf("error=%X\n", csrResult);
            }
            else
            {
                unifi_debug_log_to_buf("%X\n", io_enable);
            }
#endif
            if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
            {
                return CSR_WIFI_HIP_RESULT_NO_DEVICE;
            }
            r = CSR_RESULT_SUCCESS;
            if (csrResult != CSR_RESULT_SUCCESS)
            {
                r = ConvertCsrSdioToCsrHipResult(card, csrResult);
            }
        }
        else
        {
            r = sdio_read_f0(card, SDIO_IO_ENABLE, &io_enable);
        }
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r == CSR_RESULT_SUCCESS)
        {
            u16 mbox2;
            s16 enabled = io_enable & (1 << card->function);

            if (!enabled)
            {
                unifi_trace(card->ospriv, UDBG1,
                            "Reset complete (function %d is disabled) in ~ %u msecs\n",
                            card->function, i * MAILBOX2_TIMEOUT);

                /* Enable WLAN function and verify MAILBOX2 is zero'd */
                csrResult = CsrSdioFunctionEnable(card->sdio_if);
                if (csrResult != CSR_RESULT_SUCCESS)
                {
                    r = ConvertCsrSdioToCsrHipResult(card, csrResult);
                    unifi_error(card->ospriv, "CsrSdioFunctionEnable failed %d\n", r);
                    break;
                }
            }

            r = unifi_read_direct16(card, ChipHelper_SDIO_HIP_HANDSHAKE(card->helper) * 2, &mbox2);
            if (r != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "read HIP_HANDSHAKE failed %d\n", r);
                break;
            }
            if (mbox2 != 0)
            {
                unifi_error(card->ospriv, "MAILBOX2 non-zero after reset (mbox2 = %04x)\n", mbox2);
                r = CSR_RESULT_FAILURE;
            }
            break;
        }
        else
        {
            if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
            {
                /* We ignore read failures for the first few reads,
                 * they are probably benign. */
                if (i > MAILBOX2_ATTEMPTS / 4)
                {
                    unifi_trace(card->ospriv, UDBG1, "Failed to read CCCR IO Ready register while polling for reset\n");
                }
            }
            else
            {
                unifi_trace(card->ospriv, UDBG1, "Failed to read CCCR IO Enable register while polling for reset\n");
            }
        }
        CsrThreadSleep(MAILBOX2_TIMEOUT);
    }

    if (r == CSR_RESULT_SUCCESS && i == MAILBOX2_ATTEMPTS)
    {
        unifi_trace(card->ospriv, UDBG1, "Timeout waiting for UniFi to complete reset\n");
        r = CSR_RESULT_FAILURE;
    }

    func_exit();
    return r;
} /* card_wait_for_unifi_to_reset() */


/*
 * ---------------------------------------------------------------------------
 *  card_wait_for_unifi_to_disable
 *
 *      Waits for the function to become disabled by polling the
 *      IO_READY bit.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on failure.
 *
 *  Notes: This function can only be used with
 *         card->chip_id > SDIO_CARD_ID_UNIFI_2
 * ---------------------------------------------------------------------------
 */
static CsrResult card_wait_for_unifi_to_disable(card_t *card)
{
    s16 i;
    CsrResult r;
    u8 io_enable;
    CsrResult csrResult;

    func_enter();

    if (card->chip_id <= SDIO_CARD_ID_UNIFI_2)
    {
        unifi_error(card->ospriv,
                    "Function reset method not supported for chip_id=%d\n",
                    card->chip_id);
        func_exit();
        return CSR_RESULT_FAILURE;
    }

    r = CSR_RESULT_SUCCESS;
    for (i = 0; i < MAILBOX2_ATTEMPTS; i++)
    {
        unifi_trace(card->ospriv, UDBG1, "waiting for disable to complete, attempt %d\n", i);

        /*
         * It's quite likely that this read will timeout for the
         * first few tries - especially if we have reset via
         * DBG_RESET.
         */
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        unifi_debug_log_to_buf("r0@%02X=", SDIO_IO_READY);
#endif
        csrResult = CsrSdioF0Read8(card->sdio_if, SDIO_IO_READY, &io_enable);
#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_SDIO_TRACE)
        if (csrResult != CSR_RESULT_SUCCESS)
        {
            unifi_debug_log_to_buf("error=%X\n", csrResult);
        }
        else
        {
            unifi_debug_log_to_buf("%X\n", io_enable);
        }
#endif
        if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
        {
            return CSR_WIFI_HIP_RESULT_NO_DEVICE;
        }
        if (csrResult == CSR_RESULT_SUCCESS)
        {
            s16 enabled = io_enable & (1 << card->function);
            r = CSR_RESULT_SUCCESS;
            if (!enabled)
            {
                unifi_trace(card->ospriv, UDBG1,
                            "Disable complete (function %d is disabled) in ~ %u msecs\n",
                            card->function, i * MAILBOX2_TIMEOUT);

                break;
            }
        }
        else
        {
            /*
             * We ignore read failures for the first few reads,
             * they are probably benign.
             */
            r = ConvertCsrSdioToCsrHipResult(card, csrResult);
            if (i > (MAILBOX2_ATTEMPTS / 4))
            {
                unifi_trace(card->ospriv, UDBG1,
                            "Failed to read CCCR IO Ready register while polling for disable\n");
            }
        }
        CsrThreadSleep(MAILBOX2_TIMEOUT);
    }

    if ((r == CSR_RESULT_SUCCESS) && (i == MAILBOX2_ATTEMPTS))
    {
        unifi_trace(card->ospriv, UDBG1, "Timeout waiting for UniFi to complete disable\n");
        r = CSR_RESULT_FAILURE;
    }

    func_exit();
    return r;
} /* card_wait_for_unifi_to_reset() */


/*
 * ---------------------------------------------------------------------------
 *  card_wait_for_firmware_to_start
 *
 *      Polls the MAILBOX1 register for a non-zero value.
 *      Then reads MAILBOX0 and forms the two values into a 32-bit address
 *      which is returned to the caller.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      paddr           Pointer to receive the UniFi address formed
 *                      by concatenating MAILBOX1 and MAILBOX0.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on failure.
 * ---------------------------------------------------------------------------
 */
CsrResult card_wait_for_firmware_to_start(card_t *card, u32 *paddr)
{
    s32 i;
    u16 mbox0, mbox1;
    CsrResult r;

    func_enter();

    /*
     * Wait for UniFi to initialise its data structures by polling
     * the SHARED_MAILBOX1 register.
     * Experience shows this is typically 120ms.
     */
    CsrThreadSleep(MAILBOX1_TIMEOUT);

    mbox1 = 0;
    unifi_trace(card->ospriv, UDBG1, "waiting for MAILBOX1 to be non-zero...\n");
    for (i = 0; i < MAILBOX1_ATTEMPTS; i++)
    {
        r = unifi_read_direct16(card, ChipHelper_MAILBOX1(card->helper) * 2, &mbox1);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            /* These reads can fail if UniFi isn't up yet, so try again */
            unifi_warning(card->ospriv, "Failed to read UniFi Mailbox1 register\n");
        }

        if ((r == CSR_RESULT_SUCCESS) && (mbox1 != 0))
        {
            unifi_trace(card->ospriv, UDBG1, "MAILBOX1 ready (0x%04X) in %u millisecs\n",
                        mbox1, i * MAILBOX1_TIMEOUT);

            /* Read the MAILBOX1 again in case we caught the value as it
             * changed. */
            r = unifi_read_direct16(card, ChipHelper_MAILBOX1(card->helper) * 2, &mbox1);
            if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                return r;
            }
            if (r != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "Failed to read UniFi Mailbox1 register for second time\n");
                func_exit_r(r);
                return r;
            }
            unifi_trace(card->ospriv, UDBG1, "MAILBOX1 value=0x%04X\n", mbox1);

            break;
        }

        CsrThreadSleep(MAILBOX1_TIMEOUT);
        if ((i % 100) == 99)
        {
            unifi_trace(card->ospriv, UDBG2, "MAILBOX1 not ready (0x%X), still trying...\n", mbox1);
        }
    }

    if ((r == CSR_RESULT_SUCCESS) && (mbox1 == 0))
    {
        unifi_trace(card->ospriv, UDBG1, "Timeout waiting for firmware to start, Mailbox1 still 0 after %d ms\n",
                    MAILBOX1_ATTEMPTS * MAILBOX1_TIMEOUT);
        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
    }


    /*
     * Complete the reset handshake by setting MAILBOX2 to 0xFFFF
     */
    r = unifi_write_direct16(card, ChipHelper_SDIO_HIP_HANDSHAKE(card->helper) * 2, 0xFFFF);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write f/w startup handshake to MAILBOX2\n");
        func_exit_r(r);
        return r;
    }


    /*
     * Read the Symbol Look Up Table (SLUT) offset.
     * Top 16 bits are in mbox1, read the lower 16 bits from mbox0.
     */
    mbox0 = 0;
    r = unifi_read_direct16(card, ChipHelper_MAILBOX0(card->helper) * 2, &mbox0);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to read UniFi Mailbox0 register\n");
        func_exit_r(r);
        return r;
    }

    *paddr = (((u32)mbox1 << 16) | mbox0);

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* card_wait_for_firmware_to_start() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_capture_panic
 *
 *      Attempt to capture panic codes from the firmware. This may involve
 *      warm reset of the chip to regain access following a watchdog reset.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS if panic codes were captured, or none available
 *      CSR_RESULT_FAILURE if the driver could not access function 1
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_capture_panic(card_t *card)
{
    func_enter();

    /* The firmware must have previously initialised to read the panic addresses
     * from the SLUT
     */
    if (!card->panic_data_phy_addr || !card->panic_data_mac_addr)
    {
        func_exit();
        return CSR_RESULT_SUCCESS;
    }

    /* Ensure we can access function 1 following a panic/watchdog reset */
    if (card_access_panic(card) == CSR_RESULT_SUCCESS)
    {
        /* Read the panic codes */
        unifi_read_panic(card);
    }
    else
    {
        unifi_info(card->ospriv, "Unable to read panic codes");
    }

    func_exit();
    return CSR_RESULT_SUCCESS;
}


/*
 * ---------------------------------------------------------------------------
 *  card_access_panic
 *      Attempt to read the WLAN SDIO function in order to read panic codes
 *      and perform various reset steps to regain access if the read fails.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS if panic codes can be read
 *      CSR error code if panic codes can not be read
 * ---------------------------------------------------------------------------
 */
static CsrResult card_access_panic(card_t *card)
{
    u16 data_u16 = 0;
    s32 i;
    CsrResult r, sr;

    func_enter();

    /* A chip version of zero means that the version never got succesfully read
     * during reset. In this case give up because it will not be possible to
     * verify the chip version.
     */
    if (!card->chip_version)
    {
        unifi_info(card->ospriv, "Unknown chip version\n");
        return CSR_RESULT_FAILURE;
    }

    /* Ensure chip is awake or access to function 1 will fail */
    r = unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "unifi_set_host_state() failed %d\n", r);
        return CSR_RESULT_FAILURE; /* Card is probably unpowered */
    }
    CsrThreadSleep(20);

    for (i = 0; i < 3; i++)
    {
        sr = CsrSdioRead16(card->sdio_if, CHIP_HELPER_UNIFI_GBL_CHIP_VERSION * 2, &data_u16);
        if (sr != CSR_RESULT_SUCCESS || data_u16 != card->chip_version)
        {
            unifi_info(card->ospriv, "Failed to read valid chip version sr=%d (0x%04x want 0x%04x) try %d\n",
                       sr, data_u16, card->chip_version, i);

            /* Set clock speed low */
            sr = CsrSdioMaxBusClockFrequencySet(card->sdio_if, UNIFI_SDIO_CLOCK_SAFE_HZ);
            if (sr != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "CsrSdioMaxBusClockFrequencySet() failed1 %d\n", sr);
                r = ConvertCsrSdioToCsrHipResult(card, sr);
            }
            card->sdio_clock_speed = UNIFI_SDIO_CLOCK_SAFE_HZ;

            /* First try re-enabling function in case a f/w watchdog reset disabled it */
            if (i == 0)
            {
                unifi_info(card->ospriv, "Try function enable\n");
                sr = CsrSdioFunctionEnable(card->sdio_if);
                if (sr != CSR_RESULT_SUCCESS)
                {
                    r = ConvertCsrSdioToCsrHipResult(card, sr);
                    unifi_error(card->ospriv, "CsrSdioFunctionEnable failed %d (HIP %d)\n", sr, r);
                }
                continue;
            }

            /* Second try, set awake */
            unifi_info(card->ospriv, "Try set awake\n");

            /* Ensure chip is awake */
            r = unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
            if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                return r;
            }
            if (r != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "unifi_set_host_state() failed2 %d\n", r);
            }

            /* Set clock speed low in case setting the host state raised it, which
             * would only happen if host state was previously TORPID
             */
            sr = CsrSdioMaxBusClockFrequencySet(card->sdio_if, UNIFI_SDIO_CLOCK_SAFE_HZ);
            if (sr != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "CsrSdioMaxBusClockFrequencySet() failed2 %d\n", sr);
            }
            card->sdio_clock_speed = UNIFI_SDIO_CLOCK_SAFE_HZ;

            if (i == 1)
            {
                continue;
            }

            /* Perform a s/w reset to preserve as much as the card state as possible,
             * (mainly the preserve RAM). The context will be lost for coredump - but as we
             * were unable to access the WLAN function for panic, the coredump would have
             * also failed without a reset.
             */
            unifi_info(card->ospriv, "Try s/w reset\n");

            r = unifi_card_hard_reset(card);
            if (r != CSR_RESULT_SUCCESS)
            {
                unifi_error(card->ospriv, "unifi_card_hard_reset() failed %d\n", r);
            }
        }
        else
        {
            if (i > 0)
            {
                unifi_info(card->ospriv, "Read chip version 0x%x after %d retries\n", data_u16, i);
            }
            break;
        }
    }

    r = ConvertCsrSdioToCsrHipResult(card, sr);
    func_exit_r(r);
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_read_panic
 *      Reads, saves and prints panic codes stored by the firmware in UniFi's
 *      preserve RAM by the last panic that occurred since chip was powered.
 *      Nothing is saved if the panic codes are read as zero.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 * ---------------------------------------------------------------------------
 */
void unifi_read_panic(card_t *card)
{
    CsrResult r;
    u16 p_code, p_arg;

    func_enter();

    /* The firmware must have previously initialised to read the panic addresses
     * from the SLUT
     */
    if (!card->panic_data_phy_addr || !card->panic_data_mac_addr)
    {
        return;
    }

    /* Get the panic data from PHY */
    r = unifi_card_read16(card, card->panic_data_phy_addr, &p_code);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "capture_panic: unifi_read16 %08x failed %d\n", card->panic_data_phy_addr, r);
        p_code = 0;
    }
    if (p_code)
    {
        r = unifi_card_read16(card, card->panic_data_phy_addr + 2, &p_arg);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "capture_panic: unifi_read16 %08x failed %d\n", card->panic_data_phy_addr + 2, r);
        }
        unifi_error(card->ospriv, "Last UniFi PHY PANIC %04x arg %04x\n", p_code, p_arg);
        card->last_phy_panic_code = p_code;
        card->last_phy_panic_arg = p_arg;
    }

    /* Get the panic data from MAC */
    r = unifi_card_read16(card, card->panic_data_mac_addr, &p_code);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "capture_panic: unifi_read16 %08x failed %d\n", card->panic_data_mac_addr, r);
        p_code = 0;
    }
    if (p_code)
    {
        r = unifi_card_read16(card, card->panic_data_mac_addr + 2, &p_arg);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "capture_panic: unifi_read16 %08x failed %d\n", card->panic_data_mac_addr + 2, r);
        }
        unifi_error(card->ospriv, "Last UniFi MAC PANIC %04x arg %04x\n", p_code, p_arg);
        card->last_mac_panic_code = p_code;
        card->last_mac_panic_arg = p_arg;
    }

    func_exit();
}


/*
 * ---------------------------------------------------------------------------
 *  card_allocate_memory_resources
 *
 *      Allocates memory for the from-host, to-host bulk data slots,
 *      soft queue buffers and bulk data buffers.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on failure.
 * ---------------------------------------------------------------------------
 */
static CsrResult card_allocate_memory_resources(card_t *card)
{
    s16 n, i, k, r;
    sdio_config_data_t *cfg_data;

    func_enter();

    /* Reset any state carried forward from a previous life */
    card->fh_command_queue.q_rd_ptr = 0;
    card->fh_command_queue.q_wr_ptr = 0;
    (void)CsrSnprintf(card->fh_command_queue.name, UNIFI_QUEUE_NAME_MAX_LENGTH,
                      "fh_cmd_q");
    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        card->fh_traffic_queue[i].q_rd_ptr = 0;
        card->fh_traffic_queue[i].q_wr_ptr = 0;
        (void)CsrSnprintf(card->fh_traffic_queue[i].name,
                          UNIFI_QUEUE_NAME_MAX_LENGTH, "fh_data_q%d", i);
    }
#ifndef CSR_WIFI_HIP_TA_DISABLE
    unifi_ta_sampling_init(card);
#endif
    /* Convenience short-cut */
    cfg_data = &card->config_data;

    /*
     * Allocate memory for the from-host and to-host signal buffers.
     */
    card->fh_buffer.buf = CsrMemAllocDma(UNIFI_FH_BUF_SIZE);
    if (card->fh_buffer.buf == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for F-H signals\n");
        func_exit_r(CSR_WIFI_HIP_RESULT_NO_MEMORY);
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }
    card->fh_buffer.bufsize = UNIFI_FH_BUF_SIZE;
    card->fh_buffer.ptr = card->fh_buffer.buf;
    card->fh_buffer.count = 0;

    card->th_buffer.buf = CsrMemAllocDma(UNIFI_FH_BUF_SIZE);
    if (card->th_buffer.buf == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for T-H signals\n");
        func_exit_r(CSR_WIFI_HIP_RESULT_NO_MEMORY);
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }
    card->th_buffer.bufsize = UNIFI_FH_BUF_SIZE;
    card->th_buffer.ptr = card->th_buffer.buf;
    card->th_buffer.count = 0;


    /*
     * Allocate memory for the from-host and to-host bulk data slots.
     * This is done as separate CsrPmemAllocs because lots of smaller
     * allocations are more likely to succeed than one huge one.
     */

    /* Allocate memory for the array of pointers */
    n = cfg_data->num_fromhost_data_slots;

    unifi_trace(card->ospriv, UDBG3, "Alloc from-host resources, %d slots.\n", n);
    card->from_host_data =
        (slot_desc_t *)CsrMemAlloc(n * sizeof(slot_desc_t));
    if (card->from_host_data == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for F-H bulk data array\n");
        func_exit_r(CSR_WIFI_HIP_RESULT_NO_MEMORY);
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }

    /* Initialise from-host bulk data slots */
    for (i = 0; i < n; i++)
    {
        UNIFI_INIT_BULK_DATA(&card->from_host_data[i].bd);
    }

    /* Allocate memory for the array used for slot host tag mapping */
    card->fh_slot_host_tag_record =
        (u32 *)CsrMemAlloc(n * sizeof(u32));

    if (card->fh_slot_host_tag_record == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for F-H slot host tag mapping array\n");
        func_exit_r(CSR_WIFI_HIP_RESULT_NO_MEMORY);
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }

    /* Initialise host tag entries for from-host bulk data slots */
    for (i = 0; i < n; i++)
    {
        card->fh_slot_host_tag_record[i] = CSR_WIFI_HIP_RESERVED_HOST_TAG;
    }


    /* Allocate memory for the array of pointers */
    n = cfg_data->num_tohost_data_slots;

    unifi_trace(card->ospriv, UDBG3, "Alloc to-host resources, %d slots.\n", n);
    card->to_host_data =
        (bulk_data_desc_t *)CsrMemAlloc(n * sizeof(bulk_data_desc_t));
    if (card->to_host_data == NULL)
    {
        unifi_error(card->ospriv, "Failed to allocate memory for T-H bulk data array\n");
        func_exit_r(CSR_WIFI_HIP_RESULT_NO_MEMORY);
        return CSR_WIFI_HIP_RESULT_NO_MEMORY;
    }

    /* Initialise to-host bulk data slots */
    for (i = 0; i < n; i++)
    {
        UNIFI_INIT_BULK_DATA(&card->to_host_data[i]);
    }

    /*
     * Initialise buffers for soft Q
     */
    for (i = 0; i < UNIFI_SOFT_COMMAND_Q_LENGTH; i++)
    {
        for (r = 0; r < UNIFI_MAX_DATA_REFERENCES; r++)
        {
            UNIFI_INIT_BULK_DATA(&card->fh_command_q_body[i].bulkdata[r]);
        }
    }

    for (k = 0; k < UNIFI_NO_OF_TX_QS; k++)
    {
        for (i = 0; i < UNIFI_SOFT_TRAFFIC_Q_LENGTH; i++)
        {
            for (r = 0; r < UNIFI_MAX_DATA_REFERENCES; r++)
            {
                UNIFI_INIT_BULK_DATA(&card->fh_traffic_q_body[k][i].bulkdata[r]);
            }
        }
    }

    card->memory_resources_allocated = 1;

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* card_allocate_memory_resources() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_free_bulk_data
 *
 *      Free the data associated to a bulk data structure.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      bulk_data_slot  Pointer to bulk data structure
 *
 *  Returns:
 *      None.
 *
 * ---------------------------------------------------------------------------
 */
static void unifi_free_bulk_data(card_t *card, bulk_data_desc_t *bulk_data_slot)
{
    if (bulk_data_slot->data_length != 0)
    {
        unifi_net_data_free(card->ospriv, bulk_data_slot);
    }
} /* unifi_free_bulk_data() */


/*
 * ---------------------------------------------------------------------------
 *  card_free_memory_resources
 *
 *      Frees memory allocated for the from-host, to-host bulk data slots,
 *      soft queue buffers and bulk data buffers.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void card_free_memory_resources(card_t *card)
{
    func_enter();

    unifi_trace(card->ospriv, UDBG1, "Freeing card memory resources.\n");

    /* Clear our internal queues */
    unifi_cancel_pending_signals(card);


    if (card->to_host_data)
    {
        CsrMemFree(card->to_host_data);
        card->to_host_data = NULL;
    }

    if (card->from_host_data)
    {
        CsrMemFree(card->from_host_data);
        card->from_host_data = NULL;
    }

    /* free the memory for slot host tag mapping array */
    if (card->fh_slot_host_tag_record)
    {
        CsrMemFree(card->fh_slot_host_tag_record);
        card->fh_slot_host_tag_record = NULL;
    }

    if (card->fh_buffer.buf)
    {
        CsrMemFreeDma(card->fh_buffer.buf);
    }
    card->fh_buffer.ptr = card->fh_buffer.buf = NULL;
    card->fh_buffer.bufsize = 0;
    card->fh_buffer.count = 0;

    if (card->th_buffer.buf)
    {
        CsrMemFreeDma(card->th_buffer.buf);
    }
    card->th_buffer.ptr = card->th_buffer.buf = NULL;
    card->th_buffer.bufsize = 0;
    card->th_buffer.count = 0;


    card->memory_resources_allocated = 0;

    func_exit();
} /* card_free_memory_resources() */


static void card_init_soft_queues(card_t *card)
{
    s16 i;

    func_enter();

    unifi_trace(card->ospriv, UDBG1, "Initialising internal signal queues.\n");
    /* Reset any state carried forward from a previous life */
    card->fh_command_queue.q_rd_ptr = 0;
    card->fh_command_queue.q_wr_ptr = 0;
    (void)CsrSnprintf(card->fh_command_queue.name, UNIFI_QUEUE_NAME_MAX_LENGTH,
                      "fh_cmd_q");
    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        card->fh_traffic_queue[i].q_rd_ptr = 0;
        card->fh_traffic_queue[i].q_wr_ptr = 0;
        (void)CsrSnprintf(card->fh_traffic_queue[i].name,
                          UNIFI_QUEUE_NAME_MAX_LENGTH, "fh_data_q%d", i);
    }
#ifndef CSR_WIFI_HIP_TA_DISABLE
    unifi_ta_sampling_init(card);
#endif
    func_exit();
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_cancel_pending_signals
 *
 *      Free the signals and associated bulk data, pending in the core.
 *
 *  Arguments:
 *      card        Pointer to card struct
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void unifi_cancel_pending_signals(card_t *card)
{
    s16 i, n, r;
    func_enter();

    unifi_trace(card->ospriv, UDBG1, "Canceling pending signals.\n");

    if (card->to_host_data)
    {
        /*
         * Free any bulk data buffers allocated for the t-h slots
         * This will clear all buffers that did not make it to
         * unifi_receive_event() before cancel was request.
         */
        n = card->config_data.num_tohost_data_slots;
        unifi_trace(card->ospriv, UDBG3, "Freeing to-host resources, %d slots.\n", n);
        for (i = 0; i < n; i++)
        {
            unifi_free_bulk_data(card, &card->to_host_data[i]);
        }
    }

    /*
     * If any of the from-host bulk data has reached the card->from_host_data
     * but not UniFi, we need to free the buffers here.
     */
    if (card->from_host_data)
    {
        /* Free any bulk data buffers allocated for the f-h slots */
        n = card->config_data.num_fromhost_data_slots;
        unifi_trace(card->ospriv, UDBG3, "Freeing from-host resources, %d slots.\n", n);
        for (i = 0; i < n; i++)
        {
            unifi_free_bulk_data(card, &card->from_host_data[i].bd);
        }

        for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
        {
            card->dynamic_slot_data.from_host_used_slots[i] = 0;
            card->dynamic_slot_data.from_host_max_slots[i] = 0;
            card->dynamic_slot_data.from_host_reserved_slots[i] = 0;
        }
    }

    /*
     * Free any bulk data buffers allocated in the soft queues.
     * This covers the case where a bulk data pointer has reached the soft queue
     * but not the card->from_host_data.
     */
    unifi_trace(card->ospriv, UDBG3, "Freeing cmd q resources.\n");
    for (i = 0; i < UNIFI_SOFT_COMMAND_Q_LENGTH; i++)
    {
        for (r = 0; r < UNIFI_MAX_DATA_REFERENCES; r++)
        {
            unifi_free_bulk_data(card, &card->fh_command_q_body[i].bulkdata[r]);
        }
    }

    unifi_trace(card->ospriv, UDBG3, "Freeing traffic q resources.\n");
    for (n = 0; n < UNIFI_NO_OF_TX_QS; n++)
    {
        for (i = 0; i < UNIFI_SOFT_TRAFFIC_Q_LENGTH; i++)
        {
            for (r = 0; r < UNIFI_MAX_DATA_REFERENCES; r++)
            {
                unifi_free_bulk_data(card, &card->fh_traffic_q_body[n][i].bulkdata[r]);
            }
        }
    }

    card_init_soft_queues(card);

    func_exit();
} /* unifi_cancel_pending_signals() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_free_card
 *
 *      Free the memory allocated for the card structure and buffers.
 *
 *  Notes:
 *      The porting layer is responsible for freeing any mini-coredump buffers
 *      allocated when it called unifi_coredump_init(), by calling
 *      unifi_coredump_free() before calling this function.
 *
 *  Arguments:
 *      card        Pointer to card struct
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void unifi_free_card(card_t *card)
{
    func_enter();
#ifdef CSR_PRE_ALLOC_NET_DATA
    prealloc_netdata_free(card);
#endif
    /* Free any memory allocated. */
    card_free_memory_resources(card);

    /* Warn if caller didn't free coredump buffers */
    if (card->dump_buf)
    {
        unifi_error(card->ospriv, "Caller should call unifi_coredump_free()\n");
        unifi_coredump_free(card); /* free anyway to prevent memory leak */
    }

    CsrMemFree(card);

    func_exit();
} /* unifi_free_card() */


/*
 * ---------------------------------------------------------------------------
 *  card_init_slots
 *
 *      Allocate memory for host-side slot data and signal queues.
 *
 * Arguments:
 *      card            Pointer to card object
 *
 * Returns:
 *      CSR error code.
 * ---------------------------------------------------------------------------
 */
static CsrResult card_init_slots(card_t *card)
{
    CsrResult r;
    u8 i;

    func_enter();

    /* Allocate the buffers we need, only once. */
    if (card->memory_resources_allocated == 1)
    {
        card_free_memory_resources(card);
    }
    else
    {
        /* Initialise our internal command and traffic queues */
        card_init_soft_queues(card);
    }

    r = card_allocate_memory_resources(card);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to allocate card memory resources.\n");
        card_free_memory_resources(card);
        func_exit_r(r);
        return r;
    }

    if (card->sdio_ctrl_addr == 0)
    {
        unifi_error(card->ospriv, "Failed to find config struct!\n");
        func_exit_r(CSR_WIFI_HIP_RESULT_INVALID_VALUE);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    /*
     * Set initial counts.
     */

    card->from_host_data_head = 0;

    /* Get initial signal counts from UniFi, in case it has not been reset. */
    {
        u16 s;

        /* Get the from-host-signals-written count */
        r = unifi_card_read16(card, card->sdio_ctrl_addr + 0, &s);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to read from-host sig written count\n");
            func_exit_r(r);
            return r;
        }
        card->from_host_signals_w = (s16)s;

        /* Get the to-host-signals-written count */
        r = unifi_card_read16(card, card->sdio_ctrl_addr + 6, &s);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to read to-host sig read count\n");
            func_exit_r(r);
            return r;
        }
        card->to_host_signals_r = (s16)s;
    }

    /* Set Initialised flag. */
    r = unifi_card_write16(card, card->init_flag_addr, 0x0001);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write initialised flag\n");
        func_exit_r(r);
        return r;
    }

    /* Dynamic queue reservation */
    CsrMemSet(&card->dynamic_slot_data, 0, sizeof(card_dynamic_slot_t));

    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        card->dynamic_slot_data.from_host_max_slots[i] = card->config_data.num_fromhost_data_slots -
                                                         UNIFI_RESERVED_COMMAND_SLOTS;
        card->dynamic_slot_data.queue_stable[i] = FALSE;
    }

    card->dynamic_slot_data.packets_interval = UNIFI_PACKETS_INTERVAL;

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* card_init_slots() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_set_udi_hook
 *
 *      Registers the udi hook that reports the sent signals to the core.
 *
 *  Arguments:
 *      card            Pointer to the card context struct
 *      udi_fn          Pointer to the callback function.
 *
 *  Returns:
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE if the card pointer is invalid,
 *      CSR_RESULT_SUCCESS on success.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_set_udi_hook(card_t *card, udi_func_t udi_fn)
{
    if (card == NULL)
    {
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    if (card->udi_hook == NULL)
    {
        card->udi_hook = udi_fn;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_set_udi_hook() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_remove_udi_hook
 *
 *      Removes the udi hook that reports the sent signals from the core.
 *
 *  Arguments:
 *      card            Pointer to the card context struct
 *      udi_fn          Pointer to the callback function.
 *
 *  Returns:
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE if the card pointer is invalid,
 *      CSR_RESULT_SUCCESS on success.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_remove_udi_hook(card_t *card, udi_func_t udi_fn)
{
    if (card == NULL)
    {
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    if (card->udi_hook == udi_fn)
    {
        card->udi_hook = NULL;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_remove_udi_hook() */


static void CardReassignDynamicReservation(card_t *card)
{
    u8 i;

    func_enter();

    unifi_trace(card->ospriv, UDBG5, "Packets Txed %d %d %d %d\n",
                card->dynamic_slot_data.packets_txed[0],
                card->dynamic_slot_data.packets_txed[1],
                card->dynamic_slot_data.packets_txed[2],
                card->dynamic_slot_data.packets_txed[3]);

    /* Clear reservation and recalculate max slots */
    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        card->dynamic_slot_data.queue_stable[i] = FALSE;
        card->dynamic_slot_data.from_host_reserved_slots[i] = 0;
        card->dynamic_slot_data.from_host_max_slots[i] = card->config_data.num_fromhost_data_slots -
                                                         UNIFI_RESERVED_COMMAND_SLOTS;
        card->dynamic_slot_data.packets_txed[i] = 0;

        unifi_trace(card->ospriv, UDBG5, "CardReassignDynamicReservation: queue %d reserved %d Max %d\n", i,
                    card->dynamic_slot_data.from_host_reserved_slots[i],
                    card->dynamic_slot_data.from_host_max_slots[i]);
    }

    card->dynamic_slot_data.total_packets_txed = 0;
    func_exit();
}


/* Algorithm to dynamically reserve slots. The logic is based mainly on the outstanding queue
 * length. Slots are reserved for particular queues during an interval and cleared after the interval.
 * Each queue has three associated variables.. a) used slots - the number of slots currently occupied
 * by the queue b) reserved slots - number of slots reserved specifically for the queue c) max slots - total
 * slots that this queue can actually use (may be higher than reserved slots and is dependent on reserved slots
 * for other queues).
 * This function is called when there are no slots available for a queue. It checks to see if there are enough
 * unreserved slots sufficient for this request. If available these slots are reserved for the queue.
 * If there are not enough unreserved slots, a fair share for each queue is calculated based on the total slots
 * and the number of active queues (any queue with existing reservation is considered active). Queues needing
 * less than their fair share are allowed to have the previously reserved slots. The remaining slots are
 * distributed evenly among queues that need more than the fair share
 *
 * A better scheme would take current bandwidth per AC into consideration when reserving slots. An
 * implementation scheme could consider the relative time/service period for slots in an AC. If the firmware
 * services other ACs faster than a particular AC (packets wait in the slots longer) then it is fair to reserve
 * less slots for the AC
 */
static void CardCheckDynamicReservation(card_t *card, unifi_TrafficQueue queue)
{
    u16 q_len, active_queues = 0, excess_queue_slots, div_extra_slots,
              queue_fair_share, reserved_slots = 0, q, excess_need_queues = 0, unmovable_slots = 0;
    s32 i;
    q_t *sigq;
    u16 num_data_slots = card->config_data.num_fromhost_data_slots - UNIFI_RESERVED_COMMAND_SLOTS;

    func_enter();

    /* Calculate the pending queue length */
    sigq = &card->fh_traffic_queue[queue];
    q_len = CSR_WIFI_HIP_Q_SLOTS_USED(sigq);

    if (q_len <= card->dynamic_slot_data.from_host_reserved_slots[queue])
    {
        unifi_trace(card->ospriv, UDBG5, "queue %d q_len %d already has that many reserved slots, exiting\n", queue, q_len);
        func_exit();
        return;
    }

    /* Upper limit */
    if (q_len > num_data_slots)
    {
        q_len = num_data_slots;
    }

    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        if (i != (s32)queue)
        {
            reserved_slots += card->dynamic_slot_data.from_host_reserved_slots[i];
        }
        if ((i == (s32)queue) || (card->dynamic_slot_data.from_host_reserved_slots[i] > 0))
        {
            active_queues++;
        }
    }

    unifi_trace(card->ospriv, UDBG5, "CardCheckDynamicReservation: queue %d q_len %d\n", queue, q_len);
    unifi_trace(card->ospriv, UDBG5, "Active queues %d reserved slots on other queues %d\n",
                active_queues, reserved_slots);

    if (reserved_slots + q_len <= num_data_slots)
    {
        card->dynamic_slot_data.from_host_reserved_slots[queue] = q_len;
        if (q_len == num_data_slots)
        {
            /* This is the common case when just 1 stream is going */
            card->dynamic_slot_data.queue_stable[queue] = TRUE;
        }
    }
    else
    {
        queue_fair_share = num_data_slots / active_queues;
        unifi_trace(card->ospriv, UDBG5, "queue fair share %d\n", queue_fair_share);

        /* Evenly distribute slots among active queues */
        /* Find out the queues that need excess of fair share. Also find slots allocated
         * to queues less than their fair share, these slots cannot be reallocated (unmovable slots) */

        card->dynamic_slot_data.from_host_reserved_slots[queue] = q_len;

        for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
        {
            if (card->dynamic_slot_data.from_host_reserved_slots[i] > queue_fair_share)
            {
                excess_need_queues++;
            }
            else
            {
                unmovable_slots += card->dynamic_slot_data.from_host_reserved_slots[i];
            }
        }

        unifi_trace(card->ospriv, UDBG5, "Excess need queues %d\n", excess_need_queues);

        /* Now find the slots per excess demand queue */
        excess_queue_slots = (num_data_slots - unmovable_slots) / excess_need_queues;
        div_extra_slots = (num_data_slots - unmovable_slots) - excess_queue_slots * excess_need_queues;
        for (i = UNIFI_NO_OF_TX_QS - 1; i >= 0; i--)
        {
            if (card->dynamic_slot_data.from_host_reserved_slots[i] > excess_queue_slots)
            {
                card->dynamic_slot_data.from_host_reserved_slots[i] = excess_queue_slots;
                if (div_extra_slots > 0)
                {
                    card->dynamic_slot_data.from_host_reserved_slots[i]++;
                    div_extra_slots--;
                }
                /* No more slots will be allocated to this queue during the current interval */
                card->dynamic_slot_data.queue_stable[i] = TRUE;
                unifi_trace(card->ospriv, UDBG5, "queue stable %d\n", i);
            }
        }
    }

    /* Redistribute max slots */
    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        reserved_slots = 0;
        for (q = 0; q < UNIFI_NO_OF_TX_QS; q++)
        {
            if (i != q)
            {
                reserved_slots += card->dynamic_slot_data.from_host_reserved_slots[q];
            }
        }

        card->dynamic_slot_data.from_host_max_slots[i] = num_data_slots - reserved_slots;
        unifi_trace(card->ospriv, UDBG5, "queue %d reserved %d Max %d\n", i,
                    card->dynamic_slot_data.from_host_reserved_slots[i],
                    card->dynamic_slot_data.from_host_max_slots[i]);
    }

    func_exit();
}


/*
 * ---------------------------------------------------------------------------
 *  CardClearFromHostDataSlot
 *
 *      Clear a the given data slot, making it available again.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *      slot            Index of the signal slot to clear.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void CardClearFromHostDataSlot(card_t *card, const s16 slot)
{
    u8 queue = card->from_host_data[slot].queue;
    const void *os_data_ptr = card->from_host_data[slot].bd.os_data_ptr;

    func_enter();

    if (card->from_host_data[slot].bd.data_length == 0)
    {
        unifi_warning(card->ospriv,
                      "Surprise: request to clear an already free FH data slot: %d\n",
                      slot);
        func_exit();
        return;
    }

    if (os_data_ptr == NULL)
    {
        unifi_warning(card->ospriv,
                      "Clearing FH data slot %d: has null payload, len=%d\n",
                      slot, card->from_host_data[slot].bd.data_length);
    }

    /* Free card->from_host_data[slot].bd.os_net_ptr here. */
    /* Mark slot as free by setting length to 0. */
    unifi_free_bulk_data(card, &card->from_host_data[slot].bd);
    if (queue < UNIFI_NO_OF_TX_QS)
    {
        if (card->dynamic_slot_data.from_host_used_slots[queue] == 0)
        {
            unifi_error(card->ospriv, "Goofed up used slots q = %d used slots = %d\n",
                        queue,
                        card->dynamic_slot_data.from_host_used_slots[queue]);
        }
        else
        {
            card->dynamic_slot_data.from_host_used_slots[queue]--;
        }
        card->dynamic_slot_data.packets_txed[queue]++;
        card->dynamic_slot_data.total_packets_txed++;
        if (card->dynamic_slot_data.total_packets_txed >= card->dynamic_slot_data.packets_interval)
        {
            CardReassignDynamicReservation(card);
        }
    }

    unifi_trace(card->ospriv, UDBG4, "CardClearFromHostDataSlot: slot %d recycled %p\n", slot, os_data_ptr);

    func_exit();
} /* CardClearFromHostDataSlot() */


#ifdef CSR_WIFI_REQUEUE_PACKET_TO_HAL
/*
 * ---------------------------------------------------------------------------
 *  CardClearFromHostDataSlotWithoutFreeingBulkData
 *
 *      Clear the given data slot with out freeing the bulk data.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *      slot            Index of the signal slot to clear.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void CardClearFromHostDataSlotWithoutFreeingBulkData(card_t *card, const s16 slot)
{
    u8 queue = card->from_host_data[slot].queue;

    /* Initialise the from_host data slot so it can be re-used,
     * Set length field in from_host_data array to 0.
     */
    UNIFI_INIT_BULK_DATA(&card->from_host_data[slot].bd);

    queue = card->from_host_data[slot].queue;

    if (queue < UNIFI_NO_OF_TX_QS)
    {
        if (card->dynamic_slot_data.from_host_used_slots[queue] == 0)
        {
            unifi_error(card->ospriv, "Goofed up used slots q = %d used slots = %d\n",
                        queue,
                        card->dynamic_slot_data.from_host_used_slots[queue]);
        }
        else
        {
            card->dynamic_slot_data.from_host_used_slots[queue]--;
        }
        card->dynamic_slot_data.packets_txed[queue]++;
        card->dynamic_slot_data.total_packets_txed++;
        if (card->dynamic_slot_data.total_packets_txed >=
            card->dynamic_slot_data.packets_interval)
        {
            CardReassignDynamicReservation(card);
        }
    }
} /* CardClearFromHostDataSlotWithoutFreeingBulkData() */


#endif

u16 CardGetDataSlotSize(card_t *card)
{
    return card->config_data.data_slot_size;
} /* CardGetDataSlotSize() */


/*
 * ---------------------------------------------------------------------------
 *  CardGetFreeFromHostDataSlots
 *
 *      Retrieve the number of from-host bulk data slots available.
 *
 *  Arguments:
 *      card            Pointer to the card context struct
 *
 *  Returns:
 *      Number of free from-host bulk data slots.
 * ---------------------------------------------------------------------------
 */
u16 CardGetFreeFromHostDataSlots(card_t *card)
{
    u16 i, n = 0;

    func_enter();

    /* First two slots reserved for MLME */
    for (i = 0; i < card->config_data.num_fromhost_data_slots; i++)
    {
        if (card->from_host_data[i].bd.data_length == 0)
        {
            /* Free slot */
            n++;
        }
    }

    func_exit();
    return n;
} /* CardGetFreeFromHostDataSlots() */


/*
 * ---------------------------------------------------------------------------
 *  CardAreAllFromHostDataSlotsEmpty
 *
 *      Returns the state of from-host bulk data slots.
 *
 *  Arguments:
 *      card            Pointer to the card context struct
 *
 *  Returns:
 *      1       The from-host bulk data slots are all empty (available).
 *      0       Some or all the from-host bulk data slots are in use.
 * ---------------------------------------------------------------------------
 */
u16 CardAreAllFromHostDataSlotsEmpty(card_t *card)
{
    u16 i;

    for (i = 0; i < card->config_data.num_fromhost_data_slots; i++)
    {
        if (card->from_host_data[i].bd.data_length != 0)
        {
            return 0;
        }
    }

    return 1;
} /* CardGetFreeFromHostDataSlots() */


static CsrResult unifi_identify_hw(card_t *card)
{
    func_enter();

    card->chip_id = card->sdio_if->sdioId.cardId;
    card->function = card->sdio_if->sdioId.sdioFunction;
    card->sdio_io_block_size = card->sdio_if->blockSize;

    /* If SDIO controller doesn't support byte mode CMD53, pad transfers to block sizes */
    card->sdio_io_block_pad = (card->sdio_if->features & CSR_SDIO_FEATURE_BYTE_MODE)?FALSE : TRUE;

    /*
     * Setup the chip helper so that we can access the registers (and
     * also tell what sub-type of HIP we should use).
     */
    card->helper = ChipHelper_GetVersionSdio((u8)card->chip_id);
    if (!card->helper)
    {
        unifi_error(card->ospriv, "Null ChipHelper\n");
    }

    unifi_info(card->ospriv, "Chip ID 0x%02X  Function %u  Block Size %u  Name %s(%s)\n",
               card->chip_id, card->function, card->sdio_io_block_size,
               ChipHelper_MarketingName(card->helper),
               ChipHelper_FriendlyName(card->helper));

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* unifi_identify_hw() */


static CsrResult unifi_prepare_hw(card_t *card)
{
    CsrResult r;
    CsrResult csrResult;
    enum unifi_host_state old_state = card->host_state;

    func_enter();

    r = unifi_identify_hw(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to identify hw\n");
        func_exit_r(r);
        return r;
    }

    unifi_trace(card->ospriv, UDBG1,
                "%s mode SDIO\n", card->sdio_io_block_pad?"Block" : "Byte");
    /*
     * Chip must be a awake or blocks that are asleep may not get
     * reset.  We can only do this after we have read the chip_id.
     */
    r = unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }

    if (old_state == UNIFI_HOST_STATE_TORPID)
    {
        /* Ensure the initial clock rate is set; if a reset occured when the chip was
         * TORPID, unifi_set_host_state() may have raised it to MAX.
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

    /*
     * The WLAN function must be enabled to access MAILBOX2 and DEBUG_RST
     * registers.
     */
    csrResult = CsrSdioFunctionEnable(card->sdio_if);
    if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
    {
        return CSR_WIFI_HIP_RESULT_NO_DEVICE;
    }
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        r = ConvertCsrSdioToCsrHipResult(card, csrResult);
        /* Can't enable WLAN function. Try resetting the SDIO block. */
        unifi_error(card->ospriv, "Failed to re-enable function %d.\n", card->function);
        func_exit_r(r);
        return r;
    }

    /*
     * Poke some registers to make sure the PLL has started,
     * otherwise memory accesses are likely to fail.
     */
    bootstrap_chip_hw(card);

    /* Try to read the chip version from register. */
    r = unifi_read_chip_version(card);
    if (r != CSR_RESULT_SUCCESS)
    {
        func_exit_r(r);
        return r;
    }

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* unifi_prepare_hw() */


static CsrResult unifi_read_chip_version(card_t *card)
{
    u32 gbl_chip_version;
    CsrResult r;
    u16 ver;

    func_enter();

    gbl_chip_version = ChipHelper_GBL_CHIP_VERSION(card->helper);

    /* Try to read the chip version from register. */
    if (gbl_chip_version != 0)
    {
        r = unifi_read_direct16(card, gbl_chip_version * 2, &ver);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to read GBL_CHIP_VERSION\n");
            func_exit_r(r);
            return r;
        }
        card->chip_version = ver;
    }
    else
    {
        unifi_info(card->ospriv, "Unknown Chip ID, cannot locate GBL_CHIP_VERSION\n");
        r = CSR_RESULT_FAILURE;
    }

    unifi_info(card->ospriv, "Chip Version 0x%04X\n", card->chip_version);

    func_exit_r(r);
    return r;
} /* unifi_read_chip_version() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_reset_hardware
 *
 *      Execute the UniFi reset sequence.
 *
 *      Note: This may fail if the chip is going TORPID so retry at
 *      least once.
 *
 *  Arguments:
 *      card - pointer to card context structure
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error otherwise.
 *
 *  Notes:
 *      Some platforms (e.g. Windows Vista) do not allow access to registers
 *      that are necessary for a software soft reset.
 * ---------------------------------------------------------------------------
 */
static CsrResult unifi_reset_hardware(card_t *card)
{
    CsrResult r;
    u16 new_block_size = UNIFI_IO_BLOCK_SIZE;
    CsrResult csrResult;

    func_enter();

    /* Errors returned by unifi_prepare_hw() are not critical at this point */
    r = unifi_prepare_hw(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }

    /* First try SDIO controller reset, which may power cycle the UniFi, assert
     * its reset line, or not be implemented depending on the platform.
     */
    unifi_info(card->ospriv, "Calling CsrSdioHardReset\n");
    csrResult = CsrSdioHardReset(card->sdio_if);
    if (csrResult == CSR_RESULT_SUCCESS)
    {
        unifi_info(card->ospriv, "CsrSdioHardReset succeeded on reseting UniFi\n");
        r = unifi_prepare_hw(card);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "unifi_prepare_hw failed after hard reset\n");
            func_exit_r(r);
            return r;
        }
    }
    else if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
    {
        return CSR_WIFI_HIP_RESULT_NO_DEVICE;
    }
    else
    {
        /* Falling back to software hard reset methods */
        unifi_info(card->ospriv, "Falling back to software hard reset\n");
        r = unifi_card_hard_reset(card);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "software hard reset failed\n");
            func_exit_r(r);
            return r;
        }

        /* If we fell back to unifi_card_hard_reset() methods, chip version may
         * not have been read. (Note in the unlikely event that it is zero,
         * it will be harmlessly read again)
         */
        if (card->chip_version == 0)
        {
            r = unifi_read_chip_version(card);
            if (r != CSR_RESULT_SUCCESS)
            {
                func_exit_r(r);
                return r;
            }
        }
    }

#ifdef CSR_WIFI_HIP_SDIO_BLOCK_SIZE
    new_block_size = CSR_WIFI_HIP_SDIO_BLOCK_SIZE;
#endif

    /* After hard reset, we need to restore the SDIO block size */
    csrResult = CsrSdioBlockSizeSet(card->sdio_if, new_block_size);
    r = ConvertCsrSdioToCsrHipResult(card, csrResult);

    /* Warn if a different block size was achieved by the transport */
    if (card->sdio_if->blockSize != new_block_size)
    {
        unifi_info(card->ospriv,
                   "Actually got block size %d\n", card->sdio_if->blockSize);
    }

    /* sdio_io_block_size always needs be updated from the achieved block size,
     * as it is used by the OS layer to allocate memory in unifi_net_malloc().
     * Controllers which don't support block mode (e.g. CSPI) will report a
     * block size of zero.
     */
    if (card->sdio_if->blockSize == 0)
    {
        unifi_info(card->ospriv, "Block size 0, block mode not available\n");

        /* Set sdio_io_block_size to 1 so that unifi_net_data_malloc() has a
         * sensible rounding value. Elsewhere padding will already be
         * disabled because the controller supports byte mode.
         */
        card->sdio_io_block_size = 1;

        /* Controller features must declare support for byte mode */
        if (!(card->sdio_if->features & CSR_SDIO_FEATURE_BYTE_MODE))
        {
            unifi_error(card->ospriv, "Requires byte mode\n");
            r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
        }
    }
    else
    {
        /* Padding will be enabled if CSR_SDIO_FEATURE_BYTE_MODE isn't set */
        card->sdio_io_block_size = card->sdio_if->blockSize;
    }


    func_exit_r(r);
    return r;
} /* unifi_reset_hardware() */


/*
 * ---------------------------------------------------------------------------
 *  card_reset_method_io_enable
 *
 *      Issue a hard reset to the hw writing the IO_ENABLE.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      0 on success,
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE   if the card was ejected
 *      CSR_RESULT_FAILURE         if an SDIO error occurred or if a response
 *                                 was not seen in the expected time
 * ---------------------------------------------------------------------------
 */
static CsrResult card_reset_method_io_enable(card_t *card)
{
    CsrResult r;
    CsrResult csrResult;

    func_enter();

    /*
     * This resets only function 1, so should be used in
     * preference to the method below (CSR_FUNC_EN)
     */
    unifi_trace(card->ospriv, UDBG1, "Hard reset (IO_ENABLE)\n");

    csrResult = CsrSdioFunctionDisable(card->sdio_if);
    if (csrResult == CSR_SDIO_RESULT_NO_DEVICE)
    {
        return CSR_WIFI_HIP_RESULT_NO_DEVICE;
    }
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        r = ConvertCsrSdioToCsrHipResult(card, csrResult);
        unifi_warning(card->ospriv, "SDIO error writing IO_ENABLE: %d\n", r);
    }
    else
    {
        /* Delay here to let the reset take affect. */
        CsrThreadSleep(RESET_SETTLE_DELAY);

        r = card_wait_for_unifi_to_disable(card);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }

        if (r == CSR_RESULT_SUCCESS)
        {
            r = card_wait_for_unifi_to_reset(card);
            if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                return r;
            }
        }
    }

    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_trace(card->ospriv, UDBG1, "Hard reset (CSR_FUNC_EN)\n");

        r = sdio_write_f0(card, SDIO_CSR_FUNC_EN, 0);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_warning(card->ospriv, "SDIO error writing SDIO_CSR_FUNC_EN: %d\n", r);
            func_exit_r(r);
            return r;
        }
        else
        {
            /* Delay here to let the reset take affect. */
            CsrThreadSleep(RESET_SETTLE_DELAY);

            r = card_wait_for_unifi_to_reset(card);
            if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                return r;
            }
        }
    }

    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_warning(card->ospriv, "card_reset_method_io_enable failed to reset UniFi\n");
    }

    func_exit();
    return r;
} /* card_reset_method_io_enable() */


/*
 * ---------------------------------------------------------------------------
 *  card_reset_method_dbg_reset
 *
 *      Issue a hard reset to the hw writing the DBG_RESET.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS         on success,
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE   if the card was ejected
 *      CSR_RESULT_FAILURE         if an SDIO error occurred or if a response
 *                                 was not seen in the expected time
 * ---------------------------------------------------------------------------
 */
static CsrResult card_reset_method_dbg_reset(card_t *card)
{
    CsrResult r;

    func_enter();

    /*
     * Prepare UniFi for h/w reset
     */
    if (card->host_state == UNIFI_HOST_STATE_TORPID)
    {
        r = unifi_set_host_state(card, UNIFI_HOST_STATE_DROWSY);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Failed to set UNIFI_HOST_STATE_DROWSY\n");
            func_exit_r(r);
            return r;
        }
        CsrThreadSleep(5);
    }

    r = unifi_card_stop_processor(card, UNIFI_PROC_BOTH);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Can't stop processors\n");
        func_exit();
        return r;
    }

    unifi_trace(card->ospriv, UDBG1, "Hard reset (DBG_RESET)\n");

    /*
     * This register write may fail. The debug reset resets
     * parts of the Function 0 sections of the chip, and
     * therefore the response cannot be sent back to the host.
     */
    r = unifi_write_direct_8_or_16(card, ChipHelper_DBG_RESET(card->helper) * 2, 1);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_warning(card->ospriv, "SDIO error writing DBG_RESET: %d\n", r);
        func_exit_r(r);
        return r;
    }

    /* Delay here to let the reset take affect. */
    CsrThreadSleep(RESET_SETTLE_DELAY);

    r = card_wait_for_unifi_to_reset(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_warning(card->ospriv, "card_reset_method_dbg_reset failed to reset UniFi\n");
    }

    func_exit();
    return r;
} /* card_reset_method_dbg_reset() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_card_hard_reset
 *
 *      Issue reset to hardware, by writing to registers on the card.
 *      Power to the card is preserved.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS         on success,
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE   if the card was ejected
 *      CSR_RESULT_FAILURE         if an SDIO error occurred or if a response
 *                                 was not seen in the expected time
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_card_hard_reset(card_t *card)
{
    CsrResult r;
    const struct chip_helper_reset_values *init_data;
    u32 chunks;

    func_enter();

    /* Clear cache of page registers */
    card->proc_select = (u32)(-1);
    card->dmem_page = (u32)(-1);
    card->pmem_page = (u32)(-1);

    /*
     * We need to have a valid card->helper before we use software hard reset.
     * If unifi_identify_hw() fails to get the card ID, it probably means
     * that there is no way to talk to the h/w.
     */
    r = unifi_identify_hw(card);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "unifi_card_hard_reset failed to identify h/w\n");
        func_exit();
        return r;
    }

    /* Search for some reset code. */
    chunks = ChipHelper_HostResetSequence(card->helper, &init_data);
    if (chunks != 0)
    {
        unifi_error(card->ospriv,
                    "Hard reset (Code download) is unsupported\n");

        func_exit_r(CSR_RESULT_FAILURE);
        return CSR_RESULT_FAILURE;
    }

    if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
    {
        /* The HIP spec considers this a bus-specific reset.
         * This resets only function 1, so should be used in
         * preference to the method below (CSR_FUNC_EN)
         * If this method fails, it means that the f/w is probably
         * not running. In this case, try the DBG_RESET method.
         */
        r = card_reset_method_io_enable(card);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r == CSR_RESULT_SUCCESS)
        {
            func_exit();
            return r;
        }
    }

    /* Software hard reset */
    r = card_reset_method_dbg_reset(card);

    func_exit_r(r);
    return r;
} /* unifi_card_hard_reset() */


/*
 * ---------------------------------------------------------------------------
 *
 *  CardGenInt
 *
 *      Prod the card.
 *      This function causes an internal interrupt to be raised in the
 *      UniFi chip. It is used to signal the firmware that some action has
 *      been completed.
 *      The UniFi Host Interface asks that the value used increments for
 *      debugging purposes.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS         on success,
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE   if the card was ejected
 *      CSR_RESULT_FAILURE         if an SDIO error occurred or if a response
 *                                 was not seen in the expected time
 * ---------------------------------------------------------------------------
 */
CsrResult CardGenInt(card_t *card)
{
    CsrResult r;

    func_enter();

    if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
    {
        r = sdio_write_f0(card, SDIO_CSR_FROM_HOST_SCRATCH0,
                          (u8)card->unifi_interrupt_seq);
    }
    else
    {
        r = unifi_write_direct_8_or_16(card,
                                       ChipHelper_SHARED_IO_INTERRUPT(card->helper) * 2,
                                       (u8)card->unifi_interrupt_seq);
    }
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error writing UNIFI_SHARED_IO_INTERRUPT: %d\n", r);
        func_exit_r(r);
        return r;
    }

    card->unifi_interrupt_seq++;

    func_exit();
    return CSR_RESULT_SUCCESS;
} /* CardGenInt() */


/*
 * ---------------------------------------------------------------------------
 *  CardEnableInt
 *
 *      Enable the outgoing SDIO interrupt from UniFi to the host.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS            on success,
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE      if the card was ejected
 *      CSR_RESULT_FAILURE            if an SDIO error occurred,
 * ---------------------------------------------------------------------------
 */
CsrResult CardEnableInt(card_t *card)
{
    CsrResult r;
    u8 int_enable;

    r = sdio_read_f0(card, SDIO_INT_ENABLE, &int_enable);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error reading SDIO_INT_ENABLE\n");
        return r;
    }

    int_enable |= (1 << card->function) | UNIFI_SD_INT_ENABLE_IENM;

    r = sdio_write_f0(card, SDIO_INT_ENABLE, int_enable);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error writing SDIO_INT_ENABLE\n");
        return r;
    }

    return CSR_RESULT_SUCCESS;
} /* CardEnableInt() */


/*
 * ---------------------------------------------------------------------------
 *  CardDisableInt
 *
 *      Disable the outgoing SDIO interrupt from UniFi to the host.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS            on success,
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE      if the card was ejected
 *      CSR_RESULT_FAILURE            if an SDIO error occurred,
 * ---------------------------------------------------------------------------
 */
CsrResult CardDisableInt(card_t *card)
{
    CsrResult r;
    u8 int_enable;

    r = sdio_read_f0(card, SDIO_INT_ENABLE, &int_enable);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error reading SDIO_INT_ENABLE\n");
        return r;
    }

    int_enable &= ~(1 << card->function);

    r = sdio_write_f0(card, SDIO_INT_ENABLE, int_enable);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error writing SDIO_INT_ENABLE\n");
        return r;
    }

    return CSR_RESULT_SUCCESS;
} /* CardDisableInt() */


/*
 * ---------------------------------------------------------------------------
 *  CardPendingInt
 *
 *      Determine whether UniFi is currently asserting the SDIO interrupt
 *      request.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *      pintr           Pointer to location to write interrupt status,
 *                          TRUE if interrupt pending,
 *                          FALSE if no interrupt pending.
 *  Returns:
 *      CSR_RESULT_SUCCESS            interrupt status read successfully
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE      if the card was ejected
 *      CSR_RESULT_FAILURE            if an SDIO error occurred,
 * ---------------------------------------------------------------------------
 */
CsrResult CardPendingInt(card_t *card, CsrBool *pintr)
{
    CsrResult r;
    u8 pending;

    *pintr = FALSE;

    r = sdio_read_f0(card, SDIO_INT_PENDING, &pending);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error reading SDIO_INT_PENDING\n");
        return r;
    }

    *pintr = (pending & (1 << card->function))?TRUE : FALSE;

    return CSR_RESULT_SUCCESS;
} /* CardPendingInt() */


/*
 * ---------------------------------------------------------------------------
 *  CardClearInt
 *
 *      Clear the UniFi SDIO interrupt request.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS          if pending interrupt was cleared, or no pending interrupt.
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE    if the card was ejected
 *      CSR_RESULT_FAILURE          if an SDIO error occurred,
 * ---------------------------------------------------------------------------
 */
CsrResult CardClearInt(card_t *card)
{
    CsrResult r;
    CsrBool intr;

    if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
    {
        /* CardPendingInt() sets intr, if there is a pending interrupt */
        r = CardPendingInt(card, &intr);
        if (intr == FALSE)
        {
            return r;
        }

        r = sdio_write_f0(card, SDIO_CSR_HOST_INT_CLEAR, 1);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "SDIO error writing SDIO_CSR_HOST_INT_CLEAR\n");
        }
    }
    else
    {
        r = unifi_write_direct_8_or_16(card,
                                       ChipHelper_SDIO_HOST_INT(card->helper) * 2,
                                       0);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            return r;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "SDIO error writing UNIFI_SDIO_HOST_INT\n");
        }
    }

    return r;
} /* CardClearInt() */


/*
 * ---------------------------------------------------------------------------
 *  CardIntEnabled
 *
 *      Determine whether UniFi is currently asserting the SDIO interrupt
 *      request.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *      enabled         Pointer to location to write interrupt enable status,
 *                          TRUE if interrupts enabled,
 *                          FALSE if interupts disabled.
 *
 *  Returns:
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE      if the card was ejected
 *      CSR_RESULT_FAILURE            if an SDIO error occurred,
 * ---------------------------------------------------------------------------
 */
CsrResult CardIntEnabled(card_t *card, CsrBool *enabled)
{
    CsrResult r;
    u8 int_enable;

    r = sdio_read_f0(card, SDIO_INT_ENABLE, &int_enable);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "SDIO error reading SDIO_INT_ENABLE\n");
        return r;
    }

    *enabled = (int_enable & (1 << card->function))?TRUE : FALSE;

    return CSR_RESULT_SUCCESS;
} /* CardIntEnabled() */


/*
 * ---------------------------------------------------------------------------
 *  CardWriteBulkData
 *      Allocate slot in the pending bulkdata arrays and assign it to a signal's
 *      bulkdata reference. The slot is then ready for UniFi's bulkdata commands
 *      to transfer the data to/from the host.
 *
 *  Arguments:
 *      card            Pointer to Card object
 *      csptr           Pending signal pointer, including bulkdata ref
 *      queue           Traffic queue that this signal is using
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS if a free slot was assigned
 *      CSR_RESULT_FAILURE if no slot was available
 * ---------------------------------------------------------------------------
 */
CsrResult CardWriteBulkData(card_t *card, card_signal_t *csptr, unifi_TrafficQueue queue)
{
    u16 i, slots[UNIFI_MAX_DATA_REFERENCES], j = 0;
    u8 *packed_sigptr, num_slots_required = 0;
    bulk_data_desc_t *bulkdata = csptr->bulkdata;
    s16 h, nslots;

    func_enter();

    /* Count the number of slots required */
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++)
    {
        if (bulkdata[i].data_length != 0)
        {
            num_slots_required++;
        }
    }

    /* Get the slot numbers */
    if (num_slots_required != 0)
    {
        /* Last 2 slots for MLME */
        if (queue == UNIFI_TRAFFIC_Q_MLME)
        {
            h = card->config_data.num_fromhost_data_slots - UNIFI_RESERVED_COMMAND_SLOTS;
            for (i = 0; i < card->config_data.num_fromhost_data_slots; i++)
            {
                if (card->from_host_data[h].bd.data_length == 0)
                {
                    /* Free data slot, claim it */
                    slots[j++] = h;
                    if (j == num_slots_required)
                    {
                        break;
                    }
                }

                if (++h >= card->config_data.num_fromhost_data_slots)
                {
                    h = 0;
                }
            }
        }
        else
        {
            if (card->dynamic_slot_data.from_host_used_slots[queue]
                < card->dynamic_slot_data.from_host_max_slots[queue])
            {
                /* Data commands get a free slot only after a few checks */
                nslots = card->config_data.num_fromhost_data_slots - UNIFI_RESERVED_COMMAND_SLOTS;

                h = card->from_host_data_head;

                for (i = 0; i < nslots; i++)
                {
                    if (card->from_host_data[h].bd.data_length == 0)
                    {
                        /* Free data slot, claim it */
                        slots[j++] = h;
                        if (j == num_slots_required)
                        {
                            break;
                        }
                    }

                    if (++h >= nslots)
                    {
                        h = 0;
                    }
                }
                card->from_host_data_head = h;
            }
        }

        /* Required number of slots are not available, bail out */
        if (j != num_slots_required)
        {
            unifi_trace(card->ospriv, UDBG5, "CardWriteBulkData: didn't find free slot/s\n");

            /* If we haven't already reached the stable state we can ask for reservation */
            if ((queue != UNIFI_TRAFFIC_Q_MLME) && (card->dynamic_slot_data.queue_stable[queue] == FALSE))
            {
                CardCheckDynamicReservation(card, queue);
            }

            for (i = 0; i < card->config_data.num_fromhost_data_slots; i++)
            {
                unifi_trace(card->ospriv, UDBG5, "fh data slot %d: %d\n", i, card->from_host_data[i].bd.data_length);
            }
            func_exit();
            return CSR_RESULT_FAILURE;
        }
    }

    packed_sigptr = csptr->sigbuf;

    /* Fill in the slots with data */
    j = 0;
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++)
    {
        if (bulkdata[i].data_length == 0)
        {
            /* Zero-out the DATAREF in the signal */
            SET_PACKED_DATAREF_SLOT(packed_sigptr, i, 0);
            SET_PACKED_DATAREF_LEN(packed_sigptr, i, 0);
        }
        else
        {
            /*
             * Fill in the slot number in the SIGNAL structure but
             * preserve the offset already in there
             */
            SET_PACKED_DATAREF_SLOT(packed_sigptr, i, slots[j] | (((u16)packed_sigptr[SIZEOF_SIGNAL_HEADER + (i * SIZEOF_DATAREF) + 1]) << 8));
            SET_PACKED_DATAREF_LEN(packed_sigptr, i, bulkdata[i].data_length);

            /* Do not copy the data, just store the information to them */
            card->from_host_data[slots[j]].bd.os_data_ptr = bulkdata[i].os_data_ptr;
            card->from_host_data[slots[j]].bd.os_net_buf_ptr = bulkdata[i].os_net_buf_ptr;
            card->from_host_data[slots[j]].bd.data_length = bulkdata[i].data_length;
            card->from_host_data[slots[j]].bd.net_buf_length = bulkdata[i].net_buf_length;
            card->from_host_data[slots[j]].queue = queue;

            unifi_trace(card->ospriv, UDBG4, "CardWriteBulkData sig=0x%x, fh slot %d = %p\n",
                        GET_SIGNAL_ID(packed_sigptr), i, bulkdata[i].os_data_ptr);

            /* Sanity-check that the bulk data desc being assigned to the slot
             * actually has a payload.
             */
            if (!bulkdata[i].os_data_ptr)
            {
                unifi_error(card->ospriv, "Assign null os_data_ptr (len=%d) fh slot %d, i=%d, q=%d, sig=0x%x",
                            bulkdata[i].data_length, slots[j], i, queue, GET_SIGNAL_ID(packed_sigptr));
            }

            j++;
            if (queue < UNIFI_NO_OF_TX_QS)
            {
                card->dynamic_slot_data.from_host_used_slots[queue]++;
            }
        }
    }

    func_exit();

    return CSR_RESULT_SUCCESS;
} /*  CardWriteBulkData() */


/*
 * ---------------------------------------------------------------------------
 *  card_find_data_slot
 *
 *      Dereference references to bulk data slots into pointers to real data.
 *
 *  Arguments:
 *      card            Pointer to the card struct.
 *      slot            Slot number from a signal structure
 *
 *  Returns:
 *      Pointer to entry in bulk_data_slot array.
 * ---------------------------------------------------------------------------
 */
bulk_data_desc_t* card_find_data_slot(card_t *card, s16 slot)
{
    s16 sn;
    bulk_data_desc_t *bd;

    sn = slot & 0x7FFF;

    /* ?? check sanity of slot number ?? */

    if (slot & SLOT_DIR_TO_HOST)
    {
        bd = &card->to_host_data[sn];
    }
    else
    {
        bd = &card->from_host_data[sn].bd;
    }

    return bd;
} /* card_find_data_slot() */


/*
 * ---------------------------------------------------------------------------
 *  firmware_present_in_flash
 *
 *      Probe for external Flash that looks like it might contain firmware.
 *
 *      If Flash is not present, reads always return 0x0008.
 *      If Flash is present, but empty, reads return 0xFFFF.
 *      Anything else is considered to be firmware.
 *
 *  Arguments:
 *      card        Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS                 firmware is present in ROM or flash
 *      CSR_WIFI_HIP_RESULT_NOT_FOUND      firmware is not present in ROM or flash
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE      if the card was ejected
 *      CSR_RESULT_FAILURE                 if an SDIO error occurred
 * ---------------------------------------------------------------------------
 */
static CsrResult firmware_present_in_flash(card_t *card)
{
    CsrResult r;
    u16 m1, m5;

    if (ChipHelper_HasRom(card->helper))
    {
        return CSR_RESULT_SUCCESS;
    }
    if (!ChipHelper_HasFlash(card->helper))
    {
        return CSR_WIFI_HIP_RESULT_NOT_FOUND;
    }

    /*
     * Examine the Flash locations that are the power-on default reset
     * vectors of the XAP processors.
     * These are words 1 and 5 in Flash.
     */
    r = unifi_card_read16(card, UNIFI_MAKE_GP(EXT_FLASH, 2), &m1);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    r = unifi_card_read16(card, UNIFI_MAKE_GP(EXT_FLASH, 10), &m5);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    /* Check for uninitialised/missing flash */
    if ((m1 == 0x0008) || (m1 == 0xFFFF) ||
        (m1 == 0x0004) || (m5 == 0x0004) ||
        (m5 == 0x0008) || (m5 == 0xFFFF))
    {
        return CSR_WIFI_HIP_RESULT_NOT_FOUND;
    }

    return CSR_RESULT_SUCCESS;
} /* firmware_present_in_flash() */


/*
 * ---------------------------------------------------------------------------
 *  bootstrap_chip_hw
 *
 *      Perform chip specific magic to "Get It Working" TM.  This will
 *      increase speed of PLLs in analogue and maybe enable some
 *      on-chip regulators.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void bootstrap_chip_hw(card_t *card)
{
    const struct chip_helper_init_values *vals;
    u32 i, len;
    void *sdio = card->sdio_if;
    CsrResult csrResult;

    len = ChipHelper_ClockStartupSequence(card->helper, &vals);
    if (len != 0)
    {
        for (i = 0; i < len; i++)
        {
            csrResult = CsrSdioWrite16(sdio, vals[i].addr * 2, vals[i].value);
            if (csrResult != CSR_RESULT_SUCCESS)
            {
                unifi_warning(card->ospriv, "Failed to write bootstrap value %d\n", i);
                /* Might not be fatal */
            }

            CsrThreadSleep(1);
        }
    }
} /* bootstrap_chip_hw() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_card_stop_processor
 *
 *      Stop the UniFi XAP processors.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      which           One of UNIFI_PROC_MAC, UNIFI_PROC_PHY, UNIFI_PROC_BOTH
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS if successful, or CSR error code
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_card_stop_processor(card_t *card, enum unifi_dbg_processors_select which)
{
    CsrResult r = CSR_RESULT_SUCCESS;
    u8 status;
    s16 retry = 100;

    while (retry--)
    {
        /* Select both XAPs */
        r = unifi_set_proc_select(card, which);
        if (r != CSR_RESULT_SUCCESS)
        {
            break;
        }

        /* Stop processors */
        r = unifi_write_direct16(card, ChipHelper_DBG_EMU_CMD(card->helper) * 2, 2);
        if (r != CSR_RESULT_SUCCESS)
        {
            break;
        }

        /* Read status */
        r = unifi_read_direct_8_or_16(card,
                                      ChipHelper_DBG_HOST_STOP_STATUS(card->helper) * 2,
                                      &status);
        if (r != CSR_RESULT_SUCCESS)
        {
            break;
        }

        if ((status & 1) == 1)
        {
            /* Success! */
            return CSR_RESULT_SUCCESS;
        }

        /* Processors didn't stop, try again */
    }

    if (r != CSR_RESULT_SUCCESS)
    {
        /* An SDIO error occurred */
        unifi_error(card->ospriv, "Failed to stop processors: SDIO error\n");
    }
    else
    {
        /* If we reach here, we didn't the status in time. */
        unifi_error(card->ospriv, "Failed to stop processors: timeout waiting for stopped status\n");
        r = CSR_RESULT_FAILURE;
    }

    return r;
} /* unifi_card_stop_processor() */


/*
 * ---------------------------------------------------------------------------
 *  card_start_processor
 *
 *      Start the UniFi XAP processors.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      which           One of UNIFI_PROC_MAC, UNIFI_PROC_PHY, UNIFI_PROC_BOTH
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS or CSR error code
 * ---------------------------------------------------------------------------
 */
CsrResult card_start_processor(card_t *card, enum unifi_dbg_processors_select which)
{
    CsrResult r;

    /* Select both XAPs */
    r = unifi_set_proc_select(card, which);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "unifi_set_proc_select failed: %d.\n", r);
        return r;
    }


    r = unifi_write_direct_8_or_16(card,
                                   ChipHelper_DBG_EMU_CMD(card->helper) * 2, 8);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    r = unifi_write_direct_8_or_16(card,
                                   ChipHelper_DBG_EMU_CMD(card->helper) * 2, 0);
    if (r != CSR_RESULT_SUCCESS)
    {
        return r;
    }

    return CSR_RESULT_SUCCESS;
} /* card_start_processor() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_set_interrupt_mode
 *
 *      Configure the interrupt processing mode used by the HIP
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      mode            Interrupt mode to apply
 *
 *  Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
void unifi_set_interrupt_mode(card_t *card, u32 mode)
{
    if (mode == CSR_WIFI_INTMODE_RUN_BH_ONCE)
    {
        unifi_info(card->ospriv, "Scheduled interrupt mode");
    }
    card->intmode = mode;
} /* unifi_set_interrupt_mode() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_start_processors
 *
 *      Start all UniFi XAP processors.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on error
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_start_processors(card_t *card)
{
    return card_start_processor(card, UNIFI_PROC_BOTH);
} /* unifi_start_processors() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_request_max_sdio_clock
 *
 *      Requests that the maximum SDIO clock rate is set at the next suitable
 *      opportunity (e.g. when the BH next runs, so as not to interfere with
 *      any current operation).
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
void unifi_request_max_sdio_clock(card_t *card)
{
    card->request_max_clock = 1;
} /* unifi_request_max_sdio_clock() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_set_host_state
 *
 *      Set the host deep-sleep state.
 *
 *      If transitioning to TORPID, the SDIO driver will be notified
 *      that the SD bus will be unused (idle) and conversely, when
 *      transitioning from TORPID that the bus will be used (active).
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      state           New deep-sleep state.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS            on success
 *      CSR_WIFI_HIP_RESULT_NO_DEVICE      if the card was ejected
 *      CSR_RESULT_FAILURE            if an SDIO error occurred
 *
 *  Notes:
 *      We need to reduce the SDIO clock speed before trying to wake up the
 *      chip. Actually, in the implementation below we reduce the clock speed
 *      not just before we try to wake up the chip, but when we put the chip to
 *      deep sleep. This means that if the f/w wakes up on its' own, we waste
 *      a reduce/increace cycle. However, trying to eliminate this overhead is
 *      proved difficult, as the current state machine in the HIP lib does at
 *      least a CMD52 to disable the interrupts before we configure the host
 *      state.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_set_host_state(card_t *card, enum unifi_host_state state)
{
    CsrResult r = CSR_RESULT_SUCCESS;
    CsrResult csrResult;
    static const char *const states[] = {
        "AWAKE", "DROWSY", "TORPID"
    };
    static const u8 state_csr_host_wakeup[] = {
        1, 3, 0
    };
    static const u8 state_io_abort[] = {
        0, 2, 3
    };

    unifi_trace(card->ospriv, UDBG4, "State %s to %s\n",
                states[card->host_state], states[state]);

    if (card->host_state == UNIFI_HOST_STATE_TORPID)
    {
        CsrSdioFunctionActive(card->sdio_if);
    }

    /* Write the new state to UniFi. */
    if (card->chip_id > SDIO_CARD_ID_UNIFI_2)
    {
        r = sdio_write_f0(card, SDIO_CSR_HOST_WAKEUP,
                          (u8)((card->function << 4) | state_csr_host_wakeup[state]));
    }
    else
    {
        r = sdio_write_f0(card, SDIO_IO_ABORT, state_io_abort[state]);
    }

    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to write UniFi deep sleep state\n");
    }
    else
    {
        /*
         * If the chip was in state TORPID then we can now increase
         * the maximum bus clock speed.
         */
        if (card->host_state == UNIFI_HOST_STATE_TORPID)
        {
            csrResult = CsrSdioMaxBusClockFrequencySet(card->sdio_if,
                                                       UNIFI_SDIO_CLOCK_MAX_HZ);
            r = ConvertCsrSdioToCsrHipResult(card, csrResult);
            /* Non-fatal error */
            if (r != CSR_RESULT_SUCCESS && r != CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                unifi_warning(card->ospriv,
                              "Failed to increase the SDIO clock speed\n");
            }
            else
            {
                card->sdio_clock_speed = UNIFI_SDIO_CLOCK_MAX_HZ;
            }
        }

        /*
         * Cache the current state in the card structure to avoid
         * unnecessary SDIO reads.
         */
        card->host_state = state;

        if (state == UNIFI_HOST_STATE_TORPID)
        {
            /*
             * If the chip is now in state TORPID then we must now decrease
             * the maximum bus clock speed.
             */
            csrResult = CsrSdioMaxBusClockFrequencySet(card->sdio_if,
                                                       UNIFI_SDIO_CLOCK_SAFE_HZ);
            r = ConvertCsrSdioToCsrHipResult(card, csrResult);
            if (r != CSR_RESULT_SUCCESS && r != CSR_WIFI_HIP_RESULT_NO_DEVICE)
            {
                unifi_warning(card->ospriv,
                              "Failed to decrease the SDIO clock speed\n");
            }
            else
            {
                card->sdio_clock_speed = UNIFI_SDIO_CLOCK_SAFE_HZ;
            }
            CsrSdioFunctionIdle(card->sdio_if);
        }
    }

    return r;
} /* unifi_set_host_state() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_card_info
 *
 *      Update the card information data structure
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      card_info       Pointer to info structure to update
 *
 *  Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
void unifi_card_info(card_t *card, card_info_t *card_info)
{
    card_info->chip_id = card->chip_id;
    card_info->chip_version = card->chip_version;
    card_info->fw_build = card->build_id;
    card_info->fw_hip_version = card->config_data.version;
    card_info->sdio_block_size = card->sdio_io_block_size;
} /* unifi_card_info() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_check_io_status
 *
 *      Check UniFi for spontaneous reset and pending interrupt.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      status          Pointer to location to write chip status:
 *                        0 if UniFi is running, and no interrupt pending
 *                        1 if UniFi has spontaneously reset
 *                        2 if there is a pending interrupt
 *  Returns:
 *      CSR_RESULT_SUCCESS if OK, or CSR error
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_check_io_status(card_t *card, s32 *status)
{
    u8 io_en;
    CsrResult r;
    CsrBool pending;

    *status = 0;

    r = sdio_read_f0(card, SDIO_IO_ENABLE, &io_en);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        return r;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to read SDIO_IO_ENABLE to check for spontaneous reset\n");
        return r;
    }

    if ((io_en & (1 << card->function)) == 0)
    {
        s32 fw_count;
        *status = 1;
        unifi_error(card->ospriv, "UniFi has spontaneously reset.\n");

        /*
         * These reads are very likely to fail. We want to know if the function is really
         * disabled or the SDIO driver just returns rubbish.
         */
        fw_count = unifi_read_shared_count(card, card->sdio_ctrl_addr + 4);
        if (fw_count < 0)
        {
            unifi_error(card->ospriv, "Failed to read to-host sig written count\n");
        }
        else
        {
            unifi_error(card->ospriv, "thsw: %u (driver thinks is %u)\n",
                        fw_count, card->to_host_signals_w);
        }
        fw_count = unifi_read_shared_count(card, card->sdio_ctrl_addr + 2);
        if (fw_count < 0)
        {
            unifi_error(card->ospriv, "Failed to read from-host sig read count\n");
        }
        else
        {
            unifi_error(card->ospriv, "fhsr: %u (driver thinks is %u)\n",
                        fw_count, card->from_host_signals_r);
        }

        return r;
    }

    unifi_info(card->ospriv, "UniFi function %d is enabled.\n", card->function);

    /* See if we missed an SDIO interrupt */
    r = CardPendingInt(card, &pending);
    if (pending)
    {
        unifi_error(card->ospriv, "There is an unhandled pending interrupt.\n");
        *status = 2;
        return r;
    }

    return r;
} /* unifi_check_io_status() */


void unifi_get_hip_qos_info(card_t *card, unifi_HipQosInfo *hipqosinfo)
{
    s32 count_fhr;
    s16 t;
    u32 occupied_fh;

    q_t *sigq;
    u16 nslots, i;

    CsrMemSet(hipqosinfo, 0, sizeof(unifi_HipQosInfo));

    nslots = card->config_data.num_fromhost_data_slots;

    for (i = 0; i < nslots; i++)
    {
        if (card->from_host_data[i].bd.data_length == 0)
        {
            hipqosinfo->free_fh_bulkdata_slots++;
        }
    }

    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        sigq = &card->fh_traffic_queue[i];
        t = sigq->q_wr_ptr - sigq->q_rd_ptr;
        if (t < 0)
        {
            t += sigq->q_length;
        }
        hipqosinfo->free_fh_sig_queue_slots[i] = (sigq->q_length - t) - 1;
    }

    count_fhr = unifi_read_shared_count(card, card->sdio_ctrl_addr + 2);
    if (count_fhr < 0)
    {
        unifi_error(card->ospriv, "Failed to read from-host sig read count - %d\n", count_fhr);
        hipqosinfo->free_fh_fw_slots = 0xfa;
        return;
    }

    occupied_fh = (card->from_host_signals_w - count_fhr) % 128;

    hipqosinfo->free_fh_fw_slots = (u16)(card->config_data.num_fromhost_sig_frags - occupied_fh);
}



CsrResult ConvertCsrSdioToCsrHipResult(card_t *card, CsrResult csrResult)
{
    CsrResult r = CSR_RESULT_FAILURE;

    switch (csrResult)
    {
        case CSR_RESULT_SUCCESS:
            r = CSR_RESULT_SUCCESS;
            break;
        /* Timeout errors */
        case CSR_SDIO_RESULT_TIMEOUT:
        /* Integrity errors */
        case CSR_SDIO_RESULT_CRC_ERROR:
            r = CSR_RESULT_FAILURE;
            break;
        case CSR_SDIO_RESULT_NO_DEVICE:
            r = CSR_WIFI_HIP_RESULT_NO_DEVICE;
            break;
        case CSR_SDIO_RESULT_INVALID_VALUE:
            r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            break;
        case CSR_RESULT_FAILURE:
            r = CSR_RESULT_FAILURE;
            break;
        default:
            unifi_warning(card->ospriv, "Unrecognised csrResult error code: %d\n", csrResult);
            break;
    }

    return r;
} /* ConvertCsrSdioToCsrHipResult() */


