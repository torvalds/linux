// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000-2010 Adaptec, Inc.
 *               2010-2015 PMC-Sierra, Inc. (aacraid@pmc-sierra.com)
 *		 2016-2017 Microsemi Corp. (aacraid@microsemi.com)
 *
 * Module Name:
 *   linit.c
 *
 * Abstract: Linux Driver entry module for Adaptec RAID Array Controller
 */


#include <linux/compat.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/pci-aspm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_eh.h>

#include "aacraid.h"

#define AAC_DRIVER_VERSION		"1.2.1"
#ifndef AAC_DRIVER_BRANCH
#define AAC_DRIVER_BRANCH		""
#endif
#define AAC_DRIVERNAME			"aacraid"

#ifdef AAC_DRIVER_BUILD
#define _str(x) #x
#define str(x) _str(x)
#define AAC_DRIVER_FULL_VERSION	AAC_DRIVER_VERSION "[" str(AAC_DRIVER_BUILD) "]" AAC_DRIVER_BRANCH
#else
#define AAC_DRIVER_FULL_VERSION	AAC_DRIVER_VERSION AAC_DRIVER_BRANCH
#endif

MODULE_AUTHOR("Red Hat Inc and Adaptec");
MODULE_DESCRIPTION("Dell PERC2, 2/Si, 3/Si, 3/Di, "
		   "Adaptec Advanced Raid Products, "
		   "HP NetRAID-4M, IBM ServeRAID & ICP SCSI driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(AAC_DRIVER_FULL_VERSION);

static DEFINE_MUTEX(aac_mutex);
static LIST_HEAD(aac_devices);
static int aac_cfg_major = AAC_CHARDEV_UNREGISTERED;
char aac_driver_version[] = AAC_DRIVER_FULL_VERSION;

/*
 * Because of the way Linux names scsi devices, the order in this table has
 * become important.  Check for on-board Raid first, add-in cards second.
 *
 * Note: The last field is used to index into aac_drivers below.
 */
static const struct pci_device_id aac_pci_tbl[] = {
	{ 0x1028, 0x0001, 0x1028, 0x0001, 0, 0, 0 }, /* PERC 2/Si (Iguana/PERC2Si) */
	{ 0x1028, 0x0002, 0x1028, 0x0002, 0, 0, 1 }, /* PERC 3/Di (Opal/PERC3Di) */
	{ 0x1028, 0x0003, 0x1028, 0x0003, 0, 0, 2 }, /* PERC 3/Si (SlimFast/PERC3Si */
	{ 0x1028, 0x0004, 0x1028, 0x00d0, 0, 0, 3 }, /* PERC 3/Di (Iguana FlipChip/PERC3DiF */
	{ 0x1028, 0x0002, 0x1028, 0x00d1, 0, 0, 4 }, /* PERC 3/Di (Viper/PERC3DiV) */
	{ 0x1028, 0x0002, 0x1028, 0x00d9, 0, 0, 5 }, /* PERC 3/Di (Lexus/PERC3DiL) */
	{ 0x1028, 0x000a, 0x1028, 0x0106, 0, 0, 6 }, /* PERC 3/Di (Jaguar/PERC3DiJ) */
	{ 0x1028, 0x000a, 0x1028, 0x011b, 0, 0, 7 }, /* PERC 3/Di (Dagger/PERC3DiD) */
	{ 0x1028, 0x000a, 0x1028, 0x0121, 0, 0, 8 }, /* PERC 3/Di (Boxster/PERC3DiB) */
	{ 0x9005, 0x0283, 0x9005, 0x0283, 0, 0, 9 }, /* catapult */
	{ 0x9005, 0x0284, 0x9005, 0x0284, 0, 0, 10 }, /* tomcat */
	{ 0x9005, 0x0285, 0x9005, 0x0286, 0, 0, 11 }, /* Adaptec 2120S (Crusader) */
	{ 0x9005, 0x0285, 0x9005, 0x0285, 0, 0, 12 }, /* Adaptec 2200S (Vulcan) */
	{ 0x9005, 0x0285, 0x9005, 0x0287, 0, 0, 13 }, /* Adaptec 2200S (Vulcan-2m) */
	{ 0x9005, 0x0285, 0x17aa, 0x0286, 0, 0, 14 }, /* Legend S220 (Legend Crusader) */
	{ 0x9005, 0x0285, 0x17aa, 0x0287, 0, 0, 15 }, /* Legend S230 (Legend Vulcan) */

	{ 0x9005, 0x0285, 0x9005, 0x0288, 0, 0, 16 }, /* Adaptec 3230S (Harrier) */
	{ 0x9005, 0x0285, 0x9005, 0x0289, 0, 0, 17 }, /* Adaptec 3240S (Tornado) */
	{ 0x9005, 0x0285, 0x9005, 0x028a, 0, 0, 18 }, /* ASR-2020ZCR SCSI PCI-X ZCR (Skyhawk) */
	{ 0x9005, 0x0285, 0x9005, 0x028b, 0, 0, 19 }, /* ASR-2025ZCR SCSI SO-DIMM PCI-X ZCR (Terminator) */
	{ 0x9005, 0x0286, 0x9005, 0x028c, 0, 0, 20 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x028d, 0, 0, 21 }, /* ASR-2130S (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x029b, 0, 0, 22 }, /* AAR-2820SA (Intruder) */
	{ 0x9005, 0x0286, 0x9005, 0x029c, 0, 0, 23 }, /* AAR-2620SA (Intruder) */
	{ 0x9005, 0x0286, 0x9005, 0x029d, 0, 0, 24 }, /* AAR-2420SA (Intruder) */
	{ 0x9005, 0x0286, 0x9005, 0x029e, 0, 0, 25 }, /* ICP9024RO (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x029f, 0, 0, 26 }, /* ICP9014RO (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x02a0, 0, 0, 27 }, /* ICP9047MA (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x02a1, 0, 0, 28 }, /* ICP9087MA (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x02a3, 0, 0, 29 }, /* ICP5445AU (Hurricane44) */
	{ 0x9005, 0x0285, 0x9005, 0x02a4, 0, 0, 30 }, /* ICP9085LI (Marauder-X) */
	{ 0x9005, 0x0285, 0x9005, 0x02a5, 0, 0, 31 }, /* ICP5085BR (Marauder-E) */
	{ 0x9005, 0x0286, 0x9005, 0x02a6, 0, 0, 32 }, /* ICP9067MA (Intruder-6) */
	{ 0x9005, 0x0287, 0x9005, 0x0800, 0, 0, 33 }, /* Themisto Jupiter Platform */
	{ 0x9005, 0x0200, 0x9005, 0x0200, 0, 0, 33 }, /* Themisto Jupiter Platform */
	{ 0x9005, 0x0286, 0x9005, 0x0800, 0, 0, 34 }, /* Callisto Jupiter Platform */
	{ 0x9005, 0x0285, 0x9005, 0x028e, 0, 0, 35 }, /* ASR-2020SA SATA PCI-X ZCR (Skyhawk) */
	{ 0x9005, 0x0285, 0x9005, 0x028f, 0, 0, 36 }, /* ASR-2025SA SATA SO-DIMM PCI-X ZCR (Terminator) */
	{ 0x9005, 0x0285, 0x9005, 0x0290, 0, 0, 37 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II) */
	{ 0x9005, 0x0285, 0x1028, 0x0291, 0, 0, 38 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ 0x9005, 0x0285, 0x9005, 0x0292, 0, 0, 39 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ 0x9005, 0x0285, 0x9005, 0x0293, 0, 0, 40 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ 0x9005, 0x0285, 0x9005, 0x0294, 0, 0, 41 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ 0x9005, 0x0285, 0x103C, 0x3227, 0, 0, 42 }, /* AAR-2610SA PCI SATA 6ch */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 43 }, /* ASR-2240S (SabreExpress) */
	{ 0x9005, 0x0285, 0x9005, 0x0297, 0, 0, 44 }, /* ASR-4005 */
	{ 0x9005, 0x0285, 0x1014, 0x02F2, 0, 0, 45 }, /* IBM 8i (AvonPark) */
	{ 0x9005, 0x0285, 0x1014, 0x0312, 0, 0, 45 }, /* IBM 8i (AvonPark Lite) */
	{ 0x9005, 0x0286, 0x1014, 0x9580, 0, 0, 46 }, /* IBM 8k/8k-l8 (Aurora) */
	{ 0x9005, 0x0286, 0x1014, 0x9540, 0, 0, 47 }, /* IBM 8k/8k-l4 (Aurora Lite) */
	{ 0x9005, 0x0285, 0x9005, 0x0298, 0, 0, 48 }, /* ASR-4000 (BlackBird) */
	{ 0x9005, 0x0285, 0x9005, 0x0299, 0, 0, 49 }, /* ASR-4800SAS (Marauder-X) */
	{ 0x9005, 0x0285, 0x9005, 0x029a, 0, 0, 50 }, /* ASR-4805SAS (Marauder-E) */
	{ 0x9005, 0x0286, 0x9005, 0x02a2, 0, 0, 51 }, /* ASR-3800 (Hurricane44) */

	{ 0x9005, 0x0285, 0x1028, 0x0287, 0, 0, 52 }, /* Perc 320/DC*/
	{ 0x1011, 0x0046, 0x9005, 0x0365, 0, 0, 53 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x0364, 0, 0, 54 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x1364, 0, 0, 55 }, /* Dell PERC2/QC */
	{ 0x1011, 0x0046, 0x103c, 0x10c2, 0, 0, 56 }, /* HP NetRAID-4M */

	{ 0x9005, 0x0285, 0x1028, PCI_ANY_ID, 0, 0, 57 }, /* Dell Catchall */
	{ 0x9005, 0x0285, 0x17aa, PCI_ANY_ID, 0, 0, 58 }, /* Legend Catchall */
	{ 0x9005, 0x0285, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 59 }, /* Adaptec Catch All */
	{ 0x9005, 0x0286, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 60 }, /* Adaptec Rocket Catch All */
	{ 0x9005, 0x0288, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 61 }, /* Adaptec NEMER/ARK Catch All */
	{ 0x9005, 0x028b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 62 }, /* Adaptec PMC Series 6 (Tupelo) */
	{ 0x9005, 0x028c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 63 }, /* Adaptec PMC Series 7 (Denali) */
	{ 0x9005, 0x028d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 64 }, /* Adaptec PMC Series 8 */
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, aac_pci_tbl);

/*
 * dmb - For now we add the number of channels to this structure.
 * In the future we should add a fib that reports the number of channels
 * for the card.  At that time we can remove the channels from here
 */
static struct aac_driver_ident aac_drivers[] = {
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 2/Si (Iguana/PERC2Si) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Opal/PERC3Di) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Si (SlimFast/PERC3Si */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Iguana FlipChip/PERC3DiF */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Viper/PERC3DiV) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Lexus/PERC3DiL) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 1, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Jaguar/PERC3DiJ) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Dagger/PERC3DiD) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* PERC 3/Di (Boxster/PERC3DiB) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "catapult        ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* catapult */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "tomcat          ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* tomcat */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2120S   ", 1, AAC_QUIRK_31BIT | AAC_QUIRK_34SG },		      /* Adaptec 2120S (Crusader) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG },		      /* Adaptec 2200S (Vulcan) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* Adaptec 2200S (Vulcan-2m) */
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S220     ", 1, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* Legend S220 (Legend Crusader) */
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S230     ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* Legend S230 (Legend Vulcan) */

	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3230S   ", 2 }, /* Adaptec 3230S (Harrier) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3240S   ", 2 }, /* Adaptec 3240S (Tornado) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020ZCR     ", 2 }, /* ASR-2020ZCR SCSI PCI-X ZCR (Skyhawk) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025ZCR     ", 2 }, /* ASR-2025ZCR SCSI SO-DIMM PCI-X ZCR (Terminator) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-2230S PCI-X ", 2 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-2130S PCI-X ", 1 }, /* ASR-2130S (Lancer) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "AAR-2820SA      ", 1 }, /* AAR-2820SA (Intruder) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "AAR-2620SA      ", 1 }, /* AAR-2620SA (Intruder) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "AAR-2420SA      ", 1 }, /* AAR-2420SA (Intruder) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9024RO       ", 2 }, /* ICP9024RO (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9014RO       ", 1 }, /* ICP9014RO (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9047MA       ", 1 }, /* ICP9047MA (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9087MA       ", 1 }, /* ICP9087MA (Lancer) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP5445AU       ", 1 }, /* ICP5445AU (Hurricane44) */
	{ aac_rx_init, "aacraid",  "ICP     ", "ICP9085LI       ", 1 }, /* ICP9085LI (Marauder-X) */
	{ aac_rx_init, "aacraid",  "ICP     ", "ICP5085BR       ", 1 }, /* ICP5085BR (Marauder-E) */
	{ aac_rkt_init, "aacraid",  "ICP     ", "ICP9067MA       ", 1 }, /* ICP9067MA (Intruder-6) */
	{ NULL        , "aacraid",  "ADAPTEC ", "Themisto        ", 0, AAC_QUIRK_SLAVE }, /* Jupiter Platform */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "Callisto        ", 2, AAC_QUIRK_MASTER }, /* Jupiter Platform */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020SA       ", 1 }, /* ASR-2020SA SATA PCI-X ZCR (Skyhawk) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025SA       ", 1 }, /* ASR-2025SA SATA SO-DIMM PCI-X ZCR (Terminator) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2410SA SATA ", 1, AAC_QUIRK_17SG }, /* AAR-2410SA PCI SATA 4ch (Jaguar II) */
	{ aac_rx_init, "aacraid",  "DELL    ", "CERC SR2        ", 1, AAC_QUIRK_17SG }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2810SA SATA ", 1, AAC_QUIRK_17SG }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-21610SA SATA", 1, AAC_QUIRK_17SG }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2026ZCR     ", 1 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2610SA      ", 1 }, /* SATA 6Ch (Bearcat) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2240S       ", 1 }, /* ASR-2240S (SabreExpress) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4005        ", 1 }, /* ASR-4005 */
	{ aac_rx_init, "ServeRAID","IBM     ", "ServeRAID 8i    ", 1 }, /* IBM 8i (AvonPark) */
	{ aac_rkt_init, "ServeRAID","IBM     ", "ServeRAID 8k-l8 ", 1 }, /* IBM 8k/8k-l8 (Aurora) */
	{ aac_rkt_init, "ServeRAID","IBM     ", "ServeRAID 8k-l4 ", 1 }, /* IBM 8k/8k-l4 (Aurora Lite) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4000        ", 1 }, /* ASR-4000 (BlackBird & AvonPark) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4800SAS     ", 1 }, /* ASR-4800SAS (Marauder-X) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4805SAS     ", 1 }, /* ASR-4805SAS (Marauder-E) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-3800        ", 1 }, /* ASR-3800 (Hurricane44) */

	{ aac_rx_init, "percraid", "DELL    ", "PERC 320/DC     ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG }, /* Perc 320/DC*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "Adaptec 5400S   ", 4, AAC_QUIRK_34SG }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "AAC-364         ", 4, AAC_QUIRK_34SG }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "percraid", "DELL    ", "PERCRAID        ", 4, AAC_QUIRK_34SG }, /* Dell PERC2/QC */
	{ aac_sa_init, "hpnraid",  "HP      ", "NetRAID         ", 4, AAC_QUIRK_34SG }, /* HP NetRAID-4M */

	{ aac_rx_init, "aacraid",  "DELL    ", "RAID            ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* Dell Catchall */
	{ aac_rx_init, "aacraid",  "Legend  ", "RAID            ", 2, AAC_QUIRK_31BIT | AAC_QUIRK_34SG | AAC_QUIRK_SCSI_32 }, /* Legend Catchall */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "RAID            ", 2 }, /* Adaptec Catch All */
	{ aac_rkt_init, "aacraid", "ADAPTEC ", "RAID            ", 2 }, /* Adaptec Rocket Catch All */
	{ aac_nark_init, "aacraid", "ADAPTEC ", "RAID           ", 2 }, /* Adaptec NEMER/ARK Catch All */
	{ aac_src_init, "aacraid", "ADAPTEC ", "RAID            ", 2, AAC_QUIRK_SRC }, /* Adaptec PMC Series 6 (Tupelo) */
	{ aac_srcv_init, "aacraid", "ADAPTEC ", "RAID            ", 2, AAC_QUIRK_SRC }, /* Adaptec PMC Series 7 (Denali) */
	{ aac_srcv_init, "aacraid", "ADAPTEC ", "RAID            ", 2, AAC_QUIRK_SRC }, /* Adaptec PMC Series 8 */
};

/**
 *	aac_queuecommand	-	queue a SCSI command
 *	@cmd:		SCSI command to queue
 *	@done:		Function to call on command completion
 *
 *	Queues a command for execution by the associated Host Adapter.
 *
 *	TODO: unify with aac_scsi_cmd().
 */

static int aac_queuecommand(struct Scsi_Host *shost,
			    struct scsi_cmnd *cmd)
{
	int r = 0;
	cmd->SCp.phase = AAC_OWNER_LOWLEVEL;
	r = (aac_scsi_cmd(cmd) ? FAILED : 0);
	return r;
}

/**
 *	aac_info		-	Returns the host adapter name
 *	@shost:		Scsi host to report on
 *
 *	Returns a static string describing the device in question
 */

static const char *aac_info(struct Scsi_Host *shost)
{
	struct aac_dev *dev = (struct aac_dev *)shost->hostdata;
	return aac_drivers[dev->cardtype].name;
}

/**
 *	aac_get_driver_ident
 *	@devtype: index into lookup table
 *
 *	Returns a pointer to the entry in the driver lookup table.
 */

struct aac_driver_ident* aac_get_driver_ident(int devtype)
{
	return &aac_drivers[devtype];
}

/**
 *	aac_biosparm	-	return BIOS parameters for disk
 *	@sdev: The scsi device corresponding to the disk
 *	@bdev: the block device corresponding to the disk
 *	@capacity: the sector capacity of the disk
 *	@geom: geometry block to fill in
 *
 *	Return the Heads/Sectors/Cylinders BIOS Disk Parameters for Disk.
 *	The default disk geometry is 64 heads, 32 sectors, and the appropriate
 *	number of cylinders so as not to exceed drive capacity.  In order for
 *	disks equal to or larger than 1 GB to be addressable by the BIOS
 *	without exceeding the BIOS limitation of 1024 cylinders, Extended
 *	Translation should be enabled.   With Extended Translation enabled,
 *	drives between 1 GB inclusive and 2 GB exclusive are given a disk
 *	geometry of 128 heads and 32 sectors, and drives above 2 GB inclusive
 *	are given a disk geometry of 255 heads and 63 sectors.  However, if
 *	the BIOS detects that the Extended Translation setting does not match
 *	the geometry in the partition table, then the translation inferred
 *	from the partition table will be used by the BIOS, and a warning may
 *	be displayed.
 */

static int aac_biosparm(struct scsi_device *sdev, struct block_device *bdev,
			sector_t capacity, int *geom)
{
	struct diskparm *param = (struct diskparm *)geom;
	unsigned char *buf;

	dprintk((KERN_DEBUG "aac_biosparm.\n"));

	/*
	 *	Assuming extended translation is enabled - #REVISIT#
	 */
	if (capacity >= 2 * 1024 * 1024) { /* 1 GB in 512 byte sectors */
		if(capacity >= 4 * 1024 * 1024) { /* 2 GB in 512 byte sectors */
			param->heads = 255;
			param->sectors = 63;
		} else {
			param->heads = 128;
			param->sectors = 32;
		}
	} else {
		param->heads = 64;
		param->sectors = 32;
	}

	param->cylinders = cap_to_cyls(capacity, param->heads * param->sectors);

	/*
	 *	Read the first 1024 bytes from the disk device, if the boot
	 *	sector partition table is valid, search for a partition table
	 *	entry whose end_head matches one of the standard geometry
	 *	translations ( 64/32, 128/32, 255/63 ).
	 */
	buf = scsi_bios_ptable(bdev);
	if (!buf)
		return 0;
	if(*(__le16 *)(buf + 0x40) == cpu_to_le16(0xaa55)) {
		struct partition *first = (struct partition * )buf;
		struct partition *entry = first;
		int saved_cylinders = param->cylinders;
		int num;
		unsigned char end_head, end_sec;

		for(num = 0; num < 4; num++) {
			end_head = entry->end_head;
			end_sec = entry->end_sector & 0x3f;

			if(end_head == 63) {
				param->heads = 64;
				param->sectors = 32;
				break;
			} else if(end_head == 127) {
				param->heads = 128;
				param->sectors = 32;
				break;
			} else if(end_head == 254) {
				param->heads = 255;
				param->sectors = 63;
				break;
			}
			entry++;
		}

		if (num == 4) {
			end_head = first->end_head;
			end_sec = first->end_sector & 0x3f;
		}

		param->cylinders = cap_to_cyls(capacity, param->heads * param->sectors);
		if (num < 4 && end_sec == param->sectors) {
			if (param->cylinders != saved_cylinders)
				dprintk((KERN_DEBUG "Adopting geometry: heads=%d, sectors=%d from partition table %d.\n",
					param->heads, param->sectors, num));
		} else if (end_head > 0 || end_sec > 0) {
			dprintk((KERN_DEBUG "Strange geometry: heads=%d, sectors=%d in partition table %d.\n",
				end_head + 1, end_sec, num));
			dprintk((KERN_DEBUG "Using geometry: heads=%d, sectors=%d.\n",
					param->heads, param->sectors));
		}
	}
	kfree(buf);
	return 0;
}

/**
 *	aac_slave_configure		-	compute queue depths
 *	@sdev:	SCSI device we are considering
 *
 *	Selects queue depths for each target device based on the host adapter's
 *	total capacity and the queue depth supported by the target device.
 *	A queue depth of one automatically disables tagged queueing.
 */

static int aac_slave_configure(struct scsi_device *sdev)
{
	struct aac_dev *aac = (struct aac_dev *)sdev->host->hostdata;
	int chn, tid;
	unsigned int depth = 0;
	unsigned int set_timeout = 0;
	bool set_qd_dev_type = false;
	u8 devtype = 0;

	chn = aac_logical_to_phys(sdev_channel(sdev));
	tid = sdev_id(sdev);
	if (chn < AAC_MAX_BUSES && tid < AAC_MAX_TARGETS && aac->sa_firmware) {
		devtype = aac->hba_map[chn][tid].devtype;

		if (devtype == AAC_DEVTYPE_NATIVE_RAW) {
			depth = aac->hba_map[chn][tid].qd_limit;
			set_timeout = 1;
			goto common_config;
		}
		if (devtype == AAC_DEVTYPE_ARC_RAW) {
			set_qd_dev_type = true;
			set_timeout = 1;
			goto common_config;
		}
	}

	if (aac->jbod && (sdev->type == TYPE_DISK))
		sdev->removable = 1;

	if (sdev->type == TYPE_DISK
	 && sdev_channel(sdev) != CONTAINER_CHANNEL
	 && (!aac->jbod || sdev->inq_periph_qual)
	 && (!aac->raid_scsi_mode || (sdev_channel(sdev) != 2))) {

		if (expose_physicals == 0)
			return -ENXIO;

		if (expose_physicals < 0)
			sdev->no_uld_attach = 1;
	}

	if (sdev->tagged_supported
	 &&  sdev->type == TYPE_DISK
	 &&  (!aac->raid_scsi_mode || (sdev_channel(sdev) != 2))
	 && !sdev->no_uld_attach) {

		struct scsi_device * dev;
		struct Scsi_Host *host = sdev->host;
		unsigned num_lsu = 0;
		unsigned num_one = 0;
		unsigned cid;

		set_timeout = 1;

		for (cid = 0; cid < aac->maximum_num_containers; ++cid)
			if (aac->fsa_dev[cid].valid)
				++num_lsu;

		__shost_for_each_device(dev, host) {
			if (dev->tagged_supported
			 && dev->type == TYPE_DISK
			 && (!aac->raid_scsi_mode || (sdev_channel(sdev) != 2))
			 && !dev->no_uld_attach) {
				if ((sdev_channel(dev) != CONTAINER_CHANNEL)
				 || !aac->fsa_dev[sdev_id(dev)].valid) {
					++num_lsu;
				}
			} else {
				++num_one;
			}
		}

		if (num_lsu == 0)
			++num_lsu;

		depth = (host->can_queue - num_one) / num_lsu;

		if (sdev_channel(sdev) != NATIVE_CHANNEL)
			goto common_config;

		set_qd_dev_type = true;

	}

common_config:

	/*
	 * Check if SATA drive
	 */
	if (set_qd_dev_type) {
		if (strncmp(sdev->vendor, "ATA", 3) == 0)
			depth = 32;
		else
			depth = 64;
	}

	/*
	 * Firmware has an individual device recovery time typically
	 * of 35 seconds, give us a margin.
	 */
	if (set_timeout && sdev->request_queue->rq_timeout < (45 * HZ))
		blk_queue_rq_timeout(sdev->request_queue, 45*HZ);

	if (depth > 256)
		depth = 256;
	else if (depth < 1)
		depth = 1;

	scsi_change_queue_depth(sdev, depth);

	sdev->tagged_supported = 1;

	return 0;
}

/**
 *	aac_change_queue_depth		-	alter queue depths
 *	@sdev:	SCSI device we are considering
 *	@depth:	desired queue depth
 *
 *	Alters queue depths for target device based on the host adapter's
 *	total capacity and the queue depth supported by the target device.
 */

static int aac_change_queue_depth(struct scsi_device *sdev, int depth)
{
	struct aac_dev *aac = (struct aac_dev *)(sdev->host->hostdata);
	int chn, tid, is_native_device = 0;

	chn = aac_logical_to_phys(sdev_channel(sdev));
	tid = sdev_id(sdev);
	if (chn < AAC_MAX_BUSES && tid < AAC_MAX_TARGETS &&
		aac->hba_map[chn][tid].devtype == AAC_DEVTYPE_NATIVE_RAW)
		is_native_device = 1;

	if (sdev->tagged_supported && (sdev->type == TYPE_DISK) &&
	    (sdev_channel(sdev) == CONTAINER_CHANNEL)) {
		struct scsi_device * dev;
		struct Scsi_Host *host = sdev->host;
		unsigned num = 0;

		__shost_for_each_device(dev, host) {
			if (dev->tagged_supported && (dev->type == TYPE_DISK) &&
			    (sdev_channel(dev) == CONTAINER_CHANNEL))
				++num;
			++num;
		}
		if (num >= host->can_queue)
			num = host->can_queue - 1;
		if (depth > (host->can_queue - num))
			depth = host->can_queue - num;
		if (depth > 256)
			depth = 256;
		else if (depth < 2)
			depth = 2;
		return scsi_change_queue_depth(sdev, depth);
	} else if (is_native_device) {
		scsi_change_queue_depth(sdev, aac->hba_map[chn][tid].qd_limit);
	} else {
		scsi_change_queue_depth(sdev, 1);
	}
	return sdev->queue_depth;
}

static ssize_t aac_show_raid_level(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct aac_dev *aac = (struct aac_dev *)(sdev->host->hostdata);
	if (sdev_channel(sdev) != CONTAINER_CHANNEL)
		return snprintf(buf, PAGE_SIZE, sdev->no_uld_attach
		  ? "Hidden\n" :
		  ((aac->jbod && (sdev->type == TYPE_DISK)) ? "JBOD\n" : ""));
	return snprintf(buf, PAGE_SIZE, "%s\n",
	  get_container_type(aac->fsa_dev[sdev_id(sdev)].type));
}

static struct device_attribute aac_raid_level_attr = {
	.attr = {
		.name = "level",
		.mode = S_IRUGO,
	},
	.show = aac_show_raid_level
};

static ssize_t aac_show_unique_id(struct device *dev,
	     struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct aac_dev *aac = (struct aac_dev *)(sdev->host->hostdata);
	unsigned char sn[16];

	memset(sn, 0, sizeof(sn));

	if (sdev_channel(sdev) == CONTAINER_CHANNEL)
		memcpy(sn, aac->fsa_dev[sdev_id(sdev)].identifier, sizeof(sn));

	return snprintf(buf, 16 * 2 + 2,
		"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		sn[0], sn[1], sn[2], sn[3],
		sn[4], sn[5], sn[6], sn[7],
		sn[8], sn[9], sn[10], sn[11],
		sn[12], sn[13], sn[14], sn[15]);
}

static struct device_attribute aac_unique_id_attr = {
	.attr = {
		.name = "unique_id",
		.mode = 0444,
	},
	.show = aac_show_unique_id
};



static struct device_attribute *aac_dev_attrs[] = {
	&aac_raid_level_attr,
	&aac_unique_id_attr,
	NULL,
};

static int aac_ioctl(struct scsi_device *sdev, unsigned int cmd,
		     void __user *arg)
{
	struct aac_dev *dev = (struct aac_dev *)sdev->host->hostdata;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	return aac_do_ioctl(dev, cmd, arg);
}

static int get_num_of_incomplete_fibs(struct aac_dev *aac)
{

	unsigned long flags;
	struct scsi_device *sdev = NULL;
	struct Scsi_Host *shost = aac->scsi_host_ptr;
	struct scsi_cmnd *scmnd = NULL;
	struct device *ctrl_dev;

	int mlcnt  = 0;
	int llcnt  = 0;
	int ehcnt  = 0;
	int fwcnt  = 0;
	int krlcnt = 0;

	__shost_for_each_device(sdev, shost) {
		spin_lock_irqsave(&sdev->list_lock, flags);
		list_for_each_entry(scmnd, &sdev->cmd_list, list) {
			switch (scmnd->SCp.phase) {
			case AAC_OWNER_FIRMWARE:
				fwcnt++;
				break;
			case AAC_OWNER_ERROR_HANDLER:
				ehcnt++;
				break;
			case AAC_OWNER_LOWLEVEL:
				llcnt++;
				break;
			case AAC_OWNER_MIDLEVEL:
				mlcnt++;
				break;
			default:
				krlcnt++;
				break;
			}
		}
		spin_unlock_irqrestore(&sdev->list_lock, flags);
	}

	ctrl_dev = &aac->pdev->dev;

	dev_info(ctrl_dev, "outstanding cmd: midlevel-%d\n", mlcnt);
	dev_info(ctrl_dev, "outstanding cmd: lowlevel-%d\n", llcnt);
	dev_info(ctrl_dev, "outstanding cmd: error handler-%d\n", ehcnt);
	dev_info(ctrl_dev, "outstanding cmd: firmware-%d\n", fwcnt);
	dev_info(ctrl_dev, "outstanding cmd: kernel-%d\n", krlcnt);

	return mlcnt + llcnt + ehcnt + fwcnt;
}

static int aac_eh_abort(struct scsi_cmnd* cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct aac_dev * aac = (struct aac_dev *)host->hostdata;
	int count, found;
	u32 bus, cid;
	int ret = FAILED;

	if (aac_adapter_check_health(aac))
		return ret;

	bus = aac_logical_to_phys(scmd_channel(cmd));
	cid = scmd_id(cmd);
	if (aac->hba_map[bus][cid].devtype == AAC_DEVTYPE_NATIVE_RAW) {
		struct fib *fib;
		struct aac_hba_tm_req *tmf;
		int status;
		u64 address;

		pr_err("%s: Host adapter abort request (%d,%d,%d,%d)\n",
		 AAC_DRIVERNAME,
		 host->host_no, sdev_channel(dev), sdev_id(dev), (int)dev->lun);

		found = 0;
		for (count = 0; count < (host->can_queue + AAC_NUM_MGT_FIB); ++count) {
			fib = &aac->fibs[count];
			if (*(u8 *)fib->hw_fib_va != 0 &&
				(fib->flags & FIB_CONTEXT_FLAG_NATIVE_HBA) &&
				(fib->callback_data == cmd)) {
				found = 1;
				break;
			}
		}
		if (!found)
			return ret;

		/* start a HBA_TMF_ABORT_TASK TMF request */
		fib = aac_fib_alloc(aac);
		if (!fib)
			return ret;

		tmf = (struct aac_hba_tm_req *)fib->hw_fib_va;
		memset(tmf, 0, sizeof(*tmf));
		tmf->tmf = HBA_TMF_ABORT_TASK;
		tmf->it_nexus = aac->hba_map[bus][cid].rmw_nexus;
		tmf->lun[1] = cmd->device->lun;

		address = (u64)fib->hw_error_pa;
		tmf->error_ptr_hi = cpu_to_le32((u32)(address >> 32));
		tmf->error_ptr_lo = cpu_to_le32((u32)(address & 0xffffffff));
		tmf->error_length = cpu_to_le32(FW_ERROR_BUFFER_SIZE);

		fib->hbacmd_size = sizeof(*tmf);
		cmd->SCp.sent_command = 0;

		status = aac_hba_send(HBA_IU_TYPE_SCSI_TM_REQ, fib,
				  (fib_callback) aac_hba_callback,
				  (void *) cmd);

		/* Wait up to 15 secs for completion */
		for (count = 0; count < 15; ++count) {
			if (cmd->SCp.sent_command) {
				ret = SUCCESS;
				break;
			}
			msleep(1000);
		}

		if (ret != SUCCESS)
			pr_err("%s: Host adapter abort request timed out\n",
			AAC_DRIVERNAME);
	} else {
		pr_err(
			"%s: Host adapter abort request.\n"
			"%s: Outstanding commands on (%d,%d,%d,%d):\n",
			AAC_DRIVERNAME, AAC_DRIVERNAME,
			host->host_no, sdev_channel(dev), sdev_id(dev),
			(int)dev->lun);
		switch (cmd->cmnd[0]) {
		case SERVICE_ACTION_IN_16:
			if (!(aac->raw_io_interface) ||
			    !(aac->raw_io_64) ||
			    ((cmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16))
				break;
			/* fall through */
		case INQUIRY:
		case READ_CAPACITY:
			/*
			 * Mark associated FIB to not complete,
			 * eh handler does this
			 */
			for (count = 0;
				count < (host->can_queue + AAC_NUM_MGT_FIB);
				++count) {
				struct fib *fib = &aac->fibs[count];

				if (fib->hw_fib_va->header.XferState &&
				(fib->flags & FIB_CONTEXT_FLAG) &&
				(fib->callback_data == cmd)) {
					fib->flags |=
						FIB_CONTEXT_FLAG_TIMED_OUT;
					cmd->SCp.phase =
						AAC_OWNER_ERROR_HANDLER;
					ret = SUCCESS;
				}
			}
			break;
		case TEST_UNIT_READY:
			/*
			 * Mark associated FIB to not complete,
			 * eh handler does this
			 */
			for (count = 0;
				count < (host->can_queue + AAC_NUM_MGT_FIB);
				++count) {
				struct scsi_cmnd *command;
				struct fib *fib = &aac->fibs[count];

				command = fib->callback_data;

				if ((fib->hw_fib_va->header.XferState &
					cpu_to_le32
					(Async | NoResponseExpected)) &&
					(fib->flags & FIB_CONTEXT_FLAG) &&
					((command)) &&
					(command->device == cmd->device)) {
					fib->flags |=
						FIB_CONTEXT_FLAG_TIMED_OUT;
					command->SCp.phase =
						AAC_OWNER_ERROR_HANDLER;
					if (command == cmd)
						ret = SUCCESS;
				}
			}
			break;
		}
	}
	return ret;
}

static u8 aac_eh_tmf_lun_reset_fib(struct aac_hba_map_info *info,
				   struct fib *fib, u64 tmf_lun)
{
	struct aac_hba_tm_req *tmf;
	u64 address;

	/* start a HBA_TMF_LUN_RESET TMF request */
	tmf = (struct aac_hba_tm_req *)fib->hw_fib_va;
	memset(tmf, 0, sizeof(*tmf));
	tmf->tmf = HBA_TMF_LUN_RESET;
	tmf->it_nexus = info->rmw_nexus;
	int_to_scsilun(tmf_lun, (struct scsi_lun *)tmf->lun);

	address = (u64)fib->hw_error_pa;
	tmf->error_ptr_hi = cpu_to_le32
		((u32)(address >> 32));
	tmf->error_ptr_lo = cpu_to_le32
		((u32)(address & 0xffffffff));
	tmf->error_length = cpu_to_le32(FW_ERROR_BUFFER_SIZE);
	fib->hbacmd_size = sizeof(*tmf);

	return HBA_IU_TYPE_SCSI_TM_REQ;
}

static u8 aac_eh_tmf_hard_reset_fib(struct aac_hba_map_info *info,
				    struct fib *fib)
{
	struct aac_hba_reset_req *rst;
	u64 address;

	/* already tried, start a hard reset now */
	rst = (struct aac_hba_reset_req *)fib->hw_fib_va;
	memset(rst, 0, sizeof(*rst));
	rst->it_nexus = info->rmw_nexus;

	address = (u64)fib->hw_error_pa;
	rst->error_ptr_hi = cpu_to_le32((u32)(address >> 32));
	rst->error_ptr_lo = cpu_to_le32((u32)(address & 0xffffffff));
	rst->error_length = cpu_to_le32(FW_ERROR_BUFFER_SIZE);
	fib->hbacmd_size = sizeof(*rst);

       return HBA_IU_TYPE_SATA_REQ;
}

void aac_tmf_callback(void *context, struct fib *fibptr)
{
	struct aac_hba_resp *err =
		&((struct aac_native_hba *)fibptr->hw_fib_va)->resp.err;
	struct aac_hba_map_info *info = context;
	int res;

	switch (err->service_response) {
	case HBA_RESP_SVCRES_TMF_REJECTED:
		res = -1;
		break;
	case HBA_RESP_SVCRES_TMF_LUN_INVALID:
		res = 0;
		break;
	case HBA_RESP_SVCRES_TMF_COMPLETE:
	case HBA_RESP_SVCRES_TMF_SUCCEEDED:
		res = 0;
		break;
	default:
		res = -2;
		break;
	}
	aac_fib_complete(fibptr);

	info->reset_state = res;
}

/*
 *	aac_eh_dev_reset	- Device reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 */
static int aac_eh_dev_reset(struct scsi_cmnd *cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct aac_dev * aac = (struct aac_dev *)host->hostdata;
	struct aac_hba_map_info *info;
	int count;
	u32 bus, cid;
	struct fib *fib;
	int ret = FAILED;
	int status;
	u8 command;

	bus = aac_logical_to_phys(scmd_channel(cmd));
	cid = scmd_id(cmd);

	if (bus >= AAC_MAX_BUSES || cid >= AAC_MAX_TARGETS)
		return FAILED;

	info = &aac->hba_map[bus][cid];

	if (info->devtype != AAC_DEVTYPE_NATIVE_RAW &&
	    info->reset_state > 0)
		return FAILED;

	pr_err("%s: Host adapter reset request. SCSI hang ?\n",
	       AAC_DRIVERNAME);

	fib = aac_fib_alloc(aac);
	if (!fib)
		return ret;

	/* start a HBA_TMF_LUN_RESET TMF request */
	command = aac_eh_tmf_lun_reset_fib(info, fib, dev->lun);

	info->reset_state = 1;

	status = aac_hba_send(command, fib,
			      (fib_callback) aac_tmf_callback,
			      (void *) info);

	/* Wait up to 15 seconds for completion */
	for (count = 0; count < 15; ++count) {
		if (info->reset_state == 0) {
			ret = info->reset_state == 0 ? SUCCESS : FAILED;
			break;
		}
		msleep(1000);
	}

	return ret;
}

/*
 *	aac_eh_target_reset	- Target reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 */
static int aac_eh_target_reset(struct scsi_cmnd *cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct aac_dev * aac = (struct aac_dev *)host->hostdata;
	struct aac_hba_map_info *info;
	int count;
	u32 bus, cid;
	int ret = FAILED;
	struct fib *fib;
	int status;
	u8 command;

	bus = aac_logical_to_phys(scmd_channel(cmd));
	cid = scmd_id(cmd);

	if (bus >= AAC_MAX_BUSES || cid >= AAC_MAX_TARGETS)
		return FAILED;

	info = &aac->hba_map[bus][cid];

	if (info->devtype != AAC_DEVTYPE_NATIVE_RAW &&
	    info->reset_state > 0)
		return FAILED;

	pr_err("%s: Host adapter reset request. SCSI hang ?\n",
	       AAC_DRIVERNAME);

	fib = aac_fib_alloc(aac);
	if (!fib)
		return ret;


	/* already tried, start a hard reset now */
	command = aac_eh_tmf_hard_reset_fib(info, fib);

	info->reset_state = 2;

	status = aac_hba_send(command, fib,
			      (fib_callback) aac_tmf_callback,
			      (void *) info);

	/* Wait up to 15 seconds for completion */
	for (count = 0; count < 15; ++count) {
		if (info->reset_state <= 0) {
			ret = info->reset_state == 0 ? SUCCESS : FAILED;
			break;
		}
		msleep(1000);
	}

	return ret;
}

/*
 *	aac_eh_bus_reset	- Bus reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 */
static int aac_eh_bus_reset(struct scsi_cmnd* cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct aac_dev * aac = (struct aac_dev *)host->hostdata;
	int count;
	u32 cmd_bus;
	int status = 0;


	cmd_bus = aac_logical_to_phys(scmd_channel(cmd));
	/* Mark the assoc. FIB to not complete, eh handler does this */
	for (count = 0; count < (host->can_queue + AAC_NUM_MGT_FIB); ++count) {
		struct fib *fib = &aac->fibs[count];

		if (fib->hw_fib_va->header.XferState &&
		    (fib->flags & FIB_CONTEXT_FLAG) &&
		    (fib->flags & FIB_CONTEXT_FLAG_SCSI_CMD)) {
			struct aac_hba_map_info *info;
			u32 bus, cid;

			cmd = (struct scsi_cmnd *)fib->callback_data;
			bus = aac_logical_to_phys(scmd_channel(cmd));
			if (bus != cmd_bus)
				continue;
			cid = scmd_id(cmd);
			info = &aac->hba_map[bus][cid];
			if (bus >= AAC_MAX_BUSES || cid >= AAC_MAX_TARGETS ||
			    info->devtype != AAC_DEVTYPE_NATIVE_RAW) {
				fib->flags |= FIB_CONTEXT_FLAG_EH_RESET;
				cmd->SCp.phase = AAC_OWNER_ERROR_HANDLER;
			}
		}
	}

	pr_err("%s: Host adapter reset request. SCSI hang ?\n", AAC_DRIVERNAME);

	/*
	 * Check the health of the controller
	 */
	status = aac_adapter_check_health(aac);
	if (status)
		dev_err(&aac->pdev->dev, "Adapter health - %d\n", status);

	count = get_num_of_incomplete_fibs(aac);
	return (count == 0) ? SUCCESS : FAILED;
}

/*
 *	aac_eh_host_reset	- Host reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 */
int aac_eh_host_reset(struct scsi_cmnd *cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct aac_dev * aac = (struct aac_dev *)host->hostdata;
	int ret = FAILED;
	__le32 supported_options2 = 0;
	bool is_mu_reset;
	bool is_ignore_reset;
	bool is_doorbell_reset;

	/*
	 * Check if reset is supported by the firmware
	 */
	supported_options2 = aac->supplement_adapter_info.supported_options2;
	is_mu_reset = supported_options2 & AAC_OPTION_MU_RESET;
	is_doorbell_reset = supported_options2 & AAC_OPTION_DOORBELL_RESET;
	is_ignore_reset = supported_options2 & AAC_OPTION_IGNORE_RESET;
	/*
	 * This adapter needs a blind reset, only do so for
	 * Adapters that support a register, instead of a commanded,
	 * reset.
	 */
	if ((is_mu_reset || is_doorbell_reset)
	 && aac_check_reset
	 && (aac_check_reset != -1 || !is_ignore_reset)) {
		/* Bypass wait for command quiesce */
		if (aac_reset_adapter(aac, 2, IOP_HWSOFT_RESET) == 0)
			ret = SUCCESS;
	}
	/*
	 * Reset EH state
	 */
	if (ret == SUCCESS) {
		int bus, cid;
		struct aac_hba_map_info *info;

		for (bus = 0; bus < AAC_MAX_BUSES; bus++) {
			for (cid = 0; cid < AAC_MAX_TARGETS; cid++) {
				info = &aac->hba_map[bus][cid];
				if (info->devtype == AAC_DEVTYPE_NATIVE_RAW)
					info->reset_state = 0;
			}
		}
	}
	return ret;
}

/**
 *	aac_cfg_open		-	open a configuration file
 *	@inode: inode being opened
 *	@file: file handle attached
 *
 *	Called when the configuration device is opened. Does the needed
 *	set up on the handle and then returns
 *
 *	Bugs: This needs extending to check a given adapter is present
 *	so we can support hot plugging, and to ref count adapters.
 */

static int aac_cfg_open(struct inode *inode, struct file *file)
{
	struct aac_dev *aac;
	unsigned minor_number = iminor(inode);
	int err = -ENODEV;

	mutex_lock(&aac_mutex);  /* BKL pushdown: nothing else protects this list */
	list_for_each_entry(aac, &aac_devices, entry) {
		if (aac->id == minor_number) {
			file->private_data = aac;
			err = 0;
			break;
		}
	}
	mutex_unlock(&aac_mutex);

	return err;
}

/**
 *	aac_cfg_ioctl		-	AAC configuration request
 *	@inode: inode of device
 *	@file: file handle
 *	@cmd: ioctl command code
 *	@arg: argument
 *
 *	Handles a configuration ioctl. Currently this involves wrapping it
 *	up and feeding it into the nasty windowsalike glue layer.
 *
 *	Bugs: Needs locking against parallel ioctls lower down
 *	Bugs: Needs to handle hot plugging
 */

static long aac_cfg_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct aac_dev *aac = (struct aac_dev *)file->private_data;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	return aac_do_ioctl(aac, cmd, (void __user *)arg);
}

#ifdef CONFIG_COMPAT
static long aac_compat_do_ioctl(struct aac_dev *dev, unsigned cmd, unsigned long arg)
{
	long ret;
	switch (cmd) {
	case FSACTL_MINIPORT_REV_CHECK:
	case FSACTL_SENDFIB:
	case FSACTL_OPEN_GET_ADAPTER_FIB:
	case FSACTL_CLOSE_GET_ADAPTER_FIB:
	case FSACTL_SEND_RAW_SRB:
	case FSACTL_GET_PCI_INFO:
	case FSACTL_QUERY_DISK:
	case FSACTL_DELETE_DISK:
	case FSACTL_FORCE_DELETE_DISK:
	case FSACTL_GET_CONTAINERS:
	case FSACTL_SEND_LARGE_FIB:
		ret = aac_do_ioctl(dev, cmd, (void __user *)arg);
		break;

	case FSACTL_GET_NEXT_ADAPTER_FIB: {
		struct fib_ioctl __user *f;

		f = compat_alloc_user_space(sizeof(*f));
		ret = 0;
		if (clear_user(f, sizeof(*f)))
			ret = -EFAULT;
		if (copy_in_user(f, (void __user *)arg, sizeof(struct fib_ioctl) - sizeof(u32)))
			ret = -EFAULT;
		if (!ret)
			ret = aac_do_ioctl(dev, cmd, f);
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static int aac_compat_ioctl(struct scsi_device *sdev, unsigned int cmd,
			    void __user *arg)
{
	struct aac_dev *dev = (struct aac_dev *)sdev->host->hostdata;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	return aac_compat_do_ioctl(dev, cmd, (unsigned long)arg);
}

static long aac_compat_cfg_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	return aac_compat_do_ioctl(file->private_data, cmd, arg);
}
#endif

static ssize_t aac_show_model(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	int len;

	if (dev->supplement_adapter_info.adapter_type_text[0]) {
		char *cp = dev->supplement_adapter_info.adapter_type_text;
		while (*cp && *cp != ' ')
			++cp;
		while (*cp == ' ')
			++cp;
		len = snprintf(buf, PAGE_SIZE, "%s\n", cp);
	} else
		len = snprintf(buf, PAGE_SIZE, "%s\n",
		  aac_drivers[dev->cardtype].model);
	return len;
}

static ssize_t aac_show_vendor(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	struct aac_supplement_adapter_info *sup_adap_info;
	int len;

	sup_adap_info = &dev->supplement_adapter_info;
	if (sup_adap_info->adapter_type_text[0]) {
		char *cp = sup_adap_info->adapter_type_text;
		while (*cp && *cp != ' ')
			++cp;
		len = snprintf(buf, PAGE_SIZE, "%.*s\n",
			(int)(cp - (char *)sup_adap_info->adapter_type_text),
					sup_adap_info->adapter_type_text);
	} else
		len = snprintf(buf, PAGE_SIZE, "%s\n",
			aac_drivers[dev->cardtype].vname);
	return len;
}

static ssize_t aac_show_flags(struct device *cdev,
			      struct device_attribute *attr, char *buf)
{
	int len = 0;
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(cdev)->hostdata;

	if (nblank(dprintk(x)))
		len = snprintf(buf, PAGE_SIZE, "dprintk\n");
#ifdef AAC_DETAILED_STATUS_INFO
	len += snprintf(buf + len, PAGE_SIZE - len,
			"AAC_DETAILED_STATUS_INFO\n");
#endif
	if (dev->raw_io_interface && dev->raw_io_64)
		len += snprintf(buf + len, PAGE_SIZE - len,
				"SAI_READ_CAPACITY_16\n");
	if (dev->jbod)
		len += snprintf(buf + len, PAGE_SIZE - len, "SUPPORTED_JBOD\n");
	if (dev->supplement_adapter_info.supported_options2 &
		AAC_OPTION_POWER_MANAGEMENT)
		len += snprintf(buf + len, PAGE_SIZE - len,
				"SUPPORTED_POWER_MANAGEMENT\n");
	if (dev->msi)
		len += snprintf(buf + len, PAGE_SIZE - len, "PCI_HAS_MSI\n");
	return len;
}

static ssize_t aac_show_kernel_version(struct device *device,
				       struct device_attribute *attr,
				       char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	int len, tmp;

	tmp = le32_to_cpu(dev->adapter_info.kernelrev);
	len = snprintf(buf, PAGE_SIZE, "%d.%d-%d[%d]\n",
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  le32_to_cpu(dev->adapter_info.kernelbuild));
	return len;
}

static ssize_t aac_show_monitor_version(struct device *device,
					struct device_attribute *attr,
					char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	int len, tmp;

	tmp = le32_to_cpu(dev->adapter_info.monitorrev);
	len = snprintf(buf, PAGE_SIZE, "%d.%d-%d[%d]\n",
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  le32_to_cpu(dev->adapter_info.monitorbuild));
	return len;
}

static ssize_t aac_show_bios_version(struct device *device,
				     struct device_attribute *attr,
				     char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	int len, tmp;

	tmp = le32_to_cpu(dev->adapter_info.biosrev);
	len = snprintf(buf, PAGE_SIZE, "%d.%d-%d[%d]\n",
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  le32_to_cpu(dev->adapter_info.biosbuild));
	return len;
}

static ssize_t aac_show_driver_version(struct device *device,
					struct device_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", aac_driver_version);
}

static ssize_t aac_show_serial_number(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	int len = 0;

	if (le32_to_cpu(dev->adapter_info.serial[0]) != 0xBAD0)
		len = snprintf(buf, 16, "%06X\n",
		  le32_to_cpu(dev->adapter_info.serial[0]));
	if (len &&
	  !memcmp(&dev->supplement_adapter_info.mfg_pcba_serial_no[
	    sizeof(dev->supplement_adapter_info.mfg_pcba_serial_no)-len],
	  buf, len-1))
		len = snprintf(buf, 16, "%.*s\n",
		  (int)sizeof(dev->supplement_adapter_info.mfg_pcba_serial_no),
		  dev->supplement_adapter_info.mfg_pcba_serial_no);

	return min(len, 16);
}

static ssize_t aac_show_max_channel(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
	  class_to_shost(device)->max_channel);
}

static ssize_t aac_show_max_id(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
	  class_to_shost(device)->max_id);
}

static ssize_t aac_store_reset_adapter(struct device *device,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int retval = -EACCES;

	if (!capable(CAP_SYS_ADMIN))
		return retval;

	retval = aac_reset_adapter(shost_priv(class_to_shost(device)),
					buf[0] == '!', IOP_HWSOFT_RESET);
	if (retval >= 0)
		retval = count;

	return retval;
}

static ssize_t aac_show_reset_adapter(struct device *device,
				      struct device_attribute *attr,
				      char *buf)
{
	struct aac_dev *dev = (struct aac_dev*)class_to_shost(device)->hostdata;
	int len, tmp;

	tmp = aac_adapter_check_health(dev);
	if ((tmp == 0) && dev->in_reset)
		tmp = -EBUSY;
	len = snprintf(buf, PAGE_SIZE, "0x%x\n", tmp);
	return len;
}

static struct device_attribute aac_model = {
	.attr = {
		.name = "model",
		.mode = S_IRUGO,
	},
	.show = aac_show_model,
};
static struct device_attribute aac_vendor = {
	.attr = {
		.name = "vendor",
		.mode = S_IRUGO,
	},
	.show = aac_show_vendor,
};
static struct device_attribute aac_flags = {
	.attr = {
		.name = "flags",
		.mode = S_IRUGO,
	},
	.show = aac_show_flags,
};
static struct device_attribute aac_kernel_version = {
	.attr = {
		.name = "hba_kernel_version",
		.mode = S_IRUGO,
	},
	.show = aac_show_kernel_version,
};
static struct device_attribute aac_monitor_version = {
	.attr = {
		.name = "hba_monitor_version",
		.mode = S_IRUGO,
	},
	.show = aac_show_monitor_version,
};
static struct device_attribute aac_bios_version = {
	.attr = {
		.name = "hba_bios_version",
		.mode = S_IRUGO,
	},
	.show = aac_show_bios_version,
};
static struct device_attribute aac_lld_version = {
	.attr = {
		.name = "driver_version",
		.mode = 0444,
	},
	.show = aac_show_driver_version,
};
static struct device_attribute aac_serial_number = {
	.attr = {
		.name = "serial_number",
		.mode = S_IRUGO,
	},
	.show = aac_show_serial_number,
};
static struct device_attribute aac_max_channel = {
	.attr = {
		.name = "max_channel",
		.mode = S_IRUGO,
	},
	.show = aac_show_max_channel,
};
static struct device_attribute aac_max_id = {
	.attr = {
		.name = "max_id",
		.mode = S_IRUGO,
	},
	.show = aac_show_max_id,
};
static struct device_attribute aac_reset = {
	.attr = {
		.name = "reset_host",
		.mode = S_IWUSR|S_IRUGO,
	},
	.store = aac_store_reset_adapter,
	.show = aac_show_reset_adapter,
};

static struct device_attribute *aac_attrs[] = {
	&aac_model,
	&aac_vendor,
	&aac_flags,
	&aac_kernel_version,
	&aac_monitor_version,
	&aac_bios_version,
	&aac_lld_version,
	&aac_serial_number,
	&aac_max_channel,
	&aac_max_id,
	&aac_reset,
	NULL
};

ssize_t aac_get_serial_number(struct device *device, char *buf)
{
	return aac_show_serial_number(device, &aac_serial_number, buf);
}

static const struct file_operations aac_cfg_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= aac_cfg_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = aac_compat_cfg_ioctl,
#endif
	.open		= aac_cfg_open,
	.llseek		= noop_llseek,
};

static struct scsi_host_template aac_driver_template = {
	.module				= THIS_MODULE,
	.name				= "AAC",
	.proc_name			= AAC_DRIVERNAME,
	.info				= aac_info,
	.ioctl				= aac_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl			= aac_compat_ioctl,
#endif
	.queuecommand			= aac_queuecommand,
	.bios_param			= aac_biosparm,
	.shost_attrs			= aac_attrs,
	.slave_configure		= aac_slave_configure,
	.change_queue_depth		= aac_change_queue_depth,
	.sdev_attrs			= aac_dev_attrs,
	.eh_abort_handler		= aac_eh_abort,
	.eh_device_reset_handler	= aac_eh_dev_reset,
	.eh_target_reset_handler	= aac_eh_target_reset,
	.eh_bus_reset_handler		= aac_eh_bus_reset,
	.eh_host_reset_handler		= aac_eh_host_reset,
	.can_queue			= AAC_NUM_IO_FIB,
	.this_id			= MAXIMUM_NUM_CONTAINERS,
	.sg_tablesize			= 16,
	.max_sectors			= 128,
#if (AAC_NUM_IO_FIB > 256)
	.cmd_per_lun			= 256,
#else
	.cmd_per_lun			= AAC_NUM_IO_FIB,
#endif
	.emulated			= 1,
	.no_write_same			= 1,
};

static void __aac_shutdown(struct aac_dev * aac)
{
	int i;

	mutex_lock(&aac->ioctl_mutex);
	aac->adapter_shutdown = 1;
	mutex_unlock(&aac->ioctl_mutex);

	if (aac->aif_thread) {
		int i;
		/* Clear out events first */
		for (i = 0; i < (aac->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB); i++) {
			struct fib *fib = &aac->fibs[i];
			if (!(fib->hw_fib_va->header.XferState & cpu_to_le32(NoResponseExpected | Async)) &&
			    (fib->hw_fib_va->header.XferState & cpu_to_le32(ResponseExpected)))
				complete(&fib->event_wait);
		}
		kthread_stop(aac->thread);
		aac->thread = NULL;
	}

	aac_send_shutdown(aac);

	aac_adapter_disable_int(aac);

	if (aac_is_src(aac)) {
		if (aac->max_msix > 1) {
			for (i = 0; i < aac->max_msix; i++) {
				free_irq(pci_irq_vector(aac->pdev, i),
					 &(aac->aac_msix[i]));
			}
		} else {
			free_irq(aac->pdev->irq,
				 &(aac->aac_msix[0]));
		}
	} else {
		free_irq(aac->pdev->irq, aac);
	}
	if (aac->msi)
		pci_disable_msi(aac->pdev);
	else if (aac->max_msix > 1)
		pci_disable_msix(aac->pdev);
}
static void aac_init_char(void)
{
	aac_cfg_major = register_chrdev(0, "aac", &aac_cfg_fops);
	if (aac_cfg_major < 0) {
		pr_err("aacraid: unable to register \"aac\" device.\n");
	}
}

static int aac_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	unsigned index = id->driver_data;
	struct Scsi_Host *shost;
	struct aac_dev *aac;
	struct list_head *insert = &aac_devices;
	int error = -ENODEV;
	int unique_id = 0;
	u64 dmamask;
	int mask_bits = 0;
	extern int aac_sync_mode;

	/*
	 * Only series 7 needs freset.
	 */
	if (pdev->device == PMC_DEVICE_S7)
		pdev->needs_freset = 1;

	list_for_each_entry(aac, &aac_devices, entry) {
		if (aac->id > unique_id)
			break;
		insert = &aac->entry;
		unique_id++;
	}

	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
			       PCIE_LINK_STATE_CLKPM);

	error = pci_enable_device(pdev);
	if (error)
		goto out;
	error = -ENODEV;

	if (!(aac_drivers[index].quirks & AAC_QUIRK_SRC)) {
		error = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (error) {
			dev_err(&pdev->dev, "PCI 32 BIT dma mask set failed");
			goto out_disable_pdev;
		}
	}

	/*
	 * If the quirk31 bit is set, the adapter needs adapter
	 * to driver communication memory to be allocated below 2gig
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_31BIT) {
		dmamask = DMA_BIT_MASK(31);
		mask_bits = 31;
	} else {
		dmamask = DMA_BIT_MASK(32);
		mask_bits = 32;
	}

	error = pci_set_consistent_dma_mask(pdev, dmamask);
	if (error) {
		dev_err(&pdev->dev, "PCI %d B consistent dma mask set failed\n"
				, mask_bits);
		goto out_disable_pdev;
	}

	pci_set_master(pdev);

	shost = scsi_host_alloc(&aac_driver_template, sizeof(struct aac_dev));
	if (!shost)
		goto out_disable_pdev;

	shost->irq = pdev->irq;
	shost->unique_id = unique_id;
	shost->max_cmd_len = 16;
	shost->use_cmd_list = 1;

	if (aac_cfg_major == AAC_CHARDEV_NEEDS_REINIT)
		aac_init_char();

	aac = (struct aac_dev *)shost->hostdata;
	aac->base_start = pci_resource_start(pdev, 0);
	aac->scsi_host_ptr = shost;
	aac->pdev = pdev;
	aac->name = aac_driver_template.name;
	aac->id = shost->unique_id;
	aac->cardtype = index;
	INIT_LIST_HEAD(&aac->entry);

	if (aac_reset_devices || reset_devices)
		aac->init_reset = true;

	aac->fibs = kcalloc(shost->can_queue + AAC_NUM_MGT_FIB,
			    sizeof(struct fib),
			    GFP_KERNEL);
	if (!aac->fibs)
		goto out_free_host;
	spin_lock_init(&aac->fib_lock);

	mutex_init(&aac->ioctl_mutex);
	mutex_init(&aac->scan_mutex);

	INIT_DELAYED_WORK(&aac->safw_rescan_work, aac_safw_rescan_worker);
	/*
	 *	Map in the registers from the adapter.
	 */
	aac->base_size = AAC_MIN_FOOTPRINT_SIZE;
	if ((*aac_drivers[index].init)(aac)) {
		error = -ENODEV;
		goto out_unmap;
	}

	if (aac->sync_mode) {
		if (aac_sync_mode)
			printk(KERN_INFO "%s%d: Sync. mode enforced "
				"by driver parameter. This will cause "
				"a significant performance decrease!\n",
				aac->name,
				aac->id);
		else
			printk(KERN_INFO "%s%d: Async. mode not supported "
				"by current driver, sync. mode enforced."
				"\nPlease update driver to get full performance.\n",
				aac->name,
				aac->id);
	}

	/*
	 *	Start any kernel threads needed
	 */
	aac->thread = kthread_run(aac_command_thread, aac, AAC_DRIVERNAME);
	if (IS_ERR(aac->thread)) {
		printk(KERN_ERR "aacraid: Unable to create command thread.\n");
		error = PTR_ERR(aac->thread);
		aac->thread = NULL;
		goto out_deinit;
	}

	aac->maximum_num_channels = aac_drivers[index].channels;
	error = aac_get_adapter_info(aac);
	if (error < 0)
		goto out_deinit;

	/*
	 * Lets override negotiations and drop the maximum SG limit to 34
	 */
	if ((aac_drivers[index].quirks & AAC_QUIRK_34SG) &&
			(shost->sg_tablesize > 34)) {
		shost->sg_tablesize = 34;
		shost->max_sectors = (shost->sg_tablesize * 8) + 112;
	}

	if ((aac_drivers[index].quirks & AAC_QUIRK_17SG) &&
			(shost->sg_tablesize > 17)) {
		shost->sg_tablesize = 17;
		shost->max_sectors = (shost->sg_tablesize * 8) + 112;
	}

	if (aac->adapter_info.options & AAC_OPT_NEW_COMM)
		shost->max_segment_size = shost->max_sectors << 9;
	else
		shost->max_segment_size = 65536;

	/*
	 * Firmware printf works only with older firmware.
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_34SG)
		aac->printf_enabled = 1;
	else
		aac->printf_enabled = 0;

	/*
	 * max channel will be the physical channels plus 1 virtual channel
	 * all containers are on the virtual channel 0 (CONTAINER_CHANNEL)
	 * physical channels are address by their actual physical number+1
	 */
	if (aac->nondasd_support || expose_physicals || aac->jbod)
		shost->max_channel = aac->maximum_num_channels;
	else
		shost->max_channel = 0;

	aac_get_config_status(aac, 0);
	aac_get_containers(aac);
	list_add(&aac->entry, insert);

	shost->max_id = aac->maximum_num_containers;
	if (shost->max_id < aac->maximum_num_physicals)
		shost->max_id = aac->maximum_num_physicals;
	if (shost->max_id < MAXIMUM_NUM_CONTAINERS)
		shost->max_id = MAXIMUM_NUM_CONTAINERS;
	else
		shost->this_id = shost->max_id;

	if (!aac->sa_firmware && aac_drivers[index].quirks & AAC_QUIRK_SRC)
		aac_intr_normal(aac, 0, 2, 0, NULL);

	/*
	 * dmb - we may need to move the setting of these parms somewhere else once
	 * we get a fib that can report the actual numbers
	 */
	shost->max_lun = AAC_MAX_LUN;

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_deinit;

	aac_scan_host(aac);

	pci_enable_pcie_error_reporting(pdev);
	pci_save_state(pdev);

	return 0;

 out_deinit:
	__aac_shutdown(aac);
 out_unmap:
	aac_fib_map_free(aac);
	if (aac->comm_addr)
		dma_free_coherent(&aac->pdev->dev, aac->comm_size,
				  aac->comm_addr, aac->comm_phys);
	kfree(aac->queues);
	aac_adapter_ioremap(aac, 0);
	kfree(aac->fibs);
	kfree(aac->fsa_dev);
 out_free_host:
	scsi_host_put(shost);
 out_disable_pdev:
	pci_disable_device(pdev);
 out:
	return error;
}

static void aac_release_resources(struct aac_dev *aac)
{
	aac_adapter_disable_int(aac);
	aac_free_irq(aac);
}

static int aac_acquire_resources(struct aac_dev *dev)
{
	unsigned long status;
	/*
	 *	First clear out all interrupts.  Then enable the one's that we
	 *	can handle.
	 */
	while (!((status = src_readl(dev, MUnit.OMR)) & KERNEL_UP_AND_RUNNING)
		|| status == 0xffffffff)
			msleep(20);

	aac_adapter_disable_int(dev);
	aac_adapter_enable_int(dev);


	if (aac_is_src(dev))
		aac_define_int_mode(dev);

	if (dev->msi_enabled)
		aac_src_access_devreg(dev, AAC_ENABLE_MSIX);

	if (aac_acquire_irq(dev))
		goto error_iounmap;

	aac_adapter_enable_int(dev);

	/*max msix may change  after EEH
	 * Re-assign vectors to fibs
	 */
	aac_fib_vector_assign(dev);

	if (!dev->sync_mode) {
		/* After EEH recovery or suspend resume, max_msix count
		 * may change, therefore updating in init as well.
		 */
		dev->init->r7.no_of_msix_vectors = cpu_to_le32(dev->max_msix);
		aac_adapter_start(dev);
	}
	return 0;

error_iounmap:
	return -1;

}

#if (defined(CONFIG_PM))
static int aac_suspend(struct pci_dev *pdev, pm_message_t state)
{

	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = (struct aac_dev *)shost->hostdata;

	scsi_block_requests(shost);
	aac_cancel_safw_rescan_worker(aac);
	aac_send_shutdown(aac);

	aac_release_resources(aac);

	pci_set_drvdata(pdev, shost);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int aac_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = (struct aac_dev *)shost->hostdata;
	int r;

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	r = pci_enable_device(pdev);

	if (r)
		goto fail_device;

	pci_set_master(pdev);
	if (aac_acquire_resources(aac))
		goto fail_device;
	/*
	* reset this flag to unblock ioctl() as it was set at
	* aac_send_shutdown() to block ioctls from upperlayer
	*/
	aac->adapter_shutdown = 0;
	scsi_unblock_requests(shost);

	return 0;

fail_device:
	printk(KERN_INFO "%s%d: resume failed.\n", aac->name, aac->id);
	scsi_host_put(shost);
	pci_disable_device(pdev);
	return -ENODEV;
}
#endif

static void aac_shutdown(struct pci_dev *dev)
{
	struct Scsi_Host *shost = pci_get_drvdata(dev);
	scsi_block_requests(shost);
	__aac_shutdown((struct aac_dev *)shost->hostdata);
}

static void aac_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = (struct aac_dev *)shost->hostdata;

	aac_cancel_safw_rescan_worker(aac);
	scsi_remove_host(shost);

	__aac_shutdown(aac);
	aac_fib_map_free(aac);
	dma_free_coherent(&aac->pdev->dev, aac->comm_size, aac->comm_addr,
			  aac->comm_phys);
	kfree(aac->queues);

	aac_adapter_ioremap(aac, 0);

	kfree(aac->fibs);
	kfree(aac->fsa_dev);

	list_del(&aac->entry);
	scsi_host_put(shost);
	pci_disable_device(pdev);
	if (list_empty(&aac_devices)) {
		unregister_chrdev(aac_cfg_major, "aac");
		aac_cfg_major = AAC_CHARDEV_NEEDS_REINIT;
	}
}

static void aac_flush_ios(struct aac_dev *aac)
{
	int i;
	struct scsi_cmnd *cmd;

	for (i = 0; i < aac->scsi_host_ptr->can_queue; i++) {
		cmd = (struct scsi_cmnd *)aac->fibs[i].callback_data;
		if (cmd && (cmd->SCp.phase == AAC_OWNER_FIRMWARE)) {
			scsi_dma_unmap(cmd);

			if (aac->handle_pci_error)
				cmd->result = DID_NO_CONNECT << 16;
			else
				cmd->result = DID_RESET << 16;

			cmd->scsi_done(cmd);
		}
	}
}

static pci_ers_result_t aac_pci_error_detected(struct pci_dev *pdev,
					enum pci_channel_state error)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = shost_priv(shost);

	dev_err(&pdev->dev, "aacraid: PCI error detected %x\n", error);

	switch (error) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		aac->handle_pci_error = 1;

		scsi_block_requests(aac->scsi_host_ptr);
		aac_cancel_safw_rescan_worker(aac);
		aac_flush_ios(aac);
		aac_release_resources(aac);

		pci_disable_pcie_error_reporting(pdev);
		aac_adapter_ioremap(aac, 0);

		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		aac->handle_pci_error = 1;

		aac_flush_ios(aac);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t aac_pci_mmio_enabled(struct pci_dev *pdev)
{
	dev_err(&pdev->dev, "aacraid: PCI error - mmio enabled\n");
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t aac_pci_slot_reset(struct pci_dev *pdev)
{
	dev_err(&pdev->dev, "aacraid: PCI error - slot reset\n");
	pci_restore_state(pdev);
	if (pci_enable_device(pdev)) {
		dev_warn(&pdev->dev,
			"aacraid: failed to enable slave\n");
		goto fail_device;
	}

	pci_set_master(pdev);

	if (pci_enable_device_mem(pdev)) {
		dev_err(&pdev->dev, "pci_enable_device_mem failed\n");
		goto fail_device;
	}

	return PCI_ERS_RESULT_RECOVERED;

fail_device:
	dev_err(&pdev->dev, "aacraid: PCI error - slot reset failed\n");
	return PCI_ERS_RESULT_DISCONNECT;
}


static void aac_pci_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct scsi_device *sdev = NULL;
	struct aac_dev *aac = (struct aac_dev *)shost_priv(shost);

	if (aac_adapter_ioremap(aac, aac->base_size)) {

		dev_err(&pdev->dev, "aacraid: ioremap failed\n");
		/* remap failed, go back ... */
		aac->comm_interface = AAC_COMM_PRODUCER;
		if (aac_adapter_ioremap(aac, AAC_MIN_FOOTPRINT_SIZE)) {
			dev_warn(&pdev->dev,
				"aacraid: unable to map adapter.\n");

			return;
		}
	}

	msleep(10000);

	aac_acquire_resources(aac);

	/*
	 * reset this flag to unblock ioctl() as it was set
	 * at aac_send_shutdown() to block ioctls from upperlayer
	 */
	aac->adapter_shutdown = 0;
	aac->handle_pci_error = 0;

	shost_for_each_device(sdev, shost)
		if (sdev->sdev_state == SDEV_OFFLINE)
			sdev->sdev_state = SDEV_RUNNING;
	scsi_unblock_requests(aac->scsi_host_ptr);
	aac_scan_host(aac);
	pci_save_state(pdev);

	dev_err(&pdev->dev, "aacraid: PCI error - resume\n");
}

static struct pci_error_handlers aac_pci_err_handler = {
	.error_detected		= aac_pci_error_detected,
	.mmio_enabled		= aac_pci_mmio_enabled,
	.slot_reset		= aac_pci_slot_reset,
	.resume			= aac_pci_resume,
};

static struct pci_driver aac_pci_driver = {
	.name		= AAC_DRIVERNAME,
	.id_table	= aac_pci_tbl,
	.probe		= aac_probe_one,
	.remove		= aac_remove_one,
#if (defined(CONFIG_PM))
	.suspend	= aac_suspend,
	.resume		= aac_resume,
#endif
	.shutdown	= aac_shutdown,
	.err_handler    = &aac_pci_err_handler,
};

static int __init aac_init(void)
{
	int error;

	printk(KERN_INFO "Adaptec %s driver %s\n",
	  AAC_DRIVERNAME, aac_driver_version);

	error = pci_register_driver(&aac_pci_driver);
	if (error < 0)
		return error;

	aac_init_char();


	return 0;
}

static void __exit aac_exit(void)
{
	if (aac_cfg_major > -1)
		unregister_chrdev(aac_cfg_major, "aac");
	pci_unregister_driver(&aac_pci_driver);
}

module_init(aac_init);
module_exit(aac_exit);
