/*
 * USB Standard Host Controller definitions.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SLOT_USHC_H
#define _SLOT_USHC_H

#include <oska/io.h>

enum ushc_request
{
    USHC_GET_CAPS  = 0x00,
    USHC_HOST_CTRL = 0x01,
    USHC_PWR_CTRL  = 0x02,
    USHC_CLK_FREQ  = 0x03,
    USHC_EXEC_CMD  = 0x04,
    USHC_READ_RESP = 0x05,
    USHC_RESET     = 0x06
};

enum ushc_request_recipient
{
    USHC_RECIPIENT_DEVICE = 0x00,
    USHC_RECIPIENT_INTERFACE = 0x01,
    USHC_RECIPIENT_ENDPOINT = 0x02,
    USHC_RECIPIENT_OTHER = 0x03
};

enum ushc_request_direction
{
    USHC_HOST_TO_DEVICE = 0x00,
    USHC_DEVICE_TO_HOST = 0x01
};

struct sdioemb_ushc
{
    struct sdioemb_slot *slot;

    void (*enable_int)(struct sdioemb_slot *slot, uint32_t ints);
    void (*disable_int)(struct sdioemb_slot *slot, uint32_t ints);
    void (*cmd_complete)(struct sdioemb_slot *slot, struct sdioemb_cmd *cmd);
    int  (*set_host_ctrl)(struct sdioemb_slot *slot, uint16_t controler_state);
    int  (*submit_vendor_request)(struct sdioemb_slot *slot,
                                  enum ushc_request request,
                                  enum ushc_request_direction direction,
                                  enum ushc_request_recipient recipient,
                                  uint16_t value,
                                  uint16_t index,
                                  void* io_buffer,
                                  uint32_t io_buffer_length);
    int  (*submit_cbw_request)(struct sdioemb_slot *slot, uint8_t cmd_index, uint16_t block_size, uint32_t cmd_arg);
    int  (*submit_data_request)(struct sdioemb_slot *slot,
                                enum ushc_request_direction direction,
                                void* request_buffer,
                                uint32_t request_buffer_length);
    int  (*submit_csw_request)(struct sdioemb_slot *slot);

    os_spinlock_t lock;

    uint32_t base_clock;
    uint32_t controler_capabilities;
    uint16_t controler_state;
    struct sdioemb_cmd* current_cmd;

#define DISCONNECTED    0
#define INT_EN          1
#define IGNORE_NEXT_INT 2
#define STOP            4
    uint32_t flags;

#define USHC_INT_STATUS_SDIO_INT     (1 << 1)
#define USHC_INT_STATUS_CARD_PRESENT (1 << 0)
    uint8_t interrupt_status;

    size_t block_size;
};

#define USHC_GET_CAPS_VERSION_MASK 0xff
#define USHC_GET_CAPS_3V3      (1 << 8)
#define USHC_GET_CAPS_3V0      (1 << 9)
#define USHC_GET_CAPS_1V8      (1 << 10)
#define USHC_GET_CAPS_HIGH_SPD (1 << 16)

#define USHC_PWR_CTRL_OFF 0x00
#define USHC_PWR_CTRL_3V3 0x01
#define USHC_PWR_CTRL_3V0 0x02
#define USHC_PWR_CTRL_1V8 0x03

#define USHC_HOST_CTRL_4BIT     (1 << 1)
#define USHC_HOST_CTRL_HIGH_SPD (1 << 0)

#define USHC_READ_RESP_BUSY        (1 << 4)
#define USHC_READ_RESP_ERR_TIMEOUT (1 << 3)
#define USHC_READ_RESP_ERR_CRC     (1 << 2)
#define USHC_READ_RESP_ERR_DAT     (1 << 1)
#define USHC_READ_RESP_ERR_CMD     (1 << 0)
#define USHC_READ_RESP_ERR_MASK    0x0f

void sdioemb_ushc_init(struct sdioemb_ushc* ushc);
void sdioemb_ushc_clean_up(struct sdioemb_ushc* ushc);

int sdioemb_ushc_start(struct sdioemb_ushc* ushc);
void sdioemb_ushc_stop(struct sdioemb_ushc* ushc);

bool sdioemb_ushc_isr(struct sdioemb_ushc* ushc, uint8_t int_stat);

int sdioemb_ushc_set_bus_freq(struct sdioemb_ushc* ushc, int clk);
int sdioemb_ushc_set_bus_width(struct sdioemb_ushc* ushc, int bus_width);
int sdioemb_ushc_start_cmd(struct sdioemb_ushc* ushc, struct sdioemb_cmd *cmd);
int sdioemb_ushc_card_present(struct sdioemb_ushc* ushc);
int sdioemb_ushc_card_power(struct sdioemb_ushc* ushc, enum sdioemb_power power);
void sdioemb_ushc_enable_card_int(struct sdioemb_ushc* ushc);
void sdioemb_ushc_disable_card_int(struct sdioemb_ushc* ushc);
int sdioemb_ushc_hard_reset(struct sdioemb_ushc* ushc);

void sdioemb_ushc_command_complete(struct sdioemb_ushc* ushc, uint8_t status, uint32_t respones);

int ushc_hw_get_caps(struct sdioemb_ushc* ushc);
static int ushc_hw_set_host_ctrl(struct sdioemb_ushc* ushc, uint16_t mask, uint16_t val);
static int ushc_hw_submit_vendor_request(struct sdioemb_ushc* ushc,
                                       enum ushc_request request,
                                       enum ushc_request_recipient recipient,
                                       enum ushc_request_direction direction,
                                       uint16_t value,
                                       uint16_t index,
                                       void* io_buffer,
                                       uint32_t io_buffer_length);

#endif
