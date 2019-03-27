/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/amd/srvr_nfs.c
 *
 */

/*
 * NFS server modeling
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/*
 * Number of pings allowed to fail before host is declared down
 * - three-fifths of the allowed mount time...
 */
#define	MAX_ALLOWED_PINGS	(3 + /* for luck ... */ 1)

/*
 * How often to ping when starting a new server
 */
#define	FAST_NFS_PING		3

#if (FAST_NFS_PING * MAX_ALLOWED_PINGS) >= ALLOWED_MOUNT_TIME
# error: sanity check failed in srvr_nfs.c
/*
 * you cannot do things this way...
 * sufficient fast pings must be given the chance to fail
 * within the allowed mount time
 */
#endif /* (FAST_NFS_PING * MAX_ALLOWED_PINGS) >= ALLOWED_MOUNT_TIME */

/* structures and typedefs */
typedef struct nfs_private {
  u_short np_mountd;		/* Mount daemon port number */
  char np_mountd_inval;		/* Port *may* be invalid */
  				/* 'Y' invalid, 'N' valid, 'P' permanent */
  int np_ping;			/* Number of failed ping attempts */
  time_t np_ttl;		/* Time when server is thought dead */
  int np_xid;			/* RPC transaction id for pings */
  int np_error;			/* Error during portmap request */
} nfs_private;

/* globals */
qelem nfs_srvr_list = {&nfs_srvr_list, &nfs_srvr_list};

/* statics */
static int global_xid;		/* For NFS pings */
#define	XID_ALLOC()		(++global_xid)

#if defined(HAVE_FS_NFS4)
# define NUM_NFS_VERS 3
#elif defined(HAVE_FS_NFS3)
# define NUM_NFS_VERS 2
#else  /* not HAVE_FS_NFS3 */
# define NUM_NFS_VERS 1
#endif /* not HAVE_FS_NFS3 */
static int ping_len[NUM_NFS_VERS];
static char ping_buf[NUM_NFS_VERS][sizeof(struct rpc_msg) + 32];

#if defined(MNTTAB_OPT_PROTO) || defined(HAVE_FS_NFS3)
/*
 * Protocols we know about, in order of preference.
 *
 * Note that Solaris 8 and newer NetBSD systems are switching to UDP first,
 * so this order may have to be adjusted for Amd in the future once more
 * vendors make that change. -Erez 11/24/2000
 *
 * Or we might simply make this is a platform-specific order. -Ion 09/13/2003
 */
static char *protocols[] = { "tcp", "udp", NULL };
#endif /* defined(MNTTAB_OPT_PROTO) || defined(HAVE_FS_NFS3) */

/* forward definitions */
static void nfs_keepalive(voidp);


/*
 * Flush cached data for an fserver (or for all, if fs==NULL)
 */
void
flush_srvr_nfs_cache(fserver *fs)
{
  fserver *fs2 = NULL;

  ITER(fs2, fserver, &nfs_srvr_list) {
    if (fs == NULL || fs == fs2) {
      nfs_private *np = (nfs_private *) fs2->fs_private;
      if (np && np->np_mountd_inval != 'P') {
	np->np_mountd_inval = 'Y';
	np->np_error = -1;
      }
    }
  }
}


/*
 * Startup the NFS ping for a particular version.
 */
static void
create_ping_payload(u_long nfs_version)
{
  XDR ping_xdr;
  struct rpc_msg ping_msg;

  /*
   * Non nfs mounts like /afs/glue.umd.edu have ended up here.
   */
  if (nfs_version == 0) {
    nfs_version = NFS_VERSION;
    plog(XLOG_WARNING, "%s: nfs_version = 0, changed to 2", __func__);
  } else
    plog(XLOG_INFO, "%s: nfs_version: %d", __func__, (int) nfs_version);

  rpc_msg_init(&ping_msg, NFS_PROGRAM, nfs_version, NFSPROC_NULL);

  /*
   * Create an XDR endpoint
   */
  xdrmem_create(&ping_xdr, ping_buf[nfs_version - NFS_VERSION], sizeof(ping_buf[0]), XDR_ENCODE);

  /*
   * Create the NFS ping message
   */
  if (!xdr_callmsg(&ping_xdr, &ping_msg)) {
    plog(XLOG_ERROR, "Couldn't create ping RPC message");
    going_down(3);
    return;
  }
  /*
   * Find out how long it is
   */
  ping_len[nfs_version - NFS_VERSION] = xdr_getpos(&ping_xdr);

  /*
   * Destroy the XDR endpoint - we don't need it anymore
   */
  xdr_destroy(&ping_xdr);
}


/*
 * Called when a portmap reply arrives
 */
static void
got_portmap(voidp pkt, int len, struct sockaddr_in *sa, struct sockaddr_in *ia, voidp idv, int done)
{
  fserver *fs2 = (fserver *) idv;
  fserver *fs = NULL;

  /*
   * Find which fileserver we are talking about
   */
  ITER(fs, fserver, &nfs_srvr_list)
    if (fs == fs2)
      break;

  if (fs == fs2) {
    u_long port = 0;	/* XXX - should be short but protocol is naff */
    int error = done ? pickup_rpc_reply(pkt, len, (voidp) &port, (XDRPROC_T_TYPE) xdr_u_long) : -1;
    nfs_private *np = (nfs_private *) fs->fs_private;

    if (!error && port) {
      dlog("got port (%d) for mountd on %s", (int) port, fs->fs_host);
      /*
       * Grab the port number.  Portmap sends back
       * an u_long in native ordering, so it
       * needs converting to a u_short in
       * network ordering.
       */
      np->np_mountd = htons((u_short) port);
      np->np_mountd_inval = 'N';
      np->np_error = 0;
    } else {
      dlog("Error fetching port for mountd on %s", fs->fs_host);
      dlog("\t error=%d, port=%d", error, (int) port);
      /*
       * Almost certainly no mountd running on remote host
       */
      np->np_error = error ? error : ETIMEDOUT;
    }

    if (fs->fs_flags & FSF_WANT)
      wakeup_srvr(fs);
  } else if (done) {
    dlog("Got portmap for old port request");
  } else {
    dlog("portmap request timed out");
  }
}


/*
 * Obtain portmap information
 */
static int
call_portmap(fserver *fs, AUTH *auth, u_long prog, u_long vers, u_long prot)
{
  struct rpc_msg pmap_msg;
  int len;
  char iobuf[UDPMSGSIZE];
  int error;
  struct pmap pmap;

  rpc_msg_init(&pmap_msg, PMAPPROG, PMAPVERS, PMAPPROC_NULL);
  pmap.pm_prog = prog;
  pmap.pm_vers = vers;
  pmap.pm_prot = prot;
  pmap.pm_port = 0;
  len = make_rpc_packet(iobuf,
			sizeof(iobuf),
			PMAPPROC_GETPORT,
			&pmap_msg,
			(voidp) &pmap,
			(XDRPROC_T_TYPE) xdr_pmap,
			auth);
  if (len > 0) {
    struct sockaddr_in sin;
    memset((voidp) &sin, 0, sizeof(sin));
    sin = *fs->fs_ip;
    sin.sin_port = htons(PMAPPORT);
    error = fwd_packet(RPC_XID_PORTMAP, iobuf, len,
		       &sin, &sin, (voidp) fs, got_portmap);
  } else {
    error = -len;
  }

  return error;
}


static void
recompute_portmap(fserver *fs)
{
  int error;
  u_long mnt_version;

  /*
   * No portmap calls for pure WebNFS servers.
   */
  if (fs->fs_flags & FSF_WEBNFS)
    return;

  if (nfs_auth)
    error = 0;
  else
    error = make_nfs_auth();

  if (error) {
    nfs_private *np = (nfs_private *) fs->fs_private;
    np->np_error = error;
    return;
  }

  if (fs->fs_version == 0)
    plog(XLOG_WARNING, "%s: nfs_version = 0 fixed", __func__);

  plog(XLOG_INFO, "%s: NFS version %d on %s", __func__,
       (int) fs->fs_version, fs->fs_host);
#ifdef HAVE_FS_NFS3
  if (fs->fs_version == NFS_VERSION3)
    mnt_version = AM_MOUNTVERS3;
  else
#endif /* HAVE_FS_NFS3 */
    mnt_version = MOUNTVERS;

  plog(XLOG_INFO, "Using MOUNT version: %d", (int) mnt_version);
  call_portmap(fs, nfs_auth, MOUNTPROG, mnt_version, (u_long) IPPROTO_UDP);
}


int
get_mountd_port(fserver *fs, u_short *port, wchan_t wchan)
{
  int error = -1;

  if (FSRV_ISDOWN(fs))
    return EWOULDBLOCK;

  if (FSRV_ISUP(fs)) {
    nfs_private *np = (nfs_private *) fs->fs_private;
    if (np->np_error == 0) {
      *port = np->np_mountd;
      error = 0;
    } else {
      error = np->np_error;
    }
    /*
     * Now go get the port mapping again in case it changed.
     * Note that it is used even if (np_mountd_inval)
     * is True.  The flag is used simply as an
     * indication that the mountd may be invalid, not
     * that it is known to be invalid.
     */
    switch (np->np_mountd_inval) {
    case 'Y':
      recompute_portmap(fs);
      break;
    case 'N':
      np->np_mountd_inval = 'Y';
      break;
    case 'P':
      break;
    default:
      abort();
    }
  }
  if (error < 0 && wchan && !(fs->fs_flags & FSF_WANT)) {
    /*
     * If a wait channel is supplied, and no
     * error has yet occurred, then arrange
     * that a wakeup is done on the wait channel,
     * whenever a wakeup is done on this fs node.
     * Wakeup's are done on the fs node whenever
     * it changes state - thus causing control to
     * come back here and new, better things to happen.
     */
    fs->fs_flags |= FSF_WANT;
    sched_task(wakeup_task, wchan, (wchan_t) fs);
  }
  return error;
}


/*
 * This is called when we get a reply to an RPC ping.
 * The value of id was taken from the nfs_private
 * structure when the ping was transmitted.
 */
static void
nfs_keepalive_callback(voidp pkt, int len, struct sockaddr_in *sp, struct sockaddr_in *tsp, voidp idv, int done)
{
  int xid = (long) idv;		/* cast needed for 64-bit archs */
  fserver *fs;
  int found_map = 0;

  if (!done)
    return;

  /*
   * For each node...
   */
  ITER(fs, fserver, &nfs_srvr_list) {
    nfs_private *np = (nfs_private *) fs->fs_private;
    if (np->np_xid == xid && (fs->fs_flags & FSF_PINGING)) {
      /*
       * Reset the ping counter.
       * Update the keepalive timer.
       * Log what happened.
       */
      if (fs->fs_flags & FSF_DOWN) {
	fs->fs_flags &= ~FSF_DOWN;
	if (fs->fs_flags & FSF_VALID) {
	  srvrlog(fs, "is up");
	} else {
	  if (np->np_ping > 1)
	    srvrlog(fs, "ok");
	  else
	    srvrlog(fs, "starts up");
	  fs->fs_flags |= FSF_VALID;
	}

	map_flush_srvr(fs);
      } else {
	if (fs->fs_flags & FSF_VALID) {
	  dlog("file server %s type nfs is still up", fs->fs_host);
	} else {
	  if (np->np_ping > 1)
	    srvrlog(fs, "ok");
	  fs->fs_flags |= FSF_VALID;
	}
      }

      /*
       * Adjust ping interval
       */
      untimeout(fs->fs_cid);
      fs->fs_cid = timeout(fs->fs_pinger, nfs_keepalive, (voidp) fs);

      /*
       * Update ttl for this server
       */
      np->np_ttl = clocktime(NULL) +
	(MAX_ALLOWED_PINGS - 1) * FAST_NFS_PING + fs->fs_pinger - 1;

      /*
       * New RPC xid...
       */
      np->np_xid = XID_ALLOC();

      /*
       * Failed pings is zero...
       */
      np->np_ping = 0;

      /*
       * Recompute portmap information if not known
       */
      if (np->np_mountd_inval == 'Y')
	recompute_portmap(fs);

      found_map++;
      break;
    }
  }

  if (found_map == 0)
    dlog("Spurious ping packet");
}


static void
check_fs_addr_change(fserver *fs)
{
  struct hostent *hp = NULL;
  struct in_addr ia;
  char *old_ipaddr, *new_ipaddr;

  hp = gethostbyname(fs->fs_host);
  if (!hp ||
      hp->h_addrtype != AF_INET ||
      !STREQ((char *) hp->h_name, fs->fs_host) ||
      memcmp((voidp) &fs->fs_ip->sin_addr,
	     (voidp) hp->h_addr,
	     sizeof(fs->fs_ip->sin_addr)) == 0)
    return;
  /* if got here: downed server changed IP address */
  old_ipaddr = xstrdup(inet_ntoa(fs->fs_ip->sin_addr));
  memmove((voidp) &ia, (voidp) hp->h_addr, sizeof(struct in_addr));
  new_ipaddr = inet_ntoa(ia);	/* ntoa uses static buf */
  plog(XLOG_WARNING, "EZK: down fileserver %s changed ip: %s -> %s",
       fs->fs_host, old_ipaddr, new_ipaddr);
  XFREE(old_ipaddr);
  /* copy new IP addr */
  memmove((voidp) &fs->fs_ip->sin_addr,
	  (voidp) hp->h_addr,
	  sizeof(fs->fs_ip->sin_addr));
  /* XXX: do we need to un/set these flags? */
  fs->fs_flags &= ~FSF_DOWN;
  fs->fs_flags |= FSF_VALID | FSF_WANT;
  map_flush_srvr(fs);		/* XXX: a race with flush_srvr_nfs_cache? */
  flush_srvr_nfs_cache(fs);
  fs->fs_flags |= FSF_FORCE_UNMOUNT;

#if 0
  flush_nfs_fhandle_cache(fs);	/* done in caller: nfs_keepalive_timeout */
  /* XXX: need to purge nfs_private so that somehow it will get re-initialized? */
#endif /* 0 */
}


/*
 * Called when no ping-reply received
 */
static void
nfs_keepalive_timeout(voidp v)
{
  fserver *fs = v;
  nfs_private *np = (nfs_private *) fs->fs_private;

  /*
   * Another ping has failed
   */
  np->np_ping++;
  if (np->np_ping > 1)
    srvrlog(fs, "not responding");

  /*
   * Not known to be up any longer
   */
  if (FSRV_ISUP(fs))
    fs->fs_flags &= ~FSF_VALID;

  /*
   * If ttl has expired then guess that it is dead
   */
  if (np->np_ttl < clocktime(NULL)) {
    int oflags = fs->fs_flags;
    dlog("ttl has expired");
    if ((fs->fs_flags & FSF_DOWN) == 0) {
      /*
       * Server was up, but is now down.
       */
      srvrlog(fs, "is down");
      fs->fs_flags |= FSF_DOWN | FSF_VALID;
      /*
       * Since the server is down, the portmap
       * information may now be wrong, so it
       * must be flushed from the local cache
       */
      flush_nfs_fhandle_cache(fs);
      np->np_error = -1;
      check_fs_addr_change(fs); /* check if IP addr of fserver changed */
    } else {
      /*
       * Known to be down
       */
      if ((fs->fs_flags & FSF_VALID) == 0)
	srvrlog(fs, "starts down");
      fs->fs_flags |= FSF_VALID;
    }
    if (oflags != fs->fs_flags && (fs->fs_flags & FSF_WANT))
      wakeup_srvr(fs);
    /*
     * Reset failed ping count
     */
    np->np_ping = 0;
  } else {
    if (np->np_ping > 1)
      dlog("%d pings to %s failed - at most %d allowed", np->np_ping, fs->fs_host, MAX_ALLOWED_PINGS);
  }

  /*
   * New RPC xid, so any late responses to the previous ping
   * get ignored...
   */
  np->np_xid = XID_ALLOC();

  /*
   * Run keepalive again
   */
  nfs_keepalive(fs);
}


/*
 * Keep track of whether a server is alive
 */
static void
nfs_keepalive(voidp v)
{
  fserver *fs = v;
  int error;
  nfs_private *np = (nfs_private *) fs->fs_private;
  int fstimeo = -1;
  int fs_version = nfs_valid_version(gopt.nfs_vers_ping) &&
    gopt.nfs_vers_ping < fs->fs_version ? gopt.nfs_vers_ping : fs->fs_version;

  /*
   * Send an NFS ping to this node
   */

  if (ping_len[fs_version - NFS_VERSION] == 0)
    create_ping_payload(fs_version);

  /*
   * Queue the packet...
   */
  error = fwd_packet(MK_RPC_XID(RPC_XID_NFSPING, np->np_xid),
		     ping_buf[fs_version - NFS_VERSION],
		     ping_len[fs_version - NFS_VERSION],
		     fs->fs_ip,
		     (struct sockaddr_in *) NULL,
		     (voidp) ((long) np->np_xid), /* cast needed for 64-bit archs */
		     nfs_keepalive_callback);

  /*
   * See if a hard error occurred
   */
  switch (error) {
  case ENETDOWN:
  case ENETUNREACH:
  case EHOSTDOWN:
  case EHOSTUNREACH:
    np->np_ping = MAX_ALLOWED_PINGS;	/* immediately down */
    np->np_ttl = (time_t) 0;
    /*
     * This causes an immediate call to nfs_keepalive_timeout
     * whenever the server was thought to be up.
     * See +++ below.
     */
    fstimeo = 0;
    break;

  case 0:
    dlog("Sent NFS ping to %s", fs->fs_host);
    break;
  }

  /*
   * Back off the ping interval if we are not getting replies and
   * the remote system is known to be down.
   */
  switch (fs->fs_flags & (FSF_DOWN | FSF_VALID)) {
  case FSF_VALID:		/* Up */
    if (fstimeo < 0)		/* +++ see above */
      fstimeo = FAST_NFS_PING;
    break;

  case FSF_VALID | FSF_DOWN:	/* Down */
    fstimeo = fs->fs_pinger;
    break;

  default:			/* Unknown */
    fstimeo = FAST_NFS_PING;
    break;
  }

  dlog("NFS timeout in %d seconds", fstimeo);

  fs->fs_cid = timeout(fstimeo, nfs_keepalive_timeout, (voidp) fs);
}


static void
start_nfs_pings(fserver *fs, int pingval)
{
  if (pingval == 0)	    /* could be because ping mnt option not found */
    pingval = AM_PINGER;
  /* if pings haven't been initalized, then init them for first time */
  if (fs->fs_flags & FSF_PING_UNINIT) {
    fs->fs_flags &= ~FSF_PING_UNINIT;
    plog(XLOG_INFO, "initializing %s's pinger to %d sec", fs->fs_host, pingval);
    goto do_pings;
  }

  if ((fs->fs_flags & FSF_PINGING)  &&  fs->fs_pinger == pingval) {
    dlog("already running pings to %s", fs->fs_host);
    return;
  }

  /* if got here, then we need to update the ping value */
  plog(XLOG_INFO, "changing %s's ping value from %d%s to %d%s",
       fs->fs_host,
       fs->fs_pinger, (fs->fs_pinger < 0 ? " (off)" : ""),
       pingval, (pingval < 0 ? " (off)" : ""));
 do_pings:
  fs->fs_pinger = pingval;

  if (fs->fs_cid)
    untimeout(fs->fs_cid);
  if (pingval < 0) {
    srvrlog(fs, "wired up (pings disabled)");
    fs->fs_flags |= FSF_VALID;
    fs->fs_flags &= ~FSF_DOWN;
  } else {
    fs->fs_flags |= FSF_PINGING;
    nfs_keepalive(fs);
  }
}


/*
 * Find an nfs server for a host.
 */
fserver *
find_nfs_srvr(mntfs *mf)
{
  char *host;
  fserver *fs;
  int pingval;
  mntent_t mnt;
  nfs_private *np;
  struct hostent *hp = NULL;
  struct sockaddr_in *ip = NULL;
  u_long nfs_version = 0;	/* default is no version specified */
  u_long best_nfs_version = 0;
  char *nfs_proto = NULL;	/* no IP protocol either */
  int nfs_port = 0;
  int nfs_port_opt = 0;
  int fserver_is_down = 0;

  if (mf->mf_fo == NULL) {
    plog(XLOG_ERROR, "%s: NULL mf_fo", __func__);
    return NULL;
  }
  host = mf->mf_fo->opt_rhost;
  /*
   * Get ping interval from mount options.
   * Current only used to decide whether pings
   * are required or not.  < 0 = no pings.
   */
  mnt.mnt_opts = mf->mf_mopts;
  pingval = hasmntval(&mnt, "ping");

  if (mf->mf_flags & MFF_NFS_SCALEDOWN) {
    /*
     * the server granted us a filehandle, but we were unable to mount it.
     * therefore, scale down to NFSv2/UDP and try again.
     */
    nfs_version = NFS_VERSION;
    nfs_proto = "udp";
    plog(XLOG_WARNING, "%s: NFS mount failed, trying again with NFSv2/UDP",
      __func__);
    mf->mf_flags &= ~MFF_NFS_SCALEDOWN;
  } else {
    /*
     * Get the NFS version from the mount options. This is used
     * to decide the highest NFS version to try.
     */
#ifdef MNTTAB_OPT_VERS
    nfs_version = hasmntval(&mnt, MNTTAB_OPT_VERS);
#endif /* MNTTAB_OPT_VERS */

#ifdef MNTTAB_OPT_PROTO
    {
      char *proto_opt = hasmnteq(&mnt, MNTTAB_OPT_PROTO);
      if (proto_opt) {
	char **p;
	for (p = protocols; *p; p++)
	  if (NSTREQ(proto_opt, *p, strlen(*p))) {
	    nfs_proto = *p;
	    break;
	  }
	if (*p == NULL)
	  plog(XLOG_WARNING, "ignoring unknown protocol option for %s:%s",
	       host, mf->mf_fo->opt_rfs);
      }
    }
#endif /* MNTTAB_OPT_PROTO */

#ifdef HAVE_NFS_NFSV2_H
    /* allow overriding if nfsv2 option is specified in mount options */
    if (amu_hasmntopt(&mnt, "nfsv2")) {
      nfs_version = NFS_VERSION;/* nullify any ``vers=X'' statements */
      nfs_proto = "udp";	/* nullify any ``proto=tcp'' statements */
      plog(XLOG_WARNING, "found compatibility option \"nfsv2\": set options vers=2,proto=udp for host %s", host);
    }
#endif /* HAVE_NFS_NFSV2_H */

    /* check if we've globally overridden the NFS version/protocol */
    if (gopt.nfs_vers) {
      nfs_version = gopt.nfs_vers;
      plog(XLOG_INFO, "%s: force NFS version to %d", __func__,
	   (int) nfs_version);
    }
    if (gopt.nfs_proto) {
      nfs_proto = gopt.nfs_proto;
      plog(XLOG_INFO, "%s: force NFS protocol transport to %s", __func__,
	nfs_proto);
    }
  }

  /*
   * lookup host address and canonical name
   */
  hp = gethostbyname(host);

  /*
   * New code from Bob Harris <harris@basil-rathbone.mit.edu>
   * Use canonical name to keep track of file server
   * information.  This way aliases do not generate
   * multiple NFS pingers.  (Except when we're normalizing
   * hosts.)
   */
  if (hp && !(gopt.flags & CFM_NORMALIZE_HOSTNAMES))
    host = (char *) hp->h_name;

  if (hp) {
    switch (hp->h_addrtype) {
    case AF_INET:
      ip = CALLOC(struct sockaddr_in);
      memset((voidp) ip, 0, sizeof(*ip));
      /* as per POSIX, sin_len need not be set (used internally by kernel) */
      ip->sin_family = AF_INET;
      memmove((voidp) &ip->sin_addr, (voidp) hp->h_addr, sizeof(ip->sin_addr));
      break;

    default:
      plog(XLOG_USER, "No IP address for host %s", host);
      goto no_dns;
    }
  } else {
    plog(XLOG_USER, "Unknown host: %s", host);
    goto no_dns;
  }

  /*
   * This may not be the best way to do things, but it really doesn't make
   * sense to query a file server which is marked as 'down' for any
   * version/proto combination.
   */
  ITER(fs, fserver, &nfs_srvr_list) {
    if (FSRV_ISDOWN(fs) &&
	STREQ(host, fs->fs_host)) {
      plog(XLOG_WARNING, "fileserver %s is already hung - not running NFS proto/version discovery", host);
      fs->fs_refc++;
      XFREE(ip);
      return fs;
    }
  }

  /*
   * Get the NFS Version, and verify server is up.
   * If the client only supports NFSv2, hardcode it but still try to
   * contact the remote portmapper to see if the service is running.
   */
#ifndef HAVE_FS_NFS3
  nfs_version = NFS_VERSION;
  nfs_proto = "udp";
  plog(XLOG_INFO, "The client supports only NFS(2,udp)");
#endif /* not HAVE_FS_NFS3 */


  if (amu_hasmntopt(&mnt, MNTTAB_OPT_PUBLIC)) {
    /*
     * Use WebNFS to obtain file handles.
     */
    mf->mf_flags |= MFF_WEBNFS;
    plog(XLOG_INFO, "%s option used, NOT contacting the portmapper on %s",
	 MNTTAB_OPT_PUBLIC, host);
    /*
     * Prefer NFSv4/tcp if the client supports it (cf. RFC 2054, 7).
     */
    if (!nfs_version) {
#if defined(HAVE_FS_NFS4)
      nfs_version = NFS_VERSION4;
#elif defined(HAVE_FS_NFS3)
      nfs_version = NFS_VERSION3;
#else /* not HAVE_FS_NFS3 */
      nfs_version = NFS_VERSION;
#endif /* not HAVE_FS_NFS3 */
      plog(XLOG_INFO, "No NFS version specified, will use NFSv%d",
	   (int) nfs_version);
    }
    if (!nfs_proto) {
#if defined(MNTTAB_OPT_PROTO) || defined(HAVE_FS_NFS3) || defined(HAVE_FS_NFS4)
      nfs_proto = "tcp";
#else /* not defined(MNTTAB_OPT_PROTO) || defined(HAVE_FS_NFS3) || defined(HAVE_FS_NFS4) */
      nfs_proto = "udp";
#endif /* not defined(MNTTAB_OPT_PROTO) || defined(HAVE_FS_NFS3) || defined(HAVE_FS_NFS4) */
      plog(XLOG_INFO, "No NFS protocol transport specified, will use %s",
	   nfs_proto);
    }
  } else {
    /*
     * Find the best combination of NFS version and protocol.
     * When given a choice, use the highest available version,
     * and use TCP over UDP if available.
     */
    if (check_pmap_up(host, ip)) {
      if (nfs_proto) {
	best_nfs_version = get_nfs_version(host, ip, nfs_version, nfs_proto,
	  gopt.nfs_vers);
	nfs_port = ip->sin_port;
      }
#ifdef MNTTAB_OPT_PROTO
      else {
	u_int proto_nfs_version;
	char **p;

	for (p = protocols; *p; p++) {
	  proto_nfs_version = get_nfs_version(host, ip, nfs_version, *p,
	    gopt.nfs_vers);
	  if (proto_nfs_version > best_nfs_version) {
	    best_nfs_version = proto_nfs_version;
	    nfs_proto = *p;
	    nfs_port = ip->sin_port;
	  }
	}
      }
#endif /* MNTTAB_OPT_PROTO */
    } else {
      plog(XLOG_INFO, "portmapper service not running on %s", host);
    }

    /* use the portmapper results only nfs_version is not set yet */
    if (!best_nfs_version) {
      /*
       * If the NFS server is down or does not support the portmapper call
       * (such as certain Novell NFS servers) we mark it as version 2 and we
       * let the nfs code deal with the case when it is down.  If/when the
       * server comes back up and it can support NFSv3 and/or TCP, it will
       * use those.
       */
      if (nfs_version == 0) {
	nfs_version = NFS_VERSION;
	nfs_proto = "udp";
      }
      plog(XLOG_INFO, "NFS service not running on %s", host);
      fserver_is_down = 1;
    } else {
      if (nfs_version == 0)
	nfs_version = best_nfs_version;
      plog(XLOG_INFO, "Using NFS version %d, protocol %s on host %s",
	   (int) nfs_version, nfs_proto, host);
    }
  }

  /*
   * Determine the NFS port.
   *
   * A valid "port" mount option overrides anything else.
   * If the port has been determined from the portmapper, use that.
   * Default to NFS_PORT otherwise (cf. RFC 2054, 3).
   */
  nfs_port_opt = hasmntval(&mnt, MNTTAB_OPT_PORT);
  if (nfs_port_opt > 0)
    nfs_port = htons(nfs_port_opt);
  if (!nfs_port)
    nfs_port = htons(NFS_PORT);

  dlog("%s: using port %d for nfs on %s", __func__,
    (int) ntohs(nfs_port), host);
  ip->sin_port = nfs_port;

no_dns:
  /*
   * Try to find an existing fs server structure for this host.
   * Note that differing versions or protocols have their own structures.
   * XXX: Need to fix the ping mechanism to actually use the NFS protocol
   * chosen here (right now it always uses datagram sockets).
   */
  ITER(fs, fserver, &nfs_srvr_list) {
    if (STREQ(host, fs->fs_host) &&
 	nfs_version == fs->fs_version &&
	STREQ(nfs_proto, fs->fs_proto)) {
      /*
       * fill in the IP address -- this is only needed
       * if there is a chance an IP address will change
       * between mounts.
       * Mike Mitchell, mcm@unx.sas.com, 09/08/93
       */
      if (hp && fs->fs_ip &&
	  memcmp((voidp) &fs->fs_ip->sin_addr,
		 (voidp) hp->h_addr,
		 sizeof(fs->fs_ip->sin_addr)) != 0) {
	struct in_addr ia;
	char *old_ipaddr, *new_ipaddr;
	old_ipaddr = xstrdup(inet_ntoa(fs->fs_ip->sin_addr));
	memmove((voidp) &ia, (voidp) hp->h_addr, sizeof(struct in_addr));
	new_ipaddr = inet_ntoa(ia);	/* ntoa uses static buf */
	plog(XLOG_WARNING, "fileserver %s changed ip: %s -> %s",
	     fs->fs_host, old_ipaddr, new_ipaddr);
	XFREE(old_ipaddr);
	flush_nfs_fhandle_cache(fs);
	memmove((voidp) &fs->fs_ip->sin_addr, (voidp) hp->h_addr, sizeof(fs->fs_ip->sin_addr));
      }

      /*
       * If the new file systems doesn't use WebNFS, the nfs pings may
       * try to contact the portmapper.
       */
      if (!(mf->mf_flags & MFF_WEBNFS))
	fs->fs_flags &= ~FSF_WEBNFS;

      /* check if pingval needs to be updated/set/reset */
      start_nfs_pings(fs, pingval);

      /*
       * Following if statement from Mike Mitchell <mcm@unx.sas.com>
       * Initialize the ping data if we aren't pinging now.  The np_ttl and
       * np_ping fields are especially important.
       */
      if (!(fs->fs_flags & FSF_PINGING)) {
	np = (nfs_private *) fs->fs_private;
	if (np->np_mountd_inval != 'P') {
	  np->np_mountd_inval = TRUE;
	  np->np_xid = XID_ALLOC();
	  np->np_error = -1;
	  np->np_ping = 0;
	  /*
	   * Initially the server will be deemed dead
	   * after MAX_ALLOWED_PINGS of the fast variety
	   * have failed.
	   */
	  np->np_ttl = MAX_ALLOWED_PINGS * FAST_NFS_PING + clocktime(NULL) - 1;
	  start_nfs_pings(fs, pingval);
	  if (fserver_is_down)
	    fs->fs_flags |= FSF_VALID | FSF_DOWN;
	} else {
	  fs->fs_flags = FSF_VALID;
	}

      }

      fs->fs_refc++;
      XFREE(ip);
      return fs;
    }
  }

  /*
   * Get here if we can't find an entry
   */

  /*
   * Allocate a new server
   */
  fs = ALLOC(struct fserver);
  fs->fs_refc = 1;
  fs->fs_host = xstrdup(hp ? hp->h_name : "unknown_hostname");
  if (gopt.flags & CFM_NORMALIZE_HOSTNAMES)
    host_normalize(&fs->fs_host);
  fs->fs_ip = ip;
  fs->fs_cid = 0;
  if (ip) {
    fs->fs_flags = FSF_DOWN;	/* Starts off down */
  } else {
    fs->fs_flags = FSF_ERROR | FSF_VALID;
    mf->mf_flags |= MFF_ERROR;
    mf->mf_error = ENOENT;
  }
  if (mf->mf_flags & MFF_WEBNFS)
    fs->fs_flags |= FSF_WEBNFS;
  fs->fs_version = nfs_version;
  fs->fs_proto = nfs_proto;
  fs->fs_type = MNTTAB_TYPE_NFS;
  fs->fs_pinger = AM_PINGER;
  fs->fs_flags |= FSF_PING_UNINIT; /* pinger hasn't been initialized */
  np = ALLOC(struct nfs_private);
  memset((voidp) np, 0, sizeof(*np));
  np->np_mountd = htons(hasmntval(&mnt, "mountport"));
  if (np->np_mountd == 0) {
    np->np_mountd_inval = 'Y';
    np->np_xid = XID_ALLOC();
    np->np_error = -1;
  } else {
    plog(XLOG_INFO, "%s: using mountport: %d", __func__,
      (int) ntohs(np->np_mountd));
    np->np_mountd_inval = 'P';
    np->np_xid = 0;
    np->np_error = 0;
  }

  /*
   * Initially the server will be deemed dead after
   * MAX_ALLOWED_PINGS of the fast variety have failed.
   */
  np->np_ttl = clocktime(NULL) + MAX_ALLOWED_PINGS * FAST_NFS_PING - 1;
  fs->fs_private = (voidp) np;
  fs->fs_prfree = (void (*)(voidp)) free;

  if (!FSRV_ERROR(fs)) {
    /* start of keepalive timer, first updating pingval */
    start_nfs_pings(fs, pingval);
    if (fserver_is_down)
      fs->fs_flags |= FSF_VALID | FSF_DOWN;
  }

  /*
   * Add to list of servers
   */
  ins_que(&fs->fs_q, &nfs_srvr_list);

  return fs;
}
