/*
 * Copyright (c) 2002-2006, Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef ATH_SUPPORT_DFS
#include "dfs_host.h"
#include "dfs_common.h"

struct ath_dfs_host *dfs_attach_host(DEV_HDL dev, OS_HDL os, ATH_DFS_CAPINFO *cap_info)
{
    int i,n;
    struct ath_dfs_host *dfs;

    dfs = (struct ath_dfs_host *)DFS_MALLOC(os, sizeof(struct ath_dfs_host));

    if (dfs == NULL) {
        A_PRINTF("%s: ath_dfs allocation failed\n", __func__);
        return dfs;
    }

    OS_MEMZERO(dfs, sizeof (struct ath_dfs_host));

    dfs->dev_hdl = dev;
    dfs->os_hdl = os;

    dfs->dfs_debug_level=ATH_DEBUG_DFS;

    ATH_DFSQ_LOCK_INIT(dfs);
    STAILQ_INIT(&dfs->dfs_radarq);
    ATH_ARQ_LOCK_INIT(dfs);
    STAILQ_INIT(&dfs->dfs_arq);
    STAILQ_INIT(&(dfs->dfs_eventq));
    ATH_DFSEVENTQ_LOCK_INIT(dfs);

    OS_INIT_TIMER(&dfs->dfs_radar_task_timer, dfs_radar_task, dfs);
    
    dfs->events = (struct dfs_event *)DFS_MALLOC(os,
            sizeof(struct dfs_event)*DFS_MAX_EVENTS);
    
    if (dfs->events == NULL) {
        OS_FREE(dfs);
        dfs = NULL;
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS,
                "%s: events allocation failed\n", __func__);
        return dfs;
    }
    for (i=0; i< DFS_MAX_EVENTS; i++) {
        STAILQ_INSERT_TAIL(&(dfs->dfs_eventq), &dfs->events[i], re_list);
    }

    dfs->pulses = (struct dfs_pulseline *)DFS_MALLOC(os, sizeof(struct dfs_pulseline));

    if (dfs->pulses == NULL) {
        OS_FREE(dfs->events);   
        dfs->events = NULL;
        OS_FREE(dfs);
        dfs = NULL;
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS,
                "%s: pulse buffer allocation failed\n", __func__);
        return dfs;
    }

    dfs->pulses->pl_lastelem = DFS_MAX_PULSE_BUFFER_MASK;

#ifdef ATH_ENABLE_AR
    if(cap_info->enable_ar){
        dfs_reset_ar(dfs);
        dfs_reset_arq(dfs);
        dfs->dfs_proc_phyerr |= DFS_AR_EN;
    }
#endif /* ATH_ENABLE_AR */

    if(cap_info->enable_radar) {
        /* Allocate memory for radar filters */
        for (n=0; n<DFS_MAX_RADAR_TYPES; n++) {
            dfs->dfs_radarf[n] = (struct dfs_filtertype *)DFS_MALLOC(os, sizeof(struct dfs_filtertype));
            if (dfs->dfs_radarf[n] == NULL) {
                DFS_DPRINTK(dfs,ATH_DEBUG_DFS,
                        "%s: cannot allocate memory for radar filter types\n",
                        __func__);
                goto bad1;
            }
        }
        /* Allocate memory for radar table */
        dfs->dfs_radartable = (int8_t **)DFS_MALLOC(os, 256*sizeof(int8_t *));
        if (dfs->dfs_radartable == NULL) {
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: cannot allocate memory for radar table\n",
                    __func__);
            goto bad1;
        }
        for (n=0; n<256; n++) {
            dfs->dfs_radartable[n] = DFS_MALLOC(os, DFS_MAX_RADAR_OVERLAP*sizeof(int8_t));
            if (dfs->dfs_radartable[n] == NULL) {
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS,
                        "%s: cannot allocate memory for radar table entry\n",
                        __func__);
                goto bad2;
            }
        }

        /* Init the Bin5 chirping related data */
        dfs->dfs_rinfo.dfs_bin5_chirp_ts = cap_info->ext_chan_busy_ts;
        dfs->dfs_rinfo.dfs_last_bin5_dur = MAX_BIN5_DUR;

        dfs->dfs_b5radars = NULL;
    }

    return dfs;

bad2:
    OS_FREE(dfs->dfs_radartable);
    dfs->dfs_radartable = NULL;
bad1:	
    for (n=0; n<DFS_MAX_RADAR_TYPES; n++) {
        if (dfs->dfs_radarf[n] != NULL) {
            OS_FREE(dfs->dfs_radarf[n]);
            dfs->dfs_radarf[n] = NULL;
        }
    }
    if (dfs->pulses) {
        OS_FREE(dfs->pulses);
        dfs->pulses = NULL;
    }
    if (dfs->events) {
        OS_FREE(dfs->events);
        dfs->events = NULL;
    }

    return dfs;

}

void
dfs_detach_host(struct ath_dfs_host *dfs)
{
    int n, empty;

    if (dfs == NULL) {
        A_PRINTF("%s: sc_dfs is NULL\n", __func__);
        return;
    }

    OS_CANCEL_TIMER(&dfs->dfs_radar_task_timer);

    /* Return radar events to free q*/
    dfs_reset_radarq(dfs);
    dfs_reset_alldelaylines(dfs);

    /* Free up pulse log*/
    if (dfs->pulses != NULL) {
        OS_FREE(dfs->pulses);
        dfs->pulses = NULL;
    }

    for (n=0; n<DFS_MAX_RADAR_TYPES;n++) {
        if (dfs->dfs_radarf[n] != NULL) {
            OS_FREE(dfs->dfs_radarf[n]);
            dfs->dfs_radarf[n] = NULL;
        }
    }


    if (dfs->dfs_radartable != NULL) {
        for (n=0; n<256; n++) {
            if (dfs->dfs_radartable[n] != NULL) {
                OS_FREE(dfs->dfs_radartable[n]);
                dfs->dfs_radartable[n] = NULL;
            }
        }
        OS_FREE(dfs->dfs_radartable);
        dfs->dfs_radartable = NULL;
    }

    if (dfs->dfs_b5radars != NULL) {
        OS_FREE(dfs->dfs_b5radars);
        dfs->dfs_b5radars=NULL;
    }

    dfs_reset_ar(dfs);

    ATH_ARQ_LOCK(dfs);
    empty = STAILQ_EMPTY(&(dfs->dfs_arq));
    ATH_ARQ_UNLOCK(dfs);
    if (!empty) {
        dfs_reset_arq(dfs);
    }
    if (dfs->events != NULL) {
        OS_FREE(dfs->events);
        dfs->events = NULL;
    }
    OS_FREE(dfs);
    dfs = NULL;
}


void dfs_bangradar_enable(struct ath_dfs_host *dfs, u_int8_t enable)
{
    dfs->dfs_bangradar = enable;
}

void dfs_set_dur_multiplier(struct ath_dfs_host *dfs, u_int32_t  dur_multiplier)
{
    dfs->dur_multiplier = dur_multiplier;
    DFS_DPRINTK(dfs, ATH_DEBUG_DFS3,
            "%s: duration multiplier is %d\n", __func__, dfs->dur_multiplier);
}

void dfs_set_debug_level_host(struct ath_dfs_host *dfs, u_int32_t level)
{
    dfs->dfs_debug_level = level;
    DFS_DPRINTK(dfs, ATH_DEBUG_DFS3,
            "%s: debug level is %d\n", __func__, dfs->dfs_debug_level);

}

#endif /* ATH_UPPORT_DFS */
