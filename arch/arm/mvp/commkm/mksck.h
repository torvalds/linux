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

#ifndef _MKSCK_H
#define _MKSCK_H

/**
 * @file
 *
 * @brief The monitor-kernel socket interface definitions.
 *
 * The monitor kernel socket interface was created for (what the name
 * says) communications between the monitor and host processes. On the
 * monitor side a special API is introduced, see mksck_vmm.h. On the
 * host side the API is the standard Berkeley socket interface. Host
 * process to host process or monitor to monitor communication is not
 * supported.
 *
 * A generic address consists of two 16 bit fields: the vm id and the
 * port id.  Both hosts (vmx) and monitors (vmm) get their vm id
 * automatically.  The host vm id is assigned at the time the host
 * process opens the mvpkm file descriptor, while the monitor vm id is
 * assigned when the vmx.c:SetupWorldSwitchPage() calls
 * Mvpkm_SetupIds(). As a vmx may create multiple monitors to service
 * an MP guest, a vmx vm id may be associated with multiple monitor vm
 * ids. A monitor id, however, has a single associated vmx host id,
 * the id of its canonical vmx.
 *
 * Sockets on the host get their addresses either by explicit user
 * call (the bind command) or implicitly by (issuing a send command
 * first). At an explicit bind the user may omit one or both fields by
 * providing MKSCK_VMID_UNDEF/MKSCK_PORT_UNDEF respectively. An
 * implicit bind behaves as if both fields were omitted in an explicit
 * bind. The default value of the vmid field is the vmid computed from
 * the thread group id while that of a port is a new number. It is not
 * invalid to bind a host process socket with a vm id different from
 * the vmid computed from the tgid.
 *
 * Sockets of the monitor are automatically assigned a vmid, that of their
 * monitor, at the time of their creation. The port id can be assigned by the
 * user or left to the implementation to assign an unused one (by specifying
 * MKSCK_PORT_UNDEF at @ref Mksck_Open).
 *
 * Host unconnected sockets may receive from any monitor sender, may send to any
 * monitor socket. A socket can be connected to a peer address, that enables the
 * use of the send command.
 *
 * One of many special predefined port (both host and monitor) is
 * MKSCK_PORT_MASTER. It is used for initialization.
 *
 * Monitor sockets have to send their peer address explicitly (by
 * Mksck_SetPeer()) or implicitly by receiving first. After the peer
 * is set, monitor sockets may send or receive only to/from their
 * peer.
 */


#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "vmid.h"

/*
 * The interface limits the size of transferable packets.
 */
#define MKSCK_XFER_MAX        1024

#define MKSCK_ADDR_UNDEF      (uint32)0xffffffff

#define MKSCK_PORT_UNDEF            (uint16)0xffff
#define MKSCK_PORT_MASTER           (MKSCK_PORT_UNDEF-1)
#define MKSCK_PORT_HOST_FB          (MKSCK_PORT_UNDEF-2)
#define MKSCK_PORT_BALLOON          (MKSCK_PORT_UNDEF-3)
#define MKSCK_PORT_HOST_HID         (MKSCK_PORT_UNDEF-4)
#define MKSCK_PORT_CHECKPOINT       (MKSCK_PORT_UNDEF-5)
#define MKSCK_PORT_COMM_EV          (MKSCK_PORT_UNDEF-6)
#define MKSCK_PORT_HIGH             (MKSCK_PORT_UNDEF-7)

#define MKSCK_VMID_UNDEF      VMID_UNDEF
#define MKSCK_VMID_HIGH       (MKSCK_VMID_UNDEF-1)

#define MKSCK_DETACH          3

typedef uint16 Mksck_Port;
typedef VmId   Mksck_VmId;

/**
 * @brief Page descriptor for typed messages. Each page describes a region of
 *        the machine address space with base mpn and size 2^(12 + order) bytes.
 */
typedef struct {
   uint32 mpn   : 20; ///< Base MPN of region described by page
   uint32 order : 12; ///< Region is 2^(12 + order) bytes.
} Mksck_PageDesc;

/**
 * @brief Typed message template macro. Allows us to avoid having two message
 *        types, one with page descriptor vector (for VMM), one without (for
 *        VMX).
 *
 * @param type C type of uninterpreted component of the message (following the
 *             page descriptor vector).
 * @param pages number of page descriptors in vector.
 */
#define MKSCK_DESC_TYPE(type,pages) \
   struct { \
      type umsg; \
      Mksck_PageDesc page[pages]; \
   }

/**
 * @brief The monitor kernel socket interface address format
 */
typedef union {
   uint32        addr; ///< the address
   struct {            /* The address is decomposed to two shorts */
      Mksck_Port port; ///< port unique within a vmid
      Mksck_VmId vmId; ///< unique vmid
   };
} Mksck_Address;

static inline uint32
Mksck_AddrInit(Mksck_VmId vmId, Mksck_Port port)
{
   Mksck_Address aa;
   aa.vmId = vmId;
   aa.port = port;
   return aa.addr;
}
#endif
