/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <errno.h>
#include "bio_lcl.h"

#ifndef OPENSSL_NO_SOCK

typedef struct bio_accept_st {
    int state;
    int accept_family;
    int bind_mode;     /* Socket mode for BIO_listen */
    int accepted_mode; /* Socket mode for BIO_accept (set on accepted sock) */
    char *param_addr;
    char *param_serv;

    int accept_sock;

    BIO_ADDRINFO *addr_first;
    const BIO_ADDRINFO *addr_iter;
    BIO_ADDR cache_accepting_addr;   /* Useful if we asked for port 0 */
    char *cache_accepting_name, *cache_accepting_serv;
    BIO_ADDR cache_peer_addr;
    char *cache_peer_name, *cache_peer_serv;

    BIO *bio_chain;
} BIO_ACCEPT;

static int acpt_write(BIO *h, const char *buf, int num);
static int acpt_read(BIO *h, char *buf, int size);
static int acpt_puts(BIO *h, const char *str);
static long acpt_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int acpt_new(BIO *h);
static int acpt_free(BIO *data);
static int acpt_state(BIO *b, BIO_ACCEPT *c);
static void acpt_close_socket(BIO *data);
static BIO_ACCEPT *BIO_ACCEPT_new(void);
static void BIO_ACCEPT_free(BIO_ACCEPT *a);

# define ACPT_S_BEFORE                   1
# define ACPT_S_GET_ADDR                 2
# define ACPT_S_CREATE_SOCKET            3
# define ACPT_S_LISTEN                   4
# define ACPT_S_ACCEPT                   5
# define ACPT_S_OK                       6

static const BIO_METHOD methods_acceptp = {
    BIO_TYPE_ACCEPT,
    "socket accept",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    acpt_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    acpt_read,
    acpt_puts,
    NULL,                       /* connect_gets,         */
    acpt_ctrl,
    acpt_new,
    acpt_free,
    NULL,                       /* connect_callback_ctrl */
};

const BIO_METHOD *BIO_s_accept(void)
{
    return &methods_acceptp;
}

static int acpt_new(BIO *bi)
{
    BIO_ACCEPT *ba;

    bi->init = 0;
    bi->num = (int)INVALID_SOCKET;
    bi->flags = 0;
    if ((ba = BIO_ACCEPT_new()) == NULL)
        return 0;
    bi->ptr = (char *)ba;
    ba->state = ACPT_S_BEFORE;
    bi->shutdown = 1;
    return 1;
}

static BIO_ACCEPT *BIO_ACCEPT_new(void)
{
    BIO_ACCEPT *ret;

    if ((ret = OPENSSL_zalloc(sizeof(*ret))) == NULL) {
        BIOerr(BIO_F_BIO_ACCEPT_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    ret->accept_family = BIO_FAMILY_IPANY;
    ret->accept_sock = (int)INVALID_SOCKET;
    return ret;
}

static void BIO_ACCEPT_free(BIO_ACCEPT *a)
{
    if (a == NULL)
        return;
    OPENSSL_free(a->param_addr);
    OPENSSL_free(a->param_serv);
    BIO_ADDRINFO_free(a->addr_first);
    OPENSSL_free(a->cache_accepting_name);
    OPENSSL_free(a->cache_accepting_serv);
    OPENSSL_free(a->cache_peer_name);
    OPENSSL_free(a->cache_peer_serv);
    BIO_free(a->bio_chain);
    OPENSSL_free(a);
}

static void acpt_close_socket(BIO *bio)
{
    BIO_ACCEPT *c;

    c = (BIO_ACCEPT *)bio->ptr;
    if (c->accept_sock != (int)INVALID_SOCKET) {
        shutdown(c->accept_sock, 2);
        closesocket(c->accept_sock);
        c->accept_sock = (int)INVALID_SOCKET;
        bio->num = (int)INVALID_SOCKET;
    }
}

static int acpt_free(BIO *a)
{
    BIO_ACCEPT *data;

    if (a == NULL)
        return 0;
    data = (BIO_ACCEPT *)a->ptr;

    if (a->shutdown) {
        acpt_close_socket(a);
        BIO_ACCEPT_free(data);
        a->ptr = NULL;
        a->flags = 0;
        a->init = 0;
    }
    return 1;
}

static int acpt_state(BIO *b, BIO_ACCEPT *c)
{
    BIO *bio = NULL, *dbio;
    int s = -1, ret = -1;

    for (;;) {
        switch (c->state) {
        case ACPT_S_BEFORE:
            if (c->param_addr == NULL && c->param_serv == NULL) {
                BIOerr(BIO_F_ACPT_STATE, BIO_R_NO_ACCEPT_ADDR_OR_SERVICE_SPECIFIED);
                ERR_add_error_data(4,
                                   "hostname=", c->param_addr,
                                   " service=", c->param_serv);
                goto exit_loop;
            }

            /* Because we're starting a new bind, any cached name and serv
             * are now obsolete and need to be cleaned out.
             * QUESTION: should this be done in acpt_close_socket() instead?
             */
            OPENSSL_free(c->cache_accepting_name);
            c->cache_accepting_name = NULL;
            OPENSSL_free(c->cache_accepting_serv);
            c->cache_accepting_serv = NULL;
            OPENSSL_free(c->cache_peer_name);
            c->cache_peer_name = NULL;
            OPENSSL_free(c->cache_peer_serv);
            c->cache_peer_serv = NULL;

            c->state = ACPT_S_GET_ADDR;
            break;

        case ACPT_S_GET_ADDR:
            {
                int family = AF_UNSPEC;
                switch (c->accept_family) {
                case BIO_FAMILY_IPV6:
                    if (1) { /* This is a trick we use to avoid bit rot.
                              * at least the "else" part will always be
                              * compiled.
                              */
#ifdef AF_INET6
                        family = AF_INET6;
                    } else {
#endif
                        BIOerr(BIO_F_ACPT_STATE, BIO_R_UNAVAILABLE_IP_FAMILY);
                        goto exit_loop;
                    }
                    break;
                case BIO_FAMILY_IPV4:
                    family = AF_INET;
                    break;
                case BIO_FAMILY_IPANY:
                    family = AF_UNSPEC;
                    break;
                default:
                    BIOerr(BIO_F_ACPT_STATE, BIO_R_UNSUPPORTED_IP_FAMILY);
                    goto exit_loop;
                }
                if (BIO_lookup(c->param_addr, c->param_serv, BIO_LOOKUP_SERVER,
                               family, SOCK_STREAM, &c->addr_first) == 0)
                    goto exit_loop;
            }
            if (c->addr_first == NULL) {
                BIOerr(BIO_F_ACPT_STATE, BIO_R_LOOKUP_RETURNED_NOTHING);
                goto exit_loop;
            }
            /* We're currently not iterating, but set this as preparation
             * for possible future development in that regard
             */
            c->addr_iter = c->addr_first;
            c->state = ACPT_S_CREATE_SOCKET;
            break;

        case ACPT_S_CREATE_SOCKET:
            ret = BIO_socket(BIO_ADDRINFO_family(c->addr_iter),
                             BIO_ADDRINFO_socktype(c->addr_iter),
                             BIO_ADDRINFO_protocol(c->addr_iter), 0);
            if (ret == (int)INVALID_SOCKET) {
                SYSerr(SYS_F_SOCKET, get_last_socket_error());
                ERR_add_error_data(4,
                                   "hostname=", c->param_addr,
                                   " service=", c->param_serv);
                BIOerr(BIO_F_ACPT_STATE, BIO_R_UNABLE_TO_CREATE_SOCKET);
                goto exit_loop;
            }
            c->accept_sock = ret;
            b->num = ret;
            c->state = ACPT_S_LISTEN;
            break;

        case ACPT_S_LISTEN:
            {
                if (!BIO_listen(c->accept_sock,
                                BIO_ADDRINFO_address(c->addr_iter),
                                c->bind_mode)) {
                    BIO_closesocket(c->accept_sock);
                    goto exit_loop;
                }
            }

            {
                union BIO_sock_info_u info;

                info.addr = &c->cache_accepting_addr;
                if (!BIO_sock_info(c->accept_sock, BIO_SOCK_INFO_ADDRESS,
                                   &info)) {
                    BIO_closesocket(c->accept_sock);
                    goto exit_loop;
                }
            }

            c->cache_accepting_name =
                BIO_ADDR_hostname_string(&c->cache_accepting_addr, 1);
            c->cache_accepting_serv =
                BIO_ADDR_service_string(&c->cache_accepting_addr, 1);
            c->state = ACPT_S_ACCEPT;
            s = -1;
            ret = 1;
            goto end;

        case ACPT_S_ACCEPT:
            if (b->next_bio != NULL) {
                c->state = ACPT_S_OK;
                break;
            }
            BIO_clear_retry_flags(b);
            b->retry_reason = 0;

            OPENSSL_free(c->cache_peer_name);
            c->cache_peer_name = NULL;
            OPENSSL_free(c->cache_peer_serv);
            c->cache_peer_serv = NULL;

            s = BIO_accept_ex(c->accept_sock, &c->cache_peer_addr,
                              c->accepted_mode);

            /* If the returned socket is invalid, this might still be
             * retryable
             */
            if (s < 0) {
                if (BIO_sock_should_retry(s)) {
                    BIO_set_retry_special(b);
                    b->retry_reason = BIO_RR_ACCEPT;
                    goto end;
                }
            }

            /* If it wasn't retryable, we fail */
            if (s < 0) {
                ret = s;
                goto exit_loop;
            }

            bio = BIO_new_socket(s, BIO_CLOSE);
            if (bio == NULL)
                goto exit_loop;

            BIO_set_callback(bio, BIO_get_callback(b));
            BIO_set_callback_arg(bio, BIO_get_callback_arg(b));

            /*
             * If the accept BIO has an bio_chain, we dup it and put the new
             * socket at the end.
             */
            if (c->bio_chain != NULL) {
                if ((dbio = BIO_dup_chain(c->bio_chain)) == NULL)
                    goto exit_loop;
                if (!BIO_push(dbio, bio))
                    goto exit_loop;
                bio = dbio;
            }
            if (BIO_push(b, bio) == NULL)
                goto exit_loop;

            c->cache_peer_name =
                BIO_ADDR_hostname_string(&c->cache_peer_addr, 1);
            c->cache_peer_serv =
                BIO_ADDR_service_string(&c->cache_peer_addr, 1);
            c->state = ACPT_S_OK;
            bio = NULL;
            ret = 1;
            goto end;

        case ACPT_S_OK:
            if (b->next_bio == NULL) {
                c->state = ACPT_S_ACCEPT;
                break;
            }
            ret = 1;
            goto end;

        default:
            ret = 0;
            goto end;
        }
    }

  exit_loop:
    if (bio != NULL)
        BIO_free(bio);
    else if (s >= 0)
        BIO_closesocket(s);
  end:
    return ret;
}

static int acpt_read(BIO *b, char *out, int outl)
{
    int ret = 0;
    BIO_ACCEPT *data;

    BIO_clear_retry_flags(b);
    data = (BIO_ACCEPT *)b->ptr;

    while (b->next_bio == NULL) {
        ret = acpt_state(b, data);
        if (ret <= 0)
            return ret;
    }

    ret = BIO_read(b->next_bio, out, outl);
    BIO_copy_next_retry(b);
    return ret;
}

static int acpt_write(BIO *b, const char *in, int inl)
{
    int ret;
    BIO_ACCEPT *data;

    BIO_clear_retry_flags(b);
    data = (BIO_ACCEPT *)b->ptr;

    while (b->next_bio == NULL) {
        ret = acpt_state(b, data);
        if (ret <= 0)
            return ret;
    }

    ret = BIO_write(b->next_bio, in, inl);
    BIO_copy_next_retry(b);
    return ret;
}

static long acpt_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    int *ip;
    long ret = 1;
    BIO_ACCEPT *data;
    char **pp;

    data = (BIO_ACCEPT *)b->ptr;

    switch (cmd) {
    case BIO_CTRL_RESET:
        ret = 0;
        data->state = ACPT_S_BEFORE;
        acpt_close_socket(b);
        BIO_ADDRINFO_free(data->addr_first);
        data->addr_first = NULL;
        b->flags = 0;
        break;
    case BIO_C_DO_STATE_MACHINE:
        /* use this one to start the connection */
        ret = (long)acpt_state(b, data);
        break;
    case BIO_C_SET_ACCEPT:
        if (ptr != NULL) {
            if (num == 0) {
                char *hold_serv = data->param_serv;
                /* We affect the hostname regardless.  However, the input
                 * string might contain a host:service spec, so we must
                 * parse it, which might or might not affect the service
                 */
                OPENSSL_free(data->param_addr);
                data->param_addr = NULL;
                ret = BIO_parse_hostserv(ptr,
                                         &data->param_addr,
                                         &data->param_serv,
                                         BIO_PARSE_PRIO_SERV);
                if (hold_serv != data->param_serv)
                    OPENSSL_free(hold_serv);
                b->init = 1;
            } else if (num == 1) {
                OPENSSL_free(data->param_serv);
                data->param_serv = BUF_strdup(ptr);
                b->init = 1;
            } else if (num == 2) {
                data->bind_mode |= BIO_SOCK_NONBLOCK;
            } else if (num == 3) {
                BIO_free(data->bio_chain);
                data->bio_chain = (BIO *)ptr;
            } else if (num == 4) {
                data->accept_family = *(int *)ptr;
            }
        } else {
            if (num == 2) {
                data->bind_mode &= ~BIO_SOCK_NONBLOCK;
            }
        }
        break;
    case BIO_C_SET_NBIO:
        if (num != 0)
            data->accepted_mode |= BIO_SOCK_NONBLOCK;
        else
            data->accepted_mode &= ~BIO_SOCK_NONBLOCK;
        break;
    case BIO_C_SET_FD:
        b->num = *((int *)ptr);
        data->accept_sock = b->num;
        data->state = ACPT_S_ACCEPT;
        b->shutdown = (int)num;
        b->init = 1;
        break;
    case BIO_C_GET_FD:
        if (b->init) {
            ip = (int *)ptr;
            if (ip != NULL)
                *ip = data->accept_sock;
            ret = data->accept_sock;
        } else
            ret = -1;
        break;
    case BIO_C_GET_ACCEPT:
        if (b->init) {
            if (num == 0 && ptr != NULL) {
                pp = (char **)ptr;
                *pp = data->cache_accepting_name;
            } else if (num == 1 && ptr != NULL) {
                pp = (char **)ptr;
                *pp = data->cache_accepting_serv;
            } else if (num == 2 && ptr != NULL) {
                pp = (char **)ptr;
                *pp = data->cache_peer_name;
            } else if (num == 3 && ptr != NULL) {
                pp = (char **)ptr;
                *pp = data->cache_peer_serv;
            } else if (num == 4) {
                switch (BIO_ADDRINFO_family(data->addr_iter)) {
#ifdef AF_INET6
                case AF_INET6:
                    ret = BIO_FAMILY_IPV6;
                    break;
#endif
                case AF_INET:
                    ret = BIO_FAMILY_IPV4;
                    break;
                case 0:
                    ret = data->accept_family;
                    break;
                default:
                    ret = -1;
                    break;
                }
            } else
                ret = -1;
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
    case BIO_CTRL_FLUSH:
        break;
    case BIO_C_SET_BIND_MODE:
        data->bind_mode = (int)num;
        break;
    case BIO_C_GET_BIND_MODE:
        ret = (long)data->bind_mode;
        break;
    case BIO_CTRL_DUP:
        break;

    default:
        ret = 0;
        break;
    }
    return ret;
}

static int acpt_puts(BIO *bp, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = acpt_write(bp, str, n);
    return ret;
}

BIO *BIO_new_accept(const char *str)
{
    BIO *ret;

    ret = BIO_new(BIO_s_accept());
    if (ret == NULL)
        return NULL;
    if (BIO_set_accept_name(ret, str))
        return ret;
    BIO_free(ret);
    return NULL;
}

#endif
