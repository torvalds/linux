/*
 * Copyright (c) 2016 Broadcom
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _BRCMF_PNO_H
#define _BRCMF_PNO_H

#define BRCMF_PNO_SCAN_COMPLETE			1
#define BRCMF_PNO_MAX_PFN_COUNT			16
#define BRCMF_PNO_SCHED_SCAN_MIN_PERIOD	10
#define BRCMF_PNO_SCHED_SCAN_MAX_PERIOD	508

/**
 * brcmf_pno_clean - disable and clear pno in firmware.
 *
 * @ifp: interface object used.
 */
int brcmf_pno_clean(struct brcmf_if *ifp);

/**
 * brcmf_pno_start_sched_scan - initiate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @req: configuration parameters for scheduled scan.
 */
int brcmf_pno_start_sched_scan(struct brcmf_if *ifp,
			       struct cfg80211_sched_scan_request *req);

#endif /* _BRCMF_PNO_H */
