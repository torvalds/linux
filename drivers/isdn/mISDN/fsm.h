/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Author       Karsten Keil <kkeil@novell.com>
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 */

#ifndef _MISDN_FSM_H
#define _MISDN_FSM_H

#include <linux/timer.h>

/* Statemachine */

struct FsmInst;

typedef void (*FSMFNPTR)(struct FsmInst *, int, void *);

struct Fsm {
	FSMFNPTR *jumpmatrix;
	int state_count, event_count;
	char **strEvent, **strState;
};

struct FsmInst {
	struct Fsm *fsm;
	int state;
	int debug;
	void *userdata;
	int userint;
	void (*printdebug) (struct FsmInst *, char *, ...);
};

struct FsmNode {
	int state, event;
	void (*routine) (struct FsmInst *, int, void *);
};

struct FsmTimer {
	struct FsmInst *fi;
	struct timer_list tl;
	int event;
	void *arg;
};

extern int mISDN_FsmNew(struct Fsm *, struct FsmNode *, int);
extern void mISDN_FsmFree(struct Fsm *);
extern int mISDN_FsmEvent(struct FsmInst *, int , void *);
extern void mISDN_FsmChangeState(struct FsmInst *, int);
extern void mISDN_FsmInitTimer(struct FsmInst *, struct FsmTimer *);
extern int mISDN_FsmAddTimer(struct FsmTimer *, int, int, void *, int);
extern void mISDN_FsmRestartTimer(struct FsmTimer *, int, int, void *, int);
extern void mISDN_FsmDelTimer(struct FsmTimer *, int);

#endif
