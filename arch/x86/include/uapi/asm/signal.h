#ifndef _UAPI_ASM_X86_SIGNAL_H
#define _UAPI_ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/time.h>
#include <linux/compiler.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

#define NSIG		32
typedef unsigned long sigset_t;

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */

/*
	处理动作:
	A 缺省的动作是终止进程
	B 缺省的动作是忽略此信号 
	C 缺省的动作是终止进程并进行内核映像转储（dump core）
	D 缺省的动作是停止进程
	E 信号不能被捕获
	F 信号不能被忽略
*/
/* 信号也叫软中断，是进程间通信的一种方式, 信号只是用来通知某进程发
** 生了什么事件，并不给该进程传递任何数据 */

#define SIGHUP		 1	/* (A)		终端挂起或者控制进程终止			*/
#define SIGINT		 2	/* (A)		键盘中断（如break键被按下）		*/
#define SIGQUIT		 3	/* (C)		键盘的退出键被按下					*/
#define SIGILL		 4	/* (C)		非法指令*/
#define SIGTRAP		 5	/* ()		*/
#define SIGABRT		 6	/* (C)		由abort(3)发出的退出指令*/
#define SIGIOT		 6	/* ()*/
#define SIGBUS		 7	/* ()*/
#define SIGFPE		 8	/* (C)		浮点异常 */
#define SIGKILL		 9	/* (AEF)	Kill信号 */
#define SIGUSR1		10	/* ()*/
#define SIGSEGV		11	/* (C)		无效的内存引用*/
#define SIGUSR2		12	/* ()*/
#define SIGPIPE		13	/* (A)		管道破裂: 写一个没有读端口的管道 */
#define SIGALRM		14	/* (A)		由alarm(2)发出的信号*/
#define SIGTERM		15	/* (A)		终止信号*/
#define SIGSTKFLT	16	/* ()*/
#define SIGCHLD		17	/* (B)		子进程结束信号 */
#define SIGCONT		18	/* ()*/
#define SIGSTOP		19	/* ()*/
#define SIGTSTP		20	/* ()*/
#define SIGTTIN		21	/* ()*/
#define SIGTTOU		22	/* ()*/
#define SIGURG		23	/* ()*/
#define SIGXCPU		24	/* ()*/
#define SIGXFSZ		25	/* ()*/
#define SIGVTALRM	26	/* ()*/
#define SIGPROF		27	/* ()*/
#define SIGWINCH	28	/* ()*/
#define SIGIO		29	/* ()*/
#define SIGPOLL		SIGIO
/*
#define SIGLOST		29
*/
#define SIGPWR		30
#define SIGSYS		31
#define	SIGUNUSED	31

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_NOCLDSTOP	0x00000001u
#define SA_NOCLDWAIT	0x00000002u
#define SA_SIGINFO	0x00000004u
#define SA_ONSTACK	0x08000000u
#define SA_RESTART	0x10000000u
#define SA_NODEFER	0x40000000u
#define SA_RESETHAND	0x80000000u

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND

#define SA_RESTORER	0x04000000

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#include <asm-generic/signal-defs.h>

#ifndef __ASSEMBLY__


# ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */
#ifdef __i386__

struct sigaction {
	union {
	  __sighandler_t _sa_handler;
	  void (*_sa_sigaction)(int, struct siginfo *, void *);
	} _u;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

#define sa_handler	_u._sa_handler
#define sa_sigaction	_u._sa_sigaction

#else /* __i386__ */

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestore_t sa_restorer;
	sigset_t sa_mask;		/* mask last for extensibility */
};

#endif /* !__i386__ */
# endif /* ! __KERNEL__ */

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#endif /* __ASSEMBLY__ */

#endif /* _UAPI_ASM_X86_SIGNAL_H */
