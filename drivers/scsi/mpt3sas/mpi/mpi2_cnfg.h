/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2000-2020 Broadcom Inc. All rights reserved.
 *
 *
 *          Name:  mpi2_cnfg.h
 *         Title:  MPI Configuration messages and pages
 * Creation Date:  November 10, 2006
 *
 *    mpi2_cnfg.h Version:  02.00.47
 *
 * NOTE: Names (typedefs, defines, etc.) beginning with an MPI25 or Mpi25
 *       prefix are for use only on MPI v2.5 products, and must not be used
 *       with MPI v2.0 products. Unless otherwise noted, names beginning with
 *       MPI2 or Mpi2 are for use with both MPI v2.0 and MPI v2.5 products.
 *
 * Version History
 * ---------------
 *
 * Date      Version   Description
 * --------  --------  ------------------------------------------------------
 * 04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 * 06-04-07  02.00.01  Added defines for SAS IO Unit Page 2 PhyFlags.
 *                     Added Manufacturing Page 11.
 *                     Added MPI2_SAS_EXPANDER0_FLAGS_CONNECTOR_END_DEVICE
 *                     define.
 * 06-26-07  02.00.02  Adding generic structure for product-specific
 *                     Manufacturing pages: MPI2_CONFIG_PAGE_MANUFACTURING_PS.
 *                     Rework of BIOS Page 2 configuration page.
 *                     Fixed MPI2_BIOSPAGE2_BOOT_DEVICE to be a union of the
 *                     forms.
 *                     Added configuration pages IOC Page 8 and Driver
 *                     Persistent Mapping Page 0.
 * 08-31-07  02.00.03  Modified configuration pages dealing with Integrated
 *                     RAID (Manufacturing Page 4, RAID Volume Pages 0 and 1,
 *                     RAID Physical Disk Pages 0 and 1, RAID Configuration
 *                     Page 0).
 *                     Added new value for AccessStatus field of SAS Device
 *                     Page 0 (_SATA_NEEDS_INITIALIZATION).
 * 10-31-07  02.00.04  Added missing SEPDevHandle field to
 *                     MPI2_CONFIG_PAGE_SAS_ENCLOSURE_0.
 * 12-18-07  02.00.05  Modified IO Unit Page 0 to use 32-bit version fields for
 *                     NVDATA.
 *                     Modified IOC Page 7 to use masks and added field for
 *                     SASBroadcastPrimitiveMasks.
 *                     Added MPI2_CONFIG_PAGE_BIOS_4.
 *                     Added MPI2_CONFIG_PAGE_LOG_0.
 * 02-29-08  02.00.06  Modified various names to make them 32-character unique.
 *                     Added SAS Device IDs.
 *                     Updated Integrated RAID configuration pages including
 *                     Manufacturing Page 4, IOC Page 6, and RAID Configuration
 *                     Page 0.
 * 05-21-08  02.00.07  Added define MPI2_MANPAGE4_MIX_SSD_SAS_SATA.
 *                     Added define MPI2_MANPAGE4_PHYSDISK_128MB_COERCION.
 *                     Fixed define MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING.
 *                     Added missing MaxNumRoutedSasAddresses field to
 *                     MPI2_CONFIG_PAGE_EXPANDER_0.
 *                     Added SAS Port Page 0.
 *                     Modified structure layout for
 *                     MPI2_CONFIG_PAGE_DRIVER_MAPPING_0.
 * 06-27-08  02.00.08  Changed MPI2_CONFIG_PAGE_RD_PDISK_1 to use
 *                     MPI2_RAID_PHYS_DISK1_PATH_MAX to size the array.
 * 10-02-08  02.00.09  Changed MPI2_RAID_PGAD_CONFIGNUM_MASK from 0x0000FFFF
 *                     to 0x000000FF.
 *                     Added two new values for the Physical Disk Coercion Size
 *                     bits in the Flags field of Manufacturing Page 4.
 *                     Added product-specific Manufacturing pages 16 to 31.
 *                     Modified Flags bits for controlling write cache on SATA
 *                     drives in IO Unit Page 1.
 *                     Added new bit to AdditionalControlFlags of SAS IO Unit
 *                     Page 1 to control Invalid Topology Correction.
 *                     Added additional defines for RAID Volume Page 0
 *                     VolumeStatusFlags field.
 *                     Modified meaning of RAID Volume Page 0 VolumeSettings
 *                     define for auto-configure of hot-swap drives.
 *                     Added SupportedPhysDisks field to RAID Volume Page 1 and
 *                     added related defines.
 *                     Added PhysDiskAttributes field (and related defines) to
 *                     RAID Physical Disk Page 0.
 *                     Added MPI2_SAS_PHYINFO_PHY_VACANT define.
 *                     Added three new DiscoveryStatus bits for SAS IO Unit
 *                     Page 0 and SAS Expander Page 0.
 *                     Removed multiplexing information from SAS IO Unit pages.
 *                     Added BootDeviceWaitTime field to SAS IO Unit Page 4.
 *                     Removed Zone Address Resolved bit from PhyInfo and from
 *                     Expander Page 0 Flags field.
 *                     Added two new AccessStatus values to SAS Device Page 0
 *                     for indicating routing problems. Added 3 reserved words
 *                     to this page.
 * 01-19-09  02.00.10  Fixed defines for GPIOVal field of IO Unit Page 3.
 *                     Inserted missing reserved field into structure for IOC
 *                     Page 6.
 *                     Added more pending task bits to RAID Volume Page 0
 *                     VolumeStatusFlags defines.
 *                     Added MPI2_PHYSDISK0_STATUS_FLAG_NOT_CERTIFIED define.
 *                     Added a new DiscoveryStatus bit for SAS IO Unit Page 0
 *                     and SAS Expander Page 0 to flag a downstream initiator
 *                     when in simplified routing mode.
 *                     Removed SATA Init Failure defines for DiscoveryStatus
 *                     fields of SAS IO Unit Page 0 and SAS Expander Page 0.
 *                     Added MPI2_SAS_DEVICE0_ASTATUS_DEVICE_BLOCKED define.
 *                     Added PortGroups, DmaGroup, and ControlGroup fields to
 *                     SAS Device Page 0.
 * 05-06-09  02.00.11  Added structures and defines for IO Unit Page 5 and IO
 *                     Unit Page 6.
 *                     Added expander reduced functionality data to SAS
 *                     Expander Page 0.
 *                     Added SAS PHY Page 2 and SAS PHY Page 3.
 * 07-30-09  02.00.12  Added IO Unit Page 7.
 *                     Added new device ids.
 *                     Added SAS IO Unit Page 5.
 *                     Added partial and slumber power management capable flags
 *                     to SAS Device Page 0 Flags field.
 *                     Added PhyInfo defines for power condition.
 *                     Added Ethernet configuration pages.
 * 10-28-09  02.00.13  Added MPI2_IOUNITPAGE1_ENABLE_HOST_BASED_DISCOVERY.
 *                     Added SAS PHY Page 4 structure and defines.
 * 02-10-10  02.00.14  Modified the comments for the configuration page
 *                     structures that contain an array of data. The host
 *                     should use the "count" field in the page data (e.g. the
 *                     NumPhys field) to determine the number of valid elements
 *                     in the array.
 *                     Added/modified some MPI2_MFGPAGE_DEVID_SAS defines.
 *                     Added PowerManagementCapabilities to IO Unit Page 7.
 *                     Added PortWidthModGroup field to
 *                     MPI2_SAS_IO_UNIT5_PHY_PM_SETTINGS.
 *                     Added MPI2_CONFIG_PAGE_SASIOUNIT_6 and related defines.
 *                     Added MPI2_CONFIG_PAGE_SASIOUNIT_7 and related defines.
 *                     Added MPI2_CONFIG_PAGE_SASIOUNIT_8 and related defines.
 * 05-12-10  02.00.15  Added MPI2_RAIDVOL0_STATUS_FLAG_VOL_NOT_CONSISTENT
 *                     define.
 *                     Added MPI2_PHYSDISK0_INCOMPATIBLE_MEDIA_TYPE define.
 *                     Added MPI2_SAS_NEG_LINK_RATE_UNSUPPORTED_PHY define.
 * 08-11-10  02.00.16  Removed IO Unit Page 1 device path (multi-pathing)
 *                     defines.
 * 11-10-10  02.00.17  Added ReceptacleID field (replacing Reserved1) to
 *                     MPI2_MANPAGE7_CONNECTOR_INFO and reworked defines for
 *                     the Pinout field.
 *                     Added BoardTemperature and BoardTemperatureUnits fields
 *                     to MPI2_CONFIG_PAGE_IO_UNIT_7.
 *                     Added MPI2_CONFIG_EXTPAGETYPE_EXT_MANUFACTURING define
 *                     and MPI2_CONFIG_PAGE_EXT_MAN_PS structure.
 * 02-23-11  02.00.18  Added ProxyVF_ID field to MPI2_CONFIG_REQUEST.
 *                     Added IO Unit Page 8, IO Unit Page 9,
 *                     and IO Unit Page 10.
 *                     Added SASNotifyPrimitiveMasks field to
 *                     MPI2_CONFIG_PAGE_IOC_7.
 * 03-09-11  02.00.19  Fixed IO Unit Page 10 (to match the spec).
 * 05-25-11  02.00.20  Cleaned up a few comments.
 * 08-24-11  02.00.21  Marked the IO Unit Page 7 PowerManagementCapabilities
 *                     for PCIe link as obsolete.
 *                     Added SpinupFlags field containing a Disable Spin-up bit
 *                     to the MPI2_SAS_IOUNIT4_SPINUP_GROUP fields of SAS IO
 *                     Unit Page 4.
 * 11-18-11  02.00.22  Added define MPI2_IOCPAGE6_CAP_FLAGS_4K_SECTORS_SUPPORT.
 *                     Added UEFIVersion field to BIOS Page 1 and defined new
 *                     BiosOptions bits.
 *                     Incorporating additions for MPI v2.5.
 * 11-27-12  02.00.23  Added MPI2_MANPAGE7_FLAG_EVENTREPLAY_SLOT_ORDER.
 *                     Added MPI2_BIOSPAGE1_OPTIONS_MASK_OEM_ID.
 * 12-20-12  02.00.24  Marked MPI2_SASIOUNIT1_CONTROL_CLEAR_AFFILIATION as
 *                     obsolete for MPI v2.5 and later.
 *                     Added some defines for 12G SAS speeds.
 * 04-09-13  02.00.25  Added MPI2_IOUNITPAGE1_ATA_SECURITY_FREEZE_LOCK.
 *                     Fixed MPI2_IOUNITPAGE5_DMA_CAP_MASK_MAX_REQUESTS to
 *                     match the specification.
 * 08-19-13  02.00.26  Added reserved words to MPI2_CONFIG_PAGE_IO_UNIT_7 for
 *			future use.
 * 12-05-13  02.00.27  Added MPI2_MANPAGE7_FLAG_BASE_ENCLOSURE_LEVEL for
 *		       MPI2_CONFIG_PAGE_MAN_7.
 *		       Added EnclosureLevel and ConnectorName fields to
 *		       MPI2_CONFIG_PAGE_SAS_DEV_0.
 *		       Added MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID for
 *		       MPI2_CONFIG_PAGE_SAS_DEV_0.
 *		       Added EnclosureLevel field to
 *		       MPI2_CONFIG_PAGE_SAS_ENCLOSURE_0.
 *		       Added MPI2_SAS_ENCLS0_FLAGS_ENCL_LEVEL_VALID for
 *		       MPI2_CONFIG_PAGE_SAS_ENCLOSURE_0.
 * 01-08-14  02.00.28  Added more defines for the BiosOptions field of
 *		       MPI2_CONFIG_PAGE_BIOS_1.
 * 06-13-14  02.00.29  Added SSUTimeout field to MPI2_CONFIG_PAGE_BIOS_1, and
 *                     more defines for the BiosOptions field.
 * 11-18-14  02.00.30  Updated copyright information.
 *                     Added MPI2_BIOSPAGE1_OPTIONS_ADVANCED_CONFIG.
 *                     Added AdapterOrderAux fields to BIOS Page 3.
 * 03-16-15  02.00.31  Updated for MPI v2.6.
 *                     Added Flags field to IO Unit Page 7.
 *                     Added new SAS Phy Event codes
 * 05-25-15  02.00.33  Added more defines for the BiosOptions field of
 *                     MPI2_CONFIG_PAGE_BIOS_1.
 * 08-25-15  02.00.34  Bumped Header Version.
 * 12-18-15  02.00.35  Added SATADeviceWaitTime to SAS IO Unit Page 4.
 * 01-21-16  02.00.36  Added/modified MPI2_MFGPAGE_DEVID_SAS defines.
 *                     Added Link field to PCIe Link Pages
 *                     Added EnclosureLevel and ConnectorName to PCIe
 *                     Device Page 0.
 *                     Added define for PCIE IoUnit page 1 max rate shift.
 *                     Added comment for reserved ExtPageTypes.
 *                     Added SAS 4 22.5 gbs speed support.
 *                     Added PCIe 4 16.0 GT/sec speec support.
 *                     Removed AHCI support.
 *                     Removed SOP support.
 *                     Added NegotiatedLinkRate and NegotiatedPortWidth to
 *                     PCIe device page 0.
 * 04-10-16  02.00.37  Fixed MPI2_MFGPAGE_DEVID_SAS3616/3708 defines
 * 07-01-16  02.00.38  Added Manufacturing page 7 Connector types.
 *                     Changed declaration of ConnectorName in PCIe DevicePage0
 *                     to match SAS DevicePage 0.
 *                     Added SATADeviceWaitTime to IO Unit Page 11.
 *                     Added MPI26_MFGPAGE_DEVID_SAS4008
 *                     Added x16 PCIe width to IO Unit Page 7
 *                     Added LINKFLAGS to control SRIS in PCIe IO Unit page 1
 *                     phy data.
 *                     Added InitStatus to PCIe IO Unit Page 1 header.
 * 09-01-16  02.00.39  Added MPI26_CONFIG_PAGE_ENCLOSURE_0 and related defines.
 *                     Added MPI26_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE and
 *                     MPI26_ENCLOS_PGAD_FORM_HANDLE page address formats.
 * 02-02-17  02.00.40  Added MPI2_MANPAGE7_SLOT_UNKNOWN.
 *                     Added ChassisSlot field to SAS Enclosure Page 0.
 *                     Added ChassisSlot Valid bit (bit 5) to the Flags field
 *                     in SAS Enclosure Page 0.
 * 06-13-17  02.00.41  Added MPI26_MFGPAGE_DEVID_SAS3816 and
 *                     MPI26_MFGPAGE_DEVID_SAS3916 defines.
 *                     Removed MPI26_MFGPAGE_DEVID_SAS4008 define.
 *                     Added MPI26_PCIEIOUNIT1_LINKFLAGS_SRNS_EN define.
 *                     Renamed PI26_PCIEIOUNIT1_LINKFLAGS_EN_SRIS to
 *                     PI26_PCIEIOUNIT1_LINKFLAGS_SRIS_EN.
 *                     Renamed MPI26_PCIEIOUNIT1_LINKFLAGS_DIS_SRIS to
 *                     MPI26_PCIEIOUNIT1_LINKFLAGS_DIS_SEPARATE_REFCLK.
 * 09-29-17  02.00.42  Added ControllerResetTO field to PCIe Device Page 2.
 *                     Added NOIOB field to PCIe Device Page 2.
 *                     Added MPI26_PCIEDEV2_CAP_DATA_BLK_ALIGN_AND_GRAN to
 *                     the Capabilities field of PCIe Device Page 2.
 * 07-22-18  02.00.43  Added defines for SAS3916 and SAS3816.
 *                     Added WRiteCache defines to IO Unit Page 1.
 *                     Added MaxEnclosureLevel to BIOS Page 1.
 *                     Added OEMRD to SAS Enclosure Page 1.
 *                     Added DMDReportPCIe to PCIe IO Unit Page 1.
 *                     Added Flags field and flags for Retimers to
 *                     PCIe Switch Page 1.
 * 08-02-18  02.00.44  Added Slotx2, Slotx4 to ManPage 7.
 * 08-15-18  02.00.45  Added ProductSpecific field at end of IOC Page 1
 * 08-28-18  02.00.46  Added NVMs Write Cache flag to IOUnitPage1
 *                     Added DMDReport Delay Time defines to
 *                     PCIeIOUnitPage1
 * --------------------------------------------------------------------------
 * 08-02-18  02.00.44  Added Slotx2, Slotx4 to ManPage 7.
 * 08-15-18  02.00.45  Added ProductSpecific field at end of IOC Page 1
 * 08-28-18  02.00.46  Added NVMs Write Cache flag to IOUnitPage1
 *                     Added DMDReport Delay Time defines to PCIeIOUnitPage1
 * 12-17-18  02.00.47  Swap locations of Slotx2 and Slotx4 in ManPage 7.
 */

#ifndef MPI2_CNFG_H
#define MPI2_CNFG_H

/*****************************************************************************
*  Configuration Page Header and defines
*****************************************************************************/

/*Config Page Header */
typedef struct _MPI2_CONFIG_PAGE_HEADER {
	U8                 PageVersion;                /*0x00 */
	U8                 PageLength;                 /*0x01 */
	U8                 PageNumber;                 /*0x02 */
	U8                 PageType;                   /*0x03 */
} MPI2_CONFIG_PAGE_HEADER, *PTR_MPI2_CONFIG_PAGE_HEADER,
	Mpi2ConfigPageHeader_t, *pMpi2ConfigPageHeader_t;

typedef union _MPI2_CONFIG_PAGE_HEADER_UNION {
	MPI2_CONFIG_PAGE_HEADER  Struct;
	U8                       Bytes[4];
	U16                      Word16[2];
	U32                      Word32;
} MPI2_CONFIG_PAGE_HEADER_UNION, *PTR_MPI2_CONFIG_PAGE_HEADER_UNION,
	Mpi2ConfigPageHeaderUnion, *pMpi2ConfigPageHeaderUnion;

/*Extended Config Page Header */
typedef struct _MPI2_CONFIG_EXTENDED_PAGE_HEADER {
	U8                  PageVersion;                /*0x00 */
	U8                  Reserved1;                  /*0x01 */
	U8                  PageNumber;                 /*0x02 */
	U8                  PageType;                   /*0x03 */
	U16                 ExtPageLength;              /*0x04 */
	U8                  ExtPageType;                /*0x06 */
	U8                  Reserved2;                  /*0x07 */
} MPI2_CONFIG_EXTENDED_PAGE_HEADER,
	*PTR_MPI2_CONFIG_EXTENDED_PAGE_HEADER,
	Mpi2ConfigExtendedPageHeader_t,
	*pMpi2ConfigExtendedPageHeader_t;

typedef union _MPI2_CONFIG_EXT_PAGE_HEADER_UNION {
	MPI2_CONFIG_PAGE_HEADER          Struct;
	MPI2_CONFIG_EXTENDED_PAGE_HEADER Ext;
	U8                               Bytes[8];
	U16                              Word16[4];
	U32                              Word32[2];
} MPI2_CONFIG_EXT_PAGE_HEADER_UNION,
	*PTR_MPI2_CONFIG_EXT_PAGE_HEADER_UNION,
	Mpi2ConfigPageExtendedHeaderUnion,
	*pMpi2ConfigPageExtendedHeaderUnion;


/*PageType field values */
#define MPI2_CONFIG_PAGEATTR_READ_ONLY              (0x00)
#define MPI2_CONFIG_PAGEATTR_CHANGEABLE             (0x10)
#define MPI2_CONFIG_PAGEATTR_PERSISTENT             (0x20)
#define MPI2_CONFIG_PAGEATTR_MASK                   (0xF0)

#define MPI2_CONFIG_PAGETYPE_IO_UNIT                (0x00)
#define MPI2_CONFIG_PAGETYPE_IOC                    (0x01)
#define MPI2_CONFIG_PAGETYPE_BIOS                   (0x02)
#define MPI2_CONFIG_PAGETYPE_RAID_VOLUME            (0x08)
#define MPI2_CONFIG_PAGETYPE_MANUFACTURING          (0x09)
#define MPI2_CONFIG_PAGETYPE_RAID_PHYSDISK          (0x0A)
#define MPI2_CONFIG_PAGETYPE_EXTENDED               (0x0F)
#define MPI2_CONFIG_PAGETYPE_MASK                   (0x0F)

#define MPI2_CONFIG_TYPENUM_MASK                    (0x0FFF)


/*ExtPageType field values */
#define MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT         (0x10)
#define MPI2_CONFIG_EXTPAGETYPE_SAS_EXPANDER        (0x11)
#define MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE          (0x12)
#define MPI2_CONFIG_EXTPAGETYPE_SAS_PHY             (0x13)
#define MPI2_CONFIG_EXTPAGETYPE_LOG                 (0x14)
#define MPI2_CONFIG_EXTPAGETYPE_ENCLOSURE           (0x15)
#define MPI2_CONFIG_EXTPAGETYPE_RAID_CONFIG         (0x16)
#define MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING      (0x17)
#define MPI2_CONFIG_EXTPAGETYPE_SAS_PORT            (0x18)
#define MPI2_CONFIG_EXTPAGETYPE_ETHERNET            (0x19)
#define MPI2_CONFIG_EXTPAGETYPE_EXT_MANUFACTURING   (0x1A)
#define MPI2_CONFIG_EXTPAGETYPE_PCIE_IO_UNIT        (0x1B)
#define MPI2_CONFIG_EXTPAGETYPE_PCIE_SWITCH         (0x1C)
#define MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE         (0x1D)
#define MPI2_CONFIG_EXTPAGETYPE_PCIE_LINK           (0x1E)


/*****************************************************************************
*  PageAddress defines
*****************************************************************************/

/*RAID Volume PageAddress format */
#define MPI2_RAID_VOLUME_PGAD_FORM_MASK             (0xF0000000)
#define MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE  (0x00000000)
#define MPI2_RAID_VOLUME_PGAD_FORM_HANDLE           (0x10000000)

#define MPI2_RAID_VOLUME_PGAD_HANDLE_MASK           (0x0000FFFF)


/*RAID Physical Disk PageAddress format */
#define MPI2_PHYSDISK_PGAD_FORM_MASK                    (0xF0000000)
#define MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM    (0x00000000)
#define MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM             (0x10000000)
#define MPI2_PHYSDISK_PGAD_FORM_DEVHANDLE               (0x20000000)

#define MPI2_PHYSDISK_PGAD_PHYSDISKNUM_MASK             (0x000000FF)
#define MPI2_PHYSDISK_PGAD_DEVHANDLE_MASK               (0x0000FFFF)


/*SAS Expander PageAddress format */
#define MPI2_SAS_EXPAND_PGAD_FORM_MASK              (0xF0000000)
#define MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL     (0x00000000)
#define MPI2_SAS_EXPAND_PGAD_FORM_HNDL_PHY_NUM      (0x10000000)
#define MPI2_SAS_EXPAND_PGAD_FORM_HNDL              (0x20000000)

#define MPI2_SAS_EXPAND_PGAD_HANDLE_MASK            (0x0000FFFF)
#define MPI2_SAS_EXPAND_PGAD_PHYNUM_MASK            (0x00FF0000)
#define MPI2_SAS_EXPAND_PGAD_PHYNUM_SHIFT           (16)


/*SAS Device PageAddress format */
#define MPI2_SAS_DEVICE_PGAD_FORM_MASK              (0xF0000000)
#define MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE   (0x00000000)
#define MPI2_SAS_DEVICE_PGAD_FORM_HANDLE            (0x20000000)

#define MPI2_SAS_DEVICE_PGAD_HANDLE_MASK            (0x0000FFFF)


/*SAS PHY PageAddress format */
#define MPI2_SAS_PHY_PGAD_FORM_MASK                 (0xF0000000)
#define MPI2_SAS_PHY_PGAD_FORM_PHY_NUMBER           (0x00000000)
#define MPI2_SAS_PHY_PGAD_FORM_PHY_TBL_INDEX        (0x10000000)

#define MPI2_SAS_PHY_PGAD_PHY_NUMBER_MASK           (0x000000FF)
#define MPI2_SAS_PHY_PGAD_PHY_TBL_INDEX_MASK        (0x0000FFFF)


/*SAS Port PageAddress format */
#define MPI2_SASPORT_PGAD_FORM_MASK                 (0xF0000000)
#define MPI2_SASPORT_PGAD_FORM_GET_NEXT_PORT        (0x00000000)
#define MPI2_SASPORT_PGAD_FORM_PORT_NUM             (0x10000000)

#define MPI2_SASPORT_PGAD_PORTNUMBER_MASK           (0x00000FFF)


/*SAS Enclosure PageAddress format */
#define MPI2_SAS_ENCLOS_PGAD_FORM_MASK              (0xF0000000)
#define MPI2_SAS_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE   (0x00000000)
#define MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE            (0x10000000)

#define MPI2_SAS_ENCLOS_PGAD_HANDLE_MASK            (0x0000FFFF)

/*Enclosure PageAddress format */
#define MPI26_ENCLOS_PGAD_FORM_MASK                 (0xF0000000)
#define MPI26_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE      (0x00000000)
#define MPI26_ENCLOS_PGAD_FORM_HANDLE               (0x10000000)

#define MPI26_ENCLOS_PGAD_HANDLE_MASK               (0x0000FFFF)

/*RAID Configuration PageAddress format */
#define MPI2_RAID_PGAD_FORM_MASK                    (0xF0000000)
#define MPI2_RAID_PGAD_FORM_GET_NEXT_CONFIGNUM      (0x00000000)
#define MPI2_RAID_PGAD_FORM_CONFIGNUM               (0x10000000)
#define MPI2_RAID_PGAD_FORM_ACTIVE_CONFIG           (0x20000000)

#define MPI2_RAID_PGAD_CONFIGNUM_MASK               (0x000000FF)


/*Driver Persistent Mapping PageAddress format */
#define MPI2_DPM_PGAD_FORM_MASK                     (0xF0000000)
#define MPI2_DPM_PGAD_FORM_ENTRY_RANGE              (0x00000000)

#define MPI2_DPM_PGAD_ENTRY_COUNT_MASK              (0x0FFF0000)
#define MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT             (16)
#define MPI2_DPM_PGAD_START_ENTRY_MASK              (0x0000FFFF)


/*Ethernet PageAddress format */
#define MPI2_ETHERNET_PGAD_FORM_MASK                (0xF0000000)
#define MPI2_ETHERNET_PGAD_FORM_IF_NUM              (0x00000000)

#define MPI2_ETHERNET_PGAD_IF_NUMBER_MASK           (0x000000FF)


/*PCIe Switch PageAddress format */
#define MPI26_PCIE_SWITCH_PGAD_FORM_MASK            (0xF0000000)
#define MPI26_PCIE_SWITCH_PGAD_FORM_GET_NEXT_HNDL   (0x00000000)
#define MPI26_PCIE_SWITCH_PGAD_FORM_HNDL_PORTNUM    (0x10000000)
#define MPI26_PCIE_SWITCH_EXPAND_PGAD_FORM_HNDL     (0x20000000)

#define MPI26_PCIE_SWITCH_PGAD_HANDLE_MASK          (0x0000FFFF)
#define MPI26_PCIE_SWITCH_PGAD_PORTNUM_MASK         (0x00FF0000)
#define MPI26_PCIE_SWITCH_PGAD_PORTNUM_SHIFT        (16)


/*PCIe Device PageAddress format */
#define MPI26_PCIE_DEVICE_PGAD_FORM_MASK            (0xF0000000)
#define MPI26_PCIE_DEVICE_PGAD_FORM_GET_NEXT_HANDLE (0x00000000)
#define MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE          (0x20000000)

#define MPI26_PCIE_DEVICE_PGAD_HANDLE_MASK          (0x0000FFFF)

/*PCIe Link PageAddress format */
#define MPI26_PCIE_LINK_PGAD_FORM_MASK            (0xF0000000)
#define MPI26_PCIE_LINK_PGAD_FORM_GET_NEXT_LINK   (0x00000000)
#define MPI26_PCIE_LINK_PGAD_FORM_LINK_NUM        (0x10000000)

#define MPI26_PCIE_DEVICE_PGAD_LINKNUM_MASK       (0x000000FF)



/****************************************************************************
*  Configuration messages
****************************************************************************/

/*Configuration Request Message */
typedef struct _MPI2_CONFIG_REQUEST {
	U8                      Action;                     /*0x00 */
	U8                      SGLFlags;                   /*0x01 */
	U8                      ChainOffset;                /*0x02 */
	U8                      Function;                   /*0x03 */
	U16                     ExtPageLength;              /*0x04 */
	U8                      ExtPageType;                /*0x06 */
	U8                      MsgFlags;                   /*0x07 */
	U8                      VP_ID;                      /*0x08 */
	U8                      VF_ID;                      /*0x09 */
	U16                     Reserved1;                  /*0x0A */
	U8                      Reserved2;                  /*0x0C */
	U8                      ProxyVF_ID;                 /*0x0D */
	U16                     Reserved4;                  /*0x0E */
	U32                     Reserved3;                  /*0x10 */
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x14 */
	U32                     PageAddress;                /*0x18 */
	MPI2_SGE_IO_UNION       PageBufferSGE;              /*0x1C */
} MPI2_CONFIG_REQUEST, *PTR_MPI2_CONFIG_REQUEST,
	Mpi2ConfigRequest_t, *pMpi2ConfigRequest_t;

/*values for the Action field */
#define MPI2_CONFIG_ACTION_PAGE_HEADER              (0x00)
#define MPI2_CONFIG_ACTION_PAGE_READ_CURRENT        (0x01)
#define MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT       (0x02)
#define MPI2_CONFIG_ACTION_PAGE_DEFAULT             (0x03)
#define MPI2_CONFIG_ACTION_PAGE_WRITE_NVRAM         (0x04)
#define MPI2_CONFIG_ACTION_PAGE_READ_DEFAULT        (0x05)
#define MPI2_CONFIG_ACTION_PAGE_READ_NVRAM          (0x06)
#define MPI2_CONFIG_ACTION_PAGE_GET_CHANGEABLE      (0x07)

/*use MPI2_SGLFLAGS_ defines from mpi2.h for the SGLFlags field */


/*Config Reply Message */
typedef struct _MPI2_CONFIG_REPLY {
	U8                      Action;                     /*0x00 */
	U8                      SGLFlags;                   /*0x01 */
	U8                      MsgLength;                  /*0x02 */
	U8                      Function;                   /*0x03 */
	U16                     ExtPageLength;              /*0x04 */
	U8                      ExtPageType;                /*0x06 */
	U8                      MsgFlags;                   /*0x07 */
	U8                      VP_ID;                      /*0x08 */
	U8                      VF_ID;                      /*0x09 */
	U16                     Reserved1;                  /*0x0A */
	U16                     Reserved2;                  /*0x0C */
	U16                     IOCStatus;                  /*0x0E */
	U32                     IOCLogInfo;                 /*0x10 */
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x14 */
} MPI2_CONFIG_REPLY, *PTR_MPI2_CONFIG_REPLY,
	Mpi2ConfigReply_t, *pMpi2ConfigReply_t;



/*****************************************************************************
*
*              C o n f i g u r a t i o n    P a g e s
*
*****************************************************************************/

/****************************************************************************
*  Manufacturing Config pages
****************************************************************************/

#define MPI2_MFGPAGE_VENDORID_LSI                   (0x1000)

/*MPI v2.0 SAS products */
#define MPI2_MFGPAGE_DEVID_SAS2004                  (0x0070)
#define MPI2_MFGPAGE_DEVID_SAS2008                  (0x0072)
#define MPI2_MFGPAGE_DEVID_SAS2108_1                (0x0074)
#define MPI2_MFGPAGE_DEVID_SAS2108_2                (0x0076)
#define MPI2_MFGPAGE_DEVID_SAS2108_3                (0x0077)
#define MPI2_MFGPAGE_DEVID_SAS2116_1                (0x0064)
#define MPI2_MFGPAGE_DEVID_SAS2116_2                (0x0065)

#define MPI2_MFGPAGE_DEVID_SSS6200                  (0x007E)

#define MPI2_MFGPAGE_DEVID_SAS2208_1                (0x0080)
#define MPI2_MFGPAGE_DEVID_SAS2208_2                (0x0081)
#define MPI2_MFGPAGE_DEVID_SAS2208_3                (0x0082)
#define MPI2_MFGPAGE_DEVID_SAS2208_4                (0x0083)
#define MPI2_MFGPAGE_DEVID_SAS2208_5                (0x0084)
#define MPI2_MFGPAGE_DEVID_SAS2208_6                (0x0085)
#define MPI2_MFGPAGE_DEVID_SAS2308_1                (0x0086)
#define MPI2_MFGPAGE_DEVID_SAS2308_2                (0x0087)
#define MPI2_MFGPAGE_DEVID_SAS2308_3                (0x006E)
#define MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP            (0x02B0)
#define MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP_1          (0x02B1)

/*MPI v2.5 SAS products */
#define MPI25_MFGPAGE_DEVID_SAS3004                 (0x0096)
#define MPI25_MFGPAGE_DEVID_SAS3008                 (0x0097)
#define MPI25_MFGPAGE_DEVID_SAS3108_1               (0x0090)
#define MPI25_MFGPAGE_DEVID_SAS3108_2               (0x0091)
#define MPI25_MFGPAGE_DEVID_SAS3108_5               (0x0094)
#define MPI25_MFGPAGE_DEVID_SAS3108_6               (0x0095)

/* MPI v2.6 SAS Products */
#define MPI26_MFGPAGE_DEVID_SAS3216                 (0x00C9)
#define MPI26_MFGPAGE_DEVID_SAS3224                 (0x00C4)
#define MPI26_MFGPAGE_DEVID_SAS3316_1               (0x00C5)
#define MPI26_MFGPAGE_DEVID_SAS3316_2               (0x00C6)
#define MPI26_MFGPAGE_DEVID_SAS3316_3               (0x00C7)
#define MPI26_MFGPAGE_DEVID_SAS3316_4               (0x00C8)
#define MPI26_MFGPAGE_DEVID_SAS3324_1               (0x00C0)
#define MPI26_MFGPAGE_DEVID_SAS3324_2               (0x00C1)
#define MPI26_MFGPAGE_DEVID_SAS3324_3               (0x00C2)
#define MPI26_MFGPAGE_DEVID_SAS3324_4               (0x00C3)

#define MPI26_MFGPAGE_DEVID_SAS3516                 (0x00AA)
#define MPI26_MFGPAGE_DEVID_SAS3516_1               (0x00AB)
#define MPI26_MFGPAGE_DEVID_SAS3416                 (0x00AC)
#define MPI26_MFGPAGE_DEVID_SAS3508                 (0x00AD)
#define MPI26_MFGPAGE_DEVID_SAS3508_1               (0x00AE)
#define MPI26_MFGPAGE_DEVID_SAS3408                 (0x00AF)
#define MPI26_MFGPAGE_DEVID_SAS3716                 (0x00D0)
#define MPI26_MFGPAGE_DEVID_SAS3616                 (0x00D1)
#define MPI26_MFGPAGE_DEVID_SAS3708                 (0x00D2)

#define MPI26_MFGPAGE_DEVID_SEC_MASK_3916           (0x0003)
#define MPI26_MFGPAGE_DEVID_INVALID0_3916           (0x00E0)
#define MPI26_MFGPAGE_DEVID_CFG_SEC_3916            (0x00E1)
#define MPI26_MFGPAGE_DEVID_HARD_SEC_3916           (0x00E2)
#define MPI26_MFGPAGE_DEVID_INVALID1_3916           (0x00E3)

#define MPI26_MFGPAGE_DEVID_SEC_MASK_3816           (0x0003)
#define MPI26_MFGPAGE_DEVID_INVALID0_3816           (0x00E4)
#define MPI26_MFGPAGE_DEVID_CFG_SEC_3816            (0x00E5)
#define MPI26_MFGPAGE_DEVID_HARD_SEC_3816           (0x00E6)
#define MPI26_MFGPAGE_DEVID_INVALID1_3816           (0x00E7)


/*Manufacturing Page 0 */

typedef struct _MPI2_CONFIG_PAGE_MAN_0 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U8                      ChipName[16];               /*0x04 */
	U8                      ChipRevision[8];            /*0x14 */
	U8                      BoardName[16];              /*0x1C */
	U8                      BoardAssembly[16];          /*0x2C */
	U8                      BoardTracerNumber[16];      /*0x3C */
} MPI2_CONFIG_PAGE_MAN_0,
	*PTR_MPI2_CONFIG_PAGE_MAN_0,
	Mpi2ManufacturingPage0_t,
	*pMpi2ManufacturingPage0_t;

#define MPI2_MANUFACTURING0_PAGEVERSION                (0x00)


/*Manufacturing Page 1 */

typedef struct _MPI2_CONFIG_PAGE_MAN_1 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U8                      VPD[256];                   /*0x04 */
} MPI2_CONFIG_PAGE_MAN_1,
	*PTR_MPI2_CONFIG_PAGE_MAN_1,
	Mpi2ManufacturingPage1_t,
	*pMpi2ManufacturingPage1_t;

#define MPI2_MANUFACTURING1_PAGEVERSION                (0x00)


typedef struct _MPI2_CHIP_REVISION_ID {
	U16 DeviceID;                                       /*0x00 */
	U8  PCIRevisionID;                                  /*0x02 */
	U8  Reserved;                                       /*0x03 */
} MPI2_CHIP_REVISION_ID, *PTR_MPI2_CHIP_REVISION_ID,
	Mpi2ChipRevisionId_t, *pMpi2ChipRevisionId_t;


/*Manufacturing Page 2 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check Header.PageLength at runtime.
 */
#ifndef MPI2_MAN_PAGE_2_HW_SETTINGS_WORDS
#define MPI2_MAN_PAGE_2_HW_SETTINGS_WORDS   (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_MAN_2 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	MPI2_CHIP_REVISION_ID   ChipId;                     /*0x04 */
	U32
		HwSettings[MPI2_MAN_PAGE_2_HW_SETTINGS_WORDS];/*0x08 */
} MPI2_CONFIG_PAGE_MAN_2,
	*PTR_MPI2_CONFIG_PAGE_MAN_2,
	Mpi2ManufacturingPage2_t,
	*pMpi2ManufacturingPage2_t;

#define MPI2_MANUFACTURING2_PAGEVERSION                 (0x00)


/*Manufacturing Page 3 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check Header.PageLength at runtime.
 */
#ifndef MPI2_MAN_PAGE_3_INFO_WORDS
#define MPI2_MAN_PAGE_3_INFO_WORDS          (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_MAN_3 {
	MPI2_CONFIG_PAGE_HEADER             Header;         /*0x00 */
	MPI2_CHIP_REVISION_ID               ChipId;         /*0x04 */
	U32
		Info[MPI2_MAN_PAGE_3_INFO_WORDS];/*0x08 */
} MPI2_CONFIG_PAGE_MAN_3,
	*PTR_MPI2_CONFIG_PAGE_MAN_3,
	Mpi2ManufacturingPage3_t,
	*pMpi2ManufacturingPage3_t;

#define MPI2_MANUFACTURING3_PAGEVERSION                 (0x00)


/*Manufacturing Page 4 */

typedef struct _MPI2_MANPAGE4_PWR_SAVE_SETTINGS {
	U8                          PowerSaveFlags;                 /*0x00 */
	U8                          InternalOperationsSleepTime;    /*0x01 */
	U8                          InternalOperationsRunTime;      /*0x02 */
	U8                          HostIdleTime;                   /*0x03 */
} MPI2_MANPAGE4_PWR_SAVE_SETTINGS,
	*PTR_MPI2_MANPAGE4_PWR_SAVE_SETTINGS,
	Mpi2ManPage4PwrSaveSettings_t,
	*pMpi2ManPage4PwrSaveSettings_t;

/*defines for the PowerSaveFlags field */
#define MPI2_MANPAGE4_MASK_POWERSAVE_MODE               (0x03)
#define MPI2_MANPAGE4_POWERSAVE_MODE_DISABLED           (0x00)
#define MPI2_MANPAGE4_CUSTOM_POWERSAVE_MODE             (0x01)
#define MPI2_MANPAGE4_FULL_POWERSAVE_MODE               (0x02)

typedef struct _MPI2_CONFIG_PAGE_MAN_4 {
	MPI2_CONFIG_PAGE_HEADER             Header;                 /*0x00 */
	U32                                 Reserved1;              /*0x04 */
	U32                                 Flags;                  /*0x08 */
	U8                                  InquirySize;            /*0x0C */
	U8                                  Reserved2;              /*0x0D */
	U16                                 Reserved3;              /*0x0E */
	U8                                  InquiryData[56];        /*0x10 */
	U32                                 RAID0VolumeSettings;    /*0x48 */
	U32                                 RAID1EVolumeSettings;   /*0x4C */
	U32                                 RAID1VolumeSettings;    /*0x50 */
	U32                                 RAID10VolumeSettings;   /*0x54 */
	U32                                 Reserved4;              /*0x58 */
	U32                                 Reserved5;              /*0x5C */
	MPI2_MANPAGE4_PWR_SAVE_SETTINGS     PowerSaveSettings;      /*0x60 */
	U8                                  MaxOCEDisks;            /*0x64 */
	U8                                  ResyncRate;             /*0x65 */
	U16                                 DataScrubDuration;      /*0x66 */
	U8                                  MaxHotSpares;           /*0x68 */
	U8                                  MaxPhysDisksPerVol;     /*0x69 */
	U8                                  MaxPhysDisks;           /*0x6A */
	U8                                  MaxVolumes;             /*0x6B */
} MPI2_CONFIG_PAGE_MAN_4,
	*PTR_MPI2_CONFIG_PAGE_MAN_4,
	Mpi2ManufacturingPage4_t,
	*pMpi2ManufacturingPage4_t;

#define MPI2_MANUFACTURING4_PAGEVERSION                 (0x0A)

/*Manufacturing Page 4 Flags field */
#define MPI2_MANPAGE4_METADATA_SIZE_MASK                (0x00030000)
#define MPI2_MANPAGE4_METADATA_512MB                    (0x00000000)

#define MPI2_MANPAGE4_MIX_SSD_SAS_SATA                  (0x00008000)
#define MPI2_MANPAGE4_MIX_SSD_AND_NON_SSD               (0x00004000)
#define MPI2_MANPAGE4_HIDE_PHYSDISK_NON_IR              (0x00002000)

#define MPI2_MANPAGE4_MASK_PHYSDISK_COERCION            (0x00001C00)
#define MPI2_MANPAGE4_PHYSDISK_COERCION_1GB             (0x00000000)
#define MPI2_MANPAGE4_PHYSDISK_128MB_COERCION           (0x00000400)
#define MPI2_MANPAGE4_PHYSDISK_ADAPTIVE_COERCION        (0x00000800)
#define MPI2_MANPAGE4_PHYSDISK_ZERO_COERCION            (0x00000C00)

#define MPI2_MANPAGE4_MASK_BAD_BLOCK_MARKING            (0x00000300)
#define MPI2_MANPAGE4_DEFAULT_BAD_BLOCK_MARKING         (0x00000000)
#define MPI2_MANPAGE4_TABLE_BAD_BLOCK_MARKING           (0x00000100)
#define MPI2_MANPAGE4_WRITE_LONG_BAD_BLOCK_MARKING      (0x00000200)

#define MPI2_MANPAGE4_FORCE_OFFLINE_FAILOVER            (0x00000080)
#define MPI2_MANPAGE4_RAID10_DISABLE                    (0x00000040)
#define MPI2_MANPAGE4_RAID1E_DISABLE                    (0x00000020)
#define MPI2_MANPAGE4_RAID1_DISABLE                     (0x00000010)
#define MPI2_MANPAGE4_RAID0_DISABLE                     (0x00000008)
#define MPI2_MANPAGE4_IR_MODEPAGE8_DISABLE              (0x00000004)
#define MPI2_MANPAGE4_IM_RESYNC_CACHE_ENABLE            (0x00000002)
#define MPI2_MANPAGE4_IR_NO_MIX_SAS_SATA                (0x00000001)


/*Manufacturing Page 5 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_MAN_PAGE_5_PHY_ENTRIES
#define MPI2_MAN_PAGE_5_PHY_ENTRIES         (1)
#endif

typedef struct _MPI2_MANUFACTURING5_ENTRY {
	U64                                 WWID;           /*0x00 */
	U64                                 DeviceName;     /*0x08 */
} MPI2_MANUFACTURING5_ENTRY,
	*PTR_MPI2_MANUFACTURING5_ENTRY,
	Mpi2Manufacturing5Entry_t,
	*pMpi2Manufacturing5Entry_t;

typedef struct _MPI2_CONFIG_PAGE_MAN_5 {
	MPI2_CONFIG_PAGE_HEADER             Header;         /*0x00 */
	U8                                  NumPhys;        /*0x04 */
	U8                                  Reserved1;      /*0x05 */
	U16                                 Reserved2;      /*0x06 */
	U32                                 Reserved3;      /*0x08 */
	U32                                 Reserved4;      /*0x0C */
	MPI2_MANUFACTURING5_ENTRY
		Phy[MPI2_MAN_PAGE_5_PHY_ENTRIES];/*0x08 */
} MPI2_CONFIG_PAGE_MAN_5,
	*PTR_MPI2_CONFIG_PAGE_MAN_5,
	Mpi2ManufacturingPage5_t,
	*pMpi2ManufacturingPage5_t;

#define MPI2_MANUFACTURING5_PAGEVERSION                 (0x03)


/*Manufacturing Page 6 */

typedef struct _MPI2_CONFIG_PAGE_MAN_6 {
	MPI2_CONFIG_PAGE_HEADER         Header;             /*0x00 */
	U32                             ProductSpecificInfo;/*0x04 */
} MPI2_CONFIG_PAGE_MAN_6,
	*PTR_MPI2_CONFIG_PAGE_MAN_6,
	Mpi2ManufacturingPage6_t,
	*pMpi2ManufacturingPage6_t;

#define MPI2_MANUFACTURING6_PAGEVERSION                 (0x00)


/*Manufacturing Page 7 */

typedef struct _MPI2_MANPAGE7_CONNECTOR_INFO {
	U32                         Pinout;                 /*0x00 */
	U8                          Connector[16];          /*0x04 */
	U8                          Location;               /*0x14 */
	U8                          ReceptacleID;           /*0x15 */
	U16                         Slot;                   /*0x16 */
	U16                         Slotx2;                 /*0x18 */
	U16                         Slotx4;                 /*0x1A */
} MPI2_MANPAGE7_CONNECTOR_INFO,
	*PTR_MPI2_MANPAGE7_CONNECTOR_INFO,
	Mpi2ManPage7ConnectorInfo_t,
	*pMpi2ManPage7ConnectorInfo_t;

/*defines for the Pinout field */
#define MPI2_MANPAGE7_PINOUT_LANE_MASK                  (0x0000FF00)
#define MPI2_MANPAGE7_PINOUT_LANE_SHIFT                 (8)

#define MPI2_MANPAGE7_PINOUT_TYPE_MASK                  (0x000000FF)
#define MPI2_MANPAGE7_PINOUT_TYPE_UNKNOWN               (0x00)
#define MPI2_MANPAGE7_PINOUT_SATA_SINGLE                (0x01)
#define MPI2_MANPAGE7_PINOUT_SFF_8482                   (0x02)
#define MPI2_MANPAGE7_PINOUT_SFF_8486                   (0x03)
#define MPI2_MANPAGE7_PINOUT_SFF_8484                   (0x04)
#define MPI2_MANPAGE7_PINOUT_SFF_8087                   (0x05)
#define MPI2_MANPAGE7_PINOUT_SFF_8643_4I                (0x06)
#define MPI2_MANPAGE7_PINOUT_SFF_8643_8I                (0x07)
#define MPI2_MANPAGE7_PINOUT_SFF_8470                   (0x08)
#define MPI2_MANPAGE7_PINOUT_SFF_8088                   (0x09)
#define MPI2_MANPAGE7_PINOUT_SFF_8644_4X                (0x0A)
#define MPI2_MANPAGE7_PINOUT_SFF_8644_8X                (0x0B)
#define MPI2_MANPAGE7_PINOUT_SFF_8644_16X               (0x0C)
#define MPI2_MANPAGE7_PINOUT_SFF_8436                   (0x0D)
#define MPI2_MANPAGE7_PINOUT_SFF_8088_A                 (0x0E)
#define MPI2_MANPAGE7_PINOUT_SFF_8643_16i               (0x0F)
#define MPI2_MANPAGE7_PINOUT_SFF_8654_4i                (0x10)
#define MPI2_MANPAGE7_PINOUT_SFF_8654_8i                (0x11)
#define MPI2_MANPAGE7_PINOUT_SFF_8611_4i                (0x12)
#define MPI2_MANPAGE7_PINOUT_SFF_8611_8i                (0x13)

/*defines for the Location field */
#define MPI2_MANPAGE7_LOCATION_UNKNOWN                  (0x01)
#define MPI2_MANPAGE7_LOCATION_INTERNAL                 (0x02)
#define MPI2_MANPAGE7_LOCATION_EXTERNAL                 (0x04)
#define MPI2_MANPAGE7_LOCATION_SWITCHABLE               (0x08)
#define MPI2_MANPAGE7_LOCATION_AUTO                     (0x10)
#define MPI2_MANPAGE7_LOCATION_NOT_PRESENT              (0x20)
#define MPI2_MANPAGE7_LOCATION_NOT_CONNECTED            (0x80)

/*defines for the Slot field */
#define MPI2_MANPAGE7_SLOT_UNKNOWN                      (0xFFFF)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_MANPAGE7_CONNECTOR_INFO_MAX
#define MPI2_MANPAGE7_CONNECTOR_INFO_MAX  (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_MAN_7 {
	MPI2_CONFIG_PAGE_HEADER         Header;             /*0x00 */
	U32                             Reserved1;          /*0x04 */
	U32                             Reserved2;          /*0x08 */
	U32                             Flags;              /*0x0C */
	U8                              EnclosureName[16];  /*0x10 */
	U8                              NumPhys;            /*0x20 */
	U8                              Reserved3;          /*0x21 */
	U16                             Reserved4;          /*0x22 */
	MPI2_MANPAGE7_CONNECTOR_INFO
	ConnectorInfo[MPI2_MANPAGE7_CONNECTOR_INFO_MAX]; /*0x24 */
} MPI2_CONFIG_PAGE_MAN_7,
	*PTR_MPI2_CONFIG_PAGE_MAN_7,
	Mpi2ManufacturingPage7_t,
	*pMpi2ManufacturingPage7_t;

#define MPI2_MANUFACTURING7_PAGEVERSION                 (0x01)

/*defines for the Flags field */
#define MPI2_MANPAGE7_FLAG_BASE_ENCLOSURE_LEVEL         (0x00000008)
#define MPI2_MANPAGE7_FLAG_EVENTREPLAY_SLOT_ORDER       (0x00000002)
#define MPI2_MANPAGE7_FLAG_USE_SLOT_INFO                (0x00000001)


/*
 *Generic structure to use for product-specific manufacturing pages
 *(currently Manufacturing Page 8 through Manufacturing Page 31).
 */

typedef struct _MPI2_CONFIG_PAGE_MAN_PS {
	MPI2_CONFIG_PAGE_HEADER         Header;             /*0x00 */
	U32                             ProductSpecificInfo;/*0x04 */
} MPI2_CONFIG_PAGE_MAN_PS,
	*PTR_MPI2_CONFIG_PAGE_MAN_PS,
	Mpi2ManufacturingPagePS_t,
	*pMpi2ManufacturingPagePS_t;

#define MPI2_MANUFACTURING8_PAGEVERSION                 (0x00)
#define MPI2_MANUFACTURING9_PAGEVERSION                 (0x00)
#define MPI2_MANUFACTURING10_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING11_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING12_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING13_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING14_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING15_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING16_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING17_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING18_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING19_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING20_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING21_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING22_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING23_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING24_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING25_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING26_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING27_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING28_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING29_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING30_PAGEVERSION                (0x00)
#define MPI2_MANUFACTURING31_PAGEVERSION                (0x00)


/****************************************************************************
*  IO Unit Config Pages
****************************************************************************/

/*IO Unit Page 0 */

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_0 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U64                     UniqueValue;                /*0x04 */
	MPI2_VERSION_UNION      NvdataVersionDefault;       /*0x08 */
	MPI2_VERSION_UNION      NvdataVersionPersistent;    /*0x0A */
} MPI2_CONFIG_PAGE_IO_UNIT_0,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_0,
	Mpi2IOUnitPage0_t, *pMpi2IOUnitPage0_t;

#define MPI2_IOUNITPAGE0_PAGEVERSION                    (0x02)


/*IO Unit Page 1 */

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_1 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U32                     Flags;                      /*0x04 */
} MPI2_CONFIG_PAGE_IO_UNIT_1,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_1,
	Mpi2IOUnitPage1_t, *pMpi2IOUnitPage1_t;

#define MPI2_IOUNITPAGE1_PAGEVERSION                    (0x04)

/* IO Unit Page 1 Flags defines */
#define MPI26_IOUNITPAGE1_NVME_WRCACHE_MASK             (0x00030000)
#define MPI26_IOUNITPAGE1_NVME_WRCACHE_ENABLE           (0x00000000)
#define MPI26_IOUNITPAGE1_NVME_WRCACHE_DISABLE          (0x00010000)
#define MPI26_IOUNITPAGE1_NVME_WRCACHE_NO_CHANGE        (0x00020000)
#define MPI2_IOUNITPAGE1_ATA_SECURITY_FREEZE_LOCK       (0x00004000)
#define MPI25_IOUNITPAGE1_NEW_DEVICE_FAST_PATH_DISABLE  (0x00002000)
#define MPI25_IOUNITPAGE1_DISABLE_FAST_PATH             (0x00001000)
#define MPI2_IOUNITPAGE1_ENABLE_HOST_BASED_DISCOVERY    (0x00000800)
#define MPI2_IOUNITPAGE1_MASK_SATA_WRITE_CACHE          (0x00000600)
#define MPI2_IOUNITPAGE1_SATA_WRITE_CACHE_SHIFT         (9)
#define MPI2_IOUNITPAGE1_ENABLE_SATA_WRITE_CACHE        (0x00000000)
#define MPI2_IOUNITPAGE1_DISABLE_SATA_WRITE_CACHE       (0x00000200)
#define MPI2_IOUNITPAGE1_UNCHANGED_SATA_WRITE_CACHE     (0x00000400)
#define MPI2_IOUNITPAGE1_NATIVE_COMMAND_Q_DISABLE       (0x00000100)
#define MPI2_IOUNITPAGE1_DISABLE_IR                     (0x00000040)
#define MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING (0x00000020)
#define MPI2_IOUNITPAGE1_IR_USE_STATIC_VOLUME_ID        (0x00000004)


/*IO Unit Page 3 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for GPIOCount at runtime.
 */
#ifndef MPI2_IO_UNIT_PAGE_3_GPIO_VAL_MAX
#define MPI2_IO_UNIT_PAGE_3_GPIO_VAL_MAX    (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_3 {
	MPI2_CONFIG_PAGE_HEADER Header;			 /*0x00 */
	U8                      GPIOCount;		 /*0x04 */
	U8                      Reserved1;		 /*0x05 */
	U16                     Reserved2;		 /*0x06 */
	U16
		GPIOVal[MPI2_IO_UNIT_PAGE_3_GPIO_VAL_MAX];/*0x08 */
} MPI2_CONFIG_PAGE_IO_UNIT_3,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_3,
	Mpi2IOUnitPage3_t, *pMpi2IOUnitPage3_t;

#define MPI2_IOUNITPAGE3_PAGEVERSION                    (0x01)

/*defines for IO Unit Page 3 GPIOVal field */
#define MPI2_IOUNITPAGE3_GPIO_FUNCTION_MASK             (0xFFFC)
#define MPI2_IOUNITPAGE3_GPIO_FUNCTION_SHIFT            (2)
#define MPI2_IOUNITPAGE3_GPIO_SETTING_OFF               (0x0000)
#define MPI2_IOUNITPAGE3_GPIO_SETTING_ON                (0x0001)


/*IO Unit Page 5 */

/*
 *Upper layer code (drivers, utilities, etc.) should leave this define set to
 *one and check the value returned for NumDmaEngines at runtime.
 */
#ifndef MPI2_IOUNITPAGE5_DMAENGINE_ENTRIES
#define MPI2_IOUNITPAGE5_DMAENGINE_ENTRIES      (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_5 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U64
		RaidAcceleratorBufferBaseAddress;           /*0x04 */
	U64
		RaidAcceleratorBufferSize;                  /*0x0C */
	U64
		RaidAcceleratorControlBaseAddress;          /*0x14 */
	U8                      RAControlSize;              /*0x1C */
	U8                      NumDmaEngines;              /*0x1D */
	U8                      RAMinControlSize;           /*0x1E */
	U8                      RAMaxControlSize;           /*0x1F */
	U32                     Reserved1;                  /*0x20 */
	U32                     Reserved2;                  /*0x24 */
	U32                     Reserved3;                  /*0x28 */
	U32
	DmaEngineCapabilities[MPI2_IOUNITPAGE5_DMAENGINE_ENTRIES]; /*0x2C */
} MPI2_CONFIG_PAGE_IO_UNIT_5,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_5,
	Mpi2IOUnitPage5_t, *pMpi2IOUnitPage5_t;

#define MPI2_IOUNITPAGE5_PAGEVERSION                    (0x00)

/*defines for IO Unit Page 5 DmaEngineCapabilities field */
#define MPI2_IOUNITPAGE5_DMA_CAP_MASK_MAX_REQUESTS      (0xFFFF0000)
#define MPI2_IOUNITPAGE5_DMA_CAP_SHIFT_MAX_REQUESTS     (16)

#define MPI2_IOUNITPAGE5_DMA_CAP_EEDP                   (0x0008)
#define MPI2_IOUNITPAGE5_DMA_CAP_PARITY_GENERATION      (0x0004)
#define MPI2_IOUNITPAGE5_DMA_CAP_HASHING                (0x0002)
#define MPI2_IOUNITPAGE5_DMA_CAP_ENCRYPTION             (0x0001)


/*IO Unit Page 6 */

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_6 {
	MPI2_CONFIG_PAGE_HEADER Header;                 /*0x00 */
	U16                     Flags;                  /*0x04 */
	U8                      RAHostControlSize;      /*0x06 */
	U8                      Reserved0;              /*0x07 */
	U64
		RaidAcceleratorHostControlBaseAddress;  /*0x08 */
	U32                     Reserved1;              /*0x10 */
	U32                     Reserved2;              /*0x14 */
	U32                     Reserved3;              /*0x18 */
} MPI2_CONFIG_PAGE_IO_UNIT_6,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_6,
	Mpi2IOUnitPage6_t, *pMpi2IOUnitPage6_t;

#define MPI2_IOUNITPAGE6_PAGEVERSION                    (0x00)

/*defines for IO Unit Page 6 Flags field */
#define MPI2_IOUNITPAGE6_FLAGS_ENABLE_RAID_ACCELERATOR  (0x0001)


/*IO Unit Page 7 */

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_7 {
	MPI2_CONFIG_PAGE_HEADER Header;                 /*0x00 */
	U8                      CurrentPowerMode;       /*0x04 */
	U8                      PreviousPowerMode;      /*0x05 */
	U8                      PCIeWidth;              /*0x06 */
	U8                      PCIeSpeed;              /*0x07 */
	U32                     ProcessorState;         /*0x08 */
	U32
		PowerManagementCapabilities;            /*0x0C */
	U16                     IOCTemperature;         /*0x10 */
	U8
		IOCTemperatureUnits;                    /*0x12 */
	U8                      IOCSpeed;               /*0x13 */
	U16                     BoardTemperature;       /*0x14 */
	U8
		BoardTemperatureUnits;                  /*0x16 */
	U8                      Reserved3;              /*0x17 */
	U32			BoardPowerRequirement;	/*0x18 */
	U32			PCISlotPowerAllocation;	/*0x1C */
/* reserved prior to MPI v2.6 */
	U8		Flags;			/* 0x20 */
	U8		Reserved6;			/* 0x21 */
	U16		Reserved7;			/* 0x22 */
	U32		Reserved8;			/* 0x24 */
} MPI2_CONFIG_PAGE_IO_UNIT_7,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_7,
	Mpi2IOUnitPage7_t, *pMpi2IOUnitPage7_t;

#define MPI2_IOUNITPAGE7_PAGEVERSION			(0x05)

/*defines for IO Unit Page 7 CurrentPowerMode and PreviousPowerMode fields */
#define MPI25_IOUNITPAGE7_PM_INIT_MASK              (0xC0)
#define MPI25_IOUNITPAGE7_PM_INIT_UNAVAILABLE       (0x00)
#define MPI25_IOUNITPAGE7_PM_INIT_HOST              (0x40)
#define MPI25_IOUNITPAGE7_PM_INIT_IO_UNIT           (0x80)
#define MPI25_IOUNITPAGE7_PM_INIT_PCIE_DPA          (0xC0)

#define MPI25_IOUNITPAGE7_PM_MODE_MASK              (0x07)
#define MPI25_IOUNITPAGE7_PM_MODE_UNAVAILABLE       (0x00)
#define MPI25_IOUNITPAGE7_PM_MODE_UNKNOWN           (0x01)
#define MPI25_IOUNITPAGE7_PM_MODE_FULL_POWER        (0x04)
#define MPI25_IOUNITPAGE7_PM_MODE_REDUCED_POWER     (0x05)
#define MPI25_IOUNITPAGE7_PM_MODE_STANDBY           (0x06)


/*defines for IO Unit Page 7 PCIeWidth field */
#define MPI2_IOUNITPAGE7_PCIE_WIDTH_X1              (0x01)
#define MPI2_IOUNITPAGE7_PCIE_WIDTH_X2              (0x02)
#define MPI2_IOUNITPAGE7_PCIE_WIDTH_X4              (0x04)
#define MPI2_IOUNITPAGE7_PCIE_WIDTH_X8              (0x08)
#define MPI2_IOUNITPAGE7_PCIE_WIDTH_X16             (0x10)

/*defines for IO Unit Page 7 PCIeSpeed field */
#define MPI2_IOUNITPAGE7_PCIE_SPEED_2_5_GBPS        (0x00)
#define MPI2_IOUNITPAGE7_PCIE_SPEED_5_0_GBPS        (0x01)
#define MPI2_IOUNITPAGE7_PCIE_SPEED_8_0_GBPS        (0x02)
#define MPI2_IOUNITPAGE7_PCIE_SPEED_16_0_GBPS       (0x03)

/*defines for IO Unit Page 7 ProcessorState field */
#define MPI2_IOUNITPAGE7_PSTATE_MASK_SECOND         (0x0000000F)
#define MPI2_IOUNITPAGE7_PSTATE_SHIFT_SECOND        (0)

#define MPI2_IOUNITPAGE7_PSTATE_NOT_PRESENT         (0x00)
#define MPI2_IOUNITPAGE7_PSTATE_DISABLED            (0x01)
#define MPI2_IOUNITPAGE7_PSTATE_ENABLED             (0x02)

/*defines for IO Unit Page 7 PowerManagementCapabilities field */
#define MPI25_IOUNITPAGE7_PMCAP_DPA_FULL_PWR_MODE       (0x00400000)
#define MPI25_IOUNITPAGE7_PMCAP_DPA_REDUCED_PWR_MODE    (0x00200000)
#define MPI25_IOUNITPAGE7_PMCAP_DPA_STANDBY_MODE        (0x00100000)
#define MPI25_IOUNITPAGE7_PMCAP_HOST_FULL_PWR_MODE      (0x00040000)
#define MPI25_IOUNITPAGE7_PMCAP_HOST_REDUCED_PWR_MODE   (0x00020000)
#define MPI25_IOUNITPAGE7_PMCAP_HOST_STANDBY_MODE       (0x00010000)
#define MPI25_IOUNITPAGE7_PMCAP_IO_FULL_PWR_MODE        (0x00004000)
#define MPI25_IOUNITPAGE7_PMCAP_IO_REDUCED_PWR_MODE     (0x00002000)
#define MPI25_IOUNITPAGE7_PMCAP_IO_STANDBY_MODE         (0x00001000)
#define MPI2_IOUNITPAGE7_PMCAP_HOST_12_5_PCT_IOCSPEED   (0x00000400)
#define MPI2_IOUNITPAGE7_PMCAP_HOST_25_0_PCT_IOCSPEED   (0x00000200)
#define MPI2_IOUNITPAGE7_PMCAP_HOST_50_0_PCT_IOCSPEED   (0x00000100)
#define MPI25_IOUNITPAGE7_PMCAP_IO_12_5_PCT_IOCSPEED    (0x00000040)
#define MPI25_IOUNITPAGE7_PMCAP_IO_25_0_PCT_IOCSPEED    (0x00000020)
#define MPI25_IOUNITPAGE7_PMCAP_IO_50_0_PCT_IOCSPEED    (0x00000010)
#define MPI2_IOUNITPAGE7_PMCAP_HOST_WIDTH_CHANGE_PCIE   (0x00000008)
#define MPI2_IOUNITPAGE7_PMCAP_HOST_SPEED_CHANGE_PCIE   (0x00000004)
#define MPI25_IOUNITPAGE7_PMCAP_IO_WIDTH_CHANGE_PCIE    (0x00000002)
#define MPI25_IOUNITPAGE7_PMCAP_IO_SPEED_CHANGE_PCIE    (0x00000001)

/*obsolete names for the PowerManagementCapabilities bits (above) */
#define MPI2_IOUNITPAGE7_PMCAP_12_5_PCT_IOCSPEED    (0x00000400)
#define MPI2_IOUNITPAGE7_PMCAP_25_0_PCT_IOCSPEED    (0x00000200)
#define MPI2_IOUNITPAGE7_PMCAP_50_0_PCT_IOCSPEED    (0x00000100)
#define MPI2_IOUNITPAGE7_PMCAP_PCIE_WIDTH_CHANGE    (0x00000008) /*obsolete */
#define MPI2_IOUNITPAGE7_PMCAP_PCIE_SPEED_CHANGE    (0x00000004) /*obsolete */


/*defines for IO Unit Page 7 IOCTemperatureUnits field */
#define MPI2_IOUNITPAGE7_IOC_TEMP_NOT_PRESENT       (0x00)
#define MPI2_IOUNITPAGE7_IOC_TEMP_FAHRENHEIT        (0x01)
#define MPI2_IOUNITPAGE7_IOC_TEMP_CELSIUS           (0x02)

/*defines for IO Unit Page 7 IOCSpeed field */
#define MPI2_IOUNITPAGE7_IOC_SPEED_FULL             (0x01)
#define MPI2_IOUNITPAGE7_IOC_SPEED_HALF             (0x02)
#define MPI2_IOUNITPAGE7_IOC_SPEED_QUARTER          (0x04)
#define MPI2_IOUNITPAGE7_IOC_SPEED_EIGHTH           (0x08)

/*defines for IO Unit Page 7 BoardTemperatureUnits field */
#define MPI2_IOUNITPAGE7_BOARD_TEMP_NOT_PRESENT     (0x00)
#define MPI2_IOUNITPAGE7_BOARD_TEMP_FAHRENHEIT      (0x01)
#define MPI2_IOUNITPAGE7_BOARD_TEMP_CELSIUS         (0x02)

/* defines for IO Unit Page 7 Flags field */
#define MPI2_IOUNITPAGE7_FLAG_CABLE_POWER_EXC       (0x01)

/*IO Unit Page 8 */

#define MPI2_IOUNIT8_NUM_THRESHOLDS     (4)

typedef struct _MPI2_IOUNIT8_SENSOR {
	U16                     Flags;                  /*0x00 */
	U16                     Reserved1;              /*0x02 */
	U16
		Threshold[MPI2_IOUNIT8_NUM_THRESHOLDS]; /*0x04 */
	U32                     Reserved2;              /*0x0C */
	U32                     Reserved3;              /*0x10 */
	U32                     Reserved4;              /*0x14 */
} MPI2_IOUNIT8_SENSOR, *PTR_MPI2_IOUNIT8_SENSOR,
	Mpi2IOUnit8Sensor_t, *pMpi2IOUnit8Sensor_t;

/*defines for IO Unit Page 8 Sensor Flags field */
#define MPI2_IOUNIT8_SENSOR_FLAGS_T3_ENABLE         (0x0008)
#define MPI2_IOUNIT8_SENSOR_FLAGS_T2_ENABLE         (0x0004)
#define MPI2_IOUNIT8_SENSOR_FLAGS_T1_ENABLE         (0x0002)
#define MPI2_IOUNIT8_SENSOR_FLAGS_T0_ENABLE         (0x0001)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumSensors at runtime.
 */
#ifndef MPI2_IOUNITPAGE8_SENSOR_ENTRIES
#define MPI2_IOUNITPAGE8_SENSOR_ENTRIES     (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_8 {
	MPI2_CONFIG_PAGE_HEADER Header;                 /*0x00 */
	U32                     Reserved1;              /*0x04 */
	U32                     Reserved2;              /*0x08 */
	U8                      NumSensors;             /*0x0C */
	U8                      PollingInterval;        /*0x0D */
	U16                     Reserved3;              /*0x0E */
	MPI2_IOUNIT8_SENSOR
		Sensor[MPI2_IOUNITPAGE8_SENSOR_ENTRIES];/*0x10 */
} MPI2_CONFIG_PAGE_IO_UNIT_8,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_8,
	Mpi2IOUnitPage8_t, *pMpi2IOUnitPage8_t;

#define MPI2_IOUNITPAGE8_PAGEVERSION                    (0x00)


/*IO Unit Page 9 */

typedef struct _MPI2_IOUNIT9_SENSOR {
	U16                     CurrentTemperature;     /*0x00 */
	U16                     Reserved1;              /*0x02 */
	U8                      Flags;                  /*0x04 */
	U8                      Reserved2;              /*0x05 */
	U16                     Reserved3;              /*0x06 */
	U32                     Reserved4;              /*0x08 */
	U32                     Reserved5;              /*0x0C */
} MPI2_IOUNIT9_SENSOR, *PTR_MPI2_IOUNIT9_SENSOR,
	Mpi2IOUnit9Sensor_t, *pMpi2IOUnit9Sensor_t;

/*defines for IO Unit Page 9 Sensor Flags field */
#define MPI2_IOUNIT9_SENSOR_FLAGS_TEMP_VALID        (0x01)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumSensors at runtime.
 */
#ifndef MPI2_IOUNITPAGE9_SENSOR_ENTRIES
#define MPI2_IOUNITPAGE9_SENSOR_ENTRIES     (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_9 {
	MPI2_CONFIG_PAGE_HEADER Header;                 /*0x00 */
	U32                     Reserved1;              /*0x04 */
	U32                     Reserved2;              /*0x08 */
	U8                      NumSensors;             /*0x0C */
	U8                      Reserved4;              /*0x0D */
	U16                     Reserved3;              /*0x0E */
	MPI2_IOUNIT9_SENSOR
		Sensor[MPI2_IOUNITPAGE9_SENSOR_ENTRIES];/*0x10 */
} MPI2_CONFIG_PAGE_IO_UNIT_9,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_9,
	Mpi2IOUnitPage9_t, *pMpi2IOUnitPage9_t;

#define MPI2_IOUNITPAGE9_PAGEVERSION                    (0x00)


/*IO Unit Page 10 */

typedef struct _MPI2_IOUNIT10_FUNCTION {
	U8                      CreditPercent;      /*0x00 */
	U8                      Reserved1;          /*0x01 */
	U16                     Reserved2;          /*0x02 */
} MPI2_IOUNIT10_FUNCTION,
	*PTR_MPI2_IOUNIT10_FUNCTION,
	Mpi2IOUnit10Function_t,
	*pMpi2IOUnit10Function_t;

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumFunctions at runtime.
 */
#ifndef MPI2_IOUNITPAGE10_FUNCTION_ENTRIES
#define MPI2_IOUNITPAGE10_FUNCTION_ENTRIES      (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_IO_UNIT_10 {
	MPI2_CONFIG_PAGE_HEADER Header;                      /*0x00 */
	U8                      NumFunctions;                /*0x04 */
	U8                      Reserved1;                   /*0x05 */
	U16                     Reserved2;                   /*0x06 */
	U32                     Reserved3;                   /*0x08 */
	U32                     Reserved4;                   /*0x0C */
	MPI2_IOUNIT10_FUNCTION
		Function[MPI2_IOUNITPAGE10_FUNCTION_ENTRIES];/*0x10 */
} MPI2_CONFIG_PAGE_IO_UNIT_10,
	*PTR_MPI2_CONFIG_PAGE_IO_UNIT_10,
	Mpi2IOUnitPage10_t, *pMpi2IOUnitPage10_t;

#define MPI2_IOUNITPAGE10_PAGEVERSION                   (0x01)


/* IO Unit Page 11 (for MPI v2.6 and later) */

typedef struct _MPI26_IOUNIT11_SPINUP_GROUP {
	U8          MaxTargetSpinup;            /* 0x00 */
	U8          SpinupDelay;                /* 0x01 */
	U8          SpinupFlags;                /* 0x02 */
	U8          Reserved1;                  /* 0x03 */
} MPI26_IOUNIT11_SPINUP_GROUP,
	*PTR_MPI26_IOUNIT11_SPINUP_GROUP,
	Mpi26IOUnit11SpinupGroup_t,
	*pMpi26IOUnit11SpinupGroup_t;

/* defines for IO Unit Page 11 SpinupFlags */
#define MPI26_IOUNITPAGE11_SPINUP_DISABLE_FLAG          (0x01)


/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * four and check the value returned for NumPhys at runtime.
 */
#ifndef MPI26_IOUNITPAGE11_PHY_MAX
#define MPI26_IOUNITPAGE11_PHY_MAX        (4)
#endif

typedef struct _MPI26_CONFIG_PAGE_IO_UNIT_11 {
	MPI2_CONFIG_PAGE_HEADER       Header;			       /*0x00 */
	U32                           Reserved1;                      /*0x04 */
	MPI26_IOUNIT11_SPINUP_GROUP   SpinupGroupParameters[4];       /*0x08 */
	U32                           Reserved2;                      /*0x18 */
	U32                           Reserved3;                      /*0x1C */
	U32                           Reserved4;                      /*0x20 */
	U8                            BootDeviceWaitTime;             /*0x24 */
	U8                            Reserved5;                      /*0x25 */
	U16                           Reserved6;                      /*0x26 */
	U8                            NumPhys;                        /*0x28 */
	U8                            PEInitialSpinupDelay;           /*0x29 */
	U8                            PEReplyDelay;                   /*0x2A */
	U8                            Flags;                          /*0x2B */
	U8			      PHY[MPI26_IOUNITPAGE11_PHY_MAX];/*0x2C */
} MPI26_CONFIG_PAGE_IO_UNIT_11,
	*PTR_MPI26_CONFIG_PAGE_IO_UNIT_11,
	Mpi26IOUnitPage11_t,
	*pMpi26IOUnitPage11_t;

#define MPI26_IOUNITPAGE11_PAGEVERSION                  (0x00)

/* defines for Flags field */
#define MPI26_IOUNITPAGE11_FLAGS_AUTO_PORTENABLE        (0x01)

/* defines for PHY field */
#define MPI26_IOUNITPAGE11_PHY_SPINUP_GROUP_MASK        (0x03)






/****************************************************************************
*  IOC Config Pages
****************************************************************************/

/*IOC Page 0 */

typedef struct _MPI2_CONFIG_PAGE_IOC_0 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U32                     Reserved1;                  /*0x04 */
	U32                     Reserved2;                  /*0x08 */
	U16                     VendorID;                   /*0x0C */
	U16                     DeviceID;                   /*0x0E */
	U8                      RevisionID;                 /*0x10 */
	U8                      Reserved3;                  /*0x11 */
	U16                     Reserved4;                  /*0x12 */
	U32                     ClassCode;                  /*0x14 */
	U16                     SubsystemVendorID;          /*0x18 */
	U16                     SubsystemID;                /*0x1A */
} MPI2_CONFIG_PAGE_IOC_0,
	*PTR_MPI2_CONFIG_PAGE_IOC_0,
	Mpi2IOCPage0_t, *pMpi2IOCPage0_t;

#define MPI2_IOCPAGE0_PAGEVERSION                       (0x02)


/*IOC Page 1 */

typedef struct _MPI2_CONFIG_PAGE_IOC_1 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U32                     Flags;                      /*0x04 */
	U32                     CoalescingTimeout;          /*0x08 */
	U8                      CoalescingDepth;            /*0x0C */
	U8                      PCISlotNum;                 /*0x0D */
	U8                      PCIBusNum;                  /*0x0E */
	U8                      PCIDomainSegment;           /*0x0F */
	U32                     Reserved1;                  /*0x10 */
	U32                     ProductSpecific;            /* 0x14 */
} MPI2_CONFIG_PAGE_IOC_1,
	*PTR_MPI2_CONFIG_PAGE_IOC_1,
	Mpi2IOCPage1_t, *pMpi2IOCPage1_t;

#define MPI2_IOCPAGE1_PAGEVERSION                       (0x05)

/*defines for IOC Page 1 Flags field */
#define MPI2_IOCPAGE1_REPLY_COALESCING                  (0x00000001)

#define MPI2_IOCPAGE1_PCISLOTNUM_UNKNOWN                (0xFF)
#define MPI2_IOCPAGE1_PCIBUSNUM_UNKNOWN                 (0xFF)
#define MPI2_IOCPAGE1_PCIDOMAIN_UNKNOWN                 (0xFF)

/*IOC Page 6 */

typedef struct _MPI2_CONFIG_PAGE_IOC_6 {
	MPI2_CONFIG_PAGE_HEADER Header;         /*0x00 */
	U32
		CapabilitiesFlags;              /*0x04 */
	U8                      MaxDrivesRAID0; /*0x08 */
	U8                      MaxDrivesRAID1; /*0x09 */
	U8
		 MaxDrivesRAID1E;                /*0x0A */
	U8
		 MaxDrivesRAID10;		/*0x0B */
	U8                      MinDrivesRAID0; /*0x0C */
	U8                      MinDrivesRAID1; /*0x0D */
	U8
		 MinDrivesRAID1E;                /*0x0E */
	U8
		 MinDrivesRAID10;                /*0x0F */
	U32                     Reserved1;      /*0x10 */
	U8
		 MaxGlobalHotSpares;             /*0x14 */
	U8                      MaxPhysDisks;   /*0x15 */
	U8                      MaxVolumes;     /*0x16 */
	U8                      MaxConfigs;     /*0x17 */
	U8                      MaxOCEDisks;    /*0x18 */
	U8                      Reserved2;      /*0x19 */
	U16                     Reserved3;      /*0x1A */
	U32
		SupportedStripeSizeMapRAID0;    /*0x1C */
	U32
		SupportedStripeSizeMapRAID1E;   /*0x20 */
	U32
		SupportedStripeSizeMapRAID10;   /*0x24 */
	U32                     Reserved4;      /*0x28 */
	U32                     Reserved5;      /*0x2C */
	U16
		DefaultMetadataSize;            /*0x30 */
	U16                     Reserved6;      /*0x32 */
	U16
		MaxBadBlockTableEntries;        /*0x34 */
	U16                     Reserved7;      /*0x36 */
	U32
		IRNvsramVersion;                /*0x38 */
} MPI2_CONFIG_PAGE_IOC_6,
	*PTR_MPI2_CONFIG_PAGE_IOC_6,
	Mpi2IOCPage6_t, *pMpi2IOCPage6_t;

#define MPI2_IOCPAGE6_PAGEVERSION                       (0x05)

/*defines for IOC Page 6 CapabilitiesFlags */
#define MPI2_IOCPAGE6_CAP_FLAGS_4K_SECTORS_SUPPORT      (0x00000020)
#define MPI2_IOCPAGE6_CAP_FLAGS_RAID10_SUPPORT          (0x00000010)
#define MPI2_IOCPAGE6_CAP_FLAGS_RAID1_SUPPORT           (0x00000008)
#define MPI2_IOCPAGE6_CAP_FLAGS_RAID1E_SUPPORT          (0x00000004)
#define MPI2_IOCPAGE6_CAP_FLAGS_RAID0_SUPPORT           (0x00000002)
#define MPI2_IOCPAGE6_CAP_FLAGS_GLOBAL_HOT_SPARE        (0x00000001)


/*IOC Page 7 */

#define MPI2_IOCPAGE7_EVENTMASK_WORDS       (4)

typedef struct _MPI2_CONFIG_PAGE_IOC_7 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U32                     Reserved1;                  /*0x04 */
	U32
		EventMasks[MPI2_IOCPAGE7_EVENTMASK_WORDS];/*0x08 */
	U16                     SASBroadcastPrimitiveMasks; /*0x18 */
	U16                     SASNotifyPrimitiveMasks;    /*0x1A */
	U32                     Reserved3;                  /*0x1C */
} MPI2_CONFIG_PAGE_IOC_7,
	*PTR_MPI2_CONFIG_PAGE_IOC_7,
	Mpi2IOCPage7_t, *pMpi2IOCPage7_t;

#define MPI2_IOCPAGE7_PAGEVERSION                       (0x02)


/*IOC Page 8 */

typedef struct _MPI2_CONFIG_PAGE_IOC_8 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U8                      NumDevsPerEnclosure;        /*0x04 */
	U8                      Reserved1;                  /*0x05 */
	U16                     Reserved2;                  /*0x06 */
	U16                     MaxPersistentEntries;       /*0x08 */
	U16                     MaxNumPhysicalMappedIDs;    /*0x0A */
	U16                     Flags;                      /*0x0C */
	U16                     Reserved3;                  /*0x0E */
	U16                     IRVolumeMappingFlags;       /*0x10 */
	U16                     Reserved4;                  /*0x12 */
	U32                     Reserved5;                  /*0x14 */
} MPI2_CONFIG_PAGE_IOC_8,
	*PTR_MPI2_CONFIG_PAGE_IOC_8,
	Mpi2IOCPage8_t, *pMpi2IOCPage8_t;

#define MPI2_IOCPAGE8_PAGEVERSION                       (0x00)

/*defines for IOC Page 8 Flags field */
#define MPI2_IOCPAGE8_FLAGS_DA_START_SLOT_1             (0x00000020)
#define MPI2_IOCPAGE8_FLAGS_RESERVED_TARGETID_0         (0x00000010)

#define MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE           (0x0000000E)
#define MPI2_IOCPAGE8_FLAGS_DEVICE_PERSISTENCE_MAPPING  (0x00000000)
#define MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING      (0x00000002)

#define MPI2_IOCPAGE8_FLAGS_DISABLE_PERSISTENT_MAPPING  (0x00000001)
#define MPI2_IOCPAGE8_FLAGS_ENABLE_PERSISTENT_MAPPING   (0x00000000)

/*defines for IOC Page 8 IRVolumeMappingFlags */
#define MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE  (0x00000003)
#define MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING        (0x00000000)
#define MPI2_IOCPAGE8_IRFLAGS_HIGH_VOLUME_MAPPING       (0x00000001)


/****************************************************************************
*  BIOS Config Pages
****************************************************************************/

/*BIOS Page 1 */

typedef struct _MPI2_CONFIG_PAGE_BIOS_1 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U32                     BiosOptions;                /*0x04 */
	U32                     IOCSettings;                /*0x08 */
	U8                      SSUTimeout;                 /*0x0C */
	U8                      MaxEnclosureLevel;          /*0x0D */
	U16                     Reserved2;                  /*0x0E */
	U32                     DeviceSettings;             /*0x10 */
	U16                     NumberOfDevices;            /*0x14 */
	U16                     UEFIVersion;                /*0x16 */
	U16                     IOTimeoutBlockDevicesNonRM; /*0x18 */
	U16                     IOTimeoutSequential;        /*0x1A */
	U16                     IOTimeoutOther;             /*0x1C */
	U16                     IOTimeoutBlockDevicesRM;    /*0x1E */
} MPI2_CONFIG_PAGE_BIOS_1,
	*PTR_MPI2_CONFIG_PAGE_BIOS_1,
	Mpi2BiosPage1_t, *pMpi2BiosPage1_t;

#define MPI2_BIOSPAGE1_PAGEVERSION                      (0x07)

/*values for BIOS Page 1 BiosOptions field */
#define MPI2_BIOSPAGE1_OPTIONS_BOOT_LIST_ADD_ALT_BOOT_DEVICE    (0x00008000)
#define MPI2_BIOSPAGE1_OPTIONS_ADVANCED_CONFIG                  (0x00004000)

#define MPI2_BIOSPAGE1_OPTIONS_PNS_MASK                         (0x00003800)
#define MPI2_BIOSPAGE1_OPTIONS_PNS_PBDHL                        (0x00000000)
#define MPI2_BIOSPAGE1_OPTIONS_PNS_ENCSLOSURE                   (0x00000800)
#define MPI2_BIOSPAGE1_OPTIONS_PNS_LWWID                        (0x00001000)
#define MPI2_BIOSPAGE1_OPTIONS_PNS_PSENS                        (0x00001800)
#define MPI2_BIOSPAGE1_OPTIONS_PNS_ESPHY                        (0x00002000)

#define MPI2_BIOSPAGE1_OPTIONS_X86_DISABLE_BIOS		(0x00000400)

#define MPI2_BIOSPAGE1_OPTIONS_MASK_REGISTRATION_UEFI_BSD	(0x00000300)
#define MPI2_BIOSPAGE1_OPTIONS_USE_BIT0_REGISTRATION_UEFI_BSD	(0x00000000)
#define MPI2_BIOSPAGE1_OPTIONS_FULL_REGISTRATION_UEFI_BSD	(0x00000100)
#define MPI2_BIOSPAGE1_OPTIONS_ADAPTER_REGISTRATION_UEFI_BSD	(0x00000200)
#define MPI2_BIOSPAGE1_OPTIONS_DISABLE_REGISTRATION_UEFI_BSD	(0x00000300)

#define MPI2_BIOSPAGE1_OPTIONS_MASK_OEM_ID                  (0x000000F0)
#define MPI2_BIOSPAGE1_OPTIONS_LSI_OEM_ID                   (0x00000000)

#define MPI2_BIOSPAGE1_OPTIONS_MASK_UEFI_HII_REGISTRATION   (0x00000006)
#define MPI2_BIOSPAGE1_OPTIONS_ENABLE_UEFI_HII              (0x00000000)
#define MPI2_BIOSPAGE1_OPTIONS_DISABLE_UEFI_HII             (0x00000002)
#define MPI2_BIOSPAGE1_OPTIONS_VERSION_CHECK_UEFI_HII       (0x00000004)

#define MPI2_BIOSPAGE1_OPTIONS_DISABLE_BIOS                 (0x00000001)

/*values for BIOS Page 1 IOCSettings field */
#define MPI2_BIOSPAGE1_IOCSET_MASK_BOOT_PREFERENCE      (0x00030000)
#define MPI2_BIOSPAGE1_IOCSET_ENCLOSURE_SLOT_BOOT       (0x00000000)
#define MPI2_BIOSPAGE1_IOCSET_SAS_ADDRESS_BOOT          (0x00010000)

#define MPI2_BIOSPAGE1_IOCSET_MASK_RM_SETTING           (0x000000C0)
#define MPI2_BIOSPAGE1_IOCSET_NONE_RM_SETTING           (0x00000000)
#define MPI2_BIOSPAGE1_IOCSET_BOOT_RM_SETTING           (0x00000040)
#define MPI2_BIOSPAGE1_IOCSET_MEDIA_RM_SETTING          (0x00000080)

#define MPI2_BIOSPAGE1_IOCSET_MASK_ADAPTER_SUPPORT      (0x00000030)
#define MPI2_BIOSPAGE1_IOCSET_NO_SUPPORT                (0x00000000)
#define MPI2_BIOSPAGE1_IOCSET_BIOS_SUPPORT              (0x00000010)
#define MPI2_BIOSPAGE1_IOCSET_OS_SUPPORT                (0x00000020)
#define MPI2_BIOSPAGE1_IOCSET_ALL_SUPPORT               (0x00000030)

#define MPI2_BIOSPAGE1_IOCSET_ALTERNATE_CHS             (0x00000008)

/*values for BIOS Page 1 DeviceSettings field */
#define MPI2_BIOSPAGE1_DEVSET_DISABLE_SMART_POLLING     (0x00000010)
#define MPI2_BIOSPAGE1_DEVSET_DISABLE_SEQ_LUN           (0x00000008)
#define MPI2_BIOSPAGE1_DEVSET_DISABLE_RM_LUN            (0x00000004)
#define MPI2_BIOSPAGE1_DEVSET_DISABLE_NON_RM_LUN        (0x00000002)
#define MPI2_BIOSPAGE1_DEVSET_DISABLE_OTHER_LUN         (0x00000001)

/*defines for BIOS Page 1 UEFIVersion field */
#define MPI2_BIOSPAGE1_UEFI_VER_MAJOR_MASK              (0xFF00)
#define MPI2_BIOSPAGE1_UEFI_VER_MAJOR_SHIFT             (8)
#define MPI2_BIOSPAGE1_UEFI_VER_MINOR_MASK              (0x00FF)
#define MPI2_BIOSPAGE1_UEFI_VER_MINOR_SHIFT             (0)



/*BIOS Page 2 */

typedef struct _MPI2_BOOT_DEVICE_ADAPTER_ORDER {
	U32         Reserved1;                              /*0x00 */
	U32         Reserved2;                              /*0x04 */
	U32         Reserved3;                              /*0x08 */
	U32         Reserved4;                              /*0x0C */
	U32         Reserved5;                              /*0x10 */
	U32         Reserved6;                              /*0x14 */
} MPI2_BOOT_DEVICE_ADAPTER_ORDER,
	*PTR_MPI2_BOOT_DEVICE_ADAPTER_ORDER,
	Mpi2BootDeviceAdapterOrder_t,
	*pMpi2BootDeviceAdapterOrder_t;

typedef struct _MPI2_BOOT_DEVICE_SAS_WWID {
	U64         SASAddress;                             /*0x00 */
	U8          LUN[8];                                 /*0x08 */
	U32         Reserved1;                              /*0x10 */
	U32         Reserved2;                              /*0x14 */
} MPI2_BOOT_DEVICE_SAS_WWID,
	*PTR_MPI2_BOOT_DEVICE_SAS_WWID,
	Mpi2BootDeviceSasWwid_t,
	*pMpi2BootDeviceSasWwid_t;

typedef struct _MPI2_BOOT_DEVICE_ENCLOSURE_SLOT {
	U64         EnclosureLogicalID;                     /*0x00 */
	U32         Reserved1;                              /*0x08 */
	U32         Reserved2;                              /*0x0C */
	U16         SlotNumber;                             /*0x10 */
	U16         Reserved3;                              /*0x12 */
	U32         Reserved4;                              /*0x14 */
} MPI2_BOOT_DEVICE_ENCLOSURE_SLOT,
	*PTR_MPI2_BOOT_DEVICE_ENCLOSURE_SLOT,
	Mpi2BootDeviceEnclosureSlot_t,
	*pMpi2BootDeviceEnclosureSlot_t;

typedef struct _MPI2_BOOT_DEVICE_DEVICE_NAME {
	U64         DeviceName;                             /*0x00 */
	U8          LUN[8];                                 /*0x08 */
	U32         Reserved1;                              /*0x10 */
	U32         Reserved2;                              /*0x14 */
} MPI2_BOOT_DEVICE_DEVICE_NAME,
	*PTR_MPI2_BOOT_DEVICE_DEVICE_NAME,
	Mpi2BootDeviceDeviceName_t,
	*pMpi2BootDeviceDeviceName_t;

typedef union _MPI2_MPI2_BIOSPAGE2_BOOT_DEVICE {
	MPI2_BOOT_DEVICE_ADAPTER_ORDER  AdapterOrder;
	MPI2_BOOT_DEVICE_SAS_WWID       SasWwid;
	MPI2_BOOT_DEVICE_ENCLOSURE_SLOT EnclosureSlot;
	MPI2_BOOT_DEVICE_DEVICE_NAME    DeviceName;
} MPI2_BIOSPAGE2_BOOT_DEVICE,
	*PTR_MPI2_BIOSPAGE2_BOOT_DEVICE,
	Mpi2BiosPage2BootDevice_t,
	*pMpi2BiosPage2BootDevice_t;

typedef struct _MPI2_CONFIG_PAGE_BIOS_2 {
	MPI2_CONFIG_PAGE_HEADER     Header;                 /*0x00 */
	U32                         Reserved1;              /*0x04 */
	U32                         Reserved2;              /*0x08 */
	U32                         Reserved3;              /*0x0C */
	U32                         Reserved4;              /*0x10 */
	U32                         Reserved5;              /*0x14 */
	U32                         Reserved6;              /*0x18 */
	U8                          ReqBootDeviceForm;      /*0x1C */
	U8                          Reserved7;              /*0x1D */
	U16                         Reserved8;              /*0x1E */
	MPI2_BIOSPAGE2_BOOT_DEVICE  RequestedBootDevice;    /*0x20 */
	U8                          ReqAltBootDeviceForm;   /*0x38 */
	U8                          Reserved9;              /*0x39 */
	U16                         Reserved10;             /*0x3A */
	MPI2_BIOSPAGE2_BOOT_DEVICE  RequestedAltBootDevice; /*0x3C */
	U8                          CurrentBootDeviceForm;  /*0x58 */
	U8                          Reserved11;             /*0x59 */
	U16                         Reserved12;             /*0x5A */
	MPI2_BIOSPAGE2_BOOT_DEVICE  CurrentBootDevice;      /*0x58 */
} MPI2_CONFIG_PAGE_BIOS_2, *PTR_MPI2_CONFIG_PAGE_BIOS_2,
	Mpi2BiosPage2_t, *pMpi2BiosPage2_t;

#define MPI2_BIOSPAGE2_PAGEVERSION                      (0x04)

/*values for BIOS Page 2 BootDeviceForm fields */
#define MPI2_BIOSPAGE2_FORM_MASK                        (0x0F)
#define MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED         (0x00)
#define MPI2_BIOSPAGE2_FORM_SAS_WWID                    (0x05)
#define MPI2_BIOSPAGE2_FORM_ENCLOSURE_SLOT              (0x06)
#define MPI2_BIOSPAGE2_FORM_DEVICE_NAME                 (0x07)


/*BIOS Page 3 */

#define MPI2_BIOSPAGE3_NUM_ADAPTER      (4)

typedef struct _MPI2_ADAPTER_INFO {
	U8      PciBusNumber;                        /*0x00 */
	U8      PciDeviceAndFunctionNumber;          /*0x01 */
	U16     AdapterFlags;                        /*0x02 */
} MPI2_ADAPTER_INFO, *PTR_MPI2_ADAPTER_INFO,
	Mpi2AdapterInfo_t, *pMpi2AdapterInfo_t;

#define MPI2_ADAPTER_INFO_FLAGS_EMBEDDED                (0x0001)
#define MPI2_ADAPTER_INFO_FLAGS_INIT_STATUS             (0x0002)

typedef struct _MPI2_ADAPTER_ORDER_AUX {
	U64     WWID;					/* 0x00 */
	U32     Reserved1;				/* 0x08 */
	U32     Reserved2;				/* 0x0C */
} MPI2_ADAPTER_ORDER_AUX, *PTR_MPI2_ADAPTER_ORDER_AUX,
	Mpi2AdapterOrderAux_t, *pMpi2AdapterOrderAux_t;


typedef struct _MPI2_CONFIG_PAGE_BIOS_3 {
	MPI2_CONFIG_PAGE_HEADER Header;              /*0x00 */
	U32                     GlobalFlags;         /*0x04 */
	U32                     BiosVersion;         /*0x08 */
	MPI2_ADAPTER_INFO       AdapterOrder[MPI2_BIOSPAGE3_NUM_ADAPTER];
	U32                     Reserved1;           /*0x1C */
	MPI2_ADAPTER_ORDER_AUX  AdapterOrderAux[MPI2_BIOSPAGE3_NUM_ADAPTER];
} MPI2_CONFIG_PAGE_BIOS_3,
	*PTR_MPI2_CONFIG_PAGE_BIOS_3,
	Mpi2BiosPage3_t, *pMpi2BiosPage3_t;

#define MPI2_BIOSPAGE3_PAGEVERSION                      (0x01)

/*values for BIOS Page 3 GlobalFlags */
#define MPI2_BIOSPAGE3_FLAGS_PAUSE_ON_ERROR             (0x00000002)
#define MPI2_BIOSPAGE3_FLAGS_VERBOSE_ENABLE             (0x00000004)
#define MPI2_BIOSPAGE3_FLAGS_HOOK_INT_40_DISABLE        (0x00000010)

#define MPI2_BIOSPAGE3_FLAGS_DEV_LIST_DISPLAY_MASK      (0x000000E0)
#define MPI2_BIOSPAGE3_FLAGS_INSTALLED_DEV_DISPLAY      (0x00000000)
#define MPI2_BIOSPAGE3_FLAGS_ADAPTER_DISPLAY            (0x00000020)
#define MPI2_BIOSPAGE3_FLAGS_ADAPTER_DEV_DISPLAY        (0x00000040)


/*BIOS Page 4 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_BIOS_PAGE_4_PHY_ENTRIES
#define MPI2_BIOS_PAGE_4_PHY_ENTRIES        (1)
#endif

typedef struct _MPI2_BIOS4_ENTRY {
	U64                     ReassignmentWWID;       /*0x00 */
	U64                     ReassignmentDeviceName; /*0x08 */
} MPI2_BIOS4_ENTRY, *PTR_MPI2_BIOS4_ENTRY,
	Mpi2MBios4Entry_t, *pMpi2Bios4Entry_t;

typedef struct _MPI2_CONFIG_PAGE_BIOS_4 {
	MPI2_CONFIG_PAGE_HEADER Header;             /*0x00 */
	U8                      NumPhys;            /*0x04 */
	U8                      Reserved1;          /*0x05 */
	U16                     Reserved2;          /*0x06 */
	MPI2_BIOS4_ENTRY
		Phy[MPI2_BIOS_PAGE_4_PHY_ENTRIES];  /*0x08 */
} MPI2_CONFIG_PAGE_BIOS_4, *PTR_MPI2_CONFIG_PAGE_BIOS_4,
	Mpi2BiosPage4_t, *pMpi2BiosPage4_t;

#define MPI2_BIOSPAGE4_PAGEVERSION                      (0x01)


/****************************************************************************
*  RAID Volume Config Pages
****************************************************************************/

/*RAID Volume Page 0 */

typedef struct _MPI2_RAIDVOL0_PHYS_DISK {
	U8                      RAIDSetNum;        /*0x00 */
	U8                      PhysDiskMap;       /*0x01 */
	U8                      PhysDiskNum;       /*0x02 */
	U8                      Reserved;          /*0x03 */
} MPI2_RAIDVOL0_PHYS_DISK, *PTR_MPI2_RAIDVOL0_PHYS_DISK,
	Mpi2RaidVol0PhysDisk_t, *pMpi2RaidVol0PhysDisk_t;

/*defines for the PhysDiskMap field */
#define MPI2_RAIDVOL0_PHYSDISK_PRIMARY                  (0x01)
#define MPI2_RAIDVOL0_PHYSDISK_SECONDARY                (0x02)

typedef struct _MPI2_RAIDVOL0_SETTINGS {
	U16                     Settings;          /*0x00 */
	U8                      HotSparePool;      /*0x01 */
	U8                      Reserved;          /*0x02 */
} MPI2_RAIDVOL0_SETTINGS, *PTR_MPI2_RAIDVOL0_SETTINGS,
	Mpi2RaidVol0Settings_t,
	*pMpi2RaidVol0Settings_t;

/*RAID Volume Page 0 HotSparePool defines, also used in RAID Physical Disk */
#define MPI2_RAID_HOT_SPARE_POOL_0                      (0x01)
#define MPI2_RAID_HOT_SPARE_POOL_1                      (0x02)
#define MPI2_RAID_HOT_SPARE_POOL_2                      (0x04)
#define MPI2_RAID_HOT_SPARE_POOL_3                      (0x08)
#define MPI2_RAID_HOT_SPARE_POOL_4                      (0x10)
#define MPI2_RAID_HOT_SPARE_POOL_5                      (0x20)
#define MPI2_RAID_HOT_SPARE_POOL_6                      (0x40)
#define MPI2_RAID_HOT_SPARE_POOL_7                      (0x80)

/*RAID Volume Page 0 VolumeSettings defines */
#define MPI2_RAIDVOL0_SETTING_USE_PRODUCT_ID_SUFFIX     (0x0008)
#define MPI2_RAIDVOL0_SETTING_AUTO_CONFIG_HSWAP_DISABLE (0x0004)

#define MPI2_RAIDVOL0_SETTING_MASK_WRITE_CACHING        (0x0003)
#define MPI2_RAIDVOL0_SETTING_UNCHANGED                 (0x0000)
#define MPI2_RAIDVOL0_SETTING_DISABLE_WRITE_CACHING     (0x0001)
#define MPI2_RAIDVOL0_SETTING_ENABLE_WRITE_CACHING      (0x0002)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhysDisks at runtime.
 */
#ifndef MPI2_RAID_VOL_PAGE_0_PHYSDISK_MAX
#define MPI2_RAID_VOL_PAGE_0_PHYSDISK_MAX       (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_RAID_VOL_0 {
	MPI2_CONFIG_PAGE_HEADER Header;            /*0x00 */
	U16                     DevHandle;         /*0x04 */
	U8                      VolumeState;       /*0x06 */
	U8                      VolumeType;        /*0x07 */
	U32                     VolumeStatusFlags; /*0x08 */
	MPI2_RAIDVOL0_SETTINGS  VolumeSettings;    /*0x0C */
	U64                     MaxLBA;            /*0x10 */
	U32                     StripeSize;        /*0x18 */
	U16                     BlockSize;         /*0x1C */
	U16                     Reserved1;         /*0x1E */
	U8                      SupportedPhysDisks;/*0x20 */
	U8                      ResyncRate;        /*0x21 */
	U16                     DataScrubDuration; /*0x22 */
	U8                      NumPhysDisks;      /*0x24 */
	U8                      Reserved2;         /*0x25 */
	U8                      Reserved3;         /*0x26 */
	U8                      InactiveStatus;    /*0x27 */
	MPI2_RAIDVOL0_PHYS_DISK
	PhysDisk[MPI2_RAID_VOL_PAGE_0_PHYSDISK_MAX]; /*0x28 */
} MPI2_CONFIG_PAGE_RAID_VOL_0,
	*PTR_MPI2_CONFIG_PAGE_RAID_VOL_0,
	Mpi2RaidVolPage0_t, *pMpi2RaidVolPage0_t;

#define MPI2_RAIDVOLPAGE0_PAGEVERSION           (0x0A)

/*values for RAID VolumeState */
#define MPI2_RAID_VOL_STATE_MISSING                         (0x00)
#define MPI2_RAID_VOL_STATE_FAILED                          (0x01)
#define MPI2_RAID_VOL_STATE_INITIALIZING                    (0x02)
#define MPI2_RAID_VOL_STATE_ONLINE                          (0x03)
#define MPI2_RAID_VOL_STATE_DEGRADED                        (0x04)
#define MPI2_RAID_VOL_STATE_OPTIMAL                         (0x05)

/*values for RAID VolumeType */
#define MPI2_RAID_VOL_TYPE_RAID0                            (0x00)
#define MPI2_RAID_VOL_TYPE_RAID1E                           (0x01)
#define MPI2_RAID_VOL_TYPE_RAID1                            (0x02)
#define MPI2_RAID_VOL_TYPE_RAID10                           (0x05)
#define MPI2_RAID_VOL_TYPE_UNKNOWN                          (0xFF)

/*values for RAID Volume Page 0 VolumeStatusFlags field */
#define MPI2_RAIDVOL0_STATUS_FLAG_PENDING_RESYNC            (0x02000000)
#define MPI2_RAIDVOL0_STATUS_FLAG_BACKG_INIT_PENDING        (0x01000000)
#define MPI2_RAIDVOL0_STATUS_FLAG_MDC_PENDING               (0x00800000)
#define MPI2_RAIDVOL0_STATUS_FLAG_USER_CONSIST_PENDING      (0x00400000)
#define MPI2_RAIDVOL0_STATUS_FLAG_MAKE_DATA_CONSISTENT      (0x00200000)
#define MPI2_RAIDVOL0_STATUS_FLAG_DATA_SCRUB                (0x00100000)
#define MPI2_RAIDVOL0_STATUS_FLAG_CONSISTENCY_CHECK         (0x00080000)
#define MPI2_RAIDVOL0_STATUS_FLAG_CAPACITY_EXPANSION        (0x00040000)
#define MPI2_RAIDVOL0_STATUS_FLAG_BACKGROUND_INIT           (0x00020000)
#define MPI2_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS        (0x00010000)
#define MPI2_RAIDVOL0_STATUS_FLAG_VOL_NOT_CONSISTENT        (0x00000080)
#define MPI2_RAIDVOL0_STATUS_FLAG_OCE_ALLOWED               (0x00000040)
#define MPI2_RAIDVOL0_STATUS_FLAG_BGI_COMPLETE              (0x00000020)
#define MPI2_RAIDVOL0_STATUS_FLAG_1E_OFFSET_MIRROR          (0x00000000)
#define MPI2_RAIDVOL0_STATUS_FLAG_1E_ADJACENT_MIRROR        (0x00000010)
#define MPI2_RAIDVOL0_STATUS_FLAG_BAD_BLOCK_TABLE_FULL      (0x00000008)
#define MPI2_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE           (0x00000004)
#define MPI2_RAIDVOL0_STATUS_FLAG_QUIESCED                  (0x00000002)
#define MPI2_RAIDVOL0_STATUS_FLAG_ENABLED                   (0x00000001)

/*values for RAID Volume Page 0 SupportedPhysDisks field */
#define MPI2_RAIDVOL0_SUPPORT_SOLID_STATE_DISKS             (0x08)
#define MPI2_RAIDVOL0_SUPPORT_HARD_DISKS                    (0x04)
#define MPI2_RAIDVOL0_SUPPORT_SAS_PROTOCOL                  (0x02)
#define MPI2_RAIDVOL0_SUPPORT_SATA_PROTOCOL                 (0x01)

/*values for RAID Volume Page 0 InactiveStatus field */
#define MPI2_RAIDVOLPAGE0_UNKNOWN_INACTIVE                  (0x00)
#define MPI2_RAIDVOLPAGE0_STALE_METADATA_INACTIVE           (0x01)
#define MPI2_RAIDVOLPAGE0_FOREIGN_VOLUME_INACTIVE           (0x02)
#define MPI2_RAIDVOLPAGE0_INSUFFICIENT_RESOURCE_INACTIVE    (0x03)
#define MPI2_RAIDVOLPAGE0_CLONE_VOLUME_INACTIVE             (0x04)
#define MPI2_RAIDVOLPAGE0_INSUFFICIENT_METADATA_INACTIVE    (0x05)
#define MPI2_RAIDVOLPAGE0_PREVIOUSLY_DELETED                (0x06)


/*RAID Volume Page 1 */

typedef struct _MPI2_CONFIG_PAGE_RAID_VOL_1 {
	MPI2_CONFIG_PAGE_HEADER Header;                     /*0x00 */
	U16                     DevHandle;                  /*0x04 */
	U16                     Reserved0;                  /*0x06 */
	U8                      GUID[24];                   /*0x08 */
	U8                      Name[16];                   /*0x20 */
	U64                     WWID;                       /*0x30 */
	U32                     Reserved1;                  /*0x38 */
	U32                     Reserved2;                  /*0x3C */
} MPI2_CONFIG_PAGE_RAID_VOL_1,
	*PTR_MPI2_CONFIG_PAGE_RAID_VOL_1,
	Mpi2RaidVolPage1_t, *pMpi2RaidVolPage1_t;

#define MPI2_RAIDVOLPAGE1_PAGEVERSION           (0x03)


/****************************************************************************
*  RAID Physical Disk Config Pages
****************************************************************************/

/*RAID Physical Disk Page 0 */

typedef struct _MPI2_RAIDPHYSDISK0_SETTINGS {
	U16                     Reserved1;                  /*0x00 */
	U8                      HotSparePool;               /*0x02 */
	U8                      Reserved2;                  /*0x03 */
} MPI2_RAIDPHYSDISK0_SETTINGS,
	*PTR_MPI2_RAIDPHYSDISK0_SETTINGS,
	Mpi2RaidPhysDisk0Settings_t,
	*pMpi2RaidPhysDisk0Settings_t;

/*use MPI2_RAID_HOT_SPARE_POOL_ defines for the HotSparePool field */

typedef struct _MPI2_RAIDPHYSDISK0_INQUIRY_DATA {
	U8                      VendorID[8];                /*0x00 */
	U8                      ProductID[16];              /*0x08 */
	U8                      ProductRevLevel[4];         /*0x18 */
	U8                      SerialNum[32];              /*0x1C */
} MPI2_RAIDPHYSDISK0_INQUIRY_DATA,
	*PTR_MPI2_RAIDPHYSDISK0_INQUIRY_DATA,
	Mpi2RaidPhysDisk0InquiryData_t,
	*pMpi2RaidPhysDisk0InquiryData_t;

typedef struct _MPI2_CONFIG_PAGE_RD_PDISK_0 {
	MPI2_CONFIG_PAGE_HEADER         Header;             /*0x00 */
	U16                             DevHandle;          /*0x04 */
	U8                              Reserved1;          /*0x06 */
	U8                              PhysDiskNum;        /*0x07 */
	MPI2_RAIDPHYSDISK0_SETTINGS     PhysDiskSettings;   /*0x08 */
	U32                             Reserved2;          /*0x0C */
	MPI2_RAIDPHYSDISK0_INQUIRY_DATA InquiryData;        /*0x10 */
	U32                             Reserved3;          /*0x4C */
	U8                              PhysDiskState;      /*0x50 */
	U8                              OfflineReason;      /*0x51 */
	U8                              IncompatibleReason; /*0x52 */
	U8                              PhysDiskAttributes; /*0x53 */
	U32                             PhysDiskStatusFlags;/*0x54 */
	U64                             DeviceMaxLBA;       /*0x58 */
	U64                             HostMaxLBA;         /*0x60 */
	U64                             CoercedMaxLBA;      /*0x68 */
	U16                             BlockSize;          /*0x70 */
	U16                             Reserved5;          /*0x72 */
	U32                             Reserved6;          /*0x74 */
} MPI2_CONFIG_PAGE_RD_PDISK_0,
	*PTR_MPI2_CONFIG_PAGE_RD_PDISK_0,
	Mpi2RaidPhysDiskPage0_t,
	*pMpi2RaidPhysDiskPage0_t;

#define MPI2_RAIDPHYSDISKPAGE0_PAGEVERSION          (0x05)

/*PhysDiskState defines */
#define MPI2_RAID_PD_STATE_NOT_CONFIGURED               (0x00)
#define MPI2_RAID_PD_STATE_NOT_COMPATIBLE               (0x01)
#define MPI2_RAID_PD_STATE_OFFLINE                      (0x02)
#define MPI2_RAID_PD_STATE_ONLINE                       (0x03)
#define MPI2_RAID_PD_STATE_HOT_SPARE                    (0x04)
#define MPI2_RAID_PD_STATE_DEGRADED                     (0x05)
#define MPI2_RAID_PD_STATE_REBUILDING                   (0x06)
#define MPI2_RAID_PD_STATE_OPTIMAL                      (0x07)

/*OfflineReason defines */
#define MPI2_PHYSDISK0_ONLINE                           (0x00)
#define MPI2_PHYSDISK0_OFFLINE_MISSING                  (0x01)
#define MPI2_PHYSDISK0_OFFLINE_FAILED                   (0x03)
#define MPI2_PHYSDISK0_OFFLINE_INITIALIZING             (0x04)
#define MPI2_PHYSDISK0_OFFLINE_REQUESTED                (0x05)
#define MPI2_PHYSDISK0_OFFLINE_FAILED_REQUESTED         (0x06)
#define MPI2_PHYSDISK0_OFFLINE_OTHER                    (0xFF)

/*IncompatibleReason defines */
#define MPI2_PHYSDISK0_COMPATIBLE                       (0x00)
#define MPI2_PHYSDISK0_INCOMPATIBLE_PROTOCOL            (0x01)
#define MPI2_PHYSDISK0_INCOMPATIBLE_BLOCKSIZE           (0x02)
#define MPI2_PHYSDISK0_INCOMPATIBLE_MAX_LBA             (0x03)
#define MPI2_PHYSDISK0_INCOMPATIBLE_SATA_EXTENDED_CMD   (0x04)
#define MPI2_PHYSDISK0_INCOMPATIBLE_REMOVEABLE_MEDIA    (0x05)
#define MPI2_PHYSDISK0_INCOMPATIBLE_MEDIA_TYPE          (0x06)
#define MPI2_PHYSDISK0_INCOMPATIBLE_UNKNOWN             (0xFF)

/*PhysDiskAttributes defines */
#define MPI2_PHYSDISK0_ATTRIB_MEDIA_MASK                (0x0C)
#define MPI2_PHYSDISK0_ATTRIB_SOLID_STATE_DRIVE         (0x08)
#define MPI2_PHYSDISK0_ATTRIB_HARD_DISK_DRIVE           (0x04)

#define MPI2_PHYSDISK0_ATTRIB_PROTOCOL_MASK             (0x03)
#define MPI2_PHYSDISK0_ATTRIB_SAS_PROTOCOL              (0x02)
#define MPI2_PHYSDISK0_ATTRIB_SATA_PROTOCOL             (0x01)

/*PhysDiskStatusFlags defines */
#define MPI2_PHYSDISK0_STATUS_FLAG_NOT_CERTIFIED        (0x00000040)
#define MPI2_PHYSDISK0_STATUS_FLAG_OCE_TARGET           (0x00000020)
#define MPI2_PHYSDISK0_STATUS_FLAG_WRITE_CACHE_ENABLED  (0x00000010)
#define MPI2_PHYSDISK0_STATUS_FLAG_OPTIMAL_PREVIOUS     (0x00000000)
#define MPI2_PHYSDISK0_STATUS_FLAG_NOT_OPTIMAL_PREVIOUS (0x00000008)
#define MPI2_PHYSDISK0_STATUS_FLAG_INACTIVE_VOLUME      (0x00000004)
#define MPI2_PHYSDISK0_STATUS_FLAG_QUIESCED             (0x00000002)
#define MPI2_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC          (0x00000001)


/*RAID Physical Disk Page 1 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhysDiskPaths at runtime.
 */
#ifndef MPI2_RAID_PHYS_DISK1_PATH_MAX
#define MPI2_RAID_PHYS_DISK1_PATH_MAX   (1)
#endif

typedef struct _MPI2_RAIDPHYSDISK1_PATH {
	U16             DevHandle;          /*0x00 */
	U16             Reserved1;          /*0x02 */
	U64             WWID;               /*0x04 */
	U64             OwnerWWID;          /*0x0C */
	U8              OwnerIdentifier;    /*0x14 */
	U8              Reserved2;          /*0x15 */
	U16             Flags;              /*0x16 */
} MPI2_RAIDPHYSDISK1_PATH, *PTR_MPI2_RAIDPHYSDISK1_PATH,
	Mpi2RaidPhysDisk1Path_t,
	*pMpi2RaidPhysDisk1Path_t;

/*RAID Physical Disk Page 1 Physical Disk Path Flags field defines */
#define MPI2_RAID_PHYSDISK1_FLAG_PRIMARY        (0x0004)
#define MPI2_RAID_PHYSDISK1_FLAG_BROKEN         (0x0002)
#define MPI2_RAID_PHYSDISK1_FLAG_INVALID        (0x0001)

typedef struct _MPI2_CONFIG_PAGE_RD_PDISK_1 {
	MPI2_CONFIG_PAGE_HEADER         Header;             /*0x00 */
	U8                              NumPhysDiskPaths;   /*0x04 */
	U8                              PhysDiskNum;        /*0x05 */
	U16                             Reserved1;          /*0x06 */
	U32                             Reserved2;          /*0x08 */
	MPI2_RAIDPHYSDISK1_PATH
		PhysicalDiskPath[MPI2_RAID_PHYS_DISK1_PATH_MAX];/*0x0C */
} MPI2_CONFIG_PAGE_RD_PDISK_1,
	*PTR_MPI2_CONFIG_PAGE_RD_PDISK_1,
	Mpi2RaidPhysDiskPage1_t,
	*pMpi2RaidPhysDiskPage1_t;

#define MPI2_RAIDPHYSDISKPAGE1_PAGEVERSION          (0x02)


/****************************************************************************
*  values for fields used by several types of SAS Config Pages
****************************************************************************/

/*values for NegotiatedLinkRates fields */
#define MPI2_SAS_NEG_LINK_RATE_MASK_LOGICAL             (0xF0)
#define MPI2_SAS_NEG_LINK_RATE_SHIFT_LOGICAL            (4)
#define MPI2_SAS_NEG_LINK_RATE_MASK_PHYSICAL            (0x0F)
/*link rates used for Negotiated Physical and Logical Link Rate */
#define MPI2_SAS_NEG_LINK_RATE_UNKNOWN_LINK_RATE        (0x00)
#define MPI2_SAS_NEG_LINK_RATE_PHY_DISABLED             (0x01)
#define MPI2_SAS_NEG_LINK_RATE_NEGOTIATION_FAILED       (0x02)
#define MPI2_SAS_NEG_LINK_RATE_SATA_OOB_COMPLETE        (0x03)
#define MPI2_SAS_NEG_LINK_RATE_PORT_SELECTOR            (0x04)
#define MPI2_SAS_NEG_LINK_RATE_SMP_RESET_IN_PROGRESS    (0x05)
#define MPI2_SAS_NEG_LINK_RATE_UNSUPPORTED_PHY          (0x06)
#define MPI2_SAS_NEG_LINK_RATE_1_5                      (0x08)
#define MPI2_SAS_NEG_LINK_RATE_3_0                      (0x09)
#define MPI2_SAS_NEG_LINK_RATE_6_0                      (0x0A)
#define MPI25_SAS_NEG_LINK_RATE_12_0                    (0x0B)
#define MPI26_SAS_NEG_LINK_RATE_22_5                    (0x0C)


/*values for AttachedPhyInfo fields */
#define MPI2_SAS_APHYINFO_INSIDE_ZPSDS_PERSISTENT       (0x00000040)
#define MPI2_SAS_APHYINFO_REQUESTED_INSIDE_ZPSDS        (0x00000020)
#define MPI2_SAS_APHYINFO_BREAK_REPLY_CAPABLE           (0x00000010)

#define MPI2_SAS_APHYINFO_REASON_MASK                   (0x0000000F)
#define MPI2_SAS_APHYINFO_REASON_UNKNOWN                (0x00000000)
#define MPI2_SAS_APHYINFO_REASON_POWER_ON               (0x00000001)
#define MPI2_SAS_APHYINFO_REASON_HARD_RESET             (0x00000002)
#define MPI2_SAS_APHYINFO_REASON_SMP_PHY_CONTROL        (0x00000003)
#define MPI2_SAS_APHYINFO_REASON_LOSS_OF_SYNC           (0x00000004)
#define MPI2_SAS_APHYINFO_REASON_MULTIPLEXING_SEQ       (0x00000005)
#define MPI2_SAS_APHYINFO_REASON_IT_NEXUS_LOSS_TIMER    (0x00000006)
#define MPI2_SAS_APHYINFO_REASON_BREAK_TIMEOUT          (0x00000007)
#define MPI2_SAS_APHYINFO_REASON_PHY_TEST_STOPPED       (0x00000008)


/*values for PhyInfo fields */
#define MPI2_SAS_PHYINFO_PHY_VACANT                     (0x80000000)

#define MPI2_SAS_PHYINFO_PHY_POWER_CONDITION_MASK       (0x18000000)
#define MPI2_SAS_PHYINFO_SHIFT_PHY_POWER_CONDITION      (27)
#define MPI2_SAS_PHYINFO_PHY_POWER_ACTIVE               (0x00000000)
#define MPI2_SAS_PHYINFO_PHY_POWER_PARTIAL              (0x08000000)
#define MPI2_SAS_PHYINFO_PHY_POWER_SLUMBER              (0x10000000)

#define MPI2_SAS_PHYINFO_CHANGED_REQ_INSIDE_ZPSDS       (0x04000000)
#define MPI2_SAS_PHYINFO_INSIDE_ZPSDS_PERSISTENT        (0x02000000)
#define MPI2_SAS_PHYINFO_REQ_INSIDE_ZPSDS               (0x01000000)
#define MPI2_SAS_PHYINFO_ZONE_GROUP_PERSISTENT          (0x00400000)
#define MPI2_SAS_PHYINFO_INSIDE_ZPSDS                   (0x00200000)
#define MPI2_SAS_PHYINFO_ZONING_ENABLED                 (0x00100000)

#define MPI2_SAS_PHYINFO_REASON_MASK                    (0x000F0000)
#define MPI2_SAS_PHYINFO_REASON_UNKNOWN                 (0x00000000)
#define MPI2_SAS_PHYINFO_REASON_POWER_ON                (0x00010000)
#define MPI2_SAS_PHYINFO_REASON_HARD_RESET              (0x00020000)
#define MPI2_SAS_PHYINFO_REASON_SMP_PHY_CONTROL         (0x00030000)
#define MPI2_SAS_PHYINFO_REASON_LOSS_OF_SYNC            (0x00040000)
#define MPI2_SAS_PHYINFO_REASON_MULTIPLEXING_SEQ        (0x00050000)
#define MPI2_SAS_PHYINFO_REASON_IT_NEXUS_LOSS_TIMER     (0x00060000)
#define MPI2_SAS_PHYINFO_REASON_BREAK_TIMEOUT           (0x00070000)
#define MPI2_SAS_PHYINFO_REASON_PHY_TEST_STOPPED        (0x00080000)

#define MPI2_SAS_PHYINFO_MULTIPLEXING_SUPPORTED         (0x00008000)
#define MPI2_SAS_PHYINFO_SATA_PORT_ACTIVE               (0x00004000)
#define MPI2_SAS_PHYINFO_SATA_PORT_SELECTOR_PRESENT     (0x00002000)
#define MPI2_SAS_PHYINFO_VIRTUAL_PHY                    (0x00001000)

#define MPI2_SAS_PHYINFO_MASK_PARTIAL_PATHWAY_TIME      (0x00000F00)
#define MPI2_SAS_PHYINFO_SHIFT_PARTIAL_PATHWAY_TIME     (8)

#define MPI2_SAS_PHYINFO_MASK_ROUTING_ATTRIBUTE         (0x000000F0)
#define MPI2_SAS_PHYINFO_DIRECT_ROUTING                 (0x00000000)
#define MPI2_SAS_PHYINFO_SUBTRACTIVE_ROUTING            (0x00000010)
#define MPI2_SAS_PHYINFO_TABLE_ROUTING                  (0x00000020)


/*values for SAS ProgrammedLinkRate fields */
#define MPI2_SAS_PRATE_MAX_RATE_MASK                    (0xF0)
#define MPI2_SAS_PRATE_MAX_RATE_NOT_PROGRAMMABLE        (0x00)
#define MPI2_SAS_PRATE_MAX_RATE_1_5                     (0x80)
#define MPI2_SAS_PRATE_MAX_RATE_3_0                     (0x90)
#define MPI2_SAS_PRATE_MAX_RATE_6_0                     (0xA0)
#define MPI25_SAS_PRATE_MAX_RATE_12_0                   (0xB0)
#define MPI26_SAS_PRATE_MAX_RATE_22_5                   (0xC0)
#define MPI2_SAS_PRATE_MIN_RATE_MASK                    (0x0F)
#define MPI2_SAS_PRATE_MIN_RATE_NOT_PROGRAMMABLE        (0x00)
#define MPI2_SAS_PRATE_MIN_RATE_1_5                     (0x08)
#define MPI2_SAS_PRATE_MIN_RATE_3_0                     (0x09)
#define MPI2_SAS_PRATE_MIN_RATE_6_0                     (0x0A)
#define MPI25_SAS_PRATE_MIN_RATE_12_0                   (0x0B)
#define MPI26_SAS_PRATE_MIN_RATE_22_5                   (0x0C)


/*values for SAS HwLinkRate fields */
#define MPI2_SAS_HWRATE_MAX_RATE_MASK                   (0xF0)
#define MPI2_SAS_HWRATE_MAX_RATE_1_5                    (0x80)
#define MPI2_SAS_HWRATE_MAX_RATE_3_0                    (0x90)
#define MPI2_SAS_HWRATE_MAX_RATE_6_0                    (0xA0)
#define MPI25_SAS_HWRATE_MAX_RATE_12_0                  (0xB0)
#define MPI26_SAS_HWRATE_MAX_RATE_22_5                  (0xC0)
#define MPI2_SAS_HWRATE_MIN_RATE_MASK                   (0x0F)
#define MPI2_SAS_HWRATE_MIN_RATE_1_5                    (0x08)
#define MPI2_SAS_HWRATE_MIN_RATE_3_0                    (0x09)
#define MPI2_SAS_HWRATE_MIN_RATE_6_0                    (0x0A)
#define MPI25_SAS_HWRATE_MIN_RATE_12_0                  (0x0B)
#define MPI26_SAS_HWRATE_MIN_RATE_22_5                  (0x0C)



/****************************************************************************
*  SAS IO Unit Config Pages
****************************************************************************/

/*SAS IO Unit Page 0 */

typedef struct _MPI2_SAS_IO_UNIT0_PHY_DATA {
	U8          Port;                   /*0x00 */
	U8          PortFlags;              /*0x01 */
	U8          PhyFlags;               /*0x02 */
	U8          NegotiatedLinkRate;     /*0x03 */
	U32         ControllerPhyDeviceInfo;/*0x04 */
	U16         AttachedDevHandle;      /*0x08 */
	U16         ControllerDevHandle;    /*0x0A */
	U32         DiscoveryStatus;        /*0x0C */
	U32         Reserved;               /*0x10 */
} MPI2_SAS_IO_UNIT0_PHY_DATA,
	*PTR_MPI2_SAS_IO_UNIT0_PHY_DATA,
	Mpi2SasIOUnit0PhyData_t,
	*pMpi2SasIOUnit0PhyData_t;

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_SAS_IOUNIT0_PHY_MAX
#define MPI2_SAS_IOUNIT0_PHY_MAX        (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;   /*0x00 */
	U32                                 Reserved1;/*0x08 */
	U8                                  NumPhys;  /*0x0C */
	U8                                  Reserved2;/*0x0D */
	U16                                 Reserved3;/*0x0E */
	MPI2_SAS_IO_UNIT0_PHY_DATA
		PhyData[MPI2_SAS_IOUNIT0_PHY_MAX];    /*0x10 */
} MPI2_CONFIG_PAGE_SASIOUNIT_0,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_0,
	Mpi2SasIOUnitPage0_t, *pMpi2SasIOUnitPage0_t;

#define MPI2_SASIOUNITPAGE0_PAGEVERSION                     (0x05)

/*values for SAS IO Unit Page 0 PortFlags */
#define MPI2_SASIOUNIT0_PORTFLAGS_DISCOVERY_IN_PROGRESS     (0x08)
#define MPI2_SASIOUNIT0_PORTFLAGS_AUTO_PORT_CONFIG          (0x01)

/*values for SAS IO Unit Page 0 PhyFlags */
#define MPI2_SASIOUNIT0_PHYFLAGS_INIT_PERSIST_CONNECT       (0x40)
#define MPI2_SASIOUNIT0_PHYFLAGS_TARG_PERSIST_CONNECT       (0x20)
#define MPI2_SASIOUNIT0_PHYFLAGS_ZONING_ENABLED             (0x10)
#define MPI2_SASIOUNIT0_PHYFLAGS_PHY_DISABLED               (0x08)

/*use MPI2_SAS_NEG_LINK_RATE_ defines for the NegotiatedLinkRate field */

/*see mpi2_sas.h for values for
 *SAS IO Unit Page 0 ControllerPhyDeviceInfo values */

/*values for SAS IO Unit Page 0 DiscoveryStatus */
#define MPI2_SASIOUNIT0_DS_MAX_ENCLOSURES_EXCEED            (0x80000000)
#define MPI2_SASIOUNIT0_DS_MAX_EXPANDERS_EXCEED             (0x40000000)
#define MPI2_SASIOUNIT0_DS_MAX_DEVICES_EXCEED               (0x20000000)
#define MPI2_SASIOUNIT0_DS_MAX_TOPO_PHYS_EXCEED             (0x10000000)
#define MPI2_SASIOUNIT0_DS_DOWNSTREAM_INITIATOR             (0x08000000)
#define MPI2_SASIOUNIT0_DS_MULTI_SUBTRACTIVE_SUBTRACTIVE    (0x00008000)
#define MPI2_SASIOUNIT0_DS_EXP_MULTI_SUBTRACTIVE            (0x00004000)
#define MPI2_SASIOUNIT0_DS_MULTI_PORT_DOMAIN                (0x00002000)
#define MPI2_SASIOUNIT0_DS_TABLE_TO_SUBTRACTIVE_LINK        (0x00001000)
#define MPI2_SASIOUNIT0_DS_UNSUPPORTED_DEVICE               (0x00000800)
#define MPI2_SASIOUNIT0_DS_TABLE_LINK                       (0x00000400)
#define MPI2_SASIOUNIT0_DS_SUBTRACTIVE_LINK                 (0x00000200)
#define MPI2_SASIOUNIT0_DS_SMP_CRC_ERROR                    (0x00000100)
#define MPI2_SASIOUNIT0_DS_SMP_FUNCTION_FAILED              (0x00000080)
#define MPI2_SASIOUNIT0_DS_INDEX_NOT_EXIST                  (0x00000040)
#define MPI2_SASIOUNIT0_DS_OUT_ROUTE_ENTRIES                (0x00000020)
#define MPI2_SASIOUNIT0_DS_SMP_TIMEOUT                      (0x00000010)
#define MPI2_SASIOUNIT0_DS_MULTIPLE_PORTS                   (0x00000004)
#define MPI2_SASIOUNIT0_DS_UNADDRESSABLE_DEVICE             (0x00000002)
#define MPI2_SASIOUNIT0_DS_LOOP_DETECTED                    (0x00000001)


/*SAS IO Unit Page 1 */

typedef struct _MPI2_SAS_IO_UNIT1_PHY_DATA {
	U8          Port;                       /*0x00 */
	U8          PortFlags;                  /*0x01 */
	U8          PhyFlags;                   /*0x02 */
	U8          MaxMinLinkRate;             /*0x03 */
	U32         ControllerPhyDeviceInfo;    /*0x04 */
	U16         MaxTargetPortConnectTime;   /*0x08 */
	U16         Reserved1;                  /*0x0A */
} MPI2_SAS_IO_UNIT1_PHY_DATA,
	*PTR_MPI2_SAS_IO_UNIT1_PHY_DATA,
	Mpi2SasIOUnit1PhyData_t,
	*pMpi2SasIOUnit1PhyData_t;

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_SAS_IOUNIT1_PHY_MAX
#define MPI2_SAS_IOUNIT1_PHY_MAX        (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header; /*0x00 */
	U16
		ControlFlags;                       /*0x08 */
	U16
		SASNarrowMaxQueueDepth;             /*0x0A */
	U16
		AdditionalControlFlags;             /*0x0C */
	U16
		SASWideMaxQueueDepth;               /*0x0E */
	U8
		NumPhys;                            /*0x10 */
	U8
		SATAMaxQDepth;                      /*0x11 */
	U8
		ReportDeviceMissingDelay;           /*0x12 */
	U8
		IODeviceMissingDelay;               /*0x13 */
	MPI2_SAS_IO_UNIT1_PHY_DATA
		PhyData[MPI2_SAS_IOUNIT1_PHY_MAX];  /*0x14 */
} MPI2_CONFIG_PAGE_SASIOUNIT_1,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_1,
	Mpi2SasIOUnitPage1_t, *pMpi2SasIOUnitPage1_t;

#define MPI2_SASIOUNITPAGE1_PAGEVERSION     (0x09)

/*values for SAS IO Unit Page 1 ControlFlags */
#define MPI2_SASIOUNIT1_CONTROL_DEVICE_SELF_TEST                    (0x8000)
#define MPI2_SASIOUNIT1_CONTROL_SATA_3_0_MAX                        (0x4000)
#define MPI2_SASIOUNIT1_CONTROL_SATA_1_5_MAX                        (0x2000)
#define MPI2_SASIOUNIT1_CONTROL_SATA_SW_PRESERVE                    (0x1000)

#define MPI2_SASIOUNIT1_CONTROL_MASK_DEV_SUPPORT                    (0x0600)
#define MPI2_SASIOUNIT1_CONTROL_SHIFT_DEV_SUPPORT                   (9)
#define MPI2_SASIOUNIT1_CONTROL_DEV_SUPPORT_BOTH                    (0x0)
#define MPI2_SASIOUNIT1_CONTROL_DEV_SAS_SUPPORT                     (0x1)
#define MPI2_SASIOUNIT1_CONTROL_DEV_SATA_SUPPORT                    (0x2)

#define MPI2_SASIOUNIT1_CONTROL_SATA_48BIT_LBA_REQUIRED             (0x0080)
#define MPI2_SASIOUNIT1_CONTROL_SATA_SMART_REQUIRED                 (0x0040)
#define MPI2_SASIOUNIT1_CONTROL_SATA_NCQ_REQUIRED                   (0x0020)
#define MPI2_SASIOUNIT1_CONTROL_SATA_FUA_REQUIRED                   (0x0010)
#define MPI2_SASIOUNIT1_CONTROL_TABLE_SUBTRACTIVE_ILLEGAL           (0x0008)
#define MPI2_SASIOUNIT1_CONTROL_SUBTRACTIVE_ILLEGAL                 (0x0004)
#define MPI2_SASIOUNIT1_CONTROL_FIRST_LVL_DISC_ONLY                 (0x0002)
#define MPI2_SASIOUNIT1_CONTROL_CLEAR_AFFILIATION                   (0x0001)

/*values for SAS IO Unit Page 1 AdditionalControlFlags */
#define MPI2_SASIOUNIT1_ACONTROL_DA_PERSIST_CONNECT                 (0x0100)
#define MPI2_SASIOUNIT1_ACONTROL_MULTI_PORT_DOMAIN_ILLEGAL          (0x0080)
#define MPI2_SASIOUNIT1_ACONTROL_SATA_ASYNCHROUNOUS_NOTIFICATION    (0x0040)
#define MPI2_SASIOUNIT1_ACONTROL_INVALID_TOPOLOGY_CORRECTION        (0x0020)
#define MPI2_SASIOUNIT1_ACONTROL_PORT_ENABLE_ONLY_SATA_LINK_RESET   (0x0010)
#define MPI2_SASIOUNIT1_ACONTROL_OTHER_AFFILIATION_SATA_LINK_RESET  (0x0008)
#define MPI2_SASIOUNIT1_ACONTROL_SELF_AFFILIATION_SATA_LINK_RESET   (0x0004)
#define MPI2_SASIOUNIT1_ACONTROL_NO_AFFILIATION_SATA_LINK_RESET     (0x0002)
#define MPI2_SASIOUNIT1_ACONTROL_ALLOW_TABLE_TO_TABLE               (0x0001)

/*defines for SAS IO Unit Page 1 ReportDeviceMissingDelay */
#define MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK                 (0x7F)
#define MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16                      (0x80)

/*values for SAS IO Unit Page 1 PortFlags */
#define MPI2_SASIOUNIT1_PORT_FLAGS_AUTO_PORT_CONFIG                 (0x01)

/*values for SAS IO Unit Page 1 PhyFlags */
#define MPI2_SASIOUNIT1_PHYFLAGS_INIT_PERSIST_CONNECT               (0x40)
#define MPI2_SASIOUNIT1_PHYFLAGS_TARG_PERSIST_CONNECT               (0x20)
#define MPI2_SASIOUNIT1_PHYFLAGS_ZONING_ENABLE                      (0x10)
#define MPI2_SASIOUNIT1_PHYFLAGS_PHY_DISABLE                        (0x08)

/*values for SAS IO Unit Page 1 MaxMinLinkRate */
#define MPI2_SASIOUNIT1_MAX_RATE_MASK                               (0xF0)
#define MPI2_SASIOUNIT1_MAX_RATE_1_5                                (0x80)
#define MPI2_SASIOUNIT1_MAX_RATE_3_0                                (0x90)
#define MPI2_SASIOUNIT1_MAX_RATE_6_0                                (0xA0)
#define MPI25_SASIOUNIT1_MAX_RATE_12_0                              (0xB0)
#define MPI26_SASIOUNIT1_MAX_RATE_22_5                              (0xC0)
#define MPI2_SASIOUNIT1_MIN_RATE_MASK                               (0x0F)
#define MPI2_SASIOUNIT1_MIN_RATE_1_5                                (0x08)
#define MPI2_SASIOUNIT1_MIN_RATE_3_0                                (0x09)
#define MPI2_SASIOUNIT1_MIN_RATE_6_0                                (0x0A)
#define MPI25_SASIOUNIT1_MIN_RATE_12_0                              (0x0B)
#define MPI26_SASIOUNIT1_MIN_RATE_22_5                              (0x0C)

/*see mpi2_sas.h for values for
 *SAS IO Unit Page 1 ControllerPhyDeviceInfo values */


/*SAS IO Unit Page 4 (for MPI v2.5 and earlier) */

typedef struct _MPI2_SAS_IOUNIT4_SPINUP_GROUP {
	U8          MaxTargetSpinup;            /*0x00 */
	U8          SpinupDelay;                /*0x01 */
	U8          SpinupFlags;                /*0x02 */
	U8          Reserved1;                  /*0x03 */
} MPI2_SAS_IOUNIT4_SPINUP_GROUP,
	*PTR_MPI2_SAS_IOUNIT4_SPINUP_GROUP,
	Mpi2SasIOUnit4SpinupGroup_t,
	*pMpi2SasIOUnit4SpinupGroup_t;
/*defines for SAS IO Unit Page 4 SpinupFlags */
#define MPI2_SASIOUNIT4_SPINUP_DISABLE_FLAG         (0x01)


/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_SAS_IOUNIT4_PHY_MAX
#define MPI2_SAS_IOUNIT4_PHY_MAX        (4)
#endif

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_4 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;/*0x00 */
	MPI2_SAS_IOUNIT4_SPINUP_GROUP
		SpinupGroupParameters[4];       /*0x08 */
	U32
		Reserved1;                      /*0x18 */
	U32
		Reserved2;                      /*0x1C */
	U32
		Reserved3;                      /*0x20 */
	U8
		BootDeviceWaitTime;             /*0x24 */
	U8
		SATADeviceWaitTime;		/*0x25 */
	U16
		Reserved5;                      /*0x26 */
	U8
		NumPhys;                        /*0x28 */
	U8
		PEInitialSpinupDelay;           /*0x29 */
	U8
		PEReplyDelay;                   /*0x2A */
	U8
		Flags;                          /*0x2B */
	U8
		PHY[MPI2_SAS_IOUNIT4_PHY_MAX];  /*0x2C */
} MPI2_CONFIG_PAGE_SASIOUNIT_4,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_4,
	Mpi2SasIOUnitPage4_t, *pMpi2SasIOUnitPage4_t;

#define MPI2_SASIOUNITPAGE4_PAGEVERSION     (0x02)

/*defines for Flags field */
#define MPI2_SASIOUNIT4_FLAGS_AUTO_PORTENABLE               (0x01)

/*defines for PHY field */
#define MPI2_SASIOUNIT4_PHY_SPINUP_GROUP_MASK               (0x03)


/*SAS IO Unit Page 5 */

typedef struct _MPI2_SAS_IO_UNIT5_PHY_PM_SETTINGS {
	U8          ControlFlags;               /*0x00 */
	U8          PortWidthModGroup;          /*0x01 */
	U16         InactivityTimerExponent;    /*0x02 */
	U8          SATAPartialTimeout;         /*0x04 */
	U8          Reserved2;                  /*0x05 */
	U8          SATASlumberTimeout;         /*0x06 */
	U8          Reserved3;                  /*0x07 */
	U8          SASPartialTimeout;          /*0x08 */
	U8          Reserved4;                  /*0x09 */
	U8          SASSlumberTimeout;          /*0x0A */
	U8          Reserved5;                  /*0x0B */
} MPI2_SAS_IO_UNIT5_PHY_PM_SETTINGS,
	*PTR_MPI2_SAS_IO_UNIT5_PHY_PM_SETTINGS,
	Mpi2SasIOUnit5PhyPmSettings_t,
	*pMpi2SasIOUnit5PhyPmSettings_t;

/*defines for ControlFlags field */
#define MPI2_SASIOUNIT5_CONTROL_SAS_SLUMBER_ENABLE      (0x08)
#define MPI2_SASIOUNIT5_CONTROL_SAS_PARTIAL_ENABLE      (0x04)
#define MPI2_SASIOUNIT5_CONTROL_SATA_SLUMBER_ENABLE     (0x02)
#define MPI2_SASIOUNIT5_CONTROL_SATA_PARTIAL_ENABLE     (0x01)

/*defines for PortWidthModeGroup field */
#define MPI2_SASIOUNIT5_PWMG_DISABLE                    (0xFF)

/*defines for InactivityTimerExponent field */
#define MPI2_SASIOUNIT5_ITE_MASK_SAS_SLUMBER            (0x7000)
#define MPI2_SASIOUNIT5_ITE_SHIFT_SAS_SLUMBER           (12)
#define MPI2_SASIOUNIT5_ITE_MASK_SAS_PARTIAL            (0x0700)
#define MPI2_SASIOUNIT5_ITE_SHIFT_SAS_PARTIAL           (8)
#define MPI2_SASIOUNIT5_ITE_MASK_SATA_SLUMBER           (0x0070)
#define MPI2_SASIOUNIT5_ITE_SHIFT_SATA_SLUMBER          (4)
#define MPI2_SASIOUNIT5_ITE_MASK_SATA_PARTIAL           (0x0007)
#define MPI2_SASIOUNIT5_ITE_SHIFT_SATA_PARTIAL          (0)

#define MPI2_SASIOUNIT5_ITE_TEN_SECONDS                 (7)
#define MPI2_SASIOUNIT5_ITE_ONE_SECOND                  (6)
#define MPI2_SASIOUNIT5_ITE_HUNDRED_MILLISECONDS        (5)
#define MPI2_SASIOUNIT5_ITE_TEN_MILLISECONDS            (4)
#define MPI2_SASIOUNIT5_ITE_ONE_MILLISECOND             (3)
#define MPI2_SASIOUNIT5_ITE_HUNDRED_MICROSECONDS        (2)
#define MPI2_SASIOUNIT5_ITE_TEN_MICROSECONDS            (1)
#define MPI2_SASIOUNIT5_ITE_ONE_MICROSECOND             (0)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI2_SAS_IOUNIT5_PHY_MAX
#define MPI2_SAS_IOUNIT5_PHY_MAX        (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_5 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;   /*0x00 */
	U8                                  NumPhys;  /*0x08 */
	U8                                  Reserved1;/*0x09 */
	U16                                 Reserved2;/*0x0A */
	U32                                 Reserved3;/*0x0C */
	MPI2_SAS_IO_UNIT5_PHY_PM_SETTINGS
	SASPhyPowerManagementSettings[MPI2_SAS_IOUNIT5_PHY_MAX];/*0x10 */
} MPI2_CONFIG_PAGE_SASIOUNIT_5,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_5,
	Mpi2SasIOUnitPage5_t, *pMpi2SasIOUnitPage5_t;

#define MPI2_SASIOUNITPAGE5_PAGEVERSION     (0x01)


/*SAS IO Unit Page 6 */

typedef struct _MPI2_SAS_IO_UNIT6_PORT_WIDTH_MOD_GROUP_STATUS {
	U8          CurrentStatus;              /*0x00 */
	U8          CurrentModulation;          /*0x01 */
	U8          CurrentUtilization;         /*0x02 */
	U8          Reserved1;                  /*0x03 */
	U32         Reserved2;                  /*0x04 */
} MPI2_SAS_IO_UNIT6_PORT_WIDTH_MOD_GROUP_STATUS,
	*PTR_MPI2_SAS_IO_UNIT6_PORT_WIDTH_MOD_GROUP_STATUS,
	Mpi2SasIOUnit6PortWidthModGroupStatus_t,
	*pMpi2SasIOUnit6PortWidthModGroupStatus_t;

/*defines for CurrentStatus field */
#define MPI2_SASIOUNIT6_STATUS_UNAVAILABLE                      (0x00)
#define MPI2_SASIOUNIT6_STATUS_UNCONFIGURED                     (0x01)
#define MPI2_SASIOUNIT6_STATUS_INVALID_CONFIG                   (0x02)
#define MPI2_SASIOUNIT6_STATUS_LINK_DOWN                        (0x03)
#define MPI2_SASIOUNIT6_STATUS_OBSERVATION_ONLY                 (0x04)
#define MPI2_SASIOUNIT6_STATUS_INACTIVE                         (0x05)
#define MPI2_SASIOUNIT6_STATUS_ACTIVE_IOUNIT                    (0x06)
#define MPI2_SASIOUNIT6_STATUS_ACTIVE_HOST                      (0x07)

/*defines for CurrentModulation field */
#define MPI2_SASIOUNIT6_MODULATION_25_PERCENT                   (0x00)
#define MPI2_SASIOUNIT6_MODULATION_50_PERCENT                   (0x01)
#define MPI2_SASIOUNIT6_MODULATION_75_PERCENT                   (0x02)
#define MPI2_SASIOUNIT6_MODULATION_100_PERCENT                  (0x03)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumGroups at runtime.
 */
#ifndef MPI2_SAS_IOUNIT6_GROUP_MAX
#define MPI2_SAS_IOUNIT6_GROUP_MAX      (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_6 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;                 /*0x00 */
	U32                                 Reserved1;              /*0x08 */
	U32                                 Reserved2;              /*0x0C */
	U8                                  NumGroups;              /*0x10 */
	U8                                  Reserved3;              /*0x11 */
	U16                                 Reserved4;              /*0x12 */
	MPI2_SAS_IO_UNIT6_PORT_WIDTH_MOD_GROUP_STATUS
	PortWidthModulationGroupStatus[MPI2_SAS_IOUNIT6_GROUP_MAX]; /*0x14 */
} MPI2_CONFIG_PAGE_SASIOUNIT_6,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_6,
	Mpi2SasIOUnitPage6_t, *pMpi2SasIOUnitPage6_t;

#define MPI2_SASIOUNITPAGE6_PAGEVERSION     (0x00)


/*SAS IO Unit Page 7 */

typedef struct _MPI2_SAS_IO_UNIT7_PORT_WIDTH_MOD_GROUP_SETTINGS {
	U8          Flags;                      /*0x00 */
	U8          Reserved1;                  /*0x01 */
	U16         Reserved2;                  /*0x02 */
	U8          Threshold75Pct;             /*0x04 */
	U8          Threshold50Pct;             /*0x05 */
	U8          Threshold25Pct;             /*0x06 */
	U8          Reserved3;                  /*0x07 */
} MPI2_SAS_IO_UNIT7_PORT_WIDTH_MOD_GROUP_SETTINGS,
	*PTR_MPI2_SAS_IO_UNIT7_PORT_WIDTH_MOD_GROUP_SETTINGS,
	Mpi2SasIOUnit7PortWidthModGroupSettings_t,
	*pMpi2SasIOUnit7PortWidthModGroupSettings_t;

/*defines for Flags field */
#define MPI2_SASIOUNIT7_FLAGS_ENABLE_PORT_WIDTH_MODULATION  (0x01)


/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumGroups at runtime.
 */
#ifndef MPI2_SAS_IOUNIT7_GROUP_MAX
#define MPI2_SAS_IOUNIT7_GROUP_MAX      (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_7 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER Header;             /*0x00 */
	U8                               SamplingInterval;   /*0x08 */
	U8                               WindowLength;       /*0x09 */
	U16                              Reserved1;          /*0x0A */
	U32                              Reserved2;          /*0x0C */
	U32                              Reserved3;          /*0x10 */
	U8                               NumGroups;          /*0x14 */
	U8                               Reserved4;          /*0x15 */
	U16                              Reserved5;          /*0x16 */
	MPI2_SAS_IO_UNIT7_PORT_WIDTH_MOD_GROUP_SETTINGS
	PortWidthModulationGroupSettings[MPI2_SAS_IOUNIT7_GROUP_MAX];/*0x18 */
} MPI2_CONFIG_PAGE_SASIOUNIT_7,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_7,
	Mpi2SasIOUnitPage7_t, *pMpi2SasIOUnitPage7_t;

#define MPI2_SASIOUNITPAGE7_PAGEVERSION     (0x00)


/*SAS IO Unit Page 8 */

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT_8 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                         /*0x00 */
	U32
		Reserved1;                      /*0x08 */
	U32
		PowerManagementCapabilities;    /*0x0C */
	U8
		TxRxSleepStatus;                /*0x10 */
	U8
		Reserved2;                      /*0x11 */
	U16
		Reserved3;                      /*0x12 */
} MPI2_CONFIG_PAGE_SASIOUNIT_8,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT_8,
	Mpi2SasIOUnitPage8_t, *pMpi2SasIOUnitPage8_t;

#define MPI2_SASIOUNITPAGE8_PAGEVERSION     (0x00)

/*defines for PowerManagementCapabilities field */
#define MPI2_SASIOUNIT8_PM_HOST_PORT_WIDTH_MOD          (0x00001000)
#define MPI2_SASIOUNIT8_PM_HOST_SAS_SLUMBER_MODE        (0x00000800)
#define MPI2_SASIOUNIT8_PM_HOST_SAS_PARTIAL_MODE        (0x00000400)
#define MPI2_SASIOUNIT8_PM_HOST_SATA_SLUMBER_MODE       (0x00000200)
#define MPI2_SASIOUNIT8_PM_HOST_SATA_PARTIAL_MODE       (0x00000100)
#define MPI2_SASIOUNIT8_PM_IOUNIT_PORT_WIDTH_MOD        (0x00000010)
#define MPI2_SASIOUNIT8_PM_IOUNIT_SAS_SLUMBER_MODE      (0x00000008)
#define MPI2_SASIOUNIT8_PM_IOUNIT_SAS_PARTIAL_MODE      (0x00000004)
#define MPI2_SASIOUNIT8_PM_IOUNIT_SATA_SLUMBER_MODE     (0x00000002)
#define MPI2_SASIOUNIT8_PM_IOUNIT_SATA_PARTIAL_MODE     (0x00000001)

/*defines for TxRxSleepStatus field */
#define MPI25_SASIOUNIT8_TXRXSLEEP_UNSUPPORTED          (0x00)
#define MPI25_SASIOUNIT8_TXRXSLEEP_DISENGAGED           (0x01)
#define MPI25_SASIOUNIT8_TXRXSLEEP_ACTIVE               (0x02)
#define MPI25_SASIOUNIT8_TXRXSLEEP_SHUTDOWN             (0x03)



/*SAS IO Unit Page 16 */

typedef struct _MPI2_CONFIG_PAGE_SASIOUNIT16 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                             /*0x00 */
	U64
		TimeStamp;                          /*0x08 */
	U32
		Reserved1;                          /*0x10 */
	U32
		Reserved2;                          /*0x14 */
	U32
		FastPathPendedRequests;             /*0x18 */
	U32
		FastPathUnPendedRequests;           /*0x1C */
	U32
		FastPathHostRequestStarts;          /*0x20 */
	U32
		FastPathFirmwareRequestStarts;      /*0x24 */
	U32
		FastPathHostCompletions;            /*0x28 */
	U32
		FastPathFirmwareCompletions;        /*0x2C */
	U32
		NonFastPathRequestStarts;           /*0x30 */
	U32
		NonFastPathHostCompletions;         /*0x30 */
} MPI2_CONFIG_PAGE_SASIOUNIT16,
	*PTR_MPI2_CONFIG_PAGE_SASIOUNIT16,
	Mpi2SasIOUnitPage16_t, *pMpi2SasIOUnitPage16_t;

#define MPI2_SASIOUNITPAGE16_PAGEVERSION    (0x00)


/****************************************************************************
*  SAS Expander Config Pages
****************************************************************************/

/*SAS Expander Page 0 */

typedef struct _MPI2_CONFIG_PAGE_EXPANDER_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U8
		PhysicalPort;               /*0x08 */
	U8
		ReportGenLength;            /*0x09 */
	U16
		EnclosureHandle;            /*0x0A */
	U64
		SASAddress;                 /*0x0C */
	U32
		DiscoveryStatus;            /*0x14 */
	U16
		DevHandle;                  /*0x18 */
	U16
		ParentDevHandle;            /*0x1A */
	U16
		ExpanderChangeCount;        /*0x1C */
	U16
		ExpanderRouteIndexes;       /*0x1E */
	U8
		NumPhys;                    /*0x20 */
	U8
		SASLevel;                   /*0x21 */
	U16
		Flags;                      /*0x22 */
	U16
		STPBusInactivityTimeLimit;  /*0x24 */
	U16
		STPMaxConnectTimeLimit;     /*0x26 */
	U16
		STP_SMP_NexusLossTime;      /*0x28 */
	U16
		MaxNumRoutedSasAddresses;   /*0x2A */
	U64
		ActiveZoneManagerSASAddress;/*0x2C */
	U16
		ZoneLockInactivityLimit;    /*0x34 */
	U16
		Reserved1;                  /*0x36 */
	U8
		TimeToReducedFunc;          /*0x38 */
	U8
		InitialTimeToReducedFunc;   /*0x39 */
	U8
		MaxReducedFuncTime;         /*0x3A */
	U8
		Reserved2;                  /*0x3B */
} MPI2_CONFIG_PAGE_EXPANDER_0,
	*PTR_MPI2_CONFIG_PAGE_EXPANDER_0,
	Mpi2ExpanderPage0_t, *pMpi2ExpanderPage0_t;

#define MPI2_SASEXPANDER0_PAGEVERSION       (0x06)

/*values for SAS Expander Page 0 DiscoveryStatus field */
#define MPI2_SAS_EXPANDER0_DS_MAX_ENCLOSURES_EXCEED         (0x80000000)
#define MPI2_SAS_EXPANDER0_DS_MAX_EXPANDERS_EXCEED          (0x40000000)
#define MPI2_SAS_EXPANDER0_DS_MAX_DEVICES_EXCEED            (0x20000000)
#define MPI2_SAS_EXPANDER0_DS_MAX_TOPO_PHYS_EXCEED          (0x10000000)
#define MPI2_SAS_EXPANDER0_DS_DOWNSTREAM_INITIATOR          (0x08000000)
#define MPI2_SAS_EXPANDER0_DS_MULTI_SUBTRACTIVE_SUBTRACTIVE (0x00008000)
#define MPI2_SAS_EXPANDER0_DS_EXP_MULTI_SUBTRACTIVE         (0x00004000)
#define MPI2_SAS_EXPANDER0_DS_MULTI_PORT_DOMAIN             (0x00002000)
#define MPI2_SAS_EXPANDER0_DS_TABLE_TO_SUBTRACTIVE_LINK     (0x00001000)
#define MPI2_SAS_EXPANDER0_DS_UNSUPPORTED_DEVICE            (0x00000800)
#define MPI2_SAS_EXPANDER0_DS_TABLE_LINK                    (0x00000400)
#define MPI2_SAS_EXPANDER0_DS_SUBTRACTIVE_LINK              (0x00000200)
#define MPI2_SAS_EXPANDER0_DS_SMP_CRC_ERROR                 (0x00000100)
#define MPI2_SAS_EXPANDER0_DS_SMP_FUNCTION_FAILED           (0x00000080)
#define MPI2_SAS_EXPANDER0_DS_INDEX_NOT_EXIST               (0x00000040)
#define MPI2_SAS_EXPANDER0_DS_OUT_ROUTE_ENTRIES             (0x00000020)
#define MPI2_SAS_EXPANDER0_DS_SMP_TIMEOUT                   (0x00000010)
#define MPI2_SAS_EXPANDER0_DS_MULTIPLE_PORTS                (0x00000004)
#define MPI2_SAS_EXPANDER0_DS_UNADDRESSABLE_DEVICE          (0x00000002)
#define MPI2_SAS_EXPANDER0_DS_LOOP_DETECTED                 (0x00000001)

/*values for SAS Expander Page 0 Flags field */
#define MPI2_SAS_EXPANDER0_FLAGS_REDUCED_FUNCTIONALITY      (0x2000)
#define MPI2_SAS_EXPANDER0_FLAGS_ZONE_LOCKED                (0x1000)
#define MPI2_SAS_EXPANDER0_FLAGS_SUPPORTED_PHYSICAL_PRES    (0x0800)
#define MPI2_SAS_EXPANDER0_FLAGS_ASSERTED_PHYSICAL_PRES     (0x0400)
#define MPI2_SAS_EXPANDER0_FLAGS_ZONING_SUPPORT             (0x0200)
#define MPI2_SAS_EXPANDER0_FLAGS_ENABLED_ZONING             (0x0100)
#define MPI2_SAS_EXPANDER0_FLAGS_TABLE_TO_TABLE_SUPPORT     (0x0080)
#define MPI2_SAS_EXPANDER0_FLAGS_CONNECTOR_END_DEVICE       (0x0010)
#define MPI2_SAS_EXPANDER0_FLAGS_OTHERS_CONFIG              (0x0004)
#define MPI2_SAS_EXPANDER0_FLAGS_CONFIG_IN_PROGRESS         (0x0002)
#define MPI2_SAS_EXPANDER0_FLAGS_ROUTE_TABLE_CONFIG         (0x0001)


/*SAS Expander Page 1 */

typedef struct _MPI2_CONFIG_PAGE_EXPANDER_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U8
		PhysicalPort;               /*0x08 */
	U8
		Reserved1;                  /*0x09 */
	U16
		Reserved2;                  /*0x0A */
	U8
		NumPhys;                    /*0x0C */
	U8
		Phy;                        /*0x0D */
	U16
		NumTableEntriesProgrammed;  /*0x0E */
	U8
		ProgrammedLinkRate;         /*0x10 */
	U8
		HwLinkRate;                 /*0x11 */
	U16
		AttachedDevHandle;          /*0x12 */
	U32
		PhyInfo;                    /*0x14 */
	U32
		AttachedDeviceInfo;         /*0x18 */
	U16
		ExpanderDevHandle;          /*0x1C */
	U8
		ChangeCount;                /*0x1E */
	U8
		NegotiatedLinkRate;         /*0x1F */
	U8
		PhyIdentifier;              /*0x20 */
	U8
		AttachedPhyIdentifier;      /*0x21 */
	U8
		Reserved3;                  /*0x22 */
	U8
		DiscoveryInfo;              /*0x23 */
	U32
		AttachedPhyInfo;            /*0x24 */
	U8
		ZoneGroup;                  /*0x28 */
	U8
		SelfConfigStatus;           /*0x29 */
	U16
		Reserved4;                  /*0x2A */
} MPI2_CONFIG_PAGE_EXPANDER_1,
	*PTR_MPI2_CONFIG_PAGE_EXPANDER_1,
	Mpi2ExpanderPage1_t, *pMpi2ExpanderPage1_t;

#define MPI2_SASEXPANDER1_PAGEVERSION       (0x02)

/*use MPI2_SAS_PRATE_ defines for the ProgrammedLinkRate field */

/*use MPI2_SAS_HWRATE_ defines for the HwLinkRate field */

/*use MPI2_SAS_PHYINFO_ for the PhyInfo field */

/*see mpi2_sas.h for the MPI2_SAS_DEVICE_INFO_ defines
 *used for the AttachedDeviceInfo field */

/*use MPI2_SAS_NEG_LINK_RATE_ defines for the NegotiatedLinkRate field */

/*values for SAS Expander Page 1 DiscoveryInfo field */
#define MPI2_SAS_EXPANDER1_DISCINFO_BAD_PHY_DISABLED    (0x04)
#define MPI2_SAS_EXPANDER1_DISCINFO_LINK_STATUS_CHANGE  (0x02)
#define MPI2_SAS_EXPANDER1_DISCINFO_NO_ROUTING_ENTRIES  (0x01)

/*use MPI2_SAS_APHYINFO_ defines for AttachedPhyInfo field */


/****************************************************************************
*  SAS Device Config Pages
****************************************************************************/

/*SAS Device Page 0 */

typedef struct _MPI2_CONFIG_PAGE_SAS_DEV_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                 /*0x00 */
	U16
		Slot;                   /*0x08 */
	U16
		EnclosureHandle;        /*0x0A */
	U64
		SASAddress;             /*0x0C */
	U16
		ParentDevHandle;        /*0x14 */
	U8
		PhyNum;                 /*0x16 */
	U8
		AccessStatus;           /*0x17 */
	U16
		DevHandle;              /*0x18 */
	U8
		AttachedPhyIdentifier;  /*0x1A */
	U8
		ZoneGroup;              /*0x1B */
	U32
		DeviceInfo;             /*0x1C */
	U16
		Flags;                  /*0x20 */
	U8
		PhysicalPort;           /*0x22 */
	U8
		MaxPortConnections;     /*0x23 */
	U64
		DeviceName;             /*0x24 */
	U8
		PortGroups;             /*0x2C */
	U8
		DmaGroup;               /*0x2D */
	U8
		ControlGroup;           /*0x2E */
	U8
		EnclosureLevel;		/*0x2F */
	U32
		ConnectorName[4];	/*0x30 */
	U32
		Reserved3;              /*0x34 */
} MPI2_CONFIG_PAGE_SAS_DEV_0,
	*PTR_MPI2_CONFIG_PAGE_SAS_DEV_0,
	Mpi2SasDevicePage0_t,
	*pMpi2SasDevicePage0_t;

#define MPI2_SASDEVICE0_PAGEVERSION         (0x09)

/*values for SAS Device Page 0 AccessStatus field */
#define MPI2_SAS_DEVICE0_ASTATUS_NO_ERRORS                  (0x00)
#define MPI2_SAS_DEVICE0_ASTATUS_SATA_INIT_FAILED           (0x01)
#define MPI2_SAS_DEVICE0_ASTATUS_SATA_CAPABILITY_FAILED     (0x02)
#define MPI2_SAS_DEVICE0_ASTATUS_SATA_AFFILIATION_CONFLICT  (0x03)
#define MPI2_SAS_DEVICE0_ASTATUS_SATA_NEEDS_INITIALIZATION  (0x04)
#define MPI2_SAS_DEVICE0_ASTATUS_ROUTE_NOT_ADDRESSABLE      (0x05)
#define MPI2_SAS_DEVICE0_ASTATUS_SMP_ERROR_NOT_ADDRESSABLE  (0x06)
#define MPI2_SAS_DEVICE0_ASTATUS_DEVICE_BLOCKED             (0x07)
/*specific values for SATA Init failures */
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_UNKNOWN                (0x10)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_AFFILIATION_CONFLICT   (0x11)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_DIAG                   (0x12)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_IDENTIFICATION         (0x13)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_CHECK_POWER            (0x14)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_PIO_SN                 (0x15)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_MDMA_SN                (0x16)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_UDMA_SN                (0x17)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_ZONING_VIOLATION       (0x18)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_NOT_ADDRESSABLE        (0x19)
#define MPI2_SAS_DEVICE0_ASTATUS_SIF_MAX                    (0x1F)

/*see mpi2_sas.h for values for SAS Device Page 0 DeviceInfo values */

/*values for SAS Device Page 0 Flags field */
#define MPI2_SAS_DEVICE0_FLAGS_UNAUTHORIZED_DEVICE          (0x8000)
#define MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH           (0x4000)
#define MPI25_SAS_DEVICE0_FLAGS_FAST_PATH_CAPABLE           (0x2000)
#define MPI2_SAS_DEVICE0_FLAGS_SLUMBER_PM_CAPABLE           (0x1000)
#define MPI2_SAS_DEVICE0_FLAGS_PARTIAL_PM_CAPABLE           (0x0800)
#define MPI2_SAS_DEVICE0_FLAGS_SATA_ASYNCHRONOUS_NOTIFY     (0x0400)
#define MPI2_SAS_DEVICE0_FLAGS_SATA_SW_PRESERVE             (0x0200)
#define MPI2_SAS_DEVICE0_FLAGS_UNSUPPORTED_DEVICE           (0x0100)
#define MPI2_SAS_DEVICE0_FLAGS_SATA_48BIT_LBA_SUPPORTED     (0x0080)
#define MPI2_SAS_DEVICE0_FLAGS_SATA_SMART_SUPPORTED         (0x0040)
#define MPI2_SAS_DEVICE0_FLAGS_SATA_NCQ_SUPPORTED           (0x0020)
#define MPI2_SAS_DEVICE0_FLAGS_SATA_FUA_SUPPORTED           (0x0010)
#define MPI2_SAS_DEVICE0_FLAGS_PORT_SELECTOR_ATTACH         (0x0008)
#define MPI2_SAS_DEVICE0_FLAGS_PERSIST_CAPABLE              (0x0004)
#define MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID             (0x0002)
#define MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT               (0x0001)


/*SAS Device Page 1 */

typedef struct _MPI2_CONFIG_PAGE_SAS_DEV_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                 /*0x00 */
	U32
		Reserved1;              /*0x08 */
	U64
		SASAddress;             /*0x0C */
	U32
		Reserved2;              /*0x14 */
	U16
		DevHandle;              /*0x18 */
	U16
		Reserved3;              /*0x1A */
	U8
		InitialRegDeviceFIS[20];/*0x1C */
} MPI2_CONFIG_PAGE_SAS_DEV_1,
	*PTR_MPI2_CONFIG_PAGE_SAS_DEV_1,
	Mpi2SasDevicePage1_t,
	*pMpi2SasDevicePage1_t;

#define MPI2_SASDEVICE1_PAGEVERSION         (0x01)


/****************************************************************************
*  SAS PHY Config Pages
****************************************************************************/

/*SAS PHY Page 0 */

typedef struct _MPI2_CONFIG_PAGE_SAS_PHY_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                 /*0x00 */
	U16
		OwnerDevHandle;         /*0x08 */
	U16
		Reserved1;              /*0x0A */
	U16
		AttachedDevHandle;      /*0x0C */
	U8
		AttachedPhyIdentifier;  /*0x0E */
	U8
		Reserved2;              /*0x0F */
	U32
		AttachedPhyInfo;        /*0x10 */
	U8
		ProgrammedLinkRate;     /*0x14 */
	U8
		HwLinkRate;             /*0x15 */
	U8
		ChangeCount;            /*0x16 */
	U8
		Flags;                  /*0x17 */
	U32
		PhyInfo;                /*0x18 */
	U8
		NegotiatedLinkRate;     /*0x1C */
	U8
		Reserved3;              /*0x1D */
	U16
		Reserved4;              /*0x1E */
} MPI2_CONFIG_PAGE_SAS_PHY_0,
	*PTR_MPI2_CONFIG_PAGE_SAS_PHY_0,
	Mpi2SasPhyPage0_t, *pMpi2SasPhyPage0_t;

#define MPI2_SASPHY0_PAGEVERSION            (0x03)

/*use MPI2_SAS_APHYINFO_ defines for AttachedPhyInfo field */

/*use MPI2_SAS_PRATE_ defines for the ProgrammedLinkRate field */

/*use MPI2_SAS_HWRATE_ defines for the HwLinkRate field */

/*values for SAS PHY Page 0 Flags field */
#define MPI2_SAS_PHY0_FLAGS_SGPIO_DIRECT_ATTACH_ENC             (0x01)

/*use MPI2_SAS_PHYINFO_ for the PhyInfo field */

/*use MPI2_SAS_NEG_LINK_RATE_ defines for the NegotiatedLinkRate field */


/*SAS PHY Page 1 */

typedef struct _MPI2_CONFIG_PAGE_SAS_PHY_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U32
		Reserved1;                  /*0x08 */
	U32
		InvalidDwordCount;          /*0x0C */
	U32
		RunningDisparityErrorCount; /*0x10 */
	U32
		LossDwordSynchCount;        /*0x14 */
	U32
		PhyResetProblemCount;       /*0x18 */
} MPI2_CONFIG_PAGE_SAS_PHY_1,
	*PTR_MPI2_CONFIG_PAGE_SAS_PHY_1,
	Mpi2SasPhyPage1_t, *pMpi2SasPhyPage1_t;

#define MPI2_SASPHY1_PAGEVERSION            (0x01)


/*SAS PHY Page 2 */

typedef struct _MPI2_SASPHY2_PHY_EVENT {
	U8          PhyEventCode;       /*0x00 */
	U8          Reserved1;          /*0x01 */
	U16         Reserved2;          /*0x02 */
	U32         PhyEventInfo;       /*0x04 */
} MPI2_SASPHY2_PHY_EVENT, *PTR_MPI2_SASPHY2_PHY_EVENT,
	Mpi2SasPhy2PhyEvent_t, *pMpi2SasPhy2PhyEvent_t;

/*use MPI2_SASPHY3_EVENT_CODE_ for the PhyEventCode field */


/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhyEvents at runtime.
 */
#ifndef MPI2_SASPHY2_PHY_EVENT_MAX
#define MPI2_SASPHY2_PHY_EVENT_MAX      (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SAS_PHY_2 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U32
		Reserved1;                  /*0x08 */
	U8
		NumPhyEvents;               /*0x0C */
	U8
		Reserved2;                  /*0x0D */
	U16
		Reserved3;                  /*0x0E */
	MPI2_SASPHY2_PHY_EVENT
		PhyEvent[MPI2_SASPHY2_PHY_EVENT_MAX]; /*0x10 */
} MPI2_CONFIG_PAGE_SAS_PHY_2,
	*PTR_MPI2_CONFIG_PAGE_SAS_PHY_2,
	Mpi2SasPhyPage2_t,
	*pMpi2SasPhyPage2_t;

#define MPI2_SASPHY2_PAGEVERSION            (0x00)


/*SAS PHY Page 3 */

typedef struct _MPI2_SASPHY3_PHY_EVENT_CONFIG {
	U8          PhyEventCode;       /*0x00 */
	U8          Reserved1;          /*0x01 */
	U16         Reserved2;          /*0x02 */
	U8          CounterType;        /*0x04 */
	U8          ThresholdWindow;    /*0x05 */
	U8          TimeUnits;          /*0x06 */
	U8          Reserved3;          /*0x07 */
	U32         EventThreshold;     /*0x08 */
	U16         ThresholdFlags;     /*0x0C */
	U16         Reserved4;          /*0x0E */
} MPI2_SASPHY3_PHY_EVENT_CONFIG,
	*PTR_MPI2_SASPHY3_PHY_EVENT_CONFIG,
	Mpi2SasPhy3PhyEventConfig_t,
	*pMpi2SasPhy3PhyEventConfig_t;

/*values for PhyEventCode field */
#define MPI2_SASPHY3_EVENT_CODE_NO_EVENT                    (0x00)
#define MPI2_SASPHY3_EVENT_CODE_INVALID_DWORD               (0x01)
#define MPI2_SASPHY3_EVENT_CODE_RUNNING_DISPARITY_ERROR     (0x02)
#define MPI2_SASPHY3_EVENT_CODE_LOSS_DWORD_SYNC             (0x03)
#define MPI2_SASPHY3_EVENT_CODE_PHY_RESET_PROBLEM           (0x04)
#define MPI2_SASPHY3_EVENT_CODE_ELASTICITY_BUF_OVERFLOW     (0x05)
#define MPI2_SASPHY3_EVENT_CODE_RX_ERROR                    (0x06)
#define MPI2_SASPHY3_EVENT_CODE_RX_ADDR_FRAME_ERROR         (0x20)
#define MPI2_SASPHY3_EVENT_CODE_TX_AC_OPEN_REJECT           (0x21)
#define MPI2_SASPHY3_EVENT_CODE_RX_AC_OPEN_REJECT           (0x22)
#define MPI2_SASPHY3_EVENT_CODE_TX_RC_OPEN_REJECT           (0x23)
#define MPI2_SASPHY3_EVENT_CODE_RX_RC_OPEN_REJECT           (0x24)
#define MPI2_SASPHY3_EVENT_CODE_RX_AIP_PARTIAL_WAITING_ON   (0x25)
#define MPI2_SASPHY3_EVENT_CODE_RX_AIP_CONNECT_WAITING_ON   (0x26)
#define MPI2_SASPHY3_EVENT_CODE_TX_BREAK                    (0x27)
#define MPI2_SASPHY3_EVENT_CODE_RX_BREAK                    (0x28)
#define MPI2_SASPHY3_EVENT_CODE_BREAK_TIMEOUT               (0x29)
#define MPI2_SASPHY3_EVENT_CODE_CONNECTION                  (0x2A)
#define MPI2_SASPHY3_EVENT_CODE_PEAKTX_PATHWAY_BLOCKED      (0x2B)
#define MPI2_SASPHY3_EVENT_CODE_PEAKTX_ARB_WAIT_TIME        (0x2C)
#define MPI2_SASPHY3_EVENT_CODE_PEAK_ARB_WAIT_TIME          (0x2D)
#define MPI2_SASPHY3_EVENT_CODE_PEAK_CONNECT_TIME           (0x2E)
#define MPI2_SASPHY3_EVENT_CODE_TX_SSP_FRAMES               (0x40)
#define MPI2_SASPHY3_EVENT_CODE_RX_SSP_FRAMES               (0x41)
#define MPI2_SASPHY3_EVENT_CODE_TX_SSP_ERROR_FRAMES         (0x42)
#define MPI2_SASPHY3_EVENT_CODE_RX_SSP_ERROR_FRAMES         (0x43)
#define MPI2_SASPHY3_EVENT_CODE_TX_CREDIT_BLOCKED           (0x44)
#define MPI2_SASPHY3_EVENT_CODE_RX_CREDIT_BLOCKED           (0x45)
#define MPI2_SASPHY3_EVENT_CODE_TX_SATA_FRAMES              (0x50)
#define MPI2_SASPHY3_EVENT_CODE_RX_SATA_FRAMES              (0x51)
#define MPI2_SASPHY3_EVENT_CODE_SATA_OVERFLOW               (0x52)
#define MPI2_SASPHY3_EVENT_CODE_TX_SMP_FRAMES               (0x60)
#define MPI2_SASPHY3_EVENT_CODE_RX_SMP_FRAMES               (0x61)
#define MPI2_SASPHY3_EVENT_CODE_RX_SMP_ERROR_FRAMES         (0x63)
#define MPI2_SASPHY3_EVENT_CODE_HOTPLUG_TIMEOUT             (0xD0)
#define MPI2_SASPHY3_EVENT_CODE_MISALIGNED_MUX_PRIMITIVE    (0xD1)
#define MPI2_SASPHY3_EVENT_CODE_RX_AIP                      (0xD2)

/*Following codes are product specific and in MPI v2.6 and later */
#define MPI2_SASPHY3_EVENT_CODE_LCARB_WAIT_TIME		    (0xD3)
#define MPI2_SASPHY3_EVENT_CODE_RCVD_CONN_RESP_WAIT_TIME    (0xD4)
#define MPI2_SASPHY3_EVENT_CODE_LCCONN_TIME	            (0xD5)
#define MPI2_SASPHY3_EVENT_CODE_SSP_TX_START_TRANSMIT	    (0xD6)
#define MPI2_SASPHY3_EVENT_CODE_SATA_TX_START	            (0xD7)
#define MPI2_SASPHY3_EVENT_CODE_SMP_TX_START_TRANSMT	    (0xD8)
#define MPI2_SASPHY3_EVENT_CODE_TX_SMP_BREAK_CONN	    (0xD9)
#define MPI2_SASPHY3_EVENT_CODE_SSP_RX_START_RECEIVE	    (0xDA)
#define MPI2_SASPHY3_EVENT_CODE_SATA_RX_START_RECEIVE	    (0xDB)
#define MPI2_SASPHY3_EVENT_CODE_SMP_RX_START_RECEIVE	    (0xDC)


/*values for the CounterType field */
#define MPI2_SASPHY3_COUNTER_TYPE_WRAPPING                  (0x00)
#define MPI2_SASPHY3_COUNTER_TYPE_SATURATING                (0x01)
#define MPI2_SASPHY3_COUNTER_TYPE_PEAK_VALUE                (0x02)

/*values for the TimeUnits field */
#define MPI2_SASPHY3_TIME_UNITS_10_MICROSECONDS             (0x00)
#define MPI2_SASPHY3_TIME_UNITS_100_MICROSECONDS            (0x01)
#define MPI2_SASPHY3_TIME_UNITS_1_MILLISECOND               (0x02)
#define MPI2_SASPHY3_TIME_UNITS_10_MILLISECONDS             (0x03)

/*values for the ThresholdFlags field */
#define MPI2_SASPHY3_TFLAGS_PHY_RESET                       (0x0002)
#define MPI2_SASPHY3_TFLAGS_EVENT_NOTIFY                    (0x0001)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhyEvents at runtime.
 */
#ifndef MPI2_SASPHY3_PHY_EVENT_MAX
#define MPI2_SASPHY3_PHY_EVENT_MAX      (1)
#endif

typedef struct _MPI2_CONFIG_PAGE_SAS_PHY_3 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U32
		Reserved1;                  /*0x08 */
	U8
		NumPhyEvents;               /*0x0C */
	U8
		Reserved2;                  /*0x0D */
	U16
		Reserved3;                  /*0x0E */
	MPI2_SASPHY3_PHY_EVENT_CONFIG
		PhyEventConfig[MPI2_SASPHY3_PHY_EVENT_MAX]; /*0x10 */
} MPI2_CONFIG_PAGE_SAS_PHY_3,
	*PTR_MPI2_CONFIG_PAGE_SAS_PHY_3,
	Mpi2SasPhyPage3_t, *pMpi2SasPhyPage3_t;

#define MPI2_SASPHY3_PAGEVERSION            (0x00)


/*SAS PHY Page 4 */

typedef struct _MPI2_CONFIG_PAGE_SAS_PHY_4 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U16
		Reserved1;                  /*0x08 */
	U8
		Reserved2;                  /*0x0A */
	U8
		Flags;                      /*0x0B */
	U8
		InitialFrame[28];           /*0x0C */
} MPI2_CONFIG_PAGE_SAS_PHY_4,
	*PTR_MPI2_CONFIG_PAGE_SAS_PHY_4,
	Mpi2SasPhyPage4_t, *pMpi2SasPhyPage4_t;

#define MPI2_SASPHY4_PAGEVERSION            (0x00)

/*values for the Flags field */
#define MPI2_SASPHY4_FLAGS_FRAME_VALID        (0x02)
#define MPI2_SASPHY4_FLAGS_SATA_FRAME         (0x01)




/****************************************************************************
*  SAS Port Config Pages
****************************************************************************/

/*SAS Port Page 0 */

typedef struct _MPI2_CONFIG_PAGE_SAS_PORT_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                     /*0x00 */
	U8
		PortNumber;                 /*0x08 */
	U8
		PhysicalPort;               /*0x09 */
	U8
		PortWidth;                  /*0x0A */
	U8
		PhysicalPortWidth;          /*0x0B */
	U8
		ZoneGroup;                  /*0x0C */
	U8
		Reserved1;                  /*0x0D */
	U16
		Reserved2;                  /*0x0E */
	U64
		SASAddress;                 /*0x10 */
	U32
		DeviceInfo;                 /*0x18 */
	U32
		Reserved3;                  /*0x1C */
	U32
		Reserved4;                  /*0x20 */
} MPI2_CONFIG_PAGE_SAS_PORT_0,
	*PTR_MPI2_CONFIG_PAGE_SAS_PORT_0,
	Mpi2SasPortPage0_t, *pMpi2SasPortPage0_t;

#define MPI2_SASPORT0_PAGEVERSION           (0x00)

/*see mpi2_sas.h for values for SAS Port Page 0 DeviceInfo values */


/****************************************************************************
*  SAS Enclosure Config Pages
****************************************************************************/

/*SAS Enclosure Page 0 */

typedef struct _MPI2_CONFIG_PAGE_SAS_ENCLOSURE_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U32	Reserved1;			/*0x08 */
	U64	EnclosureLogicalID;		/*0x0C */
	U16	Flags;				/*0x14 */
	U16	EnclosureHandle;		/*0x16 */
	U16	NumSlots;			/*0x18 */
	U16	StartSlot;			/*0x1A */
	U8	ChassisSlot;			/*0x1C */
	U8	EnclosureLevel;			/*0x1D */
	U16	SEPDevHandle;			/*0x1E */
	U8	OEMRD;				/*0x20 */
	U8	Reserved1a;			/*0x21 */
	U16	Reserved2;			/*0x22 */
	U32	Reserved3;			/*0x24 */
} MPI2_CONFIG_PAGE_SAS_ENCLOSURE_0,
	*PTR_MPI2_CONFIG_PAGE_SAS_ENCLOSURE_0,
	Mpi2SasEnclosurePage0_t, *pMpi2SasEnclosurePage0_t,
	MPI26_CONFIG_PAGE_ENCLOSURE_0,
	*PTR_MPI26_CONFIG_PAGE_ENCLOSURE_0,
	Mpi26EnclosurePage0_t, *pMpi26EnclosurePage0_t;

#define MPI2_SASENCLOSURE0_PAGEVERSION      (0x04)

/*values for SAS Enclosure Page 0 Flags field */
#define MPI26_SAS_ENCLS0_FLAGS_OEMRD_VALID          (0x0080)
#define MPI26_SAS_ENCLS0_FLAGS_OEMRD_COLLECTING     (0x0040)
#define MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID    (0x0020)
#define MPI2_SAS_ENCLS0_FLAGS_ENCL_LEVEL_VALID      (0x0010)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_MASK              (0x000F)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_UNKNOWN           (0x0000)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_IOC_SES           (0x0001)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_IOC_SGPIO         (0x0002)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_EXP_SGPIO         (0x0003)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_SES_ENCLOSURE     (0x0004)
#define MPI2_SAS_ENCLS0_FLAGS_MNG_IOC_GPIO          (0x0005)

#define MPI26_ENCLOSURE0_PAGEVERSION        (0x04)

/*Values for Enclosure Page 0 Flags field */
#define MPI26_ENCLS0_FLAGS_OEMRD_VALID              (0x0080)
#define MPI26_ENCLS0_FLAGS_OEMRD_COLLECTING         (0x0040)
#define MPI26_ENCLS0_FLAGS_CHASSIS_SLOT_VALID       (0x0020)
#define MPI26_ENCLS0_FLAGS_ENCL_LEVEL_VALID         (0x0010)
#define MPI26_ENCLS0_FLAGS_MNG_MASK                 (0x000F)
#define MPI26_ENCLS0_FLAGS_MNG_UNKNOWN              (0x0000)
#define MPI26_ENCLS0_FLAGS_MNG_IOC_SES              (0x0001)
#define MPI26_ENCLS0_FLAGS_MNG_IOC_SGPIO            (0x0002)
#define MPI26_ENCLS0_FLAGS_MNG_EXP_SGPIO            (0x0003)
#define MPI26_ENCLS0_FLAGS_MNG_SES_ENCLOSURE        (0x0004)
#define MPI26_ENCLS0_FLAGS_MNG_IOC_GPIO             (0x0005)

/****************************************************************************
*  Log Config Page
****************************************************************************/

/*Log Page 0 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumLogEntries at runtime.
 */
#ifndef MPI2_LOG_0_NUM_LOG_ENTRIES
#define MPI2_LOG_0_NUM_LOG_ENTRIES          (1)
#endif

#define MPI2_LOG_0_LOG_DATA_LENGTH          (0x1C)

typedef struct _MPI2_LOG_0_ENTRY {
	U64         TimeStamp;                      /*0x00 */
	U32         Reserved1;                      /*0x08 */
	U16         LogSequence;                    /*0x0C */
	U16         LogEntryQualifier;              /*0x0E */
	U8          VP_ID;                          /*0x10 */
	U8          VF_ID;                          /*0x11 */
	U16         Reserved2;                      /*0x12 */
	U8
		LogData[MPI2_LOG_0_LOG_DATA_LENGTH];/*0x14 */
} MPI2_LOG_0_ENTRY, *PTR_MPI2_LOG_0_ENTRY,
	Mpi2Log0Entry_t, *pMpi2Log0Entry_t;

/*values for Log Page 0 LogEntry LogEntryQualifier field */
#define MPI2_LOG_0_ENTRY_QUAL_ENTRY_UNUSED          (0x0000)
#define MPI2_LOG_0_ENTRY_QUAL_POWER_ON_RESET        (0x0001)
#define MPI2_LOG_0_ENTRY_QUAL_TIMESTAMP_UPDATE      (0x0002)
#define MPI2_LOG_0_ENTRY_QUAL_MIN_IMPLEMENT_SPEC    (0x8000)
#define MPI2_LOG_0_ENTRY_QUAL_MAX_IMPLEMENT_SPEC    (0xFFFF)

typedef struct _MPI2_CONFIG_PAGE_LOG_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;       /*0x00 */
	U32                                 Reserved1;    /*0x08 */
	U32                                 Reserved2;    /*0x0C */
	U16                                 NumLogEntries;/*0x10 */
	U16                                 Reserved3;    /*0x12 */
	MPI2_LOG_0_ENTRY
		LogEntry[MPI2_LOG_0_NUM_LOG_ENTRIES]; /*0x14 */
} MPI2_CONFIG_PAGE_LOG_0, *PTR_MPI2_CONFIG_PAGE_LOG_0,
	Mpi2LogPage0_t, *pMpi2LogPage0_t;

#define MPI2_LOG_0_PAGEVERSION              (0x02)


/****************************************************************************
*  RAID Config Page
****************************************************************************/

/*RAID Page 0 */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumElements at runtime.
 */
#ifndef MPI2_RAIDCONFIG0_MAX_ELEMENTS
#define MPI2_RAIDCONFIG0_MAX_ELEMENTS       (1)
#endif

typedef struct _MPI2_RAIDCONFIG0_CONFIG_ELEMENT {
	U16                     ElementFlags;             /*0x00 */
	U16                     VolDevHandle;             /*0x02 */
	U8                      HotSparePool;             /*0x04 */
	U8                      PhysDiskNum;              /*0x05 */
	U16                     PhysDiskDevHandle;        /*0x06 */
} MPI2_RAIDCONFIG0_CONFIG_ELEMENT,
	*PTR_MPI2_RAIDCONFIG0_CONFIG_ELEMENT,
	Mpi2RaidConfig0ConfigElement_t,
	*pMpi2RaidConfig0ConfigElement_t;

/*values for the ElementFlags field */
#define MPI2_RAIDCONFIG0_EFLAGS_MASK_ELEMENT_TYPE       (0x000F)
#define MPI2_RAIDCONFIG0_EFLAGS_VOLUME_ELEMENT          (0x0000)
#define MPI2_RAIDCONFIG0_EFLAGS_VOL_PHYS_DISK_ELEMENT   (0x0001)
#define MPI2_RAIDCONFIG0_EFLAGS_HOT_SPARE_ELEMENT       (0x0002)
#define MPI2_RAIDCONFIG0_EFLAGS_OCE_ELEMENT             (0x0003)


typedef struct _MPI2_CONFIG_PAGE_RAID_CONFIGURATION_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;         /*0x00 */
	U8                                  NumHotSpares;   /*0x08 */
	U8                                  NumPhysDisks;   /*0x09 */
	U8                                  NumVolumes;     /*0x0A */
	U8                                  ConfigNum;      /*0x0B */
	U32                                 Flags;          /*0x0C */
	U8                                  ConfigGUID[24]; /*0x10 */
	U32                                 Reserved1;      /*0x28 */
	U8                                  NumElements;    /*0x2C */
	U8                                  Reserved2;      /*0x2D */
	U16                                 Reserved3;      /*0x2E */
	MPI2_RAIDCONFIG0_CONFIG_ELEMENT
		ConfigElement[MPI2_RAIDCONFIG0_MAX_ELEMENTS]; /*0x30 */
} MPI2_CONFIG_PAGE_RAID_CONFIGURATION_0,
	*PTR_MPI2_CONFIG_PAGE_RAID_CONFIGURATION_0,
	Mpi2RaidConfigurationPage0_t,
	*pMpi2RaidConfigurationPage0_t;

#define MPI2_RAIDCONFIG0_PAGEVERSION            (0x00)

/*values for RAID Configuration Page 0 Flags field */
#define MPI2_RAIDCONFIG0_FLAG_FOREIGN_CONFIG        (0x00000001)


/****************************************************************************
*  Driver Persistent Mapping Config Pages
****************************************************************************/

/*Driver Persistent Mapping Page 0 */

typedef struct _MPI2_CONFIG_PAGE_DRIVER_MAP0_ENTRY {
	U64	PhysicalIdentifier;         /*0x00 */
	U16	MappingInformation;         /*0x08 */
	U16	DeviceIndex;                /*0x0A */
	U32	PhysicalBitsMapping;        /*0x0C */
	U32	Reserved1;                  /*0x10 */
} MPI2_CONFIG_PAGE_DRIVER_MAP0_ENTRY,
	*PTR_MPI2_CONFIG_PAGE_DRIVER_MAP0_ENTRY,
	Mpi2DriverMap0Entry_t, *pMpi2DriverMap0Entry_t;

typedef struct _MPI2_CONFIG_PAGE_DRIVER_MAPPING_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header; /*0x00 */
	MPI2_CONFIG_PAGE_DRIVER_MAP0_ENTRY  Entry;  /*0x08 */
} MPI2_CONFIG_PAGE_DRIVER_MAPPING_0,
	*PTR_MPI2_CONFIG_PAGE_DRIVER_MAPPING_0,
	Mpi2DriverMappingPage0_t, *pMpi2DriverMappingPage0_t;

#define MPI2_DRIVERMAPPING0_PAGEVERSION         (0x00)

/*values for Driver Persistent Mapping Page 0 MappingInformation field */
#define MPI2_DRVMAP0_MAPINFO_SLOT_MASK              (0x07F0)
#define MPI2_DRVMAP0_MAPINFO_SLOT_SHIFT             (4)
#define MPI2_DRVMAP0_MAPINFO_MISSING_MASK           (0x000F)


/****************************************************************************
*  Ethernet Config Pages
****************************************************************************/

/*Ethernet Page 0 */

/*IP address (union of IPv4 and IPv6) */
typedef union _MPI2_ETHERNET_IP_ADDR {
	U32     IPv4Addr;
	U32     IPv6Addr[4];
} MPI2_ETHERNET_IP_ADDR, *PTR_MPI2_ETHERNET_IP_ADDR,
	Mpi2EthernetIpAddr_t, *pMpi2EthernetIpAddr_t;

#define MPI2_ETHERNET_HOST_NAME_LENGTH          (32)

typedef struct _MPI2_CONFIG_PAGE_ETHERNET_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER    Header;          /*0x00 */
	U8                                  NumInterfaces;   /*0x08 */
	U8                                  Reserved0;       /*0x09 */
	U16                                 Reserved1;       /*0x0A */
	U32                                 Status;          /*0x0C */
	U8                                  MediaState;      /*0x10 */
	U8                                  Reserved2;       /*0x11 */
	U16                                 Reserved3;       /*0x12 */
	U8                                  MacAddress[6];   /*0x14 */
	U8                                  Reserved4;       /*0x1A */
	U8                                  Reserved5;       /*0x1B */
	MPI2_ETHERNET_IP_ADDR               IpAddress;       /*0x1C */
	MPI2_ETHERNET_IP_ADDR               SubnetMask;      /*0x2C */
	MPI2_ETHERNET_IP_ADDR               GatewayIpAddress;/*0x3C */
	MPI2_ETHERNET_IP_ADDR               DNS1IpAddress;   /*0x4C */
	MPI2_ETHERNET_IP_ADDR               DNS2IpAddress;   /*0x5C */
	MPI2_ETHERNET_IP_ADDR               DhcpIpAddress;   /*0x6C */
	U8
		HostName[MPI2_ETHERNET_HOST_NAME_LENGTH];/*0x7C */
} MPI2_CONFIG_PAGE_ETHERNET_0,
	*PTR_MPI2_CONFIG_PAGE_ETHERNET_0,
	Mpi2EthernetPage0_t, *pMpi2EthernetPage0_t;

#define MPI2_ETHERNETPAGE0_PAGEVERSION   (0x00)

/*values for Ethernet Page 0 Status field */
#define MPI2_ETHPG0_STATUS_IPV6_CAPABLE             (0x80000000)
#define MPI2_ETHPG0_STATUS_IPV4_CAPABLE             (0x40000000)
#define MPI2_ETHPG0_STATUS_CONSOLE_CONNECTED        (0x20000000)
#define MPI2_ETHPG0_STATUS_DEFAULT_IF               (0x00000100)
#define MPI2_ETHPG0_STATUS_FW_DWNLD_ENABLED         (0x00000080)
#define MPI2_ETHPG0_STATUS_TELNET_ENABLED           (0x00000040)
#define MPI2_ETHPG0_STATUS_SSH2_ENABLED             (0x00000020)
#define MPI2_ETHPG0_STATUS_DHCP_CLIENT_ENABLED      (0x00000010)
#define MPI2_ETHPG0_STATUS_IPV6_ENABLED             (0x00000008)
#define MPI2_ETHPG0_STATUS_IPV4_ENABLED             (0x00000004)
#define MPI2_ETHPG0_STATUS_IPV6_ADDRESSES           (0x00000002)
#define MPI2_ETHPG0_STATUS_ETH_IF_ENABLED           (0x00000001)

/*values for Ethernet Page 0 MediaState field */
#define MPI2_ETHPG0_MS_DUPLEX_MASK                  (0x80)
#define MPI2_ETHPG0_MS_HALF_DUPLEX                  (0x00)
#define MPI2_ETHPG0_MS_FULL_DUPLEX                  (0x80)

#define MPI2_ETHPG0_MS_CONNECT_SPEED_MASK           (0x07)
#define MPI2_ETHPG0_MS_NOT_CONNECTED                (0x00)
#define MPI2_ETHPG0_MS_10MBIT                       (0x01)
#define MPI2_ETHPG0_MS_100MBIT                      (0x02)
#define MPI2_ETHPG0_MS_1GBIT                        (0x03)


/*Ethernet Page 1 */

typedef struct _MPI2_CONFIG_PAGE_ETHERNET_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                 /*0x00 */
	U32
		Reserved0;              /*0x08 */
	U32
		Flags;                  /*0x0C */
	U8
		MediaState;             /*0x10 */
	U8
		Reserved1;              /*0x11 */
	U16
		Reserved2;              /*0x12 */
	U8
		MacAddress[6];          /*0x14 */
	U8
		Reserved3;              /*0x1A */
	U8
		Reserved4;              /*0x1B */
	MPI2_ETHERNET_IP_ADDR
		StaticIpAddress;        /*0x1C */
	MPI2_ETHERNET_IP_ADDR
		StaticSubnetMask;       /*0x2C */
	MPI2_ETHERNET_IP_ADDR
		StaticGatewayIpAddress; /*0x3C */
	MPI2_ETHERNET_IP_ADDR
		StaticDNS1IpAddress;    /*0x4C */
	MPI2_ETHERNET_IP_ADDR
		StaticDNS2IpAddress;    /*0x5C */
	U32
		Reserved5;              /*0x6C */
	U32
		Reserved6;              /*0x70 */
	U32
		Reserved7;              /*0x74 */
	U32
		Reserved8;              /*0x78 */
	U8
		HostName[MPI2_ETHERNET_HOST_NAME_LENGTH];/*0x7C */
} MPI2_CONFIG_PAGE_ETHERNET_1,
	*PTR_MPI2_CONFIG_PAGE_ETHERNET_1,
	Mpi2EthernetPage1_t, *pMpi2EthernetPage1_t;

#define MPI2_ETHERNETPAGE1_PAGEVERSION   (0x00)

/*values for Ethernet Page 1 Flags field */
#define MPI2_ETHPG1_FLAG_SET_DEFAULT_IF             (0x00000100)
#define MPI2_ETHPG1_FLAG_ENABLE_FW_DOWNLOAD         (0x00000080)
#define MPI2_ETHPG1_FLAG_ENABLE_TELNET              (0x00000040)
#define MPI2_ETHPG1_FLAG_ENABLE_SSH2                (0x00000020)
#define MPI2_ETHPG1_FLAG_ENABLE_DHCP_CLIENT         (0x00000010)
#define MPI2_ETHPG1_FLAG_ENABLE_IPV6                (0x00000008)
#define MPI2_ETHPG1_FLAG_ENABLE_IPV4                (0x00000004)
#define MPI2_ETHPG1_FLAG_USE_IPV6_ADDRESSES         (0x00000002)
#define MPI2_ETHPG1_FLAG_ENABLE_ETH_IF              (0x00000001)

/*values for Ethernet Page 1 MediaState field */
#define MPI2_ETHPG1_MS_DUPLEX_MASK                  (0x80)
#define MPI2_ETHPG1_MS_HALF_DUPLEX                  (0x00)
#define MPI2_ETHPG1_MS_FULL_DUPLEX                  (0x80)

#define MPI2_ETHPG1_MS_DATA_RATE_MASK               (0x07)
#define MPI2_ETHPG1_MS_DATA_RATE_AUTO               (0x00)
#define MPI2_ETHPG1_MS_DATA_RATE_10MBIT             (0x01)
#define MPI2_ETHPG1_MS_DATA_RATE_100MBIT            (0x02)
#define MPI2_ETHPG1_MS_DATA_RATE_1GBIT              (0x03)


/****************************************************************************
*  Extended Manufacturing Config Pages
****************************************************************************/

/*
 *Generic structure to use for product-specific extended manufacturing pages
 *(currently Extended Manufacturing Page 40 through Extended Manufacturing
 *Page 60).
 */

typedef struct _MPI2_CONFIG_PAGE_EXT_MAN_PS {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER
		Header;                 /*0x00 */
	U32
		ProductSpecificInfo;    /*0x08 */
} MPI2_CONFIG_PAGE_EXT_MAN_PS,
	*PTR_MPI2_CONFIG_PAGE_EXT_MAN_PS,
	Mpi2ExtManufacturingPagePS_t,
	*pMpi2ExtManufacturingPagePS_t;

/*PageVersion should be provided by product-specific code */



/****************************************************************************
*  values for fields used by several types of PCIe Config Pages
****************************************************************************/

/*values for NegotiatedLinkRates fields */
#define MPI26_PCIE_NEG_LINK_RATE_MASK_PHYSICAL          (0x0F)
/*link rates used for Negotiated Physical Link Rate */
#define MPI26_PCIE_NEG_LINK_RATE_UNKNOWN                (0x00)
#define MPI26_PCIE_NEG_LINK_RATE_PHY_DISABLED           (0x01)
#define MPI26_PCIE_NEG_LINK_RATE_2_5                    (0x02)
#define MPI26_PCIE_NEG_LINK_RATE_5_0                    (0x03)
#define MPI26_PCIE_NEG_LINK_RATE_8_0                    (0x04)
#define MPI26_PCIE_NEG_LINK_RATE_16_0                   (0x05)


/****************************************************************************
*  PCIe IO Unit Config Pages (MPI v2.6 and later)
****************************************************************************/

/*PCIe IO Unit Page 0 */

typedef struct _MPI26_PCIE_IO_UNIT0_PHY_DATA {
	U8	Link;                   /*0x00 */
	U8	LinkFlags;              /*0x01 */
	U8	PhyFlags;               /*0x02 */
	U8	NegotiatedLinkRate;     /*0x03 */
	U32	ControllerPhyDeviceInfo;/*0x04 */
	U16	AttachedDevHandle;      /*0x08 */
	U16	ControllerDevHandle;    /*0x0A */
	U32	EnumerationStatus;      /*0x0C */
	U32	Reserved1;              /*0x10 */
} MPI26_PCIE_IO_UNIT0_PHY_DATA,
	*PTR_MPI26_PCIE_IO_UNIT0_PHY_DATA,
	Mpi26PCIeIOUnit0PhyData_t, *pMpi26PCIeIOUnit0PhyData_t;

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI26_PCIE_IOUNIT0_PHY_MAX
#define MPI26_PCIE_IOUNIT0_PHY_MAX      (1)
#endif

typedef struct _MPI26_CONFIG_PAGE_PIOUNIT_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header; /*0x00 */
	U32	Reserved1;                              /*0x08 */
	U8	NumPhys;                                /*0x0C */
	U8	InitStatus;                             /*0x0D */
	U16	Reserved3;                              /*0x0E */
	MPI26_PCIE_IO_UNIT0_PHY_DATA
		PhyData[MPI26_PCIE_IOUNIT0_PHY_MAX];    /*0x10 */
} MPI26_CONFIG_PAGE_PIOUNIT_0,
	*PTR_MPI26_CONFIG_PAGE_PIOUNIT_0,
	Mpi26PCIeIOUnitPage0_t, *pMpi26PCIeIOUnitPage0_t;

#define MPI26_PCIEIOUNITPAGE0_PAGEVERSION                   (0x00)

/*values for PCIe IO Unit Page 0 LinkFlags */
#define MPI26_PCIEIOUNIT0_LINKFLAGS_ENUMERATION_IN_PROGRESS (0x08)

/*values for PCIe IO Unit Page 0 PhyFlags */
#define MPI26_PCIEIOUNIT0_PHYFLAGS_PHY_DISABLED             (0x08)

/*use MPI26_PCIE_NEG_LINK_RATE_ defines for the NegotiatedLinkRate field */

/*see mpi2_pci.h for values for PCIe IO Unit Page 0 ControllerPhyDeviceInfo
 *values
 */

/*values for PCIe IO Unit Page 0 EnumerationStatus */
#define MPI26_PCIEIOUNIT0_ES_MAX_SWITCHES_EXCEEDED          (0x40000000)
#define MPI26_PCIEIOUNIT0_ES_MAX_DEVICES_EXCEEDED           (0x20000000)


/*PCIe IO Unit Page 1 */

typedef struct _MPI26_PCIE_IO_UNIT1_PHY_DATA {
	U8	Link;                       /*0x00 */
	U8	LinkFlags;                  /*0x01 */
	U8	PhyFlags;                   /*0x02 */
	U8	MaxMinLinkRate;             /*0x03 */
	U32	ControllerPhyDeviceInfo;    /*0x04 */
	U32	Reserved1;                  /*0x08 */
} MPI26_PCIE_IO_UNIT1_PHY_DATA,
	*PTR_MPI26_PCIE_IO_UNIT1_PHY_DATA,
	Mpi26PCIeIOUnit1PhyData_t, *pMpi26PCIeIOUnit1PhyData_t;

/*values for LinkFlags */
#define MPI26_PCIEIOUNIT1_LINKFLAGS_DIS_SEPARATE_REFCLK     (0x00)
#define MPI26_PCIEIOUNIT1_LINKFLAGS_SRIS_EN                 (0x01)
#define MPI26_PCIEIOUNIT1_LINKFLAGS_SRNS_EN                 (0x02)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumPhys at runtime.
 */
#ifndef MPI26_PCIE_IOUNIT1_PHY_MAX
#define MPI26_PCIE_IOUNIT1_PHY_MAX      (1)
#endif

typedef struct _MPI26_CONFIG_PAGE_PIOUNIT_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U16	ControlFlags;                       /*0x08 */
	U16	Reserved;                           /*0x0A */
	U16	AdditionalControlFlags;             /*0x0C */
	U16	NVMeMaxQueueDepth;                  /*0x0E */
	U8	NumPhys;                            /*0x10 */
	U8	DMDReportPCIe;                      /*0x11 */
	U16	Reserved2;                          /*0x12 */
	MPI26_PCIE_IO_UNIT1_PHY_DATA
		PhyData[MPI26_PCIE_IOUNIT1_PHY_MAX];/*0x14 */
} MPI26_CONFIG_PAGE_PIOUNIT_1,
	*PTR_MPI26_CONFIG_PAGE_PIOUNIT_1,
	Mpi26PCIeIOUnitPage1_t, *pMpi26PCIeIOUnitPage1_t;

#define MPI26_PCIEIOUNITPAGE1_PAGEVERSION   (0x00)

/*values for PCIe IO Unit Page 1 PhyFlags */
#define MPI26_PCIEIOUNIT1_PHYFLAGS_PHY_DISABLE                      (0x08)
#define MPI26_PCIEIOUNIT1_PHYFLAGS_ENDPOINT_ONLY                    (0x01)

/*values for PCIe IO Unit Page 1 MaxMinLinkRate */
#define MPI26_PCIEIOUNIT1_MAX_RATE_MASK                             (0xF0)
#define MPI26_PCIEIOUNIT1_MAX_RATE_SHIFT                            (4)
#define MPI26_PCIEIOUNIT1_MAX_RATE_2_5                              (0x20)
#define MPI26_PCIEIOUNIT1_MAX_RATE_5_0                              (0x30)
#define MPI26_PCIEIOUNIT1_MAX_RATE_8_0                              (0x40)
#define MPI26_PCIEIOUNIT1_MAX_RATE_16_0                             (0x50)

/*values for PCIe IO Unit Page 1 DMDReportPCIe */
#define MPI26_PCIEIOUNIT1_DMDRPT_UNIT_MASK                          (0x80)
#define MPI26_PCIEIOUNIT1_DMDRPT_UNIT_1_SEC                         (0x00)
#define MPI26_PCIEIOUNIT1_DMDRPT_UNIT_16_SEC                        (0x80)
#define MPI26_PCIEIOUNIT1_DMDRPT_DELAY_TIME_MASK                    (0x7F)

/*see mpi2_pci.h for values for PCIe IO Unit Page 0 ControllerPhyDeviceInfo
 *values
 */


/****************************************************************************
*  PCIe Switch Config Pages (MPI v2.6 and later)
****************************************************************************/

/*PCIe Switch Page 0 */

typedef struct _MPI26_CONFIG_PAGE_PSWITCH_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U8	PhysicalPort;               /*0x08 */
	U8	Reserved1;                  /*0x09 */
	U16	Reserved2;                  /*0x0A */
	U16	DevHandle;                  /*0x0C */
	U16	ParentDevHandle;            /*0x0E */
	U8	NumPorts;                   /*0x10 */
	U8	PCIeLevel;                  /*0x11 */
	U16	Reserved3;                  /*0x12 */
	U32	Reserved4;                  /*0x14 */
	U32	Reserved5;                  /*0x18 */
	U32	Reserved6;                  /*0x1C */
} MPI26_CONFIG_PAGE_PSWITCH_0, *PTR_MPI26_CONFIG_PAGE_PSWITCH_0,
	Mpi26PCIeSwitchPage0_t, *pMpi26PCIeSwitchPage0_t;

#define MPI26_PCIESWITCH0_PAGEVERSION       (0x00)


/*PCIe Switch Page 1 */

typedef struct _MPI26_CONFIG_PAGE_PSWITCH_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U8	PhysicalPort;               /*0x08 */
	U8	Reserved1;                  /*0x09 */
	U16	Reserved2;                  /*0x0A */
	U8	NumPorts;                   /*0x0C */
	U8	PortNum;                    /*0x0D */
	U16	AttachedDevHandle;          /*0x0E */
	U16	SwitchDevHandle;            /*0x10 */
	U8	NegotiatedPortWidth;        /*0x12 */
	U8	NegotiatedLinkRate;         /*0x13 */
	U32	Reserved4;                  /*0x14 */
	U32	Reserved5;                  /*0x18 */
} MPI26_CONFIG_PAGE_PSWITCH_1, *PTR_MPI26_CONFIG_PAGE_PSWITCH_1,
	Mpi26PCIeSwitchPage1_t, *pMpi26PCIeSwitchPage1_t;

#define MPI26_PCIESWITCH1_PAGEVERSION       (0x00)

/*use MPI26_PCIE_NEG_LINK_RATE_ defines for the NegotiatedLinkRate field */

/* defines for the Flags field */
#define MPI26_PCIESWITCH1_2_RETIMER_PRESENCE         (0x0002)
#define MPI26_PCIESWITCH1_RETIMER_PRESENCE           (0x0001)

/****************************************************************************
*  PCIe Device Config Pages (MPI v2.6 and later)
****************************************************************************/

/*PCIe Device Page 0 */

typedef struct _MPI26_CONFIG_PAGE_PCIEDEV_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U16	Slot;                   /*0x08 */
	U16	EnclosureHandle;        /*0x0A */
	U64	WWID;                   /*0x0C */
	U16	ParentDevHandle;        /*0x14 */
	U8	PortNum;                /*0x16 */
	U8	AccessStatus;           /*0x17 */
	U16	DevHandle;              /*0x18 */
	U8	PhysicalPort;           /*0x1A */
	U8	Reserved1;              /*0x1B */
	U32	DeviceInfo;             /*0x1C */
	U32	Flags;                  /*0x20 */
	U8	SupportedLinkRates;     /*0x24 */
	U8	MaxPortWidth;           /*0x25 */
	U8	NegotiatedPortWidth;    /*0x26 */
	U8	NegotiatedLinkRate;     /*0x27 */
	U8	EnclosureLevel;         /*0x28 */
	U8	Reserved2;              /*0x29 */
	U16	Reserved3;              /*0x2A */
	U8	ConnectorName[4];       /*0x2C */
	U32	Reserved4;              /*0x30 */
	U32	Reserved5;              /*0x34 */
} MPI26_CONFIG_PAGE_PCIEDEV_0, *PTR_MPI26_CONFIG_PAGE_PCIEDEV_0,
	Mpi26PCIeDevicePage0_t, *pMpi26PCIeDevicePage0_t;

#define MPI26_PCIEDEVICE0_PAGEVERSION       (0x01)

/*values for PCIe Device Page 0 AccessStatus field */
#define MPI26_PCIEDEV0_ASTATUS_NO_ERRORS                    (0x00)
#define MPI26_PCIEDEV0_ASTATUS_NEEDS_INITIALIZATION         (0x04)
#define MPI26_PCIEDEV0_ASTATUS_CAPABILITY_FAILED            (0x02)
#define MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED               (0x07)
#define MPI26_PCIEDEV0_ASTATUS_MEMORY_SPACE_ACCESS_FAILED   (0x08)
#define MPI26_PCIEDEV0_ASTATUS_UNSUPPORTED_DEVICE           (0x09)
#define MPI26_PCIEDEV0_ASTATUS_MSIX_REQUIRED                (0x0A)
#define MPI26_PCIEDEV0_ASTATUS_UNKNOWN                      (0x10)

#define MPI26_PCIEDEV0_ASTATUS_NVME_READY_TIMEOUT           (0x30)
#define MPI26_PCIEDEV0_ASTATUS_NVME_DEVCFG_UNSUPPORTED      (0x31)
#define MPI26_PCIEDEV0_ASTATUS_NVME_IDENTIFY_FAILED         (0x32)
#define MPI26_PCIEDEV0_ASTATUS_NVME_QCONFIG_FAILED          (0x33)
#define MPI26_PCIEDEV0_ASTATUS_NVME_QCREATION_FAILED        (0x34)
#define MPI26_PCIEDEV0_ASTATUS_NVME_EVENTCFG_FAILED         (0x35)
#define MPI26_PCIEDEV0_ASTATUS_NVME_GET_FEATURE_STAT_FAILED (0x36)
#define MPI26_PCIEDEV0_ASTATUS_NVME_IDLE_TIMEOUT            (0x37)
#define MPI26_PCIEDEV0_ASTATUS_NVME_FAILURE_STATUS          (0x38)

#define MPI26_PCIEDEV0_ASTATUS_INIT_FAIL_MAX                (0x3F)

/*see mpi2_pci.h for the MPI26_PCIE_DEVINFO_ defines used for the DeviceInfo
 *field
 */

/*values for PCIe Device Page 0 Flags field*/
#define MPI26_PCIEDEV0_FLAGS_2_RETIMER_PRESENCE             (0x00020000)
#define MPI26_PCIEDEV0_FLAGS_RETIMER_PRESENCE               (0x00010000)
#define MPI26_PCIEDEV0_FLAGS_UNAUTHORIZED_DEVICE            (0x00008000)
#define MPI26_PCIEDEV0_FLAGS_ENABLED_FAST_PATH              (0x00004000)
#define MPI26_PCIEDEV0_FLAGS_FAST_PATH_CAPABLE              (0x00002000)
#define MPI26_PCIEDEV0_FLAGS_ASYNCHRONOUS_NOTIFICATION      (0x00000400)
#define MPI26_PCIEDEV0_FLAGS_ATA_SW_PRESERVATION            (0x00000200)
#define MPI26_PCIEDEV0_FLAGS_UNSUPPORTED_DEVICE             (0x00000100)
#define MPI26_PCIEDEV0_FLAGS_ATA_48BIT_LBA_SUPPORTED        (0x00000080)
#define MPI26_PCIEDEV0_FLAGS_ATA_SMART_SUPPORTED            (0x00000040)
#define MPI26_PCIEDEV0_FLAGS_ATA_NCQ_SUPPORTED              (0x00000020)
#define MPI26_PCIEDEV0_FLAGS_ATA_FUA_SUPPORTED              (0x00000010)
#define MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID               (0x00000002)
#define MPI26_PCIEDEV0_FLAGS_DEVICE_PRESENT                 (0x00000001)

/* values for PCIe Device Page 0 SupportedLinkRates field */
#define MPI26_PCIEDEV0_LINK_RATE_16_0_SUPPORTED             (0x08)
#define MPI26_PCIEDEV0_LINK_RATE_8_0_SUPPORTED              (0x04)
#define MPI26_PCIEDEV0_LINK_RATE_5_0_SUPPORTED              (0x02)
#define MPI26_PCIEDEV0_LINK_RATE_2_5_SUPPORTED              (0x01)

/*use MPI26_PCIE_NEG_LINK_RATE_ defines for the NegotiatedLinkRate field */


/*PCIe Device Page 2 */

typedef struct _MPI26_CONFIG_PAGE_PCIEDEV_2 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U16	DevHandle;		/*0x08 */
	U8	ControllerResetTO;		/* 0x0A */
	U8	Reserved1;		/* 0x0B */
	U32	MaximumDataTransferSize;	/*0x0C */
	U32	Capabilities;		/*0x10 */
	U16	NOIOB;		/* 0x14 */
	U16	Reserved2;		/* 0x16 */
} MPI26_CONFIG_PAGE_PCIEDEV_2, *PTR_MPI26_CONFIG_PAGE_PCIEDEV_2,
	Mpi26PCIeDevicePage2_t, *pMpi26PCIeDevicePage2_t;

#define MPI26_PCIEDEVICE2_PAGEVERSION       (0x01)

/*defines for PCIe Device Page 2 Capabilities field */
#define MPI26_PCIEDEV2_CAP_DATA_BLK_ALIGN_AND_GRAN     (0x00000008)
#define MPI26_PCIEDEV2_CAP_SGL_FORMAT                  (0x00000004)
#define MPI26_PCIEDEV2_CAP_BIT_BUCKET_SUPPORT          (0x00000002)
#define MPI26_PCIEDEV2_CAP_SGL_SUPPORT                 (0x00000001)

/* Defines for the NOIOB field */
#define MPI26_PCIEDEV2_NOIOB_UNSUPPORTED                (0x0000)

/****************************************************************************
*  PCIe Link Config Pages (MPI v2.6 and later)
****************************************************************************/

/*PCIe Link Page 1 */

typedef struct _MPI26_CONFIG_PAGE_PCIELINK_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U8	Link;				/*0x08 */
	U8	Reserved1;			/*0x09 */
	U16	Reserved2;			/*0x0A */
	U32	CorrectableErrorCount;		/*0x0C */
	U16	NonFatalErrorCount;		/*0x10 */
	U16	Reserved3;			/*0x12 */
	U16	FatalErrorCount;		/*0x14 */
	U16	Reserved4;			/*0x16 */
} MPI26_CONFIG_PAGE_PCIELINK_1, *PTR_MPI26_CONFIG_PAGE_PCIELINK_1,
	Mpi26PcieLinkPage1_t, *pMpi26PcieLinkPage1_t;

#define MPI26_PCIELINK1_PAGEVERSION            (0x00)

/*PCIe Link Page 2 */

typedef struct _MPI26_PCIELINK2_LINK_EVENT {
	U8	LinkEventCode;		/*0x00 */
	U8	Reserved1;		/*0x01 */
	U16	Reserved2;		/*0x02 */
	U32	LinkEventInfo;		/*0x04 */
} MPI26_PCIELINK2_LINK_EVENT, *PTR_MPI26_PCIELINK2_LINK_EVENT,
	Mpi26PcieLink2LinkEvent_t, *pMpi26PcieLink2LinkEvent_t;

/*use MPI26_PCIELINK3_EVTCODE_ for the LinkEventCode field */


/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumLinkEvents at runtime.
 */
#ifndef MPI26_PCIELINK2_LINK_EVENT_MAX
#define MPI26_PCIELINK2_LINK_EVENT_MAX      (1)
#endif

typedef struct _MPI26_CONFIG_PAGE_PCIELINK_2 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U8	Link;                       /*0x08 */
	U8	Reserved1;                  /*0x09 */
	U16	Reserved2;                  /*0x0A */
	U8	NumLinkEvents;              /*0x0C */
	U8	Reserved3;                  /*0x0D */
	U16	Reserved4;                  /*0x0E */
	MPI26_PCIELINK2_LINK_EVENT
		LinkEvent[MPI26_PCIELINK2_LINK_EVENT_MAX];	/*0x10 */
} MPI26_CONFIG_PAGE_PCIELINK_2, *PTR_MPI26_CONFIG_PAGE_PCIELINK_2,
	Mpi26PcieLinkPage2_t, *pMpi26PcieLinkPage2_t;

#define MPI26_PCIELINK2_PAGEVERSION            (0x00)

/*PCIe Link Page 3 */

typedef struct _MPI26_PCIELINK3_LINK_EVENT_CONFIG {
	U8	LinkEventCode;      /*0x00 */
	U8	Reserved1;          /*0x01 */
	U16	Reserved2;          /*0x02 */
	U8	CounterType;        /*0x04 */
	U8	ThresholdWindow;    /*0x05 */
	U8	TimeUnits;          /*0x06 */
	U8	Reserved3;          /*0x07 */
	U32	EventThreshold;     /*0x08 */
	U16	ThresholdFlags;     /*0x0C */
	U16	Reserved4;          /*0x0E */
} MPI26_PCIELINK3_LINK_EVENT_CONFIG, *PTR_MPI26_PCIELINK3_LINK_EVENT_CONFIG,
	Mpi26PcieLink3LinkEventConfig_t, *pMpi26PcieLink3LinkEventConfig_t;

/*values for LinkEventCode field */
#define MPI26_PCIELINK3_EVTCODE_NO_EVENT                              (0x00)
#define MPI26_PCIELINK3_EVTCODE_CORRECTABLE_ERROR_RECEIVED            (0x01)
#define MPI26_PCIELINK3_EVTCODE_NON_FATAL_ERROR_RECEIVED              (0x02)
#define MPI26_PCIELINK3_EVTCODE_FATAL_ERROR_RECEIVED                  (0x03)
#define MPI26_PCIELINK3_EVTCODE_DATA_LINK_ERROR_DETECTED              (0x04)
#define MPI26_PCIELINK3_EVTCODE_TRANSACTION_LAYER_ERROR_DETECTED      (0x05)
#define MPI26_PCIELINK3_EVTCODE_TLP_ECRC_ERROR_DETECTED               (0x06)
#define MPI26_PCIELINK3_EVTCODE_POISONED_TLP                          (0x07)
#define MPI26_PCIELINK3_EVTCODE_RECEIVED_NAK_DLLP                     (0x08)
#define MPI26_PCIELINK3_EVTCODE_SENT_NAK_DLLP                         (0x09)
#define MPI26_PCIELINK3_EVTCODE_LTSSM_RECOVERY_STATE                  (0x0A)
#define MPI26_PCIELINK3_EVTCODE_LTSSM_RXL0S_STATE                     (0x0B)
#define MPI26_PCIELINK3_EVTCODE_LTSSM_TXL0S_STATE                     (0x0C)
#define MPI26_PCIELINK3_EVTCODE_LTSSM_L1_STATE                        (0x0D)
#define MPI26_PCIELINK3_EVTCODE_LTSSM_DISABLED_STATE                  (0x0E)
#define MPI26_PCIELINK3_EVTCODE_LTSSM_HOT_RESET_STATE                 (0x0F)
#define MPI26_PCIELINK3_EVTCODE_SYSTEM_ERROR                          (0x10)
#define MPI26_PCIELINK3_EVTCODE_DECODE_ERROR                          (0x11)
#define MPI26_PCIELINK3_EVTCODE_DISPARITY_ERROR                       (0x12)

/*values for the CounterType field */
#define MPI26_PCIELINK3_COUNTER_TYPE_WRAPPING               (0x00)
#define MPI26_PCIELINK3_COUNTER_TYPE_SATURATING             (0x01)
#define MPI26_PCIELINK3_COUNTER_TYPE_PEAK_VALUE             (0x02)

/*values for the TimeUnits field */
#define MPI26_PCIELINK3_TM_UNITS_10_MICROSECONDS            (0x00)
#define MPI26_PCIELINK3_TM_UNITS_100_MICROSECONDS           (0x01)
#define MPI26_PCIELINK3_TM_UNITS_1_MILLISECOND              (0x02)
#define MPI26_PCIELINK3_TM_UNITS_10_MILLISECONDS            (0x03)

/*values for the ThresholdFlags field */
#define MPI26_PCIELINK3_TFLAGS_EVENT_NOTIFY                 (0x0001)

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check the value returned for NumLinkEvents at runtime.
 */
#ifndef MPI26_PCIELINK3_LINK_EVENT_MAX
#define MPI26_PCIELINK3_LINK_EVENT_MAX      (1)
#endif

typedef struct _MPI26_CONFIG_PAGE_PCIELINK_3 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/*0x00 */
	U8	Link;                       /*0x08 */
	U8	Reserved1;                  /*0x09 */
	U16	Reserved2;                  /*0x0A */
	U8	NumLinkEvents;              /*0x0C */
	U8	Reserved3;                  /*0x0D */
	U16	Reserved4;                  /*0x0E */
	MPI26_PCIELINK3_LINK_EVENT_CONFIG
		LinkEventConfig[MPI26_PCIELINK3_LINK_EVENT_MAX]; /*0x10 */
} MPI26_CONFIG_PAGE_PCIELINK_3, *PTR_MPI26_CONFIG_PAGE_PCIELINK_3,
	Mpi26PcieLinkPage3_t, *pMpi26PcieLinkPage3_t;

#define MPI26_PCIELINK3_PAGEVERSION            (0x00)


#endif
