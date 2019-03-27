/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
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
 *
 * $Id: nb_lib.h,v 1.2 2000/07/17 01:49:27 bp Exp $
 * $FreeBSD$
 */
#ifndef _NETSMB_NB_LIB_H_
#define	_NETSMB_NB_LIB_H_

/*
 * Error codes
 */
#define NBERR_INVALIDFORMAT	0x0001
#define NBERR_SRVFAILURE	0x0002
#define NBERR_NAMENOTFOUND	0x0003
#define NBERR_IMP		0x0004
#define NBERR_REFUSED		0x0005
#define NBERR_ACTIVE		0x0006
#define NBERR_HOSTNOTFOUND	0x0101
#define NBERR_TOOMANYREDIRECTS	0x0102
#define NBERR_INVALIDRESPONSE	0x0103
#define NBERR_NAMETOOLONG	0x0104
#define	NBERR_NOBCASTIFS	0x0105
#define NBERR_MAX		0x0106
#define NBERROR(e)		((e) |  SMB_NB_ERROR)

#define	NBCF_RESOLVED	0x0001

/*
 * nb environment
 */
struct nb_ctx {
	int		nb_flags;
	int		nb_timo;
	char *		nb_scope;	/* NetBIOS scope */
	char *		nb_nsname;	/* name server */
	struct sockaddr_in	nb_ns;	/* ip addr of name server */
	struct sockaddr_in	nb_lastns;
	long		nb_nmbtcpport;	/* default: NMB_TCP_PORT = 137 */
	long		nb_smbtcpport;	/* default: SMB_TCP_PORT = 139 */
};

/*
 * resource record
 */
struct nbns_rr {
	u_char *	rr_name;	/* compressed NETBIOS name */
	u_int16_t	rr_type;
	u_int16_t	rr_class;
	u_int32_t	rr_ttl;
	u_int16_t	rr_rdlength;
	u_char *	rr_data;
};

#define NBRQF_BROADCAST		0x0001
/*
 * nbns request
 */
struct nbns_rq {
	int		nr_opcode;
	int		nr_nmflags;
	int		nr_rcode;
	int		nr_qdcount;
	int		nr_ancount;
	int		nr_nscount;
	int		nr_arcount;
	struct nb_name*	nr_qdname;
	u_int16_t	nr_qdtype;
	u_int16_t	nr_qdclass;
	struct sockaddr_in nr_dest;	/* receiver of query */
	struct sockaddr_in nr_sender;	/* sender of response */
	int		nr_rpnmflags;
	int		nr_rprcode;
	u_int16_t	nr_rpancount;
	u_int16_t	nr_rpnscount;
	u_int16_t	nr_rparcount;
	u_int16_t	nr_trnid;
	struct nb_ctx *	nr_nbd;
	struct mbdata	nr_rq;
	struct mbdata	nr_rp;
	struct nb_ifdesc *nr_if;
	int		nr_flags;
	int		nr_fd;
};

struct nb_ifdesc {
	int		id_flags;
	struct in_addr	id_addr;
	struct in_addr	id_mask;
	char		id_name[16];	/* actually IFNAMSIZ */
	struct nb_ifdesc * id_next;
};

struct sockaddr;

__BEGIN_DECLS

int nb_name_len(struct nb_name *);
int nb_name_encode(struct nb_name *, u_char *);
int nb_encname_len(const char *);

int  nb_snballoc(int namelen, struct sockaddr_nb **);
void nb_snbfree(struct sockaddr*);
int  nb_sockaddr(struct sockaddr *, struct nb_name *, struct sockaddr_nb **);

int  nb_resolvehost_in(const char *, struct sockaddr **, long);
int  nbns_resolvename(const char *, struct nb_ctx *, struct sockaddr **);
int  nb_getlocalname(char *name);
int  nb_enum_if(struct nb_ifdesc **, int);

const char *nb_strerror(int error);

int  nb_ctx_create(struct nb_ctx **);
void nb_ctx_done(struct nb_ctx *);
int  nb_ctx_setns(struct nb_ctx *, const char *);
int  nb_ctx_setscope(struct nb_ctx *, const char *);
int  nb_ctx_resolve(struct nb_ctx *);
int  nb_ctx_readrcsection(struct rcfile *, struct nb_ctx *, const char *, int);

__END_DECLS

#endif /* !_NETSMB_NB_LIB_H_ */
