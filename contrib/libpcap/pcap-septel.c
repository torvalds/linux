/*
 * pcap-septel.c: Packet capture interface for Intel/Septel card.
 *
 * The functionality of this code attempts to mimic that of pcap-linux as much
 * as possible.  This code is compiled in several different ways depending on
 * whether SEPTEL_ONLY and HAVE_SEPTEL_API are defined.  If HAVE_SEPTEL_API is
 * not defined it should not get compiled in, otherwise if SEPTEL_ONLY is
 * defined then the 'septel_' function calls are renamed to 'pcap_'
 * equivalents.  If SEPTEL_ONLY is not defined then nothing is altered - the
 * septel_ functions will be called as required from their
 * pcap-linux/equivalents.
 *
 * Authors: Gilbert HOYEK (gil_hoyek@hotmail.com), Elias M. KHOURY
 * (+961 3 485243)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/param.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pcap-int.h"

#include <ctype.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <msg.h>
#include <ss7_inc.h>
#include <sysgct.h>
#include <pack.h>
#include <system.h>

#include "pcap-septel.h"

static int septel_setfilter(pcap_t *p, struct bpf_program *fp);
static int septel_stats(pcap_t *p, struct pcap_stat *ps);
static int septel_getnonblock(pcap_t *p);
static int septel_setnonblock(pcap_t *p, int nonblock);

/*
 * Private data for capturing on Septel devices.
 */
struct pcap_septel {
	struct pcap_stat stat;
}

/*
 *  Read at most max_packets from the capture queue and call the callback
 *  for each of them. Returns the number of packets handled, -1 if an
 *  error occured, or -2 if we were told to break out of the loop.
 */
static int septel_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user) {

  struct pcap_septel *ps = p->priv;
  HDR *h;
  MSG *m;
  int processed = 0 ;
  int t = 0 ;

  /* identifier for the message queue of the module(upe) from which we are capturing
   * packets.These IDs are defined in system.txt . By default it is set to 0x2d
   * so change it to 0xdd for technical reason and therefore the module id for upe becomes:
   * LOCAL        0xdd           * upe - Example user part task */
  unsigned int id = 0xdd;

  /* process the packets */
  do  {

    unsigned short packet_len = 0;
    int caplen = 0;
    int counter = 0;
    struct pcap_pkthdr   pcap_header;
    u_char *dp ;

    /*
     * Has "pcap_breakloop()" been called?
     */
loop:
    if (p->break_loop) {
      /*
       * Yes - clear the flag that indicates that
       * it has, and return -2 to indicate that
       * we were told to break out of the loop.
       */
      p->break_loop = 0;
      return -2;
    }

    /*repeat until a packet is read
     *a NULL message means :
     * when no packet is in queue or all packets in queue already read */
    do  {
      /* receive packet in non-blocking mode
       * GCT_grab is defined in the septel library software */
      h = GCT_grab(id);

      m = (MSG*)h;
      /* a couter is added here to avoid an infinite loop
       * that will cause our capture program GUI to freeze while waiting
       * for a packet*/
      counter++ ;

    }
    while  ((m == NULL)&& (counter< 100)) ;

    if (m != NULL) {

      t = h->type ;

      /* catch only messages with type = 0xcf00 or 0x8f01 corrsponding to ss7 messages*/
      /* XXX = why not use API_MSG_TX_REQ for 0xcf00 and API_MSG_RX_IND
       * for 0x8f01? */
      if ((t != 0xcf00) && (t != 0x8f01)) {
        relm(h);
        goto loop ;
      }

      /* XXX - is API_MSG_RX_IND for an MTP2 or MTP3 message? */
      dp = get_param(m);/* get pointer to MSG parameter area (m->param) */
      packet_len = m->len;
      caplen =  p->snapshot ;


      if (caplen > packet_len) {

        caplen = packet_len;
      }
      /* Run the packet filter if there is one. */
      if ((p->fcode.bf_insns == NULL) || bpf_filter(p->fcode.bf_insns, dp, packet_len, caplen)) {


        /*  get a time stamp , consisting of :
         *
         *  pcap_header.ts.tv_sec:
         *  ----------------------
         *   a UNIX format time-in-seconds when he packet was captured,
         *   i.e. the number of seconds since Epoch time (January 1,1970, 00:00:00 GMT)
         *
         *  pcap_header.ts.tv_usec :
         *  ------------------------
         *   the number of microseconds since that second
         *   when the packet was captured
         */

        (void)gettimeofday(&pcap_header.ts, NULL);

        /* Fill in our own header data */
        pcap_header.caplen = caplen;
        pcap_header.len = packet_len;

        /* Count the packet. */
        ps->stat.ps_recv++;

        /* Call the user supplied callback function */
        callback(user, &pcap_header, dp);

        processed++ ;

      }
      /* after being processed the packet must be
       *released in order to receive another one */
      relm(h);
    }else
      processed++;

  }
  while (processed < cnt) ;

  return processed ;
}


static int
septel_inject(pcap_t *handle, const void *buf _U_, size_t size _U_)
{
  strlcpy(handle->errbuf, "Sending packets isn't supported on Septel cards",
          PCAP_ERRBUF_SIZE);
  return (-1);
}

/*
 *  Activate a handle for a live capture from the given Septel device.  Always pass a NULL device
 *  The promisc flag is ignored because Septel cards have built-in tracing.
 *  The timeout is also ignored as it is not supported in hardware.
 *
 *  See also pcap(3).
 */
static pcap_t *septel_activate(pcap_t* handle) {
  /* Initialize some components of the pcap structure. */
  handle->linktype = DLT_MTP2;

  /*
   * Turn a negative snapshot value (invalid), a snapshot value of
   * 0 (unspecified), or a value bigger than the normal maximum
   * value, into the maximum allowed value.
   *
   * If some application really *needs* a bigger snapshot
   * length, we should just increase MAXIMUM_SNAPLEN.
   */
  if (handle->snapshot <= 0 || handle->snapshot > MAXIMUM_SNAPLEN)
    handle->snapshot = MAXIMUM_SNAPLEN;

  handle->bufsize = 0;

  /*
   * "select()" and "poll()" don't work on Septel queues
   */
  handle->selectable_fd = -1;

  handle->read_op = septel_read;
  handle->inject_op = septel_inject;
  handle->setfilter_op = septel_setfilter;
  handle->set_datalink_op = NULL; /* can't change data link type */
  handle->getnonblock_op = septel_getnonblock;
  handle->setnonblock_op = septel_setnonblock;
  handle->stats_op = septel_stats;

  return 0;
}

pcap_t *septel_create(const char *device, char *ebuf, int *is_ours) {
	const char *cp;
	pcap_t *p;

	/* Does this look like the Septel device? */
	cp = strrchr(device, '/');
	if (cp == NULL)
		cp = device;
	if (strcmp(cp, "septel") != 0) {
		/* Nope, it's not "septel" */
		*is_ours = 0;
		return NULL;
	}

	/* OK, it's probably ours. */
	*is_ours = 1;

	p = pcap_create_common(ebuf, sizeof (struct pcap_septel));
	if (p == NULL)
		return NULL;

	p->activate_op = septel_activate;
	/*
	 * Set these up front, so that, even if our client tries
	 * to set non-blocking mode before we're activated, or
	 * query the state of non-blocking mode, they get an error,
	 * rather than having the non-blocking mode option set
	 * for use later.
	 */
	p->getnonblock_op = septel_getnonblock;
	p->setnonblock_op = septel_setnonblock;
	return p;
}

static int septel_stats(pcap_t *p, struct pcap_stat *ps) {
  struct pcap_septel *handlep = p->priv;
  /*handlep->stat.ps_recv = 0;*/
  /*handlep->stat.ps_drop = 0;*/

  *ps = handlep->stat;

  return 0;
}


int
septel_findalldevs(pcap_if_list_t *devlistp, char *errbuf)
{
  /*
   * XXX - do the notions of "up", "running", or "connected" apply here?
   */
  if (add_dev(devlistp,"septel",0,"Intel/Septel device",errbuf) == NULL)
    return -1;
  return 0;
}


/*
 * Installs the given bpf filter program in the given pcap structure.  There is
 * no attempt to store the filter in kernel memory as that is not supported
 * with Septel cards.
 */
static int septel_setfilter(pcap_t *p, struct bpf_program *fp) {
  if (!p)
    return -1;
  if (!fp) {
    strncpy(p->errbuf, "setfilter: No filter specified",
	    sizeof(p->errbuf));
    return -1;
  }

  /* Make our private copy of the filter */

  if (install_bpf_program(p, fp) < 0)
    return -1;

  return (0);
}

/*
 * We don't support non-blocking mode.  I'm not sure what we'd
 * do to support it and, given that we don't support select()/
 * poll()/epoll_wait()/kevent() etc., it probably doesn't
 * matter.
 */
static int
septel_getnonblock(pcap_t *p)
{
  fprintf(p->errbuf, PCAP_ERRBUF_SIZE, "Non-blocking mode not supported on Septel devices");
  return (-1);
}

static int
septel_setnonblock(pcap_t *p, int nonblock _U_)
{
  fprintf(p->errbuf, PCAP_ERRBUF_SIZE, "Non-blocking mode not supported on Septel devices");
  return (-1);
}

#ifdef SEPTEL_ONLY
/*
 * This libpcap build supports only Septel cards, not regular network
 * interfaces.
 */

/*
 * There are no regular interfaces, just Septel interfaces.
 */
int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
  return (0);
}

/*
 * Attempts to open a regular interface fail.
 */
pcap_t *
pcap_create_interface(const char *device, char *errbuf)
{
  pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
                "This version of libpcap only supports Septel cards");
  return (NULL);
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
  return (PCAP_VERSION_STRING " (Septel-only)");
}
#endif
