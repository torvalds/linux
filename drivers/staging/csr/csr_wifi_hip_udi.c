/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 *  FILE:     csr_wifi_hip_card_udi.c
 *
 *  PURPOSE:
 *      Maintain a list of callbacks to log UniFi exchanges to one or more
 *      debug/monitoring client applications.
 *
 * NOTES:
 *      Just call the UDI driver log fn directly for now.
 *      When done properly, each open() on the UDI device will install
 *      a log function. We will call all log fns whenever a signal is written
 *      to or read form the UniFi.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_card.h"


/*
 * ---------------------------------------------------------------------------
 *  unifi_print_status
 *
 *      Print status info to given character buffer.
 *
 *  Arguments:
 *      None.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
s32 unifi_print_status(card_t *card, char *str, s32 *remain)
{
    char *p = str;
    sdio_config_data_t *cfg;
    u16 i, n;
    s32 remaining = *remain;
    s32 written;
#ifdef CSR_UNSAFE_SDIO_ACCESS
    s32 iostate;
    CsrResult r;
    static const char *const states[] = {
        "AWAKE", "DROWSY", "TORPID"
    };
    #define SHARED_READ_RETRY_LIMIT 10
    u8 b;
#endif

    if (remaining <= 0)
    {
        return 0;
    }

    i = n = 0;
    written = CsrSnprintf(p, remaining, "Chip ID %u\n",
                          (u16)card->chip_id);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "Chip Version %04X\n",
                          card->chip_version);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "HIP v%u.%u\n",
                          (card->config_data.version >> 8) & 0xFF,
                          card->config_data.version & 0xFF);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "Build %lu: %s\n",
                          card->build_id, card->build_id_string);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    cfg = &card->config_data;

    written = CsrSnprintf(p, remaining, "sdio ctrl offset          %u\n",
                          cfg->sdio_ctrl_offset);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "fromhost sigbuf handle    %u\n",
                          cfg->fromhost_sigbuf_handle);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "tohost_sigbuf_handle      %u\n",
                          cfg->tohost_sigbuf_handle);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "num_fromhost_sig_frags    %u\n",
                          cfg->num_fromhost_sig_frags);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "num_tohost_sig_frags      %u\n",
                          cfg->num_tohost_sig_frags);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "num_fromhost_data_slots   %u\n",
                          cfg->num_fromhost_data_slots);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "num_tohost_data_slots     %u\n",
                          cfg->num_tohost_data_slots);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "data_slot_size            %u\n",
                          cfg->data_slot_size);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    /* Added by protocol version 0x0001 */
    written = CsrSnprintf(p, remaining, "overlay_size              %u\n",
                          (u16)cfg->overlay_size);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    /* Added by protocol version 0x0300 */
    written = CsrSnprintf(p, remaining, "data_slot_round           %u\n",
                          cfg->data_slot_round);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "sig_frag_size             %u\n",
                          cfg->sig_frag_size);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    /* Added by protocol version 0x0300 */
    written = CsrSnprintf(p, remaining, "tohost_sig_pad            %u\n",
                          cfg->tohost_signal_padding);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    written = CsrSnprintf(p, remaining, "\nInternal state:\n");
    UNIFI_SNPRINTF_RET(p, remaining, written);

    written = CsrSnprintf(p, remaining, "Last PHY PANIC: %04x:%04x\n",
                          card->last_phy_panic_code, card->last_phy_panic_arg);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "Last MAC PANIC: %04x:%04x\n",
                          card->last_mac_panic_code, card->last_mac_panic_arg);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    written = CsrSnprintf(p, remaining, "fhsr: %u\n",
                          (u16)card->from_host_signals_r);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "fhsw: %u\n",
                          (u16)card->from_host_signals_w);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "thsr: %u\n",
                          (u16)card->to_host_signals_r);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "thsw: %u\n",
                          (u16)card->to_host_signals_w);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining,
                          "fh buffer contains: %u signals, %u bytes\n",
                          card->fh_buffer.count,
                          card->fh_buffer.ptr - card->fh_buffer.buf);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    written = CsrSnprintf(p, remaining, "paused: ");
    UNIFI_SNPRINTF_RET(p, remaining, written);
    for (i = 0; i < sizeof(card->tx_q_paused_flag) / sizeof(card->tx_q_paused_flag[0]); i++)
    {
        written = CsrSnprintf(p, remaining, card->tx_q_paused_flag[i]?"1" : "0");
        UNIFI_SNPRINTF_RET(p, remaining, written);
    }
    written = CsrSnprintf(p, remaining, "\n");
    UNIFI_SNPRINTF_RET(p, remaining, written);

    written = CsrSnprintf(p, remaining,
                          "fh command q: %u waiting, %u free of %u:\n",
                          CSR_WIFI_HIP_Q_SLOTS_USED(&card->fh_command_queue),
                          CSR_WIFI_HIP_Q_SLOTS_FREE(&card->fh_command_queue),
                          UNIFI_SOFT_COMMAND_Q_LENGTH);
    UNIFI_SNPRINTF_RET(p, remaining, written);
    for (i = 0; i < UNIFI_NO_OF_TX_QS; i++)
    {
        written = CsrSnprintf(p, remaining,
                              "fh traffic q[%u]: %u waiting, %u free of %u:\n",
                              i,
                              CSR_WIFI_HIP_Q_SLOTS_USED(&card->fh_traffic_queue[i]),
                              CSR_WIFI_HIP_Q_SLOTS_FREE(&card->fh_traffic_queue[i]),
                              UNIFI_SOFT_TRAFFIC_Q_LENGTH);
        UNIFI_SNPRINTF_RET(p, remaining, written);
    }

    written = CsrSnprintf(p, remaining, "fh data slots free: %u\n",
                          card->from_host_data?CardGetFreeFromHostDataSlots(card) : 0);
    UNIFI_SNPRINTF_RET(p, remaining, written);


    written = CsrSnprintf(p, remaining, "From host data slots:");
    UNIFI_SNPRINTF_RET(p, remaining, written);
    n = card->config_data.num_fromhost_data_slots;
    for (i = 0; i < n && card->from_host_data; i++)
    {
        written = CsrSnprintf(p, remaining, " %u",
                              (u16)card->from_host_data[i].bd.data_length);
        UNIFI_SNPRINTF_RET(p, remaining, written);
    }
    written = CsrSnprintf(p, remaining, "\n");
    UNIFI_SNPRINTF_RET(p, remaining, written);

    written = CsrSnprintf(p, remaining, "To host data slots:");
    UNIFI_SNPRINTF_RET(p, remaining, written);
    n = card->config_data.num_tohost_data_slots;
    for (i = 0; i < n && card->to_host_data; i++)
    {
        written = CsrSnprintf(p, remaining, " %u",
                              (u16)card->to_host_data[i].data_length);
        UNIFI_SNPRINTF_RET(p, remaining, written);
    }

    written = CsrSnprintf(p, remaining, "\n");
    UNIFI_SNPRINTF_RET(p, remaining, written);

#ifdef CSR_UNSAFE_SDIO_ACCESS
    written = CsrSnprintf(p, remaining, "Host State: %s\n", states[card->host_state]);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    r = unifi_check_io_status(card, &iostate);
    if (iostate == 1)
    {
        written = CsrSnprintf(p, remaining, "I/O Check: F1 disabled\n");
        UNIFI_SNPRINTF_RET(p, remaining, written);
    }
    else
    {
        if (iostate == 1)
        {
            written = CsrSnprintf(p, remaining, "I/O Check: pending interrupt\n");
            UNIFI_SNPRINTF_RET(p, remaining, written);
        }

        written = CsrSnprintf(p, remaining, "BH reason interrupt = %d\n",
                              card->bh_reason_unifi);
        UNIFI_SNPRINTF_RET(p, remaining, written);
        written = CsrSnprintf(p, remaining, "BH reason host      = %d\n",
                              card->bh_reason_host);
        UNIFI_SNPRINTF_RET(p, remaining, written);

        for (i = 0; i < SHARED_READ_RETRY_LIMIT; i++)
        {
            r = unifi_read_8_or_16(card, card->sdio_ctrl_addr + 2, &b);
            if ((r == CSR_RESULT_SUCCESS) && (!(b & 0x80)))
            {
                written = CsrSnprintf(p, remaining, "fhsr: %u (driver thinks is %u)\n",
                                      b, card->from_host_signals_r);
                UNIFI_SNPRINTF_RET(p, remaining, written);
                break;
            }
        }
        iostate = unifi_read_shared_count(card, card->sdio_ctrl_addr + 4);
        written = CsrSnprintf(p, remaining, "thsw: %u (driver thinks is %u)\n",
                              iostate, card->to_host_signals_w);
        UNIFI_SNPRINTF_RET(p, remaining, written);
    }
#endif

    written = CsrSnprintf(p, remaining, "\nStats:\n");
    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "Total SDIO bytes: R=%lu W=%lu\n",
                          card->sdio_bytes_read, card->sdio_bytes_written);

    UNIFI_SNPRINTF_RET(p, remaining, written);
    written = CsrSnprintf(p, remaining, "Interrupts generated on card: %lu\n",
                          card->unifi_interrupt_seq);
    UNIFI_SNPRINTF_RET(p, remaining, written);

    *remain = remaining;
    return (p - str);
} /* unifi_print_status() */


