/**
 * aicwf_sdio.h
 *
 * SDIO function declarations
 *
 * Copyright (C) AICSemi 2018-2020
 */

#ifndef _AICWF_SDMMC_H_
#define _AICWF_SDMMC_H_

#ifdef AICWF_SDIO_SUPPORT
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/semaphore.h>
#include "rwnx_cmds.h"
#define AICWF_SDIO_NAME                 "aicwf_sdio"
#define SDIOWIFI_FUNC_BLOCKSIZE         512

#define SDIO_VENDOR_ID_AIC              0x8800
#define SDIO_DEVICE_ID_AIC              0x0001
#define SDIOWIFI_BYTEMODE_LEN_REG       0x02
#define SDIOWIFI_INTR_CONFIG_REG	    0x04
#define SDIOWIFI_SLEEP_REG	            0x05
#define SDIOWIFI_WAKEUP_REG             0x09
#define SDIOWIFI_FLOW_CTRL_REG          0x0A
#define SDIOWIFI_REGISTER_BLOCK         0x0B
#define SDIOWIFI_BYTEMODE_ENABLE_REG    0x11
#define SDIOWIFI_BLOCK_CNT_REG          0x12
#define SDIOWIFI_FLOWCTRL_MASK_REG      0x7F

#define SDIOWIFI_PWR_CTRL_INTERVAL      30
#define FLOW_CTRL_RETRY_COUNT           50
#define BUFFER_SIZE                     1536
#define TAIL_LEN                        4
#define TXQLEN                          (2048*4)

#define SDIO_SLEEP_ST                    0
#define SDIO_ACTIVE_ST                   1

typedef enum {
    SDIO_TYPE_DATA         = 0X00,
    SDIO_TYPE_CFG          = 0X10,
    SDIO_TYPE_CFG_CMD_RSP  = 0X11,
    SDIO_TYPE_CFG_DATA_CFM = 0X12
} sdio_type;

struct rwnx_hw;


struct aic_sdio_dev {
    struct rwnx_hw *rwnx_hw;
    struct sdio_func *func;
    struct device *dev;
    struct aicwf_bus *bus_if;
    struct rwnx_cmd_mgr cmd_mgr;

    struct aicwf_rx_priv *rx_priv;
    struct aicwf_tx_priv *tx_priv;
    u32 state;

    //for sdio pwr ctrl
    struct timer_list timer;
    uint active_duration;
    struct completion pwrctrl_trgg;
    struct task_struct *pwrctl_tsk;
    spinlock_t pwrctl_lock;
    struct semaphore pwrctl_wakeup_sema;
};
int aicwf_sdio_writeb(struct aic_sdio_dev *sdiodev, uint regaddr, u8 val);
void aicwf_sdio_hal_irqhandler(struct sdio_func *func);
void aicwf_sdio_pwrctl_timer(struct aic_sdio_dev *sdiodev, uint duration);
int aicwf_sdio_pwr_stctl(struct  aic_sdio_dev *sdiodev, uint target);
int aicwf_sdio_func_init(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_func_deinit(struct aic_sdio_dev *sdiodev);
int aicwf_sdio_flow_ctrl(struct aic_sdio_dev *sdiodev);
int aicwf_sdio_recv_pkt(struct aic_sdio_dev *sdiodev, struct sk_buff *skbbuf, u32 size);
int aicwf_sdio_send_pkt(struct aic_sdio_dev *sdiodev, u8 *buf, uint count);
void *aicwf_sdio_bus_init(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_release(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_exit(void);
void aicwf_sdio_register(void);
int aicwf_sdio_txpkt(struct aic_sdio_dev *sdiodev, struct sk_buff *pkt);
int sdio_bustx_thread(void *data);
int sdio_busrx_thread(void *data);
int aicwf_sdio_aggr(struct aicwf_tx_priv *tx_priv, struct sk_buff *pkt);
int aicwf_sdio_send(struct aicwf_tx_priv *tx_priv);
void aicwf_sdio_aggr_send(struct aicwf_tx_priv *tx_priv);
void aicwf_sdio_aggrbuf_reset(struct aicwf_tx_priv* tx_priv);
extern void aicwf_hostif_ready(void);
#ifdef CONFIG_PLATFORM_NANOPI
extern void extern_wifi_set_enable(int is_on);
extern void sdio_reinit(void);
#endif /*CONFIG_PLATFORM_NANOPI*/

#endif /* AICWF_SDIO_SUPPORT */

#endif /*_AICWF_SDMMC_H_*/
