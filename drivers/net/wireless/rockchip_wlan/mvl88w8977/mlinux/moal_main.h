/** @file moal_main.h
  *
  * @brief This file contains wlan driver specific defines etc.
  *
  * Copyright (C) 2008-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

/********************************************************
Change log:
    10/21/2008: initial version
********************************************************/

#ifndef _MOAL_MAIN_H
#define _MOAL_MAIN_H

/* warnfix for FS redefination if any? */
#ifdef FS
#undef FS
#endif

/* Linux header files */
#include        <linux/kernel.h>
#include        <linux/module.h>
#include        <linux/init.h>
#include        <linux/version.h>
#include        <linux/param.h>
#include        <linux/delay.h>
#include        <linux/slab.h>
#include        <linux/mm.h>
#include        <linux/types.h>
#include        <linux/sched.h>
#include        <linux/timer.h>
#include        <linux/ioport.h>
#include        <linux/pci.h>
#include        <linux/ctype.h>
#include        <linux/proc_fs.h>
#include        <linux/vmalloc.h>
#include        <linux/ptrace.h>
#include        <linux/string.h>
#include        <linux/irqreturn.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
#include        <linux/namei.h>
#include        <linux/fs.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
#include       <linux/config.h>
#endif

/* ASM files */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include        <linux/semaphore.h>
#else
#include        <asm/semaphore.h>
#endif
#include        <asm/byteorder.h>
#include        <asm/irq.h>
#include        <linux/uaccess.h>
#include        <asm/io.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
#include        <asm/switch_to.h>
#else
#include        <asm/system.h>
#endif

#include        <linux/spinlock.h>

/* Net header files */
#include        <linux/netdevice.h>
#include        <linux/net.h>
#include        <linux/inet.h>
#include        <linux/ip.h>
#include        <linux/skbuff.h>
#include        <linux/if_arp.h>
#include        <linux/if_ether.h>
#include        <linux/etherdevice.h>
#include        <net/sock.h>
#include        <net/arp.h>
#include        <linux/rtnetlink.h>
#include        <linux/inetdevice.h>

#include	<linux/firmware.h>

#ifdef ANDROID_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
#include <linux/pm_wakeup.h>
#include <linux/device.h>
#else
#include <linux/wakelock.h>
#endif
#endif

#include <net/ieee80211_radiotap.h>

#include        "mlan.h"
#include        "moal_shim.h"
/* Wireless header */
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#include        <net/lib80211.h>
#include        <net/cfg80211.h>
#include        <net/ieee80211_radiotap.h>
#endif
#if defined(STA_WEXT) || defined(UAP_WEXT)
#include        <linux/wireless.h>
#include        <net/iw_handler.h>
#include        "moal_wext.h"
#endif
#ifdef STA_WEXT
#include        "moal_priv.h"
#endif

#ifndef MIN
/** Find minimum */
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#endif

/** Find maximum */
#ifndef MAX
#define MAX(a, b)                ((a) > (b) ? (a) : (b))
#endif

#define COMPAT_VERSION_CODE KERNEL_VERSION( 0, 0, 0)
#define CFG80211_VERSION_CODE MAX(LINUX_VERSION_CODE, COMPAT_VERSION_CODE)

/**
 * Reason Code 3: STA is leaving (or has left) IBSS or ESS
 */
#define DEF_DEAUTH_REASON_CODE (0x3)

/**
 * 802.1 Local Experimental 1.
 */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 24)
#define REFDATA __refdata
#else
#define REFDATA
#endif

/**
 * Linux Kernels later 3.9 use CONFIG_PM_RUNTIME instead of
 * CONFIG_USB_SUSPEND
 * Linux Kernels later 3.19 use CONFIG_PM instead of
 * CONFIG_PM_RUNTIME
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
#ifdef CONFIG_PM
#ifndef CONFIG_USB_SUSPEND
#define CONFIG_USB_SUSPEND
#endif
#ifndef CONFIG_PM_RUNTIME
#define CONFIG_PM_RUNTIME
#endif
#endif
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) */
#ifdef CONFIG_PM_RUNTIME
#ifndef CONFIG_USB_SUSPEND
#define CONFIG_USB_SUSPEND
#endif
#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) */
#endif

/**
 * Linux kernel later 3.10 use strncasecmp instead of strnicmp
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define strnicmp strncasecmp
#endif

/**
 * Linux kernel later 4.7 use nl80211_band instead of ieee80211_band
 * Linux kernel later 4.7 use new macro
 */
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
#define ieee80211_band          nl80211_band
#define IEEE80211_BAND_2GHZ     NL80211_BAND_2GHZ
#define IEEE80211_BAND_5GHZ     NL80211_BAND_5GHZ
#define IEEE80211_NUM_BANDS     NUM_NL80211_BANDS
#endif

/**
* interface name
*/
#define default_mlan_name    "wlan%%d"
#define default_uap_name    "uap%%d"
#define default_wfd_name    "p2p%%d"
#define default_nan_name    "nan%%d"
#define default_mpl_name    "mpl%d"
#define default_11p_name    "ocb%d"
#define mwiphy_name          "mwiphy%d"

#ifdef OPENWRT
#ifdef mwiphy_name
#undef mwiphy_name
#define mwiphy_name          "phy%d"
#endif
#ifdef default_mlan_name
#undef default_mlan_name
#define default_mlan_name "wlan%%d"
#endif
#endif

/**
 * define write_can_lock() to fix compile issue on ACTIA platform
 */
#if !defined(write_can_lock) && defined(CONFIG_PREEMPT_RT_FULL)
#define write_can_lock(X) 1
#endif

#define MLAN_ACTION_FRAME_CATEGORY_OFFSET (40)
#define MLAN_ACTION_FRAME_ACTION_OFFSET (41)

/** Define BOOLEAN */
typedef t_u8 BOOLEAN;

/** Driver version */
extern char driver_version[];

/** Private structure for MOAL */
typedef struct _moal_private moal_private;
/** Handle data structure for MOAL  */
typedef struct _moal_handle moal_handle;

/** Hardware status codes */
typedef enum _MOAL_HARDWARE_STATUS {
	HardwareStatusReady,
	HardwareStatusInitializing,
	HardwareStatusFwReady,
	HardwareStatusReset,
	HardwareStatusClosing,
	HardwareStatusNotReady
} MOAL_HARDWARE_STATUS;

/** fw cap info 11p */
#define FW_CAPINFO_80211P          MBIT(24)
/** fw cap info BGA */
#define FW_CAPINFO_80211BGA        (MBIT(8)|MBIT(9)|MBIT(10))

/** moal_wait_option */
enum {
	MOAL_NO_WAIT,
	MOAL_IOCTL_WAIT,
	MOAL_IOCTL_WAIT_TIMEOUT
};

/** moal_main_state */
enum {
	MOAL_STATE_IDLE,
	MOAL_RECV_INT,
	MOAL_ENTER_WORK_QUEUE,
	MOAL_START_MAIN_PROCESS,
	MOAL_END_MAIN_PROCESS
};

/** HostCmd_Header */
typedef struct _HostCmd_Header {
    /** Command */
	t_u16 command;
    /** Size */
	t_u16 size;
} HostCmd_Header;

/*
 * OS timer specific
 */

/** Timer structure */
typedef struct _moal_drv_timer {
	/** Timer list */
	struct timer_list tl;
	/** Timer function */
	void (*timer_function) (void *context);
	/** Timer function context */
	void *function_context;
	/** Time period */
	t_u32 time_period;
	/** Is timer periodic ? */
	t_u32 timer_is_periodic;
	/** Is timer cancelled ? */
	t_u32 timer_is_canceled;
} moal_drv_timer, *pmoal_drv_timer;

typedef struct {
	t_u8 dialog_token;
	t_u8 follow_up_dialog_token;
	t_u32 t1;
	t_u32 t4;
	t_u8 t1_err;
	t_u8 t4_err;
} __attribute__ ((packed)) moal_wnm_tm_msmt;

/** wlan_802_11_header */
typedef struct {
    /** Frame Control */
	t_u16 frm_ctl;
    /** Duration ID */
	t_u16 duration_id;
    /** Address1 */
	mlan_802_11_mac_addr addr1;
    /** Address2 */
	mlan_802_11_mac_addr addr2;
    /** Address3 */
	mlan_802_11_mac_addr addr3;
    /** Sequence Control */
	t_u16 seq_ctl;
    /** Address4 */
	mlan_802_11_mac_addr addr4;
} __attribute__ ((packed)) moal_wlan_802_11_header;

typedef struct {
	/** t2 time */
	t_u32 t2;
	/** t2 error */
	t_u8 t2_err;
	/** t3 time */
	t_u32 t3;
	/** t3 error */
	t_u8 t3_err;
	/** ingress time */
	t_u64 ingress_time;
} __attribute__ ((packed)) moal_timestamps;

typedef struct {
	t_u8 vendor_specific;
	t_u8 length;
	t_u8 data[0];
} __attribute__ ((packed)) moal_ptp_context;

/**
 *  @brief Timer handler
 *
 *  @param fcontext	Timer context
 *
 *  @return		N/A
 */
static inline void
woal_timer_handler(unsigned long fcontext)
{
	pmoal_drv_timer timer = (pmoal_drv_timer)fcontext;

	timer->timer_function(timer->function_context);

	if (timer->timer_is_periodic == MTRUE) {
		mod_timer(&timer->tl,
			  jiffies + ((timer->time_period * HZ) / 1000));
	} else {
		timer->timer_is_canceled = MTRUE;
		timer->time_period = 0;
	}
}

/**
 *  @brief Initialize timer
 *
 *  @param timer		Timer structure
 *  @param TimerFunction	Timer function
 *  @param FunctionContext	Timer function context
 *
 *  @return			N/A
 */
static inline void
woal_initialize_timer(pmoal_drv_timer timer,
		      void (*TimerFunction) (void *context),
		      void *FunctionContext)
{
	/* First, setup the timer to trigger the wlan_timer_handler proxy */
	init_timer(&timer->tl);
	timer->tl.function = woal_timer_handler;
	timer->tl.data = (t_ptr)timer;

	/* Then tell the proxy which function to call and what to pass it */
	timer->timer_function = TimerFunction;
	timer->function_context = FunctionContext;
	timer->timer_is_canceled = MTRUE;
	timer->time_period = 0;
	timer->timer_is_periodic = MFALSE;
}

/**
 *  @brief Modify timer
 *
 *  @param timer		Timer structure
 *  @param millisecondperiod	Time period in millisecond
 *
 *  @return			N/A
 */
static inline void
woal_mod_timer(pmoal_drv_timer timer, t_u32 millisecondperiod)
{
	timer->time_period = millisecondperiod;
	mod_timer(&timer->tl, jiffies + (millisecondperiod * HZ) / 1000);
	timer->timer_is_canceled = MFALSE;
}

/**
 *  @brief Cancel timer
 *
 *  @param timer	Timer structure
 *
 *  @return		N/A
 */
static inline void
woal_cancel_timer(moal_drv_timer *timer)
{
	if (timer->timer_is_periodic || in_atomic() || irqs_disabled())
		del_timer(&timer->tl);
	else
		del_timer_sync(&timer->tl);
	timer->timer_is_canceled = MTRUE;
	timer->time_period = 0;
}

#ifdef REASSOCIATION
/*
 * OS Thread Specific
 */

#include	<linux/kthread.h>

/** Kernel thread structure */
typedef struct _moal_thread {
    /** Task control structrue */
	struct task_struct *task;
    /** Pointer to wait_queue_head */
	wait_queue_head_t wait_q;
    /** PID */
	pid_t pid;
    /** Pointer to moal_handle */
	void *handle;
} moal_thread;

/**
 *  @brief Activate thread
 *
 *  @param thr			Thread structure
 *  @return			N/A
 */
static inline void
woal_activate_thread(moal_thread *thr)
{
    /** Initialize the wait queue */
	init_waitqueue_head(&thr->wait_q);

    /** Record the thread pid */
	thr->pid = current->pid;
}

/**
 *  @brief De-activate thread
 *
 *  @param thr			Thread structure
 *  @return			N/A
 */
static inline void
woal_deactivate_thread(moal_thread *thr)
{
	/* Reset the pid */
	thr->pid = 0;
}

/**
 *  @brief Create and run the thread
 *
 *  @param threadfunc		Thread function
 *  @param thr			Thread structure
 *  @param name			Thread name
 *  @return			N/A
 */
static inline void
woal_create_thread(int (*threadfunc) (void *), moal_thread *thr, char *name)
{
	/* Create and run the thread */
	thr->task = kthread_run(threadfunc, thr, "%s", name);
}
#endif /* REASSOCIATION */

/* The following macros are neccessary to retain compatibility
 * around the workqueue chenges happened in kernels >= 2.6.20:
 * - INIT_WORK changed to take 2 arguments and let the work function
 *   get its own data through the container_of macro
 * - delayed works have been split from normal works to save some
 *   memory usage in struct work_struct
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
/** Work_queue work initialization */
#define MLAN_INIT_WORK(_work, _fun)                 INIT_WORK(_work, ((void (*)(void *))_fun), _work)
/** Work_queue delayed work initialization */
#define MLAN_INIT_DELAYED_WORK(_work, _fun)         INIT_WORK(_work, ((void (*)(void *))_fun), _work)
/** Work_queue container parameter */
#define MLAN_DELAYED_CONTAINER_OF(_ptr, _type, _m)  container_of(_ptr, _type, _m)
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20) */
/** Work_queue work initialization */
#define MLAN_INIT_WORK                              INIT_WORK
/** Work_queue delayed work initialization */
#define MLAN_INIT_DELAYED_WORK                      INIT_DELAYED_WORK
/** Work_queue container parameter */
#define MLAN_DELAYED_CONTAINER_OF(_ptr, _type, _m)  container_of(_ptr, _type, _m.work)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20) */

/**
 *  @brief Schedule timeout
 *
 *  @param millisec	Timeout duration in milli second
 *
 *  @return		N/A
 */
static inline void
woal_sched_timeout(t_u32 millisec)
{
	set_current_state(TASK_INTERRUPTIBLE);

	schedule_timeout((millisec * HZ) / 1000);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#define IN6PTON_XDIGIT		0x00010000
#define IN6PTON_DIGIT		0x00020000
#define IN6PTON_COLON_MASK	0x00700000
#define IN6PTON_COLON_1		0x00100000	/* single : requested */
#define IN6PTON_COLON_2		0x00200000	/* second : requested */
#define IN6PTON_COLON_1_2	0x00400000	/* :: requested */
#define IN6PTON_DOT		0x00800000	/* . */
#define IN6PTON_DELIM		0x10000000
#define IN6PTON_NULL		0x20000000	/* first/tail */
#define IN6PTON_UNKNOWN		0x40000000

static inline int
xdigit2bin(char c, int delim)
{
	if (c == delim || c == '\0')
		return IN6PTON_DELIM;
	if (c == ':')
		return IN6PTON_COLON_MASK;
	if (c == '.')
		return IN6PTON_DOT;
	if (c >= '0' && c <= '9')
		return IN6PTON_XDIGIT | IN6PTON_DIGIT | (c - '0');
	if (c >= 'a' && c <= 'f')
		return IN6PTON_XDIGIT | (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return IN6PTON_XDIGIT | (c - 'A' + 10);
	if (delim == -1)
		return IN6PTON_DELIM;
	return IN6PTON_UNKNOWN;
}

static inline int
in4_pton(const char *src, int srclen, u8 *dst, int delim, const char **end)
{
	const char *s;
	u8 *d;
	u8 dbuf[4];
	int ret = 0;
	int i;
	int w = 0;

	if (srclen < 0)
		srclen = strlen(src);
	s = src;
	d = dbuf;
	i = 0;
	while (1) {
		int c;
		c = xdigit2bin(srclen > 0 ? *s : '\0', delim);
		if (!
		    (c &
		     (IN6PTON_DIGIT | IN6PTON_DOT | IN6PTON_DELIM |
		      IN6PTON_COLON_MASK))) {
			goto out;
		}
		if (c & (IN6PTON_DOT | IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
			if (w == 0)
				goto out;
			*d++ = w & 0xff;
			w = 0;
			i++;
			if (c & (IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
				if (i != 4)
					goto out;
				break;
			}
			goto cont;
		}
		w = (w * 10) + c;
		if ((w & 0xffff) > 255)
			goto out;
cont:
		if (i >= 4)
			goto out;
		s++;
		srclen--;
	}
	ret = 1;
	memcpy(dst, dbuf, sizeof(dbuf));
out:
	if (end)
		*end = s;
	return ret;
}
#endif /* < 2.6.19 */

#ifndef __ATTRIB_ALIGN__
#define __ATTRIB_ALIGN__ __attribute__((aligned(4)))
#endif

#ifndef __ATTRIB_PACK__
#define __ATTRIB_PACK__ __attribute__ ((packed))
#endif

/** Get module */
#define MODULE_GET	try_module_get(THIS_MODULE)
/** Put module */
#define MODULE_PUT	module_put(THIS_MODULE)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
/** Initialize semaphore */
#define MOAL_INIT_SEMAPHORE(x)      init_MUTEX(x)
/** Initialize semaphore */
#define MOAL_INIT_SEMAPHORE_LOCKED(x)   init_MUTEX_LOCKED(x)
#else
/** Initialize semaphore */
#define MOAL_INIT_SEMAPHORE(x)      sema_init(x, 1)
/** Initialize semaphore */
#define MOAL_INIT_SEMAPHORE_LOCKED(x)   sema_init(x, 0)
#endif

/** Acquire semaphore and with blocking */
#define MOAL_ACQ_SEMAPHORE_BLOCK(x)	down_interruptible(x)
/** Acquire semaphore without blocking */
#define MOAL_ACQ_SEMAPHORE_NOBLOCK(x)	down_trylock(x)
/** Release semaphore */
#define MOAL_REL_SEMAPHORE(x)       up(x)

/** Request FW timeout in second */
#define REQUEST_FW_TIMEOUT		30

#if defined(SYSKT)
/** Max loop count (* 100ms) for waiting device ready at init time */
#define MAX_WAIT_DEVICE_READY_COUNT	50
#endif

/** Default watchdog timeout */
#define MRVDRV_DEFAULT_WATCHDOG_TIMEOUT (10 * HZ)

#ifdef UAP_SUPPORT
/** Default watchdog timeout
    Increase the value to avoid kernel Tx timeout message in case
    station in PS mode or left.
    The default value of PS station ageout timer is 40 seconds.
    Hence, the watchdog timer is set to a value higher than it.
*/
#define MRVDRV_DEFAULT_UAP_WATCHDOG_TIMEOUT (41 * HZ)
#endif

/* IOCTL Timeout */
#define MOAL_IOCTL_TIMEOUT                    (20 * HZ)

#ifdef ANDROID_KERNEL
/** Wake lock timeout in msec */
#define WAKE_LOCK_TIMEOUT 3000
/** Roaming Wake lock timeout in msec */
#define ROAMING_WAKE_LOCK_TIMEOUT 10000
#endif

/** Threshold value of number of times the Tx timeout happened */
#define NUM_TX_TIMEOUT_THRESHOLD      5

/** Custom event : DRIVER HANG */
#define CUS_EVT_DRIVER_HANG	            "EVENT=DRIVER_HANG"

/** TDLS connected event */
#define CUS_EVT_TDLS_CONNECTED           "EVENT=TDLS_CONNECTED"
/** TDLS tear down event */
#define CUS_EVT_TDLS_TEARDOWN            "EVENT=TDLS_TEARDOWN"
/** wmm info */
#define WMM_TYPE_INFO                     0
/** wmm parameter */
#define WMM_TYPE_PARAMETER                1

/** AP connected event */
#define CUS_EVT_AP_CONNECTED           "EVENT=AP_CONNECTED"

/** Custom event : BW changed */
#define CUS_EVT_BW_CHANGED		"EVENT=BW_CHANGED"
/** Custom event : OBSS scan parameter */
#define CUS_EVT_OBSS_SCAN_PARAM		"EVENT=OBSS_SCAN_PARAM"

/** Custom event : AdHoc link sensed */
#define CUS_EVT_ADHOC_LINK_SENSED	"EVENT=ADHOC_LINK_SENSED"
/** Custom event : AdHoc link lost */
#define CUS_EVT_ADHOC_LINK_LOST		"EVENT=ADHOC_LINK_LOST"
/** Custom event : MIC failure, unicast */
#define CUS_EVT_MLME_MIC_ERR_UNI	"MLME-MICHAELMICFAILURE.indication unicast"
/** Custom event : MIC failure, multicast */
#define CUS_EVT_MLME_MIC_ERR_MUL	"MLME-MICHAELMICFAILURE.indication multicast"
/** Custom event : Beacon RSSI low */
#define CUS_EVT_BEACON_RSSI_LOW		"EVENT=BEACON_RSSI_LOW"
/** Custom event : Beacon SNR low */
#define CUS_EVT_BEACON_SNR_LOW		"EVENT=BEACON_SNR_LOW"
/** Custom event : Beacon RSSI high */
#define CUS_EVT_BEACON_RSSI_HIGH	"EVENT=BEACON_RSSI_HIGH"
/** Custom event : Beacon SNR high */
#define CUS_EVT_BEACON_SNR_HIGH		"EVENT=BEACON_SNR_HIGH"
/** Custom event : Max fail */
#define CUS_EVT_MAX_FAIL		"EVENT=MAX_FAIL"
/** Custom event : Data RSSI low */
#define CUS_EVT_DATA_RSSI_LOW		"EVENT=DATA_RSSI_LOW"
/** Custom event : Data SNR low */
#define CUS_EVT_DATA_SNR_LOW		"EVENT=DATA_SNR_LOW"
/** Custom event : Data RSSI high */
#define CUS_EVT_DATA_RSSI_HIGH		"EVENT=DATA_RSSI_HIGH"
/** Custom event : Data SNR high */
#define CUS_EVT_DATA_SNR_HIGH		"EVENT=DATA_SNR_HIGH"
/** Custom event : Link Quality */
#define CUS_EVT_LINK_QUALITY		"EVENT=LINK_QUALITY"
/** Custom event : Port Release */
#define CUS_EVT_PORT_RELEASE		"EVENT=PORT_RELEASE"
/** Custom event : Pre-Beacon Lost */
#define CUS_EVT_PRE_BEACON_LOST		"EVENT=PRE_BEACON_LOST"

/** Custom event : Deep Sleep awake */
#define CUS_EVT_DEEP_SLEEP_AWAKE	"EVENT=DS_AWAKE"

#define CUS_EVT_GET_CORRELATED_TIME     "EVENT=CORRELATED-TIME"
#define CUS_EVT_TIMING_MSMT_CONFIRM     "EVENT=TIMING-MSMT-CONFIRM"
#define CUS_EVT_TM_FRAME_INDICATION     "EVENT=TIMING-MSMT-FRAME"

/** Custom event : Host Sleep activated */
#define CUS_EVT_HS_ACTIVATED		"HS_ACTIVATED"
/** Custom event : Host Sleep deactivated */
#define CUS_EVT_HS_DEACTIVATED		"HS_DEACTIVATED"
/** Custom event : Host Sleep wakeup */
#define CUS_EVT_HS_WAKEUP		"HS_WAKEUP"

/** Wakeup Reason */
typedef enum {
	NO_HSWAKEUP_REASON = 0,	//0.unknown
	BCAST_DATA_MATCHED,	// 1. Broadcast data matched
	MCAST_DATA_MATCHED,	// 2. Multicast data matched
	UCAST_DATA_MATCHED,	// 3. Unicast data matched
	MASKTABLE_EVENT_MATCHED,	// 4. Maskable event matched
	NON_MASKABLE_EVENT_MATCHED,	// 5. Non-maskable event matched
	NON_MASKABLE_CONDITION_MATCHED,	// 6. Non-maskable condition matched (EAPoL rekey)
	MAGIC_PATTERN_MATCHED,	// 7. Magic pattern matched
	CONTROL_FRAME_MATCHED,	// 8. Control frame matched
	MANAGEMENT_FRAME_MATCHED,	// 9. Management frame matched
	GTK_REKEY_FAILURE,	//10. GTK rekey failure
	RESERVED		// Others: reserved
} HSWakeupReason_t;

/** Custom event : WEP ICV error */
#define CUS_EVT_WEP_ICV_ERR		"EVENT=WEP_ICV_ERR"

/** Custom event : Channel Switch Announcment */
#define CUS_EVT_CHANNEL_SWITCH_ANN	"EVENT=CHANNEL_SWITCH_ANN"

/** Custom indiciation message sent to the application layer for WMM changes */
#define WMM_CONFIG_CHANGE_INDICATION  "WMM_CONFIG_CHANGE.indication"

#ifdef UAP_SUPPORT
/** Custom event : STA connected */
#define CUS_EVT_STA_CONNECTED           "EVENT=STA_CONNECTED"
/** Custom event : STA disconnected */
#define CUS_EVT_STA_DISCONNECTED        "EVENT=STA_DISCONNECTED"
#endif

/** 10 seconds */
#define MOAL_TIMER_10S                10000
/** 5 seconds */
#define MOAL_TIMER_5S                 5000
/** 1 second */
#define MOAL_TIMER_1S                 1000
/** 1 milisecond */
#define MOAL_TIMER_1MS                1

/** passive scan time */
#define PASSIVE_SCAN_CHAN_TIME       110
/** active scan time */
#define ACTIVE_SCAN_CHAN_TIME        110
/** specific scan time */
#define SPECIFIC_SCAN_CHAN_TIME      110
/** passive scan time */
#define INIT_PASSIVE_SCAN_CHAN_TIME  80
/** active scan time */
#define INIT_ACTIVE_SCAN_CHAN_TIME   80
/** specific scan time */
#define INIT_SPECIFIC_SCAN_CHAN_TIME 80
/** specific scan time after connected */
#define MIN_SPECIFIC_SCAN_CHAN_TIME   40

/** Default value of re-assoc timer */
#define REASSOC_TIMER_DEFAULT         500

/** Netlink protocol number */
#define NETLINK_MARVELL     (MAX_LINKS - 1)
/** Netlink maximum payload size */
#define NL_MAX_PAYLOAD      1024
/** Netlink multicast group number */
#define NL_MULTICAST_GROUP  1

#define MAX_RX_PENDING_THRHLD	50

/** high rx pending packets */
#define HIGH_RX_PENDING         100
/** low rx pending packets */
#define LOW_RX_PENDING          80

/** MAX Tx Pending count */
#define MAX_TX_PENDING      100

/** LOW Tx Pending count */
#define LOW_TX_PENDING      80

/** Offset for subcommand */
#define SUBCMD_OFFSET       4

/** default scan channel gap  */
#define DEF_SCAN_CHAN_GAP   50
/** default scan time per channel in miracast mode */
#define DEF_MIRACAST_SCAN_TIME   20

/** Macro to extract the TOS field from a skb */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
#define SKB_TOS(skb) (ip_hdr(skb)->tos)
#else
#define SKB_TOS(skb) (skb->nh.iph->tos)
#endif
#define SKB_TIDV6(skb)  (ipv6_get_dsfield(ipv6_hdr(skb)))
#define IS_SKB_MAGIC_VLAN(skb) (skb->priority >= 256 && skb->priority <= 263)
#define GET_VLAN_PRIO(skb) (skb->priority - 256)

/** Offset for TOS field in the IP header */
#define IPTOS_OFFSET 5

/** Offset for DSCP in the tos field */
#define DSCP_OFFSET 2

/** max retry count for wait_event_interupptible_xx while loop */
#define MAX_RETRY_CNT 100
/** wait_queue structure */
typedef struct _wait_queue {
	/** wait_queue_head */
	wait_queue_head_t wait;
	/** Wait condition */
	BOOLEAN condition;
	/** Start time */
	long start_time;
	/** Status from MLAN */
	mlan_status status;
    /** flag for wait_timeout */
	t_u8 wait_timeout;
    /** retry count */
	t_u8 retry;
} wait_queue, *pwait_queue;

/** Auto Rate */
#define AUTO_RATE 0xFF

#define STA_WEXT_MASK        MBIT(0)
#define UAP_WEXT_MASK        MBIT(1)
#define STA_CFG80211_MASK    MBIT(2)
#define UAP_CFG80211_MASK    MBIT(3)
#ifdef STA_CFG80211
#ifdef STA_SUPPORT
/** Is STA CFG80211 enabled in module param */
#define IS_STA_CFG80211(x)          (x & STA_CFG80211_MASK)
#endif
#endif
#ifdef UAP_CFG80211
#ifdef UAP_SUPPORT
/** Is UAP CFG80211 enabled in module param */
#define IS_UAP_CFG80211(x)          (x & UAP_CFG80211_MASK)
#endif
#endif
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
/** Is UAP or STA CFG80211 enabled in module param */
#define IS_STA_OR_UAP_CFG80211(x)   (x & (STA_CFG80211_MASK | UAP_CFG80211_MASK))
#endif

#ifdef STA_WEXT
/** Is STA WEXT enabled in module param */
#define IS_STA_WEXT(x)              (x & STA_WEXT_MASK)
#endif /* STA_WEXT */
#ifdef UAP_WEXT
/** Is UAP WEXT enabled in module param */
#define IS_UAP_WEXT(x)              (x & UAP_WEXT_MASK)
#endif /* UAP_WEXT */
#if defined(STA_WEXT) || defined(UAP_WEXT)
/** Is UAP or STA WEXT enabled in module param */
#define IS_STA_OR_UAP_WEXT(x)       (x & (STA_WEXT_MASK | UAP_WEXT_MASK))
#endif

#ifdef STA_SUPPORT
/** Driver mode STA bit */
#define DRV_MODE_STA       MBIT(0)
/** Maximum STA BSS */
#define MAX_STA_BSS        1
/** Default STA BSS */
#define DEF_STA_BSS        1
#endif
#ifdef UAP_SUPPORT
/** Driver mode uAP bit */
#define DRV_MODE_UAP       MBIT(1)
/** Maximum uAP BSS */
#define MAX_UAP_BSS        2
/** Default uAP BSS */
#define DEF_UAP_BSS        1
#endif
#if defined(WIFI_DIRECT_SUPPORT)
/** Driver mode WIFIDIRECT bit */
#define DRV_MODE_WIFIDIRECT       MBIT(2)
/** Maximum WIFIDIRECT BSS */
#define MAX_WIFIDIRECT_BSS        2
/** Default WIFIDIRECT BSS */
#define DEF_WIFIDIRECT_BSS        1
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#define DEF_VIRTUAL_BSS			  0
#endif
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
/** Driver mode NAN bit */
#define DRV_MODE_NAN              MBIT(4)
/** Maximum NAN BSS */
#define MAX_NAN_BSS               1
/** Default NAN BSS */
#define DEF_NAN_BSS               1

#define DRV_MODE_WLAN            (MBIT(0)|MBIT(1)|MBIT(2)|MBIT(3)|MBIT(4))

/**
 * the maximum number of adapter supported
 **/
#define MAX_MLAN_ADAPTER    2

typedef struct _moal_drv_mode {
    /** driver mode */
	t_u16 drv_mode;
    /** total number of interfaces */
	t_u16 intf_num;
    /** attribute of bss */
	mlan_bss_attr *bss_attr;
    /** name of firmware image */
	char *fw_name;
} moal_drv_mode;

#ifdef PROC_DEBUG
/** Debug data */
struct debug_data {
    /** Name */
	char name[32];
    /** Size */
	t_u32 size;
    /** Address */
	t_ptr addr;
};

/** Private debug data */
struct debug_data_priv {
    /** moal_private handle */
	moal_private *priv;
    /** Debug items */
	struct debug_data *items;
    /** numbre of item */
	int num_of_items;
};
#endif

/** Maximum IP address buffer length */
#define IPADDR_MAX_BUF          20
/** IP address operation: Remove */
#define IPADDR_OP_REMOVE        0

#define DROP_TCP_ACK        1
#define HOLD_TCP_ACK        2
struct tcp_sess {
	struct list_head link;
    /** tcp session info */
	t_u32 src_ip_addr;
	t_u32 dst_ip_addr;
	t_u16 src_tcp_port;
	t_u16 dst_tcp_port;
    /** tx ack packet info */
	t_u32 ack_seq;
	/** tcp ack buffer */
	void *ack_skb;
	/** priv structure */
	void *priv;
	/** pmbuf */
	void *pmbuf;
    /** timer for ack */
	moal_drv_timer ack_timer __ATTRIB_ALIGN__;
    /** timer is set */
	BOOLEAN is_timer_set;
};

struct tx_status_info {
	struct list_head link;
    /** cookie */
	t_u64 tx_cookie;
    /** seq_num */
	t_u8 tx_seq_num;
	/**          skb */
	void *tx_skb;
};

#define MAX_NUM_ETHER_TYPE 8
typedef struct {
    /** number of protocols in protocol array*/
	t_u8 protocol_num;
    /** protocols supported */
	t_u16 protocols[MAX_NUM_ETHER_TYPE];
} __ATTRIB_PACK__ dot11_protocol;
typedef struct {
    /** Data rate in unit of 0.5Mbps */
	t_u16 datarate;
    /** Channel number to transmit the frame */
	t_u8 channel;
    /** Bandwidth to transmit the frame */
	t_u8 bw;
    /** Power to be used for transmission */
	t_u8 power;
    /** Priority of the packet to be transmitted */
	t_u8 priority;
    /** retry time of tx transmission*/
	t_u8 retry_limit;
    /** Reserved fields*/
	t_u8 reserved[1];
} __ATTRIB_PACK__ dot11_txcontrol;

typedef struct {
   /** Data rate of received paccket*/
	t_u16 datarate;
   /** Channel on which packet was received*/
	t_u8 channel;
   /** Rx antenna*/
	t_u8 antenna;
   /** RSSI */
	t_u8 rssi;
   /** Reserved */
	t_u8 reserved[3];
} __ATTRIB_PACK__ dot11_rxcontrol;

#define OKC_WAIT_TARGET_PMKSA_TIMEOUT (4 * HZ / 1000)
#define PMKID_LEN 16
struct pmksa_entry {
	struct list_head link;
	u8 bssid[ETH_ALEN];
	u8 pmkid[PMKID_LEN];
};

/** default rssi low threshold */
#define TDLS_RSSI_LOW_THRESHOLD 55
/** default rssi high threshold */
#define TDLS_RSSI_HIGH_THRESHOLD 50
/** TDLS idle time */
#define TDLS_IDLE_TIME			(10*HZ)
/** TDLS max failure count */
#define TDLS_MAX_FAILURE_COUNT	 4
/** TDLS tear down reason */
#define TDLS_TEARN_DOWN_REASON_UNSPECIFIC	26

/** TDLS status */
typedef enum _tdlsStatus_e {
	TDLS_NOT_SETUP = 0,
	TDLS_SETUP_INPROGRESS,
	TDLS_SETUP_COMPLETE,
	TDLS_SETUP_FAILURE,
	TDLS_TEAR_DOWN,
	TDLS_SWITCHING_CHANNEL,
	TDLS_IN_BASE_CHANNEL,
	TDLS_IN_OFF_CHANNEL,
} tdlsStatus_e;

/** tdls peer_info */
struct tdls_peer {
	struct list_head link;
	/** MAC address information */
	t_u8 peer_addr[ETH_ALEN];
	/** rssi */
	int rssi;
    /** jiffies with rssi */
	long rssi_jiffies;
    /** link status */
	tdlsStatus_e link_status;
    /** num of set up failure */
	t_u8 num_failure;
};

/** Number of samples in histogram (/proc/mwlan/mlan0/histogram).*/
#define HIST_MAX_SAMPLES   1048576
#define RX_RATE_MAX			76

/** SRN MAX  */
#define SNR_MAX				256
/** NOISE FLR MAX  */
#define NOISE_FLR_MAX			256
/** SIG STRENTGH MAX */
#define SIG_STRENGTH_MAX		256
/** historgram data */
typedef struct _hgm_data {
    /** snr */
	atomic_t snr[SNR_MAX];
    /** noise flr */
	atomic_t noise_flr[NOISE_FLR_MAX];
    /** sig_str */
	atomic_t sig_str[SIG_STRENGTH_MAX];
    /** num sample */
	atomic_t num_samples;
    /** rx rate */
	atomic_t rx_rate[0];
} hgm_data;

/** max antenna number */
#define MAX_ANTENNA_NUM			1

/* wlan_hist_proc_data */
typedef struct _wlan_hist_proc_data {
    /** antenna */
	u8 ant_idx;
	/** Private structure */
	struct _moal_private *priv;
} wlan_hist_proc_data;

/** Private structure for MOAL */
struct _moal_private {
	/** Handle structure */
	moal_handle *phandle;
	/** Tx timeout count */
	t_u32 num_tx_timeout;
	/** BSS index */
	t_u8 bss_index;
	/** BSS type */
	t_u8 bss_type;
	/** BSS role */
	t_u8 bss_role;
	/** bss virtual flag */
	t_u8 bss_virtual;
	/** MAC address information */
	t_u8 current_addr[ETH_ALEN];
	/** Media connection status */
	BOOLEAN media_connected;
	/** Statistics of tcp ack tx dropped */
	t_u32 tcp_ack_drop_cnt;
	/** Statistics of tcp ack tx in total from kernel */
	t_u32 tcp_ack_cnt;
#ifdef UAP_SUPPORT
	/** uAP started or not */
	BOOLEAN bss_started;
    /** host based uap flag */
	BOOLEAN uap_host_based;
	/** uAP skip CAC*/
	BOOLEAN skip_cac;
	/** tx block flag */
	BOOLEAN uap_tx_blocked;
#if defined(DFS_TESTING_SUPPORT)
    /** user cac period */
	t_u32 user_cac_period_msec;
    /** channel under nop */
	BOOLEAN chan_under_nop;
#endif
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	/** current working channel */
	struct cfg80211_chan_def chan;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	/** switch channel */
	struct cfg80211_chan_def csa_chan;
	/** beacon after channel switch */
	struct cfg80211_beacon_data beacon_after;
	/** CSA work queue */
	struct workqueue_struct *csa_workqueue;
	/** csa work */
	struct delayed_work csa_work;
#endif
#endif
#endif
    /** IP addr type */
	t_u32 ip_addr_type;
    /** IP addr */
	t_u8 ip_addr[IPADDR_LEN];
#ifdef STA_SUPPORT
	/** scan type */
	t_u8 scan_type;
	/** extended capabilities */
	ExtCap_t extended_capabilities;
	/** bg_scan_start */
	t_u8 bg_scan_start;
    /** bg_scan reported */
	t_u8 bg_scan_reported;
    /** bg_scan config */
	wlan_bgscan_cfg scan_cfg;
	/** sched scaning flag */
	t_u8 sched_scanning;
#ifdef STA_CFG80211
    /** roaming enabled flag */
	t_u8 roaming_enabled;
	/** rssi low threshold */
	int rssi_low;
    /** channel for connect */
	struct ieee80211_channel conn_chan;
    /** bssid for connect */
	t_u8 conn_bssid[ETH_ALEN];
    /** ssid for connect */
	t_u8 conn_ssid[MLAN_MAX_SSID_LENGTH];
	/** key data */
	t_u8 conn_wep_key[MAX_WEP_KEY_SIZE];
    /** connection param */
	struct cfg80211_connect_params sme_current;
    /** roaming required flag */
	t_u8 roaming_required;
#endif
	t_u8 wait_target_ap_pmkid;
	wait_queue_head_t okc_wait_q __ATTRIB_ALIGN__;
	struct list_head pmksa_cache_list;
	spinlock_t pmksa_list_lock;
	struct pmksa_entry *target_ap_pmksa;
	t_u8 okc_ie_len;
	t_u8 *okc_roaming_ie;
#endif
	/** Net device pointer */
	struct net_device *netdev;
	/** Net device statistics structure */
	struct net_device_stats stats;
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	/** Wireless device pointer */
	struct wireless_dev *wdev;
	/** Wireless device */
	struct wireless_dev w_dev;
	/** Net device pointer */
	struct net_device *pa_netdev;
	/** channel parameter for UAP/GO */
	t_u16 channel;
#ifdef UAP_SUPPORT
    /** wep key */
	wep_key uap_wep_key[4];
	/** cipher */
	t_u32 cipher;
#endif
	/** pmk saved flag */
	t_u8 pmk_saved;
	/** pmk */
	mlan_pmk_t pmk;
	/** beacon ie index */
	t_u16 beacon_index;
	/** proberesp ie index */
	t_u16 proberesp_index;
	/** proberesp_p2p_index */
	t_u16 proberesp_p2p_index;
	/** assocresp ie index */
	t_u16 assocresp_index;
	/** assocresp qos map ie index */
	t_u16 assocresp_qos_map_index;
	/** probereq index for mgmt ie */
	t_u16 probereq_index;
	/** mgmt_subtype_mask */
	t_u32 mgmt_subtype_mask;
	/** beacon wps index for mgmt ie */
	t_u16 beacon_wps_index;
	/** beacon/proberesp vendor ie index */
	t_u16 proberesp_vendor_index;
#endif
#ifdef STA_CFG80211
#ifdef STA_SUPPORT
	/** CFG80211 association description */
	t_u8 cfg_bssid[ETH_ALEN];
	/** Disconnect request from CFG80211 */
	bool cfg_disconnect;
    /** connect request from CFG80211 */
	bool cfg_connect;
    /** lock for cfg connect */
	spinlock_t connect_lock;
	/** assoc status */
	t_u32 assoc_status;
	/** rssi_threshold */
	s32 cqm_rssi_thold;
	/** rssi_high_threshold */
	s32 cqm_rssi_high_thold;
	/** rssi hysteresis */
	u32 cqm_rssi_hyst;
	/** last rssi_low */
	u8 last_rssi_low;
	/** last rssi_high */
	u8 last_rssi_high;
	/** mrvl rssi threshold */
	u8 mrvl_rssi_low;
	/** last event */
	u32 last_event;
	/** fake scan flag */
	u8 fake_scan_complete;
	/**ft ie*/
	t_u8 ft_ie[MAX_IE_SIZE];
    /**ft ie len*/
	t_u8 ft_ie_len;
    /**mobility domain value*/
	t_u16 ft_md;
    /**ft capability*/
	t_u8 ft_cap;
    /**set true during ft connection*/
	t_bool ft_pre_connect;
    /**ft roaming triggered by driver or not*/
	t_bool ft_roaming_triggered_by_driver;
    /**target ap mac address for Fast Transition*/
	t_u8 target_ap_bssid[ETH_ALEN];
    /** IOCTL wait queue for FT*/
	wait_queue_head_t ft_wait_q __ATTRIB_ALIGN__;
	/** ft wait condition */
	t_bool ft_wait_condition;
#endif				/* STA_SUPPORT */
#endif				/* STA_CFG80211 */
#ifdef CONFIG_PROC_FS
	/** Proc entry */
	struct proc_dir_entry *proc_entry;
	/** Proc entry name */
	char proc_entry_name[IFNAMSIZ];
    /** proc entry for hist */
	struct proc_dir_entry *hist_entry;
	/** ant_hist_proc_data */
	wlan_hist_proc_data hist_proc[MAX_ANTENNA_NUM];
#endif				/* CONFIG_PROC_FS */
#ifdef STA_SUPPORT
	/** Nickname */
	t_u8 nick_name[16];
	/** AdHoc link sensed flag */
	BOOLEAN is_adhoc_link_sensed;
	/** Current WEP key index */
	t_u16 current_key_index;
#ifdef REASSOCIATION
	mlan_ssid_bssid prev_ssid_bssid;
	/** Re-association required */
	BOOLEAN reassoc_required;
	/** Flag of re-association on/off */
	BOOLEAN reassoc_on;
	/** Set asynced essid flag */
	BOOLEAN set_asynced_essid_flag;
#endif				/* REASSOCIATION */
	/** Report scan result */
	t_u8 report_scan_result;
	/** wpa_version */
	t_u8 wpa_version;
	/** key mgmt */
	t_u8 key_mgmt;
	/** rx_filter */
	t_u8 rx_filter;
#endif				/* STA_SUPPORT */
	/** Rate index */
	t_u16 rate_index;
#if defined(STA_WEXT) || defined(UAP_WEXT)
	/** IW statistics */
	struct iw_statistics w_stats;
#endif
#ifdef UAP_WEXT
    /** Pairwise Cipher used for WPA/WPA2 mode */
	t_u16 pairwise_cipher;
    /** Group Cipher */
	t_u16 group_cipher;
    /** Protocol stored during uap wext configuratoin */
	t_u16 uap_protocol;
    /** Key Mgmt whether PSK or 1x */
	t_u16 uap_key_mgmt;
    /** Beacon IE length from hostapd */
	t_u16 bcn_ie_len;
    /** Beacon IE buffer from hostapd */
	t_u8 bcn_ie_buf[MAX_IE_SIZE];
#endif

    /** dscp mapping */
	t_u8 dscp_map[64];
#ifdef PROC_DEBUG
    /** MLAN debug info */
	struct debug_data_priv items_priv;
#endif

    /** tcp session queue */
	struct list_head tcp_sess_queue;
    /** TCP Ack enhance flag */
	t_u8 enable_tcp_ack_enh;
    /** TCP session spin lock */
	spinlock_t tcp_sess_lock;
    /** tcp list */
	struct list_head tdls_list;
	/** tdls spin lock */
	spinlock_t tdls_lock;
	/** auto tdls  flag */
	t_u8 enable_auto_tdls;
    /** check tx packet for tdls peer */
	t_u8 tdls_check_tx;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	atomic_t wmm_tx_pending[4];
#endif
    /** per interface extra headroom */
	t_u16 extra_tx_head_len;
    /** TX status spin lock */
	spinlock_t tx_stat_lock;
    /** tx_seq_num */
	t_u8 tx_seq_num;
    /** tx status queue */
	struct list_head tx_stat_queue;

    /** rx hgm data */
	hgm_data *hist_data[3];
	BOOLEAN assoc_with_mac;
	t_u8 gtk_data_ready;
	mlan_ds_misc_gtk_rekey_data gtk_rekey_data;
	dot11_protocol tx_protocols;
	dot11_protocol rx_protocols;
};

/** channel_field.flags */
#define CHANNEL_FLAGS_TURBO 0x0010
#define CHANNEL_FLAGS_CCK   0x0020
#define CHANNEL_FLAGS_OFDM  0x0040
#define CHANNEL_FLAGS_2GHZ  0x0080
#define CHANNEL_FLAGS_5GHZ  0x0100
#define CHANNEL_FLAGS_ONLY_PASSIVSCAN_ALLOW  0x0200
#define CHANNEL_FLAGS_DYNAMIC_CCK_OFDM  0x0400
#define CHANNEL_FLAGS_GFSK  0x0800
struct channel_field {
	t_u16 frequency;
	t_u16 flags;
} __packed;

/** mcs_field.known */
#define MCS_KNOWN_BANDWIDTH   0x01
#define MCS_KNOWN_MCS_INDEX_KNOWN  0x02
#define MCS_KNOWN_GUARD_INTERVAL   0x04
#define MCS_KNOWN_HT_FORMAT   0x08
#define MCS_KNOWN_FEC_TYPE    0x10
#define MCS_KNOWN_STBC_KNOWN  0x20
#define MCS_KNOWN_NESS_KNOWN  0x40
#define MCS_KNOWN_NESS_DATA   0x80
/** bandwidth */
#define RX_BW_20   0
#define RX_BW_40   1
#define RX_BW_20L  2
#define RX_BW_20U  3
/** mcs_field.flags
The flags field is any combination of the following:
0x03    bandwidth - 0: 20, 1: 40, 2: 20L, 3: 20U
0x04    guard interval - 0: long GI, 1: short GI
0x08    HT format - 0: mixed, 1: greenfield
0x10    FEC type - 0: BCC, 1: LDPC
0x60    Number of STBC streams
0x80    Ness - bit 0 (LSB) of Number of extension spatial streams */
struct mcs_field {
	t_u8 known;
	t_u8 flags;
	t_u8 mcs;
} __packed;

/** radiotap_body.flags */
#define RADIOTAP_FLAGS_DURING_CFG  0x01
#define RADIOTAP_FLAGS_SHORT_PREAMBLE  0x02
#define RADIOTAP_FLAGS_WEP_ENCRYPTION  0x04
#define RADIOTAP_FLAGS_WITH_FRAGMENT   0x08
#define RADIOTAP_FLAGS_INCLUDE_FCS  0x10
#define RADIOTAP_FLAGS_PAD_BTW_HEADER_PAYLOAD  0x20
#define RADIOTAP_FLAGS_FAILED_FCS_CHECK  0x40
#define RADIOTAP_FLAGS_USE_SGI_HT  0x80
struct radiotap_body {
	t_u64 timestamp;
	t_u8 flags;
	t_u8 rate;
	struct channel_field channel;
	t_s8 antenna_signal;
	t_s8 antenna_noise;
	t_u8 antenna;
	struct mcs_field mcs;
} __packed;

struct radiotap_header {
	struct ieee80211_radiotap_header hdr;
	struct radiotap_body body;
} __packed;

/** Roam offload config parameters */
typedef struct woal_priv_fw_roam_offload_cfg {
	/* User set passphrase */
	t_u8 userset_passphrase;
	/* BSSID for fw roaming/auto_reconnect */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/* Retry_count for fw roaming/auto_reconnect */
	t_u8 retry_count;
	/* Condition to trigger roaming
	 * Bit0 : RSSI low trigger
	 * Bit1 : Pre-beacon lost trigger
	 * Bit2 : Link Lost trigger
	 * Bit3 : Deauth by ext-AP trigger
	 * Bit4 ~ Bit15 : Reserved
	 * value 0 : no trigger
	 * value 0xff : invalid
	 */
	t_u16 trigger_condition;
	/* SSID List(White list) */
	mlan_ds_misc_ssid_list ssid_list;
	/* Black list(BSSID list) */
	mlan_ds_misc_roam_offload_aplist black_list;

	/* RSSI paramters set flag */
	t_u8 rssi_param_set_flag;
	/* MAX_RSSI for fw roaming */
	t_u8 max_rssi;
	/*  MIN_RSSI for fw roaming */
	t_u8 min_rssi;
	/*  Step_RSSI for fw roaming */
	t_u8 step_rssi;

	/* BAND and RSSI_HYSTERESIS set flag */
	t_u8 band_rssi_flag;
	mlan_ds_misc_band_rssi band_rssi;

	/* BGSCAN params set flag */
	t_u8 bgscan_set_flag;
	mlan_ds_misc_bgscan_cfg bgscan_cfg;

	/* EES mode params set flag */
	t_u8 ees_param_set_flag;
	mlan_ds_misc_ees_cfg ees_cfg;

	/* Beacon miss threshold */
	t_u8 bcn_miss_threshold;

	/* Beacon miss threshold */
	t_u8 pre_bcn_miss_threshold;

	/* scan repeat count */
	t_u16 repeat_count;
} woal_roam_offload_cfg;

int woal_set_clear_pmk(moal_private *priv, t_u8 action);
mlan_status woal_config_fw_roaming(moal_private *priv, t_u8 cfg_mode,
				   woal_roam_offload_cfg * roam_offload_cfg);
int woal_enable_fw_roaming(moal_private *priv, int data);

#define GTK_REKEY_OFFLOAD_DISABLE                    0
#define GTK_REKEY_OFFLOAD_ENABLE                     1
#define GTK_REKEY_OFFLOAD_SUSPEND                    2

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/** Monitor Band Channel Config */
typedef struct _netmon_band_chan_cfg {
	t_u32 band;
	t_u32 channel;
	t_u32 chan_bandwidth;
} netmon_band_chan_cfg;
#endif

typedef struct _monitor_iface {
	/* The priv data of interface on which the monitor iface is based */
	moal_private *priv;
	struct wireless_dev wdev;
	/** 0 - Disabled
	    * 1 - Channel Specified sniffer mode
	    * 2 - In-Channel sniffer mode
	*/
	int sniffer_mode;
	int radiotap_enabled;
	/* The net_device on which the monitor iface is based. */
	struct net_device *base_ndev;
	struct net_device *mon_ndev;
	char ifname[IFNAMSIZ];
	int flag;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	struct cfg80211_chan_def chandef;
	/** Netmon Band Channel Config */
	netmon_band_chan_cfg band_chan_cfg;
#endif
	/** Monitor device statistics structure */
	struct net_device_stats stats;
} monitor_iface;

/** Handle data structure for MOAL */
struct _moal_handle {
	/** MLAN adapter structure */
	t_void *pmlan_adapter;
	/** Private pointer */
	moal_private *priv[MLAN_MAX_BSS_NUM];
	/** Priv number */
	t_u8 priv_num;
	/** Bss attr */
	moal_drv_mode drv_mode;

	/** Monitor interface */
	monitor_iface *mon_if;

	/** set mac address flag */
	t_u8 set_mac_addr;
	/** MAC address */
	t_u8 mac_addr[ETH_ALEN];
#ifdef CONFIG_PROC_FS
	/** Proc top level directory entry */
	struct proc_dir_entry *proc_mwlan;
#endif
	/** Firmware */
	const struct firmware *firmware;
	/** Firmware request start time */
	struct timeval req_fw_time;
	/** Init config file */
	const struct firmware *init_cfg_data;
	/** Init config file */
	const struct firmware *user_data;
	/** Init user configure wait queue token */
	t_u16 init_user_conf_wait_flag;
	/** Init user configure file wait queue */
	wait_queue_head_t init_user_conf_wait_q __ATTRIB_ALIGN__;
	/** dpd config file */
	const struct firmware *dpd_data;
	/** txpwr data file */
	const struct firmware *txpwr_data;
	/** Hotplug device */
	struct device *hotplug_device;
	/** STATUS variables */
	MOAL_HARDWARE_STATUS hardware_status;
	BOOLEAN fw_reload;
	/** POWER MANAGEMENT AND PnP SUPPORT */
	BOOLEAN surprise_removed;
	/** Firmware release number */
	t_u32 fw_release_number;
	/** ECSA support */
	t_u8 fw_ecsa_enable;
	/** FW ROAMING support */
	t_u8 fw_roam_enable;
	/** FW ROAMING capability in fw */
	t_u8 fw_roaming_support;
	/** Retry count for auto reconnect based on FW ROAMING*/
	t_u16 auto_reconnect_retry_count;
	/** The SSID for auto reconnect FW ROAMING*/
	mlan_802_11_ssid auto_reconnect_ssid;
	/** The BSSID for auto reconnect FW ROAMING*/
	mlan_802_11_mac_addr auto_reconnect_bssid;
	/** The parameters for FW  ROAMING*/
	woal_roam_offload_cfg fw_roam_params;
	/** The keys for FW  ROAMING*/
	mlan_ds_passphrase ssid_passphrase[MAX_SEC_SSID_NUM];

	/** Getlog support */
	t_u8 fw_getlog_enable;
	/** Init wait queue token */
	t_u16 init_wait_q_woken;
	/** Init wait queue */
	wait_queue_head_t init_wait_q __ATTRIB_ALIGN__;
#if defined(SDIO_SUSPEND_RESUME)
	/** Device suspend flag */
	BOOLEAN is_suspended;
#ifdef SDIO_SUSPEND_RESUME
	/** suspend notify flag */
	BOOLEAN suspend_notify_req;
#endif
	/** Host Sleep activated flag */
	t_u8 hs_activated;
	/** Host Sleep activated event wait queue token */
	t_u16 hs_activate_wait_q_woken;
	/** Host Sleep activated event wait queue */
	wait_queue_head_t hs_activate_wait_q __ATTRIB_ALIGN__;
    /** auto_arp and ipv6 offload enable/disable flag */
	t_u8 hs_auto_arp;
#endif
	/** Card pointer */
	t_void *card;
	/** Rx pending in MLAN */
	atomic_t rx_pending;
	/** Tx packet pending count in mlan */
	atomic_t tx_pending;
	/** IOCTL pending count in mlan */
	atomic_t ioctl_pending;
	/** lock count */
	atomic_t lock_count;
	/** Malloc count */
	atomic_t malloc_count;
	/** vmalloc count */
	atomic_t vmalloc_count;
	/** mlan buffer alloc count */
	atomic_t mbufalloc_count;
#if defined(SDIO_SUSPEND_RESUME)
	/** hs skip count */
	t_u32 hs_skip_count;
	/** hs force count */
	t_u32 hs_force_count;
	/** suspend_fail flag */
	BOOLEAN suspend_fail;
#endif
#ifdef REASSOCIATION
	/** Re-association thread */
	moal_thread reassoc_thread;
	/** Re-association timer set flag */
	BOOLEAN is_reassoc_timer_set;
	/** Re-association timer */
	moal_drv_timer reassoc_timer __ATTRIB_ALIGN__;
	/**  */
	struct semaphore reassoc_sem;
	/** Bitmap for re-association on/off */
	t_u8 reassoc_on;
#endif				/* REASSOCIATION */
	/** Driver workqueue */
	struct workqueue_struct *workqueue;
	/** main work */
	struct work_struct main_work;
    /** Driver workqueue */
	struct workqueue_struct *rx_workqueue;
	/** main work */
	struct work_struct rx_work;
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	struct wiphy *wiphy;
	/** Country code for regulatory domain */
	t_u8 country_code[COUNTRY_CODE_LEN];
    /** band */
	enum ieee80211_band band;
    /** first scan done flag */
	t_u8 first_scan_done;
    /** scan channel gap */
	t_u16 scan_chan_gap;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
    /** remain on channel flag */
	t_u8 remain_on_channel;
	/** bss index for remain on channel */
	t_u8 remain_bss_index;
	/** remain_on_channel timer set flag */
	BOOLEAN is_remain_timer_set;
	/** remani_on_channel_timer */
	moal_drv_timer remain_timer __ATTRIB_ALIGN__;
	/** ieee802_11_channel */
	struct ieee80211_channel chan;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	/** channel type */
	enum nl80211_channel_type channel_type;
#endif
	/** cookie */
	t_u64 cookie;
#endif
#ifdef WIFI_DIRECT_SUPPORT
    /** NoA duration */
	t_u32 noa_duration;
    /** NoA interval */
	t_u32 noa_interval;
    /** miracast mode */
	t_u8 miracast_mode;
	/** scan time in miracast mode */
	t_u16 miracast_scan_time;

	/** GO timer set flag */
	BOOLEAN is_go_timer_set;
	/** GO timer */
	moal_drv_timer go_timer __ATTRIB_ALIGN__;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	/** cfg80211_suspend status */
	t_u8 cfg80211_suspend;
#endif
#endif
	/** Read SDIO registers for debugging */
	t_u32 sdio_reg_dbg;
	/** Netlink kernel socket */
	struct sock *nl_sk;
	/** Netlink kernel socket number */
	t_u32 netlink_num;
    /** w_stats wait queue token */
	BOOLEAN meas_wait_q_woken;
    /** w_stats wait queue */
	wait_queue_head_t meas_wait_q __ATTRIB_ALIGN__;
    /** Measurement start jiffes */
	long meas_start_jiffies;
    /** CAC checking period flag */
	BOOLEAN cac_period;
    /** CAC timer jiffes */
	long cac_timer_jiffies;
    /** BSS START command delay executing flag */
	BOOLEAN delay_bss_start;
    /** SSID,BSSID parameter of delay executing */
	mlan_ssid_bssid delay_ssid_bssid;
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	/* CAC channel info */
	struct cfg80211_chan_def dfs_channel;
	/* time set flag */
	BOOLEAN is_cac_timer_set;
    /** cac_timer */
	moal_drv_timer cac_timer __ATTRIB_ALIGN__;
	/** cac bss index */
	t_u8 cac_bss_index;
#endif
#endif
#if defined(UAP_SUPPORT)
/** channel switch wait queue token */
	BOOLEAN chsw_wait_q_woken;
    /** channel switch wait queue */
	wait_queue_head_t chsw_wait_q __ATTRIB_ALIGN__;
#endif
#ifdef DFS_TESTING_SUPPORT
    /** cac period length, valid only when dfs testing is enabled */
	long cac_period_jiffies;
#endif
    /** handle index - for multiple card supports */
	t_u8 handle_idx;
#ifdef SDIO_MMC_DEBUG
	/** cmd53 write state */
	u8 cmd53w;
	/** cmd53 read state */
	u8 cmd53r;
#endif
#ifdef STA_SUPPORT
	/** Scan pending on blocked flag */
	t_u8 scan_pending_on_block;
    /** Scan Private pointer */
	moal_private *scan_priv;
	/** Async scan semaphore */
	struct semaphore async_sem;
#ifdef STA_CFG80211
	/** CFG80211 scan request description */
	struct cfg80211_scan_request *scan_request;
#endif
#endif
	/** main state */
	t_u8 main_state;
    /** driver state */
	t_u8 driver_state;
    /** ioctl timeout */
	t_u8 ioctl_timeout;
    /** FW dump state */
	t_u8 fw_dump;
	/** cmd52 function */
	t_u8 cmd52_func;
	/** cmd52 register */
	t_u8 cmd52_reg;
	/** cmd52 value */
	t_u8 cmd52_val;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	/** spinlock to stop_queue/wake_queue*/
	spinlock_t queue_lock;
#endif
	/** Driver spin lock */
	spinlock_t driver_lock;
	/** Driver ioctl spin lock */
	spinlock_t ioctl_lock;
	/** lock for scan_request */
	spinlock_t scan_req_lock;
	/** Card specific driver version */
	t_s8 driver_version[MLAN_MAX_VER_STR_LEN];
	char *fwdump_fname;
#ifdef ANDROID_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
	struct wakeup_source ws;
#else
	struct wake_lock wake_lock;
#endif
#endif
	t_u16 dfs_repeater_mode;
	t_u8 histogram_table_num;
	struct notifier_block woal_notifier;
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	struct ieee80211_regdomain *regd;
#endif
#endif
	struct net_device napi_dev;
	struct napi_struct napi_rx;
};
/**
 *  @brief set trans_start for each TX queue.
 *
 *  @param dev		A pointer to net_device structure
 *
 *  @return			N/A
 */
static inline void
woal_set_trans_start(struct net_device *dev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
	unsigned int i;
	for (i = 0; i < dev->num_tx_queues; i++)
		netdev_get_tx_queue(dev, i)->trans_start = jiffies;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	dev->trans_start = jiffies;
#else
	netif_trans_update(dev);
#endif
}

/**
 *  @brief Start queue
 *
 *  @param dev		A pointer to net_device structure
 *
 *  @return			N/A
 */
static inline void
woal_start_queue(struct net_device *dev)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 29)
	netif_start_queue(dev);
#else
	if (dev->reg_state == NETREG_REGISTERED)
		netif_tx_wake_all_queues(dev);
	else
		netif_tx_start_all_queues(dev);
#endif
}

/**
 *  @brief Stop queue
 *
 *  @param dev		A pointer to net_device structure
 *
 *  @return			N/A
 */
static inline void
woal_stop_queue(struct net_device *dev)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	unsigned long flags;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	spin_lock_irqsave(&priv->phandle->queue_lock, flags);
	woal_set_trans_start(dev);
	if (!netif_queue_stopped(dev))
		netif_tx_stop_all_queues(dev);
	spin_unlock_irqrestore(&priv->phandle->queue_lock, flags);
#else
	woal_set_trans_start(dev);
	if (!netif_queue_stopped(dev))
		netif_stop_queue(dev);
#endif
}

/**
 *  @brief wake queue
 *
 *  @param dev		A pointer to net_device structure
 *
 *  @return			N/A
 */
static inline void
woal_wake_queue(struct net_device *dev)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	unsigned long flags;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	spin_lock_irqsave(&priv->phandle->queue_lock, flags);
	if (netif_queue_stopped(dev))
		netif_tx_wake_all_queues(dev);
	spin_unlock_irqrestore(&priv->phandle->queue_lock, flags);
#else
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
#endif
}

/** Debug Macro definition*/
#ifdef	DEBUG_LEVEL1
extern t_u32 drvdbg;

#define LOG_CTRL(level)     (0)

#ifdef	DEBUG_LEVEL2
#define	PRINTM_MINFO(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MINFO) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MWARN(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MWARN) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MENTRY(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MENTRY) printk(KERN_DEBUG msg); \
} while (0)
#else
#define	PRINTM_MINFO(level, msg...)  do {} while (0)
#define	PRINTM_MWARN(level, msg...)  do {} while (0)
#define	PRINTM_MENTRY(level, msg...) do {} while (0)
#endif /* DEBUG_LEVEL2 */

#define	PRINTM_MFW_D(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MFW_D) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MCMD_D(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MCMD_D) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MDAT_D(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MDAT_D) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MIF_D(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MIF_D) printk(KERN_DEBUG msg); \
} while (0)

#define	PRINTM_MIOCTL(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MIOCTL) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MINTR(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MINTR) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MEVENT(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MEVENT) printk(msg); \
} while (0)
#define	PRINTM_MCMND(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MCMND) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MDATA(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MDATA) printk(KERN_DEBUG msg); \
} while (0)
#define	PRINTM_MERROR(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MERROR) printk(KERN_ERR msg); \
} while (0)
#define	PRINTM_MFATAL(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MFATAL) printk(KERN_ERR msg); \
} while (0)
#define	PRINTM_MMSG(level, msg...) do { \
	woal_print(level, msg); \
	if (drvdbg & MMSG) printk(KERN_ALERT msg); \
} while (0)

static inline void
woal_print(t_u32 level, char *fmt, ...)
{
}

#define	PRINTM(level, msg...) PRINTM_##level(level, msg)

#else

#define	PRINTM(level, msg...) do {} while (0)

#endif /* DEBUG_LEVEL1 */

/** Wait until a condition becomes true */
#define MASSERT(cond)                   \
do {                                    \
	if (!(cond)) {                      \
	    PRINTM(MFATAL, "ASSERT: %s: %i\n", __func__, __LINE__); \
	    panic("Assert failed: Panic!"); \
	}                                   \
} while (0)

/** Log entry point for debugging */
#define	ENTER()			PRINTM(MENTRY, "Enter: %s\n", \
									__func__)
/** Log exit point for debugging */
#define	LEAVE()			PRINTM(MENTRY, "Leave: %s\n", \
									__func__)

#ifdef DEBUG_LEVEL1
#define DBG_DUMP_BUF_LEN	64
#define MAX_DUMP_PER_LINE	16

static inline void
hexdump(t_u32 level, char *prompt, t_u8 *buf, int len)
{
	int i;
	char dbgdumpbuf[DBG_DUMP_BUF_LEN];
	char *ptr = dbgdumpbuf;

	if (drvdbg & level)
		printk(KERN_DEBUG "%s:\n", prompt);
	for (i = 1; i <= len; i++) {
		ptr += snprintf(ptr, 4, "%02x ", *buf);
		buf++;
		if (i % MAX_DUMP_PER_LINE == 0) {
			*ptr = 0;
			if (drvdbg & level)
				printk(KERN_DEBUG "%s\n", dbgdumpbuf);
			ptr = dbgdumpbuf;
		}
	}
	if (len % MAX_DUMP_PER_LINE) {
		*ptr = 0;
		if (drvdbg & level)
			printk(KERN_DEBUG "%s\n", dbgdumpbuf);
	}
}

#define DBG_HEXDUMP_MERROR(x, y, z) do { \
	if ((drvdbg & MERROR) || LOG_CTRL(MERROR)) \
		hexdump(MERROR, x, y, z); \
} while (0)
#define DBG_HEXDUMP_MCMD_D(x, y, z) do { \
	if ((drvdbg & MCMD_D) || LOG_CTRL(MCMD_D)) \
		hexdump(MCMD_D, x, y, z); \
} while (0)
#define DBG_HEXDUMP_MDAT_D(x, y, z) do { \
	if ((drvdbg & MDAT_D) || LOG_CTRL(MDAT_D)) \
		hexdump(MDAT_D, x, y, z); \
} while (0)
#define DBG_HEXDUMP_MIF_D(x, y, z) do { \
	if ((drvdbg & MIF_D)  || LOG_CTRL(MIF_D)) \
		hexdump(MIF_D, x, y, z); \
} while (0)
#define DBG_HEXDUMP_MEVT_D(x, y, z) do { \
	if ((drvdbg & MEVT_D) || LOG_CTRL(MEVT_D)) \
		hexdump(MEVT_D, x, y, z); \
} while (0)
#define DBG_HEXDUMP_MFW_D(x, y, z) do { \
	if ((drvdbg & MFW_D)  || LOG_CTRL(MFW_D)) \
		hexdump(MFW_D, x, y, z); \
} while (0)
#define	DBG_HEXDUMP(level, x, y, z)    DBG_HEXDUMP_##level(x, y, z)

#else
/** Do nothing since debugging is not turned on */
#define DBG_HEXDUMP(level, x, y, z)    do {} while (0)
#endif

#ifdef DEBUG_LEVEL2
#define HEXDUMP(x, y, z) do { \
	if ((drvdbg & MINFO) || LOG_CTRL(MINFO)) \
		hexdump(MINFO, x, y, z); \
} while (0)
#else
/** Do nothing since debugging is not turned on */
#define HEXDUMP(x, y, z)            do {} while (0)
#endif

#ifdef BIG_ENDIAN_SUPPORT
/** Convert from 16 bit little endian format to CPU format */
#define woal_le16_to_cpu(x) le16_to_cpu(x)
/** Convert from 32 bit little endian format to CPU format */
#define woal_le32_to_cpu(x) le32_to_cpu(x)
/** Convert from 64 bit little endian format to CPU format */
#define woal_le64_to_cpu(x) le64_to_cpu(x)
/** Convert to 16 bit little endian format from CPU format */
#define woal_cpu_to_le16(x) cpu_to_le16(x)
/** Convert to 32 bit little endian format from CPU format */
#define woal_cpu_to_le32(x) cpu_to_le32(x)
/** Convert to 64 bit little endian format from CPU format */
#define woal_cpu_to_le64(x) cpu_to_le64(x)
#else
/** Do nothing */
#define woal_le16_to_cpu(x) x
/** Do nothing */
#define woal_le32_to_cpu(x) x
/** Do nothing */
#define woal_le64_to_cpu(x) x
/** Do nothing */
#define woal_cpu_to_le16(x) x
/** Do nothing */
#define woal_cpu_to_le32(x) x
/** Do nothing */
#define woal_cpu_to_le64(x) x
#endif

/**
 *  @brief This function returns first available priv
 *  based on the BSS role
 *
 *  @param handle    A pointer to moal_handle
 *  @param bss_role  BSS role or MLAN_BSS_ROLE_ANY
 *
 *  @return          Pointer to moal_private
 */
static inline moal_private *
woal_get_priv(moal_handle *handle, mlan_bss_role bss_role)
{
	int i;

	for (i = 0; i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM); i++) {
		if (handle->priv[i]) {
			if (bss_role == MLAN_BSS_ROLE_ANY ||
			    GET_BSS_ROLE(handle->priv[i]) == bss_role)
				return handle->priv[i];
		}
	}
	return NULL;
}

/**
 *  @brief This function returns first available priv
 *  based on the BSS type
 *
 *  @param handle    A pointer to moal_handle
 *  @param bss_type  BSS type or MLAN_BSS_TYPE_ANY
 *
 *  @return          Pointer to moal_private
 */
static inline moal_private *
woal_get_priv_bss_type(moal_handle *handle, mlan_bss_type bss_type)
{
	int i;

	for (i = 0; i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM); i++) {
		if (handle->priv[i]) {
			if (bss_type == MLAN_BSS_TYPE_ANY ||
			    handle->priv[i]->bss_type == bss_type)
				return handle->priv[i];
		}
	}
	return NULL;
}

/* CAC Measure report default time 60 seconds */
#define MEAS_REPORT_TIME (60 * HZ)

/** Max line length allowed in init config file */
#define MAX_LINE_LEN        256
/** Max MAC address string length allowed */
#define MAX_MAC_ADDR_LEN    18
/** Max register type/offset/value etc. parameter length allowed */
#define MAX_PARAM_LEN       12

/** HostCmd_CMD_CFG_DATA for CAL data */
#define HostCmd_CMD_CFG_DATA 0x008f
/** HostCmd action set */
#define HostCmd_ACT_GEN_SET 0x0001
/** HostCmd CAL data header length */
#define CFG_DATA_HEADER_LEN	6

typedef struct _HostCmd_DS_GEN {
	t_u16 command;
	t_u16 size;
	t_u16 seq_num;
	t_u16 result;
} HostCmd_DS_GEN;

typedef struct _HostCmd_DS_802_11_CFG_DATA {
    /** Action */
	t_u16 action;
    /** Type */
	t_u16 type;
    /** Data length */
	t_u16 data_len;
    /** Data */
	t_u8 data[1];
} __ATTRIB_PACK__ HostCmd_DS_802_11_CFG_DATA;

/** combo scan header */
#define WEXT_CSCAN_HEADER		"CSCAN S\x01\x00\x00S\x00"
/** combo scan header size */
#define WEXT_CSCAN_HEADER_SIZE		12
/** combo scan ssid section */
#define WEXT_CSCAN_SSID_SECTION		'S'
/** commbo scan channel section */
#define WEXT_CSCAN_CHANNEL_SECTION	'C'
/** commbo scan passive dwell section */
#define WEXT_CSCAN_PASV_DWELL_SECTION	'P'
/** commbo scan home dwell section */
#define WEXT_CSCAN_HOME_DWELL_SECTION	'H'
/** BGSCAN RSSI section */
#define WEXT_BGSCAN_RSSI_SECTION	 'R'
/** BGSCAN SCAN INTERVAL SECTION */
#define WEXT_BGSCAN_INTERVAL_SECTION 'T'
/** BGSCAN REPEAT SECTION */
#define WEXT_BGSCAN_REPEAT_SECTION  'E'
/** Min BGSCAN interval 30 second */
#define MIN_BGSCAN_INTERVAL	 30000
/** default repeat count */
#define DEF_REPEAT_COUNT	 6

/** default rssi low threshold */
#define DEFAULT_RSSI_LOW_THRESHOLD 70
/** RSSI HYSTERSIS */
#define RSSI_HYSTERESIS		6
/** lowest rssi threshold */
#define LOWEST_RSSI_THRESHOLD	82
/** delta rssi */
#define DELTA_RSSI 10

/** NL80211 scan configuration header */
#define NL80211_SCANCFG_HEADER		"SCAN-CFG "
/** NL80211 scan configuration header length */
#define NL80211_SCANCFG_HEADER_SIZE		9
/** NL80211 scan configuration active scan section */
#define NL80211_SCANCFG_ACTV_DWELL_SECTION	'A'
/** NL80211 scan configuration passive scan section */
#define NL80211_SCANCFG_PASV_DWELL_SECTION	'P'
/** NL80211 scan configuration specific scan section */
#define NL80211_SCANCFG_SPCF_DWELL_SECTION	'S'

/** band AUTO */
#define	WIFI_FREQUENCY_BAND_AUTO		0
/** band 5G */
#define	WIFI_FREQUENCY_BAND_5GHZ        1
/** band 2G */
#define	WIFI_FREQUENCY_BAND_2GHZ		2
/** All band */
#define WIFI_FREQUENCY_ALL_BAND         3

/** Rx filter: IPV4 multicast */
#define RX_FILTER_IPV4_MULTICAST        1
/** Rx filter: broadcast */
#define RX_FILTER_BROADCAST             2
/** Rx filter: unicast */
#define RX_FILTER_UNICAST               4
/** Rx filter: IPV6 multicast */
#define RX_FILTER_IPV6_MULTICAST        8

/**  Convert ASCII string to hex value */
int woal_ascii2hex(t_u8 *d, char *s, t_u32 dlen);
/** parse ie */
const t_u8 *woal_parse_ie_tlv(const t_u8 *ie, int len, t_u8 id);
/**  Convert mac address from string to t_u8 buffer */
void woal_mac2u8(t_u8 *mac_addr, char *buf);
/**  Extract token from string */
char *woal_strsep(char **s, char delim, char esc);
/** Return int value of a given ASCII string */
mlan_status woal_atoi(int *data, char *a);
/** Return hex value of a given ASCII string */
int woal_atox(char *a);
/** Allocate buffer */
pmlan_buffer woal_alloc_mlan_buffer(moal_handle *handle, int size);
/** Allocate IOCTL request buffer */
pmlan_ioctl_req woal_alloc_mlan_ioctl_req(int size);
/** Free buffer */
void woal_free_mlan_buffer(moal_handle *handle, pmlan_buffer pmbuf);
/** Get private structure of a BSS by index */
moal_private *woal_bss_index_to_priv(moal_handle *handle, t_u8 bss_index);
/* Functions in interface module */
/** Add card */
moal_handle *woal_add_card(void *card);
/** Remove card */
mlan_status woal_remove_card(void *card);
/** broadcast event */
mlan_status woal_broadcast_event(moal_private *priv, t_u8 *payload, t_u32 len);
#ifdef CONFIG_PROC_FS
/** switch driver mode */
mlan_status woal_switch_drv_mode(moal_handle *handle, t_u32 mode);
#endif

/** Interrupt handler */
mlan_status woal_interrupt(moal_handle *handle);

/** check if any interface is up */
t_u8 woal_is_any_interface_active(moal_handle *handle);
/** Get version */
void woal_get_version(moal_handle *handle, char *version, int maxlen);
/** Get Driver Version */
int woal_get_driver_version(moal_private *priv, struct ifreq *req);
/** Get extended driver version */
int woal_get_driver_verext(moal_private *priv, struct ifreq *ireq);
/** check driver status */
t_u8 woal_check_driver_status(moal_handle *handle);
/** Mgmt frame forward registration */
int woal_reg_rx_mgmt_ind(moal_private *priv, t_u16 action,
			 t_u32 *pmgmt_subtype_mask, t_u8 wait_option);
#ifdef DEBUG_LEVEL1
/** Set driver debug bit masks */
int woal_set_drvdbg(moal_private *priv, t_u32 drvdbg);
#endif

mlan_status woal_set_get_tx_bf_cap(moal_private *priv, t_u16 action,
				   t_u32 *tx_bf_cap);
/** Set/Get TX beamforming configurations */
mlan_status woal_set_get_tx_bf_cfg(moal_private *priv, t_u16 action,
				   mlan_ds_11n_tx_bf_cfg *bf_cfg);
/** Request MAC address setting */
mlan_status woal_request_set_mac_address(moal_private *priv);
/** Request multicast list setting */
void woal_request_set_multicast_list(moal_private *priv,
				     struct net_device *dev);
/** Request IOCTL action */
mlan_status woal_request_ioctl(moal_private *priv, mlan_ioctl_req *req,
			       t_u8 wait_option);
#ifdef CONFIG_PROC_FS
mlan_status woal_request_soft_reset(moal_handle *handle);
#endif
void woal_request_fw_reload(moal_handle *handle, t_u8 mode);
/** driver initial the fw reset */
#define FW_RELOAD_SDIO_INBAND_RESET   1
/** out band reset trigger reset, no interface re-emulation */
#define FW_RELOAD_NO_EMULATION  2
/** out band reset with interface re-emulation */
#define FW_RELOAD_WITH_EMULATION 3

#ifdef PROC_DEBUG
/** Get debug information */
mlan_status woal_get_debug_info(moal_private *priv, t_u8 wait_option,
				mlan_debug_info *debug_info);
/** Set debug information */
mlan_status woal_set_debug_info(moal_private *priv, t_u8 wait_option,
				mlan_debug_info *debug_info);
#endif
/** Disconnect */
mlan_status woal_disconnect(moal_private *priv, t_u8 wait_option, t_u8 *mac,
			    t_u16 reason_code);
/** associate */
mlan_status woal_bss_start(moal_private *priv, t_u8 wait_option,
			   mlan_ssid_bssid *ssid_bssid);
/** Request firmware information */
mlan_status woal_request_get_fw_info(moal_private *priv, t_u8 wait_option,
				     mlan_fw_info *fw_info);
#ifdef STA_SUPPORT
/** Request Exented Capability information */
int woal_request_extcap(moal_private *priv, t_u8 *buf, t_u8 len);
#endif
mlan_status woal_set_get_dtim_period(moal_private *priv,
				     t_u32 action, t_u8 wait_option,
				     t_u8 *value);
/** Set/get Host Sleep parameters */
mlan_status woal_set_get_hs_params(moal_private *priv, t_u16 action,
				   t_u8 wait_option, mlan_ds_hs_cfg *hscfg);
/** Cancel Host Sleep configuration */
mlan_status woal_cancel_hs(moal_private *priv, t_u8 wait_option);
#if defined(SDIO_SUSPEND_RESUME)
/** Enable Host Sleep configuration */
int woal_enable_hs(moal_private *priv);
/** hs active timeout 2 second */
#define HS_ACTIVE_TIMEOUT  (2 * HZ)
#endif
/** Get wakeup reason */
mlan_status woal_get_wakeup_reason(moal_private *priv,
				   mlan_ds_hs_wakeup_reason *wakeup_reason);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
void woal_create_dump_dir(moal_handle *phandle, char *dir_buf, int buf_size);
#endif
mlan_status woal_save_dump_info_to_file(char *dir_name, char *file_name,
					t_u8 *buf, t_u32 buf_len);
void woal_dump_drv_info(moal_handle *phandle, t_u8 *dir_name);

void woal_dump_firmware_info_v3(moal_handle *phandle);
/* Store the FW dumps received from events in a file */
void woal_store_firmware_dump(moal_handle *phandle, mlan_event *pmevent);

/** get deep sleep */
int woal_get_deep_sleep(moal_private *priv, t_u32 *data);
/** set deep sleep */
int woal_set_deep_sleep(moal_private *priv, t_u8 wait_option,
			BOOLEAN bdeep_sleep, t_u16 idletime);
/** process hang */
void woal_process_hang(moal_handle *handle);
/** Get BSS information */
mlan_status woal_get_bss_info(moal_private *priv, t_u8 wait_option,
			      mlan_bss_info *bss_info);
void woal_process_ioctl_resp(moal_private *priv, mlan_ioctl_req *req);
/** Set/Get generic element */
mlan_status woal_set_get_gen_ie(moal_private *priv, t_u32 action, t_u8 *ie,
				int *ie_len, t_u8 wait_option);
char *region_code_2_string(t_u8 region_code);
t_bool woal_is_etsi_country(t_u8 *country_code);
t_u8 woal_is_valid_alpha2(char *alpha2);
#ifdef STA_SUPPORT
void woal_send_disconnect_to_system(moal_private *priv);
void woal_send_mic_error_event(moal_private *priv, t_u32 event);
void woal_ioctl_get_bss_resp(moal_private *priv, mlan_ds_bss *bss);
void woal_ioctl_get_info_resp(moal_private *priv, mlan_ds_get_info *info);
mlan_status woal_get_assoc_rsp(moal_private *priv,
			       mlan_ds_misc_assoc_rsp *assoc_rsp,
			       t_u8 wait_option);
/** Get signal information */
mlan_status woal_get_signal_info(moal_private *priv, t_u8 wait_option,
				 mlan_ds_get_signal *signal);
/** Get mode */
t_u32 woal_get_mode(moal_private *priv, t_u8 wait_option);
mlan_status woal_get_sta_channel(moal_private *priv, t_u8 wait_option,
				 chan_band_info * channel);
#ifdef STA_WEXT
/** Get data rates */
mlan_status woal_get_data_rates(moal_private *priv, t_u8 wait_option,
				moal_802_11_rates *m_rates);
void woal_send_iwevcustom_event(moal_private *priv, char *str);
/** Get channel list */
mlan_status woal_get_channel_list(moal_private *priv, t_u8 wait_option,
				  mlan_chan_list *chanlist);
mlan_status woal_11d_check_ap_channel(moal_private *priv, t_u8 wait_option,
				      mlan_ssid_bssid *ssid_bssid);
#endif
/** Set/Get retry count */
mlan_status woal_set_get_retry(moal_private *priv, t_u32 action,
			       t_u8 wait_option, int *value);
/** Set/Get RTS threshold */
mlan_status woal_set_get_rts(moal_private *priv, t_u32 action, t_u8 wait_option,
			     int *value);
/** Set/Get fragment threshold */
mlan_status woal_set_get_frag(moal_private *priv, t_u32 action,
			      t_u8 wait_option, int *value);
/** Set/Get TX power */
mlan_status woal_set_get_tx_power(moal_private *priv, t_u32 action,
				  mlan_power_cfg_t *pwr);
/** Set/Get power IEEE management */
mlan_status woal_set_get_power_mgmt(moal_private *priv, t_u32 action,
				    int *disabled, int type, t_u8 wait_option);
/** Get data rate */
mlan_status woal_set_get_data_rate(moal_private *priv, t_u8 action,
				   mlan_rate_cfg_t *datarate);
/** Request a network scan */
mlan_status woal_request_scan(moal_private *priv, t_u8 wait_option,
			      mlan_802_11_ssid *req_ssid);
/** Set radio on/off */
int woal_set_radio(moal_private *priv, t_u8 option);
/** Set region code */
mlan_status woal_set_region_code(moal_private *priv, char *region);
/** Set authentication mode */
mlan_status woal_set_auth_mode(moal_private *priv, t_u8 wait_option,
			       t_u32 auth_mode);
/** Set encryption mode */
mlan_status woal_set_encrypt_mode(moal_private *priv, t_u8 wait_option,
				  t_u32 encrypt_mode);
/** Enable wep key */
mlan_status woal_enable_wep_key(moal_private *priv, t_u8 wait_option);
/** Set WPA enable */
mlan_status woal_set_wpa_enable(moal_private *priv, t_u8 wait_option,
				t_u32 enable);

/** cancel scan command */
mlan_status woal_cancel_scan(moal_private *priv, t_u8 wait_option);
/** Find best network to connect */
mlan_status woal_find_best_network(moal_private *priv, t_u8 wait_option,
				   mlan_ssid_bssid *ssid_bssid);
/** Set Ad-Hoc channel */
mlan_status woal_change_adhoc_chan(moal_private *priv, int channel,
				   t_u8 wait_option);

/** Get scan table */
mlan_status woal_get_scan_table(moal_private *priv, t_u8 wait_option,
				mlan_scan_resp *scanresp);
/** Get authentication mode */
mlan_status woal_get_auth_mode(moal_private *priv, t_u8 wait_option,
			       t_u32 *auth_mode);
/** Get encryption mode */
mlan_status woal_get_encrypt_mode(moal_private *priv, t_u8 wait_option,
				  t_u32 *encrypt_mode);
/** Get WPA state */
mlan_status woal_get_wpa_enable(moal_private *priv, t_u8 wait_option,
				t_u32 *enable);
#endif	/**STA_SUPPORT */
mlan_status woal_set_11d(moal_private *priv, t_u8 wait_option, t_u8 enable);

#if defined(STA_SUPPORT) || defined(UAP_SUPPORT)
/** Get statistics information */
mlan_status woal_get_stats_info(moal_private *priv, t_u8 wait_option,
				mlan_ds_get_stats *stats);
#endif	/**STA_SUPPORT||UAP_SUPPORT*/

mlan_status woal_set_wapi_enable(moal_private *priv, t_u8 wait_option,
				 t_u32 enable);

/** Initialize priv */
void woal_init_priv(moal_private *priv, t_u8 wait_option);
/** Reset interface(s) */
int woal_reset_intf(moal_private *priv, t_u8 wait_option, int all_intf);
#define TLV_TYPE_MGMT_IE            (0x169)
#define MGMT_MASK_ASSOC_REQ             0x01
#define MGMT_MASK_REASSOC_REQ           0x04
#define MGMT_MASK_ASSOC_RESP            0x02
#define MGMT_MASK_REASSOC_RESP          0x08
#define MGMT_MASK_PROBE_REQ             0x10
#define MGMT_MASK_PROBE_RESP            0x20
#define MGMT_MASK_BEACON                0x100
#define MGMT_MASK_ASSOC_RESP_QOS_MAP        0x4000
#define MGMT_MASK_BEACON_WPS_P2P        0x8000
/** common ioctl for uap, station */
int woal_custom_ie_ioctl(struct net_device *dev, struct ifreq *req);
int woal_send_host_packet(struct net_device *dev, struct ifreq *req);
/** Private command ID to pass mgmt frame */
#define WOAL_MGMT_FRAME_TX_IOCTL          (SIOCDEVPRIVATE + 12)
/** common ioctl for TDLS */
int woal_tdls_config_ioctl(struct net_device *dev, struct ifreq *req);

int woal_get_bss_type(struct net_device *dev, struct ifreq *req);
#if defined(STA_WEXT) || defined(UAP_WEXT)
int woal_host_command(moal_private *priv, struct iwreq *wrq);
#endif
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
mlan_status woal_bss_role_cfg(moal_private *priv, t_u8 action,
			      t_u8 wait_option, t_u8 *bss_role);
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
void woal_go_timer_func(void *context);
#endif
#if defined(STA_WEXT) || defined(UAP_WEXT)
int woal_set_get_bss_role(moal_private *priv, struct iwreq *wrq);
#endif
#endif
#if defined(WIFI_DIRECT_SUPPORT) || defined(UAP_SUPPORT)
/** hostcmd ioctl for uap, wifidirect */
int woal_hostcmd_ioctl(struct net_device *dev, struct ifreq *req);
#endif

mlan_status woal_set_remain_channel_ioctl(moal_private *priv, t_u8 wait_option,
					  mlan_ds_remain_chan *pchan);
void woal_remain_timer_func(void *context);

#if defined(WIFI_DIRECT_SUPPORT)
mlan_status woal_wifi_direct_mode_cfg(moal_private *priv, t_u16 action,
				      t_u16 *mode);
mlan_status woal_p2p_config(moal_private *priv, t_u32 action,
			    mlan_ds_wifi_direct_config *p2p_config);
#endif /* WIFI_DIRECT_SUPPORT */

int woal_11h_cancel_chan_report_ioctl(moal_private *priv, t_u8 wait_option);

#ifdef CONFIG_PROC_FS
/** Initialize proc fs */
void woal_proc_init(moal_handle *handle);
/** Clean up proc fs */
void woal_proc_exit(moal_handle *handle);
/** Create proc entry */
void woal_create_proc_entry(moal_private *priv);
/** Remove proc entry */
void woal_proc_remove(moal_private *priv);
/** string to number */
int woal_string_to_number(char *s);
#endif

#ifdef PROC_DEBUG
/** Create debug proc fs */
void woal_debug_entry(moal_private *priv);
/** Remove debug proc fs */
void woal_debug_remove(moal_private *priv);
#endif /* PROC_DEBUG */

/** check pm info */
mlan_status woal_get_pm_info(moal_private *priv, mlan_ds_ps_info *pm_info);
/** get mlan debug info */
void woal_mlan_debug_info(moal_private *priv);

#ifdef REASSOCIATION
int woal_reassociation_thread(void *data);
void woal_reassoc_timer_func(void *context);
#endif /* REASSOCIATION */

t_void woal_main_work_queue(struct work_struct *work);
t_void woal_rx_work_queue(struct work_struct *work);

int woal_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
#ifdef STA_SUPPORT
mlan_status woal_init_sta_dev(struct net_device *dev, moal_private *priv);
#endif
#ifdef UAP_SUPPORT
mlan_status woal_init_uap_dev(struct net_device *dev, moal_private *priv);
#endif
mlan_status woal_update_drv_tbl(moal_handle *handle, int drv_mode_local);
moal_private *woal_add_interface(moal_handle *handle, t_u8 bss_num,
				 t_u8 bss_type);
void woal_remove_interface(moal_handle *handle, t_u8 bss_index);
void woal_set_multicast_list(struct net_device *dev);
mlan_status woal_request_fw(moal_handle *handle);
int woal_11h_channel_check_ioctl(moal_private *priv, t_u8 wait_option);
void woal_cancel_cac_block(moal_private *priv);
void woal_moal_debug_info(moal_private *priv, moal_handle *handle, u8 flag);

#ifdef STA_SUPPORT
mlan_status woal_get_powermode(moal_private *priv, int *powermode);
mlan_status woal_set_scan_type(moal_private *priv, t_u32 scan_type);
mlan_status woal_enable_ext_scan(moal_private *priv, t_u8 enable);
mlan_status woal_set_powermode(moal_private *priv, char *powermode);
int woal_find_essid(moal_private *priv, mlan_ssid_bssid *ssid_bssid,
		    t_u8 wait_option);
mlan_status woal_request_userscan(moal_private *priv, t_u8 wait_option,
				  wlan_user_scan_cfg *scan_cfg);
mlan_status woal_do_scan(moal_private *priv, wlan_user_scan_cfg *scan_cfg);
int woal_set_combo_scan(moal_private *priv, char *buf, int length);
mlan_status woal_set_scan_time(moal_private *priv, t_u16 active_scan_time,
			       t_u16 passive_scan_time,
			       t_u16 specific_scan_time);
mlan_status woal_get_band(moal_private *priv, int *band);
mlan_status woal_set_band(moal_private *priv, char *pband);
mlan_status woal_add_rxfilter(moal_private *priv, char *rxfilter);
mlan_status woal_remove_rxfilter(moal_private *priv, char *rxfilter);
mlan_status woal_priv_qos_cfg(moal_private *priv, t_u32 action, char *qos_cfg);
mlan_status woal_set_sleeppd(moal_private *priv, char *psleeppd);
int woal_set_scan_cfg(moal_private *priv, char *buf, int length);
void woal_update_dscp_mapping(moal_private *priv);

/* EVENT: BCN_RSSI_LOW */
#define EVENT_BCN_RSSI_LOW			0x0001
/* EVENT: PRE_BCN_LOST */
#define EVENT_PRE_BCN_LOST			0x0002
mlan_status woal_set_rssi_low_threshold(moal_private *priv, char *rssi,
					t_u8 wait_option);
mlan_status woal_set_rssi_threshold(moal_private *priv, t_u32 event_id,
				    t_u8 wait_option);
/* EVENT: BG_SCAN_REPORT */
#define EVENT_BG_SCAN_REPORT		0x0004
mlan_status woal_set_bg_scan(moal_private *priv, char *buf, int length);
mlan_status woal_stop_bg_scan(moal_private *priv, t_u8 wait_option);
void woal_reconfig_bgscan(moal_handle *handle);
#ifdef STA_CFG80211
void woal_config_bgscan_and_rssi(moal_private *priv, t_u8 set_rssi);
void woal_start_roaming(moal_private *priv);
#endif
#ifdef STA_CFG80211
void woal_save_conn_params(moal_private *priv,
			   struct cfg80211_connect_params *sme);
void woal_clear_conn_params(moal_private *priv);
#endif
mlan_status woal_request_bgscan(moal_private *priv, t_u8 wait_option,
				wlan_bgscan_cfg *scan_cfg);
#endif

void woal_flush_tcp_sess_queue(moal_private *priv);
void woal_flush_tdls_list(moal_private *priv);
void wlan_scan_create_brief_table_entry(t_u8 **ppbuffer,
					BSSDescriptor_t *pbss_desc);
int wlan_get_scan_table_ret_entry(BSSDescriptor_t *pbss_desc, t_u8 **ppbuffer,
				  int *pspace_left);
BOOLEAN woal_ssid_valid(mlan_802_11_ssid *pssid);
int woal_is_connected(moal_private *priv, mlan_ssid_bssid *ssid_bssid);
int woal_priv_hostcmd(moal_private *priv, t_u8 *respbuf, t_u32 respbuflen,
		      t_u8 wait_option);
void woal_flush_tx_stat_queue(moal_private *priv);
struct tx_status_info *woal_get_tx_info(moal_private *priv, t_u8 tx_seq_num);
void woal_remove_tx_info(moal_private *priv, t_u8 tx_seq_num);

mlan_status woal_request_country_power_table(moal_private *priv, char *region);
mlan_status woal_mc_policy_cfg(moal_private *priv, t_u16 *enable,
			       t_u8 wait_option, t_u8 action);
#ifdef RX_PACKET_COALESCE
mlan_status woal_rx_pkt_coalesce_cfg(moal_private *priv, t_u16 *enable,
				     t_u8 wait_option, t_u8 action);
#endif
int woal_hexval(char chr);
mlan_status woal_pmic_configure(moal_handle *handle, t_u8 wait_option);
void woal_hist_data_reset(moal_private *priv);
void woal_hist_do_reset(moal_private *priv, void *data);
void woal_hist_reset_table(moal_private *priv, t_u8 antenna);
void woal_hist_data_add(moal_private *priv, t_u8 rx_rate, t_s8 snr, t_s8 nflr,
			t_u8 antenna);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
mlan_status woal_set_net_monitor(moal_private *priv, t_u8 wait_option,
				 t_u8 enable, t_u8 filter,
				 netmon_band_chan_cfg * band_chan_cfg);
#endif
mlan_status woal_delba_all(moal_private *priv, t_u8 wait_option);

monitor_iface *woal_prepare_mon_if(moal_private *priv,
				   const char *name,
				   unsigned char name_assign_type,
				   int sniffer_mode);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
mlan_status woal_set_rekey_data(moal_private *priv,
				mlan_ds_misc_gtk_rekey_data * gtk_rekey,
				t_u8 action);
#endif
void woal_ioctl_get_misc_conf(moal_private *priv, mlan_ds_misc_cfg *info);
#endif /* _MOAL_MAIN_H */
