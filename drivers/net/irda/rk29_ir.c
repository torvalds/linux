/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/irq.h>
#include <linux/wakelock.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/rk29_iomap.h>
#include <mach/pmu.h>
#include <mach/rk29-dma-pl330.h>

#include "rk29_ir.h"

#if 0
#define RK29IR_DBG(x...) printk(x)
#else
#define RK29IR_DBG(x...)
#endif

#if 0
#define RK29IR_DATA_DBG(x...) printk(x)
#else
#define RK29IR_DATA_DBG(x...)
#endif

#define IRDA_NAME		"rk_irda"

struct irda_driver {
	struct irda_info *pin_info;
	struct device 			*dev;
};

#define IS_FIR(si)		((si)->speed >= 4000000)
static int max_rate = 4000000;
#define IRDA_FRAME_SIZE_LIMIT	BU92725GUW_FIFO_SIZE

#define RK29_MAX_RXLEN 2047

static void rk29_irda_fir_test(struct work_struct *work);
static DECLARE_DELAYED_WORK(dwork, rk29_irda_fir_test);


/*
 * Allocate and map the receive buffer, unless it is already allocated.
 */
static int rk29_irda_rx_alloc(struct rk29_irda *si)
{
	if (si->rxskb)
		return 0;

	si->rxskb = alloc_skb(RK29_MAX_RXLEN + 1, GFP_ATOMIC);

	if (!si->rxskb) {
		printk(KERN_ERR "rk29_ir: out of memory for RX SKB\n");
		return -ENOMEM;
	}

	si->rxskb->len = 0;

	/*
	 * Align any IP headers that may be contained
	 * within the frame.
	 */
	skb_reserve(si->rxskb, 1);

	return 0;
}

/*
 * Set the IrDA communications speed.
 */
static int rk29_irda_set_speed(struct rk29_irda *si, int speed)
{
	unsigned long flags;
	int ret = -EINVAL;
	
    printk("[%s][%d], speed=%d\n",__FUNCTION__,__LINE__,speed);

	switch (speed) {
	case 9600:	case 19200:	case 38400:
	case 57600:	case 115200:
		
		local_irq_save(flags);
		
		irda_hw_set_speed(speed);

		si->speed = speed;

		local_irq_restore(flags);
		ret = 0;
		break;

	case 4000000:
		local_irq_save(flags);

		si->speed = speed;
		
		irda_hw_set_speed(speed);
		//irda_hw_set_speed(1152000);//MIR
		rk29_irda_rx_alloc(si);

		local_irq_restore(flags);
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

static irqreturn_t rk29_irda_irq(int irq, void *dev_id)
{
    struct net_device *dev = (struct net_device *)dev_id;
    struct rk29_irda *si = netdev_priv(dev);
    u8 data[2048]={0,0};
    int tmp_len=0;
    int i=0;
    u32 irq_src = 0;	
	u32 irda_setptn = 0;
	
    irq_src = irda_hw_get_irqsrc();
	
    printk("[%s][%d], 0x%x\n",__FUNCTION__,__LINE__, irq_src);
	
	//disable_irq(dev->irq);
   
        /* EIR 1, 3, 11, 12 
	irda_setptn |= irq_src & (REG_INT_EOFRX | REG_INT_TXE | REG_INT_WRE | REG_INT_RDE\
	                          | REG_INT_CRC | REG_INT_OE | REG_INT_FE | REG_INT_AC\
	                          | REG_INT_DECE | REG_INT_RDOE | REG_INT_DEX) ;*/
	irda_setptn = irq_src;

	/* error */
	if (irq_src & (REG_INT_TO| REG_INT_CRC | REG_INT_OE | REG_INT_FE
		| REG_INT_AC | REG_INT_DECE | REG_INT_RDOE | REG_INT_DEX)) {
        RK29IR_DBG("[%s][%d]: do err\n",__FUNCTION__,__LINE__);
		//BU92725GUW_dump_register();
		BU92725GUW_clr_fifo();
		BU92725GUW_reset();
    }

    if (IS_FIR(si))  //FIR
    {
        RK29IR_DBG("[%s][%d]: FIR\n",__FUNCTION__,__LINE__);
        if(irda_hw_get_mode() == BU92725GUW_AUTO_MULTI_REV) {//rx
			struct sk_buff *skb = si->rxskb;
            RK29IR_DBG("[%s][%d]: rx\n",__FUNCTION__,__LINE__);
            if (irda_setptn & (REG_INT_FE | REG_INT_OE | REG_INT_CRC | REG_INT_DECE)) {
                if (irda_setptn & REG_INT_FE) {
                        printk(KERN_DEBUG "pxa_ir: fir receive frame error\n");
                    dev->stats.rx_frame_errors++;
                } else {
                    printk(KERN_DEBUG "pxa_ir: fir receive abort\n");
                    dev->stats.rx_errors++;
                }
            }
            if (irda_setptn & (FRM_EVT_RX_EOFRX | FRM_EVT_RX_RDE)) {
				tmp_len = BU92725GUW_get_data(skb->data+skb->len);
				skb->len += tmp_len;			
            }
			if (irda_setptn & REG_INT_EOF) {				
				RK29IR_DBG("[%s][%d]: report data:\n",__FUNCTION__,__LINE__);
				si->rxskb = NULL;
				RK29IR_DATA_DBG("[%s][%d]: fir report data:\n",__FUNCTION__,__LINE__);
				for (i=0;i<skb->len;i++) {
					RK29IR_DATA_DBG("0x%2x ", skb->data[i]);
				}
				RK29IR_DATA_DBG("\n");
				
				skb_put(skb, skb->len);
				
				/* Feed it to IrLAP  */
				skb->dev = dev;
				skb_reset_mac_header(skb);
				skb->protocol = htons(ETH_P_IRDA);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += skb->len;
				
				/*
				 * Before we pass the buffer up, allocate a new one.
				 */
				rk29_irda_rx_alloc(si);
				
				netif_rx(skb);
			}						
        }
		else if (irda_hw_get_mode() == BU92725GUW_MULTI_SEND) {//tx
			struct sk_buff *skb = si->txskb;			
			si->txskb = NULL;
            RK29IR_DBG("[%s][%d]: tx\n",__FUNCTION__,__LINE__);
            if (irda_setptn & (FRM_EVT_TX_TXE | FRM_EVT_TX_WRE)) {
				/*
				 * Do we need to change speed?	Note that we're lazy
				 * here - we don't free the old rxskb.	We don't need
				 * to allocate a buffer either.
				 */
				if (si->newspeed) {
					rk29_irda_set_speed(si, si->newspeed);
					si->newspeed = 0;
				}
				
				/*
				 * Account and free the packet.
				 */
				if (skb) {
					dev->stats.tx_packets ++;
					dev->stats.tx_bytes += skb->len;
					dev_kfree_skb_irq(skb);
				}
				
				/*
				 * Make sure that the TX queue is available for sending
				 * (for retries).  TX has priority over RX at all times.
				 */
				netif_wake_queue(dev);
				
				irda_hw_set_moderx();
            }
		}
    }
    else //SIR
    {
		RK29IR_DBG("[%d][%s], sir\n", __LINE__, __FUNCTION__);
        if(irda_hw_get_mode() == BU92725GUW_REV) //rx
        {
			RK29IR_DBG("[%d][%s], receive data:\n", __LINE__, __FUNCTION__);
            if(irda_setptn & (REG_INT_OE | REG_INT_FE ))
            {
                dev->stats.rx_errors++;
                if (irda_setptn & REG_INT_FE)
                    dev->stats.rx_frame_errors++;
                if (irda_setptn & REG_INT_OE)
                    dev->stats.rx_fifo_errors++;
            }
            if((irda_setptn & ( FRM_EVT_RX_EOFRX| REG_INT_EOF /*|FRM_EVT_RX_RDE*/)))
            {
                tmp_len = BU92725GUW_get_data(data);
				RK29IR_DATA_DBG("[%d][%s], sir receive data:\n", __LINE__, __FUNCTION__);
                for(i=0;i<=tmp_len;i++)
                {
                    RK29IR_DATA_DBG("0x%2x ",data[i]);
                    async_unwrap_char(dev, &dev->stats, &si->rx_buff, data[i]);
                }
				RK29IR_DATA_DBG("\n");
             //BU92725GUW_clr_fifo();
            }
        }
        else if(irda_hw_get_mode() == BU92725GUW_SEND) //tx
        {
			RK29IR_DBG("[%d][%s], transmit data\n", __LINE__, __FUNCTION__);
            if((irda_setptn & FRM_EVT_TX_TXE) && (si->tx_buff.len)) {
				RK29IR_DATA_DBG("[%d][%s], sir transmit data:\n", __LINE__, __FUNCTION__);
				for (i=0;i<si->tx_buff.len;i++) {
					RK29IR_DATA_DBG("0x%2x ", *(si->tx_buff.data)++);
				}
				RK29IR_DATA_DBG("\n");

				BU92725GUW_send_data(si->tx_buff.data, si->tx_buff.len, NULL, 0);
                si->tx_buff.len = 0;
            }
            else if (si->tx_buff.len == 0) {
                dev->stats.tx_packets++;
                dev->stats.tx_bytes += si->tx_buff.data - si->tx_buff.head;

                /*
                * Ok, we've finished transmitting.  Now enable
                * the receiver.  Sometimes we get a receive IRQ
                * immediately after a transmit...
                */
                if (si->newspeed) {
					rk29_irda_set_speed(si, si->newspeed);
                    si->newspeed = 0;
                } 

				irda_hw_set_moderx();
                
                /* I'm hungry! */
                netif_wake_queue(dev);
            }
        }
    }
    //enable_irq(dev->irq);

	return IRQ_HANDLED;
}

static int rk29_irda_start(struct net_device *dev)
{
	struct rk29_irda *si = netdev_priv(dev);
	int err = 0;
	
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	
	si->speed = 9600;
		
	/*
	 *  irda module power up
	 */
	if (si->pdata->irda_pwr_ctl)
		si->pdata->irda_pwr_ctl(1);
	si->power = 1;
	
	err = request_irq(dev->irq, rk29_irda_irq, IRQ_TYPE_LEVEL_LOW, dev->name, dev);//
	if (err) {
		printk("line %d: %s request_irq failed\n", __LINE__, __func__);
		goto err_irq;
	}

	/*
	 * The interrupt must remain disabled for now.
	 */
	disable_irq(dev->irq);

	/*
	 * Setup the smc port for the specified speed.
	 */
	err = irda_hw_startup();
	if (err) {		
		printk("line %d: %s irda_hw_startup err\n", __LINE__, __func__);
		goto err_startup;
	}
    irda_hw_set_moderx();

	/*
	 * Open a new IrLAP layer instance.
	 */
	si->irlap = irlap_open(dev, &si->qos, "rk29");
	err = -ENOMEM;
	if (!si->irlap) {
		printk("line %d: %s irlap_open err\n", __LINE__, __func__);
		goto err_irlap;
	}
	
	/*
	 * Now enable the interrupt and start the queue
	 */
	si->open = 1;
	enable_irq(dev->irq);
	netif_start_queue(dev);
	
	printk("rk29_ir: irda driver opened\n");

	//test
	//rk29_irda_set_speed(si, 4000000);
	//schedule_delayed_work(&dwork, msecs_to_jiffies(5000));

	return 0;

err_irlap:
	si->open = 0;
	irda_hw_shutdown();
	if (si->pdata->irda_pwr_ctl)
		si->pdata->irda_pwr_ctl(0);
err_startup:
	free_irq(dev->irq, dev);
err_irq:
	return err;
}

static int rk29_irda_stop(struct net_device *dev)
{
	struct rk29_irda *si = netdev_priv(dev);
	
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	disable_irq(dev->irq);
	irda_hw_shutdown();

	/*
	 * If we have been doing DMA receive, make sure we
	 * tidy that up cleanly.
	 */
	if (si->rxskb) {
		dev_kfree_skb(si->rxskb);
		si->rxskb = NULL;
	}

	/* Stop IrLAP */
	if (si->irlap) {
		irlap_close(si->irlap);
		si->irlap = NULL;
	}

	netif_stop_queue(dev);
	si->open = 0;

	/*
	 * Free resources
	 */
	free_irq(dev->irq, dev);

	//irda module power down
	if (si->pdata->irda_pwr_ctl)
		si->pdata->irda_pwr_ctl(0);

	si->power = 0;

	return 0;
}

static int rk29_irda_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct rk29_irda *si = netdev_priv(dev);
    int speed = irda_get_next_speed(skb);
	int i;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
    /*
     * Does this packet contain a request to change the interface
     * speed?  If so, remember it until we complete the transmission
     * of this frame.
     */
    if (speed != si->speed && speed != -1)
        si->newspeed = speed;

    /*
     * If this is an empty frame, we can bypass a lot.
     */
    if (skb->len == 0) {
        if (si->newspeed) {
            si->newspeed = 0;
			rk29_irda_set_speed(si, speed);
        }
        dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    netif_stop_queue(dev);

    if (!IS_FIR(si)) {
        si->tx_buff.data = si->tx_buff.head;
        si->tx_buff.len  = async_wrap_skb(skb, si->tx_buff.data, si->tx_buff.truesize);

        /* Disable STUART interrupts and switch to transmit mode. */
        /* enable STUART and transmit interrupts */
         irda_hw_tx_enable_irq(BU92725GUW_SIR);

		RK29IR_DATA_DBG("[%d][%s], sir transmit data:\n", __LINE__, __FUNCTION__);
		for (i=0;i<si->tx_buff.len;i++) {
			RK29IR_DATA_DBG("0x%2x ", *(si->tx_buff.data)++);
		}
		RK29IR_DATA_DBG("\n");

		dev_kfree_skb(skb);
		dev->trans_start = jiffies;
		BU92725GUW_send_data(si->tx_buff.data, si->tx_buff.len, NULL, 0);
		si->tx_buff.len = 0;
    } 
	else {
        unsigned long mtt = irda_get_mtt(skb);
		si->txskb = skb;
       
	   irda_hw_tx_enable_irq(BU92725GUW_FIR);

	  RK29IR_DATA_DBG("[%d][%s], fir transmit data:\n", __LINE__, __FUNCTION__);
	  for (i=0;i<skb->len;i++) {
		  RK29IR_DATA_DBG("0x%2x ", skb->data[i]);
	  }
	  RK29IR_DATA_DBG("\n");
	  
	  dev->trans_start = jiffies; 
       BU92725GUW_send_data(skb->data, skb->len, NULL, 0);
    }

    return NETDEV_TX_OK;
}

static int
rk29_irda_ioctl(struct net_device *dev, struct ifreq *ifreq, int cmd)
{
	struct if_irda_req *rq = (struct if_irda_req *)ifreq;
	struct rk29_irda *si = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	switch (cmd) {
	case SIOCSBANDWIDTH:
		if (capable(CAP_NET_ADMIN)) {
			/*
			 * We are unable to set the speed if the
			 * device is not running.
			 */
			if (si->open) {
				ret = rk29_irda_set_speed(si, rq->ifr_baudrate );
			} else {
				printk("rk29_irda_ioctl: SIOCSBANDWIDTH: !netif_running\n");
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
		rq->ifr_receiving = IS_FIR(si) ? 0
					: si->rx_buff.state != OUTSIDE_FRAME;
		break;

	default:
		break;
	}

	return ret;
}

static const struct net_device_ops rk29_irda_netdev_ops = {
	.ndo_open		= rk29_irda_start,
	.ndo_stop		= rk29_irda_stop,
	.ndo_start_xmit		= rk29_irda_hard_xmit,
	.ndo_do_ioctl		= rk29_irda_ioctl,
};


static int rk29_irda_init_iobuf(iobuff_t *io, int size)
{
	io->head = kmalloc(size, GFP_KERNEL | GFP_DMA);
	if (io->head != NULL) {
		io->truesize = size;
		io->in_frame = FALSE;
		io->state    = OUTSIDE_FRAME;
		io->data     = io->head;
	}
	return io->head ? 0 : -ENOMEM;
}

static void rk29_irda_fir_test(struct work_struct *work)
{
	char send_data[4] = {1,0,1,0};
	irda_hw_tx_enable_irq(BU92725GUW_FIR);
	
	BU92725GUW_send_data(send_data, 4, NULL, 0);
	
	schedule_delayed_work(&dwork, msecs_to_jiffies(5000));
	return ;
}

static int rk29_irda_probe(struct platform_device *pdev)
{
    struct irda_info *mach_info = NULL;
	struct net_device *dev;
	struct rk29_irda *si;
	unsigned int baudrate_mask;
	int err = -ENOMEM;

    RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

    mach_info = pdev->dev.platform_data;

    if (mach_info)
		mach_info->iomux_init();

    dev = alloc_irdadev(sizeof(struct rk29_irda));
	if (!dev) {
		printk("line %d: rk29_ir malloc failed\n", __LINE__);
		goto err_mem_1;
	}
    SET_NETDEV_DEV(dev, &pdev->dev);
	si = netdev_priv(dev);
	si->dev = &pdev->dev;
	si->pdata = pdev->dev.platform_data;

    /*
	 * Initialise the HP-SIR buffers
	 */
	err = rk29_irda_init_iobuf(&si->rx_buff, 14384);
	if (err) {
		printk("line %d: rk29_ir malloc failed\n", __LINE__);
		goto err_mem_2;
	}
	err = rk29_irda_init_iobuf(&si->tx_buff, 4000);
	if (err) {		
		printk("line %d: rk29_ir malloc failed\n", __LINE__);
		goto err_mem_3;
	}
	dev->netdev_ops	= &rk29_irda_netdev_ops;
	dev->irq = gpio_to_irq(mach_info->intr_pin);

	irda_init_max_qos_capabilies(&si->qos);

	/*
	 * We support original IRDA up to 115k2. (we don't currently
	 * support 4Mbps).  Min Turn Time set to 1ms or greater.
	 */
	baudrate_mask = IR_9600;

	switch (max_rate) {
	case 4000000:		baudrate_mask |= IR_4000000 << 8;
	case 115200:		baudrate_mask |= IR_115200;
	case 57600:		    baudrate_mask |= IR_57600;
	case 38400:		    baudrate_mask |= IR_38400;
	case 19200:		    baudrate_mask |= IR_19200;
	}

	si->qos.baud_rate.bits &= baudrate_mask;
	si->qos.min_turn_time.bits = 7;

	irda_qos_bits_to_value(&si->qos);

    /*
	 * Initially enable HP-SIR modulation, and ensure that the port
	 * is disabled.
	 */
	err = register_netdev(dev);
	if (err) {	
		printk("line %d: rk29_ir register_netdev failed\n", __LINE__);
		goto err_register;
	}
	platform_set_drvdata(pdev, dev);

	//test
	//wake_lock_init(&w_lock, WAKE_LOCK_SUSPEND, "rk29_cir");
	//wake_lock(&w_lock);
	
	return 0;
	
err_register:
	kfree(si->tx_buff.head);
err_mem_3:
	kfree(si->rx_buff.head);
err_mem_2:
	free_netdev(dev);
err_mem_1:
   return err;

}

static int rk29_irda_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (dev) {
		struct rk29_irda *si = netdev_priv(dev);
		unregister_netdev(dev);
		kfree(si->tx_buff.head);
		kfree(si->rx_buff.head);
		free_netdev(dev);
	}

	return 0;
}

#ifdef CONFIG_PM
/*
 * Suspend the IrDA interface.
 */
static int rk29_irda_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct rk29_irda *si;

	if (!dev)
		return 0;

	si = netdev_priv(dev);
	if (si->open) {
		/*
		 * Stop the transmit queue
		 */
		netif_device_detach(dev);
		disable_irq(dev->irq);
		irda_hw_shutdown();
		if (si->pdata->irda_pwr_ctl)
			si->pdata->irda_pwr_ctl(0);
	}

	return 0;
}

/*
 * Resume the IrDA interface.
 */
static int rk29_irda_resume(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct rk29_irda *si;

	if (!dev)
		return 0;

	si = netdev_priv(dev);
	if (si->open) {
		
		if (si->pdata->irda_pwr_ctl)
			si->pdata->irda_pwr_ctl(1);

		/*
		 * If we missed a speed change, initialise at the new speed
		 * directly.  It is debatable whether this is actually
		 * required, but in the interests of continuing from where
		 * we left off it is desireable.  The converse argument is
		 * that we should re-negotiate at 9600 baud again.
		 */
		if (si->newspeed) {
			si->speed = si->newspeed;
			si->newspeed = 0;
		}
		
		irda_hw_startup();
		enable_irq(dev->irq);

		/*
		 * This automatically wakes up the queue
		 */
		netif_device_attach(dev);
	}

	return 0;
}
#else
#define rk29_irda_suspend	NULL
#define rk29_irda_resume	NULL
#endif


static struct platform_driver irda_driver = {
	.driver = {
		.name = IRDA_NAME,
        .owner	= THIS_MODULE,
	},
	.probe = rk29_irda_probe,
	.remove = rk29_irda_remove,
	.suspend = rk29_irda_suspend,
	.resume = rk29_irda_resume,
};

static int __init irda_init(void)
{
	if (platform_driver_register(&irda_driver) != 0) {
    	printk("Could not register irda driver\n");
    	return -EINVAL;
	}
	return 0;
}

static void __exit irda_exit(void)
{
	platform_driver_unregister(&irda_driver);
}

module_init(irda_init);
module_exit(irda_exit);
MODULE_AUTHOR("  zyw@rock-chips.com");
MODULE_DESCRIPTION("Driver for irda device");
MODULE_LICENSE("GPL");

