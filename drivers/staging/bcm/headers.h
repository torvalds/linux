
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
#include <net/ip.h>
#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>

#include <linux/version.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/kthread.h>
#endif
#include <linux/tcp.h>
#include <linux/udp.h>
#ifndef BCM_SHM_INTERFACE
#include <linux/usb.h>
#endif
#ifdef BECEEM_TARGET

#include <mac_common.h>
#include <msg_Dsa.h>
#include <msg_Dsc.h>
#include <msg_Dsd.h>
#include <sch_definitions.h>
using namespace Beceem;
#ifdef ENABLE_CORRIGENDUM2_UPDATE
extern B_UINT32 g_u32Corr2MacFlags;
#endif
#endif

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
#include "HostMibs.h"
#include "IPv6ProtocolHdr.h"
#include "osal_misc.h"
#include "PHSModule.h"
#include "Protocol.h"
#include "Prototypes.h"
#include "Queue.h"
#include "vendorspecificextn.h"

#ifndef BCM_SHM_INTERFACE

#include "InterfaceMacros.h"
#include "InterfaceAdapter.h"
#include "InterfaceIsr.h"
#include "Interfacemain.h"
#include "InterfaceMisc.h"
#include "InterfaceRx.h"
#include "InterfaceTx.h"
#endif
#include "InterfaceIdleMode.h"
#include "InterfaceInit.h"

#ifdef BCM_SHM_INTERFACE
#include <linux/cpe_config.h>

#ifdef GDMA_INTERFACE
#include "GdmaInterface.h"
#include "symphony.h"
#else
#include "virtual_interface.h"

#endif

#endif

#endif
