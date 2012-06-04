#ifndef __RK30_HDMI_HDCP_H__
#define __RK30_HDMI_HDCP_H__

/***************************/
/* Definitions             */
/***************************/

/* Status / error codes */
#define HDCP_OK			0
#define HDCP_KEY_ERR	1
#define HDCP_KSV_ERR	2

/* Delays */
#define HDCP_ENABLE_DELAY	300
#define HDCP_REAUTH_DELAY	100

/* Event source */
#define HDCP_SRC_SHIFT		8
#define HDCP_IOCTL_SRC		(0x1 << HDCP_SRC_SHIFT)
#define HDCP_HDMI_SRC		(0x2 << HDCP_SRC_SHIFT)
#define HDCP_IRQ_SRC		(0x4 << HDCP_SRC_SHIFT)
#define HDCP_WORKQUEUE_SRC	(0x8 << HDCP_SRC_SHIFT)

/* Event */
#define HDCP_ENABLE_CTL			(HDCP_IOCTL_SRC		| 0)
#define HDCP_DISABLE_CTL		(HDCP_IOCTL_SRC		| 1)
#define HDCP_START_FRAME_EVENT	(HDCP_HDMI_SRC		| 2)
#define HDCP_STOP_FRAME_EVENT	(HDCP_HDMI_SRC		| 3)
#define HDCP_KSV_LIST_RDY_EVENT	(HDCP_IRQ_SRC		| 4)
#define HDCP_FAIL_EVENT			(HDCP_IRQ_SRC		| 5)
#define HDCP_AUTH_PASS_EVENT	(HDCP_IRQ_SRC		| 6)
#define HDCP_AUTH_REATT_EVENT	(HDCP_WORKQUEUE_SRC	| 7)

/* Key size */
#define HDCP_KEY_SIZE			308	

/* Authentication retry times */
#define HDCP_INFINITE_REAUTH	0x100

enum hdcp_states {
	HDCP_DISABLED,
	HDCP_ENABLE_PENDING,
	HDCP_AUTHENTICATION_START,
	HDCP_WAIT_KSV_LIST,
	HDCP_LINK_INTEGRITY_CHECK,
};

enum hdmi_states {
	HDMI_STOPPED,
	HDMI_STARTED
};

#define HDCP_PRIVATE_KEY_SIZE	280
#define HDCP_KEY_SHA_SIZE		20

struct hdcp_keys{
	u8 KSV[8];
	u8 DeviceKey[HDCP_PRIVATE_KEY_SIZE];
	u8 sha1[HDCP_KEY_SHA_SIZE];
};

struct hdcp_delayed_work {
	struct delayed_work work;
	int event;
};

struct hdcp {
	int	enable;
	int retry_times;
	struct hdcp_keys *keys;
	int invalidkey;
	char *invalidkeys;	
	struct mutex lock;
	struct completion	complete;
	struct workqueue_struct *workqueue;
	
	enum hdmi_states hdmi_state;
	enum hdcp_states hdcp_state;
	
	struct delayed_work *pending_start;
	struct delayed_work *pending_wq_event;
	int retry_cnt;
};

extern struct hdcp *hdcp;

#ifdef HDCP_DEBUG
#define DBG(format, ...) \
		printk(KERN_INFO "HDCP: " format "\n", ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

extern void rk30_hdcp_disable(void);
extern int	rk30_hdcp_start_authentication(void);
extern int	rk30_hdcp_check_bksv(void);
extern int	rk30_hdcp_load_key2mem(struct hdcp_keys *key);
#endif /* __RK30_HDMI_HDCP_H__ */