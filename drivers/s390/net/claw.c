/*
 *  drivers/s390/net/claw.c
 *    ESCON CLAW network driver
 *
 *  Linux for zSeries version
 *    Copyright IBM Corp. 2002, 2009
 *  Author(s) Original code written by:
 *		Kazuo Iimura <iimura@jp.ibm.com>
 *   	      Rewritten by
 *		Andy Richter <richtera@us.ibm.com>
 *		Marc Price <mwprice@us.ibm.com>
 *
 *    sysfs parms:
 *   group x.x.rrrr,x.x.wwww
 *   read_buffer nnnnnnn
 *   write_buffer nnnnnn
 *   host_name  aaaaaaaa
 *   adapter_name aaaaaaaa
 *   api_type    aaaaaaaa
 *
 *  eg.
 *   group  0.0.0200 0.0.0201
 *   read_buffer 25
 *   write_buffer 20
 *   host_name LINUX390
 *   adapter_name RS6K
 *   api_type     TCPIP
 *
 *  where
 *
 *   The device id is decided by the order entries
 *   are added to the group the first is claw0 the second claw1
 *   up to CLAW_MAX_DEV
 *
 *   rrrr     -	the first of 2 consecutive device addresses used for the
 *		CLAW protocol.
 *		The specified address is always used as the input (Read)
 *		channel and the next address is used as the output channel.
 *
 *   wwww     -	the second of 2 consecutive device addresses used for
 *		the CLAW protocol.
 *              The specified address is always used as the output
 *		channel and the previous address is used as the input channel.
 *
 *   read_buffer	-       specifies number of input buffers to allocate.
 *   write_buffer       -       specifies number of output buffers to allocate.
 *   host_name          -       host name
 *   adaptor_name       -       adaptor name
 *   api_type           -       API type TCPIP or API will be sent and expected
 *				as ws_name
 *
 *   Note the following requirements:
 *   1)  host_name must match the configured adapter_name on the remote side
 *   2)  adaptor_name must match the configured host name on the remote side
 *
 *  Change History
 *    1.00  Initial release shipped
 *    1.10  Changes for Buffer allocation
 *    1.15  Changed for 2.6 Kernel  No longer compiles on 2.4 or lower
 *    1.25  Added Packing support
 *    1.5
 */

#define KMSG_COMPONENT "claw"

#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <asm/debug.h>
#include <asm/idals.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tcp.h>
#include <linux/timer.h>
#include <linux/types.h>

#include "claw.h"

/*
   CLAW uses the s390dbf file system  see claw_trace and claw_setup
*/

static char version[] __initdata = "CLAW driver";
static char debug_buffer[255];
/**
 * Debug Facility Stuff
 */
static debug_info_t *claw_dbf_setup;
static debug_info_t *claw_dbf_trace;

/**
 *  CLAW Debug Facility functions
 */
static void
claw_unregister_debug_facility(void)
{
	if (claw_dbf_setup)
		debug_unregister(claw_dbf_setup);
	if (claw_dbf_trace)
		debug_unregister(claw_dbf_trace);
}

static int
claw_register_debug_facility(void)
{
	claw_dbf_setup = debug_register("claw_setup", 2, 1, 8);
	claw_dbf_trace = debug_register("claw_trace", 2, 2, 8);
	if (claw_dbf_setup == NULL || claw_dbf_trace == NULL) {
		claw_unregister_debug_facility();
		return -ENOMEM;
	}
	debug_register_view(claw_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(claw_dbf_setup, 2);
	debug_register_view(claw_dbf_trace, &debug_hex_ascii_view);
	debug_set_level(claw_dbf_trace, 2);
	return 0;
}

static inline void
claw_set_busy(struct net_device *dev)
{
 ((struct claw_privbk *)dev->ml_priv)->tbusy = 1;
}

static inline void
claw_clear_busy(struct net_device *dev)
{
	clear_bit(0, &(((struct claw_privbk *) dev->ml_priv)->tbusy));
	netif_wake_queue(dev);
}

static inline int
claw_check_busy(struct net_device *dev)
{
	return ((struct claw_privbk *) dev->ml_priv)->tbusy;
}

static inline void
claw_setbit_busy(int nr,struct net_device *dev)
{
	netif_stop_queue(dev);
	set_bit(nr, (void *)&(((struct claw_privbk *)dev->ml_priv)->tbusy));
}

static inline void
claw_clearbit_busy(int nr,struct net_device *dev)
{
	clear_bit(nr, (void *)&(((struct claw_privbk *)dev->ml_priv)->tbusy));
	netif_wake_queue(dev);
}

static inline int
claw_test_and_setbit_busy(int nr,struct net_device *dev)
{
	netif_stop_queue(dev);
	return test_and_set_bit(nr,
		(void *)&(((struct claw_privbk *) dev->ml_priv)->tbusy));
}


/* Functions for the DEV methods */

static int claw_probe(struct ccwgroup_device *cgdev);
static void claw_remove_device(struct ccwgroup_device *cgdev);
static void claw_purge_skb_queue(struct sk_buff_head *q);
static int claw_new_device(struct ccwgroup_device *cgdev);
static int claw_shutdown_device(struct ccwgroup_device *cgdev);
static int claw_tx(struct sk_buff *skb, struct net_device *dev);
static int claw_change_mtu( struct net_device *dev, int new_mtu);
static int claw_open(struct net_device *dev);
static void claw_irq_handler(struct ccw_device *cdev,
	unsigned long intparm, struct irb *irb);
static void claw_irq_tasklet ( unsigned long data );
static int claw_release(struct net_device *dev);
static void claw_write_retry ( struct chbk * p_ch );
static void claw_write_next ( struct chbk * p_ch );
static void claw_timer ( struct chbk * p_ch );

/* Functions */
static int add_claw_reads(struct net_device *dev,
	struct ccwbk* p_first, struct ccwbk* p_last);
static void ccw_check_return_code (struct ccw_device *cdev, int return_code);
static void ccw_check_unit_check (struct chbk * p_ch, unsigned char sense );
static int find_link(struct net_device *dev, char *host_name, char *ws_name );
static int claw_hw_tx(struct sk_buff *skb, struct net_device *dev, long linkid);
static int init_ccw_bk(struct net_device *dev);
static void probe_error( struct ccwgroup_device *cgdev);
static struct net_device_stats *claw_stats(struct net_device *dev);
static int pages_to_order_of_mag(int num_of_pages);
static struct sk_buff *claw_pack_skb(struct claw_privbk *privptr);
/* sysfs Functions */
static ssize_t claw_hname_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t claw_hname_write(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count);
static ssize_t claw_adname_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t claw_adname_write(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count);
static ssize_t claw_apname_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t claw_apname_write(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count);
static ssize_t claw_wbuff_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t claw_wbuff_write(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count);
static ssize_t claw_rbuff_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t claw_rbuff_write(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count);
static int claw_add_files(struct device *dev);
static void claw_remove_files(struct device *dev);

/*   Functions for System Validate  */
static int claw_process_control( struct net_device *dev, struct ccwbk * p_ccw);
static int claw_send_control(struct net_device *dev, __u8 type, __u8 link,
       __u8 correlator, __u8 rc , char *local_name, char *remote_name);
static int claw_snd_conn_req(struct net_device *dev, __u8 link);
static int claw_snd_disc(struct net_device *dev, struct clawctl * p_ctl);
static int claw_snd_sys_validate_rsp(struct net_device *dev,
        struct clawctl * p_ctl, __u32 return_code);
static int claw_strt_conn_req(struct net_device *dev );
static void claw_strt_read(struct net_device *dev, int lock);
static void claw_strt_out_IO(struct net_device *dev);
static void claw_free_wrt_buf(struct net_device *dev);

/* Functions for unpack reads   */
static void unpack_read(struct net_device *dev);

static int claw_pm_prepare(struct ccwgroup_device *gdev)
{
	return -EPERM;
}

/* the root device for claw group devices */
static struct device *claw_root_dev;

/* ccwgroup table  */

static struct ccwgroup_driver claw_group_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "claw",
	},
        .max_slaves  = 2,
        .driver_id   = 0xC3D3C1E6,
        .probe       = claw_probe,
        .remove      = claw_remove_device,
        .set_online  = claw_new_device,
        .set_offline = claw_shutdown_device,
	.prepare     = claw_pm_prepare,
};

static struct ccw_device_id claw_ids[] = {
	{CCW_DEVICE(0x3088, 0x61), .driver_info = claw_channel_type_claw},
	{},
};
MODULE_DEVICE_TABLE(ccw, claw_ids);

static struct ccw_driver claw_ccw_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "claw",
	},
	.ids	= claw_ids,
	.probe	= ccwgroup_probe_ccwdev,
	.remove	= ccwgroup_remove_ccwdev,
	.int_class = IOINT_CLW,
};

static ssize_t
claw_driver_group_store(struct device_driver *ddrv, const char *buf,
			size_t count)
{
	int err;
	err = ccwgroup_create_from_string(claw_root_dev,
					  claw_group_driver.driver_id,
					  &claw_ccw_driver, 2, buf);
	return err ? err : count;
}

static DRIVER_ATTR(group, 0200, NULL, claw_driver_group_store);

static struct attribute *claw_group_attrs[] = {
	&driver_attr_group.attr,
	NULL,
};

static struct attribute_group claw_group_attr_group = {
	.attrs = claw_group_attrs,
};

static const struct attribute_group *claw_group_attr_groups[] = {
	&claw_group_attr_group,
	NULL,
};

/*
*       Key functions
*/

/*----------------------------------------------------------------*
 *   claw_probe                                                   *
 *      this function is called for each CLAW device.             *
 *----------------------------------------------------------------*/
static int
claw_probe(struct ccwgroup_device *cgdev)
{
	int  		rc;
	struct claw_privbk *privptr=NULL;

	CLAW_DBF_TEXT(2, setup, "probe");
	if (!get_device(&cgdev->dev))
		return -ENODEV;
	privptr = kzalloc(sizeof(struct claw_privbk), GFP_KERNEL);
	dev_set_drvdata(&cgdev->dev, privptr);
	if (privptr == NULL) {
		probe_error(cgdev);
		put_device(&cgdev->dev);
		CLAW_DBF_TEXT_(2, setup, "probex%d", -ENOMEM);
		return -ENOMEM;
	}
	privptr->p_mtc_envelope= kzalloc( MAX_ENVELOPE_SIZE, GFP_KERNEL);
	privptr->p_env = kzalloc(sizeof(struct claw_env), GFP_KERNEL);
        if ((privptr->p_mtc_envelope==NULL) || (privptr->p_env==NULL)) {
                probe_error(cgdev);
		put_device(&cgdev->dev);
		CLAW_DBF_TEXT_(2, setup, "probex%d", -ENOMEM);
                return -ENOMEM;
        }
	memcpy(privptr->p_env->adapter_name,WS_NAME_NOT_DEF,8);
	memcpy(privptr->p_env->host_name,WS_NAME_NOT_DEF,8);
	memcpy(privptr->p_env->api_type,WS_NAME_NOT_DEF,8);
	privptr->p_env->packing = 0;
	privptr->p_env->write_buffers = 5;
	privptr->p_env->read_buffers = 5;
	privptr->p_env->read_size = CLAW_FRAME_SIZE;
	privptr->p_env->write_size = CLAW_FRAME_SIZE;
	rc = claw_add_files(&cgdev->dev);
	if (rc) {
		probe_error(cgdev);
		put_device(&cgdev->dev);
		dev_err(&cgdev->dev, "Creating the /proc files for a new"
		" CLAW device failed\n");
		CLAW_DBF_TEXT_(2, setup, "probex%d", rc);
		return rc;
	}
	privptr->p_env->p_priv = privptr;
        cgdev->cdev[0]->handler = claw_irq_handler;
	cgdev->cdev[1]->handler = claw_irq_handler;
	CLAW_DBF_TEXT(2, setup, "prbext 0");

        return 0;
}  /*  end of claw_probe       */

/*-------------------------------------------------------------------*
 *   claw_tx                                                         *
 *-------------------------------------------------------------------*/

static int
claw_tx(struct sk_buff *skb, struct net_device *dev)
{
        int             rc;
	struct claw_privbk *privptr = dev->ml_priv;
	unsigned long saveflags;
        struct chbk *p_ch;

	CLAW_DBF_TEXT(4, trace, "claw_tx");
	p_ch = &privptr->channel[WRITE_CHANNEL];
        spin_lock_irqsave(get_ccwdev_lock(p_ch->cdev), saveflags);
        rc=claw_hw_tx( skb, dev, 1 );
        spin_unlock_irqrestore(get_ccwdev_lock(p_ch->cdev), saveflags);
	CLAW_DBF_TEXT_(4, trace, "clawtx%d", rc);
	if (rc)
		rc = NETDEV_TX_BUSY;
	else
		rc = NETDEV_TX_OK;
        return rc;
}   /*  end of claw_tx */

/*------------------------------------------------------------------*
 *  pack the collect queue into an skb and return it                *
 *   If not packing just return the top skb from the queue          *
 *------------------------------------------------------------------*/

static struct sk_buff *
claw_pack_skb(struct claw_privbk *privptr)
{
	struct sk_buff *new_skb,*held_skb;
	struct chbk *p_ch = &privptr->channel[WRITE_CHANNEL];
	struct claw_env  *p_env = privptr->p_env;
	int	pkt_cnt,pk_ind,so_far;

	new_skb = NULL;		/* assume no dice */
	pkt_cnt = 0;
	CLAW_DBF_TEXT(4, trace, "PackSKBe");
	if (!skb_queue_empty(&p_ch->collect_queue)) {
	/* some data */
		held_skb = skb_dequeue(&p_ch->collect_queue);
		if (held_skb)
			dev_kfree_skb_any(held_skb);
		else
			return NULL;
		if (p_env->packing != DO_PACKED)
			return held_skb;
		/* get a new SKB we will pack at least one */
		new_skb = dev_alloc_skb(p_env->write_size);
		if (new_skb == NULL) {
			atomic_inc(&held_skb->users);
			skb_queue_head(&p_ch->collect_queue,held_skb);
			return NULL;
		}
		/* we have packed packet and a place to put it  */
		pk_ind = 1;
		so_far = 0;
		new_skb->cb[1] = 'P'; /* every skb on queue has pack header */
		while ((pk_ind) && (held_skb != NULL)) {
			if (held_skb->len+so_far <= p_env->write_size-8) {
				memcpy(skb_put(new_skb,held_skb->len),
					held_skb->data,held_skb->len);
				privptr->stats.tx_packets++;
				so_far += held_skb->len;
				pkt_cnt++;
				dev_kfree_skb_any(held_skb);
				held_skb = skb_dequeue(&p_ch->collect_queue);
				if (held_skb)
					atomic_dec(&held_skb->users);
			} else {
				pk_ind = 0;
				atomic_inc(&held_skb->users);
				skb_queue_head(&p_ch->collect_queue,held_skb);
			}
		}
	}
	CLAW_DBF_TEXT(4, trace, "PackSKBx");
	return new_skb;
}

/*-------------------------------------------------------------------*
 *   claw_change_mtu                                                 *
 *                                                                   *
 *-------------------------------------------------------------------*/

static int
claw_change_mtu(struct net_device *dev, int new_mtu)
{
	struct claw_privbk *privptr = dev->ml_priv;
	int buff_size;
	CLAW_DBF_TEXT(4, trace, "setmtu");
	buff_size = privptr->p_env->write_size;
        if ((new_mtu < 60) || (new_mtu > buff_size)) {
                return -EINVAL;
        }
        dev->mtu = new_mtu;
        return 0;
}  /*   end of claw_change_mtu */


/*-------------------------------------------------------------------*
 *   claw_open                                                       *
 *                                                                   *
 *-------------------------------------------------------------------*/
static int
claw_open(struct net_device *dev)
{

        int     rc;
        int     i;
        unsigned long       saveflags=0;
        unsigned long       parm;
        struct claw_privbk  *privptr;
	DECLARE_WAITQUEUE(wait, current);
        struct timer_list  timer;
        struct ccwbk *p_buf;

	CLAW_DBF_TEXT(4, trace, "open");
	privptr = (struct claw_privbk *)dev->ml_priv;
        /*   allocate and initialize CCW blocks */
	if (privptr->buffs_alloc == 0) {
	        rc=init_ccw_bk(dev);
        	if (rc) {
			CLAW_DBF_TEXT(2, trace, "openmem");
                	return -ENOMEM;
        	}
	}
        privptr->system_validate_comp=0;
        privptr->release_pend=0;
	if(strncmp(privptr->p_env->api_type,WS_APPL_NAME_PACKED,6) == 0) {
		privptr->p_env->read_size=DEF_PACK_BUFSIZE;
		privptr->p_env->write_size=DEF_PACK_BUFSIZE;
		privptr->p_env->packing=PACKING_ASK;
	} else {
		privptr->p_env->packing=0;
		privptr->p_env->read_size=CLAW_FRAME_SIZE;
		privptr->p_env->write_size=CLAW_FRAME_SIZE;
	}
        claw_set_busy(dev);
	tasklet_init(&privptr->channel[READ_CHANNEL].tasklet, claw_irq_tasklet,
		(unsigned long) &privptr->channel[READ_CHANNEL]);
        for ( i = 0; i < 2;  i++) {
		CLAW_DBF_TEXT_(2, trace, "opn_ch%d", i);
                init_waitqueue_head(&privptr->channel[i].wait);
		/* skb_queue_head_init(&p_ch->io_queue); */
		if (i == WRITE_CHANNEL)
			skb_queue_head_init(
				&privptr->channel[WRITE_CHANNEL].collect_queue);
                privptr->channel[i].flag_a = 0;
                privptr->channel[i].IO_active = 0;
                privptr->channel[i].flag  &= ~CLAW_TIMER;
                init_timer(&timer);
                timer.function = (void *)claw_timer;
                timer.data = (unsigned long)(&privptr->channel[i]);
                timer.expires = jiffies + 15*HZ;
                add_timer(&timer);
                spin_lock_irqsave(get_ccwdev_lock(
			privptr->channel[i].cdev), saveflags);
                parm = (unsigned long) &privptr->channel[i];
                privptr->channel[i].claw_state = CLAW_START_HALT_IO;
		rc = 0;
		add_wait_queue(&privptr->channel[i].wait, &wait);
                rc = ccw_device_halt(
			(struct ccw_device *)privptr->channel[i].cdev,parm);
                set_current_state(TASK_INTERRUPTIBLE);
                spin_unlock_irqrestore(
			get_ccwdev_lock(privptr->channel[i].cdev), saveflags);
                schedule();
		set_current_state(TASK_RUNNING);
                remove_wait_queue(&privptr->channel[i].wait, &wait);
                if(rc != 0)
                        ccw_check_return_code(privptr->channel[i].cdev, rc);
                if((privptr->channel[i].flag & CLAW_TIMER) == 0x00)
                        del_timer(&timer);
        }
	if ((((privptr->channel[READ_CHANNEL].last_dstat |
		privptr->channel[WRITE_CHANNEL].last_dstat) &
           ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) != 0x00) ||
	   (((privptr->channel[READ_CHANNEL].flag |
		privptr->channel[WRITE_CHANNEL].flag) & CLAW_TIMER) != 0x00)) {
		dev_info(&privptr->channel[READ_CHANNEL].cdev->dev,
			"%s: remote side is not ready\n", dev->name);
		CLAW_DBF_TEXT(2, trace, "notrdy");

                for ( i = 0; i < 2;  i++) {
                        spin_lock_irqsave(
				get_ccwdev_lock(privptr->channel[i].cdev),
				saveflags);
                        parm = (unsigned long) &privptr->channel[i];
                        privptr->channel[i].claw_state = CLAW_STOP;
                        rc = ccw_device_halt(
				(struct ccw_device *)&privptr->channel[i].cdev,
				parm);
                        spin_unlock_irqrestore(
				get_ccwdev_lock(privptr->channel[i].cdev),
				saveflags);
                        if (rc != 0) {
                                ccw_check_return_code(
					privptr->channel[i].cdev, rc);
                        }
                }
                free_pages((unsigned long)privptr->p_buff_ccw,
			(int)pages_to_order_of_mag(privptr->p_buff_ccw_num));
                if (privptr->p_env->read_size < PAGE_SIZE) {
                        free_pages((unsigned long)privptr->p_buff_read,
			       (int)pages_to_order_of_mag(
			       		privptr->p_buff_read_num));
                }
                else {
                        p_buf=privptr->p_read_active_first;
                        while (p_buf!=NULL) {
                                free_pages((unsigned long)p_buf->p_buffer,
				      (int)pages_to_order_of_mag(
				      	privptr->p_buff_pages_perread ));
                                p_buf=p_buf->next;
                        }
                }
                if (privptr->p_env->write_size < PAGE_SIZE ) {
                        free_pages((unsigned long)privptr->p_buff_write,
			     (int)pages_to_order_of_mag(
			     	privptr->p_buff_write_num));
                }
                else {
                        p_buf=privptr->p_write_active_first;
                        while (p_buf!=NULL) {
                                free_pages((unsigned long)p_buf->p_buffer,
				     (int)pages_to_order_of_mag(
				     	privptr->p_buff_pages_perwrite ));
                                p_buf=p_buf->next;
                        }
                }
		privptr->buffs_alloc = 0;
		privptr->channel[READ_CHANNEL].flag = 0x00;
		privptr->channel[WRITE_CHANNEL].flag = 0x00;
                privptr->p_buff_ccw=NULL;
                privptr->p_buff_read=NULL;
                privptr->p_buff_write=NULL;
                claw_clear_busy(dev);
		CLAW_DBF_TEXT(2, trace, "open EIO");
                return -EIO;
        }

        /*   Send SystemValidate command */

        claw_clear_busy(dev);
	CLAW_DBF_TEXT(4, trace, "openok");
        return 0;
}    /*     end of claw_open    */

/*-------------------------------------------------------------------*
*                                                                    *
*       claw_irq_handler                                             *
*                                                                    *
*--------------------------------------------------------------------*/
static void
claw_irq_handler(struct ccw_device *cdev,
	unsigned long intparm, struct irb *irb)
{
        struct chbk *p_ch = NULL;
        struct claw_privbk *privptr = NULL;
        struct net_device *dev = NULL;
        struct claw_env  *p_env;
        struct chbk *p_ch_r=NULL;

	CLAW_DBF_TEXT(4, trace, "clawirq");
        /* Bypass all 'unsolicited interrupts' */
	privptr = dev_get_drvdata(&cdev->dev);
	if (!privptr) {
		dev_warn(&cdev->dev, "An uninitialized CLAW device received an"
			" IRQ, c-%02x d-%02x\n",
			irb->scsw.cmd.cstat, irb->scsw.cmd.dstat);
		CLAW_DBF_TEXT(2, trace, "badirq");
                return;
        }

	/* Try to extract channel from driver data. */
	if (privptr->channel[READ_CHANNEL].cdev == cdev)
		p_ch = &privptr->channel[READ_CHANNEL];
	else if (privptr->channel[WRITE_CHANNEL].cdev == cdev)
		p_ch = &privptr->channel[WRITE_CHANNEL];
	else {
		dev_warn(&cdev->dev, "The device is not a CLAW device\n");
		CLAW_DBF_TEXT(2, trace, "badchan");
		return;
	}
	CLAW_DBF_TEXT_(4, trace, "IRQCH=%d", p_ch->flag);

	dev = (struct net_device *) (p_ch->ndev);
        p_env=privptr->p_env;

	/* Copy interruption response block. */
	memcpy(p_ch->irb, irb, sizeof(struct irb));

	/* Check for good subchannel return code, otherwise info message */
	if (irb->scsw.cmd.cstat && !(irb->scsw.cmd.cstat & SCHN_STAT_PCI)) {
		dev_info(&cdev->dev,
			"%s: subchannel check for device: %04x -"
			" Sch Stat %02x  Dev Stat %02x CPA - %04x\n",
                        dev->name, p_ch->devno,
			irb->scsw.cmd.cstat, irb->scsw.cmd.dstat,
			irb->scsw.cmd.cpa);
		CLAW_DBF_TEXT(2, trace, "chanchk");
                /* return; */
        }

        /* Check the reason-code of a unit check */
	if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK)
                ccw_check_unit_check(p_ch, irb->ecw[0]);

        /* State machine to bring the connection up, down and to restart */
	p_ch->last_dstat = irb->scsw.cmd.dstat;

        switch (p_ch->claw_state) {
	case CLAW_STOP:/* HALT_IO by claw_release (halt sequence) */
		if (!((p_ch->irb->scsw.cmd.stctl & SCSW_STCTL_SEC_STATUS) ||
		(p_ch->irb->scsw.cmd.stctl == SCSW_STCTL_STATUS_PEND) ||
		(p_ch->irb->scsw.cmd.stctl ==
		(SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND))))
			return;
		wake_up(&p_ch->wait);   /* wake up claw_release */
		CLAW_DBF_TEXT(4, trace, "stop");
		return;
	case CLAW_START_HALT_IO: /* HALT_IO issued by claw_open  */
		if (!((p_ch->irb->scsw.cmd.stctl & SCSW_STCTL_SEC_STATUS) ||
		(p_ch->irb->scsw.cmd.stctl == SCSW_STCTL_STATUS_PEND) ||
		(p_ch->irb->scsw.cmd.stctl ==
		(SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND)))) {
			CLAW_DBF_TEXT(4, trace, "haltio");
			return;
		}
		if (p_ch->flag == CLAW_READ) {
			p_ch->claw_state = CLAW_START_READ;
			wake_up(&p_ch->wait); /* wake claw_open (READ)*/
		} else if (p_ch->flag == CLAW_WRITE) {
			p_ch->claw_state = CLAW_START_WRITE;
			/*      send SYSTEM_VALIDATE                    */
			claw_strt_read(dev, LOCK_NO);
			claw_send_control(dev,
				SYSTEM_VALIDATE_REQUEST,
				0, 0, 0,
				p_env->host_name,
				p_env->adapter_name);
		} else {
			dev_warn(&cdev->dev, "The CLAW device received"
				" an unexpected IRQ, "
				"c-%02x d-%02x\n",
				irb->scsw.cmd.cstat,
				irb->scsw.cmd.dstat);
			return;
			}
		CLAW_DBF_TEXT(4, trace, "haltio");
		return;
	case CLAW_START_READ:
		CLAW_DBF_TEXT(4, trace, "ReadIRQ");
		if (p_ch->irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK) {
			clear_bit(0, (void *)&p_ch->IO_active);
			if ((p_ch->irb->ecw[0] & 0x41) == 0x41 ||
			    (p_ch->irb->ecw[0] & 0x40) == 0x40 ||
			    (p_ch->irb->ecw[0])        == 0) {
				privptr->stats.rx_errors++;
				dev_info(&cdev->dev,
					"%s: Restart is required after remote "
					"side recovers \n",
					dev->name);
			}
			CLAW_DBF_TEXT(4, trace, "notrdy");
			return;
		}
		if ((p_ch->irb->scsw.cmd.cstat & SCHN_STAT_PCI) &&
			(p_ch->irb->scsw.cmd.dstat == 0)) {
			if (test_and_set_bit(CLAW_BH_ACTIVE,
				(void *)&p_ch->flag_a) == 0)
				tasklet_schedule(&p_ch->tasklet);
			else
				CLAW_DBF_TEXT(4, trace, "PCINoBH");
			CLAW_DBF_TEXT(4, trace, "PCI_read");
			return;
		}
		if (!((p_ch->irb->scsw.cmd.stctl & SCSW_STCTL_SEC_STATUS) ||
		 (p_ch->irb->scsw.cmd.stctl == SCSW_STCTL_STATUS_PEND) ||
		 (p_ch->irb->scsw.cmd.stctl ==
		 (SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND)))) {
			CLAW_DBF_TEXT(4, trace, "SPend_rd");
			return;
		}
		clear_bit(0, (void *)&p_ch->IO_active);
		claw_clearbit_busy(TB_RETRY, dev);
		if (test_and_set_bit(CLAW_BH_ACTIVE,
			(void *)&p_ch->flag_a) == 0)
			tasklet_schedule(&p_ch->tasklet);
		else
			CLAW_DBF_TEXT(4, trace, "RdBHAct");
		CLAW_DBF_TEXT(4, trace, "RdIRQXit");
		return;
	case CLAW_START_WRITE:
		if (p_ch->irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK) {
			dev_info(&cdev->dev,
				"%s: Unit Check Occurred in "
				"write channel\n", dev->name);
			clear_bit(0, (void *)&p_ch->IO_active);
			if (p_ch->irb->ecw[0] & 0x80) {
				dev_info(&cdev->dev,
					"%s: Resetting Event "
					"occurred:\n", dev->name);
				init_timer(&p_ch->timer);
				p_ch->timer.function =
					(void *)claw_write_retry;
				p_ch->timer.data = (unsigned long)p_ch;
				p_ch->timer.expires = jiffies + 10*HZ;
				add_timer(&p_ch->timer);
				dev_info(&cdev->dev,
					"%s: write connection "
					"restarting\n", dev->name);
			}
			CLAW_DBF_TEXT(4, trace, "rstrtwrt");
			return;
		}
		if (p_ch->irb->scsw.cmd.dstat & DEV_STAT_UNIT_EXCEP) {
			clear_bit(0, (void *)&p_ch->IO_active);
			dev_info(&cdev->dev,
				"%s: Unit Exception "
				"occurred in write channel\n",
				dev->name);
		}
		if (!((p_ch->irb->scsw.cmd.stctl & SCSW_STCTL_SEC_STATUS) ||
		(p_ch->irb->scsw.cmd.stctl == SCSW_STCTL_STATUS_PEND) ||
		(p_ch->irb->scsw.cmd.stctl ==
		(SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND)))) {
			CLAW_DBF_TEXT(4, trace, "writeUE");
			return;
		}
		clear_bit(0, (void *)&p_ch->IO_active);
		if (claw_test_and_setbit_busy(TB_TX, dev) == 0) {
			claw_write_next(p_ch);
			claw_clearbit_busy(TB_TX, dev);
			claw_clear_busy(dev);
		}
		p_ch_r = (struct chbk *)&privptr->channel[READ_CHANNEL];
		if (test_and_set_bit(CLAW_BH_ACTIVE,
			(void *)&p_ch_r->flag_a) == 0)
			tasklet_schedule(&p_ch_r->tasklet);
		CLAW_DBF_TEXT(4, trace, "StWtExit");
		return;
	default:
		dev_warn(&cdev->dev,
			"The CLAW device for %s received an unexpected IRQ\n",
			 dev->name);
		CLAW_DBF_TEXT(2, trace, "badIRQ");
		return;
        }

}       /*   end of claw_irq_handler    */


/*-------------------------------------------------------------------*
*       claw_irq_tasklet                                             *
*                                                                    *
*--------------------------------------------------------------------*/
static void
claw_irq_tasklet ( unsigned long data )
{
	struct chbk * p_ch;
        struct net_device  *dev;

	p_ch = (struct chbk *) data;
        dev = (struct net_device *)p_ch->ndev;
	CLAW_DBF_TEXT(4, trace, "IRQtask");
        unpack_read(dev);
        clear_bit(CLAW_BH_ACTIVE, (void *)&p_ch->flag_a);
	CLAW_DBF_TEXT(4, trace, "TskletXt");
        return;
}       /*    end of claw_irq_bh    */

/*-------------------------------------------------------------------*
*       claw_release                                                 *
*                                                                    *
*--------------------------------------------------------------------*/
static int
claw_release(struct net_device *dev)
{
        int                rc;
        int                i;
        unsigned long      saveflags;
        unsigned long      parm;
        struct claw_privbk *privptr;
        DECLARE_WAITQUEUE(wait, current);
        struct ccwbk*             p_this_ccw;
        struct ccwbk*             p_buf;

	if (!dev)
                return 0;
	privptr = (struct claw_privbk *)dev->ml_priv;
        if (!privptr)
                return 0;
	CLAW_DBF_TEXT(4, trace, "release");
        privptr->release_pend=1;
        claw_setbit_busy(TB_STOP,dev);
        for ( i = 1; i >=0 ;  i--) {
                spin_lock_irqsave(
			get_ccwdev_lock(privptr->channel[i].cdev), saveflags);
	     /*   del_timer(&privptr->channel[READ_CHANNEL].timer);  */
 		privptr->channel[i].claw_state = CLAW_STOP;
                privptr->channel[i].IO_active = 0;
                parm = (unsigned long) &privptr->channel[i];
		if (i == WRITE_CHANNEL)
			claw_purge_skb_queue(
				&privptr->channel[WRITE_CHANNEL].collect_queue);
                rc = ccw_device_halt (privptr->channel[i].cdev, parm);
	        if (privptr->system_validate_comp==0x00)  /* never opened? */
                   init_waitqueue_head(&privptr->channel[i].wait);
                add_wait_queue(&privptr->channel[i].wait, &wait);
                set_current_state(TASK_INTERRUPTIBLE);
	        spin_unlock_irqrestore(
			get_ccwdev_lock(privptr->channel[i].cdev), saveflags);
	        schedule();
		set_current_state(TASK_RUNNING);
	        remove_wait_queue(&privptr->channel[i].wait, &wait);
	        if (rc != 0) {
                        ccw_check_return_code(privptr->channel[i].cdev, rc);
                }
        }
	if (privptr->pk_skb != NULL) {
		dev_kfree_skb_any(privptr->pk_skb);
		privptr->pk_skb = NULL;
	}
	if(privptr->buffs_alloc != 1) {
		CLAW_DBF_TEXT(4, trace, "none2fre");
		return 0;
	}
	CLAW_DBF_TEXT(4, trace, "freebufs");
	if (privptr->p_buff_ccw != NULL) {
        	free_pages((unsigned long)privptr->p_buff_ccw,
	        	(int)pages_to_order_of_mag(privptr->p_buff_ccw_num));
	}
	CLAW_DBF_TEXT(4, trace, "freeread");
        if (privptr->p_env->read_size < PAGE_SIZE) {
	    if (privptr->p_buff_read != NULL) {
                free_pages((unsigned long)privptr->p_buff_read,
		      (int)pages_to_order_of_mag(privptr->p_buff_read_num));
		}
        }
        else {
                p_buf=privptr->p_read_active_first;
                while (p_buf!=NULL) {
                        free_pages((unsigned long)p_buf->p_buffer,
			     (int)pages_to_order_of_mag(
			     	privptr->p_buff_pages_perread ));
                        p_buf=p_buf->next;
                }
        }
	 CLAW_DBF_TEXT(4, trace, "freewrit");
        if (privptr->p_env->write_size < PAGE_SIZE ) {
                free_pages((unsigned long)privptr->p_buff_write,
		      (int)pages_to_order_of_mag(privptr->p_buff_write_num));
        }
        else {
                p_buf=privptr->p_write_active_first;
                while (p_buf!=NULL) {
                        free_pages((unsigned long)p_buf->p_buffer,
			      (int)pages_to_order_of_mag(
			      privptr->p_buff_pages_perwrite ));
                        p_buf=p_buf->next;
                }
        }
	 CLAW_DBF_TEXT(4, trace, "clearptr");
	privptr->buffs_alloc = 0;
        privptr->p_buff_ccw=NULL;
        privptr->p_buff_read=NULL;
        privptr->p_buff_write=NULL;
        privptr->system_validate_comp=0;
        privptr->release_pend=0;
        /*      Remove any writes that were pending and reset all reads   */
        p_this_ccw=privptr->p_read_active_first;
        while (p_this_ccw!=NULL) {
                p_this_ccw->header.length=0xffff;
                p_this_ccw->header.opcode=0xff;
                p_this_ccw->header.flag=0x00;
                p_this_ccw=p_this_ccw->next;
        }

        while (privptr->p_write_active_first!=NULL) {
                p_this_ccw=privptr->p_write_active_first;
                p_this_ccw->header.flag=CLAW_PENDING;
                privptr->p_write_active_first=p_this_ccw->next;
                p_this_ccw->next=privptr->p_write_free_chain;
                privptr->p_write_free_chain=p_this_ccw;
                ++privptr->write_free_count;
        }
        privptr->p_write_active_last=NULL;
        privptr->mtc_logical_link = -1;
        privptr->mtc_skipping = 1;
        privptr->mtc_offset=0;

	if (((privptr->channel[READ_CHANNEL].last_dstat |
		privptr->channel[WRITE_CHANNEL].last_dstat) &
		~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) != 0x00) {
		dev_warn(&privptr->channel[READ_CHANNEL].cdev->dev,
			"Deactivating %s completed with incorrect"
			" subchannel status "
			"(read %02x, write %02x)\n",
                dev->name,
		privptr->channel[READ_CHANNEL].last_dstat,
		privptr->channel[WRITE_CHANNEL].last_dstat);
		 CLAW_DBF_TEXT(2, trace, "badclose");
        }
	CLAW_DBF_TEXT(4, trace, "rlsexit");
        return 0;
}      /* end of claw_release     */

/*-------------------------------------------------------------------*
*       claw_write_retry                                             *
*                                                                    *
*--------------------------------------------------------------------*/

static void
claw_write_retry ( struct chbk *p_ch )
{

        struct net_device  *dev=p_ch->ndev;

	CLAW_DBF_TEXT(4, trace, "w_retry");
        if (p_ch->claw_state == CLAW_STOP) {
        	return;
        }
	claw_strt_out_IO( dev );
	CLAW_DBF_TEXT(4, trace, "rtry_xit");
        return;
}      /* end of claw_write_retry      */


/*-------------------------------------------------------------------*
*       claw_write_next                                              *
*                                                                    *
*--------------------------------------------------------------------*/

static void
claw_write_next ( struct chbk * p_ch )
{

        struct net_device  *dev;
        struct claw_privbk *privptr=NULL;
	struct sk_buff *pk_skb;

	CLAW_DBF_TEXT(4, trace, "claw_wrt");
        if (p_ch->claw_state == CLAW_STOP)
                return;
        dev = (struct net_device *) p_ch->ndev;
	privptr = (struct claw_privbk *) dev->ml_priv;
        claw_free_wrt_buf( dev );
	if ((privptr->write_free_count > 0) &&
	    !skb_queue_empty(&p_ch->collect_queue)) {
	  	pk_skb = claw_pack_skb(privptr);
		while (pk_skb != NULL) {
			claw_hw_tx(pk_skb, dev, 1);
			if (privptr->write_free_count > 0) {
	   			pk_skb = claw_pack_skb(privptr);
			} else
				pk_skb = NULL;
		}
	}
        if (privptr->p_write_active_first!=NULL) {
                claw_strt_out_IO(dev);
        }
        return;
}      /* end of claw_write_next      */

/*-------------------------------------------------------------------*
*                                                                    *
*       claw_timer                                                   *
*--------------------------------------------------------------------*/

static void
claw_timer ( struct chbk * p_ch )
{
	CLAW_DBF_TEXT(4, trace, "timer");
        p_ch->flag |= CLAW_TIMER;
        wake_up(&p_ch->wait);
        return;
}      /* end of claw_timer  */

/*
*
*       functions
*/


/*-------------------------------------------------------------------*
*                                                                    *
*     pages_to_order_of_mag                                          *
*                                                                    *
*    takes a number of pages from 1 to 512 and returns the           *
*    log(num_pages)/log(2) get_free_pages() needs a base 2 order     *
*    of magnitude get_free_pages() has an upper order of 9           *
*--------------------------------------------------------------------*/

static int
pages_to_order_of_mag(int num_of_pages)
{
	int	order_of_mag=1;		/* assume 2 pages */
	int	nump;

	CLAW_DBF_TEXT_(5, trace, "pages%d", num_of_pages);
	if (num_of_pages == 1)   {return 0; }  /* magnitude of 0 = 1 page */
	/* 512 pages = 2Meg on 4k page systems */
	if (num_of_pages >= 512) {return 9; }
	/* we have two or more pages order is at least 1 */
	for (nump=2 ;nump <= 512;nump*=2) {
	  if (num_of_pages <= nump)
		  break;
	  order_of_mag +=1;
	}
	if (order_of_mag > 9) { order_of_mag = 9; }  /* I know it's paranoid */
	CLAW_DBF_TEXT_(5, trace, "mag%d", order_of_mag);
	return order_of_mag;
}

/*-------------------------------------------------------------------*
*                                                                    *
*     add_claw_reads                                                 *
*                                                                    *
*--------------------------------------------------------------------*/
static int
add_claw_reads(struct net_device *dev, struct ccwbk* p_first,
	struct ccwbk* p_last)
{
        struct claw_privbk *privptr;
        struct ccw1  temp_ccw;
        struct endccw * p_end;
	CLAW_DBF_TEXT(4, trace, "addreads");
	privptr = dev->ml_priv;
        p_end = privptr->p_end_ccw;

        /* first CCW and last CCW contains a new set of read channel programs
        *       to apend the running channel programs
        */
        if ( p_first==NULL) {
		CLAW_DBF_TEXT(4, trace, "addexit");
                return 0;
        }

        /* set up ending CCW sequence for this segment */
        if (p_end->read1) {
                p_end->read1=0x00;    /*  second ending CCW is now active */
                /*      reset ending CCWs and setup TIC CCWs              */
                p_end->read2_nop2.cmd_code = CCW_CLAW_CMD_READFF;
                p_end->read2_nop2.flags  = CCW_FLAG_SLI | CCW_FLAG_SKIP;
                p_last->r_TIC_1.cda =(__u32)__pa(&p_end->read2_nop1);
                p_last->r_TIC_2.cda =(__u32)__pa(&p_end->read2_nop1);
                p_end->read2_nop2.cda=0;
                p_end->read2_nop2.count=1;
        }
        else {
                p_end->read1=0x01;  /* first ending CCW is now active */
                /*      reset ending CCWs and setup TIC CCWs          */
                p_end->read1_nop2.cmd_code = CCW_CLAW_CMD_READFF;
                p_end->read1_nop2.flags  = CCW_FLAG_SLI | CCW_FLAG_SKIP;
                p_last->r_TIC_1.cda = (__u32)__pa(&p_end->read1_nop1);
                p_last->r_TIC_2.cda = (__u32)__pa(&p_end->read1_nop1);
                p_end->read1_nop2.cda=0;
                p_end->read1_nop2.count=1;
        }

        if ( privptr-> p_read_active_first ==NULL ) {
		privptr->p_read_active_first = p_first;  /*  set new first */
		privptr->p_read_active_last  = p_last;   /*  set new last  */
        }
        else {

                /* set up TIC ccw  */
                temp_ccw.cda= (__u32)__pa(&p_first->read);
                temp_ccw.count=0;
                temp_ccw.flags=0;
                temp_ccw.cmd_code = CCW_CLAW_CMD_TIC;


                if (p_end->read1) {

               /* first set of CCW's is chained to the new read              */
               /* chain, so the second set is chained to the active chain.   */
               /* Therefore modify the second set to point to the new        */
               /* read chain set up TIC CCWs                                 */
               /* make sure we update the CCW so channel doesn't fetch it    */
               /* when it's only half done                                   */
                        memcpy( &p_end->read2_nop2, &temp_ccw ,
				sizeof(struct ccw1));
                        privptr->p_read_active_last->r_TIC_1.cda=
				(__u32)__pa(&p_first->read);
                        privptr->p_read_active_last->r_TIC_2.cda=
				(__u32)__pa(&p_first->read);
                }
                else {
                        /* make sure we update the CCW so channel doesn't   */
			/* fetch it when it is only half done               */
                        memcpy( &p_end->read1_nop2, &temp_ccw ,
				sizeof(struct ccw1));
                        privptr->p_read_active_last->r_TIC_1.cda=
				(__u32)__pa(&p_first->read);
                        privptr->p_read_active_last->r_TIC_2.cda=
				(__u32)__pa(&p_first->read);
                }
		/*      chain in new set of blocks                         */
                privptr->p_read_active_last->next = p_first;
                privptr->p_read_active_last=p_last;
        } /* end of if ( privptr-> p_read_active_first ==NULL)  */
	CLAW_DBF_TEXT(4, trace, "addexit");
        return 0;
}    /*     end of add_claw_reads   */

/*-------------------------------------------------------------------*
 *   ccw_check_return_code                                           *
 *                                                                   *
 *-------------------------------------------------------------------*/

static void
ccw_check_return_code(struct ccw_device *cdev, int return_code)
{
	CLAW_DBF_TEXT(4, trace, "ccwret");
        if (return_code != 0) {
                switch (return_code) {
		case -EBUSY: /* BUSY is a transient state no action needed */
			break;
		case -ENODEV:
			dev_err(&cdev->dev, "The remote channel adapter is not"
				" available\n");
			break;
		case -EINVAL:
			dev_err(&cdev->dev,
				"The status of the remote channel adapter"
				" is not valid\n");
			break;
		default:
			dev_err(&cdev->dev, "The common device layer"
				" returned error code %d\n",
				  return_code);
		}
	}
	CLAW_DBF_TEXT(4, trace, "ccwret");
}    /*    end of ccw_check_return_code   */

/*-------------------------------------------------------------------*
*       ccw_check_unit_check                                         *
*--------------------------------------------------------------------*/

static void
ccw_check_unit_check(struct chbk * p_ch, unsigned char sense )
{
	struct net_device *ndev = p_ch->ndev;
	struct device *dev = &p_ch->cdev->dev;

	CLAW_DBF_TEXT(4, trace, "unitchek");
	dev_warn(dev, "The communication peer of %s disconnected\n",
		ndev->name);

	if (sense & 0x40) {
		if (sense & 0x01) {
			dev_warn(dev, "The remote channel adapter for"
				" %s has been reset\n",
				ndev->name);
		}
	} else if (sense & 0x20) {
		if (sense & 0x04) {
			dev_warn(dev, "A data streaming timeout occurred"
				" for %s\n",
				ndev->name);
		} else if (sense & 0x10) {
			dev_warn(dev, "The remote channel adapter for %s"
				" is faulty\n",
				ndev->name);
		} else {
			dev_warn(dev, "A data transfer parity error occurred"
				" for %s\n",
				ndev->name);
		}
	} else if (sense & 0x10) {
		dev_warn(dev, "A read data parity error occurred"
			" for %s\n",
			ndev->name);
	}

}   /*    end of ccw_check_unit_check    */

/*-------------------------------------------------------------------*
*               find_link                                            *
*--------------------------------------------------------------------*/
static int
find_link(struct net_device *dev, char *host_name, char *ws_name )
{
	struct claw_privbk *privptr;
	struct claw_env *p_env;
	int    rc=0;

	CLAW_DBF_TEXT(2, setup, "findlink");
	privptr = dev->ml_priv;
        p_env=privptr->p_env;
	switch (p_env->packing)
	{
		case  PACKING_ASK:
			if ((memcmp(WS_APPL_NAME_PACKED, host_name, 8)!=0) ||
			    (memcmp(WS_APPL_NAME_PACKED, ws_name, 8)!=0 ))
        	             rc = EINVAL;
			break;
		case  DO_PACKED:
		case  PACK_SEND:
			if ((memcmp(WS_APPL_NAME_IP_NAME, host_name, 8)!=0) ||
			    (memcmp(WS_APPL_NAME_IP_NAME, ws_name, 8)!=0 ))
        	        	rc = EINVAL;
			break;
		default:
	       		if ((memcmp(HOST_APPL_NAME, host_name, 8)!=0) ||
		    	    (memcmp(p_env->api_type , ws_name, 8)!=0))
        	        	rc = EINVAL;
			break;
	}

	return rc;
}    /*    end of find_link    */

/*-------------------------------------------------------------------*
 *   claw_hw_tx                                                      *
 *                                                                   *
 *                                                                   *
 *-------------------------------------------------------------------*/

static int
claw_hw_tx(struct sk_buff *skb, struct net_device *dev, long linkid)
{
        int                             rc=0;
        struct claw_privbk 		*privptr;
        struct ccwbk           *p_this_ccw;
        struct ccwbk           *p_first_ccw;
        struct ccwbk           *p_last_ccw;
        __u32                           numBuffers;
        signed long                     len_of_data;
        unsigned long                   bytesInThisBuffer;
        unsigned char                   *pDataAddress;
        struct endccw                   *pEnd;
        struct ccw1                     tempCCW;
	struct claw_env			*p_env;
	struct clawph			*pk_head;
	struct chbk			*ch;

	CLAW_DBF_TEXT(4, trace, "hw_tx");
	privptr = (struct claw_privbk *)(dev->ml_priv);
	p_env =privptr->p_env;
	claw_free_wrt_buf(dev);	/* Clean up free chain if posible */
        /*  scan the write queue to free any completed write packets   */
        p_first_ccw=NULL;
        p_last_ccw=NULL;
	if ((p_env->packing >= PACK_SEND) &&
       	    (skb->cb[1] != 'P')) {
		skb_push(skb,sizeof(struct clawph));
		pk_head=(struct clawph *)skb->data;
		pk_head->len=skb->len-sizeof(struct clawph);
		if (pk_head->len%4)  {
			pk_head->len+= 4-(pk_head->len%4);
			skb_pad(skb,4-(pk_head->len%4));
			skb_put(skb,4-(pk_head->len%4));
		}
		if (p_env->packing == DO_PACKED)
			pk_head->link_num = linkid;
		else
			pk_head->link_num = 0;
		pk_head->flag = 0x00;
		skb_pad(skb,4);
		skb->cb[1] = 'P';
	}
        if (linkid == 0) {
        	if (claw_check_busy(dev)) {
                	if (privptr->write_free_count!=0) {
                                claw_clear_busy(dev);
                        }
                        else {
                                claw_strt_out_IO(dev );
                                claw_free_wrt_buf( dev );
                                if (privptr->write_free_count==0) {
					ch = &privptr->channel[WRITE_CHANNEL];
					atomic_inc(&skb->users);
					skb_queue_tail(&ch->collect_queue, skb);
                                	goto Done;
                                }
                                else {
                                	claw_clear_busy(dev);
                                }
                        }
                }
                /*  tx lock  */
                if (claw_test_and_setbit_busy(TB_TX,dev)) { /* set to busy */
			ch = &privptr->channel[WRITE_CHANNEL];
			atomic_inc(&skb->users);
			skb_queue_tail(&ch->collect_queue, skb);
                        claw_strt_out_IO(dev );
                        rc=-EBUSY;
                        goto Done2;
                }
        }
        /*      See how many write buffers are required to hold this data */
	numBuffers = DIV_ROUND_UP(skb->len, privptr->p_env->write_size);

        /*      If that number of buffers isn't available, give up for now */
        if (privptr->write_free_count < numBuffers ||
            privptr->p_write_free_chain == NULL ) {

                claw_setbit_busy(TB_NOBUFFER,dev);
		ch = &privptr->channel[WRITE_CHANNEL];
		atomic_inc(&skb->users);
		skb_queue_tail(&ch->collect_queue, skb);
		CLAW_DBF_TEXT(2, trace, "clawbusy");
                goto Done2;
        }
        pDataAddress=skb->data;
        len_of_data=skb->len;

        while (len_of_data > 0) {
                p_this_ccw=privptr->p_write_free_chain;  /* get a block */
		if (p_this_ccw == NULL) { /* lost the race */
			ch = &privptr->channel[WRITE_CHANNEL];
			atomic_inc(&skb->users);
			skb_queue_tail(&ch->collect_queue, skb);
			goto Done2;
		}
                privptr->p_write_free_chain=p_this_ccw->next;
                p_this_ccw->next=NULL;
                --privptr->write_free_count; /* -1 */
		if (len_of_data >= privptr->p_env->write_size)
			bytesInThisBuffer = privptr->p_env->write_size;
		else
			bytesInThisBuffer = len_of_data;
                memcpy( p_this_ccw->p_buffer,pDataAddress, bytesInThisBuffer);
                len_of_data-=bytesInThisBuffer;
                pDataAddress+=(unsigned long)bytesInThisBuffer;
                /*      setup write CCW         */
                p_this_ccw->write.cmd_code = (linkid * 8) +1;
                if (len_of_data>0) {
                        p_this_ccw->write.cmd_code+=MORE_to_COME_FLAG;
                }
                p_this_ccw->write.count=bytesInThisBuffer;
                /*      now add to end of this chain    */
                if (p_first_ccw==NULL)    {
                        p_first_ccw=p_this_ccw;
                }
                if (p_last_ccw!=NULL) {
                        p_last_ccw->next=p_this_ccw;
                        /*      set up TIC ccws         */
                        p_last_ccw->w_TIC_1.cda=
				(__u32)__pa(&p_this_ccw->write);
                }
                p_last_ccw=p_this_ccw;      /* save new last block */
        }

        /*      FirstCCW and LastCCW now contain a new set of write channel
        *       programs to append to the running channel program
        */

        if (p_first_ccw!=NULL) {
		/*      setup ending ccw sequence for this segment           */
                pEnd=privptr->p_end_ccw;
                if (pEnd->write1) {
                        pEnd->write1=0x00;   /* second end ccw is now active */
                        /*      set up Tic CCWs         */
                        p_last_ccw->w_TIC_1.cda=
				(__u32)__pa(&pEnd->write2_nop1);
                        pEnd->write2_nop2.cmd_code = CCW_CLAW_CMD_READFF;
                        pEnd->write2_nop2.flags    =
				CCW_FLAG_SLI | CCW_FLAG_SKIP;
                        pEnd->write2_nop2.cda=0;
                        pEnd->write2_nop2.count=1;
                }
                else {  /*  end of if (pEnd->write1)*/
                        pEnd->write1=0x01;   /* first end ccw is now active */
                        /*      set up Tic CCWs         */
                        p_last_ccw->w_TIC_1.cda=
				(__u32)__pa(&pEnd->write1_nop1);
                        pEnd->write1_nop2.cmd_code = CCW_CLAW_CMD_READFF;
                        pEnd->write1_nop2.flags    =
				CCW_FLAG_SLI | CCW_FLAG_SKIP;
                        pEnd->write1_nop2.cda=0;
                        pEnd->write1_nop2.count=1;
                }  /* end if if (pEnd->write1) */

                if (privptr->p_write_active_first==NULL ) {
                        privptr->p_write_active_first=p_first_ccw;
                        privptr->p_write_active_last=p_last_ccw;
                }
                else {
                        /*      set up Tic CCWs         */

                        tempCCW.cda=(__u32)__pa(&p_first_ccw->write);
                        tempCCW.count=0;
                        tempCCW.flags=0;
                        tempCCW.cmd_code=CCW_CLAW_CMD_TIC;

                        if (pEnd->write1) {

                 /*
                 * first set of ending CCW's is chained to the new write
                 * chain, so the second set is chained to the active chain
                 * Therefore modify the second set to point the new write chain.
                 * make sure we update the CCW atomically
                 * so channel does not fetch it when it's only half done
                 */
                                memcpy( &pEnd->write2_nop2, &tempCCW ,
					sizeof(struct ccw1));
                                privptr->p_write_active_last->w_TIC_1.cda=
					(__u32)__pa(&p_first_ccw->write);
                        }
                        else {

                        /*make sure we update the CCW atomically
                         *so channel does not fetch it when it's only half done
                         */
                                memcpy(&pEnd->write1_nop2, &tempCCW ,
					sizeof(struct ccw1));
                                privptr->p_write_active_last->w_TIC_1.cda=
				        (__u32)__pa(&p_first_ccw->write);

                        } /* end if if (pEnd->write1) */

                        privptr->p_write_active_last->next=p_first_ccw;
                        privptr->p_write_active_last=p_last_ccw;
                }

        } /* endif (p_first_ccw!=NULL)  */
        dev_kfree_skb_any(skb);
        claw_strt_out_IO(dev );
        /*      if write free count is zero , set NOBUFFER       */
	if (privptr->write_free_count==0) {
		claw_setbit_busy(TB_NOBUFFER,dev);
        }
Done2:
	claw_clearbit_busy(TB_TX,dev);
Done:
	return(rc);
}    /*    end of claw_hw_tx    */

/*-------------------------------------------------------------------*
*                                                                    *
*     init_ccw_bk                                                    *
*                                                                    *
*--------------------------------------------------------------------*/

static int
init_ccw_bk(struct net_device *dev)
{

        __u32   ccw_blocks_required;
        __u32   ccw_blocks_perpage;
        __u32   ccw_pages_required;
        __u32   claw_reads_perpage=1;
        __u32   claw_read_pages;
        __u32   claw_writes_perpage=1;
        __u32   claw_write_pages;
        void    *p_buff=NULL;
        struct ccwbk*p_free_chain;
	struct ccwbk*p_buf;
	struct ccwbk*p_last_CCWB;
	struct ccwbk*p_first_CCWB;
        struct endccw *p_endccw=NULL;
	addr_t  real_address;
	struct claw_privbk *privptr = dev->ml_priv;
        struct clawh *pClawH=NULL;
        addr_t   real_TIC_address;
        int i,j;
	CLAW_DBF_TEXT(4, trace, "init_ccw");

        /*  initialize  statistics field */
        privptr->active_link_ID=0;
        /*  initialize  ccwbk pointers  */
        privptr->p_write_free_chain=NULL;   /* pointer to free ccw chain*/
        privptr->p_write_active_first=NULL; /* pointer to the first write ccw*/
        privptr->p_write_active_last=NULL;  /* pointer to the last write ccw*/
        privptr->p_read_active_first=NULL;  /* pointer to the first read ccw*/
        privptr->p_read_active_last=NULL;   /* pointer to the last read ccw */
        privptr->p_end_ccw=NULL;            /* pointer to ending ccw        */
        privptr->p_claw_signal_blk=NULL;    /* pointer to signal block      */
	privptr->buffs_alloc = 0;
        memset(&privptr->end_ccw, 0x00, sizeof(struct endccw));
        memset(&privptr->ctl_bk, 0x00, sizeof(struct clawctl));
        /*  initialize  free write ccwbk counter  */
        privptr->write_free_count=0;  /* number of free bufs on write chain */
        p_last_CCWB = NULL;
        p_first_CCWB= NULL;
        /*
        *  We need 1 CCW block for each read buffer, 1 for each
        *  write buffer, plus 1 for ClawSignalBlock
        */
        ccw_blocks_required =
		privptr->p_env->read_buffers+privptr->p_env->write_buffers+1;
        /*
        * compute number of CCW blocks that will fit in a page
        */
        ccw_blocks_perpage= PAGE_SIZE /  CCWBK_SIZE;
        ccw_pages_required=
		DIV_ROUND_UP(ccw_blocks_required, ccw_blocks_perpage);

        /*
         *  read and write sizes are set by 2 constants in claw.h
	 *  4k and 32k.  Unpacked values other than 4k are not going to
	 * provide good performance. With packing buffers support 32k
	 * buffers are used.
         */
	if (privptr->p_env->read_size < PAGE_SIZE) {
		claw_reads_perpage = PAGE_SIZE / privptr->p_env->read_size;
		claw_read_pages = DIV_ROUND_UP(privptr->p_env->read_buffers,
						claw_reads_perpage);
         }
         else {       /* > or equal  */
		privptr->p_buff_pages_perread =
			DIV_ROUND_UP(privptr->p_env->read_size, PAGE_SIZE);
		claw_read_pages = privptr->p_env->read_buffers *
					privptr->p_buff_pages_perread;
         }
        if (privptr->p_env->write_size < PAGE_SIZE) {
		claw_writes_perpage =
			PAGE_SIZE / privptr->p_env->write_size;
		claw_write_pages = DIV_ROUND_UP(privptr->p_env->write_buffers,
						claw_writes_perpage);

        }
        else {      /* >  or equal  */
		privptr->p_buff_pages_perwrite =
			DIV_ROUND_UP(privptr->p_env->read_size, PAGE_SIZE);
		claw_write_pages = privptr->p_env->write_buffers *
					privptr->p_buff_pages_perwrite;
        }
        /*
        *               allocate ccw_pages_required
        */
        if (privptr->p_buff_ccw==NULL) {
                privptr->p_buff_ccw=
			(void *)__get_free_pages(__GFP_DMA,
		        (int)pages_to_order_of_mag(ccw_pages_required ));
                if (privptr->p_buff_ccw==NULL) {
                        return -ENOMEM;
                }
                privptr->p_buff_ccw_num=ccw_pages_required;
        }
        memset(privptr->p_buff_ccw, 0x00,
		privptr->p_buff_ccw_num * PAGE_SIZE);

        /*
        *               obtain ending ccw block address
        *
        */
        privptr->p_end_ccw = (struct endccw *)&privptr->end_ccw;
        real_address  = (__u32)__pa(privptr->p_end_ccw);
        /*                              Initialize ending CCW block       */
        p_endccw=privptr->p_end_ccw;
        p_endccw->real=real_address;
        p_endccw->write1=0x00;
        p_endccw->read1=0x00;

        /*      write1_nop1                                     */
        p_endccw->write1_nop1.cmd_code = CCW_CLAW_CMD_NOP;
        p_endccw->write1_nop1.flags       = CCW_FLAG_SLI | CCW_FLAG_CC;
        p_endccw->write1_nop1.count       = 1;
        p_endccw->write1_nop1.cda         = 0;

        /*      write1_nop2                                     */
        p_endccw->write1_nop2.cmd_code = CCW_CLAW_CMD_READFF;
        p_endccw->write1_nop2.flags        = CCW_FLAG_SLI | CCW_FLAG_SKIP;
        p_endccw->write1_nop2.count      = 1;
        p_endccw->write1_nop2.cda        = 0;

        /*      write2_nop1                                     */
        p_endccw->write2_nop1.cmd_code = CCW_CLAW_CMD_NOP;
        p_endccw->write2_nop1.flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        p_endccw->write2_nop1.count        = 1;
        p_endccw->write2_nop1.cda          = 0;

        /*      write2_nop2                                     */
        p_endccw->write2_nop2.cmd_code = CCW_CLAW_CMD_READFF;
        p_endccw->write2_nop2.flags        = CCW_FLAG_SLI | CCW_FLAG_SKIP;
        p_endccw->write2_nop2.count        = 1;
        p_endccw->write2_nop2.cda          = 0;

        /*      read1_nop1                                      */
        p_endccw->read1_nop1.cmd_code = CCW_CLAW_CMD_NOP;
        p_endccw->read1_nop1.flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        p_endccw->read1_nop1.count        = 1;
        p_endccw->read1_nop1.cda          = 0;

        /*      read1_nop2                                      */
        p_endccw->read1_nop2.cmd_code = CCW_CLAW_CMD_READFF;
        p_endccw->read1_nop2.flags        = CCW_FLAG_SLI | CCW_FLAG_SKIP;
        p_endccw->read1_nop2.count        = 1;
        p_endccw->read1_nop2.cda          = 0;

        /*      read2_nop1                                      */
        p_endccw->read2_nop1.cmd_code = CCW_CLAW_CMD_NOP;
        p_endccw->read2_nop1.flags        = CCW_FLAG_SLI | CCW_FLAG_CC;
        p_endccw->read2_nop1.count        = 1;
        p_endccw->read2_nop1.cda          = 0;

        /*      read2_nop2                                      */
        p_endccw->read2_nop2.cmd_code = CCW_CLAW_CMD_READFF;
        p_endccw->read2_nop2.flags        = CCW_FLAG_SLI | CCW_FLAG_SKIP;
        p_endccw->read2_nop2.count        = 1;
        p_endccw->read2_nop2.cda          = 0;

        /*
        *                               Build a chain of CCWs
        *
        */
        p_buff=privptr->p_buff_ccw;

        p_free_chain=NULL;
        for (i=0 ; i < ccw_pages_required; i++ ) {
                real_address  = (__u32)__pa(p_buff);
                p_buf=p_buff;
                for (j=0 ; j < ccw_blocks_perpage ; j++) {
                        p_buf->next  = p_free_chain;
                        p_free_chain = p_buf;
                        p_buf->real=(__u32)__pa(p_buf);
                        ++p_buf;
                }
                p_buff+=PAGE_SIZE;
        }
        /*
        *                               Initialize ClawSignalBlock
        *
        */
        if (privptr->p_claw_signal_blk==NULL) {
                privptr->p_claw_signal_blk=p_free_chain;
                p_free_chain=p_free_chain->next;
                pClawH=(struct clawh *)privptr->p_claw_signal_blk;
                pClawH->length=0xffff;
                pClawH->opcode=0xff;
                pClawH->flag=CLAW_BUSY;
        }

        /*
        *               allocate write_pages_required and add to free chain
        */
        if (privptr->p_buff_write==NULL) {
            if (privptr->p_env->write_size < PAGE_SIZE) {
                privptr->p_buff_write=
			(void *)__get_free_pages(__GFP_DMA,
			(int)pages_to_order_of_mag(claw_write_pages ));
                if (privptr->p_buff_write==NULL) {
                        privptr->p_buff_ccw=NULL;
                        return -ENOMEM;
                }
                /*
                *                               Build CLAW write free chain
                *
                */

                memset(privptr->p_buff_write, 0x00,
			ccw_pages_required * PAGE_SIZE);
                privptr->p_write_free_chain=NULL;

                p_buff=privptr->p_buff_write;

                for (i=0 ; i< privptr->p_env->write_buffers ; i++) {
                        p_buf        = p_free_chain;      /*  get a CCW */
                        p_free_chain = p_buf->next;
                        p_buf->next  =privptr->p_write_free_chain;
                        privptr->p_write_free_chain = p_buf;
                        p_buf-> p_buffer	= (struct clawbuf *)p_buff;
                        p_buf-> write.cda       = (__u32)__pa(p_buff);
                        p_buf-> write.flags     = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> w_read_FF.cmd_code = CCW_CLAW_CMD_READFF;
                        p_buf-> w_read_FF.flags   = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> w_read_FF.count   = 1;
                        p_buf-> w_read_FF.cda     =
				(__u32)__pa(&p_buf-> header.flag);
                        p_buf-> w_TIC_1.cmd_code = CCW_CLAW_CMD_TIC;
                        p_buf-> w_TIC_1.flags      = 0;
                        p_buf-> w_TIC_1.count      = 0;

			if (((unsigned long)p_buff +
					    privptr->p_env->write_size) >=
			   ((unsigned long)(p_buff+2*
			    (privptr->p_env->write_size) - 1) & PAGE_MASK)) {
				p_buff = p_buff+privptr->p_env->write_size;
                        }
                }
           }
           else      /*  Buffers are => PAGE_SIZE. 1 buff per get_free_pages */
           {
               privptr->p_write_free_chain=NULL;
               for (i = 0; i< privptr->p_env->write_buffers ; i++) {
                   p_buff=(void *)__get_free_pages(__GFP_DMA,
		        (int)pages_to_order_of_mag(
			privptr->p_buff_pages_perwrite) );
                   if (p_buff==NULL) {
                        free_pages((unsigned long)privptr->p_buff_ccw,
			      (int)pages_to_order_of_mag(
			      		privptr->p_buff_ccw_num));
                        privptr->p_buff_ccw=NULL;
			p_buf=privptr->p_buff_write;
                        while (p_buf!=NULL) {
                                free_pages((unsigned long)
					p_buf->p_buffer,
					(int)pages_to_order_of_mag(
					privptr->p_buff_pages_perwrite));
                                p_buf=p_buf->next;
                        }
                        return -ENOMEM;
                   }  /* Error on get_pages   */
                   memset(p_buff, 0x00, privptr->p_env->write_size );
                   p_buf         = p_free_chain;
                   p_free_chain  = p_buf->next;
                   p_buf->next   = privptr->p_write_free_chain;
                   privptr->p_write_free_chain = p_buf;
                   privptr->p_buff_write = p_buf;
                   p_buf->p_buffer=(struct clawbuf *)p_buff;
                   p_buf-> write.cda     = (__u32)__pa(p_buff);
                   p_buf-> write.flags   = CCW_FLAG_SLI | CCW_FLAG_CC;
                   p_buf-> w_read_FF.cmd_code = CCW_CLAW_CMD_READFF;
                   p_buf-> w_read_FF.flags    = CCW_FLAG_SLI | CCW_FLAG_CC;
                   p_buf-> w_read_FF.count    = 1;
                   p_buf-> w_read_FF.cda      =
			(__u32)__pa(&p_buf-> header.flag);
                   p_buf-> w_TIC_1.cmd_code = CCW_CLAW_CMD_TIC;
                   p_buf-> w_TIC_1.flags   = 0;
                   p_buf-> w_TIC_1.count   = 0;
               }  /* for all write_buffers   */

           }    /* else buffers are PAGE_SIZE or bigger */

        }
        privptr->p_buff_write_num=claw_write_pages;
        privptr->write_free_count=privptr->p_env->write_buffers;


        /*
        *               allocate read_pages_required and chain to free chain
        */
        if (privptr->p_buff_read==NULL) {
            if (privptr->p_env->read_size < PAGE_SIZE)  {
                privptr->p_buff_read=
			(void *)__get_free_pages(__GFP_DMA,
			(int)pages_to_order_of_mag(claw_read_pages) );
                if (privptr->p_buff_read==NULL) {
                        free_pages((unsigned long)privptr->p_buff_ccw,
				(int)pages_to_order_of_mag(
					privptr->p_buff_ccw_num));
			/* free the write pages size is < page size  */
                        free_pages((unsigned long)privptr->p_buff_write,
				(int)pages_to_order_of_mag(
				privptr->p_buff_write_num));
                        privptr->p_buff_ccw=NULL;
                        privptr->p_buff_write=NULL;
                        return -ENOMEM;
                }
                memset(privptr->p_buff_read, 0x00, claw_read_pages * PAGE_SIZE);
                privptr->p_buff_read_num=claw_read_pages;
                /*
                *                               Build CLAW read free chain
                *
                */
                p_buff=privptr->p_buff_read;
                for (i=0 ; i< privptr->p_env->read_buffers ; i++) {
                        p_buf        = p_free_chain;
                        p_free_chain = p_buf->next;

                        if (p_last_CCWB==NULL) {
                                p_buf->next=NULL;
                                real_TIC_address=0;
                                p_last_CCWB=p_buf;
                        }
                        else {
                                p_buf->next=p_first_CCWB;
                                real_TIC_address=
				(__u32)__pa(&p_first_CCWB -> read );
                        }

                        p_first_CCWB=p_buf;

                        p_buf->p_buffer=(struct clawbuf *)p_buff;
                        /*  initialize read command */
                        p_buf-> read.cmd_code = CCW_CLAW_CMD_READ;
                        p_buf-> read.cda = (__u32)__pa(p_buff);
                        p_buf-> read.flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> read.count       = privptr->p_env->read_size;

                        /*  initialize read_h command */
                        p_buf-> read_h.cmd_code = CCW_CLAW_CMD_READHEADER;
                        p_buf-> read_h.cda =
				(__u32)__pa(&(p_buf->header));
                        p_buf-> read_h.flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> read_h.count      = sizeof(struct clawh);

                        /*  initialize Signal command */
                        p_buf-> signal.cmd_code = CCW_CLAW_CMD_SIGNAL_SMOD;
                        p_buf-> signal.cda =
				(__u32)__pa(&(pClawH->flag));
                        p_buf-> signal.flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> signal.count     = 1;

                        /*  initialize r_TIC_1 command */
                        p_buf-> r_TIC_1.cmd_code = CCW_CLAW_CMD_TIC;
                        p_buf-> r_TIC_1.cda = (__u32)real_TIC_address;
                        p_buf-> r_TIC_1.flags = 0;
                        p_buf-> r_TIC_1.count      = 0;

                        /*  initialize r_read_FF command */
                        p_buf-> r_read_FF.cmd_code = CCW_CLAW_CMD_READFF;
                        p_buf-> r_read_FF.cda =
				(__u32)__pa(&(pClawH->flag));
                        p_buf-> r_read_FF.flags =
				CCW_FLAG_SLI | CCW_FLAG_CC | CCW_FLAG_PCI;
                        p_buf-> r_read_FF.count    = 1;

                        /*    initialize r_TIC_2          */
                        memcpy(&p_buf->r_TIC_2,
				&p_buf->r_TIC_1, sizeof(struct ccw1));

                        /*     initialize Header     */
                        p_buf->header.length=0xffff;
                        p_buf->header.opcode=0xff;
                        p_buf->header.flag=CLAW_PENDING;

			if (((unsigned long)p_buff+privptr->p_env->read_size) >=
			  ((unsigned long)(p_buff+2*(privptr->p_env->read_size)
				 -1)
			   & PAGE_MASK)) {
                                p_buff= p_buff+privptr->p_env->read_size;
                        }
                        else {
                                p_buff=
				(void *)((unsigned long)
					(p_buff+2*(privptr->p_env->read_size)-1)
					 & PAGE_MASK) ;
                        }
                }   /* for read_buffers   */
          }         /* read_size < PAGE_SIZE  */
          else {  /* read Size >= PAGE_SIZE  */
                for (i=0 ; i< privptr->p_env->read_buffers ; i++) {
                        p_buff = (void *)__get_free_pages(__GFP_DMA,
				(int)pages_to_order_of_mag(
					privptr->p_buff_pages_perread));
                        if (p_buff==NULL) {
                                free_pages((unsigned long)privptr->p_buff_ccw,
					(int)pages_to_order_of_mag(privptr->
					p_buff_ccw_num));
				/* free the write pages  */
	                        p_buf=privptr->p_buff_write;
                                while (p_buf!=NULL) {
					free_pages(
					    (unsigned long)p_buf->p_buffer,
					    (int)pages_to_order_of_mag(
					    privptr->p_buff_pages_perwrite));
                                        p_buf=p_buf->next;
                                }
				/* free any read pages already alloc  */
	                        p_buf=privptr->p_buff_read;
                                while (p_buf!=NULL) {
					free_pages(
					    (unsigned long)p_buf->p_buffer,
					    (int)pages_to_order_of_mag(
					     privptr->p_buff_pages_perread));
                                        p_buf=p_buf->next;
                                }
                                privptr->p_buff_ccw=NULL;
                                privptr->p_buff_write=NULL;
                                return -ENOMEM;
                        }
                        memset(p_buff, 0x00, privptr->p_env->read_size);
                        p_buf        = p_free_chain;
                        privptr->p_buff_read = p_buf;
                        p_free_chain = p_buf->next;

                        if (p_last_CCWB==NULL) {
                                p_buf->next=NULL;
                                real_TIC_address=0;
                                p_last_CCWB=p_buf;
                        }
                        else {
                                p_buf->next=p_first_CCWB;
                                real_TIC_address=
					(addr_t)__pa(
						&p_first_CCWB -> read );
                        }

                        p_first_CCWB=p_buf;
				/* save buff address */
                        p_buf->p_buffer=(struct clawbuf *)p_buff;
                        /*  initialize read command */
                        p_buf-> read.cmd_code = CCW_CLAW_CMD_READ;
                        p_buf-> read.cda = (__u32)__pa(p_buff);
                        p_buf-> read.flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> read.count       = privptr->p_env->read_size;

                        /*  initialize read_h command */
                        p_buf-> read_h.cmd_code = CCW_CLAW_CMD_READHEADER;
                        p_buf-> read_h.cda =
				(__u32)__pa(&(p_buf->header));
                        p_buf-> read_h.flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> read_h.count      = sizeof(struct clawh);

                        /*  initialize Signal command */
                        p_buf-> signal.cmd_code = CCW_CLAW_CMD_SIGNAL_SMOD;
                        p_buf-> signal.cda =
				(__u32)__pa(&(pClawH->flag));
                        p_buf-> signal.flags = CCW_FLAG_SLI | CCW_FLAG_CC;
                        p_buf-> signal.count     = 1;

                        /*  initialize r_TIC_1 command */
                        p_buf-> r_TIC_1.cmd_code = CCW_CLAW_CMD_TIC;
                        p_buf-> r_TIC_1.cda = (__u32)real_TIC_address;
                        p_buf-> r_TIC_1.flags = 0;
                        p_buf-> r_TIC_1.count      = 0;

                        /*  initialize r_read_FF command */
                        p_buf-> r_read_FF.cmd_code = CCW_CLAW_CMD_READFF;
                        p_buf-> r_read_FF.cda =
				(__u32)__pa(&(pClawH->flag));
                        p_buf-> r_read_FF.flags =
				CCW_FLAG_SLI | CCW_FLAG_CC | CCW_FLAG_PCI;
                        p_buf-> r_read_FF.count    = 1;

                        /*    initialize r_TIC_2          */
                        memcpy(&p_buf->r_TIC_2, &p_buf->r_TIC_1,
				sizeof(struct ccw1));

                        /*     initialize Header     */
                        p_buf->header.length=0xffff;
                        p_buf->header.opcode=0xff;
                        p_buf->header.flag=CLAW_PENDING;

                }    /* For read_buffers   */
          }     /*  read_size >= PAGE_SIZE   */
        }       /*  pBuffread = NULL */
        add_claw_reads( dev  ,p_first_CCWB , p_last_CCWB);
	privptr->buffs_alloc = 1;

        return 0;
}    /*    end of init_ccw_bk */

/*-------------------------------------------------------------------*
*                                                                    *
*       probe_error                                                  *
*                                                                    *
*--------------------------------------------------------------------*/

static void
probe_error( struct ccwgroup_device *cgdev)
{
	struct claw_privbk *privptr;

	CLAW_DBF_TEXT(4, trace, "proberr");
	privptr = dev_get_drvdata(&cgdev->dev);
	if (privptr != NULL) {
		dev_set_drvdata(&cgdev->dev, NULL);
		kfree(privptr->p_env);
		kfree(privptr->p_mtc_envelope);
		kfree(privptr);
	}
}    /*    probe_error    */

/*-------------------------------------------------------------------*
*    claw_process_control                                            *
*                                                                    *
*                                                                    *
*--------------------------------------------------------------------*/

static int
claw_process_control( struct net_device *dev, struct ccwbk * p_ccw)
{

        struct clawbuf *p_buf;
        struct clawctl  ctlbk;
        struct clawctl *p_ctlbk;
        char    temp_host_name[8];
        char    temp_ws_name[8];
        struct claw_privbk *privptr;
        struct claw_env *p_env;
        struct sysval *p_sysval;
        struct conncmd *p_connect=NULL;
        int rc;
        struct chbk *p_ch = NULL;
	struct device *tdev;
	CLAW_DBF_TEXT(2, setup, "clw_cntl");
        udelay(1000);  /* Wait a ms for the control packets to
			*catch up to each other */
	privptr = dev->ml_priv;
        p_env=privptr->p_env;
	tdev = &privptr->channel[READ_CHANNEL].cdev->dev;
	memcpy( &temp_host_name, p_env->host_name, 8);
        memcpy( &temp_ws_name, p_env->adapter_name , 8);
	dev_info(tdev, "%s: CLAW device %.8s: "
		"Received Control Packet\n",
		dev->name, temp_ws_name);
        if (privptr->release_pend==1) {
                return 0;
        }
        p_buf=p_ccw->p_buffer;
        p_ctlbk=&ctlbk;
	if (p_env->packing == DO_PACKED) { /* packing in progress?*/
		memcpy(p_ctlbk, &p_buf->buffer[4], sizeof(struct clawctl));
	} else {
		memcpy(p_ctlbk, p_buf, sizeof(struct clawctl));
	}
        switch (p_ctlbk->command)
        {
	case SYSTEM_VALIDATE_REQUEST:
		if (p_ctlbk->version != CLAW_VERSION_ID) {
			claw_snd_sys_validate_rsp(dev, p_ctlbk,
				CLAW_RC_WRONG_VERSION);
			dev_warn(tdev, "The communication peer of %s"
				" uses an incorrect API version %d\n",
				dev->name, p_ctlbk->version);
		}
		p_sysval = (struct sysval *)&(p_ctlbk->data);
		dev_info(tdev, "%s: Recv Sys Validate Request: "
			"Vers=%d,link_id=%d,Corr=%d,WS name=%.8s,"
			"Host name=%.8s\n",
			dev->name, p_ctlbk->version,
			p_ctlbk->linkid,
			p_ctlbk->correlator,
			p_sysval->WS_name,
			p_sysval->host_name);
		if (memcmp(temp_host_name, p_sysval->host_name, 8)) {
			claw_snd_sys_validate_rsp(dev, p_ctlbk,
				CLAW_RC_NAME_MISMATCH);
			CLAW_DBF_TEXT(2, setup, "HSTBAD");
			CLAW_DBF_TEXT_(2, setup, "%s", p_sysval->host_name);
			CLAW_DBF_TEXT_(2, setup, "%s", temp_host_name);
			dev_warn(tdev,
				"Host name %s for %s does not match the"
				" remote adapter name %s\n",
				p_sysval->host_name,
				dev->name,
				temp_host_name);
		}
		if (memcmp(temp_ws_name, p_sysval->WS_name, 8)) {
			claw_snd_sys_validate_rsp(dev, p_ctlbk,
				CLAW_RC_NAME_MISMATCH);
			CLAW_DBF_TEXT(2, setup, "WSNBAD");
			CLAW_DBF_TEXT_(2, setup, "%s", p_sysval->WS_name);
			CLAW_DBF_TEXT_(2, setup, "%s", temp_ws_name);
			dev_warn(tdev, "Adapter name %s for %s does not match"
				" the remote host name %s\n",
				p_sysval->WS_name,
				dev->name,
				temp_ws_name);
		}
		if ((p_sysval->write_frame_size < p_env->write_size) &&
		    (p_env->packing == 0)) {
			claw_snd_sys_validate_rsp(dev, p_ctlbk,
				CLAW_RC_HOST_RCV_TOO_SMALL);
			dev_warn(tdev,
				"The local write buffer is smaller than the"
				" remote read buffer\n");
			CLAW_DBF_TEXT(2, setup, "wrtszbad");
		}
		if ((p_sysval->read_frame_size < p_env->read_size) &&
		    (p_env->packing == 0)) {
			claw_snd_sys_validate_rsp(dev, p_ctlbk,
				CLAW_RC_HOST_RCV_TOO_SMALL);
			dev_warn(tdev,
				"The local read buffer is smaller than the"
				" remote write buffer\n");
			CLAW_DBF_TEXT(2, setup, "rdsizbad");
		}
		claw_snd_sys_validate_rsp(dev, p_ctlbk, 0);
		dev_info(tdev,
			"CLAW device %.8s: System validate"
			" completed.\n", temp_ws_name);
		dev_info(tdev,
			"%s: sys Validate Rsize:%d Wsize:%d\n",
			dev->name, p_sysval->read_frame_size,
			p_sysval->write_frame_size);
		privptr->system_validate_comp = 1;
		if (strncmp(p_env->api_type, WS_APPL_NAME_PACKED, 6) == 0)
			p_env->packing = PACKING_ASK;
		claw_strt_conn_req(dev);
		break;
	case SYSTEM_VALIDATE_RESPONSE:
		p_sysval = (struct sysval *)&(p_ctlbk->data);
		dev_info(tdev,
			"Settings for %s validated (version=%d, "
			"remote device=%d, rc=%d, adapter name=%.8s, "
			"host name=%.8s)\n",
			dev->name,
			p_ctlbk->version,
			p_ctlbk->correlator,
			p_ctlbk->rc,
			p_sysval->WS_name,
			p_sysval->host_name);
		switch (p_ctlbk->rc) {
		case 0:
			dev_info(tdev, "%s: CLAW device "
				"%.8s: System validate completed.\n",
				dev->name, temp_ws_name);
			if (privptr->system_validate_comp == 0)
				claw_strt_conn_req(dev);
			privptr->system_validate_comp = 1;
			break;
		case CLAW_RC_NAME_MISMATCH:
			dev_warn(tdev, "Validating %s failed because of"
				" a host or adapter name mismatch\n",
				dev->name);
			break;
		case CLAW_RC_WRONG_VERSION:
			dev_warn(tdev, "Validating %s failed because of a"
				" version conflict\n",
				dev->name);
			break;
		case CLAW_RC_HOST_RCV_TOO_SMALL:
			dev_warn(tdev, "Validating %s failed because of a"
				" frame size conflict\n",
				dev->name);
			break;
		default:
			dev_warn(tdev, "The communication peer of %s rejected"
				" the connection\n",
				 dev->name);
			break;
		}
		break;

	case CONNECTION_REQUEST:
		p_connect = (struct conncmd *)&(p_ctlbk->data);
		dev_info(tdev, "%s: Recv Conn Req: Vers=%d,link_id=%d,"
			"Corr=%d,HOST appl=%.8s,WS appl=%.8s\n",
			dev->name,
			p_ctlbk->version,
			p_ctlbk->linkid,
			p_ctlbk->correlator,
			p_connect->host_name,
			p_connect->WS_name);
		if (privptr->active_link_ID != 0) {
			claw_snd_disc(dev, p_ctlbk);
			dev_info(tdev, "%s rejected a connection request"
				" because it is already active\n",
				dev->name);
		}
		if (p_ctlbk->linkid != 1) {
			claw_snd_disc(dev, p_ctlbk);
			dev_info(tdev, "%s rejected a request to open multiple"
				" connections\n",
				dev->name);
		}
		rc = find_link(dev, p_connect->host_name, p_connect->WS_name);
		if (rc != 0) {
			claw_snd_disc(dev, p_ctlbk);
			dev_info(tdev, "%s rejected a connection request"
				" because of a type mismatch\n",
				dev->name);
		}
		claw_send_control(dev,
			CONNECTION_CONFIRM, p_ctlbk->linkid,
			p_ctlbk->correlator,
			0, p_connect->host_name,
			p_connect->WS_name);
		if (p_env->packing == PACKING_ASK) {
			p_env->packing = PACK_SEND;
			claw_snd_conn_req(dev, 0);
		}
		dev_info(tdev, "%s: CLAW device %.8s: Connection "
			"completed link_id=%d.\n",
			dev->name, temp_ws_name,
			p_ctlbk->linkid);
			privptr->active_link_ID = p_ctlbk->linkid;
			p_ch = &privptr->channel[WRITE_CHANNEL];
			wake_up(&p_ch->wait);  /* wake up claw_open ( WRITE) */
		break;
	case CONNECTION_RESPONSE:
		p_connect = (struct conncmd *)&(p_ctlbk->data);
		dev_info(tdev, "%s: Recv Conn Resp: Vers=%d,link_id=%d,"
			"Corr=%d,RC=%d,Host appl=%.8s, WS appl=%.8s\n",
			dev->name,
			p_ctlbk->version,
			p_ctlbk->linkid,
			p_ctlbk->correlator,
			p_ctlbk->rc,
			p_connect->host_name,
			p_connect->WS_name);

		if (p_ctlbk->rc != 0) {
			dev_warn(tdev, "The communication peer of %s rejected"
				" a connection request\n",
				dev->name);
			return 1;
		}
		rc = find_link(dev,
			p_connect->host_name, p_connect->WS_name);
		if (rc != 0) {
			claw_snd_disc(dev, p_ctlbk);
			dev_warn(tdev, "The communication peer of %s"
				" rejected a connection "
				"request because of a type mismatch\n",
				 dev->name);
		}
		/* should be until CONNECTION_CONFIRM */
		privptr->active_link_ID = -(p_ctlbk->linkid);
		break;
	case CONNECTION_CONFIRM:
		p_connect = (struct conncmd *)&(p_ctlbk->data);
		dev_info(tdev,
			"%s: Recv Conn Confirm:Vers=%d,link_id=%d,"
			"Corr=%d,Host appl=%.8s,WS appl=%.8s\n",
			dev->name,
			p_ctlbk->version,
			p_ctlbk->linkid,
			p_ctlbk->correlator,
			p_connect->host_name,
			p_connect->WS_name);
		if (p_ctlbk->linkid == -(privptr->active_link_ID)) {
			privptr->active_link_ID = p_ctlbk->linkid;
			if (p_env->packing > PACKING_ASK) {
				dev_info(tdev,
				"%s: Confirmed Now packing\n", dev->name);
				p_env->packing = DO_PACKED;
			}
			p_ch = &privptr->channel[WRITE_CHANNEL];
			wake_up(&p_ch->wait);
		} else {
			dev_warn(tdev, "Activating %s failed because of"
				" an incorrect link ID=%d\n",
				dev->name, p_ctlbk->linkid);
			claw_snd_disc(dev, p_ctlbk);
		}
		break;
	case DISCONNECT:
		dev_info(tdev, "%s: Disconnect: "
			"Vers=%d,link_id=%d,Corr=%d\n",
			dev->name, p_ctlbk->version,
			p_ctlbk->linkid, p_ctlbk->correlator);
		if ((p_ctlbk->linkid == 2) &&
		    (p_env->packing == PACK_SEND)) {
			privptr->active_link_ID = 1;
			p_env->packing = DO_PACKED;
		} else
			privptr->active_link_ID = 0;
		break;
	case CLAW_ERROR:
		dev_warn(tdev, "The communication peer of %s failed\n",
			dev->name);
		break;
	default:
		dev_warn(tdev, "The communication peer of %s sent"
			" an unknown command code\n",
			dev->name);
		break;
        }

        return 0;
}   /*    end of claw_process_control    */


/*-------------------------------------------------------------------*
*               claw_send_control                                    *
*                                                                    *
*--------------------------------------------------------------------*/

static int
claw_send_control(struct net_device *dev, __u8 type, __u8 link,
	 __u8 correlator, __u8 rc, char *local_name, char *remote_name)
{
        struct claw_privbk 		*privptr;
        struct clawctl                  *p_ctl;
        struct sysval                   *p_sysval;
        struct conncmd                  *p_connect;
        struct sk_buff 			*skb;

	CLAW_DBF_TEXT(2, setup, "sndcntl");
	privptr = dev->ml_priv;
        p_ctl=(struct clawctl *)&privptr->ctl_bk;

        p_ctl->command=type;
        p_ctl->version=CLAW_VERSION_ID;
        p_ctl->linkid=link;
        p_ctl->correlator=correlator;
        p_ctl->rc=rc;

        p_sysval=(struct sysval *)&p_ctl->data;
        p_connect=(struct conncmd *)&p_ctl->data;

        switch (p_ctl->command) {
                case SYSTEM_VALIDATE_REQUEST:
                case SYSTEM_VALIDATE_RESPONSE:
                        memcpy(&p_sysval->host_name, local_name, 8);
                        memcpy(&p_sysval->WS_name, remote_name, 8);
			if (privptr->p_env->packing > 0) {
				p_sysval->read_frame_size = DEF_PACK_BUFSIZE;
				p_sysval->write_frame_size = DEF_PACK_BUFSIZE;
			} else {
				/* how big is the biggest group of packets */
			   p_sysval->read_frame_size =
				privptr->p_env->read_size;
			   p_sysval->write_frame_size =
				privptr->p_env->write_size;
			}
                        memset(&p_sysval->reserved, 0x00, 4);
                        break;
                case CONNECTION_REQUEST:
                case CONNECTION_RESPONSE:
                case CONNECTION_CONFIRM:
                case DISCONNECT:
                        memcpy(&p_sysval->host_name, local_name, 8);
                        memcpy(&p_sysval->WS_name, remote_name, 8);
			if (privptr->p_env->packing > 0) {
			/* How big is the biggest packet */
				p_connect->reserved1[0]=CLAW_FRAME_SIZE;
                        	p_connect->reserved1[1]=CLAW_FRAME_SIZE;
			} else {
	                        memset(&p_connect->reserved1, 0x00, 4);
        	                memset(&p_connect->reserved2, 0x00, 4);
			}
                        break;
                default:
                        break;
        }

        /*      write Control Record to the device                   */


        skb = dev_alloc_skb(sizeof(struct clawctl));
        if (!skb) {
                return -ENOMEM;
        }
	memcpy(skb_put(skb, sizeof(struct clawctl)),
		p_ctl, sizeof(struct clawctl));
	if (privptr->p_env->packing >= PACK_SEND)
		claw_hw_tx(skb, dev, 1);
	else
        	claw_hw_tx(skb, dev, 0);
        return 0;
}  /*   end of claw_send_control  */

/*-------------------------------------------------------------------*
*               claw_snd_conn_req                                    *
*                                                                    *
*--------------------------------------------------------------------*/
static int
claw_snd_conn_req(struct net_device *dev, __u8 link)
{
        int                rc;
	struct claw_privbk *privptr = dev->ml_priv;
        struct clawctl 	   *p_ctl;

	CLAW_DBF_TEXT(2, setup, "snd_conn");
	rc = 1;
        p_ctl=(struct clawctl *)&privptr->ctl_bk;
	p_ctl->linkid = link;
        if ( privptr->system_validate_comp==0x00 ) {
                return rc;
        }
	if (privptr->p_env->packing == PACKING_ASK )
		rc=claw_send_control(dev, CONNECTION_REQUEST,0,0,0,
        		WS_APPL_NAME_PACKED, WS_APPL_NAME_PACKED);
	if (privptr->p_env->packing == PACK_SEND)  {
		rc=claw_send_control(dev, CONNECTION_REQUEST,0,0,0,
        		WS_APPL_NAME_IP_NAME, WS_APPL_NAME_IP_NAME);
	}
	if (privptr->p_env->packing == 0)
        	rc=claw_send_control(dev, CONNECTION_REQUEST,0,0,0,
       			HOST_APPL_NAME, privptr->p_env->api_type);
        return rc;

}  /*  end of claw_snd_conn_req */


/*-------------------------------------------------------------------*
*               claw_snd_disc                                        *
*                                                                    *
*--------------------------------------------------------------------*/

static int
claw_snd_disc(struct net_device *dev, struct clawctl * p_ctl)
{
        int rc;
        struct conncmd *  p_connect;

	CLAW_DBF_TEXT(2, setup, "snd_dsc");
        p_connect=(struct conncmd *)&p_ctl->data;

        rc=claw_send_control(dev, DISCONNECT, p_ctl->linkid,
		p_ctl->correlator, 0,
                p_connect->host_name, p_connect->WS_name);
        return rc;
}     /*   end of claw_snd_disc    */


/*-------------------------------------------------------------------*
*               claw_snd_sys_validate_rsp                            *
*                                                                    *
*--------------------------------------------------------------------*/

static int
claw_snd_sys_validate_rsp(struct net_device *dev,
	struct clawctl *p_ctl, __u32 return_code)
{
        struct claw_env *  p_env;
        struct claw_privbk *privptr;
        int    rc;

	CLAW_DBF_TEXT(2, setup, "chkresp");
	privptr = dev->ml_priv;
        p_env=privptr->p_env;
        rc=claw_send_control(dev, SYSTEM_VALIDATE_RESPONSE,
		p_ctl->linkid,
		p_ctl->correlator,
                return_code,
		p_env->host_name,
		p_env->adapter_name  );
        return rc;
}     /*    end of claw_snd_sys_validate_rsp    */

/*-------------------------------------------------------------------*
*               claw_strt_conn_req                                   *
*                                                                    *
*--------------------------------------------------------------------*/

static int
claw_strt_conn_req(struct net_device *dev )
{
        int rc;

	CLAW_DBF_TEXT(2, setup, "conn_req");
        rc=claw_snd_conn_req(dev, 1);
        return rc;
}    /*   end of claw_strt_conn_req   */



/*-------------------------------------------------------------------*
 *   claw_stats                                                      *
 *-------------------------------------------------------------------*/

static struct
net_device_stats *claw_stats(struct net_device *dev)
{
        struct claw_privbk *privptr;

	CLAW_DBF_TEXT(4, trace, "stats");
	privptr = dev->ml_priv;
        return &privptr->stats;
}     /*   end of claw_stats   */


/*-------------------------------------------------------------------*
*       unpack_read                                                  *
*                                                                    *
*--------------------------------------------------------------------*/
static void
unpack_read(struct net_device *dev )
{
        struct sk_buff *skb;
        struct claw_privbk *privptr;
	struct claw_env    *p_env;
        struct ccwbk 	*p_this_ccw;
        struct ccwbk 	*p_first_ccw;
        struct ccwbk 	*p_last_ccw;
	struct clawph 	*p_packh;
	void		*p_packd;
	struct clawctl 	*p_ctlrec=NULL;
	struct device	*p_dev;

        __u32	len_of_data;
	__u32	pack_off;
        __u8	link_num;
        __u8 	mtc_this_frm=0;
        __u32	bytes_to_mov;
        int	i=0;
	int     p=0;

	CLAW_DBF_TEXT(4, trace, "unpkread");
        p_first_ccw=NULL;
        p_last_ccw=NULL;
	p_packh=NULL;
	p_packd=NULL;
	privptr = dev->ml_priv;

	p_dev = &privptr->channel[READ_CHANNEL].cdev->dev;
	p_env = privptr->p_env;
        p_this_ccw=privptr->p_read_active_first;
	while (p_this_ccw!=NULL && p_this_ccw->header.flag!=CLAW_PENDING) {
		pack_off = 0;
		p = 0;
		p_this_ccw->header.flag=CLAW_PENDING;
		privptr->p_read_active_first=p_this_ccw->next;
                p_this_ccw->next=NULL;
		p_packh = (struct clawph *)p_this_ccw->p_buffer;
		if ((p_env->packing == PACK_SEND) &&
		    (p_packh->len == 32)           &&
		    (p_packh->link_num == 0)) {   /* is it a packed ctl rec? */
			p_packh++;  /* peek past pack header */
			p_ctlrec = (struct clawctl *)p_packh;
			p_packh--;  /* un peek */
			if ((p_ctlrec->command == CONNECTION_RESPONSE) ||
		            (p_ctlrec->command == CONNECTION_CONFIRM))
				p_env->packing = DO_PACKED;
		}
		if (p_env->packing == DO_PACKED)
			link_num=p_packh->link_num;
		else
	                link_num=p_this_ccw->header.opcode / 8;
                if ((p_this_ccw->header.opcode & MORE_to_COME_FLAG)!=0) {
                        mtc_this_frm=1;
                        if (p_this_ccw->header.length!=
				privptr->p_env->read_size ) {
				dev_warn(p_dev,
					"The communication peer of %s"
					" sent a faulty"
					" frame of length %02x\n",
                                        dev->name, p_this_ccw->header.length);
                        }
                }

                if (privptr->mtc_skipping) {
                        /*
                        *   We're in the mode of skipping past a
			*   multi-frame message
                        *   that we can't process for some reason or other.
                        *   The first frame without the More-To-Come flag is
			*   the last frame of the skipped message.
                        */
                        /*  in case of More-To-Come not set in this frame */
                        if (mtc_this_frm==0) {
                                privptr->mtc_skipping=0; /* Ok, the end */
                                privptr->mtc_logical_link=-1;
                        }
                        goto NextFrame;
                }

                if (link_num==0) {
                        claw_process_control(dev, p_this_ccw);
			CLAW_DBF_TEXT(4, trace, "UnpkCntl");
                        goto NextFrame;
                }
unpack_next:
		if (p_env->packing == DO_PACKED) {
			if (pack_off > p_env->read_size)
				goto NextFrame;
			p_packd = p_this_ccw->p_buffer+pack_off;
			p_packh = (struct clawph *) p_packd;
			if ((p_packh->len == 0) || /* done with this frame? */
			    (p_packh->flag != 0))
				goto NextFrame;
			bytes_to_mov = p_packh->len;
			pack_off += bytes_to_mov+sizeof(struct clawph);
			p++;
		} else {
                	bytes_to_mov=p_this_ccw->header.length;
		}
                if (privptr->mtc_logical_link<0) {

                /*
                *  if More-To-Come is set in this frame then we don't know
                *  length of entire message, and hence have to allocate
		*  large buffer   */

                /*      We are starting a new envelope  */
                privptr->mtc_offset=0;
                        privptr->mtc_logical_link=link_num;
                }

                if (bytes_to_mov > (MAX_ENVELOPE_SIZE- privptr->mtc_offset) ) {
                        /*      error     */
                        privptr->stats.rx_frame_errors++;
                        goto NextFrame;
                }
		if (p_env->packing == DO_PACKED) {
			memcpy( privptr->p_mtc_envelope+ privptr->mtc_offset,
				p_packd+sizeof(struct clawph), bytes_to_mov);

		} else	{
                	memcpy( privptr->p_mtc_envelope+ privptr->mtc_offset,
                        	p_this_ccw->p_buffer, bytes_to_mov);
		}
                if (mtc_this_frm==0) {
                        len_of_data=privptr->mtc_offset+bytes_to_mov;
                        skb=dev_alloc_skb(len_of_data);
                        if (skb) {
                                memcpy(skb_put(skb,len_of_data),
					privptr->p_mtc_envelope,
					len_of_data);
                                skb->dev=dev;
				skb_reset_mac_header(skb);
                                skb->protocol=htons(ETH_P_IP);
                                skb->ip_summed=CHECKSUM_UNNECESSARY;
                                privptr->stats.rx_packets++;
				privptr->stats.rx_bytes+=len_of_data;
                                netif_rx(skb);
                        }
                        else {
				dev_info(p_dev, "Allocating a buffer for"
					" incoming data failed\n");
                                privptr->stats.rx_dropped++;
                        }
                        privptr->mtc_offset=0;
                        privptr->mtc_logical_link=-1;
                }
                else {
                        privptr->mtc_offset+=bytes_to_mov;
                }
		if (p_env->packing == DO_PACKED)
			goto unpack_next;
NextFrame:
                /*
                *   Remove ThisCCWblock from active read queue, and add it
                *   to queue of free blocks to be reused.
                */
                i++;
                p_this_ccw->header.length=0xffff;
                p_this_ccw->header.opcode=0xff;
                /*
                *       add this one to the free queue for later reuse
                */
                if (p_first_ccw==NULL) {
                        p_first_ccw = p_this_ccw;
                }
                else {
                        p_last_ccw->next = p_this_ccw;
                }
                p_last_ccw = p_this_ccw;
                /*
                *       chain to next block on active read queue
                */
                p_this_ccw = privptr->p_read_active_first;
		CLAW_DBF_TEXT_(4, trace, "rxpkt %d", p);
        } /* end of while */

        /*      check validity                  */

	CLAW_DBF_TEXT_(4, trace, "rxfrm %d", i);
        add_claw_reads(dev, p_first_ccw, p_last_ccw);
        claw_strt_read(dev, LOCK_YES);
        return;
}     /*  end of unpack_read   */

/*-------------------------------------------------------------------*
*       claw_strt_read                                               *
*                                                                    *
*--------------------------------------------------------------------*/
static void
claw_strt_read (struct net_device *dev, int lock )
{
        int        rc = 0;
        __u32      parm;
        unsigned long  saveflags = 0;
	struct claw_privbk *privptr = dev->ml_priv;
        struct ccwbk*p_ccwbk;
        struct chbk *p_ch;
        struct clawh *p_clawh;
	p_ch = &privptr->channel[READ_CHANNEL];

	CLAW_DBF_TEXT(4, trace, "StRdNter");
        p_clawh=(struct clawh *)privptr->p_claw_signal_blk;
        p_clawh->flag=CLAW_IDLE;    /* 0x00 */

        if ((privptr->p_write_active_first!=NULL &&
             privptr->p_write_active_first->header.flag!=CLAW_PENDING) ||
            (privptr->p_read_active_first!=NULL &&
             privptr->p_read_active_first->header.flag!=CLAW_PENDING )) {
                p_clawh->flag=CLAW_BUSY;    /* 0xff */
        }
        if (lock==LOCK_YES) {
                spin_lock_irqsave(get_ccwdev_lock(p_ch->cdev), saveflags);
        }
        if (test_and_set_bit(0, (void *)&p_ch->IO_active) == 0) {
		CLAW_DBF_TEXT(4, trace, "HotRead");
                p_ccwbk=privptr->p_read_active_first;
                parm = (unsigned long) p_ch;
                rc = ccw_device_start (p_ch->cdev, &p_ccwbk->read, parm,
				       0xff, 0);
                if (rc != 0) {
                        ccw_check_return_code(p_ch->cdev, rc);
                }
        }
	else {
		CLAW_DBF_TEXT(2, trace, "ReadAct");
	}

        if (lock==LOCK_YES) {
                spin_unlock_irqrestore(get_ccwdev_lock(p_ch->cdev), saveflags);
        }
	CLAW_DBF_TEXT(4, trace, "StRdExit");
        return;
}       /*    end of claw_strt_read    */

/*-------------------------------------------------------------------*
*       claw_strt_out_IO                                             *
*                                                                    *
*--------------------------------------------------------------------*/

static void
claw_strt_out_IO( struct net_device *dev )
{
        int             	rc = 0;
        unsigned long   	parm;
        struct claw_privbk 	*privptr;
        struct chbk     	*p_ch;
        struct ccwbk   	*p_first_ccw;

	if (!dev) {
		return;
	}
	privptr = (struct claw_privbk *)dev->ml_priv;
	p_ch = &privptr->channel[WRITE_CHANNEL];

	CLAW_DBF_TEXT(4, trace, "strt_io");
        p_first_ccw=privptr->p_write_active_first;

        if (p_ch->claw_state == CLAW_STOP)
                return;
        if (p_first_ccw == NULL) {
                return;
        }
        if (test_and_set_bit(0, (void *)&p_ch->IO_active) == 0) {
                parm = (unsigned long) p_ch;
		CLAW_DBF_TEXT(2, trace, "StWrtIO");
		rc = ccw_device_start(p_ch->cdev, &p_first_ccw->write, parm,
				      0xff, 0);
                if (rc != 0) {
                        ccw_check_return_code(p_ch->cdev, rc);
                }
        }
        dev->trans_start = jiffies;
        return;
}       /*    end of claw_strt_out_IO    */

/*-------------------------------------------------------------------*
*       Free write buffers                                           *
*                                                                    *
*--------------------------------------------------------------------*/

static void
claw_free_wrt_buf( struct net_device *dev )
{

	struct claw_privbk *privptr = (struct claw_privbk *)dev->ml_priv;
	struct ccwbk*p_this_ccw;
	struct ccwbk*p_next_ccw;

	CLAW_DBF_TEXT(4, trace, "freewrtb");
        /*  scan the write queue to free any completed write packets   */
        p_this_ccw=privptr->p_write_active_first;
        while ( (p_this_ccw!=NULL) && (p_this_ccw->header.flag!=CLAW_PENDING))
        {
                p_next_ccw = p_this_ccw->next;
                if (((p_next_ccw!=NULL) &&
		     (p_next_ccw->header.flag!=CLAW_PENDING)) ||
                    ((p_this_ccw == privptr->p_write_active_last) &&
                     (p_this_ccw->header.flag!=CLAW_PENDING))) {
                        /* The next CCW is OK or this is  */
			/* the last CCW...free it   @A1A  */
                        privptr->p_write_active_first=p_this_ccw->next;
			p_this_ccw->header.flag=CLAW_PENDING;
                        p_this_ccw->next=privptr->p_write_free_chain;
			privptr->p_write_free_chain=p_this_ccw;
                        ++privptr->write_free_count;
			privptr->stats.tx_bytes+= p_this_ccw->write.count;
			p_this_ccw=privptr->p_write_active_first;
                        privptr->stats.tx_packets++;
                }
                else {
			break;
                }
        }
        if (privptr->write_free_count!=0) {
                claw_clearbit_busy(TB_NOBUFFER,dev);
        }
        /*   whole chain removed?   */
        if (privptr->p_write_active_first==NULL) {
                privptr->p_write_active_last=NULL;
        }
	CLAW_DBF_TEXT_(4, trace, "FWC=%d", privptr->write_free_count);
        return;
}

/*-------------------------------------------------------------------*
*       claw free netdevice                                          *
*                                                                    *
*--------------------------------------------------------------------*/
static void
claw_free_netdevice(struct net_device * dev, int free_dev)
{
	struct claw_privbk *privptr;

	CLAW_DBF_TEXT(2, setup, "free_dev");
	if (!dev)
		return;
	CLAW_DBF_TEXT_(2, setup, "%s", dev->name);
	privptr = dev->ml_priv;
	if (dev->flags & IFF_RUNNING)
		claw_release(dev);
	if (privptr) {
		privptr->channel[READ_CHANNEL].ndev = NULL;  /* say it's free */
	}
	dev->ml_priv = NULL;
#ifdef MODULE
	if (free_dev) {
		free_netdev(dev);
	}
#endif
	CLAW_DBF_TEXT(2, setup, "free_ok");
}

/**
 * Claw init netdevice
 * Initialize everything of the net device except the name and the
 * channel structs.
 */
static const struct net_device_ops claw_netdev_ops = {
	.ndo_open		= claw_open,
	.ndo_stop		= claw_release,
	.ndo_get_stats		= claw_stats,
	.ndo_start_xmit		= claw_tx,
	.ndo_change_mtu		= claw_change_mtu,
};

static void
claw_init_netdevice(struct net_device * dev)
{
	CLAW_DBF_TEXT(2, setup, "init_dev");
	CLAW_DBF_TEXT_(2, setup, "%s", dev->name);
	dev->mtu = CLAW_DEFAULT_MTU_SIZE;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->type = ARPHRD_SLIP;
	dev->tx_queue_len = 1300;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->netdev_ops = &claw_netdev_ops;
	CLAW_DBF_TEXT(2, setup, "initok");
	return;
}

/**
 * Init a new channel in the privptr->channel[i].
 *
 * @param cdev  The ccw_device to be added.
 *
 * @return 0 on success, !0 on error.
 */
static int
add_channel(struct ccw_device *cdev,int i,struct claw_privbk *privptr)
{
	struct chbk *p_ch;
	struct ccw_dev_id dev_id;

	CLAW_DBF_TEXT_(2, setup, "%s", dev_name(&cdev->dev));
	privptr->channel[i].flag  = i+1;   /* Read is 1 Write is 2 */
	p_ch = &privptr->channel[i];
	p_ch->cdev = cdev;
	snprintf(p_ch->id, CLAW_ID_SIZE, "cl-%s", dev_name(&cdev->dev));
	ccw_device_get_id(cdev, &dev_id);
	p_ch->devno = dev_id.devno;
	if ((p_ch->irb = kzalloc(sizeof (struct irb),GFP_KERNEL)) == NULL) {
		return -ENOMEM;
	}
	return 0;
}


/**
 *
 * Setup an interface.
 *
 * @param cgdev  Device to be setup.
 *
 * @returns 0 on success, !0 on failure.
 */
static int
claw_new_device(struct ccwgroup_device *cgdev)
{
	struct claw_privbk *privptr;
	struct claw_env *p_env;
	struct net_device *dev;
	int ret;
	struct ccw_dev_id dev_id;

	dev_info(&cgdev->dev, "add for %s\n",
		 dev_name(&cgdev->cdev[READ_CHANNEL]->dev));
	CLAW_DBF_TEXT(2, setup, "new_dev");
	privptr = dev_get_drvdata(&cgdev->dev);
	dev_set_drvdata(&cgdev->cdev[READ_CHANNEL]->dev, privptr);
	dev_set_drvdata(&cgdev->cdev[WRITE_CHANNEL]->dev, privptr);
	if (!privptr)
		return -ENODEV;
	p_env = privptr->p_env;
	ccw_device_get_id(cgdev->cdev[READ_CHANNEL], &dev_id);
	p_env->devno[READ_CHANNEL] = dev_id.devno;
	ccw_device_get_id(cgdev->cdev[WRITE_CHANNEL], &dev_id);
	p_env->devno[WRITE_CHANNEL] = dev_id.devno;
	ret = add_channel(cgdev->cdev[0],0,privptr);
	if (ret == 0)
		ret = add_channel(cgdev->cdev[1],1,privptr);
	if (ret != 0) {
		dev_warn(&cgdev->dev, "Creating a CLAW group device"
			" failed with error code %d\n", ret);
		goto out;
	}
	ret = ccw_device_set_online(cgdev->cdev[READ_CHANNEL]);
	if (ret != 0) {
		dev_warn(&cgdev->dev,
			"Setting the read subchannel online"
			" failed with error code %d\n", ret);
		goto out;
	}
	ret = ccw_device_set_online(cgdev->cdev[WRITE_CHANNEL]);
	if (ret != 0) {
		dev_warn(&cgdev->dev,
			"Setting the write subchannel online "
			"failed with error code %d\n", ret);
		goto out;
	}
	dev = alloc_netdev(0,"claw%d",claw_init_netdevice);
	if (!dev) {
		dev_warn(&cgdev->dev,
			"Activating the CLAW device failed\n");
		goto out;
	}
	dev->ml_priv = privptr;
	dev_set_drvdata(&cgdev->dev, privptr);
	dev_set_drvdata(&cgdev->cdev[READ_CHANNEL]->dev, privptr);
	dev_set_drvdata(&cgdev->cdev[WRITE_CHANNEL]->dev, privptr);
	/* sysfs magic */
        SET_NETDEV_DEV(dev, &cgdev->dev);
	if (register_netdev(dev) != 0) {
		claw_free_netdevice(dev, 1);
		CLAW_DBF_TEXT(2, trace, "regfail");
		goto out;
	}
	dev->flags &=~IFF_RUNNING;
	if (privptr->buffs_alloc == 0) {
	        ret=init_ccw_bk(dev);
		if (ret !=0) {
			unregister_netdev(dev);
			claw_free_netdevice(dev,1);
			CLAW_DBF_TEXT(2, trace, "ccwmem");
			goto out;
		}
	}
	privptr->channel[READ_CHANNEL].ndev = dev;
	privptr->channel[WRITE_CHANNEL].ndev = dev;
	privptr->p_env->ndev = dev;

	dev_info(&cgdev->dev, "%s:readsize=%d  writesize=%d "
		"readbuffer=%d writebuffer=%d read=0x%04x write=0x%04x\n",
                dev->name, p_env->read_size,
		p_env->write_size, p_env->read_buffers,
		p_env->write_buffers, p_env->devno[READ_CHANNEL],
		p_env->devno[WRITE_CHANNEL]);
	dev_info(&cgdev->dev, "%s:host_name:%.8s, adapter_name "
		":%.8s api_type: %.8s\n",
                dev->name, p_env->host_name,
		p_env->adapter_name , p_env->api_type);
	return 0;
out:
	ccw_device_set_offline(cgdev->cdev[1]);
	ccw_device_set_offline(cgdev->cdev[0]);
	return -ENODEV;
}

static void
claw_purge_skb_queue(struct sk_buff_head *q)
{
        struct sk_buff *skb;

	CLAW_DBF_TEXT(4, trace, "purgque");
        while ((skb = skb_dequeue(q))) {
                atomic_dec(&skb->users);
                dev_kfree_skb_any(skb);
        }
}

/**
 * Shutdown an interface.
 *
 * @param cgdev  Device to be shut down.
 *
 * @returns 0 on success, !0 on failure.
 */
static int
claw_shutdown_device(struct ccwgroup_device *cgdev)
{
	struct claw_privbk *priv;
	struct net_device *ndev;
	int ret = 0;

	CLAW_DBF_TEXT_(2, setup, "%s", dev_name(&cgdev->dev));
	priv = dev_get_drvdata(&cgdev->dev);
	if (!priv)
		return -ENODEV;
	ndev = priv->channel[READ_CHANNEL].ndev;
	if (ndev) {
		/* Close the device */
		dev_info(&cgdev->dev, "%s: shutting down\n",
			ndev->name);
		if (ndev->flags & IFF_RUNNING)
			ret = claw_release(ndev);
		ndev->flags &=~IFF_RUNNING;
		unregister_netdev(ndev);
		ndev->ml_priv = NULL;  /* cgdev data, not ndev's to free */
		claw_free_netdevice(ndev, 1);
		priv->channel[READ_CHANNEL].ndev = NULL;
		priv->channel[WRITE_CHANNEL].ndev = NULL;
		priv->p_env->ndev = NULL;
	}
	ccw_device_set_offline(cgdev->cdev[1]);
	ccw_device_set_offline(cgdev->cdev[0]);
	return ret;
}

static void
claw_remove_device(struct ccwgroup_device *cgdev)
{
	struct claw_privbk *priv;

	BUG_ON(!cgdev);
	CLAW_DBF_TEXT_(2, setup, "%s", dev_name(&cgdev->dev));
	priv = dev_get_drvdata(&cgdev->dev);
	BUG_ON(!priv);
	dev_info(&cgdev->dev, " will be removed.\n");
	if (cgdev->state == CCWGROUP_ONLINE)
		claw_shutdown_device(cgdev);
	claw_remove_files(&cgdev->dev);
	kfree(priv->p_mtc_envelope);
	priv->p_mtc_envelope=NULL;
	kfree(priv->p_env);
	priv->p_env=NULL;
	kfree(priv->channel[0].irb);
	priv->channel[0].irb=NULL;
	kfree(priv->channel[1].irb);
	priv->channel[1].irb=NULL;
	kfree(priv);
	dev_set_drvdata(&cgdev->dev, NULL);
	dev_set_drvdata(&cgdev->cdev[READ_CHANNEL]->dev, NULL);
	dev_set_drvdata(&cgdev->cdev[WRITE_CHANNEL]->dev, NULL);
	put_device(&cgdev->dev);

	return;
}


/*
 * sysfs attributes
 */
static ssize_t
claw_hname_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	return sprintf(buf, "%s\n",p_env->host_name);
}

static ssize_t
claw_hname_write(struct device *dev, struct device_attribute *attr,
	 const char *buf, size_t count)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	if (count > MAX_NAME_LEN+1)
		return -EINVAL;
	memset(p_env->host_name, 0x20, MAX_NAME_LEN);
	strncpy(p_env->host_name,buf, count);
	p_env->host_name[count-1] = 0x20;  /* clear extra 0x0a */
	p_env->host_name[MAX_NAME_LEN] = 0x00;
	CLAW_DBF_TEXT(2, setup, "HstnSet");
	CLAW_DBF_TEXT_(2, setup, "%s", p_env->host_name);

	return count;
}

static DEVICE_ATTR(host_name, 0644, claw_hname_show, claw_hname_write);

static ssize_t
claw_adname_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	return sprintf(buf, "%s\n", p_env->adapter_name);
}

static ssize_t
claw_adname_write(struct device *dev, struct device_attribute *attr,
	 const char *buf, size_t count)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	if (count > MAX_NAME_LEN+1)
		return -EINVAL;
	memset(p_env->adapter_name, 0x20, MAX_NAME_LEN);
	strncpy(p_env->adapter_name,buf, count);
	p_env->adapter_name[count-1] = 0x20; /* clear extra 0x0a */
	p_env->adapter_name[MAX_NAME_LEN] = 0x00;
	CLAW_DBF_TEXT(2, setup, "AdnSet");
	CLAW_DBF_TEXT_(2, setup, "%s", p_env->adapter_name);

	return count;
}

static DEVICE_ATTR(adapter_name, 0644, claw_adname_show, claw_adname_write);

static ssize_t
claw_apname_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	return sprintf(buf, "%s\n",
		       p_env->api_type);
}

static ssize_t
claw_apname_write(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	if (count > MAX_NAME_LEN+1)
		return -EINVAL;
	memset(p_env->api_type, 0x20, MAX_NAME_LEN);
	strncpy(p_env->api_type,buf, count);
	p_env->api_type[count-1] = 0x20;  /* we get a loose 0x0a */
	p_env->api_type[MAX_NAME_LEN] = 0x00;
	if(strncmp(p_env->api_type,WS_APPL_NAME_PACKED,6) == 0) {
		p_env->read_size=DEF_PACK_BUFSIZE;
		p_env->write_size=DEF_PACK_BUFSIZE;
		p_env->packing=PACKING_ASK;
		CLAW_DBF_TEXT(2, setup, "PACKING");
	}
	else {
		p_env->packing=0;
		p_env->read_size=CLAW_FRAME_SIZE;
		p_env->write_size=CLAW_FRAME_SIZE;
		CLAW_DBF_TEXT(2, setup, "ApiSet");
	}
	CLAW_DBF_TEXT_(2, setup, "%s", p_env->api_type);
	return count;
}

static DEVICE_ATTR(api_type, 0644, claw_apname_show, claw_apname_write);

static ssize_t
claw_wbuff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct claw_privbk *priv;
	struct claw_env * p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	return sprintf(buf, "%d\n", p_env->write_buffers);
}

static ssize_t
claw_wbuff_write(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;
	int nnn,max;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	sscanf(buf, "%i", &nnn);
	if (p_env->packing) {
		max = 64;
	}
	else {
		max = 512;
	}
	if ((nnn > max ) || (nnn < 2))
		return -EINVAL;
	p_env->write_buffers = nnn;
	CLAW_DBF_TEXT(2, setup, "Wbufset");
	CLAW_DBF_TEXT_(2, setup, "WB=%d", p_env->write_buffers);
	return count;
}

static DEVICE_ATTR(write_buffer, 0644, claw_wbuff_show, claw_wbuff_write);

static ssize_t
claw_rbuff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct claw_privbk *priv;
	struct claw_env *  p_env;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	return sprintf(buf, "%d\n", p_env->read_buffers);
}

static ssize_t
claw_rbuff_write(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct claw_privbk *priv;
	struct claw_env *p_env;
	int nnn,max;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODEV;
	p_env = priv->p_env;
	sscanf(buf, "%i", &nnn);
	if (p_env->packing) {
		max = 64;
	}
	else {
		max = 512;
	}
	if ((nnn > max ) || (nnn < 2))
		return -EINVAL;
	p_env->read_buffers = nnn;
	CLAW_DBF_TEXT(2, setup, "Rbufset");
	CLAW_DBF_TEXT_(2, setup, "RB=%d", p_env->read_buffers);
	return count;
}

static DEVICE_ATTR(read_buffer, 0644, claw_rbuff_show, claw_rbuff_write);

static struct attribute *claw_attr[] = {
	&dev_attr_read_buffer.attr,
	&dev_attr_write_buffer.attr,
	&dev_attr_adapter_name.attr,
	&dev_attr_api_type.attr,
	&dev_attr_host_name.attr,
	NULL,
};

static struct attribute_group claw_attr_group = {
	.attrs = claw_attr,
};

static int
claw_add_files(struct device *dev)
{
	CLAW_DBF_TEXT(2, setup, "add_file");
	return sysfs_create_group(&dev->kobj, &claw_attr_group);
}

static void
claw_remove_files(struct device *dev)
{
	CLAW_DBF_TEXT(2, setup, "rem_file");
	sysfs_remove_group(&dev->kobj, &claw_attr_group);
}

/*--------------------------------------------------------------------*
*    claw_init  and cleanup                                           *
*---------------------------------------------------------------------*/

static void __exit
claw_cleanup(void)
{
	driver_remove_file(&claw_group_driver.driver,
			   &driver_attr_group);
	ccwgroup_driver_unregister(&claw_group_driver);
	ccw_driver_unregister(&claw_ccw_driver);
	root_device_unregister(claw_root_dev);
	claw_unregister_debug_facility();
	pr_info("Driver unloaded\n");

}

/**
 * Initialize module.
 * This is called just after the module is loaded.
 *
 * @return 0 on success, !0 on error.
 */
static int __init
claw_init(void)
{
	int ret = 0;

	pr_info("Loading %s\n", version);
	ret = claw_register_debug_facility();
	if (ret) {
		pr_err("Registering with the S/390 debug feature"
			" failed with error code %d\n", ret);
		goto out_err;
	}
	CLAW_DBF_TEXT(2, setup, "init_mod");
	claw_root_dev = root_device_register("claw");
	ret = IS_ERR(claw_root_dev) ? PTR_ERR(claw_root_dev) : 0;
	if (ret)
		goto register_err;
	ret = ccw_driver_register(&claw_ccw_driver);
	if (ret)
		goto ccw_err;
	claw_group_driver.driver.groups = claw_group_attr_groups;
	ret = ccwgroup_driver_register(&claw_group_driver);
	if (ret)
		goto ccwgroup_err;
	return 0;

ccwgroup_err:
	ccw_driver_unregister(&claw_ccw_driver);
ccw_err:
	root_device_unregister(claw_root_dev);
register_err:
	CLAW_DBF_TEXT(2, setup, "init_bad");
	claw_unregister_debug_facility();
out_err:
	pr_err("Initializing the claw device driver failed\n");
	return ret;
}

module_init(claw_init);
module_exit(claw_cleanup);

MODULE_AUTHOR("Andy Richter <richtera@us.ibm.com>");
MODULE_DESCRIPTION("Linux for System z CLAW Driver\n" \
			"Copyright 2000,2008 IBM Corporation\n");
MODULE_LICENSE("GPL");
