/*
 * SDIO Bluetooth Type-A interface definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef SDIOEMB_SDIO_BT_A_H
#define SDIOEMB_SDIO_BT_A_H

#include <sdioemb/sdio_csr.h>
#include <csr_sdio.h>

/*
 * Standard SDIO function registers for a Bluetooth Type-A interface.
 */
#define SDIO_BT_A_RD 0x00
#define SDIO_BT_A_TD 0x00

#define SDIO_BT_A_RX_PKT_CTRL 0x10
#  define PC_RRT 0x01

#define SDIO_BT_A_TX_PKT_CTRL 0x11
#  define PC_WRT 0x01

#define SDIO_BT_A_RETRY_CTRL  0x12
#  define RTC_STAT 0x01
#  define RTC_SET  0x01

#define SDIO_BT_A_INTRD       0x13
#  define INTRD 0x01
#  define CL_INTRD 0x01

#define SDIO_BT_A_INT_EN      0x14
#  define EN_INTRD 0x01

#define SDIO_BT_A_BT_MODE     0x20
#  define MD_STAT 0x01

/*
 * Length of the Type-A header.
 *
 * Packet length (3 octets) plus Service ID (1 octet).
 */
#define SDIO_BT_A_HEADER_LEN 4

/*
 * Maximum length of a Type-A transport packet.
 *
 * Type-A header length and maximum length of a HCI packet (65535
 * octets).
 */
#define SDIO_BT_A_PACKET_LEN_MAX 65543

enum sdioemb_bt_a_service_id {
    SDIO_BT_A_SID_CMD    = 0x01,
    SDIO_BT_A_SID_ACL    = 0x02,
    SDIO_BT_A_SID_SCO    = 0x03,
    SDIO_BT_A_SID_EVT    = 0x04,
    SDIO_BT_A_SID_VENDOR = 0xfe,
};

static __inline int sdioemb_bt_a_packet_len(const char *p)
{
    return (p[0] & 0xff) | ((p[1] & 0xff) << 8) | ((p[2] & 0xff) << 16);
}

static __inline int sdioemb_bt_a_service_id(const char *p)
{
    return p[3];
}

/*
 * Minimum amount to read (including the Type-A header). This allows
 * short packets (e.g., flow control packets) to be read with a single
 * command.
 */
#define SDIO_BT_A_MIN_READ 32

#define SDIO_BT_A_NAME_LEN 16

struct sdioemb_bt_a_dev {
    CsrSdioFunction *func;
    char name[SDIO_BT_A_NAME_LEN];
    void *drv_data;

    /**
     * Get a buffer to receive a packet into.
     *
     * @param bt the BT device.
     * @param header a buffer of length #SDIO_BT_A_MIN_READ containing
     *      (part of) the packet the buffer is for.  It will contain
     *      the Type-A header and as much of the payload that will
     *      fit.
     * @param buffer_min_len the minimum length of buffer required to
     *     receive the whole packet.  This includes space for padding
     *     the read to a whole number of blocks (if more than 512
     *     octets is still to be read).
     * @param buffer returns the buffer. The packet (including the
     *     Type-A header will be placed at the beginning of this
     *     buffer.
     * @param buffer_handle returns a buffer handle passed to the
     *     subsequent call of the receive() callback.
     *
     * @return 0 if a buffer was provided.
     * @return -ENOMEM if no buffer could be provided.
     */
    int (*get_rx_buffer)(struct sdioemb_bt_a_dev *bt, const uint8_t *header,
                         size_t buffer_min_len, uint8_t **buffer, void **buffer_handle);
    void (*receive)(struct sdioemb_bt_a_dev *bt, void *buffer_handle, int status);
    void (*sleep_state_changed)(struct sdioemb_bt_a_dev *bt);

    enum sdio_sleep_state sleep_state;

    uint8_t  max_tx_retries;
    uint8_t  max_rx_retries;
    unsigned needs_read_ack:1;
    unsigned wait_for_firmware:1;

    unsigned rx_off:1;

    /**
     * A buffer to read the packet header into before the real buffer
     * is requested with the get_rx_buffer() callback.
     *
     * @internal
     */
    uint8_t *header;
};

int  sdioemb_bt_a_setup(struct sdioemb_bt_a_dev *bt, CsrSdioFunction *func);
void sdioemb_bt_a_cleanup(struct sdioemb_bt_a_dev *bt);
int  sdioemb_bt_a_send(struct sdioemb_bt_a_dev *bt, const uint8_t *packet, size_t len);
void sdioemb_bt_a_handle_interrupt(struct sdioemb_bt_a_dev *bt);
void sdioemb_bt_a_set_sleep_state(struct sdioemb_bt_a_dev *bt, enum sdio_sleep_state state);
int  sdioemb_bt_a_check_for_reset(struct sdioemb_bt_a_dev *bt);
void sdioemb_bt_a_start(struct sdioemb_bt_a_dev *bt);
void sdioemb_bt_a_stop(struct sdioemb_bt_a_dev *bt);
void sdioemb_bt_a_rx_on(struct sdioemb_bt_a_dev *bt);
void sdioemb_bt_a_rx_off(struct sdioemb_bt_a_dev *bt);

#endif /* #ifndef SDIOEMB_SDIO_BT_A_H */
