#ifndef MS_INCD
#define MS_INCD

#include <linux/blkdev.h>
#include "common.h"

/* MemoryStick Register */
/* Status Register 0 */
#define MS_REG_ST0_MB                           0x80    /* media busy */
#define MS_REG_ST0_FB0                          0x40    /* flush busy 0 */
#define MS_REG_ST0_BE                           0x20    /* buffer empty */
#define MS_REG_ST0_BF                           0x10    /* buffer full */
#define MS_REG_ST0_SL                           0x02    /* sleep */
#define MS_REG_ST0_WP                           0x01    /* write protected */
#define MS_REG_ST0_WP_ON                        MS_REG_ST0_WP
#define MS_REG_ST0_WP_OFF                       0x00

/* Status Register 1 */
#define MS_REG_ST1_MB		0x80    /* media busy */
#define MS_REG_ST1_FB1		0x40    /* flush busy 1 */
#define MS_REG_ST1_DTER		0x20    /* error on data(corrected) */
#define MS_REG_ST1_UCDT		0x10    /* unable to correct data */
#define MS_REG_ST1_EXER		0x08    /* error on extra(corrected) */
#define MS_REG_ST1_UCEX		0x04    /* unable to correct extra */
#define MS_REG_ST1_FGER		0x02    /* error on overwrite flag(corrected) */
#define MS_REG_ST1_UCFG		0x01    /* unable to correct overwrite flag */
#define MS_REG_ST1_DEFAULT	(MS_REG_ST1_MB   | MS_REG_ST1_FB1  | \
				MS_REG_ST1_DTER | MS_REG_ST1_UCDT | \
				MS_REG_ST1_EXER | MS_REG_ST1_UCEX | \
				MS_REG_ST1_FGER | MS_REG_ST1_UCFG)

/* System Parameter */
#define MS_REG_SYSPAR_BAMD		0x80	/* block address mode */
#define MS_REG_SYSPAR_BAND_LINEAR	MS_REG_SYSPAR_BAMD  /*   linear mode */
#define MS_REG_SYSPAR_BAND_CHIP		0x00	/*  chip mode */
#define MS_REG_SYSPAR_ATEN		0x40	/* attribute ROM enable */
#define MS_REG_SYSPAR_ATEN_ENABLE	MS_REG_SYSPAR_ATEN	/*  enable */
#define MS_REG_SYSPAR_ATEN_DISABLE	0x00	/*  disable */
#define MS_REG_SYSPAR_RESERVED                  0x2f

/* Command Parameter */
#define MS_REG_CMDPAR_CP2                       0x80
#define MS_REG_CMDPAR_CP1                       0x40
#define MS_REG_CMDPAR_CP0                       0x20
#define MS_REG_CMDPAR_BLOCK_ACCESS              0
#define MS_REG_CMDPAR_PAGE_ACCESS               MS_REG_CMDPAR_CP0
#define MS_REG_CMDPAR_EXTRA_DATA                MS_REG_CMDPAR_CP1
#define MS_REG_CMDPAR_OVERWRITE                 MS_REG_CMDPAR_CP2
#define MS_REG_CMDPAR_RESERVED                  0x1f

/* Overwrite Area */
#define MS_REG_OVR_BKST		0x80            /* block status */
#define MS_REG_OVR_BKST_OK                      MS_REG_OVR_BKST     /* OK */
#define MS_REG_OVR_BKST_NG                      0x00            /* NG */
#define MS_REG_OVR_PGST0	0x40            /* page status */
#define MS_REG_OVR_PGST1                        0x20
#define MS_REG_OVR_PGST_MASK	(MS_REG_OVR_PGST0 | MS_REG_OVR_PGST1)
#define MS_REG_OVR_PGST_OK	(MS_REG_OVR_PGST0 | MS_REG_OVR_PGST1) /* OK */
#define MS_REG_OVR_PGST_NG	MS_REG_OVR_PGST1                      /* NG */
#define MS_REG_OVR_PGST_DATA_ERROR              0x00        /* data error */
#define MS_REG_OVR_UDST                         0x10        /* update status */
#define MS_REG_OVR_UDST_UPDATING                0x00        /* updating */
#define MS_REG_OVR_UDST_NO_UPDATE               MS_REG_OVR_UDST
#define MS_REG_OVR_RESERVED                     0x08
#define MS_REG_OVR_DEFAULT                      (MS_REG_OVR_BKST_OK |      \
						MS_REG_OVR_PGST_OK |      \
						MS_REG_OVR_UDST_NO_UPDATE |   \
						MS_REG_OVR_RESERVED)
/* Management Flag */
#define MS_REG_MNG_SCMS0	0x20    /* serial copy management system */
#define MS_REG_MNG_SCMS1                        0x10
#define MS_REG_MNG_SCMS_MASK		(MS_REG_MNG_SCMS0 | MS_REG_MNG_SCMS1)
#define MS_REG_MNG_SCMS_COPY_OK		(MS_REG_MNG_SCMS0 | MS_REG_MNG_SCMS1)
#define MS_REG_MNG_SCMS_ONE_COPY                MS_REG_MNG_SCMS1
#define MS_REG_MNG_SCMS_NO_COPY                 0x00
#define MS_REG_MNG_ATFLG	0x08	/* address transfer table flag */
#define MS_REG_MNG_ATFLG_OTHER                  MS_REG_MNG_ATFLG    /* other */
#define MS_REG_MNG_ATFLG_ATTBL		0x00	/* address transfer table */
#define MS_REG_MNG_SYSFLG                       0x04    /* system flag */
#define MS_REG_MNG_SYSFLG_USER		MS_REG_MNG_SYSFLG   /* user block */
#define MS_REG_MNG_SYSFLG_BOOT                  0x00    /* system block */
#define MS_REG_MNG_RESERVED                     0xc3
#define MS_REG_MNG_DEFAULT		(MS_REG_MNG_SCMS_COPY_OK |	\
					 MS_REG_MNG_ATFLG_OTHER |	\
					 MS_REG_MNG_SYSFLG_USER |	\
					 MS_REG_MNG_RESERVED)

/* Error codes */
#define MS_STATUS_SUCCESS                       0x0000
#define MS_ERROR_OUT_OF_SPACE                   0x0103
#define MS_STATUS_WRITE_PROTECT                 0x0106
#define MS_ERROR_READ_DATA                      0x8002
#define MS_ERROR_FLASH_READ                     0x8003
#define MS_ERROR_FLASH_WRITE                    0x8004
#define MS_ERROR_FLASH_ERASE                    0x8005
#define MS_ERROR_FLASH_COPY                     0x8006

#define MS_STATUS_ERROR                         0xfffe
#define MS_FIFO_ERROR                           0xfffd
#define MS_UNDEFINED_ERROR                      0xfffc
#define MS_KETIMEOUT_ERROR                      0xfffb
#define MS_STATUS_INT_ERROR                     0xfffa
#define MS_NO_MEMORY_ERROR                      0xfff9
#define MS_NOCARD_ERROR                         0xfff8
#define MS_LB_NOT_USED                          0xffff
#define MS_LB_ERROR                             0xfff0
#define MS_LB_BOOT_BLOCK                        0xfff1
#define MS_LB_INITIAL_ERROR                     0xfff2
#define MS_STATUS_SUCCESS_WITH_ECC              0xfff3
#define MS_LB_ACQUIRED_ERROR                    0xfff4
#define MS_LB_NOT_USED_ERASED                   0xfff5

#define MS_LibConv2Physical(pdx, LogBlock) \
	(((LogBlock) >= (pdx)->MS_Lib.NumberOfLogBlock) ? \
	MS_STATUS_ERROR : (pdx)->MS_Lib.Log2PhyMap[LogBlock])
#define MS_LibConv2Logical(pdx, PhyBlock) \
	(((PhyBlock) >= (pdx)->MS_Lib.NumberOfPhyBlock) ? \
	MS_STATUS_ERROR : (pdx)->MS_Lib.Phy2LogMap[PhyBlock])
	/*dphy->log table */

#define MS_LIB_CTRL_RDONLY                      0
#define MS_LIB_CTRL_WRPROTECT                   1
#define MS_LibCtrlCheck(pdx, Flag)	((pdx)->MS_Lib.flags & (1 << (Flag)))

#define MS_LibCtrlSet(pdx, Flag)	((pdx)->MS_Lib.flags |= (1 << (Flag)))
#define MS_LibCtrlReset(pdx, Flag)	((pdx)->MS_Lib.flags &= ~(1 << (Flag)))
#define MS_LibIsWritable(pdx) \
	((MS_LibCtrlCheck((pdx), MS_LIB_CTRL_RDONLY) == 0) && \
	(MS_LibCtrlCheck(pdx, MS_LIB_CTRL_WRPROTECT) == 0))

#define MS_MAX_PAGES_PER_BLOCK                  32
#define MS_LIB_BITS_PER_BYTE                    8

#define MS_LibPageMapIdx(n) ((n) / MS_LIB_BITS_PER_BYTE)
#define MS_LibPageMapBit(n) (1 << ((n) % MS_LIB_BITS_PER_BYTE))
#define MS_LibCheckPageMapBit(pdx, n) \
	((pdx)->MS_Lib.pagemap[MS_LibPageMapIdx(n)] & MS_LibPageMapBit(n))
#define MS_LibSetPageMapBit(pdx, n) \
	((pdx)->MS_Lib.pagemap[MS_LibPageMapIdx(n)] |= MS_LibPageMapBit(n))
#define MS_LibResetPageMapBit(pdx, n) \
	((pdx)->MS_Lib.pagemap[MS_LibPageMapIdx(n)] &= ~MS_LibPageMapBit(n))
#define MS_LibClearPageMap(pdx) \
	memset((pdx)->MS_Lib.pagemap, 0, sizeof((pdx)->MS_Lib.pagemap))


#define MemStickLogAddr(logadr1, logadr0) \
	((((WORD)(logadr1)) << 8) | (logadr0))

#define MS_BYTES_PER_PAGE                       512

#define MS_MAX_INITIAL_ERROR_BLOCKS             10
#define MS_NUMBER_OF_PAGES_FOR_BOOT_BLOCK       3
#define MS_NUMBER_OF_PAGES_FOR_LPCTBL           2

#define MS_NUMBER_OF_BOOT_BLOCK                 2
#define MS_NUMBER_OF_SYSTEM_BLOCK               4
#define MS_LOGICAL_BLOCKS_PER_SEGMENT           496
#define MS_LOGICAL_BLOCKS_IN_1ST_SEGMENT        494
#define MS_PHYSICAL_BLOCKS_PER_SEGMENT          0x200   /* 512 */
#define MS_PHYSICAL_BLOCKS_PER_SEGMENT_MASK     0x1ff

#define MS_SECTOR_SIZE                          512
#define MBR_SIGNATURE                           0xAA55
#define PBR_SIGNATURE                           0xAA55

#define PARTITION_FAT_12                        1
#define PARTITION_FAT_16                        2

#define MS_BOOT_BLOCK_ID                        0x0001
#define MS_BOOT_BLOCK_FORMAT_VERSION            0x0100
#define MS_BOOT_BLOCK_DATA_ENTRIES              2

#define MS_SYSINF_MSCLASS_TYPE_1                1
#define MS_SYSINF_CARDTYPE_RDONLY               1
#define MS_SYSINF_CARDTYPE_RDWR                 2
#define MS_SYSINF_CARDTYPE_HYBRID               3
#define MS_SYSINF_SECURITY                      0x01
#define MS_SYSINF_SECURITY_NO_SUPPORT           MS_SYSINF_SECURITY
#define MS_SYSINF_SECURITY_SUPPORT              0
#define MS_SYSINF_FORMAT_MAT                    0   /* ? */
#define MS_SYSINF_FORMAT_FAT                    1
#define MS_SYSINF_USAGE_GENERAL                 0
#define MS_SYSINF_PAGE_SIZE                     MS_BYTES_PER_PAGE /* fixed */
#define MS_SYSINF_RESERVED1                     1
#define MS_SYSINF_RESERVED2                     1

#define MS_SYSENT_TYPE_INVALID_BLOCK            0x01
#define MS_SYSENT_TYPE_CIS_IDI                  0x0a    /* CIS/IDI */

#define SIZE_OF_KIRO                            1024

/* BOOT BLOCK */
#define MS_NUMBER_OF_SYSTEM_ENTRY               4

/*
 * MemStickRegisters
 */
/* Status registers (16 bytes) */
typedef struct {
	BYTE Reserved0;		/* 00 */
	BYTE INTRegister;	/* 01 */
	BYTE StatusRegister0;	/* 02 */
	BYTE StatusRegister1;	/* 03 */
	BYTE Reserved1[12];	/* 04-0F */
} MemStickStatusRegisters;

/* Parameter registers (6 bytes) */
typedef struct {
	BYTE SystemParameter;	/* 10 */
	BYTE BlockAddress2;	/* 11 */
	BYTE BlockAddress1;	/* 12 */
	BYTE BlockAddress0;	/* 13 */
	BYTE CMDParameter;	/* 14 */
	BYTE PageAddress;	/* 15 */
} MemStickParameterRegisters;

/* Extra registers (9 bytes) */
typedef struct {
	BYTE OverwriteFlag;	/* 16 */
	BYTE ManagementFlag;	/* 17 */
	BYTE LogicalAddress1;	/* 18 */
	BYTE LogicalAddress0;	/* 19 */
	BYTE ReservedArea[5];	/* 1A-1E */
} MemStickExtraDataRegisters;

/* All registers in Memory Stick (32 bytes, includes 1 byte padding) */
typedef struct {
	MemStickStatusRegisters status;
	MemStickParameterRegisters param;
	MemStickExtraDataRegisters extra;
	BYTE padding;
} MemStickRegisters, *PMemStickRegisters;

/*
 * MemStickBootBlockPage0
 */
typedef struct {
	WORD wBlockID;
	WORD wFormatVersion;
	BYTE bReserved1[184];
	BYTE bNumberOfDataEntry;
	BYTE bReserved2[179];
} MemStickBootBlockHeader;

typedef struct {
	DWORD dwStart;
	DWORD dwSize;
	BYTE bType;
	BYTE bReserved[3];
} MemStickBootBlockSysEntRec;

typedef struct {
	MemStickBootBlockSysEntRec entry[MS_NUMBER_OF_SYSTEM_ENTRY];
} MemStickBootBlockSysEnt;

typedef struct {
	BYTE bMsClass;		/* must be 1 */
	BYTE bCardType;		/* see below */
	WORD wBlockSize;	/* n KB */
	WORD wBlockNumber;	/* number of physical block */
	WORD wTotalBlockNumber;	/* number of logical block */
	WORD wPageSize;		/* must be 0x200 */
	BYTE bExtraSize;	/* 0x10 */
	BYTE bSecuritySupport;
	BYTE bAssemblyDate[8];
	BYTE bFactoryArea[4];
	BYTE bAssemblyMakerCode;
	BYTE bAssemblyMachineCode[3];
	WORD wMemoryMakerCode;
	WORD wMemoryDeviceCode;
	WORD wMemorySize;
	BYTE bReserved1;
	BYTE bReserved2;
	BYTE bVCC;
	BYTE bVPP;
	WORD wControllerChipNumber;
	WORD wControllerFunction;	/* New MS */
	BYTE bReserved3[9];		/* New MS */
	BYTE bParallelSupport;		/* New MS */
	WORD wFormatValue;		/* New MS */
	BYTE bFormatType;
	BYTE bUsage;
	BYTE bDeviceType;
	BYTE bReserved4[22];
	BYTE bFUValue3;
	BYTE bFUValue4;
	BYTE bReserved5[15];
} MemStickBootBlockSysInf;

typedef struct {
	MemStickBootBlockHeader header;
	MemStickBootBlockSysEnt sysent;
	MemStickBootBlockSysInf sysinf;
} MemStickBootBlockPage0;

/*
 * MemStickBootBlockCIS_IDI
 */
typedef struct {
	BYTE bCistplDEVICE[6];            /* 0 */
	BYTE bCistplDEVICE0C[6];          /* 6 */
	BYTE bCistplJEDECC[4];            /* 12 */
	BYTE bCistplMANFID[6];            /* 16 */
	BYTE bCistplVER1[32];             /* 22 */
	BYTE bCistplFUNCID[4];            /* 54 */
	BYTE bCistplFUNCE0[4];            /* 58 */
	BYTE bCistplFUNCE1[5];            /* 62 */
	BYTE bCistplCONF[7];              /* 67 */
	BYTE bCistplCFTBLENT0[10];        /* 74 */
	BYTE bCistplCFTBLENT1[8];         /* 84 */
	BYTE bCistplCFTBLENT2[12];        /* 92 */
	BYTE bCistplCFTBLENT3[8];         /* 104 */
	BYTE bCistplCFTBLENT4[17];        /* 112 */
	BYTE bCistplCFTBLENT5[8];         /* 129 */
	BYTE bCistplCFTBLENT6[17];        /* 137 */
	BYTE bCistplCFTBLENT7[8];         /* 154 */
	BYTE bCistplNOLINK[3];            /* 162 */
} MemStickBootBlockCIS;

typedef struct {
#define MS_IDI_GENERAL_CONF         0x848A
	WORD wIDIgeneralConfiguration;     /* 0 */
	WORD wIDInumberOfCylinder;         /* 1 */
	WORD wIDIreserved0;                /* 2 */
	WORD wIDInumberOfHead;             /* 3 */
	WORD wIDIbytesPerTrack;            /* 4 */
	WORD wIDIbytesPerSector;           /* 5 */
	WORD wIDIsectorsPerTrack;          /* 6 */
	WORD wIDItotalSectors[2];          /* 7-8  high,low */
	WORD wIDIreserved1[11];            /* 9-19 */
	WORD wIDIbufferType;               /* 20 */
	WORD wIDIbufferSize;               /* 21 */
	WORD wIDIlongCmdECC;               /* 22 */
	WORD wIDIfirmVersion[4];           /* 23-26 */
	WORD wIDImodelName[20];            /* 27-46 */
	WORD wIDIreserved2;                /* 47 */
	WORD wIDIlongWordSupported;        /* 48 */
	WORD wIDIdmaSupported;             /* 49 */
	WORD wIDIreserved3;                /* 50 */
	WORD wIDIpioTiming;                /* 51 */
	WORD wIDIdmaTiming;                /* 52 */
	WORD wIDItransferParameter;        /* 53 */
	WORD wIDIformattedCylinder;        /* 54 */
	WORD wIDIformattedHead;            /* 55 */
	WORD wIDIformattedSectorsPerTrack; /* 56 */
	WORD wIDIformattedTotalSectors[2]; /* 57-58 */
	WORD wIDImultiSector;              /* 59 */
	WORD wIDIlbaSectors[2];            /* 60-61 */
	WORD wIDIsingleWordDMA;            /* 62 */
	WORD wIDImultiWordDMA;             /* 63 */
	WORD wIDIreserved4[192];           /* 64-255 */
} MemStickBootBlockIDI;

typedef struct {
	union {
		MemStickBootBlockCIS cis;
		BYTE dmy[256];
	} cis;

	union {
	MemStickBootBlockIDI idi;
	BYTE dmy[256];
	} idi;

} MemStickBootBlockCIS_IDI;

/*
 * MS_LibControl
 */
typedef struct {
	BYTE reserved;
	BYTE intr;
	BYTE status0;
	BYTE status1;
	BYTE ovrflg;
	BYTE mngflg;
	WORD logadr;
} MS_LibTypeExtdat;

typedef struct {
	DWORD flags;
	DWORD BytesPerSector;
	DWORD NumberOfCylinder;
	DWORD SectorsPerCylinder;
	WORD cardType;			/* R/W, RO, Hybrid */
	WORD blockSize;
	WORD PagesPerBlock;
	WORD NumberOfPhyBlock;
	WORD NumberOfLogBlock;
	WORD NumberOfSegment;
	WORD *Phy2LogMap;		/* phy2log table */
	WORD *Log2PhyMap;		/* log2phy table */
	WORD wrtblk;
	BYTE pagemap[(MS_MAX_PAGES_PER_BLOCK + (MS_LIB_BITS_PER_BYTE-1)) /
		     MS_LIB_BITS_PER_BYTE];
	BYTE *blkpag;
	MS_LibTypeExtdat *blkext;
	BYTE copybuf[512];
} MS_LibControl;

#endif
