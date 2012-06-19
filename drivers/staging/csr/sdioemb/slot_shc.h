/*
 * Standard Host Controller definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SLOT_SHC_H
#define _SLOT_SHC_H

#include <oska/io.h>

/* SHC registers */
#define SHC_SYSTEM_ADDRESS  0x00

#define SHC_BLOCK_SIZE      0x04
#  define SHC_BLOCK_SIZE_DMA_BOUNDARY_4K   (0x0 << 12)
#  define SHC_BLOCK_SIZE_DMA_BOUNDARY_512K (0x7 << 12)

#define SHC_BLOCK_COUNT     0x06
#define SHC_ARG             0x08

#define SHC_TRANSFER_MODE   0x0c
#  define SHC_TRANSFER_MODE_DMA_EN        0x0001
#  define SHC_TRANSFER_MODE_BLK_CNT_EN    0x0002
#  define SHC_TRANSFER_MODE_AUTO_CMD12_EN 0x0004
#  define SHC_TRANSFER_MODE_DATA_READ     0x0010
#  define SHC_TRANSFER_MODE_MULTI_BLK     0x0020

#define SHC_CMD             0x0e
#  define SHC_CMD_RESP_NONE     0x0000
#  define SHC_CMD_RESP_136      0x0001
#  define SHC_CMD_RESP_48       0x0002
#  define SHC_CMD_RESP_48B      0x0003
#  define SHC_CMD_RESP_CRC_CHK  0x0008
#  define SHC_CMD_RESP_IDX_CHK  0x0010
#  define SHC_CMD_DATA_PRESENT  0x0020
#  define SHC_CMD_TYPE_ABORT    (0x3 << 6)
#  define SHC_CMD_IDX(c)        ((c) << 8)

#define SHC_RESPONSE_0_31   0x10

#define SHC_BUFFER_DATA_PORT    0x20

#define SHC_PRESENT_STATE       0x24
#  define SHC_PRESENT_STATE_CMD_INHIBIT     0x00000001
#  define SHC_PRESENT_STATE_DAT_INHIBIT     0x00000002
#  define SHC_PRESENT_STATE_CARD_PRESENT    0x00010000

#define SHC_HOST_CTRL           0x28
#  define SHC_HOST_CTRL_LED_ON          0x01
#  define SHC_HOST_CTRL_4BIT            0x02
#  define SHC_HOST_CTRL_HIGH_SPD_EN     0x04


#define SHC_PWR_CTRL        0x29
#  define SHC_PWR_CTRL_3V3      0x0e
#  define SHC_PWR_CTRL_ON       0x01

#define SHC_BLOCK_GAP_CTRL  0x2a
#define SHC_WAKEUP_CTRL     0x2b

#define SHC_CLOCK_CTRL      0x2c
#  define SHC_CLOCK_CTRL_INT_CLK_EN     0x01
#  define SHC_CLOCK_CTRL_INT_CLK_STABLE 0x02
#  define SHC_CLOCK_CTRL_SD_CLK_EN      0x04
#  define SHC_CLOCK_CTRL_DIV(d)         (((d) >> 1) << 8) /* divisor must be power of 2 */

#define SHC_TIMEOUT_CTRL    0x2e
#  define SHC_TIMEOUT_CTRL_MAX          0x0e

#define SHC_SOFTWARE_RST    0x2f
#  define SHC_SOFTWARE_RST_ALL 0x01
#  define SHC_SOFTWARE_RST_CMD 0x02
#  define SHC_SOFTWARE_RST_DAT 0x04

#define SHC_INT_STATUS      0x30
#define SHC_INT_STATUS_EN   0x34
#define SHC_INT_SIGNAL_EN   0x38
#  define SHC_INT_CMD_COMPLETE      0x00000001
#  define SHC_INT_TRANSFER_COMPLETE 0x00000002
#  define SHC_INT_BLOCK_GAP         0x00000004
#  define SHC_INT_DMA               0x00000008
#  define SHC_INT_WR_BUF_RDY        0x00000010
#  define SHC_INT_RD_BUF_RDY        0x00000020
#  define SHC_INT_CARD_INSERTED     0x00000040
#  define SHC_INT_CARD_REMOVED      0x00000080
#  define SHC_INT_CARD_INT          0x00000100
#  define SHC_INT_ERR_ANY           0x00008000
#  define SHC_INT_ERR_CMD_TIMEOUT   0x00010000
#  define SHC_INT_ERR_CMD_CRC       0x00020000
#  define SHC_INT_ERR_CMD_ENDBIT    0x00040000
#  define SHC_INT_ERR_CMD_INDEX     0x00080000
#  define SHC_INT_ERR_CMD_ALL       0x000f0000
#  define SHC_INT_ERR_DAT_TIMEOUT   0x00100000
#  define SHC_INT_ERR_DAT_CRC       0x00200000
#  define SHC_INT_ERR_DAT_ENDBIT    0x00400000
#  define SHC_INT_ERR_DAT_ALL       0x00700000
#  define SHC_INT_ERR_CURRENT_LIMIT 0x00800000
#  define SHC_INT_ERR_AUTO_CMD12    0x01000000
#  define SHC_INT_ERR_ALL           0x01ff0000
#  define SHC_INT_ALL               0x01ff81ff

#define SHC_AUTO_CMD12_STATUS   0x3c

#define SHC_CAPS                0x40
#  define SHC_CAPS_TO_BASE_CLK_FREQ(c)  (((c) & 0x00003f00) >> 8)
#  define SHC_CAPS_PWR_3V3              (1 << 24)

#define SHC_MAX_CURRENT_CAPS    0x4c

/* PCI configuration registers. */
#define PCI_SHC_SLOT_INFO 0x40

/* Maximum time to wait for a software reset. */
#define SHC_RESET_TIMEOUT_MS 100 /* ms */

/* Maximum time to wait for internal clock to stabilize */
#define SHC_INT_CLK_STABLE_TIMEOUT_MS 100

/*
 * No supported voltages in the capabilities register.
 *
 * Workaround: Assume 3.3V is supported.
 */
#define SLOT_SHC_QUIRK_NO_VOLTAGE_CAPS (1 << 0)

/*
 * Commands with an R5B (busy) response do not complete.
 *
 * Workaround: Use R5 instead. This will only work if the busy signal
 * is cleared sufficiently quickly before the next command is started.
 */
#define SLOT_SHC_QUIRK_R5B_BROKEN      (1 << 1)

/*
 * High speed mode doesn't work.
 *
 * Workaround: limit maximum bus frequency to 25 MHz.
 */
#define SLOT_SHC_QUIRK_HIGH_SPD_BROKEN (1 << 2)

/*
 * Data timeout (TIMEOUT_CTRL) uses SDCLK and not TMCLK.
 *
 * Workaround: set TIMEOUT_CTRL using SDCLK.
 */
#define SLOT_SHC_QUIRK_DATA_TIMEOUT_USES_SDCLK (1 << 3)

/*
 * Controller can only start DMA on dword (32 bit) aligned addresses.
 *
 * Workaround: PIO is used on data transfers with a non-dword aligned
 * address.
 */
#define SHC_QUIRK_DMA_NEEDS_DWORD_ALIGNED_ADDR (1 << 4)

/*
 * Controller is unreliable following multiple transfers
 *
 * Workaround: The controller is reset following every command, not just
 * erroneous ones
 */
#define SHC_QUIRK_RESET_EVERY_CMD_COMPLETE (1 << 5)

/*
 * JMicron JMB381 to JMB385 controllers require some non-standard PCI
 * config space writes.
 */
#define SHC_QUIRK_JMICRON_JMB38X (1 << 6)

/*
 * Controller can only do DMA if the length is a whole number of
 * dwords.
 *
 * Controller with this quirk probably also need
 * SHC_QUIRK_DMA_NEEDS_DWORD_ALIGNED_ADDR.
 *
 * Workaround: PIO is used on data transfers that don't end on an
 * aligned address.
 */
#define SHC_QUIRK_DMA_NEEDS_DWORD_ALIGNED_LEN (1 << 7)

struct sdioemb_shc {
    struct sdioemb_slot *slot;
    void (*enable_int)(struct sdioemb_slot *slot, uint32_t ints);
    void (*disable_int)(struct sdioemb_slot *slot, uint32_t ints);
    void (*cmd_complete)(struct sdioemb_slot *slot, struct sdioemb_cmd *cmd);
    uint32_t quirks;
    os_io_mem_t addr;

    os_spinlock_t lock;
    os_timer_t lockup_timer;
    uint32_t base_clk;
    struct sdioemb_cmd *current_cmd;
    uint8_t *data;
    size_t remaining;
    size_t block_size;
};

void sdioemb_shc_init(struct sdioemb_shc *shc);
void sdioemb_shc_clean_up(struct sdioemb_shc *shc);

int sdioemb_shc_start(struct sdioemb_shc *shc);
void sdioemb_shc_stop(struct sdioemb_shc *shc);

bool sdioemb_shc_isr(struct sdioemb_shc *shc, uint32_t *int_stat);
void sdioemb_shc_dsr(struct sdioemb_shc *shc, uint32_t int_stat);

int sdioemb_shc_set_bus_freq(struct sdioemb_shc *shc, int clk);
int sdioemb_shc_set_bus_width(struct sdioemb_shc *shc, int bus_width);
int sdioemb_shc_start_cmd(struct sdioemb_shc *shc, struct sdioemb_cmd *cmd,
                          bool use_dma, uint64_t dma_addr);
int sdioemb_shc_card_present(struct sdioemb_shc *shc);
int sdioemb_shc_card_power(struct sdioemb_shc *shc, enum sdioemb_power power);
void sdioemb_shc_enable_card_int(struct sdioemb_shc *shc);
void sdioemb_shc_disable_card_int(struct sdioemb_shc *shc);
int sdioemb_shc_hard_reset(struct sdioemb_shc *shc);

void sdioemb_shc_show_quirks(struct sdioemb_shc *shc);

#endif /* #ifndef _SLOT_SHC_H */
