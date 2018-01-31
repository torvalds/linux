/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRIVERS_NET_IRDA_RK29_IR_H
#define __DRIVERS_NET_IRDA_RK29_IR_H

#include "bu92725guw.h"
#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>

struct rk29_irda {
    unsigned char*		irda_base_addr;
	unsigned char		power;
	unsigned char		open;

	int			speed;
	int			newspeed;

	struct sk_buff		*txskb;
	struct sk_buff		*rxskb;

    unsigned char		*dma_rx_buff;
	unsigned char		*dma_tx_buff;
	u32		            dma_rx_buff_phy;
	u32		            dma_tx_buff_phy;
	unsigned int		dma_tx_buff_len;
	int			txdma;
	int			rxdma;


	struct device		*dev;
	struct irda_info *pdata;
	struct irlap_cb		*irlap;
	struct qos_info		qos;

	iobuff_t		tx_buff;
	iobuff_t		rx_buff;
};

#endif //__DRIVERS_NET_IRDA_RK29_IR_H
