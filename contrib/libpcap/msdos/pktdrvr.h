#ifndef __PKTDRVR_H
#define __PKTDRVR_H

#define PUBLIC
#define LOCAL        static

#define RX_BUF_SIZE  ETH_MTU   /* buffer size variables. NB !! */
#define TX_BUF_SIZE  ETH_MTU   /* must be same as in pkt_rx*.* */

#ifdef __HIGHC__
#pragma Off(Align_members)
#else
#pragma pack(1)
#endif

typedef enum  {                /* Packet-driver classes */
        PD_ETHER      = 1,
        PD_PRONET10   = 2,
        PD_IEEE8025   = 3,
        PD_OMNINET    = 4,
        PD_APPLETALK  = 5,
        PD_SLIP       = 6,
        PD_STARTLAN   = 7,
        PD_ARCNET     = 8,
        PD_AX25       = 9,
        PD_KISS       = 10,
        PD_IEEE8023_2 = 11,
        PD_FDDI8022   = 12,
        PD_X25        = 13,
        PD_LANstar    = 14,
        PD_PPP        = 18
      } PKT_CLASS;

typedef enum  {             /* Packet-driver receive modes    */
        PDRX_OFF    = 1,    /* turn off receiver              */
        PDRX_DIRECT,        /* receive only to this interface */
        PDRX_BROADCAST,     /* DIRECT + broadcast packets     */
        PDRX_MULTICAST1,    /* BROADCAST + limited multicast  */
        PDRX_MULTICAST2,    /* BROADCAST + all multicast      */
        PDRX_ALL_PACKETS,   /* receive all packets on network */
      } PKT_RX_MODE;

typedef struct {
        char type[8];
        char len;
      } PKT_FRAME;


typedef struct {
        BYTE  class;        /* = 1 for DEC/Interl/Xerox Ethernet */
        BYTE  number;       /* = 0 for single LAN adapter        */
        WORD  type;         /* = 13 for 3C523                    */
        BYTE  funcs;        /* Basic/Extended/HiPerf functions   */
        WORD  intr;         /* user interrupt vector number      */
        WORD  handle;       /* Handle associated with session    */
        BYTE  name [15];    /* Name of adapter interface,ie.3C523*/
        BOOL  quiet;        /* (don't) print errors to stdout    */
        const char *error;  /* address of error string           */
        BYTE  majVer;       /* Major driver implementation ver.  */
        BYTE  minVer;       /* Minor driver implementation ver.  */
        BYTE  dummyLen;     /* length of following data          */
        WORD  MAClength;    /* HiPerformance data, N/A           */
        WORD  MTU;          /* HiPerformance data, N/A           */
        WORD  multicast;    /* HiPerformance data, N/A           */
        WORD  rcvrBuffers;  /* valid for                         */
        WORD  UMTbufs;      /*   High Performance drivers only   */
        WORD  postEOIintr;  /*                  Usage ??         */
      } PKT_INFO;

#define PKT_PARAM_SIZE  14    /* members majVer - postEOIintr */


typedef struct {
        DWORD inPackets;          /* # of packets received    */
        DWORD outPackets;         /* # of packets transmitted */
        DWORD inBytes;            /* # of bytes received      */
        DWORD outBytes;           /* # of bytes transmitted   */
        DWORD inErrors;           /* # of reception errors    */
        DWORD outErrors;          /* # of transmission errors */
        DWORD lost;               /* # of packets lost (RX)   */
      } PKT_STAT;


typedef struct {
        ETHER destin;
        ETHER source;
        WORD  proto;
        BYTE  data [TX_BUF_SIZE];
      } TX_ELEMENT;

typedef struct {
        WORD  firstCount;         /* # of bytes on 1st         */
        WORD  secondCount;        /* and 2nd upcall            */
        WORD  handle;             /* instance that upcalled    */
        ETHER destin;             /* E-net destination address */
        ETHER source;             /* E-net source address      */
        WORD  proto;              /* protocol number           */
        BYTE  data [RX_BUF_SIZE];
      } RX_ELEMENT;


#ifdef __HIGHC__
#pragma pop(Align_members)
#else
#pragma pack()
#endif


/*
 * Prototypes for publics
 */

#ifdef __cplusplus
extern "C" {
#endif

extern PKT_STAT    pktStat;     /* statistics for packets */
extern PKT_INFO    pktInfo;     /* packet-driver information */

extern PKT_RX_MODE receiveMode;
extern ETHER       myAddress, ethBroadcast;

extern BOOL  PktInitDriver (PKT_RX_MODE mode);
extern BOOL  PktExitDriver (void);

extern const char *PktGetErrorStr    (int errNum);
extern const char *PktGetClassName   (WORD class);
extern const char *PktRXmodeStr      (PKT_RX_MODE mode);
extern BOOL        PktSearchDriver   (void);
extern int         PktReceive        (BYTE *buf, int max);
extern BOOL        PktTransmit       (const void *eth, int len);
extern DWORD       PktRxDropped      (void);
extern BOOL        PktReleaseHandle  (WORD handle);
extern BOOL        PktTerminHandle   (WORD handle);
extern BOOL        PktResetInterface (WORD handle);
extern BOOL        PktSetReceiverMode(PKT_RX_MODE  mode);
extern BOOL        PktGetReceiverMode(PKT_RX_MODE *mode);
extern BOOL        PktGetStatistics  (WORD handle);
extern BOOL        PktSessStatistics (WORD handle);
extern BOOL        PktResetStatistics(WORD handle);
extern BOOL        PktGetAddress     (ETHER *addr);
extern BOOL        PktSetAddress     (const ETHER *addr);
extern BOOL        PktGetDriverInfo  (void);
extern BOOL        PktGetDriverParam (void);
extern void        PktQueueBusy      (BOOL busy);
extern WORD        PktBuffersUsed    (void);

#ifdef __cplusplus
}
#endif

#endif /* __PKTDRVR_H */

