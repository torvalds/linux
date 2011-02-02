//------------------------------------------------------------------------------
// <copyright file="dset_api.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Host-side DataSet API.
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _DSET_API_H_
#define _DSET_API_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Host-side DataSet support is optional, and is not
 * currently required for correct operation.  To disable
 * Host-side DataSet support, set this to 0.
 */
#ifndef CONFIG_HOST_DSET_SUPPORT
#define CONFIG_HOST_DSET_SUPPORT 1
#endif

/* Called to send a DataSet Open Reply back to the Target. */
int wmi_dset_open_reply(struct wmi_t *wmip,
                             u32 status,
                             u32 access_cookie,
                             u32 size,
                             u32 version,
                             u32 targ_handle,
                             u32 targ_reply_fn,
                             u32 targ_reply_arg);

/* Called to send a DataSet Data Reply back to the Target. */
int wmi_dset_data_reply(struct wmi_t *wmip,
                             u32 status,
                             u8 *host_buf,
                             u32 length,
                             u32 targ_buf,
                             u32 targ_reply_fn,
                             u32 targ_reply_arg);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* _DSET_API_H_ */
