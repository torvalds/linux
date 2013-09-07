/*
 * Blackfin Infra-red Driver
 *
 * Copyright 2006-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 *
 */
#include "bfin_sir.h"

#ifdef CONFIG_SIR_BFIN_DMA
#define DMA_SIR_RX_XCNT        10
#define DMA_SIR_RX_YCNT        (PAGE_SIZE / DMA_SIR_RX_XCNT)
#define DMA_SIR_RX_FLUSH_JIFS  (HZ * 4 / 250)
#endif

#if ANOMALY_05000447
static int max_rate = 57600;
#else
static int max_rate = 115200;
#endif

static void turnaround_delay(unsigned long last_jif, int mtt)
{
	long ticks;

	mtt = mtt < 10000 ? 10000 : mtt;
	ticks = 1 + mtt / (USEC_PER_SEC / HZ);
	schedule_timeout_uninterruptible(ticks);
}

static void bfin_sir_init_ports(struct bfin_sir_port *sp, struct platform_device *pdev)
{
	int i;
	struct resource *res;

	for (i = 0; i < pdev->num_resources; i++) {
		res = &pdev->resource[i];
		switch (res->flags) {
		case IORESOURCE_MEM:
			sp->membase   = (void __iomem *)res->start;
			break;
		case IORESOURCE_IRQ:
			sp->irq = res->start;
			break;
		case IORESOURCE_DMA:
			sp->rx_dma_channel = res->start;
			sp->tx_dma_channel = res->end;
			break;
		default:
			break;
		}
	}

	sp->clk = get_sclk();
#ifdef CONFIG_SIR_BFIN_DMA
	sp->tx_done        = 1;
	init_timer(&(sp->rx_dma_timer));
#endif
}

static void bfin_sir_stop_tx(struct bfin_sir_port *port)
{
#ifdef CONFIG_SIR_BFIN_DMA
	disable_dma(port->tx_dma_channel);
#endif

	while (!(UART_GET_LSR(port) & THRE)) {
		cpu_relax();
		continue;
	}

	UART_CLEAR_IER(port, ETBEI);
}

static void bfin_sir_enable_tx(struct bfin_sir_port *port)
{
	UART_SET_IER(port, ETBEI);
}

static void bfin_sir_stop_rx(struct bfin_sir_port *port)
{
	UART_CLEAR_IER(port, ERBFI);
}

static void bfin_sir_enable_rx(struct bfin_sir_port *port)
{
	UART_SET_IER(port, ERBFI);
}

static int bfin_sir_set_speed(struct bfin_sir_port *port, int speed)
{
	int ret = -EINVAL;
	unsigned int quot;
	unsigned short val, lsr, lcr;
	static int utime;
	int count = 10;

	lcr = WLS(8);

	switch (speed) {
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:

		/*
		 * IRDA is not affected by anomaly 05000230, so there is no
		 * need to tweak the divisor like he UART driver (which will
		 * slightly speed up the baud rate on us).
		 */
		quot = (port->clk + (8 * speed)) / (16 * speed);

		do {
			udelay(utime);
			lsr = UART_GET_LSR(port);
		} while (!(lsr & TEMT) && count--);

		/* The useconds for 1 bits to transmit */
		utime = 1000000 / speed + 1;

		/* Clear UCEN bit to reset the UART state machine
		 * and control registers
		 */
		val = UART_GET_GCTL(port);
		val &= ~UCEN;
		UART_PUT_GCTL(port, val);

		/* Set DLAB in LCR to Access THR RBR IER */
		UART_SET_DLAB(port);
		SSYNC();

		UART_PUT_DLL(port, quot & 0xFF);
		UART_PUT_DLH(port, (quot >> 8) & 0xFF);
		SSYNC();

		/* Clear DLAB in LCR */
		UART_CLEAR_DLAB(port);
		SSYNC();

		UART_PUT_LCR(port, lcr);

		val = UART_GET_GCTL(port);
		val |= UCEN;
		UART_PUT_GCTL(port, val);

		ret = 0;
		break;
	default:
		printk(KERN_WARNING "bfin_sir: Invalid speed %d\n", speed);
		break;
	}

	val = UART_GET_GCTL(port);
	/* If not add the 'RPOLC', we can't catch the receive interrupt.
	 * It's related with the HW layout and the IR transiver.
	 */
	val |= UMOD_IRDA | RPOLC;
	UART_PUT_GCTL(port, val);
	return ret;
}

static int bfin_sir_is_receiving(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;

	if (!(UART_GET_IER(port) & ERBFI))
		return 0;
	return self->rx_buff.state != OUTSIDE_FRAME;
}

#ifdef CONFIG_SIR_BFIN_PIO
static void bfin_sir_tx_chars(struct net_device *dev)
{
	unsigned int chr;
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;

	if (self->tx_buff.len != 0) {
		chr = *(self->tx_buff.data);
		UART_PUT_CHAR(port, chr);
		self->tx_buff.data++;
		self->tx_buff.len--;
	} else {
		self->stats.tx_packets++;
		self->stats.tx_bytes += self->tx_buff.data - self->tx_buff.head;
		if (self->newspeed) {
			bfin_sir_set_speed(port, self->newspeed);
			self->speed = self->newspeed;
			self->newspeed = 0;
		}
		bfin_sir_stop_tx(port);
		bfin_sir_enable_rx(port);
		/* I'm hungry! */
		netif_wake_queue(dev);
	}
}

static void bfin_sir_rx_chars(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;
	unsigned char ch;

	UART_CLEAR_LSR(port);
	ch = UART_GET_CHAR(port);
	async_unwrap_char(dev, &self->stats, &self->rx_buff, ch);
	dev->last_rx = jiffies;
}

static irqreturn_t bfin_sir_rx_int(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;

	spin_lock(&self->lock);
	while ((UART_GET_LSR(port) & DR))
		bfin_sir_rx_chars(dev);
	spin_unlock(&self->lock);

	return IRQ_HANDLED;
}

static irqreturn_t bfin_sir_tx_int(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;

	spin_lock(&self->lock);
	if (UART_GET_LSR(port) & THRE)
		bfin_sir_tx_chars(dev);
	spin_unlock(&self->lock);

	return IRQ_HANDLED;
}
#endif /* CONFIG_SIR_BFIN_PIO */

#ifdef CONFIG_SIR_BFIN_DMA
static void bfin_sir_dma_tx_chars(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;

	if (!port->tx_done)
		return;
	port->tx_done = 0;

	if (self->tx_buff.len == 0) {
		self->stats.tx_packets++;
		if (self->newspeed) {
			bfin_sir_set_speed(port, self->newspeed);
			self->speed = self->newspeed;
			self->newspeed = 0;
		}
		bfin_sir_enable_rx(port);
		port->tx_done = 1;
		netif_wake_queue(dev);
		return;
	}

	blackfin_dcache_flush_range((unsigned long)(self->tx_buff.data),
		(unsigned long)(self->tx_buff.data+self->tx_buff.len));
	set_dma_config(port->tx_dma_channel,
		set_bfin_dma_config(DIR_READ, DMA_FLOW_STOP,
			INTR_ON_BUF, DIMENSION_LINEAR, DATA_SIZE_8,
			DMA_SYNC_RESTART));
	set_dma_start_addr(port->tx_dma_channel,
		(unsigned long)(self->tx_buff.data));
	set_dma_x_count(port->tx_dma_channel, self->tx_buff.len);
	set_dma_x_modify(port->tx_dma_channel, 1);
	enable_dma(port->tx_dma_channel);
}

static irqreturn_t bfin_sir_dma_tx_int(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;

	spin_lock(&self->lock);
	if (!(get_dma_curr_irqstat(port->tx_dma_channel) & DMA_RUN)) {
		clear_dma_irqstat(port->tx_dma_channel);
		bfin_sir_stop_tx(port);

		self->stats.tx_packets++;
		self->stats.tx_bytes += self->tx_buff.len;
		self->tx_buff.len = 0;
		if (self->newspeed) {
			bfin_sir_set_speed(port, self->newspeed);
			self->speed = self->newspeed;
			self->newspeed = 0;
		}
		bfin_sir_enable_rx(port);
		/* I'm hungry! */
		netif_wake_queue(dev);
		port->tx_done = 1;
	}
	spin_unlock(&self->lock);

	return IRQ_HANDLED;
}

static void bfin_sir_dma_rx_chars(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;
	int i;

	UART_CLEAR_LSR(port);

	for (i = port->rx_dma_buf.head; i < port->rx_dma_buf.tail; i++)
		async_unwrap_char(dev, &self->stats, &self->rx_buff, port->rx_dma_buf.buf[i]);
}

void bfin_sir_rx_dma_timeout(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;
	int x_pos, pos;
	unsigned long flags;

	spin_lock_irqsave(&self->lock, flags);
	x_pos = DMA_SIR_RX_XCNT - get_dma_curr_xcount(port->rx_dma_channel);
	if (x_pos == DMA_SIR_RX_XCNT)
		x_pos = 0;

	pos = port->rx_dma_nrows * DMA_SIR_RX_XCNT + x_pos;

	if (pos > port->rx_dma_buf.tail) {
		port->rx_dma_buf.tail = pos;
		bfin_sir_dma_rx_chars(dev);
		port->rx_dma_buf.head = port->rx_dma_buf.tail;
	}
	spin_unlock_irqrestore(&self->lock, flags);
}

static irqreturn_t bfin_sir_dma_rx_int(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;
	unsigned short irqstat;

	spin_lock(&self->lock);

	port->rx_dma_nrows++;
	port->rx_dma_buf.tail = DMA_SIR_RX_XCNT * port->rx_dma_nrows;
	bfin_sir_dma_rx_chars(dev);
	if (port->rx_dma_nrows >= DMA_SIR_RX_YCNT) {
		port->rx_dma_nrows = 0;
		port->rx_dma_buf.tail = 0;
	}
	port->rx_dma_buf.head = port->rx_dma_buf.tail;

	irqstat = get_dma_curr_irqstat(port->rx_dma_channel);
	clear_dma_irqstat(port->rx_dma_channel);
	spin_unlock(&self->lock);

	mod_timer(&port->rx_dma_timer, jiffies + DMA_SIR_RX_FLUSH_JIFS);
	return IRQ_HANDLED;
}
#endif /* CONFIG_SIR_BFIN_DMA */

static int bfin_sir_startup(struct bfin_sir_port *port, struct net_device *dev)
{
#ifdef CONFIG_SIR_BFIN_DMA
	dma_addr_t dma_handle;
#endif /* CONFIG_SIR_BFIN_DMA */

	if (request_dma(port->rx_dma_channel, "BFIN_UART_RX") < 0) {
		dev_warn(&dev->dev, "Unable to attach SIR RX DMA channel\n");
		return -EBUSY;
	}

	if (request_dma(port->tx_dma_channel, "BFIN_UART_TX") < 0) {
		dev_warn(&dev->dev, "Unable to attach SIR TX DMA channel\n");
		free_dma(port->rx_dma_channel);
		return -EBUSY;
	}

#ifdef CONFIG_SIR_BFIN_DMA

	set_dma_callback(port->rx_dma_channel, bfin_sir_dma_rx_int, dev);
	set_dma_callback(port->tx_dma_channel, bfin_sir_dma_tx_int, dev);

	port->rx_dma_buf.buf = dma_alloc_coherent(NULL, PAGE_SIZE,
						  &dma_handle, GFP_DMA);
	port->rx_dma_buf.head = 0;
	port->rx_dma_buf.tail = 0;
	port->rx_dma_nrows = 0;

	set_dma_config(port->rx_dma_channel,
				set_bfin_dma_config(DIR_WRITE, DMA_FLOW_AUTO,
									INTR_ON_ROW, DIMENSION_2D,
									DATA_SIZE_8, DMA_SYNC_RESTART));
	set_dma_x_count(port->rx_dma_channel, DMA_SIR_RX_XCNT);
	set_dma_x_modify(port->rx_dma_channel, 1);
	set_dma_y_count(port->rx_dma_channel, DMA_SIR_RX_YCNT);
	set_dma_y_modify(port->rx_dma_channel, 1);
	set_dma_start_addr(port->rx_dma_channel, (unsigned long)port->rx_dma_buf.buf);
	enable_dma(port->rx_dma_channel);

	port->rx_dma_timer.data = (unsigned long)(dev);
	port->rx_dma_timer.function = (void *)bfin_sir_rx_dma_timeout;

#else

	if (request_irq(port->irq, bfin_sir_rx_int, IRQF_DISABLED, "BFIN_SIR_RX", dev)) {
		dev_warn(&dev->dev, "Unable to attach SIR RX interrupt\n");
		return -EBUSY;
	}

	if (request_irq(port->irq+1, bfin_sir_tx_int, IRQF_DISABLED, "BFIN_SIR_TX", dev)) {
		dev_warn(&dev->dev, "Unable to attach SIR TX interrupt\n");
		free_irq(port->irq, dev);
		return -EBUSY;
	}
#endif

	return 0;
}

static void bfin_sir_shutdown(struct bfin_sir_port *port, struct net_device *dev)
{
	unsigned short val;

	bfin_sir_stop_rx(port);

	val = UART_GET_GCTL(port);
	val &= ~(UCEN | UMOD_MASK | RPOLC);
	UART_PUT_GCTL(port, val);

#ifdef CONFIG_SIR_BFIN_DMA
	disable_dma(port->tx_dma_channel);
	disable_dma(port->rx_dma_channel);
	del_timer(&(port->rx_dma_timer));
	dma_free_coherent(NULL, PAGE_SIZE, port->rx_dma_buf.buf, 0);
#else
	free_irq(port->irq+1, dev);
	free_irq(port->irq, dev);
#endif
	free_dma(port->tx_dma_channel);
	free_dma(port->rx_dma_channel);
}

#ifdef CONFIG_PM
static int bfin_sir_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bfin_sir_port *sir_port;
	struct net_device *dev;
	struct bfin_sir_self *self;

	sir_port = platform_get_drvdata(pdev);
	if (!sir_port)
		return 0;

	dev = sir_port->dev;
	self = netdev_priv(dev);
	if (self->open) {
		flush_work(&self->work);
		bfin_sir_shutdown(self->sir_port, dev);
		netif_device_detach(dev);
	}

	return 0;
}
static int bfin_sir_resume(struct platform_device *pdev)
{
	struct bfin_sir_port *sir_port;
	struct net_device *dev;
	struct bfin_sir_self *self;
	struct bfin_sir_port *port;

	sir_port = platform_get_drvdata(pdev);
	if (!sir_port)
		return 0;

	dev = sir_port->dev;
	self = netdev_priv(dev);
	port = self->sir_port;
	if (self->open) {
		if (self->newspeed) {
			self->speed = self->newspeed;
			self->newspeed = 0;
		}
		bfin_sir_startup(port, dev);
		bfin_sir_set_speed(port, 9600);
		bfin_sir_enable_rx(port);
		netif_device_attach(dev);
	}
	return 0;
}
#else
#define bfin_sir_suspend   NULL
#define bfin_sir_resume    NULL
#endif

static void bfin_sir_send_work(struct work_struct *work)
{
	struct bfin_sir_self  *self = container_of(work, struct bfin_sir_self, work);
	struct net_device *dev = self->sir_port->dev;
	struct bfin_sir_port *port = self->sir_port;
	unsigned short val;
	int tx_cnt = 10;

	while (bfin_sir_is_receiving(dev) && --tx_cnt)
		turnaround_delay(dev->last_rx, self->mtt);

	bfin_sir_stop_rx(port);

	/* To avoid losting RX interrupt, we reset IR function before
	 * sending data. We also can set the speed, which will
	 * reset all the UART.
	 */
	val = UART_GET_GCTL(port);
	val &= ~(UMOD_MASK | RPOLC);
	UART_PUT_GCTL(port, val);
	SSYNC();
	val |= UMOD_IRDA | RPOLC;
	UART_PUT_GCTL(port, val);
	SSYNC();
	/* bfin_sir_set_speed(port, self->speed); */

#ifdef CONFIG_SIR_BFIN_DMA
	bfin_sir_dma_tx_chars(dev);
#endif
	bfin_sir_enable_tx(port);
	dev->trans_start = jiffies;
}

static int bfin_sir_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	int speed = irda_get_next_speed(skb);

	netif_stop_queue(dev);

	self->mtt = irda_get_mtt(skb);

	if (speed != self->speed && speed != -1)
		self->newspeed = speed;

	self->tx_buff.data = self->tx_buff.head;
	if (skb->len == 0)
		self->tx_buff.len = 0;
	else
		self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, self->tx_buff.truesize);

	schedule_work(&self->work);
	dev_kfree_skb(skb);

	return 0;
}

static int bfin_sir_ioctl(struct net_device *dev, struct ifreq *ifreq, int cmd)
{
	struct if_irda_req *rq = (struct if_irda_req *)ifreq;
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;
	int ret = 0;

	switch (cmd) {
	case SIOCSBANDWIDTH:
		if (capable(CAP_NET_ADMIN)) {
			if (self->open) {
				ret = bfin_sir_set_speed(port, rq->ifr_baudrate);
				bfin_sir_enable_rx(port);
			} else {
				dev_warn(&dev->dev, "SIOCSBANDWIDTH: !netif_running\n");
				ret = 0;
			}
		}
		break;

	case SIOCSMEDIABUSY:
		ret = -EPERM;
		if (capable(CAP_NET_ADMIN)) {
			irda_device_set_media_busy(dev, TRUE);
			ret = 0;
		}
		break;

	case SIOCGRECEIVING:
		rq->ifr_receiving = bfin_sir_is_receiving(dev);
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static struct net_device_stats *bfin_sir_stats(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);

	return &self->stats;
}

static int bfin_sir_open(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);
	struct bfin_sir_port *port = self->sir_port;
	int err;

	self->newspeed = 0;
	self->speed = 9600;

	spin_lock_init(&self->lock);

	err = bfin_sir_startup(port, dev);
	if (err)
		goto err_startup;

	bfin_sir_set_speed(port, 9600);

	self->irlap = irlap_open(dev, &self->qos, DRIVER_NAME);
	if (!self->irlap) {
		err = -ENOMEM;
		goto err_irlap;
	}

	INIT_WORK(&self->work, bfin_sir_send_work);

	/*
	 * Now enable the interrupt then start the queue
	 */
	self->open = 1;
	bfin_sir_enable_rx(port);

	netif_start_queue(dev);

	return 0;

err_irlap:
	self->open = 0;
	bfin_sir_shutdown(port, dev);
err_startup:
	return err;
}

static int bfin_sir_stop(struct net_device *dev)
{
	struct bfin_sir_self *self = netdev_priv(dev);

	flush_work(&self->work);
	bfin_sir_shutdown(self->sir_port, dev);

	if (self->rxskb) {
		dev_kfree_skb(self->rxskb);
		self->rxskb = NULL;
	}

	/* Stop IrLAP */
	if (self->irlap) {
		irlap_close(self->irlap);
		self->irlap = NULL;
	}

	netif_stop_queue(dev);
	self->open = 0;

	return 0;
}

static int bfin_sir_init_iobuf(iobuff_t *io, int size)
{
	io->head = kmalloc(size, GFP_KERNEL);
	if (!io->head)
		return -ENOMEM;
	io->truesize = size;
	io->in_frame = FALSE;
	io->state    = OUTSIDE_FRAME;
	io->data     = io->head;
	return 0;
}

static const struct net_device_ops bfin_sir_ndo = {
	.ndo_open		= bfin_sir_open,
	.ndo_stop		= bfin_sir_stop,
	.ndo_start_xmit		= bfin_sir_hard_xmit,
	.ndo_do_ioctl		= bfin_sir_ioctl,
	.ndo_get_stats		= bfin_sir_stats,
};

static int bfin_sir_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct bfin_sir_self *self;
	unsigned int baudrate_mask;
	struct bfin_sir_port *sir_port;
	int err;

	if (pdev->id >= 0 && pdev->id < ARRAY_SIZE(per) && \
				per[pdev->id][3] == pdev->id) {
		err = peripheral_request_list(per[pdev->id], DRIVER_NAME);
		if (err)
			return err;
	} else {
		dev_err(&pdev->dev, "Invalid pdev id, please check board file\n");
		return -ENODEV;
	}

	err = -ENOMEM;
	sir_port = kmalloc(sizeof(*sir_port), GFP_KERNEL);
	if (!sir_port)
		goto err_mem_0;

	bfin_sir_init_ports(sir_port, pdev);

	dev = alloc_irdadev(sizeof(*self));
	if (!dev)
		goto err_mem_1;

	self = netdev_priv(dev);
	self->dev = &pdev->dev;
	self->sir_port = sir_port;
	sir_port->dev = dev;

	err = bfin_sir_init_iobuf(&self->rx_buff, IRDA_SKB_MAX_MTU);
	if (err)
		goto err_mem_2;
	err = bfin_sir_init_iobuf(&self->tx_buff, IRDA_SIR_MAX_FRAME);
	if (err)
		goto err_mem_3;

	dev->netdev_ops = &bfin_sir_ndo;
	dev->irq = sir_port->irq;

	irda_init_max_qos_capabilies(&self->qos);

	baudrate_mask = IR_9600;

	switch (max_rate) {
	case 115200:
		baudrate_mask |= IR_115200;
	case 57600:
		baudrate_mask |= IR_57600;
	case 38400:
		baudrate_mask |= IR_38400;
	case 19200:
		baudrate_mask |= IR_19200;
	case 9600:
		break;
	default:
		dev_warn(&pdev->dev, "Invalid maximum baud rate, using 9600\n");
	}

	self->qos.baud_rate.bits &= baudrate_mask;

	self->qos.min_turn_time.bits = 1; /* 10 ms or more */

	irda_qos_bits_to_value(&self->qos);

	err = register_netdev(dev);

	if (err) {
		kfree(self->tx_buff.head);
err_mem_3:
		kfree(self->rx_buff.head);
err_mem_2:
		free_netdev(dev);
err_mem_1:
		kfree(sir_port);
err_mem_0:
		peripheral_free_list(per[pdev->id]);
	} else
		platform_set_drvdata(pdev, sir_port);

	return err;
}

static int bfin_sir_remove(struct platform_device *pdev)
{
	struct bfin_sir_port *sir_port;
	struct net_device *dev = NULL;
	struct bfin_sir_self *self;

	sir_port = platform_get_drvdata(pdev);
	if (!sir_port)
		return 0;
	dev = sir_port->dev;
	self = netdev_priv(dev);
	unregister_netdev(dev);
	kfree(self->tx_buff.head);
	kfree(self->rx_buff.head);
	free_netdev(dev);
	kfree(sir_port);

	return 0;
}

static struct platform_driver bfin_ir_driver = {
	.probe   = bfin_sir_probe,
	.remove  = bfin_sir_remove,
	.suspend = bfin_sir_suspend,
	.resume  = bfin_sir_resume,
	.driver  = {
		.name = DRIVER_NAME,
	},
};

module_platform_driver(bfin_ir_driver);

module_param(max_rate, int, 0);
MODULE_PARM_DESC(max_rate, "Maximum baud rate (115200, 57600, 38400, 19200, 9600)");

MODULE_AUTHOR("Graf Yang <graf.yang@analog.com>");
MODULE_DESCRIPTION("Blackfin IrDA driver");
MODULE_LICENSE("GPL");
