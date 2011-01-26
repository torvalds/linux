/*
 * $Id: musycc.c,v 2.1 2007/08/15 23:32:17 rickd PMCC4_3_1B $
 */

unsigned int max_intcnt = 0;
unsigned int max_bh = 0;

/*-----------------------------------------------------------------------------
 * musycc.c -
 *
 * Copyright (C) 2007  One Stop Systems, Inc.
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
 *
 * For further information, contact via email: support@onestopsystems.com
 * One Stop Systems, Inc.  Escondido, California  U.S.A.
 *-----------------------------------------------------------------------------
 * RCS info:
 * RCS revision: $Revision: 2.1 $
 * Last changed on $Date: 2007/08/15 23:32:17 $
 * Changed by $Author: rickd $
 *-----------------------------------------------------------------------------
 * $Log: musycc.c,v $
 * Revision 2.1  2007/08/15 23:32:17  rickd
 * Use 'if 0' instead of GNU comment delimeter to avoid line wrap induced compiler errors.
 *
 * Revision 2.0  2007/08/15 22:13:20  rickd
 * Update to printf pointer %p usage and correct some UINT to ULONG for
 * 64bit comptibility.
 *
 * Revision 1.7  2006/04/21 00:56:40  rickd
 * workqueue files now prefixed with <sbecom> prefix.
 *
 * Revision 1.6  2005/10/27 18:54:19  rickd
 * Clean out old code.  Default to HDLC_FCS16, not TRANS.
 *
 * Revision 1.5  2005/10/17 23:55:28  rickd
 * Initial port of NCOMM support patches from original work found
 * in pmc_c4t1e1 as updated by NCOMM.  Ref: CONFIG_SBE_PMCC4_NCOMM.
 *
 * Revision 1.4  2005/10/13 20:35:25  rickd
 * Cleanup warning for unused <flags> variable.
 *
 * Revision 1.3  2005/10/13 19:19:22  rickd
 * Disable redundant driver removal cleanup code.
 *
 * Revision 1.2  2005/10/11 18:36:16  rickd
 * Clean up warning messages caused by de-implemented some <flags> associated
 * with spin_lock() removals.
 *
 * Revision 1.1  2005/10/05 00:45:28  rickd
 * Re-enable xmit on flow-controlled and full channel to fix restart hang.
 * Add some temp spin-lock debug code (rld_spin_owner).
 *
 * Revision 1.0  2005/09/28 00:10:06  rickd
 * Initial release for C4T1E1 support. Lots of transparent
 * mode updates.
 *
 *-----------------------------------------------------------------------------
 */

char        SBEid_pmcc4_musyccc[] =
"@(#)musycc.c - $Revision: 2.1 $      (c) Copyright 2004-2006 SBE, Inc.";

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include "pmcc4_sysdep.h"
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include "sbecom_inline_linux.h"
#include "libsbew.h"
#include "pmcc4_private.h"
#include "pmcc4.h"
#include "musycc.h"

#ifdef SBE_INCLUDE_SYMBOLS
#define STATIC
#else
#define STATIC  static
#endif

#define sd_find_chan(ci,ch)   c4_find_chan(ch)


/*******************************************************************/
/* global driver variables */
extern ci_t *c4_list;
extern int  drvr_state;
extern int  cxt1e1_log_level;

extern int  cxt1e1_max_mru;
extern int  cxt1e1_max_mtu;
extern int  max_rxdesc_used;
extern int  max_txdesc_used;
extern ci_t *CI;                /* dummy pointr to board ZEROE's data - DEBUG
                                 * USAGE */


/*******************************************************************/
/* forward references */
void        c4_fifo_free (mpi_t *, int);
void        c4_wk_chan_restart (mch_t *);
void        musycc_bh_tx_eom (mpi_t *, int);
int         musycc_chan_up (ci_t *, int);
status_t __init musycc_init (ci_t *);
STATIC void __init musycc_init_port (mpi_t *);
void        musycc_intr_bh_tasklet (ci_t *);
void        musycc_serv_req (mpi_t *, u_int32_t);
void        musycc_update_timeslots (mpi_t *);

/*******************************************************************/

#if 1
STATIC int
musycc_dump_rxbuffer_ring (mch_t * ch, int lockit)
{
    struct mdesc *m;
    unsigned long flags = 0;

    u_int32_t status;
    int         n;

    if (lockit)
    {
        spin_lock_irqsave (&ch->ch_rxlock, flags);
    }
    if (ch->rxd_num == 0)
    {
        pr_info("  ZERO receive buffers allocated for this channel.");
    } else
    {
        FLUSH_MEM_READ ();
        m = &ch->mdr[ch->rxix_irq_srv];
        for (n = ch->rxd_num; n; n--)
        {
            status = le32_to_cpu (m->status);
            {
                pr_info("%c  %08lx[%2d]: sts %08x (%c%c%c%c:%d.) Data [%08x] Next [%08x]\n",
                        (m == &ch->mdr[ch->rxix_irq_srv]) ? 'F' : ' ',
                        (unsigned long) m, n,
                        status,
                        m->data ? (status & HOST_RX_OWNED ? 'H' : 'M') : '-',
                        status & POLL_DISABLED ? 'P' : '-',
                        status & EOBIRQ_ENABLE ? 'b' : '-',
                        status & EOMIRQ_ENABLE ? 'm' : '-',
                        status & LENGTH_MASK,
                        le32_to_cpu (m->data), le32_to_cpu (m->next));
#ifdef RLD_DUMP_BUFDATA
                {
                    u_int32_t  *dp;
                    int         len = status & LENGTH_MASK;

#if 1
                    if (m->data && (status & HOST_RX_OWNED))
#else
                    if (m->data)    /* always dump regardless of valid RX
                                     * data */
#endif
                    {
                        dp = (u_int32_t *) OS_phystov ((void *) (le32_to_cpu (m->data)));
                        if (len >= 0x10)
                            pr_info("    %x[%x]: %08X %08X %08X %08x\n", (u_int32_t) dp, len,
                                    *dp, *(dp + 1), *(dp + 2), *(dp + 3));
                        else if (len >= 0x08)
                            pr_info("    %x[%x]: %08X %08X\n", (u_int32_t) dp, len,
                                    *dp, *(dp + 1));
                        else
                            pr_info("    %x[%x]: %08X\n", (u_int32_t) dp, len, *dp);
                    }
                }
#endif
            }
            m = m->snext;
        }
    }                               /* -for- */
    pr_info("\n");

    if (lockit)
    {
        spin_unlock_irqrestore (&ch->ch_rxlock, flags);
    }
    return 0;
}
#endif

#if 1
STATIC int
musycc_dump_txbuffer_ring (mch_t * ch, int lockit)
{
    struct mdesc *m;
    unsigned long flags = 0;
    u_int32_t   status;
    int         n;

    if (lockit)
    {
        spin_lock_irqsave (&ch->ch_txlock, flags);
    }
    if (ch->txd_num == 0)
    {
        pr_info("  ZERO transmit buffers allocated for this channel.");
    } else
    {
        FLUSH_MEM_READ ();
        m = ch->txd_irq_srv;
        for (n = ch->txd_num; n; n--)
        {
            status = le32_to_cpu (m->status);
            {
                pr_info("%c%c %08lx[%2d]: sts %08x (%c%c%c%c:%d.) Data [%08x] Next [%08x]\n",
                        (m == ch->txd_usr_add) ? 'F' : ' ',
                        (m == ch->txd_irq_srv) ? 'L' : ' ',
                        (unsigned long) m, n,
                        status,
                     m->data ? (status & MUSYCC_TX_OWNED ? 'M' : 'H') : '-',
                        status & POLL_DISABLED ? 'P' : '-',
                        status & EOBIRQ_ENABLE ? 'b' : '-',
                        status & EOMIRQ_ENABLE ? 'm' : '-',
                        status & LENGTH_MASK,
                        le32_to_cpu (m->data), le32_to_cpu (m->next));
#ifdef RLD_DUMP_BUFDATA
                {
                    u_int32_t  *dp;
                    int         len = status & LENGTH_MASK;

                    if (m->data)
                    {
                        dp = (u_int32_t *) OS_phystov ((void *) (le32_to_cpu (m->data)));
                        if (len >= 0x10)
                            pr_info("    %x[%x]: %08X %08X %08X %08x\n", (u_int32_t) dp, len,
                                    *dp, *(dp + 1), *(dp + 2), *(dp + 3));
                        else if (len >= 0x08)
                            pr_info("    %x[%x]: %08X %08X\n", (u_int32_t) dp, len,
                                    *dp, *(dp + 1));
                        else
                            pr_info("    %x[%x]: %08X\n", (u_int32_t) dp, len, *dp);
                    }
                }
#endif
            }
            m = m->snext;
        }
    }                               /* -for- */
    pr_info("\n");

    if (lockit)
    {
        spin_unlock_irqrestore (&ch->ch_txlock, flags);
    }
    return 0;
}
#endif


/*
 * The following supports a backdoor debug facility which can be used to
 * display the state of a board's channel.
 */

status_t
musycc_dump_ring (ci_t * ci, unsigned int chan)
{
    mch_t      *ch;

    if (chan >= MAX_CHANS_USED)
    {
        return SBE_DRVR_FAIL;       /* E2BIG */
    }
    {
        int         bh;

        bh = atomic_read (&ci->bh_pending);
        pr_info(">> bh_pend %d [%d] ihead %d itail %d [%d] th_cnt %d bh_cnt %d wdcnt %d note %d\n",
                bh, max_bh, ci->iqp_headx, ci->iqp_tailx, max_intcnt,
                ci->intlog.drvr_intr_thcount,
                ci->intlog.drvr_intr_bhcount,
                ci->wdcount, ci->wd_notify);
        max_bh = 0;                 /* reset counter */
        max_intcnt = 0;             /* reset counter */
    }

    if (!(ch = sd_find_chan (dummy, chan)))
    {
        pr_info(">> musycc_dump_ring: channel %d not up.\n", chan);
        return ENOENT;
    }
    pr_info(">> CI %p CHANNEL %3d @ %p: state %x status/p %x/%x\n", ci, chan, ch, ch->state,
            ch->status, ch->p.status);
    pr_info("--------------------------------\nTX Buffer Ring - Channel %d, txd_num %d. (bd/ch pend %d %d), TXD required %d, txpkt %lu\n",
            chan, ch->txd_num,
            (u_int32_t) atomic_read (&ci->tx_pending), (u_int32_t) atomic_read (&ch->tx_pending), ch->txd_required, ch->s.tx_packets);
    pr_info("++ User 0x%p IRQ_SRV 0x%p USR_ADD 0x%p QStopped %x, start_tx %x tx_full %d txd_free %d mode %x\n",
            ch->user, ch->txd_irq_srv, ch->txd_usr_add,
            sd_queue_stopped (ch->user),
            ch->ch_start_tx, ch->tx_full, ch->txd_free, ch->p.chan_mode);
    musycc_dump_txbuffer_ring (ch, 1);
    pr_info("RX Buffer Ring - Channel %d, rxd_num %d. IRQ_SRV[%d] 0x%p, start_rx %x rxpkt %lu\n",
            chan, ch->rxd_num, ch->rxix_irq_srv,
            &ch->mdr[ch->rxix_irq_srv], ch->ch_start_rx, ch->s.rx_packets);
    musycc_dump_rxbuffer_ring (ch, 1);

    return SBE_DRVR_SUCCESS;
}


status_t
musycc_dump_rings (ci_t * ci, unsigned int start_chan)
{
    unsigned int chan;

    for (chan = start_chan; chan < (start_chan + 5); chan++)
        musycc_dump_ring (ci, chan);
    return SBE_DRVR_SUCCESS;
}


/*
 * NOTE on musycc_init_mdt():  These MUSYCC writes are only operational after
 * a MUSYCC GROUP_INIT command has been issued.
 */

void
musycc_init_mdt (mpi_t * pi)
{
    u_int32_t  *addr, cfg;
    int         i;

    /*
     * This Idle Code insertion takes effect prior to channel's first
     * transmitted  message.  After that, each message contains its own Idle
     * Code information which is to be issued after the message is
     * transmitted (Ref.MUSYCC 5.2.2.3: MCENBL bit in Group Configuration
     * Descriptor).
     */

    addr = (u_int32_t *) ((u_long) pi->reg + MUSYCC_MDT_BASE03_ADDR);
    cfg = CFG_CH_FLAG_7E << IDLE_CODE;

    for (i = 0; i < 32; addr++, i++)
    {
        pci_write_32 (addr, cfg);
    }
}


/* Set TX thp to the next unprocessed md */

void
musycc_update_tx_thp (mch_t * ch)
{
    struct mdesc *md;
    unsigned long flags;

    spin_lock_irqsave (&ch->ch_txlock, flags);
    while (1)
    {
        md = ch->txd_irq_srv;
        FLUSH_MEM_READ ();
        if (!md->data)
        {
            /* No MDs with buffers to process */
            spin_unlock_irqrestore (&ch->ch_txlock, flags);
            return;
        }
        if ((le32_to_cpu (md->status)) & MUSYCC_TX_OWNED)
        {
            /* this is the MD to restart TX with */
            break;
        }
        /*
         * Otherwise, we have a valid, host-owned message descriptor which
         * has been successfully transmitted and whose buffer can be freed,
         * so... process this MD, it's owned by the host.  (This might give
         * as a new, updated txd_irq_srv.)
         */
        musycc_bh_tx_eom (ch->up, ch->gchan);
    }
    md = ch->txd_irq_srv;
    ch->up->regram->thp[ch->gchan] = cpu_to_le32 (OS_vtophys (md));
    FLUSH_MEM_WRITE ();

    if (ch->tx_full)
    {
        ch->tx_full = 0;
        ch->txd_required = 0;
        sd_enable_xmit (ch->user);  /* re-enable to catch flow controlled
                                     * channel */
    }
    spin_unlock_irqrestore (&ch->ch_txlock, flags);

#ifdef RLD_TRANS_DEBUG
    pr_info("++ musycc_update_tx_thp[%d]: setting thp = %p, sts %x\n", ch->channum, md, md->status);
#endif
}


/*
 * This is the workq task executed by the OS when our queue_work() is
 * scheduled and run.  It can fire off either RX or TX ACTIVATION depending
 * upon the channel's ch_start_tx and ch_start_rx variables.  This routine
 * is implemented as a work queue so that the call to the service request is
 * able to sleep, awaiting an interrupt acknowledgment response (SACK) from
 * the hardware.
 */

void
musycc_wq_chan_restart (void *arg)      /* channel private structure */
{
    mch_t      *ch;
    mpi_t      *pi;
    struct mdesc *md;
#if 0
    unsigned long flags;
#endif

    ch = container_of(arg, struct c4_chan_info, ch_work);
    pi = ch->up;

#ifdef RLD_TRANS_DEBUG
    pr_info("wq_chan_restart[%d]: start_RT[%d/%d] status %x\n",
            ch->channum, ch->ch_start_rx, ch->ch_start_tx, ch->status);

#endif

    /**********************************/
    /** check for RX restart request **/
    /**********************************/

    if ((ch->ch_start_rx) && (ch->status & RX_ENABLED))
    {

        ch->ch_start_rx = 0;
#if defined(RLD_TRANS_DEBUG) || defined(RLD_RXACT_DEBUG)
        {
            static int  hereb4 = 7;

            if (hereb4)             /* RLD DEBUG */
            {
                hereb4--;
#ifdef RLD_TRANS_DEBUG
                md = &ch->mdr[ch->rxix_irq_srv];
                pr_info("++ musycc_wq_chan_restart[%d] CHAN RX ACTIVATE: rxix_irq_srv %d, md %p sts %x, rxpkt %lu\n",
                ch->channum, ch->rxix_irq_srv, md, le32_to_cpu (md->status),
                        ch->s.rx_packets);
#elif defined(RLD_RXACT_DEBUG)
                md = &ch->mdr[ch->rxix_irq_srv];
                pr_info("++ musycc_wq_chan_restart[%d] CHAN RX ACTIVATE: rxix_irq_srv %d, md %p sts %x, rxpkt %lu\n",
                ch->channum, ch->rxix_irq_srv, md, le32_to_cpu (md->status),
                        ch->s.rx_packets);
                musycc_dump_rxbuffer_ring (ch, 1);      /* RLD DEBUG */
#endif
            }
        }
#endif
        musycc_serv_req (pi, SR_CHANNEL_ACTIVATE | SR_RX_DIRECTION | ch->gchan);
    }
    /**********************************/
    /** check for TX restart request **/
    /**********************************/

    if ((ch->ch_start_tx) && (ch->status & TX_ENABLED))
    {
        /* find next unprocessed message, then set TX thp to it */
        musycc_update_tx_thp (ch);

#if 0
        spin_lock_irqsave (&ch->ch_txlock, flags);
#endif
        md = ch->txd_irq_srv;
        if (!md)
        {
#ifdef RLD_TRANS_DEBUG
            pr_info("-- musycc_wq_chan_restart[%d]: WARNING, starting NULL md\n", ch->channum);
#endif
#if 0
            spin_unlock_irqrestore (&ch->ch_txlock, flags);
#endif
        } else if (md->data && ((le32_to_cpu (md->status)) & MUSYCC_TX_OWNED))
        {
            ch->ch_start_tx = 0;
#if 0
            spin_unlock_irqrestore (&ch->ch_txlock, flags);   /* allow interrupts for service request */
#endif
#ifdef RLD_TRANS_DEBUG
            pr_info("++ musycc_wq_chan_restart() CHAN TX ACTIVATE: chan %d txd_irq_srv %p = sts %x, txpkt %lu\n",
                    ch->channum, ch->txd_irq_srv, ch->txd_irq_srv->status, ch->s.tx_packets);
#endif
            musycc_serv_req (pi, SR_CHANNEL_ACTIVATE | SR_TX_DIRECTION | ch->gchan);
        }
#ifdef RLD_RESTART_DEBUG
        else
        {
            /* retain request to start until retried and we have data to xmit */
            pr_info("-- musycc_wq_chan_restart[%d]: DELAYED due to md %p sts %x data %x, start_tx %x\n",
                    ch->channum, md,
                    le32_to_cpu (md->status),
                    le32_to_cpu (md->data), ch->ch_start_tx);
            musycc_dump_txbuffer_ring (ch, 0);
#if 0
            spin_unlock_irqrestore (&ch->ch_txlock, flags);   /* allow interrupts for service request */
#endif
        }
#endif
    }
}


 /*
  * Channel restart either fires of a workqueue request (2.6) or lodges a
  * watchdog activation sequence (2.4).
  */

void
musycc_chan_restart (mch_t * ch)
{
#ifdef RLD_RESTART_DEBUG
    pr_info("++ musycc_chan_restart[%d]: txd_irq_srv @ %p = sts %x\n",
            ch->channum, ch->txd_irq_srv, ch->txd_irq_srv->status);
#endif

    /* 2.6 - find next unprocessed message, then set TX thp to it */
#ifdef RLD_RESTART_DEBUG
    pr_info(">> musycc_chan_restart: scheduling Chan %x workQ @ %p\n", ch->channum, &ch->ch_work);
#endif
    c4_wk_chan_restart (ch);        /* work queue mechanism fires off: Ref:
                                     * musycc_wq_chan_restart () */

}


void
rld_put_led (mpi_t * pi, u_int32_t ledval)
{
    static u_int32_t led = 0;

    if (ledval == 0)
        led = 0;
    else
        led |= ledval;

    pci_write_32 ((u_int32_t *) &pi->up->cpldbase->leds, led);  /* RLD DEBUG TRANHANG */
}


#define MUSYCC_SR_RETRY_CNT  9

void
musycc_serv_req (mpi_t * pi, u_int32_t req)
{
    volatile u_int32_t r;
    int         rcnt;

    /*
     * PORT NOTE: Semaphore protect service loop guarantees only a single
     * operation at a time.  Per MUSYCC Manual - "Issuing service requests to
     * the same channel group without first receiving ACK from each request
     * may cause the host to lose track of which service request has been
     * acknowledged."
     */

    SD_SEM_TAKE (&pi->sr_sem_busy, "serv");     /* only 1 thru here, per
                                                 * group */

    if (pi->sr_last == req)
    {
#ifdef RLD_TRANS_DEBUG
        pr_info(">> same SR, Port %d Req %x\n", pi->portnum, req);
#endif

        /*
         * The most likely repeated request is the channel activation command
         * which follows the occurrence of a Transparent mode TX ONR or a
         * BUFF error.  If the previous command was a CHANNEL ACTIVATE,
         * precede it with a NOOP command in order maintain coherent control
         * of this current (re)ACTIVATE.
         */

        r = (pi->sr_last & ~SR_GCHANNEL_MASK);
        if ((r == (SR_CHANNEL_ACTIVATE | SR_TX_DIRECTION)) ||
            (r == (SR_CHANNEL_ACTIVATE | SR_RX_DIRECTION)))
        {
#ifdef RLD_TRANS_DEBUG
            pr_info(">> same CHAN ACT SR, Port %d Req %x => issue SR_NOOP CMD\n", pi->portnum, req);
#endif
            SD_SEM_GIVE (&pi->sr_sem_busy);     /* allow this next request */
            musycc_serv_req (pi, SR_NOOP);
            SD_SEM_TAKE (&pi->sr_sem_busy, "serv");     /* relock & continue w/
                                                         * original req */
        } else if (req == SR_NOOP)
        {
            /* no need to issue back-to-back SR_NOOP commands at this time */
#ifdef RLD_TRANS_DEBUG
            pr_info(">> same Port SR_NOOP skipped, Port %d\n", pi->portnum);
#endif
            SD_SEM_GIVE (&pi->sr_sem_busy);     /* allow this next request */
            return;
        }
    }
    rcnt = 0;
    pi->sr_last = req;
rewrite:
    pci_write_32 ((u_int32_t *) &pi->reg->srd, req);
    FLUSH_MEM_WRITE ();

    /*
     * Per MUSYCC Manual, Section 6.1,2 - "When writing an SCR service
     * request, the host must ensure at least one PCI bus clock cycle has
     * elapsed before writing another service request.  To meet this minimum
     * elapsed service request write timing interval, it is recommended that
     * the host follow any SCR write with another operation which reads from
     * the same address."
     */
    r = pci_read_32 ((u_int32_t *) &pi->reg->srd);      /* adhere to write
                                                         * timing imposition */


    if ((r != req) && (req != SR_CHIP_RESET) && (++rcnt <= MUSYCC_SR_RETRY_CNT))
    {
        if (cxt1e1_log_level >= LOG_MONITOR)
            pr_info("%s: %d - reissue srv req/last %x/%x (hdw reads %x), Chan %d.\n",
                    pi->up->devname, rcnt, req, pi->sr_last, r,
                    (pi->portnum * MUSYCC_NCHANS) + (req & 0x1f));
        OS_uwait_dummy ();          /* this delay helps reduce reissue counts
                                     * (reason not yet researched) */
        goto rewrite;
    }
    if (rcnt > MUSYCC_SR_RETRY_CNT)
    {
        pr_warning("%s: failed service request (#%d)= %x, group %d.\n",
                   pi->up->devname, MUSYCC_SR_RETRY_CNT, req, pi->portnum);
        SD_SEM_GIVE (&pi->sr_sem_busy); /* allow any next request */
        return;
    }
    if (req == SR_CHIP_RESET)
    {
        /*
         * PORT NOTE: the CHIP_RESET command is NOT ack'd by the MUSYCC, thus
         * the upcoming delay is used.  Though the MUSYCC documentation
         * suggests a read-after-write would supply the required delay, it's
         * unclear what CPU/BUS clock speeds might have been assumed when
         * suggesting this 'lack of ACK' workaround.  Thus the use of uwait.
         */
        OS_uwait (100000, "icard"); /* 100ms */
    } else
    {
        FLUSH_MEM_READ ();
        SD_SEM_TAKE (&pi->sr_sem_wait, "sakack");       /* sleep until SACK
                                                         * interrupt occurs */
    }
    SD_SEM_GIVE (&pi->sr_sem_busy); /* allow any next request */
}


#ifdef  SBE_PMCC4_ENABLE
void
musycc_update_timeslots (mpi_t * pi)
{
    int         i, ch;
    char        e1mode = IS_FRAME_ANY_E1 (pi->p.port_mode);

    for (i = 0; i < 32; i++)
    {
        int         usedby = 0, last = 0, ts, j, bits[8];

        u_int8_t lastval = 0;

        if (((i == 0) && e1mode) || /* disable if  E1 mode */
            ((i == 16) && ((pi->p.port_mode == CFG_FRAME_E1CRC_CAS) || (pi->p.port_mode == CFG_FRAME_E1CRC_CAS_AMI)))
            || ((i > 23) && (!e1mode))) /* disable if T1 mode */
        {
            pi->tsm[i] = 0xff;      /* make tslot unavailable for this mode */
        } else
        {
            pi->tsm[i] = 0x00;      /* make tslot available for assignment */
        }
        for (j = 0; j < 8; j++)
            bits[j] = -1;
        for (ch = 0; ch < MUSYCC_NCHANS; ch++)
        {
            if ((pi->chan[ch]->state == UP) && (pi->chan[ch]->p.bitmask[i]))
            {
                usedby++;
                last = ch;
                lastval = pi->chan[ch]->p.bitmask[i];
                for (j = 0; j < 8; j++)
                    if (lastval & (1 << j))
                        bits[j] = ch;
                pi->tsm[i] |= lastval;
            }
        }
        if (!usedby)
            ts = 0;
        else if ((usedby == 1) && (lastval == 0xff))
            ts = (4 << 5) | last;
        else if ((usedby == 1) && (lastval == 0x7f))
            ts = (5 << 5) | last;
        else
        {
            int         idx;

            if (bits[0] < 0)
                ts = (6 << 5) | (idx = last);
            else
                ts = (7 << 5) | (idx = bits[0]);
            for (j = 1; j < 8; j++)
            {
                pi->regram->rscm[idx * 8 + j] = (bits[j] < 0) ? 0 : (0x80 | bits[j]);
                pi->regram->tscm[idx * 8 + j] = (bits[j] < 0) ? 0 : (0x80 | bits[j]);
            }
        }
        pi->regram->rtsm[i] = ts;
        pi->regram->ttsm[i] = ts;
    }
    FLUSH_MEM_WRITE ();

    musycc_serv_req (pi, SR_TIMESLOT_MAP | SR_RX_DIRECTION);
    musycc_serv_req (pi, SR_TIMESLOT_MAP | SR_TX_DIRECTION);
    musycc_serv_req (pi, SR_SUBCHANNEL_MAP | SR_RX_DIRECTION);
    musycc_serv_req (pi, SR_SUBCHANNEL_MAP | SR_TX_DIRECTION);
}
#endif


#ifdef SBE_WAN256T3_ENABLE
void
musycc_update_timeslots (mpi_t * pi)
{
    mch_t      *ch;

    u_int8_t    ts, hmask, tsen;
    int         gchan;
    int         i;

#ifdef SBE_PMCC4_ENABLE
    hmask = (0x1f << pi->up->p.hypersize) & 0x1f;
#endif
#ifdef SBE_WAN256T3_ENABLE
    hmask = (0x1f << hyperdummy) & 0x1f;
#endif
    for (i = 0; i < 128; i++)
    {
        gchan = ((pi->portnum * MUSYCC_NCHANS) + (i & hmask)) % MUSYCC_NCHANS;
        ch = pi->chan[gchan];
        if (ch->p.mode_56k)
            tsen = MODE_56KBPS;
        else
            tsen = MODE_64KBPS;     /* also the default */
        ts = ((pi->portnum % 4) == (i / 32)) ? (tsen << 5) | (i & hmask) : 0;
        pi->regram->rtsm[i] = ts;
        pi->regram->ttsm[i] = ts;
    }
    FLUSH_MEM_WRITE ();
    musycc_serv_req (pi, SR_TIMESLOT_MAP | SR_RX_DIRECTION);
    musycc_serv_req (pi, SR_TIMESLOT_MAP | SR_TX_DIRECTION);
}
#endif


 /*
  * This routine converts a generic library channel configuration parameter
  * into a hardware specific register value (IE. MUSYCC CCD Register).
  */
u_int32_t
musycc_chan_proto (int proto)
{
    int         reg;

    switch (proto)
    {
    case CFG_CH_PROTO_TRANS:        /* 0 */
        reg = MUSYCC_CCD_TRANS;
        break;
    case CFG_CH_PROTO_SS7:          /* 1 */
        reg = MUSYCC_CCD_SS7;
        break;
    default:
    case CFG_CH_PROTO_ISLP_MODE:   /* 4 */
    case CFG_CH_PROTO_HDLC_FCS16:  /* 2 */
        reg = MUSYCC_CCD_HDLC_FCS16;
        break;
    case CFG_CH_PROTO_HDLC_FCS32:  /* 3 */
        reg = MUSYCC_CCD_HDLC_FCS32;
        break;
    }

    return reg;
}

#ifdef SBE_WAN256T3_ENABLE
STATIC void __init
musycc_init_port (mpi_t * pi)
{
    pci_write_32 ((u_int32_t *) &pi->reg->gbp, OS_vtophys (pi->regram));

    pi->regram->grcd =
        __constant_cpu_to_le32 (MUSYCC_GRCD_RX_ENABLE |
                                MUSYCC_GRCD_TX_ENABLE |
                                MUSYCC_GRCD_SF_ALIGN |
                                MUSYCC_GRCD_SUBCHAN_DISABLE |
                                MUSYCC_GRCD_OOFMP_DISABLE |
                                MUSYCC_GRCD_COFAIRQ_DISABLE |
                                MUSYCC_GRCD_MC_ENABLE |
                       (MUSYCC_GRCD_POLLTH_32 << MUSYCC_GRCD_POLLTH_SHIFT));

    pi->regram->pcd =
        __constant_cpu_to_le32 (MUSYCC_PCD_E1X4_MODE |
                                MUSYCC_PCD_TXDATA_RISING |
                                MUSYCC_PCD_TX_DRIVEN);

    /* Message length descriptor */
       pi->regram->mld = __constant_cpu_to_le32 (cxt1e1_max_mru | (cxt1e1_max_mru << 16));
    FLUSH_MEM_WRITE ();

    musycc_serv_req (pi, SR_GROUP_INIT | SR_RX_DIRECTION);
    musycc_serv_req (pi, SR_GROUP_INIT | SR_TX_DIRECTION);

    musycc_init_mdt (pi);

    musycc_update_timeslots (pi);
}
#endif


status_t    __init
musycc_init (ci_t * ci)
{
    char       *regaddr;        /* temp for address boundary calculations */
    int         i, gchan;

    OS_sem_init (&ci->sem_wdbusy, SEM_AVAILABLE);       /* watchdog exclusion */

    /*
     * Per MUSYCC manual, Section 6.3.4 - "The host must allocate a dword
     * aligned memory segment for interrupt queue pointers."
     */

#define INT_QUEUE_BOUNDARY  4

    regaddr = OS_kmalloc ((INT_QUEUE_SIZE + 1) * sizeof (u_int32_t));
    if (regaddr == 0)
        return ENOMEM;
    ci->iqd_p_saved = regaddr;      /* save orig value for free's usage */
    ci->iqd_p = (u_int32_t *) ((unsigned long) (regaddr + INT_QUEUE_BOUNDARY - 1) &
                               (~(INT_QUEUE_BOUNDARY - 1)));    /* this calculates
                                                                 * closest boundary */

    for (i = 0; i < INT_QUEUE_SIZE; i++)
    {
        ci->iqd_p[i] = __constant_cpu_to_le32 (INT_EMPTY_ENTRY);
    }

    for (i = 0; i < ci->max_port; i++)
    {
        mpi_t      *pi = &ci->port[i];

        /*
         * Per MUSYCC manual, Section 6.3.2 - "The host must allocate a 2KB
         * bound memory segment for Channel Group 0."
         */

#define GROUP_BOUNDARY   0x800

        regaddr = OS_kmalloc (sizeof (struct musycc_groupr) + GROUP_BOUNDARY);
        if (regaddr == 0)
        {
            for (gchan = 0; gchan < i; gchan++)
            {
                pi = &ci->port[gchan];
                OS_kfree (pi->reg);
                pi->reg = 0;
            }
            return ENOMEM;
        }
        pi->regram_saved = regaddr; /* save orig value for free's usage */
        pi->regram = (struct musycc_groupr *) ((unsigned long) (regaddr + GROUP_BOUNDARY - 1) &
                                               (~(GROUP_BOUNDARY - 1)));        /* this calculates
                                                                                 * closest boundary */
    }

    /* any board centric MUSYCC commands will use group ZERO as its "home" */
    ci->regram = ci->port[0].regram;
    musycc_serv_req (&ci->port[0], SR_CHIP_RESET);

    pci_write_32 ((u_int32_t *) &ci->reg->gbp, OS_vtophys (ci->regram));
    pci_flush_write (ci);
#ifdef CONFIG_SBE_PMCC4_NCOMM
    ci->regram->__glcd = __constant_cpu_to_le32 (GCD_MAGIC);
#else
    /* standard driver POLLS for INTB via CPLD register */
    ci->regram->__glcd = __constant_cpu_to_le32 (GCD_MAGIC | MUSYCC_GCD_INTB_DISABLE);
#endif

    ci->regram->__iqp = cpu_to_le32 (OS_vtophys (&ci->iqd_p[0]));
    ci->regram->__iql = __constant_cpu_to_le32 (INT_QUEUE_SIZE - 1);
    pci_write_32 ((u_int32_t *) &ci->reg->dacbp, 0);
    FLUSH_MEM_WRITE ();

    ci->state = C_RUNNING;          /* mark as full interrupt processing
                                     * available */

    musycc_serv_req (&ci->port[0], SR_GLOBAL_INIT);     /* FIRST INTERRUPT ! */

    /* sanity check settable parameters */

       if (cxt1e1_max_mru > 0xffe)
    {
        pr_warning("Maximum allowed MRU exceeded, resetting %d to %d.\n",
                                  cxt1e1_max_mru, 0xffe);
               cxt1e1_max_mru = 0xffe;
    }
       if (cxt1e1_max_mtu > 0xffe)
    {
        pr_warning("Maximum allowed MTU exceeded, resetting %d to %d.\n",
                                  cxt1e1_max_mtu, 0xffe);
               cxt1e1_max_mtu = 0xffe;
    }
#ifdef SBE_WAN256T3_ENABLE
    for (i = 0; i < MUSYCC_NPORTS; i++)
        musycc_init_port (&ci->port[i]);
#endif

    return SBE_DRVR_SUCCESS;        /* no error */
}


void
musycc_bh_tx_eom (mpi_t * pi, int gchan)
{
    mch_t      *ch;
    struct mdesc *md;

#if 0
#ifndef SBE_ISR_INLINE
    unsigned long flags;

#endif
#endif
    volatile u_int32_t status;

    ch = pi->chan[gchan];
    if (ch == 0 || ch->state != UP)
    {
        if (cxt1e1_log_level >= LOG_ERROR)
            pr_info("%s: intr: xmit EOM on uninitialized channel %d\n",
                    pi->up->devname, gchan);
    }
    if (ch == 0 || ch->mdt == 0)
        return;                     /* note: mdt==0 implies a malloc()
                                     * failure w/in chan_up() routine */

#if 0
#ifdef SBE_ISR_INLINE
    spin_lock_irq (&ch->ch_txlock);
#else
    spin_lock_irqsave (&ch->ch_txlock, flags);
#endif
#endif
    do
    {
        FLUSH_MEM_READ ();
        md = ch->txd_irq_srv;
        status = le32_to_cpu (md->status);

        /*
         * Note: Per MUSYCC Ref 6.4.9, the host does not poll a host-owned
         * Transmit Buffer Descriptor during Transparent Mode.
         */
        if (status & MUSYCC_TX_OWNED)
        {
            int         readCount, loopCount;

            /***********************************************************/
            /* HW Bug Fix                                              */
            /* ----------                                              */
            /* Under certain PCI Bus loading conditions, the data      */
            /* associated with an update of Shared Memory is delayed   */
            /* relative to its PCI Interrupt.  This is caught when     */
            /* the host determines it does not yet OWN the descriptor. */
            /***********************************************************/

            readCount = 0;
            while (status & MUSYCC_TX_OWNED)
            {
                for (loopCount = 0; loopCount < 0x30; loopCount++)
                    OS_uwait_dummy ();  /* use call to avoid optimization
                                         * removal of dummy delay */
                FLUSH_MEM_READ ();
                status = le32_to_cpu (md->status);
                if (readCount++ > 40)
                    break;          /* don't wait any longer */
            }
            if (status & MUSYCC_TX_OWNED)
            {
                if (cxt1e1_log_level >= LOG_MONITOR)
                {
                    pr_info("%s: Port %d Chan %2d - unexpected TX msg ownership intr (md %p sts %x)\n",
                            pi->up->devname, pi->portnum, ch->channum,
                            md, status);
                    pr_info("++ User 0x%p IRQ_SRV 0x%p USR_ADD 0x%p QStopped %x, start_tx %x tx_full %d txd_free %d mode %x\n",
                            ch->user, ch->txd_irq_srv, ch->txd_usr_add,
                            sd_queue_stopped (ch->user),
                            ch->ch_start_tx, ch->tx_full, ch->txd_free, ch->p.chan_mode);
                    musycc_dump_txbuffer_ring (ch, 0);
                }
                break;              /* Not our mdesc, done */
            } else
            {
                if (cxt1e1_log_level >= LOG_MONITOR)
                    pr_info("%s: Port %d Chan %2d - recovered TX msg ownership [%d] (md %p sts %x)\n",
                            pi->up->devname, pi->portnum, ch->channum, readCount, md, status);
            }
        }
        ch->txd_irq_srv = md->snext;

        md->data = 0;
        if (md->mem_token != 0)
        {
            /* upcount channel */
            atomic_sub (OS_mem_token_tlen (md->mem_token), &ch->tx_pending);
            /* upcount card */
            atomic_sub (OS_mem_token_tlen (md->mem_token), &pi->up->tx_pending);
#ifdef SBE_WAN256T3_ENABLE
            if (!atomic_read (&pi->up->tx_pending))
                wan256t3_led (pi->up, LED_TX, 0);
#endif

#ifdef CONFIG_SBE_WAN256T3_NCOMM
            /* callback that our packet was sent */
            {
                int         hdlcnum = (pi->portnum * 32 + gchan);

                if (hdlcnum >= 228)
                {
                    if (nciProcess_TX_complete)
                        (*nciProcess_TX_complete) (hdlcnum,
                                                   getuserbychan (gchan));
                }
            }
#endif                              /*** CONFIG_SBE_WAN256T3_NCOMM ***/

            OS_mem_token_free_irq (md->mem_token);
            md->mem_token = 0;
        }
        md->status = 0;
#ifdef RLD_TXFULL_DEBUG
        if (cxt1e1_log_level >= LOG_MONITOR2)
            pr_info("~~ tx_eom: tx_full %x  txd_free %d -> %d\n",
                    ch->tx_full, ch->txd_free, ch->txd_free + 1);
#endif
        ++ch->txd_free;
        FLUSH_MEM_WRITE ();

        if ((ch->p.chan_mode != CFG_CH_PROTO_TRANS) && (status & EOBIRQ_ENABLE))
        {
            if (cxt1e1_log_level >= LOG_MONITOR)
                pr_info("%s: Mode (%x) incorrect EOB status (%x)\n",
                        pi->up->devname, ch->p.chan_mode, status);
            if ((status & EOMIRQ_ENABLE) == 0)
                break;
        }
    }
    while ((ch->p.chan_mode != CFG_CH_PROTO_TRANS) && ((status & EOMIRQ_ENABLE) == 0));
    /*
     * NOTE: (The above 'while' is coupled w/ previous 'do', way above.) Each
     * Transparent data buffer has the EOB bit, and NOT the EOM bit, set and
     * will furthermore have a separate IQD associated with each messages
     * buffer.
     */

    FLUSH_MEM_READ ();
    /*
     * Smooth flow control hysterisis by maintaining task stoppage until half
     * the available write buffers are available.
     */
    if (ch->tx_full && (ch->txd_free >= (ch->txd_num / 2)))
    {
        /*
         * Then, only releave task stoppage if we actually have enough
         * buffers to service the last requested packet.  It may require MORE
         * than half the available!
         */
        if (ch->txd_free >= ch->txd_required)
        {

#ifdef RLD_TXFULL_DEBUG
            if (cxt1e1_log_level >= LOG_MONITOR2)
                pr_info("tx_eom[%d]: enable xmit tx_full no more, txd_free %d txd_num/2 %d\n",
                        ch->channum,
                        ch->txd_free, ch->txd_num / 2);
#endif
            ch->tx_full = 0;
            ch->txd_required = 0;
            sd_enable_xmit (ch->user);  /* re-enable to catch flow controlled
                                         * channel */
        }
    }
#ifdef RLD_TXFULL_DEBUG
    else if (ch->tx_full)
    {
        if (cxt1e1_log_level >= LOG_MONITOR2)
            pr_info("tx_eom[%d]: bypass TX enable though room available? (txd_free %d txd_num/2 %d)\n",
                    ch->channum,
                    ch->txd_free, ch->txd_num / 2);
    }
#endif

    FLUSH_MEM_WRITE ();
#if 0
#ifdef SBE_ISR_INLINE
    spin_unlock_irq (&ch->ch_txlock);
#else
    spin_unlock_irqrestore (&ch->ch_txlock, flags);
#endif
#endif
}


STATIC void
musycc_bh_rx_eom (mpi_t * pi, int gchan)
{
    mch_t      *ch;
    void       *m, *m2;
    struct mdesc *md;
    volatile u_int32_t status;
    u_int32_t   error;

    ch = pi->chan[gchan];
    if (ch == 0 || ch->state != UP)
    {
        if (cxt1e1_log_level > LOG_ERROR)
            pr_info("%s: intr: receive EOM on uninitialized channel %d\n",
                    pi->up->devname, gchan);
        return;
    }
    if (ch->mdr == 0)
        return;                     /* can this happen ? */

    for (;;)
    {
        FLUSH_MEM_READ ();
        md = &ch->mdr[ch->rxix_irq_srv];
        status = le32_to_cpu (md->status);
        if (!(status & HOST_RX_OWNED))
            break;                  /* Not our mdesc, done */
        m = md->mem_token;
        error = (status >> 16) & 0xf;
        if (error == 0)
        {
#ifdef CONFIG_SBE_WAN256T3_NCOMM
            int         hdlcnum = (pi->portnum * 32 + gchan);

            /*
             * if the packet number belongs to NCOMM, then send it to the TMS
             * driver
             */
            if (hdlcnum >= 228)
            {
                if (nciProcess_RX_packet)
                    (*nciProcess_RX_packet) (hdlcnum, status & 0x3fff, m, ch->user);
            } else
#endif                              /*** CONFIG_SBE_WAN256T3_NCOMM ***/

            {
                               if ((m2 = OS_mem_token_alloc (cxt1e1_max_mru)))
                {
                    /* substitute the mbuf+cluster */
                    md->mem_token = m2;
                    md->data = cpu_to_le32 (OS_vtophys (OS_mem_token_data (m2)));

                    /* pass the received mbuf upward */
                    sd_recv_consume (m, status & LENGTH_MASK, ch->user);
                    ch->s.rx_packets++;
                    ch->s.rx_bytes += status & LENGTH_MASK;
                } else
                {
                    ch->s.rx_dropped++;
                }
            }
        } else if (error == ERR_FCS)
        {
            ch->s.rx_crc_errors++;
        } else if (error == ERR_ALIGN)
        {
            ch->s.rx_missed_errors++;
        } else if (error == ERR_ABT)
        {
            ch->s.rx_missed_errors++;
        } else if (error == ERR_LNG)
        {
            ch->s.rx_length_errors++;
        } else if (error == ERR_SHT)
        {
            ch->s.rx_length_errors++;
        }
        FLUSH_MEM_WRITE ();
               status = cxt1e1_max_mru;
        if (ch->p.chan_mode == CFG_CH_PROTO_TRANS)
            status |= EOBIRQ_ENABLE;
        md->status = cpu_to_le32 (status);

        /* Check next mdesc in the ring */
        if (++ch->rxix_irq_srv >= ch->rxd_num)
            ch->rxix_irq_srv = 0;
        FLUSH_MEM_WRITE ();
    }
}


irqreturn_t
musycc_intr_th_handler (void *devp)
{
    ci_t       *ci = (ci_t *) devp;
    volatile u_int32_t status, currInt = 0;
    u_int32_t   nextInt, intCnt;

    /*
     * Hardware not available, potential interrupt hang.  But since interrupt
     * might be shared, just return.
     */
    if (ci->state == C_INIT)
    {
        return IRQ_NONE;
    }
    /*
     * Marked as hardware available. Don't service interrupts, just clear the
     * event.
     */

    if (ci->state == C_IDLE)
    {
        status = pci_read_32 ((u_int32_t *) &ci->reg->isd);

        /* clear the interrupt but process nothing else */
        pci_write_32 ((u_int32_t *) &ci->reg->isd, status);
        return IRQ_HANDLED;
    }
    FLUSH_PCI_READ ();
    FLUSH_MEM_READ ();

    status = pci_read_32 ((u_int32_t *) &ci->reg->isd);
    nextInt = INTRPTS_NEXTINT (status);
    intCnt = INTRPTS_INTCNT (status);
    ci->intlog.drvr_intr_thcount++;

    /*********************************************************/
    /* HW Bug Fix                                            */
    /* ----------                                            */
    /* Under certain PCI Bus loading conditions, the         */
    /* MUSYCC looses the data associated with an update      */
    /* of its ISD and erroneously returns the immediately    */
    /* preceding 'nextInt' value.  However, the 'intCnt'     */
    /* value appears to be correct.  By not starting service */
    /* where the 'missing' 'nextInt' SHOULD point causes     */
    /* the IQD not to be serviced - the 'not serviced'       */
    /* entries then remain and continue to increase as more  */
    /* incorrect ISD's are encountered.                      */
    /*********************************************************/

    if (nextInt != INTRPTS_NEXTINT (ci->intlog.this_status_new))
    {
        if (cxt1e1_log_level >= LOG_MONITOR)
        {
            pr_info("%s: note - updated ISD from %08x to %08x\n",
                    ci->devname, status,
              (status & (~INTRPTS_NEXTINT_M)) | ci->intlog.this_status_new);
        }
        /*
         * Replace bogus status with software corrected value.
         *
         * It's not known whether, during this problem occurrence, if the
         * INTFULL bit is correctly reported or not.
         */
        status = (status & (~INTRPTS_NEXTINT_M)) | (ci->intlog.this_status_new);
        nextInt = INTRPTS_NEXTINT (status);
    }
    /**********************************************/
    /* Cn847x Bug Fix                             */
    /* --------------                             */
    /* Fix for inability to write back same index */
    /* as read for a full interrupt queue.        */
    /**********************************************/

    if (intCnt == INT_QUEUE_SIZE)
    {
        currInt = ((intCnt - 1) + nextInt) & (INT_QUEUE_SIZE - 1);
    } else
        /************************************************/
        /* Interrupt Write Location Issues              */
        /* -------------------------------              */
        /* When the interrupt status descriptor is      */
        /* written, the interrupt line is de-asserted   */
        /* by the Cn847x.  In the case of MIPS          */
        /* microprocessors, this must occur at the      */
        /* beginning of the interrupt handler so that   */
        /* the interrupt handle is not re-entered due   */
        /* to interrupt dis-assertion latency.          */
        /* In the case of all other processors, this    */
        /* action should occur at the end of the        */
        /* interrupt handler to avoid overwriting the   */
        /* interrupt queue.                             */
        /************************************************/

    if (intCnt)
    {
        currInt = (intCnt + nextInt) & (INT_QUEUE_SIZE - 1);
    } else
    {
        /*
         * NOTE: Servicing an interrupt whose ISD contains a count of ZERO
         * can be indicative of a Shared Interrupt chain.  Our driver can be
         * called from the system's interrupt handler as a matter of the OS
         * walking the chain.  As the chain is walked, the interrupt will
         * eventually be serviced by the correct driver/handler.
         */
#if 0
        /* chained interrupt = not ours */
        pr_info(">> %s: intCnt NULL, sts %x, possibly a chained interrupt!\n",
                ci->devname, status);
#endif
        return IRQ_NONE;
    }

    ci->iqp_tailx = currInt;

    currInt <<= INTRPTS_NEXTINT_S;
    ci->intlog.last_status_new = ci->intlog.this_status_new;
    ci->intlog.this_status_new = currInt;

    if ((cxt1e1_log_level >= LOG_WARN) && (status & INTRPTS_INTFULL_M))
    {
        pr_info("%s: Interrupt queue full condition occurred\n", ci->devname);
    }
    if (cxt1e1_log_level >= LOG_DEBUG)
        pr_info("%s: interrupts pending, isd @ 0x%p: %x curr %d cnt %d NEXT %d\n",
                ci->devname, &ci->reg->isd,
        status, nextInt, intCnt, (intCnt + nextInt) & (INT_QUEUE_SIZE - 1));

    FLUSH_MEM_WRITE ();
#if defined(SBE_ISR_TASKLET)
    pci_write_32 ((u_int32_t *) &ci->reg->isd, currInt);
    atomic_inc (&ci->bh_pending);
    tasklet_schedule (&ci->ci_musycc_isr_tasklet);
#elif defined(SBE_ISR_IMMEDIATE)
    pci_write_32 ((u_int32_t *) &ci->reg->isd, currInt);
    atomic_inc (&ci->bh_pending);
    queue_task (&ci->ci_musycc_isr_tq, &tq_immediate);
    mark_bh (IMMEDIATE_BH);
#elif defined(SBE_ISR_INLINE)
    (void) musycc_intr_bh_tasklet (ci);
    pci_write_32 ((u_int32_t *) &ci->reg->isd, currInt);
#endif
    return IRQ_HANDLED;
}


#if defined(SBE_ISR_IMMEDIATE)
unsigned long
#else
void
#endif
musycc_intr_bh_tasklet (ci_t * ci)
{
    mpi_t      *pi;
    mch_t      *ch;
    unsigned int intCnt;
    volatile u_int32_t currInt = 0;
    volatile unsigned int headx, tailx;
    int         readCount, loopCount;
    int         group, gchan, event, err, tx;
    u_int32_t   badInt = INT_EMPTY_ENTRY;
    u_int32_t   badInt2 = INT_EMPTY_ENTRY2;

    /*
     * Hardware not available, potential interrupt hang.  But since interrupt
     * might be shared, just return.
     */
    if ((drvr_state != SBE_DRVR_AVAILABLE) || (ci->state == C_INIT))
    {
#if defined(SBE_ISR_IMMEDIATE)
        return 0L;
#else
        return;
#endif
    }
#if defined(SBE_ISR_TASKLET) || defined(SBE_ISR_IMMEDIATE)
    if (drvr_state != SBE_DRVR_AVAILABLE)
    {
#if defined(SBE_ISR_TASKLET)
        return;
#elif defined(SBE_ISR_IMMEDIATE)
        return 0L;
#endif
    }
#elif defined(SBE_ISR_INLINE)
    /* no semaphore taken, no double checks */
#endif

    ci->intlog.drvr_intr_bhcount++;
    FLUSH_MEM_READ ();
    {
        unsigned int bh = atomic_read (&ci->bh_pending);

        max_bh = max (bh, max_bh);
    }
    atomic_set (&ci->bh_pending, 0);/* if here, no longer pending */
    while ((headx = ci->iqp_headx) != (tailx = ci->iqp_tailx))
    {
        intCnt = (tailx >= headx) ? (tailx - headx) : (tailx - headx + INT_QUEUE_SIZE);
        currInt = le32_to_cpu (ci->iqd_p[headx]);

        max_intcnt = max (intCnt, max_intcnt);  /* RLD DEBUG */

        /**************************************************/
        /* HW Bug Fix                                     */
        /* ----------                                     */
        /* The following code checks for the condition    */
        /* of interrupt assertion before interrupt        */
        /* queue update.  This is a problem on several    */
        /* PCI-Local bridge chips found on some products. */
        /**************************************************/

        readCount = 0;
        if ((currInt == badInt) || (currInt == badInt2))
            ci->intlog.drvr_int_failure++;

        while ((currInt == badInt) || (currInt == badInt2))
        {
            for (loopCount = 0; loopCount < 0x30; loopCount++)
                OS_uwait_dummy ();  /* use call to avoid optimization removal
                                     * of dummy delay */
            FLUSH_MEM_READ ();
            currInt = le32_to_cpu (ci->iqd_p[headx]);
            if (readCount++ > 20)
                break;
        }

        if ((currInt == badInt) || (currInt == badInt2))        /* catch failure of Bug
                                                                 * Fix checking */
        {
            if (cxt1e1_log_level >= LOG_WARN)
                pr_info("%s: Illegal Interrupt Detected @ 0x%p, mod %d.)\n",
                        ci->devname, &ci->iqd_p[headx], headx);

            /*
             * If the descriptor has not recovered, then leaving the EMPTY
             * entry set will not signal to the MUSYCC that this descriptor
             * has been serviced. The Interrupt Queue can then start loosing
             * available descriptors and MUSYCC eventually encounters and
             * reports the INTFULL condition.  Per manual, changing any bit
             * marks descriptor as available, thus the use of different
             * EMPTY_ENTRY values.
             */

            if (currInt == badInt)
            {
                ci->iqd_p[headx] = __constant_cpu_to_le32 (INT_EMPTY_ENTRY2);
            } else
            {
                ci->iqd_p[headx] = __constant_cpu_to_le32 (INT_EMPTY_ENTRY);
            }
            ci->iqp_headx = (headx + 1) & (INT_QUEUE_SIZE - 1); /* insure wrapness */
            FLUSH_MEM_WRITE ();
            FLUSH_MEM_READ ();
            continue;
        }
        group = INTRPT_GRP (currInt);
        gchan = INTRPT_CH (currInt);
        event = INTRPT_EVENT (currInt);
        err = INTRPT_ERROR (currInt);
        tx = currInt & INTRPT_DIR_M;

        ci->iqd_p[headx] = __constant_cpu_to_le32 (INT_EMPTY_ENTRY);
        FLUSH_MEM_WRITE ();

        if (cxt1e1_log_level >= LOG_DEBUG)
        {
            if (err != 0)
                pr_info(" %08x -> err: %2d,", currInt, err);

            pr_info("+ interrupt event: %d, grp: %d, chan: %2d, side: %cX\n",
                    event, group, gchan, tx ? 'T' : 'R');
        }
        pi = &ci->port[group];      /* notice that here we assume 1-1 group -
                                     * port mapping */
        ch = pi->chan[gchan];
        switch (event)
        {
        case EVE_SACK:              /* Service Request Acknowledge */
            if (cxt1e1_log_level >= LOG_DEBUG)
            {
                volatile u_int32_t r;

                r = pci_read_32 ((u_int32_t *) &pi->reg->srd);
                pr_info("- SACK cmd: %08x (hdw= %08x)\n", pi->sr_last, r);
            }
            SD_SEM_GIVE (&pi->sr_sem_wait);     /* wake up waiting process */
            break;
        case EVE_CHABT:     /* Change To Abort Code (0x7e -> 0xff) */
        case EVE_CHIC:              /* Change To Idle Code (0xff -> 0x7e) */
            break;
        case EVE_EOM:               /* End Of Message */
        case EVE_EOB:               /* End Of Buffer (Transparent mode) */
            if (tx)
            {
                musycc_bh_tx_eom (pi, gchan);
            } else
            {
                musycc_bh_rx_eom (pi, gchan);
            }
#if 0
            break;
#else
            /*
             * MUSYCC Interrupt Descriptor section states that EOB and EOM
             * can be combined with the NONE error (as well as others).  So
             * drop thru to catch this...
             */
#endif
        case EVE_NONE:
            if (err == ERR_SHT)
            {
                ch->s.rx_length_errors++;
            }
            break;
        default:
            if (cxt1e1_log_level >= LOG_WARN)
                pr_info("%s: unexpected interrupt event: %d, iqd[%d]: %08x, port: %d\n", ci->devname,
                        event, headx, currInt, group);
            break;
        }                           /* switch on event */


        /*
         * Per MUSYCC Manual, Section 6.4.8.3 [Transmit Errors], TX errors
         * are service-affecting and require action to resume normal
         * bit-level processing.
         */

        switch (err)
        {
        case ERR_ONR:
            /*
             * Per MUSYCC manual, Section  6.4.8.3 [Transmit Errors], this
             * error requires Transmit channel reactivation.
             *
             * Per MUSYCC manual, Section  6.4.8.4 [Receive Errors], this error
             * requires Receive channel reactivation.
             */
            if (tx)
            {

                /*
                 * TX ONR Error only occurs when channel is configured for
                 * Transparent Mode.  However, this code will catch and
                 * re-activate on ANY TX ONR error.
                 */

                /*
                 * Set flag to re-enable on any next transmit attempt.
                 */
                ch->ch_start_tx = CH_START_TX_ONR;

                {
#ifdef RLD_TRANS_DEBUG
                    if (1 || cxt1e1_log_level >= LOG_MONITOR)
#else
                    if (cxt1e1_log_level >= LOG_MONITOR)
#endif
                    {
                        pr_info("%s: TX buffer underflow [ONR] on channel %d, mode %x QStopped %x free %d\n",
                                ci->devname, ch->channum, ch->p.chan_mode, sd_queue_stopped (ch->user), ch->txd_free);
#ifdef RLD_DEBUG
                        if (ch->p.chan_mode == 2)       /* problem = ONR on HDLC
                                                         * mode */
                        {
                            pr_info("++ Failed Last %x Next %x QStopped %x, start_tx %x tx_full %d txd_free %d mode %x\n",
                                    (u_int32_t) ch->txd_irq_srv, (u_int32_t) ch->txd_usr_add,
                                    sd_queue_stopped (ch->user),
                                    ch->ch_start_tx, ch->tx_full, ch->txd_free, ch->p.chan_mode);
                            musycc_dump_txbuffer_ring (ch, 0);
                        }
#endif
                    }
                }
            } else                  /* RX buffer overrun */
            {
                /*
                 * Per MUSYCC manual, Section 6.4.8.4 [Receive Errors],
                 * channel recovery for this RX ONR error IS required.  It is
                 * also suggested to increase the number of receive buffers
                 * for this channel.  Receive channel reactivation IS
                 * required, and data has been lost.
                 */
                ch->s.rx_over_errors++;
                ch->ch_start_rx = CH_START_RX_ONR;

                if (cxt1e1_log_level >= LOG_WARN)
                {
                    pr_info("%s: RX buffer overflow [ONR] on channel %d, mode %x\n",
                            ci->devname, ch->channum, ch->p.chan_mode);
                    //musycc_dump_rxbuffer_ring (ch, 0);        /* RLD DEBUG */
                }
            }
            musycc_chan_restart (ch);
            break;
        case ERR_BUF:
            if (tx)
            {
                ch->s.tx_fifo_errors++;
                ch->ch_start_tx = CH_START_TX_BUF;
                /*
                 * Per MUSYCC manual, Section  6.4.8.3 [Transmit Errors],
                 * this BUFF error requires Transmit channel reactivation.
                 */
                if (cxt1e1_log_level >= LOG_MONITOR)
                    pr_info("%s: TX buffer underrun [BUFF] on channel %d, mode %x\n",
                            ci->devname, ch->channum, ch->p.chan_mode);
            } else                  /* RX buffer overrun */
            {
                ch->s.rx_over_errors++;
                /*
                 * Per MUSYCC manual, Section 6.4.8.4 [Receive Errors], HDLC
                 * mode requires NO recovery for this RX BUFF error is
                 * required.  It is suggested to increase the FIFO buffer
                 * space for this channel.  Receive channel reactivation is
                 * not required, but data has been lost.
                 */
                if (cxt1e1_log_level >= LOG_WARN)
                    pr_info("%s: RX buffer overrun [BUFF] on channel %d, mode %x\n",
                            ci->devname, ch->channum, ch->p.chan_mode);
                /*
                 * Per MUSYCC manual, Section 6.4.9.4 [Receive Errors],
                 * Transparent mode DOES require recovery for the RX BUFF
                 * error.  It is suggested to increase the FIFO buffer space
                 * for this channel.  Receive channel reactivation IS
                 * required and data has been lost.
                 */
                if (ch->p.chan_mode == CFG_CH_PROTO_TRANS)
                    ch->ch_start_rx = CH_START_RX_BUF;
            }

            if (tx || (ch->p.chan_mode == CFG_CH_PROTO_TRANS))
                musycc_chan_restart (ch);
            break;
        default:
            break;
        }                           /* switch on err */

        /* Check for interrupt lost condition */
        if ((currInt & INTRPT_ILOST_M) && (cxt1e1_log_level >= LOG_ERROR))
        {
            pr_info("%s: Interrupt queue overflow - ILOST asserted\n",
                    ci->devname);
        }
        ci->iqp_headx = (headx + 1) & (INT_QUEUE_SIZE - 1);     /* insure wrapness */
        FLUSH_MEM_WRITE ();
        FLUSH_MEM_READ ();
    }                               /* while */
    if ((cxt1e1_log_level >= LOG_MONITOR2) && (ci->iqp_headx != ci->iqp_tailx))
    {
        int         bh;

        bh = atomic_read (&CI->bh_pending);
        pr_info("_bh_: late arrivals, head %d != tail %d, pending %d\n",
                ci->iqp_headx, ci->iqp_tailx, bh);
    }
#if defined(SBE_ISR_IMMEDIATE)
    return 0L;
#endif
    /* else, nothing returned */
}

#if 0
int         __init
musycc_new_chan (ci_t * ci, int channum, void *user)
{
    mch_t      *ch;

    ch = ci->port[channum / MUSYCC_NCHANS].chan[channum % MUSYCC_NCHANS];

    if (ch->state != UNASSIGNED)
        return EEXIST;
    /* NOTE: mch_t already cleared during OS_kmalloc() */
    ch->state = DOWN;
    ch->user = user;
#if 0
    ch->status = 0;
    ch->p.status = 0;
    ch->p.intr_mask = 0;
#endif
    ch->p.chan_mode = CFG_CH_PROTO_HDLC_FCS16;
    ch->p.idlecode = CFG_CH_FLAG_7E;
    ch->p.pad_fill_count = 2;
    spin_lock_init (&ch->ch_rxlock);
    spin_lock_init (&ch->ch_txlock);

    return 0;
}
#endif


#ifdef SBE_PMCC4_ENABLE
status_t
musycc_chan_down (ci_t * dummy, int channum)
{
    mpi_t      *pi;
    mch_t      *ch;
    int         i, gchan;

    if (!(ch = sd_find_chan (dummy, channum)))
        return EINVAL;
    pi = ch->up;
    gchan = ch->gchan;

    /* Deactivate the channel */
    musycc_serv_req (pi, SR_CHANNEL_DEACTIVATE | SR_RX_DIRECTION | gchan);
    ch->ch_start_rx = 0;
    musycc_serv_req (pi, SR_CHANNEL_DEACTIVATE | SR_TX_DIRECTION | gchan);
    ch->ch_start_tx = 0;

    if (ch->state == DOWN)
        return 0;
    ch->state = DOWN;

    pi->regram->thp[gchan] = 0;
    pi->regram->tmp[gchan] = 0;
    pi->regram->rhp[gchan] = 0;
    pi->regram->rmp[gchan] = 0;
    FLUSH_MEM_WRITE ();
    for (i = 0; i < ch->txd_num; i++)
    {
        if (ch->mdt[i].mem_token != 0)
            OS_mem_token_free (ch->mdt[i].mem_token);
    }

    for (i = 0; i < ch->rxd_num; i++)
    {
        if (ch->mdr[i].mem_token != 0)
            OS_mem_token_free (ch->mdr[i].mem_token);
    }

    OS_kfree (ch->mdr);
    ch->mdr = 0;
    ch->rxd_num = 0;
    OS_kfree (ch->mdt);
    ch->mdt = 0;
    ch->txd_num = 0;

    musycc_update_timeslots (pi);
    c4_fifo_free (pi, ch->gchan);

    pi->openchans--;
    return 0;
}
#endif


int
musycc_del_chan (ci_t * ci, int channum)
{
    mch_t      *ch;

    if ((channum < 0) || (channum >= (MUSYCC_NPORTS * MUSYCC_NCHANS)))  /* sanity chk param */
        return ECHRNG;
    if (!(ch = sd_find_chan (ci, channum)))
        return ENOENT;
    if (ch->state == UP)
        musycc_chan_down (ci, channum);
    ch->state = UNASSIGNED;
    return 0;
}


int
musycc_del_chan_stats (ci_t * ci, int channum)
{
    mch_t      *ch;

    if (channum < 0 || channum >= (MUSYCC_NPORTS * MUSYCC_NCHANS))      /* sanity chk param */
        return ECHRNG;
    if (!(ch = sd_find_chan (ci, channum)))
        return ENOENT;

    memset (&ch->s, 0, sizeof (struct sbecom_chan_stats));
    return 0;
}


int
musycc_start_xmit (ci_t * ci, int channum, void *mem_token)
{
    mch_t      *ch;
    struct mdesc *md;
    void       *m2;
#if 0
    unsigned long flags;
#endif
    int         txd_need_cnt;
    u_int32_t   len;

    if (!(ch = sd_find_chan (ci, channum)))
        return ENOENT;

    if (ci->state != C_RUNNING)     /* full interrupt processing available */
        return EINVAL;
    if (ch->state != UP)
        return EINVAL;

    if (!(ch->status & TX_ENABLED))
        return EROFS;               /* how else to flag unwritable state ? */

#ifdef RLD_TRANS_DEBUGx
    if (1 || cxt1e1_log_level >= LOG_MONITOR2)
#else
    if (cxt1e1_log_level >= LOG_MONITOR2)
#endif
    {
        pr_info("++ start_xmt[%d]: state %x start %x full %d free %d required %d stopped %x\n",
                channum, ch->state, ch->ch_start_tx, ch->tx_full,
                ch->txd_free, ch->txd_required, sd_queue_stopped (ch->user));
    }
    /***********************************************/
    /** Determine total amount of data to be sent **/
    /***********************************************/
    m2 = mem_token;
    txd_need_cnt = 0;
    for (len = OS_mem_token_tlen (m2); len > 0;
         m2 = (void *) OS_mem_token_next (m2))
    {
        if (!OS_mem_token_len (m2))
            continue;
        txd_need_cnt++;
        len -= OS_mem_token_len (m2);
    }

    if (txd_need_cnt == 0)
    {
        if (cxt1e1_log_level >= LOG_MONITOR2)
            pr_info("%s channel %d: no TX data in User buffer\n", ci->devname, channum);
        OS_mem_token_free (mem_token);
        return 0;                   /* no data to send */
    }
    /*************************************************/
    /** Are there sufficient descriptors available? **/
    /*************************************************/
    if (txd_need_cnt > ch->txd_num) /* never enough descriptors for this
                                     * large a buffer */
    {
        if (cxt1e1_log_level >= LOG_DEBUG)
        {
            pr_info("start_xmit: discarding buffer, insufficient descriptor cnt %d, need %d.\n",
                    ch->txd_num, txd_need_cnt + 1);
        }
        ch->s.tx_dropped++;
        OS_mem_token_free (mem_token);
        return 0;
    }
#if 0
    spin_lock_irqsave (&ch->ch_txlock, flags);
#endif
    /************************************************************/
    /** flow control the line if not enough descriptors remain **/
    /************************************************************/
    if (txd_need_cnt > ch->txd_free)
    {
        if (cxt1e1_log_level >= LOG_MONITOR2)
        {
            pr_info("start_xmit[%d]: EBUSY - need more descriptors, have %d of %d need %d\n",
                    channum, ch->txd_free, ch->txd_num, txd_need_cnt);
        }
        ch->tx_full = 1;
        ch->txd_required = txd_need_cnt;
        sd_disable_xmit (ch->user);
#if 0
        spin_unlock_irqrestore (&ch->ch_txlock, flags);
#endif
        return EBUSY;               /* tell user to try again later */
    }
    /**************************************************/
    /** Put the user data into MUSYCC data buffer(s) **/
    /**************************************************/
    m2 = mem_token;
    md = ch->txd_usr_add;           /* get current available descriptor */

    for (len = OS_mem_token_tlen (m2); len > 0; m2 = OS_mem_token_next (m2))
    {
        int         u = OS_mem_token_len (m2);

        if (!u)
            continue;
        len -= u;

        /*
         * Enable following chunks, yet wait to enable the FIRST chunk until
         * after ALL subsequent chunks are setup.
         */
        if (md != ch->txd_usr_add)  /* not first chunk */
            u |= MUSYCC_TX_OWNED;   /* transfer ownership from HOST to MUSYCC */

        if (len)                    /* not last chunk */
            u |= EOBIRQ_ENABLE;
        else if (ch->p.chan_mode == CFG_CH_PROTO_TRANS)
        {
            /*
             * Per MUSYCC Ref 6.4.9 for Transparent Mode, the host must
             * always clear EOMIRQ_ENABLE in every Transmit Buffer Descriptor
             * (IE. don't set herein).
             */
            u |= EOBIRQ_ENABLE;
        } else
            u |= EOMIRQ_ENABLE;     /* EOM, last HDLC chunk */


        /* last chunk in hdlc mode */
        u |= (ch->p.idlecode << IDLE_CODE);
        if (ch->p.pad_fill_count)
        {
#if 0
            /* NOOP NOTE: u_int8_t cannot be > 0xFF */
            /* sanitize pad_fill_count for maximums allowed by hardware */
            if (ch->p.pad_fill_count > EXTRA_FLAGS_MASK)
                ch->p.pad_fill_count = EXTRA_FLAGS_MASK;
#endif
            u |= (PADFILL_ENABLE | (ch->p.pad_fill_count << EXTRA_FLAGS));
        }
        md->mem_token = len ? 0 : mem_token;    /* Fill in mds on last
                                                 * segment, others set ZERO
                                                 * so that entire token is
                                                 * removed ONLY when ALL
                                                 * segments have been
                                                 * transmitted. */

        md->data = cpu_to_le32 (OS_vtophys (OS_mem_token_data (m2)));
        FLUSH_MEM_WRITE ();
        md->status = cpu_to_le32 (u);
        --ch->txd_free;
        md = md->snext;
    }
    FLUSH_MEM_WRITE ();


    /*
     * Now transfer ownership of first chunk from HOST to MUSYCC in order to
     * fire-off this XMIT.
     */
    ch->txd_usr_add->status |= __constant_cpu_to_le32 (MUSYCC_TX_OWNED);
    FLUSH_MEM_WRITE ();
    ch->txd_usr_add = md;

    len = OS_mem_token_tlen (mem_token);
    atomic_add (len, &ch->tx_pending);
    atomic_add (len, &ci->tx_pending);
    ch->s.tx_packets++;
    ch->s.tx_bytes += len;
    /*
     * If an ONR was seen, then channel requires poking to restart
     * transmission.
     */
    if (ch->ch_start_tx)
    {
        musycc_chan_restart (ch);
    }
#ifdef SBE_WAN256T3_ENABLE
    wan256t3_led (ci, LED_TX, LEDV_G);
#endif
    return 0;
}


/*** End-of-File ***/
