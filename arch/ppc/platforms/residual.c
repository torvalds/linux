/*
 * Code to deal with the PReP residual data.
 *
 * Written by: Cort Dougan (cort@cs.nmt.edu)
 * Improved _greatly_ and rewritten by Gabriel Paubert (paubert@iram.es)
 *
 *  This file is based on the following documentation:
 *
 *	IBM Power Personal Systems Architecture
 *	Residual Data
 * 	Document Number: PPS-AR-FW0001
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *
 */

#include <linux/string.h>
#include <asm/residual.h>
#include <asm/pnp.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>

#include <asm/sections.h>
#include <asm/mmu.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>


unsigned char __res[sizeof(RESIDUAL)] = {0,};
RESIDUAL *res = (RESIDUAL *)&__res;

char * PnP_BASE_TYPES[] __initdata = {
  "Reserved",
  "MassStorageDevice",
  "NetworkInterfaceController",
  "DisplayController",
  "MultimediaController",
  "MemoryController",
  "BridgeController",
  "CommunicationsDevice",
  "SystemPeripheral",
  "InputDevice",
  "ServiceProcessor"
  };

/* Device Sub Type Codes */

unsigned char * PnP_SUB_TYPES[] __initdata = {
  "\001\000SCSIController",
  "\001\001IDEController",
  "\001\002FloppyController",
  "\001\003IPIController",
  "\001\200OtherMassStorageController",
  "\002\000EthernetController",
  "\002\001TokenRingController",
  "\002\002FDDIController",
  "\002\0x80OtherNetworkController",
  "\003\000VGAController",
  "\003\001SVGAController",
  "\003\002XGAController",
  "\003\200OtherDisplayController",
  "\004\000VideoController",
  "\004\001AudioController",
  "\004\200OtherMultimediaController",
  "\005\000RAM",
  "\005\001FLASH",
  "\005\200OtherMemoryDevice",
  "\006\000HostProcessorBridge",
  "\006\001ISABridge",
  "\006\002EISABridge",
  "\006\003MicroChannelBridge",
  "\006\004PCIBridge",
  "\006\005PCMCIABridge",
  "\006\006VMEBridge",
  "\006\200OtherBridgeDevice",
  "\007\000RS232Device",
  "\007\001ATCompatibleParallelPort",
  "\007\200OtherCommunicationsDevice",
  "\010\000ProgrammableInterruptController",
  "\010\001DMAController",
  "\010\002SystemTimer",
  "\010\003RealTimeClock",
  "\010\004L2Cache",
  "\010\005NVRAM",
  "\010\006PowerManagement",
  "\010\007CMOS",
  "\010\010OperatorPanel",
  "\010\011ServiceProcessorClass1",
  "\010\012ServiceProcessorClass2",
  "\010\013ServiceProcessorClass3",
  "\010\014GraphicAssist",
  "\010\017SystemPlanar",
  "\010\200OtherSystemPeripheral",
  "\011\000KeyboardController",
  "\011\001Digitizer",
  "\011\002MouseController",
  "\011\003TabletController",
  "\011\0x80OtherInputController",
  "\012\000GeneralMemoryController",
  NULL
};

/* Device Interface Type Codes */

unsigned char * PnP_INTERFACES[] __initdata = {
  "\000\000\000General",
  "\001\000\000GeneralSCSI",
  "\001\001\000GeneralIDE",
  "\001\001\001ATACompatible",

  "\001\002\000GeneralFloppy",
  "\001\002\001Compatible765",
  "\001\002\002NS398_Floppy",         /* NS Super I/O wired to use index
                                         register at port 398 and data
                                         register at port 399               */
  "\001\002\003NS26E_Floppy",         /* Ports 26E and 26F                  */
  "\001\002\004NS15C_Floppy",         /* Ports 15C and 15D                  */
  "\001\002\005NS2E_Floppy",          /* Ports 2E and 2F                    */
  "\001\002\006CHRP_Floppy",          /* CHRP Floppy in PR*P system         */

  "\001\003\000GeneralIPI",

  "\002\000\000GeneralEther",
  "\002\001\000GeneralToken",
  "\002\002\000GeneralFDDI",

  "\003\000\000GeneralVGA",
  "\003\001\000GeneralSVGA",
  "\003\002\000GeneralXGA",

  "\004\000\000GeneralVideo",
  "\004\001\000GeneralAudio",
  "\004\001\001CS4232Audio",            /* CS 4232 Plug 'n Play Configured    */

  "\005\000\000GeneralRAM",
  /* This one is obviously wrong ! */
  "\005\000\000PCIMemoryController",    /* PCI Config Method                  */
  "\005\000\001RS6KMemoryController",   /* RS6K Config Method                 */
  "\005\001\000GeneralFLASH",

  "\006\000\000GeneralHostBridge",
  "\006\001\000GeneralISABridge",
  "\006\002\000GeneralEISABridge",
  "\006\003\000GeneralMCABridge",
  /* GeneralPCIBridge = 0, */
  "\006\004\000PCIBridgeDirect",
  "\006\004\001PCIBridgeIndirect",
  "\006\004\002PCIBridgeRS6K",
  "\006\005\000GeneralPCMCIABridge",
  "\006\006\000GeneralVMEBridge",

  "\007\000\000GeneralRS232",
  "\007\000\001COMx",
  "\007\000\002Compatible16450",
  "\007\000\003Compatible16550",
  "\007\000\004NS398SerPort",         /* NS Super I/O wired to use index
                                         register at port 398 and data
                                         register at port 399               */
  "\007\000\005NS26ESerPort",         /* Ports 26E and 26F                  */
  "\007\000\006NS15CSerPort",         /* Ports 15C and 15D                  */
  "\007\000\007NS2ESerPort",          /* Ports 2E and 2F                    */

  "\007\001\000GeneralParPort",
  "\007\001\001LPTx",
  "\007\001\002NS398ParPort",         /* NS Super I/O wired to use index
                                         register at port 398 and data
                                         register at port 399               */
  "\007\001\003NS26EParPort",         /* Ports 26E and 26F                  */
  "\007\001\004NS15CParPort",         /* Ports 15C and 15D                  */
  "\007\001\005NS2EParPort",          /* Ports 2E and 2F                    */

  "\010\000\000GeneralPIC",
  "\010\000\001ISA_PIC",
  "\010\000\002EISA_PIC",
  "\010\000\003MPIC",
  "\010\000\004RS6K_PIC",

  "\010\001\000GeneralDMA",
  "\010\001\001ISA_DMA",
  "\010\001\002EISA_DMA",

  "\010\002\000GeneralTimer",
  "\010\002\001ISA_Timer",
  "\010\002\002EISA_Timer",
  "\010\003\000GeneralRTC",
  "\010\003\001ISA_RTC",

  "\010\004\001StoreThruOnly",
  "\010\004\002StoreInEnabled",
  "\010\004\003RS6KL2Cache",

  "\010\005\000IndirectNVRAM",        /* Indirectly addressed               */
  "\010\005\001DirectNVRAM",          /* Memory Mapped                      */
  "\010\005\002IndirectNVRAM24",      /* Indirectly addressed - 24 bit      */

  "\010\006\000GeneralPowerManagement",
  "\010\006\001EPOWPowerManagement",
  "\010\006\002PowerControl",         // d1378

  "\010\007\000GeneralCMOS",

  "\010\010\000GeneralOPPanel",
  "\010\010\001HarddiskLight",
  "\010\010\002CDROMLight",
  "\010\010\003PowerLight",
  "\010\010\004KeyLock",
  "\010\010\005ANDisplay",            /* AlphaNumeric Display               */
  "\010\010\006SystemStatusLED",      /* 3 digit 7 segment LED              */
  "\010\010\007CHRP_SystemStatusLED", /* CHRP LEDs in PR*P system           */

  "\010\011\000GeneralServiceProcessor",
  "\010\012\000GeneralServiceProcessor",
  "\010\013\000GeneralServiceProcessor",

  "\010\014\001TransferData",
  "\010\014\002IGMC32",
  "\010\014\003IGMC64",

  "\010\017\000GeneralSystemPlanar",   /* 10/5/95                            */
  NULL
  };

static const unsigned char __init *PnP_SUB_TYPE_STR(unsigned char BaseType,
					     unsigned char SubType) {
	unsigned char ** s=PnP_SUB_TYPES;
	while (*s && !((*s)[0]==BaseType
		       && (*s)[1]==SubType)) s++;
	if (*s) return *s+2;
	else return("Unknown !");
};

static const unsigned char __init *PnP_INTERFACE_STR(unsigned char BaseType,
					      unsigned char SubType,
					      unsigned char Interface) {
	unsigned char ** s=PnP_INTERFACES;
	while (*s && !((*s)[0]==BaseType
		       && (*s)[1]==SubType
		       && (*s)[2]==Interface)) s++;
	if (*s) return *s+3;
	else return NULL;
};

static void __init printsmallvendor(PnP_TAG_PACKET *pkt, int size) {
	int i, c;
	char decomp[4];
#define p pkt->S14_Pack.S14_Data.S14_PPCPack
	switch(p.Type) {
	case 1:
	  /* Decompress first 3 chars */
	  c = *(unsigned short *)p.PPCData;
	  decomp[0]='A'-1+((c>>10)&0x1F);
	  decomp[1]='A'-1+((c>>5)&0x1F);
	  decomp[2]='A'-1+(c&0x1F);
	  decomp[3]=0;
	  printk("    Chip identification: %s%4.4X\n",
		 decomp, ld_le16((unsigned short *)(p.PPCData+2)));
	  break;
	default:
	  printk("    Small vendor item type 0x%2.2x, data (hex): ",
		 p.Type);
	  for(i=0; i<size-2; i++) printk("%2.2x ", p.PPCData[i]);
	  printk("\n");
	  break;
	}
#undef p
}

static void __init printsmallpacket(PnP_TAG_PACKET * pkt, int size) {
	static const unsigned char * intlevel[] = {"high", "low"};
	static const unsigned char * intsense[] = {"edge", "level"};

	switch (tag_small_item_name(pkt->S1_Pack.Tag)) {
	case PnPVersion:
	  printk("    PnPversion 0x%x.%x\n",
		 pkt->S1_Pack.Version[0], /* How to interpret version ? */
		 pkt->S1_Pack.Version[1]);
	  break;
//	case Logicaldevice:
	  break;
//	case CompatibleDevice:
	  break;
	case IRQFormat:
#define p pkt->S4_Pack
	  printk("    IRQ Mask 0x%4.4x, %s %s sensitive\n",
		 ld_le16((unsigned short *)p.IRQMask),
		 intlevel[(size>3) ? !(p.IRQInfo&0x05) : 0],
		 intsense[(size>3) ? !(p.IRQInfo&0x03) : 0]);
#undef p
	  break;
	case DMAFormat:
#define p pkt->S5_Pack
	  printk("    DMA channel mask 0x%2.2x, info 0x%2.2x\n",
		 p.DMAMask, p.DMAInfo);
#undef p
	  break;
	case StartDepFunc:
	  printk("Start dependent function:\n");
	  break;
	case EndDepFunc:
	  printk("End dependent function\n");
	  break;
	case IOPort:
#define p pkt->S8_Pack
	  printk("    Variable (%d decoded bits) I/O port\n"
		 "      from 0x%4.4x to 0x%4.4x, alignment %d, %d ports\n",
		 p.IOInfo&ISAAddr16bit?16:10,
		 ld_le16((unsigned short *)p.RangeMin),
 		 ld_le16((unsigned short *)p.RangeMax),
		 p.IOAlign, p.IONum);
#undef p
	  break;
	case FixedIOPort:
#define p pkt->S9_Pack
	  printk("    Fixed (10 decoded bits) I/O port from %3.3x to %3.3x\n",
		 (p.Range[1]<<8)|p.Range[0],
		 ((p.Range[1]<<8)|p.Range[0])+p.IONum-1);
#undef p
	  break;
	case Res1:
	case Res2:
	case Res3:
	  printk("    Undefined packet type %d!\n",
		 tag_small_item_name(pkt->S1_Pack.Tag));
	  break;
	case SmallVendorItem:
	  printsmallvendor(pkt,size);
	  break;
	default:
	  printk("    Type 0x2.2x%d, size=%d\n",
		 pkt->S1_Pack.Tag, size);
	  break;
	}
}

static void __init printlargevendor(PnP_TAG_PACKET * pkt, int size) {
	static const unsigned char * addrtype[] = {"I/O", "Memory", "System"};
	static const unsigned char * inttype[] = {"8259", "MPIC", "RS6k BUID %d"};
	static const unsigned char * convtype[] = {"Bus Memory", "Bus I/O", "DMA"};
	static const unsigned char * transtype[] = {"direct", "mapped", "direct-store segment"};
	static const unsigned char * L2type[] = {"WriteThru", "CopyBack"};
	static const unsigned char * L2assoc[] = {"DirectMapped", "2-way set"};

	int i;
	char tmpstr[30], *t;
#define p pkt->L4_Pack.L4_Data.L4_PPCPack
	switch(p.Type) {
	case 2:
	  printk("    %d K %s %s L2 cache, %d/%d bytes line/sector size\n",
		 ld_le32((unsigned int *)p.PPCData),
		 L2type[p.PPCData[10]-1],
		 L2assoc[p.PPCData[4]-1],
		 ld_le16((unsigned short *)p.PPCData+3),
		 ld_le16((unsigned short *)p.PPCData+4));
	  break;
	case 3:
	  printk("    PCI Bridge parameters\n"
		 "      ConfigBaseAddress %0x\n"
		 "      ConfigBaseData %0x\n"
		 "      Bus number %d\n",
		 ld_le32((unsigned int *)p.PPCData),
		 ld_le32((unsigned int *)(p.PPCData+8)),
		 p.PPCData[16]);
	  for(i=20; i<size-4; i+=12) {
	  	int j, first;
	  	if(p.PPCData[i]) printk("      PCI Slot %d", p.PPCData[i]);
		else printk ("      Integrated PCI device");
		for(j=0, first=1, t=tmpstr; j<4; j++) {
			int line=ld_le16((unsigned short *)(p.PPCData+i+4)+j);
			if(line!=0xffff){
			        if(first) first=0; else *t++='/';
				*t++='A'+j;
			}
		}
		*t='\0';
		printk(" DevFunc 0x%x interrupt line(s) %s routed to",
		       p.PPCData[i+1],tmpstr);
		sprintf(tmpstr,
			inttype[p.PPCData[i+2]-1],
			p.PPCData[i+3]);
		printk(" %s line(s) ",
		       tmpstr);
		for(j=0, first=1, t=tmpstr; j<4; j++) {
			int line=ld_le16((unsigned short *)(p.PPCData+i+4)+j);
			if(line!=0xffff){
				if(first) first=0; else *t++='/';
				t+=sprintf(t,"%d(%c)",
					   line&0x7fff,
					   line&0x8000?'E':'L');
			}
		}
		printk("%s\n",tmpstr);
	  }
	  break;
	case 5:
	  printk("    Bridge address translation, %s decoding:\n"
		 "      Processor  Bus        Size       Conversion Translation\n"
		 "      0x%8.8x 0x%8.8x 0x%8.8x %s %s\n",
		 p.PPCData[0]&1 ? "positive" : "subtractive",
		 ld_le32((unsigned int *)p.PPCData+1),
		 ld_le32((unsigned int *)p.PPCData+3),
		 ld_le32((unsigned int *)p.PPCData+5),
		 convtype[p.PPCData[2]-1],
		 transtype[p.PPCData[1]-1]);
	  break;
	case 6:
	  printk("    Bus speed %d Hz, %d slot(s)\n",
		 ld_le32((unsigned int *)p.PPCData),
		 p.PPCData[4]);
	  break;
	case 7:
	  printk("    SCSI buses: %d, id(s):", p.PPCData[0]);
	  for(i=1; i<=p.PPCData[0]; i++)
	    printk(" %d%c", p.PPCData[i], i==p.PPCData[0] ? '\n' : ',');
	  break;
	case 9:
	  printk("    %s address (%d bits), at 0x%x size 0x%x bytes\n",
		 addrtype[p.PPCData[0]-1],
		 p.PPCData[1],
		 ld_le32((unsigned int *)(p.PPCData+4)),
		 ld_le32((unsigned int *)(p.PPCData+12)));
	  break;
	case 10:
	  sprintf(tmpstr,
		  inttype[p.PPCData[0]-1],
		  p.PPCData[1]);

	  printk("    ISA interrupts routed to %s\n"
		 "      lines",
		 tmpstr);
	  for(i=0; i<16; i++) {
	  	int line=ld_le16((unsigned short *)p.PPCData+i+1);
		if (line!=0xffff) printk(" %d(IRQ%d)", line, i);
	  }
	  printk("\n");
	  break;
	default:
	  printk("    Large vendor item type 0x%2.2x\n      Data (hex):",
		 p.Type);
	  for(i=0; i<size-4; i++) printk(" %2.2x", p.PPCData[i]);
	  printk("\n");
#undef p
	}
}

static void __init printlargepacket(PnP_TAG_PACKET * pkt, int size) {
	switch (tag_large_item_name(pkt->S1_Pack.Tag)) {
	case LargeVendorItem:
	  printlargevendor(pkt, size);
	  break;
	default:
	  printk("    Type 0x2.2x%d, size=%d\n",
		 pkt->S1_Pack.Tag, size);
	  break;
	}
}

static void __init printpackets(PnP_TAG_PACKET * pkt, const char * cat)
{
	if (pkt->S1_Pack.Tag== END_TAG) {
		printk("  No packets describing %s resources.\n", cat);
		return;
	}
	printk(  "  Packets describing %s resources:\n",cat);
	do {
		int size;
		if (tag_type(pkt->S1_Pack.Tag)) {
		  	size= 3 +
			  pkt->L1_Pack.Count0 +
			  pkt->L1_Pack.Count1*256;
			printlargepacket(pkt, size);
		} else {
			size=tag_small_count(pkt->S1_Pack.Tag)+1;
			printsmallpacket(pkt, size);
		}
		pkt = (PnP_TAG_PACKET *)((unsigned char *) pkt + size);
	} while (pkt->S1_Pack.Tag != END_TAG);
}

void __init print_residual_device_info(void)
{
	int i;
	PPC_DEVICE *dev;
#define did dev->DeviceId

	/* make sure we have residual data first */
	if (!have_residual_data)
		return;

	printk("Residual: %ld devices\n", res->ActualNumDevices);
	for ( i = 0;
	      i < res->ActualNumDevices ;
	      i++)
	{
	  	char decomp[4], sn[20];
		const char * s;
		dev = &res->Devices[i];
		s = PnP_INTERFACE_STR(did.BaseType, did.SubType,
				      did.Interface);
		if(!s) {
			sprintf(sn, "interface %d", did.Interface);
			s=sn;
		}
		if ( did.BusId & PCIDEVICE )
		  printk("PCI Device, Bus %d, DevFunc 0x%x:",
			 dev->BusAccess.PCIAccess.BusNumber,
			 dev->BusAccess.PCIAccess.DevFuncNumber);
	       	if ( did.BusId & PNPISADEVICE ) printk("PNPISA Device:");
		if ( did.BusId & ISADEVICE )
		  printk("ISA Device, Slot %d, LogicalDev %d:",
			 dev->BusAccess.ISAAccess.SlotNumber,
			 dev->BusAccess.ISAAccess.LogicalDevNumber);
		if ( did.BusId & EISADEVICE ) printk("EISA Device:");
		if ( did.BusId & PROCESSORDEVICE )
		  printk("ProcBus Device, Bus %d, BUID %d: ",
			 dev->BusAccess.ProcBusAccess.BusNumber,
			 dev->BusAccess.ProcBusAccess.BUID);
		if ( did.BusId & PCMCIADEVICE ) printk("PCMCIA ");
		if ( did.BusId & VMEDEVICE ) printk("VME ");
		if ( did.BusId & MCADEVICE ) printk("MCA ");
		if ( did.BusId & MXDEVICE ) printk("MX ");
		/* Decompress first 3 chars */
		decomp[0]='A'-1+((did.DevId>>26)&0x1F);
		decomp[1]='A'-1+((did.DevId>>21)&0x1F);
		decomp[2]='A'-1+((did.DevId>>16)&0x1F);
		decomp[3]=0;
		printk(" %s%4.4lX, %s, %s, %s\n",
		       decomp, did.DevId&0xffff,
		       PnP_BASE_TYPES[did.BaseType],
		       PnP_SUB_TYPE_STR(did.BaseType,did.SubType),
		       s);
		if ( dev->AllocatedOffset )
			printpackets( (union _PnP_TAG_PACKET *)
				      &res->DevicePnPHeap[dev->AllocatedOffset],
				      "allocated");
		if ( dev->PossibleOffset )
			printpackets( (union _PnP_TAG_PACKET *)
				      &res->DevicePnPHeap[dev->PossibleOffset],
				      "possible");
		if ( dev->CompatibleOffset )
			printpackets( (union _PnP_TAG_PACKET *)
				      &res->DevicePnPHeap[dev->CompatibleOffset],
				      "compatible");
	}
}


#if 0
static void __init printVPD(void) {
#define vpd res->VitalProductData
	int ps=vpd.PageSize, i, j;
	static const char* Usage[]={
	  "FirmwareStack",  "FirmwareHeap",  "FirmwareCode", "BootImage",
	  "Free", "Unpopulated", "ISAAddr", "PCIConfig",
	  "IOMemory", "SystemIO", "SystemRegs", "PCIAddr",
	  "UnPopSystemRom", "SystemROM", "ResumeBlock", "Other"
	};
	static const unsigned char *FWMan[]={
	  "IBM", "Motorola", "FirmWorks", "Bull"
	};
	static const unsigned char *FWFlags[]={
	  "Conventional", "OpenFirmware", "Diagnostics", "LowDebug",
	  "MultiBoot", "LowClient", "Hex41", "FAT",
	  "ISO9660", "SCSI_ID_Override", "Tape_Boot", "FW_Boot_Path"
	};
	static const unsigned char *ESM[]={
	  "Port92", "PCIConfigA8", "FF001030", "????????"
	};
	static const unsigned char *SIOM[]={
	  "Port850", "????????", "PCIConfigA8", "????????"
	};

	printk("Model: %s\n",vpd.PrintableModel);
	printk("Serial: %s\n", vpd.Serial);
	printk("FirmwareSupplier: %s\n", FWMan[vpd.FirmwareSupplier]);
	printk("FirmwareFlags:");
	for(j=0; j<12; j++) {
	  	if (vpd.FirmwareSupports & (1<<j)) {
			printk(" %s%c", FWFlags[j],
			       vpd.FirmwareSupports&(-2<<j) ? ',' : '\n');
		}
	}
	printk("NVRamSize: %ld\n", vpd.NvramSize);
	printk("SIMMslots: %ld\n", vpd.NumSIMMSlots);
	printk("EndianSwitchMethod: %s\n",
	       ESM[vpd.EndianSwitchMethod>2 ? 2 : vpd.EndianSwitchMethod]);
	printk("SpreadIOMethod: %s\n",
	       SIOM[vpd.SpreadIOMethod>3 ? 3 : vpd.SpreadIOMethod]);
	printk("Processor/Bus frequencies (Hz): %ld/%ld\n",
	       vpd.ProcessorHz, vpd.ProcessorBusHz);
	printk("Time Base Divisor: %ld\n", vpd.TimeBaseDivisor);
	printk("WordWidth, PageSize: %ld, %d\n", vpd.WordWidth, ps);
	printk("Cache sector size, Lock granularity: %ld, %ld\n",
	       vpd.CoherenceBlockSize, vpd.GranuleSize);
	for (i=0; i<res->ActualNumMemSegs; i++) {
		int mask=res->Segs[i].Usage, first, j;
		printk("%8.8lx-%8.8lx ",
		       res->Segs[i].BasePage*ps,
		       (res->Segs[i].PageCount+res->Segs[i].BasePage)*ps-1);
		for(j=15, first=1; j>=0; j--) {
			if (mask&(1<<j)) {
				if (first) first=0;
				else printk(", ");
				printk("%s", Usage[j]);
			}
		}
		printk("\n");
	}
}

/*
 * Spit out some info about residual data
 */
void print_residual_device_info(void)
{
	int i;
	union _PnP_TAG_PACKET *pkt;
	PPC_DEVICE *dev;
#define did dev->DeviceId

	/* make sure we have residual data first */
	if (!have_residual_data)
		return;
	printk("Residual: %ld devices\n", res->ActualNumDevices);
	for ( i = 0;
	      i < res->ActualNumDevices ;
	      i++)
	{
		dev = &res->Devices[i];
		/*
		 * pci devices
		 */
		if ( did.BusId & PCIDEVICE )
		{
			printk("PCI Device:");
			/* unknown vendor */
			if ( !strncmp( "Unknown", pci_strvendor(did.DevId>>16), 7) )
				printk(" id %08lx types %d/%d", did.DevId,
				       did.BaseType, did.SubType);
			/* known vendor */
			else
				printk(" %s %s",
				       pci_strvendor(did.DevId>>16),
				       pci_strdev(did.DevId>>16,
						  did.DevId&0xffff)
					);

			if ( did.BusId & PNPISADEVICE )
			{
				printk(" pnp:");
				/* get pnp info on the device */
				pkt = (union _PnP_TAG_PACKET *)
					&res->DevicePnPHeap[dev->AllocatedOffset];
				for (; pkt->S1_Pack.Tag != DF_END_TAG;
				     pkt++ )
				{
					if ( (pkt->S1_Pack.Tag == S4_Packet) ||
					     (pkt->S1_Pack.Tag == S4_Packet_flags) )
						printk(" irq %02x%02x",
						       pkt->S4_Pack.IRQMask[0],
						       pkt->S4_Pack.IRQMask[1]);
				}
			}
			printk("\n");
			continue;
		}
		/*
		 * isa devices
		 */
		if ( did.BusId & ISADEVICE )
		{
			printk("ISA Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}
		/*
		 * eisa devices
		 */
		if ( did.BusId & EISADEVICE )
		{
			printk("EISA Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}
		/*
		 * proc bus devices
		 */
		if ( did.BusId & PROCESSORDEVICE )
		{
			printk("ProcBus Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}
		/*
		 * pcmcia devices
		 */
		if ( did.BusId & PCMCIADEVICE )
		{
			printk("PCMCIA Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}
		printk("Unknown bus access device: busid %lx\n",
		       did.BusId);
	}
}
#endif

/* Returns the device index in the residual data,
   any of the search items may be set as -1 for wildcard,
   DevID number field (second halfword) is big endian !

   Examples:
   - search for the Interrupt controller (8259 type), 2 methods:
     1) i8259 = residual_find_device(~0,
                                     NULL,
				     SystemPeripheral,
				     ProgrammableInterruptController,
				     ISA_PIC,
				     0);
     2) i8259 = residual_find_device(~0, "PNP0000", -1, -1, -1, 0)

   - search for the first two serial devices, whatever their type)
     iserial1 = residual_find_device(~0,NULL,
                                     CommunicationsDevice,
				     RS232Device,
				     -1, 0)
     iserial2 = residual_find_device(~0,NULL,
                                     CommunicationsDevice,
				     RS232Device,
				     -1, 1)
   - but search for typical COM1 and COM2 is not easy due to the
     fact that the interface may be anything and the name "PNP0500" or
     "PNP0501". Quite bad.

*/

/* devid are easier to uncompress than to compress, so to minimize bloat
in this rarely used area we unencode and compare */

/* in residual data number is big endian in the device table and
little endian in the heap, so we use two parameters to avoid writing
two very similar functions */

static int __init same_DevID(unsigned short vendor,
	       unsigned short Number,
	       char * str)
{
	static unsigned const char hexdigit[]="0123456789ABCDEF";
	if (strlen(str)!=7) return 0;
	if ( ( ((vendor>>10)&0x1f)+'A'-1 == str[0])  &&
	     ( ((vendor>>5)&0x1f)+'A'-1 == str[1])   &&
	     ( (vendor&0x1f)+'A'-1 == str[2])        &&
	     (hexdigit[(Number>>12)&0x0f] == str[3]) &&
	     (hexdigit[(Number>>8)&0x0f] == str[4])  &&
	     (hexdigit[(Number>>4)&0x0f] == str[5])  &&
	     (hexdigit[Number&0x0f] == str[6]) ) return 1;
	return 0;
}

PPC_DEVICE __init *residual_find_device(unsigned long BusMask,
			 unsigned char * DevID,
			 int BaseType,
			 int SubType,
			 int Interface,
			 int n)
{
	int i;
	if (!have_residual_data) return NULL;
	for (i=0; i<res->ActualNumDevices; i++) {
#define Dev res->Devices[i].DeviceId
		if ( (Dev.BusId&BusMask)                                  &&
		     (BaseType==-1 || Dev.BaseType==BaseType)             &&
		     (SubType==-1 || Dev.SubType==SubType)                &&
		     (Interface==-1 || Dev.Interface==Interface)          &&
		     (DevID==NULL || same_DevID((Dev.DevId>>16)&0xffff,
						Dev.DevId&0xffff, DevID)) &&
		     !(n--) ) return res->Devices+i;
#undef Dev
	}
	return NULL;
}

PPC_DEVICE __init *residual_find_device_id(unsigned long BusMask,
			 unsigned short DevID,
			 int BaseType,
			 int SubType,
			 int Interface,
			 int n)
{
	int i;
	if (!have_residual_data) return NULL;
	for (i=0; i<res->ActualNumDevices; i++) {
#define Dev res->Devices[i].DeviceId
		if ( (Dev.BusId&BusMask)                                  &&
		     (BaseType==-1 || Dev.BaseType==BaseType)             &&
		     (SubType==-1 || Dev.SubType==SubType)                &&
		     (Interface==-1 || Dev.Interface==Interface)          &&
		     (DevID==0xffff || (Dev.DevId&0xffff) == DevID)	  &&
		     !(n--) ) return res->Devices+i;
#undef Dev
	}
	return NULL;
}

static int __init
residual_scan_pcibridge(PnP_TAG_PACKET * pkt, struct pci_dev *dev)
{
	int irq = -1;

#define data pkt->L4_Pack.L4_Data.L4_PPCPack.PPCData
	if (dev->bus->number == data[16]) {
		int i, size;

		size = 3 + ld_le16((u_short *) (&pkt->L4_Pack.Count0));
		for (i = 20; i < size - 4; i += 12) {
			unsigned char pin;
			int line_irq;

			if (dev->devfn != data[i + 1])
				continue;

			pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
			if (pin) {
				line_irq = ld_le16((unsigned short *)
						(&data[i + 4 + 2 * (pin - 1)]));
				irq = (line_irq == 0xffff) ? 0
							   : line_irq & 0x7fff;
			} else
				irq = 0;

			break;
		}
	}
#undef data

	return irq;
}

int __init
residual_pcidev_irq(struct pci_dev *dev)
{
	int i = 0;
	int irq = -1;
	PPC_DEVICE *bridge;

	while ((bridge = residual_find_device
	       (-1, NULL, BridgeController, PCIBridge, -1, i++))) {

		PnP_TAG_PACKET *pkt;
		if (bridge->AllocatedOffset) {
			pkt = PnP_find_large_vendor_packet(res->DevicePnPHeap +
					   bridge->AllocatedOffset, 3, 0);
			if (!pkt)
				continue;

			irq = residual_scan_pcibridge(pkt, dev);
			if (irq != -1)
				break;
		}
	}

	return (irq < 0) ? 0 : irq;
}

void __init residual_irq_mask(char *irq_edge_mask_lo, char *irq_edge_mask_hi)
{
	PPC_DEVICE *dev;
	int i = 0;
	unsigned short irq_mask = 0x000; /* default to edge */

	while ((dev = residual_find_device(-1, NULL, -1, -1, -1, i++))) {
		PnP_TAG_PACKET *pkt;
		unsigned short mask;
		int size;
		int offset = dev->AllocatedOffset;

		if (!offset)
			continue;

		pkt = PnP_find_packet(res->DevicePnPHeap + offset,
					      IRQFormat, 0);
		if (!pkt)
			continue;

		size = tag_small_count(pkt->S1_Pack.Tag) + 1;
		mask = ld_le16((unsigned short *)pkt->S4_Pack.IRQMask);
		if (size > 3 && (pkt->S4_Pack.IRQInfo & 0x0c))
			irq_mask |= mask;
	}

	*irq_edge_mask_lo = irq_mask & 0xff;
	*irq_edge_mask_hi = irq_mask >> 8;
}

unsigned int __init residual_isapic_addr(void)
{
	PPC_DEVICE *isapic;
	PnP_TAG_PACKET *pkt;
	unsigned int addr;

	isapic = residual_find_device(~0, NULL, SystemPeripheral,
				      ProgrammableInterruptController,
				      ISA_PIC, 0);
	if (!isapic)
		goto unknown;

	pkt = PnP_find_large_vendor_packet(res->DevicePnPHeap +
						isapic->AllocatedOffset, 9, 0);
	if (!pkt)
		goto unknown;

#define p pkt->L4_Pack.L4_Data.L4_PPCPack
	/* Must be 32-bit system address */
	if (!((p.PPCData[0] == 3) && (p.PPCData[1] == 32)))
		goto unknown;

	/* It doesn't seem to work where length != 1 (what can I say? :-/ ) */
	if (ld_le32((unsigned int *)(p.PPCData + 12)) != 1)
		goto unknown;

	addr = ld_le32((unsigned int *) (p.PPCData + 4));
#undef p
	return addr;
unknown:
	return 0;
}

PnP_TAG_PACKET *PnP_find_packet(unsigned char *p,
				unsigned packet_tag,
				int n)
{
	unsigned mask, masked_tag, size;
	if(!p) return NULL;
	if (tag_type(packet_tag)) mask=0xff; else mask=0xF8;
	masked_tag = packet_tag&mask;
	for(; *p != END_TAG; p+=size) {
		if ((*p & mask) == masked_tag && !(n--))
			return (PnP_TAG_PACKET *) p;
		if (tag_type(*p))
			size=ld_le16((unsigned short *)(p+1))+3;
		else
			size=tag_small_count(*p)+1;
	}
	return NULL; /* not found */
}

PnP_TAG_PACKET __init *PnP_find_small_vendor_packet(unsigned char *p,
					     unsigned packet_type,
					     int n)
{
	int next=0;
	while (p) {
		p = (unsigned char *) PnP_find_packet(p, 0x70, next);
		if (p && p[1]==packet_type && !(n--))
			return (PnP_TAG_PACKET *) p;
		next = 1;
	};
	return NULL; /* not found */
}

PnP_TAG_PACKET __init *PnP_find_large_vendor_packet(unsigned char *p,
					   unsigned packet_type,
					   int n)
{
	int next=0;
	while (p) {
		p = (unsigned char *) PnP_find_packet(p, 0x84, next);
		if (p && p[3]==packet_type && !(n--))
			return (PnP_TAG_PACKET *) p;
		next = 1;
	};
	return NULL; /* not found */
}

#ifdef CONFIG_PROC_PREPRESIDUAL
static int proc_prep_residual_read(char * buf, char ** start, off_t off,
		int count, int *eof, void *data)
{
	int n;

	n = res->ResidualLength - off;
	if (n < 0) {
		*eof = 1;
		n = 0;
	}
	else {
		if (n > count)
			n = count;
		else
			*eof = 1;

		memcpy(buf, (char *)res + off, n);
		*start = buf;
	}

	return n;
}

int __init
proc_prep_residual_init(void)
{
	if (have_residual_data)
		create_proc_read_entry("residual", S_IRUGO, NULL,
					proc_prep_residual_read, NULL);
	return 0;
}

__initcall(proc_prep_residual_init);
#endif
