/* $Id: l3ni1.c,v 2.8.2.3 2004/01/13 14:31:25 keil Exp $
 *
 * NI1 D-channel protocol
 *
 * Author       Matt Henderson & Guy Ellis
 * Copyright    by Traverse Technologies Pty Ltd, www.travers.com.au
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * 2000.6.6 Initial implementation of routines for US NI1
 * Layer 3 protocol based on the EURO/DSS1 D-channel protocol
 * driver written by Karsten Keil et al.
 * NI-1 Hall of Fame - Thanks to....
 * Ragnar Paulson - for some handy code fragments
 * Will Scales - beta tester extraordinaire
 * Brett Whittacre - beta tester and remote devel system in Vegas
 *
 */

#include "hisax.h"
#include "isdnl3.h"
#include "l3ni1.h"
#include <linux/ctype.h>
#include <linux/slab.h>

extern char *HiSax_getrev(const char *revision);
static const char *ni1_revision = "$Revision: 2.8.2.3 $";

#define EXT_BEARER_CAPS 1

#define	MsgHead(ptr, cref, mty)			\
	*ptr++ = 0x8;				\
	if (cref == -1) {			\
		*ptr++ = 0x0;			\
	} else {				\
		*ptr++ = 0x1;			\
		*ptr++ = cref^0x80;		\
	}					\
	*ptr++ = mty


/**********************************************/
/* get a new invoke id for remote operations. */
/* Only a return value != 0 is valid          */
/**********************************************/
static unsigned char new_invoke_id(struct PStack *p)
{
	unsigned char retval;
	int i;

	i = 32; /* maximum search depth */

	retval = p->prot.ni1.last_invoke_id + 1; /* try new id */
	while ((i) && (p->prot.ni1.invoke_used[retval >> 3] == 0xFF)) {
		p->prot.ni1.last_invoke_id = (retval & 0xF8) + 8;
		i--;
	}
	if (i) {
		while (p->prot.ni1.invoke_used[retval >> 3] & (1 << (retval & 7)))
			retval++;
	} else
		retval = 0;
	p->prot.ni1.last_invoke_id = retval;
	p->prot.ni1.invoke_used[retval >> 3] |= (1 << (retval & 7));
	return (retval);
} /* new_invoke_id */

/*************************/
/* free a used invoke id */
/*************************/
static void free_invoke_id(struct PStack *p, unsigned char id)
{

	if (!id) return; /* 0 = invalid value */

	p->prot.ni1.invoke_used[id >> 3] &= ~(1 << (id & 7));
} /* free_invoke_id */


/**********************************************************/
/* create a new l3 process and fill in ni1 specific data */
/**********************************************************/
static struct l3_process
*ni1_new_l3_process(struct PStack *st, int cr)
{  struct l3_process *proc;

	if (!(proc = new_l3_process(st, cr)))
		return (NULL);

	proc->prot.ni1.invoke_id = 0;
	proc->prot.ni1.remote_operation = 0;
	proc->prot.ni1.uus1_data[0] = '\0';

	return (proc);
} /* ni1_new_l3_process */

/************************************************/
/* free a l3 process and all ni1 specific data */
/************************************************/
static void
ni1_release_l3_process(struct l3_process *p)
{
	free_invoke_id(p->st, p->prot.ni1.invoke_id);
	release_l3_process(p);
} /* ni1_release_l3_process */

/********************************************************/
/* search a process with invoke id id and dummy callref */
/********************************************************/
static struct l3_process *
l3ni1_search_dummy_proc(struct PStack *st, int id)
{ struct l3_process *pc = st->l3.proc; /* start of processes */

	if (!id) return (NULL);

	while (pc)
	{ if ((pc->callref == -1) && (pc->prot.ni1.invoke_id == id))
			return (pc);
		pc = pc->next;
	}
	return (NULL);
} /* l3ni1_search_dummy_proc */

/*******************************************************************/
/* called when a facility message with a dummy callref is received */
/* and a return result is delivered. id specifies the invoke id.   */
/*******************************************************************/
static void
l3ni1_dummy_return_result(struct PStack *st, int id, u_char *p, u_char nlen)
{ isdn_ctrl ic;
	struct IsdnCardState *cs;
	struct l3_process *pc = NULL;

	if ((pc = l3ni1_search_dummy_proc(st, id)))
	{ L3DelTimer(&pc->timer); /* remove timer */

		cs = pc->st->l1.hardware;
		ic.driver = cs->myid;
		ic.command = ISDN_STAT_PROT;
		ic.arg = NI1_STAT_INVOKE_RES;
		ic.parm.ni1_io.hl_id = pc->prot.ni1.invoke_id;
		ic.parm.ni1_io.ll_id = pc->prot.ni1.ll_id;
		ic.parm.ni1_io.proc = pc->prot.ni1.proc;
		ic.parm.ni1_io.timeout = 0;
		ic.parm.ni1_io.datalen = nlen;
		ic.parm.ni1_io.data = p;
		free_invoke_id(pc->st, pc->prot.ni1.invoke_id);
		pc->prot.ni1.invoke_id = 0; /* reset id */

		cs->iif.statcallb(&ic);
		ni1_release_l3_process(pc);
	}
	else
		l3_debug(st, "dummy return result id=0x%x result len=%d", id, nlen);
} /* l3ni1_dummy_return_result */

/*******************************************************************/
/* called when a facility message with a dummy callref is received */
/* and a return error is delivered. id specifies the invoke id.    */
/*******************************************************************/
static void
l3ni1_dummy_error_return(struct PStack *st, int id, ulong error)
{ isdn_ctrl ic;
	struct IsdnCardState *cs;
	struct l3_process *pc = NULL;

	if ((pc = l3ni1_search_dummy_proc(st, id)))
	{ L3DelTimer(&pc->timer); /* remove timer */

		cs = pc->st->l1.hardware;
		ic.driver = cs->myid;
		ic.command = ISDN_STAT_PROT;
		ic.arg = NI1_STAT_INVOKE_ERR;
		ic.parm.ni1_io.hl_id = pc->prot.ni1.invoke_id;
		ic.parm.ni1_io.ll_id = pc->prot.ni1.ll_id;
		ic.parm.ni1_io.proc = pc->prot.ni1.proc;
		ic.parm.ni1_io.timeout = error;
		ic.parm.ni1_io.datalen = 0;
		ic.parm.ni1_io.data = NULL;
		free_invoke_id(pc->st, pc->prot.ni1.invoke_id);
		pc->prot.ni1.invoke_id = 0; /* reset id */

		cs->iif.statcallb(&ic);
		ni1_release_l3_process(pc);
	}
	else
		l3_debug(st, "dummy return error id=0x%x error=0x%lx", id, error);
} /* l3ni1_error_return */

/*******************************************************************/
/* called when a facility message with a dummy callref is received */
/* and a invoke is delivered. id specifies the invoke id.          */
/*******************************************************************/
static void
l3ni1_dummy_invoke(struct PStack *st, int cr, int id,
		   int ident, u_char *p, u_char nlen)
{ isdn_ctrl ic;
	struct IsdnCardState *cs;

	l3_debug(st, "dummy invoke %s id=0x%x ident=0x%x datalen=%d",
		 (cr == -1) ? "local" : "broadcast", id, ident, nlen);
	if (cr >= -1) return; /* ignore local data */

	cs = st->l1.hardware;
	ic.driver = cs->myid;
	ic.command = ISDN_STAT_PROT;
	ic.arg = NI1_STAT_INVOKE_BRD;
	ic.parm.ni1_io.hl_id = id;
	ic.parm.ni1_io.ll_id = 0;
	ic.parm.ni1_io.proc = ident;
	ic.parm.ni1_io.timeout = 0;
	ic.parm.ni1_io.datalen = nlen;
	ic.parm.ni1_io.data = p;

	cs->iif.statcallb(&ic);
} /* l3ni1_dummy_invoke */

static void
l3ni1_parse_facility(struct PStack *st, struct l3_process *pc,
		     int cr, u_char *p)
{
	int qd_len = 0;
	unsigned char nlen = 0, ilen, cp_tag;
	int ident, id;
	ulong err_ret;

	if (pc)
		st = pc->st; /* valid Stack */
	else
		if ((!st) || (cr >= 0)) return; /* neither pc nor st specified */

	p++;
	qd_len = *p++;
	if (qd_len == 0) {
		l3_debug(st, "qd_len == 0");
		return;
	}
	if ((*p & 0x1F) != 0x11) {	/* Service discriminator, supplementary service */
		l3_debug(st, "supplementary service != 0x11");
		return;
	}
	while (qd_len > 0 && !(*p & 0x80)) {	/* extension ? */
		p++;
		qd_len--;
	}
	if (qd_len < 2) {
		l3_debug(st, "qd_len < 2");
		return;
	}
	p++;
	qd_len--;
	if ((*p & 0xE0) != 0xA0) {	/* class and form */
		l3_debug(st, "class and form != 0xA0");
		return;
	}

	cp_tag = *p & 0x1F; /* remember tag value */

	p++;
	qd_len--;
	if (qd_len < 1)
	{ l3_debug(st, "qd_len < 1");
		return;
	}
	if (*p & 0x80)
	{ /* length format indefinite or limited */
		nlen = *p++ & 0x7F; /* number of len bytes or indefinite */
		if ((qd_len-- < ((!nlen) ? 3 : (1 + nlen))) ||
		    (nlen > 1))
		{ l3_debug(st, "length format error or not implemented");
			return;
		}
		if (nlen == 1)
		{ nlen = *p++; /* complete length */
			qd_len--;
		}
		else
		{ qd_len -= 2; /* trailing null bytes */
			if ((*(p + qd_len)) || (*(p + qd_len + 1)))
			{ l3_debug(st, "length format indefinite error");
				return;
			}
			nlen = qd_len;
		}
	}
	else
	{ nlen = *p++;
		qd_len--;
	}
	if (qd_len < nlen)
	{ l3_debug(st, "qd_len < nlen");
		return;
	}
	qd_len -= nlen;

	if (nlen < 2)
	{ l3_debug(st, "nlen < 2");
		return;
	}
	if (*p != 0x02)
	{  /* invoke identifier tag */
		l3_debug(st, "invoke identifier tag !=0x02");
		return;
	}
	p++;
	nlen--;
	if (*p & 0x80)
	{ /* length format */
		l3_debug(st, "invoke id length format 2");
		return;
	}
	ilen = *p++;
	nlen--;
	if (ilen > nlen || ilen == 0)
	{ l3_debug(st, "ilen > nlen || ilen == 0");
		return;
	}
	nlen -= ilen;
	id = 0;
	while (ilen > 0)
	{ id = (id << 8) | (*p++ & 0xFF);	/* invoke identifier */
		ilen--;
	}

	switch (cp_tag) {	/* component tag */
	case 1:	/* invoke */
		if (nlen < 2) {
			l3_debug(st, "nlen < 2 22");
			return;
		}
		if (*p != 0x02) {	/* operation value */
			l3_debug(st, "operation value !=0x02");
			return;
		}
		p++;
		nlen--;
		ilen = *p++;
		nlen--;
		if (ilen > nlen || ilen == 0) {
			l3_debug(st, "ilen > nlen || ilen == 0 22");
			return;
		}
		nlen -= ilen;
		ident = 0;
		while (ilen > 0) {
			ident = (ident << 8) | (*p++ & 0xFF);
			ilen--;
		}

		if (!pc)
		{
			l3ni1_dummy_invoke(st, cr, id, ident, p, nlen);
			return;
		}
		l3_debug(st, "invoke break");
		break;
	case 2:	/* return result */
		/* if no process available handle separately */
		if (!pc)
		{ if (cr == -1)
				l3ni1_dummy_return_result(st, id, p, nlen);
			return;
		}
		if ((pc->prot.ni1.invoke_id) && (pc->prot.ni1.invoke_id == id))
		{ /* Diversion successful */
			free_invoke_id(st, pc->prot.ni1.invoke_id);
			pc->prot.ni1.remote_result = 0; /* success */
			pc->prot.ni1.invoke_id = 0;
			pc->redir_result = pc->prot.ni1.remote_result;
			st->l3.l3l4(st, CC_REDIR | INDICATION, pc); } /* Diversion successful */
		else
			l3_debug(st, "return error unknown identifier");
		break;
	case 3:	/* return error */
		err_ret = 0;
		if (nlen < 2)
		{ l3_debug(st, "return error nlen < 2");
			return;
		}
		if (*p != 0x02)
		{ /* result tag */
			l3_debug(st, "invoke error tag !=0x02");
			return;
		}
		p++;
		nlen--;
		if (*p > 4)
		{ /* length format */
			l3_debug(st, "invoke return errlen > 4 ");
			return;
		}
		ilen = *p++;
		nlen--;
		if (ilen > nlen || ilen == 0)
		{ l3_debug(st, "error return ilen > nlen || ilen == 0");
			return;
		}
		nlen -= ilen;
		while (ilen > 0)
		{ err_ret = (err_ret << 8) | (*p++ & 0xFF);	/* error value */
			ilen--;
		}
		/* if no process available handle separately */
		if (!pc)
		{ if (cr == -1)
				l3ni1_dummy_error_return(st, id, err_ret);
			return;
		}
		if ((pc->prot.ni1.invoke_id) && (pc->prot.ni1.invoke_id == id))
		{ /* Deflection error */
			free_invoke_id(st, pc->prot.ni1.invoke_id);
			pc->prot.ni1.remote_result = err_ret; /* result */
			pc->prot.ni1.invoke_id = 0;
			pc->redir_result = pc->prot.ni1.remote_result;
			st->l3.l3l4(st, CC_REDIR | INDICATION, pc);
		} /* Deflection error */
		else
			l3_debug(st, "return result unknown identifier");
		break;
	default:
		l3_debug(st, "facility default break tag=0x%02x", cp_tag);
		break;
	}
}

static void
l3ni1_message(struct l3_process *pc, u_char mt)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, pc->callref, mt);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3ni1_message_plus_chid(struct l3_process *pc, u_char mt)
/* sends an l3 messages plus channel id -  added GE 05/09/00 */
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	u_char chid;

	chid = (u_char)(pc->para.bchannel & 0x03) | 0x88;
	MsgHead(p, pc->callref, mt);
	*p++ = IE_CHANNEL_ID;
	*p++ = 0x01;
	*p++ = chid;

	if (!(skb = l3_alloc_skb(7)))
		return;
	memcpy(skb_put(skb, 7), tmp, 7);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3ni1_message_cause(struct l3_process *pc, u_char mt, u_char cause)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;

	MsgHead(p, pc->callref, mt);
	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3ni1_status_send(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;

	MsgHead(p, pc->callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = pc->para.cause | 0x80;

	*p++ = IE_CALL_STATE;
	*p++ = 0x1;
	*p++ = pc->state & 0x3f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3ni1_msg_without_setup(struct l3_process *pc, u_char pr, void *arg)
{
	/* This routine is called if here was no SETUP made (checks in ni1up and in
	 * l3ni1_setup) and a RELEASE_COMPLETE have to be sent with an error code
	 * MT_STATUS_ENQUIRE in the NULL state is handled too
	 */
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;

	switch (pc->para.cause) {
	case 81:	/* invalid callreference */
	case 88:	/* incomp destination */
	case 96:	/* mandory IE missing */
	case 100:       /* invalid IE contents */
	case 101:	/* incompatible Callstate */
		MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);
		*p++ = IE_CAUSE;
		*p++ = 0x2;
		*p++ = 0x80;
		*p++ = pc->para.cause | 0x80;
		break;
	default:
		printk(KERN_ERR "HiSax l3ni1_msg_without_setup wrong cause %d\n",
		       pc->para.cause);
		return;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	ni1_release_l3_process(pc);
}

static int ie_ALERTING[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
			    IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, IE_HLC,
			    IE_USER_USER, -1};
static int ie_CALL_PROCEEDING[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
				   IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_HLC, -1};
static int ie_CONNECT[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
			   IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_DATE, IE_SIGNAL,
			   IE_CONNECT_PN, IE_CONNECT_SUB, IE_LLC, IE_HLC, IE_USER_USER, -1};
static int ie_CONNECT_ACKNOWLEDGE[] = {IE_CHANNEL_ID, IE_DISPLAY, IE_SIGNAL, -1};
static int ie_DISCONNECT[] = {IE_CAUSE | IE_MANDATORY, IE_FACILITY,
			      IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
static int ie_INFORMATION[] = {IE_COMPLETE, IE_DISPLAY, IE_KEYPAD, IE_SIGNAL,
			       IE_CALLED_PN, -1};
static int ie_NOTIFY[] = {IE_BEARER, IE_NOTIFY | IE_MANDATORY, IE_DISPLAY, -1};
static int ie_PROGRESS[] = {IE_BEARER, IE_CAUSE, IE_FACILITY, IE_PROGRESS |
			    IE_MANDATORY, IE_DISPLAY, IE_HLC, IE_USER_USER, -1};
static int ie_RELEASE[] = {IE_CAUSE | IE_MANDATORY_1, IE_FACILITY, IE_DISPLAY,
			   IE_SIGNAL, IE_USER_USER, -1};
/* a RELEASE_COMPLETE with errors don't require special actions
   static int ie_RELEASE_COMPLETE[] = {IE_CAUSE | IE_MANDATORY_1, IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
*/
static int ie_RESUME_ACKNOWLEDGE[] = {IE_CHANNEL_ID | IE_MANDATORY, IE_FACILITY,
				      IE_DISPLAY, -1};
static int ie_RESUME_REJECT[] = {IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
static int ie_SETUP[] = {IE_COMPLETE, IE_BEARER  | IE_MANDATORY,
			 IE_CHANNEL_ID | IE_MANDATORY, IE_FACILITY, IE_PROGRESS,
			 IE_NET_FAC, IE_DISPLAY, IE_KEYPAD, IE_SIGNAL, IE_CALLING_PN,
			 IE_CALLING_SUB, IE_CALLED_PN, IE_CALLED_SUB, IE_REDIR_NR,
			 IE_LLC, IE_HLC, IE_USER_USER, -1};
static int ie_SETUP_ACKNOWLEDGE[] = {IE_CHANNEL_ID | IE_MANDATORY, IE_FACILITY,
				     IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, -1};
static int ie_STATUS[] = {IE_CAUSE | IE_MANDATORY, IE_CALL_STATE |
			  IE_MANDATORY, IE_DISPLAY, -1};
static int ie_STATUS_ENQUIRY[] = {IE_DISPLAY, -1};
static int ie_SUSPEND_ACKNOWLEDGE[] = {IE_DISPLAY, IE_FACILITY, -1};
static int ie_SUSPEND_REJECT[] = {IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
/* not used
 * static int ie_CONGESTION_CONTROL[] = {IE_CONGESTION | IE_MANDATORY,
 *		IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
 * static int ie_USER_INFORMATION[] = {IE_MORE_DATA, IE_USER_USER | IE_MANDATORY, -1};
 * static int ie_RESTART[] = {IE_CHANNEL_ID, IE_DISPLAY, IE_RESTART_IND |
 *		IE_MANDATORY, -1};
 */
static int ie_FACILITY[] = {IE_FACILITY | IE_MANDATORY, IE_DISPLAY, -1};
static int comp_required[] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 14, 15, -1};
static int l3_valid_states[] = {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 15, 17, 19, 25, -1};

struct ie_len {
	int ie;
	int len;
};

static
struct ie_len max_ie_len[] = {
	{IE_SEGMENT, 4},
	{IE_BEARER, 12},
	{IE_CAUSE, 32},
	{IE_CALL_ID, 10},
	{IE_CALL_STATE, 3},
	{IE_CHANNEL_ID,	34},
	{IE_FACILITY, 255},
	{IE_PROGRESS, 4},
	{IE_NET_FAC, 255},
	{IE_NOTIFY, 3},
	{IE_DISPLAY, 82},
	{IE_DATE, 8},
	{IE_KEYPAD, 34},
	{IE_SIGNAL, 3},
	{IE_INFORATE, 6},
	{IE_E2E_TDELAY, 11},
	{IE_TDELAY_SEL, 5},
	{IE_PACK_BINPARA, 3},
	{IE_PACK_WINSIZE, 4},
	{IE_PACK_SIZE, 4},
	{IE_CUG, 7},
	{IE_REV_CHARGE, 3},
	{IE_CALLING_PN, 24},
	{IE_CALLING_SUB, 23},
	{IE_CALLED_PN, 24},
	{IE_CALLED_SUB, 23},
	{IE_REDIR_NR, 255},
	{IE_TRANS_SEL, 255},
	{IE_RESTART_IND, 3},
	{IE_LLC, 18},
	{IE_HLC, 5},
	{IE_USER_USER, 131},
	{-1, 0},
};

static int
getmax_ie_len(u_char ie) {
	int i = 0;
	while (max_ie_len[i].ie != -1) {
		if (max_ie_len[i].ie == ie)
			return (max_ie_len[i].len);
		i++;
	}
	return (255);
}

static int
ie_in_set(struct l3_process *pc, u_char ie, int *checklist) {
	int ret = 1;

	while (*checklist != -1) {
		if ((*checklist & 0xff) == ie) {
			if (ie & 0x80)
				return (-ret);
			else
				return (ret);
		}
		ret++;
		checklist++;
	}
	return (0);
}

static int
check_infoelements(struct l3_process *pc, struct sk_buff *skb, int *checklist)
{
	int *cl = checklist;
	u_char mt;
	u_char *p, ie;
	int l, newpos, oldpos;
	int err_seq = 0, err_len = 0, err_compr = 0, err_ureg = 0;
	u_char codeset = 0;
	u_char old_codeset = 0;
	u_char codelock = 1;

	p = skb->data;
	/* skip cr */
	p++;
	l = (*p++) & 0xf;
	p += l;
	mt = *p++;
	oldpos = 0;
	while ((p - skb->data) < skb->len) {
		if ((*p & 0xf0) == 0x90) { /* shift codeset */
			old_codeset = codeset;
			codeset = *p & 7;
			if (*p & 0x08)
				codelock = 0;
			else
				codelock = 1;
			if (pc->debug & L3_DEB_CHECK)
				l3_debug(pc->st, "check IE shift%scodeset %d->%d",
					 codelock ? " locking " : " ", old_codeset, codeset);
			p++;
			continue;
		}
		if (!codeset) { /* only codeset 0 */
			if ((newpos = ie_in_set(pc, *p, cl))) {
				if (newpos > 0) {
					if (newpos < oldpos)
						err_seq++;
					else
						oldpos = newpos;
				}
			} else {
				if (ie_in_set(pc, *p, comp_required))
					err_compr++;
				else
					err_ureg++;
			}
		}
		ie = *p++;
		if (ie & 0x80) {
			l = 1;
		} else {
			l = *p++;
			p += l;
			l += 2;
		}
		if (!codeset && (l > getmax_ie_len(ie)))
			err_len++;
		if (!codelock) {
			if (pc->debug & L3_DEB_CHECK)
				l3_debug(pc->st, "check IE shift back codeset %d->%d",
					 codeset, old_codeset);
			codeset = old_codeset;
			codelock = 1;
		}
	}
	if (err_compr | err_ureg | err_len | err_seq) {
		if (pc->debug & L3_DEB_CHECK)
			l3_debug(pc->st, "check IE MT(%x) %d/%d/%d/%d",
				 mt, err_compr, err_ureg, err_len, err_seq);
		if (err_compr)
			return (ERR_IE_COMPREHENSION);
		if (err_ureg)
			return (ERR_IE_UNRECOGNIZED);
		if (err_len)
			return (ERR_IE_LENGTH);
		if (err_seq)
			return (ERR_IE_SEQUENCE);
	}
	return (0);
}

/* verify if a message type exists and contain no IE error */
static int
l3ni1_check_messagetype_validity(struct l3_process *pc, int mt, void *arg)
{
	switch (mt) {
	case MT_ALERTING:
	case MT_CALL_PROCEEDING:
	case MT_CONNECT:
	case MT_CONNECT_ACKNOWLEDGE:
	case MT_DISCONNECT:
	case MT_INFORMATION:
	case MT_FACILITY:
	case MT_NOTIFY:
	case MT_PROGRESS:
	case MT_RELEASE:
	case MT_RELEASE_COMPLETE:
	case MT_SETUP:
	case MT_SETUP_ACKNOWLEDGE:
	case MT_RESUME_ACKNOWLEDGE:
	case MT_RESUME_REJECT:
	case MT_SUSPEND_ACKNOWLEDGE:
	case MT_SUSPEND_REJECT:
	case MT_USER_INFORMATION:
	case MT_RESTART:
	case MT_RESTART_ACKNOWLEDGE:
	case MT_CONGESTION_CONTROL:
	case MT_STATUS:
	case MT_STATUS_ENQUIRY:
		if (pc->debug & L3_DEB_CHECK)
			l3_debug(pc->st, "l3ni1_check_messagetype_validity mt(%x) OK", mt);
		break;
	case MT_RESUME: /* RESUME only in user->net */
	case MT_SUSPEND: /* SUSPEND only in user->net */
	default:
		if (pc->debug & (L3_DEB_CHECK | L3_DEB_WARN))
			l3_debug(pc->st, "l3ni1_check_messagetype_validity mt(%x) fail", mt);
		pc->para.cause = 97;
		l3ni1_status_send(pc, 0, NULL);
		return (1);
	}
	return (0);
}

static void
l3ni1_std_ie_err(struct l3_process *pc, int ret) {

	if (pc->debug & L3_DEB_CHECK)
		l3_debug(pc->st, "check_infoelements ret %d", ret);
	switch (ret) {
	case 0:
		break;
	case ERR_IE_COMPREHENSION:
		pc->para.cause = 96;
		l3ni1_status_send(pc, 0, NULL);
		break;
	case ERR_IE_UNRECOGNIZED:
		pc->para.cause = 99;
		l3ni1_status_send(pc, 0, NULL);
		break;
	case ERR_IE_LENGTH:
		pc->para.cause = 100;
		l3ni1_status_send(pc, 0, NULL);
		break;
	case ERR_IE_SEQUENCE:
	default:
		break;
	}
}

static int
l3ni1_get_channel_id(struct l3_process *pc, struct sk_buff *skb) {
	u_char *p;

	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		p++;
		if (*p != 1) { /* len for BRI = 1 */
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "wrong chid len %d", *p);
			return (-2);
		}
		p++;
		if (*p & 0x60) { /* only base rate interface */
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "wrong chid %x", *p);
			return (-3);
		}
		return (*p & 0x3);
	} else
		return (-1);
}

static int
l3ni1_get_cause(struct l3_process *pc, struct sk_buff *skb) {
	u_char l, i = 0;
	u_char *p;

	p = skb->data;
	pc->para.cause = 31;
	pc->para.loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		l = *p++;
		if (l > 30)
			return (1);
		if (l) {
			pc->para.loc = *p++;
			l--;
		} else {
			return (2);
		}
		if (l && !(pc->para.loc & 0x80)) {
			l--;
			p++; /* skip recommendation */
		}
		if (l) {
			pc->para.cause = *p++;
			l--;
			if (!(pc->para.cause & 0x80))
				return (3);
		} else
			return (4);
		while (l && (i < 6)) {
			pc->para.diag[i++] = *p++;
			l--;
		}
	} else
		return (-1);
	return (0);
}

static void
l3ni1_msg_with_uus(struct l3_process *pc, u_char cmd)
{
	struct sk_buff *skb;
	u_char tmp[16 + 40];
	u_char *p = tmp;
	int l;

	MsgHead(p, pc->callref, cmd);

	if (pc->prot.ni1.uus1_data[0])
	{ *p++ = IE_USER_USER; /* UUS info element */
		*p++ = strlen(pc->prot.ni1.uus1_data) + 1;
		*p++ = 0x04; /* IA5 chars */
		strcpy(p, pc->prot.ni1.uus1_data);
		p += strlen(pc->prot.ni1.uus1_data);
		pc->prot.ni1.uus1_data[0] = '\0';
	}

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
} /* l3ni1_msg_with_uus */

static void
l3ni1_release_req(struct l3_process *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	newl3state(pc, 19);
	if (!pc->prot.ni1.uus1_data[0])
		l3ni1_message(pc, MT_RELEASE);
	else
		l3ni1_msg_with_uus(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3ni1_release_cmpl(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((ret = l3ni1_get_cause(pc, skb)) > 0) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "RELCMPL get_cause ret(%d)", ret);
	} else if (ret < 0)
		pc->para.cause = NO_CAUSE;
	StopAllL3Timer(pc);
	newl3state(pc, 0);
	pc->st->l3.l3l4(pc->st, CC_RELEASE | CONFIRM, pc);
	ni1_release_l3_process(pc);
}

#if EXT_BEARER_CAPS

static u_char *
EncodeASyncParams(u_char *p, u_char si2)
{				// 7c 06 88  90 21 42 00 bb

	p[0] = 0;
	p[1] = 0x40;		// Intermediate rate: 16 kbit/s jj 2000.02.19
	p[2] = 0x80;
	if (si2 & 32)		// 7 data bits

		p[2] += 16;
	else			// 8 data bits

		p[2] += 24;

	if (si2 & 16)		// 2 stop bits

		p[2] += 96;
	else			// 1 stop bit

		p[2] += 32;

	if (si2 & 8)		// even parity

		p[2] += 2;
	else			// no parity

		p[2] += 3;

	switch (si2 & 0x07) {
	case 0:
		p[0] = 66;	// 1200 bit/s

		break;
	case 1:
		p[0] = 88;	// 1200/75 bit/s

		break;
	case 2:
		p[0] = 87;	// 75/1200 bit/s

		break;
	case 3:
		p[0] = 67;	// 2400 bit/s

		break;
	case 4:
		p[0] = 69;	// 4800 bit/s

		break;
	case 5:
		p[0] = 72;	// 9600 bit/s

		break;
	case 6:
		p[0] = 73;	// 14400 bit/s

		break;
	case 7:
		p[0] = 75;	// 19200 bit/s

		break;
	}
	return p + 3;
}

static u_char
EncodeSyncParams(u_char si2, u_char ai)
{

	switch (si2) {
	case 0:
		return ai + 2;	// 1200 bit/s

	case 1:
		return ai + 24;		// 1200/75 bit/s

	case 2:
		return ai + 23;		// 75/1200 bit/s

	case 3:
		return ai + 3;	// 2400 bit/s

	case 4:
		return ai + 5;	// 4800 bit/s

	case 5:
		return ai + 8;	// 9600 bit/s

	case 6:
		return ai + 9;	// 14400 bit/s

	case 7:
		return ai + 11;		// 19200 bit/s

	case 8:
		return ai + 14;		// 48000 bit/s

	case 9:
		return ai + 15;		// 56000 bit/s

	case 15:
		return ai + 40;		// negotiate bit/s

	default:
		break;
	}
	return ai;
}


static u_char
DecodeASyncParams(u_char si2, u_char *p)
{
	u_char info;

	switch (p[5]) {
	case 66:	// 1200 bit/s

		break;	// si2 don't change

	case 88:	// 1200/75 bit/s

		si2 += 1;
		break;
	case 87:	// 75/1200 bit/s

		si2 += 2;
		break;
	case 67:	// 2400 bit/s

		si2 += 3;
		break;
	case 69:	// 4800 bit/s

		si2 += 4;
		break;
	case 72:	// 9600 bit/s

		si2 += 5;
		break;
	case 73:	// 14400 bit/s

		si2 += 6;
		break;
	case 75:	// 19200 bit/s

		si2 += 7;
		break;
	}

	info = p[7] & 0x7f;
	if ((info & 16) && (!(info & 8)))	// 7 data bits

		si2 += 32;	// else 8 data bits

	if ((info & 96) == 96)	// 2 stop bits

		si2 += 16;	// else 1 stop bit

	if ((info & 2) && (!(info & 1)))	// even parity

		si2 += 8;	// else no parity

	return si2;
}


static u_char
DecodeSyncParams(u_char si2, u_char info)
{
	info &= 0x7f;
	switch (info) {
	case 40:	// bit/s negotiation failed  ai := 165 not 175!

		return si2 + 15;
	case 15:	// 56000 bit/s failed, ai := 0 not 169 !

		return si2 + 9;
	case 14:	// 48000 bit/s

		return si2 + 8;
	case 11:	// 19200 bit/s

		return si2 + 7;
	case 9:	// 14400 bit/s

		return si2 + 6;
	case 8:	// 9600  bit/s

		return si2 + 5;
	case 5:	// 4800  bit/s

		return si2 + 4;
	case 3:	// 2400  bit/s

		return si2 + 3;
	case 23:	// 75/1200 bit/s

		return si2 + 2;
	case 24:	// 1200/75 bit/s

		return si2 + 1;
	default:	// 1200 bit/s

		return si2;
	}
}

static u_char
DecodeSI2(struct sk_buff *skb)
{
	u_char *p;		//, *pend=skb->data + skb->len;

	if ((p = findie(skb->data, skb->len, 0x7c, 0))) {
		switch (p[4] & 0x0f) {
		case 0x01:
			if (p[1] == 0x04)	// sync. Bitratenadaption

				return DecodeSyncParams(160, p[5]);	// V.110/X.30

			else if (p[1] == 0x06)	// async. Bitratenadaption

				return DecodeASyncParams(192, p);	// V.110/X.30

			break;
		case 0x08:	// if (p[5] == 0x02) // sync. Bitratenadaption
			if (p[1] > 3)
				return DecodeSyncParams(176, p[5]);	// V.120
			break;
		}
	}
	return 0;
}

#endif


static void
l3ni1_setup_req(struct l3_process *pc, u_char pr,
		void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;

	u_char *teln;
	u_char *sub;
	u_char *sp;
	int l;

	MsgHead(p, pc->callref, MT_SETUP);

	teln = pc->para.setup.phone;

	*p++ = 0xa1;		/* complete indicator */
	/*
	 * Set Bearer Capability, Map info from 1TR6-convention to NI1
	 */
	switch (pc->para.setup.si1) {
	case 1:	                  /* Telephony                                */
		*p++ = IE_BEARER;
		*p++ = 0x3;	  /* Length                                   */
		*p++ = 0x90;	  /* 3.1khz Audio			      */
		*p++ = 0x90;	  /* Circuit-Mode 64kbps                      */
		*p++ = 0xa2;	  /* u-Law Audio                              */
		break;
	case 5:	                  /* Datatransmission 64k, BTX                */
	case 7:	                  /* Datatransmission 64k                     */
	default:
		*p++ = IE_BEARER;
		*p++ = 0x2;	  /* Length                                   */
		*p++ = 0x88;	  /* Coding Std. CCITT, unrestr. dig. Inform. */
		*p++ = 0x90;	  /* Circuit-Mode 64kbps                      */
		break;
	}

	sub = NULL;
	sp = teln;
	while (*sp) {
		if ('.' == *sp) {
			sub = sp;
			*sp = 0;
		} else
			sp++;
	}

	*p++ = IE_KEYPAD;
	*p++ = strlen(teln);
	while (*teln)
		*p++ = (*teln++) & 0x7F;

	if (sub)
		*sub++ = '.';

#if EXT_BEARER_CAPS
	if ((pc->para.setup.si2 >= 160) && (pc->para.setup.si2 <= 175)) {	// sync. Bitratenadaption, V.110/X.30

		*p++ = IE_LLC;
		*p++ = 0x04;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x21;
		*p++ = EncodeSyncParams(pc->para.setup.si2 - 160, 0x80);
	} else if ((pc->para.setup.si2 >= 176) && (pc->para.setup.si2 <= 191)) {	// sync. Bitratenadaption, V.120

		*p++ = IE_LLC;
		*p++ = 0x05;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x28;
		*p++ = EncodeSyncParams(pc->para.setup.si2 - 176, 0);
		*p++ = 0x82;
	} else if (pc->para.setup.si2 >= 192) {		// async. Bitratenadaption, V.110/X.30

		*p++ = IE_LLC;
		*p++ = 0x06;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x21;
		p = EncodeASyncParams(p, pc->para.setup.si2 - 192);
	} else {
		switch (pc->para.setup.si1) {
		case 1:	                /* Telephony                                */
			*p++ = IE_LLC;
			*p++ = 0x3;	/* Length                                   */
			*p++ = 0x90;	/* Coding Std. CCITT, 3.1 kHz audio         */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                      */
			*p++ = 0xa2;	/* u-Law Audio                              */
			break;
		case 5:	                /* Datatransmission 64k, BTX                */
		case 7:	                /* Datatransmission 64k                     */
		default:
			*p++ = IE_LLC;
			*p++ = 0x2;	/* Length                                   */
			*p++ = 0x88;	/* Coding Std. CCITT, unrestr. dig. Inform. */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                      */
			break;
		}
	}
#endif
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
	{
		return;
	}
	memcpy(skb_put(skb, l), tmp, l);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T303, CC_T303);
	newl3state(pc, 1);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3ni1_call_proc(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3ni1_get_channel_id(pc, skb)) >= 0) {
		if ((0 == id) || ((3 == id) && (0x10 == pc->para.moderate))) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup answer with wrong chid %x", id);
			pc->para.cause = 100;
			l3ni1_status_send(pc, pr, NULL);
			return;
		}
		pc->para.bchannel = id;
	} else if (1 == pc->state) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup answer wrong chid (ret %d)", id);
		if (id == -1)
			pc->para.cause = 96;
		else
			pc->para.cause = 100;
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_CALL_PROCEEDING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	newl3state(pc, 3);
	L3AddTimer(&pc->timer, T310, CC_T310);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3ni1_std_ie_err(pc, ret);
	pc->st->l3.l3l4(pc->st, CC_PROCEEDING | INDICATION, pc);
}

static void
l3ni1_setup_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3ni1_get_channel_id(pc, skb)) >= 0) {
		if ((0 == id) || ((3 == id) && (0x10 == pc->para.moderate))) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup answer with wrong chid %x", id);
			pc->para.cause = 100;
			l3ni1_status_send(pc, pr, NULL);
			return;
		}
		pc->para.bchannel = id;
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup answer wrong chid (ret %d)", id);
		if (id == -1)
			pc->para.cause = 96;
		else
			pc->para.cause = 100;
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_SETUP_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	newl3state(pc, 2);
	L3AddTimer(&pc->timer, T304, CC_T304);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3ni1_std_ie_err(pc, ret);
	pc->st->l3.l3l4(pc->st, CC_MORE_INFO | INDICATION, pc);
}

static void
l3ni1_disconnect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;
	int ret;
	u_char cause = 0;

	StopAllL3Timer(pc);
	if ((ret = l3ni1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "DISC get_cause ret(%d)", ret);
		if (ret < 0)
			cause = 96;
		else if (ret > 0)
			cause = 100;
	}
	if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
		l3ni1_parse_facility(pc->st, pc, pc->callref, p);
	ret = check_infoelements(pc, skb, ie_DISCONNECT);
	if (ERR_IE_COMPREHENSION == ret)
		cause = 96;
	else if ((!cause) && (ERR_IE_UNRECOGNIZED == ret))
		cause = 99;
	ret = pc->state;
	newl3state(pc, 12);
	if (cause)
		newl3state(pc, 19);
	if (11 != ret)
		pc->st->l3.l3l4(pc->st, CC_DISCONNECT | INDICATION, pc);
	else if (!cause)
		l3ni1_release_req(pc, pr, NULL);
	if (cause) {
		l3ni1_message_cause(pc, MT_RELEASE, cause);
		L3AddTimer(&pc->timer, T308, CC_T308_1);
	}
}

static void
l3ni1_connect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_CONNECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);	/* T310 */
	newl3state(pc, 10);
	pc->para.chargeinfo = 0;
	/* here should inserted COLP handling KKe */
	if (ret)
		l3ni1_std_ie_err(pc, ret);
	pc->st->l3.l3l4(pc->st, CC_SETUP | CONFIRM, pc);
}

static void
l3ni1_alerting(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_ALERTING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	if (ret)
		l3ni1_std_ie_err(pc, ret);
	pc->st->l3.l3l4(pc->st, CC_ALERTING | INDICATION, pc);
}

static void
l3ni1_setup(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;
	int id;
	int err = 0;

	/*
	 * Bearer Capabilities
	 */
	p = skb->data;
	/* only the first occurrence 'll be detected ! */
	if ((p = findie(p, skb->len, 0x04, 0))) {
		if ((p[1] < 2) || (p[1] > 11))
			err = 1;
		else {
			pc->para.setup.si2 = 0;
			switch (p[2] & 0x7f) {
			case 0x00: /* Speech */
			case 0x10: /* 3.1 Khz audio */
				pc->para.setup.si1 = 1;
				break;
			case 0x08: /* Unrestricted digital information */
				pc->para.setup.si1 = 7;
/* JIM, 05.11.97 I wanna set service indicator 2 */
#if EXT_BEARER_CAPS
				pc->para.setup.si2 = DecodeSI2(skb);
#endif
				break;
			case 0x09: /* Restricted digital information */
				pc->para.setup.si1 = 2;
				break;
			case 0x11:
				/* Unrestr. digital information  with
				 * tones/announcements ( or 7 kHz audio
				 */
				pc->para.setup.si1 = 3;
				break;
			case 0x18: /* Video */
				pc->para.setup.si1 = 4;
				break;
			default:
				err = 2;
				break;
			}
			switch (p[3] & 0x7f) {
			case 0x40: /* packed mode */
				pc->para.setup.si1 = 8;
				break;
			case 0x10: /* 64 kbit */
			case 0x11: /* 2*64 kbit */
			case 0x13: /* 384 kbit */
			case 0x15: /* 1536 kbit */
			case 0x17: /* 1920 kbit */
				pc->para.moderate = p[3] & 0x7f;
				break;
			default:
				err = 3;
				break;
			}
		}
		if (pc->debug & L3_DEB_SI)
			l3_debug(pc->st, "SI=%d, AI=%d",
				 pc->para.setup.si1, pc->para.setup.si2);
		if (err) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup with wrong bearer(l=%d:%x,%x)",
					 p[1], p[2], p[3]);
			pc->para.cause = 100;
			l3ni1_msg_without_setup(pc, pr, NULL);
			return;
		}
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup without bearer capabilities");
		/* ETS 300-104 1.3.3 */
		pc->para.cause = 96;
		l3ni1_msg_without_setup(pc, pr, NULL);
		return;
	}
	/*
	 * Channel Identification
	 */
	if ((id = l3ni1_get_channel_id(pc, skb)) >= 0) {
		if ((pc->para.bchannel = id)) {
			if ((3 == id) && (0x10 == pc->para.moderate)) {
				if (pc->debug & L3_DEB_WARN)
					l3_debug(pc->st, "setup with wrong chid %x",
						 id);
				pc->para.cause = 100;
				l3ni1_msg_without_setup(pc, pr, NULL);
				return;
			}
			bcfound++;
		} else
		{ if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup without bchannel, call waiting");
			bcfound++;
		}
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup with wrong chid ret %d", id);
		if (id == -1)
			pc->para.cause = 96;
		else
			pc->para.cause = 100;
		l3ni1_msg_without_setup(pc, pr, NULL);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_SETUP);
	if (ERR_IE_COMPREHENSION == err) {
		pc->para.cause = 96;
		l3ni1_msg_without_setup(pc, pr, NULL);
		return;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x70, 0)))
		iecpy(pc->para.setup.eazmsn, p, 1);
	else
		pc->para.setup.eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, 0x71, 0))) {
		/* Called party subaddress */
		if ((p[1] >= 2) && (p[2] == 0x80) && (p[3] == 0x50)) {
			tmp[0] = '.';
			iecpy(&tmp[1], p, 2);
			strcat(pc->para.setup.eazmsn, tmp);
		} else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "wrong called subaddress");
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x6c, 0))) {
		pc->para.setup.plan = p[2];
		if (p[2] & 0x80) {
			iecpy(pc->para.setup.phone, p, 1);
			pc->para.setup.screen = 0;
		} else {
			iecpy(pc->para.setup.phone, p, 2);
			pc->para.setup.screen = p[3];
		}
	} else {
		pc->para.setup.phone[0] = 0;
		pc->para.setup.plan = 0;
		pc->para.setup.screen = 0;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x6d, 0))) {
		/* Calling party subaddress */
		if ((p[1] >= 2) && (p[2] == 0x80) && (p[3] == 0x50)) {
			tmp[0] = '.';
			iecpy(&tmp[1], p, 2);
			strcat(pc->para.setup.phone, tmp);
		} else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "wrong calling subaddress");
	}
	newl3state(pc, 6);
	if (err) /* STATUS for none mandatory IE errors after actions are taken */
		l3ni1_std_ie_err(pc, err);
	pc->st->l3.l3l4(pc->st, CC_SETUP | INDICATION, pc);
}

static void
l3ni1_reset(struct l3_process *pc, u_char pr, void *arg)
{
	ni1_release_l3_process(pc);
}

static void
l3ni1_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16 + 40];
	u_char *p = tmp;
	int l;
	u_char cause = 16;

	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;

	StopAllL3Timer(pc);

	MsgHead(p, pc->callref, MT_DISCONNECT);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	if (pc->prot.ni1.uus1_data[0])
	{ *p++ = IE_USER_USER; /* UUS info element */
		*p++ = strlen(pc->prot.ni1.uus1_data) + 1;
		*p++ = 0x04; /* IA5 chars */
		strcpy(p, pc->prot.ni1.uus1_data);
		p += strlen(pc->prot.ni1.uus1_data);
		pc->prot.ni1.uus1_data[0] = '\0';
	}

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 11);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3ni1_setup_rsp(struct l3_process *pc, u_char pr,
		void *arg)
{
	if (!pc->para.bchannel)
	{ if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "D-chan connect for waiting call");
		l3ni1_disconnect_req(pc, pr, arg);
		return;
	}
	newl3state(pc, 8);
	if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "D-chan connect for waiting call");
	l3ni1_message_plus_chid(pc, MT_CONNECT); /* GE 05/09/00 */
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3ni1_connect_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_CONNECT_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	newl3state(pc, 10);
	L3DelTimer(&pc->timer);
	if (ret)
		l3ni1_std_ie_err(pc, ret);
	pc->st->l3.l3l4(pc->st, CC_SETUP_COMPL | INDICATION, pc);
}

static void
l3ni1_reject_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 21;

	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;

	MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
	newl3state(pc, 0);
	ni1_release_l3_process(pc);
}

static void
l3ni1_release(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;
	int ret, cause = 0;

	StopAllL3Timer(pc);
	if ((ret = l3ni1_get_cause(pc, skb)) > 0) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "REL get_cause ret(%d)", ret);
	} else if (ret < 0)
		pc->para.cause = NO_CAUSE;
	if ((p = findie(skb->data, skb->len, IE_FACILITY, 0))) {
		l3ni1_parse_facility(pc->st, pc, pc->callref, p);
	}
	if ((ret < 0) && (pc->state != 11))
		cause = 96;
	else if (ret > 0)
		cause = 100;
	ret = check_infoelements(pc, skb, ie_RELEASE);
	if (ERR_IE_COMPREHENSION == ret)
		cause = 96;
	else if ((ERR_IE_UNRECOGNIZED == ret) && (!cause))
		cause = 99;
	if (cause)
		l3ni1_message_cause(pc, MT_RELEASE_COMPLETE, cause);
	else
		l3ni1_message(pc, MT_RELEASE_COMPLETE);
	pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
	newl3state(pc, 0);
	ni1_release_l3_process(pc);
}

static void
l3ni1_alert_req(struct l3_process *pc, u_char pr,
		void *arg)
{
	newl3state(pc, 7);
	if (!pc->prot.ni1.uus1_data[0])
		l3ni1_message(pc, MT_ALERTING);
	else
		l3ni1_msg_with_uus(pc, MT_ALERTING);
}

static void
l3ni1_proceed_req(struct l3_process *pc, u_char pr,
		  void *arg)
{
	newl3state(pc, 9);
	l3ni1_message(pc, MT_CALL_PROCEEDING);
	pc->st->l3.l3l4(pc->st, CC_PROCEED_SEND | INDICATION, pc);
}

static void
l3ni1_setup_ack_req(struct l3_process *pc, u_char pr,
		    void *arg)
{
	newl3state(pc, 25);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T302, CC_T302);
	l3ni1_message(pc, MT_SETUP_ACKNOWLEDGE);
}

/********************************************/
/* deliver a incoming display message to HL */
/********************************************/
static void
l3ni1_deliver_display(struct l3_process *pc, int pr, u_char *infp)
{       u_char len;
	isdn_ctrl ic;
	struct IsdnCardState *cs;
	char *p;

	if (*infp++ != IE_DISPLAY) return;
	if ((len = *infp++) > 80) return; /* total length <= 82 */
	if (!pc->chan) return;

	p = ic.parm.display;
	while (len--)
		*p++ = *infp++;
	*p = '\0';
	ic.command = ISDN_STAT_DISPLAY;
	cs = pc->st->l1.hardware;
	ic.driver = cs->myid;
	ic.arg = pc->chan->chan;
	cs->iif.statcallb(&ic);
} /* l3ni1_deliver_display */


static void
l3ni1_progress(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int err = 0;
	u_char *p;

	if ((p = findie(skb->data, skb->len, IE_PROGRESS, 0))) {
		if (p[1] != 2) {
			err = 1;
			pc->para.cause = 100;
		} else if (!(p[2] & 0x70)) {
			switch (p[2]) {
			case 0x80:
			case 0x81:
			case 0x82:
			case 0x84:
			case 0x85:
			case 0x87:
			case 0x8a:
				switch (p[3]) {
				case 0x81:
				case 0x82:
				case 0x83:
				case 0x84:
				case 0x88:
					break;
				default:
					err = 2;
					pc->para.cause = 100;
					break;
				}
				break;
			default:
				err = 3;
				pc->para.cause = 100;
				break;
			}
		}
	} else {
		pc->para.cause = 96;
		err = 4;
	}
	if (err) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "progress error %d", err);
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_PROGRESS);
	if (err)
		l3ni1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		pc->st->l3.l3l4(pc->st, CC_PROGRESS | INDICATION, pc);
}

static void
l3ni1_notify(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int err = 0;
	u_char *p;

	if ((p = findie(skb->data, skb->len, IE_NOTIFY, 0))) {
		if (p[1] != 1) {
			err = 1;
			pc->para.cause = 100;
		} else {
			switch (p[2]) {
			case 0x80:
			case 0x81:
			case 0x82:
				break;
			default:
				pc->para.cause = 100;
				err = 2;
				break;
			}
		}
	} else {
		pc->para.cause = 96;
		err = 3;
	}
	if (err) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "notify error %d", err);
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_NOTIFY);
	if (err)
		l3ni1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		pc->st->l3.l3l4(pc->st, CC_NOTIFY | INDICATION, pc);
}

static void
l3ni1_status_enq(struct l3_process *pc, u_char pr, void *arg)
{
	int ret;
	struct sk_buff *skb = arg;

	ret = check_infoelements(pc, skb, ie_STATUS_ENQUIRY);
	l3ni1_std_ie_err(pc, ret);
	pc->para.cause = 30; /* response to STATUS_ENQUIRY */
	l3ni1_status_send(pc, pr, NULL);
}

static void
l3ni1_information(struct l3_process *pc, u_char pr, void *arg)
{
	int ret;
	struct sk_buff *skb = arg;
	u_char *p;
	char tmp[32];

	ret = check_infoelements(pc, skb, ie_INFORMATION);
	if (ret)
		l3ni1_std_ie_err(pc, ret);
	if (pc->state == 25) { /* overlap receiving */
		L3DelTimer(&pc->timer);
		p = skb->data;
		if ((p = findie(p, skb->len, 0x70, 0))) {
			iecpy(tmp, p, 1);
			strcat(pc->para.setup.eazmsn, tmp);
			pc->st->l3.l3l4(pc->st, CC_MORE_INFO | INDICATION, pc);
		}
		L3AddTimer(&pc->timer, T302, CC_T302);
	}
}

/******************************/
/* handle deflection requests */
/******************************/
static void l3ni1_redir_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char *subp;
	u_char len_phone = 0;
	u_char len_sub = 0;
	int l;


	strcpy(pc->prot.ni1.uus1_data, pc->chan->setup.eazmsn); /* copy uus element if available */
	if (!pc->chan->setup.phone[0])
	{ pc->para.cause = -1;
		l3ni1_disconnect_req(pc, pr, arg); /* disconnect immediately */
		return;
	} /* only uus */

	if (pc->prot.ni1.invoke_id)
		free_invoke_id(pc->st, pc->prot.ni1.invoke_id);

	if (!(pc->prot.ni1.invoke_id = new_invoke_id(pc->st)))
		return;

	MsgHead(p, pc->callref, MT_FACILITY);

	for (subp = pc->chan->setup.phone; (*subp) && (*subp != '.'); subp++) len_phone++; /* len of phone number */
	if (*subp++ == '.') len_sub = strlen(subp) + 2; /* length including info subaddress element */

	*p++ = 0x1c;   /* Facility info element */
	*p++ = len_phone + len_sub + 2 + 2 + 8 + 3 + 3; /* length of element */
	*p++ = 0x91;  /* remote operations protocol */
	*p++ = 0xa1;  /* invoke component */

	*p++ = len_phone + len_sub + 2 + 2 + 8 + 3; /* length of data */
	*p++ = 0x02;  /* invoke id tag, integer */
	*p++ = 0x01;  /* length */
	*p++ = pc->prot.ni1.invoke_id;  /* invoke id */
	*p++ = 0x02;  /* operation value tag, integer */
	*p++ = 0x01;  /* length */
	*p++ = 0x0D;  /* Call Deflect */

	*p++ = 0x30;  /* sequence phone number */
	*p++ = len_phone + 2 + 2 + 3 + len_sub; /* length */

	*p++ = 0x30;  /* Deflected to UserNumber */
	*p++ = len_phone + 2 + len_sub; /* length */
	*p++ = 0x80; /* NumberDigits */
	*p++ = len_phone; /* length */
	for (l = 0; l < len_phone; l++)
		*p++ = pc->chan->setup.phone[l];

	if (len_sub)
	{ *p++ = 0x04; /* called party subaddress */
		*p++ = len_sub - 2;
		while (*subp) *p++ = *subp++;
	}

	*p++ = 0x01; /* screening identifier */
	*p++ = 0x01;
	*p++ = pc->chan->setup.screen;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l))) return;
	memcpy(skb_put(skb, l), tmp, l);

	l3_msg(pc->st, DL_DATA | REQUEST, skb);
} /* l3ni1_redir_req */

/********************************************/
/* handle deflection request in early state */
/********************************************/
static void l3ni1_redir_req_early(struct l3_process *pc, u_char pr, void *arg)
{
	l3ni1_proceed_req(pc, pr, arg);
	l3ni1_redir_req(pc, pr, arg);
} /* l3ni1_redir_req_early */

/***********************************************/
/* handle special commands for this protocol.  */
/* Examples are call independent services like */
/* remote operations with dummy  callref.      */
/***********************************************/
static int l3ni1_cmd_global(struct PStack *st, isdn_ctrl *ic)
{ u_char id;
	u_char temp[265];
	u_char *p = temp;
	int i, l, proc_len;
	struct sk_buff *skb;
	struct l3_process *pc = NULL;

	switch (ic->arg)
	{ case NI1_CMD_INVOKE:
			if (ic->parm.ni1_io.datalen < 0) return (-2); /* invalid parameter */

			for (proc_len = 1, i = ic->parm.ni1_io.proc >> 8; i; i++)
				i = i >> 8; /* add one byte */
			l = ic->parm.ni1_io.datalen + proc_len + 8; /* length excluding ie header */
			if (l > 255)
				return (-2); /* too long */

			if (!(id = new_invoke_id(st)))
				return (0); /* first get a invoke id -> return if no available */

			i = -1;
			MsgHead(p, i, MT_FACILITY); /* build message head */
			*p++ = 0x1C; /* Facility IE */
			*p++ = l; /* length of ie */
			*p++ = 0x91; /* remote operations */
			*p++ = 0xA1; /* invoke */
			*p++ = l - 3; /* length of invoke */
			*p++ = 0x02; /* invoke id tag */
			*p++ = 0x01; /* length is 1 */
			*p++ = id; /* invoke id */
			*p++ = 0x02; /* operation */
			*p++ = proc_len; /* length of operation */

			for (i = proc_len; i; i--)
				*p++ = (ic->parm.ni1_io.proc >> (i - 1)) & 0xFF;
			memcpy(p, ic->parm.ni1_io.data, ic->parm.ni1_io.datalen); /* copy data */
			l = (p - temp) + ic->parm.ni1_io.datalen; /* total length */

			if (ic->parm.ni1_io.timeout > 0) {
				pc = ni1_new_l3_process(st, -1);
				if (!pc) {
					free_invoke_id(st, id);
					return (-2);
				}
				/* remember id */
				pc->prot.ni1.ll_id = ic->parm.ni1_io.ll_id;
				/* and procedure */
				pc->prot.ni1.proc = ic->parm.ni1_io.proc;
			}

			if (!(skb = l3_alloc_skb(l)))
			{ free_invoke_id(st, id);
				if (pc) ni1_release_l3_process(pc);
				return (-2);
			}
			memcpy(skb_put(skb, l), temp, l);

			if (pc)
			{ pc->prot.ni1.invoke_id = id; /* remember id */
				L3AddTimer(&pc->timer, ic->parm.ni1_io.timeout, CC_TNI1_IO | REQUEST);
			}

			l3_msg(st, DL_DATA | REQUEST, skb);
			ic->parm.ni1_io.hl_id = id; /* return id */
			return (0);

	case NI1_CMD_INVOKE_ABORT:
		if ((pc = l3ni1_search_dummy_proc(st, ic->parm.ni1_io.hl_id)))
		{ L3DelTimer(&pc->timer); /* remove timer */
			ni1_release_l3_process(pc);
			return (0);
		}
		else
		{ l3_debug(st, "l3ni1_cmd_global abort unknown id");
			return (-2);
		}
		break;

	default:
		l3_debug(st, "l3ni1_cmd_global unknown cmd 0x%lx", ic->arg);
		return (-1);
	} /* switch ic-> arg */
	return (-1);
} /* l3ni1_cmd_global */

static void
l3ni1_io_timer(struct l3_process *pc)
{ isdn_ctrl ic;
	struct IsdnCardState *cs = pc->st->l1.hardware;

	L3DelTimer(&pc->timer); /* remove timer */

	ic.driver = cs->myid;
	ic.command = ISDN_STAT_PROT;
	ic.arg = NI1_STAT_INVOKE_ERR;
	ic.parm.ni1_io.hl_id = pc->prot.ni1.invoke_id;
	ic.parm.ni1_io.ll_id = pc->prot.ni1.ll_id;
	ic.parm.ni1_io.proc = pc->prot.ni1.proc;
	ic.parm.ni1_io.timeout = -1;
	ic.parm.ni1_io.datalen = 0;
	ic.parm.ni1_io.data = NULL;
	free_invoke_id(pc->st, pc->prot.ni1.invoke_id);
	pc->prot.ni1.invoke_id = 0; /* reset id */

	cs->iif.statcallb(&ic);

	ni1_release_l3_process(pc);
} /* l3ni1_io_timer */

static void
l3ni1_release_ind(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int callState = 0;
	p = skb->data;

	if ((p = findie(p, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1 == *p++)
			callState = *p;
	}
	if (callState == 0) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... and 16.1
		 * set down layer 3 without sending any message
		 */
		pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
		newl3state(pc, 0);
		ni1_release_l3_process(pc);
	} else {
		pc->st->l3.l3l4(pc->st, CC_IGNORE | INDICATION, pc);
	}
}

static void
l3ni1_dummy(struct l3_process *pc, u_char pr, void *arg)
{
}

static void
l3ni1_t302(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.loc = 0;
	pc->para.cause = 28; /* invalid number */
	l3ni1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);
}

static void
l3ni1_t303(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		L3DelTimer(&pc->timer);
		l3ni1_setup_req(pc, pr, arg);
	} else {
		L3DelTimer(&pc->timer);
		l3ni1_message_cause(pc, MT_RELEASE_COMPLETE, 102);
		pc->st->l3.l3l4(pc->st, CC_NOSETUP_RSP, pc);
		ni1_release_l3_process(pc);
	}
}

static void
l3ni1_t304(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.loc = 0;
	pc->para.cause = 102;
	l3ni1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);

}

static void
l3ni1_t305(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;
	u_char cause = 16;

	L3DelTimer(&pc->timer);
	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;

	MsgHead(p, pc->callref, MT_RELEASE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 19);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3ni1_t310(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.loc = 0;
	pc->para.cause = 102;
	l3ni1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);
}

static void
l3ni1_t313(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.loc = 0;
	pc->para.cause = 102;
	l3ni1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_CONNECT_ERR, pc);
}

static void
l3ni1_t308_1(struct l3_process *pc, u_char pr, void *arg)
{
	newl3state(pc, 19);
	L3DelTimer(&pc->timer);
	l3ni1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_2);
}

static void
l3ni1_t308_2(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_RELEASE_ERR, pc);
	ni1_release_l3_process(pc);
}

static void
l3ni1_t318(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 102;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
	pc->st->l3.l3l4(pc->st, CC_RESUME_ERR, pc);
	newl3state(pc, 19);
	l3ni1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3ni1_t319(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 102;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
	pc->st->l3.l3l4(pc->st, CC_SUSPEND_ERR, pc);
	newl3state(pc, 10);
}

static void
l3ni1_restart(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
	ni1_release_l3_process(pc);
}

static void
l3ni1_status(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int ret;
	u_char cause = 0, callState = 0;

	if ((ret = l3ni1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "STATUS get_cause ret(%d)", ret);
		if (ret < 0)
			cause = 96;
		else if (ret > 0)
			cause = 100;
	}
	if ((p = findie(skb->data, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1 == *p++) {
			callState = *p;
			if (!ie_in_set(pc, *p, l3_valid_states))
				cause = 100;
		} else
			cause = 100;
	} else
		cause = 96;
	if (!cause) { /*  no error before */
		ret = check_infoelements(pc, skb, ie_STATUS);
		if (ERR_IE_COMPREHENSION == ret)
			cause = 96;
		else if (ERR_IE_UNRECOGNIZED == ret)
			cause = 99;
	}
	if (cause) {
		u_char tmp;

		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "STATUS error(%d/%d)", ret, cause);
		tmp = pc->para.cause;
		pc->para.cause = cause;
		l3ni1_status_send(pc, 0, NULL);
		if (cause == 99)
			pc->para.cause = tmp;
		else
			return;
	}
	cause = pc->para.cause;
	if (((cause & 0x7f) == 111) && (callState == 0)) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1...
		 * if received MT_STATUS with cause == 111 and call
		 * state == 0, then we must set down layer 3
		 */
		pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
		newl3state(pc, 0);
		ni1_release_l3_process(pc);
	}
}

static void
l3ni1_facility(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_FACILITY);
	l3ni1_std_ie_err(pc, ret);
	{
		u_char *p;
		if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
			l3ni1_parse_facility(pc->st, pc, pc->callref, p);
	}
}

static void
l3ni1_suspend_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[32];
	u_char *p = tmp;
	u_char i, l;
	u_char *msg = pc->chan->setup.phone;

	MsgHead(p, pc->callref, MT_SUSPEND);
	l = *msg++;
	if (l && (l <= 10)) {	/* Max length 10 octets */
		*p++ = IE_CALL_ID;
		*p++ = l;
		for (i = 0; i < l; i++)
			*p++ = *msg++;
	} else if (l) {
		l3_debug(pc->st, "SUS wrong CALL_ID len %d", l);
		return;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	newl3state(pc, 15);
	L3AddTimer(&pc->timer, T319, CC_T319);
}

static void
l3ni1_suspend_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	L3DelTimer(&pc->timer);
	newl3state(pc, 0);
	pc->para.cause = NO_CAUSE;
	pc->st->l3.l3l4(pc->st, CC_SUSPEND | CONFIRM, pc);
	/* We don't handle suspend_ack for IE errors now */
	if ((ret = check_infoelements(pc, skb, ie_SUSPEND_ACKNOWLEDGE)))
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "SUSPACK check ie(%d)", ret);
	ni1_release_l3_process(pc);
}

static void
l3ni1_suspend_rej(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((ret = l3ni1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "SUSP_REJ get_cause ret(%d)", ret);
		if (ret < 0)
			pc->para.cause = 96;
		else
			pc->para.cause = 100;
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	ret = check_infoelements(pc, skb, ie_SUSPEND_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_SUSPEND_ERR, pc);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3ni1_std_ie_err(pc, ret);
}

static void
l3ni1_resume_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[32];
	u_char *p = tmp;
	u_char i, l;
	u_char *msg = pc->para.setup.phone;

	MsgHead(p, pc->callref, MT_RESUME);

	l = *msg++;
	if (l && (l <= 10)) {	/* Max length 10 octets */
		*p++ = IE_CALL_ID;
		*p++ = l;
		for (i = 0; i < l; i++)
			*p++ = *msg++;
	} else if (l) {
		l3_debug(pc->st, "RES wrong CALL_ID len %d", l);
		return;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	newl3state(pc, 17);
	L3AddTimer(&pc->timer, T318, CC_T318);
}

static void
l3ni1_resume_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3ni1_get_channel_id(pc, skb)) > 0) {
		if ((0 == id) || ((3 == id) && (0x10 == pc->para.moderate))) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "resume ack with wrong chid %x", id);
			pc->para.cause = 100;
			l3ni1_status_send(pc, pr, NULL);
			return;
		}
		pc->para.bchannel = id;
	} else if (1 == pc->state) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "resume ack without chid (ret %d)", id);
		pc->para.cause = 96;
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_RESUME | CONFIRM, pc);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3ni1_std_ie_err(pc, ret);
}

static void
l3ni1_resume_rej(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((ret = l3ni1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "RES_REJ get_cause ret(%d)", ret);
		if (ret < 0)
			pc->para.cause = 96;
		else
			pc->para.cause = 100;
		l3ni1_status_send(pc, pr, NULL);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3ni1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_RESUME_ERR, pc);
	newl3state(pc, 0);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3ni1_std_ie_err(pc, ret);
	ni1_release_l3_process(pc);
}

static void
l3ni1_global_restart(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[32];
	u_char *p;
	u_char ri, ch = 0, chan = 0;
	int l;
	struct sk_buff *skb = arg;
	struct l3_process *up;

	newl3state(pc, 2);
	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_RESTART_IND, 0))) {
		ri = p[2];
		l3_debug(pc->st, "Restart %x", ri);
	} else {
		l3_debug(pc->st, "Restart without restart IE");
		ri = 0x86;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		chan = p[2] & 3;
		ch = p[2];
		if (pc->st->l3.debug)
			l3_debug(pc->st, "Restart for channel %d", chan);
	}
	newl3state(pc, 2);
	up = pc->st->l3.proc;
	while (up) {
		if ((ri & 7) == 7)
			up->st->lli.l4l3(up->st, CC_RESTART | REQUEST, up);
		else if (up->para.bchannel == chan)
			up->st->lli.l4l3(up->st, CC_RESTART | REQUEST, up);

		up = up->next;
	}
	p = tmp;
	MsgHead(p, pc->callref, MT_RESTART_ACKNOWLEDGE);
	if (chan) {
		*p++ = IE_CHANNEL_ID;
		*p++ = 1;
		*p++ = ch | 0x80;
	}
	*p++ = 0x79;		/* RESTART Ind */
	*p++ = 1;
	*p++ = ri;
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 0);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3ni1_dl_reset(struct l3_process *pc, u_char pr, void *arg)
{
	pc->para.cause = 0x29;          /* Temporary failure */
	pc->para.loc = 0;
	l3ni1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);
}

static void
l3ni1_dl_release(struct l3_process *pc, u_char pr, void *arg)
{
	newl3state(pc, 0);
	pc->para.cause = 0x1b;          /* Destination out of order */
	pc->para.loc = 0;
	pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
	release_l3_process(pc);
}

static void
l3ni1_dl_reestablish(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T309, CC_T309);
	l3_msg(pc->st, DL_ESTABLISH | REQUEST, NULL);
}

static void
l3ni1_dl_reest_status(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);

	pc->para.cause = 0x1F; /* normal, unspecified */
	l3ni1_status_send(pc, 0, NULL);
}

static void l3ni1_SendSpid(struct l3_process *pc, u_char pr, struct sk_buff *skb, int iNewState)
{
	u_char *p;
	char *pSPID;
	struct Channel *pChan = pc->st->lli.userdata;
	int l;

	if (skb)
		dev_kfree_skb(skb);

	if (!(pSPID = strchr(pChan->setup.eazmsn, ':')))
	{
		printk(KERN_ERR "SPID not supplied in EAZMSN %s\n", pChan->setup.eazmsn);
		newl3state(pc, 0);
		pc->st->l3.l3l2(pc->st, DL_RELEASE | REQUEST, NULL);
		return;
	}

	l = strlen(++pSPID);
	if (!(skb = l3_alloc_skb(5 + l)))
	{
		printk(KERN_ERR "HiSax can't get memory to send SPID\n");
		return;
	}

	p = skb_put(skb, 5);
	*p++ = PROTO_DIS_EURO;
	*p++ = 0;
	*p++ = MT_INFORMATION;
	*p++ = IE_SPID;
	*p++ = l;

	memcpy(skb_put(skb, l), pSPID, l);

	newl3state(pc, iNewState);

	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, TSPID, CC_TSPID);

	pc->st->l3.l3l2(pc->st, DL_DATA | REQUEST, skb);
}

static void l3ni1_spid_send(struct l3_process *pc, u_char pr, void *arg)
{
	l3ni1_SendSpid(pc, pr, arg, 20);
}

static void l3ni1_spid_epid(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	if (skb->data[1] == 0)
		if (skb->data[3] == IE_ENDPOINT_ID)
		{
			L3DelTimer(&pc->timer);
			newl3state(pc, 0);
			l3_msg(pc->st, DL_ESTABLISH | CONFIRM, NULL);
		}
	dev_kfree_skb(skb);
}

static void l3ni1_spid_tout(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->state < 22)
		l3ni1_SendSpid(pc, pr, arg, pc->state + 1);
	else
	{
		L3DelTimer(&pc->timer);
		dev_kfree_skb(arg);

		printk(KERN_ERR "SPID not accepted\n");
		newl3state(pc, 0);
		pc->st->l3.l3l2(pc->st, DL_RELEASE | REQUEST, NULL);
	}
}

/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_SETUP | REQUEST, l3ni1_setup_req},
	{SBIT(0),
	 CC_RESUME | REQUEST, l3ni1_resume_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(25),
	 CC_DISCONNECT | REQUEST, l3ni1_disconnect_req},
	{SBIT(12),
	 CC_RELEASE | REQUEST, l3ni1_release_req},
	{ALL_STATES,
	 CC_RESTART | REQUEST, l3ni1_restart},
	{SBIT(6) | SBIT(25),
	 CC_IGNORE | REQUEST, l3ni1_reset},
	{SBIT(6) | SBIT(25),
	 CC_REJECT | REQUEST, l3ni1_reject_req},
	{SBIT(6) | SBIT(25),
	 CC_PROCEED_SEND | REQUEST, l3ni1_proceed_req},
	{SBIT(6),
	 CC_MORE_INFO | REQUEST, l3ni1_setup_ack_req},
	{SBIT(25),
	 CC_MORE_INFO | REQUEST, l3ni1_dummy},
	{SBIT(6) | SBIT(9) | SBIT(25),
	 CC_ALERTING | REQUEST, l3ni1_alert_req},
	{SBIT(6) | SBIT(7) | SBIT(9) | SBIT(25),
	 CC_SETUP | RESPONSE, l3ni1_setup_rsp},
	{SBIT(10),
	 CC_SUSPEND | REQUEST, l3ni1_suspend_req},
	{SBIT(7) | SBIT(9) | SBIT(25),
	 CC_REDIR | REQUEST, l3ni1_redir_req},
	{SBIT(6),
	 CC_REDIR | REQUEST, l3ni1_redir_req_early},
	{SBIT(9) | SBIT(25),
	 CC_DISCONNECT | REQUEST, l3ni1_disconnect_req},
	{SBIT(25),
	 CC_T302, l3ni1_t302},
	{SBIT(1),
	 CC_T303, l3ni1_t303},
	{SBIT(2),
	 CC_T304, l3ni1_t304},
	{SBIT(3),
	 CC_T310, l3ni1_t310},
	{SBIT(8),
	 CC_T313, l3ni1_t313},
	{SBIT(11),
	 CC_T305, l3ni1_t305},
	{SBIT(15),
	 CC_T319, l3ni1_t319},
	{SBIT(17),
	 CC_T318, l3ni1_t318},
	{SBIT(19),
	 CC_T308_1, l3ni1_t308_1},
	{SBIT(19),
	 CC_T308_2, l3ni1_t308_2},
	{SBIT(10),
	 CC_T309, l3ni1_dl_release},
	{ SBIT(20) | SBIT(21) | SBIT(22),
	  CC_TSPID, l3ni1_spid_tout },
};

static struct stateentry datastatelist[] =
{
	{ALL_STATES,
	 MT_STATUS_ENQUIRY, l3ni1_status_enq},
	{ALL_STATES,
	 MT_FACILITY, l3ni1_facility},
	{SBIT(19),
	 MT_STATUS, l3ni1_release_ind},
	{ALL_STATES,
	 MT_STATUS, l3ni1_status},
	{SBIT(0),
	 MT_SETUP, l3ni1_setup},
	{SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(12) |
	 SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_SETUP, l3ni1_dummy},
	{SBIT(1) | SBIT(2),
	 MT_CALL_PROCEEDING, l3ni1_call_proc},
	{SBIT(1),
	 MT_SETUP_ACKNOWLEDGE, l3ni1_setup_ack},
	{SBIT(2) | SBIT(3),
	 MT_ALERTING, l3ni1_alerting},
	{SBIT(2) | SBIT(3),
	 MT_PROGRESS, l3ni1_progress},
	{SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_INFORMATION, l3ni1_information},
	{SBIT(10) | SBIT(11) | SBIT(15),
	 MT_NOTIFY, l3ni1_notify},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_RELEASE_COMPLETE, l3ni1_release_cmpl},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(25),
	 MT_RELEASE, l3ni1_release},
	{SBIT(19),  MT_RELEASE, l3ni1_release_ind},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(15) | SBIT(17) | SBIT(25),
	 MT_DISCONNECT, l3ni1_disconnect},
	{SBIT(19),
	 MT_DISCONNECT, l3ni1_dummy},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4),
	 MT_CONNECT, l3ni1_connect},
	{SBIT(8),
	 MT_CONNECT_ACKNOWLEDGE, l3ni1_connect_ack},
	{SBIT(15),
	 MT_SUSPEND_ACKNOWLEDGE, l3ni1_suspend_ack},
	{SBIT(15),
	 MT_SUSPEND_REJECT, l3ni1_suspend_rej},
	{SBIT(17),
	 MT_RESUME_ACKNOWLEDGE, l3ni1_resume_ack},
	{SBIT(17),
	 MT_RESUME_REJECT, l3ni1_resume_rej},
};

static struct stateentry globalmes_list[] =
{
	{ALL_STATES,
	 MT_STATUS, l3ni1_status},
	{SBIT(0),
	 MT_RESTART, l3ni1_global_restart},
/*	{SBIT(1),
	MT_RESTART_ACKNOWLEDGE, l3ni1_restart_ack},
*/
	{ SBIT(0), MT_DL_ESTABLISHED, l3ni1_spid_send },
	{ SBIT(20) | SBIT(21) | SBIT(22), MT_INFORMATION, l3ni1_spid_epid },
};

static struct stateentry manstatelist[] =
{
	{SBIT(2),
	 DL_ESTABLISH | INDICATION, l3ni1_dl_reset},
	{SBIT(10),
	 DL_ESTABLISH | CONFIRM, l3ni1_dl_reest_status},
	{SBIT(10),
	 DL_RELEASE | INDICATION, l3ni1_dl_reestablish},
	{ALL_STATES,
	 DL_RELEASE | INDICATION, l3ni1_dl_release},
};

/* *INDENT-ON* */


static void
global_handler(struct PStack *st, int mt, struct sk_buff *skb)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	int i;
	struct l3_process *proc = st->l3.global;

	if (skb)
		proc->callref = skb->data[2]; /* cr flag */
	else
		proc->callref = 0;
	for (i = 0; i < ARRAY_SIZE(globalmes_list); i++)
		if ((mt == globalmes_list[i].primitive) &&
		    ((1 << proc->state) & globalmes_list[i].state))
			break;
	if (i == ARRAY_SIZE(globalmes_list)) {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "ni1 global state %d mt %x unhandled",
				 proc->state, mt);
		}
		MsgHead(p, proc->callref, MT_STATUS);
		*p++ = IE_CAUSE;
		*p++ = 0x2;
		*p++ = 0x80;
		*p++ = 81 | 0x80;	/* invalid cr */
		*p++ = 0x14;		/* CallState */
		*p++ = 0x1;
		*p++ = proc->state & 0x3f;
		l = p - tmp;
		if (!(skb = l3_alloc_skb(l)))
			return;
		memcpy(skb_put(skb, l), tmp, l);
		l3_msg(proc->st, DL_DATA | REQUEST, skb);
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "ni1 global %d mt %x",
				 proc->state, mt);
		}
		globalmes_list[i].rout(proc, mt, skb);
	}
}

static void
ni1up(struct PStack *st, int pr, void *arg)
{
	int i, mt, cr, callState;
	char *ptr;
	u_char *p;
	struct sk_buff *skb = arg;
	struct l3_process *proc;

	switch (pr) {
	case (DL_DATA | INDICATION):
	case (DL_UNIT_DATA | INDICATION):
		break;
	case (DL_ESTABLISH | INDICATION):
	case (DL_RELEASE | INDICATION):
	case (DL_RELEASE | CONFIRM):
		l3_msg(st, pr, arg);
		return;
		break;

	case (DL_ESTABLISH | CONFIRM):
		global_handler(st, MT_DL_ESTABLISHED, NULL);
		return;

	default:
		printk(KERN_ERR "HiSax ni1up unknown pr=%04x\n", pr);
		return;
	}
	if (skb->len < 3) {
		l3_debug(st, "ni1up frame too short(%d)", skb->len);
		dev_kfree_skb(skb);
		return;
	}

	if (skb->data[0] != PROTO_DIS_EURO) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			l3_debug(st, "ni1up%sunexpected discriminator %x message len %d",
				 (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 skb->data[0], skb->len);
		}
		dev_kfree_skb(skb);
		return;
	}
	cr = getcallref(skb->data);
	if (skb->len < ((skb->data[1] & 0x0f) + 3)) {
		l3_debug(st, "ni1up frame too short(%d)", skb->len);
		dev_kfree_skb(skb);
		return;
	}
	mt = skb->data[skb->data[1] + 2];
	if (st->l3.debug & L3_DEB_STATE)
		l3_debug(st, "ni1up cr %d", cr);
	if (cr == -2) {  /* wrong Callref */
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "ni1up wrong Callref");
		dev_kfree_skb(skb);
		return;
	} else if (cr == -1) {	/* Dummy Callref */
		if (mt == MT_FACILITY)
		{
			if ((p = findie(skb->data, skb->len, IE_FACILITY, 0))) {
				l3ni1_parse_facility(st, NULL,
						     (pr == (DL_DATA | INDICATION)) ? -1 : -2, p);
				dev_kfree_skb(skb);
				return;
			}
		}
		else
		{
			global_handler(st, mt, skb);
			return;
		}

		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "ni1up dummy Callref (no facility msg or ie)");
		dev_kfree_skb(skb);
		return;
	} else if ((((skb->data[1] & 0x0f) == 1) && (0 == (cr & 0x7f))) ||
		   (((skb->data[1] & 0x0f) == 2) && (0 == (cr & 0x7fff)))) {	/* Global CallRef */
		if (st->l3.debug & L3_DEB_STATE)
			l3_debug(st, "ni1up Global CallRef");
		global_handler(st, mt, skb);
		dev_kfree_skb(skb);
		return;
	} else if (!(proc = getl3proc(st, cr))) {
		/* No transaction process exist, that means no call with
		 * this callreference is active
		 */
		if (mt == MT_SETUP) {
			/* Setup creates a new transaction process */
			if (skb->data[2] & 0x80) {
				/* Setup with wrong CREF flag */
				if (st->l3.debug & L3_DEB_STATE)
					l3_debug(st, "ni1up wrong CRef flag");
				dev_kfree_skb(skb);
				return;
			}
			if (!(proc = ni1_new_l3_process(st, cr))) {
				/* May be to answer with RELEASE_COMPLETE and
				 * CAUSE 0x2f "Resource unavailable", but this
				 * need a new_l3_process too ... arghh
				 */
				dev_kfree_skb(skb);
				return;
			}
		} else if (mt == MT_STATUS) {
			if ((ptr = findie(skb->data, skb->len, IE_CAUSE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
			}
			callState = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CALL_STATE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
				callState = *ptr;
			}
			/* ETS 300-104 part 2.4.1
			 * if setup has not been made and a message type
			 * MT_STATUS is received with call state == 0,
			 * we must send nothing
			 */
			if (callState != 0) {
				/* ETS 300-104 part 2.4.2
				 * if setup has not been made and a message type
				 * MT_STATUS is received with call state != 0,
				 * we must send MT_RELEASE_COMPLETE cause 101
				 */
				if ((proc = ni1_new_l3_process(st, cr))) {
					proc->para.cause = 101;
					l3ni1_msg_without_setup(proc, 0, NULL);
				}
			}
			dev_kfree_skb(skb);
			return;
		} else if (mt == MT_RELEASE_COMPLETE) {
			dev_kfree_skb(skb);
			return;
		} else {
			/* ETS 300-104 part 2
			 * if setup has not been made and a message type
			 * (except MT_SETUP and RELEASE_COMPLETE) is received,
			 * we must send MT_RELEASE_COMPLETE cause 81 */
			dev_kfree_skb(skb);
			if ((proc = ni1_new_l3_process(st, cr))) {
				proc->para.cause = 81;
				l3ni1_msg_without_setup(proc, 0, NULL);
			}
			return;
		}
	}
	if (l3ni1_check_messagetype_validity(proc, mt, skb)) {
		dev_kfree_skb(skb);
		return;
	}
	if ((p = findie(skb->data, skb->len, IE_DISPLAY, 0)) != NULL)
		l3ni1_deliver_display(proc, pr, p); /* Display IE included */
	for (i = 0; i < ARRAY_SIZE(datastatelist); i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == ARRAY_SIZE(datastatelist)) {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "ni1up%sstate %d mt %#x unhandled",
				 (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 proc->state, mt);
		}
		if ((MT_RELEASE_COMPLETE != mt) && (MT_RELEASE != mt)) {
			proc->para.cause = 101;
			l3ni1_status_send(proc, pr, skb);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "ni1up%sstate %d mt %x",
				 (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 proc->state, mt);
		}
		datastatelist[i].rout(proc, pr, skb);
	}
	dev_kfree_skb(skb);
	return;
}

static void
ni1down(struct PStack *st, int pr, void *arg)
{
	int i, cr;
	struct l3_process *proc;
	struct Channel *chan;

	if ((DL_ESTABLISH | REQUEST) == pr) {
		l3_msg(st, pr, NULL);
		return;
	} else if (((CC_SETUP | REQUEST) == pr) || ((CC_RESUME | REQUEST) == pr)) {
		chan = arg;
		cr = newcallref();
		cr |= 0x80;
		if ((proc = ni1_new_l3_process(st, cr))) {
			proc->chan = chan;
			chan->proc = proc;
			memcpy(&proc->para.setup, &chan->setup, sizeof(setup_parm));
			proc->callref = cr;
		}
	} else {
		proc = arg;
	}
	if (!proc) {
		printk(KERN_ERR "HiSax ni1down without proc pr=%04x\n", pr);
		return;
	}

	if (pr == (CC_TNI1_IO | REQUEST)) {
		l3ni1_io_timer(proc); /* timer expires */
		return;
	}

	for (i = 0; i < ARRAY_SIZE(downstatelist); i++)
		if ((pr == downstatelist[i].primitive) &&
		    ((1 << proc->state) & downstatelist[i].state))
			break;
	if (i == ARRAY_SIZE(downstatelist)) {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "ni1down state %d prim %#x unhandled",
				 proc->state, pr);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "ni1down state %d prim %#x",
				 proc->state, pr);
		}
		downstatelist[i].rout(proc, pr, arg);
	}
}

static void
ni1man(struct PStack *st, int pr, void *arg)
{
	int i;
	struct l3_process *proc = arg;

	if (!proc) {
		printk(KERN_ERR "HiSax ni1man without proc pr=%04x\n", pr);
		return;
	}
	for (i = 0; i < ARRAY_SIZE(manstatelist); i++)
		if ((pr == manstatelist[i].primitive) &&
		    ((1 << proc->state) & manstatelist[i].state))
			break;
	if (i == ARRAY_SIZE(manstatelist)) {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "cr %d ni1man state %d prim %#x unhandled",
				 proc->callref & 0x7f, proc->state, pr);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "cr %d ni1man state %d prim %#x",
				 proc->callref & 0x7f, proc->state, pr);
		}
		manstatelist[i].rout(proc, pr, arg);
	}
}

void
setstack_ni1(struct PStack *st)
{
	char tmp[64];
	int i;

	st->lli.l4l3 = ni1down;
	st->lli.l4l3_proto = l3ni1_cmd_global;
	st->l2.l2l3 = ni1up;
	st->l3.l3ml3 = ni1man;
	st->l3.N303 = 1;
	st->prot.ni1.last_invoke_id = 0;
	st->prot.ni1.invoke_used[0] = 1; /* Bit 0 must always be set to 1 */
	i = 1;
	while (i < 32)
		st->prot.ni1.invoke_used[i++] = 0;

	if (!(st->l3.global = kmalloc(sizeof(struct l3_process), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for ni1 global CR\n");
	} else {
		st->l3.global->state = 0;
		st->l3.global->callref = 0;
		st->l3.global->next = NULL;
		st->l3.global->debug = L3_DEB_WARN;
		st->l3.global->st = st;
		st->l3.global->N303 = 1;
		st->l3.global->prot.ni1.invoke_id = 0;

		L3InitTimer(st->l3.global, &st->l3.global->timer);
	}
	strcpy(tmp, ni1_revision);
	printk(KERN_INFO "HiSax: National ISDN-1 Rev. %s\n", HiSax_getrev(tmp));
}
