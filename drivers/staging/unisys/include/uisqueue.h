/* uisqueue.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * Unisys IO Virtualization header NOTE: This file contains only Linux
 * specific structs.  All OS-independent structs are in iochannel.h.xx
 */

#ifndef __UISQUEUE_H__
#define __UISQUEUE_H__

#include "linux/version.h"
#include "iochannel.h"
#include "uniklog.h"
#include <linux/atomic.h>
#include <linux/semaphore.h>
#include <linux/uuid.h>

#include "controlvmchannel.h"
#include "controlvmcompletionstatus.h"

struct uisqueue_info {

	CHANNEL_HEADER __iomem *chan;
	/* channel containing queues in which scsi commands &
	 * responses are queued
	 */
	u64 packets_sent;
	u64 packets_received;
	u64 interrupts_sent;
	u64 interrupts_received;
	u64 max_not_empty_cnt;
	u64 total_wakeup_cnt;
	u64 non_empty_wakeup_cnt;

	struct {
		SIGNAL_QUEUE_HEADER reserved1;	/*  */
		SIGNAL_QUEUE_HEADER reserved2;	/*  */
	} safe_uis_queue;
	unsigned int (*send_int_if_needed)(struct uisqueue_info *info,
					   unsigned int whichcqueue,
					   unsigned char issue_irq_if_empty,
					   u64 irq_handle,
					   unsigned char io_termination);
};

/* uisqueue_put_cmdrsp_with_lock_client queues a commmand or response
 * to the specified queue, at the tail if the queue is full but
 * oktowait == 0, then it return 0 indicating failure.  otherwise it
 * wait for the queue to become non-full. If command is queued, return
 * 1 for success.
 */
#define DONT_ISSUE_INTERRUPT 0
#define ISSUE_INTERRUPT		 1

#define DONT_WAIT			 0
#define OK_TO_WAIT			 1
#define UISLIB_LOCK_PREFIX \
		".section .smp_locks,\"a\"\n"   \
		_ASM_ALIGN "\n"                 \
		_ASM_PTR "661f\n" /* address */ \
		".previous\n"                   \
		"661:\n\tlock; "

unsigned long long uisqueue_interlocked_or(unsigned long long __iomem *tgt,
					   unsigned long long set);
unsigned long long uisqueue_interlocked_and(unsigned long long __iomem *tgt,
					    unsigned long long set);

int uisqueue_put_cmdrsp_with_lock_client(struct uisqueue_info *queueinfo,
					 struct uiscmdrsp *cmdrsp,
					 unsigned int queue,
					 void *insertlock,
					 unsigned char issue_irq_if_empty,
					 u64 irq_handle,
					 char oktowait,
					 u8 *channel_id);

/* uisqueue_get_cmdrsp gets the cmdrsp entry at the head of the queue
 * and copies it to the area pointed by cmdrsp param.
 * returns 0 if queue is empty, 1 otherwise
 */
int

uisqueue_get_cmdrsp(struct uisqueue_info *queueinfo, void *cmdrsp,
		    unsigned int queue);

#define MAX_NAME_SIZE_UISQUEUE 64

struct extport_info {
	u8 valid:1;
	/* if 1, indicates this extport slot is occupied
	 * if 0, indicates that extport slot is unoccupied */

	u32 num_devs_using;
	/* When extport is added, this is set to 0.  For exports
	* located in NETWORK switches:
	* Each time a VNIC, i.e., intport, is added to the switch this
	* is used to assign a pref_pnic for the VNIC and when assigned
	* to a VNIC this counter is incremented. When a VNIC is
	* deleted, the extport corresponding to the VNIC's pref_pnic
	* is located and its num_devs_using is decremented. For VNICs,
	* num_devs_using is basically used to load-balance transmit
	* traffic from VNICs.
	*/

	struct switch_info *swtch;
	struct PciId pci_id;
	char name[MAX_NAME_SIZE_UISQUEUE];
	union {
		struct vhba_wwnn wwnn;
		unsigned char macaddr[MAX_MACADDR_LEN];
	};
};

struct device_info {
	void __iomem *chanptr;
	u64 channel_addr;
	u64 channel_bytes;
	uuid_le channel_uuid;
	uuid_le instance_uuid;
	struct InterruptInfo intr;
	struct switch_info *swtch;
	char devid[30];		/* "vbus<busno>:dev<devno>" */
	u16 polling;
	struct semaphore interrupt_callback_lock;
	u32 bus_no;
	u32 dev_no;
	int (*interrupt)(void *);
	void *interrupt_context;
	void *private_data;
	struct list_head list_polling_device_channels;
	unsigned long long moved_to_tail_cnt;
	unsigned long long first_busy_cnt;
	unsigned long long last_on_list_cnt;
};

typedef enum {
	RECOVERY_LAN = 1,
	IB_LAN = 2
} SWITCH_TYPE;

struct bus_info {
	u32 busNo, deviceCount;
	struct device_info **device;
	u64 guestHandle, recvBusInterruptHandle;
	uuid_le busInstGuid;
	ULTRA_VBUS_CHANNEL_PROTOCOL __iomem *pBusChannel;
	int busChannelBytes;
	struct proc_dir_entry *proc_dir;	/* proc/uislib/vbus/<x> */
	struct proc_dir_entry *proc_info;	/* proc/uislib/vbus/<x>/info */
	char name[25];
	char partitionName[99];
	struct bus_info *next;
	u8 localVnic;		/* 1 if local vnic created internally
				 * by IOVM; 0 otherwise... */
};

#define DEDICATED_SWITCH(s) ((s->extPortCount == 1) &&	\
			     (s->intPortCount == 1))

struct sn_list_entry {
	struct uisscsi_dest pdest;	/* scsi bus, target, lun for
					 * phys disk */
	u8 sernum[MAX_SERIAL_NUM];	/* serial num of physical
					 * disk.. The length is always
					 * MAX_SERIAL_NUM, padded with
					 * spaces */
	struct sn_list_entry *next;
};

struct network_policy {
	u32 promiscuous:1;
	u32 macassign:1;
	u32 peerforwarding:1;
	u32 nonotify:1;
	u32 standby:1;
	u32 callhome:2;
	char ip_addr[30];
};

/*
 * IO messages sent to UisnicControlChanFunc & UissdControlChanFunc by
 * code that processes the ControlVm channel messages.
 */


typedef enum {
	IOPART_ADD_VNIC,
	IOPART_DEL_VNIC,
	IOPART_DEL_ALL_VNICS,
	IOPART_ADD_VHBA,
	IOPART_ADD_VDISK,
	IOPART_DEL_VHBA,
	IOPART_DEL_VDISK,
	IOPART_DEL_ALL_VDISKS_FOR_VHBA,
	IOPART_DEL_ALL_VHBAS,
	IOPART_ATTACH_PHBA,
	IOPART_DETACH_PHBA,	/* 10 */
	IOPART_ATTACH_PNIC,
	IOPART_DETACH_PNIC,
	IOPART_DETACH_VHBA,
	IOPART_DETACH_VNIC,
	IOPART_PAUSE_VDISK,
	IOPART_RESUME_VDISK,
	IOPART_ADD_DEVICE,	/* add generic device */
	IOPART_DEL_DEVICE,	/* del generic device */
} IOPART_MSG_TYPE;

struct add_virt_iopart {
	void *chanptr;		/* pointer to data channel */
	u64 guest_handle;	/* used to convert guest physical
				 * address to real physical address
				 * for DMA, for ex. */
	u64 recv_bus_irq_handle;	/* used to register to receive
					 * bus level interrupts. */
	struct InterruptInfo intr;	/* contains recv & send
					 * interrupt info */
	/* recvInterruptHandle is used to register to receive
	* interrupts on the data channel. Used by GuestLinux/Windows
	* IO drivers to connect to interrupt.  sendInterruptHandle is
	* used by IOPart drivers as parameter to
	* Issue_VMCALL_IO_QUEUE_TRANSITION to interrupt thread in
	* guest linux/windows IO drivers when data channel queue for
	* vhba/vnic goes from EMPTY to NON-EMPTY. */
	struct switch_info *swtch;	/* pointer to the virtual
					 * switch to which the vnic is
					 * connected */

	u8 use_g2g_copy;	/* Used to determine if a virtual HBA
				 * needs to use G2G copy. */
	u8 filler[7];

	u32 bus_no;
	u32 dev_no;
	char *params;
	ulong params_bytes;

};

struct add_vdisk_iopart {
	void *chanptr;		      /* pointer to data channel */
	int implicit;
	struct uisscsi_dest vdest;    /* scsi bus, target, lun for virt disk */
	struct uisscsi_dest pdest;    /* scsi bus, target, lun for phys disk */
	u8 sernum[MAX_SERIAL_NUM];    /* serial num of physical disk */
	u32 serlen;		      /* length of serial num */
	u32 bus_no;
	u32 dev_no;
};

struct del_vdisk_iopart {
	void *chanptr;		     /* pointer to data channel */
	struct uisscsi_dest vdest;   /* scsi bus, target, lun for virt disk */
	u32 bus_no;
	u32 dev_no;
};

struct del_virt_iopart {
	void *chanptr;		     /* pointer to data channel */
	u32 bus_no;
	u32 dev_no;
};

struct det_virt_iopart {	     /* detach internal port */
	void *chanptr;		     /* pointer to data channel */
	struct switch_info *swtch;
};

struct paures_vdisk_iopart {
	void *chanptr;		     /* pointer to data channel */
	struct uisscsi_dest vdest;   /* scsi bus, target, lun for virt disk */
};

struct add_switch_iopart {	     /* add switch */
	struct switch_info *swtch;
	char *params;
	ulong params_bytes;
};

struct del_switch_iopart {	     /* destroy switch */
	struct switch_info *swtch;
};

struct io_msgs {

	IOPART_MSG_TYPE msgtype;

	/* additional params needed by some messages */
	union {
		struct add_virt_iopart add_vhba;
		struct add_virt_iopart add_vnic;
		struct add_vdisk_iopart add_vdisk;
		struct del_virt_iopart del_vhba;
		struct del_virt_iopart del_vnic;
		struct det_virt_iopart det_vhba;
		struct det_virt_iopart det_vnic;
		struct del_vdisk_iopart del_vdisk;
		struct del_virt_iopart del_all_vdisks_for_vhba;
		struct add_virt_iopart add_device;
		struct del_virt_iopart del_device;
		struct det_virt_iopart det_intport;
		struct add_switch_iopart add_switch;
		struct del_switch_iopart del_switch;
		struct extport_info *ext_port;	/* for attach or detach
						 * pnic/generic delete all
						 * vhbas/allvnics need no
						 * parameters */
		struct paures_vdisk_iopart paures_vdisk;
	};
};

/*
* Guest messages sent to VirtControlChanFunc by code that processes
* the ControlVm channel messages.
*/

typedef enum {
	GUEST_ADD_VBUS,
	GUEST_ADD_VHBA,
	GUEST_ADD_VNIC,
	GUEST_DEL_VBUS,
	GUEST_DEL_VHBA,
	GUEST_DEL_VNIC,
	GUEST_DEL_ALL_VHBAS,
	GUEST_DEL_ALL_VNICS,
	GUEST_DEL_ALL_VBUSES,	/* deletes all vhbas & vnics on all
				 * buses and deletes all buses */
	GUEST_PAUSE_VHBA,
	GUEST_PAUSE_VNIC,
	GUEST_RESUME_VHBA,
	GUEST_RESUME_VNIC
} GUESTPART_MSG_TYPE;

struct add_vbus_guestpart {
	void __iomem *chanptr;		/* pointer to data channel for bus -
					 * NOT YET USED */
	u32 busNo;		/* bus number to be created/deleted */
	u32 deviceCount;	/* max num of devices on bus */
	uuid_le busTypeGuid;	/* indicates type of bus */
	uuid_le busInstGuid;	/* instance guid for device */
};

struct del_vbus_guestpart {
	u32 busNo;		/* bus number to be deleted */
	/* once we start using the bus's channel, add can dump busNo
	* into the channel header and then delete will need only one
	* parameter, chanptr. */
};

struct add_virt_guestpart {
	void __iomem *chanptr;		/* pointer to data channel */
	u32 busNo;		/* bus number for the operation */
	u32 deviceNo;		/* number of device on the bus */
	uuid_le devInstGuid;	/* instance guid for device */
	struct InterruptInfo intr;	/* recv/send interrupt info */
	/* recvInterruptHandle contains info needed in order to
	 * register to receive interrupts on the data channel.
	 * sendInterruptHandle contains handle which is provided to
	 * monitor VMCALL that will cause an interrupt to be generated
	 * for the other end.
	 */
};

struct pause_virt_guestpart {
	void __iomem *chanptr;		/* pointer to data channel */
};

struct resume_virt_guestpart {
	void __iomem *chanptr;		/* pointer to data channel */
};

struct del_virt_guestpart {
	void __iomem *chanptr;		/* pointer to data channel */
};

struct init_chipset_guestpart {
	u32 busCount;		/* indicates the max number of busses */
	u32 switchCount;	/* indicates the max number of switches */
};

struct guest_msgs {

	GUESTPART_MSG_TYPE msgtype;

	/* additional params needed by messages */
	union {
		struct add_vbus_guestpart add_vbus;
		struct add_virt_guestpart add_vhba;
		struct add_virt_guestpart add_vnic;
		struct pause_virt_guestpart pause_vhba;
		struct pause_virt_guestpart pause_vnic;
		struct resume_virt_guestpart resume_vhba;
		struct resume_virt_guestpart resume_vnic;
		struct del_vbus_guestpart del_vbus;
		struct del_virt_guestpart del_vhba;
		struct del_virt_guestpart del_vnic;
		struct del_vbus_guestpart del_all_vhbas;
		struct del_vbus_guestpart del_all_vnics;
		/* del_all_vbuses needs no parameters */
	};
	struct init_chipset_guestpart init_chipset;

};

#endif				/* __UISQUEUE_H__ */
