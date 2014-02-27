//------------------------------------------------------------------------------
// Copyright (c) 2004-2011 Atheros Corporation.  All rights reserved.
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
// Author(s): ="Atheros"
//==============================================================================

/*
 * This file contains the definitions of the host_proxy interface.  
 */

#ifndef _HOST_PROXY_IFACE_H_
#define _HOST_PROXY_IFACE_H_

/* Host proxy initializes shared memory with HOST_PROXY_INIT to 
 * indicate that it is ready to receive instruction */
#define HOST_PROXY_INIT         (1)
/* Host writes HOST_PROXY_NORMAL_BOOT to shared memory to 
 * indicate to host proxy that it should proceed to boot 
 * normally (bypassing BMI).
 */
#define HOST_PROXY_NORMAL_BOOT  (2)
/* Host writes HOST_PROXY_BMI_BOOT to shared memory to
 * indicate to host proxy that is should enable BMI and 
 * exit.  This allows a host to reprogram the on board
 * flash. 
 */
#define HOST_PROXY_BMI_BOOT     (3)

#endif /* _HOST_PROXY_IFACE_H_ */
