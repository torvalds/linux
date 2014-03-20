/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef USNIC_LOG_H_
#define USNIC_LOG_H_

#include "usnic.h"

extern unsigned int usnic_log_lvl;

#define USNIC_LOG_LVL_NONE		(0)
#define USNIC_LOG_LVL_ERR		(1)
#define USNIC_LOG_LVL_INFO		(2)
#define USNIC_LOG_LVL_DBG		(3)

#define usnic_printk(lvl, args...) \
	do { \
		printk(lvl "%s:%s:%d: ", DRV_NAME, __func__, \
				__LINE__); \
		printk(args); \
	} while (0)

#define usnic_dbg(args...) \
	do { \
		if (unlikely(usnic_log_lvl >= USNIC_LOG_LVL_DBG)) { \
			usnic_printk(KERN_INFO, args); \
	} \
} while (0)

#define usnic_info(args...) \
do { \
	if (usnic_log_lvl >= USNIC_LOG_LVL_INFO) { \
			usnic_printk(KERN_INFO, args); \
	} \
} while (0)

#define usnic_err(args...) \
	do { \
		if (usnic_log_lvl >= USNIC_LOG_LVL_ERR) { \
			usnic_printk(KERN_ERR, args); \
		} \
	} while (0)
#endif /* !USNIC_LOG_H_ */
