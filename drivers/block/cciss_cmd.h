#ifndef CCISS_CMD_H
#define CCISS_CMD_H
//###########################################################################
//DEFINES
//###########################################################################
#define CISS_VERSION "1.00"

//general boundary defintions
#define SENSEINFOBYTES          32//note that this value may vary between host implementations
#define MAXSGENTRIES            31
#define MAXREPLYQS              256

//Command Status value
#define CMD_SUCCESS             0x0000
#define CMD_TARGET_STATUS       0x0001
#define CMD_DATA_UNDERRUN       0x0002
#define CMD_DATA_OVERRUN        0x0003
#define CMD_INVALID             0x0004
#define CMD_PROTOCOL_ERR        0x0005
#define CMD_HARDWARE_ERR        0x0006
#define CMD_CONNECTION_LOST     0x0007
#define CMD_ABORTED             0x0008
#define CMD_ABORT_FAILED        0x0009
#define CMD_UNSOLICITED_ABORT   0x000A
#define CMD_TIMEOUT             0x000B
#define CMD_UNABORTABLE		0x000C

//transfer direction
#define XFER_NONE               0x00
#define XFER_WRITE              0x01
#define XFER_READ               0x02
#define XFER_RSVD               0x03

//task attribute
#define ATTR_UNTAGGED           0x00
#define ATTR_SIMPLE             0x04
#define ATTR_HEADOFQUEUE        0x05
#define ATTR_ORDERED            0x06
#define ATTR_ACA                0x07

//cdb type
#define TYPE_CMD				0x00
#define TYPE_MSG				0x01

//config space register offsets
#define CFG_VENDORID            0x00
#define CFG_DEVICEID            0x02
#define CFG_I2OBAR              0x10
#define CFG_MEM1BAR             0x14

//i2o space register offsets
#define I2O_IBDB_SET            0x20
#define I2O_IBDB_CLEAR          0x70
#define I2O_INT_STATUS          0x30
#define I2O_INT_MASK            0x34
#define I2O_IBPOST_Q            0x40
#define I2O_OBPOST_Q            0x44
#define I2O_DMA1_CFG		0x214

//Configuration Table
#define CFGTBL_ChangeReq        0x00000001l
#define CFGTBL_AccCmds          0x00000001l

#define CFGTBL_Trans_Simple     0x00000002l

#define CFGTBL_BusType_Ultra2   0x00000001l
#define CFGTBL_BusType_Ultra3   0x00000002l
#define CFGTBL_BusType_Fibre1G  0x00000100l
#define CFGTBL_BusType_Fibre2G  0x00000200l
typedef struct _vals32
{
        __u32   lower;
        __u32   upper;
} vals32;

typedef union _u64bit
{
   vals32	val32;
   __u64	val;
} u64bit;

// Type defs used in the following structs
#define BYTE __u8
#define WORD __u16
#define HWORD __u16
#define DWORD __u32
#define QWORD vals32 

//###########################################################################
//STRUCTURES
//###########################################################################
#define CISS_MAX_LUN	1024
#define CISS_MAX_PHYS_LUN	1024
// SCSI-3 Cmmands 

#pragma pack(1)	

#define CISS_INQUIRY 0x12
//Date returned
typedef struct _InquiryData_struct
{
  BYTE data_byte[36];
} InquiryData_struct;

#define CISS_REPORT_LOG 0xc2    /* Report Logical LUNs */
#define CISS_REPORT_PHYS 0xc3   /* Report Physical LUNs */
// Data returned
typedef struct _ReportLUNdata_struct
{
  BYTE LUNListLength[4];
  DWORD reserved;
  BYTE LUN[CISS_MAX_LUN][8];
} ReportLunData_struct;

#define CCISS_READ_CAPACITY 0x25 /* Read Capacity */ 
typedef struct _ReadCapdata_struct
{
  BYTE total_size[4];	// Total size in blocks
  BYTE block_size[4];	// Size of blocks in bytes
} ReadCapdata_struct;

#define CCISS_READ_CAPACITY_16 0x9e /* Read Capacity 16 */

/* service action to differentiate a 16 byte read capacity from
   other commands that use the 0x9e SCSI op code */

#define CCISS_READ_CAPACITY_16_SERVICE_ACT 0x10

typedef struct _ReadCapdata_struct_16
{
	BYTE total_size[8];   /* Total size in blocks */
	BYTE block_size[4];   /* Size of blocks in bytes */
	BYTE prot_en:1;       /* protection enable bit */
	BYTE rto_en:1;        /* reference tag own enable bit */
	BYTE reserved:6;      /* reserved bits */
	BYTE reserved2[18];   /* reserved bytes per spec */
} ReadCapdata_struct_16;

/* Define the supported read/write commands for cciss based controllers */

#define CCISS_READ_10   0x28    /* Read(10)  */
#define CCISS_WRITE_10  0x2a    /* Write(10) */
#define CCISS_READ_16   0x88    /* Read(16)  */
#define CCISS_WRITE_16  0x8a    /* Write(16) */

/* Define the CDB lengths supported by cciss based controllers */

#define CDB_LEN10	10
#define CDB_LEN16	16

// BMIC commands 
#define BMIC_READ 0x26
#define BMIC_WRITE 0x27
#define BMIC_CACHE_FLUSH 0xc2
#define CCISS_CACHE_FLUSH 0x01	//C2 was already being used by CCISS

//Command List Structure
typedef union _SCSI3Addr_struct {
   struct {
    BYTE Dev;
    BYTE Bus:6;
    BYTE Mode:2;        // b00
  } PeripDev;
   struct {
    BYTE DevLSB;
    BYTE DevMSB:6;
    BYTE Mode:2;        // b01
  } LogDev;
   struct {
    BYTE Dev:5;
    BYTE Bus:3;
    BYTE Targ:6;
    BYTE Mode:2;        // b10
  } LogUnit;
} SCSI3Addr_struct;

typedef struct _PhysDevAddr_struct {
  DWORD             TargetId:24;
  DWORD             Bus:6;
  DWORD             Mode:2;
  SCSI3Addr_struct  Target[2]; //2 level target device addr
} PhysDevAddr_struct;
  
typedef struct _LogDevAddr_struct {
  DWORD            VolId:30;
  DWORD            Mode:2;
  BYTE             reserved[4];
} LogDevAddr_struct;

typedef union _LUNAddr_struct {
  BYTE               LunAddrBytes[8];
  SCSI3Addr_struct   SCSI3Lun[4];
  PhysDevAddr_struct PhysDev;
  LogDevAddr_struct  LogDev;
} LUNAddr_struct;

typedef struct _CommandListHeader_struct {
  BYTE              ReplyQueue;
  BYTE              SGList;
  HWORD             SGTotal;
  QWORD             Tag;
  LUNAddr_struct    LUN;
} CommandListHeader_struct;
typedef struct _RequestBlock_struct {
  BYTE   CDBLen;
  struct {
    BYTE Type:3;
    BYTE Attribute:3;
    BYTE Direction:2;
  } Type;
  HWORD  Timeout;
  BYTE   CDB[16];
} RequestBlock_struct;
typedef struct _ErrDescriptor_struct {
  QWORD  Addr;
  DWORD  Len;
} ErrDescriptor_struct;
typedef struct _SGDescriptor_struct {
  QWORD  Addr;
  DWORD  Len;
  DWORD  Ext;
} SGDescriptor_struct;

typedef union _MoreErrInfo_struct{
  struct {
    BYTE  Reserved[3];
    BYTE  Type;
    DWORD ErrorInfo;
  }Common_Info;
  struct{
    BYTE  Reserved[2];
    BYTE  offense_size;//size of offending entry
    BYTE  offense_num; //byte # of offense 0-base
    DWORD offense_value;
  }Invalid_Cmd;
}MoreErrInfo_struct;
typedef struct _ErrorInfo_struct {
  BYTE               ScsiStatus;
  BYTE               SenseLen;
  HWORD              CommandStatus;
  DWORD              ResidualCnt;
  MoreErrInfo_struct MoreErrInfo;
  BYTE               SenseInfo[SENSEINFOBYTES];
} ErrorInfo_struct;

/* Command types */
#define CMD_RWREQ       0x00
#define CMD_IOCTL_PEND  0x01
#define CMD_SCSI	0x03
#define CMD_MSG_DONE	0x04
#define CMD_MSG_TIMEOUT 0x05

/* This structure needs to be divisible by 8 for new
 * indexing method.
 */
#define PADSIZE (sizeof(long) - 4)
typedef struct _CommandList_struct {
  CommandListHeader_struct Header;
  RequestBlock_struct      Request;
  ErrDescriptor_struct     ErrDesc;
  SGDescriptor_struct      SG[MAXSGENTRIES];
	/* information associated with the command */ 
  __u32			   busaddr; /* physical address of this record */
  ErrorInfo_struct * 	   err_info; /* pointer to the allocated mem */ 
  int			   ctlr;
  int			   cmd_type; 
  long			   cmdindex;
  struct hlist_node list;
  struct request *	   rq;
  struct completion *waiting;
  int	 retry_count;
  void * scsi_cmd;
  char   pad[PADSIZE];
} CommandList_struct;

//Configuration Table Structure
typedef struct _HostWrite_struct {
  DWORD TransportRequest;
  DWORD Reserved;
  DWORD CoalIntDelay;
  DWORD CoalIntCount;
} HostWrite_struct;

typedef struct _CfgTable_struct {
  BYTE             Signature[4];
  DWORD            SpecValence;
  DWORD            TransportSupport;
  DWORD            TransportActive;
  HostWrite_struct HostWrite;
  DWORD            CmdsOutMax;
  DWORD            BusTypes;
  DWORD            Reserved; 
  BYTE             ServerName[16];
  DWORD            HeartBeat;
  DWORD            SCSI_Prefetch;
} CfgTable_struct;
#pragma pack()	 
#endif // CCISS_CMD_H
