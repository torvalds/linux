/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (c) 2000-2008 LSI Corporation.
 *
 *
 *           Name:  mpi_cnfg.h
 *          Title:  MPI Config message, structures, and Pages
 *  Creation Date:  July 27, 2000
 *
 *    mpi_cnfg.h Version:  01.05.18
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  05-08-00  00.10.01  Original release for 0.10 spec dated 4/26/2000.
 *  06-06-00  01.00.01  Update version number for 1.0 release.
 *  06-08-00  01.00.02  Added _PAGEVERSION definitions for all pages.
 *                      Added FcPhLowestVersion, FcPhHighestVersion, Reserved2
 *                      fields to FC_DEVICE_0 page, updated the page version.
 *                      Changed _FREE_RUNNING_CLOCK to _PACING_TRANSFERS in
 *                      SCSI_PORT_0, SCSI_DEVICE_0 and SCSI_DEVICE_1 pages
 *                      and updated the page versions.
 *                      Added _RESPONSE_ID_MASK definition to SCSI_PORT_1
 *                      page and updated the page version.
 *                      Added Information field and _INFO_PARAMS_NEGOTIATED
 *                      definitionto SCSI_DEVICE_0 page.
 *  06-22-00  01.00.03  Removed batch controls from LAN_0 page and updated the
 *                      page version.
 *                      Added BucketsRemaining to LAN_1 page, redefined the
 *                      state values, and updated the page version.
 *                      Revised bus width definitions in SCSI_PORT_0,
 *                      SCSI_DEVICE_0 and SCSI_DEVICE_1 pages.
 *  06-30-00  01.00.04  Added MaxReplySize to LAN_1 page and updated the page
 *                      version.
 *                      Moved FC_DEVICE_0 PageAddress description to spec.
 *  07-27-00  01.00.05  Corrected the SubsystemVendorID and SubsystemID field
 *                      widths in IOC_0 page and updated the page version.
 *  11-02-00  01.01.01  Original release for post 1.0 work
 *                      Added Manufacturing pages, IO Unit Page 2, SCSI SPI
 *                      Port Page 2, FC Port Page 4, FC Port Page 5
 *  11-15-00  01.01.02  Interim changes to match proposals
 *  12-04-00  01.01.03  Config page changes to match MPI rev 1.00.01.
 *  12-05-00  01.01.04  Modified config page actions.
 *  01-09-01  01.01.05  Added defines for page address formats.
 *                      Data size for Manufacturing pages 2 and 3 no longer
 *                      defined here.
 *                      Io Unit Page 2 size is fixed at 4 adapters and some
 *                      flags were changed.
 *                      SCSI Port Page 2 Device Settings modified.
 *                      New fields added to FC Port Page 0 and some flags
 *                      cleaned up.
 *                      Removed impedance flash from FC Port Page 1.
 *                      Added FC Port pages 6 and 7.
 *  01-25-01  01.01.06  Added MaxInitiators field to FcPortPage0.
 *  01-29-01  01.01.07  Changed some defines to make them 32 character unique.
 *                      Added some LinkType defines for FcPortPage0.
 *  02-20-01  01.01.08  Started using MPI_POINTER.
 *  02-27-01  01.01.09  Replaced MPI_CONFIG_PAGETYPE_SCSI_LUN with
 *                      MPI_CONFIG_PAGETYPE_RAID_VOLUME.
 *                      Added definitions and structures for IOC Page 2 and
 *                      RAID Volume Page 2.
 *  03-27-01  01.01.10  Added CONFIG_PAGE_FC_PORT_8 and CONFIG_PAGE_FC_PORT_9.
 *                      CONFIG_PAGE_FC_PORT_3 now supports persistent by DID.
 *                      Added VendorId and ProductRevLevel fields to
 *                      RAIDVOL2_IM_PHYS_ID struct.
 *                      Modified values for MPI_FCPORTPAGE0_FLAGS_ATTACH_
 *                      defines to make them compatible to MPI version 1.0.
 *                      Added structure offset comments.
 *  04-09-01  01.01.11  Added some new defines for the PageAddress field and
 *                      removed some obsolete ones.
 *                      Added IO Unit Page 3.
 *                      Modified defines for Scsi Port Page 2.
 *                      Modified RAID Volume Pages.
 *  08-08-01  01.02.01  Original release for v1.2 work.
 *                      Added SepID and SepBus to RVP2 IMPhysicalDisk struct.
 *                      Added defines for the SEP bits in RVP2 VolumeSettings.
 *                      Modified the DeviceSettings field in RVP2 to use the
 *                      proper structure.
 *                      Added defines for SES, SAF-TE, and cross channel for
 *                      IOCPage2 CapabilitiesFlags.
 *                      Removed define for MPI_IOUNITPAGE2_FLAGS_RAID_DISABLE.
 *                      Removed define for
 *                      MPI_SCSIPORTPAGE2_PORT_FLAGS_PARITY_ENABLE.
 *                      Added define for MPI_CONFIG_PAGEATTR_RO_PERSISTENT.
 *  08-29-01 01.02.02   Fixed value for MPI_MANUFACTPAGE_DEVID_53C1035.
 *                      Added defines for MPI_FCPORTPAGE1_FLAGS_HARD_ALPA_ONLY
 *                      and MPI_FCPORTPAGE1_FLAGS_IMMEDIATE_ERROR_REPLY.
 *                      Removed MPI_SCSIPORTPAGE0_CAP_PACING_TRANSFERS,
 *                      MPI_SCSIDEVPAGE0_NP_PACING_TRANSFERS, and
 *                      MPI_SCSIDEVPAGE1_RP_PACING_TRANSFERS, and
 *                      MPI_SCSIDEVPAGE1_CONF_PPR_ALLOWED.
 *                      Added defines for MPI_SCSIDEVPAGE1_CONF_WDTR_DISALLOWED
 *                      and MPI_SCSIDEVPAGE1_CONF_SDTR_DISALLOWED.
 *                      Added OnBusTimerValue to CONFIG_PAGE_SCSI_PORT_1.
 *                      Added rejected bits to SCSI Device Page 0 Information.
 *                      Increased size of ALPA array in FC Port Page 2 by one
 *                      and removed a one byte reserved field.
 *  09-28-01 01.02.03   Swapped NegWireSpeedLow and NegWireSpeedLow in
 *                      CONFIG_PAGE_LAN_1 to match preferred 64-bit ordering.
 *                      Added structures for Manufacturing Page 4, IO Unit
 *                      Page 3, IOC Page 3, IOC Page 4, RAID Volume Page 0, and
 *                      RAID PhysDisk Page 0.
 *  10-04-01 01.02.04   Added define for MPI_CONFIG_PAGETYPE_RAID_PHYSDISK.
 *                      Modified some of the new defines to make them 32
 *                      character unique.
 *                      Modified how variable length pages (arrays) are defined.
 *                      Added generic defines for hot spare pools and RAID
 *                      volume types.
 *  11-01-01 01.02.05   Added define for MPI_IOUNITPAGE1_DISABLE_IR.
 *  03-14-02 01.02.06   Added PCISlotNum field to CONFIG_PAGE_IOC_1 along with
 *                      related define, and bumped the page version define.
 *  05-31-02 01.02.07   Added a Flags field to CONFIG_PAGE_IOC_2_RAID_VOL in a
 *                      reserved byte and added a define.
 *                      Added define for
 *                      MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE.
 *                      Added new config page: CONFIG_PAGE_IOC_5.
 *                      Added MaxAliases, MaxHardAliases, and NumCurrentAliases
 *                      fields to CONFIG_PAGE_FC_PORT_0.
 *                      Added AltConnector and NumRequestedAliases fields to
 *                      CONFIG_PAGE_FC_PORT_1.
 *                      Added new config page: CONFIG_PAGE_FC_PORT_10.
 *  07-12-02 01.02.08   Added more MPI_MANUFACTPAGE_DEVID_ defines.
 *                      Added additional MPI_SCSIDEVPAGE0_NP_ defines.
 *                      Added more MPI_SCSIDEVPAGE1_RP_ defines.
 *                      Added define for
 *                      MPI_SCSIDEVPAGE1_CONF_EXTENDED_PARAMS_ENABLE.
 *                      Added new config page: CONFIG_PAGE_SCSI_DEVICE_3.
 *                      Modified MPI_FCPORTPAGE5_FLAGS_ defines.
 *  09-16-02 01.02.09   Added MPI_SCSIDEVPAGE1_CONF_FORCE_PPR_MSG define.
 *  11-15-02 01.02.10   Added ConnectedID defines for CONFIG_PAGE_SCSI_PORT_0.
 *                      Added more Flags defines for CONFIG_PAGE_FC_PORT_1.
 *                      Added more Flags defines for CONFIG_PAGE_FC_DEVICE_0.
 *  04-01-03 01.02.11   Added RR_TOV field and additional Flags defines for
 *                      CONFIG_PAGE_FC_PORT_1.
 *                      Added define MPI_FCPORTPAGE5_FLAGS_DISABLE to disable
 *                      an alias.
 *                      Added more device id defines.
 *  06-26-03 01.02.12   Added MPI_IOUNITPAGE1_IR_USE_STATIC_VOLUME_ID define.
 *                      Added TargetConfig and IDConfig fields to
 *                      CONFIG_PAGE_SCSI_PORT_1.
 *                      Added more PortFlags defines for CONFIG_PAGE_SCSI_PORT_2
 *                      to control DV.
 *                      Added more Flags defines for CONFIG_PAGE_FC_PORT_1.
 *                      In CONFIG_PAGE_FC_DEVICE_0, replaced Reserved1 field
 *                      with ADISCHardALPA.
 *                      Added MPI_FC_DEVICE_PAGE0_PROT_FCP_RETRY define.
 *  01-16-04 01.02.13   Added InitiatorDeviceTimeout and InitiatorIoPendTimeout
 *                      fields and related defines to CONFIG_PAGE_FC_PORT_1.
 *                      Added define for
 *                      MPI_FCPORTPAGE1_FLAGS_SOFT_ALPA_FALLBACK.
 *                      Added new fields to the substructures of
 *                      CONFIG_PAGE_FC_PORT_10.
 *  04-29-04 01.02.14   Added define for IDP bit for CONFIG_PAGE_SCSI_PORT_0,
 *                      CONFIG_PAGE_SCSI_DEVICE_0, and
 *                      CONFIG_PAGE_SCSI_DEVICE_1. Also bumped Page Version for
 *                      these pages.
 *  05-11-04 01.03.01   Added structure for CONFIG_PAGE_INBAND_0.
 *  08-19-04 01.05.01   Modified MSG_CONFIG request to support extended config
 *                      pages.
 *                      Added a new structure for extended config page header.
 *                      Added new extended config pages types and structures for
 *                      SAS IO Unit, SAS Expander, SAS Device, and SAS PHY.
 *                      Replaced a reserved byte in CONFIG_PAGE_MANUFACTURING_4
 *                      to add a Flags field.
 *                      Two new Manufacturing config pages (5 and 6).
 *                      Two new bits defined for IO Unit Page 1 Flags field.
 *                      Modified CONFIG_PAGE_IO_UNIT_2 to add three new fields
 *                      to specify the BIOS boot device.
 *                      Four new Flags bits defined for IO Unit Page 2.
 *                      Added IO Unit Page 4.
 *                      Added EEDP Flags settings to IOC Page 1.
 *                      Added new BIOS Page 1 config page.
 *  10-05-04 01.05.02   Added define for
 *                      MPI_IOCPAGE1_INITIATOR_CONTEXT_REPLY_DISABLE.
 *                      Added new Flags field to CONFIG_PAGE_MANUFACTURING_5 and
 *                      associated defines.
 *                      Added more defines for SAS IO Unit Page 0
 *                      DiscoveryStatus field.
 *                      Added define for MPI_SAS_IOUNIT0_DS_SUBTRACTIVE_LINK
 *                      and MPI_SAS_IOUNIT0_DS_TABLE_LINK.
 *                      Added defines for Physical Mapping Modes to SAS IO Unit
 *                      Page 2.
 *                      Added define for
 *                      MPI_SAS_DEVICE0_FLAGS_PORT_SELECTOR_ATTACH.
 *  10-27-04 01.05.03   Added defines for new SAS PHY page addressing mode.
 *                      Added defines for MaxTargetSpinUp to BIOS Page 1.
 *                      Added 5 new ControlFlags defines for SAS IO Unit
 *                      Page 1.
 *                      Added MaxNumPhysicalMappedIDs field to SAS IO Unit
 *                      Page 2.
 *                      Added AccessStatus field to SAS Device Page 0 and added
 *                      new Flags bits for supported SATA features.
 *  12-07-04  01.05.04  Added config page structures for BIOS Page 2, RAID
 *                      Volume Page 1, and RAID Physical Disk Page 1.
 *                      Replaced IO Unit Page 1 BootTargetID,BootBus, and
 *                      BootAdapterNum with reserved field.
 *                      Added DataScrubRate and ResyncRate to RAID Volume
 *                      Page 0.
 *                      Added MPI_SAS_IOUNIT2_FLAGS_RESERVE_ID_0_FOR_BOOT
 *                      define.
 *  12-09-04  01.05.05  Added Target Mode Large CDB Enable to FC Port Page 1
 *                      Flags field.
 *                      Added Auto Port Config flag define for SAS IOUNIT
 *                      Page 1 ControlFlags.
 *                      Added Disabled bad Phy define to Expander Page 1
 *                      Discovery Info field.
 *                      Added SAS/SATA device support to SAS IOUnit Page 1
 *                      ControlFlags.
 *                      Added Unsupported device to SAS Dev Page 0 Flags field
 *                      Added disable use SATA Hash Address for SAS IOUNIT
 *                      page 1 in ControlFields.
 *  01-15-05  01.05.06  Added defaults for data scrub rate and resync rate to
 *                      Manufacturing Page 4.
 *                      Added new defines for BIOS Page 1 IOCSettings field.
 *                      Added ExtDiskIdentifier field to RAID Physical Disk
 *                      Page 0.
 *                      Added new defines for SAS IO Unit Page 1 ControlFlags
 *                      and to SAS Device Page 0 Flags to control SATA devices.
 *                      Added defines and structures for the new Log Page 0, a
 *                      new type of configuration page.
 *  02-09-05  01.05.07  Added InactiveStatus field to RAID Volume Page 0.
 *                      Added WWID field to RAID Volume Page 1.
 *                      Added PhysicalPort field to SAS Expander pages 0 and 1.
 *  03-11-05  01.05.08  Removed the EEDP flags from IOC Page 1.
 *                      Added Enclosure/Slot boot device format to BIOS Page 2.
 *                      New status value for RAID Volume Page 0 VolumeStatus
 *                      (VolumeState subfield).
 *                      New value for RAID Physical Page 0 InactiveStatus.
 *                      Added Inactive Volume Member flag RAID Physical Disk
 *                      Page 0 PhysDiskStatus field.
 *                      New physical mapping mode in SAS IO Unit Page 2.
 *                      Added CONFIG_PAGE_SAS_ENCLOSURE_0.
 *                      Added Slot and Enclosure fields to SAS Device Page 0.
 *  06-24-05  01.05.09  Added EEDP defines to IOC Page 1.
 *                      Added more RAID type defines to IOC Page 2.
 *                      Added Port Enable Delay settings to BIOS Page 1.
 *                      Added Bad Block Table Full define to RAID Volume Page 0.
 *                      Added Previous State defines to RAID Physical Disk
 *                      Page 0.
 *                      Added Max Sata Targets define for DiscoveryStatus field
 *                      of SAS IO Unit Page 0.
 *                      Added Device Self Test to Control Flags of SAS IO Unit
 *                      Page 1.
 *                      Added Direct Attach Starting Slot Number define for SAS
 *                      IO Unit Page 2.
 *                      Added new fields in SAS Device Page 2 for enclosure
 *                      mapping.
 *                      Added OwnerDevHandle and Flags field to SAS PHY Page 0.
 *                      Added IOC GPIO Flags define to SAS Enclosure Page 0.
 *                      Fixed the value for MPI_SAS_IOUNIT1_CONTROL_DEV_SATA_SUPPORT.
 *  08-03-05  01.05.10  Removed ISDataScrubRate and ISResyncRate from
 *                      Manufacturing Page 4.
 *                      Added MPI_IOUNITPAGE1_SATA_WRITE_CACHE_DISABLE bit.
 *                      Added NumDevsPerEnclosure field to SAS IO Unit page 2.
 *                      Added MPI_SAS_IOUNIT2_FLAGS_HOST_ASSIGNED_PHYS_MAP
 *                      define.
 *                      Added EnclosureHandle field to SAS Expander page 0.
 *                      Removed redundant NumTableEntriesProg field from SAS
 *                      Expander Page 1.
 *  08-30-05  01.05.11  Added DeviceID for FC949E and changed the DeviceID for
 *                      SAS1078.
 *                      Added more defines for Manufacturing Page 4 Flags field.
 *                      Added more defines for IOCSettings and added
 *                      ExpanderSpinup field to Bios Page 1.
 *                      Added postpone SATA Init bit to SAS IO Unit Page 1
 *                      ControlFlags.
 *                      Changed LogEntry format for Log Page 0.
 *  03-27-06  01.05.12  Added two new Flags defines for Manufacturing Page 4.
 *                      Added Manufacturing Page 7.
 *                      Added MPI_IOCPAGE2_CAP_FLAGS_RAID_64_BIT_ADDRESSING.
 *                      Added IOC Page 6.
 *                      Added PrevBootDeviceForm field to CONFIG_PAGE_BIOS_2.
 *                      Added MaxLBAHigh field to RAID Volume Page 0.
 *                      Added Nvdata version fields to SAS IO Unit Page 0.
 *                      Added AdditionalControlFlags, MaxTargetPortConnectTime,
 *                      ReportDeviceMissingDelay, and IODeviceMissingDelay
 *                      fields to SAS IO Unit Page 1.
 *  10-11-06  01.05.13  Added NumForceWWID field and ForceWWID array to
 *                      Manufacturing Page 5.
 *                      Added Manufacturing pages 8 through 10.
 *                      Added defines for supported metadata size bits in
 *                      CapabilitiesFlags field of IOC Page 6.
 *                      Added defines for metadata size bits in VolumeSettings
 *                      field of RAID Volume Page 0.
 *                      Added SATA Link Reset settings, Enable SATA Asynchronous
 *                      Notification bit, and HideNonZeroAttachedPhyIdentifiers
 *                      bit to AdditionalControlFlags field of SAS IO Unit
 *                      Page 1.
 *                      Added defines for Enclosure Devices Unmapped and
 *                      Device Limit Exceeded bits in Status field of SAS IO
 *                      Unit Page 2.
 *                      Added more AccessStatus values for SAS Device Page 0.
 *                      Added bit for SATA Asynchronous Notification Support in
 *                      Flags field of SAS Device Page 0.
 *  02-28-07  01.05.14  Added ExtFlags field to Manufacturing Page 4.
 *                      Added Disable SMART Polling for CapabilitiesFlags of
 *                      IOC Page 6.
 *                      Added Disable SMART Polling to DeviceSettings of BIOS
 *                      Page 1.
 *                      Added Multi-Port Domain bit for DiscoveryStatus field
 *                      of SAS IO Unit Page.
 *                      Added Multi-Port Domain Illegal flag for SAS IO Unit
 *                      Page 1 AdditionalControlFlags field.
 *  05-24-07  01.05.15  Added Hide Physical Disks with Non-Integrated RAID
 *                      Metadata bit to Manufacturing Page 4 ExtFlags field.
 *                      Added Internal Connector to End Device Present bit to
 *                      Expander Page 0 Flags field.
 *                      Fixed define for
 *                      MPI_SAS_EXPANDER1_DISCINFO_BAD_PHY_DISABLED.
 *  08-07-07  01.05.16  Added MPI_IOCPAGE6_CAP_FLAGS_MULTIPORT_DRIVE_SUPPORT
 *                      define.
 *                      Added BIOS Page 4 structure.
 *                      Added MPI_RAID_PHYS_DISK1_PATH_MAX define for RAID
 *                      Physcial Disk Page 1.
 *  01-15-07  01.05.17  Added additional bit defines for ExtFlags field of
 *                      Manufacturing Page 4.
 *                      Added Solid State Drives Supported bit to IOC Page 6
 *                      Capabilities Flags.
 *                      Added new value for AccessStatus field of SAS Device
 *                      Page 0 (_SATA_NEEDS_INITIALIZATION).
 *  03-28-08  01.05.18  Defined new bits in Manufacturing Page 4 ExtFlags field
 *                      to control coercion size and the mixing of SAS and SATA
 *                      SSD drives.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_CNFG_H
#define MPI_CNFG_H


/*****************************************************************************
*
*       C o n f i g    M e s s a g e    a n d    S t r u c t u r e s
*
*****************************************************************************/

typedef struct _CONFIG_PAGE_HEADER
{
    U8                      PageVersion;                /* 00h */
    U8                      PageLength;                 /* 01h */
    U8                      PageNumber;                 /* 02h */
    U8                      PageType;                   /* 03h */
} CONFIG_PAGE_HEADER, MPI_POINTER PTR_CONFIG_PAGE_HEADER,
  ConfigPageHeader_t, MPI_POINTER pConfigPageHeader_t;

typedef union _CONFIG_PAGE_HEADER_UNION
{
   ConfigPageHeader_t  Struct;
   U8                  Bytes[4];
   U16                 Word16[2];
   U32                 Word32;
} ConfigPageHeaderUnion, MPI_POINTER pConfigPageHeaderUnion,
  CONFIG_PAGE_HEADER_UNION, MPI_POINTER PTR_CONFIG_PAGE_HEADER_UNION;

typedef struct _CONFIG_EXTENDED_PAGE_HEADER
{
    U8                  PageVersion;                /* 00h */
    U8                  Reserved1;                  /* 01h */
    U8                  PageNumber;                 /* 02h */
    U8                  PageType;                   /* 03h */
    U16                 ExtPageLength;              /* 04h */
    U8                  ExtPageType;                /* 06h */
    U8                  Reserved2;                  /* 07h */
} CONFIG_EXTENDED_PAGE_HEADER, MPI_POINTER PTR_CONFIG_EXTENDED_PAGE_HEADER,
  ConfigExtendedPageHeader_t, MPI_POINTER pConfigExtendedPageHeader_t;



/****************************************************************************
*   PageType field values
****************************************************************************/
#define MPI_CONFIG_PAGEATTR_READ_ONLY               (0x00)
#define MPI_CONFIG_PAGEATTR_CHANGEABLE              (0x10)
#define MPI_CONFIG_PAGEATTR_PERSISTENT              (0x20)
#define MPI_CONFIG_PAGEATTR_RO_PERSISTENT           (0x30)
#define MPI_CONFIG_PAGEATTR_MASK                    (0xF0)

#define MPI_CONFIG_PAGETYPE_IO_UNIT                 (0x00)
#define MPI_CONFIG_PAGETYPE_IOC                     (0x01)
#define MPI_CONFIG_PAGETYPE_BIOS                    (0x02)
#define MPI_CONFIG_PAGETYPE_SCSI_PORT               (0x03)
#define MPI_CONFIG_PAGETYPE_SCSI_DEVICE             (0x04)
#define MPI_CONFIG_PAGETYPE_FC_PORT                 (0x05)
#define MPI_CONFIG_PAGETYPE_FC_DEVICE               (0x06)
#define MPI_CONFIG_PAGETYPE_LAN                     (0x07)
#define MPI_CONFIG_PAGETYPE_RAID_VOLUME             (0x08)
#define MPI_CONFIG_PAGETYPE_MANUFACTURING           (0x09)
#define MPI_CONFIG_PAGETYPE_RAID_PHYSDISK           (0x0A)
#define MPI_CONFIG_PAGETYPE_INBAND                  (0x0B)
#define MPI_CONFIG_PAGETYPE_EXTENDED                (0x0F)
#define MPI_CONFIG_PAGETYPE_MASK                    (0x0F)

#define MPI_CONFIG_TYPENUM_MASK                     (0x0FFF)


/****************************************************************************
*   ExtPageType field values
****************************************************************************/
#define MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT          (0x10)
#define MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER         (0x11)
#define MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE           (0x12)
#define MPI_CONFIG_EXTPAGETYPE_SAS_PHY              (0x13)
#define MPI_CONFIG_EXTPAGETYPE_LOG                  (0x14)
#define MPI_CONFIG_EXTPAGETYPE_ENCLOSURE            (0x15)


/****************************************************************************
*   PageAddress field values
****************************************************************************/
#define MPI_SCSI_PORT_PGAD_PORT_MASK                (0x000000FF)

#define MPI_SCSI_DEVICE_FORM_MASK                   (0xF0000000)
#define MPI_SCSI_DEVICE_FORM_BUS_TID                (0x00000000)
#define MPI_SCSI_DEVICE_TARGET_ID_MASK              (0x000000FF)
#define MPI_SCSI_DEVICE_TARGET_ID_SHIFT             (0)
#define MPI_SCSI_DEVICE_BUS_MASK                    (0x0000FF00)
#define MPI_SCSI_DEVICE_BUS_SHIFT                   (8)
#define MPI_SCSI_DEVICE_FORM_TARGET_MODE            (0x10000000)
#define MPI_SCSI_DEVICE_TM_RESPOND_ID_MASK          (0x000000FF)
#define MPI_SCSI_DEVICE_TM_RESPOND_ID_SHIFT         (0)
#define MPI_SCSI_DEVICE_TM_BUS_MASK                 (0x0000FF00)
#define MPI_SCSI_DEVICE_TM_BUS_SHIFT                (8)
#define MPI_SCSI_DEVICE_TM_INIT_ID_MASK             (0x00FF0000)
#define MPI_SCSI_DEVICE_TM_INIT_ID_SHIFT            (16)

#define MPI_FC_PORT_PGAD_PORT_MASK                  (0xF0000000)
#define MPI_FC_PORT_PGAD_PORT_SHIFT                 (28)
#define MPI_FC_PORT_PGAD_FORM_MASK                  (0x0F000000)
#define MPI_FC_PORT_PGAD_FORM_INDEX                 (0x01000000)
#define MPI_FC_PORT_PGAD_INDEX_MASK                 (0x0000FFFF)
#define MPI_FC_PORT_PGAD_INDEX_SHIFT                (0)

#define MPI_FC_DEVICE_PGAD_PORT_MASK                (0xF0000000)
#define MPI_FC_DEVICE_PGAD_PORT_SHIFT               (28)
#define MPI_FC_DEVICE_PGAD_FORM_MASK                (0x0F000000)
#define MPI_FC_DEVICE_PGAD_FORM_NEXT_DID            (0x00000000)
#define MPI_FC_DEVICE_PGAD_ND_PORT_MASK             (0xF0000000)
#define MPI_FC_DEVICE_PGAD_ND_PORT_SHIFT            (28)
#define MPI_FC_DEVICE_PGAD_ND_DID_MASK              (0x00FFFFFF)
#define MPI_FC_DEVICE_PGAD_ND_DID_SHIFT             (0)
#define MPI_FC_DEVICE_PGAD_FORM_BUS_TID             (0x01000000)
#define MPI_FC_DEVICE_PGAD_BT_BUS_MASK              (0x0000FF00)
#define MPI_FC_DEVICE_PGAD_BT_BUS_SHIFT             (8)
#define MPI_FC_DEVICE_PGAD_BT_TID_MASK              (0x000000FF)
#define MPI_FC_DEVICE_PGAD_BT_TID_SHIFT             (0)

#define MPI_PHYSDISK_PGAD_PHYSDISKNUM_MASK          (0x000000FF)
#define MPI_PHYSDISK_PGAD_PHYSDISKNUM_SHIFT         (0)

#define MPI_SAS_EXPAND_PGAD_FORM_MASK             (0xF0000000)
#define MPI_SAS_EXPAND_PGAD_FORM_SHIFT            (28)
#define MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE  (0x00000000)
#define MPI_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM   (0x00000001)
#define MPI_SAS_EXPAND_PGAD_FORM_HANDLE           (0x00000002)
#define MPI_SAS_EXPAND_PGAD_GNH_MASK_HANDLE       (0x0000FFFF)
#define MPI_SAS_EXPAND_PGAD_GNH_SHIFT_HANDLE      (0)
#define MPI_SAS_EXPAND_PGAD_HPN_MASK_PHY          (0x00FF0000)
#define MPI_SAS_EXPAND_PGAD_HPN_SHIFT_PHY         (16)
#define MPI_SAS_EXPAND_PGAD_HPN_MASK_HANDLE       (0x0000FFFF)
#define MPI_SAS_EXPAND_PGAD_HPN_SHIFT_HANDLE      (0)
#define MPI_SAS_EXPAND_PGAD_H_MASK_HANDLE         (0x0000FFFF)
#define MPI_SAS_EXPAND_PGAD_H_SHIFT_HANDLE        (0)

#define MPI_SAS_DEVICE_PGAD_FORM_MASK               (0xF0000000)
#define MPI_SAS_DEVICE_PGAD_FORM_SHIFT              (28)
#define MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE    (0x00000000)
#define MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID      (0x00000001)
#define MPI_SAS_DEVICE_PGAD_FORM_HANDLE             (0x00000002)
#define MPI_SAS_DEVICE_PGAD_GNH_HANDLE_MASK         (0x0000FFFF)
#define MPI_SAS_DEVICE_PGAD_GNH_HANDLE_SHIFT        (0)
#define MPI_SAS_DEVICE_PGAD_BT_BUS_MASK             (0x0000FF00)
#define MPI_SAS_DEVICE_PGAD_BT_BUS_SHIFT            (8)
#define MPI_SAS_DEVICE_PGAD_BT_TID_MASK             (0x000000FF)
#define MPI_SAS_DEVICE_PGAD_BT_TID_SHIFT            (0)
#define MPI_SAS_DEVICE_PGAD_H_HANDLE_MASK           (0x0000FFFF)
#define MPI_SAS_DEVICE_PGAD_H_HANDLE_SHIFT          (0)

#define MPI_SAS_PHY_PGAD_FORM_MASK                  (0xF0000000)
#define MPI_SAS_PHY_PGAD_FORM_SHIFT                 (28)
#define MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER            (0x0)
#define MPI_SAS_PHY_PGAD_FORM_PHY_TBL_INDEX         (0x1)
#define MPI_SAS_PHY_PGAD_PHY_NUMBER_MASK            (0x000000FF)
#define MPI_SAS_PHY_PGAD_PHY_NUMBER_SHIFT           (0)
#define MPI_SAS_PHY_PGAD_PHY_TBL_INDEX_MASK         (0x0000FFFF)
#define MPI_SAS_PHY_PGAD_PHY_TBL_INDEX_SHIFT        (0)

#define MPI_SAS_ENCLOS_PGAD_FORM_MASK               (0xF0000000)
#define MPI_SAS_ENCLOS_PGAD_FORM_SHIFT              (28)
#define MPI_SAS_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE    (0x00000000)
#define MPI_SAS_ENCLOS_PGAD_FORM_HANDLE             (0x00000001)
#define MPI_SAS_ENCLOS_PGAD_GNH_HANDLE_MASK         (0x0000FFFF)
#define MPI_SAS_ENCLOS_PGAD_GNH_HANDLE_SHIFT        (0)
#define MPI_SAS_ENCLOS_PGAD_H_HANDLE_MASK           (0x0000FFFF)
#define MPI_SAS_ENCLOS_PGAD_H_HANDLE_SHIFT          (0)



/****************************************************************************
*   Config Request Message
****************************************************************************/
typedef struct _MSG_CONFIG
{
    U8                      Action;                     /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     ExtPageLength;              /* 04h */
    U8                      ExtPageType;                /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U8                      Reserved2[8];               /* 0Ch */
    CONFIG_PAGE_HEADER      Header;                     /* 14h */
    U32                     PageAddress;                /* 18h */
    SGE_IO_UNION            PageBufferSGE;              /* 1Ch */
} MSG_CONFIG, MPI_POINTER PTR_MSG_CONFIG,
  Config_t, MPI_POINTER pConfig_t;


/****************************************************************************
*   Action field values
****************************************************************************/
#define MPI_CONFIG_ACTION_PAGE_HEADER               (0x00)
#define MPI_CONFIG_ACTION_PAGE_READ_CURRENT         (0x01)
#define MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT        (0x02)
#define MPI_CONFIG_ACTION_PAGE_DEFAULT              (0x03)
#define MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM          (0x04)
#define MPI_CONFIG_ACTION_PAGE_READ_DEFAULT         (0x05)
#define MPI_CONFIG_ACTION_PAGE_READ_NVRAM           (0x06)


/* Config Reply Message */
typedef struct _MSG_CONFIG_REPLY
{
    U8                      Action;                     /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U16                     ExtPageLength;              /* 04h */
    U8                      ExtPageType;                /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U8                      Reserved2[2];               /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
    CONFIG_PAGE_HEADER      Header;                     /* 14h */
} MSG_CONFIG_REPLY, MPI_POINTER PTR_MSG_CONFIG_REPLY,
  ConfigReply_t, MPI_POINTER pConfigReply_t;



/*****************************************************************************
*
*               C o n f i g u r a t i o n    P a g e s
*
*****************************************************************************/

/****************************************************************************
*   Manufacturing Config pages
****************************************************************************/
#define MPI_MANUFACTPAGE_VENDORID_LSILOGIC          (0x1000)
/* Fibre Channel */
#define MPI_MANUFACTPAGE_DEVICEID_FC909             (0x0621)
#define MPI_MANUFACTPAGE_DEVICEID_FC919             (0x0624)
#define MPI_MANUFACTPAGE_DEVICEID_FC929             (0x0622)
#define MPI_MANUFACTPAGE_DEVICEID_FC919X            (0x0628)
#define MPI_MANUFACTPAGE_DEVICEID_FC929X            (0x0626)
#define MPI_MANUFACTPAGE_DEVICEID_FC939X            (0x0642)
#define MPI_MANUFACTPAGE_DEVICEID_FC949X            (0x0640)
#define MPI_MANUFACTPAGE_DEVICEID_FC949E            (0x0646)
/* SCSI */
#define MPI_MANUFACTPAGE_DEVID_53C1030              (0x0030)
#define MPI_MANUFACTPAGE_DEVID_53C1030ZC            (0x0031)
#define MPI_MANUFACTPAGE_DEVID_1030_53C1035         (0x0032)
#define MPI_MANUFACTPAGE_DEVID_1030ZC_53C1035       (0x0033)
#define MPI_MANUFACTPAGE_DEVID_53C1035              (0x0040)
#define MPI_MANUFACTPAGE_DEVID_53C1035ZC            (0x0041)
/* SAS */
#define MPI_MANUFACTPAGE_DEVID_SAS1064              (0x0050)
#define MPI_MANUFACTPAGE_DEVID_SAS1064A             (0x005C)
#define MPI_MANUFACTPAGE_DEVID_SAS1064E             (0x0056)
#define MPI_MANUFACTPAGE_DEVID_SAS1066              (0x005E)
#define MPI_MANUFACTPAGE_DEVID_SAS1066E             (0x005A)
#define MPI_MANUFACTPAGE_DEVID_SAS1068              (0x0054)
#define MPI_MANUFACTPAGE_DEVID_SAS1068E             (0x0058)
#define MPI_MANUFACTPAGE_DEVID_SAS1068_820XELP      (0x0059)
#define MPI_MANUFACTPAGE_DEVID_SAS1078              (0x0062)


typedef struct _CONFIG_PAGE_MANUFACTURING_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U8                      ChipName[16];               /* 04h */
    U8                      ChipRevision[8];            /* 14h */
    U8                      BoardName[16];              /* 1Ch */
    U8                      BoardAssembly[16];          /* 2Ch */
    U8                      BoardTracerNumber[16];      /* 3Ch */

} CONFIG_PAGE_MANUFACTURING_0, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_0,
  ManufacturingPage0_t, MPI_POINTER pManufacturingPage0_t;

#define MPI_MANUFACTURING0_PAGEVERSION                 (0x00)


typedef struct _CONFIG_PAGE_MANUFACTURING_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U8                      VPD[256];                   /* 04h */
} CONFIG_PAGE_MANUFACTURING_1, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_1,
  ManufacturingPage1_t, MPI_POINTER pManufacturingPage1_t;

#define MPI_MANUFACTURING1_PAGEVERSION                 (0x00)


typedef struct _MPI_CHIP_REVISION_ID
{
    U16 DeviceID;                                       /* 00h */
    U8  PCIRevisionID;                                  /* 02h */
    U8  Reserved;                                       /* 03h */
} MPI_CHIP_REVISION_ID, MPI_POINTER PTR_MPI_CHIP_REVISION_ID,
  MpiChipRevisionId_t, MPI_POINTER pMpiChipRevisionId_t;


/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_MAN_PAGE_2_HW_SETTINGS_WORDS
#define MPI_MAN_PAGE_2_HW_SETTINGS_WORDS    (1)
#endif

typedef struct _CONFIG_PAGE_MANUFACTURING_2
{
    CONFIG_PAGE_HEADER      Header;                                 /* 00h */
    MPI_CHIP_REVISION_ID    ChipId;                                 /* 04h */
    U32                     HwSettings[MPI_MAN_PAGE_2_HW_SETTINGS_WORDS];/* 08h */
} CONFIG_PAGE_MANUFACTURING_2, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_2,
  ManufacturingPage2_t, MPI_POINTER pManufacturingPage2_t;

#define MPI_MANUFACTURING2_PAGEVERSION                  (0x00)


/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_MAN_PAGE_3_INFO_WORDS
#define MPI_MAN_PAGE_3_INFO_WORDS           (1)
#endif

typedef struct _CONFIG_PAGE_MANUFACTURING_3
{
    CONFIG_PAGE_HEADER                  Header;                     /* 00h */
    MPI_CHIP_REVISION_ID                ChipId;                     /* 04h */
    U32                                 Info[MPI_MAN_PAGE_3_INFO_WORDS];/* 08h */
} CONFIG_PAGE_MANUFACTURING_3, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_3,
  ManufacturingPage3_t, MPI_POINTER pManufacturingPage3_t;

#define MPI_MANUFACTURING3_PAGEVERSION                  (0x00)


typedef struct _CONFIG_PAGE_MANUFACTURING_4
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U32                             Reserved1;          /* 04h */
    U8                              InfoOffset0;        /* 08h */
    U8                              InfoSize0;          /* 09h */
    U8                              InfoOffset1;        /* 0Ah */
    U8                              InfoSize1;          /* 0Bh */
    U8                              InquirySize;        /* 0Ch */
    U8                              Flags;              /* 0Dh */
    U16                             ExtFlags;           /* 0Eh */
    U8                              InquiryData[56];    /* 10h */
    U32                             ISVolumeSettings;   /* 48h */
    U32                             IMEVolumeSettings;  /* 4Ch */
    U32                             IMVolumeSettings;   /* 50h */
    U32                             Reserved3;          /* 54h */
    U32                             Reserved4;          /* 58h */
    U32                             Reserved5;          /* 5Ch */
    U8                              IMEDataScrubRate;   /* 60h */
    U8                              IMEResyncRate;      /* 61h */
    U16                             Reserved6;          /* 62h */
    U8                              IMDataScrubRate;    /* 64h */
    U8                              IMResyncRate;       /* 65h */
    U16                             Reserved7;          /* 66h */
    U32                             Reserved8;          /* 68h */
    U32                             Reserved9;          /* 6Ch */
} CONFIG_PAGE_MANUFACTURING_4, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_4,
  ManufacturingPage4_t, MPI_POINTER pManufacturingPage4_t;

#define MPI_MANUFACTURING4_PAGEVERSION                  (0x05)

/* defines for the Flags field */
#define MPI_MANPAGE4_FORCE_BAD_BLOCK_TABLE              (0x80)
#define MPI_MANPAGE4_FORCE_OFFLINE_FAILOVER             (0x40)
#define MPI_MANPAGE4_IME_DISABLE                        (0x20)
#define MPI_MANPAGE4_IM_DISABLE                         (0x10)
#define MPI_MANPAGE4_IS_DISABLE                         (0x08)
#define MPI_MANPAGE4_IR_MODEPAGE8_DISABLE               (0x04)
#define MPI_MANPAGE4_IM_RESYNC_CACHE_ENABLE             (0x02)
#define MPI_MANPAGE4_IR_NO_MIX_SAS_SATA                 (0x01)

/* defines for the ExtFlags field */
#define MPI_MANPAGE4_EXTFLAGS_MASK_COERCION_SIZE        (0x0180)
#define MPI_MANPAGE4_EXTFLAGS_SHIFT_COERCION_SIZE       (7)
#define MPI_MANPAGE4_EXTFLAGS_1GB_COERCION_SIZE         (0)
#define MPI_MANPAGE4_EXTFLAGS_128MB_COERCION_SIZE       (1)

#define MPI_MANPAGE4_EXTFLAGS_NO_MIX_SSD_SAS_SATA       (0x0040)
#define MPI_MANPAGE4_EXTFLAGS_MIX_SSD_AND_NON_SSD       (0x0020)
#define MPI_MANPAGE4_EXTFLAGS_DUAL_PORT_SUPPORT         (0x0010)
#define MPI_MANPAGE4_EXTFLAGS_HIDE_NON_IR_METADATA      (0x0008)
#define MPI_MANPAGE4_EXTFLAGS_SAS_CACHE_DISABLE         (0x0004)
#define MPI_MANPAGE4_EXTFLAGS_SATA_CACHE_DISABLE        (0x0002)
#define MPI_MANPAGE4_EXTFLAGS_LEGACY_MODE               (0x0001)


#ifndef MPI_MANPAGE5_NUM_FORCEWWID
#define MPI_MANPAGE5_NUM_FORCEWWID      (1)
#endif

typedef struct _CONFIG_PAGE_MANUFACTURING_5
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U64                             BaseWWID;           /* 04h */
    U8                              Flags;              /* 0Ch */
    U8                              NumForceWWID;       /* 0Dh */
    U16                             Reserved2;          /* 0Eh */
    U32                             Reserved3;          /* 10h */
    U32                             Reserved4;          /* 14h */
    U64                             ForceWWID[MPI_MANPAGE5_NUM_FORCEWWID]; /* 18h */
} CONFIG_PAGE_MANUFACTURING_5, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_5,
  ManufacturingPage5_t, MPI_POINTER pManufacturingPage5_t;

#define MPI_MANUFACTURING5_PAGEVERSION                  (0x02)

/* defines for the Flags field */
#define MPI_MANPAGE5_TWO_WWID_PER_PHY                   (0x01)


typedef struct _CONFIG_PAGE_MANUFACTURING_6
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U32                             ProductSpecificInfo;/* 04h */
} CONFIG_PAGE_MANUFACTURING_6, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_6,
  ManufacturingPage6_t, MPI_POINTER pManufacturingPage6_t;

#define MPI_MANUFACTURING6_PAGEVERSION                  (0x00)


typedef struct _MPI_MANPAGE7_CONNECTOR_INFO
{
    U32                         Pinout;                 /* 00h */
    U8                          Connector[16];          /* 04h */
    U8                          Location;               /* 14h */
    U8                          Reserved1;              /* 15h */
    U16                         Slot;                   /* 16h */
    U32                         Reserved2;              /* 18h */
} MPI_MANPAGE7_CONNECTOR_INFO, MPI_POINTER PTR_MPI_MANPAGE7_CONNECTOR_INFO,
  MpiManPage7ConnectorInfo_t, MPI_POINTER pMpiManPage7ConnectorInfo_t;

/* defines for the Pinout field */
#define MPI_MANPAGE7_PINOUT_SFF_8484_L4                 (0x00080000)
#define MPI_MANPAGE7_PINOUT_SFF_8484_L3                 (0x00040000)
#define MPI_MANPAGE7_PINOUT_SFF_8484_L2                 (0x00020000)
#define MPI_MANPAGE7_PINOUT_SFF_8484_L1                 (0x00010000)
#define MPI_MANPAGE7_PINOUT_SFF_8470_L4                 (0x00000800)
#define MPI_MANPAGE7_PINOUT_SFF_8470_L3                 (0x00000400)
#define MPI_MANPAGE7_PINOUT_SFF_8470_L2                 (0x00000200)
#define MPI_MANPAGE7_PINOUT_SFF_8470_L1                 (0x00000100)
#define MPI_MANPAGE7_PINOUT_SFF_8482                    (0x00000002)
#define MPI_MANPAGE7_PINOUT_CONNECTION_UNKNOWN          (0x00000001)

/* defines for the Location field */
#define MPI_MANPAGE7_LOCATION_UNKNOWN                   (0x01)
#define MPI_MANPAGE7_LOCATION_INTERNAL                  (0x02)
#define MPI_MANPAGE7_LOCATION_EXTERNAL                  (0x04)
#define MPI_MANPAGE7_LOCATION_SWITCHABLE                (0x08)
#define MPI_MANPAGE7_LOCATION_AUTO                      (0x10)
#define MPI_MANPAGE7_LOCATION_NOT_PRESENT               (0x20)
#define MPI_MANPAGE7_LOCATION_NOT_CONNECTED             (0x80)

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check NumPhys at runtime.
 */
#ifndef MPI_MANPAGE7_CONNECTOR_INFO_MAX
#define MPI_MANPAGE7_CONNECTOR_INFO_MAX   (1)
#endif

typedef struct _CONFIG_PAGE_MANUFACTURING_7
{
    CONFIG_PAGE_HEADER          Header;                 /* 00h */
    U32                         Reserved1;              /* 04h */
    U32                         Reserved2;              /* 08h */
    U32                         Flags;                  /* 0Ch */
    U8                          EnclosureName[16];      /* 10h */
    U8                          NumPhys;                /* 20h */
    U8                          Reserved3;              /* 21h */
    U16                         Reserved4;              /* 22h */
    MPI_MANPAGE7_CONNECTOR_INFO ConnectorInfo[MPI_MANPAGE7_CONNECTOR_INFO_MAX]; /* 24h */
} CONFIG_PAGE_MANUFACTURING_7, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_7,
  ManufacturingPage7_t, MPI_POINTER pManufacturingPage7_t;

#define MPI_MANUFACTURING7_PAGEVERSION                  (0x00)

/* defines for the Flags field */
#define MPI_MANPAGE7_FLAG_USE_SLOT_INFO                 (0x00000001)


typedef struct _CONFIG_PAGE_MANUFACTURING_8
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U32                             ProductSpecificInfo;/* 04h */
} CONFIG_PAGE_MANUFACTURING_8, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_8,
  ManufacturingPage8_t, MPI_POINTER pManufacturingPage8_t;

#define MPI_MANUFACTURING8_PAGEVERSION                  (0x00)


typedef struct _CONFIG_PAGE_MANUFACTURING_9
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U32                             ProductSpecificInfo;/* 04h */
} CONFIG_PAGE_MANUFACTURING_9, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_9,
  ManufacturingPage9_t, MPI_POINTER pManufacturingPage9_t;

#define MPI_MANUFACTURING9_PAGEVERSION                  (0x00)


typedef struct _CONFIG_PAGE_MANUFACTURING_10
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U32                             ProductSpecificInfo;/* 04h */
} CONFIG_PAGE_MANUFACTURING_10, MPI_POINTER PTR_CONFIG_PAGE_MANUFACTURING_10,
  ManufacturingPage10_t, MPI_POINTER pManufacturingPage10_t;

#define MPI_MANUFACTURING10_PAGEVERSION                 (0x00)


/****************************************************************************
*   IO Unit Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_IO_UNIT_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U64                     UniqueValue;                /* 04h */
} CONFIG_PAGE_IO_UNIT_0, MPI_POINTER PTR_CONFIG_PAGE_IO_UNIT_0,
  IOUnitPage0_t, MPI_POINTER pIOUnitPage0_t;

#define MPI_IOUNITPAGE0_PAGEVERSION                     (0x00)


typedef struct _CONFIG_PAGE_IO_UNIT_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Flags;                      /* 04h */
} CONFIG_PAGE_IO_UNIT_1, MPI_POINTER PTR_CONFIG_PAGE_IO_UNIT_1,
  IOUnitPage1_t, MPI_POINTER pIOUnitPage1_t;

#define MPI_IOUNITPAGE1_PAGEVERSION                     (0x02)

/* IO Unit Page 1 Flags defines */
#define MPI_IOUNITPAGE1_MULTI_FUNCTION                  (0x00000000)
#define MPI_IOUNITPAGE1_SINGLE_FUNCTION                 (0x00000001)
#define MPI_IOUNITPAGE1_MULTI_PATHING                   (0x00000002)
#define MPI_IOUNITPAGE1_SINGLE_PATHING                  (0x00000000)
#define MPI_IOUNITPAGE1_IR_USE_STATIC_VOLUME_ID         (0x00000004)
#define MPI_IOUNITPAGE1_DISABLE_QUEUE_FULL_HANDLING     (0x00000020)
#define MPI_IOUNITPAGE1_DISABLE_IR                      (0x00000040)
#define MPI_IOUNITPAGE1_FORCE_32                        (0x00000080)
#define MPI_IOUNITPAGE1_NATIVE_COMMAND_Q_DISABLE        (0x00000100)
#define MPI_IOUNITPAGE1_SATA_WRITE_CACHE_DISABLE        (0x00000200)

typedef struct _MPI_ADAPTER_INFO
{
    U8      PciBusNumber;                               /* 00h */
    U8      PciDeviceAndFunctionNumber;                 /* 01h */
    U16     AdapterFlags;                               /* 02h */
} MPI_ADAPTER_INFO, MPI_POINTER PTR_MPI_ADAPTER_INFO,
  MpiAdapterInfo_t, MPI_POINTER pMpiAdapterInfo_t;

#define MPI_ADAPTER_INFO_FLAGS_EMBEDDED                 (0x0001)
#define MPI_ADAPTER_INFO_FLAGS_INIT_STATUS              (0x0002)

typedef struct _CONFIG_PAGE_IO_UNIT_2
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Flags;                      /* 04h */
    U32                     BiosVersion;                /* 08h */
    MPI_ADAPTER_INFO        AdapterOrder[4];            /* 0Ch */
    U32                     Reserved1;                  /* 1Ch */
} CONFIG_PAGE_IO_UNIT_2, MPI_POINTER PTR_CONFIG_PAGE_IO_UNIT_2,
  IOUnitPage2_t, MPI_POINTER pIOUnitPage2_t;

#define MPI_IOUNITPAGE2_PAGEVERSION                     (0x02)

#define MPI_IOUNITPAGE2_FLAGS_PAUSE_ON_ERROR            (0x00000002)
#define MPI_IOUNITPAGE2_FLAGS_VERBOSE_ENABLE            (0x00000004)
#define MPI_IOUNITPAGE2_FLAGS_COLOR_VIDEO_DISABLE       (0x00000008)
#define MPI_IOUNITPAGE2_FLAGS_DONT_HOOK_INT_40          (0x00000010)

#define MPI_IOUNITPAGE2_FLAGS_DEV_LIST_DISPLAY_MASK     (0x000000E0)
#define MPI_IOUNITPAGE2_FLAGS_INSTALLED_DEV_DISPLAY     (0x00000000)
#define MPI_IOUNITPAGE2_FLAGS_ADAPTER_DISPLAY           (0x00000020)
#define MPI_IOUNITPAGE2_FLAGS_ADAPTER_DEV_DISPLAY       (0x00000040)


/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_IO_UNIT_PAGE_3_GPIO_VAL_MAX
#define MPI_IO_UNIT_PAGE_3_GPIO_VAL_MAX     (1)
#endif

typedef struct _CONFIG_PAGE_IO_UNIT_3
{
    CONFIG_PAGE_HEADER      Header;                                   /* 00h */
    U8                      GPIOCount;                                /* 04h */
    U8                      Reserved1;                                /* 05h */
    U16                     Reserved2;                                /* 06h */
    U16                     GPIOVal[MPI_IO_UNIT_PAGE_3_GPIO_VAL_MAX]; /* 08h */
} CONFIG_PAGE_IO_UNIT_3, MPI_POINTER PTR_CONFIG_PAGE_IO_UNIT_3,
  IOUnitPage3_t, MPI_POINTER pIOUnitPage3_t;

#define MPI_IOUNITPAGE3_PAGEVERSION                     (0x01)

#define MPI_IOUNITPAGE3_GPIO_FUNCTION_MASK              (0xFC)
#define MPI_IOUNITPAGE3_GPIO_FUNCTION_SHIFT             (2)
#define MPI_IOUNITPAGE3_GPIO_SETTING_OFF                (0x00)
#define MPI_IOUNITPAGE3_GPIO_SETTING_ON                 (0x01)


typedef struct _CONFIG_PAGE_IO_UNIT_4
{
    CONFIG_PAGE_HEADER      Header;                                   /* 00h */
    U32                     Reserved1;                                /* 04h */
    SGE_SIMPLE_UNION        FWImageSGE;                               /* 08h */
} CONFIG_PAGE_IO_UNIT_4, MPI_POINTER PTR_CONFIG_PAGE_IO_UNIT_4,
  IOUnitPage4_t, MPI_POINTER pIOUnitPage4_t;

#define MPI_IOUNITPAGE4_PAGEVERSION                     (0x00)


/****************************************************************************
*   IOC Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_IOC_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     TotalNVStore;               /* 04h */
    U32                     FreeNVStore;                /* 08h */
    U16                     VendorID;                   /* 0Ch */
    U16                     DeviceID;                   /* 0Eh */
    U8                      RevisionID;                 /* 10h */
    U8                      Reserved[3];                /* 11h */
    U32                     ClassCode;                  /* 14h */
    U16                     SubsystemVendorID;          /* 18h */
    U16                     SubsystemID;                /* 1Ah */
} CONFIG_PAGE_IOC_0, MPI_POINTER PTR_CONFIG_PAGE_IOC_0,
  IOCPage0_t, MPI_POINTER pIOCPage0_t;

#define MPI_IOCPAGE0_PAGEVERSION                        (0x01)


typedef struct _CONFIG_PAGE_IOC_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Flags;                      /* 04h */
    U32                     CoalescingTimeout;          /* 08h */
    U8                      CoalescingDepth;            /* 0Ch */
    U8                      PCISlotNum;                 /* 0Dh */
    U8                      Reserved[2];                /* 0Eh */
} CONFIG_PAGE_IOC_1, MPI_POINTER PTR_CONFIG_PAGE_IOC_1,
  IOCPage1_t, MPI_POINTER pIOCPage1_t;

#define MPI_IOCPAGE1_PAGEVERSION                        (0x03)

/* defines for the Flags field */
#define MPI_IOCPAGE1_EEDP_MODE_MASK                     (0x07000000)
#define MPI_IOCPAGE1_EEDP_MODE_OFF                      (0x00000000)
#define MPI_IOCPAGE1_EEDP_MODE_T10                      (0x01000000)
#define MPI_IOCPAGE1_EEDP_MODE_LSI_1                    (0x02000000)
#define MPI_IOCPAGE1_INITIATOR_CONTEXT_REPLY_DISABLE    (0x00000010)
#define MPI_IOCPAGE1_REPLY_COALESCING                   (0x00000001)

#define MPI_IOCPAGE1_PCISLOTNUM_UNKNOWN                 (0xFF)


typedef struct _CONFIG_PAGE_IOC_2_RAID_VOL
{
    U8                          VolumeID;               /* 00h */
    U8                          VolumeBus;              /* 01h */
    U8                          VolumeIOC;              /* 02h */
    U8                          VolumePageNumber;       /* 03h */
    U8                          VolumeType;             /* 04h */
    U8                          Flags;                  /* 05h */
    U16                         Reserved3;              /* 06h */
} CONFIG_PAGE_IOC_2_RAID_VOL, MPI_POINTER PTR_CONFIG_PAGE_IOC_2_RAID_VOL,
  ConfigPageIoc2RaidVol_t, MPI_POINTER pConfigPageIoc2RaidVol_t;

/* IOC Page 2 Volume RAID Type values, also used in RAID Volume pages */

#define MPI_RAID_VOL_TYPE_IS                        (0x00)
#define MPI_RAID_VOL_TYPE_IME                       (0x01)
#define MPI_RAID_VOL_TYPE_IM                        (0x02)
#define MPI_RAID_VOL_TYPE_RAID_5                    (0x03)
#define MPI_RAID_VOL_TYPE_RAID_6                    (0x04)
#define MPI_RAID_VOL_TYPE_RAID_10                   (0x05)
#define MPI_RAID_VOL_TYPE_RAID_50                   (0x06)
#define MPI_RAID_VOL_TYPE_UNKNOWN                   (0xFF)

/* IOC Page 2 Volume Flags values */

#define MPI_IOCPAGE2_FLAG_VOLUME_INACTIVE           (0x08)

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_IOC_PAGE_2_RAID_VOLUME_MAX
#define MPI_IOC_PAGE_2_RAID_VOLUME_MAX      (1)
#endif

typedef struct _CONFIG_PAGE_IOC_2
{
    CONFIG_PAGE_HEADER          Header;                              /* 00h */
    U32                         CapabilitiesFlags;                   /* 04h */
    U8                          NumActiveVolumes;                    /* 08h */
    U8                          MaxVolumes;                          /* 09h */
    U8                          NumActivePhysDisks;                  /* 0Ah */
    U8                          MaxPhysDisks;                        /* 0Bh */
    CONFIG_PAGE_IOC_2_RAID_VOL  RaidVolume[MPI_IOC_PAGE_2_RAID_VOLUME_MAX];/* 0Ch */
} CONFIG_PAGE_IOC_2, MPI_POINTER PTR_CONFIG_PAGE_IOC_2,
  IOCPage2_t, MPI_POINTER pIOCPage2_t;

#define MPI_IOCPAGE2_PAGEVERSION                        (0x04)

/* IOC Page 2 Capabilities flags */

#define MPI_IOCPAGE2_CAP_FLAGS_IS_SUPPORT               (0x00000001)
#define MPI_IOCPAGE2_CAP_FLAGS_IME_SUPPORT              (0x00000002)
#define MPI_IOCPAGE2_CAP_FLAGS_IM_SUPPORT               (0x00000004)
#define MPI_IOCPAGE2_CAP_FLAGS_RAID_5_SUPPORT           (0x00000008)
#define MPI_IOCPAGE2_CAP_FLAGS_RAID_6_SUPPORT           (0x00000010)
#define MPI_IOCPAGE2_CAP_FLAGS_RAID_10_SUPPORT          (0x00000020)
#define MPI_IOCPAGE2_CAP_FLAGS_RAID_50_SUPPORT          (0x00000040)
#define MPI_IOCPAGE2_CAP_FLAGS_RAID_64_BIT_ADDRESSING   (0x10000000)
#define MPI_IOCPAGE2_CAP_FLAGS_SES_SUPPORT              (0x20000000)
#define MPI_IOCPAGE2_CAP_FLAGS_SAFTE_SUPPORT            (0x40000000)
#define MPI_IOCPAGE2_CAP_FLAGS_CROSS_CHANNEL_SUPPORT    (0x80000000)


typedef struct _IOC_3_PHYS_DISK
{
    U8                          PhysDiskID;             /* 00h */
    U8                          PhysDiskBus;            /* 01h */
    U8                          PhysDiskIOC;            /* 02h */
    U8                          PhysDiskNum;            /* 03h */
} IOC_3_PHYS_DISK, MPI_POINTER PTR_IOC_3_PHYS_DISK,
  Ioc3PhysDisk_t, MPI_POINTER pIoc3PhysDisk_t;

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_IOC_PAGE_3_PHYSDISK_MAX
#define MPI_IOC_PAGE_3_PHYSDISK_MAX         (1)
#endif

typedef struct _CONFIG_PAGE_IOC_3
{
    CONFIG_PAGE_HEADER          Header;                                /* 00h */
    U8                          NumPhysDisks;                          /* 04h */
    U8                          Reserved1;                             /* 05h */
    U16                         Reserved2;                             /* 06h */
    IOC_3_PHYS_DISK             PhysDisk[MPI_IOC_PAGE_3_PHYSDISK_MAX]; /* 08h */
} CONFIG_PAGE_IOC_3, MPI_POINTER PTR_CONFIG_PAGE_IOC_3,
  IOCPage3_t, MPI_POINTER pIOCPage3_t;

#define MPI_IOCPAGE3_PAGEVERSION                        (0x00)


typedef struct _IOC_4_SEP
{
    U8                          SEPTargetID;            /* 00h */
    U8                          SEPBus;                 /* 01h */
    U16                         Reserved;               /* 02h */
} IOC_4_SEP, MPI_POINTER PTR_IOC_4_SEP,
  Ioc4Sep_t, MPI_POINTER pIoc4Sep_t;

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_IOC_PAGE_4_SEP_MAX
#define MPI_IOC_PAGE_4_SEP_MAX              (1)
#endif

typedef struct _CONFIG_PAGE_IOC_4
{
    CONFIG_PAGE_HEADER          Header;                         /* 00h */
    U8                          ActiveSEP;                      /* 04h */
    U8                          MaxSEP;                         /* 05h */
    U16                         Reserved1;                      /* 06h */
    IOC_4_SEP                   SEP[MPI_IOC_PAGE_4_SEP_MAX];    /* 08h */
} CONFIG_PAGE_IOC_4, MPI_POINTER PTR_CONFIG_PAGE_IOC_4,
  IOCPage4_t, MPI_POINTER pIOCPage4_t;

#define MPI_IOCPAGE4_PAGEVERSION                        (0x00)


typedef struct _IOC_5_HOT_SPARE
{
    U8                          PhysDiskNum;            /* 00h */
    U8                          Reserved;               /* 01h */
    U8                          HotSparePool;           /* 02h */
    U8                          Flags;                   /* 03h */
} IOC_5_HOT_SPARE, MPI_POINTER PTR_IOC_5_HOT_SPARE,
  Ioc5HotSpare_t, MPI_POINTER pIoc5HotSpare_t;

/* IOC Page 5 HotSpare Flags */
#define MPI_IOC_PAGE_5_HOT_SPARE_ACTIVE                 (0x01)

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_IOC_PAGE_5_HOT_SPARE_MAX
#define MPI_IOC_PAGE_5_HOT_SPARE_MAX        (1)
#endif

typedef struct _CONFIG_PAGE_IOC_5
{
    CONFIG_PAGE_HEADER          Header;                         /* 00h */
    U32                         Reserved1;                      /* 04h */
    U8                          NumHotSpares;                   /* 08h */
    U8                          Reserved2;                      /* 09h */
    U16                         Reserved3;                      /* 0Ah */
    IOC_5_HOT_SPARE             HotSpare[MPI_IOC_PAGE_5_HOT_SPARE_MAX]; /* 0Ch */
} CONFIG_PAGE_IOC_5, MPI_POINTER PTR_CONFIG_PAGE_IOC_5,
  IOCPage5_t, MPI_POINTER pIOCPage5_t;

#define MPI_IOCPAGE5_PAGEVERSION                        (0x00)

typedef struct _CONFIG_PAGE_IOC_6
{
    CONFIG_PAGE_HEADER          Header;                         /* 00h */
    U32                         CapabilitiesFlags;              /* 04h */
    U8                          MaxDrivesIS;                    /* 08h */
    U8                          MaxDrivesIM;                    /* 09h */
    U8                          MaxDrivesIME;                   /* 0Ah */
    U8                          Reserved1;                      /* 0Bh */
    U8                          MinDrivesIS;                    /* 0Ch */
    U8                          MinDrivesIM;                    /* 0Dh */
    U8                          MinDrivesIME;                   /* 0Eh */
    U8                          Reserved2;                      /* 0Fh */
    U8                          MaxGlobalHotSpares;             /* 10h */
    U8                          Reserved3;                      /* 11h */
    U16                         Reserved4;                      /* 12h */
    U32                         Reserved5;                      /* 14h */
    U32                         SupportedStripeSizeMapIS;       /* 18h */
    U32                         SupportedStripeSizeMapIME;      /* 1Ch */
    U32                         Reserved6;                      /* 20h */
    U8                          MetadataSize;                   /* 24h */
    U8                          Reserved7;                      /* 25h */
    U16                         Reserved8;                      /* 26h */
    U16                         MaxBadBlockTableEntries;        /* 28h */
    U16                         Reserved9;                      /* 2Ah */
    U16                         IRNvsramUsage;                  /* 2Ch */
    U16                         Reserved10;                     /* 2Eh */
    U32                         IRNvsramVersion;                /* 30h */
    U32                         Reserved11;                     /* 34h */
    U32                         Reserved12;                     /* 38h */
} CONFIG_PAGE_IOC_6, MPI_POINTER PTR_CONFIG_PAGE_IOC_6,
  IOCPage6_t, MPI_POINTER pIOCPage6_t;

#define MPI_IOCPAGE6_PAGEVERSION                        (0x01)

/* IOC Page 6 Capabilities Flags */

#define MPI_IOCPAGE6_CAP_FLAGS_SSD_SUPPORT              (0x00000020)
#define MPI_IOCPAGE6_CAP_FLAGS_MULTIPORT_DRIVE_SUPPORT  (0x00000010)
#define MPI_IOCPAGE6_CAP_FLAGS_DISABLE_SMART_POLLING    (0x00000008)

#define MPI_IOCPAGE6_CAP_FLAGS_MASK_METADATA_SIZE       (0x00000006)
#define MPI_IOCPAGE6_CAP_FLAGS_64MB_METADATA_SIZE       (0x00000000)
#define MPI_IOCPAGE6_CAP_FLAGS_512MB_METADATA_SIZE      (0x00000002)

#define MPI_IOCPAGE6_CAP_FLAGS_GLOBAL_HOT_SPARE         (0x00000001)


/****************************************************************************
*   BIOS Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_BIOS_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     BiosOptions;                /* 04h */
    U32                     IOCSettings;                /* 08h */
    U32                     Reserved1;                  /* 0Ch */
    U32                     DeviceSettings;             /* 10h */
    U16                     NumberOfDevices;            /* 14h */
    U8                      ExpanderSpinup;             /* 16h */
    U8                      Reserved2;                  /* 17h */
    U16                     IOTimeoutBlockDevicesNonRM; /* 18h */
    U16                     IOTimeoutSequential;        /* 1Ah */
    U16                     IOTimeoutOther;             /* 1Ch */
    U16                     IOTimeoutBlockDevicesRM;    /* 1Eh */
} CONFIG_PAGE_BIOS_1, MPI_POINTER PTR_CONFIG_PAGE_BIOS_1,
  BIOSPage1_t, MPI_POINTER pBIOSPage1_t;

#define MPI_BIOSPAGE1_PAGEVERSION                       (0x03)

/* values for the BiosOptions field */
#define MPI_BIOSPAGE1_OPTIONS_SPI_ENABLE                (0x00000400)
#define MPI_BIOSPAGE1_OPTIONS_FC_ENABLE                 (0x00000200)
#define MPI_BIOSPAGE1_OPTIONS_SAS_ENABLE                (0x00000100)
#define MPI_BIOSPAGE1_OPTIONS_DISABLE_BIOS              (0x00000001)

/* values for the IOCSettings field */
#define MPI_BIOSPAGE1_IOCSET_MASK_INITIAL_SPINUP_DELAY  (0x0F000000)
#define MPI_BIOSPAGE1_IOCSET_SHIFT_INITIAL_SPINUP_DELAY (24)

#define MPI_BIOSPAGE1_IOCSET_MASK_PORT_ENABLE_DELAY     (0x00F00000)
#define MPI_BIOSPAGE1_IOCSET_SHIFT_PORT_ENABLE_DELAY    (20)

#define MPI_BIOSPAGE1_IOCSET_AUTO_PORT_ENABLE           (0x00080000)
#define MPI_BIOSPAGE1_IOCSET_DIRECT_ATTACH_SPINUP_MODE  (0x00040000)

#define MPI_BIOSPAGE1_IOCSET_MASK_BOOT_PREFERENCE       (0x00030000)
#define MPI_BIOSPAGE1_IOCSET_ENCLOSURE_SLOT_BOOT        (0x00000000)
#define MPI_BIOSPAGE1_IOCSET_SAS_ADDRESS_BOOT           (0x00010000)

#define MPI_BIOSPAGE1_IOCSET_MASK_MAX_TARGET_SPIN_UP    (0x0000F000)
#define MPI_BIOSPAGE1_IOCSET_SHIFT_MAX_TARGET_SPIN_UP   (12)

#define MPI_BIOSPAGE1_IOCSET_MASK_SPINUP_DELAY          (0x00000F00)
#define MPI_BIOSPAGE1_IOCSET_SHIFT_SPINUP_DELAY         (8)

#define MPI_BIOSPAGE1_IOCSET_MASK_RM_SETTING            (0x000000C0)
#define MPI_BIOSPAGE1_IOCSET_NONE_RM_SETTING            (0x00000000)
#define MPI_BIOSPAGE1_IOCSET_BOOT_RM_SETTING            (0x00000040)
#define MPI_BIOSPAGE1_IOCSET_MEDIA_RM_SETTING           (0x00000080)

#define MPI_BIOSPAGE1_IOCSET_MASK_ADAPTER_SUPPORT       (0x00000030)
#define MPI_BIOSPAGE1_IOCSET_NO_SUPPORT                 (0x00000000)
#define MPI_BIOSPAGE1_IOCSET_BIOS_SUPPORT               (0x00000010)
#define MPI_BIOSPAGE1_IOCSET_OS_SUPPORT                 (0x00000020)
#define MPI_BIOSPAGE1_IOCSET_ALL_SUPPORT                (0x00000030)

#define MPI_BIOSPAGE1_IOCSET_ALTERNATE_CHS              (0x00000008)

/* values for the DeviceSettings field */
#define MPI_BIOSPAGE1_DEVSET_DISABLE_SMART_POLLING      (0x00000010)
#define MPI_BIOSPAGE1_DEVSET_DISABLE_SEQ_LUN            (0x00000008)
#define MPI_BIOSPAGE1_DEVSET_DISABLE_RM_LUN             (0x00000004)
#define MPI_BIOSPAGE1_DEVSET_DISABLE_NON_RM_LUN         (0x00000002)
#define MPI_BIOSPAGE1_DEVSET_DISABLE_OTHER_LUN          (0x00000001)

/* defines for the ExpanderSpinup field */
#define MPI_BIOSPAGE1_EXPSPINUP_MASK_MAX_TARGET         (0xF0)
#define MPI_BIOSPAGE1_EXPSPINUP_SHIFT_MAX_TARGET        (4)
#define MPI_BIOSPAGE1_EXPSPINUP_MASK_DELAY              (0x0F)

typedef struct _MPI_BOOT_DEVICE_ADAPTER_ORDER
{
    U32         Reserved1;                              /* 00h */
    U32         Reserved2;                              /* 04h */
    U32         Reserved3;                              /* 08h */
    U32         Reserved4;                              /* 0Ch */
    U32         Reserved5;                              /* 10h */
    U32         Reserved6;                              /* 14h */
    U32         Reserved7;                              /* 18h */
    U32         Reserved8;                              /* 1Ch */
    U32         Reserved9;                              /* 20h */
    U32         Reserved10;                             /* 24h */
    U32         Reserved11;                             /* 28h */
    U32         Reserved12;                             /* 2Ch */
    U32         Reserved13;                             /* 30h */
    U32         Reserved14;                             /* 34h */
    U32         Reserved15;                             /* 38h */
    U32         Reserved16;                             /* 3Ch */
    U32         Reserved17;                             /* 40h */
} MPI_BOOT_DEVICE_ADAPTER_ORDER, MPI_POINTER PTR_MPI_BOOT_DEVICE_ADAPTER_ORDER;

typedef struct _MPI_BOOT_DEVICE_ADAPTER_NUMBER
{
    U8          TargetID;                               /* 00h */
    U8          Bus;                                    /* 01h */
    U8          AdapterNumber;                          /* 02h */
    U8          Reserved1;                              /* 03h */
    U32         Reserved2;                              /* 04h */
    U32         Reserved3;                              /* 08h */
    U32         Reserved4;                              /* 0Ch */
    U8          LUN[8];                                 /* 10h */
    U32         Reserved5;                              /* 18h */
    U32         Reserved6;                              /* 1Ch */
    U32         Reserved7;                              /* 20h */
    U32         Reserved8;                              /* 24h */
    U32         Reserved9;                              /* 28h */
    U32         Reserved10;                             /* 2Ch */
    U32         Reserved11;                             /* 30h */
    U32         Reserved12;                             /* 34h */
    U32         Reserved13;                             /* 38h */
    U32         Reserved14;                             /* 3Ch */
    U32         Reserved15;                             /* 40h */
} MPI_BOOT_DEVICE_ADAPTER_NUMBER, MPI_POINTER PTR_MPI_BOOT_DEVICE_ADAPTER_NUMBER;

typedef struct _MPI_BOOT_DEVICE_PCI_ADDRESS
{
    U8          TargetID;                               /* 00h */
    U8          Bus;                                    /* 01h */
    U16         PCIAddress;                             /* 02h */
    U32         Reserved1;                              /* 04h */
    U32         Reserved2;                              /* 08h */
    U32         Reserved3;                              /* 0Ch */
    U8          LUN[8];                                 /* 10h */
    U32         Reserved4;                              /* 18h */
    U32         Reserved5;                              /* 1Ch */
    U32         Reserved6;                              /* 20h */
    U32         Reserved7;                              /* 24h */
    U32         Reserved8;                              /* 28h */
    U32         Reserved9;                              /* 2Ch */
    U32         Reserved10;                             /* 30h */
    U32         Reserved11;                             /* 34h */
    U32         Reserved12;                             /* 38h */
    U32         Reserved13;                             /* 3Ch */
    U32         Reserved14;                             /* 40h */
} MPI_BOOT_DEVICE_PCI_ADDRESS, MPI_POINTER PTR_MPI_BOOT_DEVICE_PCI_ADDRESS;

typedef struct _MPI_BOOT_DEVICE_SLOT_NUMBER
{
    U8          TargetID;                               /* 00h */
    U8          Bus;                                    /* 01h */
    U8          PCISlotNumber;                          /* 02h */
    U8          Reserved1;                              /* 03h */
    U32         Reserved2;                              /* 04h */
    U32         Reserved3;                              /* 08h */
    U32         Reserved4;                              /* 0Ch */
    U8          LUN[8];                                 /* 10h */
    U32         Reserved5;                              /* 18h */
    U32         Reserved6;                              /* 1Ch */
    U32         Reserved7;                              /* 20h */
    U32         Reserved8;                              /* 24h */
    U32         Reserved9;                              /* 28h */
    U32         Reserved10;                             /* 2Ch */
    U32         Reserved11;                             /* 30h */
    U32         Reserved12;                             /* 34h */
    U32         Reserved13;                             /* 38h */
    U32         Reserved14;                             /* 3Ch */
    U32         Reserved15;                             /* 40h */
} MPI_BOOT_DEVICE_PCI_SLOT_NUMBER, MPI_POINTER PTR_MPI_BOOT_DEVICE_PCI_SLOT_NUMBER;

typedef struct _MPI_BOOT_DEVICE_FC_WWN
{
    U64         WWPN;                                   /* 00h */
    U32         Reserved1;                              /* 08h */
    U32         Reserved2;                              /* 0Ch */
    U8          LUN[8];                                 /* 10h */
    U32         Reserved3;                              /* 18h */
    U32         Reserved4;                              /* 1Ch */
    U32         Reserved5;                              /* 20h */
    U32         Reserved6;                              /* 24h */
    U32         Reserved7;                              /* 28h */
    U32         Reserved8;                              /* 2Ch */
    U32         Reserved9;                              /* 30h */
    U32         Reserved10;                             /* 34h */
    U32         Reserved11;                             /* 38h */
    U32         Reserved12;                             /* 3Ch */
    U32         Reserved13;                             /* 40h */
} MPI_BOOT_DEVICE_FC_WWN, MPI_POINTER PTR_MPI_BOOT_DEVICE_FC_WWN;

typedef struct _MPI_BOOT_DEVICE_SAS_WWN
{
    U64         SASAddress;                             /* 00h */
    U32         Reserved1;                              /* 08h */
    U32         Reserved2;                              /* 0Ch */
    U8          LUN[8];                                 /* 10h */
    U32         Reserved3;                              /* 18h */
    U32         Reserved4;                              /* 1Ch */
    U32         Reserved5;                              /* 20h */
    U32         Reserved6;                              /* 24h */
    U32         Reserved7;                              /* 28h */
    U32         Reserved8;                              /* 2Ch */
    U32         Reserved9;                              /* 30h */
    U32         Reserved10;                             /* 34h */
    U32         Reserved11;                             /* 38h */
    U32         Reserved12;                             /* 3Ch */
    U32         Reserved13;                             /* 40h */
} MPI_BOOT_DEVICE_SAS_WWN, MPI_POINTER PTR_MPI_BOOT_DEVICE_SAS_WWN;

typedef struct _MPI_BOOT_DEVICE_ENCLOSURE_SLOT
{
    U64         EnclosureLogicalID;                     /* 00h */
    U32         Reserved1;                              /* 08h */
    U32         Reserved2;                              /* 0Ch */
    U8          LUN[8];                                 /* 10h */
    U16         SlotNumber;                             /* 18h */
    U16         Reserved3;                              /* 1Ah */
    U32         Reserved4;                              /* 1Ch */
    U32         Reserved5;                              /* 20h */
    U32         Reserved6;                              /* 24h */
    U32         Reserved7;                              /* 28h */
    U32         Reserved8;                              /* 2Ch */
    U32         Reserved9;                              /* 30h */
    U32         Reserved10;                             /* 34h */
    U32         Reserved11;                             /* 38h */
    U32         Reserved12;                             /* 3Ch */
    U32         Reserved13;                             /* 40h */
} MPI_BOOT_DEVICE_ENCLOSURE_SLOT,
  MPI_POINTER PTR_MPI_BOOT_DEVICE_ENCLOSURE_SLOT;

typedef union _MPI_BIOSPAGE2_BOOT_DEVICE
{
    MPI_BOOT_DEVICE_ADAPTER_ORDER   AdapterOrder;
    MPI_BOOT_DEVICE_ADAPTER_NUMBER  AdapterNumber;
    MPI_BOOT_DEVICE_PCI_ADDRESS     PCIAddress;
    MPI_BOOT_DEVICE_PCI_SLOT_NUMBER PCISlotNumber;
    MPI_BOOT_DEVICE_FC_WWN          FcWwn;
    MPI_BOOT_DEVICE_SAS_WWN         SasWwn;
    MPI_BOOT_DEVICE_ENCLOSURE_SLOT  EnclosureSlot;
} MPI_BIOSPAGE2_BOOT_DEVICE, MPI_POINTER PTR_MPI_BIOSPAGE2_BOOT_DEVICE;

typedef struct _CONFIG_PAGE_BIOS_2
{
    CONFIG_PAGE_HEADER          Header;                 /* 00h */
    U32                         Reserved1;              /* 04h */
    U32                         Reserved2;              /* 08h */
    U32                         Reserved3;              /* 0Ch */
    U32                         Reserved4;              /* 10h */
    U32                         Reserved5;              /* 14h */
    U32                         Reserved6;              /* 18h */
    U8                          BootDeviceForm;         /* 1Ch */
    U8                          PrevBootDeviceForm;     /* 1Ch */
    U16                         Reserved8;              /* 1Eh */
    MPI_BIOSPAGE2_BOOT_DEVICE   BootDevice;             /* 20h */
} CONFIG_PAGE_BIOS_2, MPI_POINTER PTR_CONFIG_PAGE_BIOS_2,
  BIOSPage2_t, MPI_POINTER pBIOSPage2_t;

#define MPI_BIOSPAGE2_PAGEVERSION                       (0x02)

#define MPI_BIOSPAGE2_FORM_MASK                         (0x0F)
#define MPI_BIOSPAGE2_FORM_ADAPTER_ORDER                (0x00)
#define MPI_BIOSPAGE2_FORM_ADAPTER_NUMBER               (0x01)
#define MPI_BIOSPAGE2_FORM_PCI_ADDRESS                  (0x02)
#define MPI_BIOSPAGE2_FORM_PCI_SLOT_NUMBER              (0x03)
#define MPI_BIOSPAGE2_FORM_FC_WWN                       (0x04)
#define MPI_BIOSPAGE2_FORM_SAS_WWN                      (0x05)
#define MPI_BIOSPAGE2_FORM_ENCLOSURE_SLOT               (0x06)

typedef struct _CONFIG_PAGE_BIOS_4
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U64                     ReassignmentBaseWWID;       /* 04h */
} CONFIG_PAGE_BIOS_4, MPI_POINTER PTR_CONFIG_PAGE_BIOS_4,
  BIOSPage4_t, MPI_POINTER pBIOSPage4_t;

#define MPI_BIOSPAGE4_PAGEVERSION                       (0x00)


/****************************************************************************
*   SCSI Port Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_SCSI_PORT_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Capabilities;               /* 04h */
    U32                     PhysicalInterface;          /* 08h */
} CONFIG_PAGE_SCSI_PORT_0, MPI_POINTER PTR_CONFIG_PAGE_SCSI_PORT_0,
  SCSIPortPage0_t, MPI_POINTER pSCSIPortPage0_t;

#define MPI_SCSIPORTPAGE0_PAGEVERSION                   (0x02)

#define MPI_SCSIPORTPAGE0_CAP_IU                        (0x00000001)
#define MPI_SCSIPORTPAGE0_CAP_DT                        (0x00000002)
#define MPI_SCSIPORTPAGE0_CAP_QAS                       (0x00000004)
#define MPI_SCSIPORTPAGE0_CAP_MIN_SYNC_PERIOD_MASK      (0x0000FF00)
#define MPI_SCSIPORTPAGE0_SYNC_ASYNC                    (0x00)
#define MPI_SCSIPORTPAGE0_SYNC_5                        (0x32)
#define MPI_SCSIPORTPAGE0_SYNC_10                       (0x19)
#define MPI_SCSIPORTPAGE0_SYNC_20                       (0x0C)
#define MPI_SCSIPORTPAGE0_SYNC_33_33                    (0x0B)
#define MPI_SCSIPORTPAGE0_SYNC_40                       (0x0A)
#define MPI_SCSIPORTPAGE0_SYNC_80                       (0x09)
#define MPI_SCSIPORTPAGE0_SYNC_160                      (0x08)
#define MPI_SCSIPORTPAGE0_SYNC_UNKNOWN                  (0xFF)

#define MPI_SCSIPORTPAGE0_CAP_SHIFT_MIN_SYNC_PERIOD     (8)
#define MPI_SCSIPORTPAGE0_CAP_GET_MIN_SYNC_PERIOD(Cap)      \
    (  ((Cap) & MPI_SCSIPORTPAGE0_CAP_MIN_SYNC_PERIOD_MASK) \
    >> MPI_SCSIPORTPAGE0_CAP_SHIFT_MIN_SYNC_PERIOD          \
    )
#define MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK      (0x00FF0000)
#define MPI_SCSIPORTPAGE0_CAP_SHIFT_MAX_SYNC_OFFSET     (16)
#define MPI_SCSIPORTPAGE0_CAP_GET_MAX_SYNC_OFFSET(Cap)      \
    (  ((Cap) & MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK) \
    >> MPI_SCSIPORTPAGE0_CAP_SHIFT_MAX_SYNC_OFFSET          \
    )
#define MPI_SCSIPORTPAGE0_CAP_IDP                       (0x08000000)
#define MPI_SCSIPORTPAGE0_CAP_WIDE                      (0x20000000)
#define MPI_SCSIPORTPAGE0_CAP_AIP                       (0x80000000)

#define MPI_SCSIPORTPAGE0_PHY_SIGNAL_TYPE_MASK          (0x00000003)
#define MPI_SCSIPORTPAGE0_PHY_SIGNAL_HVD                (0x01)
#define MPI_SCSIPORTPAGE0_PHY_SIGNAL_SE                 (0x02)
#define MPI_SCSIPORTPAGE0_PHY_SIGNAL_LVD                (0x03)
#define MPI_SCSIPORTPAGE0_PHY_MASK_CONNECTED_ID         (0xFF000000)
#define MPI_SCSIPORTPAGE0_PHY_SHIFT_CONNECTED_ID        (24)
#define MPI_SCSIPORTPAGE0_PHY_BUS_FREE_CONNECTED_ID     (0xFE)
#define MPI_SCSIPORTPAGE0_PHY_UNKNOWN_CONNECTED_ID      (0xFF)


typedef struct _CONFIG_PAGE_SCSI_PORT_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Configuration;              /* 04h */
    U32                     OnBusTimerValue;            /* 08h */
    U8                      TargetConfig;               /* 0Ch */
    U8                      Reserved1;                  /* 0Dh */
    U16                     IDConfig;                   /* 0Eh */
} CONFIG_PAGE_SCSI_PORT_1, MPI_POINTER PTR_CONFIG_PAGE_SCSI_PORT_1,
  SCSIPortPage1_t, MPI_POINTER pSCSIPortPage1_t;

#define MPI_SCSIPORTPAGE1_PAGEVERSION                   (0x03)

/* Configuration values */
#define MPI_SCSIPORTPAGE1_CFG_PORT_SCSI_ID_MASK         (0x000000FF)
#define MPI_SCSIPORTPAGE1_CFG_PORT_RESPONSE_ID_MASK     (0xFFFF0000)
#define MPI_SCSIPORTPAGE1_CFG_SHIFT_PORT_RESPONSE_ID    (16)

/* TargetConfig values */
#define MPI_SCSIPORTPAGE1_TARGCONFIG_TARG_ONLY        (0x01)
#define MPI_SCSIPORTPAGE1_TARGCONFIG_INIT_TARG        (0x02)


typedef struct _MPI_DEVICE_INFO
{
    U8      Timeout;                                    /* 00h */
    U8      SyncFactor;                                 /* 01h */
    U16     DeviceFlags;                                /* 02h */
} MPI_DEVICE_INFO, MPI_POINTER PTR_MPI_DEVICE_INFO,
  MpiDeviceInfo_t, MPI_POINTER pMpiDeviceInfo_t;

typedef struct _CONFIG_PAGE_SCSI_PORT_2
{
    CONFIG_PAGE_HEADER  Header;                         /* 00h */
    U32                 PortFlags;                      /* 04h */
    U32                 PortSettings;                   /* 08h */
    MPI_DEVICE_INFO     DeviceSettings[16];             /* 0Ch */
} CONFIG_PAGE_SCSI_PORT_2, MPI_POINTER PTR_CONFIG_PAGE_SCSI_PORT_2,
  SCSIPortPage2_t, MPI_POINTER pSCSIPortPage2_t;

#define MPI_SCSIPORTPAGE2_PAGEVERSION                       (0x02)

/* PortFlags values */
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_SCAN_HIGH_TO_LOW       (0x00000001)
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_AVOID_SCSI_RESET       (0x00000004)
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_ALTERNATE_CHS          (0x00000008)
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_TERMINATION_DISABLE    (0x00000010)

#define MPI_SCSIPORTPAGE2_PORT_FLAGS_DV_MASK                (0x00000060)
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_FULL_DV                (0x00000000)
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_BASIC_DV_ONLY          (0x00000020)
#define MPI_SCSIPORTPAGE2_PORT_FLAGS_OFF_DV                 (0x00000060)


/* PortSettings values */
#define MPI_SCSIPORTPAGE2_PORT_HOST_ID_MASK                 (0x0000000F)
#define MPI_SCSIPORTPAGE2_PORT_MASK_INIT_HBA                (0x00000030)
#define MPI_SCSIPORTPAGE2_PORT_DISABLE_INIT_HBA             (0x00000000)
#define MPI_SCSIPORTPAGE2_PORT_BIOS_INIT_HBA                (0x00000010)
#define MPI_SCSIPORTPAGE2_PORT_OS_INIT_HBA                  (0x00000020)
#define MPI_SCSIPORTPAGE2_PORT_BIOS_OS_INIT_HBA             (0x00000030)
#define MPI_SCSIPORTPAGE2_PORT_REMOVABLE_MEDIA              (0x000000C0)
#define MPI_SCSIPORTPAGE2_PORT_RM_NONE                      (0x00000000)
#define MPI_SCSIPORTPAGE2_PORT_RM_BOOT_ONLY                 (0x00000040)
#define MPI_SCSIPORTPAGE2_PORT_RM_WITH_MEDIA                (0x00000080)
#define MPI_SCSIPORTPAGE2_PORT_SPINUP_DELAY_MASK            (0x00000F00)
#define MPI_SCSIPORTPAGE2_PORT_SHIFT_SPINUP_DELAY           (8)
#define MPI_SCSIPORTPAGE2_PORT_MASK_NEGO_MASTER_SETTINGS    (0x00003000)
#define MPI_SCSIPORTPAGE2_PORT_NEGO_MASTER_SETTINGS         (0x00000000)
#define MPI_SCSIPORTPAGE2_PORT_NONE_MASTER_SETTINGS         (0x00001000)
#define MPI_SCSIPORTPAGE2_PORT_ALL_MASTER_SETTINGS          (0x00003000)

#define MPI_SCSIPORTPAGE2_DEVICE_DISCONNECT_ENABLE          (0x0001)
#define MPI_SCSIPORTPAGE2_DEVICE_ID_SCAN_ENABLE             (0x0002)
#define MPI_SCSIPORTPAGE2_DEVICE_LUN_SCAN_ENABLE            (0x0004)
#define MPI_SCSIPORTPAGE2_DEVICE_TAG_QUEUE_ENABLE           (0x0008)
#define MPI_SCSIPORTPAGE2_DEVICE_WIDE_DISABLE               (0x0010)
#define MPI_SCSIPORTPAGE2_DEVICE_BOOT_CHOICE                (0x0020)


/****************************************************************************
*   SCSI Target Device Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_SCSI_DEVICE_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     NegotiatedParameters;       /* 04h */
    U32                     Information;                /* 08h */
} CONFIG_PAGE_SCSI_DEVICE_0, MPI_POINTER PTR_CONFIG_PAGE_SCSI_DEVICE_0,
  SCSIDevicePage0_t, MPI_POINTER pSCSIDevicePage0_t;

#define MPI_SCSIDEVPAGE0_PAGEVERSION                    (0x04)

#define MPI_SCSIDEVPAGE0_NP_IU                          (0x00000001)
#define MPI_SCSIDEVPAGE0_NP_DT                          (0x00000002)
#define MPI_SCSIDEVPAGE0_NP_QAS                         (0x00000004)
#define MPI_SCSIDEVPAGE0_NP_HOLD_MCS                    (0x00000008)
#define MPI_SCSIDEVPAGE0_NP_WR_FLOW                     (0x00000010)
#define MPI_SCSIDEVPAGE0_NP_RD_STRM                     (0x00000020)
#define MPI_SCSIDEVPAGE0_NP_RTI                         (0x00000040)
#define MPI_SCSIDEVPAGE0_NP_PCOMP_EN                    (0x00000080)
#define MPI_SCSIDEVPAGE0_NP_NEG_SYNC_PERIOD_MASK        (0x0000FF00)
#define MPI_SCSIDEVPAGE0_NP_SHIFT_SYNC_PERIOD           (8)
#define MPI_SCSIDEVPAGE0_NP_NEG_SYNC_OFFSET_MASK        (0x00FF0000)
#define MPI_SCSIDEVPAGE0_NP_SHIFT_SYNC_OFFSET           (16)
#define MPI_SCSIDEVPAGE0_NP_IDP                         (0x08000000)
#define MPI_SCSIDEVPAGE0_NP_WIDE                        (0x20000000)
#define MPI_SCSIDEVPAGE0_NP_AIP                         (0x80000000)

#define MPI_SCSIDEVPAGE0_INFO_PARAMS_NEGOTIATED         (0x00000001)
#define MPI_SCSIDEVPAGE0_INFO_SDTR_REJECTED             (0x00000002)
#define MPI_SCSIDEVPAGE0_INFO_WDTR_REJECTED             (0x00000004)
#define MPI_SCSIDEVPAGE0_INFO_PPR_REJECTED              (0x00000008)


typedef struct _CONFIG_PAGE_SCSI_DEVICE_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     RequestedParameters;        /* 04h */
    U32                     Reserved;                   /* 08h */
    U32                     Configuration;              /* 0Ch */
} CONFIG_PAGE_SCSI_DEVICE_1, MPI_POINTER PTR_CONFIG_PAGE_SCSI_DEVICE_1,
  SCSIDevicePage1_t, MPI_POINTER pSCSIDevicePage1_t;

#define MPI_SCSIDEVPAGE1_PAGEVERSION                    (0x05)

#define MPI_SCSIDEVPAGE1_RP_IU                          (0x00000001)
#define MPI_SCSIDEVPAGE1_RP_DT                          (0x00000002)
#define MPI_SCSIDEVPAGE1_RP_QAS                         (0x00000004)
#define MPI_SCSIDEVPAGE1_RP_HOLD_MCS                    (0x00000008)
#define MPI_SCSIDEVPAGE1_RP_WR_FLOW                     (0x00000010)
#define MPI_SCSIDEVPAGE1_RP_RD_STRM                     (0x00000020)
#define MPI_SCSIDEVPAGE1_RP_RTI                         (0x00000040)
#define MPI_SCSIDEVPAGE1_RP_PCOMP_EN                    (0x00000080)
#define MPI_SCSIDEVPAGE1_RP_MIN_SYNC_PERIOD_MASK        (0x0000FF00)
#define MPI_SCSIDEVPAGE1_RP_SHIFT_MIN_SYNC_PERIOD       (8)
#define MPI_SCSIDEVPAGE1_RP_MAX_SYNC_OFFSET_MASK        (0x00FF0000)
#define MPI_SCSIDEVPAGE1_RP_SHIFT_MAX_SYNC_OFFSET       (16)
#define MPI_SCSIDEVPAGE1_RP_IDP                         (0x08000000)
#define MPI_SCSIDEVPAGE1_RP_WIDE                        (0x20000000)
#define MPI_SCSIDEVPAGE1_RP_AIP                         (0x80000000)

#define MPI_SCSIDEVPAGE1_CONF_WDTR_DISALLOWED           (0x00000002)
#define MPI_SCSIDEVPAGE1_CONF_SDTR_DISALLOWED           (0x00000004)
#define MPI_SCSIDEVPAGE1_CONF_EXTENDED_PARAMS_ENABLE    (0x00000008)
#define MPI_SCSIDEVPAGE1_CONF_FORCE_PPR_MSG             (0x00000010)


typedef struct _CONFIG_PAGE_SCSI_DEVICE_2
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     DomainValidation;           /* 04h */
    U32                     ParityPipeSelect;           /* 08h */
    U32                     DataPipeSelect;             /* 0Ch */
} CONFIG_PAGE_SCSI_DEVICE_2, MPI_POINTER PTR_CONFIG_PAGE_SCSI_DEVICE_2,
  SCSIDevicePage2_t, MPI_POINTER pSCSIDevicePage2_t;

#define MPI_SCSIDEVPAGE2_PAGEVERSION                    (0x01)

#define MPI_SCSIDEVPAGE2_DV_ISI_ENABLE                  (0x00000010)
#define MPI_SCSIDEVPAGE2_DV_SECONDARY_DRIVER_ENABLE     (0x00000020)
#define MPI_SCSIDEVPAGE2_DV_SLEW_RATE_CTRL              (0x00000380)
#define MPI_SCSIDEVPAGE2_DV_PRIM_DRIVE_STR_CTRL         (0x00001C00)
#define MPI_SCSIDEVPAGE2_DV_SECOND_DRIVE_STR_CTRL       (0x0000E000)
#define MPI_SCSIDEVPAGE2_DV_XCLKH_ST                    (0x10000000)
#define MPI_SCSIDEVPAGE2_DV_XCLKS_ST                    (0x20000000)
#define MPI_SCSIDEVPAGE2_DV_XCLKH_DT                    (0x40000000)
#define MPI_SCSIDEVPAGE2_DV_XCLKS_DT                    (0x80000000)

#define MPI_SCSIDEVPAGE2_PPS_PPS_MASK                   (0x00000003)

#define MPI_SCSIDEVPAGE2_DPS_BIT_0_PL_SELECT_MASK       (0x00000003)
#define MPI_SCSIDEVPAGE2_DPS_BIT_1_PL_SELECT_MASK       (0x0000000C)
#define MPI_SCSIDEVPAGE2_DPS_BIT_2_PL_SELECT_MASK       (0x00000030)
#define MPI_SCSIDEVPAGE2_DPS_BIT_3_PL_SELECT_MASK       (0x000000C0)
#define MPI_SCSIDEVPAGE2_DPS_BIT_4_PL_SELECT_MASK       (0x00000300)
#define MPI_SCSIDEVPAGE2_DPS_BIT_5_PL_SELECT_MASK       (0x00000C00)
#define MPI_SCSIDEVPAGE2_DPS_BIT_6_PL_SELECT_MASK       (0x00003000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_7_PL_SELECT_MASK       (0x0000C000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_8_PL_SELECT_MASK       (0x00030000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_9_PL_SELECT_MASK       (0x000C0000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_10_PL_SELECT_MASK      (0x00300000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_11_PL_SELECT_MASK      (0x00C00000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_12_PL_SELECT_MASK      (0x03000000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_13_PL_SELECT_MASK      (0x0C000000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_14_PL_SELECT_MASK      (0x30000000)
#define MPI_SCSIDEVPAGE2_DPS_BIT_15_PL_SELECT_MASK      (0xC0000000)


typedef struct _CONFIG_PAGE_SCSI_DEVICE_3
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U16                     MsgRejectCount;             /* 04h */
    U16                     PhaseErrorCount;            /* 06h */
    U16                     ParityErrorCount;           /* 08h */
    U16                     Reserved;                   /* 0Ah */
} CONFIG_PAGE_SCSI_DEVICE_3, MPI_POINTER PTR_CONFIG_PAGE_SCSI_DEVICE_3,
  SCSIDevicePage3_t, MPI_POINTER pSCSIDevicePage3_t;

#define MPI_SCSIDEVPAGE3_PAGEVERSION                    (0x00)

#define MPI_SCSIDEVPAGE3_MAX_COUNTER                    (0xFFFE)
#define MPI_SCSIDEVPAGE3_UNSUPPORTED_COUNTER            (0xFFFF)


/****************************************************************************
*   FC Port Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_FC_PORT_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Flags;                      /* 04h */
    U8                      MPIPortNumber;              /* 08h */
    U8                      LinkType;                   /* 09h */
    U8                      PortState;                  /* 0Ah */
    U8                      Reserved;                   /* 0Bh */
    U32                     PortIdentifier;             /* 0Ch */
    U64                     WWNN;                       /* 10h */
    U64                     WWPN;                       /* 18h */
    U32                     SupportedServiceClass;      /* 20h */
    U32                     SupportedSpeeds;            /* 24h */
    U32                     CurrentSpeed;               /* 28h */
    U32                     MaxFrameSize;               /* 2Ch */
    U64                     FabricWWNN;                 /* 30h */
    U64                     FabricWWPN;                 /* 38h */
    U32                     DiscoveredPortsCount;       /* 40h */
    U32                     MaxInitiators;              /* 44h */
    U8                      MaxAliasesSupported;        /* 48h */
    U8                      MaxHardAliasesSupported;    /* 49h */
    U8                      NumCurrentAliases;          /* 4Ah */
    U8                      Reserved1;                  /* 4Bh */
} CONFIG_PAGE_FC_PORT_0, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_0,
  FCPortPage0_t, MPI_POINTER pFCPortPage0_t;

#define MPI_FCPORTPAGE0_PAGEVERSION                     (0x02)

#define MPI_FCPORTPAGE0_FLAGS_PROT_MASK                 (0x0000000F)
#define MPI_FCPORTPAGE0_FLAGS_PROT_FCP_INIT             (MPI_PORTFACTS_PROTOCOL_INITIATOR)
#define MPI_FCPORTPAGE0_FLAGS_PROT_FCP_TARG             (MPI_PORTFACTS_PROTOCOL_TARGET)
#define MPI_FCPORTPAGE0_FLAGS_PROT_LAN                  (MPI_PORTFACTS_PROTOCOL_LAN)
#define MPI_FCPORTPAGE0_FLAGS_PROT_LOGBUSADDR           (MPI_PORTFACTS_PROTOCOL_LOGBUSADDR)

#define MPI_FCPORTPAGE0_FLAGS_ALIAS_ALPA_SUPPORTED      (0x00000010)
#define MPI_FCPORTPAGE0_FLAGS_ALIAS_WWN_SUPPORTED       (0x00000020)
#define MPI_FCPORTPAGE0_FLAGS_FABRIC_WWN_VALID          (0x00000040)

#define MPI_FCPORTPAGE0_FLAGS_ATTACH_TYPE_MASK          (0x00000F00)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_NO_INIT            (0x00000000)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_POINT_TO_POINT     (0x00000100)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_PRIVATE_LOOP       (0x00000200)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_FABRIC_DIRECT      (0x00000400)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_PUBLIC_LOOP        (0x00000800)

#define MPI_FCPORTPAGE0_LTYPE_RESERVED                  (0x00)
#define MPI_FCPORTPAGE0_LTYPE_OTHER                     (0x01)
#define MPI_FCPORTPAGE0_LTYPE_UNKNOWN                   (0x02)
#define MPI_FCPORTPAGE0_LTYPE_COPPER                    (0x03)
#define MPI_FCPORTPAGE0_LTYPE_SINGLE_1300               (0x04)
#define MPI_FCPORTPAGE0_LTYPE_SINGLE_1500               (0x05)
#define MPI_FCPORTPAGE0_LTYPE_50_LASER_MULTI            (0x06)
#define MPI_FCPORTPAGE0_LTYPE_50_LED_MULTI              (0x07)
#define MPI_FCPORTPAGE0_LTYPE_62_LASER_MULTI            (0x08)
#define MPI_FCPORTPAGE0_LTYPE_62_LED_MULTI              (0x09)
#define MPI_FCPORTPAGE0_LTYPE_MULTI_LONG_WAVE           (0x0A)
#define MPI_FCPORTPAGE0_LTYPE_MULTI_SHORT_WAVE          (0x0B)
#define MPI_FCPORTPAGE0_LTYPE_LASER_SHORT_WAVE          (0x0C)
#define MPI_FCPORTPAGE0_LTYPE_LED_SHORT_WAVE            (0x0D)
#define MPI_FCPORTPAGE0_LTYPE_1300_LONG_WAVE            (0x0E)
#define MPI_FCPORTPAGE0_LTYPE_1500_LONG_WAVE            (0x0F)

#define MPI_FCPORTPAGE0_PORTSTATE_UNKNOWN               (0x01)      /*(SNIA)HBA_PORTSTATE_UNKNOWN       1 Unknown */
#define MPI_FCPORTPAGE0_PORTSTATE_ONLINE                (0x02)      /*(SNIA)HBA_PORTSTATE_ONLINE        2 Operational */
#define MPI_FCPORTPAGE0_PORTSTATE_OFFLINE               (0x03)      /*(SNIA)HBA_PORTSTATE_OFFLINE       3 User Offline */
#define MPI_FCPORTPAGE0_PORTSTATE_BYPASSED              (0x04)      /*(SNIA)HBA_PORTSTATE_BYPASSED      4 Bypassed */
#define MPI_FCPORTPAGE0_PORTSTATE_DIAGNOST              (0x05)      /*(SNIA)HBA_PORTSTATE_DIAGNOSTICS   5 In diagnostics mode */
#define MPI_FCPORTPAGE0_PORTSTATE_LINKDOWN              (0x06)      /*(SNIA)HBA_PORTSTATE_LINKDOWN      6 Link Down */
#define MPI_FCPORTPAGE0_PORTSTATE_ERROR                 (0x07)      /*(SNIA)HBA_PORTSTATE_ERROR         7 Port Error */
#define MPI_FCPORTPAGE0_PORTSTATE_LOOPBACK              (0x08)      /*(SNIA)HBA_PORTSTATE_LOOPBACK      8 Loopback */

#define MPI_FCPORTPAGE0_SUPPORT_CLASS_1                 (0x00000001)
#define MPI_FCPORTPAGE0_SUPPORT_CLASS_2                 (0x00000002)
#define MPI_FCPORTPAGE0_SUPPORT_CLASS_3                 (0x00000004)

#define MPI_FCPORTPAGE0_SUPPORT_SPEED_UKNOWN            (0x00000000) /* (SNIA)HBA_PORTSPEED_UNKNOWN 0   Unknown - transceiver incapable of reporting */
#define MPI_FCPORTPAGE0_SUPPORT_1GBIT_SPEED             (0x00000001) /* (SNIA)HBA_PORTSPEED_1GBIT   1   1 GBit/sec */
#define MPI_FCPORTPAGE0_SUPPORT_2GBIT_SPEED             (0x00000002) /* (SNIA)HBA_PORTSPEED_2GBIT   2   2 GBit/sec */
#define MPI_FCPORTPAGE0_SUPPORT_10GBIT_SPEED            (0x00000004) /* (SNIA)HBA_PORTSPEED_10GBIT  4  10 GBit/sec */
#define MPI_FCPORTPAGE0_SUPPORT_4GBIT_SPEED             (0x00000008) /* (SNIA)HBA_PORTSPEED_4GBIT   8   4 GBit/sec */

#define MPI_FCPORTPAGE0_CURRENT_SPEED_UKNOWN            MPI_FCPORTPAGE0_SUPPORT_SPEED_UKNOWN
#define MPI_FCPORTPAGE0_CURRENT_SPEED_1GBIT             MPI_FCPORTPAGE0_SUPPORT_1GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_2GBIT             MPI_FCPORTPAGE0_SUPPORT_2GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_10GBIT            MPI_FCPORTPAGE0_SUPPORT_10GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_4GBIT             MPI_FCPORTPAGE0_SUPPORT_4GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_NOT_NEGOTIATED    (0x00008000)        /* (SNIA)HBA_PORTSPEED_NOT_NEGOTIATED (1<<15) Speed not established */


typedef struct _CONFIG_PAGE_FC_PORT_1
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Flags;                      /* 04h */
    U64                     NoSEEPROMWWNN;              /* 08h */
    U64                     NoSEEPROMWWPN;              /* 10h */
    U8                      HardALPA;                   /* 18h */
    U8                      LinkConfig;                 /* 19h */
    U8                      TopologyConfig;             /* 1Ah */
    U8                      AltConnector;               /* 1Bh */
    U8                      NumRequestedAliases;        /* 1Ch */
    U8                      RR_TOV;                     /* 1Dh */
    U8                      InitiatorDeviceTimeout;     /* 1Eh */
    U8                      InitiatorIoPendTimeout;     /* 1Fh */
} CONFIG_PAGE_FC_PORT_1, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_1,
  FCPortPage1_t, MPI_POINTER pFCPortPage1_t;

#define MPI_FCPORTPAGE1_PAGEVERSION                     (0x06)

#define MPI_FCPORTPAGE1_FLAGS_EXT_FCP_STATUS_EN         (0x08000000)
#define MPI_FCPORTPAGE1_FLAGS_IMMEDIATE_ERROR_REPLY     (0x04000000)
#define MPI_FCPORTPAGE1_FLAGS_FORCE_USE_NOSEEPROM_WWNS  (0x02000000)
#define MPI_FCPORTPAGE1_FLAGS_VERBOSE_RESCAN_EVENTS     (0x01000000)
#define MPI_FCPORTPAGE1_FLAGS_TARGET_MODE_OXID          (0x00800000)
#define MPI_FCPORTPAGE1_FLAGS_PORT_OFFLINE              (0x00400000)
#define MPI_FCPORTPAGE1_FLAGS_SOFT_ALPA_FALLBACK        (0x00200000)
#define MPI_FCPORTPAGE1_FLAGS_TARGET_LARGE_CDB_ENABLE   (0x00000080)
#define MPI_FCPORTPAGE1_FLAGS_MASK_RR_TOV_UNITS         (0x00000070)
#define MPI_FCPORTPAGE1_FLAGS_SUPPRESS_PROT_REG         (0x00000008)
#define MPI_FCPORTPAGE1_FLAGS_PLOGI_ON_LOGO             (0x00000004)
#define MPI_FCPORTPAGE1_FLAGS_MAINTAIN_LOGINS           (0x00000002)
#define MPI_FCPORTPAGE1_FLAGS_SORT_BY_DID               (0x00000001)
#define MPI_FCPORTPAGE1_FLAGS_SORT_BY_WWN               (0x00000000)

#define MPI_FCPORTPAGE1_FLAGS_PROT_MASK                 (0xF0000000)
#define MPI_FCPORTPAGE1_FLAGS_PROT_SHIFT                (28)
#define MPI_FCPORTPAGE1_FLAGS_PROT_FCP_INIT             ((U32)MPI_PORTFACTS_PROTOCOL_INITIATOR << MPI_FCPORTPAGE1_FLAGS_PROT_SHIFT)
#define MPI_FCPORTPAGE1_FLAGS_PROT_FCP_TARG             ((U32)MPI_PORTFACTS_PROTOCOL_TARGET << MPI_FCPORTPAGE1_FLAGS_PROT_SHIFT)
#define MPI_FCPORTPAGE1_FLAGS_PROT_LAN                  ((U32)MPI_PORTFACTS_PROTOCOL_LAN << MPI_FCPORTPAGE1_FLAGS_PROT_SHIFT)
#define MPI_FCPORTPAGE1_FLAGS_PROT_LOGBUSADDR           ((U32)MPI_PORTFACTS_PROTOCOL_LOGBUSADDR << MPI_FCPORTPAGE1_FLAGS_PROT_SHIFT)

#define MPI_FCPORTPAGE1_FLAGS_NONE_RR_TOV_UNITS         (0x00000000)
#define MPI_FCPORTPAGE1_FLAGS_THOUSANDTH_RR_TOV_UNITS   (0x00000010)
#define MPI_FCPORTPAGE1_FLAGS_TENTH_RR_TOV_UNITS        (0x00000030)
#define MPI_FCPORTPAGE1_FLAGS_TEN_RR_TOV_UNITS          (0x00000050)

#define MPI_FCPORTPAGE1_HARD_ALPA_NOT_USED              (0xFF)

#define MPI_FCPORTPAGE1_LCONFIG_SPEED_MASK              (0x0F)
#define MPI_FCPORTPAGE1_LCONFIG_SPEED_1GIG              (0x00)
#define MPI_FCPORTPAGE1_LCONFIG_SPEED_2GIG              (0x01)
#define MPI_FCPORTPAGE1_LCONFIG_SPEED_4GIG              (0x02)
#define MPI_FCPORTPAGE1_LCONFIG_SPEED_10GIG             (0x03)
#define MPI_FCPORTPAGE1_LCONFIG_SPEED_AUTO              (0x0F)

#define MPI_FCPORTPAGE1_TOPOLOGY_MASK                   (0x0F)
#define MPI_FCPORTPAGE1_TOPOLOGY_NLPORT                 (0x01)
#define MPI_FCPORTPAGE1_TOPOLOGY_NPORT                  (0x02)
#define MPI_FCPORTPAGE1_TOPOLOGY_AUTO                   (0x0F)

#define MPI_FCPORTPAGE1_ALT_CONN_UNKNOWN                (0x00)

#define MPI_FCPORTPAGE1_INITIATOR_DEV_TIMEOUT_MASK      (0x7F)
#define MPI_FCPORTPAGE1_INITIATOR_DEV_UNIT_16           (0x80)


typedef struct _CONFIG_PAGE_FC_PORT_2
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U8                      NumberActive;               /* 04h */
    U8                      ALPA[127];                  /* 05h */
} CONFIG_PAGE_FC_PORT_2, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_2,
  FCPortPage2_t, MPI_POINTER pFCPortPage2_t;

#define MPI_FCPORTPAGE2_PAGEVERSION                     (0x01)


typedef struct _WWN_FORMAT
{
    U64                     WWNN;                       /* 00h */
    U64                     WWPN;                       /* 08h */
} WWN_FORMAT, MPI_POINTER PTR_WWN_FORMAT,
  WWNFormat, MPI_POINTER pWWNFormat;

typedef union _FC_PORT_PERSISTENT_PHYSICAL_ID
{
    WWN_FORMAT              WWN;
    U32                     Did;
} FC_PORT_PERSISTENT_PHYSICAL_ID, MPI_POINTER PTR_FC_PORT_PERSISTENT_PHYSICAL_ID,
  PersistentPhysicalId_t, MPI_POINTER pPersistentPhysicalId_t;

typedef struct _FC_PORT_PERSISTENT
{
    FC_PORT_PERSISTENT_PHYSICAL_ID  PhysicalIdentifier; /* 00h */
    U8                              TargetID;           /* 10h */
    U8                              Bus;                /* 11h */
    U16                             Flags;              /* 12h */
} FC_PORT_PERSISTENT, MPI_POINTER PTR_FC_PORT_PERSISTENT,
  PersistentData_t, MPI_POINTER pPersistentData_t;

#define MPI_PERSISTENT_FLAGS_SHIFT                      (16)
#define MPI_PERSISTENT_FLAGS_ENTRY_VALID                (0x0001)
#define MPI_PERSISTENT_FLAGS_SCAN_ID                    (0x0002)
#define MPI_PERSISTENT_FLAGS_SCAN_LUNS                  (0x0004)
#define MPI_PERSISTENT_FLAGS_BOOT_DEVICE                (0x0008)
#define MPI_PERSISTENT_FLAGS_BY_DID                     (0x0080)

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_FC_PORT_PAGE_3_ENTRY_MAX
#define MPI_FC_PORT_PAGE_3_ENTRY_MAX        (1)
#endif

typedef struct _CONFIG_PAGE_FC_PORT_3
{
    CONFIG_PAGE_HEADER      Header;                                 /* 00h */
    FC_PORT_PERSISTENT      Entry[MPI_FC_PORT_PAGE_3_ENTRY_MAX];    /* 04h */
} CONFIG_PAGE_FC_PORT_3, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_3,
  FCPortPage3_t, MPI_POINTER pFCPortPage3_t;

#define MPI_FCPORTPAGE3_PAGEVERSION                     (0x01)


typedef struct _CONFIG_PAGE_FC_PORT_4
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     PortFlags;                  /* 04h */
    U32                     PortSettings;               /* 08h */
} CONFIG_PAGE_FC_PORT_4, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_4,
  FCPortPage4_t, MPI_POINTER pFCPortPage4_t;

#define MPI_FCPORTPAGE4_PAGEVERSION                     (0x00)

#define MPI_FCPORTPAGE4_PORT_FLAGS_ALTERNATE_CHS        (0x00000008)

#define MPI_FCPORTPAGE4_PORT_MASK_INIT_HBA              (0x00000030)
#define MPI_FCPORTPAGE4_PORT_DISABLE_INIT_HBA           (0x00000000)
#define MPI_FCPORTPAGE4_PORT_BIOS_INIT_HBA              (0x00000010)
#define MPI_FCPORTPAGE4_PORT_OS_INIT_HBA                (0x00000020)
#define MPI_FCPORTPAGE4_PORT_BIOS_OS_INIT_HBA           (0x00000030)
#define MPI_FCPORTPAGE4_PORT_REMOVABLE_MEDIA            (0x000000C0)
#define MPI_FCPORTPAGE4_PORT_SPINUP_DELAY_MASK          (0x00000F00)


typedef struct _CONFIG_PAGE_FC_PORT_5_ALIAS_INFO
{
    U8      Flags;                                      /* 00h */
    U8      AliasAlpa;                                  /* 01h */
    U16     Reserved;                                   /* 02h */
    U64     AliasWWNN;                                  /* 04h */
    U64     AliasWWPN;                                  /* 0Ch */
} CONFIG_PAGE_FC_PORT_5_ALIAS_INFO,
  MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_5_ALIAS_INFO,
  FcPortPage5AliasInfo_t, MPI_POINTER pFcPortPage5AliasInfo_t;

typedef struct _CONFIG_PAGE_FC_PORT_5
{
    CONFIG_PAGE_HEADER                  Header;         /* 00h */
    CONFIG_PAGE_FC_PORT_5_ALIAS_INFO    AliasInfo;      /* 04h */
} CONFIG_PAGE_FC_PORT_5, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_5,
  FCPortPage5_t, MPI_POINTER pFCPortPage5_t;

#define MPI_FCPORTPAGE5_PAGEVERSION                     (0x02)

#define MPI_FCPORTPAGE5_FLAGS_ALPA_ACQUIRED             (0x01)
#define MPI_FCPORTPAGE5_FLAGS_HARD_ALPA                 (0x02)
#define MPI_FCPORTPAGE5_FLAGS_HARD_WWNN                 (0x04)
#define MPI_FCPORTPAGE5_FLAGS_HARD_WWPN                 (0x08)
#define MPI_FCPORTPAGE5_FLAGS_DISABLE                   (0x10)

typedef struct _CONFIG_PAGE_FC_PORT_6
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Reserved;                   /* 04h */
    U64                     TimeSinceReset;             /* 08h */
    U64                     TxFrames;                   /* 10h */
    U64                     RxFrames;                   /* 18h */
    U64                     TxWords;                    /* 20h */
    U64                     RxWords;                    /* 28h */
    U64                     LipCount;                   /* 30h */
    U64                     NosCount;                   /* 38h */
    U64                     ErrorFrames;                /* 40h */
    U64                     DumpedFrames;               /* 48h */
    U64                     LinkFailureCount;           /* 50h */
    U64                     LossOfSyncCount;            /* 58h */
    U64                     LossOfSignalCount;          /* 60h */
    U64                     PrimativeSeqErrCount;       /* 68h */
    U64                     InvalidTxWordCount;         /* 70h */
    U64                     InvalidCrcCount;            /* 78h */
    U64                     FcpInitiatorIoCount;        /* 80h */
} CONFIG_PAGE_FC_PORT_6, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_6,
  FCPortPage6_t, MPI_POINTER pFCPortPage6_t;

#define MPI_FCPORTPAGE6_PAGEVERSION                     (0x00)


typedef struct _CONFIG_PAGE_FC_PORT_7
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Reserved;                   /* 04h */
    U8                      PortSymbolicName[256];      /* 08h */
} CONFIG_PAGE_FC_PORT_7, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_7,
  FCPortPage7_t, MPI_POINTER pFCPortPage7_t;

#define MPI_FCPORTPAGE7_PAGEVERSION                     (0x00)


typedef struct _CONFIG_PAGE_FC_PORT_8
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     BitVector[8];               /* 04h */
} CONFIG_PAGE_FC_PORT_8, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_8,
  FCPortPage8_t, MPI_POINTER pFCPortPage8_t;

#define MPI_FCPORTPAGE8_PAGEVERSION                     (0x00)


typedef struct _CONFIG_PAGE_FC_PORT_9
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U32                     Reserved;                   /* 04h */
    U64                     GlobalWWPN;                 /* 08h */
    U64                     GlobalWWNN;                 /* 10h */
    U32                     UnitType;                   /* 18h */
    U32                     PhysicalPortNumber;         /* 1Ch */
    U32                     NumAttachedNodes;           /* 20h */
    U16                     IPVersion;                  /* 24h */
    U16                     UDPPortNumber;              /* 26h */
    U8                      IPAddress[16];              /* 28h */
    U16                     Reserved1;                  /* 38h */
    U16                     TopologyDiscoveryFlags;     /* 3Ah */
} CONFIG_PAGE_FC_PORT_9, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_9,
  FCPortPage9_t, MPI_POINTER pFCPortPage9_t;

#define MPI_FCPORTPAGE9_PAGEVERSION                     (0x00)


typedef struct _CONFIG_PAGE_FC_PORT_10_BASE_SFP_DATA
{
    U8                      Id;                         /* 10h */
    U8                      ExtId;                      /* 11h */
    U8                      Connector;                  /* 12h */
    U8                      Transceiver[8];             /* 13h */
    U8                      Encoding;                   /* 1Bh */
    U8                      BitRate_100mbs;             /* 1Ch */
    U8                      Reserved1;                  /* 1Dh */
    U8                      Length9u_km;                /* 1Eh */
    U8                      Length9u_100m;              /* 1Fh */
    U8                      Length50u_10m;              /* 20h */
    U8                      Length62p5u_10m;            /* 21h */
    U8                      LengthCopper_m;             /* 22h */
    U8                      Reseverved2;                /* 22h */
    U8                      VendorName[16];             /* 24h */
    U8                      Reserved3;                  /* 34h */
    U8                      VendorOUI[3];               /* 35h */
    U8                      VendorPN[16];               /* 38h */
    U8                      VendorRev[4];               /* 48h */
    U16                     Wavelength;                 /* 4Ch */
    U8                      Reserved4;                  /* 4Eh */
    U8                      CC_BASE;                    /* 4Fh */
} CONFIG_PAGE_FC_PORT_10_BASE_SFP_DATA,
  MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_10_BASE_SFP_DATA,
  FCPortPage10BaseSfpData_t, MPI_POINTER pFCPortPage10BaseSfpData_t;

#define MPI_FCPORT10_BASE_ID_UNKNOWN        (0x00)
#define MPI_FCPORT10_BASE_ID_GBIC           (0x01)
#define MPI_FCPORT10_BASE_ID_FIXED          (0x02)
#define MPI_FCPORT10_BASE_ID_SFP            (0x03)
#define MPI_FCPORT10_BASE_ID_SFP_MIN        (0x04)
#define MPI_FCPORT10_BASE_ID_SFP_MAX        (0x7F)
#define MPI_FCPORT10_BASE_ID_VEND_SPEC_MASK (0x80)

#define MPI_FCPORT10_BASE_EXTID_UNKNOWN     (0x00)
#define MPI_FCPORT10_BASE_EXTID_MODDEF1     (0x01)
#define MPI_FCPORT10_BASE_EXTID_MODDEF2     (0x02)
#define MPI_FCPORT10_BASE_EXTID_MODDEF3     (0x03)
#define MPI_FCPORT10_BASE_EXTID_SEEPROM     (0x04)
#define MPI_FCPORT10_BASE_EXTID_MODDEF5     (0x05)
#define MPI_FCPORT10_BASE_EXTID_MODDEF6     (0x06)
#define MPI_FCPORT10_BASE_EXTID_MODDEF7     (0x07)
#define MPI_FCPORT10_BASE_EXTID_VNDSPC_MASK (0x80)

#define MPI_FCPORT10_BASE_CONN_UNKNOWN      (0x00)
#define MPI_FCPORT10_BASE_CONN_SC           (0x01)
#define MPI_FCPORT10_BASE_CONN_COPPER1      (0x02)
#define MPI_FCPORT10_BASE_CONN_COPPER2      (0x03)
#define MPI_FCPORT10_BASE_CONN_BNC_TNC      (0x04)
#define MPI_FCPORT10_BASE_CONN_COAXIAL      (0x05)
#define MPI_FCPORT10_BASE_CONN_FIBERJACK    (0x06)
#define MPI_FCPORT10_BASE_CONN_LC           (0x07)
#define MPI_FCPORT10_BASE_CONN_MT_RJ        (0x08)
#define MPI_FCPORT10_BASE_CONN_MU           (0x09)
#define MPI_FCPORT10_BASE_CONN_SG           (0x0A)
#define MPI_FCPORT10_BASE_CONN_OPT_PIGT     (0x0B)
#define MPI_FCPORT10_BASE_CONN_RSV1_MIN     (0x0C)
#define MPI_FCPORT10_BASE_CONN_RSV1_MAX     (0x1F)
#define MPI_FCPORT10_BASE_CONN_HSSDC_II     (0x20)
#define MPI_FCPORT10_BASE_CONN_CPR_PIGT     (0x21)
#define MPI_FCPORT10_BASE_CONN_RSV2_MIN     (0x22)
#define MPI_FCPORT10_BASE_CONN_RSV2_MAX     (0x7F)
#define MPI_FCPORT10_BASE_CONN_VNDSPC_MASK  (0x80)

#define MPI_FCPORT10_BASE_ENCODE_UNSPEC     (0x00)
#define MPI_FCPORT10_BASE_ENCODE_8B10B      (0x01)
#define MPI_FCPORT10_BASE_ENCODE_4B5B       (0x02)
#define MPI_FCPORT10_BASE_ENCODE_NRZ        (0x03)
#define MPI_FCPORT10_BASE_ENCODE_MANCHESTER (0x04)


typedef struct _CONFIG_PAGE_FC_PORT_10_EXTENDED_SFP_DATA
{
    U8                      Options[2];                 /* 50h */
    U8                      BitRateMax;                 /* 52h */
    U8                      BitRateMin;                 /* 53h */
    U8                      VendorSN[16];               /* 54h */
    U8                      DateCode[8];                /* 64h */
    U8                      DiagMonitoringType;         /* 6Ch */
    U8                      EnhancedOptions;            /* 6Dh */
    U8                      SFF8472Compliance;          /* 6Eh */
    U8                      CC_EXT;                     /* 6Fh */
} CONFIG_PAGE_FC_PORT_10_EXTENDED_SFP_DATA,
  MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_10_EXTENDED_SFP_DATA,
  FCPortPage10ExtendedSfpData_t, MPI_POINTER pFCPortPage10ExtendedSfpData_t;

#define MPI_FCPORT10_EXT_OPTION1_RATESEL    (0x20)
#define MPI_FCPORT10_EXT_OPTION1_TX_DISABLE (0x10)
#define MPI_FCPORT10_EXT_OPTION1_TX_FAULT   (0x08)
#define MPI_FCPORT10_EXT_OPTION1_LOS_INVERT (0x04)
#define MPI_FCPORT10_EXT_OPTION1_LOS        (0x02)


typedef struct _CONFIG_PAGE_FC_PORT_10
{
    CONFIG_PAGE_HEADER                          Header;             /* 00h */
    U8                                          Flags;              /* 04h */
    U8                                          Reserved1;          /* 05h */
    U16                                         Reserved2;          /* 06h */
    U32                                         HwConfig1;          /* 08h */
    U32                                         HwConfig2;          /* 0Ch */
    CONFIG_PAGE_FC_PORT_10_BASE_SFP_DATA        Base;               /* 10h */
    CONFIG_PAGE_FC_PORT_10_EXTENDED_SFP_DATA    Extended;           /* 50h */
    U8                                          VendorSpecific[32]; /* 70h */
} CONFIG_PAGE_FC_PORT_10, MPI_POINTER PTR_CONFIG_PAGE_FC_PORT_10,
  FCPortPage10_t, MPI_POINTER pFCPortPage10_t;

#define MPI_FCPORTPAGE10_PAGEVERSION                    (0x01)

/* standard MODDEF pin definitions (from GBIC spec.) */
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_MASK              (0x00000007)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF2                  (0x00000001)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF1                  (0x00000002)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF0                  (0x00000004)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_NOGBIC            (0x00000007)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_CPR_IEEE_CX       (0x00000006)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_COPPER            (0x00000005)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_OPTICAL_LW        (0x00000004)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_SEEPROM           (0x00000003)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_SW_OPTICAL        (0x00000002)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_LX_IEEE_OPT_LW    (0x00000001)
#define MPI_FCPORTPAGE10_FLAGS_MODDEF_SX_IEEE_OPT_SW    (0x00000000)

#define MPI_FCPORTPAGE10_FLAGS_CC_BASE_OK               (0x00000010)
#define MPI_FCPORTPAGE10_FLAGS_CC_EXT_OK                (0x00000020)


/****************************************************************************
*   FC Device Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_FC_DEVICE_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    U64                     WWNN;                       /* 04h */
    U64                     WWPN;                       /* 0Ch */
    U32                     PortIdentifier;             /* 14h */
    U8                      Protocol;                   /* 18h */
    U8                      Flags;                      /* 19h */
    U16                     BBCredit;                   /* 1Ah */
    U16                     MaxRxFrameSize;             /* 1Ch */
    U8                      ADISCHardALPA;              /* 1Eh */
    U8                      PortNumber;                 /* 1Fh */
    U8                      FcPhLowestVersion;          /* 20h */
    U8                      FcPhHighestVersion;         /* 21h */
    U8                      CurrentTargetID;            /* 22h */
    U8                      CurrentBus;                 /* 23h */
} CONFIG_PAGE_FC_DEVICE_0, MPI_POINTER PTR_CONFIG_PAGE_FC_DEVICE_0,
  FCDevicePage0_t, MPI_POINTER pFCDevicePage0_t;

#define MPI_FC_DEVICE_PAGE0_PAGEVERSION                 (0x03)

#define MPI_FC_DEVICE_PAGE0_FLAGS_TARGETID_BUS_VALID    (0x01)
#define MPI_FC_DEVICE_PAGE0_FLAGS_PLOGI_INVALID         (0x02)
#define MPI_FC_DEVICE_PAGE0_FLAGS_PRLI_INVALID          (0x04)

#define MPI_FC_DEVICE_PAGE0_PROT_IP                     (0x01)
#define MPI_FC_DEVICE_PAGE0_PROT_FCP_TARGET             (0x02)
#define MPI_FC_DEVICE_PAGE0_PROT_FCP_INITIATOR          (0x04)
#define MPI_FC_DEVICE_PAGE0_PROT_FCP_RETRY              (0x08)

#define MPI_FC_DEVICE_PAGE0_PGAD_PORT_MASK      (MPI_FC_DEVICE_PGAD_PORT_MASK)
#define MPI_FC_DEVICE_PAGE0_PGAD_FORM_MASK      (MPI_FC_DEVICE_PGAD_FORM_MASK)
#define MPI_FC_DEVICE_PAGE0_PGAD_FORM_NEXT_DID  (MPI_FC_DEVICE_PGAD_FORM_NEXT_DID)
#define MPI_FC_DEVICE_PAGE0_PGAD_FORM_BUS_TID   (MPI_FC_DEVICE_PGAD_FORM_BUS_TID)
#define MPI_FC_DEVICE_PAGE0_PGAD_DID_MASK       (MPI_FC_DEVICE_PGAD_ND_DID_MASK)
#define MPI_FC_DEVICE_PAGE0_PGAD_BUS_MASK       (MPI_FC_DEVICE_PGAD_BT_BUS_MASK)
#define MPI_FC_DEVICE_PAGE0_PGAD_BUS_SHIFT      (MPI_FC_DEVICE_PGAD_BT_BUS_SHIFT)
#define MPI_FC_DEVICE_PAGE0_PGAD_TID_MASK       (MPI_FC_DEVICE_PGAD_BT_TID_MASK)

#define MPI_FC_DEVICE_PAGE0_HARD_ALPA_UNKNOWN   (0xFF)

/****************************************************************************
*   RAID Volume Config Pages
****************************************************************************/

typedef struct _RAID_VOL0_PHYS_DISK
{
    U16                         Reserved;               /* 00h */
    U8                          PhysDiskMap;            /* 02h */
    U8                          PhysDiskNum;            /* 03h */
} RAID_VOL0_PHYS_DISK, MPI_POINTER PTR_RAID_VOL0_PHYS_DISK,
  RaidVol0PhysDisk_t, MPI_POINTER pRaidVol0PhysDisk_t;

#define MPI_RAIDVOL0_PHYSDISK_PRIMARY                   (0x01)
#define MPI_RAIDVOL0_PHYSDISK_SECONDARY                 (0x02)

typedef struct _RAID_VOL0_STATUS
{
    U8                          Flags;                  /* 00h */
    U8                          State;                  /* 01h */
    U16                         Reserved;               /* 02h */
} RAID_VOL0_STATUS, MPI_POINTER PTR_RAID_VOL0_STATUS,
  RaidVol0Status_t, MPI_POINTER pRaidVol0Status_t;

/* RAID Volume Page 0 VolumeStatus defines */
#define MPI_RAIDVOL0_STATUS_FLAG_ENABLED                (0x01)
#define MPI_RAIDVOL0_STATUS_FLAG_QUIESCED               (0x02)
#define MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS     (0x04)
#define MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE        (0x08)
#define MPI_RAIDVOL0_STATUS_FLAG_BAD_BLOCK_TABLE_FULL   (0x10)

#define MPI_RAIDVOL0_STATUS_STATE_OPTIMAL               (0x00)
#define MPI_RAIDVOL0_STATUS_STATE_DEGRADED              (0x01)
#define MPI_RAIDVOL0_STATUS_STATE_FAILED                (0x02)
#define MPI_RAIDVOL0_STATUS_STATE_MISSING               (0x03)

typedef struct _RAID_VOL0_SETTINGS
{
    U16                         Settings;       /* 00h */
    U8                          HotSparePool;   /* 01h */ /* MPI_RAID_HOT_SPARE_POOL_ */
    U8                          Reserved;       /* 02h */
} RAID_VOL0_SETTINGS, MPI_POINTER PTR_RAID_VOL0_SETTINGS,
  RaidVol0Settings, MPI_POINTER pRaidVol0Settings;

/* RAID Volume Page 0 VolumeSettings defines */
#define MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE       (0x0001)
#define MPI_RAIDVOL0_SETTING_OFFLINE_ON_SMART           (0x0002)
#define MPI_RAIDVOL0_SETTING_AUTO_CONFIGURE             (0x0004)
#define MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC            (0x0008)
#define MPI_RAIDVOL0_SETTING_FAST_DATA_SCRUBBING_0102   (0x0020) /* obsolete */

#define MPI_RAIDVOL0_SETTING_MASK_METADATA_SIZE         (0x00C0)
#define MPI_RAIDVOL0_SETTING_64MB_METADATA_SIZE         (0x0000)
#define MPI_RAIDVOL0_SETTING_512MB_METADATA_SIZE        (0x0040)

#define MPI_RAIDVOL0_SETTING_USE_PRODUCT_ID_SUFFIX      (0x0010)
#define MPI_RAIDVOL0_SETTING_USE_DEFAULTS               (0x8000)

/* RAID Volume Page 0 HotSparePool defines, also used in RAID Physical Disk */
#define MPI_RAID_HOT_SPARE_POOL_0                       (0x01)
#define MPI_RAID_HOT_SPARE_POOL_1                       (0x02)
#define MPI_RAID_HOT_SPARE_POOL_2                       (0x04)
#define MPI_RAID_HOT_SPARE_POOL_3                       (0x08)
#define MPI_RAID_HOT_SPARE_POOL_4                       (0x10)
#define MPI_RAID_HOT_SPARE_POOL_5                       (0x20)
#define MPI_RAID_HOT_SPARE_POOL_6                       (0x40)
#define MPI_RAID_HOT_SPARE_POOL_7                       (0x80)

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX
#define MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX        (1)
#endif

typedef struct _CONFIG_PAGE_RAID_VOL_0
{
    CONFIG_PAGE_HEADER      Header;         /* 00h */
    U8                      VolumeID;       /* 04h */
    U8                      VolumeBus;      /* 05h */
    U8                      VolumeIOC;      /* 06h */
    U8                      VolumeType;     /* 07h */ /* MPI_RAID_VOL_TYPE_ */
    RAID_VOL0_STATUS        VolumeStatus;   /* 08h */
    RAID_VOL0_SETTINGS      VolumeSettings; /* 0Ch */
    U32                     MaxLBA;         /* 10h */
    U32                     MaxLBAHigh;     /* 14h */
    U32                     StripeSize;     /* 18h */
    U32                     Reserved2;      /* 1Ch */
    U32                     Reserved3;      /* 20h */
    U8                      NumPhysDisks;   /* 24h */
    U8                      DataScrubRate;  /* 25h */
    U8                      ResyncRate;     /* 26h */
    U8                      InactiveStatus; /* 27h */
    RAID_VOL0_PHYS_DISK     PhysDisk[MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX];/* 28h */
} CONFIG_PAGE_RAID_VOL_0, MPI_POINTER PTR_CONFIG_PAGE_RAID_VOL_0,
  RaidVolumePage0_t, MPI_POINTER pRaidVolumePage0_t;

#define MPI_RAIDVOLPAGE0_PAGEVERSION                    (0x07)

/* values for RAID Volume Page 0 InactiveStatus field */
#define MPI_RAIDVOLPAGE0_UNKNOWN_INACTIVE               (0x00)
#define MPI_RAIDVOLPAGE0_STALE_METADATA_INACTIVE        (0x01)
#define MPI_RAIDVOLPAGE0_FOREIGN_VOLUME_INACTIVE        (0x02)
#define MPI_RAIDVOLPAGE0_INSUFFICIENT_RESOURCE_INACTIVE (0x03)
#define MPI_RAIDVOLPAGE0_CLONE_VOLUME_INACTIVE          (0x04)
#define MPI_RAIDVOLPAGE0_INSUFFICIENT_METADATA_INACTIVE (0x05)
#define MPI_RAIDVOLPAGE0_PREVIOUSLY_DELETED             (0x06)


typedef struct _CONFIG_PAGE_RAID_VOL_1
{
    CONFIG_PAGE_HEADER      Header;         /* 00h */
    U8                      VolumeID;       /* 04h */
    U8                      VolumeBus;      /* 05h */
    U8                      VolumeIOC;      /* 06h */
    U8                      Reserved0;      /* 07h */
    U8                      GUID[24];       /* 08h */
    U8                      Name[32];       /* 20h */
    U64                     WWID;           /* 40h */
    U32                     Reserved1;      /* 48h */
    U32                     Reserved2;      /* 4Ch */
} CONFIG_PAGE_RAID_VOL_1, MPI_POINTER PTR_CONFIG_PAGE_RAID_VOL_1,
  RaidVolumePage1_t, MPI_POINTER pRaidVolumePage1_t;

#define MPI_RAIDVOLPAGE1_PAGEVERSION                    (0x01)


/****************************************************************************
*   RAID Physical Disk Config Pages
****************************************************************************/

typedef struct _RAID_PHYS_DISK0_ERROR_DATA
{
    U8                      ErrorCdbByte;               /* 00h */
    U8                      ErrorSenseKey;              /* 01h */
    U16                     Reserved;                   /* 02h */
    U16                     ErrorCount;                 /* 04h */
    U8                      ErrorASC;                   /* 06h */
    U8                      ErrorASCQ;                  /* 07h */
    U16                     SmartCount;                 /* 08h */
    U8                      SmartASC;                   /* 0Ah */
    U8                      SmartASCQ;                  /* 0Bh */
} RAID_PHYS_DISK0_ERROR_DATA, MPI_POINTER PTR_RAID_PHYS_DISK0_ERROR_DATA,
  RaidPhysDisk0ErrorData_t, MPI_POINTER pRaidPhysDisk0ErrorData_t;

typedef struct _RAID_PHYS_DISK_INQUIRY_DATA
{
    U8                          VendorID[8];            /* 00h */
    U8                          ProductID[16];          /* 08h */
    U8                          ProductRevLevel[4];     /* 18h */
    U8                          Info[32];               /* 1Ch */
} RAID_PHYS_DISK0_INQUIRY_DATA, MPI_POINTER PTR_RAID_PHYS_DISK0_INQUIRY_DATA,
  RaidPhysDisk0InquiryData, MPI_POINTER pRaidPhysDisk0InquiryData;

typedef struct _RAID_PHYS_DISK0_SETTINGS
{
    U8              SepID;              /* 00h */
    U8              SepBus;             /* 01h */
    U8              HotSparePool;       /* 02h */ /* MPI_RAID_HOT_SPARE_POOL_ */
    U8              PhysDiskSettings;   /* 03h */
} RAID_PHYS_DISK0_SETTINGS, MPI_POINTER PTR_RAID_PHYS_DISK0_SETTINGS,
  RaidPhysDiskSettings_t, MPI_POINTER pRaidPhysDiskSettings_t;

typedef struct _RAID_PHYS_DISK0_STATUS
{
    U8                              Flags;              /* 00h */
    U8                              State;              /* 01h */
    U16                             Reserved;           /* 02h */
} RAID_PHYS_DISK0_STATUS, MPI_POINTER PTR_RAID_PHYS_DISK0_STATUS,
  RaidPhysDiskStatus_t, MPI_POINTER pRaidPhysDiskStatus_t;

/* RAID Physical Disk PhysDiskStatus flags */

#define MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC           (0x01)
#define MPI_PHYSDISK0_STATUS_FLAG_QUIESCED              (0x02)
#define MPI_PHYSDISK0_STATUS_FLAG_INACTIVE_VOLUME       (0x04)
#define MPI_PHYSDISK0_STATUS_FLAG_OPTIMAL_PREVIOUS      (0x00)
#define MPI_PHYSDISK0_STATUS_FLAG_NOT_OPTIMAL_PREVIOUS  (0x08)

#define MPI_PHYSDISK0_STATUS_ONLINE                     (0x00)
#define MPI_PHYSDISK0_STATUS_MISSING                    (0x01)
#define MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE             (0x02)
#define MPI_PHYSDISK0_STATUS_FAILED                     (0x03)
#define MPI_PHYSDISK0_STATUS_INITIALIZING               (0x04)
#define MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED          (0x05)
#define MPI_PHYSDISK0_STATUS_FAILED_REQUESTED           (0x06)
#define MPI_PHYSDISK0_STATUS_OTHER_OFFLINE              (0xFF)

typedef struct _CONFIG_PAGE_RAID_PHYS_DISK_0
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U8                              PhysDiskID;         /* 04h */
    U8                              PhysDiskBus;        /* 05h */
    U8                              PhysDiskIOC;        /* 06h */
    U8                              PhysDiskNum;        /* 07h */
    RAID_PHYS_DISK0_SETTINGS        PhysDiskSettings;   /* 08h */
    U32                             Reserved1;          /* 0Ch */
    U8                              ExtDiskIdentifier[8]; /* 10h */
    U8                              DiskIdentifier[16]; /* 18h */
    RAID_PHYS_DISK0_INQUIRY_DATA    InquiryData;        /* 28h */
    RAID_PHYS_DISK0_STATUS          PhysDiskStatus;     /* 64h */
    U32                             MaxLBA;             /* 68h */
    RAID_PHYS_DISK0_ERROR_DATA      ErrorData;          /* 6Ch */
} CONFIG_PAGE_RAID_PHYS_DISK_0, MPI_POINTER PTR_CONFIG_PAGE_RAID_PHYS_DISK_0,
  RaidPhysDiskPage0_t, MPI_POINTER pRaidPhysDiskPage0_t;

#define MPI_RAIDPHYSDISKPAGE0_PAGEVERSION           (0x02)


typedef struct _RAID_PHYS_DISK1_PATH
{
    U8                              PhysDiskID;         /* 00h */
    U8                              PhysDiskBus;        /* 01h */
    U16                             Reserved1;          /* 02h */
    U64                             WWID;               /* 04h */
    U64                             OwnerWWID;          /* 0Ch */
    U8                              OwnerIdentifier;    /* 14h */
    U8                              Reserved2;          /* 15h */
    U16                             Flags;              /* 16h */
} RAID_PHYS_DISK1_PATH, MPI_POINTER PTR_RAID_PHYS_DISK1_PATH,
  RaidPhysDisk1Path_t, MPI_POINTER pRaidPhysDisk1Path_t;

/* RAID Physical Disk Page 1 Flags field defines */
#define MPI_RAID_PHYSDISK1_FLAG_BROKEN          (0x0002)
#define MPI_RAID_PHYSDISK1_FLAG_INVALID         (0x0001)


/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength or NumPhysDiskPaths at runtime.
 */
#ifndef MPI_RAID_PHYS_DISK1_PATH_MAX
#define MPI_RAID_PHYS_DISK1_PATH_MAX    (1)
#endif

typedef struct _CONFIG_PAGE_RAID_PHYS_DISK_1
{
    CONFIG_PAGE_HEADER              Header;             /* 00h */
    U8                              NumPhysDiskPaths;   /* 04h */
    U8                              PhysDiskNum;        /* 05h */
    U16                             Reserved2;          /* 06h */
    U32                             Reserved1;          /* 08h */
    RAID_PHYS_DISK1_PATH            Path[MPI_RAID_PHYS_DISK1_PATH_MAX];/* 0Ch */
} CONFIG_PAGE_RAID_PHYS_DISK_1, MPI_POINTER PTR_CONFIG_PAGE_RAID_PHYS_DISK_1,
  RaidPhysDiskPage1_t, MPI_POINTER pRaidPhysDiskPage1_t;

#define MPI_RAIDPHYSDISKPAGE1_PAGEVERSION       (0x00)


/****************************************************************************
*   LAN Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_LAN_0
{
    ConfigPageHeader_t      Header;                     /* 00h */
    U16                     TxRxModes;                  /* 04h */
    U16                     Reserved;                   /* 06h */
    U32                     PacketPrePad;               /* 08h */
} CONFIG_PAGE_LAN_0, MPI_POINTER PTR_CONFIG_PAGE_LAN_0,
  LANPage0_t, MPI_POINTER pLANPage0_t;

#define MPI_LAN_PAGE0_PAGEVERSION                       (0x01)

#define MPI_LAN_PAGE0_RETURN_LOOPBACK                   (0x0000)
#define MPI_LAN_PAGE0_SUPPRESS_LOOPBACK                 (0x0001)
#define MPI_LAN_PAGE0_LOOPBACK_MASK                     (0x0001)

typedef struct _CONFIG_PAGE_LAN_1
{
    ConfigPageHeader_t      Header;                     /* 00h */
    U16                     Reserved;                   /* 04h */
    U8                      CurrentDeviceState;         /* 06h */
    U8                      Reserved1;                  /* 07h */
    U32                     MinPacketSize;              /* 08h */
    U32                     MaxPacketSize;              /* 0Ch */
    U32                     HardwareAddressLow;         /* 10h */
    U32                     HardwareAddressHigh;        /* 14h */
    U32                     MaxWireSpeedLow;            /* 18h */
    U32                     MaxWireSpeedHigh;           /* 1Ch */
    U32                     BucketsRemaining;           /* 20h */
    U32                     MaxReplySize;               /* 24h */
    U32                     NegWireSpeedLow;            /* 28h */
    U32                     NegWireSpeedHigh;           /* 2Ch */
} CONFIG_PAGE_LAN_1, MPI_POINTER PTR_CONFIG_PAGE_LAN_1,
  LANPage1_t, MPI_POINTER pLANPage1_t;

#define MPI_LAN_PAGE1_PAGEVERSION                       (0x03)

#define MPI_LAN_PAGE1_DEV_STATE_RESET                   (0x00)
#define MPI_LAN_PAGE1_DEV_STATE_OPERATIONAL             (0x01)


/****************************************************************************
*   Inband Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_INBAND_0
{
    CONFIG_PAGE_HEADER      Header;                     /* 00h */
    MPI_VERSION_FORMAT      InbandVersion;              /* 04h */
    U16                     MaximumBuffers;             /* 08h */
    U16                     Reserved1;                  /* 0Ah */
} CONFIG_PAGE_INBAND_0, MPI_POINTER PTR_CONFIG_PAGE_INBAND_0,
  InbandPage0_t, MPI_POINTER pInbandPage0_t;

#define MPI_INBAND_PAGEVERSION          (0x00)



/****************************************************************************
*   SAS IO Unit Config Pages
****************************************************************************/

typedef struct _MPI_SAS_IO_UNIT0_PHY_DATA
{
    U8          Port;                   /* 00h */
    U8          PortFlags;              /* 01h */
    U8          PhyFlags;               /* 02h */
    U8          NegotiatedLinkRate;     /* 03h */
    U32         ControllerPhyDeviceInfo;/* 04h */
    U16         AttachedDeviceHandle;   /* 08h */
    U16         ControllerDevHandle;    /* 0Ah */
    U32         DiscoveryStatus;        /* 0Ch */
} MPI_SAS_IO_UNIT0_PHY_DATA, MPI_POINTER PTR_MPI_SAS_IO_UNIT0_PHY_DATA,
  SasIOUnit0PhyData, MPI_POINTER pSasIOUnit0PhyData;

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_SAS_IOUNIT0_PHY_MAX
#define MPI_SAS_IOUNIT0_PHY_MAX         (1)
#endif

typedef struct _CONFIG_PAGE_SAS_IO_UNIT_0
{
    CONFIG_EXTENDED_PAGE_HEADER     Header;                             /* 00h */
    U16                             NvdataVersionDefault;               /* 08h */
    U16                             NvdataVersionPersistent;            /* 0Ah */
    U8                              NumPhys;                            /* 0Ch */
    U8                              Reserved2;                          /* 0Dh */
    U16                             Reserved3;                          /* 0Eh */
    MPI_SAS_IO_UNIT0_PHY_DATA       PhyData[MPI_SAS_IOUNIT0_PHY_MAX];   /* 10h */
} CONFIG_PAGE_SAS_IO_UNIT_0, MPI_POINTER PTR_CONFIG_PAGE_SAS_IO_UNIT_0,
  SasIOUnitPage0_t, MPI_POINTER pSasIOUnitPage0_t;

#define MPI_SASIOUNITPAGE0_PAGEVERSION      (0x04)

/* values for SAS IO Unit Page 0 PortFlags */
#define MPI_SAS_IOUNIT0_PORT_FLAGS_DISCOVERY_IN_PROGRESS    (0x08)
#define MPI_SAS_IOUNIT0_PORT_FLAGS_0_TARGET_IOC_NUM         (0x00)
#define MPI_SAS_IOUNIT0_PORT_FLAGS_1_TARGET_IOC_NUM         (0x04)
#define MPI_SAS_IOUNIT0_PORT_FLAGS_AUTO_PORT_CONFIG         (0x01)

/* values for SAS IO Unit Page 0 PhyFlags */
#define MPI_SAS_IOUNIT0_PHY_FLAGS_PHY_DISABLED              (0x04)
#define MPI_SAS_IOUNIT0_PHY_FLAGS_TX_INVERT                 (0x02)
#define MPI_SAS_IOUNIT0_PHY_FLAGS_RX_INVERT                 (0x01)

/* values for SAS IO Unit Page 0 NegotiatedLinkRate */
#define MPI_SAS_IOUNIT0_RATE_UNKNOWN                        (0x00)
#define MPI_SAS_IOUNIT0_RATE_PHY_DISABLED                   (0x01)
#define MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION       (0x02)
#define MPI_SAS_IOUNIT0_RATE_SATA_OOB_COMPLETE              (0x03)
#define MPI_SAS_IOUNIT0_RATE_1_5                            (0x08)
#define MPI_SAS_IOUNIT0_RATE_3_0                            (0x09)
#define MPI_SAS_IOUNIT0_RATE_6_0                            (0x0A)

/* see mpi_sas.h for values for SAS IO Unit Page 0 ControllerPhyDeviceInfo values */

/* values for SAS IO Unit Page 0 DiscoveryStatus */
#define MPI_SAS_IOUNIT0_DS_LOOP_DETECTED                    (0x00000001)
#define MPI_SAS_IOUNIT0_DS_UNADDRESSABLE_DEVICE             (0x00000002)
#define MPI_SAS_IOUNIT0_DS_MULTIPLE_PORTS                   (0x00000004)
#define MPI_SAS_IOUNIT0_DS_EXPANDER_ERR                     (0x00000008)
#define MPI_SAS_IOUNIT0_DS_SMP_TIMEOUT                      (0x00000010)
#define MPI_SAS_IOUNIT0_DS_OUT_ROUTE_ENTRIES                (0x00000020)
#define MPI_SAS_IOUNIT0_DS_INDEX_NOT_EXIST                  (0x00000040)
#define MPI_SAS_IOUNIT0_DS_SMP_FUNCTION_FAILED              (0x00000080)
#define MPI_SAS_IOUNIT0_DS_SMP_CRC_ERROR                    (0x00000100)
#define MPI_SAS_IOUNIT0_DS_SUBTRACTIVE_LINK                 (0x00000200)
#define MPI_SAS_IOUNIT0_DS_TABLE_LINK                       (0x00000400)
#define MPI_SAS_IOUNIT0_DS_UNSUPPORTED_DEVICE               (0x00000800)
#define MPI_SAS_IOUNIT0_DS_MAX_SATA_TARGETS                 (0x00001000)
#define MPI_SAS_IOUNIT0_DS_MULTI_PORT_DOMAIN                (0x00002000)


typedef struct _MPI_SAS_IO_UNIT1_PHY_DATA
{
    U8          Port;                       /* 00h */
    U8          PortFlags;                  /* 01h */
    U8          PhyFlags;                   /* 02h */
    U8          MaxMinLinkRate;             /* 03h */
    U32         ControllerPhyDeviceInfo;    /* 04h */
    U16         MaxTargetPortConnectTime;   /* 08h */
    U16         Reserved1;                  /* 0Ah */
} MPI_SAS_IO_UNIT1_PHY_DATA, MPI_POINTER PTR_MPI_SAS_IO_UNIT1_PHY_DATA,
  SasIOUnit1PhyData, MPI_POINTER pSasIOUnit1PhyData;

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check Header.PageLength at runtime.
 */
#ifndef MPI_SAS_IOUNIT1_PHY_MAX
#define MPI_SAS_IOUNIT1_PHY_MAX         (1)
#endif

typedef struct _CONFIG_PAGE_SAS_IO_UNIT_1
{
    CONFIG_EXTENDED_PAGE_HEADER Header;                             /* 00h */
    U16                         ControlFlags;                       /* 08h */
    U16                         MaxNumSATATargets;                  /* 0Ah */
    U16                         AdditionalControlFlags;             /* 0Ch */
    U16                         Reserved1;                          /* 0Eh */
    U8                          NumPhys;                            /* 10h */
    U8                          SATAMaxQDepth;                      /* 11h */
    U8                          ReportDeviceMissingDelay;           /* 12h */
    U8                          IODeviceMissingDelay;               /* 13h */
    MPI_SAS_IO_UNIT1_PHY_DATA   PhyData[MPI_SAS_IOUNIT1_PHY_MAX];   /* 14h */
} CONFIG_PAGE_SAS_IO_UNIT_1, MPI_POINTER PTR_CONFIG_PAGE_SAS_IO_UNIT_1,
  SasIOUnitPage1_t, MPI_POINTER pSasIOUnitPage1_t;

#define MPI_SASIOUNITPAGE1_PAGEVERSION      (0x07)

/* values for SAS IO Unit Page 1 ControlFlags */
#define MPI_SAS_IOUNIT1_CONTROL_DEVICE_SELF_TEST            (0x8000)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_3_0_MAX                (0x4000)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_1_5_MAX                (0x2000)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_SW_PRESERVE            (0x1000)
#define MPI_SAS_IOUNIT1_CONTROL_DISABLE_SAS_HASH            (0x0800)

#define MPI_SAS_IOUNIT1_CONTROL_MASK_DEV_SUPPORT            (0x0600)
#define MPI_SAS_IOUNIT1_CONTROL_SHIFT_DEV_SUPPORT           (9)
#define MPI_SAS_IOUNIT1_CONTROL_DEV_SUPPORT_BOTH            (0x00)
#define MPI_SAS_IOUNIT1_CONTROL_DEV_SAS_SUPPORT             (0x01)
#define MPI_SAS_IOUNIT1_CONTROL_DEV_SATA_SUPPORT            (0x02)

#define MPI_SAS_IOUNIT1_CONTROL_POSTPONE_SATA_INIT          (0x0100)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_48BIT_LBA_REQUIRED     (0x0080)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_SMART_REQUIRED         (0x0040)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_NCQ_REQUIRED           (0x0020)
#define MPI_SAS_IOUNIT1_CONTROL_SATA_FUA_REQUIRED           (0x0010)
#define MPI_SAS_IOUNIT1_CONTROL_PHY_ENABLE_ORDER_HIGH       (0x0008)
#define MPI_SAS_IOUNIT1_CONTROL_SUBTRACTIVE_ILLEGAL         (0x0004)
#define MPI_SAS_IOUNIT1_CONTROL_FIRST_LVL_DISC_ONLY         (0x0002)
#define MPI_SAS_IOUNIT1_CONTROL_CLEAR_AFFILIATION           (0x0001)

/* values for SAS IO Unit Page 1 AdditionalControlFlags */
#define MPI_SAS_IOUNIT1_ACONTROL_MULTI_PORT_DOMAIN_ILLEGAL          (0x0080)
#define MPI_SAS_IOUNIT1_ACONTROL_SATA_ASYNCHROUNOUS_NOTIFICATION    (0x0040)
#define MPI_SAS_IOUNIT1_ACONTROL_HIDE_NONZERO_ATTACHED_PHY_IDENT    (0x0020)
#define MPI_SAS_IOUNIT1_ACONTROL_PORT_ENABLE_ONLY_SATA_LINK_RESET   (0x0010)
#define MPI_SAS_IOUNIT1_ACONTROL_OTHER_AFFILIATION_SATA_LINK_RESET  (0x0008)
#define MPI_SAS_IOUNIT1_ACONTROL_SELF_AFFILIATION_SATA_LINK_RESET   (0x0004)
#define MPI_SAS_IOUNIT1_ACONTROL_NO_AFFILIATION_SATA_LINK_RESET     (0x0002)
#define MPI_SAS_IOUNIT1_ACONTROL_ALLOW_TABLE_TO_TABLE               (0x0001)

/* defines for SAS IO Unit Page 1 ReportDeviceMissingDelay */
#define MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK         (0x7F)
#define MPI_SAS_IOUNIT1_REPORT_MISSING_UNIT_16              (0x80)

/* values for SAS IO Unit Page 1 PortFlags */
#define MPI_SAS_IOUNIT1_PORT_FLAGS_0_TARGET_IOC_NUM         (0x00)
#define MPI_SAS_IOUNIT1_PORT_FLAGS_1_TARGET_IOC_NUM         (0x04)
#define MPI_SAS_IOUNIT1_PORT_FLAGS_AUTO_PORT_CONFIG         (0x01)

/* values for SAS IO Unit Page 0 PhyFlags */
#define MPI_SAS_IOUNIT1_PHY_FLAGS_PHY_DISABLE               (0x04)
#define MPI_SAS_IOUNIT1_PHY_FLAGS_TX_INVERT                 (0x02)
#define MPI_SAS_IOUNIT1_PHY_FLAGS_RX_INVERT                 (0x01)

/* values for SAS IO Unit Page 0 MaxMinLinkRate */
#define MPI_SAS_IOUNIT1_MAX_RATE_MASK                       (0xF0)
#define MPI_SAS_IOUNIT1_MAX_RATE_1_5                        (0x80)
#define MPI_SAS_IOUNIT1_MAX_RATE_3_0                        (0x90)
#define MPI_SAS_IOUNIT1_MIN_RATE_MASK                       (0x0F)
#define MPI_SAS_IOUNIT1_MIN_RATE_1_5                        (0x08)
#define MPI_SAS_IOUNIT1_MIN_RATE_3_0                        (0x09)

/* see mpi_sas.h for values for SAS IO Unit Page 1 ControllerPhyDeviceInfo values */


typedef struct _CONFIG_PAGE_SAS_IO_UNIT_2
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U8                                  NumDevsPerEnclosure;    /* 08h */
    U8                                  Reserved1;              /* 09h */
    U16                                 Reserved2;              /* 0Ah */
    U16                                 MaxPersistentIDs;       /* 0Ch */
    U16                                 NumPersistentIDsUsed;   /* 0Eh */
    U8                                  Status;                 /* 10h */
    U8                                  Flags;                  /* 11h */
    U16                                 MaxNumPhysicalMappedIDs;/* 12h */
} CONFIG_PAGE_SAS_IO_UNIT_2, MPI_POINTER PTR_CONFIG_PAGE_SAS_IO_UNIT_2,
  SasIOUnitPage2_t, MPI_POINTER pSasIOUnitPage2_t;

#define MPI_SASIOUNITPAGE2_PAGEVERSION      (0x06)

/* values for SAS IO Unit Page 2 Status field */
#define MPI_SAS_IOUNIT2_STATUS_DEVICE_LIMIT_EXCEEDED        (0x08)
#define MPI_SAS_IOUNIT2_STATUS_ENCLOSURE_DEVICES_UNMAPPED   (0x04)
#define MPI_SAS_IOUNIT2_STATUS_DISABLED_PERSISTENT_MAPPINGS (0x02)
#define MPI_SAS_IOUNIT2_STATUS_FULL_PERSISTENT_MAPPINGS     (0x01)

/* values for SAS IO Unit Page 2 Flags field */
#define MPI_SAS_IOUNIT2_FLAGS_DISABLE_PERSISTENT_MAPPINGS   (0x01)
/* Physical Mapping Modes */
#define MPI_SAS_IOUNIT2_FLAGS_MASK_PHYS_MAP_MODE            (0x0E)
#define MPI_SAS_IOUNIT2_FLAGS_SHIFT_PHYS_MAP_MODE           (1)
#define MPI_SAS_IOUNIT2_FLAGS_NO_PHYS_MAP                   (0x00)
#define MPI_SAS_IOUNIT2_FLAGS_DIRECT_ATTACH_PHYS_MAP        (0x01)
#define MPI_SAS_IOUNIT2_FLAGS_ENCLOSURE_SLOT_PHYS_MAP       (0x02)
#define MPI_SAS_IOUNIT2_FLAGS_HOST_ASSIGNED_PHYS_MAP        (0x07)

#define MPI_SAS_IOUNIT2_FLAGS_RESERVE_ID_0_FOR_BOOT         (0x10)
#define MPI_SAS_IOUNIT2_FLAGS_DA_STARTING_SLOT              (0x20)


typedef struct _CONFIG_PAGE_SAS_IO_UNIT_3
{
    CONFIG_EXTENDED_PAGE_HEADER Header;                         /* 00h */
    U32                         Reserved1;                      /* 08h */
    U32                         MaxInvalidDwordCount;           /* 0Ch */
    U32                         InvalidDwordCountTime;          /* 10h */
    U32                         MaxRunningDisparityErrorCount;  /* 14h */
    U32                         RunningDisparityErrorTime;      /* 18h */
    U32                         MaxLossDwordSynchCount;         /* 1Ch */
    U32                         LossDwordSynchCountTime;        /* 20h */
    U32                         MaxPhyResetProblemCount;        /* 24h */
    U32                         PhyResetProblemTime;            /* 28h */
} CONFIG_PAGE_SAS_IO_UNIT_3, MPI_POINTER PTR_CONFIG_PAGE_SAS_IO_UNIT_3,
  SasIOUnitPage3_t, MPI_POINTER pSasIOUnitPage3_t;

#define MPI_SASIOUNITPAGE3_PAGEVERSION      (0x00)


/****************************************************************************
*   SAS Expander Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_SAS_EXPANDER_0
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U8                                  PhysicalPort;           /* 08h */
    U8                                  Reserved1;              /* 09h */
    U16                                 EnclosureHandle;        /* 0Ah */
    U64                                 SASAddress;             /* 0Ch */
    U32                                 DiscoveryStatus;        /* 14h */
    U16                                 DevHandle;              /* 18h */
    U16                                 ParentDevHandle;        /* 1Ah */
    U16                                 ExpanderChangeCount;    /* 1Ch */
    U16                                 ExpanderRouteIndexes;   /* 1Eh */
    U8                                  NumPhys;                /* 20h */
    U8                                  SASLevel;               /* 21h */
    U8                                  Flags;                  /* 22h */
    U8                                  Reserved3;              /* 23h */
} CONFIG_PAGE_SAS_EXPANDER_0, MPI_POINTER PTR_CONFIG_PAGE_SAS_EXPANDER_0,
  SasExpanderPage0_t, MPI_POINTER pSasExpanderPage0_t;

#define MPI_SASEXPANDER0_PAGEVERSION        (0x03)

/* values for SAS Expander Page 0 DiscoveryStatus field */
#define MPI_SAS_EXPANDER0_DS_LOOP_DETECTED              (0x00000001)
#define MPI_SAS_EXPANDER0_DS_UNADDRESSABLE_DEVICE       (0x00000002)
#define MPI_SAS_EXPANDER0_DS_MULTIPLE_PORTS             (0x00000004)
#define MPI_SAS_EXPANDER0_DS_EXPANDER_ERR               (0x00000008)
#define MPI_SAS_EXPANDER0_DS_SMP_TIMEOUT                (0x00000010)
#define MPI_SAS_EXPANDER0_DS_OUT_ROUTE_ENTRIES          (0x00000020)
#define MPI_SAS_EXPANDER0_DS_INDEX_NOT_EXIST            (0x00000040)
#define MPI_SAS_EXPANDER0_DS_SMP_FUNCTION_FAILED        (0x00000080)
#define MPI_SAS_EXPANDER0_DS_SMP_CRC_ERROR              (0x00000100)
#define MPI_SAS_EXPANDER0_DS_SUBTRACTIVE_LINK           (0x00000200)
#define MPI_SAS_EXPANDER0_DS_TABLE_LINK                 (0x00000400)
#define MPI_SAS_EXPANDER0_DS_UNSUPPORTED_DEVICE         (0x00000800)

/* values for SAS Expander Page 0 Flags field */
#define MPI_SAS_EXPANDER0_FLAGS_CONNECTOR_END_DEVICE    (0x04)
#define MPI_SAS_EXPANDER0_FLAGS_ROUTE_TABLE_CONFIG      (0x02)
#define MPI_SAS_EXPANDER0_FLAGS_CONFIG_IN_PROGRESS      (0x01)


typedef struct _CONFIG_PAGE_SAS_EXPANDER_1
{
    CONFIG_EXTENDED_PAGE_HEADER Header;                 /* 00h */
    U8                          PhysicalPort;           /* 08h */
    U8                          Reserved1;              /* 09h */
    U16                         Reserved2;              /* 0Ah */
    U8                          NumPhys;                /* 0Ch */
    U8                          Phy;                    /* 0Dh */
    U16                         NumTableEntriesProgrammed; /* 0Eh */
    U8                          ProgrammedLinkRate;     /* 10h */
    U8                          HwLinkRate;             /* 11h */
    U16                         AttachedDevHandle;      /* 12h */
    U32                         PhyInfo;                /* 14h */
    U32                         AttachedDeviceInfo;     /* 18h */
    U16                         OwnerDevHandle;         /* 1Ch */
    U8                          ChangeCount;            /* 1Eh */
    U8                          NegotiatedLinkRate;     /* 1Fh */
    U8                          PhyIdentifier;          /* 20h */
    U8                          AttachedPhyIdentifier;  /* 21h */
    U8                          Reserved3;              /* 22h */
    U8                          DiscoveryInfo;          /* 23h */
    U32                         Reserved4;              /* 24h */
} CONFIG_PAGE_SAS_EXPANDER_1, MPI_POINTER PTR_CONFIG_PAGE_SAS_EXPANDER_1,
  SasExpanderPage1_t, MPI_POINTER pSasExpanderPage1_t;

#define MPI_SASEXPANDER1_PAGEVERSION        (0x01)

/* use MPI_SAS_PHY0_PRATE_ defines for ProgrammedLinkRate */

/* use MPI_SAS_PHY0_HWRATE_ defines for HwLinkRate */

/* use MPI_SAS_PHY0_PHYINFO_ defines for PhyInfo */

/* see mpi_sas.h for values for SAS Expander Page 1 AttachedDeviceInfo values */

/* values for SAS Expander Page 1 DiscoveryInfo field */
#define MPI_SAS_EXPANDER1_DISCINFO_BAD_PHY_DISABLED     (0x04)
#define MPI_SAS_EXPANDER1_DISCINFO_LINK_STATUS_CHANGE   (0x02)
#define MPI_SAS_EXPANDER1_DISCINFO_NO_ROUTING_ENTRIES   (0x01)

/* values for SAS Expander Page 1 NegotiatedLinkRate field */
#define MPI_SAS_EXPANDER1_NEG_RATE_UNKNOWN              (0x00)
#define MPI_SAS_EXPANDER1_NEG_RATE_PHY_DISABLED         (0x01)
#define MPI_SAS_EXPANDER1_NEG_RATE_FAILED_NEGOTIATION   (0x02)
#define MPI_SAS_EXPANDER1_NEG_RATE_SATA_OOB_COMPLETE    (0x03)
#define MPI_SAS_EXPANDER1_NEG_RATE_1_5                  (0x08)
#define MPI_SAS_EXPANDER1_NEG_RATE_3_0                  (0x09)


/****************************************************************************
*   SAS Device Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_SAS_DEVICE_0
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U16                                 Slot;                   /* 08h */
    U16                                 EnclosureHandle;        /* 0Ah */
    U64                                 SASAddress;             /* 0Ch */
    U16                                 ParentDevHandle;        /* 14h */
    U8                                  PhyNum;                 /* 16h */
    U8                                  AccessStatus;           /* 17h */
    U16                                 DevHandle;              /* 18h */
    U8                                  TargetID;               /* 1Ah */
    U8                                  Bus;                    /* 1Bh */
    U32                                 DeviceInfo;             /* 1Ch */
    U16                                 Flags;                  /* 20h */
    U8                                  PhysicalPort;           /* 22h */
    U8                                  Reserved2;              /* 23h */
} CONFIG_PAGE_SAS_DEVICE_0, MPI_POINTER PTR_CONFIG_PAGE_SAS_DEVICE_0,
  SasDevicePage0_t, MPI_POINTER pSasDevicePage0_t;

#define MPI_SASDEVICE0_PAGEVERSION          (0x05)

/* values for SAS Device Page 0 AccessStatus field */
#define MPI_SAS_DEVICE0_ASTATUS_NO_ERRORS                   (0x00)
#define MPI_SAS_DEVICE0_ASTATUS_SATA_INIT_FAILED            (0x01)
#define MPI_SAS_DEVICE0_ASTATUS_SATA_CAPABILITY_FAILED      (0x02)
#define MPI_SAS_DEVICE0_ASTATUS_SATA_AFFILIATION_CONFLICT   (0x03)
#define MPI_SAS_DEVICE0_ASTATUS_SATA_NEEDS_INITIALIZATION   (0x04)
/* specific values for SATA Init failures */
#define MPI_SAS_DEVICE0_ASTATUS_SIF_UNKNOWN                 (0x10)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_AFFILIATION_CONFLICT    (0x11)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_DIAG                    (0x12)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_IDENTIFICATION          (0x13)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_CHECK_POWER             (0x14)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_PIO_SN                  (0x15)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_MDMA_SN                 (0x16)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_UDMA_SN                 (0x17)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_ZONING_VIOLATION        (0x18)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_NOT_ADDRESSABLE         (0x19)
#define MPI_SAS_DEVICE0_ASTATUS_SIF_MAX                     (0x1F)

/* values for SAS Device Page 0 Flags field */
#define MPI_SAS_DEVICE0_FLAGS_SATA_ASYNCHRONOUS_NOTIFY      (0x0400)
#define MPI_SAS_DEVICE0_FLAGS_SATA_SW_PRESERVE              (0x0200)
#define MPI_SAS_DEVICE0_FLAGS_UNSUPPORTED_DEVICE            (0x0100)
#define MPI_SAS_DEVICE0_FLAGS_SATA_48BIT_LBA_SUPPORTED      (0x0080)
#define MPI_SAS_DEVICE0_FLAGS_SATA_SMART_SUPPORTED          (0x0040)
#define MPI_SAS_DEVICE0_FLAGS_SATA_NCQ_SUPPORTED            (0x0020)
#define MPI_SAS_DEVICE0_FLAGS_SATA_FUA_SUPPORTED            (0x0010)
#define MPI_SAS_DEVICE0_FLAGS_PORT_SELECTOR_ATTACH          (0x0008)
#define MPI_SAS_DEVICE0_FLAGS_MAPPING_PERSISTENT            (0x0004)
#define MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED                 (0x0002)
#define MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT                (0x0001)

/* see mpi_sas.h for values for SAS Device Page 0 DeviceInfo values */


typedef struct _CONFIG_PAGE_SAS_DEVICE_1
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U32                                 Reserved1;              /* 08h */
    U64                                 SASAddress;             /* 0Ch */
    U32                                 Reserved2;              /* 14h */
    U16                                 DevHandle;              /* 18h */
    U8                                  TargetID;               /* 1Ah */
    U8                                  Bus;                    /* 1Bh */
    U8                                  InitialRegDeviceFIS[20];/* 1Ch */
} CONFIG_PAGE_SAS_DEVICE_1, MPI_POINTER PTR_CONFIG_PAGE_SAS_DEVICE_1,
  SasDevicePage1_t, MPI_POINTER pSasDevicePage1_t;

#define MPI_SASDEVICE1_PAGEVERSION          (0x00)


typedef struct _CONFIG_PAGE_SAS_DEVICE_2
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U64                                 PhysicalIdentifier;     /* 08h */
    U32                                 EnclosureMapping;       /* 10h */
} CONFIG_PAGE_SAS_DEVICE_2, MPI_POINTER PTR_CONFIG_PAGE_SAS_DEVICE_2,
  SasDevicePage2_t, MPI_POINTER pSasDevicePage2_t;

#define MPI_SASDEVICE2_PAGEVERSION          (0x01)

/* defines for SAS Device Page 2 EnclosureMapping field */
#define MPI_SASDEVICE2_ENC_MAP_MASK_MISSING_COUNT       (0x0000000F)
#define MPI_SASDEVICE2_ENC_MAP_SHIFT_MISSING_COUNT      (0)
#define MPI_SASDEVICE2_ENC_MAP_MASK_NUM_SLOTS           (0x000007F0)
#define MPI_SASDEVICE2_ENC_MAP_SHIFT_NUM_SLOTS          (4)
#define MPI_SASDEVICE2_ENC_MAP_MASK_START_INDEX         (0x001FF800)
#define MPI_SASDEVICE2_ENC_MAP_SHIFT_START_INDEX        (11)


/****************************************************************************
*   SAS PHY Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_SAS_PHY_0
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U16                                 OwnerDevHandle;         /* 08h */
    U16                                 Reserved1;              /* 0Ah */
    U64                                 SASAddress;             /* 0Ch */
    U16                                 AttachedDevHandle;      /* 14h */
    U8                                  AttachedPhyIdentifier;  /* 16h */
    U8                                  Reserved2;              /* 17h */
    U32                                 AttachedDeviceInfo;     /* 18h */
    U8                                  ProgrammedLinkRate;     /* 1Ch */
    U8                                  HwLinkRate;             /* 1Dh */
    U8                                  ChangeCount;            /* 1Eh */
    U8                                  Flags;                  /* 1Fh */
    U32                                 PhyInfo;                /* 20h */
} CONFIG_PAGE_SAS_PHY_0, MPI_POINTER PTR_CONFIG_PAGE_SAS_PHY_0,
  SasPhyPage0_t, MPI_POINTER pSasPhyPage0_t;

#define MPI_SASPHY0_PAGEVERSION             (0x01)

/* values for SAS PHY Page 0 ProgrammedLinkRate field */
#define MPI_SAS_PHY0_PRATE_MAX_RATE_MASK                        (0xF0)
#define MPI_SAS_PHY0_PRATE_MAX_RATE_NOT_PROGRAMMABLE            (0x00)
#define MPI_SAS_PHY0_PRATE_MAX_RATE_1_5                         (0x80)
#define MPI_SAS_PHY0_PRATE_MAX_RATE_3_0                         (0x90)
#define MPI_SAS_PHY0_PRATE_MIN_RATE_MASK                        (0x0F)
#define MPI_SAS_PHY0_PRATE_MIN_RATE_NOT_PROGRAMMABLE            (0x00)
#define MPI_SAS_PHY0_PRATE_MIN_RATE_1_5                         (0x08)
#define MPI_SAS_PHY0_PRATE_MIN_RATE_3_0                         (0x09)

/* values for SAS PHY Page 0 HwLinkRate field */
#define MPI_SAS_PHY0_HWRATE_MAX_RATE_MASK                       (0xF0)
#define MPI_SAS_PHY0_HWRATE_MAX_RATE_1_5                        (0x80)
#define MPI_SAS_PHY0_HWRATE_MAX_RATE_3_0                        (0x90)
#define MPI_SAS_PHY0_HWRATE_MIN_RATE_MASK                       (0x0F)
#define MPI_SAS_PHY0_HWRATE_MIN_RATE_1_5                        (0x08)
#define MPI_SAS_PHY0_HWRATE_MIN_RATE_3_0                        (0x09)

/* values for SAS PHY Page 0 Flags field */
#define MPI_SAS_PHY0_FLAGS_SGPIO_DIRECT_ATTACH_ENC              (0x01)

/* values for SAS PHY Page 0 PhyInfo field */
#define MPI_SAS_PHY0_PHYINFO_SATA_PORT_ACTIVE                   (0x00004000)
#define MPI_SAS_PHY0_PHYINFO_SATA_PORT_SELECTOR                 (0x00002000)
#define MPI_SAS_PHY0_PHYINFO_VIRTUAL_PHY                        (0x00001000)

#define MPI_SAS_PHY0_PHYINFO_MASK_PARTIAL_PATHWAY_TIME          (0x00000F00)
#define MPI_SAS_PHY0_PHYINFO_SHIFT_PARTIAL_PATHWAY_TIME         (8)

#define MPI_SAS_PHY0_PHYINFO_MASK_ROUTING_ATTRIBUTE             (0x000000F0)
#define MPI_SAS_PHY0_PHYINFO_DIRECT_ROUTING                     (0x00000000)
#define MPI_SAS_PHY0_PHYINFO_SUBTRACTIVE_ROUTING                (0x00000010)
#define MPI_SAS_PHY0_PHYINFO_TABLE_ROUTING                      (0x00000020)

#define MPI_SAS_PHY0_PHYINFO_MASK_LINK_RATE                     (0x0000000F)
#define MPI_SAS_PHY0_PHYINFO_UNKNOWN_LINK_RATE                  (0x00000000)
#define MPI_SAS_PHY0_PHYINFO_PHY_DISABLED                       (0x00000001)
#define MPI_SAS_PHY0_PHYINFO_NEGOTIATION_FAILED                 (0x00000002)
#define MPI_SAS_PHY0_PHYINFO_SATA_OOB_COMPLETE                  (0x00000003)
#define MPI_SAS_PHY0_PHYINFO_RATE_1_5                           (0x00000008)
#define MPI_SAS_PHY0_PHYINFO_RATE_3_0                           (0x00000009)


typedef struct _CONFIG_PAGE_SAS_PHY_1
{
    CONFIG_EXTENDED_PAGE_HEADER Header;                     /* 00h */
    U32                         Reserved1;                  /* 08h */
    U32                         InvalidDwordCount;          /* 0Ch */
    U32                         RunningDisparityErrorCount; /* 10h */
    U32                         LossDwordSynchCount;        /* 14h */
    U32                         PhyResetProblemCount;       /* 18h */
} CONFIG_PAGE_SAS_PHY_1, MPI_POINTER PTR_CONFIG_PAGE_SAS_PHY_1,
  SasPhyPage1_t, MPI_POINTER pSasPhyPage1_t;

#define MPI_SASPHY1_PAGEVERSION             (0x00)


/****************************************************************************
*   SAS Enclosure Config Pages
****************************************************************************/

typedef struct _CONFIG_PAGE_SAS_ENCLOSURE_0
{
    CONFIG_EXTENDED_PAGE_HEADER         Header;                 /* 00h */
    U32                                 Reserved1;              /* 08h */
    U64                                 EnclosureLogicalID;     /* 0Ch */
    U16                                 Flags;                  /* 14h */
    U16                                 EnclosureHandle;        /* 16h */
    U16                                 NumSlots;               /* 18h */
    U16                                 StartSlot;              /* 1Ah */
    U8                                  StartTargetID;          /* 1Ch */
    U8                                  StartBus;               /* 1Dh */
    U8                                  SEPTargetID;            /* 1Eh */
    U8                                  SEPBus;                 /* 1Fh */
    U32                                 Reserved2;              /* 20h */
    U32                                 Reserved3;              /* 24h */
} CONFIG_PAGE_SAS_ENCLOSURE_0, MPI_POINTER PTR_CONFIG_PAGE_SAS_ENCLOSURE_0,
  SasEnclosurePage0_t, MPI_POINTER pSasEnclosurePage0_t;

#define MPI_SASENCLOSURE0_PAGEVERSION       (0x01)

/* values for SAS Enclosure Page 0 Flags field */
#define MPI_SAS_ENCLS0_FLAGS_SEP_BUS_ID_VALID       (0x0020)
#define MPI_SAS_ENCLS0_FLAGS_START_BUS_ID_VALID     (0x0010)

#define MPI_SAS_ENCLS0_FLAGS_MNG_MASK               (0x000F)
#define MPI_SAS_ENCLS0_FLAGS_MNG_UNKNOWN            (0x0000)
#define MPI_SAS_ENCLS0_FLAGS_MNG_IOC_SES            (0x0001)
#define MPI_SAS_ENCLS0_FLAGS_MNG_IOC_SGPIO          (0x0002)
#define MPI_SAS_ENCLS0_FLAGS_MNG_EXP_SGPIO          (0x0003)
#define MPI_SAS_ENCLS0_FLAGS_MNG_SES_ENCLOSURE      (0x0004)
#define MPI_SAS_ENCLS0_FLAGS_MNG_IOC_GPIO           (0x0005)


/****************************************************************************
*   Log Config Pages
****************************************************************************/
/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check NumLogEntries at runtime.
 */
#ifndef MPI_LOG_0_NUM_LOG_ENTRIES
#define MPI_LOG_0_NUM_LOG_ENTRIES        (1)
#endif

#define MPI_LOG_0_LOG_DATA_LENGTH        (0x1C)

typedef struct _MPI_LOG_0_ENTRY
{
    U32         TimeStamp;                          /* 00h */
    U32         Reserved1;                          /* 04h */
    U16         LogSequence;                        /* 08h */
    U16         LogEntryQualifier;                  /* 0Ah */
    U8          LogData[MPI_LOG_0_LOG_DATA_LENGTH]; /* 0Ch */
} MPI_LOG_0_ENTRY, MPI_POINTER PTR_MPI_LOG_0_ENTRY,
  MpiLog0Entry_t, MPI_POINTER pMpiLog0Entry_t;

/* values for Log Page 0 LogEntry LogEntryQualifier field */
#define MPI_LOG_0_ENTRY_QUAL_ENTRY_UNUSED           (0x0000)
#define MPI_LOG_0_ENTRY_QUAL_POWER_ON_RESET         (0x0001)

typedef struct _CONFIG_PAGE_LOG_0
{
    CONFIG_EXTENDED_PAGE_HEADER Header;                     /* 00h */
    U32                         Reserved1;                  /* 08h */
    U32                         Reserved2;                  /* 0Ch */
    U16                         NumLogEntries;              /* 10h */
    U16                         Reserved3;                  /* 12h */
    MPI_LOG_0_ENTRY             LogEntry[MPI_LOG_0_NUM_LOG_ENTRIES]; /* 14h */
} CONFIG_PAGE_LOG_0, MPI_POINTER PTR_CONFIG_PAGE_LOG_0,
  LogPage0_t, MPI_POINTER pLogPage0_t;

#define MPI_LOG_0_PAGEVERSION               (0x01)


#endif

