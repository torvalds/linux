
/*******************************************************************
* 		Headers.h
*******************************************************************/
#ifndef __HEADERS_H__
#define __HEADERS_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/if_arp.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/stddef.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/kthread.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/usb.h>
#include <asm/uaccess.h>
#include <net/ip.h>

#include "Typedefs.h"
#include "Version.h"
#include "Macros.h"
#include "HostMIBSInterface.h"
#include "cntrl_SignalingInterface.h"
#include "PHSDefines.h"
#include "led_control.h"
#include "Ioctl.h"
#include "nvm.h"
#include "target_params.h"
#include "Adapter.h"
#include "CmHost.h"
#include "DDRInit.h"
#include "Debug.h"
#include "IPv6ProtocolHdr.h"
#include "PHSModule.h"
#include "Protocol.h"
#include "Prototypes.h"
#include "Queue.h"
#include "vendorspecificextn.h"

#include "InterfaceMacros.h"
#include "InterfaceAdapter.h"
#include "InterfaceIsr.h"
#include "InterfaceMisc.h"
#include "InterfaceRx.h"
#include "InterfaceTx.h"
#include "InterfaceIdleMode.h"
#include "InterfaceInit.h"

#define DRV_NAME	"beceem"
#define DEV_NAME	"tarang"
#define DRV_DESCRIPTION "Beceem Communications Inc. WiMAX driver"
#define DRV_COPYRIGHT	"Copyright 2010. Beceem Communications Inc"
#define DRV_VERSION	VER_FILEVERSION_STR
#define PFX		DRV_NAME " "

#endif
