/*
 *  File.........: pktdrvr.c
 *
 *  Responsible..: Gisle Vanem,  giva@bgnett.no
 *
 *  Created......: 26.Sept 1995
 *
 *  Description..: Packet-driver interface for 16/32-bit C :
 *                 Borland C/C++ 3.0+ small/large model
 *                 Watcom C/C++ 11+, DOS4GW flat model
 *                 Metaware HighC 3.1+ and PharLap 386|DosX
 *                 GNU C/C++ 2.7+ and djgpp 2.x extender
 *
 *  References...: PC/TCP Packet driver Specification. rev 1.09
 *                 FTP Software Inc.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include "pcap-dos.h"
#include "pcap-int.h"
#include "msdos/pktdrvr.h"

#if (DOSX)
#define NUM_RX_BUF  32      /* # of buffers in Rx FIFO queue */
#else
#define NUM_RX_BUF  10
#endif

#define DIM(x)   (sizeof((x)) / sizeof(x[0]))
#define PUTS(s)  do {                                           \
                   if (!pktInfo.quiet)                          \
                      pktInfo.error ?                           \
                        printf ("%s: %s\n", s, pktInfo.error) : \
                        printf ("%s\n", pktInfo.error = s);     \
                 } while (0)

#if defined(__HIGHC__)
  extern UINT _mwenv;

#elif defined(__DJGPP__)
  #include <stddef.h>
  #include <dpmi.h>
  #include <go32.h>
  #include <pc.h>
  #include <sys/farptr.h>

#elif defined(__WATCOMC__)
  #include <i86.h>
  #include <stddef.h>
  extern char _Extender;

#else
  extern void far PktReceiver (void);
#endif


#if (DOSX & (DJGPP|DOS4GW))
  #include <sys/pack_on.h>

  struct DPMI_regs {
         DWORD  r_di;
         DWORD  r_si;
         DWORD  r_bp;
         DWORD  reserved;
         DWORD  r_bx;
         DWORD  r_dx;
         DWORD  r_cx;
         DWORD  r_ax;
         WORD   r_flags;
         WORD   r_es, r_ds, r_fs, r_gs;
         WORD   r_ip, r_cs, r_sp, r_ss;
       };

  /* Data located in a real-mode segment. This becomes far at runtime
   */
  typedef struct  {          /* must match data/code in pkt_rx1.s */
          WORD       _rxOutOfs;
          WORD       _rxInOfs;
          DWORD      _pktDrop;
          BYTE       _pktTemp [20];
          TX_ELEMENT _pktTxBuf[1];
          RX_ELEMENT _pktRxBuf[NUM_RX_BUF];
          WORD       _dummy[2];        /* screenSeg,newInOffset */
          BYTE       _fanChars[4];
          WORD       _fanIndex;
          BYTE       _PktReceiver[15]; /* starts on a paragraph (16byte) */
        } PktRealStub;
  #include <sys/pack_off.h>

  static BYTE real_stub_array [] = {
         #include "pkt_stub.inc"       /* generated opcode array */
       };

  #define rxOutOfs      offsetof (PktRealStub,_rxOutOfs)
  #define rxInOfs       offsetof (PktRealStub,_rxInOfs)
  #define PktReceiver   offsetof (PktRealStub,_PktReceiver [para_skip])
  #define pktDrop       offsetof (PktRealStub,_pktDrop)
  #define pktTemp       offsetof (PktRealStub,_pktTemp)
  #define pktTxBuf      offsetof (PktRealStub,_pktTxBuf)
  #define FIRST_RX_BUF  offsetof (PktRealStub,_pktRxBuf [0])
  #define LAST_RX_BUF   offsetof (PktRealStub,_pktRxBuf [NUM_RX_BUF-1])

#else
  extern WORD       rxOutOfs;    /* offsets into pktRxBuf FIFO queue   */
  extern WORD       rxInOfs;
  extern DWORD      pktDrop;     /* # packets dropped in PktReceiver() */
  extern BYTE       pktRxEnd;    /* marks the end of r-mode code/data  */

  extern RX_ELEMENT pktRxBuf [NUM_RX_BUF];       /* PktDrvr Rx buffers */
  extern TX_ELEMENT pktTxBuf;                    /* PktDrvr Tx buffer  */
  extern char       pktTemp[20];                 /* PktDrvr temp area  */

  #define FIRST_RX_BUF (WORD) &pktRxBuf [0]
  #define LAST_RX_BUF  (WORD) &pktRxBuf [NUM_RX_BUF-1]
#endif


#ifdef __BORLANDC__           /* Use Borland's inline functions */
  #define memcpy  __memcpy__
  #define memcmp  __memcmp__
  #define memset  __memset__
#endif


#if (DOSX & PHARLAP)
  extern void PktReceiver (void);     /* in pkt_rx0.asm */
  static int  RealCopy    (ULONG, ULONG, REALPTR*, FARPTR*, USHORT*);

  #undef  FP_SEG
  #undef  FP_OFF
  #define FP_OFF(x)     ((WORD)(x))
  #define FP_SEG(x)     ((WORD)(realBase >> 16))
  #define DOS_ADDR(s,o) (((DWORD)(s) << 16) + (WORD)(o))
  #define r_ax          eax
  #define r_bx          ebx
  #define r_dx          edx
  #define r_cx          ecx
  #define r_si          esi
  #define r_di          edi
  #define r_ds          ds
  #define r_es          es
  LOCAL FARPTR          protBase;
  LOCAL REALPTR         realBase;
  LOCAL WORD            realSeg;   /* DOS para-address of allocated area */
  LOCAL SWI_REGS        reg;

  static WORD _far *rxOutOfsFp, *rxInOfsFp;

#elif (DOSX & DJGPP)
  static _go32_dpmi_seginfo rm_mem;
  static __dpmi_regs        reg;
  static DWORD              realBase;
  static int                para_skip = 0;

  #define DOS_ADDR(s,o)     (((WORD)(s) << 4) + (o))
  #define r_ax              x.ax
  #define r_bx              x.bx
  #define r_dx              x.dx
  #define r_cx              x.cx
  #define r_si              x.si
  #define r_di              x.di
  #define r_ds              x.ds
  #define r_es              x.es

#elif (DOSX & DOS4GW)
  LOCAL struct DPMI_regs    reg;
  LOCAL WORD                rm_base_seg, rm_base_sel;
  LOCAL DWORD               realBase;
  LOCAL int                 para_skip = 0;

  LOCAL DWORD dpmi_get_real_vector (int intr);
  LOCAL WORD  dpmi_real_malloc     (int size, WORD *selector);
  LOCAL void  dpmi_real_free       (WORD selector);
  #define DOS_ADDR(s,o) (((DWORD)(s) << 4) + (WORD)(o))

#else              /* real-mode Borland etc. */
  static struct  {
         WORD r_ax, r_bx, r_cx, r_dx, r_bp;
         WORD r_si, r_di, r_ds, r_es, r_flags;
       } reg;
#endif

#ifdef __HIGHC__
  #pragma Alias (pktDrop,    "_pktDrop")
  #pragma Alias (pktRxBuf,   "_pktRxBuf")
  #pragma Alias (pktTxBuf,   "_pktTxBuf")
  #pragma Alias (pktTemp,    "_pktTemp")
  #pragma Alias (rxOutOfs,   "_rxOutOfs")
  #pragma Alias (rxInOfs,    "_rxInOfs")
  #pragma Alias (pktRxEnd,   "_pktRxEnd")
  #pragma Alias (PktReceiver,"_PktReceiver")
#endif


PUBLIC PKT_STAT    pktStat;    /* statistics for packets    */
PUBLIC PKT_INFO    pktInfo;    /* packet-driver information */

PUBLIC PKT_RX_MODE receiveMode  = PDRX_DIRECT;
PUBLIC ETHER       myAddress    = {   0,  0,  0,  0,  0,  0 };
PUBLIC ETHER       ethBroadcast = { 255,255,255,255,255,255 };

LOCAL  struct {             /* internal statistics */
       DWORD  tooSmall;     /* size < ETH_MIN */
       DWORD  tooLarge;     /* size > ETH_MAX */
       DWORD  badSync;      /* count_1 != count_2 */
       DWORD  wrongHandle;  /* upcall to wrong handle */
     } intStat;

/***************************************************************************/

PUBLIC const char *PktGetErrorStr (int errNum)
{
  static const char *errStr[] = {
                    "",
                    "Invalid handle number",
                    "No interfaces of specified class found",
                    "No interfaces of specified type found",
                    "No interfaces of specified number found",
                    "Bad packet type specified",
                    "Interface does not support multicast",
                    "Packet driver cannot terminate",
                    "Invalid receiver mode specified",
                    "Insufficient memory space",
                    "Type previously accessed, and not released",
                    "Command out of range, or not implemented",
                    "Cannot send packet (usually hardware error)",
                    "Cannot change hardware address ( > 1 handle open)",
                    "Hardware address has bad length or format",
                    "Cannot reset interface (more than 1 handle open)",
                    "Bad Check-sum",
                    "Bad size",
                    "Bad sync" ,
                    "Source hit"
                  };

  if (errNum < 0 || errNum >= DIM(errStr))
     return ("Unknown driver error.");
  return (errStr [errNum]);
}

/**************************************************************************/

PUBLIC const char *PktGetClassName (WORD class)
{
  switch (class)
  {
    case PD_ETHER:
         return ("DIX-Ether");
    case PD_PRONET10:
         return ("ProNET-10");
    case PD_IEEE8025:
         return ("IEEE 802.5");
    case PD_OMNINET:
         return ("OmniNet");
    case PD_APPLETALK:
         return ("AppleTalk");
    case PD_SLIP:
         return ("SLIP");
    case PD_STARTLAN:
         return ("StartLAN");
    case PD_ARCNET:
         return ("ArcNet");
    case PD_AX25:
         return ("AX.25");
    case PD_KISS:
         return ("KISS");
    case PD_IEEE8023_2:
         return ("IEEE 802.3 w/802.2 hdr");
    case PD_FDDI8022:
         return ("FDDI w/802.2 hdr");
    case PD_X25:
         return ("X.25");
    case PD_LANstar:
         return ("LANstar");
    case PD_PPP:
         return ("PPP");
    default:
         return ("unknown");
  }
}

/**************************************************************************/

PUBLIC char const *PktRXmodeStr (PKT_RX_MODE mode)
{
  static const char *modeStr [] = {
                    "Receiver turned off",
                    "Receive only directly addressed packets",
                    "Receive direct & broadcast packets",
                    "Receive direct,broadcast and limited multicast packets",
                    "Receive direct,broadcast and all multicast packets",
                    "Receive all packets (promiscuouos mode)"
                  };

  if (mode > DIM(modeStr))
     return ("??");
  return (modeStr [mode-1]);
}

/**************************************************************************/

LOCAL __inline BOOL PktInterrupt (void)
{
  BOOL okay;

#if (DOSX & PHARLAP)
  _dx_real_int ((UINT)pktInfo.intr, &reg);
  okay = ((reg.flags & 1) == 0);  /* OK if carry clear */

#elif (DOSX & DJGPP)
  __dpmi_int ((int)pktInfo.intr, &reg);
  okay = ((reg.x.flags & 1) == 0);

#elif (DOSX & DOS4GW)
  union  REGS  r;
  struct SREGS s;

  memset (&r, 0, sizeof(r));
  segread (&s);
  r.w.ax  = 0x300;
  r.x.ebx = pktInfo.intr;
  r.w.cx  = 0;
  s.es    = FP_SEG (&reg);
  r.x.edi = FP_OFF (&reg);
  reg.r_flags = 0;
  reg.r_ss = reg.r_sp = 0;     /* DPMI host provides stack */

  int386x (0x31, &r, &r, &s);
  okay = (!r.w.cflag);

#else
  reg.r_flags = 0;
  intr (pktInfo.intr, (struct REGPACK*)&reg);
  okay = ((reg.r_flags & 1) == 0);
#endif

  if (okay)
       pktInfo.error = NULL;
  else pktInfo.error = PktGetErrorStr (reg.r_dx >> 8);
  return (okay);
}

/**************************************************************************/

/*
 * Search for packet driver at interrupt 60h through 80h. If ASCIIZ
 * string "PKT DRVR" found at offset 3 in the interrupt handler, return
 * interrupt number, else return zero in pktInfo.intr
 */
PUBLIC BOOL PktSearchDriver (void)
{
  BYTE intr  = 0x20;
  BOOL found = FALSE;

  while (!found && intr < 0xFF)
  {
    static char str[12];                 /* 3 + strlen("PKT DRVR") */
    static char pktStr[9] = "PKT DRVR";  /* ASCIIZ string at ofs 3 */
    DWORD  rp;                           /* in interrupt  routine  */

#if (DOSX & PHARLAP)
    _dx_rmiv_get (intr, &rp);
    ReadRealMem (&str, (REALPTR)rp, sizeof(str));

#elif (DOSX & DJGPP)
    __dpmi_raddr realAdr;
    __dpmi_get_real_mode_interrupt_vector (intr, &realAdr);
    rp = (realAdr.segment << 4) + realAdr.offset16;
    dosmemget (rp, sizeof(str), &str);

#elif (DOSX & DOS4GW)
    rp = dpmi_get_real_vector (intr);
    memcpy (&str, (void*)rp, sizeof(str));

#else
    _fmemcpy (&str, getvect(intr), sizeof(str));
#endif

    found = memcmp (&str[3],&pktStr,sizeof(pktStr)) == 0;
    intr++;
  }
  pktInfo.intr = (found ? intr-1 : 0);
  return (found);
}


/**************************************************************************/

static BOOL PktSetAccess (void)
{
  reg.r_ax = 0x0200 + pktInfo.class;
  reg.r_bx = 0xFFFF;
  reg.r_dx = 0;
  reg.r_cx = 0;

#if (DOSX & PHARLAP)
  reg.ds  = 0;
  reg.esi = 0;
  reg.es  = RP_SEG (realBase);
  reg.edi = (WORD) &PktReceiver;

#elif (DOSX & DJGPP)
  reg.x.ds = 0;
  reg.x.si = 0;
  reg.x.es = rm_mem.rm_segment;
  reg.x.di = PktReceiver;

#elif (DOSX & DOS4GW)
  reg.r_ds = 0;
  reg.r_si = 0;
  reg.r_es = rm_base_seg;
  reg.r_di = PktReceiver;

#else
  reg.r_ds = 0;
  reg.r_si = 0;
  reg.r_es = FP_SEG (&PktReceiver);
  reg.r_di = FP_OFF (&PktReceiver);
#endif

  if (!PktInterrupt())
     return (FALSE);

  pktInfo.handle = reg.r_ax;
  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktReleaseHandle (WORD handle)
{
  reg.r_ax = 0x0300;
  reg.r_bx = handle;
  return PktInterrupt();
}

/**************************************************************************/

PUBLIC BOOL PktTransmit (const void *eth, int len)
{
  if (len > ETH_MTU)
     return (FALSE);

  reg.r_ax = 0x0400;             /* Function 4, send pkt */
  reg.r_cx = len;                /* total size of frame  */

#if (DOSX & DJGPP)
  dosmemput (eth, len, realBase+pktTxBuf);
  reg.x.ds = rm_mem.rm_segment;  /* DOS data segment and */
  reg.x.si = pktTxBuf;           /* DOS offset to buffer */

#elif (DOSX & DOS4GW)
  memcpy ((void*)(realBase+pktTxBuf), eth, len);
  reg.r_ds = rm_base_seg;
  reg.r_si = pktTxBuf;

#elif (DOSX & PHARLAP)
  memcpy (&pktTxBuf, eth, len);
  reg.r_ds = FP_SEG (&pktTxBuf);
  reg.r_si = FP_OFF (&pktTxBuf);

#else
  reg.r_ds = FP_SEG (eth);
  reg.r_si = FP_OFF (eth);
#endif

  return PktInterrupt();
}

/**************************************************************************/

#if (DOSX & (DJGPP|DOS4GW))
LOCAL __inline BOOL CheckElement (RX_ELEMENT *rx)
#else
LOCAL __inline BOOL CheckElement (RX_ELEMENT _far *rx)
#endif
{
  WORD count_1, count_2;

  /*
   * We got an upcall to the same RMCB with wrong handle.
   * This can happen if we failed to release handle at program exit
   */
  if (rx->handle != pktInfo.handle)
  {
    pktInfo.error = "Wrong handle";
    intStat.wrongHandle++;
    PktReleaseHandle (rx->handle);
    return (FALSE);
  }
  count_1 = rx->firstCount;
  count_2 = rx->secondCount;

  if (count_1 != count_2)
  {
    pktInfo.error = "Bad sync";
    intStat.badSync++;
    return (FALSE);
  }
  if (count_1 > ETH_MAX)
  {
    pktInfo.error = "Large esize";
    intStat.tooLarge++;
    return (FALSE);
  }
#if 0
  if (count_1 < ETH_MIN)
  {
    pktInfo.error = "Small esize";
    intStat.tooSmall++;
    return (FALSE);
  }
#endif
  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktTerminHandle (WORD handle)
{
  reg.r_ax = 0x0500;
  reg.r_bx = handle;
  return PktInterrupt();
}

/**************************************************************************/

PUBLIC BOOL PktResetInterface (WORD handle)
{
  reg.r_ax = 0x0700;
  reg.r_bx = handle;
  return PktInterrupt();
}

/**************************************************************************/

PUBLIC BOOL PktSetReceiverMode (PKT_RX_MODE mode)
{
  if (pktInfo.class == PD_SLIP || pktInfo.class == PD_PPP)
     return (TRUE);

  reg.r_ax = 0x1400;
  reg.r_bx = pktInfo.handle;
  reg.r_cx = (WORD)mode;

  if (!PktInterrupt())
     return (FALSE);

  receiveMode = mode;
  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktGetReceiverMode (PKT_RX_MODE *mode)
{
  reg.r_ax = 0x1500;
  reg.r_bx = pktInfo.handle;

  if (!PktInterrupt())
     return (FALSE);

  *mode = reg.r_ax;
  return (TRUE);
}

/**************************************************************************/

static PKT_STAT initialStat;         /* statistics at startup */
static BOOL     resetStat = FALSE;   /* statistics reset ? */

PUBLIC BOOL PktGetStatistics (WORD handle)
{
  reg.r_ax = 0x1800;
  reg.r_bx = handle;

  if (!PktInterrupt())
     return (FALSE);

#if (DOSX & PHARLAP)
  ReadRealMem (&pktStat, DOS_ADDR(reg.ds,reg.esi), sizeof(pktStat));

#elif (DOSX & DJGPP)
  dosmemget (DOS_ADDR(reg.x.ds,reg.x.si), sizeof(pktStat), &pktStat);

#elif (DOSX & DOS4GW)
  memcpy (&pktStat, (void*)DOS_ADDR(reg.r_ds,reg.r_si), sizeof(pktStat));

#else
  _fmemcpy (&pktStat, MK_FP(reg.r_ds,reg.r_si), sizeof(pktStat));
#endif

  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktSessStatistics (WORD handle)
{
  if (!PktGetStatistics(pktInfo.handle))
     return (FALSE);

  if (resetStat)
  {
    pktStat.inPackets  -= initialStat.inPackets;
    pktStat.outPackets -= initialStat.outPackets;
    pktStat.inBytes    -= initialStat.inBytes;
    pktStat.outBytes   -= initialStat.outBytes;
    pktStat.inErrors   -= initialStat.inErrors;
    pktStat.outErrors  -= initialStat.outErrors;
    pktStat.outErrors  -= initialStat.outErrors;
    pktStat.lost       -= initialStat.lost;
  }
  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktResetStatistics (WORD handle)
{
  if (!PktGetStatistics(pktInfo.handle))
     return (FALSE);

  memcpy (&initialStat, &pktStat, sizeof(initialStat));
  resetStat = TRUE;
  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktGetAddress (ETHER *addr)
{
  reg.r_ax = 0x0600;
  reg.r_bx = pktInfo.handle;
  reg.r_cx = sizeof (*addr);

#if (DOSX & DJGPP)
  reg.x.es = rm_mem.rm_segment;
  reg.x.di = pktTemp;
#elif (DOSX & DOS4GW)
  reg.r_es = rm_base_seg;
  reg.r_di = pktTemp;
#else
  reg.r_es = FP_SEG (&pktTemp);
  reg.r_di = FP_OFF (&pktTemp);  /* ES:DI = address for result */
#endif

  if (!PktInterrupt())
     return (FALSE);

#if (DOSX & PHARLAP)
  ReadRealMem (addr, realBase + (WORD)&pktTemp, sizeof(*addr));

#elif (DOSX & DJGPP)
  dosmemget (realBase+pktTemp, sizeof(*addr), addr);

#elif (DOSX & DOS4GW)
  memcpy (addr, (void*)(realBase+pktTemp), sizeof(*addr));

#else
  memcpy ((void*)addr, &pktTemp, sizeof(*addr));
#endif

  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktSetAddress (const ETHER *addr)
{
  /* copy addr to real-mode scrath area */

#if (DOSX & PHARLAP)
  WriteRealMem (realBase + (WORD)&pktTemp, (void*)addr, sizeof(*addr));

#elif (DOSX & DJGPP)
  dosmemput (addr, sizeof(*addr), realBase+pktTemp);

#elif (DOSX & DOS4GW)
  memcpy ((void*)(realBase+pktTemp), addr, sizeof(*addr));

#else
  memcpy (&pktTemp, (void*)addr, sizeof(*addr));
#endif

  reg.r_ax = 0x1900;
  reg.r_cx = sizeof (*addr);      /* address length       */

#if (DOSX & DJGPP)
  reg.x.es = rm_mem.rm_segment;   /* DOS offset to param  */
  reg.x.di = pktTemp;             /* DOS segment to param */
#elif (DOSX & DOS4GW)
  reg.r_es = rm_base_seg;
  reg.r_di = pktTemp;
#else
  reg.r_es = FP_SEG (&pktTemp);
  reg.r_di = FP_OFF (&pktTemp);
#endif

  return PktInterrupt();
}

/**************************************************************************/

PUBLIC BOOL PktGetDriverInfo (void)
{
  pktInfo.majVer = 0;
  pktInfo.minVer = 0;
  memset (&pktInfo.name, 0, sizeof(pktInfo.name));
  reg.r_ax = 0x01FF;
  reg.r_bx = 0;

  if (!PktInterrupt())
     return (FALSE);

  pktInfo.number = reg.r_cx & 0xFF;
  pktInfo.class  = reg.r_cx >> 8;
#if 0
  pktInfo.minVer = reg.r_bx % 10;
  pktInfo.majVer = reg.r_bx / 10;
#else
  pktInfo.majVer = reg.r_bx;  // !!
#endif
  pktInfo.funcs  = reg.r_ax & 0xFF;
  pktInfo.type   = reg.r_dx & 0xFF;

#if (DOSX & PHARLAP)
  ReadRealMem (&pktInfo.name, DOS_ADDR(reg.ds,reg.esi), sizeof(pktInfo.name));

#elif (DOSX & DJGPP)
  dosmemget (DOS_ADDR(reg.x.ds,reg.x.si), sizeof(pktInfo.name), &pktInfo.name);

#elif (DOSX & DOS4GW)
  memcpy (&pktInfo.name, (void*)DOS_ADDR(reg.r_ds,reg.r_si), sizeof(pktInfo.name));

#else
  _fmemcpy (&pktInfo.name, MK_FP(reg.r_ds,reg.r_si), sizeof(pktInfo.name));
#endif
  return (TRUE);
}

/**************************************************************************/

PUBLIC BOOL PktGetDriverParam (void)
{
  reg.r_ax = 0x0A00;

  if (!PktInterrupt())
     return (FALSE);

#if (DOSX & PHARLAP)
  ReadRealMem (&pktInfo.majVer, DOS_ADDR(reg.es,reg.edi), PKT_PARAM_SIZE);

#elif (DOSX & DJGPP)
  dosmemget (DOS_ADDR(reg.x.es,reg.x.di), PKT_PARAM_SIZE, &pktInfo.majVer);

#elif (DOSX & DOS4GW)
  memcpy (&pktInfo.majVer, (void*)DOS_ADDR(reg.r_es,reg.r_di), PKT_PARAM_SIZE);

#else
  _fmemcpy (&pktInfo.majVer, MK_FP(reg.r_es,reg.r_di), PKT_PARAM_SIZE);
#endif
  return (TRUE);
}

/**************************************************************************/

#if (DOSX & PHARLAP)
  PUBLIC int PktReceive (BYTE *buf, int max)
  {
    WORD inOfs  = *rxInOfsFp;
    WORD outOfs = *rxOutOfsFp;

    if (outOfs != inOfs)
    {
      RX_ELEMENT _far *head = (RX_ELEMENT _far*)(protBase+outOfs);
      int size, len = max;

      if (CheckElement(head))
      {
        size = min (head->firstCount, sizeof(RX_ELEMENT));
        len  = min (size, max);
        _fmemcpy (buf, &head->destin, len);
      }
      else
        size = -1;

      outOfs += sizeof (RX_ELEMENT);
      if (outOfs > LAST_RX_BUF)
          outOfs = FIRST_RX_BUF;
      *rxOutOfsFp = outOfs;
      return (size);
    }
    return (0);
  }

  PUBLIC void PktQueueBusy (BOOL busy)
  {
    *rxOutOfsFp = busy ? (*rxInOfsFp + sizeof(RX_ELEMENT)) : *rxInOfsFp;
    if (*rxOutOfsFp > LAST_RX_BUF)
        *rxOutOfsFp = FIRST_RX_BUF;
    *(DWORD _far*)(protBase + (WORD)&pktDrop) = 0;
  }

  PUBLIC WORD PktBuffersUsed (void)
  {
    WORD inOfs  = *rxInOfsFp;
    WORD outOfs = *rxOutOfsFp;

    if (inOfs >= outOfs)
       return (inOfs - outOfs) / sizeof(RX_ELEMENT);
    return (NUM_RX_BUF - (outOfs - inOfs) / sizeof(RX_ELEMENT));
  }

  PUBLIC DWORD PktRxDropped (void)
  {
    return (*(DWORD _far*)(protBase + (WORD)&pktDrop));
  }

#elif (DOSX & DJGPP)
  PUBLIC int PktReceive (BYTE *buf, int max)
  {
    WORD ofs = _farpeekw (_dos_ds, realBase+rxOutOfs);

    if (ofs != _farpeekw (_dos_ds, realBase+rxInOfs))
    {
      RX_ELEMENT head;
      int  size, len = max;

      head.firstCount  = _farpeekw (_dos_ds, realBase+ofs);
      head.secondCount = _farpeekw (_dos_ds, realBase+ofs+2);
      head.handle      = _farpeekw (_dos_ds, realBase+ofs+4);

      if (CheckElement(&head))
      {
        size = min (head.firstCount, sizeof(RX_ELEMENT));
        len  = min (size, max);
        dosmemget (realBase+ofs+6, len, buf);
      }
      else
        size = -1;

      ofs += sizeof (RX_ELEMENT);
      if (ofs > LAST_RX_BUF)
           _farpokew (_dos_ds, realBase+rxOutOfs, FIRST_RX_BUF);
      else _farpokew (_dos_ds, realBase+rxOutOfs, ofs);
      return (size);
    }
    return (0);
  }

  PUBLIC void PktQueueBusy (BOOL busy)
  {
    WORD ofs;

    disable();
    ofs = _farpeekw (_dos_ds, realBase+rxInOfs);
    if (busy)
       ofs += sizeof (RX_ELEMENT);

    if (ofs > LAST_RX_BUF)
         _farpokew (_dos_ds, realBase+rxOutOfs, FIRST_RX_BUF);
    else _farpokew (_dos_ds, realBase+rxOutOfs, ofs);
    _farpokel (_dos_ds, realBase+pktDrop, 0UL);
    enable();
  }

  PUBLIC WORD PktBuffersUsed (void)
  {
    WORD inOfs, outOfs;

    disable();
    inOfs  = _farpeekw (_dos_ds, realBase+rxInOfs);
    outOfs = _farpeekw (_dos_ds, realBase+rxOutOfs);
    enable();
    if (inOfs >= outOfs)
       return (inOfs - outOfs) / sizeof(RX_ELEMENT);
    return (NUM_RX_BUF - (outOfs - inOfs) / sizeof(RX_ELEMENT));
  }

  PUBLIC DWORD PktRxDropped (void)
  {
    return _farpeekl (_dos_ds, realBase+pktDrop);
  }

#elif (DOSX & DOS4GW)
  PUBLIC int PktReceive (BYTE *buf, int max)
  {
    WORD ofs = *(WORD*) (realBase+rxOutOfs);

    if (ofs != *(WORD*) (realBase+rxInOfs))
    {
      RX_ELEMENT head;
      int  size, len = max;

      head.firstCount  = *(WORD*) (realBase+ofs);
      head.secondCount = *(WORD*) (realBase+ofs+2);
      head.handle      = *(WORD*) (realBase+ofs+4);

      if (CheckElement(&head))
      {
        size = min (head.firstCount, sizeof(RX_ELEMENT));
        len  = min (size, max);
        memcpy (buf, (const void*)(realBase+ofs+6), len);
      }
      else
        size = -1;

      ofs += sizeof (RX_ELEMENT);
      if (ofs > LAST_RX_BUF)
           *(WORD*) (realBase+rxOutOfs) = FIRST_RX_BUF;
      else *(WORD*) (realBase+rxOutOfs) = ofs;
      return (size);
    }
    return (0);
  }

  PUBLIC void PktQueueBusy (BOOL busy)
  {
    WORD ofs;

    _disable();
    ofs = *(WORD*) (realBase+rxInOfs);
    if (busy)
       ofs += sizeof (RX_ELEMENT);

    if (ofs > LAST_RX_BUF)
         *(WORD*) (realBase+rxOutOfs) = FIRST_RX_BUF;
    else *(WORD*) (realBase+rxOutOfs) = ofs;
    *(DWORD*) (realBase+pktDrop) = 0UL;
    _enable();
  }

  PUBLIC WORD PktBuffersUsed (void)
  {
    WORD inOfs, outOfs;

    _disable();
    inOfs  = *(WORD*) (realBase+rxInOfs);
    outOfs = *(WORD*) (realBase+rxOutOfs);
    _enable();
    if (inOfs >= outOfs)
       return (inOfs - outOfs) / sizeof(RX_ELEMENT);
    return (NUM_RX_BUF - (outOfs - inOfs) / sizeof(RX_ELEMENT));
  }

  PUBLIC DWORD PktRxDropped (void)
  {
    return *(DWORD*) (realBase+pktDrop);
  }

#else     /* real-mode small/large model */

  PUBLIC int PktReceive (BYTE *buf, int max)
  {
    if (rxOutOfs != rxInOfs)
    {
      RX_ELEMENT far *head = (RX_ELEMENT far*) MK_FP (_DS,rxOutOfs);
      int  size, len = max;

      if (CheckElement(head))
      {
        size = min (head->firstCount, sizeof(RX_ELEMENT));
        len  = min (size, max);
        _fmemcpy (buf, &head->destin, len);
      }
      else
        size = -1;

      rxOutOfs += sizeof (RX_ELEMENT);
      if (rxOutOfs > LAST_RX_BUF)
          rxOutOfs = FIRST_RX_BUF;
      return (size);
    }
    return (0);
  }

  PUBLIC void PktQueueBusy (BOOL busy)
  {
    rxOutOfs = busy ? (rxInOfs + sizeof(RX_ELEMENT)) : rxInOfs;
    if (rxOutOfs > LAST_RX_BUF)
        rxOutOfs = FIRST_RX_BUF;
    pktDrop = 0L;
  }

  PUBLIC WORD PktBuffersUsed (void)
  {
    WORD inOfs  = rxInOfs;
    WORD outOfs = rxOutOfs;

    if (inOfs >= outOfs)
       return ((inOfs - outOfs) / sizeof(RX_ELEMENT));
    return (NUM_RX_BUF - (outOfs - inOfs) / sizeof(RX_ELEMENT));
  }

  PUBLIC DWORD PktRxDropped (void)
  {
    return (pktDrop);
  }
#endif

/**************************************************************************/

LOCAL __inline void PktFreeMem (void)
{
#if (DOSX & PHARLAP)
  if (realSeg)
  {
    _dx_real_free (realSeg);
    realSeg = 0;
  }
#elif (DOSX & DJGPP)
  if (rm_mem.rm_segment)
  {
    unsigned ofs;  /* clear the DOS-mem to prevent further upcalls */

    for (ofs = 0; ofs < 16 * rm_mem.size / 4; ofs += 4)
       _farpokel (_dos_ds, realBase + ofs, 0);
    _go32_dpmi_free_dos_memory (&rm_mem);
    rm_mem.rm_segment = 0;
  }
#elif (DOSX & DOS4GW)
  if (rm_base_sel)
  {
    dpmi_real_free (rm_base_sel);
    rm_base_sel = 0;
  }
#endif
}

/**************************************************************************/

PUBLIC BOOL PktExitDriver (void)
{
  if (pktInfo.handle)
  {
    if (!PktSetReceiverMode(PDRX_BROADCAST))
       PUTS ("Error restoring receiver mode.");

    if (!PktReleaseHandle(pktInfo.handle))
       PUTS ("Error releasing PKT-DRVR handle.");

    PktFreeMem();
    pktInfo.handle = 0;
  }

  if (pcap_pkt_debug >= 1)
     printf ("Internal stats: too-small %lu, too-large %lu, bad-sync %lu, "
             "wrong-handle %lu\n",
             intStat.tooSmall, intStat.tooLarge,
             intStat.badSync, intStat.wrongHandle);
  return (TRUE);
}

#if (DOSX & (DJGPP|DOS4GW))
static void dump_pkt_stub (void)
{
  int i;

  fprintf (stderr, "PktReceiver %lu, pkt_stub[PktReceiver] =\n",
           PktReceiver);
  for (i = 0; i < 15; i++)
      fprintf (stderr, "%02X, ", real_stub_array[i+PktReceiver]);
  fputs ("\n", stderr);
}
#endif

/*
 * Front end initialization routine
 */
PUBLIC BOOL PktInitDriver (PKT_RX_MODE mode)
{
  PKT_RX_MODE rxMode;
  BOOL   writeInfo = (pcap_pkt_debug >= 3);

  pktInfo.quiet = (pcap_pkt_debug < 3);

#if (DOSX & PHARLAP) && defined(__HIGHC__)
  if (_mwenv != 2)
  {
    fprintf (stderr, "Only Pharlap DOS extender supported.\n");
    return (FALSE);
  }
#endif

#if (DOSX & PHARLAP) && defined(__WATCOMC__)
  if (_Extender != 1)
  {
    fprintf (stderr, "Only DOS4GW style extenders supported.\n");
    return (FALSE);
  }
#endif

  if (!PktSearchDriver())
  {
    PUTS ("Packet driver not found.");
    PktFreeMem();
    return (FALSE);
  }

  if (!PktGetDriverInfo())
  {
    PUTS ("Error getting pkt-drvr information.");
    PktFreeMem();
    return (FALSE);
  }

#if (DOSX & PHARLAP)
  if (RealCopy((ULONG)&rxOutOfs, (ULONG)&pktRxEnd,
               &realBase, &protBase, (USHORT*)&realSeg))
  {
    rxOutOfsFp  = (WORD _far *) (protBase + (WORD) &rxOutOfs);
    rxInOfsFp   = (WORD _far *) (protBase + (WORD) &rxInOfs);
    *rxOutOfsFp = FIRST_RX_BUF;
    *rxInOfsFp  = FIRST_RX_BUF;
  }
  else
  {
    PUTS ("Cannot allocate real-mode stub.");
    return (FALSE);
  }

#elif (DOSX & (DJGPP|DOS4GW))
  if (sizeof(real_stub_array) > 0xFFFF)
  {
    fprintf (stderr, "`real_stub_array[]' too big.\n");
    return (FALSE);
  }
#if (DOSX & DJGPP)
  rm_mem.size = (sizeof(real_stub_array) + 15) / 16;

  if (_go32_dpmi_allocate_dos_memory(&rm_mem) || rm_mem.rm_offset != 0)
  {
    PUTS ("real-mode init failed.");
    return (FALSE);
  }
  realBase = (rm_mem.rm_segment << 4);
  dosmemput (&real_stub_array, sizeof(real_stub_array), realBase);
  _farpokel (_dos_ds, realBase+rxOutOfs, FIRST_RX_BUF);
  _farpokel (_dos_ds, realBase+rxInOfs,  FIRST_RX_BUF);

#elif (DOSX & DOS4GW)
  rm_base_seg = dpmi_real_malloc (sizeof(real_stub_array), &rm_base_sel);
  if (!rm_base_seg)
  {
    PUTS ("real-mode init failed.");
    return (FALSE);
  }
  realBase = (rm_base_seg << 4);
  memcpy ((void*)realBase, &real_stub_array, sizeof(real_stub_array));
  *(WORD*) (realBase+rxOutOfs) = FIRST_RX_BUF;
  *(WORD*) (realBase+rxInOfs)  = FIRST_RX_BUF;

#endif
  {
    int pushf = PktReceiver;

    while (real_stub_array[pushf++] != 0x9C &&    /* pushf */
           real_stub_array[pushf]   != 0xFA)      /* cli   */
    {
      if (++para_skip > 16)
      {
        fprintf (stderr, "Something wrong with `pkt_stub.inc'.\n");
        para_skip = 0;
        dump_pkt_stub();
        return (FALSE);
      }
    }
    if (*(WORD*)(real_stub_array + offsetof(PktRealStub,_dummy)) != 0xB800)
    {
      fprintf (stderr, "`real_stub_array[]' is misaligned.\n");
      return (FALSE);
    }
  }

  if (pcap_pkt_debug > 2)
      dump_pkt_stub();

#else
  rxOutOfs = FIRST_RX_BUF;
  rxInOfs  = FIRST_RX_BUF;
#endif

  if (!PktSetAccess())
  {
    PUTS ("Error setting pkt-drvr access.");
    PktFreeMem();
    return (FALSE);
  }

  if (!PktGetAddress(&myAddress))
  {
    PUTS ("Error fetching adapter address.");
    PktFreeMem();
    return (FALSE);
  }

  if (!PktSetReceiverMode(mode))
  {
    PUTS ("Error setting receiver mode.");
    PktFreeMem();
    return (FALSE);
  }

  if (!PktGetReceiverMode(&rxMode))
  {
    PUTS ("Error getting receiver mode.");
    PktFreeMem();
    return (FALSE);
  }

  if (writeInfo)
     printf ("Pkt-driver information:\n"
             "  Version  : %d.%d\n"
             "  Name     : %.15s\n"
             "  Class    : %u (%s)\n"
             "  Type     : %u\n"
             "  Number   : %u\n"
             "  Funcs    : %u\n"
             "  Intr     : %Xh\n"
             "  Handle   : %u\n"
             "  Extended : %s\n"
             "  Hi-perf  : %s\n"
             "  RX mode  : %s\n"
             "  Eth-addr : %02X:%02X:%02X:%02X:%02X:%02X\n",

             pktInfo.majVer, pktInfo.minVer, pktInfo.name,
             pktInfo.class,  PktGetClassName(pktInfo.class),
             pktInfo.type,   pktInfo.number,
             pktInfo.funcs,  pktInfo.intr,   pktInfo.handle,
             pktInfo.funcs == 2 || pktInfo.funcs == 6 ? "Yes" : "No",
             pktInfo.funcs == 5 || pktInfo.funcs == 6 ? "Yes" : "No",
             PktRXmodeStr(rxMode),
             myAddress[0], myAddress[1], myAddress[2],
             myAddress[3], myAddress[4], myAddress[5]);

#if defined(DEBUG) && (DOSX & PHARLAP)
  if (writeInfo)
  {
    DWORD    rAdr = realBase + (WORD)&PktReceiver;
    unsigned sel, ofs;

    printf ("\nReceiver at   %04X:%04X\n", RP_SEG(rAdr),    RP_OFF(rAdr));
    printf ("Realbase    = %04X:%04X\n",   RP_SEG(realBase),RP_OFF(realBase));

    sel = _FP_SEG (protBase);
    ofs = _FP_OFF (protBase);
    printf ("Protbase    = %04X:%08X\n", sel,ofs);
    printf ("RealSeg     = %04X\n", realSeg);

    sel = _FP_SEG (rxOutOfsFp);
    ofs = _FP_OFF (rxOutOfsFp);
    printf ("rxOutOfsFp  = %04X:%08X\n", sel,ofs);

    sel = _FP_SEG (rxInOfsFp);
    ofs = _FP_OFF (rxInOfsFp);
    printf ("rxInOfsFp   = %04X:%08X\n", sel,ofs);

    printf ("Ready: *rxOutOfsFp = %04X *rxInOfsFp = %04X\n",
            *rxOutOfsFp, *rxInOfsFp);

    PktQueueBusy (TRUE);
    printf ("Busy:  *rxOutOfsFp = %04X *rxInOfsFp = %04X\n",
            *rxOutOfsFp, *rxInOfsFp);
  }
#endif

  memset (&pktStat, 0, sizeof(pktStat));  /* clear statistics */
  PktQueueBusy (TRUE);
  return (TRUE);
}


/*
 * DPMI functions only for Watcom + DOS4GW extenders
 */
#if (DOSX & DOS4GW)
LOCAL DWORD dpmi_get_real_vector (int intr)
{
  union REGS r;

  r.x.eax = 0x200;
  r.x.ebx = (DWORD) intr;
  int386 (0x31, &r, &r);
  return ((r.w.cx << 4) + r.w.dx);
}

LOCAL WORD dpmi_real_malloc (int size, WORD *selector)
{
  union REGS r;

  r.x.eax = 0x0100;             /* DPMI allocate DOS memory */
  r.x.ebx = (size + 15) / 16;   /* Number of paragraphs requested */
  int386 (0x31, &r, &r);
  if (r.w.cflag & 1)
     return (0);

  *selector = r.w.dx;
  return (r.w.ax);              /* Return segment address */
}

LOCAL void dpmi_real_free (WORD selector)
{
  union REGS r;

  r.x.eax = 0x101;              /* DPMI free DOS memory */
  r.x.ebx = selector;           /* Selector to free */
  int386 (0x31, &r, &r);
}
#endif


#if defined(DOSX) && (DOSX & PHARLAP)
/*
 * Description:
 *     This routine allocates conventional memory for the specified block
 *     of code (which must be within the first 64K of the protected mode
 *     program segment) and copies the code to it.
 *
 *     The caller should free up the conventional memory block when it
 *     is done with the conventional memory.
 *
 *     NOTE THIS ROUTINE REQUIRES 386|DOS-EXTENDER 3.0 OR LATER.
 *
 * Calling arguments:
 *     start_offs      start of real mode code in program segment
 *     end_offs        1 byte past end of real mode code in program segment
 *     real_basep      returned;  real mode ptr to use as a base for the
 *                        real mode code (eg, to get the real mode FAR
 *                        addr of a function foo(), take
 *                        real_basep + (ULONG) foo).
 *                        This pointer is constructed such that
 *                        offsets within the real mode segment are
 *                        the same as the link-time offsets in the
 *                        protected mode program segment
 *     prot_basep      returned;  prot mode ptr to use as a base for getting
 *                        to the conventional memory, also constructed
 *                        so that adding the prot mode offset of a
 *                        function or variable to the base gets you a
 *                        ptr to the function or variable in the
 *                        conventional memory block.
 *     rmem_adrp       returned;  real mode para addr of allocated
 *                        conventional memory block, to be used to free
 *                        up the conventional memory when done.  DO NOT
 *                        USE THIS TO CONSTRUCT A REAL MODE PTR, USE
 *                        REAL_BASEP INSTEAD SO THAT OFFSETS WORK OUT
 *                        CORRECTLY.
 *
 * Returned values:
 *     0      if error
 *     1      if success
 */
int RealCopy (ULONG    start_offs,
              ULONG    end_offs,
              REALPTR *real_basep,
              FARPTR  *prot_basep,
              USHORT  *rmem_adrp)
{
  ULONG   rm_base;    /* base real mode para addr for accessing */
                      /* allocated conventional memory          */
  UCHAR  *source;     /* source pointer for copy                */
  FARPTR  destin;     /* destination pointer for copy           */
  ULONG   len;        /* number of bytes to copy                */
  ULONG   temp;
  USHORT  stemp;

  /* First check for valid inputs
   */
  if (start_offs >= end_offs || end_offs > 0x10000)
     return (FALSE);

  /* Round start_offs down to a paragraph (16-byte) boundary so we can set up
   * the real mode pointer easily. Round up end_offs to make sure we allocate
   * enough paragraphs
   */
  start_offs &= ~15;
  end_offs = (15 + (end_offs << 4)) >> 4;

  /* Allocate the conventional memory for our real mode code.  Remember to
   * round byte count UP to 16-byte paragraph size.  We alloc it
   * above the DOS data buffer so both the DOS data buffer and the appl
   * conventional mem block can still be resized.
   *
   * First just try to alloc it;  if we can't get it, shrink the appl mem
   * block down to the minimum, try to alloc the memory again, then grow the
   * appl mem block back to the maximum.  (Don't try to shrink the DOS data
   * buffer to free conventional memory;  it wouldn't be good for this routine
   * to have the possible side effect of making file I/O run slower.)
   */
  len = ((end_offs - start_offs) + 15) >> 4;
  if (_dx_real_above(len, rmem_adrp, &stemp) != _DOSE_NONE)
  {
    if (_dx_cmem_usage(0, 0, &temp, &temp) != _DOSE_NONE)
       return (FALSE);

    if (_dx_real_above(len, rmem_adrp, &stemp) != _DOSE_NONE)
       *rmem_adrp = 0;

    if (_dx_cmem_usage(0, 1, &temp, &temp) != _DOSE_NONE)
    {
      if (*rmem_adrp != 0)
         _dx_real_free (*rmem_adrp);
      return (FALSE);
    }

    if (*rmem_adrp == 0)
       return (FALSE);
  }

  /* Construct real mode & protected mode pointers to access the allocated
   * memory.  Note we know start_offs is aligned on a paragraph (16-byte)
   * boundary, because we rounded it down.
   *
   * We make the offsets come out rights by backing off the real mode selector
   * by start_offs.
   */
  rm_base = ((ULONG) *rmem_adrp) - (start_offs >> 4);
  RP_SET (*real_basep, 0, rm_base);
  FP_SET (*prot_basep, rm_base << 4, SS_DOSMEM);

  /* Copy the real mode code/data to the allocated memory
   */
  source = (UCHAR *) start_offs;
  destin = *prot_basep;
  FP_SET (destin, FP_OFF(*prot_basep) + start_offs, FP_SEL(*prot_basep));
  len = end_offs - start_offs;
  WriteFarMem (destin, source, len);

  return (TRUE);
}
#endif /* DOSX && (DOSX & PHARLAP) */
