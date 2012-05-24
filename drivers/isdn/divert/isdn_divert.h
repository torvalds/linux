/* $Id: isdn_divert.h,v 1.5.6.1 2001/09/23 22:24:36 kai Exp $
 *
 * Header for the diversion supplementary ioctl interface.
 *
 * Copyright 1998       by Werner Cornelius (werner@ikt.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/ioctl.h>
#include <linux/types.h>

/******************************************/
/* IOCTL codes for interface to user prog */
/******************************************/
#define DIVERT_IIOC_VERSION 0x01 /* actual version */
#define IIOCGETVER   _IO('I', 1)  /* get version of interface */
#define IIOCGETDRV   _IO('I', 2)  /* get driver number */
#define IIOCGETNAM   _IO('I', 3)  /* get driver name */
#define IIOCGETRULE  _IO('I', 4)  /* read one rule */
#define IIOCMODRULE  _IO('I', 5)  /* modify/replace a rule */
#define IIOCINSRULE  _IO('I', 6)  /* insert/append one rule */
#define IIOCDELRULE  _IO('I', 7)  /* delete a rule */
#define IIOCDODFACT  _IO('I', 8)  /* hangup/reject/alert/immediately deflect a call */
#define IIOCDOCFACT  _IO('I', 9)  /* activate control forwarding in PBX */
#define IIOCDOCFDIS  _IO('I', 10)  /* deactivate control forwarding in PBX */
#define IIOCDOCFINT  _IO('I', 11)  /* interrogate control forwarding in PBX */

/*************************************/
/* states reported through interface */
/*************************************/
#define DEFLECT_IGNORE    0  /* ignore incoming call */
#define DEFLECT_REPORT    1  /* only report */
#define DEFLECT_PROCEED   2  /* deflect when externally triggered */
#define DEFLECT_ALERT     3  /* alert and deflect after delay */
#define DEFLECT_REJECT    4  /* reject immediately */
#define DIVERT_ACTIVATE   5  /* diversion activate */
#define DIVERT_DEACTIVATE 6  /* diversion deactivate */
#define DIVERT_REPORT     7  /* interrogation result */
#define DEFLECT_AUTODEL 255  /* only for internal use */

#define DEFLECT_ALL_IDS   0xFFFFFFFF /* all drivers selected */

typedef struct
{ ulong drvid;     /* driver ids, bit mapped */
	char my_msn[35]; /* desired msn, subaddr allowed */
	char caller[35]; /* caller id, partial string with * + subaddr allowed */
	char to_nr[35];  /* deflected to number incl. subaddress */
	u_char si1, si2;  /* service indicators, si1=bitmask, si1+2 0 = all */
	u_char screen;   /* screening: 0 = no info, 1 = info, 2 = nfo with nr */
	u_char callopt;  /* option for call handling:
			    0 = all calls
			    1 = only non waiting calls
			    2 = only waiting calls */
	u_char action;   /* desired action:
			    0 = don't report call -> ignore
			    1 = report call, do not allow/proceed for deflection
			    2 = report call, send proceed, wait max waittime secs
			    3 = report call, alert and deflect after waittime
			    4 = report call, reject immediately
			    actions 1-2 only take place if interface is opened
			 */
	u_char waittime; /* maximum wait time for proceeding */
} divert_rule;

typedef union
{ int drv_version; /* return of driver version */
	struct
	{ int drvid;		/* id of driver */
		char drvnam[30];	/* name of driver */
	} getid;
	struct
	{ int ruleidx;	/* index of rule */
		divert_rule rule;	/* rule parms */
	} getsetrule;
	struct
	{ u_char subcmd;  /* 0 = hangup/reject,
			     1 = alert,
			     2 = deflect */
		ulong callid;   /* id of call delivered by ascii output */
		char to_nr[35]; /* destination when deflect,
				   else uus1 string (maxlen 31),
				   data from rule used if empty */
	} fwd_ctrl;
	struct
	{ int drvid;      /* id of driver */
		u_char cfproc;  /* cfu = 0, cfb = 1, cfnr = 2 */
		ulong procid;   /* process id returned when no error */
		u_char service; /* basically coded service, 0 = all */
		char msn[25];   /* desired msn, empty = all */
		char fwd_nr[35];/* forwarded to number + subaddress */
	} cf_ctrl;
} divert_ioctl;

#ifdef __KERNEL__

#include <linux/isdnif.h>
#include <linux/isdn_divertif.h>

#define AUTODEL_TIME 30 /* timeout in s to delete internal entries */

/**************************************************/
/* structure keeping ascii info for device output */
/**************************************************/
struct divert_info
{ struct divert_info *next;
	ulong usage_cnt; /* number of files still to work */
	char info_start[2]; /* info string start */
};


/**************/
/* Prototypes */
/**************/
extern spinlock_t divert_lock;

extern ulong if_used; /* number of interface users */
extern int divert_dev_deinit(void);
extern int divert_dev_init(void);
extern void put_info_buffer(char *);
extern int ll_callback(isdn_ctrl *);
extern isdn_divert_if divert_if;
extern divert_rule *getruleptr(int);
extern int insertrule(int, divert_rule *);
extern int deleterule(int);
extern void deleteprocs(void);
extern int deflect_extern_action(u_char, ulong, char *);
extern int cf_command(int, int, u_char, char *, u_char, char *, ulong *);

#endif /* __KERNEL__ */
