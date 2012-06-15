/* $Id: isdn_ttyfax.c,v 1.1.2.2 2004/01/12 22:37:19 keil Exp $
 *
 * Linux ISDN subsystem, tty_fax AT-command emulator (linklevel).
 *
 * Copyright 1999    by Armin Schindler (mac@melware.de)
 * Copyright 1999    by Ralf Spachmann (mel@melware.de)
 * Copyright 1999    by Cytronics & Melware
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#undef ISDN_TTY_FAX_STAT_DEBUG
#undef ISDN_TTY_FAX_CMD_DEBUG

#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_tty.h"
#include "isdn_ttyfax.h"


static char *isdn_tty_fax_revision = "$Revision: 1.1.2.2 $";

#define PARSE_ERROR1 { isdn_tty_fax_modem_result(1, info); return 1; }

static char *
isdn_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

/*
 * Fax Class 2 Modem results
 *
 */

static void
isdn_tty_fax_modem_result(int code, modem_info *info)
{
	atemu *m = &info->emu;
	T30_s *f = info->fax;
	char rs[50];
	char rss[50];
	char *rp;
	int i;
	static char *msg[] =
		{"OK", "ERROR", "+FCON", "+FCSI:", "+FDIS:",
		 "+FHNG:", "+FDCS:", "CONNECT", "+FTSI:",
		 "+FCFR", "+FPTS:", "+FET:"};


	isdn_tty_at_cout("\r\n", info);
	isdn_tty_at_cout(msg[code], info);

#ifdef ISDN_TTY_FAX_CMD_DEBUG
	printk(KERN_DEBUG "isdn_tty: Fax send %s on ttyI%d\n",
	       msg[code], info->line);
#endif
	switch (code) {
	case 0: /* OK */
		break;
	case 1: /* ERROR */
		break;
	case 2:	/* +FCON */
		/* Append CPN, if enabled */
		if ((m->mdmreg[REG_CPNFCON] & BIT_CPNFCON) &&
		    (!(dev->usage[info->isdn_channel] & ISDN_USAGE_OUTGOING))) {
			sprintf(rs, "/%s", m->cpn);
			isdn_tty_at_cout(rs, info);
		}
		info->online = 1;
		f->fet = 0;
		if (f->phase == ISDN_FAX_PHASE_A)
			f->phase = ISDN_FAX_PHASE_B;
		break;
	case 3:	/* +FCSI */
	case 8:	/* +FTSI */
		sprintf(rs, "\"%s\"", f->r_id);
		isdn_tty_at_cout(rs, info);
		break;
	case 4:	/* +FDIS */
		rs[0] = 0;
		rp = &f->r_resolution;
		for (i = 0; i < 8; i++) {
			sprintf(rss, "%c%s", rp[i] + 48,
				(i < 7) ? "," : "");
			strcat(rs, rss);
		}
		isdn_tty_at_cout(rs, info);
#ifdef ISDN_TTY_FAX_CMD_DEBUG
		printk(KERN_DEBUG "isdn_tty: Fax DIS=%s on ttyI%d\n",
		       rs, info->line);
#endif
		break;
	case 5:	/* +FHNG */
		sprintf(rs, "%d", f->code);
		isdn_tty_at_cout(rs, info);
		info->faxonline = 0;
		break;
	case 6:	/* +FDCS */
		rs[0] = 0;
		rp = &f->r_resolution;
		for (i = 0; i < 8; i++) {
			sprintf(rss, "%c%s", rp[i] + 48,
				(i < 7) ? "," : "");
			strcat(rs, rss);
		}
		isdn_tty_at_cout(rs, info);
#ifdef ISDN_TTY_FAX_CMD_DEBUG
		printk(KERN_DEBUG "isdn_tty: Fax DCS=%s on ttyI%d\n",
		       rs, info->line);
#endif
		break;
	case 7:	/* CONNECT */
		info->faxonline |= 2;
		break;
	case 9:	/* FCFR */
		break;
	case 10:	/* FPTS */
		isdn_tty_at_cout("1", info);
		break;
	case 11:	/* FET */
		sprintf(rs, "%d", f->fet);
		isdn_tty_at_cout(rs, info);
		break;
	}

	isdn_tty_at_cout("\r\n", info);

	switch (code) {
	case 7:	/* CONNECT */
		info->online = 2;
		if (info->faxonline & 1) {
			sprintf(rs, "%c", XON);
			isdn_tty_at_cout(rs, info);
		}
		break;
	}
}

static int
isdn_tty_fax_command1(modem_info *info, isdn_ctrl *c)
{
	static char *msg[] =
		{"OK", "CONNECT", "NO CARRIER", "ERROR", "FCERROR"};

#ifdef ISDN_TTY_FAX_CMD_DEBUG
	printk(KERN_DEBUG "isdn_tty: FCLASS1 cmd(%d)\n", c->parm.aux.cmd);
#endif
	if (c->parm.aux.cmd < ISDN_FAX_CLASS1_QUERY) {
		if (info->online)
			info->online = 1;
		isdn_tty_at_cout("\r\n", info);
		isdn_tty_at_cout(msg[c->parm.aux.cmd], info);
		isdn_tty_at_cout("\r\n", info);
	}
	switch (c->parm.aux.cmd) {
	case ISDN_FAX_CLASS1_CONNECT:
		info->online = 2;
		break;
	case ISDN_FAX_CLASS1_OK:
	case ISDN_FAX_CLASS1_FCERROR:
	case ISDN_FAX_CLASS1_ERROR:
	case ISDN_FAX_CLASS1_NOCARR:
		break;
	case ISDN_FAX_CLASS1_QUERY:
		isdn_tty_at_cout("\r\n", info);
		if (!c->parm.aux.para[0]) {
			isdn_tty_at_cout(msg[ISDN_FAX_CLASS1_ERROR], info);
			isdn_tty_at_cout("\r\n", info);
		} else {
			isdn_tty_at_cout(c->parm.aux.para, info);
			isdn_tty_at_cout("\r\nOK\r\n", info);
		}
		break;
	}
	return (0);
}

int
isdn_tty_fax_command(modem_info *info, isdn_ctrl *c)
{
	T30_s *f = info->fax;
	char rs[10];

	if (TTY_IS_FCLASS1(info))
		return (isdn_tty_fax_command1(info, c));

#ifdef ISDN_TTY_FAX_CMD_DEBUG
	printk(KERN_DEBUG "isdn_tty: Fax cmd %d on ttyI%d\n",
	       f->r_code, info->line);
#endif
	switch (f->r_code) {
	case ISDN_TTY_FAX_FCON:
		info->faxonline = 1;
		isdn_tty_fax_modem_result(2, info);	/* +FCON */
		return (0);
	case ISDN_TTY_FAX_FCON_I:
		info->faxonline = 16;
		isdn_tty_fax_modem_result(2, info);	/* +FCON */
		return (0);
	case ISDN_TTY_FAX_RID:
		if (info->faxonline & 1)
			isdn_tty_fax_modem_result(3, info);	/* +FCSI */
		if (info->faxonline & 16)
			isdn_tty_fax_modem_result(8, info);	/* +FTSI */
		return (0);
	case ISDN_TTY_FAX_DIS:
		isdn_tty_fax_modem_result(4, info);	/* +FDIS */
		return (0);
	case ISDN_TTY_FAX_HNG:
		if (f->phase == ISDN_FAX_PHASE_C) {
			if (f->direction == ISDN_TTY_FAX_CONN_IN) {
				sprintf(rs, "%c%c", DLE, ETX);
				isdn_tty_at_cout(rs, info);
			} else {
				sprintf(rs, "%c", 0x18);
				isdn_tty_at_cout(rs, info);
			}
			info->faxonline &= ~2;	/* leave data mode */
			info->online = 1;
		}
		f->phase = ISDN_FAX_PHASE_E;
		isdn_tty_fax_modem_result(5, info);	/* +FHNG */
		isdn_tty_fax_modem_result(0, info);	/* OK */
		return (0);
	case ISDN_TTY_FAX_DCS:
		isdn_tty_fax_modem_result(6, info);	/* +FDCS */
		isdn_tty_fax_modem_result(7, info);	/* CONNECT */
		f->phase = ISDN_FAX_PHASE_C;
		return (0);
	case ISDN_TTY_FAX_TRAIN_OK:
		isdn_tty_fax_modem_result(6, info);	/* +FDCS */
		isdn_tty_fax_modem_result(0, info);	/* OK */
		return (0);
	case ISDN_TTY_FAX_SENT:
		isdn_tty_fax_modem_result(0, info);	/* OK */
		return (0);
	case ISDN_TTY_FAX_CFR:
		isdn_tty_fax_modem_result(9, info);	/* +FCFR */
		return (0);
	case ISDN_TTY_FAX_ET:
		sprintf(rs, "%c%c", DLE, ETX);
		isdn_tty_at_cout(rs, info);
		isdn_tty_fax_modem_result(10, info);	/* +FPTS */
		isdn_tty_fax_modem_result(11, info);	/* +FET */
		isdn_tty_fax_modem_result(0, info);	/* OK */
		info->faxonline &= ~2;	/* leave data mode */
		info->online = 1;
		f->phase = ISDN_FAX_PHASE_D;
		return (0);
	case ISDN_TTY_FAX_PTS:
		isdn_tty_fax_modem_result(10, info);	/* +FPTS */
		if (f->direction == ISDN_TTY_FAX_CONN_OUT) {
			if (f->fet == 1)
				f->phase = ISDN_FAX_PHASE_B;
			if (f->fet == 0)
				isdn_tty_fax_modem_result(0, info);	/* OK */
		}
		return (0);
	case ISDN_TTY_FAX_EOP:
		info->faxonline &= ~2;	/* leave data mode */
		info->online = 1;
		f->phase = ISDN_FAX_PHASE_D;
		return (0);

	}
	return (-1);
}


void
isdn_tty_fax_bitorder(modem_info *info, struct sk_buff *skb)
{
	__u8 LeftMask;
	__u8 RightMask;
	__u8 fBit;
	__u8 Data;
	int i;

	if (!info->fax->bor) {
		for (i = 0; i < skb->len; i++) {
			Data = skb->data[i];
			for (
				LeftMask = 0x80, RightMask = 0x01;
				LeftMask > RightMask;
				LeftMask >>= 1, RightMask <<= 1
				) {
				fBit = (Data & LeftMask);
				if (Data & RightMask)
					Data |= LeftMask;
				else
					Data &= ~LeftMask;
				if (fBit)
					Data |= RightMask;
				else
					Data &= ~RightMask;

			}
			skb->data[i] = Data;
		}
	}
}

/*
 * Parse AT+F.. FAX class 1 commands
 */

static int
isdn_tty_cmd_FCLASS1(char **p, modem_info *info)
{
	static char *cmd[] =
		{"AE", "TS", "RS", "TM", "RM", "TH", "RH"};
	isdn_ctrl c;
	int par, i;
	u_long flags;

	for (c.parm.aux.cmd = 0; c.parm.aux.cmd < 7; c.parm.aux.cmd++)
		if (!strncmp(p[0], cmd[c.parm.aux.cmd], 2))
			break;

#ifdef ISDN_TTY_FAX_CMD_DEBUG
	printk(KERN_DEBUG "isdn_tty_cmd_FCLASS1 (%s,%d)\n", p[0], c.parm.aux.cmd);
#endif
	if (c.parm.aux.cmd == 7)
		PARSE_ERROR1;

	p[0] += 2;
	switch (*p[0]) {
	case '?':
		p[0]++;
		c.parm.aux.subcmd = AT_QUERY;
		break;
	case '=':
		p[0]++;
		if (*p[0] == '?') {
			p[0]++;
			c.parm.aux.subcmd = AT_EQ_QUERY;
		} else {
			par = isdn_getnum(p);
			if ((par < 0) || (par > 255))
				PARSE_ERROR1;
			c.parm.aux.subcmd = AT_EQ_VALUE;
			c.parm.aux.para[0] = par;
		}
		break;
	case 0:
		c.parm.aux.subcmd = AT_COMMAND;
		break;
	default:
		PARSE_ERROR1;
	}
	c.command = ISDN_CMD_FAXCMD;
#ifdef ISDN_TTY_FAX_CMD_DEBUG
	printk(KERN_DEBUG "isdn_tty_cmd_FCLASS1 %d/%d/%d)\n",
	       c.parm.aux.cmd, c.parm.aux.subcmd, c.parm.aux.para[0]);
#endif
	if (info->isdn_driver < 0) {
		if ((c.parm.aux.subcmd == AT_EQ_VALUE) ||
		    (c.parm.aux.subcmd == AT_COMMAND)) {
			PARSE_ERROR1;
		}
		spin_lock_irqsave(&dev->lock, flags);
		/* get a temporary connection to the first free fax driver */
		i = isdn_get_free_channel(ISDN_USAGE_FAX, ISDN_PROTO_L2_FAX,
					  ISDN_PROTO_L3_FCLASS1, -1, -1, "00");
		if (i < 0) {
			spin_unlock_irqrestore(&dev->lock, flags);
			PARSE_ERROR1;
		}
		info->isdn_driver = dev->drvmap[i];
		info->isdn_channel = dev->chanmap[i];
		info->drv_index = i;
		dev->m_idx[i] = info->line;
		spin_unlock_irqrestore(&dev->lock, flags);
		c.driver = info->isdn_driver;
		c.arg = info->isdn_channel;
		isdn_command(&c);
		spin_lock_irqsave(&dev->lock, flags);
		isdn_free_channel(info->isdn_driver, info->isdn_channel,
				  ISDN_USAGE_FAX);
		info->isdn_driver = -1;
		info->isdn_channel = -1;
		if (info->drv_index >= 0) {
			dev->m_idx[info->drv_index] = -1;
			info->drv_index = -1;
		}
		spin_unlock_irqrestore(&dev->lock, flags);
	} else {
		c.driver = info->isdn_driver;
		c.arg = info->isdn_channel;
		isdn_command(&c);
	}
	return 1;
}

/*
 * Parse AT+F.. FAX class 2 commands
 */

static int
isdn_tty_cmd_FCLASS2(char **p, modem_info *info)
{
	atemu *m = &info->emu;
	T30_s *f = info->fax;
	isdn_ctrl cmd;
	int par;
	char rs[50];
	char rss[50];
	int maxdccval[] =
		{1, 5, 2, 2, 3, 2, 0, 7};

	/* FAA still unchanged */
	if (!strncmp(p[0], "AA", 2)) {	/* TODO */
		p[0] += 2;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", 0);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			par = isdn_getnum(p);
			if ((par < 0) || (par > 255))
				PARSE_ERROR1;
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* BADLIN=value - dummy 0=disable errorchk disabled, 1-255 nr. of lines for making page bad */
	if (!strncmp(p[0], "BADLIN", 6)) {
		p[0] += 6;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->badlin);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0-255");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 255))
					PARSE_ERROR1;
				f->badlin = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FBADLIN=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* BADMUL=value - dummy 0=disable errorchk disabled (threshold multiplier) */
	if (!strncmp(p[0], "BADMUL", 6)) {
		p[0] += 6;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->badmul);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0-255");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 255))
					PARSE_ERROR1;
				f->badmul = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FBADMUL=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* BOR=n - Phase C bit order, 0=direct, 1=reverse */
	if (!strncmp(p[0], "BOR", 3)) {
		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->bor);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0,1");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 1))
					PARSE_ERROR1;
				f->bor = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FBOR=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* NBC=n - No Best Capabilities */
	if (!strncmp(p[0], "NBC", 3)) {
		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->nbc);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0,1");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 1))
					PARSE_ERROR1;
				f->nbc = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FNBC=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* BUF? - Readonly buffersize readout  */
	if (!strncmp(p[0], "BUF?", 4)) {
		p[0] += 4;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
		printk(KERN_DEBUG "isdn_tty: Fax FBUF? (%d) \n", (16 * m->mdmreg[REG_PSIZE]));
#endif
		p[0]++;
		sprintf(rs, "\r\n %d ", (16 * m->mdmreg[REG_PSIZE]));
		isdn_tty_at_cout(rs, info);
		return 0;
	}
	/* CIG=string - local fax station id string for polling rx */
	if (!strncmp(p[0], "CIG", 3)) {
		int i, r;
		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n\"%s\"", f->pollid);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n\"STRING\"");
				isdn_tty_at_cout(rs, info);
			} else {
				if (*p[0] == '"')
					p[0]++;
				for (i = 0; (*p[0]) && i < (FAXIDLEN - 1) && (*p[0] != '"'); i++) {
					f->pollid[i] = *p[0]++;
				}
				if (*p[0] == '"')
					p[0]++;
				for (r = i; r < FAXIDLEN; r++) {
					f->pollid[r] = 32;
				}
				f->pollid[FAXIDLEN - 1] = 0;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax local poll ID rx \"%s\"\n", f->pollid);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* CQ=n - copy qlty chk, 0= no chk, 1=only 1D chk, 2=1D+2D chk */
	if (!strncmp(p[0], "CQ", 2)) {
		p[0] += 2;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->cq);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0,1,2");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 2))
					PARSE_ERROR1;
				f->cq = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FCQ=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* CR=n - can receive? 0= no data rx or poll remote dev, 1=do receive data or poll remote dev */
	if (!strncmp(p[0], "CR", 2)) {
		p[0] += 2;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->cr);	/* read actual value from struct and print */
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0,1");		/* display online help */
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 1))
					PARSE_ERROR1;
				f->cr = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FCR=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* CTCRTY=value - ECM retry count */
	if (!strncmp(p[0], "CTCRTY", 6)) {
		p[0] += 6;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->ctcrty);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0-255");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 255))
					PARSE_ERROR1;
				f->ctcrty = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FCTCRTY=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* DCC=vr,br,wd,ln,df,ec,bf,st - DCE capabilities parms */
	if (!strncmp(p[0], "DCC", 3)) {
		char *rp = &f->resolution;
		int i;

		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			strcpy(rs, "\r\n");
			for (i = 0; i < 8; i++) {
				sprintf(rss, "%c%s", rp[i] + 48,
					(i < 7) ? "," : "");
				strcat(rs, rss);
			}
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				isdn_tty_at_cout("\r\n(0,1),(0-5),(0-2),(0-2),(0-3),(0-2),(0),(0-7)", info);
				p[0]++;
			} else {
				for (i = 0; (((*p[0] >= '0') && (*p[0] <= '9')) || (*p[0] == ',')) && (i < 8); i++) {
					if (*p[0] != ',') {
						if ((*p[0] - 48) > maxdccval[i]) {
							PARSE_ERROR1;
						}
						rp[i] = *p[0] - 48;
						p[0]++;
						if (*p[0] == ',')
							p[0]++;
					} else
						p[0]++;
				}
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FDCC capabilities DCE=%d,%d,%d,%d,%d,%d,%d,%d\n",
				       rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* DIS=vr,br,wd,ln,df,ec,bf,st - current session parms */
	if (!strncmp(p[0], "DIS", 3)) {
		char *rp = &f->resolution;
		int i;

		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			strcpy(rs, "\r\n");
			for (i = 0; i < 8; i++) {
				sprintf(rss, "%c%s", rp[i] + 48,
					(i < 7) ? "," : "");
				strcat(rs, rss);
			}
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				isdn_tty_at_cout("\r\n(0,1),(0-5),(0-2),(0-2),(0-3),(0-2),(0),(0-7)", info);
				p[0]++;
			} else {
				for (i = 0; (((*p[0] >= '0') && (*p[0] <= '9')) || (*p[0] == ',')) && (i < 8); i++) {
					if (*p[0] != ',') {
						if ((*p[0] - 48) > maxdccval[i]) {
							PARSE_ERROR1;
						}
						rp[i] = *p[0] - 48;
						p[0]++;
						if (*p[0] == ',')
							p[0]++;
					} else
						p[0]++;
				}
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FDIS session parms=%d,%d,%d,%d,%d,%d,%d,%d\n",
				       rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* DR - Receive Phase C data command, initiates document reception */
	if (!strncmp(p[0], "DR", 2)) {
		p[0] += 2;
		if ((info->faxonline & 16) &&	/* incoming connection */
		    ((f->phase == ISDN_FAX_PHASE_B) || (f->phase == ISDN_FAX_PHASE_D))) {
#ifdef ISDN_TTY_FAX_STAT_DEBUG
			printk(KERN_DEBUG "isdn_tty: Fax FDR\n");
#endif
			f->code = ISDN_TTY_FAX_DR;
			cmd.driver = info->isdn_driver;
			cmd.arg = info->isdn_channel;
			cmd.command = ISDN_CMD_FAXCMD;
			isdn_command(&cmd);
			if (f->phase == ISDN_FAX_PHASE_B) {
				f->phase = ISDN_FAX_PHASE_C;
			} else if (f->phase == ISDN_FAX_PHASE_D) {
				switch (f->fet) {
				case 0:	/* next page will be received */
					f->phase = ISDN_FAX_PHASE_C;
					isdn_tty_fax_modem_result(7, info);	/* CONNECT */
					break;
				case 1:	/* next doc will be received */
					f->phase = ISDN_FAX_PHASE_B;
					break;
				case 2:	/* fax session is terminating */
					f->phase = ISDN_FAX_PHASE_E;
					break;
				default:
					PARSE_ERROR1;
				}
			}
		} else {
			PARSE_ERROR1;
		}
		return 1;
	}
	/* DT=df,vr,wd,ln - TX phase C data command (release DCE to proceed with negotiation) */
	if (!strncmp(p[0], "DT", 2)) {
		int i, val[] =
			{4, 0, 2, 3};
		char *rp = &f->resolution;

		p[0] += 2;
		if (!(info->faxonline & 1))	/* not outgoing connection */
			PARSE_ERROR1;

		for (i = 0; (((*p[0] >= '0') && (*p[0] <= '9')) || (*p[0] == ',')) && (i < 4); i++) {
			if (*p[0] != ',') {
				if ((*p[0] - 48) > maxdccval[val[i]]) {
					PARSE_ERROR1;
				}
				rp[val[i]] = *p[0] - 48;
				p[0]++;
				if (*p[0] == ',')
					p[0]++;
			} else
				p[0]++;
		}
#ifdef ISDN_TTY_FAX_STAT_DEBUG
		printk(KERN_DEBUG "isdn_tty: Fax FDT tx data command parms=%d,%d,%d,%d\n",
		       rp[4], rp[0], rp[2], rp[3]);
#endif
		if ((f->phase == ISDN_FAX_PHASE_B) || (f->phase == ISDN_FAX_PHASE_D)) {
			f->code = ISDN_TTY_FAX_DT;
			cmd.driver = info->isdn_driver;
			cmd.arg = info->isdn_channel;
			cmd.command = ISDN_CMD_FAXCMD;
			isdn_command(&cmd);
			if (f->phase == ISDN_FAX_PHASE_D) {
				f->phase = ISDN_FAX_PHASE_C;
				isdn_tty_fax_modem_result(7, info);	/* CONNECT */
			}
		} else {
			PARSE_ERROR1;
		}
		return 1;
	}
	/* ECM=n - Error mode control 0=disabled, 2=enabled, handled by DCE alone incl. buff of partial pages */
	if (!strncmp(p[0], "ECM", 3)) {
		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->ecm);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0,2");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par != 0) && (par != 2))
					PARSE_ERROR1;
				f->ecm = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FECM=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* ET=n - End of page or document */
	if (!strncmp(p[0], "ET=", 3)) {
		p[0] += 3;
		if (*p[0] == '?') {
			p[0]++;
			sprintf(rs, "\r\n0-2");
			isdn_tty_at_cout(rs, info);
		} else {
			if ((f->phase != ISDN_FAX_PHASE_D) ||
			    (!(info->faxonline & 1)))
				PARSE_ERROR1;
			par = isdn_getnum(p);
			if ((par < 0) || (par > 2))
				PARSE_ERROR1;
			f->fet = par;
			f->code = ISDN_TTY_FAX_ET;
			cmd.driver = info->isdn_driver;
			cmd.arg = info->isdn_channel;
			cmd.command = ISDN_CMD_FAXCMD;
			isdn_command(&cmd);
#ifdef ISDN_TTY_FAX_STAT_DEBUG
			printk(KERN_DEBUG "isdn_tty: Fax FET=%d\n", par);
#endif
			return 1;
		}
		return 0;
	}
	/* K - terminate */
	if (!strncmp(p[0], "K", 1)) {
		p[0] += 1;
		if ((f->phase == ISDN_FAX_PHASE_IDLE) || (f->phase == ISDN_FAX_PHASE_E))
			PARSE_ERROR1;
		isdn_tty_modem_hup(info, 1);
		return 1;
	}
	/* LID=string - local fax ID */
	if (!strncmp(p[0], "LID", 3)) {
		int i, r;
		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n\"%s\"", f->id);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n\"STRING\"");
				isdn_tty_at_cout(rs, info);
			} else {
				if (*p[0] == '"')
					p[0]++;
				for (i = 0; (*p[0]) && i < (FAXIDLEN - 1) && (*p[0] != '"'); i++) {
					f->id[i] = *p[0]++;
				}
				if (*p[0] == '"')
					p[0]++;
				for (r = i; r < FAXIDLEN; r++) {
					f->id[r] = 32;
				}
				f->id[FAXIDLEN - 1] = 0;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax local ID \"%s\"\n", f->id);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}

	/* MDL? - DCE Model       */
	if (!strncmp(p[0], "MDL?", 4)) {
		p[0] += 4;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
		printk(KERN_DEBUG "isdn_tty: FMDL?\n");
#endif
		isdn_tty_at_cout("\r\nisdn4linux", info);
		return 0;
	}
	/* MFR? - DCE Manufacturer */
	if (!strncmp(p[0], "MFR?", 4)) {
		p[0] += 4;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
		printk(KERN_DEBUG "isdn_tty: FMFR?\n");
#endif
		isdn_tty_at_cout("\r\nisdn4linux", info);
		return 0;
	}
	/* MINSP=n - Minimum Speed for Phase C */
	if (!strncmp(p[0], "MINSP", 5)) {
		p[0] += 5;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->minsp);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0-5");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 5))
					PARSE_ERROR1;
				f->minsp = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FMINSP=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* PHCTO=value - DTE phase C timeout */
	if (!strncmp(p[0], "PHCTO", 5)) {
		p[0] += 5;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->phcto);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0-255");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 255))
					PARSE_ERROR1;
				f->phcto = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FPHCTO=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}

	/* REL=n - Phase C received EOL alignment */
	if (!strncmp(p[0], "REL", 3)) {
		p[0] += 3;
		switch (*p[0]) {
		case '?':
			p[0]++;
			sprintf(rs, "\r\n%d", f->rel);
			isdn_tty_at_cout(rs, info);
			break;
		case '=':
			p[0]++;
			if (*p[0] == '?') {
				p[0]++;
				sprintf(rs, "\r\n0,1");
				isdn_tty_at_cout(rs, info);
			} else {
				par = isdn_getnum(p);
				if ((par < 0) || (par > 1))
					PARSE_ERROR1;
				f->rel = par;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
				printk(KERN_DEBUG "isdn_tty: Fax FREL=%d\n", par);
#endif
			}
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	/* REV? - DCE Revision */
	if (!strncmp(p[0], "REV?", 4)) {
		p[0] += 4;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
		printk(KERN_DEBUG "isdn_tty: FREV?\n");
#endif
		strcpy(rss, isdn_tty_fax_revision);
		sprintf(rs, "\r\nRev: %s", isdn_getrev(rss));
		isdn_tty_at_cout(rs, info);
		return 0;
	}

	/* Phase C Transmit Data Block Size */
	if (!strncmp(p[0], "TBC=", 4)) {	/* dummy, not used */
		p[0] += 4;
#ifdef ISDN_TTY_FAX_STAT_DEBUG
		printk(KERN_DEBUG "isdn_tty: Fax FTBC=%c\n", *p[0]);
#endif
		switch (*p[0]) {
		case '0':
			p[0]++;
			break;
		default:
			PARSE_ERROR1;
		}
		return 0;
	}
	printk(KERN_DEBUG "isdn_tty: unknown token=>AT+F%s<\n", p[0]);
	PARSE_ERROR1;
}

int
isdn_tty_cmd_PLUSF_FAX(char **p, modem_info *info)
{
	if (TTY_IS_FCLASS2(info))
		return (isdn_tty_cmd_FCLASS2(p, info));
	else if (TTY_IS_FCLASS1(info))
		return (isdn_tty_cmd_FCLASS1(p, info));
	PARSE_ERROR1;
}
