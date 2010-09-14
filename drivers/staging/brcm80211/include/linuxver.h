/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _linuxver_h_
#define _linuxver_h_

#include <linux/version.h>
#include <linux/module.h>

#include <linux/slab.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#undef IP_TOS
#include <asm/io.h>

#include <linux/workqueue.h>

#define	MY_INIT_WORK(_work, _func)	INIT_WORK(_work, _func)

typedef irqreturn_t(*FN_ISR) (int irq, void *dev_id, struct pt_regs * ptregs);

#include <linux/sched.h>
#include <linux/ieee80211.h>

#ifndef __exit
#define __exit
#endif
#ifndef __devexit
#define __devexit
#endif
#ifndef __devinit
#define __devinit	__init
#endif
#ifndef __devinitdata
#define __devinitdata
#endif
#ifndef __devexit_p
#define __devexit_p(x)	x
#endif

#define pci_module_init pci_register_driver

#define netif_down(dev)

/* Power management related macro & routines */
#define	PCI_SAVE_STATE(a, b)	pci_save_state(a)
#define	PCI_RESTORE_STATE(a, b)	pci_restore_state(a)

/* Module refcount handled internally in 2.6.x */
#ifndef SET_MODULE_OWNER
#define SET_MODULE_OWNER(dev)		do {} while (0)
#endif
#ifndef MOD_INC_USE_COUNT
#define MOD_INC_USE_COUNT			do {} while (0)
#endif
#ifndef MOD_DEC_USE_COUNT
#define MOD_DEC_USE_COUNT			do {} while (0)
#endif
#define OLD_MOD_INC_USE_COUNT		MOD_INC_USE_COUNT
#define OLD_MOD_DEC_USE_COUNT		MOD_DEC_USE_COUNT

#ifndef SET_NETDEV_DEV
#define SET_NETDEV_DEV(net, pdev)	do {} while (0)
#endif

#ifndef HAVE_FREE_NETDEV
#define free_netdev(dev)		kfree(dev)
#endif

/* suspend args */
#define DRV_SUSPEND_STATE_TYPE pm_message_t

#define CHECKSUM_HW	CHECKSUM_PARTIAL

#include <linux/time.h>
#include <linux/wait.h>

#define KILL_PROC(nr, sig) \
	do { \
		struct task_struct *tsk; \
		struct pid *pid;    \
		pid = find_get_pid((pid_t)nr);    \
		tsk = pid_task(pid, PIDTYPE_PID);    \
		if (tsk) \
			send_sig(sig, tsk, 1); \
	} while (0)

#define WL_DEV_IF(dev)          ((wl_if_t *)netdev_priv(dev))

#endif				/* _linuxver_h_ */
