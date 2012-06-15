/*
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef mISDN_CORE_H
#define mISDN_CORE_H

extern struct mISDNdevice	*get_mdevice(u_int);
extern int			get_mdevice_count(void);

/* stack status flag */
#define mISDN_STACK_ACTION_MASK		0x0000ffff
#define mISDN_STACK_COMMAND_MASK	0x000f0000
#define mISDN_STACK_STATUS_MASK		0xfff00000
/* action bits 0-15 */
#define mISDN_STACK_WORK	0
#define mISDN_STACK_SETUP	1
#define mISDN_STACK_CLEARING	2
#define mISDN_STACK_RESTART	3
#define mISDN_STACK_WAKEUP	4
#define mISDN_STACK_ABORT	15
/* command bits 16-19 */
#define mISDN_STACK_STOPPED	16
#define mISDN_STACK_INIT	17
#define mISDN_STACK_THREADSTART	18
/* status bits 20-31 */
#define mISDN_STACK_BCHANNEL	20
#define mISDN_STACK_ACTIVE      29
#define mISDN_STACK_RUNNING     30
#define mISDN_STACK_KILLED      31


/* manager options */
#define MGR_OPT_USER		24
#define MGR_OPT_NETWORK		25

extern int	connect_Bstack(struct mISDNdevice *, struct mISDNchannel *,
			       u_int, struct sockaddr_mISDN *);
extern int	connect_layer1(struct mISDNdevice *, struct mISDNchannel *,
			       u_int, struct sockaddr_mISDN *);
extern int	create_l2entity(struct mISDNdevice *, struct mISDNchannel *,
				u_int, struct sockaddr_mISDN *);

extern int	create_stack(struct mISDNdevice *);
extern int	create_teimanager(struct mISDNdevice *);
extern void	delete_teimanager(struct mISDNchannel *);
extern void	delete_channel(struct mISDNchannel *);
extern void	delete_stack(struct mISDNdevice *);
extern void	mISDN_initstack(u_int *);
extern int      misdn_sock_init(u_int *);
extern void     misdn_sock_cleanup(void);
extern void	add_layer2(struct mISDNchannel *, struct mISDNstack *);
extern void	__add_layer2(struct mISDNchannel *, struct mISDNstack *);

extern u_int		get_all_Bprotocols(void);
struct Bprotocol	*get_Bprotocol4mask(u_int);
struct Bprotocol	*get_Bprotocol4id(u_int);

extern int	mISDN_inittimer(u_int *);
extern void	mISDN_timer_cleanup(void);

extern int	l1_init(u_int *);
extern void	l1_cleanup(void);
extern int	Isdnl2_Init(u_int *);
extern void	Isdnl2_cleanup(void);

extern void	mISDN_init_clock(u_int *);

#endif
