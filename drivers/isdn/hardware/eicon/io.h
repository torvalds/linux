
/*
 *
 Copyright (c) Eicon Networks, 2002.
 *
 This source file is supplied for the use with
 Eicon Networks range of DIVA Server Adapters.
 *
 Eicon File Revision :    2.1
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
 *
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.
 *
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __DIVA_XDI_COMMON_IO_H_INC__ /* { */
#define __DIVA_XDI_COMMON_IO_H_INC__
/*
  maximum = 16 adapters
*/
#define DI_MAX_LINKS    MAX_ADAPTER
#define ISDN_MAX_NUM_LEN 60
/* --------------------------------------------------------------------------
   structure for quadro card management (obsolete for
   systems that do provide per card load event)
   -------------------------------------------------------------------------- */
typedef struct {
	dword         Num;
	DEVICE_NAME   DeviceName[4];
	PISDN_ADAPTER QuadroAdapter[4];
} ADAPTER_LIST_ENTRY, *PADAPTER_LIST_ENTRY;
/* --------------------------------------------------------------------------
   Special OS memory support structures
   -------------------------------------------------------------------------- */
#define MAX_MAPPED_ENTRIES 8
typedef struct {
	void *Address;
	dword    Length;
} ADAPTER_MEMORY;
/* --------------------------------------------------------------------------
   Configuration of XDI clients carried by XDI
   -------------------------------------------------------------------------- */
#define DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON      0x01
#define DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON 0x02
typedef struct _diva_xdi_capi_cfg {
	byte cfg_1;
} diva_xdi_capi_cfg_t;
/* --------------------------------------------------------------------------
   Main data structure kept per adapter
   -------------------------------------------------------------------------- */
struct _ISDN_ADAPTER {
	void (*DIRequest)(PISDN_ADAPTER, ENTITY *);
	int State; /* from NT4 1.srv, a good idea, but  a poor achievement */
	int Initialized;
	int RegisteredWithDidd;
	int Unavailable;  /* callback function possible? */
	int ResourcesClaimed;
	int PnpBiosConfigUsed;
	dword Logging;
	dword features;
	char ProtocolIdString[80];
	/*
	  remember mapped memory areas
	*/
	ADAPTER_MEMORY MappedMemory[MAX_MAPPED_ENTRIES];
	CARD_PROPERTIES Properties;
	dword cardType;
	dword protocol_id;       /* configured protocol identifier */
	char protocol_name[8];  /* readable name of protocol */
	dword BusType;
	dword BusNumber;
	dword slotNumber;
	dword slotId;
	dword ControllerNumber;  /* for QUADRO cards only */
	PISDN_ADAPTER MultiMaster;       /* for 4-BRI card only - use MultiMaster or QuadroList */
	PADAPTER_LIST_ENTRY QuadroList;        /* for QUADRO card  only */
	PDEVICE_OBJECT DeviceObject;
	dword DeviceId;
	diva_os_adapter_irq_info_t irq_info;
	dword volatile IrqCount;
	int trapped;
	dword DspCodeBaseAddr;
	dword MaxDspCodeSize;
	dword downloadAddr;
	dword DspCodeBaseAddrTable[4]; /* add. for MultiMaster */
	dword MaxDspCodeSizeTable[4]; /* add. for MultiMaster */
	dword downloadAddrTable[4]; /* add. for MultiMaster */
	dword MemoryBase;
	dword MemorySize;
	byte __iomem *Address;
	byte __iomem *Config;
	byte __iomem *Control;
	byte __iomem *reset;
	byte __iomem *port;
	byte __iomem *ram;
	byte __iomem *cfg;
	byte __iomem *prom;
	byte __iomem *ctlReg;
	struct pc_maint  *pcm;
	diva_os_dependent_devica_name_t os_name;
	byte Name[32];
	dword serialNo;
	dword ANum;
	dword ArchiveType; /* ARCHIVE_TYPE_NONE ..._SINGLE ..._USGEN ..._MULTI */
	char *ProtocolSuffix; /* internal protocolfile table */
	char Archive[32];
	char Protocol[32];
	char AddDownload[32]; /* Dsp- or other additional download files */
	char Oad1[ISDN_MAX_NUM_LEN];
	char Osa1[ISDN_MAX_NUM_LEN];
	char Oad2[ISDN_MAX_NUM_LEN];
	char Osa2[ISDN_MAX_NUM_LEN];
	char Spid1[ISDN_MAX_NUM_LEN];
	char Spid2[ISDN_MAX_NUM_LEN];
	byte nosig;
	byte BriLayer2LinkCount; /* amount of TEI's that adapter will support in P2MP mode */
	dword Channels;
	dword tei;
	dword nt2;
	dword TerminalCount;
	dword WatchDog;
	dword Permanent;
	dword BChMask; /* B channel mask for unchannelized modes */
	dword StableL2;
	dword DidLen;
	dword NoOrderCheck;
	dword ForceLaw; /* VoiceCoding - default:0, a-law: 1, my-law: 2 */
	dword SigFlags;
	dword LowChannel;
	dword NoHscx30;
	dword ProtVersion;
	dword crc4;
	dword L1TristateOrQsig; /* enable Layer 1 Tristate (bit 2)Or Qsig params (bit 0,1)*/
	dword InitialDspInfo;
	dword ModemGuardTone;
	dword ModemMinSpeed;
	dword ModemMaxSpeed;
	dword ModemOptions;
	dword ModemOptions2;
	dword ModemNegotiationMode;
	dword ModemModulationsMask;
	dword ModemTransmitLevel;
	dword FaxOptions;
	dword FaxMaxSpeed;
	dword Part68LevelLimiter;
	dword UsEktsNumCallApp;
	byte UsEktsFeatAddConf;
	byte UsEktsFeatRemoveConf;
	byte UsEktsFeatCallTransfer;
	byte UsEktsFeatMsgWaiting;
	byte QsigDialect;
	byte ForceVoiceMailAlert;
	byte DisableAutoSpid;
	byte ModemCarrierWaitTimeSec;
	byte ModemCarrierLossWaitTimeTenthSec;
	byte PiafsLinkTurnaroundInFrames;
	byte DiscAfterProgress;
	byte AniDniLimiter[3];
	byte TxAttenuation;  /* PRI/E1 only: attenuate TX signal */
	word QsigFeatures;
	dword GenerateRingtone;
	dword SupplementaryServicesFeatures;
	dword R2Dialect;
	dword R2CasOptions;
	dword FaxV34Options;
	dword DisabledDspMask;
	dword AdapterTestMask;
	dword DspImageLength;
	word AlertToIn20mSecTicks;
	word ModemEyeSetup;
	byte R2CtryLength;
	byte CCBSRelTimer;
	byte *PcCfgBufferFile;/* flexible parameter via file */
	byte *PcCfgBuffer; /* flexible parameter via multistring */
	diva_os_dump_file_t dump_file; /* dump memory to file at lowest irq level */
	diva_os_board_trace_t board_trace; /* traces from the board */
	diva_os_spin_lock_t isr_spin_lock;
	diva_os_spin_lock_t data_spin_lock;
	diva_os_soft_isr_t req_soft_isr;
	diva_os_soft_isr_t isr_soft_isr;
	diva_os_atomic_t  in_dpc;
	PBUFFER RBuffer;        /* Copy of receive lookahead buffer */
	word e_max;
	word e_count;
	E_INFO *e_tbl;
	word assign;         /* list of pending ASSIGNs  */
	word head;           /* head of request queue    */
	word tail;           /* tail of request queue    */
	ADAPTER a;             /* not a separate structure */
	void (*out)(ADAPTER *a);
	byte (*dpc)(ADAPTER *a);
	byte (*tst_irq)(ADAPTER *a);
	void (*clr_irq)(ADAPTER *a);
	int (*load)(PISDN_ADAPTER);
	int (*mapmem)(PISDN_ADAPTER);
	int (*chkIrq)(PISDN_ADAPTER);
	void (*disIrq)(PISDN_ADAPTER);
	void (*start)(PISDN_ADAPTER);
	void (*stop)(PISDN_ADAPTER);
	void (*rstFnc)(PISDN_ADAPTER);
	void (*trapFnc)(PISDN_ADAPTER);
	dword (*DetectDsps)(PISDN_ADAPTER);
	void (*os_trap_nfy_Fnc)(PISDN_ADAPTER, dword);
	diva_os_isr_callback_t diva_isr_handler;
	dword sdram_bar;  /* must be 32 bit */
	dword fpga_features;
	volatile int pcm_pending;
	volatile void *pcm_data;
	diva_xdi_capi_cfg_t capi_cfg;
	dword tasks;
	void *dma_map;
	int (*DivaAdapterTestProc)(PISDN_ADAPTER);
	void *AdapterTestMemoryStart;
	dword AdapterTestMemoryLength;
	const byte *cfg_lib_memory_init;
	dword cfg_lib_memory_init_length;
};
/* ---------------------------------------------------------------------
   Entity table
   --------------------------------------------------------------------- */
struct e_info_s {
	ENTITY *e;
	byte          next;                   /* chaining index           */
	word          assign_ref;             /* assign reference         */
};
/* ---------------------------------------------------------------------
   S-cards shared ram structure for loading
   --------------------------------------------------------------------- */
struct s_load {
	byte ctrl;
	byte card;
	byte msize;
	byte fill0;
	word ebit;
	word elocl;
	word eloch;
	byte reserved[20];
	word signature;
	byte fill[224];
	byte b[256];
};
#define PR_RAM  ((struct pr_ram *)0)
#define RAM ((struct dual *)0)
/* ---------------------------------------------------------------------
   platform specific conversions
   --------------------------------------------------------------------- */
extern void *PTR_P(ADAPTER *a, ENTITY *e, void *P);
extern void *PTR_X(ADAPTER *a, ENTITY *e);
extern void *PTR_R(ADAPTER *a, ENTITY *e);
extern void CALLBACK(ADAPTER *a, ENTITY *e);
extern void set_ram(void **adr_ptr);
/* ---------------------------------------------------------------------
   ram access functions for io mapped cards
   --------------------------------------------------------------------- */
byte io_in(ADAPTER *a, void *adr);
word io_inw(ADAPTER *a, void *adr);
void io_in_buffer(ADAPTER *a, void *adr, void *P, word length);
void io_look_ahead(ADAPTER *a, PBUFFER *RBuffer, ENTITY *e);
void io_out(ADAPTER *a, void *adr, byte data);
void io_outw(ADAPTER *a, void *adr, word data);
void io_out_buffer(ADAPTER *a, void *adr, void *P, word length);
void io_inc(ADAPTER *a, void *adr);
void bri_in_buffer(PISDN_ADAPTER IoAdapter, dword Pos,
		   void *Buf, dword Len);
int bri_out_buffer(PISDN_ADAPTER IoAdapter, dword Pos,
		   void *Buf, dword Len, int Verify);
/* ---------------------------------------------------------------------
   ram access functions for memory mapped cards
   --------------------------------------------------------------------- */
byte mem_in(ADAPTER *a, void *adr);
word mem_inw(ADAPTER *a, void *adr);
void mem_in_buffer(ADAPTER *a, void *adr, void *P, word length);
void mem_look_ahead(ADAPTER *a, PBUFFER *RBuffer, ENTITY *e);
void mem_out(ADAPTER *a, void *adr, byte data);
void mem_outw(ADAPTER *a, void *adr, word data);
void mem_out_buffer(ADAPTER *a, void *adr, void *P, word length);
void mem_inc(ADAPTER *a, void *adr);
void mem_in_dw(ADAPTER *a, void *addr, dword *data, int dwords);
void mem_out_dw(ADAPTER *a, void *addr, const dword *data, int dwords);
/* ---------------------------------------------------------------------
   functions exported by io.c
   --------------------------------------------------------------------- */
extern IDI_CALL Requests[MAX_ADAPTER];
extern void     DIDpcRoutine(struct _diva_os_soft_isr *psoft_isr,
			     void *context);
extern void     request(PISDN_ADAPTER, ENTITY *);
/* ---------------------------------------------------------------------
   trapFn helpers, used to recover debug trace from dead card
   --------------------------------------------------------------------- */
typedef struct {
	word *buf;
	word  cnt;
	word  out;
} Xdesc;
extern void dump_trap_frame(PISDN_ADAPTER IoAdapter, byte __iomem *exception);
extern void dump_xlog_buffer(PISDN_ADAPTER IoAdapter, Xdesc *xlogDesc);
/* --------------------------------------------------------------------- */
#endif  /* } __DIVA_XDI_COMMON_IO_H_INC__ */
