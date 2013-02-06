/*
 * Linux 2.6.32 and later Kernel module for VMware MVP PVTCP Server
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
 * @brief Server (offload) side code.
 */

#include "pvtcp.h"

/**
 * @brief Allocates the net buffer.
 * @param size buffer size
 * @return address of buffer or NULL
 */
void *
PvtcpBufAlloc(unsigned int size)
{
   PvtcpOffBuf *buf;

   /* coverity[alloc_fn] */
   /* coverity[var_assign] */
   buf = CommOS_Kmalloc(size + sizeof *buf - sizeof buf->data);
   if (buf) {
      CommOS_ListInit(&buf->link);
      buf->len = (unsigned short)size;
      buf->off = 0;
      return PvtcpOffBufFromInternal(buf);
   }
   return NULL;
}


/**
 * @brief Deallocates given net buffer.
 * @param buf buffer to deallocate
 * @sideeffect Frees memory
 */

void
PvtcpBufFree(void *buf)
{
   CommOS_Kfree(PvtcpOffInternalFromBuf(buf));
}


/**
 * @brief Initializes the Pvtcp socket offload common fields.
 * @param pvsk pvtcp socket.
 * @param channel Comm channel this socket is associated with.
 * @return 0 if successful, -1 otherwise.
 */

int
PvtcpOffSockInit(PvtcpSock *pvsk,
                 CommChannel channel)
{
   int rc = PvtcpSockInit(pvsk, channel);

   pvsk->opFlags = 0;
   pvsk->flags = 0;
   return rc;
}
