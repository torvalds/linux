/*
 * Copyright 2005-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <errno.h>

#include "bio_lcl.h"
#ifndef OPENSSL_NO_DGRAM

# ifndef OPENSSL_NO_SCTP
#  include <netinet/sctp.h>
#  include <fcntl.h>
#  define OPENSSL_SCTP_DATA_CHUNK_TYPE            0x00
#  define OPENSSL_SCTP_FORWARD_CUM_TSN_CHUNK_TYPE 0xc0
# endif

# if defined(OPENSSL_SYS_LINUX) && !defined(IP_MTU)
#  define IP_MTU      14        /* linux is lame */
# endif

# if OPENSSL_USE_IPV6 && !defined(IPPROTO_IPV6)
#  define IPPROTO_IPV6 41       /* windows is lame */
# endif

# if defined(__FreeBSD__) && defined(IN6_IS_ADDR_V4MAPPED)
/* Standard definition causes type-punning problems. */
#  undef IN6_IS_ADDR_V4MAPPED
#  define s6_addr32 __u6_addr.__u6_addr32
#  define IN6_IS_ADDR_V4MAPPED(a)               \
        (((a)->s6_addr32[0] == 0) &&          \
         ((a)->s6_addr32[1] == 0) &&          \
         ((a)->s6_addr32[2] == htonl(0x0000ffff)))
# endif

static int dgram_write(BIO *h, const char *buf, int num);
static int dgram_read(BIO *h, char *buf, int size);
static int dgram_puts(BIO *h, const char *str);
static long dgram_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int dgram_new(BIO *h);
static int dgram_free(BIO *data);
static int dgram_clear(BIO *bio);

# ifndef OPENSSL_NO_SCTP
static int dgram_sctp_write(BIO *h, const char *buf, int num);
static int dgram_sctp_read(BIO *h, char *buf, int size);
static int dgram_sctp_puts(BIO *h, const char *str);
static long dgram_sctp_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int dgram_sctp_new(BIO *h);
static int dgram_sctp_free(BIO *data);
#  ifdef SCTP_AUTHENTICATION_EVENT
static void dgram_sctp_handle_auth_free_key_event(BIO *b, union sctp_notification
                                                  *snp);
#  endif
# endif

static int BIO_dgram_should_retry(int s);

static void get_current_time(struct timeval *t);

static const BIO_METHOD methods_dgramp = {
    BIO_TYPE_DGRAM,
    "datagram socket",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    dgram_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    dgram_read,
    dgram_puts,
    NULL,                       /* dgram_gets,         */
    dgram_ctrl,
    dgram_new,
    dgram_free,
    NULL,                       /* dgram_callback_ctrl */
};

# ifndef OPENSSL_NO_SCTP
static const BIO_METHOD methods_dgramp_sctp = {
    BIO_TYPE_DGRAM_SCTP,
    "datagram sctp socket",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    dgram_sctp_write,
    /* TODO: Convert to new style write function */
    bread_conv,
    dgram_sctp_read,
    dgram_sctp_puts,
    NULL,                       /* dgram_gets,         */
    dgram_sctp_ctrl,
    dgram_sctp_new,
    dgram_sctp_free,
    NULL,                       /* dgram_callback_ctrl */
};
# endif

typedef struct bio_dgram_data_st {
    BIO_ADDR peer;
    unsigned int connected;
    unsigned int _errno;
    unsigned int mtu;
    struct timeval next_timeout;
    struct timeval socket_timeout;
    unsigned int peekmode;
} bio_dgram_data;

# ifndef OPENSSL_NO_SCTP
typedef struct bio_dgram_sctp_save_message_st {
    BIO *bio;
    char *data;
    int length;
} bio_dgram_sctp_save_message;

typedef struct bio_dgram_sctp_data_st {
    BIO_ADDR peer;
    unsigned int connected;
    unsigned int _errno;
    unsigned int mtu;
    struct bio_dgram_sctp_sndinfo sndinfo;
    struct bio_dgram_sctp_rcvinfo rcvinfo;
    struct bio_dgram_sctp_prinfo prinfo;
    void (*handle_notifications) (BIO *bio, void *context, void *buf);
    void *notification_context;
    int in_handshake;
    int ccs_rcvd;
    int ccs_sent;
    int save_shutdown;
    int peer_auth_tested;
} bio_dgram_sctp_data;
# endif

const BIO_METHOD *BIO_s_datagram(void)
{
    return &methods_dgramp;
}

BIO *BIO_new_dgram(int fd, int close_flag)
{
    BIO *ret;

    ret = BIO_new(BIO_s_datagram());
    if (ret == NULL)
        return NULL;
    BIO_set_fd(ret, fd, close_flag);
    return ret;
}

static int dgram_new(BIO *bi)
{
    bio_dgram_data *data = OPENSSL_zalloc(sizeof(*data));

    if (data == NULL)
        return 0;
    bi->ptr = data;
    return 1;
}

static int dgram_free(BIO *a)
{
    bio_dgram_data *data;

    if (a == NULL)
        return 0;
    if (!dgram_clear(a))
        return 0;

    data = (bio_dgram_data *)a->ptr;
    OPENSSL_free(data);

    return 1;
}

static int dgram_clear(BIO *a)
{
    if (a == NULL)
        return 0;
    if (a->shutdown) {
        if (a->init) {
            BIO_closesocket(a->num);
        }
        a->init = 0;
        a->flags = 0;
    }
    return 1;
}

static void dgram_adjust_rcv_timeout(BIO *b)
{
# if defined(SO_RCVTIMEO)
    bio_dgram_data *data = (bio_dgram_data *)b->ptr;
    union {
        size_t s;
        int i;
    } sz = {
        0
    };

    /* Is a timer active? */
    if (data->next_timeout.tv_sec > 0 || data->next_timeout.tv_usec > 0) {
        struct timeval timenow, timeleft;

        /* Read current socket timeout */
#  ifdef OPENSSL_SYS_WINDOWS
        int timeout;

        sz.i = sizeof(timeout);
        if (getsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                       (void *)&timeout, &sz.i) < 0) {
            perror("getsockopt");
        } else {
            data->socket_timeout.tv_sec = timeout / 1000;
            data->socket_timeout.tv_usec = (timeout % 1000) * 1000;
        }
#  else
        sz.i = sizeof(data->socket_timeout);
        if (getsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                       &(data->socket_timeout), (void *)&sz) < 0) {
            perror("getsockopt");
        } else if (sizeof(sz.s) != sizeof(sz.i) && sz.i == 0)
            OPENSSL_assert(sz.s <= sizeof(data->socket_timeout));
#  endif

        /* Get current time */
        get_current_time(&timenow);

        /* Calculate time left until timer expires */
        memcpy(&timeleft, &(data->next_timeout), sizeof(struct timeval));
        if (timeleft.tv_usec < timenow.tv_usec) {
            timeleft.tv_usec = 1000000 - timenow.tv_usec + timeleft.tv_usec;
            timeleft.tv_sec--;
        } else {
            timeleft.tv_usec -= timenow.tv_usec;
        }
        if (timeleft.tv_sec < timenow.tv_sec) {
            timeleft.tv_sec = 0;
            timeleft.tv_usec = 1;
        } else {
            timeleft.tv_sec -= timenow.tv_sec;
        }

        /*
         * Adjust socket timeout if next handshake message timer will expire
         * earlier.
         */
        if ((data->socket_timeout.tv_sec == 0
             && data->socket_timeout.tv_usec == 0)
            || (data->socket_timeout.tv_sec > timeleft.tv_sec)
            || (data->socket_timeout.tv_sec == timeleft.tv_sec
                && data->socket_timeout.tv_usec >= timeleft.tv_usec)) {
#  ifdef OPENSSL_SYS_WINDOWS
            timeout = timeleft.tv_sec * 1000 + timeleft.tv_usec / 1000;
            if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                           (void *)&timeout, sizeof(timeout)) < 0) {
                perror("setsockopt");
            }
#  else
            if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO, &timeleft,
                           sizeof(struct timeval)) < 0) {
                perror("setsockopt");
            }
#  endif
        }
    }
# endif
}

static void dgram_reset_rcv_timeout(BIO *b)
{
# if defined(SO_RCVTIMEO)
    bio_dgram_data *data = (bio_dgram_data *)b->ptr;

    /* Is a timer active? */
    if (data->next_timeout.tv_sec > 0 || data->next_timeout.tv_usec > 0) {
#  ifdef OPENSSL_SYS_WINDOWS
        int timeout = data->socket_timeout.tv_sec * 1000 +
            data->socket_timeout.tv_usec / 1000;
        if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                       (void *)&timeout, sizeof(timeout)) < 0) {
            perror("setsockopt");
        }
#  else
        if (setsockopt
            (b->num, SOL_SOCKET, SO_RCVTIMEO, &(data->socket_timeout),
             sizeof(struct timeval)) < 0) {
            perror("setsockopt");
        }
#  endif
    }
# endif
}

static int dgram_read(BIO *b, char *out, int outl)
{
    int ret = 0;
    bio_dgram_data *data = (bio_dgram_data *)b->ptr;
    int flags = 0;

    BIO_ADDR peer;
    socklen_t len = sizeof(peer);

    if (out != NULL) {
        clear_socket_error();
        memset(&peer, 0, sizeof(peer));
        dgram_adjust_rcv_timeout(b);
        if (data->peekmode)
            flags = MSG_PEEK;
        ret = recvfrom(b->num, out, outl, flags,
                       BIO_ADDR_sockaddr_noconst(&peer), &len);

        if (!data->connected && ret >= 0)
            BIO_ctrl(b, BIO_CTRL_DGRAM_SET_PEER, 0, &peer);

        BIO_clear_retry_flags(b);
        if (ret < 0) {
            if (BIO_dgram_should_retry(ret)) {
                BIO_set_retry_read(b);
                data->_errno = get_last_socket_error();
            }
        }

        dgram_reset_rcv_timeout(b);
    }
    return ret;
}

static int dgram_write(BIO *b, const char *in, int inl)
{
    int ret;
    bio_dgram_data *data = (bio_dgram_data *)b->ptr;
    clear_socket_error();

    if (data->connected)
        ret = writesocket(b->num, in, inl);
    else {
        int peerlen = BIO_ADDR_sockaddr_size(&data->peer);

        ret = sendto(b->num, in, inl, 0,
                     BIO_ADDR_sockaddr(&data->peer), peerlen);
    }

    BIO_clear_retry_flags(b);
    if (ret <= 0) {
        if (BIO_dgram_should_retry(ret)) {
            BIO_set_retry_write(b);
            data->_errno = get_last_socket_error();
        }
    }
    return ret;
}

static long dgram_get_mtu_overhead(bio_dgram_data *data)
{
    long ret;

    switch (BIO_ADDR_family(&data->peer)) {
    case AF_INET:
        /*
         * Assume this is UDP - 20 bytes for IP, 8 bytes for UDP
         */
        ret = 28;
        break;
# if OPENSSL_USE_IPV6
    case AF_INET6:
        {
#  ifdef IN6_IS_ADDR_V4MAPPED
            struct in6_addr tmp_addr;
            if (BIO_ADDR_rawaddress(&data->peer, &tmp_addr, NULL)
                && IN6_IS_ADDR_V4MAPPED(&tmp_addr))
                /*
                 * Assume this is UDP - 20 bytes for IP, 8 bytes for UDP
                 */
                ret = 28;
            else
#  endif
            /*
             * Assume this is UDP - 40 bytes for IP, 8 bytes for UDP
             */
            ret = 48;
        }
        break;
# endif
    default:
        /* We don't know. Go with the historical default */
        ret = 28;
        break;
    }
    return ret;
}

static long dgram_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    long ret = 1;
    int *ip;
    bio_dgram_data *data = NULL;
    int sockopt_val = 0;
    int d_errno;
# if defined(OPENSSL_SYS_LINUX) && (defined(IP_MTU_DISCOVER) || defined(IP_MTU))
    socklen_t sockopt_len;      /* assume that system supporting IP_MTU is
                                 * modern enough to define socklen_t */
    socklen_t addr_len;
    BIO_ADDR addr;
# endif

    data = (bio_dgram_data *)b->ptr;

    switch (cmd) {
    case BIO_CTRL_RESET:
        num = 0;
        ret = 0;
        break;
    case BIO_CTRL_INFO:
        ret = 0;
        break;
    case BIO_C_SET_FD:
        dgram_clear(b);
        b->num = *((int *)ptr);
        b->shutdown = (int)num;
        b->init = 1;
        break;
    case BIO_C_GET_FD:
        if (b->init) {
            ip = (int *)ptr;
            if (ip != NULL)
                *ip = b->num;
            ret = b->num;
        } else
            ret = -1;
        break;
    case BIO_CTRL_GET_CLOSE:
        ret = b->shutdown;
        break;
    case BIO_CTRL_SET_CLOSE:
        b->shutdown = (int)num;
        break;
    case BIO_CTRL_PENDING:
    case BIO_CTRL_WPENDING:
        ret = 0;
        break;
    case BIO_CTRL_DUP:
    case BIO_CTRL_FLUSH:
        ret = 1;
        break;
    case BIO_CTRL_DGRAM_CONNECT:
        BIO_ADDR_make(&data->peer, BIO_ADDR_sockaddr((BIO_ADDR *)ptr));
        break;
        /* (Linux)kernel sets DF bit on outgoing IP packets */
    case BIO_CTRL_DGRAM_MTU_DISCOVER:
# if defined(OPENSSL_SYS_LINUX) && defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DO)
        addr_len = (socklen_t) sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        if (getsockname(b->num, &addr.sa, &addr_len) < 0) {
            ret = 0;
            break;
        }
        switch (addr.sa.sa_family) {
        case AF_INET:
            sockopt_val = IP_PMTUDISC_DO;
            if ((ret = setsockopt(b->num, IPPROTO_IP, IP_MTU_DISCOVER,
                                  &sockopt_val, sizeof(sockopt_val))) < 0)
                perror("setsockopt");
            break;
#  if OPENSSL_USE_IPV6 && defined(IPV6_MTU_DISCOVER) && defined(IPV6_PMTUDISC_DO)
        case AF_INET6:
            sockopt_val = IPV6_PMTUDISC_DO;
            if ((ret = setsockopt(b->num, IPPROTO_IPV6, IPV6_MTU_DISCOVER,
                                  &sockopt_val, sizeof(sockopt_val))) < 0)
                perror("setsockopt");
            break;
#  endif
        default:
            ret = -1;
            break;
        }
# else
        ret = -1;
# endif
        break;
    case BIO_CTRL_DGRAM_QUERY_MTU:
# if defined(OPENSSL_SYS_LINUX) && defined(IP_MTU)
        addr_len = (socklen_t) sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        if (getsockname(b->num, &addr.sa, &addr_len) < 0) {
            ret = 0;
            break;
        }
        sockopt_len = sizeof(sockopt_val);
        switch (addr.sa.sa_family) {
        case AF_INET:
            if ((ret =
                 getsockopt(b->num, IPPROTO_IP, IP_MTU, (void *)&sockopt_val,
                            &sockopt_len)) < 0 || sockopt_val < 0) {
                ret = 0;
            } else {
                /*
                 * we assume that the transport protocol is UDP and no IP
                 * options are used.
                 */
                data->mtu = sockopt_val - 8 - 20;
                ret = data->mtu;
            }
            break;
#  if OPENSSL_USE_IPV6 && defined(IPV6_MTU)
        case AF_INET6:
            if ((ret =
                 getsockopt(b->num, IPPROTO_IPV6, IPV6_MTU,
                            (void *)&sockopt_val, &sockopt_len)) < 0
                || sockopt_val < 0) {
                ret = 0;
            } else {
                /*
                 * we assume that the transport protocol is UDP and no IPV6
                 * options are used.
                 */
                data->mtu = sockopt_val - 8 - 40;
                ret = data->mtu;
            }
            break;
#  endif
        default:
            ret = 0;
            break;
        }
# else
        ret = 0;
# endif
        break;
    case BIO_CTRL_DGRAM_GET_FALLBACK_MTU:
        ret = -dgram_get_mtu_overhead(data);
        switch (BIO_ADDR_family(&data->peer)) {
        case AF_INET:
            ret += 576;
            break;
# if OPENSSL_USE_IPV6
        case AF_INET6:
            {
#  ifdef IN6_IS_ADDR_V4MAPPED
                struct in6_addr tmp_addr;
                if (BIO_ADDR_rawaddress(&data->peer, &tmp_addr, NULL)
                    && IN6_IS_ADDR_V4MAPPED(&tmp_addr))
                    ret += 576;
                else
#  endif
                    ret += 1280;
            }
            break;
# endif
        default:
            ret += 576;
            break;
        }
        break;
    case BIO_CTRL_DGRAM_GET_MTU:
        return data->mtu;
    case BIO_CTRL_DGRAM_SET_MTU:
        data->mtu = num;
        ret = num;
        break;
    case BIO_CTRL_DGRAM_SET_CONNECTED:
        if (ptr != NULL) {
            data->connected = 1;
            BIO_ADDR_make(&data->peer, BIO_ADDR_sockaddr((BIO_ADDR *)ptr));
        } else {
            data->connected = 0;
            memset(&data->peer, 0, sizeof(data->peer));
        }
        break;
    case BIO_CTRL_DGRAM_GET_PEER:
        ret = BIO_ADDR_sockaddr_size(&data->peer);
        /* FIXME: if num < ret, we will only return part of an address.
           That should bee an error, no? */
        if (num == 0 || num > ret)
            num = ret;
        memcpy(ptr, &data->peer, (ret = num));
        break;
    case BIO_CTRL_DGRAM_SET_PEER:
        BIO_ADDR_make(&data->peer, BIO_ADDR_sockaddr((BIO_ADDR *)ptr));
        break;
    case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
        memcpy(&(data->next_timeout), ptr, sizeof(struct timeval));
        break;
# if defined(SO_RCVTIMEO)
    case BIO_CTRL_DGRAM_SET_RECV_TIMEOUT:
#  ifdef OPENSSL_SYS_WINDOWS
        {
            struct timeval *tv = (struct timeval *)ptr;
            int timeout = tv->tv_sec * 1000 + tv->tv_usec / 1000;
            if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                           (void *)&timeout, sizeof(timeout)) < 0) {
                perror("setsockopt");
                ret = -1;
            }
        }
#  else
        if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO, ptr,
                       sizeof(struct timeval)) < 0) {
            perror("setsockopt");
            ret = -1;
        }
#  endif
        break;
    case BIO_CTRL_DGRAM_GET_RECV_TIMEOUT:
        {
            union {
                size_t s;
                int i;
            } sz = {
                0
            };
#  ifdef OPENSSL_SYS_WINDOWS
            int timeout;
            struct timeval *tv = (struct timeval *)ptr;

            sz.i = sizeof(timeout);
            if (getsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                           (void *)&timeout, &sz.i) < 0) {
                perror("getsockopt");
                ret = -1;
            } else {
                tv->tv_sec = timeout / 1000;
                tv->tv_usec = (timeout % 1000) * 1000;
                ret = sizeof(*tv);
            }
#  else
            sz.i = sizeof(struct timeval);
            if (getsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
                           ptr, (void *)&sz) < 0) {
                perror("getsockopt");
                ret = -1;
            } else if (sizeof(sz.s) != sizeof(sz.i) && sz.i == 0) {
                OPENSSL_assert(sz.s <= sizeof(struct timeval));
                ret = (int)sz.s;
            } else
                ret = sz.i;
#  endif
        }
        break;
# endif
# if defined(SO_SNDTIMEO)
    case BIO_CTRL_DGRAM_SET_SEND_TIMEOUT:
#  ifdef OPENSSL_SYS_WINDOWS
        {
            struct timeval *tv = (struct timeval *)ptr;
            int timeout = tv->tv_sec * 1000 + tv->tv_usec / 1000;
            if (setsockopt(b->num, SOL_SOCKET, SO_SNDTIMEO,
                           (void *)&timeout, sizeof(timeout)) < 0) {
                perror("setsockopt");
                ret = -1;
            }
        }
#  else
        if (setsockopt(b->num, SOL_SOCKET, SO_SNDTIMEO, ptr,
                       sizeof(struct timeval)) < 0) {
            perror("setsockopt");
            ret = -1;
        }
#  endif
        break;
    case BIO_CTRL_DGRAM_GET_SEND_TIMEOUT:
        {
            union {
                size_t s;
                int i;
            } sz = {
                0
            };
#  ifdef OPENSSL_SYS_WINDOWS
            int timeout;
            struct timeval *tv = (struct timeval *)ptr;

            sz.i = sizeof(timeout);
            if (getsockopt(b->num, SOL_SOCKET, SO_SNDTIMEO,
                           (void *)&timeout, &sz.i) < 0) {
                perror("getsockopt");
                ret = -1;
            } else {
                tv->tv_sec = timeout / 1000;
                tv->tv_usec = (timeout % 1000) * 1000;
                ret = sizeof(*tv);
            }
#  else
            sz.i = sizeof(struct timeval);
            if (getsockopt(b->num, SOL_SOCKET, SO_SNDTIMEO,
                           ptr, (void *)&sz) < 0) {
                perror("getsockopt");
                ret = -1;
            } else if (sizeof(sz.s) != sizeof(sz.i) && sz.i == 0) {
                OPENSSL_assert(sz.s <= sizeof(struct timeval));
                ret = (int)sz.s;
            } else
                ret = sz.i;
#  endif
        }
        break;
# endif
    case BIO_CTRL_DGRAM_GET_SEND_TIMER_EXP:
        /* fall-through */
    case BIO_CTRL_DGRAM_GET_RECV_TIMER_EXP:
# ifdef OPENSSL_SYS_WINDOWS
        d_errno = (data->_errno == WSAETIMEDOUT);
# else
        d_errno = (data->_errno == EAGAIN);
# endif
        if (d_errno) {
            ret = 1;
            data->_errno = 0;
        } else
            ret = 0;
        break;
# ifdef EMSGSIZE
    case BIO_CTRL_DGRAM_MTU_EXCEEDED:
        if (data->_errno == EMSGSIZE) {
            ret = 1;
            data->_errno = 0;
        } else
            ret = 0;
        break;
# endif
    case BIO_CTRL_DGRAM_SET_DONT_FRAG:
        sockopt_val = num ? 1 : 0;

        switch (data->peer.sa.sa_family) {
        case AF_INET:
# if defined(IP_DONTFRAG)
            if ((ret = setsockopt(b->num, IPPROTO_IP, IP_DONTFRAG,
                                  &sockopt_val, sizeof(sockopt_val))) < 0) {
                perror("setsockopt");
                ret = -1;
            }
# elif defined(OPENSSL_SYS_LINUX) && defined(IP_MTU_DISCOVER) && defined (IP_PMTUDISC_PROBE)
            if ((sockopt_val = num ? IP_PMTUDISC_PROBE : IP_PMTUDISC_DONT),
                (ret = setsockopt(b->num, IPPROTO_IP, IP_MTU_DISCOVER,
                                  &sockopt_val, sizeof(sockopt_val))) < 0) {
                perror("setsockopt");
                ret = -1;
            }
# elif defined(OPENSSL_SYS_WINDOWS) && defined(IP_DONTFRAGMENT)
            if ((ret = setsockopt(b->num, IPPROTO_IP, IP_DONTFRAGMENT,
                                  (const char *)&sockopt_val,
                                  sizeof(sockopt_val))) < 0) {
                perror("setsockopt");
                ret = -1;
            }
# else
            ret = -1;
# endif
            break;
# if OPENSSL_USE_IPV6
        case AF_INET6:
#  if defined(IPV6_DONTFRAG)
            if ((ret = setsockopt(b->num, IPPROTO_IPV6, IPV6_DONTFRAG,
                                  (const void *)&sockopt_val,
                                  sizeof(sockopt_val))) < 0) {
                perror("setsockopt");
                ret = -1;
            }
#  elif defined(OPENSSL_SYS_LINUX) && defined(IPV6_MTUDISCOVER)
            if ((sockopt_val = num ? IP_PMTUDISC_PROBE : IP_PMTUDISC_DONT),
                (ret = setsockopt(b->num, IPPROTO_IPV6, IPV6_MTU_DISCOVER,
                                  &sockopt_val, sizeof(sockopt_val))) < 0) {
                perror("setsockopt");
                ret = -1;
            }
#  else
            ret = -1;
#  endif
            break;
# endif
        default:
            ret = -1;
            break;
        }
        break;
    case BIO_CTRL_DGRAM_GET_MTU_OVERHEAD:
        ret = dgram_get_mtu_overhead(data);
        break;

    /*
     * BIO_CTRL_DGRAM_SCTP_SET_IN_HANDSHAKE is used here for compatibility
     * reasons. When BIO_CTRL_DGRAM_SET_PEEK_MODE was first defined its value
     * was incorrectly clashing with BIO_CTRL_DGRAM_SCTP_SET_IN_HANDSHAKE. The
     * value has been updated to a non-clashing value. However to preserve
     * binary compatiblity we now respond to both the old value and the new one
     */
    case BIO_CTRL_DGRAM_SCTP_SET_IN_HANDSHAKE:
    case BIO_CTRL_DGRAM_SET_PEEK_MODE:
        data->peekmode = (unsigned int)num;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}

static int dgram_puts(BIO *bp, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = dgram_write(bp, str, n);
    return ret;
}

# ifndef OPENSSL_NO_SCTP
const BIO_METHOD *BIO_s_datagram_sctp(void)
{
    return &methods_dgramp_sctp;
}

BIO *BIO_new_dgram_sctp(int fd, int close_flag)
{
    BIO *bio;
    int ret, optval = 20000;
    int auth_data = 0, auth_forward = 0;
    unsigned char *p;
    struct sctp_authchunk auth;
    struct sctp_authchunks *authchunks;
    socklen_t sockopt_len;
#  ifdef SCTP_AUTHENTICATION_EVENT
#   ifdef SCTP_EVENT
    struct sctp_event event;
#   else
    struct sctp_event_subscribe event;
#   endif
#  endif

    bio = BIO_new(BIO_s_datagram_sctp());
    if (bio == NULL)
        return NULL;
    BIO_set_fd(bio, fd, close_flag);

    /* Activate SCTP-AUTH for DATA and FORWARD-TSN chunks */
    auth.sauth_chunk = OPENSSL_SCTP_DATA_CHUNK_TYPE;
    ret =
        setsockopt(fd, IPPROTO_SCTP, SCTP_AUTH_CHUNK, &auth,
                   sizeof(struct sctp_authchunk));
    if (ret < 0) {
        BIO_vfree(bio);
        BIOerr(BIO_F_BIO_NEW_DGRAM_SCTP, ERR_R_SYS_LIB);
        ERR_add_error_data(1, "Ensure SCTP AUTH chunks are enabled in kernel");
        return NULL;
    }
    auth.sauth_chunk = OPENSSL_SCTP_FORWARD_CUM_TSN_CHUNK_TYPE;
    ret =
        setsockopt(fd, IPPROTO_SCTP, SCTP_AUTH_CHUNK, &auth,
                   sizeof(struct sctp_authchunk));
    if (ret < 0) {
        BIO_vfree(bio);
        BIOerr(BIO_F_BIO_NEW_DGRAM_SCTP, ERR_R_SYS_LIB);
        ERR_add_error_data(1, "Ensure SCTP AUTH chunks are enabled in kernel");
        return NULL;
    }

    /*
     * Test if activation was successful. When using accept(), SCTP-AUTH has
     * to be activated for the listening socket already, otherwise the
     * connected socket won't use it. Similarly with connect(): the socket
     * prior to connection must be activated for SCTP-AUTH
     */
    sockopt_len = (socklen_t) (sizeof(sctp_assoc_t) + 256 * sizeof(uint8_t));
    authchunks = OPENSSL_zalloc(sockopt_len);
    if (authchunks == NULL) {
        BIO_vfree(bio);
        return NULL;
    }
    ret = getsockopt(fd, IPPROTO_SCTP, SCTP_LOCAL_AUTH_CHUNKS, authchunks,
                   &sockopt_len);
    if (ret < 0) {
        OPENSSL_free(authchunks);
        BIO_vfree(bio);
        return NULL;
    }

    for (p = (unsigned char *)authchunks->gauth_chunks;
         p < (unsigned char *)authchunks + sockopt_len;
         p += sizeof(uint8_t)) {
        if (*p == OPENSSL_SCTP_DATA_CHUNK_TYPE)
            auth_data = 1;
        if (*p == OPENSSL_SCTP_FORWARD_CUM_TSN_CHUNK_TYPE)
            auth_forward = 1;
    }

    OPENSSL_free(authchunks);

    if (!auth_data || !auth_forward) {
        BIO_vfree(bio);
        BIOerr(BIO_F_BIO_NEW_DGRAM_SCTP, ERR_R_SYS_LIB);
        ERR_add_error_data(1,
                           "Ensure SCTP AUTH chunks are enabled on the "
                           "underlying socket");
        return NULL;
    }

#  ifdef SCTP_AUTHENTICATION_EVENT
#   ifdef SCTP_EVENT
    memset(&event, 0, sizeof(event));
    event.se_assoc_id = 0;
    event.se_type = SCTP_AUTHENTICATION_EVENT;
    event.se_on = 1;
    ret =
        setsockopt(fd, IPPROTO_SCTP, SCTP_EVENT, &event,
                   sizeof(struct sctp_event));
    if (ret < 0) {
        BIO_vfree(bio);
        return NULL;
    }
#   else
    sockopt_len = (socklen_t) sizeof(struct sctp_event_subscribe);
    ret = getsockopt(fd, IPPROTO_SCTP, SCTP_EVENTS, &event, &sockopt_len);
    if (ret < 0) {
        BIO_vfree(bio);
        return NULL;
    }

    event.sctp_authentication_event = 1;

    ret =
        setsockopt(fd, IPPROTO_SCTP, SCTP_EVENTS, &event,
                   sizeof(struct sctp_event_subscribe));
    if (ret < 0) {
        BIO_vfree(bio);
        return NULL;
    }
#   endif
#  endif

    /*
     * Disable partial delivery by setting the min size larger than the max
     * record size of 2^14 + 2048 + 13
     */
    ret =
        setsockopt(fd, IPPROTO_SCTP, SCTP_PARTIAL_DELIVERY_POINT, &optval,
                   sizeof(optval));
    if (ret < 0) {
        BIO_vfree(bio);
        return NULL;
    }

    return bio;
}

int BIO_dgram_is_sctp(BIO *bio)
{
    return (BIO_method_type(bio) == BIO_TYPE_DGRAM_SCTP);
}

static int dgram_sctp_new(BIO *bi)
{
    bio_dgram_sctp_data *data = NULL;

    bi->init = 0;
    bi->num = 0;
    if ((data = OPENSSL_zalloc(sizeof(*data))) == NULL) {
        BIOerr(BIO_F_DGRAM_SCTP_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }
#  ifdef SCTP_PR_SCTP_NONE
    data->prinfo.pr_policy = SCTP_PR_SCTP_NONE;
#  endif
    bi->ptr = data;

    bi->flags = 0;
    return 1;
}

static int dgram_sctp_free(BIO *a)
{
    bio_dgram_sctp_data *data;

    if (a == NULL)
        return 0;
    if (!dgram_clear(a))
        return 0;

    data = (bio_dgram_sctp_data *) a->ptr;
    if (data != NULL)
        OPENSSL_free(data);

    return 1;
}

#  ifdef SCTP_AUTHENTICATION_EVENT
void dgram_sctp_handle_auth_free_key_event(BIO *b,
                                           union sctp_notification *snp)
{
    int ret;
    struct sctp_authkey_event *authkeyevent = &snp->sn_auth_event;

    if (authkeyevent->auth_indication == SCTP_AUTH_FREE_KEY) {
        struct sctp_authkeyid authkeyid;

        /* delete key */
        authkeyid.scact_keynumber = authkeyevent->auth_keynumber;
        ret = setsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_DELETE_KEY,
                         &authkeyid, sizeof(struct sctp_authkeyid));
    }
}
#  endif

static int dgram_sctp_read(BIO *b, char *out, int outl)
{
    int ret = 0, n = 0, i, optval;
    socklen_t optlen;
    bio_dgram_sctp_data *data = (bio_dgram_sctp_data *) b->ptr;
    union sctp_notification *snp;
    struct msghdr msg;
    struct iovec iov;
    struct cmsghdr *cmsg;
    char cmsgbuf[512];

    if (out != NULL) {
        clear_socket_error();

        do {
            memset(&data->rcvinfo, 0, sizeof(data->rcvinfo));
            iov.iov_base = out;
            iov.iov_len = outl;
            msg.msg_name = NULL;
            msg.msg_namelen = 0;
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = cmsgbuf;
            msg.msg_controllen = 512;
            msg.msg_flags = 0;
            n = recvmsg(b->num, &msg, 0);

            if (n <= 0) {
                if (n < 0)
                    ret = n;
                break;
            }

            if (msg.msg_controllen > 0) {
                for (cmsg = CMSG_FIRSTHDR(&msg); cmsg;
                     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                    if (cmsg->cmsg_level != IPPROTO_SCTP)
                        continue;
#  ifdef SCTP_RCVINFO
                    if (cmsg->cmsg_type == SCTP_RCVINFO) {
                        struct sctp_rcvinfo *rcvinfo;

                        rcvinfo = (struct sctp_rcvinfo *)CMSG_DATA(cmsg);
                        data->rcvinfo.rcv_sid = rcvinfo->rcv_sid;
                        data->rcvinfo.rcv_ssn = rcvinfo->rcv_ssn;
                        data->rcvinfo.rcv_flags = rcvinfo->rcv_flags;
                        data->rcvinfo.rcv_ppid = rcvinfo->rcv_ppid;
                        data->rcvinfo.rcv_tsn = rcvinfo->rcv_tsn;
                        data->rcvinfo.rcv_cumtsn = rcvinfo->rcv_cumtsn;
                        data->rcvinfo.rcv_context = rcvinfo->rcv_context;
                    }
#  endif
#  ifdef SCTP_SNDRCV
                    if (cmsg->cmsg_type == SCTP_SNDRCV) {
                        struct sctp_sndrcvinfo *sndrcvinfo;

                        sndrcvinfo =
                            (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
                        data->rcvinfo.rcv_sid = sndrcvinfo->sinfo_stream;
                        data->rcvinfo.rcv_ssn = sndrcvinfo->sinfo_ssn;
                        data->rcvinfo.rcv_flags = sndrcvinfo->sinfo_flags;
                        data->rcvinfo.rcv_ppid = sndrcvinfo->sinfo_ppid;
                        data->rcvinfo.rcv_tsn = sndrcvinfo->sinfo_tsn;
                        data->rcvinfo.rcv_cumtsn = sndrcvinfo->sinfo_cumtsn;
                        data->rcvinfo.rcv_context = sndrcvinfo->sinfo_context;
                    }
#  endif
                }
            }

            if (msg.msg_flags & MSG_NOTIFICATION) {
                snp = (union sctp_notification *)out;
                if (snp->sn_header.sn_type == SCTP_SENDER_DRY_EVENT) {
#  ifdef SCTP_EVENT
                    struct sctp_event event;
#  else
                    struct sctp_event_subscribe event;
                    socklen_t eventsize;
#  endif

                    /* disable sender dry event */
#  ifdef SCTP_EVENT
                    memset(&event, 0, sizeof(event));
                    event.se_assoc_id = 0;
                    event.se_type = SCTP_SENDER_DRY_EVENT;
                    event.se_on = 0;
                    i = setsockopt(b->num, IPPROTO_SCTP, SCTP_EVENT, &event,
                                   sizeof(struct sctp_event));
                    if (i < 0) {
                        ret = i;
                        break;
                    }
#  else
                    eventsize = sizeof(struct sctp_event_subscribe);
                    i = getsockopt(b->num, IPPROTO_SCTP, SCTP_EVENTS, &event,
                                   &eventsize);
                    if (i < 0) {
                        ret = i;
                        break;
                    }

                    event.sctp_sender_dry_event = 0;

                    i = setsockopt(b->num, IPPROTO_SCTP, SCTP_EVENTS, &event,
                                   sizeof(struct sctp_event_subscribe));
                    if (i < 0) {
                        ret = i;
                        break;
                    }
#  endif
                }
#  ifdef SCTP_AUTHENTICATION_EVENT
                if (snp->sn_header.sn_type == SCTP_AUTHENTICATION_EVENT)
                    dgram_sctp_handle_auth_free_key_event(b, snp);
#  endif

                if (data->handle_notifications != NULL)
                    data->handle_notifications(b, data->notification_context,
                                               (void *)out);

                memset(out, 0, outl);
            } else
                ret += n;
        }
        while ((msg.msg_flags & MSG_NOTIFICATION) && (msg.msg_flags & MSG_EOR)
               && (ret < outl));

        if (ret > 0 && !(msg.msg_flags & MSG_EOR)) {
            /* Partial message read, this should never happen! */

            /*
             * The buffer was too small, this means the peer sent a message
             * that was larger than allowed.
             */
            if (ret == outl)
                return -1;

            /*
             * Test if socket buffer can handle max record size (2^14 + 2048
             * + 13)
             */
            optlen = (socklen_t) sizeof(int);
            ret = getsockopt(b->num, SOL_SOCKET, SO_RCVBUF, &optval, &optlen);
            if (ret >= 0)
                OPENSSL_assert(optval >= 18445);

            /*
             * Test if SCTP doesn't partially deliver below max record size
             * (2^14 + 2048 + 13)
             */
            optlen = (socklen_t) sizeof(int);
            ret =
                getsockopt(b->num, IPPROTO_SCTP, SCTP_PARTIAL_DELIVERY_POINT,
                           &optval, &optlen);
            if (ret >= 0)
                OPENSSL_assert(optval >= 18445);

            /*
             * Partially delivered notification??? Probably a bug....
             */
            OPENSSL_assert(!(msg.msg_flags & MSG_NOTIFICATION));

            /*
             * Everything seems ok till now, so it's most likely a message
             * dropped by PR-SCTP.
             */
            memset(out, 0, outl);
            BIO_set_retry_read(b);
            return -1;
        }

        BIO_clear_retry_flags(b);
        if (ret < 0) {
            if (BIO_dgram_should_retry(ret)) {
                BIO_set_retry_read(b);
                data->_errno = get_last_socket_error();
            }
        }

        /* Test if peer uses SCTP-AUTH before continuing */
        if (!data->peer_auth_tested) {
            int ii, auth_data = 0, auth_forward = 0;
            unsigned char *p;
            struct sctp_authchunks *authchunks;

            optlen =
                (socklen_t) (sizeof(sctp_assoc_t) + 256 * sizeof(uint8_t));
            authchunks = OPENSSL_malloc(optlen);
            if (authchunks == NULL) {
                BIOerr(BIO_F_DGRAM_SCTP_READ, ERR_R_MALLOC_FAILURE);
                return -1;
            }
            memset(authchunks, 0, optlen);
            ii = getsockopt(b->num, IPPROTO_SCTP, SCTP_PEER_AUTH_CHUNKS,
                            authchunks, &optlen);

            if (ii >= 0)
                for (p = (unsigned char *)authchunks->gauth_chunks;
                     p < (unsigned char *)authchunks + optlen;
                     p += sizeof(uint8_t)) {
                    if (*p == OPENSSL_SCTP_DATA_CHUNK_TYPE)
                        auth_data = 1;
                    if (*p == OPENSSL_SCTP_FORWARD_CUM_TSN_CHUNK_TYPE)
                        auth_forward = 1;
                }

            OPENSSL_free(authchunks);

            if (!auth_data || !auth_forward) {
                BIOerr(BIO_F_DGRAM_SCTP_READ, BIO_R_CONNECT_ERROR);
                return -1;
            }

            data->peer_auth_tested = 1;
        }
    }
    return ret;
}

/*
 * dgram_sctp_write - send message on SCTP socket
 * @b: BIO to write to
 * @in: data to send
 * @inl: amount of bytes in @in to send
 *
 * Returns -1 on error or the sent amount of bytes on success
 */
static int dgram_sctp_write(BIO *b, const char *in, int inl)
{
    int ret;
    bio_dgram_sctp_data *data = (bio_dgram_sctp_data *) b->ptr;
    struct bio_dgram_sctp_sndinfo *sinfo = &(data->sndinfo);
    struct bio_dgram_sctp_prinfo *pinfo = &(data->prinfo);
    struct bio_dgram_sctp_sndinfo handshake_sinfo;
    struct iovec iov[1];
    struct msghdr msg;
    struct cmsghdr *cmsg;
#  if defined(SCTP_SNDINFO) && defined(SCTP_PRINFO)
    char cmsgbuf[CMSG_SPACE(sizeof(struct sctp_sndinfo)) +
                 CMSG_SPACE(sizeof(struct sctp_prinfo))];
    struct sctp_sndinfo *sndinfo;
    struct sctp_prinfo *prinfo;
#  else
    char cmsgbuf[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
    struct sctp_sndrcvinfo *sndrcvinfo;
#  endif

    clear_socket_error();

    /*
     * If we're send anything else than application data, disable all user
     * parameters and flags.
     */
    if (in[0] != 23) {
        memset(&handshake_sinfo, 0, sizeof(handshake_sinfo));
#  ifdef SCTP_SACK_IMMEDIATELY
        handshake_sinfo.snd_flags = SCTP_SACK_IMMEDIATELY;
#  endif
        sinfo = &handshake_sinfo;
    }

    /* We can only send a shutdown alert if the socket is dry */
    if (data->save_shutdown) {
        ret = BIO_dgram_sctp_wait_for_dry(b);
        if (ret < 0)
            return -1;
        if (ret == 0) {
            BIO_clear_retry_flags(b);
            BIO_set_retry_write(b);
            return -1;
        }
    }

    iov[0].iov_base = (char *)in;
    iov[0].iov_len = inl;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (caddr_t) cmsgbuf;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
#  if defined(SCTP_SNDINFO) && defined(SCTP_PRINFO)
    cmsg = (struct cmsghdr *)cmsgbuf;
    cmsg->cmsg_level = IPPROTO_SCTP;
    cmsg->cmsg_type = SCTP_SNDINFO;
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndinfo));
    sndinfo = (struct sctp_sndinfo *)CMSG_DATA(cmsg);
    memset(sndinfo, 0, sizeof(*sndinfo));
    sndinfo->snd_sid = sinfo->snd_sid;
    sndinfo->snd_flags = sinfo->snd_flags;
    sndinfo->snd_ppid = sinfo->snd_ppid;
    sndinfo->snd_context = sinfo->snd_context;
    msg.msg_controllen += CMSG_SPACE(sizeof(struct sctp_sndinfo));

    cmsg =
        (struct cmsghdr *)&cmsgbuf[CMSG_SPACE(sizeof(struct sctp_sndinfo))];
    cmsg->cmsg_level = IPPROTO_SCTP;
    cmsg->cmsg_type = SCTP_PRINFO;
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct sctp_prinfo));
    prinfo = (struct sctp_prinfo *)CMSG_DATA(cmsg);
    memset(prinfo, 0, sizeof(*prinfo));
    prinfo->pr_policy = pinfo->pr_policy;
    prinfo->pr_value = pinfo->pr_value;
    msg.msg_controllen += CMSG_SPACE(sizeof(struct sctp_prinfo));
#  else
    cmsg = (struct cmsghdr *)cmsgbuf;
    cmsg->cmsg_level = IPPROTO_SCTP;
    cmsg->cmsg_type = SCTP_SNDRCV;
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
    sndrcvinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
    memset(sndrcvinfo, 0, sizeof(*sndrcvinfo));
    sndrcvinfo->sinfo_stream = sinfo->snd_sid;
    sndrcvinfo->sinfo_flags = sinfo->snd_flags;
#   ifdef __FreeBSD__
    sndrcvinfo->sinfo_flags |= pinfo->pr_policy;
#   endif
    sndrcvinfo->sinfo_ppid = sinfo->snd_ppid;
    sndrcvinfo->sinfo_context = sinfo->snd_context;
    sndrcvinfo->sinfo_timetolive = pinfo->pr_value;
    msg.msg_controllen += CMSG_SPACE(sizeof(struct sctp_sndrcvinfo));
#  endif

    ret = sendmsg(b->num, &msg, 0);

    BIO_clear_retry_flags(b);
    if (ret <= 0) {
        if (BIO_dgram_should_retry(ret)) {
            BIO_set_retry_write(b);
            data->_errno = get_last_socket_error();
        }
    }
    return ret;
}

static long dgram_sctp_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    long ret = 1;
    bio_dgram_sctp_data *data = NULL;
    socklen_t sockopt_len = 0;
    struct sctp_authkeyid authkeyid;
    struct sctp_authkey *authkey = NULL;

    data = (bio_dgram_sctp_data *) b->ptr;

    switch (cmd) {
    case BIO_CTRL_DGRAM_QUERY_MTU:
        /*
         * Set to maximum (2^14) and ignore user input to enable transport
         * protocol fragmentation. Returns always 2^14.
         */
        data->mtu = 16384;
        ret = data->mtu;
        break;
    case BIO_CTRL_DGRAM_SET_MTU:
        /*
         * Set to maximum (2^14) and ignore input to enable transport
         * protocol fragmentation. Returns always 2^14.
         */
        data->mtu = 16384;
        ret = data->mtu;
        break;
    case BIO_CTRL_DGRAM_SET_CONNECTED:
    case BIO_CTRL_DGRAM_CONNECT:
        /* Returns always -1. */
        ret = -1;
        break;
    case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
        /*
         * SCTP doesn't need the DTLS timer Returns always 1.
         */
        break;
    case BIO_CTRL_DGRAM_GET_MTU_OVERHEAD:
        /*
         * We allow transport protocol fragmentation so this is irrelevant
         */
        ret = 0;
        break;
    case BIO_CTRL_DGRAM_SCTP_SET_IN_HANDSHAKE:
        if (num > 0)
            data->in_handshake = 1;
        else
            data->in_handshake = 0;

        ret =
            setsockopt(b->num, IPPROTO_SCTP, SCTP_NODELAY,
                       &data->in_handshake, sizeof(int));
        break;
    case BIO_CTRL_DGRAM_SCTP_ADD_AUTH_KEY:
        /*
         * New shared key for SCTP AUTH. Returns 0 on success, -1 otherwise.
         */

        /* Get active key */
        sockopt_len = sizeof(struct sctp_authkeyid);
        ret =
            getsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_ACTIVE_KEY, &authkeyid,
                       &sockopt_len);
        if (ret < 0)
            break;

        /* Add new key */
        sockopt_len = sizeof(struct sctp_authkey) + 64 * sizeof(uint8_t);
        authkey = OPENSSL_malloc(sockopt_len);
        if (authkey == NULL) {
            ret = -1;
            break;
        }
        memset(authkey, 0, sockopt_len);
        authkey->sca_keynumber = authkeyid.scact_keynumber + 1;
#  ifndef __FreeBSD__
        /*
         * This field is missing in FreeBSD 8.2 and earlier, and FreeBSD 8.3
         * and higher work without it.
         */
        authkey->sca_keylength = 64;
#  endif
        memcpy(&authkey->sca_key[0], ptr, 64 * sizeof(uint8_t));

        ret =
            setsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_KEY, authkey,
                       sockopt_len);
        OPENSSL_free(authkey);
        authkey = NULL;
        if (ret < 0)
            break;

        /* Reset active key */
        ret = setsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_ACTIVE_KEY,
                         &authkeyid, sizeof(struct sctp_authkeyid));
        if (ret < 0)
            break;

        break;
    case BIO_CTRL_DGRAM_SCTP_NEXT_AUTH_KEY:
        /* Returns 0 on success, -1 otherwise. */

        /* Get active key */
        sockopt_len = sizeof(struct sctp_authkeyid);
        ret =
            getsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_ACTIVE_KEY, &authkeyid,
                       &sockopt_len);
        if (ret < 0)
            break;

        /* Set active key */
        authkeyid.scact_keynumber = authkeyid.scact_keynumber + 1;
        ret = setsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_ACTIVE_KEY,
                         &authkeyid, sizeof(struct sctp_authkeyid));
        if (ret < 0)
            break;

        /*
         * CCS has been sent, so remember that and fall through to check if
         * we need to deactivate an old key
         */
        data->ccs_sent = 1;
        /* fall-through */

    case BIO_CTRL_DGRAM_SCTP_AUTH_CCS_RCVD:
        /* Returns 0 on success, -1 otherwise. */

        /*
         * Has this command really been called or is this just a
         * fall-through?
         */
        if (cmd == BIO_CTRL_DGRAM_SCTP_AUTH_CCS_RCVD)
            data->ccs_rcvd = 1;

        /*
         * CSS has been both, received and sent, so deactivate an old key
         */
        if (data->ccs_rcvd == 1 && data->ccs_sent == 1) {
            /* Get active key */
            sockopt_len = sizeof(struct sctp_authkeyid);
            ret =
                getsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_ACTIVE_KEY,
                           &authkeyid, &sockopt_len);
            if (ret < 0)
                break;

            /*
             * Deactivate key or delete second last key if
             * SCTP_AUTHENTICATION_EVENT is not available.
             */
            authkeyid.scact_keynumber = authkeyid.scact_keynumber - 1;
#  ifdef SCTP_AUTH_DEACTIVATE_KEY
            sockopt_len = sizeof(struct sctp_authkeyid);
            ret = setsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_DEACTIVATE_KEY,
                             &authkeyid, sockopt_len);
            if (ret < 0)
                break;
#  endif
#  ifndef SCTP_AUTHENTICATION_EVENT
            if (authkeyid.scact_keynumber > 0) {
                authkeyid.scact_keynumber = authkeyid.scact_keynumber - 1;
                ret = setsockopt(b->num, IPPROTO_SCTP, SCTP_AUTH_DELETE_KEY,
                                 &authkeyid, sizeof(struct sctp_authkeyid));
                if (ret < 0)
                    break;
            }
#  endif

            data->ccs_rcvd = 0;
            data->ccs_sent = 0;
        }
        break;
    case BIO_CTRL_DGRAM_SCTP_GET_SNDINFO:
        /* Returns the size of the copied struct. */
        if (num > (long)sizeof(struct bio_dgram_sctp_sndinfo))
            num = sizeof(struct bio_dgram_sctp_sndinfo);

        memcpy(ptr, &(data->sndinfo), num);
        ret = num;
        break;
    case BIO_CTRL_DGRAM_SCTP_SET_SNDINFO:
        /* Returns the size of the copied struct. */
        if (num > (long)sizeof(struct bio_dgram_sctp_sndinfo))
            num = sizeof(struct bio_dgram_sctp_sndinfo);

        memcpy(&(data->sndinfo), ptr, num);
        break;
    case BIO_CTRL_DGRAM_SCTP_GET_RCVINFO:
        /* Returns the size of the copied struct. */
        if (num > (long)sizeof(struct bio_dgram_sctp_rcvinfo))
            num = sizeof(struct bio_dgram_sctp_rcvinfo);

        memcpy(ptr, &data->rcvinfo, num);

        ret = num;
        break;
    case BIO_CTRL_DGRAM_SCTP_SET_RCVINFO:
        /* Returns the size of the copied struct. */
        if (num > (long)sizeof(struct bio_dgram_sctp_rcvinfo))
            num = sizeof(struct bio_dgram_sctp_rcvinfo);

        memcpy(&(data->rcvinfo), ptr, num);
        break;
    case BIO_CTRL_DGRAM_SCTP_GET_PRINFO:
        /* Returns the size of the copied struct. */
        if (num > (long)sizeof(struct bio_dgram_sctp_prinfo))
            num = sizeof(struct bio_dgram_sctp_prinfo);

        memcpy(ptr, &(data->prinfo), num);
        ret = num;
        break;
    case BIO_CTRL_DGRAM_SCTP_SET_PRINFO:
        /* Returns the size of the copied struct. */
        if (num > (long)sizeof(struct bio_dgram_sctp_prinfo))
            num = sizeof(struct bio_dgram_sctp_prinfo);

        memcpy(&(data->prinfo), ptr, num);
        break;
    case BIO_CTRL_DGRAM_SCTP_SAVE_SHUTDOWN:
        /* Returns always 1. */
        if (num > 0)
            data->save_shutdown = 1;
        else
            data->save_shutdown = 0;
        break;

    default:
        /*
         * Pass to default ctrl function to process SCTP unspecific commands
         */
        ret = dgram_ctrl(b, cmd, num, ptr);
        break;
    }
    return ret;
}

int BIO_dgram_sctp_notification_cb(BIO *b,
                                   void (*handle_notifications) (BIO *bio,
                                                                 void
                                                                 *context,
                                                                 void *buf),
                                   void *context)
{
    bio_dgram_sctp_data *data = (bio_dgram_sctp_data *) b->ptr;

    if (handle_notifications != NULL) {
        data->handle_notifications = handle_notifications;
        data->notification_context = context;
    } else
        return -1;

    return 0;
}

/*
 * BIO_dgram_sctp_wait_for_dry - Wait for SCTP SENDER_DRY event
 * @b: The BIO to check for the dry event
 *
 * Wait until the peer confirms all packets have been received, and so that
 * our kernel doesn't have anything to send anymore.  This is only received by
 * the peer's kernel, not the application.
 *
 * Returns:
 * -1 on error
 *  0 when not dry yet
 *  1 when dry
 */
int BIO_dgram_sctp_wait_for_dry(BIO *b)
{
    int is_dry = 0;
    int sockflags = 0;
    int n, ret;
    union sctp_notification snp;
    struct msghdr msg;
    struct iovec iov;
#  ifdef SCTP_EVENT
    struct sctp_event event;
#  else
    struct sctp_event_subscribe event;
    socklen_t eventsize;
#  endif
    bio_dgram_sctp_data *data = (bio_dgram_sctp_data *) b->ptr;

    /* set sender dry event */
#  ifdef SCTP_EVENT
    memset(&event, 0, sizeof(event));
    event.se_assoc_id = 0;
    event.se_type = SCTP_SENDER_DRY_EVENT;
    event.se_on = 1;
    ret =
        setsockopt(b->num, IPPROTO_SCTP, SCTP_EVENT, &event,
                   sizeof(struct sctp_event));
#  else
    eventsize = sizeof(struct sctp_event_subscribe);
    ret = getsockopt(b->num, IPPROTO_SCTP, SCTP_EVENTS, &event, &eventsize);
    if (ret < 0)
        return -1;

    event.sctp_sender_dry_event = 1;

    ret =
        setsockopt(b->num, IPPROTO_SCTP, SCTP_EVENTS, &event,
                   sizeof(struct sctp_event_subscribe));
#  endif
    if (ret < 0)
        return -1;

    /* peek for notification */
    memset(&snp, 0, sizeof(snp));
    iov.iov_base = (char *)&snp;
    iov.iov_len = sizeof(union sctp_notification);
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    n = recvmsg(b->num, &msg, MSG_PEEK);
    if (n <= 0) {
        if ((n < 0) && (get_last_socket_error() != EAGAIN)
            && (get_last_socket_error() != EWOULDBLOCK))
            return -1;
        else
            return 0;
    }

    /* if we find a notification, process it and try again if necessary */
    while (msg.msg_flags & MSG_NOTIFICATION) {
        memset(&snp, 0, sizeof(snp));
        iov.iov_base = (char *)&snp;
        iov.iov_len = sizeof(union sctp_notification);
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        n = recvmsg(b->num, &msg, 0);
        if (n <= 0) {
            if ((n < 0) && (get_last_socket_error() != EAGAIN)
                && (get_last_socket_error() != EWOULDBLOCK))
                return -1;
            else
                return is_dry;
        }

        if (snp.sn_header.sn_type == SCTP_SENDER_DRY_EVENT) {
            is_dry = 1;

            /* disable sender dry event */
#  ifdef SCTP_EVENT
            memset(&event, 0, sizeof(event));
            event.se_assoc_id = 0;
            event.se_type = SCTP_SENDER_DRY_EVENT;
            event.se_on = 0;
            ret =
                setsockopt(b->num, IPPROTO_SCTP, SCTP_EVENT, &event,
                           sizeof(struct sctp_event));
#  else
            eventsize = (socklen_t) sizeof(struct sctp_event_subscribe);
            ret =
                getsockopt(b->num, IPPROTO_SCTP, SCTP_EVENTS, &event,
                           &eventsize);
            if (ret < 0)
                return -1;

            event.sctp_sender_dry_event = 0;

            ret =
                setsockopt(b->num, IPPROTO_SCTP, SCTP_EVENTS, &event,
                           sizeof(struct sctp_event_subscribe));
#  endif
            if (ret < 0)
                return -1;
        }
#  ifdef SCTP_AUTHENTICATION_EVENT
        if (snp.sn_header.sn_type == SCTP_AUTHENTICATION_EVENT)
            dgram_sctp_handle_auth_free_key_event(b, &snp);
#  endif

        if (data->handle_notifications != NULL)
            data->handle_notifications(b, data->notification_context,
                                       (void *)&snp);

        /* found notification, peek again */
        memset(&snp, 0, sizeof(snp));
        iov.iov_base = (char *)&snp;
        iov.iov_len = sizeof(union sctp_notification);
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        /* if we have seen the dry already, don't wait */
        if (is_dry) {
            sockflags = fcntl(b->num, F_GETFL, 0);
            fcntl(b->num, F_SETFL, O_NONBLOCK);
        }

        n = recvmsg(b->num, &msg, MSG_PEEK);

        if (is_dry) {
            fcntl(b->num, F_SETFL, sockflags);
        }

        if (n <= 0) {
            if ((n < 0) && (get_last_socket_error() != EAGAIN)
                && (get_last_socket_error() != EWOULDBLOCK))
                return -1;
            else
                return is_dry;
        }
    }

    /* read anything else */
    return is_dry;
}

int BIO_dgram_sctp_msg_waiting(BIO *b)
{
    int n, sockflags;
    union sctp_notification snp;
    struct msghdr msg;
    struct iovec iov;
    bio_dgram_sctp_data *data = (bio_dgram_sctp_data *) b->ptr;

    /* Check if there are any messages waiting to be read */
    do {
        memset(&snp, 0, sizeof(snp));
        iov.iov_base = (char *)&snp;
        iov.iov_len = sizeof(union sctp_notification);
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        sockflags = fcntl(b->num, F_GETFL, 0);
        fcntl(b->num, F_SETFL, O_NONBLOCK);
        n = recvmsg(b->num, &msg, MSG_PEEK);
        fcntl(b->num, F_SETFL, sockflags);

        /* if notification, process and try again */
        if (n > 0 && (msg.msg_flags & MSG_NOTIFICATION)) {
#  ifdef SCTP_AUTHENTICATION_EVENT
            if (snp.sn_header.sn_type == SCTP_AUTHENTICATION_EVENT)
                dgram_sctp_handle_auth_free_key_event(b, &snp);
#  endif

            memset(&snp, 0, sizeof(snp));
            iov.iov_base = (char *)&snp;
            iov.iov_len = sizeof(union sctp_notification);
            msg.msg_name = NULL;
            msg.msg_namelen = 0;
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = NULL;
            msg.msg_controllen = 0;
            msg.msg_flags = 0;
            n = recvmsg(b->num, &msg, 0);

            if (data->handle_notifications != NULL)
                data->handle_notifications(b, data->notification_context,
                                           (void *)&snp);
        }

    } while (n > 0 && (msg.msg_flags & MSG_NOTIFICATION));

    /* Return 1 if there is a message to be read, return 0 otherwise. */
    if (n > 0)
        return 1;
    else
        return 0;
}

static int dgram_sctp_puts(BIO *bp, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = dgram_sctp_write(bp, str, n);
    return ret;
}
# endif

static int BIO_dgram_should_retry(int i)
{
    int err;

    if ((i == 0) || (i == -1)) {
        err = get_last_socket_error();

# if defined(OPENSSL_SYS_WINDOWS)
        /*
         * If the socket return value (i) is -1 and err is unexpectedly 0 at
         * this point, the error code was overwritten by another system call
         * before this error handling is called.
         */
# endif

        return BIO_dgram_non_fatal_error(err);
    }
    return 0;
}

int BIO_dgram_non_fatal_error(int err)
{
    switch (err) {
# if defined(OPENSSL_SYS_WINDOWS)
#  if defined(WSAEWOULDBLOCK)
    case WSAEWOULDBLOCK:
#  endif
# endif

# ifdef EWOULDBLOCK
#  ifdef WSAEWOULDBLOCK
#   if WSAEWOULDBLOCK != EWOULDBLOCK
    case EWOULDBLOCK:
#   endif
#  else
    case EWOULDBLOCK:
#  endif
# endif

# ifdef EINTR
    case EINTR:
# endif

# ifdef EAGAIN
#  if EWOULDBLOCK != EAGAIN
    case EAGAIN:
#  endif
# endif

# ifdef EPROTO
    case EPROTO:
# endif

# ifdef EINPROGRESS
    case EINPROGRESS:
# endif

# ifdef EALREADY
    case EALREADY:
# endif

        return 1;
    default:
        break;
    }
    return 0;
}

static void get_current_time(struct timeval *t)
{
# if defined(_WIN32)
    SYSTEMTIME st;
    union {
        unsigned __int64 ul;
        FILETIME ft;
    } now;

    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &now.ft);
#  ifdef  __MINGW32__
    now.ul -= 116444736000000000ULL;
#  else
    now.ul -= 116444736000000000UI64; /* re-bias to 1/1/1970 */
#  endif
    t->tv_sec = (long)(now.ul / 10000000);
    t->tv_usec = ((int)(now.ul % 10000000)) / 10;
# else
    gettimeofday(t, NULL);
# endif
}

#endif
