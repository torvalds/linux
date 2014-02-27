/*
 * Copyright (c) 2002-2010, Atheros Communications Inc.
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

static char debug_dup[33];
static int debug_dup_cnt;

u_int32_t dfs_round(int32_t val)
{
	u_int32_t ival,rem;

	if (val < 0)
		return 0;
	ival = val/100;
	rem = val-(ival*100);
	if (rem <50)
		return ival;
	else
		return(ival+1);
}


static inline u_int8_t
dfs_process_pulse_dur(struct ath_dfs_host *dfs, u_int8_t re_dur) 
{
	if (re_dur == 0) {
		return 1;
	} else {
		/* Convert 0.8us durations to TSF ticks (usecs) */
		return (u_int8_t)dfs_round((int32_t)((dfs->dur_multiplier)*re_dur));
	}
}

int
dfs_process_radarevent_host(struct ath_dfs_host *dfs, int16_t *chan_index, u_int8_t *bangradar)
{
    struct dfs_event re,*event;
    struct dfs_filtertype *ft;
    struct dfs_filter *rf;
    int found, retval=0,p, empty;
    int events_processed=0;
    u_int32_t tabledepth, index;
    u_int64_t deltafull_ts = 0,this_ts, deltaT;
    struct dfs_pulseline *pl;
    static u_int32_t  test_ts  = 0;
    static u_int32_t  diff_ts  = 0;

    int ext_chan_event_flag=0;

    if (dfs == NULL) {
        A_PRINTF("%s: sc_sfs is NULL\n", __func__);
        return 0;
    }
    pl = dfs->pulses;
    /* TEST : Simulate radar bang, make sure we add the channel to NOL (bug 29968) */
    if (dfs->dfs_bangradar) {
        /* bangradar will always simulate radar found on the primary channel */
        *bangradar = 1;
        dfs->dfs_bangradar = 0; /* reset */
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: bangradar\n", __func__);
        retval = 1;                    
    }
    else
        *bangradar = 0;
 
    ATH_DFSQ_LOCK(dfs);
    empty = STAILQ_EMPTY(&(dfs->dfs_radarq));
    ATH_DFSQ_UNLOCK(dfs);

    while ((!empty) && (!retval) && (events_processed < MAX_EVENTS)) {
        ATH_DFSQ_LOCK(dfs);
        event = STAILQ_FIRST(&(dfs->dfs_radarq));
        if (event != NULL)
            STAILQ_REMOVE_HEAD(&(dfs->dfs_radarq), re_list);
        ATH_DFSQ_UNLOCK(dfs);

        if (event == NULL) {
            empty = 1;
            break;
        }
        events_processed++;
        re = *event;

        OS_MEMZERO(event, sizeof(struct dfs_event));
        ATH_DFSEVENTQ_LOCK(dfs);
        STAILQ_INSERT_TAIL(&(dfs->dfs_eventq), event, re_list);
        ATH_DFSEVENTQ_UNLOCK(dfs);

        found = 0;
        if (re.re_chanindex < DFS_NUM_RADAR_STATES)
            *chan_index = re.re_chanindex;
        else {
            ATH_DFSQ_LOCK(dfs);
            empty = STAILQ_EMPTY(&(dfs->dfs_radarq));
            ATH_DFSQ_UNLOCK(dfs);
            continue;
        }

        if (dfs->dfs_rinfo.rn_lastfull_ts == 0) {
            /*
             * Either not started, or 64-bit rollover exactly to zero
             * Just prepend zeros to the 15-bit ts
             */
            dfs->dfs_rinfo.rn_ts_prefix = 0;
            this_ts = (u_int64_t) re.re_ts;
        } else {
            /* WAR 23031- patch duplicate ts on very short pulses */
            /* This pacth has two problems in linux environment.
             * 1)The time stamp created and hence PRI depends entirely on the latency.
             *   If the latency is high, it possibly can split two consecutive
             *   pulses in the same burst so far away (the same amount of latency)
             *   that make them look like they are from differenct bursts. It is
             *   observed to happen too often. It sure makes the detection fail.
             * 2)Even if the latency is not that bad, it simply shifts the duplicate
             *   timestamps to a new duplicate timestamp based on how they are processed.
             *   This is not worse but not good either.
             *
             *   Take this pulse as a good one and create a probable PRI later
             */
            if (re.re_dur == 0 && re.re_ts == dfs->dfs_rinfo.rn_last_unique_ts) {
                debug_dup[debug_dup_cnt++] = '1';
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS1, "\n %s deltaT is 0 \n", __func__);
            } else {
                dfs->dfs_rinfo.rn_last_unique_ts = re.re_ts;
                debug_dup[debug_dup_cnt++] = '0';
            }
            if (debug_dup_cnt >= 32){
                debug_dup_cnt = 0;
            }


            if (re.re_ts <= dfs->dfs_rinfo.rn_last_ts) {
                dfs->dfs_rinfo.rn_ts_prefix += 
                    (((u_int64_t) 1) << DFS_TSSHIFT);
                /* Now, see if it's been more than 1 wrap */
                deltafull_ts = re.re_full_ts - dfs->dfs_rinfo.rn_lastfull_ts;
                if (deltafull_ts > 
                        ((u_int64_t)((DFS_TSMASK - dfs->dfs_rinfo.rn_last_ts) + 1 + re.re_ts)))
                    deltafull_ts -= (DFS_TSMASK - dfs->dfs_rinfo.rn_last_ts) + 1 + re.re_ts;
                deltafull_ts = deltafull_ts >> DFS_TSSHIFT;
                if (deltafull_ts > 1) {
                    dfs->dfs_rinfo.rn_ts_prefix += 
                        ((deltafull_ts - 1) << DFS_TSSHIFT);
                }
            } else {
                deltafull_ts = re.re_full_ts - dfs->dfs_rinfo.rn_lastfull_ts;
                if (deltafull_ts > (u_int64_t) DFS_TSMASK) {
                    deltafull_ts = deltafull_ts >> DFS_TSSHIFT;
                    dfs->dfs_rinfo.rn_ts_prefix += 
                        ((deltafull_ts - 1) << DFS_TSSHIFT);
                }
            }
            this_ts = dfs->dfs_rinfo.rn_ts_prefix | ((u_int64_t) re.re_ts);
        }
        dfs->dfs_rinfo.rn_lastfull_ts = re.re_full_ts;
        dfs->dfs_rinfo.rn_last_ts = re.re_ts;

        re.re_dur = dfs_process_pulse_dur(dfs, re.re_dur);
        if (re.re_dur != 1) {
            this_ts -= re.re_dur;
        }

        /* Save the pulse parameters in the pulse buffer(pulse line) */
        index = (pl->pl_lastelem + 1) & DFS_MAX_PULSE_BUFFER_MASK;
        if (pl->pl_numelems == DFS_MAX_PULSE_BUFFER_SIZE)
            pl->pl_firstelem = (pl->pl_firstelem+1) & DFS_MAX_PULSE_BUFFER_MASK;
        else
            pl->pl_numelems++;
        pl->pl_lastelem = index;
        pl->pl_elems[index].p_time = this_ts;
        pl->pl_elems[index].p_dur = re.re_dur;
        pl->pl_elems[index].p_rssi = re.re_rssi;
        diff_ts = (u_int32_t)this_ts - test_ts;
        test_ts = (u_int32_t)this_ts;
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS1,"ts%u %u %u diff %u pl->pl_lastelem.p_time=%llu\n",(u_int32_t)this_ts, re.re_dur, re.re_rssi, diff_ts, (unsigned long long)pl->pl_elems[index].p_time);

        found = 0;
        for (p=0; (p<dfs->dfs_rinfo.rn_numbin5radars)&&(!found); p++) {
            struct dfs_bin5radars *br;
            u_int32_t b5_rssithresh;
            br = &(dfs->dfs_b5radars[p]);
            b5_rssithresh = br->br_pulse.b5_rssithresh;

            /* Adjust the filter threshold for rssi in non TURBO mode*/
            //XXX: If turbo mode, ar events would be used?
            //if( ! (sc->sc_curchan.channelFlags & CHANNEL_TURBO) ) {
                b5_rssithresh += br->br_pulse.b5_rssimargin;
            //}

            if ((re.re_dur >= br->br_pulse.b5_mindur) &&
                    (re.re_dur <= br->br_pulse.b5_maxdur) &&
                    (re.re_rssi >= b5_rssithresh)) {

                // This is a valid Bin5 pulse, check if it belongs to a burst
                re.re_dur = dfs_retain_bin5_burst_pattern(dfs, diff_ts, re.re_dur);
                // Remember our computed duration for the next pulse in the burst (if needed)
                dfs->dfs_rinfo.dfs_bin5_chirp_ts = this_ts;
                dfs->dfs_rinfo.dfs_last_bin5_dur = re.re_dur;


                if( dfs_bin5_addpulse(dfs, br, &re, this_ts) ) {
                    found |= dfs_bin5_check(dfs);
                }
            } else 
                        DFS_DPRINTK(dfs, ATH_DEBUG_DFS3, "%s too low to be Bin5 pulse (%d)\n", __func__, re.re_dur);
        }
        if (found) {
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: Found bin5 radar\n", __func__);
            retval |= found;
            goto dfsfound;
        }
        tabledepth = 0;
        rf = NULL;

        while ((tabledepth < DFS_MAX_RADAR_OVERLAP) &&
                ((dfs->dfs_radartable[re.re_dur])[tabledepth] != -1) &&
                (!retval)) {
            ft = dfs->dfs_radarf[((dfs->dfs_radartable[re.re_dur])[tabledepth])];

            if (re.re_rssi < ft->ft_rssithresh && re.re_dur > 4) {
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,"%s : Rejecting on rssi rssi=%u thresh=%u\n", __func__, re.re_rssi, ft->ft_rssithresh);
                tabledepth++;
                ATH_DFSQ_LOCK(dfs);
                empty = STAILQ_EMPTY(&(dfs->dfs_radarq));
                ATH_DFSQ_UNLOCK(dfs);
                continue;
            }
            deltaT = this_ts - ft->ft_last_ts;
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,"deltaT = %lld (ts: 0x%llx) (last ts: 0x%llx)\n",(unsigned long long)deltaT, (unsigned long long)this_ts, (unsigned long long)ft->ft_last_ts);
            if ((deltaT < ft->ft_minpri) && (deltaT !=0)){
                /* This check is for the whole filter type. Individual filters
                   will check this again. This is first line of filtering.*/
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS2, "%s: Rejecting on pri pri=%lld minpri=%u\n", __func__, (unsigned long long)deltaT, ft->ft_minpri);
                tabledepth++;
                continue;
            }
            for (p=0, found = 0; (p<ft->ft_numfilters) && (!found); p++) {
                rf = &(ft->ft_filters[p]);
                if ((re.re_dur >= rf->rf_mindur) && (re.re_dur <= rf->rf_maxdur)) {
                    /* The above check is probably not necessary */
                    deltaT = this_ts - rf->rf_dl.dl_last_ts;
                    if (deltaT < 0)
                        deltaT = (int64_t) ((DFS_TSF_WRAP - rf->rf_dl.dl_last_ts) + this_ts +1);
                    if ((deltaT < rf->rf_minpri) && (deltaT != 0)) {
                        /* Second line of PRI filtering. */
                        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                                "filterID %d : Rejecting on individual filter min PRI deltaT=%lld rf->rf_minpri=%u\n",
                                rf->rf_pulseid, (unsigned long long)deltaT, rf->rf_minpri);
                        continue;
                    }

                    if ((rf->rf_patterntype==2) && (deltaT > rf->rf_maxpri) ) {
                        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                                "filterID %d : Staggered - Rejecting on individual filter max PRI deltaT=%lld rf->rf_maxpri=%u\n",
                                rf->rf_pulseid, (unsigned long long)deltaT, rf->rf_maxpri);
                        /* But update the last time stamp */
                        rf->rf_dl.dl_last_ts = this_ts;
                        continue;
                    }

                    if ((rf->rf_patterntype!= 2) && (deltaT > rf->rf_maxpri) && (deltaT < (2*rf->rf_minpri))) {
                        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                                "filterID %d : Rejecting on individual filter max PRI deltaT=%lld rf->rf_minpri=%u\n",
                                rf->rf_pulseid, (unsigned long long)deltaT, rf->rf_minpri);
                        /* But update the last time stamp */
                        rf->rf_dl.dl_last_ts = this_ts;
                        continue;
                    }
                    dfs_add_pulse(dfs, rf, &re, deltaT);
                    /* If this is an extension channel event, flag it for false alarm reduction */
                    if (re.re_chanindextype == EXT_CH)
                        ext_chan_event_flag = 1;

                    if (rf->rf_patterntype == 2)
                        found = dfs_staggered_check(dfs, rf, (u_int32_t) deltaT, re.re_dur, re.re_ext_chan_busy);
                    else
                        found = dfs_bin_check(dfs, rf, (u_int32_t) deltaT, re.re_dur, ext_chan_event_flag, re.re_ext_chan_busy);

                    dfs_print_delayline(dfs, &rf->rf_dl);
                    rf->rf_dl.dl_last_ts = this_ts;
                }
            } 
            ft->ft_last_ts = this_ts;
            retval |= found;
            if (found) {
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS,
                        "Found on channel minDur = %d, filterId = %d\n",ft->ft_mindur,
                        rf != NULL ? rf->rf_pulseid : -1);
            }
            tabledepth++;
        }
        ATH_DFSQ_LOCK(dfs);
        empty = STAILQ_EMPTY(&(dfs->dfs_radarq));
        ATH_DFSQ_UNLOCK(dfs);
    }
dfsfound:

      return retval;
}

#endif /* ATH_SUPPORT_DFS */
