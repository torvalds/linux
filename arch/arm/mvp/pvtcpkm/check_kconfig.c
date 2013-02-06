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
 * @brief Check for required kernel configuration
 *
 * Check to make sure that the kernel options that the MVP hypervisor requires
 * have been enabled in the kernel that this kernel module is being built
 * against.
 */
#include <linux/version.h>

/*
 * Minimum kernel version
 * - network namespace support is only really functional starting in 2.6.29
 * - Android Gingerbread requires 2.6.35
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
#error "MVP requires a host kernel newer than 2.6.35"
#endif

/* module loading ability */
#ifndef CONFIG_MODULES
#error "MVP requires kernel loadable module support be enabled (CONFIG_MODULES)"
#endif
#ifndef CONFIG_MODULE_UNLOAD
#error "MVP requires kernel module unload support be enabled (CONFIG_MODULE_UNLOAD)"
#endif

/* sysfs */
#ifndef CONFIG_SYSFS
#error "MVP requires sysfs support (CONFIG_SYSFS)"
#endif

/* network traffic isolation */
#ifndef CONFIG_NAMESPACES
#error "MVP networking support requires namespace support (CONFIG_NAMESPACES)"
#endif
#ifndef CONFIG_NET_NS
#error "MVP networking support requires Network Namespace support to be enabled (CONFIG_NET_NS)"
#endif

/* TCP/IP networking */
#ifndef CONFIG_INET
#error "MVP networking requires IPv4 support (CONFIG_INET)"
#endif
#ifndef CONFIG_IPV6
#error "MVP networking requires IPv6 support (CONFIG_IPV6)"
#endif

/* VPN support */
#if !defined(CONFIG_TUN) && !defined(CONFIG_TUN_MODULE)
#error "MVP VPN support requires TUN device support (CONFIG_TUN)"
#endif

#if !defined(CONFIG_NETFILTER) && !defined(PVTCP_DISABLE_NETFILTER)
#error "MVP networking support requires netfilter support (CONFIG_NETFILTER)"
#endif

/* Force /proc/config.gz support for eng/userdebug builds */
#ifdef MVP_DEBUG
#if !defined(CONFIG_IKCONFIG) || !defined(CONFIG_IKCONFIG_PROC)
#error "MVP kernel /proc/config.gz support required for debuggability (CONFIG_IKCONFIG_PROC)"
#endif
#endif

/* Sanity check we're only dealing with the memory hotplug + migrate and/or
 * compaction combo */
#ifdef CONFIG_MIGRATION
#if defined(CONFIG_NUMA) || defined(CONFIG_CPUSETS) || defined(CONFIG_MEMORY_FAILURE)
#error "MVP not tested with migration features other than CONFIG_MEMORY_HOTPLUG and CONFIG_COMPACTION"
#endif
#endif
