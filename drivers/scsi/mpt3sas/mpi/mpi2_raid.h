/*
 * Copyright 2000-2014 Avago Technologies.  All rights reserved.
 *
 *
 *          Name:  mpi2_raid.h
 *         Title:  MPI Integrated RAID messages and structures
 * Creation Date:  April 26, 2007
 *
 *   mpi2_raid.h Version:  02.00.11
 *
 * Version History
 * ---------------
 *
 * Date      Version   Description
 * --------  --------  ------------------------------------------------------
 * 04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 * 08-31-07  02.00.01  Modifications to RAID Action request and reply,
 *                     including the Actions and ActionData.
 * 02-29-08  02.00.02  Added MPI2_RAID_ACTION_ADATA_DISABL_FULL_REBUILD.
 * 05-21-08  02.00.03  Added MPI2_RAID_VOL_CREATION_NUM_PHYSDISKS so that
 *                     the PhysDisk array in MPI2_RAID_VOLUME_CREATION_STRUCT
 *                     can be sized by the build environment.
 * 07-30-09  02.00.04  Added proper define for the Use Default Settings bit of
 *                     VolumeCreationFlags and marked the old one as obsolete.
 * 05-12-10  02.00.05  Added MPI2_RAID_VOL_FLAGS_OP_MDC define.
 * 08-24-10  02.00.06  Added MPI2_RAID_ACTION_COMPATIBILITY_CHECK along with
 *                     related structures and defines.
 *                     Added product-specific range to RAID Action values.
 * 11-18-11  02.00.07  Incorporating additions for MPI v2.5.
 * 02-06-12  02.00.08  Added MPI2_RAID_ACTION_PHYSDISK_HIDDEN.
 * 07-26-12  02.00.09  Added ElapsedSeconds field to MPI2_RAID_VOL_INDICATOR.
 *                     Added MPI2_RAID_VOL_FLAGS_ELAPSED_SECONDS_VALID define.
 * 04-17-13  02.00.10  Added MPI25_RAID_ACTION_ADATA_ALLOW_PI.
 * 11-18-14  02.00.11  Updated copyright information.
 * --------------------------------------------------------------------------
 */

#ifndef MPI2_RAID_H
#define MPI2_RAID_H

/*****************************************************************************
*
*              Integrated RAID Messages
*
*****************************************************************************/

/****************************************************************************
* RAID Action messages
****************************************************************************/

/* ActionDataWord defines for use with MPI2_RAID_ACTION_CREATE_VOLUME action */
#define MPI25_RAID_ACTION_ADATA_ALLOW_PI            (0x80000000)

/*ActionDataWord defines for use with MPI2_RAID_ACTION_DELETE_VOLUME action */
#define MPI2_RAID_ACTION_ADATA_KEEP_LBA0            (0x00000000)
#define MPI2_RAID_ACTION_ADATA_ZERO_LBA0            (0x00000001)

/*use MPI2_RAIDVOL0_SETTING_ defines from mpi2_cnfg.h for
 *MPI2_RAID_ACTION_CHANGE_VOL_WRITE_CACHE action */

/*ActionDataWord defines for use with
 *MPI2_RAID_ACTION_DISABLE_ALL_VOLUMES action */
#define MPI2_RAID_ACTION_ADATA_DISABL_FULL_REBUILD  (0x00000001)

/*ActionDataWord for MPI2_RAID_ACTION_SET_RAID_FUNCTION_RATE Action */
typedef struct _MPI2_RAID_ACTION_RATE_DATA {
	U8 RateToChange;	/*0x00 */
	U8 RateOrMode;		/*0x01 */
	U16 DataScrubDuration;	/*0x02 */
} MPI2_RAID_ACTION_RATE_DATA, *PTR_MPI2_RAID_ACTION_RATE_DATA,
	Mpi2RaidActionRateData_t, *pMpi2RaidActionRateData_t;

#define MPI2_RAID_ACTION_SET_RATE_RESYNC            (0x00)
#define MPI2_RAID_ACTION_SET_RATE_DATA_SCRUB        (0x01)
#define MPI2_RAID_ACTION_SET_RATE_POWERSAVE_MODE    (0x02)

/*ActionDataWord for MPI2_RAID_ACTION_START_RAID_FUNCTION Action */
typedef struct _MPI2_RAID_ACTION_START_RAID_FUNCTION {
	U8 RAIDFunction;	/*0x00 */
	U8 Flags;		/*0x01 */
	U16 Reserved1;		/*0x02 */
} MPI2_RAID_ACTION_START_RAID_FUNCTION,
	*PTR_MPI2_RAID_ACTION_START_RAID_FUNCTION,
	Mpi2RaidActionStartRaidFunction_t,
	*pMpi2RaidActionStartRaidFunction_t;

/*defines for the RAIDFunction field */
#define MPI2_RAID_ACTION_START_BACKGROUND_INIT      (0x00)
#define MPI2_RAID_ACTION_START_ONLINE_CAP_EXPANSION (0x01)
#define MPI2_RAID_ACTION_START_CONSISTENCY_CHECK    (0x02)

/*defines for the Flags field */
#define MPI2_RAID_ACTION_START_NEW                  (0x00)
#define MPI2_RAID_ACTION_START_RESUME               (0x01)

/*ActionDataWord for MPI2_RAID_ACTION_STOP_RAID_FUNCTION Action */
typedef struct _MPI2_RAID_ACTION_STOP_RAID_FUNCTION {
	U8 RAIDFunction;	/*0x00 */
	U8 Flags;		/*0x01 */
	U16 Reserved1;		/*0x02 */
} MPI2_RAID_ACTION_STOP_RAID_FUNCTION,
	*PTR_MPI2_RAID_ACTION_STOP_RAID_FUNCTION,
	Mpi2RaidActionStopRaidFunction_t,
	*pMpi2RaidActionStopRaidFunction_t;

/*defines for the RAIDFunction field */
#define MPI2_RAID_ACTION_STOP_BACKGROUND_INIT       (0x00)
#define MPI2_RAID_ACTION_STOP_ONLINE_CAP_EXPANSION  (0x01)
#define MPI2_RAID_ACTION_STOP_CONSISTENCY_CHECK     (0x02)

/*defines for the Flags field */
#define MPI2_RAID_ACTION_STOP_ABORT                 (0x00)
#define MPI2_RAID_ACTION_STOP_PAUSE                 (0x01)

/*ActionDataWord for MPI2_RAID_ACTION_CREATE_HOT_SPARE Action */
typedef struct _MPI2_RAID_ACTION_HOT_SPARE {
	U8 HotSparePool;	/*0x00 */
	U8 Reserved1;		/*0x01 */
	U16 DevHandle;		/*0x02 */
} MPI2_RAID_ACTION_HOT_SPARE, *PTR_MPI2_RAID_ACTION_HOT_SPARE,
	Mpi2RaidActionHotSpare_t, *pMpi2RaidActionHotSpare_t;

/*ActionDataWord for MPI2_RAID_ACTION_DEVICE_FW_UPDATE_MODE Action */
typedef struct _MPI2_RAID_ACTION_FW_UPDATE_MODE {
	U8 Flags;		/*0x00 */
	U8 DeviceFirmwareUpdateModeTimeout;	/*0x01 */
	U16 Reserved1;		/*0x02 */
} MPI2_RAID_ACTION_FW_UPDATE_MODE,
	*PTR_MPI2_RAID_ACTION_FW_UPDATE_MODE,
	Mpi2RaidActionFwUpdateMode_t,
	*pMpi2RaidActionFwUpdateMode_t;

/*ActionDataWord defines for use with
 *MPI2_RAID_ACTION_DEVICE_FW_UPDATE_MODE action */
#define MPI2_RAID_ACTION_ADATA_DISABLE_FW_UPDATE        (0x00)
#define MPI2_RAID_ACTION_ADATA_ENABLE_FW_UPDATE         (0x01)

typedef union _MPI2_RAID_ACTION_DATA {
	U32 Word;
	MPI2_RAID_ACTION_RATE_DATA Rates;
	MPI2_RAID_ACTION_START_RAID_FUNCTION StartRaidFunction;
	MPI2_RAID_ACTION_STOP_RAID_FUNCTION StopRaidFunction;
	MPI2_RAID_ACTION_HOT_SPARE HotSpare;
	MPI2_RAID_ACTION_FW_UPDATE_MODE FwUpdateMode;
} MPI2_RAID_ACTION_DATA, *PTR_MPI2_RAID_ACTION_DATA,
	Mpi2RaidActionData_t, *pMpi2RaidActionData_t;

/*RAID Action Request Message */
typedef struct _MPI2_RAID_ACTION_REQUEST {
	U8 Action;		/*0x00 */
	U8 Reserved1;		/*0x01 */
	U8 ChainOffset;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 VolDevHandle;	/*0x04 */
	U8 PhysDiskNum;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved2;		/*0x0A */
	U32 Reserved3;		/*0x0C */
	MPI2_RAID_ACTION_DATA ActionDataWord;	/*0x10 */
	MPI2_SGE_SIMPLE_UNION ActionDataSGE;	/*0x14 */
} MPI2_RAID_ACTION_REQUEST, *PTR_MPI2_RAID_ACTION_REQUEST,
	Mpi2RaidActionRequest_t, *pMpi2RaidActionRequest_t;

/*RAID Action request Action values */

#define MPI2_RAID_ACTION_INDICATOR_STRUCT           (0x01)
#define MPI2_RAID_ACTION_CREATE_VOLUME              (0x02)
#define MPI2_RAID_ACTION_DELETE_VOLUME              (0x03)
#define MPI2_RAID_ACTION_DISABLE_ALL_VOLUMES        (0x04)
#define MPI2_RAID_ACTION_ENABLE_ALL_VOLUMES         (0x05)
#define MPI2_RAID_ACTION_PHYSDISK_OFFLINE           (0x0A)
#define MPI2_RAID_ACTION_PHYSDISK_ONLINE            (0x0B)
#define MPI2_RAID_ACTION_FAIL_PHYSDISK              (0x0F)
#define MPI2_RAID_ACTION_ACTIVATE_VOLUME            (0x11)
#define MPI2_RAID_ACTION_DEVICE_FW_UPDATE_MODE      (0x15)
#define MPI2_RAID_ACTION_CHANGE_VOL_WRITE_CACHE     (0x17)
#define MPI2_RAID_ACTION_SET_VOLUME_NAME            (0x18)
#define MPI2_RAID_ACTION_SET_RAID_FUNCTION_RATE     (0x19)
#define MPI2_RAID_ACTION_ENABLE_FAILED_VOLUME       (0x1C)
#define MPI2_RAID_ACTION_CREATE_HOT_SPARE           (0x1D)
#define MPI2_RAID_ACTION_DELETE_HOT_SPARE           (0x1E)
#define MPI2_RAID_ACTION_SYSTEM_SHUTDOWN_INITIATED  (0x20)
#define MPI2_RAID_ACTION_START_RAID_FUNCTION        (0x21)
#define MPI2_RAID_ACTION_STOP_RAID_FUNCTION         (0x22)
#define MPI2_RAID_ACTION_COMPATIBILITY_CHECK        (0x23)
#define MPI2_RAID_ACTION_PHYSDISK_HIDDEN            (0x24)
#define MPI2_RAID_ACTION_MIN_PRODUCT_SPECIFIC       (0x80)
#define MPI2_RAID_ACTION_MAX_PRODUCT_SPECIFIC       (0xFF)

/*RAID Volume Creation Structure */

/*
 *The following define can be customized for the targeted product.
 */
#ifndef MPI2_RAID_VOL_CREATION_NUM_PHYSDISKS
#define MPI2_RAID_VOL_CREATION_NUM_PHYSDISKS        (1)
#endif

typedef struct _MPI2_RAID_VOLUME_PHYSDISK {
	U8 RAIDSetNum;		/*0x00 */
	U8 PhysDiskMap;		/*0x01 */
	U16 PhysDiskDevHandle;	/*0x02 */
} MPI2_RAID_VOLUME_PHYSDISK, *PTR_MPI2_RAID_VOLUME_PHYSDISK,
	Mpi2RaidVolumePhysDisk_t, *pMpi2RaidVolumePhysDisk_t;

/*defines for the PhysDiskMap field */
#define MPI2_RAIDACTION_PHYSDISK_PRIMARY            (0x01)
#define MPI2_RAIDACTION_PHYSDISK_SECONDARY          (0x02)

typedef struct _MPI2_RAID_VOLUME_CREATION_STRUCT {
	U8 NumPhysDisks;	/*0x00 */
	U8 VolumeType;		/*0x01 */
	U16 Reserved1;		/*0x02 */
	U32 VolumeCreationFlags;	/*0x04 */
	U32 VolumeSettings;	/*0x08 */
	U8 Reserved2;		/*0x0C */
	U8 ResyncRate;		/*0x0D */
	U16 DataScrubDuration;	/*0x0E */
	U64 VolumeMaxLBA;	/*0x10 */
	U32 StripeSize;		/*0x18 */
	U8 Name[16];		/*0x1C */
	MPI2_RAID_VOLUME_PHYSDISK
		PhysDisk[MPI2_RAID_VOL_CREATION_NUM_PHYSDISKS];	/*0x2C */
} MPI2_RAID_VOLUME_CREATION_STRUCT,
	*PTR_MPI2_RAID_VOLUME_CREATION_STRUCT,
	Mpi2RaidVolumeCreationStruct_t,
	*pMpi2RaidVolumeCreationStruct_t;

/*use MPI2_RAID_VOL_TYPE_ defines from mpi2_cnfg.h for VolumeType */

/*defines for the VolumeCreationFlags field */
#define MPI2_RAID_VOL_CREATION_DEFAULT_SETTINGS     (0x80000000)
#define MPI2_RAID_VOL_CREATION_BACKGROUND_INIT      (0x00000004)
#define MPI2_RAID_VOL_CREATION_LOW_LEVEL_INIT       (0x00000002)
#define MPI2_RAID_VOL_CREATION_MIGRATE_DATA         (0x00000001)
/*The following is an obsolete define.
 *It must be shifted left 24 bits in order to set the proper bit.
 */
#define MPI2_RAID_VOL_CREATION_USE_DEFAULT_SETTINGS (0x80)

/*RAID Online Capacity Expansion Structure */

typedef struct _MPI2_RAID_ONLINE_CAPACITY_EXPANSION {
	U32 Flags;		/*0x00 */
	U16 DevHandle0;		/*0x04 */
	U16 Reserved1;		/*0x06 */
	U16 DevHandle1;		/*0x08 */
	U16 Reserved2;		/*0x0A */
} MPI2_RAID_ONLINE_CAPACITY_EXPANSION,
	*PTR_MPI2_RAID_ONLINE_CAPACITY_EXPANSION,
	Mpi2RaidOnlineCapacityExpansion_t,
	*pMpi2RaidOnlineCapacityExpansion_t;

/*RAID Compatibility Input Structure */

typedef struct _MPI2_RAID_COMPATIBILITY_INPUT_STRUCT {
	U16 SourceDevHandle;	/*0x00 */
	U16 CandidateDevHandle;	/*0x02 */
	U32 Flags;		/*0x04 */
	U32 Reserved1;		/*0x08 */
	U32 Reserved2;		/*0x0C */
} MPI2_RAID_COMPATIBILITY_INPUT_STRUCT,
	*PTR_MPI2_RAID_COMPATIBILITY_INPUT_STRUCT,
	Mpi2RaidCompatibilityInputStruct_t,
	*pMpi2RaidCompatibilityInputStruct_t;

/*defines for RAID Compatibility Structure Flags field */
#define MPI2_RAID_COMPAT_SOURCE_IS_VOLUME_FLAG      (0x00000002)
#define MPI2_RAID_COMPAT_REPORT_SOURCE_INFO_FLAG    (0x00000001)

/*RAID Volume Indicator Structure */

typedef struct _MPI2_RAID_VOL_INDICATOR {
	U64 TotalBlocks;	/*0x00 */
	U64 BlocksRemaining;	/*0x08 */
	U32 Flags;		/*0x10 */
	U32 ElapsedSeconds;	/* 0x14 */
} MPI2_RAID_VOL_INDICATOR, *PTR_MPI2_RAID_VOL_INDICATOR,
	Mpi2RaidVolIndicator_t, *pMpi2RaidVolIndicator_t;

/*defines for RAID Volume Indicator Flags field */
#define MPI2_RAID_VOL_FLAGS_ELAPSED_SECONDS_VALID   (0x80000000)
#define MPI2_RAID_VOL_FLAGS_OP_MASK                 (0x0000000F)
#define MPI2_RAID_VOL_FLAGS_OP_BACKGROUND_INIT      (0x00000000)
#define MPI2_RAID_VOL_FLAGS_OP_ONLINE_CAP_EXPANSION (0x00000001)
#define MPI2_RAID_VOL_FLAGS_OP_CONSISTENCY_CHECK    (0x00000002)
#define MPI2_RAID_VOL_FLAGS_OP_RESYNC               (0x00000003)
#define MPI2_RAID_VOL_FLAGS_OP_MDC                  (0x00000004)

/*RAID Compatibility Result Structure */

typedef struct _MPI2_RAID_COMPATIBILITY_RESULT_STRUCT {
	U8 State;		/*0x00 */
	U8 Reserved1;		/*0x01 */
	U16 Reserved2;		/*0x02 */
	U32 GenericAttributes;	/*0x04 */
	U32 OEMSpecificAttributes;	/*0x08 */
	U32 Reserved3;		/*0x0C */
	U32 Reserved4;		/*0x10 */
} MPI2_RAID_COMPATIBILITY_RESULT_STRUCT,
	*PTR_MPI2_RAID_COMPATIBILITY_RESULT_STRUCT,
	Mpi2RaidCompatibilityResultStruct_t,
	*pMpi2RaidCompatibilityResultStruct_t;

/*defines for RAID Compatibility Result Structure State field */
#define MPI2_RAID_COMPAT_STATE_COMPATIBLE           (0x00)
#define MPI2_RAID_COMPAT_STATE_NOT_COMPATIBLE       (0x01)

/*defines for RAID Compatibility Result Structure GenericAttributes field */
#define MPI2_RAID_COMPAT_GENATTRIB_4K_SECTOR            (0x00000010)

#define MPI2_RAID_COMPAT_GENATTRIB_MEDIA_MASK           (0x0000000C)
#define MPI2_RAID_COMPAT_GENATTRIB_SOLID_STATE_DRIVE    (0x00000008)
#define MPI2_RAID_COMPAT_GENATTRIB_HARD_DISK_DRIVE      (0x00000004)

#define MPI2_RAID_COMPAT_GENATTRIB_PROTOCOL_MASK        (0x00000003)
#define MPI2_RAID_COMPAT_GENATTRIB_SAS_PROTOCOL         (0x00000002)
#define MPI2_RAID_COMPAT_GENATTRIB_SATA_PROTOCOL        (0x00000001)

/*RAID Action Reply ActionData union */
typedef union _MPI2_RAID_ACTION_REPLY_DATA {
	U32 Word[6];
	MPI2_RAID_VOL_INDICATOR RaidVolumeIndicator;
	U16 VolDevHandle;
	U8 VolumeState;
	U8 PhysDiskNum;
	MPI2_RAID_COMPATIBILITY_RESULT_STRUCT RaidCompatibilityResult;
} MPI2_RAID_ACTION_REPLY_DATA, *PTR_MPI2_RAID_ACTION_REPLY_DATA,
	Mpi2RaidActionReplyData_t, *pMpi2RaidActionReplyData_t;

/*use MPI2_RAIDVOL0_SETTING_ defines from mpi2_cnfg.h for
 *MPI2_RAID_ACTION_CHANGE_VOL_WRITE_CACHE action */

/*RAID Action Reply Message */
typedef struct _MPI2_RAID_ACTION_REPLY {
	U8 Action;		/*0x00 */
	U8 Reserved1;		/*0x01 */
	U8 MsgLength;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 VolDevHandle;	/*0x04 */
	U8 PhysDiskNum;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved2;		/*0x0A */
	U16 Reserved3;		/*0x0C */
	U16 IOCStatus;		/*0x0E */
	U32 IOCLogInfo;		/*0x10 */
	MPI2_RAID_ACTION_REPLY_DATA ActionData;	/*0x14 */
} MPI2_RAID_ACTION_REPLY, *PTR_MPI2_RAID_ACTION_REPLY,
	Mpi2RaidActionReply_t, *pMpi2RaidActionReply_t;

#endif
