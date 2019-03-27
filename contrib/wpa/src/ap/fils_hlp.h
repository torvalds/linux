/*
 * FILS HLP request processing
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef FILS_HLP_H
#define FILS_HLP_H

int fils_process_hlp(struct hostapd_data *hapd, struct sta_info *sta,
		     const u8 *pos, int left);

#ifdef CONFIG_FILS

void fils_hlp_deinit(struct hostapd_data *hapd);

#else /* CONFIG_FILS */

static inline void fils_hlp_deinit(struct hostapd_data *hapd)
{
}

#endif /* CONFIG_FILS */

#endif /* FILS_HLP_H */
