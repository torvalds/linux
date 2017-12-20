/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __BGW_DESENSE_H_
#define __BGW_DESENSE_H_

#ifdef MSG
#undef MSG
#endif

#ifdef ERR
#undef ERR
#endif

#define PFX1                         "[BWG] "
#define MSG(fmt, arg ...) pr_debug(PFX1 "[D]%s: "  fmt, __func__ , ##arg)
#define ERR(fmt, arg ...) pr_debug(PFX1 "[D]%s: "  fmt, __func__ , ##arg)

#ifdef NETLINK_TEST
#undef NETLINK_TEST
#endif

#define NETLINK_TEST 17

#ifdef MAX_NL_MSG_LEN
#undef MAX_NL_MSG_LEN
#endif

#define MAX_NL_MSG_LEN 1024


#ifdef ON
#undef ON
#endif
#ifdef OFF
#undef OFF
#endif
#ifdef ACK
#undef ACK
#endif

#define ON 1
#define OFF 0
#define ACK 2

/*
used send command to native process

parameter: command could be macro ON: enable co-exist; OFF: disable co-exist;
ACK: after get native process init message send ACK

*/
extern void send_command_to_daemon(const int command);

/*
before use kernel socket, please call init socket first
return value: 0: ok; -1: fail
*/
extern int bgw_init_socket(void);

extern void bgw_destroy_netlink_kernel(void);

#endif
