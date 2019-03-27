/*
 * Copyright (c) 2000 - 2002, 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>
#include "roken.h"

#ifdef __osf__
/* hate */
struct rtentry;
struct mbuf;
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif /* HAVE_SYS_SOCKIO_H */

#ifdef HAVE_NETINET_IN6_VAR_H
#include <netinet/in6_var.h>
#endif /* HAVE_NETINET_IN6_VAR_H */

#include <ifaddrs.h>

#ifdef __hpux
#define lifconf if_laddrconf
#define lifc_len iflc_len
#define lifc_buf iflc_buf
#define lifc_req iflc_req

#define lifreq if_laddrreq
#define lifr_addr iflr_addr
#define lifr_name iflr_name
#define lifr_dstaddr iflr_dstaddr
#define lifr_broadaddr iflr_broadaddr
#define lifr_flags iflr_flags
#define lifr_index iflr_index
#endif

#ifdef AF_NETLINK

/*
 * The linux - AF_NETLINK version of getifaddrs - from Usagi.
 * Linux does not return v6 addresses from SIOCGIFCONF.
 */

/* $USAGI: ifaddrs.c,v 1.18 2002/03/06 01:50:46 yoshfuji Exp $ */

/**************************************************************************
 * ifaddrs.c
 * Copyright (C)2000 Hideaki YOSHIFUJI, All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <string.h>
#include <time.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>     /* the L2 protocols */
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#define __set_errno(e) (errno = (e))
#define __close(fd) (close(fd))
#undef ifa_broadaddr
#define ifa_broadaddr ifa_dstaddr
#define IFA_NETMASK

/* ====================================================================== */
struct nlmsg_list{
    struct nlmsg_list *nlm_next;
    struct nlmsghdr *nlh;
    int size;
    time_t seq;
};

struct rtmaddr_ifamap {
  void *address;
  void *local;
#ifdef IFA_NETMASK
  void *netmask;
#endif
  void *broadcast;
#ifdef HAVE_IFADDRS_IFA_ANYCAST
  void *anycast;
#endif
  int address_len;
  int local_len;
#ifdef IFA_NETMASK
  int netmask_len;
#endif
  int broadcast_len;
#ifdef HAVE_IFADDRS_IFA_ANYCAST
  int anycast_len;
#endif
};

/* ====================================================================== */
static size_t
ifa_sa_len(sa_family_t family, int len)
{
  size_t size;
  switch(family){
  case AF_INET:
    size = sizeof(struct sockaddr_in);
    break;
  case AF_INET6:
    size = sizeof(struct sockaddr_in6);
    break;
  case AF_PACKET:
    size = (size_t)(((struct sockaddr_ll *)NULL)->sll_addr) + len;
    if (size < sizeof(struct sockaddr_ll))
      size = sizeof(struct sockaddr_ll);
    break;
  default:
    size = (size_t)(((struct sockaddr *)NULL)->sa_data) + len;
    if (size < sizeof(struct sockaddr))
      size = sizeof(struct sockaddr);
    break;
  }
  return size;
}

static void
ifa_make_sockaddr(sa_family_t family,
		  struct sockaddr *sa,
		  void *p, size_t len,
		  uint32_t scope, uint32_t scopeid)
{
  if (sa == NULL) return;
  switch(family){
  case AF_INET:
    memcpy(&((struct sockaddr_in*)sa)->sin_addr, (char *)p, len);
    break;
  case AF_INET6:
    memcpy(&((struct sockaddr_in6*)sa)->sin6_addr, (char *)p, len);
    if (IN6_IS_ADDR_LINKLOCAL(p) ||
	IN6_IS_ADDR_MC_LINKLOCAL(p)){
      ((struct sockaddr_in6*)sa)->sin6_scope_id = scopeid;
    }
    break;
  case AF_PACKET:
    memcpy(((struct sockaddr_ll*)sa)->sll_addr, (char *)p, len);
    ((struct sockaddr_ll*)sa)->sll_halen = len;
    break;
  default:
    memcpy(sa->sa_data, p, len);	/*XXX*/
    break;
  }
  sa->sa_family = family;
#ifdef HAVE_SOCKADDR_SA_LEN
  sa->sa_len = ifa_sa_len(family, len);
#endif
}

#ifndef IFA_NETMASK
static struct sockaddr *
ifa_make_sockaddr_mask(sa_family_t family,
		       struct sockaddr *sa,
		       uint32_t prefixlen)
{
  int i;
  char *p = NULL, c;
  uint32_t max_prefixlen = 0;

  if (sa == NULL) return NULL;
  switch(family){
  case AF_INET:
    memset(&((struct sockaddr_in*)sa)->sin_addr, 0, sizeof(((struct sockaddr_in*)sa)->sin_addr));
    p = (char *)&((struct sockaddr_in*)sa)->sin_addr;
    max_prefixlen = 32;
    break;
  case AF_INET6:
    memset(&((struct sockaddr_in6*)sa)->sin6_addr, 0, sizeof(((struct sockaddr_in6*)sa)->sin6_addr));
    p = (char *)&((struct sockaddr_in6*)sa)->sin6_addr;
#if 0	/* XXX: fill scope-id? */
    if (IN6_IS_ADDR_LINKLOCAL(p) ||
	IN6_IS_ADDR_MC_LINKLOCAL(p)){
      ((struct sockaddr_in6*)sa)->sin6_scope_id = scopeid;
    }
#endif
    max_prefixlen = 128;
    break;
  default:
    return NULL;
  }
  sa->sa_family = family;
#ifdef HAVE_SOCKADDR_SA_LEN
  sa->sa_len = ifa_sa_len(family, len);
#endif
  if (p){
    if (prefixlen > max_prefixlen)
      prefixlen = max_prefixlen;
    for (i=0; i<(prefixlen / 8); i++)
      *p++ = 0xff;
    c = 0xff;
    c <<= (8 - (prefixlen % 8));
    *p = c;
  }
  return sa;
}
#endif

/* ====================================================================== */
static int
nl_sendreq(int sd, int request, int flags, int *seq)
{
  char reqbuf[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
	      NLMSG_ALIGN(sizeof(struct rtgenmsg))];
  struct sockaddr_nl nladdr;
  struct nlmsghdr *req_hdr;
  struct rtgenmsg *req_msg;
  time_t t = time(NULL);

  if (seq) *seq = t;
  memset(&reqbuf, 0, sizeof(reqbuf));
  req_hdr = (struct nlmsghdr *)reqbuf;
  req_msg = (struct rtgenmsg *)NLMSG_DATA(req_hdr);
  req_hdr->nlmsg_len = NLMSG_LENGTH(sizeof(*req_msg));
  req_hdr->nlmsg_type = request;
  req_hdr->nlmsg_flags = flags | NLM_F_REQUEST;
  req_hdr->nlmsg_pid = 0;
  req_hdr->nlmsg_seq = t;
  req_msg->rtgen_family = AF_UNSPEC;
  memset(&nladdr, 0, sizeof(nladdr));
  nladdr.nl_family = AF_NETLINK;
  return (sendto(sd, (void *)req_hdr, req_hdr->nlmsg_len, 0,
		 (struct sockaddr *)&nladdr, sizeof(nladdr)));
}

static int
nl_recvmsg(int sd, int request, int seq,
	   void *buf, size_t buflen,
	   int *flags)
{
  struct msghdr msg;
  struct iovec iov = { buf, buflen };
  struct sockaddr_nl nladdr;
  int read_len;

  for (;;){
    msg.msg_name = (void *)&nladdr;
    msg.msg_namelen = sizeof(nladdr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    read_len = recvmsg(sd, &msg, 0);
    if ((read_len < 0 && errno == EINTR) || (msg.msg_flags & MSG_TRUNC))
      continue;
    if (flags) *flags = msg.msg_flags;
    break;
  }
  return read_len;
}

static int
nl_getmsg(int sd, int request, int seq,
	  struct nlmsghdr **nlhp,
	  int *done)
{
  struct nlmsghdr *nh;
  size_t bufsize = 65536, lastbufsize = 0;
  void *buff = NULL;
  int result = 0, read_size;
  int msg_flags;
  pid_t pid = getpid();
  for (;;){
    void *newbuff = realloc(buff, bufsize);
    if (newbuff == NULL || bufsize < lastbufsize) {
      result = -1;
      break;
    }
    buff = newbuff;
    result = read_size = nl_recvmsg(sd, request, seq, buff, bufsize, &msg_flags);
    if (read_size < 0 || (msg_flags & MSG_TRUNC)){
      lastbufsize = bufsize;
      bufsize *= 2;
      continue;
    }
    if (read_size == 0) break;
    nh = (struct nlmsghdr *)buff;
    for (nh = (struct nlmsghdr *)buff;
	 NLMSG_OK(nh, read_size);
	 nh = (struct nlmsghdr *)NLMSG_NEXT(nh, read_size)){
      if (nh->nlmsg_pid != pid ||
	  nh->nlmsg_seq != seq)
	continue;
      if (nh->nlmsg_type == NLMSG_DONE){
	(*done)++;
	break; /* ok */
      }
      if (nh->nlmsg_type == NLMSG_ERROR){
	struct nlmsgerr *nlerr = (struct nlmsgerr *)NLMSG_DATA(nh);
	result = -1;
	if (nh->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
	  __set_errno(EIO);
	else
	  __set_errno(-nlerr->error);
	break;
      }
    }
    break;
  }
  if (result < 0)
    if (buff){
      int saved_errno = errno;
      free(buff);
      __set_errno(saved_errno);
    }
  *nlhp = (struct nlmsghdr *)buff;
  return result;
}

static int
nl_getlist(int sd, int seq,
	   int request,
	   struct nlmsg_list **nlm_list,
	   struct nlmsg_list **nlm_end)
{
  struct nlmsghdr *nlh = NULL;
  int status;
  int done = 0;
  int tries = 3;

 try_again:
  status = nl_sendreq(sd, request, NLM_F_ROOT|NLM_F_MATCH, &seq);
  if (status < 0)
    return status;
  if (seq == 0)
    seq = (int)time(NULL);
  while(!done){
    struct pollfd pfd;

    pfd.fd = sd;
    pfd.events = POLLIN | POLLPRI;
    pfd.revents = 0;
    status = poll(&pfd, 1, 1000);
    if (status < 0)
	return status;
    else if (status == 0) {
	seq++;
	if (tries-- > 0)
	    goto try_again;
	return -1;
    }

    status = nl_getmsg(sd, request, seq, &nlh, &done);
    if (status < 0)
      return status;
    if (nlh){
      struct nlmsg_list *nlm_next = (struct nlmsg_list *)malloc(sizeof(struct nlmsg_list));
      if (nlm_next == NULL){
	int saved_errno = errno;
	free(nlh);
	__set_errno(saved_errno);
	status = -1;
      } else {
	nlm_next->nlm_next = NULL;
	nlm_next->nlh = (struct nlmsghdr *)nlh;
	nlm_next->size = status;
	nlm_next->seq = seq;
	if (*nlm_list == NULL){
	  *nlm_list = nlm_next;
	  *nlm_end = nlm_next;
	} else {
	  (*nlm_end)->nlm_next = nlm_next;
	  *nlm_end = nlm_next;
	}
      }
    }
  }
  return status >= 0 ? seq : status;
}

/* ---------------------------------------------------------------------- */
static void
free_nlmsglist(struct nlmsg_list *nlm0)
{
  struct nlmsg_list *nlm, *nlm_next;
  int saved_errno;
  if (!nlm0)
    return;
  saved_errno = errno;
  for (nlm=nlm0; nlm; nlm=nlm_next){
    if (nlm->nlh)
      free(nlm->nlh);
    nlm_next=nlm->nlm_next;
    free(nlm);
  }
  __set_errno(saved_errno);
}

static void
free_data(void *data, void *ifdata)
{
  int saved_errno = errno;
  if (data != NULL) free(data);
  if (ifdata != NULL) free(ifdata);
  __set_errno(saved_errno);
}

/* ---------------------------------------------------------------------- */
static void
nl_close(int sd)
{
  int saved_errno = errno;
  if (sd >= 0) __close(sd);
  __set_errno(saved_errno);
}

/* ---------------------------------------------------------------------- */
static int
nl_open(void)
{
  struct sockaddr_nl nladdr;
  int sd;

  sd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sd < 0) return -1;
  memset(&nladdr, 0, sizeof(nladdr));
  nladdr.nl_family = AF_NETLINK;
  if (bind(sd, (struct sockaddr*)&nladdr, sizeof(nladdr)) < 0){
    nl_close(sd);
    return -1;
  }
  return sd;
}

/* ====================================================================== */
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_getifaddrs(struct ifaddrs **ifap)
{
  int sd;
  struct nlmsg_list *nlmsg_list, *nlmsg_end, *nlm;
  /* - - - - - - - - - - - - - - - */
  int icnt;
  size_t dlen, xlen, nlen;
  uint32_t max_ifindex = 0;

  pid_t pid = getpid();
  int seq;
  int result;
  int build     ; /* 0 or 1 */

/* ---------------------------------- */
  /* initialize */
  icnt = dlen = xlen = nlen = 0;
  nlmsg_list = nlmsg_end = NULL;

  if (ifap)
    *ifap = NULL;

/* ---------------------------------- */
  /* open socket and bind */
  sd = nl_open();
  if (sd < 0)
    return -1;

/* ---------------------------------- */
   /* gather info */
  if ((seq = nl_getlist(sd, 0, RTM_GETLINK,
			&nlmsg_list, &nlmsg_end)) < 0){
    free_nlmsglist(nlmsg_list);
    nl_close(sd);
    return -1;
  }
  if ((seq = nl_getlist(sd, seq+1, RTM_GETADDR,
			&nlmsg_list, &nlmsg_end)) < 0){
    free_nlmsglist(nlmsg_list);
    nl_close(sd);
    return -1;
  }

/* ---------------------------------- */
  /* Estimate size of result buffer and fill it */
  for (build=0; build<=1; build++){
    struct ifaddrs *ifl = NULL, *ifa = NULL;
    struct nlmsghdr *nlh, *nlh0;
    char *data = NULL, *xdata = NULL;
    void *ifdata = NULL;
    char *ifname = NULL, **iflist = NULL;
    uint16_t *ifflist = NULL;
    struct rtmaddr_ifamap ifamap;

    if (build){
      data = calloc(1,
		    NLMSG_ALIGN(sizeof(struct ifaddrs[icnt]))
		    + dlen + xlen + nlen);
      ifa = (struct ifaddrs *)data;
      ifdata = calloc(1,
		      NLMSG_ALIGN(sizeof(char *[max_ifindex+1]))
		      + NLMSG_ALIGN(sizeof(uint16_t [max_ifindex+1])));
      if (ifap != NULL)
	*ifap = (ifdata != NULL) ? ifa : NULL;
      else{
	free_data(data, ifdata);
	result = 0;
	break;
      }
      if (data == NULL || ifdata == NULL){
	free_data(data, ifdata);
	result = -1;
	break;
      }
      ifl = NULL;
      data += NLMSG_ALIGN(sizeof(struct ifaddrs)) * icnt;
      xdata = data + dlen;
      ifname = xdata + xlen;
      iflist = ifdata;
      ifflist = (uint16_t *)(((char *)iflist) + NLMSG_ALIGN(sizeof(char *[max_ifindex+1])));
    }

    for (nlm=nlmsg_list; nlm; nlm=nlm->nlm_next){
      int nlmlen = nlm->size;
      if (!(nlh0 = nlm->nlh))
	continue;
      for (nlh = nlh0;
	   NLMSG_OK(nlh, nlmlen);
	   nlh=NLMSG_NEXT(nlh,nlmlen)){
	struct ifinfomsg *ifim = NULL;
	struct ifaddrmsg *ifam = NULL;
	struct rtattr *rta;

	size_t nlm_struct_size = 0;
	sa_family_t nlm_family = 0;
	uint32_t nlm_scope = 0, nlm_index = 0;
	size_t sockaddr_size = 0;
	uint32_t nlm_prefixlen = 0;
	size_t rtasize;

	memset(&ifamap, 0, sizeof(ifamap));

	/* check if the message is what we want */
	if (nlh->nlmsg_pid != pid ||
	    nlh->nlmsg_seq != nlm->seq)
	  continue;
	if (nlh->nlmsg_type == NLMSG_DONE){
	  break; /* ok */
	}
	switch (nlh->nlmsg_type){
	case RTM_NEWLINK:
	  ifim = (struct ifinfomsg *)NLMSG_DATA(nlh);
	  nlm_struct_size = sizeof(*ifim);
	  nlm_family = ifim->ifi_family;
	  nlm_scope = 0;
	  nlm_index = ifim->ifi_index;
	  nlm_prefixlen = 0;
	  if (build)
	    ifflist[nlm_index] = ifa->ifa_flags = ifim->ifi_flags;
	  break;
	case RTM_NEWADDR:
	  ifam = (struct ifaddrmsg *)NLMSG_DATA(nlh);
	  nlm_struct_size = sizeof(*ifam);
	  nlm_family = ifam->ifa_family;
	  nlm_scope = ifam->ifa_scope;
	  nlm_index = ifam->ifa_index;
	  nlm_prefixlen = ifam->ifa_prefixlen;
	  if (build)
	    ifa->ifa_flags = ifflist[nlm_index];
	  break;
	default:
	  continue;
	}

	if (!build){
	  if (max_ifindex < nlm_index)
	    max_ifindex = nlm_index;
	} else {
	  if (ifl != NULL)
	    ifl->ifa_next = ifa;
	}

	rtasize = NLMSG_PAYLOAD(nlh, nlmlen) - NLMSG_ALIGN(nlm_struct_size);
	for (rta = (struct rtattr *)(((char *)NLMSG_DATA(nlh)) + NLMSG_ALIGN(nlm_struct_size));
	     RTA_OK(rta, rtasize);
	     rta = RTA_NEXT(rta, rtasize)){
	  struct sockaddr **sap = NULL;
	  void *rtadata = RTA_DATA(rta);
	  size_t rtapayload = RTA_PAYLOAD(rta);
	  socklen_t sa_len;

	  switch(nlh->nlmsg_type){
	  case RTM_NEWLINK:
	    switch(rta->rta_type){
	    case IFLA_ADDRESS:
	    case IFLA_BROADCAST:
	      if (build){
		sap = (rta->rta_type == IFLA_ADDRESS) ? &ifa->ifa_addr : &ifa->ifa_broadaddr;
		*sap = (struct sockaddr *)data;
	      }
	      sa_len = ifa_sa_len(AF_PACKET, rtapayload);
	      if (rta->rta_type == IFLA_ADDRESS)
		sockaddr_size = NLMSG_ALIGN(sa_len);
	      if (!build){
		dlen += NLMSG_ALIGN(sa_len);
	      } else {
		memset(*sap, 0, sa_len);
		ifa_make_sockaddr(AF_PACKET, *sap, rtadata,rtapayload, 0,0);
		((struct sockaddr_ll *)*sap)->sll_ifindex = nlm_index;
		((struct sockaddr_ll *)*sap)->sll_hatype = ifim->ifi_type;
		data += NLMSG_ALIGN(sa_len);
	      }
	      break;
	    case IFLA_IFNAME:/* Name of Interface */
	      if (!build)
		nlen += NLMSG_ALIGN(rtapayload + 1);
	      else{
		ifa->ifa_name = ifname;
		if (iflist[nlm_index] == NULL)
		  iflist[nlm_index] = ifa->ifa_name;
		strncpy(ifa->ifa_name, rtadata, rtapayload);
		ifa->ifa_name[rtapayload] = '\0';
		ifname += NLMSG_ALIGN(rtapayload + 1);
	      }
	      break;
	    case IFLA_STATS:/* Statistics of Interface */
	      if (!build)
		xlen += NLMSG_ALIGN(rtapayload);
	      else{
		ifa->ifa_data = xdata;
		memcpy(ifa->ifa_data, rtadata, rtapayload);
		xdata += NLMSG_ALIGN(rtapayload);
	      }
	      break;
	    case IFLA_UNSPEC:
	      break;
	    case IFLA_MTU:
	      break;
	    case IFLA_LINK:
	      break;
	    case IFLA_QDISC:
	      break;
	    default:
	      break;
	    }
	    break;
	  case RTM_NEWADDR:
	    if (nlm_family == AF_PACKET) break;
	    switch(rta->rta_type){
	    case IFA_ADDRESS:
		ifamap.address = rtadata;
		ifamap.address_len = rtapayload;
		break;
	    case IFA_LOCAL:
		ifamap.local = rtadata;
		ifamap.local_len = rtapayload;
		break;
	    case IFA_BROADCAST:
		ifamap.broadcast = rtadata;
		ifamap.broadcast_len = rtapayload;
		break;
#ifdef HAVE_IFADDRS_IFA_ANYCAST
	    case IFA_ANYCAST:
		ifamap.anycast = rtadata;
		ifamap.anycast_len = rtapayload;
		break;
#endif
	    case IFA_LABEL:
	      if (!build)
		nlen += NLMSG_ALIGN(rtapayload + 1);
	      else{
		ifa->ifa_name = ifname;
		if (iflist[nlm_index] == NULL)
		  iflist[nlm_index] = ifname;
		strncpy(ifa->ifa_name, rtadata, rtapayload);
		ifa->ifa_name[rtapayload] = '\0';
		ifname += NLMSG_ALIGN(rtapayload + 1);
	      }
	      break;
	    case IFA_UNSPEC:
	      break;
	    case IFA_CACHEINFO:
	      break;
	    default:
	      break;
	    }
	  }
	}
	if (nlh->nlmsg_type == RTM_NEWADDR &&
	    nlm_family != AF_PACKET) {
	  if (!ifamap.local) {
	    ifamap.local = ifamap.address;
	    ifamap.local_len = ifamap.address_len;
	  }
	  if (!ifamap.address) {
	    ifamap.address = ifamap.local;
	    ifamap.address_len = ifamap.local_len;
	  }
	  if (ifamap.address_len != ifamap.local_len ||
	      (ifamap.address != NULL &&
	       memcmp(ifamap.address, ifamap.local, ifamap.address_len))) {
	    /* p2p; address is peer and local is ours */
	    ifamap.broadcast = ifamap.address;
	    ifamap.broadcast_len = ifamap.address_len;
	    ifamap.address = ifamap.local;
	    ifamap.address_len = ifamap.local_len;
	  }
	  if (ifamap.address) {
#ifndef IFA_NETMASK
	    sockaddr_size = NLMSG_ALIGN(ifa_sa_len(nlm_family,ifamap.address_len));
#endif
	    if (!build)
	      dlen += NLMSG_ALIGN(ifa_sa_len(nlm_family,ifamap.address_len));
	    else {
	      ifa->ifa_addr = (struct sockaddr *)data;
	      ifa_make_sockaddr(nlm_family, ifa->ifa_addr, ifamap.address, ifamap.address_len,
				nlm_scope, nlm_index);
	      data += NLMSG_ALIGN(ifa_sa_len(nlm_family, ifamap.address_len));
	    }
	  }
#ifdef IFA_NETMASK
	  if (ifamap.netmask) {
	    if (!build)
	      dlen += NLMSG_ALIGN(ifa_sa_len(nlm_family,ifamap.netmask_len));
	    else {
	      ifa->ifa_netmask = (struct sockaddr *)data;
	      ifa_make_sockaddr(nlm_family, ifa->ifa_netmask, ifamap.netmask, ifamap.netmask_len,
				nlm_scope, nlm_index);
	      data += NLMSG_ALIGN(ifa_sa_len(nlm_family, ifamap.netmask_len));
	    }
	  }
#endif
	  if (ifamap.broadcast) {
	    if (!build)
	      dlen += NLMSG_ALIGN(ifa_sa_len(nlm_family,ifamap.broadcast_len));
	    else {
	      ifa->ifa_broadaddr = (struct sockaddr *)data;
	      ifa_make_sockaddr(nlm_family, ifa->ifa_broadaddr, ifamap.broadcast, ifamap.broadcast_len,
				nlm_scope, nlm_index);
	      data += NLMSG_ALIGN(ifa_sa_len(nlm_family, ifamap.broadcast_len));
	    }
	  }
#ifdef HAVE_IFADDRS_IFA_ANYCAST
	  if (ifamap.anycast) {
	    if (!build)
	      dlen += NLMSG_ALIGN(ifa_sa_len(nlm_family,ifamap.anycast_len));
	    else {
	      ifa->ifa_anycast = (struct sockaddr *)data;
	      ifa_make_sockaddr(nlm_family, ifa->ifa_anyaddr, ifamap.anycast, ifamap.anycast_len,
				nlm_scope, nlm_index);
	      data += NLMSG_ALIGN(ifa_sa_len(nlm_family, ifamap.anycast_len));
	    }
	  }
#endif
	}
	if (!build){
#ifndef IFA_NETMASK
	  dlen += sockaddr_size;
#endif
	  icnt++;
	} else {
	  if (ifa->ifa_name == NULL)
	    ifa->ifa_name = iflist[nlm_index];
#ifndef IFA_NETMASK
	  if (ifa->ifa_addr &&
	      ifa->ifa_addr->sa_family != AF_UNSPEC &&
	      ifa->ifa_addr->sa_family != AF_PACKET){
	    ifa->ifa_netmask = (struct sockaddr *)data;
	    ifa_make_sockaddr_mask(ifa->ifa_addr->sa_family, ifa->ifa_netmask, nlm_prefixlen);
	  }
	  data += sockaddr_size;
#endif
	  ifl = ifa++;
	}
      }
    }
    if (!build){
      if (icnt == 0 && (dlen + nlen + xlen == 0)){
	if (ifap != NULL)
	  *ifap = NULL;
	break; /* cannot found any addresses */
      }
    }
    else
      free_data(NULL, ifdata);
  }

/* ---------------------------------- */
  /* Finalize */
  free_nlmsglist(nlmsg_list);
  nl_close(sd);
  return 0;
}

void ROKEN_LIB_FUNCTION
rk_freeifaddrs(struct ifaddrs *ifp)
{
    /* AF_NETLINK method uses a single allocation for all interfaces */
    free(ifp);
}

#else /* !AF_NETLINK */

/*
 * The generic SIOCGIFCONF version.
 */

static int
getifaddrs2(struct ifaddrs **ifap,
	    int af, int siocgifconf, int siocgifflags,
	    size_t ifreq_sz)
{
    int ret;
    int fd;
    size_t buf_size;
    char *buf;
    struct ifconf ifconf;
    char *p;
    size_t sz;
    struct sockaddr sa_zero;
    struct ifreq *ifr;
    struct ifaddrs *start = NULL, **end = &start;

    buf = NULL;

    memset (&sa_zero, 0, sizeof(sa_zero));
    fd = socket(af, SOCK_DGRAM, 0);
    if (fd < 0)
	return -1;

    buf_size = 8192;
    for (;;) {
	buf = calloc(1, buf_size);
	if (buf == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}
	ifconf.ifc_len = buf_size;
	ifconf.ifc_buf = buf;

	/*
	 * Solaris returns EINVAL when the buffer is too small.
	 */
	if (ioctl (fd, siocgifconf, &ifconf) < 0 && errno != EINVAL) {
	    ret = errno;
	    goto error_out;
	}
	/*
	 * Can the difference between a full and a overfull buf
	 * be determined?
	 */

	if (ifconf.ifc_len < buf_size)
	    break;
	free (buf);
	buf_size *= 2;
    }

    for (p = ifconf.ifc_buf;
	 p < ifconf.ifc_buf + ifconf.ifc_len;
	 p += sz) {
	struct ifreq ifreq;
	struct sockaddr *sa;
	size_t salen;

	ifr = (struct ifreq *)p;
	sa  = &ifr->ifr_addr;

	sz = ifreq_sz;
	salen = sizeof(struct sockaddr);
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	salen = sa->sa_len;
	sz = max(sz, sizeof(ifr->ifr_name) + sa->sa_len);
#endif
#ifdef SA_LEN
	salen = SA_LEN(sa);
	sz = max(sz, sizeof(ifr->ifr_name) + SA_LEN(sa));
#endif
	memset (&ifreq, 0, sizeof(ifreq));
	memcpy (ifreq.ifr_name, ifr->ifr_name, sizeof(ifr->ifr_name));

	if (ioctl(fd, siocgifflags, &ifreq) < 0) {
	    ret = errno;
	    goto error_out;
	}

	*end = malloc(sizeof(**end));
	if (*end == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}

	(*end)->ifa_next = NULL;
	(*end)->ifa_name = strdup(ifr->ifr_name);
	if ((*end)->ifa_name == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}
	(*end)->ifa_flags = ifreq.ifr_flags;
	(*end)->ifa_addr = malloc(salen);
	if ((*end)->ifa_addr == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}
	memcpy((*end)->ifa_addr, sa, salen);
	(*end)->ifa_netmask = NULL;

#if 0
	/* fix these when we actually need them */
	if(ifreq.ifr_flags & IFF_BROADCAST) {
	    (*end)->ifa_broadaddr = malloc(sizeof(ifr->ifr_broadaddr));
	    if ((*end)->ifa_broadaddr == NULL) {
		ret = ENOMEM;
		goto error_out;
	    }
	    memcpy((*end)->ifa_broadaddr, &ifr->ifr_broadaddr,
		   sizeof(ifr->ifr_broadaddr));
	} else if(ifreq.ifr_flags & IFF_POINTOPOINT) {
	    (*end)->ifa_dstaddr = malloc(sizeof(ifr->ifr_dstaddr));
	    if ((*end)->ifa_dstaddr == NULL) {
		ret = ENOMEM;
		goto error_out;
	    }
	    memcpy((*end)->ifa_dstaddr, &ifr->ifr_dstaddr,
		   sizeof(ifr->ifr_dstaddr));
	} else
	    (*end)->ifa_dstaddr = NULL;
#else
	    (*end)->ifa_dstaddr = NULL;
#endif

	(*end)->ifa_data = NULL;

	end = &(*end)->ifa_next;

    }
    *ifap = start;
    close(fd);
    free(buf);
    return 0;
  error_out:
    rk_freeifaddrs(start);
    close(fd);
    free(buf);
    errno = ret;
    return -1;
}

#if defined(HAVE_IPV6) && defined(SIOCGLIFCONF) && defined(SIOCGLIFFLAGS)
static int
getlifaddrs2(struct ifaddrs **ifap,
	     int af, int siocgifconf, int siocgifflags,
	     size_t ifreq_sz)
{
    int ret;
    int fd;
    size_t buf_size;
    char *buf;
    struct lifconf ifconf;
    char *p;
    size_t sz;
    struct sockaddr sa_zero;
    struct lifreq *ifr;
    struct ifaddrs *start = NULL, **end = &start;

    buf = NULL;

    memset (&sa_zero, 0, sizeof(sa_zero));
    fd = socket(af, SOCK_DGRAM, 0);
    if (fd < 0)
	return -1;

    buf_size = 8192;
    for (;;) {
	buf = calloc(1, buf_size);
	if (buf == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}
#ifndef __hpux
	ifconf.lifc_family = af;
	ifconf.lifc_flags  = 0;
#endif
	ifconf.lifc_len    = buf_size;
	ifconf.lifc_buf    = buf;

	/*
	 * Solaris returns EINVAL when the buffer is too small.
	 */
	if (ioctl (fd, siocgifconf, &ifconf) < 0 && errno != EINVAL) {
	    ret = errno;
	    goto error_out;
	}
	/*
	 * Can the difference between a full and a overfull buf
	 * be determined?
	 */

	if (ifconf.lifc_len < buf_size)
	    break;
	free (buf);
	buf_size *= 2;
    }

    for (p = ifconf.lifc_buf;
	 p < ifconf.lifc_buf + ifconf.lifc_len;
	 p += sz) {
	struct lifreq ifreq;
	struct sockaddr_storage *sa;
	size_t salen;

	ifr = (struct lifreq *)p;
	sa  = &ifr->lifr_addr;

	sz = ifreq_sz;
	salen = sizeof(struct sockaddr_storage);
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	salen = sa->sa_len;
	sz = max(sz, sizeof(ifr->ifr_name) + sa->sa_len);
#endif
#ifdef SA_LEN
	salen = SA_LEN(sa);
	sz = max(sz, sizeof(ifr->ifr_name) + SA_LEN(sa));
#endif
	memset (&ifreq, 0, sizeof(ifreq));
	memcpy (ifreq.lifr_name, ifr->lifr_name, sizeof(ifr->lifr_name));

	if (ioctl(fd, siocgifflags, &ifreq) < 0) {
	    ret = errno;
	    goto error_out;
	}

	*end = malloc(sizeof(**end));
	if (*end == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}

	(*end)->ifa_next = NULL;
	(*end)->ifa_name = strdup(ifr->lifr_name);
	if ((*end)->ifa_name == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}
	(*end)->ifa_flags = ifreq.lifr_flags;
	(*end)->ifa_addr = malloc(salen);
	if ((*end)->ifa_addr == NULL) {
	    ret = ENOMEM;
	    goto error_out;
	}
	memcpy((*end)->ifa_addr, sa, salen);
	(*end)->ifa_netmask = NULL;

#if 0
	/* fix these when we actually need them */
	if(ifreq.ifr_flags & IFF_BROADCAST) {
	    (*end)->ifa_broadaddr = malloc(sizeof(ifr->ifr_broadaddr));
	    if ((*end)->ifa_broadaddr == NULL) {
		ret = ENOMEM;
		goto error_out;
	    }
	    memcpy((*end)->ifa_broadaddr, &ifr->ifr_broadaddr,
		   sizeof(ifr->ifr_broadaddr));
	} else if(ifreq.ifr_flags & IFF_POINTOPOINT) {
	    (*end)->ifa_dstaddr = malloc(sizeof(ifr->ifr_dstaddr));
	    if ((*end)->ifa_dstaddr == NULL) {
		ret = ENOMEM;
		goto error_out;
	    }
	    memcpy((*end)->ifa_dstaddr, &ifr->ifr_dstaddr,
		   sizeof(ifr->ifr_dstaddr));
	} else
	    (*end)->ifa_dstaddr = NULL;
#else
	    (*end)->ifa_dstaddr = NULL;
#endif

	(*end)->ifa_data = NULL;

	end = &(*end)->ifa_next;

    }
    *ifap = start;
    close(fd);
    free(buf);
    return 0;
  error_out:
    rk_freeifaddrs(start);
    close(fd);
    free(buf);
    errno = ret;
    return -1;
}
#endif /* defined(HAVE_IPV6) && defined(SIOCGLIFCONF) && defined(SIOCGLIFFLAGS) */

/**
 * Join two struct ifaddrs lists by appending supp to base.
 * Either may be NULL. The new list head (usually base) will be
 * returned.
 */
static struct ifaddrs *
append_ifaddrs(struct ifaddrs *base, struct ifaddrs *supp) {
    if (!base)
	return supp;

    if (!supp)
	return base;

    while (base->ifa_next)
	base = base->ifa_next;

    base->ifa_next = supp;

    return base;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_getifaddrs(struct ifaddrs **ifap)
{
    int ret = -1;
    errno = ENXIO;
#if defined(AF_INET6) && defined(SIOCGIF6CONF) && defined(SIOCGIF6FLAGS)
    if (ret)
	ret = getifaddrs2 (ifap, AF_INET6, SIOCGIF6CONF, SIOCGIF6FLAGS,
			   sizeof(struct in6_ifreq));
#endif
#if defined(HAVE_IPV6) && defined(SIOCGLIFCONF) && defined(SIOCGLIFFLAGS)
    /* Do IPv6 and IPv4 queries separately then join the result.
     *
     * HP-UX only returns IPv6 addresses using SIOCGLIFCONF,
     * SIOCGIFCONF has to be used for IPv4 addresses. The result is then
     * merged.
     *
     * Solaris needs particular care, because a SIOCGLIFCONF lookup using
     * AF_UNSPEC can fail in a Zone requiring an AF_INET lookup, so we just
     * do them separately the same as for HP-UX. See
     * http://repo.or.cz/w/heimdal.git/commitdiff/76afc31e9ba2f37e64c70adc006ade9e37e9ef73
     */
    if (ret) {
	int v6err, v4err;
	struct ifaddrs *v6addrs, *v4addrs;

	v6err = getlifaddrs2 (&v6addrs, AF_INET6, SIOCGLIFCONF, SIOCGLIFFLAGS,
			    sizeof(struct lifreq));
	v4err = getifaddrs2 (&v4addrs, AF_INET, SIOCGIFCONF, SIOCGIFFLAGS,
			    sizeof(struct ifreq));
	if (v6err)
	    v6addrs = NULL;
	if (v4err)
	    v4addrs = NULL;

	if (v6addrs) {
	    if (v4addrs)
		*ifap = append_ifaddrs(v6addrs, v4addrs);
	    else
		*ifap = v6addrs;
	} else if (v4addrs) {
	    *ifap = v4addrs;
	} else {
	    *ifap = NULL;
	}

	ret = (v6err || v4err) ? -1 : 0;
    }
#endif
#if defined(HAVE_IPV6) && defined(SIOCGIFCONF)
    if (ret)
	ret = getifaddrs2 (ifap, AF_INET6, SIOCGIFCONF, SIOCGIFFLAGS,
			   sizeof(struct ifreq));
#endif
#if defined(AF_INET) && defined(SIOCGIFCONF) && defined(SIOCGIFFLAGS)
    if (ret)
	ret = getifaddrs2 (ifap, AF_INET, SIOCGIFCONF, SIOCGIFFLAGS,
			   sizeof(struct ifreq));
#endif
    return ret;
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_freeifaddrs(struct ifaddrs *ifp)
{
    struct ifaddrs *p, *q;

    for(p = ifp; p; ) {
	free(p->ifa_name);
	if(p->ifa_addr)
	    free(p->ifa_addr);
	if(p->ifa_dstaddr)
	    free(p->ifa_dstaddr);
	if(p->ifa_netmask)
	    free(p->ifa_netmask);
	if(p->ifa_data)
	    free(p->ifa_data);
	q = p;
	p = p->ifa_next;
	free(q);
    }
}

#endif /* !AF_NETLINK */

#ifdef TEST

void
print_addr(const char *s, struct sockaddr *sa)
{
    int i;
    printf("  %s=%d/", s, sa->sa_family);
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
    for(i = 0; i < sa->sa_len - ((long)sa->sa_data - (long)&sa->sa_family); i++)
	printf("%02x", ((unsigned char*)sa->sa_data)[i]);
#else
    for(i = 0; i < sizeof(sa->sa_data); i++)
	printf("%02x", ((unsigned char*)sa->sa_data)[i]);
#endif
    printf("\n");
}

void
print_ifaddrs(struct ifaddrs *x)
{
    struct ifaddrs *p;

    for(p = x; p; p = p->ifa_next) {
	printf("%s\n", p->ifa_name);
	printf("  flags=%x\n", p->ifa_flags);
	if(p->ifa_addr)
	    print_addr("addr", p->ifa_addr);
	if(p->ifa_dstaddr)
	    print_addr("dstaddr", p->ifa_dstaddr);
	if(p->ifa_netmask)
	    print_addr("netmask", p->ifa_netmask);
	printf("  %p\n", p->ifa_data);
    }
}

int
main()
{
    struct ifaddrs *a = NULL, *b;
    getifaddrs2(&a, AF_INET, SIOCGIFCONF, SIOCGIFFLAGS, sizeof(struct ifreq));
    print_ifaddrs(a);
    printf("---\n");
    getifaddrs(&b);
    print_ifaddrs(b);
    return 0;
}
#endif
