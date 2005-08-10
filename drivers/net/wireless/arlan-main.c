/*
 *  Copyright (C) 1997 Cullen Jennings
 *  Copyright (C) 1998 Elmer Joandiu, elmer@ylenurme.ee
 *  GNU General Public License applies
 * This module provides support for the Arlan 655 card made by Aironet
 */

#include <linux/config.h>
#include "arlan.h"

#if BITS_PER_LONG != 32
#  error FIXME: this driver requires a 32-bit platform
#endif

static const char *arlan_version = "C.Jennigs 97 & Elmer.Joandi@ut.ee  Oct'98, http://www.ylenurme.ee/~elmer/655/";

struct net_device *arlan_device[MAX_ARLANS];

static int SID = SIDUNKNOWN;
static int radioNodeId = radioNodeIdUNKNOWN;
static char encryptionKey[12] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
int arlan_debug = debugUNKNOWN;
static int spreadingCode = spreadingCodeUNKNOWN;
static int channelNumber = channelNumberUNKNOWN;
static int channelSet = channelSetUNKNOWN;
static int systemId = systemIdUNKNOWN;
static int registrationMode = registrationModeUNKNOWN;
static int keyStart;
static int tx_delay_ms;
static int retries = 5;
static int tx_queue_len = 1;
static int arlan_EEPROM_bad;

#ifdef ARLAN_DEBUGGING

static int testMemory = testMemoryUNKNOWN;
static int irq = irqUNKNOWN;
static int txScrambled = 1;
static int mdebug;

module_param(irq, int, 0);
module_param(mdebug, int, 0);
module_param(testMemory, int, 0);
module_param(txScrambled, int, 0);
MODULE_PARM_DESC(irq, "(unused)");
MODULE_PARM_DESC(testMemory, "(unused)");
MODULE_PARM_DESC(mdebug, "Arlan multicast debugging (0-1)");
#endif

module_param_named(debug, arlan_debug, int, 0);
module_param(spreadingCode, int, 0);
module_param(channelNumber, int, 0);
module_param(channelSet, int, 0);
module_param(systemId, int, 0);
module_param(registrationMode, int, 0);
module_param(radioNodeId, int, 0);
module_param(SID, int, 0);
module_param(keyStart, int, 0);
module_param(tx_delay_ms, int, 0);
module_param(retries, int, 0);
module_param(tx_queue_len, int, 0);
module_param_named(EEPROM_bad, arlan_EEPROM_bad, int, 0);
MODULE_PARM_DESC(debug, "Arlan debug enable (0-1)");
MODULE_PARM_DESC(retries, "Arlan maximum packet retransmisions");
#ifdef ARLAN_ENTRY_EXIT_DEBUGGING
static int arlan_entry_debug;
static int arlan_exit_debug;
static int arlan_entry_and_exit_debug;
module_param_named(entry_debug, arlan_entry_debug, int, 0);
module_param_named(exit_debug, arlan_exit_debug, int, 0);
module_param_named(entry_and_exit_debug, arlan_entry_and_exit_debug, int, 0);
MODULE_PARM_DESC(entry_debug, "Arlan driver function entry debugging");
MODULE_PARM_DESC(exit_debug, "Arlan driver function exit debugging");
MODULE_PARM_DESC(entry_and_exit_debug, "Arlan driver function entry and exit debugging");
#endif

struct arlan_conf_stru arlan_conf[MAX_ARLANS];
static int arlans_found;

static  int 	arlan_open(struct net_device *dev);
static  int 	arlan_tx(struct sk_buff *skb, struct net_device *dev);
static  irqreturn_t arlan_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static  int 	arlan_close(struct net_device *dev);
static  struct net_device_stats *
		arlan_statistics		(struct net_device *dev);
static  void 	arlan_set_multicast		(struct net_device *dev);
static  int 	arlan_hw_tx			(struct net_device* dev, char *buf, int length );
static  int	arlan_hw_config			(struct net_device * dev);
static  void 	arlan_tx_done_interrupt		(struct net_device * dev, int status);
static  void	arlan_rx_interrupt		(struct net_device * dev, u_char rxStatus, u_short, u_short);
static  void	arlan_process_interrupt		(struct net_device * dev);
static	void	arlan_tx_timeout		(struct net_device *dev);

static inline long us2ticks(int us)
{
	return us * (1000000 / HZ);
}


#ifdef ARLAN_ENTRY_EXIT_DEBUGGING
#define ARLAN_DEBUG_ENTRY(name) \
	{\
	struct timeval timev;\
	do_gettimeofday(&timev);\
		if (arlan_entry_debug || arlan_entry_and_exit_debug)\
			printk("--->>>" name " %ld " "\n",((long int) timev.tv_sec * 1000000 + timev.tv_usec));\
	}
#define ARLAN_DEBUG_EXIT(name) \
	{\
	struct timeval timev;\
	do_gettimeofday(&timev);\
		if (arlan_exit_debug || arlan_entry_and_exit_debug)\
			printk("<<<---" name " %ld " "\n",((long int) timev.tv_sec * 1000000 + timev.tv_usec) );\
	}
#else
#define ARLAN_DEBUG_ENTRY(name)
#define ARLAN_DEBUG_EXIT(name)
#endif


#define arlan_interrupt_ack(dev)\
        clearClearInterrupt(dev);\
        setClearInterrupt(dev);

static inline int arlan_drop_tx(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	priv->stats.tx_errors++;
	if (priv->Conf->tx_delay_ms)
	{
		priv->tx_done_delayed = jiffies + priv->Conf->tx_delay_ms * HZ / 1000 + 1;
	}
	else
	{
		priv->waiting_command_mask &= ~ARLAN_COMMAND_TX;
		TXHEAD(dev).offset = 0;
		TXTAIL(dev).offset = 0;
		priv->txLast = 0;
		priv->bad = 0;
		if (!priv->under_reset && !priv->under_config)
			netif_wake_queue (dev);
	}
	return 1;
}


int arlan_command(struct net_device *dev, int command_p)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	struct arlan_conf_stru *conf = priv->Conf;
	int udelayed = 0;
	int i = 0;
	unsigned long flags;

	ARLAN_DEBUG_ENTRY("arlan_command");

	if (priv->card_polling_interval)
		priv->card_polling_interval = 1;

	if (arlan_debug & ARLAN_DEBUG_CHAIN_LOCKS)
		printk(KERN_DEBUG "arlan_command, %lx commandByte %x waiting %lx incoming %x \n",
		jiffies, READSHMB(arlan->commandByte),
		       priv->waiting_command_mask, command_p);

	priv->waiting_command_mask |= command_p;

	if (priv->waiting_command_mask & ARLAN_COMMAND_RESET)
		if (time_after(jiffies, priv->lastReset + 5 * HZ))
			priv->waiting_command_mask &= ~ARLAN_COMMAND_RESET;

	if (priv->waiting_command_mask & ARLAN_COMMAND_INT_ACK)
	{
		arlan_interrupt_ack(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_INT_ACK;
	}
	if (priv->waiting_command_mask & ARLAN_COMMAND_INT_ENABLE)
	{
		setInterruptEnable(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_INT_ENABLE;
	}

	/* Card access serializing lock */
	spin_lock_irqsave(&priv->lock, flags);

	/* Check cards status and waiting */

	if (priv->waiting_command_mask & (ARLAN_COMMAND_LONG_WAIT_NOW | ARLAN_COMMAND_WAIT_NOW))
	{
		while (priv->waiting_command_mask & (ARLAN_COMMAND_LONG_WAIT_NOW | ARLAN_COMMAND_WAIT_NOW))
		{
			if (READSHMB(arlan->resetFlag) ||
				READSHMB(arlan->commandByte))	/* || 
								   (readControlRegister(dev) & ARLAN_ACCESS))
								 */
				udelay(40);
			else
				priv->waiting_command_mask &= ~(ARLAN_COMMAND_LONG_WAIT_NOW | ARLAN_COMMAND_WAIT_NOW);

			udelayed++;

			if (priv->waiting_command_mask & ARLAN_COMMAND_LONG_WAIT_NOW)
			{
				if (udelayed * 40 > 1000000)
				{
					printk(KERN_ERR "%s long wait too long \n", dev->name);
					priv->waiting_command_mask |= ARLAN_COMMAND_RESET;
					break;
				}
			}
			else if (priv->waiting_command_mask & ARLAN_COMMAND_WAIT_NOW)
			{
				if (udelayed * 40 > 1000)
				{
					printk(KERN_ERR "%s short wait too long \n", dev->name);
					goto bad_end;
				}
			}
		}
	}
	else
	{
		i = 0;
		while ((READSHMB(arlan->resetFlag) ||
			READSHMB(arlan->commandByte)) &&
			conf->pre_Command_Wait > (i++) * 10)
			udelay(10);


		if ((READSHMB(arlan->resetFlag) ||
			READSHMB(arlan->commandByte)) &&
			!(priv->waiting_command_mask & ARLAN_COMMAND_RESET))
		{
			goto card_busy_end;
		}
	}
	if (priv->waiting_command_mask & ARLAN_COMMAND_RESET)
		priv->under_reset = 1;
	if (priv->waiting_command_mask & ARLAN_COMMAND_CONF)
		priv->under_config = 1;

	/* Issuing command */
	arlan_lock_card_access(dev);
	if (priv->waiting_command_mask & ARLAN_COMMAND_POWERUP)
	{
	//     if (readControlRegister(dev) & (ARLAN_ACCESS && ARLAN_POWER))
		setPowerOn(dev);
		arlan_interrupt_lancpu(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_POWERUP;
		priv->waiting_command_mask |= ARLAN_COMMAND_RESET;
		priv->card_polling_interval = HZ / 10;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_ACTIVATE)
	{
		WRITESHMB(arlan->commandByte, ARLAN_COM_ACTIVATE);
		arlan_interrupt_lancpu(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_ACTIVATE;
		priv->card_polling_interval = HZ / 10;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_RX_ABORT)
	{
		if (priv->rx_command_given)
		{
			WRITESHMB(arlan->commandByte, ARLAN_COM_RX_ABORT);
			arlan_interrupt_lancpu(dev);
			priv->rx_command_given = 0;
		}
		priv->waiting_command_mask &= ~ARLAN_COMMAND_RX_ABORT;
		priv->card_polling_interval = 1;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_TX_ABORT)
	{
		if (priv->tx_command_given)
		{
			WRITESHMB(arlan->commandByte, ARLAN_COM_TX_ABORT);
			arlan_interrupt_lancpu(dev);
			priv->tx_command_given = 0;
		}
		priv->waiting_command_mask &= ~ARLAN_COMMAND_TX_ABORT;
		priv->card_polling_interval = 1;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_RESET)
	{
		priv->under_reset=1;
		netif_stop_queue (dev);

		arlan_drop_tx(dev);
		if (priv->tx_command_given || priv->rx_command_given)
		{
			printk(KERN_ERR "%s: Reset under tx or rx command \n", dev->name);
		}
		netif_stop_queue (dev);
		if (arlan_debug & ARLAN_DEBUG_RESET)
			printk(KERN_ERR "%s: Doing chip reset\n", dev->name);
		priv->lastReset = jiffies;
		WRITESHM(arlan->commandByte, 0, u_char);
		/* hold card in reset state */
		setHardwareReset(dev);
		/* set reset flag and then release reset */
		WRITESHM(arlan->resetFlag, 0xff, u_char);
		clearChannelAttention(dev);
		clearHardwareReset(dev);
		priv->card_polling_interval = HZ / 4;
		priv->waiting_command_mask &= ~ARLAN_COMMAND_RESET;
		priv->waiting_command_mask |= ARLAN_COMMAND_INT_RACK;
//		priv->waiting_command_mask |= ARLAN_COMMAND_INT_RENABLE; 
//		priv->waiting_command_mask |= ARLAN_COMMAND_RX;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_INT_RACK)
	{
		clearHardwareReset(dev);
		clearClearInterrupt(dev);
		setClearInterrupt(dev);
		setInterruptEnable(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_INT_RACK;
		priv->waiting_command_mask |= ARLAN_COMMAND_CONF;
		priv->under_config = 1;
		priv->under_reset = 0;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_INT_RENABLE)
	{
		setInterruptEnable(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_INT_RENABLE;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_CONF)
	{
		if (priv->tx_command_given || priv->rx_command_given)
		{
			printk(KERN_ERR "%s: Reset under tx or rx command \n", dev->name);
		}
		arlan_drop_tx(dev);
		setInterruptEnable(dev);
		arlan_hw_config(dev);
		arlan_interrupt_lancpu(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_CONF;
		priv->card_polling_interval = HZ / 10;
//		priv->waiting_command_mask |= ARLAN_COMMAND_INT_RACK;   
//		priv->waiting_command_mask |= ARLAN_COMMAND_INT_ENABLE; 
		priv->waiting_command_mask |= ARLAN_COMMAND_CONF_WAIT;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_CONF_WAIT)
	{
		if (READSHMB(arlan->configuredStatusFlag) != 0 &&
			READSHMB(arlan->diagnosticInfo) == 0xff)
		{
			priv->waiting_command_mask &= ~ARLAN_COMMAND_CONF_WAIT;
			priv->waiting_command_mask |= ARLAN_COMMAND_RX;
			priv->waiting_command_mask |= ARLAN_COMMAND_TBUSY_CLEAR;
			priv->card_polling_interval = HZ / 10;
			priv->tx_command_given = 0;
			priv->under_config = 0;
		}
		else
		{
			priv->card_polling_interval = 1;
			if (arlan_debug & ARLAN_DEBUG_TIMING)
				printk(KERN_ERR "configure delayed \n");
		}
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_RX)
	{
		if (!registrationBad(dev))
		{
			setInterruptEnable(dev);
			memset_io(arlan->commandParameter, 0, 0xf);
			WRITESHMB(arlan->commandByte, ARLAN_COM_INT | ARLAN_COM_RX_ENABLE);
			WRITESHMB(arlan->commandParameter[0], conf->rxParameter);
			arlan_interrupt_lancpu(dev);
			priv->rx_command_given = 0; // mnjah, bad
			priv->waiting_command_mask &= ~ARLAN_COMMAND_RX;
			priv->card_polling_interval = 1;
		}
		else
			priv->card_polling_interval = 2;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_TBUSY_CLEAR)
	{
		if ( !registrationBad(dev) &&
		     (netif_queue_stopped(dev) || !netif_running(dev)) )
			{
				priv->waiting_command_mask &= ~ARLAN_COMMAND_TBUSY_CLEAR;
				netif_wake_queue (dev);
			}
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_TX)
	{
		if (!test_and_set_bit(0, (void *) &priv->tx_command_given))
		{
			if (time_after(jiffies, 
				       priv->tx_last_sent + us2ticks(conf->rx_tweak1))
			    || time_before(jiffies,
					   priv->last_rx_int_ack_time + us2ticks(conf->rx_tweak2)))
			{
				setInterruptEnable(dev);
				memset_io(arlan->commandParameter, 0, 0xf);
				WRITESHMB(arlan->commandByte, ARLAN_COM_TX_ENABLE | ARLAN_COM_INT);
				memcpy_toio(arlan->commandParameter, &TXLAST(dev), 14);
//				for ( i=1 ; i < 15 ; i++) printk("%02x:",READSHMB(arlan->commandParameter[i]));
				priv->tx_last_sent = jiffies;
				arlan_interrupt_lancpu(dev);
				priv->tx_command_given = 1;
				priv->waiting_command_mask &= ~ARLAN_COMMAND_TX;
				priv->card_polling_interval = 1;
			}
			else
			{
				priv->tx_command_given = 0;
				priv->card_polling_interval = 1;
			}
		} 
		else if (arlan_debug & ARLAN_DEBUG_CHAIN_LOCKS)
			printk(KERN_ERR "tx command when tx chain locked \n");
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_NOOPINT)
	{
		{
			WRITESHMB(arlan->commandByte, ARLAN_COM_NOP | ARLAN_COM_INT);
		}
		arlan_interrupt_lancpu(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_NOOPINT;
		priv->card_polling_interval = HZ / 3;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_NOOP)
	{
		WRITESHMB(arlan->commandByte, ARLAN_COM_NOP);
		arlan_interrupt_lancpu(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_NOOP;
		priv->card_polling_interval = HZ / 3;
	}
	else if (priv->waiting_command_mask & ARLAN_COMMAND_SLOW_POLL)
	{
		WRITESHMB(arlan->commandByte, ARLAN_COM_GOTO_SLOW_POLL);
		arlan_interrupt_lancpu(dev);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_SLOW_POLL;
		priv->card_polling_interval = HZ / 3;
	} 
	else if (priv->waiting_command_mask & ARLAN_COMMAND_POWERDOWN)
	{
		setPowerOff(dev);
		if (arlan_debug & ARLAN_DEBUG_CARD_STATE)
			printk(KERN_WARNING "%s: Arlan Going Standby\n", dev->name);
		priv->waiting_command_mask &= ~ARLAN_COMMAND_POWERDOWN;
		priv->card_polling_interval = 3 * HZ;
	}
	arlan_unlock_card_access(dev);
	for (i = 0; READSHMB(arlan->commandByte) && i < 20; i++)
		udelay(10);
	if (READSHMB(arlan->commandByte))
		if (arlan_debug & ARLAN_DEBUG_CARD_STATE)
			printk(KERN_ERR "card busy leaving command %lx\n", priv->waiting_command_mask);

	spin_unlock_irqrestore(&priv->lock, flags);
	ARLAN_DEBUG_EXIT("arlan_command");
	priv->last_command_buff_free_time = jiffies;
	return 0;

card_busy_end:
	if (time_after(jiffies, priv->last_command_buff_free_time + HZ))
		priv->waiting_command_mask |= ARLAN_COMMAND_CLEAN_AND_RESET;

	if (arlan_debug & ARLAN_DEBUG_CARD_STATE)
		printk(KERN_ERR "%s arlan_command card busy end \n", dev->name);
	spin_unlock_irqrestore(&priv->lock, flags);
	ARLAN_DEBUG_EXIT("arlan_command");
	return 1;

bad_end:
	printk(KERN_ERR "%s arlan_command bad end \n", dev->name);

	spin_unlock_irqrestore(&priv->lock, flags);
	ARLAN_DEBUG_EXIT("arlan_command");

	return -1;
}

static inline void arlan_command_process(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	int times = 0;
	while (priv->waiting_command_mask && times < 8)
	{
		if (priv->waiting_command_mask)
		{
			if (arlan_command(dev, 0))
				break;
			times++;
		}
		/* if long command, we won't repeat trying */ ;
		if (priv->card_polling_interval > 1)
			break;
		times++;
	}
}


static inline void arlan_retransmit_now(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);


	ARLAN_DEBUG_ENTRY("arlan_retransmit_now");
	if (TXLAST(dev).offset == 0)
	{
		if (TXHEAD(dev).offset)
		{
			priv->txLast = 0;
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN) printk(KERN_DEBUG "TX buff switch to head \n");

		}
		else if (TXTAIL(dev).offset)
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN) printk(KERN_DEBUG "TX buff switch to tail \n");
			priv->txLast = 1;
		}
		else
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN) printk(KERN_ERR "ReTransmit buff empty");
		netif_wake_queue (dev);
		return;

	}
	arlan_command(dev, ARLAN_COMMAND_TX);

	priv->Conf->driverRetransmissions++;
	priv->retransmissions++;

	IFDEBUG(ARLAN_DEBUG_TX_CHAIN) printk("Retransmit %d bytes \n", TXLAST(dev).length);

	ARLAN_DEBUG_EXIT("arlan_retransmit_now");
}



static void arlan_registration_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct arlan_private *priv = netdev_priv(dev);
	int bh_mark_needed = 0;
	int next_tick = 1;
	long lostTime = ((long)jiffies - (long)priv->registrationLastSeen)
			* (1000/HZ);

	if (registrationBad(dev))
	{
		priv->registrationLostCount++;
		if (lostTime > 7000 && lostTime < 7200)
		{
			printk(KERN_NOTICE "%s registration Lost \n", dev->name);
		}
		if (lostTime / priv->reRegisterExp > 2000)
			arlan_command(dev, ARLAN_COMMAND_CLEAN_AND_CONF);
		if (lostTime / (priv->reRegisterExp) > 3500)
			arlan_command(dev, ARLAN_COMMAND_CLEAN_AND_RESET);
		if (priv->reRegisterExp < 400)
			priv->reRegisterExp += 2;
		if (lostTime > 7200)
		{
			next_tick = HZ;
			arlan_command(dev, ARLAN_COMMAND_CLEAN_AND_RESET);
		}
	}
	else
	{
		if (priv->Conf->registrationMode && lostTime > 10000 &&
			priv->registrationLostCount)
		{
			printk(KERN_NOTICE "%s registration is back after %ld milliseconds\n",
			       dev->name, lostTime);
		}
		priv->registrationLastSeen = jiffies;
		priv->registrationLostCount = 0;
		priv->reRegisterExp = 1;
		if (!netif_running(dev) )
			netif_wake_queue(dev);
		if (time_after(priv->tx_last_sent,priv->tx_last_cleared) &&
		    time_after(jiffies, priv->tx_last_sent * 5*HZ) ){
			arlan_command(dev, ARLAN_COMMAND_CLEAN_AND_RESET);		
			priv->tx_last_cleared = jiffies;
		}
	}


	if (!registrationBad(dev) && priv->ReTransmitRequested)
	{
		IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
			printk(KERN_ERR "Retransmit from timer \n");
		priv->ReTransmitRequested = 0;
		arlan_retransmit_now(dev);
	}
	if (!registrationBad(dev) &&
		time_after(jiffies, priv->tx_done_delayed) &&
		priv->tx_done_delayed != 0)
	{
		TXLAST(dev).offset = 0;
		if (priv->txLast)
			priv->txLast = 0;
		else if (TXTAIL(dev).offset)
			priv->txLast = 1;
		if (TXLAST(dev).offset)
		{
			arlan_retransmit_now(dev);
			dev->trans_start = jiffies;
		}
		if (!(TXHEAD(dev).offset && TXTAIL(dev).offset))
		{
			netif_wake_queue (dev);
		}
		priv->tx_done_delayed = 0;
		bh_mark_needed = 1;
	}
	if (bh_mark_needed)
	{
		netif_wake_queue (dev);
	}
	arlan_process_interrupt(dev);

	if (next_tick < priv->card_polling_interval)
		next_tick = priv->card_polling_interval;

	priv->timer.expires = jiffies + next_tick;

	add_timer(&priv->timer);
}


#ifdef ARLAN_DEBUGGING

static void arlan_print_registers(struct net_device *dev, int line)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem *arlan = priv->card;

	u_char hostcpuLock, lancpuLock, controlRegister, cntrlRegImage,
		txStatus, rxStatus, interruptInProgress, commandByte;


	ARLAN_DEBUG_ENTRY("arlan_print_registers");
	READSHM(interruptInProgress, arlan->interruptInProgress, u_char);
	READSHM(hostcpuLock, arlan->hostcpuLock, u_char);
	READSHM(lancpuLock, arlan->lancpuLock, u_char);
	READSHM(controlRegister, arlan->controlRegister, u_char);
	READSHM(cntrlRegImage, arlan->cntrlRegImage, u_char);
	READSHM(txStatus, arlan->txStatus, u_char);
	READSHM(rxStatus, arlan->rxStatus, u_char);
	READSHM(commandByte, arlan->commandByte, u_char);

	printk(KERN_WARNING "line %04d IP %02x HL %02x LL %02x CB %02x CR %02x CRI %02x TX %02x RX %02x\n",
		line, interruptInProgress, hostcpuLock, lancpuLock, commandByte,
		controlRegister, cntrlRegImage, txStatus, rxStatus);

	ARLAN_DEBUG_EXIT("arlan_print_registers");
}
#endif


static int arlan_hw_tx(struct net_device *dev, char *buf, int length)
{
	int i;

	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	struct arlan_conf_stru *conf = priv->Conf;

	int tailStarts = 0x800;
	int headEnds = 0x0;


	ARLAN_DEBUG_ENTRY("arlan_hw_tx");
	if (TXHEAD(dev).offset)
		headEnds = (((TXHEAD(dev).offset + TXHEAD(dev).length - offsetof(struct arlan_shmem, txBuffer)) / 64) + 1) * 64;
	if (TXTAIL(dev).offset)
		tailStarts = 0x800 - (((TXTAIL(dev).offset - offsetof(struct arlan_shmem, txBuffer)) / 64) + 2) * 64;


	if (!TXHEAD(dev).offset && length < tailStarts)
	{
		IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
			printk(KERN_ERR "TXHEAD insert, tailStart %d\n", tailStarts);

		TXHEAD(dev).offset =
			offsetof(struct arlan_shmem, txBuffer);
		TXHEAD(dev).length = length - ARLAN_FAKE_HDR_LEN;
		for (i = 0; i < 6; i++)
			TXHEAD(dev).dest[i] = buf[i];
		TXHEAD(dev).clear = conf->txClear;
		TXHEAD(dev).retries = conf->txRetries;	/* 0 is use default */
		TXHEAD(dev).routing = conf->txRouting;
		TXHEAD(dev).scrambled = conf->txScrambled;
		memcpy_toio((char __iomem *)arlan + TXHEAD(dev).offset, buf + ARLAN_FAKE_HDR_LEN, TXHEAD(dev).length);
	}
	else if (!TXTAIL(dev).offset && length < (0x800 - headEnds))
	{
		IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
			printk(KERN_ERR "TXTAIL insert, headEnd %d\n", headEnds);

		TXTAIL(dev).offset =
			offsetof(struct arlan_shmem, txBuffer) + 0x800 - (length / 64 + 2) * 64;
		TXTAIL(dev).length = length - ARLAN_FAKE_HDR_LEN;
		for (i = 0; i < 6; i++)
			TXTAIL(dev).dest[i] = buf[i];
		TXTAIL(dev).clear = conf->txClear;
		TXTAIL(dev).retries = conf->txRetries;
		TXTAIL(dev).routing = conf->txRouting;
		TXTAIL(dev).scrambled = conf->txScrambled;
		memcpy_toio(((char __iomem *)arlan + TXTAIL(dev).offset), buf + ARLAN_FAKE_HDR_LEN, TXTAIL(dev).length);
	}
	else
	{
		netif_stop_queue (dev);
		IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
			printk(KERN_ERR "TX TAIL & HEAD full, return, tailStart %d headEnd %d\n", tailStarts, headEnds);
		return -1;
	}
	priv->out_bytes += length;
	priv->out_bytes10 += length;
	if (conf->measure_rate < 1)
		conf->measure_rate = 1;
	if (time_after(jiffies, priv->out_time + conf->measure_rate * HZ))
	{
		conf->out_speed = priv->out_bytes / conf->measure_rate;
		priv->out_bytes = 0;
		priv->out_time = jiffies;
	}
	if (time_after(jiffies, priv->out_time10 + conf->measure_rate * 10*HZ))
	{
		conf->out_speed10 = priv->out_bytes10 / (10 * conf->measure_rate);
		priv->out_bytes10 = 0;
		priv->out_time10 = jiffies;
	}
	if (TXHEAD(dev).offset && TXTAIL(dev).offset)
	{
		netif_stop_queue (dev);
		return 0;
	}
	else
		netif_start_queue (dev);


	IFDEBUG(ARLAN_DEBUG_HEADER_DUMP)
		printk(KERN_WARNING "%s Transmit t %2x:%2x:%2x:%2x:%2x:%2x f %2x:%2x:%2x:%2x:%2x:%2x \n", dev->name,
		   (unsigned char) buf[0], (unsigned char) buf[1], (unsigned char) buf[2], (unsigned char) buf[3],
		   (unsigned char) buf[4], (unsigned char) buf[5], (unsigned char) buf[6], (unsigned char) buf[7],
		   (unsigned char) buf[8], (unsigned char) buf[9], (unsigned char) buf[10], (unsigned char) buf[11]);

	IFDEBUG(ARLAN_DEBUG_TX_CHAIN) printk(KERN_ERR "TX command prepare for buffer %d\n", priv->txLast);

	arlan_command(dev, ARLAN_COMMAND_TX);

	priv->tx_last_sent = jiffies;

	IFDEBUG(ARLAN_DEBUG_TX_CHAIN) printk("%s TX Qued %d bytes \n", dev->name, length);

	ARLAN_DEBUG_EXIT("arlan_hw_tx");

	return 0;
}


static int arlan_hw_config(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	struct arlan_conf_stru *conf = priv->Conf;

	ARLAN_DEBUG_ENTRY("arlan_hw_config");

	printk(KERN_NOTICE "%s arlan configure called \n", dev->name);
	if (arlan_EEPROM_bad)
		printk(KERN_NOTICE "arlan configure with eeprom bad option \n");


	WRITESHM(arlan->spreadingCode, conf->spreadingCode, u_char);
	WRITESHM(arlan->channelSet, conf->channelSet, u_char);

	if (arlan_EEPROM_bad)
		WRITESHM(arlan->defaultChannelSet, conf->channelSet, u_char);

	WRITESHM(arlan->channelNumber, conf->channelNumber, u_char);

	WRITESHM(arlan->scramblingDisable, conf->scramblingDisable, u_char);
	WRITESHM(arlan->txAttenuation, conf->txAttenuation, u_char);

	WRITESHM(arlan->systemId, conf->systemId, u_int);

	WRITESHM(arlan->maxRetries, conf->maxRetries, u_char);
	WRITESHM(arlan->receiveMode, conf->receiveMode, u_char);
	WRITESHM(arlan->priority, conf->priority, u_char);
	WRITESHM(arlan->rootOrRepeater, conf->rootOrRepeater, u_char);
	WRITESHM(arlan->SID, conf->SID, u_int);

	WRITESHM(arlan->registrationMode, conf->registrationMode, u_char);

	WRITESHM(arlan->registrationFill, conf->registrationFill, u_char);
	WRITESHM(arlan->localTalkAddress, conf->localTalkAddress, u_char);
	WRITESHM(arlan->codeFormat, conf->codeFormat, u_char);
	WRITESHM(arlan->numChannels, conf->numChannels, u_char);
	WRITESHM(arlan->channel1, conf->channel1, u_char);
	WRITESHM(arlan->channel2, conf->channel2, u_char);
	WRITESHM(arlan->channel3, conf->channel3, u_char);
	WRITESHM(arlan->channel4, conf->channel4, u_char);
	WRITESHM(arlan->radioNodeId, conf->radioNodeId, u_short);
	WRITESHM(arlan->SID, conf->SID, u_int);
	WRITESHM(arlan->waitTime, conf->waitTime, u_short);
	WRITESHM(arlan->lParameter, conf->lParameter, u_short);
	memcpy_toio(&(arlan->_15), &(conf->_15), 3);
	WRITESHM(arlan->_15, conf->_15, u_short);
	WRITESHM(arlan->headerSize, conf->headerSize, u_short);
	if (arlan_EEPROM_bad)
		WRITESHM(arlan->hardwareType, conf->hardwareType, u_char);
	WRITESHM(arlan->radioType, conf->radioType, u_char);
	if (arlan_EEPROM_bad)
		WRITESHM(arlan->radioModule, conf->radioType, u_char);

	memcpy_toio(arlan->encryptionKey + keyStart, encryptionKey, 8);
	memcpy_toio(arlan->name, conf->siteName, 16);

	WRITESHMB(arlan->commandByte, ARLAN_COM_INT | ARLAN_COM_CONF);	/* do configure */
	memset_io(arlan->commandParameter, 0, 0xf);	/* 0xf */
	memset_io(arlan->commandParameter + 1, 0, 2);
	if (conf->writeEEPROM)
	{
		  memset_io(arlan->commandParameter, conf->writeEEPROM, 1);
//		conf->writeEEPROM=0;
	}
	if (conf->registrationMode && conf->registrationInterrupts)
		memset_io(arlan->commandParameter + 3, 1, 1);
	else
		memset_io(arlan->commandParameter + 3, 0, 1);

	priv->irq_test_done = 0;

	if (conf->tx_queue_len)
		dev->tx_queue_len = conf->tx_queue_len;
	udelay(100);

	ARLAN_DEBUG_EXIT("arlan_hw_config");
	return 0;
}


static int arlan_read_card_configuration(struct net_device *dev)
{
	u_char tlx415;
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	struct arlan_conf_stru *conf = priv->Conf;

	ARLAN_DEBUG_ENTRY("arlan_read_card_configuration");

	if (radioNodeId == radioNodeIdUNKNOWN)
	{
		READSHM(conf->radioNodeId, arlan->radioNodeId, u_short);
	}
	else
		conf->radioNodeId = radioNodeId;
		
	if (SID == SIDUNKNOWN)
	{
		READSHM(conf->SID, arlan->SID, u_int);
	}
	else conf->SID = SID;
		
	if (spreadingCode == spreadingCodeUNKNOWN)
	{
		  READSHM(conf->spreadingCode, arlan->spreadingCode, u_char);
	}
	else
		conf->spreadingCode = spreadingCode;
		
	if (channelSet == channelSetUNKNOWN)
	{
		READSHM(conf->channelSet, arlan->channelSet, u_char);
	}
	else conf->channelSet = channelSet;

	if (channelNumber == channelNumberUNKNOWN)
	{
		READSHM(conf->channelNumber, arlan->channelNumber, u_char);
	}
	else conf->channelNumber = channelNumber;
	
	READSHM(conf->scramblingDisable, arlan->scramblingDisable, u_char);
	READSHM(conf->txAttenuation, arlan->txAttenuation, u_char);
	
	if (systemId == systemIdUNKNOWN)
	{
		READSHM(conf->systemId, arlan->systemId, u_int);
	} 
	else conf->systemId = systemId;
	
	READSHM(conf->maxDatagramSize, arlan->maxDatagramSize, u_short);
	READSHM(conf->maxFrameSize, arlan->maxFrameSize, u_short);
	READSHM(conf->maxRetries, arlan->maxRetries, u_char);
	READSHM(conf->receiveMode, arlan->receiveMode, u_char);
	READSHM(conf->priority, arlan->priority, u_char);
	READSHM(conf->rootOrRepeater, arlan->rootOrRepeater, u_char);

	if (SID == SIDUNKNOWN)
	{
		  READSHM(conf->SID, arlan->SID, u_int);
	}
	else conf->SID = SID;
	
	if (registrationMode == registrationModeUNKNOWN)
	{
		  READSHM(conf->registrationMode, arlan->registrationMode, u_char);
	}
	else conf->registrationMode = registrationMode;
	
	READSHM(conf->registrationFill, arlan->registrationFill, u_char);
	READSHM(conf->localTalkAddress, arlan->localTalkAddress, u_char);
	READSHM(conf->codeFormat, arlan->codeFormat, u_char);
	READSHM(conf->numChannels, arlan->numChannels, u_char);
	READSHM(conf->channel1, arlan->channel1, u_char);
	READSHM(conf->channel2, arlan->channel2, u_char);
	READSHM(conf->channel3, arlan->channel3, u_char);
	READSHM(conf->channel4, arlan->channel4, u_char);
	READSHM(conf->waitTime, arlan->waitTime, u_short);
	READSHM(conf->lParameter, arlan->lParameter, u_short);
	READSHM(conf->_15, arlan->_15, u_short);
	READSHM(conf->headerSize, arlan->headerSize, u_short);
	READSHM(conf->hardwareType, arlan->hardwareType, u_char);
	READSHM(conf->radioType, arlan->radioModule, u_char);
	
	if (conf->radioType == 0)
		conf->radioType = 0xc;

	WRITESHM(arlan->configStatus, 0xA5, u_char);
	READSHM(tlx415, arlan->configStatus, u_char);
	
	if (tlx415 != 0xA5)
		printk(KERN_INFO "%s tlx415 chip \n", dev->name);
	
	conf->txClear = 0;
	conf->txRetries = 1;
	conf->txRouting = 1;
	conf->txScrambled = 0;
	conf->rxParameter = 1;
	conf->txTimeoutMs = 4000;
	conf->waitCardTimeout = 100000;
	conf->receiveMode = ARLAN_RCV_CLEAN;
	memcpy_fromio(conf->siteName, arlan->name, 16);
	conf->siteName[16] = '\0';
	conf->retries = retries;
	conf->tx_delay_ms = tx_delay_ms;
	conf->ReTransmitPacketMaxSize = 200;
	conf->waitReTransmitPacketMaxSize = 200;
	conf->txAckTimeoutMs = 900;
	conf->fastReTransCount = 3;

	ARLAN_DEBUG_EXIT("arlan_read_card_configuration");

	return 0;
}


static int lastFoundAt = 0xbe000;


/*
 * This is the real probe routine. Linux has a history of friendly device
 * probes on the ISA bus. A good device probes avoids doing writes, and
 * verifies that the correct device exists and functions.
 */
#define ARLAN_SHMEM_SIZE	0x2000
static int __init arlan_check_fingerprint(unsigned long memaddr)
{
	static const char probeText[] = "TELESYSTEM SLW INC.    ARLAN \0";
	volatile struct arlan_shmem __iomem *arlan = (struct arlan_shmem *) memaddr;
	unsigned long paddr = virt_to_phys((void *) memaddr);
	char tempBuf[49];

	ARLAN_DEBUG_ENTRY("arlan_check_fingerprint");

	if (!request_mem_region(paddr, ARLAN_SHMEM_SIZE, "arlan")) {
		// printk(KERN_WARNING "arlan: memory region %lx excluded from probing \n",paddr);
		return -ENODEV;
	}

	memcpy_fromio(tempBuf, arlan->textRegion, 29);
	tempBuf[30] = 0;

	/* check for card at this address */
	if (0 != strncmp(tempBuf, probeText, 29)){
 		release_mem_region(paddr, ARLAN_SHMEM_SIZE);
		return -ENODEV;
	}

//   printk(KERN_INFO "arlan found at 0x%x \n",memaddr);
	ARLAN_DEBUG_EXIT("arlan_check_fingerprint");

	return 0;
}

static int arlan_change_mtu(struct net_device *dev, int new_mtu)
{
	struct arlan_private *priv = netdev_priv(dev);
	struct arlan_conf_stru *conf = priv->Conf;

	ARLAN_DEBUG_ENTRY("arlan_change_mtu");
	if (new_mtu > 2032)
		return -EINVAL;
	dev->mtu = new_mtu;
	if (new_mtu < 256)
		new_mtu = 256;	/* cards book suggests 1600 */
	conf->maxDatagramSize = new_mtu;
	conf->maxFrameSize = new_mtu + 48;

	arlan_command(dev, ARLAN_COMMAND_CLEAN_AND_CONF);
	printk(KERN_NOTICE "%s mtu changed to %d \n", dev->name, new_mtu);

	ARLAN_DEBUG_EXIT("arlan_change_mtu");

	return 0;
}

static int arlan_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;


	ARLAN_DEBUG_ENTRY("arlan_mac_addr");
	return -EINVAL;

	if (!netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	ARLAN_DEBUG_EXIT("arlan_mac_addr");
	return 0;
}



static int __init arlan_setup_device(struct net_device *dev, int num)
{
	struct arlan_private *ap = netdev_priv(dev);
	int err;

	ARLAN_DEBUG_ENTRY("arlan_setup_device");

	ap->conf = (struct arlan_shmem *)(ap+1);

	dev->tx_queue_len = tx_queue_len;
	dev->open = arlan_open;
	dev->stop = arlan_close;
	dev->hard_start_xmit = arlan_tx;
	dev->get_stats = arlan_statistics;
	dev->set_multicast_list = arlan_set_multicast;
	dev->change_mtu = arlan_change_mtu;
	dev->set_mac_address = arlan_mac_addr;
	dev->tx_timeout = arlan_tx_timeout;
	dev->watchdog_timeo = 3*HZ;
	
	ap->irq_test_done = 0;
	ap->Conf = &arlan_conf[num];

	ap->Conf->pre_Command_Wait = 40;
	ap->Conf->rx_tweak1 = 30;
	ap->Conf->rx_tweak2 = 0;


	err = register_netdev(dev);
	if (err) {
		release_mem_region(virt_to_phys((void *) dev->mem_start), 
			   ARLAN_SHMEM_SIZE);
		free_netdev(dev);
		return err;
	}
	arlan_device[num] = dev;
	ARLAN_DEBUG_EXIT("arlan_setup_device");
	return 0;
}

static int __init arlan_probe_here(struct net_device *dev, 
				   unsigned long memaddr)
{
	struct arlan_private *ap = netdev_priv(dev);

	ARLAN_DEBUG_ENTRY("arlan_probe_here");

	if (arlan_check_fingerprint(memaddr))
		return -ENODEV;

	printk(KERN_NOTICE "%s: Arlan found at %x, \n ", dev->name, 
	       (int) virt_to_phys((void*)memaddr));

	ap->card = (void *) memaddr;
	dev->mem_start = memaddr;
	dev->mem_end = memaddr + ARLAN_SHMEM_SIZE-1;

	if (dev->irq < 2)
	{
		READSHM(dev->irq, ap->card->irqLevel, u_char);
	} else if (dev->irq == 2)
		dev->irq = 9;

	arlan_read_card_configuration(dev);

	ARLAN_DEBUG_EXIT("arlan_probe_here");
	return 0;
}


static int arlan_open(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	int ret = 0;

	ARLAN_DEBUG_ENTRY("arlan_open");

	ret = request_irq(dev->irq, &arlan_interrupt, 0, dev->name, dev);
	if (ret)
	{
		printk(KERN_ERR "%s: unable to get IRQ %d .\n",
			dev->name, dev->irq);
		return ret;
	}


	priv->bad = 0;
	priv->lastReset = 0;
	priv->reset = 0;
	memcpy_fromio(dev->dev_addr, arlan->lanCardNodeId, 6);
	memset(dev->broadcast, 0xff, 6);
	dev->tx_queue_len = tx_queue_len;
	priv->interrupt_processing_active = 0;
	spin_lock_init(&priv->lock);

	netif_start_queue (dev);

	priv->registrationLostCount = 0;
	priv->registrationLastSeen = jiffies;
	priv->txLast = 0;
	priv->tx_command_given = 0;
	priv->rx_command_given = 0;
	
	priv->reRegisterExp = 1;
	priv->tx_last_sent = jiffies - 1;
	priv->tx_last_cleared = jiffies;
	priv->Conf->writeEEPROM = 0;
	priv->Conf->registrationInterrupts = 1;

	init_timer(&priv->timer);
	priv->timer.expires = jiffies + HZ / 10;
	priv->timer.data = (unsigned long) dev;
	priv->timer.function = &arlan_registration_timer;	/* timer handler */

	arlan_command(dev, ARLAN_COMMAND_POWERUP | ARLAN_COMMAND_LONG_WAIT_NOW);
	mdelay(200);
	add_timer(&priv->timer);

	ARLAN_DEBUG_EXIT("arlan_open");
	return 0;
}


static void arlan_tx_timeout (struct net_device *dev)
{
	printk(KERN_ERR "%s: arlan transmit timed out, kernel decided\n", dev->name);
	/* Try to restart the adaptor. */
	arlan_command(dev, ARLAN_COMMAND_CLEAN_AND_RESET);
	// dev->trans_start = jiffies;
	// netif_start_queue (dev);
}


static int arlan_tx(struct sk_buff *skb, struct net_device *dev)
{
	short length;
	unsigned char *buf;

	ARLAN_DEBUG_ENTRY("arlan_tx");
	
	length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	buf = skb->data;

	if (length + 0x12 > 0x800) {
		printk(KERN_ERR "TX RING overflow \n");
		netif_stop_queue (dev);
	}

	if (arlan_hw_tx(dev, buf, length) == -1)
		goto bad_end;

	dev->trans_start = jiffies;

	dev_kfree_skb(skb);

	arlan_process_interrupt(dev);
	ARLAN_DEBUG_EXIT("arlan_tx");
	return 0;

bad_end:
	arlan_process_interrupt(dev);
	netif_stop_queue (dev);
	ARLAN_DEBUG_EXIT("arlan_tx");
	return 1;
}


static inline int DoNotReTransmitCrap(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	if (TXLAST(dev).length < priv->Conf->ReTransmitPacketMaxSize)
		return 1;
	return 0;

}

static inline int DoNotWaitReTransmitCrap(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	if (TXLAST(dev).length < priv->Conf->waitReTransmitPacketMaxSize)
		return 1;
	return 0;
}

static inline void arlan_queue_retransmit(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	ARLAN_DEBUG_ENTRY("arlan_queue_retransmit");

	if (DoNotWaitReTransmitCrap(dev))
	{
		  arlan_drop_tx(dev);
	} else
		priv->ReTransmitRequested++;

	ARLAN_DEBUG_EXIT("arlan_queue_retransmit");
}

static inline void RetryOrFail(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	ARLAN_DEBUG_ENTRY("RetryOrFail");

	if (priv->retransmissions > priv->Conf->retries ||
	    DoNotReTransmitCrap(dev))
	{
		arlan_drop_tx(dev);
	}
	else if (priv->bad <= priv->Conf->fastReTransCount)
	{
		arlan_retransmit_now(dev);
	}
	else arlan_queue_retransmit(dev);

	ARLAN_DEBUG_EXIT("RetryOrFail");
}


static void arlan_tx_done_interrupt(struct net_device *dev, int status)
{
	struct arlan_private *priv = netdev_priv(dev);

	ARLAN_DEBUG_ENTRY("arlan_tx_done_interrupt");

	priv->tx_last_cleared = jiffies;
	priv->tx_command_given = 0;
	switch (status)
	{
		case 1:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit OK\n");
			priv->stats.tx_packets++;
			priv->bad = 0;
			priv->reset = 0;
			priv->retransmissions = 0;
			if (priv->Conf->tx_delay_ms)
			{
				priv->tx_done_delayed = jiffies + (priv->Conf->tx_delay_ms * HZ) / 1000 + 1;
			}
			else
			{
				TXLAST(dev).offset = 0;
				if (priv->txLast)
					priv->txLast = 0;
				else if (TXTAIL(dev).offset)
					priv->txLast = 1;
				if (TXLAST(dev).offset)
				{
					arlan_retransmit_now(dev);
					dev->trans_start = jiffies;
				}
				if (!TXHEAD(dev).offset || !TXTAIL(dev).offset)
				{
					netif_wake_queue (dev);
				}
			}
		}
		break;
		
		case 2:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit timed out\n");
			priv->bad += 1;
			//arlan_queue_retransmit(dev);
			RetryOrFail(dev);
		}
		break;

		case 3:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit max retries\n");
			priv->bad += 1;
			priv->reset = 0;
			//arlan_queue_retransmit(dev);
			RetryOrFail(dev);
		}
		break;
		
		case 4:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit aborted\n");
			priv->bad += 1;
			arlan_queue_retransmit(dev);
			//RetryOrFail(dev);
		}
		break;

		case 5:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit not registered\n");
			priv->bad += 1;
			//debug=101;
			arlan_queue_retransmit(dev);
		}
		break;

		case 6:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN) 
				printk("arlan intr: transmit destination full\n");
			priv->bad += 1;
			priv->reset = 0;
			//arlan_drop_tx(dev);
			arlan_queue_retransmit(dev);
		}
		break;

		case 7:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit unknown ack\n");
			priv->bad += 1;
			priv->reset = 0;
			arlan_queue_retransmit(dev);
		}
		break;
		
		case 8:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit dest mail box full\n");
			priv->bad += 1;
			priv->reset = 0;
			//arlan_drop_tx(dev);
			arlan_queue_retransmit(dev);
		}
		break;

		case 9:
		{
			IFDEBUG(ARLAN_DEBUG_TX_CHAIN)
				printk("arlan intr: transmit root dest not reg.\n");
			priv->bad += 1;
			priv->reset = 1;
			//arlan_drop_tx(dev);
			arlan_queue_retransmit(dev);
		}
		break;

		default:
		{
			printk(KERN_ERR "arlan intr: transmit status unknown\n");
			priv->bad += 1;
			priv->reset = 1;
			arlan_drop_tx(dev);
		}
	}

	ARLAN_DEBUG_EXIT("arlan_tx_done_interrupt");
}


static void arlan_rx_interrupt(struct net_device *dev, u_char rxStatus, u_short rxOffset, u_short pkt_len)
{
	char *skbtmp;
	int i = 0;

	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	struct arlan_conf_stru *conf = priv->Conf;


	ARLAN_DEBUG_ENTRY("arlan_rx_interrupt");
	// by spec,   not                WRITESHMB(arlan->rxStatus,0x00);
	// prohibited here              arlan_command(dev, ARLAN_COMMAND_RX);

	if (pkt_len < 10 || pkt_len > 2048)
	{
		printk(KERN_WARNING "%s: got too short or long packet, len %d \n", dev->name, pkt_len);
		return;
	}
	if (rxOffset + pkt_len > 0x2000)
	{
		printk("%s: got too long packet, len %d offset %x\n", dev->name, pkt_len, rxOffset);
		return;
	}
	priv->in_bytes += pkt_len;
	priv->in_bytes10 += pkt_len;
	if (conf->measure_rate < 1)
		conf->measure_rate = 1;
	if (time_after(jiffies, priv->in_time + conf->measure_rate * HZ))
	{
		conf->in_speed = priv->in_bytes / conf->measure_rate;
		priv->in_bytes = 0;
		priv->in_time = jiffies;
	}
	if (time_after(jiffies, priv->in_time10 + conf->measure_rate * 10*HZ))
	{
		conf->in_speed10 = priv->in_bytes10 / (10 * conf->measure_rate);
		priv->in_bytes10 = 0;
		priv->in_time10 = jiffies;
	}
	DEBUGSHM(1, "arlan rcv pkt rxStatus= %d ", arlan->rxStatus, u_char);
	switch (rxStatus)
	{
		case 1:
		case 2:
		case 3:
		{
			/* Malloc up new buffer. */
			struct sk_buff *skb;

			DEBUGSHM(50, "arlan recv pkt offs=%d\n", arlan->rxOffset, u_short);
			DEBUGSHM(1, "arlan rxFrmType = %d \n", arlan->rxFrmType, u_char);
			DEBUGSHM(1, KERN_INFO "arlan rx scrambled = %d \n", arlan->scrambled, u_char);

			/* here we do multicast filtering to avoid slow 8-bit memcopy */
#ifdef ARLAN_MULTICAST
			if (!(dev->flags & IFF_ALLMULTI) &&
				!(dev->flags & IFF_PROMISC) &&
				dev->mc_list)
			{
				char hw_dst_addr[6];
				struct dev_mc_list *dmi = dev->mc_list;
				int i;

				memcpy_fromio(hw_dst_addr, arlan->ultimateDestAddress, 6);
				if (hw_dst_addr[0] == 0x01)
				{
					if (mdebug)
						if (hw_dst_addr[1] == 0x00)
							printk(KERN_ERR "%s mcast 0x0100 \n", dev->name);
						else if (hw_dst_addr[1] == 0x40)
							printk(KERN_ERR "%s m/bcast 0x0140 \n", dev->name);
					while (dmi)
					{							if (dmi->dmi_addrlen == 6)
						{
							if (arlan_debug & ARLAN_DEBUG_HEADER_DUMP)
								printk(KERN_ERR "%s mcl %2x:%2x:%2x:%2x:%2x:%2x \n", dev->name,
										 dmi->dmi_addr[0], dmi->dmi_addr[1], dmi->dmi_addr[2],
										 dmi->dmi_addr[3], dmi->dmi_addr[4], dmi->dmi_addr[5]);
							for (i = 0; i < 6; i++)
								if (dmi->dmi_addr[i] != hw_dst_addr[i])
									break;
							if (i == 6)
								break;
						}
						else
							printk(KERN_ERR "%s: invalid multicast address length given.\n", dev->name);
						dmi = dmi->next;
					}
					/* we reach here if multicast filtering is on and packet 
					 * is multicast and not for receive */
					goto end_of_interrupt;
				}
			}
#endif				// ARLAN_MULTICAST
			/* multicast filtering ends here */
			pkt_len += ARLAN_FAKE_HDR_LEN;

			skb = dev_alloc_skb(pkt_len + 4);
			if (skb == NULL)
			{
				printk(KERN_ERR "%s: Memory squeeze, dropping packet.\n", dev->name);
				priv->stats.rx_dropped++;
				break;
			}
			skb_reserve(skb, 2);
			skb->dev = dev;
			skbtmp = skb_put(skb, pkt_len);

			memcpy_fromio(skbtmp + ARLAN_FAKE_HDR_LEN, ((char __iomem *) arlan) + rxOffset, pkt_len - ARLAN_FAKE_HDR_LEN);
			memcpy_fromio(skbtmp, arlan->ultimateDestAddress, 6);
			memcpy_fromio(skbtmp + 6, arlan->rxSrc, 6);
			WRITESHMB(arlan->rxStatus, 0x00);
			arlan_command(dev, ARLAN_COMMAND_RX);

			IFDEBUG(ARLAN_DEBUG_HEADER_DUMP)
			{
				char immedDestAddress[6];
				char immedSrcAddress[6];
				memcpy_fromio(immedDestAddress, arlan->immedDestAddress, 6);
				memcpy_fromio(immedSrcAddress, arlan->immedSrcAddress, 6);

				printk(KERN_WARNING "%s t %2x:%2x:%2x:%2x:%2x:%2x f %2x:%2x:%2x:%2x:%2x:%2x imd %2x:%2x:%2x:%2x:%2x:%2x ims %2x:%2x:%2x:%2x:%2x:%2x\n", dev->name,
					(unsigned char) skbtmp[0], (unsigned char) skbtmp[1], (unsigned char) skbtmp[2], (unsigned char) skbtmp[3],
					(unsigned char) skbtmp[4], (unsigned char) skbtmp[5], (unsigned char) skbtmp[6], (unsigned char) skbtmp[7],
					(unsigned char) skbtmp[8], (unsigned char) skbtmp[9], (unsigned char) skbtmp[10], (unsigned char) skbtmp[11],
					immedDestAddress[0], immedDestAddress[1], immedDestAddress[2],
					immedDestAddress[3], immedDestAddress[4], immedDestAddress[5],
					immedSrcAddress[0], immedSrcAddress[1], immedSrcAddress[2],
					immedSrcAddress[3], immedSrcAddress[4], immedSrcAddress[5]);
			}
			skb->protocol = eth_type_trans(skb, dev);
			IFDEBUG(ARLAN_DEBUG_HEADER_DUMP)
				if (skb->protocol != 0x608 && skb->protocol != 0x8)
				{
					for (i = 0; i <= 22; i++)
						printk("%02x:", (u_char) skbtmp[i + 12]);
					printk(KERN_ERR "\n");
					printk(KERN_WARNING "arlan kernel pkt type trans %x \n", skb->protocol);
				}
			netif_rx(skb);
			dev->last_rx = jiffies;
			priv->stats.rx_packets++;
			priv->stats.rx_bytes += pkt_len;
		}
		break;
		
		default:
			printk(KERN_ERR "arlan intr: received unknown status\n");
			priv->stats.rx_crc_errors++;
			break;
	}
	ARLAN_DEBUG_EXIT("arlan_rx_interrupt");
}

static void arlan_process_interrupt(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	u_char rxStatus = READSHMB(arlan->rxStatus);
	u_char txStatus = READSHMB(arlan->txStatus);
	u_short rxOffset = READSHMS(arlan->rxOffset);
	u_short pkt_len = READSHMS(arlan->rxLength);
	int interrupt_count = 0;

	ARLAN_DEBUG_ENTRY("arlan_process_interrupt");

	if (test_and_set_bit(0, (void *) &priv->interrupt_processing_active))
	{
		if (arlan_debug & ARLAN_DEBUG_CHAIN_LOCKS)
			printk(KERN_ERR "interrupt chain reentering \n");
		goto end_int_process;
	}
	while ((rxStatus || txStatus || priv->interrupt_ack_requested)
			&& (interrupt_count < 5))
	{
		if (rxStatus)
			priv->last_rx_int_ack_time = jiffies;

		arlan_command(dev, ARLAN_COMMAND_INT_ACK);
		arlan_command(dev, ARLAN_COMMAND_INT_ENABLE);
		
		IFDEBUG(ARLAN_DEBUG_INTERRUPT)
			printk(KERN_ERR "%s:  got IRQ rx %x tx %x comm %x rxOff %x rxLen %x \n",
					dev->name, rxStatus, txStatus, READSHMB(arlan->commandByte),
					rxOffset, pkt_len);

		if (rxStatus == 0 && txStatus == 0)
		{
			if (priv->irq_test_done)
			{
				if (!registrationBad(dev))
					IFDEBUG(ARLAN_DEBUG_INTERRUPT) printk(KERN_ERR "%s unknown interrupt(nop? regLost ?) reason tx %d rx %d ",
										    dev->name, txStatus, rxStatus);
			} else {
				IFDEBUG(ARLAN_DEBUG_INTERRUPT)
					printk(KERN_INFO "%s irq $%d test OK \n", dev->name, dev->irq);

			}
			priv->interrupt_ack_requested = 0;
			goto ends;
		}
		if (txStatus != 0)
		{
			WRITESHMB(arlan->txStatus, 0x00);
			arlan_tx_done_interrupt(dev, txStatus);
			goto ends;
		}
		if (rxStatus == 1 || rxStatus == 2)
		{		/* a packet waiting */
			arlan_rx_interrupt(dev, rxStatus, rxOffset, pkt_len);
			goto ends;
		}
		if (rxStatus > 2 && rxStatus < 0xff)
		{
			WRITESHMB(arlan->rxStatus, 0x00);
			printk(KERN_ERR "%s unknown rxStatus reason tx %d rx %d ",
				dev->name, txStatus, rxStatus);
			goto ends;
		}
		if (rxStatus == 0xff)
		{
			WRITESHMB(arlan->rxStatus, 0x00);
			arlan_command(dev, ARLAN_COMMAND_RX);
			if (registrationBad(dev))
				netif_device_detach(dev);
			if (!registrationBad(dev))
			{
				priv->registrationLastSeen = jiffies;
				if (!netif_queue_stopped(dev) && !priv->under_reset && !priv->under_config)
					netif_wake_queue (dev);
			}
			goto ends;
		}
ends:

		arlan_command_process(dev);

		rxStatus = READSHMB(arlan->rxStatus);
		txStatus = READSHMB(arlan->txStatus);
		rxOffset = READSHMS(arlan->rxOffset);
		pkt_len = READSHMS(arlan->rxLength);


		priv->irq_test_done = 1;

		interrupt_count++;
	}
	priv->interrupt_processing_active = 0;

end_int_process:
	arlan_command_process(dev);

	ARLAN_DEBUG_EXIT("arlan_process_interrupt");
	return;
}

static irqreturn_t arlan_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	u_char rxStatus = READSHMB(arlan->rxStatus);
	u_char txStatus = READSHMB(arlan->txStatus);

	ARLAN_DEBUG_ENTRY("arlan_interrupt");


	if (!rxStatus && !txStatus)
		priv->interrupt_ack_requested++;

	arlan_process_interrupt(dev);
	
	priv->irq_test_done = 1;

	ARLAN_DEBUG_EXIT("arlan_interrupt");
	return IRQ_HANDLED;

}


static int arlan_close(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);

	ARLAN_DEBUG_ENTRY("arlan_close");

	del_timer_sync(&priv->timer);

	arlan_command(dev, ARLAN_COMMAND_POWERDOWN);

	IFDEBUG(ARLAN_DEBUG_STARTUP)
		printk(KERN_NOTICE "%s: Closing device\n", dev->name);

	netif_stop_queue(dev);
	free_irq(dev->irq, dev);

	ARLAN_DEBUG_EXIT("arlan_close");
	return 0;
}

#ifdef ARLAN_DEBUGGING
static long alignLong(volatile u_char * ptr)
{
	long ret;
	memcpy_fromio(&ret, (void *) ptr, 4);
	return ret;
}
#endif

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */

static struct net_device_stats *arlan_statistics(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;


	ARLAN_DEBUG_ENTRY("arlan_statistics");

	/* Update the statistics from the device registers. */

	READSHM(priv->stats.collisions, arlan->numReTransmissions, u_int);
	READSHM(priv->stats.rx_crc_errors, arlan->numCRCErrors, u_int);
	READSHM(priv->stats.rx_dropped, arlan->numFramesDiscarded, u_int);
	READSHM(priv->stats.rx_fifo_errors, arlan->numRXBufferOverflows, u_int);
	READSHM(priv->stats.rx_frame_errors, arlan->numReceiveFramesLost, u_int);
	READSHM(priv->stats.rx_over_errors, arlan->numRXOverruns, u_int);
	READSHM(priv->stats.rx_packets, arlan->numDatagramsReceived, u_int);
	READSHM(priv->stats.tx_aborted_errors, arlan->numAbortErrors, u_int);
	READSHM(priv->stats.tx_carrier_errors, arlan->numStatusTimeouts, u_int);
	READSHM(priv->stats.tx_dropped, arlan->numDatagramsDiscarded, u_int);
	READSHM(priv->stats.tx_fifo_errors, arlan->numTXUnderruns, u_int);
	READSHM(priv->stats.tx_packets, arlan->numDatagramsTransmitted, u_int);
	READSHM(priv->stats.tx_window_errors, arlan->numHoldOffs, u_int);

	ARLAN_DEBUG_EXIT("arlan_statistics");

	return &priv->stats;
}


static void arlan_set_multicast(struct net_device *dev)
{
	struct arlan_private *priv = netdev_priv(dev);
	volatile struct arlan_shmem __iomem *arlan = priv->card;
	struct arlan_conf_stru *conf = priv->Conf;
	int board_conf_needed = 0;


	ARLAN_DEBUG_ENTRY("arlan_set_multicast");

	if (dev->flags & IFF_PROMISC)
	{
		unsigned char recMode;
		READSHM(recMode, arlan->receiveMode, u_char);
		conf->receiveMode = (ARLAN_RCV_PROMISC | ARLAN_RCV_CONTROL);
		if (conf->receiveMode != recMode)
			board_conf_needed = 1;
	}
	else
	{
		/* turn off promiscuous mode  */
		unsigned char recMode;
		READSHM(recMode, arlan->receiveMode, u_char);
		conf->receiveMode = ARLAN_RCV_CLEAN | ARLAN_RCV_CONTROL;
		if (conf->receiveMode != recMode)
			board_conf_needed = 1;
	}
	if (board_conf_needed)
		arlan_command(dev, ARLAN_COMMAND_CONF);

	ARLAN_DEBUG_EXIT("arlan_set_multicast");
}


struct net_device * __init arlan_probe(int unit)
{
	struct net_device *dev;
	int err;
	int m;

	ARLAN_DEBUG_ENTRY("arlan_probe");

	if (arlans_found == MAX_ARLANS)
		return ERR_PTR(-ENODEV);

	/* 
	 * Reserve space for local data and a copy of the shared memory
	 * that is used by the /proc interface.
	 */
	dev = alloc_etherdev(sizeof(struct arlan_private)
			     + sizeof(struct arlan_shmem));
	if (!dev)
		return ERR_PTR(-ENOMEM);

	SET_MODULE_OWNER(dev);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		
		if (dev->mem_start) {
			if (arlan_probe_here(dev, dev->mem_start) == 0)
				goto found;
			goto not_found;
		}
			
	}


	for (m = (int)phys_to_virt(lastFoundAt) + ARLAN_SHMEM_SIZE; 
	     m <= (int)phys_to_virt(0xDE000); 
	     m += ARLAN_SHMEM_SIZE)
	{
		if (arlan_probe_here(dev, m) == 0)
		{
			lastFoundAt = (int)virt_to_phys((void*)m);
			goto found;
		}
	}

	if (lastFoundAt == 0xbe000)
		printk(KERN_ERR "arlan: No Arlan devices found \n");

 not_found:
	free_netdev(dev);
	return ERR_PTR(-ENODEV);

 found:
	err = arlan_setup_device(dev, arlans_found);
	if (err)
		dev = ERR_PTR(err);
	else if (!arlans_found++)
		printk(KERN_INFO "Arlan driver %s\n", arlan_version);

	return dev;
}

#ifdef  MODULE
int init_module(void)
{
	int i = 0;

	ARLAN_DEBUG_ENTRY("init_module");

	if (channelSet != channelSetUNKNOWN || channelNumber != channelNumberUNKNOWN || systemId != systemIdUNKNOWN)
		return -EINVAL;

	for (i = 0; i < MAX_ARLANS; i++) {
		struct net_device *dev = arlan_probe(i);

		if (IS_ERR(dev)) 
			return PTR_ERR(dev);
	}
	init_arlan_proc();
	printk(KERN_INFO "Arlan driver %s\n", arlan_version);
	ARLAN_DEBUG_EXIT("init_module");
	return 0;
}


void cleanup_module(void)
{
	int i = 0;
	struct net_device *dev;

	ARLAN_DEBUG_ENTRY("cleanup_module");

	IFDEBUG(ARLAN_DEBUG_SHUTDOWN)
		printk(KERN_INFO "arlan: unloading module\n");

	cleanup_arlan_proc();

	for (i = 0; i < MAX_ARLANS; i++)
	{
		dev = arlan_device[i];
		if (dev) {
			arlan_command(dev, ARLAN_COMMAND_POWERDOWN );

			unregister_netdev(dev);
			release_mem_region(virt_to_phys((void *) dev->mem_start), 
					   ARLAN_SHMEM_SIZE);
			free_netdev(dev);
			arlan_device[i] = NULL;
		}
	}

	ARLAN_DEBUG_EXIT("cleanup_module");
}


#endif
MODULE_LICENSE("GPL");
