/*
 *  This file is part of DOS-libpcap
 *  Ported to DOS/DOSX by G. Vanem <gvanem@yahoo.no>
 *
 *  pcap-dos.c: Interface to PKTDRVR, NDIS2 and 32-bit pmode
 *              network drivers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <float.h>
#include <fcntl.h>
#include <io.h>

#if defined(USE_32BIT_DRIVERS)
  #include "msdos/pm_drvr/pmdrvr.h"
  #include "msdos/pm_drvr/pci.h"
  #include "msdos/pm_drvr/bios32.h"
  #include "msdos/pm_drvr/module.h"
  #include "msdos/pm_drvr/3c501.h"
  #include "msdos/pm_drvr/3c503.h"
  #include "msdos/pm_drvr/3c509.h"
  #include "msdos/pm_drvr/3c59x.h"
  #include "msdos/pm_drvr/3c515.h"
  #include "msdos/pm_drvr/3c90x.h"
  #include "msdos/pm_drvr/3c575_cb.h"
  #include "msdos/pm_drvr/ne.h"
  #include "msdos/pm_drvr/wd.h"
  #include "msdos/pm_drvr/accton.h"
  #include "msdos/pm_drvr/cs89x0.h"
  #include "msdos/pm_drvr/rtl8139.h"
  #include "msdos/pm_drvr/ne2k-pci.h"
#endif

#include "pcap.h"
#include "pcap-dos.h"
#include "pcap-int.h"
#include "msdos/pktdrvr.h"

#ifdef USE_NDIS2
#include "msdos/ndis2.h"
#endif

#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_ether.h>
#include <net/if_packe.h>
#include <tcp.h>

#if defined(USE_32BIT_DRIVERS)
  #define FLUSHK()       do { _printk_safe = 1; _printk_flush(); } while (0)
  #define NDIS_NEXT_DEV  &rtl8139_dev

  static char *rx_pool = NULL;
  static void init_32bit (void);

  static int  pktq_init     (struct rx_ringbuf *q, int size, int num, char *pool);
  static int  pktq_check    (struct rx_ringbuf *q);
  static int  pktq_inc_out  (struct rx_ringbuf *q);
  static int  pktq_in_index (struct rx_ringbuf *q) LOCKED_FUNC;
  static void pktq_clear    (struct rx_ringbuf *q) LOCKED_FUNC;

  static struct rx_elem *pktq_in_elem  (struct rx_ringbuf *q) LOCKED_FUNC;
  static struct rx_elem *pktq_out_elem (struct rx_ringbuf *q);

#else
  #define FLUSHK()      ((void)0)
  #define NDIS_NEXT_DEV  NULL
#endif

/*
 * Internal variables/functions in Watt-32
 */
extern WORD  _pktdevclass;
extern BOOL  _eth_is_init;
extern int   _w32_dynamic_host;
extern int   _watt_do_exit;
extern int   _watt_is_init;
extern int   _w32__bootp_on, _w32__dhcp_on, _w32__rarp_on, _w32__do_mask_req;
extern void (*_w32_usr_post_init) (void);
extern void (*_w32_print_hook)();

extern void dbug_write (const char *);  /* Watt-32 lib, pcdbug.c */
extern int  pkt_get_mtu (void);

static int ref_count = 0;

static u_long mac_count    = 0;
static u_long filter_count = 0;

static volatile BOOL exc_occured = 0;

static struct device *handle_to_device [20];

static int  pcap_activate_dos (pcap_t *p);
static int  pcap_read_dos (pcap_t *p, int cnt, pcap_handler callback,
                           u_char *data);
static void pcap_cleanup_dos (pcap_t *p);
static int  pcap_stats_dos (pcap_t *p, struct pcap_stat *ps);
static int  pcap_sendpacket_dos (pcap_t *p, const void *buf, size_t len);
static int  pcap_setfilter_dos (pcap_t *p, struct bpf_program *fp);

static int  ndis_probe (struct device *dev);
static int  pkt_probe  (struct device *dev);

static void close_driver (void);
static int  init_watt32 (struct pcap *pcap, const char *dev_name, char *err_buf);
static int  first_init (const char *name, char *ebuf, int promisc);

static void watt32_recv_hook (u_char *dummy, const struct pcap_pkthdr *pcap,
                              const u_char *buf);

/*
 * These are the device we always support
 */
static struct device ndis_dev = {
              "ndis",
              "NDIS2 LanManager",
              0,
              0,0,0,0,0,0,
              NDIS_NEXT_DEV,  /* NULL or a 32-bit device */
              ndis_probe
            };

static struct device pkt_dev = {
              "pkt",
              "Packet-Driver",
              0,
              0,0,0,0,0,0,
              &ndis_dev,
              pkt_probe
            };

static struct device *get_device (int fd)
{
  if (fd <= 0 || fd >= sizeof(handle_to_device)/sizeof(handle_to_device[0]))
     return (NULL);
  return handle_to_device [fd-1];
}

/*
 * Private data for capturing on MS-DOS.
 */
struct pcap_dos {
	void (*wait_proc)(void); /* call proc while waiting */
	struct pcap_stat stat;
};

pcap_t *pcap_create_interface (const char *device _U_, char *ebuf)
{
	pcap_t *p;

	p = pcap_create_common(ebuf, sizeof (struct pcap_dos));
	if (p == NULL)
		return (NULL);

	p->activate_op = pcap_activate_dos;
	return (p);
}

/*
 * Open MAC-driver with name 'device_name' for live capture of
 * network packets.
 */
static int pcap_activate_dos (pcap_t *pcap)
{
  if (pcap->opt.rfmon) {
    /*
     * No monitor mode on DOS.
     */
    return (PCAP_ERROR_RFMON_NOTSUP);
  }

  /*
   * Turn a negative snapshot value (invalid), a snapshot value of
   * 0 (unspecified), or a value bigger than the normal maximum
   * value, into the maximum allowed value.
   *
   * If some application really *needs* a bigger snapshot
   * length, we should just increase MAXIMUM_SNAPLEN.
   */
  if (pcap->snapshot <= 0 || pcap->snapshot > MAXIMUM_SNAPLEN)
    pcap->snapshot = MAXIMUM_SNAPLEN;

  if (pcap->snapshot < ETH_MIN+8)
      pcap->snapshot = ETH_MIN+8;

  if (pcap->snapshot > ETH_MAX)   /* silently accept and truncate large MTUs */
      pcap->snapshot = ETH_MAX;

  pcap->linktype          = DLT_EN10MB;  /* !! */
  pcap->cleanup_op        = pcap_cleanup_dos;
  pcap->read_op           = pcap_read_dos;
  pcap->stats_op          = pcap_stats_dos;
  pcap->inject_op         = pcap_sendpacket_dos;
  pcap->setfilter_op      = pcap_setfilter_dos;
  pcap->setdirection_op   = NULL;  /* Not implemented.*/
  pcap->fd                = ++ref_count;

  pcap->bufsize = ETH_MAX+100;     /* add some margin */
  pcap->buffer = calloc (pcap->bufsize, 1);

  if (pcap->fd == 1)  /* first time we're called */
  {
    if (!init_watt32(pcap, pcap->opt.device, pcap->errbuf) ||
        !first_init(pcap->opt.device, pcap->errbuf, pcap->opt.promisc))
    {
      /* XXX - free pcap->buffer? */
      return (PCAP_ERROR);
    }
    atexit (close_driver);
  }
  else if (stricmp(active_dev->name,pcap->opt.device))
  {
    pcap_snprintf (pcap->errbuf, PCAP_ERRBUF_SIZE,
                   "Cannot use different devices simultaneously "
                   "(`%s' vs. `%s')", active_dev->name, pcap->opt.device);
    /* XXX - free pcap->buffer? */
    return (PCAP_ERROR);
  }
  handle_to_device [pcap->fd-1] = active_dev;
  return (0);
}

/*
 * Poll the receiver queue and call the pcap callback-handler
 * with the packet.
 */
static int
pcap_read_one (pcap_t *p, pcap_handler callback, u_char *data)
{
  struct pcap_dos *pd = p->priv;
  struct pcap_pkthdr pcap;
  struct timeval     now, expiry = { 0,0 };
  int    rx_len = 0;

  if (p->opt.timeout > 0)
  {
    gettimeofday2 (&now, NULL);
    expiry.tv_usec = now.tv_usec + 1000UL * p->opt.timeout;
    expiry.tv_sec  = now.tv_sec;
    while (expiry.tv_usec >= 1000000L)
    {
      expiry.tv_usec -= 1000000L;
      expiry.tv_sec++;
    }
  }

  while (!exc_occured)
  {
    volatile struct device *dev; /* might be reset by sig_handler */

    dev = get_device (p->fd);
    if (!dev)
       break;

    PCAP_ASSERT (dev->copy_rx_buf || dev->peek_rx_buf);
    FLUSHK();

    /* If driver has a zero-copy receive facility, peek at the queue,
     * filter it, do the callback and release the buffer.
     */
    if (dev->peek_rx_buf)
    {
      PCAP_ASSERT (dev->release_rx_buf);
      rx_len = (*dev->peek_rx_buf) (&p->buffer);
    }
    else
    {
      rx_len = (*dev->copy_rx_buf) (p->buffer, p->snapshot);
    }

    if (rx_len > 0)  /* got a packet */
    {
      mac_count++;

      FLUSHK();

      pcap.caplen = min (rx_len, p->snapshot);
      pcap.len    = rx_len;

      if (callback &&
          (!p->fcode.bf_insns || bpf_filter(p->fcode.bf_insns, p->buffer, pcap.len, pcap.caplen)))
      {
        filter_count++;

        /* Fix-me!! Should be time of arrival. Not time of
         * capture.
         */
        gettimeofday2 (&pcap.ts, NULL);
        (*callback) (data, &pcap, p->buffer);
      }

      if (dev->release_rx_buf)
        (*dev->release_rx_buf) (p->buffer);

      if (pcap_pkt_debug > 0)
      {
        if (callback == watt32_recv_hook)
             dbug_write ("pcap_recv_hook\n");
        else dbug_write ("pcap_read_op\n");
      }
      FLUSHK();
      return (1);
    }

    /* Has "pcap_breakloop()" been called?
     */
    if (p->break_loop) {
      /*
       * Yes - clear the flag that indicates that it
       * has, and return -2 to indicate that we were
       * told to break out of the loop.
       */
      p->break_loop = 0;
      return (-2);
    }

    /* If not to wait for a packet or pcap_cleanup_dos() called from
     * e.g. SIGINT handler, exit loop now.
     */
    if (p->opt.timeout <= 0 || (volatile int)p->fd <= 0)
       break;

    gettimeofday2 (&now, NULL);

    if (timercmp(&now, &expiry, >))
       break;

#ifndef DJGPP
    kbhit();    /* a real CPU hog */
#endif

    if (pd->wait_proc)
      (*pd->wait_proc)();     /* call yield func */
  }

  if (rx_len < 0)            /* receive error */
  {
    pd->stat.ps_drop++;
#ifdef USE_32BIT_DRIVERS
    if (pcap_pkt_debug > 1)
       printk ("pkt-err %s\n", pktInfo.error);
#endif
    return (-1);
  }
  return (0);
}

static int
pcap_read_dos (pcap_t *p, int cnt, pcap_handler callback, u_char *data)
{
  int rc, num = 0;

  while (num <= cnt || PACKET_COUNT_IS_UNLIMITED(cnt))
  {
    if (p->fd <= 0)
       return (-1);
    rc = pcap_read_one (p, callback, data);
    if (rc > 0)
       num++;
    if (rc < 0)
       break;
    _w32_os_yield();  /* allow SIGINT generation, yield to Win95/NT */
  }
  return (num);
}

/*
 * Return network statistics
 */
static int pcap_stats_dos (pcap_t *p, struct pcap_stat *ps)
{
  struct net_device_stats *stats;
  struct pcap_dos         *pd;
  struct device           *dev = p ? get_device(p->fd) : NULL;

  if (!dev)
  {
    strcpy (p->errbuf, "illegal pcap handle");
    return (-1);
  }

  if (!dev->get_stats || (stats = (*dev->get_stats)(dev)) == NULL)
  {
    strcpy (p->errbuf, "device statistics not available");
    return (-1);
  }

  FLUSHK();

  pd = p->priv;
  pd->stat.ps_recv   = stats->rx_packets;
  pd->stat.ps_drop  += stats->rx_missed_errors;
  pd->stat.ps_ifdrop = stats->rx_dropped +  /* queue full */
                         stats->rx_errors;    /* HW errors */
  if (ps)
     *ps = pd->stat;

  return (0);
}

/*
 * Return detailed network/device statistics.
 * May be called after 'dev->close' is called.
 */
int pcap_stats_ex (pcap_t *p, struct pcap_stat_ex *se)
{
  struct device *dev = p ? get_device (p->fd) : NULL;

  if (!dev || !dev->get_stats)
  {
    strlcpy (p->errbuf, "detailed device statistics not available",
             PCAP_ERRBUF_SIZE);
    return (-1);
  }

  if (!strnicmp(dev->name,"pkt",3))
  {
    strlcpy (p->errbuf, "pktdrvr doesn't have detailed statistics",
             PCAP_ERRBUF_SIZE);
    return (-1);
  }
  memcpy (se, (*dev->get_stats)(dev), sizeof(*se));
  return (0);
}

/*
 * Simply store the filter-code for the pcap_read_dos() callback
 * Some day the filter-code could be handed down to the active
 * device (pkt_rx1.s or 32-bit device interrupt handler).
 */
static int pcap_setfilter_dos (pcap_t *p, struct bpf_program *fp)
{
  if (!p)
     return (-1);
  p->fcode = *fp;
  return (0);
}

/*
 * Return # of packets received in pcap_read_dos()
 */
u_long pcap_mac_packets (void)
{
  return (mac_count);
}

/*
 * Return # of packets passed through filter in pcap_read_dos()
 */
u_long pcap_filter_packets (void)
{
  return (filter_count);
}

/*
 * Close pcap device. Not called for offline captures.
 */
static void pcap_cleanup_dos (pcap_t *p)
{
  struct pcap_dos *pd;

  if (!exc_occured)
  {
    pd = p->priv;
    if (pcap_stats(p,NULL) < 0)
       pd->stat.ps_drop = 0;
    if (!get_device(p->fd))
       return;

    handle_to_device [p->fd-1] = NULL;
    p->fd = 0;
    if (ref_count > 0)
        ref_count--;
    if (ref_count > 0)
       return;
  }
  close_driver();
  /* XXX - call pcap_cleanup_live_common? */
}

/*
 * Return the name of the 1st network interface,
 * or NULL if none can be found.
 */
char *pcap_lookupdev (char *ebuf)
{
  struct device *dev;

#ifdef USE_32BIT_DRIVERS
  init_32bit();
#endif

  for (dev = (struct device*)dev_base; dev; dev = dev->next)
  {
    PCAP_ASSERT (dev->probe);

    if ((*dev->probe)(dev))
    {
      FLUSHK();
      probed_dev = (struct device*) dev; /* remember last probed device */
      return (char*) dev->name;
    }
  }

  if (ebuf)
     strcpy (ebuf, "No driver found");
  return (NULL);
}

/*
 * Gets localnet & netmask from Watt-32.
 */
int pcap_lookupnet (const char *device, bpf_u_int32 *localnet,
                    bpf_u_int32 *netmask, char *errbuf)
{
  DWORD mask, net;

  if (!_watt_is_init)
  {
    strcpy (errbuf, "pcap_open_offline() or pcap_activate() must be "
                    "called first");
    return (-1);
  }

  mask  = _w32_sin_mask;
  net = my_ip_addr & mask;
  if (net == 0)
  {
    if (IN_CLASSA(*netmask))
       net = IN_CLASSA_NET;
    else if (IN_CLASSB(*netmask))
       net = IN_CLASSB_NET;
    else if (IN_CLASSC(*netmask))
       net = IN_CLASSC_NET;
    else
    {
      pcap_snprintf (errbuf, PCAP_ERRBUF_SIZE, "inet class for 0x%lx unknown", mask);
      return (-1);
    }
  }
  *localnet = htonl (net);
  *netmask = htonl (mask);

  ARGSUSED (device);
  return (0);
}

/*
 * Get a list of all interfaces that are present and that we probe okay.
 * Returns -1 on error, 0 otherwise.
 * The list may be NULL epty if no interfaces were up and could be opened.
 */
int pcap_platform_finddevs  (pcap_if_list_t *devlistp, char *errbuf)
{
  struct device     *dev;
  pcap_if_t *curdev;
#if 0   /* Pkt drivers should have no addresses */
  struct sockaddr_in sa_ll_1, sa_ll_2;
  struct sockaddr   *addr, *netmask, *broadaddr, *dstaddr;
#endif
  int       ret = 0;
  int       found = 0;

  for (dev = (struct device*)dev_base; dev; dev = dev->next)
  {
    PCAP_ASSERT (dev->probe);

    if (!(*dev->probe)(dev))
       continue;

    PCAP_ASSERT (dev->close);  /* set by probe routine */
    FLUSHK();
    (*dev->close) (dev);

    /*
     * XXX - find out whether it's up or running?  Does that apply here?
     * Can we find out if anything's plugged into the adapter, if it's
     * a wired device, and set PCAP_IF_CONNECTION_STATUS_CONNECTED
     * or PCAP_IF_CONNECTION_STATUS_DISCONNECTED?
     */
    if ((curdev = add_dev(devlistp, dev->name, 0,
                dev->long_name, errbuf)) == NULL)
    {
      ret = -1;
      break;
    }
    found = 1;
#if 0   /* Pkt drivers should have no addresses */
    memset (&sa_ll_1, 0, sizeof(sa_ll_1));
    memset (&sa_ll_2, 0, sizeof(sa_ll_2));
    sa_ll_1.sin_family = AF_INET;
    sa_ll_2.sin_family = AF_INET;

    addr      = (struct sockaddr*) &sa_ll_1;
    netmask   = (struct sockaddr*) &sa_ll_1;
    dstaddr   = (struct sockaddr*) &sa_ll_1;
    broadaddr = (struct sockaddr*) &sa_ll_2;
    memset (&sa_ll_2.sin_addr, 0xFF, sizeof(sa_ll_2.sin_addr));

    if (add_addr_to_dev(curdev, addr, sizeof(*addr),
                        netmask, sizeof(*netmask),
                        broadaddr, sizeof(*broadaddr),
                        dstaddr, sizeof(*dstaddr), errbuf) < 0)
    {
      ret = -1;
      break;
    }
#endif
  }

  if (ret == 0 && !found)
     strcpy (errbuf, "No drivers found");

  return (ret);
}

/*
 * pcap_assert() is mainly used for debugging
 */
void pcap_assert (const char *what, const char *file, unsigned line)
{
  FLUSHK();
  fprintf (stderr, "%s (%u): Assertion \"%s\" failed\n",
           file, line, what);
  close_driver();
  _exit (-1);
}

/*
 * For pcap_offline_read(): wait and yield between printing packets
 * to simulate the pace packets where actually recorded.
 */
void pcap_set_wait (pcap_t *p, void (*yield)(void), int wait)
{
  if (p)
  {
    struct pcap_dos *pd = p->priv;

    pd->wait_proc  = yield;
    p->opt.timeout = wait;
  }
}

/*
 * Initialise a named network device.
 */
static struct device *
open_driver (const char *dev_name, char *ebuf, int promisc)
{
  struct device *dev;

  for (dev = (struct device*)dev_base; dev; dev = dev->next)
  {
    PCAP_ASSERT (dev->name);

    if (strcmp (dev_name,dev->name))
       continue;

    if (!probed_dev)   /* user didn't call pcap_lookupdev() first */
    {
      PCAP_ASSERT (dev->probe);

      if (!(*dev->probe)(dev))    /* call the xx_probe() function */
      {
        pcap_snprintf (ebuf, PCAP_ERRBUF_SIZE, "failed to detect device `%s'", dev_name);
        return (NULL);
      }
      probed_dev = dev;  /* device is probed okay and may be used */
    }
    else if (dev != probed_dev)
    {
      goto not_probed;
    }

    FLUSHK();

    /* Select what traffic to receive
     */
    if (promisc)
         dev->flags |=  (IFF_ALLMULTI | IFF_PROMISC);
    else dev->flags &= ~(IFF_ALLMULTI | IFF_PROMISC);

    PCAP_ASSERT (dev->open);

    if (!(*dev->open)(dev))
    {
      pcap_snprintf (ebuf, PCAP_ERRBUF_SIZE, "failed to activate device `%s'", dev_name);
      if (pktInfo.error && !strncmp(dev->name,"pkt",3))
      {
        strcat (ebuf, ": ");
        strcat (ebuf, pktInfo.error);
      }
      return (NULL);
    }

    /* Some devices need this to operate in promiscous mode
     */
    if (promisc && dev->set_multicast_list)
       (*dev->set_multicast_list) (dev);

    active_dev = dev;   /* remember our active device */
    break;
  }

  /* 'dev_name' not matched in 'dev_base' list.
   */
  if (!dev)
  {
    pcap_snprintf (ebuf, PCAP_ERRBUF_SIZE, "device `%s' not supported", dev_name);
    return (NULL);
  }

not_probed:
  if (!probed_dev)
  {
    pcap_snprintf (ebuf, PCAP_ERRBUF_SIZE, "device `%s' not probed", dev_name);
    return (NULL);
  }
  return (dev);
}

/*
 * Deinitialise MAC driver.
 * Set receive mode back to default mode.
 */
static void close_driver (void)
{
  /* !!todo: loop over all 'handle_to_device[]' ? */
  struct device *dev = active_dev;

  if (dev && dev->close)
  {
    (*dev->close) (dev);
    FLUSHK();
  }

  active_dev = NULL;

#ifdef USE_32BIT_DRIVERS
  if (rx_pool)
  {
    k_free (rx_pool);
    rx_pool = NULL;
  }
  if (dev)
     pcibios_exit();
#endif
}


#ifdef __DJGPP__
static void setup_signals (void (*handler)(int))
{
  signal (SIGSEGV,handler);
  signal (SIGILL, handler);
  signal (SIGFPE, handler);
}

static void exc_handler (int sig)
{
#ifdef USE_32BIT_DRIVERS
  if (active_dev->irq > 0)    /* excludes IRQ 0 */
  {
    disable_irq (active_dev->irq);
    irq_eoi_cmd (active_dev->irq);
    _printk_safe = 1;
  }
#endif

  switch (sig)
  {
    case SIGSEGV:
         fputs ("Catching SIGSEGV.\n", stderr);
         break;
    case SIGILL:
         fputs ("Catching SIGILL.\n", stderr);
         break;
    case SIGFPE:
         _fpreset();
         fputs ("Catching SIGFPE.\n", stderr);
         break;
    default:
         fprintf (stderr, "Catching signal %d.\n", sig);
  }
  exc_occured = 1;
  close_driver();
}
#endif  /* __DJGPP__ */


/*
 * Open the pcap device for the first client calling pcap_activate()
 */
static int first_init (const char *name, char *ebuf, int promisc)
{
  struct device *dev;

#ifdef USE_32BIT_DRIVERS
  rx_pool = k_calloc (RECEIVE_BUF_SIZE, RECEIVE_QUEUE_SIZE);
  if (!rx_pool)
  {
    strcpy (ebuf, "Not enough memory (Rx pool)");
    return (0);
  }
#endif

#ifdef __DJGPP__
  setup_signals (exc_handler);
#endif

#ifdef USE_32BIT_DRIVERS
  init_32bit();
#endif

  dev = open_driver (name, ebuf, promisc);
  if (!dev)
  {
#ifdef USE_32BIT_DRIVERS
    k_free (rx_pool);
    rx_pool = NULL;
#endif

#ifdef __DJGPP__
    setup_signals (SIG_DFL);
#endif
    return (0);
  }

#ifdef USE_32BIT_DRIVERS
  /*
   * If driver is NOT a 16-bit "pkt/ndis" driver (having a 'copy_rx_buf'
   * set in it's probe handler), initialise near-memory ring-buffer for
   * the 32-bit device.
   */
  if (dev->copy_rx_buf == NULL)
  {
    dev->get_rx_buf     = get_rxbuf;
    dev->peek_rx_buf    = peek_rxbuf;
    dev->release_rx_buf = release_rxbuf;
    pktq_init (&dev->queue, RECEIVE_BUF_SIZE, RECEIVE_QUEUE_SIZE, rx_pool);
  }
#endif
  return (1);
}

#ifdef USE_32BIT_DRIVERS
static void init_32bit (void)
{
  static int init_pci = 0;

  if (!_printk_file)
     _printk_init (64*1024, NULL); /* calls atexit(printk_exit) */

  if (!init_pci)
     (void)pci_init();             /* init BIOS32+PCI interface */
  init_pci = 1;
}
#endif


/*
 * Hook functions for using Watt-32 together with pcap
 */
static char rxbuf [ETH_MAX+100]; /* rx-buffer with some margin */
static WORD etype;
static pcap_t pcap_save;

static void watt32_recv_hook (u_char *dummy, const struct pcap_pkthdr *pcap,
                              const u_char *buf)
{
  /* Fix me: assumes Ethernet II only */
  struct ether_header *ep = (struct ether_header*) buf;

  memcpy (rxbuf, buf, pcap->caplen);
  etype = ep->ether_type;
  ARGSUSED (dummy);
}

#if (WATTCP_VER >= 0x0224)
/*
 * This function is used by Watt-32 to poll for a packet.
 * i.e. it's set to bypass _eth_arrived()
 */
static void *pcap_recv_hook (WORD *type)
{
  int len = pcap_read_dos (&pcap_save, 1, watt32_recv_hook, NULL);

  if (len < 0)
     return (NULL);

  *type = etype;
  return (void*) &rxbuf;
}

/*
 * This function is called by Watt-32 (via _eth_xmit_hook).
 * If dbug_init() was called, we should trace packets sent.
 */
static int pcap_xmit_hook (const void *buf, unsigned len)
{
  int rc = 0;

  if (pcap_pkt_debug > 0)
     dbug_write ("pcap_xmit_hook: ");

  if (active_dev && active_dev->xmit)
     if ((*active_dev->xmit) (active_dev, buf, len) > 0)
        rc = len;

  if (pcap_pkt_debug > 0)
     dbug_write (rc ? "ok\n" : "fail\n");
  return (rc);
}
#endif

static int pcap_sendpacket_dos (pcap_t *p, const void *buf, size_t len)
{
  struct device *dev = p ? get_device(p->fd) : NULL;

  if (!dev || !dev->xmit)
     return (-1);
  return (*dev->xmit) (dev, buf, len);
}

/*
 * This function is called by Watt-32 in tcp_post_init().
 * We should prevent Watt-32 from using BOOTP/DHCP/RARP etc.
 */
static void (*prev_post_hook) (void);

static void pcap_init_hook (void)
{
  _w32__bootp_on = _w32__dhcp_on = _w32__rarp_on = 0;
  _w32__do_mask_req = 0;
  _w32_dynamic_host = 0;
  if (prev_post_hook)
    (*prev_post_hook)();
}

/*
 * Supress PRINT message from Watt-32's sock_init()
 */
static void null_print (void) {}

/*
 * To use features of Watt-32 (netdb functions and socket etc.)
 * we must call sock_init(). But we set various hooks to prevent
 * using normal PKTDRVR functions in pcpkt.c. This should hopefully
 * make Watt-32 and pcap co-operate.
 */
static int init_watt32 (struct pcap *pcap, const char *dev_name, char *err_buf)
{
  char *env;
  int   rc, MTU, has_ip_addr;
  int   using_pktdrv = 1;

  /* If user called sock_init() first, we need to reinit in
   * order to open debug/trace-file properly
   */
  if (_watt_is_init)
     sock_exit();

  env = getenv ("PCAP_TRACE");
  if (env && atoi(env) > 0 &&
      pcap_pkt_debug < 0)   /* if not already set */
  {
    dbug_init();
    pcap_pkt_debug = atoi (env);
  }

  _watt_do_exit      = 0;    /* prevent sock_init() calling exit() */
  prev_post_hook     = _w32_usr_post_init;
  _w32_usr_post_init = pcap_init_hook;
  _w32_print_hook    = null_print;

  if (dev_name && strncmp(dev_name,"pkt",3))
     using_pktdrv = FALSE;

  rc = sock_init();
  has_ip_addr = (rc != 8);  /* IP-address assignment failed */

  /* if pcap is using a 32-bit driver w/o a pktdrvr loaded, we
   * just pretend Watt-32 is initialised okay.
   *
   * !! fix-me: The Watt-32 config isn't done if no pktdrvr
   *            was found. In that case my_ip_addr + sin_mask
   *            have default values. Should be taken from another
   *            ini-file/environment in any case (ref. tcpdump.ini)
   */
  _watt_is_init = 1;

  if (!using_pktdrv || !has_ip_addr)  /* for now .... */
  {
    static const char myip[] = "192.168.0.1";
    static const char mask[] = "255.255.255.0";

    printf ("Just guessing, using IP %s and netmask %s\n", myip, mask);
    my_ip_addr    = aton (myip);
    _w32_sin_mask = aton (mask);
  }
  else if (rc && using_pktdrv)
  {
    pcap_snprintf (err_buf, PCAP_ERRBUF_SIZE, "sock_init() failed, code %d", rc);
    return (0);
  }

  /* Set recv-hook for peeking in _eth_arrived().
   */
#if (WATTCP_VER >= 0x0224)
  _eth_recv_hook = pcap_recv_hook;
  _eth_xmit_hook = pcap_xmit_hook;
#endif

  /* Free the pkt-drvr handle allocated in pkt_init().
   * The above hooks should thus use the handle reopened in open_driver()
   */
  if (using_pktdrv)
  {
    _eth_release();
/*  _eth_is_init = 1; */  /* hack to get Rx/Tx-hooks in Watt-32 working */
  }

  memcpy (&pcap_save, pcap, sizeof(pcap_save));
  MTU = pkt_get_mtu();
  pcap_save.fcode.bf_insns = NULL;
  pcap_save.linktype       = _eth_get_hwtype (NULL, NULL);
  pcap_save.snapshot       = MTU > 0 ? MTU : ETH_MAX; /* assume 1514 */

#if 1
  /* prevent use of resolve() and resolve_ip()
   */
  last_nameserver = 0;
#endif
  return (1);
}

int EISA_bus = 0;  /* Where is natural place for this? */

/*
 * Application config hooks to set various driver parameters.
 */

static const struct config_table debug_tab[] = {
            { "PKT.DEBUG",       ARG_ATOI,   &pcap_pkt_debug    },
            { "PKT.VECTOR",      ARG_ATOX_W, NULL               },
            { "NDIS.DEBUG",      ARG_ATOI,   NULL               },
#ifdef USE_32BIT_DRIVERS
            { "3C503.DEBUG",     ARG_ATOI,   &ei_debug          },
            { "3C503.IO_BASE",   ARG_ATOX_W, &el2_dev.base_addr },
            { "3C503.MEMORY",    ARG_ATOX_W, &el2_dev.mem_start },
            { "3C503.IRQ",       ARG_ATOI,   &el2_dev.irq       },
            { "3C505.DEBUG",     ARG_ATOI,   NULL               },
            { "3C505.BASE",      ARG_ATOX_W, NULL               },
            { "3C507.DEBUG",     ARG_ATOI,   NULL               },
            { "3C509.DEBUG",     ARG_ATOI,   &el3_debug         },
            { "3C509.ILOOP",     ARG_ATOI,   &el3_max_loop      },
            { "3C529.DEBUG",     ARG_ATOI,   NULL               },
            { "3C575.DEBUG",     ARG_ATOI,   &debug_3c575       },
            { "3C59X.DEBUG",     ARG_ATOI,   &vortex_debug      },
            { "3C59X.IFACE0",    ARG_ATOI,   &vortex_options[0] },
            { "3C59X.IFACE1",    ARG_ATOI,   &vortex_options[1] },
            { "3C59X.IFACE2",    ARG_ATOI,   &vortex_options[2] },
            { "3C59X.IFACE3",    ARG_ATOI,   &vortex_options[3] },
            { "3C90X.DEBUG",     ARG_ATOX_W, &tc90xbc_debug     },
            { "ACCT.DEBUG",      ARG_ATOI,   &ethpk_debug       },
            { "CS89.DEBUG",      ARG_ATOI,   &cs89_debug        },
            { "RTL8139.DEBUG",   ARG_ATOI,   &rtl8139_debug     },
        /*  { "RTL8139.FDUPLEX", ARG_ATOI,   &rtl8139_options   }, */
            { "SMC.DEBUG",       ARG_ATOI,   &ei_debug          },
        /*  { "E100.DEBUG",      ARG_ATOI,   &e100_debug        }, */
            { "PCI.DEBUG",       ARG_ATOI,   &pci_debug         },
            { "BIOS32.DEBUG",    ARG_ATOI,   &bios32_debug      },
            { "IRQ.DEBUG",       ARG_ATOI,   &irq_debug         },
            { "TIMER.IRQ",       ARG_ATOI,   &timer_irq         },
#endif
            { NULL }
          };

/*
 * pcap_config_hook() is an extension to application's config
 * handling. Uses Watt-32's config-table function.
 */
int pcap_config_hook (const char *keyword, const char *value)
{
  return parse_config_table (debug_tab, NULL, keyword, value);
}

/*
 * Linked list of supported devices
 */
struct device       *active_dev = NULL;      /* the device we have opened */
struct device       *probed_dev = NULL;      /* the device we have probed */
const struct device *dev_base   = &pkt_dev;  /* list of network devices */

/*
 * PKTDRVR device functions
 */
int pcap_pkt_debug = -1;

static void pkt_close (struct device *dev)
{
  BOOL okay = PktExitDriver();

  if (pcap_pkt_debug > 1)
     fprintf (stderr, "pkt_close(): %d\n", okay);

  if (dev->priv)
     free (dev->priv);
  dev->priv = NULL;
}

static int pkt_open (struct device *dev)
{
  PKT_RX_MODE mode;

  if (dev->flags & IFF_PROMISC)
       mode = PDRX_ALL_PACKETS;
  else mode = PDRX_BROADCAST;

  if (!PktInitDriver(mode))
     return (0);

  PktResetStatistics (pktInfo.handle);
  PktQueueBusy (FALSE);
  return (1);
}

static int pkt_xmit (struct device *dev, const void *buf, int len)
{
  struct net_device_stats *stats = (struct net_device_stats*) dev->priv;

  if (pcap_pkt_debug > 0)
     dbug_write ("pcap_xmit\n");

  if (!PktTransmit(buf,len))
  {
    stats->tx_errors++;
    return (0);
  }
  return (len);
}

static void *pkt_stats (struct device *dev)
{
  struct net_device_stats *stats = (struct net_device_stats*) dev->priv;

  if (!stats || !PktSessStatistics(pktInfo.handle))
     return (NULL);

  stats->rx_packets       = pktStat.inPackets;
  stats->rx_errors        = pktStat.lost;
  stats->rx_missed_errors = PktRxDropped();
  return (stats);
}

static int pkt_probe (struct device *dev)
{
  if (!PktSearchDriver())
     return (0);

  dev->open           = pkt_open;
  dev->xmit           = pkt_xmit;
  dev->close          = pkt_close;
  dev->get_stats      = pkt_stats;
  dev->copy_rx_buf    = PktReceive;  /* farmem peek and copy routine */
  dev->get_rx_buf     = NULL;
  dev->peek_rx_buf    = NULL;
  dev->release_rx_buf = NULL;
  dev->priv           = calloc (sizeof(struct net_device_stats), 1);
  if (!dev->priv)
     return (0);
  return (1);
}

/*
 * NDIS device functions
 */
static void ndis_close (struct device *dev)
{
#ifdef USE_NDIS2
  NdisShutdown();
#endif
  ARGSUSED (dev);
}

static int ndis_open (struct device *dev)
{
  int promis = (dev->flags & IFF_PROMISC);

#ifdef USE_NDIS2
  if (!NdisInit(promis))
     return (0);
  return (1);
#else
  ARGSUSED (promis);
  return (0);
#endif
}

static void *ndis_stats (struct device *dev)
{
  static struct net_device_stats stats;

  /* to-do */
  ARGSUSED (dev);
  return (&stats);
}

static int ndis_probe (struct device *dev)
{
#ifdef USE_NDIS2
  if (!NdisOpen())
     return (0);
#endif

  dev->open           = ndis_open;
  dev->xmit           = NULL;
  dev->close          = ndis_close;
  dev->get_stats      = ndis_stats;
  dev->copy_rx_buf    = NULL;       /* to-do */
  dev->get_rx_buf     = NULL;       /* upcall is from rmode driver */
  dev->peek_rx_buf    = NULL;
  dev->release_rx_buf = NULL;
  return (0);
}

/*
 * Search & probe for supported 32-bit (pmode) pcap devices
 */
#if defined(USE_32BIT_DRIVERS)

struct device el2_dev LOCKED_VAR = {
              "3c503",
              "EtherLink II",
              0,
              0,0,0,0,0,0,
              NULL,
              el2_probe
            };

struct device el3_dev LOCKED_VAR = {
              "3c509",
              "EtherLink III",
              0,
              0,0,0,0,0,0,
              &el2_dev,
              el3_probe
            };

struct device tc515_dev LOCKED_VAR = {
              "3c515",
              "EtherLink PCI",
              0,
              0,0,0,0,0,0,
              &el3_dev,
              tc515_probe
            };

struct device tc59_dev LOCKED_VAR = {
              "3c59x",
              "EtherLink PCI",
              0,
              0,0,0,0,0,0,
              &tc515_dev,
              tc59x_probe
            };

struct device tc90xbc_dev LOCKED_VAR = {
              "3c90x",
              "EtherLink 90X",
              0,
              0,0,0,0,0,0,
              &tc59_dev,
              tc90xbc_probe
            };

struct device wd_dev LOCKED_VAR = {
              "wd",
              "Westen Digital",
              0,
              0,0,0,0,0,0,
              &tc90xbc_dev,
              wd_probe
            };

struct device ne_dev LOCKED_VAR = {
              "ne",
              "NEx000",
              0,
              0,0,0,0,0,0,
              &wd_dev,
              ne_probe
            };

struct device acct_dev LOCKED_VAR = {
              "acct",
              "Accton EtherPocket",
              0,
              0,0,0,0,0,0,
              &ne_dev,
              ethpk_probe
            };

struct device cs89_dev LOCKED_VAR = {
              "cs89",
              "Crystal Semiconductor",
              0,
              0,0,0,0,0,0,
              &acct_dev,
              cs89x0_probe
            };

struct device rtl8139_dev LOCKED_VAR = {
              "rtl8139",
              "RealTek PCI",
              0,
              0,0,0,0,0,0,
              &cs89_dev,
              rtl8139_probe     /* dev->probe routine */
            };

/*
 * Dequeue routine is called by polling.
 * NOTE: the queue-element is not copied, only a pointer is
 * returned at '*buf'
 */
int peek_rxbuf (BYTE **buf)
{
  struct rx_elem *tail, *head;

  PCAP_ASSERT (pktq_check (&active_dev->queue));

  DISABLE();
  tail = pktq_out_elem (&active_dev->queue);
  head = pktq_in_elem (&active_dev->queue);
  ENABLE();

  if (head != tail)
  {
    PCAP_ASSERT (tail->size < active_dev->queue.elem_size-4-2);

    *buf = &tail->data[0];
    return (tail->size);
  }
  *buf = NULL;
  return (0);
}

/*
 * Release buffer we peeked at above.
 */
int release_rxbuf (BYTE *buf)
{
#ifndef NDEBUG
  struct rx_elem *tail = pktq_out_elem (&active_dev->queue);

  PCAP_ASSERT (&tail->data[0] == buf);
#else
  ARGSUSED (buf);
#endif
  pktq_inc_out (&active_dev->queue);
  return (1);
}

/*
 * get_rxbuf() routine (in locked code) is called from IRQ handler
 * to request a buffer. Interrupts are disabled and we have a 32kB stack.
 */
BYTE *get_rxbuf (int len)
{
  int idx;

  if (len < ETH_MIN || len > ETH_MAX)
     return (NULL);

  idx = pktq_in_index (&active_dev->queue);

#ifdef DEBUG
  {
    static int fan_idx LOCKED_VAR = 0;
    writew ("-\\|/"[fan_idx++] | (15 << 8),      /* white on black colour */
            0xB8000 + 2*79);  /* upper-right corner, 80-col colour screen */
    fan_idx &= 3;
  }
/* writew (idx + '0' + 0x0F00, 0xB8000 + 2*78); */
#endif

  if (idx != active_dev->queue.out_index)
  {
    struct rx_elem *head = pktq_in_elem (&active_dev->queue);

    head->size = len;
    active_dev->queue.in_index = idx;
    return (&head->data[0]);
  }

  /* !!to-do: drop 25% of the oldest element
   */
  pktq_clear (&active_dev->queue);
  return (NULL);
}

/*
 *  Simple ring-buffer queue handler for reception of packets
 *  from network driver.
 */
#define PKTQ_MARKER  0xDEADBEEF

static int pktq_check (struct rx_ringbuf *q)
{
#ifndef NDEBUG
  int   i;
  char *buf;
#endif

  if (!q || !q->num_elem || !q->buf_start)
     return (0);

#ifndef NDEBUG
  buf = q->buf_start;

  for (i = 0; i < q->num_elem; i++)
  {
    buf += q->elem_size;
    if (*(DWORD*)(buf - sizeof(DWORD)) != PKTQ_MARKER)
       return (0);
  }
#endif
  return (1);
}

static int pktq_init (struct rx_ringbuf *q, int size, int num, char *pool)
{
  int i;

  q->elem_size = size;
  q->num_elem  = num;
  q->buf_start = pool;
  q->in_index  = 0;
  q->out_index = 0;

  PCAP_ASSERT (size >= sizeof(struct rx_elem) + sizeof(DWORD));
  PCAP_ASSERT (num);
  PCAP_ASSERT (pool);

  for (i = 0; i < num; i++)
  {
#if 0
    struct rx_elem *elem = (struct rx_elem*) pool;

    /* assert dword aligned elements
     */
    PCAP_ASSERT (((unsigned)(&elem->data[0]) & 3) == 0);
#endif
    pool += size;
    *(DWORD*) (pool - sizeof(DWORD)) = PKTQ_MARKER;
  }
  return (1);
}

/*
 * Increment the queue 'out_index' (tail).
 * Check for wraps.
 */
static int pktq_inc_out (struct rx_ringbuf *q)
{
  q->out_index++;
  if (q->out_index >= q->num_elem)
      q->out_index = 0;
  return (q->out_index);
}

/*
 * Return the queue's next 'in_index' (head).
 * Check for wraps.
 */
static int pktq_in_index (struct rx_ringbuf *q)
{
  volatile int index = q->in_index + 1;

  if (index >= q->num_elem)
      index = 0;
  return (index);
}

/*
 * Return the queue's head-buffer.
 */
static struct rx_elem *pktq_in_elem (struct rx_ringbuf *q)
{
  return (struct rx_elem*) (q->buf_start + (q->elem_size * q->in_index));
}

/*
 * Return the queue's tail-buffer.
 */
static struct rx_elem *pktq_out_elem (struct rx_ringbuf *q)
{
  return (struct rx_elem*) (q->buf_start + (q->elem_size * q->out_index));
}

/*
 * Clear the queue ring-buffer by setting head=tail.
 */
static void pktq_clear (struct rx_ringbuf *q)
{
  q->in_index = q->out_index;
}

/*
 * Symbols that must be linkable for "gcc -O0"
 */
#undef __IOPORT_H
#undef __DMA_H

#define extern
#define __inline__

#include "msdos/pm_drvr/ioport.h"
#include "msdos/pm_drvr/dma.h"

#endif /* USE_32BIT_DRIVERS */

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
  return ("DOS-" PCAP_VERSION_STRING);
}
