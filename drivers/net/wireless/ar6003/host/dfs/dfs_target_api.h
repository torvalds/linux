/*
 * Copyright (c) 2005-2006 Atheros Communications, Inc.
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


#ifndef _DFS_TARGET_API_H_
#define _DFS_TARGET_API_H_

#ifdef ATH_SUPPORT_DFS

#include "ar6000_api.h"

#define DFS_SET_MINRSSITHRESH(dev_hdl, value) ar6000_dfs_set_minrssithresh_cmd((dev_hdl),(value))
#define DFS_SET_MAXPULSEDUR(dev_hdl, value) ar6000_dfs_set_maxpulsedur_cmd((dev_hdl),(value))
#define DFS_RADAR_DETECTED(dev_hdl, ch_idx, bangradar) ar6000_dfs_radar_detected_cmd((dev_hdl),(ch_idx),(bangradar))

#endif /* ATH_SUPPORT_DFS */

#endif  /* _DFS_TARGET_API_ */
