#ifndef _RDA5890_IF_SDIO_H
#define _RDA5890_IF_SDIO_H

#define IF_SDIO_SDIO2AHB_PKTLEN_L		0x00
#define IF_SDIO_SDIO2AHB_PKTLEN_H		0x01

#define IF_SDIO_AHB2SDIO_PKTLEN_L		0x02
#define IF_SDIO_AHB2SDIO_PKTLEN_H		0x03

#define IF_SDIO_FUN1_INT_MASK	0x04
#define IF_SDIO_FUN1_INT_PEND	0x05
#define IF_SDIO_FUN1_INT_STAT	0x06

#define   IF_SDIO_INT_AHB2SDIO	0x01
#define   IF_SDIO_INT_ERROR		0x04
#define   IF_SDIO_INT_SLEEP		0x10
#define   IF_SDIO_INT_AWAKE		0x20
#define   IF_SDIO_INT_RXCMPL	0x40
#define   IF_SDIO_HOST_TX_FLAG	0x80

#define IF_SDIO_FUN1_FIFO_WR	0x07
#define IF_SDIO_FUN1_FIFO_RD	0x08

#define IF_SDIO_FUN1_INT_TO_DEV	0x09

struct if_sdio_card {
        struct sdio_func        *func;
        struct rda5890_private  *priv;

        u8                      buffer[2048];

        spinlock_t              lock;
        struct if_sdio_packet   *packets;
        struct work_struct      packet_worker;
        
        struct workqueue_struct *work_thread;
        atomic_t wid_complete_flag;
#ifdef WIFI_POWER_MANAGER
        atomic_t              sleep_work_is_active;
        struct delayed_work   sleep_work;
#endif
};

void export_msdc_clk_always_on(void);
void export_msdc_clk_always_on_off(void);
void export_wifi_eirq_enable(void);
void export_wifi_eirq_disable(void);

#endif
