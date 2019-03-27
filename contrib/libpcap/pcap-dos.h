/*
 * Internal details for libpcap on DOS.
 * 32-bit targets: djgpp, Pharlap or DOS4GW.
 */

#ifndef __PCAP_DOS_H
#define __PCAP_DOS_H

#ifdef __DJGPP__
#include <pc.h>    /* simple non-conio kbhit */
#else
#include <conio.h>
#endif

typedef int            BOOL;
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef BYTE           ETHER[6];

#define ETH_ALEN       sizeof(ETHER)   /* Ether address length */
#define ETH_HLEN       (2*ETH_ALEN+2)  /* Ether header length  */
#define ETH_MTU        1500
#define ETH_MIN        60
#define ETH_MAX        (ETH_MTU+ETH_HLEN)

#ifndef TRUE
  #define TRUE   1
  #define FALSE  0
#endif

#define PHARLAP  1
#define DJGPP    2
#define DOS4GW   4

#ifdef __DJGPP__
  #undef  DOSX
  #define DOSX DJGPP
#endif

#ifdef __WATCOMC__
  #undef  DOSX
  #define DOSX DOS4GW
#endif

#ifdef __HIGHC__
  #include <pharlap.h>
  #undef  DOSX
  #define DOSX PHARLAP
  #define inline
#else
  typedef unsigned int UINT;
#endif


#if defined(__GNUC__) || defined(__HIGHC__)
  typedef unsigned long long  uint64;
  typedef unsigned long long  QWORD;
#endif

#if defined(__WATCOMC__)
  typedef unsigned __int64  uint64;
  typedef unsigned __int64  QWORD;
#endif

#define ARGSUSED(x)  (void) x

#if defined (__SMALL__) || defined(__LARGE__)
  #define DOSX 0

#elif !defined(DOSX)
  #error DOSX not defined; 1 = PharLap, 2 = djgpp, 4 = DOS4GW
#endif

#ifdef __HIGHC__
#define min(a,b) _min(a,b)
#define max(a,b) _max(a,b)
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a) < (b) ? (b) : (a))
#endif

#if !defined(_U_) && defined(__GNUC__)
#define _U_  __attribute__((unused))
#endif

#ifndef _U_
#define _U_
#endif

#if defined(USE_32BIT_DRIVERS)
  #include "msdos/pm_drvr/lock.h"

  #ifndef RECEIVE_QUEUE_SIZE
  #define RECEIVE_QUEUE_SIZE  60
  #endif

  #ifndef RECEIVE_BUF_SIZE
  #define RECEIVE_BUF_SIZE   (ETH_MAX+20)
  #endif

  extern struct device el2_dev     LOCKED_VAR;  /* 3Com EtherLink II */
  extern struct device el3_dev     LOCKED_VAR;  /*      EtherLink III */
  extern struct device tc59_dev    LOCKED_VAR;  /* 3Com Vortex Card (?) */
  extern struct device tc515_dev   LOCKED_VAR;
  extern struct device tc90x_dev   LOCKED_VAR;
  extern struct device tc90bcx_dev LOCKED_VAR;
  extern struct device wd_dev      LOCKED_VAR;
  extern struct device ne_dev      LOCKED_VAR;
  extern struct device acct_dev    LOCKED_VAR;
  extern struct device cs89_dev    LOCKED_VAR;
  extern struct device rtl8139_dev LOCKED_VAR;

  struct rx_ringbuf {
         volatile int in_index;   /* queue index head */
         int          out_index;  /* queue index tail */
         int          elem_size;  /* size of each element */
         int          num_elem;   /* number of elements */
         char        *buf_start;  /* start of buffer pool */
       };

  struct rx_elem {
         DWORD size;              /* size copied to this element */
         BYTE  data[ETH_MAX+10];  /* add some margin. data[0] should be */
       };                         /* dword aligned */

  extern BYTE *get_rxbuf     (int len) LOCKED_FUNC;
  extern int   peek_rxbuf    (BYTE **buf);
  extern int   release_rxbuf (BYTE  *buf);

#else
  #define LOCKED_VAR
  #define LOCKED_FUNC

  struct device {
         const char *name;
         const char *long_name;
         DWORD  base_addr;      /* device I/O address       */
         int    irq;            /* device IRQ number        */
         int    dma;            /* DMA channel              */
         DWORD  mem_start;      /* shared mem start         */
         DWORD  mem_end;        /* shared mem end           */
         DWORD  rmem_start;     /* shmem "recv" start       */
         DWORD  rmem_end;       /* shared "recv" end        */

         struct device *next;   /* next device in list      */

         /* interface service routines */
         int   (*probe)(struct device *dev);
         int   (*open) (struct device *dev);
         void  (*close)(struct device *dev);
         int   (*xmit) (struct device *dev, const void *buf, int len);
         void *(*get_stats)(struct device *dev);
         void  (*set_multicast_list)(struct device *dev);

         /* driver-to-pcap receive buffer routines */
         int   (*copy_rx_buf) (BYTE *buf, int max); /* rx-copy (pktdrvr only) */
         BYTE *(*get_rx_buf) (int len);             /* rx-buf fetch/enqueue */
         int   (*peek_rx_buf) (BYTE **buf);         /* rx-non-copy at queue */
         int   (*release_rx_buf) (BYTE *buf);       /* release after peek */

         WORD   flags;          /* Low-level status flags. */
         void  *priv;           /* private data */
       };

  /*
   * Network device statistics
   */
  typedef struct net_device_stats {
          DWORD  rx_packets;            /* total packets received       */
          DWORD  tx_packets;            /* total packets transmitted    */
          DWORD  rx_bytes;              /* total bytes received         */
          DWORD  tx_bytes;              /* total bytes transmitted      */
          DWORD  rx_errors;             /* bad packets received         */
          DWORD  tx_errors;             /* packet transmit problems     */
          DWORD  rx_dropped;            /* no space in Rx buffers       */
          DWORD  tx_dropped;            /* no space available for Tx    */
          DWORD  multicast;             /* multicast packets received   */

          /* detailed rx_errors: */
          DWORD  rx_length_errors;
          DWORD  rx_over_errors;        /* recv'r overrun error         */
          DWORD  rx_osize_errors;       /* recv'r over-size error       */
          DWORD  rx_crc_errors;         /* recv'd pkt with crc error    */
          DWORD  rx_frame_errors;       /* recv'd frame alignment error */
          DWORD  rx_fifo_errors;        /* recv'r fifo overrun          */
          DWORD  rx_missed_errors;      /* recv'r missed packet         */

          /* detailed tx_errors */
          DWORD  tx_aborted_errors;
          DWORD  tx_carrier_errors;
          DWORD  tx_fifo_errors;
          DWORD  tx_heartbeat_errors;
          DWORD  tx_window_errors;
          DWORD  tx_collisions;
          DWORD  tx_jabbers;
        } NET_STATS;
#endif

extern struct device       *active_dev  LOCKED_VAR;
extern const struct device *dev_base    LOCKED_VAR;
extern struct device       *probed_dev;

extern int pcap_pkt_debug;

extern void _w32_os_yield (void); /* Watt-32's misc.c */

#ifdef NDEBUG
  #define PCAP_ASSERT(x) ((void)0)

#else
  void pcap_assert (const char *what, const char *file, unsigned line);

  #define PCAP_ASSERT(x) do { \
                           if (!(x)) \
                              pcap_assert (#x, __FILE__, __LINE__); \
                         } while (0)
#endif

#endif  /* __PCAP_DOS_H */
