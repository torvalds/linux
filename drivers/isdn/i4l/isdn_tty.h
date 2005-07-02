/* $Id: isdn_tty.h,v 1.1.2.2 2004/01/12 22:37:19 keil Exp $
 *
 * header for Linux ISDN subsystem, tty related functions (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>

#define DLE 0x10
#define ETX 0x03
#define DC4 0x14


/*
 * Definition of some special Registers of AT-Emulator
 */
#define REG_RINGATA   0
#define REG_RINGCNT   1  /* ring counter register */
#define REG_ESC       2
#define REG_CR        3
#define REG_LF        4
#define REG_BS        5

#define REG_WAITC     7

#define REG_RESP     12  /* show response messages register */
#define BIT_RESP      1  /* show response messages bit      */
#define REG_RESPNUM  12  /* show numeric responses register */
#define BIT_RESPNUM   2  /* show numeric responses bit      */
#define REG_ECHO     12
#define BIT_ECHO      4
#define REG_DCD      12
#define BIT_DCD       8
#define REG_CTS      12
#define BIT_CTS      16
#define REG_DTRR     12
#define BIT_DTRR     32
#define REG_DSR      12
#define BIT_DSR      64
#define REG_CPPP     12
#define BIT_CPPP    128

#define REG_DXMT     13
#define BIT_DXMT      1
#define REG_T70      13
#define BIT_T70       2
#define BIT_T70_EXT  32
#define REG_DTRHUP   13
#define BIT_DTRHUP    4
#define REG_RESPXT   13
#define BIT_RESPXT    8
#define REG_CIDONCE  13
#define BIT_CIDONCE  16
#define REG_RUNG     13  /* show RUNG message register      */
#define BIT_RUNG     64  /* show RUNG message bit           */
#define REG_DISPLAY  13
#define BIT_DISPLAY 128

#define REG_L2PROT   14
#define REG_L3PROT   15
#define REG_PSIZE    16
#define REG_WSIZE    17
#define REG_SI1      18
#define REG_SI2      19
#define REG_SI1I     20
#define REG_PLAN     21
#define REG_SCREEN   22

#define REG_CPN      23
#define BIT_CPN       1
#define REG_CPNFCON  23
#define BIT_CPNFCON   2
#define REG_CDN      23
#define BIT_CDN       4

/* defines for result codes */
#define RESULT_OK		0
#define RESULT_CONNECT		1
#define RESULT_RING		2
#define RESULT_NO_CARRIER	3
#define RESULT_ERROR		4
#define RESULT_CONNECT64000	5
#define RESULT_NO_DIALTONE	6
#define RESULT_BUSY		7
#define RESULT_NO_ANSWER	8
#define RESULT_RINGING		9
#define RESULT_NO_MSN_EAZ	10
#define RESULT_VCON		11
#define RESULT_RUNG		12

#define TTY_IS_FCLASS1(info) \
	((info->emu.mdmreg[REG_L2PROT] == ISDN_PROTO_L2_FAX) && \
	 (info->emu.mdmreg[REG_L3PROT] == ISDN_PROTO_L3_FCLASS1))
#define TTY_IS_FCLASS2(info) \
	((info->emu.mdmreg[REG_L2PROT] == ISDN_PROTO_L2_FAX) && \
	 (info->emu.mdmreg[REG_L3PROT] == ISDN_PROTO_L3_FCLASS2))

extern void isdn_tty_modem_escape(void);
extern void isdn_tty_modem_ring(void);
extern void isdn_tty_carrier_timeout(void);
extern void isdn_tty_modem_xmit(void);
extern int  isdn_tty_modem_init(void);
extern void isdn_tty_exit(void);
extern void isdn_tty_readmodem(void);
extern int  isdn_tty_find_icall(int, int, setup_parm *);
extern int  isdn_tty_stat_callback(int, isdn_ctrl *);
extern int  isdn_tty_rcv_skb(int, int, int, struct sk_buff *);
extern int  isdn_tty_capi_facility(capi_msg *cm); 
extern void isdn_tty_at_cout(char *, modem_info *);
extern void isdn_tty_modem_hup(modem_info *, int);
#ifdef CONFIG_ISDN_TTY_FAX
extern int  isdn_tty_cmd_PLUSF_FAX(char **, modem_info *);
extern int  isdn_tty_fax_command(modem_info *, isdn_ctrl *);
extern void isdn_tty_fax_bitorder(modem_info *, struct sk_buff *);
#endif
