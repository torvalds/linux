/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Guest Communications
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief Comm event signaling, host kernel side.
 */

#include <linux/net.h>

#include "mvp_types.h"
#include "comm_os.h"
#include "comm_transp_impl.h"
#include "mksck_sockaddr.h"
#include "comm_ev.h"
#include "mvpkm_comm_ev.h"

static struct socket *sock;

/**
 * @brief Raises a transport event on the provided event ID (address). This
 *    function is called from a comm_transp provider, such as comm_transp_mvp,
 *    when it needs to signal an event on a given channel.
 * @param targetEvID opaque event channel ID (interpreted by implementation).
 * @param transpID ID of transport to signal.
 * @param eventType event type to raise.
 * @return 0 if successful, -1 otherwise.
 */

int
CommTranspEvent_Raise(unsigned int targetEvID, // unused
                      CommTranspID *transpID,
                      CommTranspIOEvent eventType)
{
   struct sockaddr_mk guestAddr;
   struct msghdr msg;
   struct kvec vec[1];
   int rc;
   CommEvent event;

   if (!transpID) {
      return -1;
   }

   guestAddr.mk_family = AF_MKSCK;
   guestAddr.mk_addr.addr = Mksck_AddrInit(transpID->d32[0], MKSCK_PORT_COMM_EV);

   memset(&msg, 0, sizeof (struct msghdr));
   msg.msg_name    = &guestAddr;
   msg.msg_namelen = sizeof (guestAddr);

   event.id = *transpID;
   event.event = eventType;

   vec[0].iov_base = &event;
   vec[0].iov_len  = sizeof (CommEvent);

   rc = kernel_sendmsg(sock,
                       &msg,
                       vec,
                       1,
                       sizeof (CommEvent));
   rc = (rc < 0) ? -1 : 0;
   return rc;
}


/**
 * @brief Performs one-time, global initialization of event provider.
 * @return 0 if successful, -1 otherwise.
 */
int
CommTranspEvent_Init(void)
{
   struct sockaddr_mk addr = { AF_MKSCK, { .addr = MKSCK_ADDR_UNDEF } };
   int rc;

   rc = sock_create_kern(AF_MKSCK, SOCK_DGRAM, 0, &sock);
   if (rc < 0) {
      goto out;
   }

   rc = kernel_bind(sock, (struct sockaddr *) &addr, sizeof addr);
   if (rc < 0) {
      sock_release(sock);
      sock = NULL;
      goto out;
   }

   Mvpkm_CommEvRegisterProcessCB(CommTranspEvent_Process);

out:
   if (rc) {
      CommOS_Log(("%s: Failed to initialize transport event signaling\n",
                  __FUNCTION__));
   } else {
      CommOS_Log(("%s: Transport event signaling initialization successful\n",
                  __FUNCTION__));
   }
   return rc;
}


/**
 * @brief Performs global clean-up of event provider.
 */

void
CommTranspEvent_Exit(void)
{
   Mvpkm_CommEvUnregisterProcessCB();
   if (sock) {
      sock_release(sock);
      sock = NULL;
   }

   CommOS_Debug(("%s: done.\n", __FUNCTION__));
}
