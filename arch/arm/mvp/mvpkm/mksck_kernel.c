/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief The monitor/kernel socket interface kernel extension.
 */

#define __KERNEL_SYSCALLS__
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/syscalls.h>
#include <linux/kmod.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/rcupdate.h>
#include <linux/smp.h>
#include <linux/spinlock.h>

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/vmalloc.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <net/sock.h>

#include <asm/memory.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include "mvp.h"
#include "actions.h"
#include "mvpkm_kernel.h"
#include "mksck_kernel.h"
#include "mksck_sockaddr.h"
#include "mutex_kernel.h"

void NORETURN FatalError(char const *file,
                int line,
                FECode feCode,
                int bugno,
                char const *fmt,
                ...)
{
   /* Lock around printing the error details so that the messages from multiple
    * threads are not interleaved. */
   static DEFINE_MUTEX(fatalErrorMutex);
   mutex_lock(&fatalErrorMutex);

   FATALERROR_COMMON(printk, vprintk, file, line, feCode, bugno, fmt);

   dump_stack();

   /* done printing */
   mutex_unlock(&fatalErrorMutex);

   /* do_exit below exits the current thread but does not crash the kernel.
    * Hence the stack dump will actually be readable from other user threads.
    */
   do_exit(1);
}


/*
 * The project uses a new address family: AF_MKSCK. Optimally this address
 * family were accepted with the Linux community and a permanent number
 * were assigned. This, however, is a dream only, not even the x86 team
 * has been able to pull it off.
 *
 * Instead we ASSUME that DECnet is dead and re-use it's address family number.
 * This is what the x86 world is moving too in the latest versions.
 */

static struct proto mksckProto = {
   .name     = "AF_MKSCK",
   .owner    = THIS_MODULE,
   .obj_size = sizeof (struct sock),
};

static int MksckCreate(struct net *net,
                       struct socket *sock,
                       int protocol,
                       int kern);

static struct net_proto_family mksckFamilyOps = {
   .family = AF_MKSCK,
   .owner  = THIS_MODULE,
   .create = MksckCreate,
};

static int MksckFault(struct vm_area_struct *vma, struct vm_fault *vmf);


/**
 * @brief Linux vma operations for receive windows established via Mksck
 *        mmap.
 */
static struct vm_operations_struct mksckVMOps = {
   .fault = MksckFault
};

/*
 * List of hosts and guests we know about.
 */
static spinlock_t mksckPageListLock;
static MksckPage *mksckPages[MKSCK_MAX_SHARES];

/*
 * The following functions form the AF_MKSCK DGRAM operations.
 */
static int MksckRelease(struct socket *sock);
static int MksckBacklogRcv(struct sock *sk, struct sk_buff *skb);
static void MksckSkDestruct(struct sock *sk);
static int MksckBind(struct socket *sock,
                     struct sockaddr *addr,
                     int addrLen);
static int MksckBindGeneric(struct sock *sk,
                            Mksck_Address addr);
static int MksckDgramRecvMsg(struct kiocb *kiocb,
                             struct socket *sock,
                             struct msghdr *msg,
                             size_t len,
                             int flags);
static int MksckDgramSendMsg(struct kiocb *kiocb,
                             struct socket *sock,
                             struct msghdr *msg,
                             size_t len);
static int MksckGetName(struct socket *sock,
                        struct sockaddr *addr,
                        int *addrLen,
                        int peer);
static unsigned int MksckPoll(struct file *filp,
                              struct socket *sock,
                              poll_table *wait);
static int MksckDgramConnect(struct socket *sock,
                             struct sockaddr *addr,
                             int addrLen,
                             int flags);
static int MksckMMap(struct file *file,
                     struct socket *sock,
                     struct vm_area_struct *vma);

static void MksckPageRelease(MksckPage *mksckPage);

static struct proto_ops mksckDgramOps = {
   .family     = AF_MKSCK,
   .owner      = THIS_MODULE,
   .release    = MksckRelease,
   .bind       = MksckBind,
   .connect    = MksckDgramConnect,
   .socketpair = sock_no_socketpair,
   .accept     = sock_no_accept,
   .getname    = MksckGetName,
   .poll       = MksckPoll,
   .ioctl      = sock_no_ioctl,
   .listen     = sock_no_listen,
   .shutdown   = sock_no_shutdown, /* MksckShutdown, */
   .setsockopt = sock_no_setsockopt,
   .getsockopt = sock_no_getsockopt,
   .sendmsg    = MksckDgramSendMsg,
   .recvmsg    = MksckDgramRecvMsg,
   .mmap       = MksckMMap,
   .sendpage   = sock_no_sendpage,
};


/**
 * @brief Initialize the MKSCK protocol
 *
 * @return 0 on success, -errno on failure
 */
int
Mksck_Init(void)
{
   int err;

   spin_lock_init(&mksckPageListLock);

   /*
    * Create a slab to allocate socket structs from.
    */
   err = proto_register(&mksckProto, 1);
   if (err != 0) {
      printk(KERN_INFO
             "Mksck_Init: Cannot register MKSCK protocol, errno = %d.\n", err);
      return err;
   }

   /*
    * Register the socket family
    */
   err = sock_register(&mksckFamilyOps);
   if (err < 0) {
      printk(KERN_INFO
            "Mksck_Init: Could not register address family AF_MKSCK"
            " (errno = %d).\n", err);
      return err;
   }

   return 0;
}


/**
 * @brief De-register the MKSCK protocol
 */
void
Mksck_Exit(void)
{
   sock_unregister(mksckFamilyOps.family);
   proto_unregister(&mksckProto);
}


/**
 * @brief Create a new MKSCK socket
 *
 * @param net      network namespace (2.6.24 or above)
 * @param sock     user socket structure
 * @param protocol protocol to be used
 * @param kern     called from kernel mode
 *
 * @return 0 on success, -errno on failure
 */
static int
MksckCreate(struct net *net,
            struct socket *sock,
            int protocol,
            int kern)
{
   struct sock *sk;
   uid_t currentUid = current_euid();

   if (!(currentUid == 0 ||
         currentUid == Mvpkm_vmwareUid)) {
      printk(KERN_WARNING
             "MksckCreate: rejected from process %s tgid=%d, pid=%d euid:%d.\n",
             current->comm,
             task_tgid_vnr(current),
             task_pid_vnr(current),
             currentUid);
      return -EPERM;
   }

   if (!sock) {
      return -EINVAL;
   }

   if (protocol) {
      return -EPROTONOSUPPORT;
   }

   switch (sock->type) {
      case SOCK_DGRAM: {
         sock->ops = &mksckDgramOps;
         break;
      }
      default: {
         return -ESOCKTNOSUPPORT;
      }
   }

   sock->state = SS_UNCONNECTED;

   /*
    * Most recently (in 2.6.24), sk_alloc() was changed to expect the
    * network namespace, and the option to zero the sock was dropped.
    */
   sk = sk_alloc(net, mksckFamilyOps.family, GFP_KERNEL, &mksckProto);

   if (!sk) {
      return -ENOMEM;
   }

   sock_init_data(sock, sk);

   sk->sk_type        = SOCK_DGRAM;
   sk->sk_destruct    = MksckSkDestruct;
   sk->sk_backlog_rcv = MksckBacklogRcv;

   /*
    * On socket lock...
    *
    * A bound socket will have an associated private area, the Mksck
    * structure part of MksckPage. That area is pointed to by
    * sk->sk_protinfo. In addition, a connected socket will have the
    * peer field in its associated area set to point to the associated
    * private area of the peer socket. A mechanism is needed to ensure
    * that these private areas area not freed while they are being
    * accessed within the scope of a function. A simple lock would not
    * suffice as the interface functions (like MksckDgramRecvMsg())
    * may block. Hence a reference count mechanism is employed. When
    * the mentioned references (sk->sk_protinfo and mksck->peer) to
    * the respective private areas are set a refcount is incremented,
    * and decremented when the references are deleted.
    *
    * The refcounts of areas pointed to by sk->sk_protinfo and
    * mksck->peer will be decremented under the lock of the socket.
    * Hence these private areas cannot disappear as long as the socket
    * lock is held.
    *
    * The interface functions will have one of the following
    * structures:
    *
    * simpleFn(sk)
    * {
    *    lock_sock(sk);
    *    if ((mksck = sk->sk_protinfo)) {
    *        <non-blocking use of mksck>
    *    }
    *    release_sock(sk);
    * }
    *
    * complexFn(sk)
    * {
    *    lock_sock(sk);
    *    if ((mksck = sk->sk_protinfo)) {
    *       IncRefc(mksck);
    *    }
    *    release_sock(sk);
    *
    *    if (mksck) {
    *       <use of mksck in a potentially blocking manner>
    *       DecRefc(mksck);
    *    }
    * }
    */
   sk->sk_protinfo = NULL;
   sock_reset_flag(sk, SOCK_DONE);

   return 0;
}


/**
 * @brief Delete a MKSCK socket
 *
 * @param sock user socket structure
 *
 * @return 0 on success, -errno on failure
 */
static int
MksckRelease(struct socket *sock)
{
   struct sock *sk = sock->sk;

   if (sk) {
      lock_sock(sk);
      sock_orphan(sk);
      release_sock(sk);
      sock_put(sk);
   }

   sock->sk = NULL;
   sock->state = SS_FREE;

   return 0;
}


static int
MksckBacklogRcv(struct sock *sk, struct sk_buff *skb)
{
   /*
    * We should never get these as we never queue an skb.
    */
   printk("MksckBacklogRcv: should never get here\n");
   return -EIO;
}


/**
 * @brief Callback at socket destruction
 *
 * @param sk pointer to kernel socket structure
 */
static void
MksckSkDestruct(struct sock *sk)
{
   Mksck *mksck;

   lock_sock(sk);
   mksck = sk->sk_protinfo;

   if (mksck != NULL) {
      sk->sk_protinfo = NULL;
      Mksck_CloseCommon(mksck);
   }

   if (sk->sk_user_data != NULL) {
      sock_kfree_s(sk, sk->sk_user_data, sizeof(int));
      sk->sk_user_data = NULL;
   }

   release_sock(sk);
}


/**
 * @brief Set the local address of a MKSCK socket
 *
 * @param sk   kernel socket structure
 * @param addr the new address of the socket
 *
 * @return 0 on success, -errno on failure
 *
 * If addr.port is undefined a new random port is assigned.
 * If addr.vmId is undefined then the vmId computed from the tgid is used.
 * Hence the vmId of a socket does not determine the host all the time.
 *
 * Assumed that the socket is locked.
 * This function is called by explicit set (MksckBind) and implicit (Send).
 */
static int
MksckBindGeneric(struct sock *sk,
                 Mksck_Address addr)
{
   int err;
   Mksck *mksck;
   MksckPage *mksckPage;

   if (sk->sk_protinfo != NULL) {
      return -EISCONN;
   }

   /*
    * Locate the page for the given host and increment its reference
    * count so it can't get freed off while we are working on it.
    */
   if (addr.vmId == MKSCK_VMID_UNDEF) {
      mksckPage = MksckPage_GetFromTgidIncRefc();
   } else {
      printk(KERN_WARNING "MksckBind: host bind called on vmid 0x%X\n", addr.vmId);
      mksckPage = MksckPage_GetFromVmIdIncRefc(addr.vmId);
   }

   if (mksckPage == NULL) {
      printk(KERN_INFO "MksckBind: no mksckPage for vm 0x%X\n", addr.vmId);
      return -ENETUNREACH;
   }
   addr.vmId = mksckPage->vmId;

   /*
    * Before we can find an unused socket port on the page we have to
    * lock the page for exclusive access so another thread can't
    * allocate the same port.
    */
   err = Mutex_Lock(&mksckPage->mutex, MutexModeEX);
   if (err < 0) {
      goto outDec;
   }

   addr.port = MksckPage_GetFreePort(mksckPage, addr.port);
   if (addr.port == MKSCK_PORT_UNDEF) {
      err = -EINVAL;
      goto outUnlockDec;
   }

   /*
    * At this point we have the mksckPage locked for exclusive access
    * and its reference count incremented.  Also, addr is completely
    * filled in with vmId and port that we want to bind.
    *
    * Find an available mksck struct on the shared page and initialize
    * it.
    */
   mksck = MksckPage_AllocSocket(mksckPage, addr);
   if (mksck == NULL) {
      err = -EMFILE;
      goto outUnlockDec;
   }

   /*
    * Stable, release mutex.  Leave mksckPage->refCount incremented so
    * mksckPage can't be freed until socket is closed.
    */
   Mutex_Unlock(&mksckPage->mutex, MutexModeEX);

   /*
    * This is why we start mksck->refCount at 1.  When sk_protinfo gets
    * cleared, we decrement mksck->refCount.
    */
   sk->sk_protinfo = mksck;

   PRINTK(KERN_DEBUG "MksckBind: socket bound to %08X\n", mksck->addr.addr);

   return 0;

outUnlockDec:
   Mutex_Unlock(&mksckPage->mutex, MutexModeEX);
outDec:
   MksckPage_DecRefc(mksckPage);
   return err;
}


/**
 * @brief Test if the socket is already bound to a local address and,
 *        if not, bind it to an unused address.
 *
 * @param sk   kernel socket structure
 * @return 0 on success, -errno on failure
 *
 * Assumed that the socket is locked.
 */
static inline int
MksckTryBind(struct sock *sk)
{
   int err = 0;

   if (!sk->sk_protinfo) {
      static const Mksck_Address addr = { .addr = MKSCK_ADDR_UNDEF };
      err = MksckBindGeneric(sk, addr);
   }
   return err;
}



/**
 * @brief Set the address of a MKSCK socket (user call)
 *
 * @param sock    user socket structure
 * @param addr    the new address of the socket
 * @param addrLen length of the address
 *
 * @return 0 on success, -errno on failure
 */
static int
MksckBind(struct socket *sock,
          struct sockaddr *addr,
          int addrLen)
{
   int err;
   struct sock *sk            = sock->sk;
   struct sockaddr_mk *addrMk = (struct sockaddr_mk *)addr;

   if (addrLen != sizeof *addrMk) {
      return -EINVAL;
   }
   if (addrMk->mk_family != AF_MKSCK) {
      return -EAFNOSUPPORT;
   }

   /*
    * Obtain the socket lock and call the generic Bind function.
    */
   lock_sock(sk);
   err = MksckBindGeneric(sk, addrMk->mk_addr);
   release_sock(sk);

   return err;
}

/**
 * @brief Lock the peer socket by locating it, incrementing its refc
 * @param addr the address of the peer socket
 * @param[out] peerMksckR set to the locked peer socket pointer
 *                   upon successful lookup
 * @return 0 on success, -errno on failure
 */
static int
LockPeer(Mksck_Address addr, Mksck **peerMksckR)
{
   int        err = 0;
   MksckPage *peerMksckPage = MksckPage_GetFromVmIdIncRefc(addr.vmId);
   Mksck     *peerMksck;

   /*
    * Find corresponding destination shared page and increment its
    * reference count so it can't be freed while we are sending to the
    * socket. Make sure that the address is indeed an address of a
    * monitor/guest socket.
    */
   if (peerMksckPage == NULL) {
      printk(KERN_INFO "LockPeer: vmId %x is not in use!\n", addr.vmId);
      return -ENETUNREACH;
   }
   if (!peerMksckPage->isGuest) {
      MksckPage_DecRefc(peerMksckPage);
      printk(KERN_INFO "LockPeer: vmId %x does not belong to a guest!\n",
             addr.vmId);
      return -ENETUNREACH;
   }


   err = Mutex_Lock(&peerMksckPage->mutex, MutexModeSH);
   if (err < 0) {
      MksckPage_DecRefc(peerMksckPage);
      return err;
   }

   /*
    * Find corresponding destination socket on that shared page and
    * increment its reference count so it can't be freed while we are
    * trying to send to it.
    */
   peerMksck = MksckPage_GetFromAddr(peerMksckPage, addr);

   if (peerMksck) {
      ATOMIC_ADDV(peerMksck->refCount, 1);
      *peerMksckR = peerMksck;
   } else {
      printk(KERN_INFO "LockPeer: addr %x is not a defined socket!\n",
             addr.addr);
      err = -ENETUNREACH;
   }

   Mutex_Unlock(&peerMksckPage->mutex, MutexModeSH);
   MksckPage_DecRefc(peerMksckPage);
   return err;
}

/**
 * @brief Set the peer address of a MKSCK socket
 *
 * @param sock    user socket structure
 * @param addr    the new address of the socket
 * @param addrLen length of the address
 * @param flags flags
 *
 * @return 0 on success, -errno on failure
 */
static int
MksckDgramConnect(struct socket *sock,
                  struct sockaddr *addr,
                  int addrLen,
                  int flags)
{
   struct sock *sk = sock->sk;
   Mksck *mksck;
   struct sockaddr_mk *peerAddrMk = (struct sockaddr_mk *)addr;
   int err = 0;

   if (addrLen != sizeof *peerAddrMk) {
      printk(KERN_INFO "MksckConnect: wrong address length!\n");
      return -EINVAL;
   }
   if (peerAddrMk->mk_family != AF_MKSCK) {
      printk(KERN_INFO "MksckConnect: wrong address family!\n");
      return -EAFNOSUPPORT;
   }

   lock_sock(sk);

   if ((err = MksckTryBind(sk))) {
      goto releaseSock;
   }
   mksck = sk->sk_protinfo;

   /*
    * First severe any past peer connections
    */
   Mksck_DisconnectPeer(mksck);
   sock->state = SS_UNCONNECTED;

   /*
    * Then build new connections ...
    */
   if (peerAddrMk->mk_addr.addr != MKSCK_ADDR_UNDEF) {
      sock->state = SS_CONNECTED;
      mksck->peerAddr = peerAddrMk->mk_addr;
      err = LockPeer(mksck->peerAddr, &mksck->peer);
      PRINTK(KERN_DEBUG "MksckConnect: socket %x is connected to %x!\n",
             mksck->addr.addr,  mksck->peerAddr.addr);
   }

releaseSock:
   release_sock(sk);

   return err;
}


/**
 * @brief returns the address of a MKSCK socket/peer address
 *
 * @param sock    user socket structure
 * @param addr    the new address of the socket
 * @param addrLen length of the address
 * @param peer    1 if the peer address is sought
 *
 * @return 0 on success, -errno on failure
 */
static int
MksckGetName(struct socket *sock,
             struct sockaddr *addr,
             int *addrLen,
             int peer)
{
   int err;
   Mksck *mksck;
   struct sock *sk = sock->sk;

   // MAX_SOCK_ADDR is size of *addr, Linux doesn't export it!
   // ASSERT_ON_COMPILE(sizeof (struct sockaddr_mk) <= MAX_SOCK_ADDR);

   lock_sock(sk);
   mksck = sk->sk_protinfo;

   if (mksck == NULL) {
      if (peer) {
          err = -ENOTCONN;
      } else {
         ((struct sockaddr_mk *)addr)->mk_family    = AF_MKSCK;
         ((struct sockaddr_mk *)addr)->mk_addr.addr = MKSCK_ADDR_UNDEF;
         *addrLen = sizeof (struct sockaddr_mk);
         err = 0;
      }
   } else if (!peer) {
      ((struct sockaddr_mk *)addr)->mk_family = AF_MKSCK;
      ((struct sockaddr_mk *)addr)->mk_addr   = mksck->addr;
      *addrLen = sizeof (struct sockaddr_mk);
      err = 0;
   } else if (mksck->peerAddr.addr == MKSCK_ADDR_UNDEF) {
      err = -ENOTCONN;
   } else {
      ((struct sockaddr_mk *)addr)->mk_family = AF_MKSCK;
      ((struct sockaddr_mk *)addr)->mk_addr   = mksck->peerAddr;
      *addrLen = sizeof (struct sockaddr_mk);
      err = 0;
   }

   release_sock(sk);

   return err;
}


/**
 * @brief VMX polling a receipted packet from VMM.
 *
 * @param filp  kernel file pointer to poll for
 * @param sock  user socket structure
 * @param wait  kernel polling table where to poll if not null
 *
 * @return poll mask state given from socket state.
 */
static unsigned int MksckPoll(struct file *filp,
                              struct socket *sock,
                              poll_table *wait)
{
   struct sock *sk = sock->sk;
   unsigned int mask = 0;
   Mksck *mksck = NULL;
   uint32 read;
   int err;

   lock_sock(sk);
   if ((err = MksckTryBind(sk))) {
      release_sock(sk);
      return err;
   }
   mksck = sk->sk_protinfo;

   /*
    * To avoid mksck disappearing right after the release_sock the
    * refcount needs to be incremented. For more details read the
    * block comment on locking in MksckCreate.
    */
   ATOMIC_ADDV(mksck->refCount, 1);
   release_sock(sk);

   /*
    * Wait to make sure this is the only thread trying to access socket.
    */
   if ((err = Mutex_Lock(&mksck->mutex, MutexModeEX)) < 0) {
      /* we might get in this situation if we are signaled
         (select() may handle this, so leave) */
      PRINTK(KERN_INFO "MksckPoll: try to abort\n");
      return mask;
   }

   /*
    * See if packet in ring.
    */
   read = mksck->read;
   if (read != mksck->write) {
      mask |= POLLIN | POLLRDNORM; /* readable, socket is unlocked */
      /* Note that if we are implementing support for POLLOUT, we SHOULD
         change this Mutex_Unlock by Mutex_UnlPoll, because there is no
         obvious knowledge about the sleepy reason that is intended by user */
      Mutex_Unlock(&mksck->mutex, MutexModeEX);
   } else {
      Mutex_UnlPoll(&mksck->mutex, MutexModeEX, MKSCK_CVAR_FILL, filp, wait);
   }

   /*
    * Note that locking rules differ a little inside MksckPoll, since we are
    * not only given a pointer to the struct socket but also a pointer to a
    * struct file. This means that during the whole operation of this function
    * and during any pending wait (registered with poll_wait()), the file itself
    * is reference counted up, and we should rely on that 'upper' reference
    * counting to prevent from tearing the Mksck down. That holds true since one
    * never re-bind sockets !
    */
   Mksck_DecRefc(mksck);
   return mask;
}

/**
 * @brief Manage a set of Mksck_PageDesc from a message or a stored array.
 *
 * @param pd       set of Mksck_PageDesc
 * @param pages    Mksck_PageDesc pages count for this management operation
 * @param incr     ternary used to indicate if we want to reference (+1), or
 *                 dereference (-1), or count (0) 4k pages
 *
 * @return length of bytes processed.
 */
static size_t
MksckPageDescManage(Mksck_PageDesc *pd,
                    uint32 pages,
                    int incr)
{
   size_t payloadLen = 0;
   uint32 i;

   for (i = 0; i < pages && pd[i].mpn != INVALID_MPN; ++i) {
      uint32 j;

      for (j = 0; j < 1 << pd[i].order; ++j) {
         struct page *page;
         MPN currMPN = pd[i].mpn + j;

         /*
          * The monitor tried to send an invalid MPN, bad.
          */
         if (!pfn_valid(currMPN)) {
            printk("MksckPageDescManage: Invalid MPN %x\n", currMPN);
         } else {
            page = pfn_to_page(currMPN);

            if (incr == +1) {
               get_page(page);
            }
            if (incr == -1) {
               put_page(page);
            }
         }

         payloadLen += PAGE_SIZE;
      }
   }

   return payloadLen;
}

/**
 * @brief Management values to be used as third parameter of MksckPageDescManage
 */
#define MANAGE_INCREMENT +1
#define MANAGE_DECREMENT -1
#define MANAGE_COUNT      0


/**
 * @brief Map a set of Mksck_PageDesc from a message or a stored array.
 *
 * @param pd       set of Mksck_PageDesc
 * @param pages    pages count for this mapping
 * @param iov      vectored user virtual addresses of the recv commands
 * @param iovCount size for iov parameter
 * @param vma      virtual memory area used for the mapping, note that
 *                 this is mandatorily required MksckPageDescMap is used
 *                 on an indirect PageDesc context (i.e whenever iov is
 *                 not computed by the kernel but by ourselves).
 *
 * Since find_vma() and vm_insert_page() are used, this function must
 * be called with current's mmap_sem locked, or inside an MMap operation.
 *
 * @return length of bytes mapped.
 */
static size_t
MksckPageDescMap(Mksck_PageDesc *pd,
                 uint32 pages,
                 struct iovec *iov,
                 int iovCount,
                 struct vm_area_struct *vma)
{
   size_t payloadLen = 0;
   uint32 i;

   for (i = 0; i < pages && pd[i].mpn != INVALID_MPN; ++i) {
      uint32 j;

      for (j = 0; j < 1 << pd[i].order; ++j) {
         HUVA huva = 0;
         struct page *page;
         MPN currMPN = pd[i].mpn + j;

         while (iovCount > 0 && iov->iov_len == 0) {
            iovCount--;
            iov++;
         }

         if (iovCount == 0) {
            printk("MksckPageDescMap: Invalid iov length\n");
            goto map_done;
         }

         huva = (HUVA)iov->iov_base;

         /*
          * iovecs for receiving the typed component of the message should
          * have page aligned base and size sufficient for page descriptor's
          * mappings.
          */
         if (huva & (PAGE_SIZE - 1) || iov->iov_len < PAGE_SIZE) {
            printk("MksckPageDescMap: Invalid huva %x or iov_len %d\n",
                   huva,
                   iov->iov_len);
            goto map_done;
         }

         /*
          * Might be in a new vma...
          */
         if (vma == NULL || huva < vma->vm_start || huva >= vma->vm_end) {
            vma = find_vma(current->mm, huva);

            /*
             * Couldn't find a matching vma for huva.
             */
            if (vma == NULL ||
                huva < vma->vm_start ||
                vma->vm_ops != &mksckVMOps) {
               printk("MksckPageDescMap: Invalid vma\n");
               goto map_done;
            }
         }

         /*
          * The monitor tried to send an invalid MPN, bad.
          */
         if (!pfn_valid(currMPN)) {
            printk("MksckPageDescMap: Invalid MPN %x\n", currMPN);
         } else {
            int rc;

            page = pfn_to_page(currMPN);

            /*
             * Map into the receive window.
             */
            rc = vm_insert_page(vma, huva, page);
            if (rc) {
               printk("MksckPageDescMap: Failed to insert %x at %x, error %d\n",
                      currMPN,
                      huva,
                      rc);
               goto map_done;
            }

            ASSERT(iov->iov_len >= PAGE_SIZE);
            iov->iov_base += PAGE_SIZE;
            iov->iov_len -= PAGE_SIZE;
         }

         payloadLen += PAGE_SIZE;
      }
   }

map_done:
   return payloadLen;
}


/**
 * @brief Check if the provided MsgHdr has still room for a receive operation.
 *
 * @param msg   user buffer
 * @return 1 if MsgHdr has IO space room in order to receive a mapping, 0 otherwise.
 */
static int
MsgHdrHasAvailableRoom(struct msghdr *msg)
{
   struct iovec *vec = msg->msg_iov;
   uint32 count = msg->msg_iovlen;

   while (count > 0 && vec->iov_len == 0) {
      count--;
      vec++;
   }

   return (count != 0);
}


/**
 * Whenever a typed message is received from the monitor, we may choose to store
 * all the page descriptor content in a linked state of descriptors, through the
 * following information context
 */
typedef struct MksckPageDescInfo {
   struct MksckPageDescInfo *next;
   uint32 flags;
   uint32 pages;
   uint32 mapCounts;
   Mksck_PageDesc descs[0];
} MksckPageDescInfo;

static void MksckPageDescSkDestruct(struct sock *sk);
static int MksckPageDescMMap(struct file *file,
                             struct socket *sock,
                             struct vm_area_struct *vma);
static int MksckPageDescIoctl(struct socket *sock,
                              unsigned int cmd,
                              unsigned long arg);

/**
 * @brief Delete a page descriptor container socket
 *
 * @param sock user socket structure
 * @return 0 on success, -errno on failure
 */
static int
MksckPageDescRelease(struct socket *sock)
{
   /* This is generic socket release */
   struct sock *sk = sock->sk;

   if (sk) {
      lock_sock(sk);
      sock_orphan(sk);
      release_sock(sk);
      sock_put(sk);
   }

   sock->sk = NULL;
   sock->state = SS_FREE;

   return 0;
}


/**
 * Whenever a typed message is received from the monitor, we may choose to store
 * all the page descriptor content for a future mapping. One shall put a context
 * usable by host userland, that means trough a file descriptor, and as a secure
 * implementation we choose to define a strict set of operations that are used
 * only for that purpose. This set of operation is reduced to leaving the
 * default "PageDesc(s) accumulating" mode (inside ioctl), mapping the context,
 * and generic socket destruction.
 */
static struct proto_ops mksckPageDescOps = {
   .family     = AF_MKSCK,
   .owner      = THIS_MODULE,
   .release    = MksckPageDescRelease,
   .bind       = sock_no_bind,
   .connect    = sock_no_connect,
   .socketpair = sock_no_socketpair,
   .accept     = sock_no_accept,
   .getname    = sock_no_getname,
   .poll       = sock_no_poll,
   .ioctl      = MksckPageDescIoctl,
   .listen     = sock_no_listen,
   .shutdown   = sock_no_shutdown,
   .setsockopt = sock_no_setsockopt,
   .getsockopt = sock_no_getsockopt,
   .sendmsg    = sock_no_sendmsg,
   .recvmsg    = sock_no_recvmsg,
   .mmap       = MksckPageDescMMap,
   .sendpage   = sock_no_sendpage,
};


/**
 * @brief Create or accumulate to a PageDesc context, backed as a descriptor.
 *
 * @param sock  user socket structure
 * @param msg   user buffer to receive the file descriptor as ancillary data
 * @param pd    source descriptor part of a message
 * @param pages pages count for this mapping
 *
 * @return error if negative,  0 otherwise
 *
 */
static int
MksckPageDescToFd(struct socket *sock,
                  struct msghdr *msg,
                  Mksck_PageDesc *pd,
                  uint32 pages)
{
   int retval;
   int newfd;
   struct socket *newsock;
   struct sock *newsk;
   struct sock *sk = sock->sk;
   MksckPageDescInfo **pmpdi, *mpdi;
   lock_sock(sk);

   /*
    * Relation between any mk socket and the PageDesc context is as follow:
    *
    * From the mk socket to the PageDesc context:
    * - sk->sk_user_data is a WEAK LINK, containing only a file descriptor
    *                    numerical value such that accumulating is keyed on it.
    *
    * From the PageDesc context to the mk socket:
    * - sk->sk_protinfo contains a MksckPageDescInfo struct.
    * - sk->sk_user_data is a pointer REF-COUNTED sock_hold() LINK, also it is
    *                    rarely dereferenced but usually used to check that the
    *                    right socket pair is used. Full dereferencing is used
    *                    only to break the described links.
    */
   if (sk->sk_user_data) {
      MksckPageDescInfo *mpdi2;

      /* continue any previous on-going mapping, i.e accumulate */
      newfd = *((int *)sk->sk_user_data);
      newsock = sockfd_lookup(newfd, &retval); // promote the weak link
      if (!newsock) {
         retval = -EINVAL;
         goto endProcessingReleaseSock;
      }

      newsk = newsock->sk;
      lock_sock(newsk);
      sockfd_put(newsock);

      if (((struct sock *)newsk->sk_user_data) != sk) {
         /* One way of going into this situation would be for userland to dup
            the file descriptor just received, close the original number, and
            open a new mk socket in the very same spot. The userland code have
            a lot of way of interacting with the kernel without this driver
            code to be notified. */
         retval = -EINVAL;
         release_sock(newsk);
         goto endProcessingReleaseSock;
      }

      mpdi = sock_kmalloc(newsk, sizeof(MksckPageDescInfo) +
                          pages*sizeof(Mksck_PageDesc), GFP_KERNEL);
      if (IS_ERR(mpdi)) {
         retval = PTR_ERR(mpdi);
         release_sock(newsk);
         goto endProcessingReleaseSock;
      }

      /* There is no mandatory needs for us to notify userland from
         the progress in "appending" to the file descriptor, but it
         would feel strange if the userland would have no mean to
         tell if the received message was just not thrown away. So, in
         order to be consistent one fill the ancillary message while
         "creating" and "appending to" file descriptors. */
      retval = put_cmsg(msg, SOL_DECNET, 0, sizeof(int), &newfd);
      if (retval < 0) {
         goto endProcessingKFreeReleaseSock;
      }

      release_sock(sk);

      mpdi2 = (MksckPageDescInfo *)newsk->sk_protinfo;
      while (mpdi2->next) {
         mpdi2 = mpdi2->next;
      }
      pmpdi = &(mpdi2->next);

   } else {
      /* Create a new socket, new context and a new file descriptor. */
      retval = sock_create(sk->sk_family, sock->type, 0, &newsock);
      if (retval < 0) {
         goto endProcessingReleaseSock;
      }

      newsk = newsock->sk;
      lock_sock(newsk);
      newsk->sk_destruct = &MksckPageDescSkDestruct;
      newsk->sk_user_data = sk;
      sock_hold(sk); // keeps a reference to parent mk socket
      newsock->ops = &mksckPageDescOps;

      mpdi = sock_kmalloc(newsk, sizeof(MksckPageDescInfo) +
                          pages*sizeof(Mksck_PageDesc), GFP_KERNEL);
      if (IS_ERR(mpdi)) {
         retval = PTR_ERR(mpdi);
         goto endProcessingFreeNewSock;
      }

      sk->sk_user_data = sock_kmalloc(sk, sizeof(int), GFP_KERNEL);
      if (IS_ERR(sk->sk_user_data)) {
         retval = PTR_ERR(sk->sk_user_data);
         sk->sk_user_data = NULL;
         goto endProcessingKFreeAndNewSock;
      }

      /* mapping to a file descriptor may fail if a thread is closing
         in parallel of sock_map_fd/sock_alloc_fd, or kernel memory is full */
      newfd = sock_map_fd(newsock, O_CLOEXEC);
      if (newfd < 0) {
         retval = newfd;
         sock_kfree_s(sk, sk->sk_user_data, sizeof(int));
         sk->sk_user_data = NULL;
         goto endProcessingKFreeAndNewSock;
      }

      /* notify userland from a new file descriptor, alike AF_UNIX ancillary */
      retval = put_cmsg(msg, SOL_DECNET, 0, sizeof(int), &newfd);
      if (retval < 0) {
         sock_kfree_s(sk, sk->sk_user_data, sizeof(int));
         sk->sk_user_data = NULL;
         sock_kfree_s(newsk, mpdi, sizeof(MksckPageDescInfo) +
                      mpdi->pages*sizeof(Mksck_PageDesc));
         release_sock(newsk);
         sockfd_put(newsock);
         sock_release(newsock);
         put_unused_fd(newfd);
         goto endProcessingReleaseSock;
      }

      *(int*)sk->sk_user_data = newfd;
      release_sock(sk);
      pmpdi = (MksckPageDescInfo **)(&(newsk->sk_protinfo));
   }

   mpdi->next  = NULL;
   mpdi->flags = 0;
   mpdi->mapCounts = 0;
   mpdi->pages = pages;
   memcpy(mpdi->descs, pd, pages*sizeof(Mksck_PageDesc));

   *pmpdi = mpdi; // link
   release_sock(newsk);

   /* increment all reference counters for the pages */
   MksckPageDescManage(pd, pages, MANAGE_INCREMENT);
   return 0;

endProcessingKFreeAndNewSock:
   sock_kfree_s(newsk, mpdi, sizeof(MksckPageDescInfo) +
                mpdi->pages*sizeof(Mksck_PageDesc));
endProcessingFreeNewSock:
   release_sock(newsk);
   sock_release(newsock);
   release_sock(sk);
   return retval;

endProcessingKFreeReleaseSock:
   sock_kfree_s(newsk, mpdi, sizeof(MksckPageDescInfo) +
                mpdi->pages*sizeof(Mksck_PageDesc));
   release_sock(newsk);
endProcessingReleaseSock:
   release_sock(sk);
   return retval;
}

/**
 * @brief Callback at socket destruction
 *
 * @param sk pointer to kernel socket structure
 */
static void
MksckPageDescSkDestruct(struct sock *sk)
{
   struct sock *mkSk = NULL;
   MksckPageDescInfo *mpdi;
   lock_sock(sk);
   mpdi = sk->sk_protinfo;
   while (mpdi) {
      MksckPageDescInfo *next = mpdi->next;
      MksckPageDescManage(mpdi->descs, mpdi->pages,
                          MANAGE_DECREMENT);
      sock_kfree_s(sk, mpdi, sizeof(MksckPageDescInfo) +
                   mpdi->pages*sizeof(Mksck_PageDesc));
      mpdi = next;
   }
   if (sk->sk_user_data) {
      mkSk = (struct sock *)sk->sk_user_data;
      sk->sk_user_data = NULL;
   }
   sk->sk_protinfo  = NULL;
   release_sock(sk);
   /* clean the monki socket that we are holding */
   if (mkSk) {
      lock_sock(mkSk);
      sock_kfree_s(mkSk, mkSk->sk_user_data, sizeof(int));
      mkSk->sk_user_data = NULL;
      release_sock(mkSk);
      sock_put(mkSk); // revert of sock_hold()
   }
}

/**
 * @brief The mmap operation of the PageDesc context file descriptor.
 *
 * The mmap command is used to mmap any detached (i.e. no more accumulating)
 * PageDesc context, full of the content from its parent communication mk
 * socket. Mapping may be done a specified number of times, so that the
 * PageDesc context could become useless (as a security restriction).
 *
 * Also note that mapping from an offset different from zero is considered
 * as a userland invalid operation.
 *
 * @param file  user file structure
 * @param sock  user socket structure
 * @param vma   virtual memory area structure
 *
 * @return error code, 0 on success
 */
static int
MksckPageDescMMap(struct file *file,
                  struct socket *sock,
                  struct vm_area_struct *vma)
{
   struct sock *sk = sock->sk;
   MksckPageDescInfo *mpdi;
   struct iovec iov;
   unsigned long vm_flags;
   int freed = 0;

   iov.iov_base = (void*)vma->vm_start;
   iov.iov_len  = vma->vm_end - vma->vm_start;

   lock_sock(sk);
   mpdi = sk->sk_protinfo;

   // vma->vm_pgoff is checked, since offsetting the map is not supported
   if (!mpdi || sk->sk_user_data || vma->vm_pgoff) {
      release_sock(sk);
      printk(KERN_INFO "MMAP failed for virt %lx size %lx\n",
             vma->vm_start, vma->vm_end - vma->vm_start);
      return -EINVAL;
   }

   vm_flags = mpdi->flags;
   if ((vma->vm_flags & ~vm_flags) & (VM_READ|VM_WRITE)) {
      release_sock(sk);
      return -EACCES;
   }

   while (mpdi) {
      MksckPageDescInfo *next = mpdi->next;
      MksckPageDescMap(mpdi->descs, mpdi->pages, &iov, 1, vma);
      if (mpdi->mapCounts && !--mpdi->mapCounts) {
         MksckPageDescManage(mpdi->descs, mpdi->pages,
                             MANAGE_DECREMENT);
         sock_kfree_s(sk, mpdi, sizeof(MksckPageDescInfo) +
                      mpdi->pages*sizeof(Mksck_PageDesc));
         freed = 1;
      }
      mpdi = next;
   }

   if (freed) {
      sk->sk_protinfo  = NULL;
   }
   vma->vm_ops = &mksckVMOps;
   release_sock(sk);
   return 0;
}

/**
 * @brief The ioctl operation of the PageDesc context file descriptor.
 *
 * The ioctl MKSCK_DETACH command is used to detach the PageDesc context
 * from its parent communication mk socket. Once done, the context
 * is able to remap the transferred PageDesc(s) of typed messages accumulated
 * into the context.
 *
 * @param sock  user socket structure
 * @param cmd   select which cmd function needs to be performed
 * @param arg   argument for command
 *
 * @return error code, 0 on success
 */
static int
MksckPageDescIoctl(struct socket *sock,
                   unsigned int  cmd,
                   unsigned long arg)
{
   struct sock *monkiSk = NULL;
   struct sock *sk = sock->sk;
   MksckPageDescInfo *mpdi;
   int retval = 0;

   switch (cmd) {
      /**
       * ioctl MKSCK_DETACH (in and out):
       * Detach, compute size and define allowed protection access rights
       *
       * [in]:  unsigned long flags, similar to prot argument of mmap()
       *        unsigned long number of available further mappings
       *           with 0 meaning unlimited number of mappings
       * [out]: unsigned long size of the available mappable area
       */
      case MKSCK_DETACH: {
         unsigned long ul[2];
         lock_sock(sk);
         mpdi = sk->sk_protinfo;
         // read unsigned long argument that contains the mmap alike flags
         if (copy_from_user(ul, (void *)arg, sizeof ul)) {
            retval = -EFAULT;
         // check that the file descriptor has a parent and some context there
         } else if (!mpdi || !sk->sk_user_data) {
            retval = -EINVAL;
         } else {
            /* compute mapping protection bits from argument and size of the
             * mapping, that is also given back to userland as unsigned long.
             */
            uint32 flags = calc_vm_prot_bits(ul[0]);
            ul[0] = 0;
            while (mpdi) {
               MksckPageDescInfo *next = mpdi->next;
               ul[0] += MksckPageDescManage(mpdi->descs, mpdi->pages,
                                            MANAGE_COUNT);
               mpdi->mapCounts = ul[1];
               mpdi = next;
            }
            if (copy_to_user((void *)arg, ul, sizeof(ul[0]))) {
               retval = -EFAULT;
            } else {
               mpdi = sk->sk_protinfo;
               mpdi->flags = flags;
               monkiSk = (struct sock *)sk->sk_user_data;
               sk->sk_user_data = NULL;
            }
         }
         release_sock(sk);
         // clean the monki socket that we are holding
         if ((sk = monkiSk)) {
            lock_sock(sk);
            sock_kfree_s(sk, sk->sk_user_data, sizeof(int));
            sk->sk_user_data = NULL;
            release_sock(sk);
            sock_put(sk);
         }
         break;
      }
      default: {
         retval = -EINVAL;
         break;
      }
   }
   return retval;
}


/**
 * @brief VMX receiving a packet from VMM.
 *
 * @param kiocb kernel io control block (unused)
 * @param sock  user socket structure
 * @param msg   user buffer to receive the packet
 * @param len   size of the user buffer
 * @param flags flags
 *
 * @return -errno on failure, else length of untyped portion + total number
 *           of bytes mapped for typed portion.
 */
static int
MksckDgramRecvMsg(struct kiocb *kiocb,
                  struct socket *sock,
                  struct msghdr *msg,
                  size_t len,
                  int flags)
{
   int err = 0;
   struct sock *sk = sock->sk;
   Mksck *mksck;
   Mksck_Datagram *dg;
   struct sockaddr_mk *fromAddr;
   uint32 read;
   struct iovec *iov;
   size_t payloadLen, untypedLen;
   uint32 iovCount;

   if (flags & MSG_OOB || flags & MSG_ERRQUEUE) {
      return -EOPNOTSUPP;
   }

   if ((msg->msg_name != NULL) && (msg->msg_namelen < sizeof *fromAddr)) {
      return -EINVAL;
   }

   lock_sock(sk);
   if ((err = MksckTryBind(sk))) {
      release_sock(sk);
      return err;
   }
   mksck = sk->sk_protinfo;

   /*
    * To avoid mksck disappearing right after the release_sock the
    * refcount needs to be incremented. For more details read the
    * block comment on locking in MksckCreate.
    */
   ATOMIC_ADDV(mksck->refCount, 1);
   release_sock(sk);

   /*
    * Get pointer to next packet in ring to be dequeued.
    */
   while (1) {

      /*
       * Wait to make sure this is the only thread trying to access socket.
       */
      if ((err = Mutex_Lock(&mksck->mutex, MutexModeEX)) < 0) {
         goto decRefc;
      }

      /*
       * See if packet in ring.
       */
      read = mksck->read;
      if (read != mksck->write) {
         break;
      }

      /*
       * Nothing there, if user wants us not to block then just return EAGAIN.
       */
      if (flags & MSG_DONTWAIT) {
         Mutex_Unlock(&mksck->mutex, MutexModeEX);
         err = -EAGAIN;
         goto decRefc;
      }

      /*
       * Nothing there, unlock socket and wait for data.
       */
      mksck->foundEmpty ++;
      err = Mutex_UnlSleep(&mksck->mutex, MutexModeEX, MKSCK_CVAR_FILL);
      if (err < 0) {
         PRINTK(KERN_INFO "MksckDgramRecvMsg: aborted\n");
         goto decRefc;
      }
   }

   /*
    * Point to packet in ring.
    */
   dg = (void *)&mksck->buff[read];

   /*
    * Provide the address of the sender.
    */
   if (msg->msg_name != NULL) {
      fromAddr            = (void *)msg->msg_name;
      fromAddr->mk_addr   = dg->fromAddr;
      fromAddr->mk_family = AF_MKSCK;
      msg->msg_namelen    = sizeof *fromAddr;
   } else {
      msg->msg_namelen = 0;
   }

   /*
    * Copy data from ring buffer to caller's buffer and remove packet from
    * ring buffer.
    */
   iov = msg->msg_iov;
   iovCount = msg->msg_iovlen;
   payloadLen = untypedLen =
      dg->len - dg->pages * sizeof(Mksck_PageDesc) - dg->pad;

   /*
    * Handle the untyped portion of the message.
    */
   if (untypedLen <= len) {
      err = memcpy_toiovec(iov,
                           dg->data,
                           untypedLen);
      if (err < 0) {
         printk("MksckDgramRecvMsg: Failed to memcpy_to_iovec untyped message component "
                "(buf len %d datagram len %d (untyped %d))\n",
                len,
                dg->len,
                untypedLen);
      }
   } else {
      err = -EINVAL;
   }

   /*
    * Map in the typed descriptor.
    */
   if (err >= 0 && dg->pages > 0) {
      Mksck_PageDesc *pd = (Mksck_PageDesc *)(dg->data + untypedLen + dg->pad);

      /*
       * There are 3 ways of receiving typed messages from the monitor.
       * - The typed message is mapped directly into a VMA. To indicate this the
       *   userland sets msg_controllen == 0.
       * - The typed message is mapped directly into a VMA and a file descriptor
       *   created for further mappings on the host (in same userland address
       *   space or an alternate userland address space). In this case
       *   msg_controllen should be set to sizeof(fd).
       * - The typed message is not mapped directly into a VMA, but a file
       *   descriptor is created for later mapping on the host. In this case
       *   msg_controllen should be set to sizeof(fd) and the supplied iovec
       *   shall not specify a receive window.
       *
       * The conjuncts below decide on which of these 3 cases we've encountered.
       */

      if ((msg->msg_controllen <= 0) ||
          ((err = MksckPageDescToFd(sock, msg, pd, dg->pages)) != 0) ||
          (MsgHdrHasAvailableRoom(msg) != 0)) {

         down_write(&current->mm->mmap_sem); // lock for a change of mapping
         payloadLen += MksckPageDescMap(pd, dg->pages, iov, iovCount, NULL);
         up_write(&current->mm->mmap_sem);
      }
   }

   /*
    * Now that packet is removed, it is safe to unlock socket so another thread
    * can do a recv().  We also want to wake someone waiting for room to insert
    * a new packet.
    */
   if ((err >= 0) && Mksck_IncReadIndex(mksck, read, dg)) {
      Mutex_UnlWake(&mksck->mutex, MutexModeEX, MKSCK_CVAR_ROOM, true);
   } else {
      Mutex_Unlock(&mksck->mutex, MutexModeEX);
   }

   /*
    * If memcpy error, return error status.
    * Otherwise, return number of bytes copied.
    */
   if (err >= 0) {
      err = payloadLen;
   }

decRefc:
   Mksck_DecRefc(mksck);
   return err;
}


/**
 * @brief VMX sending a packet to VMM.
 *
 * @param kiocb kernel io control block
 * @param sock  user socket structure
 * @param msg   packet to be transmitted
 * @param len   length of the packet
 *
 * @return length of the sent msg on success, -errno on failure
 */
static int
MksckDgramSendMsg(struct kiocb *kiocb,
                  struct socket *sock,
                  struct msghdr *msg,
                  size_t len)
{
   int             err = 0;
   struct sock    *sk = sock->sk;
   Mksck          *peerMksck;
   Mksck_Datagram *dg;
   uint32          needed;
   uint32          write;
   Mksck_Address   fromAddr;

   if (msg->msg_flags & MSG_OOB) {
      return -EOPNOTSUPP;
   }

   if (len > MKSCK_XFER_MAX) {
      return -EMSGSIZE;
   }

   /*
    * In the next locked section peerMksck pointer needs to be set and
    * its refcount needs to be incremented.
    */
   lock_sock(sk);
   do {
      Mksck *mksck;
      Mksck_Address peerAddr =
         { .addr = (msg->msg_name ?
                    ((struct sockaddr_mk *)msg->msg_name)->mk_addr.addr :
                    MKSCK_ADDR_UNDEF) };

      if ((err = MksckTryBind(sk))) {
         break;
      }
      mksck = sk->sk_protinfo;
      fromAddr = mksck->addr;

      /*
       * If the socket is connected, use that address (no sendto for
       * connected sockets). Otherwise, use the provided address if any.
       */
       if ((peerMksck = mksck->peer)) {
         if (peerAddr.addr != MKSCK_ADDR_UNDEF &&
             peerAddr.addr != mksck->peerAddr.addr) {
            err = -EISCONN;
            break;
         }
         /*
          * To avoid mksckPeer disappearing right after the
          * release_sock the refcount needs to be incremented. For
          * more details read the block comment on locking in
          * MksckCreate.
          */
         ATOMIC_ADDV(peerMksck->refCount, 1);
       } else if (peerAddr.addr == MKSCK_ADDR_UNDEF) {
          err = -ENOTCONN;
       } else {
          /*
           * LockPeer also increments the refc on the peer.
           */
          err = LockPeer(peerAddr, &peerMksck);
       }
   } while(0);
   release_sock(sk);

   if (err) {
      return err;
   }

   /*
    * Get pointer to sufficient empty space in ring buffer.
    */
   needed = MKSCK_DGSIZE(len);
   while (1) {
      /*
       * Wait to make sure this is the only thread trying to write to ring.
       */
      if ((err = Mutex_Lock(&peerMksck->mutex, MutexModeEX)) < 0) {
         goto decRefc;
      }

      /*
       * Check if socket can receive data.
       */
      if (peerMksck->shutDown & MKSCK_SHUT_RD) {
         err = -ENOTCONN;
         goto unlockDecRefc;
      }

      /*
       * See if there is room for the packet.
       */
      write = Mksck_FindSendRoom(peerMksck, needed);
      if (write != MKSCK_FINDSENDROOM_FULL) {
         break;
      }

      /*
       * No room, unlock socket and maybe wait for room.
       */
      if (msg->msg_flags & MSG_DONTWAIT) {
         err = -EAGAIN;
         goto unlockDecRefc;
      }

      peerMksck->foundFull ++;
      err = Mutex_UnlSleep(&peerMksck->mutex,
                           MutexModeEX,
                           MKSCK_CVAR_ROOM);
      if (err < 0) {
         PRINTK(KERN_INFO "MksckDgramSendMsg: aborted\n");
         goto decRefc;
      }
   }

   /*
    * Point to room in ring and fill in message.
    */
   dg = (void *)&peerMksck->buff[write];

   dg->fromAddr = fromAddr;
   dg->len      = len;

   if ((err = memcpy_fromiovec(dg->data, msg->msg_iov, len)) != 0) {
      goto unlockDecRefc;
   }

   /*
    * Increment past message.
    */
   Mksck_IncWriteIndex(peerMksck, write, needed);

   /*
    * Unlock socket and wake someone trying to receive, ie, we filled
    * in a message.
    */
   Mutex_UnlWake(&peerMksck->mutex, MutexModeEX, MKSCK_CVAR_FILL, false);

   /*
    * Maybe guest is in a general 'wait for interrupt' wait or
    * grinding away executing guest instructions.
    *
    * If it has a receive callback armed for the socket and is
    * waiting a message, just wake it up.  Else send an IPI to the CPU
    * running the guest so it will interrupt whatever it is doing and
    * read the message.
    *
    * Holding the mksckPage->mutex prevents mksckPage->vmHKVA from
    * clearing on us.
    */
   if (peerMksck->rcvCBEntryMVA != 0) {
      MksckPage *peerMksckPage = Mksck_ToSharedPage(peerMksck);

      if ((err = Mutex_Lock(&peerMksckPage->mutex, MutexModeSH)) == 0) {
         uint32 sockIdx = peerMksck->index;
         MvpkmVM *vm = (MvpkmVM *) peerMksckPage->vmHKVA;

         /*
          * The destruction of vm and wsp is blocked by the
          * mksckPage->mutex.
          */
         if (vm) {
            WorldSwitchPage *wsp = vm->wsp;

            ASSERT(sockIdx < 8 * sizeof peerMksckPage->wakeVMMRecv);
            ATOMIC_ORV(peerMksckPage->wakeVMMRecv, 1U << sockIdx);

            if (wsp) {
               Mvpkm_WakeGuest(vm, ACTION_MKSCK);
            }
         }
         Mutex_Unlock(&peerMksckPage->mutex, MutexModeSH);
      }
   }

   /*
    * If all are happy tell the caller the number of transferred bytes.
    */
   if (!err) {
      err = len;
   }

   /*
    * Now that we are done with target socket, allow it to be freed.
    */
decRefc:
   Mksck_DecRefc(peerMksck);
   return err;

unlockDecRefc:
   Mutex_Unlock(&peerMksck->mutex, MutexModeEX);
   goto decRefc;
}


/**
 * @brief Page fault handler for receive windows. Since the host process
 *        should not be faulting in this region and only be accessing
 *        memory that has been established via a typed message transfer,
 *        we always signal the fault back to the process.
 */
static int
MksckFault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
   return VM_FAULT_SIGBUS;
}

/**
 * @brief Establish a region in the host process suitable for use as a
 *        receive window.
 *
 * @param file file reference (ignored).
 * @param sock user socket structure.
 * @param vma Linux virtual memory area defining the region.
 *
 * @return 0 on success, otherwise error code.
 */
static int
MksckMMap(struct file *file, struct socket *sock, struct vm_area_struct *vma)
{
   /*
    * All the hard work is done in MksckDgramRecvMsg. Here we simply mark the
    * vma as belonging to Mksck.
    */
   vma->vm_ops = &mksckVMOps;

   return 0;
}

/**
 * @brief This gets called after returning from the monitor.
 *        Since the monitor doesn't directly wake VMX threads when it sends
 *        something to VMX (for efficiency), this routine checks for the
 *        omitted wakes and does them.
 * @param mksckPage some shared page that the monitor writes packets to, ie
 *                  an host shared page
 */
void
Mksck_WakeBlockedSockets(MksckPage *mksckPage)
{
   Mksck *mksck;
   uint32 i, wakeHostRecv;

   wakeHostRecv = mksckPage->wakeHostRecv;
   if (wakeHostRecv != 0) {
      mksckPage->wakeHostRecv = 0;
      for (i = 0; wakeHostRecv != 0; i ++) {
         if (wakeHostRecv & 1) {
             mksck = &mksckPage->sockets[i];
             Mutex_CondSig(&mksck->mutex, MKSCK_CVAR_FILL, true);
         }
         wakeHostRecv >>= 1;
      }
   }
}

/**
 * @brief allocate and initialize a shared page.
 * @return pointer to shared page.<br>
 *         NULL on error
 */
MksckPage *
MksckPageAlloc(void)
{
   uint32 jj;
   /*
    * Ask for pages in the virtual kernel space. There is no
    * requirement to be physically contiguous.
    */
   MksckPage *mksckPage = vmalloc(MKSCKPAGE_SIZE);

   if (mksckPage) {

      /*
       * Initialize its contents.  Start refCount at 1 and decrement it
       * when the worldswitch or VM page gets freed.
       */
      memset(mksckPage, 0, MKSCKPAGE_SIZE);
      ATOMIC_SETV(mksckPage->refCount, 1);
      mksckPage->portStore = MKSCK_PORT_HIGH;

      Mutex_Init(&mksckPage->mutex);
      for (jj = 0; jj<MKSCK_SOCKETS_PER_PAGE; jj++) {
         Mutex_Init(&mksckPage->sockets[jj].mutex);
      }
   }

   return mksckPage;
}

/**
 * @brief Release the allocated pages.
 * @param mksckPage the address of the mksckPage to be released
 */
static void
MksckPageRelease(MksckPage *mksckPage)
{
   int ii;

   for (ii = 0; ii<MKSCK_SOCKETS_PER_PAGE; ii++) {
      Mutex_Destroy(&mksckPage->sockets[ii].mutex);
   }
   Mutex_Destroy(&mksckPage->mutex);

   vfree(mksckPage);
}

/**
 * @brief Using the tgid locate the vmid of this process.
 *        Assumed that mksckPageListLock is held
 * @return the vmId if page is already allocated,
 *         the first vacant vmid if not yet allocated.<br>
 *         MKSCK_PORT_UNDEF if no slot is vacant
 */
static inline Mksck_VmId
GetHostVmId(void)
{
   uint32 jj;
   Mksck_VmId vmId, vmIdFirstVacant = MKSCK_VMID_UNDEF;
   MksckPage *mksckPage;
   uint32 tgid = task_tgid_vnr(current);
   /*
    * Assign an unique vmId to the shared page. Start the search from
    * the vmId that is the result of hashing tgid to 15 bits. As a
    * used page with a given vmId can occupy only a given slot in the
    * mksckPages array, it is enough to search through the
    * MKSCK_MAX_SHARES slots for a vacancy.
    */
   for (jj = 0, vmId = MKSCK_TGID2VMID(tgid);
        jj < MKSCK_MAX_SHARES;
        jj++, vmId++) {
      if (vmId > MKSCK_VMID_HIGH) {
         vmId = 0;
      }
      mksckPage = mksckPages[MKSCK_VMID2IDX(vmId)];

      if (mksckPage) {
         if (mksckPage->tgid == tgid &&
             !mksckPage->isGuest) {
            return mksckPage->vmId;
         }

      } else if (vmIdFirstVacant == MKSCK_VMID_UNDEF) {
         vmIdFirstVacant = vmId;
      }
   }
   return vmIdFirstVacant;
}


/**
 * @brief Locate the first empty slot
 *        Assumed that mksckPageListLock is held
 * @return the first vacant vmid.<br>
 *         MKSCK_PORT_UNDEF if no slot is vacant
 */
static inline Mksck_VmId
GetNewGuestVmId(void)
{
   Mksck_VmId vmId;

   for (vmId = 0; vmId < MKSCK_MAX_SHARES; vmId++) {
      if (!mksckPages[MKSCK_VMID2IDX(vmId)]) {
         return vmId;
      }
   }
   return MKSCK_VMID_UNDEF;
}


/**
 * @brief Find shared page for a given idx. The page referred to be the
 *        idx should exist and be locked by the caller.
 * @param idx index of the page in the array
 * @return pointer to shared page
 */
MksckPage *
MksckPage_GetFromIdx(uint32 idx)
{
   MksckPage *mksckPage = mksckPages[idx];
   ASSERT(mksckPage);
   ASSERT(idx<MKSCK_MAX_SHARES);
   ASSERT(ATOMIC_GETO(mksckPage->refCount));
   return mksckPage;
}

/**
 * @brief find shared page for a given vmId
 *        The vmid should exist and be locked by the caller.
 * @param vmId vmId to look for, either an host vmId or a guest vmId
 * @return pointer to shared page
 */
MksckPage *
MksckPage_GetFromVmId(Mksck_VmId vmId)
{
   MksckPage *mksckPage = mksckPages[MKSCK_VMID2IDX(vmId)];
   ASSERT(mksckPage);
   ASSERT(mksckPage->vmId == vmId);
   ASSERT(ATOMIC_GETO(mksckPage->refCount));
   return mksckPage;
}


/**
 * @brief find shared page for a given vmId
 * @param vmId vmId to look for, either an host vmId or a guest vmId
 * @return NULL: no such shared page exists<br>
 *         else: pointer to shared page.
 *               Call Mksck_DecRefc() when done with pointer
 */
MksckPage *
MksckPage_GetFromVmIdIncRefc(Mksck_VmId vmId)
{
   MksckPage *mksckPage;

   spin_lock(&mksckPageListLock);
   mksckPage = mksckPages[MKSCK_VMID2IDX(vmId)];

   if (!mksckPage || (mksckPage->vmId != vmId)) {
      printk(KERN_INFO "MksckPage_GetFromVmIdIncRefc: vmId %04X not found\n",
             vmId);
      mksckPage = NULL;
   } else {
      ATOMIC_ADDV(mksckPage->refCount, 1);
   }
   spin_unlock(&mksckPageListLock);
   return mksckPage;
}


/**
 * @brief find or allocate shared page using tgid
 * @return NULL: no such shared page exists<br>
 *         else: pointer to shared page.
 *               Call Mksck_DecRefc() when done with pointer
 */
MksckPage *
MksckPage_GetFromTgidIncRefc(void)
{
   MksckPage *mksckPage;
   Mksck_VmId vmId;

   while (1) {
      spin_lock(&mksckPageListLock);
      vmId = GetHostVmId();

      if (vmId == MKSCK_VMID_UNDEF) {
         /*
          * No vmId has been allocated yet and there is no free slot.
          */
         spin_unlock(&mksckPageListLock);
         return NULL;
      }

      mksckPage = mksckPages[MKSCK_VMID2IDX(vmId)];
      if (mksckPage != NULL) {
         /*
          * There is a vmid already allocated, increment the refc on it.
          */
         ATOMIC_ADDV(mksckPage->refCount, 1);
         spin_unlock(&mksckPageListLock);
         return mksckPage;
      }

      /*
       * Have to release spinlock to allocate a new page.
       */
      spin_unlock(&mksckPageListLock);
      mksckPage = MksckPageAlloc();
      if (mksckPage == NULL) {
         return NULL;
      }

      /*
       * Re-lock and make sure no one else allocated while unlocked.
       * If someone else did allocate, free ours off and use theirs.
       */
      spin_lock(&mksckPageListLock);
      vmId = GetHostVmId();
      if ((vmId != MKSCK_VMID_UNDEF) &&
          (mksckPages[MKSCK_VMID2IDX(vmId)] == NULL)) {
         break;
      }
      spin_unlock(&mksckPageListLock);
      MksckPageRelease(mksckPage);
   }

   /*
    * This is a successful new allocation. insert it into the table
    * and initialize the fields.
    */
   mksckPages[MKSCK_VMID2IDX(vmId)] = mksckPage;
   mksckPage->vmId    = vmId;
   mksckPage->isGuest = false;
   mksckPage->vmHKVA  = 0;
   mksckPage->tgid    = task_tgid_vnr(current);
   printk(KERN_DEBUG "New host mksck page is allocated: idx %x, vmId %x, tgid %d\n",
          MKSCK_VMID2IDX(vmId), vmId, mksckPage->tgid);

   spin_unlock(&mksckPageListLock);
   return mksckPage;
}

/**
 * @brief Initialize the VMX provided wsp. Allocate communication page.
 * @param vm  which virtual machine we're running
 * @return 0 if all OK, error value otherwise
 */
int
Mksck_WspInitialize(MvpkmVM *vm)
{
   WorldSwitchPage *wsp = vm->wsp;
   int err;
   Mksck_VmId vmId;
   MksckPage *mksckPage;

   if (wsp->guestId) {
      err = -EBUSY;
   } else if (!(mksckPage = MksckPageAlloc())) {
      err = -ENOMEM;
   } else {
      spin_lock(&mksckPageListLock);

      if ((vmId = GetNewGuestVmId()) == MKSCK_VMID_UNDEF) {

         err = -EMFILE;
         MksckPageRelease(mksckPage);

         printk(KERN_INFO "Mksck_WspInitialize: Cannot allocate vmId\n");

      } else {
         /*
          * Now that the mksckPage is all initialized, let others see it.
          */
         mksckPages[MKSCK_VMID2IDX(vmId)] = mksckPage;
         mksckPage->vmId    = vmId;
         mksckPage->isGuest = true;
         mksckPage->vmHKVA  = (HKVA)vm;
         /* mksckPage->tgid is undefined when isGuest is true */

         wsp->guestId = vmId;

         printk(KERN_DEBUG "New guest mksck page is allocated: idx %x, vmId %x\n",
                MKSCK_VMID2IDX(vmId), vmId);

         err = 0;
      }

      /*
       * All stable, ie, mksckPages[] written, ok to unlock now.
       */
      spin_unlock(&mksckPageListLock);
   }

   return err;
}

/**
 * @brief Release the wsp. Clean up after the monitor. Free the
 *        associated communication page.
 * @param wsp which worldswitch page (VCPU)
 */
void
Mksck_WspRelease(WorldSwitchPage *wsp)
{
   int ii;
   int err;
   MksckPage *mksckPage = MksckPage_GetFromVmId(wsp->guestId);

   /*
    * The worldswitch page for a particular VCPU is about to be freed
    * off, so we know the monitor will never execute again.  But the
    * monitor most likely left some sockets open. Those may have
    * outbound connections to host sockets that we must close.
    *
    * Loop through all possibly open sockets.
    */
   uint32 isOpened = wsp->isOpened;
   Mksck *mksck = mksckPage->sockets;
   while (isOpened) {
      if (isOpened & 1) {
         ASSERT(ATOMIC_GETO(mksck->refCount) != 0);
         /*
          * The socket may be connected to a peer (host) socket, so we
          * have to decrement that target socket's reference
          * count. Unfortunately, Mksck_DisconnectPeer(mksck) cannot
          * be called as mksck->peer is an mva not an hkva. Translate
          * the address first.
          */
         if (mksck->peer) {
            MksckPage *mksckPagePeer = MksckPage_GetFromVmId(mksck->peerAddr.vmId);
            ASSERT(mksckPagePeer);
            mksck->peer = MksckPage_GetFromAddr(mksckPagePeer, mksck->peerAddr);
            ASSERT(mksck->peer);
            /* mksck->peer is now a hkva */
         }

         Mksck_CloseCommon(mksck);
      }
      isOpened >>= 1;
      mksck++;
   }

   /*
    * A host socket may be in the process of sending to the guest. It
    * will attempt to wake up the guest using mksckPage->vmHKVA and
    * mksckPage->vmHKVA->wsp. To assure that the vm and wsp structures
    * are not disappearing from under the sending thread we lock the
    * page here.
    */
   err = Mutex_Lock(&mksckPage->mutex, MutexModeEX);
   ASSERT(!err);
   mksckPage->vmHKVA = 0;
   Mutex_Unlock(&mksckPage->mutex, MutexModeEX);
   /*
    * Decrement refcount set by MksckPageAlloc() call in
    * Mksck_WspInitialize().
    */
   MksckPage_DecRefc(mksckPage);

   /*
    * Decrement refcount set by VMM:Mksck_Init() referring to the local
    * variable guestMksckPage.
    */
   if (wsp->guestPageMapped) {
      wsp->guestPageMapped = false;
      MksckPage_DecRefc(mksckPage);
   }

   /*
    * Another task is to decrement the reference count on the mksck
    * pages the monitor accessed. Those pages are listed in the
    * wsp->isPageMapped list. They were locked by the monitor
    * calling WSCALL_GET_PAGE_FROM_VMID
    */
   for (ii = 0; ii < MKSCK_MAX_SHARES; ii++) {
      if (wsp->isPageMapped[ii]) {
         MksckPage *mksckPageOther = MksckPage_GetFromIdx(ii);

         wsp->isPageMapped[ii] = false;
         MksckPage_DecRefc(mksckPageOther);
      }
   }
}

/**
 * @brief disconnect from peer by decrementing
 *        peer socket's reference count and clearing the pointer.
 * @param mksck local socket to check for connection
 */
void
Mksck_DisconnectPeer(Mksck *mksck)
{
   Mksck *peerMksck = mksck->peer;
   if (peerMksck != NULL) {
      mksck->peer = NULL;
      mksck->peerAddr.addr = MKSCK_ADDR_UNDEF;
      Mksck_DecRefc(peerMksck);
   }
}


/**
 * @brief decrement shared page reference count, free page if it goes zero.
 *        also do a dmb first to make sure all activity on the struct is
 *        finished before decrementing the ref count.
 * @param mksckPage shared page
 */
void
MksckPage_DecRefc(MksckPage *mksckPage)
{
   uint32 oldRefc;

   DMB();
   do {
      while ((oldRefc = ATOMIC_GETO(mksckPage->refCount)) == 1) {

         /*
          * Find corresponding entry in list of known shared pages and
          * clear it so we can't open any new sockets on this shared
          * page, thus preventing its refCount from being incremented.
          */
         spin_lock(&mksckPageListLock);
         if (ATOMIC_SETIF(mksckPage->refCount, 0, 1)) {
            uint32 ii = MKSCK_VMID2IDX(mksckPage->vmId);
            ASSERT(ii < MKSCK_MAX_SHARES);
            ASSERT(mksckPages[ii] == mksckPage);
            mksckPages[ii] = NULL;
            spin_unlock(&mksckPageListLock);
            printk(KERN_DEBUG "%s mksck page is released: idx %x, vmId %x, tgid %d\n",
                   mksckPage->isGuest?"Guest":"Host",
                   ii, mksckPage->vmId, mksckPage->tgid);
            MksckPageRelease(mksckPage);
            return;
         }
         spin_unlock(&mksckPageListLock);
      }
      ASSERT(oldRefc != 0);
   } while (!ATOMIC_SETIF(mksckPage->refCount, oldRefc - 1, oldRefc));
}

/**
 * @brief Lookup if the provided mpn belongs to one of the Mksck pages. Map if found.
 * @return 0 if all OK, error value otherwise
 */
int
MksckPage_LookupAndInsertPage(struct vm_area_struct *vma,
                              unsigned long address,
                              MPN mpn)
{
   int ii, jj;
   MksckPage **mksckPagePtr = mksckPages;

   spin_lock(&mksckPageListLock);
   for (jj = MKSCK_MAX_SHARES; jj--; mksckPagePtr++) {
      if (*mksckPagePtr) {
         for (ii = 0; ii < MKSCKPAGE_TOTAL; ii++) {
            if (vmalloc_to_pfn((void*)(((HKVA)*mksckPagePtr) + ii*PAGE_SIZE)) == mpn &&
                vm_insert_page(vma, address, pfn_to_page(mpn)) == 0) {
               spin_unlock(&mksckPageListLock);
               return 0;
            }
         }
      }
   }
   spin_unlock(&mksckPageListLock);
   return -1;
}


/**
 * @brief Print information on the allocated shared pages
 *
 * This function reports (among many other things) on the use of locks
 * on the mksck page (page lock and individual socket locks). To avoid
 * the Hiesenberg effect it avoids using locks unless there is a
 * danger of dereferencing freed memory. In particular, holding
 * mksckPageListLock ensures that the mksck page is not freed while it
 * is read. But under very rare conditions this function may report
 * inconsistent or garbage data.
 */
static int
MksckPageInfoShow(struct seq_file *m, void *private)
{
   int ii, jj;
   uint32 isPageMapped = 0;
   int err;
   MvpkmVM *vm;

   /*
    * Lock is needed to atomize the test and dereference of
    * mksckPages[ii]
    */
   spin_lock(&mksckPageListLock);
   for (ii = 0; ii < MKSCK_MAX_SHARES; ii++) {
      MksckPage *mksckPage  = mksckPages[ii];
      if (mksckPage != NULL && mksckPage->isGuest) {
         /*
          * After the refcount is incremented mksckPage will not be
          * freed and it can continued to be dereferenced after the
          * unlock of mksckPageListLock.
          */
         ATOMIC_ADDV(mksckPage->refCount, 1);
         spin_unlock(&mksckPageListLock);

         /*
          * To dereference mksckPage->vmHKVA, we need to have the page
          * lock.
          */
         err = Mutex_Lock(&mksckPage->mutex, MutexModeEX);
         vm = (MvpkmVM *) mksckPage->vmHKVA;

         if (err == 0 && vm && vm->wsp) {
            for (jj = 0; jj < MKSCK_MAX_SHARES; jj++) {
               if (vm->wsp->isPageMapped[jj]) isPageMapped |= 1<<jj;
            }
         }
         Mutex_Unlock(&mksckPage->mutex, MutexModeEX);
         /*
          * Decrement the page refcount and relock the
          * mksckPageListLock for the next for loop.
          */
         MksckPage_DecRefc(mksckPage);
         spin_lock(&mksckPageListLock);
         break;
      }
   }

   /* mksckPageListLock is still locked,  mksckPages[ii] can be dereferenced */
   for (ii = 0; ii < MKSCK_MAX_SHARES; ii++) {
      MksckPage *mksckPage  = mksckPages[ii];
      if (mksckPage != NULL) {
         uint32 lState = ATOMIC_GETO(mksckPage->mutex.state);
         uint32 isOpened = 0; /* guest has an implicit ref */

         seq_printf(m, "MksckPage[%02d]: { vmId = %4x(%c), refC = %2d%s",
                    ii, mksckPage->vmId,
                    mksckPage->isGuest?'G':'H',
                    ATOMIC_GETO(mksckPage->refCount),
                    (isPageMapped&(1<<ii) ? "*" : ""));

         if (lState) {
            seq_printf(m, ", lock=%x locked by line %d, unlocked by %d",
                       lState, mksckPage->mutex.line, mksckPage->mutex.lineUnl);
         }


         if (!mksckPage->isGuest) {
            struct task_struct *target;
            seq_printf(m, ", tgid = %d", mksckPage->tgid);

            rcu_read_lock();

            target = pid_task(find_vpid(mksckPage->tgid), PIDTYPE_PID);
            seq_printf(m, "(%s)", target ? target->comm : "no such process");

            rcu_read_unlock();
         } else {
            ATOMIC_ADDV(mksckPage->refCount, 1);
            spin_unlock(&mksckPageListLock);

            err = Mutex_Lock(&mksckPage->mutex, MutexModeEX);
            vm = (MvpkmVM *) mksckPage->vmHKVA;

            if (err == 0 && vm && vm->wsp) {
               isOpened = vm->wsp->isOpened;
            }
            Mutex_Unlock(&mksckPage->mutex, MutexModeEX);
            MksckPage_DecRefc(mksckPage);
            spin_lock(&mksckPageListLock);
            /*
             * As the mksckPageListLock was unlocked, nothing
             * prevented the MksckPage_DecRefc from actually freeing
             * the page. Lets verify that the page is still there.
             */
            if (mksckPage != mksckPages[ii]) {
               seq_printf(m, " released }\n");
               continue;
            }
         }
         seq_printf(m, ", sockets[] = {");

         for (jj = 0; jj < mksckPage->numAllocSocks; jj++, isOpened >>= 1) {
            Mksck *mksck = mksckPage->sockets + jj;

            if (ATOMIC_GETO(mksck->refCount)) {
               uint32 blocked;
               lState = ATOMIC_GETO(mksck->mutex.state);
               seq_printf(m, "\n             { addr = %8x, refC = %2d%s%s%s",
                          mksck->addr.addr,
                          ATOMIC_GETO(mksck->refCount),
                          (isOpened & 1 ? "*" : ""),
                          (mksck->shutDown & MKSCK_SHUT_RD ? " SHUTD_RD":""),
                          (mksck->shutDown & MKSCK_SHUT_WR ? " SHUTD_WR":""));

               if (mksck->peer) {
                  seq_printf(m, ", peerAddr = %8x",
                             mksck->peerAddr.addr);
               }

               if (lState) {
                  seq_printf(m, ", lock=%x locked by line %d, unlocked by %d",
                             lState, mksck->mutex.line, mksck->mutex.lineUnl);
               }

               if ((blocked = ATOMIC_GETO(mksck->mutex.blocked))) {
                  seq_printf(m, ", blocked=%d", blocked);
               }

               seq_printf(m, " }");
            }
         }
         seq_printf(m, " } }\n");
      }
   }
   spin_unlock(&mksckPageListLock);

   return 0;
}


static int
MksckPageInfoOpen(struct inode *inode, struct file *file)
{
   return single_open(file, MksckPageInfoShow, inode->i_private);
}

static const struct file_operations mksckPageInfoFops = {
   .open = MksckPageInfoOpen,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release,
};

static struct dentry *mksckPageDentry = NULL;

void
MksckPageInfo_Init(void)
{
   mksckPageDentry = debugfs_create_file("mksckPage",
                                         S_IROTH,
                                         NULL,
                                         NULL,
                                         &mksckPageInfoFops);
}

void
MksckPageInfo_Exit(void)
{
   if (mksckPageDentry) {
      debugfs_remove(mksckPageDentry);
   }
}
