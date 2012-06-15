/* $Id: isdnl3.h,v 2.6.6.2 2001/09/23 22:24:49 kai Exp $
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define SBIT(state) (1 << state)
#define ALL_STATES  0x03ffffff

#define PROTO_DIS_EURO	0x08

#define L3_DEB_WARN	0x01
#define L3_DEB_PROTERR	0x02
#define L3_DEB_STATE	0x04
#define L3_DEB_CHARGE	0x08
#define L3_DEB_CHECK	0x10
#define L3_DEB_SI	0x20

struct stateentry {
	int state;
	int primitive;
	void (*rout) (struct l3_process *, u8, void *);
};

#define l3_debug(st, fmt, args...) HiSax_putstatus(st->l1.hardware, "l3 ", fmt, ## args)

struct PStack;

void newl3state(struct l3_process *pc, int state);
void L3InitTimer(struct l3_process *pc, struct L3Timer *t);
void L3DelTimer(struct L3Timer *t);
int L3AddTimer(struct L3Timer *t, int millisec, int event);
void StopAllL3Timer(struct l3_process *pc);
struct sk_buff *l3_alloc_skb(int len);
struct l3_process *new_l3_process(struct PStack *st, int cr);
void release_l3_process(struct l3_process *p);
struct l3_process *getl3proc(struct PStack *st, int cr);
void l3_msg(struct PStack *st, int pr, void *arg);
void setstack_dss1(struct PStack *st);
void setstack_ni1(struct PStack *st);
void setstack_1tr6(struct PStack *st);
