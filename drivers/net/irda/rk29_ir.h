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
extern void irda_hw_init(struct rk29_irda *si);
extern int irda_hw_get_mode(void);
extern void irda_hw_deinit(struct rk29_irda *si);
extern int irda_hw_startup(struct rk29_irda *si);
extern int irda_hw_shutdown(struct rk29_irda *si);
extern int irda_hw_set_speed(u32 speed);
extern int irda_hw_tx_enable_irq(enum eTrans_Mode mode);
extern int irda_hw_tx_enable(int len);
extern int irda_hw_get_irqsrc(void);
extern int irda_hw_get_data16(char* data8);
extern void irda_hw_set_moderx(void);
extern int irda_hw_get_mode(void);


#endif //__DRIVERS_NET_IRDA_RK29_IR_H
