/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: func.h
**	SID		: 1.3
**	Last Modified	: 11/6/98 11:34:10
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)func.h	1.3
**
** -----------------------------------------------------------------------------
*/

#ifndef __func_h_def
#define __func_h_def

#include <linux/kdev_t.h>

/* rioboot.c */
int RIOBootCodeRTA(struct rio_info *, struct DownLoad *);
int RIOBootCodeHOST(struct rio_info *, struct DownLoad *);
int RIOBootCodeUNKNOWN(struct rio_info *, struct DownLoad *);
void msec_timeout(struct Host *);
int RIOBootRup(struct rio_info *, unsigned int, struct Host *, struct PKT __iomem *);
int RIOBootOk(struct rio_info *, struct Host *, unsigned long);
int RIORtaBound(struct rio_info *, unsigned int);
void rio_fill_host_slot(int, int, unsigned int, struct Host *);

/* riocmd.c */
int RIOFoadRta(struct Host *, struct Map *);
int RIOZombieRta(struct Host *, struct Map *);
int RIOCommandRta(struct rio_info *, unsigned long, int (*func) (struct Host *, struct Map *));
int RIOIdentifyRta(struct rio_info *, void __user *);
int RIOKillNeighbour(struct rio_info *, void __user *);
int RIOSuspendBootRta(struct Host *, int, int);
int RIOFoadWakeup(struct rio_info *);
struct CmdBlk *RIOGetCmdBlk(void);
void RIOFreeCmdBlk(struct CmdBlk *);
int RIOQueueCmdBlk(struct Host *, unsigned int, struct CmdBlk *);
void RIOPollHostCommands(struct rio_info *, struct Host *);
int RIOWFlushMark(unsigned long, struct CmdBlk *);
int RIORFlushEnable(unsigned long, struct CmdBlk *);
int RIOUnUse(unsigned long, struct CmdBlk *);

/* rioctrl.c */
int riocontrol(struct rio_info *, dev_t, int, unsigned long, int);

int RIOPreemptiveCmd(struct rio_info *, struct Port *, unsigned char);

/* rioinit.c */
void rioinit(struct rio_info *, struct RioHostInfo *);
void RIOInitHosts(struct rio_info *, struct RioHostInfo *);
void RIOISAinit(struct rio_info *, int);
int RIODoAT(struct rio_info *, int, int);
caddr_t RIOCheckForATCard(int);
int RIOAssignAT(struct rio_info *, int, void __iomem *, int);
int RIOBoardTest(unsigned long, void __iomem *, unsigned char, int);
void RIOAllocDataStructs(struct rio_info *);
void RIOSetupDataStructs(struct rio_info *);
int RIODefaultName(struct rio_info *, struct Host *, unsigned int);
struct rioVersion *RIOVersid(void);
void RIOHostReset(unsigned int, struct DpRam __iomem *, unsigned int);

/* riointr.c */
void RIOTxEnable(char *);
void RIOServiceHost(struct rio_info *, struct Host *);
int riotproc(struct rio_info *, struct ttystatics *, int, int);

/* rioparam.c */
int RIOParam(struct Port *, int, int, int);
int RIODelay(struct Port *PortP, int);
int RIODelay_ni(struct Port *PortP, int);
void ms_timeout(struct Port *);
int can_add_transmit(struct PKT __iomem **, struct Port *);
void add_transmit(struct Port *);
void put_free_end(struct Host *, struct PKT __iomem *);
int can_remove_receive(struct PKT __iomem **, struct Port *);
void remove_receive(struct Port *);

/* rioroute.c */
int RIORouteRup(struct rio_info *, unsigned int, struct Host *, struct PKT __iomem *);
void RIOFixPhbs(struct rio_info *, struct Host *, unsigned int);
unsigned int GetUnitType(unsigned int);
int RIOSetChange(struct rio_info *);
int RIOFindFreeID(struct rio_info *, struct Host *, unsigned int *, unsigned int *);


/* riotty.c */

int riotopen(struct tty_struct *tty, struct file *filp);
int riotclose(void *ptr);
int riotioctl(struct rio_info *, struct tty_struct *, int, caddr_t);
void ttyseth(struct Port *, struct ttystatics *, struct old_sgttyb *sg);

/* riotable.c */
int RIONewTable(struct rio_info *);
int RIOApel(struct rio_info *);
int RIODeleteRta(struct rio_info *, struct Map *);
int RIOAssignRta(struct rio_info *, struct Map *);
int RIOReMapPorts(struct rio_info *, struct Host *, struct Map *);
int RIOChangeName(struct rio_info *, struct Map *);

#if 0
/* riodrvr.c */
struct rio_info *rio_install(struct RioHostInfo *);
int rio_uninstall(struct rio_info *);
int rio_open(struct rio_info *, int, struct file *);
int rio_close(struct rio_info *, struct file *);
int rio_read(struct rio_info *, struct file *, char *, int);
int rio_write(struct rio_info *, struct file *f, char *, int);
int rio_ioctl(struct rio_info *, struct file *, int, char *);
int rio_select(struct rio_info *, struct file *f, int, struct sel *);
int rio_intr(char *);
int rio_isr_thread(char *);
struct rio_info *rio_info_store(int cmd, struct rio_info *p);
#endif

extern void rio_copy_to_card(void *from, void __iomem *to, int len);
extern int rio_minor(struct tty_struct *tty);
extern int rio_ismodem(struct tty_struct *tty);

extern void rio_start_card_running(struct Host *HostP);

#endif				/* __func_h_def */
