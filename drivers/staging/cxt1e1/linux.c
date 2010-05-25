/* Copyright (C) 2007-2008  One Stop Systems
 * Copyright (C) 2003-2006  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "libsbew.h"
#include "pmcc4.h"
#include "pmcc4_ioctls.h"
#include "pmcc4_private.h"
#include "sbeproc.h"

/*****************************************************************************************
 * Error out early if we have compiler trouble.
 *
 *   (This section is included from the kernel's init/main.c as a friendly
 *   spiderman recommendation...)
 *
 * Versions of gcc older than that listed below may actually compile and link
 * okay, but the end product can have subtle run time bugs.  To avoid associated
 * bogus bug reports, we flatly refuse to compile with a gcc that is known to be
 * too old from the very beginning.
 */
#if (__GNUC__ < 3) || (__GNUC__ == 3 && __GNUC_MINOR__ < 2)
#error Sorry, your GCC is too old. It builds incorrect kernels.
#endif

#if __GNUC__ == 4 && __GNUC_MINOR__ == 1 && __GNUC_PATCHLEVEL__ == 0
#warning gcc-4.1.0 is known to miscompile the kernel.  A different compiler version is recommended.
#endif

/*****************************************************************************************/

#ifdef SBE_INCLUDE_SYMBOLS
#define STATIC
#else
#define STATIC  static
#endif

#define CHANNAME "hdlc"

/*******************************************************************/
/* forward references */
status_t    c4_chan_work_init (mpi_t *, mch_t *);
void        musycc_wq_chan_restart (void *);
status_t __init c4_init (ci_t *, u_char *, u_char *);
status_t __init c4_init2 (ci_t *);
ci_t       *__init c4_new (void *);
int __init  c4hw_attach_all (void);
void __init hdw_sn_get (hdw_info_t *, int);

#ifdef CONFIG_SBE_PMCC4_NCOMM
irqreturn_t c4_ebus_intr_th_handler (void *);

#endif
int         c4_frame_rw (ci_t *, struct sbecom_port_param *);
status_t    c4_get_port (ci_t *, int);
int         c4_loop_port (ci_t *, int, u_int8_t);
int         c4_musycc_rw (ci_t *, struct c4_musycc_param *);
int         c4_new_chan (ci_t *, int, int, void *);
status_t    c4_set_port (ci_t *, int);
int         c4_pld_rw (ci_t *, struct sbecom_port_param *);
void        cleanup_devs (void);
void        cleanup_ioremap (void);
status_t    musycc_chan_down (ci_t *, int);
irqreturn_t musycc_intr_th_handler (void *);
int         musycc_start_xmit (ci_t *, int, void *);

extern char pmcc4_OSSI_release[];
extern ci_t *CI;
extern struct s_hdw_info hdw_info[];

#if defined(CONFIG_SBE_HDLC_V7) || defined(CONFIG_SBE_WAN256T3_HDLC_V7) || \
    defined(CONFIG_SBE_HDLC_V7_MODULE) || defined(CONFIG_SBE_WAN256T3_HDLC_V7_MODULE)
#define _v7_hdlc_  1
#else
#define _v7_hdlc_  0
#endif

#if _v7_hdlc_
#define V7(x) (x ## _v7)
extern int  hdlc_netif_rx_v7 (hdlc_device *, struct sk_buff *);
extern int  register_hdlc_device_v7 (hdlc_device *);
extern int  unregister_hdlc_device_v7 (hdlc_device *);

#else
#define V7(x) x
#endif

int         error_flag;         /* module load error reporting */
int         log_level = LOG_ERROR;
int         log_level_default = LOG_ERROR;
module_param(log_level, int, 0444);

int         max_mru = MUSYCC_MRU;
int         max_mru_default = MUSYCC_MRU;
module_param(max_mru, int, 0444);

int         max_mtu = MUSYCC_MTU;
int         max_mtu_default = MUSYCC_MTU;
module_param(max_mtu, int, 0444);

int         max_txdesc_used = MUSYCC_TXDESC_MIN;
int         max_txdesc_default = MUSYCC_TXDESC_MIN;
module_param(max_txdesc_used, int, 0444);

int         max_rxdesc_used = MUSYCC_RXDESC_MIN;
int         max_rxdesc_default = MUSYCC_RXDESC_MIN;
module_param(max_rxdesc_used, int, 0444);

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

void       *
getuserbychan (int channum)
{
    mch_t      *ch;

    ch = c4_find_chan (channum);
    return ch ? ch->user : 0;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#define DEV_TO_PRIV(dev) ( * (struct c4_priv **) ((hdlc_device*)(dev)+1))
#else

char       *
get_hdlc_name (hdlc_device * hdlc)
{
    struct c4_priv *priv = hdlc->priv;
    struct net_device *dev = getuserbychan (priv->channum);

    return dev->name;
}
#endif


static      status_t
mkret (int bsd)
{
    if (bsd > 0)
        return -bsd;
    else
        return bsd;
}

/***************************************************************************/
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,41)
#include <linux/workqueue.h>

/***
 * One workqueue (wq) per port (since musycc allows simultaneous group
 * commands), with individual data for each channel:
 *
 *   mpi_t -> struct workqueue_struct *wq_port;  (dynamically allocated using
 *                                               create_workqueue())
 *
 * With work structure (work) statically allocated for each channel:
 *
 *   mch_t -> struct work_struct ch_work;  (statically allocated using ???)
 *
 ***/


/*
 * Called by the start transmit routine when a channel TX_ENABLE is to be
 * issued.  This queues the transmission start request among other channels
 * within a port's group.
 */
void
c4_wk_chan_restart (mch_t * ch)
{
    mpi_t      *pi = ch->up;

#ifdef RLD_RESTART_DEBUG
    pr_info(">> %s: queueing Port %d Chan %d, mch_t @ %p\n",
            __func__, pi->portnum, ch->channum, ch);
#endif

    /* create new entry w/in workqueue for this channel and let'er rip */

    /** queue_work (struct workqueue_struct *queue,
     **             struct work_struct *work);
     **/
    queue_work (pi->wq_port, &ch->ch_work);
}

status_t
c4_wk_chan_init (mpi_t * pi, mch_t * ch)
{
    /*
     * this will be used to restart a stopped channel
     */

    /** INIT_WORK (struct work_struct *work,
     **            void (*function)(void *),
     **            void *data);
     **/
    INIT_WORK(&ch->ch_work, (void *)musycc_wq_chan_restart);
    return 0;                       /* success */
}

status_t
c4_wq_port_init (mpi_t * pi)
{

    char        name[16], *np;  /* NOTE: name of the queue limited by system
                                 * to 10 characters */

    if (pi->wq_port)
        return 0;                   /* already initialized */

    np = name;
    memset (name, 0, 16);
    sprintf (np, "%s%d", pi->up->devname, pi->portnum); /* IE pmcc4-01) */

#ifdef RLD_RESTART_DEBUG
    pr_info(">> %s: creating workqueue <%s> for Port %d.\n",
            __func__, name, pi->portnum); /* RLD DEBUG */
#endif
    if (!(pi->wq_port = create_singlethread_workqueue (name)))
        return ENOMEM;
    return 0;                       /* success */
}

void
c4_wq_port_cleanup (mpi_t * pi)
{
    /*
     * PORT POINT: cannot call this if WQ is statically allocated w/in
     * structure since it calls kfree(wq);
     */
    if (pi->wq_port)
    {
        destroy_workqueue (pi->wq_port);        /* this also calls
                                                 * flush_workqueue() */
        pi->wq_port = 0;
    }
}
#endif

/***************************************************************************/

irqreturn_t
c4_linux_interrupt (int irq, void *dev_instance)
{
    struct net_device *ndev = dev_instance;

    return musycc_intr_th_handler(netdev_priv(ndev));
}


#ifdef CONFIG_SBE_PMCC4_NCOMM
irqreturn_t
c4_ebus_interrupt (int irq, void *dev_instance)
{
    struct net_device *ndev = dev_instance;

    return c4_ebus_intr_th_handler(netdev_priv(ndev));
}
#endif


static int
void_open (struct net_device * ndev)
{
    pr_info("%s: trying to open master device !\n", ndev->name);
    return -1;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#if !defined(GENERIC_HDLC_VERSION) || (GENERIC_HDLC_VERSION < 4)

/** Linux 2.4.18-19 **/
STATIC int
chan_open (hdlc_device * hdlc)
{
    status_t    ret;

    if ((ret = c4_chan_up (DEV_TO_PRIV (hdlc)->ci, DEV_TO_PRIV (hdlc)->channum)))
        return -ret;
    MOD_INC_USE_COUNT;
    netif_start_queue (hdlc_to_dev (hdlc));
    return 0;                       /* no error = success */
}

#else

/** Linux 2.4.20 and higher **/
STATIC int
chan_open (struct net_device * ndev)
{
    hdlc_device *hdlc = dev_to_hdlc (ndev);
    status_t    ret;

    hdlc->proto = IF_PROTO_HDLC;
    if ((ret = hdlc_open (hdlc)))
    {
        pr_info("hdlc_open failure, err %d.\n", ret);
        return ret;
    }
    if ((ret = c4_chan_up (DEV_TO_PRIV (hdlc)->ci, DEV_TO_PRIV (hdlc)->channum)))
        return -ret;
    MOD_INC_USE_COUNT;
    netif_start_queue (hdlc_to_dev (hdlc));
    return 0;                       /* no error = success */
}
#endif

#else

/** Linux 2.6 **/
STATIC int
chan_open (struct net_device * ndev)
{
    hdlc_device *hdlc = dev_to_hdlc (ndev);
    const struct c4_priv *priv = hdlc->priv;
    int         ret;

    if ((ret = hdlc_open (ndev)))
    {
        pr_info("hdlc_open failure, err %d.\n", ret);
        return ret;
    }
    if ((ret = c4_chan_up (priv->ci, priv->channum)))
        return -ret;
    try_module_get (THIS_MODULE);
    netif_start_queue (ndev);
    return 0;                       /* no error = success */
}
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#if !defined(GENERIC_HDLC_VERSION) || (GENERIC_HDLC_VERSION < 4)

/** Linux 2.4.18-19 **/
STATIC void
chan_close (hdlc_device * hdlc)
{
    netif_stop_queue (hdlc_to_dev (hdlc));
    musycc_chan_down ((ci_t *) 0, DEV_TO_PRIV (hdlc)->channum);
    MOD_DEC_USE_COUNT;
}
#else

/** Linux 2.4.20 and higher **/
STATIC int
chan_close (struct net_device * ndev)
{
    hdlc_device *hdlc = dev_to_hdlc (ndev);

    netif_stop_queue (hdlc_to_dev (hdlc));
    musycc_chan_down ((ci_t *) 0, DEV_TO_PRIV (hdlc)->channum);
    hdlc_close (hdlc);
    MOD_DEC_USE_COUNT;
    return 0;
}
#endif

#else

/** Linux 2.6 **/
STATIC int
chan_close (struct net_device * ndev)
{
    hdlc_device *hdlc = dev_to_hdlc (ndev);
    const struct c4_priv *priv = hdlc->priv;

    netif_stop_queue (ndev);
    musycc_chan_down ((ci_t *) 0, priv->channum);
    hdlc_close (ndev);
    module_put (THIS_MODULE);
    return 0;
}
#endif


#if !defined(GENERIC_HDLC_VERSION) || (GENERIC_HDLC_VERSION < 4)

/** Linux 2.4.18-19 **/
STATIC int
chan_ioctl (hdlc_device * hdlc, struct ifreq * ifr, int cmd)
{
    if (cmd == HDLCSCLOCK)
    {
        ifr->ifr_ifru.ifru_ivalue = LINE_DEFAULT;
        return 0;
    }
    return -EINVAL;
}
#endif


#if !defined(GENERIC_HDLC_VERSION) || (GENERIC_HDLC_VERSION < 4)
STATIC int
chan_dev_ioctl (struct net_device * hdlc, struct ifreq * ifr, int cmd)
{
    if (cmd == HDLCSCLOCK)
    {
        ifr->ifr_ifru.ifru_ivalue = LINE_DEFAULT;
        return 0;
    }
    return -EINVAL;
}
#else
STATIC int
chan_dev_ioctl (struct net_device * dev, struct ifreq * ifr, int cmd)
{
    return hdlc_ioctl (dev, ifr, cmd);
}


STATIC int
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
chan_attach_noop (hdlc_device * hdlc, unsigned short foo_1, unsigned short foo_2)
#else
chan_attach_noop (struct net_device * ndev, unsigned short foo_1, unsigned short foo_2)
#endif
{
    return 0;                   /* our driver has nothing to do here, show's
                                 * over, go home */
}
#endif


STATIC struct net_device_stats *
chan_get_stats (struct net_device * ndev)
{
    mch_t      *ch;
    struct net_device_stats *nstats;
    struct sbecom_chan_stats *stats;
    int         channum;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    channum = DEV_TO_PRIV (ndev)->channum;
#else
    {
        struct c4_priv *priv;

        priv = (struct c4_priv *) dev_to_hdlc (ndev)->priv;
        channum = priv->channum;
    }
#endif

    ch = c4_find_chan (channum);
    if (ch == NULL)
        return NULL;

    nstats = &ndev->stats;
    stats = &ch->s;

    memset (nstats, 0, sizeof (struct net_device_stats));
    nstats->rx_packets = stats->rx_packets;
    nstats->tx_packets = stats->tx_packets;
    nstats->rx_bytes = stats->rx_bytes;
    nstats->tx_bytes = stats->tx_bytes;
    nstats->rx_errors = stats->rx_length_errors +
        stats->rx_over_errors +
        stats->rx_crc_errors +
        stats->rx_frame_errors +
        stats->rx_fifo_errors +
        stats->rx_missed_errors;
    nstats->tx_errors = stats->tx_dropped +
        stats->tx_aborted_errors +
        stats->tx_fifo_errors;
    nstats->rx_dropped = stats->rx_dropped;
    nstats->tx_dropped = stats->tx_dropped;

    nstats->rx_length_errors = stats->rx_length_errors;
    nstats->rx_over_errors = stats->rx_over_errors;
    nstats->rx_crc_errors = stats->rx_crc_errors;
    nstats->rx_frame_errors = stats->rx_frame_errors;
    nstats->rx_fifo_errors = stats->rx_fifo_errors;
    nstats->rx_missed_errors = stats->rx_missed_errors;

    nstats->tx_aborted_errors = stats->tx_aborted_errors;
    nstats->tx_fifo_errors = stats->tx_fifo_errors;

    return nstats;
}


static ci_t *
get_ci_by_dev (struct net_device * ndev)
{
    return (ci_t *)(netdev_priv(ndev));
}


#if !defined(GENERIC_HDLC_VERSION) || (GENERIC_HDLC_VERSION < 4)
STATIC int
c4_linux_xmit (hdlc_device * hdlc, struct sk_buff * skb)
{
    int         rval;

    rval = musycc_start_xmit (DEV_TO_PRIV (hdlc)->ci, DEV_TO_PRIV (hdlc)->channum, skb);
    return -rval;
}
#else                           /* new */
STATIC int
c4_linux_xmit (struct sk_buff * skb, struct net_device * ndev)
{
    const struct c4_priv *priv;
    int         rval;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    priv = DEV_TO_PRIV (ndev);
#else
    hdlc_device *hdlc = dev_to_hdlc (ndev);

    priv = hdlc->priv;
#endif

    rval = musycc_start_xmit (priv->ci, priv->channum, skb);
    return -rval;
}
#endif                          /* GENERIC_HDLC_VERSION */

static const struct net_device_ops chan_ops = {
       .ndo_open       = chan_open,
       .ndo_stop       = chan_close,
       .ndo_start_xmit = c4_linux_xmit,
       .ndo_do_ioctl   = chan_dev_ioctl,
       .ndo_get_stats  = chan_get_stats,
};

STATIC struct net_device *
create_chan (struct net_device * ndev, ci_t * ci,
             struct sbecom_chan_param * cp)
{
    hdlc_device *hdlc;
    struct net_device *dev;
    hdw_info_t *hi;
    int         ret;

    if (c4_find_chan (cp->channum))
        return 0;                   /* channel already exists */

    {
        struct c4_priv *priv;

        /* allocate then fill in private data structure */
        priv = OS_kmalloc (sizeof (struct c4_priv));
        if (!priv)
        {
            pr_warning("%s: no memory for net_device !\n", ci->devname);
            return 0;
        }
        dev = alloc_hdlcdev (priv);
        if (!dev)
        {
            pr_warning("%s: no memory for hdlc_device !\n", ci->devname);
            OS_kfree (priv);
            return 0;
        }
        priv->ci = ci;
        priv->channum = cp->channum;
    }

    hdlc = dev_to_hdlc (dev);

    dev->base_addr = 0;             /* not I/O mapped */
    dev->irq = ndev->irq;
    dev->type = ARPHRD_RAWHDLC;
    *dev->name = 0;                 /* default ifconfig name = "hdlc" */

    hi = (hdw_info_t *) ci->hdw_info;
    if (hi->mfg_info_sts == EEPROM_OK)
    {
        switch (hi->promfmt)
        {
        case PROM_FORMAT_TYPE1:
            memcpy (dev->dev_addr, (FLD_TYPE1 *) (hi->mfg_info.pft1.Serial), 6);
            break;
        case PROM_FORMAT_TYPE2:
            memcpy (dev->dev_addr, (FLD_TYPE2 *) (hi->mfg_info.pft2.Serial), 6);
            break;
        default:
            memset (dev->dev_addr, 0, 6);
            break;
        }
    } else
    {
        memset (dev->dev_addr, 0, 6);
    }

    hdlc->xmit = c4_linux_xmit;

    dev->netdev_ops = &chan_ops;
    /*
     * The native hdlc stack calls this 'attach' routine during
     * hdlc_raw_ioctl(), passing parameters for line encoding and parity.
     * Since hdlc_raw_ioctl() stack does not interrogate whether an 'attach'
     * routine is actually registered or not, we supply a dummy routine which
     * does nothing (since encoding and parity are setup for our driver via a
     * special configuration application).
     */

    hdlc->attach = chan_attach_noop;

    rtnl_unlock ();                 /* needed due to Ioctl calling sequence */
    ret = register_hdlc_device (dev);
    /* NOTE: <stats> setting must occur AFTER registration in order to "take" */
    dev->tx_queue_len = MAX_DEFAULT_IFQLEN;

    rtnl_lock ();                   /* needed due to Ioctl calling sequence */
    if (ret)
    {
        if (log_level >= LOG_WARN)
            pr_info("%s: create_chan[%d] registration error = %d.\n",
                    ci->devname, cp->channum, ret);
        free_netdev (dev);          /* cleanup */
        return 0;                   /* failed to register */
    }
    return dev;
}


/* the idea here is to get port information and pass it back (using pointer) */
STATIC      status_t
do_get_port (struct net_device * ndev, void *data)
{
    int         ret;
    ci_t       *ci;             /* ci stands for card information */
    struct sbecom_port_param pp;/* copy data to kernel land */

    if (copy_from_user (&pp, data, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    if (pp.portnum >= MUSYCC_NPORTS)
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;             /* get card info */

    ret = mkret (c4_get_port (ci, pp.portnum));
    if (ret)
        return ret;
    if (copy_to_user (data, &ci->port[pp.portnum].p,
                      sizeof (struct sbecom_port_param)))
        return -EFAULT;
    return 0;
}

/* this function copys the user data and then calls the real action function */
STATIC      status_t
do_set_port (struct net_device * ndev, void *data)
{
    ci_t       *ci;             /* ci stands for card information */
    struct sbecom_port_param pp;/* copy data to kernel land */

    if (copy_from_user (&pp, data, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    if (pp.portnum >= MUSYCC_NPORTS)
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;             /* get card info */

    if (pp.portnum >= ci->max_port) /* sanity check */
        return ENXIO;

    memcpy (&ci->port[pp.portnum].p, &pp, sizeof (struct sbecom_port_param));
    return mkret (c4_set_port (ci, pp.portnum));
}

/* work the port loopback mode as per directed */
STATIC      status_t
do_port_loop (struct net_device * ndev, void *data)
{
    struct sbecom_port_param pp;
    ci_t       *ci;

    if (copy_from_user (&pp, data, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;
    return mkret (c4_loop_port (ci, pp.portnum, pp.port_mode));
}

/* set the specified register with the given value / or just read it */
STATIC      status_t
do_framer_rw (struct net_device * ndev, void *data)
{
    struct sbecom_port_param pp;
    ci_t       *ci;
    int         ret;

    if (copy_from_user (&pp, data, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;
    ret = mkret (c4_frame_rw (ci, &pp));
    if (ret)
        return ret;
    if (copy_to_user (data, &pp, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    return 0;
}

/* set the specified register with the given value / or just read it */
STATIC      status_t
do_pld_rw (struct net_device * ndev, void *data)
{
    struct sbecom_port_param pp;
    ci_t       *ci;
    int         ret;

    if (copy_from_user (&pp, data, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;
    ret = mkret (c4_pld_rw (ci, &pp));
    if (ret)
        return ret;
    if (copy_to_user (data, &pp, sizeof (struct sbecom_port_param)))
        return -EFAULT;
    return 0;
}

/* set the specified register with the given value / or just read it */
STATIC      status_t
do_musycc_rw (struct net_device * ndev, void *data)
{
    struct c4_musycc_param mp;
    ci_t       *ci;
    int         ret;

    if (copy_from_user (&mp, data, sizeof (struct c4_musycc_param)))
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;
    ret = mkret (c4_musycc_rw (ci, &mp));
    if (ret)
        return ret;
    if (copy_to_user (data, &mp, sizeof (struct c4_musycc_param)))
        return -EFAULT;
    return 0;
}

STATIC      status_t
do_get_chan (struct net_device * ndev, void *data)
{
    struct sbecom_chan_param cp;
    int         ret;

    if (copy_from_user (&cp, data,
                        sizeof (struct sbecom_chan_param)))
        return -EFAULT;

    if ((ret = mkret (c4_get_chan (cp.channum, &cp))))
        return ret;

    if (copy_to_user (data, &cp, sizeof (struct sbecom_chan_param)))
        return -EFAULT;
    return 0;
}

STATIC      status_t
do_set_chan (struct net_device * ndev, void *data)
{
    struct sbecom_chan_param cp;
    int         ret;
    ci_t       *ci;

    if (copy_from_user (&cp, data, sizeof (struct sbecom_chan_param)))
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;
    switch (ret = mkret (c4_set_chan (cp.channum, &cp)))
    {
    case 0:
        return 0;
    default:
        return ret;
    }
}

STATIC      status_t
do_create_chan (struct net_device * ndev, void *data)
{
    ci_t       *ci;
    struct net_device *dev;
    struct sbecom_chan_param cp;
    int         ret;

    if (copy_from_user (&cp, data, sizeof (struct sbecom_chan_param)))
        return -EFAULT;
    ci = get_ci_by_dev (ndev);
    if (!ci)
        return -EINVAL;
    dev = create_chan (ndev, ci, &cp);
    if (!dev)
        return -EBUSY;
    ret = mkret (c4_new_chan (ci, cp.port, cp.channum, dev));
    if (ret)
    {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
        rtnl_unlock ();             /* needed due to Ioctl calling sequence */
        V7 (unregister_hdlc_device) (dev_to_hdlc (dev));
        rtnl_lock ();               /* needed due to Ioctl calling sequence */
        OS_kfree (DEV_TO_PRIV (dev));
        OS_kfree (dev);
#else
        rtnl_unlock ();             /* needed due to Ioctl calling sequence */
        unregister_hdlc_device (dev);
        rtnl_lock ();               /* needed due to Ioctl calling sequence */
        free_netdev (dev);
#endif
    }
    return ret;
}

STATIC      status_t
do_get_chan_stats (struct net_device * ndev, void *data)
{
    struct c4_chan_stats_wrap ccs;
    int         ret;

    if (copy_from_user (&ccs, data,
                        sizeof (struct c4_chan_stats_wrap)))
        return -EFAULT;
    switch (ret = mkret (c4_get_chan_stats (ccs.channum, &ccs.stats)))
    {
    case 0:
        break;
    default:
        return ret;
    }
    if (copy_to_user (data, &ccs,
                      sizeof (struct c4_chan_stats_wrap)))
        return -EFAULT;
    return 0;
}
STATIC      status_t
do_set_loglevel (struct net_device * ndev, void *data)
{
    unsigned int log_level;

    if (copy_from_user (&log_level, data, sizeof (int)))
        return -EFAULT;
    sbecom_set_loglevel (log_level);
    return 0;
}

STATIC      status_t
do_deluser (struct net_device * ndev, int lockit)
{
    if (ndev->flags & IFF_UP)
        return -EBUSY;

    {
        ci_t       *ci;
        mch_t      *ch;
        const struct c4_priv *priv;
        int         channum;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
        priv = DEV_TO_PRIV (ndev);
#else
        priv = (struct c4_priv *) dev_to_hdlc (ndev)->priv;
#endif
        ci = priv->ci;
        channum = priv->channum;

        ch = c4_find_chan (channum);
        if (ch == NULL)
            return -ENOENT;
        ch->user = 0;               /* will be freed, below */
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    if (lockit)
        rtnl_unlock ();             /* needed if Ioctl calling sequence */
    V7 (unregister_hdlc_device) (dev_to_hdlc (ndev));
    if (lockit)
        rtnl_lock ();               /* needed if Ioctl calling sequence */
    OS_kfree (DEV_TO_PRIV (ndev));
    OS_kfree (ndev);
#else
    if (lockit)
        rtnl_unlock ();             /* needed if Ioctl calling sequence */
    unregister_hdlc_device (ndev);
    if (lockit)
        rtnl_lock ();               /* needed if Ioctl calling sequence */
    free_netdev (ndev);
#endif
    return 0;
}

int
do_del_chan (struct net_device * musycc_dev, void *data)
{
    struct sbecom_chan_param cp;
    char        buf[sizeof (CHANNAME) + 3];
    struct net_device *dev;
    int         ret;

    if (copy_from_user (&cp, data,
                        sizeof (struct sbecom_chan_param)))
        return -EFAULT;
    sprintf (buf, CHANNAME "%d", cp.channum);
    if (!(dev = dev_get_by_name (&init_net, buf)))
        return -ENOENT;
    dev_put (dev);
    ret = do_deluser (dev, 1);
    if (ret)
        return ret;
    return c4_del_chan (cp.channum);
}
int         c4_reset_board (void *);

int
do_reset (struct net_device * musycc_dev, void *data)
{
    const struct c4_priv *priv;
    int         i;

    for (i = 0; i < 128; i++)
    {
        struct net_device *ndev;
        char        buf[sizeof (CHANNAME) + 3];

        sprintf (buf, CHANNAME "%d", i);
        if (!(ndev = dev_get_by_name(&init_net, buf)))
            continue;
        priv = dev_to_hdlc (ndev)->priv;

        if ((unsigned long) (priv->ci) ==
            (unsigned long) (netdev_priv(musycc_dev)))
        {
            ndev->flags &= ~IFF_UP;
            dev_put (ndev);
            netif_stop_queue (ndev);
            do_deluser (ndev, 1);
        } else
            dev_put (ndev);
    }
    return 0;
}

int
do_reset_chan_stats (struct net_device * musycc_dev, void *data)
{
    struct sbecom_chan_param cp;

    if (copy_from_user (&cp, data,
                        sizeof (struct sbecom_chan_param)))
        return -EFAULT;
    return mkret (c4_del_chan_stats (cp.channum));
}

STATIC      status_t
c4_ioctl (struct net_device * ndev, struct ifreq * ifr, int cmd)
{
    ci_t       *ci;
    void       *data;
    int         iocmd, iolen;
    status_t    ret;
    static struct data
    {
        union
        {
            u_int8_t c;
            u_int32_t i;
            struct sbe_brd_info bip;
            struct sbe_drv_info dip;
            struct sbe_iid_info iip;
            struct sbe_brd_addr bap;
            struct sbecom_chan_stats stats;
            struct sbecom_chan_param param;
            struct temux_card_stats cards;
            struct sbecom_card_param cardp;
            struct sbecom_framer_param frp;
        }           u;
    }           arg;


    if (!capable (CAP_SYS_ADMIN))
        return -EPERM;
    if (cmd != SIOCDEVPRIVATE + 15)
        return -EINVAL;
    if (!(ci = get_ci_by_dev (ndev)))
        return -EINVAL;
    if (ci->state != C_RUNNING)
        return -ENODEV;
    if (copy_from_user (&iocmd, ifr->ifr_data, sizeof (iocmd)))
        return -EFAULT;
#if 0
    if (copy_from_user (&len, ifr->ifr_data + sizeof (iocmd), sizeof (len)))
        return -EFAULT;
#endif

#if 0
    pr_info("c4_ioctl: iocmd %x, dir %x type %x nr %x iolen %d.\n", iocmd,
            _IOC_DIR (iocmd), _IOC_TYPE (iocmd), _IOC_NR (iocmd),
            _IOC_SIZE (iocmd));
#endif
    iolen = _IOC_SIZE (iocmd);
    data = ifr->ifr_data + sizeof (iocmd);
    if (copy_from_user (&arg, data, iolen))
        return -EFAULT;

    ret = 0;
    switch (iocmd)
    {
    case SBE_IOC_PORT_GET:
        //pr_info(">> SBE_IOC_PORT_GET Ioctl...\n");
        ret = do_get_port (ndev, data);
        break;
    case SBE_IOC_PORT_SET:
        //pr_info(">> SBE_IOC_PORT_SET Ioctl...\n");
        ret = do_set_port (ndev, data);
        break;
    case SBE_IOC_CHAN_GET:
        //pr_info(">> SBE_IOC_CHAN_GET Ioctl...\n");
        ret = do_get_chan (ndev, data);
        break;
    case SBE_IOC_CHAN_SET:
        //pr_info(">> SBE_IOC_CHAN_SET Ioctl...\n");
        ret = do_set_chan (ndev, data);
        break;
    case C4_DEL_CHAN:
        //pr_info(">> C4_DEL_CHAN Ioctl...\n");
        ret = do_del_chan (ndev, data);
        break;
    case SBE_IOC_CHAN_NEW:
        ret = do_create_chan (ndev, data);
        break;
    case SBE_IOC_CHAN_GET_STAT:
        ret = do_get_chan_stats (ndev, data);
        break;
    case SBE_IOC_LOGLEVEL:
        ret = do_set_loglevel (ndev, data);
        break;
    case SBE_IOC_RESET_DEV:
        ret = do_reset (ndev, data);
        break;
    case SBE_IOC_CHAN_DEL_STAT:
        ret = do_reset_chan_stats (ndev, data);
        break;
    case C4_LOOP_PORT:
        ret = do_port_loop (ndev, data);
        break;
    case C4_RW_FRMR:
        ret = do_framer_rw (ndev, data);
        break;
    case C4_RW_MSYC:
        ret = do_musycc_rw (ndev, data);
        break;
    case C4_RW_PLD:
        ret = do_pld_rw (ndev, data);
        break;
    case SBE_IOC_IID_GET:
        ret = (iolen == sizeof (struct sbe_iid_info)) ? c4_get_iidinfo (ci, &arg.u.iip) : -EFAULT;
        if (ret == 0)               /* no error, copy data */
            if (copy_to_user (data, &arg, iolen))
                return -EFAULT;
        break;
    default:
        //pr_info(">> c4_ioctl: EINVAL - unknown iocmd <%x>\n", iocmd);
        ret = -EINVAL;
        break;
    }
    return mkret (ret);
}

static const struct net_device_ops c4_ops = {
       .ndo_open       = void_open,
       .ndo_start_xmit = c4_linux_xmit,
       .ndo_do_ioctl   = c4_ioctl,
};

static void c4_setup(struct net_device *dev)
{
       dev->type = ARPHRD_VOID;
       dev->netdev_ops = &c4_ops;
}

struct net_device *__init
c4_add_dev (hdw_info_t * hi, int brdno, unsigned long f0, unsigned long f1,
            int irq0, int irq1)
{
    struct net_device *ndev;
    ci_t       *ci;

    ndev = alloc_netdev(sizeof(ci_t), SBE_IFACETMPL, c4_setup);
    if (!ndev)
    {
        pr_warning("%s: no memory for struct net_device !\n", hi->devname);
        error_flag = ENOMEM;
        return 0;
    }
    ci = (ci_t *)(netdev_priv(ndev));
    ndev->irq = irq0;

    ci->hdw_info = hi;
    ci->state = C_INIT;         /* mark as hardware not available */
    ci->next = c4_list;
    c4_list = ci;
    ci->brdno = ci->next ? ci->next->brdno + 1 : 0;

    if (CI == 0)
        CI = ci;                    /* DEBUG, only board 0 usage */

    strcpy (ci->devname, hi->devname);
    ci->release = &pmcc4_OSSI_release[0];

    /* tasklet */
#if defined(SBE_ISR_TASKLET)
    tasklet_init (&ci->ci_musycc_isr_tasklet,
                  (void (*) (unsigned long)) musycc_intr_bh_tasklet,
                  (unsigned long) ci);

    if (atomic_read (&ci->ci_musycc_isr_tasklet.count) == 0)
        tasklet_disable_nosync (&ci->ci_musycc_isr_tasklet);
#elif defined(SBE_ISR_IMMEDIATE)
    ci->ci_musycc_isr_tq.routine = (void *) (unsigned long) musycc_intr_bh_tasklet;
    ci->ci_musycc_isr_tq.data = ci;
#endif


    if (register_netdev (ndev) ||
        (c4_init (ci, (u_char *) f0, (u_char *) f1) != SBE_DRVR_SUCCESS))
    {
        OS_kfree (netdev_priv(ndev));
        OS_kfree (ndev);
        error_flag = ENODEV;
        return 0;
    }
    /*************************************************************
     *  int request_irq(unsigned int irq,
     *                  void (*handler)(int, void *, struct pt_regs *),
     *                  unsigned long flags, const char *dev_name, void *dev_id);
     *  wherein:
     *  irq      -> The interrupt number that is being requested.
     *  handler  -> Pointer to handling function being installed.
     *  flags    -> A bit mask of options related to interrupt management.
     *  dev_name -> String used in /proc/interrupts to show owner of interrupt.
     *  dev_id   -> Pointer (for shared interrupt lines) to point to its own
     *              private data area (to identify which device is interrupting).
     *
     *  extern void free_irq(unsigned int irq, void *dev_id);
     **************************************************************/

    if (request_irq (irq0, &c4_linux_interrupt,
#if defined(SBE_ISR_TASKLET)
                     IRQF_DISABLED | IRQF_SHARED,
#elif defined(SBE_ISR_IMMEDIATE)
                     IRQF_DISABLED | IRQF_SHARED,
#elif defined(SBE_ISR_INLINE)
                     IRQF_SHARED,
#endif
                     ndev->name, ndev))
    {
        pr_warning("%s: MUSYCC could not get irq: %d\n", ndev->name, irq0);
        unregister_netdev (ndev);
        OS_kfree (netdev_priv(ndev));
        OS_kfree (ndev);
        error_flag = EIO;
        return 0;
    }
#ifdef CONFIG_SBE_PMCC4_NCOMM
    if (request_irq (irq1, &c4_ebus_interrupt, IRQF_SHARED, ndev->name, ndev))
    {
        pr_warning("%s: EBUS could not get irq: %d\n", hi->devname, irq1);
        unregister_netdev (ndev);
        free_irq (irq0, ndev);
        OS_kfree (netdev_priv(ndev));
        OS_kfree (ndev);
        error_flag = EIO;
        return 0;
    }
#endif

    /* setup board identification information */

    {
        u_int32_t   tmp;

        hdw_sn_get (hi, brdno);     /* also sets PROM format type (promfmt)
                                     * for later usage */

        switch (hi->promfmt)
        {
        case PROM_FORMAT_TYPE1:
            memcpy (ndev->dev_addr, (FLD_TYPE1 *) (hi->mfg_info.pft1.Serial), 6);
            memcpy (&tmp, (FLD_TYPE1 *) (hi->mfg_info.pft1.Id), 4);     /* unaligned data
                                                                         * acquisition */
            ci->brd_id = cpu_to_be32 (tmp);
            break;
        case PROM_FORMAT_TYPE2:
            memcpy (ndev->dev_addr, (FLD_TYPE2 *) (hi->mfg_info.pft2.Serial), 6);
            memcpy (&tmp, (FLD_TYPE2 *) (hi->mfg_info.pft2.Id), 4);     /* unaligned data
                                                                         * acquisition */
            ci->brd_id = cpu_to_be32 (tmp);
            break;
        default:
            ci->brd_id = 0;
            memset (ndev->dev_addr, 0, 6);
            break;
        }

#if 1
        sbeid_set_hdwbid (ci);      /* requires bid to be preset */
#else
        sbeid_set_bdtype (ci);      /* requires hdw_bid to be preset */
#endif

    }

#ifdef CONFIG_PROC_FS
    sbecom_proc_brd_init (ci);
#endif
#if defined(SBE_ISR_TASKLET)
    tasklet_enable (&ci->ci_musycc_isr_tasklet);
#endif


    if ((error_flag = c4_init2 (ci)) != SBE_DRVR_SUCCESS)
    {
#ifdef CONFIG_PROC_FS
        sbecom_proc_brd_cleanup (ci);
#endif
        unregister_netdev (ndev);
        free_irq (irq1, ndev);
        free_irq (irq0, ndev);
        OS_kfree (netdev_priv(ndev));
        OS_kfree (ndev);
        return 0;                   /* failure, error_flag is set */
    }
    return ndev;
}

STATIC int  __init
c4_mod_init (void)
{
    int         rtn;

    pr_warning("%s\n", pmcc4_OSSI_release);
    if ((rtn = c4hw_attach_all ()))
        return -rtn;                /* installation failure - see system log */

    /* housekeeping notifications */
    if (log_level != log_level_default)
        pr_info("NOTE: driver parameter <log_level> changed from default %d to %d.\n",
                log_level_default, log_level);
    if (max_mru != max_mru_default)
        pr_info("NOTE: driver parameter <max_mru> changed from default %d to %d.\n",
                max_mru_default, max_mru);
    if (max_mtu != max_mtu_default)
        pr_info("NOTE: driver parameter <max_mtu> changed from default %d to %d.\n",
                max_mtu_default, max_mtu);
    if (max_rxdesc_used != max_rxdesc_default)
    {
        if (max_rxdesc_used > 2000)
            max_rxdesc_used = 2000; /* out-of-bounds reset */
        pr_info("NOTE: driver parameter <max_rxdesc_used> changed from default %d to %d.\n",
                max_rxdesc_default, max_rxdesc_used);
    }
    if (max_txdesc_used != max_txdesc_default)
    {
        if (max_txdesc_used > 1000)
            max_txdesc_used = 1000; /* out-of-bounds reset */
        pr_info("NOTE: driver parameter <max_txdesc_used> changed from default %d to %d.\n",
                max_txdesc_default, max_txdesc_used);
    }
    return 0;                       /* installation success */
}


 /*
  * find any still allocated hdlc registrations and unregister via call to
  * do_deluser()
  */

STATIC void __exit
cleanup_hdlc (void)
{
    hdw_info_t *hi;
    ci_t       *ci;
    struct net_device *ndev;
    int         i, j, k;

    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        if (hi->ndev)               /* a board has been attached */
        {
            ci = (ci_t *)(netdev_priv(hi->ndev));
            for (j = 0; j < ci->max_port; j++)
                for (k = 0; k < MUSYCC_NCHANS; k++)
                    if ((ndev = ci->port[j].chan[k]->user))
                    {
                        do_deluser (ndev, 0);
                    }
        }
    }
}


STATIC void __exit
c4_mod_remove (void)
{
    cleanup_hdlc ();            /* delete any missed channels */
    cleanup_devs ();
    c4_cleanup ();
    cleanup_ioremap ();
    pr_info("SBE - driver removed.\n");
}

module_init (c4_mod_init);
module_exit (c4_mod_remove);

#ifndef SBE_INCLUDE_SYMBOLS
#ifndef CONFIG_SBE_WANC24_NCOMM
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
EXPORT_NO_SYMBOLS;
#endif
#endif
#endif

MODULE_AUTHOR ("SBE Technical Services <support@sbei.com>");
MODULE_DESCRIPTION ("wanPCI-CxT1E1 Generic HDLC WAN Driver module");
#ifdef MODULE_LICENSE
MODULE_LICENSE ("GPL");
#endif

/***  End-of-File  ***/
